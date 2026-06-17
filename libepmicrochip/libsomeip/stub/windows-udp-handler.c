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
/* Windows UDP Handler                                                                            */
/*------------------------------------------------------------------------------------------------*/

#define _WIN32_WINNT 0x0601 // At least Windows 7

#include <stdio.h>
#include <stdarg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wchar.h>
#include <windows.h>
#include <assert.h>
#include "windows-udp-handler.h"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                      DEFINES AND LOCAL VARIABLES                     */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define PRINT(...)          printf(__VA_ARGS__);fflush(stdout)
#define MAGIC               (0xAFFEAFFE)

struct udp_t
{
    uint8_t rxBuf[1502];
    SOCKET udpSock;
    HANDLE rxThread;
    DWORD rxThreadId;
    WinUdpHandler_CB_RX callback;
    uint32_t magic;
    bool allowRun;
};

static bool s_initialized = false;
static uint16_t s_socketInstances = 0;

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    PRIVATE FUNCTIONS IMPLEMENTATIONS                 */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static DWORD WINAPI ReceiveThread(LPVOID lpParam)
{
    udp_t *pUDP = (udp_t *)lpParam;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    SOCKADDR_IN sourceAddr;
    int sourceAddrSize = sizeof(sourceAddr);
    DWORD flags = 0;
    DWORD bytesReceived = 0;

    wsaBuf.buf = (char *)pUDP->rxBuf;
    wsaBuf.len = sizeof(pUDP->rxBuf);

    while (pUDP && (pUDP->magic == MAGIC) && (pUDP->allowRun) && (pUDP->callback)) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));

        int ret = WSARecvFrom(
            pUDP->udpSock,
            &wsaBuf,
            1,
            &bytesReceived,
            &flags,
            (SOCKADDR *)&sourceAddr,
            &sourceAddrSize,
            &overlapped,
            NULL
        );

        if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            PRINT("WSARecvFrom failed with error %d\n", WSAGetLastError());
            pUDP->allowRun = false;
            break;
        }

        // Wait for the overlapped I/O to complete
        ret = WSAGetOverlappedResult(
            pUDP->udpSock,
            &overlapped,
            &bytesReceived,
            TRUE,
            &flags
        );

        if (ret) {
            if (bytesReceived > 0) {
                uint8_t remoteIP[4];
                remoteIP[0] = sourceAddr.sin_addr.S_un.S_un_b.s_b1;
                remoteIP[1] = sourceAddr.sin_addr.S_un.S_un_b.s_b2;
                remoteIP[2] = sourceAddr.sin_addr.S_un.S_un_b.s_b3;
                remoteIP[3] = sourceAddr.sin_addr.S_un.S_un_b.s_b4;
                uint16_t remotePort = ntohs(sourceAddr.sin_port);
                pUDP->callback(pUDP, remoteIP, remotePort, pUDP->rxBuf, (uint16_t)bytesReceived);
            }
        }
        else {
            PRINT("WSAGetOverlappedResult failed with error %d\n", WSAGetLastError());
            pUDP->allowRun = false;
            break;
        }
    }

    return 0;
}

static struct addrinfo *ResolveAddress(const char *addr, const char *port, int af, int type, int proto)
{
    struct addrinfo hints, *res = NULL;
    int             rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = ((addr) ? 0 : AI_PASSIVE);
    hints.ai_family = af;
    hints.ai_socktype = type;
    hints.ai_protocol = proto;

    rc = getaddrinfo(addr, port, &hints, &res);
    if (rc != 0) {
        PRINT("Invalid address %s, getaddrinfo failed: %d\n", addr, rc);
        return NULL;
    }
    return res;
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                            Public API                                */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

udp_t *WinUdpHandler_CreateSocket(uint16_t *bindPort, WinUdpHandler_CB_RX callback)
{
    udp_t *pUDP = NULL;
    if (!s_initialized) {
        WSADATA wsaData;
        int ret;
        // Initialize Winsock version 2.2
        if ((ret = WSAStartup(MAKEWORD(2, 2), &wsaData)) == 0) {
            s_initialized = true;
        }
        else {
            PRINT("WSAStartup failed with error %d\n", WSAGetLastError());
        }
    }
    if (s_initialized) {
        pUDP = (udp_t *)calloc(1, sizeof(struct udp_t));
        assert(pUDP);
        pUDP->magic = MAGIC;
        pUDP->udpSock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (pUDP->udpSock != INVALID_SOCKET) {
            bool success = true;
            char enableSocketFlag = 1;
            success = success && (setsockopt(pUDP->udpSock, SOL_SOCKET, SO_REUSEADDR, &enableSocketFlag, sizeof(enableSocketFlag)) >= 0); /* Try to reuse hanging socket*/
            enableSocketFlag = 1;
            success = success && (setsockopt(pUDP->udpSock, SOL_SOCKET, SO_BROADCAST, &enableSocketFlag, sizeof(enableSocketFlag)) >= 0); /* Enable broadcast */

            // Increase the receive buffer size
            int recvBufSize = 1024 * 1024; // 1 MB buffer size
            success = success && (setsockopt(pUDP->udpSock, SOL_SOCKET, SO_RCVBUF, (char *)&recvBufSize, sizeof(recvBufSize)) >= 0);

            if ((NULL != bindPort) && (NULL != callback)) {
                SOCKADDR_IN  receiverAddr;
                pUDP->callback = callback;
                receiverAddr.sin_family = AF_INET;
                receiverAddr.sin_port = htons(*bindPort);
                receiverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                if (bind(pUDP->udpSock, (SOCKADDR *)&receiverAddr, sizeof(receiverAddr)) != SOCKET_ERROR) {
                    /* Find assigned port in case of random port number (0) */
                    if (0u == *bindPort) {
                        struct sockaddr_in listenAddress;
                        socklen_t len = sizeof(listenAddress);
                        getsockname(pUDP->udpSock, (struct sockaddr *)&listenAddress, &len);
                        *bindPort = ntohs(listenAddress.sin_port);
                    }

                    /* Creating receive thread */
                    pUDP->allowRun = true;
                    pUDP->rxThread = CreateThread(
                        NULL,                   // default security attributes
                        0,                      // use default stack size
                        ReceiveThread,          // thread function name
                        pUDP,                   // argument to thread function
                        0,                      // use default creation flags
                        &pUDP->rxThreadId);     // returns the thread identifier
                    if (!pUDP->rxThread) {
                        PRINT("Could not start receive thread for port=%d\r\n", *bindPort);
                        success = false;
                    }
                    else {
                        // Set thread priority to highest
                        SetThreadPriority(pUDP->rxThread, THREAD_PRIORITY_TIME_CRITICAL);
                    }
                }
                else {
                    PRINT("Could not bind socket to port=%d, erro=%d\r\n", *bindPort, WSAGetLastError());
                    success = false;
                }
            }
            if (success) {
                s_socketInstances++;
            }
            else {
                closesocket(pUDP->udpSock);
                free(pUDP);
                pUDP = NULL;
            }
        }
        else {
            PRINT("CreateSocket failed, port=%d, error=%d\r\n", (NULL != bindPort ? *bindPort : 0xFFFFu), WSAGetLastError());
        }
    }
    return pUDP;
}

void WinUdpHandler_DestroySocket(udp_t *pUDP)
{
    if (pUDP && (pUDP->magic == MAGIC) && (pUDP->udpSock != INVALID_SOCKET)) {
        pUDP->allowRun = false;
        closesocket(pUDP->udpSock);
        free(pUDP);
        s_socketInstances--;
        if (s_socketInstances == 0) {
            if (WSACleanup() == SOCKET_ERROR) {
                PRINT("WSACleanup failed with error %d\n", WSAGetLastError());
            }
        }
    }
}

bool WinUdpHandler_Send(udp_t *pUDP, const uint8_t ip[4], uint16_t destPort, const uint8_t *buf, uint16_t len)
{
    bool success = false;
    if (pUDP && ip && destPort && (pUDP->magic == MAGIC) && (pUDP->udpSock != INVALID_SOCKET)) {
        int bytesSent;
        SOCKADDR_IN receiverAddr;
        receiverAddr.sin_family = AF_INET;
        receiverAddr.sin_port = htons(destPort);
        receiverAddr.sin_addr.S_un.S_un_b.s_b1 = ip[0];
        receiverAddr.sin_addr.S_un.S_un_b.s_b2 = ip[1];
        receiverAddr.sin_addr.S_un.S_un_b.s_b3 = ip[2];
        receiverAddr.sin_addr.S_un.S_un_b.s_b4 = ip[3];
        bytesSent = sendto(pUDP->udpSock, (const char *)buf, len, 0, (SOCKADDR *)&receiverAddr, sizeof(receiverAddr));
        if (bytesSent == SOCKET_ERROR) {
            PRINT("sendto failed, error=%d\n", WSAGetLastError());
            success = false;
        } else {
            success = (bytesSent == len);
        }
    }
    return success;
}

bool WinUdpHandler_JoinMulticastGroup(udp_t *pUDP, const uint8_t multicastIP[4], const uint8_t localIP[4], uint16_t port)
{
    bool success = false;
    char localString[16];
    char mcastString[16];
    char portString[8];
    snprintf(localString, sizeof(localString), "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
    snprintf(mcastString, sizeof(mcastString), "%d.%d.%d.%d", multicastIP[0], multicastIP[1], multicastIP[2], multicastIP[3]);
    snprintf(portString, sizeof(portString), "%d", port);
    struct addrinfo *group = ResolveAddress(mcastString, portString, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);
    struct addrinfo *iface = ResolveAddress(localString, portString, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);
    if (group && iface) {
        struct ip_mreq mreqv4;
        struct ipv6_mreq mreqv6;
        char *optval = NULL;
        int optlevel = 0;
        int option = 0;
        int optlen = 0;
        int rc = NO_ERROR;
        if (group->ai_family == AF_INET) {
            // Setup the v4 option values and ip_mreq structure
            optlevel = IPPROTO_IP;
            option = IP_ADD_MEMBERSHIP;
            optval = (char *)&mreqv4;
            optlen = sizeof(mreqv4);

            mreqv4.imr_multiaddr.s_addr = ((SOCKADDR_IN *)group->ai_addr)->sin_addr.s_addr;
            mreqv4.imr_interface.s_addr = ((SOCKADDR_IN *)iface->ai_addr)->sin_addr.s_addr;
        }
        else if (group->ai_family == AF_INET6) {
            // Setup the v6 option values and ipv6_mreq structure
            optlevel = IPPROTO_IPV6;
            option = IPV6_ADD_MEMBERSHIP;
            optval = (char *)&mreqv6;
            optlen = sizeof(mreqv6);

            mreqv6.ipv6mr_multiaddr = ((SOCKADDR_IN6 *)group->ai_addr)->sin6_addr;
            mreqv6.ipv6mr_interface = ((SOCKADDR_IN6 *)iface->ai_addr)->sin6_scope_id;
        }
        else {
            fprintf(stderr, "Attempting to join multicast group for invalid address family!\n");
            rc = SOCKET_ERROR;
        }
        if (NO_ERROR == rc) {
            // Join the group
            rc = setsockopt(pUDP->udpSock, optlevel, option, optval, optlen);
            if (rc != SOCKET_ERROR) {
                /* Set the outgoing interface for multicast packets */
                struct in_addr localInterface;
                rc = SOCKET_ERROR;
                localInterface.s_addr = (localIP[3] << 24) |
                    (localIP[2] << 16) |
                    (localIP[1] << 8) |
                    localIP[0];
                rc = setsockopt(pUDP->udpSock, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface));
            }
        }
        if (SOCKET_ERROR == rc) {
            int lastError = WSAGetLastError();
            if (lastError == 10022) {
                /* Already joined, treat as success */
                rc = NO_ERROR;
            }
            else {
                PRINT("JoinMulticastGroup: setsockopt failed with error code %d\n", WSAGetLastError());
            }
        }
        success = (NO_ERROR == rc);
    }
    return success;
}

bool WinUdpHandler_GetLocalIPs(WinUdpHandler_CB_Local_IP cb)
{
    ULONG WORKING_BUFFER_SIZE = 15000;
    DWORD dwRetVal = ERROR_INVALID_HANDLE;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST;
    ULONG family = AF_UNSPEC; // Both IPv4 and IPv6
    ULONG outBufLen = WORKING_BUFFER_SIZE;
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;

    if (!cb) {
        return false;
    }

    pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(outBufLen);
    if (pAddresses == NULL) {
        return false;
    }

    dwRetVal = GetAdaptersAddresses(family, flags, NULL, pAddresses, &outBufLen);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(outBufLen);
        if (pAddresses == NULL) {
            return false;
        }
        dwRetVal = GetAdaptersAddresses(family, flags, NULL, pAddresses, &outBufLen);
    }

    if (dwRetVal != NO_ERROR) {
        free(pAddresses);
        return false;
    }

    pCurrAddresses = pAddresses;
    while (pCurrAddresses) {
        if (pCurrAddresses->OperStatus != IfOperStatusUp) {
            pCurrAddresses = pCurrAddresses->Next;
            continue;
        }

        // Description (wchar_t* to UTF-8)
        char description[512] = { 0 };
        char friendlyName[512] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->Description, -1, description, sizeof(description), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, pCurrAddresses->FriendlyName, -1, friendlyName, sizeof(friendlyName), NULL, NULL);

        // MAC address
        uint8_t mac[6] = { 0 };
        if (pCurrAddresses->PhysicalAddressLength == 6) {
            memcpy(mac, pCurrAddresses->PhysicalAddress, 6);
        }

        // Link speed (TransmitLinkSpeed is in bits per second)
        uint64_t linkSpeed = pCurrAddresses->TransmitLinkSpeed;

        // For each unicast address
        pUnicast = pCurrAddresses->FirstUnicastAddress;
        while (pUnicast) {
            if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                // IPv4
                struct sockaddr_in *sa_in = (struct sockaddr_in *)pUnicast->Address.lpSockaddr;
                uint8_t ipv4[4];
                memcpy(ipv4, &sa_in->sin_addr, 4);
                uint8_t prefixLength = pUnicast->OnLinkPrefixLength;

                // Find first IPv6 address for this adapter (optional: you can call for each IPv6)
                uint8_t ipv6[16] = { 0 };
                uint8_t ipv6PrefixLength = 0;
                PIP_ADAPTER_UNICAST_ADDRESS pUnicast6 = pCurrAddresses->FirstUnicastAddress;
                while (pUnicast6) {
                    if (pUnicast6->Address.lpSockaddr->sa_family == AF_INET6) {
                        struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)pUnicast6->Address.lpSockaddr;
                        memcpy(ipv6, &sa_in6->sin6_addr, 16);
                        ipv6PrefixLength = pUnicast6->OnLinkPrefixLength;
                        //break; // Only first IPv6
                    }
                    pUnicast6 = pUnicast6->Next;
                }

                if ((ipv4[0] != 169) && (ipv4[0] != 127)) {
                    cb(description, friendlyName, ipv4, prefixLength, ipv6, ipv6PrefixLength, mac, linkSpeed);
                }
            }
            pUnicast = pUnicast->Next;
        }
        pCurrAddresses = pCurrAddresses->Next;
    }

    free(pAddresses);
    return true;
}

bool WinUdpHandler_ConvertStringToIP(const char *ipAddr, uint8_t pIP[4])
{
    bool success = false;
    if (ipAddr && pIP) {
        unsigned long convetedAddr = inet_addr(ipAddr);
        if (convetedAddr > 0) {
            pIP[0] = (uint8_t)convetedAddr;
            pIP[1] = (uint8_t)(convetedAddr >> 8);
            pIP[2] = (uint8_t)(convetedAddr >> 16);
            pIP[3] = (uint8_t)(convetedAddr >> 24);
            success = true;
        }
    }
    return success;
}
#else  /* WIN32 */
void *EmptyDummy = 0; /* Avoid Compiler warning */
#endif /* WIN32 */
