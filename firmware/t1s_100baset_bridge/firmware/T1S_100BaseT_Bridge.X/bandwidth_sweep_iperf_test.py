#!/usr/bin/env python3
"""
Bandwidth sweep iperf test runner — COM8 (MCU) and COM9 (MPU / Linux).

For each target rate the script runs the test in BOTH directions:
  (A)  MPU → MCU :  MPU = iperf UDP client,  MCU = iperf UDP server
  (B)  MCU → MPU :  MCU = iperf UDP client,  MPU = iperf UDP server

Default rates: 1M, 2M, 4M, 6M, 8M, 10M

A summary table is printed at the end and written to report files:
  bandwidth_sweep_report_<timestamp>.txt
  bandwidth_sweep_report_<timestamp>.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

import serial


# ---------------------------------------------------------------------------
# Serial helper
# ---------------------------------------------------------------------------

@dataclass
class CommandResult:
    command: str
    output: str
    timed_out: bool


class SerialCLI:
    def __init__(self, port: str, baudrate: int, timeout: float = 0.2):
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
            _ = self.ser.read_all()

    def write_line(self, line: str) -> None:
        if not self.ser:
            raise RuntimeError(f"Serial port {self.port} is not open")
        self.ser.write((line + "\n").encode("utf-8", errors="ignore"))
        self.ser.flush()

    def write_ctrl_c(self) -> None:
        if not self.ser:
            raise RuntimeError(f"Serial port {self.port} is not open")
        self.ser.write(b"\x03")
        self.ser.flush()

    def _looks_like_prompt(self, text: str, prompt_chars: str = r"[>#\$]") -> bool:
        lines = [ln.rstrip() for ln in text.splitlines() if ln.strip()]
        if not lines:
            return False
        return bool(re.search(rf"{prompt_chars}\s*$", lines[-1]))

    def read_until_idle(
        self,
        overall_timeout: float,
        idle_timeout: float,
        stop_markers: Optional[List[str]] = None,
        stop_on_prompt: bool = False,
        live_label: str = "",
    ) -> CommandResult:
        if not self.ser:
            raise RuntimeError(f"Serial port {self.port} is not open")

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

                if stop_markers and any(marker in buf for marker in stop_markers):
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

    def wake_prompt(self) -> str:
        self.clear_input()
        self.write_line("")
        time.sleep(0.2)
        return self.ser.read_all().decode("utf-8", errors="ignore") if self.ser else ""


# ---------------------------------------------------------------------------
# KPI parsers
# ---------------------------------------------------------------------------

def _unit_to_mbps(value: float, unit: str) -> float:
    normalized = unit.strip().lower()
    if normalized.startswith("k"):
        return value / 1000.0
    if normalized.startswith("g"):
        return value * 1000.0
    return value


def parse_mcu_iperf_summary(text: str) -> Dict[str, object]:
    """Parse MCU iperf summary line, e.g. '[ 0.0- 10.1 sec]  0/5219 ( 0%) 6042 Kbps'."""
    pattern = re.compile(
        r"\[\s*0(?:\.0)?-\s*([0-9]+\.[0-9]+)\s*sec\]\s*"
        r"([0-9]+)\s*/\s*([0-9]+)\s*\(\s*([0-9]+)%\)\s*([0-9]+)\s*Kbps",
        re.IGNORECASE,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        return {}

    m = matches[-1]
    lost = int(m.group(2))
    total = int(m.group(3))
    loss_pct = int(m.group(4))
    bw_kbps = int(m.group(5))

    return {
        "interval_sec": float(m.group(1)),
        "lost": lost,
        "total_datagrams": total,
        "loss_percent": loss_pct,
        "bandwidth_kbps": bw_kbps,
        "bandwidth_mbps": round(bw_kbps / 1000.0, 3),
    }


def parse_linux_client_summary(text: str) -> Dict[str, object]:
    """Parse Linux iperf UDP client output."""
    bw_pat = re.compile(
        r"\[\s*\d+\]\s*0\.00-\s*([0-9]+\.[0-9]+)\s*sec\s+"
        r"([0-9]+\.[0-9]+)\s+([KMG]?Bytes)\s+([0-9]+\.[0-9]+)\s+([KMG]?bits/sec)",
        re.IGNORECASE,
    )
    sent_pat = re.compile(r"Sent\s+([0-9]+)\s+datagrams", re.IGNORECASE)
    ooo_pat = re.compile(r"([0-9]+)\s+datagrams\s+received\s+out-of-order", re.IGNORECASE)

    bw_match = bw_pat.search(text)
    sent_match = sent_pat.search(text)
    ooo_match = ooo_pat.search(text)

    if not bw_match and not sent_match:
        return {}

    data: Dict[str, object] = {}
    if bw_match:
        bw_value = float(bw_match.group(4))
        bw_unit = bw_match.group(5)
        data.update(
            {
                "interval_sec": float(bw_match.group(1)),
                "transfer": f"{bw_match.group(2)} {bw_match.group(3)}",
                "bandwidth": f"{bw_value} {bw_unit}",
                "bandwidth_mbps": round(_unit_to_mbps(bw_value, bw_unit), 3),
            }
        )
    if sent_match:
        data["sent_datagrams"] = int(sent_match.group(1))
    if ooo_match:
        data["out_of_order_datagrams"] = int(ooo_match.group(1))

    return data


def parse_linux_server_summary(text: str) -> Dict[str, object]:
    """Parse Linux iperf UDP server report line with loss statistics."""
    server_pat = re.compile(
        r"\[\s*\d+\]\s*0\.00-\s*([0-9]+\.[0-9]+)\s*sec\s+"
        r"([0-9]+\.[0-9]+)\s+([KMG]?Bytes)\s+([0-9]+\.[0-9]+)\s+([KMG]?bits/sec)\s+"
        r"([0-9]+\.[0-9]+)\s*ms\s+([0-9]+)\s*/\s*([0-9]+)\s*\(\s*([0-9]+)%\)",
        re.IGNORECASE,
    )
    matches = list(server_pat.finditer(text))
    if not matches:
        return {}

    m = matches[-1]
    bw_value = float(m.group(4))
    bw_unit = m.group(5)
    return {
        "interval_sec": float(m.group(1)),
        "transfer": f"{m.group(2)} {m.group(3)}",
        "bandwidth": f"{bw_value} {bw_unit}",
        "bandwidth_mbps": round(_unit_to_mbps(bw_value, bw_unit), 3),
        "jitter_ms": float(m.group(6)),
        "lost": int(m.group(7)),
        "total_datagrams": int(m.group(8)),
        "loss_percent": int(m.group(9)),
    }


# ---------------------------------------------------------------------------
# MCU server stop helper
# ---------------------------------------------------------------------------

def stop_mcu_iperf_server(mcu: SerialCLI) -> Dict[str, str]:
    responses: Dict[str, str] = {}
    for cmd in ("iperk", "iperfk"):
        res = mcu.run_command(
            command=cmd,
            overall_timeout=4.0,
            idle_timeout=0.6,
            stop_on_prompt=True,
        )
        responses[cmd] = res.output
        if "*** Command" not in res.output and "unknown" not in res.output.lower():
            break
    return responses


# ---------------------------------------------------------------------------
# Single rate test (one direction)
# ---------------------------------------------------------------------------

def _run_mpu_to_mcu(
    mcu: SerialCLI,
    mpu: SerialCLI,
    rate: str,
    mcu_ip: str,
    iperf_duration: int,
    client_timeout: float,
    client_idle_timeout: float,
) -> Dict[str, object]:
    """MPU sends UDP to MCU.  MCU = server, MPU = client."""
    label_srv = f"MCU-srv"
    label_cli = f"MPU→{rate}"

    mcu_server_start = mcu.run_command(
        command="iperf -s -u",
        overall_timeout=8.0,
        idle_timeout=0.8,
        stop_markers=["Server listening on UDP port 5001"],
        live_label=label_srv,
    )

    mpu_client = mpu.run_command(
        command=f"iperf -u -c {mcu_ip} -b {rate} -t {iperf_duration}",
        overall_timeout=client_timeout,
        idle_timeout=client_idle_timeout,
        stop_on_prompt=True,
        live_label=label_cli,
    )

    print(f"  [{label_srv}] collecting async server output ...", flush=True)
    mcu_async = mcu.read_until_idle(
        overall_timeout=6.0,
        idle_timeout=1.0,
        stop_on_prompt=False,
        live_label=label_srv,
    )

    mcu_stop = stop_mcu_iperf_server(mcu)

    # Parse KPIs
    client_kpi = parse_linux_client_summary(mpu_client.output)
    server_kpi = parse_mcu_iperf_summary(mcu_server_start.output + "\n" + mcu_async.output)

    return {
        "direction": "MPU->MCU",
        "rate_target": rate,
        "mcu_server_start": {"command": mcu_server_start.command, "output": mcu_server_start.output},
        "mpu_client": {"command": mpu_client.command, "output": mpu_client.output, "timed_out": mpu_client.timed_out},
        "mcu_async": {"output": mcu_async.output, "timed_out": mcu_async.timed_out},
        "mcu_stop": mcu_stop,
        "kpi": {
            "client": client_kpi,
            "server": server_kpi,
            "bw_target_mbps": _rate_to_mbps(rate),
            "bw_client_mbps": client_kpi.get("bandwidth_mbps", None),
            "bw_server_mbps": server_kpi.get("bandwidth_mbps", None),
            "datagrams_sent": client_kpi.get("sent_datagrams", None),
            "datagrams_total": server_kpi.get("total_datagrams", None),
            "lost": server_kpi.get("lost", None),
            "loss_percent": server_kpi.get("loss_percent", None),
        },
    }


def _run_mcu_to_mpu(
    mcu: SerialCLI,
    mpu: SerialCLI,
    rate: str,
    mpu_ip: str,
    client_timeout: float,
) -> Dict[str, object]:
    """MCU sends UDP to MPU.  MPU = server, MCU = client."""
    label_srv = f"MPU-srv"
    label_cli = f"MCU→{rate}"

    mpu.clear_input()
    mpu_server_start = mpu.run_command(
        command="iperf -s -u",
        overall_timeout=8.0,
        idle_timeout=0.8,
        stop_markers=["Server listening on UDP port 5001"],
        live_label=label_srv,
    )

    mcu_client = mcu.run_command(
        command=f"iperf -u -c {mpu_ip} -b {rate}",
        overall_timeout=client_timeout,
        idle_timeout=1.5,
        stop_on_prompt=True,
        live_label=label_cli,
    )

    print(f"  [{label_srv}] collecting async server output ...", flush=True)
    mpu_async = mpu.read_until_idle(
        overall_timeout=8.0,
        idle_timeout=1.0,
        stop_on_prompt=False,
        live_label=label_srv,
    )

    mpu.clear_input()
    mpu.write_ctrl_c()
    time.sleep(0.1)
    mpu_stop = mpu.read_until_idle(overall_timeout=4.0, idle_timeout=0.8, stop_on_prompt=True)

    # Parse KPIs
    client_kpi = parse_mcu_iperf_summary(mcu_client.output)
    server_kpi = parse_linux_server_summary(mpu_server_start.output + "\n" + mpu_async.output)

    return {
        "direction": "MCU->MPU",
        "rate_target": rate,
        "mpu_server_start": {"command": mpu_server_start.command, "output": mpu_server_start.output},
        "mcu_client": {"command": mcu_client.command, "output": mcu_client.output, "timed_out": mcu_client.timed_out},
        "mpu_async": {"output": mpu_async.output, "timed_out": mpu_async.timed_out},
        "mpu_stop_ctrl_c": {"output": mpu_stop.output, "timed_out": mpu_stop.timed_out},
        "kpi": {
            "client": client_kpi,
            "server": server_kpi,
            "bw_target_mbps": _rate_to_mbps(rate),
            "bw_client_mbps": client_kpi.get("bandwidth_mbps", None),
            "bw_server_mbps": server_kpi.get("bandwidth_mbps", None),
            "datagrams_sent": client_kpi.get("total_datagrams", None),
            "datagrams_total": server_kpi.get("total_datagrams", None),
            "lost": server_kpi.get("lost", None),
            "loss_percent": server_kpi.get("loss_percent", None),
        },
    }


# ---------------------------------------------------------------------------
# Rate conversion helper
# ---------------------------------------------------------------------------

def _rate_to_mbps(rate: str) -> float:
    rate_upper = rate.strip().upper()
    try:
        if rate_upper.endswith("G"):
            return float(rate_upper[:-1]) * 1000.0
        if rate_upper.endswith("M"):
            return float(rate_upper[:-1])
        if rate_upper.endswith("K"):
            return float(rate_upper[:-1]) / 1000.0
        return float(rate_upper)
    except ValueError:
        return 0.0


def parse_rates(csv_rates: str) -> List[str]:
    rates = [x.strip() for x in csv_rates.split(",") if x.strip()]
    return rates or ["1M", "2M", "4M", "6M", "8M", "10M"]


# ---------------------------------------------------------------------------
# Main sweep runner
# ---------------------------------------------------------------------------

def run_sweep(args: argparse.Namespace) -> Dict[str, object]:
    mcu = SerialCLI(port=args.mcu_port, baudrate=args.baudrate, timeout=0.2)
    mpu = SerialCLI(port=args.mpu_port, baudrate=args.baudrate, timeout=0.2)

    rates = parse_rates(args.rates)

    report: Dict[str, object] = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "config": {
            "mcu_port": args.mcu_port,
            "mpu_port": args.mpu_port,
            "baudrate": args.baudrate,
            "mcu_ip": args.mcu_ip,
            "mpu_ip": args.mpu_ip,
            "rates": rates,
            "iperf_duration": args.iperf_duration,
        },
        "results": [],
    }

    try:
        print(f"[INFO] Opening MCU: {args.mcu_port},  MPU: {args.mpu_port}")
        mcu.open()
        mpu.open()

        print("[INFO] Sync prompts ...")
        mcu.wake_prompt()
        mpu.wake_prompt()

        total = len(rates) * 2
        step = 0

        for rate in rates:
            # --- Direction A: MPU → MCU ---
            step += 1
            print(f"\n{'='*70}")
            print(f"[{step}/{total}]  MPU → MCU  @  {rate}")
            print(f"{'='*70}")

            result_a = _run_mpu_to_mcu(
                mcu=mcu,
                mpu=mpu,
                rate=rate,
                mcu_ip=args.mcu_ip,
                iperf_duration=int(args.iperf_duration),
                client_timeout=args.client_timeout,
                client_idle_timeout=args.client_idle_timeout,
            )
            report["results"].append(result_a)

            # Short pause between directions
            time.sleep(1.0)

            # --- Direction B: MCU → MPU ---
            step += 1
            print(f"\n{'='*70}")
            print(f"[{step}/{total}]  MCU → MPU  @  {rate}")
            print(f"{'='*70}")

            result_b = _run_mcu_to_mpu(
                mcu=mcu,
                mpu=mpu,
                rate=rate,
                mpu_ip=args.mpu_ip,
                client_timeout=args.client_timeout,
            )
            report["results"].append(result_b)

            # Pause before next rate
            time.sleep(1.0)

    finally:
        mcu.close()
        mpu.close()

    return report


# ---------------------------------------------------------------------------
# Summary table
# ---------------------------------------------------------------------------

_NA = "n/a"


def _fmt(value: object, width: int, decimals: int = 2) -> str:
    if value is None:
        return _NA.center(width)
    try:
        return f"{float(value):.{decimals}f}".rjust(width)
    except (TypeError, ValueError):
        return str(value).rjust(width)


def _loss_str(lost: object, total: object, pct: object) -> str:
    if lost is None or total is None or pct is None:
        return _NA
    return f"{lost}/{total} ({pct}%)"


def print_summary_table(report: Dict[str, object]) -> None:
    results: List[Dict] = report.get("results", [])
    if not results:
        print("\n[WARN] No results to display.")
        return

    col_w = {
        "rate":      8,
        "dir":      10,
        "target":   10,
        "bw_cli":   12,
        "bw_srv":   12,
        "dg":       15,
        "loss":     20,
    }

    header = (
        f"{'Rate':>{col_w['rate']}} "
        f"{'Direction':<{col_w['dir']}} "
        f"{'Target':>{col_w['target']}} "
        f"{'BW-client':>{col_w['bw_cli']}} "
        f"{'BW-server':>{col_w['bw_srv']}} "
        f"{'Datagrams':>{col_w['dg']}} "
        f"{'Loss (lost/total %)':>{col_w['loss']}}"
    )
    sub = (
        f"{'Mbit/s':>{col_w['rate']}} "
        f"{'':>{col_w['dir']}} "
        f"{'Mbit/s':>{col_w['target']}} "
        f"{'Mbit/s':>{col_w['bw_cli']}} "
        f"{'Mbit/s':>{col_w['bw_srv']}} "
        f"{'sent':>{col_w['dg']}} "
        f"{'':>{col_w['loss']}}"
    )
    sep = "-" * len(header)

    print()
    print("=" * len(header))
    print("BANDWIDTH SWEEP SUMMARY")
    print("=" * len(header))
    print(header)
    print(sub)
    print(sep)

    prev_rate = None
    for r in results:
        kpi = r.get("kpi", {})
        rate = r.get("rate_target", _NA)
        direction = r.get("direction", _NA)
        target_mbps = kpi.get("bw_target_mbps", None)
        bw_cli = kpi.get("bw_client_mbps", None)
        bw_srv = kpi.get("bw_server_mbps", None)
        dg_sent = kpi.get("datagrams_sent") or kpi.get("datagrams_total") or kpi.get("client", {}).get("total_datagrams")
        lost = kpi.get("lost")
        total_dg = kpi.get("datagrams_total")
        loss_pct = kpi.get("loss_percent")

        # Separator between different rates for readability
        if prev_rate is not None and rate != prev_rate:
            print(sep)
        prev_rate = rate

        loss_str = _loss_str(lost, total_dg, loss_pct)
        dg_str = str(dg_sent) if dg_sent is not None else _NA

        print(
            f"{_fmt(target_mbps, col_w['rate'])} "
            f"{direction:<{col_w['dir']}} "
            f"{_fmt(target_mbps, col_w['target'])} "
            f"{_fmt(bw_cli, col_w['bw_cli'])} "
            f"{_fmt(bw_srv, col_w['bw_srv'])} "
            f"{dg_str:>{col_w['dg']}} "
            f"{loss_str:>{col_w['loss']}}"
        )

    print("=" * len(header))


# ---------------------------------------------------------------------------
# Report file output
# ---------------------------------------------------------------------------

def save_report_files(report: Dict[str, object], out_dir: Path) -> Dict[str, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = out_dir / f"bandwidth_sweep_report_{stamp}.json"
    txt_path = out_dir / f"bandwidth_sweep_report_{stamp}.txt"

    json_path.write_text(json.dumps(report, indent=2, ensure_ascii=True), encoding="utf-8")

    # Build text report
    lines: List[str] = []
    lines.append("BANDWIDTH SWEEP IPERF REPORT\n")
    lines.append(f"Timestamp : {report.get('timestamp', '')}\n")
    lines.append(json.dumps(report.get("config", {}), indent=2, ensure_ascii=True) + "\n\n")

    results: List[Dict] = report.get("results", [])
    col_widths = (8, 10, 10, 12, 12, 15, 20)
    col_names  = ("Mbit/s", "Direction", "Target", "BW-client", "BW-server", "Datagrams", "Loss (lost/total %)")

    def row_str(cols: tuple) -> str:
        return "  ".join(str(c).rjust(w) for c, w in zip(cols, col_widths))

    header = row_str(col_names)
    sep = "-" * len(header)
    lines.append(header + "\n")
    lines.append(sep + "\n")

    prev_rate = None
    for r in results:
        kpi = r.get("kpi", {})
        rate = r.get("rate_target", _NA)
        direction = r.get("direction", _NA)
        target_mbps = kpi.get("bw_target_mbps", None)
        bw_cli = kpi.get("bw_client_mbps", None)
        bw_srv = kpi.get("bw_server_mbps", None)
        dg_sent = kpi.get("datagrams_sent") or kpi.get("datagrams_total") or kpi.get("client", {}).get("total_datagrams")
        lost = kpi.get("lost")
        total_dg = kpi.get("datagrams_total")
        loss_pct = kpi.get("loss_percent")

        if prev_rate is not None and rate != prev_rate:
            lines.append(sep + "\n")
        prev_rate = rate

        dg_str = str(dg_sent) if dg_sent is not None else _NA
        loss_str = _loss_str(lost, total_dg, loss_pct)

        t = target_mbps
        t_str = f"{float(t):.2f}" if t is not None else _NA
        cli_str = f"{float(bw_cli):.2f}" if bw_cli is not None else _NA
        srv_str = f"{float(bw_srv):.2f}" if bw_srv is not None else _NA

        lines.append(row_str((t_str, direction, t_str, cli_str, srv_str, dg_str, loss_str)) + "\n")

    lines.append(sep + "\n\n")

    # Per-result raw logs
    for idx, r in enumerate(results, start=1):
        direction = r.get("direction", "?")
        rate = r.get("rate_target", "?")
        lines.append(f"{'='*60}\n")
        lines.append(f"[{idx}] {direction}  @  {rate}\n")
        lines.append(f"{'='*60}\n")

        for key, label in (
            ("mcu_server_start", "MCU SERVER START"),
            ("mpu_client", "MPU CLIENT"),
            ("mcu_async", "MCU ASYNC OUTPUT"),
            ("mpu_server_start", "MPU SERVER START"),
            ("mcu_client", "MCU CLIENT"),
            ("mpu_async", "MPU ASYNC OUTPUT"),
        ):
            block = r.get(key)
            if block:
                body = block.get("output", "")
                lines.append(f"\n--- {label} ---\n")
                lines.append((body.rstrip() if body.strip() else "<no output>") + "\n")

    txt_path.write_text("".join(lines), encoding="utf-8")
    return {"json": json_path, "txt": txt_path}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Bandwidth sweep iperf test: tests each rate in both directions "
            "(MPU→MCU and MCU→MPU) and prints a summary table."
        ),
    )
    parser.add_argument("--mcu-port", default="COM8", help="MCU serial port (default: COM8)")
    parser.add_argument("--mpu-port", default="COM9", help="MPU serial port (default: COM9)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--mcu-ip", default="192.168.0.200", help="MCU IP address (default: 192.168.0.200)")
    parser.add_argument("--mpu-ip", default="192.168.0.5", help="MPU IP address (default: 192.168.0.5)")
    parser.add_argument(
        "--rates",
        default="1M,2M,4M,6M,8M,10M",
        help="Comma-separated bandwidth rates to test (default: 1M,2M,4M,6M,8M,10M)",
    )
    parser.add_argument(
        "--iperf-duration",
        type=float,
        default=10.0,
        help="iperf test duration in seconds (default: 10)",
    )
    parser.add_argument(
        "--client-timeout",
        type=float,
        default=40.0,
        help="Max wait for iperf client command to finish (default: 40s)",
    )
    parser.add_argument(
        "--client-idle-timeout",
        type=float,
        default=12.0,
        help="Idle timeout when collecting Linux iperf client output (default: 12s)",
    )
    parser.add_argument(
        "--out-dir",
        default=".",
        help="Directory for report files (default: current directory)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        report = run_sweep(args)
        print_summary_table(report)
        out_paths = save_report_files(report, Path(args.out_dir))
        print("\n[INFO] Report files written:")
        print(f"  JSON: {out_paths['json']}")
        print(f"  TXT : {out_paths['txt']}")
        return 0
    except serial.SerialException as exc:
        print(f"[ERROR] Serial connection failed: {exc}")
        return 2
    except Exception as exc:  # noqa: BLE001
        print(f"[ERROR] Sweep failed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
