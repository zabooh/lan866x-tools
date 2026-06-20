# I²C demo — read a device ID, non-blocking (`lan866x-i2cid`)

> **TL;DR** `lan866x-i2cid` reads the **VCNL4200** proximity sensor's ID register
> over I²C and checks it equals **0x1058** — using the **async RCP API** so the
> main loop never blocks on the round-trip. It is the minimal, copy-me example for
> *"talk to an I²C device over SOME/IP without stalling the loop."*

> 📄 **Source:** [../i2cid.c](../i2cid.c) · live monitor [../proxmon.c](../proxmon.c) ·
> bus scan [../i2cscan.c](../i2cscan.c) — core: [../src/rcp.c](../src/rcp.c) /
> [../src/rcp.h](../src/rcp.h). Demo index: [DEMOS.md](DEMOS.md).

---

## Table of contents

1. [What it does](#1-what-it-does)
2. [The hardware](#2-the-hardware)
3. [Run it](#3-run-it)
4. [How an I²C register read works on this stack](#4-how-an-ic-register-read-works-on-this-stack)
5. [Why non-blocking (code walk-through)](#5-why-non-blocking-code-walk-through)
6. [Adapting it to another I²C device](#6-adapting-it-to-another-ic-device)
7. [Troubleshooting](#7-troubleshooting)
8. [Going further: live monitor & sensor→actuator](#8-going-further-live-monitor--sensoractuator)

---

## 1. What it does

The Proximity 3 Click on the demo board carries a **Vishay VCNL4200** ambient-light
+ proximity sensor. Like most real I²C parts it has a **device-ID register** you can
read to confirm "yes, the expected chip is on the bus and talking." `lan866x-i2cid`:

1. discovers + selects the endpoint (SOME/IP-SD),
2. opens the I²C bus (`OpenI2C`),
3. fires a **non-blocking** "write register address, read 2 bytes" transaction
   (`WriteAndReadI2C`) at the VCNL4200,
4. decodes the 16-bit, little-endian ID and checks it against **0x1058**.

This is the I²C sibling of [docs/LEDDEMO.md](LEDDEMO.md) (GPIO) — same async idea,
applied to a register read instead of a pin write.

---

## 2. The hardware

| Item | Value |
|---|---|
| Device | VCNL4200 (Proximity 3 Click), **I²C address 0x51** |
| Slot | Click slot 3 (btm-right), channel SER2 |
| Pins | **SDA = PA08, SCL = PA09** (board DIP default) |
| Bus speed | 400 kHz (`--speed 1`) |
| ID register | **0x0E**, value **0x1058**, **LSB first** |

See [TOOLS.md §2.4](../TOOLS.md#24-click-slots--what-plugs-where) for the slot map and
DIP routing. If the Proximity Click is not in slot 3 (or its I²C DIP switches are
open), `OpenI2C` or the read will fail.

---

## 3. Run it

```bat
release\lan866x-i2cid.exe --ip 192.168.0.54
```

Expected output:

```
Reading ID of 0x51 register 0x0E (SDA=PA08 SCL=PA09, non-blocking)...
  ID = 0x1058   (loop spun 3 times while the read was in flight)
  -> VCNL4200 (Proximity 3 Click) detected.
```

Options: `--addr`, `--reg` (hex ok), `--sda`, `--scl`, `--speed`, `--ip`/`--ep`.
With `--addr`/`--reg` you can read the ID register of a *different* I²C device.

---

## 4. How an I²C register read works on this stack

Reading register `R` of an I²C device is a classic **write-then-read**: write the
1-byte register address, then (repeated START) read N bytes back. The RCP service
exposes that as **`WriteAndReadI2C` (`0x1208`)** — one method, one round-trip:

| Param | Here |
|---|---|
| `HandleI2C` | from `OpenI2C` |
| `DeviceAddress` | `0x51` |
| `ReadDataLength` | `2` (ID is 16-bit) |
| `WriteId` | incrementing (makes a resent request idempotent) |
| `WriteData` | `[0x0E]` (the register address) |

The reply carries `ReadId` + the data blob. The VCNL4200 returns the **LSB first**,
so the ID is `data[0] | (data[1] << 8)`.

> **Note — read vs. scan.** A *bus scan* (`lan866x-i2cscan`) uses the pure-read
> `ReadI2C` (`0x1220`) because `WriteAndReadI2C` with a 0-byte write does **not**
> report an address NACK on this firmware (phantom devices — gotcha #2). For a
> *register read* of a known device, `WriteAndReadI2C` is exactly right.

---

## 5. Why non-blocking (code walk-through)

A blocking read (`rcp_write_and_read_i2c`) parks the whole program until the reply
arrives. On a real device the loop must stay live — so this example uses the async
API. Three pieces:

**(1) Set a deadline and fire.** `rcp_enc_i2c_read()` builds the `WriteAndReadI2C`
params; `rcp_async_request()` sends them and returns immediately:

```c
rcp_set_async_timeout_ms(300);                       /* per-attempt deadline */
uint8_t params[64], reg = 0x0E;
uint16_t n = rcp_enc_i2c_read(params, sizeof(params),
                              handle, 0x51, wid++, &reg, 1, /*read*/2);
rcp_async_request(0x1208, params, n, on_id, NULL);   /* returns at once */
```

**(2) Spin the loop, never block on the wire.** `rcp_async_poll()` pumps RX and
fires the deadline; the callback runs from *inside* it. We count the spins just to
make the non-blocking nature visible:

```c
while (!s_done) { rcp_async_poll(); Sleep(2); spins++; }   /* loop stays free */
```

**(3) Decode in a short callback** (inline on the single thread — no lock, no
`rcp_*` calls):

```c
static void on_id(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen) {
    s_rc = rc; s_done = 1;
    if (rc == RT_OK && rx) {
        uint8_t d[8]; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) {
            s_id = d[0] | (d[1] << 8);   /* LSB first */
            s_ok = 1;
        }
    }
}
```

The tool retries the whole fire/wait a few times: on this host an occasional RCP
reply is dropped under load (gotcha #4), and `WriteId` makes a re-sent request safe.

---

## 6. Adapting it to another I²C device

- **Different ID register / width:** pass `--reg`, and change the `ReadDataLength`
  (here `2`) and the byte assembly (here LSB-first) for your device's endianness.
- **A device with no ID register:** there is nothing to read — do a 1-byte
  presence read instead (that's what `lan866x-i2cscan` does), or read a known
  status register.
- **Pins / speed:** `--sda`, `--scl`, `--speed` match whatever your board routes.

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `OpenI2C failed` | I²C not configured on that endpoint, or pins wrong. Check `--sda`/`--scl` and that Click slot 3's I²C DIP switches (SW10) are ON. |
| `read failed (rc=…)` | device absent / wrong address / Proximity not in slot 3. Verify with `lan866x-i2cscan` (should show `0x51`). |
| `ID = 0x….` but "unexpected" | you read the right bus but a different device, or the wrong register. |
| occasional failure, succeeds on retry | host dropped the reply (gotcha #4) — normal; the built-in retries cover it. |

See also: [docs/LEDDEMO.md](LEDDEMO.md) (GPIO, blocking vs. async),
[docs/SPIDEMO.md](SPIDEMO.md) (the SPI sibling),
[docs/RCP_API.md](RCP_API.md) (full `rcp_*` reference), [TOOLS.md](../TOOLS.md).

---

## 8. Going further: live monitor & sensor→actuator

`i2cid` reads the ID *once*. Two tools build on the same async pattern for
continuous use:

**`lan866x-proxmon`** — live proximity monitor. It enables the VCNL4200's PS engine
(one-time blocking writes to `PS_CONF1/3`, `ALS_CONF`) and then reads `PS_DATA`
(`0x08`) continuously, drawing a live bar. One read is in flight at a time; the
render loop runs at a steady cadence decoupled from reply timing:

```bat
release\lan866x-proxmon.exe --ip 192.168.0.54
  [##########------------------------------] raw=  102
```

**`lan866x-proxled`** — the **sensor→actuator** mini-app: proximity drives the
on-board LEDs as a 0–3 level meter (closer hand = more LEDs), with **no video**.
It reads the sensor over I²C (async) and writes the LEDs over GPIO (blocking, only
on a level change) — the full input→decide→output loop a real device runs. Full
write-up: **[docs/COMBODEMO.md](COMBODEMO.md)**.

```bat
release\lan866x-proxled.exe --ip 192.168.0.54
  raw=  310  level=2/3  [##-]
```
