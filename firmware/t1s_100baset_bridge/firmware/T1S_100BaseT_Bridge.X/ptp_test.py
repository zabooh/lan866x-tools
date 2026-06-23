#!/usr/bin/env python3
"""
ptp_test.py
-----------
Testet PTP-Funktionalität auf Grandmaster (COM10) und Follower (COM8).

Tests:
  1. ptp_mode-Konfiguration (Follower=slave, GM=master)
  2. Sync-Zähler steigt an (GM sendet Sync-Frames)
  3. Offset-Konvergenz: |offset| < CONVERGENCE_THRESHOLD in CONVERGENCE_TIMEOUT s
  4. Offset-Stabilität: N aufeinander folgende Messungen alle < STABILITY_THRESHOLD

Verwendung:
  python -u ptp_test.py              # mit Reset
  python -u ptp_test.py --no-reset   # ohne Reset (Boards bereits konfiguriert)
  python -u ptp_test.py --skip-setup # Konfiguration überspringen, nur Tests laufen lassen
"""

import serial
import time
import sys
import re
import argparse

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
FOLLOWER_PORT    = "COM8"
GRANDMASTER_PORT = "COM10"
BAUDRATE         = 115200
SERIAL_TIMEOUT   = 3

FOLLOWER_IP      = "192.168.0.20"
GRANDMASTER_IP   = "192.168.0.30"
NETMASK          = "255.255.255.0"
INTERFACE        = "eth0"

PROMPT_MARKERS   = ["\r\n> ", "\n> ", "> "]
RESPONSE_TIMEOUT = 5.0

# PTP-Testparameter
SYNC_MIN_COUNT           = 5         # Mindestanzahl neuer Syncs in Test 2
CONVERGENCE_THRESHOLD_NS = 500_000   # 500 µs — initiale Konvergenz
STABILITY_THRESHOLD_NS   = 200_000   # 200 µs — Stabilitätstest
CONVERGENCE_TIMEOUT_S    = 30.0      # Timeout für Konvergenztest
STABILITY_READINGS       = 5         # Anzahl stabiler Messungen
STABILITY_INTERVAL_S     = 1.0       # Pause zwischen Stabilitätsmessungen

# ---------------------------------------------------------------------------
# Serial-Hilfsfunktionen
# ---------------------------------------------------------------------------

def open_port(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUDRATE, timeout=SERIAL_TIMEOUT)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"[{port}] Verbunden.")
    return ser


def send_cmd(ser: serial.Serial, port_name: str, command: str,
             timeout: float = RESPONSE_TIMEOUT) -> str:
    """Sendet einen CLI-Befehl und liest die Antwort bis zum Prompt oder Timeout."""
    ser.reset_input_buffer()
    ser.write((command + "\r\n").encode())
    print(f"[{port_name}] >>> {command}")

    response = ""
    deadline = time.time() + timeout

    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            response += chunk
            if any(response.endswith(p) for p in PROMPT_MARKERS):
                break
        else:
            time.sleep(0.05)

    time.sleep(0.1)
    if ser.in_waiting:
        response += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")

    lines = [l for l in response.splitlines() if command not in l]
    output = "\n".join(lines).strip()
    if output:
        print(f"[{port_name}] <<< {output}")
    return response


def wake_port(ser: serial.Serial, port_name: str):
    """Sendet Enter um den Prompt zu aktivieren."""
    ser.write(b"\r\n")
    time.sleep(0.5)
    if ser.in_waiting:
        ser.read(ser.in_waiting)
    print(f"[{port_name}] Prompt bereit.")


def wait_quiet(ser: serial.Serial, port_name: str,
               quiet_secs: float = 2.0, total_timeout: float = 12.0) -> None:
    """Wartet bis das Board keine unaufgeforderten Meldungen mehr sendet."""
    deadline  = time.time() + total_timeout
    last_data = time.time()
    print(f"[{port_name}] Warte auf Board-Ruhe ({quiet_secs:.0f} s still) ...")
    while time.time() < deadline:
        if ser.in_waiting:
            raw = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            last_data = time.time()
            for line in raw.splitlines():
                if line.strip():
                    print(f"[{port_name}] (async) {line}")
        elif time.time() - last_data >= quiet_secs:
            break
        else:
            time.sleep(0.1)


def set_ip(ser: serial.Serial, port_name: str, ip: str, mask: str, iface: str,
           retries: int = 3, retry_delay: float = 2.0) -> bool:
    for attempt in range(1, retries + 1):
        resp = send_cmd(ser, port_name, f"setip {iface} {ip} {mask}",
                        timeout=RESPONSE_TIMEOUT)
        if any(m in resp for m in ["Error", "error", "Usage", "No such", "failed"]):
            if attempt < retries:
                time.sleep(retry_delay)
            continue
        print(f"[{port_name}] IP {ip}/{mask} auf {iface} gesetzt.")
        return True
    print(f"[{port_name}] FEHLER: setip nach {retries} Versuchen nicht erfolgreich.")
    return False


# ---------------------------------------------------------------------------
# PTP-Parsing
# ---------------------------------------------------------------------------

def parse_ptp_status(response: str):
    """Parst '[PTP] mode=slave gmSyncs=47 gmState=2'. Gibt dict oder None zurück."""
    m = re.search(r'\[PTP\]\s+mode=(\w+)\s+gmSyncs=(\d+)\s+gmState=(\d+)', response)
    if not m:
        return None
    return {
        "mode":    m.group(1),
        "gmSyncs": int(m.group(2)),
        "gmState": int(m.group(3)),
    }


def parse_ptp_offset(response: str):
    """Parst '[PTP] offset=-12345 ns  abs=12345 ns'. Gibt dict oder None zurück."""
    m = re.search(r'\[PTP\]\s+offset=(-?\d+)\s+ns\s+abs=(\d+)\s+ns', response)
    if not m:
        return None
    return {
        "offset": int(m.group(1)),
        "abs":    int(m.group(2)),
    }


def get_ptp_status(ser, port_name: str):
    return parse_ptp_status(send_cmd(ser, port_name, "ptp_status"))


def get_ptp_offset(ser, port_name: str):
    return parse_ptp_offset(send_cmd(ser, port_name, "ptp_offset"))


def capture_async(ser: serial.Serial, port_name: str, duration_s: float) -> str:
    """Liest asynchron für duration_s Sekunden alle ankommenden Daten."""
    deadline = time.time() + duration_s
    captured = ""
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            captured += chunk
            for line in chunk.splitlines():
                if line.strip():
                    print(f"  [{port_name}] {line}")
        else:
            time.sleep(0.05)
    return captured


def parse_ptp_hex_dumps(captured: str):
    """
    Sucht in DumpMem-Ausgabe nach 'E0:PTP[0x88F7]'-Blöcken und extrahiert Frame-Bytes.
    Gibt Liste von (frame_bytes, rx_ts) zurück.
    """
    frames = []
    lines = captured.splitlines()
    i = 0
    while i < len(lines):
        m = re.search(r'E0:PTP\[0x88F7\] len=(\d+) ts=(\d+)', lines[i])
        if m:
            frame_len = int(m.group(1))
            ts        = int(m.group(2))
            raw = []
            i += 1
            while i < len(lines):
                hm = re.match(r'\s*[0-9a-fA-F]{8}:\s+((?:\s*[0-9a-fA-F]{2})+)', lines[i])
                if hm:
                    raw.extend(int(b, 16) for b in hm.group(1).split())
                    i += 1
                else:
                    break
            if raw:
                frames.append((bytes(raw[:frame_len]), ts))
        else:
            i += 1
    return frames


_PTP_MSG_NAMES = {
    0x0: 'Sync', 0x1: 'Delay_Req', 0x2: 'Pdelay_Req', 0x3: 'Pdelay_Resp',
    0x8: 'Follow_Up', 0x9: 'Delay_Resp', 0xA: 'Pdelay_Resp_FollowUp',
    0xB: 'Announce', 0xC: 'Signaling', 0xD: 'Management',
}


def analyze_ptp_frame(frame_bytes: bytes):
    """
    Analysiert einen PTP-Frame (ab Ethernet-Header-Anfang, pMacLayer).
    Gibt (info_dict, error_str) zurück.
    """
    if len(frame_bytes) < 34:
        return None, f"Frame zu kurz ({len(frame_bytes)} B)"

    dst_mac   = ':'.join(f'{b:02x}' for b in frame_bytes[0:6])
    src_mac   = ':'.join(f'{b:02x}' for b in frame_bytes[6:12])
    ethertype = (frame_bytes[12] << 8) | frame_bytes[13]

    if ethertype != 0x88F7:
        return None, f"Ungültiger EtherType: 0x{ethertype:04X}"
    if len(frame_bytes) < 31:
        return None, f"PTP-Payload zu kurz ({len(frame_bytes)} B)"

    ptp      = frame_bytes[14:]
    tsmt     = ptp[0]
    msg_type = tsmt & 0x0F
    version  = ptp[1] & 0x0F
    msg_len  = (ptp[2] << 8) | ptp[3]
    domain   = ptp[4]
    flags0   = ptp[6]
    flags1   = ptp[7]
    seq_id   = (ptp[16] << 8) | ptp[17] if len(ptp) >= 18 else 0

    msg_name = _PTP_MSG_NAMES.get(msg_type, f'Unknown(0x{msg_type:X})')

    issues = []
    if version != 2:
        issues.append(f"version={version} (erwartet 2)")
    if domain != 0:
        issues.append(f"domain={domain} (erwartet 0)")
    if dst_mac not in ('01:80:c2:00:00:0e', 'ff:ff:ff:ff:ff:ff'):
        issues.append(f"dst_mac={dst_mac} (kein PTP-Multicast 01:80:c2:00:00:0e)")

    return {
        "dst_mac":   dst_mac,  "src_mac":  src_mac,
        "msg_type":  msg_type, "msg_name": msg_name,
        "version":   version,  "msg_len":  msg_len,
        "domain":    domain,
        "flags":     f"0x{flags0:02X} 0x{flags1:02X}",
        "seq_id":    seq_id,
        "issues":    issues,
        "ok":        len(issues) == 0,
    }, ""


# ---------------------------------------------------------------------------
# Test-Hilfsfunktion
# ---------------------------------------------------------------------------

def report(name: str, passed: bool, detail: str = "") -> bool:
    status = "PASS" if passed else "FAIL"
    suffix = f"  ({detail})" if detail else ""
    print(f"\n  [{status}] {name}{suffix}")
    return passed


# ---------------------------------------------------------------------------
# Einzelne Tests
# ---------------------------------------------------------------------------

FRAME_CAPTURE_S = 3.0   # Sekunden, die auf PTP-Frames gewartet wird
MIN_PTP_FRAMES  = 2     # Mindestanzahl empfangener PTP-Frames


def test_ptp_frame_reception(ser_follower) -> bool:
    """Test 2: Prüft ob der Follower PTP-Frames empfängt und der Inhalt plausibel ist."""
    print("\n" + "-"*60)
    print("TEST 2: PTP-Frame-Empfang auf Follower (eth0)")
    print("-"*60)

    # ipdump auf eth0 des Followers aktivieren
    send_cmd(ser_follower, FOLLOWER_PORT, "ipdump 1", timeout=RESPONSE_TIMEOUT)
    print(f"\n  Lausche {FRAME_CAPTURE_S:.0f} s auf eingehende PTP-Frames ...")
    captured = capture_async(ser_follower, FOLLOWER_PORT, FRAME_CAPTURE_S)
    # ipdump wieder deaktivieren
    send_cmd(ser_follower, FOLLOWER_PORT, "ipdump 0", timeout=RESPONSE_TIMEOUT)

    # --- Zählen ---
    ptp_count = len(re.findall(r'E0:PTP\[0x88F7\]', captured))
    print(f"\n  PTP-Frame-Zeilen erkannt: {ptp_count}")
    ok_recv = report(
        f">= {MIN_PTP_FRAMES} PTP-Frames empfangen",
        ptp_count >= MIN_PTP_FRAMES,
        f"gefunden: {ptp_count}"
    )

    # --- Frames parse + nach Typen aufteilen ---
    frames = parse_ptp_hex_dumps(captured)
    syncs     = [(f, ts) for f, ts in frames if len(f) >= 15 and (f[14] & 0x0F) == 0x0]
    followups = [(f, ts) for f, ts in frames if len(f) >= 15 and (f[14] & 0x0F) == 0x8]
    print(f"  Geparsete Frames: gesamt={len(frames)}  Sync={len(syncs)}  Follow_Up={len(followups)}")

    ok_sync = report(
        "Mindestens 1 Sync-Frame identifiziert",
        len(syncs) >= 1,
        f"Sync={len(syncs)} Follow_Up={len(followups)}"
    )

    # --- Inhalt des ersten Sync-Frames analysieren ---
    ok_content = True
    if syncs:
        target_bytes, target_ts = syncs[0]
        info, err = analyze_ptp_frame(target_bytes)
        if info:
            print(f"\n  Analyse erster Sync-Frame (rx_ts={target_ts}):")
            print(f"    dst_mac  : {info['dst_mac']}")
            print(f"    src_mac  : {info['src_mac']}")
            print(f"    msg_type : {info['msg_type']} ({info['msg_name']})")
            print(f"    version  : {info['version']}")
            print(f"    msg_len  : {info['msg_len']} B")
            print(f"    domain   : {info['domain']}")
            print(f"    flags    : {info['flags']}")
            print(f"    seq_id   : {info['seq_id']}")
            if info["issues"]:
                for issue in info["issues"]:
                    print(f"    *** Problem: {issue}")
                ok_content = False
        else:
            print(f"  Frame-Parse-Fehler: {err}")
            ok_content = False
        ok_content = report("Sync-Frame-Inhalt plausibel (version=2, domain=0, dst=PTP-Multicast)",
                            ok_content)
    elif ptp_count > 0:
        print("  (kein vollständiger Hex-Dump vorhanden — Frame-Inhalt nicht prüfbar)")

    return ok_recv and ok_sync and ok_content


def test_ptp_mode(ser_gm, ser_follower) -> bool:
    """Test 1: Prüft ob PTP-Modus korrekt auf master/slave gesetzt ist."""
    print("\n" + "-"*60)
    print("TEST 1: PTP-Modus")
    print("-"*60)

    gm_st = get_ptp_status(ser_gm, GRANDMASTER_PORT)
    fl_st = get_ptp_status(ser_follower, FOLLOWER_PORT)

    ok_gm = gm_st is not None and gm_st["mode"] == "master"
    ok_fl = fl_st is not None and fl_st["mode"] == "slave"

    report("GM mode=master",      ok_gm, f"ptp_status={gm_st}")
    report("Follower mode=slave", ok_fl, f"ptp_status={fl_st}")
    return ok_gm and ok_fl


def test_sync_counter(ser_gm) -> bool:
    """Test 2: GM-Sync-Zähler muss innerhalb der Wartezeit um >= SYNC_MIN_COUNT steigen."""
    print("\n" + "-"*60)
    print("TEST 2: Sync-Zähler (GM)")
    print("-"*60)

    st1 = get_ptp_status(ser_gm, GRANDMASTER_PORT)
    if st1 is None:
        return report("ptp_status lesbar", False, "kein Parse-Ergebnis")

    # Warte auf mindestens SYNC_MIN_COUNT Sync-Intervalle (125 ms je Sync + Puffer)
    wait_s = (SYNC_MIN_COUNT * 0.130) + 0.5
    print(f"  Messung 1: gmSyncs={st1['gmSyncs']}")
    print(f"  Warte {wait_s:.1f} s auf >= {SYNC_MIN_COUNT} neue Syncs ...")
    time.sleep(wait_s)

    st2 = get_ptp_status(ser_gm, GRANDMASTER_PORT)
    if st2 is None:
        return report("ptp_status nach Wartezeit lesbar", False)

    delta = st2["gmSyncs"] - st1["gmSyncs"]
    print(f"  Messung 2: gmSyncs={st2['gmSyncs']}  delta={delta}")
    return report(
        f"gmSyncs steigt um >= {SYNC_MIN_COUNT}",
        delta >= SYNC_MIN_COUNT,
        f"vorher={st1['gmSyncs']} nachher={st2['gmSyncs']} delta={delta}"
    )


def test_offset_convergence(ser_follower) -> bool:
    """Test 3: Follower-Offset muss unter CONVERGENCE_THRESHOLD_NS sinken."""
    print("\n" + "-"*60)
    print(f"TEST 3: Offset-Konvergenz  (Ziel < {CONVERGENCE_THRESHOLD_NS:,} ns = "
          f"{CONVERGENCE_THRESHOLD_NS/1000:.0f} µs)")
    print("-"*60)

    deadline = time.time() + CONVERGENCE_TIMEOUT_S
    best_abs = None
    attempt  = 0

    while time.time() < deadline:
        attempt += 1
        remaining = deadline - time.time()
        off = get_ptp_offset(ser_follower, FOLLOWER_PORT)
        if off is None:
            print(f"  [#{attempt}] ptp_offset nicht parsebar — warte 1 s ...")
            time.sleep(1.0)
            continue

        abs_ns = off["abs"]
        if best_abs is None or abs_ns < best_abs:
            best_abs = abs_ns

        print(f"  [#{attempt}]  offset={off['offset']:+,} ns  "
              f"abs={abs_ns:,} ns  best={best_abs:,} ns  "
              f"(noch {remaining:.0f} s)")

        if abs_ns < CONVERGENCE_THRESHOLD_NS:
            return report(
                f"Konvergenz < {CONVERGENCE_THRESHOLD_NS:,} ns",
                True,
                f"abs={abs_ns:,} ns nach {attempt} Messungen"
            )
        time.sleep(1.0)

    best_str = f"{best_abs:,} ns" if best_abs is not None else "keine Messung"
    return report(
        f"Konvergenz < {CONVERGENCE_THRESHOLD_NS:,} ns",
        False,
        f"best={best_str}, Timeout nach {CONVERGENCE_TIMEOUT_S:.0f} s"
    )


def test_offset_stability(ser_follower) -> bool:
    """Test 4: N aufeinander folgende Offset-Messungen müssen alle unter STABILITY_THRESHOLD_NS liegen."""
    print("\n" + "-"*60)
    print(f"TEST 4: Offset-Stabilität  ({STABILITY_READINGS}x < "
          f"{STABILITY_THRESHOLD_NS:,} ns = {STABILITY_THRESHOLD_NS/1000:.0f} µs)")
    print("-"*60)

    readings = []
    for i in range(STABILITY_READINGS):
        off = get_ptp_offset(ser_follower, FOLLOWER_PORT)
        if off is None:
            return report("ptp_offset lesbar", False, f"fehlgeschlagen bei Messung {i+1}")
        readings.append(off["abs"])
        status = "OK" if off["abs"] < STABILITY_THRESHOLD_NS else "OVER"
        print(f"  [{i+1}/{STABILITY_READINGS}]  abs={off['abs']:,} ns  [{status}]")
        if i < STABILITY_READINGS - 1:
            time.sleep(STABILITY_INTERVAL_S)

    all_ok = all(v < STABILITY_THRESHOLD_NS for v in readings)
    return report(
        f"Alle {STABILITY_READINGS} Messungen < {STABILITY_THRESHOLD_NS:,} ns",
        all_ok,
        f"min={min(readings):,}  max={max(readings):,} ns"
    )


# ---------------------------------------------------------------------------
# Board-Setup
# ---------------------------------------------------------------------------

def setup_boards(ser_gm, ser_follower, do_reset: bool) -> bool:
    if do_reset:
        print("\n=== Reset ===")
        ser_follower.write(b"reset\r\n")
        ser_gm.write(b"reset\r\n")
        print("Warte 8 s auf Neustart ...")
        time.sleep(8)
        ser_follower.reset_input_buffer()
        ser_gm.reset_input_buffer()
        wake_port(ser_follower, FOLLOWER_PORT)
        wake_port(ser_gm,       GRANDMASTER_PORT)

    print("\n=== IP-Konfiguration ZUERST — PTP startet erst danach ===")
    ok_fl = set_ip(ser_follower, FOLLOWER_PORT,    FOLLOWER_IP,    NETMASK, INTERFACE)
    ok_gm = set_ip(ser_gm,       GRANDMASTER_PORT, GRANDMASTER_IP, NETMASK, INTERFACE)

    print("\nWarte 2 s auf IP-Stack-Stabilisierung ...")
    time.sleep(2)

    print("\n=== PTP starten (nach setip) ===")
    # Follower zuerst, damit PLCA-Bus bereit ist wenn GM als Beacon startet
    send_cmd(ser_follower, FOLLOWER_PORT,    "ptp_mode follower", timeout=RESPONSE_TIMEOUT)
    time.sleep(0.5)
    send_cmd(ser_gm,       GRANDMASTER_PORT, "ptp_mode master",   timeout=RESPONSE_TIMEOUT)
    wait_quiet(ser_gm, GRANDMASTER_PORT, quiet_secs=2.0, total_timeout=12.0)

    print("\nWarte 3 s auf PTP-Stabilisierung ...")
    time.sleep(3)

    return ok_fl and ok_gm


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="PTP-Funktionstest für T1S 100BaseT Bridge (Grandmaster / Follower)"
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Reset überspringen (Boards bereits hochgefahren)"
    )
    parser.add_argument(
        "--skip-setup",
        action="store_true",
        help="ptp_mode/setip überspringen — nur Tests ausführen"
    )
    args = parser.parse_args()

    try:
        ser_follower = open_port(FOLLOWER_PORT)
    except serial.SerialException as e:
        print(f"FEHLER: Kann {FOLLOWER_PORT} nicht öffnen: {e}")
        sys.exit(1)

    try:
        ser_gm = open_port(GRANDMASTER_PORT)
    except serial.SerialException as e:
        print(f"FEHLER: Kann {GRANDMASTER_PORT} nicht öffnen: {e}")
        ser_follower.close()
        sys.exit(1)

    try:
        wake_port(ser_follower, FOLLOWER_PORT)
        wake_port(ser_gm,       GRANDMASTER_PORT)

        if not args.skip_setup:
            ok = setup_boards(ser_gm, ser_follower, do_reset=not args.no_reset)
            if not ok:
                print("\nWARNUNG: Setup-Schritt fehlgeschlagen — Tests werden trotzdem gestartet.")

        print("\n" + "="*60)
        print("PTP-FUNKTIONSTEST")
        print(f"  GM:       {GRANDMASTER_PORT}  ({GRANDMASTER_IP})")
        print(f"  Follower: {FOLLOWER_PORT}  ({FOLLOWER_IP})")
        print(f"  Konvergenz-Schwellwert: {CONVERGENCE_THRESHOLD_NS:,} ns")
        print(f"  Stabilitäts-Schwellwert: {STABILITY_THRESHOLD_NS:,} ns")
        print("="*60)

        results = []
        results.append(("PTP-Modus",           test_ptp_mode(ser_gm, ser_follower)))
        results.append(("Frame-Empfang",        test_ptp_frame_reception(ser_follower)))
        results.append(("Sync-Zähler",          test_sync_counter(ser_gm)))
        results.append(("Offset-Konvergenz",    test_offset_convergence(ser_follower)))
        results.append(("Offset-Stabilität",    test_offset_stability(ser_follower)))

        passed = sum(v for _, v in results)
        total  = len(results)

        print("\n" + "="*60)
        print("TESTERGEBNIS")
        print("="*60)
        for name, ok in results:
            print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        print("-"*60)
        print(f"  {passed}/{total} Tests bestanden")
        print("="*60)
        if passed == total:
            print("ALLE PTP-TESTS BESTANDEN")
        else:
            print(f"{total - passed} TEST(S) FEHLGESCHLAGEN")

        sys.exit(0 if passed == total else 1)

    finally:
        ser_follower.close()
        ser_gm.close()
        print("\nSerielle Verbindungen geschlossen.")


if __name__ == "__main__":
    main()
