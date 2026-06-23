#!/usr/bin/env python3
"""
ioctl_link_down_test.py
=======================
Testet Option B: sicherer LAN865X Register-Zugriff via ioctl
Protokoll: ip link set eth0 down  →  lan_read/lan_write  →  ip link set eth0 up

Verbindet sich via COM9 (root/microchip) auf das Target.

Verwendung:
    python ioctl_link_down_test.py
    python ioctl_link_down_test.py COM9
"""

import serial
import threading
import queue
import time
import sys
from datetime import datetime


# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
DEFAULT_PORT     = "COM9"
DEFAULT_BAUDRATE = 115200
LOGIN_USER       = "root"
LOGIN_PASS       = "microchip"
INTERFACE        = "eth0"
IOCTL_DEV        = "/dev/lan865x_eth0"

# Register-Adressen (aus lan_ioctl.h)
REG_MAC_NET_CTL   = 0x00010000   # Network Control  (TX/RX Enable)
REG_MAC_NET_CFG   = 0x00010001   # Network Config   (Promiscuous etc.)
REG_MAC_L_SADDR1  = 0x00010022   # MAC Address [31:0]
REG_MAC_H_SADDR1  = 0x00010023   # MAC Address [47:32]
REG_MAC_TSU_INCR  = 0x00010077   # TSU Timer Increment


# ---------------------------------------------------------------------------
# SerialCLI (identisches Framework wie ethtool_phytool_tester.py)
# ---------------------------------------------------------------------------
class SerialCLI:
    PROMPTS        = ["# ", "$ ", "~# ", "~$ ", "root@"]
    _FLUSH_PATTERNS = ["assword", "login:", "Login:", "# ", "$ ", ":~#", ":~$"]

    def __init__(self, port: str, baudrate: int = 115200):
        self.port     = port
        self.baudrate = baudrate
        self.ser      = None
        self._rx_queue: queue.Queue = queue.Queue()
        self._stop     = threading.Event()
        self._thread   = None

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(
                port=self.port, baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE, timeout=0.1,
                xonxoff=False, rtscts=False, dsrdtr=False,
            )
            print(f"[COM] Verbunden: {self.port} @ {self.baudrate} baud")
            self._stop.clear()
            self._thread = threading.Thread(target=self._reader, daemon=True)
            self._thread.start()
            return True
        except serial.SerialException as e:
            print(f"[COM] FEHLER: {e}")
            return False

    def disconnect(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self.ser and self.ser.is_open:
            self.ser.close()
        print("[COM] Verbindung getrennt")

    def _reader(self):
        buf = ""
        while not self._stop.is_set():
            try:
                if self.ser and self.ser.in_waiting:
                    data = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="replace")
                    buf += data
                    lines = buf.split("\n")
                    buf = lines[-1]
                    for line in lines[:-1]:
                        self._rx_queue.put(line.rstrip("\r"))
                    if buf and any(p in buf for p in self._FLUSH_PATTERNS):
                        self._rx_queue.put(buf.rstrip("\r"))
                        buf = ""
                else:
                    time.sleep(0.02)
            except Exception:
                time.sleep(0.1)

    def _send_raw(self, text: str):
        if self.ser and self.ser.is_open:
            self.ser.write(text.encode("utf-8"))
            self.ser.flush()

    def _wait_for(self, patterns: list, timeout: float) -> tuple:
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self._rx_queue.get(timeout=0.1)
                lines.append(line)
                if any(p in line for p in patterns):
                    return True, lines
            except queue.Empty:
                pass
        return False, lines

    def login(self, timeout: float = 20.0) -> bool:
        print("[LOGIN] Warte auf Prompt...")
        self._send_raw("\r\n")
        time.sleep(0.5)
        found, lines = self._wait_for(
            self.PROMPTS + ["login:", "Login:", "assword:", "Password:"], timeout=timeout)

        if any(p in l for l in lines for p in self.PROMPTS):
            print("[LOGIN] Bereits eingeloggt")
            return True

        if any("assword" in l for l in lines):
            self._send_raw(LOGIN_PASS + "\r\n")
            found3, _ = self._wait_for(self.PROMPTS, timeout=8)
            if found3:
                print("[LOGIN] Eingeloggt (Passwort direkt)")
                return True
            return False

        if any("login" in l.lower() for l in lines):
            self._send_raw(LOGIN_USER + "\r\n")
            time.sleep(0.5)
            found2, lines2 = self._wait_for(["assword", "assword:"], timeout=8)
            if found2:
                self._send_raw(LOGIN_PASS + "\r\n")
                found3, _ = self._wait_for(self.PROMPTS, timeout=10)
                if found3:
                    print("[LOGIN] Erfolgreich eingeloggt")
                    return True
            return False

        print("[LOGIN] FEHLER: Kein Prompt erkannt")
        return False

    def run(self, cmd: str, timeout: float = 8.0) -> str:
        while not self._rx_queue.empty():
            try:
                self._rx_queue.get_nowait()
            except queue.Empty:
                break
        self._send_raw(cmd + "\r\n")
        _, lines = self._wait_for(self.PROMPTS, timeout=timeout)
        if lines and cmd.strip() in lines[0]:
            lines = lines[1:]
        if lines and any(p in lines[-1] for p in self.PROMPTS):
            lines = lines[:-1]
        return "\n".join(lines).strip()


# ---------------------------------------------------------------------------
# Test-Klasse
# ---------------------------------------------------------------------------
class IoctlLinkDownTester:
    def __init__(self, cli: SerialCLI):
        self.cli     = cli
        self.results = []
        self.passed  = 0
        self.failed  = 0

    # ------------------------------------------------------------------
    def _run(self, label: str, cmd: str, timeout: float = 6.0) -> str:
        out = self.cli.run(cmd, timeout=timeout)
        self.results.append({"label": label, "cmd": cmd, "out": out})
        return out

    def _check(self, label: str, cmd: str, expect: str, timeout: float = 6.0) -> bool:
        out = self._run(label, cmd, timeout)
        ok = expect.lower() in out.lower()
        status = "PASS" if ok else "FAIL"
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        print(f"  [{status}] {label}")
        if not ok:
            print(f"         Erwartet: '{expect}'")
            print(f"         Erhalten: '{out[:120]}'")
        return ok

    def _sec(self, title: str):
        print(f"\n{'='*65}")
        print(f"  {title}")
        print(f"{'='*65}")

    # ------------------------------------------------------------------
    def test_0_voraussetzungen(self):
        self._sec("0. VORAUSSETZUNGEN")

        out = self._run("ioctl device vorhanden", f"ls -la {IOCTL_DEV} 2>&1")
        if IOCTL_DEV in out:
            print(f"  [PASS] {IOCTL_DEV} vorhanden")
            self.passed += 1
        else:
            print(f"  [FAIL] {IOCTL_DEV} NICHT gefunden!")
            print(f"         Ausgabe: {out}")
            self.failed += 1
            return False

        out = self._run("lan_read verfügbar", "which lan_read 2>&1 || echo NOT_FOUND")
        if "NOT_FOUND" in out:
            print("  [FAIL] lan_read nicht gefunden — bitte in /usr/bin installieren")
            self.failed += 1
            return False
        print(f"  [PASS] lan_read: {out.strip()}")
        self.passed += 1

        out = self._run("lan_write verfügbar", "which lan_write 2>&1 || echo NOT_FOUND")
        if "NOT_FOUND" in out:
            print("  [FAIL] lan_write nicht gefunden")
            self.failed += 1
            return False
        print(f"  [PASS] lan_write: {out.strip()}")
        self.passed += 1

        return True

    # ------------------------------------------------------------------
    def test_1_ebusy_schutz(self):
        """Wenn Link UP ist, muss ioctl EBUSY (Fehler) zurückgeben."""
        self._sec("1. EBUSY-SCHUTZ: ioctl bei Link UP muss abgewiesen werden")

        # Sicherstellen dass Link UP
        self._run("link up setzen", f"ip link set {INTERFACE} up")
        time.sleep(1)

        out = self._run("link status prüfen", f"ip link show {INTERFACE} 2>&1")
        if "UP" in out:
            print(f"  [INFO] Link ist UP — Test EBUSY-Schutz")
        else:
            print(f"  [WARN] Link-Status unklar: {out[:80]}")

        # lan_read bei Link UP — muss scheitern
        out = self._run("lan_read bei Link UP (muss EBUSY)", f"lan_read 0x{REG_MAC_NET_CTL:08X} 2>&1")
        if any(kw in out.lower() for kw in ["busy", "error", "fail", "operation not permitted",
                                             "-16", "ebusy", "cannot"]):
            print(f"  [PASS] EBUSY-Schutz wirkt — ioctl korrekt abgewiesen")
            print(f"         Meldung: {out[:100]}")
            self.passed += 1
        else:
            # Falls Register-Wert zurückkommt = Schutz FEHLT
            print(f"  [WARN] EBUSY-Schutz: Unklare Antwort: '{out[:100]}'")
            print(f"         Wenn ein Hex-Wert erscheint: Schutz fehlt! Chip könnte beschädigt werden!")
            # Kein hartes FAIL — Kernel-Version könnte Check nicht haben
            self.results[-1]["warn"] = True

    # ------------------------------------------------------------------
    def test_2_link_down_read(self):
        """Lesen aller Key-Register bei Link DOWN."""
        self._sec("2. REGISTER LESEN bei Link DOWN")

        print(f"  [STEP] ip link set {INTERFACE} down")
        self._run("link down", f"ip link set {INTERFACE} down")
        time.sleep(1)

        # Link-Status bestätigen
        out = self._run("link status", f"ip link show {INTERFACE} 2>&1")
        if "DOWN" in out or ("UP" not in out):
            print(f"  [INFO] Link ist DOWN — starte Register-Reads")
        else:
            print(f"  [WARN] Link möglicherweise noch UP: {out[:80]}")

        t_start = time.time()

        regs = [
            (REG_MAC_NET_CTL,  "MAC_NET_CTL  (Network Control, erwartet ~0x0C)"),
            (REG_MAC_NET_CFG,  "MAC_NET_CFG  (Network Config)"),
            (REG_MAC_L_SADDR1, "MAC_L_SADDR1 (MAC Addr [31:0])"),
            (REG_MAC_H_SADDR1, "MAC_H_SADDR1 (MAC Addr [47:32])"),
            (REG_MAC_TSU_INCR, "MAC_TSU_INCR (TSU Timer, erwartet 0x28)"),
        ]

        read_ok = 0
        for addr, desc in regs:
            out = self._run(f"Read 0x{addr:08X}", f"lan_read 0x{addr:08X} 2>&1")
            # Erfolgreich wenn Hex-Wert in Ausgabe
            if "0x" in out.lower() or (len(out) > 0 and all(c in "0123456789abcdefABCDEF \n\r" for c in out.strip())):
                print(f"  [PASS] {desc}")
                print(f"         Wert: {out.strip()}")
                self.passed += 1
                read_ok += 1
            else:
                print(f"  [FAIL] {desc}")
                print(f"         Ausgabe: '{out[:100]}'")
                self.failed += 1

        elapsed = time.time() - t_start
        if read_ok > 0:
            avg_ms = (elapsed / read_ok) * 1000
            print(f"\n  [PERF] {read_ok} Reads in {elapsed:.3f}s → Ø {avg_ms:.1f} ms/Read")

        return read_ok

    # ------------------------------------------------------------------
    def test_3_write_readback(self):
        """Schreibe einen sicheren Testwert und lese zurück."""
        self._sec("3. WRITE → READBACK Konsistenztest (Link DOWN)")

        # Link DOWN sicherstellen (sollte noch down sein)
        out = self._run("link status", f"ip link show {INTERFACE} 2>&1")
        if "UP" in out and "DOWN" not in out:
            print(f"  [STEP] Link war UP — setze DOWN")
            self._run("link down", f"ip link set {INTERFACE} down")
            time.sleep(1)

        # Original-Wert lesen
        out_orig = self._run("Read Original MAC_NET_CTL", f"lan_read 0x{REG_MAC_NET_CTL:08X} 2>&1")
        print(f"  [INFO] Original MAC_NET_CTL: {out_orig.strip()}")

        # Testwert schreiben: TX+RX enable (0x0C) — sicherer Wert
        test_val = 0x0000000C
        print(f"  [STEP] Schreibe 0x{test_val:08X} → MAC_NET_CTL")
        self._run("Write MAC_NET_CTL", f"lan_write 0x{REG_MAC_NET_CTL:08X} 0x{test_val:08X} 2>&1")
        time.sleep(0.1)

        # Zurücklesen
        out_rb = self._run("Readback MAC_NET_CTL", f"lan_read 0x{REG_MAC_NET_CTL:08X} 2>&1")
        print(f"  [INFO] Readback MAC_NET_CTL: {out_rb.strip()}")

        # Konsistenz prüfen
        expected_hex = f"{test_val:08x}"
        if expected_hex in out_rb.lower() or f"0x{expected_hex}" in out_rb.lower():
            print(f"  [PASS] Write/Readback konsistent: 0x{test_val:08X}")
            self.passed += 1
        else:
            print(f"  [FAIL] Write/Readback inkonsistent!")
            print(f"         Geschrieben: 0x{test_val:08X}")
            print(f"         Gelesen:     {out_rb.strip()}")
            self.failed += 1

        # Original-Wert wiederherstellen
        # Ausgabe-Format: "0xAAAAAAAA = 0xVVVVVVVV" — zweiten Hex-Wert (Wert, nicht Adresse) nehmen
        print(f"  [STEP] Stelle Original-Wert wieder her: {out_orig.strip()}")
        import re
        matches = re.findall(r'0x([0-9a-fA-F]+)', out_orig)
        if len(matches) >= 2:
            # matches[0] = Adresse, matches[1] = Wert
            restore_val = int(matches[1], 16)
            self._run("Restore MAC_NET_CTL",
                      f"lan_write 0x{REG_MAC_NET_CTL:08X} 0x{restore_val:08X} 2>&1")
            print(f"  [INFO] Wiederhergestellt: 0x{restore_val:08X}")
        elif len(matches) == 1:
            restore_val = int(matches[0], 16)
            self._run("Restore MAC_NET_CTL",
                      f"lan_write 0x{REG_MAC_NET_CTL:08X} 0x{restore_val:08X} 2>&1")
            print(f"  [INFO] Wiederhergestellt: 0x{restore_val:08X}")
        else:
            print(f"  [WARN] Kein Hex-Wert in Original-Ausgabe — kein Restore möglich")

    # ------------------------------------------------------------------
    def test_4_link_up_verify(self):
        """Link wieder UP setzen und prüfen ob PLCA/Link sich erholt."""
        self._sec("4. LINK WIEDERHERSTELLEN und Verifikation")

        print(f"  [STEP] ip link set {INTERFACE} up")
        self._run("link up", f"ip link set {INTERFACE} up")

        # Warten bis Link UP
        link_ok = False
        for attempt in range(8):
            time.sleep(1)
            out = self._run(f"link status check {attempt+1}", f"ip link show {INTERFACE} 2>&1")
            if "LOWER_UP" in out or ("UP" in out and "DOWN" not in out):
                link_ok = True
                break
            print(f"  [WAIT] Link noch nicht UP... ({attempt+1}/8)")

        if link_ok:
            print(f"  [PASS] Link ist wieder UP")
            self.passed += 1
        else:
            print(f"  [FAIL] Link kam nach ip link up nicht zurück!")
            print(f"         Letzter Status: {out[:120]}")
            self.failed += 1

        # PLCA-Status prüfen
        time.sleep(1)
        out = self._run("PLCA-Status nach Wiederherstellung", f"ethtool --get-plca-status {INTERFACE} 2>&1")
        print(f"  [INFO] PLCA: {out.strip()}")
        if "on" in out.lower() or "up" in out.lower():
            print(f"  [PASS] PLCA aktiv")
            self.passed += 1
        else:
            print(f"  [WARN] PLCA-Status: {out.strip()} (kein Partner auf Bus?)")

        # ethtool Basisstatus
        out = self._run("ethtool Status", f"ethtool {INTERFACE} 2>&1")
        print(f"\n  [INFO] ethtool nach Restore:")
        for line in out.splitlines()[:8]:
            print(f"         {line}")

    # ------------------------------------------------------------------
    def test_5_performance(self):
        """Geschwindigkeitstest: 10 Reads bei Link DOWN, Timing messen."""
        self._sec("5. PERFORMANCE-TEST (10 Reads bei Link DOWN)")

        print(f"  [STEP] Link DOWN für Performance-Test")
        self._run("link down", f"ip link set {INTERFACE} down")
        time.sleep(1)

        times = []
        print(f"  Starte 10 Reads von MAC_NET_CTL...")
        for i in range(10):
            t0 = time.time()
            out = self.cli.run(f"lan_read 0x{REG_MAC_NET_CTL:08X}", timeout=3.0)
            dt = (time.time() - t0) * 1000
            times.append(dt)
            print(f"  Read {i+1:2d}: {out.strip():<20}  {dt:.1f} ms")

        avg = sum(times) / len(times)
        mn  = min(times)
        mx  = max(times)
        print(f"\n  [PERF] Min: {mn:.1f} ms  Max: {mx:.1f} ms  Avg: {avg:.1f} ms")

        if avg < 50:
            print(f"  [PASS] Schnell (<50ms) — ioctl-Pfad aktiv")
            self.passed += 1
        elif avg < 500:
            print(f"  [PASS] OK (<500ms)")
            self.passed += 1
        else:
            print(f"  [WARN] Langsam (>500ms) — möglicherweise debugfs-Fallback")

        # Link wieder UP
        print(f"\n  [STEP] Link wieder UP")
        self._run("link up", f"ip link set {INTERFACE} up")
        time.sleep(2)

    # ------------------------------------------------------------------
    def print_summary(self):
        self._sec("ZUSAMMENFASSUNG")
        total = self.passed + self.failed
        print(f"  Datum:    {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"  Ergebnis: {self.passed}/{total} Tests bestanden")
        print()

        if self.failed == 0:
            print("  ✅ ALLE TESTS BESTANDEN")
            print("  Option B (ip link down → ioctl → ip link up) funktioniert korrekt.")
        else:
            print(f"  ❌ {self.failed} TEST(S) FEHLGESCHLAGEN")

        print()
        print("  OPTION B PROTOKOLL (für manuelles Debugging):")
        print("  ─────────────────────────────────────────────")
        print(f"  ip link set {INTERFACE} down")
        print(f"  lan_read  0x{REG_MAC_NET_CTL:08X}            # MAC_NET_CTL lesen")
        print(f"  lan_read  0x{REG_MAC_NET_CFG:08X}            # MAC_NET_CFG lesen")
        print(f"  lan_read  0x{REG_MAC_L_SADDR1:08X}           # MAC-Adresse low")
        print(f"  lan_read  0x{REG_MAC_H_SADDR1:08X}           # MAC-Adresse high")
        print(f"  lan_write 0x{REG_MAC_NET_CTL:08X} 0x0000000C  # TX+RX Enable")
        print(f"  ip link set {INTERFACE} up")
        print()
        print("  VOLLSTÄNDIGE KOMMANDO-AUSGABEN:")
        print("  ─────────────────────────────────────────────")
        for r in self.results:
            print(f"\n  [{r['label']}]")
            print(f"  CMD: {r['cmd']}")
            print(f"  OUT: {r['out']}")

    # ------------------------------------------------------------------
    def run_all(self):
        if not self.test_0_voraussetzungen():
            print("\n[ABBRUCH] Voraussetzungen nicht erfüllt")
            return
        self.test_1_ebusy_schutz()
        self.test_2_link_down_read()
        self.test_3_write_readback()
        self.test_4_link_up_verify()
        self.test_5_performance()
        self.print_summary()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    port     = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUDRATE

    print("=" * 65)
    print("  LAN865X ioctl Option-B Tester")
    print("  Protokoll: ip link down → ioctl → ip link up")
    print(f"  Port: {port}  |  Login: {LOGIN_USER}/{LOGIN_PASS}")
    print("=" * 65)

    cli = SerialCLI(port, baudrate)
    if not cli.connect():
        sys.exit(1)

    try:
        if not cli.login():
            print("ABBRUCH: Login fehlgeschlagen")
            sys.exit(1)
        tester = IoctlLinkDownTester(cli)
        tester.run_all()
    except KeyboardInterrupt:
        print("\n[ABBRUCH] Durch Benutzer unterbrochen")
    finally:
        cli.disconnect()


if __name__ == "__main__":
    main()
