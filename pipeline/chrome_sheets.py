#!/usr/bin/env python3
"""The sheets a weapon's chrome passes draw with, written beside its recipe.

    pipeline/chrome_sheets.py              every weapon and monster whose recipe has a `chrome` list
    pipeline/chrome_sheets.py Sword21      one

MuMain draws some weapons again over themselves with one of its chrome images: the Knight
Blade's violet blade, the Staff of Kundun's two sheens, the Bill of Balrog's blue veins, the
Great Reign Crossbow's grooves (RenderPartObjectBody, ZzzObject.cpp:7065-7075, 7613-7624,
7989-7996, 9682-9688). A recipe says each pass under `chrome`:

    {"slot": "nks01", "image": "chrome01", "uv": "chrome", "tint": [0.43, 0.14, 0.6],
     "where": "over"}

`image` is MU's chrome01 (BITMAP_CHROME), chrome02 (BITMAP_CHROME2) or bab2 (BITMAP_CHROME + 1),
out of content/effects/refine, or shiny01 (BITMAP_SHINY, RENDER_METAL's). `own` is the slot's
own sheet, for a pass MU draws with no image bound (the Dark Horse's). `mask`, when it names one
of those, is that image as the mask -- MU's Chrome01 lightmap over a metal pass. `where` is "over" for the whole mesh, or "cut" for where its own
sheet's alpha is cut away: MU draws those passes before the body, so they only show through the
holes its alpha test leaves.

Two files a pass, in content/items/weapons/chrome/:

  * `<item>_<n>.png`, the chrome image times the tint over its largest channel -- a sheet holds
    no more than 1, and export_gltf.chrome_material carries that largest channel in the glow's
    brightness, so a tint of (0.5, 0.5, 1.5) is drawn at 1.5 and not clipped;
  * `<item>_<n>_mask.png`, where the pass shows, read in the mesh's own UVs: white for "over",
    and for "cut" the slot's sheet with its kept texels black and its cut ones white.
"""
import json
import os
import sys

from PIL import Image

import os as _os
import sys as _sys
_sys.path.append(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", "tools"))
from paths import CONTENT

WEAPONS = os.path.join(CONTENT, "items", "weapons")
# A monster's passes too (Hell Maine's red chrome over its armour and wings, GMAida.cpp:
# 1389-1405), written beside its recipe, where export_gltf looks: content/monsters/chrome/.
# And a shield's (the Crimson Glory's rim, ZzzObject.cpp:7229): content/items/shields/chrome/.
# And a mount's and its horn's (the Dark Horse's gold neck plate and the horn's orange sheen,
# ZzzObject.cpp:783-790, 7130-7135): content/items/pets/chrome/.
FOLDERS = (WEAPONS, os.path.join(CONTENT, "monsters"), os.path.join(CONTENT, "items", "shields"),
           os.path.join(CONTENT, "items", "pets"))
# And socketstaff, MU's BITMAP_SOCKETSTAFF (Item/soketmagic_stape02.jpg, ZzzOpenData.cpp:5451):
# the Imperial Staff's gold lightning, laid three times over its body at its own UVs. And
# dancerfire, the Sword Dancer's swordmagum_06e, its fire sliding over the lava MU lays first.
IMAGES = {"chrome01": "refine/chrome01.png", "chrome02": "refine/chrome02.png",
          "bab2": "refine/bab2.png", "shiny01": "drop/shiny01.png",
          "socketstaff": "refine/socketstaff.png", "dancerfire": "refine/dancerfire.png",
          "chrome06": "refine/chrome06.png", "chrome07": "refine/chrome07.png",
          "energy01": "refine/energy01.png",
          "bladeeff2_r": "kanturu/bladeeff2_r.png", "buserbody_r": "kanturu/buserbody_r.png",
          "busersword_r": "kanturu/busersword_r.png", "illumi_r": "kanturu/illumi_r.png",
          "prsona_r": "kanturu/prsona_r.png", "prsonass_r": "kanturu/prsonass_r.png"}
# And Kanturu's weapons' three (ZzzOpenData.cpp:5301-5303): BITMAP_CHROME6 (Effect/Chrome06.jpg,
# the Sylph Wind Bow's), BITMAP_CHROME7 (Chrome07.jpg, the Bone Blade's) and BITMAP_CHROME_ENERGY
# (energy01.jpg, the Grand Viper Staff's), each laid at the mesh's own UVs (ZzzObject.cpp:7644-7666).
# Where an alpha-tested sheet counts as cut: MU's own test, alpha under a half
# (EnableAlphaTest's GL_GREATER 0.25 on a .tga is sharper than any sheet here needs).
CUT_BELOW = 128


def write(item, recipe, out):
    passes = recipe.get("chrome") or []
    sheets = {k.lower(): v for k, v in (recipe.get("sheets") or {}).items()}
    for n, one in enumerate(passes):
        # `own`: no chrome image bound, so the slot's own sheet at the pass's coordinates -- MU's
        # RenderMesh / RenderBody with RENDER_CHROME and no texture given (the Dark Horse's).
        if one["image"] == "own":
            own = sheets.get(one["slot"].lower())
            if own is None:
                raise SystemExit(f"{item}: chrome pass {n} names '{one['slot']}', no sheet of it")
            image = Image.open(os.path.join(CONTENT, "textures", own)).convert("RGB")
        else:
            image = Image.open(os.path.join(CONTENT, "effects",
                                            IMAGES[one["image"]])).convert("RGB")
        tint = one.get("tint", [1.0, 1.0, 1.0])
        top = max(tint) or 1.0
        bands = [band.point(lambda v, f=c / top: int(round(v * f)))
                 for band, c in zip(image.split(), tint)]
        Image.merge("RGB", bands).save(os.path.join(out, f"{item}_{n}.png"))

        if one.get("mask") in IMAGES:
            # A chrome image as the mask: MU's lightmap over a RENDER_METAL pass, read at the
            # mesh's own UVs and slid (the Archangel's three).
            mask = Image.open(os.path.join(CONTENT, "effects",
                                           IMAGES[one["mask"]])).convert("L")
        elif one.get("where", "over") == "cut":
            sheet = sheets.get(one["slot"].lower())
            if sheet is None:
                raise SystemExit(f"{item}: chrome pass {n} names '{one['slot']}', no sheet of it")
            alpha = Image.open(os.path.join(CONTENT, "textures", sheet)).convert(
                "RGBA").getchannel("A")
            mask = alpha.point(lambda a: 255 if a < CUT_BELOW else 0)
        else:
            mask = Image.new("L", (4, 4), 255)
        mask.convert("RGB").save(os.path.join(out, f"{item}_{n}_mask.png"))
        print(f"  {item} {n}: {one['slot']} {one['image']} {one.get('uv', 'chrome')} "
              f"{one.get('where', 'over')}, tint {tint}")


def main():
    wanted = set(sys.argv[1:])
    for folder in FOLDERS:
        out = os.path.join(folder, "chrome")
        for name in sorted(os.listdir(folder)):
            if not name.endswith(".json") or name.endswith(".rig.json"):
                continue
            item = name[:-5]
            if wanted and item not in wanted:
                continue
            with open(os.path.join(folder, name)) as handle:
                recipe = json.load(handle)
            if recipe.get("chrome"):
                os.makedirs(out, exist_ok=True)
                write(item, recipe, out)


if __name__ == "__main__":
    main()
