#!/usr/bin/env python3
"""The void's clouds: four big cloud masses seen from above, a 2x2 atlas on one 1024 sheet.

The void clouds were MU's smoke02, a 64-pixel puff, laid by the hundred -- the user,
2026-10-05: 'clouds dont have to looks like puffs but like actual big clouds'. So these are
made rather than cut, as the lobby's mist is (pipeline/mist_sheet.py): a lumpy outline of a few
overlapping blobs, eaten into by warped noise so the edge is ragged and torn, and lit from one
side by the density's own slope, so the tops billow and the hollows sit in shade. Grey in the
colour, the cloud in the alpha, nothing at a cell's border. Ours, not MU's.

    python3 pipeline/cloud_sheet.py [out.png]    default source/effects/clouds/void_clouds.png
"""
import os
import sys

import numpy as np
from PIL import Image

CELL = 512
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def smooth_noise(size, cells, rng):
    """Value noise: a random lattice `cells` wide, smoothstepped between its points."""
    lattice = rng.random((cells + 2, cells + 2))
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


def fbm(size, rng, octaves=(3, 6, 12, 24, 48, 96), gain=0.5):
    total = np.zeros((size, size))
    weight = 0.0
    for k, cells in enumerate(octaves):
        amplitude = gain ** k
        total += smooth_noise(size, cells, rng) * amplitude
        weight += amplitude
    return total / weight


def sample(field, x, y):
    """Bilinear lookup of `field` at pixel coordinates, clamped."""
    size = field.shape[0]
    x = np.clip(x, 0, size - 1.001)
    y = np.clip(y, 0, size - 1.001)
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    a = field[y0, x0]
    b = field[y0, x0 + 1]
    c = field[y0 + 1, x0]
    d = field[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def cloud(rng):
    size = CELL
    yy, xx = np.mgrid[0:size, 0:size].astype(float)
    u, v = xx / size - 0.5, yy / size - 0.5

    # The body: a few broad masses along one long axis, summed (a soft union, no seams), and
    # on them dozens of small billows, the cauliflower a cloud's top is seen as from above.
    stretch = rng.uniform(1.1, 1.7)
    angle = rng.uniform(0.0, np.pi)
    ca, sa = np.cos(angle), np.sin(angle)
    ru, rv = u * ca + v * sa, -u * sa + v * ca
    mass = np.zeros((size, size))
    for _ in range(rng.integers(4, 7)):
        cx, cy = rng.normal(0.0, 0.10) * stretch, rng.normal(0.0, 0.05)
        r = rng.uniform(0.09, 0.15)
        mass += np.exp(-((ru - cx) ** 2 + (rv - cy) ** 2) / (2 * r * r))
    billows = np.zeros((size, size))
    for _ in range(70):
        cx, cy = rng.normal(0.0, 0.12) * stretch, rng.normal(0.0, 0.07)
        r = rng.uniform(0.025, 0.06)
        billows += np.exp(-((ru - cx) ** 2 + (rv - cy) ** 2) / (2 * r * r)) * rng.uniform(0.5, 1.0)

    # Warped noise eats the edge into torn wisps; inside, it only roughens.
    warp_x, warp_y = fbm(size, rng), fbm(size, rng)
    detail = fbm(size, rng, octaves=(4, 8, 16, 32, 64, 128), gain=0.55)
    detail = sample(detail, xx + (warp_x - 0.5) * 160, yy + (warp_y - 0.5) * 160)

    # Thickness, and the alpha follows it: thin at the edge, never a flat opaque plate inside.
    thick = mass * 0.55 + billows * 0.22 + (detail - 0.5) * 1.1
    t = np.clip((thick - 0.25) / 1.1, 0.0, 1.0)
    alpha = t * t * (3.0 - 2.0 * t) * 0.92
    # Nothing at the cell's border, whatever the shapes did.
    r = np.hypot(u, v) / 0.5
    alpha *= np.clip((1.0 - r) / 0.2, 0.0, 1.0)

    # Lit from the upper left off the billows' own surface normal: their sunward faces bright,
    # the far sides and the hollows between them in shade; the thin edge a touch lighter.
    height = billows * 0.6 + mass * 0.4 + detail * 0.5
    gy, gx = np.gradient(height * size * 0.02)
    n = np.stack([-gx, -gy, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=0)
    light = np.array([-0.55, -0.55, 0.63])
    lit = np.clip(np.tensordot(light / np.linalg.norm(light), n, axes=1), 0.0, 1.0)
    shade = 0.42 + 0.55 * lit + 0.08 * (1.0 - alpha)
    return np.clip(shade, 0.0, 1.0), np.clip(alpha, 0.0, 1.0)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "source/effects/clouds/void_clouds.png")
    rng = np.random.default_rng(11)  # Blood Castle's map number
    rgba = np.zeros((CELL * 2, CELL * 2, 4), dtype=np.uint8)
    for k in range(4):
        shade, alpha = cloud(rng)
        row, col = divmod(k, 2)
        tile = rgba[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL]
        grey = (shade * 255.0 + 0.5).astype(np.uint8)
        tile[..., 0] = grey
        tile[..., 1] = grey
        tile[..., 2] = grey
        tile[..., 3] = (alpha * 255.0 + 0.5).astype(np.uint8)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(out)
    print(f"cloud_sheet: {out}, {CELL * 2}x{CELL * 2}, four clouds")


if __name__ == "__main__":
    main()
