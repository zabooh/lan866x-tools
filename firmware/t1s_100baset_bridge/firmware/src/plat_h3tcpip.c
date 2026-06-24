/*
 * plat_h3tcpip.c - Platform layer (plat.h) for the LAN866x SOME/IP client
 *                  running on the T1S<->100BASE-T bridge, over the MPLAB
 *                  Harmony 3 TCP/IP stack (TCPIP_UDP_*). No C++.
 *
 * This is the single per-target file the LAN866x toolset port needs (see
 * PORTING.md). Everything above it - rcp.c, someip_stub.c, libsomeip - is
 * platform-neutral and unchanged. The Windows reference impl is plat_win.c.
 *
 * SINGLE-THREAD / SUPERLOOP: there are no threads here. plat_udp_poll() drains
 * the Harmony UDP sockets and dispatches each datagram synchronously to the rx
 * callback. The Harmony stack itself is pumped by SYS_Tasks() in the main
 * superloop; this layer only reads/writes already-buffered UDP datagrams, so it
 * must NOT be re-entered from inside a SYS_CMD handler that blocks the superloop.
 *
 * The SD multicast (224.0.0.1:30490) is received via eth0 in promiscuous mode
 * (DRV_LAN865X promiscuous) + the socket's multicast-accept flags, which avoids
 * fiddly IGMP/MAC-filter group joins on the T1S MAC-PHY.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "definitions.h"           /* Harmony: TCPIP stack + SYS_TIME APIs */
#include "plat.h"

#define MAX_PLAT_SOCKETS  (8)
#define RX_BUF_SIZE       (1600)

struct plat_udp {
    UDP_SOCKET      sock;          /* Harmony UDP socket handle              */
    plat_udp_rx_cb  rx;
    void           *tag;
    bool            used;
};

static struct plat_udp s_socks[MAX_PLAT_SOCKETS];

/* ===================== 1) time base ===================================== */

uint32_t plat_now_ms(void)
{
    uint32_t freq = SYS_TIME_FrequencyGet();
    if (freq == 0u) return 0u;
    return (uint32_t)((SYS_TIME_Counter64Get() * 1000ULL) / (uint64_t)freq);
}

/* ===================== 2) non-blocking UDP + net ======================= */

plat_udp_t *plat_udp_open(uint16_t *port, plat_udp_rx_cb rx, void *tag)
{
    struct plat_udp *s = NULL;
    UDP_SOCKET sock;
    int i;

    if (!port || !rx) return NULL;

    for (i = 0; i < MAX_PLAT_SOCKETS; i++)
        if (!s_socks[i].used) { s = &s_socks[i]; break; }
    if (!s) return NULL;

    /* Server socket bound to *port, accepting datagrams from any remote on any
     * interface (the destination is set per-send in plat_udp_send). */
    sock = TCPIP_UDP_ServerOpen(IP_ADDRESS_TYPE_IPV4, (UDP_PORT)*port, NULL);
    if (sock == INVALID_UDP_SOCKET) return NULL;

    /* Accept multicast traffic on this socket (SD group 224.0.0.1) and don't
     * restrict by source/interface - we bridge, traffic can arrive on eth0. */
    {
        UDP_OPTION_MULTICAST_DATA mc;
        mc.flagsMask  = UDP_MCAST_FLAG_DEFAULT;
        mc.flagsValue = UDP_MCAST_FLAG_DEFAULT;
        (void)TCPIP_UDP_OptionsSet(sock, UDP_OPTION_MULTICAST, &mc);
    }

    if (*port == 0u) {                 /* report back the actual bound port */
        UDP_SOCKET_INFO info;
        if (TCPIP_UDP_SocketInfoGet(sock, &info))
            *port = (uint16_t)info.localPort;
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
    IP_MULTI_ADDRESS dst;
    uint16_t ready;

    if (!s || !s->used || !dstIp || dstPort == 0u || !buf) return false;

    dst.v4Add.v[0] = dstIp[0];
    dst.v4Add.v[1] = dstIp[1];
    dst.v4Add.v[2] = dstIp[2];
    dst.v4Add.v[3] = dstIp[3];

    if (!TCPIP_UDP_DestinationIPAddressSet(s->sock, IP_ADDRESS_TYPE_IPV4, &dst))
        return false;
    if (!TCPIP_UDP_DestinationPortSet(s->sock, (UDP_PORT)dstPort))
        return false;

    ready = TCPIP_UDP_TxPutIsReady(s->sock, len);
    if (ready < len) return false;

    if (TCPIP_UDP_ArrayPut(s->sock, buf, len) != len) return false;
    (void)TCPIP_UDP_Flush(s->sock);
    return true;
}

bool plat_udp_join_multicast(plat_udp_t *s, const uint8_t group[4],
                             const uint8_t localIf[4])
{
    /* On the bridge the SD multicast reaches eth0 via L2 flooding + the MAC's
     * promiscuous mode; the socket already accepts multicast (flags set in
     * plat_udp_open). No explicit IGMP group join is required here. */
    (void)group; (void)localIf;
    return (s && s->used);
}

int plat_udp_poll(void)
{
    static uint8_t rxbuf[RX_BUF_SIZE];
    int i, dispatched = 0;

    for (i = 0; i < MAX_PLAT_SOCKETS; i++) {
        struct plat_udp *s = &s_socks[i];
        uint16_t nb;
        if (!s->used) continue;

        while ((nb = TCPIP_UDP_GetIsReady(s->sock)) > 0u) {
            UDP_SOCKET_INFO info;
            uint8_t  ip[4] = {0,0,0,0};
            uint16_t port  = 0u;
            uint16_t toRead = (nb > (uint16_t)sizeof(rxbuf)) ? (uint16_t)sizeof(rxbuf) : nb;
            uint16_t got    = TCPIP_UDP_ArrayGet(s->sock, rxbuf, toRead);

            if (TCPIP_UDP_SocketInfoGet(s->sock, &info)) {
                ip[0] = info.sourceIPaddress.v4Add.v[0];
                ip[1] = info.sourceIPaddress.v4Add.v[1];
                ip[2] = info.sourceIPaddress.v4Add.v[2];
                ip[3] = info.sourceIPaddress.v4Add.v[3];
                port  = (uint16_t)info.remotePort;
            }

            (void)TCPIP_UDP_Discard(s->sock);   /* drop any remainder of this datagram */

            if (got > 0u) {
                s->rx(s, ip, port, rxbuf, got, s->tag);
                dispatched++;
            }
        }
    }
    return dispatched;
}

void plat_udp_close(plat_udp_t *s)
{
    if (s && s->used) {
        TCPIP_UDP_Close(s->sock);
        s->used = false;
        s->rx = NULL;
        s->tag = NULL;
    }
}

void plat_net_enum_ifaces(plat_if_cb cb, void *tag)
{
    int n, i;
    if (!cb) return;

    n = TCPIP_STACK_NumberOfNetworksGet();
    for (i = 0; i < n; i++) {
        TCPIP_NET_HANDLE net = TCPIP_STACK_IndexToNet(i);
        IPV4_ADDR ipa, maska;
        uint8_t ip[4], mask[4];

        if (net == 0) continue;
        if (!TCPIP_STACK_NetIsUp(net)) continue;

        ipa.Val   = TCPIP_STACK_NetAddress(net);
        maska.Val = TCPIP_STACK_NetMask(net);
        if (ipa.Val == 0u) continue;

        ip[0] = ipa.v[0];   ip[1] = ipa.v[1];   ip[2] = ipa.v[2];   ip[3] = ipa.v[3];
        mask[0] = maska.v[0]; mask[1] = maska.v[1]; mask[2] = maska.v[2]; mask[3] = maska.v[3];
        cb(ip, mask, tag);
    }
}

/* ===================== 3) wait / yield ================================= */

void plat_sleep_ms(uint32_t ms)
{
    /* Busy-wait on the time base. NOTE: this does NOT pump SYS_Tasks(), so the
     * SOME/IP client must be driven cooperatively from APP_Tasks (rcp_poll once
     * per superloop iteration), not by blocking here. Kept minimal on purpose. */
    uint32_t start = plat_now_ms();
    while ((plat_now_ms() - start) < ms) {
        /* spin */
    }
}

void plat_yield(void)
{
    /* nothing to yield to in the bare-metal superloop */
}
