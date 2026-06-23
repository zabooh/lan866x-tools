#!/usr/bin/env python3
"""
mdb_flash.py
------------
Flash a HEX file via MPLAB MDB (Debugger Backend) and release the device
from reset with 'run' afterwards.

Avoids ipecmd's behaviour of leaving the MCU held in reset after programming.
The approach is taken from:
  C:\\work\\ptp\\T1S_Autoconf\\tools\\multi_progger.pyw

Usage:
  python mdb_flash.py --hex <path.hex> --serial <ATML...> [--label <name>]

Optional overrides:
  --mdb     Path to mdb.bat  (default: auto-detected for MPLABX v6.25)
  --mcu     Target MCU       (default: ATSAME54P20A)
  --hwtool  Programmer type  (default: PKOB4)
"""
import subprocess
import sys
import argparse
import time
import os
import re

MDB_DEFAULT  = r"C:\Program Files\Microchip\MPLABX\v6.25\mplab_platform\bin\mdb.bat"
MCU_DEFAULT  = "ATSAME54P20A"
TOOL_DEFAULT = "edbg"
SWD_KHZ_DEFAULT = 2000


def _wait_prompt(proc, timeout=60):
    """Read MDB stdout byte-by-byte until the '>' prompt appears or timeout."""
    output = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        ch = proc.stdout.read(1)
        if not ch:
            break
        output += ch.decode("utf-8", errors="ignore")
        if output.endswith(">"):
            break
    return output


def _cmd(proc, cmd, label="", timeout=120):
    """Send one MDB command and wait for the prompt; print I/O."""
    prefix = f"[{label}] " if label else ""
    proc.stdin.write((cmd + "\n").encode("utf-8"))
    proc.stdin.flush()
    print(f"{prefix}MDB << {cmd.strip()}")
    output = _wait_prompt(proc, timeout)
    for line in output.splitlines():
        stripped = line.strip()
        if stripped and stripped != ">":
            print(f"{prefix}MDB >> {stripped}")
    return output


def _cmd_ok(output):
    """Heuristic success check for MDB command output."""
    return re.search(r"(error|failed|unknown|invalid)", output, re.IGNORECASE) is None


def _set_swd_speed(proc, swd_khz, label=""):
    """
    Try known MDB property names for SWD frequency.
    Returns True if one property appears accepted.
    """
    if swd_khz is None:
        return True

    attempts = [
        f"set communication.speed {swd_khz}",
        f"set communication.frequency {swd_khz}",
        f"set communication.clock {swd_khz}",
    ]

    print(f"[{label}] INFO: Request SWD speed = {swd_khz} kHz")
    for cmd in attempts:
        out = _cmd(proc, cmd, label=label, timeout=15)
        if _cmd_ok(out):
            print(f"[{label}] INFO: SWD speed set via '{cmd}'")
            return True

    print(f"[{label}] WARNING: Could not set SWD speed via MDB properties; using tool default.")
    return False


def _find_tool_index(proc, hwtool, serial, label=""):
    """
    Run 'hwtool' (no args) to list all connected tools, then return
    (tool_type, index) matching *serial*.
    MDB output format per line:  <index>  <ToolType>  <Serial>  ...
    Falls back to 'hwtool <type>' if the bare listing is empty.
    """
    import re
    raw = _cmd(proc, "hwtool", label=label, timeout=30)
    matches = re.findall(r'\s*(\d+)\s+(\w+)\s+(\w+)', raw)
    for idx, tool_type, sn in matches:
        if sn == serial:
            return tool_type, idx
    # Fallback: try with explicit tool type
    if hwtool:
        raw2 = _cmd(proc, f"hwtool {hwtool}", label=label, timeout=30)
        matches2 = re.findall(r'\s*(\d+)\s+(\w+)\s+(\w+)', raw2)
        for idx, tool_type, sn in matches2:
            if sn == serial:
                return tool_type, idx
    print(f"[{label}] WARNING: serial {serial} not found. hwtool output:")
    for line in raw.splitlines():
        if line.strip() and not line.strip().startswith("Mar ") and "SLF4J" not in line:
            print(f"[{label}]   {line.strip()}")
    return None, None


def flash(hex_file, serial, mdb_path=MDB_DEFAULT, mcu=MCU_DEFAULT,
          hwtool=TOOL_DEFAULT, label="", swd_khz=SWD_KHZ_DEFAULT):
    """
    Program *hex_file* onto the device identified by *serial* and then
    issue 'reset' + 'run' so the MCU starts executing immediately.

    Returns 0 on success, 1 on error.
    """
    if not os.path.isfile(hex_file):
        print(f"[{label}] ERROR: HEX file not found: {hex_file}")
        return 1
    if not os.path.isfile(mdb_path):
        print(f"[{label}] ERROR: mdb.bat not found: {mdb_path}")
        return 1

    print(f"\n{'='*60}")
    print(f"  Flash{' ' + label if label else ''}: {mcu}")
    print(f"  Tool: {hwtool}  SN={serial}")
    if swd_khz is not None:
        print(f"  SWD : requested {swd_khz} kHz")
    print(f"  HEX : {hex_file}")
    print(f"{'='*60}")

    proc = subprocess.Popen(
        [mdb_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    # Wait for the initial MDB prompt
    _wait_prompt(proc, timeout=30)

    _cmd(proc, f"device {mcu}",               label=label)
    _cmd(proc, "set AutoSelectMemRanges auto", label=label)
    _cmd(proc, "set communication.interface swd", label=label)

    # Find the port index for the requested serial number
    tool_type, idx = _find_tool_index(proc, hwtool, serial, label)
    if idx is None:
        print(f"[{label}] ERROR: programmer {serial} not found — is it connected?")
        _cmd(proc, "quit", label=label, timeout=10)
        proc.wait(timeout=15)
        return 1

    _cmd(proc, f"hwtool {tool_type} -p {idx}", label=label, timeout=30)

    # Optional flash speed tuning (supported property name depends on MDB/tool backend).
    _set_swd_speed(proc, swd_khz, label=label)

    result = _cmd(proc, f'program "{hex_file}"', label=label, timeout=120)

    if any(kw in result for kw in ("Error", "error", "FAILED", "failed")):
        print(f"[{label}] FLASH FAILED — aborting")
        _cmd(proc, "quit", label=label, timeout=10)
        proc.wait(timeout=15)
        return 1

    # Release device from reset so it starts running immediately
    _cmd(proc, "reset", label=label, timeout=15)
    _cmd(proc, "run",   label=label, timeout=15)
    _cmd(proc, "quit",  label=label, timeout=10)

    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()

    print(f"[{label}] SUCCESS: Device programmed and running.")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description="Flash via MDB and release device from reset")
    ap.add_argument("--hex",     required=True,         help="Path to .hex file")
    ap.add_argument("--serial",  required=True,         help="Programmer serial (ATML…)")
    ap.add_argument("--label",   default="",            help="Human-readable board name for log output")
    ap.add_argument("--mdb",     default=MDB_DEFAULT,   help="Path to mdb.bat")
    ap.add_argument("--mcu",     default=MCU_DEFAULT,   help="Target MCU")
    ap.add_argument("--hwtool",  default=TOOL_DEFAULT,  help="Programmer tool type")
    ap.add_argument("--swd-khz", type=int, default=SWD_KHZ_DEFAULT,
                    help="Requested SWD clock in kHz (best effort, default: 2000)")
    args = ap.parse_args()
    sys.exit(flash(args.hex, args.serial, args.mdb, args.mcu,
                   args.hwtool, args.label, args.swd_khz))
