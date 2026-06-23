#!/usr/bin/env python3
"""
LAN8651 PMD register read/write access test.

This tool talks to the firmware console via serial commands:
- lan_read 0xAAAAAAAA
- lan_write 0xAAAAAAAA VVVV

Default PMD registers:
- PMD_CONTROL: 0x00030001 (read/write)
- PMD_STATUS : 0x00030002 (read-only in most implementations)
"""

import argparse
import re
import sys
import time

import serial

PMD_CONTROL = 0x00030001
PMD_STATUS = 0x00030002
CFD_ENABLE_BIT = 0x0002


def read_until_prompt(ser, timeout_s=2.0):
    """Read serial response until prompt appears or timeout occurs."""
    end_time = time.time() + timeout_s
    response = ""

    while time.time() < end_time:
        if ser.in_waiting > 0:
            response += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            if response.rstrip().endswith(">"):
                break
        time.sleep(0.01)

    return response


def send_command(ser, command, timeout_s=2.0):
    ser.reset_input_buffer()
    ser.write(f"{command}\r\n".encode("ascii"))
    return read_until_prompt(ser, timeout_s=timeout_s)


def parse_read_value(response):
    """Extract the read value from a firmware response string."""
    # Typical pattern: "LAN865X Read: [0x00030001] = 0x00000002"
    # Use the right-hand side of the last '=' to avoid parsing command echo text.
    if "=" not in response:
        return None

    rhs = response.rsplit("=", 1)[1].strip()
    token_match = re.search(r"(0x[0-9A-Fa-f]+|\d+)", rhs)
    if not token_match:
        return None

    token = token_match.group(1)
    if token.lower().startswith("0x"):
        return int(token, 16)
    return int(token, 10)


def read_register(ser, address, timeout_s=2.0):
    response = send_command(ser, f"lan_read 0x{address:08X}", timeout_s=timeout_s)
    value = parse_read_value(response)
    return value, response


def write_register(ser, address, value, timeout_s=2.0):
    response = send_command(ser, f"lan_write 0x{address:08X} {value}", timeout_s=timeout_s)
    ok = "OK" in response or response.rstrip().endswith(">")
    return ok, response


def fmt_hex32(value):
    return f"0x{value:08X}"


def main():
    parser = argparse.ArgumentParser(description="Test PMD read/write access on LAN8651")
    parser.add_argument("--port", default="COM8", help="Serial port (default: COM8)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="Command timeout in seconds")
    args = parser.parse_args()

    print("=" * 72)
    print("LAN8651 PMD Register Read/Write Access Test")
    print("=" * 72)
    print(f"Port: {args.port}, Baud: {args.baud}")

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            time.sleep(0.5)

            failures = 0

            print("\n[1] Read PMD_CONTROL")
            ctrl_before, raw = read_register(ser, PMD_CONTROL, timeout_s=args.timeout)
            if ctrl_before is None:
                print("  FAIL: PMD_CONTROL could not be read")
                print(f"  Raw response: {raw.strip()}")
                return 2
            print(f"  OK  : PMD_CONTROL = {fmt_hex32(ctrl_before)}")

            print("\n[2] Read PMD_STATUS")
            status_before, raw = read_register(ser, PMD_STATUS, timeout_s=args.timeout)
            if status_before is None:
                print("  WARN: PMD_STATUS could not be read")
                print(f"  Raw response: {raw.strip()}")
                failures += 1
            else:
                print(f"  OK  : PMD_STATUS  = {fmt_hex32(status_before)}")

            print("\n[3] Write/Read verify on PMD_CONTROL (toggle CFD_ENABLE bit)")
            ctrl_test = ctrl_before ^ CFD_ENABLE_BIT
            ok, raw = write_register(ser, PMD_CONTROL, ctrl_test, timeout_s=args.timeout)
            if not ok:
                print("  FAIL: Write to PMD_CONTROL returned no acknowledgement")
                print(f"  Raw response: {raw.strip()}")
                failures += 1
            else:
                ctrl_after, raw = read_register(ser, PMD_CONTROL, timeout_s=args.timeout)
                if ctrl_after is None:
                    print("  FAIL: PMD_CONTROL readback failed after write")
                    print(f"  Raw response: {raw.strip()}")
                    failures += 1
                elif ctrl_after != ctrl_test:
                    print("  FAIL: PMD_CONTROL readback mismatch")
                    print(f"  Expected: {fmt_hex32(ctrl_test)}, Got: {fmt_hex32(ctrl_after)}")
                    failures += 1
                else:
                    print(f"  OK  : Readback matches ({fmt_hex32(ctrl_after)})")

            print("\n[4] Restore PMD_CONTROL original value")
            ok, raw = write_register(ser, PMD_CONTROL, ctrl_before, timeout_s=args.timeout)
            if not ok:
                print("  FAIL: Could not restore PMD_CONTROL")
                print(f"  Raw response: {raw.strip()}")
                failures += 1
            else:
                ctrl_restored, raw = read_register(ser, PMD_CONTROL, timeout_s=args.timeout)
                if ctrl_restored != ctrl_before:
                    print("  FAIL: PMD_CONTROL restore verification failed")
                    if ctrl_restored is None:
                        print(f"  Raw response: {raw.strip()}")
                    else:
                        print(f"  Expected: {fmt_hex32(ctrl_before)}, Got: {fmt_hex32(ctrl_restored)}")
                    failures += 1
                else:
                    print(f"  OK  : Restored to {fmt_hex32(ctrl_restored)}")

            print("\n[5] Optional write attempt on PMD_STATUS (usually read-only)")
            if status_before is None:
                print("  SKIP: PMD_STATUS baseline is missing")
            else:
                status_test = status_before ^ 0x0001
                ok, raw = write_register(ser, PMD_STATUS, status_test, timeout_s=args.timeout)
                status_after, raw_read = read_register(ser, PMD_STATUS, timeout_s=args.timeout)
                if status_after is None:
                    print("  WARN: PMD_STATUS unreadable after write attempt")
                    failures += 1
                elif status_after == status_before:
                    print("  OK  : PMD_STATUS unchanged (register behaves read-only)")
                elif ok:
                    print("  WARN: PMD_STATUS changed after write attempt")
                    print(f"  Before: {fmt_hex32(status_before)}, After: {fmt_hex32(status_after)}")
                    print("  Note: verify this register behavior in your datasheet/firmware.")
                    failures += 1
                else:
                    print("  WARN: PMD_STATUS behavior unclear")
                    print(f"  Write response: {raw.strip()}")
                    print(f"  Read response : {raw_read.strip()}")
                    failures += 1

            print("\n" + "=" * 72)
            if failures == 0:
                print("RESULT: PASS (PMD register access test successful)")
                return 0

            print(f"RESULT: FAIL ({failures} issue(s) found)")
            return 1

    except serial.SerialException as exc:
        print(f"ERROR: Could not open serial port {args.port}: {exc}")
        return 3
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        return 130


if __name__ == "__main__":
    sys.exit(main())
