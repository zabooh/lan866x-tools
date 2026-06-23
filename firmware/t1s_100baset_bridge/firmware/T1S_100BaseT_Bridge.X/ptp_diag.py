#!/usr/bin/env python3
"""
ptp_diag.py
-----------
Bottom-Up Diagnose-Test für T1S 100BaseT Bridge PTP.

Teststufen (abbrechen bei erstem Block-Fehler):

  Schritt 0  IP-Konfiguration  (setip eth0)
  Schritt 1  Ping-Test          GM→Follower und Follower→GM
  Schritt 2  PTP aktivieren     ptp_mode follower / ptp_mode master
  Schritt 3  GM sendet Syncs?   gmSyncs-Zähler steigt; gmState-Diagnose
  Schritt 4  TX-Match Detect?   lan_read GM_TXMCTL → TXPMDET-Bit auslesen
  Schritt 5  Follower empfängt? ipdump 1 → 0x88F7-Frames zählen

Verwendung:
  python ptp_diag.py
  python ptp_diag.py --no-reset      # ohne Board-Reset
  python ptp_diag.py --from-step 2   # ab Schritt 2 (Boards bereits konfiguriert)
"""

import serial
import time
import re
import sys
import argparse
import threading
import statistics
from datetime import datetime


class _Tee:
    """Schreibt gleichzeitig auf stream (Konsole) und in eine UTF-8-Datei."""
    def __init__(self, stream, filepath):
        self._stream = stream
        self._file = open(filepath, "w", encoding="utf-8")

    def write(self, data):
        self._stream.write(data)
        self._stream.flush()
        self._file.write(data)
        self._file.flush()

    def flush(self):
        self._stream.flush()
        self._file.flush()

    def fileno(self):
        return self._stream.fileno()

    def close(self):
        self._file.close()

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
FOLLOWER_PORT    = "COM8"
GRANDMASTER_PORT = "COM10"
BAUDRATE         = 115200
SERIAL_TIMEOUT   = 3

FOLLOWER_IP      = "192.168.0.30"
GRANDMASTER_IP   = "192.168.0.20"
NETMASK          = "255.255.255.0"
INTERFACE        = "eth0"

PROMPT_MARKERS   = ["\r\n> ", "\n> ", "> "]
RESPONSE_TIMEOUT = 5.0

PING_COUNT       = 4        # Anzahl ICMP-Requests pro Richtung
PING_TIMEOUT_S   = 12.0     # Maximale Wartezeit auf "Ping: done."

SYNC_WAIT_S      = 2.5      # Wartezeit zwischen gmSyncs-Messungen (>= 2 × 125 ms × 5)
SYNC_MIN_DELTA   = 5        # Mindest-Anstieg des gmSyncs-Zählers

TXMCTL_POLL_N    = 6        # Wie oft TXMCTL gelesen wird (nach je ~200 ms)
TXMCTL_ADDR      = "0x00040040"

IPDUMP_SECS      = 5.0      # Lauschzeit auf PTP-Frames beim Follower
MIN_PTP_FRAMES   = 2        # Mindestanzahl 0x88F7-Frames

FOLLOWUP_LISTEN_S      = 30.0   # Lauschzeit GM FollowUp-Pfad (s)
CONVERGENCE_TIMEOUT_S  = 120.0  # Max. Wartezeit auf FOL FINE-State (s)
STABILITY_DURATION_S   = 60.0   # Dauer Offset-Stabilitätsmessung (s)
POLL_INTERVAL_S        = 1.5    # Periodenzeit ptp_offset-Polling (s)
CONVERGE_THRESHOLD_NS  = 100    # Offset-Schwellwert "konvergiert" (ns)
CONVERGE_CONSECUTIVE   = 10     # N Messungen unter Schwellwert → stabil

# Diagnose: gmState-Bedeutungen (aus ptp_gm_task.c-Enum)
GM_STATE_NAMES = {
    0: "IDLE",
    1: "WAIT_PERIOD",
    2: "SEND_SYNC",
    3: "READ_TXMCTL",
    4: "WAIT_TXMCTL",   # ← Häufiger Fehler-State!
    5: "READ_STATUS0",
    6: "WAIT_STATUS0",
    7: "READ_TTSCA_H",
    8: "WAIT_TTSCA_H",
    9: "READ_TTSCA_L",
    10: "WAIT_TTSCA_L",
    11: "WRITE_CLEAR",
    12: "WAIT_CLEAR",
    13: "SEND_FOLLOWUP",
}

# ---------------------------------------------------------------------------
# Serielle Hilfsfunktionen
# ---------------------------------------------------------------------------

def open_port(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUDRATE, timeout=SERIAL_TIMEOUT)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print(f"[{port}] Verbunden.")
    return ser


def wake_port(ser: serial.Serial, port_name: str) -> None:
    ser.write(b"\r\n")
    time.sleep(0.5)
    ser.reset_input_buffer()
    print(f"[{port_name}] Prompt bereit.")


def send_cmd(ser: serial.Serial, port_name: str, command: str,
             timeout: float = RESPONSE_TIMEOUT) -> str:
    """Sendet CLI-Befehl, liest Antwort bis Prompt oder Timeout."""
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


def send_cmd_wait_for(ser: serial.Serial, port_name: str, command: str,
                      wait_for: str, timeout: float = 15.0) -> str:
    """
    Sendet Befehl, liest asynchron bis 'wait_for' in der Ausgabe erscheint oder Timeout.
    Für Befehle wie 'ping' die Ergebnisse asynchron ausgeben.
    """
    ser.reset_input_buffer()
    ser.write((command + "\r\n").encode())
    print(f"[{port_name}] >>> {command}")

    response = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
            response += chunk
            for line in chunk.splitlines():
                stripped = line.strip()
                if stripped and command not in stripped:
                    print(f"[{port_name}]   {stripped}")
            if wait_for in response:
                break
        else:
            time.sleep(0.05)
    return response


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
                    print(f"  [async] {line}")
        else:
            time.sleep(0.05)
    return captured


def drain(ser: serial.Serial) -> None:
    time.sleep(0.2)
    ser.reset_input_buffer()


def capture_dual(ser_a: serial.Serial, name_a: str,
                 ser_b: serial.Serial, name_b: str,
                 duration: float) -> tuple:
    """Liest beide COM-Ports parallel fuer `duration` Sekunden mit (threading)."""
    buf_a, buf_b = [], []
    stop_evt = threading.Event()

    def _reader(ser, name, buf_list):
        while not stop_evt.is_set():
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                buf_list.append(chunk)
                for line in chunk.splitlines():
                    if line.strip():
                        print(f"  [{name}] {line}")
            else:
                time.sleep(0.02)

    ta = threading.Thread(target=_reader, args=(ser_a, name_a, buf_a), daemon=True)
    tb = threading.Thread(target=_reader, args=(ser_b, name_b, buf_b), daemon=True)
    ta.start(); tb.start()
    time.sleep(duration)
    stop_evt.set()
    ta.join(timeout=1.0); tb.join(timeout=1.0)
    return "".join(buf_a), "".join(buf_b)


# ---------------------------------------------------------------------------
# Ergebnisausgabe
# ---------------------------------------------------------------------------

_pad = 50

def ok(label: str, detail: str = "") -> bool:
    suffix = f"  ({detail})" if detail else ""
    print(f"  [PASS] {label}{suffix}")
    return True


def fail(label: str, detail: str = "") -> bool:
    suffix = f"  ({detail})" if detail else ""
    print(f"  [FAIL] {label}{suffix}")
    return False


def hint(text: str) -> None:
    for line in text.strip().splitlines():
        print(f"         ↳ {line}")


def section(title: str) -> None:
    bar = "-" * 60
    print(f"\n{bar}\n{title}\n{bar}")


def summary_line(step: int, name: str, passed: bool) -> str:
    return f"  {'✓' if passed else '✗'}  Schritt {step}: {name}"


# ---------------------------------------------------------------------------
# Schritt 0: IP-Konfiguration
# ---------------------------------------------------------------------------

def step0_setip(ser_gm, ser_fl) -> bool:
    section("Schritt 0: IP-Konfiguration")

    def _setip(ser, port, ip):
        resp = send_cmd(ser, port, f"setip {INTERFACE} {ip} {NETMASK}")
        errors = ["Error", "error", "Usage", "No such", "failed"]
        if any(e in resp for e in errors):
            return fail(f"setip {ip}", resp.strip()[:80])
        return ok(f"setip {ip}/{NETMASK} auf {port}")

    gm_ok = _setip(ser_gm, GRANDMASTER_PORT, GRANDMASTER_IP)
    fl_ok = _setip(ser_fl, FOLLOWER_PORT,    FOLLOWER_IP)

    print("\nWarte 2 s auf IP-Stack-Stabilisierung ...")
    time.sleep(2.0)
    return gm_ok and fl_ok


# ---------------------------------------------------------------------------
# Schritt 1: Ping-Test
# ---------------------------------------------------------------------------

def _parse_ping_done(output: str) -> tuple:
    """
    Parst 'Ping: done. Sent N requests, received M replies.'
    Gibt (sent, received) oder (None, None) bei keinem Treffer zurück.
    """
    m = re.search(r'Ping: done\. Sent (\d+) requests, received (\d+) replies', output)
    if m:
        return int(m.group(1)), int(m.group(2))
    # Zähle alternativ Reply-Zeilen
    sent    = len(re.findall(r'Ping: sent request', output))
    replies = len(re.findall(r'Ping: reply\[\d+\]', output))
    if sent > 0 or replies > 0:
        return sent, replies
    return None, None


def _do_ping(ser, port_name: str, target_ip: str, count: int) -> bool:
    """
    Führt 'ping <ip> n <count>' aus, wartet auf 'Ping: done.'
    Gibt True wenn mindestens 1 Reply erhalten wurde.
    """
    cmd = f"ping {target_ip} n {count}"
    output = send_cmd_wait_for(ser, port_name, cmd, "Ping: done.", timeout=PING_TIMEOUT_S)
    sent, received = _parse_ping_done(output)

    if sent is None:
        # Ping wurde gar nicht gestartet (z.B. IP noch nicht gesetzt)
        if "Ping: failed" in output or "request aborted" in output:
            result = fail(f"Ping {port_name}→{target_ip}", "Ping fehlgeschlagen zu starten")
            hint("IP-Stack noch nicht bereit? setip bereits ausgeführt?")
            return result
        result = fail(f"Ping {port_name}→{target_ip}", "'Ping: done.' nicht empfangen")
        hint("Board hat evtl. nicht geantwortet — PLCA-Link noch nicht aktiv?")
        return result

    if received == 0:
        result = fail(f"Ping {port_name}→{target_ip}",
                      f"0/{sent} Replies — keine Antwort")
        hint(f"Mögliche Ursachen:\n"
             f"  • PLCA-Bus noch nicht aufgebaut (beide Boards aktiv?)\n"
             f"  • IP auf Gegenseite nicht gesetzt oder falsch\n"
             f"  • ARP schlägt fehl (prüfe Link-LEDs)\n"
             f"  • PLCA nodeId: GM muss 0 sein, Follower muss 1 sein")
        return result

    return ok(f"Ping {port_name}→{target_ip}",
              f"{received}/{sent} Replies empfangen")


def step1_ping(ser_gm, ser_fl) -> bool:
    section("Schritt 1: Ping-Test (beidseitig)")

    print(f"\n[GM→Follower]  {GRANDMASTER_PORT} pingt {FOLLOWER_IP} ...")
    gm_ok = _do_ping(ser_gm, GRANDMASTER_PORT, FOLLOWER_IP, PING_COUNT)
    drain(ser_gm)

    print(f"\n[Follower→GM]  {FOLLOWER_PORT} pingt {GRANDMASTER_IP} ...")
    fl_ok = _do_ping(ser_fl, FOLLOWER_PORT, GRANDMASTER_IP, PING_COUNT)
    drain(ser_fl)

    if not (gm_ok or fl_ok):
        print("\n  ABBRUCH: Kein Ping möglich — PTP-Tests werden übersprungen.")
        hint("Ohne funktionierenden PLCA-Link über T1S hat PTP keine Chance.\n"
             "Prüfe zuerst den physikalischen Layer (Kabel, PLCA-Config, Boards an?).")
    elif not gm_ok or not fl_ok:
        print("\n  WARNUNG: Ping nur in einer Richtung erfolgreich — fortfahren.")
    return gm_ok and fl_ok


# ---------------------------------------------------------------------------
# Schritt 2: PTP aktivieren
# ---------------------------------------------------------------------------

def step2_ptp_activate(ser_gm, ser_fl) -> bool:
    section("Schritt 2: PTP-Service aktivieren")

    # Follower zuerst, damit PLCA-Node 1 schon läuft wenn GM als Node 0 startet
    resp_fl = send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode follower")
    resp_gm = send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode master")

    fl_ok = "[PTP] follower mode" in resp_fl or "follower mode" in resp_fl
    gm_ok = "[PTP] grandmaster mode" in resp_gm or "grandmaster mode" in resp_gm or \
            "Init complete" in resp_gm

    if not fl_ok:
        fail("ptp_mode follower", resp_fl.strip()[:80])
        hint("Kommando 'ptp_mode' vorhanden? Firmware kompiliert/geflasht?")
    else:
        ok("ptp_mode follower")

    if not gm_ok:
        fail("ptp_mode master", resp_gm.strip()[:80])
        hint("GM-Init schlägt fehl? PTP_GM_Init() korrekt aufgerufen?")
    else:
        ok("ptp_mode master")

    # Kurz warten und dann asynchrone Ausgaben lesen
    print(f"\n  Warte 2 s auf Initialisierungs-Ausgaben (GM) ...")
    captured = capture_async(ser_gm, GRANDMASTER_PORT, 2.0)

    # WAIT_TXMCTL-Spam sofort erkennen
    wait_txmctl_count = captured.count("WAIT_TXMCTL cb timeout")
    if wait_txmctl_count > 0:
        fail("GM-Zustand: keine WAIT_TXMCTL-Schleife",
             f"{wait_txmctl_count}× 'WAIT_TXMCTL cb timeout' in 2 s")
        hint(
            "BEKANNTER BUG in ptp_gm_task.c GM_STATE_SEND_SYNC:\n"
            "  DRV_LAN865X_ReadRegister() gibt TCPIP_MAC_RES_OK==0 bei Erfolg zurück.\n"
            "  Die Bedingung  if (!DRV_LAN865X_ReadRegister(...))  ist **invertiert**:\n"
            "    !0 == true  → Code denkt der Aufruf scheiterte → springt zu WAIT_PERIOD\n"
            "    !nonzero == false → landet in WAIT_TXMCTL, aber Callback kam nie an\n"
            "  FIX in ptp_gm_task.c, GM_STATE_SEND_SYNC:\n"
            "    if (!DRV_LAN865X_ReadRegister(...))  →  if (DRV_LAN865X_ReadRegister(...) != TCPIP_MAC_RES_OK)"
        )
        gm_ok = False
    elif gm_ok:
        ok("GM-Zustand: keine WAIT_TXMCTL-Schleife in 2 s")

    return gm_ok and fl_ok


# ---------------------------------------------------------------------------
# Schritt 3: GM Sync-Zähler steigt
# ---------------------------------------------------------------------------

def _parse_ptp_status(response: str):
    m = re.search(r'\[PTP\]\s+mode=(\w+)\s+gmSyncs=(\d+)\s+gmState=(\d+)', response)
    if not m:
        return None
    return {
        "mode":    m.group(1),
        "gmSyncs": int(m.group(2)),
        "gmState": int(m.group(3)),
    }


def step3_gm_syncs(ser_gm) -> bool:
    section("Schritt 3: GM sendet Sync-Frames?  (gmSyncs-Zähler)")

    resp1 = send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_status")
    st1   = _parse_ptp_status(resp1)

    if st1 is None:
        fail("ptp_status parsebar")
        hint("Firmware nicht geladen? Falscher COM-Port?")
        return False

    state_name = GM_STATE_NAMES.get(st1["gmState"], f"?({st1['gmState']})")
    print(f"\n  gmSyncs={st1['gmSyncs']}  gmState={st1['gmState']} ({state_name})")

    if st1["gmState"] == 4:  # GM_STATE_WAIT_TXMCTL
        hint(
            "gmState=4 = WAIT_TXMCTL: Maschine steckt im Callback-Timeout.\n"
            "Ursache ist sehr wahrscheinlich der invertierte Rückgabewert-Check\n"
            "(siehe Hinweis in Schritt 2)."
        )

    print(f"\n  Warte {SYNC_WAIT_S:.1f} s auf >= {SYNC_MIN_DELTA} neue Syncs ...")
    # Laufende WAIT_TXMCTL-Meldungen zählen
    captured = capture_async(ser_gm, GRANDMASTER_PORT, SYNC_WAIT_S)
    drain(ser_gm)

    resp2 = send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_status")
    st2   = _parse_ptp_status(resp2)

    if st2 is None:
        return fail("ptp_status nach Wartezeit parsebar")

    delta = st2["gmSyncs"] - st1["gmSyncs"]
    state_name2 = GM_STATE_NAMES.get(st2["gmState"], f"?({st2['gmState']})")
    print(f"  gmSyncs={st2['gmSyncs']}  gmState={st2['gmState']} ({state_name2})  delta={delta}")

    if delta >= SYNC_MIN_DELTA:
        return ok(f"gmSyncs stieg um {delta} (≥ {SYNC_MIN_DELTA})",
                  f"vorher={st1['gmSyncs']} nachher={st2['gmSyncs']}")

    result = fail(f"gmSyncs stieg um {delta} (erwartet ≥ {SYNC_MIN_DELTA})",
                  f"vorher={st1['gmSyncs']} nachher={st2['gmSyncs']}")

    wait_txmctl_n = captured.count("WAIT_TXMCTL cb timeout")
    if wait_txmctl_n > 0:
        hint(
            f"{wait_txmctl_n}× WAIT_TXMCTL in {SYNC_WAIT_S:.1f} s → GM-State-Machine sendet keine Syncs.\n"
            "GM arbeitet zwar (Timer-Callback läuft), aber der DRV_LAN865X_ReadRegister-\n"
            "Rückgabewert-Bug verhindert korrekte Zustandsübergänge.\n"
            "\n"
            "Nächster Schritt: Firmware-Fix in src/ptp_gm_task.c, dann neu flashen."
        )
    else:
        hint(
            "gmSyncs bleibt bei 0 und keine WAIT_TXMCTL-Meldungen.\n"
            "Mögliche Ursachen:\n"
            "  • PTP_GM_Service() wird nicht aufgerufen (GM_TimerCallback läuft?)\n"
            "  • ptp_mode master wurde nicht angenommen\n"
            "  • PLCA-Verbindung down → SendRawEthFrame schlägt fehl\n"
            "  • gmState steckt in IDLE oder WAIT_PERIOD ohne weiterzumachen"
        )
    return result


# ---------------------------------------------------------------------------
# Schritt 4: TX-Match-Register TXPMDET
# ---------------------------------------------------------------------------

def _parse_lan_read_value(output: str, addr_str: str) -> int | None:
    """
    Parst 'LAN865X Read: Addr=0x00040040 Value=0xXXXXXXXX'.
    addr_str z.B. '0x00040040'
    """
    # Kanonische Form: Adresse mit 8 Hex-Ziffern
    addr_int = int(addr_str, 0)
    addr_pat = f"0x{addr_int:08X}"
    m = re.search(
        rf'LAN865X Read: Addr={addr_pat}\s+Value=(0x[0-9A-Fa-f]+)',
        output
    )
    if m:
        return int(m.group(1), 16)
    # Variante ohne führende Nullen
    m2 = re.search(r'LAN865X Read: Addr=\S+\s+Value=(0x[0-9A-Fa-f]+)', output)
    if m2:
        return int(m2.group(1), 16)
    return None


def step4_txmctl(ser_gm) -> bool:
    section("Schritt 4: TX-Match-Register TXPMDET  (GM_TXMCTL @ 0x00040040)")

    print(f"\n  Lese GM_TXMCTL {TXMCTL_POLL_N}× in ~{TXMCTL_POLL_N * 0.2:.0f} s "
          f"(suche TXPMDET-Bit 0x0080) ...")

    TXPMDET = 0x0080
    detected_any   = False
    any_read_ok    = False
    read_values    = []

    for i in range(TXMCTL_POLL_N):
        send_cmd(ser_gm, GRANDMASTER_PORT,
                 f"lan_read {TXMCTL_ADDR}", timeout=1.0)
        time.sleep(0.2)
        captured = capture_async(ser_gm, GRANDMASTER_PORT, 0.3)
        val = _parse_lan_read_value(captured, TXMCTL_ADDR)
        if val is not None:
            any_read_ok = True
            read_values.append(val)
            txpmdet_set = bool(val & TXPMDET)
            armed       = bool(val & 0x0002)
            print(f"  [{i+1}/{TXMCTL_POLL_N}] TXMCTL=0x{val:08X}  "
                  f"ARMED={'ja' if armed else 'nein'}  "
                  f"TXPMDET={'JA ✓' if txpmdet_set else 'nein'}")
            if txpmdet_set:
                detected_any = True
        else:
            print(f"  [{i+1}/{TXMCTL_POLL_N}] Keine Antwort erhalten")

    if not any_read_ok:
        result = fail("GM_TXMCTL lesbar")
        hint(
            "lan_read liefert keine Antwort.\n"
            "  • DRV_LAN865X_ReadRegister() gibt Fehler zurück → Treiber nicht bereit?\n"
            "  • LAN8651 über SPI erreichbar? → prüfe Gerät-Index 0"
        )
        return result

    ok("GM_TXMCTL lesbar", f"Werte: {[f'0x{v:08X}' for v in read_values]}")

    if detected_any:
        return ok("TXPMDET-Bit wurde mindestens 1× gesetzt",
                  "GM-Hardware erkennt Sync-Frame im TX-Strom")

    result = fail("TXPMDET-Bit nie gesetzt",
                  "0x00040040 zeigte TXPMDET=0 bei allen Lesungen")
    hint(
        "TXPMDET wird nicht gesetzt. Mögliche Ursachen:\n"
        "  A) State-Machine schickt gar keinen Sync-Frame ab\n"
        "     → Schritt 3 bereits fehlgeschlagen? gmSyncs=0?\n"
        "  B) TX-Match falsch konfiguriert (TXMLOC/TXMPATH/TXMPATL)\n"
        "     Erwartet: TXMLOC=30, TXMPATH=0x88, TXMPATL=0xF710\n"
        "     → lan_read 0x00040041 (TXMPATH) / 0x00040042 (TXMPATL)\n"
        "  C) TXMCTL wird nicht vor jedem Sync mit 0x02 neu gesetzt (kein Arm)\n"
        "  D) DRV_LAN865X_ReadRegister Rückgabewert-Bug: State-Machine liest\n"
        "     TXMCTL nie ab weil sie nach dem Arm sofort zu WAIT_PERIOD springt"
    )
    return result


# ---------------------------------------------------------------------------
# Schritt 5: Follower empfängt PTP-Frames
# ---------------------------------------------------------------------------

def step5_follower_rx(ser_fl) -> bool:
    section("Schritt 5: PTP-Frames auf Follower  (ipdump, EtherType 0x88F7)")

    print(f"\n  ipdump 1 auf {FOLLOWER_PORT} — Lausche {IPDUMP_SECS:.0f} s ...")
    send_cmd(ser_fl, FOLLOWER_PORT, "ipdump 1", timeout=2.0)
    captured = capture_async(ser_fl, FOLLOWER_PORT, IPDUMP_SECS)
    send_cmd(ser_fl, FOLLOWER_PORT, "ipdump 0", timeout=2.0)

    ptp_count = len(re.findall(r'E0:PTP\[0x88F7\]', captured))
    print(f"\n  PTP-Frames gezählt: {ptp_count}")

    if ptp_count >= MIN_PTP_FRAMES:
        result = ok(f">= {MIN_PTP_FRAMES} PTP-Frames empfangen",
                    f"gefunden: {ptp_count} in {IPDUMP_SECS:.0f} s")

        # Sync / Follow_Up unterscheiden
        # Frame-Typ steht im ersten PTP-Payloadbyte [14] nach Ethernet-Header (tsmt & 0x0F)
        # Aus dem Dump: "E0:PTP[0x88F7]" gefolgt von Hex-Dump
        sync_count = 0
        fu_count   = 0
        lines = captured.splitlines()
        for i, line in enumerate(lines):
            if "E0:PTP[0x88F7]" not in line:
                continue
            # Nächste Hex-Zeile enthält Ethernet-Header:
            # Byte [14] = PTP tsmt → msgType = tsmt & 0x0F
            # Typ 0x0 = Sync, 0x8 = Follow_Up
            # Im Hex-Dump steht die dritte Zeile mit Offset 0x10 usw.
            # Einfacher: ersten Hex-Dump-Block ab nächster Zeile sammeln
            raw = []
            for j in range(i + 1, min(i + 10, len(lines))):
                hm = re.match(r'\s*[0-9a-fA-F]{8}:\s+((?:\s*[0-9a-fA-F]{2})+)', lines[j])
                if hm:
                    raw.extend(int(b, 16) for b in hm.group(1).split())
                else:
                    break
            if len(raw) >= 15:
                msg_type = raw[14] & 0x0F
                if msg_type == 0x0:
                    sync_count += 1
                elif msg_type == 0x8:
                    fu_count += 1

        if sync_count > 0 or fu_count > 0:
            ok(f"Frame-Typen erkannt: Sync={sync_count}  Follow_Up={fu_count}")

        return result

    result = fail(f">= {MIN_PTP_FRAMES} PTP-Frames empfangen",
                  f"gefunden: {ptp_count} in {IPDUMP_SECS:.0f} s")
    hint(
        "Follower empfängt keine PTP-Frames (EtherType 0x88F7).\n"
        "Das kann bedeuten:\n"
        "  A) GM sendet gar keine Sync-Frames (gmSyncs=0)\n"
        "     → Schritt 3 bereits fehlgeschlagen → Firmware-Bug beheben\n"
        "  B) PLCA-Bus down (beide Boards senden/empfangen auf T1S?)\n"
        "     → Ping-Test nochmals versuchen nach PTP-Mode-Aktivierung\n"
        "  C) Multicast-Filterung blockiert 01:80:C2:00:00:0E\n"
        "     → ipdump 3 auf Follower versuchen (eth0 + eth1)\n"
        "  D) PTP-Frame wird von pktEth0Handler abgefangen bevor DumpMem\n"
        "     → ipdump prüft frameType == 0x88F7 und gibt E0:PTP aus — ist OK"
    )
    return result


# ---------------------------------------------------------------------------
# Schritt 6: GM FollowUp-Pfad — laeuft GM durch SEND_FOLLOWUP?
# ---------------------------------------------------------------------------

def step6_gm_followup_path(ser_gm, ser_fl) -> bool:
    section("Schritt 6: GM FollowUp-Pfad prüfen (SEND_FOLLOWUP im State-Log?)")

    print(f"\n  ptp_mode follower/master aktivieren ...")
    send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode follower", timeout=3.0)
    time.sleep(0.3)
    send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode master",   timeout=3.0)

    print(f"\n  Lausche {FOLLOWUP_LISTEN_S:.0f} s auf GM- und FOL-Ausgaben ...")
    gm_buf, fol_buf = capture_dual(ser_gm, GRANDMASTER_PORT,
                                   ser_fl, FOLLOWER_PORT,
                                   FOLLOWUP_LISTEN_S)

    # GM FollowUp-Pfad-States
    path_states = [
        "GM_STATE_READ_TXMCTL",
        "GM_STATE_WAIT_TXMCTL",
        "GM_STATE_WAIT_STATUS0",
        "GM_STATE_READ_TTSCA_H",
        "GM_STATE_READ_TTSCA_L",
        "GM_STATE_WRITE_CLEAR",
        "GM_STATE_SEND_FOLLOWUP",
    ]
    found = {s: s in gm_buf for s in path_states}
    followup_n       = gm_buf.count("GM_STATE_SEND_FOLLOWUP")
    txpmdet_timeouts = gm_buf.count("TXPMDET timeout")

    # FOL-Empfang prüfen
    fol_ptp_rx = fol_buf.count("0x88F7") + fol_buf.count("PTP_FOL_OnFrame")

    st = send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_status", timeout=3.0)
    m = re.search(r"gmSyncs=(\d+)", st)
    gm_syncs = int(m.group(1)) if m else -1

    print(f"\n  FollowUp State-Log-Prüfung:")
    for s, v in found.items():
        print(f"    {'✓' if v else '✗'} {s}")
    print(f"\n  gmSyncs         : {gm_syncs}")
    print(f"  SEND_FOLLOWUP×  : {followup_n}")
    print(f"  TXPMDET timeouts: {txpmdet_timeouts}")
    print(f"  FOL PTP-Empfang : {fol_ptp_rx}")

    passed = found.get("GM_STATE_SEND_FOLLOWUP", False)

    if passed:
        ok("GM_STATE_SEND_FOLLOWUP im Log", f"{followup_n}× in {FOLLOWUP_LISTEN_S:.0f} s")
    else:
        fail("GM_STATE_SEND_FOLLOWUP NICHT gefunden")
        if txpmdet_timeouts > 0:
            hint(
                f"{txpmdet_timeouts}× TXPMDET timeout — TX-Match-Hardware sieht keinen Sync-Frame.\n"
                "  Prüfe TXMLOC/TXMPATH/TXMPATL-Konfiguration (--dump-txmatch)."
            )
        elif "GM_STATE_READ_TXMCTL" not in gm_buf:
            hint(
                "GM verbleibt in WAIT_PERIOD — SEND_SYNC wird nicht ausgeführt.\n"
                "  Ist ptp_mode master korrekt initialisiert? Timer-Callback aktiv?"
            )
        elif "GM_STATE_WAIT_TXMCTL" in gm_buf and followup_n == 0:
            hint(
                "State-Machine erreicht READ/WAIT_TXMCTL aber nicht SEND_FOLLOWUP.\n"
                "  Mögliche Ursache: STATUS0-Bit TTSCA nie gesetzt (kein Timestamp abgefangen).\n"
                "  Prüfe TXMCTL-Arm-Logik in ptp_gm_task.c GM_STATE_SEND_SYNC."
            )

    # PTP wieder ausschalten (für nächste Schritte sauber)
    send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode off", timeout=3.0)
    send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode off", timeout=3.0)
    time.sleep(0.5)
    return passed


# ---------------------------------------------------------------------------
# Schritt 7: Follower-Konvergenz — UNINIT → MATCHFREQ → HARDSYNC → COARSE → FINE
# ---------------------------------------------------------------------------

def step7_fol_convergence(ser_gm, ser_fl) -> bool:
    section("Schritt 7: Follower-Servo-Konvergenz  (bis FINE oder Timeout)")

    send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode follower", timeout=3.0)
    time.sleep(0.3)
    send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode master",   timeout=3.0)

    print(f"\n  Polling ptp_offset alle {POLL_INTERVAL_S:.1f} s, Timeout {CONVERGENCE_TIMEOUT_S:.0f} s ...")

    fol_log, gm_log = [], []
    stop_log = threading.Event()

    def _log_reader(ser, name, buf):
        while not stop_log.is_set():
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                buf.append(chunk)
                for line in chunk.splitlines():
                    if line.strip() and any(kw in line for kw in
                                            ["PTP", "MATCH", "COARSE", "FINE", "SYNC",
                                             "offset", "STATE", "FOLLOW"]):
                        print(f"  [{name}] {line}")
            else:
                time.sleep(0.02)

    ta = threading.Thread(target=_log_reader, args=(ser_gm, GRANDMASTER_PORT, gm_log), daemon=True)
    tb = threading.Thread(target=_log_reader, args=(ser_fl, FOLLOWER_PORT, fol_log), daemon=True)
    ta.start(); tb.start()

    start_ts       = time.time()
    converge_time  = None
    converged      = False

    while time.time() - start_ts < CONVERGENCE_TIMEOUT_S:
        time.sleep(POLL_INTERVAL_S)
        elapsed = time.time() - start_ts
        resp = send_cmd(ser_fl, FOLLOWER_PORT, "ptp_offset", timeout=3.0)
        m = re.search(r"offset=(-?\d+)\s*ns\s+abs=(\d+)\s*ns", resp)
        if m:
            off_ns  = int(m.group(1))
            abs_ns  = int(m.group(2))
            fol_text = "".join(fol_log)
            in_fine  = "PTP FINE" in fol_text
            phase    = "FINE" if in_fine else ("COARSE" if "PTP COARSE" in fol_text else
                       "MATCHFREQ" if "MATCHFREQ" in fol_text else "UNINIT")
            print(f"  [{elapsed:5.1f}s] [{phase}] offset={off_ns:+10d} ns  abs={abs_ns} ns")
            if in_fine and converge_time is None:
                converge_time = elapsed
                converged = True
                print(f"\n  *** Servo erreicht FINE nach {elapsed:.1f} s ***")
                break
        else:
            print(f"  [{elapsed:5.1f}s] ptp_offset: kein Wert  ({resp.strip()[:60]!r})")

    stop_log.set()
    ta.join(timeout=1.0); tb.join(timeout=1.0)

    fol_text  = "".join(fol_log)
    matchfreq = "MATCHFREQ" in fol_text
    coarse    = "PTP COARSE" in fol_text

    print(f"\n  Servo-Phasen beobachtet:")
    print(f"    UNINIT→MATCHFREQ : {'✓' if matchfreq else '✗'}")
    print(f"    HARDSYNC/COARSE  : {'✓' if coarse    else '✗'}")
    print(f"    FINE             : {'✓' if converged else '✗'}")
    print(f"    Konvergenzzeit   : {converge_time:.1f} s" if converge_time else
          f"    Konvergenzzeit   : TIMEOUT ({CONVERGENCE_TIMEOUT_S:.0f} s)")

    if converged:
        ok("Servo konvergiert auf FINE", f"{converge_time:.1f} s")
    else:
        fail("FINE nicht erreicht innerhalb Timeout")
        if not matchfreq:
            hint(
                "MATCHFREQ nie gesehen — FOL empfängt keine Sync/FollowUp-Frames.\n"
                "  Schritt 6 (FollowUp-Pfad) bestanden? GM sendet FollowUp wirklich?"
            )
        elif not coarse:
            hint(
                "MATCHFREQ gesehen aber kein COARSE — Offset-Berechnung blockiert?\n"
                "  FollowUp-Timestamps korrekt? MAC_TI/MAC_TISUBN-Register zugänglich?"
            )
        else:
            hint(
                "COARSE gesehen aber kein FINE — Regler konvergiert zu langsam / schwingt.\n"
                "  Prüfe Servo-Parameter in PTP_FOL_task.c (PID-Koeffizienten)."
            )

    return converged


# ---------------------------------------------------------------------------
# Schritt 8: Offset-Stabilität — 60 s Messung nach Konvergenz
# ---------------------------------------------------------------------------

def step8_offset_stability(ser_fl, duration_s: float) -> bool:
    section(f"Schritt 8: Offset-Stabilitätsmessung  ({duration_s:.0f} s)")

    offsets     = []
    start_ts    = time.time()
    deadline    = start_ts + duration_s
    consec_ok   = 0

    while time.time() < deadline:
        elapsed = time.time() - start_ts
        resp = send_cmd(ser_fl, FOLLOWER_PORT, "ptp_offset", timeout=3.0)
        m = re.search(r"offset=(-?\d+)\s*ns\s+abs=(\d+)\s*ns", resp)
        if m:
            off_ns = int(m.group(1))
            offsets.append(off_ns)
            under = abs(off_ns) < CONVERGE_THRESHOLD_NS
            consec_ok = consec_ok + 1 if under else 0
            print(f"  [{elapsed:5.1f}s] offset={off_ns:+10d} ns  "
                  f"{'< ' + str(CONVERGE_THRESHOLD_NS) + ' ns ✓' if under else '> threshold'}"
                  f"  (consec_ok={consec_ok})")
        else:
            print(f"  [{elapsed:5.1f}s] kein Wert")
        time.sleep(POLL_INTERVAL_S)

    if not offsets:
        fail("Keine Offset-Messwerte empfangen")
        return False

    abs_offsets = [abs(o) for o in offsets]
    mean_ns   = statistics.mean(offsets)
    stdev_ns  = statistics.stdev(offsets) if len(offsets) > 1 else 0.0
    min_ns    = min(offsets)
    max_ns    = max(offsets)
    abs_mean  = statistics.mean(abs_offsets)
    n_under   = sum(1 for v in abs_offsets if v < CONVERGE_THRESHOLD_NS)
    pct_under = 100.0 * n_under / len(offsets)

    print(f"\n  Statistik ({len(offsets)} Messungen):")
    print(f"    Mittelwert  : {mean_ns:+.1f} ns")
    print(f"    Std-Abw     : {stdev_ns:.1f} ns")
    print(f"    Min / Max   : {min_ns:+d} ns / {max_ns:+d} ns")
    print(f"    |abs| mean  : {abs_mean:.1f} ns")
    print(f"    < {CONVERGE_THRESHOLD_NS} ns    : {n_under}/{len(offsets)} = {pct_under:.0f}%")

    passed = pct_under >= 80.0
    if passed:
        ok(f"Offset stabil: {pct_under:.0f}% der Messungen < {CONVERGE_THRESHOLD_NS} ns",
           f"mean={mean_ns:+.1f} ns  stdev={stdev_ns:.1f} ns")
    else:
        fail(f"Offset instabil: nur {pct_under:.0f}% < {CONVERGE_THRESHOLD_NS} ns",
             f"min={min_ns:+d}  max={max_ns:+d}  stdev={stdev_ns:.1f} ns")
        hint(
            "Servo ist nicht stabil — mögliche Ursachen:\n"
            "  • Regler-Loop zu aggressiv (Integral-Term zu gross)\n"
            "  • Verzögerungen im T1S-Link (PLCA-Kollisionen?)\n"
            "  • clock_adjust Quantisierung (MAC_TISUBN Auflösung)\n"
            "  • Sporadische OS-Scheduling-Jitter ( vTaskDelay-basierter Loop)"
        )
    return passed


# ---------------------------------------------------------------------------
# Zusätzliche TX-Match-Konfiguration auslesen (Debugging-Hilfe)
# ---------------------------------------------------------------------------

def dump_txmatch_config(ser_gm) -> None:
    section("Zusatz: TX-Match-Konfiguration auslesen (GM_TXMLOC / TXMPATH / TXMPATL)")

    regs = {
        "GM_TXMCTL  (0x00040040) — Control/Status": "0x00040040",
        "GM_TXMPATH (0x00040041) — Pattern High":   "0x00040041",
        "GM_TXMPATL (0x00040042) — Pattern Low":    "0x00040042",
        "GM_TXMMSKH (0x00040043) — Mask High":      "0x00040043",
        "GM_TXMMSKL (0x00040044) — Mask Low":       "0x00040044",
        "GM_TXMLOC  (0x00040045) — Pattern Location":"0x00040045",
    }
    expected = {
        "0x00040041": (0x88, "0x88 [EtherType High-Byte]"),
        "0x00040042": (0xF710, "0xF710 [EtherType Low + nächstes Byte]"),
        "0x00040045": (30, "30 [Byte-Offset des EtherType im Frame]"),
    }

    for label, addr in regs.items():
        send_cmd(ser_gm, GRANDMASTER_PORT, f"lan_read {addr}", timeout=1.0)
        time.sleep(0.15)
        captured = capture_async(ser_gm, GRANDMASTER_PORT, 0.3)
        val = _parse_lan_read_value(captured, addr)
        if val is None:
            print(f"  {label}: --- (keine Antwort)")
        else:
            exp = expected.get(addr)
            if exp:
                match = "✓" if val == exp[0] else f"✗ erwartet {exp[1]}"
                print(f"  {label}: 0x{val:08X}  {match}")
            else:
                print(f"  {label}: 0x{val:08X}")


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="T1S PTP Bridge — Bottom-Up Diagnosetest + Konvergenztest"
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Board-Reset überspringen"
    )
    parser.add_argument(
        "--from-step", "-s",
        type=int, default=0, metavar="N",
        help="Ab Schritt N starten (0=alles, 1=ab Ping, 6=ab FollowUp-Pfad, ...)"
    )
    parser.add_argument(
        "--duration",
        type=float, default=STABILITY_DURATION_S, metavar="S",
        help=f"Dauer der Stabilitätsmessung in Sekunden (default {STABILITY_DURATION_S:.0f})"
    )
    parser.add_argument(
        "--dump-txmatch",
        action="store_true",
        help="TX-Match-Konfigurationsregister am Ende auslesen"
    )
    args = parser.parse_args()

    # Ausgabe gleichzeitig auf Konsole und in Logdatei
    log_path = f"ptp_diag_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    tee = _Tee(sys.stdout, log_path)
    sys.stdout = tee
    sys.stderr = tee
    print(f"[LOG] Ausgabe wird gespeichert in: {log_path}")

    try:
        _main_run(args, log_path)
    finally:
        tee.close()


def _main_run(args, log_path):
    try:
        ser_fl = open_port(FOLLOWER_PORT)
    except serial.SerialException as e:
        print(f"FEHLER: {FOLLOWER_PORT} nicht öffenbar: {e}")
        sys.exit(1)
    try:
        ser_gm = open_port(GRANDMASTER_PORT)
    except serial.SerialException as e:
        print(f"FEHLER: {GRANDMASTER_PORT} nicht öffenbar: {e}")
        ser_fl.close()
        sys.exit(1)

    results = {}  # step_nr → (name, passed)

    try:
        wake_port(ser_fl, FOLLOWER_PORT)
        wake_port(ser_gm, GRANDMASTER_PORT)

        if not args.no_reset and args.from_step == 0:
            print("\n=== Board-Reset ===")
            ser_fl.write(b"reset\r\n")
            ser_gm.write(b"reset\r\n")
            print("Warte 8 s auf Neustart ...")
            time.sleep(8)
            ser_fl.reset_input_buffer()
            ser_gm.reset_input_buffer()
            wake_port(ser_fl, FOLLOWER_PORT)
            wake_port(ser_gm, GRANDMASTER_PORT)

        # -----------------------------------------------------------------------
        # Schritt 0: IP
        # -----------------------------------------------------------------------
        if args.from_step <= 0:
            p = step0_setip(ser_gm, ser_fl)
            results[0] = ("IP-Konfiguration", p)
            if not p:
                print("\n  ABBRUCH: setip fehlgeschlagen — folgende Schritte übersprungen.")
                _print_summary(results)
                sys.exit(1)

        # -----------------------------------------------------------------------
        # Schritt 1: Ping
        # -----------------------------------------------------------------------
        if args.from_step <= 1:
            p = step1_ping(ser_gm, ser_fl)
            results[1] = ("Ping beidseitig", p)
            if not p:
                _print_summary(results)
                sys.exit(1)

        # -----------------------------------------------------------------------
        # Schritt 2: PTP aktivieren
        # -----------------------------------------------------------------------
        if args.from_step <= 2:
            p = step2_ptp_activate(ser_gm, ser_fl)
            results[2] = ("PTP aktivieren", p)
            # Kein hard exit — wir wollen immer noch gmSyncs prüfen

        # -----------------------------------------------------------------------
        # Schritt 3: GM Sync-Zähler
        # -----------------------------------------------------------------------
        if args.from_step <= 3:
            p = step3_gm_syncs(ser_gm)
            results[3] = ("GM Sync-Zähler", p)

        # -----------------------------------------------------------------------
        # Schritt 4: TXPMDET
        # -----------------------------------------------------------------------
        if args.from_step <= 4:
            p = step4_txmctl(ser_gm)
            results[4] = ("TX-Match TXPMDET", p)

        # -----------------------------------------------------------------------
        # Schritt 5: Follower RX
        # -----------------------------------------------------------------------
        if args.from_step <= 5:
            p = step5_follower_rx(ser_fl)
            results[5] = ("Follower PTP-RX", p)
            # PTP nach Schritt 5 ausschalten (Schritt 6 startet ihn sauber neu)
            send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode off", timeout=3.0)
            send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode off", timeout=3.0)
            time.sleep(1.0)

        # -----------------------------------------------------------------------
        # Schritt 6: GM FollowUp-Pfad
        # -----------------------------------------------------------------------
        if args.from_step <= 6:
            p = step6_gm_followup_path(ser_gm, ser_fl)
            results[6] = ("GM FollowUp-Pfad", p)

        # -----------------------------------------------------------------------
        # Schritt 7: Follower-Konvergenz
        # -----------------------------------------------------------------------
        if args.from_step <= 7:
            p = step7_fol_convergence(ser_gm, ser_fl)
            results[7] = ("FOL Servo Konvergenz FINE", p)
            if not p:
                print("\n  Schritt 8 übersprungen — Servo nicht konvergiert.")
                results[8] = ("Offset Stabilität", False)
                _print_summary(results)
                # PTP ausschalten
                send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode off", timeout=3.0)
                send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode off", timeout=3.0)
                return

        # -----------------------------------------------------------------------
        # Schritt 8: Offset-Stabilität (PTP bleibt jetzt aktiv von Schritt 7)
        # -----------------------------------------------------------------------
        if args.from_step <= 8:
            p = step8_offset_stability(ser_fl, args.duration)
            results[8] = ("Offset Stabilität", p)

        # -----------------------------------------------------------------------
        # Optional: TX-Match-Config-Dump
        # -----------------------------------------------------------------------
        if args.dump_txmatch:
            dump_txmatch_config(ser_gm)

    finally:
        # Sauber aufräumen
        try:
            send_cmd(ser_gm, GRANDMASTER_PORT, "ptp_mode off", timeout=3.0)
            send_cmd(ser_fl, FOLLOWER_PORT,    "ptp_mode off", timeout=3.0)
        except Exception:
            pass
        ser_fl.close()
        ser_gm.close()
        print("\nSerielle Verbindungen geschlossen.")

    _print_summary(results)
    print(f"\n[LOG] Vollständige Ausgabe gespeichert in: {log_path}")
    all_ok = all(v for _, v in results.values())
    sys.exit(0 if all_ok else 1)


def _print_summary(results: dict) -> None:
    names = {
        0: "IP-Konfiguration",
        1: "Ping beidseitig",
        2: "PTP aktivieren",
        3: "GM Sync-Zähler steigt",
        4: "TX-Match TXPMDET",
        5: "Follower PTP-RX",
        6: "GM FollowUp-Pfad",
        7: "FOL Servo Konvergenz FINE",
        8: "Offset Stabilität (60 s)",
    }
    print("\n" + "=" * 60)
    print("DIAGNOSEERGEBNIS")
    print("=" * 60)
    for step in sorted(results.keys()):
        name, passed = results[step]
        mark = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {mark}  Schritt {step}: {name}")
    n_pass = sum(1 for _, (_, p) in results.items() if p)
    n_total = len(results)
    print("-" * 60)
    print(f"  {n_pass}/{n_total} Schritte bestanden")
    print("=" * 60)

    if n_pass < n_total:
        first_fail = next(
            (s for s in sorted(results.keys()) if not results[s][1]), None
        )
        if first_fail is not None:
            print(f"\nErster fehlgeschlagener Schritt: {first_fail} — {names.get(first_fail, '')}")
            print("Behebe diesen Fehler und führe das Skript erneut aus.")
            if first_fail <= 3:
                print("\nNächste empfohlene Maßnahme:")
                print("  Prüfe / fixe den Firmware-Bug in src/ptp_gm_task.c")
                print("  (DRV_LAN865X_ReadRegister Rückgabewert-Check in GM_STATE_SEND_SYNC)")


if __name__ == "__main__":
    main()
