#!/usr/bin/env python3
"""A shadow setting against a much finer one, on the same frames: how wrong, and how much
the wrongness flickers.

A figure's own shadow changes every frame because the figure moves, so counting changed
pixels says nothing about aliasing. Two runs with the same `--fixed-dt` and the same seed
draw the same frames, though, so the difference between a setting and a reference taken at
a far finer map is the aliasing alone, and how much THAT changes from one frame to the next
is the flicker. Both runs with --shadow-view, so the picture is the sun's visibility only.

    tools/shadowref.py /abs/candidate /abs/reference [--box 0.4,0.5]

`--box` is the middle of the frame, as fractions of its width and height, which is where
MU's camera keeps the character and his shadow. docs/shadow-probe.md.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("candidate")
    ap.add_argument("reference")
    ap.add_argument("--box", default="0.4,0.5")
    ap.add_argument("--jump", type=float, default=16.0,
                    help="a change in the error bigger than this, of 255, is a flicker")
    ap.add_argument("--skip", type=int, default=2)
    args = ap.parse_args()
    bw, bh = (float(v) for v in args.box.split(","))

    names = sorted(set(n for n in os.listdir(args.candidate) if n.endswith(".png")) &
                   set(n for n in os.listdir(args.reference) if n.endswith(".png")))[args.skip:]
    if len(names) < 3:
        print("need three shots or more present in both directories")
        return 1

    def load(d, n):
        return np.asarray(Image.open(os.path.join(d, n)).convert("L"), dtype=np.float32)

    prev = None
    err_all, err_box, flick_all, flick_box, jumps_box = [], [], [], [], 0
    box_px = 0
    for n in names:
        err = load(args.candidate, n) - load(args.reference, n)
        h, w = err.shape
        y0, y1 = int(h * (0.5 - bh / 2)), int(h * (0.5 + bh / 2))
        x0, x1 = int(w * (0.5 - bw / 2)), int(w * (0.5 + bw / 2))
        err_all.append(np.abs(err).mean())
        err_box.append(np.abs(err[y0:y1, x0:x1]).mean())
        if prev is not None:
            d = np.abs(err - prev)
            flick_all.append(d.mean())
            db = d[y0:y1, x0:x1]
            flick_box.append(db.mean())
            jumps_box += int((db > args.jump).sum())
            box_px += db.size
        prev = err

    print(f"{len(names)} frames")
    print(f"error    mean |candidate - reference|   frame {np.mean(err_all):6.3f}   box {np.mean(err_box):6.3f}")
    print(f"flicker  mean |change in that error|    frame {np.mean(flick_all):6.3f}   box {np.mean(flick_box):6.3f}"
          f"   box pixels over {args.jump:.0f}: {100.0 * jumps_box / max(1, box_px):.3f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
