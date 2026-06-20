# Porting to another platform (MCU + lwIP)

The toolset is **single-thread** and structured so that a new target needs **exactly
one new file**: `src/plat_<target>.c`, the implementation of the narrow `src/plat.h`
interface. Everything above it stays **unchanged**:

- the tools (`clickdemo.c`, `discovery.c`, …),
- the RCP wrapper (`src/rcp.c` / `rcp.h`),
- the platform-neutral SOME/IP stub (`src/someip_stub.c`),
- the SOME/IP core (`libepmicrochip/libsomeip/src/*.c`).

> A ready-to-fill **template** of the lwIP/bare-metal port is in
> [`src/plat_lwip.c.template`](src/plat_lwip.c.template). Rename it to `plat_lwip.c`,
> fill in the board hooks, and swap it for `plat_win.c` in `CMakeLists.txt`.

## Exact source files to compile (drop-in for an existing FreeRTOS + lwIP project)

This repo's `CMakeLists.txt` builds standalone Windows tools, but you don't need
CMake to embed the stack. If you already have an ARM target with **FreeRTOS** and
**lwIP** running, add the SOME/IP client by compiling **exactly these 8 `.c`
files** into your existing build (Makefile, MPLAB X, STM32CubeIDE, Keil, …) and
adding **4 include paths**. Nothing else from this repo is required.

### Compile these (8 translation units)

| File | Role |
|---|---|
| `libepmicrochip/libsomeip/src/someip-client.c` | SOME/IP service/discovery state machine |
| `libepmicrochip/libsomeip/src/someip-gen.c` | message generator (request encoder) |
| `libepmicrochip/libsomeip/src/someip-pars.c` | message parser (reply decoder) |
| `libepmicrochip/libsomeip/src/someip-timer.c` | SD / retransmit timers |
| `libepmicrochip/libsomeip/src/someip-transmit.c` | transmit buffer pool + send path |
| `src/someip_stub.c` | platform-neutral `SOMEIP_CB_*` over `plat.h` (**do not modify**) |
| `src/rcp.c` | the RCP-over-SOME/IP wrapper — typed methods + async API (your app's entry point) |
| `src/plat_lwip.c` | **the one file you write** — the six `plat.h` functions over lwIP (from `plat_lwip.c.template`) |

> Tip: `libsomeip/src/*.c` is a clean glob — adding all `.c` in that folder is
> correct and future-proof. The only per-target file is `plat_lwip.c`.

### Add these 4 include paths (`-I`)

| Include path | Headers it provides |
|---|---|
| `libepmicrochip/libsomeip/inc` | `someip.h` (public API) |
| `libepmicrochip/libsomeip/src` | `someip-common.h`, `someip-gen.h`, `someip-pars.h`, `someip-timer.h` |
| `libepmicrochip/libsomeip/stub` | `someip-cfg.h` (compile-time sizing — already MCU-tuned) |
| `src` | `plat.h`, `rcp.h`, `someip_stub.h` |

### Do NOT copy these (Windows-only / dead / not the stack)

| File | Why not |
|---|---|
| `src/plat_win.c` | Windows (Winsock) impl — **replaced by your `plat_lwip.c`** |
| `src/someip_stub_win.c` | old **threaded** stub, superseded by `src/someip_stub.c` (single-thread); not built |
| `libepmicrochip/libsomeip/stub/windows-udp-handler.c` / `.h` | old threaded Windows UDP path; not built |
| `*.c` tool files (`discovery.c`, `clickdemo.c`, `diag.c`, …) | standalone CLI programs; you write your own app that calls `rcp_*` instead. Read them as **usage examples** only |
| `third-party/minizip/` | only `flashpkg`'s ZIP reader; not part of the SOME/IP stack |
| `include/`, `src/tool_common.h` | helpers for the CLI tools, not referenced by `rcp.c`/`someip_stub.c` |

### Minimal API your application calls

After the files above are in your build, the application code is just the RCP
wrapper — include `rcp.h`, then on your existing strand:

```c
#include "rcp.h"
rcp_init(...);                 /* once, after the lwIP netif is UP with a valid IP */
for (;;) {
    app_step();                /* your work */
    rcp_async_poll();          /* drives plat_udp_poll() -> lwIP -> synchronous RX */
}
```

`rcp.c` is the only file you `#include` from; `someip_stub.c` and the libsomeip
core are pulled in by the linker. See the `## Steps` and `plat.h` sections below
for how `plat_lwip.c` bridges to lwIP and where it fits with FreeRTOS.

> 📖 **API reference:** every `rcp_*` function your application calls — method IDs,
> request/reply structs, return codes and the WTLV encoding rules — is documented in
> [`docs/RCP_API.md`](docs/RCP_API.md), which also doubles as a from-scratch
> re-implementation guide if you port to another language/transport.

### FreeRTOS + lwIP specifics (your stated platform)

Because FreeRTOS and lwIP already exist, use the **RTOS variant** of the port
(also noted in *Single-thread model* below):

- Run `rcp_init()` + the `rcp_async_poll()` loop in **one dedicated task** — that
  task is the single strand the whole stack assumes. Do **not** call `rcp_*` from
  any other task or from an ISR.
- Let lwIP's `tcpip_thread` receive frames as usual; have `plat_udp_poll()` either
  use lwIP's **socket API** (`LWIP_SOCKET=1`, non-blocking `recvfrom`) or drain a
  queue that your `udp_recv` callback fills. Either way the RX is dispatched
  synchronously inside `plat_udp_poll()`.
- Map `plat_sleep_ms()` → `vTaskDelay(pdMS_TO_TICKS(ms))` and `plat_now_ms()` →
  `xTaskGetTickCount() * portTICK_PERIOD_MS` (or `sys_now()`).
- The mutual-exclusion callbacks stay no-ops **as long as only that one task
  touches `rcp_*`** — this is what keeps the port lockless.
- lwIP config: `LWIP_IGMP=1` (for the SD multicast `224.0.0.1` join) and a UDP
  pool large enough for the SD + method sockets.

## Single-thread model

There are **no application threads**. Received UDP datagrams are delivered
**synchronously** from `plat_udp_poll()`, which the superloop calls once per tick (via
`rcp_poll()` / `rcp_async_poll()` → `someip_service()`), on the same and only
execution strand as the rest of the program. For the port this means:

- A plain `while (1)` superloop is enough — **no RTOS required** (bare metal + lwIP RAW API).
- No `volatile` / atomic / lock is needed for the shared request/reply state.
- The mutual-exclusion callbacks collapse: `SOMEIP_CB_EnterCriticialSection`/`Leave`
  and `SOMEIP_CB_NeedService` are no-ops; the `SOMEIP_CB_Sem*` set is never called.

An RTOS target is also supported: let an lwIP `tcpip_thread` receive frames and have
`plat_udp_poll()` drain a queue the tcpip callback fills, and map `plat_sleep_ms` to
`vTaskDelay`. The rest of the porting surface is identical.

## What stays / what you write

| Layer | This repo (Windows) | New target |
|---|---|---|
| Tools + RCP encoding (`*.c`, `rcp.c`) | C | **same** |
| SOME/IP core (`someip-client/-gen/-pars/-transmit/-timer.c`) | C | **same** |
| SOME/IP platform stub (`SOMEIP_CB_*`) | `src/someip_stub.c` | **same** (platform-neutral) |
| **Platform layer (`plat.h`)** | `src/plat_win.c` | **`src/plat_<target>.c` — the only file you write** |
| `someip-cfg.h` sizes | PC sizes | tune smaller if RAM-constrained |
| T1S connection | USB-Ethernet bridge | LAN8650/51 MAC-PHY (OA-SPI) or LAN8670 PHY (RMII) + lwIP |

## The `plat.h` porting surface — six functions, three responsibilities

| Function(s) | Task | lwIP / bare-metal implementation |
|---|---|---|
| `plat_now_ms` | millisecond time base (monotonic; 32-bit wrap is fine) | `sys_now()` (or a systick counter) |
| `plat_udp_open` / `_send` / `_close` | non-blocking UDP socket | `udp_new`/`udp_bind`/`udp_recv` ; `udp_sendto` ; `udp_remove` |
| `plat_udp_poll` | drain RX, dispatch the rx cb **synchronously** | pump the netif (`ethernetif_input`) + `sys_check_timeouts()`; lwIP calls the `udp_recv` trampoline inline |
| `plat_udp_join_multicast` | join the SD group `224.0.0.1` | `igmp_joingroup` (needs `LWIP_IGMP=1`) |
| `plat_net_enum_ifaces` | local IPs + masks (for subnet match) | iterate `netif_list` |
| `plat_sleep_ms` / `plat_yield` | wait between ticks | spin while pumping the stack (bare metal) / `vTaskDelay` (RTOS) |

## You do NOT reimplement the `SOMEIP_CB_*` callbacks

The libsomeip core calls a fixed set of platform callbacks. They are implemented
**once, platform-neutrally**, in `src/someip_stub.c` on top of `plat.h`; you never
rewrite them per target. How the stub maps each one:

| Callback | Mapped to |
|---|---|
| `SOMEIP_CB_OpenSocket` | `plat_udp_open` (registers the synchronous RX trampoline) |
| `SOMEIP_CB_SendUdp` | `plat_udp_send` (+ `plat_udp_join_multicast` for `224.0.0.1`) |
| `SOMEIP_CB_GetLocalIpAddr` | cached `plat_net_enum_ifaces` table (subnet match) |
| `SOMEIP_CB_GetTimeMS` | `plat_now_ms` |
| `SOMEIP_CB_EnterCriticialSection` / `Leave` | no-op (single-thread) |
| `SOMEIP_CB_NeedService` | no-op (the superloop pumps every tick) |
| `SOMEIP_CB_SemInit/Wait/Post/Destroy` | trivial stubs (the core never calls them) |
| `SOMEIP_CB_ProvideBuffer`/`Calloc`/`Free` | `malloc`/`calloc`/`free` (swap for a static pool if no heap) |
| `SOMEIP_CB_Log` | `printf` (retarget to UART/ITM) |

## Steps

1. **Write `plat_<target>.c`** from [`src/plat_lwip.c.template`](src/plat_lwip.c.template):
   the six `plat.h` functions over the lwIP **RAW API** (`LWIP_SOCKET=0`) with
   `LWIP_IGMP=1`. In `CMakeLists.txt`, replace `src/plat_win.c` with your file in the
   `rcpcore` library.
2. **Time base:** `plat_now_ms()` → `sys_now()` (bare metal) or
   `xTaskGetTickCount()*portTICK_PERIOD_MS` (RTOS).
3. **Pump loop:**
   ```c
   while (1) { app_render(); rcp_async_poll(); }   /* plat_udp_poll() runs inside */
   ```
   `plat_udp_poll()` pushes RX frames into lwIP, whose `udp_recv` callback fires
   synchronously and dispatches to the stub.
4. **Startup order:** PHY → lwIP `netif` UP → `rcp_init()` → service discovery → app.
   If SOME/IP starts before the interface has a valid IP, discovery fails.
5. **Memory:** for a no-heap target, back `SOMEIP_CB_ProvideBuffer`/`Calloc` with a
   static pool and set lwIP `MEM_LIBC_MALLOC=0`.

## Footprint — and how to size it
Static RAM is **entirely config-driven**: the core statically reserves a transmit
buffer pool of `SOMEIP_TRANSMIT_MAX_INSTANCES × SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES`
buffers of `SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN + 32` bytes each. **The shipped defaults
are already MCU-sized** — the sizing knobs:

| Knob | Where | Default | Tune |
|---|---|---|---|
| `SOMEIP_TRANSMIT_MAX_INSTANCES` | `someip-cfg.h` | **1** | one transmit instance is used; leave at 1 |
| `SOMEIP_TRANSMIT_MAX_QUEUE_ENTRIES` | `someip-cfg.h` | **8** | in-flight buffers; 4 is enough for the sync tools |
| `MAX_CONNECTIONS_CLIENT` | `someip-cfg.h` | **8** | endpoints tracked at once; raise for a large PLCA bus |
| `SOMEIP_TRANSMIT_MAX_PAYLOAD_LEN` | `someip.h` | **1440** | drop to **512** for a no-OTA build (OTA needs ≥1288 for 1200-byte WriteImage chunks) |

Measured static RAM (bss, arch-independent — `size` over `libsomeip/src/*.o`):

| Config | Core RAM | + `rcp.c` scratch (`s_rx`+`s_scratch` = 2 × payload) | Total |
|---|---|---|---|
| **Default (shipped)** — 1 × 8, 8 conn, 1440 | **≈ 15 kB** | ≈ 3.8 kB | **≈ 19 kB** |
| Smaller — 1 × 4, 2 conn, 1440 | ≈ 6.7 kB | ≈ 3.8 kB | ≈ 10 kB |
| No-OTA — 1 × 8, 8 conn, 512 | ≈ 8 kB | ≈ 1 kB | ≈ 9 kB |
| *(for reference)* old PC sizing — 4 × 64, 16 conn | ≈ 375 kB | ≈ 3.8 kB | — |

Code (text): ≈ 12 kB at `-Os` (≈ 28 kB unoptimised); add the plat layer's RX linear
buffer (≈ 1.6 kB RAM). All comfortably MCU-sized.

## Reference
- **LAN866x Endpoint User's Guide** — §4 Functional Description, §6 SOME/IP methods.
- **LAN8660 / 8661 / 8662 datasheets** — pin mapping, wake/sleep, T1S MAC-PHY.
