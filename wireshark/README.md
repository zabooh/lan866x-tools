# Wireshark setup for LAN866x / T1S SOME/IP analysis

This folder bundles the Wireshark add-ons needed to **decode the protocols that
travel on the 10BASE-T1S bus** between this toolset (or the bridge firmware) and a
LAN866x endpoint:

| Protocol | What it is | Wireshark add-on here |
|---|---|---|
| **SOME/IP (RCP, service `0xFF10`)** | the remote-control RPC the `lan866x-*` tools speak | `SOMEIP/` — config for Wireshark's **built-in** SOME/IP dissector |
| **DNCP** | Dynamic Node Configuration Protocol (PLCA node enumeration) | `DNCP/proto_dncp.lua` — a Lua dissector |
| **RTP / RFC4175 (ST 2110-20)** | the uncompressed video stream to the display endpoints (UDP 5001) | `RTP-RFC4175/ST2110-20.lua` — a Lua dissector |

With these installed, a SOME/IP `GetStatus` reply shows named fields
(`ActiveApplication`, `ChipIdentifier`, `TOCounter [uint32] …`) instead of raw
"Unparsed Payload" hex, DNCP Announce/Registry packets are decoded, and the RGB
video frames are dissected as ST 2110-20.

> **Prerequisite:** Wireshark **3.4 or newer** (the version that ships the SOME/IP
> dissector with WTLV support), built **with Lua** (check *Help → About Wireshark*
> — it should mention Lua, and *Plugins* should be available).

There are two different install locations, and that trips people up:

- **SOME/IP** config files go in the **Personal configuration** directory.
- **Lua** dissectors (DNCP, RTP) go in the **Personal Lua Plugins** directory.

Both directories are shown in **Help → About Wireshark → Folders**. **Restart
Wireshark** after installing either — both are read only at startup.

---

## 1. SOME/IP dissector (the main one)

The `SOMEIP/` files teach the built-in SOME/IP dissector the LAN866x service map
(service `0xFF10` = `LAN866X`, method `0x1002` = `GetStatus`, …) and the per-message
field layout (including the WTLV data-id overrides the LAN866x API uses).

### Install

**Windows** — run the bundled installer (copies the 8 files to `%APPDATA%\Wireshark\`):
```bat
cd wireshark\SOMEIP
install.bat
```
**Linux / macOS** (installs to `~/.config/wireshark/`):
```sh
cd wireshark/SOMEIP
./install.sh          # or: WIRESHARK_CONFIG_DIR=/path ./install.sh
```
**Manual** — copy these 8 files into the **Personal configuration** dir
(*Help → About Wireshark → Folders → Personal configuration*; Windows
`%APPDATA%\Wireshark\`, Linux/macOS `~/.config/wireshark/`):
`SOMEIP_service_identifiers`, `SOMEIP_method_event_identifiers`,
`SOMEIP_eventgroup_identifiers`, `SOMEIP_parameter_base_types`,
`SOMEIP_parameter_strings`, `SOMEIP_parameter_arrays`,
`SOMEIP_parameter_structs`, `SOMEIP_parameter_list`.

Then **restart Wireshark**. (See `SOMEIP/README.md` for a per-file description.)

### Tell the dissector which UDP ports are SOME/IP

If SOME/IP packets are not recognized, set the ports under
**Edit → Preferences → Protocols → SOME/IP → SOME/IP Ports** (or *Decode As → SOME/IP*):

| UDP port | Role |
|---|---|
| **30490** | Service Discovery (multicast `224.0.0.1`) |
| **6800** | RCP method endpoint (requests) |
| **49153** | source port the endpoint replies from |

### Verify

1. Open the bundled capture `SOMEIP/example-capture.pcapng` (or capture live — see
   §4), select a **GetStatus (0x1002)** or **GetNetworkStatus (0x1600)** response.
2. Expand **SOME/IP Protocol → (struct payload)** → you should see named fields.
3. From the shell:
   ```sh
   tshark -r SOMEIP/example-capture.pcapng \
          -Y "someip.methodid==0x1002 && someip.messagetype==0x80" -V | head -40
   ```

---

## 2. DNCP dissector (Lua)

`DNCP/proto_dncp.lua` decodes DNCP (Announce / Registry / …) on **UDP 65526/65527**
— the same packets the `lan866x-dncpmon` / `dncpdisc` tools and the firmware `dncp`
commands produce.

**Install:** copy `proto_dncp.lua` into the **Personal Lua Plugins** dir
(*Help → About Wireshark → Folders → Personal Lua Plugins*; typically
`%APPDATA%\Wireshark\plugins\` on Windows, `~/.local/lib/wireshark/plugins/` on
Linux). Restart Wireshark; *Help → About Wireshark → Plugins* should list it.

---

## 3. RTP / RFC4175 video dissector (Lua)

`RTP-RFC4175/ST2110-20.lua` (SMPTE ST 2110-20, by Thomas Edwards — GPL) dissects the
uncompressed RGB video the lighting endpoint renders, carried as **RTP on UDP 5001**
(`lan866x-clickdemo` / `lan866x-video`, and the firmware `clickdemo` / `video`).

**Install:** copy `ST2110-20.lua` into the **Personal Lua Plugins** dir (as in §2),
restart. Then:
- **Edit → Preferences → Protocols → ST2110-20** → set *dynamic payload type* `96`.
- Right-click a UDP/5001 packet → **Decode As… → RTP**; the RTP payload is then shown
  as ST 2110-20 video. (See `RTP-RFC4175/README.md` for the original notes.)

---

## 4. Where to capture the T1S traffic

The bus is two-wire 10BASE-T1S, so you can't put a normal NIC on it directly. Two
practical capture points:

- **Through the bridge's SPAN/mirror:** on the T1S↔100BASE-T bridge firmware run
  `mirror 1`, then capture on the PC's Ethernet adapter connected to the bridge's
  `eth1`. The bridge clones the bridge↔endpoint conversation (SOME/IP, ARP, ICMP,
  DNCP, RTP) onto `eth1` — see `firmware/t1s_100baset_bridge/README.md` §6.
- **On the host running the tools:** capture on the network interface the
  T1S-USB adapter presents, while `lan866x-*` tools talk to the endpoint.

### Handy display filters

```
someip                                   # all SOME/IP
someip.serviceid == 0xff10               # the LAN866x RCP service
udp.port == 30490 || udp.port == 6800    # SD + RCP method endpoint
dncp                                     # DNCP (with the lua dissector)
udp.port == 5001                         # the RTP video stream
```

---

## 5. Python tooling (live sniff + automated report)

Two Python helpers in this folder drive capture/analysis from the command line.

```bash
pip install pyshark matplotlib pyserial     # tshark + Npcap must already be installed
```

### `sniff_someip.py` — live SOME/IP sniffer
A pyshark live sniffer that decodes RCP methods by name (GetStatus, SetGpio, …),
reusing Wireshark's dissectors. The RCP ports (6800/49153/30490) are forced to
SOME/IP via Decode-As, so it works even without the port preference set.
```bash
python sniff_someip.py --iface "Ethernet 8" --ip 192.168.0.54
```

### `t1s_report.py` — capture + timing/protocol HTML report
Orchestrates an end-to-end measurement and renders a self-contained
**`report.html`** with timing diagrams:
1. enables the bridge **port mirror** (`mirror 1` on the board, over `--port`);
2. runs `discovery`, `diag` and `gpiomax` on the board while **tshark** captures
   the mirrored T1S traffic on `--iface`;
3. analyses each scenario — **request→response round-trip times** (per SOME/IP
   method), inter-packet timing, throughput, and the **protocol/method mix**;
4. writes `report.html` (RTT-over-time, RTT histogram and protocol-mix diagrams
   embedded as PNGs, plus per-method RTT tables); disables the mirror.
```bash
python t1s_report.py --iface "Ethernet 8" --port COM8 --ip 192.168.0.54
# offline (analyse an existing capture, no board needed):
python t1s_report.py --pcap captures/diag.pcapng --out report.html
```
Live capture needs **Npcap + admin**; the board must be reachable through the
bridge on `--iface` (run with the PC on the bridge's `eth1`). Per-scenario `.pcapng`
files land in `captures/` and `report.html` in this folder (both git-ignored).

A committed sample run is in **[`example-report.html`](example-report.html)** —
open it in a browser to see the layout (timing diagrams + per-method RTT tables)
without any hardware. It captured 8 / 63 / 7485 RCP round-trips for
discovery / diag / gpiomax.

### `bridge_delay.py` — measure the bridge's one-way forwarding delay
Uses the NTP-synced firmware clock + the eth0 timestamp tap to measure how long a
frame takes **through the bridge** in each direction. It keeps `lan866x-ntpsync`
running **continuously** (so the firmware clock stays disciplined — a one-shot sync
drifts), **calibrates** the constant skew `S` between the NIC capture clock (Npcap)
and the sync clock by self-stamping outgoing frames, enables the firmware eth0 tap
(every IPv4 frame on the T1S side is stamped with the synced NTP time and streamed
to the PC), captures the same frames at the NIC, runs `lan866x-discovery` to
generate traffic, and matches the two by IPv4 id (removing `S`):
```bash
python bridge_delay.py --iface "Ethernet 8" --src-ip 192.168.0.200 \
       --bridge 192.168.0.181 --endpoint 192.168.0.54
```
```text
  calibrated capture-vs-sync clock skew S : -0.6 us  (60 frames)
  -> exact one-way bridge delay (skew removed, NO symmetry assumption):
       PC -> endpoint :    699.9 us
       endpoint -> PC :     86.4 us
     cross-check round-trip (mf+mr, S-independent) : 786.3 us
```
The calibration finds `S ≈ 0` (Npcap and the sync clock are effectively the same
clock), which rules out a capture-clock offset and proves the only thing that can
bias the result is firmware **drift** — hence the continuous re-sync. The result is
**asymmetric**: PC→endpoint (eth0 egress, through the SPI MAC-PHY) ≈ 0.5–0.7 ms
dominates; endpoint→PC ≈ 0–0.1 ms sits within the software-NTP floor. `--src-ip` is
the capture NIC's own address (so calibration frames egress that NIC when several
share the subnet); `--no-calibrate` reports only the skew-free round-trip. The
firmware stamp is taken in the eth0 packet hook (no UART in the timing path), so
console output does not perturb the measurement. **Full write-up + analysis:
[NTP_TIMING.md](NTP_TIMING.md).**

> 📖 Software NTP write-ups: **[NTP_SYNC.md](NTP_SYNC.md)** (implementation + how the
> convergence works, with a real run analysed), **[NTP_TIMING.md](NTP_TIMING.md)**
> (usage + this bridge-delay test), and
> **[NTP_TWO_NODE_CONVERGENCE.md](NTP_TWO_NODE_CONVERGENCE.md)** (theory).

---

## Troubleshooting

- **Fields still show "Unparsed Payload":** confirm the 8 `SOMEIP_*` files are in
  the exact *Personal configuration* dir, then **restart** Wireshark (UAT tables
  load only at startup). Needs Wireshark ≥ 3.4.
- **"Error loading table 'SOME/IP Parameter …'" at startup:** a `SOMEIP_*` file got
  mangled (line endings / quotes). Re-run the installer to re-copy clean files.
- **Lua dissector not active:** it must be in *Personal Lua Plugins* (not the config
  dir); check *About Wireshark → Plugins* lists it; Wireshark must be Lua-enabled.
- **No SOME/IP traffic at all:** check you're capturing at a point that sees the bus
  (§4) and that the SOME/IP ports are configured (§1).

## Provenance & licensing

- `SOMEIP/SOMEIP_*` — generated by Microchip's SOME/IP generator for the LAN866x
  service; used as-is (see `SOMEIP/README.md`).
- `DNCP/proto_dncp.lua` — Microchip DNCP dissector.
- `RTP-RFC4175/ST2110-20.lua` — © Thomas Edwards, **GPL v2+** (see the header in the
  file). Distributed here under its own licence.
- `SOMEIP/example-capture.pcapng`, `RTP-RFC4175/ScreenShot.png` — example assets.
