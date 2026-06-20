# Porting to MCU32 (lwIP, bare-metal superloop or FreeRTOS)

The toolset is **single-thread** and structured so that for the embedded port you
write **exactly one file**: `src/plat_<target>.c`, the implementation of the narrow
`src/plat.h` interface. Everything above it — the tools, the RCP wrapper (`rcp.c/.h`),
the platform-neutral SOME/IP stub (`src/someip_stub.c`) and the SOME/IP core
(`libsomeip/src/*.c`) — stays **unchanged**.

> A ready-to-fill **sketch** of the lwIP/bare-metal port lives in
> [`src/plat_lwip.c.template`](src/plat_lwip.c.template) (not compiled). Rename it to
> `plat_lwip.c`, fill in the board hooks, and swap it for `plat_win.c` in CMake.

## Single-thread model — why there is no porting of locks/threads

The host prototype has **no application threads**. Received UDP datagrams are
delivered **synchronously** from `plat_udp_poll()` (called once per superloop tick by
`rcp_poll()`/`rcp_async_poll()` via `someip_service()`), on the same and only
execution strand as the waiting code. Consequences for the port:

- **No RTOS is required.** A plain `while(1)` superloop works (bare metal + lwIP RAW API).
- `SOMEIP_CB_EnterCriticialSection`/`Leave` and `SOMEIP_CB_NeedService` are **no-ops**;
  the `SOMEIP_CB_Sem*` set is never called by the core. None of this needs OS objects.
- No `volatile`/atomic/lock is needed for the shared request/reply state.
- An RTOS variant is still possible (lwIP `tcpip_thread` pumps RX; `plat_sleep_ms` →
  `vTaskDelay`); then `plat_udp_poll()` drains a queue the tcpip callback fills.

## What stays / what is swapped

| Layer | Windows prototype | MCU32 |
|---|---|---|
| Tools + RCP encoding (`*.c`, `rcp.c`) | C | **same** |
| SOME/IP core (`someip-client/-gen/-pars/-transmit/-timer.c`) | C | **same** |
| SOME/IP platform stub (`SOMEIP_CB_*`) | `src/someip_stub.c` (platform-neutral) | **same** |
| **Platform layer (`plat.h`)** | `src/plat_win.c` (Winsock, non-blocking) | **`src/plat_lwip.c` (new — the only file you write)** |
| Time base (`plat_now_ms`) | `GetTickCount()` | `sys_now()` / `xTaskGetTickCount()` |
| Config (`stub/someip-cfg.h`) | PC sizes | **tune smaller** |
| T1S connection | USB-Ethernet bridge | **LAN8650/51 MAC-PHY via OA-SPI** (or LAN8670 PHY via RMII) + lwIP |

## The `plat.h` porting surface — three responsibilities

| Function(s) | Task | lwIP / bare-metal implementation |
|---|---|---|
| `plat_now_ms` | millisecond time base | `sys_now()` (or a systick counter) |
| `plat_udp_open` / `_send` / `_close` | non-blocking UDP socket | `udp_new`/`udp_bind`/`udp_recv` ; `udp_sendto` ; `udp_remove` |
| `plat_udp_poll` | drain RX, dispatch rx cb **synchronously** | pump the netif (`ethernetif_input`) + `sys_check_timeouts()`; lwIP calls the `udp_recv` trampoline inline |
| `plat_udp_join_multicast` | join SD group `224.0.0.1` | `igmp_joingroup` (needs `LWIP_IGMP=1`) |
| `plat_net_enum_ifaces` | local IPs + masks (subnet match) | iterate `netif_list` |
| `plat_sleep_ms` / `plat_yield` | wait between ticks | spin while pumping the stack (bare metal) / `vTaskDelay` (RTOS) |

## Under the hood: the `SOMEIP_CB_*` callbacks (you do NOT reimplement these)

The libsomeip core calls a fixed set of **platform callbacks**. They are implemented
**once, platform-neutrally**, in `src/someip_stub.c` on top of `plat.h` — you do **not**
rewrite them per target. The table shows how the stub maps each one (most collapse to
`plat_*` or to a no-op thanks to the single-thread model):

| Callback | Stub maps it to | Note |
|---|---|---|
| `SOMEIP_CB_OpenSocket` | `plat_udp_open` | registers the synchronous RX trampoline |
| `SOMEIP_CB_SendUdp` | `plat_udp_send` (+ `plat_udp_join_multicast` for `224.0.0.1`) | |
| `SOMEIP_CB_GetLocalIpAddr` | cached `plat_net_enum_ifaces` table | subnet match |
| `SOMEIP_CB_GetTimeMS` | `plat_now_ms` | |
| `SOMEIP_CB_EnterCriticialSection` / `Leave` | **no-op** | single-thread, no contention |
| `SOMEIP_CB_NeedService` | **no-op** | the superloop pumps every tick |
| `SOMEIP_CB_SemInit/Wait/Post/Destroy` | trivial stubs | the core never calls them |
| `SOMEIP_CB_ProvideBuffer`/`Calloc`/`Free` | `malloc`/`calloc`/`free` | swap for a static pool if no heap |
| `SOMEIP_CB_Log` | `printf` | retarget to UART/ITM |

> So the MCU32 port is essentially: **implement the six `plat.h` functions** in
> `plat_lwip.c` + define `MULTICAST_IP[]={224,0,0,1}` in the app. `someip_stub.c`,
> `rcp.c` and the tools stay unchanged. The RX dispatch lives in `rcp.c`
> (`on_data_received`, ported 1:1 from `LAN866XClientImpl::OnDataReceived`).

## Concrete steps

1. **Write `plat_lwip.c`** from [`src/plat_lwip.c.template`](src/plat_lwip.c.template):
   the six `plat.h` functions over the lwIP **RAW API** (`LWIP_SOCKET=0`, less RAM) +
   `LWIP_IGMP=1` for the multicast join. Swap `plat_win.c` → `plat_lwip.c` in CMake.
2. **Time:** `plat_now_ms()` → `sys_now()` (bare metal) or `xTaskGetTickCount()*portTICK_PERIOD_MS`
   (RTOS). It must increment monotonically; 32-bit wrap is fine.
3. **Pump model (single-thread default):**
   ```
   while (1) { app_render(); rcp_async_poll(); }   // plat_udp_poll() runs inside
   ```
   `plat_udp_poll()` pushes RX frames into lwIP, whose `udp_recv` callback fires
   synchronously and dispatches to the stub. *(RTOS variant: a `tcpip_thread` pumps RX
   and `plat_udp_poll()` drains a queue instead.)*
4. **Startup order:** PHY → lwIP netif UP → `rcp_init()` → service discovery → app.
   *Common mistake:* SOME/IP starts before a valid IP → discovery fails.
5. **Memory:** no `malloc` in the hot path if you can avoid it; for a no-heap target,
   back `SOMEIP_CB_ProvideBuffer`/`Calloc` with a static pool; lwIP `MEM_LIBC_MALLOC=0`.

## Footprint (per the roadmap overview)
SOME/IP core **ROM ≈ 28 kB, RAM ≈ 300 B** → fits on an MCU32 incl. FreeRTOS.

## Recommended base: the official pure-C client (`lan866x_c/`)
Microchip ships a **generated, callback-based pure-C client** — `lan866x_c/lan866x_client.c/.h` + `lan866x_common.h` — in the multi-language library repo (documented in the *LAN866x Library Integration Manual*). This is exactly the "pure C client API variant" the embedded port wants, and it should be preferred over the hand-rolled `src/rcp.c` stand-in here.

It shares the **same data types** as this package (`include/lan866x_common.h`): `ReturnCode_t` (`RT_OK=0`, `RT_TIMEOUT=3`, `RT_DEVICE_NOT_AVAILABLE=4`, `RT_NOT_REACHABLE=5`) and the `*Var_t`/`*Reply_t` request/reply structs. Its integration surface is small:

| API | Purpose |
|---|---|
| `LAN866X_Client_Init(true)` | init stack + start service discovery (SD on UDP 30490) |
| `LAN866X_CB_Available(node, instId, avail, ip, ipLen, port)` | device appear/disappear callback (you implement) |
| `LAN866X_EventCB_OnGpioEvents / OnUartReceive / OnAdcEvent / OnTDMeasurementCompleted` | async event callbacks (you implement) |
| `LAN866X_VerbPeripheral(node, &var, &reply) -> ReturnCode_t` | every RPC (e.g. `LAN866X_OpenI2C`, `LAN866X_ReadAdc`, `LAN866X_WritePwm`) |

`RT_NOT_REACHABLE` is **not** an error — it means that peripheral is not enabled in the node's COMO configuration; multi-node code should skip such nodes.

**Porting plan with the official client:** vendor `lan866x_c/` + its `someip/` submodule, then provide only the platform layer — the `SOMEIP_CB_*` table below (lwIP + FreeRTOS) — exactly as for this package. The C++ `liblan866x` (`LAN866XClient`) is **not** intended for embedded.

## Reference
- **LAN866x Library Integration Manual** — the official C client API, build steps, and 8 example programs (device_discovery, device_info, led_blink, adc_demo, i2c_proximity, pwm, spi_thumbstick, uart_terminal).
- **LAN866x Endpoint User's Guide** (§4 Functional Description, §6 SOME/IP methods) and the LAN8660/8661/8662 datasheets — architecture (RTOS/lwIP), pin mapping & wake/sleep.
