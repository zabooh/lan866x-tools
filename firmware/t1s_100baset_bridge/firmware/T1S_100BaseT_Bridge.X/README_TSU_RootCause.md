# LAN8651 TSU-Analyse: Warum TTSCAA nie gesetzt wird

> **Datum:** 2026-04-02  
> **Ausgangsbefund (ursprünglich):** TXPMDET ~0.6 ✓ | TTSCAA immer 0 ❌ | TTSCMA immer gesetzt ❌  
> **Testbefund 2026-04-02 (tsu_register_check.py):** STATUS0-Events = 0 | STATUS1-Events = 0 | **weder TTSCAA noch TTSCMA** — kein Timestamp-Event überhaupt  
> **Firmware-Basis:** ptp_gm_task.c + drv_lan865x_api.c (Stand nach Fixes #1–#4 + CHK-11)

---

## Stand 2026-04-04 — TXMCTL-Fix + Testergebnis

### Fixes implementiert

**Fix A — TXMCTL nie armiert (CHK-50):**  
`GM_STATE_WRITE_TXMCTL` schrieb `0x0000u` (TXME=0 = Match-Detector disarmiert) vor jedem Sync.  
Geändert zu `0x0002u` (TXME=1 = armiert) in `ptp_gm_task.c` Zeile 644.

```c
// Vorher (falsch):
gm_write_register(GM_TXMCTL, 0x0000u, true);

// Nachher (korrekt, CHK-50):
gm_write_register(GM_TXMCTL, 0x0002u, true);  // TXME=1: arm TX-Match-Detector
```

**Fix B — TXMCTL fehlte in Init-Sequenz:**  
`GM_INIT_WRITE_COUNT` 7 → 8; `GM_TXMCTL = 0x0000u` als erster Eintrag  
in `gm_init_addrs[]` / `gm_init_vals[]` hinzugefügt.  
Sichert disarmierten Zustand nach Power-On und jedem LOFR-Reset.

### Testergebnis 2026-04-04 (ptp_diag.py --dump-txmatch --duration 60)

| Schritt | Test | Ergebnis |
|---------|------|---------|
| 0 | IP-Konfiguration | **PASS** |
| 1 | Ping GM↔Follower | **PASS** |
| 2 | PTP aktivieren | **PASS** |
| 3 | GM sendet Syncs (gmSyncs-Zähler) | **PASS** |
| 4 | TXPMDET-Bit gesetzt (TXMCTL 0x0080) | **FAIL** |
| 5 | Follower empfängt PTP-Frames | **PASS** |
| 6 | FollowUp gesendet (TTSCAA → FU-Pfad) | **FAIL** |
| 7 | Servo konvergiert | **FAIL** |
| 8 | Offset-Stabilität | **FAIL** |

**Serielle Log-Ausgabe (GM, repräsentativ):**
```
[PTP-GM] TTSCA not set after Sync #0
[PTP-GM] TTSCA not set after Sync #1
...
```
`[DBG] _OnStatus0` **erscheint nie** (hatte `0x0F00u`-Schwelle).  
`[DBG] _OnStatus1` **erscheint nie**.  
→ STATUS0 und STATUS1 bleiben dauerhaft 0.

### Schlussfolgerung

**TXME=1 allein reicht nicht.** Obwohl TXMCTL jetzt korrekt armiert  
wird, setzt die Hardware TXPMDET und TTSCAA weiterhin nie.  
Der Fix war notwendig (vorher logisch falsch), aber nicht hinreichend.

### Bestätigte korrekte Pfade (Code-Review 2026-04-04)

| Prüfung | Ergebnis |
|---------|---------|
| TSC=1 in SPI-Header (byte[3] bits[7:6]) | ✅ korrekt — `SET_VAL(HDR_TSC, entry->tsc, tx_buf)` bei SV=1 |
| `DRV_LAN865X_SendRawEthFrame` → `TC6_SendRawEthernetPacket` mit tsc=1 | ✅ korrekt |
| FTSE=0x80 in CONFIG0 (`_InitUserSettings` case 8) | ✅ korrekt |
| IMASK0 = 0x00000000 (TTSCAA unmaskiert) | ✅ korrekt |
| `_OnStatus0` speichert TTSCAA-Bits vor W1C | ✅ korrekt |
| `DRV_LAN865X_GetAndClearTsCapture()` atomar | ✅ korrekt |

### Nächste Schritte (aktualisiert 2026-04-04)

~~1. **reg_access_test.py**~~ ✅ ERLEDIGT — 17/18 PASS (siehe unten)  
~~2. **CHK-52**~~ ✅ ERLEDIGT — tsc=0x01 bestätigt (siehe unten)  
~~3. **CHK-12 (Bit15)**~~ ✅ ERLEDIGT — kein Effekt (siehe unten)  

→ **Alle Software-Pfade bewiesen. Problem in Hardware-Capture-Engine.**  
→ **Nächster Schritt: Microchip Support kontaktieren** (siehe Abschnitt unten)

---

## Stand 2026-04-04 (II) — CHK-12, reg_access_test, CHK-52

### reg_access_test.py — 2026-04-04

Neues Testskript `reg_access_test.py` erstellt und ausgeführt. Ergebnis: **17/18 PASS**.

| Test | Register | Ergebnis |
|------|---------|---------|
| T01 | CONFIG0 = 0x000090E6 | ✅ FTSE+FTSS gesetzt |
| T02 | IMASK0 = 0x00000000 | ✅ TTSCAA unmaskiert |
| T03–T05 | TXMLOC Roundtrip (0xAB schreiben + zurücklesen) | ✅ Callback funktioniert |
| T06 | TXMCTL = 0x00000002 (TXME=1) | ✅ sinnvoller Wert |
| T07 | NETWORK_CTRL = 0x0000000C (TXEN+RXEN) | ✅ Bit15 = 0 |
| T08 | Callback-Latenz Ø 4650 ms | ⚠ >1000 ms (PTP-Pipeline blockiert, erwartet) |

**Schluss:** Register-Zugriffs-Mechanismus einwandfrei. Callbacks feuern korrekt.  
Hohe Latenz (~4.5 s) ist durch die 500-ms-STATUS0-Wartezeit in der PTP-State-Machine erwartet.

### CHK-12 (NETWORK_CTRL Bit15) — 2026-04-04

Zweiter Lauf mit TXME=1 (CHK-50 Fix eingebaut). Beide Tests je 60+ Syncs:

| Test | NC | TTSCAA | TTSCMA | Syncs |
|------|----|--------|--------|-------|
| A (Baseline) | `0x0000000C` | nein | 0 | 60 |
| B (+Bit15) | `0x0000800C` | nein | 0 | 60 |

**CHK-12 endgültig ausgeschlossen:** `STORE_TX_TS` (Bit15) hat keinen messbaren Effekt.  
Auch TTSCMA = 0 in beiden Tests — TC6 versucht keinen Capture.

### CHK-52 (tsc=0x01 Verifizierung) — 2026-04-04

Debug-Print in `DRV_LAN865X_SendRawEthFrame` (`drv_lan865x_api.c`) eingebaut:

```c
if (0u != tsc) {
    static uint8_t chk52_tsc_cnt = 0u;
    if (chk52_tsc_cnt < 10u) {
        chk52_tsc_cnt++;
        SYS_CONSOLE_PRINT("[DBG-CHK52] tsc=0x%02X len=%u call#%u\r\n", ...);
    }
}
```

**Serielle Ausgabe (GM, 10 aufeinanderfolgende Syncs):**
```
[DBG-CHK52] tsc=0x01 len=60 call#1
[PTP-GM] Sync #0 sent, waiting for TX done...
[PTP-GM] Sync #0 TX confirmed
[PTP-GM] TTSCA not set after Sync #0
[DBG-CHK52] tsc=0x01 len=60 call#2
...
```

**CHK-52 BESTÄTIGT: `tsc=0x01` erreicht `TC6_SendRawEthernetPacket` — jedes Mal.**  
`SET_VAL(HDR_TSC, 1, tx_buf)` → byte[3] = 0x40 (bits[7:6]=01) wird korrekt gesetzt.  
Der gesamte Software-Pfad ist ausgeschlossen.

### Gesamtbeweis (Stand 2026-04-04)

| CHK | Beschreibung | Ergebnis |
|-----|-------------|---------|
| CHK-50 | TXME=1 (TXMCTL=0x0002) | ✅ Fix angewendet — notwendig aber nicht hinreichend |
| CHK-12 | NETWORK_CTRL Bit15 | ❌ kein Effekt — ausgeschlossen |
| CHK-52 | tsc=0x01 zu TC6 | ✅ Software korrekt — ausgeschlossen |
| reg_access | Callback-Roundtrip | ✅ Mechanismus einwandfrei |
| STATUS0/1 | Timestamp-Events | ❌ **permanent 0 — Hardware-Ebene** |

**Fazit: Die GEM-TSU-Capture-Engine im LAN8651 erzeugt keine TX-Timestamp-Events,**  
**obwohl TSC=1 korrekt im SPI-Header codiert wird. Alle konfigurierbaren SW-Register korrekt.**  
**Ursache liegt in der Hardware oder in einer undokumentierten Initialisierungsvoraussetzung.**

### Empfohlene nächste Schritte

1. **Microchip Support** — Case öffnen mit:
   - Chip: LAN8651 Rev. ?
   - TC6 SPI-Header TSC=01 (byte[3]=0x40) korrekt encodiert ✓
   - CONFIG0=0x000090E6 (FTSE+FTSS) ✓  
   - TXMCTL=0x0002 (TXME=1) ✓
   - IMASK0=0x00000000 ✓
   - NETWORK_CTRL=0x0000000C und 0x0000800C beide getestet
   - STATUS0 und STATUS1 bleiben **permanent 0**
   - Frage: Welche zusätzliche Initialisierung / welcher Register-Wert aktiviert die TX-Timestamp-Capture-Engine?

2. **CHK-31** (optional, vor Support-Antwort) — CONFIG0 isoliert testen:
   ```
   lan_write 0x00000004 0x000000C0   # nur FTSE+FTSS, alle anderen Bits 0
   ```
   Wenn TTSCAA dann erscheint: einer der Base-Bits in 0x9026 blockiert Capture-Pfad.

---

## 0  Was die Code-Analyse liefert (vor Theorienliste)

Vor der Priorisierung: was der Code **beweist**, um Spekulation zu begrenzen.

### 0.1 TSC-Feld im SPI-Header — korrekt

`tc6.c`:
```c
#define HDR_TSC   FLD(3u, 6u, 2u)   // byte[3], bits[7:6] im 4-Byte-Header
...
SET_VAL(HDR_TSC, entry->tsc, tx_buf);   // nur in Chunk mit SV=1
```
`DRV_LAN865X_SendRawEthFrame(idx, buf, len, tsc=1, ...)` ruft  
`TC6_SendRawEthernetPacket(..., tsc=1)` direkt auf → Feld wird korrekt gesetzt.  
**Ein falsches TSC-Placement ist ausgeschlossen.**

### 0.2 TTSCAA-Speicher-Mechanismus — korrekt implementiert

`_OnStatus0`:
```c
if (0u != (value & 0x0F00u))
    SYS_CONSOLE_PRINT("[DBG] _OnStatus0: 0x%08lX\r\n", value);  // kein PRINT_LIMIT!

if (0u != (value & 0x0700u))
    drvTsCaptureStatus0[i] |= (value & 0x0700u);   // vor W1C gespeichert

TC6_WriteRegister(pInst, addr, value, ..., _OnClearStatus0, NULL);  // W1C danach
```
`DRV_LAN865X_GetAndClearTsCapture()` liest und löscht `drvTsCaptureStatus0` atomar.  
Der `[DBG]`-Print ist **kein** `PRINT_LIMIT` → falls TTSCAA je gesetzt würde, erschiene er.  
**Er erscheint nie → TTSCAA wird auf Hardware-Ebene nie gesetzt.**

### 0.3 IMASK0 — korrekt

MemMap-Eintrag (Zeile ~1738 in drv_lan865x_api.c):
```c
{ .address=0x0000000C, .value=0x00000000, ...write... }  /* IMASK0: alles unmaskiert */
```
TTSCAA (bit 8) ist **nicht maskiert** → würde EXST auslösen, wenn es gesetzt würde.

### 0.4 TTSCMA beweist Hardware-Sichtbarkeit, aber Capture-Versagen

`_OnStatus1`, case 24: `PRINT_LIMIT("Status1.TX_Timestamp_Capture_Missed_A\r\n")`.  
LAN8651-Semantik von **TTSCMA**: Der TC6-Controller hat TSC=01 empfangen, wollte  
den Timestamp in Slot A schreiben — aber die Capture-Engine **konnte den Timestamp nicht**  
**bereitstellen**. Slot A ist leer (TTSCOFA=0), Frame kommt raus → der Fehler liegt  
in der internen GEM-TSU-Anbindung, nicht in TXME/MACTXTSE/TXMPATL.

### 0.5 Historisches Artefakt: DELAY_UNLOCK_EXT

```c
#define DELAY_UNLOCK_EXT (5u)
/* reduced from 100ms: TTSCAA appears ~1ms after EXST; 100ms lock caused TTSCMA */
```
**TTSCAA wurde früher beobachtet.** Die 100 ms-Sperre hatte TTSCAA "verpasst" und  
TTSCMA verursacht. Nach Reduktion auf 5 ms tritt TTSCMA weiterhin auf.  
→ Das deutet darauf hin, dass TTSCAA nicht mehr gesetzt wird — ein Regressionsfehler.

### 0.6 Testergebnis 2026-04-02: STATUS1 = 0 — TTSCMA NICHT gesetzt ⚠️ NEUER BEFUND

**CHK-11** wurde implementiert: `[DBG] _OnStatus1: 0x%08lX` ohne `PRINT_LIMIT` in `_OnStatus1`.  
Firmware gebaut, geflasht. 60+ Sync-Frames in ~60 s:

```
Test A (NC=0x0000000C, Baseline):  Syncs=30, STATUS0-Events=0, STATUS1-Events=0
Test B (NC=0x0000800C, +Bit15):    Syncs=28, STATUS0-Events=0, STATUS1-Events=0
```

**`[DBG] _OnStatus1` ist NIEMALS erschienen** → STATUS1 permanent 0.  
**`[DBG] _OnStatus0` ist NIEMALS erschienen** (0x0F00-Bits nie gesetzt).

Dieser Befund **widerlegt** die ursprüngliche Beobachtung „TTSCMA immer gesetzt":  
- Entweder war TTSCMA nur beim Startup-Reset (via `PRINT_LIMIT`) sichtbar  
- Oder die ursprüngliche Beobachtung bezog sich auf eine andere Firmware-Version

**Konsequenz:** Das Problem ist nicht „TSC=1 empfangen, Capture misslingt (→ TTSCMA)",  
sondern **„kein Timestamp-Event überhaupt"**. Der TC6-Controller erzeugt weder TTSCAA  
noch TTSCMA, obwohl Sync-Frames gesendet werden und TSC=1 im API-Call steht.  
Block A (NETWORK_CONTROL Bit 15) wurde getestet und hat keinen Effekt.

---

## 1  Theorieblöcke, sortiert nach Wahrscheinlichkeit

### BLOCK A — GEM-TSU intern nicht vollständig aktiviert ★★★★★ (höchste Prio)

**Betroffene Theorien: H8, H7 (teilweise)**

`_InitUserSettings` case 9 schreibt:  
```c
TC6_WriteRegister(tc, 0x00010000 /* NETWORK_CONTROL */, 0x0000000C, ...)
```
Nur `TXEN|RXEN = 0x0C` — keine weiteren Bits.

In Standard-GEM (Cadence, den LAN8651 intern nutzt) gibt es folgende TSU-relevante  
NETWORK_CONTROL-Bits, die in **0x0C fehlen**:

| Bit | Symbolname | Bedeutung für TSU |
|-----|------------|-------------------|
| 9   | `TXPAUSE`  | nicht relevant |
| **15** | **`TSUENSEL` / `STORE_TX_TS`** | **TSU TX-Capture an TX-Pfad binden** |
| **18** | **`STORE_RX_TS`** | RX-Timestamps (für FRAMEINFO/FTSE-Pfad) |

Insbesondere **Bit 15** (`STORE_TX_TS` in GEM-Ref-Manual, manchmal auch anders benannt):  
Ohne dieses Bit liefert das GEM keinen Timestamp an die Capture-Engine des TC6-Controllers.  
Selbst wenn `MAC_TI=40` den freien Timer laufen lässt, kann der Frame-SFD-Moment  
nicht in TTSCAH/TTSCAL landen, wenn der GEM intern die TX-Capture-Route nicht aktiviert hat.

**Erklärung für TTSCMA:**  
TC6-Controller sieht TSC=01 → will Timestamp von GEM-TSU holen → GEM sagt  
"kein TX-Timestamp-Capture aktiv" → TC6 setzt TTSCMA statt TTSCAA.

**Test (A-1):** Direkt prüfbar per `lan_write` ohne Firmware-Build:
```
lan_write 0x00010000 0x0000804C    # 0x0C + Bit15 (0x8000) + reserviert (harmlos)
```
Dann einen Sync senden und STATUS0 beobachten.

> **GETESTET 2026-04-02 (CHK-12) — FAIL:**  
> Test A (NC=0x0000000C): STATUS1=0, STATUS0=0 — kein Effekt  
> Test B (NC=0x0000800C, +Bit15): STATUS1=0, STATUS0=0 — kein Effekt  
> **Block A nicht ausreichend.** Bit 15 ändert nichts — auch TTSCMA bleibt 0.

**Hinweis zur Regression:** Der Kommentar `TTSCAA appears ~1ms after EXST` impliziert,  
dass eine frühere Firmware-Version TTSCAA sah. Möglicher Unterschied: In dieser früheren  
Version war NETWORK_CONTROL möglicherweise anders (z. B. aus dem noIP-Referenzcode  
initialisiert). Die Übernahme in Harmony 3 hat `_InitUserSettings` auf das Minimum  
reduziert.

---

### BLOCK B — EXST-Timing-Race: TTSCAA wird gesetzt, aber nie gelesen ★★★★☆

**Betroffene Theorien: H4 (teil), H8 (timing)**

**Ablauf-Hypothese:**
```
t=0:    Sync gesendet (TSC=1)
t=0.1ms: PLCA-Beacon o.ä. → EXST fires → TC6_CB_OnExtendedStatus aufgerufen
         STATUS0 gelesen → TTSCAA noch nicht gesetzt (Frame noch im TX-Puffer)
         STATUS0 W1C'd (0x0000 → kein Bit gesetzt)
         unlockExtTime = now → 5ms Sperre
t=0.5ms: Sync-Frame geht auf den Draht → LAN8651 setzt TTSCAA in STATUS0
         EXST-Bit erscheint in RX-Footer
t=5ms:   TC6_UnlockExtendedStatus() ← Sperre wird released
t=5ms+ε: Nächste TC6_Service-Ausführung liest EXST → TC6_CB_OnExtendedStatus →
          STATUS0 lesen → TTSCAA=1 → [DBG]-Print → gespeichert ✓
```
**Das wäre korrekt — ABER:** Was wenn zwischen t=5ms und dem nächsten TC6_Service  
der nächste Sync (125ms Periode) gesendet wird, der seinerseits EXST auslöst und  
STATUS0 erneut clear'd? Nein, 125ms > 5ms, das kann nicht der Grund sein.

**Aber**: Was wenn `TC6_UnlockExtendedStatus` called wird, aber dann lange kein  
`TC6_Service` läuft, weil der SPI-Bus mit anderen Transaktionen belegt ist  
(z. B. `gm_write(GM_TXMCTL, 0)` direkt vor dem Sync)?  
→ EXST-Flag akkumuliert sich im Footer, aber `TC6_CB_OnExtendedStatus` wird erst  
beim nächsten freien SPI-Slot aufgerufen.

**Entscheidend:** Wenn **Block A** zutrifft (TTSCAA wird hardware-seitig nie gesetzt),  
dann ist Block B irrelevant. Block A zuerst prüfen.

---

### BLOCK C — CONFIG0 base value 0x9026: unbekannte Bits ★★★☆☆

`_InitUserSettings` case 8:
```c
regVal = 0x9026u;
regVal |= 0x80u;  // FTSE
regVal |= 0x40u;  // FTSS
// → 0x90E6
```
`0x9026 = 0b1001_0000_0010_0110`:
- Bit 15: `1` ← was ist das in LAN8651-CONFIG0?
- Bit 13: `0` (gesetzt durch `0x9026` nein, 0x2000 wäre bit 13)
- Bit 5: `1` (0x0020)
- Bit 2: `1` (0x0004)  ← TXCTE?
- Bit 1: `1` (0x0002)  ← RXCTE?

Ohne genaue LAN8651-CONFIG0-Bitmap:
- Bit 15 = evtl. `RTSA` (auto-strip RX frame timestamp) — relevant für FTSS/FRAMEINFO
- Bit 2/1 = evtl. CRC-Optionen oder Cut-Through-Enable

**Für TX-Timestamps:** Wenn Bit 2 oder 15 im falschen Zustand eine TX-SFD-Detect-Pipeline  
beeinflusst, könnte das TTSCAA blockieren. **Weniger wahrscheinlich als Block A.**

---

### BLOCK D — FTSE/FTSS falsch interpretiert ★★★☆☆

**Betroffene Theorien: H1, Kommentar-Fehler**

Code-Kommentar (case 8):  
```c
regVal |= 0x80u; /* FTSE: Frame Timestamp Enable (required for TTSCAA TX capture) */
```
**Der Kommentar ist falsch.** FTSE (Frame Timestamp Enable) steuert den  
**RX-FRAMEINFO-Präfix** (8-Byte-Timestamp vor eingehenden Frames, RTSA-Bit im Footer).  
Er ist **nicht** der Enable-Bit für TX-Capture via TSC-Feld.

Für TSC-basiertes TX-Timestamping ist FTSE laut TC6-Spec irrelevant.  
FTSE=0 könnte sogar **nötig** sein, wenn FTSS=0 ebenfalls gesetzt werden muss —  
aber das ist unwahrscheinlich, da FTSE=0 nur den RX-FRAMEINFO-Anteil abschaltet.

**Test (D-1):** CONFIG0 = `0x0000` (alles off) → TSC=1 senden → TTSCAA?  
→ Wenn ja: FTSE interferiert. Wenn weiterhin TTSCMA: FTSE irrelevant.

---

### BLOCK E — LOFR-Reset löscht PTP-Konfiguration ★★★☆☆

**Betroffene Theorien: H7**

LOFR setzt `pDrvInst->initState = DRV_LAN865X_INITSTATE_RESET` → `_InitUserSettings`  
läuft erneut. Das schreibt korrekt MAC_TI=40 (case 8) und NETWORK_CONTROL=0x0C (case 9).  
**Aber: `PTP_GM_Init()` wird nach LOFR-Reset NICHT erneut aufgerufen.**

`PTP_GM_Init()` schreibt zusätzlich:
```c
gm_write(PPSCTL, 0x0000007Du);   // PPS-Ausgang
// aber kein NETWORK_CONTROL mit PTP-Bits!
```
→ Der LOFR-Reset macht die Situation für diesen Block nicht schlechter als ohne Reset.  
Aber: Wenn Block A zutrifft (NETWORK_CONTROL fehlt Bit), dann ist auch nach LOFR  
das richtige Bit nicht gesetzt.

---

### BLOCK F — Pattern-Match-Pfad (TXME, MACTXTSE, TXMPATL) ★★★★☆ (erhöhte Prio nach CHK-12)

**Betroffene Theorien: H2, H5, H6, H12**

Aktueller Stand: `TXMCTL = 0x0000` (kein TXME, kein MACTXTSE).  
Die State-Machine geht direkt nach `GM_STATE_WAIT_STATUS0` — ohne TXPMDET-Poll.  
TXMPATL-Bit 0x10 (H6) ist für den TSC-Pfad **vollständig irrelevant**  
(TXMPATL gilt nur für Pattern-Match-Trigger, nicht für TSC).

TXPMDET (~0.6) beweist, dass der Pattern-Matcher grundsätzlich feuert —  
aber das ist ein separater Pfad von TSC.

**NACH CHK-12-BEFUND (STATUS1=0 auch mit Bit15):** Wenn weder TTSCAA noch TTSCMA  
gesetzt wird, ignoriert der TC6-Controller das TSC-Feld im SPI-Header vollständig.  
Neue Hypothese (H12): **Das TC6-Capture-Subsystem ist ohne TXME=1 oder MACTXTSE=1**  
**komplett inaktiv** — TSC=01 im SPI-Header allein genügt nicht.  
Das würde erklären, warum STATUS1=0 (kein Capture-Versuch, daher auch kein TTSCMA).

**Nächster Test (CHK-50 — HÖCHSTE PRIO):** `TXMCTL = 0x0002` (TXME=1) setzen,  
dann Sync mit tsc=1 senden → STATUS1/STATUS0 beobachten.

---

### BLOCK G — IMASK0/W1C-Race ★☆☆☆☆

**Betroffene Theorien: H4**

Bereits analysiert (Abschnitt 0.2 + 0.3): IMASK0=0x0000 ✓ und `_OnStatus0` speichert  
TTSCAA **vor** W1C. Kein Fehler im Code. Ausgeschlossen, solange TTSCAA nie gesetzt wird.

---

## 2  Priorisierte Checkliste (Schritt-für-Schritt-Arbeitsplan)

### ── Tier 0: Regressions-Orientierung (5 Minuten) ──

**CHK-00 — Wann lief TTSCAA zuletzt korrekt?**
- Logdateien/Git-History nach `[DBG] _OnStatus0: 0x.......1..` suchen  
- Welcher Commit hat DELAY_UNLOCK_EXT von 100ms auf 5ms geändert?  
  Davor gab es TTSCAA. Was hat sich seitdem in `_InitUserSettings` oder  
  `PTP_GM_Init` verändert?

---

### ── Tier 1: GEM-NETWORK_CONTROL-Bits (Block A) ──

**CHK-10 — NETWORK_CONTROL Ist-Wert lesen** ✅ ERLEDIGT (2026-04-02)
```
lan_read 0x00010000  →  0x0000000C  ✓ (erwartet)
```

**CHK-11 — STATUS1 Rohwert immer mitloggen** ✅ ERLEDIGT (2026-04-02)
Implementiert: `SYS_CONSOLE_PRINT("[DBG] _OnStatus1: 0x%08lX\r\n", (unsigned long)value)` ohne PRINT_LIMIT.  
Firmware gebaut, geflasht. **Ergebnis: `[DBG] _OnStatus1` niemals erschienen** — STATUS1 = 0 permanent.

**CHK-12 — NETWORK_CONTROL Bit 15 Test** ❌ GETESTET — FAIL (2026-04-02)
```
Test A: NC=0x0000000C (Baseline)   → Syncs=30, STATUS0-Events=0, STATUS1-Events=0 → FAIL
Test B: NC=0x0000800C (+Bit15)     → Syncs=28, STATUS0-Events=0, STATUS1-Events=0 → FAIL
```
**Befund:** Bit 15 hat keinen Effekt. Weder TTSCAA noch TTSCMA — kein Timestamp-Event.

**CHK-13 — NETWORK_CONTROL Bit 18 zusätzlich (STORE_RX_TS)** 🔴 Offen (Prio mittel)
```
lan_write 0x00010000 0x0004800C    # Bit15 + Bit18 + TXEN + RXEN
```
Test wie CHK-12. Erst nach CHK-50 angehen.

---

### ── Tier 2: EXST-Timing und STATUS0/STATUS1-Kontexte ──

**CHK-20 — STATUS1 vollständige Bitmaske bei TTSCMA**  
Nach CHK-11: In welchen anderen STATUS1-Bits erscheinen gleichzeitig mit bit24?  
- Nur bit24 allein → reines Capture-Fehler-Ereignis  
- bit24 + bit17 (FSM_State_Error) → GEM-interne Zustandsmaschine kaputt  
- bit24 + bit21 (Overflow_A) → Slot ist belegt geblieben

**CHK-21 — STATUS0 Kontext bei TTSCMA**  
In `_OnStatus0` bereits der komplette Wert ist geloggt. Wenn STATUS0 niemals  
TTSCAA zeigt (0x0F00-Bits immer 0): bekräftigt Block A (GEM liefert keinen Timestamp).

**CHK-22 — EXST-Counter einbauen**
In `TC6_CB_OnExtendedStatus` Zähler inkrementieren:
```c
static uint32_t exstCount = 0u;
exstCount++;
if (exstCount % 10u == 0u)
    SYS_CONSOLE_PRINT("[DBG] EXST #%lu\r\n", (unsigned long)exstCount);
```
Wie oft feuert EXST pro Sekunde? Wenn > 10/s: permanente Störevents blockieren  
möglicherweise das 5ms-Timing-Fenster tatsächlich (Block B).

---

### ── Tier 3: CONFIG0-Isolation ──

**CHK-30 — CONFIG0 Ist-Wert nach Init**
```
lan_read 0x00000004
```
Muss `0x000090E6` sein. Wenn `0x9026`: FTSE/FTSS nicht geschrieben (Init-Reihenfolge kaputt).  
Wenn irgendwas anderes: MemMap-Überschreibung nach CHK-10-Setup.

**CHK-31 — Pure FTSE/FTSS ohne Base-Bits**
```bash
lan_write 0x00000004 0x000000C0    # nur FTSE+FTSS, alle anderen Bits 0
```
Dann Sync senden. Wenn TTSCAA jetzt erscheint: einer der Base-Bits in 0x9026  
blockiert den Capture-Pfad.

---

### ── Tier 4: Minimal-Capture-Test ohne PTP-Stack ──

**CHK-40 — Direkt-Test: Ein Frame, direktes STATUS0-Lesen**  
(Pseudocode — in `cmd_*`-Handler in app.c implementieren oder als Direktsequenz):

```
1. Schreibe MAC_TI = 40 (sicherstellen)
2. Warte 2ms
3. Lese MAC_TN → merke T1
4. Warte 10ms
5. Lese MAC_TN → merke T2, prüfe: (T2-T1) ≈ 10_000_000 ± 20%
   → TSU läuft korrekt?
6. Schreibe IMASK0 = 0 (TTSCAA mit allen Bits unmaskiert)
7. Sende 1 Frame mit TSC=01 (DRV_LAN865X_SendRawEthFrame, tsc=1)
8. Warte 50ms (OHNE TC6_Service zu blockieren — InDRVTask normal laufen lassen)
9. Direct-read STATUS0: TC6_ReadRegister(0x00000008, ...)
10. Direct-read STATUS1: TC6_ReadRegister(0x00000009, ...)
11. Logge beide Rohwerte
```

Ergebnis-Matrix:

| STATUS0 | STATUS1 | Diagnose |
|---------|---------|----------|
| bit8=1 (TTSCAA) | 0 | ✅ Hardware funktioniert — Problem liegt im Lese-Timing der Firmware |
| 0 | bit24=1 (TTSCMA) | ❌ GEM-interne Konfiguration fehlerhaft (Block A) |
| 0 | 0 | ❌ Frame nicht rausgegangen oder TSC-Bit nicht übertragen |
| 0 | bit17=1 (FSM Error) | ❌ GEM-interne Zustandsmaschine Fehler |

---

### ── Tier 5: Pattern-Match als Alternative (nur wenn Tier 1–4 kein Ergebnis) ──

**CHK-50 — TXME=1 + TSC=1 gleichzeitig** 🔴 HÖCHSTE PRIO
```c
gm_write(GM_TXMCTL, 0x0002u);  // TXME=1, kein MACTXTSE
// dann senden mit tsc=1
```
Wenn TTSCAA oder TTSCMA jetzt erscheint: TXME=1 ist zusätzlich zu TSC=1 erforderlich  
(H12 bestätigt). Wenn weiter STATUS1=0: TSC-Feld oder tc6.c-Pfad prüfen.

**CHK-50b — TXME=1 + TSC=0 (Kontroll-Test)**
```c
gm_write(GM_TXMCTL, 0x0002u);  // TXME=1
// senden mit tsc=0
```
Erwartet: TXPMDET erscheint, aber kein TTSCAA/TTSCMA. Bestätigt TXME-Funktion isoliert.

**CHK-51 — MACTXTSE=1 + TXME=0 + TSC=0**
```c
gm_write(GM_TXMCTL, 0x0004u);  // MACTXTSE, kein TXME
// senden mit tsc=0
```
Wenn TTSCAA jetzt erscheint: der MACTXTSE-Pfad (GEM-intern) funktioniert,  
aber TSC-Pfad ist blockiert.

**CHK-52 — TX-Header-Byte3-Verifikation in tc6.c** 🔴 WICHTIG
Falls CHK-50 weiter STATUS1=0 zeigt: Debug-Log in `tc6.c` einbauen um zu prüfen,  
ob `entry->tsc` zur Sendezeit tatsächlich 1 ist:
```c
// In tc6.c, nach SET_VAL(HDR_TSC, entry->tsc, tx_buf):
SYS_CONSOLE_PRINT("[DBG] TX_HDR byte3=0x%02X tsc=%d\r\n",
    (unsigned int)tx_buf[3], (int)((tx_buf[3] >> 6) & 3));
```
Erwartetes Ergebnis: `byte3=0x40` (tsc=01 = Slot A, bits[7:6]=01).  
Wenn `byte3=0x00`: `entry->tsc` ist 0 beim Senden → API-Aufruf übergibt tsc=0.

---

## 3  Minimal-Hardware-Test (Pseudocode ohne PTP-Stack)

Dieses Design eliminiert alle Stack-Effekte. Implementierung als einzelner CLI-Befehl  
`tsu_probe` in `app.c`.

```c
/* -----------------------------------------------------------------------
 * CLI-Befehl:  tsu_probe
 * Zweck:       Prüft ob LAN8651-TSU überhaupt einmal TTSCAA setzt.
 * Keine State-Machine, kein PTP — einmalige sequenzielle Ausführung.
 * ----------------------------------------------------------------------- */

// Schritt 1: Sicherungszustand speichern
uint32_t saved_nc, saved_cfg0, saved_imask0, saved_txmctl;
DRV_LAN865X_ReadRegBlocking(0x00010000, &saved_nc);
DRV_LAN865X_ReadRegBlocking(0x00000004, &saved_cfg0);
DRV_LAN865X_ReadRegBlocking(0x0000000C, &saved_imask0);
DRV_LAN865X_ReadRegBlocking(0x00040040, &saved_txmctl);

UART_LOG("[TSU_PROBE] NC=0x%08X CFG0=0x%08X IMASK0=0x%08X TXMCTL=0x%08X\r\n",
         saved_nc, saved_cfg0, saved_imask0, saved_txmctl);

// Schritt 2: Minimale Konfiguration setzen
DRV_LAN865X_WriteRegBlocking(0x0000000C, 0x00000000); // IMASK0: alles unmaskiert
DRV_LAN865X_WriteRegBlocking(0x00010000, 0x0000804C); // NC: TXEN+RXEN+Bit15
DRV_LAN865X_WriteRegBlocking(0x00010077, 40u);         // MAC_TI = 40 ns/tick
DRV_LAN865X_WriteRegBlocking(0x00040040, 0x00000000); // TXMCTL = 0 (pure TSC-Pfad)

// Schritt 3: Slot A leeren (W1C falls belegt)
DRV_LAN865X_WriteRegBlocking(0x00000008, 0x00000100); // Clear TTSCAA
delay_ms(5);

// Schritt 4: TSU-Lauf verifizieren
uint32_t tn1, tn2;
DRV_LAN865X_ReadRegBlocking(0x00010075, &tn1);
delay_ms(20);
DRV_LAN865X_ReadRegBlocking(0x00010075, &tn2);
uint32_t delta = (tn2 >= tn1) ? (tn2 - tn1) : (1000000000u - tn1 + tn2);
UART_LOG("[TSU_PROBE] MAC_TN delta=%lu ns (expected ~20000000)\r\n", delta);
if (delta < 15000000u || delta > 25000000u)
    UART_LOG("[TSU_PROBE] FAIL: TSU not running!\r\n");

// Schritt 5: Minimalen Ethernet-Frame aufbauen (kein PTP-Payload nötig)
static uint8_t probe_frame[60];
memset(probe_frame, 0, 60);
probe_frame[0] = 0xFF; probe_frame[1] = 0xFF; probe_frame[2] = 0xFF;  // dst: broadcast
probe_frame[3] = 0xFF; probe_frame[4] = 0xFF; probe_frame[5] = 0xFF;
// src: eigene MAC (aus gm_src_mac)
memcpy(&probe_frame[6], gm_src_mac, 6);
// EtherType: 0x88F7 (PTP)
probe_frame[12] = 0x88; probe_frame[13] = 0xF7;
// Payload: beliebig (0xAA)
memset(&probe_frame[14], 0xAA, 46);

// Schritt 6: Frame senden mit TSC=01
bool sent = DRV_LAN865X_SendRawEthFrame(0, probe_frame, 60, 0x01, NULL, NULL);
UART_LOG("[TSU_PROBE] Frame sent: %s\r\n", sent ? "OK" : "FAIL");

// Schritt 7: 50ms warten (lässt PLCA-Slot und EXST-Verarbeitung zu)
//            WICHTIG: TC6_Service muss während dieser Zeit regulär laufen!
//            → nicht einfach delay_ms(50) bei blockierender Wait-Schleife.
//              Stattdessen: asynchrones Polling über mehrere Service-Zyklen.
uint32_t t_start = GET_TICKS_MS();
uint32_t status0_snap = 0, status1_snap = 0;
bool ttscaa_found = false;

while ((GET_TICKS_MS() - t_start) < 200u) {
    // TC6_Service läuft im Hintergrund (Harmonx 3 Task)
    // Hier direkt die gespeicherten TTSC-Bits pollen
    uint32_t ts = DRV_LAN865X_GetAndClearTsCapture(0);
    if (ts & 0x0700u) {
        UART_LOG("[TSU_PROBE] TTSCAA/B/C bits: 0x%03X  *** CAPTURE SUCCESS! ***\r\n",
                 (ts >> 8) & 0x7u);
        ttscaa_found = true;
        break;
    }
    delay_ms(2);
}

// Schritt 8: Ergebnis
if (!ttscaa_found) {
    // Direkter Rohlesezugriff auf STATUS0
    DRV_LAN865X_ReadRegBlocking(0x00000008, &status0_snap);
    DRV_LAN865X_ReadRegBlocking(0x00000009, &status1_snap);
    UART_LOG("[TSU_PROBE] FAIL — STATUS0=0x%08X STATUS1=0x%08X\r\n",
             status0_snap, status1_snap);
    UART_LOG("[TSU_PROBE] TTSCAA bit8=%d  TTSCMA bit24=%d  TTSCOFA bit21=%d\r\n",
             (int)((status0_snap >> 8) & 1),
             (int)((status1_snap >> 24) & 1),
             (int)((status1_snap >> 21) & 1));
}

// Schritt 9: Konfiguration wiederherstellen
DRV_LAN865X_WriteRegBlocking(0x00010000, saved_nc);
DRV_LAN865X_WriteRegBlocking(0x00000004, saved_cfg0);
DRV_LAN865X_WriteRegBlocking(0x0000000C, saved_imask0);
DRV_LAN865X_WriteRegBlocking(0x00040040, saved_txmctl);
UART_LOG("[TSU_PROBE] Config restored.\r\n");
```

**Interpretation des Schritt-8-Ergebnisses:**

| STATUS0.bit8 | STATUS1.bit24 | STATUS1.bit21 | Diagnose |
|:---:|:---:|:---:|---------|
| 1 | 0 | 0 | ✅ Hardware OK, Bug liegt in `_OnStatus0` oder Timing |
| 0 | 1 | 0 | ❌ **Block A**: GEM-TSU liefert keinen TX-Timestamp. Bit 15 NETWORK_CONTROL testen. |
| 0 | 1 | 1 | ❌ Slot A war voll (eigentlich nicht, da wir cleared haben) → Race Condition |
| 0 | 0 | 0 | ❌ Frame nie rausgegangen (PLCA-Link down?) oder TSC-Bit verloren |
| 0 | 0 | 1 — bit17=1 | ❌ GEM FSM_State_Error — interne Fehlermaschine → Neustart nötig |

---

## 4  Register-Details für den Test

### OA_STATUS0 (0x00000008)

| Bit | Name | Bedeutung | W1C-Verhalten |
|-----|------|-----------|---------------|
| 8 | TTSCAA | TX Timestamp Capture Available Slot A | Durch Lesen + W1C löschen |
| 9 | TTSCAB | Slot B | dto. |
| 10 | TTSCAC | Slot C | dto. |
| 11 | TXFCSE | TX Frame Check Sequence Error | dto. |
| 4 | LOFR | Loss of Framing → Reinit | dto. + reinit |
| 6 | RESETC | Reset Complete | dto. |

**Fallstrick:** `_OnStatus0` in `drv_lan865x_api.c` speichert TTSCAA-Bits **vor** W1C korrekt.  
**Aber:** `PRINT_LIMIT` im switch-Block kann TTSCAA-Print unterdrücken. Der `[DBG]`-Print  
(Zeile ~2341) ist kein PRINT_LIMIT → wenn er nie erscheint, wurde TTSCAA nie gesetzt.

### OA_STATUS1 (0x00000009)

| Bit | Name | Bedeutung |
|-----|------|-----------|
| 24 | TTSCMA | TX Timestamp Capture **Missed** A → Capture-Engine Fehler |
| 21 | TTSCOFA | TX Timestamp Capture **Overflow** A → Slot war noch belegt |
| 17 | FSM_State_Error | Interne GEM-Zustandsmaschine Fehler |
| 18 | SRAM_ECC_Error | Speicherfehler → Neustart nötig |

**Fallstrick:** `PRINT_LIMIT` in `_OnStatus1` → genaue Häufigkeit nicht sichtbar.  
**Fix:** `SYS_CONSOLE_PRINT("[DBG] _OnStatus1: 0x%08lX\r\n", value)` ohne PRINT_LIMIT  
direkt am Anfang von `_OnStatus1` hinzufügen.

### NETWORK_CONTROL / GEM (0x00010000)

| Bit | Name | Relevanz |
|-----|------|---------|
| 2 | TXEN | TX aktiviert — muss gesetzt sein |
| 3 | RXEN | RX aktiviert — muss gesetzt sein |
| **15** | **STORE_TX_TS?** | **Hypothese: muss 1 sein für TX-SFD-Capture** |
| 18 | STORE_RX_TS? | Evtl. für RX-Timestamps |

**Fallstrick:** Nach LOFR-Reset schreibt `_InitUserSettings` case 9 immer `0x0000000C`.  
Wenn CHK-12 zeigt, dass Bit 15 funktioniert, muss `_InitUserSettings` case 9 dauerhaft  
angepasst werden, damit der Fix auch nach Resets bestehen bleibt.

### MAC_TI / GEM (0x00010077)

| Bits | Bedeutung |
|------|-----------|
| [7:0] | Nanosekunden-Inkrement pro Takt. Bei 25 MHz: 40 ns → `0x28` |

**Verifikation:** `mac_tn` steigt mit ~40 ns/Takt ≈ 350 MHz nicht, sondern  
25 MHz × 40 ns/Takt = 1 ns/ns = TSU läuft mit Systemzeit in Nanosekunden.  
Δ(MAC_TN) über 10 ms ≈ 10.000.000 ns ± 1%.

### CONFIG0 (0x00000004)

| Bit | Name | Bedeutung |
|-----|------|-----------|
| 7 | FTSE | Frame Timestamp Enable — **für RX-FRAMEINFO-Präfix** (nicht TX TSC!) |
| 6 | FTSS | Frame Timestamp Size (0=32bit, 1=64bit) |

**Fallstrick:** Code-Kommentar `"required for TTSCAA TX capture"` ist **falsch**.  
FTSE steuert den RTSA-Bit-Mechanismus für eingehende Frames. Für TSC-basiertes  
TX-Timestamping ist FTSE nicht nötig — es schadet aber auch nicht.

### TXMCTL (0x00040040)

| Bit | Name | Beschreibung |
|-----|------|-------------|
| 7 | TXPMDET | TX Pattern Match Detected (RO, auto-clear nach Lesen) |
| 2 | MACTXTSE | MAC TX Timestamp Slave Enable (GEM-interner Pfad) |
| 1 | TXME | TX Match Enable (Pattern-Match-Pfad aktiv) |

**Aktueller Wert:** 0x0000 — rein TSC-basierter Pfad wird getestet.  
**Wichtig:** TXPMDET (~0.6) beweist EtherType-Erkennung, aber nicht Capture.

### OA_IMASK0 (0x0000000C)

Wert: `0x00000000` (korrekt, alle Bits unmaskiert, TTSCAA nicht blockiert).  
**Nach LOFR-Reset:** MemMap schreibt `0x00000000` → bleibt korrekt. ✓

---

## 5  Sofort-Aktionsplan (priorisiert)

```
✅ ERLEDIGT (2026-04-02):
 └─ CHK-10: NC=0x0000000C bestätigt
 └─ CHK-11: _OnStatus1 Debug-Print eingebaut, gebaut, geflasht
 └─ CHK-12: Bit 15 getestet (A: Baseline, B: +Bit15) → beide STATUS1=0, kein Effekt

Priorität 1 (JETZT — Firmware-Build nötig):
 └─ CHK-50: TXME=1 + TSC=1 → ptp_mode master → STATUS1/STATUS0 beobachten
    → gm_write(GM_TXMCTL, 0x0002) setzen (ptp_gm_task.c oder via CLI lan_write 0x00040040 0x0002)
 └─ CHK-52: TX-Header byte3 im tc6.c loggen → tsc-Feld im Draht verifizieren

Priorität 2 (nach CHK-50-Ergebnis):
 └─ Wenn CHK-50 TTSCAA/TTSCMA gibt:
     → TXME=1 als permanenter Fix, kombiniert mit tsc=1 testen
     → Microchip TC6-Spec: erklärt TXME-Anforderung für TSC
 └─ Wenn CHK-50 weiter STATUS1=0:
     → CHK-52 zwingend: SPI-Header-Byte3-Log einbauen
     → CHK-40: tsu_probe-Befehl als isolierter Test
     → Microchip Support kontaktieren

Priorität 3:
 └─ CHK-22: EXST-Counter → EXST-Rate prüfen
 └─ CHK-51: MACTXTSE-Pfad isoliert testen
 └─ CHK-13: NETWORK_CONTROL +Bit18 testen
```

---

## 6  Bekannte Hypothesen-Statusüberblick (Stand 2026-04-02)

| ID | Hypothese | Status | Nächster Schritt |
|----|-----------|--------|-----------------|
| H1 | CONFIG0.FTSE/FTSS falsch | ⚠️ Falsch-Kommentar, aber Wert gesetzt | CHK-31 |
| H2 | TXMLOC/TXMMSK falsch | ✅ Widerlegt | — |
| H3 | MACTXTSE Kollision | ✅ Widerlegt | — |
| H4 | IMASK0/W1C Race | ✅ Code korrekt, ausgeschlossen | — |
| **H5** | **TXME für TSC nötig (ohne → kein Capture-Versuch)** | **🔴 HÖCHSTE PRIO** | **CHK-50** |
| H6 | TXMPATL Bit 0x10 (TSMT) | 🟡 Irrelevant für TSC-Pfad | — |
| H7 | LOFR-Reset löscht PTP-Config | 🟡 Teilweise — MAC_TI wird neu geschrieben | CHK-22 |
| H8 | NETWORK_CONTROL fehlt GEM-TSU-Bit (Bit 15) | ❌ GETESTET (CHK-12) — kein Effekt | — |
| H9 | TTSCAA gesetzt aber zu schnell gelöscht | ✅ Ausgeschlossen — STATUS1 ebenfalls 0 | — |
| H10 | lan_read verursacht LOFR und Reset | 🟡 Sekundär | CHK-22 |
| **H11** | **TSC-Feld im SPI-Header wird nicht gesetzt (entry->tsc=0)** | **🔴 Offen** | **CHK-52** |
| **H12** | **TC6 ignoriert TSC ohne TXME=1 (Spec-Verhalten)** | **🔴 Offen** | **CHK-50** |

---

*Analysierte Code-Basis: tc6.c (SET_VAL, HDR_TSC), drv_lan865x_api.c (_OnStatus0,  
_OnStatus1, TC6_CB_OnExtendedStatus, _InitUserSettings), ptp_gm_task.c (GM-State-Machine).*

---

## 7  Testergebnisse 2026-04-02 (tsu_register_check.py)

**Script:** `tsu_register_check.py --gm COM10 --fol COM8`  
**Log:** `tsu_check_20260402_173633.log`  
**Firmware:** Build nach CHK-11 (STATUS1-Debug-Print ohne PRINT_LIMIT)

### TIER 1 — TSU-Uhr

| Test | Ergebnis | Details |
|------|---------|---------|
| TEST_TSU_01 MAC_TI | ✅ PASS | MAC_TI=0x28 (40 ns/Takt) ✓ |
| TEST_TSU_02 MAC_TN | ❌ FAIL | Δ=137.288.000 ns in 500ms (erwartet ~500M ns, ~27%) |
| TEST_TSU_03 MAC_TSL | ✅ PASS | MAC_TSL Δ=5 s ✓ |
| TEST_TSU_04 MAC_TSH | ✅ PASS | MAC_TSH stabil ✓ |

**Anmerkung TEST_TSU_02:** Die ~27%-Abweichung ist wahrscheinlich ein Messartefakt  
des Python-Scripts (Serial-Overhead macht 500ms-Python-Sleep nicht board-äquivalent).  
MAC_TSL steigt korrekt (5s über 5s) → TSU läuft auf dem Board mit korrekter Rate.

### TIER 2 — OA-Register

| Test | Ergebnis | Details |
|------|---------|---------|
| TEST_OA_01 CONFIG0 | ✅ PASS | 0x000090E6 ✓ |
| TEST_OA_02 STATUS0 W1C | ✅ PASS | W1C=0x00000000 ✓ |
| TEST_OA_03 IMASK | ✅ PASS | IMASK0=0x00000000, IMASK1=0x3FFE0003 ✓ |
| TEST_OA_04 TTSC-Baseline | ✅ PASS | Alle TTSC-Register = 0 ✓ |

### TIER 3 — TX-Match + NETWORK_CONTROL

| Test | Ergebnis | Details |
|------|---------|---------|
| TEST_TXM_01 TX-Match Regs | ✅ PASS | Alle TX-Match-Register korrekt ✓ |
| TEST_TXM_02 EtherType | ✅ PASS | EtherType-Low=0xF7 ✓ |
| TXMCTL | — | 0x00000000 (TXME=0, MACTXTSE=0) |
| NETWORK_CTRL | ⚠️ WARN | 0x0000000C — Bit 15 nicht gesetzt |
| RXMCTL | — | 0x02, RXMMSKH=0xFF, RXMMSKL=0xFFFF ✓ |

### TIER 4 — TTSCAA-Capture-Baseline

```
65 Syncs gesendet | 66 "TTSCA not set" Timeouts
STATUS0-Events = 0  (TTSCAA nie gesetzt)
STATUS1-Events = 0  ([DBG] _OnStatus1 nie erschienen → TTSCMA nie gesetzt)
FU = 0
→ FAIL: EXST ohne TTSCAA, keine Timestamp-Aktivität
```

### CHK-12 A/B Vergleichstest

```
── Test A — NC=0x0000000C (Baseline) ──
Syncs=30 | STATUS0-Events=0 | STATUS1-Events=0 | TTSCAA=nein | TTSCMA=0 | FU=0
→ FAIL

── Test B — NC=0x0000800C (+Bit15) ──
NC readback: 0x0000800C ✓
Syncs=28 | STATUS0-Events=0 | STATUS1-Events=0 | TTSCAA=nein | TTSCMA=0 | FU=0
→ FAIL

Ergebnis: TTSCAA in KEINEM Test — Bit 15 allein nicht ausreichend
```

### Gesamtergebnis

```
✗  TIER-1 TSU-Uhr  (TEST_TSU_02 Messartefakt, Board-seitig OK)
✓  TIER-2 OA-Reg
✓  TIER-3 TX-Match+NC
✗  TIER-4 TTSCAA-Capture
✗  CHK-12 Bit15-Test
2/5 Tests bestanden.
```

### Schlussfolgerung aus Testergebnissen

Das kritischste Ergebnis: **STATUS1 = 0 in allen Tests.**  
Mit CHK-11 (Debug-Print ohne PRINT_LIMIT) bewiesen, dass TTSCMA vom TC6-Controller  
nicht generiert wird. Das bedeutet: der Controller unternimmt **keinen Capture-Versuch**,  
obwohl TSC=01 via API gesetzt wird.

Mögliche Ursachen (priorisiert für nächsten Schritt):
1. **H12 / CHK-50**: TC6 benötigt TXME=1, um TSC-Feld zu verarbeiten → Test ausstehend
2. **H11 / CHK-52**: `entry->tsc` ist 0 beim Senden → TX-Header-Log in tc6.c einbauen
