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
import os
import re
import sqlite3
import subprocess
import time

from things import (ASSETS, COOKED, PIPELINE, ROOT, SOURCE, WORKSHOP, cooked_worlds,
                    world_recipes)

LOCKS = os.path.join(WORKSHOP, ".mu")
TOOLS = os.path.join(ROOT, "tools")
COOK = ["python3", os.path.join(TOOLS, "cook.py")]
DB = os.path.join(SOURCE, "mu.db")


def mtime(path):
    try:
        return os.path.getmtime(path)
    except (OSError, TypeError):
        return 0.0


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
            if path and mtime(path) > built:
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
        for thing in things:
            if newest([thing.recipe, thing.glb()]) > at:
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


def tables(threads):
    return [Step(f"tables {w}", COOK + ["--world", w, "--only", "tables"],
                 cook_test([os.path.join(COOKED, w, w + ".mur")],
                           [os.path.join(ASSETS, "index.json")], f"{w}.mur"),
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
                       [os.path.join(ASSETS, "index.json")], f"{world}.mur"),
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
        steps += tables(threads)
        steps.append(Step(f"figure {thing.name}",
                          COOK + ["--world", thing.world, "--only", "figures", "--monsters", thing.name] + t,
                          cook_test([os.path.join(COOKED, "figures", "figures.json" if thing.world == "lorencia"
                                                  else f"figures_{thing.world}.json")],
                                    [glb, os.path.join(ASSETS, "index.json")], "the figures table"),
                          "cooked", lock="figures",
                          after=jobs(os.path.join(COOKED, "figures", "jobs.txt"), 60)))
    else:
        steps.append(Step(f"cook_one {thing.name}",
                          ["python3", os.path.join(TOOLS, "cook_one.py"), thing.name, "--no-shots",
                           "--world", thing.world],
                          always("cook_one keeps no date of its own"), "cooked", lock=thing.world))
    return steps


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


def plan_sounds(threads):
    sounds = os.path.join(SOURCE, "sounds")
    mus = os.path.join(COOKED, "showing", "showing.mus")
    return [
        Step("index", ["python3", os.path.join(PIPELINE, "index.py"), SOURCE, WORKSHOP],
             lambda c: ((newest_in(sounds, ".json") > mtime(os.path.join(WORKSHOP, "index.json")),
                         "sounds.json against the index")), "index"),
        Step("sync", ["python3", os.path.join(TOOLS, "sync.py"), "--src", WORKSHOP, "--dst", ASSETS,
                      "--world", "lorencia", "--only-world"], sync_test([]), "assets"),
        Step("showing", COOK + ["--only", "showing", "--threads", str(threads)],
             lambda c: ((bool(c & {"index", "assets"}) or newest_in(sounds, ".wav") > mtime(mus),
                         "a sound is newer than showing.mus, or the index moved")), "cooked", lock="showing"),
    ]


def plan_tables(threads):
    return tables(threads)


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


def run(steps, dry, say, logs, env=None):
    """Walks the plan. Returns (ok, results). In dry mode a needed step is assumed to change its tag."""
    changed, results = set(), []
    for step in steps:
        needed, reason = step.test(changed)
        result = dict(step.result, needed=needed, reason=reason)
        if step.name.startswith("build "):
            result["cmd"] = " ".join(build_cmd(step))
        results.append(result)
        mark = "run " if needed else "skip"
        say(f"{mark} {step.name:<22} {reason}")
        if not needed:
            continue
        if dry:
            changed.add(step.makes)
            continue
        started = time.time()
        with Lock(step.lock, say):
            before = mtime(step.thing.glb()) if step.name.startswith("build ") else None
            if step.name == "checkpoint mu.db":
                checkpoint()
                code = 0
            else:
                cmd = build_cmd(step) if step.name.startswith("build ") else step.cmd
                os.makedirs(logs, exist_ok=True)
                log = os.path.join(logs, step.name.replace(" ", "_").replace("/", "_") + ".log")
                with open(log, "w") as out:
                    code = subprocess.run(["nice", "-n", "10"] + cmd, stdout=out,
                                          stderr=subprocess.STDOUT, cwd=ROOT, env=env).returncode
                result["log"] = os.path.relpath(log, ROOT)
        result.update(seconds=round(time.time() - started, 1), code=code)
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
        say(f"     done in {result['seconds']} s")
    return True, results
