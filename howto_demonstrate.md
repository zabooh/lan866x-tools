# How to set up and demonstrate the LAN866x T1S demo

A step-by-step guide to building the demo from scratch and showing it live: the
hardware you need, how to wire it, how to configure the PC, find the endpoint, flash
the firmware, and run the interactive **Click demo**.

> See also: [README.md](README.md) (build & tool overview) and
> [TOOLS.md](TOOLS.md) (board details, jumper/DIP map, full tool reference).

## Table of contents

1. [Why this tooling exists](#1-why-this-tooling-exists)
2. [What you need](#2-what-you-need)
3. [Assemble the hardware](#3-assemble-the-hardware)
   - 3.1 [Mount the Click boards & set the jumpers](#31-mount-the-click-boards--set-the-jumpers)
   - 3.2 [Wire the T1S bus – P↔P, N↔N](#32-wire-the-t1s-bus--pp-nn)
4. [Configure the PC – static IP 192.168.0.100](#4-configure-the-pc--static-ip-1921680100)
5. [Build the tools](#5-build-the-tools)
6. [Find the endpoint (bus scan)](#6-find-the-endpoint-bus-scan)
7. [Flash the current firmware package](#7-flash-the-current-firmware-package)
8. [Run the Click demo](#8-run-the-click-demo)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Why this tooling exists

Two goals drove this toolset:

1. **Instant start, zero extra installs.** The whole thing is a handful of small
   command-line executables. You don't install a runtime, a Qt GUI, Python, .NET or any
   SDK – unpack, build once, run. Everything needed to build is in this one directory
   (the SOME/IP stack, the RCP wrapper and the platform layer are all bundled). It is
   the fastest way to bring up a LAN866x endpoint and prove the link, the firmware and
   the peripherals all work.

2. **A 1:1 template for an MCU port.** Every tool, the RCP layer and the platform stub
   are written in **plain vanilla C — no C++** (they link without `libstdc++`). The
   exact same source is meant to be lifted onto a **32-bit microcontroller** (lwIP +
   FreeRTOS): you swap a *single* platform file (`src/someip_stub_win.c`) and keep all
   the RCP and application logic. So this PC demo doubles as the reference
   implementation for embedded firmware. (See [README §9](README.md#9-porting-to-mcu32)
   and [PORTING.md](PORTING.md).)

```
   PC demo (this toolset)                          MCU target (your firmware)
   ┌───────────────────────────┐                  ┌───────────────────────────┐
   │ tool logic (discovery,     │   identical C    │ tool logic                 │
   │ flashpkg, clickdemo …)     │ ───────────────► │ (unchanged)                │
   │ rcp.c  (RCP encode/decode) │                  │ rcp.c  (unchanged)         │
   │ libsomeip/src/*.c          │                  │ libsomeip/src/*.c (same)   │
   │ someip_stub_win.c (Winsock)│   ← swap ONLY →   │ someip_stub_lwip.c (lwIP)  │
   └───────────────────────────┘   this one file   └───────────────────────────┘
```

---

## 2. What you need

**Boards**

| # | Item | Order code | Role |
|---|---|---|---|
| 1 | **EVB-LAN8670-USB-PSE** | EV79C09A-R1 | USB→T1S adapter for the PC; also powers the bus (PoDL) |
| 1 | **EVB-LAN8680-LAN866x** | EV12T76A | the LAN866x endpoint (the “Apps Board”) you control |

**Click boards (MikroE) for the demo**

| Qty | Click | Goes in slot |
|---|---|---|
| 2 | **10×10 RGB Click** (WS2812) | Click 1 (top-left) + Click 2 (top-right) |
| 1 | **Thumbstick Click** (MCP3204, SPI) | Click 4 (bottom-left) |
| 1 | **Proximity 3 Click** (VCNL4200, I²C) | Click 3 (bottom-right) |

**Cables, power & PC**

- A **USB cable** from the PC to the adapter (the adapter's J4).
- **Two wires** for the T1S single pair (to the screw terminals) — or a short twisted
  pair.
- A **Windows 10/11 PC** with admin rights (to set the NIC IP and install the driver).
- The **adapter Windows driver** (`EVB-LAN8670-USB_Drv_Setup.exe`) and the **firmware
  package** `LAN8661-ws2812_V1.3.2_RELEASE_display1.mchpkg` (both in the demo material).
- A **C compiler + CMake** to build the tools once (see [README §2](README.md#2-system-requirements)).

```
   ┌─────────┐   USB    ┌───────────────────────┐  T1S pair (P↔P, N↔N)  ┌────────────────────────┐
   │   PC    │─────────►│  EVB-LAN8670-USB-PSE   │══════════════════════│  EVB-LAN8680-LAN866x   │
   │ (tools) │          │  adapter (PSE/PoDL)    │   2-wire + 12 V PoDL  │  + 4 Click boards      │
   └─────────┘          └───────────────────────┘                       └────────────────────────┘
```

---

## 3. Assemble the hardware

### 3.1 Mount the Click boards & set the jumpers

Plug the four Click boards onto the endpoint board exactly like this (LED matrices and
sensors facing the **outer** board edges; match each Click's **bevelled corner** to the
socket so the two 1×8 rows line up):

![Click placement on the endpoint board](docs/img/setup-click.jpg)

```
   ┌───────────────────┐     ┌───────────────────┐
   │ CLICK 1            │     │ CLICK 2            │
   │ 10×10 RGB (WS2812) │     │ 10×10 RGB (WS2812) │
   └───────────────────┘     └───────────────────┘
   ┌───────────────────┐     ┌───────────────────┐
   │ CLICK 4            │     │ CLICK 3            │
   │ Thumbstick (SPI)   │     │ Proximity 3 (I²C)  │
   └───────────────────┘     └───────────────────┘
```

> ⚠️ **Thumbstick → Slot 4, Proximity → Slot 3** (that's the SPI / I²C routing set by
> the DIP switches). The two RGB panels are interchangeable.

Set the jumpers and DIP switches to the shipped demo configuration (legend `▣`=ON,
`·`=OFF). Reference photo:

![Jumper & DIP layout](docs/img/setup-jumper-and-dip.jpg)

```
   Power jumpers:   J11 [1]·[2▣3]   J12 [1]·[2▣3]   J15 [1▣2]·[3]
                    J13-1 ▣──▣ J14-2  (5 V Click header from USB — the RGB panels need it)
                    J18 [▣ closed]    (12 V PoDL ; open = 5 V USB)

   Click routing:   1 2 3 4 5 6 7 8
        SW5  RGB1   · · · · · · · ▣      SW7  RGB2   · · · · · · · ▣
        SW9  Prox   · · ▣ · · ·          SW10 Prox   · · ▣ · · ·
        SW11 Thumb  ▣ · · · · · ▣ ·      SW12 Thumb  ▣ · · · · ·
        SW13 LEDs   ▣ ▣ ▣ · · · · ·      (enable the 3 on-board LEDs)
```

Full explanation of every switch: [TOOLS.md §2.4 / §2.5](TOOLS.md#24-click-slots--what-plugs-where).

### 3.2 Wire the T1S bus – P↔P, N↔N

Connect the adapter's screw terminal (**J6**) to the endpoint's bus terminal (**CN1**)
with two wires. **Polarity matters because the adapter feeds PoDL power** — always join
**P to P and N to N**:

```
   EVB-LAN8670-USB-PSE (J6)                 EVB-LAN8680-LAN866x (CN1)
   ┌───────────────┐                        ┌───────────────┐
   │  Terminal 1 = TRX_P  ●─────────────────● TRX_P  (P)     │
   │  Terminal 2 = TRX_N  ●─────────────────● TRX_N  (N)     │
   └───────────────┘                        └───────────────┘
            P ───────────► P        N ───────────► N
```

> 🛑 **Do NOT cross the pair.** P↔N swapped will not link, and with PoDL active it can
> stress the power path. Double-check P→P / N→N before powering up.

**Termination:** a 10BASE-T1S segment is terminated at its **two physical ends**.
- On the adapter: close **both** termination jumpers **J1 + J2**.
- On the endpoint: enable its edge termination (the board's termination option).

For a simple **point-to-point** demo (adapter ↔ one endpoint) both devices are ends, so
terminate both. Power the endpoint over the bus (PoDL, J18 closed) or via its own USB-C.

Now plug the adapter into the PC's USB. Windows enumerates it as an Ethernet NIC; if
prompted, install `EVB-LAN8670-USB_Drv_Setup.exe` first.

![The EVB-LAN8670-USB-PSE adapter](docs/img/usb-pse-adapter.png)

---

## 4. Configure the PC – static IP 192.168.0.100

The endpoints live on `192.168.0.<NodeID>` (the demo board = `192.168.0.54`). Give the
adapter's NIC a **static IP `192.168.0.100`**, mask `255.255.255.0`.

**GUI way**
1. `Win+R` → `ncpa.cpl` → Enter (Network Connections).
2. Find the adapter (a new “Ethernet” NIC — *LAN7800 / USB Ethernet* style name). Right-
   click → **Properties**.
3. Select **Internet Protocol Version 4 (TCP/IPv4)** → **Properties**.
4. **Use the following IP address:**
   - IP address: `192.168.0.100`
   - Subnet mask: `255.255.255.0`
   - Gateway / DNS: leave empty.
5. **OK** → **Close**.

```
   ┌ Internet Protocol Version 4 (TCP/IPv4) Properties ─────────┐
   │ ( ) Obtain an IP address automatically                     │
   │ (•) Use the following IP address:                          │
   │       IP address . . . . . :  192 . 168 .  0 . 100         │
   │       Subnet mask  . . . . :  255 . 255 . 255 .  0         │
   │       Default gateway  . . :   .   .   .                   │
   └────────────────────────────────────────────────────────────┘
```

**Command-line way** (admin “Command Prompt”; replace the interface name with yours from
`netsh interface show interface`):

```bat
netsh interface ip set address name="Ethernet 5" static 192.168.0.100 255.255.255.0
```

Verify:

```bat
ipconfig            REM the adapter's IPv4 must read 192.168.0.100
ping 192.168.0.54   REM optional, once the endpoint is flashed & powered
```

> 💡 If the PC has other network adapters, the tools join SOME/IP multicast on all of
> them but only the T1S NIC will answer. Disabling unrelated NICs makes the demo more
> reliable (fewer dropped responses).

---

## 5. Get the tools (already built)

**You don't have to build anything.** All tools are pre-built and statically linked in
the **`release\`** folder, together with the demo firmware package — just run them from
there:

```bat
dir release\*.exe          REM lan866x-discovery.exe, -flashpkg.exe, -clickdemo.exe, ...
```

All commands in this guide can be run from `release\` (e.g. `release\lan866x-discovery.exe`).

**Optional – build from source** (only if you change the code; needs CMake + a C
compiler, [README §2](README.md#2-system-requirements)):

```bat
build.bat
```

This produces `out\lan866x-*.exe` and refreshes `release\`. No runtime to install — the
binaries are statically linked.

---

## 6. Find the endpoint (bus scan)

Run **discovery** — it broadcasts a SOME/IP `FindService` and lists every endpoint that
answers, with its IP, chip, firmware versions and link state:

```bat
out\lan866x-discovery.exe
```

```
Devices available = 1

========================================================
Endpoint #0  -  192.168.0.54:6800  (instance 0x0001, available=1)
========================================================
  Chip Identifier:    LAN8661B   -> Lighting Endpoint
  Main Version:       LAN8661-main-ws2812_V1.3.x-..
  Endpoint Status:    Link-Up
  Arbitration:        PLCA
  PLCA Node Id:       4
```

Seeing the endpoint here proves the **wiring, termination, driver and IP** are all
correct. If nothing shows up, jump to [Troubleshooting](#9-troubleshooting).

> For a deeper link check use `out\lan866x-diag.exe --ip 192.168.0.54` (loss %, RTT,
> PLCA) — see [TOOLS.md §4.3](TOOLS.md#43-lan866x-diag).

---

## 7. Flash the current firmware package

To run the Click demo the endpoint must run the **Lighting** firmware
(`LAN8661-ws2812 … display1`). Flash it straight from the `.mchpkg` package:

```bat
out\lan866x-flashpkg.exe LAN8661-ws2812_V1.3.2_RELEASE_display1.mchpkg --ip 192.168.0.54
```

What it does: opens the package, extracts the app + config images, reboots the endpoint
into the bootloader, flashes **app then config**, reboots into the main app and verifies
the running version — printing a clear `UPDATE OK`.

```
Package: LAN8661-ws2812_V1.3.2_RELEASE_display1.mchpkg
  target main version: LAN8661-main-ws2812_V1.3.2-64
Rebooting into bootloader ...  -> rebooted
Flashing main/app.bin (196608 bytes) ...  writing 196608 / 196608 bytes (100%)  -> written OK
Flashing main/config.bin (4096 bytes) ... -> written OK
Rebooting into main app ...    -> rebooted
  [after] App=main/app.bin  Main=LAN8661-main-ws2812_V1.3.2-64
================  UPDATE OK  ================
```

> ⚠️ Flashing writes signed images and is mildly risky, but **recoverable**: a half-
> written config just makes the device fall back to the bootloader — re-run the command
> to recover. Always flash a matching **app+config pair**. Details:
> [TOOLS.md §4.11](TOOLS.md#411-lan866x-flashpkg).

The `display1` package sets the board to **IP 192.168.0.54 / PLCA node 4**. (Variants
`display2`/`display3` use `.55`/`.56`.)

---

## 8. Run the Click demo

Start the interactive demo against the endpoint:

```bat
out\lan866x-clickdemo.exe --ip 192.168.0.54
```

**What it shows / how to present it**

- The **left RGB panel** (Click 1) shows an **orange “flashlight” spot** that you steer
  with the **Thumbstick** (Click 4). Move the stick → the spot moves to the edges;
  centred → the spot sits in the middle. → *demonstrates SPI peripheral control* (the
  tool reads the MCP3204 ADC over SPI).
- The **right RGB panel** (Click 2) shows a **blue bar** whose height follows the
  **Proximity** sensor (Click 3): hold your hand close → the bar rises; move away → it
  drops. → *demonstrates I²C peripheral control* (reading the VCNL4200 over I²C).

```
   ┌─────────────────┐   ┌─────────────────┐
   │  ░░░░░░░░░░      │   │            ▓     │   left  = Thumbstick → orange spot (SPI)
   │  ░░░▓▓▓░░░░      │   │            ▓     │   right = Proximity  → blue bar    (I²C)
   │  ░░▓███▓░░░      │   │            ▓     │
   │  ░░░▓▓▓░░░░      │   │            ▓     │   move stick → spot moves
   │  ░░░░░░░░░░      │   │                  │   hand closer → bar rises
   └─────────────────┘   └─────────────────┘
        Click 1               Click 2
```

The console prints live values; `..` next to a sensor means it isn't answering (Click
not seated / DIP wrong):

```
  Thumbstick x=2048 y=2050    | Proximity raw=  37
```

**Under the hood** (good talking points): sensors are read over the **RCP SOME/IP
service** (SPI + I²C round-trips); the two displays are painted from a single **20×10
RTP/RFC4175 video stream** the tool sends to UDP 5001 (left 10 columns = display 1, right
10 = display 2). All of it is **vanilla C** — the same code that would run on an MCU.

Press **Ctrl-C** to stop; the demo clears both displays and releases the peripherals.

Options: `--fps N`, `--bright 0..255`, `--bar 0..255`, `--prox-max N`
(see [TOOLS.md §4.12](TOOLS.md#412-lan866x-clickdemo)).

---

## 9. Troubleshooting

| Symptom | Check |
|---|---|
| `discovery` finds nothing | NIC IP = `192.168.0.100/24`? adapter driver installed? bus wired **P↔P / N↔N**? termination on both ends? endpoint powered (PoDL J18 closed or USB-C)? |
| Endpoint powers but no link | swapped pair (P↔N) — fix polarity; missing/!double termination |
| Found, but `clickdemo` says sensors `..` | Thumbstick must be in **Slot 4**, Proximity in **Slot 3**; check DIP SW9/10-3, SW11-1/7, SW12-1 |
| RGB panels stay dark | 5 V Click path not enabled (J13-1↔J14-2), USB underpowered, or SW5-8 / SW7-8 not ON |
| `clickdemo` runs but nothing on the panels | wrong firmware — must be the **ws2812 Lighting** build (re-flash, §7) |
| Flash fails / device stuck in bootloader | re-run `flashpkg` with a matching app+config package (recoverable) |
| Lots of dropped responses | disable PC NICs not on the T1S subnet; allow the tools through the firewall |

> A packet capture (Wireshark on the T1S NIC) is the authoritative reference if the link
> behaves oddly.
