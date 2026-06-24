#!/usr/bin/env python3
"""
setup_flasher.py
----------------
Detect the connected Microchip/Atmel EDBG debugger (Curiosity Nano / SAME54
Curiosity) and save its serial number to setup_flasher.config (JSON), so
flash.py can program the bridge board without re-detecting.

Single board (unlike the two-board net_10base_t1s flasher): the bridge is one
SAME54. If several debuggers are plugged in, you are asked to pick the bridge's.

Usage:
  python setup_flasher.py
"""

import sys
import json
import os
import serial.tools.list_ports

_HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(_HERE, "setup_flasher.config")


def _is_microchip_debugger(p):
    if p.vid == 0x03EB:
        return True
    if p.serial_number and p.serial_number.startswith("ATML"):
        return True
    mfr = (p.manufacturer or "").lower()
    return "microchip" in mfr or "atmel" in mfr


def _com_num(p):
    for part in p.device.split("COM"):
        if part.isdigit():
            return int(part)
    return 9999


def _find_debuggers():
    ports = [p for p in serial.tools.list_ports.comports() if _is_microchip_debugger(p)]
    return sorted(ports, key=_com_num)


def main():
    print("=" * 60)
    print("  setup_flasher.py - Bridge board configuration")
    print("=" * 60)
    print()

    dbgs = _find_debuggers()
    if not dbgs:
        print("[ERROR] No Microchip/Atmel debugger found.")
        print("        Connect the bridge board via its USB debugger port and retry.")
        return 1

    for i, p in enumerate(dbgs):
        print(f"  [{i + 1}] {p.device}  SN={p.serial_number or 'N/A'}  {p.description}")
    print()

    if len(dbgs) == 1:
        board = dbgs[0]
        print(f"One debugger found -> using {board.device} (SN: {board.serial_number}).")
    else:
        while True:
            raw = input(f"Which one is the BRIDGE board? (1-{len(dbgs)}): ").strip()
            if raw.isdigit() and 1 <= int(raw) <= len(dbgs):
                board = dbgs[int(raw) - 1]
                break
            print("  Invalid input, try again.")

    config = {
        "board": {
            "serial":      board.serial_number or "",
            "com_port":    board.device,
            "description": board.description or "",
        }
    }
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=4)

    print()
    print(f"[OK] Saved: {CONFIG_FILE}")
    print(f"     Board : {board.device}  SN={board.serial_number}")
    print()
    print("Done. flash.py will now program this board (the console is its EDGB COM port).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
