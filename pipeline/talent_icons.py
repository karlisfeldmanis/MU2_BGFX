"""MU's master-tree sheets, whole, into content/interface/talents: the Talents window's node icons.

    python3 pipeline/talent_icons.py [--source <MuMain>/Data/Interface]

The Talents window (game/ui/talents.cpp, docs/plans/talents.md) draws every node as one cell of
MuMain's Season 6 master-tree sheet, new_Master_Icon.OZJ, and a node that is not open yet as the
same cell of its greyed twin, new_Master_non_Icon.OZJ -- the pair CNewUIMasterLevel draws from.
Where a skill's icon lives is arithmetic, as the buff sheet's is (buff_icons.py): the sheet is
512 by 512, twenty-five 20-by-28 cells a row, and master skill N sits in cell N - 300, counting
across then down. The table names each node's number (sim/talents_table.inc's `icon`).

**Whole sheets, not 133 cut files.** The window draws a cell as a region of the sheet
(gfx::Canvas::region), so two textures carry every node of every class, and a talent moved to
another icon on the canvas needs no cut here at all.

**MuMain's copy, at its own size.** The canvas the user picked the look on was drawn from
MuMain's sheet (its atlas matches it cell for cell; S17's third_party copy repaints some cells,
388 among them). MuDream's four-times copies could not be read from this machine, so this is
MU's painting at 1x, which the window enlarges to 2x and 2.4x on a 1080 screen: inside the 3x
the upscale rules allow, and no model's guess in it. Decoded and written untouched
(decode_texture.decode): no resize, no sharpen.

Cells with no painting (411, 463, 464, 498, 502, 524, 525, 540-550, 553, 556, 557, 561-567,
570, and 576 up) carry their number printed on grey; the table names none of them.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
# The folders are tools/paths.py's; appended, so nothing there shadows a module here.
sys.path.append(str(Path(__file__).resolve().parents[1] / "tools"))

from decode_texture import decode  # noqa: E402
from paths import P  # noqa: E402

OUT = P.CONTENT / "interface" / "talents"

#: MuMain's Data/Interface, kept in the repository's reference tree.
DEFAULT_SOURCE = P.ROOT.parent / "LEGACY/reference/MuMain/src/bin/Data/Interface"

#: index key -> MuMain's file. The keys are the files' stems, which index.py lists off the folder.
SHEETS = {
    "talent_icons": "new_Master_Icon.OZJ",
    "talent_icons_grey": "new_Master_non_Icon.OZJ",
}
SIZE = (512, 512)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for key, name in SHEETS.items():
        source = args.source / name
        if not source.exists():
            sys.exit(f"talent_icons: no {source}")
        image = decode(source).convert("RGB")
        if image.size != SIZE:
            sys.exit(f"talent_icons: {name} is {image.size}, not MuMain's {SIZE}")
        target = OUT / f"{key}.png"
        image.save(target, optimize=True)
        print(f"{name} -> {target.relative_to(P.ROOT)} ({image.size[0]}x{image.size[1]})")


if __name__ == "__main__":
    main()
