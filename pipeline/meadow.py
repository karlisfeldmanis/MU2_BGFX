"""Paints the meadow's wild plants into one sheet, source/effects/grass/wild.png.

    python3 pipeline/meadow.py [--seed 7] [--preview out.png]

Not MU's. The client grows one painted strip per grass tile and nothing else; this bench
scatters remaster_one-style cards of that strip, and every one of them is a slice of the same
256-pixel picture, so a field of them is one tuft many times. What a meadow has that a lawn
does not is other plants: a seed head standing over the blades, a broad leaf lying under
them, a few flowers. They are this project's invention and are marked as such in Turf.

Eight cells of 64 by 128, root at the bottom edge, painted as a side view like the grass
sheets are:

    0  timothy     two or three stems with a cylindrical seed head
    1  foxtail     one arching stem with a drooping fuzzy head
    2  blades      a few long single blades, taller than the lawn
    3  broadleaf   a low fan of plantain leaves
    4  clover      a low mound of trefoils
    5  daisy       white heads with a yellow eye
    6  buttercup   small yellow cups on branching stems
    7  bellflower  small violet bells

Drawn at four times the size and reduced, and the colour of every clear texel is filled from
its painted neighbours, so the linear filter never pulls a black fringe in at a cut edge. The
shader alpha-tests at 0.28, so strokes are kept at least a texel and a half wide at the end.
The painted top of each cell is what Turf reads as the plant's height, so a clover painted to
a third of the cell stands a third of a stalk's height without anything saying so.
"""

from __future__ import annotations

import argparse
import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

CELL_W, CELL_H = 64, 128
CELLS = 8
SCALE = 4

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "source" / "effects" / "grass" / "wild.png"

# Greens a little under Noria's lawn (112, 123, 24) and over Lorencia's (69, 64, 16); Turf
# dims the sheet per world rather than this painting two of them.
LEAF = [(88, 108, 34), (74, 96, 30), (104, 118, 40), (96, 104, 36)]
STRAW = (150, 138, 84)


def jitter(colour: tuple[int, int, int], rng: random.Random, amount: int = 12) -> tuple[int, ...]:
    return tuple(max(0, min(255, c + rng.randint(-amount, amount))) for c in colour) + (255,)


def bezier(p0, p1, p2, t):
    u = 1 - t
    return (
        u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
        u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1],
    )


def blade(draw, p0, p1, p2, root_w, tip_w, colour, steps=24):
    """A tapered strip along a quadratic curve, as one polygon."""
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        x, y = bezier(p0, p1, p2, t)
        nx, ny = bezier(p0, p1, p2, min(1, t + 1e-3))
        px, py = bezier(p0, p1, p2, max(0, t - 1e-3))
        dx, dy = nx - px, ny - py
        length = math.hypot(dx, dy) or 1
        w = (root_w + (tip_w - root_w) * t) / 2
        left.append((x - dy / length * w, y + dx / length * w))
        right.append((x + dy / length * w, y - dx / length * w))
    draw.polygon(left + right[::-1], fill=colour)
    return bezier(p0, p1, p2, 1.0)


def stem(draw, rng, base_x, height, bend, width, colour):
    """A stem from the bottom edge, returning where its top ended up."""
    h = CELL_H * SCALE
    p0 = (base_x, h)
    p2 = (base_x + bend, h - height)
    p1 = (base_x + bend * 0.2 + rng.uniform(-6, 6), h - height * 0.55)
    return blade(draw, p0, p1, p2, width, width * 0.8, colour)


def timothy(draw, rng):
    for _ in range(rng.randint(2, 3)):
        base = rng.uniform(96, 160)
        height = rng.uniform(300, 440)
        top = stem(draw, rng, base, height, rng.uniform(-40, 40), 7, jitter(LEAF[1], rng))
        head = jitter(STRAW, rng, 16)
        draw.ellipse((top[0] - 10, top[1] - 70, top[0] + 10, top[1] + 4), fill=head)
    for _ in range(3):
        base = rng.uniform(100, 156)
        blade(draw, (base, 512), (base + rng.uniform(-30, 30), 400),
              (base + rng.uniform(-90, 90), rng.uniform(300, 380)), 16, 3, jitter(LEAF[0], rng))


def foxtail(draw, rng):
    base = rng.uniform(110, 140)
    p0, p1, p2 = (base, 512), (base + 20, 200), (base + 110, 150)
    blade(draw, p0, p1, p2, 8, 6, jitter(LEAF[2], rng))
    # The head droops from the top of the arch, a fuzzy spindle of overlapping dots.
    for i in range(22):
        t = i / 21
        x = p2[0] + t * 30
        y = p2[1] + t * 110
        r = 12 * math.sin(math.pi * (0.15 + 0.85 * t)) + 4
        draw.ellipse((x - r, y - r, x + r, y + r), fill=jitter((168, 160, 96), rng, 14))
    for _ in range(4):
        b = rng.uniform(90, 170)
        blade(draw, (b, 512), (b + rng.uniform(-20, 20), 420),
              (b + rng.uniform(-100, 100), rng.uniform(330, 400)), 18, 3, jitter(LEAF[0], rng))


def blades(draw, rng):
    for _ in range(rng.randint(4, 6)):
        base = rng.uniform(80, 176)
        lean = rng.uniform(-120, 120)
        top = rng.uniform(20, 160)
        blade(draw, (base, 512), (base + lean * 0.1, top + 200), (base + lean, top),
              rng.uniform(14, 22), 2.5, jitter(rng.choice(LEAF), rng, 16))


def broadleaf(draw, rng):
    count = rng.randint(6, 8)
    for i in range(count):
        side = (i / (count - 1)) * 2 - 1
        base = 128 + side * 10
        reach = rng.uniform(90, 120)
        tip = (128 + side * reach, 512 - rng.uniform(120, 200) * (1.1 - abs(side) * 0.5))
        mid = (128 + side * reach * 0.5, tip[1] - 30)
        blade(draw, (base, 512), mid, tip, 10, 4, jitter(LEAF[1], rng, 10))
        # The leaf itself: a fat spindle along the last two thirds of the stalk.
        blade(draw, bezier((base, 512), mid, tip, 0.3), bezier((base, 512), mid, tip, 0.65), tip,
              46, 4, jitter(LEAF[0], rng, 10))


def clover(draw, rng):
    for _ in range(rng.randint(9, 12)):
        base = rng.uniform(60, 196)
        height = rng.uniform(70, 150)
        top = stem(draw, rng, base, height, rng.uniform(-30, 30), 6, jitter(LEAF[2], rng))
        colour = jitter(LEAF[3], rng, 10)
        # Three leaflets seen edge-on: left, right, and one standing up between them.
        for dx, dy in ((-20, 0), (20, 0), (0, -12)):
            cx, cy = top[0] + dx, top[1] + dy
            draw.ellipse((cx - 18, cy - 12, cx + 18, cy + 12), fill=colour)


def flowers(draw, rng, petal, eye, size, count, cup=False, bell=False):
    for _ in range(3):
        b = rng.uniform(90, 166)
        blade(draw, (b, 512), (b + rng.uniform(-20, 20), 440),
              (b + rng.uniform(-80, 80), rng.uniform(380, 440)), 16, 3, jitter(LEAF[0], rng))
    for _ in range(count):
        base = rng.uniform(96, 160)
        height = rng.uniform(220, 400)
        top = stem(draw, rng, base, height, rng.uniform(-50, 50), 6, jitter(LEAF[1], rng))
        x, y = top
        if bell:
            # A bell hangs: a stubby cone below the top of the stem, opening downward.
            draw.polygon([(x - 4, y), (x + 4, y), (x + size, y + size * 2), (x - size, y + size * 2)],
                         fill=jitter(petal, rng, 12))
            draw.ellipse((x - size, y + size * 1.6, x + size, y + size * 2.3), fill=jitter(petal, rng, 12))
        elif cup:
            draw.pieslice((x - size, y - size, x + size, y + size), 0, 180, fill=jitter(petal, rng, 10))
            draw.ellipse((x - size, y - size * 0.35, x + size, y + size * 0.35), fill=jitter(petal, rng, 10))
        else:
            # A daisy seen from the side: a flattened disk of petals, the eye on top of it.
            draw.ellipse((x - size, y - size * 0.45, x + size, y + size * 0.45), fill=jitter(petal, rng, 6))
            draw.ellipse((x - size * 0.38, y - size * 0.55, x + size * 0.38, y + size * 0.1), fill=eye)


PAINTERS = [
    timothy,
    foxtail,
    blades,
    broadleaf,
    clover,
    lambda d, r: flowers(d, r, (236, 234, 222), (222, 176, 40), 26, 4),
    lambda d, r: flowers(d, r, (236, 196, 40), None, 16, 6, cup=True),
    lambda d, r: flowers(d, r, (132, 104, 196), None, 11, 5, bell=True),
]


def filled(image: Image.Image) -> Image.Image:
    """Fills every clear texel's colour from its painted neighbours, keeping the alpha."""
    rgba = np.asarray(image, dtype=np.float32) / 255.0
    alpha = rgba[..., 3:4]
    colour = rgba[..., :3] * alpha
    weight = alpha.copy()
    out = rgba[..., :3].copy()
    known = alpha[..., 0] > 0.5

    for radius in (1, 2, 4, 8, 16, 32):
        # Box blur by cumulative sums, per channel, which numpy does without SciPy.
        def box(a):
            k = radius * 2 + 1
            p = np.pad(a, ((radius, radius), (radius, radius), (0, 0)), mode="edge")
            c = p.cumsum(0).cumsum(1)
            c = np.pad(c, ((1, 0), (1, 0), (0, 0)))
            return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)

        wc, ww = box(colour), box(weight)
        fresh = (~known) & (ww[..., 0] > 1e-4)
        out[fresh] = wc[fresh] / ww[fresh]
        known = known | fresh

    result = np.concatenate([out, alpha], axis=-1)
    return Image.fromarray((np.clip(result, 0, 1) * 255).astype(np.uint8), "RGBA")


def paint(seed: int) -> Image.Image:
    sheet = Image.new("RGBA", (CELL_W * CELLS, CELL_H), (0, 0, 0, 0))

    for index, painter in enumerate(PAINTERS):
        rng = random.Random(seed * 1000 + index)
        big = Image.new("RGBA", (CELL_W * SCALE, CELL_H * SCALE), (0, 0, 0, 0))
        painter(ImageDraw.Draw(big), rng)

        # Kept inside its own cell with a texel of clear margin, so a card cut to it never
        # reaches into the next plant.
        margin = SCALE * 2
        mask = Image.new("L", big.size, 0)
        ImageDraw.Draw(mask).rectangle((margin, margin, big.width - margin, big.height), fill=255)
        big.putalpha(Image.fromarray(np.minimum(np.asarray(big)[..., 3], np.asarray(mask))))

        small = big.resize((CELL_W, CELL_H), Image.LANCZOS)
        sheet.paste(small, (index * CELL_W, 0))

    return filled(sheet)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    sheet = paint(args.seed)
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(TARGET)
    print(f"  meadow     {TARGET.relative_to(ROOT.parent)}  {sheet.width}x{sheet.height}")

    if args.preview:
        shown = sheet.copy()
        shown.putalpha(Image.fromarray(np.where(np.asarray(sheet)[..., 3] >= 72, 255, 0).astype(np.uint8)))
        ground = Image.new("RGBA", sheet.size, (90, 90, 110, 255))
        ground.alpha_composite(shown)
        ground.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
