#!/usr/bin/env python3
"""
debugfs_impact_test.py
======================
Prüft ob Register-Zugriffe via debugfs bei Link-UP das System beeinflussen.

Im Gegensatz zu register_impact_test.py (lan_read/ioctl):
  - kein eth0 down/up
  - Register werden bei LAUFENDEM Link via debugfs gelesen
  - Methode: echo 'ADDR' > .../register  →  dmesg | grep 'Register read'

Ablauf:
  Phase 1:  iperf Baseline        (MCU→MPU UDP  +  MPU→MCU TCP)
  Phase 2:  debugfs Register-Dump (Link bleibt UP!)
  Phase 3:  iperf Nachher         (MCU→MPU UDP  +  MPU→MCU TCP)
  Phase 4:  Vergleich Vorher vs. Nachher

COM8 = MCU (RTOS CLI, kein Login)
COM9 = MPU (Linux, root/microchip)

Verwendung:
    python debugfs_impact_test.py
    python debugfs_impact_test.py --mcu-port COM8 --mpu-port COM9
    python debugfs_impact_test.py --duration 15
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
MCU_IP      = "192.168.0.200"
MPU_IP      = "192.168.0.5"
INTERFACE   = "eth0"
LOGIN_USER  = "root"
LOGIN_PASS  = "microchip"
IPERF_DUR   = 12

DEBUGFS_REG = "/sys/kernel/debug/lan865x_eth0/register"

# MMS0 Register (identisch zu register_impact_test.py)
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
# SerialCLI
# ---------------------------------------------------------------------------
try:
    import serial
except ImportError:
    print("FEHLER: pyserial nicht installiert. Bitte: pip install pyserial")
    sys.exit(1)


class SerialCLI:
    PROMPTS         = ["# ", "$ ", "~# ", "~$ ", "root@"]
    _FLUSH_PATTERNS = ["assword", "login:", "Login:", "# ", "$ ", ":~#", ":~$", "> "]

    def __init__(self, name: str, port: str, baudrate: int = 115200):
        self.name     = name
        self.port     = port
        self.baudrate = baudrate
        self.ser      = None
        self._q: queue.Queue = queue.Queue()
        self._stop    = threading.Event()
        self._thread  = None

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

    def login_mpu(self, timeout: float = 20.0) -> bool:
        self._send("\r\n")
        time.sleep(0.5)
        found, lines = self._wait_for(
            self.PROMPTS + ["login:", "Login:", "assword:"], timeout)
        if any(p in l for l in lines for p in self.PROMPTS):
            print(f"[{self.name}] Eingeloggt")
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
        self._send("\r\n")
        ok, _ = self._wait_for(["> ", ">"], timeout)
        print(f"[{self.name}] MCU RTOS bereit")
        return True

    def run(self, cmd: str, timeout: float = 10.0, prompts: list = None) -> str:
        p = prompts or self.PROMPTS
        self._flush_q()
        self._send(cmd + "\r\n")
        _, lines = self._wait_for(p, timeout=timeout)
        if lines and cmd.strip() in lines[0]:
            lines = lines[1:]
        if lines and any(p2 in lines[-1] for p2 in p):
            lines = lines[:-1]
        return "\n".join(lines).strip()

    def run_mcu(self, cmd: str, timeout: float = 10.0) -> str:
        return self.run(cmd, timeout=timeout, prompts=["> ", ">"])


# ---------------------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------------------
def sep(title: str):
    print(f"\n{'='*65}")
    print(f"  {title}")
    print(f"{'='*65}")


def parse_loss(text: str) -> float | None:
    m = re.search(r'\(\s*(\d+(?:\.\d+)?)\s*%\s*\)', text)
    if m:
        return float(m.group(1))
    m2 = re.search(r'(\d+(?:\.\d+)?)\s*%', text)
    if m2:
        return float(m2.group(1))
    return None


def parse_bandwidth(text: str) -> str:
    m = re.search(r'([\d.]+\s*[KMG]?bits/sec)', text, re.IGNORECASE)
    return m.group(1) if m else "?"


# ---------------------------------------------------------------------------
# Haupt-Test-Klasse
# ---------------------------------------------------------------------------
class DebugfsImpactTest:
    def __init__(self, mcu: SerialCLI, mpu: SerialCLI, duration: int):
        self.mcu      = mcu
        self.mpu      = mpu
        self.duration = duration
        self.results  = {
            "before":    {"mcu_to_mpu": None, "mpu_to_mcu": None},
            "after":     {"mcu_to_mpu": None, "mpu_to_mcu": None},
            "registers": {},
        }

    # ------------------------------------------------------------------
    def _mpu_killall_iperf(self):
        self.mpu.run("killall iperf iperf3 2>/dev/null; true", timeout=4)
        time.sleep(0.5)

    def _mcu_fwd_off(self):
        self.mcu.run_mcu("fwd 0", timeout=5)
        time.sleep(0.3)

    def _mcu_killall_iperf(self):
        self.mcu.run_mcu("iperfk", timeout=5)
        time.sleep(0.5)

    # ------------------------------------------------------------------
    def _kick_plca(self, label: str = ""):
        tag = f"[{label}] " if label else ""
        print(f"  {tag}[MCU] PLCA Kick-Ping MCU→{MPU_IP}...")
        self.mcu.run_mcu(f"ping {MPU_IP}", timeout=12)
        time.sleep(1)
        self.mpu.run(
            f"ping -c 1 -W 2 -I {INTERFACE} {MCU_IP} 2>/dev/null; true",
            timeout=6)
        time.sleep(0.5)

    # ------------------------------------------------------------------
    def iperf_mcu_to_mpu(self, label: str) -> dict:
        sep(f"iperf MCU → MPU UDP  [{label}]")
        self._mpu_killall_iperf()
        self._mcu_fwd_off()
        self._kick_plca(label)

        print(f"  [MPU] iperf UDP Server starten...")
        self.mpu.run(
            f"iperf -s -u -i 1 -B {MPU_IP} > /tmp/iperf_mcu2mpu_{label}.log 2>&1 &",
            timeout=4)
        time.sleep(2)

        self._mcu_killall_iperf()
        print(f"  [MCU] iperf UDP Client → {MPU_IP} ...")
        mcu_out = self.mcu.run_mcu(
            f"iperf -u -c {MPU_IP}",
            timeout=float(self.duration + 20))
        print(f"  [MCU] {mcu_out[:300]}")

        time.sleep(max(0, self.duration - 5))
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
        sep(f"iperf MPU → MCU TCP  [{label}]")
        self._mpu_killall_iperf()
        self._mcu_fwd_off()
        self.mpu.run(
            f"ping -c 1 -W 2 -I {INTERFACE} {MCU_IP} 2>/dev/null; true",
            timeout=6)
        time.sleep(0.5)

        self._mcu_killall_iperf()
        print(f"  [MCU] iperf Server starten...")
        self.mcu.run_mcu("iperf -s", timeout=5)
        time.sleep(1)

        print(f"  [MPU] iperf TCP Client → {MCU_IP} ...")
        mpu_out = self.mpu.run(
            f"iperf -c {MCU_IP} -t {self.duration} -B {MPU_IP} 2>&1",
            timeout=float(self.duration + 20))
        print(f"  [MPU] {mpu_out[:400]}")

        self._mcu_killall_iperf()
        self._mcu_fwd_off()

        bw     = parse_bandwidth(mpu_out)
        loss   = parse_loss(mpu_out)
        result = {"client_out": mpu_out, "server_out": "",
                  "loss_pct": loss, "bandwidth": bw}
        print(f"\n  --> Bandbreite: {bw}  Verlust: {loss if loss is not None else 'n/a (TCP)'}")
        return result

    # ------------------------------------------------------------------
    def read_registers_debugfs(self):
        """Register via debugfs lesen — Link bleibt UP!"""
        sep("REGISTER-ZUGRIFF via debugfs (Link bleibt UP)")

        # debugfs verfügbar prüfen
        chk = self.mpu.run(f"ls {DEBUGFS_REG} 2>&1", timeout=5)
        if "No such file" in chk or "cannot access" in chk:
            print(f"  [MPU] FEHLER: debugfs nicht verfügbar: {chk}")
            print(f"  [MPU] Ist debugfs gemountet? → mount -t debugfs debugfs /sys/kernel/debug")
            return

        # Link-Status vor Reads prüfen
        st = self.mpu.run(f"ip link show {INTERFACE} 2>&1", timeout=5)
        link_up = "LOWER_UP" in st or ("UP" in st and "DOWN" not in st)
        print(f"  [MPU] Link-Status vor Reads: {'UP ✓' if link_up else 'DOWN ✗'}")
        if not link_up:
            print(f"  [MPU] WARNUNG: Link bereits DOWN — Test nicht aussagekräftig!")

        print(f"\n  [MPU] Lese {len(MMS0_REGISTERS)} Register via debugfs...")
        t0 = time.time()
        errors = 0

        for addr, desc in MMS0_REGISTERS:
            # Adresse schreiben
            wr_out = self.mpu.run(
                f"echo '0x{addr:08X}' > {DEBUGFS_REG} 2>&1",
                timeout=5)

            # Wert aus dmesg lesen (Kernel loggt "Register read: 0xADDR = 0xVALUE")
            dmesg_out = self.mpu.run(
                f"dmesg | tail -10 | grep -i 'Register read' | tail -1",
                timeout=5)

            # Wert parsen
            m = re.search(
                rf'Register read[:\s]+0x{addr:08X}\s*=\s*(0x[0-9A-Fa-f]+)',
                dmesg_out, re.IGNORECASE)
            if m:
                val_str = m.group(1)
                ok = True
            elif wr_out and ("error" in wr_out.lower() or "permission" in wr_out.lower()):
                val_str = f"FEHLER: {wr_out.strip()[:40]}"
                ok = False
                errors += 1
            else:
                # Fallback: rohe dmesg-Zeile
                val_str = dmesg_out.strip()[-40:] if dmesg_out.strip() else "(kein dmesg-Eintrag)"
                ok = bool(dmesg_out.strip())
                if not ok:
                    errors += 1

            self.results["registers"][f"0x{addr:08X}"] = val_str
            mark = "✓" if ok else "✗ FEHLER"
            print(f"  {mark}  0x{addr:08X}  {desc[:40]:<42}  {val_str[:30]}")

        elapsed = time.time() - t0
        n = len(MMS0_REGISTERS)
        print(f"\n  [PERF] {n} Reads in {elapsed:.2f}s → Ø {elapsed/n*1000:.1f} ms/Read")
        print(f"  [INFO] Hinweis: debugfs-Methode ist langsamer als ioctl (~500ms/Read erwartet)")

        # Link-Status NACH Reads prüfen — hat sich was geändert?
        st2 = self.mpu.run(f"ip link show {INTERFACE} 2>&1", timeout=5)
        link_after = "LOWER_UP" in st2 or ("UP" in st2 and "DOWN" not in st2)
        print(f"  [MPU] Link-Status nach Reads:  {'UP ✓' if link_after else 'DOWN ✗ ← PROBLEM!'}")

        if link_up and not link_after:
            print(f"  [MPU] ❌ KRITISCH: Link nach debugfs-Reads ausgefallen!")
        elif link_up and link_after:
            print(f"  [MPU] ✓ Link blieb während aller debugfs-Reads stabil")

        if errors > 0:
            print(f"  [MPU] ⚠ {errors} Register konnten nicht gelesen werden")

    # ------------------------------------------------------------------
    def run(self):
        sep("PHASE 1: IPERF BASELINE (vor Register-Zugriff)")
        self.results["before"]["mcu_to_mpu"] = self.iperf_mcu_to_mpu("before")
        time.sleep(3)
        self.results["before"]["mpu_to_mcu"] = self.iperf_mpu_to_mcu("before")
        time.sleep(3)

        sep("PHASE 2: REGISTER-ZUGRIFF via debugfs (Link bleibt UP)")
        self.read_registers_debugfs()
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

        print(f"\n  {'Richtung':<30} {'Vorher':<20} {'Nachher':<20} {'Δ'}")
        print(f"  {'-'*30} {'-'*20} {'-'*20} {'-'*12}")

        bl = b_m2m.get("loss_pct")
        al = a_m2m.get("loss_pct")
        bw_b  = b_m2m.get("bandwidth", "?")
        bw_a  = a_m2m.get("bandwidth", "?")
        delta = f"{al - bl:+.1f}%" if (bl is not None and al is not None) else "?"
        print(f"  {'MCU→MPU UDP':<30} "
              f"{f'{bl}% ({bw_b})':<20} "
              f"{f'{al}% ({bw_a})':<20} {delta}")

        bl2  = b_mpu2mc.get("loss_pct")
        al2  = a_mpu2mc.get("loss_pct")
        bw_b2 = b_mpu2mc.get("bandwidth", "?")
        bw_a2 = a_mpu2mc.get("bandwidth", "?")
        delta2 = f"{al2 - bl2:+.1f}%" if (bl2 is not None and al2 is not None) else "n/a (TCP)"
        print(f"  {'MPU→MCU TCP':<30} "
              f"{f'{bw_b2}':<20} "
              f"{f'{bw_a2}':<20} {delta2}")

        print()
        problems = []
        if bl is not None and al is not None and (al - bl) > 10:
            problems.append(
                f"MCU→MPU Paketverlust nach debugfs-Reads um {al - bl:.1f}% gestiegen!")

        def parse_mbit(s):
            m = re.search(r'([\d.]+)\s*([KMG]?)bits', s, re.IGNORECASE)
            if not m:
                return None
            v = float(m.group(1))
            u = m.group(2).upper()
            return v * {"K": 0.001, "M": 1, "G": 1000, "": 0.001}.get(u, 1)

        if bw_b2 != "?" and bw_a2 != "?":
            mb_b = parse_mbit(bw_b2)
            mb_a = parse_mbit(bw_a2)
            if mb_b and mb_a and mb_b > 0 and (mb_b - mb_a) / mb_b > 0.2:
                problems.append(
                    f"MPU→MCU Bandbreite nach debugfs-Reads um >20% gesunken!")

        if not problems:
            print("  ✅ ERGEBNIS: debugfs Register-Zugriff bei Link UP hat KEINEN Einfluss")
            print("             auf die nachfolgende Netzwerk-Performance.")
            print("             debugfs ist SICHER verwendbar (auch bei aktivem Link).")
        else:
            print("  ❌ WARNUNG: Mögliche Auswirkungen erkannt:")
            for p in problems:
                print(f"     - {p}")

        # Register-Dump
        print(f"\n  REGISTER-DUMP (debugfs):")
        print(f"  {'-'*65}")
        err_count = 0
        for addr_str, val in self.results["registers"].items():
            err = "fehler" in val.lower() or "error" in val.lower() or "kein" in val.lower()
            if err:
                err_count += 1
            mark = "✗" if err else "✓"
            print(f"  {mark} {addr_str}: {val}")

        if err_count == 0:
            print(f"\n  ✅ Alle {len(self.results['registers'])} Register via debugfs gelesen")
        else:
            print(f"\n  ❌ {err_count} von {len(self.results['registers'])} Registern mit Fehler")

        print(f"\n  Datum: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def parse_args():
    p = argparse.ArgumentParser(
        description="Debugfs Impact Test — Register-Reads bei Link UP via debugfs")
    p.add_argument("--mcu-port", default="COM8")
    p.add_argument("--mpu-port", default="COM9")
    p.add_argument("--baudrate", type=int, default=115200)
    p.add_argument("--duration", type=int, default=IPERF_DUR,
                   help=f"iperf Testdauer in Sekunden (default: {IPERF_DUR})")
    return p.parse_args()


def main():
    args = parse_args()

    print("=" * 65)
    print("  Debugfs Impact Test")
    print("  Prüft ob debugfs-Reads bei Link-UP das System beeinflusst")
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

        tester = DebugfsImpactTest(mcu, mpu, args.duration)
        tester.run()

    except KeyboardInterrupt:
        print("\n[ABBRUCH] Durch Benutzer unterbrochen")
    finally:
        mcu.disconnect()
        mpu.disconnect()


if __name__ == "__main__":
    main()
