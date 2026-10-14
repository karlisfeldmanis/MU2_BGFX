"""`mu paint`: a world's ground painted, tile by tile, in content/world/<w>/tiles.png.

tiles.png is MU's terrain mapping as pipeline/terrain.py wrote it: red the base slot (layer1),
green the overlay slot (layer2), blue how far the overlay is painted over the base (alpha,
0..255). A slot is an index into the world's `tile_slots` (<w>.json): Lorencia's 0 is
TileGrass01. The studio's Place paints with this (docs/plans/order-plan.md §9c, sprint G); the game
sees it after `mu make ground <w>`, which cooks the ground mesh again.

    ./mu paint lorencia 183 136 2                    tile (183, 136) floored with slot 2
    ./mu paint lorencia 183 136 0 --overlay --alpha 128 --radius 2
    ./mu paint lorencia --undo                       the last stroke on Lorencia taken back

A stroke is one journal line (`op` paint) holding every tile's old and new values, so an undo
is exact; it writes another line. Only the tiles named change: PIL re-saves an untouched grid
byte for byte (checked on Lorencia, Noria and Devias), so a stroke and its undo leave the file
as it was.
"""

import json
import os
import time

from PIL import Image

import edit
from things import ROOT, SOURCE, NotFound


def grid(world):
    path = os.path.join(SOURCE, "world", world, "tiles.png")
    if not os.path.exists(path):
        raise NotFound(f"no {os.path.relpath(path, ROOT)}")
    return path


def slots(world):
    with open(os.path.join(SOURCE, "world", world, world + ".json")) as handle:
        return {int(k): v for k, v in (json.load(handle).get("tile_slots") or {}).items()}


def disc(column, row, radius):
    """The tiles within `radius` of (column, row), a round brush."""
    return [(column + dx, row + dy) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)
            if dx * dx + dy * dy <= radius * radius + radius]


def paint(world, tiles, who="mu", op="paint"):
    """`tiles`: [[column, row, layer1, layer2, alpha], ...], a None keeping that channel.
    Writes them and journals the stroke; the tiles off the map are left out."""
    path = grid(world)
    known = slots(world)
    image = Image.open(path)
    image.load()
    if image.mode != "RGB":
        raise NotFound(f"{os.path.relpath(path, ROOT)} is {image.mode}, not RGB")
    pixels = image.load()
    width, height = image.size
    changed = []
    for column, row, *new in tiles:
        if not (0 <= column < width and 0 <= row < height):
            continue
        old = list(pixels[column, row])
        value = [old[i] if new[i] is None else int(new[i]) for i in range(3)]
        for i in (0, 1):
            if new[i] is not None and known and value[i] not in known and value[i] != 255:
                raise NotFound(f"slot {value[i]} is not in {world}'s tile_slots ({sorted(known)})")
        value[2] = max(0, min(255, value[2]))
        if value != old:
            pixels[column, row] = tuple(value)
            changed.append([column, row] + old + value)
    if changed:
        image.save(path)
    line = {"at": time.strftime("%Y-%m-%d %H:%M:%S"), "who": who, "file": os.path.relpath(path, ROOT),
            "op": op, "world": world, "tiles": changed}
    if changed:
        os.makedirs(os.path.dirname(edit.JOURNAL), exist_ok=True)
        with open(edit.JOURNAL, "a") as handle:
            handle.write(json.dumps(line) + "\n")
    return {"world": world, "tiles": len(changed), "line": line}


def brush(world, column, row, slot, overlay=False, alpha=255, radius=0, who="mu"):
    """A round stroke: the base slot set (and the overlay cleared off it), or the overlay slot
    painted over the base at `alpha`."""
    tiles = [[c, r, None, slot, alpha] if overlay else [c, r, slot, None, 0] for c, r in disc(column, row, radius)]
    return paint(world, tiles, who)


def undo(world, who="mu"):
    """Takes back the last stroke on `world` that is not itself taken back."""
    if not os.path.exists(edit.JOURNAL):
        raise NotFound("no journal")
    with open(edit.JOURNAL) as handle:
        strokes = [json.loads(one) for one in handle if '"op": "paint' in one]
    undone = 0
    for line in reversed([s for s in strokes if s.get("world") == world]):
        if line["op"] == "paint-undo":
            undone += 1
            continue
        if undone:
            undone -= 1
            continue
        back = [[t[0], t[1]] + t[2:5] for t in line["tiles"]]
        return paint(world, back, who, op="paint-undo")
    raise NotFound(f"no stroke on {world} to take back")
