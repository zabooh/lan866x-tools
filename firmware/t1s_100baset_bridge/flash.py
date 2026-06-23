#!/usr/bin/env python3
"""
flash.py
--------
Program the T1S<->100BASE-T bridge firmware onto the SAME54 board via MPLAB MDB.

Single board (unlike the two-board net_10base_t1s flasher).  By default it
auto-detects the one connected EDBG/PKOB debugger; pass --serial to pick a
specific one when several are attached.

Default HEX is the build output:
  firmware/T1S_100BaseT_Bridge.X/out/T1S_100BaseT_Bridge/default.hex
Run build.bat first to (re)generate it.

Usage:
  python flash.py
  python flash.py --hex <path/to/firmware.hex>
  python flash.py --serial ATML3264031800001049
"""

import sys
import os
import argparse

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
from mdb_flash import flash  # noqa: E402

HEX_DEFAULT = os.path.join(
    _HERE,
    "firmware", "T1S_100BaseT_Bridge.X", "out", "T1S_100BaseT_Bridge", "default.hex",
)


def _is_microchip_debugger(p):
    if p.vid == 0x03EB:
        return True
    if p.serial_number and p.serial_number.startswith("ATML"):
        return True
    mfr = (p.manufacturer or "").lower()
    return "microchip" in mfr or "atmel" in mfr


def _autodetect_serial():
    """Return the serial of the single connected debugger, or None/ambiguous."""
    try:
        import serial.tools.list_ports as lp
    except ImportError:
        return None, "pyserial not installed (pip install pyserial)"
    dbgs = [p for p in lp.comports() if _is_microchip_debugger(p) and p.serial_number]
    # De-duplicate by serial (EDBG exposes several interfaces under one SN).
    serials = sorted({p.serial_number for p in dbgs})
    if not serials:
        return None, "no Microchip/Atmel debugger found"
    if len(serials) > 1:
        return None, f"multiple debuggers found ({', '.join(serials)}); pass --serial"
    return serials[0], None


def main():
    ap = argparse.ArgumentParser(description="Flash the bridge firmware via MDB")
    ap.add_argument("--hex", default=HEX_DEFAULT, help=f"HEX file (default: {HEX_DEFAULT})")
    ap.add_argument("--serial", default=None, help="Programmer serial (ATML...). Auto-detected if omitted.")
    ap.add_argument("--mcu", default="ATSAME54P20A", help="Target MCU")
    ap.add_argument("--swd-khz", type=int, default=2000, help="SWD clock in kHz (default: 2000)")
    args = ap.parse_args()

    hex_path = os.path.abspath(args.hex)
    if not os.path.isfile(hex_path):
        print(f"[ERROR] HEX file not found: {hex_path}")
        print("        Run build.bat first.")
        return 1

    serial = args.serial
    if serial is None:
        serial, err = _autodetect_serial()
        if serial is None:
            print(f"[ERROR] {err}")
            return 1
        print(f"[INFO] Auto-detected programmer: {serial}")

    print(f"\n=== Flash T1S_100BaseT_Bridge ===")
    print(f"    HEX: {hex_path}")
    print()
    rc = flash(hex_path, serial, mcu=args.mcu, label="BRIDGE", swd_khz=args.swd_khz)
    if rc != 0:
        print("[BRIDGE] ERROR: Programming failed!")
    else:
        print("[BRIDGE] OK")
    return rc


if __name__ == "__main__":
    sys.exit(main())
