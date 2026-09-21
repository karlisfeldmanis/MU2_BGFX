"""Copies the frame's art out of MuDream's LegendHUD sheets, into source/interface.

    python3 pipeline/hud_cut.py [--source <MuDream>/Data/Interface]

MuDream.online is a Season 6 MuMain client, and its bottom frame is CNewUIMainFrameWindow
re-skinned. It ships two skins for it. Data/Interface/new_main_frame_window — named after
the class — is the older one, oval orbs on a gothic plate, and is not what the game shows.
Data/Interface/LegendHUD is what the game shows: two diamond gems, eleven square slots in a
row between them, two thin bars above, a hairline of experience beneath. Every one of its
files is a PNG under a three-byte XOR (see decode_texture.KEYS), which is the only
transformation here: the sheets are decoded and written as they are, at their own pixels.

Where the pieces go is hard-coded in a main.exe this project does not have, so it is
measured off UI_HUD_Base itself and carried in Hud.cs: the row is eleven boxes in two sizes,
six skill boxes 61 wide on a 65 pitch from x 290 running y 96-157 and five item boxes 48 wide
on a 51 pitch from x 681 running y 111-159; the diamonds' holes run x 125-280 and 944-1099
by y 30-160, and the two
bars sit in the rail's windows at y 72. The gem sheets are six frames across and ten down
at 152, the bars are 346 by 11 with an empty twin each, and the experience track is a
1448-by-10 rail its 1408-by-8 fill lies in. The side buttons are the one composition here:
a disc off MuDream's TopMenu sheet with its centre cleared, and MuMain's own button glyphs
cut round to sit in it — see DISC, ICONS and glyph().
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_texture import decode  # noqa: E402

PROJECT = Path(__file__).resolve().parents[1]
ASSETS = PROJECT / "source" / "interface"

#: Where the extracted client usually is. Not in the repository: it is 1.7 GB of someone
#: else's game, and this script is what turns the few files it needs into ours.
DEFAULT_SOURCE = Path.home() / "Documents" / "mu-dream-online" / "Data" / "Interface"

FOLDER = "LegendHUD"

#: The frame's art, under Hud.cs's names, and the size each is expected at — a sheet of
#: another size would be another layout, and Hud.cs's numbers would be wrong for it.
SHEETS = {
    "hud_base": ("UI_HUD_Base.pdream", (1224, 180)),
    "hud_gem_life": ("UI_HUD_LIFE.pdream", (912, 1520)),
    "hud_gem_mana": ("UI_HUD_MANA.pdream", (912, 1520)),
    "hud_bar_shield": ("UI_HUD_SD.pdream", (346, 11)),
    "hud_bar_shield_empty": ("UI_HUD_SD_Empty.pdream", (346, 11)),
    "hud_bar_ability": ("UI_HUD_AG.pdream", (346, 11)),
    "hud_bar_ability_empty": ("UI_HUD_AG_Empty.pdream", (346, 11)),
    "hud_level_track": ("ActionBarsView_I2AC.pdream", (1448, 10)),
    "hud_level_fill": ("ActionBarsView_I2B0.pdream", (1408, 8)),
    # A slot's two states, drawn over its hole: the blue frame MuDream puts on the slot just
    # pressed, and the pale sheen it puts under the pointer.
    "hud_slot_selected": ("Slot_Selected.pdream", (52, 52)),
    "hud_slot_hover": ("SkillHover.pdream", (53, 52)),
    # The skill list's own cell, which is not LegendHUD's and is why these two are the only
    # rows here that reach outside the folder.
    #
    # LegendHUD paints the frame's eleven boxes into its base plate, so it ships no empty box
    # of its own — the two rows above it are overlays, a sheen and a frame, with nothing
    # underneath. That is fine for a box the plate has already drawn and no good at all for
    # the list that opens above the plate, where there is nothing behind a cell but the world.
    #
    # MuMain's own is newui_skillbox, which CNewUISkillList::Render draws behind every cell of
    # the open list, swapping to newui_skillbox2 for the one currently in hand. MuDream ships
    # both, repainted at four times MuMain's 32x38 exactly as it repainted the skill icons —
    # see skill_icons.py, which found the same 4x on newui_skill.
    "hud_skill_box": ("../newui_skillbox.jdream", (128, 152)),
    "hud_skill_box_chosen": ("../newui_skillbox2.jdream", (128, 152)),
    # ---- the chat window, which is MuMain's own and not LegendHUD's -----------------------
    #
    # The log box above the frame's left shoulder and the bar of buttons under it:
    # CNewUIChatLogWindow and CNewUIChatInputBox. MuDream did not repaint these — they are the
    # stock newui set it still ships, in the Interface root beside the skill boxes above, which
    # is why every row here reaches out of the folder too.
    #
    # Each size below is the C++ constant it answers to, and that is the check worth having:
    # newui_chat_back is exactly CHATBOX_WIDTH by CHATBOX_HEIGHT, the stretch bar is exactly
    # RESIZING_BTN_WIDTH by RESIZING_BTN_HEIGHT, and every button is BUTTON_WIDTH by
    # BUTTON_HEIGHT with the two-state ones stacked. So a sheet of the wrong size is a sheet
    # from a different client, and the layout in Log.cs would be wrong for it.
    #
    # The scrollbar is the exception and is oversampled: its pieces are drawn at 7 wide and 3,
    # 15 or 30 tall, and ship at four times that. Cut as they are and scaled down where they
    # are drawn, which is what the frame does with every other 4x sheet here.
    "chat_back": ("../newui_chat_back.jdream", (281, 47)),
    "chat_grip": ("../newui_Scrollbar_stretch.jdream", (281, 10)),
    "chat_scroll_top": ("../newui_scrollbar_up.tdream", (28, 12)),
    "chat_scroll_middle": ("../newui_scrollbar_m.tdream", (28, 60)),
    "chat_scroll_bottom": ("../newui_scrollbar_down.tdream", (28, 12)),
    "chat_thumb": ("../newui_scroll_on.tdream", (60, 120)),
    # The lit state of each button, drawn over the one the plate has already painted dark.
    # The four send channels come first and in this order, because RenderButtons addresses
    # them as IMAGE_INPUTBOX_NORMAL_ON + m_iInputMsgType and the order is the arithmetic.
    "chat_on_normal": ("../newui_chat_normal_on.jdream", (27, 26)),
    "chat_on_party": ("../newui_chat_party_on.jdream", (27, 26)),
    "chat_on_guild": ("../newui_chat_guild_on.jdream", (27, 26)),
    "chat_on_gens": ("../newui_chat_gens_on.jdream", (27, 26)),
    "chat_on_whisper": ("../newui_chat_whisper_on.jdream", (27, 26)),
    "chat_on_system": ("../newui_chat_system_on.jdream", (27, 26)),
    "chat_on_log": ("../newui_chat_chat_on.jdream", (27, 26)),
    "chat_on_frame": ("../newui_chat_frame_on.jdream", (27, 26)),
    # These two are buttons in their own right rather than overlays — CUIButton with two
    # states stacked, off above on — so they are drawn instead of the plate's, not over it.
    "chat_btn_size": ("../newui_chat_btn_size.jdream", (27, 52)),
    "chat_btn_alpha": ("../newui_chat_btn_alpha.jdream", (27, 52)),
}

#: Sheets whose red and blue channels are swapped on the way out: blue glow to gold.
#:
#: newui_skillbox2 is the plain box with a blue light run round its frame - (39, 145, 231)
#: at the top edge, so blue over green over red. This frame is gold: LegendHUD's own
#: in-hand box is painted (222, 166, 74) and the plate carries that gold round the slot the
#: skill in hand sits in, so a blue cell in the list above it would be the one lit thing on
#: the frame in a colour the frame does not use.
#:
#: Swapping the two channels is the whole transform. It takes the glow to (231, 145, 39),
#: which is within a few levels of LegendHUD's gold and the same order - red over green
#: over blue. The box's own steel is untouched, being neutral, and a swap does nothing to
#: r == g == b. Its interior is not neutral and does turn with the frame: the blue box is
#: tinted (14, 28, 39) inside and the gold one comes out (39, 28, 14), a warm well under a
#: warm frame, which is the right way for it to go.
#:
#: A hand-tuned curve would match LegendHUD's gold more closely and would be a set of
#: numbers nobody could check. This one is reversible and is the whole of what was done.
GOLDEN = {"hud_skill_box_chosen"}

#: The side buttons' disc: one of the round buttons on GFx/TopMenu_I1.dds — the plain grey
#: one, fifth in the top row — with the glyph it carries cleared out of its centre, leaving
#: the metal ring and a dark well for one of MuMain's icons to sit in. (left, top, right,
#: bottom) of the disc's cell; the ring is what lies beyond RING of its radius.
DISC = ("../GFx/TopMenu_I1.dds", (1024, 256), (726, 0, 761, 32), "hud_disc")
RING = 0.66

#: MuMain's icons, from the stock newui set MuDream still ships (paths relative to
#: Data/Interface): the main frame's character and inventory buttons — four 42-tall states
#: on a 168 sheet, in RegisterButtonState's order: closed, closed and hovered, open, open
#: and hovered — the chat window's bubble, and its size toggle's lined glyph, both two
#: states of 26, off then on. Each is written with its states stacked in the order kept,
#: and Hud.cs reads the count off the sheet's height.
#: The last field is the glyph's own box within each kept state, (left, top, right,
#: bottom), one per state: each of these sheets paints its glyph on a square metal button,
#: and a square button inside a round one is two buttons. The boxes differ by a pixel or
#: two between states because MuMain draws a pressed or open button with its glyph nudged
#: down and right — measured off the sheets — and cutting every state at one box put the
#: nudge on the disc. The box is cut round — see glyph().
ICONS = {
    "hud_button_character": ("../newui_menu_Bt01.jdream", (38, 168), 4, (0, 1, 2, 3),
                             ((6, 7, 32, 35), (6, 7, 32, 35), (7, 9, 33, 37), (7, 9, 33, 37))),
    "hud_button_inventory": ("../newui_menu_Bt02.jdream", (38, 168), 4, (0, 1, 2, 3),
                             ((6, 7, 32, 35), (6, 7, 32, 35), (7, 9, 33, 37), (7, 9, 33, 37))),
    "hud_button_chat": ("../newui_Bt_Chat_normal.jdream", (27, 52), 2, (0, 1),
                        ((6, 5, 21, 21), (7, 6, 22, 22))),
    "hud_button_menu": ("../newui_chat_btn_size.jdream", (27, 52), 2, (1, 0),
                        ((7, 6, 22, 22), (6, 5, 21, 21))),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--out", type=Path, default=ASSETS)
    args = parser.parse_args()

    source: Path = args.source / FOLDER
    out: Path = args.out

    if not source.is_dir():
        raise SystemExit(f"error: no {FOLDER} under {args.source}; pass --source")

    out.mkdir(parents=True, exist_ok=True)

    for name, (file, size) in SHEETS.items():
        image = decode(source / file).convert("RGBA")

        if image.size != size:
            raise SystemExit(f"error: {file} is {image.size}, not the {size} Hud.cs is measured against")

        if name in GOLDEN:
            image = golden(image)

        image.save(out / f"{name}.png", "PNG", optimize=True)
        print(f"{name:22s} {image.width:4d} x {image.height:<4d}  {FOLDER}/{file}"
              f"{'  (blue swapped to gold)' if name in GOLDEN else ''}")

    file, size, box, name = DISC
    sheet = decode(source / file).convert("RGBA")

    if sheet.size != size:
        raise SystemExit(f"error: {file} is {sheet.size}, not the {size} the disc is cut from")

    disc = cleared(squared(sheet.crop(box)))
    disc.save(out / f"{name}.png", "PNG", optimize=True)
    print(f"{name:22s} {disc.width:4d} x {disc.height:<4d}  {file} at {box}, squared and centre cleared")

    for name, (file, size, states, keep, boxes) in ICONS.items():
        strip = decode(source / file).convert("RGBA")

        if strip.size != size:
            raise SystemExit(f"error: {file} is {strip.size}, not the {size} its states are cut from")

        tall = size[1] // states
        wide, high = boxes[0][2] - boxes[0][0], boxes[0][3] - boxes[0][1]
        stack = Image.new("RGBA", (wide, high * len(keep)))

        for row, (state, box) in enumerate(zip(keep, boxes)):
            if (box[2] - box[0], box[3] - box[1]) != (wide, high):
                raise SystemExit(f"error: {name}'s state boxes are not all {wide} by {high}")

            cell = strip.crop((box[0], (state * tall) + box[1], box[2], (state * tall) + box[3]))
            stack.paste(glyph(cell), (0, row * high))

        stack.save(out / f"{name}.png", "PNG", optimize=True)
        print(f"{name:22s} {stack.width:4d} x {stack.height:<4d}  {file}, states {keep} of {states}, glyph {boxes[0]}")


def golden(image: Image.Image) -> Image.Image:
    """The same image with red and blue exchanged. See GOLDEN."""
    red, green, blue, alpha = image.split()
    return Image.merge("RGBA", (blue, green, red, alpha))


def squared(disc: Image.Image) -> Image.Image:
    """The disc on a square canvas, its own bounds centred.

    The sheet's disc is 35 wide and 32 tall — round, with its cell's spare rows below it —
    and a round thing drawn into a square from a cell that is not square lands off centre:
    the well is cleared round one middle and the glyph is set on another. So the disc's
    opaque bounds are found and set in the middle of a square, and every later measure is
    taken from that square's centre.
    """
    left, top, right, bottom = disc.getbbox() or (0, 0, disc.width, disc.height)
    tight = disc.crop((left, top, right, bottom))
    side = max(tight.width, tight.height)

    square = Image.new("RGBA", (side, side))
    square.paste(tight, ((side - tight.width) // 2, (side - tight.height) // 2))
    return square


def cleared(disc: Image.Image) -> Image.Image:
    """The disc with its glyph gone: the ring kept, the well inside refilled with its own dark.

    The well's colour is read off the disc itself, just inside the ring, so the fill is the
    metal's own shadow and not a colour chosen here; it darkens a little toward the middle,
    which is what a dished button does under a light from above. The whole disc is then
    taken to grey: the TopMenu sheet is warm brown bronze and everything else on this frame
    is cold steel, and a bronze button on a steel plate is from another game.
    """
    pixels = disc.load()
    width, height = disc.size
    cx, cy = (width - 1) / 2, (height - 1) / 2
    radius = min(cx, cy)

    # The colour just inside the ring, averaged round it.
    samples = []
    for y in range(height):
        for x in range(width):
            distance = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / radius
            if RING - 0.06 <= distance <= RING and pixels[x, y][3] > 200:
                samples.append(pixels[x, y][:3])

    if not samples:
        raise SystemExit("error: the disc's ring was not found; check DISC's cell")

    well = tuple(sum(channel) // len(samples) for channel in zip(*samples))

    for y in range(height):
        for x in range(width):
            distance = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / radius
            if distance < RING:
                shade = 0.82 + (0.18 * distance / RING)
                pixels[x, y] = (*(int(channel * shade) for channel in well), 255)

            r, g, b, a = pixels[x, y]
            grey = int((0.299 * r) + (0.587 * g) + (0.114 * b))
            pixels[x, y] = (grey, grey, int(min(255, grey * 1.06)), a)

    return disc


def glyph(cell: Image.Image) -> Image.Image:
    """A button's glyph as a round inset: the square button's face cut to a circle.

    Keying the metal out was tried and lost the glyphs that are grey at rest — the main
    frame's character and inventory buttons are steel until their window opens and they turn
    gold. So the face is kept, metal and all, and cut round: inside the disc's well it reads
    as a steel inset with the glyph on it, which is what MuMain's square buttons are. The
    edge is feathered over a pixel so it does not stair-step against the well.
    """
    out = cell.copy()
    pixels = out.load()
    cx, cy = (out.width - 1) / 2, (out.height - 1) / 2
    radius = min(cx, cy) + 0.5

    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = pixels[x, y]
            distance = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            keep = max(0.0, min(1.0, radius - distance))
            pixels[x, y] = (r, g, b, int(a * keep))

    return out


if __name__ == "__main__":
    main()
