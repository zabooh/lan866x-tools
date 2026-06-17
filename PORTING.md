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

## Note on the C++ wrapper
`liblan866x` (C++ `LAN866XClient`) is **not** intended for embedded. For MCU32 you build directly on `libsomeip` (C) – as done here. If Microchip provides a **pure C client API variant** of libLAN866x (the overview mentions "C … MCUs"), that would be the ideal base – worth asking for.

## Reference
Architecture (RTOS/lwIP), pin mapping & wake/sleep: **LAN866x Endpoint User's Guide** (§4 Functional Description, §6 SOME/IP methods) and the LAN8660/8661/8662 datasheets.
