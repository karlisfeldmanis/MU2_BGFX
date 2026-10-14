"""`mu new monster <number>` and `mu new item <name>`: recipes whose facts are copied, not typed.

A monster (docs/content/recipes.md, "A monster", steps 1-4):
  - its row and its spawns from OpenMU, the number being the server's (Agon is 31);
  - its model, scale, name and weapons from MuMain's CreateMonster case for that number:
    OpenMonsterModel(MONSTER_MODEL_X) is Data/Monster/Monster{X+1}.bmd;
  - the mesh and the rig with every action (--actions=all: without it a monster stands in one
    idle), the sheets decoded under the monster's name where MU's own is generic (Agon's `pt`);
  - the recipe, with the row and the spawns in it as `stats` and `spawns`: the json is the truth
    mu.db will be cooked from (the user's decision, 2026-10-11; sprint E). Until then mu.db
    still feeds the pipeline, so `--db` writes the row and spawns there too, checkpointed, and
    only when the row is absent: an existing row's differences from OpenMU are listed, never
    overwritten -- they are recorded choices (WebZen's attack rates and respawns, Flame of
    Evil's 50 where OpenMU has its generic 150).
  - Left for judgement: each sheet's material (CHOOSE, which mu make refuses) and the hands --
    MuMain names them MODEL_SERPENT_SWORD and the like; the recipe quotes the lines.

An item (steps 1-2 and 5):
  - its OpenMU call, read against the helper's declaration, into the `stats` keys index.py
    reads -- checked against the 226 hand-made recipes with a call before this landed;
  - its model, Data/Item for weapons and shields, Data/Player for armour, as mu.db names it or
    by MU's file numbering (number + 1);
  - mesh and sheets, the recipe, and with `--db` the items row (code = group * 512 + number).
  - Only weapons, shields and armour pieces: a ring's or a scroll's helper means other things by
    its numbers, and those recipes stay hand-made.
"""

import filecmp
import json
import os
import re
import sqlite3
import subprocess
import sys
import tempfile

import sources
from new import MUEXTRACT, UNCHOSEN, data_root, materials
from things import ROOT, SOURCE, NotFound

# pipeline/world_manifest.py: the worlds and their nests. Appended, so nothing there shadows ours.
sys.path.append(os.path.join(ROOT, "pipeline"))
import world_manifest  # noqa: E402

DB = os.path.join(SOURCE, "mu.db")
WEAPONS, ARMOUR = range(0, 7), range(7, 12)


def muextract(*args):
    done = subprocess.run(["dotnet", MUEXTRACT, *args], capture_output=True, text=True)
    if done.returncode:
        raise NotFound(f"MuExtract {args[0]} failed: {(done.stdout + done.stderr).strip()[-300:]}")
    return done.stdout


def textures_of(obj):
    with open(obj) as handle:
        return list(dict.fromkeys(l.split(":", 1)[1].strip() for l in handle if l.startswith("# texture:")))


def decode(mu_file, wanted, say):
    """Decodes into content/textures/<wanted>, or beside it if that name already holds other art."""
    target = os.path.join(SOURCE, "textures", wanted)
    scratch = os.path.join(tempfile.mkdtemp(prefix="mu-sheet-"), wanted)
    done = subprocess.run(["python3", os.path.join(ROOT, "pipeline", "decode_texture.py"), mu_file, scratch],
                          capture_output=True, text=True)
    alpha = next((l.strip() for l in done.stdout.splitlines() if l.strip().startswith("alpha")), "")
    if os.path.exists(target) and not filecmp.cmp(scratch, target, shallow=False):
        stem, ext = os.path.splitext(wanted)
        target = os.path.join(SOURCE, "textures", f"{stem}_{os.path.basename(os.path.dirname(mu_file)).lower()}{ext}")
        say(f"        {wanted} is other art already; this one is {os.path.basename(target)}")
    if not os.path.exists(target):
        os.replace(scratch, target)
    return os.path.basename(target), "cutting away" in alpha and weak_alpha(target), alpha


def weak_alpha(png):
    """Whether a sheet's alpha is too weak to be its own mask, so `cutout` must key it off
    brightness (guard_hair.ozt's plume: 0.2% of it solid). A hard mask is left undeclared: the
    export finds it and alpha-tests it, and declaring it keyed the Spider's dark bristles off
    their dark ground into orange confetti -- twice, the second time from this scaffold
    (2026-10-12). pipeline/transfer_albedo.py's carry_alpha has the whole of it."""
    from PIL import Image
    alpha = Image.open(png).convert("RGBA").getchannel("A")
    solid = sum(alpha.histogram()[250:]) / (alpha.width * alpha.height)
    return solid < 0.1


def sheets_for(obj, folder, prefix_unless, say):
    from new import source_sheet
    sheets, cutout = {}, []
    for name in textures_of(obj):
        key = os.path.splitext(name)[0]
        mu_file = source_sheet(folder, name)
        if mu_file is None:
            say(f"NOTE    {name}: not in {os.path.basename(folder)}; find and decode it by hand")
            continue
        stem = key.lower()
        if prefix_unless and not stem.startswith(prefix_unless):
            stem = f"{prefix_unless}_{stem}"
        file, cut, alpha = decode(mu_file, stem + ".png", say)
        sheets[key] = file
        if cut:
            cutout.append(key)
        elif "cutting away" in alpha:
            alpha += "; a hard mask, left to the export (no cutout)"
        say(f"sheet   {key} -> {file}: {alpha}")
    return sheets, cutout


def world_of(map_number):
    """The world whose manifest is MU's map `map_number` (content/world/<w>/world.json)."""
    return world_manifest.world_of_map(map_number)


def db_row(table, key, value):
    with sqlite3.connect(f"file:{DB}?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row
        row = db.execute(f"select * from {table} where {key} = ?", (value,)).fetchone()
        return dict(row) if row else None


def write_db(statements):
    with sqlite3.connect(DB) as db:
        for sql, args in statements:
            db.execute(sql, args)
    with sqlite3.connect(DB) as db:
        db.execute("PRAGMA wal_checkpoint(TRUNCATE)")


def write_recipe(path, doc, say):
    with open(path, "w") as handle:
        json.dump(doc, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    say(f"recipe  {os.path.relpath(path, ROOT)}")


# ----------------------------------------------------------------------------- monsters

# A recipe's `combat` (sprint E2; mu.db's monster_kinds before it), as OpenMU gives it.
COMBAT_KEYS = ["level", "health", "minimum_damage", "maximum_damage", "defense",
               "move_range", "attack_range", "view_range", "move_delay", "attack_delay",
               "attack_rate", "defense_rate", "attack_skill"]


def new_monster(number, world, write, say):
    row = sources.monster(number)
    case = sources.monster_case(number)
    if row is None:
        raise NotFound(f"no monster {number} in OpenMU's {', '.join(sources.VERSIONS)}")
    if case is None or case["model_index"] is None:
        raise NotFound(f"no CreateMonster case with a model for {number} in MuMain")
    name = re.sub(r"[^A-Za-z0-9]", "", row["name"].title()) + "01"
    recipe = os.path.join(SOURCE, "monsters", name + ".json")
    for path in [recipe] + [os.path.join(SOURCE, "monsters", f) for f in os.listdir(os.path.join(SOURCE, "monsters"))
                            if f.endswith(".json") and not f.endswith(".rig.json")]:
        if os.path.exists(path):
            with open(path) as handle:
                other = json.load(handle)
            if path == recipe or other.get("object_type") == number:
                raise NotFound(f"monster {number} already has {os.path.relpath(path, ROOT)}")

    spawn_rows = sources.spawns(number, row["version"])
    maps = sorted({s["map"] for s in spawn_rows})
    world = world or (world_of(maps[0]) if maps else None)
    if not world:
        raise NotFound(f"{row['name']} spawns on no map this game has ({maps}); pass --world")
    say(f"monster {number} {row['name']} ({row['version']}, {row['from']}), {len(spawn_rows)} spawn areas on maps {maps}")
    say(f"model   {case['model']} = {case['model_index']}, scale {case['scale']} ({case['from']})")

    folder = os.path.join(data_root(), "Monster")
    bmd = os.path.join(folder, f"Monster{case['model_index'] + 1:02d}.bmd")
    if not os.path.exists(bmd):
        raise NotFound(f"no {os.path.relpath(bmd, data_root())}")
    obj = os.path.join(SOURCE, "monsters", name + ".obj")
    muextract("export-obj", bmd, obj)
    muextract("export-rig", bmd, os.path.join(SOURCE, "monsters", name + ".rig.json"), "--actions=all")
    say(f"mesh    {os.path.relpath(obj, ROOT)} and its rig, every action")
    sheets, cutout = sheets_for(obj, folder, re.sub(r"\d+$", "", name).lower(), say)

    stats = {k: row[k] for k in COMBAT_KEYS if k in row}
    stats["note"] = f"OpenMU {row['version']} ({row['from']})."
    doc = {
        "item": name,
        "mu_name": row["name"],
        "mu_name_from": f"OpenMU {row['version']} names number {number} '{row['name']}' ({row['from']}); "
                        f"MuMain's {case['enum']} ({case['from']}).",
        "world": world,
        "object_type": number,
        "monster": {"model_index": case["model_index"], "scale": case["scale"]},
        "monster_from": f"{case['from']}:\n\n{case['block']}",
        "profile": "monster",
        "sheets": sheets,
        "sheet_materials": {k: UNCHOSEN for k in sheets},
        "combat": stats,
    }
    if cutout:
        doc["cutout"] = cutout
    if case["weapons"]:
        doc["hands_todo"] = (f"MuMain gives it {', '.join(f'Weapon[{i}] = {m}' for i, m in case['weapons'])}: "
                             f"name the item files and bones as right_hand/left_hand (Agon01 shows how).")
    doc["why_this_one"] = "To write."
    write_recipe(recipe, doc, say)

    # The breed's combat is its recipe's (above); its nests are its worlds' nests.json (E3).
    existing = [n for n in world_manifest.nests() if n["breed"] == number]
    if existing:
        say(f"nests   {len(existing)} of {number} already stand in nests.json, kept")
    elif write:
        next_id = max((n["id"] for n in world_manifest.nests()), default=0) + 1
        by_world = {}
        for i, s in enumerate(spawn_rows):
            home = world_of(s["map"])
            if home is None:
                raise NotFound(f"map {s['map']} has no world manifest for {number}'s nest")
            by_world.setdefault(home, []).append({"id": next_id + i, "breed": number,
                                                  "box": [s["x1"], s["y1"], s["x2"], s["y2"]],
                                                  "count": s["count"]})
        for home, rows in by_world.items():
            path = world_manifest.nests_path(home)
            path.write_text(world_manifest.format_nests(world_manifest.world_nests(home) + rows))
            say(f"nests   {len(rows)} of {number} written to {os.path.relpath(path, ROOT)}")
    else:
        say(f"nests   none of {number} yet: --db writes its {len(spawn_rows)} into nests.json")
    say(f"CHOOSE  a material per sheet ({', '.join(sheets)}) from: {', '.join(materials())}")
    say(f"then    ./mu make {name}")
    return True, {"recipe": os.path.relpath(recipe, ROOT), "world": world, "stats": stats,
                  "spawns": len(spawn_rows), "sheets": sheets, "in_db": bool(existing)}


# ----------------------------------------------------------------------------- items

def new_item(name, write, say):
    found = sources.item(name)
    if found is None:
        raise NotFound(f"no item called '{name}' in OpenMU's items")
    if "error" in found:
        raise NotFound(found["error"])
    stats = sources.item_stats(found)
    group, number = stats.get("group"), stats.get("number")
    if group not in WEAPONS and group not in ARMOUR:
        raise NotFound(f"{found['helper']} is not a weapon, shield or armour helper; that recipe stays hand-made")
    row = db_row("items", "code", group * 512 + number)
    model, model_from = sources.item_model(group, number)
    if model is None:
        raise NotFound(f"MuMain's ZzzOpenData.cpp loads no model for group {group} number {number}")
    bmd = sources.find_file(data_root(), model)
    if bmd is None:
        raise NotFound(f"ZzzOpenData.cpp names {model} ({model_from}) and MuMain's Data has no such file")
    model = os.path.relpath(bmd, data_root())
    stem = os.path.splitext(os.path.basename(model))[0]
    stem = stem[0].upper() + stem[1:]
    folder_name = "weapons" if group <= 5 else "shields" if group == 6 else "armor"
    recipe = os.path.join(SOURCE, "items", folder_name, stem + ".json")
    if os.path.exists(recipe):
        raise NotFound(f"{os.path.relpath(recipe, ROOT)} already exists")
    say(f"item    {stats.get('name')}: group {group} number {number} ({found['version']}, {found['from']})")

    obj = os.path.join(SOURCE, "items", folder_name, stem + ".obj")
    muextract("export-obj", bmd, obj)
    say(f"mesh    {os.path.relpath(obj, ROOT)} from {model}")
    sheets, cutout = sheets_for(obj, os.path.dirname(bmd), None, say)

    item_name = stats.pop("name", stem)
    doc = {
        "item": stem,
        "mu_name": item_name,
        "mu_name_from": f"OpenMU {found['version']}'s {found['helper']} ({found['from']}); the model is {model}, "
                        f"which MuMain's OpenItems loads for it ({model_from}).",
        "profile": "armour" if group in ARMOUR else "weapon",
        "stats": stats,
        "sheets": sheets,
    }
    if cutout:
        doc["cutout"] = cutout
    doc["why_this_one"] = "To write."
    write_recipe(recipe, doc, say)

    if row:
        say(f"mu.db   has item {row['code']} ({row['name']})")
    elif write:
        write_db([("insert into items (code, item_group, item_index, name, model, drop_level) values (?,?,?,?,?,?)",
                   [group * 512 + number, group, number, item_name, model, stats.get("drop_level", 0)])])
        say(f"mu.db   item {group * 512 + number} written, checkpointed")
    else:
        say(f"mu.db   has no item {group * 512 + number}: --db writes it")
    say(f"then    ./mu make {stem}   (armour: soft_armour is the profile for cloth)")
    return True, {"recipe": os.path.relpath(recipe, ROOT), "stats": stats, "sheets": sheets, "in_db": bool(row)}
