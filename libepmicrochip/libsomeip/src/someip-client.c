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
/* SOME/IP Client Statemachine                                                                    */
/*------------------------------------------------------------------------------------------------*/

#include <string.h> /* memcpy */
#include "someip-cfg.h"
#include "someip-gen.h"
#include "someip-pars.h"
#include "someip-timer.h"

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                            DEFINITIONS                               */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

enum Client_State
{
    C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN = 0x20,
    C_STATE_NOT_REQUESTED_SERVICE_SEEN,
    C_STATE_REQUESTED_BUT_NOT_READY,
    C_STATE_MAIN,
    C_STATE_MAIN_SERVICE_READY,
    C_STATE_MAIN_STOPPED,
    C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE,
    C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE_TIMER_SET,
    C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE,
    C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE_TIMER_SET
};

enum Event_State
{
    CE_STATE_SERVICE_IDLE  = 0x0,
    CE_STATE_SERVICE_OFFER_RECEIVED_INITAL_WAIT,
    CE_STATE_SERVICE_OFFER_RECEIVED_SEND_SUBSRIBE,
};

struct Event_Var
{
    struct SomeIP_Timer eventTimer;
    enum Event_State state;
    uint8_t counter;
    uint8_t retries;
    bool receivedTriggerSubscription;
    bool receivedSubscribeAck;
    bool receivedSubscribeNack;
};

#if (0 != MAX_CONFIG_OPT_ENTRIES)
struct Config_Var
{
    struct SOMEIP_OptConfig receivedConfig;
    struct SOMEIP_OptConfig sendConfig;
    SOMEIP_SendConfig_Callback_t pSendCB;
    uint32_t lastSendTime;
    uint16_t retries;
    bool receivedConfigValid;
    bool sendConfigValid;
    bool receivedAck;
};
#endif

struct ClientConnection_var
{
    struct SomeIP_Timer stateTimer;
    struct SOMEIP_IpAddr receivedAddr;
    struct Event_Var event;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    struct Config_Var config;
#endif
    enum Client_State state;
    uint32_t sessionIdsTx[2]; /* [0] for Multicast and [1] for Unicast */
    uint32_t sessionIdsRx[2]; /* [0] for Multicast and [1] for Unicast */
    uint16_t instanceId;
    uint16_t run;
    bool receivedOffserService;
    bool receivedStopOffserService;
    bool serviceAvailable;
    bool resetDetected;
};

struct Client_Var
{
    struct SOMEIP_Server_Client info;
    struct ClientConnection_var con[MAX_CONNECTIONS_CLIENT];
    void *sockHandle;
    bool up_and_configured;
    bool service_requested;
    bool subscribe_to_event;
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                            LOCAL VARIABLES                           */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static void *m_discoverySockHandle = NULL;
static struct Client_Var m_client[MAX_CLIENT_SERVICES] = { 0 };
extern uint8_t MULTICAST_IP[SOMEIP_IPV4_ADDR_LEN];

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                       LOCAL FUNCTION DECLERATIONS                    */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static int32_t GetClientIndexService(const struct SOMEIP_SD_Service *pService, enum SOMEIP_ReturnCode *pResult);
static int32_t GetClientIndexEvent(const struct SOMEIP_SD_Event *pEvent, enum SOMEIP_ReturnCode *pResult);
static int16_t GetConnectionFromClient(struct Client_Var *pClient, const struct SOMEIP_IpAddr *pRemoteIp);
static void CheckSessionId(struct ClientConnection_var *pClient, uint16_t receivedSessionId, bool receviedMulticast);
static bool SendService(struct Client_Var *pClient, struct ClientConnection_var *pCon, enum SOMEIP_SD_ServiceEntryType servicType, uint32_t ttl, bool multicast);
static bool SendEvent(struct Client_Var *pClient, struct ClientConnection_var *pCon, enum SOMEIP_CB_EventEntryType eventType, uint32_t ttl, uint8_t counter, uint16_t eventGroupID, bool multicast);
static bool FindRemoteIp(const struct SOMEIP_SD_Frame *pFrame, struct SOMEIP_IpAddr *pRemoteIp);
static void HandleReceivedConfig(const struct SOMEIP_SD_Frame *pFrame, struct Client_Var *pClient);

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                           PUBLIC API                                 */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

bool SOMEIP_Client_AddService(const struct SOMEIP_Server_Client *pClientInfo, bool requested, bool subscribeToEvent)
{
    uint16_t i;
    bool serviceAdded = false;

    SOMEIP_CB_EnterCriticialSection();
    if (NULL == m_discoverySockHandle) {
        uint16_t port = SD_PORT;
        SOMEIP_CB_OpenSocket(&port, SOMEIP_Client_DataReceived, NULL, &m_discoverySockHandle);
    }
    for (i = 0; !serviceAdded && (NULL != m_discoverySockHandle) && (i < MAX_CLIENT_SERVICES); i++) {
        struct Client_Var *pClient = &m_client[i];
        if (!pClient->up_and_configured) {
            uint16_t k;
            pClient->sockHandle = m_discoverySockHandle;
            pClient->up_and_configured = true;
            pClient->service_requested = requested;
            pClient->subscribe_to_event = subscribeToEvent;
            for (k = 0; k < MAX_CONNECTIONS_CLIENT; k++) {
                if (requested) {
                    pClient->con[k].state = C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE;
                } else {
                    pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN;
                }
                SOMEIP_Timer_Init(&pClient->con[k].stateTimer);
            }
            (void)memcpy(&pClient->info, pClientInfo, sizeof(struct SOMEIP_Server_Client));
            serviceAdded = true;
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
    if (serviceAdded) {
        SOMEIP_CB_NeedService();
    }
    return serviceAdded;
}

bool SOMEIP_Client_SetServiceRequested(uint16_t serviceId, bool requested)
{
    uint16_t i;
    bool success = false;
    SOMEIP_CB_EnterCriticialSection();
    for (i = 0; !success && (i < MAX_CLIENT_SERVICES); i++) {
        struct Client_Var *pClient = &m_client[i];
        if (pClient->up_and_configured &&
                (pClient->info.serviceId == serviceId))
        {
            pClient->service_requested = requested;
            success = true;
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
    if (success) {
        SOMEIP_CB_NeedService();
    }
    return success;
}

void SOMEIP_Client_CheckTimers(void)
{
    uint32_t delay;
    uint16_t i;
    bool needService;
    SOMEIP_CB_EnterCriticialSection();
    do {
        needService = false;
        for (i = 0; i < MAX_CLIENT_SERVICES; i++) {
            struct Client_Var *pClient = &m_client[i];
            if (pClient->up_and_configured) {
                uint16_t k;
                for (k = 0; k < MAX_CONNECTIONS_CLIENT; k++) {
                    enum Client_State oldState = pClient->con[k].state;
                    if (pClient->con[k].resetDetected) {
                        pClient->con[k].resetDetected = false;
                        pClient->con[k].sessionIdsRx[0] = 0u;
                        pClient->con[k].sessionIdsRx[1] = 0u;
                        if (NULL != pClient->info.pEventCb) {
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                            pClient->info.pEventCb(EV_CLIENT_REMOTE_RESET_DETECTED, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, (pClient->con[k].config.receivedConfigValid ? &pClient->con[k].config.receivedConfig : NULL), pClient->info.cbData);
                            pClient->con[k].config.receivedConfigValid = false;
#else
                            pClient->info.pEventCb(EV_CLIENT_REMOTE_RESET_DETECTED, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, NULL, pClient->info.cbData);
#endif
                        }
                        pClient->con[k].state = C_STATE_MAIN_STOPPED;
                    }
                    switch(pClient->con[k].state) {
                    case C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN:
                        if (pClient->con[k].receivedOffserService) {
                            pClient->con[k].receivedOffserService = false;
                            SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, (1000u * pClient->info.ttl));
                            pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_SEEN;
                            needService = true;
                        }
                        break;

                    case C_STATE_NOT_REQUESTED_SERVICE_SEEN:
                        if (pClient->con[k].receivedOffserService) {
                            pClient->con[k].receivedOffserService = false;
                            SOMEIP_Timer_Reset(&pClient->con[k].stateTimer);
                        }
                        else if (pClient->con[k].receivedStopOffserService) {
                            pClient->con[k].receivedStopOffserService = false;
                            pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN;
                        }
                        else if (SOMEIP_Timer_CheckTimer(&pClient->con[k].stateTimer)) {
                            pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN;
                        }
                        else if (pClient->service_requested) {
                            pClient->con[k].state = C_STATE_MAIN;
                        }
                        else { /* MISRA enforced if-else termination */ }
                        break;

                    case C_STATE_REQUESTED_BUT_NOT_READY:
                        SOMEIP_ASSERT(false, __FILE__, __LINE__); /* Not used in this implemetation */
                        break;

                    case C_STATE_MAIN:
                        break;

                    case C_STATE_MAIN_SERVICE_READY:
                        if (!pClient->service_requested) {
                            pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_SEEN;
                            needService = true;
                        }
                        else if (pClient->con[k].receivedOffserService) {
                            pClient->con[k].receivedOffserService = false;
                            SOMEIP_Timer_Reset(&pClient->con[k].stateTimer);
                        }
                        else if (SOMEIP_Timer_CheckTimer(&pClient->con[k].stateTimer)) {
                            pClient->con[k].state = C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE;
                        }
                        else if (pClient->con[k].receivedStopOffserService) {
                            pClient->con[k].receivedStopOffserService = false;
                            SOMEIP_Timer_Init(&pClient->con[k].stateTimer);
                            pClient->con[k].state = C_STATE_MAIN_STOPPED;
                        }
                        else { /* MISRA enforced if-else termination */ }
                        break;

                    case C_STATE_MAIN_STOPPED:
                        if (!pClient->service_requested) {
                            pClient->con[k].state = C_STATE_NOT_REQUESTED_SERVICE_NOT_SEEN;
                        }
                        else if (pClient->con[k].receivedOffserService) {
                            pClient->con[k].receivedOffserService = false;
                            SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, (1000u * pClient->info.ttl));
                            pClient->con[k].state = C_STATE_MAIN_SERVICE_READY;
                            needService = true;
                        }
                        else { /* MISRA enforced if-else termination */ }
                        break;

                    case C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE:
                        SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, SOMEIP_CB_GetRandom(INITIAL_DELAY_MIN, INITIAL_DELAY_MAX));
                        pClient->con[k].state = C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE_TIMER_SET;
                        break;

                    case C_STATE_SEARCHINIG_FOR_SERVICE_INITIAL_WAIT_PHASE_TIMER_SET:
                        if (SOMEIP_Timer_CheckTimer(&pClient->con[k].stateTimer)) {
                            pClient->con[k].run = 0;
                            pClient->con[k].state = C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE;
                            needService = true;
                        }
                        break;

                    case C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE:
                        if ((k != 0) || (SendService(pClient, &pClient->con[k], SDServiceType_FindService, pClient->info.ttl, true))) {
#if (REPETITIONS_MAX > 0u) && (REPETITIONS_BASE_DELAY > 0u)
                            delay = REPETITIONS_BASE_DELAY;
                            SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, delay);
                            pClient->con[k].state = C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE_TIMER_SET;
#else
                            pClient->con[k].state = C_STATE_MAIN_STOPPED;
#endif
                        }
                        break;

                    case C_STATE_SEARCHINIG_FOR_SERVICE_REPETITION_PHASE_TIMER_SET:
#if (REPETITIONS_MAX > 0u) && (REPETITIONS_BASE_DELAY > 0u)
                        if (pClient->con[k].receivedOffserService) {
                            pClient->con[k].receivedOffserService = false;
                            SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, (1000u * pClient->info.ttl));
                            pClient->con[k].state = C_STATE_MAIN_SERVICE_READY;
                            needService = true;
                        }
                        else if (pClient->con[k].receivedStopOffserService) {
                            pClient->con[k].receivedStopOffserService = false;
                            SOMEIP_Timer_Init(&pClient->con[k].stateTimer);
                            pClient->con[k].state = C_STATE_MAIN_STOPPED;
                        }
                        else if (SOMEIP_Timer_CheckTimer(&pClient->con[k].stateTimer)) {
                            if (pClient->con[k].run < REPETITIONS_MAX) {
                                pClient->con[k].run++;
                                delay = (1u << pClient->con[k].run) * REPETITIONS_BASE_DELAY;
                                if (delay > CYCLIC_OFFER_DELAY) {
                                    delay = CYCLIC_OFFER_DELAY;
                                }
                                SOMEIP_Timer_SetTimer(&pClient->con[k].stateTimer, delay);
                            } else {
                                SOMEIP_Timer_Init(&pClient->con[k].stateTimer);
                                pClient->con[k].state = C_STATE_MAIN_STOPPED;
                            }
                        }
                        else { /* MISRA enforced if-else termination */ }
#else
                        SOMEIP_ASSERT(false);
#endif
                        break;

                    default:
                        SOMEIP_ASSERT(false, __FILE__, __LINE__);
                        break;
                    }
                    if (oldState != pClient->con[k].state) {
                        if (C_STATE_MAIN_SERVICE_READY == pClient->con[k].state) {
                            pClient->con[k].serviceAvailable = true;
                            if (NULL != pClient->info.pEventCb) {
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                                pClient->info.pEventCb(EV_CLIENT_SERVICE_AVAILABLE, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, (pClient->con[k].config.receivedConfigValid ? &pClient->con[k].config.receivedConfig : NULL), pClient->info.cbData);
                                pClient->con[k].config.receivedConfigValid = false;
#else
                                pClient->info.pEventCb(EV_CLIENT_SERVICE_AVAILABLE, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, NULL, pClient->info.cbData);
#endif
                            }
                        }
                        else if (C_STATE_MAIN_STOPPED == pClient->con[k].state) {
                            pClient->con[k].serviceAvailable = false;
                            (void)memset(&pClient->con[k].event, 0u, sizeof(pClient->con[k].event)); /* Clear all event variables */
                            if (NULL != pClient->info.pEventCb) {
#if (0 != MAX_CONFIG_OPT_ENTRIES)
                                pClient->info.pEventCb(EV_CLIENT_SERVICE_STOPPED, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, (pClient->con[k].config.receivedConfigValid ? &pClient->con[k].config.receivedConfig : NULL), NULL);
                                pClient->con[k].config.receivedConfigValid = false;
#else
                                pClient->info.pEventCb(EV_CLIENT_SERVICE_STOPPED, &pClient->info, &pClient->con[k].receivedAddr, pClient->con[k].instanceId, NULL, NULL);
#endif
                            }
                        }
                        else { /* MISRA enforced if-else termination */ }
                    }

#if (0 != MAX_CONFIG_OPT_ENTRIES)
                    /* Check if Configuration needs to be send or retry is needed */
                    if (pClient->con[k].config.sendConfigValid) {
                        if (pClient->con[k].serviceAvailable) {
                            uint32_t now = SOMEIP_CB_GetTimeMS();
                            if (pClient->con[k].config.receivedAck) {
                                pClient->con[k].config.receivedAck = false;
                                if (NULL != pClient->con[k].config.pSendCB) {
                                    pClient->con[k].config.pSendCB(pClient->info.serviceId , pClient->con[k].instanceId, &pClient->con[k].config.sendConfig, true);
                                }
                                pClient->con[k].config.sendConfigValid = false;
                            }
                            else if ((now - pClient->con[k].config.lastSendTime) >= CONFIG_RETRY_DELAY_TIME) {
                                if (pClient->con[k].config.retries <= MAX_CONFIG_RETRIES) {
                                    if ((k != 0) || (SendService(pClient, &pClient->con[k], SDServiceType_FindService, pClient->info.ttl, false))) {
                                        pClient->con[k].config.lastSendTime = now;
                                        pClient->con[k].config.retries++;
                                    }
                                } else {
                                    /* All retries done */
                                    if (NULL != pClient->con[k].config.pSendCB) {
                                        pClient->con[k].config.pSendCB(pClient->info.serviceId , pClient->con[k].instanceId, &pClient->con[k].config.sendConfig, false);
                                    }
                                    pClient->con[k].config.sendConfigValid = false;
                                }
                            }
                            else { /* MISRA enforced if-else termination */ }
                        } else {
                            if (NULL != pClient->con[k].config.pSendCB) {
                                pClient->con[k].config.pSendCB(pClient->info.serviceId , pClient->con[k].instanceId, &pClient->con[k].config.sendConfig, false);
                            }
                            pClient->con[k].config.sendConfigValid = false;
                        }
                    }
#endif

                    /* Check Events */
                    if (pClient->con[k].serviceAvailable && pClient->info.eventHandlingEnabled) {
                        switch(pClient->con[k].event.state) {
                        case CE_STATE_SERVICE_IDLE:
                            if (pClient->con[k].event.receivedTriggerSubscription) {
                                pClient->con[k].event.receivedTriggerSubscription = false;

                                pClient->con[k].event.retries = 0u;
                                pClient->con[k].event.receivedSubscribeAck = false;
                                pClient->con[k].event.receivedSubscribeNack = false;
                                SOMEIP_Timer_SetTimer(&pClient->con[k].event.eventTimer, SOMEIP_CB_GetRandom(SUBSCRIPTION_DELAY_MIN, SUBSCRIPTION_DELAY_MAX));
                                pClient->con[k].event.state = CE_STATE_SERVICE_OFFER_RECEIVED_INITAL_WAIT;
                            }
                            break;
                        case CE_STATE_SERVICE_OFFER_RECEIVED_INITAL_WAIT:
                            if (SOMEIP_Timer_CheckTimer(&pClient->con[k].event.eventTimer)) {
                                pClient->con[k].event.state = CE_STATE_SERVICE_OFFER_RECEIVED_SEND_SUBSRIBE;
                            }
                            break;
                        case CE_STATE_SERVICE_OFFER_RECEIVED_SEND_SUBSRIBE:
                            if (pClient->subscribe_to_event) {
                                if (SendEvent(pClient, &pClient->con[k], SDEventType_SubscribeEventGroup, pClient->info.ttl, pClient->con[k].event.counter, pClient->info.eventGroupId, false)) {
                                    pClient->con[k].event.counter++;
                                    pClient->con[k].event.state = CE_STATE_SERVICE_IDLE;
                                }
                            } else {
                                pClient->con[k].event.state = CE_STATE_SERVICE_IDLE;
                            }
                            break;
                        default:
                            SOMEIP_ASSERT(false, __FILE__, __LINE__);
                            break;
                        }
                    }
                }
            }
        }
    }
    while(needService);
    SOMEIP_CB_LeaveCriticialSection();
}

enum SOMEIP_ReturnCode SOMEIP_Client_DataReceived(const uint8_t *b, uint16_t bLen, struct SOMEIP_IpAddr *pIpAddr, void *rxTag)
{
    struct SOMEIP_IpAddr frameIp = { 0 };
    struct SOMEIP_SD_Frame frame;
    enum SOMEIP_ReturnCode result;
    uint16_t i;
    bool receivedMulticast = false;
    (void)rxTag;
    SOMEIP_CB_EnterCriticialSection();
    result = Parse_SomeIP_Frame(b, bLen, &frame);
    if ((169 == pIpAddr->sourceAddr[0]) || (169 == pIpAddr->destinAddr[0])) {
        /* Ignore Link local Addresses */
        result = SOMEIP_E_NOT_REACHABLE;
    }
    if ((SOMEIP_E_OK == result) && (SOMEIP_SD_SERVICE_ID != frame.someIp.serviceId)) {
        result = SOMEIP_E_UNKNOWN_SERVICE;
    }
    if ((SOMEIP_E_OK == result) && (SOMEIP_Event_ID != frame.someIp.methodId)) {
        result = SOMEIP_E_UNKNOWN_METHOD;
    }
    if ((SOMEIP_E_OK == result) && (SOMEIP_SD_SERVICE_EVENT != frame.someIp.generateEvent)) {
        result = SOMEIP_E_UNKNOWN_METHOD;
    }
    if ((SOMEIP_E_OK == result) && (SOMEIP_SD_INTERFACE_VER != frame.someIp.interfaceVersion)) {
        result = SOMEIP_E_WRONG_INTERFACE_VERSION;
    }
    if (SOMEIP_E_OK == result) {
        if (!FindRemoteIp(&frame, &frameIp)) {
            memcpy(frameIp.sourceAddr, pIpAddr->destinAddr, 4u);
            frameIp.port = pIpAddr->port;
        }

        if ((NULL != pIpAddr) && (0u != pIpAddr->destinAddr[0])) {
            receivedMulticast = (224u == pIpAddr->destinAddr[0]) || (0 == memcmp(pIpAddr->destinAddr, MULTICAST_IP, SOMEIP_IPV4_ADDR_LEN));
        }

        if (0u != frameIp.sourceAddr[0]) {

            for (i = 0u; i < MAX_SERVICE_EVNT_FIELDS; i++) {
                if (SOMEIP_SD_UnionService == frame.servicesSel[i]) {
                    if (SDServiceType_OfferService == frame.services[i].service.type) {

                        int32_t index = GetClientIndexService(&frame.services[i].service, &result);
                        if ((index >= 0) && (SOMEIP_E_OK == result)) {
                            struct Client_Var *pClient = &m_client[index];
                            int32_t k = GetConnectionFromClient(pClient, &frameIp);
                            if (k >= 0) {
                                SOMEIP_ASSERT(pClient->up_and_configured, __FILE__, __LINE__);

                                pClient->con[k].instanceId = frame.services[i].service.instanceID;

                                HandleReceivedConfig(&frame, pClient);

                                CheckSessionId(&pClient->con[k], frame.someIp.sessionId, receivedMulticast);

                                if ((0x0u != frame.services[i].service.ttl)) {
                                    /* Offer Service */
                                    pClient->con[k].receivedOffserService = true;

                                    /* Trigger Event to send out Subscribe */
                                    pClient->con[k].event.receivedTriggerSubscription = true;

#if (0 != MAX_CONFIG_OPT_ENTRIES)
                                    if (pClient->con[k].config.sendConfigValid && !receivedMulticast) {
                                        pClient->con[k].config.receivedAck = true;
                                    }
#endif
                                } else {
                                    /* Stop Offer Service */
                                    pClient->con[k].receivedStopOffserService = true;
                                }
                            }
                        }
                    }
                }
                else if (SOMEIP_SD_UnionEvent == frame.servicesSel[i]) {
                    if (SDEventType_SubscribeEventgroupAck == frame.services[i].event.type) {
                        int32_t index = GetClientIndexEvent(&frame.services[i].event, &result);
                        if ((index >= 0) && (SOMEIP_E_OK == result)) {
                            struct Client_Var *pClient = &m_client[index];
                            int32_t k = GetConnectionFromClient(pClient, &frameIp);
                            if (k >= 0) {
                                (void)memcpy(&pClient->con[k].receivedAddr, &frameIp, sizeof(struct SOMEIP_IpAddr));

                                pClient->con[k].instanceId = frame.services[i].event.instanceID;

                                HandleReceivedConfig(&frame, pClient);

                                if ((0x0u != frame.services[i].service.ttl)) {
                                    /* SubscribeEventgroup ACK */
                                    pClient->con[k].event.receivedSubscribeAck = true;
                                } else {
                                    /* SubscribeEventgroup NOT ACK */
                                    pClient->con[k].event.receivedSubscribeNack = true;
                                }
                            }
                        }
                    }
                }
                else { /* MISRA enforced if-else termination */ }
            }
        } else {
            result = SOMEIP_E_NOT_REACHABLE;
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
    SOMEIP_CB_NeedService();
    return result;
}

bool SOMEIP_Client_SendConfig(uint16_t serviceId, uint16_t instanceId, const struct SOMEIP_OptConfig *pConfig, SOMEIP_SendConfig_Callback_t pSendCB)
{
    bool success = false;
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    uint16_t i;
    SOMEIP_CB_EnterCriticialSection();
    for (i = 0; i < MAX_CLIENT_SERVICES; i++) {
        struct Client_Var *pClient = &m_client[i];
        if (pClient->up_and_configured &&
                (pClient->info.serviceId == serviceId))
        {
            /* Client found */
            uint16_t k;
            for (k = 0; k < MAX_CONNECTIONS_CLIENT; k++) {
                if (pClient->con[k].serviceAvailable && !pClient->con[k].config.sendConfigValid && (pClient->con[k].instanceId == instanceId)) {
                    pClient->con[k].config.sendConfigValid = true;
                    pClient->con[k].config.lastSendTime = 0u;
                    pClient->con[k].config.retries = 0u;
                    pClient->con[k].config.pSendCB = pSendCB;
                    pClient->con[k].config.receivedAck = false;
                    (void)memcpy(&pClient->con[k].config.sendConfig, pConfig, sizeof(struct SOMEIP_OptConfig));
                    success = true;
                }
            }
        }
    }
    SOMEIP_CB_LeaveCriticialSection();
#endif
    return success;
}

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                     LOCAL FUNCTION IMPLEMENTATIONS                   */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

static int32_t GetClientIndexService(const struct SOMEIP_SD_Service *pService, enum SOMEIP_ReturnCode *pResult)
{
    uint32_t i;
    int32_t foundIndex = -1;
    SOMEIP_ASSERT((NULL != pService), __FILE__, __LINE__);
    SOMEIP_ASSERT((NULL != pResult), __FILE__, __LINE__);
    *pResult = SOMEIP_E_OK;
    for (i = 0; (i < MAX_CLIENT_SERVICES) && (-1 == foundIndex); i++) {
        struct Client_Var *pClient = &m_client[i];

        if (pClient->info.serviceId == pService->serviceID) {
            bool success = true;
            foundIndex = i;

            success &= (SOMEIP_SD_SERVICE_MAJOR_VER_ANY == pClient->info.majorVersion)||
                    (SOMEIP_SD_SERVICE_MAJOR_VER_ANY == pService->majorVersion) ||
                    (pClient->info.majorVersion == pService->majorVersion);

            success &= (SOMEIP_SD_SERVICE_MINOR_VER_ANY == pClient->info.minorVersion)||
                    (SOMEIP_SD_SERVICE_MINOR_VER_ANY == pService->minorVersion) ||
                    (pService->minorVersion >= pClient->info.minorVersion);

            *pResult = (success ? SOMEIP_E_OK : SOMEIP_E_NOT_OK);
        }
    }
    return foundIndex;
}

static int32_t GetClientIndexEvent(const struct SOMEIP_SD_Event *pEvent, enum SOMEIP_ReturnCode *pResult)
{
    uint32_t i;
    int32_t foundIndex = -1;
    SOMEIP_ASSERT((NULL != pEvent), __FILE__, __LINE__);
    SOMEIP_ASSERT((NULL != pResult), __FILE__, __LINE__);
    *pResult = SOMEIP_E_OK;
    for (i = 0; (i < MAX_CLIENT_SERVICES) && (-1 == foundIndex); i++) {
        struct Client_Var *pClient = &m_client[i];
        if (pClient->info.serviceId == pEvent->serviceID) {
            bool success = true;
            foundIndex = i;

            success &= (SOMEIP_SD_SERVICE_MAJOR_VER_ANY == pClient->info.majorVersion)||
                    (SOMEIP_SD_SERVICE_MAJOR_VER_ANY == pEvent->majorVersion) ||
                    (pClient->info.majorVersion == pEvent->majorVersion);

            success &= (pClient->info.eventGroupId == pEvent->eventGroupID);

            *pResult = (success ? SOMEIP_E_OK : SOMEIP_E_NOT_OK);
        }
    }
    return foundIndex;
}

static int16_t GetConnectionFromClient(struct Client_Var *pClient, const struct SOMEIP_IpAddr *pRemoteIp)
{
    int16_t index = -1;
    if (pClient && pRemoteIp && pClient->up_and_configured) {
        int16_t k;
        /* Find existing entry */
        for (k = 0; (index < 0) && (k < (int16_t)MAX_CONNECTIONS_CLIENT); k++) {
            if (0 == memcmp(pClient->con[k].receivedAddr.sourceAddr, pRemoteIp->sourceAddr, sizeof(pRemoteIp->sourceAddr))) {
                index = k;
            }
        }
        /* Find free entry */
        for (k = 0; (index < 0) && (k < (int16_t)MAX_CONNECTIONS_CLIENT); k++) {
            if (0 == pClient->con[k].receivedAddr.sourceAddr[0]) {
                (void)memcpy(&pClient->con[k].receivedAddr, pRemoteIp, sizeof(struct SOMEIP_IpAddr));
                index = k;
            }
        }
    }
    return index;
}

static void CheckSessionId(struct ClientConnection_var *pClient, uint16_t receivedSessionId, bool receviedMulticast)
{
    int32_t diff;
    SOMEIP_ASSERT((NULL != pClient), __FILE__, __LINE__);
    uint32_t *pLastSessionId = (receviedMulticast ? &pClient->sessionIdsRx[0] : &pClient->sessionIdsRx[1]);
    if (0u != *pLastSessionId) {
        if (0u == receivedSessionId) {
            diff = 0x10000u - (*pLastSessionId & 0xFFFF);
        } else {
            diff = receivedSessionId - *pLastSessionId;
        }
        if (diff < 0) {
            pClient->receivedOffserService = false;
            pClient->receivedStopOffserService = false;
            pClient->resetDetected = true;
        }
    }
    *pLastSessionId = receivedSessionId;
}

static bool SendService(struct Client_Var *pClient, struct ClientConnection_var *pCon, enum SOMEIP_SD_ServiceEntryType servicType, uint32_t ttl, bool multicast)
{
    bool success = false;
    if (SDServiceType_FindService == servicType) {
        success = true;
    }
    else {
        struct SOMEIP_Server_Client *pInfo;
        struct SOMEIP_SD_Frame par1;
        uint8_t *pBuf = NULL;
        void *pMemTag = NULL;
        uint32_t *pSessionId;
        uint16_t bufLen;
        uint8_t optIndex = 0u;
        bool ipAvailable;
        SOMEIP_ASSERT((NULL != pClient), __FILE__, __LINE__);
        pInfo = &pClient->info;

        (void)memset(&par1, 0u, sizeof(struct SOMEIP_SD_Frame));

        ipAvailable = (SOMEIP_CB_GetLocalIpAddr(pInfo->ipAddr.sourceAddr, pCon->receivedAddr.sourceAddr) && (0u != pInfo->ipAddr.port));
        pSessionId = (multicast) ? &pCon->sessionIdsTx[0] : &pCon->sessionIdsTx[1];
        (*pSessionId) += 1;
        if (0u == ((*pSessionId) & 0xFFFF)) {
            (*pSessionId) += 1;
        }
        par1.someIp.msgType = SOMEIP_SD_MESSAGE_TYPE;
        par1.someIp.retCode = SOMEIP_E_OK;
        par1.someIp.serviceId = SOMEIP_SD_SERVICE_ID;
        par1.someIp.methodId = SOMEIP_Event_ID;
        par1.someIp.clientId = pInfo->clientId;;
        par1.someIp.sessionId = (*pSessionId);
        par1.someIp.interfaceVersion = SOMEIP_SD_INTERFACE_VER;
        par1.someIp.generateEvent = SOMEIP_SD_SERVICE_EVENT;

        par1.someIpSd.reboot = ((*pSessionId) <= 0xFFFFu);
        par1.someIpSd.unicast = true;

        par1.servicesSel[0] = SOMEIP_SD_UnionService;
        par1.services[0].service.type = servicType;
        par1.services[0].service.index1stOpt = 0u;
        par1.services[0].service.index2ndOpt = 0u;
        par1.services[0].service.numberOfOpt1 = 0u;
        par1.services[0].service.numberOfOpt2 = 0u;
        par1.services[0].service.serviceID = pInfo->serviceId;
        par1.services[0].service.instanceID = pInfo->instanceId;
        par1.services[0].service.majorVersion = pInfo->majorVersion;
        par1.services[0].service.ttl = ttl;
        par1.services[0].service.minorVersion = pInfo->minorVersion;

        if (ipAvailable) {
            par1.optionsSel[optIndex] = SOMEIP_SD_UnionOptIpV4;
            par1.options[optIndex].ipV4.ipV4Addr[0] = pInfo->ipAddr.sourceAddr[0];
            par1.options[optIndex].ipV4.ipV4Addr[1] = pInfo->ipAddr.sourceAddr[1];
            par1.options[optIndex].ipV4.ipV4Addr[2] = pInfo->ipAddr.sourceAddr[2];
            par1.options[optIndex].ipV4.ipV4Addr[3] = pInfo->ipAddr.sourceAddr[3];
            par1.options[optIndex].ipV4.portNumber = pInfo->ipAddr.port;
            par1.options[optIndex].ipV4.udp = true;
            par1.services[0].service.numberOfOpt1++;
            optIndex++;
        }
#if (0 != MAX_CONFIG_OPT_ENTRIES)
        if (pCon->config.sendConfigValid && (optIndex < MAX_OPTION_FIELDS)) {
            par1.optionsSel[optIndex] = SOMEIP_SD_UnionOptConfig;
            (void)memcpy(&par1.options[optIndex].config, &pCon->config.sendConfig, sizeof(struct SOMEIP_OptConfig));
            par1.services[0].service.numberOfOpt1++;
            optIndex++;
        }
#endif

        bufLen = Fill_SomeIP_Frame(&pBuf, &pMemTag, &par1);
        if (0u != bufLen) {
            success = SOMEIP_CB_SendUdp(pBuf, bufLen, pMemTag, ((multicast) ? MULTICAST_IP : pCon->receivedAddr.sourceAddr), SD_PORT, pClient->sockHandle);
        }
    }
    return success;
}

static bool SendEvent(struct Client_Var *pClient, struct ClientConnection_var *pCon, enum SOMEIP_CB_EventEntryType eventType, uint32_t ttl, uint8_t counter, uint16_t eventGroupID, bool multicast)
{
    struct SOMEIP_Server_Client *pInfo;
    struct SOMEIP_SD_Frame par1;
    uint8_t *pBuf = NULL;
    void *pMemTag = NULL;
    uint32_t *pSessionId;
    uint16_t bufLen;
    bool ipAvailable;
    bool success = false;

    SOMEIP_ASSERT((NULL != pClient), __FILE__, __LINE__);
    pInfo = &pClient->info;

    (void)memset(&par1, 0u, sizeof(struct SOMEIP_SD_Frame));

    ipAvailable = (SOMEIP_CB_GetLocalIpAddr(pInfo->ipAddr.sourceAddr, pCon->receivedAddr.sourceAddr) && (0u != pInfo->ipAddr.port));
    pSessionId = (multicast) ? &pCon->sessionIdsTx[0] : &pCon->sessionIdsTx[1];
    (*pSessionId) += 1;
    if (0u == ((*pSessionId) & 0xFFFF)) {
        (*pSessionId) += 1;
    }
    par1.someIp.msgType = SOMEIP_SD_MESSAGE_TYPE;
    par1.someIp.retCode = SOMEIP_E_OK;
    par1.someIp.serviceId = SOMEIP_SD_SERVICE_ID;
    par1.someIp.methodId = SOMEIP_Event_ID;
    par1.someIp.clientId = pInfo->clientId;
    par1.someIp.sessionId = (*pSessionId);
    par1.someIp.interfaceVersion = SOMEIP_SD_INTERFACE_VER;
    par1.someIp.generateEvent = SOMEIP_SD_SERVICE_EVENT;

    par1.someIpSd.reboot = ((*pSessionId) <= 0xFFFFu);
    par1.someIpSd.unicast = true;

    par1.servicesSel[0] = SOMEIP_SD_UnionEvent;
    par1.services[0].event.type = eventType;
    par1.services[0].event.index1stOpt = 0;
    par1.services[0].event.index2ndOpt = 0;
    par1.services[0].event.numberOfOpt1 = (ipAvailable ? 1u : 0u);
    par1.services[0].event.numberOfOpt2 = 0;
    par1.services[0].event.serviceID = pInfo->serviceId;
    par1.services[0].event.instanceID = pCon->instanceId;
    par1.services[0].event.majorVersion = pInfo->majorVersion;
    par1.services[0].event.ttl = ttl;
    par1.services[0].event.counter = counter;
    par1.services[0].event.eventGroupID = eventGroupID;

    if (ipAvailable) {
        par1.optionsSel[0] = SOMEIP_SD_UnionOptIpV4;
        par1.options[0].ipV4.ipV4Addr[0] = pInfo->ipAddr.sourceAddr[0];
        par1.options[0].ipV4.ipV4Addr[1] = pInfo->ipAddr.sourceAddr[1];
        par1.options[0].ipV4.ipV4Addr[2] = pInfo->ipAddr.sourceAddr[2];
        par1.options[0].ipV4.ipV4Addr[3] = pInfo->ipAddr.sourceAddr[3];
        par1.options[0].ipV4.portNumber = pInfo->ipAddr.port;
        par1.options[0].ipV4.udp = true;
    }

    bufLen = Fill_SomeIP_Frame(&pBuf, &pMemTag, &par1);
    if (0u != bufLen) {
        success = SOMEIP_CB_SendUdp(pBuf, bufLen, pMemTag, ((multicast) ? MULTICAST_IP : pCon->receivedAddr.sourceAddr), SD_PORT, pClient->sockHandle);
    }
    return success;
}

static bool FindRemoteIp(const struct SOMEIP_SD_Frame *pFrame, struct SOMEIP_IpAddr *pRemoteIp)
{
    uint16_t i;
    bool success = false;
    SOMEIP_ASSERT((NULL != pFrame), __FILE__, __LINE__);
    SOMEIP_ASSERT((NULL != pRemoteIp), __FILE__, __LINE__);

    for (i = 0; !success && (i < MAX_OPTION_FIELDS); i++) {
        switch(pFrame->optionsSel[i]) {
        case SOMEIP_SD_UnionOptIpV4:
            (void)memcpy(pRemoteIp->sourceAddr, pFrame->options[i].ipV4.ipV4Addr, SOMEIP_IPV4_ADDR_LEN);
            pRemoteIp->port = pFrame->options[i].ipV4.portNumber;
            success = true;
            break;
        case SOMEIP_SD_UnionOptIpV4SD:
            (void)memcpy(pRemoteIp->sourceAddr, pFrame->options[i].ipV4Sd.ipV4Addr, SOMEIP_IPV4_ADDR_LEN);
            pRemoteIp->port = pFrame->options[i].ipV4Sd.portNumber;
            success = true;
            break;
        case SOMEIP_SD_UnionOptNone:
        case SOMEIP_SD_UnionOptIpV4MCast:
        default:
            break;
        }
    }
    return success;
}

static void HandleReceivedConfig(const struct SOMEIP_SD_Frame *pFrame, struct Client_Var *pClient)
{
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    uint16_t i;
    uint16_t k;
    SOMEIP_ASSERT((NULL != pFrame), __FILE__, __LINE__);
    SOMEIP_ASSERT((NULL != pClient), __FILE__, __LINE__);
    for (k = 0u; k < MAX_CONNECTIONS_CLIENT; k++) {
        for (i = 0u; (!pClient->con[k].config.receivedConfigValid) && (i < MAX_OPTION_FIELDS); i++) {
            if (SOMEIP_SD_UnionOptConfig == pFrame->optionsSel[i]) {
                (void)memcpy(&pClient->con[k].config.receivedConfig, &pFrame->options[i].config, sizeof(struct SOMEIP_OptConfig));
                pClient->con[k].config.receivedConfigValid = true;
            }
        }
    }
#endif
}
