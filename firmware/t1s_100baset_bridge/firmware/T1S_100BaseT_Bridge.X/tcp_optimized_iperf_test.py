#!/usr/bin/env python3
"""
Bidirectional TCP iperf test with buffer/window size optimisation.

Based on tcp_dual_target_iperf_test.py.

Key additions vs. the base script:
  1. MCU socket buffer tuning via ``iperfs -tx <N> -rx <N>`` before each phase
     (default: 16384 Byte TX + RX, up from the firmware default of 4096 Byte).
  2. TCP window size passed to every iperf command via ``-w`` (default: 16K).
  3. New CLI args:  --mcu-tx-buffer, --mcu-rx-buffer, --tcp-window
     (--tcp-window now defaults to 16K instead of None).

Performance model:
  Max. TCP throughput ≈ window_size / RTT
  With 16 KB window and ~4 ms RTT  ≈ 32 Mbit/s ceiling (well above link limit).
  The binding constraint is the MCU RX buffer (rxBuffSize in iperfs) because the
  TCP stack advertises RWND = rxBuffSize to the remote sender.

Targets:
  MCU CLI on COM8  (ATSAME54, 192.168.0.200)
  MPU Linux CLI on COM9  (192.168.0.5)

Flow:
  0. Tune MCU buffers (iperfs)
  Phase 1:  MCU = TCP server,  MPU = TCP client
  Phase 2:  MPU = TCP server,  MCU = TCP client

Report files:
  tcp_optimized_report_<timestamp>.txt
  tcp_optimized_report_<timestamp>.json
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
# Serial helper (identical to base script)
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
# Buffer tuning (NEW)
# ---------------------------------------------------------------------------

def tune_mcu_buffers(
    mcu: SerialCLI,
    tx_bytes: Optional[int] = None,
    rx_bytes: Optional[int] = None,
) -> Dict[str, object]:
    """Send ``iperfs`` to the MCU to adjust TX and/or RX socket buffers.

    Only the arguments that are explicitly provided are added to the command.

    Background
    ----------
    - ``iperfs -rx N``: sets rxBuffSize, which the Harmony TCP server socket
      tries to apply as TCP_OPTION_RX_BUFF (controls RWND advertised to the
      remote sender).  May fail on some builds with "Set of RX buffer size
      failed" — in that case the firmware default (4096 B) stays in effect.
    - ``iperfs -tx N``: sets txBuffSize, applied as TCP_OPTION_TX_BUFF on
      the client socket.  Enlarging this buffer beyond 4096 B has been
      observed to cause catastrophic TCP throughput regression on the MCU
      (65 Kbps instead of ~4 Mbit/s) — avoid for MCU-as-client phases.

    Valid range: 1 … 65535 bytes.
    """
    if tx_bytes is None and rx_bytes is None:
        raise ValueError("At least one of tx_bytes or rx_bytes must be given")

    parts = []
    if tx_bytes is not None:
        parts.append(f"-tx {tx_bytes}")
    if rx_bytes is not None:
        parts.append(f"-rx {rx_bytes}")
    cmd = "iperfs " + " ".join(parts)

    label_parts = []
    if tx_bytes is not None:
        label_parts.append(f"TX={tx_bytes} B")
    if rx_bytes is not None:
        label_parts.append(f"RX={rx_bytes} B")
    print(f"\n[TUNE] MCU buffer tuning: {cmd}  ({', '.join(label_parts)})")

    res = mcu.run_command(
        command=cmd,
        overall_timeout=5.0,
        idle_timeout=0.8,
        stop_on_prompt=True,
        live_label="MCU",
    )
    ok = "iperfs: OK" in res.output
    print(f"  → {'OK' if ok else 'WARNING — no OK response'}")
    return {
        "command": cmd,
        "output": res.output,
        "timed_out": res.timed_out,
        "ok": ok,
        "tx_bytes": tx_bytes,
        "rx_bytes": rx_bytes,
    }


# ---------------------------------------------------------------------------
# TCP KPI parsers (identical to base script)
# ---------------------------------------------------------------------------

def _unit_to_mbps(value: float, unit: str) -> float:
    normalized = unit.strip().lower()
    if normalized.startswith("k"):
        return value / 1000.0
    if normalized.startswith("g"):
        return value * 1000.0
    return value


def parse_mcu_tcp_summary(text: str) -> Dict[str, object]:
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
# Diagnostics (identical to base script)
# ---------------------------------------------------------------------------

def collect_mpu_diagnostics(mpu: SerialCLI, tag: str) -> Dict[str, object]:
    commands = ["ip -s link", "ifconfig -a", "cat /proc/net/snmp"]
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
        r"iperfs: OK",
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
# iperf command builders
# ---------------------------------------------------------------------------

def _mcu_server_cmd(args: argparse.Namespace) -> str:
    # MCU iperf has no -w flag; window is controlled by iperfs (rxBuffSize)
    return "iperf -s"


def _mpu_client_cmd(args: argparse.Namespace) -> str:
    cmd = f"iperf -c {args.mcu_ip} -t {int(args.iperf_duration)}"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    if args.parallel > 1:
        cmd += f" -P {args.parallel}"
    return cmd


def _mpu_server_cmd(args: argparse.Namespace) -> str:
    cmd = "iperf -s"
    if args.tcp_window:
        cmd += f" -w {args.tcp_window}"
    if args.parallel > 1:
        cmd += f" -P {args.parallel}"
    return cmd


def _mcu_client_cmd(args: argparse.Namespace) -> str:
    # MCU iperf has no -w flag; window controlled by iperfs (txBuffSize)
    return f"iperf -c {args.mpu_ip}"


# ---------------------------------------------------------------------------
# Main test runner
# ---------------------------------------------------------------------------

def run_test(args: argparse.Namespace) -> Dict[str, object]:
    mcu = SerialCLI(port=args.mcu_port, baudrate=args.baudrate, timeout=0.2)
    mpu = SerialCLI(port=args.mpu_port, baudrate=args.baudrate, timeout=0.2)

    report: Dict[str, object] = {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "protocol": "TCP",
        "optimisation": {
            "phase1_mcu_rx_buffer_bytes": args.mcu_rx_buffer,
            "phase2_mcu_tx_buffer_bytes": 4096,
            "phase2_mcu_rx_buffer_bytes": 4096,
            "linux_tcp_window": args.tcp_window or "default",
            "note": (
                "Phase 1 (MCU server): MCU RX buffer enlarged to mcu_rx_buffer for better RWND. "
                "Phase 2 (MCU client): MCU buffers reset to 4096 B — "
                "enlarging MCU TX buffer causes catastrophic TCP regression (~65 Kbps). "
                "Linux -w sets the Linux-side socket window (effective for both phases)."
            ),
        },
        "config": {
            "mcu_port": args.mcu_port,
            "mpu_port": args.mpu_port,
            "baudrate": args.baudrate,
            "mcu_ip": args.mcu_ip,
            "mpu_ip": args.mpu_ip,
            "iperf_duration": args.iperf_duration,
            "tcp_window": args.tcp_window or "default",
            "parallel_streams": args.parallel,
            "mcu_tx_buffer": args.mcu_tx_buffer,
            "mcu_rx_buffer": args.mcu_rx_buffer,
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

        # ── Step 0: MCU RX buffer tuning (Phase 1: MCU is server) ───────────
        # Only set the RX buffer here — enlarging the TX buffer on the MCU
        # client socket (Phase 2) causes catastrophic TCP throughput regression.
        print(
            f"\n[TUNE] Setting MCU RX buffer for Phase 1 (MCU as server): "
            f"RX={args.mcu_rx_buffer} B  (firmware default: 4096 B)"
        )
        tune_result = tune_mcu_buffers(mcu, rx_bytes=args.mcu_rx_buffer)
        report["steps"]["mcu_buffer_tuning"] = tune_result

        if not tune_result["ok"]:
            print(
                "  [WARNING] MCU did not confirm RX buffer tuning — "
                "firmware default (4096 B) stays in effect"
            )

        # ── diagnostics before Phase 1 ─────────────────────────────────────
        print("\n[DIAG] phase1_before — collecting stats ...")
        report["diagnostics"]["phase1_before"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase1_before"),
            "mcu": collect_mcu_stats(mcu, "phase1_before"),
        }

        # ── Phase 1: MCU TCP server, MPU TCP client ─────────────────────────
        print("\n[STEP] Phase 1: MCU TCP server + MPU TCP client")
        print(
            f"  MCU rx_buffer={args.mcu_rx_buffer} B → advertised RWND to MPU sender\n"
            f"  MPU -w {args.tcp_window or 'default'} → MPU socket window"
        )

        srv_cmd = _mcu_server_cmd(args)
        mcu_server_start = mcu.run_command(
            command=srv_cmd,
            overall_timeout=8.0,
            idle_timeout=0.8,
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

        # ── diagnostics after Phase 1 ──────────────────────────────────────
        print("\n[DIAG] phase1_after — collecting stats ...")
        report["diagnostics"]["phase1_after"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase1_after"),
            "mcu": collect_mcu_stats(mcu, "phase1_after"),
        }

        # ── Phase 2 buffer reset: restore MCU TX buffer to firmware default ──
        # DO NOT enlarge the MCU TX buffer for Phase 2 (MCU is client):
        # setting TCP_OPTION_TX_BUFF > 4096 causes catastrophic TCP regression
        # (~65 Kbps instead of ~4 Mbit/s). Reset to firmware default 4096 B.
        print(f"\n[TUNE] Resetting MCU TX buffer to default (4096 B) for Phase 2 (MCU as client) ...")
        tune_result_p2 = tune_mcu_buffers(mcu, tx_bytes=4096, rx_bytes=4096)
        report["steps"]["mcu_buffer_tuning_phase2"] = tune_result_p2

        mpu.clear_input()

        # ── Phase 2: MPU TCP server, MCU TCP client ─────────────────────────
        print("\n[STEP] Phase 2: MPU TCP server + MCU TCP client")
        print(
            f"  MCU tx_buffer={args.mcu_tx_buffer} B → MCU in-flight window\n"
            f"  MPU -w {args.tcp_window or 'default'} → MPU rx window (RWND advertised to MCU)"
        )

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

        # ── diagnostics after Phase 2 ──────────────────────────────────────
        print("\n[DIAG] phase2_after — collecting stats ...")
        report["diagnostics"]["phase2_after"] = {
            "mpu": collect_mpu_diagnostics(mpu, "phase2_after"),
            "mcu": collect_mcu_stats(mcu, "phase2_after"),
        }

        # ── KPIs ──────────────────────────────────────────────────────────
        report["kpis"] = build_phase_kpis(
            phase1_mcu_text=mcu_server_start.output + "\n" + mcu_after_client.output,
            phase1_mpu_text=mpu_client.output,
            phase2_mpu_text=mpu_server_start.output + "\n" + mpu_after_client.output,
            phase2_mcu_text=mcu_client.output,
        )

        report["highlights"] = {
            "buffer_tuning": extract_key_lines(tune_result["output"] + "\n" + tune_result_p2["output"]),
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
    print("DUAL TARGET TCP IPERF REPORT — OPTIMISED BUFFERS / WINDOW")
    print("=" * W)

    opt = report.get("optimisation", {})
    print("BUFFER / WINDOW OPTIMISATION")
    print(f"  MCU RX buffer (Phase1) : {opt.get('phase1_mcu_rx_buffer_bytes')} B  ← controls RWND advertised to Linux")
    print(f"  MCU TX buffer (Phase2) : {opt.get('phase2_mcu_tx_buffer_bytes')} B  ← reset to firmware default")
    print(f"  MCU RX buffer (Phase2) : {opt.get('phase2_mcu_rx_buffer_bytes')} B  ← reset to firmware default")
    print(f"  Linux -w               : {opt.get('linux_tcp_window')}")
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

    section("MCU BUFFER TUNING (Phase 1)", steps.get("mcu_buffer_tuning", {}).get("output", ""))
    section("MCU BUFFER TUNING (Phase 2)", steps.get("mcu_buffer_tuning_phase2", {}).get("output", ""))

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
    json_path = out_dir / f"tcp_optimized_report_{stamp}.json"
    txt_path  = out_dir / f"tcp_optimized_report_{stamp}.txt"

    json_path.write_text(json.dumps(report, indent=2, ensure_ascii=True), encoding="utf-8")

    chunks: List[str] = []
    chunks.append("DUAL TARGET TCP IPERF REPORT — OPTIMISED BUFFERS / WINDOW\n")

    opt = report.get("optimisation", {})
    chunks.append(f"MCU RX buffer (Phase1) : {opt.get('phase1_mcu_rx_buffer_bytes')} B  <- controls RWND advertised to Linux\n")
    chunks.append(f"MCU TX buffer (Phase2) : {opt.get('phase2_mcu_tx_buffer_bytes')} B  <- reset to firmware default\n")
    chunks.append(f"MCU RX buffer (Phase2) : {opt.get('phase2_mcu_rx_buffer_bytes')} B  <- reset to firmware default\n")
    chunks.append(f"Linux -w               : {opt.get('linux_tcp_window')}\n")

    kpis = report.get("kpis", {})
    chunks.append("\nKPI SUMMARY\n")
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

    append_block("MCU BUFFER TUNING (Phase 1)", steps.get("mcu_buffer_tuning", {}).get("output", ""))
    append_block("MCU BUFFER TUNING (Phase 2)", steps.get("mcu_buffer_tuning_phase2", {}).get("output", ""))
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
        append_block(f"DIAG {phase_tag.upper()} MCU STATS", diag.get("mcu", {}).get("output", ""))
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
            "Bidirectional TCP iperf test with buffer/window optimisation.\n"
            "Sets MCU socket buffers via 'iperfs' before each phase and\n"
            "passes -w to Linux iperf for a larger TCP window.\n\n"
            "Performance model:\n"
            "  Max. throughput ≈ min(MCU_rx_buffer, linux_window) / RTT\n"
            "  MCU rxBuffSize → RWND advertised to Linux sender (Phase 1)\n"
            "  MCU txBuffSize → in-flight data MCU can have pending (Phase 2)\n"
            "  Linux -w       → Linux socket window (both phases)"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--mcu-port",  default="COM8",          help="MCU serial port (default: COM8)")
    parser.add_argument("--mpu-port",  default="COM9",          help="MPU serial port (default: COM9)")
    parser.add_argument("--baudrate",  type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--mcu-ip",    default="192.168.0.200", help="MCU IP (default: 192.168.0.200)")
    parser.add_argument("--mpu-ip",    default="192.168.0.5",   help="MPU IP (default: 192.168.0.5)")
    parser.add_argument(
        "--iperf-duration", type=float, default=10.0,
        help="TCP test duration in seconds (default: 10)",
    )
    parser.add_argument(
        "--tcp-window", default="16K",
        help=(
            "Linux iperf TCP window size passed as -w, e.g. 16K, 32K, 64K "
            "(default: 16K). Has no effect on MCU (use --mcu-rx-buffer instead)."
        ),
    )
    parser.add_argument(
        "--mcu-tx-buffer", type=int, default=16384,
        help=(
            "MCU iperf TX socket buffer in bytes, set via 'iperfs -tx' "
            "(default: 16384; firmware default: 4096). "
            "Relevant for Phase 2 where MCU is the sender."
        ),
    )
    parser.add_argument(
        "--mcu-rx-buffer", type=int, default=16384,
        help=(
            "MCU iperf RX socket buffer in bytes, set via 'iperfs -rx' "
            "(default: 16384; firmware default: 4096). "
            "Controls the RWND advertised to the Linux sender in Phase 1."
        ),
    )
    parser.add_argument(
        "--parallel",       type=int, default=1,
        help="Number of parallel iperf streams -P on Linux side (default: 1)",
    )
    parser.add_argument(
        "--client-timeout", type=float, default=40.0,
        help="Max wait for iperf client to finish (default: 40s)",
    )
    parser.add_argument(
        "--client-idle-timeout", type=float, default=12.0,
        help="Idle timeout for Linux iperf client output collection (default: 12s)",
    )
    parser.add_argument(
        "--out-dir", default=".",
        help="Directory for report files (default: current directory)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    print(
        f"\n[CONFIG] MCU buffer: TX={args.mcu_tx_buffer} B  RX={args.mcu_rx_buffer} B  "
        f"Linux -w={args.tcp_window}  parallel={args.parallel}"
    )
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
