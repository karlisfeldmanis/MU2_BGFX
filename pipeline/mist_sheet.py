#!/usr/bin/env python3
"""The character screen's mist puff: a soft cloud of smoke on a 512x512 sheet, generated.

MU's own smoke sheets (effects/fire/smoke01.png) are 64 pixels across, which is right for a
chimney's puff and blocky drawn several metres wide behind the pedestals -- the user,
2026-09-27: "the fog is pixelated, not like real smoke fog". So this one is made rather than cut:
several octaves of smooth value noise, shaped by a soft round falloff so the puff has no edge,
white in the colour and the cloud in the alpha. Ours, not MU's.

    python3 pipeline/mist_sheet.py [out.png]      default workshop/effects/lobby/mist.png
"""
import os
import sys

import numpy as np
from PIL import Image

SIZE = 512
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def smooth_noise(size, cells, rng):
    """Value noise: a random lattice `cells` wide, smoothstepped between its points, tiling."""
    lattice = rng.random((cells + 1, cells + 1))
    lattice[-1, :] = lattice[0, :]
    lattice[:, -1] = lattice[:, 0]
    t = np.linspace(0.0, cells, size, endpoint=False)
    i = t.astype(int)
    f = t - i
    f = f * f * (3.0 - 2.0 * f)
    y0, x0 = np.meshgrid(i, i, indexing="ij")
    fy, fx = np.meshgrid(f, f, indexing="ij")
    a = lattice[y0, x0]
    b = lattice[y0, x0 + 1]
    c = lattice[y0 + 1, x0]
    d = lattice[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "workshop/effects/lobby/mist.png")
    rng = np.random.default_rng(74)  # world 74's
    cloud = np.zeros((SIZE, SIZE))
    weight = 0.0
    for octave, cells in enumerate((4, 8, 16, 32, 64)):
        amplitude = 0.55 ** octave
        cloud += smooth_noise(SIZE, cells, rng) * amplitude
        weight += amplitude
    cloud /= weight
    # Contrast in the wisps, so it reads as smoke and not as a blur.
    cloud = np.clip((cloud - 0.30) / 0.50, 0.0, 1.0) ** 1.4
    # A round falloff with no edge: full in the middle, nothing by the rim.
    yy, xx = np.mgrid[0:SIZE, 0:SIZE]
    r = np.hypot(xx - SIZE / 2 + 0.5, yy - SIZE / 2 + 0.5) / (SIZE / 2)
    falloff = np.clip(1.0 - r, 0.0, 1.0) ** 1.6
    alpha = np.clip(cloud * falloff * 1.35, 0.0, 1.0)
    rgba = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = (alpha * 255.0 + 0.5).astype(np.uint8)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(out)
    print(f"mist_sheet: {out}, {SIZE}x{SIZE}, alpha peak {alpha.max():.2f}")


if __name__ == "__main__":
    main()
