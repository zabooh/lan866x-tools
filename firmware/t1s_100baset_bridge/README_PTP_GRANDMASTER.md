# PTP Grandmaster Mode — Design & Implementation

**Project:** T1S 100BaseT Bridge — ATSAME54P20A / LAN865x  
**Reference:** `LAN865x-TimeSync/noIP-SAM-E54-Curiosity-PTP-Grandmaster/`  
**Status:** ✅ Implemented — build verified (make + CMake/ninja, XC32 v4.60, 2026-03-30)

---

## 1. Objective

Extend the T1S 100BaseT Bridge firmware with a hardware-assisted PTP Grandmaster (GM) mode
that is selectable at runtime via CLI.  After this change the firmware can operate in three modes:

| Mode | CLI arg | Description |
|------|---------|-------------|
| `PTP_DISABLED` | `off` | No PTP processing (default) |
| `PTP_SLAVE` | `follower` | Clock Follower — already implemented |
| `PTP_MASTER` | `master` | **Clock Grandmaster — new** |

The `ptpMode_t` enum and the `ptpMode` variable already exist in `PTP_FOL_task.c`; only
`PTP_MASTER` behaviour needs to be wired up.

---

## 2. Reference Implementation Analysis

### 2.1 Source

```
noIP-SAM-E54-Curiosity-PTP-Grandmaster/firmware/src/
    main.c       — PTP GM state machine (830 lines)
    ptp.h        — structs, constants, state enum
    tc6-noip.c   — TC6 transport layer
    libtc6.X/inc/tc6-regs.h — register address definitions
```

The reference runs as a bare-metal noIP application (no Harmony TCP/IP stack).  
The PTP state machine lives entirely in `main()` as a `switch` inside `while(true)`.

### 2.2 Grandmaster State Machine

Seven states, executed in a tight polling loop with 125 ms between each Sync burst:

```
PTP_STATE_send_sync
    │  Wait SYNC_MESSAGE_PERIOD_MS (125 ms) and txBusy==false
    │  Build syncMsg_t, copy into temp_buffer[0..14+44]
    │  Send with TSC=1 → TC6NoIP_SendEthernetPacket_TimestampA()
    ▼
PTP_STATE_get_tx_status
    │  Read TXMCTL register
    │  Advance when read completes
    ▼
PTP_STATE_get_oa_status0
    │  Check TXMCTL_TXPMDET (bit 7) in read value
    │  If set  → read OA_STATUS0
    │  If clear → back to send_sync (retry)
    ▼
PTP_STATE_get_timestamp_reg_sec
    │  Check OA_STS0_TTSCAA/B/C in OA_STATUS0 value
    │  Read matching OA_TTSCxH register (seconds)
    ▼
PTP_STATE_get_timestamp_reg_nsec
    │  Read matching OA_TTSCxL register (nanoseconds)
    ▼
PTP_STATE_clear_status_reg
    │  Store timestamp_nsec from previous read
    │  Write back OA_STATUS0 value to clear (W1C)
    ▼
PTP_STATE_send_followup
    │  Build followUpMsg_t with preciseOriginTimestamp:
    │    nsec = timestamp_nsec + STATIC_OFFSET (7650 ns)
    │    if nsec > 1 s → wrap: nsec -= 1e9, sec++
    │  Send without TSC
    ▼
    (back to PTP_STATE_send_sync)
```

### 2.3 Sending Sync with TSC=1

The critical difference between a Sync and any other frame is that the TC6 Data Header
must have **TSC=1** (Transmit Timestamp Capture A request) set:

```c
// Reference: tc6-noip.c
bool TC6NoIP_SendEthernetPacket_TimestampA(int8_t idx, const uint8_t *pTx,
                                           uint16_t len, TC6NoIP_OnTxCallback_t cb)
{
    return TC6_SendRawEthernetPacket(mlw[idx].tc.tc6, pTx, len,
                                     0x01,   // <-- txCaptureTimeStampA = 1
                                     (TC6_RawTxCallback_t)cb, ...);
}
```

When `txCaptureTimeStampA=1` the LAN865x hardware latches the SFD (start-of-frame)
transmission timestamp into `OA_TTSCAH` / `OA_TTSCAL`.

### 2.4 TX Match Configuration (Grandmaster Init)

Before the first Sync can be sent the TX Match mechanism must be enabled.  This tells the
LAN865x to detect the EtherType 0x88F7 pattern and set `TXMCTL_TXPMDET` when a matching
frame is transmitted:

| Register | Address | Write Value | Note |
|----------|---------|-------------|------|
| `TXMCTL`   | `0x00040040` | `0x02`          | Arm TX match / enable |
| `TXMPATH`  | `0x00040041` | `0x88`          | Pattern byte 0 of EtherType |
| `TXMPATL`  | `0x00040042` | `0xF710`        | Pattern byte 1 + next byte |
| `TXMMSKH`  | `0x00040043` | `0x00`          | No masking |
| `TXMMSKL`  | `0x00040044` | `0x00`          | No masking |
| `TXMLOC`   | `0x00040045` | `30`            | Byte offset within frame |
| `OA_CONFIG0` | `0x00000004` | RMW `0xC0/0xC0` | TX/RX cut-through |
| `PADCTRL`  | `0x000A0088` | RMW `0x100/0x300` | Pad timing |
| `MAC_TI`   | `0x00010077` | `40`            | 40 ns time increment |
| `PPSCTL`   | `0x000A0239` | `0x0000007D`    | PPS output (optional) |

All of these are issued with `TC6_WriteRegister` or `TC6_ReadModifyWriteRegister` in the
reference; in the bridge project they map to `DRV_LAN865X_WriteRegister` /
`DRV_LAN865X_ReadModifyWriteRegister`.

### 2.5 TX Timestamp Retrieval Registers

All addresses from `libtc6.X/inc/tc6-regs.h`:

```c
#define TXMCTL          (0x00040040u)
#define OA_CONFIG0      (0x00000004u)
#define OA_STATUS0      (0x00000008u)

/* Timestamp Capture A/B/C — High = seconds, Low = nanoseconds */
#define OA_TTSCAH       (0x00000010u)
#define OA_TTSCAL       (0x00000011u)
#define OA_TTSCBH       (0x00000012u)
#define OA_TTSCBL       (0x00000013u)
#define OA_TTSCCH       (0x00000014u)
#define OA_TTSCCL       (0x00000015u)

#define TXMCTL_TXPMDET  (0x0080u)   /* TX Packet Match Detected */
#define OA_STS0_TTSCAA  (0x0100u)   /* Timestamp Capture Available A */
#define OA_STS0_TTSCAB  (0x0200u)   /* Timestamp Capture Available B */
#define OA_STS0_TTSCAC  (0x0400u)   /* Timestamp Capture Available C */
```

> **NOTE — Address discrepancy:** The session summary quoted `OA_TTSCAH = 0x11` and
> `OA_TTSCAL = 0x12`.  The authoritative source `tc6-regs.h` defines `OA_TTSCAH = 0x10`
> and `OA_TTSCAL = 0x11`.  Use the `tc6-regs.h` values above.

### 2.6 Follow-Up Frame — Timestamp Correction

```c
// STATIC_OFFSET = 7650 ns  (static TX path compensation from reference)
// MAX_MAC_TN_VAL = 0x3B9ACA00  (1 000 000 000 ns = 1 second)

uint32_t nsec = timestamp_nsec + STATIC_OFFSET;
if (nsec > MAX_MAC_TN_VAL) {
    nsec -= MAX_MAC_TN_VAL;
    sec++;
}
// Store as big-endian in followUpMsg_t.preciseOriginTimestamp
msg.preciseOriginTimestamp.secondsLsb = invert_uint32(sec);
msg.preciseOriginTimestamp.nanoseconds = invert_uint32(nsec);
```

### 2.7 PTP Frame Formats

Both `syncMsg_t` and `followUpMsg_t` structs are already defined in `PTP_FOL_task.h`
(identical to the GM reference `ptp.h`).  The Ethernet header is 14 bytes:

```
Destination MAC:  01:80:C2:00:00:0E  (PTP Layer-2 multicast)
Source MAC:       <board MAC — read dynamically>
EtherType:        88:F7
```

Sync total wire length:    14 + 44 = **58 bytes**  
FollowUp total wire length: 14 + 76 = **90 bytes**

---

## 3. Required Changes to Bridge Project

### 3.1 New File: `firmware/src/ptp_gm_task.h`

Header with all GM-specific constants and the public API:

```c
#ifndef PTP_GM_TASK_H
#define PTP_GM_TASK_H

#include <stdint.h>
#include <stdbool.h>

/* ---- TX Match registers (GM only) ---- */
#define GM_TXMCTL       (0x00040040u)
#define GM_TXMPATH      (0x00040041u)
#define GM_TXMPATL      (0x00040042u)
#define GM_TXMMSKH      (0x00040043u)
#define GM_TXMMSKL      (0x00040044u)
#define GM_TXMLOC       (0x00040045u)
#define GM_OA_CONFIG0   (0x00000004u)
#define GM_OA_STATUS0   (0x00000008u)

/* ---- TX Timestamp Capture registers ---- */
#define GM_OA_TTSCAH    (0x00000010u)
#define GM_OA_TTSCAL    (0x00000011u)
#define GM_OA_TTSCBH    (0x00000012u)
#define GM_OA_TTSCBL    (0x00000013u)
#define GM_OA_TTSCCH    (0x00000014u)
#define GM_OA_TTSCCL    (0x00000015u)

/* ---- Status / control bit masks ---- */
#define GM_TXMCTL_TXPMDET   (0x0080u)
#define GM_STS0_TTSCAA      (0x0100u)
#define GM_STS0_TTSCAB      (0x0200u)
#define GM_STS0_TTSCAC      (0x0400u)

/* ---- Timing ---- */
#define PTP_GM_SYNC_PERIOD_MS   125u
#define PTP_GM_STATIC_OFFSET    7650u
#define PTP_GM_MAX_TN_VAL       0x3B9ACA00u
#define PTP_GM_MAX_RETRIES      5u

/* ---- Public API ---- */
void PTP_GM_Init(void);
void PTP_GM_Service(void);       /* call every 1 ms */
void PTP_GM_GetStatus(uint32_t *pSyncCount, uint32_t *pState);

#endif /* PTP_GM_TASK_H */
```

### 3.2 New File: `firmware/src/ptp_gm_task.c`

Grandmaster send state machine, ported from `main.c` of the reference to the
Harmony non-blocking pattern.

**Skeleton outline:**

```c
#include "ptp_gm_task.h"
#include "PTP_FOL_task.h"
#include "config/default/driver/lan865x/drv_lan865x.h"
#include "config/default/system/console/sys_console.h"
#include "config/default/library/tcpip/tcpip.h"

typedef enum {
    GM_STATE_IDLE = 0,
    GM_STATE_WAIT_PERIOD,
    GM_STATE_SEND_SYNC,
    GM_STATE_WAIT_TXMCTL,
    GM_STATE_CHECK_TXMCTL,
    GM_STATE_WAIT_STATUS0,
    GM_STATE_CHECK_STATUS0,
    GM_STATE_WAIT_TTSCAH,
    GM_STATE_WAIT_TTSCAL,
    GM_STATE_CLEAR_STATUS0,
    GM_STATE_SEND_FOLLOWUP
} gmState_t;

static gmState_t   gm_state     = GM_STATE_IDLE;
static volatile bool  gm_rd_done = false;
static volatile uint32_t gm_rd_val = 0;
static uint32_t    gm_status0   = 0;
static uint32_t    gm_ts_sec    = 0;
static uint32_t    gm_ts_nsec   = 0;
static uint32_t    gm_tick_ms   = 0;   /* incremented by 1 ms service calls */
static uint32_t    gm_last_sync = 0;
static uint16_t    gm_seq_id    = 0;
static uint32_t    gm_sync_cnt  = 0;

static void gm_read_cb(void *r1, bool ok, uint32_t addr,
                        uint32_t value, void *tag, void *r2) {
    gm_rd_val  = value;
    gm_rd_done = true;
}

/* non-blocking GM register write — fire and forget */
static void gm_write(uint32_t addr, uint32_t value) {
    DRV_LAN865X_WriteRegister(0u, addr, value, true, NULL, NULL);
}

void PTP_GM_Init(void) {
    /* Port of TC6_ptp_master_init() from tc6-noip.c */
    gm_write(GM_TXMLOC,    30u);
    gm_write(GM_TXMPATH,   0x88u);
    gm_write(GM_TXMPATL,   0xF710u);
    gm_write(GM_TXMMSKH,   0x00u);
    gm_write(GM_TXMMSKL,   0x00u);
    gm_write(GM_TXMCTL,    0x02u);
    gm_write(MAC_TI,       40u);          /* MAC_TI already defined in PTP_FOL_task.h */
    /* RMW: OA_CONFIG0 |= 0xC0 */
    DRV_LAN865X_ReadModifyWriteRegister(0u, GM_OA_CONFIG0, 0xC0u, 0xC0u, true, NULL, NULL);
    /* RMW: PADCTRL = (PADCTRL & ~0x300) | 0x100 */
    DRV_LAN865X_ReadModifyWriteRegister(0u, PADCTRL, 0x100u, 0x300u, true, NULL, NULL);
    gm_write(PPSCTL, 0x0000007Du);
    gm_state  = GM_STATE_WAIT_PERIOD;
    gm_seq_id = 0;
    gm_sync_cnt = 0;
    SYS_CONSOLE_PRINT("[PTP-GM] Init complete\r\n");
}

void PTP_GM_Service(void) {
    /* Called every 1 ms from timer callback */
    gm_tick_ms++;
    /* ... full state machine implementation ... */
}
```

> The complete `PTP_GM_Service()` implementation follows the 7-state flow described in
> section 2.2.  Each state checks `gm_rd_done` (set by `gm_read_cb`) before advancing,
> mirroring the pattern already used in `PTP_FOL_task.c` for the Follower servo.

### 3.3 Key Challenge: TSC=1 in the Harmony TX Path  ✅ Solved

`DRV_LAN865X_PacketTx()` does **not** expose the `txCaptureTimeStampA` flag in the TC6
Data Header.  Without TSC=1, `OA_TTSCAH`/`OA_TTSCAL` will not be populated.

**Implemented solution — Generic `DRV_LAN865X_SendRawEthFrame()` with `tsc` param**

Added to `firmware/src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c`:

```c
/* Callback typedef (void* for pInst avoids tc6.h in public header) */
typedef void (*DRV_LAN865X_RawTxCallback_t)(void *pInst, const uint8_t *pTx,
                                             uint16_t len, void *pTag,
                                             void *pGlobalTag);

/* Generic raw frame send — tsc=0x01 for Sync, tsc=0x00 for FollowUp */
bool DRV_LAN865X_SendRawEthFrame(uint8_t idx, const uint8_t *pBuf, uint16_t len,
                                  uint8_t tsc,
                                  DRV_LAN865X_RawTxCallback_t cb, void *pTag)
{
    bool result = false;
    if (idx < DRV_LAN865X_INSTANCES_NUMBER) {
        DRV_LAN865X_DriverInfo *pDrv = &drvLAN865XDrvInst[idx];
        if (SYS_STATUS_READY == pDrv->state) {
            result = TC6_SendRawEthernetPacket(pDrv->drvTc6, pBuf, len,
                                               tsc,
                                               (TC6_RawTxCallback_t)(void *)cb,
                                               pTag);
        }
    }
    return result;
}
```

Usage in `ptp_gm_task.c`:
```c
DRV_LAN865X_SendRawEthFrame(0, gm_sync_buf,    58, 0x01u, gm_tx_cb, NULL); /* Sync     */
DRV_LAN865X_SendRawEthFrame(0, gm_followup_buf, 90, 0x00u, gm_tx_cb, NULL); /* FollowUp */
```

### 3.4 Patch: `firmware/src/PTP_FOL_task.c` / `.h`

Add two accessors so `app.c` and `ptp_gm_task.c` can read/write the mode variable
without exposing the static:

```c
/* PTP_FOL_task.h — add */
ptpMode_t PTP_FOL_GetMode(void);
void      PTP_FOL_SetMode(ptpMode_t mode);

/* PTP_FOL_task.c — add */
ptpMode_t PTP_FOL_GetMode(void) { return ptpMode; }
void      PTP_FOL_SetMode(ptpMode_t mode) {
    ptpMode = mode;
    if (mode == PTP_SLAVE) {
        /* reset follower state */
        resetSlaveNode();
    }
}
```

`PTP_FOL_OnFrame()` already works correctly when `ptpMode != PTP_SLAVE` (it just
processes every frame type unconditionally).  After adding the getter/setter, gate
Sync/FollowUp processing on `ptpMode == PTP_SLAVE`.

### 3.5 Patch: `firmware/src/app.c`

#### 3.5.1 New CLI Commands

Add to `msd_cmd_tbl[]`:

```c
{"ptp_mode",   (SYS_CMD_FNC) cmd_ptp_mode,   ": set PTP mode (off|follower|master)"},
{"ptp_status", (SYS_CMD_FNC) cmd_ptp_status, ": show PTP mode, sync count, offset"},
{"ptp_interval",(SYS_CMD_FNC)cmd_ptp_interval,": set GM Sync interval in ms (default 125)"},
```

Implementation sketch:

```c
static void cmd_ptp_mode(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    if (argc != 2) {
        SYS_CONSOLE_PRINT("Usage: ptp_mode [off|follower|master]\r\n");
        return;
    }
    if (strcmp(argv[1], "off") == 0) {
        PTP_FOL_SetMode(PTP_DISABLED);
        SYS_CONSOLE_PRINT("[PTP] disabled\r\n");
    } else if (strcmp(argv[1], "follower") == 0) {
        PTP_FOL_SetMode(PTP_SLAVE);
        SYS_CONSOLE_PRINT("[PTP] follower mode\r\n");
    } else if (strcmp(argv[1], "master") == 0) {
        PTP_GM_Init();
        PTP_FOL_SetMode(PTP_MASTER);
        SYS_CONSOLE_PRINT("[PTP] grandmaster mode\r\n");
    } else {
        SYS_CONSOLE_PRINT("Unknown mode: %s\r\n", argv[1]);
    }
}

static void cmd_ptp_status(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv) {
    const char *modeStr[] = {"disabled", "master", "slave"};
    uint32_t cnt, state;
    PTP_GM_GetStatus(&cnt, &state);
    SYS_CONSOLE_PRINT("[PTP] mode=%s gmSyncs=%u gmState=%u\r\n",
                       modeStr[PTP_FOL_GetMode()], (unsigned)cnt, (unsigned)state);
}
```

#### 3.5.2 Periodic GM Service

Add a 1 ms periodic timer for the GM state machine (existing timer is 1000 ms — too coarse):

```c
static SYS_TIME_HANDLE gmTimerHandle = SYS_TIME_HANDLE_INVALID;

static void GM_TimerCallback(uintptr_t context) {
    if (PTP_FOL_GetMode() == PTP_MASTER) {
        PTP_GM_Service();
    }
}

/* In APP_Initialize(), after existing timerHandle setup: */
gmTimerHandle = SYS_TIME_TimerCreate(0, SYS_TIME_MSToCount(1),
                                      &GM_TimerCallback, 0, SYS_TIME_PERIODIC);
SYS_TIME_TimerStart(gmTimerHandle);
```

---

## 4. Adaptation: Harmony non-blocking vs. noIP blocking

The reference state machine uses synchronous register access:

```c
/* Reference (noIP) — blocking */
TC6_read_finished = false;
TC6_ReadRegister(tc6, OA_STATUS0, true, _ReadComplete, NULL);
TC6_Service(tc6, true);            // blocks until SPI done
uint32_t val = TC6_read_value;     // result immediately available
```

In Harmony, every register access is asynchronous with a callback.  The same state
machine step is split across two states:

```c
/* Harmony — non-blocking pattern (same as PTP_FOL_task.c) */
case GM_STATE_READ_STATUS0:
    gm_rd_done = false;
    DRV_LAN865X_ReadRegister(0u, GM_OA_STATUS0, false, gm_read_cb, NULL);
    gm_state = GM_STATE_WAIT_STATUS0;
    break;

case GM_STATE_WAIT_STATUS0:
    if (gm_rd_done) {
        gm_rd_done = false;
        gm_status0 = gm_rd_val;
        if (gm_status0 & GM_STS0_TTSCAA) {
            gm_state = GM_STATE_READ_TTSCAH;
        } else {
            gm_state = GM_STATE_SEND_SYNC;   /* retry */
        }
    }
    break;
```

This is identical in structure to how `PTP_FOL_OnFrame()` feeds register writes
asynchronously for the clock servo.

---

## 5. File Change Summary

| File | Change Type | Status | Details |
|------|------------|--------|---------|
| `firmware/src/ptp_gm_task.h` | **NEW** | ✅ Done | GM register defines, 14-state enum, public API |
| `firmware/src/ptp_gm_task.c` | **NEW** | ✅ Done | Full 14-state non-blocking GM state machine |
| `firmware/src/PTP_FOL_task.h` | **PATCH** | ✅ Done | Added `PTP_FOL_GetMode()` / `PTP_FOL_SetMode()` declarations |
| `firmware/src/PTP_FOL_task.c` | **PATCH** | ✅ Done | Implemented `GetMode`/`SetMode`; `OnFrame` gated on `ptpMode == PTP_SLAVE` |
| `firmware/src/app.c` | **PATCH** | ✅ Done | Added `cmd_ptp_mode`, `cmd_ptp_status`, `cmd_ptp_interval`; 1 ms GM timer |
| `firmware/src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c` | **PATCH** | ✅ Done | Added `DRV_LAN865X_SendRawEthFrame()` (generic TSC param) |
| `firmware/src/config/default/driver/lan865x/drv_lan865x.h` | **PATCH** | ✅ Done | Added `DRV_LAN865X_RawTxCallback_t` typedef + `SendRawEthFrame` declaration |
| `nbproject/Makefile-default.mk` | **UPDATE** | ✅ Done | Variable lists + DEBUG/release compile rules for `ptp_gm_task.c` |
| `nbproject/configurations.xml` | **UPDATE** | ✅ Done | Added `ptp_gm_task.c` to project source tree |
| `cmake/.generated/file.cmake` | **UPDATE** | ✅ Done | Added `ptp_gm_task.c` to CMake source list |

---

## 6. Implementation Order

1. **Define registers & API** → Created `ptp_gm_task.h` ✅
2. **Driver patch** → Added `DRV_LAN865X_SendRawEthFrame()` to `drv_lan865x_api.c` / `.h` ✅
3. **GM init** → Implemented `PTP_GM_Init()` (port of `TC6_ptp_master_init` from `tc6-noip.c`) ✅
4. **Read accessors** → Added `PTP_FOL_GetMode()` / `PTP_FOL_SetMode()` to `PTP_FOL_task.c` ✅
5. **GM state machine** → Implemented `PTP_GM_Service()` as 14-state non-blocking machine ✅
6. **Timer** → Added 1 ms periodic timer for `PTP_GM_Service()` in `APP_Initialize()` ✅
7. **CLI** → Added `ptp_mode`, `ptp_status`, `ptp_interval` commands to `app.c` ✅
8. **Build plumbing** → Updated `Makefile-default.mk`, `configurations.xml`, `file.cmake` ✅
9. **Test** → Verify FollowUp timestamp visible on Follower within ≈ 1–2 µs offset  
   (compare with Follower `offset` variable via `ptp_status` CLI)

---

## 7. Reference Comparison

| Aspect | noIP GM (reference) | Bridge Harmony (implemented) |
|--------|---------------------|------------------------------|
| TX with TSC=1 | `TC6_SendRawEthernetPacket(..., 0x01)` | `DRV_LAN865X_SendRawEthFrame(..., tsc=0x01, ...)` — generic tsc param |
| TX without TSC | same, tsc=0 | `DRV_LAN865X_SendRawEthFrame(..., tsc=0x00, ...)` for FollowUp |
| Register write | `TC6_WriteRegister()` + `TC6_Service()` (blocking) | `DRV_LAN865X_WriteRegister()` (async) |
| Register read | `TC6_ReadRegister()` + `TC6_Service()` → value ready | `DRV_LAN865X_ReadRegister()` + callback flag |
| Periodic timer | `systick.tickCounter` in `while(true)` | `SYS_TIME_TimerCreate()` 1 ms callback |
| State machine | 7 states, synchronous | 14 states (READ + WAIT pairs), non-blocking |
| Source MAC | Hardcoded (`40:84:32:7D:07:FA`) | Read via `TCPIP_STACK_NetAddressMac(netH)` → `const uint8_t*` |
| Init | `TC6_ptp_master_init()` in `tc6-noip.c` | `PTP_GM_Init()` using Harmony register API |
| Retry on error | `MAX_NUM_REG_RETRIES = 5` counter | `PTP_GM_MAX_RETRIES = 5` counter in `PTP_GM_Service()` |

---

## 8. Implementation Notes (Deviations from Plan)

### 8.1 `DRV_LAN865X_SendRawEthFrame` — Generic TSC Parameter

The plan proposed a dedicated `DRV_LAN865X_SendRawWithTscA()`.  The implementation uses
a single generic function with an explicit `tsc` argument:

```c
/* drv_lan865x.h */
typedef void (*DRV_LAN865X_RawTxCallback_t)(void *pInst, const uint8_t *pTx,
                                             uint16_t len, void *pTag,
                                             void *pGlobalTag);

bool DRV_LAN865X_SendRawEthFrame(uint8_t idx, const uint8_t *pBuf, uint16_t len,
                                  uint8_t tsc,
                                  DRV_LAN865X_RawTxCallback_t cb, void *pTag);
```

`tsc=0x01` for Sync (capture TSC-A), `tsc=0x00` for FollowUp (no capture).

The callback typedef uses `void*` for the TC6 instance pointer to avoid pulling
`tc6.h` into the public driver header.

### 8.2 `TCPIP_STACK_NetAddressMac` Signature

The Harmony TCP/IP library declares this function as returning `const uint8_t*`,
**not** taking an output pointer:

```c
/* Actual Harmony declaration (tcpip_manager.h) */
const uint8_t* TCPIP_STACK_NetAddressMac(TCPIP_NET_HANDLE netH);

/* ptp_gm_task.c — correct usage */
const uint8_t *pMac = TCPIP_STACK_NetAddressMac(netH);
if (pMac != NULL) {
    memcpy(gm_src_mac, pMac, 6);
}
```

The README plan showed a two-argument form; this was corrected during the build.

### 8.3 14-State Machine Instead of 7

Because every register read is asynchronous, each logical state from the reference is
split into a **READ** state (kick off the async request) and a **WAIT** state (poll the
callback flag).  Actual states:

```
IDLE → WAIT_PERIOD → SEND_SYNC
     → READ_TXMCTL → WAIT_TXMCTL
     → READ_STATUS0 → WAIT_STATUS0
     → READ_TTSCA_H → WAIT_TTSCA_H
     → READ_TTSCA_L → WAIT_TTSCA_L
     → WRITE_CLEAR → WAIT_CLEAR
     → SEND_FOLLOWUP → (back to WAIT_PERIOD)
```

### 8.4 `PTP_GM_SetSyncInterval` Added to Public API

Beyond `Init / Service / GetStatus`, a fourth function was added to support the
`ptp_interval` CLI command (runtime-adjustable Sync period):

```c
void PTP_GM_SetSyncInterval(uint32_t intervalMs);
```

Default: `PTP_GM_SYNC_PERIOD_MS = 125`.

### 8.5 CMake `file.cmake`

The `cmake/T1S_100BaseT_Bridge/default/.generated/file.cmake` file is auto-generated
by MPLAB X but can be patched because it is tracked in the project.  `ptp_gm_task.c`
must be added there in addition to `Makefile-default.mk` so that both the traditional
MPLAB make build and the VS Code CMake build work.

---

## 9. Risks and Open Questions

| Item | Risk | Status |
|------|------|--------|
| **TSC=1 TX path** | `DRV_LAN865X_PacketTx` doesn't expose TSC flag | ✅ Solved: `DRV_LAN865X_SendRawEthFrame(tsc=0x01)` |
| **RMW register API** | `DRV_LAN865X_ReadModifyWriteRegister` may not exist | ✅ Exists in `drv_lan865x_api.c` |
| **OA_TTSCAH value** | Notes had wrong value 0x11 | ✅ Confirmed: `OA_TTSCAH=0x10`, `OA_TTSCAL=0x11` |
| **Harmony read callback latency** | Async reads span multiple SPI cycles | ✅ Handled: READ/WAIT state pairs |
| **Source MAC** | Hardcoded in reference | ✅ Read via `TCPIP_STACK_NetAddressMac()` |
| **GM + Follower simultaneously** | TX–RX race on EtherType 0x88F7 | ✅ Gated: `PTP_FOL_OnFrame()` skips when `ptpMode == PTP_MASTER` |
| **PPSCTL side-effects** | Writes `0x7D`, enables PPS output pin | ⚠️ Open: verify PPS pin is free on board |
| **Sequence ID on reinit** | After `ptp_mode off` → `master`, seq_id resets | ✅ Intended; Follower re-syncs |
| **`TCPIP_STACK_NetAddressMac` signature** | Two-arg form assumed in plan | ✅ Fixed: returns `const uint8_t*` |
| **Hardware timestamp accuracy** | ±1 µs expected | ⚠️ Open: verify on hardware vs. Follower offset |

---

## 10. Estimating Implementation Effort (Actual)

| Task | Estimated | Actual |
|------|-----------|--------|
| `ptp_gm_task.h` — constants and declarations | ~30 min | ✅ Done |
| `DRV_LAN865X_SendRawEthFrame()` driver patch | ~45 min | ✅ Done |
| `PTP_GM_Init()` — register writes | ~30 min | ✅ Done |
| `PTP_GM_Service()` — full 14-state machine | ~3–4 h | ✅ Done |
| `PTP_FOL_GetMode/SetMode` + `OnFrame` gate | ~30 min | ✅ Done |
| CLI commands in `app.c` | ~45 min | ✅ Done |
| Build plumbing (Makefile, configs.xml, cmake) | ~20 min | ✅ Done (cmake fix needed) |
| Build error fix (`NetAddressMac` signature) | — | ✅ 5 min |
| Integration test on hardware | ~2–4 h | ⏳ Pending |

---

## 11. Related Files

| File | Description |
|------|-------------|
| [README_PTP_TCP.md](README_PTP_TCP.md) | PTP Follower implementation — already done |
| `firmware/src/ptp_gm_task.h` | GM register defines, public API |
| `firmware/src/ptp_gm_task.c` | GM 14-state non-blocking state machine |
| `firmware/src/PTP_FOL_task.c` | Follower clock servo + `ptpMode` variable |
| `firmware/src/PTP_FOL_task.h` | Follower types, addresses, mode API |
| `firmware/src/app.c` | CLI commands + GM 1 ms timer |
| `firmware/src/config/default/driver/lan865x/drv_lan865x.h` | `DRV_LAN865X_SendRawEthFrame` declaration |
| `firmware/src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c` | `DRV_LAN865X_SendRawEthFrame` implementation |
| `firmware/src/filters.c` / `.h` | FIR/IIR filter helpers used by the Follower servo |
| `LAN865x-TimeSync/noIP-SAM-E54-Curiosity-PTP-Grandmaster/firmware/src/main.c` | GM state machine reference (830 lines) |
| `LAN865x-TimeSync/noIP-SAM-E54-Curiosity-PTP-Grandmaster/libtc6.X/inc/tc6-regs.h` | Authoritative register address source |
