"""Paints a sheet of grass blades, MU2_BGFX/assets/effects/grass/sward.png.

    python3 MU2_BGFX/pipeline/sward.py [--seed 11] [--preview out.png]

Why this exists, said plainly: **MU's own grass sheet cannot draw a blade.**

MU paints one tuft, four 64-pixel columns of it, and stands cut-out cards of it on the land.
At MU's own resolution and at MU's own scale -- one quad a tile, a metre wide -- that reads.
It does not read here. A card narrow enough to draw a blade-width blade is 12 cm across, and
12 cm of a 64-pixel column is sixteen pixels of soft overlapping strokes; every cutout
threshold from 0.28 to 0.68 was tried on it and the field went from overlapping plates
straight to nothing, with no setting in between where a blade came apart from its neighbour.
The strokes are soft, broad and joined at the root because they were painted to be seen as a
tuft. docs/grass.md has the sweep.

So the sheet is the limit, not the method. This paints one that is not: eight columns of
separate blades, each its own height, lean, width and green, at a resolution where a blade is
six to ten pixels across rather than one or two. Everything around it -- the patch system, the
hashing, the clumping, the cutout, the coverage-held mips, the alpha-to-coverage, the colour
grade -- is unchanged and does not care which sheet it is given.

Eight cells of 128 by 256, root at the bottom edge, painted as a side view as MU's are:

    0..7  a spray of three to five blades, no two cells alike

Drawn at three times the size and reduced, and the colour of every clear texel is filled from
its painted neighbours, so the linear filter never pulls a black fringe in at a cut edge --
which is meadow.py's trick and is needed here for the same reason. The shader grades this
sheet by its LUMINANCE, so what is painted here is light and shade first and colour second: a
blade is dark where it is sheathed and pale where it is thin, and that is what survives the
grade.
"""

from __future__ import annotations

import argparse
import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

CELL_W, CELL_H = 128, 256
CELLS = 8
SCALE = 3

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "assets" / "effects" / "grass" / "sward.png"

# Greens around MU's own lawn. The shader re-hues by luminance, so the spread that matters here
# is the spread in VALUE -- these differ in brightness as much as in hue on purpose.
# Olive, as MU's lawn is -- (69, 64, 16) is what Lorencia's own sheet averages, and these sit
# around it rather than over it. The shader's grade can always take them greener; it cannot
# take a neon sheet back.
LEAF = [(76, 80, 38), (88, 92, 44), (60, 64, 28), (100, 102, 52), (82, 84, 40)]


def bezier(p0, p1, p2, t):
    u = 1.0 - t
    return (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
            u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])


def blade(draw, rng, base_x):
    """One blade: a tapered strip along a quadratic curve, root at the bottom edge."""
    h = CELL_H * SCALE
    w = CELL_W * SCALE

    height = rng.uniform(0.52, 1.0) * h
    lean = rng.uniform(-0.30, 0.30) * height
    # The arch: stiff blades stand and turn over at the top, floppy ones bow the whole way.
    stiff = rng.uniform(0.0, 1.0)
    p0 = (base_x, h)
    p2 = (base_x + lean, h - height)
    p1 = (base_x + lean * (0.36 - 0.28 * stiff), h - height * (0.44 + 0.30 * stiff))

    # FAT in texels, thin in metres. A card is 128 texels across and lands about twenty pixels
    # wide on screen, so the sheet is minified six times over -- and a stroke six texels wide
    # does not survive that. It box-averages to a smear, and the coverage-preserving mip chain
    # then scales that smear back up until it passes the cutout as a fat blob. That is what
    # "plates blended together" was: not the card, not the count, the mip chain inflating a
    # stroke too thin to survive it.
    #
    # So a blade is eighteen to thirty texels of the cell's hundred and twenty-eight -- wide
    # enough to still be a blade at mip 3 -- and its thinness in the WORLD comes from the card
    # being narrow, which costs nothing and survives everything.
    root_w = rng.uniform(18.0, 30.0) * SCALE
    colour = LEAF[rng.randrange(len(LEAF))]
    # Along its length the blade lightens: sheathed and shaded at the root, thin and lit at the
    # tip. This gradient is the whole of what survives the shader's luminance grade, so it is
    # painted here rather than left to the lighting.
    steps = 26
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        x, y = bezier(p0, p1, p2, t)
        nx, ny = bezier(p0, p1, p2, min(1.0, t + 1e-3))
        px, py = bezier(p0, p1, p2, max(0.0, t - 1e-3))
        dx, dy = nx - px, ny - py
        length = math.hypot(dx, dy) or 1.0
        # Nearly full width for the first half, then away to a point. A linear taper is a spike.
        half = root_w * (1.0 - t * t * 0.92) * 0.5
        left.append((x - dy / length * half, y + dx / length * half))
        right.append((x + dy / length * half, y - dx / length * half))

    # Drawn as a few bands rather than one polygon, so the gradient up the blade is real.
    bands = 6
    for b in range(bands):
        a, c = b * steps // bands, (b + 1) * steps // bands
        t = (b + 0.5) / bands
        lift = 0.62 + 0.62 * t
        shade = tuple(min(255, int(v * lift)) for v in colour) + (255,)
        poly = left[a:c + 1] + right[a:c + 1][::-1]
        if len(poly) >= 3:
            draw.polygon(poly, fill=shade)


def paint_cell(draw, rng):
    # Five to nine blades, their roots spread across the cell but gathered towards the middle:
    # a tuft grows from a crown, not from a line.
    count = rng.randint(3, 5)
    w = CELL_W * SCALE
    for _ in range(count):
        base = w * 0.5 + rng.gauss(0.0, w * 0.21)
        base = max(w * 0.12, min(w * 0.88, base))
        blade(draw, rng, base)


def filled(image: Image.Image) -> Image.Image:
    """Fills every clear texel's colour from its painted neighbours, keeping the alpha.

    meadow.py's, and needed for the same reason: the linear filter reads colour out of texels
    the cutout throws away, and a clear texel that is black pulls a dark fringe along every
    blade's edge.
    """
    rgba = np.asarray(image, dtype=np.float32) / 255.0
    alpha = rgba[..., 3:4]
    colour = rgba[..., :3] * alpha
    weight = alpha.copy()
    out = rgba[..., :3].copy()
    known = alpha[..., 0] > 0.5

    for radius in (1, 2, 4, 8, 16):
        def box(a, r=radius):
            k = r * 2 + 1
            p = np.pad(a, ((r, r), (r, r), (0, 0)), mode="edge")
            c = p.cumsum(0).cumsum(1)
            c = np.pad(c, ((1, 0), (1, 0), (0, 0)))
            return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)

        wc, ww = box(colour), box(weight)
        fresh = (~known) & (ww[..., 0] > 1e-4)
        out[fresh] = wc[fresh] / ww[fresh]
        known = known | fresh

    return Image.fromarray(
        (np.clip(np.concatenate([out, alpha], axis=-1), 0, 1) * 255).astype(np.uint8), "RGBA")


def feathered(image: Image.Image) -> Image.Image:
    """Softens the alpha by about a texel, and it is not a nicety.

    The shader turns this sheet's alpha into sample coverage, and it measures how wide to make
    that ramp from how fast the alpha changes across a pixel (`fwidth`). A polygon edge reduced
    by LANCZOS is very nearly binary: alpha goes 0 to 255 inside a texel, so the derivative is
    huge, the ramp collapses to nothing and alpha-to-coverage has no partial coverage to hand
    out. The field then crawls exactly as a hard alpha test does -- measured at 9.15 levels of
    per-pixel change a frame against the ground's 4.89, where the soft sheet it replaced sat at
    4.68.

    MU's own tuft is soft for the same reason it has to be. This gives the painted blades the
    edge they need, and it costs about a texel of width, which the cutout takes back.
    """
    rgba = np.asarray(image).copy()
    alpha = Image.fromarray(rgba[..., 3], "L").filter(ImageFilter.GaussianBlur(0.8))
    rgba[..., 3] = np.asarray(alpha)
    return Image.fromarray(rgba, "RGBA")


def paint(seed: int) -> Image.Image:
    sheet = Image.new("RGBA", (CELL_W * CELLS, CELL_H), (0, 0, 0, 0))
    for index in range(CELLS):
        rng = random.Random(seed * 1000 + index)
        big = Image.new("RGBA", (CELL_W * SCALE, CELL_H * SCALE), (0, 0, 0, 0))
        paint_cell(ImageDraw.Draw(big), rng)
        # Kept inside its own cell with a margin of clear, so a card cut to one column never
        # reaches into the next. The sampler clamps at the PICTURE's edge, not at a column's.
        margin = SCALE * 3
        mask = Image.new("L", big.size, 0)
        ImageDraw.Draw(mask).rectangle((margin, 0, big.width - margin, big.height), fill=255)
        big.putalpha(Image.fromarray(np.minimum(np.asarray(big)[..., 3], np.asarray(mask))))
        sheet.paste(big.resize((CELL_W, CELL_H), Image.LANCZOS), (index * CELL_W, 0))
    return feathered(filled(sheet))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=11)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    sheet = paint(args.seed)
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(TARGET)
    covered = float((np.asarray(sheet)[..., 3] >= 72).mean())
    print(f"  sward      {TARGET.relative_to(ROOT.parent)}  {sheet.width}x{sheet.height}  "
          f"{covered * 100:.1f}% over the cutout")

    if args.preview:
        shown = sheet.copy()
        shown.putalpha(Image.fromarray(
            np.where(np.asarray(sheet)[..., 3] >= 72, 255, 0).astype(np.uint8)))
        ground = Image.new("RGBA", sheet.size, (52, 44, 34, 255))
        ground.alpha_composite(shown)
        ground.convert("RGB").save(args.preview)


if __name__ == "__main__":
    main()
