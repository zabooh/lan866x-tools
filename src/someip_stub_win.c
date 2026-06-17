/*
 * someip_stub_win.c  -  Pure-C Windows platform stub for libsomeip.
 *
 * Implements all SOMEIP_CB_* callbacks the SOME/IP core needs, on Win32 +
 * Winsock, reusing windows-udp-handler.c for the actual sockets. This is the
 * C equivalent of the shipped C++ stub (libsomeip/stub/someip-stub.cpp) - the
 * only C++ in that file was a std::mutex, replaced here by a CRITICAL_SECTION.
 *
 * Because this stub provides the whole platform layer in C, a tool built from
 * src/*.c + libsomeip/src/*.c + windows-udp-handler.c + this file links with
 * NO C++ runtime (no libstdc++/libc++): a genuine vanilla-C host.
 *
 * On an MCU32 target this single file is what you replace with an
 * lwIP/FreeRTOS implementation of the same callback set (see PORTING.md).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "someip-cfg.h"
#include "someip.h"
#include "windows-udp-handler.h"

#define MAX_CALLBACKS         (8u)
#define SERVICE_INTERVAL_MS   (20u)
#define SEM_WAIT_TIMEOUT_MS   (1200u)
#define MAX_AVAILABLE_SOCKETS (32)
#define MAX_INTERFACES        (16)

#define PRINT(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

/* SD multicast address - defined by the application (probe.c). */
extern uint8_t MULTICAST_IP[];

struct udpSocketVar {
    SOMEIP_DataReceived_CB_t rxCB[MAX_CALLBACKS];
    void                    *rxTag[MAX_CALLBACKS];
    udp_t                   *udpSock;
    uint16_t                 udpPort;
    bool                     multicastJoined;
    bool                     used;
};

typedef struct {
    uint8_t interfaceIP[4];
    uint8_t subnetMask[4];
    bool    multicastJoined;
    bool    enumarated;
} InterfaceInfo_t;

struct udpLocalVar {
    struct udpSocketVar sockets[MAX_AVAILABLE_SOCKETS];
    InterfaceInfo_t     inf[MAX_INTERFACES];
    udp_t              *multicastSock;
    HANDLE              semSomeIP;
    HANDLE              semServiceTimer;
    HANDLE              serviceThread;
    DWORD               serviceThreadId;
    CRITICAL_SECTION    mallocCs;
    bool                allowThreadRun;
    bool                threadServiceRuns;
    bool                initialized;
};

static struct udpLocalVar m;

static bool Init(void);
static DWORD WINAPI ServiceThread(LPVOID lpParam);
static void OnLocalIp(const char *adapterName, const char *friendlyName,
                      const uint8_t localIPv4[4], uint8_t subnetLength,
                      const uint8_t localIPv6[16], uint8_t ipv6PrefixLength,
                      const uint8_t macAddress[6], uint64_t linkSpeed);
static void PollNetworkStatusAndJoinMulticast(void);
static void OnUdpRx(udp_t *pUdp, const uint8_t remoteIp[4], uint16_t remotePort,
                    const uint8_t *buf, uint16_t len);

/* ===================== callbacks from the SOME/IP core ==================== */

void SOMEIP_CB_Log(const char *logMsg) { PRINT("SOME-IP-log:%s\r\n", logMsg); }

bool SOMEIP_CB_OpenSocket(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback,
                          void *rxTag, void **sockHandle)
{
    uint8_t i;
    bool existing = false, newEntry = false;
    if (sockHandle && Init()) {
        *sockHandle = NULL;
        for (i = 0; !existing && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (pSock->used && (pSock->udpPort == *udpPort)) {
                uint8_t j;
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++)
                    existing = ((rxCallback == pSock->rxCB[j]) && (rxTag == pSock->rxTag[j]));
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++) {
                    if (NULL == pSock->rxCB[j]) {
                        pSock->rxCB[j] = rxCallback;
                        pSock->rxTag[j] = rxTag;
                        existing = true;
                    }
                }
                if (existing) *sockHandle = pSock;
                else { PRINT("Failed to register callback for RX thread\r\n"); return false; }
            }
        }
        for (i = 0; !existing && !newEntry && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (!pSock->used) {
                pSock->used = true;
                pSock->rxCB[0] = rxCallback;
                pSock->rxTag[0] = rxTag;
                pSock->udpSock = WinUdpHandler_CreateSocket(udpPort, OnUdpRx);
                pSock->udpPort = *udpPort;
                if (pSock->udpSock) {
                    newEntry = true;
                    if (SD_PORT == pSock->udpPort) m.multicastSock = pSock->udpSock;
                    *sockHandle = pSock;
                } else {
                    pSock->used = false;
                    PRINT("Failed to open socket\r\n");
                    break;
                }
            }
        }
    }
    return (existing || newEntry);
}

bool SOMEIP_CB_GetLocalIpAddr(uint8_t localIP[SOMEIP_IPV4_ADDR_LEN],
                              const uint8_t targetIP[SOMEIP_IPV4_ADDR_LEN])
{
    bool success = false;
    if (localIP && targetIP && targetIP[0]) {
        uint8_t i;
        for (i = 0; !success && (i < MAX_INTERFACES); i++) {
            InterfaceInfo_t *pInfo = &m.inf[i];
            bool match = true;
            uint8_t k;
            for (k = 0; match && (k < 4); k++) {
                if (pInfo->enumarated && pInfo->subnetMask[k]) {
                    uint8_t mt = (uint8_t)(targetIP[k] & pInfo->subnetMask[k]);
                    uint8_t ml = (uint8_t)(pInfo->interfaceIP[k] & pInfo->subnetMask[k]);
                    match = (ml == mt);
                }
            }
            if (match && pInfo->enumarated) { memcpy(localIP, pInfo->interfaceIP, 4); success = true; }
        }
    }
    return success;
}

void SOMEIP_CB_EnterCriticialSection(void) { WaitForSingleObject(m.semSomeIP, INFINITE); }
void SOMEIP_CB_LeaveCriticialSection(void) { LONG prev; ReleaseSemaphore(m.semSomeIP, 1, &prev); }

bool SOMEIP_CB_SemInit(SOME_IP_SEM_t *sem, int8_t initialValue)
{
    HANDLE h = CreateSemaphoreA(NULL, initialValue, 1, NULL);
    *sem = NULL;
    if (h) { *sem = h; return true; }
    return false;
}
bool SOMEIP_CB_SemWait(SOME_IP_SEM_t *sem)
{
    if (sem && *sem)
        return (WAIT_OBJECT_0 == WaitForSingleObject((HANDLE)*sem, SEM_WAIT_TIMEOUT_MS));
    return false;
}
void SOMEIP_CB_SemPost(SOME_IP_SEM_t *sem)
{
    if (sem && *sem) { LONG prev; ReleaseSemaphore((HANDLE)*sem, 1, &prev); }
}
void SOMEIP_CB_SemDestroy(SOME_IP_SEM_t *sem)
{
    if (sem && *sem) { CloseHandle((HANDLE)*sem); *sem = NULL; }
}

void SOMEIP_CB_NeedService(void) { ReleaseSemaphore(m.semServiceTimer, 1, NULL); }

bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length)
{
    bool success = false;
    uint8_t *pBuffer;
    EnterCriticalSection(&m.mallocCs);
    pBuffer = (uint8_t *)malloc(length);
    if (pBuffer) { *ppBuffer = pBuffer; *ppMemTag = pBuffer; success = true; }
    LeaveCriticalSection(&m.mallocCs);
    return success;
}

void *SOMEIP_CB_Calloc(size_t xNum, size_t xSize)
{
    void *p;
    EnterCriticalSection(&m.mallocCs);
    p = calloc(xNum, xSize);
    LeaveCriticalSection(&m.mallocCs);
    return p;
}

void SOMEIP_CB_Free(void *pMem)
{
    EnterCriticalSection(&m.mallocCs);
    free(pMem);
    LeaveCriticalSection(&m.mallocCs);
}

bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag,
                       const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port,
                       void *sockHandle)
{
    struct udpSocketVar *pSock = (struct udpSocketVar *)sockHandle;
    bool success = false;
    if (pSock) {
        if (0 == memcmp(MULTICAST_IP, ipAddrV4, 4)) {
            if (!pSock->multicastJoined) {
                uint8_t i;
                for (i = 0; !success && (i < MAX_INTERFACES); i++) {
                    InterfaceInfo_t *pInfo = &m.inf[i];
                    if (pInfo->enumarated &&
                        WinUdpHandler_JoinMulticastGroup(pSock->udpSock, MULTICAST_IP,
                                                         pInfo->interfaceIP, pSock->udpPort))
                        pSock->multicastJoined = true;
                }
            }
        }
        success = WinUdpHandler_Send(pSock->udpSock, ipAddrV4, port, pBuf, length);
    }
    if (pMemTag) { EnterCriticalSection(&m.mallocCs); free(pMemTag); LeaveCriticalSection(&m.mallocCs); }
    return success;
}

void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr)
{
    PRINT("Assertion in '%s' Line=%u\r\n", pFilename, (unsigned)lineNr);
}

uint32_t SOMEIP_CB_GetTimeMS(void) { return GetTickCount(); }

uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max)
{
    uint32_t diff = (max - min + 1u);
    return ((uint32_t)rand() % diff) + min;
}

/* ============================ private helpers ============================= */

static bool Init(void)
{
    if (!m.initialized) {
        m.initialized = true;
        m.allowThreadRun = true;
        InitializeCriticalSection(&m.mallocCs);
        m.semSomeIP = CreateSemaphoreA(NULL, 1, 1, NULL);
        if (!m.semSomeIP) { PRINT("Failed to create SOME/IP semaphore\r\n"); m.initialized = false; }
        m.semServiceTimer = CreateSemaphoreA(NULL, 0, 1, NULL);
        if (!m.semServiceTimer) { PRINT("Failed to create service-timer semaphore\r\n"); m.initialized = false; }
        m.serviceThread = CreateThread(NULL, 0, ServiceThread, NULL, 0, &m.serviceThreadId);
        if (!m.serviceThread) { PRINT("Failed to start service thread\r\n"); m.initialized = false; }
    }
    return m.initialized;
}

static DWORD WINAPI ServiceThread(LPVOID lpParam)
{
    (void)lpParam;
    m.threadServiceRuns = true;
    while (m.allowThreadRun) {
        WaitForSingleObject(m.semServiceTimer, SERVICE_INTERVAL_MS);
        SOMEIP_Client_CheckTimers();
        PollNetworkStatusAndJoinMulticast();
    }
    m.threadServiceRuns = false;
    return 0;
}

static void OnLocalIp(const char *adapterName, const char *friendlyName,
                      const uint8_t localIPv4[4], uint8_t subnetLength,
                      const uint8_t localIPv6[16], uint8_t ipv6PrefixLength,
                      const uint8_t macAddress[6], uint64_t linkSpeed)
{
    bool found = false;
    uint8_t i;
    (void)adapterName; (void)friendlyName; (void)localIPv6;
    (void)ipv6PrefixLength; (void)macAddress; (void)linkSpeed;

    for (i = 0; !found && (i < MAX_INTERFACES); i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        if (0 == memcmp(pInfo->interfaceIP, localIPv4, 4)) { found = true; pInfo->enumarated = true; }
    }
    for (i = 0; !found && (i < MAX_INTERFACES); i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        if (0 == pInfo->interfaceIP[0]) {
            uint32_t mask = (subnetLength == 0) ? 0u : (0xFFFFFFFFu << (32 - subnetLength));
            found = true;
            memcpy(pInfo->interfaceIP, localIPv4, 4);
            pInfo->subnetMask[0] = (uint8_t)((mask >> 24) & 0xFF);
            pInfo->subnetMask[1] = (uint8_t)((mask >> 16) & 0xFF);
            pInfo->subnetMask[2] = (uint8_t)((mask >> 8) & 0xFF);
            pInfo->subnetMask[3] = (uint8_t)(mask & 0xFF);
            pInfo->multicastJoined = false;
            pInfo->enumarated = true;
        }
    }
    if (!found) PRINT("Could not store local IP info, increase MAX_INTERFACES\r\n");
}

static void PollNetworkStatusAndJoinMulticast(void)
{
    uint8_t i;
    for (i = 0; i < MAX_INTERFACES; i++) m.inf[i].enumarated = false;

    if (!WinUdpHandler_GetLocalIPs(OnLocalIp)) PRINT("WinUdpHandler_GetLocalIPs() failed\r\n");

    for (i = 0; i < MAX_INTERFACES; i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        if (!pInfo->enumarated && pInfo->interfaceIP[0]) {
            memset(pInfo, 0, sizeof(InterfaceInfo_t));
        } else if (pInfo->enumarated && m.multicastSock && !pInfo->multicastJoined) {
            if (WinUdpHandler_JoinMulticastGroup(m.multicastSock, MULTICAST_IP, pInfo->interfaceIP, SD_PORT)) {
                PRINT("Joined Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                      MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                      pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3],
                      SD_PORT);
                pInfo->multicastJoined = true;
            }
        }
    }
}

static void OnUdpRx(udp_t *pUdp, const uint8_t ip[4], uint16_t remotePort,
                    const uint8_t *buf, uint16_t len)
{
    struct udpSocketVar *pSock = NULL;
    uint8_t i;
    for (i = 0; !pSock && (i < MAX_AVAILABLE_SOCKETS); i++) {
        struct udpSocketVar *pTmp = &m.sockets[i];
        if (pTmp->used && (pTmp->udpSock == pUdp)) pSock = pTmp;
    }
    if (pSock) {
        struct SOMEIP_IpAddr rxIp;
        uint8_t j;
        memset(&rxIp, 0, sizeof(rxIp));
        if (SOMEIP_CB_GetLocalIpAddr(rxIp.sourceAddr, ip)) {
            memcpy(rxIp.destinAddr, ip, 4);
            rxIp.port = remotePort;
            for (j = 0; j < MAX_CALLBACKS; j++) {
                if (NULL != pSock->rxCB[j]) {
                    enum SOMEIP_ReturnCode result = pSock->rxCB[j](buf, len, &rxIp, pSock->rxTag[j]);
                    if (SOMEIP_E_OK != result)
                        PRINT("SOME/IP parsing (port %d) returned 0x%X\r\n", remotePort, result);
                }
            }
        }
    }
}
