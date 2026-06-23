#!/usr/bin/env python3
"""
ethtool_phytool_tester.py
=========================
Testet ethtool und phytool auf dem LAN865X Target via serielle Konsole (COM9).

Login:   root / microchip
Target:  LAN965x / LAN866X MPU mit LAN865X T1S Ethernet

Verwendung:
    python ethtool_phytool_tester.py
    python ethtool_phytool_tester.py COM9
    python ethtool_phytool_tester.py COM9 115200

Ausgabe am Ende kopieren und zur Analyse einfügen.
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


# ---------------------------------------------------------------------------
# SerialCLI — robuster Thread-basierter serieller Zugriff
# ---------------------------------------------------------------------------
class SerialCLI:
    """Threaded serial reader mit Login-Support."""

    PROMPTS = ["# ", "$ ", "~# ", "~$ ", "root@"]

    def __init__(self, port: str, baudrate: int = 115200):
        self.port     = port
        self.baudrate = baudrate
        self.ser      = None
        self._rx_queue: queue.Queue = queue.Queue()
        self._stop     = threading.Event()
        self._thread   = None

    # ------------------------------------------------------------------
    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(
                port      = self.port,
                baudrate  = self.baudrate,
                bytesize  = serial.EIGHTBITS,
                parity    = serial.PARITY_NONE,
                stopbits  = serial.STOPBITS_ONE,
                timeout   = 0.1,
                xonxoff   = False,
                rtscts    = False,
                dsrdtr    = False,
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

    # ------------------------------------------------------------------
    # Prompt-Texte die OHNE \n enden — Buffer sofort flushen
    _FLUSH_PATTERNS = ["assword", "login:", "Login:", "# ", "$ ", ":~#", ":~$"]

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
                    # Unvollständige Zeile flushen wenn Prompt-Muster enthalten
                    # (z.B. "Password: " endet ohne \n)
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

    # ------------------------------------------------------------------
    def _collect(self, timeout: float) -> list[str]:
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self._rx_queue.get(timeout=0.05)
                lines.append(line)
            except queue.Empty:
                pass
        return lines

    def _wait_for(self, patterns: list[str], timeout: float) -> tuple[bool, list[str]]:
        """Sammelt bis ein Muster auftaucht oder Timeout."""
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

    # ------------------------------------------------------------------
    def login(self, timeout: float = 20.0) -> bool:
        """Sendet Enter, wartet auf login-Prompt und meldet sich an."""
        print("[LOGIN] Warte auf Prompt...")

        # Enter senden — vielleicht ist Session schon offen
        self._send_raw("\r\n")
        time.sleep(0.5)

        # Alles sammeln was kommt
        found, lines = self._wait_for(
            self.PROMPTS + ["login:", "Login:", "assword:", "Password:"],
            timeout=timeout
        )

        print(f"[LOGIN] Empfangen ({len(lines)} Zeilen):")
        for l in lines[-10:]:
            print(f"         > {repr(l)}")

        # Schon eingeloggt?
        if any(p in l for l in lines for p in self.PROMPTS):
            print("[LOGIN] Bereits eingeloggt")
            return True

        # Direkt Passwort-Prompt? (z.B. nach vorherigem Username-Echo)
        if any("assword" in l for l in lines):
            print("[LOGIN] Passwort-Prompt direkt erkannt, sende Passwort...")
            self._send_raw(LOGIN_PASS + "\r\n")
            found3, lines3 = self._wait_for(self.PROMPTS, timeout=8)
            if found3:
                print("[LOGIN] Erfolgreich eingeloggt")
                return True
            print("[LOGIN] FEHLER nach Passwort:", lines3[-5:])
            return False

        # Login-Prompt
        if any("login" in l.lower() for l in lines):
            print("[LOGIN] Login-Prompt erkannt, sende Benutzernamen...")
            self._send_raw(LOGIN_USER + "\r\n")
            time.sleep(0.5)
            # Warten auf Passwort-Prompt (großzügiger Timeout, verschiedene Schreibweisen)
            found2, lines2 = self._wait_for(
                ["assword:", "assword :", "ASSWORD", "assword"],
                timeout=8
            )
            print(f"[LOGIN] Nach Username empfangen ({len(lines2)} Zeilen):")
            for l in lines2[-10:]:
                print(f"         > {repr(l)}")
            if found2:
                print("[LOGIN] Passwort-Prompt erkannt, sende Passwort...")
                self._send_raw(LOGIN_PASS + "\r\n")
                found3, lines3 = self._wait_for(self.PROMPTS, timeout=10)
                if found3:
                    print("[LOGIN] Erfolgreich eingeloggt")
                    return True
                print("[LOGIN] FEHLER: Kein Shell-Prompt nach Passwort")
                print("        Empfangen:", lines3[-5:] if lines3 else "(nichts)")
                return False
            # Kein Passwort-Prompt — vielleicht kein Passwort nötig?
            print("[LOGIN] Kein Passwort-Prompt — prüfe ob Shell-Prompt kam...")
            if any(p in l for l in lines2 for p in self.PROMPTS):
                print("[LOGIN] Eingeloggt ohne Passwort")
                return True
            print("[LOGIN] FEHLER: Kein Passwort-Prompt und kein Shell-Prompt")
            return False

        # Nichts erkannt — nochmal versuchen
        print("[LOGIN] Kein bekannter Prompt — sende nochmal Enter...")
        self._send_raw("\r\n")
        time.sleep(2)
        self._send_raw("\r\n")
        found, lines = self._wait_for(
            self.PROMPTS + ["login:", "Login:"],
            timeout=10
        )
        print(f"[LOGIN] 2. Versuch empfangen ({len(lines)} Zeilen):")
        for l in lines[-10:]:
            print(f"         > {repr(l)}")
        if any(p in l for l in lines for p in self.PROMPTS):
            print("[LOGIN] Bereits eingeloggt (2. Versuch)")
            return True
        if any("login" in l.lower() for l in lines):
            # Rekursiv nochmal versuchen (nur einmal)
            print("[LOGIN] Login-Prompt beim 2. Versuch — starte Login-Sequenz...")
            self._send_raw(LOGIN_USER + "\r\n")
            time.sleep(0.5)
            found2, lines2 = self._wait_for(["assword:", "assword"], timeout=8)
            if found2:
                self._send_raw(LOGIN_PASS + "\r\n")
                found3, lines3 = self._wait_for(self.PROMPTS, timeout=10)
                if found3:
                    print("[LOGIN] Erfolgreich eingeloggt (2. Versuch)")
                    return True
        print("[LOGIN] FEHLER: Kein Prompt erreichbar")
        return False

    # ------------------------------------------------------------------
    def run(self, cmd: str, timeout: float = 6.0) -> str:
        """Sendet Kommando, wartet auf Shell-Prompt, gibt Ausgabe zurück."""
        # RX-Queue leeren
        while not self._rx_queue.empty():
            try:
                self._rx_queue.get_nowait()
            except queue.Empty:
                break

        self._send_raw(cmd + "\r\n")
        _, lines = self._wait_for(self.PROMPTS, timeout=timeout)

        # Erste Zeile ist Echo des Kommandos — entfernen wenn vorhanden
        if lines and cmd.strip() in lines[0]:
            lines = lines[1:]
        # Letzte Zeile ist der Prompt — entfernen
        if lines and any(p in lines[-1] for p in self.PROMPTS):
            lines = lines[:-1]

        return "\n".join(lines).strip()


# ---------------------------------------------------------------------------
# Test-Sektionen
# ---------------------------------------------------------------------------
class EthtoolPhytoolTester:
    def __init__(self, cli: SerialCLI):
        self.cli    = cli
        self.iface  = INTERFACE
        self.results: list[dict] = []

    def _run(self, label: str, cmd: str, timeout: float = 6.0) -> str:
        print(f"  CMD: {cmd}")
        output = self.cli.run(cmd, timeout=timeout)
        self.results.append({"label": label, "cmd": cmd, "output": output})
        # Kurze Ausgabe-Vorschau
        preview = output[:120].replace("\n", " | ") if output else "(keine Ausgabe)"
        print(f"  OUT: {preview}")
        return output

    # ------------------------------------------------------------------
    def section(self, title: str):
        sep = "=" * 70
        print(f"\n{sep}")
        print(f"  {title}")
        print(sep)

    # ------------------------------------------------------------------
    def test_prerequisites(self):
        self.section("0. VORAUSSETZUNGEN")
        self._run("uname",          "uname -a")
        self._run("hostname",       "hostname")
        self._run("ip link",        f"ip link show {self.iface}")
        self._run("ethtool vorhanden", "which ethtool || echo 'NOT FOUND'")
        self._run("phytool vorhanden", "which phytool || echo 'NOT FOUND'")
        self._run("ethtool version",   "ethtool --version 2>&1 || ethtool -v 2>&1 || echo 'kein --version'")

    # ------------------------------------------------------------------
    def test_ethtool_basic(self):
        self.section("1. ETHTOOL — Basis-Informationen")
        self._run("ethtool eth0",          f"ethtool {self.iface} 2>&1")
        self._run("ethtool -i eth0",       f"ethtool -i {self.iface} 2>&1")
        self._run("ethtool --show-phys",   f"ethtool --show-phys {self.iface} 2>&1")
        self._run("ethtool -j eth0",       f"ethtool -j {self.iface} 2>&1")

    # ------------------------------------------------------------------
    def test_ethtool_plca(self):
        self.section("2. ETHTOOL — PLCA (10BASE-T1S Multi-Drop)")
        self._run("plca-cfg",    f"ethtool --get-plca-cfg {self.iface} 2>&1")
        self._run("plca-status", f"ethtool --get-plca-status {self.iface} 2>&1")

    # ------------------------------------------------------------------
    def test_ethtool_stats(self):
        self.section("3. ETHTOOL — Statistiken")
        self._run("ethtool -S",              f"ethtool -S {self.iface} 2>&1", timeout=8)
        self._run("ethtool --phy-stats",     f"ethtool --phy-statistics {self.iface} 2>&1")
        self._run("ethtool -S --all-groups", f"ethtool -S {self.iface} --all-groups 2>&1", timeout=8)

    # ------------------------------------------------------------------
    def test_ethtool_features(self):
        self.section("4. ETHTOOL — Features und Offloads")
        self._run("ethtool -k",  f"ethtool -k {self.iface} 2>&1")
        self._run("ethtool -T",  f"ethtool -T {self.iface} 2>&1")
        self._run("ethtool -P",  f"ethtool -P {self.iface} 2>&1")

    # ------------------------------------------------------------------
    def test_ethtool_link_mgmt(self):
        self.section("5. ETHTOOL — Link-Management (erwartet: nicht unterstützt)")
        self._run("ethtool -a",  f"ethtool -a {self.iface} 2>&1")
        self._run("ethtool -c",  f"ethtool -c {self.iface} 2>&1")
        self._run("ethtool -g",  f"ethtool -g {self.iface} 2>&1")
        self._run("ethtool -l",  f"ethtool -l {self.iface} 2>&1")

    # ------------------------------------------------------------------
    def test_ethtool_registers(self):
        self.section("6. ETHTOOL — Register-Dump")
        self._run("ethtool -d",  f"ethtool -d {self.iface} 2>&1", timeout=10)
        self._run("ethtool -e",  f"ethtool -e {self.iface} 2>&1", timeout=10)

    # ------------------------------------------------------------------
    def test_ethtool_diag(self):
        self.section("7. ETHTOOL — Diagnose-Tests")
        self._run("ethtool -t online",   f"ethtool -t {self.iface} online 2>&1", timeout=15)
        self._run("show-priv-flags",     f"ethtool --show-priv-flags {self.iface} 2>&1")
        self._run("show-fec",            f"ethtool --show-fec {self.iface} 2>&1")

    # ------------------------------------------------------------------
    def test_phytool_c22(self):
        self.section("8. PHYTOOL — Clause 22 Standard-Register (0-7)")
        # print-Kommando gibt alle auf einmal
        self._run("phytool print",  f"phytool print {self.iface}/0 2>&1", timeout=8)
        # Einzelne Standard-Register
        regs = {
            0: "BCR (Basic Control)",
            1: "BSR (Basic Status)",
            2: "PHY ID 1",
            3: "PHY ID 2",
            4: "ANAR (Auto-Neg Adv.)",
            5: "ANLPAR (Link Partner)",
            6: "ANER (Auto-Neg Exp.)",
            7: "ANNPR (Next Page)",
        }
        for reg, name in regs.items():
            self._run(f"C22 reg {reg} {name}", f"phytool read {self.iface}/0/{reg} 2>&1")

    # ------------------------------------------------------------------
    def test_phytool_c22_vendor(self):
        self.section("9. PHYTOOL — Clause 22 Vendor-spezifische Register (16-31)")
        for reg in range(16, 32):
            self._run(f"C22 reg {reg}", f"phytool read {self.iface}/0/{reg} 2>&1")

    # ------------------------------------------------------------------
    def test_phytool_c45(self):
        self.section("10. PHYTOOL — Clause 45 (erweiterte Adressierung)")
        # MMD 1 = PMA/PMD, MMD 3 = PCS, MMD 7 = AN
        c45_regs = [
            (1, 0,  "PMA/PMD Control 1"),
            (1, 1,  "PMA/PMD Status 1"),
            (1, 2,  "PMA/PMD Device ID MSW"),
            (1, 3,  "PMA/PMD Device ID LSW"),
            (3, 0,  "PCS Control 1"),
            (3, 1,  "PCS Status 1"),
            (7, 0,  "AN Control"),
            (7, 1,  "AN Status"),
            (31, 0, "Vendor MMD 31 Reg 0"),
            (31, 1, "Vendor MMD 31 Reg 1"),
        ]
        for mmd, reg, name in c45_regs:
            self._run(f"C45 MMD{mmd} reg{reg} {name}",
                      f"phytool read {self.iface}/0:{mmd}/{reg} 2>&1")

    # ------------------------------------------------------------------
    def test_network_status(self):
        self.section("11. NETZWERK-STATUS (Kontext)")
        self._run("ip addr",      f"ip addr show {self.iface} 2>&1")
        self._run("ip route",     "ip route show 2>&1")
        self._run("cat /proc/net/dev", "cat /proc/net/dev 2>&1")
        self._run("dmesg lan8",   "dmesg | grep -i 'lan8\\|t1s\\|oa tc6\\|plca' | tail -20 2>&1", timeout=8)

    # ------------------------------------------------------------------
    def print_summary(self):
        sep = "=" * 70
        print(f"\n\n{sep}")
        print("  ZUSAMMENFASSUNG")
        print(sep)
        print(f"  Datum:      {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"  Interface:  {self.iface}")
        print(f"  Kommandos:  {len(self.results)}")
        print()

        ok = [r for r in self.results if r["output"] and "NOT FOUND" not in r["output"]
              and "Operation not supported" not in r["output"]
              and "No such" not in r["output"]]
        fail = [r for r in self.results if not r["output"]
                or "Operation not supported" in r["output"]
                or "No such" in r["output"]
                or "NOT FOUND" in r["output"]]

        print(f"  ✅ Vermutlich OK ({len(ok)}):")
        for r in ok:
            print(f"     {r['label']}")

        print(f"\n  ❌ Nicht unterstützt / leer ({len(fail)}):")
        for r in fail:
            print(f"     {r['label']}")

        print(f"\n{sep}")
        print("  VOLLSTÄNDIGE AUSGABE (zum Kopieren für Analyse)")
        print(sep)
        for r in self.results:
            print(f"\n--- [{r['label']}] ---")
            print(f"CMD: {r['cmd']}")
            print(f"OUT:\n{r['output']}")
        print(f"\n{sep}")
        print("  ENDE DER AUSGABE")
        print(sep)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    port     = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUDRATE

    print("=" * 70)
    print("  ethtool / phytool Tester für LAN865X")
    print(f"  Port: {port}  Baudrate: {baudrate}")
    print(f"  Login: {LOGIN_USER} / {LOGIN_PASS}")
    print("=" * 70)

    cli = SerialCLI(port, baudrate)

    if not cli.connect():
        print("ABBRUCH: Serielle Verbindung fehlgeschlagen.")
        sys.exit(1)

    try:
        if not cli.login():
            print("ABBRUCH: Login fehlgeschlagen.")
            sys.exit(1)

        tester = EthtoolPhytoolTester(cli)

        tester.test_prerequisites()
        tester.test_ethtool_basic()
        tester.test_ethtool_plca()
        tester.test_ethtool_stats()
        tester.test_ethtool_features()
        tester.test_ethtool_link_mgmt()
        tester.test_ethtool_registers()
        tester.test_ethtool_diag()
        tester.test_phytool_c22()
        tester.test_phytool_c22_vendor()
        tester.test_phytool_c45()
        tester.test_network_status()

        tester.print_summary()

    except KeyboardInterrupt:
        print("\n[ABBRUCH] Durch Benutzer unterbrochen")
    finally:
        cli.disconnect()


if __name__ == "__main__":
    main()
