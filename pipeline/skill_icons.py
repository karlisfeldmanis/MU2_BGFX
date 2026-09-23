"""Cuts one icon per 0.75 skill out of MuDream's skill sheet, into source/interface/skills.

    python3 pipeline/skill_icons.py [--source <MuDream>/Data/Interface]

Where a skill's icon lives is not data in MuMain; it is arithmetic in
CNewUISkillList::RenderSkillIcon (UI/NewUI/HUD/NewUIMainFrameWindow.cpp). Every skill numbered
below Spiral Slash (57) whose SkillUseType is not 4 falls through to the last branch there:

    fU = ((type - 1) % 8) * 20 / 256,   fV = ((type - 1) / 8) * 28 / 256,   image newui_skill.jpg

so the sheet is a grid of 20-by-28 cells, eight to a row, and skill number N sits in cell N-1,
counting across then down. Skill.bmd's Magic_Icon column is only read for master skills and
use-type 4; none of 0.75's skills are either. All of 0.75's skills are in that range, which is
why this script needs one sheet and no table of cells — the table below is names, not geometry.

MuDream ships newui_skill.jdream at 1024 by 1024, four times MuMain's 256, and it is a
repaint rather than a blow-up: the fireball on it is a different painting from the one on the
512-pixel DIV_Skill01.ozj in new_main_frame_window, and much sharper. So the cell is 80 by 112,
and that is the size each icon is written at, untouched. The file is a JPEG under a three-byte
XOR, which decode_texture undoes; the sheet has no alpha, the icons are on their own dark
panels.

Which skills 0.75 has is OpenMU's Version075/SkillsInitializer.cs: the wizard's 1-12 and Energy
Ball 17, the knight's Defense 18 and 19-23, the elf's Triple Shot 24, Heal 26, the two Greaters
27-28 and the six summons 30-35. Flame of Evil 50 is a monster's and has no cell. Cometfall 13,
Inferno 14, Ice Arrow 51 and Penetration 52 arrive in 0.95d (Version095d/SkillsInitializer.cs),
not 0.75, and are not cut: a catalogue that holds a skill this game does not have is a
catalogue that will one day hand it out.

Three past 0.75 ARE cut, on 2026-09-23, and they are the exception that proves that rule — the
game now has them. Twisting Slash 41 (0.95d), Rageful Blow 42 and Death Stab 43 (Season 6) are
the knight's own, and they exist here because his skills are gated on the weapon family
(docs/skills-dk.md §3.1b): gating 0.75's five leaves a one-handed axe, a mace and a spear with
one key each. Nothing else on the sheet is cut speculatively.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_texture import decode  # noqa: E402

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = PROJECT / "source" / "interface" / "skills"

#: Where the extracted client usually is. Not in the repository: it is 1.7 GB of someone
#: else's game, and this script is what turns the one file it needs into ours.
DEFAULT_SOURCE = (
    Path.home()
    / "Library/Containers/com.Ababe.UnzipAnyFile/Data/Library/Application Support"
    / "AFBFEF0915FC2AE3A3D15501B15A096D/Mudream.online/Data/Interface"
)

SHEET = "newui_skill.jdream"
SHEET_SIZE = (1024, 1024)

#: RenderSkillIcon's grid, at MuDream's four times: 20 by 28 of 256 is 80 by 112 of 1024.
CELL = (80, 112)
PER_ROW = 8

#: Skill number -> (class, name), 0.75's set, per OpenMU's Version075 and MuMain's _enum.h.
SKILLS = {
    1: ("Dark Wizard", "Poison"),
    2: ("Dark Wizard", "Meteorite"),
    3: ("Dark Wizard", "Lightning"),
    4: ("Dark Wizard", "Fire Ball"),
    5: ("Dark Wizard", "Flame"),
    6: ("Dark Wizard", "Teleport"),
    7: ("Dark Wizard", "Ice"),
    8: ("Dark Wizard", "Twister"),
    9: ("Dark Wizard", "Evil Spirit"),
    10: ("Dark Wizard", "Hellfire"),
    11: ("Dark Wizard", "Power Wave"),
    12: ("Dark Wizard", "Aqua Beam"),
    17: ("Dark Wizard", "Energy Ball"),
    18: ("Dark Knight", "Defense"),
    19: ("Dark Knight", "Falling Slash"),
    20: ("Dark Knight", "Lunge"),
    21: ("Dark Knight", "Uppercut"),
    22: ("Dark Knight", "Cyclone"),
    23: ("Dark Knight", "Slash"),
    24: ("Fairy Elf", "Triple Shot"),
    26: ("Fairy Elf", "Heal"),
    27: ("Fairy Elf", "Greater Defense"),
    28: ("Fairy Elf", "Greater Damage"),
    30: ("Fairy Elf", "Summon Goblin"),
    31: ("Fairy Elf", "Summon Stone Golem"),
    32: ("Fairy Elf", "Summon Assassin"),
    33: ("Fairy Elf", "Summon Elite Yeti"),
    34: ("Fairy Elf", "Summon Dark Knight"),
    35: ("Fairy Elf", "Summon Bali"),
    # The three past 0.75, cut on 2026-09-23 because the knight's skills are now gated on the
    # weapon family (docs/skills-dk.md §3.1b) and gating 0.75's five leaves an axe, a mace and a
    # spear with one key each. The names and the numbers are the client's own -- `skill_eng.bmd`,
    # decoded with MU's three-byte XOR, reads 41 Twisting Slash, 42 Rageful Blow, 43 Death Stab
    # (docs/mu-scrolls-and-orbs.md §7 is the decode) -- so only WHICH HAND each one asks for is
    # ours. Twisting Slash is 0.95d's; the other two are Season 6's. This is the "should the
    # hybrid want it" case the docstring above left the arithmetic for.
    41: ("Dark Knight", "Twisting Slash"),
    42: ("Dark Knight", "Rageful Blow"),
    43: ("Dark Knight", "Death Stab"),
}


#: MuMain's own sheet, and the second way in. MuDream's 1024 repaint is the better picture and
#: stays the default, but it lives inside another application's container and macOS refuses to
#: open it from here whatever the shell is allowed (2026-09-23, cutting 41-43: `PermissionError`
#: on a plain read, with and without the sandbox). MuMain's `newui_skill.OZJ` is in this
#: repository, is the same grid, and is MU's own painting rather than a repaint of it -- it is
#: simply four times smaller, 20 by 28 a cell.
#:
#: So `--mumain` cuts from that instead, through pipeline/upscale.py at THREE times, which is
#: the whole sheet enlarged once and then cut: the model gets 256 texels of context rather than
#: a 20-pixel stamp, and three is the project's cap on an enlargement of MU's art. What comes
#: out is 60 by 84 against MuDream's 80 by 112, which the plate scales to the same box. The day
#: the container opens, re-cut these numbers without the flag and they get sharper for free.
MUMAIN_SHEET = PROJECT / "reference/MuMain/src/bin/Data/Interface/newui_skill.OZJ"
MUMAIN_SIZE = (256, 256)
MUMAIN_FACTOR = 3


def cell(number: int, size: tuple[int, int] = CELL) -> tuple[int, int, int, int]:
    """The pixel rectangle of skill `number` on the sheet, RenderSkillIcon's arithmetic."""
    index = number - 1
    x = (index % PER_ROW) * size[0]
    y = (index // PER_ROW) * size[1]
    return (x, y, x + size[0], y + size[1])


def mumain_sheet(path: Path, out: Path) -> "Image.Image":
    """MuMain's 256 sheet, enlarged three times. The enlargement is cached beside the icons."""
    import subprocess
    import tempfile

    sheet = decode(path)
    if sheet.size != MUMAIN_SIZE:
        sys.exit(f"{path.name} is {sheet.size}, expected {MUMAIN_SIZE}; the cell would be wrong")
    with tempfile.TemporaryDirectory() as folder:
        small = Path(folder) / "newui_skill.png"
        big = Path(folder) / "newui_skill_x3.png"
        sheet.convert("RGB").save(small)
        # The drawn model and eight passes: an icon is a painted object at arm's length, which
        # is upscale.py's own argument for the item settings rather than the ground's.
        subprocess.run(
            [sys.executable, str(PROJECT / "pipeline/upscale.py"), str(small), str(big),
             "0.65", str(MUMAIN_FACTOR), "edge", "drawn", "8"],
            check=True)
        return Image.open(big).convert("RGB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out", type=Path, default=ASSETS)
    parser.add_argument("--mumain", action="store_true",
                        help="cut from MuMain's own 256 sheet, enlarged 3x (see MUMAIN_SHEET)")
    parser.add_argument("--mumain-sheet", type=Path, default=MUMAIN_SHEET,
                        help="where that sheet is, if tools/fetch_mumain.sh has not been run")
    parser.add_argument("--only", type=int, nargs="+", default=[],
                        help="cut only these skill numbers, leaving every other icon alone")
    args = parser.parse_args()

    wanted = {n: SKILLS[n] for n in args.only} if args.only else SKILLS
    for number in args.only:
        if number not in SKILLS:
            sys.exit(f"skill {number} is not in this script's table")

    if args.mumain:
        args.out.mkdir(parents=True, exist_ok=True)
        size = (CELL[0] * MUMAIN_FACTOR // 4, CELL[1] * MUMAIN_FACTOR // 4)
        sheet = mumain_sheet(args.mumain_sheet, args.out)
        for number, (_, name) in sorted(wanted.items()):
            icon = sheet.crop(cell(number, size))
            target = args.out / f"skill_{number}.png"
            icon.save(target, optimize=True)
            print(f"  {target.name:<14} {name:<20} {cell(number, size)}")
        print(f"{len(wanted)} icons at {size[0]}x{size[1]} in {args.out} (MuMain, 3x)")
        return

    path = args.source / SHEET
    if not path.is_file():
        sys.exit(f"missing {path}; pass --source")
    sheet = decode(path)
    if sheet.size != SHEET_SIZE:
        sys.exit(f"{SHEET} is {sheet.size}, expected {SHEET_SIZE}; the cell would be wrong")
    sheet = sheet.convert("RGB")

    args.out.mkdir(parents=True, exist_ok=True)
    for number, (_, name) in sorted(wanted.items()):
        icon = sheet.crop(cell(number))
        target = args.out / f"skill_{number}.png"
        icon.save(target, optimize=True)
        print(f"  {target.name:<14} {name:<20} {cell(number)}")
    print(f"{len(wanted)} icons at {CELL[0]}x{CELL[1]} in {args.out}")


if __name__ == "__main__":
    main()
