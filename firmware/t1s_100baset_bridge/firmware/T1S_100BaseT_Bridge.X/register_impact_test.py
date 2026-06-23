#!/usr/bin/env python3
"""
register_impact_test.py
=======================
Prüft ob Register-Zugriffe (lan_read) bei Link-DOWN das System beeinflussen.

Ablauf:
  Phase 1:  iperf Baseline   (MCU→MPU UDP  +  MPU→MCU TCP)
  Phase 2:  eth0 down  →  MMS0 Register-Dump  →  eth0 up
  Phase 3:  iperf Nachher    (MCU→MPU UDP  +  MPU→MCU TCP)
  Phase 4:  Vergleich  Vorher vs. Nachher

COM8 = MCU (RTOS CLI, kein Login)
COM9 = MPU (Linux, root/microchip)

Verwendung:
    python register_impact_test.py
    python register_impact_test.py --mcu-port COM8 --mpu-port COM9
    python register_impact_test.py --duration 15
"""

import argparse
import queue
import re
import sys
import threading
import time
from datetime import datetime


# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
MCU_IP       = "192.168.0.200"
MPU_IP       = "192.168.0.5"
INTERFACE    = "eth0"
LOGIN_USER   = "root"
LOGIN_PASS   = "microchip"
IPERF_DUR    = 12          # Sekunden pro iperf-Test

# MMS0 Register die gelesen werden (Open Alliance TC6 + MAC)
MMS0_REGISTERS = [
    (0x00000000, "OA_ID           (Open Alliance ID, erwartet 0x11)"),
    (0x00000001, "OA_PHYID        (PHY ID)"),
    (0x00000002, "OA_STDCAP       (Standard Capabilities)"),
    (0x00000004, "OA_CONFIG0      (Konfiguration)"),
    (0x00000008, "OA_STATUS0      (Status: RESETC, HDRE, LOFE, RXBOE...)"),
    (0x00000009, "OA_STATUS1      (Status1)"),
    (0x0000000B, "OA_BUFSTS       (TX/RX Buffer Status)"),
    (0x0000000C, "OA_IMASK0       (Interrupt Mask 0)"),
    (0x00800004, "T1S_STS0        (T1S Link Status)"),
    (0x00800100, "T1S_SQI         (Signal Quality Index)"),
    (0x00800300, "PLCA_CTRL0      (PLCA Enable + Coordinator)"),
    (0x00800302, "PLCA_CTRL1      (Node Count + Node ID)"),
    (0x00800304, "PLCA_STATUS0    (PLCA Status)"),
    (0x00800306, "PLCA_STATUS1    (TX Opportunity Timer)"),
    (0x00010000, "MAC_NET_CTL     (TX/RX Enable, erwartet 0x0C)"),
    (0x00010001, "MAC_NET_CFG     (Network Config)"),
    (0x00010022, "MAC_L_SADDR1   (MAC Addr [31:0])"),
    (0x00010023, "MAC_H_SADDR1   (MAC Addr [47:32])"),
    (0x00010077, "MAC_TSU_INCR   (TSU Timer, erwartet 0x28)"),
    (0x00020000, "RX_GOOD_FRAMES  (Empfangene gute Frames)"),
    (0x00020004, "RX_BAD_FRAMES   (Empfangene fehlerh. Frames)"),
]


# ---------------------------------------------------------------------------
# SerialCLI — Thread-basiert, robust (identisch zu ioctl_link_down_test.py)
# ---------------------------------------------------------------------------
try:
    import serial
except ImportError:
    print("FEHLER: pyserial nicht installiert. Bitte: pip install pyserial")
    sys.exit(1)


class SerialCLI:
    """Thread-basierter serieller Zugriff mit Queue."""

    PROMPTS         = ["# ", "$ ", "~# ", "~$ ", "root@"]
    _FLUSH_PATTERNS = ["assword", "login:", "Login:", "# ", "$ ", ":~#", ":~$", "> "]

    def __init__(self, name: str, port: str, baudrate: int = 115200):
        self.name      = name
        self.port      = port
        self.baudrate  = baudrate
        self.ser       = None
        self._q: queue.Queue = queue.Queue()
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
            self._stop.clear()
            self._thread = threading.Thread(target=self._reader, daemon=True)
            self._thread.start()
            print(f"[{self.name}] Verbunden: {self.port} @ {self.baudrate}")
            return True
        except serial.SerialException as e:
            print(f"[{self.name}] FEHLER: {e}")
            return False

    def disconnect(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self.ser and self.ser.is_open:
            self.ser.close()

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
                        self._q.put(line.rstrip("\r"))
                    # Unvollständige Zeile flushen wenn Prompt enthalten
                    if buf and any(p in buf for p in self._FLUSH_PATTERNS):
                        self._q.put(buf.rstrip("\r"))
                        buf = ""
                else:
                    time.sleep(0.02)
            except Exception:
                time.sleep(0.1)

    def _send(self, text: str):
        if self.ser and self.ser.is_open:
            self.ser.write(text.encode("utf-8"))
            self.ser.flush()

    def _flush_q(self):
        while not self._q.empty():
            try:
                self._q.get_nowait()
            except queue.Empty:
                break

    def _wait_for(self, patterns: list, timeout: float):
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self._q.get(timeout=0.1)
                lines.append(line)
                if any(p in line for p in patterns):
                    return True, lines
            except queue.Empty:
                pass
        return False, lines

    # ------------------------------------------------------------------
    def login_mpu(self, timeout: float = 20.0) -> bool:
        """Login für MPU (Linux root/microchip)."""
        self._send("\r\n")
        time.sleep(0.5)
        found, lines = self._wait_for(
            self.PROMPTS + ["login:", "Login:", "assword:"], timeout)

        if any(p in l for l in lines for p in self.PROMPTS):
            print(f"[{self.name}] Bereits eingeloggt")
            return True
        if any("assword" in l for l in lines):
            self._send(LOGIN_PASS + "\r\n")
            ok, _ = self._wait_for(self.PROMPTS, timeout=8)
            return ok
        if any("login" in l.lower() for l in lines):
            self._send(LOGIN_USER + "\r\n")
            time.sleep(0.5)
            ok2, _ = self._wait_for(["assword"], timeout=8)
            if ok2:
                self._send(LOGIN_PASS + "\r\n")
                ok3, _ = self._wait_for(self.PROMPTS, timeout=10)
                if ok3:
                    print(f"[{self.name}] Eingeloggt")
                    return True
        print(f"[{self.name}] Login fehlgeschlagen")
        return False

    def sync_mcu(self, timeout: float = 5.0) -> bool:
        """Prompt-Sync für MCU RTOS (Prompt: '> ')."""
        self._send("\r\n")
        ok, lines = self._wait_for(["> ", ">"], timeout)
        if ok:
            print(f"[{self.name}] MCU RTOS bereit")
            return True
        print(f"[{self.name}] MCU Prompt nicht gefunden (Empfangen: {lines[-3:]})")
        return True  # weiter versuchen

    def run(self, cmd: str, timeout: float = 10.0, prompts: list = None) -> str:
        """Kommando senden, auf Prompt warten, Ausgabe zurückgeben."""
        p = prompts or self.PROMPTS
        self._flush_q()
        self._send(cmd + "\r\n")
        _, lines = self._wait_for(p, timeout=timeout)
        # Echo-Zeile entfernen
        if lines and cmd.strip() in lines[0]:
            lines = lines[1:]
        # Prompt-Zeile entfernen
        if lines and any(p2 in lines[-1] for p2 in p):
            lines = lines[:-1]
        return "\n".join(lines).strip()

    def run_mcu(self, cmd: str, timeout: float = 10.0) -> str:
        """Kommando an MCU RTOS senden (Prompt '> ')."""
        return self.run(cmd, timeout=timeout, prompts=["> ", ">"])


# ---------------------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------------------
def sep(title: str):
    print(f"\n{'='*65}")
    print(f"  {title}")
    print(f"{'='*65}")


def parse_loss(text: str) -> float | None:
    """Extrahiert Paketverlust in % aus iperf-Ausgabe."""
    # MCU RTOS Format:  "850/1000 (85%)"  oder  "(85%)"
    m = re.search(r'\(\s*(\d+(?:\.\d+)?)\s*%\s*\)', text)
    if m:
        return float(m.group(1))
    # Linux iperf Server: "850 datagrams received out-of-order" etc.
    m2 = re.search(r'(\d+(?:\.\d+)?)\s*%', text)
    if m2:
        return float(m2.group(1))
    return None


def parse_bandwidth(text: str) -> str:
    """Extrahiert Bandbreite aus iperf-Ausgabe."""
    m = re.search(r'([\d.]+\s*[KMG]?bits/sec)', text, re.IGNORECASE)
    return m.group(1) if m else "?"


# ---------------------------------------------------------------------------
# Haupt-Test-Klasse
# ---------------------------------------------------------------------------
class RegisterImpactTest:
    def __init__(self, mcu: SerialCLI, mpu: SerialCLI, duration: int):
        self.mcu      = mcu
        self.mpu      = mpu
        self.duration = duration
        self.results  = {
            "before": {"mcu_to_mpu": None, "mpu_to_mcu": None},
            "after":  {"mcu_to_mpu": None, "mpu_to_mcu": None},
            "registers": {},
        }

    # ------------------------------------------------------------------
    def _mpu_killall_iperf(self):
        self.mpu.run("killall iperf iperf3 2>/dev/null; true", timeout=4)
        time.sleep(0.5)

    def _mcu_fwd_off(self):
        """MCU Forwarding aus — bekannter Stable-State."""
        self.mcu.run_mcu("fwd 0", timeout=5)
        time.sleep(0.3)

    def _mcu_killall_iperf(self):
        """Laufende MCU iperf-Instanzen stoppen (Kommando: iperfk)."""
        self.mcu.run_mcu("iperfk", timeout=5)
        time.sleep(0.5)

    # ------------------------------------------------------------------
    def _kick_plca(self, label: str = ""):
        """MCU→MPU Ping damit MCU (Coordinator) PLCA-Bus aktiviert."""
        tag = f"[{label}] " if label else ""
        print(f"  {tag}[MCU] PLCA Kick-Ping MCU→{MPU_IP}...")
        self.mcu.run_mcu(f"ping {MPU_IP}", timeout=12)
        time.sleep(1)
        # MPU ARP für MCU auffrischen
        self.mpu.run(f"ping -c 1 -W 2 -I {INTERFACE} {MCU_IP} 2>/dev/null; true",
                     timeout=6)
        time.sleep(0.5)

    def iperf_mcu_to_mpu(self, label: str) -> dict:
        """MCU → MPU UDP (MCU=Client, MPU=Server)."""
        sep(f"iperf MCU → MPU UDP  [{label}]")
        self._mpu_killall_iperf()
        self._mcu_fwd_off()
        self._kick_plca(label)

        # MPU Server starten — explizit an eth0-IP binden
        print(f"  [MPU] iperf UDP Server starten...")
        self.mpu.run(
            f"iperf -s -u -i 1 -B {MPU_IP} > /tmp/iperf_mcu2mpu_{label}.log 2>&1 &",
            timeout=4)
        time.sleep(2)

        # MCU Client
        self._mcu_killall_iperf()
        print(f"  [MCU] iperf UDP Client → {MPU_IP} ...")
        mcu_out = self.mcu.run_mcu(
            f"iperf -u -c {MPU_IP}",
            timeout=float(self.duration + 20)
        )
        print(f"  [MCU] {mcu_out[:300]}")

        # Warten bis MCU iperf fertig
        time.sleep(max(0, self.duration - 5))

        # MPU Server-Log
        time.sleep(2)
        self._mpu_killall_iperf()
        srv_out = self.mpu.run(f"cat /tmp/iperf_mcu2mpu_{label}.log", timeout=5)
        print(f"  [MPU] Server-Log:\n{srv_out[:500]}")

        loss   = parse_loss(mcu_out) or parse_loss(srv_out)
        bw     = parse_bandwidth(mcu_out) or parse_bandwidth(srv_out)
        result = {"client_out": mcu_out, "server_out": srv_out,
                  "loss_pct": loss, "bandwidth": bw}
        print(f"\n  --> Paketverlust: {loss}%  Bandbreite: {bw}")
        return result

    # ------------------------------------------------------------------
    def iperf_mpu_to_mcu(self, label: str) -> dict:
        """MPU → MCU TCP (MPU=Client, MCU=Server)."""
        sep(f"iperf MPU → MCU TCP  [{label}]")
        self._mpu_killall_iperf()
        self._mcu_fwd_off()
        # Kein extra Kick nötig — iperf_mcu_to_mpu hat bereits gekickt
        # Aber ARP sicherstellen:
        self.mpu.run(
            f"ping -c 1 -W 2 -I {INTERFACE} {MCU_IP} 2>/dev/null; true",
            timeout=6)
        time.sleep(0.5)

        # MCU iperf Server starten
        self._mcu_killall_iperf()
        print(f"  [MCU] iperf Server starten...")
        self.mcu.run_mcu("iperf -s", timeout=5)
        time.sleep(1)

        # MPU Client — explizit an eth0-IP binden (verhindert Routing über eth1)
        print(f"  [MPU] iperf TCP Client → {MCU_IP} ...")
        mpu_out = self.mpu.run(
            f"iperf -c {MCU_IP} -t {self.duration} -B {MPU_IP} 2>&1",
            timeout=float(self.duration + 20)
        )
        print(f"  [MPU] {mpu_out[:400]}")

        # MCU stoppen
        self._mcu_killall_iperf()
        self._mcu_fwd_off()

        bw     = parse_bandwidth(mpu_out)
        # TCP hat keinen Paketverlust in diesem Sinne
        loss   = parse_loss(mpu_out)
        result = {"client_out": mpu_out, "server_out": "",
                  "loss_pct": loss, "bandwidth": bw}
        print(f"\n  --> Bandbreite: {bw}  Verlust: {loss if loss is not None else 'n/a (TCP)'}")
        return result

    # ------------------------------------------------------------------
    def read_mms0_registers(self, skip_regs: bool = False):
        """eth0 down → (optional) MMS0 Register lesen → eth0 up."""
        if skip_regs:
            sep("REGISTER-ZUGRIFF: eth0 DOWN → [Register übersprungen] → eth0 UP")
        else:
            sep("REGISTER-ZUGRIFF: eth0 DOWN → lan_read MMS0 → eth0 UP")

        # --- Link DOWN ---
        print(f"  [MPU] ip link set {INTERFACE} down")
        self.mpu.run(f"ip link set {INTERFACE} down", timeout=5)
        time.sleep(1)

        status = self.mpu.run(f"ip link show {INTERFACE} 2>&1", timeout=5)
        link_down_ok = "DOWN" in status
        print(f"  [MPU] Link-Status: {'DOWN ✓' if link_down_ok else f'UNKLAR: {status[:80]}'}")

        if skip_regs:
            print(f"\n  [INFO] Register-Reads übersprungen (--skip-regs).")
            print(f"         Warte 5s (entspricht ca. Dauer eines Register-Dumps)...")
            time.sleep(5)
        else:
            # --- Register lesen ---
            print(f"\n  [MPU] Lese {len(MMS0_REGISTERS)} MMS0/MAC Register...")
            t0 = time.time()

            for addr, desc in MMS0_REGISTERS:
                out = self.mpu.run(f"lan_read 0x{addr:08X} 2>&1", timeout=5)
                self.results["registers"][f"0x{addr:08X}"] = out.strip()
                ok = "error" not in out.lower() and "fault" not in out.lower()
                mark = "✓" if ok else "✗ FEHLER"
                print(f"  {mark}  0x{addr:08X}  {desc[:40]:<42}  {out.strip()[:30]}")

            elapsed = time.time() - t0
            n = len(MMS0_REGISTERS)
            print(f"\n  [PERF] {n} Reads in {elapsed:.2f}s → Ø {elapsed/n*1000:.1f} ms/Read")

        # --- Link UP ---
        print(f"\n  [MPU] ip link set {INTERFACE} up")
        self.mpu.run(f"ip link set {INTERFACE} up", timeout=5)

        # Warten bis Link UP + PLCA
        link_ok = False
        for attempt in range(10):
            time.sleep(1)
            st = self.mpu.run(f"ip link show {INTERFACE} 2>&1", timeout=5)
            if "LOWER_UP" in st or ("UP" in st and "DOWN" not in st):
                link_ok = True
                break
            print(f"  [MPU] Warte auf Link UP... ({attempt+1}/10)")

        if link_ok:
            print(f"  [MPU] Link UP ✓")
        else:
            print(f"  [MPU] WARNUNG: Link kam nicht zurück! Letzter Status: {st[:80]}")

        # IP-Adresse auf eth0 sicherstellen (nach down/up kann sie verloren gehen)
        ip_info = self.mpu.run(f"ip addr show {INTERFACE} 2>&1", timeout=5)
        if MPU_IP not in ip_info:
            print(f"  [MPU] WARNUNG: {MPU_IP} fehlt auf {INTERFACE}! Neu zuweisen...")
            self.mpu.run(f"ip addr add {MPU_IP}/16 dev {INTERFACE} 2>&1", timeout=5)
            time.sleep(0.5)
        else:
            print(f"  [MPU] IP {MPU_IP} auf {INTERFACE} vorhanden ✓")

        # Route zu MCU explizit via eth0 sicherstellen
        self.mpu.run(
            f"ip route replace {MCU_IP}/32 dev {INTERFACE} src {MPU_IP} 2>&1",
            timeout=5)
        time.sleep(0.3)

        # PLCA prüfen
        plca = self.mpu.run(f"ethtool --get-plca-status {INTERFACE} 2>&1", timeout=5)
        print(f"  [MPU] PLCA nach Restore: {plca.strip()}")

        # ARP-Cache leeren — nach down/up sind alte Einträge ungültig
        print(f"  [MPU] ARP-Cache für {INTERFACE} leeren...")
        self.mpu.run(f"ip neigh flush dev {INTERFACE} 2>/dev/null; true", timeout=4)
        time.sleep(0.5)

        # MCU zuerst pingen lassen (MCU = PLCA-Coordinator/Node 0)
        # Der MCU muss PLCA initiieren — erst danach kann MPU senden
        print(f"  [MCU] MCU→MPU Kick-Ping (PLCA-Bus aktivieren)...")
        self.mcu.run_mcu(f"ping {MPU_IP}", timeout=15)
        time.sleep(1)

        # MPU → MCU Ping — explizit eth0 erzwingen (verhindert Routing über eth1)
        print(f"  [MPU] Ping MCU ({MCU_IP}) via {INTERFACE}...")
        ping_out = self.mpu.run(
            f"ping -c 4 -W 2 -I {INTERFACE} {MCU_IP} 2>&1", timeout=20)
        rx = re.search(r'(\d+) received', ping_out)
        if rx and int(rx.group(1)) > 0:
            print(f"  [MPU] Ping OK — {rx.group(1)}/4 Antworten ✓")
        else:
            print(f"  [MPU] Ping FEHLGESCHLAGEN — MCU nicht erreichbar!")
            print(f"         Ausgabe: {ping_out[:200]}")
            # Zweiter Versuch: Route explizit setzen
            print(f"  [MPU] Setze explizite Route via {INTERFACE}...")
            self.mpu.run(
                f"ip route replace {MCU_IP}/32 dev {INTERFACE} src {MPU_IP} 2>&1",
                timeout=5)
            time.sleep(1)
            ping_out2 = self.mpu.run(
                f"ping -c 3 -W 2 -I {INTERFACE} {MCU_IP} 2>&1", timeout=15)
            rx2 = re.search(r'(\d+) received', ping_out2)
            if rx2 and int(rx2.group(1)) > 0:
                print(f"  [MPU] Ping OK nach Route-Fix — {rx2.group(1)}/3 Antworten ✓")
            else:
                print(f"  [MPU] Ping weiterhin fehlgeschlagen: {ping_out2[:150]}")

        return link_ok

    # ------------------------------------------------------------------
    def run(self, skip_regs: bool = False):
        sep("PHASE 1: IPERF BASELINE (vor Register-Zugriff)")
        self.results["before"]["mcu_to_mpu"] = self.iperf_mcu_to_mpu("before")
        time.sleep(3)
        self.results["before"]["mpu_to_mcu"] = self.iperf_mpu_to_mcu("before")
        time.sleep(3)

        if skip_regs:
            sep("PHASE 2: ETH0 DOWN → [OHNE Register-Reads] → ETH0 UP")
        else:
            sep("PHASE 2: ETH0 DOWN → MMS0 REGISTER LESEN → ETH0 UP")
        link_ok = self.read_mms0_registers(skip_regs=skip_regs)
        if not link_ok:
            print("\n  *** WARNUNG: Link nach Register-Zugriff nicht wiederhergestellt! ***")
            print("  *** Power-Cycle notwendig?                                     ***")
        time.sleep(3)

        sep("PHASE 3: IPERF NACHHER (nach Register-Zugriff)")
        self.results["after"]["mcu_to_mpu"] = self.iperf_mcu_to_mpu("after")
        time.sleep(3)
        self.results["after"]["mpu_to_mcu"] = self.iperf_mpu_to_mcu("after")

        self.print_summary()

    # ------------------------------------------------------------------
    def print_summary(self):
        sep("PHASE 4: VERGLEICH VORHER vs. NACHHER")

        b_m2m    = self.results["before"]["mcu_to_mpu"]  or {}
        b_mpu2mc = self.results["before"]["mpu_to_mcu"]  or {}
        a_m2m    = self.results["after"]["mcu_to_mpu"]   or {}
        a_mpu2mc = self.results["after"]["mpu_to_mcu"]   or {}

        print(f"\n  {'Richtung':<30} {'Vorher':<20} {'Nachher':<20} {'Δ Verlust'}")
        print(f"  {'-'*30} {'-'*20} {'-'*20} {'-'*12}")

        # MCU → MPU UDP
        bl = b_m2m.get("loss_pct")
        al = a_m2m.get("loss_pct")
        bw_b = b_m2m.get("bandwidth", "?")
        bw_a = a_m2m.get("bandwidth", "?")
        delta = f"{al - bl:+.1f}%" if (bl is not None and al is not None) else "?"
        print(f"  {'MCU→MPU UDP':<30} "
              f"{f'{bl}% ({bw_b})':<20} "
              f"{f'{al}% ({bw_a})':<20} {delta}")

        # MPU → MCU TCP
        bl2 = b_mpu2mc.get("loss_pct")
        al2 = a_mpu2mc.get("loss_pct")
        bw_b2 = b_mpu2mc.get("bandwidth", "?")
        bw_a2 = a_mpu2mc.get("bandwidth", "?")
        delta2 = f"{al2 - bl2:+.1f}%" if (bl2 is not None and al2 is not None) else "n/a (TCP)"
        print(f"  {'MPU→MCU TCP':<30} "
              f"{f'{bw_b2}':<20} "
              f"{f'{bw_a2}':<20} {delta2}")

        # Bewertung
        print()
        problems = []
        if bl is not None and al is not None and (al - bl) > 10:
            problems.append(f"MCU→MPU Paketverlust nach Register-Zugriff um {al - bl:.1f}% gestiegen!")
        if bw_b2 != "?" and bw_a2 != "?":
            # grobe Bandbreitenprüfung
            def parse_mbit(s):
                m = re.search(r'([\d.]+)\s*([KMG]?)bits', s, re.IGNORECASE)
                if not m:
                    return None
                v = float(m.group(1))
                u = m.group(2).upper()
                return v * {"K": 0.001, "M": 1, "G": 1000, "": 0.001}.get(u, 1)
            mb_b = parse_mbit(bw_b2)
            mb_a = parse_mbit(bw_a2)
            if mb_b and mb_a and mb_b > 0 and (mb_b - mb_a) / mb_b > 0.2:
                problems.append(f"MPU→MCU Bandbreite nach Register-Zugriff um >20% gesunken!")

        if not problems:
            print("  ✅ ERGEBNIS: Register-Zugriff bei eth0 DOWN hat KEINEN Einfluss")
            print("             auf die nachfolgende Netzwerk-Performance.")
            print("             Option B ist SICHER verwendbar.")
        else:
            print("  ❌ WARNUNG: Mögliche Auswirkungen erkannt:")
            for p in problems:
                print(f"     - {p}")
            print("     Register-Dump bitte prüfen — manuell analysieren.")

        # Register-Dump
        print(f"\n  REGISTER-DUMP (MMS0):")
        print(f"  {'-'*65}")
        errors = 0
        for addr_str, val in self.results["registers"].items():
            err = "error" in val.lower() or "fault" in val.lower()
            if err:
                errors += 1
            mark = "✗" if err else "✓"
            print(f"  {mark} {addr_str}: {val}")
        if errors == 0:
            print(f"\n  ✅ Alle {len(self.results['registers'])} Register erfolgreich gelesen — keine Fehler")
        else:
            print(f"\n  ❌ {errors} von {len(self.results['registers'])} Registern mit Fehler")

        print(f"\n  Datum: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def parse_args():
    p = argparse.ArgumentParser(description="Register Impact Test — eth0 down/up + MMS0 reads")
    p.add_argument("--mcu-port", default="COM8")
    p.add_argument("--mpu-port", default="COM9")
    p.add_argument("--baudrate", type=int, default=115200)
    p.add_argument("--duration", type=int, default=IPERF_DUR,
                   help=f"iperf Testdauer in Sekunden (default: {IPERF_DUR})")
    p.add_argument("--skip-regs", action="store_true",
                   help="Phase 2 ohne Register-Reads — nur eth0 down/up (Isolations-Test)")
    return p.parse_args()


def main():
    args = parse_args()

    print("=" * 65)
    print("  Register Impact Test")
    print("  Prüft ob lan_read bei eth0-DOWN das System beeinflusst")
    print(f"  MCU: {args.mcu_port}  MPU: {args.mpu_port}  iperf: {args.duration}s")
    print("=" * 65)

    mcu = SerialCLI("MCU", args.mcu_port, args.baudrate)
    mpu = SerialCLI("MPU", args.mpu_port, args.baudrate)

    if not mcu.connect():
        print("ABBRUCH: MCU Verbindung fehlgeschlagen")
        sys.exit(1)
    if not mpu.connect():
        print("ABBRUCH: MPU Verbindung fehlgeschlagen")
        mcu.disconnect()
        sys.exit(1)

    try:
        print("\n[INIT] Synchronisiere Prompts...")
        mcu.sync_mcu(timeout=5)
        if not mpu.login_mpu(timeout=20):
            print("ABBRUCH: MPU Login fehlgeschlagen")
            sys.exit(1)

        tester = RegisterImpactTest(mcu, mpu, args.duration)
        if args.skip_regs:
            print("  [MODUS] --skip-regs: Register-Reads werden übersprungen (Isolations-Test)")
        tester.run(skip_regs=args.skip_regs)

    except KeyboardInterrupt:
        print("\n[ABBRUCH] Durch Benutzer unterbrochen")
    finally:
        mcu.disconnect()
        mpu.disconnect()


if __name__ == "__main__":
    main()
