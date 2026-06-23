//DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.
Adapted from noIP-SAM-E54-Curiosity-PTP-Follower/ptp_task.h for Harmony TCP/IP bridge.
*/
//DOM-IGNORE-END

#ifndef PTP_FOL_TASK_H
#define PTP_FOL_TASK_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "filters.h"

/* Byte-swap helpers (pure C, no CMSIS dependency) */
#ifndef static_htons
#define static_htons(x) (((x & 0xff) << 8) | (x >> 8))
#endif

#ifndef htons
#define htons(x) ((uint16_t)(((uint16_t)(x) >> 8u) | ((uint16_t)(x) << 8u)))
#endif

#ifndef htonl
#define htonl(x) ((uint32_t)( \
    (((uint32_t)(x) & 0xFF000000u) >> 24u) | \
    (((uint32_t)(x) & 0x00FF0000u) >>  8u) | \
    (((uint32_t)(x) & 0x0000FF00u) <<  8u) | \
    (((uint32_t)(x) & 0x000000FFu) << 24u) ))
#endif


#define SEC_IN_NS (1000000000llu)
#define SEC_IN_US (1000000u)
#define SEC_IN_MS (1000u)

#define PTP_ETHER_TYPE_H 0x88u
#define PTP_ETHER_TYPE_L 0xF7u

/* LAN865x register addresses */
#define OA_IMASK1           (0x0000000Du)
#define OA_IMASK1_SEVM_Pos  28u
#define OA_IMASK1_SEVM_Msk  (1U << OA_IMASK1_SEVM_Pos)

#define PADCTRL             (0x000A0088u)
#define MAC_TISUBN          (0x0001006Fu)
#define MAC_TSH             (0x00010070u)
#define MAC_TSL             (0x00010074u)
#define MAC_TN              (0x00010075u)
#define MAC_TA              (0x00010076u)
#define MAC_TI              (0x00010077u)
#define PACYC               (0x000A021Fu)
#define PACTRL              (0x000A0220u)
#define EG0STNS             (0x000A0221u)
#define EG0STSECL           (0x000A0222u)
#define EG0STSECH           (0x000A0223u)
#define EG0PW               (0x000A0224u)
#define EG0IT               (0x000A0225u)
#define EG0CTL              (0x000A0226u)
#define PPSCTL              (0x000A0239u)

#define SEVINTEN            (0x000A023Au)
#define SEVINTEN_PPSDONE_Pos 30u
#define SEVINTEN_PPSDONE_Msk (1u << SEVINTEN_PPSDONE_Pos)

#define EG0CTL_START        (1u << 0)
#define EG0CTL_STOP         (1u << 1)
#define EG0CTL_AH           (1u << 2)
#define EG0CTL_REP          (1u << 3)
#define EG0CTL_ISREL        (1u << 4)

#define UNINIT      0
#define MATCHFREQ   1
#define HARDSYNC    2
#define COARSE      3
#define FINE        4

#define PTP_SYNC_INTERVAL       500u
#define PTP_ANNOUNCE_INTERVAL   1000u

#define MATCHFREQ_RESET_THRESHOLD   100000000
#define HARDSYNC_RESET_THRESHOLD    0x3FFFFFFFu
#define HARDSYNC_THRESHOLD          0xFFFFFF
#define HARDSYNC_COARSE_THRESHOLD   90
#define HARDSYNC_FINE_THRESHOLD     50

typedef enum
{
    PTP_DISABLED,
    PTP_MASTER,
    PTP_SLAVE
} ptpMode_t;

typedef enum
{
    MSG_SYNC          = 0x00,
    MSG_DELAY_REQ     = 0x01,
    MSG_PDELAY_REQ    = 0x02,
    MSG_PDELAY_RESP   = 0x03,
    MSG_FOLLOW_UP     = 0x08,
    MSG_DELAY_RESP    = 0x09,
    MSG_PDELAY_RESP_FUP = 0x0A,
    MSG_ANNOUNCE      = 0x0B
} ptpMsgType_t;

typedef struct
{
    uint16_t secondsMsb;
    uint32_t secondsLsb;
    uint32_t nanoseconds;
    uint64_t correctionField;
} timeStamp_t;

#pragma pack(1)

typedef struct {
    uint8_t destMacAddr[6];
    uint8_t srcMacAddr[6];
    uint8_t ethType[2];
} ethHeader_t;

typedef uint8_t clockIdentity_t[8];

typedef struct
{
    uint16_t secondsMsb;
    uint32_t secondsLsb;
    uint32_t nanoseconds;
} ptpTimeStamp_t;

typedef struct
{
    clockIdentity_t clockIdentity;
    uint16_t        portNumber;
} portIdentity_t;

typedef struct
{
    uint8_t  clockClass;
    uint8_t  clockAccuracy;
    uint16_t offsetScaledLogVariance;
} clockQuality_t;

typedef struct
{
    uint16_t        tlvType;
    uint16_t        lengthField;
    clockIdentity_t pathSequence;
} tlv_t;

typedef struct
{
    uint16_t tlvType;
    uint16_t lengthField;
    uint8_t  organizationId[3];
    uint8_t  organizationSubType[3];
    uint32_t cumulativescaledRateOffset;
    uint16_t gmTimeBaseIndicator;
    uint8_t  lastGmPhaseChange[12];
    uint32_t scaledLastGmFreqChange;
} tlv_followUp_t;

typedef struct
{
    uint8_t         tsmt;
    uint8_t         version;
    uint16_t        messageLength;
    uint8_t         domainNumber;
    uint8_t         reserved2;
    uint8_t         flags[2];
    int64_t         correctionField;
    uint8_t         reserved3[4];
    portIdentity_t  sourcePortIdentity;
    uint16_t        sequenceID;
    uint8_t         controlField;
    uint8_t         logMessageInterval;
} ptpHeader_t;

typedef struct
{
    ptpHeader_t    header;
    ptpTimeStamp_t originTimestamp;
} announceMsg_t;

typedef struct
{
    ptpHeader_t    header;
    ptpTimeStamp_t originTimestamp;
} syncMsg_t;

typedef struct
{
    ptpHeader_t      header;
    ptpTimeStamp_t   preciseOriginTimestamp;
    tlv_followUp_t   tlv;
} followUpMsg_t;

typedef struct
{
    ptpHeader_t    header;
    ptpTimeStamp_t originTimestamp;
    uint8_t        reserved[10];
} pdelayReqMsg_t;

typedef struct
{
    ptpHeader_t    header;
    ptpTimeStamp_t receiveReceiptTimestamp;
    portIdentity_t requestingPortIdentity;
} pdelayRespMsg_t;

typedef struct
{
    ptpHeader_t    header;
    ptpTimeStamp_t responseOriginTimestamp;
    portIdentity_t requestingPortIdentity;
} pdelayRespFollowUpMsg_t;
#pragma pack()

typedef struct
{
    timeStamp_t origin;
    timeStamp_t origin_prev;
    timeStamp_t receipt;
    timeStamp_t receipt_prev;
} ptpSync_ct;

/*
 * Public API
 */

/** Initialise the PTP FOL follower (call once after the TCPIP stack is up). */
void PTP_FOL_Init(void);

/** Get current PTP operating mode. */
ptpMode_t PTP_FOL_GetMode(void);

/** Set PTP operating mode.  Setting PTP_SLAVE also resets the follower servo. */
void PTP_FOL_SetMode(ptpMode_t mode);

/**
 * Feed a received Ethernet frame into the PTP engine.
 * Only active when mode == PTP_SLAVE; frames are ignored in other modes.
 *
 * pData        - Pointer to the start of the Ethernet frame (MAC header included).
 * len          - Total frame length in bytes.
 * rxTimestamp  - 64-bit hardware timestamp from g_ptp_rx_ts (0 if not available).
 *                Format: Bit[63:32] = seconds, Bit[31:0] = nanoseconds.
 */
void PTP_FOL_OnFrame(const uint8_t *pData, uint16_t len, uint64_t rxTimestamp);

/* Internal helpers kept visible for debugging */
uint64_t tsToInternal(const timeStamp_t *ts);
void     handlePtp(uint8_t *pData, uint32_t size, uint32_t sec, uint32_t nsec);

/** Get current follower offset.
 *  pOffset    - receives signed offset in nanoseconds (may be NULL).
 *  pOffsetAbs - receives absolute offset in nanoseconds (may be NULL).
 */
void PTP_FOL_GetOffset(int64_t *pOffset, uint64_t *pOffsetAbs);

/** Reset follower servo to UNINIT state (same effect as SetMode(PTP_SLAVE)). */
void PTP_FOL_Reset(void);

/**
 * Service function for PTP Follower - must be called periodically (e.g., every 1ms).
 * Handles sequential register writes to LAN865x with proper synchronization.
 * Only active when mode == PTP_SLAVE.
 */
void PTP_FOL_Service(void);

#endif /* PTP_FOL_TASK_H */
