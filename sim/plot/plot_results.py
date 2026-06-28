#!/usr/bin/env python3
"""plot_results.py - visualise the sync-ADC simulation CSVs.

Reads whatever CSVs are present in the run directory and draws the relevant
panels, so it is useful from M2 onward (it simply skips panels whose CSV is
not there yet):

  run_timeseries.csv  -> ntp_err(t) per node ; s_rate_ppb(t) settling curve   (M2+)
  run_pairwise.csv    -> index_skew(t) ; time_skew(t) ; time_skew histogram    (M4+)
  dither_jitter.csv   -> period sequence + FFT (spurious-tone check, C4)        (M3/M5)

Usage:
  python plot/plot_results.py [run_dir]      (default: current dir)
Outputs PNGs into <run_dir>/plot/ and, if a display is available, shows them.

Pure stdlib + matplotlib (+ numpy if available for the FFT; falls back to a
plain DFT note if numpy is missing).
"""
import csv
import os
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")  # file output always works; no display needed
    import matplotlib.pyplot as plt
except Exception as e:  # pragma: no cover
    print("matplotlib required: pip install matplotlib", file=sys.stderr)
    raise

try:
    import numpy as np
    HAVE_NUMPY = True
except Exception:
    HAVE_NUMPY = False


def read_csv(path):
    if not os.path.isfile(path):
        return None
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return rows if rows else None


def fnum(row, key):
    try:
        return float(row[key])
    except (KeyError, ValueError, TypeError):
        return None


def plot_timeseries(rows, outdir):
    by_node = defaultdict(lambda: defaultdict(list))
    for r in rows:
        nid = r.get("node_id", "0")
        for k in ("t_ms", "s_offset_ns", "s_rate_ppb", "sample_k", "true_ppm", "ntp_err_ns"):
            v = fnum(r, k)
            if v is not None:
                by_node[nid][k].append(v)

    # ntp_err(t)
    fig, ax = plt.subplots(figsize=(10, 4))
    for nid, d in sorted(by_node.items()):
        if d["t_ms"] and d["ntp_err_ns"]:
            ax.plot([t / 1000 for t in d["t_ms"]], [e / 1000 for e in d["ntp_err_ns"]],
                    label=f"node {nid}", lw=0.9)
    ax.set_xlabel("t [s]"); ax.set_ylabel("ntp_err [us]")
    ax.set_title("Clock error vs master (Loop A) - should converge to ~0")
    ax.axhline(0, color="k", lw=0.5); ax.grid(alpha=0.3); ax.legend(fontsize=7, ncol=2)
    save(fig, outdir, "ts_ntp_err.png")

    # s_rate_ppb(t) settling curve
    fig, ax = plt.subplots(figsize=(10, 4))
    for nid, d in sorted(by_node.items()):
        if d["t_ms"] and d["s_rate_ppb"]:
            ax.plot([t / 1000 for t in d["t_ms"]], [p / 1000 for p in d["s_rate_ppb"]],
                    label=f"node {nid}", lw=0.9)
        if d["t_ms"] and d["true_ppm"]:
            ax.plot([t / 1000 for t in d["t_ms"]], [-p for p in d["true_ppm"]],
                    "k--", lw=0.5, alpha=0.5)
    ax.set_xlabel("t [s]"); ax.set_ylabel("s_rate_ppb [ppm]")
    ax.set_title("Frequency integral s_rate_ppb (I term) - settles to -true_ppm (dashed)")
    ax.grid(alpha=0.3); ax.legend(fontsize=7, ncol=2)
    save(fig, outdir, "ts_rate_ppb.png")


def plot_pairwise(rows, outdir):
    by_pair = defaultdict(lambda: defaultdict(list))
    for r in rows:
        pair = (r.get("node_a", "?"), r.get("node_b", "?"))
        for k in ("t_ms", "index_skew_samples", "time_skew_ns"):
            v = fnum(r, k)
            if v is not None:
                by_pair[pair][k].append(v)

    # index skew (THE headline metric, M4)
    fig, ax = plt.subplots(figsize=(10, 4))
    for pair, d in sorted(by_pair.items()):
        if d["t_ms"] and d["index_skew_samples"]:
            ax.plot([t / 1000 for t in d["t_ms"]], d["index_skew_samples"],
                    label=f"{pair[0]}-{pair[1]}", lw=0.8)
    ax.axhline(1, color="r", ls="--", lw=1, label="1 sample (limit)")
    ax.axhline(-1, color="r", ls="--", lw=1)
    ax.set_xlabel("t [s]"); ax.set_ylabel("index skew [samples]")
    ax.set_title("Pairwise index skew - MUST stay |.|<1 (core feasibility metric)")
    ax.grid(alpha=0.3); ax.legend(fontsize=6, ncol=3)
    save(fig, outdir, "pw_index_skew.png")

    # time skew + histogram (stackup, B1/B4/G3)
    all_skew = []
    fig, ax = plt.subplots(figsize=(10, 4))
    for pair, d in sorted(by_pair.items()):
        if d["t_ms"] and d["time_skew_ns"]:
            ax.plot([t / 1000 for t in d["t_ms"]], [s / 1000 for s in d["time_skew_ns"]],
                    lw=0.6, alpha=0.7)
            all_skew += d["time_skew_ns"]
    ax.set_xlabel("t [s]"); ax.set_ylabel("time skew [us]")
    ax.set_title("Pairwise time skew")
    ax.grid(alpha=0.3)
    save(fig, outdir, "pw_time_skew.png")

    if all_skew:
        fig, ax = plt.subplots(figsize=(7, 4))
        ax.hist([s / 1000 for s in all_skew], bins=60, color="steelblue")
        worst = max(abs(min(all_skew)), abs(max(all_skew))) / 1000
        ax.set_xlabel("time skew [us]"); ax.set_ylabel("count")
        ax.set_title(f"Time-skew distribution (worst |.| = {worst:.2f} us)")
        ax.grid(alpha=0.3)
        save(fig, outdir, "pw_time_skew_hist.png")


def plot_dither(rows, outdir):
    per = [fnum(r, "period_ticks") for r in rows]
    per = [p for p in per if p is not None]
    if not per:
        return
    fig, ax = plt.subplots(figsize=(10, 3))
    ax.plot(per[:2000], lw=0.5)
    ax.set_xlabel("sample_k"); ax.set_ylabel("period [ticks]")
    ax.set_title("Sample-clock period sequence (Bresenham vs noise dither)")
    ax.grid(alpha=0.3)
    save(fig, outdir, "dither_seq.png")

    # FFT of the period jitter - the C4 spurious-tone check
    if HAVE_NUMPY and len(per) >= 256:
        x = np.array(per, dtype=float)
        x = x - x.mean()
        n = 1 << (len(x).bit_length() - 1)   # largest power of two <= len
        x = x[:n]
        win = np.hanning(n)
        spec = np.abs(np.fft.rfft(x * win))
        freq = np.fft.rfftfreq(n, d=1.0)     # cycles per sample
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.semilogy(freq[1:], spec[1:] + 1e-9, lw=0.7)
        ax.set_xlabel("frequency [cycles/sample]"); ax.set_ylabel("|FFT| (log)")
        ax.set_title("Dither-jitter spectrum - discrete spikes = spurious tones (C4)")
        ax.grid(alpha=0.3, which="both")
        save(fig, outdir, "dither_fft.png")
    elif not HAVE_NUMPY:
        print("  (numpy not installed -> skipping dither FFT; pip install numpy)")


def plot_summary(rows, outdir):
    """Sweep summary: max_index_skew vs sigma (the B2 breaking-point curve),
    aggregated over seeds for the gauss jitter model."""
    from collections import defaultdict
    pts = defaultdict(list)   # sigma_us -> [skew, ...] for gauss runs
    for r in rows:
        if r.get("jitter_model") != "gauss":
            continue
        if r.get("drift_model", "none") not in ("none", ""):
            continue
        if r.get("dither_mode", "bresenham") != "bresenham":
            continue
        sg = fnum(r, "sigma"); sk = fnum(r, "max_index_skew")
        if sg is None or sk is None or sk < 0:
            continue
        pts[sg / 1000.0].append(sk)
    if len(pts) < 2:
        return
    xs = sorted(pts)
    mean = [sum(pts[x]) / len(pts[x]) for x in xs]
    lo = [min(pts[x]) for x in xs]; hi = [max(pts[x]) for x in xs]
    fig, ax = plt.subplots(figsize=(9, 5))
    ax.fill_between(xs, lo, hi, alpha=0.2, label="min..max over seeds")
    ax.plot(xs, mean, "o-", label="mean max-skew")
    ax.axhline(1, color="r", ls="--", label="1 sample (limit)")
    # interpolate the sigma where mean skew == 1
    for i in range(1, len(xs)):
        if mean[i-1] < 1 <= mean[i]:
            frac = (1 - mean[i-1]) / (mean[i] - mean[i-1])
            sbreak = xs[i-1] + frac * (xs[i] - xs[i-1])
            ax.axvline(sbreak, color="g", ls=":", label=f"break ~{sbreak:.0f} us")
            break
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("sync offset-jitter sigma [us]"); ax.set_ylabel("max index skew [samples]")
    ax.set_title("B2: index skew vs sync jitter - where does <1 sample break?")
    ax.grid(alpha=0.3, which="both"); ax.legend()
    save(fig, outdir, "sweep_skew_vs_sigma.png")


def save(fig, outdir, name):
    path = os.path.join(outdir, name)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print(f"  wrote {path}")


def main():
    run_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    outdir = os.path.join(run_dir, "plot")
    os.makedirs(outdir, exist_ok=True)
    print(f"reading CSVs from {run_dir}/, writing PNGs to {outdir}/")

    did = False
    ts = read_csv(os.path.join(run_dir, "run_timeseries.csv"))
    if ts:
        plot_timeseries(ts, outdir); did = True
    else:
        print("  (no run_timeseries.csv yet - M2 produces it)")

    pw = read_csv(os.path.join(run_dir, "run_pairwise.csv"))
    if pw:
        plot_pairwise(pw, outdir); did = True
    else:
        print("  (no run_pairwise.csv yet - M4 produces it)")

    dj = read_csv(os.path.join(run_dir, "dither_jitter.csv"))
    if dj:
        plot_dither(dj, outdir); did = True
    else:
        print("  (no dither_jitter.csv yet - M3/M5 produce it)")

    sm = read_csv(os.path.join(run_dir, "run_summary.csv"))
    if sm and len(sm) > 2:
        plot_summary(sm, outdir); did = True

    print("done." if did else "nothing to plot yet.")


if __name__ == "__main__":
    main()
