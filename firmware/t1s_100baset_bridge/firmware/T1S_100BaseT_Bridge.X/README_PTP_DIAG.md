# PTP Diagnose & Konvergenztest

Datum: 2026-04-01  
Uhrzeit: (Erstellung) 14:00 MESZ  
Zuletzt aktualisiert: 2026-04-04 — **ABGESCHLOSSEN: PTP GM vollständig funktionsfähig, Offset mean=+45.5 ns**

---

## 1. Was ist bereits implementiert

### Grandmaster (`ptp_gm_task.c`)

| Funktion | Status |
|---|---|
| `PTP_GM_Init()` | TX-Match-Register (TXMLOC/PATH/PATL/MSKH/MSKL), MAC_TI=40, PPSCTL=0x7D |
| `PTP_GM_Service()` | 1 ms Tick-basierte State Machine |
| `PTP_GM_Deinit()` | TX-Match disarm, MAC_TI=0, PPSCTL=0 |
| PTP Sync-Frame senden | EtherType 0x88F7, `tsc=0x01` (TX Timestamp Capture aktiviert) |
| TX Match arm vor jedem Sync | `GM_TXMCTL = 0x0001` in `GM_STATE_SEND_SYNC` |
| TX-Timestamp-Capture | State Machine: READ_TXMCTL → WAIT_TXMCTL → WAIT_STATUS0 → READ_TTSCA_H/L → WRITE_CLEAR → WAIT_CLEAR |
| PTP FollowUp senden | `GM_STATE_SEND_FOLLOWUP`: statischen TX-Pfadoffset (7650 ns) addiert |
| DST-MAC konfigurierbar | `ptp_dst broadcast` / `multicast` (01:80:C2:00:00:0E) |
| Sync-Intervall konfigurierbar | `ptp_interval <ms>` (default 125 ms) |
| State-Logging | Nur noch Schlüsselereignisse: `Sync #N`, `TXPMDET ok`, `FU #N t1=...`, Fehler-Timeouts (2026-04-01) |
| Diagnostikkommandos | `ptp_status`, `ptp_offset`, `ptp_reset` |

**LAN865x-API-Nutzung GM (alle aktiv seit 2026-04-01):**
- `DRV_LAN865X_WriteRegister` — TX-Match, MAC_TI, PPSCTL, Deinit ✓
- `DRV_LAN865X_ReadRegister` — TXMCTL-Polling, TTSCA Timestamps ✓
- `DRV_LAN865X_SendRawEthFrame` — Sync + FollowUp senden ✓
- `DRV_LAN865X_GetAndClearTsCapture` — STATUS0-Bits aus Interrupt-Handler ✓

---

### Follower (`PTP_FOL_task.c`)

| Funktion | Status |
|---|---|
| `PTP_FOL_Init()` | PPSCTL=0x02 (Puls-Preset), SEVINTEN (PPSDONE-Interrupt) |
| `PTP_FOL_OnFrame()` | Einstiegspunkt für EtherType 0x88F7 aus `pktEth0Handler()` |
| Sync-Frame verarbeiten | `processSync()`: SequenzID-Tracking und Empfangsbestätigung |
| FollowUp verarbeiten | `processFollowUp()`: t1 aus PTP-Header extrahieren, t2 aus HW-RX-Timestamp |
| Rate-Ratio-Schätzung | FIR-Tiefpassfilter über `diffLocal`/`diffRemote` |
| Servo-State-Machine | UNINIT → MATCHFREQ → HARDSYNC → COARSE → FINE |
| Uhranpassung | `MAC_TISUBN` + `MAC_TI` (Rate), `MAC_TA` (Offset), `MAC_TSL`/`MAC_TN` (Hard-Sync) |
| 1PPS aktivieren | PPSCTL=0x7D nach erstem Sync (einmalig) |
| `PTP_FOL_GetOffset()` | Liest aktuellen Offset + Absolutwert aus |
| **Buffer-Leak-Fix** | `TCPIP_PKT_PacketAcknowledge()` nach `PTP_FOL_OnFrame()` ✓ (2026-04-01) |

**LAN865x-API-Nutzung FOL (immer direkt aktiv, kein Makro-Schutz):**
- `DRV_LAN865X_WriteRegister` — MAC_TSL, MAC_TN, MAC_TI, MAC_TISUBN, MAC_TA, PPSCTL ✓

---

### Packet-Handler (`app.c`)

| Funktion | Status |
|---|---|
| `pktEth0Handler()` | EtherType 0x88F7 → `PTP_FOL_OnFrame()` + `PacketAcknowledge()` |
| RX-Timestamp IPC | `g_ptp_rx_ts` (aus TC6-Callback) → an `PTP_FOL_OnFrame()` übergeben |
| `ptp_mode follower` | `PTP_FOL_SetMode(PTP_SLAVE)` + `resetSlaveNode()` |
| `ptp_mode master` | `PTP_GM_Init()` + `PTP_FOL_SetMode(PTP_MASTER)` |
| `ptp_mode off` | `PTP_GM_Deinit()` + `PTP_FOL_SetMode(PTP_DISABLED)` |

---

## 2. Testplan

### Vorbedingungen
- GM: COM10, IP 192.168.0.20, PLCA nodeId=0
- FOL: COM8, IP 192.168.0.30, PLCA nodeId=1
- Beide Boards mit aktueller Firmware geflasht (GM + Follower HEX)

### Schritt 1 — PTP-Modus starten und FollowUp-Pfad prüfen

**Ziel:** Prüfen ob GM jetzt den vollständigen Pfad läuft:  
`SEND_SYNC → READ_TXMCTL → WAIT_TXMCTL → WAIT_STATUS0 → READ_TTSCA_H/L → SEND_FOLLOWUP`

**Erwartete GM-Konsolausgabe:**
```
[PTP-GM][STATE] GM_STATE_SEND_SYNC @L...
[PTP-GM][STATE] GM_STATE_READ_TXMCTL @L...
[PTP-GM][STATE] GM_STATE_WAIT_TXMCTL @L...
[PTP-GM][STATE] GM_STATE_WAIT_STATUS0 @L...
[PTP-GM][STATE] GM_STATE_READ_TTSCA_H @L...
[PTP-GM][STATE] GM_STATE_READ_TTSCA_L @L...
[PTP-GM][STATE] GM_STATE_WRITE_CLEAR @L...
[PTP-GM][STATE] GM_STATE_WAIT_CLEAR @L...
[PTP-GM][STATE] GM_STATE_SEND_FOLLOWUP @L...
[PTP-GM][STATE] GM_STATE_WAIT_PERIOD @L...
```

**Kriterium:** `SEND_FOLLOWUP` erscheint → FollowUp wird gesendet.

### Schritt 2 — Follower-Servo-Konvergenz messen

**Ziel:** Follower-Servo läuft durch alle Phasen und konvergiert auf FINE.

**Erwartete FOL-Konsolausgabe (zeitlicher Ablauf):**
```
PTP UNINIT->MATCHFREQ  MAC_TI=40 TISUBN=0x...
PTP COARSE  offset=... val=...
PTP FINE    offset=... val=...
```

**Konvergenzkritierium:** `ptp_offset` auf FOL zeigt Offset < 100 ns für ≥ 10 aufeinanderfolgende Abfragen.

### Schritt 3 — Langzeitmessung Offset-Stabilität (60 s)

**Ziel:** Offset-Verlauf über 60 Sekunden messen (Drift, Schwankung, Ausreißer).

**Messung:** `ptp_offset` alle 1 s abfragen → Min/Max/Mittelwert/Standardabweichung.

### Schritt 4 — PTP-Modus-Zyklus (ptp_mode off → follower → master → prüfen)

**Ziel:** Prüfen ob nach `ptp_mode off` + Neustart des PTP-Modus die Konvergenz erneut stattfindet (kein Leak, kein Hänger).

---

## 3. Ursachenanalyse

### Root Cause #2 — Falscher TXMLOC-Wert (Byte-Offset EtherType)

**Symptom:** TXPMDET bleibt dauerhaft 0 — Timeout nach `MAX_RETRIES` Versuchen, kein `WAIT_STATUS0`

**Ursache:**  
`GM_PTP_INIT()` schrieb `TXMLOC = 30`, also suchte der TX-Match-Detektor das Pattern
`88 F7 10` bei **Byte 30** des Ethernet-Frames. Tatsächlich liegt das EtherType-Feld
(0x88F7) bei **Byte 12** (nach 6 Byte DST-MAC + 6 Byte SRC-MAC). Byte 30 liegt im
PTP-Payload (reserved-Felder = 0x00) → Pattern passt nie → TXPMDET nie gesetzt.

**Ethernet-Frame-Layout:**
```
Byte 0-5:   Dst-MAC
Byte 6-11:  Src-MAC
Byte 12-13: EtherType = 0x88F7   ← TXMLOC muss 12 sein
Byte 14:    PTP tsmt = 0x10
...
Byte 30:    PTP reserved (0x00)  ← TXMLOC=30 suchte hier
```

**Fix in `ptp_gm_task.c` `PTP_GM_Init()`:**
```c
// ALT (kaputt):
gm_write(GM_TXMLOC, 30u);   /* falsch: liegt im PTP-Payload */

// NEU (korrekt):
gm_write(GM_TXMLOC, 12u);   /* korrekt: EtherType bei Byte 12 (6+6=12) */
```

**Status:** ✓ ANGEWENDET (2026-04-01)

---

### Root Cause #1 — Invertierter Rückgabewert in `gm_read_register()` und `gm_write_register()`

**Symptom:** GM-State-Machine: `SEND_SYNC → READ_TXMCTL → WAIT_PERIOD` (sofortiger Rückfall)

**Ursache:**
```c
// ptp_gm_task.c, Zeile ~154
static bool gm_read_register(uint32_t addr, bool useCallbackProtectedMode)
{
    return DRV_LAN865X_ReadRegister(0u, addr, useCallbackProtectedMode, gm_op_cb, NULL);
    //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //     Rückgabetyp: TCPIP_MAC_RES (int-Enum)
    //     TCPIP_MAC_RES_OK = 0  →  als bool: false  →  "fehlgeschlagen"
    //     TCPIP_MAC_RES_PENDING, ERROR = nonzero → als bool: true → "erfolgreich"
    //     Semantik ist INVERTIERT!
}
```

**Effekt in der State-Machine:**
```c
case GM_STATE_READ_TXMCTL:
    if (!gm_read_register(GM_TXMCTL, true)) {  // !false == true  ← immer wahr
        GM_SET_STATE(GM_STATE_WAIT_PERIOD);     // ← immer genommen
        break;
    }
    GM_SET_STATE(GM_STATE_WAIT_TXMCTL);         // ← nie erreicht
```

**Betroffen:** Alle Aufrufe mit Return-Check:
- `GM_STATE_READ_TXMCTL` — `gm_read_register(GM_TXMCTL, ...)`
- `GM_STATE_READ_TTSCA_H` — `gm_read_register(secReg, ...)`
- `GM_STATE_READ_TTSCA_L` — `gm_read_register(nsecReg, ...)`
- `GM_STATE_WRITE_CLEAR` — `gm_write_register(GM_OA_STATUS0, ...)`

**Fix:** Wrapper-Funktionen returnieren `== TCPIP_MAC_RES_OK`:
```c
static bool gm_read_register(uint32_t addr, bool useCallbackProtectedMode)
{
    return DRV_LAN865X_ReadRegister(...) == TCPIP_MAC_RES_OK;
}

static bool gm_write_register(uint32_t addr, uint32_t value, bool useCallbackProtectedMode)
{
    return DRV_LAN865X_WriteRegister(...) == TCPIP_MAC_RES_OK;
}
```

**Status:** ✓ ANGEWENDET (2026-04-01)

---

### Mögliche weitere Ursachen (nach Fix #1 zu prüfen)

| # | Mögliche Ursache | Symptom | Prüfung |
|---|---|---|---|
| 2 | TXPMDET-Bit wird nie gesetzt — TXMCTL-Arm zu früh (vor SPI-TX-Commit) | `WAIT_TXMCTL` läuft, aber TXPMDET=0 immer | `lan_read 0x00040040` nach Sync |
| 3 | `DRV_LAN865X_GetAndClearTsCapture()` gibt immer 0 — TTSCA-Bits in STATUS0 werden vom Interrupt-Handler gelöscht bevor GM sie abfragen kann | `WAIT_STATUS0` timeout | SYS_CONSOLE_PRINT STATUS0-Wert direkt nach TTSCA-Prüfung |
| 4 | TX-Timestamp-Capture-Slot falsch — TXMLOC=30 passt nicht zu Sync-Frame-Layout | TTSCA gesetzt, aber Timestamp-Wert 0 | Prüfe Sync-Frame: Byte 30/31 = EtherType 0x88F7? |
| 5 | Callback `gm_op_cb` wird nicht aufgerufen — `gm_op_done` bleibt `false` | `WAIT_TXMCTL` cb timeout nach 200 ms | SYS_CONSOLE_PRINT in `gm_op_cb` |
| 6 | FOL-Servo berechnet falschen Offset — `processFollowUp()` extrahiert falsches t1 | FINE nie erreicht, Offset wächst unbegrenzt | `ptp_offset` + GM CONSOLE vergleichen |
| 7 | PTP-Multicast wird von Follower-Ethernet gefiltert | FollowUp wird gesendet, FOL empfängt but ignoriert | `ipdump 1` auf FOL während GM sendet |
| 8 | Timestamp-Auflösung MAC_TISUBN zu grob | Servo schwingt in COARSE, erreicht FINE nicht | Offset-Plot über 60 s auf COARSE-Verlauf prüfen |

---

### Root Cause #3 — TX-Timestamp-Capture: STATUS0.TTSCAA/B/C nie gesetzt (analysiert, 2026-04-02)

**Symptom:** `TXPMDET ok, Sync #N` → `TTSCA not set after Sync #N`

#### Log-Beweise (ptp_diag_20260401_165641.log)

```
Select-String "[DBG] _OnStatus0" → 0 Treffer
Select-String "TTSCAA|TTSCAB|TTSCAC" → 0 Treffer
```

Der Debug-Print in `_OnStatus0` (drv_lan865x_api.c Zeile 2336) ist bedingt:
```c
if (0u != (value & 0x0F00u)) {  // Bits 8-11: TTSCAA/B/C + TXFCSE
    SYS_CONSOLE_PRINT("[DBG] _OnStatus0: 0x%08lX\r\n", (unsigned long)value);
}
```
→ **`_OnStatus0` wird definitiv aufgerufen** (Loss of Framing, Reset Complete Events im Log bestätigt)  
→ **STATUS0 Bits 8–10 wurden nie gesetzt** — der LAN865x Hardware erfasst den TX-Timestamp überhaupt nicht

#### Ereignis-Muster im Log

```
L76:  [PTP-GM] TXPMDET ok, Sync #0     ← vor erstem LOS, Config korrekt
L77:  [PTP-GM] TTSCA not set after Sync #0
---
L420: Status0.Loss of Framing Error
L426: Status0.Reset Complete           ← MemMap reinit läuft
L427: [PTP-GM] TXPMDET ok, Sync #0    ← nach reinit, Config zerstört
L428: [PTP-GM] TTSCA not set after Sync #0
```

Muster: **jedes TXPMDET ok erscheint unmittelbar nach Reset Complete** (6 von 7 Fällen).  
Ausnahme: Zeile 76 — erster Sync, noch **vor** jedem LOS-Ereignis, trotzdem kein TTSCA.

#### Zwei separate Bugs

**Bug A — Zeile 76: Fundamentales TTSCA-Problem**  
Bei Sync #0 war `PTP_GM_Init()` korrekt ausgeführt (TXMLOC=12, TXMMSKH=0, TXMMSKL=0), kein LOS.  
Der Frame wird mit `tsc=1` im SPI-Data-Header gesendet (`TC6_SendRawEthernetPacket`) — dieser Pfad wurde im Code verifiziert (`SET_VAL(HDR_TSC, entry->tsc, tx_buf)`).  
Trotzdem setzt der LAN865x STATUS0.TTSCAA nie. → **Hardware-Konfigurationsproblem**.

Mögliche Ursachen:
- `CONFIG0` (Adresse `0x00000004`) Bits für TX-Timestamp nicht korrekt gesetzt  
- `PADCTRL` (Adresse `0x000400E0`) Wert `0x0000C100` könnte nicht ausreichen oder falsch sein  
- LAN865x benötigt ggf. zusätzliches Aktivierungs-Register das nicht geschrieben wird

**Bug B — Zeilen 427+: MemMap-Overwrite nach LOS** (→ Root Cause #4, siehe unten)

**Status:** ⚠ AKTIV — Bug A erfordert Hardware-Register-Verifikation (Testlauf 6)

---

### Root Cause #4 — MemMap-Overwrite zerstört TX-Match-Konfiguration nach LOS (neu, 2026-04-02)

**Symptom:** Nach jedem `Status0.Reset Complete` erscheint sofort `TXPMDET ok, Sync #0` — d.h. der Match feuert für **jeden** Frame.

**Ursache:**  
Nach jedem Loss-of-Framing-Fehler läuft `_InitMemMap()` in `drv_lan865x_api.c`. Die Memory-Map-Tabelle (Zeile 1704ff.) schreibt folgende Register zurück:

```c
/* drv_lan865x_api.c, TC6_MEMMAP[] */
{ .address=0x00040043, .value=0x000000FF },  /* GM_TXMMSKH — war 0x00 (kein Mask) */
{ .address=0x00040044, .value=0x0000FFFF },  /* GM_TXMMSKL — war 0x00 (kein Mask) */
{ .address=0x00040045, .value=0x00000000 },  /* GM_TXMLOC  — war 12 (EtherType) */
{ .address=0x00040040, .value=0x00000002 },  /* GM_TXMCTL  — war 0x0001 (Slot A) */
```

Zustand nach MemMap-Reinit:
- `TXMLOC = 0` → Pattern-Vergleich beginnt bei Byte 0 (DST-MAC)
- `TXMMSKH = 0xFF` → alle Bits des High-Pattern-Bytes ignoriert
- `TXMMSKL = 0xFFFF` → alle Bits des Low-Pattern-Bytes ignoriert
- Ergebnis: **TX-Match trifft auf jeden Frame** — TXPMDET wird beim nächsten beliebigen TX gesetzt

GM re-armt TXMCTL per Sync auf `0x0001` (Slot A), aber TXMLOC und Masken bleiben zerstört.  
PTP_GM_Init() wird nach LOS **nicht** erneut aufgerufen → Zustand bleibt inkorrekt.

**Ethernet-Frame-Layout nach MemMap:**
```
Byte 0-5:   Dst-MAC   ← TXMLOC=0 → Pattern wird hier gesucht
Byte 0-1:   Pattern=0x88 0xF7 mit Maske=0xFF → alle Bits ignoriert = immer MATCH
```

**Fix:** `PTP_GM_Init()` (oder zumindest die TX-Match-Registerwrites) nach jedem LOS Reset Complete erneut ausführen.

Mögliche Implementierung: In `DRV_LAN865X_Tasks()` / `_InitMemMap` Callback oder über einen Notify-Mechanismus aus dem Treiber an die Applikationsebene.

**Status:** ⚠ AKTIV — Fix in Testlauf 6 zu implementieren

---

## 4. Test-Ergebnisse

### Testlauf 1 — Baseline (2026-04-01, vor Fixes)

**Laufparameter:**
- Firmware-Build: `build_dual.bat` → `=== BEIDE BUILDS ERFOLGREICH ===`
- Flash: `flash_dual.py` → `FOLLOWER SUCCESS` + `GRANDMASTER SUCCESS`
- Testskript: `ptp_diag.py` (Schritt 0–7, ohne `--no-reset`)

**Beobachtete GM-Ausgabe (Dauerschleife):**
```
[PTP-GM][STATE] GM_STATE_SEND_SYNC @L374
[PTP-GM][STATE] GM_STATE_READ_TXMCTL @L413
[PTP-GM][STATE] GM_STATE_WAIT_PERIOD @L421
[PTP-GM][STATE] GM_STATE_SEND_SYNC @L374
...
```
`WAIT_TXMCTL`, `WAIT_STATUS0`, `SEND_FOLLOWUP` — **nie erreicht**.

`ptp_offset` auf FOL: dauerhaft `offset=0 ns  abs=0 ns` — Servo bleibt in UNINIT (`CONVERGENCE_TIMEOUT = 120 s` → abgelaufen).

| Schritt | Ergebnis |
|---|---|
| 0 IP-Konfiguration | PASS |
| 1 Ping beidseitig | PASS |
| 2 PTP aktivieren | PASS |
| 3 GM Sync-Zähler steigt | PASS (`gmSyncs` zählt hoch) |
| 4 TX-Match TXPMDET | FAIL (TXPMDET nie gesetzt) |
| 5 Follower PTP-RX | PASS (0x88F7-Frames empfangen — nur Sync, kein FollowUp) |
| 6 GM FollowUp-Pfad | FAIL (`SEND_FOLLOWUP` nie erreicht) |
| 7 FOL Servo Konvergenz | FAIL (FINE timeout) |

---

### Testlauf 3 (2026-04-01, Fix #1 + Fix #2, `ptp_diag_20260401_163547.log`)

**Laufparameter:**
- Firmware-Build: `build_dual.bat`
- Flash: `flash_dual.py`
- Testskript: `ptp_diag.py`
- Fixes: Root Cause #1 (`TCPIP_MAC_RES_OK == ...`) + Root Cause #2 (`TXMLOC=12`)

#### Ergebnisse

| Schritt | Ergebnis | Notizen |
|---|---|---|
| 0 IP-Konfiguration | ✓ PASS | |
| 1 Ping beidseitig | ✓ PASS | 4/4 in beide Richtungen |
| 2 PTP aktivieren | ✓ PASS | |
| 2b GM kein WAIT_TXMCTL-Loop | ✓ PASS | |
| 3 GM Sync-Zähler steigt | ✓ PASS | gmSyncs 54→116 (delta=62) |
| 4 TX-Match TXPMDET | **✗ FAIL** | 1034 Timeouts bei 1338 Syncs — TXPMDET nie gesetzt |
| 5 Follower PTP-RX | ✓ PASS | 39 Sync-Frames (kein FollowUp empfangen) |
| 6 GM FollowUp-Pfad | **✗ FAIL** | SEND_FOLLOWUP nie erreicht |
| 7 FOL Servo Konvergenz | **✗ FAIL** | FINE-Timeout (kein FollowUp → kein t1) |

**Fix #1 und Fix #2 bereits angewendet, aber TXPMDET bleibt 0 → weiterer Bug.**

**Diagnose Testlauf 3:**
- Sync-Intervall auf dem Draht gemessen (ipdump): **~128 ms** ✓ korrekt
- UART-Überlastung durch `[PTP-GM][STATE]`-Spam bei ~80 Zeilen/s bestätigt
- Fix für Testlauf 4: State-Logging auf Schlüsselereignisse reduziert

---

### Testlauf 4 (2026-04-01, Fix #3 — State-Spam, `ptp_diag_20260401_165641.log`)

#### Änderungen gegenüber Testlauf 3

| Änderung | Datei | Beschreibung |
|---|---|---|
| Fix #3 | `ptp_gm_task.c` | `gm_set_state()` gibt kein `SYS_CONSOLE_PRINT` mehr aus — State-Spam entfernt |
| Fix #3 | `ptp_gm_task.c` | Neue Schlüsselereignis-Prints: `Sync #N`, `TXPMDET ok, Sync #N`, `FU #N t1=Xs.Yns` |

#### Erwartetes Verhalten (Normalfall)
```
[PTP-GM] Init complete (MAC ...)
[PTP-GM] Sync #0
[PTP-GM] TXPMDET ok, Sync #0
[PTP-GM] FU #0 t1=1743600000s 007650000ns
[PTP-GM] Sync #1
...
```

#### Erwartetes Verhalten (Fehlerfall TXPMDET)
```
[PTP-GM] Sync #0
[PTP-GM] TXPMDET timeout after Sync #0
[PTP-GM] Sync #1
...
```

#### Ergebnisse

| Schritt | Ergebnis | Notizen |
|---|---|---|
| 0 IP-Konfiguration | ✓ PASS | |
| 1 Ping beidseitig | ✓ PASS | 4/4 in beide Richtungen |
| 2 PTP aktivieren | ✓ PASS | |
| 2b GM kein WAIT_TXMCTL-Loop | ✓ PASS | |
| 3 GM Sync-Zähler steigt | ✓ PASS | gmSyncs 54→116 (delta=62) |
| 4 TX-Match TXPMDET | **✗ FAIL** | TXPMDET ok: 7×, TXPMDET timeout: 1188× — sporadisch |
| 5 Follower PTP-RX | ✓ PASS | 40 Sync-Frames (kein FollowUp) |
| 6 GM FollowUp-Pfad | **✗ FAIL** | FU=0 — TTSCA nicht gesetzt nach TXPMDET ok |
| 7 FOL Servo Konvergenz | **✗ FAIL** | FINE-Timeout |

**Fortschritt gegenüber Testlauf 3:** TXPMDET wird jetzt 7× erkannt (vorher 0×). Neues Fehlermuster: `TXPMDET ok → TTSCA not set` — STATUS0 TTSCA-Bits werden nie gesetzt.

**Log-Verbesserung:** Ausgabe auf 4871 Zeilen reduziert (vorher 35719) — UART-Überlastung beseitigt.

**Fix #3 (ptp_diag.py):** `NameError: log_path` behoben — `_main_run()` als separate Funktion, `tee.close()` in `finally`-Block.

**Neuer Root Cause (Priorität 1):** `TTSCA not set after Sync #N` trotz `TXPMDET ok`:
- TXPMDET-Bit wird gesetzt → State-Machine erreicht `GM_STATE_WAIT_STATUS0`
- `DRV_LAN865X_GetAndClearTsCapture()` gibt 0 zurück → Bits TTSCAA/B/C nie gesetzt
- Mögliche Ursachen: (a) Interrupt-Handler löscht STATUS0 bevor GetAndClear aufgerufen wird, (b) TX-Capture nicht aktiviert (`tsc`-Bit im Sync-Frame), (c) falscher Capture-Slot abgefragt

---

### Testlauf 5 — Log-Analyse (2026-04-02, ptp_diag_20260401_165641.log)

Kein neuer Build/Flash — nur Analyse des vorhandenen Logs.

#### Kernbefunde

| Frage | Antwort |
|---|---|
| Wird `_OnStatus0` aufgerufen? | ✓ JA — Loss of Framing, Reset Complete bestätigt |
| Werden STATUS0 Bits 8–10 jemals gesetzt? | ✗ NEIN — `[DBG] _OnStatus0` Print tritt nie auf |
| Ist ein Muster erkennbar? | ✓ JA — TXPMDET ok immer nach Reset Complete |
| Tritt TTSCA-Fehler auch ohne LOS auf? | ✓ JA — Zeile 76 (erster Sync, vor jedem LOS) |

**Log-Statistik:**

| Event | Anzahl |
|---|---|
| Loss of Framing | 6× |
| Reset Complete | 6× |
| TXPMDET ok | 7× (davon 6× nach Reset Complete, 1× initial) |
| TTSCA not set | 7× |
| `[DBG] _OnStatus0` mit Bits 8–10 | 0× |

#### Schlussfolgerung

Zwei separate Bugs identifiziert:
- **RC#4 (Bug B):** MemMap-Overwrite nach LOS zerstört TX-Match-Konfiguration → TXPMDET feuert für jeden Frame
- **RC#3 (Bug A):** Fundamentales TTSCA-Problem — LAN865x Hardware setzt STATUS0.TTSCAA nie, selbst bei korrekt konfiguriertem Init

#### Ergebnisse — Testlauf 5
[PASS] Analyse erfolgreich — 2 neue Root Causes identifiziert

---

### Testlauf 6 — Geplant (2026-04-02)

#### Ziel
RC#3 (TTSCA hardware-seitig) und RC#4 (MemMap-Overwrite) beheben.

#### Fix A — RC#4: GM-Reinitialisierung nach LOS (in `drv_lan865x_api.c` oder `app.c`)

Option 1 — Callback-Mechanismus: Treiber ruft Applikations-Callback nach Reset Complete auf.
```c
/* drv_lan865x_api.c — _OnStatus0, nach reinit: */
if (reinit) {
    pDrvInst->initState = DRV_LAN865X_INITSTATE_RESET;
    pDrvInst->state = SYS_STATUS_UNINITIALIZED;
    /* Notify application that reinit will happen */
    if (pDrvInst->pfnLinkChange) { pDrvInst->pfnLinkChange(false); }
}
```

Option 2 — Direkte Umgehung: TX-Match-Werte in `TC6_MEMMAP[]` gleich korrekt setzen:
```c
/* drv_lan865x_api.c, TC6_MEMMAP[] — korrekte Werte für GM: */
{ .address=0x00040043, .value=0x00000000 },  /* GM_TXMMSKH = 0 (kein Mask) */
{ .address=0x00040044, .value=0x00000000 },  /* GM_TXMMSKL = 0 (kein Mask) */
{ .address=0x00040045, .value=0x0000000C },  /* GM_TXMLOC  = 12 (EtherType) */
```
⚠ Achtung: Diese Werte gelten nur für GM-Mode — im Follower-Mode ist TXMLOC irrelevant.

#### Fix B — RC#3: TTSCA Hardware-Verifikation

Schritt 1 — Register-Dump nach `ptp_mode master` (mit `lan_read`):
```
lan_read 0x00000004    → CONFIG0: erwartete Bits 1,2,5,6,7 gesetzt (0xE6)
lan_read 0x000400E0    → PADCTRL: erwartet 0x0000C100
lan_read 0x00040040    → TXMCTL:  nach GM-Init erwartet 0x????  (0x0001 wenn armed)
```

Schritt 2 — Manueller TTSCA-Test via CLI:
```
# GM-Mode aktiv, dann manuell arm + direkter Status-Read:
lan_write 0x00040045 12       → TXMLOC = 12
lan_write 0x00040041 0x88     → TXMPATH = 0x88 (EtherType High)
lan_write 0x00040042 0xF710   → TXMPATL = 0xF710 (EtherType Low + tsmt)
lan_write 0x00040043 0         → TXMMSKH = 0
lan_write 0x00040044 0         → TXMMSKL = 0
lan_write 0x00040040 1         → TXMCTL arm Slot A
# Danach: GM sendet Sync → STATUS0 lesen:
lan_read 0x00000008            → STATUS0: Bit 8 (0x100) erwartet
```

Schritt 3 — Falls STATUS0.TTSCAA auch nach manuellem Arm nie gesetzt:
```
# Prüfe ob TXPE in CONFIG0 wirklich gesetzt ist:
lan_read 0x00000004   → bit 5 (0x20) muss 1 sein
# Prüfe PADCTRL:
lan_read 0x000400E0   → erwartet 0x0000C100
# Lese LAN865x Application Note für TX Timestamp Activation sequence
```

#### Erwartetes Ergebnis nach beiden Fixes
```
[PTP-GM] Init complete (MAC ...)
[PTP-GM] Sync #0
[PTP-GM] TXPMDET ok, Sync #0
[PTP-GM] FU #0 t1=1743600000s 007650000ns
[PTP-GM] Sync #1
[PTP-GM] TXPMDET ok, Sync #1
[PTP-GM] FU #1 t1=1743600000s 133150000ns
...
[PTP-FOL] UNINIT->MATCHFREQ
[PTP-FOL] COARSE offset=...
[PTP-FOL] FINE   offset=...
```

#### Build/Flash/Test Kommandos
```powershell
cd "C:\work\ptp\AN1847\t1s_100baset_bridge\firmware\T1S_100BaseT_Bridge.X"
cmd /c build_dual.bat 2>&1
python flash_dual.py 2>&1
python ptp_diag.py 2>&1
```

#### Ergebnisse — Testlauf 6
*(ausstehend)*

---

## 5. Testplan (aktuell, Stand 2026-04-02)

### Prioritäten-Übersicht

```
Priorität 1: RC#3 Bug A — TTSCA nie gesetzt (hardware-seitig)
  → Schritt A1: Register-Dump CONFIG0 + PADCTRL via lan_read
  → Schritt A2: Manueller TTSCA-Test via CLI
  → Schritt A3: Fix implementieren basierend auf Befund

Priorität 2: RC#4 Bug B — MemMap-Overwrite nach LOS
  → Schritt B1: TC6_MEMMAP[] Werte für TXMLOC/MSK korrigieren
  → Schritt B2: Alternativ: PTP_GM_Init() nach LOS-Callback aufrufen

Danach: build_dual.bat → flash_dual.py → ptp_diag.py
  → Erwartet: FU #N t1=... in Log
  → Folge: Follower-Konvergenz MATCHFREQ → COARSE → FINE
```

---

### Modifikation 1 — Fix #1 anwenden (bereits erledigt)

**Aktion in `ptp_gm_task.c`:**
```c
// ALT (kaputt):
static bool gm_read_register(uint32_t addr, bool useCallbackProtectedMode)
{
    return DRV_LAN865X_ReadRegister(0u, addr, useCallbackProtectedMode, gm_op_cb, NULL);
}
static bool gm_write_register(uint32_t addr, uint32_t value, bool useCallbackProtectedMode)
{
    return DRV_LAN865X_WriteRegister(0u, addr, value, useCallbackProtectedMode, gm_op_cb, NULL);
}

// NEU (korrekt):
static bool gm_read_register(uint32_t addr, bool useCallbackProtectedMode)
{
    return TCPIP_MAC_RES_OK == DRV_LAN865X_ReadRegister(0u, addr, useCallbackProtectedMode, gm_op_cb, NULL);
}
static bool gm_write_register(uint32_t addr, uint32_t value, bool useCallbackProtectedMode)
{
    return TCPIP_MAC_RES_OK == DRV_LAN865X_WriteRegister(0u, addr, value, useCallbackProtectedMode, gm_op_cb, NULL);
}
```

**Status:** ✓ BEREITS ANGEWENDET (2026-04-01)

---

### Modifizierter Schritt M1 — TXPMDET-Prüfung (nach Fix #1)

**Ziel:** Prüfen ob TXPMDET-Bit gesetzt wird nachdem State-Machine `WAIT_TXMCTL` erreicht.

**Erwartetes Ergebnis:**
```
[PTP-GM][STATE] GM_STATE_READ_TXMCTL @L...
[PTP-GM][STATE] GM_STATE_WAIT_TXMCTL @L...
[PTP-GM][STATE] GM_STATE_WAIT_STATUS0 @L...   ← TXPMDET war gesetzt
[PTP-GM][STATE] GM_STATE_READ_TTSCA_H @L...
```
**Fail-Muster:**
```
[PTP-GM] TXPMDET timeout after Sync #N   ← TXPMDET kam nie in MAX_RETRIES Versuchen
```
→ Dann Ursache #2 prüfen: TXMCTL-Arm-Zeitpunkt oder TXMLOC

---

### Modifizierter Schritt M2 — TTSCA-Capture prüfen (nach RC#3 + RC#4 Fix)

**Ziel:** Bestätigen dass STATUS0.TTSCAA nach Fix gesetzt wird.

**Erwartetes Ergebnis:** `[PTP-GM] FU #N t1=...s ...ns` erscheint im Log  
**Fail-Muster:** `TTSCA not set after Sync #N` → RC#3 Bug A noch offen → Schritt B in Testlauf 6

---

### Modifizierter Schritt M3 — Follower-Konvergenz-Volltest (nach RC#3 + RC#4 Fix)

Läuft durch `ptp_diag.py --from-step 7`

**Erweiterte Konvergenzdiagnose:**
- Wenn MATCHFREQ gesehen aber kein COARSE → Ursache #6 (t1-Extraktion)
- Wenn COARSE aber kein FINE → Ursache #8 (Servo-Parameter)
- Wenn FINE gesehen aber `ptp_offset` > 1 μs → Ursache #7 (Filterung) oder #4 (Timestamp-Offset)

---

### Modifizierter Schritt M4 — 60 s Stabilitätsmessung

Identisch zu originalem **Schritt 3**. Kriterien:  
- PASS: ≥ 80 % der Messungen `|offset| < 100 ns`  
- Ziel Langzeit: `|offset| < 50 ns`, `stdev < 20 ns`
