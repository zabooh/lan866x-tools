#!/usr/bin/env python3
"""
plot_timing.py - timing diagram for a lan866x-clickdemo run.

Reads the two logs that a clickdemo run produces and draws them on ONE shared
time axis:

  1. the demo's event log  (clickdemo-events.csv: epoch,rel_ms,event,sid,v1,v2,rc,lat_ms)
  2. the Wireshark capture  (*.pcapng: RCP requests :6800, replies :49153, RTP :5001)

Both clocks are the same machine's UTC clock (the CSV epoch == tshark
frame.time_epoch), so they line up 1:1. The SOME/IP session id (someip.sessionid)
== the CSV 'sid' column, so each app event matches its packet.

The headline insight is drawn explicitly: a THUMB_TMO/PROX_TMO whose reply IS
present on the wire is a *host drop* (gotcha #4) - those are marked in red and
connected to the wire reply the app never received.

Usage:
  python tools/plot_timing.py                       # newest logs/*.pcapng + default csv
  python tools/plot_timing.py --pcap logs/log.005.pcapng --csv release/clickdemo-events.csv
  python tools/plot_timing.py --from 13 --to 16     # zoom to a 3 s window (seconds from start)
  python tools/plot_timing.py --out logs/timing.png --show

Needs: matplotlib, and tshark (Wireshark) for the pcapng. No pyshark required.
"""
import argparse
import csv
import glob
import os
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


DEF_TSHARK = r"C:\Program Files\Wireshark\tshark.exe"


def find_tshark(arg):
    if arg and os.path.isfile(arg):
        return arg
    for c in (DEF_TSHARK, "tshark", "/usr/bin/tshark"):
        if os.path.isfile(c):
            return c
        # also accept a bare name resolvable on PATH
        from shutil import which
        w = which(c)
        if w:
            return w
    sys.exit("tshark not found - pass --tshark <path to tshark.exe>")


def newest_pcap():
    files = sorted(glob.glob("logs/*.pcapng")) + sorted(glob.glob("logs/*.pcap"))
    return files[-1] if files else None


def read_events(path):
    """Return list of dicts from the clickdemo event CSV."""
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                rows.append({
                    "epoch": float(r["epoch"]),
                    "event": r["event"],
                    "sid": int(r["sid"]),
                    "v1": int(r["v1"]),
                    "v2": int(r["v2"]),
                    "rc": int(r["rc"]),
                    "lat": float(r["lat_ms"]),
                })
            except (KeyError, ValueError):
                continue
    if not rows:
        sys.exit("no usable rows in %s" % path)
    return rows


def read_wire(tshark, pcap):
    """Run tshark once; return list of dicts for ports 5001/6800/49153."""
    fields = ["frame.time_epoch", "udp.srcport", "udp.dstport",
              "someip.methodid", "someip.sessionid"]
    cmd = [tshark, "-r", pcap, "-Y", "udp.port==5001 || udp.port==6800 || udp.port==49153",
           "-T", "fields", "-E", "separator=,"]
    for fld in fields:
        cmd += ["-e", fld]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    except subprocess.CalledProcessError as e:
        sys.exit("tshark failed:\n" + (e.stderr or ""))
    pkts = []
    for line in out.splitlines():
        c = line.split(",")
        if len(c) < 5 or not c[0]:
            continue
        try:
            t = float(c[0])
        except ValueError:
            continue
        src = int(c[1]) if c[1] else 0
        dst = int(c[2]) if c[2] else 0
        meth = int(c[3], 16) if c[3] else None
        sid = int(c[4], 16) if c[4] else None
        if dst == 5001:
            kind = "rtp"
        elif dst == 6800:
            kind = "req"
        elif src == 49153:
            kind = "resp"
        else:
            continue
        pkts.append({"t": t, "kind": kind, "meth": meth, "sid": sid})
    return pkts


# lane layout: name -> y
LANES = [
    ("wire RTP :5001", 8),
    ("wire reply ←:49153", 7),
    ("wire req →:6800", 6),
    ("app FRAME", 5),
    ("app PROX", 3),
    ("app THUMB", 1),
]
LANE_Y = dict(LANES)


def main():
    ap = argparse.ArgumentParser(description="Timing diagram for a clickdemo run.")
    ap.add_argument("--csv", default="release/clickdemo-events.csv")
    ap.add_argument("--pcap", default=None, help="default: newest logs/*.pcapng")
    ap.add_argument("--tshark", default=None)
    ap.add_argument("--out", default="logs/timing.png")
    ap.add_argument("--from", dest="t_from", type=float, default=None,
                    help="window start, seconds from run start")
    ap.add_argument("--to", dest="t_to", type=float, default=None,
                    help="window end, seconds from run start")
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()

    pcap = args.pcap or newest_pcap()
    if not pcap or not os.path.isfile(pcap):
        sys.exit("no pcap found - pass --pcap <file>")
    if not os.path.isfile(args.csv):
        sys.exit("no csv found - pass --csv <file>")
    tshark = find_tshark(args.tshark)

    events = read_events(args.csv)
    pkts = read_wire(tshark, pcap)

    # shared time origin (both are the same UTC clock)
    t0 = min(min(e["epoch"] for e in events), min(p["t"] for p in pkts))
    for e in events:
        e["x"] = e["epoch"] - t0
    for p in pkts:
        p["x"] = p["t"] - t0

    # host-drop detection: a TMO whose sid appears as a reply on the wire
    wire_resp_t = {}
    for p in pkts:
        if p["kind"] == "resp" and p["sid"] is not None:
            wire_resp_t.setdefault(p["sid"], p["x"])
    drops = []  # (event_x, wire_resp_x, lane_y, sensor)
    n_tmo = 0
    for e in events:
        if e["event"] in ("THUMB_TMO", "PROX_TMO"):
            n_tmo += 1
            lane = LANE_Y["app THUMB"] if e["event"].startswith("THUMB") else LANE_Y["app PROX"]
            if e["sid"] in wire_resp_t:
                drops.append((e["x"], wire_resp_t[e["sid"]], lane, e["event"]))

    # --- plot ---------------------------------------------------------------
    fig, (ax, axv) = plt.subplots(
        2, 1, figsize=(16, 9), sharex=True,
        gridspec_kw={"height_ratios": [3, 1]})

    def sel(pred):
        xs = [o["x"] for o in pred]
        return xs

    # wire lanes (thin ticks)
    def ticks(xs, y, color, h=0.32):
        ax.vlines(xs, y - h, y + h, color=color, lw=0.7, alpha=0.8)

    ticks([p["x"] for p in pkts if p["kind"] == "rtp"], LANE_Y["wire RTP :5001"], "#1f77b4")
    ticks([p["x"] for p in pkts if p["kind"] == "resp"], LANE_Y["wire reply ←:49153"], "#2ca02c")
    ticks([p["x"] for p in pkts if p["kind"] == "req"], LANE_Y["wire req →:6800"], "#7f7f7f")

    # app FRAME
    ticks([e["x"] for e in events if e["event"] == "FRAME"], LANE_Y["app FRAME"], "#1f77b4")

    # app sensor events: REQ (open circle), RSP (filled), TMO/BAD (red x)
    def scat(names, y, marker, color, size, face=True, label=None):
        xs = [e["x"] for e in events if e["event"] in names]
        if marker == "x":   # unfilled marker: color via 'c', no face/edge split
            ax.scatter(xs, [y] * len(xs), marker=marker, s=size, c=color,
                       linewidths=1.2, label=label, zorder=3)
        else:
            ax.scatter(xs, [y] * len(xs), marker=marker, s=size,
                       facecolors=(color if face else "none"), edgecolors=color,
                       linewidths=0.8, label=label, zorder=3)

    for sensor, y in (("THUMB", LANE_Y["app THUMB"]), ("PROX", LANE_Y["app PROX"])):
        scat({sensor + "_REQ"}, y + 0.0, "o", "#999999", 14, face=False)
        scat({sensor + "_RSP"}, y, "o", "#2ca02c", 16, face=True)
        scat({sensor + "_TMO", sensor + "_BAD"}, y, "x", "#d62728", 55)

    # connect each host-drop: app TMO  <-->  the wire reply it never got
    for ex, wx, ly, _ in drops:
        ax.plot([wx, ex], [LANE_Y["wire reply ←:49153"], ly],
                color="#d62728", lw=0.8, ls=":", alpha=0.7, zorder=2)
        ax.scatter([wx], [LANE_Y["wire reply ←:49153"]], marker="o", s=22,
                   facecolors="none", edgecolors="#d62728", linewidths=1.2, zorder=4)

    ax.set_yticks([y for _, y in LANES])
    ax.set_yticklabels([n for n, _ in LANES])
    ax.set_ylim(0, 9)
    ax.grid(axis="x", ls=":", alpha=0.4)

    # legend
    leg = [
        Line2D([], [], marker="o", ls="none", mfc="none", mec="#999999", label="sensor REQ"),
        Line2D([], [], marker="o", ls="none", mfc="#2ca02c", mec="#2ca02c", label="sensor RSP (ok)"),
        Line2D([], [], marker="x", ls="none", mec="#d62728", label="sensor TMO/BAD"),
        Line2D([], [], color="#d62728", ls=":", label="host drop: reply was on the wire"),
    ]
    ax.legend(handles=leg, loc="upper right", fontsize=8, framealpha=0.9)

    # bottom panel: sensor values over time (step), shows when values actually update
    tx = [(e["x"], e["v1"]) for e in events if e["event"] == "THUMB_RSP"]
    ty = [(e["x"], e["v2"]) for e in events if e["event"] == "THUMB_RSP"]
    px = [(e["x"], e["v1"]) for e in events if e["event"] == "PROX_RSP"]
    if tx:
        axv.step(*zip(*tx), where="post", color="#ff7f0e", lw=0.9, label="thumb x")
    if ty:
        axv.step(*zip(*ty), where="post", color="#8c564b", lw=0.9, label="thumb y")
    if px:
        axv2 = axv.twinx()
        axv2.step(*zip(*px), where="post", color="#1f77b4", lw=0.9, label="prox raw")
        axv2.set_ylabel("prox raw", color="#1f77b4", fontsize=9)
        axv2.tick_params(axis="y", labelcolor="#1f77b4")
    axv.set_ylabel("thumb adc", fontsize=9)
    axv.grid(axis="x", ls=":", alpha=0.4)
    axv.legend(loc="upper left", fontsize=8)
    axv.set_xlabel("time since run start [s]")

    if args.t_from is not None or args.t_to is not None:
        ax.set_xlim(args.t_from, args.t_to)

    # title with summary - rate over the streaming loop only (exclude the
    # discovery/setup phase that precedes the first FRAME).
    loop_xs = [e["x"] for e in events
               if e["event"] in ("FRAME", "THUMB_REQ", "PROX_REQ", "THUMB_RSP", "PROX_RSP")]
    loop_dur = (max(loop_xs) - min(loop_xs)) if len(loop_xs) > 1 else max(e["x"] for e in events)
    nthr = sum(1 for e in events if e["event"] == "THUMB_REQ")
    npr = sum(1 for e in events if e["event"] == "PROX_REQ")
    nfr = sum(1 for e in events if e["event"] == "FRAME")
    title = (f"clickdemo timing - {os.path.basename(pcap)} + {os.path.basename(args.csv)}   "
             f"|  loop {loop_dur:.1f}s   video {nfr/loop_dur:.0f} fps   "
             f"thumb {nthr/loop_dur:.1f} Hz   prox {npr/loop_dur:.1f} Hz   "
             f"timeouts {n_tmo} (host-drops {len(drops)})")
    ax.set_title(title, fontsize=10)

    fig.tight_layout()
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}")
    print(f"  events={len(events)}  wire packets={len(pkts)}  "
          f"timeouts={n_tmo}  host-drops={len(drops)}")
    if drops:
        # plot-x times of every host drop, so you can zoom: --from T-0.3 --to T+0.3
        ts = sorted(f"{ex:.2f}" for ex, _, _, _ in drops)
        print("  host-drop times [s]: " + ", ".join(ts))
    if args.show:
        matplotlib.use("TkAgg", force=True)
        plt.show()


if __name__ == "__main__":
    main()
