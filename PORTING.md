# Portierung auf STM32 (lwIP + FreeRTOS)

Der Prototyp ist so geschnitten, dass für den Embedded-Port **nur die Plattformschicht** getauscht wird. App-Logik (`main.c`), RCP-Wrapper (`rcp.c/.h`) und der SOME/IP-Kern (`libsomeip/src/*.c`) bleiben **unverändert**.

## Was bleibt / was getauscht wird

| Schicht | Windows-Prototyp | STM32 |
|---|---|---|
| App + RCP-Encoding (`main.c`, `rcp.c`) | C | **gleich** |
| SOME/IP-Kern (`someip-client/-gen/-pars/-transmit/-timer.c`) | C | **gleich** |
| UDP-Transport | `stub/windows-udp-handler.c` (Winsock) | **lwIP-Handler (neu)** |
| Zeitbasis (`someip-timer`) | Systemuhr | **`xTaskGetTickCount()`** |
| Konfig (`stub/someip-cfg.h`) | PC-Größen | **kleiner tunen** |
| T1S-Anbindung | USB-Ethernet-Bridge | **LAN8650/51 MAC-PHY via OA-SPI** (oder LAN8670 PHY via RMII) + lwIP |

## Die echte Portier-Grenze: die `SOMEIP_CB_*`-Callbacks

Der libsomeip-Kern ruft eine feste Menge **Plattform-Callbacks** auf. Unter Windows liefert sie `libsomeip/stub/someip-stub.cpp` (+ `windows-udp-handler.c`, Win32-Threads). **Genau diese Funktionen** implementierst du für STM32 neu (lwIP + FreeRTOS) — der Rest bleibt:

| Callback | Aufgabe | STM32-Umsetzung |
|---|---|---|
| `SOMEIP_CB_OpenSocket` | UDP-Socket öffnen, RX-Callback registrieren | lwIP `udp_new`/`udp_bind`/`udp_recv` |
| `SOMEIP_CB_SendUdp` | UDP senden (inkl. Multicast-Join bei `224.0.0.1`) | `udp_sendto` + `igmp_joingroup` |
| `SOMEIP_CB_GetLocalIpAddr` | lokale IP zum Ziel-Subnetz | aus `netif` |
| `SOMEIP_CB_EnterCriticialSection` / `Leave` | Gegenseitiger Ausschluss | FreeRTOS Mutex / `taskENTER_CRITICAL` |
| `SOMEIP_CB_SemInit/Wait/Post/Destroy` | Semaphore | FreeRTOS `xSemaphore*` |
| `SOMEIP_CB_ProvideBuffer` | Speicher für TX | statischer Pool / `pbuf` |
| `SOMEIP_CB_NeedService` | „bald CheckTimers nötig" | Task notifizieren |
| `SOMEIP_CB_Log` | Logging | UART/ITM |
| RX-Dispatch (`on_data_received`) | Parsen + `SOMEIP_Client_DataReceived` / `SOMEIP_Transmit_ReceivedResponse` | aus `LAN866XClientImpl::OnDataReceived` übernehmen |

> Damit ist der STM32-Port im Kern: **diese Tabelle umsetzen** + `MULTICAST_IP[]={224,0,0,1}` definieren. App (`main.c`) und RCP-Wrapper (`rcp.c`) bleiben unverändert.

## Konkrete Schritte

1. **UDP-Handler ersetzen:** `windows-udp-handler.c` durch eine lwIP-Variante. Implementiere die vom Kern erwarteten Plattform-Funktionen (`SomeIPSocket_Init`, Senden, RX → `SOMEIP_Client_DataReceived(...)`). lwIP **RAW-API** bevorzugen (`LWIP_SOCKET=0`, weniger RAM).
2. **Zeit:** `someip_get_time_ms()` auf FreeRTOS-Tick mappen:
   ```c
   uint32_t someip_get_time_ms(void){ return xTaskGetTickCount()*portTICK_PERIOD_MS; }
   ```
   Keine `HAL_Delay()` / blockierenden Delays im SOME/IP-Task.
3. **Task-Modell:**
   ```
   Ethernet-IRQ → lwIP (tcpip_thread) → Queue → SOME/IP-Task → App-Task(s)
   ```
   Prioritäten: lwIP **hoch**, SOME/IP **mittel**, App **mittel/niedrig**.
4. **Startup-Reihenfolge:** PHY → lwIP netif UP → SOME/IP-Init → Service Discovery → App.
   *Typischer Fehler:* SOME/IP startet vor gültiger IP → Discovery schlägt fehl.
5. **Speicher:** kein `malloc` im Hot-Path; FreeRTOS `heap_4`/`heap_5`; statische Queues/Buffer; lwIP `MEM_LIBC_MALLOC=0`.

## Footprint (laut Roadmap-Übersicht)
SOME/IP-Kern **ROM ≈ 28 kB, RAM ≈ 300 B** → passt auf STM32 inkl. FreeRTOS.

## Hinweis zum C++-Wrapper
`liblan866x` (C++ `LAN866XClient`) ist **nicht** für Embedded vorgesehen. Für STM32 wird – wie hier – direkt auf `libsomeip` (C) aufgesetzt. Falls Microchip eine **reine C-Client-API-Variante** von libLAN866x bereitstellt (Übersicht nennt „C … MCUs"), wäre das die ideale Basis – erfragen.

## Referenz
Architektur (RTOS/lwIP), Pin-Belegung & Wake/Sleep: **LAN866x Endpoint User's Guide** (§4 Functional Description, §6 SOME/IP-Methoden) sowie die Datenblätter LAN8660/8661/8662.
