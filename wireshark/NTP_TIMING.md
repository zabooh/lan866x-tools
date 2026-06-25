# Software NTP time sync & bridge-delay measurement

This document explains the software NTP time synchronisation between the bridge
firmware and the PC (how it is implemented, how it is used, and what it achieves),
the bridge-delay test that builds on it, and an analysis of the measured results.

- Firmware: `firmware/t1s_100baset_bridge/firmware/src/ntp_sync.c` (+ eth0 hooks in `app.c`)
- PC tools: `ntpsync.c` → `lan866x-ntpsync`, and `wireshark/bridge_delay.py`

---

## 1. Why

Two needs:

1. **Timestamp firmware events on the PC's timebase.** The bridge has no real-time
   clock; its `SYS_TIME` counter only measures time *since boot*. To relate a
   firmware event to PC/wall-clock time we must align the two clocks.
2. **Measure the bridge's forwarding delay.** A frame on the T1S bus (`eth0`) only
   reaches a PC capture after crossing the bridge + 100BASE-T (`eth1`) + the NIC.
   With both clocks aligned we can timestamp the same frame at `eth0` (firmware)
   and at the NIC (PC) and take the difference.

The mechanism is a small **software NTP** sync — no extra hardware, just UDP.

---

## 2. How the NTP sync is implemented

### 2.1 The firmware time counter

The bridge keeps a free-running 64-bit **NTP counter** in nanoseconds:

```
ntp_now_ns() = raw_ns() + offset_ns
raw_ns()     = SYS_TIME counter, converted to ns (overflow-safe: sec*1e9 + frac)
```

`raw_ns()` starts at 0 at boot. `offset_ns` is a signed value the PC sets so the
counter reads PC time. On the SAME54 the `SYS_TIME` source runs at **60 MHz**, i.e.
a **~16 ns** resolution (reported by the `ntp` CLI). That tick is *not* the accuracy
limit — the network exchange jitter is (see §3).

### 2.2 The UDP service

A UDP server on **port 30491**, not pinned to an interface, so the PC reaches it
directly on the bridge (e.g. `192.168.0.181:30491`). All integers are big-endian.

| Op | Dir | Payload | Meaning |
|---|---|---|---|
| `0x01 REQUEST` | PC→FW | `t1` | time request (t1 = PC send time) |
| `0x02 REPLY` | FW→PC | `t1, t2, t3` | t2 = FW receive, t3 = FW send |
| `0x03 SET_OFFSET` | PC→FW | `adjust, delay` | `offset_ns += adjust`; store PC-measured delay |
| `0x04 SET_ACK` | FW→PC | `now` | FW NTP time after the set |
| `0x05 TAP_SET` | PC→FW | `enable, port` | enable/aim the eth0 timestamp tap (§4) |
| `0x06 TAP_REC` | FW→PC | `dir, t, ipid, proto, len, src, dst` | one eth0 timestamp record (§4) |

### 2.3 The exchange (t1/t2/t3/t4)

```
t1 ── PC sends REQUEST ───────────────►
                                 t2 ── FW receives
                                 t3 ── FW replies ──►
t4 ── PC receives REPLY ◄──────────────

offset = ((t2 − t1) + (t3 − t4)) / 2     # FW clock − PC clock
delay  = (t4 − t1) − (t3 − t2)           # round-trip transit (FW turnaround removed)
```

The PC runs several rounds and keeps the sample with the **smallest delay** (least
distortion), then sends `SET_OFFSET` with `adjust = −offset` so the firmware counter
reads PC time, plus the measured `delay` (so the firmware can display it too).

### 2.4 The PC reference clock

`lan866x-ntpsync` uses `GetSystemTimePreciseAsFileTime()` — a QPC-disciplined wall
clock, ~sub-µs resolution — expressed as **nanoseconds since the Unix epoch**.

### 2.5 Using it

```bat
lan866x-ntpsync                 :: continuous re-sync every 250 ms (Ctrl+C to stop)
lan866x-ntpsync --once          :: sync once and exit
lan866x-ntpsync --interval 500  :: continuous, 500 ms period
```
On the board:
```text
ntp            # status snapshot: source/resolution, offset, last delay/adjust,
               #   whether/when the last sync was, the estimated oscillator drift
               #   (ppm) and how far the counter has likely drifted since the last
               #   sync, plus the NTP time and local (GMT+2) wall clock
ntp watch      # one line per sync (throttled to ~1/s), GMT+2, until q / Ctrl-C
```
The one-shot `ntp` status projects the deviation since the last sync from the last
correction over the last sync interval (`drift ≈ −adjust / interval`), so you can see
at a glance whether the counter is still trustworthy or has drifted (it drifts ~ms/s
free-running — re-sync to pull it back). Example after syncing then idling ~8 s:
```text
  synced     : YES (8 sync msg)
  last sync  : 7.801 s ago
  est. drift : +1600 ppm  (last -506.476 us correction over 316.506 ms)
  est. dev.  : ~12.483 ms drifted off since last sync (projected)
  local time : 15:17:20.557 (GMT+2)
```
Offset and delay are printed **human-readable** (ns/us/ms/s) on both sides. The
board shows local time in **GMT+2** (display only; the stored counter is UTC epoch).

---

## 3. What it achieves, and its accuracy

After a sync the firmware counter tracks PC wall-clock time, so `ntp_now_ns()` can
stamp firmware events on the PC timebase.

- **Resolution** of the counter: ~16 ns (SAME54 `SYS_TIME` @ 60 MHz).
- **Sync accuracy**: bounded by the software round-trip. The residual after sync is
  on the order of **delay/2 plus jitter ≈ a few hundred microseconds** (the tool
  prints the round-trip `delay` and the residual; measured ~150–360 µs here).
- That floor is set by *software* timestamping at both ends and the 100BASE-T
  round-trip — **not** by the 16 ns counter. Sub-µs/absolute timing needs hardware
  timestamping / PTP (which lives in the newer `net_10base_t1s` project).
- **Drift:** the firmware oscillator is undisciplined and drifts ~ms/s, so a *single*
  sync only holds for a moment. To stamp events more than a fraction of a second after
  syncing, re-sync continuously (the default `lan866x-ntpsync` mode) — see §5.1.

> **What `delay` means.** It is the **round-trip transit time** PC↔bridge, with the
> firmware's own receive→reply turnaround `(t3 − t2)` removed: `delay = (t4 − t1) −
> (t3 − t2)`. Smaller delay ⇒ more accurate sync, which is why the lowest-delay
> round is used.

---

## 4. The bridge-delay test

**Goal:** measure how long a frame takes **through the bridge** in each direction —
PC→endpoint (NIC → `eth0`) and endpoint→PC (`eth0` → NIC) — i.e. the forwarding +
`eth1` + NIC transport latency the bridge adds.

**Mechanism** (`wireshark/bridge_delay.py`, orchestrating the existing tools):

1. **Continuous** `lan866x-ntpsync` in the background — keeps the firmware counter
   disciplined to the PC epoch clock for the *whole* run. A one-shot sync is **not**
   enough: the firmware oscillator drifts ~ms/s, so by the time we measure (seconds
   later) a one-shot-synced clock has already slid milliseconds off (see §5.1).
2. **Calibrate the capture-clock skew** `S` once: the PC sends self-timestamped UDP
   packets out the capture NIC and reads their `frame.time_epoch` back; `S = median(
   FILETIME_send − capture_epoch)` is the constant offset between the NIC capture
   clock (Npcap) and the `GetSystemTimePreciseAsFileTime()` clock the firmware is
   synced to. (In practice `S ≈ 0` here — see §5.)
3. `TAP_SET` enables the firmware **eth0 timestamp tap**: every IPv4 frame crossing
   `eth0` is stamped with `ntp_now_ns()` **inside the packet hook** (ingress in
   `pktEth0Handler`, egress in `mirror_eth0_tx_hook`) — so no UART/console is in the
   timing path — and the record `(dir, t, ipid, proto, len, src, dst)` is streamed to
   the PC from `NTP_Task` (decoupled from the stamp, so sending never delays it).
4. `tshark` captures the same frames at the **NIC** (epoch timestamps), and the tool
   collects the tap stream.
5. `lan866x-discovery` generates the traffic (PC→endpoint requests + endpoint→PC
   replies + SD).
6. Each captured frame is matched to its tap record by **IPv4 id** (the bridge
   forwards transparently, so the id/length are preserved), and `S` is removed:

```
PC → endpoint : delay_fwd = t(eth0 egress)  − t(NIC sent)     ;  D_fwd = delay_fwd − S
endpoint → PC : delay_rev = t(NIC received) − t(eth0 ingress) ;  D_rev = delay_rev + S
```

Run it (`--src-ip` = the capture NIC's own address; needed when several NICs share
the subnet, so the calibration packets really egress that NIC):
```bash
python bridge_delay.py --iface "Ethernet 8" --src-ip 192.168.0.200 \
       --bridge 192.168.0.181 --endpoint 192.168.0.54
# skew-free round-trip only, no calibration / no exact split:
python bridge_delay.py --iface "Ethernet 8" --no-calibrate
```

---

## 5. Analysis of the results

A first attempt with a **one-shot** sync gave nonsense raw values:

```
PC -> endpoint  (eth0 - NIC): median ≈ +20.7 ms
endpoint -> PC  (NIC - eth0): median ≈ −19.9 ms      ← negative one-way delay is impossible
```

The two directions carry a large offset of **opposite sign**, so something biases the
firmware clock relative to the capture clock by ~20 ms. The whole point of the
calibration step (§4.2) was to find *what*.

### 5.1 The bias is firmware clock drift, **not** a capture-clock skew

Calibrating `S` (NIC capture clock vs the `FILETIME` clock the firmware is synced to)
gave **`S ≈ 0` — only a few microseconds**, stable across runs (−0.6, 2.8, 5.7 µs).
So Npcap and `FILETIME` are effectively the *same* clock; the ~20 ms is **not** a
capture-clock offset.

The real cause: after a **one-shot** sync the firmware's free-running oscillator
**drifts** (here ~2–3 ms/s, i.e. thousands of ppm — normal for an undisciplined MCU
clock). By the time we measure, seconds after the sync, the firmware clock has slid
~20 ms ahead of PC time. That drift enters `delay_fwd` as `+drift` and `delay_rev` as
`−drift` — exactly the opposite-sign pattern observed. The built-in consistency check
flags it: *“symmetry would estimate S = 20288 µs vs calibrated 2.8 µs.”*

**Fix:** keep `lan866x-ntpsync` running **continuously** during the capture (§4.1) so
the firmware clock is re-disciplined every ~200 ms and never drifts far.

### 5.2 Result with continuous sync + calibration

A representative run (continuous sync, `S` calibrated):

```
calibrated capture-vs-sync clock skew S : -0.6 us  (60 frames)
-> exact one-way bridge delay (skew removed, NO symmetry assumption):
     PC -> endpoint :    699.9 us
     endpoint -> PC :     86.4 us
   cross-check round-trip (mf+mr, S-independent) : 786.3 us
```

Across runs: **PC→endpoint ≈ 0.5–0.7 ms**, **endpoint→PC ≈ 0–0.1 ms**, round-trip
**≈ 0.4–0.8 ms**. Because `S ≈ 0`, the directions are now directly comparable — no
symmetry assumption needed.

### 5.3 What this tells us

- The bridge’s forwarding latency is **sub-millisecond**, and it is **asymmetric**:
  the **PC→endpoint (eth0 egress) leg ≈ 0.5–0.7 ms dominates**, while the reverse
  (eth0 ingress) leg is **≈ 0–0.1 ms**, within the sync-residual floor. The egress
  leg is the slow path — the frame goes bridge → SPI → the LAN8651 MAC-PHY → onto the
  T1S bus, with MAC-PHY/SPI buffering — whereas the ingress stamp is taken right at
  the `eth0` RX hook.
- That ~0.7 ms is small versus the T1S RTT (~2 ms), which is why, for *round-trip* RCP
  timing measured at a single capture point, the bridge delay largely cancels as
  common-mode (see the SPAN/mirror discussion in the firmware README §6).
- **Calibration earned its keep by being ~0:** it ruled out the capture clock and
  redirected the diagnosis to firmware drift — the actual fix was continuous sync.

### 5.4 Limitations & how to improve

- **Software-NTP floor (~hundreds of µs):** the reverse leg sits at/below it, so its
  median is noisy (and can read slightly negative). Aggregate over more frames (longer
  capture / more `--discovery-runs`) to average it down; the round-trip sum is the
  most stable figure.
- **Continuous sync is required** for a meaningful per-direction split — a one-shot
  sync drifts (§5.1). The calibration of `S` removes any residual capture-clock offset
  exactly (no symmetry assumption); `--no-calibrate` falls back to the skew-free
  round-trip only.
- **Few matched frames per run** (discovery is bursty; SD is ~1/s). More traffic or a
  longer window tightens the medians.
- **For sub-µs / hardware-grade timing**, use PTP / hardware timestamping rather than
  software NTP — see `net_10base_t1s`.

> 🧭 **Could two T1S nodes sync to each other (convergingly) with this same software
> NTP — and how accurate/reliable could that get?** A theoretical treatment (German,
> with diagrams) is in **[NTP_TWO_NODE_CONVERGENCE.md](NTP_TWO_NODE_CONVERGENCE.md)**.

### 5.5 Why the UART does not corrupt the measurement

The eth0 timestamp is taken **in the packet hook** with `ntp_now_ns()`, before any
console activity; the records are sent to the PC later from `NTP_Task`. The `ntp
watch` console output is throttled to ~1/s and the NTP socket is serviced every
~1 ms, so console/UART I/O is never on the timing path of either the sync exchange
or the eth0 tap.
