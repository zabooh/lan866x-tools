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
/* SOME/IP for embedded systems configuration file                                                */
/*------------------------------------------------------------------------------------------------*/

#ifndef SOMEIP_CFG_H
#define SOMEIP_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*           EMBEDDED SOME IP SPECIFIC CONFIGURATION PARAMETERS         */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define MAX_SERVER_SERVICES     (0u)        /** Specifies the maximum possible amount of server services/events */
#define MAX_CONNECTIONS_SERVER  (0u)        /** Specifies the maximum remote (IP-) connections for each server instance */

#define MAX_CLIENT_SERVICES     (1u)        /** Specifies the maximum possible amount of client services/events */
#define MAX_CONNECTIONS_CLIENT  (16u)       /** Specifies the maximum remote (IP-) connections for each client instance */

#define MAX_SERVER_SESSION_IDS  (4u)        /** Specifies the maximum possible session id lookup table (support for multiple nodes, each having its own session id) */

#define MAX_SERVICE_EVNT_FIELDS (2u)        /** Specifies the amount of SD-Service or SD-EventGroup entry per SOME/IP message (min 1) */
#define MAX_OPTION_FIELDS       (2u)        /** Specifies the amount of SD-Option fields per SOME/IP message (min 1) */

#define SUBSCRIPTION_DELAY_MIN  (1u)        /** Minimum duration to trigger a SubscribeEventgroup message after a OfferService message was received */
#define SUBSCRIPTION_DELAY_MAX  (10u)       /** Maximum duration to trigger a SubscribeEventgroup message after a OfferService message was received */

#define MAX_CONFIG_OPT_ENTRIES  (5u)        /** Maximum amount of configuration options, 0 turn config option completly off */
#define MAX_CONFIG_OPT_KEY_LEN  (16u)       /** Maxmimum length of zero terminated string for key of Configuration Option */
#define MAX_CONFIG_OPT_VAL_LEN  (16u)       /** Maxmimum length of zero terminated string for valu of Configuration Option */
#define MAX_CONFIG_RETRIES      (3u)        /** Maximum retry amount in case of Config transmission */
#define CONFIG_RETRY_DELAY_TIME (50u)       /** Time in millis to do Config retransmission */

#define SOMEIP_TRANSMIT_MAX_INSTANCES       (4u)
#define SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES   (64u)
#define SOMEIP_TRANSMIT_MAX_TIMEOUT_TIME    (1000u)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                  OFFICIAL SOME IP CONFIGURATION PARAMETERS           */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define INITIAL_DELAY_MIN       (1u)        /** Minimum duration to delay randomly the transmission of a message */
#define INITIAL_DELAY_MAX       (10u)       /** Maximum duration to delay randomly the transmission of a message */
#define REPETITIONS_BASE_DELAY  (1u)        /** Duration of delay for repetitions */
#define REPETITIONS_MAX         (8u)        /** Configuration for the maximum number of repetitions */
#define REQUEST_RESPONSE_DELAY  (10u)       /** The Service Discovery shall delay anserws using this configuration item */
#define CYCLIC_OFFER_DELAY      (1000u)     /** Interval between cyclic offers in the main phase */
#define SD_PORT                 (30490u)    /** Is a UDP port for SD Messages (30490 as default)*/
#define SUBSCRIBE_RETRY_MAX     (3u)        /** Max count of retries for subscrive, as long the Event is requested (0=no retry, INF=retry for ever) */
#define SUBSCRIBE_RETRY_DELAY   (50u)       /** Duration of delay to send a consecutive subscribe entry */
#define MULTICAST_THRESHOLD     (1u)        /** Specifiy the number of subscribed clients with different endpoint information per Event */

#ifdef __cplusplus
}
#endif

#endif /* SOMEIP_CFG_H */
