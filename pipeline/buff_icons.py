"""Cuts one icon per 0.75 buff or debuff out of MuDream's status sheet, into source/interface/buffs.

    python3 pipeline/buff_icons.py [--source <MuDream>/Data/Interface]

Like the skill icons, where a buff's icon lives is arithmetic, not data: CNewUIBuffWindow::RenderBuffIcon
(UI/NewUI/HUD/NewUIBuffWindow.cpp). Every eBuffState below Berserker (81) is drawn from
newui_statusicon.jpg as

    u = ((state - 1) % 10) * 20 / 256,   v = ((state - 1) / 10) * 28 / 256

with BUFF_IMG_WIDTH 20 and BUFF_IMG_HEIGHT 28 (the 256 is the power-of-two padding LoadBitmap gives a
200-by-224 JPEG). So the sheet is a grid of 20-by-28 cells, ten to a row, and state N sits in cell
N-1, counting across then down. The state numbers are eBuffState in Core/Globals/_enum.h, and they
are the wire numbers too: OpenMU's MagicEffectNumber has Poisoned at 0x37 = 55 where MuMain has
eDeBuff_Poison = 55, Iced at 0x38 = 56 where MuMain has eDeBuff_Freeze = 56. States 81 and up are on
newui_statusicon2 with the same arithmetic from 81, and 161 and up on statusicon3; none of 0.75's
are there.

MuDream ships newui_statusicon.jdream at 800 by 896, four times MuMain's 200 by 224, and it is a
repaint like the skill sheet (the poison skull and the frost on it are sharper paintings, not
blown-up ones). So the cell is 80 by 112, and that is the size each icon is written at, untouched.
It is a JPEG under MuDream's XOR, which decode_texture undoes; no alpha, the icons are on their own
dark panels. MuMain's own newui_statusicon.OZJ in LEGACY/build is the same grid at 200 by 224 and
is the fallback if the MuDream file is missing: same cell index, 20 by 28.

Which states 0.75 can carry is OpenMU's Version075/SkillsInitializer.cs. InitializeEffects makes
five: Defense (ShieldSkill), Greater Damage, Greater Defense, Heal and Alcohol; and CreateSkill's
elemental modifier (SkillsInitializerBase.ApplyElementalModifier) gives the wizard's Poison (1) a
Poisoned effect and Ice (7) an Iced one. FixMagicEffectNumbers then renumbers every non-negative
effect to its skill's number, so on 0.75's wire Greater Damage is 28, Greater Defense 27, Poisoned
1, Iced 7 and the knight's Defense 18; Alcohol keeps 201. Of those seven, four have a cell:

    greater_damage   eBuff_Attack     1   the elf's Greater Damage, skill 28
    greater_defense  eBuff_Defense    2   the elf's Greater Defense, skill 27
    poison           eDeBuff_Poison  55   the wizard's Poison, skill 1
    ice              eDeBuff_Freeze  56   the wizard's Ice, skill 7 (OpenMU's Iced, not Freeze)

Heal is instantaneous and never a state. The knight's Defense (18) is a stance the client shows on
the model, and MuMain has no eBuffState for it; Ale's Alcohol (201) is a screen wobble in MuMain,
with no eBuffState either. Neither has an icon anywhere, so neither is cut. And a buff strip at all
is a bench invention here: 0.75's client drew no status icons, this HUD borrows Season 3's.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_texture import decode  # noqa: E402

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = PROJECT / "source" / "interface" / "buffs"

#: Where the extracted client usually is. Not in the repository: it is 1.7 GB of someone
#: else's game, and this script is what turns the one file it needs into ours.
DEFAULT_SOURCE = (
    Path.home()
    / "Library/Containers/com.Ababe.UnzipAnyFile/Data/Library/Application Support"
    / "AFBFEF0915FC2AE3A3D15501B15A096D/Mudream.online/Data/Interface"
)

#: MuMain's own copy, the fallback, at a quarter of MuDream's size.
FALLBACK_SOURCE = PROJECT / "LEGACY/build/MuRemaster.app/Contents/Resources/Data/Interface"

#: (file, sheet size, cell size): RenderBuffIcon's 20 by 28 at MuDream's four times, or MuMain's own.
SHEETS = {
    "mudream": ("newui_statusicon.jdream", (800, 896), (80, 112)),
    "mumain": ("newui_statusicon.OZJ", (200, 224), (20, 28)),
}
PER_ROW = 10

#: key -> (eBuffState number, MuMain's enumerator, what puts it on a 0.75 character).
STATES = {
    "greater_damage": (1, "eBuff_Attack", "Greater Damage, skill 28"),
    "greater_defense": (2, "eBuff_Defense", "Greater Defense, skill 27"),
    "poison": (55, "eDeBuff_Poison", "Poison, skill 1"),
    "ice": (56, "eDeBuff_Freeze", "Ice, skill 7"),
    # The knight's Defense, and the one row here that is a borrowing rather than a lookup.
    #
    # There is no cell for it. MuMain has no eBuffState for skill 18 at all - the stance is
    # shown on the model and nowhere else - so unlike the four above, this key cannot be
    # derived from anything. What it takes instead is cell 45, whose enumerator is
    # eBuff_EliteScroll2: a Season 6 item buff with no relation to Defense whatever, chosen
    # because its painting is a plain heater shield with a cross on it and reads at 20 by 28
    # as a man raising his guard. Nothing in 0.75 uses cell 45, so nothing is displaced.
    #
    # Chosen on looks, and the alternatives are recorded so the choice can be argued with:
    # cell 2 (eBuff_Defense) is the elf's Greater Defense and already taken; cell 4
    # (eBuff_WizDefense) has the better name and is a blue magic crescent, which is a wizard's
    # barrier and not a shield; cell 14 (eBuff_CastleRegimentDefense) is crossed swords on a
    # siege banner and is busy at icon size.
    #
    # Marked as this project's. So is the strip it hangs in - see the note at the top of this
    # file, where 0.75's client draws no status icons at all.
    "defense": (45, "eBuff_EliteScroll2", "Defense, skill 18 - borrowed, see note"),
}


def cell(state: int, size: tuple[int, int]) -> tuple[int, int, int, int]:
    """The pixel rectangle of eBuffState `state` on the sheet, RenderBuffIcon's arithmetic."""
    index = state - 1
    x = (index % PER_ROW) * size[0]
    y = (index // PER_ROW) * size[1]
    return (x, y, x + size[0], y + size[1])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out", type=Path, default=ASSETS)
    args = parser.parse_args()

    name, expected, size = SHEETS["mudream"]
    path = args.source / name
    if not path.is_file():
        name, expected, size = SHEETS["mumain"]
        path = FALLBACK_SOURCE / name
        print(f"no MuDream sheet under {args.source}; falling back to MuMain's {name} at a quarter size")
        if not path.is_file():
            sys.exit(f"missing {path} too; pass --source")
    sheet = decode(path)
    if sheet.size != expected:
        sys.exit(f"{name} is {sheet.size}, expected {expected}; the cell would be wrong")
    sheet = sheet.convert("RGB")

    args.out.mkdir(parents=True, exist_ok=True)
    for key, (state, enumerator, origin) in STATES.items():
        icon = sheet.crop(cell(state, size))
        target = args.out / f"buff_{key}.png"
        icon.save(target, optimize=True)
        print(f"  {target.name:<24} {enumerator:<16} {state:>3}  {cell(state, size)}  {origin}")
    print(f"{len(STATES)} icons at {size[0]}x{size[1]} from {name} in {args.out}")


if __name__ == "__main__":
    main()
