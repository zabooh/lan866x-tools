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
/* SOME/IP Common Header for Generator and Parser                                                 */
/*------------------------------------------------------------------------------------------------*/

#ifndef SOMEIP_COMMON_H
#define SOMEIP_COMMON_H

#include "../inc/someip.h"

#ifdef __cplusplus
extern "C" {
#endif

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    INTERNAL DEFINITIONS AND MACROS                   */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_VERSION          (1u)

#define MASK(width)             (((uint32_t)1u << (width)) - 1u)

#define FLD_LJ(pos, width)      ((uint32_t)(((width) << 8u) | (32 - pos - width))) /* Left Justified */
#define FLD_RJ(pos, width)      ((uint32_t)(((width) << 8u) | pos)) /* Right Justified */
#define FLD_POS(f)              ((f) & 255u)
#define FLD_MASK(f)             MASK(((f) >> (8u & 255u)))

#define GET_VAL(_field, _v32)   (((uint32_t)(_v32) >> (FLD_POS(_field))) & (FLD_MASK(_field)))
#define MK_MASK(_field, _val)   (((uint32_t)(_val) & (FLD_MASK(_field))) << (FLD_POS(_field)))

#define SOMEIP_ASSERT(CONDITION, FILENAME, LINE)  do { if (!CONDITION) SOMEIP_CB_Assert(FILENAME, LINE); } while(0)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    GENERIC SOME/IP DEFINTIONS                        */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define BYTE_ORDER_MARK(U)      (U + 0xFEFFu)

#define SOMEIP_HEADER_LENGTH    (16u)

#define SOMEIP_POS_MSG_ID_0     (0u)
#define SOMEIP_POS_MSG_ID_1     (1u)
#define SOMEIP_POS_MSG_ID_2     (2u)
#define SOMEIP_POS_MSG_ID_3     (3u)

#define SOMEIP_POS_LENGTH_0     (4u)
#define SOMEIP_POS_LENGTH_1     (5u)
#define SOMEIP_POS_LENGTH_2     (6u)
#define SOMEIP_POS_LENGTH_3     (7u)

#define SOMEIP_POS_REQ_ID_0     (8u)
#define SOMEIP_POS_REQ_ID_1     (9u)
#define SOMEIP_POS_REQ_ID_2     (10u)
#define SOMEIP_POS_REQ_ID_3     (11u)

#define SOMEIP_POS_PROT_VER     (12u)
#define SOMEIP_POS_INTF_VER     (13u)

#define SOMEIP_POS_MSG_TYPE     (14u)
#define SOMEIP_POS_RET_CODE     (15u)

#define SOMEIP_FLD_ID_SERVICE_ID    FLD_LJ(0u, 16u)    /* Structure ID, Field Service ID */
#define SOMEIP_FLD_ID_MTHD_EVNT     FLD_LJ(16u, 1u)    /* Structure ID, Selector Method(0)/Event(1) */
#define SOMEIP_FLD_ID_METHOD_ID     FLD_LJ(16u, 16u)   /* Structure ID, Field Method ID */

#define SOMEIP_FLD_REQID_CLIENT_ID  FLD_LJ(0u, 16u)    /* Structure Request ID, Field Client ID */
#define SOMEIP_FLD_REQID_SESSION_ID FLD_LJ(16u, 16u)   /* Structure Request ID, Field Session ID */

#define SOMEIP_MAX_TAG_ID       (0xFFFu)

enum SOMEIP_WireType
{
    WIRETYPE_8_BIT = 0x00,                  /** 8 Bit Data Base data type */
    WIRETYPE_16_BIT = 0x01,                 /** 16 Bit Data Base data type */
    WIRETYPE_32_BIT = 0x02,                 /** 32 Bit Data Base data type */
    WIRETYPE_64_BIT = 0x03,                 /** 64 Bit Data Base data type */
    WIRETYPE_COMPLEX_STATIC_SIZE = 0x04,    /** Complex data type: Array, Struc, String, Union with length field of static size */
    WIRETYPE_COMPLEX_1_BYTE = 0x05,         /** Complex data type: Array, Struc, String, Union with length field of 1 Byte */
    WIRETYPE_COMPLEX_2_BYTE = 0x06,         /** Complex data type: Array, Struc, String, Union with length field of 2 Byte */
    WIRETYPE_COMPLEX_4_BYTE = 0x07,         /** Complex data type: Array, Struc, String, Union with length field of 4 Byte */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                    SOME/IP SERVICE DISCOVERY DEFINTIONS              */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_SERVICE_EVENT  (true)
#define SOMEIP_SD_SERVICE_ID     (0xFFFFu)
#define SOMEIP_Event_ID          (0x8100u)
#define SOMEIP_SD_INTERFACE_VER  (1u)
#define SOMEIP_SD_MESSAGE_TYPE   (MSGTYPE_NOTIFICATION)

#define SOMEIP_SD_LENGTH         (8u)

#define SOMEIP_SD_POS_FLAGS      (0u)

#define SOMEIP_SD_POS_LEN_ENTR_0 (4u)
#define SOMEIP_SD_POS_LEN_ENTR_1 (5u)
#define SOMEIP_SD_POS_LEN_ENTR_2 (6u)
#define SOMEIP_SD_POS_LEN_ENTR_3 (7u)

#define SOMEIP_SD_FLD_FLAGS_REBOOT   0x80     /* Structure Flags, Field Reboot Flag */
#define SOMEIP_SD_FLD_FLAGS_UNICAST  0x40     /* Structure Flags, Field Unicast Flag */

struct SOMEIP_SD_Header
{
    uint32_t length;                          /** Length of Entries Array in Bytes */
    bool reboot;                              /**  */
    bool unicast;                             /**  */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*            SOME/IP SERVICE DISCOVERY SERVICE ENTRY DEFINTIONS        */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_SERVICE_INST_ID_ALL        (0xFFFFu)
#define SOMEIP_SD_SERVICE_MAJOR_VER_ANY      (0xFFu)
#define SOMEIP_SD_SERVICE_MINOR_VER_ANY      (0xFFFFFFFFul)

#define SOMEIP_SD_SERVICE_LENGTH             (16u)

#define SOMEIP_SD_SERVICE_POS_TYPE           (0u)
#define SOMEIP_SD_SERVICE_POS_INDEX_OPT1     (1u)
#define SOMEIP_SD_SERVICE_POS_INDEX_OPT2     (2u)
#define SOMEIP_SD_SERVICE_POS_NUM_OPT        (3u)

#define SOMEIP_SD_SERVICE_POS_SERVICE_ID_0   (4u)
#define SOMEIP_SD_SERVICE_POS_SERVICE_ID_1   (5u)

#define SOMEIP_SD_SERVICE_POS_INST_ID_0      (6u)
#define SOMEIP_SD_SERVICE_POS_INST_ID_1      (7u)

#define SOMEIP_SD_SERVICE_POS_MAJOR_VER      (8u)

#define SOMEIP_SD_SERVICE_POS_TTL_0          (9u)
#define SOMEIP_SD_SERVICE_POS_TTL_1          (10u)
#define SOMEIP_SD_SERVICE_POS_TTL_2          (11u)

#define SOMEIP_SD_SERVICE_POS_MINOR_VER_0    (12u)
#define SOMEIP_SD_SERVICE_POS_MINOR_VER_1    (13u)
#define SOMEIP_SD_SERVICE_POS_MINOR_VER_2    (14u)
#define SOMEIP_SD_SERVICE_POS_MINOR_VER_3    (15u)

#define SOMEIP_SD_SERVICE_NUM_OPT1           FLD_RJ(4u, 4u)    /* Structure Service, Field NumOpt1 */
#define SOMEIP_SD_SERVICE_NUM_OPT2           FLD_RJ(0u, 4u)    /* Structure Service, Field NumOpt2 */

enum SOMEIP_SD_ServiceEntryType
{
    SDServiceType_FindService = 0,          /** */
    SDServiceType_OfferService = 1,         /** */
    SDServiceType_StopService = 1,          /** */
};

struct SOMEIP_SD_Service
{
    enum SOMEIP_SD_ServiceEntryType type;       /**  */
    uint32_t ttl;                           /**  */
    uint32_t minorVersion;                  /**  */
    uint16_t serviceID;                     /**  */
    uint16_t instanceID;                    /**  */
    uint8_t index1stOpt;                    /**  */
    uint8_t index2ndOpt;                    /**  */
    uint8_t numberOfOpt1;                   /**  */
    uint8_t numberOfOpt2;                   /**  */
    uint8_t majorVersion;                   /**  */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*          SOME/IP SERVICE DISCOVERY EVENTGROUP ENTRY DEFINTIONS       */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_EVTGRP_INST_ID_ALL         (0xFFFFu)
#define SOMEIP_SD_EVTGRP_EVENTGROUP_ID_ANY   (0xFFFFu)
#define SOMEIP_SD_EVTGRP_MAJOR_VER_ANY       (0xFFu)
#define SOMEIP_SD_EVTGRP_TTL_NAK             (0x0u)
#define SOMEIP_SD_EVTGRP_LENGTH              (16u)

#define SOMEIP_SD_EVTGRP_POS_TYPE            (0u)
#define SOMEIP_SD_EVTGRP_POS_INDEX_OPT1      (1u)
#define SOMEIP_SD_EVTGRP_POS_INDEX_OPT2      (2u)
#define SOMEIP_SD_EVTGRP_POS_NUM_OPT         (3u)

#define SOMEIP_SD_EVTGRP_POS_SERVICE_ID_0    (4u)
#define SOMEIP_SD_EVTGRP_POS_SERVICE_ID_1    (5u)

#define SOMEIP_SD_EVTGRP_POS_INST_ID_0       (6u)
#define SOMEIP_SD_EVTGRP_POS_INST_ID_1       (7u)

#define SOMEIP_SD_EVTGRP_POS_MAJOR_VER       (8u)

#define SOMEIP_SD_EVTGRP_POS_TTL_0           (9u)
#define SOMEIP_SD_EVTGRP_POS_TTL_1           (10u)
#define SOMEIP_SD_EVTGRP_POS_TTL_2           (11u)

#define SOMEIP_SD_EVTGRP_POS_COUNTER         (13u)
#define SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_0   (14u)
#define SOMEIP_SD_EVTGRP_POS_EVENTGRP_ID_1   (15u)

#define SOMEIP_SD_EVTGRP_NUM_OPT1            FLD_RJ(4u, 4u)     /* Structure EventGroup, Field NumOpt1 */
#define SOMEIP_SD_EVTGRP_NUM_OPT2            FLD_RJ(0u, 4u)     /* Structure EventGroup, Field NumOpt2 */
#define SOMEIP_SD_EVTGRP_COUNTER             FLD_RJ(0u, 4u)     /* Structure EventGroup, Field Counter */

enum SOMEIP_CB_EventEntryType
{
    SDEventType_SubscribeEventGroup = 6,    /** */
    SDEventType_StopSubscribeEventgroup = 6,/** */
    SDEventType_SubscribeEventgroupAck = 7, /** */
    SDEventType_SubscribeEventgroupNack = 7,/** */
};

struct SOMEIP_SD_Event
{
    enum SOMEIP_CB_EventEntryType type;        /**  */
    uint32_t ttl;                           /**  */
    uint16_t serviceID;                     /**  */
    uint16_t instanceID;                    /**  */
    uint16_t eventGroupID;                  /**  */
    uint8_t index1stOpt;                    /**  */
    uint8_t index2ndOpt;                    /**  */
    uint8_t numberOfOpt1;                   /**  */
    uint8_t numberOfOpt2;                   /**  */
    uint8_t majorVersion;                   /**  */
    uint8_t counter;                        /**  */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*      SOME/IP SERVICE DISCOVERY LENGTH OF OPTION ARRAY DEFINTIONS     */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_OPTLEN_LENGTH              (4u)

#define SOMEIP_SD_OPTLEN_POS_LEN_0           (0u)
#define SOMEIP_SD_OPTLEN_POS_LEN_1           (1u)
#define SOMEIP_SD_OPTLEN_POS_LEN_2           (2u)
#define SOMEIP_SD_OPTLEN_POS_LEN_3           (3u)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*            SOME/IP SERVICE DISCOVERY CONFIGURATION OPTION            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_OPT_CONFIG_HDR_LENGTH      (3u)
#define SOMEIP_SD_OPT_CONFIG_TYPE_VALUE      (0x1u)

#define SOMEIP_SD_OPT_CONFIG_POS_LENGTH_0    (0u)
#define SOMEIP_SD_OPT_CONFIG_POS_LENGTH_1    (1u)

#define SOMEIP_SD_OPT_CONFIG_POS_TYPE        (2u)
#define SOMEIP_SD_OPT_CONFIG_POS_RESERVED    (3u)

#define SOMEIP_SD_OPT_CONFIG_POS_STRING      (4u)

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*            SOME/IP SERVICE DISCOVERY IPV4 ENDPOINT OPTION            */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_OPT_IPV4_INNER_LENGTH      (9u)
#define SOMEIP_SD_OPT_IPV4_OUTER_LENGTH      (12u)
#define SOMEIP_SD_OPT_IPV4_TYPE_VALUE        (0x4u)
#define SOMEIP_SD_OPT_IPV4_PROT_TCP          (0x6u)
#define SOMEIP_SD_OPT_IPV4_PROT_UDP          (0x11u)

#define SOMEIP_SD_OPT_IPV4_POS_LENGTH_0      (0u)
#define SOMEIP_SD_OPT_IPV4_POS_LENGTH_1      (1u)

#define SOMEIP_SD_OPT_IPV4_POS_TYPE          (2u)

#define SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_0   (4u)
#define SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_1   (5u)
#define SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_2   (6u)
#define SOMEIP_SD_OPT_IPV4_POS_IPV4_ADDR_3   (7u)

#define SOMEIP_SD_OPT_IPV4_POS_IPV4_PROTO    (9u)
#define SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_0   (10u)
#define SOMEIP_SD_OPT_IPV4_POS_IPV4_PORT_1   (11u)

struct SOMEIP_SD_OptIpV4
{
    uint8_t ipV4Addr[SOMEIP_IPV4_ADDR_LEN]; /**  */
    uint16_t portNumber;                    /**  */
    bool udp;                               /** true, if UDP is used. false, if TCP is used. */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*       SOME/IP SERVICE DISCOVERY IPV4 MULTICAST ENDPOINT OPTION       */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_OPT_IPV4MCAST_INNER_LENGTH      (9u)
#define SOMEIP_SD_OPT_IPV4MCAST_OUTER_LENGTH      (12u)
#define SOMEIP_SD_OPT_IPV4MCAST_TYPE_VALUE        (0x14u)
#define SOMEIP_SD_OPT_IPV4MCAST_PROT_UDP          (0x11u)

#define SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_0      (0u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_LENGTH_1      (1u)

#define SOMEIP_SD_OPT_IPV4MCAST_POS_TYPE          (2u)

#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_0   (4u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_1   (5u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_2   (6u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_ADDR_3   (7u)

#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PROTO    (9u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_0   (10u)
#define SOMEIP_SD_OPT_IPV4MCAST_POS_IPV4_PORT_1   (11u)

struct SOMEIP_SD_OptIpV4Mcast
{
    uint8_t ipV4Addr[SOMEIP_IPV4_ADDR_LEN]; /**  */
    uint16_t portNumber;                    /**  */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*  SOME/IP SERVICE DISCOVERY IPV4 SENDER INFORMATION ENDPOINT OPTION   */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

#define SOMEIP_SD_OPT_IPV4_SD_INNER_LENGTH      (9u)
#define SOMEIP_SD_OPT_IPV4_SD_OUTER_LENGTH      (12u)
#define SOMEIP_SD_OPT_IPV4_SD_TYPE_VALUE        (0x24u)
#define SOMEIP_SD_OPT_IPV4_SD_PROT_TCP          (0x6u)
#define SOMEIP_SD_OPT_IPV4_SD_PROT_UDP          (0x11u)

#define SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_0      (0u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_LENGTH_1      (1u)

#define SOMEIP_SD_OPT_IPV4_SD_POS_TYPE          (2u)

#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_0   (4u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_1   (5u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_2   (6u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_ADDR_3   (7u)

#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PROTO    (9u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_0   (10u)
#define SOMEIP_SD_OPT_IPV4_SD_POS_IPV4_PORT_1   (11u)

struct SOMEIP_SD_OptIpV4SD
{
    uint8_t ipV4Addr[SOMEIP_IPV4_ADDR_LEN]; /**  */
    uint16_t portNumber;                    /**  */
    bool udp;                               /** true, if UDP is used. false, if TCP is used. */
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*                  SOME/IP SERVICE DISCOVERY UNIONS                    */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

union SOMEIP_SD_services
{
    struct SOMEIP_SD_Service service;        /**  */
    struct SOMEIP_SD_Event event;            /**  */
};

enum SOMEIP_SD_servicesSelect
{
    SOMEIP_SD_UnionUnused = 0x0,             /**  */
    SOMEIP_SD_UnionService = 0xAA,           /**  */
    SOMEIP_SD_UnionEvent                     /**  */
};

union SOMEIP_SD_UnionOpt
{
    struct SOMEIP_SD_OptIpV4 ipV4;           /**  */
    struct SOMEIP_SD_OptIpV4Mcast ipV4MCast; /**  */
    struct SOMEIP_SD_OptIpV4SD ipV4Sd;       /**  */
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    struct SOMEIP_OptConfig config;       /**  */
#endif
};

enum SOMEIP_SD_UnionOptSel
{
    SOMEIP_SD_UnionOptNone = 0x0,            /**  */
    SOMEIP_SD_UnionOptIpV4 = 0x80,           /**  */
    SOMEIP_SD_UnionOptIpV4MCast,             /**  */
    SOMEIP_SD_UnionOptIpV4SD,                /**  */
#if (0 != MAX_CONFIG_OPT_ENTRIES)
    SOMEIP_SD_UnionOptConfig,                /**  */
#endif
};

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/
/*         SOME/IP SERVICE DISCOVERY STRUCTURE FOR ENTIRE FRAME         */
/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>*/

struct SOMEIP_SD_Frame
{
    union SOMEIP_SD_services services[MAX_SERVICE_EVNT_FIELDS];
    union SOMEIP_SD_UnionOpt options[MAX_OPTION_FIELDS];
    struct SOMEIP_Header someIp;
    struct SOMEIP_SD_Header someIpSd;
    enum SOMEIP_SD_servicesSelect servicesSel[MAX_SERVICE_EVNT_FIELDS];
    enum SOMEIP_SD_UnionOptSel optionsSel[MAX_OPTION_FIELDS];
};

#ifdef __cplusplus
}
#endif

#endif /* SOMEIP_COMMON_H */
