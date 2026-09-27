"""Blurs a cut-out sheet's alpha so its comb of tips reads as fewer, wider ones.

    python3 pipeline/soften_alpha.py in.png out.png ACROSS ALONG

ACROSS and ALONG are the blur's sigma in texels of the sheet as it arrives here (the 3x),
along the sheet's x and y. The colour is untouched; only the mask moves.

Why this exists: `grass_01` is MU's 32x64 straw, and its top and bottom are a comb of strand
tips cut out of it. At 3x the slits between those tips are one to three texels wide -- 26 of
them across the sheet at the half, 21 at MU's own quarter -- and the bales wear the whole
sheet on every side, so from the game's camera each slit is under a pixel. A cut on a mask
that fine is decided afresh every frame the camera moves, and the straw by Lorencia's houses
crawled in tiny pixels as the hero walked (2026-09-24). No filter in the renderer reaches it:
the tips are finer than the pixel before any test is made.

A blur across the strands merges neighbouring slits into one notch and leaves the band's
fill where it was. At 2.5 across and 0.8 along, cut at the quarter: ten notches, three of
them small, fill 0.98 against the sheet's 0.96. Ours, not MU's: MU tested the 32x64 as it was.
"""

import sys

import numpy as np
from PIL import Image


def kernel(sigma: float) -> np.ndarray:
    reach = max(1, int(3.0 * sigma))
    x = np.arange(-reach, reach + 1, dtype=np.float64)
    k = np.exp(-x * x / (2.0 * sigma * sigma))
    return k / k.sum()


def blur(field: np.ndarray, sigma: float, axis: int, mode: str) -> np.ndarray:
    if sigma <= 0.0:
        return field
    k = kernel(sigma)
    pad = len(k) // 2
    widths = [(pad, pad) if i == axis else (0, 0) for i in range(2)]
    padded = np.pad(field, widths, mode=mode)
    return np.apply_along_axis(lambda row: np.convolve(row, k, "valid"), axis, padded)


def main() -> None:
    source, destination = sys.argv[1], sys.argv[2]
    across, along = float(sys.argv[3]), float(sys.argv[4])
    image = Image.open(source).convert("RGBA")
    pixels = np.asarray(image).copy()
    alpha = pixels[..., 3].astype(np.float64) / 255.0
    # Across wraps, because the bales wrap the sheet round themselves; along does not, because
    # the sheet's top and bottom are the two ends of a bundle and never meet.
    alpha = blur(alpha, across, 1, "wrap")
    alpha = blur(alpha, along, 0, "edge")
    pixels[..., 3] = np.clip(np.round(alpha * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(pixels, "RGBA").save(destination, "PNG", optimize=True)
    print(f"  alpha      softened {across} across, {along} along -> {destination}")


if __name__ == "__main__":
    main()
