#!/usr/bin/env python3
"""
MPU RX Performance Diagnostic Test Suite
Automated diagnostic runner based on MPU RX Diagnostic Plan

Systematically analyzes 85% packet loss issue on MCU→MPU T1S bridge
Tests interrupt load, network stack limits, memory pressure, hardware registers,
and kernel parameter optimization.

Usage:
  python mpu_rx_diagnostic_test.py
  python mpu_rx_diagnostic_test.py --mcu-port COM8 --mpu-port COM9
  python mpu_rx_diagnostic_test.py --skip-phases 1,2 --test-duration 15
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Set

import serial


@dataclass
class TestResult:
    """Single test result with timing and success info."""
    phase: str
    step: str
    command: str
    output: str
    success: bool
    duration_sec: float
    error_message: str = ""


@dataclass 
class DiagnosticReport:
    """Complete diagnostic report with all phases."""
    timestamp: str
    config: Dict[str, object] = field(default_factory=dict)
    baseline: Dict[str, object] = field(default_factory=dict)
    phases: Dict[str, object] = field(default_factory=dict)
    optimization_results: Dict[str, object] = field(default_factory=dict)
    analysis: Dict[str, object] = field(default_factory=dict)
    recommendations: List[str] = field(default_factory=list)


class SerialCLI:
    """Serial command line interface for MCU/MPU control."""
    
    def __init__(self, port: str, baudrate: int, timeout: float = 0.3):
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
        if not self.ser:
            return
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

    def login(self, username: str, password: str, timeout: float = 30.0) -> bool:
        """Handle Linux login prompt if the system has just booted.

        Sends an initial newline, then waits for either a shell prompt
        (already logged in) or a 'login:' / 'Password:' sequence.
        Returns True when a shell prompt is detected, False on timeout.
        """
        if not self.ser:
            raise RuntimeError(f"Serial port {self.port} is not open")

        self.ser.write(b"\n")
        self.ser.flush()

        buf = ""
        end_time = time.time() + timeout
        while time.time() < end_time:
            chunk = self.ser.read_all().decode("utf-8", errors="ignore")
            if chunk:
                buf += chunk

            # Already at a shell prompt
            if re.search(r'[#\$]\s*$', buf):
                return True

            # Login prompt
            if re.search(r'login:\s*$', buf, re.IGNORECASE):
                self.ser.write((username + "\n").encode("utf-8", errors="ignore"))
                self.ser.flush()
                buf = ""
                time.sleep(0.5)
                continue

            # Password prompt
            if re.search(r'[Pp]assword:\s*$', buf):
                self.ser.write((password + "\n").encode("utf-8", errors="ignore"))
                self.ser.flush()
                buf = ""
                time.sleep(1.0)
                continue

            time.sleep(0.2)

        return False

    def run_command(
        self,
        command: str,
        timeout: float = 10.0,
        expect_prompt: bool = True,
        live_output: bool = False,
        label: str = ""
    ) -> TestResult:
        """Execute command and return structured result."""
        start_time = time.time()
        
        if not self.ser:
            return TestResult(
                phase="", step="", command=command, output="",
                success=False, duration_sec=0.0,
                error_message="Serial port not open"
            )

        self.clear_input()
        if live_output and label:
            print(f"[{label}] $ {command}")

        try:
            self.write_line(command)
            output = ""
            end_time = start_time + timeout
            
            while time.time() < end_time:
                chunk = self.ser.read_all().decode("utf-8", errors="ignore")
                if chunk:
                    output += chunk
                    if live_output and label:
                        # Print new lines as they arrive
                        for line in chunk.splitlines():
                            if line.strip():
                                print(f"[{label}] {line}")
                    
                    # Check for prompt (simple heuristic)
                    if expect_prompt and re.search(r'[>#$]\s*$', output):
                        break
                
                time.sleep(0.1)

            duration = time.time() - start_time
            success = not ("command not found" in output.lower() or 
                          "no such file" in output.lower() or
                          "error" in output.lower())
            
            return TestResult(
                phase="", step="", command=command, output=output,
                success=success, duration_sec=duration
            )
            
        except Exception as e:
            duration = time.time() - start_time
            return TestResult(
                phase="", step="", command=command, output="",
                success=False, duration_sec=duration,
                error_message=str(e)
            )


class MPURXDiagnosticRunner:
    """Main diagnostic test runner implementing the diagnostic plan."""
    
    def __init__(
        self,
        mcu_port: str = "COM8",
        mpu_port: str = "COM9", 
        baudrate: int = 115200,
        mcu_ip: str = "192.168.0.200",
        mpu_ip: str = "192.168.0.5",
        test_duration: int = 30,
        skip_phases: Optional[Set[int]] = None
    ):
        self.mcu = SerialCLI(mcu_port, baudrate)
        self.mpu = SerialCLI(mpu_port, baudrate)
        self.mcu_ip = mcu_ip
        self.mpu_ip = mpu_ip
        self.test_duration = test_duration
        self.skip_phases = skip_phases or set()
        
        self.report = DiagnosticReport(
            timestamp=datetime.now().isoformat(),
            config={
                "mcu_port": mcu_port,
                "mpu_port": mpu_port, 
                "baudrate": baudrate,
                "mcu_ip": mcu_ip,
                "mpu_ip": mpu_ip,
                "test_duration": test_duration,
                "skip_phases": list(self.skip_phases) if self.skip_phases else [],
            }
        )

    def run_diagnostic(self) -> DiagnosticReport:
        """Execute complete diagnostic test suite."""
        print("🔍 MPU RX Performance Diagnostic Test Suite")
        print("=" * 60)
        print(f"Timestamp: {self.report.timestamp}")
        print(f"Configuration: {json.dumps(self.report.config, indent=2)}")
        print("=" * 60)

        try:
            print("[INFO] Opening serial connections...")
            self.mcu.open()
            self.mpu.open()
            
            # Wake up / log in MPU
            print("[INFO] Synchronizing prompts (MPU login if needed)...")
            self.mpu.clear_input()
            mpu_ready = self.mpu.login(username="root", password="microchip", timeout=30.0)
            if mpu_ready:
                print("[INFO] MPU shell ready.")
            else:
                print("[WARN] MPU login timed out – continuing anyway.")

            # Wake up MCU
            self.mcu.clear_input()
            self.mcu.write_line("")
            time.sleep(0.5)

            # Ensure MCU forward mode is OFF before any test.
            # The MCU firmware's iperf may re-enable forwarding or reinitialise
            # the T1S stack; issuing fwd 0 here puts it into a known-good state.
            print("[INFO] Setting MCU to passthrough mode (fwd 0)...")
            self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True)
            time.sleep(1.0)

            # Execute diagnostic phases
            self._run_phase0_connectivity_check()

            if 1 not in self.skip_phases:
                self._run_phase1_baseline()
                
            if 2 not in self.skip_phases:
                self._run_phase2_interrupt_analysis()
                
            if 3 not in self.skip_phases:
                self._run_phase3_network_stack()
                
            if 4 not in self.skip_phases:
                self._run_phase4_hardware_diagnostics()
                
            if 5 not in self.skip_phases:
                self._run_phase5_kernel_optimization()
                
            self._run_phase6_analysis_and_report()

            # Register snapshot intentionally runs LAST.
            # lan_read corrupts TC6 SPI state machine permanently (confirmed
            # 2026-03-18) — all iperf measurements must be complete before this.
            self._run_final_register_snapshot()

        except Exception as e:
            print(f"[ERROR] Diagnostic failed: {e}")
            self.report.analysis["fatal_error"] = str(e)
            
        finally:
            print("[INFO] Closing serial connections...")
            self.mcu.close()
            self.mpu.close()

        return self.report

    def _wait_for_mcu_iperf_idle(self, iperf_start_time: float, min_idle_secs: int = 15) -> None:
        """Wait until MCU iperf is expected to have finished.

        The MCU RTOS CLI returns the shell prompt immediately while iperf runs
        as a background task (default iperf duration is 10 seconds). Block until
        at least min_idle_secs have elapsed since iperf was started, so the next
        phase does not see 'iperf: All instances busy'.
        """
        elapsed = time.time() - iperf_start_time
        remaining = min_idle_secs - elapsed
        if remaining > 0.5:
            print(f"    [MCU] Waiting {remaining:.0f}s for MCU iperf to finish...")
            time.sleep(remaining)
        # Send Ctrl+C in case the iperf instance is still alive, then restore
        # passthrough mode so the T1S stack is back in its known-good state.
        self.mcu.write_ctrl_c()
        time.sleep(0.3)
        self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True)
        time.sleep(0.5)

    def _mcu_ping_and_wait(self, target_ip: str, timeout: float = 20.0,
                           label: str = "MCU-PING") -> bool:
        """Send a ping from the MCU and wait for the async 'Ping: done.' line.

        The MCU RTOS ping command is non-blocking: it immediately returns the
        '>' shell prompt while the ping runs in the background.  Waiting for
        the prompt (as run_command does) therefore returns before any replies
        arrive.  This helper bypasses prompt detection and instead polls until
        'Ping: done.' appears or the timeout expires.

        Returns True if at least one reply was received.
        """
        if not self.mcu.ser:
            return False

        print(f"[{label}] $ ping {target_ip}")
        self.mcu.clear_input()
        self.mcu.write_line(f"ping {target_ip}")

        buf = ""
        end_time = time.time() + timeout
        while time.time() < end_time:
            chunk = self.mcu.ser.read_all().decode("utf-8", errors="ignore")
            if chunk:
                buf += chunk
                for line in chunk.splitlines():
                    if line.strip():
                        print(f"[{label}] {line}")
            if "Ping: done." in buf:
                break
            time.sleep(0.2)

        rx_match = re.search(r'received (\d+) replies', buf)
        ok = bool(rx_match and int(rx_match.group(1)) > 0)
        if ok:
            print(f"[{label}] ✓ Ping done — {rx_match.group(1)} replies")
        else:
            if "Ping: done." in buf:
                print(f"[{label}] ✗ Ping done but 0 replies")
            else:
                print(f"[{label}] ✗ Ping timed out (no 'Ping: done.' in {timeout:.0f}s)")
        return ok

    def _mpu_ping_mcu(self, label: str = "MPU-PING") -> tuple:
        """Ping MCU from MPU. Returns (result, ping_ok, rx_match)."""
        result = self.mpu.run_command(
            f"ping -c 4 -W 2 {self.mcu_ip} 2>&1",
            timeout=20.0, live_output=True, label=label
        )
        rx_match = re.search(r'(\d+) received', result.output)
        ok = bool(rx_match and int(rx_match.group(1)) > 0)
        return result, ok, rx_match

    def _run_phase0_connectivity_check(self) -> None:
        """Phase 0: Pre-flight T1S connectivity check.

        NOTE: lan_read calls are deliberately absent here.
        Testing confirmed that any lan_read (SPI/TC6 register access) races
        with the kernel LAN865x ISR on the SPI bus and corrupts the TC6 state
        machine, breaking T1S connectivity immediately.  Register reads happen
        in Phase 1; the link is restored by a recovery ping at the end of
        Phase 1 before Phase 2 iperf starts.
        """
        print("\n🔌 Phase 0: T1S Connectivity Pre-Check")
        print("-" * 40)

        # 0.1  MCU → MPU ping  (kick PLCA bus; MCU is coordinator/node 0)
        # MCU ping is asynchronous — shell returns '>' immediately.
        # _mcu_ping_and_wait() blocks until 'Ping: done.' or 20 s timeout.
        print(f"  0.1 MCU → MPU ping ({self.mpu_ip}) — kick PLCA bus...")
        mcu_ping_ok = self._mcu_ping_and_wait(self.mpu_ip, timeout=20.0, label="MCU-PING-0.1")
        if mcu_ping_ok:
            print("  ✓ MCU → MPU OK — PLCA bus active")
        else:
            print("  ⚠ MCU → MPU: 0 replies — PLCA may not be running")
        time.sleep(0.5)  # let ARP settle

        # 0.2  MPU → MCU ping  (bidirectional check)
        print(f"  0.2 MPU → MCU ping ({self.mcu_ip}) — bidirectional check...")
        ping_result, ping_ok, rx_match = self._mpu_ping_mcu("MPU-PING-0.2")
        if ping_ok:
            print(f"  ✓ T1S link UP — {rx_match.group(1)}/4 replies")
        else:
            print("  ✗ T1S link DOWN: MCU did not respond to ping!")
            print("    ↻ Recovery: fwd 0 + MCU kick...")
            self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True)
            time.sleep(1.0)
            self._mcu_ping_and_wait(self.mpu_ip, timeout=20.0, label="MCU-RECOVER-0")
            time.sleep(1.0)
            ping_result, ping_ok, rx_match = self._mpu_ping_mcu("MPU-PING-RETRY-0")
            if ping_ok:
                print(f"  ✓ Recovery OK: {rx_match.group(1)}/4 replies")
            else:
                print("  ✗ Recovery failed — power cycle both boards")
                print("    Continuing to collect static data anyway...")

        self.report.baseline["connectivity_check"] = {
            "mcu_ping_ok":   mcu_ping_ok,
            "mpu_ping_ok":   ping_ok,
            "ping_result":   ping_result.output.strip(),
            "t1s_link":      "up" if ping_ok else "down",
            "mcu_reachable": "yes" if ping_ok else "no",
        }
        print(f"  Phase 0 complete. T1S link: {'UP ✓' if ping_ok else 'DOWN ✗'}")

    def _run_phase1_baseline(self) -> None:
        """Phase 1: Baseline System Information."""
        print("\n📊 Phase 1: Baseline System Information")
        print("-" * 40)
        
        phase_data = {"start_time": datetime.now().isoformat(), "results": []}

        # 1.1 System Status Collection
        print("  1.1 System Status Collection...")
        system_commands = [
            "cat /proc/cpuinfo | grep -E '(processor|model name|cpu MHz|bogomips)'",
            "cat /proc/meminfo | grep -E '(MemTotal|MemFree|MemAvailable|Buffers|Cached)'", 
            "cat /proc/version",
            "uname -a",
            "ip addr show eth0",
            "ip link show eth0", 
            "ifconfig eth0",
            "cat /proc/sys/net/core/netdev_max_backlog",
            "cat /proc/sys/net/core/rmem_default",
            "cat /proc/sys/net/core/rmem_max",
            "cat /proc/sys/net/core/netdev_budget",
        ]
        
        for cmd in system_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU")
            result.phase = "Phase1"
            result.step = "SystemInfo"
            phase_data["results"].append({
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # NOTE: Hardware register snapshot (lan_read) is intentionally NOT done here.
        # EMPIRICALLY CONFIRMED (2026-03-18): any lan_read call races with the
        # kernel LAN865x ISR (IRQ 37, spi0.0) on the TC6 SPI bus, permanently
        # corrupting the TC6 state machine. Recovery (eth0 bounce) does not help.
        # Only power-cycle restores the link.
        # Register snapshot is done in _run_final_register_snapshot() which runs
        # AFTER all iperf phases, so it doesn't matter if it breaks the link.
        print("  1.2 Hardware register snapshot deferred to end of test (see final phase).")

        phase_data["end_time"] = datetime.now().isoformat()
        self.report.phases["phase1_baseline"] = phase_data
        print(f"  ✓ Phase 1 completed: {len(phase_data['results'])} commands executed")

    def _run_phase2_interrupt_analysis(self) -> None:
        """Phase 2: Interrupt and CPU Load Analysis."""
        print("\n🚨 Phase 2: Interrupt and CPU Load Analysis")
        print("-" * 40)
        
        phase_data = {"start_time": datetime.now().isoformat(), "results": []}

        # 2.1 Pre-Test Interrupt Baseline
        print("  2.1 Pre-Test Interrupt Baseline...")
        baseline_commands = [
            "cat /proc/interrupts",
            "cat /proc/stat | grep ctxt",
            "cat /proc/loadavg",
            "cat /proc/net/dev | grep eth0",
        ]
        
        baseline_data = {}
        for cmd in baseline_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-BASE")
            baseline_data[cmd] = result.output
            phase_data["results"].append({
                "step": "baseline",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # 2.2 Concurrent iperf + monitoring test
        print(f"  2.2 Running {self.test_duration}s iperf test with monitoring...")
        
        # Start MPU iperf server
        print("    Starting MPU iperf server...")
        iperf_start = self.mpu.run_command(
            "iperf -s -u -i 1 > /tmp/iperf_server.log 2>&1 &",
            timeout=5.0, expect_prompt=True, live_output=True, label="MPU-IPERF"
        )
        time.sleep(2)  # Give server time to start

        # Start monitoring scripts on MPU
        print("    Starting monitoring scripts...")
        monitor_commands = [
            "while sleep 1; do echo \"$(date '+%H:%M:%S'): $(cat /proc/interrupts | grep -E 'eth0|lan865' | head -3)\"; done > /tmp/irq_monitor.log &",
            "while sleep 1; do echo \"$(date '+%H:%M:%S'): Load=$(cat /proc/loadavg), NetDev=$(cat /proc/net/dev | grep eth0)\"; done > /tmp/system_load.log &",
        ]
        
        for cmd in monitor_commands:
            result = self.mpu.run_command(cmd, timeout=3.0, expect_prompt=True, label="MPU-MON")
            phase_data["results"].append({
                "step": "monitoring_start",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # Start MCU iperf client (this should cause 85% packet loss)
        print("    Starting MCU iperf client...")
        # Restore passthrough mode: the MCU iperf command may reinitialise the
        # T1S stack and re-enable frame forwarding, which disrupts PLCA/CSMA.
        self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True, label="MCU-FWD")
        time.sleep(0.5)
        iperf_start_time = time.time()
        mcu_iperf = self.mcu.run_command(
            f"iperf -u -c {self.mpu_ip}",
            timeout=float(self.test_duration + 10),
            expect_prompt=True, live_output=True, label="MCU-CLIENT"
        )
        # MCU RTOS CLI returns the prompt immediately while iperf runs in the background;
        # wait here so subsequent phases don't hit 'iperf: All instances busy'.
        self._wait_for_mcu_iperf_idle(iperf_start_time)

        phase_data["results"].append({
            "step": "mcu_client",
            "command": mcu_iperf.command,
            "output": mcu_iperf.output,
            "success": mcu_iperf.success,
            "duration": mcu_iperf.duration_sec
        })

        # Stop monitoring and iperf server
        print("    Stopping monitoring and server...")
        time.sleep(1)  # Let monitoring catch final data
        
        stop_commands = [
            "killall iperf 2>/dev/null || true",
            "kill %1 %2 2>/dev/null || true",  # Kill background jobs
            "cat /tmp/iperf_server.log",
            "cat /tmp/irq_monitor.log | tail -10",
            "cat /tmp/system_load.log | tail -10",
        ]
        
        for cmd in stop_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-STOP")
            phase_data["results"].append({
                "step": "monitoring_stop",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # 2.3 Post-test comparison
        print("  2.3 Post-Test Analysis...")
        for cmd in baseline_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-POST")
            phase_data["results"].append({
                "step": "post_analysis", 
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        phase_data["end_time"] = datetime.now().isoformat()
        self.report.phases["phase2_interrupt_analysis"] = phase_data
        print(f"  ✓ Phase 2 completed: {len(phase_data['results'])} commands executed")

    def _run_phase3_network_stack(self) -> None:
        """Phase 3: Kernel Network Stack Deep-Dive."""
        print("\n📦 Phase 3: Kernel Network Stack Deep-Dive")
        print("-" * 40)
        
        phase_data = {"start_time": datetime.now().isoformat(), "results": []}

        # 3.1 Network Stack Counter Analysis
        print("  3.1 Network Stack Counter Analysis...")
        
        # Baseline counters
        baseline_commands = [
            "cat /proc/net/snmp",
            "cat /proc/net/dev",
            "cat /proc/net/softnet_stat",
        ]
        
        for cmd in baseline_commands:
            result = self.mpu.run_command(f"{cmd} > /tmp/{cmd.split('/')[-1]}_baseline.txt", 
                                        timeout=5.0, expect_prompt=True, label="MPU-NET-BASE")
            phase_data["results"].append({
                "step": "network_baseline",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # Continuous network monitoring script
        monitoring_script = '''
#!/bin/bash
while true; do
    TIMESTAMP=$(date '+%H:%M:%S')
    DROPS=$(cat /proc/net/dev | grep eth0 | awk '{print $4}')
    FIFO=$(cat /proc/net/dev | grep eth0 | awk '{print $5}')
    UDP_ERRORS=$(cat /proc/net/snmp | grep "Udp:" | tail -1 | awk '{print $4}')
    UDP_RCVBUF=$(cat /proc/net/snmp | grep "Udp:" | tail -1 | awk '{print $6}')
    SOFTNET=$(cat /proc/net/softnet_stat | head -1)
    echo "$TIMESTAMP: drops=$DROPS fifo=$FIFO udp_err=$UDP_ERRORS udp_rcvbuf=$UDP_RCVBUF softnet=[$SOFTNET]"
    sleep 0.5
done > /tmp/detailed_net_stats.log
'''
        
        # Create and start monitoring script
        result = self.mpu.run_command(f'cat > /tmp/net_monitor.sh << \'EOF\'\n{monitoring_script}\nEOF', 
                                     timeout=3.0, expect_prompt=True, label="MPU-NET-SCRIPT")
        
        result = self.mpu.run_command("chmod +x /tmp/net_monitor.sh", timeout=2.0, expect_prompt=True, label="MPU-NET-CHMOD")
        
        result = self.mpu.run_command("/tmp/net_monitor.sh &", timeout=3.0, expect_prompt=True, label="MPU-NET-START")
        
        # Run iperf test with network monitoring
        print(f"  3.2 Running {self.test_duration}s iperf with detailed network monitoring...")
        
        # Start server
        result = self.mpu.run_command("iperf -s -u -i 1 > /tmp/iperf_netstack.log 2>&1 &", 
                                     timeout=5.0, expect_prompt=True, label="MPU-IPERF-NET")
        time.sleep(2)
        
        # MCU client
        self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True, label="MCU-FWD")
        time.sleep(0.5)
        iperf_start_time = time.time()
        mcu_iperf = self.mcu.run_command(
            f"iperf -u -c {self.mpu_ip}",
            timeout=float(self.test_duration + 10),
            expect_prompt=True, live_output=True, label="MCU-NET-CLIENT"
        )
        self._wait_for_mcu_iperf_idle(iperf_start_time)

        phase_data["results"].append({
            "step": "network_iperf",
            "command": mcu_iperf.command,
            "output": mcu_iperf.output,
            "success": mcu_iperf.success,
            "duration": mcu_iperf.duration_sec
        })

        # Stop monitoring and collect results
        time.sleep(1)
        stop_commands = [
            "killall iperf net_monitor.sh 2>/dev/null || true",
            "cat /tmp/iperf_netstack.log",
            "cat /tmp/detailed_net_stats.log | tail -20",
        ]
        
        for cmd in stop_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-NET-STOP")
            phase_data["results"].append({
                "step": "network_stop",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # Final comparison
        for cmd in baseline_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-NET-FINAL")
            phase_data["results"].append({
                "step": "network_final",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        phase_data["end_time"] = datetime.now().isoformat() 
        self.report.phases["phase3_network_stack"] = phase_data
        print(f"  ✓ Phase 3 completed: {len(phase_data['results'])} commands executed")

    def _run_phase4_hardware_diagnostics(self) -> None:
        """Phase 4: Hardware Layer Diagnostics."""
        print("\n🔧 Phase 4: Hardware Layer Diagnostics")  
        print("-" * 40)
        
        phase_data = {"start_time": datetime.now().isoformat(), "results": []}

        # 4.1 T1S Link Quality During Load
        print("  4.1 T1S Link Quality During Load...")
        
        # NOTE: lan_read is intentionally NOT used here.
        # CONFIRMED: any lan_read SPI access races with the kernel ISR (IRQ 37
        # spi0.0) and corrupts the TC6 state machine, breaking T1S connectivity
        # immediately.  Use only kernel-space counters via /proc and /sys.
        phy_monitoring_script = r'''
#!/bin/sh
while true; do
    TS=$(date "+%H:%M:%S")
    RX=$(cat /sys/class/net/eth0/statistics/rx_packets 2>/dev/null)
    TX=$(cat /sys/class/net/eth0/statistics/tx_packets 2>/dev/null)
    RX_DROP=$(cat /sys/class/net/eth0/statistics/rx_dropped 2>/dev/null)
    RX_ERR=$(cat /sys/class/net/eth0/statistics/rx_errors 2>/dev/null)
    IRQ37=$(grep -E "^ *37:" /proc/interrupts 2>/dev/null | awk "{print \$2}")
    IRQ33=$(grep -E "^ *33:" /proc/interrupts 2>/dev/null | awk "{print \$2}")
    echo "$TS: RX=$RX TX=$TX RX_drop=$RX_DROP RX_err=$RX_ERR IRQ37=$IRQ37 IRQ33=$IRQ33"
    sleep 2
done > /tmp/phy_quality.log
'''
        
        # Create PHY monitoring script
        result = self.mpu.run_command(f'cat > /tmp/phy_monitor.sh << \'EOF\'\n{phy_monitoring_script}\nEOF',
                                     timeout=3.0, expect_prompt=True, label="MPU-PHY-SCRIPT")
        
        result = self.mpu.run_command("chmod +x /tmp/phy_monitor.sh", timeout=2.0, expect_prompt=True, label="MPU-PHY-CHMOD")
        
        result = self.mpu.run_command("/tmp/phy_monitor.sh &", timeout=3.0, expect_prompt=True, label="MPU-PHY-START")
        
        # Combined iperf + PHY monitoring test
        print(f"  4.2 Running {self.test_duration}s iperf with PHY monitoring...")
        
        result = self.mpu.run_command("iperf -s -u -i 1 > /tmp/iperf_phy.log 2>&1 &",
                                     timeout=5.0, expect_prompt=True, label="MPU-IPERF-PHY")
        time.sleep(2)
        
        self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True, label="MCU-FWD")
        time.sleep(0.5)
        iperf_start_time = time.time()
        mcu_iperf = self.mcu.run_command(
            f"iperf -u -c {self.mpu_ip}",
            timeout=float(self.test_duration + 10),
            expect_prompt=True, live_output=True, label="MCU-PHY-CLIENT"
        )
        self._wait_for_mcu_iperf_idle(iperf_start_time)

        phase_data["results"].append({
            "step": "phy_iperf",
            "command": mcu_iperf.command,
            "output": mcu_iperf.output,
            "success": mcu_iperf.success,
            "duration": mcu_iperf.duration_sec
        })

        # Stop monitoring and analyze
        time.sleep(1)
        stop_commands = [
            "killall iperf phy_monitor.sh 2>/dev/null || true",
            "cat /tmp/iperf_phy.log",
            "cat /tmp/phy_quality.log",
        ]
        
        for cmd in stop_commands:
            result = self.mpu.run_command(cmd, timeout=5.0, live_output=True, label="MPU-PHY-STOP")
            phase_data["results"].append({
                "step": "phy_analysis",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # 4.3 Register Access Performance Impact Test
        # SKIPPED: Confirmed via empirical testing (Phase 0 experiment, 2026-03-18)
        # that ANY lan_read call instantly corrupts the TC6 state machine by
        # racing with the kernel ISR on the SPI bus.  Running this test would
        # only destroy connectivity and produce meaningless results.
        # The conclusion is already known: register reads must NEVER happen
        # while the kernel driver is active.  The ioctl needs spin_lock_irqsave
        # protection in the kernel driver to be safe to use concurrently.
        print("  4.3 Register access performance test: SKIPPED")
        print("      (CONFIRMED: lan_read always corrupts TC6 state — see Phase 0 results)")
        phase_data["results"].append({
            "step": "register_perf_test",
            "command": "SKIPPED",
            "output": "lan_read confirmed to corrupt TC6 via SPI race with kernel ISR",
            "success": True,
            "duration": 0.0
        })
        self.mpu.run_command("killall iperf 2>/dev/null || true", timeout=3.0, expect_prompt=True, label="MPU-KILL")

        phase_data["end_time"] = datetime.now().isoformat()
        self.report.phases["phase4_hardware_diagnostics"] = phase_data
        print(f"  ✓ Phase 4 completed: {len(phase_data['results'])} commands executed")

    def _run_phase5_kernel_optimization(self) -> None:
        """Phase 5: Kernel Parameter Optimization Testing."""
        print("\n🛠️  Phase 5: Kernel Parameter Optimization Testing")
        print("-" * 40)
        
        phase_data = {"start_time": datetime.now().isoformat(), "tests": [], "results": []}

        # Backup current settings
        print("  5.1 Backup current kernel parameters...")
        backup_commands = [
            "cat /proc/sys/net/core/netdev_max_backlog",
            "cat /proc/sys/net/core/netdev_budget",
        ]
        
        original_settings = {}
        for cmd in backup_commands:
            result = self.mpu.run_command(cmd, timeout=3.0, live_output=True, label="MPU-BACKUP")
            param_name = cmd.split('/')[-1]
            original_settings[param_name] = result.output.strip()
            phase_data["results"].append({
                "step": "backup",
                "command": result.command,
                "output": result.output,
                "success": result.success,
                "duration": result.duration_sec
            })

        # Define optimization tests
        optimization_tests = [
            {
                "name": "Baseline (default settings)",
                "settings": {},  # No changes
            },
            {
                "name": "Increased netdev_max_backlog to 10000",
                "settings": {"netdev_max_backlog": "10000"},
            },
            {
                "name": "Increased netdev_max_backlog to 50000", 
                "settings": {"netdev_max_backlog": "50000"},
            },
            {
                "name": "Increased netdev_budget to 600",
                "settings": {"netdev_max_backlog": original_settings.get("netdev_max_backlog", "300"), 
                           "netdev_budget": "600"},
            },
        ]

        # Run optimization tests
        for i, test_config in enumerate(optimization_tests, 1):
            print(f"  5.{i} {test_config['name']}")
            
            # Apply settings
            for param, value in test_config["settings"].items():
                cmd = f"echo {value} > /proc/sys/net/core/{param}"
                result = self.mpu.run_command(cmd, timeout=3.0, expect_prompt=True, label="MPU-OPT-SET")
                phase_data["results"].append({
                    "step": f"optimization_set_{param}",
                    "command": result.command,
                    "output": result.output,
                    "success": result.success,
                    "duration": result.duration_sec
                })

            # Run iperf test
            result = self.mpu.run_command("iperf -s -u -i 1 > /tmp/iperf_opt.log 2>&1 &",
                                         timeout=5.0, expect_prompt=True, label="MPU-OPT-IPERF")
            time.sleep(2)

            self.mcu.run_command("fwd 0", timeout=5.0, expect_prompt=True, label="MCU-FWD")
            time.sleep(0.5)
            iperf_start_time = time.time()
            mcu_iperf = self.mcu.run_command(f"iperf -u -c {self.mpu_ip}", timeout=35.0,
                                           expect_prompt=True, live_output=True, label=f"MCU-OPT-{i}")
            self._wait_for_mcu_iperf_idle(iperf_start_time)
            
            # Get iperf server results
            server_result = self.mpu.run_command("cat /tmp/iperf_opt.log", timeout=3.0, live_output=True, label="MPU-OPT-RESULT")
            
            # Parse packet loss from results
            packet_loss = self._extract_packet_loss(mcu_iperf.output, server_result.output)
            
            test_result = {
                "test_name": test_config["name"],
                "settings": test_config["settings"],
                "mcu_client": {
                    "command": mcu_iperf.command,
                    "output": mcu_iperf.output,
                    "success": mcu_iperf.success,
                    "duration": mcu_iperf.duration_sec,
                    "packet_loss": packet_loss.get("client_loss", "unknown")
                },
                "mpu_server": {
                    "command": server_result.command,
                    "output": server_result.output,
                    "success": server_result.success,
                    "duration": server_result.duration_sec,
                    "packet_loss": packet_loss.get("server_loss", "unknown")
                }
            }
            
            phase_data["tests"].append(test_result)
            
            # Stop iperf
            self.mpu.run_command("killall iperf 2>/dev/null || true", timeout=3.0, expect_prompt=True, label="MPU-OPT-KILL")
            time.sleep(1)

        # Restore original settings
        print("  5.5 Restoring original kernel parameters...")
        for param, value in original_settings.items():
            if value and value.isdigit():
                cmd = f"echo {value} > /proc/sys/net/core/{param}"
                result = self.mpu.run_command(cmd, timeout=3.0, expect_prompt=True, label="MPU-RESTORE")
                phase_data["results"].append({
                    "step": f"restore_{param}",
                    "command": result.command,
                    "output": result.output,
                    "success": result.success,
                    "duration": result.duration_sec
                })

        phase_data["end_time"] = datetime.now().isoformat()
        self.report.phases["phase5_kernel_optimization"] = phase_data
        self.report.optimization_results = {
            "tests": phase_data["tests"],
            "original_settings": original_settings,
        }
        print(f"  ✓ Phase 5 completed: {len(phase_data['tests'])} optimization tests executed")

    def _run_phase6_analysis_and_report(self) -> None:
        """Phase 6: Analysis and Recommendations."""
        print("\n📋 Phase 6: Analysis and Recommendations")
        print("-" * 40)
        
        analysis = {
            "timestamp": datetime.now().isoformat(),
            "root_cause_analysis": {},
            "performance_impact": {},
            "recommendations": []
        }

        # Analyze optimization results
        if "optimization_results" in self.report.__dict__ and self.report.optimization_results.get("tests"):
            best_result = None
            baseline_loss = None
            
            for test in self.report.optimization_results["tests"]:
                test_name = test.get("test_name", "unknown")
                client_loss = test.get("mcu_client", {}).get("packet_loss", "unknown")
                
                print(f"    {test_name}: {client_loss} packet loss")
                
                if "Baseline" in test_name:
                    baseline_loss = client_loss
                elif best_result is None or (isinstance(client_loss, (int, float)) and 
                                           isinstance(best_result.get("packet_loss"), (int, float)) and
                                           client_loss < best_result["packet_loss"]):
                    best_result = {"test_name": test_name, "packet_loss": client_loss}

            # Generate recommendations based on results
            if best_result and baseline_loss:
                if isinstance(best_result["packet_loss"], (int, float)) and isinstance(baseline_loss, (int, float)):
                    improvement = baseline_loss - best_result["packet_loss"]
                    if improvement > 20:  # More than 20% improvement
                        analysis["recommendations"].append(
                            f"✓ Buffer tuning shows significant improvement: "
                            f"{best_result['test_name']} reduced packet loss by {improvement}% - make permanent"
                        )
                    elif improvement > 5:
                        analysis["recommendations"].append(
                            f"⚠ Buffer tuning shows modest improvement: "
                            f"{best_result['test_name']} reduced packet loss by {improvement}% - consider implementing"
                        )
                    else:
                        analysis["recommendations"].append(
                            "⚠ Buffer tuning has minimal impact - investigate hardware limits"
                        )
                else:
                    analysis["recommendations"].append(
                        "⚠ Unable to parse packet loss results - manual analysis required"
                    )

        # Additional recommendations based on phases executed
        if 2 not in self.skip_phases:
            analysis["recommendations"].append(
                "📊 Check Phase 2 interrupt monitoring logs for IRQ storms or excessive context switches"
            )
            
        if 4 not in self.skip_phases:
            analysis["recommendations"].append(
                "🔧 Review Phase 4 PHY quality logs for T1S link degradation during high traffic"
            )

        # Final assessment
        analysis["recommendations"].extend([
            "🔍 If buffer tuning ineffective: Root cause is likely hardware bottleneck (LAN8651/LAN9662 or ARM-CPU)",
            "⚡ If register access impacts performance: Consider conditional compilation of debugging features",
            "🚨 If IRQ storms detected: Implement interrupt coalescing or NAPI tuning",
            "📈 Consider MCU firmware fix for rate control (eliminate 1ms timer floor)"
        ])

        self.report.analysis = analysis
        
        print("\n" + "=" * 60)
        print("📋 DIAGNOSTIC ANALYSIS COMPLETE")
        print("=" * 60)
        print("\nRECOMMENDations:")
        for i, rec in enumerate(analysis["recommendations"], 1):
            print(f"  {i}. {rec}")

        print(f"\n💾 Full report available in generated files")
        print("🏁 Diagnostic execution completed successfully")

    def _run_final_register_snapshot(self) -> None:
        """Final phase: hardware register snapshot via lan_read.

        MUST run LAST.  lan_read races with the kernel LAN865x ISR on the TC6
        SPI bus and permanently corrupts the TC6 state machine (confirmed
        empirically 2026-03-18).  By running this after all iperf tests the
        link damage does not affect any measurements.
        """
        print("\n📍 Final: Hardware Register Snapshot (lan_read — will break T1S link)")
        print("-" * 40)
        print("  WARNING: these lan_read calls corrupt TC6. Running last on purpose.")

        register_commands = [
            ("lan_read 0x00800004", "STS0 — PHY/T1S link status"),
            ("lan_read 0x00800100", "SQI — signal quality index"),
            ("lan_read 0x00800300", "PLCA_CTRL0 — PLCA_EN + coordinator"),
            ("lan_read 0x00800302", "PLCA_CTRL1 — node count + node ID"),
            ("lan_read 0x00800304", "PLCA_STATUS0"),
            ("lan_read 0x00800306", "PLCA_STATUS1 / TX opportunity timer"),
            ("lan_read 0x00010000", "MAC_NET_CTL"),
            ("lan_read 0x00010001", "MAC_NET_CFG"),
            # 0x00000008 (TC6 STATUS0 W1C) deliberately omitted — even more destructive
            ("lan_read 0x000000E0", "VENDOR_CTRL"),
            ("lan_read 0x00020000", "RX_GOOD_FRAMES"),
            ("lan_read 0x00020004", "RX_BAD_FRAMES"),
            ("lan_read 0x00020040", "RX_OVERSIZE_FRAMES"),
        ]

        snapshot = {}
        for cmd, desc in register_commands:
            result = self.mpu.run_command(cmd, timeout=3.0, live_output=True, label="FINAL-REG")
            print(f"    {desc}: {result.output.strip()}")
            snapshot[cmd] = result.output.strip()

        self.report.phases["final_register_snapshot"] = snapshot
        print("  ✓ Register snapshot complete (T1S link may now be broken — power cycle to restore).")

    def _extract_packet_loss(self, mcu_output: str, mpu_output: str) -> Dict[str, object]:
        """Extract packet loss percentages from iperf outputs."""
        result = {}
        
        # MCU client output pattern: [0.0-10.0 sec] 123/1000 (12%) 5000 Kbps
        mcu_pattern = r'\[\s*[\d\.]+\s*-\s*[\d\.]+\s*sec\]\s*(\d+)\s*/\s*(\d+)\s*\(\s*(\d+)%\)'
        mcu_match = re.search(mcu_pattern, mcu_output)
        if mcu_match:
            result["client_loss"] = int(mcu_match.group(3))
        
        # MPU server output pattern: similar format
        mpu_pattern = r'(\d+)/\s*(\d+)\s*\(\s*(\d+)%\)'
        mpu_match = re.search(mpu_pattern, mpu_output)  
        if mpu_match:
            result["server_loss"] = int(mpu_match.group(3))
            
        return result


def save_diagnostic_report(report: DiagnosticReport, output_dir: Path) -> Dict[str, Path]:
    """Save diagnostic report to JSON and text files."""
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    json_file = output_dir / f"mpu_rx_diagnostic_report_{timestamp}.json"
    txt_file = output_dir / f"mpu_rx_diagnostic_report_{timestamp}.txt"
    
    # Save JSON report
    with open(json_file, 'w', encoding='utf-8') as f:
        json.dump(report.__dict__, f, indent=2, default=str)
    
    # Save text report
    with open(txt_file, 'w', encoding='utf-8') as f:
        f.write("MPU RX PERFORMANCE DIAGNOSTIC REPORT\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"Timestamp: {report.timestamp}\n")
        f.write(f"Configuration: {json.dumps(report.config, indent=2)}\n\n")
        
        f.write("EXECUTIVE SUMMARY\n")
        f.write("-" * 20 + "\n")
        if report.analysis.get("recommendations"):
            for i, rec in enumerate(report.analysis["recommendations"], 1):
                f.write(f"{i}. {rec}\n")
        f.write("\n")
        
        f.write("PHASE RESULTS\n")
        f.write("-" * 20 + "\n")
        for phase_name, phase_data in report.phases.items():
            f.write(f"\n{phase_name.upper()}:\n")
            if isinstance(phase_data.get("results"), list):
                f.write(f"  Commands executed: {len(phase_data['results'])}\n")
                f.write(f"  Duration: {phase_data.get('start_time', 'unknown')} - {phase_data.get('end_time', 'unknown')}\n")
        
        f.write(f"\nOptimization Tests: {len(report.optimization_results.get('tests', []))}\n")
        f.write("\nFull details available in JSON report.\n")
    
    return {"json": json_file, "txt": txt_file}


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="MPU RX Performance Diagnostic Test Suite - Automated analysis of 85% packet loss issue"
    )
    parser.add_argument("--mcu-port", default="COM8", help="MCU serial port (default: COM8)")
    parser.add_argument("--mpu-port", default="COM9", help="MPU serial port (default: COM9)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--mcu-ip", default="192.168.0.200", help="MCU IP address (default: 192.168.0.200)")
    parser.add_argument("--mpu-ip", default="192.168.0.5", help="MPU IP address (default: 192.168.0.5)")
    parser.add_argument("--test-duration", type=int, default=30, help="iperf test duration in seconds (default: 30)")
    parser.add_argument("--skip-phases", help="Comma-separated list of phases to skip (1-5)")
    parser.add_argument("--output-dir", default=".", help="Output directory for reports (default: current)")
    
    args = parser.parse_args()
    
    # Parse skip phases
    if args.skip_phases:
        try:
            args.skip_phases = {int(x.strip()) for x in args.skip_phases.split(",") if x.strip().isdigit()}
        except ValueError:
            args.skip_phases = set()
    else:
        args.skip_phases = set()
    
    return args


def main() -> int:
    """Main entry point."""
    args = parse_args()
    
    try:
        # Initialize diagnostic runner
        runner = MPURXDiagnosticRunner(
            mcu_port=args.mcu_port,
            mpu_port=args.mpu_port,
            baudrate=args.baudrate,
            mcu_ip=args.mcu_ip,
            mpu_ip=args.mpu_ip,
            test_duration=args.test_duration,
            skip_phases=args.skip_phases
        )
        
        # Run diagnostic
        report = runner.run_diagnostic()
        
        # Save report
        output_paths = save_diagnostic_report(report, Path(args.output_dir))
        
        print(f"\n📁 Report files saved:")
        print(f"   JSON: {output_paths['json']}")
        print(f"   TXT:  {output_paths['txt']}")
        
        return 0
        
    except serial.SerialException as e:
        print(f"❌ Serial connection error: {e}")
        return 2
    except Exception as e:
        print(f"❌ Diagnostic failed: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())