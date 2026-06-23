#!/usr/bin/env python3
"""
flash_single.py
---------------
Programmiert ein einziges Firmware-Image auf beide Boards (GM + Follower).

Beide Boards erhalten dieselbe Firmware (Single-Image).
Die Rolle (GM / Follower) wird nach dem Start per CLI gesetzt:
  GM-Board:       plca_node 0  →  ptp_mode master
  Follower-Board: plca_node 1  →  ptp_mode follower

Voraussetzung: build_single_cmake.bat muss vorher ausgeführt worden sein.

Aufruf:
  python flash_single.py
  python flash_single.py --hex <pfad/zur/firmware.hex>
  python flash_single.py --gm-only
  python flash_single.py --fol-only
"""

import sys
import os
import argparse

_HERE = os.path.dirname(os.path.abspath(__file__))

HEX_DEFAULT = os.path.join(_HERE, r"dist\single\T1S_100BaseT_Bridge.X.production.hex")

FOLLOWER_SERIAL    = "ATML3264031800001049"
GRANDMASTER_SERIAL = "ATML3264031800001290"

sys.path.insert(0, _HERE)
from mdb_flash import flash


def main():
    ap = argparse.ArgumentParser(
        description="Flash single firmware image auf beide Boards via MDB"
    )
    ap.add_argument(
        "--hex", default=HEX_DEFAULT,
        help=f"Pfad zur HEX-Datei (default: {HEX_DEFAULT})"
    )
    ap.add_argument(
        "--swd-khz", type=int, default=2000,
        help="SWD-Takt in kHz (default: 2000)"
    )
    ap.add_argument(
        "--gm-only", action="store_true",
        help="Nur das GM-Board programmieren"
    )
    ap.add_argument(
        "--fol-only", action="store_true",
        help="Nur das Follower-Board programmieren"
    )
    args = ap.parse_args()

    hex_path = os.path.abspath(args.hex)

    if not os.path.isfile(hex_path):
        print(f"[ERROR] HEX nicht gefunden: {hex_path}")
        print("        Bitte zuerst build_single_cmake.bat ausführen.")
        return 1

    print(f"\n=== Flash Single Firmware ===")
    print(f"    HEX: {hex_path}")
    print()

    errors = 0

    if not args.gm_only:
        print("### Flash FOLLOWER ###")
        rc = flash(hex_path, FOLLOWER_SERIAL, label="FOLLOWER", swd_khz=args.swd_khz)
        if rc != 0:
            print("[FOLLOWER] FEHLER beim Programmieren!")
            errors += 1
        else:
            print("[FOLLOWER] OK")
        print()

    if not args.fol_only:
        print("### Flash GRANDMASTER ###")
        rc = flash(hex_path, GRANDMASTER_SERIAL, label="GRANDMASTER", swd_khz=args.swd_khz)
        if rc != 0:
            print("[GRANDMASTER] FEHLER beim Programmieren!")
            errors += 1
        else:
            print("[GRANDMASTER] OK")
        print()

    if errors == 0:
        print("=== Beide Boards erfolgreich programmiert. ===")
        print()
        print("Nächste Schritte:")
        print("  GM-Board    (COM10):  plca_node 0  →  ptp_mode master")
        print("  Follower    (COM8):   plca_node 1  →  ptp_mode follower")
    else:
        print(f"=== {errors} Board(s) konnten nicht programmiert werden. ===")

    return errors


if __name__ == "__main__":
    sys.exit(main())
