# Software NTP time sync on the T1S bridge — implementation, operation & convergence

This document is the practical reference for the **software NTP time
synchronisation** between the PC and the bridge firmware: **what is implemented, how
it works, and how the convergence is achieved** — illustrated with a **real
`ntp watch` run**.

It pulls the conceptual model and diagrams from the theory note
[NTP_TWO_NODE_CONVERGENCE.md](NTP_TWO_NODE_CONVERGENCE.md) and complements the
usage/bridge-delay write-up in [NTP_TIMING.md](NTP_TIMING.md). The planned
**multi-node** extension (1:N discovery, configuration, sync and pin-toggle
verification) is a design concept in
[NTP_MULTINODE_SZENARIO.md](NTP_MULTINODE_SZENARIO.md).

- Firmware (follower): `firmware/t1s_100baset_bridge/firmware/src/ntp_sync.c`
- PC (master): `ntpsync.c` → `lan866x-ntpsync`

## Contents

- [1. What is implemented](#1-what-is-implemented)
- [2. How it works](#2-how-it-works)
  - [2.1 The free-running counter](#21-the-free-running-counter)
  - [2.2 The exchange (t1/t2/t3/t4)](#22-the-exchange-t1t2t3t4)
  - [2.3 The offset is only as good as the path symmetry](#23-the-offset-is-only-as-good-as-the-path-symmetry)
  - [2.4 Why a one-shot sync drifts away](#24-why-a-one-shot-sync-drifts-away)
- [3. How convergence is done — the PI discipline](#3-how-convergence-is-done--the-pi-discipline)
- [4. Convergence in practice — a real `ntp watch` run](#4-convergence-in-practice--a-real-ntp-watch-run)
  - [4.1 The three columns](#41-the-three-columns)
  - [4.2 The whole run](#42-the-whole-run)
  - [4.3 The lock-in (first ~8 s)](#43-the-lock-in-first-8-s)
  - [4.4 Steady state: jitter floor vs the mean](#44-steady-state-jitter-floor-vs-the-mean)
  - [4.5 Why `drift` wanders — and the Ki trade-off](#45-why-drift-wanders--and-the-ki-trade-off)
  - [4.6 Holdover: missed syncs stay sub-millisecond](#46-holdover-missed-syncs-stay-sub-millisecond)
- [5. What the hardware time base changed — a live before/after](#5-what-the-hardware-time-base-changed--a-live-beforeafter)
  - [5.1 The headline — the oscillator drift collapses ~40×](#51-the-headline--the-oscillator-drift-collapses-40)
  - [5.2 The live run on the HW clock](#52-the-live-run-on-the-hw-clock)
  - [5.3 What did not change — and why](#53-what-did-not-change--and-why)
  - [5.4 Holdover: now limited by the loop, not the oscillator](#54-holdover-now-limited-by-the-loop-not-the-oscillator)
  - [5.5 On the wire (tshark, Ethernet 8)](#55-on-the-wire-tshark-ethernet-8)
- [6. Accuracy, limits & what's next](#6-accuracy-limits--whats-next)

---

## 1. What is implemented

**Firmware follower** (`ntp_sync.c`):
- A free-running high-resolution **NTP counter** (`SYS_TIME` ns + signed offset).
- A **UDP service on port 30491** (not pinned to an interface) speaking a 4-timestamp
  exchange + a `SET_OFFSET` discipline message; big-endian signed-64-bit ns.
- A **PI frequency discipline** that turns the stream of corrections into a phase
  *and* a frequency term, cancelling the oscillator drift (the core of this doc).
- The on-board CLI: **`ntp`** (status snapshot) and **`ntp watch`** (live per-sync
  output with convergence columns).
- An eth0 timestamp **tap** used by the bridge-delay measurement (see
  [NTP_TIMING.md](NTP_TIMING.md)).

**PC master** (`lan866x-ntpsync`):
- Runs the t1/t2/t3/t4 rounds, takes the **median offset of the lowest-delay rounds**
  (robust min-delay filter), raises the Windows timer resolution to **1 ms**, and
  sends `SET_OFFSET`. Continuous (every 250 ms) by default; `--once` for a single sync.

**Headline results** (measured on the SAME54 bridge):

| Quantity | Value |
|---|---|
| Counter resolution | ~16 ns (SYS_TIME @ 60 MHz) |
| Free-running oscillator drift | ~**1850 ppm** (frequency-locked & tracked) |
| Per-sync offset jitter (steady state) | σ ≈ **150 µs** (the floor) |
| Rolling-mean offset (steady state) | **+1.4 µs**, σ ≈ **13 µs** (converged & unbiased) |
| **Holdover** after 6 s without sync | **~58 µs** (vs ~9.6 ms un-disciplined) |

> These are the **original DFLL-era** numbers (`SYS_TIME` on the open-loop DFLL). The
> bridge now runs the NTP counter on a **disciplined hardware clock** (XOSC1→DPLL1→TC2
> @ 96 MHz) — for the live **before/after** (drift ~1864 → ~45 ppm, what changed and what
> didn't), see [§5](#5-what-the-hardware-time-base-changed--a-live-beforeafter).

---

## 2. How it works

### 2.1 The free-running counter

The follower keeps a 64-bit counter in nanoseconds:

```
ntp_now_ns() = raw + offset + rate·(raw − last_sync)
raw          = SYS_TIME counter → ns (overflow-safe: sec·1e9 + frac·1e9/freq)
```

`raw` starts at 0 at boot. `offset` (phase) and `rate` (frequency correction,
`s_rate_ppb`) are what the discipline sets — see §3.

### 2.2 The exchange (t1/t2/t3/t4)

![NTP exchange: the t1/t2/t3/t4 quartet](img/ntp_exchange.png)

```
offset  θ = ((t2 − t1) + (t3 − t4)) / 2      # follower clock − PC clock
delay   δ = (t4 − t1) − (t3 − t2)            # round-trip transit
```

The PC keeps the lowest-`δ` rounds, then sends `SET_OFFSET` with `adjust = −θ` so the
follower moves toward PC time, plus the measured `δ`.

### 2.3 The offset is only as good as the path symmetry

θ is exact **only if the forward and reverse paths are equal**. Otherwise
`θ_measured = θ_true + (d→ − d←)/2`. The error splits into three classes — and only
one averages away:

| Error class | Cause | Under averaging of N samples |
|---|---|---|
| **Random** (white phase noise) | software timestamp jitter, scheduling | falls as **1/√N** |
| **Drift** (frequency) | oscillator runs fast/slow, temperature | does **not** average — must be *modelled* (§3) |
| **Systematic** (asymmetry bias) | constant unequal fwd/rev processing | does **not** average — stays as a floor |

### 2.4 Why a one-shot sync drifts away

The oscillator drifts ~1850 ppm → in one 250 ms sync interval the clock slips
`1850 ppm × 250 ms ≈ 460 µs`. Correcting only the **phase** each sync leaves a
**sawtooth**; averaging cannot remove it because the clock keeps drifting *between*
samples.

![Drift sawtooth vs frequency discipline](img/ntp_sawtooth.png)

The fix is to also learn and apply a **frequency** correction — then the line between
syncs is flat instead of a ramp.

---

## 3. How convergence is done — the PI discipline

The follower does **not** just step the phase. On every `SET_OFFSET` it runs a small
**PI loop** on the correction stream (`adjust = −measured offset`):

```c
// consolidate the rate accrued since the last sync into the phase (stay continuous)
s_offset_ns += rate_held(raw);
s_lastInterval = raw - s_lastSyncRaw;  s_lastSyncRaw = raw;

// I term: integrate the residual (as ppb) into the frequency correction
if (synced && interval > 0) {
    int64_t drift_ppb = (adjust * 1e6) / (interval_us);   // ns/interval → ppb
    s_rate_ppb += drift_ppb / NTP_KI_DEN;                 // Ki = 1/4
}
// P term: correct the phase now (full step)
s_offset_ns += adjust;
```

and `ntp_now_ns()` accrues `rate·(raw − last_sync)` between syncs. Key points:

- **Phase (P)** aligns the clock immediately each sync (as before).
- **Frequency (I)** learns the oscillator drift from the *stream* of corrections and
  keeps compensating it between syncs. The huge first-sync correction is skipped (the
  loop only starts integrating once `synced`), so only the small per-interval residual
  feeds the frequency term.
- The **wire protocol is unchanged** — the PC still only sends `adjust`. All the
  "compute the frequency compensation and apply it" happens in the firmware, fed by the
  PC's sync packets.
- It is **adaptive**, not a one-time calibration: it follows a slowly changing drift
  (e.g. temperature) as long as syncs keep arriving.

The **random** part of the residual still falls as `σ/√N`, but only down to the
asymmetry/transit floor — the convergence plateaus there (theory:
[NTP_TWO_NODE_CONVERGENCE.md](NTP_TWO_NODE_CONVERGENCE.md)):

![Random error σ/√N down to the floor](img/ntp_convergence.png)

---

## 4. Convergence in practice — a real `ntp watch` run

The following is a real, ~2.5-minute `ntp watch` capture taken **from a fresh boot**
while `lan866x-ntpsync` ran continuously. (Diagrams generated from the raw trace by
[`ntp_trace_plot.py`](ntp_trace_plot.py).)

### 4.1 The three columns

`ntp watch` prints three quantities per line, chosen so the convergence is visible
even though the raw measurement never gets quieter:

- **`offset`** — this sync's raw measured offset. This is the single-shot measurement
  **jitter** (bounded by ~`δ/2`); it does **not** shrink.
- **`mean`** — rolling mean of the last 16 offsets. **Converges to ~0** as the loop
  locks. The huge initial phase-lock offset is excluded so it can't poison the mean.
- **`drift`** — the **frequency-locked** oscillator drift (`s_rate_ppb` in ppm).
  Ramps up from 0 and then **settles**.

### 4.2 The whole run

![ntp watch overview: offset, mean, drift over 2.5 min](img/ntp_trace_overview.png)

Three things to read off it:
1. **`offset`** (grey dots) stays a wide ±~200 µs cloud the whole time — the jitter
   floor, untouched by the discipline.
2. **`mean`** (red) drops out of the lock-in band in the first ~5 s and then **hugs 0**
   for the rest of the run.
3. **`drift`** (blue, right axis) climbs to ~1850 ppm in the first few syncs and then
   **wanders around that level** (it is being continuously re-estimated and also tracks
   the oscillator warming).

### 4.3 The lock-in (first ~8 s)

![Lock-in: frequency converges, mean collapses](img/ntp_trace_lockin.png)

From the trace: `mean` goes `411 → 272 → 185 → 120 → 25 → ~0 µs` and `drift` goes
`+1404 → +1858 → +1898 → … → ~1850 ppm` over the first 5–6 syncs (~1.5 s of real
time; printed ~1/s). **That ramp is the convergence** — it lives in `mean` and
`drift`, not in the per-sync `offset`.

### 4.4 Steady state: jitter floor vs the mean

![Steady-state distribution: raw offset wide, mean tight and ~0](img/ntp_trace_hist.png)

Steady-state statistics (t ≥ 6 s, from the trace):

| Quantity | mean | σ | range |
|---|---|---|---|
| raw `offset` | +4.0 µs | **151.6 µs** | −392 … +446 µs |
| rolling `mean` | **+1.4 µs** | **13.3 µs** | — |
| `drift` | 1864 ppm | 126 ppm | — |

The raw offset is a wide, zero-centred cloud (the measurement jitter floor). The
rolling mean is **~10× tighter and centred on ~0**. That the mean sits at **+1.4 µs**
— not at a constant +150 µs — is the empirical proof that there is **no significant
path asymmetry** here: a one-time asymmetry calibration would buy essentially nothing.
(If the mean parked at a clear non-zero value over many lines, *that* would indicate a
calibratable bias.)

### 4.5 Why `drift` wanders — and the Ki trade-off

The integral is nudged by `(adjust/interval)/Ki` each sync, and `adjust` carries the
per-sync offset noise: ±200 µs over 250 ms is **±800 ppm instantaneous**, so with
`Ki = 1/4` each step moves the rate by up to ~±200 ppm. Hence the ±126 ppm wander.

This is **cosmetic**: timekeeping quality shows up in `mean ≈ 0` and in the holdover
(§4.6), not in how steady the `drift` column looks. For a *quieter* drift estimate
(slightly better holdover, slower thermal tracking) increase `NTP_KI_DEN` (smaller Ki
= more smoothing); for faster tracking, decrease it. It is the one tuning knob.

### 4.6 Holdover: missed syncs stay sub-millisecond

Two places in the run show the frequency lock holding when syncs stop:

- **The ~8 s gap** at 18:39:20 → 18:39:28 (marked "missed syncs" in §4.2): the next
  offset is only **+446 µs**, not `8 s × 1850 ppm ≈ 14.7 ms`. The learned rate kept
  compensating.
- **The final `ntp` status**, taken after `watch` stopped and the PC sync was ended:

```text
  synced     : YES (490 sync msg)
  last sync  : 7.119 s ago
  osc. drift : +1835 ppm  (frequency-locked, applied)
  residual   : ~82.522 us offset at last sync (freq lock keeps holdover slow)
  local time : 18:40:09.999 (GMT+2)
```

7 s after the last sync the clock is still good — the holdover is governed by the
*residual* drift the loop hasn't captured (a few ppm), not the full ~1850 ppm. The
dedicated holdover test elsewhere measured **~58 µs after 6 s**.

---

## 5. What the hardware time base changed — a live before/after

Everything above describes the follower running on **`SYS_TIME` = the open-loop DFLL**
(~1850 ppm). The bridge now runs its NTP counter on a **disciplined hardware clock**
instead — **XOSC1 (12 MHz MEMS) → DPLL1 → TC2 @ 96 MHz** — brought up and tested
step-by-step in
[HW_TIMEBASE_BRINGUP_STEPS.md](../firmware/t1s_100baset_bridge/docs/HW_TIMEBASE_BRINGUP_STEPS.md).
`ntp_raw_ns()` simply reads `hwclock_now_ns()` now; the PI discipline (§3) is unchanged.

The same `ntp watch` test was re-run on it (**fresh boot, continuous `lan866x-ntpsync`,
PC sync deliberately PAUSED for ~31 s** to expose the holdover), with the NTP traffic
captured in parallel by **tshark on `Ethernet 8`** (`udp port 30491`). New diagrams
([`ntp_trace_plot_hwclk.py`](ntp_trace_plot_hwclk.py)) sit next to the old ones so the
change is visible; the DFLL-era figures above are kept for the comparison.

### 5.1 The headline — the oscillator drift collapses ~40×

![Drift the loop must remove: DFLL ~1864 ppm vs HW clock ~45 ppm](img/ntp_drift_compare.png)

The `drift` column is the **frequency error the discipline has to remove**. On the
open-loop DFLL it parked at **~1864 ppm**; on the XOSC1→DPLL1→TC2 clock it now hugs
**~+45 ppm** — the raw MEMS-oscillator drift, **~41× smaller**. This is the whole point
of the hardware time base: the *physical* clock is intrinsically stable, so the loop is
barely correcting anything.

### 5.2 The live run on the HW clock

![ntp watch on the HW clock: drift hugs ~+45 ppm, mean still → 0, with the paused-sync window](img/ntp_trace_overview_hwclk.png)

Same shape as before — `offset` is the same wide jitter cloud, `mean` still collapses to
~0 — but the blue `drift` trace now lives near **+45 ppm** (dotted line marks the old
~1864 ppm level for scale). The grey band is the **31 s with the PC sync stopped**.

### 5.3 What did **not** change — and why

| Quantity | old: `SYS_TIME` / open-loop DFLL | new: TC2 / XOSC1→DPLL1 |
|---|---|---|
| Counter resolution | ~16 ns (60 MHz) | ~10 ns (96 MHz) |
| **Oscillator drift the loop fights** | **~1864 ppm** (σ 126) | **~+45 ppm** (σ 99) — **~41× less** |
| Per-sync offset jitter σ | ~152 µs | **~159 µs — unchanged** |
| Rolling-mean offset (converged) | +1.4 µs → ~0 | +38 µs → ~0 |
| Round-trip delay | ~0.5–1 ms | ~0.74 ms (min 0.39) |

The **per-sync jitter floor is unchanged (~150 µs)** — it is set by *software* timestamping
and the T1S round-trip, **not** the oscillator, so a better clock cannot move it (only
hardware/PTP timestamping can; that is `net_10base_t1s`). The 16 → 10 ns resolution bump is
irrelevant against the µs-scale floor. **The clock changed exactly one thing: the drift.**

### 5.4 Holdover: now limited by the *loop*, not the oscillator

![Holdover: 31 s without sync, the clock free-runs then re-locks in one sync](img/ntp_trace_holdover_hwclk.png)

When the PC sync stopped for 31 s, the next offset jumped to **−4.9 ms** (then re-locked in
a single sync). That looks worse than the DFLL run's holdover — and it is honest to say
**the clock alone did not improve holdover**. The reason is subtle and important:

- The software frequency term `s_rate_ppb` still **wanders ±99 ppm**, because it integrates
  the *same* ±150 µs per-sync offset noise (Ki = 1/4) — see §4.5. That wander is unchanged.
- With the **old DFLL** the loop's job was to track a **large but slowly-changing** drift
  (~1850 ppm, warming): its frozen value at a sync gap was close to the truth, so holdover
  stayed small.
- With the **new stable clock** the true drift is only ~+45 ppm, but `s_rate_ppb` keeps
  wandering ±99 ppm around it. Frozen at a noisy excursion during the pause, it
  **over-corrects** by ~150 ppm → the −4.9 ms over 31 s. The holdover is now the *loop noise*,
  not the oscillator.

So the hardware clock **moved the bottleneck**: drift is solved, and what remains is the
NTP-transport jitter feeding the software loop. Two ways to cash in the now-stable clock:
**(a)** slow the loop down (raise `NTP_KI_DEN`) — affordable now that the true drift is tiny
and stable, so `s_rate_ppb` settles near +45 ppm with little noise; **(b)** the proper fix —
move the frequency correction into **hardware** (LDRFRAC), so the *physical* TC counts at the
true rate and there is no software rate term to freeze. (b) is exactly what the bring-up
attempted; it is **blocked on this Rev D silicon** — an on-the-fly `DPLLRATIO` write stalls
the DPLL (errata 2.13.x), so the correction stays in software for now. See
[HW_TIMEBASE_BRINGUP_STEPS.md §6](../firmware/t1s_100baset_bridge/docs/HW_TIMEBASE_BRINGUP_STEPS.md).

### 5.5 On the wire (tshark, `Ethernet 8`)

The parallel capture ([`ntp_hwclk_capture.pcapng`](ntp_hwclk_capture.pcapng), `udp port
30491`, decoded by the bundled **`lan866x_ntp`** dissector) shows the mechanism
end-to-end: **1997 frames over ~60 s**, one sync = ~8 REQUEST(9 B)/
REPLY(25 B) min-delay rounds + a `SET_OFFSET`(17 B) + `SET_ACK`(9 B), every 250 ms, raw
round-trip **~0.45 ms**. The **31 s sync pause is plainly visible as a gap with no packets**
— the firmware free-runs on the HW clock through it (that is the −4.9 ms holdover above),
then traffic resumes and it re-locks in one sync.

---

## 6. Accuracy, limits & what's next

- **Per-sync jitter floor (~150 µs σ):** set by *software* timestamping (superloop +
  stack + SPI MAC-PHY) and the round-trip — **not** by the 16 ns counter, and **not**
  reducible by the frequency discipline. Only **hook-level / hardware timestamping
  (PTP)** lowers it; that is the sister project `net_10base_t1s`.
- **Asymmetry:** empirically ~0 here (mean ≈ +1.4 µs), so no calibration needed; if it
  ever showed, a one-time calibration would remove it.
- **Frequency discipline (this doc):** removes the drift/sawtooth — the big lever. It
  is why the holdover is ~58 µs instead of ~9.6 ms over 6 s, and why the live mean is
  unbiased.
- **Oscillator / where the timer comes from:** the ~1850 ppm is not arbitrary — it is
  exactly what the clock tree predicts. `SYS_TIME` is **TC0** (16-bit, prescaler
  DIV1), and its 60 MHz derives — verified in
  `config/default/peripheral/clock/plib_clock.c` — from the **internal DFLL48M running
  open-loop** (both `OSCCTRL_Initialize()` and `DFLL_Initialize()` are empty stubs, so
  **no external crystal** (XOSC0/XOSC1) is enabled and the DFLL stays in its
  free-running reset default):

```
internal DFLL48M  (open-loop, RC-based, factory-trimmed only → temperature-dependent)
   │  GCLK_GENCTRL[2] = SRC(6)=DFLL ÷48
 1 MHz ──► FDPLL0 reference (DPLLCTRLB.REFCLK=GCLK, PCHCTRL[1]=GEN2); DPLLRATIO LDR=119 → ×120
   ▼
120 MHz (FDPLL0)
   ├─ GCLK_GENCTRL[0] SRC(7)=DPLL0 ÷1 → 120 MHz  (CPU / main clock)
   └─ GCLK_GENCTRL[1] SRC(7)=DPLL0 ÷2 →  60 MHz  (GCLK1) ─ PCHCTRL[9] → TC0 (DIV1)
                                                          → 60 MHz SYS_TIME tick (~16 ns)
```

  So the *entire* timebase (CPU and the NTP counter) is rooted in the internal,
  open-loop DFLL48M — an RC-class source specified to ~±0.4–1 % and temperature-
  dependent, which is precisely why the measured drift is ~1800–2000 ppm and rises as
  the board warms. The *absolute* error is irrelevant to the follower (the PI loop
  learns it), but clocking the DFLL/PLL reference — or `SYS_TIME` itself — from an
  **external crystal / TCXO / MEMS** would shrink the drift to tens of ppm (crystal)
  or ~1 ppm (TCXO/MEMS), improving the drift-wander and holdover (the per-sync jitter
  floor is unaffected). See the oscillator comparison in
  [NTP_TWO_NODE_CONVERGENCE.md §3.2](NTP_TWO_NODE_CONVERGENCE.md).
  → The full study of how to build a **disciplined hardware time base** from this
  (XOSC1 + DPLL1 + a dedicated TC, with EVSYS triggering ADC/DAC/GPIO/PWM at exact NTP
  instants) — now **realised and measured**, see [§5](#5-what-the-hardware-time-base-changed--a-live-beforeafter)
  — and verified step-by-step on the MCU is in the bridge firmware docs:
  [HW_TIMEBASE_OPTIONS.md](../firmware/t1s_100baset_bridge/docs/HW_TIMEBASE_OPTIONS.md)
  (option comparison),
  [HW_TIMEBASE_B_C_IMPLEMENTATION.md](../firmware/t1s_100baset_bridge/docs/HW_TIMEBASE_B_C_IMPLEMENTATION.md)
  (implementation + errata check), and
  [HW_TIMEBASE_BRINGUP_STEPS.md](../firmware/t1s_100baset_bridge/docs/HW_TIMEBASE_BRINGUP_STEPS.md)
  (tested bring-up).
- **Earlier timestamping (`#2`)** via a UDP RX signal handler was evaluated and
  **rejected**: on this cooperative-superloop + SPI-stack architecture it stamps at the
  same point `NTP_Task` already does, so it does not move the floor.

> **See also:** [NTP_TIMING.md](NTP_TIMING.md) (usage, the bridge one-way delay
> measurement) and [NTP_TWO_NODE_CONVERGENCE.md](NTP_TWO_NODE_CONVERGENCE.md) (the
> theory: convergence maths, master/slave, distributed sampling bandwidth).
