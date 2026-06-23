"""
noip_test.py
------------
Bidirektionaler Layer-2 NoIP Konnektivitaetstest.
Sendet 5 raw-Ethernet-Frames (EtherType 0x88B5) von jedem Board und
prueft ob die Gegenseite sie empfaengt.

Aufruf:  python noip_test.py
"""
import serial
import time
import sys
import datetime

COM_GM       = "COM10"   # Grandmaster  (SN: ATML3264031800001290)
COM_FOLLOWER = "COM8"    # Follower     (SN: ATML3264031800001049)
BAUD         = 115200
N_FRAMES     = 5
GAP_MS       = 5

SEP = "-" * 60


def ts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def open_port(port):
    print(f"[{ts()}] [OPEN] {port} @ {BAUD}", flush=True)
    s = serial.Serial(port, BAUD, timeout=0.3)
    s.reset_input_buffer()
    s.reset_output_buffer()
    return s


def send_cmd(name, s, cmd, wait=1.5):
    """Send a command, wait, collect and PRINT everything that comes back."""
    print(f"[{ts()}] [{name}] >>> {cmd}", flush=True)
    s.reset_input_buffer()
    s.write((cmd + "\r\n").encode())
    deadline = time.time() + wait
    buf = []
    partial = ""
    line_count = 0
    while time.time() < deadline:
        chunk = s.read(max(1, s.in_waiting)).decode(errors="replace")
        if chunk:
            buf.append(chunk)
            partial += chunk
            while "\n" in partial:
                line, partial = partial.split("\n", 1)
                if line.strip():
                    line_count += 1
                    print(f"[{ts()}] [{name}] <<< {line.rstrip()}", flush=True)
        else:
            time.sleep(0.05)
    # drain remaining
    time.sleep(0.1)
    while s.in_waiting:
        chunk = s.read(s.in_waiting).decode(errors="replace")
        buf.append(chunk)
        partial += chunk
        while "\n" in partial:
            line, partial = partial.split("\n", 1)
            if line.strip():
                line_count += 1
                print(f"[{ts()}] [{name}] <<< {line.rstrip()}", flush=True)
    if partial.strip():
        line_count += 1
        print(f"[{ts()}] [{name}] <<< {partial.rstrip()}", flush=True)

    text = "".join(buf)
    if line_count == 0:
        print(f"[{ts()}] [{name}] <<< (keine Ausgabe)", flush=True)
    print(f"[{ts()}] [{name}] capture summary: bytes={len(text)} lines={line_count}", flush=True)
    return text


def drain_verbose(name, s, wait=3.0):
    """Wait <wait> s then drain, print every non-empty line."""
    print(f"[{ts()}] [{name}] capture start ({wait:.1f}s)", flush=True)
    deadline = time.time() + wait
    buf = []
    partial = ""
    line_count = 0
    while time.time() < deadline:
        chunk = s.read(max(1, s.in_waiting)).decode(errors="replace")
        if chunk:
            buf.append(chunk)
            partial += chunk
            while "\n" in partial:
                line, partial = partial.split("\n", 1)
                if line.strip():
                    line_count += 1
                    print(f"[{ts()}] [{name}] {line.rstrip()}", flush=True)
        else:
            time.sleep(0.05)
    time.sleep(0.1)
    while s.in_waiting:
        chunk = s.read(s.in_waiting).decode(errors="replace")
        buf.append(chunk)
        partial += chunk
        while "\n" in partial:
            line, partial = partial.split("\n", 1)
            if line.strip():
                line_count += 1
                print(f"[{ts()}] [{name}] {line.rstrip()}", flush=True)

    if partial.strip():
        line_count += 1
        print(f"[{ts()}] [{name}] {partial.rstrip()}", flush=True)

    text = "".join(buf)
    if line_count == 0:
        print(f"[{ts()}] [{name}] (keine Ausgabe)", flush=True)
    print(f"[{ts()}] [{name}] capture summary: bytes={len(text)} lines={line_count}", flush=True)
    return text


def capture_both(ser_a_name, ser_a, ser_b_name, ser_b, wait=4.0):
    """Capture both serial ports concurrently so RX-side logs are not missed."""
    print(f"[{ts()}] [CAPTURE] start both ports for {wait:.1f}s", flush=True)
    deadline = time.time() + wait
    buf_a = []
    buf_b = []
    partial_a = ""
    partial_b = ""
    lines_a = 0
    lines_b = 0

    while time.time() < deadline:
        n_a = ser_a.in_waiting
        if n_a > 0:
            chunk_a = ser_a.read(n_a).decode(errors="replace")
            if chunk_a:
                buf_a.append(chunk_a)
                partial_a += chunk_a
                while "\n" in partial_a:
                    l, partial_a = partial_a.split("\n", 1)
                    if l.strip():
                        lines_a += 1
                        print(f"[{ts()}] [{ser_a_name}] {l.rstrip()}", flush=True)

        n_b = ser_b.in_waiting
        if n_b > 0:
            chunk_b = ser_b.read(n_b).decode(errors="replace")
            if chunk_b:
                buf_b.append(chunk_b)
                partial_b += chunk_b
                while "\n" in partial_b:
                    l, partial_b = partial_b.split("\n", 1)
                    if l.strip():
                        lines_b += 1
                        print(f"[{ts()}] [{ser_b_name}] {l.rstrip()}", flush=True)

        if n_a == 0 and n_b == 0:
            time.sleep(0.02)

    time.sleep(0.08)
    while ser_a.in_waiting:
        chunk_a = ser_a.read(ser_a.in_waiting).decode(errors="replace")
        if chunk_a:
            buf_a.append(chunk_a)
            partial_a += chunk_a
            while "\n" in partial_a:
                l, partial_a = partial_a.split("\n", 1)
                if l.strip():
                    lines_a += 1
                    print(f"[{ts()}] [{ser_a_name}] {l.rstrip()}", flush=True)

    while ser_b.in_waiting:
        chunk_b = ser_b.read(ser_b.in_waiting).decode(errors="replace")
        if chunk_b:
            buf_b.append(chunk_b)
            partial_b += chunk_b
            while "\n" in partial_b:
                l, partial_b = partial_b.split("\n", 1)
                if l.strip():
                    lines_b += 1
                    print(f"[{ts()}] [{ser_b_name}] {l.rstrip()}", flush=True)

    if partial_a.strip():
        lines_a += 1
        print(f"[{ts()}] [{ser_a_name}] {partial_a.rstrip()}", flush=True)
    if partial_b.strip():
        lines_b += 1
        print(f"[{ts()}] [{ser_b_name}] {partial_b.rstrip()}", flush=True)

    text_a = "".join(buf_a)
    text_b = "".join(buf_b)
    print(f"[{ts()}] [{ser_a_name}] capture summary: bytes={len(text_a)} lines={lines_a}", flush=True)
    print(f"[{ts()}] [{ser_b_name}] capture summary: bytes={len(text_b)} lines={lines_b}", flush=True)
    return text_a, text_b


def count_lines(text, keyword):
    return sum(1 for l in text.splitlines() if keyword in l)


def main():
    print("=" * 60, flush=True)
    print("  NoIP Layer-2 Konnektivitaetstest (verbose)", flush=True)
    print("=" * 60, flush=True)
    print(f"  COM_GM={COM_GM}  COM_FOLLOWER={COM_FOLLOWER}  N={N_FRAMES}  GAP_MS={GAP_MS}", flush=True)
    print(f"  Startzeit: {datetime.datetime.now().isoformat(timespec='seconds')}", flush=True)
    print()

    gm  = open_port(COM_GM)
    fol = open_port(COM_FOLLOWER)

    print(SEP, flush=True)
    print("[Init] CLI reset auf beiden Boards, dann 8s warten ...", flush=True)
    print(f"[{ts()}] [GM] >>> reset", flush=True)
    gm.write(b"reset\r\n")
    print(f"[{ts()}] [FOL] >>> reset", flush=True)
    fol.write(b"reset\r\n")
    time.sleep(8.0)
    print(f"[{ts()}] 8s vorbei, lese Boot-Ausgaben ...", flush=True)
    drain_verbose("GM", gm, wait=1.0)
    drain_verbose("FOL", fol, wait=1.0)

    # --- Prompt ---
    print(SEP, flush=True)
    print("[Init] Prompt abwarten (1 s) ...", flush=True)
    gm.write(b"\r\n") ; fol.write(b"\r\n")
    time.sleep(1)
    gm.reset_input_buffer() ; fol.reset_input_buffer()

    # --- IP-Konfiguration ---
    print(SEP, flush=True)
    print("[Init] IP-Konfiguration ...", flush=True)
    send_cmd("GM",  gm,  "setip eth0 192.168.0.30 255.255.255.0", wait=1.5)
    send_cmd("FOL", fol, "setip eth0 192.168.0.20 255.255.255.0", wait=1.5)
    time.sleep(2.0)
    print()

    # --- ipdump 1 auf FOL aktivieren (sieht ALLE eingehenden eth0-Pakete) ---
    print(SEP, flush=True)
    print("[Init] ipdump 1 auf FOLLOWER aktivieren (zeigt alle eth0-RX-Pakete) ...", flush=True)
    send_cmd("FOL", fol, "ipdump 1", wait=1.0)
    print("[Init] ipdump 1 auf GM aktivieren ...", flush=True)
    send_cmd("GM",  gm,  "ipdump 1", wait=1.0)
    print()

    print(SEP, flush=True)
    print("[Init] Baseline noip_stat vor dem Test", flush=True)
    gm_stat_before = send_cmd("GM", gm, "noip_stat", wait=0.8)
    fol_stat_before = send_cmd("FOL", fol, "noip_stat", wait=0.8)

    # =========================================================================
    print(SEP, flush=True)
    print(f"[Schritt 1] GM -> Follower: {N_FRAMES} NoIP-Frames senden", flush=True)
    print(SEP, flush=True)
    fol.reset_input_buffer()
    gm.reset_input_buffer()

    print(f"[{ts()}] [GM] >>> noip_send {N_FRAMES} {GAP_MS}", flush=True)
    gm.write(f"noip_send {N_FRAMES} {GAP_MS}\r\n".encode())

    print(f"[{ts()}] Warte 4 s auf TX/RX + ipdump (beide Ports parallel) ...", flush=True)
    gm_out, fol_out = capture_both("GM", gm, "FOL", fol, wait=4.5)

    tx1 = count_lines(gm_out,  "[NoIP-TX]")
    rx1 = count_lines(fol_out, "[NoIP-RX]")
    print()
    print(f"[{ts()}] GM  TX : {tx1}/{N_FRAMES}", flush=True)
    print(f"[{ts()}] FOL RX : {rx1}/{N_FRAMES}  (NoIP-RX Zeilen)", flush=True)
    result1 = "[PASS]" if rx1 == N_FRAMES else "[FAIL]"
    print(f"[{ts()}] Ergebnis: {result1}", flush=True)

    # =========================================================================
    print()
    print(SEP, flush=True)
    print(f"[Schritt 2] Follower -> GM: {N_FRAMES} NoIP-Frames senden", flush=True)
    print(SEP, flush=True)
    gm.reset_input_buffer()
    fol.reset_input_buffer()

    print(f"[{ts()}] [FOL] >>> noip_send {N_FRAMES} {GAP_MS}", flush=True)
    fol.write(f"noip_send {N_FRAMES} {GAP_MS}\r\n".encode())

    print(f"[{ts()}] Warte 4 s auf TX/RX + ipdump (beide Ports parallel) ...", flush=True)
    fol_out2, gm_out2 = capture_both("FOL", fol, "GM", gm, wait=4.5)

    tx2 = count_lines(fol_out2, "[NoIP-TX]")
    rx2 = count_lines(gm_out2,  "[NoIP-RX]")
    print()
    print(f"[{ts()}] FOL TX : {tx2}/{N_FRAMES}", flush=True)
    print(f"[{ts()}] GM  RX : {rx2}/{N_FRAMES}  (NoIP-RX Zeilen)", flush=True)
    result2 = "[PASS]" if rx2 == N_FRAMES else "[FAIL]"
    print(f"[{ts()}] Ergebnis: {result2}", flush=True)

    # =========================================================================
    # ipdump wieder aus
    print()
    print(SEP, flush=True)
    print("[Cleanup] ipdump ausschalten ...", flush=True)
    send_cmd("GM",  gm,  "ipdump 0", wait=0.5)
    send_cmd("FOL", fol, "ipdump 0", wait=0.5)

    # =========================================================================
    print()
    print(SEP, flush=True)
    print("[Statistiken]", flush=True)
    gm_stat_after = send_cmd("GM",  gm,  "noip_stat", wait=0.8)
    fol_stat_after = send_cmd("FOL", fol, "noip_stat", wait=0.8)
    send_cmd("GM",  gm,  "stats",     wait=0.8)
    send_cmd("FOL", fol, "stats",     wait=0.8)

    print(SEP, flush=True)
    print("[Delta noip_stat]", flush=True)
    print(f"GM  before: {gm_stat_before.strip() if gm_stat_before.strip() else '(leer)'}", flush=True)
    print(f"GM  after : {gm_stat_after.strip() if gm_stat_after.strip() else '(leer)'}", flush=True)
    print(f"FOL before: {fol_stat_before.strip() if fol_stat_before.strip() else '(leer)'}", flush=True)
    print(f"FOL after : {fol_stat_after.strip() if fol_stat_after.strip() else '(leer)'}", flush=True)

    # =========================================================================
    print()
    print("=" * 60, flush=True)
    print("GESAMTERGEBNIS", flush=True)
    print("=" * 60, flush=True)
    if rx1 == N_FRAMES and rx2 == N_FRAMES:
        print(f"[PASS] Bidirektionale NoIP-Kommunikation funktioniert!", flush=True)
        rc = 0
    else:
        print(f"[FAIL] GM->FOL: {rx1}/{N_FRAMES}  FOL->GM: {rx2}/{N_FRAMES}", flush=True)
        rc = 1

    gm.close()
    fol.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
