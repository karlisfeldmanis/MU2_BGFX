#!/usr/bin/env python3
"""Cooks ONE thing, reports it, shoots it on the viewer's stage, and logs it for a look.

    tools/cook_one.py Scale              an armour set, by its label or suffix (Male07)
    tools/cook_one.py "Light Crossbow"   a weapon or a shield, by label or file name
    tools/cook_one.py Hound01            a figure's mesh -- a monster, a person, a part
    tools/cook_one.py Cannon01           a world object, in --world (lorencia)
    tools/cook_one.py Scale --no-shots   cook and report only
    tools/cook_one.py --pass Scale "gold reads as gold at dusk"
    tools/cook_one.py --fail Scale "the gloves are chrome"

Why this exists: a full cook changes a hundred things at once and nobody looks at each of
them, so a mistake is found much later and far from its cause. This is the day-to-day way:
one item, its report, its shots, a look, and a line in docs/cook-log.md saying whether it
passed -- then the next. `tools/cook.py` still rebuilds everything, for when that is wanted.

What it touches and nothing else: that item's textures (compressed only if not already
there -- a .ktx is named by the sha1 of its source bytes, so existence is the up-to-date
test), that item's meshes, and that item's entries in the area's textures.json and table
(wardrobe.json, figures.json). Every other entry in them is left as it was. A world object
new to the town still needs `tools/cook.py --only placements` to be stood anywhere; this
says so rather than doing it.
"""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cook  # noqa: E402  (the cook's own texture and mesh steps, so the two cannot drift)
from cook import ASSETS, ROOT, cook_mesh, image_bytes, read_glb, roles_of, safe  # noqa: E402

LOG = os.path.join(ROOT, "docs", "cook-log.md")
PIECES = ("Helm", "Armor", "Pant", "Glove", "Boot")
TIMES = ("noon", "dusk", "night")


def load_json(path, default):
    if not os.path.exists(path):
        return default
    with open(path) as handle:
        return json.load(handle)


def write_json(path, value):
    with open(path, "w") as handle:
        json.dump(value, handle, indent=1, sort_keys=True)


def lower(text):
    return (text or "").lower()


# --------------------------------------------------------------------------- what NAME is

def resolve(name, world):
    """What `name` is, as (kind, area, meshes, extra): meshes maps mesh name -> glb path.

    Asked in this order: an armour set, a weapon or shield, a figure mesh, a world object.
    Matching ignores case, and a set answers to its label ("Scale"), its suffix ("Male07")
    or its torso's file ("ArmorMale07").
    """
    index = load_json(os.path.join(ASSETS, "index.json"), {})
    rows = {}
    for one in index.get("objects", []):
        glb = one.get("glb", "")
        if glb and one.get("kind") in ("armor", "weapon", "shield"):
            rows[os.path.splitext(os.path.basename(glb))[0]] = one
    wanted = lower(name)

    # An armour set.
    suits = {}
    for file, one in rows.items():
        if one.get("kind") != "armor":
            continue
        for piece in PIECES:
            if file.startswith(piece):
                suits.setdefault(file[len(piece):], {})[piece] = (file, one)
    for suffix, worn in suits.items():
        torso = worn.get("Armor")
        if torso is None:
            continue
        label = torso[1].get("label", suffix)
        label = label[: -len(" Armor")] if label.endswith(" Armor") else label
        if wanted in (lower(label), lower(suffix), lower("Armor" + suffix)):
            meshes = {file: os.path.join(ASSETS, one["glb"])
                      for piece in PIECES if piece in worn for file, one in [worn[piece]]}
            stats = torso[1].get("stats") or {}
            entry = {"name": suffix, "label": label,
                     "parts": [piece + suffix for piece in PIECES if piece in worn],
                     "classes": stats.get("classes") or [],
                     "defense": stats.get("defense", 0),
                     "keeps_head": bool(worn["Helm"][1].get("keeps_head"))
                     if "Helm" in worn else False}
            return "set", "wardrobe", meshes, entry

    # A weapon or a shield.
    for file, one in rows.items():
        if one.get("kind") in ("weapon", "shield") and wanted in (lower(file),
                                                                   lower(one.get("label"))):
            stats = one.get("stats") or {}
            entry = {"name": file, "label": one.get("label", file), "mesh": file,
                     "kind": one["kind"], "stance": one.get("stance", ""),
                     "two_handed": bool(one.get("two_handed")),
                     "classes": stats.get("classes") or []}
            return "arm", "wardrobe", {file: os.path.join(ASSETS, one["glb"])}, entry

    # A figure's mesh, and any variant cut from it (BullFighter01~whole).
    figures = load_json(os.path.join(ASSETS, "cooked", "figures", "figures.json"), {})
    labels = {lower(m.get("label")): m.get("mesh") for m in figures.get("monsters", [])}
    base = labels.get(wanted)
    for mesh_name in figures.get("meshes", {}):
        if lower(mesh_name) == wanted:
            base = mesh_name.split("~", 1)[0]
    if base:
        glb = None
        for one in index.get("objects", []) + index.get("monsters", []):
            candidate = one.get("glb")
            if isinstance(candidate, list):
                candidate = candidate[0] if candidate else None
            if candidate and os.path.splitext(os.path.basename(candidate))[0] == base:
                glb = os.path.join(ASSETS, candidate)
        if glb and os.path.exists(glb):
            meshes = {m: glb for m in figures["meshes"] if m.split("~", 1)[0] == base}
            meshes.setdefault(base, glb)
            return "figure", "figures", meshes, figures

    # A world object.
    world_glb = os.path.join(ASSETS, "world", world, name, f"{name}.glb")
    if os.path.exists(world_glb):
        return "world", world, {name: world_glb}, None
    return None, None, None, None


# --------------------------------------------------------------------------- the cooking

def cook_textures(meshes, area_dir, texcook):
    """The item's images into the area's textures, and its entries into textures.json."""
    raw_dir = os.path.join(area_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)
    os.makedirs(os.path.join(area_dir, "textures"), exist_ok=True)
    manifest_path = os.path.join(area_dir, "textures.json")
    document = load_json(manifest_path, {"version": 1, "textures": {}})
    manifest = document.setdefault("textures", {})

    jobs = []
    sources = {os.path.splitext(os.path.basename(p))[0]: p for p in meshes.values()}
    for model, path in sorted(sources.items()):
        glb, binary = read_glb(path)
        for image, role, cutout in roles_of(glb):
            data = image_bytes(glb, binary, image)
            if data is None:
                uri = glb["images"][image].get("uri")
                if uri is None:
                    continue
                with open(os.path.join(os.path.dirname(path), uri), "rb") as handle:
                    data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            label = glb["images"][image].get("name") or f"image{image}"
            stem = f"{safe(model)}_{safe(label)}_{role}_{digest}"
            ktx_path = os.path.join(area_dir, "textures", stem + ".ktx")
            manifest[f"{model}#{image}:{role}"] = os.path.relpath(ktx_path, ASSETS)
            if os.path.exists(ktx_path) or any(j[3] == ktx_path for j in jobs):
                continue
            raw_path = os.path.join(raw_dir, stem + ".bin")
            with open(raw_path, "wb") as handle:
                handle.write(data)
            jobs.append((role, cutout, raw_path, ktx_path))

    print(f"cook_one: {len(jobs)} image(s) to compress for {', '.join(sorted(sources))}")
    if jobs:
        job_file = os.path.join(area_dir, "jobs_one.txt")
        with open(job_file, "w") as handle:
            for role, cutout, source, target in jobs:
                handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")
        if subprocess.run([texcook, job_file]).returncode:
            print("cook_one: texcook failed; nothing written", file=sys.stderr)
            return None
    write_json(manifest_path, document)
    return manifest


def cook_item(kind, area, meshes, extra, texcook):
    area_dir = os.path.join(ASSETS, "cooked", area)
    manifest = cook_textures(meshes, area_dir, texcook)
    if manifest is None:
        return None
    mesh_dir = os.path.join(area_dir, "meshes")
    os.makedirs(mesh_dir, exist_ok=True)

    hidden = {}
    if kind == "figure":
        for row in extra.get("monsters", []):
            if row.get("hidden_mesh") is not None:
                hidden.setdefault(row["mesh"], row["hidden_mesh"])

    cooked = {}
    for mesh_name, path in sorted(meshes.items()):
        out_path = os.path.join(mesh_dir, mesh_name + ".mum")
        tris, _verts, _size, bones = cook_mesh(mesh_name, path, out_path, manifest,
                                               hidden.get(mesh_name))
        cooked[mesh_name] = {"mesh": os.path.relpath(out_path, ASSETS), "bones": bones,
                             "triangles": tris}
        print(f"cook_one: {mesh_name}: {tris} triangles -> {os.path.relpath(out_path, ROOT)}")

    if area == "wardrobe":
        table_path = os.path.join(area_dir, "wardrobe.json")
        table = load_json(table_path, {"version": 1, "meshes": {}, "sets": [], "arms": []})
        table.setdefault("meshes", {}).update(cooked)
        key = "sets" if kind == "set" else "arms"
        table[key] = [one for one in table.get(key, []) if one["name"] != extra["name"]]
        table[key].append(extra)
        table[key].sort(key=lambda one: one["name"])
        write_json(table_path, table)
    elif area == "figures":
        table_path = os.path.join(area_dir, "figures.json")
        extra.setdefault("meshes", {}).update(cooked)
        write_json(table_path, extra)
    else:
        town = os.path.join(area_dir, f"{area}.mut")
        print(f"cook_one: a world object's mesh is rewritten in place; if it is new to the "
              f"town, `tools/cook.py --only placements` stands it ({os.path.relpath(town, ROOT)})")
    return cooked


# --------------------------------------------------------------------------- the report

def report(meshes):
    """Each piece's material slots: what the ORM under them carries, and the metal's flag."""
    import matcheck  # noqa: E402  (its readers, not its gate)
    index = load_json(os.path.join(ASSETS, "index.json"), {})
    library = {m["name"]: m for m in index.get("materials", [])}
    lines = []
    for mesh_name, path in sorted(meshes.items()):
        if "~" in mesh_name:
            continue
        glb, _binary = read_glb(path)
        slots = matcheck.read_model(path, library)
        for slot in slots:
            if slot.triangles == 0:
                continue
            material = glb["materials"][slot.index]
            image = cook.image_for(glb, material, "albedo")
            named = glb["images"][image].get("name", "") if image is not None else ""
            calibrated = named.endswith("_basecolor")
            metal = "-" if slot.metal is None else f"{slot.metal:.2f}"
            rough = "-" if slot.roughness is None else f"{slot.roughness:.2f}"
            lines.append(f"  {mesh_name:16} {slot.name:16} {slot.triangles:5} tris  metal {metal:>4}"
                         f"  rough {rough:>4}  {'calibrated' if calibrated else 'paint':10}"
                         f"  {slot.alpha_mode.lower()}")
    print("cook_one: report")
    print("\n".join(lines))
    return lines


# --------------------------------------------------------------------------- the shots

def shoot(kind, name, extra, world):
    """The item on the viewer's stage at noon, dusk and night, and the three side by side."""
    category, pick = {
        "set": ("armour", extra["label"] if extra else name),
        "arm": ("weapons", extra["label"] if extra else name),
        "figure": ("monsters", name),
        "world": ("world", name),
    }[kind]
    out = os.path.join(ROOT, "shots", "cook", safe(name))
    os.makedirs(out, exist_ok=True)
    frames = []
    for time in TIMES:
        where = os.path.join(out, time)
        os.makedirs(where, exist_ok=True)
        # No --world: with it the viewer opens the whole map rather than its stage.
        command = [os.path.join(ROOT, "viewer.sh"), "--category", category,
                   "--pick", pick, "--time", time, "--frames", "90", "--shot", "89",
                   "--shot-path", where, "--log", os.path.join(out, f"{time}.log")]
        # Close enough to read a material on what is worn and held; a world object at MU's
        # own distance, which is how the town shows it.
        if kind in ("set", "arm", "figure"):
            command += ["--dist", "3.5"]
        subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        frame = os.path.join(where, "00089.png")
        if os.path.exists(frame):
            frames.append((time, frame))
    try:
        from PIL import Image, ImageDraw
        tiles = []
        for time, frame in frames:
            image = Image.open(frame).convert("RGB")
            w, h = image.size
            tile = image.crop((int(w * 0.18), 0, int(w * 0.82), h)).resize((820, 720))
            ImageDraw.Draw(tile).text((8, 8), f"{name}  {time}", fill=(255, 255, 0))
            tiles.append(tile)
        if tiles:
            sheet = Image.new("RGB", (820 * len(tiles), 720))
            for i, tile in enumerate(tiles):
                sheet.paste(tile, (i * 820, 0))
            path = os.path.join(out, "sheet.png")
            sheet.save(path)
            print(f"cook_one: shots -> {os.path.relpath(path, ROOT)}")
            return path
    except ImportError:
        pass
    print(f"cook_one: shots -> {os.path.relpath(out, ROOT)}")
    return out


# --------------------------------------------------------------------------- the log

HEADER = """# Cook log

One line per item cooked with `tools/cook_one.py`: when, what, and whether its shots passed
a look. An item is not done when it is cooked; it is done when this says `passed`. Mark it
with `tools/cook_one.py --pass NAME "note"` or `--fail NAME "note"`.

| item | kind | cooked | look | note |
|---|---|---|---|---|
"""


def log_rows():
    if not os.path.exists(LOG):
        return []
    rows = []
    with open(LOG) as handle:
        for line in handle:
            if line.startswith("| ") and not line.startswith("| item") and "---" not in line:
                rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows


def write_log(rows):
    with open(LOG, "w") as handle:
        handle.write(HEADER)
        for row in rows:
            handle.write("| " + " | ".join(row) + " |\n")


def log(name, kind=None, look=None, note=None):
    rows = log_rows()
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    for row in rows:
        if lower(row[0]) == lower(name):
            if kind:
                row[1], row[2], row[3], row[4] = kind, now, "awaiting", ""
            if look:
                row[3], row[4] = look, note or ""
            write_log(rows)
            return True
    if not kind:
        print(f"cook_one: {name} is not in the log; cook it first", file=sys.stderr)
        return False
    rows.append([name, kind, now, "awaiting", ""])
    write_log(rows)
    return True


# --------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name", nargs="?")
    parser.add_argument("--world", default="lorencia")
    parser.add_argument("--texcook", default=os.path.join(ROOT, "build", "texcook"))
    parser.add_argument("--no-shots", action="store_true")
    parser.add_argument("--pass", dest="passed", nargs=2, metavar=("NAME", "NOTE"))
    parser.add_argument("--fail", dest="failed", nargs=2, metavar=("NAME", "NOTE"))
    args = parser.parse_args()

    if args.passed:
        return 0 if log(args.passed[0], look="passed", note=args.passed[1]) else 1
    if args.failed:
        return 0 if log(args.failed[0], look="FAILED", note=args.failed[1]) else 1
    if not args.name:
        parser.error("name what to cook")
    if not os.path.exists(args.texcook):
        print(f"cook_one: {args.texcook} is not built. cmake --build build --target texcook",
              file=sys.stderr)
        return 2

    kind, area, meshes, extra = resolve(args.name, args.world)
    if kind is None:
        print(f"cook_one: nothing called {args.name}: not an armour set, a weapon, a figure "
              f"or a {args.world} object", file=sys.stderr)
        return 1
    print(f"cook_one: {args.name} is a {kind} in {area}: {', '.join(sorted(meshes))}")
    if cook_item(kind, area, meshes, extra, args.texcook) is None:
        return 1
    report(meshes)
    log(args.name, kind=kind)
    if not args.no_shots:
        shoot(kind, args.name, extra if kind in ("set", "arm") else None, args.world)
    print(f"cook_one: logged in {os.path.relpath(LOG, ROOT)} as awaiting a look")
    return 0


if __name__ == "__main__":
    sys.exit(main())
