#!/usr/bin/env python3
"""
ntp_convergence_diagrams.py - render the figures for NTP_TWO_NODE_CONVERGENCE.md
with matplotlib (PNG into ./img/). Pure illustration; numbers match the doc.

    pip install matplotlib
    python ntp_convergence_diagrams.py
"""
import os
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "img")
os.makedirs(IMG, exist_ok=True)
plt.rcParams.update({"font.size": 11, "axes.grid": True, "grid.alpha": 0.3,
                     "figure.dpi": 130, "savefig.dpi": 130})

C_A, C_B, C_RND, C_FLOOR = "#1f77b4", "#d62728", "#1f77b4", "#7f7f7f"


def save(fig, name):
    fig.tight_layout()
    p = os.path.join(IMG, name)
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print("wrote", os.path.relpath(p, HERE))


# 1) t1/t2/t3/t4 exchange (sequence sketch) --------------------------------------
def fig_exchange():
    fig, ax = plt.subplots(figsize=(7.2, 4.3))
    xA, xB = 0.18, 0.82
    ax.plot([xA, xA], [0.05, 0.95], color="0.4", lw=1.5)
    ax.plot([xB, xB], [0.05, 0.95], color="0.4", lw=1.5)
    ax.text(xA, 0.99, "Knoten A (Anfrager)", ha="center", va="bottom", fontweight="bold")
    ax.text(xB, 0.99, "Knoten B", ha="center", va="bottom", fontweight="bold")
    # REQUEST  t1 -> t2 ; REPLY t3 -> t4   (time runs downward)
    y_t1, y_t2, y_t3, y_t4 = 0.84, 0.66, 0.50, 0.30
    ax.annotate("", xy=(xB, y_t2), xytext=(xA, y_t1),
                arrowprops=dict(arrowstyle="-|>", color=C_A, lw=2))
    ax.annotate("", xy=(xA, y_t4), xytext=(xB, y_t3),
                arrowprops=dict(arrowstyle="-|>", color=C_B, lw=2))
    ax.text((xA + xB) / 2, (y_t1 + y_t2) / 2 + 0.03, "REQUEST  (sendet t1)",
            ha="center", color=C_A, fontsize=10)
    ax.text((xA + xB) / 2, (y_t3 + y_t4) / 2 - 0.05, "REPLY  (t1, t2, t3)",
            ha="center", color=C_B, fontsize=10)
    for x, y, t, ha in [(xA, y_t1, "t1", "right"), (xB, y_t2, "t2", "left"),
                        (xB, y_t3, "t3", "left"), (xA, y_t4, "t4", "right")]:
        ax.plot(x, y, "o", color="0.2", ms=5)
        dx = -0.02 if ha == "right" else 0.02
        ax.text(x + dx, y, t, ha=ha, va="center", fontweight="bold")
    ax.text(0.5, 0.10,
            r"$\theta=\dfrac{(t_2-t_1)+(t_3-t_4)}{2}$   (Uhr$_B$ - Uhr$_A$)"
            "\n"
            r"$\delta=(t_4-t_1)-(t_3-t_2)$   (Round-Trip)",
            ha="center", va="center", fontsize=11,
            bbox=dict(boxstyle="round", fc="#eef", ec="#88a"))
    ax.set_title("NTP-Austausch: ein Zeitstempel-Quartett")
    ax.set_xlim(0, 1); ax.set_ylim(0, 1.05); ax.axis("off")
    save(fig, "ntp_exchange.png")


# 2) drift sawtooth vs frequency discipline --------------------------------------
def fig_sawtooth():
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    ppm, T = 1600e-6, 250.0     # 1600 ppm, sync interval 250 ms
    t = [i for i in range(0, 1001)]
    saw = [ppm * (ti % T) / 1.0 * 1000.0 for ti in t]   # us: ppm * (ms within interval) -> us
    ax.plot(t, saw, color=C_B, lw=2, label="nur Positions-Sprung (Sägezahn)")
    ax.axhline(0, color="#2ca02c", lw=2.2, label="mit Frequenzregelung (Holdover < 1 µs)")
    for s in (250, 500, 750, 1000):
        ax.axvline(s, color="0.6", ls=":", lw=1)
        ax.annotate("sync", xy=(s, 0), xytext=(s, -55), ha="center", color="0.4", fontsize=8)
    ax.axhline(400, color=C_B, ls="--", lw=0.8, alpha=0.6)
    ax.text(20, 408, "Spitze 400 µs", color=C_B, fontsize=9)
    ax.axhline(200, color=C_B, ls="--", lw=0.8, alpha=0.4)
    ax.text(20, 208, "Mittel ~200 µs", color=C_B, fontsize=9)
    ax.set_title("Drift-Sägezahn (1600 ppm) vs. Frequenz-Disziplinierung")
    ax.set_xlabel("Zeit (ms)"); ax.set_ylabel("Uhrfehler (µs)")
    ax.set_xlim(0, 1000); ax.set_ylim(-70, 440)
    ax.legend(loc="upper right", fontsize=9)
    save(fig, "ntp_sawtooth.png")


# 3) 1/sqrt(N) convergence with plateau ------------------------------------------
def fig_convergence():
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    sigma, floor = 200.0, 20.0
    N = list(range(1, 1200))
    rnd = [sigma / math.sqrt(n) for n in N]
    eff = [max(r, floor) for r in rnd]
    ax.plot(N, rnd, color=C_RND, ls="--", lw=1.5, label=r"Zufallsanteil $\sigma/\sqrt{N}$  (σ≈200 µs)")
    ax.axhline(floor, color=C_FLOOR, ls="--", lw=1.5, label="Asymmetrie-/Transit-Boden")
    ax.plot(N, eff, color="#ff7f0e", lw=2.5, label="effektiver Fehler = max(beider)")
    ax.axvline(100, color="0.7", ls=":", lw=1)
    ax.annotate("Plateau erreicht\n(weiteres Mitteln bringt nichts)", xy=(160, floor),
                xytext=(330, 70), fontsize=9,
                arrowprops=dict(arrowstyle="->", color="0.4"))
    ax.set_title("Konvergenz: σ/√N bis zum nicht mittelbaren Boden")
    ax.set_xlabel("Anzahl gemittelter Samples N"); ax.set_ylabel("Offset-Unsicherheit (µs)")
    ax.set_xscale("log"); ax.set_xlim(1, 1200); ax.set_ylim(0, 210)
    ax.legend(loc="upper right", fontsize=9)
    save(fig, "ntp_convergence.png")


# 4) consensus: both clocks toward the midpoint ----------------------------------
def fig_consensus():
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    k = list(range(0, 7))
    A = [600 * (0.5 ** i) for i in k]
    B = [-600 * (0.5 ** i) for i in k]
    ax.plot(k, A, "o-", color=C_A, lw=2, label="Knoten A")
    ax.plot(k, B, "o-", color=C_B, lw=2, label="Knoten B")
    ax.axhline(0, color="0.4", lw=1.2, ls="-")
    ax.text(6, 12, "Mittenzeit", color="0.4", ha="right", fontsize=9)
    for i in k[:-1]:
        ax.annotate("", xy=(i + 1, A[i + 1]), xytext=(i, A[i]),
                    arrowprops=dict(arrowstyle="->", color=C_A, alpha=0.4))
    ax.set_title("Konsens: je ½ pro Runde → beide treffen sich in der Mitte")
    ax.set_xlabel("Sync-Runde"); ax.set_ylabel("Abweichung von der Mittenzeit (µs)")
    ax.set_xlim(-0.2, 6.2); ax.set_ylim(-680, 680)
    ax.legend(loc="upper right", fontsize=9)
    save(fig, "ntp_consensus.png")


# 5) PI / consensus control-loop block diagram -----------------------------------
def fig_loop():
    fig, ax = plt.subplots(figsize=(8.6, 4.2))
    ax.set_xlim(0, 10); ax.set_ylim(0, 6); ax.axis("off")

    def box(x, y, w, h, text, fc="#eaf2fb", ec="#3a6ea5"):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.08",
                                    fc=fc, ec=ec, lw=1.6))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=9.5)
        return (x, y, w, h)

    def arrow(p1, p2, label="", color="#444", off=0.18):
        a = FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=14,
                            color=color, lw=1.6, shrinkA=2, shrinkB=2)
        ax.add_patch(a)
        if label:
            ax.text((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2 + off, label,
                    ha="center", va="bottom", fontsize=8.5, color=color)

    ex = box(0.3, 3.6, 2.0, 1.0, "NTP-Austausch\nθ, δ")
    fil = box(3.0, 3.6, 2.2, 1.0, "Min-Delay-Filter\n+ robustes Mittel")
    pi = box(5.9, 3.6, 1.9, 1.0, "PI-Regler\n(Offset + Drift)")
    pos = box(8.0, 4.5, 1.8, 0.9, "Positions-Korr.\noffset_ns")
    frq = box(8.0, 2.7, 1.8, 0.9, "Frequenz-Korr.\nΔf")
    clk = box(5.9, 0.9, 1.9, 1.0, "disziplinierte Uhr\nntp_now_ns()")
    q = box(3.0, 1.0, 2.2, 0.9, "Güte Λ\nδ_min/2 + σ/√N + |Δf|·t", fc="#eef0ff", ec="#7a7ad0")

    arrow((ex[0] + ex[2], 4.1), (fil[0], 4.1))
    arrow((fil[0] + fil[2], 4.1), (pi[0], 4.1))
    arrow((pi[0] + pi[2], 4.3), (pos[0], 4.9), "Kp·θ/2 (Konsens)")
    arrow((pi[0] + pi[2], 3.9), (frq[0], 3.1), "Ki·θ")
    arrow((pos[0] + pos[2] / 2, pos[1]), (clk[0] + clk[2], clk[1] + clk[3] * 0.7), color="#2a8")
    arrow((frq[0] + frq[2] / 2, frq[1]), (clk[0] + clk[2], clk[1] + clk[3] * 0.4), color="#2a8")
    arrow((clk[0], clk[1] + clk[3] / 2), (ex[0] + ex[2] / 2, ex[1]), "nächster Austausch")
    arrow((fil[0] + fil[2] / 2, fil[1]), (q[0] + q[2] / 2, q[1] + q[3]), color="#7a7ad0", off=-0.0)

    ax.set_title("Regelkreis: Austausch → Filter → PI (Offset+Frequenz) → Uhr → Güte", fontsize=11)
    save(fig, "ntp_loop.png")


if __name__ == "__main__":
    fig_exchange()
    fig_sawtooth()
    fig_convergence()
    fig_consensus()
    fig_loop()
    print("done.")
