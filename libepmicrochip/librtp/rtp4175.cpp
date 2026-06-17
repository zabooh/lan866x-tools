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

#define MAX_RTP_PAYLOAD_LENGTH  (1460)

#define ASSERT(x) assert(x)

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <ctime>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "rtp4175.hpp"

using namespace microchip::rtp;

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                          PROTOCOL DEFINITIONS                        */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define MASK(width) (((int16_t)1u << (width)) - 1u)

#define FLD(pos, width) ((int16_t)(((width) << 8u) | (pos)))
#define FLD_POS(f)      ((f)&255u)
#define FLD_MASK(f)     MASK(((f) >> (8u & 255u)))

#define GET_VAL(_field, _v16) (((int16_t)(_v16) >> (FLD_POS(_field))) & (FLD_MASK(_field)))
#define MK_MASK(_field, _v16) (((int16_t)(_v16) & (FLD_MASK(_field))) << (FLD_POS(_field)))

/* RTP Header */
#define RTP_HLEN 12

#define RTP_HDR_1_VER          FLD(14u, 2) /* Version */
#define RTP_HDR_1_PAD          FLD(13u, 1) /* Padding */
#define RTP_HDR_1_EXT          FLD(12u, 1) /* Extension */
#define RTP_HDR_1_CC           FLD(8u, 4)  /* Contributing source identifiers count */
#define RTP_HDR_1_MARK         FLD(7u, 1)  /* Marker */
#define RTP_HDR_1_PAYLOAD_TYPE FLD(0u, 7)  /* Payload Type */

/* RFC 4175 Header */
#define RFC4175_OUTER_HLEN  2
#define RFC4175_INNNER_HLEN 6

#define RFC4175_FIELD_ID     FLD(15u, 1) /* Field Identification */
#define RFC4175_LINE_NR      FLD(0u, 15) /* Line number */
#define RFC4175_CONTINUATION FLD(15u, 1) /* Continuation */
#define RFC4175_OFFSET       FLD(0u, 15) /* Offset in Line */

// Function to get the current time in microseconds
int64_t getCurrentTimeMicroseconds() {
#ifdef WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000) / frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (ts.tv_nsec / 1000);
#endif
}


RTP4175::RTP4175(uint8_t rtpType)
    : m_rtpType(rtpType)
{
    srand(static_cast<unsigned int>(std::time(nullptr)));
    m_ssrc = rand();
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

RTP4175::~RTP4175() {
    if (m_udpSock != INVAL_SOCK) {
#ifdef _WIN32
        closesocket(m_udpSock);
#else
        close(m_udpSock);
#endif
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RTP4175::send(const uint8_t* pFrame, uint32_t frameLength, uint16_t resX, uint16_t resY, const uint8_t remoteAddr[4], uint16_t remotePort)
{
    vector<uint8_t> header;
    vector<uint8_t> imageData;
    int64_t timestamp;
    uint32_t pixelPos = 0;
    if (!pFrame || !frameLength || !remoteAddr || !remotePort) {
        return false;
    }
    ASSERT(frameLength < MAX_RTP_PAYLOAD_LENGTH);

    timestamp = getCurrentTimeMicroseconds() / 10; // Convert to 10 microsecond resolution
    addRtpHeader(header, false, m_rtpType, timestamp);
    for (uint32_t y = 0; y < resY; y++) {
        bool last = (y + 1) == resY;
        bool cont = !last && (header.size() + imageData.size() + (2 * resX * 3) + 6) <= MAX_RTP_PAYLOAD_LENGTH;
        add4175Header(header, resX * 3, y, 0, cont); /* 24 Bit RGB */
        for (uint32_t x = 0; x < resX; x++) {
            imageData.push_back(pFrame[pixelPos++]);
            imageData.push_back(pFrame[pixelPos++]);
            imageData.push_back(pFrame[pixelPos++]);
        }
        if (!last && !cont) {
            /* Next line will not fit into RTP frame, so send now */
            header.insert(header.end(), imageData.begin(), imageData.end());
            sendUdp(header, remoteAddr, remotePort);

            /* Start with a new RTP frame */
            header.clear();
            imageData.clear();
            addRtpHeader(header, false, m_rtpType, timestamp);
        }
    }
    modifyRtpHeader(header, true, m_rtpType);
    header.insert(header.end(), imageData.begin(), imageData.end());
    sendUdp(header, remoteAddr, remotePort);
    return true;
}

void RTP4175::addRtpHeader(vector<uint8_t>& data, bool marker, uint8_t rtpType, int64_t timestamp)
{
    uint16_t hdr1 = 0;
    hdr1 |= MK_MASK(RTP_HDR_1_VER, 2);                  /* RFC 1889 Version 2 */
    hdr1 |= MK_MASK(RTP_HDR_1_MARK, marker);
    hdr1 |= MK_MASK(RTP_HDR_1_PAYLOAD_TYPE, rtpType); /* RFC 4175 */

    data.push_back(static_cast<uint8_t>(hdr1 >> 8));
    data.push_back(static_cast<uint8_t>(hdr1));

    data.push_back(static_cast<uint8_t>(m_seqNr >> 8));
    data.push_back(static_cast<uint8_t>(m_seqNr));

    data.push_back(static_cast<uint8_t>(timestamp >> 24));
    data.push_back(static_cast<uint8_t>(timestamp >> 16));
    data.push_back(static_cast<uint8_t>(timestamp >> 8));
    data.push_back(static_cast<uint8_t>(timestamp));

    data.push_back(static_cast<uint8_t>(m_ssrc >> 24));
    data.push_back(static_cast<uint8_t>(m_ssrc >> 16));
    data.push_back(static_cast<uint8_t>(m_ssrc >> 8));
    data.push_back(static_cast<uint8_t>(m_ssrc));

    /* This is already part of RFC4175, once per RTP header,
     * the upper 16 Bit of seq number are transmitted in RTP payload */
    data.push_back(static_cast<uint8_t>(m_seqNr >> 24));
    data.push_back(static_cast<uint8_t>(m_seqNr >> 16));

    m_seqNr++;
}

void RTP4175::modifyRtpHeader(vector<uint8_t>& data, bool marker, uint8_t rtpType)
{
    ASSERT(data.size() >= 2);
    uint16_t hdr1 = 0;
    hdr1 |= MK_MASK(RTP_HDR_1_VER, 2);                  /* RFC 1889 Version 2 */
    hdr1 |= MK_MASK(RTP_HDR_1_MARK, marker);
    hdr1 |= MK_MASK(RTP_HDR_1_PAYLOAD_TYPE, rtpType);        /* RFC 4175 */
    data[0] = static_cast<uint8_t>(hdr1 >> 8);
    data[1] = static_cast<uint8_t>(hdr1);
}

void RTP4175::add4175Header(vector<uint8_t>& data, uint16_t length, uint16_t lineNr, uint16_t offset, bool cont)
{
    lineNr &= 0x7FFF; /* 15 Bit */
    offset &= 0x7FFF; /* 15 Bit */
    if (cont) {
        offset |= 0x8000;
    }

    data.push_back(static_cast<uint8_t>(length >> 8));
    data.push_back(static_cast<uint8_t>(length));

    data.push_back(static_cast<uint8_t>(lineNr >> 8));
    data.push_back(static_cast<uint8_t>(lineNr));

    data.push_back(static_cast<uint8_t>(offset >> 8));
    data.push_back(static_cast<uint8_t>(offset));
}

bool RTP4175::sendUdp(vector<uint8_t>& data, const uint8_t remoteAddr[4], uint16_t remotePort)
{
    bool success = false;
    if (m_udpSock == INVAL_SOCK) {
        m_udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    if (m_udpSock != INVAL_SOCK) {
        struct sockaddr_in destination;
        int bytesSent = 0;
        uint8_t* pBuf = &data[0];
        memset(&destination, 0, sizeof(destination));
        destination.sin_family = AF_INET;
        destination.sin_addr.s_addr = (remoteAddr[3] << 24) |
            (remoteAddr[2] << 16) |
            (remoteAddr[1] << 8) |
            remoteAddr[0];
        destination.sin_port = htons(remotePort);
        bytesSent = sendto(m_udpSock, reinterpret_cast<const char*>(pBuf), (int)data.size(), 0, (struct sockaddr*)&destination, sizeof(destination));
        success = (bytesSent == (int)data.size());
    }
    return success;
}
