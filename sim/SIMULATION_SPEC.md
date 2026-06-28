# Simulations-Spezifikation — Machbarkeitsprüfung Sync-ADC-Konzept

> **Was dieses Dokument ist.** Ein **Arbeitsauftrag** für eine Claude-Code-Agenten-
> Session. Es beschreibt eine PC-Simulation in C, die das in `SYNC_ADC_KONZEPT.md`
> beschriebene Konzept auf Machbarkeit prüft — bevor Hardware-Aufwand entsteht.
>
> **Begleitdokument.** `SYNC_ADC_KONZEPT.md` (Kontext: was & warum). Dieses Dokument
> referenziert dessen Risiko-IDs (z. B. ⚠C1) — die Simulation existiert genau, um
> diese Risiken am Schreibtisch zu entscheiden.
>
> **Referenz-Firmware.** Die realen Reglergleichungen stehen in `ntp_sync.c`
> (PI-Regler, `s_offset_ns`/`s_rate_ppb`) und `hwclk_cli.c` (Sampletakt/TCC0). Der
> Sim-Reglercode MUSS diese Gleichungen 1:1 nachbilden, nicht eigene erfinden. Vor
> dem Schreiben des `sync_core`-Moduls diese Dateien lesen und die Formeln daraus
> übernehmen.

---

## 0. Leitprinzipien für den Agenten

1. **Meilensteinweise arbeiten.** Nach jedem Meilenstein (§7) anhalten, die
   Ergebnis-Kennzahlen zeigen, auf Freigabe warten. Nicht alles in einem Rutsch.
2. **`sync_core` ist firmware-tauglich.** Reiner C99, keine Heap-Allokation im
   Kern, keine PC-only-Abhängigkeiten (keine `printf`/`malloc`/`math.h`-Exoten im
   Kernmodul außer dem Nötigsten). Die Simulationshülle drumherum darf alles nutzen.
3. **Determinismus.** Seedbare Zufallszahlen; jeder Lauf bei gleichem Seed exakt
   reproduzierbar. Ohne das sind die Ergebnisse wertlos.
4. **Falsifizieren, nicht beschönigen.** Ziel ist, das Konzept zum **Brechen** zu
   bringen, wenn es bricht. Pessimistische Parameter sind erwünscht. Die Simulation
   ist ein Sieb, kein Werbemittel.
5. **Keine Hardware-Behauptungen.** Die Sim kann ⚠F1 (TCC0-PERBUF) und die reale
   PLCA-Statistik NICHT beantworten (§8). Das ist explizit zu dokumentieren, nicht
   zu übertünchen.

---

## 1. Ziel der Simulation

Quantitativ beantworten, ob die **Regelungs- und Zeitlogik** des Konzepts unter
plausiblen Annahmen funktioniert. Konkret die Kernfrage:

> Bleiben die Sample-Indizes von N Knoten über lange Laufzeit **knotenübergreifend
> ausgerichtet** (< 1 Sample Versatz), wenn jeder Knoten einen eigenen, driftenden
> Oszillator hat und nur alle 125 ms einen verrauschten Sync bekommt?

Und die Folgefragen (= die im Konzept offenen Risiken):

| Frage | Risiko-ID | Was die Sim liefert |
| --- | --- | --- |
| Schwingt die A→B-Kaskade stabil ein? | ⚠C1 ⚠C2 | Einschwingkurve, Stabilitätsurteil |
| Hält Index-Sync über Stunden? | Kernanf. | Versatz(t)-Kurve je Knotenpaar |
| Wie groß ist der reale Fehler-Stackup? | ⚠B1 ⚠B4 ⚠G3 | Verteilung, Worst-Case statt Einzelwerte |
| Folgt die Nachführung der Thermik-Drift? | ⚠A2 ⚠C3 | Versatz bei zeitvariablem ppm |
| Erzeugt das Dithering Spurious Tones? | ⚠C4 | FFT des Sampletakt-Jitters |
| Ab welcher Jitter-Charakteristik bricht es? | ⚠B2 | Robustheitsgrenze (Parametersweep) |

---

## 2. Was die Simulation modelliert — und was nicht

**Modelliert (hohe Aussagekraft):**
- N Knoten, je eigener Oszillator mit ppm-Offset + zeitvariabler ppm-Drift.
- Virtuelle NTP-Uhr je Knoten (`raw + s_offset_ns + rate_held`).
- Schleife A: 125-ms-Sync mit konfigurierbarem Offset-Jitter-Modell, PI-Regler.
- Schleife B: Sampletakt mit Perioden-Dithering, gekoppelt an `s_rate_ppb`.
- Gemeinsamer Start (Index-Anker) + Index-Fortschreibung je Knoten.

**Vereinfacht modelliert (mittlere Aussagekraft):**
- Sync-Jitter als **statistisches Modell** (mehrere Varianten, §4), nicht als echte
  PLCA/UDP-Simulation. Die Sim sagt „*wenn* Jitter so, dann …".
- Optional: einfaches Paketverlust-/Reboot-Ereignismodell für ⚠D1-Tests.

**NICHT modelliert (keine Aussagekraft — explizit so dokumentieren):**
- TCC0-PERBUF-Hardwareverhalten (⚠F1) — bleibt Scope-Arbeit.
- Reale PLCA-Slot-Dynamik / Buslast-Schwebung (⚠E1 ⚠E2) — nur grob abschätzbar.
- ADC-Wandelqualität (⚠F3).

---

## 3. Architektur & Modulgrenzen

```
sim/
  sync_core.h        ← FIRMWARE-TAUGLICH: Datentypen + Reglerfunktionen
  sync_core.c        ← FIRMWARE-TAUGLICH: PI-Regler (A) + Dithering (B)
  sim_main.c         ← PC-only: Event-Loop, Knotenverwaltung, CSV-Ausgabe
  sim_noise.h/.c     ← PC-only: seedbare Jitter-/Drift-Modelle
  sim_config.h       ← Parameter (N, Laufzeit, Jitter-Modell, Seeds …)
  plot/
    plot_results.py  ← OPTIONAL: liest CSV, erzeugt Plots (matplotlib)
  Makefile
  README.md          ← wie bauen/ausführen, was die CSVs bedeuten
```

**Die harte Modulgrenze:** Alles in `sync_core.{h,c}` muss ohne Änderung auf den
ATSAME54 kompilieren. Das heißt:
- C99, keine dynamische Allokation, keine OS-Aufrufe.
- Feste Integer-Typen (`int64_t`, `uint32_t` …).
- Fließkomma nur, wo die Firmware es auch nutzt (der PI-Regler rechnet in der
  Firmware mit `double` — das ist auf dem M4F mit FPU vertretbar; im Zweifel die
  Firmware-Wahl spiegeln).
- Zustand wird als `struct` von außen hereingereicht, nicht global gehalten.

`sim_main.c` ruft `sync_core`-Funktionen genauso auf, wie es später die Firmware
täte — dadurch ist der getestete Code identisch mit dem späteren Produktivcode.

---

## 4. Modelldetails

### 4.1 Knoten-Oszillator

Jeder Knoten i hat:
```
ppm_base[i]    : statischer Frequenz-Offset, z.B. gleichverteilt in [+20, +35] ppm
                 (entspricht dem realen ~+28-ppm-Rest, Streuung zwischen Boards)
ppm_drift(t,i) : zeitvariabler Anteil (Thermik, ⚠A2), z.B.
                 - Variante a: 0 (Referenzfall)
                 - Variante b: langsame Sinusdrift ±3 ppm, Periode ~10 min
                 - Variante c: Rampe +2 ppm über die Laufzeit
```
Die **wahre** lokale Tickrate ist `96e6 * (1 + (ppm_base+ppm_drift)/1e6)`. Daraus
schreitet `raw_ticks` je virtuellem Zeitschritt fort. Wichtig: Die wahre Drift kennt
nur der Simulator — der Knoten-Regler sieht sie nur indirekt über den Sync.

### 4.2 Sync-Jitter-Modell (Schleife A) — der kritische Stimulus

Der gemessene Offset trägt Rauschen. Mehrere Modelle bereitstellen (per Config
wählbar), weil die reale Statistik unbekannt ist (⚠B2):
```
- gauss       : N(0, sigma), z.B. sigma = 2 µs
- heavy_tail  : Gauss + gelegentliche Ausreißer (z.B. 5 % Samples ×10 sigma)
                → bildet PLCA-/Stack-Spikes nach
- load_dep    : sigma steigt, wenn "Datenlast aktiv" (Schalter) → Henne-Ei ⚠B2
- biased      : konstanter positiver Offset je Knoten (positionsabh. Bias ⚠A3)
```
Der robuste Schätzer aus dem Konzept (R Runden, min-delay, Median) ist im
**Master-Teil** der Sim nachzubilden, damit getestet wird, wie gut er die Modelle
filtert.

### 4.3 Schleife A — PI-Regler (aus ntp_sync.c übernehmen)

Pseudostruktur (echte Konstanten/Reihenfolge aus der Firmware verifizieren):
```
sync_apply_offset(node, adjust, interval_us):
    node.s_offset_ns += rate_held(node, raw)      # Rate in Phase einfrieren
    node.s_lastSyncRaw = raw
    node.s_offset_ns += adjust                     # P, Kp=1
    if node.synced and interval_us > 0:
        drift_ppb = adjust * 1e6 / interval_us
        node.s_rate_ppb += drift_ppb / KI_DEN      # I, KI_DEN=4
    else:
        node.synced = true                         # erster Sync: nur Phase
```

### 4.4 Schleife B — Perioden-Dithering (aus Konzept §5.3)

```
sample_core_next_period(node):
    per_ideal = PER_NOM * (1 + node.s_rate_ppb / 1e9)   # PER_NOM=12000
    node.per_resid += frac(per_ideal)
    if node.per_resid >= 1.0:
        per_this = PER_NOM + 1; node.per_resid -= 1.0
    else:
        per_this = PER_NOM
    node.sample_k += 1
    return per_this
```
**Variante für ⚠C4 bereitstellen:** rauschförmiges Dithering (zufälliger Schwellwert
statt fester Akkumulation) als umschaltbare Alternative, um die Spektren zu
vergleichen.

### 4.5 Gemeinsamer Start & Index

```
X_ns = master_now + Vorlauf
je Knoten: target_tick aus X_ns über die (synchronisierte) lokale Uhr
           sample_k = 0 bei target_tick
```
Für ⚠D1-Tests optional: einzelne Knoten verpassen GO / rebooten zur Laufzeit /
verlieren Samples → prüfen, ob der **Master-Konsistenz-Check** (§8.1 im Konzept,
auch hier nachbilden) das erkennt.

---

## 5. Zeitführung der Simulation

**Ereignisgesteuert**, virtuelle Zeit, kein Echtzeitbezug. Zwei Ereignisarten:
```
- SYNC_EVENT   alle 125 ms (Master fragt Knoten round-robin ab)
- SAMPLE_EVENT je Knoten gemäß aktueller geditherter Periode (~8 kHz)
```
Empfehlung: globale Zeit in **Ticks** (96 MHz-Basis) als `int64_t`, damit dieselbe
Einheit wie die Hardware. Sample- und Sync-Events in eine zeitsortierte Queue (oder
einfach: feiner Zeitschritt + Schwellwertvergleich, wenn das robuster zu schreiben
ist — Korrektheit vor Eleganz).

Laufzeit konfigurierbar: kurze Läufe (Sekunden) für Einschwingen, lange Läufe
(simulierte Stunden) für Drift-Stabilität.

---

## 6. Auszugebende Kennzahlen (CSV)

Mindestens diese CSV-Dateien:

**`run_timeseries.csv`** — Zeitreihe (gesampelt, nicht jeder Tick):
```
t_ms, node_id, s_offset_ns, s_rate_ppb, sample_k, true_ppm, ntp_err_ns
```

**`run_pairwise.csv`** — knotenübergreifender Versatz (die Kernkennzahl):
```
t_ms, node_a, node_b, index_skew_samples, time_skew_ns
```

**`run_summary.csv`** — ein Datensatz je Lauf (für Parametersweeps):
```
seed, jitter_model, sigma, n_nodes, runtime_s, dither_mode,
  max_index_skew, rms_time_skew_ns, lock_time_s, stable(bool)
```

**`dither_jitter.csv`** — Sampletakt-Periodenfolge eines Knotens (für FFT/⚠C4):
```
sample_k, period_ticks
```

Das Python-Plotskript (optional) liest diese und erzeugt: Versatz(t),
Einschwingkurve `s_rate_ppb`(t), FFT des Dither-Jitters, Stackup-Histogramm.

---

## 7. Meilensteine (schrittweiser Bauplan)

Jeder Meilenstein ist lauffähig und hat ein **Abnahme-Ergebnis**. Nach jedem
anhalten und Kennzahlen zeigen.

### M0 — Gerüst
- **Bauen:** Verzeichnisstruktur (§3), Makefile, leere Module, `README.md`.
  `sim_main` startet, liest `sim_config.h`, gibt eine Dummy-CSV aus.
- **Abnahme:** `make` läuft fehlerfrei, ein Lauf erzeugt eine CSV-Datei.

### M1 — Eine Knoten-Uhr, kein Sync
- **Bauen:** `sync_core` mit Uhr-Formel; ein Knoten mit ppm-Offset läuft frei.
- **Abnahme:** `ntp_err_ns` wächst linear mit der Zeit gemäß ppm-Offset
  (Plausibilitätstest der Zeitbasis).

### M2 — Schleife A (Uhr-Sync), ein Knoten
- **Bauen:** SYNC_EVENT + PI-Regler (aus `ntp_sync.c`), Jitter-Modell `gauss`.
- **Abnahme:** `ntp_err_ns` konvergiert nach ≤2 Syncs gegen ~0; `s_rate_ppb`
  nähert sich dem wahren ppm-Offset. **Lock-Zeit messen.** (Validiert ⚠C-Basis.)

### M3 — Schleife B (Sampletakt) + Index, ein Knoten
- **Bauen:** SAMPLE_EVENT + Dithering, gekoppelt an `s_rate_ppb`.
- **Abnahme:** mittlere Sampleperiode entspricht der Master-Rate (kein
  Index-Weglaufen gegen die Master-Zeitachse über lange Läufe).

### M4 — N Knoten, gemeinsamer Start, Kernfrage
- **Bauen:** N Knoten (Ziel 7), GO/Index-Anker, `run_pairwise.csv`.
- **Abnahme:** **`index_skew_samples` bleibt über Stunden < 1.** Das ist die
  zentrale Machbarkeitsaussage. Falls nicht → Konzept gebrochen, Ursache benennen.

### M5 — Stresstests (die Risiken)
- **Bauen:** Jitter-Modelle `heavy_tail`/`load_dep`/`biased`, Thermik-Drift
  Varianten b/c, rauschförmiges Dither-Alternativ.
- **Abnahme (je Test eine Aussage):**
  - ⚠C1/⚠C2: bleibt die A→B-Kaskade stabil oder pendelt sie? (Einschwingkurve)
  - ⚠A2/⚠C3: folgt die Nachführung der Thermik-Drift, Versatz < 1 Sample?
  - ⚠B2: ab welchem Jitter-sigma / welcher Schwere bricht Index-Sync? (Sweep)
  - ⚠C4: FFT-Vergleich Bresenham- vs. Rausch-Dither — Spurious Tones sichtbar?
  - ⚠B1/⚠B4/⚠G3: Verteilung des time_skew, **Worst-Case** statt Mittelwert.

### M6 — Fehlerfälle (optional, ⚠D1)
- **Bauen:** Ereignismodell GO-Verlust / Reboot / Sample-Verlust + Master-
  Konsistenz-Check.
- **Abnahme:** Check erkennt den falsch verankerten Strom und flaggt ihn (statt
  stiller Fehlkorrelation).

### M7 — Auswertung & Bericht
- **Bauen:** Parametersweep-Treiber (mehrere Seeds × Modelle → `run_summary.csv`),
  Plot-Skript, kurzer Ergebnis-`REPORT.md`.
- **Abnahme:** `REPORT.md` beantwortet je Risiko-ID: bestanden / Grenze / gebrochen,
  mit Kennzahl und Plot-Verweis.

---

## 8. Grenzen der Simulation (verbindlich dokumentieren)

`REPORT.md` MUSS einen Abschnitt enthalten, der klarstellt, was die Sim **nicht**
beweist:

- **⚠F1 (TCC0-PERBUF on-the-fly)** — Hardwareverhalten, simulativ nicht
  entscheidbar. Bleibt Scope-Nachweis am realen Board.
- **⚠E1/⚠E2 (PLCA-Buslast/-Schwebung)** — nur grob abschätzbar; das Jitter-Modell
  ist eine Annahme, keine Bus-Simulation.
- **⚠B2 (reale Jitter-Statistik)** — die Sim liefert eine **Robustheitsgrenze**
  („bricht ab sigma=X"), nicht den realen sigma. Den misst erst die Hardware.
- **⚠G1** — die Sim reduziert das Integrationsrisiko, hebt es nicht auf.

**Korrekte Schlussfolgerung der Sim:** Sie kann das Konzept **falsifizieren** (wenn
es schon im idealisierten Modell bricht) und seine **Robustheitsgrenzen** kartieren.
Sie kann es **nicht** abschließend verifizieren. Ein bestandener Sim-Lauf heißt:
„Die Regelungs-/Zeitlogik trägt unter den angenommenen Stimuli" — die nächste Stufe
ist dann der Hardware-Prototyp für ⚠F1 und die realen Stimuli.

---

## 9. Erste Schritte für den Agenten (konkret)

1. `SYNC_ADC_KONZEPT.md` lesen (Kontext + Risiko-IDs).
2. `ntp_sync.c` und `hwclk_cli.c` im Repo lesen; PI-Regler-Konstanten und
   Sampletakt-Rechnung **exakt** extrahieren (KI_DEN, Kp, ns/Tick-Faktor, PER_NOM).
   Falls die Werte von den Annahmen in §4.3/§4.4 abweichen, die **Firmware-Werte
   gewinnen** — diese Spec entsprechend kommentieren.
3. M0 bauen, anhalten, Gerüst zeigen.
4. M1–M7 der Reihe nach, nach jedem Meilenstein Kennzahlen zeigen und auf Freigabe
   warten.
5. Determinismus von Anfang an: Seed in `sim_config.h`, in jede `run_summary`-Zeile
   loggen.

> **Erinnerung an den Agenten:** Ziel ist eine ehrliche Machbarkeitsaussage, kein
> grünes Dashboard. Wenn das Konzept in M4 oder M5 bricht, ist das ein **wertvolles**
> Ergebnis — sauber dokumentieren, Ursache benennen, nicht die Parameter
> schönschrauben, bis es passt.
