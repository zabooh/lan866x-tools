#!/usr/bin/env python3
"""
reg_access_test.py
------------------
Verifikation des LAN865x Register-Roundtrip via serieller Konsole (GM-Board).

Testet ob der Callback-basierte Register-Zugriffs-Mechanismus in der Firmware
korrekt funktioniert, BEVOR mit der eigentlichen TTSCAA-Diagnose weitergemacht wird.

Testplan:
  T01  CONFIG0 lesen → FTSE-Bit (0x80) muss gesetzt sein
  T02  IMASK0 lesen  → muss 0x00000000 sein (TTSCAA unmaskiert)
  T03  TXMLOC lesen  → Basis-Wert erfassen
  T04  TXMLOC schreiben (Testwert 0xAB) → lesen zurück → prüfen
  T05  TXMLOC wiederherstellen
  T06  TXMCTL lesen (zwischen Syncs) → muss 0x0000 oder 0x0002 sein (nicht 0xDEAD)
  T07  NETWORK_CTRL lesen → TXEN+RXEN (Bits 2+3) müssen gesetzt sein
  T08  Callback-Latenz: Messung der Roundtrip-Zeit (Write + Read = 2 Callbacks)

Jeder Test-Check wird mit PASS / FAIL annotiert.
Am Ende: Zusammenfassung PASS/FAIL-Zähler.

Verwendung:
    python reg_access_test.py
    python reg_access_test.py --port COM10
    python reg_access_test.py --port COM10 --no-restore   # TXMLOC nicht zurückschreiben
"""

import serial
import time
import re
import sys
import argparse
from datetime import datetime

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
BAUDRATE        = 115200
SERIAL_TIMEOUT  = 3
RESP_TIMEOUT    = 5.0
PROMPT_MARKERS  = ["\r\n> ", "\n> ", "> "]

GM_PORT_DEFAULT = "COM10"

# Register-Adressen (aus ptp_gm_task.h + LAN8651 Datenblatt)
REG_CONFIG0      = 0x00000004
REG_STATUS0      = 0x00000008
REG_IMASK0       = 0x0000000C
REG_NETWORK_CTRL = 0x00010000
REG_TXMCTL       = 0x00040040
REG_TXMLOC       = 0x00040045   # TX-Match Location (Byte-Offset im Frame) — sicherer Test-Reg

# Erwartete Werte
TXMLOC_INIT_EXPECTED = 12        # Init schreibt 12 (Byte-Offset 12 = EtherType-Feld)
TXMLOC_TEST_VALUE    = 0xAB      # Testwert für Roundtrip-Schreibtest
CONFIG0_FTSE_MASK    = 0x80      # Frame Timestamp Enable (FTSE)
CONFIG0_FTSS_MASK    = 0x40      # Frame Timestamp Slot Select (FTSS = Slot A)
NETCTRL_TXEN_RXEN    = 0x0C      # Bits 2+3: TXEN + RXEN

# ---------------------------------------------------------------------------
# Zähler
# ---------------------------------------------------------------------------
_pass_count = 0
_fail_count = 0
_warn_count = 0

# ---------------------------------------------------------------------------
# Serielle Hilfsfunktionen (gleiche Muster wie tsu_register_check.py)
# ---------------------------------------------------------------------------
def open_port(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUDRATE, timeout=SERIAL_TIMEOUT)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"[{port}] Verbunden.")
    return ser


def wake_port(ser: serial.Serial, name: str) -> None:
    """Bereinigt den Eingangspuffer und wartet auf Prompt."""
    ser.write(b"\r\n")
    time.sleep(0.5)
    ser.reset_input_buffer()
    print(f"[{name}] Prompt bereit.")


def send_cmd(ser: serial.Serial, name: str, cmd: str,
             timeout: float = RESP_TIMEOUT) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    print(f"[{name}] >>> {cmd}")
    response = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            response += chunk
            if any(response.endswith(p) for p in PROMPT_MARKERS):
                break
        else:
            time.sleep(0.05)
    time.sleep(0.1)
    if ser.in_waiting:
        response += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
    lines = [ln for ln in response.splitlines() if cmd not in ln]
    out = "\n".join(lines).strip()
    if out:
        print(f"[{name}] <<< {out}")
    return response


def capture_async(ser: serial.Serial, name: str, duration_s: float) -> str:
    deadline = time.time() + duration_s
    captured = ""
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            captured += chunk
            for line in chunk.splitlines():
                if line.strip():
                    print(f"  [{name}]  {line}")
        else:
            time.sleep(0.05)
    return captured


# ---------------------------------------------------------------------------
# Register-Zugriff via lan_read / lan_write CLI-Befehle
# ---------------------------------------------------------------------------
def lan_read(ser: serial.Serial, name: str, addr: int) -> "int | None":
    """Sendet 'lan_read 0x...' und parst den zurückgegebenen 32-Bit-Wert."""
    addr_str = f"0x{addr:08X}"
    t0 = time.monotonic()
    resp = send_cmd(ser, name, f"lan_read {addr_str}", timeout=4.0)
    time.sleep(0.1)
    extra = capture_async(ser, name, 0.4)
    duration_ms = (time.monotonic() - t0) * 1000.0
    text = resp + extra

    # Format: "LAN865X Read: Addr=0x00010000 Value=0x0000000C"
    m = re.search(r'LAN865X Read: Addr=0x[0-9A-Fa-f]+\s+Value=(0x[0-9A-Fa-f]+)', text)
    if m:
        return int(m.group(1), 16), duration_ms
    m2 = re.search(r'Value=(0x[0-9A-Fa-f]+)', text)
    if m2:
        return int(m2.group(1), 16), duration_ms

    # Prüfe auf Callback-Timeout-Meldungen
    if "cb timeout" in text.lower() or "callback timeout" in text.lower():
        print(f"  ⚠ lan_read 0x{addr:08X}: Callback-Timeout erkannt!")
    return None, duration_ms


def lan_write(ser: serial.Serial, name: str, addr: int, value: int) -> "float":
    """Sendet 'lan_write 0x... 0x...' und gibt die Roundtrip-Zeit zurück."""
    t0 = time.monotonic()
    resp = send_cmd(ser, name, f"lan_write 0x{addr:08X} 0x{value:08X}", timeout=4.0)
    duration_ms = (time.monotonic() - t0) * 1000.0
    time.sleep(0.1)
    return duration_ms, resp


# ---------------------------------------------------------------------------
# Test-Helfer
# ---------------------------------------------------------------------------
def check(label: str, passed: bool, detail: str = "") -> bool:
    global _pass_count, _fail_count
    if passed:
        _pass_count += 1
        marker = "PASS"
    else:
        _fail_count += 1
        marker = "FAIL"
    s = f"  [{marker}] {label}"
    if detail:
        s += f"  — {detail}"
    print(s)
    return passed


def warn(label: str, detail: str = "") -> None:
    global _warn_count
    _warn_count += 1
    s = f"  [WARN] {label}"
    if detail:
        s += f"  — {detail}"
    print(s)


def section(title: str) -> None:
    print(f"\n{'─'*60}")
    print(f"  {title}")
    print(f"{'─'*60}")


# ---------------------------------------------------------------------------
# Einzelne Tests
# ---------------------------------------------------------------------------
def test_config0(ser: serial.Serial, name: str) -> None:
    section("T01  CONFIG0 — FTSE/FTSS-Bits prüfen")
    val, ms = lan_read(ser, name, REG_CONFIG0)
    if val is None:
        check("CONFIG0 lesbar", False, "kein Wert zurückgegeben (Callback-Problem?)")
        return
    print(f"  CONFIG0 = 0x{val:08X}  (Roundtrip: {ms:.0f} ms)")
    check("CONFIG0 lesbar", True, f"0x{val:08X}")
    check("FTSE-Bit (0x80) gesetzt",
          bool(val & CONFIG0_FTSE_MASK),
          f"bit7={'1' if val & CONFIG0_FTSE_MASK else '0 (Frame-Timestamp-Capture deaktiviert!)'}")
    check("FTSS-Bit (0x40) gesetzt",
          bool(val & CONFIG0_FTSS_MASK),
          f"bit6={'1' if val & CONFIG0_FTSS_MASK else '0 (Slot-A-Select fehlt!)'}")
    if ms > 500:
        warn("CONFIG0 Roundtrip-Zeit hoch", f"{ms:.0f} ms (erwartet <500 ms)")


def test_imask0(ser: serial.Serial, name: str) -> None:
    section("T02  IMASK0 — alle Interrupts unmaskiert?")
    val, ms = lan_read(ser, name, REG_IMASK0)
    if val is None:
        check("IMASK0 lesbar", False, "kein Wert zurückgegeben")
        return
    print(f"  IMASK0 = 0x{val:08X}  (Roundtrip: {ms:.0f} ms)")
    check("IMASK0 lesbar", True)
    check("IMASK0 == 0x00000000 (TTSCAA unmaskiert)",
          val == 0x00000000,
          f"ist 0x{val:08X} — TTSCAA-Bit (bit8) {'maskiert!' if val & 0x100 else 'ok'}")
    if val != 0:
        # Einzelne maskierte Bits aufzeigen
        masked_bits = [i for i in range(32) if (val >> i) & 1]
        warn("IMASK0 hat maskierte Bits", f"Bits: {masked_bits}")


def test_txmloc_roundtrip(ser: serial.Serial, name: str, restore: bool) -> None:
    section("T03–T05  TXMLOC — Register-Roundtrip-Schreibtest")
    print("  Lese Ausgangswert...")
    base_val, ms = lan_read(ser, name, REG_TXMLOC)
    if base_val is None:
        check("TXMLOC lesbar", False, "kein Wert zurückgegeben")
        return
    print(f"  TXMLOC (Ausgangswert) = 0x{base_val:08X}  (Roundtrip: {ms:.0f} ms)")
    check("TXMLOC lesbar", True, f"0x{base_val:08X}")
    check("TXMLOC Init-Wert == 12",
          base_val == TXMLOC_INIT_EXPECTED,
          f"erwartet 12 (0x0000000C), ist 0x{base_val:08X}")

    # Schreibe Testwert
    print(f"  Schreibe Testwert 0x{TXMLOC_TEST_VALUE:08X}...")
    write_ms, write_resp = lan_write(ser, name, REG_TXMLOC, TXMLOC_TEST_VALUE)
    if "cb timeout" in write_resp.lower():
        check("TXMLOC schreiben", False, "Callback-Timeout beim Schreiben!")
        return
    check("TXMLOC schreiben", True, f"Roundtrip: {write_ms:.0f} ms")
    if write_ms > 500:
        warn("TXMLOC Write-Roundtrip hoch", f"{write_ms:.0f} ms")

    # Lese zurück
    time.sleep(0.05)
    readback, ms2 = lan_read(ser, name, REG_TXMLOC)
    if readback is None:
        check("TXMLOC Readback", False, "kein Wert zurückgegeben")
    else:
        print(f"  TXMLOC Readback = 0x{readback:08X}  (Roundtrip: {ms2:.0f} ms)")
        check("TXMLOC Readback == Testwert",
              readback == TXMLOC_TEST_VALUE,
              f"erwartet 0x{TXMLOC_TEST_VALUE:08X}, gelesen 0x{readback:08X}")

    # Wiederherstellen
    if restore:
        print(f"  Wiederherstellen: TXMLOC = 0x{base_val:08X}...")
        lan_write(ser, name, REG_TXMLOC, base_val)
        restored, _ = lan_read(ser, name, REG_TXMLOC)
        check("TXMLOC wiederhergestellt",
              restored == base_val,
              f"0x{(restored or 0):08X}")
    else:
        warn("TXMLOC nicht wiederhergestellt (--no-restore)")


def test_txmctl(ser: serial.Serial, name: str) -> None:
    section("T06  TXMCTL — Wert zwischen Syncs")
    val, ms = lan_read(ser, name, REG_TXMCTL)
    if val is None:
        check("TXMCTL lesbar", False, "kein Wert zurückgegeben")
        return
    print(f"  TXMCTL = 0x{val:08X}  (Roundtrip: {ms:.0f} ms)")
    check("TXMCTL lesbar", True)
    # Zwischen Syncs: TXME=0 (disarmiert) erwartet.
    # Während Sync: TXME=1 (0x0002) — kurzes Zeitfenster.
    # TXPMDET (0x0080) zeigt Match — wäre hier sehr gut.
    is_sane = val in (0x0000, 0x0002, 0x0082)  # 0x0082 = TXME+TXPMDET gesetzt
    check("TXMCTL sinnvoller Wert",
          is_sane,
          f"0x{val:08X} {'(TXPMDET gesetzt — gut!)' if val & 0x0080 else ''}")
    if val & 0x0080:
        print("  ** HINWEIS: TXPMDET=1 beim Lesen — Match-Event aufgetreten! **")
    if not is_sane:
        warn("TXMCTL unerwarteter Wert — könnte auf Firmware-Regression hindeuten",
             f"0x{val:08X}")


def test_network_ctrl(ser: serial.Serial, name: str) -> None:
    section("T07  NETWORK_CTRL — TXEN+RXEN gesetzt?")
    val, ms = lan_read(ser, name, REG_NETWORK_CTRL)
    if val is None:
        check("NETWORK_CTRL lesbar", False, "kein Wert zurückgegeben")
        return
    print(f"  NETWORK_CTRL = 0x{val:08X}  (Roundtrip: {ms:.0f} ms)")
    check("NETWORK_CTRL lesbar", True)
    check("TXEN (bit2) gesetzt",
          bool(val & 0x04),
          f"bit2={'1' if val & 0x04 else '0 (TX deaktiviert!)'}")
    check("RXEN (bit3) gesetzt",
          bool(val & 0x08),
          f"bit3={'1' if val & 0x08 else '0 (RX deaktiviert!)'}")
    bit15 = (val >> 15) & 1
    print(f"  NETWORK_CTRL Bit15 (STORE_TX_TS?) = {bit15}  "
          f"{'(gesetzt — CHK-12 Hypothese prüfbar)' if bit15 else '(nicht gesetzt — CHK-12 Kandidat)'}")
    # Kein PASS/FAIL für Bit15 — nur informativ (Hypothese noch offen)


def test_callback_latency(ser: serial.Serial, name: str) -> None:
    """T08: Mehrere aufeinanderfolgende Reads — Latenz und Konsistenz messen."""
    section("T08  Callback-Latenz — 5× IMASK0 lesen")
    latencies = []
    values = []
    for i in range(5):
        val, ms = lan_read(ser, name, REG_IMASK0)
        latencies.append(ms)
        values.append(val)
        time.sleep(0.05)
    valid = [v for v in values if v is not None]
    print(f"  Latenzwerte (ms): {[f'{x:.0f}' for x in latencies]}")
    check("Alle 5 Reads erfolgreich", len(valid) == 5,
          f"{len(valid)}/5 erfolgreich")
    if len(valid) > 1:
        consistent = len(set(valid)) == 1
        check("Konsistente Werte (alle gleich)", consistent,
              f"Werte: {[f'0x{v:08X}' for v in valid]}")
    if latencies:
        avg_ms = sum(latencies) / len(latencies)
        max_ms = max(latencies)
        print(f"  Ø Latenz: {avg_ms:.0f} ms  |  Max: {max_ms:.0f} ms")
        check("Max. Callback-Latenz < 1000 ms", max_ms < 1000.0,
              f"max={max_ms:.0f} ms")


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------
def run_all_tests(port: str, restore: bool) -> None:
    print(f"\n{'='*60}")
    print(f"  reg_access_test.py — LAN865x Register-Zugriff Verifikation")
    print(f"  Datum: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  Port: {port}")
    print(f"{'='*60}")

    try:
        ser = open_port(port)
    except serial.SerialException as exc:
        print(f"[FEHLER] Konnte {port} nicht öffnen: {exc}")
        sys.exit(1)

    wake_port(ser, port)

    try:
        test_config0(ser, port)
        test_imask0(ser, port)
        test_txmloc_roundtrip(ser, port, restore)
        test_txmctl(ser, port)
        test_network_ctrl(ser, port)
        test_callback_latency(ser, port)
    finally:
        ser.close()

    # Zusammenfassung
    total = _pass_count + _fail_count
    print(f"\n{'='*60}")
    print(f"  ERGEBNIS: {_pass_count}/{total} Tests bestanden"
          f"  ({_warn_count} Warnungen)")
    if _fail_count == 0:
        print("  ✓ Alle Tests BESTANDEN — Register-Zugriff funktioniert korrekt.")
    else:
        print(f"  ✗ {_fail_count} Test(s) GESCHEITERT — Register-Zugriffs-Problem!")
        print("  → Prüfe Firmware-Callbacks und CLI-Befehle lan_read/lan_write.")
    print(f"{'='*60}\n")

    sys.exit(0 if _fail_count == 0 else 1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="LAN865x Register-Zugriff Verifikationstest (GM-Board)"
    )
    parser.add_argument("--port", default=GM_PORT_DEFAULT,
                        help=f"Serieller Port des GM-Boards (Standard: {GM_PORT_DEFAULT})")
    parser.add_argument("--no-restore", action="store_true",
                        help="TXMLOC nach dem Test nicht zurückschreiben")
    args = parser.parse_args()

    run_all_tests(args.port, restore=not args.no_restore)


if __name__ == "__main__":
    main()
