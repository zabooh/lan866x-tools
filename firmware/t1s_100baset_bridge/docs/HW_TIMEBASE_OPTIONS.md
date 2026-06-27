# Hardware-Zeitbasis für die NTP-Uhr — drei Optionen (SAME54)

Untersuchung, wie aus der heute **softwareabgeleiteten** NTP-Zeit der Bridge eine
**echte Hardware-Zeit** wird, mit der sich Peripherien (ADC, DAC, GPIO, PWM) zu
exakten, zeitsynchronen Instanten treiben lassen.

> Quelle aller Datenblatt-Fakten: **SAM D5x/E5x Family Data Sheet, DS60001507K**
> (`firmware/t1s_100baset_bridge/Curiosity/SAME54_Datasheet.pdf`). Seitenangaben =
> gedruckte DS-Seiten. Code-Bezug: [`ntp_sync.c`](../firmware/src/ntp_sync.c),
> Taktbaum [`plib_clock.c`](../firmware/src/config/default/peripheral/clock/plib_clock.c).

---

## 1. Aufgabe

**Ist-Zustand.** Die Bridge führt die NTP-Zeit rein in Software:
- Ein **freilaufender Hardware-Zähler** `SYS_TIME = TC0_CH0` (16-bit, ~60 MHz aus
  GCLK1 = DPLL0÷2) wird ausgelesen (`ntp_raw_ns()`).
- `ntp_now_ns() = raw + s_offset_ns + ntp_rate_held(raw)` — Phasen-Offset **plus**
  eine in Software integrierte Frequenzkorrektur (`s_rate_ppb`, PI-Regler über die
  SET_OFFSET-Pakete vom PC, UDP 30491).
- Die lokale NTP-Zeit ist also ein **Rechenergebnis in Software**.

**Taktwurzel-Problem.** Der ganze Taktbaum wurzelt im **open-loop DFLL48M** (DPLL0
×120 → 120 MHz; GCLK1 = 60 MHz → TC0). Roh-Drift **~1800 ppm**. Der fitted
**12-MHz-MEMS-Oszillator (XOSC0)** der Curiosity-Ultra ist **nicht** als Taktquelle
gewählt.

**Das eigentliche Problem.** Ein *Software*-Zeitwert kann **nichts auslösen**: kein
ADC sampelt, kein GPIO toggelt, kein PWM-Flankenzeitpunkt liegt auf einem
NTP-Instant. Dafür muss die **Zeit als Zählerstand eines Hardware-Timers** vorliegen,
und dieser Zähler muss **synchronisiert (Phase) und syntonisiert (Frequenz)** sein.

**Zielgenauigkeit.** Synchronisation im Bereich **10–100 µs**, besser wenn möglich.

---

## 2. Die drei untersuchten Möglichkeiten

| # | Option | Rolle |
|---|---|---|
| **A** | **GMAC IEEE-1588 Timestamp Unit (TSU)** | disziplinierbare PTP-Hardware-Uhr + HW-Frame-Timestamps |
| **B** | **Oszillator/PLL-disziplinierter Timer** (XOSC0-Wurzel + DPLL1→TC) | disziplinierte Hardware-Zeitbasis über die Taktquelle |
| **C** | **Event System (EVSYS) als Fan-Out** | verteilt einen Timer-Compare CPU-frei an ADC/DAC/GPIO/PWM |

Vorab die Kernaussage, die der Vergleich (§6) ausführt: **A und B sind Alternativen
für die *disziplinierte Uhr*; C ist kein Konkurrent, sondern der *Verteilmechanismus*,
den A und B beide brauchen, um Peripherie zu treiben.**

---

## 3. Option A — GMAC IEEE-1588 Timestamp Unit (TSU)

### 3.1 Tiefenbohrung

**Zähler-Architektur (DS §24.6.2 S.437, §24.6.14 S.455/456).** Die TSU ist ein
**94-Bit-Timer**, getaktet mit dem GMAC-MCK:
- Bits [93:46] = **Sekunden** (48 bit) → `TSH`/`TSL`,
- Bits [45:16] = **Nanosekunden** (30 bit) → `TN`,
- Bits [15:0] = **Sub-Nanosekunden** (16 bit, intern, via Increment sichtbar).
Lese-/schreib-/justierbar in **1-ns-Auflösung** über APB; Sekunden-Übertrag erzeugt
Interrupt.

**Zentrale Register (DS §24.9):**

| Reg | Name | Offset | Seite | Schlüsselfelder |
|---|---|---|---|---|
| `TISUBN` | Timer Increment Sub-ns | 0x1BC | S.566 | `LSBTIR[15:0]` |
| `TSH` | Timer Seconds High | 0x1C0 | S.567 | `TCS[15:0]` = Sek [47:32] |
| `TSL` | Timer Seconds Low | 0x1D0 | S.568 | `TCS[31:0]` = Sek [31:0] |
| `TN` | Timer Nanoseconds | 0x1D4 | S.571 | `TNS[29:0]` |
| `TA` | Timer Adjust | 0x1D8 | S.572 | `ADJ` (Bit31: 1=subtr.), `ITDT[29:0]` ns |
| `TI` | Timer Increment | 0x1DC | S.573 | `CNS[7:0]` ns/Takt, `ACNS[7:0]`, `NIT[7:0]` |

**Raten-Mathematik (DS §24.6.14 S.455, §24.9.83/90 S.566/573).** Pro MCK-Takt: Timer
+= `TI.CNS` ns + `TISUBN.LSBTIR` sub-ns. **Sub-ns-Auflösung: Bit n = 2^(n−16) ns ≈
15,2 fs.** Das Alternativ-Increment (`ACNS`/`NIT`) erlaubt nicht-ganzzahlige
mittlere ns/Takt (DS-Beispiel 10,2 MHz, S.455).

**Offset-/Phasen-Justage (DS §24.9.89 S.572).** Ein Schreibzugriff auf `TA`
(`ADJ` + `ITDT[29:0]`) addiert/subtrahiert atomar einen ns-Betrag — Phasensprung in
einem Zugriff.

**Frame-Timestamping (DS §24.6.14 S.455, §24.9.91–94 S.574–577).**
**80-Bit-Capture-Register** halten den Timerwert beim *message timestamp point* (SFD
an der MII) — getrennt TX/RX, Event/Peer-Event (Offsets 0xE8…0x1EC). Jedes Update →
**Interrupt**. Wichtig: **Single-Capture pro Klasse** (letzter Event-Frame), keine
pro-Descriptor-Werte → bei Frame-Bursts zeitnah auslesen.

**Compare / „PPS" / Event-System (DS §24.4 S.435, §24.5.5 S.435, §24.6.14 S.456).**
- Compare-Register `NSC` (0xDC), `SCL` (0xE0), `SCH` (0xE4): Vergleich der 48
  Sekunden-Bits + oberen 22 ns-Bits; Treffer → internes Signal **GTSUCOMP**.
- **„The event GMAC Timestamp Comparison is connected to the Event System (EVSYS)"**
  (DS §24.5.5 S.435) + optionaler Interrupt (Status-Bit 29).
- **Kein dedizierter PPS-Ausgangspin** — der Compare-Output ist nur das interne
  GTSUCOMP-/EVSYS-Event plus Interrupt.

### 3.2 Analyse
**Passt hervorragend:** disziplinierbare HW-Uhr in Reinform — Rate per `TI`/`TISUBN`
(15,2-fs-Raster), Phase per `TA` (1 ns). Die heutige Software-Aufteilung
`s_rate_ppb`/`s_offset_ns` bildet sich **1:1** ab. Echte **SFD-genaue HW-Frame-Timestamps**
ersetzen den Software-Zeitstempel im UDP-Callback (größter Jitter-Beitrag heute).
Echtes 94-Bit-Sekunden+ns-Format (kein 16-bit-Wrap).

**Grenzen:** (1) **Keine direkte Peripherie-Triggerung** — der Compare liefert nur
GTSUCOMP→EVSYS und Interrupt; ADC/DAC/PWM nur **indirekt via EVSYS**. (2) **Nur 1
Compare-Slot** → mehrere/periodische Trigger per ISR-Reload. (3) **Capture = letzter
Frame**. (4) **Taktwurzel bleibt das Problem:** TSU läuft auf MCK = DFLL; der Servo
muss weiter ~1800 ppm wegregeln — die TSU verbessert nur Stellauflösung und Messung,
nicht die Drift selbst. (5) **Kein PPS-Pin**.

### 3.3 Synthese — konkrete Vorgehensweise
1. **Nominaluhr init:** GMAC-MCK ermitteln, Nominal-Increment ns/Takt = 1e9/f_MCK in
   `TI.CNS`(+`ACNS`/`NIT`/`TISUBN`); `TSH/TSL/TN` auf aktuelle NTP-Zeit.
2. **PI auf TSU abbilden:** Phase → einmalig `TA`; Rate → `TISUBN`-Anpassung
   (Δ = nominal × ppb/1e9; bei 60 MHz MCK ≈ **0,9 ppb/LSB**). `ntp_now_ns()` liest
   künftig `TSH/TSL/TN`. Der PI-Loop bleibt unverändert, nur die Aktuatoren wechseln.
3. **Frame-Timestamps:** im Harmony-GMAC-Treiber aktivieren, RX/TX-Capture-ISR; die
   t1…t4-Messung der `ntp_sync`-Logik auf HW-Timestamps umstellen (Mess-Jitter
   ~100 µs → low-µs).
4. **Fan-Out:** Ziel-T in `SCH:SCL:NSC`; bei Match GTSUCOMP → **EVSYS** → ADC/DAC/TCC
   (HW-Trigger zum NTP-Instant). PPS-Ersatz: GTSUCOMP → TC → GPIO. Periodisch: nächsten
   Wert im Compare-ISR nachladen.

### 3.4 Beurteilung
Stellauflösung (1 ns Phase, ~sub-ppb Rate) und SFD-genaue Timestamps liegen **weit
unter** dem 10–100-µs-Ziel; EVSYS-Trigger feuert mit **sub-µs**-HW-Jitter,
Interrupt-Pfad mit einigen µs. **Ziel klar erreichbar, low-µs realistisch** — *sofern*
die NTP-Übertragung über die Bridge so genau ist (heute hunderte µs Roundtrip-Jitter;
mit HW-Timestamps an beiden Enden → low-µs). **Aufwand mittel.** **Risiken:** Taktwurzel
(DFLL) ungelöst, nur 1 Compare, kein PPS-Pin, GMAC braucht aktive Ethernet-Taktdomäne.
Technisch die **sauberste HW-Uhr-Variante** (echter PTP-Zähler), real limitiert durch
NTP-Transport und DFLL-Wurzel — beide separat adressierbar.

---

## 4. Option B — Oszillator/PLL-disziplinierter Timer (XOSC0 + DPLL1)

### 4.1 Tiefenbohrung

**FDPLL200M (DPLL0/DPLL1) — Frequenzgleichung (DS §28.6.5 S.704):**

  f_DPLL = f_CKR × (LDR + 1 + LDRFRAC/32)

- `DPLLnRATIO.LDR[12:0]` — 13-bit Integer (DS §28.8.13 S.733).
- `DPLLnRATIO.LDRFRAC[4:0]` — **5 bit, Nenner 32** = feinste HW-Frequenzstufe
  (DS §28.2 S.696).

**Frequenz-Auflösung (die zentrale Zahl):** 1 LDRFRAC-Schritt = f_CKR/32 →
**Δf/f ≈ 1/(32 × Multiplikator)**. Also **fein nur bei kleiner f_CKR**:
- f_CKR = 12 MHz, →120 MHz (×10): 1 Schritt ≈ **3125 ppm** — viel zu grob.
- f_CKR ≈ 47 kHz (12 MHz÷256), →120 MHz (×2554): ≈ **12 ppm/LSB**.
- f_CKR = 32,768 kHz, →120 MHz (×3662): ≈ **8,5 ppm/LSB**.

**REFCLK & Eingangsgrenzen (DS §28.8.14 S.735, §28.2 S.696):**
`DPLLnCTRLB.REFCLK` = GCLK(0)/XOSC32(1)/**XOSC0(2)**/XOSC1(3). **Erlaubter
Referenzbereich 32 kHz…3,2 MHz.** ⇒ **XOSC0 12 MHz kann NICHT direkt** Referenz sein,
muss geteilt werden: `DPLLnCTRLB.DIV[10:0]`, f_DIV = f_XOSC/(2·(DIV+1)) (DS §28.6.5
S.707). Z. B. DIV=127 → 12e6/256 ≈ 46,875 kHz.

**On-the-fly-Update (entscheidend):** DPLLnRATIO darf bei aktivem DPLL geändert werden
(DS §28.6.5 S.707); **reine `LDRFRAC`-Änderungen löschen den LOCK NICHT** (DS S.708) —
ideal fürs feine Syntonisieren. `LTIME=0` (Auto-Lock) + GCLK_DPLLn_32K-Takt nötig.
Fractional-Mode hat „negative impact on jitter" (DS §28.6.5 S.704) — für eine
TC-Zeitbasis tolerierbar (mittelt sich).

**GCLK-Routing (DS §14, Table 14-4 S.154, S.157–158):** `GENCTRLn.SRC`:
XOSC0=0x0, **DPLL0=0x07, DPLL1=0x08**. Teiler `DIV`+`DIVSEL`. Peripheral-Channels:
**GCLK_TC0,TC1 = Index 9**; GCLK_DPLL1 = Index 2; GCLK_DPLLn_32K = Index 3.

**FREQM (DS §30):** misst f_MSR = (VALUE/REFNUM)·f_REF, **VALUE 24 bit** (S.770),
REFNUM 1…255 (S.764), Referenz muss langsamer als Messtakt sein (S.758) — zum
**Charakterisieren** von XOSC0/DPLL gegen XOSC32K geeignet.

### 4.2 Analyse
Frequenz-Disziplinierung ist der **richtige Hebel** (ein HW-nachführbar getakteter TC
kann triggern, die reine Software-Addition nicht). **Größter Nachteil:** DPLL0 nudgen
würde **CPU/Bus/alle Takte** mitverschieben → **dedizierte DPLL1** verwenden. **Auflösungslimit:**
~12 ppm/LSB (XOSC0÷256) = über 1 s ~12 µs Quantisierung — am oberen Zielrand; Rest in
**Software (s_rate_ppb)** oder per **Sigma-Delta-Dither** zwischen zwei LDRFRAC-Werten
unter ~1 ppm drücken. **Wurzelwechsel auf XOSC0 allein bringt am meisten:** MEMS-Quarz
~±20–50 ppm statt open-loop-DFLL ~1800 ppm → **~50× weniger Roh-Drift, ganz ohne PLL-Tuning.**

### 4.3 Synthese — konkrete Vorgehensweise
**Stufe (a) — Roh-Drift senken (Pflicht-Basismaßnahme, reine Clock-Init-Änderung):**
1. `OSCCTRL->XOSCCTRL[0]`: ENABLE/XTALEN, IMULT/IPTAT für 8–16 MHz (Table 28-7 S.725),
   auf `STATUS.XOSCRDY0` warten.
2. DPLL0 von XOSC0 referenzieren: REFCLK=XOSC0, DIV so dass f_CKR ≤ 3,2 MHz (z. B.
   DIV=1 → 3 MHz), LDR auf 120 MHz (LDR=39). → CPU/Bus/TC0 wurzeln im **Quarz**; die
   bestehende Software-NTP-Korrektur muss nur noch ~30 ppm statt ~1800 ppm wegregeln.

**Stufe (b) — disziplinierter Timing-TC über DPLL1 (für HW-Trigger an NTP-Instanten):**
3. **DPLL1** aus XOSC0, REFCLK=XOSC0, DIV=127 (→ ~47 kHz für ppm-Auflösung), `LTIME=0`,
   FILTER moderat; GCLK_DPLL1_32K-Lock-Takt (PCHCTRL Index 3) bereitstellen; LDR auf
   z. B. 96 MHz; auf LOCK warten.
4. Dedizierten GCLK (`SRC=DPLL1`) auf ein **32-bit-TC-Paar** (TC0+TC1, GCLK Index 9)
   routen, frei laufen lassen → **dieser Zählerstand IST die syntonisierte Zeit**.
5. **Per-Sync-Frequenz:** ppb → nächstes `LDRFRAC` (nur LDRFRAC schreiben → Lock bleibt),
   Sub-LSB-Rest in Software/Dither.
6. **Phase:** **nicht** über DPLL, sondern am TC: Grobsprung per COUNT-Reload, fein per
   **Compare-Retargeting** (NTP-Instant → aktueller Tickwert + konstanter Offset in CCx).
7. Optional: **FREQM** (MSR=DPLL1, REF=XOSC32K) zum Verifizieren nach jedem Nudge.

### 4.4 Beurteilung
**Stufe (a) allein:** Roh-Drift **~1800 ppm → ~±20–50 ppm** (Quarz über Temperatur),
kurzfristig < 1 ppm; Holdover über 1 s ~20–50 µs worst-case, bei stabiler Temperatur
< 1 µs → **bringt 10–100 µs schon in Reichweite, ohne jedes PLL-Tuning.** **Stufe (a)+(b):**
Frequenzfehler aktiv weggeregelt; limitierend wird die **NTP-Transportmessung** (~hunderte
µs), nicht der Oszillator. LDRFRAC-Granularität (~12 ppm/LSB) reicht mit Software-Feintrim.
**Aufwand:** (a) gering, **stark empfohlen als Erstmaßnahme**; (b) mittel–hoch (DPLL1 +
Lock-GCLK + 32-bit-TC + ppb→LDRFRAC-Regler + Compare-Retargeting + Dither). **Risiken:**
CPU-Kopplung (gebannt durch DPLL1-Nutzung), Fractional-Jitter, **Board-Risiko: ist XOSC0
ein Quarz (XIN/XOUT, XTALEN=1) oder ein MEMS/CMOS-Clock-Out (externer Takt, XTALEN=0)?
→ Curiosity-Ultra-Schaltplan prüfen.** Fallback ohne XOSC0: DPLL closed-loop gegen
XOSC32K (besser als DFLL, schlechtere Kurzzeitstabilität).

---

## 5. Option C — Event System (EVSYS) als Fan-Out

### 5.1 Tiefenbohrung

**Generator → Kanal → User (DS §31.1–31.5 S.771–778):** CPU-frei, ohne Bus/RAM-Last.
**119 Generatoren, 32 Kanäle, 67 User** (DS §31.2 S.771). Ein Kanal hat genau einen
Generator; ein Generator darf mehrere Kanäle speisen.

**Verdrahtung (DS §31.5.2.1 S.773, §31.7.8/§31.7.13 S.792/799):**
1. **User zuerst:** `EVSYS.USERm.CHANNEL = n+1` (0 = kein Kanal).
2. **Kanal:** `EVSYS.CHANNELn` mit `EVGEN[6:0]`, `PATH[1:0]`, `EDGSEL[1:0]`.
3. Generator freigeben (`TC.EVCTRL.MCEOx` / `TCC.EVCTRL.MCEOx`), User-Eingang freigeben
   (`ADC.EVCTRL.STARTEI` …).
**Software-Event:** `EVSYS.SWEVT.CHANNELn` (DS §31.7.2 S.786) — für die Software-Variante.

**Pfade & Latenz (DS §31.5.2.8 S.775):** Async = nur Gatter-Delay (~ns, keine
Edge-Detect/IRQ); Sync = +1 GCLK_EVSYS; Resync = +3; plus ≤3 Peripherie-Takte.
Sync/Resync nur Kanal **0–11** (DS §31.4.3 S.772).

**Timer-Compare → Event:** TC erzeugt OVF und **Match MCx** (`EVCTRL.MCEOx`,
DS §48.6.6 S.1569); Match bei `COUNT==CCx` (DS §48.6.2.6.1 S.1560), Double-Buffer via
`CCBUFx` (DS §48.6.2.7 S.1562). TCC analog, bis 6 CC (DS §49.6.5.3 S.1665). Generator-IDs
z. B. `TC0_MC0`=0x4A, `TCC0_MC0`=0x2C (DS §31.7.8 S.793).

**Event-User — Eintrittspunkte:**
- **ADC START** (User m=55 ADC0): `ADC.EVCTRL.STARTEI=1` (DS §45.6.6 S.1455). Sampling
  beginnt **erst beim Trigger** → Sampling-Instant = das Event. **Nur asynchroner Pfad**
  (DS §45.6.6 S.1455).
- **DAC START** (m=61/62): `DAC.EVCTRL.STARTEIx=1` (DS §47.6.6 S.1523), **Pfad nur A**;
  bei Event-Start `DATABUFx` schreiben (nicht `DATAx`).
- **GPIO/Pin:** (1) **direkt per TC/TCC-Waveform-Out (WO)** im MFRQ/NFRQ-Toggle bei
  Match (DS §48.6.2.6.1 S.1560) — deterministischste Variante, kein EVSYS nötig;
  (2) **PORT-Event** SET/CLEAR/TOGGLE (User m=1..4, DS §32.2 S.801); (3) **CCL→Pin**
  (DS §41 S.1273, nur 1-Takt-Strobe).
- **PWM:** ist die disziplinierte Zeitbasis die **TCC** selbst, sind alle PWM-Flanken
  inhärent zeit-aligned (DS §49.6.2.5.5 S.1642) — kein EVSYS nötig.

**Kanal-Budget:** 32 Kanäle, 8×TC (à 2 CC) + 5×TCC (à bis 6 CC) → reichlich unabhängige
Trigger-Quellen. Async-User (ADC/DAC/PORT/CCL) nutzen jeden der 32 Kanäle.

### 5.2 Analyse
Der Pfad Compare → EVSYS → User läuft **vollständig in Hardware**: nur der Zählerstand
bestimmt den Auslösezeitpunkt, **kein Interrupt-Jitter**. **Jitter-Boden:** Async ~ns;
Match-Quantisierung = 1 Timer-Taktperiode (~16 ns @ 60 MHz) → **nicht** der Engpass.
**Limitation der „Software-Clock + Software-geladenes Compare"-Minimalvariante:**
**Reload-Zwang** pro Event (Single-Shot); **Rate-Drift** zwischen Laden und Feuern (bei
1800 ppm und 100 µs Vorlauf ~0,18 µs, unkritisch; bei ms–s wächst es linear);
**Wraparound-Race** (CC zu spät geschrieben → 1 Periode zu spät, DS §48.6.2.7 S.1563) →
Vorlauf + Double-Buffer. Reines `SWEVT` ohne Timer-Compare bringt CPU-Jitter zurück.

### 5.3 Synthese — konkrete Vorgehensweise
**A) Kontinuierlich (Compare→EVSYS→User):** TC/TCC `CTRLA`/`WAVE` setzen → `EVCTRL.MCEO0=1`
→ T in `CCx`/`CCBUFx`; `EVSYS.USERm.CHANNEL=n+1`, `EVSYS.CHANNELn.EVGEN=TCx_MC0`,
`PATH=ASYNC`; User-EI freigeben (ADC `STARTEI`, DAC `STARTEI0`+`DATABUF0`, PORT TOGGLE).
**B) GPIO/PWM direkt:** Timer-WO-Pin (MFRQ/NFRQ-Toggle) bzw. TCC-NPWM (`PER`/`CCx`) — ist
die Zeitbasis die TCC, ist die PWM automatisch syntonisiert.
**C) Minimal („SW rechnet CC, EVSYS feuert"):** freilaufender TC als HW-Spiegel der
Software-Zeit; pro NTP-Instant `CC = (T−t_jetzt)·f_timer + COUNT_jetzt` mit Vorlauf in
`CCBUFx`; EVSYS verdrahtet → Match feuert; danach nächstes T nachladen. **Geeignet für
diskrete, nicht zu schnelle Trigger** (10–100 µs), **nicht** für Dauer-PWM/strenge Syntonisierung.

### 5.4 Beurteilung
**EVSYS-Pfad-Jitter** ns-Bereich → **nicht limitierend**; **32 Kanäle** = reichlich
synchrone I/O. **Aufwand gering–mittel** (reine Registerkonfig; Reload-Management ist die
Hauptarbeit). **Was EVSYS NICHT löst:** es verteilt nur einen *vorhandenen* Zeitpunkt — die
10–100-µs-Genauigkeit hängt einzig an der **Disziplinierung der Zeitbasis**. Auf dem
open-loop DFLL (~1800 ppm) verfehlt ein „in 1 s feuern"-Trigger das Ziel um ~1,8 ms. ⇒
**Option C braucht zwingend Option A oder B.** Mit disziplinierter Zeitbasis ist C der
ideale, CPU-freie Fan-Out (ns-Zusatzjitter).

---

## 6. Vergleich der drei Möglichkeiten

| Kriterium | **A — GMAC-TSU** | **B — XOSC0 + DPLL1-TC** | **C — EVSYS** |
|---|---|---|---|
| **Rolle** | disziplinierte HW-Uhr **+ HW-Frame-Timestamps** | disziplinierte HW-Zeitbasis (Taktquelle) | **Fan-Out** (Trigger-Verteilung) |
| **Phasen-Stellglied** | `TA` (1 ns, atomar) | TC-COUNT-Reload / Compare-Retarget | — |
| **Frequenz-Stellglied** | `TI`/`TISUBN` (~0,9 ppb/LSB, 15 fs) | DPLL1 `LDRFRAC` (~12 ppm/LSB) + SW-Sub-LSB | — |
| **Roh-Drift behoben?** | **nein** (MCK bleibt DFLL) | **ja** — Stufe (a): XOSC0 → ~30 ppm | nein |
| **HW-Frame-Timestamps** | **ja** (SFD-genau, TX/RX) | nein | nein |
| **Peripherie-Trigger** | nur via EVSYS (GTSUCOMP) | TC-Compare → EVSYS / WO-Pin | **das ist seine Funktion** |
| **Compare/Trigger-Quellen** | **1 Slot** | viele (TC/TCC CCx) | 32 Kanäle |
| **Aufwand** | mittel | (a) gering / (b) mittel–hoch | gering |
| **Erreichbar (HW)** | sub-µs | (a) 10–100 µs sofort / (b) < µs | ns (Pfad selbst) |
| **Hauptlimit** | MCK-Wurzel (DFLL), 1 Compare, kein PPS-Pin | LDRFRAC-Quantisierung, DPLL-Jitter, Board-XOSC0? | braucht A **oder** B |

### 6.1 Wie die drei zusammenspielen
- **A und B sind Alternativen für die disziplinierte Uhr** — beide liefern einen
  Hardware-Zähler, dessen Stand = synchronisierte Zeit.
- **C ist kein Konkurrent**, sondern der **Verteilmechanismus**, den **A und B beide
  brauchen**, um ADC/DAC/GPIO/PWM zu treiben. Allein bringt C keine Genauigkeit.
- **B-Stufe (a) (XOSC0-Wurzel) ist orthogonal und nützt allen drei**: A's TSU läuft auf
  MCK; ohne XOSC0 muss A's Servo 1800 ppm wegregeln, mit XOSC0 nur ~30 ppm. C's Trigger
  driften ohne XOSC0 zwischen den Syncs.

### 6.2 Optimale Kombination (Empfehlung)
```
B(a) XOSC0 als Taktwurzel      →   A  GMAC-TSU als PTP-Uhr (+ HW-Timestamps)
   (Roh-Drift ~50× runter)          (Phase=TA, Rate=TI/TISUBN, t1..t4 in HW)
                                          │  GTSUCOMP
                                          ▼
                          C  EVSYS  →  ADC / DAC / GPIO / PWM (zum NTP-Instant)
```
Das ist der **PTP-Standardweg** und passt zur Richtung des `net_10base_t1s`-Projekts.

> **Weiterführend (Option B-b + C konkret):** die ausgearbeitete Umsetzung mit
> Registersequenzen, Errata-Check und Diagrammen steht in
> [HW_TIMEBASE_B_C_IMPLEMENTATION.md](HW_TIMEBASE_B_C_IMPLEMENTATION.md); der
> schrittweise, je Schritt getestete Bring-up in
> [HW_TIMEBASE_BRINGUP_STEPS.md](HW_TIMEBASE_BRINGUP_STEPS.md).

### 6.3 Roadmap nach Aufwand/Wirkung
1. **Sofort — B-Stufe (a): XOSC0 als Taktwurzel.** Reine Clock-Init-Änderung in
   `plib_clock.c`, größter Gewinn/Aufwand: Roh-Drift ~1800 ppm → ~30 ppm, bringt 10–100 µs
   in Reichweite und verbessert **alles** (auch A und C). **Vorab Schaltplan prüfen**
   (XOSC0 Quarz vs. externer Takt).
2. **Für getriggerte Peripherie — C: EVSYS verdrahten** (TC/TCC-Compare → ADC/DAC/PWM/GPIO).
3. **Für die beste Uhr + Ethernet-HW-Timestamps (PTP-Pfad) — A: GMAC-TSU** als
   disziplinierte Uhr; Servo auf `TA`/`TI`; NTP-Frames HW-stempeln.
4. **Alternative ohne GMAC — B-Stufe (b): DPLL1-syntonisierter 32-bit-TC** als disziplinierte
   Zeitbasis + C, wenn Ethernet-HW-Timestamping nicht gebraucht wird (rein lokale getriggerte I/O).

### 6.4 Gemeinsame Obergrenze
Für **lokale** getriggerte I/O (ADC/DAC/PWM/GPIO zum NTP-Instant) erreichen alle Wege mit
disziplinierter Zeitbasis das Ziel mit Reserve (HW-Jitter ns–µs). Die reale Obergrenze ist
die **Genauigkeit der NTP-Zeitübertragung über die T1S-Bridge** (heute ~hunderte µs Software-
Roundtrip). Echte **Sub-10-µs-Leitungssync** braucht **HW-Frame-Timestamping an beiden Enden
(Option A bzw. PTP)** — das ist der Pfad, den `net_10base_t1s` verfolgt.
