//DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.
Adapted from noIP-SAM-E54-Curiosity-PTP-Follower/ptp_task.c for use with the
Harmony TCP/IP stack (T1S 100BaseT Bridge project).

Key differences vs. the noIP version:
  - TC6_WriteRegister() replaced by DRV_LAN865X_WriteRegister() (fire-and-forget).
  - TC6_Service() blocking loops removed (the Harmony driver handles service internally).
  - get_macPhy_inst() / TC6_t* macPhy removed (use driver index 0 instead).
  - printf replaced with SYS_CONSOLE_PRINT.
  - ptpTask() renamed to PTP_FOL_Init().
  - PTP_FOL_OnFrame() added as the entry point from pktEth0Handler().
*/
//DOM-IGNORE-END

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "PTP_FOL_task.h"
#include "filters.h"
#include "config/default/driver/lan865x/drv_lan865x.h"
#include "config/default/system/console/sys_console.h"

#define PTP_LOG SYS_CONSOLE_PRINT

/* -------------------------------------------------------------------------
 * State machine for sequential register writes
 * ---------------------------------------------------------------------- */

typedef enum {
    FOL_REG_IDLE,
    FOL_REG_WRITE_TSL,
    FOL_REG_WAIT_TSL,
    FOL_REG_WRITE_TN,
    FOL_REG_WAIT_TN,
    FOL_REG_WRITE_PPSCTL,
    FOL_REG_WAIT_PPSCTL,
    FOL_REG_WRITE_TISUBN,
    FOL_REG_WAIT_TISUBN,
    FOL_REG_WRITE_TI,
    FOL_REG_WAIT_TI,
    FOL_REG_WRITE_TA,
    FOL_REG_WAIT_TA,
    FOL_REG_DONE
} fol_reg_state_t;

typedef enum {
    FOL_ACTION_NONE,
    FOL_ACTION_HARD_SYNC,
    FOL_ACTION_ENABLE_PPS,
    FOL_ACTION_SET_CLOCK_INC,
    FOL_ACTION_ADJUST_OFFSET
} fol_action_t;

typedef struct {
    uint32_t tsl_value;
    uint32_t tn_value;
    uint32_t tisubn_value;
    uint32_t ti_value;
    uint32_t ta_value;
} fol_reg_values_t;

static fol_reg_state_t  fol_reg_state         = FOL_REG_IDLE;
static fol_action_t     fol_pending_action    = FOL_ACTION_NONE;
static fol_reg_values_t fol_reg_values        = {0u, 0u, 0u, 0u, 0u};
static volatile bool    fol_reg_write_complete = false;
static uint32_t         fol_reg_timeout       = 0u;

/* -------------------------------------------------------------------------
 * Globals (mirror of ptp_task.c)
 * ---------------------------------------------------------------------- */

ptpSync_ct      TS_SYNC;

static ptpMode_t ptpMode          = PTP_DISABLED;
static int32_t   ptp_sync_sequenceId = -1;
static uint8_t   syncReceived     = 0;
static bool      wallClockSet     = false;

volatile double rateRatio         = 1.0;
volatile double rateRatioIIR      = 1.0;
volatile double rateRatioFIR      = 1.0;
volatile double offsetIIR         = 0;
volatile double offsetFIR         = 0;

static uint8_t  ptpSynced         = 0;
static uint8_t  syncStatus        = UNINIT;
static uint32_t runs              = 0;
static uint64_t diffLocal         = 0;
static uint64_t diffRemote        = 0;

volatile int64_t  offset          = 0;
volatile uint64_t offset_abs      = 0;
volatile int      hardResync      = 0;

static double   rateRatioValue[FIR_FILER_SIZE]         = {0};
static lpfStateF rateRatiolpfState;

/* Calibrated TI/TISUBN saved at first UNINIT->MATCHFREQ; reused after GM restart
 * so that the 16-frame UNINIT re-measurement (which gives rateRatioFIR~1.0 because
 * the LAN865x clock is already at the calibrated rate) does not overwrite the
 * correct crystal-compensated increment value. */
static uint32_t calibratedTI_value     = 0u;
static uint32_t calibratedTISUBN_value = 0u;

static int32_t  offsetValue[FIR_FILER_SIZE_FINE]       = {0};
static lpfState offsetState;

static int32_t  offsetCoarseValue[FIR_FILER_SIZE_FINE] = {0};
static lpfState offsetCoarseState;

long double continiousratio = 1.0;
static int32_t  __attribute__((unused)) diff        = 0;
static int32_t  __attribute__((unused)) filteredDiff= 0;
long double corrNs          = 0.0;
long double corrNsFlt       = 0.0;

/* -------------------------------------------------------------------------
 * Register-write callback and service function
 * ---------------------------------------------------------------------- */

static void fol_reg_write_callback(void *reserved1, bool success, uint32_t addr,
                                   uint32_t value, void *pTag, void *reserved2)
{
    (void)reserved1; (void)addr; (void)value; (void)pTag; (void)reserved2;
    if (success) {
        fol_reg_write_complete = true;
    } else {
        PTP_LOG("[FOL] Register write failed: addr=0x%08X\r\n", (unsigned int)addr);
        fol_reg_state      = FOL_REG_IDLE;
        fol_pending_action = FOL_ACTION_NONE;
    }
}

#define FOL_REG_TIMEOUT_MS 100u

void PTP_FOL_Service(void)
{
    if (ptpMode != PTP_SLAVE) {
        return;
    }

    switch (fol_reg_state) {
        case FOL_REG_IDLE:
            if (fol_pending_action != FOL_ACTION_NONE) {
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                fol_reg_state   = FOL_REG_WRITE_TSL;
            }
            break;

        case FOL_REG_WRITE_TSL:
            if (fol_pending_action == FOL_ACTION_HARD_SYNC) {
                fol_reg_write_complete = false;
                (void)DRV_LAN865X_WriteRegister(0u, MAC_TSL, fol_reg_values.tsl_value,
                                                true, fol_reg_write_callback, NULL);
                fol_reg_state = FOL_REG_WAIT_TSL;
            } else {
                fol_reg_state = FOL_REG_WRITE_PPSCTL;
            }
            break;

        case FOL_REG_WAIT_TSL:
            if (fol_reg_write_complete) {
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                fol_reg_state   = FOL_REG_WRITE_TN;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for MAC_TSL write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_WRITE_TN:
            fol_reg_write_complete = false;
            fol_reg_timeout = FOL_REG_TIMEOUT_MS;
            (void)DRV_LAN865X_WriteRegister(0u, MAC_TN, fol_reg_values.tn_value,
                                            true, fol_reg_write_callback, NULL);
            fol_reg_state = FOL_REG_WAIT_TN;
            break;

        case FOL_REG_WAIT_TN:
            if (fol_reg_write_complete) {
                PTP_LOG("[FOL] Hard sync completed\r\n");
                fol_reg_state = FOL_REG_DONE;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for MAC_TN write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_WRITE_PPSCTL:
            if (fol_pending_action == FOL_ACTION_ENABLE_PPS) {
                fol_reg_write_complete = false;
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                (void)DRV_LAN865X_WriteRegister(0u, PPSCTL, 0x000007Du,
                                                true, fol_reg_write_callback, NULL);
                fol_reg_state = FOL_REG_WAIT_PPSCTL;
            } else {
                fol_reg_state = FOL_REG_WRITE_TISUBN;
            }
            break;

        case FOL_REG_WAIT_PPSCTL:
            if (fol_reg_write_complete) {
                PTP_LOG("[FOL] 1PPS output enabled\r\n");
                fol_reg_state = FOL_REG_DONE;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for PPSCTL write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_WRITE_TISUBN:
            if (fol_pending_action == FOL_ACTION_SET_CLOCK_INC) {
                fol_reg_write_complete = false;
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                (void)DRV_LAN865X_WriteRegister(0u, MAC_TISUBN, fol_reg_values.tisubn_value,
                                                true, fol_reg_write_callback, NULL);
                fol_reg_state = FOL_REG_WAIT_TISUBN;
            } else {
                fol_reg_state = FOL_REG_WRITE_TA;
            }
            break;

        case FOL_REG_WAIT_TISUBN:
            if (fol_reg_write_complete) {
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                fol_reg_state   = FOL_REG_WRITE_TI;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for MAC_TISUBN write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_WRITE_TI:
            fol_reg_write_complete = false;
            fol_reg_timeout = FOL_REG_TIMEOUT_MS;
            (void)DRV_LAN865X_WriteRegister(0u, MAC_TI, fol_reg_values.ti_value,
                                            true, fol_reg_write_callback, NULL);
            fol_reg_state = FOL_REG_WAIT_TI;
            break;

        case FOL_REG_WAIT_TI:
            if (fol_reg_write_complete) {
                PTP_LOG("[FOL] Clock increment set: TI=%u TISUBN=0x%08X\r\n",
                        (unsigned int)fol_reg_values.ti_value,
                        (unsigned int)fol_reg_values.tisubn_value);
                fol_reg_state = FOL_REG_DONE;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for MAC_TI write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_WRITE_TA:
            if (fol_pending_action == FOL_ACTION_ADJUST_OFFSET) {
                fol_reg_write_complete = false;
                fol_reg_timeout = FOL_REG_TIMEOUT_MS;
                (void)DRV_LAN865X_WriteRegister(0u, MAC_TA, fol_reg_values.ta_value,
                                                true, fol_reg_write_callback, NULL);
                fol_reg_state = FOL_REG_WAIT_TA;
            } else {
                fol_reg_state = FOL_REG_DONE;
            }
            break;

        case FOL_REG_WAIT_TA:
            if (fol_reg_write_complete) {
                fol_reg_state = FOL_REG_DONE;
            } else if (--fol_reg_timeout == 0u) {
                PTP_LOG("[FOL] Timeout waiting for MAC_TA write\r\n");
                fol_reg_state      = FOL_REG_IDLE;
                fol_pending_action = FOL_ACTION_NONE;
            }
            break;

        case FOL_REG_DONE:
            fol_reg_state      = FOL_REG_IDLE;
            fol_pending_action = FOL_ACTION_NONE;
            break;
    }
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Full 64-bit byte swap using GCC builtin (XC32 / ARM Cortex-M4 supported) */
static uint64_t BSWAP64(uint64_t rawValue)
{
    uint32_t high = (uint32_t)((rawValue >> 32u) & 0xFFFFFFFFu);
    uint32_t low  = (uint32_t)((rawValue >>  0u) & 0xFFFFFFFFu);
    return (((uint64_t)__builtin_bswap32(low)) << 32u) |
            ((uint64_t)__builtin_bswap32(high));
}

static int64_t getCorrectionField(ptpHeader_t *hdr)
{
    return (int64_t)(BSWAP64((uint64_t)hdr->correctionField) >> 16);
}

uint64_t tsToInternal(const timeStamp_t *ts)
{
    uint64_t seconds = ((uint64_t)ts->secondsMsb << 32u) | ts->secondsLsb;
    return (seconds * SEC_IN_NS) + ts->nanoseconds;
}

/* -------------------------------------------------------------------------
 * Slave-node reset
 * ---------------------------------------------------------------------- */

static void resetSlaveNode(void)
{
    PTP_LOG("GM_RESET -> Slave node reset initiated due to sequence ID mismatch\r\n");

    ptp_sync_sequenceId = -1;
    syncReceived        = 0;
    wallClockSet        = false;
    ptpSynced           = 0;
    runs                = 0;
    hardResync          = 1;    /* force hard-sync on first FollowUp after reset */
    diffLocal           = 0;
    diffRemote          = 0;

    memset(&TS_SYNC, 0, sizeof(ptpSync_ct));

    for (uint32_t x = 0; x < FIR_FILER_SIZE_FINE; x++) {
        firLowPassFilter(0, &offsetCoarseState);
        firLowPassFilter(0, &offsetState);
    }
    for (uint32_t x = 0; x < FIR_FILER_SIZE; x++) {
        firLowPassFilterF(1.0, &rateRatiolpfState);
    }

    if (calibratedTI_value != 0u) {
        /* Fast-reset path: LAN865x TI/TISUBN registers already hold the
         * calibrated crystal-compensation value — re-apply them and skip
         * the 16-frame UNINIT re-measurement that would produce
         * rateRatioFIR~1.0 (= uncompensated, 5ppm drift per frame). */
        fol_reg_values.ti_value     = calibratedTI_value;
        fol_reg_values.tisubn_value = calibratedTISUBN_value;
        fol_pending_action          = FOL_ACTION_SET_CLOCK_INC;
        syncStatus                  = MATCHFREQ;   /* jump straight to MATCHFREQ */
        PTP_LOG("GM_RESET: reusing calibrated TI=%u TISUBN=0x%08X\r\n",
                (unsigned int)calibratedTI_value,
                (unsigned int)calibratedTISUBN_value);
    } else {
        syncStatus = UNINIT;   /* first boot: normal calibration needed */
    }

    PTP_FOL_Init();
}

/* -------------------------------------------------------------------------
 * PTP message processors
 * ---------------------------------------------------------------------- */

static void processSync(syncMsg_t *ptpPkt)
{
    uint16_t seqId = htons(ptpPkt->header.sequenceID);

    if (ptp_sync_sequenceId < 0) {
        ptp_sync_sequenceId = seqId;
        syncReceived        = 0;
    } else {
        int sequenceDifference = abs((int)seqId - (int)ptp_sync_sequenceId);
        if (sequenceDifference > 10) {
            PTP_LOG("Large sequence mismatch: %u vs %d. Resetting...\r\n",
                    (unsigned int)seqId, (int)ptp_sync_sequenceId);
            resetSlaveNode();
        } else if ((int)ptp_sync_sequenceId == (int)seqId) {
            syncReceived = 1;
        } else {
            syncReceived = 0;
            PTP_LOG("Sync seqId mismatch. Is: %u - %d\r\n",
                    (unsigned int)seqId, (int)ptp_sync_sequenceId);
            ptp_sync_sequenceId = -1;
        }
    }
}

static void processFollowUp(followUpMsg_t *ptpPkt)
{
    uint16_t seqId = htons(ptpPkt->header.sequenceID);

    if (ptp_sync_sequenceId >= 0 && syncReceived) {
        if (ptp_sync_sequenceId == (int)seqId) {
            ptp_sync_sequenceId = (ptp_sync_sequenceId + 1) % (int)UINT16_MAX;
            syncReceived = 0;
        } else {
            PTP_LOG("FollowUp seqId mismatch. Is: %u - %d\r\n",
                    (unsigned int)seqId, (int)ptp_sync_sequenceId);
            ptp_sync_sequenceId = -1;
            memset(&TS_SYNC.receipt,      0, sizeof(ptpTimeStamp_t));
            memset(&TS_SYNC.receipt_prev, 0, sizeof(ptpTimeStamp_t));
            return;
        }
    } else {
        ptp_sync_sequenceId = ((int)seqId + 1) % (int)UINT16_MAX;
        PTP_LOG("FollowUp seqId out of sync. Is: %u - %d\r\n",
                (unsigned int)seqId, (int)ptp_sync_sequenceId);
        return;
    }

    /* Extract t1 from PTP frame */
    TS_SYNC.origin.secondsMsb  = htons(ptpPkt->preciseOriginTimestamp.secondsMsb);
    TS_SYNC.origin.secondsLsb  = htonl(ptpPkt->preciseOriginTimestamp.secondsLsb);
    TS_SYNC.origin.nanoseconds = htonl(ptpPkt->preciseOriginTimestamp.nanoseconds);
    TS_SYNC.origin.correctionField = (uint64_t)getCorrectionField(&ptpPkt->header);

    /* Hard sync: set the local wall clock directly to the GM timestamp */
    if (hardResync) {
        fol_reg_values.tsl_value = TS_SYNC.origin.secondsLsb;
        fol_reg_values.tn_value  = TS_SYNC.origin.nanoseconds;
        fol_pending_action = FOL_ACTION_HARD_SYNC;
        PTP_LOG("Large offset, scheduling hard sync\r\n");
        hardResync = 0;
    }

    /* Enable 1PPS output once the clock is synced */
    if (ptpSynced && !wallClockSet) {
        fol_pending_action = FOL_ACTION_ENABLE_PPS;
        wallClockSet = true;
    }

    /* Convert to internal ns representation */
    uint64_t t1 = tsToInternal(&TS_SYNC.origin);
    uint64_t t2 = tsToInternal(&TS_SYNC.receipt);

    if (TS_SYNC.receipt_prev.secondsLsb != 0u) {
        uint64_t curr = t2;
        uint64_t prev = tsToInternal(&TS_SYNC.receipt_prev);
        diffLocal = curr - prev;
    }

    if (TS_SYNC.origin_prev.secondsLsb != 0u) {
        uint64_t curr = t1;
        uint64_t prev = tsToInternal(&TS_SYNC.origin_prev);
        diffRemote = curr - prev;
    }

    TS_SYNC.receipt_prev = TS_SYNC.receipt;
    TS_SYNC.origin_prev  = TS_SYNC.origin;

    /* Rate-ratio estimation */
    if (diffLocal && diffRemote) {
        if (syncStatus == UNINIT || syncStatus > HARDSYNC) {
            rateRatio = (double)diffRemote / (double)diffLocal;
            if (rateRatio > 0.998 && rateRatio < 1.002) {
                rateRatioIIR = lowPassExponential((double)diffRemote / (double)diffLocal,
                                                  rateRatio, 0.5);
                rateRatioFIR = firLowPassFilterF((double)diffRemote / (double)diffLocal,
                                                 &rateRatiolpfState);
            } else {
                PTP_LOG("Filtered rateRatio outlier\r\n");
            }
        }
        runs++;
    } else {
        PTP_LOG("!");
        return;
    }

    offset     = (int64_t)t2 - (int64_t)t1;
    uint8_t neg = (offset < 0) ? 0u : 1u;
    offset_abs  = (uint64_t)llabs(offset);

    /* ---- Clock servo state machine ---- */
    if (syncStatus == UNINIT) {
        if (runs >= (FIR_FILER_SIZE * 1u)) {
            double calcInc = CLOCK_CYCLE_NS * rateRatioFIR;
            uint8_t mac_ti = (uint8_t)calcInc;
            double calcSubInc = calcInc - (double)mac_ti;
            calcSubInc *= 16777216.0;
            uint32_t calcSubInc_uint = (uint32_t)calcSubInc;
            calcSubInc_uint = ((calcSubInc_uint >> 8) & 0xFFFFu)
                            | ((calcSubInc_uint & 0xFFu) << 24);

            fol_reg_values.tisubn_value = calcSubInc_uint;
            fol_reg_values.ti_value     = (uint32_t)mac_ti;
            fol_pending_action = FOL_ACTION_SET_CLOCK_INC;
            PTP_LOG("PTP UNINIT->MATCHFREQ  scheduling TI=%u TISUBN=0x%08X\r\n",
                    (unsigned int)mac_ti, (unsigned int)calcSubInc_uint);

            /* Save calibrated values for reuse after any subsequent GM restart */
            calibratedTI_value     = (uint32_t)mac_ti;
            calibratedTISUBN_value = calcSubInc_uint;

            syncStatus = MATCHFREQ;
            ptpSynced  = 1;
            runs       = 0;
        }

    } else if (syncStatus == MATCHFREQ) {
        if (offset_abs > MATCHFREQ_RESET_THRESHOLD) {
            hardResync = 1;
        } else {
            PTP_LOG("[PTP-DBG] MATCHFREQ->HARDSYNC offset=%d abs=%llu\r\n",
                    (int)offset, (unsigned long long)offset_abs);
            syncStatus = HARDSYNC;
            ptpSynced  = 1;   /* ensure PPS is re-enabled on next frame (fast-reset path) */
        }

    } else if (syncStatus >= HARDSYNC) {
        if (offset_abs > HARDSYNC_RESET_THRESHOLD) {
            syncStatus = UNINIT;
            for (uint32_t x = 0; x < FIR_FILER_SIZE_FINE; x++) {
                (void)firLowPassFilter(0, &offsetCoarseState);
                (void)firLowPassFilter(0, &offsetState);
            }
            for (uint32_t x = 0; x < FIR_FILER_SIZE; x++) {
                (void)firLowPassFilterF(1.0, &rateRatiolpfState);
            }
            runs = 0;

        } else if (offset_abs > HARDSYNC_THRESHOLD) {
            PTP_LOG("[PTP-DBG] HARDSYNC big offset=%d abs=%llu\r\n",
                    (int)offset, (unsigned long long)offset_abs);
            offset_abs = HARDSYNC_THRESHOLD;
            fol_reg_values.ta_value = ((neg & 1u) << 31) | (uint32_t)offset_abs;
            fol_pending_action = FOL_ACTION_ADJUST_OFFSET;
            syncStatus = HARDSYNC;

        } else if (offset_abs > HARDSYNC_COARSE_THRESHOLD) {
            PTP_LOG("[PTP-DBG] HARDSYNC coarse offset=%d abs=%llu\r\n",
                    (int)offset, (unsigned long long)offset_abs);
            for (uint32_t x = 0; x < FIR_FILER_SIZE_FINE; x++) {
                (void)firLowPassFilter(0, &offsetCoarseState);
                (void)firLowPassFilter(0, &offsetState);
                offsetCoarseState.filled = 0;
                offsetState.filled       = 0;
            }
            fol_reg_values.ta_value = ((neg & 1u) << 31) | (uint32_t)offset_abs;
            fol_pending_action = FOL_ACTION_ADJUST_OFFSET;
            syncStatus = HARDSYNC;

        } else if (offset_abs > HARDSYNC_FINE_THRESHOLD) {
            for (uint32_t x = 0; x < FIR_FILER_SIZE_FINE; x++) {
                (void)firLowPassFilter(0, &offsetState);
                offsetState.filled = 0;
            }
            offsetFIR = firLowPassFilter((int32_t)offset, &offsetCoarseState);
            neg = (offsetFIR < 0) ? 0u : 1u;
            int32_t write_val = (int32_t)offsetFIR;
            if (!neg) write_val = write_val * (-1);

            fol_reg_values.ta_value = ((neg & 1u) << 31) | (uint32_t)write_val;
            fol_pending_action = FOL_ACTION_ADJUST_OFFSET;
            syncStatus = COARSE;
            PTP_LOG("PTP COARSE  offset=%d val=%d\r\n",
                    (int)offset, (int)write_val);

        } else {
            offsetFIR = firLowPassFilter((int32_t)offset, &offsetState);
            neg = (offsetFIR < 0) ? 0u : 1u;
            int32_t write_val = (int32_t)offsetFIR;
            if (!neg) write_val = write_val * (-1);

            fol_reg_values.ta_value = ((neg & 1u) << 31) | (uint32_t)write_val;
            fol_pending_action = FOL_ACTION_ADJUST_OFFSET;
            syncStatus = FINE;
            PTP_LOG("PTP FINE    offset=%d val=%d\r\n",
                    (int)offset, (int)write_val);
        }
    }
}

/* -------------------------------------------------------------------------
 * PTP frame dispatcher  (called internally & exposed via header)
 * ---------------------------------------------------------------------- */

void handlePtp(uint8_t *pData, uint32_t size, uint32_t sec, uint32_t nsec)
{
    (void)size;
    ptpHeader_t *ptpPkt = (ptpHeader_t *)(pData + sizeof(ethHeader_t));
    uint8_t messageType = ptpPkt->tsmt & 0x0Fu;

    if (messageType == (uint8_t)MSG_FOLLOW_UP) {
        processFollowUp((followUpMsg_t *)ptpPkt);
    } else if (messageType == (uint8_t)MSG_SYNC) {
        processSync((syncMsg_t *)ptpPkt);
        if (syncReceived) {
            TS_SYNC.receipt_prev        = TS_SYNC.receipt;
            TS_SYNC.receipt.secondsLsb  = sec;
            TS_SYNC.receipt.nanoseconds = nsec;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void PTP_FOL_Init(void)
{
    /* Set up PPS output (stopped; PPSCTL=0x02 = pulse-width / period preset) */
    DRV_LAN865X_WriteRegister(0u, PPSCTL,   0x00000002u,       true, NULL, NULL);
    DRV_LAN865X_WriteRegister(0u, SEVINTEN, SEVINTEN_PPSDONE_Msk, true, NULL, NULL);

    memset(&TS_SYNC, 0, sizeof(TS_SYNC));

    rateRatiolpfState.buffer     = &rateRatioValue[0];
    rateRatiolpfState.filterSize = sizeof(rateRatioValue) / sizeof(rateRatioValue[0]);

    offsetState.buffer           = &offsetValue[0];
    offsetState.filterSize       = sizeof(offsetValue) / sizeof(offsetValue[0]);

    offsetCoarseState.buffer     = &offsetCoarseValue[0];
    offsetCoarseState.filterSize = sizeof(offsetCoarseValue) / sizeof(offsetCoarseValue[0]);

    PTP_LOG("PTP_FOL_Init: HW init done, PTP mode=%d (not activated)\r\n", (int)ptpMode);
}

ptpMode_t PTP_FOL_GetMode(void)
{
    return ptpMode;
}

void PTP_FOL_SetMode(ptpMode_t mode)
{
    ptpMode = mode;
    if (mode == PTP_SLAVE) {
        resetSlaveNode();
    }
}

void PTP_FOL_GetOffset(int64_t *pOffset, uint64_t *pOffsetAbs)
{
    if (pOffset)    *pOffset    = offset;
    if (pOffsetAbs) *pOffsetAbs = offset_abs;
}

void PTP_FOL_Reset(void)
{
    resetSlaveNode();
}

void PTP_FOL_OnFrame(const uint8_t *pData, uint16_t len, uint64_t rxTimestamp)
{
    if (ptpMode != PTP_SLAVE) {
        return;
    }
    uint32_t sec  = (uint32_t)((rxTimestamp >> 32u) & 0xFFFFFFFFu);
    uint32_t nsec = (uint32_t)( rxTimestamp         & 0xFFFFFFFFu);
    handlePtp((uint8_t *)pData, (uint32_t)len, sec, nsec);
}
