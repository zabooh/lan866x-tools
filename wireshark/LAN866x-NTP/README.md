# LAN866x Software-NTP dissector

Wireshark Lua dissector for the **software NTP time-sync protocol** used between
the bridge firmware (`firmware/.../src/ntp_sync.c`) and the host tool
`lan866x-ntpsync` (`ntpsync.c`). It decodes the t1/t2/t3/t4 exchange, the
`SET_OFFSET` discipline messages, and the eth0 timestamp-tap records.

- File: [`lan866x_ntp.lua`](lan866x_ntp.lua)
- Transport: **UDP port 30491**, all integers **big-endian**, signed **64-bit ns**.

## Wire protocol

| Op | Name | Dir | Payload after the 1-byte op |
|----|------|-----|------------------------------|
| `0x01` | REQUEST    | PC→FW | `t1` (PC send time) — 9 B |
| `0x02` | REPLY      | FW→PC | `t1` `t2` `t3` (echo, FW recv, FW send) — 25 B |
| `0x03` | SET_OFFSET | PC→FW | `adjust` `delay` (offset += adjust; PC-measured RTT) — 17 B |
| `0x04` | SET_ACK    | FW→PC | `now` (FW NTP time after the set) — 9 B |
| `0x05` | TAP_SET    | PC→FW | `enable:1` `port:2` (set/clear eth0 tap, collector port) — 4 B |
| `0x06` | TAP_REC    | FW→PC | `dir` `t:8` `ipid:2` `proto` `iplen:2` `src:4` `dst:4` — 23 B |

The dissector shows each 64-bit ns value both raw and human-readable: **durations**
(`adjust`, `delay`, FW turnaround `t3-t2`) as `ns/us/ms/s`, **timestamps**
(`t1/t2/t3/now/t`) as `<sec>.<ns>` plus the UTC wall-clock once the counter is
PC-synced (epoch). Each field is filterable, e.g. `lan866x_ntp.op == 0x03`,
`lan866x_ntp.delay > 5000000`, `lan866x_ntp.dir == 1`.

## Install

1. **Check Lua** — `Help → About Wireshark`, the *Wireshark* tab must say it was
   compiled "with Lua".
2. **Find the plugin folder** — `Help → About Wireshark → Folders →` **Personal Lua
   Plugins**. On Windows this is `%APPDATA%\Wireshark\plugins\`.
3. **Copy** `lan866x_ntp.lua` into that folder.
4. **Load it** — restart Wireshark, or `Analyze → Reload Lua Plugins` (Ctrl+Shift+L).
   `Help → About Wireshark → Plugins` should now list `lan866x_ntp.lua`.

That's all — the dissector **binds to UDP 30491 automatically**, so no "Decode As"
is needed. Filter on `lan866x_ntp` to see only the time-sync traffic.

> If the firmware ever uses a different port (`lan866x-ntpsync --port <n>`), change
> the `NTP_PORT` constant at the top of the `.lua` and reload.
