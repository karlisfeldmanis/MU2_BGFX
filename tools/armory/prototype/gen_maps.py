#!/usr/bin/env python3
"""World charts for the website, drawn as src/game/ui/minimap.cpp Minimap::bakeChart draws them.

    cd MU2_BGFX && nice python3 <scratch>/gen_maps.py [--out DIR]

Reads content/world/<world>/{<world>.json, attributes.png, tiles.png}; writes
DIR/maps/<world>.png (512 square, opaque, the site's palette) and DIR/maps/index.json.

The grid (src/content/ground.cpp readGrids, grid.h): attributes.png pixel (x, y) is the tile
at column x, row y -- no flip -- and MU's attribute word is red | green << 8. Open land is
(word & (kNoMove 0x4 | kNoGround 0x8)) == 0; town is open and kSafeZone 0x1. Water is a
blocked tile whose floor slot (tiles.png red, Ground::floorAt) is named "...Water..." in the
world json's tile_slots (Ground::floorName).

The chart: the game bakes 4 texels a tile (kChartTexels); this bakes the same 1024 square
with the same tent softening, edge distance, hairline (0.75 texel half-width) and halo
(2.6 texels), then takes it to 512 -- 2 px a tile. Tile (x, y) covers pixels
[2x, 2x+2) x [2y, 2y+2); its centre is (2x + 1, 2y + 1).
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path.cwd()
WORLD = ROOT / "content" / "world"

K_NO_MOVE, K_NO_GROUND, K_SAFE = 0x4, 0x8, 0x1
TEXELS = 4                 # kChartTexels
SHORE = 0.75               # kShoreTexels
HALO = 2.6                 # kHaloTexels
OUT_SIZE = 512


def hexrgb(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32) / 255.0


BG = hexrgb("#0D0B0A")
LAND = hexrgb("#2A211B")
TOWN = hexrgb("#3A2D24")
WATER = hexrgb("#14202A")
LINE = hexrgb("#8E7258")
LINE_ALPHA = 0.55
HALO_ALPHA = 0.35


def live_worlds():
    out = []
    for w in sorted(WORLD.glob("*/world.json")):
        name = w.parent.name
        if name.startswith("kanturu") or name == "charscene":
            continue
        if (w.parent / "attributes.png").exists():
            out.append(name)
    return out


def soften(a):
    k = np.array([1, 2, 1], dtype=np.float32)
    p = np.pad(a, 1, mode="edge")
    rows = p[:, :-2] * k[0] + p[:, 1:-1] * k[1] + p[:, 2:] * k[2]
    return (rows[:-2] + rows[1:-1] * 2 + rows[2:]) / 16.0


def sample(field, fx, fy):
    n = field.shape[0]
    fx = np.clip(fx, 0, n - 1)
    fy = np.clip(fy, 0, n - 1)
    x0 = fx.astype(int); y0 = fy.astype(int)
    x1 = np.minimum(x0 + 1, n - 1); y1 = np.minimum(y0 + 1, n - 1)
    tx = (fx - x0)[None, :]; ty = (fy - y0)[:, None]
    X0, Y0 = np.meshgrid(x0, y0); X1, Y1 = np.meshgrid(x1, y1)
    return ((field[Y0, X0] * (1 - tx) + field[Y0, X1] * tx) * (1 - ty) +
            (field[Y1, X0] * (1 - tx) + field[Y1, X1] * tx) * ty)


def chart(world):
    doc = json.loads((WORLD / world / f"{world}.json").read_text())
    att = np.asarray(Image.open(WORLD / world / doc.get("attributes", "attributes.png")).convert("RGB"))
    words = att[..., 0].astype(np.uint16) | (att[..., 1].astype(np.uint16) << 8)
    tiles = words.shape[0]
    open_ = (words & (K_NO_MOVE | K_NO_GROUND)) == 0
    safe = (words & K_SAFE) != 0
    wet_slots = {int(k) for k, v in (doc.get("tile_slots") or {}).items() if "Water" in v}
    if doc.get("tiles") and wet_slots:
        floor = np.asarray(Image.open(WORLD / world / doc["tiles"]).convert("RGB"))[..., 0]
        wet = np.isin(floor, list(wet_slots))
    else:
        wet = np.zeros_like(open_)
    land = soften(open_.astype(np.float32))
    town = soften((open_ & safe).astype(np.float32))
    water = soften((~open_ & wet).astype(np.float32))

    size = tiles * TEXELS
    per_unit = 2.0 * TEXELS
    f = (np.arange(size, dtype=np.float32) + 0.5) / TEXELS - 0.5
    d = (sample(land, f, f) - 0.5) * per_unit
    cover = np.clip(d + 0.5, 0, 1)
    in_town = np.clip((sample(town, f, f) - 0.5) * per_unit + 0.5, 0, 1)
    shore = np.clip(SHORE + 0.5 - np.abs(d), 0, 1)
    wetness = np.clip(sample(water, f, f) * 1.6, 0, 1)
    halo = np.clip(1 - np.abs(d) / HALO, 0, 1)

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
    return out.resize((OUT_SIZE, OUT_SIZE), Image.LANCZOS), tiles, open_, safe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent))
    a = ap.parse_args()
    out = Path(a.out) / "maps"
    out.mkdir(parents=True, exist_ok=True)
    index = {}
    for world in live_worlds():
        im, tiles, open_, safe = chart(world)
        im.save(out / f"{world}.png", optimize=True)
        k = OUT_SIZE / tiles
        index[world] = {"png": f"maps/{world}.png", "size": OUT_SIZE, "tiles": tiles,
                        "transform": f"px = (x + 0.5) * {k:g}, py = (y + 0.5) * {k:g}  "
                                     f"(x = column = image right, y = row = image down, no flip; "
                                     f"tile x,y covers [{k:g}x, {k:g}x+{k:g}))"}
        npcs = json.loads((WORLD / world / "world.json").read_text()).get("npcs") or []
        report = []
        for n in npcs:
            x, y = n["at"]
            report.append(f"{n['name']}@{x},{y}:" + ("open" if open_[y, x] else "BLOCKED") +
                          ("+safe" if safe[y, x] else ""))
        print(world, f"{(out / f'{world}.png').stat().st_size // 1024} KB", "; ".join(report))
    (out / "index.json").write_text(json.dumps(index, indent=1))


if __name__ == "__main__":
    main()
