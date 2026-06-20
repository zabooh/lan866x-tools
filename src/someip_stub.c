/*
 * someip_stub.c  -  Platform-neutral SOME/IP platform stub (single-thread).
 *
 * Implements every SOMEIP_CB_* callback the libsomeip core needs, on top of the
 * narrow plat.h layer. This file is PLATFORM-NEUTRAL: all OS specifics live in
 * plat_<target>.c. It replaces the old someip_stub_win.c + windows-udp-handler.c
 * pair (Win32 threads + blocking WSARecvFrom).
 *
 * SINGLE-THREAD MODEL
 *   There are NO threads. The two background threads of the old design are gone:
 *     - the per-socket blocking ReceiveThread  -> plat_udp_poll() (synchronous)
 *     - the 20 ms ServiceThread (timers+mcast)  -> someip_service() per tick
 *   Received datagrams are delivered synchronously from plat_udp_poll(), which
 *   someip_service() calls. Because the RX path now runs on the same (only)
 *   execution strand as the rest of the program:
 *     - SOMEIP_CB_EnterCriticialSection / Leave are NO-OPs (no contention).
 *     - SOMEIP_CB_NeedService is a NO-OP (the superloop pumps every tick).
 *     - the non-reentrant-semaphore self-deadlock (old gotcha #3) cannot occur.
 *     - shared state needs no volatile/atomic/lock.
 *   The SOMEIP_CB_Sem* set is implemented trivially: the libsomeip core never
 *   calls it (0 references in libsomeip/src), it exists only to satisfy the link.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "someip-cfg.h"   /* SD_PORT */
#include "someip.h"
#include "plat.h"
#include "someip_stub.h"

#define MAX_STUB_SOCKETS   (8u)
#define MAX_CALLBACKS      (8u)
#define MAX_IFACES         (16u)
#define ENUM_INTERVAL_MS   (1000u)   /* re-enumerate interfaces at most this often */

#define PRINT(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

/* SD multicast address - defined by each tool (e.g. {224,0,0,1}). */
extern uint8_t MULTICAST_IP[];

struct stubsock {
    plat_udp_t              *udp;
    SOMEIP_DataReceived_CB_t rxCB[MAX_CALLBACKS];
    void                    *rxTag[MAX_CALLBACKS];
    uint16_t                 port;
    bool                     used;
};

typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    bool    valid;   /* slot occupied */
    bool    seen;    /* refreshed in the current enum pass */
    bool    joined;  /* SD socket has joined multicast on this interface */
} iface_t;

static struct stubsock s_socks[MAX_STUB_SOCKETS];
static struct stubsock *s_sdSock = NULL;      /* the SD (30490) socket, if open */
static iface_t          s_ifaces[MAX_IFACES];
static uint32_t         s_lastEnum = 0u;
static bool             s_enumeratedOnce = false;

/* ===================== private helpers ================================= */

static void on_iface(const uint8_t ip[4], const uint8_t mask[4], void *tag)
{
    uint8_t i;
    (void)tag;
    for (i = 0; i < MAX_IFACES; i++) {       /* refresh existing entry */
        if (s_ifaces[i].valid && memcmp(s_ifaces[i].ip, ip, 4) == 0) {
            memcpy(s_ifaces[i].mask, mask, 4);
            s_ifaces[i].seen = true;
            return;
        }
    }
    for (i = 0; i < MAX_IFACES; i++) {       /* add a new one */
        if (!s_ifaces[i].valid) {
            memcpy(s_ifaces[i].ip, ip, 4);
            memcpy(s_ifaces[i].mask, mask, 4);
            s_ifaces[i].valid = true;
            s_ifaces[i].seen = true;
            s_ifaces[i].joined = false;
            return;
        }
    }
    PRINT("someip_stub: out of interface slots, increase MAX_IFACES\r\n");
}

/* Join the SD multicast group on every interface not yet joined. */
static void join_sd_multicast(void)
{
    uint8_t i;
    if (!s_sdSock) return;
    for (i = 0; i < MAX_IFACES; i++) {
        if (s_ifaces[i].valid && !s_ifaces[i].joined) {
            if (plat_udp_join_multicast(s_sdSock->udp, MULTICAST_IP, s_ifaces[i].ip)) {
                s_ifaces[i].joined = true;
                PRINT("Joined Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                      MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                      s_ifaces[i].ip[0], s_ifaces[i].ip[1], s_ifaces[i].ip[2], s_ifaces[i].ip[3],
                      SD_PORT);
            }
        }
    }
}

/* (Throttled) interface enumeration + SD multicast join. Replaces the old
 * ServiceThread's PollNetworkStatusAndJoinMulticast(). */
static void net_maintain(void)
{
    uint32_t now = plat_now_ms();
    if (!s_enumeratedOnce || (now - s_lastEnum) >= ENUM_INTERVAL_MS) {
        uint8_t i;
        for (i = 0; i < MAX_IFACES; i++) s_ifaces[i].seen = false;
        plat_net_enum_ifaces(on_iface, NULL);
        for (i = 0; i < MAX_IFACES; i++) {           /* drop vanished interfaces */
            if (s_ifaces[i].valid && !s_ifaces[i].seen)
                memset(&s_ifaces[i], 0, sizeof(s_ifaces[i]));
        }
        s_lastEnum = now;
        s_enumeratedOnce = true;
    }
    join_sd_multicast();
}

/* plat RX callback: runs SYNCHRONOUSLY from plat_udp_poll(). Mirrors the old
 * OnUdpRx: sourceAddr = local interface, destinAddr = remote peer, port = remote. */
static void on_udp_rx(plat_udp_t *s, const uint8_t srcIp[4], uint16_t srcPort,
                      const uint8_t *buf, uint16_t len, void *tag)
{
    struct stubsock *ss = (struct stubsock *)tag;
    struct SOMEIP_IpAddr rxIp;
    uint8_t j;
    (void)s;
    if (!ss) return;
    memset(&rxIp, 0, sizeof(rxIp));
    if (SOMEIP_CB_GetLocalIpAddr(rxIp.sourceAddr, srcIp)) {
        memcpy(rxIp.destinAddr, srcIp, 4);
        rxIp.port = srcPort;
        for (j = 0; j < MAX_CALLBACKS; j++) {
            if (ss->rxCB[j]) {
                enum SOMEIP_ReturnCode r = ss->rxCB[j](buf, len, &rxIp, ss->rxTag[j]);
                if (r != SOMEIP_E_OK)
                    PRINT("SOME/IP parsing (port %d) returned 0x%X\r\n", srcPort, r);
            }
        }
    }
}

/* ===================== the superloop service tick ====================== */

void someip_service(void)
{
    net_maintain();              /* interfaces known + SD multicast joined */
    plat_udp_poll();             /* drain RX -> dispatch responses/offers synchronously */
    SOMEIP_Client_CheckTimers(); /* SD state machine (FindService, offer timeouts, events) */
}

/* ===================== callbacks from the SOME/IP core ================= */

void SOMEIP_CB_Log(const char *logMsg) { PRINT("SOME-IP-log:%s\r\n", logMsg); }

bool SOMEIP_CB_OpenSocket(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback,
                          void *rxTag, void **sockHandle)
{
    uint8_t i, j;
    struct stubsock *ss = NULL;
    if (!sockHandle || !udpPort || !rxCallback) return false;
    *sockHandle = NULL;

    /* Existing socket on the same port: just register another callback. */
    for (i = 0; i < MAX_STUB_SOCKETS; i++) {
        if (s_socks[i].used && s_socks[i].port == *udpPort) {
            ss = &s_socks[i];
            for (j = 0; j < MAX_CALLBACKS; j++)        /* already registered? */
                if (ss->rxCB[j] == rxCallback && ss->rxTag[j] == rxTag) {
                    *sockHandle = ss; return true;
                }
            for (j = 0; j < MAX_CALLBACKS; j++)        /* take a free callback slot */
                if (!ss->rxCB[j]) {
                    ss->rxCB[j] = rxCallback; ss->rxTag[j] = rxTag;
                    *sockHandle = ss; return true;
                }
            PRINT("SOMEIP_CB_OpenSocket: no free callback slot for port %d\r\n", *udpPort);
            return false;
        }
    }

    /* New socket. */
    for (i = 0; i < MAX_STUB_SOCKETS; i++) if (!s_socks[i].used) { ss = &s_socks[i]; break; }
    if (!ss) { PRINT("SOMEIP_CB_OpenSocket: no free socket slot\r\n"); return false; }

    memset(ss, 0, sizeof(*ss));
    ss->udp = plat_udp_open(udpPort, on_udp_rx, ss);   /* *udpPort filled if ephemeral */
    if (!ss->udp) { PRINT("SOMEIP_CB_OpenSocket: plat_udp_open(port=%d) failed\r\n", *udpPort); return false; }
    ss->port = *udpPort;
    ss->rxCB[0] = rxCallback;
    ss->rxTag[0] = rxTag;
    ss->used = true;
    if (ss->port == SD_PORT) s_sdSock = ss;
    *sockHandle = ss;
    return true;
}

bool SOMEIP_CB_GetLocalIpAddr(uint8_t localIP[SOMEIP_IPV4_ADDR_LEN],
                              const uint8_t targetIP[SOMEIP_IPV4_ADDR_LEN])
{
    uint8_t i, k;
    if (!localIP || !targetIP || !targetIP[0]) return false;
    for (i = 0; i < MAX_IFACES; i++) {
        iface_t *p = &s_ifaces[i];
        bool match = true;
        if (!p->valid) continue;
        for (k = 0; match && k < 4; k++) {
            if (p->mask[k]) {
                uint8_t mt = (uint8_t)(targetIP[k] & p->mask[k]);
                uint8_t ml = (uint8_t)(p->ip[k]     & p->mask[k]);
                match = (ml == mt);
            }
        }
        if (match) { memcpy(localIP, p->ip, 4); return true; }
    }
    return false;
}

/* Single-thread: no contention -> no-ops (old gotcha #3 cannot occur). */
void SOMEIP_CB_EnterCriticialSection(void) { }
void SOMEIP_CB_LeaveCriticialSection(void) { }

/* The core never calls these (0 references in libsomeip/src); trivial stubs to
 * satisfy the link. */
bool SOMEIP_CB_SemInit(SOME_IP_SEM_t *sem, int8_t initialValue)
{
    (void)initialValue;
    if (sem) { *sem = (void *)1; return true; }
    return false;
}
bool SOMEIP_CB_SemWait(SOME_IP_SEM_t *sem) { (void)sem; return true; }
void SOMEIP_CB_SemPost(SOME_IP_SEM_t *sem) { (void)sem; }
void SOMEIP_CB_SemDestroy(SOME_IP_SEM_t *sem) { if (sem) *sem = NULL; }

/* Single-thread: the superloop pumps every tick, nothing to wake. */
void SOMEIP_CB_NeedService(void) { }

/* Single-thread: plain allocation, no malloc lock needed. */
bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length)
{
    uint8_t *p = (uint8_t *)malloc(length);
    if (!p) return false;
    *ppBuffer = p;
    *ppMemTag = p;
    return true;
}
void *SOMEIP_CB_Calloc(size_t xNum, size_t xSize) { return calloc(xNum, xSize); }
void  SOMEIP_CB_Free(void *pMem) { free(pMem); }

bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag,
                       const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port,
                       void *sockHandle)
{
    struct stubsock *ss = (struct stubsock *)sockHandle;
    bool ok = false;
    if (ss) {
        if (memcmp(MULTICAST_IP, ipAddrV4, 4) == 0) join_sd_multicast(); /* ensure membership */
        ok = plat_udp_send(ss->udp, ipAddrV4, port, pBuf, length);
    }
    if (pMemTag) free(pMemTag);   /* the core hands ownership to us (see SOMEIP_CB_ProvideBuffer) */
    return ok;
}

void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr)
{
    PRINT("Assertion in '%s' Line=%u\r\n", pFilename, (unsigned)lineNr);
}

uint32_t SOMEIP_CB_GetTimeMS(void) { return plat_now_ms(); }

uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max)
{
    uint32_t diff = (max - min + 1u);
    return ((uint32_t)rand() % diff) + min;
}
