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

/*------------------------------------------------------------------------------------------------*/
/* SOME/IP Parser for embedded systems                                                            */
/*------------------------------------------------------------------------------------------------*/

#ifndef SOMEIP_PARS_H
#define SOMEIP_PARS_H

#include "someip-common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                             INTERNAL API                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

enum SOMEIP_ReturnCode Parse_SomeIP_Frame(const uint8_t *b, uint16_t bLen, struct SOMEIP_SD_Frame *h);

enum SOMEIP_ReturnCode Parse_SomeIP_Header(const uint8_t *pBuf, struct SOMEIP_Header *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Header(const uint8_t *pBuf, struct SOMEIP_SD_Header *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Service_Header(const uint8_t *pBuf, struct SOMEIP_SD_Service *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Event_Header(const uint8_t *pBuf, struct SOMEIP_SD_Event *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_LengthOfOptions_Header(const uint8_t *pBuf, uint32_t *pLen);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionConfiguration_Header(const uint8_t *pBuf, uint16_t bufLength, struct SOMEIP_OptConfig *pConfig, uint16_t *pConsumedBytes);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4_Header(const uint8_t *pBuf, struct SOMEIP_SD_OptIpV4 *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4Multicast_Header(const uint8_t *pBuf, struct SOMEIP_SD_OptIpV4Mcast *pParam);

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4SD_Header(const uint8_t *pBuf, struct SOMEIP_SD_OptIpV4SD *pParam);

#ifdef __cplusplus
}
#endif

#endif /* SOMEIP_PARS_H */