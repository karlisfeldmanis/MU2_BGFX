"""Cuts the silhouette out of each of MU's empty-slot plates, into source/interface.

MU paints an empty worn slot with `win_slot_*.png`: a gold-rimmed plate of dark leather with a
diamond weave, and the shape of what goes there -- a helm, a boot, a sword -- painted a little
LIGHTER on it. Drawn whole over the flat skin the plate is a brown tile filling the cell, which
is exactly what a flat cell is not; tinted dark it was a muddier tile with a shape lost in it.
The user, 2026-09-23: *"equipment slots could looks better"*.

What a flat window wants is the shape alone, so the bag can lay it in the cell at whatever tone
and size it likes: `win_ghost_<slot>.png`, which index.py lists under `bag_ghost_*`.

*How the shape is found.* The first cut of this script asked how much lighter each pixel was
than the plate around it and wrote that as alpha. It could not work, and the reason is in the
art: the weave is not a faint texture but a bold diamond maze whose strokes are as light as the
sword lying across them. Thresholding brightness therefore had to choose between a shape with
the weave hanging off it in hooks and bars, and a shape eaten down to its brightest patches.
It chose the second, and the ghosts were blobs.

**The weave is the same painting on every plate of a size.** That is what this cut uses. Stack
the plates that share a size and take the darkest value at each pixel: the object is lighter
than the leather, so wherever ONE plate is bare that pixel comes out as pure weave. Where they
all cover the same pixel the stack is contaminated -- the sword and the shield and the breast-
plate all lie down the middle of the 92x132 plate -- so a second estimate is taken from each
plate folded onto itself. The weave is symmetric (mirrored about both axes, and about the
diagonals too where the plate is square), the objects are not, so the folds give a bare reading
almost everywhere; they run a little dark, because a fold can only ever lower the minimum. So:
take the stack where the two agree, and the folds where the stack is more than TAU brighter,
which is exactly where the stack is standing on somebody's object. Subtract that, and the weave
is gone -- not thresholded away, subtracted -- and what is left is the object, whole, down to
the dark folds of a boot that no brightness threshold could have kept.

The shape is then closed at three times MU's size -- a threshold on a coarse grid is a staircase
-- and written at one and a half, which is about what a slot is drawn at (`written`):

- what stands above the noise of the leftover weave, measured on a band round the edge of the
  plate where nothing is ever painted, is the shape;
- opened by a pixel and grown back, so the weave's last hairs come off but the shape's own
  edge does not move;
- the patches that hold a bright pixel and are at least a sixth of the largest (a pair of boots
  is two patches, the armour's skirt is a third of its chest, a speck of weave is a hundredth);
- grown out along the shape's own dim paint, a pixel at a time and not far, so a band that turns
  away from the light -- the ring's -- is followed round instead of broken off;
- small holes filled, large ones kept: a ring is a ring, and its middle is not a hole;
- its outline blurred and cut again at a half, which takes off anything thinner than the blur
  and keeps every bay and spur of MU's own drawing;
- and written as MU's own shading in RGB with the coverage in alpha, rather than the old flat
  white whose alpha carried the tone. That is the other half of why the ghosts were blobs: a
  shape whose dark parts are transparent has no dark parts, only missing ones. Now the bag
  tints a solid silhouette and the folds inside it are MU's.

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
#: The shape is found at this multiple of the plate, which is MU's 46x46 already doubled...
SCALE = 3
#: ...and written at this one, which is about the size a worn slot is drawn at. See `written`.
OUT = 1.5
#: How far over the leftover weave's own brightest corner the floor stands.
EDGE = 1.05
#: Growth follows paint this share of the floor, if it starts on the shape and stays beside it.
DIM = 0.45
#: Brighter than this above the folded estimate and the stacked one is standing on an object.
TAU = 8.0
#: The floor under the noise estimate: nothing below this share of the brightest paint is shape.
LOW = 0.05
#: A patch with nothing this bright in it is not the shape, whatever its size.
HIGH = 0.28
#: A patch smaller than this share of the largest is weave, not shape.
SHARE = 0.15
#: The opening that takes the weave's last hairs off, in pixels of the enlarged plate.
OPEN = 2 * SCALE - 1
#: Air round the trimmed shape.
PAD = 2
#: How dark the shape's own darkest fold is allowed to be drawn. Below this it stops reading as
#: one thing lit from somewhere and starts reading as two.
FLOOR_TONE = 0.42


def label(mask: np.ndarray) -> tuple[np.ndarray, int]:
    """The 4-connected patches of `mask`, numbered from 1."""
    labels = np.zeros(mask.shape, np.int32)
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
    return labels, current


def fill_small_holes(mask: np.ndarray) -> np.ndarray:
    """Closes the gaps inside the shape, and leaves the big ones: a ring keeps its middle."""
    height, width = mask.shape
    back = ~mask
    seen = np.zeros_like(back)
    stack = []
    for y in range(height):
        for x in range(width):
            if (y in (0, height - 1) or x in (0, width - 1)) and back[y, x] and not seen[y, x]:
                seen[y, x] = True
                stack.append((y, x))
    while stack:
        y, x = stack.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < height and 0 <= nx < width and back[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True
                stack.append((ny, nx))
    holes = back & ~seen
    labels, count = label(holes)
    if count:
        sizes = np.bincount(labels.ravel(), minlength=count + 1)
        small = np.flatnonzero(sizes <= max(mask.sum() * 0.06, 4.0))
        holes = np.isin(labels, small[small > 0])
    return mask | holes


def morph(mask: np.ndarray, size: int, grow: bool) -> np.ndarray:
    """A square dilation or erosion, `size` across."""
    kernel = ImageFilter.MaxFilter(size) if grow else ImageFilter.MinFilter(size)
    return np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(kernel)) > 127


def plates() -> dict[tuple[int, int], list[tuple[Path, np.ndarray]]]:
    """Every plate's inside, in grey, grouped by the size it shares with its fellows."""
    groups: dict[tuple[int, int], list[tuple[Path, np.ndarray]]] = {}
    for path in sorted(ASSETS.glob("win_slot_*.png")):
        image = Image.open(path).convert("RGB")
        lum = np.asarray(image).astype(float).mean(axis=2)
        groups.setdefault(image.size, []).append((path, lum[RIM:-RIM, RIM:-RIM]))
    return groups


def folds(plate: np.ndarray) -> list[np.ndarray]:
    """The plate laid over itself every way its weave is symmetric."""
    turns = range(4) if plate.shape[0] == plate.shape[1] else (0, 2)
    return [m for turn in turns for m in (np.rot90(plate, turn), np.rot90(plate, turn)[:, ::-1])]


def weave(group: list[tuple[Path, np.ndarray]]) -> np.ndarray:
    """The leather under the paintings: the stack's darkest, repaired from the folds where the
    stack has an object in it."""
    stacked = np.stack([plate for _, plate in group]).min(axis=0)
    folded = np.stack([m for _, plate in group for m in folds(plate)]).min(axis=0)
    return np.where(stacked - folded > TAU, folded, stacked)


def cut(path: Path, plate: np.ndarray, leather: np.ndarray) -> Path:
    shape = np.clip(plate - leather, 0.0, None)
    big = Image.fromarray(np.clip(shape, 0, 255).astype(np.uint8)).resize(
        (shape.shape[1] * SCALE, shape.shape[0] * SCALE), Image.LANCZOS)
    light = np.asarray(big).astype(float)
    light = np.clip(light / max(float(np.percentile(light, 99.5)), 1.0), 0.0, 1.0)

    # What the subtraction left behind, read where nothing is ever painted: the band round the
    # edge of the plate. A plate whose weave came off cleanly gets a floor at LOW and keeps its
    # faintest paint; the ring's, whose leather is a sunburst and whose group is two plates
    # deep, gets a higher one and keeps its ring.
    band = SCALE * 4
    edge = np.concatenate([light[:band].ravel(), light[-band:].ravel(),
                           light[band:-band, :band].ravel(), light[band:-band, -band:].ravel()])
    floor = max(LOW, EDGE * float(np.percentile(edge, 98)))

    standing = light > floor
    seed = morph(morph(standing, OPEN, False), OPEN, True)
    labels, count = label(seed)
    bright = np.unique(labels[light > HIGH])
    bright = bright[bright > 0]
    if not len(bright):
        raise SystemExit(f"no shape found in {path.name}")
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    kept = [i for i in bright if sizes[i] >= sizes[bright].max() * SHARE]
    # Grown back to the edge the opening took off, but no further than the paint itself goes.
    body = morph(np.isin(labels, kept), OPEN, True) & standing
    # And then out along the shape's own dim paint, a pixel at a time and not far: the ring's
    # band turns away from the light on its lower left and falls under any floor high enough to
    # keep the sunburst out, so it came out a hook. Growth that has to start on the shape and
    # stay next to it can follow the band round without the leather coming with it.
    dim = light > floor * DIM
    for _ in range(SCALE):
        body |= morph(body, 3, True) & dim
    body = fill_small_holes(body)
    # The outline smoothed before it is written: blurred and cut again at a half, which is a
    # contour that has lost everything thinner than the blur. What that takes off is the fur the
    # growth leaves behind -- a weave hair a pixel or two wide, standing off the shape -- and
    # what it keeps is every bay and spur of MU's own drawing.
    body = np.asarray(Image.fromarray((body * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(SCALE * 0.55))) > 127

    alpha = np.asarray(Image.fromarray((body * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(SCALE * 0.35))).astype(float) / 255.0
    alpha = np.clip((alpha - 0.2) / 0.6, 0.0, 1.0)
    tone = FLOOR_TONE + (1.0 - FLOOR_TONE) * np.clip(
        light / max(float(np.percentile(light[body], 92)), 0.05), 0.0, 1.0)

    ys, xs = np.where(alpha > 0.004)
    y0, y1 = max(0, ys.min() - PAD), min(alpha.shape[0], ys.max() + 1 + PAD)
    x0, x1 = max(0, xs.min() - PAD), min(alpha.shape[1], xs.max() + 1 + PAD)
    trimmed, shaded = alpha[y0:y1, x0:x1], tone[y0:y1, x0:x1]
    out = np.zeros(trimmed.shape + (4,), dtype=np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = (shaded * 255).astype(np.uint8)
    out[..., 3] = (trimmed * 255).astype(np.uint8)
    target = path.with_name(path.name.replace("win_slot_", "win_ghost_"))
    Image.fromarray(written(out), "RGBA").save(target)
    return target


def written(image: np.ndarray) -> np.ndarray:
    """Down to the size it is drawn at, and sharpened there.

    The shape is found at three times the plate because a threshold on a coarse grid gives a
    staircase, but three times the plate is twice the cell: at 1080p a worn slot is about 62x72
    pixels and the helm was 144x186. The window samples its art once per pixel with no mip chain,
    so a picture at twice the size it is drawn is not detail, it is every other texel thrown away
    -- which is the blur. The user, 2026-09-23: *"they look blurry"*. So the cut is taken at
    SCALE and written at OUT, by a filter that reads every texel on the way down, and given back
    the edge the two resamples cost it.
    """
    rgba = image.astype(float) / 255.0
    lit = rgba[..., :3] * rgba[..., 3:4]  # through the resize premultiplied, or the trim's
    height = max(1, int(round(image.shape[0] * OUT / SCALE)))  # transparent black bleeds in
    width = max(1, int(round(image.shape[1] * OUT / SCALE)))
    small = np.stack([
        np.asarray(Image.fromarray((plane * 255).astype(np.uint8)).resize(
            (width, height), Image.LANCZOS)).astype(float) / 255.0
        for plane in (lit[..., 0], rgba[..., 3])
    ])
    alpha = np.clip(small[1], 0.0, 1.0)
    tone = np.where(alpha > 0.004, np.clip(small[0] / np.maximum(alpha, 0.004), 0.0, 1.0), 0.0)
    sharp = np.asarray(Image.fromarray((tone * 255).astype(np.uint8)).filter(
        ImageFilter.UnsharpMask(radius=1.2, percent=70, threshold=2))).astype(float) / 255.0
    out = np.zeros(alpha.shape + (4,), dtype=np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = (sharp * 255).astype(np.uint8)
    out[..., 3] = (alpha * 255).astype(np.uint8)
    return out


def main() -> None:
    wanted = sys.argv[1:]
    # Whole groups always, whatever was asked for: the leather is read off the plates that share
    # a size, so a single slot cannot be cut on its own.
    for group in plates().values():
        leather = weave(group)
        for path, plate in group:
            slot = path.stem[len("win_slot_"):]
            if wanted and slot not in wanted:
                continue
            target = cut(path, plate, leather)
            with Image.open(target) as ghost:
                print(f"  ghost      {slot:<13} {ghost.width}x{ghost.height}"
                      f" -> {target.relative_to(PROJECT)}")


if __name__ == "__main__":
    main()
