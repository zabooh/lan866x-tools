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

#include <stddef.h>
#include <assert.h>
#include <string.h> /* For memset */
#include "someip-cfg.h"
#include "someip-pars.h"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                         PUBLIC FUNCTIONS                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

enum SOMEIP_ReturnCode SOMEIP_Parser_Read_Header(const uint8_t *pBuf, uint16_t bufLen, struct SOMEIP_Header *pParam, uint16_t *pConsumed)
{
    const uint16_t consume = 16u;
    enum SOMEIP_ReturnCode result = SOMEIP_E_NOT_OK;
    if ((NULL != pBuf) && (NULL != pParam) && (bufLen >= consume)) {
        result = Parse_SomeIP_Header(pBuf, pParam);
        if ((SOMEIP_E_OK == result) && (NULL != pConsumed)) {
            *pConsumed = (*pConsumed) + consume;
        }
    }
    return result;
}

bool SOMEIP_Parser_Read_Tag(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint16_t *pLength, uint16_t *pConsumed)
{
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (bufLen >= 2u)) {
        uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
        uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
        uint16_t length = 0u;
        uint16_t consume = 0u;
        if (WIRETYPE_COMPLEX_2_BYTE == wireType) {
            consume = 4u;
            length = (pBuf[2] << 8) | pBuf[3];
            success = true;
        }
        else if (WIRETYPE_COMPLEX_STATIC_SIZE == wireType) {
            consume = 2u;
            success = true;
        } else { /* MISRA enforced if-else termination */ }
        if (success) {
            *pTagDataId = tagDataId;
            if (NULL != pLength) {
                *pLength = length;
            }
            if (NULL != pConsumed) {
                *pConsumed = (*pConsumed) + consume;
            }
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_UINT8(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint8_t *pValue, uint16_t *pConsumed)
{
    const uint16_t consume = 3u;
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (NULL != pValue)) {
        if ((bufLen >= consume)) {
            uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (WIRETYPE_8_BIT == wireType)) {
                *pTagDataId = tagDataId;
                *pValue = pBuf[2];
                if (NULL != pConsumed) {
                    *pConsumed = (*pConsumed) + consume;
                }
                success = true;
            }
        } else {
            /* Be backward compatible. Return all bits set to 1 to mark invalid. */
            *pValue = 0xFF;
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_UINT16(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint16_t *pValue, uint16_t *pConsumed)
{
    const uint16_t consume = 4u;
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (NULL != pValue) && (NULL != pValue)) {
        if ((bufLen >= consume)) {
            uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (WIRETYPE_16_BIT == wireType)) {
                *pTagDataId = tagDataId;
                *pValue = ((uint16_t)pBuf[2] << 8) | (uint16_t)pBuf[3];
                if (NULL != pConsumed) {
                    *pConsumed = (*pConsumed) + consume;
                }
                success = true;
            }
        } else {
            /* Be backward compatible. Return all bits set to 1 to mark invalid. */
            *pValue = 0xFFFF;
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_UINT32(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint32_t *pValue, uint16_t *pConsumed)
{
    const uint16_t consume = 6u;
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (NULL != pValue) && (NULL != pValue)) {
        if ((bufLen >= consume)) {
            uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (WIRETYPE_32_BIT == wireType)) {
                *pTagDataId = tagDataId;
                *pValue = ((uint32_t)pBuf[2] << 24) | ((uint32_t)pBuf[3] << 16) | ((uint32_t)pBuf[4] << 8) | (uint32_t)pBuf[5];
                if (NULL != pConsumed) {
                    *pConsumed = (*pConsumed) + consume;
                }
                success = true;
            }
        } else {
            /* Be backward compatible. Return all bits set to 1 to mark invalid. */
            *pValue = 0xFFFFFFFF;
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_UINT64(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint64_t *pValue, uint16_t *pConsumed)
{
    const uint16_t consume = 10u;
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (NULL != pValue)&& (NULL != pValue)) {
        if ((bufLen >= consume)) {
            uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (WIRETYPE_64_BIT == wireType)) {
                *pTagDataId = tagDataId;
                *pValue =   ((uint64_t)pBuf[2] << 56) |
                            ((uint64_t)pBuf[3] << 48) |
                            ((uint64_t)pBuf[4] << 40) |
                            ((uint64_t)pBuf[5] << 32) |
                            ((uint64_t)pBuf[6] << 24) |
                            ((uint64_t)pBuf[7] << 16) |
                            ((uint64_t)pBuf[8] <<  8) |
                            ((uint64_t)pBuf[9] <<  0) ;
                if (NULL != pConsumed) {
                    *pConsumed = (*pConsumed) + consume;
                }
                success = true;
            }
        } else {
            /* Be backward compatible. Return all bits set to 1 to mark invalid. */
            *pValue = 0xFFFFFFFFFFFFFFFF;
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_Length_BLOB(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pBlobLen)
{
    bool success = false;
    if ((NULL != pBuf) && (NULL != pBlobLen)) {
        if (bufLen >= 5u) {
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            if (WIRETYPE_COMPLEX_2_BYTE == wireType) {
                *pBlobLen = (pBuf[2] << 8) | pBuf[3];
                success = true;
            }
        } else {
            /* Be backward compatible. Report empty BLOB. */
            * pBlobLen = 0;
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Parser_Read_BLOB(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint8_t *pBlob, uint16_t *pBlobLen, uint16_t *pConsumed)
{
    bool success = false;
    if ((NULL != pBuf) && (NULL != pTagDataId) && (NULL != pBlob) && (NULL != pBlobLen)) {
        if (bufLen >= 4u) {
            uint16_t tagDataId = ((pBuf[0] & 0xFu) << 8u) | pBuf[1];
            uint8_t wireType = ((pBuf[0] >> 4) & 0x7u);
            uint16_t blobLen;
            uint16_t consume;
            if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (WIRETYPE_COMPLEX_2_BYTE == wireType)) {
                *pTagDataId = tagDataId;
                blobLen = (pBuf[2] << 8) | pBuf[3];
                consume = blobLen + 4u;
                uint16_t newBlobLen = 0;
                if ((0xEFu == pBuf[4]) && (0xBBu == pBuf[5]) && (0xBFu == pBuf[6])) {
                    /* UTF8 String */
                    newBlobLen = blobLen - 3;
                    if (newBlobLen > *pBlobLen) {
                        newBlobLen = *pBlobLen;
                    }
                    (void)memcpy(pBlob, &pBuf[7], newBlobLen);
                } else {
                    newBlobLen = blobLen;
                    if (newBlobLen > *pBlobLen) {
                        newBlobLen = *pBlobLen;
                    }
                    (void)memcpy(pBlob, &pBuf[4], newBlobLen);
                }
                *pBlobLen = newBlobLen;

                if (NULL != pConsumed) {
                    *pConsumed = (*pConsumed) + consume;
                }
                success = true;
            } else {
                SOMEIP_ASSERT(false, __FILE__, __LINE__);
            }
        } else {
            /* Be backward compatible. Report empty BLOB. */
            * pBlobLen = 0;
            success = true;
        }
    }
    return success;
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                             INTERNAL API                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

enum SOMEIP_ReturnCode Parse_SomeIP_Frame(const uint8_t *b, uint16_t bLen, struct SOMEIP_SD_Frame *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        uint32_t entriesLength = 0u;
        uint32_t optionsLength = 0u;
        uint32_t i;
        uint32_t j;
        uint16_t currentPos = 0u;
        (void)memset(h, 0, sizeof(struct SOMEIP_SD_Frame));
        if (bLen >= 12u) {
            success = Parse_SomeIP_Header(&b[currentPos], &h->someIp);
        } else {
            success = SOMEIP_E_MALFORMED_MESSAGE;
        }
        if (SOMEIP_E_OK == success) {
            currentPos += SOMEIP_HEADER_LENGTH;
            if (currentPos < bLen) {
                success = Parse_SomeIP_SD_Header(&b[currentPos], &h->someIpSd);
            } else {
                success = SOMEIP_E_MALFORMED_MESSAGE;
            }
        }
        if (SOMEIP_E_OK == success) {
            currentPos += SOMEIP_SD_LENGTH;
            if ((currentPos < bLen) && (0u == (h->someIpSd.length % SOMEIP_SD_SERVICE_LENGTH))) {
                entriesLength = (h->someIpSd.length / SOMEIP_SD_SERVICE_LENGTH);
                SOMEIP_ASSERT((0u != entriesLength), __FILE__, __LINE__);
                SOMEIP_ASSERT((entriesLength <= MAX_SERVICE_EVNT_FIELDS), __FILE__, __LINE__);
            } else {
                success = SOMEIP_E_MALFORMED_MESSAGE;
            }
        }
        for (i = 0u; (SOMEIP_E_OK == success) && (i < entriesLength) && (i < MAX_SERVICE_EVNT_FIELDS); i++) {
            if (currentPos < bLen) {
                switch(b[currentPos]) {
                    case SDServiceType_FindService:
                    case SDServiceType_OfferService:
                        success = Parse_SomeIP_SD_Service_Header(&b[currentPos], &h->services[i].service);
                        if (SOMEIP_E_OK == success) {
                            currentPos += SOMEIP_SD_SERVICE_LENGTH;
                            h->servicesSel[i] = SOMEIP_SD_UnionService;
                        }
                        break;

                    case SDEventType_SubscribeEventGroup:
                    case SDEventType_SubscribeEventgroupAck:
                        success = Parse_SomeIP_SD_Event_Header(&b[currentPos], &h->services[i].event);
                        if (SOMEIP_E_OK == success) {
                            currentPos += SOMEIP_SD_EVTGRP_LENGTH;
                            h->servicesSel[i] = SOMEIP_SD_UnionEvent;
                        }
                        break;

                    default:
                        success = SOMEIP_E_WRONG_MESSAGE_TYPE;
                        break;
                }
            } else {
                success = SOMEIP_E_MALFORMED_MESSAGE;
            }
        }
        if (SOMEIP_E_OK == success) {
            success = Parse_SomeIP_SD_LengthOfOptions_Header(&b[currentPos], &optionsLength);
        }
        if (SOMEIP_E_OK == success) {
            currentPos += SOMEIP_SD_OPTLEN_LENGTH;
            if (currentPos > bLen) {
                success = SOMEIP_E_MALFORMED_MESSAGE;
            }
        }
        j = 0;
        for (i = 0u; (SOMEIP_E_OK == success) && (j < optionsLength) && (i < MAX_OPTION_FIELDS); i++) {
            if (currentPos < bLen) {
                switch(b[currentPos + SOMEIP_SD_OPT_IPV4_POS_TYPE]) {
                    case SOMEIP_SD_OPT_IPV4_TYPE_VALUE:
                        success = Parse_SomeIP_SD_OptionIpV4_Header(&b[currentPos], &h->options[i].ipV4);
                        if (SOMEIP_E_OK == success) {
                            currentPos += SOMEIP_SD_OPT_IPV4_OUTER_LENGTH;
                            j += SOMEIP_SD_OPT_IPV4_OUTER_LENGTH;
                            h->optionsSel[i] = SOMEIP_SD_UnionOptIpV4;
                        }
                        break;

                    case SOMEIP_SD_OPT_IPV4MCAST_TYPE_VALUE:
                        success = Parse_SomeIP_SD_OptionIpV4Multicast_Header(&b[currentPos], &h->options[i].ipV4MCast);
                        if (SOMEIP_E_OK == success) {
                            currentPos += SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH;
                            j += SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH;
                            h->optionsSel[i] = SOMEIP_SD_UnionOptIpV4MCast;
                        }
                        break;


                    case SOMEIP_SD_OPT_IPV4_SD_TYPE_VALUE:
                        success = Parse_SomeIP_SD_OptionIpV4SD_Header(&b[currentPos], &h->options[i].ipV4Sd);
                        if (SOMEIP_E_OK == success) {
                            currentPos += SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH;
                            j += SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH;
                            h->optionsSel[i] = SOMEIP_SD_UnionOptIpV4SD;
                        }
                        break;


                    case SOMEIP_SD_OPT_CONFIG_TYPE_VALUE:
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                    {
                        uint16_t totLen = 0u;
                        success = Parse_SomeIP_SD_OptionConfiguration_Header(&b[currentPos], (bLen - currentPos), &h->options[i].config, &totLen);
                        if (SOMEIP_E_OK == success) {
                            currentPos += totLen;
                            j += totLen;
                            h->optionsSel[i] = SOMEIP_SD_UnionOptConfig;
                        }
                    }
#endif
                        break;

                    default:
                        success = SOMEIP_E_WRONG_MESSAGE_TYPE;
                        break;
                }
            } else {
                success = SOMEIP_E_MALFORMED_MESSAGE;
            }
        }
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_Header(const uint8_t *b, struct SOMEIP_Header *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        (void)memset(h, 0, sizeof(struct SOMEIP_Header));
        if (SOMEIP_VERSION == b[SOMEIP_POS_PROT_VER]) {
            uint32_t msgId;
            uint32_t reqId;

            msgId = ( (b[SOMEIP_POS_MSG_ID_0] << 24)
                    | (b[SOMEIP_POS_MSG_ID_1] << 16)
                    | (b[SOMEIP_POS_MSG_ID_2] << 8)
                    |  b[SOMEIP_POS_MSG_ID_3]
                    );

            h->serviceId = GET_VAL(SOMEIP_FLD_ID_SERVICE_ID, msgId);
            h->generateEvent = (0u != GET_VAL(SOMEIP_FLD_ID_MTHD_EVNT, msgId));
            h->methodId = GET_VAL(SOMEIP_FLD_ID_METHOD_ID, msgId);

            h->length = ( (b[SOMEIP_POS_LENGTH_0] << 24)
                        | (b[SOMEIP_POS_LENGTH_1] << 16)
                        | (b[SOMEIP_POS_LENGTH_2] << 8)
                        |  b[SOMEIP_POS_LENGTH_3]
                        );

            reqId = ( (b[SOMEIP_POS_REQ_ID_0] << 24)
                    | (b[SOMEIP_POS_REQ_ID_1] << 16)
                    | (b[SOMEIP_POS_REQ_ID_2] << 8)
                    |  b[SOMEIP_POS_REQ_ID_3]
                    );

            h->clientId = GET_VAL(SOMEIP_FLD_REQID_CLIENT_ID, reqId);
            h->sessionId = GET_VAL(SOMEIP_FLD_REQID_SESSION_ID, reqId);

            h->interfaceVersion = b[SOMEIP_POS_INTF_VER];

            h->msgType = b[SOMEIP_POS_MSG_TYPE];
            h->retCode = b[SOMEIP_POS_RET_CODE];

            success = SOMEIP_E_OK;
        } else {
            success = SOMEIP_E_WRONG_PROTOCOL_VERSION;
        }
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Header(const uint8_t *b, struct SOMEIP_SD_Header *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        uint32_t flags = b[SOMEIP_SD_POS_FLAGS];
        (void)memset(h, 0, sizeof(struct SOMEIP_SD_Header));

        h->reboot = (0u != (flags & SOMEIP_SD_FLD_FLAGS_REBOOT));
        h->unicast = (0u != (flags & SOMEIP_SD_FLD_FLAGS_UNICAST));

        h->length = ( (b[SOMEIP_SD_POS_LEN_ENTR_0] << 24)
                    | (b[SOMEIP_SD_POS_LEN_ENTR_1] << 16)
                    | (b[SOMEIP_SD_POS_LEN_ENTR_2] << 8)
                    |  b[SOMEIP_SD_POS_LEN_ENTR_3]
                    );

        success = SOMEIP_E_OK;
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Service_Header(const uint8_t *b, struct SOMEIP_SD_Service *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        uint32_t numOpts = b[SOMEIP_SD_SERVICE_POS_NUM_OPT];
        (void)memset(h, 0, sizeof(struct SOMEIP_SD_Service));

        h->type = b[SOMEIP_SD_SERVICE_POS_TYPE];
        h->index1stOpt = b[SOMEIP_SD_SERVICE_POS_INDEX_OPT1];
        h->index2ndOpt = b[SOMEIP_SD_SERVICE_POS_INDEX_OPT2];
        h->numberOfOpt1 = GET_VAL(SOMEIP_SD_SERVICE_NUM_OPT1, numOpts);
        h->numberOfOpt2 = GET_VAL(SOMEIP_SD_SERVICE_NUM_OPT2, numOpts);

        h->serviceID = ( (b[SOMEIP_SD_SERVICE_POS_SERVICE_ID_0] << 8)
                       |  b[SOMEIP_SD_SERVICE_POS_SERVICE_ID_1]
                       );

        h->instanceID = ( (b[SOMEIP_SD_SERVICE_POS_INST_ID_0] << 8)
                        |  b[SOMEIP_SD_SERVICE_POS_INST_ID_1]
                        );

        h->majorVersion = b[SOMEIP_SD_SERVICE_POS_MAJOR_VER];

        h->ttl = ( (b[SOMEIP_SD_SERVICE_POS_TTL_0] << 16)
                 | (b[SOMEIP_SD_SERVICE_POS_TTL_1] << 8)
                 |  b[SOMEIP_SD_SERVICE_POS_TTL_2]
                 );

        h->minorVersion = ( (b[SOMEIP_SD_SERVICE_POS_MINOR_VER_0] << 24)
                          | (b[SOMEIP_SD_SERVICE_POS_MINOR_VER_1] << 16)
                          | (b[SOMEIP_SD_SERVICE_POS_MINOR_VER_2] << 8)
                          |  b[SOMEIP_SD_SERVICE_POS_MINOR_VER_3]
                          );

        success = SOMEIP_E_OK;
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_Event_Header(const uint8_t *b, struct SOMEIP_SD_Event *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        uint32_t numOpts = b[SOMEIP_SD_EVTGRP_POS_NUM_OPT];
        uint32_t cnt = b[SOMEIP_SD_EVTGRP_POS_COUNTER];
        (void)memset(h, 0, sizeof(struct SOMEIP_SD_Event));

        h->type = b[SOMEIP_SD_EVTGRP_POS_TYPE];
        h->index1stOpt = b[SOMEIP_SD_EVTGRP_POS_INDEX_OPT1];
        h->index2ndOpt = b[SOMEIP_SD_EVTGRP_POS_INDEX_OPT2];
        h->numberOfOpt1 = GET_VAL(SOMEIP_SD_EVTGRP_NUM_OPT1, numOpts);
        h->numberOfOpt2 = GET_VAL(SOMEIP_SD_EVTGRP_NUM_OPT2, numOpts);

        h->serviceID = ( (b[SOMEIP_SD_EVTGRP_POS_SERVICE_ID_0] << 8)
                       |  b[SOMEIP_SD_EVTGRP_POS_SERVICE_ID_1]
                       );

        h->instanceID = ( (b[SOMEIP_SD_EVTGRP_POS_INST_ID_0] << 8)
                        |  b[SOMEIP_SD_EVTGRP_POS_INST_ID_1]
                        );

        h->majorVersion = b[SOMEIP_SD_EVTGRP_POS_MAJOR_VER];

        h->ttl = ( (b[SOMEIP_SD_EVTGRP_POS_TTL_0] << 16)
                 | (b[SOMEIP_SD_EVTGRP_POS_TTL_1] << 8)
                 |  b[SOMEIP_SD_EVTGRP_POS_TTL_2]
                 );

        h->counter = GET_VAL(SOMEIP_SD_EVTGRP_COUNTER, cnt);

        h->eventGroupID = ( (b[SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_0] << 8)
                          |  b[SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_1]
                          );

        success = SOMEIP_E_OK;
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_LengthOfOptions_Header(const uint8_t *b, uint32_t *pLen)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != pLen)) {
        *pLen = ( (b[SOMEIP_SD_OPTLEN_POS_LEN_0] << 24)
                | (b[SOMEIP_SD_OPTLEN_POS_LEN_1] << 16)
                | (b[SOMEIP_SD_OPTLEN_POS_LEN_2] << 8)
                |  b[SOMEIP_SD_OPTLEN_POS_LEN_3]
                );
        success = SOMEIP_E_OK;
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionConfiguration_Header(const uint8_t *b, uint16_t bLen, struct SOMEIP_OptConfig *pConfig, uint16_t *pConsumedBytes)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    if ((NULL != b) && (NULL != pConfig) && (NULL != pConsumedBytes) && (0u != bLen)) {
        bool check;
        uint32_t optTotLen = ( (b[SOMEIP_SD_OPT_CONFIG_POS_LENGTH_0] << 8)
                           |  b[SOMEIP_SD_OPT_CONFIG_POS_LENGTH_1]
                           );

        *pConsumedBytes = 0u;

        check = (optTotLen + SOMEIP_SD_OPT_CONFIG_HDR_LENGTH) <= bLen;
        check = check && ((optTotLen - 1u) <= bLen);
        check = check && (SOMEIP_SD_OPT_CONFIG_TYPE_VALUE == b[SOMEIP_SD_OPT_CONFIG_POS_TYPE]);

        if (check) {
            const uint8_t *s = &b[SOMEIP_SD_OPT_CONFIG_POS_STRING];

            uint16_t pos = 0u;
            uint16_t *pArrayCnt = &pConfig->pairCount;
            pConfig->pairCount = 0u;
            while((SOMEIP_E_MALFORMED_MESSAGE != success) && (pos < bLen) && (pConfig->pairCount < MAX_CONFIG_OPT_ENTRIES)) {

                uint16_t cPos;
                uint16_t equalPos = 0;
                uint8_t cLen = s[pos++];

                for (cPos = 0u; (0u == equalPos) && (cPos < cLen); cPos++) {
                    if ('=' == s[cPos + pos]) {
                        equalPos = cPos;
                    }
                }
                if (0u != equalPos) {
                    uint16_t keyLen = equalPos;
                    uint16_t valLen = (cLen - keyLen - 1);
                    if ((keyLen < MAX_CONFIG_OPT_KEY_LEN) && (valLen < MAX_CONFIG_OPT_VAL_LEN)) {
                        (void)memcpy(pConfig->key[*pArrayCnt], &s[pos], keyLen);
                        pConfig->key[*pArrayCnt][keyLen] = 0x0u; /* String termination */
                        pos += keyLen + 1u; /* +1 because of equal character */

                        (void)memcpy(pConfig->val[*pArrayCnt], &s[pos], valLen);
                        pConfig->val[*pArrayCnt][valLen] = 0x0u; /* String termination */
                        pos += valLen;

                        (*pArrayCnt)++;
                        success = SOMEIP_E_OK;
                        *pConsumedBytes = (optTotLen + 3u); /* 2 Byte length field and 1 Byte type flag */
                    } else {
                        success = SOMEIP_E_MALFORMED_MESSAGE;
                    }
                } else {
                    break;
                }
            }
        } else {
            success = SOMEIP_E_MALFORMED_MESSAGE;
        }
    }
#endif
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4_Header(const uint8_t *b, struct SOMEIP_SD_OptIpV4 *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        bool check;
        uint32_t compare = ( (b[SOMEIP_SD_OPT_IPV4_POS_LENGTH_0] << 8)
                           |  b[SOMEIP_SD_OPT_IPV4_POS_LENGTH_1]
                           );

        (void)memset(h, 0, sizeof(struct SOMEIP_SD_OptIpV4));

        check = (SOMEIP_SD_OPT_IPV4_INNER_LENGTH == compare);
        check = check && (SOMEIP_SD_OPT_IPV4_TYPE_VALUE == b[SOMEIP_SD_OPT_IPV4_POS_TYPE]);
        if (check) {
            switch(b[SOMEIP_SD_OPT_IPV4_POS_IPV4_PROTO]) {
                case SOMEIP_SD_OPT_IPV4_PROT_UDP:
                    h->udp = true;
                    break;
                case SOMEIP_SD_OPT_IPV4_PROT_TCP:
                    h->udp = false;
                    break;
                default:
                    check = false;
                    break;
            }
        }
        if (check) {
            h->ipV4Addr[0] = b[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_0];
            h->ipV4Addr[1] = b[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_1];
            h->ipV4Addr[2] = b[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_2];
            h->ipV4Addr[3] = b[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_3];

            h->portNumber = ( (b[SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_0] << 8)
                            |  b[SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_1]
                            );

        }
        success = (check ? SOMEIP_E_OK : SOMEIP_E_MALFORMED_MESSAGE);
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4Multicast_Header(const uint8_t *b, struct SOMEIP_SD_OptIpV4Mcast *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        bool check;
        uint32_t compare = ( (b[SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_0] << 8)
                           |  b[SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_1]
                           );

        (void)memset(h, 0, sizeof(struct SOMEIP_SD_OptIpV4Mcast));

        check = (SOMEIP_SD_OPT_IPV4MCAST_INNER_LENGTH == compare);
        check = check && (SOMEIP_SD_OPT_IPV4MCAST_TYPE_VALUE == b[SOMEIP_SD_OPT_IPV4MCAST_POS_TYPE]);
        check = check && (SOMEIP_SD_OPT_IPV4MCAST_PROT_UDP == b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PROTO]);

        if (check) {
            h->ipV4Addr[0] = b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_0];
            h->ipV4Addr[1] = b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_1];
            h->ipV4Addr[2] = b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_2];
            h->ipV4Addr[3] = b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_3];

            h->portNumber = ( (b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_0] << 8)
                            |  b[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_1]
                            );

        }
        success = (check ? SOMEIP_E_OK : SOMEIP_E_MALFORMED_MESSAGE);
    }
    return success;
}

enum SOMEIP_ReturnCode Parse_SomeIP_SD_OptionIpV4SD_Header(const uint8_t *b, struct SOMEIP_SD_OptIpV4SD *h)
{
    enum SOMEIP_ReturnCode success = SOMEIP_E_NOT_OK;
    if ((NULL != b) && (NULL != h)) {
        bool check;
        uint32_t compare = ( (b[SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_0] << 8)
                           |  b[SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_1]
                           );

        (void)memset(h, 0, sizeof(struct SOMEIP_SD_OptIpV4SD));

        check = (SOMEIP_SD_OPT_IPV4_INNER_LENGTH == compare);
        check = check && (SOMEIP_SD_OPT_IPV4_SD_TYPE_VALUE == b[SOMEIP_SD_OPT_IPV4_SD_POS_TYPE]);
        if (check) {
            switch(b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PROTO]) {
                case SOMEIP_SD_OPT_IPV4_PROT_UDP:
                    h->udp = true;
                    break;
                case SOMEIP_SD_OPT_IPV4_PROT_TCP:
                    h->udp = false;
                    break;
                default:
                    check = false;
                    break;
            }
        }
        if (check) {
            h->ipV4Addr[0] = b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_0];
            h->ipV4Addr[1] = b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_1];
            h->ipV4Addr[2] = b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_2];
            h->ipV4Addr[3] = b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_3];

            h->portNumber = ( (b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_0] << 8)
                            |  b[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_1]
                            );

        }
        success = (check ? SOMEIP_E_OK : SOMEIP_E_MALFORMED_MESSAGE);
    }
    return success;
}
