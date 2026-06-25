#!/usr/bin/env python3
"""
bridge_delay.py - measure the bridge's one-way forwarding delay in BOTH directions,
using the NTP-synced firmware clock + a PC-side NIC capture.

Idea: after lan866x-ntpsync disciplines the firmware NTP counter to the PC wall
clock, the bridge and the PC share one timebase (Unix-epoch ns). The firmware taps
every IPv4 frame crossing eth0 (the T1S side) with its NTP time and streams those
records to this PC tool; the PC captures the same frames at its NIC (also epoch ns).
Matching the two by IPv4 id then gives:

  PC -> endpoint : delay_fwd = t(eth0 egress)  - t(NIC sent)      # frames to the endpoint
  endpoint -> PC : delay_rev = t(NIC received) - t(eth0 ingress)  # frames from the endpoint

There is a catch: the NIC capture timestamps come from Npcap's clock, while ntpsync
disciplines the firmware to GetSystemTimePreciseAsFileTime(). Those two PC clocks
differ by a CONSTANT skew S = (FILETIME clock) - (Npcap clock) (~tens of ms). S enters
delay_fwd as +S and delay_rev as -S. Two ways to handle it:

  * skew-free round-trip  : delay_fwd + delay_rev = D_fwd + D_rev   (S cancels)
  * exact per-direction   : calibrate S once, then D_fwd = delay_fwd - S,
                            D_rev = delay_rev + S   (no symmetry assumption needed)

S is calibrated (default on) by sending self-stamped UDP packets out the capture NIC
and reading their Npcap capture timestamps back: S = median(FILETIME_send - frame.time_epoch).

`lan866x-discovery` is used to generate the bridge<->endpoint traffic. The remaining
accuracy floor is the NTP sync residual (~hundreds of us, software NTP), so results
are aggregated over many frames.

Requires: lan866x-ntpsync(.exe), lan866x-discovery(.exe), tshark (Wireshark) + Npcap,
admin. The firmware must run the NTP service (port 30491) + eth0 tap.

Usage:
  python bridge_delay.py --iface "Ethernet 8" --bridge 192.168.0.181 --endpoint 192.168.0.54
  python bridge_delay.py --iface "Ethernet 8" --no-calibrate          # skew-free round-trip only
"""
import argparse, ctypes, os, socket, struct, subprocess, sys, threading, time, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

OP_TAP_SET = 0x05
OP_TAP_REC = 0x06
COLLECT_PORT = 30492
CALIB_PORT = 39999


# --- PC reference clock: the SAME clock lan866x-ntpsync disciplines the firmware to
#     (GetSystemTimePreciseAsFileTime), expressed as Unix-epoch nanoseconds. ----------
if sys.platform == "win32":
    _kernel32 = ctypes.windll.kernel32
    _FT_EPOCH = 116444736000000000      # 100 ns ticks between 1601-01-01 and 1970-01-01

    def filetime_ns():
        ft = ctypes.c_ulonglong(0)
        _kernel32.GetSystemTimePreciseAsFileTime(ctypes.byref(ft))
        return (ft.value - _FT_EPOCH) * 100
else:
    def filetime_ns():
        return time.time_ns()


def tool(name):
    for p in (os.path.join(ROOT, "release", name + ".exe"),
              os.path.join(ROOT, "release", name)):
        if os.path.isfile(p):
            return p
    return name


def find_tshark():
    for p in (r"C:\Program Files\Wireshark\tshark.exe",
              r"C:\Program Files (x86)\Wireshark\tshark.exe"):
        if os.path.isfile(p):
            return p
    return "tshark"


def ip2u32(s):
    a = s.split(".")
    return (int(a[0]) << 24) | (int(a[1]) << 16) | (int(a[2]) << 8) | int(a[3])


def collector(sock, taps, stop):
    """Receive OP_TAP_REC records: taps[(dir,ipid,iplen)] = t_ns."""
    while not stop.is_set():
        try:
            d, _ = sock.recvfrom(64)
        except socket.timeout:
            continue
        except OSError:
            break
        if len(d) >= 23 and d[0] == OP_TAP_REC:
            dirn = d[1]
            t = struct.unpack(">Q", d[2:10])[0]
            ipid = struct.unpack(">H", d[10:12])[0]
            iplen = struct.unpack(">H", d[13:15])[0]
            taps[(dirn, ipid, iplen)] = t


def calibrate_skew(TS, iface, dst_ip, npkts, src_ip=None):
    """Measure S = (FILETIME clock) - (Npcap capture clock), in ns.

    Send self-stamped UDP packets out the capture NIC and read their Npcap capture
    timestamps back; S = median(FILETIME_send - frame.time_epoch). The firmware NTP
    clock is disciplined to FILETIME, so subtracting S converts a capture timestamp
    into the firmware/sync timebase (and adding it does the reverse).

    The capture point delay (sendto -> Npcap stamp) is a few us, negligible vs the
    ~tens-of-ms clock offset being calibrated. Calibration uses outgoing (TX) frames;
    the clock-source offset is direction-independent, so the same S applies to the
    incoming (RX) reverse direction. With several NICs on one subnet, pass src_ip
    (the capture NIC's address) so the calibration packets really egress that NIC.
    """
    pcap = os.path.join(HERE, "captures", "calib.pcapng")
    os.makedirs(os.path.dirname(pcap), exist_ok=True)
    dur = int(2 + npkts * 0.02 + 2)
    cap = subprocess.Popen([TS, "-i", str(iface), "-f", f"udp port {CALIB_PORT}",
                            "-a", f"duration:{dur}", "-w", pcap, "-q"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)                                  # let the capture start
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if src_ip:
        s.bind((src_ip, 0))                          # force egress out the capture NIC
    sent = {}
    for seq in range(npkts):
        payload = struct.pack(">I", seq) + b"CAPCAL-skew"
        t0 = filetime_ns()
        s.sendto(payload, (dst_ip, CALIB_PORT))
        t1 = filetime_ns()
        sent[seq] = (t0 + t1) // 2                   # midpoint of the send syscall
        time.sleep(0.02)
    s.close()
    cap.wait()
    rows = subprocess.run([TS, "-r", pcap, "-Y", "udp", "-T", "fields",
                           "-e", "frame.time_epoch", "-e", "udp.payload"],
                          capture_output=True, text=True).stdout.splitlines()
    skews = []
    for line in rows:
        c = line.split("\t")
        if len(c) < 2 or not c[0] or not c[1]:
            continue
        hexb = c[1].replace(":", "")
        if len(hexb) < 8:
            continue
        try:
            seq = int(hexb[:8], 16)
            t_cap = int(round(float(c[0]) * 1e9))
        except ValueError:
            continue
        if seq in sent:
            skews.append(sent[seq] - t_cap)
    if not skews:
        return None, 0
    return statistics.median(skews), len(skews)


def main():
    ap = argparse.ArgumentParser(description="Measure the bridge one-way delay both directions")
    ap.add_argument("--iface", default="Ethernet 8", help="PC capture interface (name or tshark -D number)")
    ap.add_argument("--bridge", default="192.168.0.181", help="bridge NTP service IP")
    ap.add_argument("--endpoint", default="192.168.0.54", help="LAN866x endpoint IP")
    ap.add_argument("--secs", type=int, default=12, help="capture window seconds")
    ap.add_argument("--discovery-runs", type=int, default=3)
    ap.add_argument("--no-calibrate", action="store_true",
                    help="skip the capture-clock skew calibration (report skew-free round-trip only)")
    ap.add_argument("--calib-packets", type=int, default=40, help="self-sent frames for skew calibration")
    ap.add_argument("--sync-ms", type=int, default=200,
                    help="continuous NTP re-sync period (ms) kept running during the capture")
    ap.add_argument("--src-ip", default=None,
                    help="capture NIC's own IPv4 (bind calibration sends to it; needed when "
                         "several NICs share the subnet)")
    args = ap.parse_args()

    TS = find_tshark()

    # 1) keep the firmware NTP counter disciplined to this PC for the WHOLE run.
    #    A one-shot --once sync is not enough: the firmware oscillator drifts (~ms/s),
    #    so by the time we measure (seconds later) the clock has slid far off. Run
    #    ntpsync continuously in the background so the tap timestamps stay aligned.
    print(f"[1/5] starting continuous NTP sync (every {args.sync_ms} ms) ...")
    synp = subprocess.Popen([tool("lan866x-ntpsync"), "--ip", args.bridge,
                             "--interval", str(args.sync_ms)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)                                  # let the first few syncs land
    if synp.poll() is not None:
        print("  ! ntpsync exited immediately; continuing anyway (results may be skewed)")

    # 2) calibrate the constant skew between the NIC capture clock and the sync clock
    S = None; ns = 0
    if not args.no_calibrate:
        print(f"[2/5] calibrating capture-vs-sync clock skew ({args.calib_packets} self-sent frames) ...")
        S, ns = calibrate_skew(TS, args.iface, args.bridge, args.calib_packets, args.src_ip)
        if S is None:
            print("  ! calibration captured no frames (admin? right --iface?); "
                  "falling back to skew-free round-trip only")
        else:
            print(f"  S = {S/1e3:.1f} us  (FILETIME clock - Npcap clock, from {ns} frames)")
    else:
        print("[2/5] skew calibration skipped (--no-calibrate)")

    # 3) enable the eth0 tap -> this PC:COLLECT_PORT
    coll = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    coll.bind(("", COLLECT_PORT)); coll.settimeout(0.3)
    setter = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    setter.connect((args.bridge, 30491))
    setter.send(bytes([OP_TAP_SET, 1]) + struct.pack(">H", COLLECT_PORT))
    try: setter.recv(32)
    except OSError: pass
    taps = {}; stop = threading.Event()
    th = threading.Thread(target=collector, args=(coll, taps, stop)); th.start()
    print(f"[3/5] eth0 tap enabled -> PC:{COLLECT_PORT}")

    # 4) capture at the NIC while running discovery
    pcap = os.path.join(HERE, "captures", "bridge_delay.pcapng")
    os.makedirs(os.path.dirname(pcap), exist_ok=True)
    bpf = f"host {args.endpoint} or udp port 30490"
    print(f"[4/5] capturing {args.secs}s on '{args.iface}' + running discovery x{args.discovery_runs} ...")
    cap = subprocess.Popen([TS, "-i", str(args.iface), "-f", bpf, "-a", f"duration:{args.secs}",
                            "-w", pcap, "-q"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    for _ in range(args.discovery_runs):
        subprocess.run([tool("lan866x-discovery"), "--ip", args.endpoint],
                       capture_output=True, text=True)
        time.sleep(0.5)
    cap.wait()
    time.sleep(0.4)
    stop.set(); th.join()
    setter.send(bytes([OP_TAP_SET, 0]) + struct.pack(">H", 0))     # disable tap
    coll.close(); setter.close()
    synp.terminate()                                               # stop the background sync
    try: synp.wait(timeout=2)
    except Exception: synp.kill()
    print(f"  tap records: {len(taps)}")

    # 5) decode the capture and match by IPv4 id
    fields = ["frame.time_epoch", "ip.id", "ip.src", "ip.dst", "ip.len", "ip.proto"]
    cmd = [TS, "-r", pcap, "-Y", "ip", "-T", "fields", "-E", "separator=\t"]
    for f in fields: cmd += ["-e", f]
    rows = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
    fwd, rev = [], []     # ns
    for line in rows:
        c = line.split("\t")
        if len(c) < 6 or not c[0]:
            continue
        try:
            t_nic = int(round(float(c[0]) * 1e9))
            ipid = int(c[1].split(",")[0], 0) if c[1] else 0
            iplen = int(c[4].split(",")[0]) if c[4] else 0
        except ValueError:
            continue
        src, dst = c[2].split(",")[0], c[3].split(",")[0]
        if dst == args.endpoint:                      # PC -> endpoint (forwarded out eth0)
            t = taps.get((1, ipid, iplen))
            if t is not None: fwd.append(t - t_nic)
        elif src == args.endpoint:                    # endpoint -> PC (in at eth0, forwarded to NIC)
            t = taps.get((0, ipid, iplen))
            if t is not None: rev.append(t_nic - t)

    def report(name, vals):
        if not vals:
            print(f"  {name:<28}: no matched frames"); return None
        v = sorted(vals)
        print(f"  {name:<28}: n={len(v):3d}  min={min(v)/1e3:9.1f}  median={statistics.median(v)/1e3:9.1f}  "
              f"max={max(v)/1e3:9.1f}  us")
        return statistics.median(v)

    print("[5/5] bridge one-way delay (matched by IPv4 id):")
    print("  raw (firmware NTP clock vs NIC capture clock):")
    mf = report("PC -> endpoint  (eth0 - NIC)", fwd)
    mr = report("endpoint -> PC  (NIC - eth0)", rev)
    if mf is None or mr is None:
        print("\n  (need matched frames in BOTH directions to report a delay)")
        return 0

    total = mf + mr                                   # D_fwd + D_rev (skew-free, S cancels)
    if S is not None:
        d_fwd = mf - S                                # exact one-way, skew removed
        d_rev = mr + S
        sym = (mf - mr) / 2.0                          # what symmetry would have estimated for S
        print(f"\n  calibrated capture-vs-sync clock skew S : {S/1e3:.1f} us  ({ns} frames)")
        print("  -> exact one-way bridge delay (skew removed, NO symmetry assumption):")
        print(f"       PC -> endpoint : {d_fwd/1e3:8.1f} us")
        print(f"       endpoint -> PC : {d_rev/1e3:8.1f} us")
        print(f"     cross-check round-trip (mf+mr, S-independent) : {total/1e3:.1f} us")
        print(f"     consistency: symmetry would estimate S = {sym/1e3:.1f} us vs calibrated {S/1e3:.1f} us")
        print("  (a large fwd/rev imbalance after correction, or calibrated S far from the")
        print("   symmetry estimate, indicates the skew drifted during the run.)")
    else:
        print("\n  The raw per-direction values carry a CONSTANT skew S between the NIC capture")
        print("  clock (Npcap) and the clock ntpsync disciplines to - it appears with opposite")
        print("  sign in the two directions, so the SUM cancels it:")
        skew = (mf - mr) / 2.0
        print(f"    bridge round-trip transport (NIC<->eth0, skew-free) : {total/1e3:.1f} us")
        print(f"    -> per direction (assuming symmetry)                : {total/2e3:.1f} us each")
        print(f"    estimated capture-vs-sync clock skew S              : {skew/1e3:.1f} us")
        print("  Run without --no-calibrate to measure S and split the directions exactly.")
    print("  Floor = NTP sync residual (~hundreds of us, software NTP) - run more frames to average down.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
