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
ntp            # status snapshot (source/resolution, offset, last delay/adjust, NTP time)
ntp watch      # one line per sync (throttled to ~1/s), GMT+2, until q / Ctrl-C
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

1. `lan866x-ntpsync --once` — align the firmware counter to the PC epoch clock.
2. `TAP_SET` enables the firmware **eth0 timestamp tap**: every IPv4 frame crossing
   `eth0` is stamped with `ntp_now_ns()` **inside the packet hook** (ingress in
   `pktEth0Handler`, egress in `mirror_eth0_tx_hook`) — so no UART/console is in the
   timing path — and the record `(dir, t, ipid, proto, len, src, dst)` is streamed to
   the PC from `NTP_Task` (decoupled from the stamp, so sending never delays it).
3. `tshark` captures the same frames at the **NIC** (epoch timestamps), and the tool
   collects the tap stream.
4. `lan866x-discovery` generates the traffic (PC→endpoint requests + endpoint→PC
   replies + SD).
5. Each captured frame is matched to its tap record by **IPv4 id** (the bridge
   forwards transparently, so the id/length are preserved):

```
PC → endpoint : delay_fwd = t(eth0 egress)  − t(NIC sent)
endpoint → PC : delay_rev = t(NIC received) − t(eth0 ingress)
```

Run it:
```bash
python bridge_delay.py --iface "Ethernet 8" --bridge 192.168.0.181 --endpoint 192.168.0.54
```

---

## 5. Analysis of the results

A representative run:

```
raw (firmware NTP clock vs NIC capture clock):
  PC -> endpoint  (eth0 - NIC): n=  2  min= 13345.9  median= 13362.3  max= 13378.6  us
  endpoint -> PC  (NIC - eth0): n= 11  min=-17703.3  median=-12629.4  max= -3331.3  us

  bridge round-trip transport (NIC<->eth0, skew-free) : 733 us
  -> per direction (assuming symmetry)                : 366 us each
  estimated capture-vs-sync clock skew S              : ~13 ms
```

### 5.1 The raw values carry a constant clock skew

The raw per-direction numbers are implausible: +13 ms one way, **−13 ms** the other.
A negative one-way delay is impossible, so there is a **systematic offset**. Its
cause: the NIC capture timestamps come from **Npcap's clock**, which differs from the
`GetSystemTimePreciseAsFileTime()` clock that `ntpsync` disciplines the firmware to,
by a **constant skew `S` ≈ 11–13 ms**. The firmware is aligned to one clock; the
capture is stamped by another.

### 5.2 Why the sum cancels it

With `S` = (firmware/tap clock) − (capture clock), constant:

```
delay_fwd(measured) = D_fwd + S        # eth0_out is on the FW clock, NIC_sent on the capture clock
delay_rev(measured) = D_rev − S        # NIC_recv on the capture clock, eth0_in on the FW clock
```

where `D_fwd`, `D_rev` are the true one-way delays. The skew enters with **opposite
sign**, so:

```
delay_fwd + delay_rev = D_fwd + D_rev          ← skew-free: the true bridge round-trip
delay_fwd − delay_rev = (D_fwd − D_rev) + 2·S  ← gives S if we assume D_fwd ≈ D_rev
```

So the tool reports the **skew-free round-trip transport** `D_fwd + D_rev ≈ 0.7–1.2 ms`
(varies run to run with the small sample count and jitter), and, **assuming
symmetry**, `≈ 0.35–0.6 ms per direction`. The extracted skew `S ≈ 11–13 ms` matches
across both directions and across runs, confirming it is a genuine constant
clock offset, not the bridge delay.

### 5.3 What this tells us

- The **bridge adds roughly 0.4–0.6 ms each way** (MAC-bridge forwarding + the
  100BASE-T `eth1` hop + the NIC/Npcap path). That is small relative to the T1S RTT
  (~2 ms) and explains why, for *round-trip* RCP timing measured at a single capture
  point, the bridge delay largely cancels (it is common-mode — see the SPAN/mirror
  discussion in the firmware README §6).
- The **dominant uncertainty is the Npcap-vs-sync clock skew (~11–13 ms)**, not the
  bridge. Because it is constant and opposite-signed, the round-trip sum removes it;
  an *exact* per-direction split, however, requires the symmetry assumption.

### 5.4 Limitations & how to improve

- **Software-NTP floor (~hundreds of µs):** aggregate over more frames (longer
  capture / more `--discovery-runs`) to average it down. The skew-free round-trip is
  more stable than either raw direction.
- **Per-direction split needs symmetry.** To split exactly, the constant Npcap-vs-
  `FILETIME` skew `S` would have to be calibrated once (e.g. a loopback frame
  timestamped at NIC-TX and NIC-RX), then subtracted per direction.
- **Few matched frames per run** (discovery is bursty; SD is ~1/s). More traffic or a
  longer window tightens the medians.
- **For sub-µs / hardware-grade timing**, use PTP / hardware timestamping rather than
  software NTP — see `net_10base_t1s`.

### 5.5 Why the UART does not corrupt the measurement

The eth0 timestamp is taken **in the packet hook** with `ntp_now_ns()`, before any
console activity; the records are sent to the PC later from `NTP_Task`. The `ntp
watch` console output is throttled to ~1/s and the NTP socket is serviced every
~1 ms, so console/UART I/O is never on the timing path of either the sync exchange
or the eth0 tap.
