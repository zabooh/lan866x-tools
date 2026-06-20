# Demos — the worked examples, from "hello world" up

This is the index of the **didactic examples**: small, single-purpose tools that
each show one piece of "remote-control a LAN866x peripheral over SOME/IP (RCP)" in
**pure C**. Every demo has its own deep-dive doc *and* a single `.c` source file you
can read end to end. They build on each other — read them in order and you've seen
the whole stack (discovery → open → read/write, blocking and non-blocking).

> New to the hardware first? See **[../howto_demonstrate.md](../howto_demonstrate.md)**
> (boards, wiring, IP, flashing) and **[../TOOLS.md](../TOOLS.md)** (board guide +
> per-tool reference). For the full `rcp_*` API: **[RCP_API.md](RCP_API.md)**.

---

## Suggested learning path

| # | Demo | Source | Doc | What you learn |
|---|---|---|---|---|
| 1 | **LED running light** ("hello world") | [../ledblink.c](../ledblink.c) | [LEDDEMO.md](LEDDEMO.md) | discovery → `OpenGpio` → `SetGpio`; the **blocking** request/response model |
| 2 | **GPIO→LED mapper** | [../ledscan.c](../ledscan.c) | [LEDDEMO.md §4](LEDDEMO.md#4-lan866x-ledscan--find-the-led-pins) | interactive probing; writing results to JSON ([../release/led_map.json](../release/led_map.json)) |
| 3 | **Non-blocking LED toggle** | [../ledtoggle.c](../ledtoggle.c) | [LEDDEMO.md §6.2](LEDDEMO.md#62-non-blocking-variant--lan866x-ledtoggle) | the **async API** (`rcp_async_request`/`rcp_async_poll`); why the loop must not block |
| 4 | **I²C device-ID read** | [../i2cid.c](../i2cid.c) | [I2CDEMO.md](I2CDEMO.md) | `WriteAndReadI2C` (register read), non-blocking; VCNL4200 ID `0x1058` |
| 5 | **I²C live monitor** | [../proxmon.c](../proxmon.c) | [I2CDEMO.md §8](I2CDEMO.md#8-going-further-live-monitor--sensoractuator) | continuous sensor reads at a steady cadence (decoupled from replies) |
| 6 | **SPI thumbstick identify** | [../spiid.c](../spiid.c) | [SPIDEMO.md](SPIDEMO.md) | `WriteAndReadSpi` full-duplex transfer; MCP3204 (no ID register → fingerprint) |
| 7 | **SPI live monitor** | [../thumbmon.c](../thumbmon.c) | [SPIDEMO.md §8](SPIDEMO.md#8-going-further-live-monitor) | one async transfer in flight, alternating axes |
| 8 | **Sensor → actuator app** | [../proxled.c](../proxled.c) | [COMBODEMO.md](COMBODEMO.md) | the full **input → decide → output** superloop (I²C in, GPIO out), no video |
| 9 | **Breathing LED (PWM)** | [../ledpwm.c](../ledpwm.c) | [PWMDEMO.md](PWMDEMO.md) | `OpenPwm`/`WritePwm` duty ramp ⚠️ *PWM firmware-dependent* |
| — | **Click demo** (full app) | [../clickdemo.c](../clickdemo.c) | [CLICKDEMO.md](CLICKDEMO.md) | two sensors + RTP video actuator; the production-scale version of #8 |

All tools are pre-built under [../release/](../release/) — run them without building.
Target an endpoint with `--ip <addr>` or `--ep <index>`; every tool has `--help`.

---

## By peripheral / RCP topic

- **GPIO (digital out):** [../ledblink.c](../ledblink.c) (blocking running light),
  [../ledtoggle.c](../ledtoggle.c) (non-blocking), [../ledscan.c](../ledscan.c)
  (find the LED pins) — all in [LEDDEMO.md](LEDDEMO.md). Single pin set/read:
  [../gpio.c](../gpio.c).
- **I²C:** [../i2cid.c](../i2cid.c) (ID read), [../proxmon.c](../proxmon.c) (live)
  — [I2CDEMO.md](I2CDEMO.md). Bus scan: [../i2cscan.c](../i2cscan.c). Reading the
  **LAN8680 front-end** (SBC) over its housekeeping I²C: [../lan8680.c](../lan8680.c)
  — [LAN8680.md](LAN8680.md).
- **SPI:** [../spiid.c](../spiid.c) (identify), [../thumbmon.c](../thumbmon.c)
  (live) — [SPIDEMO.md](SPIDEMO.md). Raw transfer: [../spi.c](../spi.c).
- **PWM:** [../ledpwm.c](../ledpwm.c) — [PWMDEMO.md](PWMDEMO.md). Raw PWM:
  [../pwm.c](../pwm.c).
- **Sensor → actuator:** [../proxled.c](../proxled.c) — [COMBODEMO.md](COMBODEMO.md);
  full version [../clickdemo.c](../clickdemo.c) — [CLICKDEMO.md](CLICKDEMO.md).

---

## Blocking vs. non-blocking at a glance

| | Blocking | Non-blocking (async) |
|---|---|---|
| call | `rcp_set_gpio()`, `rcp_write_and_read_i2c()`, … | `rcp_async_request()` + `rcp_async_poll()` |
| loop during round-trip | **parked** until the reply | **keeps spinning**; reply via callback |
| examples | [../ledblink.c](../ledblink.c), [../i2cid.c](../i2cid.c) (setup) | [../ledtoggle.c](../ledtoggle.c), [../proxmon.c](../proxmon.c), [../thumbmon.c](../thumbmon.c), [../proxled.c](../proxled.c), [../ledpwm.c](../ledpwm.c) |
| why it matters | simple scripts / one-shots | superloops, fixed-rate work, MCU ports |

The shared core every demo links: [../src/rcp.c](../src/rcp.c) / [../src/rcp.h](../src/rcp.h)
(RCP over SOME/IP), [../src/someip_stub.c](../src/someip_stub.c),
[../src/plat_win.c](../src/plat_win.c) (platform layer), and the small
discover-and-select helper [../src/tool_common.h](../src/tool_common.h).

---

## What's intentionally not (yet) a demo

- **UART** (`OpenUart`/`WriteUart`/`ReadUart`) — no tool yet; needs a loopback wire
  and is unconfirmed on the Lighting firmware.
- **GPIO event subscription** (`OnGpioEvents 0x8000`) — push instead of poll;
  firmware support unconfirmed.
- **ADC** (`OpenAdc`) — **not implemented** on the Lighting firmware
  (`E_UNKNOWN_METHOD`); raw tool: [../adc.c](../adc.c) (needs a Control build).

See also the non-demo utilities in [../TOOLS.md](../TOOLS.md): discovery, diag, boot,
flashimg/flashpkg, video, dncpmon/dncpdisc.
