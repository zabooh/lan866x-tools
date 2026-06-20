# SPI demo — identify the Thumbstick, non-blocking (`lan866x-spiid`)

> **TL;DR** `lan866x-spiid` reads the **Thumbstick Click's MCP3204 ADC** over SPI
> and reports both joystick axes — using the **async RCP API** so the main loop
> never blocks on the round-trip. It is the minimal, copy-me example for *"do a
> full-duplex SPI transfer over SOME/IP without stalling the loop."*

---

## Table of contents

1. [What it does — and the "ID" caveat](#1-what-it-does--and-the-id-caveat)
2. [The hardware](#2-the-hardware)
3. [Run it](#3-run-it)
4. [How an MCP3204 read works on this stack](#4-how-an-mcp3204-read-works-on-this-stack)
5. [Why non-blocking (code walk-through)](#5-why-non-blocking-code-walk-through)
6. [Adapting it to another SPI device](#6-adapting-it-to-another-spi-device)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. What it does — and the "ID" caveat

The Thumbstick Click reads its 2-axis joystick through a **Microchip MCP3204**, a
12-bit, 4-channel SPI ADC. The natural ask is *"read the thumbstick's ID over SPI"*
— but there is an honest catch:

> **The MCP3204 has no silicon ID register.** Unlike the VCNL4200 (which answers
> with `0x1058` at register `0x0E`, see [docs/I2CDEMO.md](I2CDEMO.md)), a plain ADC
> has nothing to identify itself with. So *"identifying"* it over SPI means proving
> it **responds with a valid 12-bit conversion**: a centred joystick reads ~2048 on
> both axes at rest. That successful round-trip **is** the device's fingerprint.

So `lan866x-spiid` reads channel 1 (X axis) and channel 0 (Y axis) and confirms the
MCP3204 is present and talking. Everything else — discovery, OpenSpi, the async
transfer — is exactly the SPI pattern you'd reuse to read any SPI device.

---

## 2. The hardware

| Item | Value |
|---|---|
| Device | MCP3204 (Thumbstick Click), 12-bit 4-ch SPI ADC |
| Slot | Click slot 4 (btm-left), channel SER3 |
| Pins | **MISO = PA12, SCK = PA13, CS = PA14, MOSI = PA15** (board DIP default) |
| SPI mode | 1 |
| Clock | ~1.92 MHz |
| Axes | **ch0 = Y, ch1 = X** (button is separate) |

See [TOOLS.md §2.4](../TOOLS.md#24-click-slots--what-plugs-where) for the slot map and
DIP routing. The Thumbstick **must** be in slot 4 (SPI); if it isn't, or its SPI DIP
switches are open, `OpenSpi` or the read will fail.

---

## 3. Run it

```bat
release\lan866x-spiid.exe --ip 192.168.0.54
```

Expected output (joystick at rest):

```
Reading Thumbstick MCP3204 over SPI (MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15, non-blocking)...
  channel 1 (X axis): raw=2041 (0x7F9)
  channel 0 (Y axis): raw=2057 (0x809)
  (loop spun 6 times while the reads were in flight)
  -> MCP3204 Thumbstick responding. (No silicon ID register exists;
     a valid 12-bit conversion - ~2048 per axis at rest - is its fingerprint.)
```

Push the stick while running it again and the raw values swing toward 0 / 4095.
Options: `--miso`, `--sck`, `--cs`, `--mosi`, `--mode`, `--speed`, `--ip`/`--ep`.

---

## 4. How an MCP3204 read works on this stack

An MCP3204 single-ended conversion is a **3-byte full-duplex** SPI transaction: you
clock out a 3-byte command and simultaneously clock in the 12-bit result. The RCP
service exposes a single transfer as **`WriteAndReadSpi` (`0x1508`)**:

| Param | Here |
|---|---|
| `HandleSpi` | from `OpenSpi` |
| `ReadDataLength` | `3` |
| `WriteId` | incrementing (idempotent resend) |
| `WriteData` | `0x06, ch<<6, 0xFF` |

The command bytes:

| Byte | Value | Meaning |
|---|---|---|
| 0 | `0x06` | leading zeros + **start** bit + **SGL/DIFF**=1 (single-ended) |
| 1 | `ch << 6` | channel select in the high bits (`0x00`=ch0, `0x40`=ch1, …) |
| 2 | `0xFF` | don't-care while the result clocks back on MISO |

The reply's 12-bit value sits in bytes `[1..2]`: `((d[1] << 8) | d[2]) & 0x0FFF`.

> **Single vs. compound.** This example uses one transfer per channel (`0x1508`),
> which works on every firmware build. `lan866x-clickdemo` instead uses the
> **compound** transfer `WriteAndReadSpi2` (`0x1509`, config ≥ V1.3.2) to read both
> axes in **one** round-trip — handy when you want to halve latency and dodge the
> host's back-to-back-reply drop (gotcha #4). Async helpers exist for both:
> `rcp_enc_spi1`/`rcp_dec_spi1` (single) and `rcp_enc_spi2`/`rcp_dec_spi2` (compound).

---

## 5. Why non-blocking (code walk-through)

A blocking transfer (`rcp_write_and_read_spi`) parks the program until the reply
arrives. This example uses the async API so the loop stays live. Per channel:

**(1) Build the command, set a deadline, fire.** `rcp_enc_spi1()` builds the
`WriteAndReadSpi` params; `rcp_async_request()` sends and returns at once:

```c
rcp_set_async_timeout_ms(300);
uint8_t tx[3] = { 0x06, (uint8_t)(ch << 6), 0xFF };
uint8_t params[64];
uint16_t n = rcp_enc_spi1(params, sizeof(params), handle, wid++, tx, 3, /*read*/3);
rcp_async_request(0x1508, params, n, on_spi, NULL);   /* returns at once */
```

**(2) Spin the loop, never block on the wire** (the count just makes it visible):

```c
while (!s_done) { rcp_async_poll(); Sleep(2); spins++; }   /* loop stays free */
```

**(3) Decode in a short callback** (inline, single-thread — no lock):

```c
static void on_spi(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen) {
    s_done = 1;
    if (rc == RT_OK && rx) {
        uint8_t d[8]; uint16_t l = sizeof(d);
        if (rcp_dec_spi1(rx, rxLen, d, &l) && l >= 3) {
            s_val = ((d[1] << 8) | d[2]) & 0x0FFF;   /* 12-bit result */
            s_ok = 1;
        }
    }
}
```

It reads two channels by calling this fire/wait twice (X then Y), each retried a few
times to ride out an occasional dropped reply (gotcha #4); `WriteId` makes a re-sent
transfer safe.

---

## 6. Adapting it to another SPI device

- **Different command/length:** replace the 3 command bytes and `ReadDataLength`
  with your device's protocol; the async plumbing is unchanged.
- **A device *with* an ID:** read its ID/WHO_AM_I register and compare — the SPI
  flavour of [docs/I2CDEMO.md](I2CDEMO.md). (Many sensors: e.g. an LIS3DH WHO_AM_I.)
- **Lower latency / paired reads:** switch to the compound transfer
  `rcp_enc_spi2`/`0x1509` (config ≥ V1.3.2) to do two transfers in one round-trip.
- **Mode / clock / pins:** `--mode`, `--speed`, `--miso/--sck/--cs/--mosi`.

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `OpenSpi failed` | SPI not configured / pins wrong / Thumbstick not in slot 4. Check the SPI DIP switches (SW11) are ON. |
| `read failed (X=… Y=…)` | device absent or CS/MISO miswired; verify the Click is seated in slot 4. |
| values stuck at 0 or 4095 | wrong mode/clock, or MISO not reaching the MAC-PHY; try `--mode 0`. |
| axes feel swapped/inverted | ch0/ch1 and polarity depend on the Click orientation — remap in your code. |
| occasional failure, ok on retry | host dropped the reply (gotcha #4) — normal; retries cover it. |

See also: [docs/LEDDEMO.md](LEDDEMO.md) (GPIO, blocking vs. async),
[docs/I2CDEMO.md](I2CDEMO.md) (the I²C sibling),
[docs/RCP_API.md](RCP_API.md) (full `rcp_*` reference), [TOOLS.md](../TOOLS.md).
