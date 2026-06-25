-- lan866x_ntp.lua - Wireshark dissector for the LAN866x-tools software NTP
-- time-sync protocol (firmware src/ntp_sync.c <-> host tool lan866x-ntpsync).
--
-- Wire protocol - UDP port 30491, all integers big-endian, signed 64-bit ns:
--   0x01 REQUEST    PC->FW : [op][t1]                 (t1 = PC send time)
--   0x02 REPLY      FW->PC : [op][t1][t2][t3]         (t2 = FW recv, t3 = FW send)
--   0x03 SET_OFFSET PC->FW : [op][adjust][delay]      (FW: offset_ns += adjust)
--   0x04 SET_ACK    FW->PC : [op][now]                (now = FW NTP time after set)
--   0x05 TAP_SET    PC->FW : [op][enable:1][port:2]   (set/clear eth0-timestamp tap)
--   0x06 TAP_REC    FW->PC : [op][dir][t:8][ipid:2][proto][iplen:2][src:4][dst:4]
--
-- Install: copy this file into your Wireshark "Personal Lua Plugins" folder
--   (Help -> About Wireshark -> Folders), then Analyze -> Reload Lua Plugins
--   (Ctrl+Shift+L). It binds to UDP 30491 automatically - no "Decode As" needed.

local p_ntp = Proto("lan866x_ntp", "LAN866x Software NTP")

local NTP_PORT = 30491

local OP = {
    [0x01] = "REQUEST",
    [0x02] = "REPLY",
    [0x03] = "SET_OFFSET",
    [0x04] = "SET_ACK",
    [0x05] = "TAP_SET",
    [0x06] = "TAP_REC",
}

local DIR = { [0] = "RX (from T1S bus)", [1] = "TX (to T1S bus)" }

local f = p_ntp.fields
f.op     = ProtoField.uint8 ("lan866x_ntp.op",     "Op",            base.HEX, OP)
f.t1     = ProtoField.int64 ("lan866x_ntp.t1",     "t1 (PC send)",        base.DEC)
f.t2     = ProtoField.int64 ("lan866x_ntp.t2",     "t2 (FW recv)",        base.DEC)
f.t3     = ProtoField.int64 ("lan866x_ntp.t3",     "t3 (FW send)",        base.DEC)
f.adjust = ProtoField.int64 ("lan866x_ntp.adjust", "adjust (offset += )", base.DEC)
f.delay  = ProtoField.int64 ("lan866x_ntp.delay",  "delay (PC RTT)",      base.DEC)
f.now    = ProtoField.int64 ("lan866x_ntp.now",    "now (FW NTP time)",   base.DEC)
f.enable = ProtoField.uint8 ("lan866x_ntp.enable", "tap enable",          base.DEC)
f.port   = ProtoField.uint16("lan866x_ntp.port",   "collector port",      base.DEC)
f.dir    = ProtoField.uint8 ("lan866x_ntp.dir",    "direction",     base.DEC, DIR)
f.tstamp = ProtoField.int64 ("lan866x_ntp.t",      "timestamp (NTP ns)",  base.DEC)
f.ipid   = ProtoField.uint16("lan866x_ntp.ipid",   "IP id",         base.HEX)
f.proto  = ProtoField.uint8 ("lan866x_ntp.proto",  "IP proto",            base.DEC)
f.iplen  = ProtoField.uint16("lan866x_ntp.iplen",  "IP total length",     base.DEC)
f.src    = ProtoField.ipv4  ("lan866x_ntp.src",    "tapped src")
f.dst    = ProtoField.ipv4  ("lan866x_ntp.dst",    "tapped dst")
f.human  = ProtoField.string("lan866x_ntp.human",  "decoded")

local NS = Int64(1000000000)

-- A signed ns *duration* -> "-1.234 ms" style (mirrors the firmware fmt_dur()).
local function dur_str(v)
    local neg = v < Int64(0)
    local sg = neg and "-" or ""
    local a = (neg and (Int64(0) - v) or v):tonumber()
    -- integer math (locale-proof: always '.' as separator, like the firmware)
    if     a < 1e3 then return string.format("%s%d ns", sg, math.floor(a))
    elseif a < 1e6 then return string.format("%s%d.%03d us", sg, math.floor(a/1e3), math.floor(a) % 1000)
    elseif a < 1e9 then return string.format("%s%d.%03d ms", sg, math.floor(a/1e6), math.floor(a/1e3) % 1000)
    else                return string.format("%s%d.%03d s",  sg, math.floor(a/1e9), math.floor(a/1e6) % 1000) end
end

-- A ns *timestamp* -> "<sec>.<frac> s" and, when it looks like a real Unix epoch
-- (post-sync, sec > ~2001), also the UTC wall-clock time.
local function ts_str(v)
    local sec  = (v / NS):tonumber()
    local frac = (v % NS):tonumber()
    if frac < 0 then frac = -frac end
    local s = string.format("%d.%09d s", math.floor(sec), math.floor(frac))
    if sec > 1000000000 then   -- plausible epoch -> add readable UTC
        s = s .. os.date("!  (%Y-%m-%d %H:%M:%S", math.floor(sec))
              .. string.format(".%03d UTC)", math.floor(frac / 1000000))
    else
        s = s .. "  (uptime, pre-sync)"
    end
    return s
end

local function add_ns(tree, field, tvb_range, fmt)
    local item = tree:add(field, tvb_range)
    item:append_text("  =  " .. fmt(tvb_range:int64()))
    return item
end

function p_ntp.dissector(tvb, pinfo, tree)
    local len = tvb:len()
    if len < 1 then return 0 end
    local op = tvb(0,1):uint()
    local name = OP[op]
    if name == nil then return 0 end          -- not ours; let other dissectors try

    pinfo.cols.protocol = "LAN866x-NTP"
    local t = tree:add(p_ntp, tvb(), "LAN866x Software NTP, Op: " .. name)
    t:add(f.op, tvb(0,1))

    local info = name
    if op == 0x01 and len >= 9 then                       -- REQUEST
        add_ns(t, f.t1, tvb(1,8), ts_str)
        info = "REQUEST    t1=" .. ts_str(tvb(1,8):int64())

    elseif op == 0x02 and len >= 25 then                  -- REPLY
        add_ns(t, f.t1, tvb(1,8),  ts_str)
        add_ns(t, f.t2, tvb(9,8),  ts_str)
        add_ns(t, f.t3, tvb(17,8), ts_str)
        -- FW turnaround t3-t2 is fully inside this one packet -> show it
        local turn = tvb(17,8):int64() - tvb(9,8):int64()
        t:add(f.human, tvb(9,16), "FW turnaround (t3-t2)"):append_text(" = " .. dur_str(turn))
        info = "REPLY      FW turnaround " .. dur_str(turn)

    elseif op == 0x03 and len >= 9 then                   -- SET_OFFSET
        add_ns(t, f.adjust, tvb(1,8), dur_str)
        info = "SET_OFFSET adjust=" .. dur_str(tvb(1,8):int64())
        if len >= 17 then
            add_ns(t, f.delay, tvb(9,8), dur_str)
            info = info .. "  delay=" .. dur_str(tvb(9,8):int64())
        end

    elseif op == 0x04 and len >= 9 then                   -- SET_ACK
        add_ns(t, f.now, tvb(1,8), ts_str)
        info = "SET_ACK    now=" .. ts_str(tvb(1,8):int64())

    elseif op == 0x05 and len >= 4 then                   -- TAP_SET
        t:add(f.enable, tvb(1,1))
        t:add(f.port,   tvb(2,2))
        info = string.format("TAP_SET    %s port=%d",
            tvb(1,1):uint() ~= 0 and "ENABLE" or "disable", tvb(2,2):uint())

    elseif op == 0x06 and len >= 23 then                  -- TAP_REC
        t:add(f.dir,    tvb(1,1))
        add_ns(t, f.tstamp, tvb(2,8), ts_str)
        t:add(f.ipid,   tvb(10,2))
        t:add(f.proto,  tvb(12,1))
        t:add(f.iplen,  tvb(13,2))
        t:add(f.src,    tvb(15,4))
        t:add(f.dst,    tvb(19,4))
        info = string.format("TAP_REC    %s  %s -> %s  t=%s",
            (DIR[tvb(1,1):uint()] or "?"),
            tostring(tvb(15,4):ipv4()), tostring(tvb(19,4):ipv4()),
            ts_str(tvb(2,8):int64()))
    end

    pinfo.cols.info = info
    return len
end

-- Bind to UDP 30491 (matches either src or dst port, so both directions decode).
DissectorTable.get("udp.port"):add(NTP_PORT, p_ntp)
