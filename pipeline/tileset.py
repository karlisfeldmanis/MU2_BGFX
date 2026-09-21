"""Prepares a world's ground sheets: upscaled, enlarged, and given their surface maps.

    python3 pipeline/tileset.py source/world/lorencia/ground.json workshop/world/lorencia

The tiles profile is the one that has no model in it. Every other asset here is a thing — an
.obj arrives, a .glb leaves, and the sheets exist to be put on it — but TileGrass01 is not a
thing, it is what 81% of Lorencia is made of. So this run stops after the surface work: no
unwrap, no bake, no export, because there is nothing to unwrap. The ground mesh is built from
the map data by ground.py and picks up what this leaves.

What it leaves is a slot file of exactly the shape export_gltf's tiled path uses, so the same
reader serves both and the ground gets the same maps a boulder does.
"""

import json
import subprocess
import sys
from pathlib import Path

from PIL import Image

#: MU's rule: every tile shows this many texels of its sheet, whatever the sheet measures.
TEXELS_PER_TILE = 64

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def run(script: str, *arguments: str) -> None:
    subprocess.run(
        [sys.executable, str(HERE / script), *arguments], check=True)


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 tileset.py <ground.json> <out dir> [--rebuild]", file=sys.stderr)
        raise SystemExit(2)

    asset, out = Path(sys.argv[1]), Path(sys.argv[2])
    forced = "--rebuild" in sys.argv

    document = json.loads(asset.read_text())
    profiles = asset.parents[2] / "profiles"
    textures = asset.parents[2] / "textures"

    profile = json.loads((profiles / f"{document['profile']}.json").read_text())
    upscale = profile.get("upscale", {})
    factor = int(upscale.get("factor", 1))
    detail = float(upscale.get("detail", 0.0))
    pad = upscale.get("pad", "edge")
    model = upscale.get("model", "photographic")
    passes = int(upscale.get("passes", 8))
    enlarge = int(profile.get("enlarge", 1))

    out.mkdir(parents=True, exist_ok=True)
    materials = document.get("sheet_materials", {})
    fallback = profile.get("default_material", "unassigned")

    print(f"\n=== {document['tileset']} tileset: "
          f"{len(document['sheets'])} sheet(s), {factor}x at detail {detail}, {pad} ===")

    slots = []

    # The overlays that wear something other than the layer under them, as extra slots.
    #
    # Named <slot>_over, which is what the glTF calls the material and what the Ground tab
    # lists, so the asset and the panel use one vocabulary. Everything below treats them as
    # ordinary slots — they are upscaled, healed and given maps by the same code — and
    # ground.py picks the _over slot for the blended layer when one exists.
    sheets = dict(document["sheets"])

    for slot, file in (document.get("sheet_overlay") or {}).items():
        sheets[f"{slot}_over"] = file

    for name, file in sorted(sheets.items()):
        one: dict = {}
        source = textures / file

        if not source.exists():
            print(f"  {name:14s} no texture at {source}", file=sys.stderr)
            continue

        stem = Path(file).stem
        current = source

        # How far one copy of this sheet reaches, from MU's own file and nothing else.
        #
        # ground.py used to work this out from whatever was on disk at the end of the chain,
        # dividing the width by three because the name said _x3. That reads the *result* to
        # learn about the *source*, and it holds only while nothing changes the factor — the
        # old port guards the same spot with a comment saying so, and reads the shipped file's
        # size rather than the loaded texture's.
        #
        # It stops being hypothetical the moment a sheet arrives from somewhere else. A tile
        # upscaled sixfold outside this pipeline is the same tile; measured off the file it
        # would be twice as far across the ground as MU drew it.
        with Image.open(source) as opened:
            one["reach"] = max(1.0, opened.width / TEXELS_PER_TILE)

        if factor > 1 and detail > 0.0:
            big = out / f"{stem}_x{factor}.png"

            if forced or not big.exists() or source.stat().st_mtime > big.stat().st_mtime:
                run("upscale.py", str(current), str(big), str(detail), str(factor), pad,
                    model, str(passes))

            current = big

        # And made to wrap, which MU's ground sheets do not.
        #
        # Measured, not assumed: tileground02's top and bottom rows differ by 65 of 255 where
        # a neighbouring row differs by 8. The client never showed it - 64 texels a tile, forty
        # pixels a tile, nearest filtering - and here it is a continuous line across the plaza
        # every two metres in both directions. Distinct from the upscale's wrap padding above,
        # which stops a seam being *introduced* and cannot remove one that was already there.
        even = out / f"{Path(current).stem}_tiling.png"

        if forced or not even.exists() or current.stat().st_mtime > even.stat().st_mtime:
            run("seamless.py", str(current), str(even))

        current = even

        hd = out / f"{Path(current).stem}_hd.png"

        if forced or not hd.exists() or current.stat().st_mtime > hd.stat().st_mtime:
            run("enlarge.py", str(current), str(asset), str(hd), str(enlarge))

        # What the Ground tab decided about this tile, carried into the slot.
        #
        # The same keys a world object uses and read by the same code: tiled_maps runs over
        # this file a few lines down and applies saturation to the sheet and roughness and
        # metallic to the ORM, exactly as it does for a wall. Only `scale` is different in
        # kind, and it is different because the ground is: a world object's UVs put one copy
        # of a sheet across a face, so its scale *is* the repeat count, where a tile's repeat
        # is already set by how many texels the sheet has. So this multiplies that rather
        # than replacing it, which is also what the bench's slider does to what is built.
        one.update({
            "name": name,
            "sheet": str(hd),

            # What the *asset* calls this sheet, beside what the build made of it.
            #
            # The object pipeline has written this for as long as slots.json has existed and
            # the ground never did, which the Ground tab's report then repeated back at you:
            # "surface  darktile_fab_x3_tiling_hd_sat0.75.png". That is a true statement about
            # the build and a useless one to paste into an asset, which knows only
            # darktile_fab.png — and the report exists to be pasted. The upscaled, healed,
            # enlarged, desaturated file is four derivations downstream of the decision.
            "asset_sheet": file,
            "material": materials.get(name, fallback),
            "cutout": False,
            "additive": False,
        })

        for key, into in (("sheet_scale", "scale"),
                          ("sheet_saturation", "saturation"),
                          ("sheet_roughness", "roughness_scale"),
                          ("sheet_metallic", "metal_value"),
                          ("sheet_relief", "relief")):
            if (document.get(key) or {}).get(name) is not None:
                one[into] = float(document[key][name])

        if (document.get("sheet_tint") or {}).get(name):
            one["tint"] = str(document["sheet_tint"][name])

        slots.append(one)

    listing = out / "tiles_slots.json"
    listing.write_text(json.dumps(slots, indent=2))

    # The same maps a tiling world object gets, from the same code: a normal read off the
    # art's own luminance and a packed occlusion-roughness. Ground is where that pays most —
    # a cobbled street is its mortar joints and a meadow is the shadow between its tufts.
    run("tiled_maps.py", str(listing), str(asset.parents[2] / "materials"))

    print(f"  wrote      {listing}")


main()
