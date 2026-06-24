#!/usr/bin/env python3
"""
cli.py - Send CLI commands to the bridge firmware over its EDBG virtual COM port.

Usage:
  python cli.py "netinfo" "bridge status"
  python cli.py --port COM8 --read 3 "ping 192.168.0.54 i LAN865x n 4"
  python cli.py --listen 8          # just stream output for 8 s (e.g. watch ipdump)

Each positional argument is one CLI command.  After sending all commands the
tool keeps reading for --read seconds so async/deferred output is captured.
"""
import argparse
import sys
import time

import serial  # pyserial


def drain(ser, seconds):
    """Read for at least `seconds` (a fixed window that never shrinks), plus a
    little longer while data keeps flowing. The fixed floor matters because the
    firmware can block in a CLI handler for a few seconds (RCP round-trips) and
    only flush its output afterwards."""
    out = bytearray()
    floor = time.time() + seconds          # minimum window, regardless of silence
    deadline = floor
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
            deadline = max(floor, time.time() + 0.5)  # keep going while data flows
        else:
            time.sleep(0.02)
    return out.decode("utf-8", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("commands", nargs="*", help="CLI commands to send")
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--read", type=float, default=2.0,
                    help="seconds to keep reading after each command")
    ap.add_argument("--listen", type=float, default=0.0,
                    help="just stream output for N seconds, then exit")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    try:
        # Nudge the prompt and flush any boot/backlog noise.
        ser.reset_input_buffer()
        ser.write(b"\r\n")
        time.sleep(0.2)
        ser.reset_input_buffer()

        if args.listen > 0:
            sys.stdout.write(drain(ser, args.listen))
            sys.stdout.flush()
            return 0

        for cmd in args.commands:
            ser.write((cmd + "\r\n").encode("ascii"))
            ser.flush()
            print(f"\n===> {cmd}")
            sys.stdout.write(drain(ser, args.read))
            sys.stdout.flush()
        print()
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
