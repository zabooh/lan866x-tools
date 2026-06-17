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
/* SOME/IP Transmitter for embedded systems                                                       */
/*------------------------------------------------------------------------------------------------*/

#include <string.h>
#include "someip-cfg.h"
#include "someip-common.h"

struct state_vars;

struct SOMETR_t
{
    struct SOMEIP_Transmit_Buffer pub[SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES];
    void *sockHandle;
    uint16_t lastBufPos;
    bool used;
};

static struct SOMETR_t m_tr[SOMEIP_TRANSMIT_MAX_INSTANCES] = { 0 };

SOMETR_t *SOMEIP_Transmit_Init(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback, void *rxTag)
{
    uint8_t i;
    SOMETR_t *pInst = NULL;
    /* Find new free entry */
    for (i = 0u; (NULL == pInst) && (i < SOMEIP_TRANSMIT_MAX_INSTANCES); i++)
    {
        struct SOMETR_t *pTr = &m_tr[i];
        if (!pTr->used) {
            pTr->used = true;
            if (SOMEIP_CB_OpenSocket(udpPort, rxCallback, rxTag, &pTr->sockHandle)) {
                pInst = pTr;
            } else {
                pTr->used = false;
            }
        }
    }
    return pInst;
}

struct SOMEIP_Transmit_Buffer *SOMEIP_Transmit_GetBuffer(SOMETR_t *tr)
{
    uint16_t j;
    struct SOMEIP_Transmit_Buffer *pBuf = NULL;
    uint32_t now = SOMEIP_CB_GetTimeMS();
    SOMEIP_CB_EnterCriticialSection();
    for (j = 0u; (NULL == pBuf) && (j < SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES); j++) {
        uint16_t pos = (j + tr->lastBufPos) % SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES;
        if ((0u == tr->pub[pos].ipV4Addr[0]) || ((now - tr->pub[pos].sendTime) > SOMEIP_TRANSMIT_MAX_TIMEOUT_TIME)) {
            pBuf = &tr->pub[pos];
            tr->lastBufPos = (pos + 1);
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
    return pBuf;
}

bool SOMEIP_Transmit_Send(SOMETR_t *tr, struct SOMEIP_Transmit_Buffer *pBuf)
{
    bool success = false;
    SOMEIP_CB_EnterCriticialSection();
    if ((NULL != pBuf) && (0u != pBuf->ipV4Addr[0]) && (0u != pBuf->udpPort) && (pBuf->payloadLength <= SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN)) {
        pBuf->sendTime = SOMEIP_CB_GetTimeMS();
        if (SOMEIP_CB_SendUdp((uint8_t *)pBuf->payload, pBuf->payloadLength, NULL, pBuf->ipV4Addr, pBuf->udpPort, tr->sockHandle)) {
            success = true;
            if (pBuf->fireAndForget) {
                memset(pBuf->ipV4Addr, 0, sizeof(pBuf->ipV4Addr));
            }
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
    return success;
}

void SOMEIP_Transmit_ReleaseBufferOnError(SOMETR_t *tr, struct SOMEIP_Transmit_Buffer *pBuf)
{
    (void)tr;
    SOMEIP_CB_EnterCriticialSection();
    if (NULL != pBuf) {
        memset(pBuf->ipV4Addr, 0, sizeof(pBuf->ipV4Addr));
    }
    SOMEIP_CB_LeaveCriticialSection();
}

void SOMEIP_Transmit_CheckTimers(void)
{
#if !defined(_WIN32) && !defined(__linux__)
    uint32_t now = SOMEIP_CB_GetTimeMS();
    uint16_t i;
    uint8_t j;
    SOMEIP_CB_EnterCriticialSection();
    for (j = 0u; j < SOMEIP_TRANSMIT_MAX_INSTANCES; j++) {
        SOMETR_t *tr = &m_tr[j];
        if (!tr->used) {
            continue;
        }

        for (i = 0u; i < SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES; i++) {
            struct SOMEIP_Transmit_Buffer *pub = &tr->pub[i];
            if ((0u != pub->ipV4Addr[0]) && ((now - pub->sendTime) >= SOMEIP_TRANSMIT_MAX_TIMEOUT_TIME)) {
                if (NULL != pub->callback) {
                    pub->callback(pub, false, SOMEIP_E_TIMEOUT, NULL, 0u);
                }
                memset(pub->ipV4Addr, 0, sizeof(pub->ipV4Addr));
            }
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
#endif
}

void SOMEIP_Transmit_ReceivedResponse(uint8_t remoteIP[SOMEIP_IPV4_ADDR_LEN], SOMETR_t *tr, uint16_t sessionId, enum SOMEIP_ReturnCode retCode, const uint8_t *pRxBuf, uint16_t rxBufLen)
{
    uint16_t i;
    bool found = false;

    for (i = 0u; !found && (i < SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES); i++) {
        struct SOMEIP_Transmit_Buffer *pub;

        SOMEIP_CB_EnterCriticialSection();
        pub = &tr->pub[i];

        if ((0u == memcmp(pub->ipV4Addr, remoteIP, SOMEIP_IPV4_ADDR_LEN)) && (pub->waitForSessionId == sessionId)) {
            SOMEIP_ASSERT(!pub->fireAndForget, __FILE__, __LINE__);
            if (NULL != pub->callback) {
                pub->callback(pub, true, retCode, pRxBuf, rxBufLen);
            }
            memset(pub->ipV4Addr, 0, sizeof(pub->ipV4Addr));
            found = true;
        }
        SOMEIP_CB_LeaveCriticialSection();
    }

    SOMEIP_CB_NeedService();
}

