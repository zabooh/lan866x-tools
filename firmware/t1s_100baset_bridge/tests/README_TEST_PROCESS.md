# PTP Test Process

## Hardware Setup

| Board | Serial Port | IP Address | Role |
|-------|-------------|------------|------|
| ATSAME54P20A #1 | COM10 | 192.168.0.20 | Grandmaster (GM) |
| ATSAME54P20A #2 | COM8  | 192.168.0.30 | Follower (FOL) |

Both boards connected via 10BASE-T1S (LAN865x). Serial: 115200 baud 8N1.

---

## Regression Test — New Firmware Version

Run after every firmware build to verify basic PTP functionality:

```powershell
cd C:\work\ptp\AN1847\t1s_100baset_bridge\tests
python ptp_test_agent.py --gm-port COM10 --fol-port COM8
```

Expected result: **6/6 PASS**

```
[PASS] Step 0: Reset
[PASS] Step 1: IP Configuration
[PASS] Step 2: Network Connectivity (GM->FOL, FOL->GM)
[PASS] Step 3: PTP Start
[PASS] Step 4: Convergence to FINE state
[PASS] Step 5: Offset Validation
Overall Result: PASS (6/6 tests passed)
```

The log is saved automatically as `ptp_test_YYYYMMDD_HHMMSS.log`.

---

## Known-Good Baseline — Tag v1.0.1

Tag `v1.0.1` marks the firmware + test version that is confirmed passing.

To fall back to the known-good state:

```powershell
git checkout v1.0.1
python tests/ptp_test_agent.py --gm-port COM10 --fol-port COM8
```

This is useful to distinguish whether a failure is caused by a firmware regression
or by a test/environment problem.

---

## On-Off Resilience Test

Investigates Follower behaviour when the Grandmaster stops and restarts:

```powershell
python ptp_onoff_test.py --gm-port COM10 --fol-port COM8
```

Default scenario: GM runs 10 s → stops 5 s → restarts.  
Options:

| Option | Default | Description |
|--------|---------|-------------|
| `--gm-on-time S` | 10 | Seconds GM runs before stop |
| `--gm-off-time S` | 5 | Seconds GM is off (blackout) |
| `--cycles N` | 1 | Number of stop/restart cycles |
| `--samples N` | 10 | Offset samples per phase |
| `--convergence-timeout S` | 30 | Max seconds to wait for FINE |

The test measures:
- **Baseline offset** while GM is running (mean/stdev)
- **Blackout behaviour**: does FOL emit state transitions? Does offset drift?
- **Re-convergence time**: how long does FOL take to reach FINE after GM restart?
- **Post-restart offset**: is accuracy restored within ±100 ns?

### Expected Result (firmware ≥ v1.1.0)

```
[PASS] Cycle 1
       baseline: n=10 mean=+44.0 ns stdev=16.6 ns
       blackout: no state transitions
       FINE@0.9s (HARD_SYNC@0.3s, COARSE@0.7s)
       post: n=10 mean=+51.6 ns stdev=0.7 ns min=+51 ns max=+53 ns
Overall Result: PASS (5/5 steps/cycles passed)
```

Key metrics after GM restart:
- Re-convergence to FINE: **< 1 s**
- Post-restart offset stdev: **< 1 ns**
- Calibrated `TISUBN` reused identically (no 16-frame re-calibration needed)

### Firmware Fix History

#### Bug 1 — `hardResync` not set after `GM_RESET` (fixed 2026-04-06)
**Symptom:** FOL entered `MATCHFREQ` after GM restart but never progressed beyond
`HARDSYNC` coarse loop — stuck with ~644 ns residual indefinitely.  
**Root cause:** `resetSlaveNode()` did not set `hardResync=1`, so the first FollowUp
after reset triggered an early-return ("seqId out of sync") without performing a
hard clock-set. The clock drifted ~5 ppm (TI=40 integer) × 125 ms = 640 ns per
frame, exactly equal to the TA correction applied each cycle → infinite loop.  
**Fix:** Added `hardResync=1; diffLocal=0; diffRemote=0;` in `resetSlaveNode()`.

#### Bug 2 — Calibrated TI/TISUBN overwritten after `GM_RESET` (fixed 2026-04-06)
**Symptom:** Even with Bug 1 fixed, FOL still stuck at ~640 ns in `HARDSYNC` coarse
loop after GM restart. `TISUBN` after restart was `0x2600FFFF` (≈ 40.000 ns, no
crystal compensation) instead of the calibrated value (e.g. `0xE900FFF2`).  
**Root cause:** After `GM_RESET` the FOL re-ran the 16-frame `UNINIT` phase. During
this phase the LAN865x clock already ran at the calibrated rate, so
`diffRemote/diffLocal ≈ 1.0` → `rateRatioFIR ≈ 1.0` → `TI = 40` (5 ppm error).
This overwrote the previously computed crystal-compensation value.  
**Fix:** `calibratedTI_value` / `calibratedTISUBN_value` are saved at first
`UNINIT→MATCHFREQ` and reused directly in subsequent `resetSlaveNode()` calls,
setting `syncStatus=MATCHFREQ` immediately and skipping the UNINIT re-measurement.

---

## Test Files

| File | Purpose |
|------|---------|
| `ptp_test_agent.py` | Regression test (Steps 0–5, must pass on every firmware release) |
| `ptp_onoff_test.py` | On-off resilience test — **also part of regression suite as of v1.1.0** |

---

## Workflow: Release a New Firmware Version

1. Flash firmware to both boards.
2. Run regression test → must be 6/6 PASS:
   ```powershell
   python ptp_test_agent.py --gm-port COM10 --fol-port COM8
   ```
3. Run on-off resilience test → must be 5/5 PASS:
   ```powershell
   python ptp_onoff_test.py --gm-port COM10 --fol-port COM8
   ```
4. Commit test results / firmware changes.
5. Set a new tag:

```powershell
git add tests/ptp_test_agent.py tests/ptp_onoff_test.py
git commit -m "Description of changes"
git tag vX.Y.Z
git push origin <branch>
git push origin vX.Y.Z
```

---

## Changelog

### 2026-04-06

```
fix(ptp-follower): fix GM restart re-convergence — reuse calibrated TI/TISUBN after reset

Two firmware bugs prevented the PTP Follower from re-converging to FINE
after a Grandmaster restart:

1. hardResync not set in resetSlaveNode() (fixes HARDSYNC infinite loop)
   After GM_RESET the first FollowUp triggered an early-return ("seqId out
   of sync") without performing a hard clock-set. The clock drifted at
   ~5 ppm (integer TI=40), producing a ~640 ns residual equal to the TA
   correction applied each cycle — infinite loop in HARDSYNC coarse branch.
   Fix: set hardResync=1, diffLocal=0, diffRemote=0 in resetSlaveNode().

2. Calibrated TI/TISUBN overwritten by UNINIT re-measurement after reset
   After reset, FOL re-ran the 16-frame UNINIT phase. Because the LAN865x
   clock already ran at the calibrated rate, diffRemote/diffLocal ≈ 1.0 →
   rateRatioFIR ≈ 1.0 → TI=40 (no crystal compensation, 5 ppm error).
   Fix: save calibratedTI_value / calibratedTISUBN_value at first
   UNINIT→MATCHFREQ; on subsequent resets jump directly to MATCHFREQ
   with the stored values, skipping the UNINIT re-measurement entirely.

Result: re-convergence to FINE in < 1 s (was: timeout after 30 s).
        post-restart offset stdev: 0.7 ns.

Also:
- ptp_onoff_test.py: fix live_log, add thread-safe Logger, faulthandler,
  exception handling in convergence worker and run()
- README_TEST_PROCESS.md: document fix history, update expected results,
  promote ptp_onoff_test.py to regression suite
```
