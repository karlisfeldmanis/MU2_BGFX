"""`mu check`: what can say no in seconds, without a window or a build.

The code's two gates (layercheck, sizecheck) and the content's silent faults, each of which
was once found late, in a shot or by the user:

  wal        mu.db writes sitting in the WAL, which the pipeline sees and git does not
  index      workshop/index.json older than a built .glb: a --build-only that never indexed
  sync       assets/index.json behind workshop's: cooks reading yesterday's index
  white      a cooked .mum with no texture links: meshes cooked before textures
  tables     an item in index.json missing from a world's .mur: new items cook every world

`cmake --build build --target checks` is still the full gate (the tests, matcheck). This is
the quick one, for before a cook or a commit.
"""

import json
import os
import subprocess

from things import ASSETS, COOKED, ROOT, SOURCE, WORKSHOP, cooked_worlds


def mtime(path):
    try:
        return os.path.getmtime(path)
    except OSError:
        return 0.0


def code_gate(script):
    done = subprocess.run(["python3", os.path.join(ROOT, "tools", script)], capture_output=True,
                          text=True, cwd=ROOT)
    lines = (done.stdout + done.stderr).strip().splitlines()
    return done.returncode == 0, lines[0] if lines else "", lines[1:]


def wal():
    path = os.path.join(SOURCE, "mu.db-wal")
    size = os.path.getsize(path) if os.path.exists(path) else 0
    if size:
        return False, f"{size} bytes of mu.db writes in the WAL; `mu make` checkpoints, or PRAGMA wal_checkpoint(TRUNCATE)", []
    return True, "mu.db's WAL is empty", []


def index():
    at = mtime(os.path.join(WORKSHOP, "index.json"))
    late = []
    for folder, _dirs, files in os.walk(WORKSHOP):
        if os.sep + "." in folder or os.sep + "logs" in folder:
            continue
        for name in files:
            if name.endswith(".glb") and not name.endswith(".actions.glb"):
                path = os.path.join(folder, name)
                if mtime(path) > at:
                    late.append(os.path.relpath(path, WORKSHOP))
    if late:
        return False, f"{len(late)} .glb built after the index was written; run pipeline/index.py", late[:10]
    return True, "the index is newer than every build", []


def sync():
    work, here = mtime(os.path.join(WORKSHOP, "index.json")), mtime(os.path.join(ASSETS, "index.json"))
    if work > here:
        return False, "assets/index.json is older than workshop's; sync before cooking", []
    return True, "assets/index.json is current", []


def white():
    bad = []
    for world in cooked_worlds():
        mark = f"cooked/{world}/textures".encode()
        folder = os.path.join(COOKED, world, "meshes")
        textures = mtime(os.path.join(COOKED, world, "textures.json"))
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            path = os.path.join(folder, name)
            # Only meshes cooked before the manifest that should have fed them: an untextured
            # mesh cooked after it is a model that wears no sheet, which some do.
            if not name.endswith(".mum") or mtime(path) >= textures:
                continue
            with open(path, "rb") as handle:
                if mark not in handle.read():
                    bad.append(f"{world}/{name}")
    if bad:
        return False, f"{len(bad)} meshes cooked before their world's textures, with no links", bad[:10]
    return True, "no mesh is older than its world's textures and unlinked", []


def tables():
    try:
        with open(os.path.join(ASSETS, "index.json")) as handle:
            objects = json.load(handle).get("objects", [])
    except (OSError, ValueError):
        return False, "assets/index.json unreadable", []
    items = sorted({os.path.splitext(os.path.basename(o["glb"]))[0] for o in objects
                    if o.get("kind") in ("weapon", "armor", "shield") and o.get("glb")})
    missing = []
    for world in cooked_worlds():
        with open(os.path.join(COOKED, world, world + ".mur"), "rb") as handle:
            blob = handle.read()
        gone = [i for i in items if i.encode() not in blob]
        if gone:
            missing.append(f"{world}: {len(gone)} missing, e.g. {', '.join(gone[:4])}")
    if missing:
        return False, "items in index.json missing from a world's tables; `mu make tables`", missing
    return True, f"all {len(items)} items in every world's tables", []


def realm():
    """The realm's compiled gate and world tables against the world manifests
    (pipeline/realm_tables.py)."""
    done = subprocess.run(["python3", os.path.join(ROOT, "pipeline", "realm_tables.py"), "--check"],
                          capture_output=True, text=True, cwd=ROOT)
    lines = (done.stdout + done.stderr).strip().splitlines()
    return done.returncode == 0, lines[0] if lines else "", lines[1:]


CHECKS = [
    ("layers", lambda: code_gate("layercheck.py")),
    ("sizes", lambda: code_gate("sizecheck.py")),
    ("wal", wal),
    ("index", index),
    ("sync", sync),
    ("white", white),
    ("tables", tables),
    ("realm", realm),
]


def run(say):
    results, ok = [], True
    for name, check in CHECKS:
        passed, line, detail = check()
        ok = ok and passed
        results.append({"check": name, "ok": passed, "says": line, "detail": detail})
        say(f"{'ok  ' if passed else 'FAIL'} {name:<7} {line}")
        for more in detail:
            say(f"             {more}")
    return ok, results
