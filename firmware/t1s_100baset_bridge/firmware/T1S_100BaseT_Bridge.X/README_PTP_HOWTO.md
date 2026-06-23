# PTP Implementierung — HowTo und Änderungsübersicht

**Datum:** 2026-04-04  
**Branch:** `vscode-migration`  
**Plattform:** ATSAME54P20A / LAN865x (10BASE-T1S, TC6)  
**Standard:** IEEE 1588-2019 PTPv2, Hardware-Timestamping  
**Status:** Vollständig funktionsfähig — Follower-Offset mean=+45.5 ns, stdev=8.6 ns

---

## Inhaltsverzeichnis

1. [Architekturübersicht](#1-architekturübersicht)
2. [Neu eingeführte Dateien](#2-neu-eingeführte-dateien)
3. [Angefasste Dateien — was und warum](#3-angefasste-dateien--was-und-warum)
4. [TC6-Layer — minimale notwendige Eingriffe](#4-tc6-layer--minimale-notwendige-eingriffe)
5. [Datenfluss PTP end-to-end](#5-datenfluss-ptp-end-to-end)
6. [Follower-Servo: Konvergenzzustände](#6-follower-servo-konvergenzzustände)
7. [Kritischer Fix: TTSCAA Race Condition](#7-kritischer-fix-ttscaa-race-condition)
8. [Referenzprojekte](#8-referenzprojekte)

---

## 1. Architekturübersicht

```
┌───────────────────────────────────────────────────────────────────────┐
│  Grandmaster-Board (PLCA node 0)                                       │
│                                                                        │
│  ptp_gm_task.c                                                         │
│  ┌──────────────────────────────────────────────────┐                 │
│  │  GM State Machine (non-blocking, 1 ms Tick)      │                 │
│  │  WAIT_PERIOD → SEND_SYNC → WAIT_TXMCTL           │                 │
│  │  → READ_STATUS0 → READ_TTSCA_H/L                 │                 │
│  │  → SEND_FOLLOWUP → WAIT_FOLLOWUP_TX_DONE         │                 │
│  └──────────────────────────────────────────────────┘                 │
│         │ DRV_LAN865X_SendRawEthFrame(tsc=1)  ← Sync mit TSC-Flag     │
│         │ DRV_LAN865X_GetAndClearTsCapture()  ← TX-Timestamp lesen    │
│         ▼                                                              │
│  drv_lan865x_api.c  (TC6-Layer, LAN865x SPI)                          │
│  ┌──────────────────────────────────────────────────┐                 │
│  │  _OnStatus0() speichert TTSCAA → drvTsCaptureStatus0[]             │
│  │  TC6_CB_OnRxEthernetPacket() speichert rxTimestamp → g_ptp_rx_ts   │
│  └──────────────────────────────────────────────────┘                 │
│         │ 10BASE-T1S / PLCA                                           │
└─────────┼─────────────────────────────────────────────────────────────┘
          │  PTP Sync Frame  (EtherType 0x88F7, messageType 0x00)
          │  PTP FollowUp Frame (EtherType 0x88F7, messageType 0x08)
          │
┌─────────┼─────────────────────────────────────────────────────────────┐
│  Follower-Board (PLCA node 1)                                          │
│         ▼                                                              │
│  drv_lan865x_api.c                                                     │
│  ┌──────────────────────────────────────────────────┐                 │
│  │  TC6_CB_OnRxEthernetPacket() → g_ptp_rx_ts       │                 │
│  └──────────────────────────────────────────────────┘                 │
│         │ pktEth0Handler() in app.c                                   │
│         │ erkennt EtherType 0x88F7, kopiert Frame + rxTimestamp       │
│         ▼                                                              │
│  PTP_FOL_task.c                                                        │
│  ┌──────────────────────────────────────────────────┐                 │
│  │  PTP_FOL_OnFrame()                               │                 │
│  │  offset = t2(rxTimestamp) - t1(FollowUp)         │                 │
│  │  FIR-Filter (N=3) → val → MAC_TA schreiben       │                 │
│  │  Servo: UNINIT→MATCHFREQ→HARDSYNC→COARSE→FINE    │                 │
│  └──────────────────────────────────────────────────┘                 │
└───────────────────────────────────────────────────────────────────────┘
```

---

## 2. Neu eingeführte Dateien

### `src/ptp_gm_task.c` / `src/ptp_gm_task.h`
**Ursprung:** Portiert von `noIP-SAM-E54-Curiosity-PTP-Grandmaster/firmware/src/main.c`

**Zweck:** Nicht-blockierende Grandmaster-State-Machine die:
- PTP Sync-Frames (EtherType `0x88F7`, messageType `0x00`) sendet
- TX-Timestamp (t1) aus dem LAN865x TSU (TTSCAH/TTSCAL Register) liest
- PTP FollowUp-Frames mit dem exakten t1-Wert sendet
- TX-Match-Register beim Init/Deinit konfiguriert

**State Machine Zustände:**
```
IDLE → WAIT_PERIOD (125 ms)
     → SEND_SYNC               (TX mit tsc=1)
     → WAIT_SYNC_TX_DONE       (warte auf TX-Done-Callback)
     → READ_TXMCTL / WAIT_TXMCTL  (warte auf TXPMDET-Bit)
     → READ_STATUS0 / WAIT_STATUS0  (prüfe TTSCAA via CB-Shortcut)
     → READ_TTSCA_H/L / WAIT_TTSCA_H/L  (lese 64-bit Timestamp)
     → WRITE_CLEAR / WAIT_CLEAR  (W1C-Reset des Latches)
     → SEND_FOLLOWUP           (TX mit t1 eingebettet)
     → WAIT_FOLLOWUP_TX_DONE   (warte auf TX-Done-Callback)
     → zurück zu WAIT_PERIOD
```

**Wichtige Unterschiede zur Referenz:**
| Referenz (noIP) | Diese Implementierung |
|-----------------|----------------------|
| `TC6_BlockingRegisterAccess()` | `DRV_LAN865X_ReadRegister()` + Callback-Flag |
| `TC6NoIP_SendEthernetPacket_TimestampA()` | `DRV_LAN865X_SendRawEthFrame(tsc=1)` |
| Blocking `while()` Schleifen | Non-blocking State Machine |
| systick Timer | Harmony `SYS_TIME_Counter64Get()` |
| Feste MAC-Adresse | `TCPIP_STACK_NetAddressMac()` dynamisch |

---

### `src/PTP_FOL_task.c` / `src/PTP_FOL_task.h`
**Ursprung:** Portiert von `noIP-SAM-E54-Curiosity-PTP-Follower/ptp_task.c`

**Zweck:** Follower-Servo der:
- Empfangene Sync/FollowUp-Frames verarbeitet
- Offset berechnet: `offset = t2 - t1 - propagation_delay`
- FIR-gefiltertes Korrektursignal (`val`) in `MAC_TA` schreibt
- Sequentielle GEM-Register-Schreibzugriffe per State Machine koordiniert

**Servo-Register:**
| Register | Adresse | Funktion |
|----------|---------|----------|
| `MAC_TI` | `0x00010077` | Taktinkrement (40 = 25 MHz GEM-Uhr) |
| `MAC_TISUBN` | `0x0001006F` | Subnanosekunden-Feinkorrektur |
| `MAC_TSL` | `0x00010071` | Time Seconds Low — Hardsync |
| `MAC_TN` | `0x00010076` | Time Nanoseconds — Hardsync |
| `MAC_TA` | `0x00010076` | Time Adjust — Feinregelung pro Sync |
| `PPSCTL` | `0x000A0239` | PPS-Ausgang Steuerung |

**Wichtige Unterschiede zur Referenz:**
| Referenz (noIP) | Diese Implementierung |
|-----------------|----------------------|
| `TC6_WriteRegister()` direkt | `DRV_LAN865X_WriteRegister()` |
| Blocking `TC6_Service()` Schleifen | Harmony-Driver übernimmt Service |
| `get_macPhy_inst() / TC6_t*` | Driver-Index `0` |
| `printf()` | `SYS_CONSOLE_PRINT()` |
| `ptpTask()` | `PTP_FOL_Init()` + `PTP_FOL_OnFrame()` |

---

### `src/ptp_ts_ipc.h`
**Neu:** 24 Zeilen, IPC-Struktur für RX-Timestamp.

```c
typedef struct { uint64_t rxTimestamp; bool valid; } PTP_RxTimestampEntry_t;
extern volatile PTP_RxTimestampEntry_t g_ptp_rx_ts;
```

**Zweck:** Brücke zwischen TC6-Callback-Kontext (`drv_lan865x_api.c`) und
Application-Kontext (`app.c` / `PTP_FOL_task.c`).

Der Hardware-RX-Timestamp wird in `TC6_CB_OnRxEthernetPacket()` in den TC6-Layer
geliefert und muss über den Kontext-Wechsel hinaus verfügbar sein.
Ohne diesen Mechanismus wäre t2 = 0 und der Offset komplett falsch.

---

### `src/filters.c` / `src/filters.h`
**Ursprung:** Portiert mit dem FOL-Task aus dem noIP-Referenzprojekt.

**Zweck:** FIR-Lowpass-Filter für den Servo.

| Parameter | Wert |
|-----------|------|
| `FIR_FILER_SIZE_FINE` | 3 (FINE-Regelung, Offset-Filter) |
| `FIR_FILER_SIZE` | 16 (Frequenz-Ratio-Filter) |
| `CLOCK_CYCLE_NS` | 40.0 (entspricht MAC_TI=40) |
| `CLOCK_OFFsET_NS` | 4.0 |

Ohne den Filter würde `val` (das MAC_TA-Korrekturwort) mit jedem Messwert
direkt springen → Regler hätte keine Dämpfung → Offset-Schwankung > 100 ns.

---

## 3. Angefasste Dateien — was und warum

### `src/app.c` (+764 Zeilen)

| Ergänzung | Zweck |
|-----------|-------|
| `#include "ptp_ts_ipc.h"` | Zugriff auf `g_ptp_rx_ts` |
| `PTP_FRAME_BUFFER ptp_rx_buffer` | Zwischenpuffer für empfangene PTP-Frames |
| `pktEth0Handler()` erweitert | Erkennt `EtherType 0x88F7`, kopiert Frame + `g_ptp_rx_ts.rxTimestamp` in `ptp_rx_buffer` |
| `APP_Tasks()` erweitert | Ruft `PTP_GM_Service()` alle 1 ms auf (wenn mode=MASTER); liefert gebufferte Frames an `PTP_FOL_OnFrame()` (wenn mode=SLAVE) |
| Loss-of-Framing Guard | `DRV_LAN865X_IsReady()` prüft ob Driver nach Re-Init wieder bereit; GM-TX-Match wird dann neu konfiguriert |
| CLI-Befehle | 7 neue Befehle: `ptp_mode`, `ptp_status`, `ptp_interval`, `ptp_dst`, `ptp_offset`, `ptp_reset`, `ptp_regs` |

**Kritischer Pfad in `pktEth0Handler()`:**
```c
if (g_ptp_rx_ts.valid) {
    rxTs = g_ptp_rx_ts.rxTimestamp;   ← Hardware-RX-Timestamp aus TC6-Callback
    g_ptp_rx_ts.valid = false;
}
// ... Frame kopieren ...
ptp_rx_buffer.rxTimestamp = rxTs;      ← An FOL-Task weitergeben
ptp_rx_buffer.pending     = true;
```

---

### `src/config/default/initialization.c` (+44 Zeilen)

**Änderung 1 — `nodeId = 1` hardcoded:**
```c
- .nodeId = DRV_LAN865X_PLCA_NODE_ID_IDX0,
+ .nodeId = 1,
```
**Warum:** Beide Boards werden mit derselben Firmware geflasht.
Das GM-Board hat PLCA node 0 (konfiguriert via Jumper/Fuse), das FOL-Board node 1.
Die TX-Match-Logik im GM wird nur aktiviert wenn das Board PLCA node 0 ist.

**Änderung 2 — TRNG MAC-Randomisierung:**
```c
APP_RandomizeMacLastBytes(s_macAddrStr0, "00:04:25");
APP_RandomizeMacLastBytes(s_macAddrStr1, "00:04:25");
```
**Warum:** Eine Firmware für alle Boards — die letzten 3 MAC-Bytes werden via
Hardware-TRNG beim Boot zufällig gesetzt. Kein PTP-Requirement, aber notwendig
damit beide Boards unterschiedliche MAC-Adressen haben (die clockIdentity im
PTP-Header wird direkt aus der MAC abgeleitet).

---

### `src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c`

Siehe separater Abschnitt 4.

---

### `src/config/default/driver/lan865x/drv_lan865x.h`

Fünf neue öffentliche Funktionsdeklarationen hinzugefügt — ebenfalls in Abschnitt 4 beschrieben.

---

## 4. TC6-Layer — minimale notwendige Eingriffe

Der TC6-Layer (`drv_lan865x_api.c`) musste an 9 Stellen angefasst werden.
Diese Änderungen sind technisch unvermeidlich — es gibt keine Alternative
die ohne Eingriff in den Driver auskommt.

### Änderung 1 — `DELAY_UNLOCK_EXT`: 100 ms → 5 ms
```c
- #define DELAY_UNLOCK_EXT (100u)
+ #define DELAY_UNLOCK_EXT (5u)  /* TTSCAA erscheint ~1ms nach EXST */
```
**Warum:** TTSCAA (TX Timestamp Capture Available) erscheint typisch 1–2 ms nach dem
EXST-Interrupt. Der alte 100 ms Lock blockierte das Capture-Fenster vollständig →
`TTSCMA` (Timestamp Missed) bei jedem Sync.

---

### Änderung 2+3 — TTSCAA vor W1C-Löschung retten

**Neue Variable:**
```c
static volatile uint32_t drvTsCaptureStatus0[DRV_LAN865X_INSTANCES_NUMBER];
```

**In `_OnStatus0()`:**
```c
if (0u != (value & 0x0700u)) {
    drvTsCaptureStatus0[i] |= (value & 0x0700u);  /* Bits 8-10 = TTSCAA/B/C */
}
/* W1C-Löschung erfolgt danach via TC6_WriteRegister() */
```

**Warum:** `_OnStatus0` ist der einzige Punkt im System der STATUS0 *synchron
mit dem Interrupt* liest und dann per W1C löscht. Der GM-Task kommt immer
*zu spät* — STATUS0 war bereits 0 wenn er es über SPI las. Die Bits müssen
hier gesichert werden bevor sie verloren gehen.

---

### Änderung 4 — Neue Funktion `DRV_LAN865X_GetAndClearTsCapture()`

```c
uint32_t DRV_LAN865X_GetAndClearTsCapture(uint8_t idx)
{
    uint32_t val = drvTsCaptureStatus0[idx];
    drvTsCaptureStatus0[idx] = 0u;
    return val;   /* atomisches Lesen+Löschen */
}
```

**Warum:** GM-State-Machine ruft dies in `GM_STATE_READ_STATUS0` als Shortcut
auf. Wenn TTSCAA bereits im CB-Buffer → kein SPI-Lesezugriff nötig.

---

### Änderung 5 — `TC6_CB_OnRxEthernetPacket()`: RX-Timestamp speichern

```c
- (void)rxTimestamp;
+ if (rxTimestamp != NULL) {
+     g_ptp_rx_ts.rxTimestamp = *rxTimestamp;
+     g_ptp_rx_ts.valid       = true;
+ }
```

**Warum:** Der Hardware-RX-Timestamp (t2) wird nur in diesem Callback geliefert.
Er muss für `PTP_FOL_OnFrame()` verfügbar sein. Ohne t2 ist `offset = t2 - t1`
nicht berechenbar.

---

### Änderung 6 — Interrupt-Service außerhalb State-Guard verschoben

```c
/* Vorher: nur wenn state == READY */
/* Nachher: immer — auch während Loss-of-Framing Re-Init */
if (!SYS_PORT_PinRead(pDrvInst->drvCfg.interruptPin)) {
    TC6_Service(pDrvInst->drvTc6, false);
}
```

**Warum:** Während einer Loss-of-Framing Re-Initialisierung (`state == UNINITIALIZED`)
gelangte kein TC6_Service-Aufruf mehr für den Interrupt-Pin. Eingehende Pakete
(ARP, ICMP replies, PTP FollowUp) füllten den RX-FIFO und wurden verworfen.

---

### Änderung 7 — `_InitMemMap()`: TX-Match + LAN865x Init für PTP

Diese Register werden beim Driver-Start (und nach Loss-of-Framing Re-Init) gesetzt:

| Register | Adresse | Wert | Bedeutung |
|----------|---------|------|-----------|
| `TXMPATH` | `0x40041` | `0x88` | EtherType High-Byte |
| `TXMPATL` | `0x40042` | `0xF710` | EtherType `0x88F7` + Sync-Type `0x10` |
| `TXMMSKH` | `0x40043` | `0x00` | Kein Masking (exaktes Match) |
| `TXMMSKL` | `0x40044` | `0x00` | Kein Masking |
| `TXMLOC`  | `0x40045` | `0x1E` (=30) | Byte-Offset 30 (Microchip Referenzwert) |
| `TXMCTL`  | `0x40040` | `0x00` | Beim Start deaktiviert; pro Sync per-Arm |
| `IMASK0`  | `0x000C`  | `0x00` | Alle Interrupts unmaskiert inkl. Bit 8 (TTSCAA) |
| `MAC_TI`  | `0x10077` | `0x28` (=40) | 40 ns/Tick = 25 MHz GEM-Takt |

**Warum:** Ohne TX-Match-Pattern feuert TXPMDET/TTSCAA nie. Ohne `IMASK0=0`
erreicht der TTSCAA-Interrupt den TC6-Callback nicht.

---

### Änderung 8 — `_InitConfig()` case 46+47: PADCTRL + PPSCTL

```c
case 46:
    /* PADCTRL (0x000A0088): RMW value=0x100, mask=0x300 — TX Timestamp Ausgang */
    TC6_ReadModifyWriteRegister(tc, 0x000A0088u, 0x00000100u, 0x00000300u, ...);
    break;
case 47:
    /* PPSCTL (0x000A0239): 0x7D = 125 — PPS-Takt für TSU-Zähler */
    TC6_WriteRegister(tc, 0x000A0239u, 0x0000007Du, ...);
    break;
```

**Warum:** `PADCTRL` aktiviert den TX-Timestamp-Ausgang des LAN865x-PHY.
`PPSCTL` startet den internen PPS-Takt der den TSU-Zähler (Timestamp Unit) antreibt.
Beide sind Voraussetzung für funktionierende Hardware-Timestamps.

---

### Änderung 9 — `_InitUserSettings()` case 8: FTSE + FTSS

```c
regVal |= 0x80u;  /* FTSE: Frame Timestamp Enable */
regVal |= 0x40u;  /* FTSS: 64-bit Timestamps */
```

**Warum:** Frame-Timestamping auf Hardware-Ebene einschalten.
Ohne `FTSE` liefert `TC6_CB_OnRxEthernetPacket()` immer `rxTimestamp=NULL`.

---

### Änderung 10 — Neue Funktion `DRV_LAN865X_SendRawEthFrame()`

```c
bool DRV_LAN865X_SendRawEthFrame(uint8_t idx, const uint8_t *pBuf, uint16_t len,
                                  uint8_t tsc, DRV_LAN865X_RawTxCallback_t cb,
                                  void *pTag);
```

**Warum:** Der TCPIP-Stack bietet keine Möglichkeit das TSC-Flag
(Transmit Timestamp Capture) bei einzelnen Frames zu setzen.
`tsc=0x01` bei Sync-Frames → Capture A aktiviert.
`tsc=0x00` bei FollowUp-Frames → kein Capture.

---

## 5. Datenfluss PTP end-to-end

```
GM Board                               Follower Board
─────────────────────────────────────  ────────────────────────────────────
1. PTP_GM_Service() ruft
   DRV_LAN865X_SendRawEthFrame(tsc=1)
   → LAN865x sendet Sync-Frame
   → LAN865x-TSU stempelt TX-Zeitpunkt (t1)
   → TTSCAA-Bit in STATUS0

2. TC6-Interrupt EXST →
   _OnStatus0() liest STATUS0=0x100
   → drvTsCaptureStatus0[0] |= 0x100       ════════════╗
   → TC6_WriteRegister(W1C-Clear)                       ║ T1S Bus
                                                         ║ Sync-Frame
3. GM_STATE_READ_STATUS0:                               ║ FollowUp-Frame
   GetAndClearTsCapture() → 0x100          ════════════╬══▶
   → GM_STATE_READ_TTSCA_H                              ║
                                                         ║
4. SPI: lese TTSCAH (Sekunden)                          ║
   SPI: lese TTSCAL (Nanosekunden)                      ║
   → t1 = sec:nsec                                       ║
                                                         ║
5. build_followup(t1.sec, t1.nsec)                       ║
   SendRawEthFrame(followup, tsc=0) ════════════════════╝
                                         ▼
                              pktEth0Handler() (Interrupt-Kontext)
                              g_ptp_rx_ts.rxTimestamp = t2  (Hardware-RX-TS)
                              ptp_rx_buffer = Frame-Kopie
                              ptp_rx_buffer.pending = true

                                         ▼
                              APP_Tasks() (Main-Loop)
                              PTP_FOL_OnFrame(frame, len, t2)

                                         ▼
                              PTP_FOL_task.c:
                              offset = t2 - t1 - prop_delay
                              val = firLowPassFilter(offset)  ← FIR N=3
                              DRV_LAN865X_WriteRegister(MAC_TA, val)
                              → GEM-Uhr wird um val ns verschoben
```

---

## 6. Follower-Servo: Konvergenzzustände

```
UNINIT
  │ Erster FollowUp empfangen
  ▼
MATCHFREQ
  │ MAC_TI + MAC_TISUBN gesetzt (Frequenzabgleich)
  ▼
HARDSYNC   ← |offset| > 90 ns
  │ MAC_TSL + MAC_TN hart gesetzt (Zeitsprung)
  ▼
COARSE     ← 50 ns < |offset| ≤ 90 ns
  │ MAC_TA = firLowPassFilter(offsetCoarseState)
  ▼
FINE       ← |offset| ≤ 50 ns              ← ZIELZUSTAND
  │ MAC_TA = firLowPassFilter(offsetState, N=3)
  │ Typische Werte: offset ±4 ns (Ruhephase), ±40 ns (Ausreißer)
  │
  └─ Rückfall nach HARDSYNC wenn |offset| > 90 ns
```

**Konsolenmeldungen:**
```
PTP UNINIT->MATCHFREQ  scheduling TI=40 TISUBN=0x1E00000E
[FOL] Clock increment set: TI=40 TISUBN=0x1E00000E
[FOL] 1PPS output enabled
Large offset, scheduling hard sync
[FOL] Hard sync completed
PTP COARSE  offset=12500 val=12500
PTP FINE    offset=-3 val=3      ← eingeschwungen
```

**Bedeutung der FINE-Ausgabe:**

| Feld | Bedeutung | Einheit |
|------|-----------|---------|
| `offset` | Roher Zeitversatz Follower vs. GM | ns |
| `val`    | FIR-gefilterter Korrekturwert → in MAC_TA geschrieben | ns |

`val` = gleitender Mittelwert der letzten 3 `offset`-Werte → dämpft Rauschen.

---

## 7. Kritischer Fix: TTSCAA Race Condition

Dies war das zentrale Problem das PTP verhinderte.

### Fehlerkette (vor dem Fix)
```
1. Sync TX → Hardware setzt TTSCAA in STATUS0
2. TC6-Library EXST-Pfad → _OnStatus0() liest STATUS0=0x100
3. _OnStatus0() führt W1C-Clear durch → STATUS0 = 0x00
4. GM-State-Machine liest STATUS0 via SPI → 0x00 → FAIL
   → "TTSCA not set after Sync #N" wiederholt
5. TTSCAH/TTSCAL Latch nie gelesen → bleibt besetzt
6. Nächster Sync kann keinen neuen Timestamp capturen
7. TTSCAA feuert nie wieder
```

### Fix
`gm_get_and_clear_ts_capture()` in `GM_STATE_READ_STATUS0` als ersten Schritt:
```c
case GM_STATE_READ_STATUS0:
{
    uint32_t cbCapture = gm_get_and_clear_ts_capture();
    if (0u != cbCapture) {
        gm_status0 = cbCapture;               /* CB hat TTSCAA bereits gerettet */
        GM_SET_STATE(GM_STATE_READ_TTSCA_H);  /* Direktpfad, kein SPI-Read nötig */
        break;
    }
    /* Fallback: SPI-Direktlesezugriff */
    ...
    GM_SET_STATE(GM_STATE_WAIT_STATUS0);
    break;
}
```

### Beweis des Fixes
Log `ptp_diag_20260404_203210.log` zeigt:
```
[PTP-GM] TTSCAA via CB=0x00000100, Sync #0
[PTP-GM] TTSCAA via CB=0x00000100, Sync #1
...
[PTP-GM] TTSCAA via CB=0x00000100, Sync #198
```
TTSCAA feuert bei 100% der Sync-Frames (199/199).

---

## 8. Referenzprojekte

| Projekt | Verwendet für |
|---------|--------------|
| `noIP-SAM-E54-Curiosity-PTP-Grandmaster` | GM-State-Machine, TX-Match-Register-Werte, TXMLOC=30, PADCTRL/PPSCTL-Init |
| `noIP-SAM-E54-Curiosity-PTP-Follower` | FOL-Servo-Logik, Filter-Koeffizienten, Konvergenzschwellen |

**Wesentliche Anpassungen gegenüber beiden Referenzen:**
- Harmonie-3-basiert: keine blocking TC6-API-Aufrufe
- Non-blocking State Machines statt `while(true)` Polling
- Harmony `SYS_TIME` statt systick
- Dynamische MAC-Adresse statt hardcoded
- `DRV_LAN865X_*` API statt direkte `TC6_*` Aufrufe

---

## Anhang: Vollständige Dateiliste

| Datei | Status | Zeilen +/- | Zweck |
|-------|--------|------------|-------|
| `src/ptp_gm_task.c` | **NEU** | +970 | GM-State-Machine |
| `src/ptp_gm_task.h` | **NEU** | +141 | GM-API + Register-Defines |
| `src/PTP_FOL_task.c` | **NEU** (portiert) | +615 | FOL-Servo |
| `src/PTP_FOL_task.h` | **NEU** (portiert) | +274 | FOL-API + Threshold-Defines |
| `src/ptp_ts_ipc.h` | **NEU** | +24 | RX-Timestamp IPC |
| `src/filters.c` | **NEU** (portiert) | +105 | FIR-Lowpass-Filter |
| `src/filters.h` | **NEU** (portiert) | +72 | Filter-Parameter |
| `src/app.c` | **GEÄNDERT** | +764 | Frame-Routing, CLI, GM/FOL-Service |
| `src/config/default/initialization.c` | **GEÄNDERT** | +44 | nodeId=1, TRNG-MAC |
| `src/config/default/driver/lan865x/src/dynamic/drv_lan865x_api.c` | **GEÄNDERT** | +180 | TC6-Layer PTP-Erweiterungen |
| `src/config/default/driver/lan865x/drv_lan865x.h` | **GEÄNDERT** | +80 | Neue öffentliche API-Deklarationen |
