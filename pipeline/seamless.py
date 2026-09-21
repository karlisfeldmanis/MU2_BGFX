"""Makes a ground sheet wrap, by pulling its opposite borders onto their common average.

    python3 pipeline/seamless.py <in.png> <out.png> [band]

MU's ground sheets do not tile. Measured on the native art rather than assumed: the top and
bottom rows of `tileground02` differ by 65 of 255 where a typical neighbouring row differs by
8, and the left and right columns by 22. So the join is eight times the size of the steps
either side of it, which is not a texture that tiles — it is a texture with an edge.

The client got away with it. MU draws 64 texels of a sheet on each terrain tile, on a camera
where a tile is about forty pixels, with nearest filtering: the join lands inside a pixel or
two and is one more speck in a noisy image. Here a tile is a metre, the sheet is upscaled
threefold and filtered linearly, and the same join is a continuous line across the whole
plaza every two metres, in both directions. It is most of what "the ground has a grid on it"
is.

The heal is deliberately the dullest one that works. Both borders are moved toward their
shared average, by an amount that is full strength at the outermost row and falls to nothing
over a narrow band, so the two edges meet. Nothing is invented, nothing is copied in from
elsewhere in the sheet, and the interior — everything past the band — is untouched to the
byte. What it costs is that the outermost few per cent of MU's painting is averaged with the
painting at the far side, which at this band width is a change of a few levels on a handful of
rows.

*Not the same thing as the upscale's wrap padding.* That already exists and does a different
job: it feeds the model the far edge as context so the upscale does not invent a border of its
own. It cannot remove a seam that was in the art before the upscale ran, and it did not.

*Not a fix for repetition.* A sheet that tiles perfectly still shows its own features every
two metres, and `tileground02` has a bright band across it that reads clearly at that spacing.
That is a separate problem with separate answers, and this is not one of them.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

#: How far the blend reaches in from each border, as a fraction of that side's length.
#:
#: Six per cent, which on a 384-pixel upscaled sheet is 23 pixels. Wide enough that the ramp
#: is not itself a visible line, narrow enough that the great majority of the sheet is the
#: art exactly as it was: at this width 88% of the image is untouched.
DEFAULT_BAND = 0.06


def heal(plane: np.ndarray, band: int, axis: int) -> np.ndarray:
    """Bring the two borders along one axis onto their average."""
    if band < 1:
        return plane

    moved = plane.copy()
    length = plane.shape[axis]

    for step in range(band):
        # Full strength at the outermost line, nothing at the inner end of the band. Halved
        # because both sides move: each gives up half the gap, so they arrive together.
        weight = 0.5 * (1.0 - (step / band))

        near = np.take(plane, step, axis=axis).astype(np.float32)
        far = np.take(plane, length - 1 - step, axis=axis).astype(np.float32)
        middle = (near + far) * 0.5

        index = [slice(None)] * plane.ndim
        index[axis] = step
        moved[tuple(index)] = near + ((middle - near) * (weight * 2.0))

        index[axis] = length - 1 - step
        moved[tuple(index)] = far + ((middle - far) * (weight * 2.0))

    return moved


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 seamless.py <in.png> <out.png> [band]", file=sys.stderr)
        raise SystemExit(2)

    source, target = Path(sys.argv[1]), Path(sys.argv[2])
    fraction = float(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_BAND

    image = Image.open(source)
    mode = image.mode
    plane = np.asarray(image.convert("RGBA" if "A" in mode else "RGB"), dtype=np.float32)

    def join(data: np.ndarray, axis: int) -> tuple[float, float]:
        """The wrap-around step, and the typical step between neighbours, for scale."""
        first = np.take(data, 0, axis=axis)
        last = np.take(data, data.shape[axis] - 1, axis=axis)
        wrap = float(np.abs(last - first).mean())
        inner = float(np.abs(np.diff(data, axis=axis)).mean())
        return wrap, inner

    before = (join(plane, 0), join(plane, 1))

    healed = heal(plane, max(1, round(plane.shape[0] * fraction)), axis=0)
    healed = heal(healed, max(1, round(plane.shape[1] * fraction)), axis=1)

    after = (join(healed, 0), join(healed, 1))

    Image.fromarray(np.clip(healed, 0, 255).astype(np.uint8), mode="RGBA" if "A" in mode
                    else "RGB").convert(mode).save(target)

    print(f"=== {source.name} -> {target.name} ===")
    print(f"  size       {image.width} x {image.height}, band "
          f"{round(plane.shape[1] * fraction)} px")

    for axis, name in ((0, "top/bottom"), (1, "left/right")):
        was, now = before[axis], after[axis]
        print(f"  {name:11s} join {was[0]:5.1f} -> {now[0]:5.1f}   "
              f"(a typical neighbouring line differs by {now[1]:.1f})")


main()
