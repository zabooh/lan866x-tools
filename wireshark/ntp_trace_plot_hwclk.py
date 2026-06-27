#!/usr/bin/env python3
"""
ntp_trace_plot_hwclk.py - render the *new* convergence diagrams after the hardware
time base (XOSC1 -> DPLL1 -> TC2 @ 96 MHz) replaced the open-loop DFLL SYS_TIME.

Generates ./img/ntp_trace_overview_hwclk.png, ntp_trace_holdover_hwclk.png and the
old-vs-new comparison ntp_drift_compare.png. The trace below is a real `ntp watch`
capture taken from a fresh boot while `lan866x-ntpsync` ran, with the PC sync
deliberately PAUSED for ~31 s (40..70 s) so the holdover/drift is visible. Network
traffic was captured in parallel with tshark on "Ethernet 8" (udp port 30491).

    pip install matplotlib
    python ntp_trace_plot_hwclk.py

Keep the original DFLL-era script ntp_trace_plot.py and its PNGs for the comparison.
"""
import os, re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "img")
os.makedirs(IMG, exist_ok=True)
plt.rcParams.update({"font.size": 11, "axes.grid": True, "grid.alpha": 0.3,
                     "figure.dpi": 130, "savefig.dpi": 130})

# --- NEW trace: NTP counter now riding the disciplined HW clock (TC2 @ 96 MHz) ---
TRACE = r"""
[21:35:21.610] offset -13.350 us   mean -13.350 us   drift   -10 ppm  delay 848.311 us
[21:35:22.799] offset -79.543 us   mean 33.256 us    drift  +140 ppm  delay 1.020 ms
[21:35:23.980] offset 158.666 us   mean 5.322 us     drift   +37 ppm  delay 719.824 us
[21:35:25.155] offset 309.394 us   mean 13.794 us    drift  +147 ppm  delay 839.871 us
[21:35:26.331] offset 53.891 us    mean -1.644 us    drift   -36 ppm  delay 492.917 us
[21:35:27.510] offset -32.794 us   mean -11.555 us   drift   -16 ppm  delay 653.644 us
[21:35:28.690] offset 389.837 us   mean 22.403 us    drift  +338 ppm  delay 1.292 ms
[21:35:29.871] offset 37.857 us    mean -9.349 us    drift   +18 ppm  delay 607.525 us
[21:35:31.047] offset 4.210 us     mean 1.732 us     drift   -17 ppm  delay 594.306 us
[21:35:32.225] offset -159.759 us  mean 2.228 us     drift    +8 ppm  delay 680.239 us
[21:35:33.406] offset 197.557 us   mean -20.763 us   drift   +51 ppm  delay 1.070 ms
[21:35:34.580] offset -115.372 us  mean -3.395 us    drift   -29 ppm  delay 766.102 us
[21:35:35.760] offset 193.721 us   mean 11.606 us    drift  +139 ppm  delay 755.562 us
[21:35:36.944] offset 83.292 us    mean 8.749 us     drift  +124 ppm  delay 865.382 us
[21:35:38.132] offset -108.076 us  mean -9.349 us    drift   -71 ppm  delay 503.221 us
[21:35:39.325] offset 74.894 us    mean 7.456 us     drift   +75 ppm  delay 766.702 us
[21:35:40.514] offset 59.611 us    mean -6.856 us    drift   +48 ppm  delay 639.462 us
[21:35:41.689] offset 5.530 us     mean -5.637 us    drift   +50 ppm  delay 682.454 us
[21:35:42.863] offset -222.082 us  mean -6.195 us    drift  -155 ppm  delay 594.023 us
[21:35:44.037] offset -47.053 us   mean -1.052 us    drift   +62 ppm  delay 1.537 ms
[21:35:45.222] offset -42.574 us   mean -6.080 us    drift   -32 ppm  delay 487.467 us
[21:35:46.403] offset -36.934 us   mean 4.526 us     drift  +110 ppm  delay 989.304 us
[21:35:47.586] offset -157.626 us  mean 5.902 us     drift   -77 ppm  delay 562.244 us
[21:35:48.763] offset 143.130 us   mean 268 ns       drift   +60 ppm  delay 725.724 us
[21:35:49.938] offset -128.559 us  mean 6.455 us     drift   +47 ppm  delay 602.184 us
[21:35:51.118] offset 352.036 us   mean 10.110 us    drift  +243 ppm  delay 881.562 us
[21:35:52.289] offset -23.867 us   mean 3.674 us     drift   -30 ppm  delay 427.314 us
[21:35:53.459] offset 220.613 us   mean 4.855 us     drift  +125 ppm  delay 783.451 us
[21:35:54.639] offset -236.919 us  mean 851 ns       drift   +56 ppm  delay 679.818 us
[21:35:55.816] offset 39.735 us    mean -20.410 us   drift   -36 ppm  delay 539.706 us
[21:35:56.998] offset -128.165 us  mean 840 ns       drift   -22 ppm  delay 391.624 us
[21:35:58.170] offset 138.246 us   mean -5.525 us    drift   +47 ppm  delay 624.919 us
[21:35:59.344] offset 162.402 us   mean 2.662 us     drift   +93 ppm  delay 741.125 us
[21:36:00.521] offset 29.936 us    mean 3.617 us     drift   +12 ppm  delay 656.513 us
[21:36:01.694] offset 287.441 us   mean 15.274 us    drift  +183 ppm  delay 965.461 us
[21:36:32.746] offset -4.902 ms    mean -295.745 us  drift  +143 ppm  delay 797.006 us
[21:36:33.933] offset 218.236 us   mean -292.042 us  drift  +172 ppm  delay 1.240 ms
[21:36:35.111] offset -4.316 us    mean -296.665 us  drift   +77 ppm  delay 557.055 us
[21:36:36.291] offset 103.007 us   mean -303.378 us  drift   +27 ppm  delay 632.782 us
[21:36:37.461] offset 115.707 us   mean -6.274 us    drift   +47 ppm  delay 778.468 us
[21:36:38.635] offset 15.338 us    mean -15.054 us   drift   -40 ppm  delay 481.113 us
[21:36:39.812] offset 93.842 us    mean -1.930 us    drift   +48 ppm  delay 613.828 us
[21:36:40.989] offset -237.407 us  mean -7.203 us    drift   -71 ppm  delay 665.719 us
[21:36:42.168] offset -48.517 us   mean 995 ns       drift   +63 ppm  delay 756.187 us
[21:36:43.338] offset -176.605 us  mean -2.654 us    drift   -73 ppm  delay 564.704 us
[21:36:44.512] offset 118.820 us   mean 1.178 us     drift   +66 ppm  delay 1.045 ms
[21:36:45.682] offset 89.270 us    mean 7.369 us     drift   +26 ppm  delay 600.524 us
[21:36:46.861] offset 185.743 us   mean 3.637 us     drift  +106 ppm  delay 732.213 us
[21:36:48.046] offset 55.331 us    mean 6.559 us     drift    +9 ppm  delay 757.718 us
[21:36:49.226] offset -186.781 us  mean -12.049 us   drift  -106 ppm  delay 429.210 us
[21:36:50.405] offset 214.021 us   mean 2.895 us     drift   +57 ppm  delay 795.071 us
[21:36:51.581] offset 32.846 us    mean -10.343 us   drift   -36 ppm  delay 500.618 us
[21:36:52.767] offset 451.965 us   mean 20.288 us    drift  +271 ppm  delay 897.561 us
[21:36:53.944] offset -129.469 us  mean 15.188 us    drift   +90 ppm  delay 1.035 ms
[21:36:55.114] offset 24.816 us    mean -11.543 us   drift  -107 ppm  delay 647.845 us
[21:36:56.297] offset 151.526 us   mean 13.864 us    drift  +138 ppm  delay 646.713 us
[21:36:57.474] offset -74.443 us   mean -20.470 us   drift    -9 ppm  delay 597.342 us
[21:36:58.651] offset 213.895 us   mean 14.228 us    drift  +279 ppm  delay 1.356 ms
[21:36:59.829] offset -64.332 us   mean 3.328 us     drift   -66 ppm  delay 556.398 us
[21:37:01.011] offset -151.160 us  mean -12.125 us   drift   -24 ppm  delay 820.525 us
"""

# DFLL-era reference numbers (from ntp_trace_plot.py / NTP_SYNC.md §4.4)
OLD_DRIFT_MEAN, OLD_DRIFT_STD = 1864.0, 126.0
OLD_OFFSET_STD, OLD_MEAN_MEAN = 151.6, 1.4

U = {"ns": 1e-3, "us": 1.0, "ms": 1e3, "s": 1e6}   # -> microseconds
ROW = re.compile(
    r"\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\]\s+offset\s+(-?[\d.]+)\s+(ns|us|ms|s)\s+"
    r"mean\s+(-?[\d.]+)\s+(ns|us|ms|s)\s+drift\s+([+-]?\d+)\s+ppm\s+"
    r"delay\s+(-?[\d.]+)\s+(ns|us|ms|s)")

t, off, mean, drift, delay = [], [], [], [], []
t0 = None
for line in TRACE.strip().splitlines():
    m = ROW.search(line)
    if not m:
        continue
    hh, mm, ss, ms = (int(m.group(i)) for i in range(1, 5))
    sec = hh * 3600 + mm * 60 + ss + ms / 1000.0
    if t0 is None:
        t0 = sec
    t.append(sec - t0)
    off.append(float(m.group(5)) * U[m.group(6)])
    mean.append(float(m.group(7)) * U[m.group(8)])
    drift.append(int(m.group(9)))
    delay.append(float(m.group(10)) * U[m.group(11)])

t = np.array(t); off = np.array(off); mean = np.array(mean)
drift = np.array(drift); delay = np.array(delay)

# the post-gap row carries the holdover error (~ -4.9 ms); find the gap
dt = np.diff(t)
gap_i = int(np.argmax(dt))
gap_len = dt[gap_i]
holdover_us = off[gap_i + 1]            # offset at the first sync after the gap
print(f"gap: {gap_len:.1f} s  -> post-gap offset (holdover) = {holdover_us/1000:.3f} ms "
      f"= {holdover_us/gap_len:.0f} ppm effective over the gap")

# steady state = exclude the lock-in (<6 s) and the post-gap spike row
ss = (t >= 6.0) & (np.abs(off) < 2000)   # |offset| < 2 ms drops the holdover spike
print("steady-state (t>=6s, excl. holdover spike):")
print(f"  offset  mean={off[ss].mean():+.1f} us  std={off[ss].std():.1f} us")
print(f"  mean-col std={mean[ss].std():.1f} us")
print(f"  drift   mean={drift[ss].mean():+.0f} ppm  std={drift[ss].std():.0f} ppm")
print(f"  delay   mean={delay[ss].mean():.0f} us  min={delay[ss].min():.0f}")


def fig_overview():
    fig, axL = plt.subplots(figsize=(9.2, 4.6))
    axL.axhline(0, color="0.6", lw=0.8)
    om = np.abs(off) < 2000          # hide the -4.9 ms holdover spike from the µs axis
    axL.plot(t[om], off[om], ".", ms=4, color="#bbbbbb", label="offset (raw, per sync)")
    axL.plot(t[om], mean[om], "-", lw=2.0, color="#d62728", label="mean (last 16) → ~0")
    axL.set_xlabel("time since first sync (s)")
    axL.set_ylabel("offset / mean (µs)", color="#444"); axL.set_ylim(-450, 500)
    axR = axL.twinx()
    axR.plot(t, drift, "-", lw=1.5, color="#1f77b4", alpha=0.85, label="drift (locked freq, ppm)")
    axR.axhline(OLD_DRIFT_MEAN, color="#1f77b4", ls=":", lw=1.2, alpha=0.7)
    axR.text(t[-1], OLD_DRIFT_MEAN, " old DFLL ~1864 ppm", va="center", ha="right",
             fontsize=8, color="#1f77b4")
    axR.set_ylabel("drift (ppm)", color="#1f77b4"); axR.set_ylim(-300, 2000)
    axR.tick_params(axis="y", labelcolor="#1f77b4")
    axL.axvspan(0, 6, color="#fff3cd", alpha=0.6)
    axL.text(3, 430, "lock-in", ha="center", fontsize=9, color="#9a7d0a")
    axL.axvspan(t[gap_i], t[gap_i + 1], color="#e0e0e0", alpha=0.7)
    axL.text((t[gap_i] + t[gap_i + 1]) / 2, -380, "PC sync\nPAUSED", ha="center", fontsize=8, color="0.4")
    l1, lab1 = axL.get_legend_handles_labels(); l2, lab2 = axR.get_legend_handles_labels()
    axL.legend(l1 + l2, lab1 + lab2, loc="upper right", fontsize=8.5)
    axL.set_title("HW clock (TC2 @ 96 MHz): drift now hugs ~+40 ppm (was ~1864) — mean still → 0")
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_trace_overview_hwclk.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_trace_overview_hwclk.png")


def fig_holdover():
    """Zoom on the paused-sync window: the clock free-runs and the offset jumps."""
    fig, ax = plt.subplots(figsize=(8.0, 4.2))
    z = (t >= t[gap_i] - 6) & (t <= t[gap_i + 1] + 8)
    ax.axhline(0, color="0.6", lw=0.8)
    ax.plot(t[z], off[z] / 1000.0, "o-", ms=5, lw=1.0, color="#d62728")
    ax.axvspan(t[gap_i], t[gap_i + 1], color="#e0e0e0", alpha=0.7)
    ax.annotate(f"{gap_len:.0f}s without sync →\noffset jumps {holdover_us/1000:.1f} ms\n"
                f"(≈{abs(holdover_us/gap_len):.0f} ppm: the frozen software\nrate term, not the oscillator)",
                xy=(t[gap_i + 1], holdover_us / 1000.0), xytext=(t[gap_i] - 5, holdover_us / 1000.0 * 0.6),
                fontsize=9, color="#d62728", arrowprops=dict(arrowstyle="->", color="#d62728"))
    ax.set_xlabel("time since first sync (s)"); ax.set_ylabel("offset (ms)")
    ax.set_title("Holdover (PC sync paused ~31 s): the clock free-runs, then re-locks in one sync")
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_trace_holdover_hwclk.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_trace_holdover_hwclk.png")


def fig_drift_compare():
    """The headline: the drift the loop must fight, old DFLL vs new HW clock."""
    new_mean, new_std = drift[ss].mean(), drift[ss].std()
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    labels = ["old: SYS_TIME\n(open-loop DFLL)", "new: TC2 96 MHz\n(XOSC1→DPLL1)"]
    means = [OLD_DRIFT_MEAN, abs(new_mean)]
    stds = [OLD_DRIFT_STD, new_std]
    bars = ax.bar(labels, means, yerr=stds, capsize=6,
                  color=["#bbbbbb", "#2ca02c"], width=0.55)
    ax.set_ylabel("|drift| the discipline must remove (ppm)")
    ax.set_title(f"Oscillator drift: {OLD_DRIFT_MEAN:.0f} ppm → {abs(new_mean):.0f} ppm "
                 f"(~{OLD_DRIFT_MEAN/max(abs(new_mean),1):.0f}× more stable)")
    for b, mν in zip(bars, means):
        ax.text(b.get_x() + b.get_width() / 2, mν + 30, f"{mν:.0f} ppm",
                ha="center", fontsize=11, fontweight="bold")
    ax.set_ylim(0, OLD_DRIFT_MEAN * 1.15)
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_drift_compare.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_drift_compare.png")


if __name__ == "__main__":
    fig_overview()
    fig_holdover()
    fig_drift_compare()
    print("done.")
