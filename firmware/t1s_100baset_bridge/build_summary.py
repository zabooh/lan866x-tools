r"""
build_summary.py — Post-build memory and interrupt summary for the
                   tcpip_iperf_lan865x Harmony 3 firmware.

Reads:
  * memoryfile.xml  — total flash / RAM from linker
  * mem.map         — linker map (heap size, stack size, section details)
  * default.elf     — via xc32-nm (active interrupt handlers)
  * setup_compiler.config — to locate the XC32 bin dir

Prints a concise human-readable summary to stdout.

Usage (called by build.bat after a successful build):
    python build_summary.py <BUILD_DIR> <ELF_PATH> <XC32_BIN_DIR>
"""

import datetime
import io
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _kib(n: int) -> str:
    return f"{n / 1024:.1f} KiB"


def _pct(used: int, total: int) -> str:
    return f"{100 * used / total:.1f}%" if total else "?%"


def _bar(used: int, total: int, width: int = 30) -> str:
    filled = round(width * used / total) if total else 0
    return "[" + "#" * filled + "-" * (width - filled) + "]"


# ---------------------------------------------------------------------------
# 1. Flash / RAM from memoryfile.xml
# ---------------------------------------------------------------------------

def read_memory_xml(build_dir: str) -> dict:
    path = os.path.join(build_dir, "memoryfile.xml")
    result = {}
    if not os.path.isfile(path):
        return result
    tree = ET.parse(path)
    for mem in tree.getroot().iter("memory"):
        name  = mem.get("name", "").strip()
        used  = int(mem.findtext("used",   "0"))
        total = int(mem.findtext("length", "0"))
        free  = int(mem.findtext("free",   "0"))
        result[name] = {"used": used, "total": total, "free": free}
    return result


# ---------------------------------------------------------------------------
# 2. Heap / Stack from mem.map
# ---------------------------------------------------------------------------

def read_map_file(build_dir: str) -> dict:
    path = os.path.join(build_dir, "mem.map")
    result = {"heap": None, "stack": None, "bss_sections": []}
    if not os.path.isfile(path):
        return result

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # _min_heap_size / _min_stack_size are linker script assigns:
    #   0x0000afa0     _min_heap_size = 0xafa0
    for m in re.finditer(r'_min_heap_size\s*=\s*(0x[0-9a-fA-F]+)', content):
        result["heap"] = int(m.group(1), 16)
    for m in re.finditer(r'_min_stack_size\s*=\s*(0x[0-9a-fA-F]+)', content):
        result["stack"] = int(m.group(1), 16)

    # Text / data / bss totals from the section summary block at the top:
    # .text  0x00000000   0x20431  ...
    # .bss   0x20000180   0x3de0   ...
    for m in re.finditer(r'^(\.text|\.data|\.bss)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)',
                         content, re.MULTILINE):
        result["bss_sections"].append({
            "name": m.group(1),
            "addr": int(m.group(2), 16),
            "size": int(m.group(3), 16),
        })

    return result


# ---------------------------------------------------------------------------
# 3. Active interrupts from xc32-nm
# ---------------------------------------------------------------------------

# Well-known placeholder / non-interrupt symbols to exclude
_EXCLUDE_RE = re.compile(
    r'^(Dummy_Handler|_EventHandlerSPI|CommandPingHandler|'
    r'DRV_SPI_TransferEventHandlerSet|pktEth0Handler|'
    r'TCPIP_ARP_Handler|TCPIP_STACK_PacketHandler|'
    r'TCPIP_TCP_SignalHandler|TCPIP_UDP_SignalHandler|'
    r'TCPIPStack|F_TCPIP|F_DNS|F_Iperf|TCPIP_IPV4|'
    r'lDRV_SPI)',
    re.IGNORECASE,
)

# ARM/SAME54 core (non-peripheral) interrupt handler names
_CORE_INTERRUPTS = {
    "Reset_Handler", "NMI_Handler", "NonMaskableInt_Handler",
    "HardFault_Handler", "MemoryManagement_Handler",
    "BusFault_Handler", "UsageFault_Handler",
    "SVCall_Handler", "SVC_Handler",
    "DebugMonitor_Handler", "PendSV_Handler", "SysTick_Handler",
}

# Suffix patterns that identify hardware interrupt handlers
_IRQ_NAME_RE = re.compile(
    r'(?:_?InterruptHandler|_Handler)$'
)


def read_active_interrupts(elf_path: str, xc32_bin: str) -> tuple[list, list]:
    """
    Returns (core_irqs, peripheral_irqs) — lists of handler name strings.
    Only non-weak (T/t) and known active weak (W but not Dummy_Handler address)
    symbols are included.
    """
    nm_exe = os.path.join(xc32_bin, "xc32-nm.exe")
    if not os.path.isfile(nm_exe):
        return [], []

    try:
        out = subprocess.check_output(
            [nm_exe, "--defined-only", elf_path],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        out = e.output or ""

    # Address of Dummy_Handler — weak handlers redirected there are "not implemented"
    dummy_addr = None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == "Dummy_Handler":
            dummy_addr = parts[0].lstrip("0") or "0"
            break

    core_irqs = []
    peripheral_irqs = []

    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        addr_str, sym_type, name = parts[0], parts[1], parts[2]

        # Must look like a hardware/core handler
        if not _IRQ_NAME_RE.search(name):
            continue

        # Only process globally-visible symbols (T/W); skip file-local (t/w)
        if sym_type not in ("T", "W"):
            continue

        # Skip non-handler utility functions caught by the suffix pattern
        if _EXCLUDE_RE.match(name):
            continue

        # Weak symbol pointing to Dummy_Handler → not implemented
        if sym_type.upper() == "W":
            canon_addr = addr_str.lstrip("0") or "0"
            if dummy_addr and canon_addr == dummy_addr:
                continue  # Default weak alias, not active

        # Strip handler suffix to get the peripheral name
        irq_name = re.sub(r'(_?InterruptHandler|_Handler)$', '', name)

        if name in _CORE_INTERRUPTS:
            core_irqs.append(irq_name)
        else:
            peripheral_irqs.append(irq_name)

    return sorted(set(core_irqs)), sorted(set(peripheral_irqs))


# ---------------------------------------------------------------------------
# 4. Build timestamp from ELF binary
# ---------------------------------------------------------------------------

def read_build_timestamp(elf_path: str) -> tuple[str, str]:
    """
    Scans the ELF binary for the embedded build-time string written by:
        SYS_CONSOLE_PRINT("[APP] Build: " __DATE__ " " __TIME__ "\\r\\n");
    Returns (human_str, tag_str) e.g. ("Apr  8 2026 14:44:18", "20260408_144418")
    or ("", "") if not found.
    """
    try:
        with open(elf_path, "rb") as f:
            data = f.read()
    except OSError:
        return "", ""

    m = re.search(
        rb'\[APP\] Build: ([A-Za-z]{3} [ \d]\d \d{4} \d{2}:\d{2}:\d{2})',
        data,
    )
    if not m:
        return "", ""

    ts_str = m.group(1).decode("ascii", errors="replace")
    try:
        ts_norm = re.sub(r"\s+", " ", ts_str.strip())
        dt = datetime.datetime.strptime(ts_norm, "%b %d %Y %H:%M:%S")
        ts_tag = dt.strftime("%Y%m%d_%H%M%S")
    except ValueError:
        ts_tag = ts_str.replace(" ", "_").replace(":", "")

    return ts_str, ts_tag


# ---------------------------------------------------------------------------
# 5. Format / print summary
# ---------------------------------------------------------------------------

def print_summary(mem: dict, map_info: dict, core_irqs: list, periph_irqs: list,
                  build_ts: str = "") -> str:
    """Prints the build summary to stdout and returns it as a string."""
    buf = io.StringIO()

    def out(s: str = "") -> None:
        print(s)
        buf.write(s + "\n")

    SEP = "=" * 62
    out()
    out(SEP)
    out("  BUILD SUMMARY")
    out(SEP)

    # --- Build timestamp ---
    if build_ts:
        out()
        out(f"  Build      : {build_ts}")

    # --- Flash ---
    if "program" in mem:
        m = mem["program"]
        used, total, free = m["used"], m["total"], m["free"]
        pct = _pct(used, total)
        out()
        out(f"  Flash (program memory)")
        out(f"    Used   : {used:>8,} bytes  ({_kib(used):>10})  {pct}")
        out(f"    Free   : {free:>8,} bytes  ({_kib(free):>10})")
        out(f"    Total  : {total:>8,} bytes  ({_kib(total):>10})")
        out(f"    {_bar(used, total)}")

    # --- RAM ---
    if "data" in mem:
        m = mem["data"]
        used, total, free = m["used"], m["total"], m["free"]
        pct = _pct(used, total)
        out()
        out(f"  RAM (data memory)")
        out(f"    Used   : {used:>8,} bytes  ({_kib(used):>10})  {pct}")
        out(f"    Free   : {free:>8,} bytes  ({_kib(free):>10})")
        out(f"    Total  : {total:>8,} bytes  ({_kib(total):>10})")
        out(f"    {_bar(used, total)}")

    # --- Heap / Stack ---
    out()
    out("  Linker-Reserved Regions")
    if map_info.get("heap") is not None:
        h = map_info["heap"]
        out(f"    Heap   : {h:>8,} bytes  ({_kib(h):>10})  (_min_heap_size)")
    else:
        out("    Heap   :       -- not found in map --")

    if map_info.get("stack") is not None:
        s = map_info["stack"]
        out(f"    Stack  : {s:>8,} bytes  ({_kib(s):>10})  (_min_stack_size)")
    else:
        out("    Stack  :       -- not found in map --")

    # --- Interrupts ---
    out()
    out("  Interrupt Handlers")
    core_str = ", ".join(core_irqs) if core_irqs else "none"
    out(f"    Core IRQs        ({len(core_irqs):2d}): {core_str}")

    out(f"    Peripheral IRQs  ({len(periph_irqs):2d}):")
    if periph_irqs:
        for name in periph_irqs:
            out(f"      - {name}")
    else:
        out("      none")

    # --- Heap usage note ---
    if map_info.get("heap"):
        out()
        out(f"  Note: Heap is active ({_kib(map_info['heap'])}).")
        out("        Used by: musl malloc (XC32 libc), TCPIP internal heap.")
        out("        Runtime heap consumption is not measurable at link time.")

    out()
    out(SEP)
    out()
    return buf.getvalue()


# ---------------------------------------------------------------------------
# 6. Image output  (image/<project>_<timestamp>.hex + build_summary_<ts>.txt)
# ---------------------------------------------------------------------------

def write_image(elf_path: str, summary_text: str, ts_tag: str) -> None:
    """
    Creates <out_dir>/image/ and writes:
      - tcpip_iperf_lan865x_<ts_tag>.hex  (copy of default.hex)
      - build_summary_<ts_tag>.txt        (summary text)
    """
    out_dir   = os.path.dirname(os.path.abspath(elf_path))
    image_dir = os.path.join(out_dir, "image")
    os.makedirs(image_dir, exist_ok=True)

    tag         = ts_tag if ts_tag else "unknown"
    hex_dst     = os.path.join(image_dir, f"T1S_100BaseT_Bridge_{tag}.hex")
    summary_dst = os.path.join(image_dir, f"build_summary_{tag}.txt")

    hex_src = os.path.join(out_dir, "default.hex")
    if os.path.isfile(hex_src):
        shutil.copy2(hex_src, hex_dst)
        print(f"  Image HEX  : {hex_dst}")
    else:
        print(f"  WARNING: HEX not found at {hex_src}, skipping copy.")

    with open(summary_dst, "w", encoding="utf-8") as f:
        f.write(summary_text)
    print(f"  Summary    : {summary_dst}")
    print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) != 4:
        print("Usage: python build_summary.py <BUILD_DIR> <ELF_PATH> <XC32_BIN_DIR>")
        sys.exit(1)

    build_dir = sys.argv[1]
    elf_path  = sys.argv[2]
    xc32_bin  = sys.argv[3]

    mem                    = read_memory_xml(build_dir)
    map_info               = read_map_file(build_dir)
    core_irqs, periph_irqs = read_active_interrupts(elf_path, xc32_bin)
    build_ts, ts_tag       = read_build_timestamp(elf_path)

    summary_text = print_summary(mem, map_info, core_irqs, periph_irqs, build_ts)
    write_image(elf_path, summary_text, ts_tag)


if __name__ == "__main__":
    main()
