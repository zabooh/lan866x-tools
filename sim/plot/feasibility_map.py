#!/usr/bin/env python3
"""feasibility_map.py - the sampling-rate vs sync-jitter feasibility map.

Drives sim.exe over a grid of (sigma, sample_rate), records the worst-case max
index skew, and draws the map: x = sync offset-jitter sigma, y = sample rate.
The "max index skew = 1 sample" contour is the feasibility boundary - below/left
of it the per-sample index stays aligned (<1 sample), above/right it breaks.

Boundaries are drawn for two controllers:
  firmware  : Ki=1/4, Kp=1            (current firmware)
  tuned     : Ki=1/128, Kp=1/8        (REPORT.md section 5)

Run from sim/:  python plot/feasibility_map.py [--quick]
Caches the grid to feasibility.csv (delete it to recompute). --quick = coarse grid.
"""
import csv, os, subprocess, sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SIM = "./sim.exe" if os.name == "nt" else "./sim"
QUICK = "--quick" in sys.argv

FS   = [500, 1000, 2000, 4000, 8000, 16000]                 # sample rates [Hz]
SIG  = [5,10,20,40,80,150,300,600,1000] if not QUICK else [10,40,150,600]  # sigma [us]
SEEDS = (1, 2) if not QUICK else (1,)
RUNTIME = "120"
CTRL = {"firmware": [], "tuned": ["--kiden", "128", "--kp", "0.125"]}
CSV = "feasibility.csv"


def run_max_skew(fs, sigma_us, ctrl_args):
    """worst-case max index skew over seeds (gauss jitter)."""
    w = 0.0
    for s in SEEDS:
        out = subprocess.run(
            [SIM, "--summaryonly", "--runtime", RUNTIME, "--samplehz", str(fs),
             "--sigma", str(int(sigma_us*1000)), "--seed", str(s)] + ctrl_args,
            capture_output=True, text=True).stdout
        for line in out.splitlines():
            if "max |index_skew|" in line:
                w = max(w, float(line.split("max |index_skew| =")[1].split("samples")[0]))
    return w


def compute():
    rows = []
    total = len(CTRL)*len(FS)*len(SIG); n = 0
    for ctrl, args in CTRL.items():
        for fs in FS:
            for sg in SIG:
                sk = run_max_skew(fs, sg, args)
                rows.append({"controller": ctrl, "fs_hz": fs, "sigma_us": sg, "max_skew": sk})
                n += 1; print(f"\r  {n}/{total}  {ctrl} fs={fs} sigma={sg}us -> {sk:.2f}    ", end="")
    print()
    with open(CSV, "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=["controller","fs_hz","sigma_us","max_skew"])
        wr.writeheader(); wr.writerows(rows)
    return rows


def load():
    if os.path.isfile(CSV):
        with open(CSV) as f:
            return list(csv.DictReader(f))
    return None


def boundary(rows, ctrl):
    """for each fs, interpolate the sigma where max_skew crosses 1.0."""
    xs, ys = [], []
    by_fs = {}
    for r in rows:
        if r["controller"] != ctrl: continue
        by_fs.setdefault(int(r["fs_hz"]), []).append((float(r["sigma_us"]), float(r["max_skew"])))
    for fs, pts in sorted(by_fs.items()):
        pts.sort()
        prev = None
        sb = None
        for sg, sk in pts:
            if prev and prev[1] < 1 <= sk:
                f = (1 - prev[1]) / (sk - prev[1]); sb = prev[0] + f*(sg - prev[0]); break
            prev = (sg, sk)
        if sb: xs.append(sb); ys.append(fs)
    return xs, ys


def main():
    rows = load() or compute()

    fig, ax = plt.subplots(figsize=(9, 6))
    # heatmap of the firmware controller's max skew (current reality)
    fsv = sorted({int(r["fs_hz"]) for r in rows})
    sgv = sorted({float(r["sigma_us"]) for r in rows})
    Z = np.full((len(fsv), len(sgv)), np.nan)
    for r in rows:
        if r["controller"] != "firmware": continue
        i = fsv.index(int(r["fs_hz"])); j = sgv.index(float(r["sigma_us"]))
        Z[i, j] = float(r["max_skew"])
    X, Y = np.meshgrid(sgv, fsv)
    pc = ax.pcolormesh(X, Y, np.clip(Z, 0.05, 10), shading="nearest",
                       norm=matplotlib.colors.LogNorm(0.05, 10), cmap="RdYlGn_r")
    cb = fig.colorbar(pc, ax=ax); cb.set_label("max index skew [samples] (firmware)")

    for ctrl, style in (("firmware", "k-"), ("tuned", "b--")):
        xs, ys = boundary(rows, ctrl)
        if xs: ax.plot(xs, ys, style, lw=2.5, label=f"skew=1 boundary ({ctrl})")

    ax.axvline(150, color="purple", ls=":", lw=1.5, label="real sigma ~150 us")
    ax.axhline(8000, color="gray", ls=":", lw=1.5, label="8 kHz design rate")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("sync offset-jitter sigma [us]")
    ax.set_ylabel("sample rate [Hz]")
    ax.set_title("Feasibility map: which sample rate is possible at which sync jitter\n"
                 "(below/left of a boundary = index stays <1 sample)")
    ax.legend(fontsize=8, loc="lower left")
    ax.grid(alpha=0.3, which="both")
    out = os.path.join(os.path.dirname(__file__), "feasibility_map.png")
    fig.tight_layout(); fig.savefig(out, dpi=120); print("wrote", out)


if __name__ == "__main__":
    main()
