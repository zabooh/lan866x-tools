#!/usr/bin/env python3
"""
mcu_direct_access_proof.py
==========================
Nachweis des Direct-Register-Access-Issues auf der MCU
und Verifikation eines entsprechenden Fixes.

Testablauf (MPU = Client, MCU = UDP-Server):
  A  BASELINE  iperf 2M/10s, keine Eingriffe         -> erwartet ~0% Verlust
  B  LAN_READ  iperf 2M/10s + lan_read alle 200ms    -> erwartet hohen Verlust
  C  CONTROL   iperf 2M/10s + stats    alle 200ms    -> erwartet ~0% Verlust

Interpretation:
  Bug aktiv  : A=~0%, B>>0%, C=~0%   -> lan_read verdraengt TC6-Datenpfad
  Bug gefixt : A=~0%, B=~0%          -> lan_read wird abgewiesen/verschoben

Richtung MPU->MCU bewusst gewaehlt: MCU->MPU-Richtung hat separaten Timer-Bug
(README_MCU_TIMER.md), der das Ergebnis verfaelschen wuerde.

Verwendung:
  python mcu_direct_access_proof.py
  python mcu_direct_access_proof.py --mcu-port COM8 --mpu-port COM9
  python mcu_direct_access_proof.py --duration 15 --rate 4M --verbose
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

import serial


# ---------------------------------------------------------------------------
# Serial CLI  (Pattern aus bandwidth_sweep_iperf_test.py / dual_target_iperf_serial_test.py)
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

    def login(self, username: str = "root", password: str = "microchip", timeout: float = 30.0) -> bool:
        """Handle Linux login prompt if needed. Gibt True zurueck, wenn Shell-Prompt bereit."""
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
# Parse-Funktionen (aus bandwidth_sweep_iperf_test.py / dual_target_iperf_serial_test.py)
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


def parse_linux_client_summary(text: str) -> Dict:
    """MPU iperf-Client-Summary: gesendete Datagramme und Bandbreite."""
    bw_pat = re.compile(
        r"\[\s*\d+\]\s*0\.00-\s*([0-9]+\.[0-9]+)\s*sec\s+"
        r"([0-9]+\.[0-9]+)\s+([KMG]?Bytes)\s+([0-9]+\.[0-9]+)\s+([KMG]?bits/sec)",
        re.IGNORECASE,
    )
    sent_pat = re.compile(r"Sent\s+([0-9]+)\s+datagrams", re.IGNORECASE)
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
    """iperfk (Fallback: iperk). Gibt die gesamte Ausgabe (cmd + response) zurueck."""
    for cmd in ("iperfk", "iperk"):
        res = mcu.run_command(command=cmd, overall_timeout=6.0, idle_timeout=1.5, stop_on_prompt=True)
        if "*** Command" not in res.output and "unknown" not in res.output.lower():
            return res.output
    return ""


def parse_mcu_intervals_aggregate(text: str) -> Dict:
    """Notfall-Fallback: summiert alle Per-Sekunden-Intervall-Zeilen.

    Format: '    - [ 0-  1 sec]   5/ 178 ( 2%)    2032 Kbps'
    Wird verwendet wenn kein abschliessendes Summary-Paket empfangen wurde.
    """
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


# ---------------------------------------------------------------------------
# Injektions-Thread
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
    """Sendet 'command' periodisch an MCU-Serial. Laeuft in eigenem Thread.

    Greift NUR auf ser.write() zu. Keine Lese-Operationen.
    Thread-sicher: Haupt-Thread liest nur MPU-Serial waehrend Injektion laeuft.

    burst: Anzahl schnell aufeinanderfolgender Kommandos pro Zyklus.
           burst > 1 saettigt den REG_OP_ARRAY (Groesse 2) gezielt.
    """
    time.sleep(start_delay)
    encoded = (command + "\n").encode("utf-8", errors="ignore")
    while not stop_event.is_set():
        try:
            for _ in range(burst):
                ser.write(encoded)
                ser.flush()
                counter[0] += 1
                if burst > 1 and _ < burst - 1:
                    time.sleep(0.005)  # 5ms zwischen Burst-Kommandos
        except Exception:
            break
        time.sleep(interval)


# ---------------------------------------------------------------------------
# Test-Case-Datenstruktur
# ---------------------------------------------------------------------------

@dataclass
class CaseResult:
    name: str
    description: str
    mcu_server_raw: str = ""
    mcu_async_raw: str = ""
    mcu_stop_raw: str = ""
    mpu_client_raw: str = ""
    injections_sent: int = 0
    duration_sec: float = 0.0
    server_kpi: Dict = field(default_factory=dict)   # primaere Verlust-Quelle: MCU-Server
    client_kpi: Dict = field(default_factory=dict)   # MPU-Client: gesendete Pakete
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
    result = CaseResult(name=name, description=description)
    t_start = time.time()
    live = name if verbose else ""

    try:
        # --- Grundzustand herstellen ---
        mcu.run_command("fwd 0", overall_timeout=5.0, idle_timeout=0.8, stop_on_prompt=True)
        time.sleep(0.2)
        # Allfaelligen laufenden iperf-Server stoppen
        mcu.run_command("iperfk", overall_timeout=3.0, idle_timeout=0.6, stop_on_prompt=True)
        time.sleep(0.2)
        # MPU-seitig aufraueumen
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

        # --- Injektions-Thread starten (nur bei B und C) ---
        stop_event = threading.Event()
        counter: List[int] = [0]
        inject_thread: Optional[threading.Thread] = None
        if inject_cmd and mcu.ser:
            inject_thread = threading.Thread(
                target=_inject_loop,
                args=(mcu.ser, inject_cmd, inject_interval, inject_start_delay, stop_event, counter, inject_burst),
                daemon=True,
            )
            inject_thread.start()

        # --- MPU iperf-Client starten (Hauptmessung) ---
        # idle_timeout muss > duration sein: iperf-Client ist nach den Verbindungszeilen
        # fuer ~duration Sekunden Still, bevor er am Ende Summary + Prompt ausgibt.
        mpu_client = mpu.run_command(
            f"iperf -u -c {mcu_ip} -b {rate} -t {duration}",
            overall_timeout=duration + 18.0,
            idle_timeout=duration + 2.0,
            stop_on_prompt=True,
            live_label=live,
        )
        result.mpu_client_raw = mpu_client.output

        # --- Injektion beenden ---
        stop_event.set()
        if inject_thread:
            inject_thread.join(timeout=2.0)
        result.injections_sent = counter[0]

        # --- MCU async-Ausgabe lesen (iperf-Session-Summary erscheint hier) ---
        # overall_timeout gross genug damit MCU alle Intervalle + Summary ausgeben kann.
        # idle_timeout 2.5s: MCU druckt jede Sekunde ein Intervall, nach >2s ist Session fertig.
        mcu_async = mcu.read_until_idle(
            overall_timeout=duration + 8.0,
            idle_timeout=2.5,
            stop_on_prompt=False,
            live_label=live,
        )
        result.mcu_async_raw = mcu_async.output

        # --- MCU-Server stoppen, Output fuer Parse aufheben ---
        # Summary kann nach iperfk erscheinen wenn Client-Fin-Paket nie ankam.
        result.mcu_stop_raw = stop_mcu_iperf_server(mcu)

        # --- KPIs parsen ---
        # 1. Versuch: finales Summary-Paket in server_start + async + stop
        all_mcu = (
            result.mcu_server_raw + "\n"
            + result.mcu_async_raw + "\n"
            + result.mcu_stop_raw
        )
        result.server_kpi = parse_mcu_iperf_summary(all_mcu)
        # 2. Fallback: per-Sekunden-Intervalle aufsummieren
        if not result.server_kpi:
            result.server_kpi = parse_mcu_intervals_aggregate(all_mcu)
        result.client_kpi = parse_linux_client_summary(result.mpu_client_raw)

    except Exception as ex:
        result.error = str(ex)
        # Cleanup-Versuch
        try:
            stop_event.set()
            stop_mcu_iperf_server(mcu)
        except Exception:
            pass

    result.duration_sec = time.time() - t_start
    return result


# ---------------------------------------------------------------------------
# Ausgabe und Bewertung
# ---------------------------------------------------------------------------

LOSS_THRESHOLD_PCT = 3  # <= 3% gilt als "kein Problem"


def _verdict(c: CaseResult) -> str:
    if c.error:
        return f"ERROR: {c.error[:60]}"
    lp = c.loss_pct
    if lp is None:
        return "KEIN MESSWERT"
    if c.name.startswith("B"):
        # Case B: testen ob Bug vorhanden oder gefixt
        return "BUG AKTIV" if lp > LOSS_THRESHOLD_PCT else "GEFIXT / kein Effekt"
    else:
        return "OK" if lp <= LOSS_THRESHOLD_PCT else f"FEHLER ({lp}% > {LOSS_THRESHOLD_PCT}%)"


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


def print_summary(cases: List[CaseResult]) -> None:
    W = 72
    print("\n" + "=" * W)
    print("  ZUSAMMENFASSUNG: Direct-Register-Access-Issue Nachweis")
    print("=" * W)
    hdr = f"  {'Case':<14} {'Beschreibung':<30} {'Verlust':>18}  Urteil"
    print(hdr)
    print("-" * W)
    for c in cases:
        print(f"  {c.name:<14} {c.description:<30} {_loss_str(c):>18}  {_verdict(c)}")
    print("=" * W)

    a = next((c for c in cases if c.name.startswith("A")), None)
    b = next((c for c in cases if c.name.startswith("B")), None)
    ctrl = next((c for c in cases if c.name.startswith("C")), None)

    print("\n  BEFUND:")
    if a and b:
        a_lp = a.loss_pct if a.loss_pct is not None else 999
        b_lp = b.loss_pct if b.loss_pct is not None else 999
        c_lp = ctrl.loss_pct if (ctrl and ctrl.loss_pct is not None) else None

        if a_lp <= LOSS_THRESHOLD_PCT and b_lp > LOSS_THRESHOLD_PCT:
            print(f"  [BUG AKTIV]  lan_read verursacht {b_lp}% Paketverlust waehrend iperf!")
            print(f"               Baseline {a_lp}% -> LAN_READ {b_lp}%  (Delta: +{b_lp - a_lp}%)")
            if c_lp is not None:
                control_ok = "OK (kein SPI-Konflikt)" if c_lp <= LOSS_THRESHOLD_PCT else f"UNERWARTET hoch: {c_lp}%"
                print(f"               CONTROL (stats): {c_lp}% -> {control_ok}")
            print(f"\n  Fix empfohlen: src/app.c -> lan_read()/lan_write() pruefen ob TC6 beschaeftigt.")

        elif a_lp <= LOSS_THRESHOLD_PCT and b_lp <= LOSS_THRESHOLD_PCT:
            print(f"  [BUG GEFIXT] lan_read stoert den Datenpfad NICHT mehr.")
            print(f"               Baseline {a_lp}% -> LAN_READ {b_lp}%  (kein signifikanter Unterschied)")

        elif a_lp > LOSS_THRESHOLD_PCT:
            print(f"  [BASELINE FEHLERHAFT] {a_lp}% Verlust ohne Eingriff!")
            print(f"  Netzwerk / PLCA-Setup pruefen. Test-Ergebnis nicht aussagekraeftig.")

        else:
            print(f"  [UNKLAR] Baseline {a_lp}%, LAN_READ {b_lp}%")
    print()


def save_report(cases: List[CaseResult], args: argparse.Namespace) -> Path:
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / f"direct_access_proof_{ts}.json"

    report = {
        "timestamp": datetime.now().isoformat(),
        "tool": "mcu_direct_access_proof.py",
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
                    "mpu_client": c.mpu_client_raw,
                },
            }
            for c in cases
        ],
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report_path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="MCU Direct-Register-Access-Issue: Nachweis und Fix-Verifikation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--mcu-port", default="COM8", help="MCU Serial-Port (default: COM8)")
    p.add_argument("--mpu-port", default="COM9", help="MPU Serial-Port (default: COM9)")
    p.add_argument("--baudrate", type=int, default=115200)
    p.add_argument("--mcu-ip", default="192.168.0.200", help="MCU IP-Adresse")
    p.add_argument("--mpu-ip", default="192.168.0.5",   help="MPU IP-Adresse (nicht direkt benutzt)")
    p.add_argument("--duration", type=int, default=10,
                   help="iperf-Dauer pro Case in Sekunden (default: 10)")
    p.add_argument("--rate", default="2M",
                   help="iperf UDP-Senderate, z.B. 2M, 4M (default: 2M)")
    p.add_argument("--inject-interval", type=float, default=0.2,
                   help="Zeitabstand zwischen Injektionen in Sekunden (default: 0.2)")
    p.add_argument("--inject-start-delay", type=float, default=1.0,
                   help="Verzoegerung vor erster Injektion nach iperf-Start (default: 1.0)")
    p.add_argument("--inject-burst", type=int, default=1,
                   help="Anzahl lan_read-Kommandos pro Injektionszyklus (default: 1). "
                        "Burst>1 saettigt den REG_OP_ARRAY_SIZE=2 und macht den Bug staerker sichtbar.")
    p.add_argument("--aggressive", action="store_true",
                   help="Verschaerft-Modus: inject_interval=0.05s, inject_burst=3, rate=4M. "
                        "Macht den Bug ~ 2x deutlicher (>10%% erwartet). "
                        "Einzeln gesetzte Optionen (--rate, --inject-interval, --inject-burst) haben Vorrang.")
    p.add_argument("--skip-control", action="store_true",
                   help="Case C (CONTROL/stats) ueberspringen -> schnellerer Test")
    p.add_argument("--output-dir", default=".",
                   help="Ausgabeverzeichnis fuer JSON-Report (default: .)")
    p.add_argument("--verbose", action="store_true",
                   help="MCU/MPU-Ausgaben live anzeigen waehrend Test")
    return p.parse_args()


def main() -> None:
    args = parse_args()

    # --aggressive setzt Defaults, wird aber durch explizit gesetzte Optionen ueberschrieben.
    # Da argparse keine einfache "war gesetzt"-Erkennung bietet, wenden wir das Preset
    # nur auf die Defaultwerte an (Vergleich gegen Defaultwert).
    if args.aggressive:
        if args.inject_interval == 0.2:   # Default nicht vom User geaendert
            args.inject_interval = 0.05
        if args.inject_burst == 1:
            args.inject_burst = 3
        if args.rate == "2M":
            args.rate = "4M"

    n_cases = 2 if args.skip_control else 3
    est_sec = n_cases * (args.duration + 10)

    mode_tag = "  [VERSCHAERFT / --aggressive]" if args.aggressive else ""
    print("=" * 60)
    print(f"  MCU Direct-Register-Access-Issue  Nachweis & Fix-Check{mode_tag}")
    print("=" * 60)
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
    print(f"  Geschaetzte Gesamtlaufzeit: ~{est_sec}s")
    print()

    mcu = SerialCLI(args.mcu_port, args.baudrate)
    mpu = SerialCLI(args.mpu_port, args.baudrate)

    cases: List[CaseResult] = []

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

        # Verbindungstest
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
        # Case B: lan_read Injektion (der eigentliche Nachweis)
        # ------------------------------------------------------------------
        burst_tag = f" burst={args.inject_burst}" if args.inject_burst > 1 else ""
        print(f"[CASE B] LAN_READ  --  iperf + lan_read 0x00000000 alle {args.inject_interval}s{burst_tag}")
        b = run_case(
            mcu, mpu,
            name="B_LAN_READ",
            description=f"iperf + lan_read/{args.inject_interval}s",
            mcu_ip=args.mcu_ip,
            duration=args.duration,
            rate=args.rate,
            inject_cmd="lan_read 0x00000000",
            inject_interval=args.inject_interval,
            inject_start_delay=args.inject_start_delay,
            verbose=args.verbose,
            inject_burst=args.inject_burst,
        )
        cases.append(b)
        print_case_quick(b)
        time.sleep(1.0)

        # ------------------------------------------------------------------
        # Case C: CONTROL - stats (kein SPI-Zugriff) als Kontrollgruppe
        # ------------------------------------------------------------------
        if not args.skip_control:
            print(f"\n[CASE C] CONTROL   --  iperf + stats alle {args.inject_interval}s (kein SPI)")
            c = run_case(
                mcu, mpu,
                name="C_CONTROL",
                description=f"iperf + stats/{args.inject_interval}s",
                mcu_ip=args.mcu_ip,
                duration=args.duration,
                rate=args.rate,
                inject_cmd="stats",
                inject_interval=args.inject_interval,
                inject_start_delay=args.inject_start_delay,
                verbose=args.verbose,
                inject_burst=1,  # stats kennt kein Burst-Konzept, bleibt immer 1
            )
            cases.append(c)
            print_case_quick(c)

        # ------------------------------------------------------------------
        # Ergebnis-Zusammenfassung
        # ------------------------------------------------------------------
        print_summary(cases)

        report_path = save_report(cases, args)
        print(f"  Report: {report_path}\n")

    except KeyboardInterrupt:
        print("\n[ABGEBROCHEN] Aufraeumen ...")
        try:
            stop_mcu_iperf_server(mcu)
        except Exception:
            pass
        sys.exit(1)
    finally:
        mcu.close()
        mpu.close()


if __name__ == "__main__":
    main()
