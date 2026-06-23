#!/usr/bin/env python3
"""
flash_all.py
------------
Programmiert beide Boards (Follower + Grandmaster) nacheinander via MDB
und gibt den Controller nach dem Flash automatisch frei (reset + run).

Aufruf:
  python flash_all.py
"""
import sys
import os
import argparse

# --- Konfiguration -----------------------------------------------------------
_HERE = os.path.dirname(os.path.abspath(__file__))

HEX_FILE         = os.path.join(_HERE, r"dist\default\production\T1S_100BaseT_Bridge.X.production.hex")
FOLLOWER_SERIAL  = "ATML3264031800001049"
GRANDMASTER_SERIAL = "ATML3264031800001290"
# -----------------------------------------------------------------------------

# mdb_flash.py liegt im selben Verzeichnis
sys.path.insert(0, _HERE)
from mdb_flash import flash

def main():
    ap = argparse.ArgumentParser(description="Flash both boards via MDB")
    ap.add_argument("--swd-khz", type=int, default=2000,
                    help="Requested SWD clock in kHz (best effort, default: 2000)")
    args = ap.parse_args()

    errors = 0

    print("\n### Flash FOLLOWER ###")
    rc = flash(HEX_FILE, FOLLOWER_SERIAL, label="FOLLOWER", swd_khz=args.swd_khz)
    if rc != 0:
        print("[FOLLOWER] FEHLER beim Programmieren!")
        errors += 1

    print("\n### Flash GRANDMASTER ###")
    rc = flash(HEX_FILE, GRANDMASTER_SERIAL, label="GRANDMASTER", swd_khz=args.swd_khz)
    if rc != 0:
        print("[GRANDMASTER] FEHLER beim Programmieren!")
        errors += 1

    print()
    if errors == 0:
        print("=== Beide Boards erfolgreich programmiert und gestartet. ===")
    else:
        print(f"=== {errors} Board(s) konnten nicht programmiert werden. ===")

    return errors

if __name__ == "__main__":
    sys.exit(main())
