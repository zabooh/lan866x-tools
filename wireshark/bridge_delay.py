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

`lan866x-discovery` is used to generate the traffic. Accuracy floor is the NTP sync
residual (~hundreds of us, software NTP), so results are aggregated over many frames;
values near/below that floor mean the bridge delay is below the sync noise.

Requires: lan866x-ntpsync(.exe), lan866x-discovery(.exe), tshark (Wireshark) + Npcap,
admin. The firmware must run the NTP service (port 30491) + eth0 tap.

Usage:
  python bridge_delay.py --iface "Ethernet 8" --bridge 192.168.0.181 --endpoint 192.168.0.54
"""
import argparse, os, socket, struct, subprocess, sys, threading, time, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

OP_TAP_SET = 0x05
OP_TAP_REC = 0x06
COLLECT_PORT = 30492


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


def main():
    ap = argparse.ArgumentParser(description="Measure the bridge one-way delay both directions")
    ap.add_argument("--iface", default="Ethernet 8", help="PC capture interface (name or tshark -D number)")
    ap.add_argument("--bridge", default="192.168.0.181", help="bridge NTP service IP")
    ap.add_argument("--endpoint", default="192.168.0.54", help="LAN866x endpoint IP")
    ap.add_argument("--secs", type=int, default=12, help="capture window seconds")
    ap.add_argument("--discovery-runs", type=int, default=3)
    args = ap.parse_args()

    TS = find_tshark()
    ep_u32 = ip2u32(args.endpoint)

    # 1) sync the firmware NTP counter to this PC
    print("[1/4] NTP sync ...")
    r = subprocess.run([tool("lan866x-ntpsync"), "--ip", args.bridge, "--once"],
                       capture_output=True, text=True)
    sys.stdout.write("  " + (r.stdout.strip().replace("\n", "\n  ")) + "\n")
    if "Done" not in r.stdout:
        print("  ! sync may have failed; continuing anyway")

    # 2) enable the eth0 tap -> this PC:COLLECT_PORT
    coll = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    coll.bind(("", COLLECT_PORT)); coll.settimeout(0.3)
    setter = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    setter.connect((args.bridge, 30491))
    setter.send(bytes([OP_TAP_SET, 1]) + struct.pack(">H", COLLECT_PORT))
    try: setter.recv(32)
    except OSError: pass
    taps = {}; stop = threading.Event()
    th = threading.Thread(target=collector, args=(coll, taps, stop)); th.start()
    print(f"[2/4] eth0 tap enabled -> PC:{COLLECT_PORT}")

    # 3) capture at the NIC while running discovery
    pcap = os.path.join(HERE, "captures", "bridge_delay.pcapng")
    os.makedirs(os.path.dirname(pcap), exist_ok=True)
    bpf = f"host {args.endpoint} or udp port 30490"
    print(f"[3/4] capturing {args.secs}s on '{args.iface}' + running discovery x{args.discovery_runs} ...")
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
    print(f"  tap records: {len(taps)}")

    # 4) decode the capture and match by IPv4 id
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

    print("[4/4] bridge one-way delay (matched by IPv4 id):")
    print("  raw (firmware NTP clock vs NIC capture clock):")
    mf = report("PC -> endpoint  (eth0 - NIC)", fwd)
    mr = report("endpoint -> PC  (NIC - eth0)", rev)
    print("\n  The raw per-direction values carry a CONSTANT skew S between the NIC capture")
    print("  clock (Npcap) and the clock ntpsync disciplines to - it appears with opposite")
    print("  sign in the two directions, so the SUM cancels it:")
    if mf is not None and mr is not None:
        total = mf + mr                      # D_fwd + D_rev  (skew-free)
        skew  = (mf - mr) / 2.0              # capture-clock - sync-clock (assumes symmetry)
        print(f"    bridge round-trip transport (NIC<->eth0, skew-free) : {total/1e3:.1f} us")
        print(f"    -> per direction (assuming symmetry)                : {total/2e3:.1f} us each")
        print(f"    estimated capture-vs-sync clock skew S              : {skew/1e3:.1f} us")
        print("  The skew-free round-trip (and its half) is the meaningful bridge-delay figure;")
        print("  splitting it per direction needs the symmetry assumption. Floor = NTP sync")
        print("  residual (~hundreds of us, software NTP) - run more frames to average it down.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
