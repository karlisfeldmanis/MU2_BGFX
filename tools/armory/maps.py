"""World charts, drawn as src/game/ui/minimap.cpp Minimap::bakeChart draws them (ported from the
prototype's gen_maps.py).

Reads the snapshot's content/world/<w>/{<w>.json, attributes.png, tiles.png}; writes
maps/<world>.png (512 square, opaque, the site's palette). Cached under
out/armory/cache/maps/<hash>.png by the hash of those files and this script.

The grid (src/content/ground.cpp readGrids): attributes.png pixel (x, y) is the tile at column x,
row y, no flip, and MU's attribute word is red | green << 8. Open land is
(word & (kNoMove 0x4 | kNoGround 0x8)) == 0; town is open and kSafeZone 0x1. Water is a blocked
tile whose floor slot (tiles.png red) is named "...Water..." in the world json's tile_slots.

The chart's texels, hairline and halo are read from the snapshot's minimap.cpp (kChartTexels,
kShoreTexels, kHaloTexels), so the site's chart follows the game's when they change. It bakes
the game's 1024 square and takes it to 512: 2 px a tile, tile (x, y) covering [2x, 2x+2).
"""
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image

from common import CACHE, file_hash, place, source_hash

K_NO_MOVE, K_NO_GROUND, K_SAFE = 0x4, 0x8, 0x1
OUT_SIZE = 512


def hexrgb(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32) / 255.0


# The site's palette (web/site/site.css), not the game's: the chart sits on the site's page.
BG = hexrgb("#0D0B0A")
LAND = hexrgb("#2A211B")
TOWN = hexrgb("#3A2D24")
WATER = hexrgb("#14202A")
LINE = hexrgb("#8E7258")
LINE_ALPHA = 0.55
HALO_ALPHA = 0.35


def chart_constants(src):
    text = (src / "src/game/ui/minimap.cpp").read_text()
    got = {}
    for name in ("kChartTexels", "kShoreTexels", "kHaloTexels"):
        m = re.search(r"constexpr (?:int|float) %s = ([\d.]+)f?;" % name, text)
        if not m:
            raise RuntimeError(f"minimap.cpp no longer says {name}: maps.py must follow bakeChart")
        got[name] = float(m.group(1))
    return int(got["kChartTexels"]), got["kShoreTexels"], got["kHaloTexels"]


def camera_turn(src):
    """How far the game's minimap turns the chart, degrees clockwise on screen: Minimap::setView
    lays a tile column along the camera's flattened right and a row along its flattened forward,
    and the played camera (src/game/world/world.cpp) looks down at kYawDegrees, the same in every
    world. Right is (-cos yaw, -sin yaw) in tiles and forward (-sin yaw, cos yaw), so a column
    goes to the screen's (-cos yaw, sin yaw): a turn of 180 - yaw."""
    text = (src / "src/game/world/world.cpp").read_text()
    m = re.search(r"constexpr float kYawDegrees = (-?[\d.]+)f?;", text)
    if not m:
        raise RuntimeError("world.cpp no longer says kYawDegrees: maps.py must follow the camera")
    return round(180.0 - float(m.group(1)), 3)


def soften(a):
    k = np.array([1, 2, 1], dtype=np.float32)
    p = np.pad(a, 1, mode="edge")
    rows = p[:, :-2] * k[0] + p[:, 1:-1] * k[1] + p[:, 2:] * k[2]
    return (rows[:-2] + rows[1:-1] * 2 + rows[2:]) / 16.0


def sample(field, fx, fy):
    n = field.shape[0]
    fx = np.clip(fx, 0, n - 1)
    fy = np.clip(fy, 0, n - 1)
    x0 = fx.astype(int)
    y0 = fy.astype(int)
    x1 = np.minimum(x0 + 1, n - 1)
    y1 = np.minimum(y0 + 1, n - 1)
    tx = (fx - x0)[None, :]
    ty = (fy - y0)[:, None]
    X0, Y0 = np.meshgrid(x0, y0)
    X1, Y1 = np.meshgrid(x1, y1)
    return ((field[Y0, X0] * (1 - tx) + field[Y0, X1] * tx) * (1 - ty) +
            (field[Y1, X0] * (1 - tx) + field[Y1, X1] * tx) * ty)


def grid(wdir, world):
    doc = json.loads((wdir / f"{world}.json").read_text())
    att = np.asarray(Image.open(wdir / doc.get("attributes", "attributes.png")).convert("RGB"))
    words = att[..., 0].astype(np.uint16) | (att[..., 1].astype(np.uint16) << 8)
    open_ = (words & (K_NO_MOVE | K_NO_GROUND)) == 0
    safe = (words & K_SAFE) != 0
    wet_slots = {int(k) for k, v in (doc.get("tile_slots") or {}).items() if "Water" in v}
    if doc.get("tiles") and wet_slots:
        floor = np.asarray(Image.open(wdir / doc["tiles"]).convert("RGB"))[..., 0]
        wet = np.isin(floor, list(wet_slots))
    else:
        wet = np.zeros_like(open_)
    return doc, open_, safe, wet


def chart(wdir, world, consts):
    texels, shore_w, halo_w = consts
    doc, open_, safe, wet = grid(wdir, world)
    tiles = open_.shape[0]
    land = soften(open_.astype(np.float32))
    town = soften((open_ & safe).astype(np.float32))
    water = soften((~open_ & wet).astype(np.float32))
    size = tiles * texels
    per_unit = 2.0 * texels
    f = (np.arange(size, dtype=np.float32) + 0.5) / texels - 0.5
    d = (sample(land, f, f) - 0.5) * per_unit
    cover = np.clip(d + 0.5, 0, 1)
    in_town = np.clip((sample(town, f, f) - 0.5) * per_unit + 0.5, 0, 1)
    shore = np.clip(shore_w + 0.5 - np.abs(d), 0, 1)
    wetness = np.clip(sample(water, f, f) * 1.6, 0, 1)
    halo = np.clip(1 - np.abs(d) / halo_w, 0, 1)
    img = np.broadcast_to(BG, (size, size, 3)).copy()

    def over(ink, a):
        a = a[..., None]
        img[:] = ink * a + img * (1 - a)

    over(WATER, wetness * (1 - cover))
    over(LAND, cover)
    over(TOWN, cover * in_town)
    over(BG * 0.5, HALO_ALPHA * halo * halo)
    over(LINE, LINE_ALPHA * shore)
    out = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")
    return out.resize((OUT_SIZE, OUT_SIZE), Image.LANCZOS), tiles


def build(src, worlds, out_dir, say):
    """maps/<world>.png for every world with a ground; returns world -> chart facts."""
    consts = chart_constants(src)
    turn = camera_turn(src)
    me = source_hash("maps.py")
    cache = CACHE / "maps"
    cache.mkdir(parents=True, exist_ok=True)
    facts, made = {}, 0
    for w, wd in worlds.items():
        wdir = wd["dir"]
        if not (wdir / "attributes.png").exists() or not (wdir / f"{w}.json").exists():
            continue
        doc = json.loads((wdir / f"{w}.json").read_text())
        files = [wdir / f"{w}.json", wdir / doc.get("attributes", "attributes.png")]
        if doc.get("tiles"):
            files.append(wdir / doc["tiles"])
        h = file_hash(files, (me + repr(consts)).encode())
        cached = cache / f"{h}.png"
        tiles_meta = cache / f"{h}.json"
        if not cached.exists():
            im, tiles = chart(wdir, w, consts)
            im.save(cached, optimize=True)
            tiles_meta.write_text(json.dumps({"tiles": tiles}))
            made += 1
        tiles = json.loads(tiles_meta.read_text())["tiles"]
        place(cached, Path(out_dir) / "maps" / f"{w}.png")
        k = OUT_SIZE / tiles
        facts[w] = {"png": f"maps/{w}.png", "size": OUT_SIZE, "tiles": tiles, "px_per_tile": k, "turn": turn,
                    "transform": f"px = (x + 0.5) * {k:g}, py = (y + 0.5) * {k:g} (x column, y row, no flip)"}
    say(f"maps: {len(facts)} charts ({made} drawn, {len(facts) - made} from the cache)")
    return facts
