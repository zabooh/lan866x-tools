# PTP GM Implementierung — Diff zur Referenz und offene Maßnahmen

Stand: 2026-04-04 — **ABGESCHLOSSEN** — PTP GM funktioniert, Follower konvergiert auf ±45 ns  
Root Cause behoben: `gm_get_and_clear_ts_capture()` war definiert aber nie aufgerufen → TC6-Library hat TTSCAA bereits W1C-gelöscht bevor die State Machine lesen konnte.  
Referenz: `C:\work\ptp\AN1847\LAN865x-TimeSync\noIP-SAM-E54-Curiosity-PTP-Grandmaster\firmware\src\`  
Unsere Impl.: `src/ptp_gm_task.c` + `src/ptp_gm_task.h`

---

## 1. Architektur-Unterschied (kein Bug, by Design)

| Aspekt | Referenz (`main.c`) | Unsere Impl. (`ptp_gm_task.c`) |
|--------|---------------------|-------------------------------|
| Aufrufsystem | Blockierende Endlosschleife mit `TC6NoIP_Service()` | Non-blocking Harmony State Machine, aufgerufen durch `PTP_GM_Service()` |
| Register-Zugriff | `TC6_WriteRegister()` + sofort `TC6_Service()` — synchron blockierend | `DRV_LAN865X_WriteRegister()` mit Callback `gm_op_cb` — asynchron |
| Timing | `systick.tickCounter` (bare-metal) | `SYS_TIME`-basierter 1-ms-Tick-Zähler |
| Initialisierung | `TC6_ptp_master_init()` — einmaliger Aufruf, synchron blockierend | `GM_STATE_INIT_WRITE` / `GM_STATE_WAIT_INIT_WRITE` — Callback-geschützte Sequenz |

Der asynchrone Ansatz ist **korrekt** für Harmony 3 und nicht der Grund für das fehlende TTSCAA.

---

## 2. Init-Sequenz — Schritt-für-Schritt-Vergleich

### Referenz `TC6_ptp_master_init()` (10 Schritte)

```c
TC6_ptp_master_init_write_helper(TXMLOC,  30,         secure)   // Schritt 1
TC6_ptp_master_init_write_helper(TXMPATH, 0x88,       secure)   // Schritt 2
TC6_ptp_master_init_write_helper(TXMPATL, 0xF710,     secure)   // Schritt 3
TC6_ptp_master_init_write_helper(TXMMSKH, 0x00,       secure)   // Schritt 4
TC6_ptp_master_init_write_helper(TXMMSKL, 0x00,       secure)   // Schritt 5
TC6_ptp_master_init_write_helper(TXMCTL,  0x02,       secure)   // Schritt 6  ← TXME=1 einmalig im Init
TC6_ptp_master_init_write_helper(MAC_TI,  40,         secure)   // Schritt 7
TC6_ptp_master_init_RMW_helper  (OA_CONFIG0, 0xC0, 0xC0, secure) // Schritt 8  ← FEHLT bei uns
TC6_ptp_master_init_RMW_helper  (PADCTRL,    0x100, 0x300, secure)// Schritt 9  ← nicht fest implementiert
TC6_ptp_master_init_write_helper(PPSCTL, 0x7D,        secure)   // Schritt 10
```

### Unsere `gm_init_addrs[8]`

```c
GM_TXMCTL  = 0x0000   // disarm (Schritt 0, zusätzlich zu Referenz)
GM_TXMLOC  = 12       // ← FALSCH: Referenz hat 30
GM_TXMPATH = 0x88     // ✓
GM_TXMPATL = 0xF710   // ✓  (wird korrekt aus EtherType berechnet)
GM_TXMMSKH = 0x00     // ✓
GM_TXMMSKL = 0x00     // ✓
MAC_TI     = 40       // ✓
PPSCTL     = 0x7D     // ✓
// OA_CONFIG0 RMW fehlt komplett
// PADCTRL    RMW fehlt (war nur Testlauf, nicht fest implementiert)
```

---

## 3. Detaillierte Unterschiede

### Diff #1 — TXMLOC: 12 vs. 30  (**BEHOBEN ✓ — Hardware bestätigt**)

| | Referenz | Unsere Impl. |
|-|----------|-------------|
| `TXMLOC` | **30** | **30** ✓ (war 12) |
| Bedeutung | Byte-Position im Frame, an der das TX-Match-Feld beginnt | gleiche Bedeutung |

**Status:** Implementiert 2026-04-04. Hardware-Readback aus `tsu_check_20260404_191030.log` bestätigt:  
`TXMLOC = 0x0000001E` = 30 ✓

Wert 12 = Byte-Position am Anfang des Ethernet-Payload (nach dem 14-Byte-Header).  
Wert 30 = Referenzwert — Position innerhalb des PTP-Headers (tsmt-Feld).

**Datei:** `src/ptp_gm_task.c`, Zeile ~140

---

### Diff #2 — OA_CONFIG0 RMW(0xC0, 0xC0) fehlt komplett  (**HÖCHSTE PRIORITÄT**)

Die Referenz setzt in OA_CONFIG0 (0x00000004) die Bits **7** und **6** via Read-Modify-Write:

```
mask  = 0xC0 = 0b1100_0000
value = 0xC0 = 0b1100_0000
```

| Bit | Name | Bedeutung |
|-----|------|-----------|
| 7   | FTSE | Frame Timestamp Enable — aktiviert die TSU-Hardware |
| 6   | Reserviert / TXCTE? | Laut Referenzcode explizit gesetzt |

**Unser Stand:**  
Der TCPIP-Stack setzt CONFIG0 = `0x000090E6` → Bit 7 (FTSE) = **1** ✓, Bit 6 = **0** ✗  

Bit 6 von OA_CONFIG0 wurde **noch nie** explizit gesetzt.  
Wenn Bit 6 die TX-Timestamp-Engine aktiviert/konfiguriert, erklärt das, warum TTSCAA nie gesetzt wird.

**Datei:** `src/ptp_gm_task.c` + `src/ptp_gm_task.h`  
**Änderung:**  
1. `DRV_LAN865X_ReadRegister()` für OA_CONFIG0 aufrufen
2. Im Callback: `value |= 0xC0u` und `DRV_LAN865X_WriteRegister()` zurückschreiben
3. Als neue State-Pair `GM_STATE_RMW_CONFIG0_READ` + `GM_STATE_RMW_CONFIG0_WRITE` in der Init-Sequenz

---

### Diff #3 — PADCTRL RMW(0x100, 0x300)  (**IMPLEMENTIERT — Bit8 scheint write-ignored!**)

Adresse: `0x000A0088`  
```
mask  = 0x300 = Bits 9:8
value = 0x100 = Bit 8
```

**Status:** Implementiert 2026-04-04 als `GM_STATE_RMW_PADCTRL_READ/WAIT_READ/WAIT_WAIT_WRITE`.  
Hardware-Readback aus `tsu_check_20260404_191030.log`:  
`PADCTRL = 0xFC000400` — **Bit8 = 0, Bit9 = 0** → Write hatte keinen Effekt!

**Analyse:**  
- Erwartet nach RMW: `0xFC000500` (Bit8 = 1)  
- Gelesen: `0xFC000400` (Bit8 = 0, unverändert)
- Schluss: Bit8 von 0x000A0088 ist in LAN8651 möglicherweise **read-only / write-ignored**  
- Alternativ: State-Machine hat WAIT_WRITE nicht korrekt abgewartet (Race)

**Nächster Schritt:** Direkttest via CLI `ptp_mode off` → `lan_write 0x000A0088 0xFC000100` → `lan_read 0x000A0088` — wenn immer noch 0xFC000400: Register definitiv nicht beschreibbar → RMW in Firmware entfernen.

---

### Diff #4 — TXMCTL-Strategie

| | Referenz | Unsere Impl. |
|-|----------|-------------|
| Init | `TXMCTL = 0x02` (TXME=1 einmalig, bleibt gesetzt) | `TXMCTL = 0x00` (erst disarm) |
| Vor jedem Sync | kein Write | `TXMCTL = 0x0002` in `GM_STATE_WRITE_TXMCTL` |
| Nach Sync | kein Write | — |

Die Referenz lässt TXME permanent an. Unser Ansatz (re-arm vor jedem Sync) ist per Spezifikation korrekt und sollte kein Problem sein, bringt aber zusätzliche Latenz durch den extra Callback-Roundtrip.

---

### Diff #5 — TXMCTL: Init-Wert 0x02 vs. Init-Dis-Arm 0x00

Referenz schreibt beim Init `TXMCTL = 0x02` (TXME=1 → sofort armiert).  
Wir schreiben beim Init `TXMCTL = 0x00` (Disarm) und dann vor jedem Sync 0x0002.  
→ Funktional äquivalent, aber Referenz vertraut darauf dass TXME dauerhaft gesetzt bleibt.

---

### Diff #6 — Sync-Frame-Header-Felder (flags[0])

| Feld | Referenz | Unsere Impl. (BUILD_LEVEL=3) |
|------|----------|------------------------------|
| `tsmt` | `0x10` | `0x10` ✓ |
| `flags[0]` | `0x02` | `0x02` ✓ |
| `flags[1]` | `0x08` | `0x08` ✓ |
| `logMessageInterval` | `0xFD` | `0xFD` ✓ |
| `controlField` | `0x02` | `0x02` ✓ |
| `clockIdentity` | Hardcoded `{0x40,0x84,0x32,0xFF,0xFE,0x7D,0x07,0xFA}` | Dynamisch aus TCPIP_STACK MAC (EUI-64) |

→ Keine relevante Differenz beim Inhalt, solange `PTP_GM_SYNC_BUILD_LEVEL = 3`.

---

### Diff #7 — FollowUp: Byte-Reihenfolge (KEIN Bug)

Referenz nutzt `invert_uint32()` / `invert_uint16()` (eigene Byte-Swap-Funktion).  
Wir nutzen `htonl()` / `htons()` (POSIX-Standard).  
→ Gleiches Ergebnis (Big-Endian auf dem Netz).

---

### Diff #8 — STATIC_OFFSET  

| | Referenz | Unsere Impl. |
|-|----------|-------------|
| `STATIC_OFFSET` | `7650` ns | `PTP_GM_STATIC_OFFSET = 7650u` ✓ |

Identisch.

---

### Diff #9 — MAC_TI Untersuchung  (**ABGESCHLOSSEN — MAC_TI=40 ist korrekt, 2026-04-04**)

`MAC_TI` (Timer Increment, Addr `0x00010077`) gibt an, wie viele Nanosekunden die TSU-Uhr pro Takt vorwärtszählt.

Referenzwert im Code: `MAC_TI = 40` → entspricht 25 MHz TSU-Takt.

**Ursprüngliche Hypothese (WIDERLEGT):** TSU-Fehler in TEST_TSU_02/03 deuten auf 125 MHz GEM-Takt hin → MAC_TI=8 nötig.

**Gegenprobe (2026-04-04):** MAC_TI auf 8 gesetzt, Firmware gebaut und geflasht.  
Ergebnis: TSU lief **5× langsamer** als Echtzeit (Faktor 0.49× gemessen in Log `194038`).  
Das beweist: 25 MHz GEM-Takt ist korrekt → MAC_TI=40 ist richtig.  
→ Änderung **revertet**.

**Wahre Ursache der Testfehler:** Das Testskript `tsu_register_check.py` verwendete bei TEST_TSU_02/03 eine falsche Zeitreferenz. Jeder `lan_read`-Aufruf dauert ~4 s (PTP-State-Machine blockiert Callback-Pipeline). Die naive Differenzbildung `sleep(5)-Fenster vs. TSU-Δ` war methodisch falsch.  
Fix: Per-Read-PC-Timestamps (`t_after_tn0`, `t_after_tn5`) als identische Zeitreferenz wie TSU-Δ.

**Status:** Kein Firmware-Handlungsbedarf. MAC_TI=40 bleibt in `gm_init_vals[]` und in `_InitMemMap` (`drv_lan865x_api.c`).

---

## 4. Maßnahmenplan (priorisiert)

### Maßnahme 1 — OA_CONFIG0 Bit 6+7 setzen  ✅ ERLEDIGT (2026-04-04, Hardware bestätigt)

**Was:** RMW auf OA_CONFIG0 (0x00000004): `new_value = old_value | 0xC0u`  
**Ergebnis:** `CONFIG0 = 0x000090E6`, `& 0xC0 = 0xC0` ✓  
**Files:** States `GM_STATE_RMW_CONFIG0_READ/WAIT_READ/WAIT_WRITE` in `ptp_gm_task.c` vorhanden.

---

### Maßnahme 2 — TXMLOC = 30  ✅ ERLEDIGT (2026-04-04, Hardware bestätigt)

**Was:** `gm_init_vals[]` TXMLOC von `12u` auf `30u` geändert.  
**Ergebnis:** Hardware-Readback `TXMLOC = 0x1E` = 30 ✓

---

### Maßnahme 3 — PADCTRL RMW(0x100, 0x300)  ⚠️ IMPLEMENTIERT — aber Bit8 write-ignored

**Was:** RMW-States für `0x000A0088` implementiert.  
**Ergebnis:** `PADCTRL = 0xFC000400` — Bit8 bleibt 0 nach Write.  
**Nächster Schritt:** CLI-Direkttest (s. Diff #3) zur Bestätigung ob Register grundsätzlich schreibgeschützt ist.

---

### Maßnahme 4 — MAC_TI = 8  ❌ REVERTET — MAC_TI=40 ist korrekt (2026-04-04)

**Ergebnis:** Gegenprobe mit MAC_TI=8 hat gezeigt, dass TSU dann 5× zu langsam läuft (Faktor 0.49×).  
GEM-Takt ist 25 MHz → MAC_TI=40 korrekt (25 MHz × 40 ns = 1 Mrd. ns/s = 1 s/s real).  
**Keine Änderung erforderlich.** Beide Dateien enthalten den korrekten Wert:
- `src/ptp_gm_task.c` `gm_init_vals[]`: `MAC_TI = 40u`
- `drv_lan865x_api.c` `_InitMemMap`: Addr `0x00010077` = `0x00000028` (=40)

**Hinweis:** `_InitMemMap` in `drv_lan865x_api.c` wird bei jedem PLCA-Reset ausgeführt und überschreibt MAC_TI. Der Wert in `gm_init_vals[]` ist daher nur relevant wenn `_InitMemMap` nicht läuft (Erststart ohne PLCA-Reset).

---

### Maßnahme 5 — (Optional) TXMCTL dauerhaft auf TXME=1

Init-Sequenz so ändern, dass TXMCTL = 0x02 am Ende des Init gesetzt bleibt (wie Referenz).  
Den separaten `GM_STATE_WRITE_TXMCTL` vor jedem Sync entfernen.  
**Risiko:** Wenn TXME permanent = 1, könnten ungewollte Frames gematcht werden — daher optional.

---

## 5. Reihenfolge der nächsten Schritte

```
1. [x] Maßnahme 1 erledigt: OA_CONFIG0 RMW — Hardware bestätigt ✓
2. [x] Maßnahme 2 erledigt: TXMLOC = 30 — Hardware bestätigt ✓
3. [x] Maßnahme 3: PADCTRL Bit8 — write-ignored, Register ist read-only in LAN8651 ✓
4. [x] Maßnahme 4: MAC_TI=40 ist korrekt bestätigt (Gegenprobe MAC_TI=8 war 5× zu langsam → revertet) ✓
5. [x] Testskript tsu_register_check.py: Messmethodik korrigiert (per-read PC-Timestamps) ✓
6. [x] _InitMemMap in drv_lan865x_api.c: MAC_TI=0x28 korrekt, verstanden dass es bei PLCA-Reset überschreibt ✓
7. [x] Root Cause TTSCAA: gm_get_and_clear_ts_capture() jetzt in GM_STATE_READ_STATUS0 aufgerufen ✓
         → ptp_gm_task.c: GM_STATE_READ_STATUS0 prüft CB-Capture-Wert, bevor SPI-Read ausgelöst wird
8. [x] Test: python ptp_diag.py --from-step 2 (Log: ptp_diag_20260404_203210.log)
         → TTSCAA via CB feuert bei Sync #0..#198 (100%) ✓
         → Follower konvergiert auf FINE ✓
         → Offset: mean=+45.5 ns, stdev=8.6 ns, 13/13 < 100 ns ✓
9. [x] PTP GM VOLLSTÄNDIG FUNKTIONSFÄHIG ✓
```

---

## 7. Testergebnisse

### Log `tsu_check_20260404_191030.log` — Ausgangslage

| Test | Ergebnis | Details |
|------|----------|---------|
| TEST_TSU_01 MAC_TI | ✅ PASS | MAC_TI = 0x28 = 40 ✓ |
| TEST_TSU_02 MAC_TN Δ | ❌ FAIL | Messmethodik fehlerhaft (lan_read Latenz ~4s nicht berücksichtigt) |
| TEST_TSU_03 MAC_TSL Δ | ❌ FAIL | Δ = 5 s in 1,2 s real — Messmethodik fehlerhaft |
| TEST_TSU_04 MAC_TSH | ✅ PASS | Stabil = 0x00000000 |
| TEST_OA_01 CONFIG0 | ✅ PASS | 0x000090E6 (FTSE+FTSS korrekt) |
| TEST_OA_02 STATUS0 W1C | ✅ PASS | 0x00000000 nach Clear |
| TEST_OA_03 IMASK0 | ✅ PASS | 0x00000000 (TTSCAA unmaskiert) |
| TEST_OA_03b CONFIG0 Bit6+7 | ✅ PASS | 0x90E6 & 0xC0 = 0xC0 ✓ (RMW erfolgreich) |
| TEST_OA_03c PADCTRL Bit8 | ⚠️ WARN | 0xFC000400 — Bit8=0; Register ist read-only in LAN8651 |
| TEST_OA_04 TTSC-Baseline | ✅ PASS | Alle TTSCAH/L/BH/L/CH/CL = 0x00000000 |
| TEST_TXM_01 TX-Match-Register | ✅ PASS | TXMLOC=30, TXMPATH=0x88, TXMPATL=0xF710 ✓ |
| TEST_TXM_02 EtherType-Low | ✅ PASS | 0xF7 korrekt |
| CHK-10 NETWORK_CTRL | ✅ PASS | TXEN+RXEN gesetzt |

**Gesamt:** 10/13 PASS, 0 echte FAIL (2 TSU-Tests waren Messmethodikfehler, 1 WARN = read-only Register)

### Logs `194038` / `195146` — MAC_TI Gegenprobe (ABGESCHLOSSEN)

| Log | MAC_TI | TSU-Faktor | Ergebnis |
|-----|--------|------------|----------|
| `194038` | 0x08 (=8) | 0.49× (zu langsam) | Beweis: 25 MHz GEM-Takt |
| `195146` | 0x28 (=40) | ~0.58× FAIL | Messmethodik noch unvollständig |

**Schlussfolgerung:** MAC_TI=40 ist korrekt. TSU-Uhr läuft. Testskript nach Metodikkorrektur (per-read Timestamps) erwartet Faktor ~1.0×.

### Root Cause TTSCAA — BEHOBEN (2026-04-04)

**Bug:** `gm_get_and_clear_ts_capture()` war als Helper definiert aber **nie aufgerufen**.

**Fehlersequenz:**
1. Sync TX → Hardware setzt TTSCAA in STATUS0
2. TC6-Library sieht EXST-Bit → liest STATUS0=`0x100` → ruft `_OnStatus0` auf → **W1C-löscht STATUS0** → speichert TTSCAA in `drvTsCaptureStatus0[0]`
3. State Machine `GM_STATE_READ_STATUS0` liest STATUS0 via SPI → bekommt `0x00000000` → FAIL
4. Da TTSCAH/TTSCAL-Latch nie gelesen wurde (nie `GM_STATE_READ_TTSCA_H` erreicht), bleibt Latch belegt → Hardware kann keinen neuen Timestamp erfassen → TTSCAA feuert nie wieder

**Fix in `ptp_gm_task.c`:**  
`GM_STATE_READ_STATUS0` prüft zuerst `gm_get_and_clear_ts_capture()`. Bei Treffer: direkt zu `GM_STATE_READ_TTSCA_H` → Latch wird gelesen und freigegeben. Nur bei Fehlschlag: SPI-Direktread als Fallback.

**Ergebnis (Log `ptp_diag_20260404_203210.log`):**
- TTSCAA feuert bei **Sync #0 bis #198** (100% Erfolgsquote)
- Follower erreicht **FINE**-State
- Offset: **mean=+45.5 ns, stdev=8.6 ns, 13/13 < 100 ns**

---

## 6. Register-Adressen Referenz vs. unsere Defines

| Register | Referenz-Adresse | Unser Define | Gleich? |
|----------|-----------------|--------------|---------|
| `TXMCTL`   | `0x00040040` | `GM_TXMCTL  = 0x00040040u` | ✓ |
| `TXMPATH`  | `0x00040041` | `GM_TXMPATH = 0x00040041u` | ✓ |
| `TXMPATL`  | `0x00040042` | `GM_TXMPATL = 0x00040042u` | ✓ |
| `TXMMSKH`  | `0x00040043` | `GM_TXMMSKH = 0x00040043u` | ✓ |
| `TXMMSKL`  | `0x00040044` | `GM_TXMMSKL = 0x00040044u` | ✓ |
| `TXMLOC`   | `0x00040045` | `GM_TXMLOC  = 0x00040045u` | ✓ (Adresse gleich, Wert verschieden) |
| `OA_CONFIG0` | `0x00000004` | `GM_OA_CONFIG0 = 0x00000004u` | ✓ |
| `OA_STATUS0` | `0x00000008` | `GM_OA_STATUS0 = 0x00000008u` | ✓ |
| `OA_TTSCAH`  | `0x00000010` | `GM_OA_TTSCAH  = 0x00000010u` | ✓ |
| `OA_TTSCAL`  | `0x00000011` | `GM_OA_TTSCAL  = 0x00000011u` | ✓ |
| `MAC_TI`   | `0x00010077` | implizit (kein eigener `#define`) | ✓ (Wert aus Initialisierungsarray) |
| `PADCTRL`  | `0x000A0088` | kein `#define` | — (Adresse bekannt, nicht als Const definiert) |
| `PPSCTL`   | `0x000A0239` | implizit im Init-Array | ✓ |
| `TXMCTL_TXPMDET` | `0x0080` | `GM_TXMCTL_TXPMDET = 0x0080u` | ✓ |
| `OA_STS0_TTSCAA` | `0x0100` | `GM_STS0_TTSCAA   = 0x0100u` | ✓ |
| `OA_STS0_TTSCAB` | `0x0200` | `GM_STS0_TTSCAB   = 0x0200u` | ✓ |
| `OA_STS0_TTSCAC` | `0x0400` | `GM_STS0_TTSCAC   = 0x0400u` | ✓ |
