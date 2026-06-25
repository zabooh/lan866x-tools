#!/usr/bin/env python3
"""
t1s_report.py - capture + timing/protocol analysis of the LAN866x SOME/IP traffic,
                rendered to a self-contained HTML report.

Flow (live):
  1. enable the bridge port mirror  ("mirror 1" on the board CLI, COM port)
  2. for each scenario - discovery, diag, gpiomax - start a tshark capture on the
     PC's bridge-side interface, run the board command (which generates the
     traffic), stop the capture
  3. decode each capture (SOME/IP forced on the RCP ports), analyse timing
     (request->response round-trip times, inter-packet gaps, throughput) and
     protocol mix (per SOME/IP method / message type)
  4. write report.html with tables + matplotlib timing diagrams (RTT-over-time,
     RTT histogram, protocol bar) embedded as base64 PNGs
  5. disable the mirror

Offline: analyse an existing capture instead of step 1-2:
  python t1s_report.py --pcap some.pcapng --out report.html

Requirements: tshark (Wireshark) + Npcap + admin (live capture); pyserial (live);
matplotlib. Install the SOME/IP dissector config first (wireshark/SOMEIP/install.bat)
for named method fields; the RCP ports are also forced via Decode-As here.

Examples:
  python t1s_report.py --iface "Ethernet 8" --port COM8 --ip 192.168.0.54
  python t1s_report.py --pcap captures/diag.pcapng
"""
import argparse
import base64
import glob
import io
import os
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

MSGTYPE = {0x00: "REQUEST", 0x01: "REQUEST_NO_RETURN", 0x02: "NOTIFICATION",
           0x80: "RESPONSE", 0x81: "ERROR"}

# scenario name -> (board CLI command, capture window s, repeats within the window)
# Short commands (discovery) are issued several times so their brief request/
# response burst reliably lands inside the capture; long commands run once.
SCENARIOS = [
    ("discovery", "discovery", 12, 4),
    ("diag",      "diag 30",   12, 1),
    ("gpiomax",   "gpiomax 5", 9,  1),
]


def find_tshark():
    for p in (r"C:\Program Files\Wireshark\tshark.exe",
              r"C:\Program Files (x86)\Wireshark\tshark.exe"):
        if os.path.isfile(p):
            return p
    return "tshark"


def load_method_names():
    names = {}
    path = os.path.join(HERE, "SOMEIP", "SOMEIP_method_event_identifiers")
    try:
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                parts = [x.strip().strip('"') for x in line.split(",")]
                if len(parts) >= 3:
                    names[int(parts[1], 16)] = parts[2]
    except OSError:
        pass
    return names


METHOD_NAMES = load_method_names()
TSHARK = find_tshark()
DECODE_AS = ["-d", "udp.port==6800,someip", "-d", "udp.port==49153,someip",
             "-d", "udp.port==30490,someip", "-d", "udp.port==5001,rtp"]


# --------------------------------------------------------------------------- serial
def serial_cmd(port, baud, cmd, drain_s, repeats=1):
    """Send a CLI command `repeats` times spread over drain_s seconds, reading the
    board output throughout (keeps the port open the whole time)."""
    import serial
    out = bytearray()
    interval = drain_s / max(1, repeats)
    with serial.Serial(port, baud, timeout=0.1) as s:
        for _ in range(max(1, repeats)):
            s.write((cmd + "\r\n").encode("ascii"))
            end = time.time() + interval
            while time.time() < end:
                out += s.read(s.in_waiting or 1)
    return out.decode("utf-8", "replace")


# --------------------------------------------------------------------------- capture
def capture(iface, bpf, seconds, pcap):
    """tshark live capture for `seconds` into pcap (duration-bounded, self-stopping)."""
    cmd = [TSHARK, "-i", str(iface), "-f", bpf, "-a", f"duration:{seconds}", "-w", pcap, "-q"]
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def extract(pcap):
    """Decode a pcap to a list of per-packet dicts (SOME/IP fields enriched)."""
    fields = ["frame.time_relative", "frame.len", "_ws.col.protocol",
              "ip.src", "ip.dst", "udp.srcport", "udp.dstport",
              "someip.serviceid", "someip.methodid", "someip.messagetype", "someip.sessionid"]
    cmd = [TSHARK, "-r", pcap] + DECODE_AS + ["-T", "fields", "-E", "separator=\t"]
    for f in fields:
        cmd += ["-e", f]
    raw = subprocess.run(cmd, capture_output=True, text=True).stdout
    rows = []
    for line in raw.splitlines():
        c = line.split("\t")
        if len(c) < len(fields):
            c += [""] * (len(fields) - len(c))

        def first(s):                       # tshark joins repeats with ','
            return s.split(",")[0] if s else ""

        def hx(s):
            s = first(s)
            try:
                return int(s, 16)
            except ValueError:
                return None
        try:
            t = float(c[0])
        except ValueError:
            continue
        rows.append({
            "t": t, "len": int(first(c[1]) or 0), "proto": c[2],
            "src": first(c[3]), "dst": first(c[4]),
            "sport": first(c[5]), "dport": first(c[6]),
            "svc": hx(c[7]), "mid": hx(c[8]), "mt": hx(c[9]), "sid": hx(c[10]),
        })
    return rows


# --------------------------------------------------------------------------- analysis
def label_of(r):
    if r["svc"] == 0xffff or (r["mid"] is not None and r["mid"] == 0x8100):
        return "SOME/IP-SD"
    if r["mid"] is not None:
        return METHOD_NAMES.get(r["mid"], f"method 0x{r['mid']:04x}")
    if r["dport"] == "5001" or r["sport"] == "5001":
        return "RTP video"
    return r["proto"] or "other"


def analyse(rows):
    """Return a dict of timing + protocol stats for one scenario's packets."""
    res = {"packets": len(rows), "bytes": sum(r["len"] for r in rows),
           "dur": (rows[-1]["t"] - rows[0]["t"]) if rows else 0.0,
           "proto": {}, "methods": {}, "rtts": [], "rtt_t": []}
    # protocol mix
    for r in rows:
        lab = label_of(r)
        res["proto"][lab] = res["proto"].get(lab, 0) + 1
    # request -> response round-trip per (method, session)
    pend = {}
    for r in rows:
        if r["mid"] is None or r["mt"] is None or r["svc"] == 0xffff:
            continue
        key = (r["mid"], r["sid"])
        if r["mt"] in (0x00, 0x01):                 # request
            pend[key] = r["t"]
        elif r["mt"] in (0x80, 0x81) and key in pend:   # response/error
            rtt = (r["t"] - pend.pop(key)) * 1000.0     # ms
            if rtt >= 0:
                res["rtts"].append(rtt)
                res["rtt_t"].append(r["t"])
                name = METHOD_NAMES.get(r["mid"], f"0x{r['mid']:04x}")
                res["methods"].setdefault(name, []).append(rtt)
    return res


def stats_line(vals):
    if not vals:
        return "-"
    vals = sorted(vals)
    p95 = vals[min(len(vals) - 1, int(0.95 * len(vals)))]
    return (f"n={len(vals)}  min={min(vals):.2f}  avg={statistics.mean(vals):.2f}  "
            f"median={statistics.median(vals):.2f}  p95={p95:.2f}  max={max(vals):.2f} ms")


# --------------------------------------------------------------------------- plots
def _png(fig):
    import matplotlib.pyplot as plt
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=92, bbox_inches="tight")
    plt.close(fig)
    return base64.b64encode(buf.getvalue()).decode("ascii")


def plots_for(name, res):
    """Return a list of (title, base64png) timing/protocol diagrams."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    imgs = []
    rtts, ts = res["rtts"], res["rtt_t"]

    if rtts:
        t0 = ts[0]
        fig, ax = plt.subplots(figsize=(7.5, 2.6))
        ax.plot([t - t0 for t in ts], rtts, ".-", ms=4, lw=0.6, color="#1f77b4")
        ax.set_xlabel("time since first reply [s]"); ax.set_ylabel("RTT [ms]")
        ax.set_title(f"{name}: SOME/IP request→response RTT over time")
        ax.grid(True, alpha=0.3)
        imgs.append(("RTT over time", _png(fig)))

        fig, ax = plt.subplots(figsize=(7.5, 2.6))
        ax.hist(rtts, bins=min(40, max(5, len(rtts) // 3)), color="#2ca02c", alpha=0.85)
        ax.set_xlabel("RTT [ms]"); ax.set_ylabel("count")
        ax.set_title(f"{name}: RTT distribution")
        ax.grid(True, alpha=0.3)
        imgs.append(("RTT histogram", _png(fig)))

    if res["proto"]:
        labels = list(res["proto"].keys())
        counts = [res["proto"][k] for k in labels]
        fig, ax = plt.subplots(figsize=(7.5, max(2.2, 0.42 * len(labels) + 1)))
        ax.barh(labels, counts, color="#ff7f0e")
        ax.set_xlabel("packets"); ax.set_title(f"{name}: protocol / method mix")
        ax.grid(True, axis="x", alpha=0.3)
        for i, v in enumerate(counts):
            ax.text(v, i, f" {v}", va="center", fontsize=8)
        imgs.append(("Protocol mix", _png(fig)))
    return imgs


# --------------------------------------------------------------------------- HTML
def html_report(title, scenarios, meta):
    def esc(s):
        return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    H = [f"""<!doctype html><html><head><meta charset="utf-8"><title>{esc(title)}</title>
<style>
 body{{font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;margin:2em auto;max-width:980px;color:#222;padding:0 1em}}
 h1{{border-bottom:3px solid #1f77b4;padding-bottom:.2em}} h2{{margin-top:1.8em;border-bottom:1px solid #ddd}}
 table{{border-collapse:collapse;margin:.6em 0}} td,th{{border:1px solid #ccc;padding:3px 9px;text-align:left}}
 th{{background:#f0f4f8}} code{{background:#f4f4f4;padding:1px 4px;border-radius:3px}}
 img{{max-width:100%;border:1px solid #eee;margin:.4em 0}} .meta{{color:#666;font-size:12px}}
 .kpi{{display:inline-block;background:#f0f4f8;border:1px solid #d6e0ea;border-radius:6px;padding:.4em .8em;margin:.2em .4em .2em 0}}
</style></head><body>"""]
    H.append(f"<h1>{esc(title)}</h1>")
    H.append('<p class="meta">' + " &nbsp;|&nbsp; ".join(esc(m) for m in meta) + "</p>")

    # overall summary
    H.append("<h2>Summary</h2>")
    H.append("<table><tr><th>Scenario</th><th>Packets</th><th>Bytes</th><th>Duration</th>"
             "<th>RCP round-trips</th><th>RTT avg / p95 / max</th></tr>")
    for name, res, _txt, _imgs in scenarios:
        rtts = res["rtts"]
        if rtts:
            sv = sorted(rtts)
            p95 = sv[min(len(sv) - 1, int(0.95 * len(sv)))]
            rttcol = f"{statistics.mean(rtts):.2f} / {p95:.2f} / {max(rtts):.2f} ms"
        else:
            rttcol = "-"
        H.append(f"<tr><td><b>{esc(name)}</b></td><td>{res['packets']}</td>"
                 f"<td>{res['bytes']}</td><td>{res['dur']:.2f} s</td>"
                 f"<td>{len(rtts)}</td><td>{rttcol}</td></tr>")
    H.append("</table>")

    # per scenario
    for name, res, txt, imgs in scenarios:
        H.append(f"<h2>{esc(name)}</h2>")
        thr = (res["bytes"] * 8 / res["dur"] / 1000.0) if res["dur"] > 0 else 0
        H.append(f'<span class="kpi">packets <b>{res["packets"]}</b></span>'
                 f'<span class="kpi">{res["bytes"]} bytes</span>'
                 f'<span class="kpi">{res["dur"]:.2f} s</span>'
                 f'<span class="kpi">~{thr:.0f} kbit/s</span>'
                 f'<span class="kpi">RCP round-trips <b>{len(res["rtts"])}</b></span>')
        H.append("<h3>Timing (request → response RTT)</h3>")
        H.append(f"<p><b>overall:</b> {esc(stats_line(res['rtts']))}</p>")
        if res["methods"]:
            H.append("<table><tr><th>Method</th><th>RTT min / avg / max [ms]</th><th>count</th></tr>")
            for m, v in sorted(res["methods"].items(), key=lambda kv: -len(kv[1])):
                H.append(f"<tr><td><code>{esc(m)}</code></td>"
                         f"<td>{min(v):.2f} / {statistics.mean(v):.2f} / {max(v):.2f}</td>"
                         f"<td>{len(v)}</td></tr>")
            H.append("</table>")
        H.append("<h3>Protocol mix</h3><table><tr><th>Label</th><th>packets</th></tr>")
        for lab, c in sorted(res["proto"].items(), key=lambda kv: -kv[1]):
            H.append(f"<tr><td>{esc(lab)}</td><td>{c}</td></tr>")
        H.append("</table>")
        for ttl, png in imgs:
            H.append(f"<h3>{esc(ttl)}</h3><img alt='{esc(ttl)}' src='data:image/png;base64,{png}'>")
        if txt:
            H.append("<h3>Board output (excerpt)</h3><pre style='background:#f7f7f7;border:1px solid #eee;"
                     "padding:.6em;overflow:auto;max-height:240px;font-size:12px'>"
                     + esc(txt[-1500:]) + "</pre>")
    H.append("</body></html>")
    return "\n".join(H)


# --------------------------------------------------------------------------- main
def run_live(args):
    capdir = os.path.join(HERE, "captures")
    os.makedirs(capdir, exist_ok=True)
    bpf = f"host {args.ip} or udp port 30490" if args.ip else "udp"
    scenarios = []

    print(f"[mirror] enabling port mirror on {args.port} ...")
    try:
        serial_cmd(args.port, args.baud, "mirror 1", 2)
        serial_cmd(args.port, args.baud, "discovery", 4)   # warm up the endpoint table
    except Exception as e:
        print(f"  ! serial error: {e}")

    for name, cmd, window, repeats in SCENARIOS:
        pcap = os.path.join(capdir, f"{name}.pcapng")
        print(f"[{name}] capturing {window}s on '{args.iface}' while running '{cmd}'"
              f"{f' x{repeats}' if repeats > 1 else ''} ...")
        cap = capture(args.iface, bpf, window, pcap)
        time.sleep(1.6)                                    # capture warm-up (Npcap start latency)
        txt = ""
        try:
            txt = serial_cmd(args.port, args.baud, cmd, window - 2.0, repeats)
        except Exception as e:
            print(f"  ! serial error: {e}")
        cap.wait()
        rows = extract(pcap)
        res = analyse(rows)
        imgs = plots_for(name, res)
        print(f"  -> {res['packets']} pkts, {len(res['rtts'])} RCP round-trips")
        scenarios.append((name, res, txt, imgs))

    try:
        serial_cmd(args.port, args.baud, "mirror 0", 2)
    except Exception:
        pass
    return scenarios, bpf


def run_offline(args):
    rows = extract(args.pcap)
    res = analyse(rows)
    imgs = plots_for("capture", res)
    return [("capture", res, "", imgs)], "(offline pcap)"


def main():
    ap = argparse.ArgumentParser(description="LAN866x SOME/IP capture + timing/protocol HTML report")
    ap.add_argument("--iface", default="Ethernet 8", help='capture interface (name or tshark -D number)')
    ap.add_argument("--port", default="COM8", help="board CLI serial port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ip", default="192.168.0.54", help="endpoint IP (capture filter)")
    ap.add_argument("--pcap", default=None, help="offline: analyse this capture instead of live")
    ap.add_argument("--out", default=os.path.join(HERE, "report.html"))
    args = ap.parse_args()

    t0 = time.strftime("%Y-%m-%d %H:%M:%S")
    if args.pcap:
        scenarios, bpf = run_offline(args)
        meta = [f"generated {t0}", f"offline: {os.path.basename(args.pcap)}", f"tshark: {TSHARK}"]
    else:
        scenarios, bpf = run_live(args)
        meta = [f"generated {t0}", f"iface: {args.iface}", f"endpoint: {args.ip}",
                f"capture filter: {bpf}", f"tshark: {TSHARK}"]

    html = html_report("LAN866x SOME/IP — timing & protocol report", scenarios, meta)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"\nReport written: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
