"""`mu make`: one thing from its recipe to the cooked files the game loads, in the one order that works.

The chain is source -> workshop (asset.sh, Blender) -> index (pipeline/index.py) -> assets
(sync.py) -> cooked (cook.py / cook_one.py). Each of those scripts is right on its own; what
went wrong, again and again, was the order and the steps between them, and every one of those
was learned the hard way and kept only in memory. Here each is an edge:

  - a sheet newer than the built .glb rebuilds with --rebuild: the x3 upscale cache is keyed by
    the sheet's name, so a plain build reports success and keeps the old art;
  - a mu.db write is checkpointed out of the WAL before the index reads it;
  - the index runs after any build: --build-only never ran it;
  - sync copies workshop -> assets after the index, index.json whole: by hand, spawns were
    merged into assets/index.json and figures cooked before the merge drew as nothing;
  - textures cook before meshes: meshes first wrote .mum files with no texture links, white;
  - a new item cooks the wardrobe and every world's tables, not only the one in hand;
  - a monster's tables cook in every world before its figure.

Every step says why it runs or why it does not, so `mu make X --plan` is the answer to "what
will this cost" before it costs anything. Cooks take a lock per world (workshop/.mu/), so two
sessions using mu never write one world's cook at once; a script run by hand does not take it.
"""

import fcntl
import json
import os
import re
import sqlite3
import subprocess
import time

from recipe_dates import BUILD_IGNORES, REALM_IGNORES, recipe_time
from things import (ASSETS, BUILD, COOKED, PIPELINE, ROOT, SOURCE, TOOLS, WORKSHOP, cooked_worlds,
                    world_recipes)

LOCKS = os.path.join(WORKSHOP, ".mu")
COOK = ["python3", os.path.join(TOOLS, "cook.py")]
DB = os.path.join(SOURCE, "mu.db")


def mtime(path, without=("mu_checklist",)):
    try:
        when = os.path.getmtime(path)
    except (OSError, TypeError):
        return 0.0
    if path.endswith(".json") and os.path.dirname(path) == os.path.join(SOURCE, "monsters"):
        return recipe_time(path, when, without)
    return when


def newest(paths):
    return max((mtime(p) for p in paths), default=0.0)


def newest_in(folder, suffix):
    try:
        return max((mtime(os.path.join(folder, f)) for f in os.listdir(folder) if f.endswith(suffix)),
                   default=0.0)
    except OSError:
        return 0.0


def build_scripts():
    """The pipeline scripts asset.sh runs, read out of asset.sh itself so the list cannot drift.

    asset.sh's own gate rebuilds when ANY pipeline script is newer, so an edit to terrain.py or
    worklist.py made every model in the tree stale -- minutes of Blender each, for nothing.
    mu asks only about the scripts a build runs; index.py is left out, it is the next step.
    """
    with open(os.path.join(TOOLS, "asset.sh")) as handle:
        names = set(re.findall(r"pipeline/([a-z_]+\.py)", handle.read()))
    return [os.path.join(PIPELINE, n) for n in sorted(names - {"index.py"})]


_changed = {}


def changed_at(path):
    """When a tracked script last changed: its commit's time if the copy is clean, else its date.

    A file's date lies for scripts: a checkout, a subtree split or an editor that saves without
    a change moves it, and asset.sh's date rule then rebuilds every model for nothing.
    tiled_maps.py's date said 2026-10-11 when its last change was the 09-30 commit.
    """
    if path not in _changed:
        rel = os.path.relpath(path, ROOT)
        dirty = subprocess.run(["git", "diff", "--quiet", "HEAD", "--", rel], cwd=ROOT).returncode
        when = mtime(path)
        if dirty == 0:
            out = subprocess.run(["git", "log", "-1", "--format=%ct", "--", rel], cwd=ROOT,
                                 capture_output=True, text=True).stdout.strip()
            when = float(out) if out else when
        _changed[path] = when
    return _changed[path]


def newest_file(folder, suffix):
    try:
        files = [os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(suffix)]
    except OSError:
        return None
    return max(files, key=mtime, default=None)


class Step:
    """One command, a test that says whether it is needed and why, and a tag for what it changes."""

    def __init__(self, name, cmd, test, makes, lock=None, after=None):
        self.name, self.cmd, self.test, self.makes = name, cmd, test, makes
        self.lock, self.after = lock, after
        self.result = {"step": name, "cmd": " ".join(cmd) if cmd else ""}


# ----------------------------------------------------------------------------- the tests

def build_test(thing):
    def test(changed):
        glb = thing.glb()
        if glb is None:
            return True, "never built"
        built = mtime(glb)
        sheets = [s for s in thing.sheets() if mtime(s) > built]
        if sheets:
            thing.rebuild = True
            names = ", ".join(os.path.basename(s) for s in sheets[:3])
            return True, f"sheet newer than the build ({names}): --rebuild, the upscale cache is keyed by name"
        for path in [thing.recipe, thing.obj(), thing.rig()]:
            if path and mtime(path, BUILD_IGNORES) > built:
                return True, f"{os.path.relpath(path, ROOT)} newer than the build"
        script = max(build_scripts(), key=changed_at, default=None)
        if script and changed_at(script) > built:
            return True, f"pipeline/{os.path.basename(script)}, which the build runs, is newer than it"
        profile = newest_file(os.path.join(SOURCE, "profiles"), ".json")
        if profile and mtime(profile) > built:
            return True, f"profiles/{os.path.basename(profile)} is newer than the build"
        if newest_in(os.path.join(SOURCE, "materials"), ".json") > built:
            return True, "the material library is newer than the build"
        return False, "built, nothing newer"
    return test


def wal_test():
    def test(changed):
        if os.path.exists(DB + "-wal") and os.path.getsize(DB + "-wal") > 0:
            return True, "mu.db has writes in its WAL that the file and git do not see"
        return False, "mu.db's WAL is empty"
    return test


def index_test(things):
    def test(changed):
        index = os.path.join(WORKSHOP, "index.json")
        if "glb" in changed:
            return True, "a build ran"
        if not os.path.exists(index):
            return True, "workshop/index.json is missing"
        at = mtime(index)
        if mtime(DB) > at:
            return True, "source/mu.db is newer than the index"
        # A world's monster nests (sprint E3) are read by the index too.
        worlds = os.path.join(SOURCE, "world")
        for world in sorted(os.listdir(worlds)):
            nests = os.path.join(worlds, world, "nests.json")
            if os.path.exists(nests) and mtime(nests) > at:
                return True, f"{world}'s nests.json is newer than the index"
        # pipeline/index.py reads neither a recipe's show, its rules nor its checklist.
        for thing in things:
            if max(mtime(thing.recipe, BUILD_IGNORES), mtime(thing.glb())) > at:
                return True, f"{thing.name} is newer than the index"
        return False, "the index is newer than every input"
    return test


def sync_test(things):
    def test(changed):
        if changed & {"glb", "index"}:
            return True, "the workshop changed"
        if mtime(os.path.join(WORKSHOP, "index.json")) > mtime(os.path.join(ASSETS, "index.json")):
            return True, "assets/index.json is behind workshop's"
        for thing in things:
            rel = thing.glb_rel()
            if rel and mtime(thing.glb()) > mtime(os.path.join(ASSETS, rel)):
                return True, f"assets/ holds an older {thing.name}.glb than workshop/"
        return False, "assets/ matches workshop/ for these"
    return test


def cook_test(outputs, inputs, label):
    """Needed when something upstream changed, or an output is missing or older than an input."""
    def test(changed):
        if changed & {"glb", "index", "assets"}:
            return True, "upstream changed"
        outs = [o() if callable(o) else o for o in outputs]
        missing = [o for o in outs if not os.path.exists(o)]
        if missing:
            return True, f"{os.path.relpath(missing[0], ROOT)} is missing"
        ins = [i() if callable(i) else i for i in inputs]
        if newest(ins) > min(mtime(o) for o in outs):
            return True, f"{label} is older than what it is cooked from"
        return False, f"{label} is newer than its inputs"
    return test


def always(reason):
    return lambda changed: (True, reason)


# ----------------------------------------------------------------------------- the checks after a cook

def textured(world, names):
    """The white-mesh trap: each .mum must link the world's textures."""
    def check():
        mark = f"cooked/{world}/textures".encode()
        white = []
        for name in names:
            path = os.path.join(COOKED, world, "meshes", name + ".mum")
            if os.path.exists(path):
                with open(path, "rb") as handle:
                    if mark not in handle.read():
                        white.append(name)
        return f"no texture links (white) in {', '.join(white)}" if white else None
    return check


def jobs(path, expect):
    """The 'cook only what changed' trap: a cook compressing far more than its job is a missing skip."""
    def check():
        try:
            with open(path) as handle:
                count = sum(1 for _ in handle)
        except OSError:
            return None
        if count > expect:
            return f"{count} images went to the compressor ({os.path.relpath(path, ROOT)}); expected a handful"
        return None
    return check


# ----------------------------------------------------------------------------- the plans

def head(things, threads):
    """Build, checkpoint, index, sync: the same for every kind."""
    steps = []
    for thing in things:
        thing.rebuild = False
        step = Step(f"build {thing.build_name}", None, build_test(thing), "glb")
        step.thing = thing
        steps.append(step)
    steps.append(Step("checkpoint mu.db", None, wal_test(), "db", after=None))
    steps.append(Step("index", ["python3", os.path.join(PIPELINE, "index.py"), SOURCE, WORKSHOP],
                      index_test(things), "index"))
    worlds = sorted({t.world for t in things})
    sync = ["python3", os.path.join(TOOLS, "sync.py"), "--src", WORKSHOP, "--dst", ASSETS]
    for world in worlds:
        sync += ["--world", world]
    steps.append(Step("sync", sync + ["--only-world"], sync_test(things), "assets"))
    return steps


def manifest(world):
    """The world's manifest, source/world/<w>/world.json: its tables read its townsfolk (`npcs`)
    and its safe gate, so a townsperson moved in the studio's Place makes them stale."""
    return os.path.join(SOURCE, "world", world, "world.json")


def tables(threads):
    return [Step(f"tables {w}", COOK + ["--world", w, "--only", "tables"],
                 cook_test([os.path.join(COOKED, w, w + ".mur")],
                           [os.path.join(ASSETS, "index.json"), manifest(w)], f"{w}.mur"),
                 "tables", lock=w) for w in cooked_worlds()]


def world_cook(world, things, threads):
    t = ["--threads", str(threads)]
    meshes = [os.path.join(COOKED, world, "meshes", x.name + ".mum") for x in things]
    glbs = [lambda x=x: os.path.join(ASSETS, x.glb_rel() or "") for x in things]
    return [
        Step(f"textures {world}", COOK + ["--world", world, "--only", "textures"] + t,
             cook_test([os.path.join(COOKED, world, "textures.json")], glbs, "textures.json"),
             "cooked", lock=world, after=jobs(os.path.join(COOKED, world, "jobs.txt"), 60)),
        Step(f"meshes {world}", COOK + ["--world", world, "--only", "meshes"],
             cook_test(meshes, glbs + [os.path.join(COOKED, world, "textures.json")], "every mesh"),
             "cooked", lock=world, after=textured(world, [x.name for x in things])),
        Step(f"placements {world}", COOK + ["--world", world, "--only", "placements"],
             cook_test([os.path.join(COOKED, world, world + ".mut")],
                       [os.path.join(ASSETS, "index.json")] + meshes, f"{world}.mut"),
             "cooked", lock=world),
        Step(f"tables {world}", COOK + ["--world", world, "--only", "tables"],
             cook_test([os.path.join(COOKED, world, world + ".mur")],
                       [os.path.join(ASSETS, "index.json"), manifest(world)], f"{world}.mur"),
             "cooked", lock=world),
    ]


def cook_one_name(thing):
    """cook_one answers an armour piece by its set's suffix (Male07), anything else by its file."""
    for piece in ("Helm", "Armor", "Pant", "Glove", "Boot"):
        if thing.name.startswith(piece) and thing.name[len(piece):]:
            return thing.name[len(piece):]
    return thing.name


def plan_thing(thing, threads):
    left = [k for k, v in (thing.doc.get("sheet_materials") or {}).items() if v == "CHOOSE"]
    if left:
        from things import NotFound
        library = sorted(f[:-5] for f in os.listdir(os.path.join(SOURCE, "materials")) if f.endswith(".json"))
        raise NotFound(f"{thing.rel}: choose a material for {', '.join(left)} "
                       f"(sheet_materials) from {', '.join(library)}")
    steps = head([thing], threads)
    t = ["--threads", str(threads)]
    glb = lambda: os.path.join(ASSETS, thing.glb_rel() or "")
    if thing.kind == "object":
        steps += world_cook(thing.world, [thing], threads)
    elif thing.kind == "item":
        steps.append(Step(f"cook_one {thing.name}",
                          ["python3", os.path.join(TOOLS, "cook_one.py"), cook_one_name(thing), "--no-shots"],
                          cook_test([os.path.join(COOKED, "wardrobe", "wardrobe.json")], [glb], "the wardrobe entry"),
                          "cooked", lock="wardrobe"))
        steps.append(Step("wardrobe", COOK + ["--only", "wardrobe"] + t,
                          cook_test([os.path.join(COOKED, "wardrobe", "wardrobe.json")], [glb], "the wardrobe"),
                          "cooked", lock="wardrobe",
                          after=jobs(os.path.join(COOKED, "wardrobe", "jobs.txt"), 60)))
        steps += tables(threads)
    elif thing.kind == "monster":
        # Its rules (src/sim/breeds_table.inc, out of every recipe's `rules`) and its show
        # (cooked/figures/shows.json: aura, eye trails, stars, claws...) are plan_realm's two
        # steps: either edit is nothing in a fight without them. The rules are compiled, so
        # the game is built after them (build_game) -- the arena window then fights by them.
        steps += plan_realm()
        steps += tables(threads)
        steps.append(Step(f"figure {thing.name}",
                          COOK + ["--world", thing.world, "--only", "figures", "--monsters", thing.name] + t,
                          cook_test([os.path.join(COOKED, "figures", "figures.json" if thing.world == "lorencia"
                                                  else f"figures_{thing.world}.json")],
                                    [glb, os.path.join(ASSETS, "index.json")], "the figures table"),
                          "cooked", lock="figures",
                          after=jobs(os.path.join(COOKED, "figures", "jobs.txt"), 60)))
        steps.append(build_game(threads))
    else:
        steps.append(Step(f"cook_one {thing.name}",
                          ["python3", os.path.join(TOOLS, "cook_one.py"), thing.name, "--no-shots",
                           "--world", thing.world],
                          always("cook_one keeps no date of its own"), "cooked", lock=thing.world))
    return steps


def plan_set(suffix, threads, say):
    """An armour set by the game's name for it, Set<suffix> (SetMale16, what the studio's
    Armour sets tab has in hand): each of its pieces built, then the set cooked once --
    cook_one takes a set by its suffix, and the wardrobe and the tables follow it once."""
    from things import NotFound, find
    pieces = []
    for piece in ("Helm", "Armor", "Pant", "Glove", "Boot"):
        try:
            pieces.append(find(piece + suffix))
        except NotFound:
            continue
    if not pieces:
        raise NotFound(f"no piece of the set {suffix} under source/ (Helm{suffix}, Armor{suffix}, ...)")
    say(f"set {suffix}: {', '.join(p.name for p in pieces)}")
    steps = plan_thing(pieces[0], threads)
    builds = head(pieces, threads)
    # The pieces' builds in place of the first piece's alone; its cook steps after them.
    first_cook = next(i for i, s in enumerate(steps) if not s.name.startswith(("build ", "checkpoint", "index", "sync")))
    return builds + steps[first_cook:]


def plan_placements(world, threads):
    """`mu make placements <w>`: the world's placed objects after they were moved (the studio's
    Place, F6). The cook reads assets/world/<w>/<w>.json, which the index makes out of
    source/world/<w>/<w>.json, so: the index, the sync, the placements. No model is built.

    And the townsfolk's figures: the index adds placements.json's `spawns` to the placed
    objects, and the figure cook stands a townsperson only when its model is placed (Aida's
    Amy and Baz, 2026-10-12, were in its tables but drawn by no one). --no-monsters keeps the
    world's breeds as they were cooked."""
    source = os.path.join(SOURCE, "world", world, f"{world}.json")
    if not os.path.exists(source):
        from things import NotFound
        raise NotFound(f"no world source/world/{world}/{world}.json")
    spawns = os.path.join(SOURCE, "world", world, "placements.json")
    newest = lambda: max(mtime(source), mtime(spawns))
    index_json = os.path.join(WORKSHOP, "index.json")
    placed = os.path.join(ASSETS, "world", world, f"{world}.json")
    return [
        Step("index", ["python3", os.path.join(PIPELINE, "index.py"), SOURCE, WORKSHOP],
             lambda c: ((newest() > mtime(index_json),
                         f"{world}.json or placements.json is newer than the index"
                         if newest() > mtime(index_json)
                         else "the index is newer than the placements")), "index"),
        Step("sync", ["python3", os.path.join(TOOLS, "sync.py"), "--src", WORKSHOP, "--dst", ASSETS,
                      "--world", world, "--only-world"],
             lambda c: ((bool(c & {"index"}) or newest() > mtime(placed),
                         "the workshop changed" if c & {"index"} else "assets/ against the source")),
             "assets"),
        Step(f"placements {world}", COOK + ["--world", world, "--only", "placements"],
             cook_test([os.path.join(COOKED, world, world + ".mut")], [placed], f"{world}.mut"),
             "cooked", lock=world),
        Step(f"townsfolk {world}", COOK + ["--world", world, "--only", "figures", "--no-monsters"],
             cook_test([os.path.join(COOKED, "figures", "figures.json" if world == "lorencia"
                                     else f"figures_{world}.json")], [placed], "the figures table"),
             "cooked", lock="figures", after=jobs(os.path.join(COOKED, "figures", "jobs.txt"), 60)),
    ]


def plan_world(world, threads):
    things = world_recipes(world)
    return head(things, threads) + world_cook(world, things, threads)


# ----------------------------------------------------------------------------- the land

GRIDS = ("height.png", "tiles.png", "attributes.png", "light.png", "blend_keep.png")


def plan_ground(world, folder, threads, reimport=False):
    """A world's bare land: terrain import, the tile sheets, the ground, then index, sync, cook.

    docs/aida-port.md step 1 and the cook-trap memories, in order:
      - terrain.py writes the grids and <w>.json into source/ from MU's Data/World<N>. It runs on
        a world never imported (--folder N), or when asked (--import). Never by itself: those
        files are tracked, and terrain.py's per-map tables change for one world at a time -- an
        Aida edit made every world look stale, and re-importing Lorencia for it would rewrite
        Lorencia's source. The plan says when terrain.py is newer; --import acts on it. The
        folder is <w>.json's own map_number after the first import;
      - tileset.py, then ground.py, never content.sh --world, which rebuilds every recipe;
      - the index after the grids: it is what copies them into workshop/ (the Lost Tower's lava
        stood on the old grids when it was skipped);
      - `--only ground` on a world with no objects yet, `--only textures` (the ground included)
        on one that has them: `--only ground` writes a manifest of the ground alone, and a mesh
        cooked against it is white;
      - every world's tables, since a gate joins two.
    """
    from new import data_root, world_doc
    src = os.path.join(SOURCE, "world", world)
    work = os.path.join(WORKSHOP, "world", world)
    ground_json = os.path.join(src, "ground.json")
    if not os.path.exists(ground_json):
        raise NotFound_(f"no {os.path.relpath(ground_json, ROOT)}: the tile sheets are named there, by hand")
    grids = [os.path.join(src, g) for g in GRIDS]
    placed = os.path.join(src, f"{world}.json")
    if folder is None and os.path.exists(placed):
        folder = world_doc(world).get("map_number")
    steps = []
    if folder is not None:
        def terrain_test(changed):
            if not os.path.exists(placed):
                return True, "never imported"
            if reimport:
                return True, f"--import: World{folder} read again"
            if changed_at(os.path.join(PIPELINE, "terrain.py")) > mtime(placed):
                return False, "terrain.py changed since the import; --import re-reads it (rewrites source/world/" + world + ")"
            return False, f"imported from World{folder}, terrain.py unchanged since"
        steps.append(Step("terrain", ["python3", os.path.join(PIPELINE, "terrain.py"),
                                      os.path.join(data_root(), f"World{folder}"), world,
                                      os.path.join(SOURCE, "world"), str(folder)],
                          terrain_test, "grids"))
    slots = os.path.join(work, "tiles_slots.json")
    glb = os.path.join(work, f"{world}_ground.glb")

    def tileset_test(changed):
        if "grids" in changed:
            return True, "the terrain was imported"
        if not os.path.exists(slots):
            return True, "never built"
        tool = changed_at(os.path.join(PIPELINE, "tileset.py"))
        if max(mtime(ground_json), tool) > mtime(slots):
            which = "pipeline/tileset.py" if tool > mtime(ground_json) else "ground.json"
            return True, f"{which} is newer than the tile sheets"
        return False, "the tile sheets are newer than ground.json"

    def ground_test(changed):
        if changed & {"grids", "tiles"}:
            return True, "upstream changed"
        if not os.path.exists(glb):
            return True, "never built"
        ins = {p: mtime(p) for p in grids + [ground_json, slots]}
        ins[os.path.join(PIPELINE, "ground.py")] = changed_at(os.path.join(PIPELINE, "ground.py"))
        late = max(ins, key=ins.get)
        if ins[late] > mtime(glb):
            return True, f"{os.path.relpath(late, ROOT)} is newer than the ground"
        return False, f"{world}_ground.glb is newer than its grids"

    def index_ground(changed):
        if changed & {"grids", "tiles", "land"}:
            return True, "the land changed; the index copies the grids into workshop/"
        if newest(grids + [glb]) > mtime(os.path.join(WORKSHOP, "index.json")):
            return True, "a grid or the ground is newer than the index"
        return False, "the index is newer than the land"

    def sync_ground(changed):
        if changed & {"index", "land"}:
            return True, "the workshop changed"
        if mtime(glb) > mtime(os.path.join(ASSETS, "world", world, f"{world}_ground.glb")):
            return True, "assets/ holds an older ground"
        return False, "assets/ holds this ground"

    has_objects = os.path.isdir(os.path.join(COOKED, world, "meshes"))
    only = "textures" if has_objects else "ground"
    steps += [
        Step("tileset", ["python3", os.path.join(PIPELINE, "tileset.py"), ground_json, work], tileset_test, "tiles"),
        Step("ground", ["python3", os.path.join(PIPELINE, "ground.py"), src, work, os.path.join(SOURCE, "textures")],
             ground_test, "land"),
        Step("index", ["python3", os.path.join(PIPELINE, "index.py"), SOURCE, WORKSHOP], index_ground, "index"),
        Step("sync", ["python3", os.path.join(TOOLS, "sync.py"), "--src", WORKSHOP, "--dst", ASSETS,
                      "--world", world, "--only-world"], sync_ground, "assets"),
        Step(f"{only} {world}", COOK + ["--world", world, "--only", only, "--threads", str(threads)],
             cook_test([os.path.join(COOKED, world, "textures.json")],
                       [lambda: os.path.join(ASSETS, "world", world, f"{world}_ground.glb")], "textures.json"),
             "cooked", lock=world, after=jobs(os.path.join(COOKED, world, "jobs.txt"), 60)),
    ]
    if has_objects:
        steps.append(Step(f"placements {world}", COOK + ["--world", world, "--only", "placements"],
                          cook_test([os.path.join(COOKED, world, world + ".mut")],
                                    [os.path.join(ASSETS, "index.json")], f"{world}.mut"),
                          "cooked", lock=world))
    return steps + tables(threads)


def NotFound_(message):
    from things import NotFound
    return NotFound(message)


def newest_under(folder):
    """The newest file anywhere under `folder`, by date."""
    return max((os.path.getmtime(os.path.join(at, f)) for at, _, files in os.walk(folder) for f in files),
               default=0.0)


def plan_sounds(threads):
    sounds = os.path.join(SOURCE, "sounds")
    mus = os.path.join(COOKED, "showing", "showing.mus")

    # The effect sheets ride the same index (index.py's table of names, the PNGs under
    # source/effects): a new sheet for a monster's show made nothing stale, and the game
    # said `smoke NO` (2026-10-12, the Witch Queen's Chrome03 and fantaB).
    def index_stale(changed):
        at = mtime(os.path.join(WORKSHOP, "index.json"))
        if newest_in(sounds, ".json") > at:
            return True, "sounds.json is newer than the index"
        if changed_at(os.path.join(PIPELINE, "index.py")) > at:
            return True, "pipeline/index.py, its table of effect sheets, is newer than the index"
        if newest_under(os.path.join(SOURCE, "effects")) > at:
            return True, "an effect sheet under source/effects is newer than the index"
        return False, "the index is newer than sounds.json and the effect sheets"
    return [
        Step("index", ["python3", os.path.join(PIPELINE, "index.py"), SOURCE, WORKSHOP],
             index_stale, "index"),
        Step("sync", ["python3", os.path.join(TOOLS, "sync.py"), "--src", WORKSHOP, "--dst", ASSETS,
                      "--world", "lorencia", "--only-world"], sync_test([]), "assets"),
        Step("showing", COOK + ["--only", "showing", "--threads", str(threads)],
             # And sounds.json: an event added to it whose index a monster's cook ran since
             # (the Bloody Orc's, 2026-10-12) left the index fresh and showing.mus without it.
             lambda c: ((bool(c & {"index", "assets"})
                         or max(newest_in(sounds, ".wav"), newest_in(sounds, ".json")) > mtime(mus),
                         "a sound or sounds.json is newer than showing.mus, or the index moved")),
             "cooked", lock="showing"),
    ]


GAME = os.path.join(BUILD, "mu2")


def build_game(threads):
    """mu2 and mu2_server built again after the realm's tables changed: the rules are C++.

    Never netbot: rebuilding it under a running bot kills the bot. Under the `build` lock, so
    two mu jobs never build at once; a session building by hand does not take it, and a build
    racing another session's header edit mixes layouts (the shared-build trap: delete every
    mu*.dir object and build again).
    """
    tables_inc = [os.path.join(ROOT, "src", "sim", n)
                  for n in ("gates_table.inc", "maps_table.inc", "breeds_table.inc")]

    def test(changed):
        if "realm" in changed:
            return True, "the realm's tables were written"
        if newest(tables_inc) > mtime(GAME):
            return True, "a realm table is newer than build/mu2"
        return False, "build/mu2 is newer than the realm's tables"
    return Step("compile the game", ["cmake", "--build", BUILD, "--target", "mu2", "mu2_server",
                                   "-j", str(threads)], test, "binary", lock="build")


def plan_tables(threads):
    return tables(threads)


def plan_realm():
    """The realm's compiled tables, src/sim/gates_table.inc and maps_table.inc, out of every
    world's manifest's `gates` and `rules`, and breeds_table.inc out of every monster recipe's
    `rules` (pipeline/realm_tables.py). A rebuild of the game, the server and netbot follows by
    hand."""
    outs = [os.path.join(ROOT, "src", "sim", name)
            for name in ("gates_table.inc", "maps_table.inc", "breeds_table.inc")]
    script = os.path.join(PIPELINE, "realm_tables.py")
    manifests = [os.path.join(SOURCE, "world", w, "world.json") for w in sorted(os.listdir(os.path.join(SOURCE, "world")))]
    monsters = os.path.join(SOURCE, "monsters")
    manifests += [os.path.join(monsters, f) for f in sorted(os.listdir(monsters))
                  if f.endswith(".json") and not f.endswith(".rig.json")]
    # The script rewrites a table only when its text changes, so the game is not rebuilt for
    # nothing; the tables' own dates then lag every recipe edit that left the rules alone (a
    # note, a material, a checklist), and the realm read stale for ever. The stamp is when the
    # realm last ran.
    stamp = os.path.join(LOCKS, "realm.stamp")
    ran = max(min(mtime(out) for out in outs), mtime(stamp))
    stale = max([mtime(script)] + [mtime(m, REALM_IGNORES) for m in manifests
                                    if os.path.exists(m)]) > ran

    def stamped():
        os.makedirs(LOCKS, exist_ok=True)
        with open(stamp, "w") as handle:
            handle.write(time.strftime("%Y-%m-%d %H:%M:%S\n"))
        return None
    # And the client's half of a recipe: every monster's show tags, cooked/figures/shows.json
    # (sprint E4a), which game/figures.cpp reads.
    shows = os.path.join(ASSETS, "cooked", "figures", "shows.json")
    cook = os.path.join(TOOLS, "cook.py")
    shown = newest([cook] + [m for m in manifests if os.path.exists(m)]) > mtime(shows)
    return [Step("realm", ["python3", script],
                 lambda c: (stale, "a manifest or the script is newer than the realm's tables" if stale
                            else "the realm's tables are newer than every manifest"),
                 "realm", after=stamped),
            Step("shows", ["python3", cook, "--only", "shows"],
                 lambda c: (shown, "a recipe or the cook is newer than shows.json" if shown
                            else "shows.json is newer than every recipe"),
                 "shows")]


# ----------------------------------------------------------------------------- running

def build_cmd(step):
    cmd = [os.path.join(TOOLS, "asset.sh"), step.thing.build_name]
    if getattr(step.thing, "rebuild", False):
        cmd.append("--rebuild")
    return cmd


def checkpoint():
    with sqlite3.connect(DB) as db:
        db.execute("PRAGMA wal_checkpoint(TRUNCATE)")


class Lock:
    def __init__(self, name, say):
        self.name, self.say, self.handle = name, say, None

    def __enter__(self):
        if not self.name:
            return self
        os.makedirs(LOCKS, exist_ok=True)
        path = os.path.join(LOCKS, f"{self.name}.lock")
        self.handle = open(path, "a+")
        try:
            fcntl.flock(self.handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self.handle.seek(0)
            self.say(f"  waiting: {self.name} is being cooked by another mu ({self.handle.read().strip()})")
            fcntl.flock(self.handle, fcntl.LOCK_EX)
        self.handle.seek(0)
        self.handle.truncate()
        self.handle.write(f"pid {os.getpid()}")
        self.handle.flush()
        return self

    def __exit__(self, *exc):
        if self.handle:
            fcntl.flock(self.handle, fcntl.LOCK_UN)
            self.handle.close()


def plan(target, threads, say, folder=None, reimport=False):
    """The steps for what `mu make` was given: shared by the command and by `mu serve`."""
    from things import NotFound, find
    if target[0] == "ground" and len(target) == 2:
        return plan_ground(target[1], folder, threads, reimport)
    if target[0] == "world" and len(target) == 2:
        return plan_world(target[1], threads)
    if target[0] == "placements" and len(target) == 2:
        return plan_placements(target[1], threads)
    if target == ["sounds"]:
        return plan_sounds(threads)
    if target == ["tables"]:
        return plan_tables(threads)
    if target[0] == "tables" and len(target) == 2:
        # One world's: what a townsperson moved in the studio needs (its manifest's `npcs`).
        steps = [s for s in tables(threads) if s.name == f"tables {target[1]}"]
        if not steps:
            raise NotFound(f"no cooked world {target[1]}")
        return steps
    if target in (["realm"], ["gates"]):
        return plan_realm()
    if target == ["rehearsal"]:
        return plan_rehearsal()
    if len(target) == 1 and target[0].startswith("Set") and target[0][3:4].isupper():
        return plan_set(target[0][3:], threads, say)
    if len(target) == 1:
        thing = find(target[0])
        say(f"{thing.kind} {thing.build_name}, world {thing.world}: {thing.rel}")
        return plan_thing(thing, threads)
    raise NotFound(f"make takes one name, 'world <w>', 'ground <w>', 'sounds', 'tables' or 'realm', "
                   f"not {' '.join(target)}")


REHEARSE = """import sys, time
steps, label = int(sys.argv[1]), sys.argv[2]
for i in range(steps):
    print(f"@progress {i}/{steps} {label} {i + 1}")
    print(f"working on {label} {i + 1}")
    time.sleep(0.25)
sys.exit(int(sys.argv[3]) if len(sys.argv) > 3 else 0)
"""


def plan_rehearsal():
    """`mu make rehearsal`: three steps that print bars and cook nothing, for proving the job
    runner and for drawing the studio's jobs panel without touching a file the game reads."""
    script = ["python3", "-c", REHEARSE]
    return [Step("rehearse sheets", script + ["8", "sheet"], always("a rehearsal"), "rehearsal"),
            Step("rehearse compress", script + ["20", "compress"], always("a rehearsal"), "rehearsal"),
            Step("rehearse tables", script + ["4", "tables"], always("a rehearsal"), "rehearsal")]


# What a step says while it runs (the @progress lines) and how long steps take: progress.py.
from progress import (PROGRESS, TIMINGS, estimate, kind_of, quiet, read_timings,  # noqa: E402,F401
                      remember, stream)


def run(steps, dry, say, logs, env=None, on=None, stop=None):
    """Walks the plan. Returns (ok, results). In dry mode a needed step is assumed to change its tag.

    `on(kind, **fields)` hears the walk as events: step, progress, line, done (mu serve).
    """
    on = on or quiet
    changed, results = set(), []
    for index, step in enumerate(steps):
        if stop is not None and (stop.after or stop.now):
            say("stopped before " + step.name)
            on("stopped", step=index)
            return False, results
        needed, reason = step.test(changed)
        result = dict(step.result, needed=needed, reason=reason)
        if step.name.startswith("build "):
            result["cmd"] = " ".join(build_cmd(step))
        results.append(result)
        mark = "run " if needed else "skip"
        say(f"{mark} {step.name:<22} {reason}")
        if not needed:
            on("skip", step=index, name=step.name, reason=reason)
            continue
        if dry:
            changed.add(step.makes)
            continue
        on("step", step=index, name=step.name, reason=reason, estimate=estimate(step.name))
        started = time.time()
        with Lock(step.lock, lambda text: (say(text), on("line", step=index, text=text.strip()))):
            before = mtime(step.thing.glb()) if step.name.startswith("build ") else None
            if step.name == "checkpoint mu.db":
                checkpoint()
                code = 0
            else:
                cmd = build_cmd(step) if step.name.startswith("build ") else step.cmd
                os.makedirs(logs, exist_ok=True)
                log = os.path.join(logs, step.name.replace(" ", "_").replace("/", "_") + ".log")
                code = stream(cmd, log, env, on, index, stop)
                result["log"] = os.path.relpath(log, ROOT)
        result.update(seconds=round(time.time() - started, 1), code=code)
        if code == 0:
            remember(step.name, result["seconds"])
        on("done", step=index, seconds=result["seconds"], code=code, log=result.get("log"))
        if code != 0:
            say(f"     failed with {code}; see {result.get('log', step.name)}")
            return False, results
        if step.name.startswith("build "):
            if mtime(step.thing.glb()) != before:
                changed.add("glb")
        else:
            changed.add(step.makes)
        if step.after:
            warning = step.after()
            if warning:
                result["warning"] = warning
                say(f"     WARNING {warning}")
                on("warning", step=index, text=warning)
        say(f"     done in {result['seconds']} s")
    return True, results
