# Verteiltes synchrones ADC-Sampling über 10BASE-T1S — Konzept & Entwicklungsleitfaden

> **Zweck dieses Dokuments.** Es beschreibt das Gesamtkonzept für ein System, in dem
> mehrere gleichartige ATSAME54-Knoten an einem 10BASE-T1S-Bus **gleichzeitig und
> knotenübergreifend synchron** ADC-Daten erfassen und als Datenströme zu einem
> Master senden, der sie ohne zeitliche Neuinterpolation korrelieren kann.
> Es ist so gegliedert, dass sich daraus eine **stufenweise Entwicklungsanweisung**
> ableiten lässt: jede Stufe hat ein Ziel, ein Abnahmekriterium und benennt ihre
> Abhängigkeiten.
>
> **Statusangaben.** Bausteine sind markiert als
> **[vorhanden]** (im Code real implementiert),
> **[konzept]** (entworfen, noch nicht umgesetzt) oder
> **[zu verifizieren]** (am realen Chip/Bus noch nicht nachgewiesen).
>
> **Reifegrad (ehrlich).** Säule 1 ist vorhanden, Säulen 2–4 sind Konzept, und
> **kein einziger Genauigkeitswert ist am verteilten Aufbau gemessen** — alle Zahlen
> sind abgeleitet oder aus Einzelknoten-/Leerlauf-Tests übernommen. Dieses Dokument
> ist eine **begründete Hypothese, kein verifiziertes Design**. Die kritische
> Risikobetrachtung ist in dieses Dokument eingearbeitet: risikobehaftete Aussagen
> tragen einen Inline-Hinweis **⚠(ID)**, der auf die Risiko-Matrix in §11 verweist.
>
> **Zwei Pflicht-Sicherungen, die das ursprüngliche Konzept nicht hatte** und die als
> verbindlich gelten (Begründung in §11):
> 1. **Master-seitiger Index-Konsistenz-Check** gegen stillen Index-Versatz (⚠D1).
>    Ohne ihn ist das System diagnose-*fähig*, aber nicht diagnose-*verlässlich*.
> 2. **Früher Hardware-Nachweis von TCC0-PERBUF on-the-fly** (⚠F1), bevor Schleife B
>    gebaut wird.

---

## 1. Zielbild

Ein 10BASE-T1S-Bus trägt einen **Koordinator (Master)** und bis zu **N Follower**
(Zielgröße N = 7). Jeder Follower tastet einen analogen Kanal mit **8 kHz** ab
(12-bit-Wandlung, in 16 bit kodiert) und streamt die Samples zum Master.

**Kernanforderung:** Sample mit Index *k* bedeutet auf **jedem** Knoten denselben
Zeitpunkt — kontinuierlich, dauerhaft, ohne dass der Master die Ströme zeitlich
neu ausrichten muss. Die Synchronität wird **an der Quelle** hergestellt, nicht
nachträglich beim Empfänger.

**Einsatzklasse:** Diagnose / Analyse / Korrelation (latenz**un**kritisch).
Echtzeit-Regelung über den Bus ist *kein* Ziel und mit diesem Medium auch nicht
darstellbar (siehe §10).

### 1.1 Was erreichbar ist

| Größe | Wert | Quelle |
| --- | --- | --- |
| Abtastrate je Knoten | 8 kHz (125 µs/Sample) | Vorgabe |
| Knotenzahl | bis PLCA-Node-Count − 1 (Ziel 7) | Bus |
| Knotenübergr. Synchronität (Mittelwert) | wenige µs (Software-NTP-begrenzt) | [konzept] ⚠B1 |
| — momentaner Pro-Sample-Fehler | größer, unter Last schlechter | ⚠B1 ⚠B2 |
| Nutzdatenrate gesamt (7 Knoten) | ~896 kbit/s netto | Rechnung |
| Buslast mit Blockung (nur Daten) | ~1 Mbit/s (~10 % von 10 Mbit/s) | Rechnung ⚠E1 |

> **Wichtige Differenzierung (⚠B1):** Die „wenige µs" sind der **Erwartungswert** des
> Sync-Offsets über Mittelung, **keine pro Sample garantierte Schranke**. Für
> mittelnde Korrelation über lange Fenster tragfähig; für **ereignisgenaue**
> Koinzidenz (Flanke, Transient zweier Kanäle) zählt der momentane Fehler, der
> schlechter ist — und unter Datenlast zusätzlich wächst (⚠B2). Das System ist für
> die mittelnde Klasse ausgelegt.

### 1.2 Was *nicht* erreichbar ist

- **Sub-µs-Synchronität** — bräuchte Hardware-Timestamping (PTP); anderes Projekt.
- **Latenzkritische Regelung** über den Bus bei 8 kHz × 7 — scheitert an
  Frame-Overhead und PLCA-Slot-Rate (siehe §10).

---

## 2. Architektur-Überblick

Das System besteht aus vier funktionalen Säulen, die getrennt entwickelbar sind:

```
┌─────────────────────────────────────────────────────────────────┐
│  SÄULE 1: Lokale Hardware-Zeitbasis (pro Knoten)                  │
│  TC2/TC3 als disziplinierte NTP-Uhr, getrieben XOSC1→DPLL1→GEN5   │
│  [vorhanden]                                                      │
└─────────────────────────────────────────────────────────────────┘
        │ liefert ntp_now_ns(), s_offset_ns, s_rate_ppb
        ▼
┌─────────────────────────────────────────────────────────────────┐
│  SÄULE 2: Uhr-Synchronisation über N Knoten (Schleife A)         │
│  Master misst je Follower Offset/Delay (125 ms), PI-Disziplin    │
│  [konzept, 1:1-Variante vorhanden]                               │
└─────────────────────────────────────────────────────────────────┘
        │ hält alle Uhren gleich (~µs)
        ▼
┌─────────────────────────────────────────────────────────────────┐
│  SÄULE 3: Synchroner Sampletakt (Schleife B + gem. Start)        │
│  TCC0 erzeugt 8 kHz, Perioden-Dithering führt Takt nach,         │
│  Broadcast-GO verankert Sample-Index 0                           │
│  [konzept]                                                       │
└─────────────────────────────────────────────────────────────────┘
        │ index-synchrone Samples, per DMA im Ringpuffer
        ▼
┌─────────────────────────────────────────────────────────────────┐
│  SÄULE 4: Transport & Scheduling                                 │
│  Eigenes UDP-Protokoll, konfigurierbare Blockgröße,             │
│  uhr-gestaffeltes Sende-Timing hält Sync-Fenster frei            │
│  [konzept]                                                       │
└─────────────────────────────────────────────────────────────────┘
        │ UDP-Ströme (routbar über Bridge)
        ▼
   Master / entfernter Host: sortiert nach (node_id, sample_index)
```

### 2.1 Das Trennprinzip — und seine Grenze

Zwei Regelkreise, die nicht über denselben **Transportweg** laufen:

- **Schleife A** (Uhr) läuft über kleine, latenzarme **Sync-Pakete** alle 125 ms.
- **Schleife B** (Sampletakt) läuft lokal im Knoten, liest nur den geglätteten
  Frequenzwert aus A.
- Der **Datenstrom** trägt seine Synchronität *intrinsisch* über den Sample-Index
  und ist damit von Transport-Latenz entkoppelt.

Daraus folgt die wichtigste Architektur-Regel: **Datenverkehr darf den
Sync-Verkehr nicht verdrängen.** Sync-Pakete bleiben klein und ungeblockt;
Datenpakete werden geblockt und zeitlich gestaffelt (§7).

> **Grenze der „Entkopplung" (⚠C1).** Die Trennung gilt für den *Transportweg*, **nicht
> für die Information**: B speist sich aus `s_rate_ppb`, dem I-Ausgang von A. Jede
> Störung in A (Jitter, Ausreißer, kurzer Sync-Verlust) propagiert direkt in den
> Sampletakt. A und B sind also **regelungstechnisch gekoppelt**, nur transportseitig
> getrennt. Die Gesamt-Schleifendynamik A→`s_rate_ppb`→B ist zu analysieren, bevor
> dem Feldtest vertraut wird (⚠C2) — zwei kaskadierte Integratoren (`s_rate_ppb` in A,
> `per_resid` in B) können ohne getrennte Zeitkonstanten überschwingen.

### 2.2 Medium-Eigenschaften, die das Design prägen

- 10BASE-T1S ist **Half-Duplex** auf einem **geteilten** Medium (Multidrop).
  Senden und Empfangen teilen sich die 10 Mbit/s; zu jedem Zeitpunkt sendet nur
  einer.
- **PLCA** vergibt Sendegelegenheiten reihum, ist aber **kein TDMA** — die
  Transmit Opportunities sind variabel. Zeitgesteuertes Scheduling auf
  Anwendungsebene (§7) ergibt daher eine *statistische*, keine harte Trennung.
- Konsequenz: Der Engpass ist nicht die Bitrate, sondern die **PLCA-Slot-/
  Frame-Rate**. Wenige große Frames sind besser als viele kleine.

---

## 3. Säule 1 — Lokale Hardware-Zeitbasis [vorhanden]

Jeder Knoten besitzt eine disziplinierte Hardware-Uhr. Diese Säule ist im
Firmware-Code bereits implementiert (`hwclk_cli.c`, `ntp_sync.c`).

### 3.1 Taktkette

```
XOSC1 (12 MHz ext. MEMS)
  → DPLL1 (÷182 → 32.79 kHz, ×5856 → ~192 MHz)
  → GCLK GEN5 (÷2 → 96 MHz)
  → TC2/TC3   : 64-bit-Zähler = Roh-Zeitbasis
  → TCC0      : derselbe 96-MHz-Takt (für Sampletakt, Säule 3)
```

TC2 ist 32-bit, per TC3/Overflow-ISR auf 64 bit Software-erweitert.

### 3.2 Die Uhr-Formel

```
ntp_now_ns() = raw + s_offset_ns + rate_held(raw)

raw          = tc2_read64() * 125 / 12      (Ticks → ns bei 96 MHz)
rate_held(r) = (r - s_lastSyncRaw)/1000 * s_rate_ppb / 1e6
```

- `s_offset_ns` — Phasen-Offset (P-Anteil der Disziplinierung)
- `s_rate_ppb`  — Frequenzkorrektur in ppb (I-Anteil)

### 3.3 Wichtige Einschränkung (Silizium Rev D)

Die Frequenzkorrektur sitzt in **Software** (`s_rate_ppb`), nicht in der Hardware-
PLL: Jeder On-the-fly-Schreibzugriff auf `DPLLRATIO` stoppt den DPLL1-Ausgang
dauerhaft. Der physische 96-MHz-Takt läuft daher mit festem Restfehler (~+28 ppm);
korrigiert wird nur rechnerisch im Lesepfad. **Dies ist der Grund, warum Säule 3
den Sampletakt per Software-Dithering nachführen muss statt per Takt-Tuning.**

> **Einordnung (⚠F2).** Die gesamte Dither-Konstruktion (§5.3) ist eine
> **Software-Ersatzlösung um einen Silizium-Bug herum**. Auf einem fehlerfreien
> Stepping wäre HW-Frequenzdisziplinierung der robustere Weg und mehrere
> Regelungs-Risiken (⚠C2/⚠C4/⚠F1) entfielen. Die Komplexität ist also teils
> **plattform-induziert**, nicht inhärent — relevant bei einer etwaigen
> Hardware-Revisions-Entscheidung.

### 3.4 Abnahmekriterium dieser Säule

`hwclk`-CLI zeigt: XOSC1 läuft, DPLL1 LOCK, TC2 zählt 64-bit, `ntp_now_ns()`
liefert plausible Zeit. (Im Bring-up bereits als PASS dokumentiert.)

---

## 4. Säule 2 — Uhr-Synchronisation über N Knoten (Schleife A)

Bringt die NTP-Uhr jedes Followers auf die Master-Zeit. Die 1:1-Variante
(PC ↔ ein Knoten) ist **[vorhanden]**; die Erweiterung auf 1:N **[konzept]**.

### 4.1 Messung (Vier-Zeitstempel, je Knoten, alle 125 ms)

```
Master: t1 = ntp_now_ns(); sende REQUEST(dstMac=k)
Knoten k: t2 = ntp_now_ns() bei Empfang
          t3 = ntp_now_ns() vor Antwort; sende REPLY(t1,t2,t3)
Master: t4 = ntp_now_ns() bei Empfang

delay  = (t4 - t1) - (t3 - t2)
offset = ((t2 - t1) + (t3 - t4)) / 2
```

### 4.2 Robuster Schätzer

Pro Knoten **R Runden** messen; die Runden mit **kleinstem delay** auswählen,
davon den **Median(offset)** nehmen. Filtert PLCA-/Stack-Ausreißer. (Logik aus dem
vorhandenen PC-Tool übernehmen.)

> **Decke der Genauigkeit (⚠B2).** t2/t3 werden im **UDP-Task** gestempelt (nicht in
> ISR/PHY). Die Stempelunsicherheit ist damit die Superloop-/Task-Latenz und ist
> **nicht konstant**: Unter Datenlast (Streaming läuft, DMA-Blöcke, ADC-ISR) ist sie
> größer als im Leerlauf, in dem das ursprüngliche NTP getestet wurde. Henne-Ei:
> gerade wenn viele Samples fließen — worauf es ankommt — ist der Sync am
> ungenauesten. Die Leerlauf-Genauigkeitszahlen dürfen **nicht** ungeprüft auf den
> Volllastbetrieb übertragen werden; der reale Wert ist unter Last zu messen (⚠G1).

### 4.3 Korrektur — PI-Regler im Follower [vorhanden]

```
# 1) bisher aufgelaufene Rate in die Phase einfrieren (Stetigkeit)
s_offset_ns += rate_held(raw)
s_lastSyncRaw = raw

# 2) P-Anteil: Phase sofort voll korrigieren  (Kp = 1)
s_offset_ns += adjust

# 3) I-Anteil: Frequenz nachführen, erster Sync ausgeschlossen
if (synced and interval_us > 0):
    drift_ppb  = adjust * 1e6 / interval_us
    s_rate_ppb += drift_ppb / KI_DEN          # KI_DEN = 4
```

Nach ~2 Syncs ist die Uhr „gelockt". Der große erste Adjust (Epochensprung) speist
den I-Anteil nicht.

### 4.4 Der N-Knoten-Zusatz [konzept]

Master führt **pro Knoten** (MAC-adressiert) eigenen Offset/Delay/Zustand und
fährt eine **nicht-blockierende Round-Robin-State-Machine**:

```
für aktuellen Knoten k:
  IDLE → SEND_REQ → WAIT_REPLY(timeout) → akkumuliere(offset,delay)
       → nach R Runden: robuster Offset → SEND_SET → WAIT_ACK
       → k = (k+1) mod N
```

Jeder Knoten wird im 125-ms-Zyklus einmal bedient.

### 4.5 Abnahmekriterium

Zwei Knoten + Master: Pin-Toggle aus `ntp_now_ns()` auf allen Knoten; am
Logic-Analyzer fallen die Flanken nach Sync-Start zusammen und driften bei
Sync-Abschaltung sichtbar auseinander.

---

## 5. Säule 3 — Synchroner Sampletakt (gemeinsamer Start + Schleife B) [konzept]

Erzeugt auf jedem Knoten einen 8-kHz-Sampletakt, der (a) auf allen Knoten zum
gleichen NTP-Zeitpunkt startet und (b) im Mittel exakt gleich schnell läuft.

### 5.1 Gemeinsamer Start — Index-Anker

```
Master: X_ns = ntp_now_ns() + Vorlauf            # z.B. +500 ms
        broadcast GO(start_ns = X_ns, start_index = 0)

Knoten: target_tick = ns_to_ticks(X_ns - s_offset_ns - rate_held@X)
        arme Hardware-Compare(TC2) → startet TCC0-Sampletakt bei target_tick
        sample_k = 0
```

Da alle Uhren synchron sind, fällt `target_tick` auf allen Knoten auf denselben
realen Moment (±µs). Ab da gilt: Index 0 = Zeit X auf jedem Knoten.

**Späterer Einstieg** (Knoten kommt dazu):
```
sample_k_join = (ntp_now_ns() - X_ns) / 125000   # 125 µs je Sample
```

> **Vorbedingung für den Späteinstieg (⚠D4).** Die Rückrechnung setzt voraus, dass
> `ntp_now_ns()` beim Einstieg **bereits gelockt** ist. Ein frisch gebooteter Knoten
> braucht ≥2 Syncs zum Lock (§4.3); rechnet er vorher, ist der Einstiegsindex falsch
> und er steigt **still auf der falschen Achse** ein (⚠D1). Regel: Sampling-Einstieg
> erst **nach bestätigtem Lock** freigeben.
>
> **GO-Zustellung (⚠D1).** Der GO-Broadcast ist UDP (kein ACK im Grundkonzept). Ein
> Knoten, der GO **verpasst**, startet nie oder mit falschem Nullpunkt. GO daher mit
> **ACK + Wiederholung** absichern; ohne Quittung ist der gemeinsame Anker nicht
> garantiert.

### 5.2 Das Drift-Problem

Jeder Knoten erzeugt 8 kHz aus seinem eigenen Takt mit eigenem ppm-Rest. Ohne
Gegenmaßnahme verschieben sich die Index-Achsen: schon **~1 ppm** Differenz ergibt
nach ~8 s einen vollen Sample Versatz. Der gemeinsame Start allein genügt also
**nicht** — die Rate muss laufend nachgeführt werden.

> **Der ppm-Rest ist nicht statisch (⚠A2).** Das ~+28-ppm sind ein Momentanwert; der
> MEMS-Oszillator driftet über **Temperatur und Alterung** um mehrere ppm. Bei 7
> räumlich verteilten Knoten mit unterschiedlicher Eigenerwärmung driften die Werte
> **unterschiedlich und zeitvariabel**. Schleife B muss also einen *wandernden* Offset
> nachführen, und ihre Nachführbandbreite muss schnell genug gegen die thermische
> Drift sein — das steht im direkten Konflikt zur Glättung gegen Sync-Jitter (⚠C3).

### 5.3 Schleife B — Perioden-Dithering (Sigma-Delta)

Ideale Sampleperiode in Takten (nicht ganzzahlig):

```
per_ideal = 12000 * (1 + s_rate_ppb / 1e9)        # z.B. 12000.336 bei +28 ppm
```

Ein Hardware-Timer kann nur ganze Perioden. Lösung: zwischen 12000 und 12001 so
dithern, dass der **Mittelwert** exakt `per_ideal` ist:

```
on SAMPLE_TIMER_RELOAD (~8000/s):
    per_resid += frac(per_ideal)              # z.B. += 0.336
    if per_resid >= 1.0:
        per_this  = 12001
        per_resid -= 1.0
    else:
        per_this  = 12000
    set_next_period(per_this)                 # TCC0 PERBUF
    sample_k++
```

- Mittlere Periode = `per_ideal` → mittlere Rate = Master-Rate.
- Momentaner Jitter = ±1 Takt = **±10 ns** → drei Größenordnungen unter 125 µs,
  für Korrelation unsichtbar.
- **Lückenlos**: es wird kein Sample eingefügt/verworfen, nur die Periodenlänge
  variiert. Index bleibt monoton.

> **Achtung Spektrum (⚠C4).** Der **Amplituden**wert (±10 ns) ist klein, aber
> Bresenham-/Sigma-Delta-Dithering erzeugt **periodische Muster (Spurious Tones)**,
> kein weißes Rauschen. Bei Spektralanalyse der ADC-Daten kann das diskrete Störlinien
> einbringen, deren Lage von `frac(per_ideal)` abhängt. Für ein System, dessen Zweck
> Analyse/Korrelation ist, ist die **spektrale Struktur** des Takt-Jitters potenziell
> schädlicher als der kleine Amplitudenwert nahelegt. Gegenmittel: rauschförmiges
> Dithering (zufälliger Schwellwert statt fester Bresenham-Akkumulation). Vor
> Produktiveinsatz das Jitter-Spektrum per FFT prüfen.

### 5.4 Kopplung A → B

`s_rate_ppb` wird alle 125 ms von Schleife A aufgefrischt; B liest den jeweils
aktuellen, **bereits geglätteten** (I-Anteil, /4) Wert. **Niemals den rohen
Einzel-Offset in B verwenden** — sonst überträgt sich der µs-Sync-Jitter in die
Sample-Rate. Bei unruhigem Bus ggf. zusätzlichen Tiefpass auf `s_rate_ppb`
(Preis: langsameres Einrasten).

### 5.5 Restfehler-Bilanz

```
(1) Sync-Offset-Jitter     ~µs       dominierend, nicht durch B reduzierbar
(2) Holdover je 125 ms      ~0.1 µs   (1 ppm Rest × 125 ms), wird je Sync genullt
(3) Dither-Jitter           ±10 ns    mittelwertfrei (aber strukturiert, ⚠C4)
                            --------
   knotenübergreifend ≈ (1), wenige µs, << 1 Sample bei 8 kHz
```

Da (2) und (3) sich **nicht aufakkumulieren**, hält die Index-Synchronität
dauerhaft. Der slave↔slave-Bias (gemeinsamer Master-Pfad-Fehler) hebt sich
zwischen zwei Knoten weitgehend auf → realer Wert am unteren Ende von (1).

> **Diese Bilanz ist optimistisch (⚠B4 ⚠A3 ⚠G3).** Drei Einschränkungen:
> - **(2) setzt 1 ppm Restunsicherheit voraus** — eine Setzung, nicht belegt. Bei
>   verrauschtem PLCA-Sync (⚠B2) sind 5–10 ppm plausibel → Holdover 0.6–1.25 µs je
>   Intervall statt 0.1 µs. Immer noch < 1 Sample, aber nicht „vernachlässigbar".
> - **Bias-Aufhebung nur teilweise (⚠A3):** Nur der *symmetrische* Master-Pfad-Anteil
>   hebt sich auf. **Positionsabhängige** PLCA-Slot-Latenzen je Knoten bleiben als
>   systematischer Offset stehen.
> - **Kein Worst-Case-Stackup:** Obige Bilanz bewertet jede Quelle isoliert als klein.
>   Ehrlicher wäre die **Summe** unter pessimistischen Annahmen gegen das
>   Sample-Intervall — dieser Stackup ist vor dem Feldtest zu rechnen (⚠G1).

### 5.6 Hardware-Pfad

```
TCC0 (NPWM @ GEN5 96 MHz, PER≈11999) → Event → ADC0_START
ADC0 RESRDY → DMA → Ringpuffer im RAM (CPU-frei)
```

### 5.7 Abnahmekriterium

Zwei Knoten, gemeinsamer Start, Dauerlauf über Minuten: Sample-Index k auf beiden
Knoten bleibt zeitlich deckungsgleich (am Scope/über Referenzsignal geprüft),
keine kumulative Drift.

### 5.8 Zu verifizieren [zu verifizieren]

- **TCC0-PERBUF on-the-fly**: ob die Periode pro Zyklus glitch-frei aus dem
  Pufferregister übernommen wird (Errata zu TCC-Buffer-Updates beachten). Vorab am
  Scope nachweisen — dies ist der eine Punkt, der am Silizium scheitern könnte.
- **Belastbarkeit von `s_rate_ppb`** bei verrauschtem PLCA-Sync (Tuning
  Glättung ↔ Einrastzeit).

---

## 6. Säule 4a — Transport-Protokoll [konzept]

### 6.1 Wahl: eigenes UDP-Protokoll

- **UDP/IP** (nicht reines L2), damit der Strom über die Bridge in ein anderes
  Netz/zu einem entfernten Host **routbar** bleibt.
- **Eigenes Payload-Format** (kein RTP), um Overhead und MCU-Footprint zu
  minimieren. RTP wurde verworfen: sein Sync-Mechanismus (RTCP→Wallclock) würde
  empfängerseitige Interpolation verlangen — genau das, was vermieden werden soll.

### 6.2 Paketkopf (Vorschlag)

| Feld | Bytes | Zweck |
| --- | --- | --- |
| `version/type` | 1 | Formatkennung |
| `node_id` | 1 | welcher der N Kanäle |
| `sample_count` | 2 | Samples in diesem Paket (Blockgröße variabel) |
| `first_index` | 8 | **globaler** Sample-Index des ersten Samples — der Synchronitäts-Anker |
| `samples[]` | 2·count | 12-bit-Wert in 16 bit |

Der `first_index` trägt die knotenübergreifende Gleichzeitigkeit. Eine Lücke im
Index ist zugleich die Verlusterkennung — eine separate Sequenznummer ist nicht
nötig.

> **Index nur sinnvoll relativ zum Anker (⚠B3 ⚠D1).** Der 64-bit-Index wrappt nie,
> ist aber nur gültig **relativ zum gemeinsamen `start_index`**. Zwei kritische
> Stellen, an denen er **still falsch** werden kann:
> - **DMA-Überlauf:** Werden Samples verworfen, ohne den Index-Zähler entsprechend zu
>   erhöhen, ist der Strom ab da verschoben — formal lückenlos, inhaltlich falsch. Der
>   Index-Zähler **muss** an die physisch erzeugten Samples gekoppelt sein, nicht an
>   die gesendeten.
> - **Reboot/Re-Sync** mit falschem Nullpunkt (⚠D4). Beides liefert einen gültig
>   aussehenden, aber falsch verankerten Strom. → Master-Konsistenz-Check (§8.1)
>   verpflichtend.

### 6.3 Bandbreite

```
Nutzdaten je Knoten : 8000 × 2 B = 16 kB/s = 128 kbit/s
7 Knoten netto      : 896 kbit/s

Mit Blockung (Beispiel 250 Samples/Paket):
  Overhead UDP+IP+Eth ≈ 66 B + Header ~12 B auf 500 B Payload → ~13 %
  → ~1.0 Mbit/s gesamt, ~32 Frames/s/Knoten, 7×32 ≈ 224 Frames/s

Mit großen Blöcken (≈700 Samples, nahe MTU):
  Overhead < 5 %, ~0.9 Mbit/s, ~77 Frames/s gesamt → minimale PLCA-Last
```

### 6.4 Blockgröße

Laufzeit-Parameter (`sample_count` im Header → kein Formatwechsel). Grenzen:
1 … ~700 Samples (MTU). **Default Diagnose: groß** (Effizienz). Klein wählbar für
spätere latenzärmere Betriebsarten. Obergrenze zusätzlich durch §7 begrenzt
(Slot-Belegungsdauer).

### 6.5 Adressierung / Bridge

- Ziel-IP/Port konfigurierbar: lokaler Master **oder** entfernter Host.
- Bridge macht eth0 (T1S) und eth1 (PC-Netz) zu einem L2-Segment → selbes Subnetz
  direkt erreichbar; echtes Fremdsubnetz braucht Gateway (Config-Feld vorhanden).
- Multicast als spätere Option, wenn mehrere Empfänger denselben Strom sehen
  sollen.

---

## 7. Säule 4b — Sende-Scheduling / Traffic Shaping [konzept]

Hält den Sync-Kanal frei, ohne in PLCA-Parameter einzugreifen. Nutzt die
**ohnehin vorhandene gemeinsame Uhr** als Sende-Taktgeber → selbstorganisiertes,
leichtes TDMA *über* PLCA.

### 7.1 Mechanismen

1. **Sync-Fenster reservieren.** 125-ms-Zyklus in Sync-Phase (kurz, nur
   Master↔Knoten) und Daten-Phase teilen. Jeder Knoten kennt seine Phase aus
   `ntp_now_ns() mod 125ms`. Datenknoten senden nie in der Sync-Phase.
2. **Sende-Slots staffeln.** Jeder Knoten sendet zu einem aus `node_id` +
   gemeinsamer Uhr abgeleiteten, versetzten Zeitpunkt → 7 Blöcke stauen sich
   nicht an einer PLCA-Runde.
3. **Blockgröße an Restbudget koppeln.** Ein Frame muss kürzer sein als das
   verfügbare Sende-Fenster (MTU-Frame ≈ 1.2 ms Belegung). Das deckelt die
   Blockgröße aus **Slot-Belegungs**-, nicht aus Bandbreitengründen.

### 7.2 Grenze (ehrlich)

PLCA ist kein TDMA: der tatsächliche Sendemoment hängt von der Transmit
Opportunity ab. Das Scheduling ergibt eine **statistische** Trennung von Daten und
Sync, keine harte Garantie. Für Diagnose mit ~10 % Buslast ausreichend.

### 7.3 Burst-Hinweis

PLCA-Burst (`MAXBC`) lässt einen Knoten *mehrere* Frames am Stück senden, wenn
mehrere anstehen — nicht durch Framegröße ausgelöst, sondern durch Warteschlange.
Für dieses System **Burst klein/aus halten**, damit kein Knoten den Bus über
mehrere große Frames monopolisiert und den Sync verzögert.

### 7.4 Abnahmekriterium

Voller Datenbetrieb (7 Knoten streamen) + laufender Sync: der gemessene
Sync-Jitter bleibt im Bereich ohne Datenlast (keine systematische Verzögerung der
Sync-Pakete hinter Datenframes).

---

## 8. Master-Seite — Zusammenführung [konzept]

Der Master arbeitet drei Aufgaben nicht-blockierend aus dem Superloop ab:

1. **Sync-State-Machine** (§4.4): Round-Robin REQUEST/REPLY/SET über N Knoten,
   alle 125 ms.
2. **Start-Koordination** (§5.1): einmaliger GO-Broadcast **mit ACK** (⚠D1), danach
   Überwachung.
3. **Datenempfang**: UDP-Pakete nach `(node_id, first_index)` einsortieren —
   **reines Einsortieren, keine Interpolation**.

Ergebnis: N gleichlange, index-ausgerichtete Kanäle. Sample *k* von Knoten 1 …
Knoten N stammt vom selben Zeitpunkt und kann direkt korreliert werden.

### 8.1 Index-Konsistenz-Check — verpflichtend (⚠D1, ⚠B3, ⚠D4)

Das „reine Einsortieren" hat keinen eingebauten Schutz gegen einen **still falsch
verankerten** Strom (verpasstes GO, Reboot, DMA-Überlauf). Da der Master sonst
beide Ströme als gültig behandelt, ist ein **billiger Cross-Check Pflicht**, nicht
optional:

```
# jeder Knoten sendet periodisch (z.B. alle 1 s) ein HEARTBEAT:
#   (node_id, aktueller_index, ntp_zeit_des_index)
# Master verifiziert:
erwartet = (ntp_zeit_des_index - X_ns) / 125000
if |aktueller_index - erwartet| > TOLERANZ:
    flagge node_id als "index-inkonsistent"   # Daten verwerfen/markieren
```

Dieser Check verwandelt „diagnose-fähig" in „diagnose-verlässlich" und kostet einen
winzigen Bruchteil der Bandbreite. **Er ist die wichtigste einzelne Härtung des
Gesamtkonzepts.**

### 8.2 Sync-Verlust-Zustand — verpflichtend (⚠D2)

Jeder Knoten definiert einen **Holdover-Grenzwert** (z. B. > K verpasste Syncs). Wird
er überschritten, markiert der Knoten seine Datenpakete als **„unsynchron"**, statt
stillschweigend mit eingefrorenem `s_rate_ppb` weiterzudriften. Ein Diagnosesystem
muss ungültige Phasen **kennzeichnen**, nicht verstecken.

### 8.3 Verfügbarkeit (⚠D3)

Der Master ist **Single Point of Failure**: fällt er aus, bricht Schleife A für alle
Knoten zusammen, eine Master-Neuwahl im Betrieb ist nicht vorgesehen. Für Laboraufbau
akzeptabel; für Dauerbetrieb ist ein Backup-Master/Neuwahl-Mechanismus nachzurüsten.

---

## 9. Stufenweise Entwicklungsanweisung

Jede Stufe ist einzeln testbar und baut auf der vorigen auf. **Reihenfolge nicht
überspringen** — jede Stufe ist Abnahmevoraussetzung der nächsten.

### Stufe 0 — Basis sichern [vorhanden, nur verifizieren]
- **Ziel:** disziplinierte Hardware-Uhr je Knoten läuft.
- **Tun:** `hwclk`-Bring-up (XOSC1→DPLL1→TC2) auf jedem Board nachfahren.
- **Abnahme:** §3.4.

### Stufe 1 — 1:1-Uhr-Sync bestätigen [vorhanden]
- **Ziel:** ein Knoten synchronisiert gegen Master/PC.
- **Tun:** vorhandenen PI-Pfad (`ntp_sync.c`) in Betrieb nehmen, `ntp watch`.
- **Abnahme:** Offset konvergiert, Holdover klein nach Sync-Stopp.

### Stufe 2 — Multi-Node-Discovery & Sync [konzept]
- **Ziel:** Master kennt N Knoten und hält ihre Uhren (Schleife A, 1:N).
- **Tun:** Beacon/Config-Block + Round-Robin-Sync-State-Machine (§4.4),
  MAC-adressierte Pakete.
- **Abnahme:** §4.5 (Pin-Toggle aller Knoten deckungsgleich am Logic-Analyzer).
- **Abhängig von:** Stufe 1.

### Stufe 3 — Hardware-Sampletakt je Knoten [konzept]
- **Ziel:** TCC0 erzeugt 8 kHz, ADC per Event, DMA in Ringpuffer.
- **Tun:** TCC0/EVSYS/ADC0/DMA aufsetzen (TCC0-Demo-Code als Vorlage).
- **Abnahme:** kontinuierlicher 8-kHz-Sample-Strom lokal, CPU-frei.
- **HARTE VORBEDINGUNG (⚠F1):** TCC0-PERBUF-Update on-the-fly **zuerst** am Scope
  nachweisen (glitch-frei, ein Übernahme pro Zyklus). Schlägt das fehl, ist Schleife B
  (Stufe 5) nicht baubar — dann alternative Takterzeugung nötig. **Dieser Nachweis
  geht allem anderen in Säule 3 voraus.**
- **Mitprüfen (⚠B3/⚠D1):** Index-Zähler an **physisch erzeugte** Samples koppeln (bei
  DMA-Überlauf mitzählen), nicht an gesendete.

### Stufe 4 — Gemeinsamer Start [konzept]
- **Ziel:** alle Knoten starten Sampling bei NTP-Zeit X, Index 0 verankert.
- **Tun:** GO-Broadcast **mit ACK + Wiederholung** + Compare-Arm auf X (§5.1).
- **Abnahme:** Sample-0 auf allen Knoten innerhalb µs gleichzeitig.
- **Vorbedingung (⚠D4):** Start je Knoten erst **nach bestätigtem Uhr-Lock** freigeben.
- **Abhängig von:** Stufe 2 (synchrone Uhren), Stufe 3 (Sampletakt).

### Stufe 5 — Sampletakt-Nachführung (Schleife B) [konzept]
- **Ziel:** Index-Achsen laufen dauerhaft nicht auseinander.
- **Tun:** Perioden-Dithering (§5.3), gekoppelt an `s_rate_ppb` (§5.4).
- **Abnahme:** §5.7 (Minuten-Dauerlauf, keine kumulative Drift).
- **Vorbedingung (⚠C1/⚠C2):** Gesamt-Schleifendynamik A→`s_rate_ppb`→B vorab
  analysieren/simulieren (kaskadierte Integratoren, Stabilität).
- **Mitprüfen (⚠C4):** Jitter-Spektrum per FFT — auf Spurious Tones achten, ggf.
  rauschförmiges Dithering. (⚠A2/⚠C3): Nachführbandbreite gegen thermische Drift
  testen, nicht nur gegen konstanten Offset.
- **Abhängig von:** Stufe 4.

### Stufe 6 — Transport-Protokoll [konzept]
- **Ziel:** Samples als UDP-Blöcke zum Master, korrekt einsortiert.
- **Tun:** Paketformat (§6.2), Blockung, Master-Einsortierung (§8).
- **Abnahme:** Master rekonstruiert N index-synchrone Kanäle; Paketverlust
  erkennbar an Index-Lücke.
- **Abhängig von:** Stufe 5.

### Stufe 6b — Härtung: Konsistenz-Check & Sync-Verlust-Flag [konzept] — verpflichtend
- **Ziel:** stille Fehlerfälle (falsch verankerter Strom) werden erkannt.
- **Tun:** HEARTBEAT `(node_id, index, ntp_zeit)` + Master-Cross-Check (§8.1);
  Holdover-Grenzwert → „unsynchron"-Flag (§8.2).
- **Abnahme (⚠D1/⚠D2):** Provozierter GO-Verlust / Knoten-Reboot / simulierter
  DMA-Überlauf wird vom Master als inkonsistent erkannt und geflaggt, **nicht** still
  fehlkorreliert.
- **Begründung:** Ohne diese Stufe ist das System nicht diagnose-*verlässlich*. Sie ist
  **kein optionales Extra**, sondern Teil der Kernfunktion.
- **Abhängig von:** Stufe 6.

### Stufe 7 — Sende-Scheduling [konzept]
- **Ziel:** Datenverkehr behindert Sync nicht.
- **Tun:** uhr-gestaffeltes Sende-Timing + Sync-Fenster (§7).
- **Abnahme:** §7.4 (Sync-Jitter unter Volllast unverändert).
- **Mitprüfen (⚠E1):** Daten- **und** Sync-Frame-Rate zusammenrechnen (kleine
  Sync-Frames sind slot-teuer). (⚠E2): auf Schwebung zwischen Uhr-Sendeplan und
  PLCA-Slot-Vergabe achten — periodische Häufung statt Gleichverteilung möglich.
- **Abhängig von:** Stufe 6b.

### Stufe 8 — Bridge/Remote & Skalierung [konzept]
- **Ziel:** Strom zu entferntem Host; Betrieb mit vollen 7 Knoten.
- **Tun:** Ziel-IP/Gateway konfigurierbar, ggf. Multicast; Lasttest.
- **Abnahme:** entfernter Host empfängt korrelierbare Ströme; Buslast/Sync stabil.
- **Mitprüfen (⚠E3):** Sync-Zyklus skaliert linear mit N×R — Grenze dokumentieren, ab
  der Round-Robin nicht mehr in 125 ms passt. (⚠G1): **Worst-Case-Fehler-Stackup** am
  realen Volllast-Aufbau messen, nicht nur Einzelwerte — die Gesamtgenauigkeit ist erst
  hier belegt.

---

## 10. Abgrenzung: warum keine Echtzeit-Regelung

Zur Einordnung dokumentiert, damit die Einsatzgrenze klar bleibt:

- **Einzel-Sample-Frames** bei 8 kHz × 7 = 56 000 Frames/s. Mindest-Ethernet-Frame
  „on wire" ~84 B → ~37 Mbit/s, weit über 10BASE-T1S. **Nicht darstellbar.**
- Selbst schlankes Custom-L2 pro Sample scheitert am Ethernet-Rahmen-Fixoverhead
  (~38 B) und an der PLCA-Frame-Rate.
- Half-Duplex + PLCA-Round-Robin addieren Wartezeit pro Sende-Richtung → zusätzliche
  Latenz.

**Fazit:** Latenzkritische Regelung über den Bus ist mit diesem Medium nicht das
Ziel. Das Konzept ist für **latenztolerante Diagnose/Korrelation** ausgelegt, wo
Blockung erlaubt ist und die Synchronität über den Sample-Index statt über
Ankunftszeit getragen wird. Für echte sub-µs-/Regelungs-Anwendungen → Hardware-
Timestamping/PTP (Schwesterprojekt), ggf. Voll-Duplex-Medium (100BASE-T1).

---

## 11. Risiko-Matrix & Gegenmaßnahmen

Vollständige kritische Betrachtung, in dieses Konzept eingearbeitet. Die Inline-
Hinweise **⚠(ID)** im Text verweisen hierher. Schwere: niedrig/mittel/hoch/
**kritisch** (kann Konzept kippen). Konfidenz = Sicherheit der Risikoeinschätzung
selbst.

### 11.1 Die vier gravierendsten Punkte

1. **⚠D1 — stiller Index-Versatz** (kritisch). GO-Verlust, Reboot oder DMA-Überlauf
   erzeugen einen formal gültigen, aber falsch verankerten Strom; der Master „sortiert
   nur ein" und merkt es nicht. → **Gegenmaßnahme: Konsistenz-Check §8.1 (Pflicht).**
2. **⚠F1 — TCC0-PERBUF on-the-fly** (kritisch). Schleife B ändert die Periode 8000×/s;
   greift das Buffer-Update nicht glitch-frei, bricht die Nachführung. → **Vorab am
   Scope absichern (Stufe 3 Vorbedingung).**
3. **⚠B1/⚠B2 — µs ist Mittelwert, nicht Pro-Sample**, und die Stempel-Decke wächst
   unter Last (Henne-Ei). → Für mittelnde Korrelation ausgelegt; ereignisgenau nicht
   belegt.
4. **⚠G1 — nichts am Gesamtaufbau gemessen.** Integrationsrisiko ist das größte
   Einzelrisiko und nicht wegargumentierbar.

### 11.2 Vollständige Matrix

| ID | Risiko | Schwere | Konf. | Gegenmaßnahme / Verweis |
| --- | --- | --- | --- | --- |
| D1 | Stiller Index-Versatz (Verlust/Reboot/DMA) | **kritisch** | 80 % | Konsistenz-Check §8.1; Stufe 6b |
| F1 | TCC0-PERBUF bricht Schleife B | **kritisch** | 60 % | Scope-Nachweis vor Stufe 5; Stufe 3 |
| B1 | µs = Mittelwert, nicht Pro-Sample | hoch | 80 % | Einsatzklasse begrenzen §1.1 |
| B2 | Stempel-Decke wächst unter Last | hoch | 75 % | unter Last messen §4.2/⚠G1 |
| C1 | A/B nicht regelungstechnisch entkoppelt | hoch | 80 % | Schleifenanalyse §2.1/Stufe 5 |
| D2 | Kein definierter Sync-Verlust-Zustand | hoch | 75 % | Holdover-Flag §8.2 |
| G1 | Nichts am Gesamtaufbau gemessen | hoch | 95 % | Volllast-Stackup Stufe 8 |
| A1 | Kein Master-Fallback gegen Slip | mittel | 80 % | Konsistenz-Check §8.1 als Netz |
| A2 | ppm-Rest als statisch behandelt (Thermik) | mittel | 75 % | Nachführbandbreite §5.2/Stufe 5 |
| A3 | Bias-Aufhebung nur teilweise | mittel | 70 % | positionsabh. Offset messen §5.5 |
| B4 | Holdover-Annahme (1 ppm) optimistisch | mittel | 70 % | realen Rest messen §5.5 |
| C2 | Verschachtelte Integratoren, Stabilität | mittel | 65 % | Schleifensimulation Stufe 5 |
| C3 | Glättung-vs-Drift kein gutes Tuning | mittel | 75 % | ggf. besserer Schätzer §5.2 |
| C4 | Dither erzeugt Spurious Tones | mittel | 70 % | FFT-Prüfung, Rausch-Dither §5.3 |
| D3 | Master = Single Point of Failure | mittel | 85 % | Backup/Neuwahl §8.3 |
| D4 | Spät-Einstieg zirkulär (Lock nötig) | mittel | 70 % | Einstieg nach Lock §5.1/Stufe 4 |
| E1 | Sync-Frame-Rate in Buslast unterschätzt | mittel | 70 % | Gesamtlast rechnen Stufe 7 |
| E2 | Uhr-Plan vs. PLCA-Slot schwebt | mittel | 70 % | Schwebung messen Stufe 7 |
| F2 | Komplexität durch Rev-D selbst-induziert | mittel | 90 % | bei HW-Revision neu bewerten §3.3 |
| G2 | TC2- vs. SYS_TIME-Welt nicht integriert | mittel | 80 % | Stränge zusammenführen §12 |
| B3 | Index nur relativ zum Anker gültig | niedrig | 85 % | Index an phys. Samples §6.2 |
| E3 | N-Skalierung sprengt Sync-Zyklus | niedrig | 65 % | Grenze dokumentieren Stufe 8 |
| F3 | ADC am DFLL, Wandelqualität | niedrig | 70 % | bei Präzisionsanspruch prüfen |
| G3 | Optimismus-Schlagseite, kein Stackup | niedrig | 70 % | Worst-Case-Stackup §5.5/Stufe 8 |

### 11.3 Gegenmaßnahmen nach Priorität

1. **Master-Index-Plausibilisierung** (gegen D1/B3/D4/A1) — billigstes wirksamstes
   Sicherheitsnetz, Stufe 6b. Macht aus „diagnose-fähig" ein „diagnose-verlässlich".
2. **TCC0-PERBUF zuerst am Scope** (gegen F1) — vor allem anderen in Säule 3.
3. **Sync-Verlust-Zustand + Daten-Flag** (gegen D2) — §8.2.
4. **Gesamt-Schleifendynamik A→B simulieren** (gegen C1/C2/C3) — vor dem Feldtest.
5. **Worst-Case-Fehler-Stackup** statt Einzelbewertung (gegen G3/G1) — pessimistische
   Annahmen gegen das Sample-Intervall.
6. **Dither-Spektrum prüfen** (gegen C4) — FFT; falls störend, rauschförmiges Dither.

---

## 12. Quellenbezug (Projekt)

- Hardware-Zeitbasis & Trigger: `firmware/.../hwclk_cli.c`, Bring-up-Doku.
- 1:1-Sync & PI-Regler: `firmware/.../ntp_sync.c`, `NTP_FUNKTIONSWEISE.md`.
- Multi-Node-Rahmen (Discovery/Config/1:N): `NTP_MULTINODE_SZENARIO.md` (Konzept).
- Konvergenz-/Genauigkeitstheorie: `NTP_TWO_NODE_CONVERGENCE.md`.

> **Status gesamt:** Säule 1 vorhanden, Säulen 2–4 Konzept. Dieses Dokument ist die
> Implementierungsvorlage; die stufenweise Anweisung (§9) ist der Arbeitsplan.
>
> **Gesamturteil (ehrlich).** Das Konzept ist in sich schlüssig und für **mittelnde
> Korrelations-Diagnose** plausibel tragfähig. Es ist **nicht** belegt für
> ereignisgenaue Synchronität (⚠B1), **nicht** robust gegen stille Fehlerfälle ohne
> die Härtung aus §8.1/Stufe 6b (⚠D1 ist die ernsteste Lücke), und in mehreren
> Genauigkeitsaussagen optimistisch (⚠G3). Reife: **tragfähige Hypothese, kein
> verifiziertes Design.** Die größten Hebel sind der Master-seitige Konsistenz-Check
> und der frühe HW-Nachweis von TCC0-PERBUF — beide sind oben als verbindlich
> markiert.
