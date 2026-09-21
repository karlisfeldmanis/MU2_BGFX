#!/usr/bin/env python3
"""Shadow fizz under a moving camera, measured at fixed spots on the ground.

tools/shimmer.py needs the camera held, because it compares pixel with pixel. A camera that
moves puts a different piece of ground under every pixel, so this compares GROUND with
ground: the run writes, a row a frame, where each point of a fixed grid on the land lands on
screen (`--shadow-points`), and this samples each shot at those spots. A shadow that belongs
to the world keeps its value at a spot however the camera moves; one that fizzes does not.

    ./run.sh --world lorencia --no-figures --shadow-view --fixed-dt 16.6667 --frames 120 \\
             --shot 1 --shot-path /abs/pan --shadow-points /abs/pan.csv [--shadow-noise ...]
    tools/pan.py /abs/pan /abs/pan.csv

Run it with --shadow-view, so the picture is the sun's visibility alone and a change is the
shadow's. Without --still the world camera glides across the town, which is the pan.

The numbers are for comparing runs taken the same way. A buried point (behind a wall, under
the fountain) moves as the view changes whatever the shadow does, and bilinear resampling
has a floor of its own, so `--shadow-noise none` is the floor to read the others against.
docs/shadow-probe.md.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def bilinear(img, x, y):
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    a = img[y0, x0]
    b = img[y0, x0 + 1]
    c = img[y0 + 1, x0]
    d = img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shots")
    ap.add_argument("points")
    ap.add_argument("--skip", type=int, default=2, help="frames to drop while the first settle")
    ap.add_argument("--jump", type=float, default=8.0,
                    help="a change at a spot bigger than this, of 255, counts as a flicker")
    args = ap.parse_args()

    rows = {}
    with open(args.points) as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip().split(",")
            rows[int(parts[0])] = np.array(parts[1:], dtype=np.float32).reshape(-1, 2)

    frames = sorted(n for n in rows if os.path.exists(os.path.join(args.shots, f"{n:05d}.png")))
    frames = frames[args.skip:]
    if len(frames) < 3:
        print("need three frames or more with both a shot and a row")
        return 1

    values = []
    valid = []
    for n in frames:
        img = np.asarray(Image.open(os.path.join(args.shots, f"{n:05d}.png")).convert("L"),
                         dtype=np.float32)
        h, w = img.shape
        xy = rows[n]
        ok = (xy[:, 0] >= 1) & (xy[:, 1] >= 1) & (xy[:, 0] < w - 2) & (xy[:, 1] < h - 2)
        v = np.zeros(len(xy), dtype=np.float32)
        v[ok] = bilinear(img, xy[ok, 0], xy[ok, 1])
        values.append(v)
        valid.append(ok)
    values = np.array(values)
    valid = np.array(valid)

    both = valid[1:] & valid[:-1]
    delta = np.abs(values[1:] - values[:-1])[both]
    lo_v = np.minimum(values[1:], values[:-1])[both]
    hi_v = np.maximum(values[1:], values[:-1])[both]
    seen = values[valid]
    lit, dark = np.percentile(seen, 99), np.percentile(seen, 1)
    # A spot in the penumbra in both frames: not lit outright and not in full shadow.
    soft = (lo_v > dark + 0.08 * (lit - dark)) & (hi_v < lit - 0.08 * (lit - dark))

    print(f"{len(frames)} frames, {values.shape[1]} ground points, {int(both.sum())} "
          f"spot-pairs seen twice, lit {lit:.0f} dark {dark:.0f}")
    print(f"all spots:       mean change {delta.mean():6.3f}   flickers {int((delta > args.jump).sum()):6d}"
          f" ({100.0 * (delta > args.jump).mean():.3f}%)")
    if soft.any():
        print(f"penumbra spots:  mean change {delta[soft].mean():6.3f}   flickers "
              f"{int((delta[soft] > args.jump).sum()):6d} ({100.0 * (delta[soft] > args.jump).mean():.3f}%)"
              f"   of {int(soft.sum())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
