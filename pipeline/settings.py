"""Works out what one asset's build settings actually are, and prints them for a shell.

    python3 pipeline/settings.py source/items/Sword01/Sword01.json source/profiles

Two places can say how an asset is built and they are not equals. A *profile* is a
statement about a kind of thing — every weapon's sheet is an atlas, so its pad is
replicated; every weapon is looked at from arm's length, so its maps are 2048. The asset's
own file is where one particular thing turns out not to be like its kind, and that is rare
enough to be worth writing down loudly when it happens.

So the profile is the default and the asset overrides it, and this prints the result rather
than leaving each step to work it out again. The alternative — every pipeline step reading
both files and merging them itself — is four implementations of one rule, which is three
opportunities for a weapon to be enlarged sixteen times in one step and eight in the next.

Printed as shell assignments because the caller is a shell script. Values are quoted and
the keys are fixed, so `eval` on the output is reading a known set of names rather than
whatever a JSON file happened to contain.
"""

import json
import math
import shlex
import sys
from pathlib import Path

#: What a build needs to know, and what it falls back to when nothing says otherwise.
#:
#: The fallbacks are deliberately the conservative end: no upscale at all, and an
#: enlargement that only a reconstruction filter touches. An asset that names no profile
#: gets MU's art and nothing generated, which is the right thing to happen by accident.
DEFAULTS = {
    "sheet": "",
    "profile": "",
    "material_maps": 1024,
    "maps": 512,
    "enlarge": 2,
    "upscale_factor": 0,
    "upscale_detail": 0.0,
    "upscale_pad": "edge",

    #: Which network, and how many ways round the sheet is fed to it. See upscale.MODELS:
    #: the ground wants its drawing recovered and an item wants to be left alone, and those
    #: are two different models rather than two settings on one.
    "upscale_model": "photographic",
    "upscale_passes": 8,
    "tiled": 0,

    #: Whether backfaces are culled, the way the client culls them. Off by default
    #: because MU's items are open shells; on for anything worn. See export_gltf.
    "cull": 0,

    #: Whether the sheet tiles. Off for an atlas, which is what a body and an item wear:
    #: the head, torso, legs, hands and feet sit side by side on one page and nothing on it
    #: continues over an edge. Only read on the outside-sheet path, where it decides whether
    #: the sheet is seam-healed — wrapping an atlas drags one edge's content across the
    #: opposite one, which showed up as tan bands down the Dark Wizard's arm.
    "tiling": 1,
    "default_material": "unassigned",

    #: How fast a clip plays, as MU's own play speed: keys a second is this times 25.
    #:
    #: Empty means "whatever export_gltf defaults to", which is the townsperson's 0.25 —
    #: ZzzOpenData's OpenNpc walks every action of every NPC model and sets it. A world object
    #: is not an NPC and CreateObject hands it 0.16, so the world profile says so and the
    #: whole of the town's scenery stops running 56% fast. An asset overrides it where MU's
    #: own switch does: a street lamp, a candle and a sign at 0.3, a tree at 0.4 over its
    #: scale, a treasure chest at nought.
    "play_speed": "",
}

#: The most MU's art is ever allowed to be enlarged, at any one step.
#:
#: Three. It was two, and the reasons for two were about the reconstruction filter rather
#: than the model: no filter adds information, so enlarging past what the art holds only
#: makes a bigger, smearier copy. That still governs `enlarge`, which is capped at the map
#: it feeds and mostly runs at one.
#:
#: The model pass is a different thing, and it was made safe enough to ask more of — see
#: upscale.py. It now averages eight orientations of the sheet, which cancels the part of
#: its output that depends on which way round the picture was rather than on the picture,
#: and it adds its detail to brightness alone, so MU's colour comes through untouched. With
#: those two, a third of the sheet's worth of extra reconstruction is worth having and the
#: detail figure came up from 0.35 to 0.65.
#:
#: It was still two for the filter, and is not a tuning knob. `upscale.py` already argues the case for its own model
#: pass — two is enough to resolve what is there and not enough for the model to start
#: composing — and the same limit belongs on the reconstruction filter, which was running at
#: sixteen and taking a 256 sheet to 8192. No filter adds information. All a larger factor
#: buys is a bigger container for the same picture, and a smeared one, because everything
#: downstream then resamples it again.
SCALE_CAP = 3

#: The smallest and largest a bake map is allowed to be.
#:
#: The floor is so a tiny sheet still has room for the unwrap's seams and the eight texels
#: of bleed build_maps puts around each island. It has come down twice, both times because
#: the test caught it handing a small sheet a map larger than the cap allows — 256 when the
#: shield's 64-wide art wanted 128, and 128 when the axe's 32-wide art wanted 96. A floor
#: that quietly reintroduces the thing the cap exists to prevent is not caution, it is the
#: bug wearing a smaller number, and it has now been that twice.
#:
#: The ceiling is so nothing can go back to a 2048 square for art holding two thousand
#: texels.
MAP_FLOOR, MAP_CEILING = 64, 1024

#: The size of the maps that are generated rather than resampled: orm and normal.
#:
#: Not bound by the 2x rule, and that is not an exception to it — it is what the rule is
#: about. The cap exists so MU's *painting* is never enlarged past what it holds, because
#: no filter can add colour detail that was never drawn. The orm and the normal are not
#: MU's painting. They are built from a description of what a surface is — steel is
#: metallic and has a mill grain, leather scatters and is wound in bands — and that
#: description has no resolution of its own.
#:
#: A thousand and twenty-four because of what the grain needs to be drawn. A material's
#: `scale` is a count of cells across the map, and a cell wants two texels or more or it
#: aliases; plate_steel asks for 260, so it needs above 520. Sizing these maps to the art
#: instead put 260 cells on a 128 square and drew pitting the size of a shoulder plate.
MATERIAL_MAP = 1024


def resolve(asset: Path, profiles: Path, textures: Path | None = None) -> dict:
    document = json.loads(asset.read_text())
    settings = dict(DEFAULTS)

    name = document.get("profile", "")
    if name:
        path = profiles / f"{name}.json"
        if not path.exists():
            raise SystemExit(f"error: {asset.name} names profile '{name}', which is not in {profiles}")

        settings.update(flatten(json.loads(path.read_text())))
        settings["profile"] = name

    # The asset's own word, last.
    settings.update(flatten(document))
    settings["sheet"] = document.get("sheet", "")

    # The cap is the last word, over both. A profile or an asset asking for more than the cap
    # is asking for something no filter can give it: MU's own painting is the whole of what
    # a sheet knows, and the project stands on that painting and nothing larger.
    settings["enlarge"] = min(int(settings["enlarge"]), SCALE_CAP)
    settings["upscale_factor"] = min(int(settings["upscale_factor"]), SCALE_CAP)

    settings["material_maps"] = MATERIAL_MAP

    if "maps" not in flatten(document) and textures is not None:
        if (sized := map_size(document, textures)) is not None:
            settings["maps"] = sized

        # And never enlarge past the map that is going to hold it.
        #
        # enlarge exists to keep one part of a sheet from bleeding into the next while it is
        # resampled, and the factor was only ever there to reach a 2048 bake. With the map
        # sized to the art, the upscaled sheet is usually already at or past it, and taking
        # it further makes a blurrier intermediate and nothing else — the bake would only
        # sample it back down. What is left is the boundary protection, which is the part
        # that was worth having.
        widest = widest_sheet(document, textures)
        if widest:
            already = widest * max(1, int(settings["upscale_factor"]))
            settings["enlarge"] = max(1, min(
                int(settings["enlarge"]), -(-int(settings["maps"]) // already)))

    return settings


def widest_sheet(document: dict, textures: Path) -> int:
    """The largest dimension of any sheet this asset names, in texels."""
    # All three spellings, because the tree uses all three.
    #
    # map_size's own note says a cleverer version of it "read only one of the two spellings
    # of `sheets`, so half the assets silently kept the old fixed number". There is a third:
    # GloveMale10 and BootMale10 name a single texture as `sheet`, and this read neither of
    # them — so both were baked at the profile's default forever, at whatever size that
    # happened to be rather than at the size of their art. Silent, and only visible when
    # their sheets grew sixfold and their atlases did not.
    sheets = document.get("sheets")
    names = list(sheets.values()) if isinstance(sheets, dict) else list(sheets or [])

    if document.get("sheet"):
        names.append(document["sheet"])

    try:
        from PIL import Image
    except ImportError:
        return 0

    widest = 0
    for name in names:
        # MU's own file, which is the only sheet there is. There was a time a sheet could
        # arrive already enlarged from outside the pipeline, and a bake sized to MU's file
        # threw that away; the drop-box is gone, and the upscale is the pipeline's alone.
        path = textures / name

        if path.exists():
            with Image.open(path) as image:
                widest = max(widest, max(image.size))

    return widest



def map_size(document: dict, textures: Path) -> int | None:
    """Twice the widest sheet the asset draws on, and nothing more.

    A flat 2048 square was costing a hundred times more container than content — Shield10 is
    2,246 real texels of MU's art and it was being baked into four million. That is not
    detail, it is the same picture across more memory.

    The rule is the one the whole pipeline runs on: MU's art is doubled and never more. The
    bake layout is MU's own wherever it can be, so a map twice the sheet puts every texel of
    the art on four of the map's, which is exactly what the upscale pass has to give it and
    exactly nothing beyond.

    It was briefly cleverer than this — counting the texels the islands actually sample and
    sizing to their square root — and cleverer was wrong twice over. It double-counted
    mirrored islands, and it read only one of the two spellings of `sheets` in the tree, so
    half the assets silently kept the old fixed number. Width of the art times two needs
    neither the island boxes nor a special case.
    """
    widest = widest_sheet(document, textures)

    if widest == 0:
        return None

    # Exactly the cap times the art, and not rounded to a power of two.
    #
    # Rounding was habit and it cannot express a cap of three: 256 art wants 768, and the
    # nearest power of two is 1024, which is four times the art and the very thing the cap
    # forbids. The test said so on the first run after the cap moved. Nothing downstream
    # needs a power of two — the maps are sampled by a modern renderer that has not cared
    # since it stopped being OpenGL 2.
    return int(min(max(SCALE_CAP * widest, MAP_FLOOR), MAP_CEILING))


def sheet_texels(document: dict, textures: Path) -> int:
    """How many texels of MU's art this asset draws on, across every sheet it names."""
    sheets = document.get("sheets")
    names = sheets.values() if isinstance(sheets, dict) else (sheets or [])

    try:
        from PIL import Image
    except ImportError:
        return 0

    total = 0
    for name in names:
        path = textures / name
        if path.exists():
            with Image.open(path) as image:
                total += image.size[0] * image.size[1]

    return total


def flatten(document: dict) -> dict:
    """The keys a build cares about, with the nested upscale block spread out flat."""
    out = {}

    for key in ("maps", "enlarge", "default_material", "play_speed"):
        if key in document:
            out[key] = document[key]

    upscale = document.get("upscale")
    if isinstance(upscale, dict):
        out["upscale_factor"] = upscale.get("factor", 2)
        out["upscale_detail"] = upscale.get("detail", 0.0)

        # How the sheet's border is extended before the network sees it, which is a fact
        # about the kind of sheet and not about the model. An item sheet is an atlas and
        # replicates; a world sheet tiles across a wall and wraps. The profiles have said
        # this since they were written and nobody was carrying it as far as upscale.py,
        # which had "edge" as a constant.
        out["upscale_pad"] = upscale.get("pad", "edge")
        out["upscale_model"] = upscale.get("model", "photographic")
        out["upscale_passes"] = int(upscale.get("passes", 8))

    # Whether this ships MU's sheets at MU's coordinates instead of a baked atlas. A fact
    # about the kind of art rather than about the object: scenery tiles and items do not.
    if "tiled" in document:
        out["tiled"] = 1 if document["tiled"] else 0

    # And whether it is drawn one-sided. Also a fact about the kind of thing: a worn part is
    # a closed body and an item is an open shell. See export_gltf's note on the client.
    if "cull" in document:
        out["cull"] = 1 if document["cull"] else 0

    if "tiling" in document:
        out["tiling"] = 1 if document["tiling"] else 0

    return out


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 settings.py <asset.json> <profiles dir> [textures dir]",
              file=sys.stderr)
        raise SystemExit(2)

    textures = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    settings = resolve(Path(sys.argv[1]), Path(sys.argv[2]), textures)

    for key in DEFAULTS:
        print(f"{key}={shlex.quote(str(settings[key]))}")


main()
