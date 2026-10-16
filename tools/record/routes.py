"""Straight runs for a recording: where in a world a hero can run LENGTH tiles in a straight line
along one direction, outside any safe zone, with open ground either side so the route never
bends -- the one thing that keeps his stride and the camera the same in every world's take.

    python3 tools/record/routes.py DX DY LENGTH [--top=N] WORLD [WORLD ...]

Prints a JSON object: each world's best start tile, scored by how many monsters' nests lie
a few tiles off the line but not on it (content/world/<w>/nests.json), so he runs past life. The walk
test is content/grid.h's: a word with the safe and wall bits clear (0x20/0x40/0x80 ignored).

And no tile of it may be one where a tall object stands between him and the game's camera: the
camera is fixed (pitch 48.5 degrees, yaw 45, 9.5 m; game/world/world.cpp), toward (1, -1) in
tiles from him, so its line of sight to his chest climbs 1.13 m a tile toward it, and anything
in the cooked world (out/assets/world/<w>/<w>.json, its glb's bounds) taller than that line
where it stands hides him.
"""

import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NEAR = 8  # tiles either side of the line a nest counts within...
SPREAD = 30  # tiles between two candidates' starts
TOP = 1      # how many candidates a world (--top=N)
CLOSE = 2  # ...and no nearer: he runs past them, not through a pack standing on his road
CAMERA = (1.0 / math.sqrt(2.0), -1.0 / math.sqrt(2.0))  # toward the camera, in tiles
CLIMB = math.tan(math.radians(48.5))  # the line of sight's rise, metres a tile
CHEST = 1.0  # metres: where on him the line of sight is aimed
SIDE = 1.5   # tiles either side of that line an object still covers him within


def model_height(path, cache={}):
    if path not in cache:
        height = 0.0
        try:
            blob = open(path, "rb").read()
            length = struct.unpack_from("<I", blob, 12)[0]
            gltf = json.loads(blob[20:20 + length])
            low, high = 1e9, -1e9
            for mesh in gltf.get("meshes", []):
                for primitive in mesh["primitives"]:
                    accessor = gltf["accessors"][primitive["attributes"]["POSITION"]]
                    if "min" in accessor:
                        low, high = min(low, accessor["min"][1]), max(high, accessor["max"][1])
            height = max(0.0, high) if high > low else 0.0
        except (OSError, ValueError, KeyError, struct.error):
            pass
        cache[path] = height
    return cache[path]


def hidden(world, rows, columns):
    """The tiles where something tall stands between him and the camera."""
    out = np.zeros((rows, columns), bool)
    cooked = os.path.join(ROOT, "out", "assets", "world", world)
    try:
        objects = json.load(open(os.path.join(cooked, world + ".json")))
    except OSError:
        return out
    unit = objects.get("units_per_tile", 100.0)
    for one in objects.get("objects", []):
        model = one["model"]
        height = model_height(os.path.join(cooked, model, model + ".glb")) * one.get("scale", 1.0)
        reach = (height - CHEST) / CLIMB  # how far from him toward the camera it still covers him
        if reach <= 0.0:
            continue
        ox, oy = one["at"][0] / unit, one["at"][1] / unit
        # Every tile P with O - P = s * CAMERA + lateral, 0 < s < reach and |lateral| < SIDE.
        steps = int(math.ceil(reach * 2))
        for i in range(steps + 1):
            s = reach * i / max(1, steps)
            for k in range(-3, 4):
                lateral = SIDE * k / 3.0
                px = ox - s * CAMERA[0] - lateral * CAMERA[1]
                py = oy - s * CAMERA[1] + lateral * CAMERA[0]
                tx, ty = int(round(px)), int(round(py))
                if 0 <= tx < columns and 0 <= ty < rows:
                    out[ty, tx] = True
    return out


def best(world, dx, dy, length):
    folder = os.path.join(ROOT, "content", "world", world)
    pixels = np.array(Image.open(os.path.join(folder, "attributes.png")).convert("RGBA"))
    word = pixels[..., 0].astype(np.int32) | (pixels[..., 1].astype(np.int32) << 8)
    open_ = (word & ~0xE0) == 0
    # And its four neighbours open too, so the route has no corner to cut.
    clear = open_.copy()
    clear[1:, :] &= open_[:-1, :]
    clear[:-1, :] &= open_[1:, :]
    clear[:, 1:] &= open_[:, :-1]
    clear[:, :-1] &= open_[:, 1:]
    rows, columns = clear.shape
    clear &= ~hidden(world, rows, columns)

    life = np.zeros((rows, columns), np.float32)
    nests = os.path.join(folder, "nests.json")
    if os.path.isfile(nests):
        for nest in json.load(open(nests)).get("nests", []):
            x0, y0, x1, y1 = nest["box"]
            area = max(1, (x1 - x0 + 1) * (y1 - y0 + 1))
            life[max(0, y0):y1 + 1, max(0, x0):x1 + 1] += nest.get("count", 1) / area

    # Scenery: what stands near the line and does not hide him -- plants, stones, ruins, lamps --
    # counted a tile at a time, so a stretch through something is preferred to bare ground.
    scenery = np.zeros((rows, columns), np.float32)
    cooked = os.path.join(ROOT, "out", "assets", "world", world)
    try:
        objects = json.load(open(os.path.join(cooked, world + ".json")))
        unit = objects.get("units_per_tile", 100.0)
        for one in objects.get("objects", []):
            tx, ty = int(one["at"][0] / unit), int(one["at"][1] / unit)
            if 0 <= tx < columns and 0 <= ty < rows:
                scenery[ty, tx] += 1.0
    except OSError:
        pass

    def boxed(grid, reach):
        """Each tile's sum of `grid` over the square `reach` round it."""
        padded = np.pad(grid, reach)
        table = padded.cumsum(0).cumsum(1)
        table = np.pad(table, ((1, 0), (1, 0)))
        side = 2 * reach + 1
        return (table[side:, side:] - table[:-side, side:] - table[side:, :-side]
                + table[:-side, :-side])[:rows, :columns]

    near, close, around = boxed(life, NEAR), boxed(life, CLOSE), boxed(scenery, 6)
    per_tile = near - 4.0 * close + 0.5 * around

    found = []
    for y in range(rows):
        for x in range(columns):
            ex, ey = x + dx * (length - 1), y + dy * (length - 1)
            if not (0 <= ex < columns and 0 <= ey < rows):
                continue
            if not all(clear[y + dy * t, x + dx * t] for t in range(length)):
                continue
            score = float(sum(per_tile[y + dy * t, x + dx * t] for t in range(length)))
            rim = min(x, y, columns - 1 - ex, rows - 1 - ey)
            if rim < 12:
                score *= 0.5
            found.append((score, x, y, ex, ey))
    # The best few, each at least SPREAD tiles from every one taken before it, so the choice is
    # between different places rather than one place shifted a tile.
    found.sort(reverse=True)
    picked = []
    for score, x, y, ex, ey in found:
        if all(abs(x - p["start"][0]) + abs(y - p["start"][1]) >= SPREAD for p in picked):
            picked.append({"start": [x, y], "end": [ex, ey], "score": round(score, 1)})
        if len(picked) >= TOP:
            break
    return picked


def main():
    global TOP
    dx, dy, length = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    if len(sys.argv) > 4 and sys.argv[4].startswith("--top="):
        TOP = int(sys.argv.pop(4)[6:])
    print(json.dumps({w: best(w, dx, dy, length) for w in sys.argv[4:]}, indent=1))


if __name__ == "__main__":
    main()
