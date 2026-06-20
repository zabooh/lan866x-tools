# LED demo — the "hello world" of remote embedded GPIO

> **TL;DR** `lan866x-ledblink` blinks the three on-board LEDs of the
> EVB-LAN8680-LAN866x as a **running light** ("Lauflicht") over **SOME/IP / RCP**
> — half-a-second per LED, looping forever. It is the canonical *blink-an-LED*
> first program, but done **remotely** over 10BASE-T1S instead of on bare metal.
> The LED pins (PA02 / PA06 / PA10) were discovered with `lan866x-ledscan` and
> stored in [`led_map.json`](../release/led_map.json).

---

## Table of contents

1. [Why an LED demo](#1-why-an-led-demo)
2. [The hardware path](#2-the-hardware-path)
3. [Which pins, and how we found them](#3-which-pins-and-how-we-found-them)
4. [`lan866x-ledscan` — find the LED pins](#4-lan866x-ledscan--find-the-led-pins)
5. [`lan866x-ledblink` — the running light](#5-lan866x-ledblink--the-running-light)
6. [How GPIO-over-RCP works (code walk-through)](#6-how-gpio-over-rcp-works-code-walk-through)
   - 6.1 [What does `set_pin()` return mean — "received" or "actually set"?](#61-what-does-set_pin-return-mean--received-or-actually-set)
   - 6.2 [Non-blocking variant — `lan866x-ledtoggle`](#62-non-blocking-variant--lan866x-ledtoggle)
7. [Build & run](#7-build--run)
8. [The same code on a real MCU](#8-the-same-code-on-a-real-mcu)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Why an LED demo

On a microcontroller, *"blink an LED"* is the first program everyone writes: it
proves the toolchain, the clock, and a GPIO pin all work. This project is a
**host-side template for an embedded system** (pure C, single-thread superloop,
ports 1:1 to a 32-bit MCU — see [PORTING.md](../PORTING.md)). So its *hello world*
is the same idea, with one twist: **the GPIO lives on a remote LAN866x endpoint**
and we toggle it over the network via the **Remote Control Protocol (RCP)** on
top of **SOME/IP**.

If you can blink an LED this way, you have exercised the whole stack end to end:

```
service discovery → endpoint selection → pin release → GPIO open → GPIO write
```

…which is exactly what every other tool in this repo (i2cscan, spi, adc, …) does
before it gets to its real work.

---

## 2. The hardware path

```
 ┌─────────┐  USB   ┌────────────────────────┐   10BASE-T1S    ┌────────────────────────┐
 │   PC    │───────►│  EVB-LAN8670-USB-PSE    │═════════════════│  EVB-LAN8680-LAN866x   │
 │ ledblink│ (NIC)  │  USB→T1S adapter (PSE)  │  (2-wire+PoDL)  │  LAN8661 endpoint MCU  │
 └─────────┘        └────────────────────────┘                 └───────────┬────────────┘
                                                                            │ PA02/PA06/PA10
                                                                            ▼
                                                                   LD1   LD2   LD3
                                                                   (on-board LEDs)
```

- The PC runs `lan866x-ledblink`. The USB adapter is a normal Ethernet NIC whose
  wire is the single-pair T1S bus.
- The endpoint is the **LAN8661** application MCU (the "Lighting" part on the
  Apps Board). Its GPIO pins **PA00–PA15** are what the RCP GPIO service drives.
- LD1/LD2/LD3 are the three on-board LEDs, enabled by DIP switch **SW13-1/2/3**
  (the default demo configuration).

> The two front-end push-buttons (`BUTTON_1`/`BUTTON_2`) are **not** on this MCU —
> they hang on the LAN8680 front-end (DIO0/DIO1) and are *not* reachable via the
> RCP GPIO service. See [TOOLS.md §2.7](../TOOLS.md#27-on-board-leds--buttons).

---

## 3. Which pins, and how we found them

The on-board LEDs are wired to the LAN8661's **SERCOM CS lines (P2)**, gated onto
the LEDs by the 8-way DIP switch **SW13**:

| LED | GPIO pin | SERCOM function | SW13 position |
|---|---|---|---|
| **LD1** | **PA02** | SER0 CS | SW13-1 |
| **LD2** | **PA06** | SER1 CS | SW13-2 |
| **LD3** | **PA10** | SER2 CS | SW13-3 |
| (LD4) | PA14 | SER3 CS | SW13-4 — **OFF** by default → dark |

This mapping was **not** taken from prose — it was measured live with
`lan866x-ledscan` (next section) and it matches the schematic exactly:
the three lit pins (PA02/PA06/PA10) are the CS pins of SER0/SER1/SER2, and the
fourth (PA14) stays dark because SW13-4 is open in the shipped configuration.

> Pins **PA03 / PA07** (the MOSI / P3 lines of SER0/SER1) could **not** be opened
> as GPIO during the scan — they are already claimed by the firmware to drive the
> two WS2812 RGB panels on Click 1/2. That is expected, not an error.

---

## 4. `lan866x-ledscan` — find the LED pins

`lan866x-ledscan` is the discovery tool that produced the table above. It walks a
list of candidate pins, **blinks each one**, and asks the operator whether an LED
blinked. The yes/no answers are written to a JSON file you can read back.

```bat
release\lan866x-ledscan.exe --ip 192.168.0.54
release\lan866x-ledscan.exe --all                 REM probe PA00..PA15
release\lan866x-ledscan.exe --pins 2,6,10,14      REM probe a specific set
release\lan866x-ledscan.exe --blinks 8 --on 200 --off 200 --out my_leds.json
```

Per pin it asks `[y]es / [n]o / [r]epeat / [q]uit` (`r` re-blinks if you missed
it, `q` aborts but still writes what was found). The result is machine-readable
[`led_map.json`](../release/led_map.json):

```json
{
  "tool": "lan866x-ledscan",
  "endpoint": "192.168.0.54",
  "timestamp": "2026-06-20 16:21:01",
  "blink": { "count": 6, "on_ms": 250, "off_ms": 250 },
  "results": [
    { "pin": 2,  "name": "PA02", "led": true,  "status": "led" },
    { "pin": 3,  "name": "PA03", "led": false, "status": "skipped" },
    { "pin": 6,  "name": "PA06", "led": true,  "status": "led" },
    { "pin": 7,  "name": "PA07", "led": false, "status": "skipped" },
    { "pin": 10, "name": "PA10", "led": true,  "status": "led" },
    { "pin": 11, "name": "PA11", "led": false, "status": "none" },
    { "pin": 14, "name": "PA14", "led": false, "status": "none" },
    { "pin": 15, "name": "PA15", "led": false, "status": "none" }
  ],
  "leds_found": [2, 6, 10]
}
```

`status` is `led` (operator saw it), `none` (no LED), or `skipped` (the pin could
not be opened — already in use). `leds_found` is the ready-to-use pin list that
`lan866x-ledblink` defaults to.

---

## 5. `lan866x-ledblink` — the running light

The demo itself. It drives the three LEDs as a **running light**: LD1 on for half
a second, off; LD2 on, off; LD3 on, off; then back to LD1 — forever, until Ctrl+C.

```bat
release\lan866x-ledblink.exe                       REM PA02,PA06,PA10 @ 500 ms/step
release\lan866x-ledblink.exe --ip 192.168.0.54
release\lan866x-ledblink.exe --pins 2,6,10 --beat 250
release\lan866x-ledblink.exe --all-on              REM all LEDs on, then exit
```

| Option | Meaning | Default |
|---|---|---|
| `--pins <list>` | comma list of PA pins to cycle | `2,6,10` (LD1,LD2,LD3) |
| `--beat <ms>` | time each LED stays lit per step | `500` |
| `--all-on` | turn every listed LED on once and exit (no loop) | — |
| `--ip` / `--ep` | target endpoint (IP or discovery index) | first endpoint |

On **Ctrl+C** it turns all LEDs off and releases the pins cleanly, so the board is
left in a known state.

---

## 6. How GPIO-over-RCP works (code walk-through)

The whole demo is four RCP calls. The relevant types come from
`lan866x_common.h`; the wrappers from [`src/rcp.c`](../src/rcp.c) (full reference:
[docs/RCP_API.md](RCP_API.md)).

**(1) Discover & select the endpoint.** The tools never hard-code an IP; they learn
it from SOME/IP Service Discovery and pick the requested one:

```c
tool_select(wantIp, wantEp, 5, "LAN866x LED running-light demo");
```

**(2) Release the pin, then open it as an output.** A pin may still be configured
from a previous run, so we release it first (`ReleaseDigitalPins`, 0x1105), then
`OpenGpio` (0x1300) with `Direction = 1` (output, starts LOW). The reply carries a
**handle** we use for every later write:

```c
ReleaseDigitalPinsVar_t rel = {0};
rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
rcp_release_digital_pins(&rel);

OpenGpioVar_t ov = {0}; OpenGpioReply_t orep = {0};
ov.PinIdGpio = pin;
ov.Direction = 1;                 /* output, LOW */
rcp_open_gpio(&ov, &orep);
uint16_t handle = orep.HandleGpio;
```

**(3) Write the pin.** `SetGpio` (0x1330) takes 3-byte tuples
`[handleHi, handleLo, value]`:

```c
SetGpioVar_t sv = {0};
sv.GpioValues[0] = handle >> 8;
sv.GpioValues[1] = handle & 0xFF;
sv.GpioValues[2] = 1;             /* 1 = HIGH = LED on, 0 = LOW = off */
sv.GpioValuesLength = 3;
rcp_set_gpio(&sv);
```

**(4) The loop.** One LED lit at a time, advancing every `beat` ms:

```c
while (g_run) {
    for (i = 0; i < nPins && g_run; ++i) {
        set_pin(handle[i], 1);    /* on  */
        Sleep(beat);
        set_pin(handle[i], 0);    /* off */
    }
}
```

Two single-thread facts worth knowing (full list in
[docs/INTEGRATION_NOTES.md](INTEGRATION_NOTES.md)):

- `rcp_set_gpio()` is **blocking** — it sends the request and pumps the transport
  until the reply (or timeout) arrives. No polling loop is needed in the demo.
- We pace control traffic with small `Sleep(20)` between setup round-trips; the
  Windows host drops back-to-back RCP replies (gotcha #4). The 500 ms beat is far
  more than enough spacing during the loop itself.

### 6.1 What does `set_pin()` return mean — "received" or "actually set"?

**It returns only after the endpoint has *executed* the command — i.e. the GPIO
register was written — not on mere receipt.** This matters if you ever build
timing-sensitive logic on top of it.

RCP is a request/response RPC over UDP, and **there is no separate transport
ACK**. There is exactly **one** reply: the SOME/IP *RESPONSE* message, and the
endpoint's firmware emits it from its method handler *after* it has run the
method. So "command received" and "command executed" are not two events — there
is only the single RESPONSE, and it carries the **return code** (the result).

In the wrapper, `rcp_set_gpio()` → `rcp_xfer(0x1330, …)` blocks on exactly that
reply ([`src/rcp.c`](../src/rcp.c)):

```c
while (!s_done && (now - start) < s_timeoutMs) { rcp_poll(); plat_sleep_ms(2); }
return (ReturnCode_t)s_rc;          /* the result code FROM the response */
```

`s_rc` is the code the firmware put in the RESPONSE. That it can be a meaningful
*error* (e.g. `RT_PARAMETER_NOT_VALID` for a bad handle) is the proof that this is
an **execution result**, not a bare "packet arrived" acknowledgement.

The one honest caveat: *"GPIO actually set"* means **the firmware wrote the output
register and reported OK** — which for an output drives the pin within
nanoseconds. The firmware does **not** read the physical voltage back to verify
it. If you want the electrical level confirmed, read it back with `GetGpio`
(`0x1332`) — though for an output that just echoes the value you set.

Timing consequence for the running light:

| Case | What happens | Cost |
|---|---|---|
| Normal | endpoint answers in ~2 ms (T1S RTT) → loop breaks immediately | negligible vs. the 500 ms beat |
| No answer | retries until `s_timeoutMs × (s_retries+1)` (default 1500 ms × 4) | up to ~6 s stall, then `RT_TIMEOUT` |

So each `Sleep(beat)` in the loop is really `beat + RTT`; at 500 ms that's
imperceptible. If you ever need *non-blocking* writes (e.g. a tight MCU superloop
that must never stall), use the async API `rcp_async_request()` /
`rcp_async_poll()` instead — it sends and returns, delivering the result later via
callback.

> **Exception — `Reboot`:** this "response = executed" guarantee holds for normal
> methods like `SetGpio`. `Reboot` (`0x1000`) is special: the device often resets
> *before* its response goes out, so the tools treat a lost reboot reply as success
> (see `lan866x-boot`). For `SetGpio` the reply is reliable: it means *done*.

### 6.2 Non-blocking variant — `lan866x-ledtoggle`

`lan866x-ledblink` is fine for a demo, but every `set_pin()` **stops the loop**
until the endpoint answers (§6.1). On a real device the main loop usually must
**stay responsive** — service other I/O, render a frame, feed a watchdog — while a
control round-trip is still in flight. That is what the **async API** is for, and
`lan866x-ledtoggle` is the minimal example of it: it toggles **one** LED at a
half-second beat **without ever blocking**.

```bat
release\lan866x-ledtoggle.exe --ip 192.168.0.54     REM toggle PA02 (LD1) @ 500 ms
release\lan866x-ledtoggle.exe --pin 6 --beat 250
```

**Blocking vs. non-blocking — the difference in one line:**

| | `ledblink` (blocking) | `ledtoggle` (non-blocking) |
|---|---|---|
| write call | `rcp_set_gpio()` → waits for the reply | `rcp_async_request()` → returns at once |
| during the round-trip | the loop is **stuck** in the call | the loop **keeps spinning** |
| reply handling | return value, inline | a short **callback** from `rcp_async_poll()` |
| good for | scripts, one-shot tools | superloops, fixed-rate work, many in-flight |

**How it works.** Three async pieces ([full API in rcp.h](../src/rcp.h),
[INTEGRATION_NOTES](INTEGRATION_NOTES.md)):

1. **Fire and forget the deadline.** `rcp_async_request(method, params, len, cb,
   ctx)` builds + sends the request and returns immediately. We set a deadline
   shorter than the beat so a lost reply times out before the next toggle:

   ```c
   rcp_set_async_timeout_ms(300);              /* < beat (500 ms) */
   ```

2. **Toggle on a clock, never on a reply.** The loop checks a millisecond clock;
   when the beat is due it flips the value and fires the request — it does **not**
   wait for the previous one:

   ```c
   if ((long)(GetTickCount() - nextToggle) >= 0) {
       value = !value;
       uint8_t p[16];
       uint16_t n = rcp_enc_gpio_set(p, sizeof(p), handle, value);  /* SetGpio params */
       rcp_async_request(0x1330, p, n, on_set_done, NULL);          /* returns at once */
       nextToggle += beat;
   }
   rcp_async_poll();   /* drives replies + deadlines; the callback runs from HERE */
   Sleep(2);           /* the rest of the superloop would go here */
   ```

3. **Handle the result in a short callback.** It runs **inline** from
   `rcp_async_poll()` (single-thread — same strand, never concurrent), so it needs
   no lock; it must stay short and must **not** call any `rcp_*` itself:

   ```c
   static void on_set_done(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen) {
       if (rc == RT_OK)           g_ok++;
       else if (rc == RT_TIMEOUT) g_to++;     /* deadline passed, no reply */
       else                       g_err++;
   }
   ```

The tool prints, at each toggle, **how many times the loop spun since the last
one** — concrete proof the main loop kept running freely instead of parking on the
network:

```
  PA02 -> ON   (loop spun 248 times since last toggle; ok=4 to=0 err=0)
  PA02 -> OFF  (loop spun 249 times since last toggle; ok=5 to=0 err=0)
```

**Things to know:**

- The `SetGpio` params for the async path are built with the small helper
  `rcp_enc_gpio_set()` (added alongside `rcp_enc_spi2`/`rcp_enc_i2c_read`) — the
  blocking `rcp_set_gpio()` does the same encoding internally.
- Up to `RCP_ASYNC_MAX` (16) requests may be outstanding at once. Here only one is
  ever in flight (300 ms deadline < 500 ms beat), but a faster `--beat` with a lossy
  link could overlap a few — that is fine and is exactly why the deadline exists.
- Setup (`OpenGpio`) and teardown (LED off) still use the simple **blocking** calls
  — they are one-time and not in the hot loop, so there is nothing to gain from
  making them async.
- This is the loop shape an MCU port wants. Nothing here is Windows-specific except
  `GetTickCount()`/`Sleep()`; on the MCU those become the lwIP/RTOS clock and yield
  (see [PORTING.md](../PORTING.md)).

---

## 7. Build & run

Pre-built binaries already ship in [`release/`](../release/); building is only
needed if you change the source.

```bat
build.bat mingw                 REM or: build.bat vs
release\lan866x-discovery.exe   REM confirm the endpoint is reachable
release\lan866x-ledblink.exe --ip 192.168.0.54
```

If you are on a fresh board and don't yet know the LED pins, run the scanner
first and feed its result in:

```bat
release\lan866x-ledscan.exe --all --out led_map.json
REM read the "leds_found" list, then:
release\lan866x-ledblink.exe --pins 2,6,10
```

---

## 8. The same code on a real MCU

This is the point of the whole repo. The four calls above
(`rcp_release_digital_pins`, `rcp_open_gpio`, `rcp_set_gpio`) and the discovery
flow are **platform-independent C**. Porting the demo to a 32-bit MCU (lwIP,
single-thread superloop) means writing exactly **one file** — `src/plat_<target>.c`
implementing the six functions of [`src/plat.h`](../src/plat.h) (millisecond clock,
non-blocking UDP, sleep). `ledblink.c`, `rcp.c`, the SOME/IP core and the stub all
compile unchanged. See [PORTING.md](../PORTING.md).

So this LED running light is genuinely the *hello world* for the embedded target:
get it blinking, and the transport + RCP layer are proven for everything else.

---

## 9. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `No endpoints found` | NIC has no `192.168.0.x` IP · bus not terminated · endpoint unpowered · firewall blocks SD multicast. Run `lan866x-discovery` first. |
| `OpenGpio failed on PAxx` | the pin is already used by the firmware (e.g. PA03/PA07 drive the WS2812 panels) — pick a real LED pin from `led_map.json`. |
| LED never lights though `SetGpio` returns OK | the LED's **SW13** switch is open. The shipped default enables SW13-1/2/3 (LD1–LD3); LD4/PA14 needs SW13-4 ON. |
| Runs but LEDs flicker irregularly | host dropping back-to-back replies — harmless for a blink demo; increase `--beat`. |
| LEDs stay on after a hard kill | `ledblink` only cleans up on Ctrl+C. Re-run and Ctrl+C, or `lan866x-gpio --pin <n> --set 0`. |

See also: [TOOLS.md](../TOOLS.md) (board guide + per-tool reference),
[docs/RCP_API.md](RCP_API.md) (full `rcp_*` reference),
[README.md](../README.md) (build & overview).
