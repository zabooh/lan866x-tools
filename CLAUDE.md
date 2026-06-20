# CLAUDE.md — guidance for working on this repo with Claude Code

`lan866x-tools` is a **pure-C** host toolset that remote-controls Microchip
**LAN866x 10BASE-T1S** endpoints over the **RCP** service (`0xFF10`) on top of
SOME/IP, via a T1S-to-USB adapter. Each tool is a small command-line program.

Read this file first, then `README.md` (build/usage), `docs/INTEGRATION_NOTES.md`
(the hard-won protocol/stack know-how), `docs/RCP_API.md` (the full `rcp.c` API
reference), `PORTING.md` (MCU port), `TOOLS.md`.

## Constraints — do not violate
- **Keep this repo private.** It bundles Microchip SLA001 vendor sources under
  `libepmicrochip/`. Do not publish or make it public.
- **No end-customer name** anywhere (code, comments, commits, output, filenames).
  The toolset must stay generic.
- **Do not commit NDA material**: datasheets, integration/user manuals, app
  notes, board dossiers, firmware packages. These live locally *outside* the
  repo (e.g. an `EVB/…` dossier on the dev's machine) and must not be added.
- Bundled third-party: `libepmicrochip/` (Microchip SLA001) and
  `third-party/minizip/` (bitbank2 unzipLIB fork). Respect their licenses.

## Local reference material (NDA) — ASK for the path
The datasheets, manuals, app notes, schematic, board dossier and firmware
packages are **not in this repo** (NDA) but are often needed (e.g. to look up an
RCP method, a register, a pin, or to flash a build). They live in a separate
local directory on the developer's machine — on the original setup `C:\work\EVB\`,
but **the path varies per machine, so ASK the developer for it; do not assume or
hard-code it.** Once given, read from there (don't copy NDA files into the repo).

That directory should contain (and a developer setting it up should provide):
- **Endpoint User Guide** PDF (`LAN866x-Endpoint-Users-Guide-…`) — the RCP/SOME-IP
  service spec (method IDs, request/reply structs, reset/diagnosis fields). Primary ref.
- **Chip datasheets**: LAN8660 (Control), LAN8661 (Lighting), LAN8662 (Audio).
- **Board schematic** (`02-01132-R3_SCH.pdf`) + the EVB **board dossier**
  (Setup: DIP switches / Click mounting guide, photos, Wireshark captures).
- **Firmware packages** (`*.mchpkg`): the display builds + `reference-configs/`
  (LAN8660/61/62, V1.3.2 & V1.4.0) — used by `lan866x-flashpkg`.
- **App notes**: Remote-Service + RTP video; firewall/ports. **Integration guides**
  (libconfigurator integration guide, Remote Configurator user guide).
- **T1S-USB adapter** user guide + Win/Linux drivers (EVB-LAN8670-USB-PSE).
- **Peripheral datasheets**: VCNL4200 (proximity), MCP3204 (thumbstick ADC).
- A `README.md` cataloguing all of the above (the `C:\work\EVB\README.md` is the template).

## Architecture
- Vanilla **C**, no C++/libstdc++. One `.c` per tool: discovery, i2cscan, i2cid,
  proxmon, gpio, ledscan, ledblink, ledtoggle, ledpwm, proxled, spi, spiid,
  thumbmon, adc, pwm, boot, flashimg, flashpkg, clickdemo, diag, video, dncpmon,
  dncpdisc. Didactic non-blocking examples (async RCP): `ledtoggle` (GPIO),
  `i2cid`/`proxmon` (I²C VCNL4200), `spiid`/`thumbmon` (SPI MCP3204), `ledpwm` (PWM,
  firmware-dependent), `proxled` (sensor→actuator: proximity→LEDs). Plus `ledblink`
  (GPIO "hello world" running light) and `ledscan` (GPIO→LED mapper → `led_map.json`).
  See docs/LEDDEMO.md, I2CDEMO.md, SPIDEMO.md, COMBODEMO.md, PWMDEMO.md.
- **Single-thread** (superloop): received UDP is dispatched synchronously from a
  per-tick poll (`plat_udp_poll()` inside `rcp_poll()`/`rcp_async_poll()`). No
  threads, no locks, no `volatile` shared state.
- Shared core built as the `rcpcore` static lib:
  - `src/rcp.c` — RCP-over-SOME/IP wrapper (typed methods + an async API).
  - `src/someip_stub.c` — platform-neutral `SOMEIP_CB_*`, built on the `plat.h` layer.
  - `src/plat.h` + `src/plat_win.c` — the narrow platform layer (time, non-blocking
    UDP, sleep); `plat_win.c` is the Windows (Winsock) implementation.
  - `libepmicrochip/libsomeip/*` — the C SOME/IP stack.
- **Port to a 32-bit MCU** (lwIP) by writing one file `src/plat_<target>.c` (the six
  `plat.h` functions) — see PORTING.md. `someip-cfg.h` is sized MCU-friendly
  (core static RAM ≈ 15 kB).
- Wire facts: service `0xFF10`; SD on UDP **30490**; method endpoint on UDP **6800**;
  the endpoint **replies from src port 49153**. Video is RTP/RFC4175 on UDP **5001**.

## Build / run / test
- Build: `build.bat mingw` (MinGW-w64 GCC) or `build.bat vs` (MSVC) → `release/`.
- Each tool self-documents: `lan866x-<tool> --help`. Target with `--ip <addr>` or `--ep <i>`.
- Live test needs a board: `lan866x-discovery` lists endpoints; `lan866x-diag --ip <addr>`
  reports link quality.
- **Keep 0 C++ symbols**: `nm release/lan866x-<tool>.exe | grep -cE '_ZSt|_ZNSt|__gxx'` → 0.

## Non-obvious gotchas (don't relearn these the hard way)
1. **`SOMEIP_Generator_Fill_*` writes absolutely at `pBuf[0]`** and only advances
   `*consumed`. Pass `&buf[consumed]` and `(MAX-consumed)` for *every* field
   (like the C++ `LAN866XClientImpl`), else each field overwrites the previous →
   device returns `E_MALFORMED_MESSAGE` (0x09).
2. **I²C presence / bus scan must use `ReadI2C` (0x1220)** — a pure read — **not**
   `WriteAndReadI2C` (0x1208) with a 0-byte write: this firmware does not report
   an address NACK as an error for that call, so absent addresses look "present"
   (phantom devices). I²C is deterministic; if a scan looks "noisy", it's the
   wrong method or host reply-drops, not the bus.
3. **Single-thread model:** all `SOMEIP_CB_*` run on one strand, so
   `SOMEIP_CB_EnterCriticialSection`/`Leave` are no-ops. Response/async callbacks
   fire **synchronously** from `rcp_poll()`/`rcp_async_poll()` (the transmit layer
   invokes them inline) — keep callbacks short and **do not** re-enter `rcp_*` from
   inside them (it would reuse transmit buffers mid-iteration).
4. **The Windows host drops RCP replies under back-to-back requests** (~60% loss
   at 0 ms gap vs ~1% at ~20 ms). Pace control traffic, batch reads (compound SPI
   `0x1509`), or read sensors sequentially. The **T1S link itself is excellent**
   (~2 ms RTT, ~0% wire loss, PLCA) — verified by capture. Many active host NICs
   make the drop worse (the SD multicast is joined on every interface).
5. **Displays:** send **one 20×10 RTP frame** (left half → display 1, right half →
   display 2). Sending each display as a separate RFC4175 region packet makes the
   firmware split each display top/bottom.
6. **Peripheral wedge:** after hard-killed runs, `OpenSpi`/`OpenI2C` may start
   failing; a soft RCP reboot does **not** clear it — needs a **physical
   power-cycle** of the board. Pace open/setup round-trips to dodge gotcha #4.
7. **Windows `Sleep` granularity ~15.6 ms.** Call `timeBeginPeriod(1)` for snappy
   loops, and use a real-time `SOMEIP_CB_GetTimeMS()` deadline for timeouts (never
   count `Sleep(2)` iterations).
8. **`FinishUpdate` can return a benign `RT_NOT_REACHABLE` (5) even on success.**
   `flashpkg` judges success by **outcome** (reboot + running version), not the rc.

## Async RCP (non-blocking)
`rcp_async_request(method, params, len, cb, ctx)` sends and returns; `rcp_async_poll()`
delivers replies/timeouts; `rcp_set_async_timeout_ms()` sets the deadline. The callback
runs synchronously from `rcp_async_poll()` (single-thread — keep it short, no rcp_* calls).
Param/reply helpers: `rcp_enc_spi2`/`rcp_dec_spi2`, `rcp_enc_i2c_read`/`rcp_dec_i2c_read`.

## Method IDs
Base table is in `README.md` §8; corrected/extra IDs and all encoding rules are in
`docs/INTEGRATION_NOTES.md`. The full per-function `rcp_*` reference (method IDs,
request/reply structs, return codes, WTLV encoding) is in `docs/RCP_API.md`.
Authoritative source = the SOME/IP **dissector CSV**, not the integration-manual
prose (which has typos).
