"""Enlarges MU's sheet to HD without letting one part of it bleed into the next.

    python3 pipeline/enlarge.py \
        source/textures/sword02_npc.png source/items/Sword01/Sword01.json \
        workshop/items/Sword01/sword02_hd.png 16

The atlas an item is baked into is 2048 square and MU's sheet for that item is 64x16. The
magnification is somewhere around thirty times, and what fills the gap is the whole
question of texture quality here. Three answers were compared at that magnification, on
the blade, by looking:

*Nearest* keeps every source texel exactly and turns them into squares. It reads as the
most faithful choice and is not one: MU's art is a photographic JPEG, already soft, with no
hard edges anywhere in it. Blowing it up as blocks does not preserve edges, it *invents*
them — thirty-pixel steps that exist nowhere in the painting.

*Bilinear* removes the steps and most of the blade with them. The highlight that runs along
the edge is the one piece of structure the art has, and it goes to mush.

*Lanczos* keeps it. A small blurry image is a band-limited signal, and a windowed-sinc is
the reconstruction of that signal rather than a guess at what a sharper one would look
like. Nothing is added — no invented facets, no studs, none of what the neural upscale
put there. It is what the art already says, resampled.

*So why is this not one call to `resize`.* Because MU packs several parts of an item side
by side on one sheet, and a wide kernel does not know they are different things. Sword01's
guard occupies 2.4 pixels of the sheet's 64 — the columns between the blade and the grip —
and a Lanczos tap that reaches three pixels either way would replace the guard with a
blend of its neighbours and put brass into the end of the blade.

So the regions are enlarged separately and composited. Which regions those are is not
guessed: the asset's own file records, for every island, the box on MU's sheet it
samples, because islands.py measured it. Regions that touch are merged and each is
enlarged clamped to its own edges, so a tap near a boundary reads more of its own art
rather than any of its neighbour's.
"""

import json
import sys
from pathlib import Path

from PIL import Image

#: Pixels of margin added around each region before it is enlarged, so the filter has real
#: art to reach into at the edges rather than clamping against a hard border. Kept small:
#: on a 64-wide sheet, one pixel is a long way.
CONTEXT = 1


def regions_from(assignment: Path, size: tuple[int, int]) -> list[tuple[int, int, int, int]]:
    """The boxes on MU's sheet that this item actually samples, merged where they touch.

    Returned in pixels rather than in UV, and grown outward to whole pixels: a box that
    covers four fifths of a texel still needs all of it, because the bake will sample that
    texel and half of it being right is a seam.
    """
    width, height = size
    document = json.loads(assignment.read_text())

    boxes = []
    for island in document.get("islands", []):
        u0, v0, u1, v1 = island["mu_uv"]

        # MU's V runs the other way from the image's rows, which is the flip every step in
        # this pipeline has to make somewhere. Here rather than later, so what comes back
        # is an image box and can be handed straight to crop.
        boxes.append((
            int(u0 * width),
            int((1.0 - v1) * height),
            min(width, int(u1 * width) + 1),
            min(height, int((1.0 - v0) * height) + 1),
        ))

    # Merged until nothing overlaps. Two islands that share a piece of the sheet — the two
    # faces of a blade always do — have to be enlarged as one region, or the seam between
    # them is a seam between two separate resamplings of the same art.
    merged: list[list[int]] = []
    for box in boxes:
        current = list(box)
        again = True

        while again:
            again = False
            for other in merged[:]:
                if (current[0] < other[2] and other[0] < current[2]
                        and current[1] < other[3] and other[1] < current[3]):
                    current = [
                        min(current[0], other[0]), min(current[1], other[1]),
                        max(current[2], other[2]), max(current[3], other[3])]

                    merged.remove(other)
                    again = True

        merged.append(current)

    return [tuple(box) for box in merged]


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 enlarge.py <sheet.png> <materials.json> [out.png] [factor]")
        return

    source = Path(sys.argv[1])
    assignment = Path(sys.argv[2])
    destination = (
        Path(sys.argv[3]) if len(sys.argv) > 3
        else source.with_name(source.stem + "_hd.png"))

    factor = int(sys.argv[4]) if len(sys.argv) > 4 else 16

    sheet = Image.open(source).convert("RGBA")
    width, height = sheet.size

    # Everything starts as the blunt enlargement. The parts of the sheet no island samples
    # are never read by anything, and a nearest fill there is honest about that — it is the
    # source, at size, with nothing done to it.
    out = sheet.resize((width * factor, height * factor), Image.NEAREST)

    regions = regions_from(assignment, sheet.size)

    print(f"\n=== {source.name} -> {destination.name} ===")
    print(f"  source     {width} x {height}")
    print(f"  factor     {factor}x   ->   {width * factor} x {height * factor}")
    print(f"  regions    {len(regions)} sampled by this item, enlarged separately")

    for left, top, right, bottom in regions:
        # A region can arrive empty, and on world art it does.
        #
        # These are worked out from where the model's UVs land on the sheet, and an item's
        # UVs stay inside the square. A tiling one does not: Tree06's canopy cards run past
        # 1.0, so a region came back with its top below its bottom once clamped to the sheet,
        # and PIL refuses that crop rather than returning nothing. Skipped, because a region
        # with no area is not a region and there is nothing to enlarge.
        if right <= left or bottom <= top:
            continue

        # Enlarged with a pixel of the neighbouring art included so the filter has
        # something real at the edge, then trimmed back to the region itself. The context
        # is thrown away rather than pasted: it belongs to whatever is next door.
        outer = (
            max(0, left - CONTEXT), max(0, top - CONTEXT),
            min(width, right + CONTEXT), min(height, bottom + CONTEXT))

        if outer[2] <= outer[0] or outer[3] <= outer[1]:
            continue

        patch = sheet.crop(outer).resize(
            ((outer[2] - outer[0]) * factor, (outer[3] - outer[1]) * factor),
            Image.LANCZOS)

        inner = patch.crop((
            (left - outer[0]) * factor, (top - outer[1]) * factor,
            (right - outer[0]) * factor, (bottom - outer[1]) * factor))

        out.paste(inner, (left * factor, top * factor))

        print(
            f"    x[{left:3d},{right:3d}) y[{top:3d},{bottom:3d})   "
            f"{right - left:2d} x {bottom - top:2d} px  ->  "
            f"{(right - left) * factor:4d} x {(bottom - top) * factor:3d}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    out.save(destination, "PNG", optimize=True)

    print(f"  wrote      {destination} ({destination.stat().st_size // 1024} KB)")


main()
