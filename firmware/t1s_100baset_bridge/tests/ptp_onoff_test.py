#!/usr/bin/env python3
"""PTP On-Off Resilience Test for T1S 100BaseT Bridge
====================================================

Investigates how the PTP Follower behaves when the Grandmaster
stops and then restarts after a short blackout period.

Scenario (one cycle):
  1. Start PTP (Follower first, then GM) and wait for FINE convergence.
  2. Run a baseline phase: GM keeps running for --gm-on-time seconds,
     collect offset samples every second.
  3. Stop GM  (ptp_mode off) — passively observe Follower output for
     --gm-off-time seconds, sample offset values periodically.
  4. Restart GM (ptp_mode master) — monitor Follower re-convergence.
  5. Collect post-restart offset samples.

Use --cycles N to repeat the stop/restart steps (phases 2-5) N times.
The initial setup (reset, IP config, ping, PTP start) is only done once.

Usage:
    python tests/ptp_onoff_test.py --gm-port COM10 --fol-port COM8

Requirements:
    pip install pyserial
"""

import argparse
import datetime
import re
import faulthandler
import statistics
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple

# Enable faulthandler so C-level crashes (e.g. pyserial/Windows COM) dump a
# Python traceback to stderr instead of silently terminating the process.
faulthandler.enable()

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run: pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DEFAULT_GM_PORT             = "COM10"
DEFAULT_FOL_PORT            = "COM8"
DEFAULT_GM_IP               = "192.168.0.20"
DEFAULT_FOL_IP              = "192.168.0.30"
DEFAULT_NETMASK             = "255.255.255.0"
DEFAULT_BAUDRATE            = 115200
DEFAULT_CMD_TIMEOUT         = 5.0    # seconds — single command response
DEFAULT_CONVERGENCE_TIMEOUT = 30.0   # seconds — wait for FINE state
DEFAULT_GM_ON_TIME          = 10.0   # seconds GM runs before being stopped
DEFAULT_GM_OFF_TIME         = 5.0    # seconds GM is off (blackout period)
DEFAULT_SAMPLES             = 10     # offset samples per baseline/post phase
DEFAULT_CYCLES              = 1      # number of on/off cycles

OFFSET_THRESHOLD_NS         = 100    # pass criterion (nanoseconds)
BLACKOUT_SAMPLE_INTERVAL    = 1.0    # seconds between ptp_offset queries during blackout

# Regex patterns  (identical to ptp_test_agent.py for consistency)
RE_IP_SET       = re.compile(r"Set ip address OK|IP address set to")
RE_PING_REPLY   = re.compile(r"Ping:.*reply.*from|Reply from")
RE_PING_DONE    = re.compile(r"Ping: done\.")
RE_FOL_START    = re.compile(r"\[PTP\] follower mode")
RE_GM_START     = re.compile(r"\[PTP\] grandmaster mode")
RE_MATCHFREQ    = re.compile(r"UNINIT->MATCHFREQ")
RE_HARD_SYNC    = re.compile(r"Hard sync completed")
RE_COARSE       = re.compile(r"PTP COARSE")
RE_FINE         = re.compile(r"PTP FINE")
RE_PTP_OFFSET   = re.compile(r"\[PTP\] offset=([+-]?\d+)\s*ns")
RE_PTP_STATUS   = re.compile(r"\[PTP\]\s+mode=(\S+)\s+gmSyncs=(\d+)\s+gmState=(\d+)")
RE_PTP_DISABLED = re.compile(r"\[PTP\] disabled")

# State transitions of interest for the Follower
FOL_STATE_PATTERNS: Dict[str, re.Pattern] = {
    "MATCHFREQ": RE_MATCHFREQ,
    "HARD_SYNC": RE_HARD_SYNC,
    "COARSE":    RE_COARSE,
    "FINE":      RE_FINE,
}


# ---------------------------------------------------------------------------
# Logger  (identical to ptp_test_agent.py)
# ---------------------------------------------------------------------------

class Logger:
    """Dual-writes to stdout and an optional log file.  Thread-safe."""

    def __init__(self, log_file: str = None, verbose: bool = False):
        self.log_file = log_file
        self.verbose = verbose
        self._fh = None
        self._lock = threading.Lock()
        if log_file:
            self._fh = open(log_file, "w", encoding="utf-8")

    def _write(self, line: str):
        with self._lock:
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
        with self._lock:
            if self._fh:
                self._fh.close()
                self._fh = None


# ---------------------------------------------------------------------------
# Serial helpers  (identical to ptp_test_agent.py)
# ---------------------------------------------------------------------------

def open_port(port: str, baudrate: int = DEFAULT_BAUDRATE) -> serial.Serial:
    """Open a serial port with standard 8N1 non-blocking settings."""
    return serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
    )


def send_command(
    ser: serial.Serial,
    cmd: str,
    timeout: float = DEFAULT_CMD_TIMEOUT,
    log: Logger = None,
) -> str:
    """Send CLI command; collect response via 0.5 s quiet-period detection."""
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode("ascii"))
    if log:
        log.debug(f"  >> {cmd}")

    parts = []
    deadline  = time.monotonic() + timeout
    last_data = time.monotonic()

    while time.monotonic() < deadline:
        chunk = ser.read(256)
        if chunk:
            decoded = chunk.decode("ascii", errors="replace")
            parts.append(decoded)
            last_data = time.monotonic()
            if log:
                log.debug(decoded.rstrip())
        else:
            if parts and (time.monotonic() - last_data) > 0.5:
                break
            time.sleep(0.05)

    return "".join(parts)


def wait_for_pattern(
    ser: serial.Serial,
    pattern: re.Pattern,
    timeout: float,
    log: Logger = None,
    extra_patterns: dict = None,
    live_log: bool = False,
) -> Tuple[bool, float, dict]:
    """Read from *ser* until *pattern* matches or *timeout* expires.

    Returns:
        (matched, elapsed_seconds, milestones_dict)
    """
    if extra_patterns is None:
        extra_patterns = {}

    milestones: dict = {}
    buffer = ""
    start   = time.monotonic()
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
                return True, time.monotonic() - start, milestones
        else:
            time.sleep(0.05)

    return False, time.monotonic() - start, milestones


# ---------------------------------------------------------------------------
# On-Off Test Agent
# ---------------------------------------------------------------------------

class PTPOnOffTestAgent:
    """Runs the GM on/off resilience scenario.

    Steps executed once at the beginning:
      0 — Reset both boards
      1 — IP Configuration
      2 — Ping Connectivity
      3 — Initial PTP Start + Convergence to FINE

    Cycle (repeated --cycles times):
      A — Baseline: collect offsets while GM runs for --gm-on-time seconds
      B — Blackout: stop GM, observe FOL for --gm-off-time seconds
      C — Restart:  restart GM, wait for FOL re-convergence
      D — Validation: collect post-restart offset samples
    """

    def __init__(
        self,
        gm_port: str,
        fol_port: str,
        gm_ip: str = DEFAULT_GM_IP,
        fol_ip: str = DEFAULT_FOL_IP,
        netmask: str = DEFAULT_NETMASK,
        samples: int = DEFAULT_SAMPLES,
        gm_on_time: float = DEFAULT_GM_ON_TIME,
        gm_off_time: float = DEFAULT_GM_OFF_TIME,
        cycles: int = DEFAULT_CYCLES,
        convergence_timeout: float = DEFAULT_CONVERGENCE_TIMEOUT,
        cmd_timeout: float = DEFAULT_CMD_TIMEOUT,
        log: Logger = None,
    ):
        self.gm_port_name       = gm_port
        self.fol_port_name      = fol_port
        self.gm_ip              = gm_ip
        self.fol_ip             = fol_ip
        self.netmask            = netmask
        self.samples            = samples
        self.gm_on_time         = gm_on_time
        self.gm_off_time        = gm_off_time
        self.cycles             = cycles
        self.convergence_timeout = convergence_timeout
        self.cmd_timeout        = cmd_timeout
        self.log                = log or Logger()

        self.gm_ser:  Optional[serial.Serial] = None
        self.fol_ser: Optional[serial.Serial] = None

        # Step results: list of (name, passed, detail)
        self.results: list = []

        # Per-cycle data for final report
        self.cycle_data: list = []

        # Convergence thread state
        self._conv_thread: Optional[threading.Thread] = None
        self._conv_result: Optional[tuple] = None

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    def connect(self):
        """Open serial connections to both boards."""
        self.log.info(f"Connecting to GM  ({self.gm_port_name})...")
        try:
            self.gm_ser = open_port(self.gm_port_name)
            self.log.info(f"  GM  port open: {self.gm_port_name}")
        except serial.SerialException as exc:
            self.log.info(f"ERROR: Cannot open GM port {self.gm_port_name}: {exc}")
            sys.exit(1)

        self.log.info(f"Connecting to FOL ({self.fol_port_name})...")
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
    # Step 0 — Reset
    # ------------------------------------------------------------------

    def test_step_0_reset(self):
        """Send reset to both boards and wait 8 seconds."""
        step = "Step 0: Reset"
        self.log.info(f"\n--- {step} ---")
        parts = []
        for label, ser in [("GM ", self.gm_ser), ("FOL", self.fol_ser)]:
            self.log.info(f"  [{label}] reset")
            try:
                send_command(ser, "reset", self.cmd_timeout, self.log)
                parts.append(f"{label} reset sent")
                self.log.info(f"  [{label}] reset command sent")
            except Exception as exc:
                parts.append(f"{label} reset WARNING ({exc})")
                self.log.info(f"  [{label}] WARNING: reset failed (continuing): {exc}")
        self.log.info("  Waiting 8 s after reset...")
        time.sleep(8)
        self._record(step, True, "; ".join(parts))

    # ------------------------------------------------------------------
    # Step 1 — IP Configuration
    # ------------------------------------------------------------------

    def test_step_1_ip_config(self) -> bool:
        """Configure IP addresses on both boards."""
        step = "Step 1: IP Configuration"
        self.log.info(f"\n--- {step} ---")
        passed = True
        parts = []
        for label, ser, ip in [
            ("GM ", self.gm_ser, self.gm_ip),
            ("FOL", self.fol_ser, self.fol_ip),
        ]:
            cmd = f"setip eth0 {ip} {self.netmask}"
            self.log.info(f"  [{label}] {cmd}")
            resp = send_command(ser, cmd, self.cmd_timeout, self.log)
            if RE_IP_SET.search(resp):
                parts.append(f"{label} IP ok ({ip})")
                self.log.info(f"  [{label}] IP set confirmed")
            else:
                parts.append(f"{label} IP FAIL ({ip})")
                self.log.info(f"  [{label}] Unexpected response: {resp.strip()!r}")
                passed = False
        self._record(step, passed, "; ".join(parts))
        return passed

    # ------------------------------------------------------------------
    # Step 2 — Ping Connectivity
    # ------------------------------------------------------------------

    def test_step_2_connectivity(self) -> bool:
        """Ping in both directions to verify network connectivity."""
        step = "Step 2: Network Connectivity"
        self.log.info(f"\n--- {step} ---")
        passed = True
        parts = []
        for src_label, src_ser, dst_ip in [
            ("GM ->FOL", self.gm_ser, self.fol_ip),
            ("FOL->GM ", self.fol_ser, self.gm_ip),
        ]:
            cmd = f"ping {dst_ip}"
            self.log.info(f"  [{src_label}] {cmd}")
            src_ser.reset_input_buffer()
            src_ser.write((cmd + "\r\n").encode("ascii"))
            matched, elapsed, milestones = wait_for_pattern(
                src_ser, RE_PING_DONE, timeout=15.0, log=self.log,
                extra_patterns={"first_reply": RE_PING_REPLY}, live_log=True,
            )
            if matched:
                parts.append(f"{src_label} ok")
                self.log.info(f"  [{src_label}] Ping done ({elapsed:.1f}s)")
            elif milestones.get("first_reply") is not None:
                parts.append(f"{src_label} ok (partial)")
                self.log.info(f"  [{src_label}] Got reply but no done-line ({elapsed:.1f}s)")
            else:
                parts.append(f"{src_label} FAIL")
                self.log.info(f"  [{src_label}] No reply within 15 s")
                passed = False
        self._record(step, passed, "; ".join(parts))
        return passed

    # ------------------------------------------------------------------
    # Step 3 — Initial PTP Start + Convergence
    # ------------------------------------------------------------------

    def test_step_3_start_ptp(self) -> bool:
        """Start PTP on both boards and wait for initial FINE convergence."""
        step = "Step 3: Initial PTP Start + Convergence"
        self.log.info(f"\n--- {step} ---")
        passed = True
        parts = []

        # Start convergence monitor BEFORE any ptp_mode command so the
        # elapsed timer begins from PTP activation, not from quiet-period end.
        self.fol_ser.reset_input_buffer()
        self._start_convergence_thread()

        # Follower first
        self.log.info("  [FOL] ptp_mode follower")
        resp = send_command(self.fol_ser, "ptp_mode follower", self.cmd_timeout, self.log)
        if RE_FOL_START.search(resp):
            parts.append("FOL start ok")
            self.log.info("  [FOL] follower mode confirmed")
        else:
            parts.append("FOL start FAIL")
            self.log.info(f"  [FOL] Unexpected response: {resp.strip()!r}")
            passed = False

        time.sleep(0.5)

        # Grandmaster second — use pattern wait to avoid quiet-period block
        self.log.info("  [GM ] ptp_mode master")
        self.gm_ser.reset_input_buffer()
        self.gm_ser.write(b"ptp_mode master\r\n")
        gm_matched, _, _ = wait_for_pattern(
            self.gm_ser, RE_GM_START, timeout=self.cmd_timeout, log=self.log
        )
        if gm_matched:
            parts.append("GM start ok")
            self.log.info("  [GM ] grandmaster mode confirmed")
        else:
            parts.append("GM start FAIL")
            self.log.info("  [GM ] grandmaster mode not confirmed")
            passed = False

        # Collect convergence result (thread started above)
        self.log.info(f"  Waiting for FOL FINE state (timeout={self.convergence_timeout}s)...")
        matched, elapsed, milestones = self._collect_convergence_result()

        ms_str = ", ".join(f"{k}@{v:.1f}s" for k, v in milestones.items())
        if ms_str:
            self.log.info(f"  Milestones: {ms_str}")

        if matched:
            self.log.info(f"  FINE reached in {elapsed:.1f}s")
            parts.append(f"FINE@{elapsed:.1f}s ({ms_str})")
        else:
            self.log.info(f"  FINE NOT reached within {self.convergence_timeout}s")
            parts.append(f"FINE NOT reached (milestones: {ms_str or 'none'})")
            passed = False

        self._record(step, passed, "; ".join(parts))
        return passed

    # ------------------------------------------------------------------
    # Convergence thread helpers
    # ------------------------------------------------------------------

    def _start_convergence_thread(self):
        """Launch a background thread that monitors FOL for FINE state."""
        self._conv_result = None
        self._conv_thread = threading.Thread(
            target=self._convergence_worker, daemon=True
        )
        self._conv_thread.start()

    def _convergence_worker(self):
        """Background: wait for FOL to reach FINE; record milestones.

        live_log=True so all FOL output is visible as info during the wait —
        without this Phase C would appear completely silent for up to 30 s.
        """
        try:
            self._conv_result = wait_for_pattern(
                self.fol_ser,
                RE_FINE,
                timeout=self.convergence_timeout,
                log=self.log,
                extra_patterns={
                    "MATCHFREQ": RE_MATCHFREQ,
                    "HARD_SYNC": RE_HARD_SYNC,
                    "COARSE":    RE_COARSE,
                },
                live_log=True,
            )
        except Exception as exc:  # noqa: BLE001
            import traceback
            msg = f"  [CONV-THREAD ERROR] {type(exc).__name__}: {exc}\n{traceback.format_exc()}"
            self.log.info(msg)
            sys.stderr.write(msg + "\n")
            sys.stderr.flush()
            self._conv_result = (False, self.convergence_timeout, {})

    def _collect_convergence_result(self) -> Tuple[bool, float, dict]:
        """Join convergence thread and return (matched, elapsed, milestones)."""
        if self._conv_thread is not None:
            self._conv_thread.join(timeout=self.convergence_timeout + 2.0)
        if self._conv_result is not None:
            return self._conv_result
        # Should not happen; return timeout result
        return False, self.convergence_timeout, {}

    # ------------------------------------------------------------------
    # Offset helpers
    # ------------------------------------------------------------------

    def _read_one_offset(self) -> Optional[int]:
        """Send ptp_offset and return ns value, or None on timeout.

        Uses pattern-based reading (not send_command) because PTP in FINE
        emits continuous output that prevents the quiet-period from triggering.
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

    def _collect_offsets(
        self, n: int, interval: float, phase_label: str
    ) -> List[int]:
        """Collect *n* offset samples spaced by *interval* seconds."""
        offsets = []
        for i in range(n):
            val = self._read_one_offset()
            if val is not None:
                offsets.append(val)
                self.log.info(
                    f"  [{phase_label}] sample {i+1:2d}/{n}: {val:+d} ns"
                )
            else:
                self.log.info(
                    f"  [{phase_label}] sample {i+1}/{n}: no data within 2 s"
                )
            if i < n - 1:
                time.sleep(interval)
        return offsets

    # ------------------------------------------------------------------
    # GM control helpers
    # ------------------------------------------------------------------

    def _stop_gm(self) -> bool:
        """Send ptp_mode off to GM and confirm with RE_PTP_DISABLED."""
        self.log.info("  [GM ] ptp_mode off")
        self.gm_ser.reset_input_buffer()
        self.gm_ser.write(b"ptp_mode off\r\n")
        matched, _, _ = wait_for_pattern(
            self.gm_ser, RE_PTP_DISABLED, timeout=self.cmd_timeout, log=self.log
        )
        if matched:
            self.log.info("  [GM ] PTP disabled confirmed")
        else:
            self.log.info("  [GM ] WARNING: ptp_mode off not confirmed")
        return matched

    def _start_gm(self) -> bool:
        """Send ptp_mode master to GM and confirm with RE_GM_START."""
        self.log.info("  [GM ] ptp_mode master")
        self.gm_ser.reset_input_buffer()
        self.gm_ser.write(b"ptp_mode master\r\n")
        matched, _, _ = wait_for_pattern(
            self.gm_ser, RE_GM_START, timeout=self.cmd_timeout, log=self.log
        )
        if matched:
            self.log.info("  [GM ] grandmaster mode confirmed")
        else:
            self.log.info("  [GM ] WARNING: ptp_mode master not confirmed")
        return matched

    # ------------------------------------------------------------------
    # Blackout monitoring
    # ------------------------------------------------------------------

    def _monitor_blackout(
        self, duration: float
    ) -> Tuple[List[Tuple[float, int]], List[Tuple[float, str]]]:
        """Passively monitor FOL while GM is off for *duration* seconds.

        Periodically sends ptp_offset to see whether FOL still responds and
        to observe any drift.  Logs all raw FOL output with timestamps.

        Returns:
            (offset_samples, state_events) where
              offset_samples — list of (elapsed_s, ns_value)
              state_events   — list of (elapsed_s, state_label) in order seen
        """
        self.log.info(
            f"  Monitoring FOL for {duration:.0f} s "
            f"(ptp_offset every {BLACKOUT_SAMPLE_INTERVAL:.0f} s)..."
        )
        offset_samples: List[Tuple[float, int]] = []
        state_events: List[Tuple[float, str]] = []

        self.fol_ser.reset_input_buffer()
        start    = time.monotonic()
        deadline = start + duration
        next_query = start + BLACKOUT_SAMPLE_INTERVAL

        line_buf = ""  # accumulates partial lines for pretty printing

        while time.monotonic() < deadline:
            now = time.monotonic()
            elapsed = now - start

            # Periodic offset query
            if now >= next_query:
                next_query += BLACKOUT_SAMPLE_INTERVAL
                self.fol_ser.write(b"ptp_offset\r\n")
                self.log.debug(f"  >> ptp_offset  (+{elapsed:.1f}s)")

            chunk = self.fol_ser.read(256)
            if chunk:
                decoded = chunk.decode("ascii", errors="replace")
                line_buf += decoded

                # Print complete lines with elapsed timestamp
                while "\n" in line_buf:
                    line, line_buf = line_buf.split("\n", 1)
                    stripped = line.rstrip()
                    if stripped:
                        self.log.info(f"    [+{elapsed:5.1f}s] {stripped}")

                # Collect offset values appearing in this chunk
                for m in RE_PTP_OFFSET.finditer(decoded):
                    ns = int(m.group(1))
                    offset_samples.append((elapsed, ns))
                    self.log.info(
                        f"  [FOL blackout] offset={ns:+d} ns  (+{elapsed:.1f}s)"
                    )

                # Detect state transitions in this chunk
                for label, pat in FOL_STATE_PATTERNS.items():
                    if pat.search(decoded):
                        state_events.append((elapsed, label))
                        self.log.info(
                            f"  *** FOL state event: {label}  (+{elapsed:.1f}s)"
                        )
            else:
                time.sleep(0.02)

        # Flush any remaining partial line
        if line_buf.strip():
            self.log.info(f"    {line_buf.rstrip()}")

        self.log.info(
            f"  Blackout monitoring done. "
            f"Offsets captured: {len(offset_samples)}, "
            f"State events: {len(state_events)}"
        )
        return offset_samples, state_events

    # ------------------------------------------------------------------
    # One on/off cycle
    # ------------------------------------------------------------------

    def run_cycle(self, cycle_num: int) -> bool:
        """Execute phases A-D for one on/off cycle.

        Returns:
            True if all phases passed.
        """
        self.log.info(f"\n{'='*60}")
        self.log.info(f"  Cycle {cycle_num}")
        self.log.info(f"{'='*60}")
        cycle_passed = True

        cycle_record = {
            "cycle":             cycle_num,
            "baseline_offsets":  [],
            "blackout_offsets":  [],
            "blackout_events":   [],
            "reconv_matched":    False,
            "reconv_time":       None,
            "reconv_milestones": {},
            "post_offsets":      [],
            "passed":            False,
        }

        # ----------------------------------------------------------
        # Phase A — Baseline: GM running, collect offsets
        # ----------------------------------------------------------
        self.log.info(
            f"\n  [Phase A] Baseline — GM on for {self.gm_on_time:.0f} s, "
            f"{self.samples} offset samples"
        )
        sample_interval = max(0.2, self.gm_on_time / self.samples)
        baseline = self._collect_offsets(self.samples, sample_interval, "baseline")
        cycle_record["baseline_offsets"] = baseline

        if baseline:
            mean_b = statistics.mean(baseline)
            stdev_b = statistics.stdev(baseline) if len(baseline) > 1 else 0.0
            self.log.info(
                f"  Baseline: n={len(baseline)} "
                f"mean={mean_b:+.1f} ns  stdev={stdev_b:.1f} ns"
            )
        else:
            self.log.info("  WARNING: No baseline offsets collected")
            cycle_passed = False

        # ----------------------------------------------------------
        # Phase B — Stop GM, observe FOL during blackout
        # ----------------------------------------------------------
        self.log.info(
            f"\n  [Phase B] Stopping GM, observing FOL for {self.gm_off_time:.0f} s"
        )
        gm_stopped = self._stop_gm()
        if not gm_stopped:
            self.log.info("  WARNING: GM stop not confirmed — continuing anyway")

        blackout_offsets, blackout_events = self._monitor_blackout(self.gm_off_time)
        cycle_record["blackout_offsets"] = blackout_offsets
        cycle_record["blackout_events"]  = blackout_events

        # Summarise what happened during blackout
        if blackout_events:
            events_str = ", ".join(f"{l}@+{t:.1f}s" for t, l in blackout_events)
            self.log.info(f"  FOL state events during blackout: {events_str}")
        else:
            self.log.info("  FOL state events: none (FOL stayed silent / no transitions)")

        if blackout_offsets:
            vals = [v for _, v in blackout_offsets]
            drift = max(vals) - min(vals)
            self.log.info(
                f"  Blackout offsets: first={vals[0]:+d} ns  last={vals[-1]:+d} ns  "
                f"drift_range={drift} ns"
            )
        else:
            self.log.info("  Blackout offsets: none received")

        # ----------------------------------------------------------
        # Phase C — Restart GM, wait for FOL re-convergence
        # ----------------------------------------------------------
        self.log.info(
            f"\n  [Phase C] Restarting GM, waiting for FOL re-convergence "
            f"(timeout={self.convergence_timeout:.0f} s)"
        )

        # Start convergence thread BEFORE GM restart command so the
        # timer captures the moment PTP becomes active again.
        sys.stderr.write("TRACE PhaseC-1: reset_input_buffer\n"); sys.stderr.flush()
        self.fol_ser.reset_input_buffer()
        sys.stderr.write("TRACE PhaseC-2: start_convergence_thread\n"); sys.stderr.flush()
        self._start_convergence_thread()
        sys.stderr.write("TRACE PhaseC-3: _start_gm\n"); sys.stderr.flush()

        gm_started = self._start_gm()
        sys.stderr.write(f"TRACE PhaseC-4: gm_started={gm_started}\n"); sys.stderr.flush()
        if not gm_started:
            self.log.info("  WARNING: GM restart not confirmed — convergence may fail")

        matched, elapsed, milestones = self._collect_convergence_result()
        cycle_record["reconv_matched"]    = matched
        cycle_record["reconv_time"]       = elapsed
        cycle_record["reconv_milestones"] = milestones

        ms_str = ", ".join(f"{k}@{v:.1f}s" for k, v in milestones.items())
        if ms_str:
            self.log.info(f"  Re-convergence milestones: {ms_str}")

        if matched:
            self.log.info(f"  FOL re-converged to FINE in {elapsed:.1f} s")
        else:
            self.log.info(
                f"  FOL did NOT re-converge within {self.convergence_timeout:.0f} s  "
                f"(milestones: {ms_str or 'none'})"
            )
            cycle_passed = False

        # ----------------------------------------------------------
        # Phase D — Post-restart offset validation
        # ----------------------------------------------------------
        self.log.info(
            f"\n  [Phase D] Post-restart offset validation ({self.samples} samples)"
        )

        if not matched:
            self.log.info("  Skipping offset collection — FOL not in FINE")
        else:
            post = self._collect_offsets(self.samples, 0.5, "post")
            cycle_record["post_offsets"] = post

            if post:
                mean_p  = statistics.mean(post)
                stdev_p = statistics.stdev(post) if len(post) > 1 else 0.0
                within  = [v for v in post if abs(v) <= OFFSET_THRESHOLD_NS]
                pct     = 100.0 * len(within) / len(post)
                self.log.info(
                    f"  Post-restart: n={len(post)} "
                    f"mean={mean_p:+.1f} ns  stdev={stdev_p:.1f} ns  "
                    f"within +/-{OFFSET_THRESHOLD_NS} ns: "
                    f"{len(within)}/{len(post)} ({pct:.0f}%)"
                )
                if len(within) < len(post):
                    cycle_passed = False
            else:
                self.log.info("  WARNING: No post-restart offsets collected")
                cycle_passed = False

        cycle_record["passed"] = cycle_passed
        self.cycle_data.append(cycle_record)
        return cycle_passed

    # ------------------------------------------------------------------
    # Full test
    # ------------------------------------------------------------------

    def run(self) -> int:
        """Execute setup steps + all cycles and print the final report.

        Returns:
            0 if everything passed, 1 otherwise.
        """
        start_time = datetime.datetime.now()

        self.log.info("=" * 60)
        self.log.info("  PTP On-Off Resilience Test — T1S 100BaseT Bridge")
        self.log.info("=" * 60)
        self.log.info(f"Date               : {start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log.info(f"GM Port            : {self.gm_port_name} ({self.gm_ip})")
        self.log.info(f"Follower Port      : {self.fol_port_name} ({self.fol_ip})")
        self.log.info(f"GM on time         : {self.gm_on_time:.0f} s")
        self.log.info(f"GM off time        : {self.gm_off_time:.0f} s")
        self.log.info(f"Cycles             : {self.cycles}")
        self.log.info(f"Samples / phase    : {self.samples}")
        self.log.info(f"Convergence timeout: {self.convergence_timeout:.0f} s")

        self.connect()

        try:
            # ---- One-time setup ----
            self.test_step_0_reset()

            if not self.test_step_1_ip_config():
                self.log.info("\nAborting: IP configuration failed.")
                return self._print_report(start_time)

            if not self.test_step_2_connectivity():
                self.log.info("\nAborting: Network connectivity check failed.")
                return self._print_report(start_time)

            if not self.test_step_3_start_ptp():
                self.log.info("\nAborting: Initial PTP convergence failed.")
                return self._print_report(start_time)

            # ---- On/off cycles ----
            for c in range(1, self.cycles + 1):
                passed = self.run_cycle(c)
                step_name = f"Cycle {c}"
                detail = self._cycle_summary_str(self.cycle_data[-1])
                self._record(step_name, passed, detail)
                if not passed:
                    self.log.info(f"\nCycle {c} FAILED.")

        except KeyboardInterrupt:
            self.log.info("\n\nInterrupted by user (Ctrl+C).")
        except Exception as exc:
            self.log.info(f"\nFATAL ERROR: {type(exc).__name__}: {exc}")
            import traceback
            self.log.info(traceback.format_exc())
        finally:
            self._cleanup()

        return self._print_report(start_time)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _cleanup(self):
        """Stop PTP on both boards and close serial ports."""
        self.log.info("\n--- Cleanup ---")
        for label, ser in [("GM ", self.gm_ser), ("FOL", self.fol_ser)]:
            if ser and ser.is_open:
                resp = send_command(ser, "ptp_mode off", self.cmd_timeout, self.log)
                if RE_PTP_DISABLED.search(resp):
                    self.log.info(f"  [{label}] PTP disabled")
                else:
                    self.log.info(f"  [{label}] ptp_mode off response: {resp.strip()!r}")
        self.disconnect()

    def _record(self, name: str, passed: bool, detail: str):
        self.results.append((name, passed, detail))

    @staticmethod
    def _offset_stats_str(offsets: List[int]) -> str:
        if not offsets:
            return "n/a"
        mean_v = statistics.mean(offsets)
        stdev_v = statistics.stdev(offsets) if len(offsets) > 1 else 0.0
        return (
            f"n={len(offsets)} mean={mean_v:+.1f} ns "
            f"stdev={stdev_v:.1f} ns "
            f"min={min(offsets):+d} ns max={max(offsets):+d} ns"
        )

    def _cycle_summary_str(self, cd: dict) -> str:
        parts = []
        parts.append(f"baseline: {self._offset_stats_str(cd['baseline_offsets'])}")
        if cd["blackout_events"]:
            ev = ", ".join(f"{l}@+{t:.1f}s" for t, l in cd["blackout_events"])
            parts.append(f"blackout events: {ev}")
        else:
            parts.append("blackout: no state transitions")
        if cd["reconv_matched"]:
            ms_str = ", ".join(
                f"{k}@{v:.1f}s" for k, v in cd["reconv_milestones"].items()
            )
            parts.append(f"FINE@{cd['reconv_time']:.1f}s ({ms_str})")
        else:
            parts.append("re-convergence FAILED")
        if cd["post_offsets"]:
            parts.append(f"post: {self._offset_stats_str(cd['post_offsets'])}")
        return "; ".join(parts)

    def _print_report(self, start_time: datetime.datetime) -> int:
        """Print final summary report. Returns 0 = pass, 1 = fail."""
        self.log.info("\n" + "=" * 60)
        self.log.info("  PTP On-Off Resilience Test — Final Report")
        self.log.info("=" * 60)
        self.log.info(f"Date        : {start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log.info(f"GM Port     : {self.gm_port_name} ({self.gm_ip})")
        self.log.info(f"FOL Port    : {self.fol_port_name} ({self.fol_ip})")
        self.log.info(f"GM on       : {self.gm_on_time:.0f} s")
        self.log.info(f"GM off      : {self.gm_off_time:.0f} s")
        self.log.info(f"Cycles      : {self.cycles}")
        self.log.info("")

        passed_count = 0
        for name, passed, detail in self.results:
            tag = "PASS" if passed else "FAIL"
            self.log.info(f"[{tag}] {name}")
            if detail:
                # Wrap long detail lines at semicolons for readability
                for part in detail.split("; "):
                    self.log.info(f"       {part}")
            if passed:
                passed_count += 1

        # Per-cycle detailed table
        if self.cycle_data:
            self.log.info("")
            self.log.info("  Per-Cycle Summary:")
            self.log.info(
                f"  {'Cycle':>5}  {'FINE?':>6}  {'ReconvTime':>10}  "
                f"{'Blackout events':<25}  {'Post-mean':>10}  {'Post-stdev':>10}"
            )
            self.log.info("  " + "-" * 74)
            for cd in self.cycle_data:
                fine_str  = f"{cd['reconv_time']:.1f}s" if cd["reconv_matched"] else "FAIL"
                event_str = (
                    ", ".join(l for _, l in cd["blackout_events"])
                    if cd["blackout_events"] else "-"
                )
                if cd["post_offsets"]:
                    po = cd["post_offsets"]
                    pmean  = f"{statistics.mean(po):+.1f}ns"
                    pstdev = f"{statistics.stdev(po) if len(po)>1 else 0.0:.1f}ns"
                else:
                    pmean = pstdev = "n/a"
                reconv_col = fine_str if cd["reconv_matched"] else "n/a"
                self.log.info(
                    f"  {cd['cycle']:>5}  {fine_str:>6}  "
                    f"{reconv_col:>10}  "
                    f"{event_str:<25}  {pmean:>10}  {pstdev:>10}"
                )

        total   = len(self.results)
        overall = "PASS" if passed_count == total else "FAIL"
        self.log.info("")
        self.log.info(
            f"Overall Result: {overall} ({passed_count}/{total} steps/cycles passed)"
        )
        if self.log.log_file:
            self.log.info(f"Log saved to  : {self.log.log_file}")

        return 0 if passed_count == total else 1


# ---------------------------------------------------------------------------
# CLI entry-point
# ---------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "PTP On-Off Resilience Test for T1S 100BaseT Bridge\n"
            "Tests Follower behaviour during GM stop/restart cycles."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--gm-port",  default=DEFAULT_GM_PORT,
                        metavar="PORT", help=f"Grandmaster COM port (default: {DEFAULT_GM_PORT})")
    parser.add_argument("--fol-port", default=DEFAULT_FOL_PORT,
                        metavar="PORT", help=f"Follower COM port (default: {DEFAULT_FOL_PORT})")
    parser.add_argument("--gm-ip",    default=DEFAULT_GM_IP,
                        metavar="IP",   help=f"GM IP address (default: {DEFAULT_GM_IP})")
    parser.add_argument("--fol-ip",   default=DEFAULT_FOL_IP,
                        metavar="IP",   help=f"Follower IP (default: {DEFAULT_FOL_IP})")
    parser.add_argument("--gm-on-time", type=float, default=DEFAULT_GM_ON_TIME,
                        metavar="S",  help=f"Seconds GM runs before stop (default: {DEFAULT_GM_ON_TIME})")
    parser.add_argument("--gm-off-time", type=float, default=DEFAULT_GM_OFF_TIME,
                        metavar="S",  help=f"Seconds GM is off / blackout (default: {DEFAULT_GM_OFF_TIME})")
    parser.add_argument("--cycles", type=int, default=DEFAULT_CYCLES,
                        metavar="N",  help=f"Number of on/off cycles (default: {DEFAULT_CYCLES})")
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES,
                        metavar="N",  help=f"Offset samples per phase (default: {DEFAULT_SAMPLES})")
    parser.add_argument("--convergence-timeout", type=float, default=DEFAULT_CONVERGENCE_TIMEOUT,
                        metavar="S",  help=f"Max seconds to wait for FINE (default: {DEFAULT_CONVERGENCE_TIMEOUT})")
    parser.add_argument("--log-file", default=None, metavar="FILE",
                        help="Log file (default: auto-generated ptp_onoff_YYYYMMDD_HHMMSS.log)")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose debug output")
    return parser


def main() -> int:
    parser = build_arg_parser()
    args   = parser.parse_args()

    log_file = args.log_file
    if log_file is None:
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log_file = f"ptp_onoff_{ts}.log"

    log = Logger(log_file=log_file, verbose=args.verbose)

    agent = PTPOnOffTestAgent(
        gm_port             = args.gm_port,
        fol_port            = args.fol_port,
        gm_ip               = args.gm_ip,
        fol_ip              = args.fol_ip,
        samples             = args.samples,
        gm_on_time          = args.gm_on_time,
        gm_off_time         = args.gm_off_time,
        cycles              = args.cycles,
        convergence_timeout = args.convergence_timeout,
        log                 = log,
    )

    try:
        exit_code = agent.run()
    finally:
        log.close()

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
