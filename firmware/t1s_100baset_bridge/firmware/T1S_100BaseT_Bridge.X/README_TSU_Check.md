# README: LAN8651 TSU / PTP-Register-Prüfplan

## Zweck

Dieser Prüfplan klärt systematisch, welche LAN8651-Register für PTP-Timestamping
relevant sind, prüft ihre Konsistenz und zeigt Kausalitätsketten auf.
Anlass: TTSCMA (TX Timestamp Capture Missed A) tritt permanent auf, obwohl
TSC=1 korrekt im SPI-Header gesetzt wird und der TSU läuft.

---

## Abgleich: Externes README vs. Firmware-Zustand (2026-04-02)

### ÜBEREINSTIMMUNGEN ✓

| Thema | Externes README | Unsere Firmware |
|-------|-----------------|-----------------|
| CONFIG0.FTSE aktivieren | Bit 7 setzen | 0x90E6 (FTSE=1, FTSS=1) ✓ |
| CONFIG0.FTSS = 1 (64-bit) | Bit 6 setzen | 0x90E6 ✓ |
| MAC_TI = 40 ns | 40 ns pro 25-MHz-Takt | `gm_write(MAC_TI, 40)` ✓ |
| MAC_TSH / MAC_TSL / MAC_TN | Wall-Clock-Register | PTP_FOL_task.h ✓ |
| TSC-Feld im SPI-Header | 01=Slot A | `TC6_SendRawEthernetPacket(..., tsc=1, ...)` ✓ |
| TTSCAH/TTSCAL auslesen nach TTSCAA | OA-Reg 0x10/0x11 | GM_OA_TTSCAH/L ✓ |
| NETWORK_CONTROL TX/RX enable | TXEN+RXEN = 0x0C | `_InitUserSettings` case 9 → 0x0C ✓ |
| IMASK0 = 0 (TTSCAA nicht maskiert) | Muss 0 sein | IMASK0 = 0x00000000 ✓ |

### ABWEICHUNGEN / LÜCKEN ⚠

| Thema | Externes README | Unsere Firmware | Bewertung |
|-------|-----------------|-----------------|-----------|
| RXMMSKH/RXMMSKL = 0xFFFFFF | Für RX-Timing konfigurieren | 0x00040053=0xFF, 0x00040054=0xFFFF (fast gleich) | Kleiner Unterschied im High-Byte |
| RXMLOC = 0 | Match ab SFD-Start | 0x00040055=0x0000 ✓ | OK |
| RXMCTL — RX Match Enable | Nicht explizit erwähnt | 0x00040050=0x0002 (RXME=1) | Im MEMMAP aktiviert ✓ |
| MACTXTSE für TX-Capture | Nicht erwähnt (TSC allein reicht) | Firmware nutzte MACTXTSE=4 | ⚠ Widerspruch — MACTXTSE nicht nötig laut Spec |
| TSC=1 allein → TTSCAA | Direkte Kausalität laut Spec | Liefert TTSCMA statt TTSCAA | ❌ PROBLEM |
| FTSE ist für RX-Timestamps | Klar benannt | Kommentar in Code: "required for TTSCAA" | ❌ Falschaussage im Code-Kommentar |
| MAC_TSH init auf Startzeit | Empfohlen: bekannte Startzeit setzen | Nicht gesetzt (TSU startet bei 0) | ⚠ OK für relative Messung |
| GMAC_NCR erweiterte PTP-Bits | PTPUNICAST / weitere Bits? | Nur TXEN+RXEN=0x0C gesetzt | Unklar ob weitere Bits nötig |

### KRITISCHE OFFENE FRAGEN

1. **Warum TTSCMA statt TTSCAA?**
   - TSC=1 im SPI-Header erreicht die Hardware (TTSCMA beweist das)
   - TSU läuft (MAC_TN zählt ~350 Mns/s)
   - Slot A ist nicht belegt (TTSCOFA=0, TTSCAH=0)
   - Weder MACTXTSE noch TXME hat Einfluss auf das Problem
   - → Möglicherweise fehlt ein LAN8651-spezifischer Enable-Bit

2. **Braucht die GEM-Engine einen PTP-Enable-Bit?**
   - Standard-GEM hat kein explizites "TSU clock source enable" außer MAC_TI > 0
   - LAN8651 könnte proprietäre Voraussetzung haben

3. **Ist TXMPATL-Unterfeld "tsmt" (0x10) relevant?**
   - TXMPATL = 0xF710: obere 8 Bit = 0xF7 (EtherType low), untere Bits = 0x10
   - Was bedeutet 0x10 im unteren Byte von TXMPATL?

---

## Register-Karte (alle PTP-relevanten Register)

### MMS=0 (Open Alliance Standard-Register-Raum, 0x0000xxxx)

| Adresse | Name | Funktion | Erwartet | Prüfbar? |
|---------|------|----------|---------|---------|
| 0x00000004 | CONFIG0 | Globale Konfiguration, FTSE/FTSS | 0x90E6 | W+R |
| 0x00000008 | STATUS0 | TX-Timestamp-Capture A/B/C, TXBUE | W1C | W1C-Test |
| 0x00000009 | STATUS1 | TTSCMA, Fehler | W1C | W1C-Test |
| 0x0000000C | IMASK0 | Interrupt-Maske für STATUS0 | 0x0000 (alles unmasked) | R |
| 0x0000000D | IMASK1 | Interrupt-Maske für STATUS1 | 0x3FFE0003 | R |
| 0x00000010 | TTSCAH | TX Timestamp Capture A: Sekunden | 0 (kein Capture) | R |
| 0x00000011 | TTSCAL | TX Timestamp Capture A: Nanosekunden | 0 | R |
| 0x00000012 | TTSCBH | TX Timestamp Capture B: Sekunden | 0 | R |
| 0x00000013 | TTSCBL | TX Timestamp Capture B: Nanosekunden | 0 | R |
| 0x00000014 | TTSCCH | TX Timestamp Capture C: Sekunden | 0 | R |
| 0x00000015 | TTSCCL | TX Timestamp Capture C: Nanosekunden | 0 | R |

### MMS=1 (LAN8651-interner GEM-MAC, 0x0001xxxx)

| Adresse | Name | GEM-Register (Byte-Offset) | Funktion | Erwartet |
|---------|------|---------------------------|----------|---------|
| 0x00010000 | NETWORK_CONTROL | 0x000 | TXEN, RXEN, 1588-Optionen | 0x0000000C |
| 0x00010001 | NETWORK_CONFIG | 0x004 | Speed, Duplex, Promiscuous | 0x00000000 |
| 0x0001006F | MAC_TISUBN | 0x1BC | Sub-Nanosekunden-Inkrement | 0 (OS-Init) |
| 0x00010070 | MAC_TSH | 0x1C0 | TSU Sekunden High (32 Bit) | ≥0 |
| 0x00010074 | MAC_TSL | 0x1D0 | TSU Sekunden Low (32 Bit) | ≥0, steigt 1/s |
| 0x00010075 | MAC_TN | 0x1D4 | TSU Nanosekunden | zwischen 0–1e9, läuft |
| 0x00010076 | MAC_TA | 0x1D8 | TSU Timer Adjust (einmalig) | 0 nach Anwendung |
| 0x00010077 | MAC_TI | 0x1DC | TSU Timer Inkrement [7:0] | 0x28 (40 ns) |

### MMS=4 (LAN8651 TX/RX Match + analoge Register, 0x0004xxxx)

| Adresse | Name | Funktion | Erwartet |
|---------|------|----------|---------|
| 0x00040040 | TXMCTL | TX Match Control: MACTXTSE(2), TXME(1), TXPMDET(7) | 0x0000 (Baseline) |
| 0x00040041 | TXMPATH | TX Match Pattern High (EtherType-High-Byte) | 0x0088 |
| 0x00040042 | TXMPATL | TX Match Pattern Low + Steuerbits | 0xF710 |
| 0x00040043 | TXMMSKH | TX Match Maske High (0=exact, FF=ignore) | 0x0000 |
| 0x00040044 | TXMMSKL | TX Match Maske Low | 0x0000 |
| 0x00040045 | TXMLOC | TX Match Location (Byte-Offset) | 0x000C (12) |
| 0x00040050 | RXMCTL | RX Match Control | 0x0002 (RXME=1) |
| 0x00040053 | RXMMSKH | RX Match Maske High | 0x00FF |
| 0x00040054 | RXMMSKL | RX Match Maske Low | 0xFFFF |
| 0x00040055 | RXMLOC | RX Match Location | 0x0000 |

---

## Testplan

### TIER 1: TSU Wall-Clock-Verifikation

#### TEST_TSU_01 — MAC_TI Write/Read-Kohärenz
- **Aktion**: `lan_write 0x00010077 0x00000028`, dann `lan_read 0x00010077`
- **Erwartet**: Readback = 0x28 (40)
- **Kausalität**: MAC_TI muss persistieren, sonst takt TSU nicht richtig
- **Fehlerfall**: Readback ≠ 0x28 → MAC-Register-Zugriff kaputt oder falsche Adresse

#### TEST_TSU_02 — MAC_TN Zählt
- **Aktion**: `lan_read MAC_TN` bei t=0 und t=500ms
- **Erwartet**: Δ ≈ 500 000 000 (±20%)
- **Kausalität**: TSU muss 40 ns pro 25-MHz-Takt inkrementieren
- **Fehlerfall**: Δ ≈ 0 → TSU steht (MAC_TI nicht gesetzt oder MAC nicht initalisiert)

#### TEST_TSU_03 — MAC_TSL Sekunden-Rollover
- **Aktion**: `lan_read MAC_TSL` bei t=0 und t=1200ms
- **Erwartet**: Δ ≥ 1
- **Kausalität**: Wenn MAC_TN 1e9 überläuft, muss MAC_TSL inkrementieren

#### TEST_TSU_04 — MAC_TSH konsistent
- **Aktion**: `lan_read MAC_TSH` zweimal mit 2s Abstand
- **Erwartet**: Gleicher Wert (ändert sich erst nach ~136 Jahren)

---

### TIER 2: OA-Register-Kohärenz

#### TEST_OA_01 — CONFIG0 Wert nach Init
- **Aktion**: `lan_read 0x00000004` nach 8s Reset
- **Erwartet**: 0x90E6 (FTSE=1, FTSS=1)
- **Kausalität**: _InitUserSettings case 8 muss nach MEMMAP ausgeführt worden sein
- **Hinweis**: MEMMAP schreibt CONFIG0=0x26 (ohne FTSE/FTSS). Fall 8 überschreibt mit 0x90E6.
  **Wenn hier 0x26 steht → Initialisierungsreihenfolge ist falsch!**

#### TEST_OA_02 — STATUS0 W1C-Verhalten
- **Aktion**: W1C STATUS0 mit 0xFFFFFFFF, dann sofort lesen
- **Erwartet**: 0x00000000
- **Kausalität**: W1C muss sofort wirken

#### TEST_OA_03 — IMASK0/IMASK1 Prüfung
- **Aktion**: `lan_read 0x0000000C`, `lan_read 0x0000000D`
- **Erwartet**: IMASK0=0x0000 (nichts maskiert), IMASK1=0x3FFE0003
- **Kausalität**: Falls TTSCAA-Bit in IMASK0 gesetzt, wird STATUS0-Interrupt blockiert

#### TEST_OA_04 — TTSCAH/TTSCAL/TTSCBH/TTSCBL Baseline
- **Aktion**: Alle 6 Capture-Register (0x10–0x15) lesen, BEVOR PTP aktiv
- **Erwartet**: Alle = 0x00000000
- **Kausalität**: Bei stehender TSC-Anfrage kein Capture → muss 0 sein

---

### TIER 3: TX-Match-Register-Kohärenz

#### TEST_TXM_01 — Alle TX-Match-Register Schreib/Lese
- **Aktion**: Jeden Register schreiben, direkt zurücklesen
- Register: TXMLOC=12, TXMPATH=0x88, TXMPATL=0xF710, TXMMSKH=0x00, TXMMSKL=0x00
- **Erwartet**: Exakter Readback
- **Kausalität**: Wenn TXMPATL falsch zurückgelesen → Pattern-Match unmöglich

#### TEST_TXM_02 — TXMPATL Unterfeld-Analyse (0x10)
- **Frage**: Was bedeutet das untere Byte 0x10 in TXMPATL=0xF710?
  - Bit 4 könnte "TSMT" (Timestamp Match Trigger) sein
  - Ist dieses Bit nötig für TTSCAA-Capture?
- **Aktion A**: TXMPATL=0xF700 (ohne 0x10), TXME=1, vollständige Maske, senden, warten
- **Aktion B**: TXMPATL=0xF710 (mit 0x10), TXME=1, vollständige Maske, senden, warten
- **Vergleich**: Erscheint TTSCAA bei Aktion A oder B?

#### TEST_TXM_03 — TXME + Vollmaske = TXPMDET?
- **Aktion**: TXMCTL=0x0002 (TXME, kein MACTXTSE), TXMMSKH=0xFF, TXMMSKL=0xFF, dann senden
- **Poll**: TXMCTL bit 7 (TXPMDET)
- **Erwartet**: TXPMDET=1 nach nächstem gesendeten Frame
- **Kausalität**: Wenn TXPMDET mit Vollmaske nie feuert → TX-Pattern-Matcher selbst defekt

#### TEST_TXM_04 — MACTXTSE + TXME Gegenseitigkeitsausschluss
- **Aktion**: TXMCTL schreiben mit 0x0006 (MACTXTSE|TXME), dann zurücklesen
- **Erwartet**: Hardware erzwingt Auswahl (laut vorheriger Test: Readback = 0x04)
- **Kausalität**: Bestätigt Gegenseitigkeitsausschluss gemäß Datenblatt 11.5.20

---

### TIER 4: TX Timestamp Capture — Kausalitätskette

#### TEST_TTSC_01 — Minimaler Test: TSC=1, kein MACTXTSE, kein TXME
- **Setup**: TXMCTL=0x0000, ptp_mode master
- **Aktion**: 1 Sync senden (TSC=1 im Header), STATUS0+STATUS1 alle 10ms für 500ms pollen
- **Erwartet**: **TTSCAA (STATUS0 bit 8) erscheint** — nicht TTSCMA
- **Kausalität**: TSC=1 allein MUSS genügen per OPEN Alliance TC6 Spec
- **Fehlerfall**: TTSCMA → Capture-Request erreicht Hardware, aber GEM-MAC kann nicht capturen

#### TEST_TTSC_02 — TTSCAH/TTSCAL sofort nach Capture
- **Wenn TEST_TTSC_01 TTSCAA liefert**:
  - Sofort TTSCAH lesen (≤ 5ms nach TTSCAA)
  - TTSCAL lesen
  - **Erwartet**: TTSCAH = aktueller MAC_TSL, TTSCAL < 1e9
  - **Kausalität**: Sekunden-Teil muss dem Laufzuwachs entsprechen

#### TEST_TTSC_03 — TTSCMA-Tiefendiagnose
- **Prüfe STATUS1-Inhalt genau**:
  - Bit 24 = TTSCMA ← unser Problem
  - Bit 21 = TTSCOFA (Overflow A) ← falls gesetzt: Slot bereits belegt
  - Bit 17 = FSM_State_Error ← falls GEM-MAC in Fehlerzustand
  - Bit 18 = SRAM_ECC_Error
- **Kausalität**: TTSCMA + TTSCOFA=0 → Slot frei aber Capture scheitert = GEM-internes Problem

#### TEST_TTSC_04 — NETWORK_CONTROL Baseline
- **Aktion**: `lan_read 0x00010000`
- **Erwartet**: 0x0000000C
- **Kausalität**: MAC muss TX+RX enabled haben
- **Erweitert**: Prüfe ob Bits 18-20 (PTPUNICAST, 2-step etc.) relevant sind

#### TEST_TTSC_05 — NETWORK_CONFIG Baseline
- **Aktion**: `lan_read 0x00010001`
- **Erwartet**: 0x00000000 oder 0x00000010 (Promiscuous)
- **Kausalität**: Keine PTP-blockierenden Filter gesetzt?

---

### TIER 5: RX-Kanal-Verifikation (informatorisch)

#### TEST_RX_01 — RX Match Register Baseline
- **Aktion**: Alle RX-Match-Register lesen (0x50–0x55)
- **Erwartet**:
  - RXMCTL (0x50) = 0x0002 (RXME=1)
  - RXMMSKH (0x53) = 0x00FF
  - RXMMSKL (0x54) = 0xFFFF
  - RXMLOC (0x55) = 0x0000

#### TEST_RX_02 — FTSE Wirkung auf RX-Timestamps
- **Konzept**: Mit FTSE=1 sollte jedes empfangene Frame einen 8-Byte-Timestamp-Präfix
  im SPI-Datenstrom haben (RTSA=1 im Footer)
- **Kausalität**: Wenn FTSE=1 aber kein RX-Timestamp-Präfix → FTSE-Initialisierung falsch

---

## Bekannte Probleme / Hypothesen-Stand

| # | Hypothese | Status | Quelle |
|---|-----------|--------|--------|
| H1 | TXBUE kommt von MACTXTSE-Kollision (Normal) | ✅ Widerlegt — kein TXBUE ohne MACTXTSE | Diagnose-Test |
| H2 | EXST-Lock (100ms) blockiert TTSCAA-Verarbeitung | ✅ Widerlegt — auf 5ms reduziert, kein Effekt | DELAY_UNLOCK_EXT=5ms |
| H3 | TXMLOC falsch (Nibble- vs. Byte-Offset) | ✅ Widerlegt — TXMLOC=12 (Byte) korrekt, trotzdem TTSCMA | TXMLOC-Fix |
| H4 | MACTXTSE verhindert Capture ohne TXPMDET | ✅ Widerlegt — ohne MACTXTSE (TXMCTL=0) identisches Ergebnis | Aktueller Test |
| H5 | TXME erforderlich für TTSCAA | 🔴 Offen — TEST_TXM_03 prüft das | Nächster Schritt |
| H6 | TXMPATL-Bit 4 (TSMT) steuert Timestamp-Triggerung | 🔴 Offen — TEST_TXM_02 prüft das | Nächster Schritt |
| H7 | NETWORK_CONTROL fehlt PTP-Enable-Bit | 🔴 Offen — TEST_TTSC_04/05 prüfen GEM-NCR | Nächster Schritt |
| H8 | GEM benötigt Bit in NETWORK_CONTROL oder anderen MAC-Regs | 🔴 Offen | Datenblatt-Analyse nötig |

---

## Ausführung

```bash
python tsu_register_check.py --gm COM10 --fol COM8
```

### Erwartetes Gesamtergebnis bei korrekter Funktion:
- TEST_TSU_01–04: PASS
- TEST_OA_01–04: PASS
- TEST_TXM_01–04: Ergebnisse zeigen wo Blockierung liegt
- TEST_TTSC_01: **TTSCAA muss einmal erscheinen** (Hauptziel!)

---

## SPI-Header-Felder (Referenz)

```
Byte 3 [31:24]: DNC(1) DV(1) NORX(1) VS(2) SV(1) SWO(4) ...
Byte 2 [23:16]: EV(1) EBO(6) TSC(2) ...
Byte 1 [15:8]:  P(1) RCA(6) SEQ(1) ...
Byte 0 [7:0]:   P(1) ...
```

`TSC[1:0]` = Bits [17:16] des 32-Bit-Headers:
- `00` = kein Timestamp-Capture
- `01` = Capture in Slot A (TTSCAH/TTSCAL) → setzt TTSCAA in STATUS0
- `10` = Capture in Slot B
- `11` = Capture in Slot C

---

## Dokumentations-Links

- OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface Spec (TC6) v1.1
- Microchip LAN8651 Datasheet (DS60001828)
- Microchip AN1847: T1S 100BASE-T Bridge reference design
