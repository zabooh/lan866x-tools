/*
 * plat_win.c  -  Windows implementation of the narrow platform layer (plat.h).
 *
 * Single-thread / superloop model: NO threads. UDP receive is non-blocking;
 * datagrams are drained and dispatched synchronously from plat_udp_poll(),
 * which the application calls once per superloop iteration. This replaces the
 * old per-socket blocking ReceiveThread (windows-udp-handler.c) and the
 * ServiceThread (someip_stub_win.c).
 *
 * Pure C, Winsock + Iphlpapi. No C++ runtime.
 */
#define _WIN32_WINNT 0x0601   /* at least Windows 7 (GetAdaptersAddresses) */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"

#define MAX_PLAT_SOCKETS  (32)
#define RX_BUF_SIZE       (1600)

#define PRINT(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

struct plat_udp {
    SOCKET          sock;
    plat_udp_rx_cb  rx;
    void           *tag;
    bool            used;
};

static struct plat_udp s_socks[MAX_PLAT_SOCKETS];
static int             s_wsaRefs = 0;   /* WSAStartup ref count */

/* ===================== 1) time base ===================================== */

uint32_t plat_now_ms(void) { return GetTickCount(); }

/* ===================== 2) non-blocking UDP + net ======================= */

static bool wsa_startup(void)
{
    if (s_wsaRefs == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            PRINT("WSAStartup failed with error %d\n", WSAGetLastError());
            return false;
        }
    }
    s_wsaRefs++;
    return true;
}

static void wsa_cleanup(void)
{
    if (s_wsaRefs > 0 && --s_wsaRefs == 0) {
        WSACleanup();
    }
}

plat_udp_t *plat_udp_open(uint16_t *port, plat_udp_rx_cb rx, void *tag)
{
    struct plat_udp *s = NULL;
    SOCKET sock;
    int i;
    char enable = 1;
    int rcvBuf = 1024 * 1024;          /* 1 MB rx buffer (bursty SD/replies)  */
    u_long nonblock = 1;
    SOCKADDR_IN addr;

    if (!port || !rx) return NULL;

    for (i = 0; i < MAX_PLAT_SOCKETS; i++)
        if (!s_socks[i].used) { s = &s_socks[i]; break; }
    if (!s) { PRINT("plat_udp_open: no free socket slot\n"); return NULL; }

    if (!wsa_startup()) return NULL;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        PRINT("plat_udp_open: socket() failed, error=%d\n", WSAGetLastError());
        wsa_cleanup();
        return NULL;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)); /* reuse hanging socket */
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)); /* enable broadcast    */
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&rcvBuf, sizeof(rcvBuf));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(*port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (SOCKADDR *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PRINT("plat_udp_open: bind(port=%u) failed, error=%d\n", *port, WSAGetLastError());
        closesocket(sock);
        wsa_cleanup();
        return NULL;
    }
    if (*port == 0u) {                  /* report back the ephemeral port */
        struct sockaddr_in bound;
        socklen_t len = sizeof(bound);
        if (getsockname(sock, (struct sockaddr *)&bound, &len) == 0)
            *port = ntohs(bound.sin_port);
    }

    /* non-blocking: plat_udp_poll() drains without ever blocking */
    if (ioctlsocket(sock, FIONBIO, &nonblock) == SOCKET_ERROR) {
        PRINT("plat_udp_open: ioctlsocket(FIONBIO) failed, error=%d\n", WSAGetLastError());
        closesocket(sock);
        wsa_cleanup();
        return NULL;
    }

    s->sock = sock;
    s->rx   = rx;
    s->tag  = tag;
    s->used = true;
    return s;
}

bool plat_udp_send(plat_udp_t *s, const uint8_t dstIp[4], uint16_t dstPort,
                   const uint8_t *buf, uint16_t len)
{
    SOCKADDR_IN dst;
    int sent;
    if (!s || !s->used || !dstIp || dstPort == 0u) return false;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(dstPort);
    dst.sin_addr.S_un.S_un_b.s_b1 = dstIp[0];
    dst.sin_addr.S_un.S_un_b.s_b2 = dstIp[1];
    dst.sin_addr.S_un.S_un_b.s_b3 = dstIp[2];
    dst.sin_addr.S_un.S_un_b.s_b4 = dstIp[3];
    sent = sendto(s->sock, (const char *)buf, len, 0, (SOCKADDR *)&dst, sizeof(dst));
    if (sent == SOCKET_ERROR) {
        PRINT("plat_udp_send: sendto failed, error=%d\n", WSAGetLastError());
        return false;
    }
    return (sent == (int)len);
}

bool plat_udp_join_multicast(plat_udp_t *s, const uint8_t group[4],
                             const uint8_t localIf[4])
{
    struct ip_mreq mreq;
    struct in_addr outIf;
    int rc;
    if (!s || !s->used || !group || !localIf) return false;

    mreq.imr_multiaddr.s_addr = (uint32_t)group[0] | ((uint32_t)group[1] << 8) |
                                ((uint32_t)group[2] << 16) | ((uint32_t)group[3] << 24);
    mreq.imr_interface.s_addr = (uint32_t)localIf[0] | ((uint32_t)localIf[1] << 8) |
                                ((uint32_t)localIf[2] << 16) | ((uint32_t)localIf[3] << 24);

    rc = setsockopt(s->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEINVAL) return true;   /* 10022 = already a member -> success */
        PRINT("plat_udp_join_multicast: IP_ADD_MEMBERSHIP failed, error=%d\n", err);
        return false;
    }
    /* set the outgoing interface for multicast packets on this socket */
    outIf.s_addr = mreq.imr_interface.s_addr;
    setsockopt(s->sock, IPPROTO_IP, IP_MULTICAST_IF, (char *)&outIf, sizeof(outIf));
    return true;
}

int plat_udp_poll(void)
{
    static uint8_t rxbuf[RX_BUF_SIZE];
    fd_set rd;
    struct timeval tv;
    SOCKET maxfd = 0;
    int i, n, dispatched = 0;

    FD_ZERO(&rd);
    for (i = 0; i < MAX_PLAT_SOCKETS; i++) {
        if (s_socks[i].used) {
            FD_SET(s_socks[i].sock, &rd);
            if (s_socks[i].sock > maxfd) maxfd = s_socks[i].sock;
        }
    }
    if (rd.fd_count == 0) return 0;

    tv.tv_sec = 0; tv.tv_usec = 0;       /* poll, never block */
    n = select((int)maxfd + 1, &rd, NULL, NULL, &tv);
    if (n <= 0) return 0;                 /* 0 = nothing ready, SOCKET_ERROR = ignore */

    for (i = 0; i < MAX_PLAT_SOCKETS; i++) {
        struct plat_udp *s = &s_socks[i];
        if (!s->used || !FD_ISSET(s->sock, &rd)) continue;
        /* drain this socket fully (a select-ready socket may hold several) */
        for (;;) {
            SOCKADDR_IN from;
            int fromLen = sizeof(from);
            int len = recvfrom(s->sock, (char *)rxbuf, sizeof(rxbuf), 0,
                               (SOCKADDR *)&from, &fromLen);
            if (len == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK)
                    PRINT("plat_udp_poll: recvfrom failed, error=%d\n", err);
                break;
            }
            if (len > 0) {
                uint8_t ip[4];
                uint16_t port;
                ip[0] = from.sin_addr.S_un.S_un_b.s_b1;
                ip[1] = from.sin_addr.S_un.S_un_b.s_b2;
                ip[2] = from.sin_addr.S_un.S_un_b.s_b3;
                ip[3] = from.sin_addr.S_un.S_un_b.s_b4;
                port = ntohs(from.sin_port);
                s->rx(s, ip, port, rxbuf, (uint16_t)len, s->tag);
                dispatched++;
            }
        }
    }
    return dispatched;
}

void plat_udp_close(plat_udp_t *s)
{
    if (s && s->used) {
        closesocket(s->sock);
        s->used = false;
        s->rx = NULL;
        s->tag = NULL;
        wsa_cleanup();
    }
}

void plat_net_enum_ifaces(plat_if_cb cb, void *tag)
{
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddrs = NULL, p;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    DWORD rc;
    int attempt;

    if (!cb) return;

    for (attempt = 0; attempt < 3; attempt++) {
        pAddrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!pAddrs) return;
        rc = GetAdaptersAddresses(AF_INET, flags, NULL, pAddrs, &bufLen);
        if (rc == ERROR_BUFFER_OVERFLOW) { free(pAddrs); pAddrs = NULL; continue; }
        break;
    }
    if (!pAddrs) return;
    if (rc != NO_ERROR) { free(pAddrs); return; }

    for (p = pAddrs; p; p = p->Next) {
        PIP_ADAPTER_UNICAST_ADDRESS u;
        if (p->OperStatus != IfOperStatusUp) continue;
        for (u = p->FirstUnicastAddress; u; u = u->Next) {
            struct sockaddr_in *sa;
            uint8_t ip[4], mask[4];
            uint32_t m;
            uint8_t prefix;
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            sa = (struct sockaddr_in *)u->Address.lpSockaddr;
            memcpy(ip, &sa->sin_addr, 4);
            if (ip[0] == 169 || ip[0] == 127) continue;   /* skip link-local / loopback */
            prefix = u->OnLinkPrefixLength;
            m = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
            mask[0] = (uint8_t)(m >> 24); mask[1] = (uint8_t)(m >> 16);
            mask[2] = (uint8_t)(m >> 8);  mask[3] = (uint8_t)(m);
            cb(ip, mask, tag);
        }
    }
    free(pAddrs);
}

/* ===================== 3) wait / yield ================================= */

void plat_sleep_ms(uint32_t ms) { Sleep(ms); }

void plat_yield(void) { SwitchToThread(); }
