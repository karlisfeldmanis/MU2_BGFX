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
27-28 and the six summons 30-35. Flame of Evil 50 is a monster's and has no cell. Twisting
Slash 41, Cometfall 13, Inferno 14, Ice Arrow 51 and Penetration 52 arrive in 0.95d
(Version095d/SkillsInitializer.cs), not 0.75, and are not cut. The sheet has them and cutting
them costs nothing, which is why they were here for a while; they are gone because a catalogue
that holds a skill this game does not have is a catalogue that will one day hand it out. The
cell arithmetic below is the whole recipe for getting one back, should the hybrid want it.
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
}


def cell(number: int) -> tuple[int, int, int, int]:
    """The pixel rectangle of skill `number` on the sheet, RenderSkillIcon's arithmetic."""
    index = number - 1
    x = (index % PER_ROW) * CELL[0]
    y = (index // PER_ROW) * CELL[1]
    return (x, y, x + CELL[0], y + CELL[1])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out", type=Path, default=ASSETS)
    args = parser.parse_args()

    path = args.source / SHEET
    if not path.is_file():
        sys.exit(f"missing {path}; pass --source")
    sheet = decode(path)
    if sheet.size != SHEET_SIZE:
        sys.exit(f"{SHEET} is {sheet.size}, expected {SHEET_SIZE}; the cell would be wrong")
    sheet = sheet.convert("RGB")

    args.out.mkdir(parents=True, exist_ok=True)
    for number, (_, name) in sorted(SKILLS.items()):
        icon = sheet.crop(cell(number))
        target = args.out / f"skill_{number}.png"
        icon.save(target, optimize=True)
        print(f"  {target.name:<14} {name:<20} {cell(number)}")
    print(f"{len(SKILLS)} icons at {CELL[0]}x{CELL[1]} in {args.out}")


if __name__ == "__main__":
    main()
