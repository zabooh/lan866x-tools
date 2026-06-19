# CLAUDE.md — guidance for working on this repo with Claude Code

`lan866x-tools` is a **pure-C** host toolset that remote-controls Microchip
**LAN866x 10BASE-T1S** endpoints over the **RCP** service (`0xFF10`) on top of
SOME/IP, via a T1S-to-USB adapter. Each tool is a small command-line program.

Read this file first, then `README.md` (build/usage), `docs/INTEGRATION_NOTES.md`
(the hard-won protocol/stack know-how), `PORTING.md` (MCU port), `TOOLS.md`.

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

## Architecture
- Vanilla **C**, no C++/libstdc++. One `.c` per tool: discovery, i2cscan, gpio,
  spi, adc, pwm, boot, flashimg, flashpkg, clickdemo, diag, video, dncpmon, dncpdisc.
- Shared core built as the `rcpcore` static lib:
  - `src/rcp.c` — RCP-over-SOME/IP wrapper (typed methods + an async API).
  - `src/someip_stub_win.c` — Win32 platform stub (all `SOMEIP_CB_*`).
  - `libepmicrochip/libsomeip/*` — the C SOME/IP stack.
- **Ports to a 32-bit MCU** (lwIP/FreeRTOS) by swapping only the stub — see PORTING.md.
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
3. **`SOMEIP_CB_EnterCriticialSection` is a NON-reentrant semaphore**, and the
   transmit layer (`someip-transmit.c` ReceivedResponse/CheckTimers) calls a
   buffer's callback **while holding it**. A response/async callback **must not**
   take that lock again → it self-deadlocks the rx thread and wedges everything.
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
delivers replies/timeouts; `rcp_set_async_timeout_ms()` sets the deadline. The
callback runs under the transmit lock (gotcha #3 — keep it short, no rcp_* calls).
Param/reply helpers: `rcp_enc_spi2`/`rcp_dec_spi2`, `rcp_enc_i2c_read`/`rcp_dec_i2c_read`.

## Method IDs
Base table is in `README.md` §8; corrected/extra IDs and all encoding rules are in
`docs/INTEGRATION_NOTES.md`. Authoritative source = the SOME/IP **dissector CSV**,
not the integration-manual prose (which has typos).
