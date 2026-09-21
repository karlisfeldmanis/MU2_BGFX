#!/usr/bin/env python3
"""Shadow shimmer, counted: how many pixels change between consecutive shots.

Reads a directory of shots from a run in which the ONLY thing allowed to move is the sun's
split -- `--world lorencia --still --no-figures --shadow-slide MM --shot 1` -- so any pixel
that differs between two frames is the shadow's fault. A split snapped to its texels draws
the same picture every frame, and the answer is zero.

    tools/shimmer.py /abs/shots/dir [--threshold 2] [--heat /abs/heat.png] [--allow 0]

Prints a row per pair and a summary; writes a heat map where every pixel that ever changed
is lit by how many pairs it changed in, over the first shot darkened, so the crawl can be
seen on the edges it crawls along. Exit 1 when more than --allow pixels changed in any pair.
docs/shadow-probe.md.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shots")
    ap.add_argument("--threshold", type=int, default=2,
                    help="a channel must move more than this many of 255 to count")
    ap.add_argument("--heat", help="absolute path for the heat map png")
    ap.add_argument("--allow", type=int, default=0,
                    help="the most changed pixels a pair may have and still pass")
    ap.add_argument("--skip", type=int, default=2,
                    help="shots to drop at the start, while the first frames settle")
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.shots) if n.endswith(".png"))[args.skip:]
    if len(names) < 2:
        print(f"need two shots or more in {args.shots}, found {len(names)}")
        return 1

    first = load(os.path.join(args.shots, names[0]))
    heat = np.zeros(first.shape[:2], dtype=np.int32)
    prev = first
    worst = 0
    changed_pairs = 0
    counts = []
    print(f"{'pair':>14} {'changed':>9} {'max':>4}")
    for a, b in zip(names, names[1:]):
        cur = load(os.path.join(args.shots, b))
        delta = np.abs(cur - prev).max(axis=2)
        moved = delta > args.threshold
        n = int(moved.sum())
        heat += moved
        counts.append(n)
        worst = max(worst, n)
        changed_pairs += n > args.allow
        print(f"{a[:-4]}->{b[:-4]} {n:9d} {int(delta.max()):4d}")
        prev = cur

    total = first.shape[0] * first.shape[1]
    print(f"\n{len(counts)} pairs, {changed_pairs} over the allowance of {args.allow}; "
          f"worst {worst} px ({100.0 * worst / total:.3f}% of the frame), "
          f"median {int(np.median(counts))}, {int((heat > 0).sum())} px ever changed")

    if args.heat:
        base = (first.astype(np.float32) * 0.35).astype(np.uint8)
        lit = heat > 0
        level = np.clip(heat.astype(np.float32) / max(1, heat.max()), 0.0, 1.0)
        base[lit] = np.stack([255 * np.ones_like(level[lit]), 255 * (1.0 - level[lit]),
                              np.zeros_like(level[lit])], axis=1).astype(np.uint8)
        Image.fromarray(base).save(args.heat)
        print(f"heat map: {args.heat}")

    return 1 if changed_pairs else 0


if __name__ == "__main__":
    sys.exit(main())
