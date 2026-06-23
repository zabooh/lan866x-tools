#!/usr/bin/env python3
"""
lan8651_causality_test.py
--------------------------
Ausführlicher Register-Kausalitäts- und Integritätstest für den LAN8651/LAN865x.

ZIEL
----
Sicherstellen, dass:
  1. Alle bekannten Register zuverlässig lesbar sind (kein Callback-Timeout).
  2. Nach der Initialisierung die richtigen Werte in den Registern stehen.
  3. Ein Schreiben TATSÄCHLICH im Register ankommt (write→read→verify = Kausalität).
  4. Das zuletzt geschriebene Muster gültig ist (Write-B überschreibt Write-A).
  5. Das Schreiben eines Registers das Nachbarregister NICHT korrumpiert (Isolation).
  6. Mehrere Register gleichzeitig eindeutige Werte halten (Adressuniqueness).
  7. Wiederholtes Lesen konsistente Werte liefert (kein Bit-Rauschen).
  8. Extreme Bitmuster (0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555) korrekt gespeichert werden.
  9. IMASK0 roundtrip (Interrupt-Maske kurz schreiben/lesen/restore).
 10. PLCA-Status-Register lesbar ist.

AUFWAND
-------
Jede lan_read / lan_write Transaktion dauert bei laufender PTP-State-Machine
ca. 4–6 Sekunden (Callback-Pipeline-Latenz). Die Gesamtlaufzeit ist daher
abhängig von der Anzahl aktiver Test-Gruppen und beträgt typisch 5–15 Minuten.

VERWENDUNG
----------
    python lan8651_causality_test.py                  # alle Tests, COM10
    python lan8651_causality_test.py --port COM10
    python lan8651_causality_test.py --port COM10 --skip T03  # T03 überspringen
    python lan8651_causality_test.py --port COM10 --only T01,T02
    python lan8651_causality_test.py --port COM10 --no-restore  # Werte NICHT zurückschreiben
    python lan8651_causality_test.py --port COM10 --timeout 10  # 10s pro Transaktion

WICHTIG
-------
Alle Schreibzugriffe werden am Testende wiederhergestellt (--no-restore unterdrückt das).
Der Test schreibt NICHT in TXMCTL, um die laufende PTP-Sync-Sequenz nicht zu stören.
"""

import serial
import time
import re
import sys
import argparse
import json
from datetime import datetime

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
BAUDRATE          = 115200
SERIAL_TIMEOUT    = 3
DEFAULT_PORT      = "COM10"
DEFAULT_TX_TIMEOUT = 8.0       # Sekunden pro Transaktion (4.5s + Puffer)
RESP_TIMEOUT_FAST = 8.0        # normaler Wert
PROMPT_MARKERS    = ["\r\n> ", "\n> ", "> "]

# ---------------------------------------------------------------------------
# Register-Map  (MMS-Adressraum: 0xMMMMAAAA)
# MMS_0  = 0x0000xxxx  OA-Registers
# MMS_1  = 0x0001xxxx  GEM-MAC-Registers
# MMS_4  = 0x0004xxxx  T1S-Registers (Match / PLCA)
# MMS_10 = 0x000CAxx   PLCA-Registers (via clause 45 MMS4)
# ---------------------------------------------------------------------------
REG = {
    # ---- MMS_0 : Open Alliance SPI Registers (OA = TC6) ----
    "IDVER"         : (0x00000000, "ID/Version (RO)", False),
    "PHYID"         : (0x00000001, "PHY Identifier (RO)", False),
    "CAPABILITY"    : (0x00000002, "Capabilities (RO)", False),
    "RESET"         : (0x00000003, "Reset register", False),  # W1 = SW reset → skip write
    "CONFIG0"       : (0x00000004, "OA Configuration 0 (FTSE/FTSS/etc.)", False),
    "STATUS0"       : (0x00000008, "OA Status 0 (TTSCAA etc.) – self-clearing bits!", False),
    "STATUS1"       : (0x00000009, "OA Status 1", False),
    "IMASK0"        : (0x0000000C, "OA Interrupt Mask 0", True),   # writable
    "IMASK1"        : (0x0000000D, "OA Interrupt Mask 1", True),   # writable
    "TTSCAH"        : (0x00000010, "TX Timestamp Capture A High (RO)", False),
    "TTSCAL"        : (0x00000011, "TX Timestamp Capture A Low (RO)", False),
    "TTSCBH"        : (0x00000012, "TX Timestamp Capture B High (RO)", False),
    "TTSCBL"        : (0x00000013, "TX Timestamp Capture B Low (RO)", False),
    "TTSCCH"        : (0x00000014, "TX Timestamp Capture C High (RO)", False),
    "TTSCCL"        : (0x00000015, "TX Timestamp Capture C Low (RO)", False),

    # ---- MMS_1 : GEM MAC Registers ----
    "NETWORK_CTRL"  : (0x00010000, "GEM Network Control (TXEN/RXEN)", False),
    "NETWORK_CFG"   : (0x00010001, "GEM Network Config", False),
    "NETWORK_STATUS": (0x00010008, "GEM Network Status (RO)", False),
    "INT_STATUS"    : (0x0001001C, "GEM Interrupt Status (self-clearing!)", False),
    "INT_ENABLE"    : (0x0001001D, "GEM Interrupt Enable", False),
    "PHY_MGMT"      : (0x00010022, "PHY Management", False),
    "MAC_TSH"       : (0x00010070, "MAC Timestamp Seconds High", False),
    "MAC_TSL"       : (0x00010074, "MAC Timestamp Seconds Low", False),
    "MAC_TN"        : (0x00010075, "MAC Timestamp Nanoseconds", False),
    "MAC_TI"        : (0x00010077, "MAC Timestamp Increment (ns-per-cycle)", True),   # writable

    # ---- MMS_4 : T1S Registers (TX/RX Match, PLCA, PPS) ----
    "TXMCTL"        : (0x00040040, "TX Match Control (bit0=TXME)", False),  # managed by PTP SM
    "TXMPATH"       : (0x00040041, "TX Match Pattern High byte", True),
    "TXMPATL"       : (0x00040042, "TX Match Pattern Low  bytes", True),
    "TXMMSKH"       : (0x00040043, "TX Match Mask High", True),
    "TXMMSKL"       : (0x00040044, "TX Match Mask Low", True),
    "TXMLOC"        : (0x00040045, "TX Match Location (frame byte-offset)", True),
    "RXMCTL"        : (0x00040050, "RX Match Control", True),
    "RXMPATH"       : (0x00040051, "RX Match Pattern High", True),
    "RXMPATL"       : (0x00040052, "RX Match Pattern Low", True),
    "RXMMSKH"       : (0x00040053, "RX Match Mask High", True),
    "RXMMSKL"       : (0x00040054, "RX Match Mask Low", True),
    "RXMLOC"        : (0x00040055, "RX Match Location (frame byte-offset)", True),
    "PPSCTL"        : (0x00040090, "PPS Control", True),

    # ---- MMS_4 : PLCA Registers (via extended clause 45) ----
    "PLCA_CTRL0"    : (0x0004CA01, "PLCA Control 0 (EN/RST)", False),
    "PLCA_CTRL1"    : (0x0004CA02, "PLCA Control 1 (nodeId/nodeCount)", False),
    "PLCA_STATUS"   : (0x0004CA03, "PLCA Status (PST = PLCA running)", False),
    "PLCA_TOTMR"    : (0x0004CA04, "PLCA TO Timer", False),
    "PLCA_BURST"    : (0x0004CA05, "PLCA Burst Control", False),
}

# Erwarteter Init-Zustand nach PTP_GM_Init() — aus ptp_gm_task.c gm_init_vals[]
# PTP_GM_PTP_ETHERTYPE = 0x88F7
_PTP_ETH = 0x88F7
EXPECTED_INIT = {
    "TXMCTL"    : None,          # zyklisch 0x0000/0x0002 — nur Sanity-Prüfung
    "TXMLOC"    : 12,            # Byte-Offset 12 = EtherType-Feld im Frame
    "TXMPATH"   : (_PTP_ETH >> 8) & 0xFF,              # 0x88
    "TXMPATL"   : ((_PTP_ETH & 0xFF) << 8) | 0x10,    # 0xF710
    "TXMMSKH"   : 0x00,
    "TXMMSKL"   : 0x00,
    "MAC_TI"    : 40,            # ns increment per 25MHz clock tick
    "CONFIG0"   : None,          # nur Bit-Check: FTSE (bit7) und FTSS (bit6)
    "IMASK0"    : 0x00000000,    # alle Interrupts unmaskiert
    "NETWORK_CTRL": None,        # nur Bit-Check: TXEN (bit2) + RXEN (bit3)
    "PLCA_CTRL1": None,          # nodeCount=8 (bits[15:8]), nodeId=1 (bits[7:0])
}

# Testwerte für den Roundtrip / Kausalitätstest
# Jedes Register bekommt eine Liste von Testmustern
TEST_PATTERNS = {
    "TXMLOC"  : [0x1A, 0x2B, 0x3C, 0x00, 0xFF],       # 8-bit Byte-Offset
    "TXMPATH" : [0x11, 0x22, 0x55, 0xAA, 0x00, 0xFF],  # 8-bit EtherType high byte
    "TXMPATL" : [0x1111, 0x2222, 0x5555, 0xAAAA,
                 0x0000, 0xFFFF],                        # 16-bit EtherType/TSC field
    "TXMMSKH" : [0xDEADBEEF, 0x55555555, 0xAAAAAAAA,
                 0x00000000, 0xFFFFFFFF],                # 32-bit mask
    "TXMMSKL" : [0x12345678, 0x5A5A5A5A, 0xA5A5A5A5,
                 0x00000000, 0xFFFFFFFF],                # 32-bit mask
    "RXMLOC"  : [0x04, 0x08, 0x0C, 0x00, 0xFF],        # 8-bit
    "RXMMSKH" : [0xCAFEBABE, 0x0F0F0F0F, 0xF0F0F0F0,
                 0x00000000, 0xFFFFFFFF],
    "RXMMSKL" : [0xFEEDFACE, 0x01020304, 0xA0B0C0D0,
                 0x00000000, 0xFFFFFFFF],
    "IMASK0"  : [0xFFFFFFFF, 0x00000100, 0x00000000],  # kurzer Roundtrip
}

# Registersatz für den Adressuniqueness-Test
# Gleich­zeitig einzigartige Werte schreiben, dann alle zurücklesen
UNIQUENESS_REGS = [
    ("TXMLOC",  0x00000011),
    ("TXMPATH", 0x00000022),
    ("TXMPATL", 0x00003344),
    ("TXMMSKH", 0x55660000),
    ("TXMMSKL", 0x77880000),
    ("RXMLOC",  0x00000099),
    ("RXMMSKH", 0xAABBCC00),
    ("RXMMSKL", 0x00DDEEFF),
]

# ---------------------------------------------------------------------------
# Globale Zähler
# ---------------------------------------------------------------------------
_pass = 0
_fail = 0
_warn = 0
_skip = 0

_results_log = []   # Liste von (group, label, status, detail)

# ---------------------------------------------------------------------------
# Serielle Hilfsfunktionen
# ---------------------------------------------------------------------------
def open_port(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUDRATE, timeout=SERIAL_TIMEOUT)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"[SER] {port} @ {BAUDRATE} baud geöffnet.")
    return ser


def wake_port(ser: serial.Serial) -> None:
    ser.write(b"\r\n")
    time.sleep(0.5)
    ser.reset_input_buffer()
    print("[SER] Prompt bereit.")


def _is_response_complete(response: str, cmd_lower: str) -> bool:
    if len(response) < 10:
        return False
    if "lan_read" in cmd_lower:
        m = re.search(r'LAN865X Read OK: Addr=0x[0-9A-Fa-f]+ Value=0x[0-9A-Fa-f]+',
                      response, re.IGNORECASE)
        if m:
            tail = response[m.end():]
            return '\n' in tail or '\r' in tail
        return False
    if "lan_write" in cmd_lower:
        m = re.search(r'LAN865X Write (?:OK|failed|timeout)',
                      response, re.IGNORECASE)
        if m:
            tail = response[m.end():]
            return '\n' in tail or '\r' in tail
        return False
    # Fallback: traditionelle Prompt-Erkennung
    return any(response.endswith(p) for p in PROMPT_MARKERS)


def send_cmd(ser: serial.Serial, cmd: str,
             timeout: float = RESP_TIMEOUT_FAST) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    response = ""
    deadline = time.time() + timeout
    cmd_lower = cmd.lower()
    while time.time() < deadline:
        available = ser.in_waiting
        if available > 0:
            chunk = ser.read(available).decode("utf-8", errors="ignore")
            response += chunk
            if _is_response_complete(response, cmd_lower):
                break
        time.sleep(0.0005)  # 0.5ms Polling
    return response


# ---------------------------------------------------------------------------
# Register-Zugriff
# ---------------------------------------------------------------------------
def lan_read(ser: serial.Serial, addr: int,
             timeout: float = RESP_TIMEOUT_FAST) -> "tuple[int | None, float]":
    addr_str = f"0x{addr:08X}"
    t0 = time.monotonic()
    resp = send_cmd(ser, f"lan_read {addr_str}", timeout=timeout)
    elapsed_ms = (time.monotonic() - t0) * 1000.0
    text = resp

    # Primäres Format: "LAN865X Read OK: Addr=0x... Value=0x..."
    m = re.search(r'LAN865X Read OK: Addr=0x[0-9A-Fa-f]+\s+Value=(0x[0-9A-Fa-f]+)',
                  text, re.IGNORECASE)
    if m:
        return int(m.group(1), 16), elapsed_ms

    # Fallback: erstes "Value=0x..."
    m2 = re.search(r'Value=(0x[0-9A-Fa-f]+)', text, re.IGNORECASE)
    if m2:
        return int(m2.group(1), 16), elapsed_ms

    # Timeout-Meldung in der Antwort?
    if re.search(r'cb\s*timeout|callback\s*timeout', text, re.IGNORECASE):
        print(f"    !! Callback-Timeout bei lan_read 0x{addr:08X}")

    return None, elapsed_ms


def lan_write(ser: serial.Serial, addr: int, value: int,
              timeout: float = RESP_TIMEOUT_FAST) -> "tuple[bool, float, str]":
    t0 = time.monotonic()
    resp = send_cmd(ser, f"lan_write 0x{addr:08X} 0x{value:08X}", timeout=timeout)
    elapsed_ms = (time.monotonic() - t0) * 1000.0
    failed = bool(re.search(r'cb\s*timeout|callback\s*timeout|error', resp, re.IGNORECASE))
    return not failed, elapsed_ms, resp


# ---------------------------------------------------------------------------
# Test-Framework
# ---------------------------------------------------------------------------
def _log(group: str, label: str, status: str, detail: str, ms: float) -> None:
    _results_log.append({
        "group":  group,
        "label":  label,
        "status": status,
        "detail": detail,
        "ms":     round(ms, 1),
    })


def check(group: str, label: str, passed: bool,
          detail: str = "", ms: float = 0.0) -> bool:
    global _pass, _fail
    status = "PASS" if passed else "FAIL"
    if passed:
        _pass += 1
    else:
        _fail += 1
    ms_str = f"  [{ms:.0f}ms]" if ms else ""
    marker = "✓" if passed else "✗"
    print(f"  [{status}] {marker} {label}{ms_str}"
          + (f"  — {detail}" if detail else ""))
    _log(group, label, status, detail, ms)
    return passed


def warn(group: str, label: str, detail: str = "", ms: float = 0.0) -> None:
    global _warn
    _warn += 1
    ms_str = f"  [{ms:.0f}ms]" if ms else ""
    print(f"  [WARN] ⚠ {label}{ms_str}" + (f"  — {detail}" if detail else ""))
    _log(group, label, "WARN", detail, ms)


def skip(group: str, label: str) -> None:
    global _skip
    _skip += 1
    print(f"  [SKIP] {label}")
    _log(group, label, "SKIP", "", 0)


def section(code: str, title: str) -> None:
    bar = "═" * 62
    print(f"\n{bar}")
    print(f"  {code}  {title}")
    print(bar)


def subsection(title: str) -> None:
    print(f"\n  ── {title}")


# ---------------------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------------------
def _read_checked(ser: serial.Serial, group: str, name: str,
                  addr: int, tx_to: float) -> "int | None":
    """Liest ein Register, loggt PASS/FAIL für die Lesbarkeit, gibt Wert zurück."""
    val, ms = lan_read(ser, addr, timeout=tx_to)
    if val is None:
        check(group, f"{name} (0x{addr:08X}) lesbar", False,
              "kein Wert (Callback-Timeout?)", ms)
    else:
        check(group, f"{name} (0x{addr:08X}) lesbar", True,
              f"= 0x{val:08X}", ms)
        if ms > 6000:
            warn(group, f"{name} Roundtrip kritisch langsam", f"{ms:.0f} ms")
        elif ms > 3000:
            warn(group, f"{name} Roundtrip langsam", f"{ms:.0f} ms")
    return val


def _write_and_restore(ser: serial.Serial, group: str, name: str,
                       addr: int, test_val: int,
                       original_val: "int | None",
                       tx_to: float,
                       label_suffix: str = "") -> "tuple[bool, int | None]":
    """
    Schreibt test_val ins Register, liest zurück, prüft Übereinstimmung.
    Gibt (passed, readback_val) zurück. Restore wird separat gemacht.
    """
    lbl = f"{name}←0x{test_val:08X}{label_suffix}"

    ok_w, ms_w, _ = lan_write(ser, addr, test_val, timeout=tx_to)
    if not ok_w:
        check(group, f"{lbl} schreiben", False, "Callback-Timeout", ms_w)
        return False, None

    readback, ms_r = lan_read(ser, addr, timeout=tx_to)
    total_ms = ms_w + ms_r

    if readback is None:
        check(group, f"{lbl} readback", False,
              "kein Wert nach Schreiben", total_ms)
        return False, None

    causal = (readback == test_val)
    check(group, f"{lbl} kausal",
          causal,
          f"erwartet=0x{test_val:08X}  gelesen=0x{readback:08X}",
          total_ms)
    return causal, readback


def _restore_register(ser: serial.Serial, group: str, name: str,
                      addr: int, original: int, tx_to: float) -> None:
    ok_w, ms_w, _ = lan_write(ser, addr, original, timeout=tx_to)
    if not ok_w:
        warn(group, f"{name} restore fehlgeschlagen", f"0x{original:08X}", ms_w)
        return
    val, ms_r = lan_read(ser, addr, timeout=tx_to)
    if val is None or val != original:
        warn(group, f"{name} nach Restore falsch",
             f"erwartet=0x{original:08X} gelesen=0x{(val or 0):08X}",
             ms_w + ms_r)
    else:
        print(f"    [OK] {name} wiederhergestellt → 0x{original:08X}  "
              f"[{ms_w + ms_r:.0f}ms]")


# ---------------------------------------------------------------------------
# T01 — Allgemeiner Lesebarkeitscheck (alle bekannten Register)
# ---------------------------------------------------------------------------
def test_T01_read_all(ser: serial.Serial, tx_to: float) -> None:
    section("T01", "Lesebarkeitscheck aller bekannten Register")
    print("  Jedes Register im REG-Dictionary wird einmal gelesen.")
    print("  Erwartung: kein Register gibt None zurück (kein Callback-Timeout).\n")
    for name, (addr, desc, _writable) in REG.items():
        val, ms = lan_read(ser, addr, timeout=tx_to)
        ok = val is not None
        detail = f"= 0x{val:08X}" if ok else "Kein Wert (Callback-Timeout?)"
        check("T01", f"{name:12s} (0x{addr:08X})  {desc[:38]}", ok, detail, ms)
        if ms > 6000:
            warn("T01", f"{name} Roundtrip sehr langsam", f"{ms:.0f} ms")


# ---------------------------------------------------------------------------
# T02 — Initialisierungswerte / Sanity Check
# ---------------------------------------------------------------------------
def test_T02_init_state(ser: serial.Serial, tx_to: float) -> None:
    section("T02", "Initialisierungswerte — Sanity Check")
    grp = "T02"

    # CONFIG0: FTSE (bit7) + FTSS (bit6) müssen gesetzt sein
    subsection("CONFIG0 — FTSE/FTSS Bits")
    v = _read_checked(ser, grp, "CONFIG0", REG["CONFIG0"][0], tx_to)
    if v is not None:
        check(grp, "CONFIG0: FTSE-Bit (bit7) gesetzt",   bool(v & 0x80),
              f"CONFIG0=0x{v:08X}, bit7={'1' if v & 0x80 else '0'}")
        check(grp, "CONFIG0: FTSS-Bit (bit6) gesetzt",   bool(v & 0x40),
              f"CONFIG0=0x{v:08X}, bit6={'1' if v & 0x40 else '0'}")

    # IMASK0: soll 0x00000000 sein
    subsection("IMASK0 — alle Interrupts unmaskiert")
    v = _read_checked(ser, grp, "IMASK0", REG["IMASK0"][0], tx_to)
    if v is not None:
        check(grp, "IMASK0 == 0x00000000 (TTSCAA unmaskiert)",
              v == 0x00000000,
              f"ist 0x{v:08X}"
              + (f" — TTSCAA (bit8) maskiert!" if v & 0x100 else ""))
        if v != 0:
            masked = [i for i in range(32) if (v >> i) & 1]
            warn(grp, "Maskierte Interrupt-Bits", f"Bits: {masked}")

    # NETWORK_CTRL: TXEN (bit2) + RXEN (bit3)
    subsection("NETWORK_CTRL — TXEN + RXEN")
    v = _read_checked(ser, grp, "NETWORK_CTRL", REG["NETWORK_CTRL"][0], tx_to)
    if v is not None:
        check(grp, "NETWORK_CTRL: TXEN (bit2) gesetzt", bool(v & 0x04),
              f"0x{v:08X}")
        check(grp, "NETWORK_CTRL: RXEN (bit3) gesetzt", bool(v & 0x08),
              f"0x{v:08X}")
        b15 = (v >> 15) & 1
        print(f"    INFO  NETWORK_CTRL Bit15 (STORE_TX_TS?): "
              f"{'GESETZT' if b15 else 'nicht gesetzt'}")

    # TXMLOC — muss 12 sein (nach Firmware-Init, Fix #2)
    subsection("TXMLOC — Byte-Offset 12")
    v = _read_checked(ser, grp, "TXMLOC", REG["TXMLOC"][0], tx_to)
    if v is not None:
        check(grp, f"TXMLOC == 12 (0x0C)",
              v == 12,
              f"ist 0x{v:08X} ({v})"
              + (" ← war vor Fix#2 = 30!" if v == 30 else ""))

    # TXMPATH — muss 0x88 sein (high byte von EtherType 0x88F7)
    subsection("TXMPATH — EtherType High Byte = 0x88")
    v = _read_checked(ser, grp, "TXMPATH", REG["TXMPATH"][0], tx_to)
    if v is not None:
        check(grp, f"TXMPATH == 0x88 (EtherType-High)",
              (v & 0xFF) == 0x88,
              f"ist 0x{v:08X}")

    # TXMPATL — muss 0xF710 sein
    subsection("TXMPATL — EtherType Low Byte + TSC field = 0xF710")
    v = _read_checked(ser, grp, "TXMPATL", REG["TXMPATL"][0], tx_to)
    if v is not None:
        check(grp, f"TXMPATL == 0xF710",
              (v & 0xFFFF) == 0xF710,
              f"ist 0x{v:08X}")

    # TXMMSKH/L — sollen 0 sein (kein Match-Mask = alles matcht)
    subsection("TXMMSKH / TXMMSKL — Maske = 0 (alle Bits match)")
    for name in ("TXMMSKH", "TXMMSKL"):
        v = _read_checked(ser, grp, name, REG[name][0], tx_to)
        if v is not None:
            check(grp, f"{name} == 0x00000000",
                  v == 0,
                  f"ist 0x{v:08X}")

    # TXMCTL — Sanity-Prüfung (darf nicht auf einem absurden Wert stehen)
    subsection("TXMCTL — Sanity (darf kein Kauderwelsch-Wert sein)")
    v = _read_checked(ser, grp, "TXMCTL", REG["TXMCTL"][0], tx_to)
    if v is not None:
        sane = v in (0x0000, 0x0002, 0x0082, 0x0006, 0x0086)
        check(grp, "TXMCTL hat sinnvollen Wert",
              sane,
              f"0x{v:08X}"
              + (" — TXME=1 (armiert)" if v & 0x0002 else " — TXME=0 (disarmiert)")
              + (" TXPMDET=1 (Match!)" if v & 0x0080 else ""))

    # MAC_TI — muss 40 sein (25MHz → 40ns increment)
    subsection("MAC_TI — ns-Inkrement = 40")
    v = _read_checked(ser, grp, "MAC_TI", REG["MAC_TI"][0], tx_to)
    if v is not None:
        check(grp, "MAC_TI == 40",
              v == 40,
              f"ist 0x{v:08X} ({v})")

    # PLCA_CTRL1 — nodeCount=8 (bits[15:8]) + nodeId (bits[7:0])
    subsection("PLCA_CTRL1 — nodeCount=8 / nodeId prüfbar")
    v = _read_checked(ser, grp, "PLCA_CTRL1", REG["PLCA_CTRL1"][0], tx_to)
    if v is not None:
        node_count = (v >> 8) & 0xFF
        node_id    = v & 0xFF
        check(grp, "PLCA nodeCount == 8",
              node_count == 8,
              f"nodeCount={node_count}, nodeId={node_id}")
        print(f"    INFO  PLCA nodeId={node_id} "
              f"({'GM=0' if node_id == 0 else ('Follower=1' if node_id == 1 else 'unbekannt')})")

    # PHYID — darf nicht 0 oder 0xFFFFFFFF sein
    subsection("PHYID — Identifikation des LAN8651")
    v = _read_checked(ser, grp, "PHYID", REG["PHYID"][0], tx_to)
    if v is not None:
        valid_id = v not in (0x00000000, 0xFFFFFFFF)
        check(grp, "PHYID != 0x00000000 und != 0xFFFFFFFF",
              valid_id,
              f"PHYID=0x{v:08X}")
        # LAN865x OUI = 0x00800F (Microchip)
        # LAN8651 model number = 0x162 (bits [15:4])
        oui = (v >> 10) & 0x3FFFFF       # vereinfachte Extraktion
        print(f"    INFO  PHYID=0x{v:08X} (OUI-Bits[31:10]=0x{oui:05X})")


# ---------------------------------------------------------------------------
# T03 — Roundtrip-Schreibtest: Muster-Suite für jedes writable Register
# ---------------------------------------------------------------------------
def test_T03_roundtrip(ser: serial.Serial, tx_to: float,
                       restore: bool) -> None:
    section("T03", "Roundtrip-Schreibtest — Bitmuster-Suite")
    grp = "T03"
    print("  Für jedes schreibbare Register: mehrere Testmuster schreiben,")
    print("  zurücklesen, Übereinstimmung prüfen. Original danach restore.\n")

    for reg_name, patterns in TEST_PATTERNS.items():
        if reg_name not in REG:
            print(f"  [SKIP] {reg_name} nicht in Register-Map — übersprungen")
            continue
        addr, desc, writable = REG[reg_name]
        if not writable:
            skip(grp, f"{reg_name} — nicht schreibbar (RO)")
            continue

        subsection(f"{reg_name} (0x{addr:08X}) — {desc}")

        # Original sichern
        original, ms_r = lan_read(ser, addr, timeout=tx_to)
        if original is None:
            check(grp, f"{reg_name} initial lesbar", False,
                  "Kein Wert → Register übersprungen", ms_r)
            continue
        print(f"    Ausgangswert: 0x{original:08X}  [{ms_r:.0f}ms]")

        pass_count = 0
        fail_count = 0
        for pat in patterns:
            ok_write, ms_w, _ = lan_write(ser, addr, pat, timeout=tx_to)
            if not ok_write:
                check(grp, f"  {reg_name}←0x{pat:08X} schreiben",
                      False, "Callback-Timeout", ms_w)
                fail_count += 1
                continue

            readback, ms_rb = lan_read(ser, addr, timeout=tx_to)
            total_ms = ms_w + ms_rb
            if readback is None:
                check(grp, f"  {reg_name}←0x{pat:08X} readback",
                      False, "Kein Wert nach Schreiben", total_ms)
                fail_count += 1
                continue

            causal = (readback == pat)
            check(grp,
                  f"  {reg_name}←0x{pat:08X}  →  0x{readback:08X}",
                  causal,
                  ("OK" if causal
                   else f"ERWARTET 0x{pat:08X} GELESEN 0x{readback:08X}"),
                  total_ms)
            if causal:
                pass_count += 1
            else:
                fail_count += 1

        # Restore
        if restore and original is not None:
            _restore_register(ser, grp, reg_name, addr, original, tx_to)
        else:
            warn(grp, f"{reg_name} NICHT wiederhergestellt", "--no-restore aktiv")

        print(f"    → {reg_name}: {pass_count}/{pass_count+fail_count} Muster bestanden")


# ---------------------------------------------------------------------------
# T04 — Sequentielle Kausalität: Write-B überschreibt Write-A
# ---------------------------------------------------------------------------
def test_T04_sequential(ser: serial.Serial, tx_to: float, restore: bool) -> None:
    section("T04", "Sequentielle Kausalität — letzter Schreibwert gewinnt")
    grp = "T04"
    print("  Schreibt Wert A, schnell gefolgt von Wert B in dasselbe Register.")
    print("  Readback MUSS B zurückliefern (Regression wäre: A bleibt stehen).\n")

    sequences = [
        ("TXMLOC",   0x1A,  0x2B),
        ("TXMLOC",   0xFF,  0x00),
        ("TXMPATH",  0xAA,  0x55),
        ("TXMMSKH",  0xDEAD0000, 0x5A5A5A5A),
        ("TXMMSKL",  0x12345678, 0xA5A5A5A5),
    ]

    original_vals: dict[str, int | None] = {}

    for reg_name, val_a, val_b in sequences:
        addr = REG[reg_name][0]

        # Original speichern (nur einmalig pro Register)
        if reg_name not in original_vals:
            orig, ms = lan_read(ser, addr, timeout=tx_to)
            original_vals[reg_name] = orig
            if orig is None:
                print(f"  [SKIP] {reg_name} initial nicht lesbar — übersprungen")
                continue

        subsection(f"{reg_name}: A=0x{val_a:08X} → B=0x{val_b:08X} → read → B?")

        # Schreibe A (ohne Readback — nur um A zu setzen)
        ok_a, ms_a, _ = lan_write(ser, addr, val_a, timeout=tx_to)
        if not ok_a:
            check(grp, f"{reg_name} Write-A (0x{val_a:08X})",
                  False, "Callback-Timeout", ms_a)
            continue

        # Schreibe B (sofort danach)
        ok_b, ms_b, _ = lan_write(ser, addr, val_b, timeout=tx_to)
        if not ok_b:
            check(grp, f"{reg_name} Write-B (0x{val_b:08X})",
                  False, "Callback-Timeout", ms_b)
            continue

        # Readback — muss B sein
        rb, ms_r = lan_read(ser, addr, timeout=tx_to)
        total_ms = ms_a + ms_b + ms_r
        if rb is None:
            check(grp, f"{reg_name} Readback == B",
                  False, "Kein Wert", total_ms)
        else:
            check(grp,
                  f"{reg_name}: Readback=0x{rb:08X} == B=0x{val_b:08X}",
                  rb == val_b,
                  ("OK" if rb == val_b
                   else f"IST 0x{rb:08X}, ERWARTET B=0x{val_b:08X}"
                        f"{' (gleich A!)' if rb == val_a else ''}"),
                  total_ms)

    # Restore
    for reg_name, orig in original_vals.items():
        if orig is not None and restore:
            addr = REG[reg_name][0]
            _restore_register(ser, grp, reg_name, addr, orig, tx_to)


# ---------------------------------------------------------------------------
# T05 — Register-Isolation: Schreiben in Y korrumpiert X nicht
# ---------------------------------------------------------------------------
def test_T05_isolation(ser: serial.Serial, tx_to: float, restore: bool) -> None:
    section("T05", "Register-Isolation — Schreiben in Y korrumpiert X nicht")
    grp = "T05"
    print("  Schreibt Testwert in Register X, dann Testwert in Y (≠X),")
    print("  liest X → X muss noch denselben Wert enthalten.\n")

    _pairs = [
        # (X,              val_X,       Y,              val_Y)
        ("TXMMSKH", 0xAA00AA00, "TXMMSKL", 0x00BB00BB),
        ("TXMPATH", 0x00000033, "TXMPATL", 0x00005566),
        ("TXMLOC",  0x0000001A, "RXMLOC",  0x0000002B),
        ("RXMMSKH", 0xCC00CC00, "RXMMSKL", 0x00DD00DD),
    ]

    originals: dict[str, int | None] = {}

    for name_x, val_x, name_y, val_y in _pairs:
        addr_x = REG[name_x][0]
        addr_y = REG[name_y][0]

        # Originals sichern
        for n, a in [(name_x, addr_x), (name_y, addr_y)]:
            if n not in originals:
                v, _ = lan_read(ser, a, timeout=tx_to)
                originals[n] = v

        subsection(f"X={name_x} ← 0x{val_x:08X} / Y={name_y} ← 0x{val_y:08X}")

        # Schreibe X
        ok_x, ms_x, _ = lan_write(ser, addr_x, val_x, timeout=tx_to)
        if not ok_x:
            check(grp, f"{name_x} schreiben", False, "Timeout", ms_x)
            continue

        # Schreibe Y
        ok_y, ms_y, _ = lan_write(ser, addr_y, val_y, timeout=tx_to)
        if not ok_y:
            check(grp, f"{name_y} schreiben", False, "Timeout", ms_y)
            continue

        # Readback X → muss noch val_x sein
        rb_x, ms_rx = lan_read(ser, addr_x, timeout=tx_to)
        total_ms = ms_x + ms_y + ms_rx
        if rb_x is None:
            check(grp, f"{name_x} nach Write-Y lesbar", False,
                  "Kein Wert", total_ms)
        else:
            check(grp,
                  f"{name_x} nach Write-{name_y} unverändert",
                  rb_x == val_x,
                  f"X=0x{rb_x:08X}, erwartet=0x{val_x:08X}"
                  + (" ← KORRUMPIERT!" if rb_x != val_x else ""),
                  total_ms)

    # Restore
    for reg_name, orig in originals.items():
        if orig is not None and restore:
            addr = REG[reg_name][0]
            _restore_register(ser, grp, reg_name, addr, orig, tx_to)


# ---------------------------------------------------------------------------
# T06 — Adressuniqueness: Alle Register halten gleichzeitig eindeutige Werte
# ---------------------------------------------------------------------------
def test_T06_uniqueness(ser: serial.Serial, tx_to: float, restore: bool) -> None:
    section("T06", "Adressuniqueness — Register halten gleichzeitig eindeutige Werte")
    grp = "T06"
    print("  Schreibt in jedes Testreg einen anderen eindeutigen Wert,")
    print("  liest dann alle zurück — jedes muss SEINEN Wert zurückliefern.\n")

    # Originals sichern
    originals: dict[str, int | None] = {}
    for reg_name, _ in UNIQUENESS_REGS:
        addr = REG[reg_name][0]
        v, _ = lan_read(ser, addr, timeout=tx_to)
        originals[reg_name] = v
        print(f"    Backup {reg_name:10s} (0x{addr:08X}) = "
              + (f"0x{v:08X}" if v is not None else "None"))

    # Alle mit eindeutigen Werten beschreiben
    print()
    written: dict[str, int] = {}
    for reg_name, test_val in UNIQUENESS_REGS:
        addr = REG[reg_name][0]
        ok_w, ms_w, _ = lan_write(ser, addr, test_val, timeout=tx_to)
        if ok_w:
            written[reg_name] = test_val
            print(f"    WRITE {reg_name:10s} ← 0x{test_val:08X}  [{ms_w:.0f}ms]")
        else:
            check(grp, f"{reg_name} schreiben", False, "Timeout", ms_w)

    # Alle zurücklesen und prüfen
    print()
    for reg_name, test_val in UNIQUENESS_REGS:
        if reg_name not in written:
            skip(grp, f"{reg_name} nicht beschrieben — übersprungen")
            continue
        addr = REG[reg_name][0]
        rb, ms_r = lan_read(ser, addr, timeout=tx_to)
        if rb is None:
            check(grp, f"{reg_name} Readback", False, "Kein Wert", ms_r)
            continue
        expected = written[reg_name]
        # Sonderfall: einige Register sind schmaler als 32 bit (z.B. TXMLOC = 8bit)
        # → mask auf signifikante Bits
        mask = _get_register_mask(reg_name)
        rb_masked  = rb & mask
        exp_masked = expected & mask
        check(grp,
              f"{reg_name:10s} = 0x{rb_masked:08X} == 0x{exp_masked:08X}",
              rb_masked == exp_masked,
              ("OK" if rb_masked == exp_masked
               else f"GELESEN 0x{rb:08X}, ERWARTET 0x{exp_masked:08X}"
                    + _cross_contamination_hint(reg_name, rb, written)),
              ms_r)

    # Restore
    for reg_name, orig in originals.items():
        if orig is not None and restore:
            addr = REG[reg_name][0]
            _restore_register(ser, grp, reg_name, addr, orig, tx_to)


def _get_register_mask(name: str) -> int:
    """Signifikante Bitsbreite pro Register (konservativ)."""
    narrow_8  = ["TXMLOC", "RXMLOC", "TXMPATH", "PLCA_CTRL0"]
    narrow_16 = ["TXMPATL", "RXMPATL", "PLCA_CTRL1"]
    if name in narrow_8:
        return 0x000000FF
    if name in narrow_16:
        return 0x0000FFFF
    return 0xFFFFFFFF


def _cross_contamination_hint(name_x: str, rb: int,
                               written: dict[str, int]) -> str:
    """Gibt einen Hinweis, falls der gelesene Wert dem eines anderen Registers entspricht."""
    for other_name, other_val in written.items():
        if other_name == name_x:
            continue
        mask = _get_register_mask(name_x)
        if (rb & mask) == (other_val & mask):
            return f" ← ALIASING? Stimmt mit {other_name}=0x{other_val:08X} überein!"
    return ""


# ---------------------------------------------------------------------------
# T07 — Konsistenz: Wiederholtes Lesen desselben Registers
# ---------------------------------------------------------------------------
def test_T07_consistency(ser: serial.Serial, tx_to: float) -> None:
    section("T07", "Konsistenz — Wiederholtes Lesen liefert gleiche Werte")
    grp = "T07"
    print("  Wählt stabile (nicht selbstlöschende) Register und liest 5× hintereinander.")
    print("  Alle Werte müssen übereinstimmen.\n")

    stable_regs = [
        "TXMLOC", "TXMPATH", "TXMPATL", "TXMMSKH", "TXMMSKL",
        "IMASK0", "NETWORK_CTRL", "MAC_TI",
    ]

    for reg_name in stable_regs:
        addr = REG[reg_name][0]
        subsection(f"{reg_name} (0x{addr:08X}) — 5× lesen")
        vals = []
        times = []
        for _ in range(5):
            v, ms = lan_read(ser, addr, timeout=tx_to)
            vals.append(v)
            times.append(ms)

        valid = [v for v in vals if v is not None]
        none_count = vals.count(None)
        check(grp, f"{reg_name}: alle 5 Reads erfolgreich",
              none_count == 0,
              f"{5 - none_count}/5 gültig{' — ' + str(none_count) + ' Timeouts' if none_count else ''}")

        if len(valid) > 1:
            unique_vals = list(set(valid))
            consistent = (len(unique_vals) == 1)
            check(grp, f"{reg_name}: Alle Werte identisch",
                  consistent,
                  (f"0x{unique_vals[0]:08X}" if consistent
                   else f"Verschiedene Werte: "
                        + ", ".join(f"0x{x:08X}" for x in unique_vals)))

        avg_ms = sum(times) / len(times) if times else 0
        max_ms = max(times) if times else 0
        print(f"    Ø Latenz: {avg_ms:.0f} ms  Max: {max_ms:.0f} ms")
        if max_ms > 6000:
            warn(grp, f"{reg_name} max Latenz sehr hoch", f"{max_ms:.0f} ms")


# ---------------------------------------------------------------------------
# T08 — Extreme Bitmuster: 0x00, 0xFF, 0xAA, 0x55
# ---------------------------------------------------------------------------
def test_T08_extreme_patterns(ser: serial.Serial, tx_to: float,
                               restore: bool) -> None:
    section("T08", "Extreme Bitmuster — 0x00000000 / 0xFFFFFFFF / 0xAAAAAAAA / 0x55555555")
    grp = "T08"
    print("  Prüft ob 32-Bit Register extreme Werte korrekt speichern.")
    print("  Wichtig: 0xFFFFFFFF und 0x00000000 dürfen nicht auf Hardware-Ebene")
    print("  abgeschnitten oder gespiegelt werden.\n")

    test_regs = [
        ("TXMMSKH", 0xFFFFFFFF),
        ("TXMMSKL", 0xFFFFFFFF),
        ("TXMMSKH", 0xAAAAAAAA),
        ("TXMMSKL", 0x55555555),
        ("TXMMSKH", 0x00000000),
        ("TXMMSKL", 0x00000000),
    ]

    originals: dict[str, int | None] = {}
    for reg_name, _ in test_regs:
        if reg_name not in originals:
            addr = REG[reg_name][0]
            v, _ = lan_read(ser, addr, timeout=tx_to)
            originals[reg_name] = v

    for reg_name, pat in test_regs:
        addr = REG[reg_name][0]
        _write_and_restore(ser, grp, reg_name, addr, pat, None, tx_to,
                           label_suffix="")

    for reg_name, orig in originals.items():
        if orig is not None and restore:
            addr = REG[reg_name][0]
            _restore_register(ser, grp, reg_name, addr, orig, tx_to)


# ---------------------------------------------------------------------------
# T09 — IMASK0 Roundtrip (kurzer Eingriff)
# ---------------------------------------------------------------------------
def test_T09_imask_roundtrip(ser: serial.Serial, tx_to: float,
                              restore: bool) -> None:
    section("T09", "IMASK0 Roundtrip — Interrupt-Maske kurz setzen/lesen/restore")
    grp = "T09"
    addr = REG["IMASK0"][0]

    print("  IMASK0 wird kurz auf 0xFFFFFFFF gesetzt (alle IRQs maskiert),")
    print("  zurückgelesen (muss 0xFFFFFFFF sein), dann auf 0x00000000 restored.")
    print("  Auswirkung: ggf. ein PTP-ISR fehlt während der ~4.5s Latenz.\n")

    # Sichern
    orig, ms_r0 = lan_read(ser, addr, timeout=tx_to)
    print(f"  IMASK0 Ausgangswert: "
          + (f"0x{orig:08X}" if orig is not None else "None")
          + f"  [{ms_r0:.0f}ms]")

    # Test 1: Schreibe 0xFFFFFFFF
    ok_w1, ms_w1, _ = lan_write(ser, addr, 0xFFFFFFFF, timeout=tx_to)
    if not ok_w1:
        check(grp, "IMASK0 ← 0xFFFFFFFF schreiben", False, "Timeout", ms_w1)
    else:
        rb1, ms_r1 = lan_read(ser, addr, timeout=tx_to)
        if rb1 is None:
            check(grp, "IMASK0 == 0xFFFFFFFF", False, "Kein Wert", ms_r1)
        else:
            check(grp, f"IMASK0 == 0xFFFFFFFF",
                  rb1 == 0xFFFFFFFF,
                  f"gelesen=0x{rb1:08X}", ms_w1 + ms_r1)

    # Test 2: Schreibe 0x00000100 (nur TTSCAA maskiert)
    ok_w2, ms_w2, _ = lan_write(ser, addr, 0x00000100, timeout=tx_to)
    if ok_w2:
        rb2, ms_r2 = lan_read(ser, addr, timeout=tx_to)
        if rb2 is not None:
            check(grp, "IMASK0 == 0x00000100 (nur TTSCAA)",
                  rb2 == 0x00000100,
                  f"gelesen=0x{rb2:08X}", ms_w2 + ms_r2)

    # Restore auf 0x00000000
    if restore or orig == 0x00000000:
        restore_val = orig if orig is not None else 0x00000000
        ok_rest, ms_rest, _ = lan_write(ser, addr, restore_val, timeout=tx_to)
        if ok_rest:
            rb_rest, ms_rr = lan_read(ser, addr, timeout=tx_to)
            check(grp, f"IMASK0 restore → 0x{restore_val:08X}",
                  rb_rest == restore_val if rb_rest is not None else False,
                  f"gelesen=0x{(rb_rest or 0):08X}", ms_rest + ms_rr)
        else:
            warn(grp, "IMASK0 restore fehlgeschlagen — KRITISCH!",
                 "Interrupts bleiben ggf. maskiert")


# ---------------------------------------------------------------------------
# T10 — PLCA Status und Identifikation
# ---------------------------------------------------------------------------
def test_T10_plca_status(ser: serial.Serial, tx_to: float) -> None:
    section("T10", "PLCA-Status und Chip-Identifikation")
    grp = "T10"

    # PLCA_STATUS (0x0004CA03): bit0 = PST (PLCA running)
    v = _read_checked(ser, grp, "PLCA_STATUS", REG["PLCA_STATUS"][0], tx_to)
    if v is not None:
        pst = v & 0x01
        check(grp, "PLCA_STATUS: PST-Bit (bit0) gesetzt (PLCA läuft)",
              bool(pst),
              f"PLCA_STATUS=0x{v:08X}, PST={'1 (PLCA aktiv)' if pst else '0 (PLCA INAKTIV!)'}")

    # PLCA_CTRL0 (0x0004CA01): bit15 = PLCAEN
    v = _read_checked(ser, grp, "PLCA_CTRL0", REG["PLCA_CTRL0"][0], tx_to)
    if v is not None:
        en = (v >> 15) & 1
        check(grp, "PLCA_CTRL0: PLCAEN (bit15) gesetzt",
              bool(en),
              f"0x{v:08X}, PLCAEN={en}")

    # PLCA_BURST (0x0004CA05)
    v = _read_checked(ser, grp, "PLCA_BURST", REG["PLCA_BURST"][0], tx_to)
    if v is not None:
        burst_count = v & 0xFF
        burst_timer = (v >> 8) & 0xFF
        print(f"    INFO  PLCA burstCount={burst_count}, burstTimer={burst_timer}")

    # IDVER — Chip-Version
    v = _read_checked(ser, grp, "IDVER", REG["IDVER"][0], tx_to)
    if v is not None:
        rev   = v & 0xF
        model = (v >> 4) & 0x3F
        manuf = (v >> 10) & 0x3F
        print(f"    INFO  IDVER=0x{v:08X}: manuf=0x{manuf:02X} model=0x{model:02X} rev={rev}")
        # LAN8651 model = 0x02 (8-bit model typically), check not 0/invalid
        check(grp, "IDVER != 0x00000000",
              v != 0,
              f"0x{v:08X}")


# ---------------------------------------------------------------------------
# Zusammenfassung und JSON-Export
# ---------------------------------------------------------------------------
def print_summary(port: str, start_ts: str) -> None:
    total = _pass + _fail
    bar = "═" * 62
    print(f"\n{bar}")
    print(f"  ERGEBNIS  lan8651_causality_test.py")
    print(f"  Board    : {port}")
    print(f"  Datum    : {start_ts}")
    print(f"  Abschluss: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(bar)
    print(f"  PASS  : {_pass:3d}")
    print(f"  FAIL  : {_fail:3d}")
    print(f"  WARN  : {_warn:3d}")
    print(f"  SKIP  : {_skip:3d}")
    print(f"  TOTAL : {total:3d} (PASS+FAIL)")
    print(bar)

    if _fail == 0 and _pass > 0:
        print("  ✓ ALLE Tests BESTANDEN.")
        print("  ✓ Register-Zugriff ist korrekt und kausal.")
    elif _fail == 0 and _pass == 0:
        print("  ⚠ Keine Tests ausgeführt (--only / --skip zu restriktiv?).")
    else:
        print(f"  ✗ {_fail} Test(e) GESCHEITERT.")
        print("  → Bitte folgende FAILs prüfen:")
        for r in _results_log:
            if r["status"] == "FAIL":
                print(f"    [{r['group']}] {r['label']}"
                      + (f"  — {r['detail']}" if r['detail'] else ""))

    print(bar)


def save_results(port: str, start_ts: str, filename: str) -> None:
    data = {
        "test":      "lan8651_causality_test",
        "port":      port,
        "start":     start_ts,
        "end":       datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "pass":      _pass,
        "fail":      _fail,
        "warn":      _warn,
        "skip":      _skip,
        "results":   _results_log,
    }
    with open(filename, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    print(f"\n  [SAVE] Ergebnisse gespeichert: {filename}")


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(
        description="LAN8651 Register-Kausalitätstest (umfassend)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help=f"Serieller Port (Standard: {DEFAULT_PORT})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TX_TIMEOUT,
                        help=f"Sekunden pro Transaktion (Standard: {DEFAULT_TX_TIMEOUT})")
    parser.add_argument("--no-restore", action="store_true",
                        help="Register nach Tests NICHT wiederherstellen")
    parser.add_argument("--skip", metavar="T01,T02,...",
                        help="Komma-getrennte Test-Gruppen überspringen (z.B. --skip T07,T08)")
    parser.add_argument("--only", metavar="T01,T02,...",
                        help="Nur diese Test-Gruppen ausführen")
    parser.add_argument("--save", metavar="FILE",
                        help="Ergebnisse als JSON speichern (z.B. --save results.json)")
    args = parser.parse_args()

    restore = not args.no_restore
    tx_to   = args.timeout

    skip_groups: set[str] = set()
    only_groups: set[str] | None = None

    if args.skip:
        skip_groups = {g.strip().upper() for g in args.skip.split(",")}
    if args.only:
        only_groups = {g.strip().upper() for g in args.only.split(",")}

    def should_run(code: str) -> bool:
        c = code.upper()
        if only_groups is not None:
            return c in only_groups
        return c not in skip_groups

    start_ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    print(f"\n{'═'*62}")
    print(f"  lan8651_causality_test.py — LAN8651 Kausaliätstest")
    print(f"  Port    : {args.port}")
    print(f"  Timeout : {tx_to:.1f}s pro Transaktion")
    print(f"  Restore : {'ja' if restore else 'NEIN'}")
    print(f"  Datum   : {start_ts}")
    print(f"{'═'*62}")
    print()

    try:
        ser = open_port(args.port)
    except serial.SerialException as exc:
        print(f"\n[FEHLER] Port {args.port} nicht öffnenbar: {exc}")
        sys.exit(1)

    wake_port(ser)

    try:
        if should_run("T01"):
            test_T01_read_all(ser, tx_to)
        else:
            print("\n[SKIP] T01 übersprungen")

        if should_run("T02"):
            test_T02_init_state(ser, tx_to)
        else:
            print("\n[SKIP] T02 übersprungen")

        if should_run("T03"):
            test_T03_roundtrip(ser, tx_to, restore)
        else:
            print("\n[SKIP] T03 übersprungen")

        if should_run("T04"):
            test_T04_sequential(ser, tx_to, restore)
        else:
            print("\n[SKIP] T04 übersprungen")

        if should_run("T05"):
            test_T05_isolation(ser, tx_to, restore)
        else:
            print("\n[SKIP] T05 übersprungen")

        if should_run("T06"):
            test_T06_uniqueness(ser, tx_to, restore)
        else:
            print("\n[SKIP] T06 übersprungen")

        if should_run("T07"):
            test_T07_consistency(ser, tx_to)
        else:
            print("\n[SKIP] T07 übersprungen")

        if should_run("T08"):
            test_T08_extreme_patterns(ser, tx_to, restore)
        else:
            print("\n[SKIP] T08 übersprungen")

        if should_run("T09"):
            test_T09_imask_roundtrip(ser, tx_to, restore)
        else:
            print("\n[SKIP] T09 übersprungen")

        if should_run("T10"):
            test_T10_plca_status(ser, tx_to)
        else:
            print("\n[SKIP] T10 übersprungen")

    finally:
        ser.close()
        print("\n[SER] Port geschlossen.")

    print_summary(args.port, start_ts)

    if args.save:
        save_results(args.port, start_ts, args.save)

    sys.exit(0 if _fail == 0 else 1)


if __name__ == "__main__":
    main()
