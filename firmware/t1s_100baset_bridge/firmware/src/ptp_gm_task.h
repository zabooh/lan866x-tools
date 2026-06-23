//DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.
PTP Grandmaster task — header for T1S 100BaseT Bridge (ATSAME54P20A / LAN865x).
Ported from noIP-SAM-E54-Curiosity-PTP-Grandmaster/firmware/src/ptp.h.
*/
//DOM-IGNORE-END

#ifndef PTP_GM_TASK_H
#define PTP_GM_TASK_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	PTP_GM_DST_MULTICAST = 0,
	PTP_GM_DST_BROADCAST = 1
} ptp_gm_dst_mode_t;

/* ---- TX Match registers (GM only) ---- */
#define GM_TXMCTL       (0x00040040u)
#define GM_TXMPATH      (0x00040041u)
#define GM_TXMPATL      (0x00040042u)
#define GM_TXMMSKH      (0x00040043u)
#define GM_TXMMSKL      (0x00040044u)
#define GM_TXMLOC       (0x00040045u)

/* ---- OA registers used by GM ---- */
#define GM_OA_CONFIG0   (0x00000004u)
#define GM_OA_STATUS0   (0x00000008u)

/* ---- Vendor/PHY registers used during GM init ---- */
#define GM_PADCTRL      (0x000A0088u)   /* Pad control register (RMW in init)  */

/* ---- RMW bit masks for GM init sequence ---- */
/* OA_CONFIG0: set Bits 7 (FTSE) and 6 — same as reference TC6_ptp_master_init */
#define GM_OA_CONFIG0_RMW_VALUE     (0x000000C0u)
#define GM_OA_CONFIG0_RMW_MASK      (0x000000C0u)
/* PADCTRL: set Bit 8, clear Bit 9 — same as reference TC6_ptp_master_init */
#define GM_PADCTRL_RMW_VALUE        (0x00000100u)
#define GM_PADCTRL_RMW_MASK         (0x00000300u)

/* ---- TX Timestamp Capture A/B/C ---- */
#define GM_OA_TTSCAH    (0x00000010u)   /* Capture A: seconds   */
#define GM_OA_TTSCAL    (0x00000011u)   /* Capture A: nanosecs  */
#define GM_OA_TTSCBH    (0x00000012u)   /* Capture B: seconds   */
#define GM_OA_TTSCBL    (0x00000013u)   /* Capture B: nanosecs  */
#define GM_OA_TTSCCH    (0x00000014u)   /* Capture C: seconds   */
#define GM_OA_TTSCCL    (0x00000015u)   /* Capture C: nanosecs  */

/* ---- Status / control bit masks ---- */
#define GM_TXMCTL_TXPMDET   (0x0080u)   /* TX Packet Match Detected (RO)     */
#define GM_TXMCTL_MACTXTSE  (0x0004u)   /* MAC TX Timestamp Enable           */
#define GM_TXMCTL_TXME      (0x0002u)   /* TX Match Enable                   */
#define GM_STS0_TTSCAA      (0x0100u)   /* Timestamp Capture Available A */
#define GM_STS0_TTSCAB      (0x0200u)   /* Timestamp Capture Available B */
#define GM_STS0_TTSCAC      (0x0400u)   /* Timestamp Capture Available C */

/* ---- Timing constants ---- */
#define PTP_GM_SYNC_PERIOD_MS   125u
#define PTP_GM_STATIC_OFFSET    7650u
#define PTP_GM_MAX_TN_VAL       0x3B9ACA00u   /* 1 000 000 000 ns */
#define PTP_GM_MAX_RETRIES      5u

/* ---- Optional LAN865x driver access switches ----
 * Default: disabled, so ptp_gm_task.c does not actively access the LAN865x
 * driver APIs unless these macros are overridden to 1 at compile time.
 */
#ifndef PTP_GM_USE_DRV_LAN865X_WRITEREGISTER
#define PTP_GM_USE_DRV_LAN865X_WRITEREGISTER    1
#endif

#ifndef PTP_GM_USE_DRV_LAN865X_READREGISTER
#define PTP_GM_USE_DRV_LAN865X_READREGISTER     1
#endif

#ifndef PTP_GM_USE_DRV_LAN865X_SENDRAWETHFRAME
#define PTP_GM_USE_DRV_LAN865X_SENDRAWETHFRAME  1
#endif

#ifndef PTP_GM_USE_DRV_LAN865X_GETANDCLEARTSCAPTURE
#define PTP_GM_USE_DRV_LAN865X_GETANDCLEARTSCAPTURE 1
#endif

/* Sync TX frame source mode:
 * 0 = send PTP Sync frame (EtherType 0x88F7)
 * 1 = send noIP test frame (EtherType 0x88B5) for A/B diagnostics
 */
#ifndef PTP_GM_SYNC_TX_MODE
#define PTP_GM_SYNC_TX_MODE 0
#endif

/* EtherType used by the PTP payload path (SYNC_TX_MODE == 0).
 * Default is standard PTP L2 EtherType 0x88F7.
 * For step-1 diagnostics this can be set to 0x88B5 to isolate EtherType effects
 * while keeping the same PTP payload format.
 */
#ifndef PTP_GM_PTP_ETHERTYPE
#define PTP_GM_PTP_ETHERTYPE 0x88F7u
#endif

/* Step-2 incremental PTP Sync frame profile:
 * 1 = Minimal valid Sync header (strict baseline)
 * 2 = Minimal + common timing/control fields
 * 3 = Legacy profile from previous implementation
 */
#ifndef PTP_GM_SYNC_BUILD_LEVEL
#define PTP_GM_SYNC_BUILD_LEVEL 3
#endif

/* ---- Public API ---- */

/** Initialise hardware (TX Match, MAC time increment, PADCTRL) and reset state.
 *  Must be called after the TCPIP stack is up and the LAN865x driver is ready.
 */
void PTP_GM_Init(void);

/** Service the GM state machine.  Must be called every 1 ms (from a timer callback). */
void PTP_GM_Service(void);

/** De-initialize GM runtime state and disarm TX-Match registers (best effort). */
void PTP_GM_Deinit(void);

/** Read-only status query. */
void PTP_GM_GetStatus(uint32_t *pSyncCount, uint32_t *pState);

/** Change the Sync burst interval (default 125 ms).  Valid range: 10 – 10000 ms. */
void PTP_GM_SetSyncInterval(uint32_t intervalMs);

/** Configure destination MAC mode for outgoing PTP frames. */
void PTP_GM_SetDstMode(ptp_gm_dst_mode_t mode);

/** Read current destination MAC mode for outgoing PTP frames. */
ptp_gm_dst_mode_t PTP_GM_GetDstMode(void);

/** Request a one-shot dump of all TX-Match registers via the GM state machine.
 *  Safe to call from any context. The dump is executed from PTP_GM_Service()
 *  so it never races with active SPI traffic (no TC6Error_SyncLost side-effect). */
void PTP_GM_RequestRegDump(void);

#endif /* PTP_GM_TASK_H */
