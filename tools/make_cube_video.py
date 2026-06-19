#!/usr/bin/env python3
"""
make_cube_video.py - generate a seamless‑looping "rotating cube that zooms in
and out" clip for lan866x-video.

The lan866x-video tool runs ffmpeg to decode + scale any video to the board's
20x10 display (left half = display 1, right half = display 2) and loops it. This
script renders such a clip: a solid, shaded, tumbling cube whose perspective zoom
pulses from far/small to near/big and back. The loop is seamless — the cube's
orientation and zoom return exactly to the start, and the last rendered frame is
the one *before* t=T (== t=0), so ffmpeg's loop has no stutter.

It renders at a 2:1 aspect (matching the 20x10 display) so nothing is distorted
when the tool downscales it.

  python tools/make_cube_video.py                       # -> media/cube.mp4
  python tools/make_cube_video.py --out media/cube.mp4 --seconds 8 --fps 25
  python tools/make_cube_video.py --preview             # also write a preview strip

Then send it to the board:
  release\\lan866x-video.exe media\\cube.mp4 --ip 192.168.0.54

Needs: numpy, Pillow, ffmpeg (on PATH or --ffmpeg).
"""
import argparse
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

# unit cube centred at the origin
VERTS = np.array([
    [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
    [-1, -1,  1], [1, -1,  1], [1, 1,  1], [-1, 1,  1],
], dtype=float)

# each face: 4 vertex indices + base RGB colour + outward normal
FACES = [
    ([4, 5, 6, 7], (255,  40,  40), (0, 0,  1)),   # front  - red
    ([0, 1, 2, 3], (40, 255,  40), (0, 0, -1)),    # back   - green
    ([1, 5, 6, 2], (40,  80, 255), (1, 0,  0)),    # right  - blue
    ([0, 3, 7, 4], (255, 210,  30), (-1, 0, 0)),   # left   - yellow
    ([3, 2, 6, 7], (0, 230, 230), (0, 1,  0)),     # top    - cyan
    ([0, 4, 5, 1], (230,  40, 230), (0, -1, 0)),   # bottom - magenta
]


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def render_frame(t, W, H, turns_x, turns_y, zoom_cycles, base_f, cam_d):
    """t in [0,1). Returns a PIL RGB image of the cube at that phase."""
    ax = 2 * math.pi * turns_x * t
    ay = 2 * math.pi * turns_y * t
    R = rot_y(ay) @ rot_x(ax)

    # zoom: starts/ends at the smallest size (clean loop point), pulses bigger.
    # min stays large enough to still read as a cube after the 16x downscale to 20x10.
    zmid, zamp = 1.45, 0.62
    zoom = zmid - zamp * math.cos(2 * math.pi * zoom_cycles * t)

    v = VERTS @ R.T                      # rotate all vertices
    n = np.array([f[2] for f in FACES], dtype=float) @ R.T   # rotate normals

    cx, cy = W / 2.0, H / 2.0

    def project(p):
        s = base_f * zoom / (cam_d - p[2])
        return (cx + s * p[0], cy - s * p[1])

    light = np.array([0.4, 0.6, 1.0])
    light /= np.linalg.norm(light)

    # visible faces (normal points toward the camera at +z), far ones first
    drawn = []
    for i, (idx, col, _) in enumerate(FACES):
        if n[i][2] <= 0.02:              # back-face cull
            continue
        zavg = float(np.mean([v[j][2] for j in idx]))
        inten = max(0.22, float(np.dot(n[i], light)))
        shade = tuple(int(c * inten) for c in col)
        poly = [project(v[j]) for j in idx]
        drawn.append((zavg, poly, shade))
    drawn.sort(key=lambda d: d[0])       # painter's algorithm

    img = Image.new("RGB", (W, H), (0, 0, 0))
    dr = ImageDraw.Draw(img)
    for _, poly, shade in drawn:
        edge = tuple(min(255, int(c * 0.5) + 30) for c in shade)
        dr.polygon(poly, fill=shade, outline=edge)
    return img


def main():
    ap = argparse.ArgumentParser(description="Generate a rotating+zooming cube clip.")
    ap.add_argument("--out", default="media/cube.mp4")
    ap.add_argument("--w", type=int, default=320, help="render width (2:1 of height)")
    ap.add_argument("--h", type=int, default=160)
    ap.add_argument("--fps", type=int, default=25)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--turns-x", type=float, default=2.0, help="cube X turns per loop")
    ap.add_argument("--turns-y", type=float, default=3.0, help="cube Y turns per loop")
    ap.add_argument("--zoom-cycles", type=float, default=2.0, help="in/out pulses per loop")
    ap.add_argument("--base-f", type=float, default=104.0, help="projection scale")
    ap.add_argument("--cam-d", type=float, default=4.0, help="camera distance")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--preview", action="store_true", help="also write <out>.preview.png")
    args = ap.parse_args()

    nframes = max(1, int(round(args.seconds * args.fps)))
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    # frames for t in [0,1): the frame at t=1 equals t=0, so we omit it -> seamless loop
    frames = [render_frame(i / nframes, args.w, args.h, args.turns_x, args.turns_y,
                           args.zoom_cycles, args.base_f, args.cam_d)
              for i in range(nframes)]

    if args.preview:
        cols = 8
        sel = [frames[int(k * nframes / cols)] for k in range(cols)]
        strip = Image.new("RGB", (args.w * cols, args.h), (20, 20, 20))
        for k, im in enumerate(sel):
            strip.paste(im, (k * args.w, 0))
        pv = args.out + ".preview.png"
        strip.save(pv)
        print(f"wrote {pv}")

    cmd = [args.ffmpeg, "-y", "-f", "rawvideo", "-pixel_format", "rgb24",
           "-video_size", f"{args.w}x{args.h}", "-framerate", str(args.fps),
           "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p",
           "-movflags", "+faststart", args.out]
    try:
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except FileNotFoundError:
        sys.exit("ffmpeg not found - install it or pass --ffmpeg <path>")
    for im in frames:
        p.stdin.write(np.asarray(im, dtype=np.uint8).tobytes())
    p.stdin.close()
    err = p.stderr.read().decode(errors="ignore")
    if p.wait() != 0:
        sys.exit("ffmpeg failed:\n" + err[-1500:])

    sz = os.path.getsize(args.out)
    print(f"wrote {args.out}  ({nframes} frames, {args.seconds:.0f}s @ {args.fps} fps, "
          f"{sz/1024:.0f} kB, seamless loop)")


if __name__ == "__main__":
    main()
