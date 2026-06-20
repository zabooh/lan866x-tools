# Sensor → actuator demo — `lan866x-proxled`

> **TL;DR** `lan866x-proxled` is the **"real embedded app"** example: it reads the
> VCNL4200 proximity sensor over I²C and lights the three on-board LEDs as a level
> meter — the closer your hand, the more LEDs come on. It ties an **input read** and
> an **output write** into one non-blocking superloop, with **no video** (unlike
> `lan866x-clickdemo`). This is the loop shape a real device actually runs.

---

## Table of contents

1. [What it does](#1-what-it-does)
2. [The hardware](#2-the-hardware)
3. [Run it](#3-run-it)
4. [The loop: input → decide → output](#4-the-loop-input--decide--output)
5. [Why this is the "real app" shape](#5-why-this-is-the-real-app-shape)
6. [Make it your own](#6-make-it-your-own)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. What it does

Every previous example exercised **one** direction:

| Example | Direction |
|---|---|
| `ledblink` / `ledtoggle` | output (GPIO write) |
| `i2cid` / `proxmon` | input (I²C read) |
| `spiid` / `thumbmon` | input (SPI read) |

`lan866x-proxled` closes the loop: **read a sensor, decide, drive an actuator.**
It maps the proximity reading to a level of 0–3 and lights LD1/LD2/LD3 accordingly.
That is exactly what `lan866x-clickdemo` does too — but clickdemo adds an RTP video
pipeline and two sensors, which buries the core idea. This strips it to the bone.

---

## 2. The hardware

| Role | Device | Wire |
|---|---|---|
| **Input** | VCNL4200 (Proximity 3 Click, slot 3) | I²C @ 0x51, SDA=PA08 SCL=PA09 |
| **Output** | on-board LEDs LD1/LD2/LD3 | GPIO PA02 / PA06 / PA10 (SW13-1/2/3) |

Both are verified-working RCP functions on the Lighting firmware. See
[TOOLS.md §2.4](../TOOLS.md#24-click-slots--what-plugs-where) (slot map) and
[TOOLS.md §2.7](../TOOLS.md#27-on-board-leds--buttons) (the LED pins).

---

## 3. Run it

```bat
release\lan866x-proxled.exe --ip 192.168.0.54
```

Move your hand toward the Proximity Click:

```
Proximity -> LED level meter. Move your hand toward the sensor.
  0 LEDs (far) .. 3 LEDs (near). Ctrl+C to stop.

  raw=  310  level=2/3  [##-]
```

`--max <n>` sets the raw value that lights all three LEDs (default 400 — tune it to
your ambient IR / hand distance). Ctrl+C turns the LEDs off and closes I²C.

---

## 4. The loop: input → decide → output

After a one-time blocking setup (OpenI²C + VCNL4200 PS-engine enable + OpenGpio on
the 3 LEDs), the superloop is three clear stages:

```c
while (g_run) {
    /* INPUT — async proximity read, only when none is in flight */
    if (!s_pending) {
        uint8_t p[64], reg = 0x08;                 /* PS_DATA */
        uint16_t n = rcp_enc_i2c_read(p, sizeof(p), s_i2c, 0x51, s_wid++, &reg, 1, 2);
        if (n && rcp_async_request(0x1208, p, n, on_prox, NULL) == RT_OK) s_pending = 1;
    }
    rcp_async_poll();                              /* on_prox() runs from here */

    /* DECIDE — map raw proximity to a 0..3 level */
    int lvl = s_prox * 4 / maxRaw;  if (lvl > 3) lvl = 3;

    /* OUTPUT — write the LEDs only when the level actually changes */
    if (lvl != level) { for (i=0;i<3;i++) led_set(i, i < lvl); level = lvl; }

    Sleep(5);
}
```

Two design choices worth calling out:

- **The input is async**, so the loop never parks waiting for the sensor — the same
  non-blocking pattern as [proxmon](I2CDEMO.md#8-going-further-live-monitor--sensoractuator).
- **The output is written only on change.** LED writes are infrequent (a level only
  flips when your hand crosses a threshold), so a simple **blocking** `SetGpio` is
  fine there — no need to complicate the rare path. Pace matters (gotcha #4); doing
  the writes rarely keeps the control traffic light.

---

## 5. Why this is the "real app" shape

A field device is rarely "read once and print." It runs forever, sampling inputs and
driving outputs while staying responsive. `proxled` is the minimum honest version of
that:

- **Forever loop**, clean Ctrl+C shutdown (actuator returned to a safe state).
- **Non-blocking input** so one slow/lost round-trip can't freeze the device.
- **Event-driven output** (act on change, not every tick) to keep the bus quiet.
- **Same C, portable to the MCU** — only `GetTickCount()`/`Sleep()` are host-isms;
  on the target they become the lwIP/RTOS clock and yield (see [PORTING.md](../PORTING.md)).

Scale this up and you get `clickdemo` (two sensors + a video actuator); scale it
down and you get the single-direction examples. This sits in the sweet spot for
*"show me a complete control loop."*

---

## 6. Make it your own

- **Thumbstick instead of proximity:** read an MCP3204 axis (see
  [SPIDEMO.md](SPIDEMO.md)) and map it to which LED lights — a "VU meter" steered by
  the joystick. One function swap.
- **Brightness instead of count:** if PWM is available on your firmware, drive a
  single LED's duty cycle from the proximity value (combine with
  [PWMDEMO.md](PWMDEMO.md)) for a smooth dimmer.
- **Hysteresis:** add a small dead-band around each threshold so the level doesn't
  flicker when your hand hovers on a boundary.

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `VCNL4200 not found` | Proximity Click not in slot 3, or I²C DIP (SW10) open; check with `lan866x-i2cscan` (expects `0x51`). |
| `OpenGpio failed on PA…` | an LED pin is in use; confirm the pins with `lan866x-ledscan`. |
| LEDs never light | `--max` too high for your setup — lower it; or the LED's SW13 switch is off. |
| level flickers on a boundary | expected without hysteresis — see §6. |
| occasional stutter | host dropped a reply (gotcha #4) — harmless here; the next read recovers. |

See also: [docs/I2CDEMO.md](I2CDEMO.md), [docs/SPIDEMO.md](SPIDEMO.md),
[docs/LEDDEMO.md](LEDDEMO.md), [docs/CLICKDEMO.md](CLICKDEMO.md) (the full version),
[TOOLS.md](../TOOLS.md).
