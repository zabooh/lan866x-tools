#!/usr/bin/env python3
# rtp4175_to_video.py - reconstruct a playable video from a captured
# LAN866x RTP/RFC4175 display stream (the inverse of the host `video.c` tool).
#
# The board's video demo sends each 20x10 frame as ONE RTP/RFC4175 packet to
# UDP 5001:  RTP hdr (12B) | extended seq (2B) | 10x SRD hdr (6B) | 600B RGB24.
# Here we pull those payloads out of a pcap with tshark, strip the headers,
# and pipe the raw RGB frames into ffmpeg to scale + encode an MP4/GIF.
#
# Usage:
#   python rtp4175_to_video.py <capture.pcapng> [-o out.mp4] [--fps 15]
#                              [--scale 400x200] [--port 5001]
#
# Needs tshark and ffmpeg on PATH (both ship with this repo's toolchain).

import argparse, shutil, subprocess, sys

X_RES, Y_RES = 20, 10
RTP_HDR  = 12                 # V2 header, no CSRC/ext
ESN      = 2                  # RFC4175 extended sequence number
SRD      = 6 * Y_RES          # one 6-byte SRD header per scanline
PIXELS   = X_RES * Y_RES * 3  # RGB24
PAYLOAD_OFF = RTP_HDR + ESN + SRD


def find(exe):
    p = shutil.which(exe)
    if p:
        return p
    # common install dirs that are often not on PATH (Windows)
    import os
    for d in (r"C:\Program Files\Wireshark", r"C:\Program Files (x86)\Wireshark"):
        cand = os.path.join(d, exe + ".exe")
        if os.path.isfile(cand):
            return cand
    sys.exit(f"error: '{exe}' not found on PATH")


def extract_frames(tshark, pcap, port):
    """Yield 600-byte RGB24 frames in capture order."""
    cmd = [tshark, "-r", pcap, "-Y", f"udp.dstport=={port}",
           "-T", "fields", "-e", "udp.payload"]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    n = 0
    for line in out.splitlines():
        hx = line.strip().replace(":", "")
        if not hx:
            continue
        buf = bytes.fromhex(hx)
        if len(buf) < PAYLOAD_OFF + PIXELS:
            continue
        if (buf[1] & 0x7F) != 96:          # dynamic payload type 96 only
            continue
        yield buf[PAYLOAD_OFF:PAYLOAD_OFF + PIXELS]
        n += 1
    if n == 0:
        sys.exit("error: no RFC4175 frames found - check --port / capture")
    print(f"  extracted {n} frames ({X_RES}x{Y_RES} RGB24)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("-o", "--out", default="video_out.mp4")
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--scale", default="400x200",
                    help="output size WxH (nearest-neighbour upscale); 'none' to keep 20x10")
    ap.add_argument("--port", type=int, default=5001)
    a = ap.parse_args()

    tshark, ffmpeg = find("tshark"), find("ffmpeg")

    vf = []
    if a.scale.lower() != "none":
        w, h = a.scale.lower().split("x")
        vf = ["-vf", f"scale={w}:{h}:flags=neighbor"]

    ff = [ffmpeg, "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
          "-s", f"{X_RES}x{Y_RES}", "-r", str(a.fps), "-i", "-",
          *vf, "-pix_fmt", "yuv420p", a.out]

    print(f"reading {a.pcap} ...")
    proc = subprocess.Popen(ff, stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for frame in extract_frames(tshark, a.pcap, a.port):
        proc.stdin.write(frame)
    proc.stdin.close()
    if proc.wait() != 0:
        sys.exit("error: ffmpeg failed")
    print(f"  wrote {a.out}  ({a.fps} fps, scale {a.scale})")


if __name__ == "__main__":
    main()
