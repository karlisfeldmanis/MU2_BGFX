"""Each world's manifest, source/world/<w>/world.json, as the pipeline reads it.

The order plan's sprint D (docs/order-plan.md): what terrain.py, index.py and tools/cook.py kept
as one dict per fact with a row per world is each world's own `pipeline` block, so a new map is
its manifest and its art. The scripts keep what a knob does and why; the manifest keeps which
worlds take it and with what, each row's citation beside it as a `note`.

A knob is written as its value, or as {"value": ..., "note": "..."}. JSON has no sets, tuples or
integer keys, so each knob is read through its converter here back into the shape its script
always had, and a dict built from the manifests equals the literal it replaced.

    by_map("hidden", as_set)       {MU map number: value} for every world that sets the knob
    by_world("lifted")             {world folder: value}
    maps_with("sky")               the map numbers whose knob is true
    worlds_with("flat_over_void")  the world folders whose knob is true
"""
import json
from pathlib import Path

WORLDS = Path(__file__).resolve().parent.parent / "source" / "world"

_manifests = None


def manifests() -> dict:
    """Every world's manifest by its folder, read once. A folder without one is not a world the
    pipeline knows yet."""
    global _manifests
    if _manifests is None:
        _manifests = {}
        for path in sorted(WORLDS.glob("*/world.json")):
            _manifests[path.parent.name] = json.loads(path.read_text())
    return _manifests


def _value(entry):
    return entry["value"] if isinstance(entry, dict) and "value" in entry else entry


def _knob(doc: dict, knob: str):
    pipeline = doc.get("pipeline", {})
    return _value(pipeline[knob]) if knob in pipeline else None


def by_map(knob: str, convert=lambda value: value) -> dict:
    return {doc["map"]: convert(value) for doc in manifests().values()
            if (value := _knob(doc, knob)) is not None}


def by_world(knob: str, convert=lambda value: value) -> dict:
    return {world: convert(value) for world, doc in manifests().items()
            if (value := _knob(doc, knob)) is not None}


def maps_with(knob: str) -> set:
    return {doc["map"] for doc in manifests().values() if _knob(doc, knob) is True}


def worlds_with(knob: str) -> set:
    return {world for world, doc in manifests().items() if _knob(doc, knob) is True}


def shown_models(block: str) -> dict:
    """{world: model} for the birds (`boids`) or crawlers (`crawls`) each world's `show` names:
    the client's choice (game/world/world_show.h), which the cook must build the same model for."""
    return {world: model for world, doc in manifests().items()
            if (model := doc.get("show", {}).get(block, {}).get("model"))}


def gate_boxes() -> dict:
    """{map: [(x1, y1, x2, y2)]}: every gate box on each map, enter and exit, from the
    manifests' `gates` (the ones off the map, the talk gates' -1s, left out). The index keeps
    them open; the realm walks the same rows (pipeline/realm_tables.py)."""
    boxes = {}
    for doc in manifests().values():
        gates = doc.get("gates", {})
        for row in gates.get("exits", []) + gates.get("enters", []):
            if row["box"][0] >= 0:
                boxes.setdefault(doc["map"], []).append(tuple(row["box"]))
    return boxes


def npcs() -> dict:
    """{map: [(npc, name, figure, column, row, look)]}: the townsfolk each world's manifest
    stands (`npcs`), for the cook's tables (tools/cook.py)."""
    return {doc["map"]: [(row["npc"], row["name"], row["figure"], row["at"][0], row["at"][1], row["look"])
                         for row in doc["npcs"]]
            for doc in manifests().values() if "npcs" in doc}


# ---- The converters: each knob back into its script's shape ----

def as_set(value) -> set:
    return set(value)


def as_tuples(value) -> list:
    return [tuple(row) for row in value]


def as_int_keys(value) -> dict:
    return {int(key): row for key, row in value.items()}


def as_types(value):
    """A run of types, {"range": [first, past]}, or a list of them."""
    return range(*value["range"]) if isinstance(value, dict) else set(value)


def as_walls(value) -> dict:
    """A rule naming `walls` (types) and maybe `keep` boxes."""
    rule = dict(value)
    rule["walls"] = set(rule["walls"])
    if "keep" in rule:
        rule["keep"] = as_tuples(rule["keep"])
    return rule


def as_perches(value) -> dict:
    """{type: (pose, turns, leans, tall)}, from {"<type>": {"pose", "turns", "leans", "tall"}}."""
    return {int(kind): (row["pose"], row["turns"], row["leans"], row["tall"])
            for kind, row in value.items()}


def as_operable(value) -> set:
    """The types a character poses against: the perches' own types."""
    return {int(kind) for kind in value}
