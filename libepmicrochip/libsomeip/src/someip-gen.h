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
/* SOME/IP Generator for embedded systems                                                         */
/*------------------------------------------------------------------------------------------------*/

#ifndef SOMEIP_GEN_H
#define SOMEIP_GEN_H

#include "someip-common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                             INTERNAL API                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

uint16_t Fill_SomeIP_Frame(uint8_t **ppBuf, void **pMemTag, struct SOMEIP_SD_Frame *someIpFrame);

uint16_t Fill_SomeIP_Header(uint8_t *pBuf, const struct SOMEIP_Header *pParam);

uint16_t Fill_SomeIP_SD_Header(uint8_t *pBuf, const struct SOMEIP_SD_Header *par);

uint16_t Fill_SomeIP_SD_Service_Header(uint8_t *pBuf, const struct SOMEIP_SD_Service *par);

uint16_t Fill_SomeIP_SD_Event_Header(uint8_t *pBuf, const struct SOMEIP_SD_Event *par);

uint16_t Fill_SomeIP_SD_LengthOfOptions_Header(uint8_t *pBuf, uint32_t optionLength);

uint16_t Fill_SomeIP_SD_OptionConfiguration_Header(uint8_t *pBuf, uint16_t bufLength, const struct SOMEIP_OptConfig *pConfig);

uint16_t Fill_SomeIP_SD_OptionIpV4_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4 *par);

uint16_t Fill_SomeIP_SD_OptionIpV4Multicast_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4Mcast *par);

uint16_t Fill_SomeIP_SD_OptionIpV4SD_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4SD *par);

#ifdef __cplusplus
}
#endif

#endif /* SOMEIP_GEN_H */