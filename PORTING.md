# Porting to MCU32 (lwIP + FreeRTOS)

The prototype is structured so that for the embedded port **only the platform layer** is swapped. App logic (`main.c`), the RCP wrapper (`rcp.c/.h`) and the SOME/IP core (`libsomeip/src/*.c`) stay **unchanged**.

## What stays / what is swapped

| Layer | Windows prototype | MCU32 |
|---|---|---|
| App + RCP encoding (`main.c`, `rcp.c`) | C | **same** |
| SOME/IP core (`someip-client/-gen/-pars/-transmit/-timer.c`) | C | **same** |
| UDP transport | `stub/windows-udp-handler.c` (Winsock) | **lwIP handler (new)** |
| Time base (`someip-timer`) | system clock | **`xTaskGetTickCount()`** |
| Config (`stub/someip-cfg.h`) | PC sizes | **tune smaller** |
| T1S connection | USB-Ethernet bridge | **LAN8650/51 MAC-PHY via OA-SPI** (or LAN8670 PHY via RMII) + lwIP |

## The real porting boundary: the `SOMEIP_CB_*` callbacks

The libsomeip core calls a fixed set of **platform callbacks**. On Windows these are provided by `libsomeip/stub/someip-stub.cpp` (+ `windows-udp-handler.c`, Win32 threads). **You reimplement exactly these functions** for MCU32 (lwIP + FreeRTOS) — the rest stays:

| Callback | Task | MCU32 implementation |
|---|---|---|
| `SOMEIP_CB_OpenSocket` | open UDP socket, register RX callback | lwIP `udp_new`/`udp_bind`/`udp_recv` |
| `SOMEIP_CB_SendUdp` | send UDP (incl. multicast join for `224.0.0.1`) | `udp_sendto` + `igmp_joingroup` |
| `SOMEIP_CB_GetLocalIpAddr` | local IP for the target subnet | from `netif` |
| `SOMEIP_CB_EnterCriticialSection` / `Leave` | mutual exclusion | FreeRTOS mutex / `taskENTER_CRITICAL` |
| `SOMEIP_CB_SemInit/Wait/Post/Destroy` | semaphores | FreeRTOS `xSemaphore*` |
| `SOMEIP_CB_ProvideBuffer` | memory for TX | static pool / `pbuf` |
| `SOMEIP_CB_NeedService` | "CheckTimers needed soon" | notify task |
| `SOMEIP_CB_Log` | logging | UART/ITM |
| RX dispatch (`on_data_received`) | parse + `SOMEIP_Client_DataReceived` / `SOMEIP_Transmit_ReceivedResponse` | adopt from `LAN866XClientImpl::OnDataReceived` |

> So the MCU32 port is essentially: **implement this table** + define `MULTICAST_IP[]={224,0,0,1}`. App (`main.c`) and RCP wrapper (`rcp.c`) stay unchanged.

## Concrete steps

1. **Replace the UDP handler:** `windows-udp-handler.c` with an lwIP variant. Implement the platform functions the core expects (`SomeIPSocket_Init`, send, RX → `SOMEIP_Client_DataReceived(...)`). Prefer the lwIP **RAW API** (`LWIP_SOCKET=0`, less RAM).
2. **Time:** map `someip_get_time_ms()` to the FreeRTOS tick:
   ```c
   uint32_t someip_get_time_ms(void){ return xTaskGetTickCount()*portTICK_PERIOD_MS; }
   ```
   No `HAL_Delay()` / blocking delays in the SOME/IP task.
3. **Task model:**
   ```
   Ethernet IRQ → lwIP (tcpip_thread) → queue → SOME/IP task → app task(s)
   ```
   Priorities: lwIP **high**, SOME/IP **medium**, app **medium/low**.
4. **Startup order:** PHY → lwIP netif UP → SOME/IP init → service discovery → app.
   *Common mistake:* SOME/IP starts before a valid IP → discovery fails.
5. **Memory:** no `malloc` in the hot path; FreeRTOS `heap_4`/`heap_5`; static queues/buffers; lwIP `MEM_LIBC_MALLOC=0`.

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
