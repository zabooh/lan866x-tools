#!/usr/bin/env python3
"""
Bidirectional TCP iperf serial test runner for two targets:
  MCU CLI on COM8  (ATSAME54, 192.168.0.200)
  MPU Linux CLI on COM9  (192.168.0.5)

Flow:
  Phase 1:  MCU = TCP server,  MPU = TCP client
  Phase 2:  MPU = TCP server,  MCU = TCP client

TCP has no datagram-loss statistics; KPIs show bandwidth and transfer only.

Report files:
  tcp_iperf_report_<timestamp>.txt
  tcp_iperf_report_<timestamp>.json
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
# Serial helper (identical to dual_target_iperf_serial_test.py)
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
# TCP KPI parsers
# ---------------------------------------------------------------------------

def _unit_to_mbps(value: float, unit: str) -> float:
    normalized = unit.strip().lower()
    if normalized.startswith("k"):
        return value / 1000.0
    if normalized.startswith("g"):
        return value * 1000.0
    return value


def parse_mcu_tcp_summary(text: str) -> Dict[str, object]:
    """Parse MCU iperf TCP summary.

    Expected MCU TCP formats (implementation-dependent):
      - [0.0- 10.0 sec]   8500 Kbps
      - [0.0- 10.0 sec]   10.5 MBytes   8500 Kbps
    Falls back to any Kbps line if the structured format is absent.
    """
    # Pattern with optional transfer field
    pattern = re.compile(
        r"\[\s*0(?:\.0)?-\s*([0-9]+\.[0-9]+)\s*sec\]\s*"
        r"(?:([0-9]+(?:\.[0-9]+)?)\s*[KMG]?Bytes\s+)?"
        r"([0-9]+)\s*Kbps",
        re.IGNORECASE,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        return {}

    m = matches[-1]
    bw_kbps = int(m.group(3))
    result: Dict[str, object] = {
        "interval_sec": float(m.group(1)),
        "bandwidth_kbps": bw_kbps,
        "bandwidth_mbps": round(bw_kbps / 1000.0, 3),
    }
    if m.group(2) is not None:
        result["transfer"] = m.group(2)
    return result


def parse_linux_tcp_summary(text: str) -> Dict[str, object]:
    """Parse Linux iperf TCP summary line.

    Format: [  1] 0.00-10.09 sec  10.5 MBytes  8.71 Mbits/sec
    The server line is identical in format (no jitter/loss for TCP).
    """
    pat = re.compile(
        r"\[\s*\d+\]\s*0\.00-\s*([0-9]+\.[0-9]+)\s*sec\s+"
        r"([0-9]+(?:\.[0-9]+)?)\s+([KMG]?Bytes)\s+"
        r"([0-9]+(?:\.[0-9]+)?)\s+([KMG]?bits/sec)",
        re.IGNORECASE,
    )
    matches = list(pat.finditer(text))
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
    }


def build_phase_kpis(
    phase1_mcu_text: str,
    phase1_mpu_text: str,
    phase2_mpu_text: str,
    phase2_mcu_text: str,
) -> Dict[str, object]:
    """Build TCP KPI summary (no loss — TCP is reliable)."""
    phase1_mcu = parse_mcu_tcp_summary(phase1_mcu_text)
    phase1_mpu = parse_linux_tcp_summary(phase1_mpu_text)
    phase2_mpu = parse_linux_tcp_summary(phase2_mpu_text)
    phase2_mcu = parse_mcu_tcp_summary(phase2_mcu_text)

    phase1_line = (
        "PHASE1 MPU->MCU (TCP) | "
        f"BW(client)={phase1_mpu.get('bandwidth_mbps', 'n/a')} Mbit/s | "
        f"Transfer(client)={phase1_mpu.get('transfer', 'n/a')} | "
        f"BW(server)={phase1_mcu.get('bandwidth_kbps', 'n/a')} Kbps"
    )
    phase2_line = (
        "PHASE2 MCU->MPU (TCP) | "
        f"BW(client)={phase2_mcu.get('bandwidth_kbps', 'n/a')} Kbps | "
        f"BW(server)={phase2_mpu.get('bandwidth_mbps', 'n/a')} Mbit/s | "
        f"Transfer(server)={phase2_mpu.get('transfer', 'n/a')}"
    )

    return {
        "phase1": {
            "direction": "MPU->MCU",
            "client": phase1_mpu,
            "server": phase1_mcu,
            "compact_line": phase1_line,
        },
        "phase2": {
            "direction": "MCU->MPU",
            "client": phase2_mcu,
            "server": phase2_mpu,
            "compact_line": phase2_line,
        },
    }


# ---------------------------------------------------------------------------
# Diagnostics (same as UDP version)
# ---------------------------------------------------------------------------

def collect_mpu_diagnostics(mpu: SerialCLI, tag: str) -> Dict[str, object]:
    commands = [
        "ip -s link",
        "ifconfig -a",
        "cat /proc/net/snmp",
    ]
    collected: Dict[str, object] = {"tag": tag, "commands": []}
    for cmd in commands:
        res = mpu.run_command(
            command=cmd,
            overall_timeout=8.0,
            idle_timeout=0.8,
            stop_on_prompt=True,
        )
        collected["commands"].append(
            {"command": cmd, "output": res.output, "timed_out": res.timed_out}
        )
    return collected


def collect_mcu_stats(mcu: SerialCLI, tag: str) -> Dict[str, object]:
    res = mcu.run_command(
        command="stats",
        overall_timeout=8.0,
        idle_timeout=0.8,
        stop_on_prompt=True,
    )
    return {
        "tag": tag,
        "command": res.command,
        "output": res.output,
        "timed_out": res.timed_out,
    }


# ---------------------------------------------------------------------------
# Key-line extractor for highlights
# ---------------------------------------------------------------------------

def extract_key_lines(text: str) -> List[str]:
    patterns = [
        r"Server listening on TCP port",
        r"Server listening on port",
        r"Client connecting to",
        r"\[\s*\d+\]\s*0\.00-\d+\.\d+ sec",
        r"\[0\.0-\s*\d+\.\d+ sec\].*Kbps",
        r"instance\s+\d+\s+completed",
        r"Rx done|Tx done",
        r"WARNING:",
        r"connect failed|Connection refused|timed out",
    ]
    combined = re.compile("|".join(patterns), re.IGNORECASE)
    return [line for line in text.splitlines() if combined.search(line)]


# ---------------------------------------------------------------------------
# MCU server stop
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
# Main test runner
# ---------------------------------------------------------------------------

def _mcu_server_cmd(args: argparse.Namespace) -> str:
    cmd = "iperf -s"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    return cmd


def _mpu_client_cmd(args: argparse.Namespace) -> str:
    cmd = f"iperf -c {args.mcu_ip} -t {int(args.iperf_duration)}"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    if args.parallel:
        cmd += f" -P {args.parallel}"
    return cmd


def _mpu_server_cmd(args: argparse.Namespace) -> str:
    cmd = "iperf -s"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    if args.parallel:
        cmd += f" -P {args.parallel}"
    return cmd


def _mcu_client_cmd(args: argparse.Namespace) -> str:
    cmd = f"iperf -c {args.mpu_ip}"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    return cmd


def run_test(args: argparse.Namespace) -> Dict[str, object]:
    mcu = SerialCLI(port=args.mcu_port, baudrate=args.baudrate, timeout=0.2)
    mpu = SerialCLI(port=args.mpu_port, baudrate=args.baudrate, timeout=0.2)

    report: Dict[str, object] = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "protocol": "TCP",
        "config": {
            "mcu_port": args.mcu_port,
            "mpu_port": args.mpu_port,
            "baudrate": args.baudrate,
            "mcu_ip": args.mcu_ip,
            "mpu_ip": args.mpu_ip,
            "iperf_duration": args.iperf_duration,
            "tcp_window": args.tcp_window or "default",
            "parallel_streams": args.parallel,
        },
        "steps": {},
        "diagnostics": {},
    }

    try:
        print(f"[INFO] Opening MCU: {args.mcu_port},  MPU: {args.mpu_port}")
        mcu.open()
        mpu.open()

        print("[INFO] Sync prompts ...")
        report["steps"]["sync_prompts"] = {
            "mcu": mcu.wake_prompt(),
            "mpu": mpu.wake_prompt(),
        }

        # ── diagnostics before Phase 1 ──────────────────────────────────────
        print("\n[DIAG] phase1_before — collecting stats ...")
        report["diagnostics"]["phase1_before"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase1_before"),
            "mcu": collect_mcu_stats(mcu, "phase1_before"),
        }

        # ── Phase 1: MCU TCP server, MPU TCP client ──────────────────────────
        print("\n[STEP] Phase 1: MCU TCP server + MPU TCP client")

        srv_cmd = _mcu_server_cmd(args)
        mcu_server_start = mcu.run_command(
            command=srv_cmd,
            overall_timeout=8.0,
            idle_timeout=0.8,
            # TCP server listen message varies: "Server listening on TCP port 5001"
            stop_markers=["Server listening on TCP port", "Server listening on port 5001"],
            live_label="MCU",
        )

        cli_cmd = _mpu_client_cmd(args)
        mpu_client = mpu.run_command(
            command=cli_cmd,
            overall_timeout=args.client_timeout,
            idle_timeout=args.client_idle_timeout,
            stop_on_prompt=True,
            live_label="MPU",
        )

        print("  [MCU] collecting async server output ...", flush=True)
        mcu_after_client = mcu.read_until_idle(
            overall_timeout=8.0,
            idle_timeout=1.5,
            stop_on_prompt=False,
            live_label="MCU",
        )

        mcu_stop = stop_mcu_iperf_server(mcu)

        report["steps"]["phase1"] = {
            "mcu_server_start": {
                "command": mcu_server_start.command,
                "output": mcu_server_start.output,
                "timed_out": mcu_server_start.timed_out,
            },
            "mpu_client": {
                "command": mpu_client.command,
                "output": mpu_client.output,
                "timed_out": mpu_client.timed_out,
            },
            "mcu_async_after_client": {
                "output": mcu_after_client.output,
                "timed_out": mcu_after_client.timed_out,
            },
            "mcu_stop_commands": mcu_stop,
        }

        # ── diagnostics after Phase 1 ────────────────────────────────────────
        print("\n[DIAG] phase1_after — collecting stats ...")
        report["diagnostics"]["phase1_after"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase1_after"),
            "mcu": collect_mcu_stats(mcu, "phase1_after"),
        }

        mpu.clear_input()

        # ── Phase 2: MPU TCP server, MCU TCP client ──────────────────────────
        print("\n[STEP] Phase 2: MPU TCP server + MCU TCP client")

        srv_cmd_mpu = _mpu_server_cmd(args)
        mpu_server_start = mpu.run_command(
            command=srv_cmd_mpu,
            overall_timeout=8.0,
            idle_timeout=0.8,
            stop_markers=["Server listening on TCP port", "Server listening on port 5001"],
            live_label="MPU",
        )

        cli_cmd_mcu = _mcu_client_cmd(args)
        mcu_client = mcu.run_command(
            command=cli_cmd_mcu,
            overall_timeout=args.client_timeout,
            idle_timeout=2.0,
            stop_on_prompt=True,
            live_label="MCU",
        )

        print("  [MPU] collecting async server output ...", flush=True)
        mpu_after_client = mpu.read_until_idle(
            overall_timeout=10.0,
            idle_timeout=1.5,
            stop_on_prompt=False,
            live_label="MPU",
        )

        mpu.clear_input()
        mpu.write_ctrl_c()
        time.sleep(0.1)
        mpu_stop = mpu.read_until_idle(
            overall_timeout=4.0,
            idle_timeout=0.8,
            stop_on_prompt=True,
        )

        report["steps"]["phase2"] = {
            "mpu_server_start": {
                "command": mpu_server_start.command,
                "output": mpu_server_start.output,
                "timed_out": mpu_server_start.timed_out,
            },
            "mcu_client": {
                "command": mcu_client.command,
                "output": mcu_client.output,
                "timed_out": mcu_client.timed_out,
            },
            "mpu_async_after_client": {
                "output": mpu_after_client.output,
                "timed_out": mpu_after_client.timed_out,
            },
            "mpu_stop_ctrl_c": {
                "command": "CTRL+C",
                "output": mpu_stop.output,
                "timed_out": mpu_stop.timed_out,
            },
        }

        # ── diagnostics after Phase 2 ────────────────────────────────────────
        print("\n[DIAG] phase2_after — collecting stats ...")
        report["diagnostics"]["phase2_after"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase2_after"),
            "mcu": collect_mcu_stats(mcu, "phase2_after"),
        }

        # ── KPIs ─────────────────────────────────────────────────────────────
        report["kpis"] = build_phase_kpis(
            phase1_mcu_text=mcu_server_start.output + "\n" + mcu_after_client.output,
            phase1_mpu_text=mpu_client.output,
            phase2_mpu_text=mpu_server_start.output + "\n" + mpu_after_client.output,
            phase2_mcu_text=mcu_client.output,
        )

        report["highlights"] = {
            "phase1_mcu": extract_key_lines(mcu_server_start.output + "\n" + mcu_after_client.output),
            "phase1_mpu": extract_key_lines(mpu_client.output),
            "phase2_mpu": extract_key_lines(mpu_server_start.output + "\n" + mpu_after_client.output),
            "phase2_mcu": extract_key_lines(mcu_client.output),
        }

        return report

    finally:
        mcu.close()
        mpu.close()


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def print_report_to_stdout(report: Dict[str, object]) -> None:
    W = 90
    print("\n" + "=" * W)
    print("DUAL TARGET TCP IPERF REPORT")
    print("=" * W)

    kpis = report.get("kpis", {})
    print("KPI SUMMARY")
    print(f"  {kpis.get('phase1', {}).get('compact_line', 'PHASE1: n/a')}")
    print(f"  {kpis.get('phase2', {}).get('compact_line', 'PHASE2: n/a')}")
    print("=" * W)
    print(json.dumps(report["config"], indent=2, ensure_ascii=True))

    steps = report.get("steps", {})

    def section(title: str, text: str) -> None:
        print("\n" + "-" * W)
        print(title)
        print("-" * W)
        print(text.rstrip() if text.strip() else "<no output>")

    phase1 = steps.get("phase1", {})
    section("PHASE1 MCU SERVER START", phase1.get("mcu_server_start", {}).get("output", ""))
    section("PHASE1 MPU CLIENT", phase1.get("mpu_client", {}).get("output", ""))
    section("PHASE1 MCU ASYNC OUTPUT", phase1.get("mcu_async_after_client", {}).get("output", ""))
    for cmd, out in phase1.get("mcu_stop_commands", {}).items():
        section(f"PHASE1 MCU STOP CMD: {cmd}", out)

    phase2 = steps.get("phase2", {})
    section("PHASE2 MPU SERVER START", phase2.get("mpu_server_start", {}).get("output", ""))
    section("PHASE2 MCU CLIENT", phase2.get("mcu_client", {}).get("output", ""))
    section("PHASE2 MPU ASYNC OUTPUT", phase2.get("mpu_async_after_client", {}).get("output", ""))
    section("PHASE2 MPU STOP CTRL+C", phase2.get("mpu_stop_ctrl_c", {}).get("output", ""))

    diagnostics = report.get("diagnostics", {})
    for phase_tag in ["phase1_before", "phase1_after", "phase2_after"]:
        diag = diagnostics.get(phase_tag, {})
        section(f"DIAG {phase_tag.upper()} MCU STATS", diag.get("mcu", {}).get("output", ""))
        for entry in diag.get("mpu", {}).get("commands", []):
            section(
                f"DIAG {phase_tag.upper()} MPU: {entry.get('command', '')}",
                entry.get("output", ""),
            )

    print("\n" + "-" * W)
    print("HIGHLIGHTS")
    print("-" * W)
    for key, lines in report.get("highlights", {}).items():
        print(f"[{key}]")
        if lines:
            for line in lines:
                print(f"  {line}")
        else:
            print("  <no key lines found>")


def save_report_files(report: Dict[str, object], out_dir: Path) -> Dict[str, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = out_dir / f"tcp_iperf_report_{stamp}.json"
    txt_path = out_dir / f"tcp_iperf_report_{stamp}.txt"

    json_path.write_text(json.dumps(report, indent=2, ensure_ascii=True), encoding="utf-8")

    chunks: List[str] = []
    chunks.append("DUAL TARGET TCP IPERF REPORT\n")
    kpis = report.get("kpis", {})
    chunks.append("KPI SUMMARY\n")
    chunks.append(f"  {kpis.get('phase1', {}).get('compact_line', 'PHASE1: n/a')}\n")
    chunks.append(f"  {kpis.get('phase2', {}).get('compact_line', 'PHASE2: n/a')}\n")
    chunks.append(json.dumps(report.get("config", {}), indent=2, ensure_ascii=True) + "\n")

    steps = report.get("steps", {})
    phase1 = steps.get("phase1", {})
    phase2 = steps.get("phase2", {})

    def append_block(title: str, body: str) -> None:
        chunks.append("\n" + "=" * 40 + "\n")
        chunks.append(title + "\n")
        chunks.append("=" * 40 + "\n")
        chunks.append((body.rstrip() if body.strip() else "<no output>") + "\n")

    append_block("PHASE1 MCU SERVER START", phase1.get("mcu_server_start", {}).get("output", ""))
    append_block("PHASE1 MPU CLIENT", phase1.get("mpu_client", {}).get("output", ""))
    append_block("PHASE1 MCU ASYNC OUTPUT", phase1.get("mcu_async_after_client", {}).get("output", ""))
    for cmd, out in phase1.get("mcu_stop_commands", {}).items():
        append_block(f"PHASE1 MCU STOP CMD: {cmd}", out)

    append_block("PHASE2 MPU SERVER START", phase2.get("mpu_server_start", {}).get("output", ""))
    append_block("PHASE2 MCU CLIENT", phase2.get("mcu_client", {}).get("output", ""))
    append_block("PHASE2 MPU ASYNC OUTPUT", phase2.get("mpu_async_after_client", {}).get("output", ""))
    append_block("PHASE2 MPU STOP CTRL+C", phase2.get("mpu_stop_ctrl_c", {}).get("output", ""))

    diagnostics = report.get("diagnostics", {})
    for phase_tag in ["phase1_before", "phase1_after", "phase2_after"]:
        diag = diagnostics.get(phase_tag, {})
        append_block(
            f"DIAG {phase_tag.upper()} MCU STATS",
            diag.get("mcu", {}).get("output", ""),
        )
        for entry in diag.get("mpu", {}).get("commands", []):
            append_block(
                f"DIAG {phase_tag.upper()} MPU: {entry.get('command', '')}",
                entry.get("output", ""),
            )

    chunks.append("\n" + "=" * 40 + "\nHIGHLIGHTS\n" + "=" * 40 + "\n")
    for key, lines in report.get("highlights", {}).items():
        chunks.append(f"[{key}]\n")
        for line in lines:
            chunks.append(f"  {line}\n")
        if not lines:
            chunks.append("  <no key lines found>\n")

    txt_path.write_text("".join(chunks), encoding="utf-8")
    return {"json": json_path, "txt": txt_path}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run bidirectional TCP iperf tests between MCU (COM8) and MPU (COM9) "
            "via serial CLI. No -u flag — TCP mode."
        ),
    )
    parser.add_argument("--mcu-port", default="COM8", help="MCU serial port (default: COM8)")
    parser.add_argument("--mpu-port", default="COM9", help="MPU serial port (default: COM9)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--mcu-ip", default="192.168.0.200", help="MCU IP (default: 192.168.0.200)")
    parser.add_argument("--mpu-ip", default="192.168.0.5", help="MPU IP (default: 192.168.0.5)")
    parser.add_argument(
        "--iperf-duration",
        type=float,
        default=10.0,
        help="TCP test duration in seconds for MPU client (default: 10)",
    )
    parser.add_argument(
        "--tcp-window",
        default=None,
        help="TCP window size, e.g. 64K or 256K (optional, passed as -w to iperf)",
    )
    parser.add_argument(
        "--parallel",
        type=int,
        default=1,
        help="Number of parallel iperf streams -P (default: 1)",
    )
    parser.add_argument(
        "--client-timeout",
        type=float,
        default=40.0,
        help="Max wait for iperf client to finish (default: 40s)",
    )
    parser.add_argument(
        "--client-idle-timeout",
        type=float,
        default=12.0,
        help="Idle timeout for Linux iperf client output collection (default: 12s)",
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
        report = run_test(args)
        print_report_to_stdout(report)
        out_paths = save_report_files(report, Path(args.out_dir))
        print("\n[INFO] Report files written:")
        print(f"  JSON: {out_paths['json']}")
        print(f"  TXT : {out_paths['txt']}")
        return 0
    except serial.SerialException as exc:
        print(f"[ERROR] Serial connection failed: {exc}")
        return 2
    except Exception as exc:  # noqa: BLE001
        print(f"[ERROR] Test failed: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
