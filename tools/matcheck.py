#!/usr/bin/env python3
"""matcheck: does every surface in this content wear the material it ought to?

The engine's material model is closed -- albedo, normal, ORM, emissive, and the three flags
(cutout, two-sided, skinned) -- and PLAN.md's foundation 5 says MU2's material library is
mapped onto it at cook time. Nothing until now checked that the mapping happened, or that what
it produced matches what the library asked for.

Four fifths of the material slots in `assets/` are named `mu2` or after the MU texture sheet
they came off (`ston01`, `tile_wood02`, `lpwing`) rather than after a library entry. That is
not quite the same as unmapped -- most of those sheets carry an ORM whose roughness sits on a
library value, so a recipe did run -- but the record of *which* material was chosen was thrown
away with the name, and roughness alone cannot put it back: the library clusters six or seven
entries inside one tolerance between 0.7 and 0.9. 102 slots have no ORM map at all.

This tool is the audit, and it is deliberately two halves, because only one of them can be
mechanised:

  * **The mechanical half.** A material's name either resolves to `index.json`'s library or
    it does not. Its baked ORM either agrees with that library entry or it does not. A metal
    either appears in the model's own `metal:` list or it is a metal nobody asked for. A
    `MASK` material either has holes in its albedo's alpha or the cutout is a lie. A cooked
    `.ktx` either carries the format its role demands or the colour space is wrong. None of
    these need an eye and all of them fail the run.

  * **The judged half.** No checker knows that a Budge Dragon's wing membrane should not wear
    `leather`. That is the user's call, it is made once by reading a shot, and it is written
    down in `sheets/materials.json` as a ruling. After that the audit is mechanical again: a
    slot with a ruling must match its ruling, and a slot without one is listed as unruled so
    the list shrinks instead of being rediscovered.

What it does not do is fix anything. A wrong assignment is repaired either in MU2's pipeline
recipe and re-exported, or -- for the ones not worth a round trip -- by a ruling the cook
applies over the baked ORM. Both are decisions, not errands.

Usage:
    tools/matcheck.py                      # audit, exit non-zero on a failure
    tools/matcheck.py --census out.md      # also write the whole table for reading
    tools/matcheck.py --model BudgeDragon01   # one model, verbose
"""

import argparse
import collections
import glob
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

# How far a baked ORM may sit from the library value it claims before it is a failure. The
# ORM is 8-bit and BC7-compressed, so a texel cannot be exact; 0.04 is about ten codes, which
# is wider than the compressor's error and far narrower than the gap between any two library
# entries (the closest pair is bone 0.58 and chitin 0.62).
ORM_TOLERANCE = 0.04

# Below this mean lean, in degrees, a normal map is doing nothing. A BC5 pair quantised to
# 8 bits carries about half a degree of noise around flat, so 2 degrees is comfortably above
# the floor and far below anything a relief of 0.3 or more would produce.
RELIEF_MIN_TILT = 2.0

# A material name a checker must not silently accept. `mu2` is the exporter's default and the
# single biggest hole in the mapping; the rest are MU's own sheet names, which say what the
# texture is and nothing about how it reflects.
DEFAULT_MATERIAL_NAMES = {"mu2", "material", "default", "none", "hidden"}


def fail(failures, model, what, why):
    failures.append((model, what, why))


def note(notes, model, what, why):
    """Worth knowing and not worth failing a run over: nothing renders wrong because of it."""
    notes.append((model, what, why))


def failure_key(what, why):
    """What a baseline records: the slot and the kind of fault, not the measured number.

    The numbers move whenever a sheet is retuned or a texture recooked, so keying on them
    would make the baseline expire constantly. The kind is the first clause of the sentence,
    up to the first number, which is stable.
    """
    kind = []
    for word in why.split():
        if any(character.isdigit() for character in word):
            break
        kind.append(word)
    return f"{what}|{' '.join(kind[:6])}"


# ---------------------------------------------------------------------------- glb reading

class Glb:
    """Just enough glTF to read materials, UVs and the images they point at."""

    def __init__(self, path):
        self.path = path
        data = open(path, "rb").read()
        if data[:4] != b"glTF":
            raise ValueError("not a glb")
        json_len = struct.unpack("<I", data[12:16])[0]
        self.doc = json.loads(data[20:20 + json_len])
        # The binary chunk's header follows the json chunk, padded to four bytes.
        offset = 20 + json_len
        self.bin = b""
        while offset + 8 <= len(data):
            length, kind = struct.unpack_from("<I4s", data, offset)
            if kind == b"BIN\x00":
                self.bin = data[offset + 8:offset + 8 + length]
                break
            offset += 8 + length
        self._images = {}

    def view(self, index):
        v = self.doc["bufferViews"][index]
        start = v.get("byteOffset", 0)
        return self.bin[start:start + v["byteLength"]]

    def accessor(self, index, components):
        """One accessor as an (count, components) float array, byte stride respected."""
        a = self.doc["accessors"][index]
        v = self.doc["bufferViews"][a["bufferView"]]
        base = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        code, size = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2),
                      5125: ("I", 4), 5126: ("f", 4)}[a["componentType"]]
        count = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        stride = v.get("byteStride") or size * count
        out = np.empty((a["count"], count), dtype=np.float64)
        for i in range(a["count"]):
            out[i] = struct.unpack_from("<" + code * count, self.bin, base + i * stride)
        return out[:, :components] if components else out

    def image(self, index):
        """An image as float RGBA in [0,1], decoded once.

        Nearly every glb here embeds its images, but the three ground meshes point at sibling
        PNGs by uri, so both are read.
        """
        if index not in self._images:
            entry = self.doc["images"][index]
            if "bufferView" in entry:
                source = io.BytesIO(self.view(entry["bufferView"]))
            else:
                source = os.path.join(os.path.dirname(self.path), entry["uri"])
                if not os.path.exists(source):
                    return None
            pixels = np.asarray(Image.open(source).convert("RGBA"), dtype=np.float64)
            self._images[index] = pixels / 255.0
        return self._images[index]

    def image_for(self, material, role):
        """Which image index a material uses in a role, or None. The cook's own rule."""
        pbr = material.get("pbrMetallicRoughness", {})
        source = {
            "albedo": pbr.get("baseColorTexture"),
            "orm": pbr.get("metallicRoughnessTexture") or material.get("occlusionTexture"),
            "normal": material.get("normalTexture"),
            "emissive": material.get("emissiveTexture"),
        }[role]
        if source is None:
            return None
        texture = self.doc["textures"][source["index"]]
        return texture.get("source")


def sample_uvs(uv, indices, width, height, budget=4000):
    """Texel coordinates the triangles of one part actually cover.

    Vertex UVs alone are not enough: a seam vertex sits on the boundary between two painted
    regions and can read the neighbour's value, which would make a correct bake look wrong.
    Each triangle contributes its own three corners pulled a third of the way towards its
    centroid, plus the centroid, so every sample is strictly inside the triangle.
    """
    tris = indices.reshape(-1, 3)
    if len(tris) > budget:
        step = len(tris) // budget + 1
        tris = tris[::step]
    a, b, c = uv[tris[:, 0]], uv[tris[:, 1]], uv[tris[:, 2]]
    centroid = (a + b + c) / 3.0
    points = np.concatenate([centroid,
                             a + (centroid - a) * 0.34,
                             b + (centroid - b) * 0.34,
                             c + (centroid - c) * 0.34])
    x = np.clip((points[:, 0] % 1.0) * width, 0, width - 1).astype(np.int32)
    y = np.clip((points[:, 1] % 1.0) * height, 0, height - 1).astype(np.int32)
    return x, y


def triangle_area(pos, indices):
    """World area of a part, so a report can say how much of a model a slot is."""
    tris = indices.reshape(-1, 3)
    a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    return float(np.linalg.norm(np.cross(b - a, c - a), axis=1).sum() * 0.5)


# ---------------------------------------------------------------------------- the checks

class Slot:
    """One material slot of one model, with what was measured under it."""

    def __init__(self, model, index, name):
        self.model = model
        self.index = index
        self.name = name
        self.area = 0.0
        self.triangles = 0
        self.roughness = None
        self.metal = None
        self.occlusion = None
        self.alpha_mode = "OPAQUE"
        self.cutout = -1.0
        self.two_sided = False
        self.alpha_holes = None   # fraction of albedo texels below the cutout
        self.tilt = None          # mean degrees the normal map leans off the surface
        self.maps = {}
        self.library = None
        self.ruling = None
        self.nearest = None       # the library entry its measured ORM matches, if any


# The land is the documented exception to the closed material model: it has its own shader,
# it blends two full material sets by a per-vertex weight, and its surfaces come from a
# world's `ground_surfaces.json` rather than from a glTF material slot. Auditing its slots
# against the library would be measuring the wrong thing. See docs/conventions.md, Materials.
def is_ground(model):
    return model.endswith("_ground")


def read_model(path, library):
    """Every material slot of one glb, measured. Raises on a file that will not parse."""
    glb = Glb(path)
    model = os.path.splitext(os.path.basename(path))[0]
    doc = glb.doc
    materials = doc.get("materials", [])
    slots = [Slot(model, i, m.get("name", "")) for i, m in enumerate(materials)]

    for i, m in enumerate(materials):
        slot = slots[i]
        slot.alpha_mode = m.get("alphaMode", "OPAQUE")
        if slot.alpha_mode == "MASK":
            slot.cutout = float(m.get("alphaCutoff", 0.5))
        slot.two_sided = bool(m.get("doubleSided"))
        slot.library = library.get(slot.name)
        for role in ("albedo", "normal", "orm", "emissive"):
            image = glb.image_for(m, role)
            if image is not None:
                slot.maps[role] = doc["images"][image].get("name", f"image{image}")

    # Walk the primitives so a slot's geometry is what actually references it, not what the
    # material list claims. A slot no primitive uses is a finding of its own.
    for mesh in doc.get("meshes", []):
        for prim in mesh["primitives"]:
            index = prim.get("material")
            if index is None or index >= len(slots):
                continue
            slot = slots[index]
            material = materials[index]
            attrs = prim["attributes"]
            if "indices" not in prim:
                continue
            indices = glb.accessor(prim["indices"], 1).astype(np.int64).ravel()
            slot.triangles += len(indices) // 3
            if "POSITION" in attrs:
                slot.area += triangle_area(glb.accessor(attrs["POSITION"], 3), indices)
            if "TEXCOORD_0" not in attrs:
                continue
            uv = glb.accessor(attrs["TEXCOORD_0"], 2)

            orm_image = glb.image_for(material, "orm")
            pixels = glb.image(orm_image) if orm_image is not None else None
            if pixels is not None:
                x, y = sample_uvs(uv, indices, pixels.shape[1], pixels.shape[0])
                slot.occlusion = float(pixels[y, x, 0].mean())
                slot.roughness = float(pixels[y, x, 1].mean())
                slot.metal = float(pixels[y, x, 2].mean())

            albedo_image = glb.image_for(material, "albedo")
            pixels = glb.image(albedo_image) if albedo_image is not None else None
            if pixels is not None:
                x, y = sample_uvs(uv, indices, pixels.shape[1], pixels.shape[0])
                threshold = slot.cutout if slot.cutout > 0 else 0.5
                slot.alpha_holes = float((pixels[y, x, 3] < threshold).mean())

            # How far the normal map actually leans, in degrees off the surface. A library
            # entry's `relief` is a claim about this number and until now nothing compared the
            # two: a map that is flat everywhere leaves one wide coherent highlight across a
            # whole surface, which is what a painted MU sheet reads as glossy plastic. The
            # tilt is measured rather than inferred from the map's presence, because a normal
            # map full of (0.5, 0.5) is present and does nothing.
            normal_image = glb.image_for(material, "normal")
            pixels = glb.image(normal_image) if normal_image is not None else None
            if pixels is not None:
                x, y = sample_uvs(uv, indices, pixels.shape[1], pixels.shape[0])
                nxy = pixels[y, x, :2] * 2.0 - 1.0
                lean = np.clip(np.linalg.norm(nxy, axis=1), 0.0, 1.0)
                slot.tilt = float(np.degrees(np.arcsin(lean)).mean())

    return slots


def nearest_library(slot, library):
    """Which library entry a measured ORM matches, and whether the match is unique.

    MU2's pipeline did apply the library -- it just named the glTF material after the MU
    texture sheet it came off rather than after the material it chose, so the mapping is
    recoverable from the numbers instead of having to be redone by hand. Returns the entry and
    how many entries are within tolerance of it; two or more means the ORM cannot say which,
    and only a ruling can.
    """
    if slot.roughness is None:
        return None, 0
    close = [entry for entry in library.values()
             if abs(entry["roughness"] - slot.roughness) <= ORM_TOLERANCE
             and abs(entry["metallic"] - slot.metal) <= ORM_TOLERANCE]
    if not close:
        return None, 0
    best = min(close, key=lambda e: abs(e["roughness"] - slot.roughness))
    return best, len(close)


def check_slot(slot, entry, failures, notes, unruled):
    """Every mechanical check one slot has to pass."""
    model, name = slot.model, slot.name or f"slot{slot.index}"
    where = f"{model}/{name}"

    # A slot no primitive draws with renders nothing, so it is a note rather than a failure --
    # but it is not nothing: every one of these is a material MU2's exporter wrote and the
    # mesh dropped, and they are where the `mu2` default hides. 65 of them would drown a gate.
    if slot.triangles == 0:
        note(notes, model, where, "a material slot no primitive draws with; the part list and "
                                  "the material list have drifted apart")
        return

    # 1. An ORM with no ORM, checked before anything about the name, because a surface with no
    #    ORM map is wrong whatever it is called: the engine's fallback is occlusion 1,
    #    roughness 1, metal 0, which is a deliberate matte rather than a right answer. This
    #    check sat below the unruled return until it was noticed that the 102 slots it was
    #    meant for -- the trees, which are the two largest surfaces in the content -- are all
    #    unruled and were all returning before they reached it.
    if slot.roughness is None:
        fail(failures, model, where, "no ORM map at all; the surface falls back to "
                                     "roughness 1 metal 0 and nothing chose that")

    # 2. The name has to mean something. A slot named after MU's texture sheet or left at the
    #    exporter's default was never mapped onto the library, so nothing chose its reflectance.
    if slot.library is None:
        target = slot.ruling
        if target is None:
            kind = "the exporter's default" if slot.name in DEFAULT_MATERIAL_NAMES \
                else "MU's own sheet name"
            unruled.append((slot, kind))
            return

    # 3. What the ruling or the name claims, the ORM has to carry. This is the check that
    #    catches a swapped channel, a region painted from the wrong recipe, and a bake that
    #    never ran -- all three of which look like "the wrong material" on screen.
    want = slot.ruling or slot.library
    if want is not None and slot.roughness is not None:
        if abs(slot.roughness - want["roughness"]) > ORM_TOLERANCE:
            fail(failures, model, where,
                 f"ORM roughness {slot.roughness:.3f} but {want['name']} asks "
                 f"{want['roughness']:.2f}")
        if abs(slot.metal - want["metallic"]) > ORM_TOLERANCE:
            fail(failures, model, where,
                 f"ORM metal {slot.metal:.3f} but {want['name']} asks {want['metallic']:.2f}")

    # 4. Metal is the loudest mistake in this content -- a metal has no diffuse, so a wrong
    #    one turns painted art into a mirror. A partial metal is not a material, it is a
    #    blend of two, and the library has no entry between 0.35 and 1.0 on purpose.
    if slot.metal is not None and 0.05 < slot.metal < 0.30:
        fail(failures, model, where,
             f"ORM metal {slot.metal:.3f} is neither dielectric nor metal; a texel between "
             "the two shades as neither")

    # 5. The cutout flag against the albedo's own alpha. A MASK material with no holes pays
    #    for a discard in four passes and gets nothing; an OPAQUE material with holes is the
    #    leaf that went solid, which is how MU's foliage breaks.
    if slot.alpha_holes is not None:
        if slot.alpha_mode == "MASK" and slot.alpha_holes < 0.001:
            fail(failures, model, where,
                 "alphaMode MASK but the albedo's alpha has no holes under this part; the "
                 "discard costs four passes and cuts nothing")
        if slot.alpha_mode == "OPAQUE" and slot.alpha_holes > 0.02:
            fail(failures, model, where,
                 f"alphaMode OPAQUE but {slot.alpha_holes * 100:.0f}% of the albedo's alpha "
                 "is below 0.5; whatever was meant to be cut out is drawn solid")

    # 6. The relief the library asked for, against the relief the normal map carries. This is
    #    the check the Budge Dragon's wings were found by: chitin asks relief 1.00, the map
    #    leans 0.3 degrees, and a flat map at roughness 0.62 puts one unbroken highlight
    #    across a whole two-sided wing, which reads as glossy plastic rather than as a
    #    membrane. Relief is what breaks a highlight up, and roughness cannot do its job.
    if want is not None and want.get("relief", 0.0) >= 0.3:
        if "normal" not in slot.maps:
            fail(failures, model, where,
                 f"{want['name']} carries relief {want['relief']:.2f} and this slot has no "
                 "normal map at all; the relief was authored and then dropped")
        elif slot.tilt is not None and slot.tilt < RELIEF_MIN_TILT:
            fail(failures, model, where,
                 f"{want['name']} carries relief {want['relief']:.2f} but its normal map "
                 f"leans {slot.tilt:.1f}deg under this part; the map is flat and the relief "
                 "never reached it")


def check_metal_list(slots, index_entry, failures):
    """A monster's own `metal:` list is the only record of which metals it may wear."""
    if index_entry is None:
        return
    allowed = set(index_entry.get("metal", []))
    model = slots[0].model if slots else "?"
    for slot in slots:
        want = slot.ruling or slot.library
        if want is None or want["metallic"] < 0.5:
            continue
        if slot.name not in allowed:
            fail(failures, model, f"{model}/{slot.name}",
                 f"wears the metal {slot.name} but index.json's metal list for this model is "
                 f"{sorted(allowed) or 'empty'}")


# ------------------------------------------------------------------- the cooked side

KTX_FORMATS = {
    0x8E8C: "BC7 linear",
    0x8E8D: "BC7 sRGB",
    # Not RGTC2 (0x8DBD), which is what BC5's GL enum looks like it should be. bimg's own KTX
    # table writes BC5 as the LATC2 extension, and a checker that expects RGTC2 calls every
    # correctly cooked normal map a failure -- which is what this one did until the table in
    # `bimg/src/image.cpp` settled it.
    0x8C72: "BC5",
}

# What each role's cooked texture must be, from docs/conventions.md's table. A role in the
# wrong colour space is the one material bug that is invisible in the source and obvious on
# screen: an ORM read as sRGB turns roughness 0.62 into 0.35 and every surface goes glossy.
ROLE_FORMAT = {
    "albedo": "BC7 sRGB",
    "emissive": "BC7 sRGB",
    "normal": "BC5",
    "orm": "BC7 linear",
}


def check_cooked_textures(failures):
    """Every cooked .ktx against the format its role demands."""
    checked = 0
    for path in sorted(glob.glob(os.path.join(ASSETS, "cooked", "**", "*.ktx"),
                                 recursive=True)):
        name = os.path.basename(path)
        role = None
        for candidate in ROLE_FORMAT:
            if f"_{candidate}_" in name or f"_{candidate}." in name:
                role = candidate
        if role is None:
            continue
        head = open(path, "rb").read(68)
        if head[:7] != b"\xabKTX 11":
            fail(failures, name, name, "not a KTX 1.1 file")
            continue
        fields = struct.unpack_from("<13I", head, 16)
        internal = fields[3]
        got = KTX_FORMATS.get(internal, f"unknown 0x{internal:x}")
        checked += 1
        if got != ROLE_FORMAT[role]:
            fail(failures, name, name,
                 f"a {role} cooked as {got}, and the role demands {ROLE_FORMAT[role]}")
        if fields[10] <= 1:
            fail(failures, name, name, "one mip level; PLAN.md's mip rule is not optional")
    return checked


# ---------------------------------------------------------------------------- the sheet

def read_rulings(library):
    """sheets/materials.json: the judged half, per model and slot.

    A ruling either names a library entry -- which keeps it traced -- or gives roughness and
    metal outright, which is an invention and has to say why.
    """
    path = os.path.join(ROOT, "sheets", "materials.json")
    if not os.path.exists(path):
        return {}, []
    sheet = json.load(open(path))
    rulings, problems = {}, []
    for rule in sheet.get("rulings", []):
        key = (rule["model"], rule["slot"])
        if "material" in rule:
            entry = library.get(rule["material"])
            if entry is None:
                problems.append(f"ruling {key} names {rule['material']}, which is not in the "
                                "library")
                continue
            rulings[key] = dict(entry)
        elif "roughness" in rule and "metal" in rule:
            if not rule.get("why"):
                problems.append(f"ruling {key} invents roughness and metal and gives no why; "
                                "PLAN.md's rule is traced or marked")
            rulings[key] = {"name": f"{rule['model']}/{rule['slot']} (invention)",
                            "roughness": float(rule["roughness"]),
                            "metallic": float(rule["metal"]),
                            "relief": float(rule.get("relief", 0.0))}
        else:
            problems.append(f"ruling {key} says neither a library material nor a pair of "
                            "numbers")
    return rulings, problems


# ---------------------------------------------------------------------------- the run

def index_entries(index):
    """Model name -> its index.json entry, for every section that names a glb.

    `glb` is a string on nearly every entry and a list on the few built from more than one
    part (SkeletonWarrior), so both are walked.
    """
    entries = {}
    for section in ("objects", "monsters", "npc"):
        for entry in index.get(section, []) or []:
            paths = entry.get("glb")
            if isinstance(paths, str):
                paths = [paths]
            for path in paths or []:
                entries[os.path.splitext(os.path.basename(path))[0]] = entry
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--census", help="write the whole per-slot table here as markdown")
    parser.add_argument("--model", help="audit one model and print every slot")
    parser.add_argument("--no-cooked", action="store_true",
                        help="skip the cooked .ktx format checks")
    parser.add_argument("--record", action="store_true",
                        help="rewrite the baseline from this run's failures")
    parser.add_argument("--strict", action="store_true",
                        help="fail on every failure, baseline or not")
    args = parser.parse_args()

    index = json.load(open(os.path.join(ASSETS, "index.json")))
    library = {m["name"]: m for m in index["materials"]}
    entries = index_entries(index)
    rulings, sheet_problems = read_rulings(library)

    failures = []
    notes = []
    unruled = []
    grounds = []
    all_slots = []
    paths = sorted(glob.glob(os.path.join(ASSETS, "**", "*.glb"), recursive=True))
    if args.model:
        paths = [p for p in paths
                 if os.path.splitext(os.path.basename(p))[0].lower() == args.model.lower()]
        if not paths:
            print(f"matcheck: no model named {args.model}", file=sys.stderr)
            return 2

    for path in paths:
        try:
            slots = read_model(path, library)
        except Exception as error:  # a file that will not parse is a failure, not a crash
            model = os.path.splitext(os.path.basename(path))[0]
            fail(failures, model, model, f"will not parse: {error}")
            continue
        model = slots[0].model if slots else None
        if model and is_ground(model):
            grounds.append(model)
            continue
        seen = collections.Counter(s.name for s in slots)
        for slot in slots:
            slot.ruling = rulings.get((slot.model, slot.name))
            if seen[slot.name] > 1:
                fail(failures, slot.model, f"{slot.model}/{slot.name}",
                     f"{seen[slot.name]} slots share this name; a ruling could not tell them "
                     "apart")
            if slot.library is None and slot.ruling is None:
                slot.nearest = nearest_library(slot, library)
            check_slot(slot, library.get(slot.name), failures, notes, unruled)
            all_slots.append(slot)
        if model:
            check_metal_list(slots, entries.get(model), failures)

    cooked = 0 if args.no_cooked else check_cooked_textures(failures)

    # ------------------------------------------------------------------ the report
    total = len(all_slots)
    named = sum(1 for s in all_slots if s.library is not None)
    ruled = sum(1 for s in all_slots if s.ruling is not None)
    area = sum(s.area for s in all_slots) or 1.0
    named_area = sum(s.area for s in all_slots if s.library or s.ruling)

    print(f"matcheck: {len(paths)} models, {total} material slots, {cooked} cooked textures")
    if grounds:
        print(f"  {len(grounds)} ground meshes skipped: the land has its own shader and its "
              "surfaces come from the world, not from a material slot")
    print(f"  library material: {named} slots, plus {ruled} by ruling  "
          f"({100.0 * (named + ruled) / max(total, 1):.0f}% of slots, "
          f"{100.0 * named_area / area:.0f}% of the area)")
    print(f"  unruled: {len(unruled)} slots on "
          f"{len({s.model for s, _ in unruled})} models")

    for problem in sheet_problems:
        print(f"  SHEET {problem}")

    if args.model:
        for slot in all_slots:
            want = slot.ruling or slot.library
            claim = want["name"] if want else "-- nothing --"
            print(f"  slot {slot.index} {slot.name!r} -> {claim}")
            print(f"    {slot.triangles} triangles, area {slot.area:.2f}, "
                  f"{slot.alpha_mode}"
                  f"{f' cutout {slot.cutout:.2f}' if slot.cutout > 0 else ''}"
                  f"{' two-sided' if slot.two_sided else ''}")
            if slot.roughness is not None:
                print(f"    ORM occlusion {slot.occlusion:.3f} roughness {slot.roughness:.3f} "
                      f"metal {slot.metal:.3f}")
            if want:
                print(f"    asks roughness {want['roughness']:.2f} metal "
                      f"{want['metallic']:.2f} relief {want.get('relief', 0):.2f}")
            print(f"    maps: {', '.join(f'{k}={v}' for k, v in slot.maps.items()) or 'none'}")

    if unruled and not args.model:
        # Worst first by area: what covers the most screen is what is worth a ruling first.
        # Each line says which library entries the measured ORM could belong to, which turns
        # some rulings into a yes -- but the library clusters hard between 0.7 and 0.9, so most
        # of these cannot be settled by the numbers and want an eye on a shot.
        inferred = sum(1 for slot, _ in unruled if slot.nearest and slot.nearest[1] == 1)
        missing = sum(1 for slot, _ in unruled if slot.roughness is None)
        print(f"\n  of the {len(unruled)} unruled, {inferred} fit exactly one library entry "
              f"and {missing} have no ORM map at all. Largest area first:")
        for slot, kind in sorted(unruled, key=lambda pair: -pair[0].area)[:25]:
            rough = f"{slot.roughness:.2f}" if slot.roughness is not None else "none"
            if slot.roughness is None:
                guess = "NO ORM MAP -- falls back to roughness 1"
            elif slot.nearest is None or slot.nearest[0] is None:
                guess = "matches no library entry"
            elif slot.nearest[1] == 1:
                guess = f"can only be {slot.nearest[0]['name']}"
            else:
                # Roughness alone does not identify a material: the library clusters hard
                # between 0.7 and 0.9, so most of these need an eye rather than a number.
                guess = f"{slot.nearest[1]} entries fit; the ORM cannot say which"
            print(f"    {slot.model}/{slot.name}  area {slot.area:7.2f}  roughness "
                  f"{rough}  {guess}")

    # The baseline is what makes this a gate rather than a report. Every failure here is real
    # and none of them can be fixed today -- most are repairs in MU2's pipeline, and one is a
    # judgement only the user can make -- so a run that failed on all of them would be
    # switched off within a week. Instead the known ones are recorded once and the gate fails
    # on what is new, and on any recorded failure that has since been fixed, so the file
    # shrinks and cannot quietly grow.
    baseline_path = os.path.join(ROOT, "sheets", "materials.baseline.json")
    baseline = set()
    if os.path.exists(baseline_path) and not args.record:
        baseline = set(json.load(open(baseline_path))["known"])

    keys = {failure_key(what, why): (what, why) for _, what, why in failures}
    # One model's run saw only one model's slots, so every baseline entry for every other
    # model would look fixed. A single model lists its failures and settles nothing.
    single = bool(args.model)
    fresh = sorted(set(keys) - baseline) if not single else sorted(keys)
    fixed = sorted(baseline - set(keys)) if not single else []

    if notes:
        print(f"\n  {len(notes)} notes (nothing renders wrong):")
        kinds = collections.Counter(why.split(';')[0] for _, _, why in notes)
        for why, count in kinds.most_common():
            print(f"    {count} x {why}")

    if failures:
        if single:
            print(f"\n  {len(failures)} failures:")
        else:
            print(f"\n  {len(failures)} failures, {len(baseline)} of them known:")
        shown = failures if args.strict else [f for f in failures
                                              if failure_key(f[1], f[2]) in fresh]
        for _, what, why in shown[:40]:
            print(f"    FAIL {what}: {why}")
        if len(shown) > 40:
            print(f"    ... and {len(shown) - 40} more")
        if not shown:
            print("    all of them known; --strict lists them")

    if fixed:
        print(f"\n  {len(fixed)} baseline failures no longer happen -- rerun with --record:")
        for key in fixed[:10]:
            print(f"    FIXED {key}")

    if args.record:
        if single:
            print("\n  --record needs the whole run; one model cannot rewrite the baseline")
            return 2
        os.makedirs(os.path.dirname(baseline_path), exist_ok=True)
        json.dump({
            "comment": "Material failures known on the day matcheck was written. The gate "
                       "fails on anything not in here, and on anything in here that has been "
                       "fixed. It only shrinks.",
            "known": sorted(keys),
        }, open(baseline_path, "w"), indent=1)
        print(f"\n  baseline recorded: {len(keys)} known failures")
        return 0

    if args.census:
        write_census(args.census, all_slots, unruled, failures, notes, library, cooked)
        print(f"\n  census written to {args.census}")

    if args.strict:
        return 1 if failures else 0
    return 1 if (fresh or fixed) else 0


def write_census(path, slots, unruled, failures, notes, library, cooked):
    lines = []
    lines.append("# The material census\n")
    lines.append("Written by `tools/matcheck.py`. Every material slot in `assets/`, what "
                 "library entry\nit claims, and what its baked ORM actually carries.\n")
    total = len(slots)
    named = sum(1 for s in slots if s.library or s.ruling)
    lines.append(f"{total} slots on {len({s.model for s in slots})} models, "
                 f"{cooked} cooked textures. {named} slots "
                 f"({100.0 * named / max(total, 1):.0f}%) resolve to a library material; "
                 f"{len(unruled)} do not.\n")
    lines.append("## Unruled slots\n")
    lines.append("| model | slot | triangles | area | ORM rough | ORM metal |")
    lines.append("|---|---|---:|---:|---:|---:|")
    for slot, _ in sorted(unruled, key=lambda pair: -pair[0].area):
        rough = f"{slot.roughness:.3f}" if slot.roughness is not None else "-"
        metal = f"{slot.metal:.3f}" if slot.metal is not None else "-"
        lines.append(f"| {slot.model} | {slot.name} | {slot.triangles} | {slot.area:.2f} | "
                     f"{rough} | {metal} |")
    lines.append("\n## Every slot\n")
    lines.append("| model | slot | claims | ORM rough | asks | ORM metal | asks | flags |")
    lines.append("|---|---|---|---:|---:|---:|---:|---|")
    for slot in sorted(slots, key=lambda s: (s.model, s.index)):
        want = slot.ruling or slot.library
        flags = []
        if slot.cutout > 0:
            flags.append(f"cutout {slot.cutout:.2f}")
        if slot.two_sided:
            flags.append("two-sided")
        rough = f"{slot.roughness:.3f}" if slot.roughness is not None else "-"
        metal = f"{slot.metal:.3f}" if slot.metal is not None else "-"
        claims = want["name"] if want else "**none**"
        wants_rough = f"{want['roughness']:.2f}" if want else "-"
        wants_metal = f"{want['metallic']:.2f}" if want else "-"
        lines.append(f"| {slot.model} | {slot.name} | {claims} | {rough} | {wants_rough} "
                     f"| {metal} | {wants_metal} | {', '.join(flags)} |")
    if failures:
        lines.append(f"\n## {len(failures)} failures\n")
        for model, what, why in failures:
            lines.append(f"- **{what}** — {why}")
    if notes:
        lines.append(f"\n## {len(notes)} notes\n")
        lines.append("Nothing renders wrong because of these.\n")
        for model, what, why in notes:
            lines.append(f"- **{what}** — {why}")
    open(path, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    sys.exit(main())
