"""GLBs of the release's items, monsters and townsfolk figures: an internal step of the Armory.

The GLBs are never published (the site shows thumbnails, thumbs.py renders them from these). Each
is kept in out/armory/cache/glb/<hash>.glb, the hash over its .obj, its recipe json, the sheets and
material files it names and this script, so only what changed is converted again. Ported from the
prototype's gen_models.py; reads the snapshot's content/ only (stdlib + PIL).

Conventions:
  * glTF Y-up. The .obj files are already in the engine's Y-up space (MuExtract's
    AxisConvention maps MU (x, y, z) -> (x, z, -y)), so armour and monsters stand as-is.
    Weapons lie along +Z in that space (the game holds them in a hand); items under
    content/items/weapons whose length runs along Z are turned so the tip points +Y.
  * Metres: MU units / 100 (export_gltf's MU_UNITS_PER_METRE); monsters are also
    multiplied by their monster.scale, so their relative sizes are the game's.
  * Centred: the bounding-box centre sits at the origin.
  * UVs: OBJ vt is bottom-left origin (MuExtract wrote 1 - V), glTF is top-left, so V is
    flipped back here.
  * Normals: MU's own (the obj's vn); computed per face only where an obj has none.
  * Every material is doubleSided (MU draws its meshes without culling).

Textures follow the cook (pipeline/asset.sh): an asset's `sheets` dict maps OBJ group ->
file, a `sheets` list maps Path(file).stem -> file, a lone `sheet` likewise; the slot is
the group name, compared case-folded (transfer_albedo). A slot the asset does not name
bakes black in the game; here it falls back to a case-insensitive match of the group name
or the obj's `# texture:` stem against content/textures, then to neutral grey (noted).
"""
import io
import json
import os
import struct
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image

# Set by use(src): the snapshot's content/ (a worker process sets it from its job).
CONTENT = TEX = MATS = None


def use(content):
    global CONTENT, TEX, MATS, _TEX_INDEX
    content = Path(content)
    if CONTENT != content:
        CONTENT, TEX, MATS = content, content / "textures", content / "materials"
        _TEX_INDEX = None
        _MAT_CACHE.clear()

ALPHA_CUTOFF = 0.25          # export_gltf.ALPHA_TEST_REF, MU's glAlphaFunc(GL_GREATER, 0.25)
UNITS = 100.0                # export_gltf.MU_UNITS_PER_METRE
ITEM_TEX = 256
MONSTER_TEX = 256
JPEG_Q = 85

FLOAT, U16, U32 = 5126, 5123, 5125


# ---------------------------------------------------------------- content lookups

def load_json(p):
    with open(p) as h:
        return json.load(h)


def scan():
    items, monsters = {}, {}
    for p in sorted(CONTENT.glob("items/*/*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load_json(p)
        if "item" in d:
            items[d["item"]] = (p, d)
    for p in sorted(CONTENT.glob("monsters/*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load_json(p)
        if "item" in d:
            monsters[d["item"]] = (p, d)
    return items, monsters


_TEX_INDEX = None


def tex_index():
    global _TEX_INDEX
    if _TEX_INDEX is None:
        _TEX_INDEX = {}
        for f in os.listdir(TEX):
            if f.lower().endswith(".png"):
                _TEX_INDEX.setdefault(f.lower(), f)
    return _TEX_INDEX


def slot_map(doc):
    """asset.sh's slots: dict group->file, list stem->file, or sheet stem->file; folded."""
    s = doc.get("sheets")
    out = {}
    if isinstance(s, dict):
        out = {g.lower(): f for g, f in s.items()}
    elif isinstance(s, list):
        out = {Path(f).stem.lower(): f for f in s}
    elif doc.get("sheet"):
        out = {Path(doc["sheet"]).stem.lower(): doc["sheet"]}
    return out


def lower_set(v):
    if isinstance(v, (list, tuple)):
        return {str(x).lower() for x in v}
    if isinstance(v, dict):
        return {str(x).lower() for x in v}
    return set()


_MAT_CACHE = {}


def library(name):
    if name not in _MAT_CACHE:
        p = MATS / f"{name}.json"
        _MAT_CACHE[name] = load_json(p) if p.exists() else {}
    return _MAT_CACHE[name]


# ---------------------------------------------------------------- obj

def parse_obj(path):
    vs, vts, vns = [], [], []
    groups = []
    cur = None
    with open(path) as h:
        for line in h:
            if not line.strip():
                continue
            t = line.split()
            k = t[0]
            if k == "v":
                vs.append((float(t[1]), float(t[2]), float(t[3])))
            elif k == "vt":
                vts.append((float(t[1]), float(t[2])))
            elif k == "vn":
                vns.append((float(t[1]), float(t[2]), float(t[3])))
            elif k == "g":
                cur = {"name": " ".join(t[1:]), "texture": None, "faces": []}
                groups.append(cur)
            elif k == "#" and len(t) > 2 and t[1] == "texture:":
                if cur is None:
                    cur = {"name": Path(t[2]).stem, "texture": None, "faces": []}
                    groups.append(cur)
                cur["texture"] = " ".join(t[2:])
            elif k == "f":
                if cur is None:
                    cur = {"name": path.stem, "texture": None, "faces": []}
                    groups.append(cur)
                corners = []
                for c in t[1:]:
                    parts = (c.split("/") + ["", ""])[:3]
                    corners.append(tuple(int(x) - 1 if x else -1 for x in parts))
                for i in range(1, len(corners) - 1):
                    cur["faces"].append((corners[0], corners[i], corners[i + 1]))
    return vs, vts, vns, groups


# ---------------------------------------------------------------- textures

def prepare_image(file, mode, limit):
    """Returns (bytes, mime). mode: OPAQUE, MASK, BLEND, GLOW."""
    im = Image.open(TEX / file)
    im.load()
    im = im.convert("RGBA")
    w, h = im.size
    if max(w, h) > limit:
        s = limit / max(w, h)
        im = im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    if mode == "GLOW":
        from PIL import ImageChops
        r, g, b, a = im.split()
        # alpha = brightest channel: black adds nothing in MU's additive draw.
        m = ImageChops.lighter(ImageChops.lighter(r, g), b)
        im = Image.merge("RGBA", (r, g, b, m))
        mode = "BLEND"
    if mode == "OPAQUE" or im.getextrema()[3][0] == 255 and mode != "BLEND":
        buf = io.BytesIO()
        im.convert("RGB").save(buf, "JPEG", quality=JPEG_Q, optimize=True)
        return buf.getvalue(), "image/jpeg"
    best = None
    for candidate in (im, im.quantize(colors=256, method=Image.FASTOCTREE)):
        buf = io.BytesIO()
        candidate.save(buf, "PNG", optimize=True)
        if best is None or len(buf.getvalue()) < len(best):
            best = buf.getvalue()
    return best, "image/png"


# ---------------------------------------------------------------- glb writer

class GLB:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []
        self.images, self.textures, self.materials, self.prims = [], [], [], []
        self.image_cache = {}

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, data, ctype, count, typ, mn=None, mx=None, target=None):
        a = {"bufferView": self.view(data, target), "componentType": ctype,
             "count": count, "type": typ}
        if mn is not None:
            a["min"], a["max"] = mn, mx
        self.accessors.append(a)
        return len(self.accessors) - 1

    def image(self, key, data, mime):
        if key in self.image_cache:
            return self.image_cache[key]
        self.images.append({"bufferView": self.view(data), "mimeType": mime})
        self.textures.append({"source": len(self.images) - 1, "sampler": 0})
        self.image_cache[key] = len(self.textures) - 1
        return self.image_cache[key]

    def write(self, path, extras=None):
        gj = {
            "asset": {"version": "2.0", "generator": "MU2 tools/armory/models.py"},
            "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0, "name": Path(path).stem}],
            "meshes": [{"name": Path(path).stem, "primitives": self.prims}],
            "materials": self.materials,
            "accessors": self.accessors,
            "bufferViews": self.views,
            "buffers": [{"byteLength": len(self.bin)}],
        }
        if extras:
            gj["nodes"][0]["extras"] = extras
        if self.images:
            gj["images"] = self.images
            gj["textures"] = self.textures
            gj["samplers"] = [{"magFilter": 9729, "minFilter": 9987,
                               "wrapS": 10497, "wrapT": 10497}]
        js = json.dumps(gj, separators=(",", ":")).encode()
        js += b" " * (-len(js) % 4)
        bn = bytes(self.bin) + b"\0" * (-len(self.bin) % 4)
        total = 12 + 8 + len(js) + 8 + len(bn)
        with open(path, "wb") as h:
            h.write(struct.pack("<III", 0x46546C67, 2, total))
            h.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            h.write(struct.pack("<II", len(bn), 0x004E4942) + bn)
        return total


# ---------------------------------------------------------------- building

def normalize(n):
    l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) ** 0.5
    return (n[0] / l, n[1] / l, n[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)


def build(job):
    """job: key, kind, out, parts=[(obj_path, doc)], hidden (set of group indices of parts[0]),
    scale, tint, upright, tex_limit, content. Returns (key, size, notes)."""
    use(job["content"])
    key = job["key"]
    notes = []
    # Many sheets on one figure (gambler_moss wears twelve): take them to 128 to stay small.
    sheets = sum(len(slot_map(doc)) for _, doc in job["parts"])
    if sheets >= 8:
        job = dict(job, tex_limit=min(job["tex_limit"], 128))
    glb = GLB()
    prims = []  # (positions list, normals, uvs, indices, material index)
    mat_cache = {}

    for part_i, (obj, doc) in enumerate(job["parts"]):
        vs, vts, vns, groups = parse_obj(obj)
        slots = slot_map(doc)
        cutout = lower_set(doc.get("cutout"))
        soft = lower_set(doc.get("soft_alpha"))
        glow = lower_set(doc.get("glow")) | lower_set(doc.get("additive"))
        # A glow marked `over` stays solid and is drawn again, added (export_gltf's echo).
        over = {k.lower() for k, v in (doc.get("glow") or {}).items()
                if isinstance(doc.get("glow"), dict) and isinstance(v, dict) and v.get("over")}
        smat = {k.lower(): v for k, v in (doc.get("sheet_materials") or {}).items()}
        overrides = doc.get("material_overrides") or {}
        islands = doc.get("islands") or []
        dominant = None
        if islands:
            tally = {}
            for isl in islands:
                tally[isl.get("material")] = tally.get(isl.get("material"), 0) + isl.get("triangles", 0)
            dominant = max(tally, key=tally.get)
        hidden = job.get("hidden", set()) if part_i == 0 else set()

        for gi, g in enumerate(groups):
            if gi in hidden or not g["faces"]:
                continue
            slot = g["name"].lower()
            file = slots.get(slot)
            how = "sheet"
            if file and not (TEX / file).exists():
                found = tex_index().get(file.lower())
                file = found
            if not file:
                stems = [slot]
                if g["texture"]:
                    stems += [Path(g["texture"]).stem.lower(), g["texture"].lower()]
                idx = tex_index()
                file = next((idx[s + ".png"] for s in stems if s + ".png" in idx), None)
                how = "stem" if file else "grey"
                notes.append(f"{key}: group '{g['name']}' (texture {g['texture']}) not named by "
                             f"the asset's sheets -> {file + ' by case-insensitive stem' if file else 'neutral grey'}")
            # mode
            if slot in glow and slot not in over:
                mode = "GLOW"
            elif slot in soft:
                mode = "BLEND"
            elif slot in cutout:
                mode = "MASK"
            else:
                mode = "OPAQUE"
            material_name = smat.get(slot) or dominant
            tint = None
            if job.get("tint") and material_name in job["tint"]:
                tint = job["tint"][material_name]
            modes = [mode] + (["GLOW"] if slot in over and file else [])
            mis = []
            for mode in modes:
              mk = (file, mode, material_name, tuple(tint) if tint else None)
              if mk not in mat_cache:
                  m = {"name": f"{g['name']}" + ("__additive" if mode == "GLOW" else ""),
                       "doubleSided": True,
                       "pbrMetallicRoughness": {}}
                  pbr = m["pbrMetallicRoughness"]
                  lib = library(material_name) if material_name else {}
                  metal = float(lib.get("metallic", 0.0) or 0.0)
                  rough = float(lib.get("roughness", 0.8) or 0.8)
                  ov = overrides.get(material_name) or {}
                  if isinstance(ov.get("roughness"), (int, float)):
                      rough = float(ov["roughness"])
                  if isinstance(ov.get("metallic"), (int, float)):
                      metal = float(ov["metallic"])
                  # Softened for an unlit web page: MU's sheets are painted with their light,
                  # and a full mirror metal with no environment reads black.
                  pbr["metallicFactor"] = round(metal * 0.6, 3)
                  pbr["roughnessFactor"] = round(max(rough, 0.4), 3)
                  if tint:
                      pbr["baseColorFactor"] = [float(c) for c in tint] + [1.0]
                  if file:
                      data, mime = prepare_image(file, mode, job["tex_limit"])
                      ti = glb.image((file, mode), data, mime)
                      if mode == "GLOW":
                          pbr["baseColorFactor"] = [0.0, 0.0, 0.0, 1.0]
                          pbr["baseColorTexture"] = {"index": ti}
                          m["emissiveTexture"] = {"index": ti}
                          m["emissiveFactor"] = [1.0, 1.0, 1.0]
                          m["alphaMode"] = "BLEND"
                          m["extras"] = {"mu_additive": True}
                      else:
                          pbr["baseColorTexture"] = {"index": ti}
                          if mode == "MASK":
                              m["alphaMode"] = "MASK"
                              m["alphaCutoff"] = ALPHA_CUTOFF
                          elif mode == "BLEND":
                              m["alphaMode"] = "BLEND"
                  else:
                      pbr["baseColorFactor"] = [0.5, 0.5, 0.5, 1.0]
                      pbr["metallicFactor"] = 0.0
                  if material_name:
                      m.setdefault("extras", {})["mu_material"] = material_name
                  glb.materials.append(m)
                  mat_cache[mk] = len(glb.materials) - 1
              mis.append(mat_cache[mk])

            remap = {}
            P, N, T, I = [], [], [], []
            for face in g["faces"]:
                fn = None
                for (vi, ti, ni) in face:
                    if (ni < 0 or ni >= len(vns)) and fn is None:
                        a, b, c = (vs[f[0]] for f in face)
                        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
                        w = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
                        fn = normalize((u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
                                        u[0] * w[1] - u[1] * w[0]))
                for (vi, ti, ni) in face:
                    k = (vi, ti, ni if ni >= 0 else -2 - len(P))
                    if k not in remap:
                        remap[k] = len(P)
                        P.append(vs[vi])
                        N.append(normalize(vns[ni]) if 0 <= ni < len(vns) else fn)
                        uv = vts[ti] if 0 <= ti < len(vts) else (0.0, 0.0)
                        T.append((uv[0], 1.0 - uv[1]))
                    I.append(remap[k])
            for mi in mis:
                prims.append((P, N, T, I, mi))

    if not prims:
        return key, 0, notes + [f"{key}: no faces"]

    # transform: scale, upright, centre
    s = job["scale"] / UNITS
    allp = [p for pr in prims for p in pr[0]]
    rot = face = False
    if job.get("upright"):
        ext = [max(p[i] for p in allp) - min(p[i] for p in allp) for i in range(3)]
        rot = job.get("upright") == "weapons" and ext[2] > ext[1] * 1.2 and ext[2] >= ext[0]
        if rot:
            ext = [ext[0], ext[2], ext[1]]
        # Broad side to the camera (+Z): a blade, bow or shield is thin along X in the
        # game's space, so it is turned a quarter about Y when X is its thinner side.
        face = ext[0] < ext[2]

    def tfn(n):
        x, y, z = n
        if rot:
            y, z = z, -y
        if face:
            x, z = z, -x
        return (x, y, z)

    def tf(p):
        x, y, z = tfn(p)
        return (x * s, y * s, z * s)

    allt = [tf(p) for p in allp]
    lo = [min(p[i] for p in allt) for i in range(3)]
    hi = [max(p[i] for p in allt) for i in range(3)]
    c = [(lo[i] + hi[i]) / 2 for i in range(3)]

    for P, N, T, I, mi in prims:
        pos = [tuple(v - c[i] for i, v in enumerate(tf(p))) for p in P]
        pmn = [min(p[i] for p in pos) for i in range(3)]
        pmx = [max(p[i] for p in pos) for i in range(3)]
        pa = glb.accessor(struct.pack(f"<{3 * len(pos)}f", *[x for p in pos for x in p]),
                          FLOAT, len(pos), "VEC3", pmn, pmx, 34962)
        na = glb.accessor(struct.pack(f"<{3 * len(N)}f", *[x for n in N for x in tfn(n)]),
                          FLOAT, len(N), "VEC3", target=34962)
        ta = glb.accessor(struct.pack(f"<{2 * len(T)}f", *[x for t in T for x in t]),
                          FLOAT, len(T), "VEC2", target=34962)
        if len(pos) < 65536:
            data, ct = struct.pack(f"<{len(I)}H", *I), U16
        else:
            data, ct = struct.pack(f"<{len(I)}I", *I), U32
        ia = glb.accessor(data, ct, len(I), "SCALAR", target=34963)
        glb.prims.append({"attributes": {"POSITION": pa, "NORMAL": na, "TEXCOORD_0": ta},
                          "indices": ia, "material": mi, "mode": 4})

    extras = {"mu_key": key, "kind": job["kind"], "size_m": [round(hi[i] - lo[i], 4) for i in range(3)],
              "turned_upright": rot, "turned_to_face": face}
    size = glb.write(job["out"], extras)
    return key, size, notes


# ---------------------------------------------------------------- jobs

def find_item_obj(items, key):
    if key in items:
        p, d = items[key]
        o = p.with_suffix(".obj")
        if o.exists():
            return o, d
    return None, None



# ---------------------------------------------------------------- jobs

ME = None


def _me():
    global ME
    if ME is None:
        from common import source_hash
        ME = source_hash("models.py")
    return ME


def _groups(obj):
    """The obj's group names and `# texture:` stems, read fast (for the hash's texture list)."""
    names = set()
    with open(obj, "rb") as h:
        for line in h:
            if line.startswith(b"g ") or line.startswith(b"# texture:"):
                names.add(line.split(b" ", 1 if line[0:1] == b"g" else 2)[-1].strip().decode(errors="replace"))
    return names


def sources(job):
    """Every file the GLB is made from: what its hash covers."""
    files = []
    for obj, doc in job["parts"]:
        files.append(Path(obj))
        if doc.get("_file"):
            files.append(Path(doc["_file"]))
        named = set(slot_map(doc).values())
        idx = tex_index()
        for g in _groups(obj):
            stem = Path(g).stem.lower()
            for cand in (g.lower() + ".png", stem + ".png"):
                if cand in idx:
                    named.add(idx[cand])
        for f in sorted(named):
            files.append(TEX / f)
        mats = set((doc.get("sheet_materials") or {}).values())
        mats |= {isl.get("material") for isl in doc.get("islands") or [] if isl.get("material")}
        for m in sorted(x for x in mats if isinstance(x, str)):
            files.append(MATS / f"{m}.json")
    return files


def _job(key, kind, parts, hidden=(), scale=1.0, tint=None, upright=None, tex_limit=256):
    return {"key": key, "kind": kind, "parts": parts, "hidden": set(hidden), "scale": scale,
            "tint": tint, "upright": upright, "tex_limit": tex_limit, "content": str(CONTENT)}


def plan(src, items, monsters, figures):
    """Jobs for every item, monster and townsfolk figure that has a model.

    items, monsters: content.py's dicts. figures: the npc figure names the release's worlds stand.
    Returns (jobs keyed "item:<key>" / "monster:<key>" / "npc:<figure>", notes)."""
    use(Path(src) / "content")
    notes = []
    I = {k: (v["file"], dict(v["doc"], _file=str(v["file"]))) for k, v in items.items()}
    # Items without stats (the riders) are still parts a figure may wear.
    for p in sorted(CONTENT.glob("items/*/*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load_json(p)
        if "item" in d and d["item"] not in I:
            I[d["item"]] = (p, dict(d, _file=str(p)))
    M = {}
    for p in sorted(CONTENT.glob("monsters/*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load_json(p)
        if "item" in d:
            M[d["item"]] = (p, dict(d, _file=str(p)))

    def item_obj(key):
        if key in I:
            p, d = I[key]
            o = p.with_suffix(".obj")
            if o.exists():
                return o, d
        return None, None

    jobs = {}
    for key in items:
        p, d = I[key]
        obj, doc = p.with_suffix(".obj"), d
        if not obj.exists() and d.get("mesh"):
            obj, base = item_obj(d["mesh"])
            if obj is not None:
                doc = d if (d.get("sheets") or d.get("sheet")) else base
        if obj is None or not obj.exists():
            notes.append(f"item {key}: no model (.obj)")
            continue
        jobs[f"item:{key}"] = _job(key, "item", [(obj, doc)],
                                   upright=p.parent.name if p.parent.name in ("weapons", "shields") else None)

    for key in monsters:
        p, d = M[key]
        scale = float((d.get("monster") or {}).get("scale", 1.0) or 1.0)
        obj = p.with_suffix(".obj")
        parts, hidden = None, set()
        if obj.exists():
            parts = [(obj, d)]
            if d.get("hidden_mesh") is not None:
                hidden = {int(d["hidden_mesh"])}
        elif d.get("mesh"):
            base = M.get(d["mesh"])
            bobj = base[0].with_suffix(".obj") if base else None
            if bobj is not None and bobj.exists():
                doc = d if (d.get("sheets") or d.get("sheet")) else base[1]
                parts = [(bobj, doc)]
                if d.get("hidden_mesh") is not None:
                    hidden = {int(d["hidden_mesh"])}
        elif d.get("kind") == "character" and d.get("parts"):
            parts = []
            for part in d["parts"]:
                o, pd = item_obj(part)
                if o is None and part in M:
                    mo = M[part][0].with_suffix(".obj")
                    if mo.exists():
                        o, pd = mo, M[part][1]
                if o is None:
                    notes.append(f"monster {key}: part {part} has no model; left out")
                    continue
                parts.append((o, pd))
        if not parts:
            notes.append(f"monster {key}: no model")
            continue
        jobs[f"monster:{key}"] = _job(key, "monster", parts, hidden, scale, d.get("tint"))

    def part_of(name):
        o, pd = item_obj(name)
        if o is not None:
            return o, pd
        for folder in ("monsters", "npc/body", "players/body", "npc"):
            o = CONTENT / folder / f"{name}.obj"
            j = CONTENT / folder / f"{name}.json"
            if o.exists():
                return o, (dict(load_json(j), _file=str(j)) if j.exists() else {})
        return None, None

    for fig in figures:
        j = CONTENT / "npc" / f"{fig}.json"
        d = dict(load_json(j), _file=str(j)) if j.exists() else {}
        obj = CONTENT / "npc" / f"{fig}.obj"
        parts, hidden = [], set()
        if obj.exists():
            parts = [(obj, d)]
            if d.get("hidden_mesh") is not None:
                hidden = {int(d["hidden_mesh"])}
        elif d.get("kind") == "character" and d.get("parts"):
            for part in d["parts"]:
                o, pd = part_of(part)
                if o is None:
                    notes.append(f"npc {fig}: part {part} has no model; left out")
                    continue
                parts.append((o, pd))
                # An open helm goes on over the bare head (figures.cpp dress, keeps_head).
                if part.startswith("Helm") and pd.get("keeps_head"):
                    ho, hd = part_of("HelmClass03" if d.get("female") else "HelmClass02")
                    if ho is not None:
                        parts.append((ho, hd))
        elif d.get("mesh"):
            o, pd = part_of(d["mesh"])
            if o is not None:
                parts = [(o, d if (d.get("sheets") or d.get("sheet")) else pd)]
        if not parts:
            notes.append(f"npc {fig}: no model")
            continue
        jobs[f"npc:{fig}"] = _job(f"npc_{fig}", "npc", parts, hidden, float(d.get("scale", 1.0) or 1.0))
    return jobs, notes


def convert(jobs, workers, say):
    """Each job's GLB into the cache unless its hash is there. Returns {job id: (hash, path)}."""
    from common import CACHE, file_hash
    out = CACHE / "glb"
    out.mkdir(parents=True, exist_ok=True)
    done, todo = {}, []
    for jid, job in jobs.items():
        extra = repr((job["kind"], sorted(job["hidden"]), job["scale"], job["tint"], job["upright"],
                      job["tex_limit"], _me())).encode()
        h = file_hash(sources(job), extra)
        path = out / f"{h}.glb"
        done[jid] = (h, path)
        # Two things on one model and one set of sheets share one GLB.
        if not path.exists() and all(t["final"] != str(path) for t in todo):
            todo.append(dict(job, out=str(path) + ".part", final=str(path)))
    notes = []
    if todo:
        say(f"models: converting {len(todo)} of {len(jobs)} ({len(jobs) - len(todo)} cached)")
        with ProcessPoolExecutor(max_workers=workers) as pool:
            for job, (key, size, n) in zip(todo, pool.map(build, todo, chunksize=2)):
                notes.extend(n)
                if size:
                    os.replace(job["out"], job["final"])
    else:
        say(f"models: all {len(jobs)} cached")
    for jid in list(done):
        if not done[jid][1].exists():
            done.pop(jid)
    return done, notes
