# t1s_100baset_bridge

A **10BASE-T1S ↔ 100BASE-T Layer-2 bridge** firmware for the ATSAME54P20A, with
an embedded **LAN866x SOME/IP (RCP) client**. It lets a PC on ordinary Fast
Ethernet reach — and command — a Microchip **LAN866x 10BASE-T1S endpoint** that
lives on the two-wire T1S bus, and it can run the same control/diagnostic flows
on-board, straight from a serial console.

> This is a vendored copy under `lan866x-tools/`. PTP grandmaster/follower
> support has been removed here and lives on in the newer
> [`net_10base_t1s`](https://github.com/zabooh/net_10base_t1s) project.

---

## Contents

- [1. What this firmware is for](#1-what-this-firmware-is-for)
  - [What you can demonstrate](#what-you-can-demonstrate)
- [2. Hardware setup](#2-hardware-setup)
  - [Bridge board: bill of materials](#bridge-board-bill-of-materials)
  - [How `eth0` (LAN865x) is wired](#how-eth0-lan865x-is-wired-from-the-firmware-config)
  - [Endpoint side (separate hardware)](#endpoint-side-separate-hardware)
  - [Network and addressing (default)](#network-and-addressing-default)
  - [Console and cabling](#console-and-cabling)
- [3. Firmware architecture](#3-firmware-architecture)
  - [Block view](#block-view)
  - [The bridge data path](#the-bridge-data-path)
  - [The embedded LAN866x SOME/IP client (the toolset port)](#the-embedded-lan866x-someip-client-the-toolset-port)
  - [Application state machine (`app.c`)](#application-state-machine-appc)
  - [Port mirror and SPAN (Wireshark)](#port-mirror-and-span-wireshark)
  - [CLI command groups](#cli-command-groups)
- [4. Building it yourself](#4-building-it-yourself)
  - [4.1 Tool prerequisites (per machine)](#41-tool-prerequisites-per-machine)
  - [4.2 One-time setup after cloning](#42-one-time-setup-after-cloning)
  - [4.3 Build and flash](#43-build-and-flash)
  - [4.4 First bring-up checklist](#44-first-bring-up-checklist)
- [5. Changing IP and PLCA configuration](#5-changing-ip-and-plca-configuration)
  - [5.1 Persistent: edit the build config and rebuild](#51-persistent-edit-the-build-config-and-rebuild)
  - [5.2 Runtime via the CLI (not persistent)](#52-runtime-via-the-cli-not-persistent)
- [6. Port mirror: capturing the T1S bus in Wireshark](#6-port-mirror-capturing-the-t1s-bus-in-wireshark)
  - [6.1 Why a mirror is needed](#61-why-a-mirror-is-needed)
  - [6.2 What gets mirrored (RX and TX paths)](#62-what-gets-mirrored-rx-and-tx-paths)
  - [6.3 Using it](#63-using-it)
  - [6.4 Limitations](#64-limitations)

---

## 1. What this firmware is for

The board sits between two worlds:

```
   PC / lab network                Bridge (this firmware)            T1S bus
   100BASE-T (RJ45)          ATSAME54P20A + LAN865x + LAN8740     10BASE-T1S (2-wire)
   ┌──────────────┐  100M    ┌───────────────────────────┐  T1S   ┌──────────────┐
   │  Wireshark   │◄────────►│ eth1 (GMAC)   eth0 (LAN865x)│◄──────►│  LAN866x     │
   │  ping        │ .181/.180│   └── MAC bridge (L2) ──┘   │ PLCA   │  endpoint    │
   │ lan866x-*.exe│          │   + SOME/IP RCP client      │ node 0 │  192.168.0.54│
   └──────────────┘          └───────────────────────────┘        └──────────────┘
```

It does three distinct jobs:

**a) Transparent L2 bridge.** The two interfaces — `eth0` (the T1S MAC-PHY) and
`eth1` (100BASE-T) — are joined by the Harmony **MAC bridge**, so any PC-side
traffic (ARP, ICMP/ping, mDNS, and the host tools' SOME/IP) flows through to the
endpoint on the T1S bus and back, with MAC learning (FDB). From the PC you can
simply `ping 192.168.0.54` or run `lan866x-discovery.exe` and reach the endpoint
*through* the bridge as if it were on the local Ethernet.

**b) On-board LAN866x controller.** The full `lan866x-tools` SOME/IP client
(`rcp.c` + the SOME/IP stack) is compiled into the firmware and exposed as a
`lan866x` console command group. So the same operations the PC tools perform —
endpoint discovery, link diagnostics, the LED demo, the sensors→displays Click
demo — can be driven from the board's serial CLI without any PC software. This is
also the reference **MCU port** of the toolset (see `PORTING.md`): the only
platform-specific file is `plat_h3tcpip.c`.

**c) T1S bus analyzer / SPAN port.** The firmware can mirror T1S traffic onto
`eth1` so you can capture the two-wire bus in **Wireshark** on the PC — including
the endpoint's SOME/IP offers/replies *and* the bridge's own requests
(`mirror` command). It also has raw frame dump/logging (`ipdump`, `logstat`), a
raw-Ethernet loopback test (`noip_send`), LAN865x register peek/poke
(`lan_read`/`lan_write`), PLCA node-ID control, and per-interface counters.

### What you can demonstrate

- A PC reaching a 10BASE-T1S endpoint over a standard RJ45 link, end to end.
- Live T1S/PLCA traffic in Wireshark (mirrored onto Fast Ethernet).
- The LAN866x RCP service: device identity, firmware versions, link quality,
  PLCA status, achievable RCP goodput.
- Sensor-to-actuator over T1S: a thumbstick + proximity sensor driving two RGB
  matrix displays on a lighting endpoint, rendered via an RTP video stream.
- The toolset running unchanged on an MCU (same `rcp.c`, same protocol).

---

## 2. Hardware setup

The bridge node is built from Microchip Xplained-Pro hardware. The **LAN866x
endpoint** on the T1S side is a *separate* device (the thing you control); it is
not part of the bridge board.

### Bridge board: bill of materials

| Function | Board | Microchip order number |
|---|---|---|
| **MCU host** (Cortex-M4F, runs this firmware) | SAM E54 Xplained Pro Evaluation Kit (ATSAME54P20A) | **ATSAME54-XPRO** |
| **100BASE-T PHY** for `eth1` (GMAC ↔ RJ45) | LAN8740A PHY Daughter Board | **AC320004-3** |
| **10BASE-T1S MAC-PHY** for `eth0` (SPI ↔ two-wire bus) | MikroElektronika **Two-Wire ETH Click** (LAN8651) | `MIKROE-xxxx` — *verify on mikroe.com* |

> The LAN865x driver (`DRV_LAN865X`) talks to the **LAN8651** MAC-PHY on the
> Two-Wire ETH Click over SERCOM SPI. The SPI pin assignment in the firmware is
> fixed (CS=PC15, INT=PC14, see below) and the Click's wiring must land on those
> pins. The exact MikroElektronika order code should be confirmed against the
> board you have — it could not be looked up offline.

### How `eth0` (LAN865x) is wired (from the firmware config)

| Signal | SAM E54 pin | Notes |
|---|---|---|
| SPI instance | SERCOM **SPI driver instance 0** | `DRV_LAN865X_SPI_DRIVER_INSTANCE_IDX0 = 0` |
| SPI clock | **15 MHz** | `DRV_LAN865X_SPI_FREQ_IDX0 = 15000000` |
| Chip select (CS) | **PC15** | `DRV_LAN865X_SPI_CS_IDX0 = SYS_PORT_PIN_PC15` |
| Interrupt (INT) | **PC14** | `DRV_LAN865X_INTERRUPT_PIN_IDX0 = SYS_PORT_PIN_PC14` |

### Endpoint side (separate hardware)

- A **LAN866x 10BASE-T1S endpoint** (LAN8660 Control / LAN8661 Lighting /
  LAN8662 Audio), default address **192.168.0.54**, connected to the bridge's
  `eth0` over the two-wire T1S bus.
- For the **Click demo**, the endpoint is a **Lighting endpoint** with two
  10×10 RGB matrix displays, a **Thumbstick Click** (SPI, MCP3204) and a
  **Proximity 3 Click** (I²C, VCNL4200).
- The endpoint hardware, datasheets and firmware packages are **NDA material**
  and are not part of this repo (see `CLAUDE.md`).

### Network and addressing (default)

| Interface | Role | IP | Mask | PLCA |
|---|---|---|---|---|
| `eth0` | T1S (LAN865x) | **192.168.0.180** | /24 | node id **0** (coordinator), node count **8** |
| `eth1` | 100BASE-T (GMAC) | **192.168.0.181** | /24 | — |
| endpoint | LAN866x | 192.168.0.54 | /24 | follower |

Both bridge interfaces share one `192.168.0.0/24` subnet (gateway
`192.168.0.1`) — the MAC bridge makes that a single L2 segment. Put the PC's
RJ45 adapter on the same subnet (e.g. `192.168.0.200`).

> **PLCA coordinator.** The bridge is the PLCA coordinator (node id 0). If the
> T1S side shows no RX, this is the first thing to check — `plca_node`
> reads it back; `plca_node 0` re-asserts it.

### Console and cabling

1. **Debugger + console:** one USB cable from the PC to the SAM E54 Xplained
   Pro **EDBG** USB port. This is both the programmer (PKOB/EDBG) and the
   virtual COM port for the CLI (**115200 8N1**).
2. **100BASE-T:** RJ45 on the LAN8740 daughter board ↔ the PC's Ethernet
   adapter (the one set to `192.168.0.200`).
3. **T1S:** the two-wire bus from the LAN865x add-on to the LAN866x endpoint.

---

## 3. Firmware architecture

Built on **MPLAB Harmony 3** for the ATSAME54P20A. Single-threaded cooperative
superloop (`SYS_Tasks()` in `main.c`); no RTOS, no threads, no locks.

### Block view

```
                       ┌──────────────────────────────────────────────┐
   serial CLI ───────► │ SYS_CMD console                              │
   (EDBG COM)          │   ├─ "Test"  group  (app.c)                  │
                       │   └─ "lan866x" group (lan866x_cli.c,         │
                       │                       clickdemo_cli.c)        │
                       ├──────────────────────────────────────────────┤
   LAN866x SOME/IP  ◄─►│ rcp.c  ─►  someip_stub.c  ─►  libsomeip       │  (toolset core,
   (RCP 0xFF10)        │                 │                             │   unchanged)
                       │                 ▼                             │
                       │           plat_h3tcpip.c  (the only port file)│
                       ├─────────────────┼────────────────────────────┤
   T1S bus  ◄──────────┤ eth0: DRV_LAN865X ┐                          │
                       │                   ├─ TCPIP MAC bridge (L2) ─┐ │
   100BASE-T ◄─────────┤ eth1: GMAC+LAN8740┘   + Harmony TCP/IP stack │ │
                       └──────────────────────────────────────────────┘
                                         packet handlers (app.c):
                                         pktEth0Handler / pktEth1Handler
```

### The bridge data path

- `TCPIP_STACK_USE_MAC_BRIDGE` is enabled with **2 ports** (`eth0`, `eth1`), a
  17-entry FDB, and a dedicated packet pool. **The MAC bridge does all L2
  forwarding** between T1S and 100BASE-T — there is no manual forwarding code in
  the application (a former `fwd` command was removed for exactly this reason).
- `pktEth0Handler` / `pktEth1Handler` are **non-consuming observers**: they run
  per RX frame for logging/mirroring, then return `false` so the frame proceeds
  to normal stack + bridge processing.

### The embedded LAN866x SOME/IP client (the toolset port)

- `rcp.c`, `someip_stub.c` and `libepmicrochip/libsomeip/` are the **exact same**
  platform-neutral sources as the PC tools. The MCU value is **not** in `rcp.c`
  but in the platform layer.
- `plat_h3tcpip.c` implements the six `plat.h` functions over Harmony
  `TCPIP_UDP_*`. The hard-won details that make RCP work on this stack:
  - **Ephemeral ports:** Harmony `ServerOpen(...,0,...)` does not auto-assign a
    port, so `*port==0` requests get a concrete port from a dynamic range
    (49200+) and it's reported back, or the reply would be lost.
  - **eth0 pinning:** sockets are pinned to `eth0` (`TCPIP_UDP_SocketNetSet`)
    so requests to the endpoint always source from `.180` and replies return on
    T1S — both interfaces share one /24, so without pinning the reply path is
    ambiguous → `RT_TIMEOUT`.
  - **Multicast flags:** SD multicast (`224.0.0.1:30490`) is accepted *without*
    `UDP_MCAST_FLAG_IGNORE_UNICAST`, otherwise the unicast method replies (same
    socket) would be dropped.
  - **TX buffer = 1024 B:** the default 512 B is too small for the clickdemo RTP
    frame (~674 B), which would silently never send.
  - **`plat_sleep_ms()` pumps the stack:** it calls `TCPIP_STACK_Task()` +
    `SYS_CONSOLE_Tasks()` while waiting, so a *blocking* `rcp_*` call issued from
    a CLI handler still lets the network stack and live console progress run.
    This is safe because the command handler runs sequentially **after**
    `TCPIP_STACK_Task()` in the superloop (never re-entrant).

### Application state machine (`app.c`)

`APP_Initialize` registers the Telnet auth + a 1 s timer and calls
`LAN866X_CLI_Init()`. `APP_Tasks` walks `INIT → WAIT → SERVICE_TASKS → IDLE`:
in `SERVICE_TASKS` it registers the two packet handlers; in `IDLE` it (1) drives
the SOME/IP client every tick (`LAN866X_CLI_Task()` → background Service
Discovery), (2) services the async LAN865x register read/write state machine,
and (3) drains the deferred packet-log ring buffer to the console (≤10
entries/iteration, so logging never stalls the loop). Captured frame bytes go to
a separate circular pool; the ring uses a lock-free single-producer/consumer
pattern (handlers write, `APP_Tasks` reads).

### Port mirror and SPAN (Wireshark)

*(Full walkthrough and limitations in [§6](#6-port-mirror-capturing-the-t1s-bus-in-wireshark).)*

`mirror 1` turns on two clone paths so a PC capture on `eth1` sees the full T1S
picture:
- **RX mirror** (`mirror_eth0_to_eth1`, app.c): every frame the bus sends to the
  bridge is cloned to `eth1`.
- **TX mirror** (`mirror_udp_tx`, called from `plat_udp_send`): the bridge's own
  outgoing UDP (SOME/IP FindService + RCP requests) is rebuilt as an
  Ethernet/IPv4/UDP frame and injected on `eth1` — because a node never receives
  its own transmissions, this is the only way to see the request side.

### CLI command groups

Two `SYS_CMD` groups; type the command name directly (no group prefix needed).

**`lan866x` group** — mirrors the host tools against the endpoint over T1S:

| Command | Mirrors | Description |
|---|---|---|
| `discovery` | `lan866x-discovery.exe` | list endpoints + full status (run this first) |
| `diag [probes]` | `lan866x-diag.exe` | T1S link diagnostics + RCP-goodput estimate |
| `ledblink [laps] [ms]` | `lan866x-ledblink.exe` | LED running light (PA02/06/10) |
| `clickdemo [s] [fps]` | `lan866x-clickdemo.exe` | Thumbstick+Proximity → RGB displays (Ctrl-C/`q` to stop) |

**`Test` group** — bridge / diagnostics / bring-up:

| Command | Description |
|---|---|
| `mirror [0\|1]` | SPAN: copy T1S (eth0) traffic — RX **and** the bridge's own TX — to eth1 for Wireshark |
| `ipdump [0..3]` | dump RX frames (0=off, 1=eth0, 2=eth1, 3=both) |
| `stats` | per-interface TX/RX software counters |
| `plca_node [id]` | get/set PLCA node id (0 = coordinator); no arg = show current |
| `lan_read <addr>` / `lan_write <addr> <val>` | LAN865x register access (hex) |
| `noip_send <n> [gap_ms]` / `noip_stat` | raw-Ethernet (EtherType 0x88B5) loopback test + counters |
| `dump <addr> <count>` | memory dump (hex) |
| `logstat` / `logclear` | deferred packet-log statistics / clear |
| `timestamp` | firmware build timestamp |

Harmony stack commands (`netinfo`, `bridge`, `ping`, etc.) are also available.

---

## 4. Building it yourself

### 4.1 Tool prerequisites (per machine)

| Requirement | Notes |
|---|---|
| **MPLAB XC32** | v4.60 (baked into `toolchain.cmake`) or v5.x, under `C:\Program Files\Microchip\xc32\` |
| **CMake ≥ 4.1 + Ninja** | both on `PATH` (the supported build uses CMake presets + Ninja) |
| **MPLAB X / MDB** | required by `flash.py` (uses `mdb.bat`); any installed version is auto-discovered |
| **Device pack** | SAME54_DFP in `%USERPROFILE%\.mchp_packs` (installed via MPLAB X / MCC) |
| **Python 3.9+** | `pyserial` for the tool scripts (installed by setup) |
| **Terminal** | the board's EDBG virtual COM port, 115200 8N1 |

> The supported build path is `build.bat` (CMake/Ninja). The MPLAB X IDE build
> (`nbproject/` + `Makefile`) is **not** wired for the SOME/IP sources and won't
> link as-is; the IDE is kept for editing / MCC regeneration only.

### 4.2 One-time setup after cloning

Because every machine has different compiler versions, COM ports and tool
versions, a one-shot setup adapts the project locally. Connect the board via its
USB debugger port first, then:

```bat
git clone https://github.com/zabooh/lan866x-tools.git
cd lan866x-tools\firmware\t1s_100baset_bridge
setup.bat
```

`setup.bat` runs four independent steps (a failure in one is reported but does
not abort the rest):

| Step | Script | What it adapts |
|---|---|---|
| 1 | `requirements.txt` (pip) | install `pyserial` |
| 2 | **`setup_compiler.py`** | detect the installed **XC32 version** → writes `setup_compiler.config` and patches `toolchain.cmake` |
| 3 | **`setup_flasher.py`** | detect the **board's EDBG programmer + COM port** → writes `setup_flasher.config` |
| 4 | `setup_debug.py` | SAME54_DFP tool-pack fix (only needed for VS Code debugging) |

The two `.config` files are per-machine and **git-ignored**, so each clone
generates its own. You can re-run any step on its own (e.g. `python
setup_compiler.py` after installing a new XC32). The **MPLAB X version** needs no
setup step — `mdb_flash.py` auto-discovers the newest installed `mdb.bat`.

### 4.3 Build and flash

```bat
build.bat            :: incremental build  (build.bat rebuild = clean build, build.bat help)
python flash.py      :: program the board via MDB and release it from reset
```

`build.bat`:
1. reads the XC32 selection from `setup_compiler.config`,
2. configures with the CMake preset and builds with Ninja,
3. copies the resulting HEX into a tracked **`release/T1S_100BaseT_Bridge.hex`**,
4. prints a flash/RAM/heap/IRQ **build summary** (`build_summary.py`).

Because the HEX is committed under `release/`, a **fresh clone can flash without
building**:

```bat
python flash.py      :: flashes release/T1S_100BaseT_Bridge.hex
```

`flash.py` reads the programmer/COM assignment from `setup_flasher.config`,
locates `mdb.bat` from the newest installed MPLAB X, programs over SWD, then
issues `reset` + `run` so the MCU starts immediately (avoids leaving it held in
reset).

### 4.4 First bring-up checklist

1. `setup.bat` → `build.bat` → `python flash.py`.
2. Open the EDBG COM port at 115200 8N1; you should see the build banner.
3. `stats` — confirm `eth0`/`eth1` exist and counters move.
4. `plca_node` — should report node id **0**.
5. From the PC (on `192.168.0.200`): `ping 192.168.0.54` → 0% loss (bridge works).
6. `discovery` on the CLI **or** `lan866x-discovery.exe` on the PC → the endpoint
   appears. Then `diag`, `ledblink`, `clickdemo` as desired.

> **Peripheral wedge:** after hard-killed runs, the endpoint's `OpenSpi`/`OpenI2C`
> can start failing and a soft reboot won't clear it — power-cycle the endpoint.
> Pace control traffic; the Windows host can drop back-to-back RCP replies (the
> T1S link itself is excellent). See `../../docs/INTEGRATION_NOTES.md`.

---

## 5. Changing IP and PLCA configuration

The IP addresses (`eth0` = 192.168.0.180, `eth1` = 192.168.0.181) and the PLCA
parameters (node id 0, node count 8) can be changed two ways. **Editing the build
config is the only persistent method** — a runtime CLI change is convenient for
experiments but is forgotten on the next reset.

### 5.1 Persistent: edit the build config and rebuild

All defaults live in **`firmware/src/config/default/configuration.h`** (an
MCC-generated file). Edit the macros, then `build.bat` + `python flash.py`.

| Setting | Macro (`configuration.h`) | Default |
|---|---|---|
| eth0 (T1S) IP | `TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX0` | `"192.168.0.180"` |
| eth0 subnet mask | `TCPIP_NETWORK_DEFAULT_IP_MASK_IDX0` | `"255.255.255.0"` |
| eth0 gateway | `TCPIP_NETWORK_DEFAULT_GATEWAY_IDX0` | `"192.168.0.1"` |
| eth0 MAC | `TCPIP_NETWORK_DEFAULT_MAC_ADDR_IDX0` | `"00:04:25:01:02:01"` |
| eth1 (100BASE-T) IP | `TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX1` | `"192.168.0.181"` |
| eth1 subnet mask | `TCPIP_NETWORK_DEFAULT_IP_MASK_IDX1` | `"255.255.255.0"` |
| eth1 gateway | `TCPIP_NETWORK_DEFAULT_GATEWAY_IDX1` | `"192.168.0.1"` |
| eth1 MAC | `TCPIP_NETWORK_DEFAULT_MAC_ADDR_IDX1` | `"00:04:25:01:02:04"` |
| PLCA node id | `DRV_LAN865X_PLCA_NODE_ID_IDX0` | `0` (coordinator) |
| PLCA node count | `DRV_LAN865X_PLCA_NODE_COUNT_IDX0` | `8` |

The PLCA node-id macro is the single source of truth: `initialization.c` seeds
the LAN865x driver from it (`.nodeId = DRV_LAN865X_PLCA_NODE_ID_IDX0`) and `app.c`
uses it as the CLI default, so changing the macro is enough.

```bat
:: after editing configuration.h
build.bat
python flash.py
```

> **Keep `eth0` as PLCA node id 0** unless you deliberately move the coordinator
> role elsewhere — the bridge must coordinate the T1S bus. Keep both interfaces
> on the same subnet as the endpoint and the PC, since the MAC bridge makes them
> one L2 segment.
>
> **MCC note:** `configuration.h` is generated by MCC. A plain text edit +
> rebuild is fully supported. Only if you *re-run MCC code generation* will it be
> overwritten — in that case make the change in the MCC project (TCP/IP network
> config / LAN865x PLCA) instead.

### 5.2 Runtime via the CLI (not persistent)

The Harmony TCP/IP stack commands and the `Test` group let you change addressing
and PLCA on the fly. Run `netinfo` first to see the exact interface names
(`eth0`/`eth1`).

```text
netinfo                                   # show both interfaces, IPs, MACs, status
setip  eth0 192.168.0.190 255.255.255.0   # set eth0 IPv4 address + mask
setgw  eth0 192.168.0.1                    # set eth0 gateway
setip  eth1 192.168.0.191 255.255.255.0   # set eth1 IPv4 address + mask
plca_node 0                                # set PLCA node id (writes PLCA_CTRL1)
plca_node                                  # (no arg) read back the current node id
```

- `setip <interface> <ipv4> <mask>` and `setgw <interface> <ipv4>` are Harmony
  stack commands.
- `plca_node [id]` is in the `Test` group; with an argument it writes the
  LAN865x `PLCA_CTRL1` register (NODE_CNT:NODE_ID) live.

> ⚠️ **Runtime changes are volatile.** Anything set with `setip`/`setgw`/
> `plca_node` is lost on the next reset or power-cycle — the board boots back to
> the `configuration.h` defaults. **For a permanent change, edit the build config
> (§5.1) and reflash.** Use the CLI only to try a value before baking it in.

---

## 6. Port mirror: capturing the T1S bus in Wireshark

The `mirror` command turns the bridge into a SPAN/monitor port: it copies T1S
(`eth0`) traffic onto `eth1` so a PC running Wireshark on its Fast-Ethernet
adapter can see the two-wire bus. This is the primary way to debug SOME/IP / RCP
exchanges with the endpoint.

### 6.1 Why a mirror is needed

Two things are otherwise invisible to a PC capture on `eth1`:

1. **The endpoint's replies/offers** arrive on `eth0` and — because they are
   addressed to the bridge itself — are delivered *locally* by the MAC bridge, not
   forwarded onto `eth1`. So a plain `eth1` capture never shows them.
2. **The bridge's own requests** (SOME/IP FindService, RCP method calls) are sent
   *out* of `eth0` by the firmware. A node never receives its own transmissions,
   so no packet handler ever sees them either.

The mirror reconstructs both directions onto `eth1`, giving a complete picture of
the T1S conversation.

### 6.2 What gets mirrored (RX and TX paths)

| Path | Source | Implementation (`app.c` / `plat_h3tcpip.c`) | What it captures |
|---|---|---|---|
| **RX mirror** | bus → bridge | `mirror_eth0_to_eth1()`, called from `pktEth0Handler` | every frame the endpoint (or any T1S node) sends to the bridge — offers, replies, ARP — cloned verbatim onto `eth1` |
| **TX mirror** | bridge → bus | `mirror_udp_tx()`, called from `plat_udp_send()` | the firmware's own outgoing UDP (FindService + RCP requests), rebuilt as an Ethernet/IPv4/UDP frame and injected on `eth1` |

The original `eth0` frame is never altered — the RX mirror clones a fresh packet
for `eth1` and leaves the bus frame for normal local/bridge processing.

### 6.3 Using it

1. On the PC, start **Wireshark** on the Fast-Ethernet adapter connected to the
   bridge's `eth1` (RJ45 on the LAN8740 board).
2. On the board CLI: `mirror 1` (turn it on). `mirror` with no argument shows the
   current state; `mirror 0` turns it off.
3. Run a command that talks to the endpoint — e.g. `discovery`, `diag`, or any
   `lan866x` command — and watch the SOME/IP traffic appear in Wireshark.
4. A useful Wireshark display filter: `udp.port == 30490 || udp.port == 6800`
   (Service Discovery + RCP method endpoint), or `someip` if the dissector is on.

```text
mirror 1        # eth0(T1S) -> eth1 mirror: ON   (RX + the bridge's own TX)
discovery       # now visible on eth1 in Wireshark: FindService, offers, GetStatus...
mirror 0        # turn it off when done
```

### 6.4 Limitations

- **L2 addressing is best-effort** on the TX mirror: the destination MAC is taken
  from the ARP cache for unicast, the standard `01:00:5E:xx:xx:xx` mapping for
  IPv4 multicast, or broadcast if unresolved. Wireshark dissects the IP/UDP
  payload correctly regardless; only the Ethernet destination may be approximate.
- **UDP checksum is sent as 0** (not computed) on mirrored TX frames — valid for
  IPv4, but Wireshark may flag "checksum unverified".
- The TX mirror only handles datagrams up to ~1400 B of payload; the RX mirror
  copies frames up to the 1518-byte Ethernet MTU.
- Mirroring adds one cloned `eth1` transmit per relevant frame. It is meant for
  diagnostics — leave it **off** for normal bridging to avoid the extra load.
- Mirror state is a runtime toggle (like the §5.2 CLI settings) and defaults to
  **off** on every boot.
