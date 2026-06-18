# LAN866x Tools – SOME/IP console tools (RCP), pure C

> 🚀 **New here? Start with [howto_demonstrate.md](howto_demonstrate.md)** — a complete,
> illustrated walkthrough to set up and show the demo end-to-end (boards, wiring P↔P/N↔N,
> static IP, bus scan, flashing, Click demo).
>
> ✅ **No build required to run:** all tools are **already built** under
> [`release/`](release/) (statically linked `.exe`, plus the demo firmware package). Just
> run them from there. Building (below) is optional — only needed if you change the source.

A **pure-C** host that remote-controls LAN866x endpoints via the **Remote Control Protocol (RCP)** on top of the **C SOME/IP stack (`libsomeip`)**. Access is via the **T1S-USB adapter** (EVB-LAN8670-USB) as an Ethernet bridge.

> 🟦 **100 % C — no C++.** Every tool, the RCP wrapper and the platform layer are C; they link **without `libstdc++`**. The same code base ports to a 32-bit MCU (lwIP/FreeRTOS) by swapping a single file — see [chapter 9](#9-porting-to-mcu32).
>
> 📦 **Self-contained:** this directory contains **all** sources required to build. Unpack → build, no external paths.

## Table of contents

1. [Overview](#1-overview)
2. [System requirements](#2-system-requirements)
   - 2.1 [CMake 3.10 or newer](#21-cmake-310-or-newer)
   - 2.2 [A C compiler](#22-a-c-compiler)
   - 2.3 [Hardware and driver](#23-hardware-and-driver)
   - 2.4 [Network](#24-network)
3. [How to compile](#3-how-to-compile)
   - 3.1 [Quick path – batch script](#31-quick-path--batch-script)
   - 3.2 [Manual – CMake on the command line](#32-manual--cmake-on-the-command-line)
4. [Running and output](#4-running-and-output)
   - 4.1 [Discovery (full status)](#41-discovery-full-status)
   - 4.2 [I2C bus scanner](#42-i2c-bus-scanner)
   - 4.3 [GPIO set/read](#43-gpio-setread)
   - 4.4 [SPI transfer (full-duplex)](#44-spi-transfer-full-duplex)
   - 4.5 [ADC read](#45-adc-read)
   - 4.6 [PWM output](#46-pwm-output)
   - 4.7 [DNCP monitor (passive)](#47-dncp-monitor-passive)
   - 4.8 [DNCP discovery (active, read-only)](#48-dncp-discovery-active-read-only)
   - 4.9 [Boot, flash & diagnostics tools](#49-boot-flash--diagnostics-tools)
5. [How does discovery work?](#5-how-does-discovery-work)
6. [Project structure](#6-project-structure)
7. [Example pin mapping (LAN8660)](#7-example-pin-mapping-lan8660)
8. [RCP method IDs](#8-rcp-method-ids)
9. [Porting to MCU32](#9-porting-to-mcu32)

> 📖 **Hardware setup & full per-tool reference:** see **[TOOLS.md](TOOLS.md)** —
> board description (what plugs where), jumper/DIP ASCII map with photos, and a
> detailed page for every tool. **Demo walkthrough:** [howto_demonstrate.md](howto_demonstrate.md).

---

## 1. Overview

**Purpose:** a Windows host prototype that doubles as a **1:1 template for a 32-bit embedded device** (MCU32 + lwIP + FreeRTOS). Everything is plain C.

The tools (all build to `lan866x-<name>.exe`):

| Tool | Purpose |
|---|---|
| **`lan866x-discovery`** | list reachable endpoints + type + full `GetStatus` / `GetNetworkStatus` |
| **`lan866x-i2cscan`** | scan an endpoint's I2C bus (like `i2cdetect`) |
| **`lan866x-gpio`** | set / read a GPIO pin |
| **`lan866x-spi`** | SPI transfer (full-duplex) |
| **`lan866x-adc`** | read the on-chip ADC (analog input or internal temperature) |
| **`lan866x-pwm`** | drive a PWM output on a digital pin |
| **`lan866x-boot`** | reboot between main app and bootloader (non-destructive) |
| **`lan866x-flashimg`** | write ONE signed/encrypted image via the bootloader |
| **`lan866x-flashpkg`** | update an endpoint straight from an **MCHPKG** package |
| **`lan866x-diag`** | read & interpret **T1S link quality** (read-only) |
| **`lan866x-clickdemo`** | interactive MikroE **Click** demo (Thumbstick + Proximity → 2× RGB) |
| **`lan866x-dncpmon`** | passive **DNCP** monitor (standalone, not SOME/IP) |
| **`lan866x-dncpdisc`** | active **DNCP** discovery (Registry broadcast → collect Announces, read-only) |

The SOME/IP tools use `src/rcp.c` (RCP over `libsomeip`) + the C platform stub `src/someip_stub_win.c`; `lan866x-flashpkg` additionally links a bundled ZIP reader (`third-party/minizip`). The two DNCP tools are standalone (Winsock only — DNCP is not SOME/IP).

> 📖 Every tool is documented in detail — with all options, examples and the board
> setup it needs — in **[TOOLS.md](TOOLS.md)**.

---

## 2. System requirements

To **build**, the machine needs:

### 2.1 CMake 3.10 or newer
*(tested with CMake 4.1)*
- Download: <https://cmake.org/download/> → "Windows x64 Installer".
- During installation choose **"Add CMake to the system PATH"**.
- Check: `cmake --version`

### 2.2 A C compiler
*(one of the two options)*

**Option A – MinGW-w64 (GCC)** — recommended for the command line *(tested GCC 16.1)*
- Easiest source: **WinLibs** <https://winlibs.com/> (UCRT variant) – unzip and add the `…\mingw64\bin` folder to **PATH**.
- Alternatively **MSYS2** <https://www.msys2.org/>: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake`
- Check: `gcc --version` and `mingw32-make --version`

**Option B – Visual Studio 2022**
- Installer: <https://visualstudio.microsoft.com/> → workload **"Desktop development with C++"** (also provides the MSVC **C** compiler and CMake). No C++ runtime is linked — the sources are C.

### 2.3 Hardware and driver
*(only needed to run, not to build)*
- **EVB-LAN8670-USB** (T1S-USB adapter) – shows up on Windows as a normal **Ethernet NIC**.
- Install the **Windows driver** (`EVB-LAN8670-USB_Drv_Setup.exe`, from the LAN866x Remote Demo package / MicrochipDirect **EV08L38A**).
- LAN866x endpoint(s) on the T1S bus, **bus terminated**, PoDL power if applicable.

### 2.4 Network
*(only needed to run)*
- Give the USB-T1S NIC a **static IP** in the endpoint subnet: **`192.168.0.100/24`**. Endpoints = `192.168.0.<NodeID>`.
- **SOME/IP-SD** uses multicast `224.0.0.1` (UDP 30490). Allow the tools through the Windows **firewall**.

---

## 3. How to compile

> ℹ️ **Optional.** Pre-built, statically linked executables for all tools already ship in
> [`release/`](release/) — you can run the demo straight from there without compiling
> anything. Build only if you modify the source.

### 3.1 Quick path – batch script
The package contains **`build.bat`** (chooses the compiler automatically: MinGW, else VS2022):

```bat
build.bat            REM build (compiler chosen automatically)
build.bat mingw      REM force MinGW-w64 (GCC)
build.bat vs         REM force Visual Studio 2022
build.bat clean      REM delete build folder
```
Result: `out\lan866x-discovery.exe` (MinGW) or `out\Release\lan866x-discovery.exe` (VS), and all tools copied to `release\`.

### 3.2 Manual – CMake on the command line
**Always specify the generator (`-G …`) explicitly** – otherwise CMake aborts with "CMAKE_C_COMPILER not set".

**MinGW-w64 (GCC):**
```bat
cmake -G "MinGW Makefiles" -B out
cmake --build out
```
**Visual Studio 2022 (MSVC):**
```bat
cmake -G "Visual Studio 17 2022" -A x64 -B out
cmake --build out --config Release
```

The `out/` folder is pure build output and can be deleted at any time (remove before packaging).

---

## 4. Running and output

### 4.1 Discovery (full status)

```bat
out\lan866x-discovery.exe
```

Per endpoint the tool prints the full status — via `GetStatus (0x1002)` + `GetNetworkStatus (0x1600)`. Example (verified live, pure C):
```
Devices available = 2

========================================================
Endpoint #0  -  192.168.0.101:6800  (instance 0x0001, available=1)
========================================================
  Uptime:             2h 38m 18s
  Application:        main/app.bin
  Chip Identifier:    LAN8662B   -> Audio Endpoint
  Main Version:       LAN8662-main_V1.3.0-54
  Root Version:       LAN866x-root_V1.2.0-53
  Bootloader Version: LAN866x-bootloader_V1.3.0-54
  COMO Version:       0x00020006
  Service Version:    0x00010600
  Keys Version:       V0.0.1
  StartupInformation: 0x0000000000000219 (Security Mode 1)
  MAC:                8C:71:12:2B:98:7F
  IPv4:               192.168.0.101
  Endpoint Status:    Link-Up
  OASPI Status:       Disabled
  Arbitration:        PLCA no fallback
  PLCA Node Id:       1
```

**Nothing found?** Check: driver installed · NIC IP `192.168.0.x` set · bus terminated · endpoints powered · firewall allowed.

### 4.2 I2C bus scanner
```bat
out\lan866x-i2cscan.exe                 REM first endpoint, SDA=PA08 SCL=PA09, 400 kHz
out\lan866x-i2cscan.exe --ip 192.168.0.54
out\lan866x-i2cscan.exe --ep 1 --sda 8 --scl 9 --speed 1
```
Probes 0x08..0x77 with a 1-byte read and prints an `i2cdetect` grid (uses a short ~150 ms per-probe timeout since absent addresses never reply). Example (Proximity 3 Click on the demo board):
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- --

1 device(s) found on the I2C bus.
```
> Pins must match the board configuration (`--sda`/`--scl`, PA number 0–15). The tool releases SDA/SCL before `OpenI2C` automatically (`ReleaseDigitalPins`). If I2C is not configured on that endpoint, `OpenI2C` returns `RT_NOT_REACHABLE` ("OpenI2C failed").

### 4.3 GPIO set/read
```bat
out\lan866x-gpio.exe --pin 2 --set 1     REM PA02 as output, high
out\lan866x-gpio.exe --pin 2 --get       REM PA02 as input, read
out\lan866x-gpio.exe --ip 192.168.0.54 --pin 6 --set 0
```

### 4.4 SPI transfer (full-duplex)
```bat
out\lan866x-spi.exe --tx 9F0000          REM send 3 bytes, read MISO at the same time
out\lan866x-spi.exe --tx AA55 --mode 0 --speed 1000000
out\lan866x-spi.exe --miso 12 --sck 13 --cs 14 --mosi 15 --tx 0102
```
Output: `TX: …` / `RX: …`. Default pins MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15. Pins are released before `OpenSpi`.

### 4.5 ADC read
```bat
out\lan866x-adc.exe                      REM single analog read, 3V3 reference
out\lan866x-adc.exe --temp               REM internal temperature sensor
out\lan866x-adc.exe --vref 1             REM use the 1V1 reference
out\lan866x-adc.exe --count 10 --interval 200
```
Reads the on-chip 12-bit ADC (0..4095) and prints the scaled voltage, e.g. `raw=2048  =  1.650 V`. Channel `0` = analog input, `1` = internal temperature; reference `0` = 3V3, `1` = 1V1.

### 4.6 PWM output
```bat
out\lan866x-pwm.exe --pin 6 --freq 1000 --duty 50    REM 1 kHz, 50% on PA06
out\lan866x-pwm.exe --pin 6 --period-ns 20000000 --duty 7.5   REM servo, 1.5 ms pulse
out\lan866x-pwm.exe --pin 6 --duty 0                 REM stop output (0%)
out\lan866x-pwm.exe --pin 7 --freq 500 --duty 25 --hold 5
```
Opens a PWM channel on a digital pin. Duty cycle wire encoding: `0 = 0% .. 2^31 = 100%` (the tool takes percent and converts). By default the signal is **left running** on the endpoint after the tool exits (the handle lives on the device); `--hold <s>` stops it again after N seconds. The pin is released before `OpenPwm`.

### 4.7 DNCP monitor (passive)
```bat
out\lan866x-dncpmon.exe                REM listen forever (Ctrl+C to stop)
out\lan866x-dncpmon.exe --timeout 30   REM stop after 30 s without packets
```
Decodes **DNCP** packets (Dynamic Node Configuration Protocol) on **UDP 65526/65527** — Announce/Registry with MAC, device id, IPv4/IPv6, state (Unconfigured/Configured) and PLCA ids. Standalone (Winsock only), **not** part of SOME/IP.
> Purely **passive**: only shows DNCP traffic actually present on the bus. To trigger actively, see `lan866x-dncpdisc`.

### 4.8 DNCP discovery (active, read-only)
```bat
out\lan866x-dncpdisc.exe                       REM 3 rounds, channel 11
out\lan866x-dncpdisc.exe --channel 11 --rounds 5 --timeout 4
```
Acts as a **temporary DNCP server** (per AN1891): broadcasts an **empty Registry** to `224.0.0.1:65527`; nodes that do not find themselves in it send an **Announce** to `224.0.0.1:65526`. Per node **all Announce fields** are decoded: MAC, vendor device id, **IPv4 + IPv6**, state, persistency, BurstFramesPerTO, protocol version and all PLCA node ids.
> **Read-only** — assigns no PLCA ids/IPs, persists nothing. EnumChannel = default (11). **Use only when no other DNCP server is active.** Verified live (a LAN8662 responded).

### 4.9 Boot, flash & diagnostics tools

Four further tools cover firmware management, link diagnostics and the Click demo.
They are documented in full — with every option and the board setup they need — in
**[TOOLS.md](TOOLS.md)**:

| Tool | One-liner | TOOLS.md |
|---|---|---|
| `lan866x-boot` | reboot between main app ↔ bootloader (non-destructive) | [§4.9](TOOLS.md#49-lan866x-boot) |
| `lan866x-flashimg` | write one signed image via the bootloader (writes flash) | [§4.10](TOOLS.md#410-lan866x-flashimg) |
| `lan866x-flashpkg` | update an endpoint from an `.mchpkg` package (writes flash) | [§4.11](TOOLS.md#411-lan866x-flashpkg) |
| `lan866x-diag` | read & interpret T1S link quality (read-only) | [§4.3](TOOLS.md#43-lan866x-diag) |
| `lan866x-clickdemo` | Thumbstick + Proximity → 2× RGB Click panels (RTP) | [§4.12](TOOLS.md#412-lan866x-clickdemo) |

```bat
out\lan866x-diag.exe --ip 192.168.0.54
out\lan866x-boot.exe --to bootloader
out\lan866x-flashpkg.exe LAN8661-ws2812_V1.3.2_RELEASE_display1.mchpkg --ip 192.168.0.54
out\lan866x-clickdemo.exe --ip 192.168.0.54
```

> `lan866x-flashimg`/`-flashpkg` **write flash** (recoverable from the bootloader);
> `lan866x-clickdemo` needs the Click boards seated and the DIP switches set as in
> [TOOLS.md §2.4/§2.5](TOOLS.md#24-click-slots--what-plugs-where).

---

## 5. How does discovery work?

The tools do **not** know the endpoint IPs in advance – they learn them at runtime via **SOME/IP Service Discovery (SD)**:

1. **Join multicast:** the platform stub joins the SD group **`224.0.0.1`** on each PC interface (the "Joined Multicast group" lines at startup). SD runs on **UDP port 30490**.
2. **Request the service:** `rcp_init()` calls `SOMEIP_Client_AddService(0xFF10, requested=true)`; the stack sends `FindService` and also receives the endpoints' periodic `OfferService`.
3. **Endpoints respond:** every endpoint that offers `0xFF10` sends an **`OfferService`** containing **its own IP/port** (method endpoint = **UDP 6800**, e.g. `192.168.0.101:6800`). The SD event callback (`on_event`) stores it.
4. **Build the list:** `rcp_get_endpoints()` returns the collected list. **The IP comes from the endpoint, not the tool.**

```
PC  --FindService(0xFF10)-->  224.0.0.1:30490  (multicast)
EP1 --OfferService: I am 192.168.0.101:6800 -->  PC
EP2 --OfferService: I am 192.168.0.102:6800 -->  PC
```

**Target endpoint:** default is `[0]` (first found). Select with `--ip <addr>` or `--ep <index>`.

> Multiple PC interfaces: the stub joins on all of them; responses arrive only over the **T1S interface** (`192.168.0.x`). Its NIC must have an IP in the endpoint subnet (chapter 2.4).

---

## 6. Project structure

```
lan866x-tools/
├── build.bat            Windows build script (chapter 3)
├── CMakeLists.txt       C-only build: rcpcore lib + tool executables
├── discovery.c          list endpoints + full GetStatus/GetNetworkStatus
├── i2cscan.c            I2C bus scanner
├── gpio.c               GPIO set/read
├── spi.c                SPI transfer
├── adc.c                ADC read (analog / temperature)
├── pwm.c                PWM output
├── boot.c               reboot main app ↔ bootloader (non-destructive)
├── flashimg.c           write one signed image via the bootloader
├── flashpkg.c           update from an MCHPKG package (uses minizip)
├── diag.c               T1S link-quality diagnostics
├── clickdemo.c          MikroE Click demo (Thumbstick + Proximity → 2× RGB)
├── dncpmon.c            passive DNCP monitor (standalone, Winsock)
├── dncpdisc.c           active DNCP discovery (standalone, Winsock)
├── src/
│   ├── rcp.h / rcp.c    RCP over libsomeip — typed methods, 1:1 with the C++ client
│   ├── someip_stub_win.c  C Windows platform stub (the file you swap for MCU32)
│   └── tool_common.h    tiny shared discover-and-select helper
├── include/             lan866x_common.h (RCP request/reply structs, used by rcp.c)
├── libepmicrochip/
│   └── libsomeip/       the C SOME/IP stack (src/*.c) + windows-udp-handler.c
├── third-party/
│   └── minizip/         bundled ZIP reader (only lan866x-flashpkg uses it)
├── docs/img/            board photos used by TOOLS.md
├── README.md
├── TOOLS.md             board guide + full per-tool reference
└── PORTING.md           MCU32 port (lwIP/FreeRTOS)
```
> `libepmicrochip/` also still contains Microchip's C++ vendor sources (`liblan866x`, `librtp`, `someip-stub.cpp`). They are **not built** — this toolset uses only the C `libsomeip` core. They can be deleted for a strictly C-only tree.

The build produces a shared static lib **`rcpcore`** (rcp.c + someip_stub_win.c + windows-udp-handler.c + libsomeip/src/\*.c) that each SOME/IP tool links; the DNCP tools link only Winsock.

---

## 7. Example pin mapping (LAN8660)

*(example configuration of a control endpoint with 1× UART / I2C / SPI each + GPIOs)*

| Function | SERCOM | Pins |
|---|---|---|
| UART | SER0 | TX = PA00, RX = PA03 |
| GPIO out | – | PA02, PA06 |
| I2C | SER1 | SDA = PA04, SCL = PA05 |
| SPI | SER2 | SDI = PA08, SCK = PA09, CS_N = PA10, SDO = PA11 |

---

## 8. RCP method IDs

Service `0xFF10`. **Verified** against the authoritative Microchip SOME/IP dissector table (Wireshark `SOMEIP_method_event_identifiers`). Each request is encoded as `Fill_Header` + one `Fill_<field>` per parameter (tag data id 0,1,2,…) + `Update_Length`; replies are parsed field-by-field — exactly as the C++ `LAN866XClientImpl`.

| Method | ID | | Method | ID |
|---|---|---|---|---|
| Reboot | `0x1000` | | OpenSpi | `0x1500` |
| GetStatus | `0x1002` | | CloseSpi | `0x1502` |
| GetNetworkStatus | `0x1600` | | WriteAndReadSpi | `0x1508` |
| WakeupNetwork | `0x1601` | | OpenUart | `0x1400` |
| ReleaseDigitalPins | `0x1105` | | WriteUart | `0x1404` |
| OpenGpio | `0x1300` | | ReadUart | `0x1420` |
| SetGpio | `0x1330` | | OpenAdc | `0x1700` |
| GetGpio | `0x1332` | | ReadAdc | `0x1720` |
| OpenI2C | `0x1200` | | OpenPwm | `0x1800` |
| WriteI2C | `0x1204` | | WritePwm | `0x1804` |
| ReadI2C | `0x1220` | | OnGpioEvents (evt) | `0x8000` |
| WriteAndReadI2C | `0x1208` | | OnUartReceive (evt) | `0x8010` |

> Note: the *Library Integration Manual* prose lists `OpenI2C = 0x0100` — that is a typo; the dissector table (and the live wire) use `0x1200`. Always trust the dissector CSV / `lan866x_common.h` over the prose.

---

## 9. Porting to MCU32

The whole toolset is already vanilla C on `libsomeip`, so the embedded port is small. The **only platform-specific file is `src/someip_stub_win.c`** — it implements the `SOMEIP_CB_*` callbacks (UDP sockets, semaphores, critical sections, time, buffers) on Win32 + Winsock. For MCU32 you **replace just that one file** with an lwIP/FreeRTOS implementation of the same callback set; `rcp.c`/`rcp.h` and the tool logic stay unchanged.

What stays / what is swapped:

| Layer | Windows | MCU32 |
|---|---|---|
| Tool + `rcp.c` (RCP encode/decode) | C | **same** |
| SOME/IP core (`libsomeip/src/*.c`) | C | **same** |
| Platform stub (`SOMEIP_CB_*`) | `src/someip_stub_win.c` (Winsock, Win32 threads) | **new lwIP/FreeRTOS file** |
| Time base (`SOMEIP_CB_GetTimeMS`) | `GetTickCount()` | `xTaskGetTickCount()` |
| T1S link | USB-Ethernet bridge | LAN8650/51 MAC-PHY (OA-SPI) or LAN8670 PHY (RMII) + lwIP |

**Recommended base:** Microchip also ships a generated, callback-based **pure-C client** (`lan866x_c/`) in the multi-language library repo (see the *LAN866x Library Integration Manual*); it uses the same `RT_*` codes and `*Var_t`/`*Reply_t` structs as `include/lan866x_common.h`. `src/rcp.c` here is a compact, self-contained equivalent built directly on `libsomeip`.

➡️ Details: **[PORTING.md](PORTING.md)**.
