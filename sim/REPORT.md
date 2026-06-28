# Machbarkeitsbericht — verteiltes synchrones ADC-Sampling (Simulation)

Ergebnis der meilensteinweisen Simulation gemäß `SIMULATION_SPEC.md`, die die
Regelungs-/Zeitlogik aus `SYNC_ADC_KONZEPT.md` prüft. Der `sync_core.{h,c}`-Regler
ist **firmware-treu**: die PI-Konstanten und die Sampletakt-Rechnung sind 1:1 aus
`ntp_sync.c` / `hwclk_cli.c` übernommen (siehe `README.md`), nicht erfunden. Alle
Läufe sind deterministisch (seedbar); die Kennzahlen sind über die Seeds 1–3
gemittelt.

> **Fazit.** Die Regelungs- und Zeitlogik ist **solide** — Uhr-Sync, die
> A→B-Kaskade, die Thermik-Nachführung, das Sampletakt-Dithering und der
> Fehler-Konsistenz-Check funktionieren wie entworfen. **Aber die „<1 Sample"-
> Zusage ist jitter-limitiert:** sie hält nur für einen Sync-Offset-Jitter
> **σ ≲ 28 µs**. Bei dem **real gemessenen σ ≈ 150 µs** (dem Software-NTP-Floor
> dieser Plattform, vgl. `wireshark/NTP_SYNC.md`) beträgt der Index-Skew
> **~5 Samples**, d. h. die **per-Sample-/ereignisgenaue Synchronität bricht**.
> Für den erklärten Zweck des Konzepts — *mittelnde* Korrelations-Diagnose —
> bleibt es tragfähig, weil der Skew mittelwertfrei ist und sich wegmittelt; für
> ereignisgenaue Koinzidenz nicht.

---

## 1. Verwendete Firmware-Konstanten (extrahiert, nicht erfunden)

| Größe | Wert | Quelle |
|---|---|---|
| PI-Integral-Verstärkung | `Ki = 1/4` (`NTP_KI_DEN`) | `ntp_sync.c:64` |
| PI-Proportional-Verstärkung | `Kp = 1` (voller Phasenschritt je Sync) | `ntp_sync.c:364` |
| Uhr-Lesen | `now = raw + s_offset_ns + rate_held` | `ntp_sync.c:111` |
| Sampleperiode | `96e6/freq` Ticks → 8 kHz = 12000 (Reg 11999) | `hwclk_cli.c:765` |
| Tick | `125/12 ns` | `hwclk_cli.c hwclock_now_ns` |

**Zwei Korrekturen, wo die Konzept-Doku der Firmware/Physik widersprach** (Firmware gewinnt):
1. `s_rate_ppb` ist für einen schnellen (+ppm) Oszillator **negativ** → Loop B
   nutzt `per_ideal = PER_NOM·(1 − s_rate_ppb/1e9)` (Konzept §5.3 hatte `+`).
   In M2 verifiziert: `s_rate_ppb` rastet auf `−true_ppm` ein.
2. `PER_NOM = 12000` ist die Perioden*länge*; das HW-Register ist Länge−1.

---

## 2. Urteil je Risiko

Schwere aus `SYNC_ADC_KONZEPT.md` §11. Urteil = **PASS** / **GRENZE** (funktioniert
mit benannter Schranke) / **GEBROCHEN** (versagt schon im idealisierten Modell).

| ID | Risiko | Urteil | Beleg (Kennzahl, Plot) |
|---|---|---|---|
| **Kern** | index_skew < 1 Sample über lange Läufe | **PASS für σ≲28 µs** | M4: 0,061 @ σ=2 µs (16× Reserve), `plot/pw_index_skew.png` |
| **B1/B2** | µs ist Mittelwert, nicht pro-Sample; Floor wächst unter Last | **GRENZE / GEBROCHEN** | Sweep: skew ∝ 0,030·σ[µs]; **bricht bei σ≈28 µs**; σ=150 µs → 5,4 Samples. `sweep_results/plot/sweep_skew_vs_sigma.png` |
| **C1/C2** | A→B-Kaskade nicht regelungstechnisch entkoppelt; verschachtelte Integratoren schwingen | **PASS** | `s_rate_ppb` rastet ein, kein Überschwingen/Pendeln in keinem Lauf. `plot/ts_rate_ppb.png` |
| **A2/C3** | ppm nicht statisch (Thermik); Nachführbandbreite vs. Glättung | **PASS** | Drift sine ±3 ppm / ramp +2 ppm → skew 0,073 = identisch zu kein-Drift |
| **B2** | heavy-tail PLCA/Stack-Spikes | **PASS** | heavy_tail 0,371 vs. gauss 0,364 @ σ=10 µs — robuster Schätzer (min-delay-Median) filtert die 5%×10σ-Ausreißer |
| **B2** | lastabhängiger Jitter (Henne-Ei) | **GRENZE** | load_dep (×4) → 0,627; verschlechtert sich ∝ effektivem σ wie vorhergesagt |
| **A3** | Bias-Aufhebung nur teilweise (positionsabh.) | **BESTÄTIGT** | biased ±50 µs/Knoten → 0,715; der *differenzielle* Bias überlebt den paarweisen Vergleich (nur ein gemeinsamer Bias hebt sich auf) |
| **C4** | Dither erzeugt Spurious Tones, kein weißes Rauschen | **BESTÄTIGT + behoben** | Bresenham: diskreter Hügel ~0,34 cyc/sample (`plot/dither_fft_bresenham.png`); Rausch-Dither glättet ihn zu Breitband (`plot/dither_fft_noise.png`) bei identischem Skew |
| **B1/B4/G3** | optimistische Bilanz, kein Worst-Case-Stackup | **BESTÄTIGT** | Worst-Case |time_skew| = **629 µs** @ σ=150 µs heavy_tail (`plot/stackup_worstcase.png`); Worst-Case ≫ Mittelwert |
| **D1** | stiller Index-Versatz (GO-Verlust / Reboot / DMA-Overflow) | **PASS (mit Check)** | M6: sample_loss→40, go_loss→8, reboot→4 Samples; der Master-Konsistenz-Check flaggt genau den schuldigen Knoten, Rest sauber. Ohne Check sind diese unsichtbar. |
| **D4** | Spät-/Reboot-Einstieg vor Lock verankert falsch | **PASS (mit Check)** | M6-Reboot-Szenario geflaggt (4 Samples) |

---

## 3. Warum es bei σ≈28 µs bricht (der Mechanismus)

Der Index-Skew zwischen zwei Knoten wird von der Differenz ihrer disziplinierten
Uhren geteilt durch die 125-µs-Sampleperiode bestimmt. Weil **Kp = 1**, schnappt
jeder Sync die Phase des Knotens auf den *vollen verrauschten* Offset-Messwert →
die Uhr selbst trägt ≈1,5·(Pro-Sync-Offset-Rauschen). Der robuste Schätzer senkt
dieses Rauschen um √(R/2), aber der Rest skaliert linear mit σ:

```
max index skew ≈ 0,030 × σ[µs]      (gemessene Steigung, 3 Seeds)
  σ = 2 µs   → 0,07 Samples
  σ = 28 µs  → 1,0  Samples   ← Bruch
  σ = 150 µs → 5,4  Samples   (realer Software-NTP-Floor)
```

Die Grenze ist also **nicht** der Oszillator, das Dithering oder die
Schleifendynamik — sondern der **Timestamp-Jitter von Software-NTP** (⚠B1/B2/G1).
Zwei Hebel verschieben die Grenze:
- **σ unter 28 µs senken** → Hardware-/PTP-Frame-Timestamping (Schwesterprojekt
  `net_10base_t1s`). Das ist der einzige Weg zu per-Sample-Sync.
- **Lock-Zeit gegen Skew tauschen** → ein Phasen-Tiefpass (Kp < 1) statt des vollen
  Schritts würde das eingespeiste Rauschen senken, zum Preis langsamerer Konvergenz.
  Steckt nicht in der aktuellen Firmware; ein Folge-Thema, falls per-Sample-Sync
  auf Software-NTP je gefordert ist.

---

## 4. Was das am Konzept ändert

- **Mittelnde Korrelationsklasse (der erklärte Zweck, §1.1):** tragfähig. Der Skew
  ist mittelwertfrei (Histogramm um 0 zentriert), also bleibt Korrelation über
  Fenster ≫ 125 ms unberührt. Das System leistet, wofür es ausgelegt wurde.
- **Ereignisgenaue Koinzidenz (Zwei-Kanal-Flanke/Transient):** auf Software-NTP
  beim gemessenen σ nicht erreichbar. Das deckt sich mit dem ⚠B1-Vorbehalt des
  Konzepts selbst — jetzt quantifiziert: es braucht σ < 28 µs, also
  Hardware-Timestamping.
- **Die verpflichtende Härtung (§8.1 Konsistenz-Check) ist gerechtfertigt:** M6
  zeigt, dass die Stillfehler-Fälle real und einzeln unsichtbar für „nur nach Index
  einsortieren" sind, und dass der billige Heartbeat-Cross-Check jeden erkennt.

---

## 5. Hebel: längere Konvergenzzeit gegen σ≈150 µs

Frage: *Eine längere Konvergenzzeit ist akzeptabel — wie lang muss sie sein, damit
σ≈150 µs das Konzept nicht bricht?* Antwort aus der Simulation (Knöpfe `--kiden`,
`--kp`; **vorgeschlagene** Firmware-Änderung, Default firmware-treu 4/1):

**Wichtig zuerst:** mit der aktuellen Firmware (Ki=1/4, Kp=1) hilft längeres Laufen
**nicht** — die 6,5 Samples bei σ=150 µs sind *stationäres Rauschen*, kein
Transient. „Konvergenzzeit verlängern" heißt: die Schleife **langsamer + glättender**
auslegen (Ki kleiner + Kp kleiner). Das kostet Lock-Zeit und kauft Rauschunterdrückung.

Worst-case max Index-Skew über 3 Seeds, σ=150 µs, 180 s:

| | kp=1.0 | kp=0.5 | kp=0.25 | kp=0.125 | Frequenz-Lock |
|---|---|---|---|---|---|
| **ki_den=16** | 2,97 | 2,12 | 2,04 | 2,44 | ~2 s |
| **ki_den=32** | 2,39 | 1,61 | 1,50 | 1,63 | ~4 s |
| **ki_den=64** | 2,39 | 1,36 | 1,24 | 1,13 | ~8 s |
| **ki_den=128** | 3,12 | 1,40 | 1,03 | **0,88** ✅ | **~16 s** |

**Betriebspunkt:** `Ki = 1/128`, `Kp = 1/8` → max Skew **0,92 < 1** (5 Seeds, robust;
hält mit Thermik-Sinus ±3 ppm und Rampe +2 ppm). Firmware-Baseline dort: 6,45 Samples.

**Benötigte Konvergenzzeit:** der Frequenz-Lock dominiert ≈ `ki_den × 125 ms =
128 × 0,125 s ≈ 16 s` (Phasen-τ = 125 ms/Kp = 1 s) → gesamte Einschwingzeit grob
**15–20 s** statt ~1,5 s heute (**≈10×**).

**Warum zwei Knöpfe:** Ki kleiner glättet `s_rate_ppb` (weniger Rate-Wander zwischen
Syncs) — der *dominante* Hebel (allein 6,5 → 2,2). Über ki_den≈128 hinaus wird es
wieder schlechter (Loop zu langsam für den statischen ppm). Kp kleiner mittelt erst
*danach* den Phasen-Schnapp-Floor weg; allein (bei Ki=1/4) bringt es nichts, weil das
Rate-Rauschen dominiert.

**Einschränkungen:** (a) Marge dünn (0,9 von 1,0, ~10 %) → σ=150 µs liegt gerade eben
im Grünen; streut σ unter Last höher (⚠B2), reicht es nicht. (b) ⚠C3-Grenze: ein
16-s-Loop trägt ±3 ppm/10 min locker, würde bei *schnellerer* Thermik (Sprung,
Lüfteranlauf) aber nachhinken. (c) Sauberer bleibt, **σ zu senken** (HW-/PTP-
Timestamping, σ<28 µs) — das erreicht <1 Sample ohne den Loop zu verkrüppeln und mit
voller Drift-Bandbreite.

---

## 6. Rückkanal — kann ein Knoten verlässlich wissen, dass er wirklich im Sync ist?

Frage: lokal allein kann ein Knoten nur die *Voraussetzung* prüfen (sein eigenes
Sync-σ), nicht den *tatsächlichen* Versatz oder einen still falschen Anker — seine
eigene Uhr ist ja die Referenz, die infrage steht. Lösung: ein **Rückkanal** zum
Master.

**Protokoll (modelliert):** jeder Knoten sendet periodisch (1 s) einen Heartbeat
`(Index, eigene NTP-Zeit)`. Der Master legt den Index auf **seine eigene** Zeitachse
(traut der Knoten-Uhr nicht → fängt Uhr- *und* Anker-Fehler), **mittelt** über mehrere
Heartbeats (EMA — ein konstanter Anker-Fehler überlebt die Mittelung, der Timestamp-
Jitter wird herausgemittelt) und schickt **CONFIRMED/FLAGGED** zurück. Der Knoten
zertifiziert seine Daten nur bei frischem CONFIRMED.

Verglichen werden zwei Selbstdiagnosen gegen die **Ground Truth** der Sim (mittlerer
\|Skew vs. Master-Achse\| < 1 Sample = „wirklich verankert im Sync"):

| Szenario | tats. mean \|skew\| | lokal allein | Rückkanal |
|---|---|---|---|
| gesund, σ=2 µs | ~0 | IN ✅ | IN ✅ |
| **go_loss** (stiller Anker, σ=2) | ~8 (Knoten 5) | **IN — FALSE POSITIVE** ❌ | **out — gefangen** ✅ |
| **reboot** (Pre-Lock-Rejoin) | ~4 | **IN — FALSE POSITIVE** ❌ | out — gefangen ✅ |
| σ=150 µs, **Firmware**-Regler | 1–2 (echt daneben) | out | out (korrekt zurückhaltend) |
| σ=150 µs, **getunt** (ki=128/kp=0.125) | 0,15–0,47 (<1) | **out ×7 — FALSE NEGATIVE** ❌ | **IN ×7 — korrekt** ✅ |
| σ=150 µs getunt **+ go_loss** | Schuldiger ~8, Rest <1 | gemischt | **0 FP, 0 FN — reliable** ✅ |

**Ergebnis:** Der Rückkanal hat über alle Szenarien **0 Falsch-Positive** — kein Knoten
zertifiziert sich fälschlich als „im Sync". Er löst beide Schwächen der Lokaldiagnose:
- **unsicher** (Lokal-FP bei stillem Anker-Fehler) → Rückkanal fängt go_loss/reboot.
- **überkonservativ** (Lokal-FN: erkennt nicht, dass der getunte Regler σ=150 fixt) →
  nur der Rückkanal bestätigt den real erreichten Sync.

**Ehrliche Grenze:** „CONFIRMED" heißt **„korrekt verankert & gelockt" (mean < 1)**, nicht
„jeder Sample < 1". Bei σ=150 µs mit *ungetuntem* Regler ist die Ausrichtung real >1 — der
Rückkanal bestätigt dann korrekt **nicht**. Die *per-Sample*-Unsicherheit (~σ/125 µs) muss
der Knoten **zusätzlich** als Qualitätsmaß melden. Beides zusammen — Rückkanal-CONFIRMED
(Struktur/Anker) **plus** gemeldetes σ (Streuung) — gibt dem Knoten eine vollständige,
verlässliche Selbstauskunft. Kosten: ein kleiner Heartbeat-Roundtrip/s.

> CLI: der Rückkanal-Report läuft in jedem Lauf; `run_faultcheck.csv` enthält die
> Master-Prüfwerte. Reproduktion in §8.

---

## 7. Was diese Simulation NICHT beweist (verbindlich, SIMULATION_SPEC.md §8)

Die Simulation kann das Konzept **falsifizieren** und seine **Robustheitsgrenzen
kartieren**; sie kann es nicht abschließend verifizieren. Ausdrücklich außerhalb
des Geltungsbereichs:

- **⚠F1 — TCC0-PERBUF on-the-fly.** Ob das Sampleperioden-Register pro Zyklus
  glitch-frei übernimmt, ist **Hardwareverhalten**, hier nicht entscheidbar. Bleibt
  eine Scope-Messung am realen Board und ist *Vorbedingung* für Loop B (Konzept
  Stufe 3). Die Sim setzt voraus, dass die Perioden-Übernahme funktioniert.
- **⚠E1/E2 — PLCA-Bus-Dynamik.** Der Sync-Jitter ist ein *angenommenes
  statistisches Modell*, keine Bus-Simulation. Die σ-Bruchgrenze (28 µs) ist eine
  **Robustheitsgrenze**, keine Vorhersage des realen σ — das reale σ muss unter
  Last auf Hardware gemessen werden (die Sim sagt nur „bricht oberhalb 28 µs").
- **⚠F3 — ADC-Wandelqualität** — nicht modelliert.
- **⚠G1 — Integrationsrisiko.** Nichts hiervon ist am verteilten Aufbau gemessen;
  die Sim reduziert dieses Risiko, hebt es nicht auf.

**Korrekte Lesart eines „PASS":** die Regelungs-/Zeitlogik trägt unter den
angenommenen Stimuli. Der nächste Schritt ist ein Hardware-Prototyp, um das reale σ
zu messen (liegt es unter oder über der 28-µs-Grenze?) und ⚠F1 zu klären.

---

## 8. Reproduzieren

```sh
mingw32-make                       # sim.exe bauen (gcc, 0 Warnungen)
./sim.exe                          # Default-Lauf (7 Knoten, σ=2 µs) -> CSVs + M4-Urteil
./sim.exe --sigma 150000           # realistischer Jitter -> skew ~5 Samples
./sim.exe --sigma 150000 --kiden 128 --kp 0.125 --runtime 240   # §5: ~16s Lock -> <1 Sample
./sim.exe --fault go_loss --faultnode 5      # §6: Rückkanal fängt stillen Anker-Fehler
./sim.exe --sigma 150000 --kiden 128 --kp 0.125 --runtime 300   # §6: Rückkanal bestätigt 0 FP/FN
bash sweep.sh sweep_results        # M7-Sweep -> sweep_results/run_summary.csv
python plot/plot_results.py .                 # Pro-Lauf-Plots
python plot/plot_results.py sweep_results      # Bruchgrenzen-Kurve
```

Der Rückkanal-/Zertifizierungs-Report wird in **jedem** Lauf am Ende ausgegeben
(Vergleich lokal-allein vs. Rückkanal, mit Falsch-Positiv/Negativ-Zählung).

Alle oben referenzierten Abbildungen werden von diesen beiden Plot-Befehlen neu
erzeugt. Seeds stehen in jeder `run_summary.csv`-Zeile; gleicher Seed → identischer
Lauf.
