#!/usr/bin/env python3
"""
flash_dual.py
-------------
Flasht die zwei Firmware-Varianten auf die korrekten Boards:

  GM       (nodeId=0)  →  dist/gm/        →  GRANDMASTER-Board  (SN: ATML3264031800001290)
  Follower (nodeId=1)  →  dist/follower/  →  FOLLOWER-Board     (SN: ATML3264031800001049)

Voraussetzung: build_dual.bat muss vorher ausgeführt worden sein.

Aufruf:  python flash_dual.py
"""
import sys
import os
import argparse

_HERE = os.path.dirname(os.path.abspath(__file__))

HEX_GM       = os.path.join(_HERE, r"dist\gm\T1S_100BaseT_Bridge.X.production.hex")
HEX_FOLLOWER = os.path.join(_HERE, r"dist\follower\T1S_100BaseT_Bridge.X.production.hex")

FOLLOWER_SERIAL    = "ATML3264031800001049"
GRANDMASTER_SERIAL = "ATML3264031800001290"

sys.path.insert(0, _HERE)
from mdb_flash import flash

def main():
    ap = argparse.ArgumentParser(description="Flash dual images via MDB")
    ap.add_argument("--swd-khz", type=int, default=2000,
                    help="Requested SWD clock in kHz (best effort, default: 2000)")
    args = ap.parse_args()

    errors = 0

    for hex_path in (HEX_GM, HEX_FOLLOWER):
        if not os.path.isfile(hex_path):
            print(f"[ERROR] HEX nicht gefunden: {hex_path}")
            print("        Bitte zuerst build_dual.bat ausführen.")
            return 1

    print("\n### Flash FOLLOWER (nodeId=1) ###")
    rc = flash(HEX_FOLLOWER, FOLLOWER_SERIAL, label="FOLLOWER", swd_khz=args.swd_khz)
    if rc != 0:
        print("[FOLLOWER] FEHLER beim Programmieren!")
        errors += 1

    print("\n### Flash GRANDMASTER (nodeId=0) ###")
    rc = flash(HEX_GM, GRANDMASTER_SERIAL, label="GRANDMASTER", swd_khz=args.swd_khz)
    if rc != 0:
        print("[GRANDMASTER] FEHLER beim Programmieren!")
        errors += 1

    print()
    if errors == 0:
        print("=== Beide Boards erfolgreich programmiert und gestartet. ===")
        print("    FOLLOWER    (nodeId=1): " + HEX_FOLLOWER)
        print("    GRANDMASTER (nodeId=0): " + HEX_GM)
    else:
        print(f"=== {errors} Board(s) konnten nicht programmiert werden. ===")

    return errors

if __name__ == "__main__":
    sys.exit(main())
