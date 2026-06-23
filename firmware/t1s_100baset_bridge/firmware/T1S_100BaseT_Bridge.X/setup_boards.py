
#!/usr/bin/env python3
"""
setup_boards.py
---------------
Konfiguriert Follower (COM8) und Grandmaster (COM10) über das MPLAB-Harmony CLI:
  - Setzt IP-Adressen per setip-Befehl
  - Verifiziert Konnektivität per ping in beide Richtungen
"""

import serial
import time
import sys

# ---------------------------------------------------------------------------
# Konfiguration
# ---------------------------------------------------------------------------
FOLLOWER_PORT   = "COM8"
GRANDMASTER_PORT = "COM10"
BAUDRATE        = 115200
SERIAL_TIMEOUT  = 3         # Sekunden Lese-Timeout pro Chunk

FOLLOWER_IP     = "192.168.0.20"
GRANDMASTER_IP  = "192.168.0.30"
NETMASK         = "255.255.255.0"
INTERFACE       = "eth0"

FOLLOWER_MAC    = "00:04:25:01:02:01"
GRANDMASTER_MAC = "00:04:25:01:02:00"

PROMPT_MARKERS  = ["\r\n> ", "\n> ", "> "]   # Harmony SYS_CONSOLE Prompt

def mac_to_spec_add_regs(mac_str: str):
    """Berechnet SPEC_ADD2_BOTTOM, SPEC_ADD2_TOP, SPEC_ADD1_BOTTOM aus MAC-String.

    Der LAN865x-Treiber schreibt beim Boot folgende Register (aus _InitUserSettings):
      SPEC_ADD2_BOTTOM (0x00010024) = mac[3]<<24 | mac[2]<<16 | mac[1]<<8 | mac[0]
      SPEC_ADD2_TOP    (0x00010025) = mac[5]<<8  | mac[4]
      SPEC_ADD1_BOTTOM (0x00010022) = mac[5]<<24 | mac[4]<<16 | mac[3]<<8 | mac[2]
    setmac aktualisiert nur den SW-Stack, nicht diese HW-Register.  Wir schreiben
    sie daher nach jedem setmac direkt per lan_write nach.
    """
    m = [int(b, 16) for b in mac_str.split(':')]
    bottom2 = (m[3] << 24) | (m[2] << 16) | (m[1] << 8) | m[0]
    top2    = (m[5] <<  8) | m[4]
    bottom1 = (m[5] << 24) | (m[4] << 16) | (m[3] << 8) | m[2]
    return bottom2, top2, bottom1
RESPONSE_TIMEOUT = 5.0    # Sekunden maximaler Warte-Zeitraum je Befehl
PING_TIMEOUT     = 10.0   # Sekunden für Ping-Antwort


# ---------------------------------------------------------------------------
# Hilfsfunktionen
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
            # Harmony gibt nach der Ausgabe wieder den Prompt aus
            if any(response.endswith(p) for p in PROMPT_MARKERS):
                break
            # Alternativ: einfach kurz warten und Rest lesen, falls kein Prompt
        else:
            time.sleep(0.05)

    # Nochmal restliche Bytes lesen
    time.sleep(0.1)
    if ser.in_waiting:
        response += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")

    # Ausgabe ohne Echo der Zeile
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


def if_up(ser: serial.Serial, port_name: str, iface: str,
           retries: int = 10, retry_delay: float = 2.0) -> bool:
    """Bringt ein Interface hoch und wartet bis Link UP meldet."""
    # Zuerst prüfen ob schon up
    resp = send_cmd(ser, port_name, "netinfo", timeout=RESPONSE_TIMEOUT)
    if f"Interface <{iface}/" in resp and "Link is UP" in resp:
        print(f"[{port_name}] {iface} ist bereits UP.")
        return True

    send_cmd(ser, port_name, f"if {iface} up", timeout=RESPONSE_TIMEOUT)

    for attempt in range(1, retries + 1):
        time.sleep(retry_delay)
        resp = send_cmd(ser, port_name, "netinfo", timeout=RESPONSE_TIMEOUT)
        if "Link is UP" in resp or "Status: Ready" in resp:
            print(f"[{port_name}] {iface} ist UP nach {attempt * retry_delay:.0f} s.")
            return True
        print(f"[{port_name}] Warte auf Link UP ({attempt}/{retries}) ...")

    print(f"[{port_name}] FEHLER: {iface} nicht UP nach {retries * retry_delay:.0f} s.")
    return False


def set_ip(ser: serial.Serial, port_name: str, ip: str, mask: str, iface: str,
           retries: int = 3, retry_delay: float = 2.0) -> bool:
    for attempt in range(1, retries + 1):
        cmd = f"setip {iface} {ip} {mask}"
        resp = send_cmd(ser, port_name, cmd, timeout=RESPONSE_TIMEOUT)
        fail_markers = ["Error", "error", "Usage", "No such", "not found", "failed"]
        if any(m in resp for m in fail_markers):
            print(f"[{port_name}] Versuch {attempt}/{retries} fehlgeschlagen — warte {retry_delay:.0f} s ...")
            if attempt < retries:
                time.sleep(retry_delay)
            continue
        if "OK" in resp or "ok" in resp or "Set ip" in resp:
            print(f"[{port_name}] IP {ip}/{mask} auf {iface} gesetzt.")
            return True
        # Keine bekannte Antwort — trotzdem als Erfolg werten wenn kein Fehler
        print(f"[{port_name}] IP {ip}/{mask} auf {iface} gesetzt (Antwort: {resp.strip()!r})")
        return True
    print(f"[{port_name}] FEHLER: setip nach {retries} Versuchen nicht erfolgreich.")
    return False


def collect_unsolicited(ser: serial.Serial, port_name: str, extra_wait: float = 1.5) -> str:
    """Liest alle noch gepufferten Bytes (ipdump-Ausgabe, ohne auf Prompt zu warten)."""
    time.sleep(extra_wait)
    data = ""
    while ser.in_waiting:
        data += ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
        time.sleep(0.05)
    if data.strip():
        # Ausgabe zeilenweise
        for line in data.splitlines():
            if line.strip():
                print(f"[{port_name}] <<< {line}")
    return data


def wait_quiet(ser: serial.Serial, port_name: str,
               quiet_secs: float = 2.0, total_timeout: float = 12.0) -> None:
    """
    Wartet bis der Board keine unaufgeforderten Meldungen mehr sendet.
    Beendet sich wenn quiet_secs lang keine Bytes ankommen.
    Typischer Einsatz: nach ptp_mode master auf Status0/Status1-Reset-Meldungen warten.
    """
    deadline = time.time() + total_timeout
    last_data = time.time()
    printed_waiting = False
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
            if not printed_waiting:
                print(f"[{port_name}] Warte auf Board-Ruhe ({quiet_secs:.0f}s still) ...")
                printed_waiting = True
            time.sleep(0.1)


def ping_test(ser: serial.Serial, port_name: str, target_ip: str) -> bool:
    """Pingt target_ip vom Board aus. Wertet Harmony-Ping-Zusammenfassung aus."""
    cmd = f"ping {target_ip}"
    resp = send_cmd(ser, port_name, cmd, timeout=PING_TIMEOUT)

    # Harmony: "Sent X requests, received Y replies" — Y > 0 ist Erfolg
    import re
    m = re.search(r'received\s+(\d+)\s+repl', resp, re.IGNORECASE)
    if m:
        received = int(m.group(1))
        if received > 0:
            print(f"[{port_name}] PING {target_ip} -> OK ({received} replies)")
            return True
        else:
            print(f"[{port_name}] PING {target_ip} -> FEHLGESCHLAGEN (0 replies)")
            return False

    # Fallback auf generische Marker
    success_markers = ["Reply from", "reply from", "bytes from", "alive"]
    fail_markers    = ["Request timeout", "unreachable", "no route", "host not found"]
    if any(m in resp for m in success_markers):
        print(f"[{port_name}] PING {target_ip} -> OK")
        return True
    if any(m in resp for m in fail_markers):
        print(f"[{port_name}] PING {target_ip} -> FEHLGESCHLAGEN")
        return False
    print(f"[{port_name}] PING {target_ip} -> Antwort unklar: {resp.strip()!r}")
    return False


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------

def main():
    errors = 0

    # --- Ports öffnen ---
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
        # --- Prompt aktivieren ---
        wake_port(ser_follower, FOLLOWER_PORT)
        wake_port(ser_gm,       GRANDMASTER_PORT)

        # ----------------------------------------------------------------
        # Reset beide Boards und warten bis sie wieder booten
        # ----------------------------------------------------------------
        print("\n=== Reset ===")
        ser_follower.write(b"reset\r\n")
        ser_gm.write(b"reset\r\n")
        print("Warte 8 s auf Neustart ...")
        time.sleep(8)
        ser_follower.reset_input_buffer()
        ser_gm.reset_input_buffer()
        wake_port(ser_follower, FOLLOWER_PORT)
        wake_port(ser_gm,       GRANDMASTER_PORT)

        # ----------------------------------------------------------------
        # IP-Konfiguration ZUERST — PTP startet erst danach
        # ----------------------------------------------------------------
        print("\n=== IP-Konfiguration ===")
        ok = set_ip(ser_follower, FOLLOWER_PORT,    FOLLOWER_IP,    NETMASK, INTERFACE)
        if not ok:
            errors += 1
        ok = set_ip(ser_gm,       GRANDMASTER_PORT, GRANDMASTER_IP, NETMASK, INTERFACE)
        if not ok:
            errors += 1

        print("\nWarte 2 s auf IP-Stack-Stabilisierung ...")
        time.sleep(2)

        # ----------------------------------------------------------------
        # PLCA-Modus und PTP starten (erst NACH setip)
        # Follower (node 1) ZUERST, bevor GM (node 0) als Beacon startet.
        # ----------------------------------------------------------------
        print("\n=== PLCA / PTP Modus konfigurieren ===")
        send_cmd(ser_follower, FOLLOWER_PORT,    "ptp_mode follower", timeout=RESPONSE_TIMEOUT)
        time.sleep(0.5)
        send_cmd(ser_gm,       GRANDMASTER_PORT, "ptp_mode master",  timeout=RESPONSE_TIMEOUT)

        print("Warte auf GM-Ruhe nach ptp_mode master ...")
        wait_quiet(ser_gm, GRANDMASTER_PORT, quiet_secs=2.0, total_timeout=12.0)

        print("\nWarte 3 s auf PTP-Stabilisierung ...")
        time.sleep(3)

        # ----------------------------------------------------------------
        # netinfo anzeigen
        # ----------------------------------------------------------------
        print("\n=== netinfo Grandmaster ===")
        send_cmd(ser_gm,       GRANDMASTER_PORT, "netinfo", timeout=RESPONSE_TIMEOUT)
        print("\n=== netinfo Follower ===")
        send_cmd(ser_follower, FOLLOWER_PORT,    "netinfo", timeout=RESPONSE_TIMEOUT)

        # ----------------------------------------------------------------
        # Ping-Tests
        # ----------------------------------------------------------------
        print("\n=== Ping-Tests ===")

        print("\n-- Follower → Grandmaster --")
        ok = ping_test(ser_follower, FOLLOWER_PORT, GRANDMASTER_IP)
        if not ok:
            errors += 1

        print("\n-- Grandmaster → Follower --")
        ok = ping_test(ser_gm, GRANDMASTER_PORT, FOLLOWER_IP)
        if not ok:
            errors += 1

        # ----------------------------------------------------------------
        # netinfo nach den Pings
        # ----------------------------------------------------------------
        print("\n=== netinfo nach Pings (Grandmaster) ===")
        send_cmd(ser_gm,       GRANDMASTER_PORT, "netinfo", timeout=RESPONSE_TIMEOUT)
        print("\n=== netinfo nach Pings (Follower) ===")
        send_cmd(ser_follower, FOLLOWER_PORT,    "netinfo", timeout=RESPONSE_TIMEOUT)

        # ----------------------------------------------------------------
        # Ergebnis
        # ----------------------------------------------------------------
        print("\n=== Ergebnis ===")
        if errors == 0:
            print("ALLE TESTS BESTANDEN — Boards korrekt konfiguriert.")
        else:
            print(f"{errors} FEHLER aufgetreten. Bitte Ausgabe prüfen.")

    finally:
        ser_follower.close()
        ser_gm.close()
        print("\nSerielle Verbindungen geschlossen.")


if __name__ == "__main__":
    main()
