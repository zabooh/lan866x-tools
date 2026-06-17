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
/* SOME/IP for embedded systems                                                                   */
/*------------------------------------------------------------------------------------------------*/

#ifndef SOMEIP_H
#define SOMEIP_H

// *****************************************************************************
// *****************************************************************************
// Section: File includes
// *****************************************************************************
// *****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "someip-cfg.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus   // Provide C++ Compatibility
extern "C" {
#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Constants
// *****************************************************************************
// *****************************************************************************

#define SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN     (1440u)

// *****************************************************************************
// *****************************************************************************
// Section: Data Types
// *****************************************************************************
// *****************************************************************************

// DOM-IGNORE-BEGIN
/* Forward declaration of struct SOMEIP_Server_Client, will be defined later*/
struct SOMEIP_Server_Client;
// DOM-IGNORE-END

// *****************************************************************************
/* IP Version 4 address length

  Summary:
    Definition of IP V4 address length, measured in Bytes.

  Description:
    4 Byte (32 Bit) address length definition for API purpose.

  Remarks:
    None.
*/

// *****************************************************************************
/* Enumeration for parser results.

  Summary:
    Definition of all possible parser return values.

  Description:
    Parser functions will return this enumeration, defined by the SOME/IP spec.

  Remarks:
    None.
*/

enum SOMEIP_ReturnCode
{
    SOMEIP_E_OK = 0x00,                            /** No error occurred */
    SOMEIP_E_NOT_OK = 0x01,                        /** An unspecified error occurred */
    SOMEIP_E_UNKNOWN_SERVICE = 0x02,               /** The requested Service ID is unknown */
    SOMEIP_E_UNKNOWN_METHOD = 0x03,                /** The requested Method ID is unknown. Service ID is known.*/
    SOMEIP_E_NOT_READY = 0x04,                     /** Service ID and Method ID are known. Application not running. */
    SOMEIP_E_NOT_REACHABLE = 0x05,                 /** System running the service is not reachable (internal error code only) */
    SOMEIP_E_TIMEOUT = 0x06,                       /** A timeout occurred (internal error code only) */
    SOMEIP_E_WRONG_PROTOCOL_VERSION = 0x07,        /** Version of SOME/IP not supported */
    SOMEIP_E_WRONG_INTERFACE_VERSION = 0x08,       /** Interface version mismatch */
    SOMEIP_E_MALFORMED_MESSAGE = 0x09,             /** Deserialization error, so that payload cannot be deserialized */
    SOMEIP_E_WRONG_MESSAGE_TYPE = 0x0A,            /** An unexpected message type was received */
    SOMEIP_E_E2E_REPEATED = 0x0B,                  /** Repeated E2E calculation error */
    SOMEIP_E_E2E_WRONG_SEQUENCE = 0x0C,            /** Wrong E2E sequence error */
    SOMEIP_E_E2E = 0x0D,                           /** Not further specified E2E error */
    SOMEIP_E_E2E_NOT_AVAILABLE = 0x0E,             /** E2E not available */
    SOMEIP_E_E2E_NO_NEW_DATA = 0x0F                /** No new data for E2E calculation present */
};

#define SOMEIP_IPV4_ADDR_LEN (4u)

struct SOMETR_t;

typedef struct SOMETR_t SOMETR_t;

struct SOMEIP_Transmit_Buffer;

typedef void (*SOMEIP_Transmit_CB_t)(struct SOMEIP_Transmit_Buffer *pBuf, bool success, enum SOMEIP_ReturnCode retCode, const uint8_t *pRxBuf, uint16_t rxBufLen);

struct SOMEIP_Transmit_Buffer
{
    uint8_t payload[SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN];
    SOMEIP_Transmit_CB_t callback;
    void *callbackTag;
    uint32_t sendTime; /* Will be set internally */
    uint8_t ipV4Addr[4];
    uint16_t payloadLength;
    uint16_t udpPort;
    uint16_t waitForSessionId;
    bool fireAndForget;
};


// *****************************************************************************
/* Source and Destination IP address and port structure

  Summary:
    Structure to hold IP addresses both from source and destination and the used
    port.

  Description:
    This structure is used to provide IP addresses and port to the SOME/IP
    component.

  Remarks:
    None.
*/

struct SOMEIP_IpAddr
{
    uint8_t sourceAddr[SOMEIP_IPV4_ADDR_LEN];   /** The address of the source (sender) */
    uint8_t destinAddr[SOMEIP_IPV4_ADDR_LEN];   /** The address of the destination (this machine), used to distinguish between Unicast, Broadcast and Multicast */
    uint16_t port;                              /** The TCP or UDP port used (from the source) */
};


// *****************************************************************************
/* Structure to store key / value pairs to transmit with a config option.

  Summary:
    Structure for passing key / value pairs to the SOMEIP_Client_SendConfig()
    function.

  Description:
    In order to remotely configure a SOME/IP node, an array of key value pairs
    can be sent over the network.

  Remarks:
    This structure is only useable when the MAX_CONFIG_OPT_ENTRIES is set to values
    bigger than zero.
*/

struct SOMEIP_OptConfig
{
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    char key[MAX_CONFIG_OPT_ENTRIES][MAX_CONFIG_OPT_KEY_LEN]; /** Array of zero terminated strings representing the keys */
    char val[MAX_CONFIG_OPT_ENTRIES][MAX_CONFIG_OPT_VAL_LEN]; /** Array of zero terminated strings representing the values */
#else
    char key[1][1]; /* Dummy, just to keep API alive */
    char val[1][1]; /* Dummy, just to keep API alive */
#endif
    uint16_t pairCount; /** Amount of valid key / value pairs */
};


// *****************************************************************************
/* Enumeration to distinguish the reason for the SOMEIP_Event_Callback_t callback

  Summary:
    This enumeration is given back with the SOMEIP_Event_Callback_t callback,
    which was registered in the struct SOMEIP_Server_Client and then given to
    SOMEIP_Server_AddService() function.

  Description:
    This enumeration can be used to trigger next actions, such as send initial
    configuration and initial values or to stop the application.

  Remarks:
    None.
*/

enum SOMEIP_CB_Event
{
    EV_SERVER_RECEIVED_FIND,                /** Server received find. That means that client newly arrived or went through reset */
    EV_SERVER_SUBSCRIBE_INITIAL,            /** Server received Subscribe event. This is initial event, so sent out initial events now */
    EV_SERVER_REMOTE_RESET_DETECTED,        /** Server detected that client went through reset */
    EV_SERVER_STOP_SUBSCRIBE,               /** Server received Stub Subscribe event. Stop sending any events */
    EV_SERVER_TIMEOUT_CLIENT,               /** Server did not receive any message from client for a time longer as configured in TTL. Stop sending any events */
    EV_CLIENT_SERVICE_AVAILABLE,            /** Client sees server available. Service might be used now */
    EV_CLIENT_SERVICE_STOPPED,              /** Client does not see server anymore. Service is unavailable now */
    EV_CLIENT_REMOTE_RESET_DETECTED,        /** Client detected that server went through reset */
    EV_CLIENT_EVENT_SUBSCRIBED,             /** Client did successfully subscribe to the given event */
    EV_CLIENT_EVENT_NOT_SUBSCRIBED,         /** Client is no longer subscribed to the given event */
};


// *****************************************************************************
/* Function:
    void (*SOMEIP_Event_Callback_t)
    (
        enum SOMEIP_CB_Event evnt,
        struct SOMEIP_Server_Client *pServerClient,
        struct SOMEIP_IpAddr *pIpAddr,
        struct SOMEIP_OptConfig *pConfig
    )

  Summary:
    Callback function type definition for SOME/IP events.

  Description:
    This function type definition can be used to implement an callback and
    register it dynamically along with the SOMEIP_Client_AddService() or
    SOMEIP_Server_AddService.

  Precondition:
    SOMEIP_Client_AddService() or SOMEIP_Server_AddService must have been be
    called in first. And the implemented callback (compatible to this definition)
    must have been registered to the struct SOMEIP_Server_Client.

  Parameters:
    evnt - The reason why this event was raised.

    pIpAddr - The IP address and port informations of the events source.

    pConfig - If there was a configuration option field set, then this pointer
    is pointing to an array of key / value pairs with zero terminated strings.
    Otherwise, this parameter will be a NULL pointer.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to implement a event
    // callback and register it to the SOME/IP server or client

    static void OnSomeIpEvent(enum SOMEIP_CB_Event evnt, struct SOMEIP_Server_Client *pServerClient,
                  struct SOMEIP_IpAddr *pIp, struct SOMEIP_OptConfig *pConfig)
    {
        switch(evnt) {
            case EV_SERVER_RECEIVED_FIND:
                printf("Server: Received Find");
                break;
            case EV_SERVER_SUBSCRIBE_INITIAL:
                printf("Server: Received Subcribe (initial)");
                break;
            case EV_SERVER_REMOTE_RESET_DETECTED:
                printf("Server: Detected Client Reset");
                break;
            case EV_SERVER_STOP_SUBSCRIBE:
                printf("Server: Received Stop Subscribe");
                break;
            case EV_SERVER_TIMEOUT_CLIENT:
                printf("Server: Timeout of Client");
                break;
            case EV_CLIENT_SERVICE_AVAILABLE:
                printf("Client: Service is available");
                break;
            case EV_CLIENT_SERVICE_STOPPED:
                printf("Client: Service not available");
                break;
            case EV_CLIENT_REMOTE_RESET_DETECTED:
                printf("Client: Detected Server Reset");
                break;
            case EV_CLIENT_EVENT_SUBSCRIBED:
                printf("Client: Event is subscribed");
                break;
            case EV_CLIENT_EVENT_NOT_SUBSCRIBED:
                printf("Client: Event is no longer subscribed");
                break;
            default:
                printf("Unknown SOMEIP event");
                ASSERT(false);
                break;
        }
        if (NULL != pIp) {
            printf(", ServiceId=0x%X (%d), IP=%d.%d.%d.%d, Port=%d\r\n", pServerClient->serviceId, pServerClient->serviceId,
                pIp->sourceAddr[0], pIp->sourceAddr[1], pIp->sourceAddr[2], pIp->sourceAddr[3], pIp->port);
        } else {
            printf(", ServiceId=0x%X (%d), unknown IP address\r\n", pServerClient->serviceId, pServerClient->serviceId);
        }
        if (NULL != pConfig) {
            uint16_t i;
            for (i = 0; i < pConfig->pairCount; i++) {
                printf("[%d]'%s'='%s'\r\n", i, pConfig->key[i], pConfig->val[i]);
            }
        }
    }

   bool SomeIPSocket_Init(const uint8_t serverIP[4])
   {
       bool success;
       struct SOMEIP_Server_Client service;
       service.eventHandlingEnabled = true;
       service.eventGroupId = 0xABBA;
       service.ttl = 5;
       service.minorVersion = 1;
       service.clientId = 0xAFFE;
       service.serviceId = SERVICE_ID_1;
       service.instanceId = INSTANCE_ID1;
       service.majorVersion = 1;
       service.pEventCb = OnSomeIpEvent;
       service.ipAddr.port = 54321;
       memcpy(&service.ipAddr.sourceAddr, serverIP, 4);

       success = SOMEIP_Server_AddService(&service, true);
       success &= SOMEIP_Server_AddService(&service, true);
       return success;
   }

    </code>

  Remarks:
    All the structures will be invalid, once the callback is returning. Make
    sure to copy the data, if needed for a longer time period.
*/

typedef void (*SOMEIP_Event_Callback_t)(enum SOMEIP_CB_Event evnt, struct SOMEIP_Server_Client *pServerClient,
              struct SOMEIP_IpAddr *pIpAddr, uint16_t receivedInstanceId, struct SOMEIP_OptConfig *pConfig, void* eventData);


// *****************************************************************************
/* Function:
    void (*SOMEIP_SendConfig_Callback_t)
        (uint16_t serviceId,
        uint16_t instanceId,
        struct SOMEIP_OptConfig *pConfig,
        bool success
    );

  Summary:
    Callback function type definition for the SOMEIP_Client_SendConfig().

  Description:
    This function type definition can be used to implement an callback and
    register it dynamically along with the SOMEIP_Client_SendConfig function.

  Precondition:
    SOMEIP_Client_SendConfig() with a pointer to this function must have been
    called.

  Parameters:
    serviceId - The service identifier, as provided while calling
    SOMEIP_Client_SendConfig().

    instanceId - The instance identifier, as provided while calling
    SOMEIP_Client_SendConfig().

    pConfig - Pointer to the array of key / value pairs as given with the
    SOMEIP_Client_SendConfig() function. Pointer will be invalid after callback
    returns.

    success - Is set to true, if the configuration was successful sent and the
    receiver send back an acknowledge. false, all retries timed out.
    Configuration was not applied to remote system.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to implement a event
    // callback and register it to the SOME/IP server or client

    static void OnSendConfigEvent(uint16_t serviceId, uint16_t instanceId, struct SOMEIP_OptConfig *pConfig, bool success)
    {
        printf("Send config callback, serviceId=%d instanceId=%d configLen=%d success=%d\r\n", serviceId, instanceId, pConfig->pairCount, success);
    }

    void SomeIPSocket_SendConfig(void)
    {
        struct SOMEIP_OptConfig config;
        uint16_t i;
        config.pairCount = 0;
        for (i = 0u; i < MAX_CONFIG_OPT_ENTRIES; i++) {
            snprintf(config.key[i], MAX_CONFIG_OPT_KEY_LEN, "Key[%d]", i);
            snprintf(config.val[i], MAX_CONFIG_OPT_VAL_LEN, "Value[%d]", i);
            config.pairCount++;
        }

        if (!SOMEIP_Client_SendConfig(SERVICE_ID_1, INSTANCE_ID1, &config, OnSendConfigEvent)) {
            printf("SOMEIP_Client_SendConfig() failed\r\n");
        }
    }

    </code>

  Remarks:
    None.
*/

typedef void (*SOMEIP_SendConfig_Callback_t)(uint16_t serviceId, uint16_t instanceId, const struct SOMEIP_OptConfig *pConfig, bool success);


// *****************************************************************************
/* Function:
    enum SOMEIP_ReturnCode (*SOMEIP_DataReceived_CB_t)
    (
        const uint8_t *b,
        uint16_t bLen,
        struct SOMEIP_IpAddr *pIpAddr
    )

  Summary:
    This callback function used to pass received UDP/TCP data from socket
	receiver back to the SOME/IP server / client / transmitter.

  Description:
    Whenever SOME/IP related data was received, this function will to be
	called back. The data is getting analyzed and parsed.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to an array with the received SOME/IP datagrams. Note,
    the Ethernet, IP, UDP/TCP headers should already be stripped away by the
    underlying TCP/IP stack. So the first byte of this Byte array is the first
    Byte of the SOME/IP message. The payload is getting deep copied if needed,
    so the memory of this pointer might me reused directly after the call of
    this function.

    bufLen - The length of the Byte array given with pBuf measured in Bytes.

    pIpAddr - Optional (but highly recommended) parameter. Passes the source
    and destination IP address along with the remote port to client. This is
    mainly helpful to distinguish between Unicast / Broadcast and Multicast
    messages. The payload is getting deep copied if needed, so the memory of
    this pointer might me reused directly after the call of this function. If
    the used TCP/IP does not provide that IP-related informations, pass NULL.

  Returns:
    enum SOMEIP_ReturnCode - Result of the parsing of the given data. Anything
    else than SOMEIP_E_OK can be treated as an error.

  Example:
    <code>
    // The following code snippet shows an example how to pass received data to
    // the SOME/IP client

    while(true) {
        static uint8_t udpData[1500];  //Do not kill your memory stack ;-)
        uint16_t dataLength = 0;
        enum SOMEIP_ReturnCode result;
        struct SOMEIP_IpAddr remoteIP;

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(udpData, &dataLength, &remoteIP);

        // Pass the received data to SOME/IP server component
        result = SOMEIP_Client_DataReceived(udpData, dataLength, &remoteIP);
        if (SOMEIP_E_OK != result) {
            // TODO: DO error handling
        }
    }

    </code>

  Remarks:
    This function parses the given data and return the result back instantly.
    However, it will not raise any reaction out of this function call. This will
    be done earliest in the next call to SOMEIP_Client_CheckTimers().
*/

typedef enum SOMEIP_ReturnCode (*SOMEIP_DataReceived_CB_t)(const uint8_t *b, uint16_t bLen, struct SOMEIP_IpAddr *pIpAddr, void* rxTag);


// *****************************************************************************
/* Main configuration structure both for Client and Server.

  Summary:
    Configuration structure to set all the addresses, versions and callback for
    SOME/IP server and client component.

  Description:
    Main configuration structure to hold all addresses, version, identifier,
    callback. It works both for method are events, just by toggling the
    eventHandlingEnabled flag.

  Remarks:
    All functions using this structure will deep copy all entries. So integrator
    can allocate this structure temporary on the stack.
*/

struct SOMEIP_Server_Client
{
    SOMEIP_Event_Callback_t pEventCb;       /** Optional pointer to callback function. May be set to NULL, if no callback is required */
    struct SOMEIP_IpAddr ipAddr;            /** Optional pointer to IP address structure. Only the source address and the port need to be set. May be set to NULL.  */
    uint32_t ttl;                           /** Time to live value in seconds (SOME/IP SD Field) */
    uint32_t minorVersion;                  /** The minor version of this method or event. (SOME/IP SD Field) */
    uint16_t clientId;                      /** Unique ID for the calling client inside the ECU. Allows differentiate calls from multiple clients for the same method. Client ID together with the Session ID (automatic incremented value) are forming the Request ID */
    uint16_t serviceId;                     /** Unique Service ID inside whole system  */
    uint16_t instanceId;                    /** The instance ID field. (SOME/IP SD Field)  */
    uint16_t eventGroupId;                  /** Identifier for event group. Only needed in case eventHandlingEnabled is set to true. Other, is don't care. */
    uint8_t majorVersion;                   /** The major version of this method or event. (SOME/IP SD Field) */
    bool eventHandlingEnabled;              /** true, generates an event. false, generates a method */
    void* cbData;
};


// *****************************************************************************
/* SOME/IP Message type selector.

  Summary:
    Enumeration to select the type of SOME/IP message.

  Description:
    This enumeration is embedded in the struct SOMEIP_Header., It can be used
    to generate a start ofSOME/IP frame. Use the SOMEIP_Generator_Fill_Header()
    therefor.

  Remarks:
    None.
*/

enum SOMEIP_MsgType
{
    MSGTYPE_REQUEST = 0x00,                 /** A request expecting a response (even void) */
    MSGTYPE_REQUEST_NO_RETURN = 0x01,       /** A fire & forget request */
    MSGTYPE_NOTIFICATION = 0x02,            /** A request of a notification/Event callback expecting no response */
    MSGTYPE_RESPONSE = 0x80,                /** The response message */
    MSGTYPE_ERROR = 0x81,                   /** The response containing an error */
    MSGTYPE_TP_REQUEST = 0x20,              /** A TP request expecting a response (even void) */
    MSGTYPE_TP_REQUEST_NO_RETURN = 0x21,    /** A TP fire & forget request */
    MSGTYPE_TP_NOTIFICATION = 0x22,         /** A TP request of a notification / Event callback expecting no response */
    MSGTYPE_TP_RESPONSE = 0xA0,             /** The TP response message */
    MSGTYPE_TP_ERROR = 0xA1                 /** The response containing an error */
};


// *****************************************************************************
/* Structure to hold all informations to fill a SOME/IP header.

  Summary:
    With this structure all fields of an SOME/IP frame can be filled.

  Description:
    To be used together with the SOMEIP_Generator_Fill_Header() function.

  Remarks:
    None.
*/

struct SOMEIP_Header
{
    uint32_t length;                /** Length in Byte starting from Request ID / Client ID until the end of the SOMEIP message */
    enum SOMEIP_MsgType msgType;    /** Used to differentiate different types of messages */
    enum SOMEIP_ReturnCode retCode; /** Signals whether a request was successfully processed. For simplification of the header layout, every message transports the field Return Code */
    uint16_t serviceId;             /** Unique Service ID inside whole system  */
    uint16_t methodId;              /** ID identify the method or the event (see eventHandlingEnabled flag) */
    uint16_t clientId;              /** Unique Client ID for the calling client side the ECU. Allows ECU to differentiate calls from multiple clients to the same method. */
    uint16_t sessionId;             /** Unique Session ID that allows to distinguish sequential messages or requests originating from the the same sender from each other. */
    uint8_t interfaceVersion;       /** Contains the Major Version of the Service Interface */
    bool generateEvent;             /** true, generates an event. false, generates a method */
};

// *****************************************************************************
// *****************************************************************************
// Section: Public API for SOME/IP Server
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Function:
    bool SOMEIP_Server_AddService
    (
        const struct SOMEIP_Server_Client *pServerInfo,
        bool serviceUp
    )

  Summary:
    Enables the SOME/IP server and register a new service / event.

  Description:
    This routine enables the SOME/IP server and add the given service or event
    to the internal list. Check the MAX_SERVER_SERVICES definition in the config
    file to adjust the amount of maximum possible service / events.

  Precondition:
    None.

  Parameters:
    pServerInfo - Pointer to a structure with all needed informations to run the
    service or event. The given structure will be deep copied inside this
    function. So user can temporarily allocate the structure from the stack and
    hand it over to this function.

    serviceUp - true, if the given service / event is up and running. false, if
    it should be added but not yet running.

  Returns:
    If successful, returns true.
    Otherwise, returns false, you might need to increase MAX_SERVER_SERVICES in
    the configuration file.

  Example:
    <code>
    // The following code snippet shows an example how to add a service or event
    // to the server.

    struct SOMEIP_Server_Client service;
    service.eventHandlingEnabled = true;
    service.eventGroupId = 0xABBA;
    service.ttl = 5;
    service.minorVersion = 1;
    service.clientId = 0xAFFE;
    service.serviceId = 0xBEAF;
    service.instanceId = 1;
    service.majorVersion = 1;
    service.pEventCb = NULL;
    service.ipAddr.port = 54321;
    service.ipAddr.sourceAddr[0] = 192;
    service.ipAddr.sourceAddr[1] = 168;
    service.ipAddr.sourceAddr[2] = 0;
    service.ipAddr.sourceAddr[3] = 100;

    if (!SOMEIP_Server_AddService(&service, true)) {
        // TODO: DO error handling
    }

    </code>

  Remarks:
    This routine might be called multiple times in order to register multiple
    services or events.
*/

bool SOMEIP_Server_AddService(const struct SOMEIP_Server_Client *pServerInfo, bool serviceUp);


// *****************************************************************************
/* Function:
    bool SOMEIP_Server_SetServiceStatus
    (
        uint16_t serviceId,
        uint16_t instanceId,
        bool serviceUp
    )

  Summary:
    Sets the status of a given service / event to available or unavailable for a
    server.

  Description:
    A service or event might be temporarily set to unavailable for some reasons.
    If so calling this function,  let the server tell to the client that the
    service / event is currently down. It might then be enabled later on again.

  Precondition:
    SOMEIP_Server_AddService() must have been be called in front.

  Parameters:
    serviceId - The service identifier, as provided in the struct SOMEIP_Server_Client
    while calling SOMEIP_Server_AddService().

    instanceId - The instance identifier, as provided in the struct SOMEIP_Server_Client
    while calling SOMEIP_Server_AddService().

    serviceUp - true, if the given service / event is up and running.
    false, service or event is temporarily unavailable.

  Returns:
    If successful, returns true.
    Otherwise, returns false, did not find a valid entry with the given
    serviceId and instanceId.

  Example:
    <code>
    // The following code snippet shows an example how to temporarily disable
    and reenable a service or event.

    struct SOMEIP_Server_Client service;
    service.eventHandlingEnabled = true;
    service.eventGroupId = 0xABBA;
    service.ttl = 5;
    service.minorVersion = 1;
    service.clientId = 0xAFFE;
    service.serviceId = 0xBEAF;
    service.instanceId = 1;
    service.majorVersion = 1;
    service.pEventCb = NULL;
    service.ipAddr.port = 54321;
    service.ipAddr.sourceAddr[0] = 192;
    service.ipAddr.sourceAddr[1] = 168;
    service.ipAddr.sourceAddr[2] = 0;
    service.ipAddr.sourceAddr[3] = 100;

    if (!SOMEIP_Server_AddService(&service, true)) {
        // TODO: DO error handling
    }

    // Disable service or event
    SOMEIP_Server_SetServiceStatus(service.serviceId, service.instanceId, false);

    // Spend some time somewhere else

    // Enable service or event
    SOMEIP_Server_SetServiceStatus(service.serviceId, service.instanceId, true);

    </code>

  Remarks:
    Disable an already disabled service or event does is getting ignored. The
    same applies for enabling an already enabled service or event.
*/

bool SOMEIP_Server_SetServiceStatus(uint16_t serviceId, uint16_t instanceId, bool serviceUp);


// *****************************************************************************
/* Function:
    void SOMEIP_Server_CheckTimers
    (
        void
    )

  Summary:
    This function is giving processor time to the internal state machines of the
    server.

  Description:
     All registered services or events are getting handled inside this function.
     Mainly all the callbacks from the SOME/IP server module are getting called
     within this function.

  Precondition:
    SOMEIP_Server_AddService() must have been be called in front.

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to cyclic service the
    // SOME/IP Server module

    while(true) {
       SOMEIP_Server_CheckTimers();
       //Sleep for some milliseconds
    }

    </code>

  Remarks:
    The cycle time of calling this function is also the smallest reaction time
    of the SOME/IP server. For example, calling this function every 5ms, the
    server is able to respond to an incoming SOME/IP message within this
    particular 5ms.
*/

void SOMEIP_Server_CheckTimers(void);


// *****************************************************************************
/* Function:
    enum SOMEIP_ReturnCode SOMEIP_Server_DataReceived
    (
        const uint8_t *b,
        uint16_t bLen,
        struct SOMEIP_IpAddr *pIpAddr
    )

  Summary:
    This function is passing received UDP/TCP data to the server.

  Description:
    Whenever SOME/IP server related data (SOME/IP-SD-Port) was received, this
    function needs to be called. The data is getting analyzed and parsed.

  Precondition:
    SOMEIP_Server_AddService() must have been be called in front.

  Parameters:
    pBuf - Pointer to an array with the received SOME/IP datagrams. Note,
    the Ethernet, IP, UDP/TCP headers should already be stripped away by the
    underlying TCP/IP stack. So the first byte of this Byte array is the first
    Byte of the SOME/IP message. The payload is getting deep copied if needed,
    so the memory of this pointer might me reused directly after the call of
    this function.

    bufLen - The length of the Byte array given with pBuf measured in Bytes.

    pIpAddr - Optional (but highly recommended) parameter. Passes the source
    and destination IP address along with the remote port to server. This is
    mainly helpful to distinguish between Unicast / Broadcast and Multicast
    messages. The payload is getting deep copied if needed, so the memory of
    this pointer might me reused directly after the call of this function. If
    the used TCP/IP does not provide that IP-related informations, pass NULL.

  Returns:
    enum SOMEIP_ReturnCode - Result of the parsing of the given data. Anything
    else than SOMEIP_E_OK can be treated as an error.

  Example:
    <code>
    // The following code snippet shows an example how to pass received data to
    // the SOME/IP server

    while(true) {
        static uint8_t udpData[1500];  //Do not kill your memory stack ;-)
        uint16_t dataLength = 0;
        enum SOMEIP_ReturnCode result;
        struct SOMEIP_IpAddr remoteIP;

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(udpData, &dataLength, &remoteIP);

        // Pass the received data to SOME/IP server component
        result = SOMEIP_Server_DataReceived(udpData, dataLength, &remoteIP);
        if (SOMEIP_E_OK != result) {
            // TODO: DO error handling
        }
    }

    </code>

  Remarks:
    This function parses the given data and return the result back instantly.
    However, it will not raise any reaction out of this function call. This will
    be done earliest in the next call to SOMEIP_Server_CheckTimers().
*/

enum SOMEIP_ReturnCode SOMEIP_Server_DataReceived(const uint8_t *pBuf, uint16_t bufLen, struct SOMEIP_IpAddr *pIpAddr, void *rxTag);


// *****************************************************************************
// *****************************************************************************
// Section: Public API for SOME/IP Client
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
/* Function:
    bool SOMEIP_Client_AddService
    (
        const struct SOMEIP_Server_Client *pClientInfo,
        bool requested
    )

  Summary:
    Enables the SOME/IP client and register a new service / event.

  Description:
    This routine enables the SOME/IP client and add the given service or event
    to the internal list. Check the MAX_CLIENT_SERVICES definition in the config
    file to adjust the amount of maximum possible service / events.

  Precondition:
    None.

  Parameters:
    pClientInfo - Pointer to a structure with all needed informations to run the
    service or event. The given structure will be deep copied inside this
    function. So user can temporarily allocate the structure from the stack and
    hand it over to this function.

    requested - true, if the given service / event is of interest. false, tell
    the server, that this client currently not interested in receiving events.

    subscribeToEvent - true, if the client shall subscribe to events. false, only requests are working.

  Returns:
    If successful, returns true.
    Otherwise, returns false, you might need to increase MAX_CLIENT_SERVICES in
    the configuration file.

  Example:
    <code>
    // The following code snippet shows an example how to add a service or event
    // to the client.

    struct SOMEIP_Server_Client service;
    service.eventHandlingEnabled = true;
    service.eventGroupId = 0xABBA;
    service.ttl = 5;
    service.minorVersion = 1;
    service.clientId = 0xAFFE;
    service.serviceId = 0xBEAF;
    service.instanceId = 1;
    service.majorVersion = 1;
    service.pEventCb = NULL;
    service.ipAddr.port = 54321;
    service.ipAddr.sourceAddr[0] = 192;
    service.ipAddr.sourceAddr[1] = 168;
    service.ipAddr.sourceAddr[2] = 0;
    service.ipAddr.sourceAddr[3] = 100;

    if (!SOMEIP_Client_AddService(&service, true, true)) {
        // TODO: DO error handling
    }

    </code>

  Remarks:
    This routine might be called multiple times in order to register multiple
    services or events.
*/

bool SOMEIP_Client_AddService(const struct SOMEIP_Server_Client *pClientInfo, bool requested, bool subscribeToEvent);


// *****************************************************************************
/* Function:
    bool SOMEIP_Client_SetServiceRequested
    (
        uint16_t serviceId,
        uint16_t instanceId,
        bool requested
    )

  Summary:
    Sets the requested state of a given service / event.

  Description:
    This function can be used to tell server that this client is temporarily not
    interested in its service / event.

  Precondition:
    SOMEIP_Server_AddService() must have been be called in front.

  Parameters:
    serviceId - The service identifier, as provided in the struct SOMEIP_Server_Client
    while calling SOMEIP_Client_AddService().

    requested - true, if the given service / event is from interest.
    false, service or event is not from interest, server should stop servicing.

  Returns:
    If successful, returns true.
    Otherwise, returns false, did not find a valid entry with the given
    serviceId and instanceId.

  Example:
    <code>
    // The following code snippet shows an example how to temporarily toggle the
    requested state of a service or event on a client.

    struct SOMEIP_Server_Client service;
    service.eventHandlingEnabled = true;
    service.eventGroupId = 0xABBA;
    service.ttl = 5;
    service.minorVersion = 1;
    service.clientId = 0xAFFE;
    service.serviceId = 0xBEAF;
    service.instanceId = 1;
    service.majorVersion = 1;
    service.pEventCb = NULL;
    service.ipAddr.port = 54321;
    service.ipAddr.sourceAddr[0] = 192;
    service.ipAddr.sourceAddr[1] = 168;
    service.ipAddr.sourceAddr[2] = 0;
    service.ipAddr.sourceAddr[3] = 100;

    if (!SOMEIP_Client_AddService(&service, true, true)) {
        // TODO: DO error handling
    }

    // Do not longer request service or event
    SOMEIP_Client_SetServiceRequested(service.serviceId, service.instanceId, false);

    // Spend some time somewhere else

    // Requesting again service or event
    SOMEIP_Client_SetServiceRequested(service.serviceId, service.instanceId, true);

    </code>

  Remarks:
  Setting the same service request state as already set before will be ignored.
*/

bool SOMEIP_Client_SetServiceRequested(uint16_t serviceId, bool requested);


// *****************************************************************************
/* Function:
    void SOMEIP_Client_CheckTimers
    (
        void
    )

  Summary:
    This function is giving processor time to the internal state machines of the
    client.

  Description:
     All registered services or events are getting handled inside this function.
     Mainly all the callbacks from the SOME/IP client module are getting called
     within this function.

  Precondition:
    SOMEIP_Client_AddService() must have been be called in front.

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to cyclic service the
    // SOME/IP Client module

    while(true) {
       SOMEIP_Client_CheckTimers();
       //Sleep for some milliseconds
    }

    </code>

  Remarks:
    The cycle time of calling this function is also the smallest reaction time
    of the SOME/IP client. For example, calling this function every 5ms, the
    client is able to respond to an incoming SOME/IP message within this
    particular 5ms.
*/

void SOMEIP_Client_CheckTimers(void);


// *****************************************************************************
/* Function:
    enum SOMEIP_ReturnCode SOMEIP_Client_DataReceived
    (
        const uint8_t *b,
        uint16_t bLen,
        struct SOMEIP_IpAddr *pIpAddr
    )

  Summary:
    This function is passing received UDP/TCP data to the client.

  Description:
    Whenever SOME/IP client related data (SOME/IP-SD-Port) was received, this
    function needs to be called. The data is getting analyzed and parsed.

  Precondition:
    SOMEIP_Client_AddService() must have been be called in front.

  Parameters:
    pBuf - Pointer to an array with the received SOME/IP datagrams. Note,
    the Ethernet, IP, UDP/TCP headers should already be stripped away by the
    underlying TCP/IP stack. So the first byte of this Byte array is the first
    Byte of the SOME/IP message. The payload is getting deep copied if needed,
    so the memory of this pointer might me reused directly after the call of
    this function.

    bufLen - The length of the Byte array given with pBuf measured in Bytes.

    pIpAddr - Optional (but highly recommended) parameter. Passes the source
    and destination IP address along with the remote port to client. This is
    mainly helpful to distinguish between Unicast / Broadcast and Multicast
    messages. The payload is getting deep copied if needed, so the memory of
    this pointer might me reused directly after the call of this function. If
    the used TCP/IP does not provide that IP-related informations, pass NULL.

  Returns:
    enum SOMEIP_ReturnCode - Result of the parsing of the given data. Anything
    else than SOMEIP_E_OK can be treated as an error.

  Example:
    <code>
    // The following code snippet shows an example how to pass received data to
    // the SOME/IP client

    while(true) {
        static uint8_t udpData[1500];  //Do not kill your memory stack ;-)
        uint16_t dataLength = 0;
        enum SOMEIP_ReturnCode result;
        struct SOMEIP_IpAddr remoteIP;

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(udpData, &dataLength, &remoteIP);

        // Pass the received data to SOME/IP server component
        result = SOMEIP_Client_DataReceived(udpData, dataLength, &remoteIP);
        if (SOMEIP_E_OK != result) {
            // TODO: DO error handling
        }
    }

    </code>

  Remarks:
    This function parses the given data and return the result back instantly.
    However, it will not raise any reaction out of this function call. This will
    be done earliest in the next call to SOMEIP_Client_CheckTimers().
*/

enum SOMEIP_ReturnCode SOMEIP_Client_DataReceived(const uint8_t *b, uint16_t bLen, struct SOMEIP_IpAddr *pIpAddr, void *rxTag);


// *****************************************************************************
/* Function:
    bool SOMEIP_Client_SendConfig
    (
         uint16_t serviceId,
         uint16_t instanceId,
         struct SOMEIP_OptConfig *pConfig,
         SOMEIP_SendConfig_Callback_t pSendCB
    )

  Summary:
    This function is used to configure the server remotely from this client.

  Description:
    This function let the client send out configuration key/value pairs
    to to server. The data is getting parsed on the server and it will adjust
    it internal blocks accordingly.

  Precondition:
    SOMEIP_Client_AddService() must have been be called in front.

  Parameters:
    serviceId - The service identifier, as provided in the struct SOMEIP_Server_Client
    while calling SOMEIP_Client_AddService().

    instanceId - The instance identifier, as provided in the struct SOMEIP_Server_Client
    while calling SOMEIP_Client_AddService().

    pConfig - Pointer to a filled key-value-pair-array to be transmitted. The
    given structure will be deep copied inside this function. So user can
    temporarily allocate the structure from the stack and hand it over to this
    function.

    pSendCB - Pointer to a callback function. This function will be called
    whenever this function has been finished. The success (it might timeout)
    will be reported as function parameter along with the callback.

  Returns:
    If successful, returns true.
    Otherwise, returns false, did not find a valid entry with the given
    serviceId and instanceId or it is currently busy, try again later.

  Example:
    <code>
    // The following code snippet shows an example how to transmit configuration
    // data from the client to the server.

    struct SOMEIP_Server_Client service;
    service.eventHandlingEnabled = true;
    service.eventGroupId = 0xABBA;
    service.ttl = 5;
    service.minorVersion = 1;
    service.clientId = 0xAFFE;
    service.serviceId = 0xBEAF;
    service.instanceId = 1;
    service.majorVersion = 1;
    service.pEventCb = NULL;
    service.ipAddr.port = 54321;
    service.ipAddr.sourceAddr[0] = 192;
    service.ipAddr.sourceAddr[1] = 168;
    service.ipAddr.sourceAddr[2] = 0;
    service.ipAddr.sourceAddr[3] = 100;

    if (SOMEIP_Client_AddService(&service, true, true)) {
        struct SOMEIP_OptConfig config;
        uint16_t i;
        config.pairCount = 0;
        for (i = 0u; i < MAX_CONFIG_OPT_ENTRIES; i++) {
            snprintf(config.key[i], MAX_CONFIG_OPT_KEY_LEN, "MyKey-%d", i);
            snprintf(config.val[i], MAX_CONFIG_OPT_VAL_LEN, "MyValue-%d", i);
            config.pairCount++;
        }

        if (!SOMEIP_Client_SendConfig(service.serviceId, service.instanceId, &config, OnSendConfigEvent)) {
            //TODO: Try again later..
        }
    } else {
        // TODO: DO error handling
    }

    </code>

  Remarks:
    Consider using secure string functions while filling out the configuration
    structure. For safety reasons double check and use the MAX_CONFIG_OPT_ENTRIES,
    MAX_CONFIG_OPT_KEY_LEN and MAX_CONFIG_OPT_VAL_LEN defines in the config file.
*/

bool SOMEIP_Client_SendConfig(uint16_t serviceId, uint16_t instanceId, const struct SOMEIP_OptConfig *pConfig, SOMEIP_SendConfig_Callback_t pSendCB);

// *****************************************************************************
// *****************************************************************************
// Section: Public API for SOME/IP Generator
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_Header
    (
        const struct SOMEIP_Header *pParam,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with the SOME/IP basic header (addressing part).

  Description:
    With this function the start of a SOME/IP datagram (SOME/IP Header) can be
    generated and written into the given Byte array.

  Precondition:
    None.

  Parameters:
    pParam - Pointer to the structure holding all SOME/IP relevant information.
    The given structure will be deep copied inside this function. So user can
    temporarily allocate the structure from the stack and hand it over to this
    function.

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_Header(const struct SOMEIP_Header *pParam, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);

bool SOMEIP_Generator_Update_Length(uint32_t consumed, uint8_t *pBuf, uint16_t bufLen);

// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_Tag
    (
        uint16_t tagDataId,
        uint16_t length,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag (without data).

  Description:
    With this function a tag (consisting of Data Id and length) is getting
    serialized into the given buffer. Having a tag only maybe useful for
    starting a struct, a union or an array.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    length - The length of the payload following this tag. If this variable is
    set to 0, then the length field of the tag will be skipped (not generated).

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_Tag(uint16_t tagDataId, uint16_t length, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_UINT8
    (
        uint16_t tagDataId,
        uint8_t value,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag and a 8 Bit data
    field with the given value.

  Description:
    With this function a tag and a 8 Bit value is getting serialized into the
    given buffer.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    value - The 8 Bit value to be encoded.

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_UINT8(uint16_t tagDataId, uint8_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_UINT16
    (
        uint16_t tagDataId,
        uint16_t value,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag and a 16 Bit data
    field with the given value.

  Description:
    With this function a tag and a 16 Bit value is getting serialized into the
    given buffer.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    value - The 16 Bit value to be encoded.

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_UINT16(uint16_t tagDataId, uint16_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_UINT32
    (
        uint16_t tagDataId,
        uint32_t value,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag and a 32 Bit data
    field with the given value.

  Description:
    With this function a tag and a 32 Bit value is getting serialized into the
    given buffer.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    value - The 32 Bit value to be encoded.

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_UINT32(uint16_t tagDataId, uint32_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);

// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_UINT64
    (
        uint16_t tagDataId,
        uint64_t value,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag and a 64 Bit data
    field with the given value.

  Description:
    With this function a tag and a 64 Bit value is getting serialized into the
    given buffer.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    value - The 64 Bit value to be encoded.

    pBuf - Pointer to a Byte array which shall be filled.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_64 = 0xFEADDEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT64(tag_id_32, val_64, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_UINT64(uint16_t tagDataId, uint64_t value, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);

// *****************************************************************************
/* Function:
    bool SOMEIP_Generator_Fill_BLOB
    (
        uint16_t tagDataId,
        const uint8_t *pBlob,
        uint16_t blobLen,
        uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pConsumed
    )

  Summary:
    This function fills a given Byte array with a SOME/IP tag and a BLOB (binary
    large object), meaning a source Byte array.

  Description:
    With this function a tag and a BLOB Byte array is getting serialized into
    the given buffer.

  Precondition:
    None.

  Parameters:
    tagDataId - The ID of the tag.

    pBlob - Pointer to a Byte array which shall be encoded (acting as source).

    blobLen -  Length of the pBlob Byte array. Measured in Bytes.

    pBuf - Pointer to a Byte array which shall be filled (acting as destination).

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the tag header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parameter error.

  Example:
    <code>
    // The following code snippet shows an example how to generate a SOME/IP
    // datagram using several SOMEIP_Generator_Fill_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t consumed = 0u;
        bool success;
        uint16_t tag_id_base = 0xEDC;
        uint16_t tag_id_8 = 0xF00u;
        uint16_t tag_id_16 = 0xF01u;
        uint16_t tag_id_32 = 0xF02u;
        uint16_t tag_id_blob = 0xF03u;

        const uint8_t val_blob[] = { 0xD0, 0xD1u, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD6u, 0xD7u, 0xD8u, 0xD9u };
        uint32_t val_32 = 0xFEADBEEFu;
        uint16_t val_16 = 0xAFFEu;
        uint8_t val_8 = 0xABu;

        struct SOMEIP_Header par1;
        par1.eventHandlingEnabled =    false;
        par1.serviceId =        0x1122;
        par1.methodId =  0x3344;
        par1.length =           0x55667788;
        par1.interfaceVersion = 0x99;
        par1.clientId =         0xAABB;
        par1.sessionId =        0xCCDD;
        par1.msgType =          MSGTYPE_REQUEST;
        par1.retCode =          SOMEIP_E_OK;

        success = SOMEIP_Generator_Fill_Header(&par1, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_Tag(tag_id_base, tag_master_length, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT8(tag_id_8, val_8, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT16(tag_id_16, val_16, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_UINT32(tag_id_32, val_32, &buf[consumed], (sizeof(buf) - consumed), &consumed) &&
                  SOMEIP_Generator_Fill_BLOB(tag_id_blob, val_blob, sizeof(val_blob), &buf[consumed], (sizeof(buf) - consumed), &consumed);

        if (success) {
            //Send UDP telegram with buf[consumed]
        } else {
            // Check parameters, at least one is erroneous.
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Generator_Fill_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Generator_Fill_BLOB(uint16_t tagDataId, const uint8_t *pBlob, uint16_t blobLen, uint8_t *pBuf, uint16_t bufLen, uint16_t *pConsumed);


// *****************************************************************************
// *****************************************************************************
// Section: Public API for SOME/IP Parser
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
/* Function:
    enum SOMEIP_ReturnCode SOMEIP_Parser_Read_Header
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        struct SOMEIP_Header *pParam,
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP base header from the given Byte array.

  Description:
    With this function the start of a SOME/IP datagram (SOME/IP Header) can be
    parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pParam - Pointer to the structure holding all SOME/IP relevant information.
    The given structure will be filled inside this function, when there was no
    parsing error.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    enum SOMEIP_ReturnCode - Result of the parsing of the given data. Anything
    else than SOMEIP_E_OK can be treated as an error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

enum SOMEIP_ReturnCode SOMEIP_Parser_Read_Header(const uint8_t *pBuf, uint16_t bufLen, struct SOMEIP_Header *pParam, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    enum SOMEIP_ReturnCode SOMEIP_Parser_Read_Tag
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint16_t *pLength
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag (without data) from the given Byte array.

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) can be
    parsed from the given Byte array. Having a tag only maybe useful for
    starting a struct, a union or an array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pLength - Pointer to the length of tag. This variable will be written
    within this function, when parsing was successful. This variable might be
    set to 0, when there was no length field encoded in the given SOME/IP data.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    enum SOMEIP_ReturnCode - Result of the parsing of the given data. Anything
    else than SOMEIP_E_OK can be treated as an error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_Tag(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint16_t *pLength, uint16_t *pConsumed);

// *****************************************************************************
/* Function:
    bool SOMEIP_Parser_Read_UINT8
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint8_t *pValue,
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag and a 8 Bit data field from the given
    Byte array.

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) and
    an additional 8 Bit data field can be parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pValue - Pointer to the 8 Bit data value.  This variable will be written
    within this function, when parsing was successful.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parsing error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_UINT8(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint8_t *pValue, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    bool SOMEIP_Parser_Read_UINT16
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint16_t *pValue,
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag and a 16 Bit data field from the given
    Byte array.

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) and
    an additional 16 Bit data field can be parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pValue - Pointer to the 16 Bit data value.  This variable will be written
    within this function, when parsing was successful.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parsing error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_UINT16(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint16_t *pValue, uint16_t *pConsumed);


// *****************************************************************************
/* Function:
    bool SOMEIP_Parser_Read_UINT32
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint32_t *pValue,
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag and a 32 Bit data field from the given
    Byte array.

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) and
    an additional 32 Bit data field can be parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pValue - Pointer to the 32 Bit data value.  This variable will be written
    within this function, when parsing was successful.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parsing error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_UINT32(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint32_t *pValue, uint16_t *pConsumed);



// *****************************************************************************
/* Function:
    bool SOMEIP_Parser_Read_UINT64
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint64_t *pValue,
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag and a 64 Bit data field from the given
    Byte array.

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) and
    an additional 64 Bit data field can be parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data.

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pValue - Pointer to the 64 Bit data value.  This variable will be written
    within this function, when parsing was successful.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parsing error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_64 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint64_t val_64 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT64(&buf1[parsed], (rx_length - parsed), &tag_id_64, &val_64, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_UINT64(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint64_t *pValue, uint16_t *pConsumed);

bool SOMEIP_Parser_Read_Length_BLOB(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pBlobLen);

// *****************************************************************************
/* Function:
    bool SOMEIP_Parser_Read_BLOB
    (
        const uint8_t *pBuf,
        uint16_t bufLen,
        uint16_t *pTagDataId,
        uint8_t *pBlob,
        uint16_t *pBlobLen
        uint16_t *pConsumed
    )

  Summary:
    This function parses a SOME/IP tag and  a BLOB (binary large object),
    meaning a source Byte array

  Description:
    With this function a SOME/IP tag (consisting of Data Id and length) and
    an additional BLOB Byte array can be parsed from the given Byte array.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to a Byte array which contains the serialized data (received
    data).

    bufLen - The maximum available length of the Byte array given with pBuf
    measured in Bytes.

    pTagDataId - Pointer to the ID of the tag. This variable will be written
    within this function, when parsing was successful.

    pBlob - Pointer to the BLOB data array (application data). This variable
    will be written within this function, when parsing was successful.

    pBlobLen - Pointer to the length of the pBlob Byte array. Measured in Bytes.
    This variable will be written within this function, when parsing was
    successful.

    pConsumed - Optional pointer to consumed variable. If the pointer is not
    NULL, then the current value inside the variable will be incremented by the
    amount of Bytes consumed by the SOME/IP header generated by this function.
    Doing so, make it easy to combine multiple SOMEIP_Generator_Fill_x functions
    into a single statement (see code example).

  Returns:
    If successful, returns true.
    Otherwise, returns false, due to parsing error.

  Example:
    <code>
    // The following code snippet shows an example how to parse a SOME/IP
    // datagram using several SOMEIP_Parser_Read_x functions

    while(true) {
        uint8_t buf[64];
        uint16_t  rx_length = 0u;
        uint16_t parsed = 0u;
        bool success;

        uint16_t tag_id_base = 0u;
        uint16_t tag_id_8 = 0u;
        uint16_t tag_id_16 = 0u;
        uint16_t tag_id_32 = 0u;
        uint16_t tag_id_blob = 0u;

        uint8_t val_blob[16] = { 0u };
        uint16_t val_blob_array_len = 0u;

        uint32_t val_32 = 0u;
        uint16_t val_16 = 0u;
        uint8_t val_8 = 0u;

        struct SOMEIP_Header par1 = { 0 };

        // Fake call to a hypothetic UDP/IP Stack
        FAKE_UDP_STACK_RECEIVE_DATA(buf, &rx_length);

        success = (SOMEIP_E_OK == SOMEIP_Parser_Read_Header(&buf1[parsed], (rx_length - parsed), &par1, &parsed)) &&
              SOMEIP_Parser_Read_Tag(&buf1[parsed], (rx_length - parsed), &tag_id_base, &tag_master_length, &parsed) &&
              SOMEIP_Parser_Read_UINT8(&buf1[parsed], (rx_length - parsed), &tag_id_8, &val_8, &parsed) &&
              SOMEIP_Parser_Read_UINT16(&buf1[parsed], (rx_length - parsed), &tag_id_16, &val_16, &parsed) &&
              SOMEIP_Parser_Read_UINT32(&buf1[parsed], (rx_length - parsed), &tag_id_32, &val_32, &parsed) &&
              SOMEIP_Parser_Read_BLOB(&buf1[parsed], (rx_length - parsed), &tag_id_blob, val_blob, &val_blob_array_len, &parsed);

        if (success) {
            // All variables have been filled and may be used now.
        } else {
            // TODO: DO error handling and discard received datagram
        }
    }

    </code>

  Remarks:
    This helper function and the other SOMEIP_Parser_Read_x functions are
    meant to be used in the SOME/IP data communication parallel to the SOME/IP
    service discovery (Other TCP/UDP ports than the 30490 for service discovery).
*/

bool SOMEIP_Parser_Read_BLOB(const uint8_t *pBuf, uint16_t bufLen, uint16_t *pTagDataId, uint8_t *pBlob, uint16_t *pBlobLen, uint16_t *pConsumed);


// *****************************************************************************
// *****************************************************************************
// Section: SOME/IP Transmitter
// *****************************************************************************
// *****************************************************************************

SOMETR_t *SOMEIP_Transmit_Init(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback, void *rxTag);

struct SOMEIP_Transmit_Buffer *SOMEIP_Transmit_GetBuffer(SOMETR_t *tr);

bool SOMEIP_Transmit_Send(SOMETR_t *tr, struct SOMEIP_Transmit_Buffer *pBuf);

void SOMEIP_Transmit_ReleaseBufferOnError(SOMETR_t *tr, struct SOMEIP_Transmit_Buffer *pBuf);

void SOMEIP_Transmit_CheckTimers(void);

void SOMEIP_Transmit_ReceivedResponse(uint8_t remoteIP[SOMEIP_IPV4_ADDR_LEN], SOMETR_t *tr, uint16_t sessionId, enum SOMEIP_ReturnCode retCode, const uint8_t *pRxBuf, uint16_t rxBufLen);

// *****************************************************************************
// *****************************************************************************
// Section: Callback functions from SOME/IP, to be implemented in higer layers
// *****************************************************************************
// *****************************************************************************

void SOMEIP_CB_Log(const char *logMsg); /* Zero terminated human readable character string */

bool SOMEIP_CB_OpenSocket(uint16_t *udpPort, SOMEIP_DataReceived_CB_t rxCallback, void *rxTag, void **sockHandle);

bool SOMEIP_CB_GetLocalIpAddr(uint8_t localIP[SOMEIP_IPV4_ADDR_LEN], const uint8_t targetIP[SOMEIP_IPV4_ADDR_LEN]);

void SOMEIP_CB_EnterCriticialSection(void);

void SOMEIP_CB_LeaveCriticialSection(void);

typedef void *SOME_IP_SEM_t;

bool SOMEIP_CB_SemInit(SOME_IP_SEM_t *sem, int8_t initialValue);

bool SOMEIP_CB_SemWait(SOME_IP_SEM_t *sem);

void SOMEIP_CB_SemPost(SOME_IP_SEM_t *sem);

void SOMEIP_CB_SemDestroy(SOME_IP_SEM_t *sem);

void *SOMEIP_CB_Calloc(size_t xNum, size_t xSize);

void SOMEIP_CB_Free(void *pMem);

// *****************************************************************************
/* Function:
    extern bool SOMEIP_CB_ProvideBuffer
    (
        uint8_t **ppBuffer,
        void **ppMemTag,
        uint16_t length
    )

  Summary:
    Callback to get dynamic memory.

  Description:
    Callback from SOME/IP component to higher layer in order to get dynamic
    allocated memory, which ideally is already in the memory section of the
    used TCP/IP stack.

  Precondition:
    None.

  Parameters:
    ppBuffer - Output pointer to hand over the dynamically allocated memory to
    the SOME/IP component. Usually the TCP/IP stacks provide functions to do so.

    ppMemTag - Output void Pointer to store any TCP/IP specific information, so
    that the allocated memory can be referenced and freed up later on (Buffer
    handle).

    length - Input length. The dynamic allocated memory must have at least this
    length, measured in Bytes. If length is set to 0, the buffer will be freed
    without sending any data to the network.

  Returns:
    Integrator shall return true, if successfully allocated memory.
    Otherwise, returning false, due to allocation error. In that case the
    pending outgoing SOME/IP frame is getting either dropped or sent later.

  Example:
    <code>
    // The following code snippet shows an example how to allocate memory with
    // the lwIP TCP/IP stack. This should be similiar for other TCP/IP stacks

    bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length)
    {
        bool success = false;
        struct pbuf *pb =  pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
        if (NULL != pb) {
            *ppBuffer = pb->payload;
            *ppMemTag = pb;
            success = true;
        }
        return success;
    }

    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator.
*/

extern bool SOMEIP_CB_ProvideBuffer(uint8_t **ppBuffer, void **ppMemTag, uint16_t length);

// *****************************************************************************
/* Function:
    extern bool SOMEIP_CB_SendUdp
    (
        uint8_t *pBuf,
        uint16_t length,
        void *pMemTag,
        const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN],
        uint16_t port
    )

  Summary:
    Callback to send out a SOME/IP datagram via an UDP message.
    And free up the given memory, referenced via pMemTag!!!

  Description:
    Callback from SOME/IP component to higher layer in order to send out a
    SOME/IP datagram via an UDP message.

  Precondition:
    None.

  Parameters:
    pBuf - Pointer to the payload to be sent via UDP. Note, that the SOME/IP
    component will delete any reference to this buffer after this callback has
    ended. So it is up to the integrator to avoid memory leaks, regardless if
    the sending of the UDP message was successful or not!

    length - The length of the pBuf Byte array, measured in Bytes.

    pMemTag - Void pointer which may point to TCP/IP specific informations. It
    is the same pointer, as given back with ppMemTag parameter of the
   SOMEIP_CB_ProvideBuffer() callback function.

    ipAddrV4 - The target IP-V4 32Bit address.

    port - The target UDP port.

  Returns:
    Integrator shall return true, if the UDP message was successfully enqueued
    for sending with the TCP/IP stack.
    Otherwise, returning false, due to sending problems. In that case the
    pending outgoing SOME/IP frame is getting either dropped or sent later.

  Example:
    <code>
    // The following code snippet shows an example how to send out a UDP message
    // with the lwIP TCP/IP stack. This should be similiar for other TCP/IP
    // stacks

    bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag, const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port, void *sockHandle)
    {
        struct ip_addr remoteIp;
        struct pbuf *pb = pMemTag;
        IP4_ADDR(&remoteIp, ip[0], ip[1], ip[2], ip[3]);
        err_t result = udp_sendto(m.sock, pb, &remoteIp, port);
        pbuf_free(pb);
        return (ERR_OK == result);
    }

    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator. Make sure that the buffer is
    marked for free up after this callback to avoid memory leaks!
*/

extern bool SOMEIP_CB_SendUdp(uint8_t *pBuf, uint16_t length, void *pMemTag, const uint8_t ipAddrV4[SOMEIP_IPV4_ADDR_LEN], uint16_t port, void *sockHandle);

// *****************************************************************************
/* Function:
    extern void SOMEIP_CB_Assert
    (
        const char *pFilename,
        uint32_t lineNr
    )

  Summary:
    Callback to help debugging the SOME/IP component.

  Description:
    Callback from SOME/IP component to higher layer in order to report an
    internal error. This might be helpful to determinate code issues.

  Precondition:
    None.

  Parameters:
    pFilename - Zero terminated string, giving the full path of the C- file
    having raised the issue.

    lineNr - Line number inside the given file, causing the issue.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to deal with the
    // assertion. If there is any persistent logging available, then store it
    // there

    void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr)
    {
        printf("ASSERT, file='%s', line=%d\r\n", pFilename, lineNr);
    }

    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator. It might be implemented empty
    for release versions.
*/

extern void SOMEIP_CB_Assert(const char *pFilename, uint32_t lineNr);


// *****************************************************************************
/* Function:
    extern uint32_t SOMEIP_CB_GetTimeMS
    (
        void
    )

  Summary:
    Callback to deliver back the current time in millisecond ticks.

  Description:
    Callback from SOME/IP component to higher layer in order to retrieve the
    current system time in milliseconds.

  Precondition:
    None.

  Parameters:
    None.

  Returns:
    Integrator shall return the current system time in milliseconds. The initial
    offset is do not care. Overflowing of 32 Bit value is also no problem. But
    it must statically increment up (not jumping backwards to due corrections).

  Example:
    <code>
    // The following code snippet shows an example how to retrieve the system
    // clocks on Harmony 3 with the systick component

    uint32_t SOMEIP_CB_GetTimeMS(void)
    {
        return systick.tickCounter;
    }

    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator.
*/

extern uint32_t SOMEIP_CB_GetTimeMS(void);


// *****************************************************************************
/* Function:
    extern void SOMEIP_CB_NeedService
    (
        void
    )

  Summary:
    Callback to speed up the performance by servicing SOME/IP after this call as
    quick as possible (for example by stopping ongoing waits).

  Description:
    Callback from SOME/IP component to higher layer in order to trigger service
    calls of all SOME/IP components.

  Precondition:
    None.

  Parameters:
    None.

  Returns:
    None.

  Example:
    <code>
    // The following code snippet shows an example how to retrieve the system
    // clocks on Harmony 3 with the systick component

    void SOMEIP_CB_NeedService(void)
    {
        // Waking thread waiting with sem_timedwait()
        sem_post(&m.semServiceTimer);
    }
    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator.
*/

extern void SOMEIP_CB_NeedService(void);

// *****************************************************************************
/* Function:
    extern uint32_t SOMEIP_CB_GetRandom
    (
        uint32_t min,
        uint32_t max
    )

  Summary:
    Callback to deliver back a random number.

  Description:
    Callback from SOME/IP component to higher layer in order to retrieve a
    random number in the range of the provided parameters. There is not a high
    demand on the quality of the random number (no cryptography involved).

  Precondition:
    None.

  Parameters:
    min - The minimum value which is allowed to be returned

    max - The Maximum value which is allowed to be returned

  Returns:
    Integrator shall return a random number in between the min and max parameter
    (both values inclusive)

  Example:
    <code>
    // The following code snippet shows an example how to generate a random
    // number using the current time. If there is a dedicated IP available, then
    // this should be preferred.

    uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max)
    {
        uint32_t diff = (max - min + 1);
        uint32_t val = (systick.tickCounter % diff);
        return (val + min);
    }

    </code>

  Remarks:
    This function is a mandatory callback to be implemented in the target
    application by the corresponding integrator.
*/

extern uint32_t SOMEIP_CB_GetRandom(uint32_t min, uint32_t max);


//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif /* SOMEIP_GEN_H */
