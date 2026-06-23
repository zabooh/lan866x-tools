#!/usr/bin/env python3
"""
mpu_direct_access_proof.py
==========================
Nachweis des Direct-Register-Access-Issues auf der MPU (LAN865x Kernel-Treiber).

Testablauf (MPU = Client im &-Hintergrund, MCU = UDP-Server):
  A  BASELINE       iperf 2M/10s, keine Eingriffe                  -> ~0% Verlust
  B  LAN_READ_MPU   iperf + lan_read auf MPU alle 200ms             -> Link-Tod erwartet
  RECOVERY          eth0 bounce + Ping nach Case B                  -> Wiederherstellung?
  C  CONTROL        iperf + cat /proc/net/dev alle 200ms (kein SPI) -> ~0% Verlust

Kernunterschied zu mcu_direct_access_proof.py:
  - Injektion geht auf MPU-Serial (lan_read laeuft als Linux-Userspace-ioctl)
  - MPU-iperf laeuft im Hintergrund (&) damit Injektion gleichzeitig moeglich ist
  - Messung erfolgt von MCU-Serial (MCU-iperf-Server = Referenz, wird nie injiziert)
  - Erwartetes Schadensbild: Link-Tod (>40% Verlust) statt ~5% wie auf MCU-Seite
  - Nach Case B: eth0-Bounce-Wiederherstellungsversuch + Ping-Test

Hintergrund:
  lan_read auf der MPU ist User-Space-ioctl, der direkt auf den TC6/SPI-Bus
  zugreift - in direkter Konkurrenz zum aktiven LAN865x-Kernel-ISR (IRQ 37,
  spi0.0). Das korrupiert die TC6-State-Machine permanent.
  Empirisch bestaetigt 2026-03-18 in mpu_rx_diagnostic_test.py.
  Recovery (eth0 bounce) hilft laut bisherigen Beobachtungen NICHT.
  Nur Power-Cycle stellt den Link wieder her.

Verwendung:
  python mpu_direct_access_proof.py
  python mpu_direct_access_proof.py --verbose
  python mpu_direct_access_proof.py --aggressive --verbose
  python mpu_direct_access_proof.py --skip-control
  python mpu_direct_access_proof.py --skip-recovery --skip-control  # schnellster Modus
"""

from __future__ import annotations

import argparse
import json
import re
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

import serial


# ---------------------------------------------------------------------------
# Serial CLI  (identisch mit mcu_direct_access_proof.py)
# ---------------------------------------------------------------------------

@dataclass
class CommandResult:
    command: str
    output: str
    timed_out: bool


class SerialCLI:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 0.2):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser: Optional[serial.Serial] = None

    def open(self) -> None:
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.timeout,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )

    def close(self) -> None:
        if self.ser and self.ser.is_open:
            self.ser.close()

    def clear_input(self) -> None:
        if self.ser:
            self.ser.read_all()

    def write_line(self, line: str) -> None:
        if not self.ser:
            raise RuntimeError(f"Port {self.port} not open")
        self.ser.write((line + "\n").encode("utf-8", errors="ignore"))
        self.ser.flush()

    def write_ctrl_c(self) -> None:
        if self.ser:
            self.ser.write(b"\x03")
            self.ser.flush()

    def _looks_like_prompt(self, text: str) -> bool:
        lines = [ln.rstrip() for ln in text.splitlines() if ln.strip()]
        return bool(lines and re.search(r"[>#\$]\s*$", lines[-1]))

    def read_until_idle(
        self,
        overall_timeout: float,
        idle_timeout: float,
        stop_markers: Optional[List[str]] = None,
        stop_on_prompt: bool = False,
        live_label: str = "",
    ) -> CommandResult:
        if not self.ser:
            raise RuntimeError(f"Port {self.port} not open")
        start = time.time()
        last_rx = start
        buf = ""
        printed_chars = 0

        def _flush_live(text: str) -> None:
            nonlocal printed_chars
            new_text = text[printed_chars:]
            if not new_text:
                return
            lines = new_text.split("\n")
            for line in lines[:-1]:
                clean = line.rstrip("\r")
                if clean.strip():
                    prefix = f"  [{live_label}] " if live_label else "  "
                    print(prefix + clean, flush=True)
            last_nl = new_text.rfind("\n")
            if last_nl >= 0:
                printed_chars += last_nl + 1

        while True:
            chunk = self.ser.read_all().decode("utf-8", errors="ignore")
            if chunk:
                buf += chunk
                last_rx = time.time()
                if live_label:
                    _flush_live(buf)
                if stop_markers and any(m in buf for m in stop_markers):
                    time.sleep(0.2)
                    extra = self.ser.read_all().decode("utf-8", errors="ignore")
                    buf += extra
                    if live_label:
                        _flush_live(buf)
                    return CommandResult(command="", output=buf, timed_out=False)
                if stop_on_prompt and self._looks_like_prompt(buf):
                    return CommandResult(command="", output=buf, timed_out=False)
            now = time.time()
            if now - start > overall_timeout:
                return CommandResult(command="", output=buf, timed_out=True)
            if now - last_rx > idle_timeout:
                return CommandResult(command="", output=buf, timed_out=False)
            time.sleep(0.05)

    def run_command(
        self,
        command: str,
        overall_timeout: float,
        idle_timeout: float,
        stop_markers: Optional[List[str]] = None,
        stop_on_prompt: bool = False,
        live_label: str = "",
    ) -> CommandResult:
        self.clear_input()
        if live_label:
            print(f"\n>>> [{live_label}] $ {command}", flush=True)
        self.write_line(command)
        result = self.read_until_idle(
            overall_timeout=overall_timeout,
            idle_timeout=idle_timeout,
            stop_markers=stop_markers,
            stop_on_prompt=stop_on_prompt,
            live_label=live_label,
        )
        result.command = command
        return result

    def login(self, username: str = "root", password: str = "microchip",
              timeout: float = 30.0) -> bool:
        """Handle Linux login prompt falls noetig. True wenn Shell-Prompt bereit."""
        if not self.ser:
            return False
        self.ser.write(b"\n")
        self.ser.flush()
        buf = ""
        end_time = time.time() + timeout
        while time.time() < end_time:
            chunk = self.ser.read_all().decode("utf-8", errors="ignore")
            if chunk:
                buf += chunk
            if re.search(r"[#\$]\s*$", buf):
                return True
            if re.search(r"login:\s*$", buf, re.IGNORECASE):
                self.ser.write((username + "\n").encode("utf-8", errors="ignore"))
                self.ser.flush()
                buf = ""
                time.sleep(0.5)
                continue
            if re.search(r"[Pp]assword:\s*$", buf):
                self.ser.write((password + "\n").encode("utf-8", errors="ignore"))
                self.ser.flush()
                buf = ""
                time.sleep(1.0)
                continue
            time.sleep(0.2)
        return False

    def wake_prompt(self) -> None:
        self.clear_input()
        self.write_line("")
        time.sleep(0.3)
        if self.ser:
            self.ser.read_all()


# ---------------------------------------------------------------------------
# Parse-Funktionen  (identisch mit mcu_direct_access_proof.py)
# ---------------------------------------------------------------------------

def _unit_to_mbps(value: float, unit: str) -> float:
    u = unit.strip().lower()
    if u.startswith("k"):
        return value / 1000.0
    if u.startswith("g"):
        return value * 1000.0
    return value


def parse_mcu_iperf_summary(text: str) -> Dict:
    """MCU UDP-Server-Summary: '[ 0.0-10.x sec]  lost/total (pct%)  bw Kbps'"""
    pattern = re.compile(
        r"\[\s*0(?:\.0)?-\s*([0-9]+\.[0-9]+)\s*sec\]\s*"
        r"([0-9]+)\s*/\s*([0-9]+)\s*\(\s*([0-9]+)%\)\s*([0-9]+)\s*Kbps",
        re.IGNORECASE,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        return {}
    m = matches[-1]
    return {
        "interval_sec": float(m.group(1)),
        "lost": int(m.group(2)),
        "total_datagrams": int(m.group(3)),
        "loss_percent": int(m.group(4)),
        "bandwidth_kbps": int(m.group(5)),
        "bandwidth_mbps": round(int(m.group(5)) / 1000.0, 3),
    }


def parse_mcu_intervals_aggregate(text: str) -> Dict:
    """Notfall-Fallback: summiert Per-Sekunden-Intervall-Zeilen."""
    pat = re.compile(
        r"[-\s]+\[\s*(\d+)-\s*(\d+)\s*sec\]\s*(\d+)\s*/\s*(\d+)\s*\(\s*(\d+)%\)\s*(\d+)\s*Kbps",
        re.IGNORECASE,
    )
    total_lost = 0
    total_pkts = 0
    kbps_vals: List[int] = []
    for m in pat.finditer(text):
        total_lost += int(m.group(3))
        total_pkts += int(m.group(4))
        kbps_vals.append(int(m.group(6)))
    if not kbps_vals:
        return {}
    loss_pct = round(100.0 * total_lost / total_pkts) if total_pkts > 0 else 0
    avg_kbps = round(sum(kbps_vals) / len(kbps_vals))
    return {
        "interval_sec": len(kbps_vals),
        "lost": total_lost,
        "total_datagrams": total_pkts,
        "loss_percent": loss_pct,
        "bandwidth_kbps": avg_kbps,
        "bandwidth_mbps": round(avg_kbps / 1000.0, 3),
        "source": "intervals_aggregate",
    }


def _parse_linux_bg_iperf(text: str) -> Dict:
    """Parst iperf-Client-Summary aus MPU-Hintergrundausgabe (optional, nicht Hauptmessung)."""
    sent_pat = re.compile(r"Sent\s+([0-9]+)\s+datagrams", re.IGNORECASE)
    bw_pat = re.compile(
        r"\[\s*\d+\]\s*0\.00-\s*([0-9]+\.[0-9]+)\s*sec\s+"
        r"([0-9]+\.[0-9]+)\s+([KMG]?Bytes)\s+([0-9]+\.[0-9]+)\s+([KMG]?bits/sec)",
        re.IGNORECASE,
    )
    data: Dict = {}
    m = bw_pat.search(text)
    if m:
        val = float(m.group(4))
        data["bandwidth_mbps"] = round(_unit_to_mbps(val, m.group(5)), 3)
        data["interval_sec"] = float(m.group(1))
    s = sent_pat.search(text)
    if s:
        data["sent_datagrams"] = int(s.group(1))
    return data


def stop_mcu_iperf_server(mcu: SerialCLI) -> str:
    """iperfk (Fallback: iperk). Gibt die volle Ausgabe zurueck (wird fuer Parse benoetigt)."""
    for cmd in ("iperfk", "iperk"):
        res = mcu.run_command(cmd, overall_timeout=6.0, idle_timeout=1.5, stop_on_prompt=True)
        if "*** Command" not in res.output and "unknown" not in res.output.lower():
            return res.output
    return ""


# ---------------------------------------------------------------------------
# Injektions-Thread  (schreibt NUR auf MPU-Serial — Thread-sicher)
# ---------------------------------------------------------------------------

def _inject_loop(
    ser: serial.Serial,
    command: str,
    interval: float,
    start_delay: float,
    stop_event: threading.Event,
    counter: List[int],
    burst: int = 1,
) -> None:
    """Sendet 'command' periodisch an MPU-Serial in einem eigenen Thread.

    Schreibt NUR auf ser.write() — keine Leseoperationen.
    Thread-sicher: Haupt-Thread liest AUSSCHLIESSLICH MCU-Serial waehrend Injektion laeuft.

    Warum MPU-Serial:
      Auf der MPU ist lan_read ein Linux-Userspace-ioctl. Er laeuft als
      Shell-Kommando auf COM9 (MPU) und konkurriert mit dem LAN865x-Kernel-ISR
      auf dem TC6/SPI-Bus -- ohne dass der Kernel-Treiber davon weiss.

    burst > 1: mehrere Kommandos dicht hintereinander pro Zyklus.
    """
    time.sleep(start_delay)
    encoded = (command + "\n").encode("utf-8", errors="ignore")
    while not stop_event.is_set():
        try:
            for i in range(burst):
                ser.write(encoded)
                ser.flush()
                counter[0] += 1
                if burst > 1 and i < burst - 1:
                    time.sleep(0.005)  # 5ms Abstand innerhalb eines Bursts
        except Exception:
            break
        time.sleep(interval)


# ---------------------------------------------------------------------------
# Datenstrukturen
# ---------------------------------------------------------------------------

@dataclass
class CaseResult:
    name: str
    description: str
    mcu_server_raw: str = ""
    mcu_async_raw: str = ""
    mcu_stop_raw: str = ""
    mpu_client_raw: str = ""    # iperf-Hintergrund-Output + Injektion-Antworten (gemischt)
    injections_sent: int = 0
    duration_sec: float = 0.0
    server_kpi: Dict = field(default_factory=dict)  # primaere Verlust-Quelle: MCU-Server
    client_kpi: Dict = field(default_factory=dict)  # optional (iperf-Hinterground-Summary)
    error: str = ""

    @property
    def loss_pct(self) -> Optional[int]:
        return self.server_kpi.get("loss_percent")

    @property
    def total_datagrams(self) -> Optional[int]:
        return self.server_kpi.get("total_datagrams")

    @property
    def lost_datagrams(self) -> Optional[int]:
        return self.server_kpi.get("lost")


@dataclass
class RecoveryResult:
    """Ergebnis des eth0-Bounce-Wiederherstellungsversuchs nach Case B."""
    attempted: bool = False
    ping_before_bounce: int = 0   # empfangene Pakete direkt nach Case B
    ping_after_bounce: int = 0    # empfangene Pakete nach eth0 down/up
    ping_count: int = 5
    bounce_raw: str = ""
    ping_before_raw: str = ""
    ping_after_raw: str = ""
    error: str = ""

    @property
    def link_ok(self) -> bool:
        return self.ping_after_bounce > 0


# ---------------------------------------------------------------------------
# Einzelner Test-Case
# ---------------------------------------------------------------------------

def run_case(
    mcu: SerialCLI,
    mpu: SerialCLI,
    name: str,
    description: str,
    mcu_ip: str,
    duration: int,
    rate: str,
    inject_cmd: Optional[str],
    inject_interval: float,
    inject_start_delay: float,
    verbose: bool,
    inject_burst: int = 1,
) -> CaseResult:
    """Fuehrt einen einzelnen Test-Case aus.

    Ablauf:
      1. MCU: iperf-Server starten
      2. MPU: iperf-Client IM HINTERGRUND (&) starten
             -> Shell-Prompt kehrt sofort zurueck -> lan_read kann gleichzeitig laufen
      3. Injektions-Thread: schreibt inject_cmd auf MPU-Serial
      4. Haupt-Thread: liest MCU-Serial (Server-Ausgabe = Messung)
      5. Aufraueumen: iperf beenden, MCU-Server stoppen
      6. Parsen: MCU-Server-Output -> server_kpi (primaere Messung)
    """
    result = CaseResult(name=name, description=description)
    t_start = time.time()
    live = name if verbose else ""

    try:
        # --- Grundzustand herstellen ---
        mcu.run_command("fwd 0", overall_timeout=5.0, idle_timeout=0.8, stop_on_prompt=True)
        time.sleep(0.2)
        mcu.run_command("iperfk", overall_timeout=3.0, idle_timeout=0.6, stop_on_prompt=True)
        time.sleep(0.2)
        mpu.write_ctrl_c()
        time.sleep(0.1)
        mpu.run_command("killall iperf 2>/dev/null; true",
                        overall_timeout=3.0, idle_timeout=0.8, stop_on_prompt=True)
        time.sleep(0.3)

        # --- MCU UDP-Server starten ---
        mcu_server_start = mcu.run_command(
            "iperf -s -u",
            overall_timeout=8.0,
            idle_timeout=0.8,
            stop_markers=["Server listening on UDP port 5001"],
            live_label=live,
        )
        result.mcu_server_raw = mcu_server_start.output

        # --- MPU iperf-Client IM HINTERGRUND starten ---
        # Das '&' ist zwingend: nur so kann die Shell danach lan_read-Kommandos
        # empfangen waehrend iperf laeuft. Ohne '&' werden injizierte Kommandos
        # erst nach iperf-Ende ausgefuehrt (kein Gleichzeitigkeitseffekt!).
        if live:
            print(f"\n>>> [{live}] $ iperf -u -c {mcu_ip} -b {rate} -t {duration} &",
                  flush=True)
        mpu.write_line(f"iperf -u -c {mcu_ip} -b {rate} -t {duration} &")
        # Initiale MPU-Antwort: [1] <pid> + Verbindungsaufbau-Zeilen (~1.5s)
        mpu_init = mpu.read_until_idle(
            overall_timeout=3.5, idle_timeout=0.8,
            stop_on_prompt=True, live_label=live,
        )
        result.mpu_client_raw = mpu_init.output

        # --- Injektions-Thread starten (schreibt auf MPU-Serial) ---
        stop_event = threading.Event()
        counter: List[int] = [0]
        inject_thread: Optional[threading.Thread] = None
        if inject_cmd and mpu.ser:
            inject_thread = threading.Thread(
                target=_inject_loop,
                args=(mpu.ser, inject_cmd, inject_interval, inject_start_delay,
                      stop_event, counter, inject_burst),
                daemon=True,
            )
            inject_thread.start()

        # --- MCU Async-Output lesen (iperf-Intervalle + Summary erscheinen hier) ---
        # Haupt-Thread liest AUSSCHLIESSLICH MCU-Serial -> kein Konflikt mit Injektions-Thread.
        # overall_timeout gross: MCU gibt jede Sekunde eine Zeile aus, nach >2s still = fertig.
        mcu_async = mcu.read_until_idle(
            overall_timeout=duration + 10.0,
            idle_timeout=2.5,
            stop_on_prompt=False,
            live_label=live,
        )
        result.mcu_async_raw = mcu_async.output

        # --- Injektion beenden ---
        stop_event.set()
        if inject_thread:
            inject_thread.join(timeout=2.0)
        result.injections_sent = counter[0]

        # --- MPU aufraueumen: Hintergrund-iperf beenden, Puffer leeren ---
        mpu.run_command("killall iperf 2>/dev/null; true",
                        overall_timeout=4.0, idle_timeout=0.8, stop_on_prompt=True,
                        live_label=live)
        # Restausgabe sammeln: iperf-Hintergrundausgabe + lan_read-Antworten (gemischt)
        mpu_rest = mpu.read_until_idle(
            overall_timeout=3.0, idle_timeout=1.0, stop_on_prompt=True,
            live_label=live,
        )
        result.mpu_client_raw += "\n" + mpu_rest.output

        # --- MCU-Server stoppen, Output sichern ---
        result.mcu_stop_raw = stop_mcu_iperf_server(mcu)

        # --- KPIs parsen ---
        # Primaer: MCU-Server-Output (zuverlassig, unabhaengig von MPU-Zustand)
        all_mcu = (result.mcu_server_raw + "\n"
                   + result.mcu_async_raw + "\n"
                   + result.mcu_stop_raw)
        result.server_kpi = parse_mcu_iperf_summary(all_mcu)
        if not result.server_kpi:
            result.server_kpi = parse_mcu_intervals_aggregate(all_mcu)
        # Optional: MPU-Client-Summary (wenn iperf-Hintergrundausgabe vollstaendig)
        result.client_kpi = _parse_linux_bg_iperf(result.mpu_client_raw)

    except Exception as ex:
        result.error = str(ex)
        try:
            stop_event.set()
            stop_mcu_iperf_server(mcu)
            mpu.run_command("killall iperf 2>/dev/null; true",
                            overall_timeout=4.0, idle_timeout=0.8, stop_on_prompt=True)
        except Exception:
            pass

    result.duration_sec = time.time() - t_start
    return result


# ---------------------------------------------------------------------------
# Recovery-Check nach Case B
# ---------------------------------------------------------------------------

def run_recovery_check(
    mcu: SerialCLI,
    mpu: SerialCLI,
    mcu_ip: str,
    verbose: bool,
) -> RecoveryResult:
    """Versucht T1S-Link via eth0-Bounce wiederherzustellen.

    Empirische Erwartung (laut mpu_rx_diagnostic_test.py):
      eth0-Bounce hilft NICHT. Nur Power-Cycle stellt den Link wieder her.
    Dieser Test prueft, ob sich das bestaetigt oder widerlegt.
    """
    live = "RECOVERY" if verbose else ""
    rec = RecoveryResult(attempted=True)

    try:
        # 1. Ping-Zustand direkt nach Case B
        ping_b = mpu.run_command(
            f"ping -c 3 -W 1 {mcu_ip} 2>&1",
            overall_timeout=8.0, idle_timeout=1.0, stop_on_prompt=True,
            live_label=live,
        )
        rec.ping_before_raw = ping_b.output
        m = re.search(r"(\d+) received", ping_b.output)
        rec.ping_before_bounce = int(m.group(1)) if m else 0

        # 2. eth0 down / up Bounce
        # Die Hoffnung: der LAN865x-Kernel-Treiber reinitalisiert TC6 beim ifup.
        bounce = mpu.run_command(
            "ip link set eth0 down && sleep 2 && ip link set eth0 up",
            overall_timeout=12.0, idle_timeout=0.8, stop_on_prompt=True,
            live_label=live,
        )
        rec.bounce_raw = bounce.output

        # PLCA-Neuinitialisierung dauert einige Sekunden
        if verbose:
            print("  [RECOVERY] Warte 6s fuer PLCA-Resync ...", flush=True)
        time.sleep(6.0)

        # 3. Ping nach Bounce
        ping_a = mpu.run_command(
            f"ping -c 5 -W 2 {mcu_ip} 2>&1",
            overall_timeout=20.0, idle_timeout=1.0, stop_on_prompt=True,
            live_label=live,
        )
        rec.ping_after_raw = ping_a.output
        rec.ping_count = 5
        m = re.search(r"(\d+) received", ping_a.output)
        rec.ping_after_bounce = int(m.group(1)) if m else 0

    except Exception as ex:
        rec.error = str(ex)

    return rec


# ---------------------------------------------------------------------------
# Ausgabe und Bewertung
# ---------------------------------------------------------------------------

LOSS_THRESHOLD_PCT   = 3    # <= 3% = kein Problem
LINK_DEATH_THRESHOLD = 40   # >= 40% = Link-Tod (TC6 korrupiert)


def _verdict(c: CaseResult) -> str:
    if c.error:
        return f"ERROR: {c.error[:60]}"
    lp = c.loss_pct
    if lp is None:
        return "KEIN MESSWERT"
    if c.name.startswith("B"):
        if lp >= LINK_DEATH_THRESHOLD:
            return "LINK_DEATH"
        elif lp > LOSS_THRESHOLD_PCT:
            return "DISRUPTION"
        else:
            return "OK (kein Effekt)"
    else:
        return "OK" if lp <= LOSS_THRESHOLD_PCT else f"FEHLER ({lp}% > {LOSS_THRESHOLD_PCT}%)"


def _recovery_verdict(r: RecoveryResult) -> str:
    if r.error:
        return f"ERROR: {r.error[:60]}"
    if not r.attempted:
        return "NICHT VERSUCHT"
    if r.ping_after_bounce >= 3:
        return f"ERHOLT  ({r.ping_after_bounce}/{r.ping_count} Pings OK)"
    elif r.ping_after_bounce > 0:
        return f"TEILWEISE  ({r.ping_after_bounce}/{r.ping_count} Pings)"
    else:
        return f"POWER CYCLE ERFORDERLICH  (0/{r.ping_count} — eth0-Bounce ohne Wirkung)"


def _loss_str(c: CaseResult) -> str:
    if c.loss_pct is None:
        return "n/a"
    lost = c.lost_datagrams
    total = c.total_datagrams
    src = " ~" if c.server_kpi.get("source") == "intervals_aggregate" else ""
    return f"{c.loss_pct}%{src}  ({lost}/{total})"


def print_case_quick(c: CaseResult) -> None:
    print(f"  Verlust : {_loss_str(c)}", flush=True)
    if c.injections_sent:
        print(f"  Inj.    : {c.injections_sent} Kommandos gesendet", flush=True)
    print(f"  Urteil  : {_verdict(c)}", flush=True)


def print_summary(cases: List[CaseResult], rec: Optional[RecoveryResult] = None) -> None:
    W = 78
    print("\n" + "=" * W)
    print("  ZUSAMMENFASSUNG: MPU Direct-Register-Access-Issue Nachweis")
    print("=" * W)
    hdr = f"  {'Case':<20} {'Beschreibung':<26} {'Verlust':>16}  Urteil"
    print(hdr)
    print("-" * W)
    for c in cases:
        print(f"  {c.name:<20} {c.description:<26} {_loss_str(c):>16}  {_verdict(c)}")

    if rec:
        before_str = f"vor: {rec.ping_before_bounce}/3"
        print("-" * W)
        print(f"  {'RECOVERY':<20} {'eth0 bounce + Ping':<26} {before_str:>16}  {_recovery_verdict(rec)}")

    print("=" * W)

    a = next((c for c in cases if c.name.startswith("A")), None)
    b = next((c for c in cases if c.name.startswith("B")), None)
    ctrl = next((c for c in cases if c.name.startswith("C")), None)

    print("\n  BEFUND:")
    if a and b:
        a_lp = a.loss_pct if a.loss_pct is not None else 999
        b_lp = b.loss_pct if b.loss_pct is not None else 999
        c_lp = ctrl.loss_pct if (ctrl and ctrl.loss_pct is not None) else None

        if a_lp <= LOSS_THRESHOLD_PCT and b_lp >= LINK_DEATH_THRESHOLD:
            print(f"  [LINK_DEATH] lan_read auf MPU hat den T1S-Link zerstoert!")
            print(f"               Baseline {a_lp}% -> LAN_READ_MPU {b_lp}%  (Delta: +{b_lp - a_lp}%)")
            if c_lp is not None:
                ctrl_ok = "OK" if c_lp <= LOSS_THRESHOLD_PCT else f"AUFFAELLIG {c_lp}%"
                print(f"               CONTROL (proc/net/dev): {c_lp}% -> {ctrl_ok}")
            if rec:
                print(f"               Recovery: {_recovery_verdict(rec)}")
            print(f"\n  Bestaetigt: lan_read races mit LAN865x-Kernel-ISR -> TC6-Korruption.")
            print(f"  Operative Regel: lan_read NIEMALS waehrend aktivem Kernel-Treiber/Traffic.")

        elif a_lp <= LOSS_THRESHOLD_PCT and b_lp > LOSS_THRESHOLD_PCT:
            print(f"  [DISRUPTION] lan_read auf MPU verursacht {b_lp}% Paketverlust.")
            print(f"               Baseline {a_lp}% -> LAN_READ_MPU {b_lp}%  (Delta: +{b_lp - a_lp}%)")
            if c_lp is not None:
                print(f"               CONTROL: {c_lp}%")
            if rec:
                print(f"               Recovery: {_recovery_verdict(rec)}")
            print(f"\n  Eventuell langsame Injektionsrate. Versuch mit --aggressive.")

        elif a_lp <= LOSS_THRESHOLD_PCT and b_lp <= LOSS_THRESHOLD_PCT:
            print(f"  [KEIN EFFEKT] lan_read hat keinen messbaren Einfluss in diesem Lauf.")
            print(f"               Injektionsrate evtl. zu niedrig fuer das Mess-Fenster.")
            print(f"               Versuch mit --aggressive (50ms, burst=3, 4M).")

        elif a_lp > LOSS_THRESHOLD_PCT:
            print(f"  [BASELINE FEHLERHAFT] {a_lp}% Verlust ohne Eingriff!")
            print(f"  T1S-Link / PLCA-Setup pruefen. Test-Ergebnis nicht aussagekraeftig.")

        else:
            print(f"  [UNKLAR] Baseline {a_lp}%, LAN_READ_MPU {b_lp}%")
    print()


def save_report(cases: List[CaseResult], rec: Optional[RecoveryResult],
                args: argparse.Namespace) -> Path:
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / f"mpu_direct_access_proof_{ts}.json"

    report = {
        "timestamp": datetime.now().isoformat(),
        "tool": "mpu_direct_access_proof.py",
        "config": {
            "mcu_port": args.mcu_port,
            "mpu_port": args.mpu_port,
            "mcu_ip": args.mcu_ip,
            "rate": args.rate,
            "duration_sec": args.duration,
            "inject_interval_sec": args.inject_interval,
            "inject_burst": args.inject_burst,
            "inject_start_delay_sec": args.inject_start_delay,
            "aggressive_mode": args.aggressive,
            "loss_threshold_pct": LOSS_THRESHOLD_PCT,
            "link_death_threshold_pct": LINK_DEATH_THRESHOLD,
        },
        "cases": [
            {
                "name": c.name,
                "description": c.description,
                "injections_sent": c.injections_sent,
                "duration_sec": round(c.duration_sec, 1),
                "server_kpi": c.server_kpi,
                "client_kpi": c.client_kpi,
                "verdict": _verdict(c),
                "error": c.error,
                "raw": {
                    "mcu_server_start": c.mcu_server_raw,
                    "mcu_async": c.mcu_async_raw,
                    "mpu_client_bg": c.mpu_client_raw,
                },
            }
            for c in cases
        ],
        "recovery": {
            "attempted": rec.attempted,
            "ping_before_bounce": rec.ping_before_bounce,
            "ping_after_bounce": rec.ping_after_bounce,
            "ping_count": rec.ping_count,
            "verdict": _recovery_verdict(rec),
            "ping_before_raw": rec.ping_before_raw,
            "ping_after_raw": rec.ping_after_raw,
            "bounce_raw": rec.bounce_raw,
            "error": rec.error,
        } if rec else {"attempted": False},
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report_path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="MPU Direct-Register-Access-Issue: Nachweis via lan_read auf Linux-Host",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--mcu-port", default="COM8", help="MCU Serial-Port (default: COM8)")
    p.add_argument("--mpu-port", default="COM9", help="MPU Serial-Port (default: COM9)")
    p.add_argument("--baudrate", type=int, default=115200)
    p.add_argument("--mcu-ip", default="192.168.0.200", help="MCU IP-Adresse")
    p.add_argument("--mpu-ip", default="192.168.0.5",   help="MPU IP-Adresse")
    p.add_argument("--duration", type=int, default=10,
                   help="iperf-Dauer pro Case in Sekunden (default: 10)")
    p.add_argument("--rate", default="2M",
                   help="iperf UDP-Senderate z.B. 2M, 4M (default: 2M)")
    p.add_argument("--inject-interval", type=float, default=0.2,
                   help="Zeitabstand zwischen Injektionen in Sekunden (default: 0.2)")
    p.add_argument("--inject-start-delay", type=float, default=1.5,
                   help="Verzoegerung vor erster Injektion nach iperf-Start (default: 1.5)")
    p.add_argument("--inject-burst", type=int, default=1,
                   help="Anzahl lan_read-Kommandos pro Injektionszyklus (default: 1). "
                        "Burst>1 erhoeh die Kollisionswahrscheinlichkeit mit dem Kernel-ISR.")
    p.add_argument("--aggressive", action="store_true",
                   help="Verschaerft-Modus: inject_interval=0.05s, inject_burst=3, rate=4M. "
                        "Erhoeht Wahrscheinlichkeit und Geschwindigkeit des Link-Tods. "
                        "Einzeln gesetzte Optionen haben Vorrang.")
    p.add_argument("--skip-control", action="store_true",
                   help="Case C (CONTROL) ueberspringen")
    p.add_argument("--skip-recovery", action="store_true",
                   help="Recovery-Check nach Case B ueberspringen "
                        "(sinnvoll wenn Power-Cycle ohnehin noetig oder Test-Zeit sparen)")
    p.add_argument("--output-dir", default=".",
                   help="Ausgabeverzeichnis fuer JSON-Report (default: .)")
    p.add_argument("--verbose", action="store_true",
                   help="MCU/MPU-Ausgaben live anzeigen waehrend Test")
    return p.parse_args()


def main() -> None:
    args = parse_args()

    # --aggressive setzt Defaults; explizit gesetzte Optionen haben Vorrang
    if args.aggressive:
        if args.inject_interval == 0.2:
            args.inject_interval = 0.05
        if args.inject_burst == 1:
            args.inject_burst = 3
        if args.rate == "2M":
            args.rate = "4M"

    n_cases = 2 if args.skip_control else 3
    est_sec = n_cases * (args.duration + 12)
    if not args.skip_recovery:
        est_sec += 20  # eth0-Bounce + PLCA-Resync + Pings

    mode_tag = "  [VERSCHAERFT / --aggressive]" if args.aggressive else ""
    print("=" * 62)
    print(f"  MPU Direct-Register-Access-Issue  Nachweis{mode_tag}")
    print("=" * 62)
    print(f"  MCU  : {args.mcu_port}  ({args.mcu_ip})")
    print(f"  MPU  : {args.mpu_port}")
    burst_info = f"  burst={args.inject_burst}" if args.inject_burst > 1 else ""
    print(f"  iperf: {args.rate} / {args.duration}s  |  Inj.-Intervall: {args.inject_interval}s{burst_info}")
    inj_count_est = int((args.duration - args.inject_start_delay) / args.inject_interval)
    total_cmds_est = inj_count_est * args.inject_burst
    if args.inject_burst > 1:
        print(f"  Injektionen/Case: ~{inj_count_est} x {args.inject_burst} = ~{total_cmds_est} lan_read-Kommandos")
    else:
        print(f"  Injektionen/Case: ~{inj_count_est}")
    print(f"  Richtung : MPU (Client, &-Hintergrund) -> MCU (Server)")
    print(f"  Injektion: lan_read 0x00000001 -> MPU-Serial (konkurriert mit Kernel-ISR)")
    print(f"  Messung  : MCU-iperf-Server (unabhaengig von MPU-Zustand)")
    print(f"  Recovery : {'ja (eth0 bounce nach Case B)' if not args.skip_recovery else 'nein (--skip-recovery)'}")
    print(f"  Geschaetzte Gesamtlaufzeit: ~{est_sec}s")
    print()

    mcu = SerialCLI(args.mcu_port, args.baudrate)
    mpu = SerialCLI(args.mpu_port, args.baudrate)
    cases: List[CaseResult] = []
    rec: Optional[RecoveryResult] = None

    try:
        print("[SETUP] Oeffne serielle Ports ...", flush=True)
        mcu.open()
        mpu.open()

        print("[SETUP] MPU-Login (falls noetig) ...", flush=True)
        if not mpu.login():
            print("[WARN]  MPU-Login Timeout - fahre trotzdem fort")

        mcu.wake_prompt()

        print("[SETUP] MCU Grundzustand (fwd 0) ...")
        mcu.run_command("fwd 0", overall_timeout=5.0, idle_timeout=0.8, stop_on_prompt=True)
        time.sleep(0.5)

        # Verbindungstest vor dem Test
        print(f"[SETUP] Verbindungstest ping MPU -> MCU ({args.mcu_ip}) ...")
        ping_res = mpu.run_command(
            f"ping -c 3 -W 2 {args.mcu_ip} 2>&1",
            overall_timeout=12.0, idle_timeout=1.0, stop_on_prompt=True,
        )
        rx = re.search(r"(\d+) received", ping_res.output)
        if rx and int(rx.group(1)) > 0:
            print(f"  -> T1S-Link OK  ({rx.group(1)}/3 Antworten)\n")
        else:
            print("  -> WARNUNG: Keine Ping-Antwort! T1S-Link pruefen.\n")

        # ------------------------------------------------------------------
        # Case A: Baseline
        # ------------------------------------------------------------------
        print(f"[CASE A] BASELINE  --  iperf {args.rate}/{args.duration}s, keine Eingriffe")
        a = run_case(
            mcu, mpu,
            name="A_BASELINE",
            description=f"iperf {args.rate}/{args.duration}s",
            mcu_ip=args.mcu_ip,
            duration=args.duration,
            rate=args.rate,
            inject_cmd=None,
            inject_interval=args.inject_interval,
            inject_start_delay=args.inject_start_delay,
            verbose=args.verbose,
        )
        cases.append(a)
        print_case_quick(a)
        time.sleep(1.0)

        # ------------------------------------------------------------------
        # Case B: lan_read auf MPU (der eigentliche Nachweis)
        # ------------------------------------------------------------------
        burst_tag = f" burst={args.inject_burst}" if args.inject_burst > 1 else ""
        print(f"\n[CASE B] LAN_READ_MPU  --  iperf + lan_read 0x00000001 auf MPU alle "
              f"{args.inject_interval}s{burst_tag}")
        print(f"  WARNUNG: lan_read auf MPU kann TC6-State-Machine permanent zerstoeren!")
        b = run_case(
            mcu, mpu,
            name="B_LAN_READ_MPU",
            description=f"iperf+lan_read/{args.inject_interval}s",
            mcu_ip=args.mcu_ip,
            duration=args.duration,
            rate=args.rate,
            inject_cmd="lan_read 0x00000001",
            inject_interval=args.inject_interval,
            inject_start_delay=args.inject_start_delay,
            verbose=args.verbose,
            inject_burst=args.inject_burst,
        )
        cases.append(b)
        print_case_quick(b)
        time.sleep(1.0)

        # ------------------------------------------------------------------
        # Recovery nach Case B (eth0 bounce + Ping)
        # ------------------------------------------------------------------
        if not args.skip_recovery:
            print(f"\n[RECOVERY] eth0 bounce + Ping-Test nach Case B ...")
            rec = run_recovery_check(mcu, mpu, args.mcu_ip, args.verbose)
            print(f"  Ping vor Bounce : {rec.ping_before_bounce}/3  "
                  f"({'OK' if rec.ping_before_bounce > 0 else 'Link bereits tot'})")
            print(f"  Ping nach Bounce: {rec.ping_after_bounce}/{rec.ping_count}")
            print(f"  Urteil          : {_recovery_verdict(rec)}")
            if not rec.link_ok:
                print(f"  -> Link tot! Case C laeuft zur Dokumentation des Zustands.")
            time.sleep(1.0)

        # ------------------------------------------------------------------
        # Case C: CONTROL - cat /proc/net/dev (kein SPI-Zugriff)
        # ------------------------------------------------------------------
        if not args.skip_control:
            print(f"\n[CASE C] CONTROL  --  iperf + cat /proc/net/dev alle "
                  f"{args.inject_interval}s (kein SPI)")
            c = run_case(
                mcu, mpu,
                name="C_CONTROL",
                description=f"iperf+proc/net/dev/{args.inject_interval}s",
                mcu_ip=args.mcu_ip,
                duration=args.duration,
                rate=args.rate,
                inject_cmd="cat /proc/net/dev",
                inject_interval=args.inject_interval,
                inject_start_delay=args.inject_start_delay,
                verbose=args.verbose,
                inject_burst=1,  # kein Burst bei Control
            )
            cases.append(c)
            print_case_quick(c)

        # ------------------------------------------------------------------
        # Zusammenfassung
        # ------------------------------------------------------------------
        print_summary(cases, rec)

        report_path = save_report(cases, rec, args)
        print(f"  Report: {report_path}\n")

    except KeyboardInterrupt:
        print("\n[ABGEBROCHEN] Aufraeumen ...")
        try:
            mcu.run_command("iperfk", overall_timeout=3.0, idle_timeout=0.8, stop_on_prompt=True)
            mpu.run_command("killall iperf 2>/dev/null; true",
                            overall_timeout=3.0, idle_timeout=0.8, stop_on_prompt=True)
        except Exception:
            pass
    finally:
        mcu.close()
        mpu.close()


if __name__ == "__main__":
    main()
