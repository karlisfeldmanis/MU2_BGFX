"""`mu new object <world>/<ObjectNN>`: a world object's recipe, with everything a tool can know filled in.

What the Aida batches did by hand, per kind (docs/recipes.md, "A world object"):

  1. export the mesh out of MU's .bmd (MuExtract export-obj), into source/world/<w>/;
  2. read which sheets it wears -- MuExtract writes them into the .obj as `# texture:` lines,
     which pipeline/model_sheets.py cannot do for the version-12 models Aida ships;
  3. decode each into source/textures under the world's prefix (ai_, ad_ ...: the pool is flat
     and names collide across worlds), and read decode_texture's alpha verdict for the cutout;
  4. count its placements and read its object type out of the terrain import (<w>.json);
  5. write the recipe.

What a tool cannot know is left marked: each sheet's material is "CHOOSE", and `mu make`
refuses the recipe until every one is chosen, naming the library. That is the judgement --
read the art, name what it is -- and it stays a person's or an agent's, on purpose.
"""

import collections
import glob
import json
import os
import re
import subprocess

from things import ROOT, SOURCE, NotFound

UNCHOSEN = "CHOOSE"
DATA_ROOTS = [os.path.join(ROOT, "reference", "MuMain", "src", "bin", "Data"),
              os.path.join(ROOT, "..", "LEGACY", "reference", "MuMain", "src", "bin", "Data")]
MUEXTRACT = os.path.join(ROOT, "tools", "MuExtract", "bin", "Release", "net10.0", "muextract.dll")


def data_root():
    for root in DATA_ROOTS:
        if os.path.isdir(root):
            return os.path.normpath(root)
    raise NotFound("no MuMain Data: run tools/fetch_mumain.sh")


def world_doc(world):
    path = os.path.join(SOURCE, "world", world, f"{world}.json")
    if not os.path.exists(path):
        raise NotFound(f"no terrain import {os.path.relpath(path, ROOT)}: `mu make ground {world} --folder N` first")
    with open(path) as handle:
        return json.load(handle)


def world_prefix(world):
    """The prefix the world's recipes already decode their sheets under."""
    seen = collections.Counter()
    for path in glob.glob(os.path.join(SOURCE, "world", world, "*.json")):
        try:
            with open(path) as handle:
                sheets = json.load(handle).get("sheets") or {}
        except (OSError, ValueError, AttributeError):
            continue
        for file in sheets.values():
            match = re.match(r"([a-z]+_)", str(file))
            if match:
                seen[match.group(1)] += 1
    return seen.most_common(1)[0][0] if seen else None


def materials():
    return sorted(os.path.splitext(f)[0] for f in os.listdir(os.path.join(SOURCE, "materials"))
                  if f.endswith(".json"))


def unchosen(doc):
    return sorted(k for k, v in (doc.get("sheet_materials") or {}).items() if v == UNCHOSEN)


def source_sheet(folder, name):
    """MU's file for a sheet the model names: boss.JPG is boss.OZJ, a .tga an .OZT, any case."""
    stem = os.path.splitext(name)[0].lower()
    want = {".jpg": ".ozj", ".tga": ".ozt", ".bmp": ".ozb"}.get(os.path.splitext(name)[1].lower())
    for file in os.listdir(folder):
        base, ext = os.path.splitext(file)
        if base.lower() == stem and (want is None or ext.lower() == want):
            return os.path.join(folder, file)
    return None


def new_object(target, prefix, say):
    if "/" not in target:
        raise NotFound("name it with its world: mu new object aida/Object52")
    world, name = target.split("/", 1)
    recipe = os.path.join(SOURCE, "world", world, name + ".json")
    if os.path.exists(recipe):
        raise NotFound(f"{os.path.relpath(recipe, ROOT)} already exists")
    doc = world_doc(world)
    folder = os.path.join(data_root(), f"Object{doc['map_number']}")
    bmd = os.path.join(folder, name + ".bmd")
    if not os.path.exists(bmd):
        raise NotFound(f"no {os.path.relpath(bmd, data_root())} in MuMain's Data")
    prefix = prefix or world_prefix(world)
    if not prefix:
        raise NotFound(f"{world} has no recipes to read a sheet prefix from: pass --prefix xx_")

    obj = os.path.join(SOURCE, "world", world, name + ".obj")
    done = subprocess.run(["dotnet", MUEXTRACT, "export-obj", bmd, obj], capture_output=True, text=True)
    if done.returncode or not os.path.exists(obj):
        raise NotFound(f"MuExtract export-obj failed: {(done.stdout + done.stderr).strip()[-300:]}")
    with open(obj) as handle:
        names = [line.split(":", 1)[1].strip() for line in handle if line.startswith("# texture:")]
    say(f"mesh    {os.path.relpath(obj, ROOT)}: {len(names)} mesh(es)")

    sheets, cutout, notes = {}, [], []
    for sheet in dict.fromkeys(names):
        key = os.path.splitext(sheet)[0]
        mu_file = source_sheet(folder, sheet)
        if mu_file is None:
            notes.append(f"{sheet}: not in {os.path.basename(folder)}; find and decode it by hand")
            sheets[key] = f"{prefix}{key.lower()}.png"
            continue
        png = os.path.join(SOURCE, "textures", f"{prefix}{key.lower()}.png")
        report = ""
        if not os.path.exists(png):
            done = subprocess.run(["python3", os.path.join(ROOT, "pipeline", "decode_texture.py"), mu_file, png],
                                  capture_output=True, text=True)
            report = done.stdout
        else:
            report = "already decoded"
        if "cutting away" in report:
            cutout.append(key)
        sheets[key] = os.path.basename(png)
        alpha = next((l.strip() for l in report.splitlines() if l.strip().startswith("alpha")), report.strip())
        say(f"sheet   {key} -> {os.path.basename(png)}: {alpha}")

    number = int(re.sub(r"\D", "", name) or 0)
    placed = [o for o in doc.get("objects", []) if o.get("model") == name]
    recipe_doc = {
        "item": name,
        "mu_name": name,
        "mu_name_from": f"{world}'s object slot: type N is Object{{N+1}}.bmd, from Data/Object{doc['map_number']}.",
        "world": world,
        "object_type": placed[0]["type"] if placed else number - 1,
        "placements": len(placed),
        "profile": "world",
        "sheets": sheets,
        "sheet_from": f"Data/Object{doc['map_number']}/, decoded under the {prefix} prefix by `mu new`.",
        "sheet_materials": {key: UNCHOSEN for key in sheets},
        "material_note": "To write: what each sheet is, read off the art.",
    }
    if cutout:
        recipe_doc["cutout"] = cutout
        recipe_doc["cutout_from"] = "decode_texture found a real alpha channel cutting the sheet."
    recipe_doc["why_this_one"] = "To write."
    with open(recipe, "w") as handle:
        json.dump(recipe_doc, handle, indent=2)
        handle.write("\n")

    say(f"recipe  {os.path.relpath(recipe, ROOT)}: {len(placed)} placements, type {recipe_doc['object_type']}")
    for note in notes:
        say(f"NOTE    {note}")
    say(f"CHOOSE  a material per sheet ({', '.join(sheets)}) from: {', '.join(materials())}")
    say(f"then    ./mu make {world}/{name}")
    return True, {"recipe": os.path.relpath(recipe, ROOT), "sheets": sheets, "cutout": cutout,
                  "placements": len(placed), "unchosen": list(sheets), "notes": notes}
