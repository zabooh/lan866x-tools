#!/usr/bin/env python3
"""
setup_debug.py
--------------
One-time fix for SAME54_DFP tool pack 1.6.762 Jython bug:

    NameError: global name 'is_debug_build' is not defined

The script dap_cortex-m4.py in the SAME54_DFP pack references 'is_debug_build'
in get_crc_skiplist() and _erase_internal() but the variable is never defined at
module level in tool pack versions <= 1.6.762. This causes VS Code debugging via
the mplab-core-da extension to fail at the "Erasing..." step.

Fix: insert  is_debug_build = False  after the other global declarations.

Usage:
  python setup_debug.py          # apply fix (idempotent)
  python setup_debug.py --check  # check only, no modification
"""

import os
import re
import sys
import glob
import argparse

PACKS_ROOT = os.path.join(os.path.expanduser("~"), ".mchp_packs", "Microchip")
DFP_GLOB   = os.path.join(PACKS_ROOT, "SAME54_DFP", "*", "scripts", "dap_cortex-m4.py")

MARKER     = "is_debug_build = False"
ANCHOR     = "comm_iface = True # default to swd"
INSERT     = "\nis_debug_build = False  # workaround: tool pack <= 1.6.762 does not inject this variable"


def find_scripts():
    return glob.glob(DFP_GLOB)


def fix_script(path, check_only=False):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    if MARKER in content:
        print(f"[OK]      Already patched: {path}")
        return True

    if ANCHOR not in content:
        print(f"[SKIP]    Anchor line not found (different DFP version?): {path}")
        return False

    if check_only:
        print(f"[MISSING] Patch not applied: {path}")
        return False

    new_content = content.replace(ANCHOR, ANCHOR + INSERT, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(new_content)
    print(f"[FIXED]   Patch applied: {path}")
    return True


def main():
    ap = argparse.ArgumentParser(description="Fix SAME54_DFP is_debug_build Jython bug")
    ap.add_argument("--check", action="store_true", help="Check only, do not modify")
    args = ap.parse_args()

    scripts = find_scripts()
    if not scripts:
        print(f"[ERROR] No SAME54_DFP scripts found under: {PACKS_ROOT}")
        print("        Install the SAME54_DFP pack via MPLAB X or MPLAB Code Configurator first.")
        sys.exit(1)

    results = [fix_script(s, check_only=args.check) for s in scripts]

    if all(results):
        print("\nAll scripts OK. VS Code debugging should work.")
    else:
        print("\nSome scripts could not be patched. Check output above.")
        sys.exit(1)


if __name__ == "__main__":
    main()
