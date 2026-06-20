# PWM demo — breathing LED (`lan866x-ledpwm`)

> **TL;DR** `lan866x-ledpwm` smoothly ramps an on-board LED's brightness up and
> down (a "breathing" effect) by varying the **PWM duty cycle** in a non-blocking
> loop. It's the analog-style sibling of `lan866x-ledtoggle` (on/off).
>
> ⚠️ **PWM support is *not confirmed* on the Lighting firmware (.54).** `OpenPwm`
> may return `E_UNKNOWN_METHOD` / `RT_NOT_REACHABLE` on a build that doesn't
> implement it. **Verify on your board** (see §3). On a Control build PWM is
> available.

> 📄 **Source:** [../ledpwm.c](../ledpwm.c) · raw tool [../pwm.c](../pwm.c) — core:
> [../src/rcp.c](../src/rcp.c) / [../src/rcp.h](../src/rcp.h). Demo index: [DEMOS.md](DEMOS.md).

---

## Table of contents

1. [What it does](#1-what-it-does)
2. [The firmware caveat — read this first](#2-the-firmware-caveat--read-this-first)
3. [Run it (and confirm PWM works)](#3-run-it-and-confirm-pwm-works)
4. [How PWM works on this stack](#4-how-pwm-works-on-this-stack)
5. [Why non-blocking (code walk-through)](#5-why-non-blocking-code-walk-through)
6. [Troubleshooting](#6-troubleshooting)

---

## 1. What it does

A hardware PWM channel toggles a pin at a fixed carrier frequency; the **duty cycle**
(fraction of each period the pin is high) sets the average level. Sweep the duty from
0 % → 100 % → 0 % and an LED appears to *breathe*. `lan866x-ledpwm`:

1. opens a PWM channel on the LED pin (`OpenPwm`, default PA02 = LD1),
2. in a non-blocking loop, pushes a new duty cycle each step (`WritePwm`),
3. ramps it as a triangle wave so the LED fades smoothly in and out.

This adds the **PWM** method family (`OpenPwm 0x1800`, `WritePwm 0x1804`,
`ClosePwm 0x1802`) to the example set, which the GPIO/I²C/SPI demos don't touch.

---

## 2. The firmware caveat — read this first

The board on the bench runs the **Lighting** firmware (`LAN8661-ws2812`). On that
build, the project verified live that **GPIO, I²C and SPI work** but **ADC does not**
(`OpenAdc` → `E_UNKNOWN_METHOD`). **PWM was never separately confirmed** — see
[EVB/.../README.md §9.7](../EVB/EVB-LAN8680-LAN866x/README.md). So:

- On a firmware that implements PWM, `lan866x-ledpwm` breathes the LED.
- On one that doesn't, `OpenPwm` fails and the tool says so plainly (it does **not**
  pretend to work).

The tool and this doc are deliberately honest about that. The code is correct and
ready; whether the *device* answers is the open question, and only the board can tell.

---

## 3. Run it (and confirm PWM works)

First, a quick existence check with the low-level tool:

```bat
release\lan866x-pwm.exe --ip 192.168.0.54 --pin 2 --freq 1000 --duty 50 --hold 2
```

- If LD1 lights at ~half brightness for 2 s → **PWM works**, run the breather:
  ```bat
  release\lan866x-ledpwm.exe --ip 192.168.0.54
  ```
- If it prints `OpenPwm failed (PWM not configured on this pin?)` → PWM isn't in this
  firmware build; flash a Control build to use it (see TOOLS.md flash tools).

Options: `--pin`, `--freq` (carrier Hz), `--period` (ms per full breath), `--steps`
(brightness steps per half-breath), `--ip`/`--ep`.

---

## 4. How PWM works on this stack

| Method | ID | Params |
|---|---|---|
| `OpenPwm` | `0x1800` | `PinId`, `IntervalTime` (period, ns), `DutyCycle` (initial) → `HandlePwm` |
| `WritePwm` | `0x1804` | `HandlePwm`, `WriteId`, `WriteData` (duty) |
| `ClosePwm` | `0x1802` | `HandlePwm` |

**Duty encoding (q31):** `0 = 0 %`, `2³¹ = 100 %`. The helper converts a percentage:

```c
uint32_t duty_q31(double pct) { return (uint32_t)(pct / 100.0 * 2147483648.0); }
```

`IntervalTime` is the carrier period in nanoseconds (`1e9 / freq`); the firmware
rounds period and duty down to a multiple of 20 ns. `WriteId` is a consecutive
counter (starts at 0 after `OpenPwm`) that makes a resent write idempotent — same
idea as the I²C/SPI `WriteId`.

> A wrapper for `WritePwm` was added for this demo: blocking `rcp_write_pwm()` and
> the async encoder `rcp_enc_pwm_write()` (paired with `rcp_async_request`).

---

## 5. Why non-blocking (code walk-through)

The breathing ramp must keep moving smoothly; it shouldn't stall on each
acknowledgement. So duty updates go out with the async API:

```c
rcp_set_async_timeout_ms(200);
DWORD nextStep = GetTickCount();
while (g_run) {
    if ((long)(GetTickCount() - nextStep) >= 0) {
        double pct = 100.0 * step / steps;
        uint8_t p[32];
        uint16_t n = rcp_enc_pwm_write(p, sizeof(p), handle, wid++, duty_q31(pct));
        rcp_async_request(0x1804, p, n, on_write, NULL);   /* returns at once */
        step += dir;
        if (step >= steps)      { step = steps; dir = -1; }  /* fade back down */
        else if (step <= 0)     { step = 0;     dir =  1; }  /* fade back up   */
        nextStep += stepMs;
    }
    rcp_async_poll();   /* delivers the WritePwm acks via on_write() */
    Sleep(2);
}
```

The acknowledgements are handled in a one-line callback (`on_write` just counts
them); the loop's timing is driven entirely by the wall clock, never by the network
round-trip — exactly the [ledtoggle](LEDDEMO.md#62-non-blocking-variant--lan866x-ledtoggle)
pattern, applied to duty cycle instead of on/off.

On exit it writes 0 % and `ClosePwm`s the channel so the LED is left off.

---

## 6. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `OpenPwm failed` | PWM not implemented on this firmware (most likely on the Lighting build), or not available on that pin. Confirm with `lan866x-pwm`; flash a Control build for PWM. |
| LED on but not dimming | the firmware may clamp/round duty, or the LED pin can't PWM; try another `--pin` or a lower `--freq`. |
| breathing looks steppy | increase `--steps` or `--period`. |
| flickers / irregular | host dropped a `WritePwm` reply (gotcha #4) — harmless; `WriteId` makes the resend safe and the next step corrects it. |

See also: [docs/LEDDEMO.md](LEDDEMO.md) (GPIO on/off, blocking vs. async),
[docs/COMBODEMO.md](COMBODEMO.md) (sensor→actuator), [TOOLS.md §4.8](../TOOLS.md#48-lan866x-pwm)
(`lan866x-pwm`), [docs/RCP_API.md](RCP_API.md).
