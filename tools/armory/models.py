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


# ---------------------------------------------------------------- the pose
#
# A figure is drawn as the game shows it standing still: its idle's first key, with what it holds
# on the bone it holds it by. Every .obj is MU's geometry at action 0 key 0 (MuExtract's
# BmdPose.RestAction/RestKey, the pose its .rig.json's `skeleton` also records), so a vertex
# goes back to its bone through that pose's world and out through the idle's:
# posed = idle[b] * rest[b]^-1 * v, in MU's space (Z up), as the game's skin does
# (Figure::pose, inverseBind then world). Bones are matched by index, as the game's clip rows
# are: a part on the player rig takes the player library's tracks for the bones it has.
# A held thing is its rest geometry carried by its grip bone's idle world (Figure::poseHeld,
# the bind then the hand), MU's RenderLinkObject.

_RIGS = {}


def rig(path):
    path = str(path)
    if path not in _RIGS:
        _RIGS[path] = load_json(path)
    return _RIGS[path]


def rig_of(obj):
    p = Path(str(obj)[:-4] + ".rig.json")
    return p if p.exists() else None


def _qm(q, t):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0]],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1]],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2]],
            [0.0, 0.0, 0.0, 1.0]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def _inv(m):
    """A rigid transform's inverse: R^T, -R^T t."""
    r = [[m[j][i] for j in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0.0, 0.0, 0.0, 1.0]]


def _key0(doc, action):
    """{bone index: (t, r)} at the action's first key, or None when the rig has no such action."""
    for a in doc.get("animations") or []:
        if a.get("action") == action:
            out = {}
            for tr in a.get("tracks") or []:
                if tr.get("t") and tr.get("r"):
                    out[tr["bone"]] = (tr["t"][0], tr["r"][0])
            return out
    return None


def _worlds(skeleton, local):
    """Every bone's world; local(i) -> (t, r) or None (MU's dummy: identity)."""
    I4 = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    W = []
    for i, b in enumerate(skeleton):
        tr = local(i)
        L = _qm(tr[1], tr[0]) if tr else I4
        p = b.get("parent", -1)
        W.append(L if p < 0 or p >= i else _mul(W[p], L))
    return W


def rest_worlds(doc):
    sk = doc["skeleton"]
    return _worlds(sk, lambda i: None if sk[i].get("dummy") else (sk[i]["t"], sk[i]["r"]))


def pose_worlds(doc, library, action):
    """The part's bones at the library's action, key 0; a bone the library has no track for
    stays at the part's rest."""
    sk = doc["skeleton"]
    keys = _key0(library, action) or {}
    return _worlds(sk, lambda i: keys.get(i) or (None if sk[i].get("dummy") else (sk[i]["t"], sk[i]["r"])))


def _apply(m, p, w=1.0):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] * w for i in range(3))


def _to_mu(p):
    return (p[0], -p[2], p[1])


def _from_mu(p):
    return (p[0], p[2], -p[1])


def repose(vs, vns, groups, skin):
    """vs/vns in the obj's Y-up space, posed by `skin` ({"kind": "part", ...} or "held").
    The normals' indices follow the vertices' (MuExtract writes one of each per vertex)."""
    if skin["kind"] == "held":
        m = skin["matrix"]
        vs = [_from_mu(_apply(m, _to_mu(v))) for v in vs]
        vns = [_from_mu(_apply(m, _to_mu(n), 0.0)) for n in vns]
        return vs, vns
    doc = rig(skin["rig"])
    bones = [b for mesh in doc.get("meshes") or [] for b in mesh.get("vertex_bones") or []]
    if len(bones) != len(vs):
        return vs, vns  # not the rig's vertices: left as exported
    rest = rest_worlds(doc)
    pose = pose_worlds(doc, rig(skin["library"]), skin["action"])
    mats = [_mul(p, _inv(r)) for p, r in zip(pose, rest)]
    out = [_from_mu(_apply(mats[b], _to_mu(v))) if 0 <= b < len(mats) else v for v, b in zip(vs, bones)]
    if len(vns) == len(vs):
        vns = [_from_mu(_apply(mats[b], _to_mu(n), 0.0)) if 0 <= b < len(mats) else n
               for n, b in zip(vns, bones)]
    return out, vns


def grip_matrix(library, action, bone):
    """The world of the library's bone by name at the action's first key (MU's space)."""
    sk = library["skeleton"]
    for i, b in enumerate(sk):
        if b["name"] == bone:
            return pose_worlds(library, library, action)[i]
    return None


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
        # And the colour at full brightness, so laid on by that alpha it adds what MU's
        # additive draw adds over a dark ground instead of darkening what is behind it.
        lut = lambda c, a: (c * 255 // a) if a else 0
        px = [(lut(R, A), lut(G, A), lut(B, A), A) for R, G, B, A in
              zip(r.getdata(), g.getdata(), b.getdata(), m.getdata())]
        im = Image.new("RGBA", im.size)
        im.putdata(px)
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

    skins = job.get("skins") or [None] * len(job["parts"])
    hidden_names = {n.lower() for n in job.get("hidden_groups") or ()}
    for part_i, (obj, doc) in enumerate(job["parts"]):
        vs, vts, vns, groups = parse_obj(obj)
        if skins[part_i]:
            vs, vns = repose(vs, vns, groups, skins[part_i])
        slots = slot_map(doc)
        cutout = lower_set(doc.get("cutout"))
        soft = lower_set(doc.get("soft_alpha"))
        glow = lower_set(doc.get("glow")) | lower_set(doc.get("additive"))
        # A glow marked `over` stays solid and is drawn again, added (export_gltf's echo).
        over = {k.lower() for k, v in (doc.get("glow") or {}).items()
                if isinstance(doc.get("glow"), dict) and isinstance(v, dict) and v.get("over")}
        # How bright a glow stands in a still picture: the middle of its pulse where it pulses
        # (cook.py's `pulse`, the shader's low and high), else whole.
        strength = {}
        if isinstance(doc.get("glow"), dict):
            for k, v in doc["glow"].items():
                pulse = v.get("pulse") if isinstance(v, dict) else None
                if isinstance(pulse, list) and len(pulse) == 2:
                    strength[k.lower()] = round((float(pulse[0]) + float(pulse[1])) / 2, 3)
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
            if gi in hidden or not g["faces"] or (part_i == 0 and g["name"].lower() in hidden_names):
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
              glow_strength = strength.get(slot, 1.0) if mode == "GLOW" else None
              mk = (file, mode, material_name, tuple(tint) if tint else None, glow_strength)
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
                          m["extras"] = {"mu_additive": True, "mu_strength": glow_strength}
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
        r = rig_of(obj)
        if r is not None:
            files.append(r)
        mats = set((doc.get("sheet_materials") or {}).values())
        mats |= {isl.get("material") for isl in doc.get("islands") or [] if isl.get("material")}
        for m in sorted(x for x in mats if isinstance(x, str)):
            files.append(MATS / f"{m}.json")
    libs = {sk["library"] for sk in job.get("skins") or [] if sk and sk.get("library")}
    files += [Path(x) for x in sorted(libs)]
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

    player = CONTENT / "players" / "rig" / "player.rig.json"

    def stance_of(key):
        return (I[key][1].get("stance") or "") if key in I else ""

    def posed(d, parts, character, label):
        """The figure posed to stand as the game stands it, holding what it holds: (skins, extra
        parts with their skins, hidden group names, notes). src/game/figures.cpp decides each of
        these and is followed here: `posture` and `addMonster` (which idle: a monster's action 0,
        a figure on the player rig its stance's, else its named idle, else the bare stop),
        `kStances` (a stance's action), `kRightGrip`/`kLeftGrip` and the recipe's `<side>_bone`
        (where a held thing rides; a bow in the left), a staff skinned to the body's rig worn
        as a part; and never drawn: the Hydra's show.beam_material (play_open.cpp
        hideBodyMaterial) and the recipe's hidden_part."""
        notes_ = []
        hide = set()
        if d.get("hidden_part"):
            hide.add(str(d["hidden_part"]))
        beam = ((d.get("show") or {}).get("beam_material") or {}).get("name")
        if beam:
            hide.add(beam[5:] if beam.startswith("glow_") else beam)
        if character:
            # The player rig's clips, unless the first part names its own (the townsfolk's
            # bodies, npc/body/*.json `animation_rig`: Man01, Female01, Girl01).
            own = parts[0][1].get("animation_rig")
            library = CONTENT / own if own else player
            # Characters stand in their weapon's stance (cook.py gives a character the right
            # hand's; a monster on the player rig names its own), else their named idle, else
            # the bare stop.
            stance = d.get("stance") or stance_of(d.get("right_hand") or "")
            table = {"sword": 4, "two_hand_sword": 5, "spear": 6, "scythe": 7, "bow": 8,
                     "crossbow": 9, "wand": 10}
            named = str(d.get("idle") or "")
            if stance in table:
                action = table[stance]
            elif named.startswith("action") and named[6:].isdigit():
                action = int(named[6:])
            else:
                action = 2 if d.get("female") else 1
        else:
            library = rig_of(parts[0][0])
            action = 0
        if library is None or not Path(library).exists():
            return [None] * len(parts), [], hide, notes_
        lib = rig(library)
        if _key0(lib, action) is None:
            action = 0  # figures.cpp posture: an idle the library lacks falls to its action 0
        skins = []
        for o, _ in parts:
            r = rig_of(o)
            # A monster's own body is already at its action 0, key 0: its idle's first key.
            skins.append({"kind": "part", "rig": str(r), "library": str(library), "action": action}
                         if r is not None and character else None)
        extra = []
        if d.get("slung"):
            notes_.append(f"{label}: its slung weapon is left out of the thumbnail")
        for side in ("right_hand", "left_hand") if not d.get("slung") else ():
            name = d.get(side)
            if not name:
                continue
            o, pd = item_obj(name)
            if o is None:
                notes_.append(f"{label}: {side} {name} has no model; left out")
                continue
            r = rig_of(o)
            first = rig_of(parts[0][0])
            if r is not None and first is not None:
                # Skinned to the body's own rig: worn, not held (figures.cpp: a character's by
                # its bone count against the first part's, a monster's by its bones' names).
                mine = [b["name"] for b in rig(r)["skeleton"]]
                body = [b["name"] for b in rig(first)["skeleton"]]
                if (len(mine) == len(body)) if character else (mine == body):
                    extra.append(((o, pd), {"kind": "part", "rig": str(r), "library": str(library),
                                            "action": action}))
                    continue
            bone = d.get(f"{side}_bone") or ""
            if not bone:
                bone = "knife_gdf" if side == "right_hand" else "hand_bofdgne01"
                if stance_of(name) == "bow":
                    bone = "hand_bofdgne01"
            m = grip_matrix(lib, action, bone)
            if m is None:
                notes_.append(f"{label}: no bone {bone} to hold {name}; left out")
                continue
            extra.append(((o, pd), {"kind": "held", "matrix": m, "bone": bone, "action": action,
                                    "library": str(library)}))
        return skins, extra, hide, notes_

    def figure_job(key, kind, d, parts, hidden, scale, tint, label):
        character = d.get("kind") == "character"
        skins, extra, hide, n = posed(d, parts, character, label)
        notes.extend(n)
        job = _job(key, kind, parts + [x[0] for x in extra], hidden, scale, tint)
        job["skins"] = skins + [x[1] for x in extra]
        job["hidden_groups"] = sorted(hide)
        return job

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
        jobs[f"monster:{key}"] = figure_job(key, "monster", d, parts, hidden, scale, d.get("tint"),
                                            f"monster {key}")

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
        jobs[f"npc:{fig}"] = figure_job(f"npc_{fig}", "npc", d, parts, hidden,
                                        float(d.get("scale", 1.0) or 1.0), None, f"npc {fig}")
    return jobs, notes


def convert(jobs, workers, say):
    """Each job's GLB into the cache unless its hash is there. Returns {job id: (hash, path)}."""
    from common import CACHE, file_hash
    out = CACHE / "glb"
    out.mkdir(parents=True, exist_ok=True)
    done, todo = {}, []
    for jid, job in jobs.items():
        extra = repr((job["kind"], sorted(job["hidden"]), job["scale"], job["tint"], job["upright"],
                      job["tex_limit"], job.get("hidden_groups"),
                      [(sk["kind"], sk.get("action"), sk.get("bone")) if sk else None
                       for sk in job.get("skins") or []], _me())).encode()
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
