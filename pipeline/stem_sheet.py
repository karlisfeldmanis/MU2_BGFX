"""Paints the stem card Noria's flower heads stand on, content/textures/flower_stem.png.

    python3 pipeline/stem_sheet.py [--seed 3] [--preview out.png]

Not MU's (the user, 2026-10-18: the flowers "float in air without any support"). MU's
Object12 to Object17 are flower heads with nothing under them; clean_lowpoly.grow_stems gives
each a crossed pair of cards, and this is what the cards wear: one stem up the middle, root at
the bottom edge, with two small leaves off it low down. The greens sit with the meadow's
(pipeline/meadow.py), a little under Noria's lawn.

Drawn at four times the size and reduced, and every clear texel's colour filled from its
painted neighbours, as meadow.py does, so the cut-out's edge pulls in no black fringe.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.append(str(Path(__file__).resolve().parents[1] / "tools"))
sys.path.append(str(Path(__file__).resolve().parent))
from paths import P  # noqa: E402
from meadow import blade, filled, jitter  # noqa: E402

import random  # noqa: E402

W, H, SCALE = 32, 128, 4
TARGET = P.CONTENT / "textures" / "flower_stem.png"

STEM = (70, 94, 30)
LEAF = (84, 108, 34)


def paint(seed: int) -> Image.Image:
    rng = random.Random(seed)
    big = Image.new("RGBA", (W * SCALE, H * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(big)
    w, h = big.size
    mid = w / 2

    # Two leaves low on the stem, one each side, before the stem so it draws over their roots.
    for side, at in ((-1, 0.72), (1, 0.55)):
        root = (mid, h * at)
        tip = (mid + side * w * 0.42, h * at - h * 0.12)
        bend = (mid + side * w * 0.28, h * at + h * 0.01)
        blade(draw, root, bend, tip, 34, 4, jitter(LEAF, rng, 8))

    # The stem: a little wider at the root, a faint curve, the full height of the card.
    blade(draw, (mid, h), (mid + rng.uniform(-6, 6), h * 0.5), (mid, 0), 22, 16,
          jitter(STEM, rng, 6))
    # A lighter line down its lit side, so it reads round rather than flat.
    blade(draw, (mid - 3, h), (mid - 3, h * 0.5), (mid - 3, 0), 6, 4,
          tuple(min(255, c + 22) for c in STEM) + (255,))

    return filled(big.resize((W, H), Image.LANCZOS))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    sheet = paint(args.seed)
    sheet.save(TARGET)
    print(f"  stem       {TARGET.relative_to(P.ROOT.parent)}  {sheet.width}x{sheet.height}")

    if args.preview:
        ground = Image.new("RGBA", sheet.size, (90, 90, 110, 255))
        ground.alpha_composite(sheet)
        ground.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
