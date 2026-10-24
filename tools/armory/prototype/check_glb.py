#!/usr/bin/env python3
"""Parse GLBs back (counts, bounds, indices in range) and software-render previews.

    python3 check_glb.py models/Sword17.glb ... [--render OUTDIR]
"""
import io
import json
import math
import struct
import sys
from pathlib import Path

from PIL import Image

SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
FMT = {5126: "f", 5123: "H", 5125: "I", 5121: "B"}


def load(path):
    data = Path(path).read_bytes()
    magic, ver, total = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and ver == 2 and total == len(data), "bad header"
    jl, jt = struct.unpack_from("<II", data, 12)
    assert jt == 0x4E4F534A
    gj = json.loads(data[20:20 + jl])
    bo = 20 + jl
    bl, bt = struct.unpack_from("<II", data, bo)
    assert bt == 0x004E4942
    binc = data[bo + 8:bo + 8 + bl]
    assert gj["buffers"][0]["byteLength"] <= bl
    return gj, binc


def read_acc(gj, binc, i):
    a = gj["accessors"][i]
    v = gj["bufferViews"][a["bufferView"]]
    n = SIZES[a["type"]]
    f = FMT[a["componentType"]]
    vals = struct.unpack_from(f"<{a['count'] * n}{f}", binc, v["byteOffset"])
    assert struct.calcsize(f"<{a['count'] * n}{f}") <= v["byteLength"]
    return [vals[k:k + n] for k in range(0, len(vals), n)] if n > 1 else list(vals)


def check(path):
    gj, binc = load(path)
    out = []
    lo, hi = [1e9] * 3, [-1e9] * 3
    tris = 0
    for p in gj["meshes"][0]["primitives"]:
        pos = read_acc(gj, binc, p["attributes"]["POSITION"])
        nor = read_acc(gj, binc, p["attributes"]["NORMAL"])
        uv = read_acc(gj, binc, p["attributes"]["TEXCOORD_0"])
        idx = read_acc(gj, binc, p["indices"])
        assert len(pos) == len(nor) == len(uv)
        assert max(idx) < len(pos) and len(idx) % 3 == 0
        a = gj["accessors"][p["attributes"]["POSITION"]]
        for k in range(3):
            assert abs(min(q[k] for q in pos) - a["min"][k]) < 1e-5
            lo[k] = min(lo[k], a["min"][k]); hi[k] = max(hi[k], a["max"][k])
        tris += len(idx) // 3
    return {"prims": len(gj["meshes"][0]["primitives"]), "tris": tris,
            "images": len(gj.get("images", [])), "min": [round(x, 3) for x in lo],
            "max": [round(x, 3) for x in hi]}


def render(path, out, size=360):
    gj, binc = load(path)
    imgs = []
    for im in gj.get("images", []):
        v = gj["bufferViews"][im["bufferView"]]
        imgs.append(Image.open(io.BytesIO(binc[v["byteOffset"]:v["byteOffset"] + v["byteLength"]])).convert("RGBA"))
    prims = []
    for p in gj["meshes"][0]["primitives"]:
        m = gj["materials"][p["material"]]
        pbr = m["pbrMetallicRoughness"]
        tex = imgs[gj["textures"][pbr["baseColorTexture"]["index"]]["source"]] if "baseColorTexture" in pbr else None
        prims.append((read_acc(gj, binc, p["attributes"]["POSITION"]),
                      read_acc(gj, binc, p["attributes"]["NORMAL"]),
                      read_acc(gj, binc, p["attributes"]["TEXCOORD_0"]),
                      read_acc(gj, binc, p["indices"]), tex, m))
    allp = [q for pr in prims for q in pr[0]]
    rad = max(math.sqrt(x * x + y * y + z * z) for x, y, z in allp) or 1
    panels = []
    for yaw in (0.0, 35.0, 180.0):
        W = H = size
        col = Image.new("RGB", (W, H), (60, 64, 72))
        px = col.load()
        zb = [[-1e9] * W for _ in range(H)]
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cx, sx = math.cos(math.radians(-15)), math.sin(math.radians(-15))
        light = (0.4, 0.7, 0.6)
        ll = math.sqrt(sum(c * c for c in light)); light = tuple(c / ll for c in light)

        def view(v):
            x, y, z = v
            x, z = x * cy + z * sy, -x * sy + z * cy  # yaw about Y
            y, z = y * cx - z * sx, y * sx + z * cx   # pitch
            return x, y, z
        sc = (W * 0.46) / rad
        for pos, nor, uv, idx, tex, m in prims:
            vp = [view(v) for v in pos]
            vn = [view(n) for n in nor]
            tw, th = (tex.size if tex else (1, 1))
            tpx = tex.load() if tex else None
            cut = m.get("alphaMode") in ("MASK", "BLEND")
            for t in range(0, len(idx), 3):
                i0, i1, i2 = idx[t], idx[t + 1], idx[t + 2]
                s = [(W / 2 + vp[i][0] * sc, H / 2 - vp[i][1] * sc, vp[i][2]) for i in (i0, i1, i2)]
                minx = max(int(min(q[0] for q in s)), 0); maxx = min(int(max(q[0] for q in s)) + 1, W - 1)
                miny = max(int(min(q[1] for q in s)), 0); maxy = min(int(max(q[1] for q in s)) + 1, H - 1)
                d = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1])
                if abs(d) < 1e-9:
                    continue
                for yy in range(miny, maxy + 1):
                    for xx in range(minx, maxx + 1):
                        fx, fy = xx + 0.5, yy + 0.5
                        w1 = ((fx - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (fy - s[0][1])) / d
                        w2 = ((s[1][0] - s[0][0]) * (fy - s[0][1]) - (fx - s[0][0]) * (s[1][1] - s[0][1])) / d
                        w0 = 1 - w1 - w2
                        if w0 < 0 or w1 < 0 or w2 < 0:
                            continue
                        z = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]
                        if z <= zb[yy][xx]:
                            continue
                        if tpx:
                            u = w0 * uv[i0][0] + w1 * uv[i1][0] + w2 * uv[i2][0]
                            v = w0 * uv[i0][1] + w1 * uv[i1][1] + w2 * uv[i2][1]
                            c = tpx[int((u % 1) * tw) % tw, int((v % 1) * th) % th]
                            if cut and c[3] < 64:
                                continue
                        else:
                            c = (128, 128, 128, 255)
                        n = [w0 * vn[i0][k] + w1 * vn[i1][k] + w2 * vn[i2][k] for k in range(3)]
                        nl = math.sqrt(sum(q * q for q in n)) or 1
                        lam = abs(sum(n[k] / nl * light[k] for k in range(3)))  # two-sided
                        f = 0.35 + 0.75 * lam
                        if m.get("extras", {}).get("mu_additive"):
                            f = 1.0
                        zb[yy][xx] = z
                        px[xx, yy] = tuple(min(255, int(c[k] * f)) for k in range(3))
        panels.append(col)
    sheet = Image.new("RGB", (size * 3, size))
    for i, p in enumerate(panels):
        sheet.paste(p, (i * size, 0))
    sheet.save(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    outdir = None
    if "--render" in args:
        i = args.index("--render"); outdir = Path(args[i + 1]); del args[i:i + 2]
        outdir.mkdir(parents=True, exist_ok=True)
    for p in args:
        print(Path(p).stem, check(p))
        if outdir:
            render(p, outdir / (Path(p).stem + ".png"))
