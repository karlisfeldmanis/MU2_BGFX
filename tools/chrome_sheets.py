#!/usr/bin/env python3
"""The sheets a weapon's chrome passes draw with, written beside its recipe.

    tools/chrome_sheets.py              every weapon and monster whose recipe has a `chrome` list
    tools/chrome_sheets.py Sword21      one

MuMain draws some weapons again over themselves with one of its chrome images: the Knight
Blade's violet blade, the Staff of Kundun's two sheens, the Bill of Balrog's blue veins, the
Great Reign Crossbow's grooves (RenderPartObjectBody, ZzzObject.cpp:7065-7075, 7613-7624,
7989-7996, 9682-9688). A recipe says each pass under `chrome`:

    {"slot": "nks01", "image": "chrome01", "uv": "chrome", "tint": [0.43, 0.14, 0.6],
     "where": "over"}

`image` is MU's chrome01 (BITMAP_CHROME), chrome02 (BITMAP_CHROME2) or bab2 (BITMAP_CHROME + 1),
out of source/effects/refine, or shiny01 (BITMAP_SHINY, RENDER_METAL's). `mask`, when it names one
of those, is that image as the mask -- MU's Chrome01 lightmap over a metal pass. `where` is "over" for the whole mesh, or "cut" for where its own
sheet's alpha is cut away: MU draws those passes before the body, so they only show through the
holes its alpha test leaves.

Two files a pass, in source/items/weapons/chrome/:

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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEAPONS = os.path.join(ROOT, "source", "items", "weapons")
# A monster's passes too (Hell Maine's red chrome over its armour and wings, GMAida.cpp:
# 1389-1405), written beside its recipe, where export_gltf looks: source/monsters/chrome/.
FOLDERS = (WEAPONS, os.path.join(ROOT, "source", "monsters"))
IMAGES = {"chrome01": "refine/chrome01.png", "chrome02": "refine/chrome02.png",
          "bab2": "refine/bab2.png", "shiny01": "drop/shiny01.png"}
# Where an alpha-tested sheet counts as cut: MU's own test, alpha under a half
# (EnableAlphaTest's GL_GREATER 0.25 on a .tga is sharper than any sheet here needs).
CUT_BELOW = 128


def write(item, recipe, out):
    passes = recipe.get("chrome") or []
    sheets = {k.lower(): v for k, v in (recipe.get("sheets") or {}).items()}
    for n, one in enumerate(passes):
        image = Image.open(os.path.join(ROOT, "source", "effects",
                                        IMAGES[one["image"]])).convert("RGB")
        tint = one.get("tint", [1.0, 1.0, 1.0])
        top = max(tint) or 1.0
        bands = [band.point(lambda v, f=c / top: int(round(v * f)))
                 for band, c in zip(image.split(), tint)]
        Image.merge("RGB", bands).save(os.path.join(out, f"{item}_{n}.png"))

        if one.get("mask") in IMAGES:
            # A chrome image as the mask: MU's lightmap over a RENDER_METAL pass, read at the
            # mesh's own UVs and slid (the Archangel's three).
            mask = Image.open(os.path.join(ROOT, "source", "effects",
                                           IMAGES[one["mask"]])).convert("L")
        elif one.get("where", "over") == "cut":
            sheet = sheets.get(one["slot"].lower())
            if sheet is None:
                raise SystemExit(f"{item}: chrome pass {n} names '{one['slot']}', no sheet of it")
            alpha = Image.open(os.path.join(ROOT, "source", "textures", sheet)).convert(
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
