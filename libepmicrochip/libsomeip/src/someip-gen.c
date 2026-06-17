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

#include <stddef.h>
#include <string.h>
#include "someip-cfg.h"
#include "someip-common.h"
#include "someip-gen.h"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                      PRIVATE FUNCTION PROTOTYPES                     */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static uint32_t my_strnlen(const char* str, uint32_t max);

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                              PUBLIC API                              */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

bool SOMEIP_Generator_Fill_Header(const struct SOMEIP_Header *pParam, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    bool success = false;
    if ((NULL != pParam) && (NULL != pBuf) && (bufLen >= 16u)) {
        uint16_t len = Fill_SomeIP_Header(pBuf, pParam);
        if (0u != len) {
            SOMEIP_ASSERT((16u == len), __FILE__, __LINE__);
            if (NULL != pConsumed) {
                *pConsumed = (*pConsumed) + len;
            }
            success = true;
        }
    }
    return success;
}

bool SOMEIP_Generator_Update_Length(uint32_t consumed, uint8_t *pBuf, uint16_t bufLen)
{
    bool success = false;
    if ((consumed > 8) && (NULL != pBuf) && (bufLen >= 16u)) {
        uint32_t length = (consumed - 8u); /* Message ID and length fields do not count */
        pBuf[4] = length >> 24u;
        pBuf[5] = length >> 16u;
        pBuf[6] = length >> 8u;
        pBuf[7] = length;
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_Tag(uint16_t tagDataId, uint16_t length, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    uint16_t consume = ((0u != length) ? 4u : 2u) ;
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {

        if (0u != length) {
            pBuf[0] = (WIRETYPE_COMPLEX_2_BYTE << 4) | ((tagDataId >> 8) & 0xFu);
            pBuf[2] = (length >> 8);
            pBuf[3] = length & 0xFFu;
        } else {
            pBuf[0] = (WIRETYPE_COMPLEX_STATIC_SIZE << 4) | ((tagDataId >> 8) & 0xFu);
        }
        pBuf[1] = tagDataId & 0xFFu;
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_UINT8(uint16_t tagDataId, uint8_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    const uint16_t consume = 3u;
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {
        pBuf[0] = (WIRETYPE_8_BIT << 4) | ((tagDataId >> 8) & 0xFu);
        pBuf[1] = tagDataId & 0xFFu;
        pBuf[2] = value;
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_UINT16(uint16_t tagDataId, uint16_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    const uint16_t consume = 4u;
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {
        pBuf[0] = (WIRETYPE_16_BIT << 4) | ((tagDataId >> 8) & 0xFu);
        pBuf[1] = tagDataId & 0xFFu;
        pBuf[2] = (value >> 8);
        pBuf[3] = value & 0xFFu;
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_UINT32(uint16_t tagDataId, uint32_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    const uint16_t consume = 6u;
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {
        pBuf[0] = (WIRETYPE_32_BIT << 4) | ((tagDataId >> 8) & 0xFu);
        pBuf[1] = tagDataId & 0xFFu;
        pBuf[2] = (value >> 24);
        pBuf[3] = (value >> 16);
        pBuf[4] = (value >> 8);
        pBuf[5] = value & 0xFFu;
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_UINT64(uint16_t tagDataId, uint64_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    const uint16_t consume = 10u;
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {
        pBuf[0] = (WIRETYPE_64_BIT << 4) | ((tagDataId >> 8) & 0xFu);
        pBuf[1] = tagDataId & 0xFFu;
        pBuf[2] = (value >> 56) & 0xFFu;
        pBuf[3] = (value >> 48) & 0xFFu;
        pBuf[4] = (value >> 40) & 0xFFu;
        pBuf[5] = (value >> 32) & 0xFFu;
        pBuf[6] = (value >> 24) & 0xFFu;
        pBuf[7] = (value >> 16) & 0xFFu;
        pBuf[8] = (value >>  8) & 0xFFu;
        pBuf[9] = value & 0xFFu;
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

bool SOMEIP_Generator_Fill_BLOB(uint16_t tagDataId, const uint8_t *pBlob, uint16_t blobLen, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed)
{
    uint16_t consume = (blobLen + 4u);
    bool success = false;
    if ((tagDataId <= SOMEIP_MAX_TAG_ID) && (NULL != pBuf) && (bufLen >= consume)) {
        pBuf[0] = (WIRETYPE_COMPLEX_2_BYTE << 4) | ((tagDataId >> 8) & 0xFu);
        pBuf[1] = tagDataId & 0xFFu;
        pBuf[2] = (blobLen >> 8);
        pBuf[3] = blobLen & 0xFFu;
        if ((NULL != pBlob) && (0u != blobLen)) {
            (void)memcpy(&pBuf[4], pBlob, blobLen);
        }
        if (NULL != pConsumed) {
            *pConsumed = (*pConsumed) + consume;
        }
        success = true;
    }
    return success;
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                             INTERNAL API                             */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

uint16_t Fill_SomeIP_Frame(uint8_t **ppBuf, void **pMemTag, struct SOMEIP_SD_Frame *par)
{
    uint8_t *pBuf = NULL;
    uint16_t consumed = 0u;
    uint16_t currentPos = 0u;
    uint16_t result;
    bool success = (NULL != par);
    if (success) {
        uint16_t i;
        uint32_t lengthEntries = 0u;
        uint32_t lengthOptions = 0u;

        /* Counting needed buffer length in Bytes */
        consumed = SOMEIP_HEADER_LENGTH
                 + SOMEIP_SD_LENGTH
                 + SOMEIP_SD_OPTLEN_LENGTH;

        for (i = 0u; success && (i < MAX_SERVICE_EVNT_FIELDS); i++) {
            switch(par->servicesSel[i]) {
                case SOMEIP_SD_UnionUnused:
                    break;
                case SOMEIP_SD_UnionService:
                    consumed += SOMEIP_SD_SERVICE_LENGTH;
                    lengthEntries += SOMEIP_SD_SERVICE_LENGTH;
                    break;
                case SOMEIP_SD_UnionEvent:
                    consumed += SOMEIP_SD_EVTGRP_LENGTH;
                    lengthEntries += SOMEIP_SD_EVTGRP_LENGTH;
                    break;
                default:
                    SOMEIP_ASSERT(false, __FILE__, __LINE__);
                    success = false;
                    break;
            }
        }
        for (i = 0u; success && (i < MAX_OPTION_FIELDS); i++) {
            switch(par->optionsSel[i]) {
                case SOMEIP_SD_UnionOptNone:
                    break;
                case SOMEIP_SD_UnionOptIpV4:
                    consumed += SOMEIP_SD_OPT_IPV4_OUTER_LENGTH;
                    lengthOptions += SOMEIP_SD_OPT_IPV4_OUTER_LENGTH;
                    break;
                case SOMEIP_SD_UnionOptIpV4MCast:
                    consumed += SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH;
                    lengthOptions += SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH;
                    break;
                case SOMEIP_SD_UnionOptIpV4SD:
                    consumed += SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH;
                    lengthOptions += SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH;
                    break;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                case SOMEIP_SD_UnionOptConfig:
                    {
                        const struct SOMEIP_OptConfig *pConfig = &par->options[i].config;
                        uint32_t optLen = 5u; /* 4 Byte header + 1 zero termination */
                        for (uint16_t j = 0; j < pConfig->pairCount; j++) {
                            optLen += 2u; /* length field + equal character */
                            optLen += my_strnlen(pConfig->key[j], MAX_CONFIG_OPT_KEY_LEN - 1u);
                            optLen += my_strnlen(pConfig->val[j], MAX_CONFIG_OPT_VAL_LEN - 1u);
                        }
                        consumed += optLen;
                        lengthOptions += optLen;
                    }
                    break;
#endif
                default:
                    SOMEIP_ASSERT(false, __FILE__, __LINE__);
                    success = false;
                    break;
            }
        }


        /* External allocation of memory by integrator */
        if (success) {
            success = SOMEIP_CB_ProvideBuffer(&pBuf, pMemTag, consumed) && (NULL != pBuf);
        }

        /* Serializing given structure into given buffer */
        if (success) {
            par->someIp.length = consumed - 8u;
            par->someIpSd.length = lengthEntries;

            result = Fill_SomeIP_Header(&pBuf[currentPos], &par->someIp);
            currentPos += result;
            success = (0u != result);
        }
        if (success) {
            result = Fill_SomeIP_SD_Header(&pBuf[currentPos], &par->someIpSd);
            currentPos += result;
            success = (0u != result);
        }
        for (i = 0u; success && (i < MAX_SERVICE_EVNT_FIELDS); i++) {
            switch(par->servicesSel[i]) {
                case SOMEIP_SD_UnionUnused:
                    break;
                case SOMEIP_SD_UnionService:
                    result = Fill_SomeIP_SD_Service_Header(&pBuf[currentPos], &par->services[i].service);
                    currentPos += result;
                    success = (0u != result);
                    break;
                case SOMEIP_SD_UnionEvent:
                    result = Fill_SomeIP_SD_Event_Header(&pBuf[currentPos], &par->services[i].event);
                    currentPos += result;
                    success = (0u != result);
                    break;
                default:
                    SOMEIP_ASSERT(false, __FILE__, __LINE__);
                    success = false;
                    break;
            }
        }
        if (success) {
            result = Fill_SomeIP_SD_LengthOfOptions_Header(&pBuf[currentPos], lengthOptions);
            currentPos += result;
            success = (0u != result);
        }
        for (i = 0u; success && (i < MAX_OPTION_FIELDS); i++) {
            switch(par->optionsSel[i]) {
                case SOMEIP_SD_UnionOptNone:
                    break;
                case SOMEIP_SD_UnionOptIpV4:
                    result = Fill_SomeIP_SD_OptionIpV4_Header(&pBuf[currentPos], &par->options[i].ipV4);
                    currentPos += result;
                    success = (0u != result);
                    break;
                case SOMEIP_SD_UnionOptIpV4MCast:
                    result = Fill_SomeIP_SD_OptionIpV4Multicast_Header(&pBuf[currentPos], &par->options[i].ipV4MCast);
                    currentPos += result;
                    success = (0u != result);
                    break;
                case SOMEIP_SD_UnionOptIpV4SD:
                    result = Fill_SomeIP_SD_OptionIpV4SD_Header(&pBuf[currentPos], &par->options[i].ipV4Sd);
                    currentPos += result;
                    success = (0u != result);
                    break;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                case SOMEIP_SD_UnionOptConfig:
                    result = Fill_SomeIP_SD_OptionConfiguration_Header(&pBuf[currentPos], (consumed - currentPos), &par->options[i].config);
                    currentPos += result;
                    success = (0u != result);
                    break;
#endif
                default:
                    SOMEIP_ASSERT(false, __FILE__, __LINE__);
                    success = false;
                    break;
            }
        }
    }
    success = (success && (currentPos == consumed));
    if (NULL != ppBuf) {
        *ppBuf = (success ? pBuf : NULL);
    }
    return (success ? consumed : 0u);
}

uint16_t Fill_SomeIP_Header(uint8_t *pBuf, const struct SOMEIP_Header *par)
{
    uint16_t consumed = 0u;
    if ((NULL != pBuf) && (NULL != par)) {
        uint32_t msgId = MK_MASK(SOMEIP_FLD_ID_SERVICE_ID, par->serviceId)
                       | MK_MASK(SOMEIP_FLD_ID_MTHD_EVNT, (par->generateEvent ? 1u : 0u))
                       | MK_MASK(SOMEIP_FLD_ID_METHOD_ID, par->methodId);

        uint32_t reqId = MK_MASK(SOMEIP_FLD_REQID_CLIENT_ID, par->clientId)
                       | MK_MASK(SOMEIP_FLD_REQID_SESSION_ID, par->sessionId);

        (void)memset(pBuf, 0u, SOMEIP_HEADER_LENGTH);

        pBuf[SOMEIP_POS_MSG_ID_0] = msgId >> 24;
        pBuf[SOMEIP_POS_MSG_ID_1] = msgId >> 16;
        pBuf[SOMEIP_POS_MSG_ID_2] = msgId >> 8;
        pBuf[SOMEIP_POS_MSG_ID_3] = msgId;

        pBuf[SOMEIP_POS_LENGTH_0] = par->length >> 24;
        pBuf[SOMEIP_POS_LENGTH_1] = par->length >> 16;
        pBuf[SOMEIP_POS_LENGTH_2] = par->length >> 8;
        pBuf[SOMEIP_POS_LENGTH_3] = par->length;

        pBuf[SOMEIP_POS_REQ_ID_0] = reqId >> 24;
        pBuf[SOMEIP_POS_REQ_ID_1] = reqId >> 16;
        pBuf[SOMEIP_POS_REQ_ID_2] = reqId >> 8;
        pBuf[SOMEIP_POS_REQ_ID_3] = reqId;

        pBuf[SOMEIP_POS_PROT_VER] = SOMEIP_VERSION;
        pBuf[SOMEIP_POS_INTF_VER] = par->interfaceVersion;

        pBuf[SOMEIP_POS_MSG_TYPE] = par->msgType;
        pBuf[SOMEIP_POS_RET_CODE] = par->retCode;

        consumed = SOMEIP_HEADER_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_Header(uint8_t *pBuf, const struct SOMEIP_SD_Header *par)
{
    uint16_t consumed = 0u;
    if ((NULL != pBuf) && (NULL != par)) {
        uint32_t flags = (par->reboot ? SOMEIP_SD_FLD_FLAGS_REBOOT : 0u)
                       | (par->unicast ? SOMEIP_SD_FLD_FLAGS_UNICAST : 0u);

        (void)memset(pBuf, 0u, SOMEIP_SD_LENGTH);

        pBuf[SOMEIP_SD_POS_FLAGS] = flags;

        pBuf[SOMEIP_SD_POS_LEN_ENTR_0] = par->length >> 24;
        pBuf[SOMEIP_SD_POS_LEN_ENTR_1] = par->length >> 16;
        pBuf[SOMEIP_SD_POS_LEN_ENTR_2] = par->length >> 8;
        pBuf[SOMEIP_SD_POS_LEN_ENTR_3] = par->length;

        consumed = SOMEIP_SD_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_Service_Header(uint8_t *pBuf, const struct SOMEIP_SD_Service *par)
{
    uint16_t consumed = 0u;
    if ((NULL != pBuf) && (NULL != par)) {
        uint32_t numOpts = MK_MASK(SOMEIP_SD_SERVICE_NUM_OPT1, par->numberOfOpt1)
                         | MK_MASK(SOMEIP_SD_SERVICE_NUM_OPT2, par->numberOfOpt2);

        (void)memset(pBuf, 0u, SOMEIP_SD_SERVICE_LENGTH);

        pBuf[SOMEIP_SD_SERVICE_POS_TYPE] = par->type;
        pBuf[SOMEIP_SD_SERVICE_POS_INDEX_OPT1] = par->index1stOpt;
        pBuf[SOMEIP_SD_SERVICE_POS_INDEX_OPT2] = par->index2ndOpt;
        pBuf[SOMEIP_SD_SERVICE_POS_NUM_OPT] = numOpts;
        pBuf[SOMEIP_SD_SERVICE_POS_SERVICE_ID_0] = par->serviceID >> 8;
        pBuf[SOMEIP_SD_SERVICE_POS_SERVICE_ID_1] = par->serviceID & 0xFFu;
        pBuf[SOMEIP_SD_SERVICE_POS_INST_ID_0] = par->instanceID >> 8;
        pBuf[SOMEIP_SD_SERVICE_POS_INST_ID_1] = par->instanceID & 0xFFu;
        pBuf[SOMEIP_SD_SERVICE_POS_MAJOR_VER] = par->majorVersion;
        pBuf[SOMEIP_SD_SERVICE_POS_TTL_0] = par->ttl >> 16;
        pBuf[SOMEIP_SD_SERVICE_POS_TTL_1] = par->ttl >> 8;
        pBuf[SOMEIP_SD_SERVICE_POS_TTL_2] = par->ttl;
        pBuf[SOMEIP_SD_SERVICE_POS_MINOR_VER_0] = par->minorVersion >> 24;
        pBuf[SOMEIP_SD_SERVICE_POS_MINOR_VER_1] = par->minorVersion >> 16;
        pBuf[SOMEIP_SD_SERVICE_POS_MINOR_VER_2] = par->minorVersion >> 8;
        pBuf[SOMEIP_SD_SERVICE_POS_MINOR_VER_3] = par->minorVersion;

        consumed = SOMEIP_SD_SERVICE_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_Event_Header(uint8_t *pBuf, const struct SOMEIP_SD_Event *par)
{
    uint16_t consumed = 0u;
    if ((NULL != pBuf) && (NULL != par)) {
        uint32_t numOpts = MK_MASK(SOMEIP_SD_EVTGRP_NUM_OPT1, par->numberOfOpt1)
                         | MK_MASK(SOMEIP_SD_EVTGRP_NUM_OPT2, par->numberOfOpt2);

        uint32_t cnt = MK_MASK(SOMEIP_SD_EVTGRP_COUNTER, par->counter);

        (void)memset(pBuf, 0u, SOMEIP_SD_EVTGRP_LENGTH);

        pBuf[SOMEIP_SD_EVTGRP_POS_TYPE] = par->type;
        pBuf[SOMEIP_SD_EVTGRP_POS_INDEX_OPT1] = par->index1stOpt;
        pBuf[SOMEIP_SD_EVTGRP_POS_INDEX_OPT2] = par->index2ndOpt;
        pBuf[SOMEIP_SD_EVTGRP_POS_NUM_OPT] = numOpts;
        pBuf[SOMEIP_SD_EVTGRP_POS_SERVICE_ID_0] = par->serviceID >> 8;
        pBuf[SOMEIP_SD_EVTGRP_POS_SERVICE_ID_1] = par->serviceID & 0xFFu;
        pBuf[SOMEIP_SD_EVTGRP_POS_INST_ID_0] = par->instanceID >> 8;
        pBuf[SOMEIP_SD_EVTGRP_POS_INST_ID_1] = par->instanceID & 0xFFu;
        pBuf[SOMEIP_SD_EVTGRP_POS_MAJOR_VER] = par->majorVersion;
        pBuf[SOMEIP_SD_EVTGRP_POS_TTL_0] = par->ttl >> 16;
        pBuf[SOMEIP_SD_EVTGRP_POS_TTL_1] = par->ttl >> 8;
        pBuf[SOMEIP_SD_EVTGRP_POS_TTL_2] = par->ttl;
        pBuf[SOMEIP_SD_EVTGRP_POS_COUNTER] = cnt;
        pBuf[SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_0] = par->eventGroupID >> 8;
        pBuf[SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_1] = par->eventGroupID & 0xFFu;

        consumed = SOMEIP_SD_EVTGRP_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_LengthOfOptions_Header(uint8_t *pBuf, uint32_t optionLength)
{
    uint16_t consumed = 0u;
    if (NULL != pBuf) {
        (void)memset(pBuf, 0u, SOMEIP_SD_OPTLEN_LENGTH);

        pBuf[SOMEIP_SD_OPTLEN_POS_LEN_0] = optionLength >> 24;
        pBuf[SOMEIP_SD_OPTLEN_POS_LEN_1] = optionLength >> 16;
        pBuf[SOMEIP_SD_OPTLEN_POS_LEN_2] = optionLength >> 8;
        pBuf[SOMEIP_SD_OPTLEN_POS_LEN_3] = optionLength;
        consumed = SOMEIP_SD_OPTLEN_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_OptionConfiguration_Header(uint8_t *pBuf, uint16_t bufLength, const struct SOMEIP_OptConfig *pConfig)
{
    uint16_t consumed = 0u;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    if ((NULL != pBuf) && (0u != bufLength) && (NULL != pConfig)) {
        uint16_t i;
        uint16_t payloadLen;

        consumed = SOMEIP_SD_OPT_CONFIG_POS_STRING;

        for (i = 0; i < pConfig->pairCount; i++) {

            uint16_t keyLen = (uint16_t)my_strnlen(pConfig->key[i], MAX_CONFIG_OPT_KEY_LEN - 1u);
            uint16_t valLen = (uint16_t)my_strnlen(pConfig->val[i], MAX_CONFIG_OPT_VAL_LEN - 1u);

            pBuf[consumed++] = keyLen + 1u + valLen;

            (void)memcpy(&pBuf[consumed], pConfig->key[i], keyLen);
            consumed += keyLen;

            pBuf[consumed++] = '=';

            (void)memcpy(&pBuf[consumed], pConfig->val[i], valLen);
            consumed += valLen;
        }
        pBuf[consumed++] = 0x0u /* Zero termination */;

        payloadLen = (consumed - SOMEIP_SD_OPT_CONFIG_HDR_LENGTH);

        pBuf[SOMEIP_SD_OPT_CONFIG_POS_LENGTH_0] = payloadLen >> 8;
        pBuf[SOMEIP_SD_OPT_CONFIG_POS_LENGTH_1] = payloadLen & 0xFFu;
        pBuf[SOMEIP_SD_OPT_CONFIG_POS_TYPE] = SOMEIP_SD_OPT_CONFIG_TYPE_VALUE;
        pBuf[SOMEIP_SD_OPT_CONFIG_POS_RESERVED] = 0x0u;

        SOMEIP_ASSERT((consumed <= bufLength), __FILE__, __LINE__);
    }
#endif
    return consumed;
}

uint16_t Fill_SomeIP_SD_OptionIpV4_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4 *par)
{
    uint16_t consumed = 0u;
    if (NULL != pBuf) {
        (void)memset(pBuf, 0u, SOMEIP_SD_OPT_IPV4_OUTER_LENGTH);

        pBuf[SOMEIP_SD_OPT_IPV4_POS_LENGTH_0] = SOMEIP_SD_OPT_IPV4_INNER_LENGTH >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4_POS_LENGTH_1] = SOMEIP_SD_OPT_IPV4_INNER_LENGTH;
        pBuf[SOMEIP_SD_OPT_IPV4_POS_TYPE] = SOMEIP_SD_OPT_IPV4_TYPE_VALUE;
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_0] = par->ipV4Addr[0];
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_1] = par->ipV4Addr[1];
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_2] = par->ipV4Addr[2];
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_3] = par->ipV4Addr[3];
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_PROTO] = (par->udp ? SOMEIP_SD_OPT_IPV4_PROT_UDP : SOMEIP_SD_OPT_IPV4_PROT_TCP);
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_0] = par->portNumber >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_1] = par->portNumber & 0xFFu;

        consumed = SOMEIP_SD_OPT_IPV4_OUTER_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_OptionIpV4Multicast_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4Mcast *par)
{
    uint16_t consumed = 0u;
    if (NULL != pBuf) {
        (void)memset(pBuf, 0u, SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH);

        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_0] = SOMEIP_SD_OPT_IPV4MCAST_INNER_LENGTH >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_1] = SOMEIP_SD_OPT_IPV4MCAST_INNER_LENGTH;
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_TYPE] = SOMEIP_SD_OPT_IPV4MCAST_TYPE_VALUE;
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_0] = par->ipV4Addr[0];
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_1] = par->ipV4Addr[1];
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_2] = par->ipV4Addr[2];
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_3] = par->ipV4Addr[3];
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PROTO] = SOMEIP_SD_OPT_IPV4MCAST_PROT_UDP;
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_0] = par->portNumber >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_1] = par->portNumber & 0xFFu;

        consumed = SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH;
    }
    return consumed;
}

uint16_t Fill_SomeIP_SD_OptionIpV4SD_Header(uint8_t *pBuf, const struct SOMEIP_SD_OptIpV4SD *par)
{
    uint16_t consumed = 0u;
    if (NULL != pBuf) {
        (void)memset(pBuf, 0u, SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH);

        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_0] = SOMEIP_SD_OPT_IPV4_SD_INNER_LENGTH >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_1] = SOMEIP_SD_OPT_IPV4_SD_INNER_LENGTH;
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_TYPE] = SOMEIP_SD_OPT_IPV4_SD_TYPE_VALUE;
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_0] = par->ipV4Addr[0];
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_1] = par->ipV4Addr[1];
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_2] = par->ipV4Addr[2];
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_3] = par->ipV4Addr[3];
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PROTO] = (par->udp ? SOMEIP_SD_OPT_IPV4_SD_PROT_UDP : SOMEIP_SD_OPT_IPV4_SD_PROT_TCP);
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_0] = par->portNumber >> 8;
        pBuf[SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_1] = par->portNumber & 0xFFu;

        consumed = SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH;
    }
    return consumed;
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                  PRIVATE FUNCTION IMPLEMENTATIONS                    */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static uint32_t my_strnlen(const char *str, uint32_t max)
{
    const char *end = memchr (str, 0, max);
    return end ? (uint32_t)(end - str) : max;
}

