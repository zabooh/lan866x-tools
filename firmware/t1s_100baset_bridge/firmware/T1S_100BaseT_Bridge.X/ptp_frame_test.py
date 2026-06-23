#!/usr/bin/env python3
"""
ptp_frame_test.py
-----------------
Vereinfachter PTP-Frame-Test: Überprüft ob PTP-Sync-Frames (EtherType 0x88F7)
vom Grandmaster zum Follower übertragen werden.

Keine Register-Zugriffe (lan_read/lan_write) — nur Board-Reset, IP, Ping,
PTP-Aktivierung, ipdump lauschen, Frame-Validierung.

Schritte:
  1  Board-Reset + 8 s Wartezeit
  2  IP setzen (GM=192.168.0.20, FOL=192.168.0.30)
  3  Ping-Test beidseitig
    4  NoIP-Sendetest (GM -> Follower)
    5  PTP aktivieren (follower + master)
    6  ipdump 1 auf Follower — PTP-Frames (0x88F7) lauschen + DumpMem-Ausgabe
    7  Frames parsen + validieren (DST-MAC, EtherType, msgType, Version)
"""

import serial
import time
import re
import sys
import argparse

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
GM_PORT  = "COM10"
FOL_PORT = "COM8"
BAUDRATE = 115200

GM_IP    = "192.168.0.20"
FOL_IP   = "192.168.0.30"
NETMASK  = "255.255.255.0"
IFACE    = "eth0"

LISTEN_S   = 12.0   # Lauschzeit auf PTP-Frames beim Follower (s)
MIN_FRAMES = 3      # Mindestanzahl erwarteter PTP-Frames
NOIP_LISTEN_S = 4.0
NOIP_COUNT    = 5
NOIP_GAP_MS   = 5
EXPECTED_ETHERTYPE = 0x88F7

PROMPT_MARKERS = ["\r\n> ", "\n> ", "> "]

# ---------------------------------------------------------------------------
# Serielle Hilfsfunktionen
# ---------------------------------------------------------------------------

def open_port(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUDRATE, timeout=2)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"[{port}] Verbunden")
    return ser


def wake(ser: serial.Serial, name: str) -> None:
    ser.write(b"\r\n")
    time.sleep(0.3)
    ser.reset_input_buffer()
    print(f"[{name}] Prompt bereit")


def _is_response_complete(response: str, cmd_lower: str) -> bool:
    if len(response) < 10:
        return False
    if "lan_read" in cmd_lower:
        m = re.search(r'LAN865X Read OK: Addr=0x[0-9A-Fa-f]+ Value=0x[0-9A-Fa-f]+',
                      response, re.IGNORECASE)
        if m:
            tail = response[m.end():]
            return '\n' in tail or '\r' in tail
        return False
    if "lan_write" in cmd_lower:
        m = re.search(r'LAN865X Write (?:OK|failed|timeout)',
                      response, re.IGNORECASE)
        if m:
            tail = response[m.end():]
            return '\n' in tail or '\r' in tail
        return False
    # Fallback: traditionelle Prompt-Erkennung
    return any(response.endswith(p) for p in PROMPT_MARKERS)


def send_cmd(ser: serial.Serial, name: str, cmd: str,
             timeout: float = 5.0) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    print(f"[{name}] >> {cmd}")
    resp = ""
    deadline = time.time() + timeout
    cmd_lower = cmd.lower()
    while time.time() < deadline:
        available = ser.in_waiting
        if available > 0:
            chunk = ser.read(available).decode("utf-8", errors="ignore")
            resp += chunk
            if _is_response_complete(resp, cmd_lower):
                break
        time.sleep(0.0005)  # 0.5ms Polling
    out = "\n".join(l for l in resp.splitlines() if cmd not in l).strip()
    if out:
        print(f"[{name}] << {out}")
    return resp


def send_ping(ser: serial.Serial, name: str, target: str,
              count: int = 4, timeout: float = 15.0) -> bool:
    """Sendet ping und wartet auf 'Ping: done.'"""
    ser.reset_input_buffer()
    cmd = f"ping {target} n {count}"
    ser.write((cmd + "\r\n").encode())
    print(f"[{name}] >> {cmd}")
    resp = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            resp += chunk
            for line in chunk.splitlines():
                s = line.strip()
                if s and cmd not in s:
                    print(f"  [{name}] {s}")
            if "Ping: done." in resp:
                break
        else:
            time.sleep(0.05)
    m = re.search(r'received (\d+) replies', resp)
    rx = int(m.group(1)) if m else 0
    ok = rx > 0
    print(f"  → {'✓ PASS' if ok else '✗ FAIL'}: {rx}/{count} Replies von {target}")
    return ok


def capture_async(ser: serial.Serial, name: str, duration_s: float) -> str:
    """Liest alle Daten für duration_s Sekunden und gibt sie zurück."""
    buf = ""
    deadline = time.time() + duration_s
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            buf += chunk
            for line in chunk.splitlines():
                if line.strip():
                    print(f"  [{name}] {line.rstrip()}")
        else:
            time.sleep(0.05)
    return buf


def capture_async_dual(ser_a: serial.Serial, name_a: str,
                      ser_b: serial.Serial, name_b: str,
                      duration_s: float) -> tuple[str, str]:
    """Liest beide Ports parallel fuer duration_s Sekunden und gibt beide Buffer zurueck."""
    buf_a = ""
    buf_b = ""
    deadline = time.time() + duration_s

    while time.time() < deadline:
        had_data = False

        if ser_a.in_waiting:
            chunk_a = ser_a.read(ser_a.in_waiting).decode("utf-8", errors="ignore")
            buf_a += chunk_a
            for line in chunk_a.splitlines():
                if line.strip():
                    print(f"  [{name_a}] {line.rstrip()}")
            had_data = True

        if ser_b.in_waiting:
            chunk_b = ser_b.read(ser_b.in_waiting).decode("utf-8", errors="ignore")
            buf_b += chunk_b
            for line in chunk_b.splitlines():
                if line.strip():
                    print(f"  [{name_b}] {line.rstrip()}")
            had_data = True

        if not had_data:
            time.sleep(0.05)

    return buf_a, buf_b


def run_noip_smoke_test(ser_gm: serial.Serial, ser_fol: serial.Serial) -> bool:
    """Kurzer L2-NoIP-Test: GM sendet NoIP-Frames, Follower lauscht via ipdump."""
    print("\n" + "=" * 60)
    print("Schritt 4: NoIP-Sendetest (GM -> Follower)")
    print("=" * 60)

    send_cmd(ser_fol, "FOL", "ipdump 1", timeout=2.0)
    send_cmd(ser_gm, "GM", f"noip_send {NOIP_COUNT} {NOIP_GAP_MS}", timeout=2.0)
    rx_text = capture_async(ser_fol, "FOL", NOIP_LISTEN_S)
    send_cmd(ser_fol, "FOL", "ipdump 0", timeout=2.0)

    hits = len(re.findall(r'0x88B5|\[NoIP-RX\]|E0:NoIP\[0x88B5\]|E0:NOIP\[0x88B5\]', rx_text))
    ok = hits > 0
    print(f"  NoIP-Indikatoren gefunden: {hits}×")
    print(f"  → {'✓ PASS' if ok else '✗ FAIL'}: NoIP-Verkehr {'sichtbar' if ok else 'nicht sichtbar'}")
    return ok


def count_ptp_hits(captured_text: str, expected_ethertype: int) -> int:
    et_hex = f"0x{expected_ethertype:04X}"
    if expected_ethertype == 0x88F7:
        return len(re.findall(r'E0:PTP\[0x88F7\]', captured_text))
    if expected_ethertype == 0x88B5:
        return len(re.findall(r'0x88B5|\[NoIP-RX\]|E0:NoIP\[0x88B5\]|E0:NOIP\[0x88B5\]', captured_text))
    return len(re.findall(re.escape(et_hex), captured_text, re.IGNORECASE))


def run_ptp_capture_variant(ser_gm: serial.Serial, ser_fol: serial.Serial,
                            label: str, dst_mode: str, expected_ethertype: int) -> dict:
    """Run one PTP capture variant and return counter data."""
    print("\n" + "-" * 60)
    print(f"A/B-Variante: {label}  (ptp_dst {dst_mode})")
    print("-" * 60)

    send_cmd(ser_gm, "GM", "ptp_mode off", timeout=2.0)
    send_cmd(ser_fol, "FOL", "ptp_mode off", timeout=2.0)
    send_cmd(ser_gm, "GM", f"ptp_dst {dst_mode}", timeout=2.0)

    resp_fol = send_cmd(ser_fol, "FOL", "ptp_mode follower", timeout=3.0)
    time.sleep(0.3)
    resp_gm = send_cmd(ser_gm, "GM", "ptp_mode master", timeout=3.0)
    capture_async(ser_gm, "GM", 2.0)

    fl_ok = "follower mode" in resp_fol
    gm_ok = "grandmaster mode" in resp_gm or "Init complete" in resp_gm
    print(f"  Mode-Start Follower: {'✓' if fl_ok else '✗'}")
    print(f"  Mode-Start Master  : {'✓' if gm_ok else '✗'}")

    send_cmd(ser_fol, "FOL", "ipdump 1", timeout=2.0)
    captured_text, gm_debug_text = capture_async_dual(ser_fol, "FOL", ser_gm, "GM", LISTEN_S)
    send_cmd(ser_fol, "FOL", "ipdump 0", timeout=2.0)

    ptp_hits = count_ptp_hits(captured_text, expected_ethertype)
    print(f"  ipdump EtherType-Treffer (0x{expected_ethertype:04X}): {ptp_hits}×")

    state_log_hits = len(re.findall(r'\[PTP-GM\]\[STATE\]', gm_debug_text))
    print(f"  GM-State-Debugmeldungen: {state_log_hits}×")

    status_resp = send_cmd(ser_gm, "GM", "ptp_status", timeout=2.0)
    m = re.search(r'gmSyncs=(\d+)', status_resp)
    gm_syncs = int(m.group(1)) if m else -1

    return {
        "label": label,
        "dst_mode": dst_mode,
        "ptp_hits": ptp_hits,
        "gm_state_logs": state_log_hits,
        "gm_syncs": gm_syncs,
        "capture": captured_text,
        "gm_capture": gm_debug_text,
    }


def run_noip_postcheck_bidirectional(ser_gm: serial.Serial, ser_fol: serial.Serial,
                                     reset_gm: bool = False,
                                     reset_fol: bool = False) -> bool:
    """Ende-Postcheck: NoIP in beide Richtungen senden und ipdump am Empfaenger pruefen.

    reset_gm=True:  Isolationstest — GM-Board nach ptp_mode off per SW-Reset neustarten.
                    PASS => Ursache ist persistenter LAN865x/TC6-Zustand auf GM-Seite.
                    FAIL => Ursache liegt ausserhalb des GM (Follower oder Bus).
    reset_fol=True: Isolationstest — Follower-Board nach ptp_mode off per SW-Reset neustarten.
                    PASS => Ursache ist persistenter Follower-seitiger Zustand.
                    FAIL => Ursache liegt im Bus-Zustand (bilateral).
    """
    print("\n" + "=" * 60)
    if reset_gm:
        label = "End-Postcheck (mit GM-SW-Reset nach PTP)"
    elif reset_fol:
        label = "End-Postcheck (mit Follower-SW-Reset nach PTP)"
    else:
        label = "End-Postcheck: NoIP bidirektional mit ipdump"
    print(label)
    print("=" * 60)

    # Isolate NoIP from PTP traffic/state-machine load.
    send_cmd(ser_gm, "GM", "ptp_mode off", timeout=2.0)
    send_cmd(ser_fol, "FOL", "ptp_mode off", timeout=2.0)
    time.sleep(1.0)

    if reset_gm:
        print("\n  [Isolationstest] GM-SW-Reset wird ausgefuehrt ...")
        ser_gm.write(b"reset\r\n")
        print("  Warte 8 s auf GM-Neustart ...")
        for i in range(8, 0, -1):
            print(f"  {i} s ...", end="\r", flush=True)
            time.sleep(1)
        print("  GM-Reset abgeschlossen.   ")
        ser_gm.reset_input_buffer()
        wake(ser_gm, "GM")
        send_cmd(ser_gm, "GM", f"setip {IFACE} {GM_IP} {NETMASK}", timeout=5.0)
        print("  Warte 2 s auf IP-Stack-Stabilisierung nach Reset ...")
        time.sleep(2.0)

    if reset_fol:
        print("\n  [Isolationstest] Follower-SW-Reset wird ausgefuehrt ...")
        ser_fol.write(b"reset\r\n")
        print("  Warte 8 s auf Follower-Neustart ...")
        for i in range(8, 0, -1):
            print(f"  {i} s ...", end="\r", flush=True)
            time.sleep(1)
        print("  Follower-Reset abgeschlossen.   ")
        ser_fol.reset_input_buffer()
        wake(ser_fol, "FOL")
        send_cmd(ser_fol, "FOL", f"setip {IFACE} {FOL_IP} {NETMASK}", timeout=5.0)
        print("  Warte 2 s auf IP-Stack-Stabilisierung nach Reset ...")
        time.sleep(2.0)

    # GM -> FOL
    print("\n  Richtung 1: GM -> FOL")
    send_cmd(ser_fol, "FOL", "ipdump 1", timeout=2.0)
    send_cmd(ser_gm, "GM", f"noip_send {NOIP_COUNT} {NOIP_GAP_MS}", timeout=2.0)
    rx_fol = capture_async(ser_fol, "FOL", NOIP_LISTEN_S)
    send_cmd(ser_fol, "FOL", "ipdump 0", timeout=2.0)
    hits_gm_to_fol = len(re.findall(r'0x88B5|\[NoIP-RX\]|E0:NoIP\[0x88B5\]|E0:NOIP\[0x88B5\]', rx_fol))
    ok_gm_to_fol = hits_gm_to_fol > 0
    print(f"    Treffer: {hits_gm_to_fol}×  -> {'✓ PASS' if ok_gm_to_fol else '✗ FAIL'}")

    # FOL -> GM
    print("\n  Richtung 2: FOL -> GM")
    send_cmd(ser_gm, "GM", "ipdump 1", timeout=2.0)
    send_cmd(ser_fol, "FOL", f"noip_send {NOIP_COUNT} {NOIP_GAP_MS}", timeout=2.0)
    rx_gm = capture_async(ser_gm, "GM", NOIP_LISTEN_S)
    send_cmd(ser_gm, "GM", "ipdump 0", timeout=2.0)
    hits_fol_to_gm = len(re.findall(r'0x88B5|\[NoIP-RX\]|E0:NoIP\[0x88B5\]|E0:NOIP\[0x88B5\]', rx_gm))
    ok_fol_to_gm = hits_fol_to_gm > 0
    print(f"    Treffer: {hits_fol_to_gm}×  -> {'✓ PASS' if ok_fol_to_gm else '✗ FAIL'}")

    overall = ok_gm_to_fol and ok_fol_to_gm
    print("\n  Postcheck gesamt: " + ("✓ PASS" if overall else "✗ FAIL"))
    return overall


# ---------------------------------------------------------------------------
# Frame-Parsing
# ---------------------------------------------------------------------------

def parse_hex_dump_frames(text: str, expected_ethertype: int) -> list:
    """
    Extrahiert Frame-Bytes aus DumpMem-Ausgabe nach EtherType-Markern.
    Gibt Liste von bytes-Objekten zurück.
    """
    frames = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        et_marker = f"0x{expected_ethertype:04X}"
        if et_marker in lines[i].upper():
            raw = []
            for j in range(i + 1, min(i + 25, len(lines))):
                m = re.match(
                    r'\s*[0-9a-fA-F]{8}:\s+((?:\s*[0-9a-fA-F]{2})+)',
                    lines[j]
                )
                if m:
                    raw.extend(int(b, 16) for b in m.group(1).split())
                else:
                    break
            if len(raw) >= 14:
                frames.append(bytes(raw))
        i += 1
    return frames


# ---------------------------------------------------------------------------
# Frame-Validierung
# ---------------------------------------------------------------------------

PTP_MSG_NAMES = {
    0x0: "Sync",
    0x1: "Delay_Req",
    0x2: "Pdelay_Req",
    0x3: "Pdelay_Resp",
    0x8: "Follow_Up",
    0x9: "Delay_Resp",
    0xB: "Announce",
}

# GM sendet an PTP L2 Multicast: 01:80:C2:00:00:0E
PTP_MULTICAST_MAC = bytes([0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E])


def validate_frame(data: bytes, idx: int, expected_ethertype: int) -> bool:
    """
    Validiert PTP-Ethernet-Frame-Struktur.

    Ethernet-Header (14 Bytes):
      [0..5]   DST MAC
      [6..11]  SRC MAC
      [12..13] EtherType

    PTP-Payload (ab Byte 14):
      [14]     tsmt (transportSpecific[7:4] | messageType[3:0])
      [15]     reserved[7:4] | versionPTP[3:0]
      [16..17] messageLength
      [18]     domainNumber
      [19]     reserved
      [20..21] flags
      [22..29] correctionField
      [30..33] reserved
      [34..43] sourcePortIdentity (clockIdentity 8B + portNumber 2B)
      [44..45] sequenceId
      [46]     controlField
      [47]     logMessageInterval
    """
    ok = True
    sep = "-" * 50
    print(f"\n  {sep}")
    print(f"  Frame #{idx + 1}  ({len(data)} Bytes)")
    print(f"  {sep}")

    if len(data) < 20:
        print(f"  ✗ Zu kurz: {len(data)} Bytes (min. 20)")
        return False

    # --- Ethernet Header ---
    dst = data[0:6]
    src = data[6:12]
    et  = (data[12] << 8) | data[13]

    dst_str = ":".join(f"{b:02X}" for b in dst)
    src_str = ":".join(f"{b:02X}" for b in src)

    if dst == PTP_MULTICAST_MAC:
        dst_label = "✓ PTP-Multicast (01:80:C2:00:00:0E)"
    elif all(b == 0xFF for b in dst):
        dst_label = "✓ Broadcast"
    else:
        dst_label = "✗ Unbekannt (erwartet PTP-MC 01:80:C2:00:00:0E)"
        ok = False

    print(f"  DST MAC    : {dst_str}  {dst_label}")
    print(f"  SRC MAC    : {src_str}")

    if et == expected_ethertype:
        print(f"  EtherType  : 0x{et:04X}  ✓ erwartet")
    else:
        print(f"  EtherType  : 0x{et:04X}  ✗ Erwartet 0x{expected_ethertype:04X}")
        ok = False

    # --- PTP-Header ---
    if len(data) >= 15:
        tsmt     = data[14]
        msg_type = tsmt & 0x0F
        trans_sp = (tsmt >> 4) & 0x0F
        type_name = PTP_MSG_NAMES.get(msg_type, f"Unbekannt(0x{msg_type:X})")
        known = msg_type in PTP_MSG_NAMES
        print(f"  msgType    : 0x{msg_type:X} = {type_name}  {'✓' if known else '?'}"
              f"  (transportSpecific={trans_sp})")

    if len(data) >= 16:
        ptp_ver = data[15] & 0x0F
        v_ok = (ptp_ver == 2)
        print(f"  PTPversion : {ptp_ver}  {'✓ PTPv2' if v_ok else '✗ erwartet 2'}")
        if not v_ok:
            ok = False

    if len(data) >= 18:
        ptp_len = (data[16] << 8) | data[17]
        print(f"  msgLength  : {ptp_len} Bytes")

    if len(data) >= 19:
        domain = data[18]
        print(f"  domain     : {domain}")

    if len(data) >= 46:
        seq_id = (data[44] << 8) | data[45]
        print(f"  sequenceId : {seq_id}")

    if len(data) >= 44:
        # clockIdentity ist EUI-64 aus GM-MAC
        ci = data[34:42]
        ci_str = ":".join(f"{b:02X}" for b in ci)
        print(f"  clockIdent : {ci_str}")

    print(f"  → {'✓ VALID' if ok else '✗ INVALID'}")
    return ok


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="PTP Frame Test — simplified")
    parser.add_argument("--no-reset", action="store_true",
                        help="Board-Reset überspringen")
    parser.add_argument("--from-step", "-s", type=int, default=1, metavar="N",
                        help="Ab Schritt N starten (1=Reset, 2=IP, 3=Ping, 4=NoIP, 5=PTP, 6=Capture)")
    parser.add_argument("--expect-ethertype", default="88F7",
                        help="Expected EtherType for capture/validation, e.g. 88F7 or 88B5")
    parser.add_argument("--reset-gm-after-ptp", action="store_true",
                        help="Isolationstest: GM per SW-Reset neustarten nach ptp_mode off, dann NoIP-Postcheck")
    parser.add_argument("--reset-fol-after-ptp", action="store_true",
                        help="Isolationstest: Follower per SW-Reset neustarten nach ptp_mode off, dann NoIP-Postcheck")
    args = parser.parse_args()

    expected_ethertype = int(args.expect_ethertype, 16)

    print("=" * 60)
    print("PTP Frame Test — GM→Follower ohne Register-Zugriffe")
    print(f"  GM  : {GM_PORT}  IP={GM_IP}")
    print(f"  FOL : {FOL_PORT}  IP={FOL_IP}")
    print(f"  Erwarteter EtherType: 0x{expected_ethertype:04X}")
    print("=" * 60)

    try:
        ser_gm  = open_port(GM_PORT)
        ser_fol = open_port(FOL_PORT)
    except serial.SerialException as e:
        print(f"\nFEHLER: COM-Port nicht öffenbar: {e}")
        sys.exit(1)

    captured_frames = []

    try:
        wake(ser_gm,  GM_PORT)
        wake(ser_fol, FOL_PORT)

        # -------------------------------------------------------------------
        # Schritt 1: Board-Reset
        # -------------------------------------------------------------------
        if args.from_step <= 1 and not args.no_reset:
            print("\n" + "=" * 60)
            print("Schritt 1: Board-Reset")
            print("=" * 60)
            ser_gm.write(b"reset\r\n")
            ser_fol.write(b"reset\r\n")
            print("Warte 8 s auf Neustart ...")
            for i in range(8, 0, -1):
                print(f"  {i} s ...", end="\r", flush=True)
                time.sleep(1)
            print("  Reset abgeschlossen.   ")
            ser_gm.reset_input_buffer()
            ser_fol.reset_input_buffer()
            wake(ser_gm,  GM_PORT)
            wake(ser_fol, FOL_PORT)

        # -------------------------------------------------------------------
        # Schritt 2: IP setzen
        # -------------------------------------------------------------------
        if args.from_step <= 2:
            print("\n" + "=" * 60)
            print("Schritt 2: IP-Konfiguration")
            print("=" * 60)
            send_cmd(ser_gm,  "GM",  f"setip {IFACE} {GM_IP} {NETMASK}")
            send_cmd(ser_fol, "FOL", f"setip {IFACE} {FOL_IP} {NETMASK}")
            print("Warte 2 s auf IP-Stack-Stabilisierung ...")
            time.sleep(2.0)

        # -------------------------------------------------------------------
        # Schritt 3: Ping-Test
        # -------------------------------------------------------------------
        if args.from_step <= 3:
            print("\n" + "=" * 60)
            print("Schritt 3: Ping-Test (beidseitig)")
            print("=" * 60)
            gm_ping  = send_ping(ser_gm,  "GM",  FOL_IP)
            fol_ping = send_ping(ser_fol, "FOL", GM_IP)

            if not (gm_ping or fol_ping):
                print("\n✗ ABBRUCH: Kein Ping erfolgreich — PLCA-Link prüfen!")
                return

        # -------------------------------------------------------------------
        # Schritt 4: NoIP-Sendetest
        # -------------------------------------------------------------------
        if args.from_step <= 4:
            noip_ok = run_noip_smoke_test(ser_gm, ser_fol)
            if not noip_ok:
                print("\n  Hinweis: NoIP-Test fehlgeschlagen. PTP-Test läuft trotzdem weiter.")

        # -------------------------------------------------------------------
        # Schritt 5-7: A/B-Test Broadcast vs Multicast (ipdump-Zaehler)
        # -------------------------------------------------------------------
        print("\n" + "=" * 60)
        print("Schritt 5-7: A/B-Test PTP-DST (Broadcast vs Multicast)")
        print("=" * 60)

        variant_results = []
        variant_results.append(run_ptp_capture_variant(
            ser_gm, ser_fol, label="A: Broadcast-DST", dst_mode="broadcast", expected_ethertype=expected_ethertype
        ))
        variant_results.append(run_ptp_capture_variant(
            ser_gm, ser_fol, label="B: Multicast-DST", dst_mode="multicast", expected_ethertype=expected_ethertype
        ))

        print("\n" + "=" * 60)
        print("A/B-VERGLEICH (ipdump-Zaehler)")
        print("=" * 60)
        for r in variant_results:
            print(f"  {r['label']}: hits={r['ptp_hits']}  gmSyncs={r['gm_syncs']}")

        a_hits = variant_results[0]["ptp_hits"]
        b_hits = variant_results[1]["ptp_hits"]
        delta = a_hits - b_hits
        if delta > 0:
            print(f"\n  Ergebnis: Broadcast liefert +{delta} Treffer gegenueber Multicast")
        elif delta < 0:
            print(f"\n  Ergebnis: Multicast liefert +{-delta} Treffer gegenueber Broadcast")
        else:
            print("\n  Ergebnis: Kein Unterschied im ipdump-Zaehler (A == B)")

        best = variant_results[0] if variant_results[0]["ptp_hits"] >= variant_results[1]["ptp_hits"] else variant_results[1]
        raw_count = best["ptp_hits"]
        captured_text = best["capture"]

        print(f"\n  Detail-Analyse wird auf bester Variante ausgefuehrt: {best['label']}")
        if raw_count == 0:
            print("\n  ✗ Keine PTP-Frames in beiden Varianten empfangen!")
            run_noip_postcheck_bidirectional(ser_gm, ser_fol,
                                             reset_gm=args.reset_gm_after_ptp,
                                             reset_fol=args.reset_fol_after_ptp)
            return

        frames = parse_hex_dump_frames(captured_text, expected_ethertype)
        print(f"  Frames vollstaendig geparst: {len(frames)}/{raw_count}")

        valid_cnt = 0
        for i, frame in enumerate(frames[:5]):
            if validate_frame(frame, i, expected_ethertype):
                valid_cnt += 1

        print("\n" + "=" * 60)
        print("ERGEBNIS")
        print("=" * 60)
        print(f"  Beste Variante         : {best['label']}")
        print(f"  PTP-Frames empfangen   : {raw_count}")
        print(f"  Frames geparst         : {len(frames)}")
        print(f"  Davon VALID            : {valid_cnt}")
        print("=" * 60)

        run_noip_postcheck_bidirectional(ser_gm, ser_fol,
                                         reset_gm=args.reset_gm_after_ptp,
                                         reset_fol=args.reset_fol_after_ptp)

    finally:
        ser_gm.close()
        ser_fol.close()
        print("\nVerbindungen geschlossen.")


if __name__ == "__main__":
    main()
