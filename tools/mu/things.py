"""What a name on the command line is: a recipe in source/, and what it takes to make it.

A Thing knows its kind (object, item, monster, npc, other), its world, its recipe, the files
the recipe is built from, and where the build puts its .glb. Nothing here runs a tool; make.py
reads these facts to decide what is stale.

The kind is the recipe's folder, not something the caller repeats:

    source/world/<w>/<Name>.json        object   built as "<w>/<Name>", cooked in <w>
    source/items/<category>/<Name>.json item     cooked into the wardrobe and every world's tables
    source/monsters/<Name>.json         monster  cooked as a figure in the recipe's `world`
    source/npc/<Name>.json              npc      cooked by cook_one
    anything else                       other    cooked by cook_one
"""

import glob
import json
import os
import sys

# The folders are tools/paths.py's, one level up; mu's modules take them from here.
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from paths import (ASSETS, BUILD, COOKED, PIPELINE, ROOT, SHEETS, SHOTS, TOOLS,  # noqa: E402,F401
                   WORKSHOP, CONTENT as SOURCE)

# Folders under source/ that hold no recipes of their own.
NOT_RECIPES = {"textures", "materials", "profiles", "sounds", "voice", "music", "interface",
               "grades", "lobby"}


class NotFound(Exception):
    pass


class Thing:
    def __init__(self, recipe):
        self.recipe = recipe
        rel = os.path.relpath(recipe, SOURCE)
        self.rel = rel
        parts = rel.split(os.sep)
        self.name = os.path.splitext(parts[-1])[0]
        with open(recipe) as handle:
            self.doc = json.load(handle)
        self.world = self.doc.get("world") or "lorencia"
        if parts[0] == "world" and len(parts) == 3:
            self.kind, self.world = "object", parts[1]
            self.build_name = f"{parts[1]}/{self.name}"
        elif parts[0] == "items":
            self.kind, self.build_name = "item", self.name
        elif parts[0] == "monsters":
            self.kind, self.build_name = "monster", self.name
        elif parts[0] == "npc":
            self.kind, self.build_name = "npc", self.name
        else:
            self.kind, self.build_name = "other", self.name
        self.folder = os.path.dirname(recipe)

    # ------------------------------------------------------------------ what it is made from

    def obj(self):
        path = os.path.join(self.folder, self.name + ".obj")
        return path if os.path.exists(path) else None

    def rig(self):
        path = os.path.join(self.folder, self.name + ".rig.json")
        return path if os.path.exists(path) else None

    def sheets(self):
        """Every source/textures/*.png the recipe names, by any key."""
        found = set()

        def walk(node):
            if isinstance(node, str):
                if node.endswith(".png"):
                    path = os.path.join(SOURCE, "textures", os.path.basename(node))
                    if os.path.exists(path):
                        found.add(path)
            elif isinstance(node, dict):
                for value in node.values():
                    walk(value)
            elif isinstance(node, list):
                for value in node:
                    walk(value)

        walk(self.doc)
        return sorted(found)

    def inputs(self):
        return [p for p in [self.recipe, self.obj(), self.rig()] if p] + self.sheets()

    # ------------------------------------------------------------------ what the build makes

    def glb(self):
        """workshop/<rel folder>/<Name>/<Name>.glb, or None before the first build."""
        here = os.path.relpath(self.folder, SOURCE)
        exact = os.path.join(WORKSHOP, here, self.name, self.name + ".glb")
        if os.path.exists(exact):
            return exact
        hits = glob.glob(os.path.join(WORKSHOP, "**", self.name + ".glb"), recursive=True)
        hits = [h for h in hits if os.sep + here + os.sep in h] or hits
        return hits[0] if len(hits) == 1 else None

    def glb_rel(self):
        glb = self.glb()
        return os.path.relpath(glb, WORKSHOP) if glb else None

    def cooked_mesh(self):
        return os.path.join(COOKED, self.world, "meshes", self.name + ".mum")

    def __repr__(self):
        return f"{self.kind} {self.build_name} ({self.rel})"


def find(name):
    """A name, a world/Name, or a path to a recipe -> Thing. Raises NotFound, naming the choices."""
    if name.endswith(".json") and os.path.exists(name):
        return Thing(os.path.abspath(name))
    if "/" in name:
        for candidate in (os.path.join(SOURCE, "world", name + ".json"),
                          os.path.join(SOURCE, name + ".json")):
            if os.path.exists(candidate):
                return Thing(candidate)
        raise NotFound(f"no recipe source/world/{name}.json or source/{name}.json")
    hits = []
    for top in sorted(os.listdir(SOURCE)):
        if top in NOT_RECIPES or not os.path.isdir(os.path.join(SOURCE, top)):
            continue
        for path in glob.glob(os.path.join(SOURCE, top, "**", name + ".json"), recursive=True):
            if not path.endswith(".rig.json"):
                hits.append(path)
    if not hits and name.startswith("Arm") and name[3:4].isupper():
        # The game's name for a held weapon's figure is Arm<recipe> (ArmSword01 is Sword01's),
        # which is what the studio's browser has in hand.
        return find(name[3:])
    if not hits:
        raise NotFound(f"no recipe called {name} under source/")
    if len(hits) > 1:
        names = ", ".join(os.path.relpath(h, SOURCE)[:-5] for h in sorted(hits))
        raise NotFound(f"{name} is more than one recipe, name its folder: {names}")
    return Thing(hits[0])


def world_recipes(world):
    folder = os.path.join(SOURCE, "world", world)
    if not os.path.isdir(folder):
        raise NotFound(f"no world source/world/{world}")
    return [Thing(p) for p in sorted(glob.glob(os.path.join(folder, "*.json")))
            if not p.endswith(".rig.json") and _is_recipe(p)]


def _is_recipe(path):
    """A world folder also holds ground.json, placements and the like: a recipe names an item."""
    try:
        with open(path) as handle:
            doc = json.load(handle)
    except (OSError, ValueError):
        return False
    return isinstance(doc, dict) and "item" in doc


def cooked_worlds():
    """The worlds that have a cooked table: the ones 'every world's tables' means."""
    return sorted(w for w in os.listdir(COOKED)
                  if os.path.exists(os.path.join(COOKED, w, w + ".mur")))
