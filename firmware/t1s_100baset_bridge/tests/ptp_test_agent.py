#!/usr/bin/env python3
"""
PTP Test Agent for T1S 100BaseT Bridge
=======================================
Tests the PTP (Precision Time Protocol) functionality via CLI commands
on two ATSAME54P20A / LAN865x boards connected via serial ports.

Based on firmware/T1S_100BaseT_Bridge.X/README_PTP_USAGE.md

Usage:
    python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8

Requirements:
    pip install pyserial
"""

import argparse
import datetime
import re
import statistics
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DEFAULT_GM_PORT = "COM10"
DEFAULT_FOL_PORT = "COM8"
DEFAULT_GM_IP = "192.168.0.20"
DEFAULT_FOL_IP = "192.168.0.30"
DEFAULT_NETMASK = "255.255.255.0"
DEFAULT_BAUDRATE = 115200
DEFAULT_CMD_TIMEOUT = 5        # seconds
DEFAULT_CONVERGENCE_TIMEOUT = 30  # seconds
DEFAULT_SAMPLES = 20
OFFSET_THRESHOLD_NS = 100      # nanoseconds

# Regex patterns used for parsing
RE_IP_SET = re.compile(r"Set ip address OK|IP address set to")
RE_PING_REPLY = re.compile(r"Ping:.*reply.*from|Reply from")
RE_PING_DONE  = re.compile(r"Ping: done\.")
RE_FOL_START = re.compile(r"\[PTP\] follower mode")
RE_GM_START = re.compile(r"\[PTP\] grandmaster mode")
RE_MATCHFREQ = re.compile(r"UNINIT->MATCHFREQ")
RE_HARD_SYNC = re.compile(r"Hard sync completed")
RE_COARSE = re.compile(r"PTP COARSE")
RE_FINE = re.compile(r"PTP FINE")
RE_PTP_OFFSET = re.compile(r"\[PTP\] offset=([+-]?\d+)\s*ns")
RE_PTP_STATUS = re.compile(
    r"\[PTP\]\s+mode=(\S+)\s+gmSyncs=(\d+)\s+gmState=(\d+)"
)
RE_PTP_DISABLED = re.compile(r"\[PTP\] disabled")


# ---------------------------------------------------------------------------
# Logger
# ---------------------------------------------------------------------------

class Logger:
    """Dual-writes to stdout and an optional log file."""

    def __init__(self, log_file: str = None, verbose: bool = False):
        self.log_file = log_file
        self.verbose = verbose
        self._fh = None
        if log_file:
            self._fh = open(log_file, "w", encoding="utf-8")

    def _write(self, line: str):
        print(line)
        if self._fh:
            self._fh.write(line + "\n")
            self._fh.flush()

    def info(self, msg: str):
        self._write(msg)

    def debug(self, msg: str):
        if self.verbose:
            self._write(f"  [DBG] {msg}")

    def close(self):
        if self._fh:
            self._fh.close()
            self._fh = None


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

def open_port(port: str, baudrate: int = DEFAULT_BAUDRATE) -> serial.Serial:
    """Open a serial port with standard 8N1 settings.

    Args:
        port: Port name, e.g. ``COM10`` or ``/dev/ttyUSB0``.
        baudrate: Baud rate (default 115200).

    Returns:
        Opened :class:`serial.Serial` instance.

    Raises:
        serial.SerialException: If the port cannot be opened.
    """
    ser = serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,  # non-blocking reads; we poll manually
    )
    return ser


def send_command(
    ser: serial.Serial,
    cmd: str,
    timeout: float = DEFAULT_CMD_TIMEOUT,
    log: Logger = None,
) -> str:
    """Send a CLI command and collect the response.

    Sends *cmd* terminated by ``\\r\\n``, then reads until the timeout
    expires without new data.

    Args:
        ser: Open serial port.
        cmd: Command string (without line ending).
        timeout: Maximum seconds to wait for the response.
        log: Optional :class:`Logger` for debug output.

    Returns:
        Accumulated response string.
    """
    # Flush any stale data in the receive buffer.
    ser.reset_input_buffer()

    full_cmd = (cmd + "\r\n").encode("ascii")
    ser.write(full_cmd)
    if log:
        log.debug(f"  >> {cmd}")

    response_parts = []
    deadline = time.monotonic() + timeout
    last_data = time.monotonic()

    while time.monotonic() < deadline:
        chunk = ser.read(256)
        if chunk:
            decoded = chunk.decode("ascii", errors="replace")
            response_parts.append(decoded)
            last_data = time.monotonic()
            if log:
                log.debug(decoded.rstrip())
        else:
            # Stop once the port has been quiet for 0.5 s after receiving data.
            if response_parts and (time.monotonic() - last_data) > 0.5:
                break
            time.sleep(0.05)

    response = "".join(response_parts)
    return response


def wait_for_pattern(
    ser: serial.Serial,
    pattern: "re.Pattern",
    timeout: float = DEFAULT_CONVERGENCE_TIMEOUT,
    log: Logger = None,
    extra_patterns: dict = None,
    live_log: bool = False,
) -> tuple:
    """Read from *ser* until *pattern* matches or *timeout* expires.

    Args:
        ser: Open serial port.
        pattern: Compiled regex to wait for.
        timeout: Maximum wait time in seconds.
        log: Optional :class:`Logger` for debug output.
        extra_patterns: Mapping of label→compiled-regex for milestones to
            record (first-match timestamps returned in the result dict).
        live_log: If ``True``, log incoming lines via ``log.info`` instead
            of ``log.debug`` so they appear without ``--verbose``.

    Returns:
        A tuple ``(matched: bool, elapsed: float, milestones: dict)``.
        *milestones* maps label→elapsed-seconds for each extra pattern that
        was first matched.
    """
    if extra_patterns is None:
        extra_patterns = {}

    milestones: dict = {}
    buffer = ""
    start = time.monotonic()
    deadline = start + timeout

    while time.monotonic() < deadline:
        chunk = ser.read(256)
        if chunk:
            decoded = chunk.decode("ascii", errors="replace")
            buffer += decoded
            if log:
                for line in decoded.splitlines():
                    if line.strip():
                        if live_log:
                            log.info(f"    {line.rstrip()}")
                        else:
                            log.debug(f"  <- {line.rstrip()}")

            for label, pat in extra_patterns.items():
                if label not in milestones and pat.search(buffer):
                    milestones[label] = time.monotonic() - start

            if pattern.search(buffer):
                elapsed = time.monotonic() - start
                return True, elapsed, milestones
        else:
            time.sleep(0.05)

    elapsed = time.monotonic() - start
    return False, elapsed, milestones


# ---------------------------------------------------------------------------
# Offset parsing
# ---------------------------------------------------------------------------

def parse_offset(response: str) -> Optional[int]:
    """Extract the offset value (in nanoseconds) from a ``ptp_offset`` response.

    Expected format: ``[PTP] offset=+45 ns  abs=45 ns``

    Args:
        response: Raw CLI response string.

    Returns:
        Integer offset in ns, or ``None`` if not found.
    """
    m = RE_PTP_OFFSET.search(response)
    if m:
        return int(m.group(1))
    return None


def parse_ptp_status(response: str) -> Optional[Dict]:
    """Parse the output of ``ptp_status``.

    Expected format: ``[PTP] mode=master gmSyncs=198 gmState=3``

    Args:
        response: Raw CLI response string.

    Returns:
        Dict with keys ``mode``, ``gmSyncs`` (int), ``gmState`` (int), or
        ``None`` if the pattern was not found.
    """
    m = RE_PTP_STATUS.search(response)
    if m:
        return {
            "mode": m.group(1),
            "gmSyncs": int(m.group(2)),
            "gmState": int(m.group(3)),
        }
    return None


# ---------------------------------------------------------------------------
# Test steps
# ---------------------------------------------------------------------------

class PTPTestAgent:
    """Orchestrates the full PTP test sequence.

    Args:
        gm_port: Serial port name for the Grandmaster board.
        fol_port: Serial port name for the Follower board.
        gm_ip: IP address to assign to the Grandmaster.
        fol_ip: IP address to assign to the Follower.
        netmask: Subnet mask (default ``255.255.255.0``).
        samples: Number of ``ptp_offset`` samples to collect in Step 5.
        convergence_timeout: Max seconds to wait for FINE state.
        cmd_timeout: Max seconds to wait for a single command response.
        stop_ptp_after: If ``True`` (default), send ``ptp_mode off`` during cleanup.
        from_step: Skip steps below this number.
        log: :class:`Logger` instance.
    """

    def __init__(
        self,
        gm_port: str,
        fol_port: str,
        gm_ip: str = DEFAULT_GM_IP,
        fol_ip: str = DEFAULT_FOL_IP,
        netmask: str = DEFAULT_NETMASK,
        samples: int = DEFAULT_SAMPLES,
        convergence_timeout: float = DEFAULT_CONVERGENCE_TIMEOUT,
        cmd_timeout: float = DEFAULT_CMD_TIMEOUT,
        stop_ptp_after: bool = True,
        from_step: int = 1,
        log: Logger = None,
    ):
        self.gm_port_name = gm_port
        self.fol_port_name = fol_port
        self.gm_ip = gm_ip
        self.fol_ip = fol_ip
        self.netmask = netmask
        self.samples = samples
        self.convergence_timeout = convergence_timeout
        self.cmd_timeout = cmd_timeout
        self.stop_ptp_after = stop_ptp_after
        self.from_step = from_step
        self.log = log or Logger()

        self.gm_ser: Optional[serial.Serial] = None
        self.fol_ser: Optional[serial.Serial] = None

        # Test results: step_name → (passed: bool, detail: str)
        self.results: list = []

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    def connect(self):
        """Open serial connections to both boards.

        Raises:
            SystemExit: On connection failure.
        """
        self.log.info(f"Connecting to GM  ({self.gm_port_name})…")
        try:
            self.gm_ser = open_port(self.gm_port_name)
            self.log.info(f"  GM  port open: {self.gm_port_name}")
        except serial.SerialException as exc:
            self.log.info(f"ERROR: Cannot open GM port {self.gm_port_name}: {exc}")
            sys.exit(1)

        self.log.info(f"Connecting to FOL ({self.fol_port_name})…")
        try:
            self.fol_ser = open_port(self.fol_port_name)
            self.log.info(f"  FOL port open: {self.fol_port_name}")
        except serial.SerialException as exc:
            self.log.info(f"ERROR: Cannot open FOL port {self.fol_port_name}: {exc}")
            if self.gm_ser:
                self.gm_ser.close()
            sys.exit(1)

    def disconnect(self):
        """Close serial ports gracefully."""
        for ser in (self.gm_ser, self.fol_ser):
            if ser and ser.is_open:
                try:
                    ser.close()
                except Exception:
                    pass

    # ------------------------------------------------------------------
    # Step 0 — Reset both boards
    # ------------------------------------------------------------------

    def test_step_0_reset(self):
        """Send a reset command to both boards and wait 8 seconds.

        Always executed regardless of ``--from-step``.  A failure to
        confirm the reset only produces a warning — the test continues.
        """
        step = "Step 0: Reset"
        self.log.info(f"\n--- {step} ---")

        detail_parts = []

        for label, ser in [("GM ", self.gm_ser), ("FOL", self.fol_ser)]:
            self.log.info(f"  [{label}] reset")
            try:
                resp = send_command(ser, "reset", self.cmd_timeout, self.log)
                detail_parts.append(f"{label} reset sent")
                self.log.info(f"  [{label}] ✓ reset command sent")
                if resp.strip():
                    self.log.debug(f"  [{label}] response: {resp.strip()!r}")
            except Exception as exc:
                detail_parts.append(f"{label} reset WARNING ({exc})")
                self.log.info(f"  [{label}] ⚠ reset failed (continuing): {exc}")

        self.log.info("  Waiting 8 s after reset…")
        time.sleep(8)

        self._record(step, True, "; ".join(detail_parts))

    # ------------------------------------------------------------------
    # Step 1 — IP configuration
    # ------------------------------------------------------------------

    def test_step_1_ip_config(self) -> bool:
        """Configure IP addresses on both boards.

        Returns:
            ``True`` if both boards confirmed their IP address.
        """
        step = "Step 1: IP Configuration"
        self.log.info(f"\n--- {step} ---")

        passed = True
        detail_parts = []

        for label, ser, ip in [
            ("GM ", self.gm_ser, self.gm_ip),
            ("FOL", self.fol_ser, self.fol_ip),
        ]:
            cmd = f"setip eth0 {ip} {self.netmask}"
            self.log.info(f"  [{label}] {cmd}")
            resp = send_command(ser, cmd, self.cmd_timeout, self.log)
            if RE_IP_SET.search(resp):
                detail_parts.append(f"{label} IP ok ({ip})")
                self.log.info(f"  [{label}] ✓ IP set")
            else:
                detail_parts.append(f"{label} IP FAIL ({ip})")
                self.log.info(f"  [{label}] ✗ Unexpected response: {resp.strip()!r}")
                passed = False

        self._record(step, passed, "; ".join(detail_parts))
        return passed

    # ------------------------------------------------------------------
    # Step 2 — Network connectivity
    # ------------------------------------------------------------------

    def test_step_2_connectivity(self) -> bool:
        """Ping in both directions to verify network connectivity.

        Returns:
            ``True`` if both pings replied.
        """
        step = "Step 2: Network Connectivity (GM→FOL, FOL→GM)"
        self.log.info(f"\n--- {step} ---")

        passed = True
        detail_parts = []

        for src_label, src_ser, dst_ip in [
            ("GM →FOL", self.gm_ser, self.fol_ip),
            ("FOL→GM ", self.fol_ser, self.gm_ip),
        ]:
            cmd = f"ping {dst_ip}"
            self.log.info(f"  [{src_label}] {cmd}")
            # Ping reply is async in Harmony TCP/IP — wait for 'Ping: done.'
            src_ser.reset_input_buffer()
            src_ser.write((cmd + "\r\n").encode("ascii"))
            self.log.debug(f"  >> {cmd}")
            matched, elapsed, milestones = wait_for_pattern(
                src_ser,
                RE_PING_DONE,
                timeout=15.0,
                log=self.log,
                extra_patterns={"first_reply": RE_PING_REPLY},
                live_log=True,
            )
            if matched:
                detail_parts.append(f"{src_label} ok")
                self.log.info(f"  [{src_label}] ✓ Ping done ({elapsed:.1f}s)")
            elif milestones.get("first_reply") is not None:
                # Got at least one reply but 'done' line didn't appear
                detail_parts.append(f"{src_label} ok (partial)")
                self.log.info(f"  [{src_label}] ✓ Got reply but no 'done' line ({elapsed:.1f}s)")
            else:
                detail_parts.append(f"{src_label} FAIL")
                self.log.info(f"  [{src_label}] ✗ No reply within 15s")
                passed = False

        self._record(step, passed, "; ".join(detail_parts))
        return passed

    # ------------------------------------------------------------------
    # Step 3 — Start PTP
    # ------------------------------------------------------------------

    def test_step_3_start_ptp(self) -> bool:
        """Start PTP — Follower first, then Grandmaster.

        The convergence monitor thread is started *before* any ptp_mode command
        is sent so that Step 4 reports the true elapsed time from PTP activation
        to FINE state, not just the residual buffer drain time.

        Returns:
            ``True`` if both boards confirmed their PTP mode.
        """
        step = "Step 3: PTP Start"
        self.log.info(f"\n--- {step} ---")

        passed = True
        detail_parts = []

        # Flush FOL RX buffer, then launch convergence monitor in background
        # so the timer starts from the moment PTP is activated.
        self.fol_ser.reset_input_buffer()
        self._convergence_result = None
        self._convergence_thread = threading.Thread(
            target=self._convergence_worker, daemon=True
        )
        self._convergence_thread.start()

        # Follower first
        self.log.info("  [FOL] ptp_mode follower")
        resp_fol = send_command(
            self.fol_ser, "ptp_mode follower", self.cmd_timeout, self.log
        )
        if RE_FOL_START.search(resp_fol):
            detail_parts.append("FOL start ok")
            self.log.info("  [FOL] \u2713 follower mode confirmed")
        else:
            detail_parts.append("FOL start FAIL")
            self.log.info(
                f"  [FOL] \u2717 Unexpected response: {resp_fol.strip()!r}"
            )
            passed = False

        # Short pause before starting GM
        time.sleep(0.5)

        # Grandmaster second — wait for the confirmation pattern directly
        # (no quiet-period) because the GM continuously emits debug output.
        self.log.info("  [GM ] ptp_mode master")
        self.gm_ser.reset_input_buffer()
        self.gm_ser.write(b"ptp_mode master\r\n")
        gm_matched, _, _ = wait_for_pattern(
            self.gm_ser, RE_GM_START, timeout=self.cmd_timeout, log=self.log
        )
        if gm_matched:
            detail_parts.append("GM start ok")
            self.log.info("  [GM ] \u2713 grandmaster mode confirmed")
        else:
            detail_parts.append("GM start FAIL")
            self.log.info("  [GM ] \u2717 grandmaster mode not confirmed")
            passed = False

        self._record(step, passed, "; ".join(detail_parts))
        return passed

    def _convergence_worker(self):
        """Background thread: monitor FOL port for FINE state convergence.

        Writes ``(matched, elapsed, milestones)`` into ``self._convergence_result``.
        """
        extra = {
            "MATCHFREQ": RE_MATCHFREQ,
            "HARD_SYNC": RE_HARD_SYNC,
            "COARSE":    RE_COARSE,
        }
        self._convergence_result = wait_for_pattern(
            self.fol_ser,
            RE_FINE,
            timeout=self.convergence_timeout,
            log=self.log,
            extra_patterns=extra,
        )

    # ------------------------------------------------------------------
    # Step 4 — Convergence monitoring
    # ------------------------------------------------------------------

    def test_step_4_convergence(self) -> bool:
        """Collect convergence result from the background thread started in Step 3.

        Because the thread began before any ``ptp_mode`` command was sent,
        *elapsed* is the true wall-clock time from PTP activation to FINE state.
        Falls back to an inline monitor when Step 3 was skipped via ``--from-step``.

        Returns:
            ``True`` if FINE state was reached within the timeout.
        """
        step = "Step 4: Convergence to FINE state"
        self.log.info(f"\n--- {step} (timeout={self.convergence_timeout}s) ---")

        # Wait for the worker thread (self-terminates on timeout).
        thread = getattr(self, "_convergence_thread", None)
        if thread is not None:
            thread.join(timeout=self.convergence_timeout + 2.0)

        result = getattr(self, "_convergence_result", None)
        if result is None:
            # Step 3 was skipped: run the monitor inline now.
            self.log.info("  (No convergence thread — running inline monitor)")
            extra = {
                "MATCHFREQ": RE_MATCHFREQ,
                "HARD_SYNC": RE_HARD_SYNC,
                "COARSE":    RE_COARSE,
            }
            result = wait_for_pattern(
                self.fol_ser,
                RE_FINE,
                timeout=self.convergence_timeout,
                log=self.log,
                extra_patterns=extra,
            )

        matched, elapsed, milestones = result

        milestone_str = ", ".join(
            f"{k}@{v:.1f}s" for k, v in milestones.items()
        )
        if milestone_str:
            self.log.info(f"  Milestones: {milestone_str}")

        if matched:
            detail = f"FINE reached in {elapsed:.1f}s"
            self.log.info(f"  ✓ {detail}")
        else:
            detail = (
                f"FINE NOT reached within {self.convergence_timeout}s "
                f"(milestones: {milestone_str or 'none'})"
            )
            self.log.info(f"  ✗ {detail}")

        self._record(step, matched, detail)
        return matched

    # ------------------------------------------------------------------
    # Step 5 — Offset validation
    # ------------------------------------------------------------------

    def _read_one_offset(self) -> Optional[int]:
        """Send ``ptp_offset`` and return the ns value, or ``None`` on timeout.

        Uses pattern-based reading instead of ``send_command`` because PTP in
        FINE state continuously emits output that prevents the 0.5 s quiet
        period inside ``send_command`` from ever triggering.
        """
        self.fol_ser.reset_input_buffer()
        self.fol_ser.write(b"ptp_offset\r\n")
        deadline = time.monotonic() + 2.0
        buf = ""
        while time.monotonic() < deadline:
            chunk = self.fol_ser.read(256)
            if chunk:
                buf += chunk.decode("ascii", errors="replace")
                m = RE_PTP_OFFSET.search(buf)
                if m:
                    return int(m.group(1))
            else:
                time.sleep(0.05)
        return None

    def test_step_5_offset_validation(self) -> bool:
        """Collect *samples* offset measurements from the Follower.

        Returns:
            ``True`` if all samples are within ±``OFFSET_THRESHOLD_NS`` ns.
        """
        step = "Step 5: Offset Validation"
        self.log.info(
            f"\n--- {step} (samples={self.samples}, threshold=±{OFFSET_THRESHOLD_NS}ns) ---"
        )

        offsets = []
        for i in range(self.samples):
            value = self._read_one_offset()
            if value is not None:
                offsets.append(value)
                self.log.info(f"  sample {i+1:2d}/{self.samples}: {value:+d} ns")
            else:
                self.log.info(
                    f"  ✗ sample {i+1}/{self.samples}: no offset received within 2s"
                )
            # Brief pause between samples
            time.sleep(0.2)

        if not offsets:
            self._record(step, False, "No valid offset samples collected")
            return False

        mean_val = statistics.mean(offsets)
        stdev_val = statistics.stdev(offsets) if len(offsets) > 1 else 0.0
        min_val = min(offsets)
        max_val = max(offsets)
        within = [v for v in offsets if abs(v) <= OFFSET_THRESHOLD_NS]
        pct = 100.0 * len(within) / len(offsets)

        self.log.info(f"  Samples : {len(offsets)}")
        self.log.info(f"  Mean    : {mean_val:+.1f} ns")
        self.log.info(f"  Stdev   : {stdev_val:.1f} ns")
        self.log.info(f"  Min     : {min_val:+d} ns")
        self.log.info(f"  Max     : {max_val:+d} ns")
        self.log.info(
            f"  Within ±{OFFSET_THRESHOLD_NS}ns: {len(within)}/{len(offsets)} ({pct:.0f}%)"
        )

        passed = len(within) == len(offsets)
        detail = (
            f"mean={mean_val:+.1f}ns stdev={stdev_val:.1f}ns "
            f"min={min_val:+d}ns max={max_val:+d}ns "
            f"within±{OFFSET_THRESHOLD_NS}ns: {len(within)}/{len(offsets)} ({pct:.0f}%)"
        )

        # Store statistics for report
        self._offset_stats = {
            "samples": len(offsets),
            "mean": mean_val,
            "stdev": stdev_val,
            "min": min_val,
            "max": max_val,
            "within": len(within),
            "pct": pct,
        }

        self._record(step, passed, detail)
        return passed

    # ------------------------------------------------------------------
    # Optional — PTP status
    # ------------------------------------------------------------------

    def check_ptp_status(self):
        """Query ``ptp_status`` on both boards and log the results."""
        self.log.info("\n--- PTP Status Check ---")
        for label, ser in [("GM ", self.gm_ser), ("FOL", self.fol_ser)]:
            resp = send_command(ser, "ptp_status", self.cmd_timeout, self.log)
            status = parse_ptp_status(resp)
            if status:
                self.log.info(
                    f"  [{label}] mode={status['mode']}  "
                    f"gmSyncs={status['gmSyncs']}  gmState={status['gmState']}"
                )
            else:
                self.log.info(f"  [{label}] Could not parse ptp_status: {resp.strip()!r}")

    # ------------------------------------------------------------------
    # Cleanup
    # ------------------------------------------------------------------

    def cleanup(self):
        """Optionally stop PTP on both boards, then close ports."""
        if self.stop_ptp_after and self.gm_ser and self.fol_ser:
            self.log.info("\n--- Stopping PTP ---")
            for label, ser in [("GM ", self.gm_ser), ("FOL", self.fol_ser)]:
                resp = send_command(
                    ser, "ptp_mode off", self.cmd_timeout, self.log
                )
                if RE_PTP_DISABLED.search(resp):
                    self.log.info(f"  [{label}] ✓ PTP disabled")
                else:
                    self.log.info(
                        f"  [{label}] ✗ Unexpected: {resp.strip()!r}"
                    )
        self.disconnect()

    # ------------------------------------------------------------------
    # Run all steps
    # ------------------------------------------------------------------

    def run(self) -> int:
        """Execute all enabled test steps and print the final report.

        Returns:
            ``0`` if all steps passed, ``1`` otherwise.
        """
        self._offset_stats = None
        start_time = datetime.datetime.now()

        self.log.info("=" * 60)
        self.log.info("  PTP Test Agent — T1S 100BaseT Bridge")
        self.log.info("=" * 60)
        self.log.info(f"Date        : {start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log.info(f"GM Port     : {self.gm_port_name} ({self.gm_ip})")
        self.log.info(f"Follower    : {self.fol_port_name} ({self.fol_ip})")
        self.log.info(f"From step   : {self.from_step}")

        self.connect()

        try:
            # Step 0 is always executed before any user-selected start step.
            self.test_step_0_reset()

            step_functions = [
                (1, self.test_step_1_ip_config),
                (2, self.test_step_2_connectivity),
                (3, self.test_step_3_start_ptp),
                (4, self.test_step_4_convergence),
                (5, self.test_step_5_offset_validation),
            ]

            last_step_passed = True
            for step_num, func in step_functions:
                if step_num < self.from_step:
                    self.log.info(f"\n(Skipping Step {step_num})")
                    continue
                last_step_passed = func()
                # Abort remaining steps on failure (except connectivity failure
                # which is non-fatal if we resume from a later step).
                if not last_step_passed and step_num <= 3:
                    self.log.info(
                        "\nAborting: early step failure — check hardware."
                    )
                    break

            self.check_ptp_status()

        except KeyboardInterrupt:
            self.log.info("\n\nInterrupted by user (Ctrl+C).")
        finally:
            self.cleanup()

        return self._print_report(start_time)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _record(self, name: str, passed: bool, detail: str):
        self.results.append((name, passed, detail))

    def _print_report(self, start_time: datetime.datetime) -> int:
        """Print the final test report.

        Returns:
            Exit code: ``0`` = all passed, ``1`` = any failed.
        """
        self.log.info("\n" + "=" * 60)
        self.log.info("  PTP Functionality Test Report")
        self.log.info("=" * 60)
        self.log.info(f"Date      : {start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log.info(f"GM Port   : {self.gm_port_name} ({self.gm_ip})")
        self.log.info(f"FOL Port  : {self.fol_port_name} ({self.fol_ip})")
        self.log.info("")

        passed_count = 0
        for name, passed, detail in self.results:
            tag = "PASS" if passed else "FAIL"
            self.log.info(f"[{tag}] {name}")
            if detail:
                self.log.info(f"       {detail}")
            if passed:
                passed_count += 1

        # Offset statistics block
        if self._offset_stats:
            st = self._offset_stats
            self.log.info("")
            self.log.info("  Offset Statistics:")
            self.log.info(f"    Samples : {st['samples']}")
            self.log.info(f"    Mean    : {st['mean']:+.1f} ns")
            self.log.info(f"    Stdev   : {st['stdev']:.1f} ns")
            self.log.info(f"    Min     : {st['min']:+d} ns")
            self.log.info(f"    Max     : {st['max']:+d} ns")
            self.log.info(
                f"    Within ±{OFFSET_THRESHOLD_NS}ns: "
                f"{st['within']}/{st['samples']} ({st['pct']:.0f}%)"
            )

        total = len(self.results)
        overall = "PASS" if passed_count == total else "FAIL"
        self.log.info("")
        self.log.info(
            f"Overall Result: {overall} ({passed_count}/{total} tests passed)"
        )

        if self.log.log_file:
            self.log.info(f"Log saved to  : {self.log.log_file}")

        return 0 if passed_count == total else 1


# ---------------------------------------------------------------------------
# CLI entry-point
# ---------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    """Construct and return the CLI argument parser."""
    parser = argparse.ArgumentParser(
        description=(
            "PTP Test Agent for T1S 100BaseT Bridge\n"
            "Tests PTP functionality via serial CLI commands."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        "--gm-port",
        default=DEFAULT_GM_PORT,
        metavar="PORT",
        help=f"Grandmaster COM port (default: {DEFAULT_GM_PORT})",
    )
    parser.add_argument(
        "--fol-port",
        default=DEFAULT_FOL_PORT,
        metavar="PORT",
        help=f"Follower COM port (default: {DEFAULT_FOL_PORT})",
    )
    parser.add_argument(
        "--gm-ip",
        default=DEFAULT_GM_IP,
        metavar="IP",
        help=f"GM IP address (default: {DEFAULT_GM_IP})",
    )
    parser.add_argument(
        "--fol-ip",
        default=DEFAULT_FOL_IP,
        metavar="IP",
        help=f"Follower IP address (default: {DEFAULT_FOL_IP})",
    )
    parser.add_argument(
        "--samples",
        type=int,
        default=DEFAULT_SAMPLES,
        metavar="N",
        help=f"Number of offset samples (default: {DEFAULT_SAMPLES})",
    )
    parser.add_argument(
        "--convergence-timeout",
        type=float,
        default=DEFAULT_CONVERGENCE_TIMEOUT,
        metavar="S",
        help=f"Convergence timeout in seconds (default: {DEFAULT_CONVERGENCE_TIMEOUT})",
    )
    parser.add_argument(
        "--from-step",
        type=int,
        default=1,
        metavar="N",
        choices=range(1, 6),
        help="Start from step N (1–5, default: 1)",
    )
    parser.add_argument(
        "--no-stop-ptp",
        dest="stop_ptp",
        action="store_false",
        help="Do not send 'ptp_mode off' on both boards after the test",
    )
    parser.add_argument(
        "--log-file",
        default=None,
        metavar="FILE",
        help="Log file path (default: auto-generated ptp_test_YYYYMMDD_HHMMSS.log)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose (debug) output",
    )

    return parser


def main() -> int:
    """Main entry-point.

    Returns:
        Exit code: ``0`` = all passed, ``1`` = any failed.
    """
    parser = build_arg_parser()
    args = parser.parse_args()

    # Auto-generate log file name if not provided.
    log_file = args.log_file
    if log_file is None:
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log_file = f"ptp_test_{ts}.log"

    log = Logger(log_file=log_file, verbose=args.verbose)

    agent = PTPTestAgent(
        gm_port=args.gm_port,
        fol_port=args.fol_port,
        gm_ip=args.gm_ip,
        fol_ip=args.fol_ip,
        samples=args.samples,
        convergence_timeout=args.convergence_timeout,
        stop_ptp_after=args.stop_ptp,
        from_step=args.from_step,
        log=log,
    )

    try:
        exit_code = agent.run()
    finally:
        log.close()

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
