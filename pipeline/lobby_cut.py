"""Copies the character screen's art out of MuDream, into source/interface.

    python3 pipeline/lobby_cut.py [--source <MuDream>/Data/Interface]

MuMain's character scene loads eight files by name in OpenCharacterSceneData
(Engine/Object/ZzzOpenData.cpp:5082), and MuDream ships all eight under the same names as
.tdream, which is a TGA under a three-byte XOR (see decode_texture.KEYS). That undoing is the
only transformation here: each is decoded and written as it is, at its own pixels, under a
lobby_ name. See docs/character-select.md, and Lobby.cs for where each is drawn.

What each is, measured off the decoded file:

- cha_id, 346 by 38: the strip along the bottom that the four buttons sit on. The fork's
  CSMW_SPR_INFO is a black rectangle at alpha 143 stretched to the screen; this is the
  later, drawn one.
- cha_bt, 108 by 104: four states of the create window's class button, 26 tall each, with no
  text baked in - the class name is written over it.
- b_create, b_connect, b_delete, 54 by 120: four states of a 54 by 30 button each, up,
  over, down and off, with the word baked in. server_menu_b_all is the Menu button, 54 by
  90, three states.
- character_ex, 118 by 54: the name plate's balloon.
- deco, 189 by 103: the ornament in the bar's corner, CSMW_SPR_DECO.

The pick's three effect sheets are not interface art and do not come from MuDream: two of
them are not in it at all. They are taken from MuMain's own Data, which is in the repository
under LEGACY/reference - see --mu.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_texture import decode  # noqa: E402

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = PROJECT / "source" / "interface"

#: Where the extracted client usually is. Not in the repository; see hud_cut.py.
DEFAULT_SOURCE = Path.home() / "Documents" / "mu-dream-online" / "Data" / "Interface"

#: The screen's art, under Lobby.cs's names, and the size each is expected at.
SHEETS = {
    "lobby_strip": ("cha_id.tdream", (346, 38)),
    "lobby_class": ("cha_bt.tdream", (108, 104)),
    "lobby_create": ("b_create.tdream", (54, 120)),
    "lobby_menu": ("server_menu_b_all.tdream", (54, 90)),
    "lobby_connect": ("b_connect.tdream", (54, 120)),
    "lobby_delete": ("b_delete.tdream", (54, 120)),
    "lobby_balloon": ("character_ex.tdream", (118, 54)),
    "lobby_deco": ("deco.tdream", (189, 103)),

    # The message box and the create window's two buttons, loaded by OpenInterface
    # (ZzzOpenData.cpp:5431): BITMAP_MESSAGE_WIN, BITMAP_MSG_WIN_INPUT and BITMAP_BUTTON + 0
    # and + 1. The buttons are three states of 54 by 30; the box is drawn whole.
    "lobby_msg_back": ("message_back.tdream", (352, 113)),
    "lobby_msg_field": ("delete_secret_number.tdream", (171, 23)),
    "lobby_ok": ("message_ok_b_all.tdream", (54, 90)),
    "lobby_cancel": ("loding_cancel_b_all.tdream", (54, 90)),
}

#: Where MuMain's own Data is, which is where the pick's three effect sheets come from.
DEFAULT_MU = PROJECT / "reference" / "MuMain" / "src" / "bin" / "Data"

#: What the picked figure stands in, by the bitmap the client names.
#:
#: All three are plain OZJs and none is interface art, so they are written beside the other
#: effects rather than under a lobby_ name. RenderSelectedCharacterEffects draws the first as
#: two counter-rotating discs at its feet and throws the other two off it as particles, four
#: and five (CharacterScene.cpp:317-347, ZzzEffectParticle.cpp:122).
EFFECTS = {
    # BITMAP_GM_AURORA. The runic disc, 128 square, white on black: the two rings under the
    # pick. ZzzOpenData.cpp:5247. An earlier pass drew the move marker's ring here.
    "gmmzine": ("Skill/gmmzine.OZJ", (128, 128), "effects/lobby/gmmzine.png"),

    # BITMAP_EFFECT. A soft blue smudge, 16 by 128 - the type-4 blob, which hangs where it is
    # spawned. ZzzOpenData.cpp:5094. Not in MuDream, and an earlier pass had the spark below
    # standing in for it.
    "chasellight": ("Logo/chasellight.OZJ", (16, 128), "effects/lobby/chasellight.png"),

    # BITMAP_EXT_LOG_IN + 2. A star flare, 64 square - the type-5 particle, which rises.
    # ZzzOpenData.cpp:5093.
    "impack03": ("Effect/Impack03.OZJ", (64, 64), "effects/lobby/impack03.png"),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--mu", type=Path, default=DEFAULT_MU)
    parser.add_argument("--out", type=Path, default=ASSETS)
    args = parser.parse_args()

    if not args.source.is_dir():
        raise SystemExit(f"error: no such directory {args.source}; pass --source")

    args.out.mkdir(parents=True, exist_ok=True)

    for name, (file, size) in SHEETS.items():
        image = decode(args.source / file).convert("RGBA")

        if image.size != size:
            raise SystemExit(f"error: {file} is {image.size}, not the {size} Lobby.cs is measured against")

        image.save(args.out / f"{name}.png", "PNG", optimize=True)
        print(f"{name:16s} {image.width:4d} x {image.height:<4d}  {file}")

    if not args.mu.is_dir():
        raise SystemExit(f"error: no such directory {args.mu}; pass --mu")

    for name, (file, size, into) in EFFECTS.items():
        sheet = decode(args.mu / file).convert("RGBA")

        if sheet.size != size:
            raise SystemExit(f"error: {file} is {sheet.size}, not the {size} it is drawn at")

        target = args.out.parent / into
        target.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(target, "PNG", optimize=True)
        print(f"{name:16s} {sheet.width:4d} x {sheet.height:<4d}  {file}")


if __name__ == "__main__":
    main()
