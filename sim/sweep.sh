#!/usr/bin/env bash
# sweep.sh - M7 parameter-sweep driver (SIMULATION_SPEC.md §7 M7).
# Runs the sim across seeds x sigma x jitter/drift/dither models, accumulating
# one row per run into <out>/run_summary.csv (deterministic; seed in each row).
#
#   ./sweep.sh [out_dir]      (default: sweep_results)
#
# Uses --summaryonly so only the summary row is written per run (fast).
set -euo pipefail
SIM=./sim.exe
OUT="${1:-sweep_results}"
mkdir -p "$OUT"
rm -f "$OUT/run_summary.csv"

SEEDS="1 2 3"
# sigma sweep (gauss) - the B2 breaking-point curve, in ns
SIGMAS="2000 5000 10000 20000 33000 50000 100000 150000 500000"
# jitter models at a fixed, still-passing sigma (10 us) - relative model effect
MODELS="gauss heavy_tail load_dep biased"

echo "sweep -> $OUT/run_summary.csv"
n=0
for seed in $SEEDS; do
  for s in $SIGMAS; do
    $SIM --summaryonly --append --out "$OUT" --seed "$seed" --sigma "$s" --jitter gauss >/dev/null
    n=$((n+1))
  done
  for m in $MODELS; do
    $SIM --summaryonly --append --out "$OUT" --seed "$seed" --sigma 10000 --jitter "$m" --bias 50000 >/dev/null
    n=$((n+1))
  done
  # drift + dither variants at low sigma (isolate those effects)
  for d in none sine ramp; do
    $SIM --summaryonly --append --out "$OUT" --seed "$seed" --sigma 2000 --drift "$d" >/dev/null
    n=$((n+1))
  done
  for dz in bresenham noise; do
    $SIM --summaryonly --append --out "$OUT" --seed "$seed" --sigma 2000 --dither "$dz" >/dev/null
    n=$((n+1))
  done
done
echo "$n runs done -> $OUT/run_summary.csv ($(wc -l < "$OUT/run_summary.csv") lines incl header)"
