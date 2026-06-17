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

#ifndef RTP4175_H
#define RTP4175_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>

using namespace std;

#ifdef WIN32
#include <basetsd.h>
typedef UINT_PTR SOCKET;
#define INVAL_SOCK  ((SOCKET)-1)
#else
#define INVAL_SOCK  (-1)
#endif

namespace microchip {
    namespace rtp {

        /**
         * @class RTP4175
         * @brief Implements the RTP protocol as defined in RFC 4175 for video transmission.
         *
         * This class provides functionalities to send video frames over RTP using the
         * specifications outlined in RFC 4175. It handles the construction of RTP and
         * RFC 4175 headers and manages the transmission of data over UDP.
         */
        class RTP4175
        {
        public:
            /**
             * @brief Constructs an RTP4175 object with a specified RTP payload type.
             * @param rtpType The RTP payload type to be used. Default is 96.
             */
            explicit RTP4175(uint8_t rtpType = 96);

            /**
             * @brief Destructor for the RTP4175 class.
             */
            virtual ~RTP4175();

            /**
             * @brief Sends a video frame over RTP.
             * @param pFrame Pointer to the frame data.
             * @param frameLength Length of the frame data.
             * @param resX Horizontal resolution of the video frame.
             * @param resY Vertical resolution of the video frame.
             * @param remoteAddr IP address of the remote host.
             * @param remotePort Port number of the remote host.
             * @return True if the frame was sent successfully, false otherwise.
             */
            virtual bool send(const uint8_t* pFrame, uint32_t frameLength, uint16_t resX, uint16_t resY, const uint8_t remoteAddr[4], uint16_t remotePort);

        protected:
            /**
             * @brief Adds an RTP header to the data.
             * @param data The data vector to which the RTP header will be added.
             * @param marker Marker bit for the RTP header.
             * @param rtpType RTP payload type.
             * @param timestamp Timestamp for the RTP packet.
             */
            void addRtpHeader(vector<uint8_t>& data, bool marker, uint8_t rtpType, int64_t timestamp);

            /**
             * @brief Modifies an existing RTP header in the data.
             * @param data The data vector containing the RTP header to be modified.
             * @param marker Marker bit for the RTP header.
             * @param rtpType RTP payload type.
             */
            void modifyRtpHeader(vector<uint8_t>& data, bool marker, uint8_t rtpType);

            /**
             * @brief Adds an RFC 4175 header to the data.
             * @param data The data vector to which the RFC 4175 header will be added.
             * @param length Length of the payload.
             * @param lineNr Line number in the video frame.
             * @param offset Offset within the line.
             * @param cont Continuation flag for the header.
             */
            void add4175Header(vector<uint8_t>& data, uint16_t length, uint16_t lineNr, uint16_t offset, bool cont);

            /**
             * @brief Sends data over UDP to a specified remote address and port.
             * @param data The data vector to be sent.
             * @param remoteAddr IP address of the remote host.
             * @param remotePort Port number of the remote host.
             * @return True if the data was sent successfully, false otherwise.
             */
            bool sendUdp(vector<uint8_t>& data, const uint8_t remoteAddr[4], uint16_t remotePort);

        protected:
#ifdef WIN32
            SOCKET m_udpSock = INVAL_SOCK; ///< UDP socket for sending data on Windows.
#else
            int m_udpSock = INVAL_SOCK; ///< UDP socket for sending data on non-Windows platforms.
#endif
            int32_t m_seqNr = 0; ///< Sequence number for RTP packets.
            int32_t m_ssrc = 0; ///< Synchronization source identifier for RTP packets.
            uint8_t m_rtpType; ///< RTP payload type.
        };

    } // rtp
} // microchip

#endif /* RTP4175_H */
