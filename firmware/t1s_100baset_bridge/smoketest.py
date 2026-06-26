#!/usr/bin/env python3
"""
smoketest.py - end-to-end smoke test for the flashed T1S<->100BASE-T bridge firmware.

Proves the core functions are alive after a build/flash (e.g. after running MCC):
  1. bridge IP reachable                (the bridge's own TCP/IP stack on eth1)
  2. L2 forwarding                       (PC -> bridge -> T1S -> endpoint, ICMP)
  3. SOME/IP service discovery           (full RCP path; via lan866x-discovery)
  4. firmware NTP service                (UDP 30491 t1/t2/t3/t4 exchange, sane RTT)
  5. on-board console + stack (optional) (UART 'ntp' over --com)

Each check prints PASS/FAIL; the script exits non-zero if any CRITICAL check fails.
Checks whose tool/port is unavailable are SKIPPED (not failed). No external deps
except pyserial (only for the optional --com UART check).

Usage:
  python smoketest.py                                   # defaults (.181 / .54)
  python smoketest.py --bridge 192.168.0.181 --endpoint 192.168.0.54 --com COM8
"""
import argparse, os, re, socket, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))      # repo root (has release/)
NTP_PORT = 30491
OP_REQUEST, OP_REPLY = 0x01, 0x02

results = []   # (name, status, detail)  status in {"PASS","FAIL","SKIP"}
def record(name, status, detail=""):
    results.append((name, status, detail))
    mark = {"PASS": "PASS", "FAIL": "FAIL", "SKIP": "skip"}[status]
    print(f"  [{mark}] {name}" + (f"  - {detail}" if detail else ""))


def find_tool(name):
    for p in (os.path.join(ROOT, "release", name + ".exe"),
              os.path.join(ROOT, "release", name)):
        if os.path.isfile(p):
            return p
    return None


def ping(host, timeout_s=1):
    if sys.platform == "win32":
        cmd = ["ping", "-n", "2", "-w", str(int(timeout_s * 1000)), host]
    else:
        cmd = ["ping", "-c", "2", "-W", str(int(timeout_s)), host]
    return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


# --- 1) bridge reachable -----------------------------------------------------------
def check_bridge(args):
    record("bridge IP reachable (%s)" % args.bridge,
           "PASS" if ping(args.bridge) else "FAIL")


# --- 2) L2 forwarding to the endpoint ----------------------------------------------
def check_forwarding(args):
    record("L2 forwarding to endpoint (%s via bridge)" % args.endpoint,
           "PASS" if ping(args.endpoint) else "FAIL")


# --- 3) SOME/IP service discovery (uses lan866x-discovery) -------------------------
def check_discovery(args):
    exe = find_tool("lan866x-discovery")
    if not exe:
        record("SOME/IP discovery", "SKIP", "lan866x-discovery not found in release/")
        return
    try:
        out = subprocess.run([exe, "--ip", args.endpoint], capture_output=True,
                             text=True, timeout=20).stdout
    except Exception as e:
        record("SOME/IP discovery", "FAIL", f"tool error: {e}"); return
    m = re.search(r"Devices available\s*=\s*(\d+)", out)
    n = int(m.group(1)) if m else 0
    found_ep = args.endpoint in out
    chip = re.search(r"Chip Identifier:\s*(\S+)", out)
    if n >= 1 and found_ep:
        record("SOME/IP discovery", "PASS",
               f"{n} endpoint(s); {args.endpoint}" + (f" ({chip.group(1)})" if chip else ""))
    else:
        record("SOME/IP discovery", "FAIL", f"devices={n}, endpoint_seen={found_ep}")


# --- 4) firmware NTP service (raw UDP t1/t2/t3/t4) ---------------------------------
def check_ntp(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(1.0)
    try:
        s.connect((args.bridge, NTP_PORT))
        delays = []
        for _ in range(5):
            t1 = time.time_ns()
            s.send(bytes([OP_REQUEST]) + struct.pack(">q", t1))
            d = s.recv(64); t4 = time.time_ns()
            if len(d) < 25 or d[0] != OP_REPLY:
                continue
            t2 = struct.unpack(">q", d[9:17])[0]
            t3 = struct.unpack(">q", d[17:25])[0]
            if t2 == 0 or t3 == 0:
                continue
            delays.append(((t4 - t1) - (t3 - t2)) / 1e3)   # us
            time.sleep(0.02)
        if not delays:
            record("firmware NTP service (UDP %d)" % NTP_PORT, "FAIL", "no valid reply")
            return
        best = min(delays)
        ok = 0 < best < 5000.0     # sane round-trip on a quiet T1S strand (< 5 ms)
        record("firmware NTP service (UDP %d)" % NTP_PORT, "PASS" if ok else "FAIL",
               f"{len(delays)}/5 replies, min RTT {best:.0f} us")
    except Exception as e:
        record("firmware NTP service (UDP %d)" % NTP_PORT, "FAIL", str(e))
    finally:
        s.close()


# --- 5) on-board console + stack (optional, UART) ----------------------------------
def check_uart(args):
    if not args.com:
        record("on-board console (UART)", "SKIP", "no --com given")
        return
    try:
        import serial
    except ImportError:
        record("on-board console (UART)", "SKIP", "pyserial not installed")
        return
    try:
        ser = serial.Serial(args.com, 115200, timeout=0.1)
    except Exception as e:
        record("on-board console (UART)", "SKIP", f"{args.com} unavailable ({e})")
        return
    try:
        time.sleep(0.4); ser.write(b"\r"); ser.flush(); time.sleep(0.3)
        ser.reset_input_buffer(); ser.write(b"ntp\r"); ser.flush()
        end = time.time() + 2.0; out = b""
        while time.time() < end:
            n = ser.in_waiting
            out += ser.read(n) if n else b""
            if not n:
                time.sleep(0.02)
        txt = out.decode(errors="replace")
        if "NTP time counter" in txt and "SYS_TIME" in txt:
            src = re.search(r"source\s*:\s*([^\r\n]+)", txt)
            record("on-board console (UART)", "PASS",
                   "ntp responded" + (f"; {src.group(1).strip()}" if src else ""))
        else:
            record("on-board console (UART)", "FAIL", "no valid 'ntp' output")
    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser(description="Smoke test for the T1S<->100BASE-T bridge firmware")
    ap.add_argument("--bridge", default="192.168.0.181", help="bridge eth1 IP")
    ap.add_argument("--endpoint", default="192.168.0.54", help="LAN866x endpoint IP (behind the bridge)")
    ap.add_argument("--com", default=None, help="serial port for the optional UART check (e.g. COM8)")
    args = ap.parse_args()

    print(f"== bridge firmware smoke test ==  bridge={args.bridge} endpoint={args.endpoint}"
          + (f" com={args.com}" if args.com else ""))
    check_bridge(args)
    check_forwarding(args)
    check_discovery(args)
    check_ntp(args)
    check_uart(args)

    npass = sum(1 for _, s, _ in results if s == "PASS")
    nfail = sum(1 for _, s, _ in results if s == "FAIL")
    nskip = sum(1 for _, s, _ in results if s == "SKIP")
    print(f"\n== result: {npass} pass, {nfail} fail, {nskip} skip ==")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
