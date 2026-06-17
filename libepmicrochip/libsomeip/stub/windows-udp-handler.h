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

#ifndef WINDOWS_UDP_HANDLER_H
#define WINDOWS_UDP_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus   // Provide C++ Compatibility
extern "C" {
#endif

    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
    /*                            DEFINITIONS                               */
    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

    /**
     * @brief Opaque UDP socket structure.
     */
    struct udp_t;
    typedef struct udp_t udp_t;

    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
    /*                           CALLBACK API                               */
    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

    /**
     * @brief Callback type for receiving UDP packets.
     *
     * @param pUdp         Pointer to the UDP socket.
     * @param remoteIp     Remote IPv4 address (4 bytes).
     * @param remotePort   Remote UDP port.
     * @param buf          Pointer to the received data buffer.
     * @param len          Length of the received data.
     */
    typedef void (*WinUdpHandler_CB_RX)(udp_t *pUdp, const uint8_t remoteIp[4], uint16_t remotePort, const uint8_t *buf, uint16_t len);

    /**
     * @brief Callback type for reporting local interface information.
     *
     * @param adapterName      Adapter name (ANSI string).
     * @param friendlyName     Friendly name (ANSI string).
     * @param localIPv4        Local IPv4 address (4 bytes).
     * @param subnetLength     Subnet prefix length for IPv4.
     * @param localIPv6        Local IPv6 address (16 bytes).
     * @param ipv6PrefixLength Subnet prefix length for IPv6.
     * @param macAddress       MAC address (6 bytes).
     * @param linkSpeed        Link speed in bits per second.
     */
    typedef void (*WinUdpHandler_CB_Local_IP)(
        const char *adapterName,
        const char *friendlyName,
        const uint8_t localIPv4[4],
        uint8_t subnetLength,
        const uint8_t localIPv6[16],
        uint8_t ipv6PrefixLength,
        const uint8_t macAddress[6],
        uint64_t linkSpeed
        );

    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
    /*                            Public API                                */
    /*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

    /**
     * @brief Creates a UDP socket and binds it to the specified port.
     *
     * @param bindPort Pointer to the port to bind. If *bindPort is 0, an ephemeral port is chosen and returned.
     * @param callback Callback function to be called on packet reception.
     * @return Pointer to the created UDP socket, or NULL on failure.
     */
    udp_t *WinUdpHandler_CreateSocket(uint16_t *bindPort, WinUdpHandler_CB_RX callback);

    /**
     * @brief Destroys a previously created UDP socket.
     *
     * @param pUdp Pointer to the UDP socket to destroy.
     */
    void WinUdpHandler_DestroySocket(udp_t *pUdp);

    /**
     * @brief Sends a UDP packet to the specified IPv4 address and port.
     *
     * @param pUdp     Pointer to the UDP socket.
     * @param ip       Destination IPv4 address (4 bytes).
     * @param destPort Destination UDP port.
     * @param buf      Pointer to the data buffer to send.
     * @param len      Length of the data to send.
     * @return true if the packet was sent successfully, false otherwise.
     */
    bool WinUdpHandler_Send(udp_t *pUdp, const uint8_t ip[4], uint16_t destPort, const uint8_t *buf, uint16_t len);

    /**
     * @brief Joins a multicast group on the specified interface and port.
     *
     * @param pUDP     Pointer to the UDP socket.
     * @param mcastIP  Multicast IPv4 address (4 bytes).
     * @param localIP  Local IPv4 address (4 bytes) of the interface to join.
     * @param port     UDP port to join.
     * @return true if the operation was successful, false otherwise.
     */
    bool WinUdpHandler_JoinMulticastGroup(udp_t *pUDP, const uint8_t mcastIP[4], const uint8_t localIP[4], uint16_t port);

    /**
     * @brief Enumerates local network interfaces and invokes the callback for each.
     *
     * @param cb Callback function to be called for each local interface.
     * @return true if enumeration was successful, false otherwise.
     */
    bool WinUdpHandler_GetLocalIPs(WinUdpHandler_CB_Local_IP cb);

    /**
     * @brief Converts a string representation of an IPv4 address to a 4-byte array.
     *
     * @param ipAddr String containing the IPv4 address (e.g., "192.168.1.1").
     * @param pIP    Output array to receive the 4-byte IPv4 address.
     * @return true if the conversion was successful, false otherwise.
     */
    bool WinUdpHandler_ConvertStringToIP(const char *ipAddr, uint8_t pIP[4]);

#ifdef __cplusplus   // Provide C++ Compatibility
}
#endif

#endif /* WINDOWS_UDP_HANDLER_H */
#endif /* WIN32 */
