# Integration notes — RCP on the pure-C `libsomeip` stack

Hard-won facts for driving the LAN866x **RCP** service directly on the C
`libsomeip` stack (the `src/rcp.c` wrapper + a platform stub), bypassing the C++
`liblan866x` client. All verified live against real endpoints. Pair with
`CLAUDE.md` (constraints + gotcha summary) and `PORTING.md`.

## Wire basics
- Service id `0xFF10`, `clientId 0xaffe`, `interfaceVersion 1`.
- SD (OfferService) on UDP **30490** (multicast 224.0.0.1); method endpoint on UDP
  **6800**; the endpoint **sends responses from src port 49153** (so a capture
  filter `udp.port==6800` shows only requests — match responses by
  `ip.src==<ep> && someip.messagetype>=0x80`). Response msgtype `0x80`=OK, `0x81`=error.
- Video: RTP/RFC4175 on UDP **5001** (see "Displays / RTP" below).

## Method IDs (verified)
Authoritative source is the SOME/IP dissector CSV, **not** the manual prose
(which has typos, e.g. it claims OpenI2C=0x0100 but it is 0x1200).

| Group | Method | ID |
|------|--------|----|
| Device | Reboot / Identify / GetStatus | 0x1000 / 0x1001 / 0x1002 |
| Device | ReadDiagnosisData | 0x1003 |
| Update | StartUpdate / WriteImage / FinishUpdate | 0x1004 / 0x1005 / 0x1006 |
| Device | GetCurrentWallclock / Lock / Shutdown | 0x1007 / 0x1008 / 0x1009 |
| Network| GetNetworkStatus / WakeupNetwork | 0x1600 / 0x1601 |
| Network| StartTDMeasurement / GetTDMeasurementResult / StartPMATestMode | 0x1602 / 0x1603 / 0x1604 |
| Pins | ConfigDigitalPin / ReleaseDigitalPins | 0x1100 / 0x1105 |
| GPIO | OpenGpio / CloseGpio / SetGpio / GetGpio | 0x1300 / 0x1302 / 0x1330 / 0x1332 |
| I²C | OpenI2C / CloseI2C / WriteI2C | 0x1200 / 0x1202 / 0x1204 |
| I²C | **ReadI2C** / WriteAndReadI2C | **0x1220** / 0x1208 |
| SPI | OpenSpi / CloseSpi / WriteAndReadSpi | 0x1500 / 0x1502 / 0x1508 |
| SPI | **WriteAndReadSpiExtended** (compound) / WriteSpiFireAndForget | **0x1509** / 0x1510 |
| UART | OpenUart / CloseUart / WriteUart / ReadUart | 0x1400 / 0x1402 / 0x1404 / 0x1420 |
| ADC | OpenAdc / CloseAdc / ReadAdc | 0x1700 / 0x1702 / 0x1720 |
| PWM | OpenPwm / ClosePwm / WritePwm | 0x1800 / 0x1802 / 0x1804 |

`ReadI2C` (0x1220) = `UINT16 Handle, UINT16 DeviceAddress, UINT16 ReadDataLength`
→ reply `UINT32 ReadId + BLOB`. `WriteAndReadSpiExtended` (0x1509) packs N SPI
elements into one round-trip (needs config ≥ V1.3.2); great for a 2-axis ADC read.

## Request/response encoding
Mirror the C++ `LAN866XClientImpl`: `SOMEIP_Generator_Fill_Header`, then one
`Fill_UINT8/16/32`/`Fill_BLOB` per field with **tagDataId = 0,1,2,…** in field
order, then `SOMEIP_Generator_Update_Length`. Decode with `SOMEIP_Parser_Read_*`
in order; for a BLOB set the reply's `*Length` field to `sizeof(buffer)` before
reading (it is an in/out max-capacity).

**GOTCHA — Fill_* write ABSOLUTELY at `pBuf[0]`** (someip-gen.c) and only advance
`*pConsumed`. The caller MUST pass `&buf[consumed]` and `(MAX-consumed)` for each
field. Passing the base pointer for every field makes each field overwrite the
previous → `E_MALFORMED_MESSAGE` (0x09). This silently breaks all multi-field
methods (GPIO/I²C/SPI/ADC/PWM opens, flash). Symptom: "Open* failed"/"not
supported" that are really corrupted requests.

**retCode diagnosis:** `0x09 E_MALFORMED_MESSAGE` = your payload bytes are wrong;
`0x03 E_UNKNOWN_METHOD` = that method isn't in this firmware build (e.g. ADC on
the lighting build, or ReadDiagnosisData/SQI which needs a diagnosis-enabled
config); `0x05 E_NOT_REACHABLE` = peripheral not configured (or a benign code
some methods return even on success — see FinishUpdate below). Confirm on the
wire with a capture.

## GOTCHA — I²C scan must use ReadI2C (0x1220)
A `WriteAndReadI2C(WriteDataLength=0, ReadDataLength=1)` probe does **not** get a
clean address-NACK reported as an error on this firmware → absent addresses
return RT_OK = phantom "devices" (an i2cdetect-style scan shows dozens; the
run-to-run variation is host reply-drops, not bus noise — I²C is deterministic).
The original C++ scanner probed with `ReadI2C(addr, ReadDataLength=1)` and was
clean; a C port that substitutes `WriteAndReadI2C` regresses it. **Lesson: port
the EXACT original method, not a superficially similar one.**

## Callbacks run synchronously on the single execution strand
The toolset is single-thread. `someip-transmit.c` `ReceivedResponse` and
`CheckTimers` call `pub->callback(...)` inline, and the stub's
`SOMEIP_CB_EnterCriticialSection`/`Leave` are no-ops — so a buffer callback (the
sync `on_response` or the async dispatcher) runs **synchronously** from
`rcp_poll()` / `rcp_async_poll()`, on the same strand as the waiting code. No
locks, no `volatile`, no reentrancy hazard. Keep callbacks short and **do not**
re-enter `rcp_*` from inside one (it would reuse transmit buffers mid-iteration);
the state a callback touches is naturally serialized by the single strand.

## Transmit API
`SOMEIP_Transmit_Init(&port, rxCb, tag)` → `SOMETR_t*`; `SOMEIP_Transmit_GetBuffer`;
fill `tb->{ipV4Addr,udpPort,waitForSessionId,payloadLength,callback,fireAndForget}`;
`SOMEIP_Transmit_Send`; on the rx callback call
`SOMEIP_Transmit_ReceivedResponse(remoteIp, tr, sessionId, retCode, payload, len)`
which fires the buffer's callback **synchronously** (set a flag there). The app
pumps `SOMEIP_Transmit_CheckTimers` for timeouts. No retransmission exists; the
host retries by sending again (WriteId makes flash chunks idempotent).

`src/rcp.c` adds an **async** layer on top: `rcp_async_request()` +
`rcp_async_poll()` + `rcp_set_async_timeout_ms()`, with a session→callback slot
table dispatched synchronously from `rcp_async_poll()`. Param/reply helpers:
`rcp_enc_spi2`/`rcp_dec_spi2`, `rcp_enc_i2c_read`/`rcp_dec_i2c_read`.

## Stub IP convention
Platform `OnUdpRx` fills `pIp->sourceAddr` = local interface IP, `pIp->destinAddr`
= remote peer (endpoint). In the transmit rx callback use `pIp->destinAddr` as the
remote endpoint for ReceivedResponse; in the SD event callback
(`EV_CLIENT_SERVICE_AVAILABLE`) use `pIp->sourceAddr` + `pIp->port`. The transmit
rx callback must NOT call `SOMEIP_Client_DataReceived` — SD runs on a separate
internally-opened socket.

## GOTCHA — Windows Sleep granularity
A wait loop counting `Sleep(2)` iterations is wrong; Sleep rounds to ~15.6 ms, so
"N×2 ms" timeouts run ~7× too long. Use a real-time deadline:
`start=SOMEIP_CB_GetTimeMS(); while(!done && GetTimeMS()-start < timeoutMs)`.
`timeBeginPeriod(1)` (winmm) gives a 1 ms tick for snappy loops.

## Host throughput vs T1S link quality (important)
The 10BASE-T1S link is excellent — wire RTT ~1.7 ms, ~0% frame loss, PLCA active
(verified by capture: 507 requests → 507 responses). Sluggishness is **host-side**:
Windows drops RCP **replies** when requests are sent back-to-back (~60% loss at
0 ms gap vs ~1% at ~20 ms), worsened by multiple active NICs (SD multicast joins
all interfaces). Mitigation: pace RCP traffic, batch reads (compound SPI), read
sensors sequentially, disable NICs not on the endpoint subnet. `lan866x-diag`
measures this (paced probe = link verdict; `--gap 0` exposes the host drop).
`ReadDiagnosisData` (SQI/short-circuit) is not implemented in the lighting build
(rc=3) — needs a diagnosis-enabled config.

## Remote update / flash (run while the node is in the bootloader)
`Reboot(0x1000)` with a **BOM-prefixed** image name (`EF BB BF` + name + NUL),
names `"bootloader/app.bin"` / `"main/app.bin"`. Then per image:
`StartUpdate(name,IV)` (0x1004) → `WriteImage(name, writeId++, chunk)` (0x1005,
reply echoes WriteId) → `FinishUpdate(name, signature)` (0x1006). Update image
names `"main/app.bin"` / `"main/config.bin"` (also BOM-prefixed). Blobs come from
the MCHPKG (`*.bin` + `*.iv.bin` + `*.signature.bin`); the host only transports
them, the bootloader verifies the signature. WriteId makes a resent chunk
idempotent → safe to retry on a lossy link. **Reboot often resets before its
response arrives** → treat a lost ack as success and confirm via the uptime reset.
A config-only flash leaves the node in the bootloader if the app version differs
(COMO skew) → flash a matching **app+config pair**. **`FinishUpdate` may return a
benign `RT_NOT_REACHABLE` (5) even on success** → judge success by outcome (reboot
+ check the running version), as `flashpkg` does.

## Displays / RTP (clickdemo, video)
The lighting firmware renders a video stream onto the WS2812 displays; they are
**not** per-pixel addressable over RCP. Send **one 20×10 RTP/RFC4175 frame** to
UDP 5001: left half (cols 0–9) → display 1, right half (cols 10–19) → display 2.
Sending each display as a separate RFC4175 region packet makes the firmware split
each display top/bottom. Frame = RTP header (12 B, marker set) + RFC4175 extended
seq (2 B) + per-line headers (6 B each: length, lineNr, offset+continuation) +
RGB pixels. One 20×10 frame ≈ 674 B, fits in a single UDP datagram.
