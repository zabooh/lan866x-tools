# Sync-ADC-Machbarkeitssimulation

PC-Simulation (C99), die die **Regelungs- und Zeitlogik** des Konzepts für
verteiltes synchrones ADC-Sampling (`SYNC_ADC_KONZEPT.md`) auf Machbarkeit prüft,
**bevor** Hardware-Aufwand entsteht — gemäß dem Arbeitsauftrag in
`SIMULATION_SPEC.md`.

Sie beantwortet die Kernfrage: *Bleiben die Sample-Indizes von N Knoten über
lange Laufzeit ausgerichtet (< 1 Sample)*, wenn jeder Knoten einen eigenen,
driftenden Oszillator hat und nur alle 125 ms einen verrauschten Sync bekommt?

## Firmware-treuer Kern

`sync_core.{h,c}` ist der **firmware-taugliche** Teil (kein Heap, kein OS, C99).
Seine Konstanten und Gleichungen sind **1:1 aus der echten Firmware übernommen**,
nicht erfunden (Spec §9.2 „Firmware gewinnt"):

| Größe | Wert | Firmware-Quelle |
|---|---|---|
| Loop-A Integral-Verstärkung | `Ki = 1/4` (`NTP_KI_DEN`) | `ntp_sync.c:64` |
| Loop-A Proportional-Verstärkung | `Kp = 1` (voller Phasenschritt) | `ntp_sync.c:364` |
| Uhr-Lesen | `now = raw + s_offset_ns + rate_held` | `ntp_sync.c:111` |
| Sampleperiode | `96e6/freq` Ticks; 8 kHz → 12000 (Reg 11999) | `hwclk_cli.c:765` |
| Tick | `125/12 ns` | `hwclk_cli.c hwclock_now_ns` |

**Zwei Abweichungen von der Konzept-Doku (Firmware/Physik gewinnen, siehe `sync_core.h`):**
1. `s_rate_ppb` ist für einen schnellen (+ppm) Oszillator **negativ**, daher nutzt
   Loop B `per_ideal = PER_NOM*(1 - s_rate_ppb/1e9)` (Konzept §5.3 hatte `+`).
2. `PER_NOM = 12000` ist die Perioden*länge*; das HW-Register hält Länge−1.

## Bauen & ausführen

`gcc` + (`mingw32-make` unter Windows / `make` sonst):

```sh
mingw32-make            # -> sim.exe
mingw32-make run        # Lauf mit Defaults, schreibt CSVs hierher
./sim --help            # alle Optionen (nodes, runtime, seed, sigma, out ...)
```

Ohne make: `gcc -std=c99 -O2 sim_main.c sync_core.c sim_noise.c -o sim -lm`

## Ausgabe-CSVs (Spec §6)

| Datei | Inhalt | ab |
|---|---|---|
| `run_summary.csv` | eine Zeile je Lauf (für Sweeps) | M0 |
| `run_timeseries.csv` | `t_ms,node_id,s_offset_ns,s_rate_ppb,sample_k,true_ppm,ntp_err_ns` | M2 |
| `run_pairwise.csv` | `t_ms,node_a,node_b,index_skew_samples,time_skew_ns` | M4 |
| `dither_jitter.csv` | `sample_k,period_ticks` (ein Knoten, für FFT/⚠C4) | M3 |
| `run_faultcheck.csv` | `t_ms,node_id,reported_index,expected_index,skew_samples,flagged` | M6 |

## Plots

```sh
python plot/plot_results.py [lauf_verzeichnis]     # Default: aktuelles Verzeichnis
```
Liest die vorhandenen CSVs und schreibt PNGs nach `<lauf_verzeichnis>/plot/`:
`ts_ntp_err.png`, `ts_rate_ppb.png` (M2), `pw_index_skew.png`,
`pw_time_skew*.png` (M4), `dither_seq.png`, `dither_fft.png` (M3/M5),
`sweep_skew_vs_sigma.png` (M7-Sweep). Braucht `matplotlib` (und `numpy` für das
FFT-Panel).

## Meilensteine (Spec §7) — alle abgeschlossen; siehe `REPORT.md`

- **M0** ✅ Gerüst: baut, Dummy-CSV
- **M1** ✅ frei laufende Uhr (ntp_err linear mit ppm, 0,00 % Steigungsfehler)
- **M2** ✅ Loop-A-Sync (ntp_err ms→µs, s_rate_ppb→−true_ppm, Lock ≤1,5 s)
- **M3** ✅ Loop-B-Sampletakt (mittlere Periode 125000,00 ns, Runaway < 0,01 Smp)
- **M4** ✅ N Knoten, gemeinsamer Start: **index_skew 0,061 < 1 Sample** (Kernergebnis)
- **M5** ✅ Stress: σ-Sweep bricht bei **σ≈28 µs**; Modelle/Drift/Dither charakterisiert
- **M6** ✅ Fehlerfälle (GO-Verlust/Reboot/DMA-Overflow) vom Konsistenz-Check erkannt
- **M7** ✅ Sweep-Treiber (`sweep.sh`) + Bruchgrenzen-Plot + `REPORT.md`

**Kernaussage:** Regelungs-/Zeitlogik solide; die „<1 Sample"-Zusage ist
jitter-limitiert auf **σ ≲ 28 µs** — per-Sample-Sync braucht also PTP/HW-
Timestamping. Vollständige Analyse und der verbindliche Abschnitt „was die Sim
*nicht* beweist" stehen in `REPORT.md`.

## Was diese Sim NICHT entscheiden kann (Spec §8)

TCC0-PERBUF on-the-fly (⚠F1), reale PLCA-Bus-Dynamik (⚠E1/E2), ADC-Wandelqualität
(⚠F3). Sie kann das Konzept **falsifizieren** und seine **Robustheitsgrenzen**
kartieren; sie kann es **nicht** abschließend verifizieren. Ein bestandener Lauf
bedeutet „die Regelungs-/Zeitlogik trägt unter den angenommenen Stimuli", nicht
„die Hardware funktioniert".
