# LAN866X Wireshark SOME/IP dissector configuration

These files teach Wireshark's built-in **SOME/IP** dissector how to decode
messages exchanged with the LAN866X SOME/IP service (service ID `0xFF10`).
With them installed, every RPC request/response and event payload shows up
with named fields — `ActiveApplication: "main/app.bin"`, `HandleI2C=41248`,
`TOCounter [uint32]: 60602223` — instead of raw hex "Unparsed Payload".

## Install

### Windows

Double-click **`install.bat`** (or run it from a terminal). It copies the 8
SOMEIP\_\* files into `%APPDATA%\Wireshark\`. Restart Wireshark afterwards.

### Linux / macOS

```sh
./install.sh
```

The script installs to `~/.config/wireshark/`. Override the target with:

```sh
WIRESHARK_CONFIG_DIR=/path/to/wireshark/config ./install.sh
```

### Manual install

Copy these 8 files into your Wireshark personal configuration directory:

- `SOMEIP_service_identifiers`
- `SOMEIP_method_event_identifiers`
- `SOMEIP_eventgroup_identifiers`
- `SOMEIP_parameter_base_types`
- `SOMEIP_parameter_strings`
- `SOMEIP_parameter_arrays`
- `SOMEIP_parameter_structs`
- `SOMEIP_parameter_list`

You can find that directory in Wireshark under **Help → About Wireshark →
Folders → Personal configuration**. Typical locations:

| OS | Path |
|---|---|
| Windows | `%APPDATA%\Wireshark\` |
| Linux | `~/.config/wireshark/` |
| macOS | `~/.config/wireshark/` |

Restart Wireshark after copying.

## Verify it works

1. Open any LAN866X SOME/IP capture (or live-capture on the interface the
   device is reachable on — SOME/IP discovery uses UDP port 30490).
2. Select a response packet for **GetStatus (0x1002)** or
   **GetNetworkStatus (0x1600)**.
3. Expand **SOME/IP Protocol → struct payload**. You should see field names
   such as `ActiveApplication`, `ChipIdentifier`, `EndpointIpV4Address`,
   `TOCounter`, etc. If the dissector is not loaded, the payload body
   displays as generic "Unparsed Payload" hex chunks.

You can also confirm from the command line:

```sh
tshark -r your-capture.pcapng \
       -Y "someip.methodid==0x1002 and someip.messagetype==0x80" -V | head -40
```

## Troubleshooting

**"Error loading table 'SOME/IP Parameter …'" when Wireshark starts.**
Usually means one of the CSV files got mangled (wrong line endings, opened
and saved in a text editor that stripped quotes, etc.). Re-run the installer
or re-copy the files from this directory.

**Fields still show as "Unparsed Payload".**
1. Check **Help → About Wireshark → Folders → Personal configuration** and
   confirm the SOMEIP\_\* files landed in that exact directory.
2. Restart Wireshark — the UAT tables are only read at startup.
3. Make sure you are running a Wireshark version that ships the SOME/IP
   dissector with WTLV support — version **3.4 or later**.

**Capture has no SOME/IP traffic at all.**
Verify the UDP port configuration of the SOME/IP dissector: **Edit →
Preferences → Protocols → SOME/IP → SOME/IP Ports**. The LAN866X service
uses discovery on UDP/30490 and method endpoints on UDP/6800.

## Regenerating these files

The 8 files are generated from the LAN866x SOME/IP definition by Microchip's
SOME/IP generator. The generator is **not** bundled in this toolset; the files
here are the published output and are used as-is. Treat them as generated
artifacts (manual edits may be overwritten if regenerated upstream).

## What each file contains

| File | Purpose |
|---|---|
| `SOMEIP_service_identifiers` | Maps service ID `0xFF10` to the name `LAN866X`. |
| `SOMEIP_method_event_identifiers` | Maps each method/event ID (e.g. `0x1002`) to its name (e.g. `GetStatus`). |
| `SOMEIP_eventgroup_identifiers` | Names the LAN866X event groups (`0x2000 LAN866X_Events`, `0x2001 LAN866X_TDEvents`). |
| `SOMEIP_parameter_base_types` | Defines `uint8`/`uint16`/`uint32`/`uint64` and signed variants. |
| `SOMEIP_parameter_strings` | Encoding metadata for every string field. |
| `SOMEIP_parameter_arrays` | Metadata for every repeated byte-array field. |
| `SOMEIP_parameter_structs` | The per-message field layout, including WTLV data-id overrides (e.g. `GetNetworkStatus` uses grouped IDs `0x100/0x150/0x200/0x300/0x400/0x500` per the LAN866x API Catalogue). |
| `SOMEIP_parameter_list` | Top-level entry: which struct to use for each method request, response, and event. |
