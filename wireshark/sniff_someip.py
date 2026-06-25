#!/usr/bin/env python3
"""
sniff_someip.py - live SOME/IP (LAN866x RCP) sniffer using pyshark.

Reuses Wireshark's dissectors (via tshark), so it decodes the same way Wireshark
does - provided the SOME/IP config in ./SOMEIP/ is installed (run SOMEIP/install.bat
or install.sh first). Method/event IDs are additionally named from
SOMEIP_method_event_identifiers so you see "GetStatus", "SetGpio", ... live.

Examples:
  python sniff_someip.py --iface "Ethernet 8"
  python sniff_someip.py --iface "Ethernet 8" --ip 192.168.0.54 --count 50
  python sniff_someip.py --iface 3            # tshark interface number (tshark -D)

Capture needs Npcap + admin rights on Windows. The method endpoint (UDP 6800) and
SD (30490) are forced to SOME/IP via Decode-As so it works even if the SOME/IP
port preference isn't set.
"""
import argparse
import os

HERE = os.path.dirname(os.path.abspath(__file__))

MSGTYPE = {0x00: "REQUEST", 0x01: "REQUEST_NO_RETURN", 0x02: "NOTIFICATION",
           0x80: "RESPONSE", 0x81: "ERROR"}


def load_method_names():
    """Parse SOMEIP/SOMEIP_method_event_identifiers -> {methodid:int -> name}."""
    names = {}
    path = os.path.join(HERE, "SOMEIP", "SOMEIP_method_event_identifiers")
    try:
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip().strip('"') for p in line.split(",")]
            if len(parts) >= 3:                 # "ff10","1002","GetStatus"
                names[int(parts[1], 16)] = parts[2]
    except OSError:
        pass
    return names


def main():
    ap = argparse.ArgumentParser(description="Live SOME/IP sniffer (pyshark)")
    ap.add_argument("--iface", required=True, help='capture interface name ("Ethernet 8") or tshark -D number')
    ap.add_argument("--ip", default=None, help="only show traffic to/from this endpoint IP")
    ap.add_argument("--count", type=int, default=0, help="stop after N SOME/IP packets (0 = forever)")
    args = ap.parse_args()

    import pyshark  # imported here so --help works without it

    names = load_method_names()
    bpf = "udp"
    if args.ip:
        bpf = f"host {args.ip} or udp port 30490"

    cap = pyshark.LiveCapture(
        interface=args.iface,
        bpf_filter=bpf,
        display_filter="someip",
        decode_as={"udp.port==6800": "someip", "udp.port==49153": "someip",
                   "udp.port==30490": "someip"},
    )
    print(f"Sniffing SOME/IP on '{args.iface}' (filter: {bpf}). Ctrl+C to stop.\n")
    n = 0
    try:
        for pkt in cap.sniff_continuously():
            try:
                si = pkt.someip
                mid = int(si.methodid, 16)
                mt = int(si.messagetype, 16)
                name = names.get(mid, f"0x{mid:04x}")
                src = pkt.ip.src if hasattr(pkt, "ip") else "?"
                dst = pkt.ip.dst if hasattr(pkt, "ip") else "?"
                print(f"{float(pkt.sniff_timestamp):.6f}  {src:>15} -> {dst:<15}  "
                      f"svc=0x{int(si.serviceid,16):04x} {name:<22} "
                      f"{MSGTYPE.get(mt, hex(mt)):<18} sid=0x{int(si.sessionid,16):04x}")
            except AttributeError:
                continue
            n += 1
            if args.count and n >= args.count:
                break
    except KeyboardInterrupt:
        pass
    finally:
        print(f"\n{n} SOME/IP packet(s).")


if __name__ == "__main__":
    main()
