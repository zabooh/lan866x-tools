//DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.

The software and documentation is provided by microchip and its contributors
"as is" and any express, implied or statutory warranties, including, but not
limited to, the implied warranties of merchantability, fitness for a particular
purpose and non-infringement of third party intellectual property rights are
disclaimed to the fullest extent permitted by law. In no event shall microchip
or its contributors be liable for any direct, indirect, incidental, special,
exemplary, or consequential damages (including, but not limited to, procurement
of substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in contract,
strict liability, or tort (including negligence or otherwise) arising in any way
out of the use of the software and documentation, even if advised of the
possibility of such damage.

Except as expressly permitted hereunder and subject to the applicable license terms
for any third-party software incorporated in the software and any applicable open
source software license terms, no license or other rights, whether express or
implied, are granted under any patent or other intellectual property rights of
Microchip or any third party.
*/
//DOM-IGNORE-END

#ifdef WIN32
/*------------------------------------------------------------------------------------------------*/
/* SOMEIP Stub Code for Windows                                                                   */
/*------------------------------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <windows.h>
#include <process.h>
#include <mutex>
#include <optional>
#include "someip-cfg.h"
#include "someip.h"
#include "windows-udp-handler.h"
#include "lan866x_client_impl.hpp"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                          USER ADJUSTABLE                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define MAX_CALLBACKS           (8u)
#define SERVICE_INTERVAL_MS     (20u)
#define SEM_WAIT_TIMEOUT_MS     (1200u)

#define PRINT(...) printf(__VA_ARGS__); fflush(stdout)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                         LOCAL DEFINITIONS                            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

extern "C" uint8_t MULTICAST_IP[];
static std::mutex malloc_mutex;

#define MAX_AVAILABLE_SOCKETS   (32)
#define MAX_INTERFACES          (16)

struct udpSocketVar
{
    uint8_t tempBuffer[1522];
    SOMEIP_DataReceived_CB_t rxCB[MAX_CALLBACKS];
    void *rxTag[MAX_CALLBACKS];
    udp_t *udpSock;
    uint16_t udpPort;
    bool multicastJoined;
    bool used;
};

typedef struct
{
    uint8_t interfaceIP[4];
    uint8_t subnetMask[4];
    bool multicastJoined;
    bool enumarated;
} InterfaceInfo_t;

struct udpLocalVar
{
    struct udpSocketVar sockets[MAX_AVAILABLE_SOCKETS];
    InterfaceInfo_t inf[MAX_INTERFACES];
    udp_t *multicastSock;
    HANDLE semSomeIP;
    HANDLE semServiceTimer;
    HANDLE serviceThread;
    DWORD serviceThreadId;
    bool allowThreadRun;
    bool threadServiceRuns;
    bool initialized;
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                PRIVATE VARIABLES AND FUNCTION PROTOTYPES             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static struct udpLocalVar m = {};

static bool Init(void);
static DWORD WINAPI ServiceThread( LPVOID lpParam );
static void OnLocalIp(
        const char *adapterName,
        const char *friendlyName,
        const uint8_t localIPv4[4],
        uint8_t subnetLength,
        const uint8_t localIPv6[16],
        uint8_t ipv6PrefixLength,
        const uint8_t macAddress[6],
        uint64_t linkSpeed);
static void PollNetworkStatusAndJoinMulticast(void);
static void OnUdpRx(udp_t *pUdp, const uint8_t remoteIp[4], uint16_t remotePort, const uint8_t *buf, uint16_t len);

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    CALLBACKS FROM SOME/IP COMPONENT                  */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void SOMEIP_CB_Log(const char *logMsg)
{
    PRINT("SOME-IP-log:%s\r\n", logMsg);
}

bool SOMEIP_CB_OpenSocket(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback, void *rxTag, void **sockHandle)
{
    uint8_t i;
    bool existing = false;
    bool newEntry = false;

    if (sockHandle && Init()) {
        *sockHandle = NULL;
        /* Find already used socket entry */
        for (i = 0; !existing && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (pSock->used && (pSock->udpPort == *udpPort)) {
                uint8_t j;
                /* Check if already exists */
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++) {
                    existing = ((rxCallback == pSock->rxCB[j]) && (rxTag == pSock->rxTag[j]));
                }
                /* Find new entry */
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++) {
                    if (NULL ==  pSock->rxCB[j]) {
                        pSock->rxCB[j] = rxCallback;
                        pSock->rxTag[j] = rxTag;
                        existing = true;
                    }
                }
                if (existing) {
                    *sockHandle = pSock;
                } else {
                    PRINT("Failed to register callback for RX thread\r\n");
                    return false;
                }
            }
        }

        /* Find free socket entry */
        for (i = 0; !existing && !newEntry && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (!pSock->used) {
                pSock->used = true;
                pSock->udpSock = NULL;
                pSock->rxCB[0] = rxCallback;
                pSock->rxTag[0] = rxTag;

                pSock->udpSock = WinUdpHandler_CreateSocket(udpPort, OnUdpRx);
                pSock->udpPort = *udpPort;
                if (pSock->udpSock) {
                    newEntry = true;
                    if(SD_PORT == pSock->udpPort) {
                        m.multicastSock = pSock->udpSock;
                    }
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

bool SOMEIP_CB_GetLocalIpAddr(uint8_t localIP[SOMEIP_IPV4_ADDR_LEN], const uint8_t targetIP[SOMEIP_IPV4_ADDR_LEN])
{
    bool success = false;
    if (localIP && targetIP && targetIP[0]) {
        for (uint8_t i = 0; !success && (i < MAX_INTERFACES); i++) {
            InterfaceInfo_t *pInfo = &m.inf[i];
            bool match = true;
            for (uint8_t i = 0; match && (i < 4); i++) {
                if (pInfo->enumarated && pInfo->subnetMask[i]) {
                    uint8_t maskedTarget = (targetIP[i] & pInfo->subnetMask[i]);
                    uint8_t maskedLocal = (pInfo->interfaceIP[i] & pInfo->subnetMask[i]);
                    match = (maskedLocal == maskedTarget);
                }
            }
            if (match) {
                memcpy(localIP, pInfo->interfaceIP, 4);
                success = true;
            }
        }
    }
    return success;
}

void SOMEIP_CB_EnterCriticialSection(void)
{
    WaitForSingleObject(m.semSomeIP, INFINITE);
}

void SOMEIP_CB_LeaveCriticialSection(void)
{
    LONG previous;
    ReleaseSemaphore(m.semSomeIP, 1, &previous);
}

bool SOMEIP_CB_SemInit(SOME_IP_SEM_t *sem, int8_t initialValue)
{
    bool success = false;
    *sem = NULL;
    HANDLE semHandle = CreateSemaphoreA(NULL, initialValue, 1, NULL);
    if (semHandle) {
        *sem = semHandle;
        success = true;
    }
    return success;
}

bool SOMEIP_CB_SemWait(SOME_IP_SEM_t *sem)
{
    bool success = false;
    if ((NULL != sem) && (NULL != *sem)) {
        if (WAIT_OBJECT_0 == WaitForSingleObject((HANDLE)*sem, SEM_WAIT_TIMEOUT_MS)) {
            success = true;
        }
    }
    return success;
}

void SOMEIP_CB_SemPost(SOME_IP_SEM_t *sem)
{
    if ((NULL != sem) && (NULL != *sem)) {
        LONG previous;
        ReleaseSemaphore((HANDLE)*sem, 1, &previous);
    }
}

void SOMEIP_CB_SemDestroy(SOME_IP_SEM_t *sem)
{
    if ((NULL != sem) && (NULL != *sem)) {
        CloseHandle((HANDLE)*sem);
        *sem = NULL;
    }
}

void SOMEIP_CB_NeedService(void)
{
    /* Working thread waiting with WaitForSingleObject()*/
    ReleaseSemaphore(m.semServiceTimer, 1, NULL);
}

bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length)
{
    std::lock_guard<std::mutex> lock(malloc_mutex);
    bool success = false;
    uint8_t *pBuffer;

    pBuffer = (uint8_t *)malloc(length);
    if (NULL != pBuffer) {
        *ppBuffer = pBuffer;
        *ppMemTag = pBuffer;
        success = true;
    }
    return success;
}

bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag, const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port, void *sockHandle)
{
    struct udpSocketVar *pSock = (struct udpSocketVar *)sockHandle;
    bool success = false;
    (void)pMemTag;
    if (pSock) {
        if (0 == memcmp(MULTICAST_IP, ipAddrV4, 4)) {
            if (!pSock->multicastJoined) {
                for (uint8_t i = 0; !success && (i < MAX_INTERFACES); i++) {
                    InterfaceInfo_t *pInfo = &m.inf[i];
                    if (pInfo->enumarated) {
                        if (WinUdpHandler_JoinMulticastGroup(pSock->udpSock, MULTICAST_IP, pInfo->interfaceIP, pSock->udpPort)) {
                            pSock->multicastJoined = true;
                        }
                    }
                }
            }
        }
        success = WinUdpHandler_Send(pSock->udpSock, ipAddrV4, port, pBuf, length);
        if (!success) {
            PRINT("SOMEIP_CB_SendUdp ip=%d.%d.%d.%d errno=%d\r\n", ipAddrV4[0], ipAddrV4[1], ipAddrV4[2], ipAddrV4[3], errno);
        }
    }
    if (pMemTag) {
        std::lock_guard<std::mutex> lock(malloc_mutex);
        free(pMemTag);
    }
    return success;
}

void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr)
{
    PRINT("Assertion in '%s' Line=%d\r\n", pFilename, lineNr);
#if DEBUG
    exit(-1);
#endif
}

uint32_t SOMEIP_CB_GetTimeMS(void)
{
    return GetTickCount();
}

uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max)
{
    uint32_t diff = (max - min + 1);
    uint32_t val = (rand() % diff);
    return (val + min);
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                         PRIVATE FUNCTIONS                            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static bool Init(void)
{
    if (!m.initialized) {
        m.initialized = true;
        m.allowThreadRun = true;
        m.semSomeIP = CreateSemaphoreA(NULL, 1, 1, NULL);
        if (!m.semSomeIP) {
            PRINT("Failed to create semaphore for SOME/IP stack\r\n");
            m.initialized = false;
        }
        m.semServiceTimer = CreateSemaphoreA(NULL, 0, 1, NULL);
        if (!m.semServiceTimer) {
            PRINT("Failed to create mutex for cyclic servicing\r\n");
            m.initialized = false;
        }
        m.serviceThread = CreateThread(
                    NULL,                   // default security attributes
                    0,                      // use default stack size
                    ServiceThread,          // thread function name
                    NULL,                   // argument to thread function
                    0,                      // use default creation flags
                    &m.serviceThreadId);    // returns the thread identifier
        if (!m.serviceThread) {
            PRINT("Failed to start service thread\r\n");
            m.initialized = false;
        }
    }
    return m.initialized;
}

static DWORD WINAPI ServiceThread( LPVOID lpParam )
{
    (void)lpParam;
    m.threadServiceRuns = true;
    while(m.allowThreadRun) {
        WaitForSingleObject(m.semServiceTimer, SERVICE_INTERVAL_MS);

        SOMEIP_Client_CheckTimers();

        PollNetworkStatusAndJoinMulticast();
    }
    m.threadServiceRuns = false;
    return 0;
}

static void OnLocalIp(const char *adapterName,
                      const char *friendlyName,
                      const uint8_t localIPv4[4],
                      uint8_t subnetLength,
                      const uint8_t localIPv6[16],
                      uint8_t ipv6PrefixLength,
                      const uint8_t macAddress[6],
                      uint64_t linkSpeed)
{
    bool found = false;
    (void)adapterName;
    (void)friendlyName;
    (void)subnetLength;
    (void)localIPv6;
    (void)ipv6PrefixLength;
    (void)macAddress;
    (void)linkSpeed;
    /* Find existing entry */
    for (uint8_t i = 0; !found && (i < MAX_INTERFACES); i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        if (0 == memcmp(pInfo->interfaceIP, localIPv4, 4)) {
            found = true;
            pInfo->enumarated = true;
        }
    }

    /* If not found, create new entry */
    for (uint8_t i = 0; !found && (i < MAX_INTERFACES); i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        if (0 == pInfo->interfaceIP[0]) {
            found = true;
            memcpy(pInfo->interfaceIP, localIPv4, 4);
            uint32_t mask = (subnetLength == 0) ? 0 : (0xFFFFFFFF << (32 - subnetLength));
            pInfo->subnetMask[0] = (mask >> 24) & 0xFF;
            pInfo->subnetMask[1] = (mask >> 16) & 0xFF;
            pInfo->subnetMask[2] = (mask >> 8) & 0xFF;
            pInfo->subnetMask[3] = mask & 0xFF;
            pInfo->multicastJoined = false;
            pInfo->enumarated = true;
            //PRINT("Local IP=%d.%d.%d.%d subnet=%d.%d.%d.%d\r\n", localIP[0], localIP[1], localIP[2], localIP[3],
            //        pInfo->subnetMask[0], pInfo->subnetMask[1], pInfo->subnetMask[2], pInfo->subnetMask[3]);
        }
    }

    if (!found) {
        PRINT("Could not store Local IP info, increase MAX_INTERFACES define\r\n");
    }
}

static void PollNetworkStatusAndJoinMulticast(void)
{
    /* Mark all interfaces as not enumerated */
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        pInfo->enumarated = false;
    }

    if (!WinUdpHandler_GetLocalIPs(OnLocalIp)) {
        PRINT("WinUdpHandler_GetLocalIPs() failed\r\n");
    }

    /* Process all interfaces after enumeration result */
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        InterfaceInfo_t *pInfo = &m.inf[i];
        /* Check for lost interfaces */
        if (!pInfo->enumarated && pInfo->interfaceIP[0]) {
            PRINT("Multicast interface lost, IP=%d.%d.%d.%d\r\n", pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3]);
            memset(pInfo, 0, sizeof(InterfaceInfo_t));
        }
        /* Try to join Multicast, if not already done */
        else if (pInfo->enumarated && m.multicastSock && !pInfo->multicastJoined) {
            if (WinUdpHandler_JoinMulticastGroup(m.multicastSock, MULTICAST_IP, pInfo->interfaceIP, SD_PORT)) {
                PRINT("Joined Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                       MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                        pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3],
                        SD_PORT);
                pInfo->multicastJoined = true;
            } else {
                PRINT("Failed to join to Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                       MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                        pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3],
                        SD_PORT);
            }
        }
    }
}

static void OnUdpRx(udp_t *pUdp, const uint8_t ip[4], uint16_t remotePort, const uint8_t *buf, uint16_t len)
{
    struct udpSocketVar *pSock = NULL;
    uint8_t i;
    for (i = 0; !pSock && (i < MAX_AVAILABLE_SOCKETS); i++) {
        struct udpSocketVar *pTmp = &m.sockets[i];
        if (pTmp->used && (pTmp->udpSock == pUdp)) {
            pSock = pTmp;
        }
    }
    if (pSock) {
        struct SOMEIP_IpAddr rxIp;
        uint8_t j;
        memset(&rxIp, 0, sizeof(struct SOMEIP_IpAddr));
        if (SOMEIP_CB_GetLocalIpAddr(rxIp.sourceAddr, ip)) {
            memcpy(rxIp.destinAddr, ip, 4);
            rxIp.port = remotePort;
            for (j = 0; j < MAX_CALLBACKS; j++) {
                if (NULL != pSock->rxCB[j]) {
                    enum SOMEIP_ReturnCode result = pSock->rxCB[j](buf, len, &rxIp, pSock->rxTag[j]);
                    if (SOMEIP_E_OK != result) {
                        PRINT("SOME/IP parsing (port %d) failed with error code 0x%X\r\n", remotePort, result);
                    }
                }
            }
        } else {
            PRINT("Could not found local IP for RX message from %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        }
    }
}

#else /* WIN32 */
/*------------------------------------------------------------------------------------------------*/
/* SOMEIP Stub Code for Linux                                                                     */
/*------------------------------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <net/if.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <mutex>
#include "someip.h"
#include "someip-cfg.h"
#include "lan866x_client_impl.hpp"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                          USER ADJUSTABLE                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define MAX_CALLBACKS           (8u)
#define SERVICE_INTERVAL_MS     (20u)
#define SEM_WAIT_TIMEOUT_MS     (1200u)
#define MAX_RETRIES             (5)
#define RETRY_DELAY_US          (10000)

#define PRINT(...) printf(__VA_ARGS__); fflush(stdout)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                         LOCAL DEFINITIONS                            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define MAX_AVAILABLE_SOCKETS   (32u)
#define MAX_INTERFACES          (16u)

struct state_vars;

struct udpSocketVar
{
    uint8_t tempBuffer[1522];
    SOMEIP_DataReceived_CB_t rxCB[MAX_CALLBACKS];
    void *rxTag[MAX_CALLBACKS];
    pthread_t threadRX;
    sem_t semSock;
    int udpSock;
    uint16_t udpPort;
    bool threadRXRuns;
    bool used;
    char ethn[32];
};

struct InterfaceInfo
{
    uint8_t interfaceIP[4];
    uint8_t subnetMask[4];
    bool multicastJoined;
    bool enumarated;
};

struct udpLocalVar
{
    struct udpSocketVar sockets[MAX_AVAILABLE_SOCKETS];
    struct InterfaceInfo inf[MAX_INTERFACES];
    int multicastSock;
    pthread_t threadService;
    sem_t semSomeIP;
    sem_t semServiceTimer;
    bool allowThreadRun;
    bool threadServiceRuns;
    bool initialized;
};

#ifdef NO_RAW_CLOCK
#define CLOCK_SRC CLOCK_MONOTONIC
#else
#define CLOCK_SRC CLOCK_MONOTONIC_RAW
#endif

static std::mutex malloc_mutex;
static std::mutex socket_mutex;
extern uint8_t MULTICAST_IP[];

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                PRIVATE VARIABLES AND FUNCTION PROTOTYPES             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static struct udpLocalVar m = {};

static bool Init(void);
static bool OpenSocket(struct udpSocketVar *pSock);
static void *ServiceThread(void *tag);
static void *ReceiveThread(void *tag);

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    CALLBACKS FROM SOME/IP COMPONENT                  */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

void SOMEIP_CB_Log(const char *logMsg)
{
    PRINT("SOMEIP_CB_Log:%s\r\n", logMsg);
}

bool SOMEIP_CB_OpenSocket(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback, void *rxTag, void **sockHandle)
{
    uint8_t i;
    bool existing = false;
    bool newEntry = false;

    if (sockHandle && Init()) {
        *sockHandle = NULL;
        /* Find already used socket entry */
        for (i = 0; !existing && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (pSock->used && (pSock->udpPort == *udpPort)) {
                uint8_t j;
                /* Check if already exists */
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++) {
                    existing = ((rxCallback == pSock->rxCB[j]) && (rxTag == pSock->rxTag[j]));
                }
                /* Find new entry */
                for (j = 0; !existing && (j < MAX_CALLBACKS); j++) {
                    if (NULL ==  pSock->rxCB[j]) {
                        pSock->rxCB[j] = rxCallback;
                        pSock->rxTag[j] = rxTag;
                        existing = true;
                    }
                }
                if (existing) {
                    *sockHandle = pSock;
                } else {
                    PRINT("Failed to register callback for RX thread\r\n");
                    return false;
                }
            }
        }

        /* Find free socket entry */
        for (i = 0; !existing && !newEntry && (i < MAX_AVAILABLE_SOCKETS); i++) {
            struct udpSocketVar *pSock = &m.sockets[i];
            if (!pSock->used) {
                bool success = false;
                pSock->used = true;
                pSock->udpPort = *udpPort;
                pSock->udpSock = -1;
                pSock->rxCB[0] = rxCallback;
                pSock->rxTag[0] = rxTag;
                if (-1 == (sem_init(&pSock->semSock, 0, 1))) {
                    pSock->used = false;
                    PRINT("Failed to create socket semaphore for RX thread\r\n");
                    break;
                }

                if (OpenSocket(pSock)) {
                    if (0 == pthread_create(&pSock->threadRX, NULL, ReceiveThread, pSock)) {
                        success = true;
                        newEntry = true;
                        *sockHandle = pSock;
                        *udpPort = pSock->udpPort;
                        if(SD_PORT == pSock->udpPort) {
                            m.multicastSock = pSock->udpSock;
                        }
                    } else {
                        close(pSock->udpSock);
                        PRINT("Failed to start receive thread\r\n");
                    }
                } else {
                    PRINT("Failed to open socket\r\n");
                }
                if (!success) {
                    pSock->udpSock = -1;
                    pSock->used = false;
                    pSock->rxCB[0] = NULL;
                    pSock->rxTag[0] = NULL;
                    sleep(1);
                    break;
                }
            }
        }
    }
    return (existing || newEntry);
}

bool SOMEIP_CB_GetLocalIpAddr(uint8_t localIP[SOMEIP_IPV4_ADDR_LEN], const uint8_t targetIP[SOMEIP_IPV4_ADDR_LEN])
{
    bool success = false;
    if (localIP && targetIP && targetIP[0]) {
        for (uint8_t i = 0; !success && (i < MAX_INTERFACES); i++) {
            struct InterfaceInfo *pInfo = &m.inf[i];
            bool match = true;
            for (uint8_t i = 0; match && (i < 4); i++) {
                if (pInfo->subnetMask[i]) {
                    uint8_t maskedTarget = (targetIP[i] & pInfo->subnetMask[i]);
                    uint8_t maskedLocal = (pInfo->interfaceIP[i] & pInfo->subnetMask[i]);
                    match = (maskedLocal == maskedTarget);
                }
            }
            if (match) {
                memcpy(localIP, pInfo->interfaceIP, 4);
                success = true;
            }
        }
    }
    return success;
}

void SOMEIP_CB_EnterCriticialSection(void)
{
    sem_wait(&m.semSomeIP);
}

void SOMEIP_CB_LeaveCriticialSection(void)
{
    sem_post(&m.semSomeIP);
}

bool SOMEIP_CB_SemInit(SOME_IP_SEM_t *sem, int8_t initialValue)
{
    bool success = false;
    sem_t *pSem = (sem_t *)malloc(sizeof(sem_t));
    success = (0 == sem_init(pSem, 0, initialValue));
    if (success) {
        *sem = pSem;
    } else {
        free(pSem);
    }
    return success;
}

bool SOMEIP_CB_SemWait(SOME_IP_SEM_t *sem)
{
    bool success = false;
    if ((NULL != sem) && (NULL != *sem)) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != -1) {
            int retries = 0;
            ts.tv_nsec += SEM_WAIT_TIMEOUT_MS * 1000000;
            /* Need to normalize after adjusting nsec */
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
            while (true) {
                if (sem_timedwait((sem_t *)*sem, &ts) == 0) {
                    success = true;
                    break;
                } else {
                    if (errno == ETIMEDOUT) {
                        success = false;
                        break;
                    } else {
                        perror("sem_wait failed");
                        if (++retries >= MAX_RETRIES) {
                            success = false;
                            fprintf(stderr, "Max retries reached for sem_wait\n");
                            break;
                        }
                        usleep(RETRY_DELAY_US);
                    }
                }
            }
        }
    } else {
        fprintf(stderr, "Invalid semaphore pointer in SOMEIP_CB_SemWait\n");
    }
    return success;
}

void SOMEIP_CB_SemPost(SOME_IP_SEM_t *sem)
{
    if ((NULL != sem) && (NULL != *sem)) {
        int retries = 0;
        while (sem_post((sem_t *)*sem) != 0) {
            perror("sem_post failed");
            if (++retries >= MAX_RETRIES) {
                fprintf(stderr, "Max retries reached for sem_post\n");
                break;
            }
            usleep(RETRY_DELAY_US);
        }
    } else {
        fprintf(stderr, "Invalid semaphore pointer in SOMEIP_CB_SemPost\n");
    }
}

void SOMEIP_CB_SemDestroy(SOME_IP_SEM_t *sem)
{
    if ((NULL != sem) && (NULL != *sem)) {
        int retries = 0;
        while (sem_destroy((sem_t *)*sem) != 0) {
            perror("sem_destroy failed");
            if (++retries >= MAX_RETRIES) {
                fprintf(stderr, "Max retries reached for sem_destroy\n");
                break;
            }
            usleep(RETRY_DELAY_US);
        }
        free(*sem);
        *sem = NULL;  // Avoid dangling pointer
    } else {
        fprintf(stderr, "Invalid semaphore pointer in SOMEIP_CB_SemDestroy\n");
    }
}

void SOMEIP_CB_NeedService(void)
{
    /* Waking thread waiting with sem_timedwait()*/
    sem_post(&m.semServiceTimer);
}

bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length)
{
    std::lock_guard<std::mutex> lock(malloc_mutex);
    bool success = false;
    uint8_t *pBuffer = (uint8_t *)malloc(length);
    if (NULL != pBuffer) {
        *ppBuffer = pBuffer;
        *ppMemTag = pBuffer;
        success = true;
    }
    return success;
}

bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag, const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port, void *sockHandle)
{
    struct udpSocketVar *pSock = (struct udpSocketVar *)sockHandle;
    bool success = false;
    (void)pMemTag;
    if ((NULL != pSock) && OpenSocket(pSock)) {
        struct sockaddr_in destination;
        ssize_t bytesSent = 0;
        memset( &destination, 0, sizeof ( destination ) );
        destination.sin_family = AF_INET;
        destination.sin_addr.s_addr = (ipAddrV4[3] << 24) |
                (ipAddrV4[2] << 16) |
                (ipAddrV4[1] << 8) |
                ipAddrV4[0];
        destination.sin_port = htons( port );

        // Protect sendto call with a mutex because socket is shared
        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            bytesSent = sendto(pSock->udpSock, pBuf, length, 0, (struct sockaddr *)&destination, sizeof(destination));
        }
        success = (bytesSent == length);
        if (!success) {
            PRINT("SOMEIP_CB_SendUdp errno=%d\r\n", errno);
        }
    }
    if (NULL != pMemTag) {
        std::lock_guard<std::mutex> lock(malloc_mutex);
        free(pMemTag);
    }
    return success;
}

void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr)
{
    PRINT("Assertion in '%s' Line=%d\r\n", pFilename, lineNr);
#if DEBUG
    exit(-1);
#endif
}

uint32_t SOMEIP_CB_GetTimeMS(void)
{
    struct timespec currentTime;
    if (clock_gettime(CLOCK_SRC, &currentTime))
    {
        assert(false);
        return 0;
    }
    return ( currentTime.tv_sec * 1000 ) + ( currentTime.tv_nsec / 1000000 );
}

uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max)
{
    uint32_t diff = (max - min + 1);
    uint32_t val = (rand() % diff);
    return (val + min);
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                         PRIVATE FUNCTIONS                            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static bool Init(void)
{
    if (!m.initialized) {
        m.allowThreadRun = true;
        if (-1 == (sem_init(&m.semSomeIP, 0, 1))) {
            PRINT("Failed to create socket semaphore for SOME/IP stack\r\n");
        }
        if (-1 == (sem_init(&m.semServiceTimer, 0, 0))) {
            PRINT("Failed to create socket semaphore for cyclic servicing\r\n");
        }
        else if (0 != pthread_create(&m.threadService, NULL, ServiceThread, &m)) {
            PRINT("Failed to start receive thread\r\n");
        } else {
            m.initialized = true;
        }
    }
    return m.initialized;
}

static bool OpenSocket(struct udpSocketVar *pSock)
{
    sem_wait(&pSock->semSock);

    if (pSock->udpSock < 0) {
        struct sockaddr_in listenAddress;
        int enableSocketFlag;
        bool success = false;
        memset(&listenAddress, 0, sizeof(listenAddress));
        listenAddress.sin_family = AF_INET;
        listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        listenAddress.sin_port = htons(pSock->udpPort);
        pSock->udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        success = (pSock->udpSock >= 0);

        enableSocketFlag = 1;
        success = success && (setsockopt(pSock->udpSock, SOL_SOCKET, SO_REUSEADDR, &enableSocketFlag, sizeof(enableSocketFlag)) >= 0); /* Try to reuse hanging socket*/
        enableSocketFlag = 1;
        success = success && (setsockopt(pSock->udpSock, SOL_SOCKET, SO_BROADCAST, &enableSocketFlag, sizeof(enableSocketFlag)) >= 0); /* Enable broadcast */
        enableSocketFlag = 1;
        success = success && (setsockopt(pSock->udpSock, IPPROTO_IP, IP_PKTINFO, &enableSocketFlag, sizeof(enableSocketFlag)) >= 0);   /* Give detailed information about received packet */
        if (success) {
            success = (bind(pSock->udpSock, (struct sockaddr *)&listenAddress, sizeof(listenAddress)) >= 0);
            if (success) {
                /* Find assigned port in case of random port number (0) */
                if (0u == pSock->udpPort) {
                    socklen_t len = sizeof(listenAddress);
                    getsockname(pSock->udpSock, (struct sockaddr *) &listenAddress, &len);
                    pSock->udpPort = ntohs(listenAddress.sin_port);
                }
            } else {
                PRINT("Port %d is in use, is application running multiple times?\r\n", pSock->udpPort);
            }
        }

        if (!success && pSock->udpSock >= 0) {
            close(pSock->udpSock);
            pSock->udpSock = -1;
        }
    }
    sem_post(&pSock->semSock);

    return (pSock->udpSock >= 0);
}

static void OnLocalIp(uint8_t localIP[4], uint8_t subnet[4])
{
    //PRINT("IP: %u.%u.%u.%u, Mask: %u.%u.%u.%u\n", localIP[0], localIP[1], localIP[2], localIP[3], subnet[0], subnet[1], subnet[2], subnet[3]);
    bool found = false;
    /* Find existing entry */
    for (uint8_t i = 0; !found && (i < MAX_INTERFACES); i++) {
        struct InterfaceInfo *pInfo = &m.inf[i];
        if (0 == memcmp(pInfo->interfaceIP, localIP, 4)) {
            found = true;
            pInfo->enumarated = true;
        }
    }

    /* If not found, create new entry */
    for (uint8_t i = 0; !found && (i < MAX_INTERFACES); i++) {
        struct InterfaceInfo *pInfo = &m.inf[i];
        if (0 == pInfo->interfaceIP[0]) {
            found = true;
            memcpy(pInfo->interfaceIP, localIP, 4);
            memcpy(pInfo->subnetMask, subnet, 4);
            pInfo->multicastJoined = false;
            pInfo->enumarated = true;
            //PRINT("Local IP=%d.%d.%d.%d subnet=%d.%d.%d.%d\r\n", localIP[0], localIP[1], localIP[2], localIP[3],
            //        pInfo->subnetMask[0], pInfo->subnetMask[1], pInfo->subnetMask[2], pInfo->subnetMask[3]);
        }
    }

    if (!found) {
        PRINT("Could not store Local IP info, increase MAX_INTERFACES define\r\n");
    }
}

static void CheckMulticastStatus()
{
    /* Mark all interfaces as not enumerated */
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        struct InterfaceInfo *pInfo = &m.inf[i];
        pInfo->enumarated = false;
    }
    const char sysfs[] = "/sys/class/net";
    DIR* directory = opendir(sysfs);
    if (NULL != directory) {
        struct dirent* entry = NULL;
        while ((entry = readdir(directory)) != NULL) {
            if (entry->d_type == DT_LNK) {
                const char *interfaceName = entry->d_name;
                struct ifaddrs* ifaddr = 0;
                struct ifaddrs* ifa = 0;

                if (0 == getifaddrs(&ifaddr)) {
                    for (ifa = ifaddr; (ifa != NULL); ifa = ifa->ifa_next) {
                        if (ifa->ifa_addr &&
                                ifa->ifa_addr->sa_family == AF_INET &&
                                strcmp(ifa->ifa_name, interfaceName) == 0 &&
                                (ifa->ifa_flags & IFF_UP)) {

                            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
                            uint8_t ipAddrV4[4];
                            memcpy(ipAddrV4, &sin->sin_addr, 4);

                            // Filter out link-local and loopback
                            if (ipAddrV4[0] == 169 && ipAddrV4[1] == 254) continue;
                            if (ipAddrV4[0] == 127) continue;

                            struct sockaddr_in *netmask_in = (struct sockaddr_in *)ifa->ifa_netmask;
                            uint8_t subnet[4];
                            memcpy(subnet, &netmask_in->sin_addr, 4);

                            OnLocalIp(ipAddrV4, subnet);
                        }
                    }
                    freeifaddrs(ifaddr);
                }
            }
        }
        closedir(directory);
    }

    /* Process all interfaces after enumeration result */
    for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
        struct InterfaceInfo *pInfo = &m.inf[i];
        /* Check for lost interfaces */
        if (!pInfo->enumarated && pInfo->interfaceIP[0]) {
            PRINT("Multicast interface lost, IP=%d.%d.%d.%d\r\n", pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3]);
            memset(pInfo, 0, sizeof(struct InterfaceInfo));
        }
        /* Try to join Multicast, if not already done */
        else if (pInfo->enumarated && m.multicastSock && !pInfo->multicastJoined) {
            struct ip_mreq mr;
            mr.imr_interface.s_addr = (pInfo->interfaceIP[3] << 24) |
                    (pInfo->interfaceIP[2] << 16) |
                    (pInfo->interfaceIP[1] << 8) |
                    pInfo->interfaceIP[0];
            mr.imr_multiaddr.s_addr = (MULTICAST_IP[3] << 24) |
                    (MULTICAST_IP[2] << 16) |
                    (MULTICAST_IP[1] << 8) |
                    MULTICAST_IP[0];
            bool success = (setsockopt(m.multicastSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mr, sizeof(mr)) >= 0);

            if (success) {
                /* Set the outgoing interface for multicast packets */
                struct in_addr localInterface;
                localInterface.s_addr = (pInfo->interfaceIP[3] << 24) |
                        (pInfo->interfaceIP[2] << 16) |
                        (pInfo->interfaceIP[1] << 8) |
                        pInfo->interfaceIP[0];
                success = (setsockopt(m.multicastSock, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface)) >= 0);
                if (!success) {
                    PRINT("Failed to set the outgoing interface for multicast packets\r\n");
                }
            } else {
                PRINT("Failed to join Multicast Group\r\n");
            }
            if (success) {
                PRINT("Joined Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                       MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                        pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3],
                        SD_PORT);
                pInfo->multicastJoined = true;
            } else {
                PRINT("Failed to join to Multicast group %d.%d.%d.%d for local %d.%d.%d.%d, port %d.\r\n",
                       MULTICAST_IP[0], MULTICAST_IP[1], MULTICAST_IP[2], MULTICAST_IP[3],
                        pInfo->interfaceIP[0], pInfo->interfaceIP[1], pInfo->interfaceIP[2], pInfo->interfaceIP[3],
                        SD_PORT);
            }
        }
    }
}

static void *ServiceThread(void *tag)
{
    struct udpLocalVar *d = (struct udpLocalVar *)tag;
    assert(NULL != d);
    d->threadServiceRuns = true;
    while(m.allowThreadRun) {
        struct timeval tv;
        struct timespec ts;

        gettimeofday(&tv, NULL);
        ts.tv_sec = time(NULL) + SERVICE_INTERVAL_MS / 1000;
        ts.tv_nsec = tv.tv_usec * 1000 + 1000 * 1000 * (SERVICE_INTERVAL_MS % 1000);
        ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
        ts.tv_nsec %= (1000 * 1000 * 1000);
        while ((sem_timedwait(&d->semServiceTimer, &ts)) == -1 && errno == EINTR);

        SOMEIP_Client_CheckTimers();

        CheckMulticastStatus();
    }
    d->threadServiceRuns = false;
    return tag;
}

static void *ReceiveThread(void *tag)
{
    struct udpSocketVar *pSock = (struct udpSocketVar *)tag;
    assert(NULL != pSock);
    pSock->threadRXRuns = true;
    while(m.allowThreadRun) {
        ssize_t receivedLength;
        // the control data is dumped here
        char cmbuf[0x100];
        // the remote/source sockaddr is put here
        struct sockaddr_in peeraddr;
        struct iovec iov[1];

        while(m.allowThreadRun && !OpenSocket(pSock)) {

            struct timespec t;
            t.tv_sec = 0;
            t.tv_nsec = 300000000l;
            nanosleep(&t, NULL);
        }
        if (!m.allowThreadRun) {
            break;
        }
        iov[0].iov_base = pSock->tempBuffer;
        iov[0].iov_len = sizeof(pSock->tempBuffer);

        struct msghdr mh = {
            .msg_name = &peeraddr,
                    .msg_namelen = sizeof(peeraddr),
                    .msg_iov = iov,
                    .msg_iovlen = 1,
                    .msg_control = cmbuf,
                    .msg_controllen = sizeof(cmbuf),
                    .msg_flags = 0
        };
        receivedLength = recvmsg(pSock->udpSock, &mh, 0);
        if (receivedLength > 0) {
            // iterate through all the control headers
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mh); cmsg != NULL; cmsg = CMSG_NXTHDR(&mh, cmsg)) {

                struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
                struct SOMEIP_IpAddr remoteIp;
                uint8_t j;

                // ignore the control headers that don't match what we want
                if (cmsg->cmsg_level != IPPROTO_IP || cmsg->cmsg_type != IP_PKTINFO) {
                    continue;
                }

                // at this point, peeraddr is the source sockaddr
                // pi->ipi_spec_dst is the destination in_addr
                // pi->ipi_addr is the receiving interface in_addr

                remoteIp.sourceAddr[3] = peeraddr.sin_addr.s_addr >> 24;
                remoteIp.sourceAddr[2] = peeraddr.sin_addr.s_addr >> 16;
                remoteIp.sourceAddr[1] = peeraddr.sin_addr.s_addr >> 8;
                remoteIp.sourceAddr[0] = peeraddr.sin_addr.s_addr;

                remoteIp.destinAddr[3] = pi->ipi_addr.s_addr >> 24;
                remoteIp.destinAddr[2] = pi->ipi_addr.s_addr >> 16;
                remoteIp.destinAddr[1] = pi->ipi_addr.s_addr >> 8;
                remoteIp.destinAddr[0] = pi->ipi_addr.s_addr;

                remoteIp.port = htons(peeraddr.sin_port);
#if 0
                PRINT("source=%d.%d.%d.%d dest=%d.%d.%d.%d port=%d\r\n", remoteIp.sourceAddr[0], remoteIp.sourceAddr[1], remoteIp.sourceAddr[2], remoteIp.sourceAddr[3],
                        remoteIp.destinAddr[0], remoteIp.destinAddr[1], remoteIp.destinAddr[2], remoteIp.destinAddr[3],
                        remoteIp.port);
#endif

                for (j = 0; j < MAX_CALLBACKS; j++) {
                    if (NULL != pSock->rxCB[j]) {
                        enum SOMEIP_ReturnCode result = pSock->rxCB[j](pSock->tempBuffer, receivedLength, &remoteIp, pSock->rxTag[j]);
                        if (SOMEIP_E_OK != result) {
                            PRINT("SOME/IP parsing (port %d) failed with error code 0x%X\r\n", remoteIp.port, result);
                        }
                    }
                }
                break;
            }
        } else {
            sem_wait(&pSock->semSock);
            close(pSock->udpSock);
            pSock->udpSock = -1;
            sem_post(&pSock->semSock);
        }
    }
    pSock->threadRXRuns = false;
    return tag;
}
#endif /* WIN32 */
