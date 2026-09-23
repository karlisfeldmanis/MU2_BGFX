"""Cuts the silhouette out of each of MU's empty-slot plates, into source/interface.

MU paints an empty worn slot with `win_slot_*.png`: a gold-rimmed plate of dark leather with a
diamond weave, and the shape of what goes there -- a helm, a boot, a sword -- painted a little
LIGHTER on it. Drawn whole over the flat skin the plate is a brown tile filling the cell, which
is exactly what a flat cell is not; tinted dark it was a muddier tile with a shape lost in it.
The user, 2026-09-23: *"equipment slots could looks better"*.

What a flat window wants is the shape alone, as a mask, so the bag can lay it in the cell at
whatever tone and size it likes. So: inside the rim, take how much lighter each pixel is than the
plate around it (the 35th percentile of the inner area is plate, the 99th is the silhouette's
brightest paint), throw away the weave's own faint lights, blur the pixel grain half a pixel,
keep only the patches of paint at least a sixth the size of the largest (the weave leaks in as
specks and hatching, and a pair of boots is two patches), and write white at that alpha,
trimmed to the shape with two
pixels round it: `win_ghost_<slot>.png`. index.py lists them under `bag_ghost_*`.

    python3 pipeline/slot_ghosts.py            all eleven
    python3 pipeline/slot_ghosts.py helm       one, by MU's slot name
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = PROJECT / "source" / "interface"

#: The rim: gold, and a shadow inside it, none of which is the plate.
RIM = 10
#: Below this much of the way from plate to brightest paint, a pixel is weave.
FLOOR = 0.18
#: And below this, after the blur, it is not the shape's edge either.
KEEP = 0.25
#: Air round the trimmed shape.
PAD = 2


#: A patch smaller than this share of the largest is weave, not shape. A pair of boots is two
#: patches of about a size; the armour's skirt is a third of its chest; a speck is a hundredth.
SHARE = 0.15


def connected(mask: np.ndarray) -> np.ndarray:
    """The 4-connected patches of `mask` that are at least SHARE of the largest."""
    labels = np.zeros(mask.shape, dtype=np.int32)
    current = 0
    height, width = mask.shape
    for sy in range(height):
        for sx in range(width):
            if not mask[sy, sx] or labels[sy, sx]:
                continue
            current += 1
            stack = [(sy, sx)]
            labels[sy, sx] = current
            while stack:
                y, x = stack.pop()
                for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                    if 0 <= ny < height and 0 <= nx < width and mask[ny, nx] and not labels[ny, nx]:
                        labels[ny, nx] = current
                        stack.append((ny, nx))
    if current == 0:
        return mask
    sizes = np.bincount(labels.ravel())[1:]
    kept = np.flatnonzero(sizes >= sizes.max() * SHARE) + 1
    return np.isin(labels, kept)


def cut(plate: Path) -> Path:
    image = np.asarray(Image.open(plate).convert("RGBA")).astype(float)
    height, width = image.shape[:2]
    inner = image[RIM : height - RIM, RIM : width - RIM]
    lum = inner[..., :3].mean(axis=2)
    base = np.percentile(lum, 35)
    spread = max(np.percentile(lum, 99) - base, 1.0)
    light = np.clip((lum - base) / spread, 0.0, 1.0)
    light = np.where(light < FLOOR, 0.0, light)
    blurred = Image.fromarray((light * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(0.6)
    )
    alpha = np.asarray(blurred).astype(float) / 255.0
    # The shape is the biggest patch; the weave is specks. A dilation of the patch by a pixel
    # takes the shape's soft edge back with it.
    body = connected(alpha > KEEP)
    grown = body.copy()
    grown[1:, :] |= body[:-1, :]
    grown[:-1, :] |= body[1:, :]
    grown[:, 1:] |= body[:, :-1]
    grown[:, :-1] |= body[:, 1:]
    alpha = np.where(grown, alpha, 0.0)
    ys, xs = np.where(alpha > 0.0)
    y0, y1 = max(0, ys.min() - PAD), min(alpha.shape[0], ys.max() + 1 + PAD)
    x0, x1 = max(0, xs.min() - PAD), min(alpha.shape[1], xs.max() + 1 + PAD)
    trimmed = np.clip(alpha[y0:y1, x0:x1] * 1.15, 0.0, 1.0)
    out = np.zeros(trimmed.shape + (4,), dtype=np.uint8)
    out[..., :3] = 255
    out[..., 3] = (trimmed * 255).astype(np.uint8)
    target = plate.with_name(plate.name.replace("win_slot_", "win_ghost_"))
    Image.fromarray(out, "RGBA").save(target)
    return target


def main() -> None:
    wanted = sys.argv[1:]
    for plate in sorted(ASSETS.glob("win_slot_*.png")):
        slot = plate.stem[len("win_slot_") :]
        if wanted and slot not in wanted:
            continue
        target = cut(plate)
        with Image.open(target) as ghost:
            print(f"  ghost      {slot:<13} {ghost.width}x{ghost.height} -> {target.relative_to(PROJECT)}")


if __name__ == "__main__":
    main()
