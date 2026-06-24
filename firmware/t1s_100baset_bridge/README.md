# t1s_100baset_bridge

A **10BASE-T1S ↔ 100BASE-T Layer-2 bridge** on an ATSAME54P20A + LAN865x.
The bridge node is the **PLCA coordinator (node id 0)** and bridges the T1S bus
(`eth0`/LAN865x) to standard Ethernet (`eth1`/GMAC), so a PC on `eth1` can reach
a LAN866x endpoint on the T1S bus.

On top of the bridge it embeds the **LAN866x SOME/IP (RCP) client** from
`lan866x-tools`, exposing the host tools as on-board CLI commands.

> This is a vendored copy under `lan866x-tools/`. PTP grandmaster/follower
> support has been removed here and lives on in the newer
> [`net_10base_t1s`](https://github.com/zabooh/net_10base_t1s) project.

## Prerequisites

| Requirement | Notes |
|---|---|
| **MPLAB XC32** | v4.60 (baked into `toolchain.cmake`) or v5.x, under `C:\Program Files\Microchip\xc32\` |
| **CMake ≥ 4.1 + Ninja** | on `PATH` |
| **MPLAB X / MDB** | required by `flash.py` (uses `mdb.bat`) |
| **Python 3.9+** | `pyserial` for the tool scripts |
| **Terminal** | the board's EDBG virtual COM port, 115200 8N1 |

## Tool Setup (once per machine)

```bat
install_dependencies.bat       :: pyserial (python deps)
python setup_compiler.py       :: pick the installed XC32 version (writes setup_compiler.config)
python setup_flasher.py        :: detect + save the bridge board's EDBG (setup_flasher.config)
python setup_debug.py          :: SAME54_DFP tool-pack fix for VS Code debugging
```

Both `.config` files are per-machine and git-ignored.

## Build & Flash

```bat
build.bat            :: incremental build (build.bat rebuild = clean build)
python flash.py      :: program the board via MDB
```

`build.bat` builds with cmake/Ninja, copies the HEX to `release/`, and prints a
flash/RAM/heap/IRQ **build summary**. A pre-built `release/T1S_100BaseT_Bridge.hex`
is committed, so a **fresh clone can flash without building**:

```bat
python flash.py      :: flashes release/T1S_100BaseT_Bridge.hex
```

> The supported build path is `build.bat` (cmake/Ninja). The MPLAB X IDE build
> (`nbproject/` + `Makefile`) is **not** wired for the SOME/IP sources and won't
> link as-is; the IDE is kept for editing/MCC only.

## CLI

Connect a terminal to the board's EDBG virtual COM port (115200 8N1). The
`lan866x` command group mirrors the host tools against the endpoint over T1S:

| Command | Mirrors | Description |
|---|---|---|
| `discovery` | `lan866x-discovery.exe` | list endpoints + full status |
| `diag [probes]` | `lan866x-diag.exe` | T1S link diagnostics + RCP-goodput estimate |
| `ledblink [laps] [ms]` | `lan866x-ledblink.exe` | LED running light (PA02/06/10) |
| `clickdemo [s] [fps]` | `lan866x-clickdemo.exe` | Thumbstick+Proximity → RGB displays (Ctrl-C/`q` to stop) |

Run `discovery` first to populate the endpoint list.

Bridge / diagnostic commands (`Test` group, type the name directly):

| Command | Description |
|---|---|
| `mirror [0\|1]` | SPAN: copy T1S (eth0) traffic — RX **and** the bridge's own TX — to eth1 for Wireshark |
| `fwd [0\|1]` | manual eth0→eth1 forward (the stack MAC bridge does L2 bridging anyway) |
| `ipdump [0..3]` | dump RX frames (1=eth0, 2=eth1, 3=both) |
| `plca_node [id]` | get/set PLCA node id (0 = coordinator) |
| `stats` | per-interface TX/RX counters |
| `lan_read/lan_write <addr> [val]` | LAN865x register access |
| `netinfo`, `bridge`, `ping` | Harmony TCP/IP stack commands |
