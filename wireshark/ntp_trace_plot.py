#!/usr/bin/env python3
"""
ntp_trace_plot.py - render diagrams from a real `ntp watch` convergence trace
(into ./img/ntp_trace_*.png). The trace is embedded verbatim below.

    pip install matplotlib
    python ntp_trace_plot.py
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

TRACE = r"""
[18:37:32.310] offset -1782405437.761 s mean 0 ns         drift    +0 ppm  delay 905.617 us
[18:37:33.480] offset 307.193 us   mean 411.325 us   drift +1404 ppm  delay 761.504 us
[18:37:34.649] offset 255.144 us   mean 272.109 us   drift +1858 ppm  delay 912.281 us
[18:37:35.811] offset 166.132 us   mean 185.557 us   drift +1898 ppm  delay 611.233 us
[18:37:36.980] offset -80.133 us   mean 120.606 us   drift +1643 ppm  delay 564.515 us
[18:37:38.150] offset -113.751 us  mean 25.125 us    drift +1744 ppm  delay 673.516 us
[18:37:39.313] offset 119.124 us   mean -5.626 us    drift +1771 ppm  delay 757.530 us
[18:37:40.482] offset -122.185 us  mean -11.215 us   drift +1737 ppm  delay 785.085 us
[18:37:41.653] offset -121.441 us  mean 18.356 us    drift +1882 ppm  delay 478.153 us
[18:37:42.823] offset 109.586 us   mean 19.423 us    drift +1996 ppm  delay 1.001 ms
[18:37:43.996] offset -391.603 us  mean -7.397 us    drift +1660 ppm  delay 719.088 us
[18:37:45.179] offset 228.738 us   mean 15.496 us    drift +1939 ppm  delay 953.616 us
[18:37:46.350] offset -22.924 us   mean -18.046 us   drift +1634 ppm  delay 674.312 us
[18:37:47.521] offset -82.982 us   mean -32.670 us   drift +1547 ppm  delay 726.448 us
[18:37:48.686] offset 113.029 us   mean 13.140 us    drift +1841 ppm  delay 759.750 us
[18:37:49.858] offset 63.420 us    mean -13.972 us   drift +1746 ppm  delay 1.011 ms
[18:37:51.028] offset 100.750 us   mean 6.109 us     drift +1713 ppm  delay 662.380 us
[18:37:52.211] offset 29.509 us    mean 13.544 us    drift +1725 ppm  delay 678.099 us
[18:37:53.380] offset -19.511 us   mean -5.303 us    drift +1760 ppm  delay 399.451 us
[18:37:54.549] offset -325.749 us  mean -1.538 us    drift +1719 ppm  delay 842.386 us
[18:37:55.717] offset -156.266 us  mean 10.241 us    drift +1842 ppm  delay 583.637 us
[18:37:56.884] offset 40.923 us    mean 10.504 us    drift +1861 ppm  delay 818.867 us
[18:37:58.055] offset 277.262 us   mean 21.432 us    drift +2045 ppm  delay 1.145 ms
[18:37:59.231] offset -143.697 us  mean 17 ns        drift +1713 ppm  delay 783.913 us
[18:38:00.402] offset 208.580 us   mean 2.577 us     drift +1875 ppm  delay 844.316 us
[18:38:01.565] offset -64.503 us   mean -13.674 us   drift +1672 ppm  delay 428.532 us
[18:38:02.742] offset 170.471 us   mean -9.805 us    drift +1907 ppm  delay 893.367 us
[18:38:03.912] offset -96.811 us   mean -11.390 us   drift +1553 ppm  delay 513.464 us
[18:38:05.078] offset 150.253 us   mean 3.844 us     drift +1922 ppm  delay 879.817 us
[18:38:06.248] offset -89.507 us   mean 12.971 us    drift +1847 ppm  delay 648.404 us
[18:38:07.413] offset 7.265 us     mean -3.228 us    drift +1863 ppm  delay 707.052 us
[18:38:08.593] offset -33.305 us   mean 8.080 us     drift +1661 ppm  delay 484.782 us
[18:38:09.768] offset 97.459 us    mean -17.350 us   drift +1688 ppm  delay 538.398 us
[18:38:10.939] offset 87.396 us    mean -2.027 us    drift +1820 ppm  delay 991.267 us
[18:38:12.108] offset -141.911 us  mean -7.645 us    drift +1762 ppm  delay 885.184 us
[18:38:13.277] offset -9.547 us    mean 14.934 us    drift +1875 ppm  delay 711.385 us
[18:38:14.447] offset 268.524 us   mean 17.051 us    drift +1925 ppm  delay 1.161 ms
[18:38:15.622] offset 91.930 us    mean -54 ns       drift +1825 ppm  delay 753.783 us
[18:38:16.794] offset 199.463 us   mean 18.527 us    drift +2018 ppm  delay 668.168 us
[18:38:17.965] offset 25.354 us    mean 4.319 us     drift +1937 ppm  delay 767.137 us
[18:38:19.138] offset 151.258 us   mean 13.300 us    drift +2109 ppm  delay 968.987 us
[18:38:20.311] offset 116.491 us   mean 18.362 us    drift +2074 ppm  delay 807.987 us
[18:38:21.478] offset 199.782 us   mean 2.827 us     drift +2053 ppm  delay 670.836 us
[18:38:22.640] offset 225.514 us   mean 14.423 us    drift +2122 ppm  delay 871.501 us
[18:38:23.818] offset -48.412 us   mean -12.549 us   drift +1926 ppm  delay 789.521 us
[18:38:24.993] offset -38.480 us   mean 5.142 us     drift +2140 ppm  delay 669.207 us
[18:38:26.165] offset -143.565 us  mean -12.650 us   drift +1880 ppm  delay 441.704 us
[18:38:27.338] offset 49.792 us    mean -14.969 us   drift +1921 ppm  delay 469.836 us
[18:38:28.498] offset 94.795 us    mean 10.137 us    drift +2070 ppm  delay 869.754 us
[18:38:29.666] offset -67.379 us   mean -20.263 us   drift +1864 ppm  delay 608.269 us
[18:38:30.836] offset -1.649 us    mean 6.406 us     drift +1966 ppm  delay 757.088 us
[18:38:32.008] offset 176.149 us   mean 8.959 us     drift +2041 ppm  delay 700.070 us
[18:38:33.184] offset -71.147 us   mean -14.156 us   drift +1873 ppm  delay 1.013 ms
[18:38:34.348] offset -189.611 us  mean -1.740 us    drift +1836 ppm  delay 565.550 us
[18:38:35.528] offset -170.291 us  mean -12.560 us   drift +1783 ppm  delay 521.453 us
[18:38:36.707] offset 161.909 us   mean -6.306 us    drift +1945 ppm  delay 825.085 us
[18:38:37.875] offset 2.983 us     mean 13.769 us    drift +2053 ppm  delay 829.823 us
[18:38:39.050] offset -231.470 us  mean 11.582 us    drift +1986 ppm  delay 899.574 us
[18:38:40.226] offset 208.262 us   mean 7.959 us     drift +1893 ppm  delay 742.166 us
[18:38:41.400] offset -166.462 us  mean -14.406 us   drift +1746 ppm  delay 457.870 us
[18:38:42.564] offset -28.539 us   mean -17.161 us   drift +1816 ppm  delay 695.083 us
[18:38:43.734] offset 150.255 us   mean -323 ns      drift +1977 ppm  delay 713.435 us
[18:38:44.905] offset -172.850 us  mean -1.457 us    drift +1869 ppm  delay 709.038 us
[18:38:46.076] offset 228.060 us   mean 27.994 us    drift +2128 ppm  delay 871.670 us
[18:38:47.240] offset -49.064 us   mean -4.548 us    drift +1753 ppm  delay 791.734 us
[18:38:48.414] offset 144.169 us   mean -3.226 us    drift +1933 ppm  delay 759.078 us
[18:38:49.581] offset 53.140 us    mean -2.532 us    drift +1831 ppm  delay 751.083 us
[18:38:50.758] offset 26.614 us    mean -14.450 us   drift +1923 ppm  delay 677.086 us
[18:38:51.935] offset -114.641 us  mean 10.885 us    drift +1894 ppm  delay 775.070 us
[18:38:53.100] offset -143.853 us  mean -9.795 us    drift +1796 ppm  delay 479.752 us
[18:38:54.268] offset 42.535 us    mean 4.865 us     drift +1895 ppm  delay 443.318 us
[18:38:55.447] offset 12.724 us    mean -487 ns      drift +1919 ppm  delay 821.869 us
[18:38:56.621] offset -147.116 us  mean -9.018 us    drift +1775 ppm  delay 1.038 ms
[18:38:57.794] offset 55.739 us    mean 6.029 us     drift +1879 ppm  delay 893.169 us
[18:38:58.969] offset 93.934 us    mean 1.935 us     drift +1923 ppm  delay 809.552 us
[18:39:00.142] offset -114.190 us  mean -12.785 us   drift +1740 ppm  delay 610.135 us
[18:39:01.325] offset -84.456 us   mean 2.035 us     drift +1797 ppm  delay 637.820 us
[18:39:02.501] offset -88.800 us   mean -3.450 us    drift +1823 ppm  delay 679.504 us
[18:39:03.669] offset -184.301 us  mean -17.160 us   drift +1674 ppm  delay 522.118 us
[18:39:04.842] offset -120.624 us  mean 5.924 us     drift +1811 ppm  delay 773.919 us
[18:39:06.015] offset 130.598 us   mean -1.761 us    drift +1763 ppm  delay 703.115 us
[18:39:07.181] offset 120.363 us   mean -1.339 us    drift +1797 ppm  delay 820.583 us
[18:39:08.353] offset 43.910 us    mean 8.054 us     drift +1782 ppm  delay 539.583 us
[18:39:09.519] offset 287.079 us   mean 25.074 us    drift +2150 ppm  delay 1.174 ms
[18:39:10.686] offset 165.055 us   mean 10.469 us    drift +1903 ppm  delay 863.133 us
[18:39:11.851] offset -11.932 us   mean 183 ns       drift +1798 ppm  delay 500.967 us
[18:39:13.017] offset 241.119 us   mean 19.958 us    drift +2054 ppm  delay 1.007 ms
[18:39:14.185] offset -75.407 us   mean -22.513 us   drift +1842 ppm  delay 832.570 us
[18:39:15.367] offset 208.405 us   mean 21.390 us    drift +2190 ppm  delay 1.205 ms
[18:39:16.570] offset -150.629 us  mean -2.735 us    drift +1756 ppm  delay 538.868 us
[18:39:17.749] offset -152.364 us  mean -26.772 us   drift +1671 ppm  delay 616.267 us
[18:39:18.917] offset -40.873 us   mean 3.162 us     drift +1863 ppm  delay 666.636 us
[18:39:20.103] offset -384.801 us  mean -31.378 us   drift +1750 ppm  delay 851.573 us
[18:39:28.008] offset 446.353 us   mean 34.688 us    drift +1838 ppm  delay 795.402 us
[18:39:29.177] offset -282.410 us  mean 3.017 us     drift +1638 ppm  delay 504.053 us
[18:39:30.353] offset -316.201 us  mean 36.647 us    drift +1836 ppm  delay 920.690 us
[18:39:31.526] offset 136.673 us   mean 45.634 us    drift +2026 ppm  delay 1.257 ms
[18:39:32.715] offset -35.721 us   mean 3.988 us     drift +1887 ppm  delay 917.671 us
[18:39:33.884] offset -65.274 us   mean 8.641 us     drift +1750 ppm  delay 544.784 us
[18:39:35.050] offset 18.903 us    mean -3.306 us    drift +1787 ppm  delay 738.199 us
[18:39:36.218] offset -125.086 us  mean -18.804 us   drift +1762 ppm  delay 493.652 us
[18:39:37.390] offset 149.439 us   mean 2.036 us     drift +1907 ppm  delay 968.350 us
[18:39:38.567] offset -12.704 us   mean 1.198 us     drift +1761 ppm  delay 376.817 us
[18:39:39.748] offset 36.147 us    mean 5.481 us     drift +1862 ppm  delay 720.052 us
[18:39:40.914] offset -10.484 us   mean 14.416 us    drift +1966 ppm  delay 606.820 us
[18:39:42.088] offset 5.368 us     mean -9.491 us    drift +1785 ppm  delay 482.401 us
[18:39:43.276] offset -166.105 us  mean 1.302 us     drift +1790 ppm  delay 1.017 ms
[18:39:44.452] offset 212.488 us   mean 6.144 us     drift +1952 ppm  delay 973.868 us
[18:39:45.641] offset -66.203 us   mean -3.643 us    drift +1920 ppm  delay 1.286 ms
[18:39:46.811] offset -51.532 us   mean -7.389 us    drift +1689 ppm  delay 630.867 us
[18:39:47.982] offset 96.269 us    mean 7.612 us     drift +1893 ppm  delay 757.334 us
[18:39:49.162] offset 136.008 us   mean -1.408 us    drift +1932 ppm  delay 1.071 ms
[18:39:50.337] offset -122.384 us  mean -3.310 us    drift +1874 ppm  delay 880.404 us
[18:39:51.505] offset 33.917 us    mean 13.734 us    drift +1871 ppm  delay 552.251 us
[18:39:52.679] offset -79.692 us   mean 6.481 us     drift +1975 ppm  delay 957.273 us
[18:39:53.847] offset -297.595 us  mean -6.881 us    drift +1828 ppm  delay 1.076 ms
[18:39:55.010] offset -58.673 us   mean -1.307 us    drift +1845 ppm  delay 531.153 us
[18:39:56.174] offset -21.565 us   mean 10.215 us    drift +2004 ppm  delay 809.472 us
[18:39:57.339] offset 132.577 us   mean 4.824 us     drift +2035 ppm  delay 854.801 us
[18:39:58.513] offset -152.687 us  mean 1.468 us     drift +1845 ppm  delay 791.420 us
[18:39:59.684] offset 87.732 us    mean 7.887 us     drift +1950 ppm  delay 721.603 us
[18:40:00.853] offset -77.382 us   mean -7.133 us    drift +1902 ppm  delay 731.455 us
[18:40:02.017] offset 154.702 us   mean -6.651 us    drift +1941 ppm  delay 793.334 us
"""

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

# the first row is the one-shot phase lock (offset in seconds) -> exclude from µs plots
good = np.abs(off) < 1e6          # |offset| < 1 s, in µs
tg, offg, meang, driftg, delayg = t[good], off[good], mean[good], drift[good], delay[good]

# steady-state window (after the ~5 s frequency lock-in)
ss = tg >= 6.0
print("steady-state (t>=6s):")
print(f"  offset  mean={offg[ss].mean():+.1f} us  std={offg[ss].std():.1f} us  "
      f"min={offg[ss].min():.1f}  max={offg[ss].max():.1f}")
print(f"  mean-col mean={meang[ss].mean():+.2f} us  std={meang[ss].std():.1f} us")
print(f"  drift   mean={driftg[ss].mean():.0f} ppm  std={driftg[ss].std():.0f} ppm")
print(f"  delay   mean={delayg[ss].mean():.0f} us  min={delayg[ss].min():.0f}")


# 1) overview: offset + mean (left axis, µs) and drift (right axis, ppm) over time
def fig_overview():
    fig, axL = plt.subplots(figsize=(9.2, 4.6))
    axL.axhline(0, color="0.6", lw=0.8)
    axL.plot(tg, offg, ".", ms=4, color="#bbbbbb", label="offset (raw, per sync)")
    axL.plot(tg, meang, "-", lw=2.2, color="#d62728", label="mean (last 16) → ~0")
    axL.set_xlabel("time since first sync (s)")
    axL.set_ylabel("offset / mean (µs)", color="#444")
    axL.set_ylim(-450, 500)
    axR = axL.twinx()
    axR.plot(tg, driftg, "-", lw=1.6, color="#1f77b4", alpha=0.85, label="drift (locked freq, ppm)")
    axR.set_ylabel("drift (ppm)", color="#1f77b4")
    axR.set_ylim(0, 2300); axR.tick_params(axis="y", labelcolor="#1f77b4")
    axL.axvspan(0, 6, color="#fff3cd", alpha=0.6)
    axL.text(3, 430, "frequency\nlock-in", ha="center", fontsize=9, color="#9a7d0a")
    # mark the missed-sync gap
    gi = int(np.argmax(np.diff(tg))) if len(tg) > 2 else 0
    if np.diff(tg).max() > 3:
        gx = (tg[gi] + tg[gi + 1]) / 2
        axL.axvspan(tg[gi], tg[gi + 1], color="#e0e0e0", alpha=0.6)
        axL.text(gx, -380, "missed\nsyncs", ha="center", fontsize=8, color="0.4")
    l1, lab1 = axL.get_legend_handles_labels()
    l2, lab2 = axR.get_legend_handles_labels()
    axL.legend(l1 + l2, lab1 + lab2, loc="upper right", fontsize=8.5)
    axL.set_title("ntp watch: raw offset stays at the jitter floor, mean → 0, drift locks in")
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_trace_overview.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_trace_overview.png")


# 2) zoom on the first ~8 s: the lock-in
def fig_lockin():
    z = tg <= 8.5
    fig, axL = plt.subplots(figsize=(7.6, 4.2))
    axL.axhline(0, color="0.6", lw=0.8)
    axL.plot(tg[z], offg[z], ".-", ms=6, lw=0.6, color="#bbbbbb", label="offset (raw)")
    axL.plot(tg[z], meang[z], "o-", ms=4, lw=2.2, color="#d62728", label="mean (last 16)")
    axL.set_xlabel("time since first sync (s)"); axL.set_ylabel("offset / mean (µs)")
    axR = axL.twinx()
    axR.plot(tg[z], driftg[z], "s-", ms=4, lw=1.8, color="#1f77b4", label="drift (ppm)")
    axR.set_ylabel("drift (ppm)", color="#1f77b4"); axR.set_ylim(0, 2300)
    axR.tick_params(axis="y", labelcolor="#1f77b4")
    axL.annotate("mean 411 → ~0 µs", xy=(5, 25), xytext=(4.2, 300),
                 fontsize=9, color="#d62728", arrowprops=dict(arrowstyle="->", color="#d62728"))
    axL.annotate("drift 0 → ~1850 ppm", xy=(2.3, 0), xytext=(3.0, -150),
                 fontsize=9, color="#1f77b4", arrowprops=dict(arrowstyle="->", color="#1f77b4"))
    l1, lab1 = axL.get_legend_handles_labels(); l2, lab2 = axR.get_legend_handles_labels()
    axL.legend(l1 + l2, lab1 + lab2, loc="lower right", fontsize=8.5)
    axL.set_title("Lock-in (first ~8 s): frequency converges, mean collapses")
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_trace_lockin.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_trace_lockin.png")


# 3) steady-state distribution: raw offset (wide) vs rolling mean (tight, ~0)
def fig_hist():
    fig, ax = plt.subplots(figsize=(7.6, 4.2))
    bins = np.linspace(-450, 450, 31)
    ax.hist(offg[ss], bins=bins, color="#bbbbbb", alpha=0.8,
            label=f"raw offset (σ={offg[ss].std():.0f} µs)")
    ax.hist(meang[ss], bins=bins, color="#d62728", alpha=0.8,
            label=f"rolling mean (σ={meang[ss].std():.0f} µs, ⟨⟩={meang[ss].mean():+.1f})")
    ax.axvline(0, color="0.4", lw=1)
    ax.set_xlabel("value (µs)"); ax.set_ylabel("count (steady state, t ≥ 6 s)")
    ax.set_title("Steady state: raw offset = jitter floor; the mean is tight and ~0 (unbiased)")
    ax.legend(loc="upper right", fontsize=9)
    fig.tight_layout(); fig.savefig(os.path.join(IMG, "ntp_trace_hist.png"), bbox_inches="tight")
    plt.close(fig); print("wrote img/ntp_trace_hist.png")


if __name__ == "__main__":
    fig_overview()
    fig_lockin()
    fig_hist()
    print("done.")
