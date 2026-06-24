#!/usr/bin/env python3
"""
someip_size.py - RAM/ROM footprint of the SOME/IP components in the bridge build.

Reads the per-object sizes (xc32-size) of the compiled .c.o files from the
CMake/Ninja build tree and groups them into the SOME/IP components:

  - the SOME/IP stack (libepmicrochip/libsomeip/src/someip-*.c),
  - the RCP wrapper (rcp.c) + the platform-neutral stub (someip_stub.c),
  - the MCU platform port (plat_h3tcpip.c),
  - and each endpoint demo/tool CLI group (lan866x_cli, clickdemo_cli,
    gpio_cli, i2c_cli, spi_cli, sys_cli, dncp_cli).

ROM (flash) = .text + .data   (initialised data is stored in flash and copied
                               to RAM at startup)
RAM (SRAM)  = .data + .bss

Run after a build (build.bat). Usage:
  python someip_size.py [--build-dir <dir>] [--size <xc32-size.exe>] [--bytes]
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# component label -> list of object basenames (without the trailing .c.o), and group
#   group "core"  = the reusable SOME/IP client (stack + rcp + stub + port)
#   group "tools" = the endpoint demos/tools (one CLI file per command group)
COMPONENTS = [
    ("SOME/IP stack: someip-client",  ["someip-client"],  "core"),
    ("SOME/IP stack: someip-gen",     ["someip-gen"],      "core"),
    ("SOME/IP stack: someip-pars",    ["someip-pars"],     "core"),
    ("SOME/IP stack: someip-timer",   ["someip-timer"],    "core"),
    ("SOME/IP stack: someip-transmit",["someip-transmit"], "core"),
    ("RCP wrapper (rcp.c)",           ["rcp"],             "core"),
    ("SOME/IP stub (someip_stub.c)",  ["someip_stub"],     "core"),
    ("Platform port (plat_h3tcpip.c)",["plat_h3tcpip"],    "core"),
    ("lan866x_cli (discovery/diag/ledblink/gpiomax/clickdemo)", ["lan866x_cli"], "tools"),
    ("clickdemo_cli (clickdemo)",     ["clickdemo_cli"],   "tools"),
    ("gpio_cli (gpio/gpioevents/ledtoggle/ledpwm)",        ["gpio_cli"], "tools"),
    ("i2c_cli (i2cscan/i2cid/proxmon/lan8680/proxled)",    ["i2c_cli"],  "tools"),
    ("spi_cli (spi/spiid/thumbmon/adc/pwm)",               ["spi_cli"],  "tools"),
    ("sys_cli (servicetest/boot/uart/video)",              ["sys_cli"],  "tools"),
    ("dncp_cli (dncpmon/dncpdisc)",   ["dncp_cli"],        "tools"),
]


def find_xc32_size(explicit):
    if explicit:
        return explicit
    # bin_dir written by setup_compiler.py
    cfg = os.path.join(HERE, "setup_compiler.config")
    if os.path.isfile(cfg):
        try:
            bd = json.load(open(cfg)).get("bin_dir")
            if bd:
                cand = os.path.join(bd, "xc32-size.exe")
                if os.path.isfile(cand):
                    return cand
                cand = os.path.join(bd, "xc32-size")
                if os.path.isfile(cand):
                    return cand
        except Exception:
            pass
    # newest installed XC32
    pats = [r"C:\Program Files\Microchip\xc32\v*\bin\xc32-size.exe",
            r"C:\Program Files (x86)\Microchip\xc32\v*\bin\xc32-size.exe"]
    found = []
    for p in pats:
        found += glob.glob(p)
    found.sort(reverse=True)
    return found[0] if found else "xc32-size"


def find_build_dir(explicit):
    if explicit:
        return explicit
    # the CMake/Ninja object tree
    root = os.path.join(HERE, "firmware", "T1S_100BaseT_Bridge.X", "_build")
    return root


def object_index(build_dir):
    """basename (without .c.o) -> full path of the object file."""
    idx = {}
    for o in glob.glob(os.path.join(build_dir, "**", "*.c.o"), recursive=True):
        base = os.path.basename(o)[:-4]   # strip ".c.o"
        idx.setdefault(base, o)           # first wins (there is only one per source)
    return idx


def size_of(size_tool, obj):
    """Return (text, data, bss) for one object file via xc32-size (Berkeley)."""
    out = subprocess.run([size_tool, obj], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[0].isdigit():
            return int(f[0]), int(f[1]), int(f[2])
    return 0, 0, 0


def fmt(n, as_bytes):
    return f"{n:>8}" if as_bytes else f"{n/1024.0:>8.2f}"


def main():
    ap = argparse.ArgumentParser(description="RAM/ROM footprint of the SOME/IP components")
    ap.add_argument("--build-dir", default=None, help="CMake _build dir with the .c.o objects")
    ap.add_argument("--size", default=None, help="path to xc32-size")
    ap.add_argument("--bytes", action="store_true", help="print bytes instead of KiB")
    args = ap.parse_args()

    size_tool = find_xc32_size(args.size)
    build_dir = find_build_dir(args.build_dir)
    if not os.path.isdir(build_dir):
        print(f"ERROR: build dir not found: {build_dir}\n       Run build.bat first (or pass --build-dir).")
        return 1
    idx = object_index(build_dir)
    if not idx:
        print(f"ERROR: no .c.o objects under {build_dir}. Build first.")
        return 1

    unit = "bytes" if args.bytes else "KiB"
    print(f"\nSOME/IP RAM/ROM footprint  (bridge firmware)")
    print(f"  size tool : {size_tool}")
    print(f"  objects   : {build_dir}")
    print(f"  ROM = .text + .data (flash) ; RAM = .data + .bss (SRAM) ; values in {unit}\n")
    print(f"  {'component':<52}{'ROM':>9}{'RAM':>9}")
    print(f"  {'-'*52}{'-'*9}{'-'*9}")

    grp_tot = {"core": [0, 0, 0], "tools": [0, 0, 0]}
    missing = []
    last_group = None
    for label, bases, group in COMPONENTS:
        if group != last_group:
            print(f"  == {'SOME/IP core (reusable client)' if group=='core' else 'Endpoint demos / tools'} ==")
            last_group = group
        t = d = b = 0
        present = False
        for base in bases:
            obj = idx.get(base)
            if not obj:
                continue
            present = True
            st, sd, sb = size_of(size_tool, obj)
            t += st; d += sd; b += sb
        if not present:
            missing.append(label)
            continue
        rom, ram = t + d, d + b
        grp_tot[group][0] += t; grp_tot[group][1] += d; grp_tot[group][2] += b
        print(f"  {label:<52}{fmt(rom, args.bytes)}{fmt(ram, args.bytes)}")

    def total_row(name, tdb):
        t, d, b = tdb
        print(f"  {'-'*52}{'-'*9}{'-'*9}")
        print(f"  {name:<52}{fmt(t + d, args.bytes)}{fmt(d + b, args.bytes)}")

    total_row("SOME/IP core total", grp_tot["core"])
    total_row("Endpoint demos / tools total", grp_tot["tools"])
    grand = [grp_tot["core"][i] + grp_tot["tools"][i] for i in range(3)]
    print()
    total_row("GRAND TOTAL (SOME/IP + tools)", grand)

    if missing:
        print("\n  note: no object found for (build out of date?):")
        for m in missing:
            print(f"    - {m}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
