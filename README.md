# LAN866x Tools – SOME/IP console tools (RCP)

Minimal **C** host that remote-controls LAN866x **control endpoints** via the **Remote Control Protocol (RCP)** on top of the pure **C SOME/IP stack (`libsomeip`)**. Access is via the **T1S-USB adapter** (EVB-LAN8670-USB) as an Ethernet bridge.

> 📦 **Self-contained package:** This directory contains **all** sources required to build. Unpack → build, no external paths needed.

## Table of contents

1. [Overview](#1-overview)
2. [System requirements](#2-system-requirements)
3. [How to compile](#3-how-to-compile)
4. [Running and output](#4-running-and-output)
5. [How does discovery work?](#5-how-does-discovery-work)
6. [Project structure](#6-project-structure)
7. [Example pin mapping (LAN8660)](#7-example-pin-mapping-lan8660)
8. [RCP method IDs](#8-rcp-method-ids)
9. [C template for MCU32 (Track B)](#9-c-template-for-mcu32-track-b)

---

## 1. Overview

**Purpose:** Windows prototype → later used 1:1 as a template for a **32-bit embedded device** (MCU32 + lwIP + FreeRTOS). Typical **control-endpoint use case**: only **GPIO / I2C / SPI** (+ UART), e.g. with a LAN8660.

The package consists of two parts:

| Track | File(s) | Status | Purpose |
|---|---|---|---|
| **A – tools (C++)** | `discovery.cpp`, `i2cscan.cpp`, ... | ✅ **builds & runs** | Ready-to-use PC tools on top of the prebuilt stack |
| **B – C host template** | `src/*.c` | template | Portable C code → MCU32 (lwIP/FreeRTOS), see [chapter 9](#9-c-template-for-mcu32-track-b) |

**Track A tools** (use `libepmicrochip` over the T1S-USB adapter):
- **`lan866x-discovery`** – lists reachable endpoints + type + RCP service `0xFF10` + full `GetStatus`/`GetNetworkStatus` info.
- **`lan866x-i2cscan`** – scans the I2C bus of an endpoint (like `i2cdetect`).
- **`lan866x-gpio`** – set/read a GPIO pin.
- **`lan866x-spi`** – SPI transfer (full-duplex).
- **`lan866x-dncpmon`** – passive **DNCP** monitor (standalone, not SOME/IP).
- **`lan866x-dncpdisc`** – **active** DNCP discovery (Registry broadcast → collect Announces, read-only).

---

## 2. System requirements

To **build**, the machine needs:

### 2.1 CMake ≥ 3.10  *(tested: 4.1)*
- Download: <https://cmake.org/download/> → "Windows x64 Installer".
- During installation choose **"Add CMake to the system PATH"**.
- Check: `cmake --version`

### 2.2 A C/C++ compiler  *(one of the two options)*

**Option A – MinGW-w64 (GCC)** — recommended for the command line *(tested GCC 16.1)*
- Easiest source: **WinLibs** <https://winlibs.com/> (UCRT variant) – unzip and add the `…\mingw64\bin` folder to **PATH**.
- Alternatively **MSYS2** <https://www.msys2.org/>: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake`
- Check: `gcc --version` and `mingw32-make --version`

**Option B – Visual Studio 2022**
- Installer: <https://visualstudio.microsoft.com/> → workload **"Desktop development with C++"** (includes the MSVC compiler **and** CMake).

### 2.3 Hardware & driver  *(only to run, not to build)*
- **EVB-LAN8670-USB** (T1S-USB adapter) – shows up on Windows as a normal **Ethernet NIC**.
- Install the **Windows driver** (`EVB-LAN8670-USB_Drv_Setup.exe`, from the LAN866x Remote Demo package / MicrochipDirect **EV08L38A**).
- LAN866x endpoint(s) on the T1S bus, **bus terminated**, PoDL power if applicable.

### 2.4 Network  *(only to run)*
- Give the USB-T1S NIC a **static IP** in the endpoint subnet: **`192.168.0.100/24`**. Endpoints = `192.168.0.<NodeID>`.
- **SOME/IP-SD** uses multicast `224.0.0.1`. Allow `lan866x-discovery.exe` through the Windows **firewall**.

---

## 3. How to compile

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
**Always specify the generator (`-G …`) explicitly** – otherwise CMake aborts with "CMAKE_C(XX)_COMPILER not set".

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

```bat
out\lan866x-discovery.exe
```

Per endpoint the tool prints the **full** status (like the Microchip Remote Configurator) — via `GetStatus (0x1002)` + `GetNetworkStatus (0x1600)`. Example (verified on the training setup):
```
Devices available = 2

========================================================
Endpoint #0  -  192.168.0.54:6800  (SOME/IP Instance 0x0001, available=1)
========================================================
  Uptime:             0h 17m 17.897s
  Application:        main/app.bin
  Chip Identifier:    LAN8661B
  Main Name:          Endpoint Demo Display And Remote
  Bootloader Name:    Endpoint Demo Display
  Main Version:       LAN8661-main-ws2812_V1.3.2-64
  Root Version:       LAN866x-root_V1.2.0-53
  Bootloader Version: LAN866x-bootloader_V1.3.1-60
  COMO Version:       V2.1.1
  Service Version:    V1.8
  Keys Version:       V0.0.1
  StartupInformation:
        ..Software Reset
        ..Security Mode: 1
  MAC:                8C:71:12:2D:02:FA
  IPv4:               192.168.0.54
  IPv6:               FDF8:6EF3:E05A:0001:0000:0000:0000:0054
  Endpoint Status:    Link-Up
  OASPI Status:       Disabled
  Arbitration:        PLCA no fallback
  PLCA Node Id:       4
```

**Nothing found?** Check: driver installed · NIC IP `192.168.0.x` set · bus terminated · endpoints powered · firewall allowed.

### I2C bus scanner
```bat
out\lan866x-i2cscan.exe                 REM first endpoint, SDA=PA08 SCL=PA09, 400 kHz
out\lan866x-i2cscan.exe --ip 192.168.0.54
out\lan866x-i2cscan.exe --ep 1 --sda 8 --scl 9 --speed 1
```
Probes 0x08..0x77 with a 1-byte read and prints an `i2cdetect` grid. Example (Proximity 3 Click on the demo board):
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
50: -- 51 -- -- -- -- -- -- -- -- -- -- -- -- -- --

1 device(s) found on the I2C bus.
```
> Pins must match the board configuration (`--sda`/`--scl`, PA number 0–15). The tool releases SDA/SCL before `OpenI2C` automatically (`ReleaseDigitalPins`).

### GPIO set/read
```bat
out\lan866x-gpio.exe --pin 2 --set 1     REM PA02 as output, high
out\lan866x-gpio.exe --pin 2 --get       REM PA02 as input, read
out\lan866x-gpio.exe --ip 192.168.0.54 --pin 6 --set 0
```

### SPI transfer (full-duplex)
```bat
out\lan866x-spi.exe --tx 9F0000          REM send 3 bytes, read MISO at the same time
out\lan866x-spi.exe --tx AA55 --mode 0 --speed 1000000
out\lan866x-spi.exe --miso 12 --sck 13 --cs 14 --mosi 15 --tx 0102
```
Output: `TX: …` / `RX: …`. Default pins MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15. Pins are released before `OpenSpi`.

### DNCP monitor (passive)
```bat
out\lan866x-dncpmon.exe                REM listen forever (Ctrl+C to stop)
out\lan866x-dncpmon.exe --timeout 30   REM stop after 30 s without packets
```
Decodes **DNCP** packets (Dynamic Node Configuration Protocol) on **UDP 65526/65527** — Announce/Registry with MAC, device id, IPv4/IPv6, state (Unconfigured/Configured) and PLCA ids. Standalone (Winsock only), **not** part of SOME/IP/`libLAN866x`.
> Purely **passive**: only shows DNCP traffic actually present on the bus. To trigger actively, see `lan866x-dncpdisc`.

### DNCP discovery (active, read-only)
```bat
out\lan866x-dncpdisc.exe                       REM 3 rounds, channel 11
out\lan866x-dncpdisc.exe --channel 11 --rounds 5 --timeout 4
```
Acts as a **temporary DNCP server** (per AN1891): broadcasts an **empty Registry** to `224.0.0.1:65527`; nodes that do not find themselves in it send an **Announce** to `224.0.0.1:65526`. Per node **all Announce fields** are decoded: MAC, vendor device id, **IPv4 + IPv6**, state, persistency, BurstFramesPerTO, protocol version and all PLCA node ids.
> **Read-only** — assigns no PLCA ids/IPs, persists nothing (no Assign/StoreSettings/Activate). EnumChannel = default (11), so the nodes' enumeration channel stays unchanged. **Use only when no other DNCP server is active.** Verified live (a LAN8662 responded).

---

## 5. How does discovery work?

The tools do **not** know the endpoint IPs in advance – they learn them at runtime via **SOME/IP Service Discovery (SD)**:

1. **Join multicast:** the tool joins the SD group **`224.0.0.1`** (the "Joined Multicast group" lines at startup – one per PC network interface).
2. **Send FindService:** it queries via SD **`FindService`** for the RCP service **`0xFF10`**.
3. **Endpoints respond:** every endpoint that offers `0xFF10` sends back an **`OfferService`** – **containing its own IP/port** (e.g. `192.168.0.102:6800`). Endpoints also send Offers periodically on their own.
4. **Build the list:** `libepmicrochip` collects the offers → `GetAllClients()`. **So the IP comes from the endpoint, not from the tool.**

```
PC  --FindService(0xFF10)-->  224.0.0.1  (multicast)
EP1 --OfferService: I am 192.168.0.101 -->  PC
EP2 --OfferService: I am 192.168.0.102 -->  PC
```

**Target endpoint:** default is `[0]` (first found). Select with `--ip <addr>` or `--ep <index>`. Newly attached endpoints appear automatically.

> Multiple PC interfaces: the tool joins on all of them; responses arrive only over the **T1S interface** (`192.168.0.x`). Its NIC must therefore have an IP in the endpoint subnet (chapter 2.4).

---

## 6. Project structure

```
lan866x-tools/
├── build.bat            Windows build script (chapter 3)
├── CMakeLists.txt       builds the tool targets
├── discovery.cpp        Track A: endpoint/service discovery (ready to run)
├── i2cscan.cpp          Track A: I2C bus scanner
├── gpio.cpp             Track A: GPIO set/read
├── spi.cpp              Track A: SPI transfer
├── dncpmon.cpp          Track A: passive DNCP monitor (standalone)
├── dncpdisc.cpp         Track A: active DNCP discovery (standalone)
├── include/             public headers (lan866x_client.hpp, ...)
├── libepmicrochip/      SOME/IP stack (C) + liblan866x + Windows platform stub
├── src/                 Track B: portable C template for MCU32
│   ├── main.c           demo loop (init → discovery → GPIO/I2C/SPI)
│   ├── rcp.h            method IDs, pin map, API
│   └── rcp.c            RCP wrapper over libsomeip
├── README.md
└── PORTING.md           MCU32 port (lwIP/FreeRTOS)
```
All sources required to build live **inside** this directory.

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

Service `0xFF10`. Verified from `lan866x_client.cpp`.

| Method | ID | | Method | ID |
|---|---|---|---|---|
| GetStatus | `0x1002` | | OpenSpi | `0x1500` |
| OpenGpio | `0x1300` | | WriteAndReadSpi | `0x1508` |
| SetGpio | `0x1330` | | OpenUart | `0x1400` |
| GetGpio | `0x1332` | | WriteUart | `0x1404` |
| OpenI2C | `0x1200` | | ReadUart | `0x1420` |
| WriteAndReadI2C | `0x1208`* | | WakeupNetwork | `0x1601` |

\* I2C read/write variants are in the range `0x1203…0x1220` – verify the exact ID/payload against `lan866x_client.cpp`.

---

## 9. C template for MCU32 (Track B)

`src/main.c`, `rcp.c`, `rcp.h` are the **portable C template** for the embedded target (pure C on `libsomeip`). It is built against the real C API + verified method IDs/structs; only the parameter layouts marked `[V3]/[V4]` and the RX dispatch function `on_data_received()` (template: `LAN866XClientImpl::OnDataReceived`) remain to be completed.

Track B is **not** built by this Windows CMake, but in the MCU32 project together with an lwIP/FreeRTOS implementation of the `SOMEIP_CB_*` callbacks (replacing the Windows stub).

➡️ Details: **[PORTING.md](PORTING.md)**.
