#!/usr/bin/env python3
"""Turns assets/ into what the engine loads. Sprint 3's cook; nothing else runs it.

What it does today is the textures, which are the load time and the VRAM: Lorencia's town
carries 587 images inside 97.6 MB of .glb, and the engine decodes every one of them and
builds its mip chain at startup. Cooked, they are BC7 and BC5 blocks in .ktx files with the
chain already in them, and a cutout's alpha has been rescaled down that chain so a leaf
keeps its coverage -- the rule docs/conventions.md records as owed.

The meshes are the other half, and they are here for a reason that is not the triangles:
the town's 105 models are 17 790 triangles inside 97.6 MB of .glb, because nearly all of
that file is the images now cooked out of it. A flat .mum is about two megabytes the engine
reads whole, with no glTF parser and no buffer walk at load.

The third part is the town itself: 2845 placements become one flat list, already in metres,
already on the right axes, already sorted into the chunks a frame walks. Foundation 7 of
PLAN.md says chunk bounds and each kind's range are settled once and not per frame, and the
cook is the once.

    tools/cook.py --world lorencia [--only textures|meshes|placements|missiles|all] [--chunk 32]

The .mut format ("MU2 town"), version 2, little-endian:

    'MU2T', u32 version, u32 models, u32 chunks, u32 instances,
    u32 size (tiles a side), u32 chunkTiles, f32 metresPerTile
    models:    u16 name length and bytes, u16 mesh path length and bytes,
               f32 bounds min[3], f32 bounds max[3], u32 instances of it
    chunks:    f32 bounds min[3], f32 bounds max[3], u32 firstInstance, u32 instanceCount,
               u16 column, u16 row
    instances: f32 position[3], f32 yaw, f32 pitch, f32 roll, f32 scale,
               u16 model, u16 flags, u8 light[3], u8 spare   -- 36 bytes, and 36 is what a
               C++ struct of those fields is too, with no implicit padding anywhere
    emitters:  u32 count, then 48 bytes each -- u16 model (0xFFFF: a world anchor), u8 kind
               (0 lamp, 1 fire, 2 candle, 3 window, 4 smoke), u8 spare, f32 at[3], f32
               colour[3], f32 low, f32 high, f32 reach, f32 flickerHz, f32 smoothSeconds
    glows:     u32 count, then 20 bytes each -- u16 model, u16 spare, f32 low, f32 high,
               f32 flickerHz, f32 smoothSeconds

Version 2 added the last two tables (sprint 8a, docs/sprints/08a-the-lamps.md). An emitter's
`at` is in metres on our axes: in the model's own frame, unscaled -- MU turns the offset by the
object's angle and never scales it -- or, for an anchor, in the world. `reach` is MU's range in
tiles, which is metres. A glow is how an object's BlendMesh flickers, one per model, since MU
keeps one BlendMeshLight an object.

`flags` bit 0 says the placement was laid on the terrain rather than used as the map stores
it; bit 1 that its model is a roof, one of the pieces index.json marks `roof_fade`, which the
game hides while the hero stands indoors. The rest are spare. `light` is MU's own baked terrain light at the placement's tile,
which is what stops the town standing unlit on lit ground.

Instances are sorted by chunk and then by model, so one chunk that survives a cull is a run
of instances and each model inside it is a contiguous sub-run: an instanced draw is a range,
found without a sort and without a map lookup.

The .mum format, version 3 (static) and 4 (skinned), little-endian throughout:

    'MU2M', u32 version, u32 vertices, u32 indices, u32 parts, u32 materials
    f32 bounds min[3], f32 bounds max[3]        (the centre and radius are derived at load)
    vertices:  position[3] normal[3] tangent[4] uv[2], 48 bytes, content::Vertex exactly
    indices:   u32 each
    parts:     u32 firstIndex, u32 indexCount, u32 material
    materials: f32 cutout (-1 for none), u8 flags (bit 5: a glow's pulse a, b and jitter
               follow as three f32 after everything else; bit 6: a glow that casts a shadow, its
               strength one f32 after those), (bit 0 two-sided, bit 1 a glow: MU's
               BlendMesh, drawn added and nowhere else, bit 2 translucent), then five
               strings -- name, albedo, normal, orm, emissive -- each u16 length and its
               bytes, the four texture strings being paths under assets/ or empty for none,
               then f32 roughnessFactor, f32 metalFactor, and when bit 2 is set one more
               f32, the translucency. A file with no bit 2 reads as it always did.

Versions 1 and 2 were the same file without those last two floats, and are refused rather
than defaulted: the factor is the whole answer on 195 of this content's material slots, so a
reader that assumed 1.0 would draw MU's foliage, grass and water as the missing-map fallback
and look like it had worked. See orm_factors.

Node transforms are baked, which in this content is a check rather than a step: nothing in
Lorencia's 105 models puts a transform on a mesh node, so the flattening content/mesh.cpp
already does is right here, and the cook refuses loudly rather than quietly if a world
arrives where it is not.

Three decisions this file makes, and each is a decision rather than a detail:

  * **What an image is for is decided here, not by its name.** A glTF material says which
    of its textures is the base colour, which is the normal map and which carries occlusion,
    roughness and metal; that is what picks BC7-sRGB, BC5 or BC7-linear. An image used in
    two roles by two materials is cooked twice, because the role is the format.
  * **A cutout's threshold comes from its material**, off `alphaMode` and `alphaCutoff` as
    content/mesh.cpp reads it, and travels with the albedo it belongs to. No pixel is
    inspected to decide it.
  * **The same bytes are cooked once.** MU2's build embeds the same sheet in many models;
    they are deduplicated by the hash of the source bytes *and the role*, and the manifest
    maps every use onto the one file.
"""

import argparse
import array
import hashlib
import json
import math
import os
import struct
import subprocess
import sys
import time
import wave
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

# What a new character is holding on the day he is made, by index.json's own names. OpenMU's
# Version075 character-created plug-ins: AddSmallAxeForDarkKnight puts a Small Axe in the
# knight's hand and AddShortBowForFairyElf a Short Bow in the elf's; the Dark Wizard is created
# empty-handed and taught Energy Ball instead. MU2's `Cradle.cs` holds the same three rows.
#
# They are named here because nothing else reaches them: index.json's characters are the town's
# people and the armoured Dark Knight, and every one of them is holding something a level-one
# character has not earned. A hero who starts with nothing on him has nothing to hold unless
# these two meshes are cooked, and the naked figure with an axe is the game's first minute.
CRADLE_ARMS = ("Axe01", "Bow01")


def read_glb(path):
    """The JSON chunk and the binary chunk of a .glb."""
    with open(path, "rb") as handle:
        magic, _version, _length = struct.unpack("<III", handle.read(12))
        if magic != 0x46546C67:
            raise ValueError(f"{path} is not a .glb")
        json_length, _kind = struct.unpack("<II", handle.read(8))
        document = json.loads(handle.read(json_length))
        binary = b""
        header = handle.read(8)
        if len(header) == 8:
            binary_length, _kind = struct.unpack("<II", header)
            binary = handle.read(binary_length)
    return document, binary


def image_bytes(document, binary, index):
    """One image's file bytes, whether it is embedded or beside the .glb."""
    image = document["images"][index]
    if "bufferView" in image:
        view = document["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        return binary[start:start + view["byteLength"]]
    return None


def roles_of(document):
    """Every (image index, role, cutout) a model's materials ask for.

    glTF's own layout, which is what MU2's pipeline writes: base colour is the albedo,
    `metallicRoughnessTexture` carries roughness in G and metal in B with occlusion written
    into its R, and the separate occlusion texture is the fallback content/mesh.cpp uses.
    """
    wanted = {}

    def texture_image(view):
        if not view:
            return None
        texture = document.get("textures", [])[view["index"]]
        return texture.get("source")

    for material in document.get("materials", []):
        cutout = -1.0
        if material.get("alphaMode") == "MASK":
            cutout = float(material.get("alphaCutoff", 0.5))

        pbr = material.get("pbrMetallicRoughness", {})
        pairs = [
            (texture_image(pbr.get("baseColorTexture")), "albedo", cutout),
            (texture_image(pbr.get("metallicRoughnessTexture")), "orm", -1.0),
            (texture_image(material.get("normalTexture")), "normal", -1.0),
            (texture_image(material.get("emissiveTexture")), "emissive", cutout),
        ]
        if pairs[1][0] is None:
            pairs[1] = (texture_image(material.get("occlusionTexture")), "orm", -1.0)

        for index, role, threshold in pairs:
            if index is None:
                continue
            key = (index, role)
            # A sheet used by a cutout material and an opaque one keeps the cutout: the
            # rescale holds coverage the opaque path never asks about.
            if key not in wanted or threshold > wanted[key]:
                wanted[key] = threshold
    return [(index, role, threshold) for (index, role), threshold in wanted.items()]


def safe(name):
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in name)


# What flies over a world, by the model name index.json knows it by. MU chooses it with
# `switch (gMapManager.WorldActive)` in GOBoid.cpp -- Lorencia gets MODEL_BIRD01, the Dungeon
# and Lost Tower get bats, Blood Castle crows, Noria a butterfly -- and none of them is PLACED:
# the pool is spawned around the player and exists nowhere on the map. So the model reaches
# nothing the town cook walks, and has to be named here or it is never cooked at all.
#
# It is a world's fact and not a boid's, which is why it is a table and not a constant: the
# velocity, whether the terrain's light falls on it and whether it calls are the same kind of
# fact and live beside it in the engine (game/world/boids.h), as MU2's `Airs.cs` gathered them.
AIRS = {"lorencia": "Bird01", "noria": "Butterfly01", "dungeon": "Bat01"}
# And what runs along the floor: MU's fish slot (MoveFishs), which the Dungeon fills with
# MODEL_RAT01 (GOBoid.cpp:1720-1722). Unplaced for the same reason, so named here too; the
# engine's pool is game/world/scurry.h.
CRAWLS = {"dungeon": "Rat01"}
# And what a trap throws: the Lance Trap's saw (MODEL_SAW), and the ceiling's falling pebble
# (MODEL_DUNGEON_STONE01), drawn by game/world/trap_show.h and placed by nothing.
TRAP_MODELS = {"dungeon": ("Saw01", "DungeonStone01")}
# Where a world's boid is built, when it is not the world's own: MuMain loads Noria's
# MODEL_BUTTERFLY01 from Data/Object1, Lorencia's objects (MapManager.cpp:89).
AIRS_FROM = {"noria": "lorencia"}


def boid_glb(world, model):
    """The .glb a model of this world is cooked from: its own folder, else the boid's home."""
    own = os.path.join(ASSETS, "world", world, model, f"{model}.glb")
    if os.path.exists(own) or model != AIRS.get(world) or world not in AIRS_FROM:
        return own
    return os.path.join(ASSETS, "world", AIRS_FROM[world], model, f"{model}.glb")


def crawling(world):
    """The floor boid this world runs, if its glb is there, else nothing."""
    name = CRAWLS.get(world)
    if name is None or not os.path.exists(os.path.join(ASSETS, "world", world, name, f"{name}.glb")):
        return None
    return name


def flying(world):
    """The boid model this world flies, if its glb is there, else nothing."""
    name = AIRS.get(world)
    if name is None:
        return None
    if not os.path.exists(boid_glb(world, name)):
        return None
    return name


def collect(world, out_dir, raw_dir):
    """Every image the world's models reach for, written out ready to compress."""
    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)

    # The boid's sheet comes with the town's, not with the effects': it is a MODEL's albedo,
    # cooked by the same rules and looked up through the same textures.json the .mum names.
    models = sorted({one["model"] for one in map_data["objects"]} | set(filter(None, [flying(world), crawling(world)])) | set(TRAP_MODELS.get(world, ())))
    jobs = []
    manifest = {}
    seen = {}
    drawn = 0
    missing = []

    for model in models:
        path = boid_glb(world, model)
        if not os.path.exists(path):
            missing.append(model)
            continue
        drawn += 1
        document, binary = read_glb(path)
        for index, role, cutout in roles_of(document):
            data = image_bytes(document, binary, index)
            if data is None:
                uri = document["images"][index].get("uri")
                if uri is None:
                    continue
                with open(os.path.join(os.path.dirname(path), uri), "rb") as handle:
                    data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            key = f"{digest}-{role}"
            source_key = f"{model}#{index}:{role}"
            if key in seen:
                manifest[source_key] = seen[key]
                continue

            name = document["images"][index].get("name") or f"image{index}"
            stem = f"{safe(model)}_{safe(name)}_{role}_{digest}"
            ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
            relative = os.path.relpath(ktx_path, ASSETS)
            seen[key] = relative
            manifest[source_key] = relative
            # A .ktx that is there is a .ktx of these bytes in this role, because the stem
            # carries the sha1 of the source AND the role -- so existence is the up-to-date
            # test and the file is left alone. This is `cook_figures`' own rule, which the
            # world's collect did not have: a map where two textures had gone missing
            # re-compressed all 540 of Lorencia's to make them, twenty minutes of BC7 for two
            # files (2026-09-23, and the user asked the obvious question about it).
            #
            # What it costs: a .ktx that is corrupt rather than absent is never noticed. That
            # is the same bargain the figures have made since they were cooked, and the answer
            # to it is the same -- delete the file, or the directory, and cook again.
            if os.path.exists(ktx_path):
                continue
            raw_path = os.path.join(raw_dir, stem + ".bin")
            with open(raw_path, "wb") as handle:
                handle.write(data)
            jobs.append((role, cutout, raw_path, ktx_path))

    return jobs, manifest, drawn, missing, len(models)


def ground_jobs(world, out_dir, manifest):
    """The land's own sheets, which are loose .png beside ground_surfaces.json."""
    world_dir = os.path.join(ASSETS, "world", world)
    surfaces_path = os.path.join(world_dir, "ground_surfaces.json")
    if not os.path.exists(surfaces_path):
        return []
    with open(surfaces_path) as handle:
        surfaces = json.load(handle)

    jobs = []
    seen = {}
    for entry in surfaces.values() if isinstance(surfaces, dict) else surfaces:
        for half in ("base", "overlay"):
            layer = entry.get(half) if isinstance(entry, dict) else None
            if not layer:
                continue
            for field, role in (("albedo", "albedo"), ("normal", "normal"), ("orm", "orm")):
                name = layer.get(field)
                if not name:
                    continue
                source = os.path.join(world_dir, name)
                if not os.path.exists(source):
                    continue
                key = (name, role)
                if key in seen:
                    continue
                stem = f"ground_{safe(os.path.splitext(name)[0])}_{role}"
                ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
                relative = os.path.relpath(ktx_path, ASSETS)
                seen[key] = relative
                manifest[f"ground/{name}:{role}"] = relative
                # Left alone where it is already newer than the .png it was made from.
                # `collect` skips a model's sheet on EXISTENCE, because that stem carries the
                # sha1 of the source; these stems carry only the name, so the file has to be
                # dated instead. Without this the 28 land sheets -- the biggest images in the
                # world, and the whole of the time -- were re-compressed on every texture
                # cook, however little had changed. Cooking Bird01, one 64-pixel bird, queued
                # 29 jobs of which 28 were Lorencia's ground (2026-09-23).
                if (os.path.exists(ktx_path)
                        and os.path.getmtime(ktx_path) >= os.path.getmtime(source)):
                    continue
                jobs.append((role, -1.0, source, ktx_path))
    return jobs


COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4),
             5126: ("f", 4)}
COMPONENTS_OF = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_accessor(document, binary, index):
    """One accessor as a list of tuples, de-quantised as glTF says it should be."""
    accessor = document["accessors"][index]
    if "sparse" in accessor:
        raise ValueError("a sparse accessor, which this cook does not read")
    kind, size = COMPONENT[accessor["componentType"]]
    width = COMPONENTS_OF[accessor["type"]]
    count = accessor["count"]
    if "bufferView" not in accessor:
        return [(0.0,) * width] * count

    view = document["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride") or size * width
    normalised = accessor.get("normalized", False)
    divisor = {"b": 127.0, "B": 255.0, "h": 32767.0, "H": 65535.0}.get(kind, 1.0)

    out = []
    layout = struct.Struct("<" + kind * width)
    for i in range(count):
        at = start + i * stride
        values = layout.unpack_from(binary, at)
        if normalised:
            values = tuple(max(v / divisor, -1.0) for v in values)
        out.append(values)
    return out


def write_string(text):
    raw = text.encode("utf-8")
    return struct.pack("<H", len(raw)) + raw


def identity(node):
    if "matrix" in node:
        return all(abs(a - b) < 1e-6 for a, b in
                   zip(node["matrix"], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]))
    return not ({"translation", "rotation", "scale"} & set(node))


def skin_bones(document, binary, skin):
    """One skin as (name, parent, inverse bind) a bone, in the skin's own joint order.

    The parent is an index into that same order, or -1. Every joint whose parent is a joint
    is preceded by it in MU2's exports -- checked here rather than assumed, because a pose
    composed in one walk of a flat array is only right if that holds.
    """
    nodes = document.get("nodes", [])
    joints = skin["joints"]
    place = {node: index for index, node in enumerate(joints)}
    parent_node = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parent_node[child] = index

    inverse = read_accessor(document, binary, skin["inverseBindMatrices"])
    bones = []
    for index, node in enumerate(joints):
        parent = place.get(parent_node.get(node, -1), -1)
        if parent >= index:
            raise ValueError("a joint stands before its own parent in the skin's order")
        bones.append((nodes[node].get("name", f"bone{index}"), parent, inverse[index]))
    return bones


def primitive_material_name(document, primitive):
    """The name of the material a primitive draws with, or "" if it has none."""
    index = primitive.get("material")
    materials = document.get("materials", [])
    return materials[index].get("name", "") if index is not None and index < len(materials) else ""


def cook_mesh(model, path, out_path, textures, hidden=None, scroll_per_second=0.0,
              scrolls=None):
    """One .glb into one .mum. Returns (triangles, vertices, bytes, bones).

    A skinned .glb writes version 4: a 56-byte vertex with four joint bytes and four weight
    bytes on the end of the 48, and the skin's own bone table after the materials. An
    unskinned one writes version 3 exactly as the town's models do.

    `hidden` is `index.json`'s `hidden_mesh`: MU's `c->Object.HiddenMesh`, the mesh a variant
    puts away -- the plain Hound's helm, the plain Bull Fighter's crest. It is dropped here
    rather than skipped at load.

    `scroll_per_second` is the asset's `glow.<sheet>.scrolls_per_second` -- MoveObject's
    BlendMeshTexCoordV, which slides the one additive submesh's V coordinate off the world
    clock: the waterspout's fall, and the two houses' lit windows sliding behind their
    panes. It rides on the model's glow material (flag bit 2, the only BLEND material a
    world object has ever shipped with two of), and is nothing on every other kind of mesh,
    which is why it is a parameter here and not read from the glb: a glb has no clock in it.

    `scrolls` is the asset's whole `glow` table, for a model whose glows do not all slide alike:
    a sheet named there with an `axis` or a `mask_held` scrolls by its own entry and the
    model's other glows stand still. The Lost Tower's machines are the case -- MU slides their
    t20 orb and taa01's red rings along U, and taa03's red centre not at all (ZzzObject.cpp:
    1035-1059, 4006-4009). `axis` "u" slides U instead of V; `mask_held` keeps the sheet's
    alpha where it is while its colour slides, which is MU's chrome streaming behind a band
    that does not move. Bit 7 carries both, as one byte after bit 6's float.

    The number is MU's mesh index in the .bmd and not a glb primitive index. MU2's exporter
    groups primitives by material and ships the mesh to put away as a part called `hidden`,
    so that name is what is dropped. Taken as a primitive index until 2026-09-21, it dropped
    the Hound's bare fur head and drew it in the Hell Hound's brass helm, and cut six
    triangles of horn off the Bull Fighter while leaving it the Elite's crest. The index is
    kept only for a glb with no part of that name.
    """
    document, binary = read_glb(path)
    # A part named outright (a row's `hidden_part`): that material's primitives go, whatever
    # the glb calls `hidden`.
    by_name = hidden if isinstance(hidden, str) else None
    if by_name is not None:
        hidden = None
    named_hidden = hidden is not None and any(
        primitive_material_name(document, primitive) == "hidden"
        for mesh in document.get("meshes", []) for primitive in mesh.get("primitives", []))

    for node in document.get("nodes", []):
        if "mesh" in node and not identity(node):
            raise ValueError(f"{model}: a mesh node carries a transform, and this cook bakes "
                             f"none -- see content/mesh.cpp, which flattens the same way")

    skins = document.get("skins", [])
    if len(skins) > 1:
        raise ValueError(f"{model}: {len(skins)} skins, and a figure here is one palette")
    bones = skin_bones(document, binary, skins[0]) if skins else []
    if len(bones) > 255:
        raise ValueError(f"{model}: {len(bones)} bones, and a joint index is one byte")

    vertices = bytearray()
    indices = bytearray()
    parts = []
    vertex_count = 0
    low = [1e30] * 3
    high = [-1e30] * 3
    primitive_index = -1

    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            attributes = primitive["attributes"]
            if "POSITION" not in attributes:
                continue
            primitive_index += 1
            if by_name is not None and primitive_material_name(document, primitive) == by_name:
                continue
            if hidden is not None and (
                    primitive_material_name(document, primitive) == "hidden" if named_hidden
                    else primitive_index == hidden):
                continue
            positions = read_accessor(document, binary, attributes["POSITION"])
            count = len(positions)
            normals = (read_accessor(document, binary, attributes["NORMAL"])
                       if "NORMAL" in attributes else [(0.0, 1.0, 0.0)] * count)
            tangents = (read_accessor(document, binary, attributes["TANGENT"])
                        if "TANGENT" in attributes else None)
            # TEXCOORD_0 and nothing else. Every primitive in MU2's build carries a second
            # set as well, and content/mesh.cpp drops it for the same reason: set 1 is a
            # lightmap or a second layer that nothing in this frame samples.
            uvs = (read_accessor(document, binary, attributes["TEXCOORD_0"])
                   if "TEXCOORD_0" in attributes else [(0.0, 0.0)] * count)
            if tangents is None:
                raise ValueError(f"{model}: a primitive with no TANGENT, which the cook would "
                                 f"have to derive; every one in this world has its own")

            # JOINTS_0 is UNSIGNED_SHORT in every figure MU2 exports, and bgfx has no
            # unsigned 16-bit vertex attribute at all. Handing those bytes over as Uint8
            # reads the low byte of one index and the high byte of the next -- half the
            # palette collapsed onto bone 0, which looks like a broken rig rather than like
            # a type error, and Metal says nothing. The conversion is lossless here because
            # the largest rig in this content is 115 joints, and the line above refuses
            # anything that is not.
            joints = weights = None
            if bones:
                if "JOINTS_0" not in attributes or "WEIGHTS_0" not in attributes:
                    raise ValueError(f"{model}: a skinned mesh with no JOINTS_0/WEIGHTS_0")
                joints = read_accessor(document, binary, attributes["JOINTS_0"])
                weights = read_accessor(document, binary, attributes["WEIGHTS_0"])

            base = vertex_count
            for i in range(count):
                p = positions[i]
                for axis in range(3):
                    low[axis] = min(low[axis], p[axis])
                    high[axis] = max(high[axis], p[axis])
                vertices += struct.pack("<3f3f4f2f", *p[:3], *normals[i][:3], *tangents[i][:4],
                                        *uvs[i][:2])
                if bones:
                    index = [int(v) for v in joints[i][:4]]
                    if max(index) >= len(bones):
                        raise ValueError(f"{model}: a vertex names joint {max(index)} of "
                                         f"{len(bones)}")
                    # Quantised so the four weights sum to 255 exactly: dropping the
                    # remainder darkens or stretches whichever vertex it fell off, and the
                    # shader must not have to normalise what the cook can settle once.
                    raw = [max(0.0, float(v)) for v in weights[i][:4]]
                    total = sum(raw) or 1.0
                    scaled = [int(round(v * 255.0 / total)) for v in raw]
                    drift = 255 - sum(scaled)
                    scaled[scaled.index(max(scaled))] += drift
                    vertices += struct.pack("<4B4B", *index, *scaled)
            vertex_count += count

            first_index = len(indices) // 4
            if "indices" in primitive:
                for value in read_accessor(document, binary, primitive["indices"]):
                    indices += struct.pack("<I", base + int(value[0]))
            else:
                for i in range(count):
                    indices += struct.pack("<I", base + i)
            parts.append((first_index, len(indices) // 4 - first_index,
                          primitive.get("material", len(document.get("materials", [])))))

    materials = bytearray()
    material_list = document.get("materials", [])
    for index, material in enumerate(material_list):
        cutout = -1.0
        flags = 1 if material.get("doubleSided") else 0
        mode = material.get("alphaMode")
        if mode == "MASK":
            cutout = float(material.get("alphaCutoff", 0.5))
        elif mode == "BLEND":
            # A glow: MU's BlendMesh, the one submesh an object draws ADDED to the frame at
            # its BlendMeshLight -- the fires, the candles, the street light's smear, the lit
            # window panes and the waterspout's fall. Bit 1 of the flags, and the renderer
            # takes these out of the shadow, the prepass and the shade and draws them in the
            # transparent pass instead. Sprint 8a; they were 0.5 cutouts drawn opaque until
            # then, and printed themselves into the shadow map.
            flags |= 2
        # Translucency: MU2's pipeline carries light that comes THROUGH a leaf as an
        # emissive that is the leaf's own sheet at a fraction (0.28 on every grass, flower
        # and tree here). It is not a light source, and read as one at 1.0 -- which is what
        # dropping the factor did -- every blade in Lorencia shone its full colour, by day
        # and at night alike. Bit 2 says so, and the factor follows the metal factor; the
        # shade scales it by the light the scene has. A glow's emissive is a real one.
        translucency = 0.0
        if not flags & 2 and material.get("emissiveTexture") is not None:
            through = material.get("emissiveFactor", [1.0, 1.0, 1.0])
            same = (material.get("pbrMetallicRoughness", {}).get("baseColorTexture", {})
                    .get("index") == material["emissiveTexture"]["index"])
            if same and max(through) < 1.0:
                flags |= 4
                translucency = float(max(through))
        # Bit 3: the albedo's metal is already reflectance. MU2's item bake lifts a metal's
        # painted texels onto the metal's own f0 and names the sheet `<item>_basecolor`
        # (build_maps.base_colour); the world's tiled sheets are MU's dark paint as it is.
        # The shade's metal_gain is for the second kind only -- applied to the first it lifted
        # the armour twice. See content::Material::calibrated.
        albedo_image = image_for(document, material, "albedo")
        if albedo_image is not None and (document["images"][albedo_image].get("name") or ""
                                         ).endswith("_basecolor"):
            flags |= 8
        # Bit 4: this is the model's one glow material and the asset named a scroll rate for
        # it -- MoveObject's BlendMeshTexCoordV, the waterspout's fall and the two houses'
        # lit windows. A model with none writes nothing extra, so every file cooked before
        # this reads the same as it always did.
        rate = scroll_per_second
        mode = 0
        if scrolls and any(("axis" in one or "mask_held" in one) for one in scrolls.values()):
            own = scrolls.get(material.get("name", ""))
            rate = float(own.get("scrolls_per_second", 0.0)) if own else 0.0
            if own:
                mode = (1 if own.get("axis") == "u" else 0) | (2 if own.get("mask_held") else 0)
        if flags & 2 and rate:
            flags |= 16
        if flags & 16 and mode:
            flags |= 128
        # Bit 5: an item's glow -- drawn at its own level, not the world's glow_strength --
        # and whether it pulses or jumps, as MU's ItemObjectAttribute makes it --
        # BlendMeshLight = sin(WorldTime*0.004)*a + b, and a per-frame BlendMeshTexCoord jump in
        # steps of `jitter`. Said by export_gltf.glow_material in the material's extras. Three
        # floats follow: a, b (0, 1 for no pulse), jitter (0 for none).
        said = material.get("extras") or {}
        pulse = said.get("pulse") or [0.0, 1.0]
        jitter = float(said.get("jitter") or 0.0)
        if flags & 2 and (said.get("item") or said.get("pulse") or jitter):
            flags |= 32
        # Bit 6: a glow that casts the sun's shadow all the same, at a strength (1 full) --
        # one float after bit 5's. Ours: the Ice Monster, whose additive body is seen on
        # Devias's snow by the shadow under it.
        shadow = float(said.get("shadow") or 0.0)
        if flags & 2 and shadow > 0.0:
            flags |= 64
        maps = {"albedo": "", "normal": "", "orm": "", "emissive": ""}
        # By the glb's own name: a `~whole` variant is a second cut of the same file, and the
        # manifest only knows the file. Keyed by the variant, the Elite came out untextured.
        source = model.split("~", 1)[0]
        for role in maps:
            maps[role] = textures.get(f"{source}#{image_for(document, material, role)}:{role}", "")
        materials += struct.pack("<fB", cutout, flags)
        materials += write_string(material.get("name", "material"))
        for role in ("albedo", "normal", "orm", "emissive"):
            materials += write_string(maps[role])
        materials += struct.pack("<2f", *orm_factors(material, maps["orm"]))
        if flags & 4:
            materials += struct.pack("<f", translucency)
        if flags & 16:
            materials += struct.pack("<f", float(rate))
        if flags & 32:
            materials += struct.pack("<3f", float(pulse[0]), float(pulse[1]), jitter)
        if flags & 64:
            materials += struct.pack("<f", shadow)
        if flags & 128:
            materials += struct.pack("<B", mode)
    # The fallback a primitive with no material of its own draws with, as content/mesh.cpp
    # appends it. Rough and not metal, for the reason orm_factors gives.
    materials += struct.pack("<fB", -1.0, 0) + write_string("none")
    for _ in range(4):
        materials += write_string("")
    materials += struct.pack("<2f", 1.0, 0.0)

    header = struct.pack("<4sIIIII", b"MU2M", 4 if bones else 3, vertex_count,
                         len(indices) // 4, len(parts), len(material_list) + 1)
    header += struct.pack("<3f3f", *low, *high)
    if bones:
        header += struct.pack("<I", len(bones))
    body = bytes(vertices) + bytes(indices)
    for part in parts:
        body += struct.pack("<III", *part)
    body += bytes(materials)
    for name, parent, inverse in bones:
        body += write_string(name) + struct.pack("<i16f", parent, *inverse)

    with open(out_path, "wb") as handle:
        handle.write(header + body)
    return len(indices) // 12, vertex_count, len(header) + len(body), len(bones)


def orm_factors(material, orm_path):
    """glTF's roughness and metal factors, which multiply the ORM rather than decorate it.

    Every material in this content that carries an ORM texture states both factors as 1.0, and
    195 that carry no ORM texture at all state the real number there instead -- foliage at
    0.90, water at 0.08, MU's grass and leaves and every surface whose material declares no
    grain, because MU2's tiled_maps writes no map for those and puts the answer in the factor.
    Read only the texture and all 195 arrive at the engine's missing-map fallback.

    One departure from the spec, deliberate. glTF's default for `metallicFactor` is 1.0, so a
    material with neither a texture nor a stated factor is a mirror by the letter of it. That
    is the sprint 4 bug back again from the other side -- a metal has no diffuse, so plate
    armour whose ORM did not load drew black -- and it is not what an unstated metal means in
    MU's painted content. Absent both, this writes metal 0.
    """
    pbr = material.get("pbrMetallicRoughness", {})
    roughness = float(pbr.get("roughnessFactor", 1.0))
    if "metallicFactor" in pbr:
        metal = float(pbr["metallicFactor"])
    else:
        metal = 1.0 if orm_path else 0.0
    return roughness, metal


def image_for(document, material, role):
    """Which image index a material uses in a role, or -1."""
    pbr = material.get("pbrMetallicRoughness", {})
    view = {"albedo": pbr.get("baseColorTexture"),
            "orm": pbr.get("metallicRoughnessTexture") or material.get("occlusionTexture"),
            "normal": material.get("normalTexture"),
            "emissive": material.get("emissiveTexture")}[role]
    if not view:
        return -1
    return document.get("textures", [])[view["index"]].get("source", -1)


def cook_world_clip(model, path, out_path):
    """A world object's own embedded animation, baked flat into one .muc -- or None.

    Unlike the player's and the monsters', a world object carries its clip in the SAME glb
    as its mesh: MU2's exporter writes it there because the object owns the clip and nothing
    else ever plays it. One clip only, in every asset the rig audit found (`actions_available`
    in the .rig.json notes), so `cook_clips` runs with no name table, no travel and nothing
    that holds -- it just bakes whatever `animations` the glb carries, closes the loop the
    same way a player clip's is, and returns.

    Twenty of MU2's world objects carry a rig this way -- trees, the street lamp's arm, the
    hanging inn sign, curtains, carriages -- MoveObject's per-type `o->Velocity`, traced in
    ZzzObject.cpp. Cooked for all of them here, generically, so the file exists whichever one
    a later pass plays; only the trees are wired into the renderer today (docs/sprints, and
    the render side is deliberately narrower than this).
    """
    document, binary = read_glb(path)
    if not document.get("animations"):
        return None
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    clips, frames, size, _closed, _spread = cook_clips(document, binary, out_path, {}, {}, set())
    return clips, frames, size


def cook_meshes(world, out_dir):
    """Every model the world places, into .mum beside the manifest the textures wrote."""
    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)
    manifest_path = os.path.join(out_dir, "textures.json")
    if not os.path.exists(manifest_path):
        print("cook: the textures have not been cooked, and a mesh names them", file=sys.stderr)
        return 2
    with open(manifest_path) as handle:
        textures = json.load(handle)["textures"]

    # The one thing about a model this file cannot read off its own glb: which asset it was
    # built from, and whether that asset named a scroll rate for the model's glow. See
    # cook_placements just below, which reads the same table for the same reason.
    with open(os.path.join(ASSETS, "index.json")) as handle:
        listed = json.load(handle).get("objects", [])
    carried = {one["name"]: one for one in listed if one.get("world") in (world, None)}

    mesh_dir = os.path.join(out_dir, "meshes")
    clip_dir = os.path.join(out_dir, "clips")
    os.makedirs(mesh_dir, exist_ok=True)

    clips_manifest = {}
    # The models whose clip MU never runs -- `o->Velocity = 0.f`, the treasure chest. Their
    # clip is still cooked, because MU draws them on its first key and not in the bind pose;
    # the game holds them there. See index.py's `still` and game/world/sway.cpp.
    still = []
    triangles = vertices = cooked = source = models = 0
    # The boid among them, though nothing places it: its .mum and its .muc land beside the
    # town's and the engine opens them by name. It is NOT in the .mut -- Sway walks the town's
    # placements and finds none of it, which is what leaves the pool to game/world/boids.cpp.
    for model in sorted({one["model"] for one in map_data["objects"]} |
                        set(filter(None, [flying(world), crawling(world)])) | set(TRAP_MODELS.get(world, ()))):
        path = boid_glb(world, model)
        if not os.path.exists(path):
            continue
        scroll = 0.0
        for one in carried.get(model, {}).get("glow", {}).values():
            scroll = float(one.get("scrolls_per_second", 0.0))
            break
        tris, verts, size, _bones = cook_mesh(model, path,
                                              os.path.join(mesh_dir, model + ".mum"), textures,
                                              scroll_per_second=scroll,
                                              scrolls=carried.get(model, {}).get("glow"))
        triangles += tris
        vertices += verts
        cooked += size
        source += os.path.getsize(path)
        models += 1
        clip_path = os.path.join(clip_dir, model + ".muc")
        if cook_world_clip(model, path, clip_path) is not None:
            clips_manifest[model] = os.path.relpath(clip_path, ASSETS)
            if carried.get(model, {}).get("still"):
                still.append(model)
    with open(os.path.join(out_dir, "clips.json"), "w") as handle:
        json.dump({"version": 1, "clips": clips_manifest, "still": sorted(still)}, handle,
                  indent=1, sort_keys=True)
    print(f"cook: {models} meshes, {triangles} triangles, {vertices} vertices, "
          f"{cooked / 1e6:.1f} MB of .mum out of {source / 1e6:.1f} MB of .glb, "
          f"{len(clips_manifest)} with their own clip")
    return 0


def clip_slot(name):
    """`action15` is MU's own action number 15, and nothing else here is a clip name."""
    if name and name.startswith("action") and name[len("action"):].isdigit():
        return int(name[len("action"):])
    return -1


def resample_track(keys, values, times, rotation):
    """One channel's LINEAR track, read at `times` -- slerp for a quaternion, lerp otherwise."""
    out = []
    span = 0
    for (at,) in times:
        while span + 2 < len(keys) and keys[span + 1][0] <= at:
            span += 1
        t0, t1 = keys[span][0], keys[min(span + 1, len(keys) - 1)][0]
        f = 0.0 if t1 <= t0 else min(max((at - t0) / (t1 - t0), 0.0), 1.0)
        a, b = values[span], values[min(span + 1, len(values) - 1)]
        if not rotation:
            out.append(tuple(x + (y - x) * f for x, y in zip(a, b)))
            continue
        dot = sum(x * y for x, y in zip(a, b))
        if dot < 0.0:
            b, dot = tuple(-y for y in b), -dot
        if dot > 0.9995:
            q = tuple(x + (y - x) * f for x, y in zip(a, b))
        else:
            theta = math.acos(min(dot, 1.0))
            sa, sb = math.sin((1.0 - f) * theta), math.sin(f * theta)
            q = tuple((x * sa + y * sb) / math.sin(theta) for x, y in zip(a, b))
        length = math.sqrt(sum(x * x for x in q)) or 1.0
        out.append(tuple(x / length for x in q))
    return out


def cook_clips(document, binary, out_path, names, travel, holds, cloth=False):
    """Every animation in a .glb, baked flat, into one .muc. Returns (clips, frames, bytes).

    Flat means: per clip, a frame count and a duration, then `frames x bones` of local
    rotation and translation in the skin's own joint order. No key times, no samplers, no
    accessor indirection -- which is the reason to bake, rather than the bytes, since the
    283-clip player library is 5 MB either way.

    Nothing is resampled. Every channel of every clip in this content shares one key-time
    list, LINEAR throughout, and MU's own rate is `action_speeds x 25` -- never above 25 Hz.
    Resampling the library to a fixed 25 Hz would treble it and add no information.

    **A looping clip must close, and 35 of the player's 283 do not.** MU writes most looping
    clips with one extra key holding the first pose again, so the wrap has an interval to
    happen over, and that key is KEPT here: playing wraps the clock in [0, duration), which
    makes the wrap an interpolation rather than a repeat. But the locomotion set -- every
    Walk, every Run, Fly, Walk swim, Run ride, and every monster's `action2` -- was written
    WITHOUT it: its last key is a distinct pose and MU's own player wraps from it to the
    first. Left alone, such a clip snaps once a cycle, and worse, its `duration` is one
    interval short of the true cycle: six intervals where the walk has seven. `action_travel`
    is metres per CYCLE, so matching playback to move speed off that duration slides the feet
    by a sixth, which is the number sprint 5 spends.

    So the cook closes them: a looping clip whose last frame differs from its first gets the
    first frame appended and its duration extended by one interval. Every looping clip in a
    `.muc` closes, and the runtime stays as dumb as it was. A `hold` clip is left alone --
    it stops on its last frame and never wraps, so it has nothing to close.

    `holds` is the set of slots that stop on their last frame: MU's `monster_holds`, which is
    the death, and for the player library the two deaths `actions` names ("Die 1", "Die 2"),
    which no monster table covers.
    """
    nodes = document.get("nodes", [])
    joints = document["skins"][0]["joints"]
    place = {node: index for index, node in enumerate(joints)}
    rest = []
    for node in joints:
        rest.append((tuple(nodes[node].get("rotation", (0.0, 0.0, 0.0, 1.0))),
                     tuple(nodes[node].get("translation", (0.0, 0.0, 0.0)))))

    cache = {}

    def accessor(index):
        if index not in cache:
            cache[index] = read_accessor(document, binary, index)
        return cache[index]

    clips = []
    frames_total = 0
    closed = 0
    spread = 0
    body = bytearray()
    for animation in document.get("animations", []):
        name = animation.get("name", "")
        slot = clip_slot(name)
        rotation = {}
        translation = {}
        # The densest key list is the clip's; a channel on another list is resampled onto it.
        # Carriage01 is the case: four of its channels (Bone02, Bone06, Bone08) carry 21 keys
        # over the same 5.25 s the other 24 carry 22 in -- the span agrees, the keys do not.
        # Linear between the channel's own keys, which is what glTF's LINEAR means, so
        # nothing is invented. A clip whose channels already agree is baked as it was.
        lists = [accessor(animation["samplers"][c["sampler"]]["input"])
                 for c in animation["channels"]]
        times = max(lists, key=len) if lists else None
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            if sampler.get("interpolation", "LINEAR") != "LINEAR":
                raise ValueError(f"{name}: {sampler['interpolation']} interpolation, and this "
                                 f"cook bakes LINEAR keys as they stand")
            keys = accessor(sampler["input"])
            if abs(times[-1][0] - keys[-1][0]) > 1e-6 or abs(times[0][0] - keys[0][0]) > 1e-6:
                raise ValueError(f"{name}: its channels do not span one time, so a flat bake "
                                 f"would have to invent keys past a channel's own end")
            target = channel["target"]
            index = place.get(target["node"])
            if index is None:
                continue
            values = accessor(sampler["output"])
            if len(keys) != len(times) or any(abs(a[0] - b[0]) > 1e-6
                                               for a, b in zip(keys, times)):
                values = resample_track(keys, values, times, target["path"] == "rotation")
            if target["path"] == "rotation":
                rotation[index] = values
            elif target["path"] == "translation":
                translation[index] = values
            else:
                raise ValueError(f"{name}: a {target['path']} channel, and this content has "
                                 f"only rotation and translation")
        if not times:
            continue

        frames = len(times)
        duration = float(times[-1][0] - times[0][0])
        hold = 1 if slot in holds else 0

        def pose_of(frame):
            out = []
            for bone in range(len(joints)):
                r = rotation[bone][frame] if bone in rotation else rest[bone][0]
                t = translation[bone][frame] if bone in translation else rest[bone][1]
                out.append((tuple(r[:4]), tuple(t[:3])))
            return out

        poses = [pose_of(frame) for frame in range(len(times))]
        # Every quaternion on the same side as the frame before it, so that a difference taken
        # across the cycle below is a small rotation rather than a trip the long way round.
        for bone in range(len(joints)):
            for frame in range(1, len(poses)):
                was, now = poses[frame - 1][bone][0], poses[frame][bone][0]
                if sum(a * b for a, b in zip(was, now)) < 0.0:
                    poses[frame][bone] = (tuple(-v for v in now), poses[frame][bone][1])

        first, last = poses[0], poses[-1]
        gaps = [max(max(abs(a - b) for a, b in zip(ra, rb)),
                    max(abs(a - b) for a, b in zip(ta, tb)))
                for (ra, ta), (rb, tb) in zip(first, last)]
        gap = max(gaps) if gaps else 0.0
        # Closed to a ten-thousandth: MU's exporter writes the repeat exactly, so this is a
        # test of whether the key is there at all rather than a tolerance on how close it is.
        closes = gap < 1e-4

        # **Whether the BODY closes, which is not the same question as whether every bone
        # does.** Asked of the whole rig, the test above failed 26 of the player's clips --
        # every walk, every run -- on bones that do not carry the figure: the cloth
        # (`Bone02/03/06/07`, the cape and the skirts), `Bip01 Footsteps` (a marker pinned to
        # the ground) and, twice, the head by about four degrees. The legs and the spine of
        # those clips already close: MU wrote the repeat of the first pose as their last key.
        # So appending ANOTHER copy of the first pose gave every walk a seventh interval from
        # the first pose to itself -- 133 ms of every 0.93 s cycle in which the legs held still
        # under a body still gliding forwards. That is what "the legs are a bit slow and the
        # walk is not right" was, and `tools/stride.py` shows it as a key step of 0.000 m.
        #
        # MuMain plays it the same way (BMD::PlayAnimation wraps key 6 into key 0 and blends
        # across it), so the freeze is MU's own and closing it is OURS: asked for on
        # 2026-09-21 on the measured seam, under the rule that a visible MU pop may be closed
        # as a marked departure. See docs/sprints/06-the-showing.md.
        #
        # For a clip whose body closes, the little drift left in the secondary bones is spread
        # across the cycle instead -- each key shifted by its share of the gap -- so the last
        # key becomes the first exactly, with no interval spent on nothing and no pop at the
        # wrap. The cape's 0.036 is six-hundredths of a degree a key. A clip that is genuinely
        # open (its limbs off by up to 1.9, nine of them) still gets the closing key appended,
        # because it really does need an interval to travel back over.
        bone_names = [nodes[node].get("name", "") for node in joints]
        # Only on MU's Bip01 skeleton, where `BoneNN` really is the cloth hung off it. A monster
        # rig names EVERY bone `BoneNN` -- the Spider's legs are Bone01 upwards -- so the same
        # rule there counts the whole animal as cloth, finds its body gap to be nothing, and
        # spreads a genuinely open walk: the first draft of this did exactly that to the
        # Spider's (0.388) and the Budge Dragon's (0.381), which would have bent their stride
        # to fit a loop MU never closed. On a rig with no Bip01 every bone is the body.
        # Said by the CALLER, for the player's library alone, and not guessed from the rig.
        # Two guesses were tried and both were wrong: a monster names every bone `BoneNN`, so
        # "BoneNN is cloth" called a whole Spider cloth; and "has a Bip01 skeleton" let in the
        # Budge Dragon, whose Bip01 legs close and whose BoneNN WINGS are off by 0.381 -- a
        # real 45-degree swing that spreading would have bent into its wingbeat. On every
        # other rig all bones are the body, so only noise-level drift is spread there (the
        # Giant's, Hound's and Lich's walks, off by 0.001 to 0.011, which the appended key had
        # been freezing too) and anything larger keeps the key.
        def secondary(name):
            if not cloth:
                return False
            return (name.startswith("Bone") and name[4:].isdigit()) or name == "Bip01 Footsteps"

        body_gap = max((g for g, n in zip(gaps, bone_names) if not secondary(n)), default=0.0)
        # Anything else that is off by a lot -- a prop swinging through the clip, which is
        # what `Mesh01` off by 2.4 in action77 is -- is real movement and not drift.
        prop_gap = max((g for g, n in zip(gaps, bone_names) if n.startswith(("Mesh", "gfh"))),
                       default=0.0)
        # And only a modest drift, anywhere. Four player clips close in the body and are off
        # by 1.8 to 2.0 on a cloth bone, which is not drift -- it is a quaternion the far side
        # of the sphere or a swing the clip really makes -- and sharing THAT across the keys
        # would wrench the cloth through every frame. They keep the appended key, as before.
        #
        # And only in the player's library, for now. The monsters have the same seam -- the Bull
        # Fighter's feet close and its walk has the same 0.000 m key step -- but what was asked
        # for on 2026-09-21 was the character's walk, and a monster's walk is left exactly as it
        # was validated until it is asked for too. `cloth` is the caller saying which library
        # this is.
        # **And the same seam on every other rig, asked for on 2026-09-23**: "there is still a
        # little hiccup on monster walks". Measured on the cooked cycle, every biped monster's
        # walk had a last interval of exactly 0.000 m of foot travel -- 118 ms of a Bull
        # Fighter's 824 ms stride in which the legs hold still under a body gliding on at 2.5
        # m/s, once per stride, forever. It is the character's bug of 2026-09-21, unfixed
        # everywhere the character is not.
        #
        # The player's rule cannot simply be turned on for them, and the two paragraphs above
        # say why: naming the secondary bones takes a table this cook does not have, and both
        # guesses tried were wrong on some rig. So a monster's clip is asked a different and
        # more basic question -- **is the last key the animator's duplicate of the first?**
        # MU's convention is binary and leaves a binary trace: where the animator wrote the
        # repeat, the bones come back BIT-IDENTICAL. The Bull Fighter's walk closes to 0.0
        # exactly on 41 of its 45 bones (the neck, the head and two clavicles drift, which is
        # sloppiness in the source and is what gets spread); the Budge Dragon's closes on every
        # bone it has but `Bip01 Footsteps`, the marker pinned to the ground. The Spider's
        # closes on NOTHING -- every one of its eight legs is off by 0.37 to 0.39 -- so its
        # walk really is open, really does need an interval to travel back over, and keeps the
        # appended key. That is the line the magnitude thresholds were groping for, and it is
        # drawn by the data rather than by a number picked to suit six animals.
        #
        # The old comment here said the Budge Dragon's BoneNN WINGS were off by 0.381. They are
        # not: its wings close exactly and the 0.381 is `Bip01 Footsteps`. Measured 2026-09-23.
        exact = sum(1 for g in gaps if g < 1e-4)
        repeat = bool(gaps) and exact * 4 >= len(gaps) * 3
        spreads = (not hold and not closes and len(poses) > 1 and prop_gap < 0.5 and gap < 0.5
                   and ((cloth and body_gap < 0.05) or (not cloth and repeat)))

        if spreads:
            span = float(len(poses) - 1)
            for bone in range(len(joints)):
                (ra, ta), (rb, tb) = first[bone], last[bone]
                dr = [b - a for a, b in zip(ra, rb)]
                dt = [b - a for a, b in zip(ta, tb)]
                for frame in range(1, len(poses)):
                    share = frame / span
                    r, t = poses[frame][bone]
                    r = [v - d * share for v, d in zip(r, dr)]
                    norm = math.sqrt(sum(v * v for v in r)) or 1.0
                    r = tuple(v / norm for v in r)
                    t = tuple(v - d * share for v, d in zip(t, dt))
                    poses[frame][bone] = (r, t)
            spread += 1
        elif not hold and not closes and frames > 1:
            interval = duration / float(frames - 1)
            duration += interval
            frames += 1
            closed += 1

        # Frame-major: one frame's bones are contiguous, because what reads this walks two
        # whole frames and blends them. The appended frame is the first one again.
        for frame in range(frames):
            for bone, (r, t) in enumerate(poses[frame if frame < len(poses) else 0]):
                body += struct.pack("<4f3f", *r, *t)
        clips.append((name, names.get(str(slot), names.get(slot, name)), slot, frames, duration,
                      hold, float(travel.get(str(slot), travel.get(slot, 0.0)))))
        frames_total += frames

    header = struct.pack("<4sIII", b"MU2C", 1, len(clips), len(joints))
    table = bytearray()
    for node in joints:
        table += write_string(nodes[node].get("name", "bone"))
    for name, label, slot, frames, duration, hold, travel_metres in clips:
        table += write_string(name) + write_string(label)
        table += struct.pack("<iIffI", slot, frames, duration, travel_metres, hold)

    with open(out_path, "wb") as handle:
        handle.write(header + bytes(table) + bytes(body))
    return len(clips), frames_total, len(header) + len(table) + len(body), closed, spread


def read_png(path):
    """A PNG as (width, height, channels, bytes). Enough for MU's grids, and no more."""
    raw = open(path, "rb").read()
    at = 8
    data = b""
    width = height = depth = colour = 0
    while at < len(raw):
        length, kind = struct.unpack(">I4s", raw[at:at + 8])
        chunk = raw[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            data += chunk
        elif kind == b"IEND":
            break
    if depth != 8:
        raise ValueError(f"{path} is {depth} bits a channel, and this reader does 8")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    stride = width * channels
    out = bytearray(width * height * channels)
    previous = bytearray(stride)
    source = zlib.decompress(data)
    at = 0
    for y in range(height):
        filter_kind = source[at]
        at += 1
        line = bytearray(source[at:at + stride])
        at += stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            upleft = previous[i - channels] if i >= channels else 0
            if filter_kind == 1:
                line[i] = (line[i] + left) & 255
            elif filter_kind == 2:
                line[i] = (line[i] + up) & 255
            elif filter_kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif filter_kind == 4:
                pa, pb, pc = abs(up - upleft), abs(left - upleft), abs(left + up - 2 * upleft)
                nearest = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upleft)
                line[i] = (line[i] + nearest) & 255
        out[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, bytes(out)


# Laid on the terrain instead of at the height MU stores for them -- MuMain itself never does
# this (OpenObjectsEnc). Types 20 to 27 are Lorencia's grass and fern block, and this is MU2's
# own deviation (`World.cs:3672`), not our arithmetic being corrected -- re-measured on this
# copy of the placement list before it was copied here: of 999 grass placements, 57% carry a
# stored pitch or roll, and the stored height runs from 6.60 m *below* the terrain to 4.29 m
# above it, with Grass01's median a clear metre over the ground. Reproduced faithfully that
# is a town with tufts of grass at head height. Height comes from the terrain, pitch and roll
# are dropped, yaw is kept: a tuft has a direction it faces and no business leaning. A
# placement placements.json marks `as_stored` is the exception, and stands as MU stores it.
#
# Per world, because a type number is a different object in each: Devias's 20 to 27 are its
# doors, benches and desks, which stand on raised floors and would sink into the terrain.
# Noria's stays as it has cooked since its objects stood, until somebody judges it.
#: How many tiles round a placement its baked light is the median of, by world: 1 is the 3x3
#: every world takes, wider where the painted light jumps between neighbours. See `lit`.
#: Worlds whose ground is one flat sheet over a black void, where a swaying placement that
#: reaches below it is hanging into the dark, not buried. See the buried test in the placements.
FLAT_OVER_VOID = {"dungeon", "losttower"}

LIGHT_REACH_BY_WORLD = {"losttower": 5}

#: How much of MU's baked light's variation a world's objects keep, about the mean of its ground's
#: light, as ground.py's LIGHT_DEPTH does for the ground. Ours. The Lost Tower took MU's painted
#: light whole and its shade was nearly all baked; the user: 'we ened more actual shadows and
#: little bit less baked in'. 1 (all of it) everywhere else.
OBJECT_LIGHT_DEPTH_BY_WORLD = {"losttower": 0.6}

GROUNDED_TYPES = {"lorencia": range(20, 28), "noria": range(20, 28)}

# OURS, not MU's: three rocks by the river west of Lorencia stand in the air in MU's own
# data. The heightmap is MuMain's TerrainHeight.OZB byte for byte, heights are bytes x 1.5 in
# both, and MuMain places an object at its stored z with no terrain snap (OpenObjectsEnc) --
# so MU floats them too, most likely a riverbed lowered after the rocks were placed. Each is
# lowered, angle kept, until its lowest vertex under MU's (Z * Y) * X rotation sits 15 cm
# into the ground, which is shallower than the metre its grounded neighbours are sunk.
# Measured 2026-09-24: lowest vertex 1.38 m, 1.40 m and 0.67 m over the bed. Keyed by
# model and MU's stored (x, y); metres to lower by.
LOWERED = {("Stone04", 3076.018, 14026.856): 1.53,
           ("Stone03", 2708.711, 13432.253): 1.55,
           ("Stone01", 2430.678, 13666.117): 0.82}

# MU's hidden anchors in Lorencia: MoveObject's WD_0LORENCIA calls CreateFire(0|1|2, o, 0,0,0)
# on MODEL_LIGHT01..03 and hides the holder (ZzzObject.cpp). Kind 1 is a fire, 4 a smoke.
ANCHOR_KINDS = {"Light01": 1, "Light02": 4, "Light03": 4}
# And world 74's, the character screen's, which are placements with no mesh rather than hidden
# ones: GMEmpireGuardian4 hides types 79 and 132 (HiddenMesh = -2) and hangs a fire off 79 --
# a light sprite and flame particles, RenderObjectVisual case 79 -- and smoke off 132 (case 132).
# Type 129's blue cloud is not carried: the user keeps that sky black.
# The Lost Tower's 95 Flame vents (Object25, hidden): MoveObject's WD_4LOSTTOWER arm lights
# the wizard's Flame on one a frame in 64 (ZzzObject.cpp:4020-4024). Kind 5, a vent: Lamps
# rolls it and Play burns it (game/world/lamps.h).
ANCHOR_KINDS_BY_WORLD = {"charscene": {"Object80": 1, "Object133": 4},
                         "losttower": {"Object25": 5}}
BRAZIER_BOWLS = {"charscene": ("Object15",)}
# World 74's meshes MU never draws: GMEmpireGuardian4::MoveObject sets HiddenMesh = -2 on types
# 79 to 86 and 129 to 132 (models Object80.. and Object130..133). Type 129's cloud anchor is
# Object130, whose green `angeflo_r` glow slabs stood on the wall tops until this was read.
HIDDEN_BY_WORLD = {"charscene": {"Object80", "Object81", "Object83", "Object84", "Object85",
                                 "Object86", "Object87", "Object130", "Object131", "Object132",
                                 "Object133"}}
# What each throws: CreateFire(0)'s own (ZzzEffectFireLeave.cpp:61) -- L = rand[0.6, 1.1),
# colour (L, 0.6L, 0.4L), range 4 -- with index.json's flicker for every other fire. Smoke
# throws no light. Colour, low, high, reach, hz, smoothing.
ANCHOR_LIGHT = {1: ((1.0, 0.6, 0.4), 0.6, 1.1, 4.0, 3.0, 0.12),
                4: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                # A vent's light is the Flame's, (1, 0.4, 0) over three tiles (MoveHandlers.cpp:
                # 1815-1816), and dark until it burns: Lamps drives its level.
                5: ((1.0, 0.4, 0.0), 0.0, 0.0, 3.0, 0.0, 0.0)}


# Where the town's own stone stands in the lawn, for the grass to stay out of. The .walls
# format, little-endian:
#
#     'MU2W', u32 version (1), u32 size (tiles a side), u32 cells (a side, per tile), u32 tiles
#     tiles: u16 column, u16 row, u32 low, u32 high
#
# A tile is cut into WALL_CELLS by WALL_CELLS squares, and a set bit is a square some object's
# own surface passes through within the grass's height of the ground. Bit `v * cells + u`,
# where u runs along +x and v along +z from the tile's -z edge -- the same (u, v) grass.sh
# stands a card at -- with the first 18 bits in `low` and the rest in `high`. 18 because
# that is what a float carries exactly, and the two ride to the shader in the two floats the
# grass instance had spare. Only tiles with a bit set are written.
#
# From the meshes and not from the solids. The solids are one box a model and say where a
# character may not walk: Fence02, the corner of the flower beds, is a whole square metre of
# it, and a bed built of four is walled right across. Its own triangles are a rim 26 cm thick
# round an open middle, which is the shape the grass has to leave.
WALL_CELLS = 6
# The band an object has to cross to take the lawn from under it, in metres off the ground at
# that point. The bottom is a little under the ground, where MU sinks a wall's foot; the top
# is below the shortest card, so a branch overhead takes nothing and a wall that meets the
# ground takes what it stands on.
WALL_BAND = (-0.05, 0.35)
# MU's own grass models are what a lawn grows round, not through.
WALL_SKIPPED = ("Grass",)


def cook_walls(out_dir, world, models, buckets, heights, channels, grid_w, grid_h, lift):
    import numpy as np

    cells = WALL_CELLS
    size = grid_w
    grid = np.frombuffer(bytes(heights), np.uint8)[::channels].astype(np.float32)
    grid = grid.reshape(grid_h, grid_w) * lift

    # The land under a point, bilinear over the tile's corners as cook_placements' terrain()
    # and Ground::heightAt have it.
    def ground(x, z):
        column = np.clip(x, 0.0, grid_w - 1.001)
        row = np.clip(-z, 0.0, grid_h - 1.001)
        c0 = column.astype(np.int64)
        r0 = row.astype(np.int64)
        fc = column - c0
        fr = row - r0
        top = grid[r0, c0] * (1 - fc) + grid[r0, c0 + 1] * fc
        bottom = grid[r0 + 1, c0] * (1 - fc) + grid[r0 + 1, c0 + 1] * fc
        return top * (1 - fr) + bottom * fr

    # content/placement.cpp's transform, for a row vector: pitch, then roll, then yaw.
    def turned(pitch, yaw, roll):
        s, c = math.sin(pitch), math.cos(pitch)
        about_x = np.array([[1, 0, 0], [0, c, s], [0, -s, c]])
        s, c = math.sin(roll), math.cos(roll)
        about_z = np.array([[c, s, 0], [-s, c, 0], [0, 0, 1]])
        s, c = math.sin(yaw), math.cos(yaw)
        about_y = np.array([[c, 0, -s], [0, 1, 0], [s, 0, c]])
        return about_x @ about_z @ about_y

    meshes = {}

    def mesh(path):
        if path not in meshes:
            with open(os.path.join(ASSETS, path), "rb") as handle:
                raw = handle.read()
            _magic, version, vertices, indices, _parts, _materials = struct.unpack_from(
                "<4s5I", raw, 0)
            stride, start = (48, 48) if version == 3 else (56, 52)
            rows = np.frombuffer(raw, np.uint8, vertices * stride, start).reshape(vertices, stride)
            points = rows[:, :12].copy().view(np.float32).reshape(vertices, 3)
            triangles = np.frombuffer(raw, np.uint32, indices, start + vertices * stride)
            meshes[path] = (points.astype(np.float64), triangles.reshape(-1, 3))
        return meshes[path]

    low, high = WALL_BAND
    walled = np.zeros((size, size, cells * cells), bool)
    crossing = 0
    for entries in buckets.values():
        for model, x, y, z, yaw, pitch, roll, scale, _colour, _flags in entries:
            name, mesh_path = models[model][0], models[model][1]
            if name.startswith(WALL_SKIPPED):
                continue
            points, triangles = mesh(mesh_path)
            placed = (points @ turned(pitch, yaw, roll)) * scale + (x, y, z)
            corners = placed[triangles]
            above = corners[:, :, 1] - ground(corners[:, :, 0], corners[:, :, 2])
            keep = (above.min(1) < high) & (above.max(1) > low)
            corners, above = corners[keep], above[keep]
            if not len(corners):
                continue
            crossing += len(corners)

            # Each triangle is walked on a barycentric lattice fine enough that no cell can be
            # stepped over -- half a cell at the longest edge -- and every lattice point inside
            # the band marks the cell it falls in. Grouped by lattice size so a model is a few
            # array operations rather than a loop over its triangles.
            longest = np.max(np.linalg.norm(corners - np.roll(corners, 1, axis=1), axis=2), axis=1)
            steps = np.clip(np.ceil(longest * cells * 2.0), 1, 400).astype(np.int64)
            for step in np.unique(steps):
                chosen = steps == step
                i, j = np.meshgrid(np.arange(step + 1), np.arange(step + 1))
                inside = (i + j) <= step
                a = (i[inside] / step)[None, :]
                b = (j[inside] / step)[None, :]
                one = corners[chosen]
                lifted = above[chosen]
                point = (one[:, None, 0] * (1 - a - b)[..., None] + one[:, None, 1] * a[..., None]
                         + one[:, None, 2] * b[..., None])
                off = lifted[:, None, 0] * (1 - a - b) + lifted[:, None, 1] * a + lifted[:, None, 2] * b
                point = point[(off > low) & (off < high)]
                column = np.floor(point[:, 0]).astype(np.int64)
                row = np.floor(-point[:, 2]).astype(np.int64)
                u = np.clip(np.floor((point[:, 0] - column) * cells), 0, cells - 1).astype(np.int64)
                v = np.clip(np.floor((row + 1 + point[:, 2]) * cells), 0, cells - 1).astype(np.int64)
                on = (column >= 0) & (column < size) & (row >= 0) & (row < size)
                walled[row[on], column[on], (v * cells + u)[on]] = True

    split = 18
    weights = (1 << np.arange(cells * cells, dtype=np.uint64)).astype(np.uint64)
    tiles = np.argwhere(walled.any(axis=2))
    body = bytearray()
    for row, column in tiles:
        bits = int((walled[row, column].astype(np.uint64) * weights).sum())
        body += struct.pack("<2H2I", column, row, bits & ((1 << split) - 1), bits >> split)
    header = struct.pack("<4s4I", b"MU2W", 1, size, cells, len(tiles))
    out_path = os.path.join(out_dir, f"{world}.walls")
    with open(out_path, "wb") as handle:
        handle.write(header + bytes(body))
    print(f"cook: {len(tiles)} tiles walled against the grass, {int(walled.sum())} cells of "
          f"{cells}x{cells}, from {crossing} triangles crossing the ground, "
          f"{(len(header) + len(body)) / 1000:.0f} kB")


def cook_placements(world, out_dir, chunk_tiles):
    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)

    size = int(map_data["size"])
    per_tile = float(map_data["units_per_tile"])
    height_factor = float(map_data["height_factor"])
    metres_per_tile = 1.0                      # docs/conventions.md: one tile is one metre
    mesh_dir = os.path.join(out_dir, "meshes")

    # The models that have a mesh, and the bounds the cook already wrote into each .mum.
    models = []
    index_of = {}
    for name in sorted({one["model"] for one in map_data["objects"]}):
        path = os.path.join(mesh_dir, name + ".mum")
        if not os.path.exists(path):
            continue
        with open(path, "rb") as handle:
            header = handle.read(48)
        if header[:4] != b"MU2M":
            raise ValueError(f"{path} is not a .mum")
        bounds = struct.unpack_from("<6f", header, 24)
        index_of[name] = len(models)
        models.append([name, os.path.relpath(path, ASSETS), bounds, 0])

    # The roofs: the models MU gets out of the way when the hero is indoors. MU keeps a list
    # of types per world (IndoorFadeTypes); MU2's asset carries it as a flag, and so does the
    # placement here, so the game needs no table of names.
    with open(os.path.join(ASSETS, "index.json")) as handle:
        listed = json.load(handle).get("objects", [])
    roofs = {one["name"] for one in listed if one.get("roof_fade")}
    # The models that sway, which the mesh cook wrote down just before this runs: only
    # those can be held still for being buried. See `flags |= 4` below.
    try:
        with open(os.path.join(out_dir, "clips.json")) as handle:
            swaying = set(json.load(handle).get("clips", {}))
    except (OSError, ValueError):
        swaying = set()
    # The lights and glows each model carries. `world` is checked because index.json has one
    # Object10 in Noria and could have a same-named model in two maps.
    carried = {one["name"]: one for one in listed if one.get("world") in (world, None)}

    grid_w, grid_h, _channels, heights = read_png(os.path.join(world_dir, map_data["height"]))
    light_w, light_h, light_channels, light = read_png(
        os.path.join(world_dir, map_data["light"]))
    if grid_w != size or light_w != size:
        raise ValueError(f"{world}: a grid is {grid_w} wide and the world says {size}")

    # Bilinear over the tile's four corners, and it has to be, because that is what the
    # ground the tuft stands on IS: `Ground::heightAt` interpolates the same four, and the
    # terrain mesh is drawn from them. Read at the nearest corner -- which is what this did
    # first -- a placement anywhere but on a grid line is stood at a height the ground does
    # not have under it, and the steeper the tile the further out it is. Measured over
    # Lorencia's 999 grounded placements: 107 floated and 97 sank by more than 5 cm, 28 were
    # over a quarter-metre out, and a `Grass01` on the river bank stood 1.08 m in the air.
    # The half-tile offset is deliberate and MU's: a placement's (column, row) is measured
    # from the same grid origin as the heightmap, so tile (c, r)'s corner is sample (c, r).
    def terrain(column, row):
        c0 = min(max(int(column), 0), grid_w - 2)
        r0 = min(max(int(row), 0), grid_h - 2)
        fc = min(max(column - c0, 0.0), 1.0)
        fr = min(max(row - r0, 0.0), 1.0)
        at = lambda c, r: heights[r * grid_w + c] * height_factor / per_tile
        top = at(c0, r0) * (1.0 - fc) + at(c0 + 1, r0) * fc
        bottom = at(c0, r0 + 1) * (1.0 - fc) + at(c0 + 1, r0 + 1) * fc
        return top * (1.0 - fr) + bottom * fr

    def texel(x, y):
        x = min(max(x, 0), light_w - 1)
        y = min(max(y, 0), light_h - 1)
        at = (y * light_w + x) * light_channels
        if light_channels >= 3:
            return light[at], light[at + 1], light[at + 2]
        return light[at], light[at], light[at]

    # The median of the 3x3 tiles round the origin, per channel, rather than the one tile under
    # it. A placement is one light for its whole length, and MU's map has single near-black
    # texels where a wall meets the ground: the railing at Lorencia (122,109) stood on
    # (10,5,1) and its four metres of iron went black beside a neighbour on (102,94,91). A
    # median ignores one such texel and is the centre wherever the light is smooth.
    #
    # Wider in the Lost Tower, the user's 'some of walls has not mathcing colors to sibling
    # walls' (2026-10-01): its light is painted in pools and dark patches a few tiles across,
    # and on 3x3 two walls side by side came out up to 139 of 255 apart, one in a pool and its
    # neighbour beside it. 11x11 takes the floor's light rather than the tile's: neighbours at
    # most 62 apart, none over 40. Ours; MU lights each object at its own tile.
    reach = LIGHT_REACH_BY_WORLD.get(world, 1)
    #
    # And in the tower, without the void: MU never draws a NoGround tile and its light there is
    # a flat 142, so on floor 7's three-tile causeway the window was mostly void and every skull
    # on it came out lit bright to the edge, where MU's painted light falls to 0 and the
    # causeway blends into the dark (the user: 'we need that nice blending with void').
    no_ground = None
    if world in FLAT_OVER_VOID and world in LIGHT_REACH_BY_WORLD:
        aw, ah, ac, attrs = read_png(os.path.join(world_dir, map_data["attributes"]))
        no_ground = lambda x, y: (attrs[(min(max(y, 0), ah - 1) * aw + min(max(x, 0), aw - 1)) * ac] & 8) != 0

    def lit(column, row):
        x, y = int(column), int(row)
        cells = [(x + dx, y + dy) for dy in range(-reach, reach + 1) for dx in range(-reach, reach + 1)]
        if no_ground is not None:
            cells = [c for c in cells if not no_ground(*c)] or [(x, y)]
        around = [texel(cx, cy) for cx, cy in cells]
        middle = len(around) // 2
        return tuple(sorted(one[channel] for one in around)[middle] for channel in range(3))

    # The objects' share of the baked light's variation (OBJECT_LIGHT_DEPTH_BY_WORLD), about the
    # mean of the light over the ground the world draws.
    depth = OBJECT_LIGHT_DEPTH_BY_WORLD.get(world, 1.0)
    if depth != 1.0:
        drawn = [texel(cx, cy) for cy in range(light_h) for cx in range(light_w)
                 if no_ground is None or not no_ground(cx, cy)]
        mean = [sum(one[k] for one in drawn) / max(len(drawn), 1) for k in range(3)]

    def deep(rgb):
        if depth == 1.0:
            return rgb
        return tuple(int(round(min(max(mean[k] + (rgb[k] - mean[k]) * depth, 0), 255)))
                     for k in range(3))

    chunks_across = (size + chunk_tiles - 1) // chunk_tiles
    buckets = {}
    dropped_hidden = dropped_model = grounded = outside = roofed = lowered_count = buried = 0

    anchors = []
    world_anchors = ANCHOR_KINDS_BY_WORLD.get(world, {})
    # The braziers' bowls (Object15 on world 74), for a smoke anchor standing in one.
    bowls = [one["at"] for one in map_data["objects"]
             if one["model"] in BRAZIER_BOWLS.get(world, ())]
    for one in map_data["objects"]:
        if one["model"] in world_anchors:
            sx, sy, sz = one["at"]
            kind = world_anchors[one["model"]]
            anchors.append((kind, (sx / per_tile, sz / per_tile, -sy / per_tile)))
            # A smoke in a brazier's bowl burns as well -- ours: MU only smokes the brazier at
            # the row's left end while its three fellows burn, and the user asked for it lit.
            if kind == 4 and any(math.hypot(bx - sx, by - sy) < 0.5 * per_tile
                                 for bx, by, _bz in bowls):
                anchors.append((1, (sx / per_tile, sz / per_tile, -sy / per_tile)))
            dropped_hidden += 1
            continue
        if one["model"] in HIDDEN_BY_WORLD.get(world, ()):
            dropped_hidden += 1
            continue
        if one.get("hidden"):
            # MU's hidden anchors are still something: Light01 is a fire nobody sees the
            # holder of, Light02 and Light03 are chimney smoke (MoveObject, WD_0LORENCIA).
            kind = ANCHOR_KINDS.get(one["model"])
            if kind is not None:
                sx, sy, sz = one["at"]
                anchors.append((kind, (sx / per_tile, sz / per_tile, -sy / per_tile)))
            dropped_hidden += 1
            continue
        model = index_of.get(one["model"])
        if model is None:
            dropped_model += 1
            continue

        # MU stores z up and y south; ours is y up and row -z. docs/conventions.md.
        stored_x, stored_y, stored_z = one["at"]
        column = stored_x / per_tile
        row = stored_y / per_tile
        x = column * metres_per_tile
        z = -row * metres_per_tile
        y = stored_z / per_tile

        # MU's angles are degrees about its own z-up axes. Its z is our yaw and its x our
        # pitch, both at the same angle; its y is our roll NEGATED, because the axis swap is
        # a rotation about x by -90 degrees and it sends MU's +y to our -z. No placement in
        # Lorencia or Noria carries a y angle, so this line has never mattered and is
        # written correctly anyway -- it used to drop the angle on the floor.
        pitch, muRoll, yaw = (math.radians(a) for a in one["angle"])
        roll = -muRoll
        flags = 0
        # Unless placements.json names it as MU's own, to be stood where the map stores it:
        # the fountain's planting is on the stone and in the bowl, not on the terrain.
        if one["type"] in GROUNDED_TYPES.get(world, ()) and not one.get("as_stored"):
            y = terrain(column, row)
            pitch = 0.0
            roll = 0.0
            flags |= 1
            grounded += 1
        lowered = LOWERED.get((one["model"], round(stored_x, 3), round(stored_y, 3)))
        if lowered is not None:
            y -= lowered
            lowered_count += 1
        if one["model"] in roofs:
            flags |= 2
            roofed += 1
        # OURS, not MU's: a placement buried past 60% of its height is not the model any
        # more but the part that shows. MU makes a thicket out of four Tree01 crowns sunk
        # 5.5-6.6 m into the meadow south-west of the carriage camp (73,107), and a crown
        # at ground height swaying as a whole tree does reads as a bush on a spring. MU
        # plays their clip (MoveObject has no such test); here Sway holds them on their
        # first key, as it does the treasure chest. Nothing else that sways in Lorencia is
        # past half: two street lights come nearest, at 43% and 50%.
        bottom, top = models[model][2][1], models[model][2][4]
        # Not in the Dungeon or the Lost Tower: their ground is a flat sheet at 1.7 m over a
        # void, and the animated spines and hung skeletons that reach down into the dark read
        # as buried here while MU plays them (the tower's Object35-37: 72 of 111 caught).
        if world not in FLAT_OVER_VOID and one["model"] in swaying and not flags & 1 and top > bottom and \
                terrain(column, row) - (y + bottom * float(one.get("scale", 1.0))) > \
                0.6 * (top - bottom) * float(one.get("scale", 1.0)):
            flags |= 4
            buried += 1

        # Some of MU's placements stand off the edge of its own grid -- a ship moored past
        # the shore, a tree behind the sea wall. They are drawn where they are and belong to
        # the nearest chunk, which is a clamp; it is counted and reported rather than passed
        # over in silence, because a placement that has left the map is also the shape a
        # units-per-tile mistake would take.
        def chunk_of(tile):
            index = int(math.floor(tile)) // chunk_tiles
            return min(max(index, 0), chunks_across - 1)

        if not (0.0 <= column < size and 0.0 <= row < size):
            outside += 1
        chunk = (chunk_of(column), chunk_of(row))
        buckets.setdefault(chunk, []).append(
            (model, x, y, z, yaw, pitch, roll, float(one.get("scale", 1.0)),
             deep(lit(column, row)), flags))
        models[model][3] += 1

    # Sorted once: by chunk, then by model inside it, so a surviving chunk is a run of
    # instances and each model within it a contiguous sub-run. A frame appends a range.
    instances = bytearray()
    chunk_records = []
    written = 0
    for (cx, cy) in sorted(buckets):
        entries = sorted(buckets[(cx, cy)], key=lambda e: e[0])
        low = [1e30] * 3
        high = [-1e30] * 3
        first = written
        for model, x, y, z, yaw, pitch, roll, scale, colour, flags in entries:
            instances += struct.pack("<7f2H4B", x, y, z, yaw, pitch, roll, scale, model, flags,
                                     colour[0], colour[1], colour[2], 0)
            written += 1
            # The instance's own box: the model's bounds, scaled, around where it stands.
            # Rotation is not folded in; the radius of the scaled box is used instead, which
            # is never smaller than the rotated box and costs a chunk nothing to be generous
            # about at this count.
            bounds = models[model][2]
            reach = scale * max(abs(v) for v in bounds)
            for axis, centre in enumerate((x, y, z)):
                low[axis] = min(low[axis], centre - reach)
                high[axis] = max(high[axis], centre + reach)
        chunk_records.append((low, high, first, written - first, cx, cy))

    header = struct.pack("<4sIIIIIIf", b"MU2T", 2, len(models), len(chunk_records), written,
                         size, chunk_tiles, metres_per_tile)
    body = bytearray()
    for name, mesh_path, bounds, count in models:
        body += write_string(name) + write_string(mesh_path)
        body += struct.pack("<6fI", *bounds, count)
    for low, high, first, count, cx, cy in chunk_records:
        body += struct.pack("<6fII2H", *low, *high, first, count, cx, cy)
    body += bytes(instances)

    # The lights. Per model in its own frame, and the anchors in the world's.
    emitters = bytearray()
    emitter_count = 0
    kinds = {"lamp": 0, "fire": 1, "candle": 2, "window": 3, "smoke": 4, "vent": 5}

    def emitter(model, kind, at, colour, low, high, reach, hz, smooth):
        return struct.pack("<HBx3f3f5f", model, kind, *at, *colour, low, high, reach, hz, smooth)

    glows = bytearray()
    glow_count = 0
    for model_index, (name, _mesh, _bounds, count) in enumerate(models):
        entry = carried.get(name, {})
        for one in entry.get("emitters", []):
            # MU z-up, y south, in units; ours y-up, -z, in metres. docs/conventions.md.
            mx, my, mz = one.get("at", (0, 0, 0))
            high = float(one.get("high", 1.0))
            emitters += emitter(model_index, kinds.get(one.get("kind", "lamp"), 0),
                                (mx / per_tile, mz / per_tile, -my / per_tile),
                                one.get("colour", (1, 1, 1)), float(one.get("low", high)), high,
                                float(one.get("range_tiles", 3)), float(one.get("flicker_hz", 0)),
                                float(one.get("smooth_seconds", 0)))
            emitter_count += 1
        # One flicker per object: MU keeps a single BlendMeshLight. A glow entry that only
        # scrolls (House04, the waterspout) has no brightness to carry.
        for one in entry.get("glow", {}).values():
            if "low" in one and "high" in one:
                glows += struct.pack("<Hxx4f", model_index, float(one["low"]),
                                     float(one["high"]), float(one.get("hz", 0)),
                                     float(one.get("smooth_seconds", 0)))
                glow_count += 1
                break
    for kind, at in anchors:
        emitters += emitter(0xFFFF, kind, at, *ANCHOR_LIGHT[kind])
        emitter_count += 1
    body += struct.pack("<I", emitter_count) + bytes(emitters)
    body += struct.pack("<I", glow_count) + bytes(glows)

    out_path = os.path.join(out_dir, f"{world}.mut")
    with open(out_path, "wb") as handle:
        handle.write(header + bytes(body))

    cook_walls(out_dir, world, models, buckets, heights, _channels, grid_w, grid_h,
               height_factor / per_tile)

    print(f"cook: {written} placements in {len(chunk_records)} chunks of {chunk_tiles} tiles, "
          f"{len(models)} models, {grounded} laid on the terrain, {lowered_count} of {len(LOWERED)} "
          f"floating rocks lowered, {roofed} roofs, {buried} buried and held still, "
          f"{dropped_hidden} hidden and {dropped_model} without a mesh dropped, "
          f"{outside} standing off the grid, "
          f"{emitter_count} lights and smokes ({len(anchors)} of them hidden anchors), "
          f"{glow_count} flickering glows, "
          f"{(len(header) + len(body)) / 1000:.0f} kB")
    return 0


def figures_table(world):
    """The file a world's figures are listed in, under cooked/figures.

    One per world, since the meshes, clips and textures beside it are shared and the table
    is what differs: which breeds spawn, which townsfolk stand, where. `figures.json` stays
    Lorencia's name because every figure cooked before there was a second world is in it;
    any other world is `figures_<world>.json`. Before this, cooking Noria's figures would
    have written over Lorencia's table and taken its town away (2026-09-28).
    """
    return "figures.json" if world == "lorencia" else f"figures_{world}.json"


def figure_set(world):
    """What figures the world reaches, resolved out of index.json. No file is written.

    Returns (models, characters, monsters, standalone, placements) where `models` maps a
    mesh name onto its .glb. The rule is sprint 3's: the cook takes what is reached and
    nothing else. 106 item files are 148 MB; Lorencia's figures reach 21 of them.
    """
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)
    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)
    number = next(one["number"] for one in index["worlds"] if one["name"] == world)

    models = {}

    def reach(path):
        """A path as index.json writes it, onto the mesh name the cook will use."""
        if not path:
            return None
        name = os.path.splitext(os.path.basename(path))[0]
        full = os.path.join(ASSETS, path)
        if not os.path.exists(full):
            return None
        models[name] = full
        return name

    # Every weapon row in index.json carries the stance it is held in -- `Sword01` sword,
    # `Spear08` scythe, `CrossBow04` crossbow, `Axe07` two_hand_sword -- so which idle a
    # character stands in is read rather than guessed from a name. MU2's `Clips.cs` holds the
    # same table in the other direction.
    stance_of = {}
    for one in index.get("objects", []):
        glb = one.get("glb", "")
        if one.get("stance") and glb:
            stance_of[os.path.splitext(os.path.basename(glb))[0]] = one["stance"]

    characters = []
    placed = {one["model"] for one in map_data["objects"] if not one.get("hidden")}
    for one in index["characters"]:
        if one["name"] not in placed and not one.get("playable"):
            continue
        entry = {"name": one["name"], "label": one.get("label", one["name"]),
                 "parts": [reach(p) for p in one.get("parts", [])],
                 "right_hand": reach(one.get("right_hand")),
                 "left_hand": reach(one.get("left_hand")),
                 # The empty-hand stance is the one row of MU's table that asks who is
                 # standing in it: (1, 15) for a man and (2, 16) for a woman, and every
                 # armed row is shared because a crossbow is held one way whoever holds it.
                 "female": bool(one.get("female")),
                 # The hands decide the stance, and the right hand is the one that holds the
                 # weapon: a two-handed axe is not a sword held differently, it is actions 5
                 # and 18 where a sword is 4 and 17.
                 "stance": stance_of.get(
                     os.path.splitext(os.path.basename(one.get("right_hand") or ""))[0], ""),
                 "idle": one.get("idle", ""),
                 # The plus each piece is drawn at ({"parts": 7, "right_hand": 8}); the game
                 # turns it into a shine once it has the item table. Empty is +0.
                 "plus": one.get("plus", {}),
                 # On the back wherever he stands: the Golden Archer's crossbow.
                 "slung": bool(one.get("slung"))}
        entry["parts"] = [p for p in entry["parts"] if p]
        characters.append(entry)

    # And the cradle's own two weapons, which no character in index.json is drawn holding. They
    # go into `models` and so into the manifest's `items`, where the engine reads what each one
    # is and how it is slung; which hand they end up in is the game's business and not the
    # cook's. See CRADLE_ARMS.
    for one in index.get("objects", []):
        if one["name"] in CRADLE_ARMS:
            reach(one.get("glb"))

    monsters = []
    for one in index["monsters"]:
        if not any(s["map"] == number for s in one.get("spawns", [])):
            continue
        # Skeleton01's row is the only one whose `glb` is a list and whose model_index is -1:
        # it is shaped like a character because it IS one -- a 60-joint player rig with no
        # clips of its own, animated out of the player library. A parser written against the
        # other fifteen rows takes the wrong branch here and says nothing.
        glb = one["glb"]
        if isinstance(glb, list):
            glb = glb[0]
        mesh = reach(glb)
        if mesh is None:
            continue
        monsters.append({
            "name": one["name"], "label": one.get("label", one["name"]), "mesh": mesh,
            "scale": float(one.get("scale", 1.0)),
            "hidden_mesh": one.get("hidden_mesh"),
            "hidden_part": one.get("hidden_part"),
            "right_hand": reach(one.get("right_hand")),
            "right_hand_bone": one.get("right_hand_bone", ""),
            "left_hand": reach(one.get("left_hand")),
            "left_hand_bone": one.get("left_hand_bone", ""),
            # Skeleton01 is a monster on the player rig and its row says which stance it
            # stands in, because slot 0 of the player library is "Set" and not an idle at all.
            "stance": one.get("stance", ""),
            "action_keys": one.get("action_keys", {}),
            "action_travel": one.get("action_travel", {}),
            # How fast its glow sheet scrolls, as cook_meshes reads an object's: the Ice
            # Monster's BlendMeshTexCoordV. 0 for every other breed.
            "scroll": next((float(g.get("scrolls_per_second", 0.0))
                            for g in (one.get("glow") or {}).values()), 0.0),
            "spawns": [s for s in one.get("spawns", []) if s["map"] == number]})

    # The four standalone town figures -- Smith01, Wizard01 and two Storage01 -- are whole
    # models with their own clips inside them, not a set of parts on a shared rig.
    standalone = []
    named = {one["name"] for one in characters} | {one["name"] for one in monsters}
    for model in sorted(placed):
        if model in named or os.path.exists(os.path.join(world_dir, model, f"{model}.glb")):
            continue
        for where in ("assets/npc", "assets/monsters", "assets/lobby"):
            mesh = reach(f"{where}/{model}/{model}.glb")
            if mesh:
                standalone.append({"name": model, "mesh": mesh})
                break

    # The placements the town cook dropped for having no mesh: these fourteen.
    # The pets, on every map: a worn Guardian Angel or Imp is drawn with whoever wears it
    # (game/pets.h), wherever that is. Each is one model playing its own one clip.
    for one in index.get("objects", []):
        if one.get("kind") == "pet" and one["name"] not in named:
            mesh = reach(one.get("glb"))
            if mesh:
                standalone.append({"name": one["name"], "mesh": mesh})

    figures = {one["name"] for one in characters} | {one["name"] for one in standalone}
    per_tile = float(map_data["units_per_tile"])
    placements = []
    for one in map_data["objects"]:
        if one.get("hidden") or one["model"] not in figures:
            continue
        stored_x, stored_y, stored_z = one["at"]
        pitch, _unused, yaw = (math.radians(a) for a in one["angle"])
        # MU stores z up and y south, and one tile is one metre. docs/conventions.md.
        placements.append({"figure": one["model"],
                           "at": [stored_x / per_tile, stored_z / per_tile,
                                  -stored_y / per_tile],
                           "yaw": yaw, "pitch": pitch,
                           "scale": float(one.get("scale", 1.0))})
    return models, characters, monsters, standalone, placements, index


# The tick the delays below are converted at. It is written into the file and checked at
# load rather than assumed: every delay in the rows is in milliseconds, and a table cooked
# at one rate read by a sim running at another is a fight that is quietly the wrong speed.
# Realm.cs:49, `public const int Hz = 20`.
SIM_HZ = 20

# mu.db stores respawn_seconds as 10 for all sixteen breeds, and Version075 does not.
# Lorencia.cs:89 gives the Bull Fighter 3 s and Lorencia.cs:152 the Budge Dragon 3 s; the
# other six of Lorencia's breeds and all eight of Noria's are 10. The map files across
# Version075 hold six distinct delays (3, 8, 10, 15, 50, 150), so "10 everywhere" is a
# flattening in whatever wrote mu.db rather than a 0.75 fact, and it makes the two commonest
# low nests refill three times slower than MU's. Corrected here, by breed number, and every
# correction is printed.
RESPAWN_VERSION075 = {0: 3, 2: 3}


def ticks_of(milliseconds, what, name, remainders):
    """A delay in milliseconds as whole ticks, converted once, here.

    A delay re-derived per tick in floating point is how a seeded run stops reproducing, and
    a delay that does not divide is a rounding decision somebody has to make on purpose: all
    sixteen of today's rows divide exactly (400 ms is 8 ticks; 1400-2200 ms are 28-44), so a
    remainder means a new row has arrived and it is logged rather than swallowed.
    """
    whole, left = divmod(int(milliseconds) * SIM_HZ, 1000)
    if left:
        remainders.append(f"{name}.{what} {milliseconds} ms is {whole} ticks and {left}/1000 over")
    return max(1, whole)


# The town's people, by map: MU's own NPC number, the name, the tile, the facing (MU2's Look:
# 1 West ... 3 South ... 5 East ... 7 North, clockwise from West), and the cooked figure that
# stands for them. Transcribed from MU2's shared/Folk.cs `Townsfolk.Spawns`, which is OpenMU's
# Version075/Maps/Lorencia.cs and Noria.cs NPC spawns. Not in index.json or mu.db's
# npc_spawns in a form the cook can read today, so it is here, with its source, as the
# respawn corrections above are.
#
# A figure of "" is a townsperson the world's own placements already stand -- Hanzo is
# Smith01 at 116,141 in lorencia.json, Pasi Wizard01, Baz Storage01 -- so the sim knows them
# and nothing draws them twice. The wandering merchants' travelling is not carried.
FOLK_VERSION075 = {
    0: [  # Lorencia
        (248, "Wandering Merchant Martin", "WanderingMerchant", 6, 145, 4),
        (240, "Baz The Vault Keeper", "", 146, 110, 4),
        (240, "Baz The Vault Keeper", "", 147, 145, 2),
        (249, "Berdysh Guard", "BerdyshGuard", 131, 88, 2),
        (249, "Berdysh Guard", "BerdyshGuard", 173, 125, 4),
        (249, "Berdysh Guard", "BerdyshGuard", 94, 125, 8),
        (249, "Berdysh Guard", "BerdyshGuard", 94, 130, 8),
        (249, "Berdysh Guard", "BerdyshGuard", 131, 148, 2),
        (247, "Crossbow Guard", "CrossbowGuard", 114, 125, 4),
        (250, "Wandering Merchant Harold", "WanderingMerchant", 183, 137, 3),
        (251, "Hanzo The Blacksmith", "", 116, 141, 4),
        (253, "Potion Girl Amy", "PotionGirlAmy", 127, 86, 3),
        (254, "Pasi The Mage", "", 118, 113, 4),
        (255, "Lumen the Barmaid", "LumentheBarmaid", 123, 135, 2),
        # Ours, not Version075's: the quest giver, MuMain's MONSTER_MARLON, on the town square
        # where the user placed him (2026-09-28), square to the wall at his back -- OpenMU Season Six has him wandering from
        # 136,88 by the north bridge. Lorencia is the scope, so here he stays.
        (229, "Marlon", "Marlon", 130, 127, 5),
        # Ours, not Version075's: the Dungeon's quest giver (docs/golden-archer.md), MU's
        # MONSTER_GOLDEN_ARCHER, at the arch where the user put him (2026-09-30), facing 5 as
        # MuMain forces an NPC skeleton in Lorencia to angle 90. 0.95d stands him at 175,120.
        (236, "Golden Archer", "GoldenArcher", 120, 229, 5),
    ],
    3: [  # Noria
        (253, "Potion Girl Amy", "PotionGirlAmy", 169, 109, 4),
        (253, "Potion Girl Amy", "PotionGirlAmy", 193, 110, 3),
        (242, "Elf Lala", "", 173, 125, 2),
        (243, "Eo the Craftsman", "", 195, 124, 3),
        (240, "Baz The Vault Keeper", "", 172, 96, 4),
        (238, "Chaos Goblin", "", 180, 103, 2),
        # Ours, not Version075's: the elf's quest giver. MuMain's MONSTER_ELF_SOLDIER, whom its
        # Korean client names Peia, in the flower bed under the town's great tree where the user
        # put her (2026-09-29) -- OpenMU Season Six stands her four tiles west, at 167,118.
        (257, "Peia", "Peia", 171, 118, 3),
        # Ours: Charon (237), Devil Square's gatekeeper, where 0.95d stands him
        # (Version095d/Maps/Noria.cs:32). 0.75 has no Devil Square; until it is written he says
        # the hero is not ready (the user, 2026-09-30).
        (237, "Charon", "", 171, 105, 4),
        # Ours: Noria's watch (the user, 2026-09-29), which neither Version075 nor MuMain has.
        # An archer and a crossbow at each of the town's three roads, a tile outside the safe
        # bit. Every one fights on the Crossbow Guard's row, 247 -- five tiles is a bow's reach
        # as much as a crossbow's -- and the figure says which weapon she draws.
        (247, "Elf Archer", "ElfArcher", 166, 80, 3),
        (247, "Elf Crossbow Guard", "ElfCrossbowGuard", 163, 83, 2),
        (247, "Elf Archer", "ElfArcher", 157, 112, 1),
        (247, "Elf Crossbow Guard", "ElfCrossbowGuard", 157, 116, 1),
        (247, "Elf Archer", "ElfArcher", 208, 128, 6),
        (247, "Elf Crossbow Guard", "ElfCrossbowGuard", 204, 131, 6),
    ],
    2: [  # Devias: Version075/Maps/Devias.cs:46-54
        (244, "Caren the Barmaid", "", 226, 25, 4),
        (245, "Izabel The Wizard", "", 225, 41, 4),
        (246, "Zienna The Weapons Merchant", "", 186, 47, 4),
        (247, "Crossbow Guard", "CrossbowGuard", 224, 79, 6),
        (247, "Crossbow Guard", "CrossbowGuard", 219, 79, 6),
        (247, "Crossbow Guard", "CrossbowGuard", 169, 45, 8),
        (247, "Crossbow Guard", "CrossbowGuard", 169, 39, 8),
        (241, "Guild Master", "", 215, 45, 2),
        (240, "Baz The Vault Keeper", "", 218, 63, 3),
        # Ours, not Version075's: Devias's quest giver, MuMain's MONSTER_PRIEST_DEVIN (406),
        # where OpenMU's Season Six stands him (VersionSeasonSix/Maps/Devias.cs:36); the user
        # took that spot, 2026-09-29.
        (406, "Apostle Devin", "", 181, 35, 4),
        # Ours: Sevina the Priestess (235), the class change's giver, where 0.95d stands her
        # (Version095d/Maps/Devias.cs:35). 0.75 has no class change.
        (235, "Sevina the Priestess", "", 183, 32, 4),
        # Ours: the Messenger of Archangel (233), Blood Castle's gatekeeper, at the first of the
        # two tiles Season Six stands him on (VersionSeasonSix/Maps/Devias.cs:45). 0.75 has no
        # Blood Castle; until it is written he says the hero is not ready (the user, 2026-09-30).
        (233, "Messenger of Archangel", "", 217, 29, 4),
    ],
}


# What a click on a placement does, by map and MU object type: (pose, turns, leans, tall).
# MU2's client/core/Poses.cs `Poses.Of`, which is ZzzInterface.cpp's MOVEMENT_OPERATE switch --
# none of it is in the map file, so it cannot be derived and is transcribed whole. The pose is
# OpenMU's CharacterPose number: 2 sitting, 3 leaning, 4 hanging. `turns` is the arms that carry
# `Hero->Object.Angle[2] = TargetAngle`; `leans` is RenderCursor's own list for the lean pointer,
# which disagrees with the pose on Noria's 38; `tall` is the three with the 160-unit pick box.
# Poses.cs keeps the other worlds' rows as comments; they join this table when their map opens.
PERCHES = {
    0: {  # Lorencia: 53 hidden pose boxes along the walls, the river's logs, the tavern benches
        133: (3, True, True, True),     # PoseBox01, invisible
        6: (2, False, False, False),    # Tree07, the fallen log
        145: (2, True, False, False),   # Furniture06
        146: (2, False, False, False),  # Furniture07
    },
    1: {  # the Dungeon: MOVEMENT_OPERATE's WD_1DUNGEON arm (ZzzInterface.cpp:1705-1712), 60 on
          # RenderCursor's lean list (:4053) and given the 160-unit box (ZzzObject.cpp:4661-4663)
        60: (3, True, True, True),      # Object61, the lean box, invisible and with no .bmd
        59: (2, False, False, False),   # Object60, the stone seat
    },
    2: {  # Devias: MOVEMENT_OPERATE's WD_2DEVIAS arm (ZzzInterface.cpp:1715-1726), and 91 on
          # RenderCursor's lean list (:4054). 44 lean boxes, 85 seats in the church and the inns.
        91: (3, True, True, True),      # Object92, the lean box, invisible
        22: (2, True, False, False),    # Object23
        25: (2, True, False, False),    # Object26
        40: (2, True, False, False),    # Object41
        55: (2, True, False, False),    # Object56
        45: (2, False, False, False),   # Object46
        73: (2, False, False, False),   # Object74
    },
    3: {  # Noria
        38: (4, True, True, False),     # the hanging tree, invisible
        8: (2, False, False, False),    # the stump
    },
}


def cook_tables(world, out_dir):
    """mu.db's rows, through index.json, as one flat versioned file the game reads whole.

    The .mur format ("MU2 rules"), version 9, little-endian:

        'MU2R', u32 version, u32 hz, u32 kinds, u32 spawns, u32 arms, u32 actions, u32 items,
                u32 folk,
                u32 map number, u32 grid size, i32 safe gate x1, y1, x2, y2
        kinds:  u16 len + figure name (the cooked figure this breed wears, or empty),
                u16 len + label ("Bull Fighter"),
                i32 number, level, health, minimumDamage, maximumDamage, defense,
                i32 moveRange, attackRange, viewRange,
                i32 moveTicks, attackTicks, respawnTicks,
                i32 attackRate, defenseRate, attackSkill, f32 scale
        spawns: u32 kind (an index into the kinds above), i32 x1, x2, y1, y2, u32 count
        arms:   u16 len + name ("Sword01"), u16 len + label ("Kris"),
                u16 len + stance ("sword", "two_hand_sword", "spear", "scythe", ... , empty),
                i32 kind (0 a weapon, 1 a shield), i32 minimum damage, i32 maximum damage,
                i32 attack speed, i32 defense, i32 strength wanted, i32 agility wanted,
                i32 classes (bit 0 Dark Wizard, bit 1 Fairy Elf, bit 2 Dark Knight -- mu.db's
                own class enumeration, not MU's packed class byte),
                i32 group, i32 number (MU's own item group and index, which is what the
                client's attack ladder tests), i32 flags (bit 0 two-handed, bit 1 a bow,
                bit 2 a crossbow)
        items:  (version 4, sprint 7) u16 len + name, u16 len + label, u16 len + glb path,
                then (version 7) u16 len + the name of the skill it teaches and u16 len + a
                line of what that skill does, both "" off a scroll or an orb, and (version 8)
                u16 len + a line of what the ITEM does -- "" on every row a column already
                states, and see pipeline/index.py for which six carry one -- then
                (version 7) i32 the skill's number, its level requirement, its energy
                requirement, and then
                i32 group, number, drop level, width, height, minimum damage, maximum
                damage, attack speed, defense, magic power, durability, classes (as the
                arms), then the requirement's RAW level, strength, agility, energy,
                vitality (MU's formula scales them in the sim), flags (bit 0 drops from
                monsters, bit 1 a jewel, bit 2 two-handed, bit 3 worn armour, bit 4 a
                shield, bit 5 a weapon), maximum drop level (0 none), skill, and (version 6)
                defense rate -- a shield's block column, 0 for everything else
        folk:   (version 5, sprint 7) u16 len + name, u16 len + figure ("" where the world's
                own placements stand them), i32 MU's NPC number, tile x, tile y, facing (MU2's
                Look, 1 West clockwise to 8 NorthWest). FOLK_VERSION075 above
        actions: i32 action (MU's own number), i32 keys, f32 authored play speed -- the player
                library's, and only the actions a swing can land on
        grid:   u16 a tile, row-major [y][x], MU's own attribute word
        perches: (version 9) u32 count, then per perch u8 pose (2 sit, 3 lean, 4 hang), u8
                turns, u8 leans, u8 tall, i32 column, row (the tile walked to), f32 x, y (the
                placement's origin in the sim's tiles), f32 aim (the sim's facing). PERCHES

    The arms are what a fight needs off an item and nothing else: a damage band, a defence, who
    may hold it and what it asks of him. They come from `index.json`'s own object rows and NOT
    from `mu.db`, whose `items` table carries names and drop levels alone -- MU2's pipeline puts
    an item's combat row on the asset. Everything else about an item -- the bag, the drop, the
    durability, the level and its options, the excellent arm -- is sprint 7's, and none of it is
    here. `attack_speed` is carried and is deliberately NOT consumed yet: MU paces a swing by
    the attack clip's own authored length, and a mapping from this number to a swing delay
    would be an invention in the one sprint that has none.

    The actions are here for one reason: **how fast a character swings is the length of the clip
    he swings with**, and the sim has no clips. MU's own arithmetic is
    `length = keys / ((speed + attackSpeed * 0.004) * 25)` seconds -- `SetAttackSpeed` at
    ZzzCharacter.cpp:813 for the 0.004, and 25 is the frame rate MU's play speeds are stated
    against -- so the two numbers a clip contributes are its key count and its authored speed,
    and those are what travel. The clips themselves stay where they are.

    The safe gate is the rectangle a dead character stands up in -- the map's own spawn box,
    `gates.safe` on the world's json, which for Lorencia is (133, 118)-(151, 135) and is
    `gates` row 17 in mu.db. All zeroes means the map has none and a death has nowhere to send
    anybody.

    The grid is in here because the sim is the first thing that reads it for a *decision* and
    the sim has no PNG decoder and no window. It is MU's whole 16-bit word -- `red | green << 8`,
    as Terrain.cs:87 reads it -- and not the red channel alone: measured, the green channel is
    zero on all 65 536 tiles of Lorencia and of Noria, so nothing is being lost today and
    everything would be on the first map that used the high byte. The game reads the same file
    and checks it against the ground's own copy of attributes.png at load, so the two cannot
    drift apart unnoticed.

    All sixteen kinds are written whatever map this is -- the breed table is not a map's --
    and only this map's spawn rectangles. `x` is the attribute grid's column and `y` its row,
    MU's own tile coordinates, un-negated: the negation into world z belongs to whoever draws
    them, not to the table (docs/conventions.md, "Space").

    There is no experience table and no drop table here, and PLAN.md foundation 11 used to say
    there would be. mu.db holds neither, because MU has an expression per level rather than a
    list of numbers; both are code in src/sim/rules.cpp with their OpenMU lines beside them.
    """
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)

    entry = next(w for w in index["worlds"] if w["name"] == world)
    number = entry["number"]
    # Which cooked figure a breed wears, by MU's own monster number. A breed with no model in
    # this content still gets a row: the rules do not need a mesh, and the sim is what this
    # table is for.
    figure_of = {one["number"]: (one["name"], float(one.get("scale", 1.0)))
                 for one in index["monsters"]}

    remainders = []
    corrections = []
    kinds, spawns = [], []
    for breed in sorted(index["breeds"], key=lambda b: b["number"]):
        combat = breed["combat"]
        kind = breed["number"]
        figure, scale = figure_of.get(kind, ("", 1.0))
        respawn = RESPAWN_VERSION075.get(kind, combat["respawn_seconds"])
        if respawn != combat["respawn_seconds"]:
            corrections.append(f"{combat['name']} respawn {combat['respawn_seconds']}s -> "
                               f"{respawn}s (Version075/Maps/Lorencia.cs)")
        kinds.append(write_string(figure) + write_string(combat["name"]) + struct.pack(
            "<15if", kind, combat["level"], combat["health"], combat["minimum_damage"],
            combat["maximum_damage"], combat["defense"], combat["move_range"],
            combat["attack_range"], combat["view_range"],
            ticks_of(combat["move_delay"], "move_delay", combat["name"], remainders),
            ticks_of(combat["attack_delay"], "attack_delay", combat["name"], remainders),
            respawn * SIM_HZ, combat["attack_rate"], combat["defense_rate"],
            combat.get("attack_skill", 0), scale))
        for spawn in breed.get("spawns", []):
            if spawn["map"] != number:
                continue
            spawns.append(struct.pack("<I4iI", len(kinds) - 1, spawn["x1"], spawn["x2"],
                                      spawn["y1"], spawn["y2"], spawn["count"]))

    # Every action a swing can land on, with the two numbers its length is made of. 38 is the
    # fist, 39-45 the sword ladder, 46-49 spear and scythe, 50 and 51 the bow and the crossbow.
    #
    # And the skill clips, 60 to 64 and Defense's 187, for the same reason rather than a new one:
    # a skill's cooldown can never be shorter than the animation it plays (docs/skills-dk.md
    # §3.2), and the animation's length is these same two numbers. The sim asks
    # `sim::castTicks` for it, which reads this table. And the wizard's two hands, 147 and 148
    # (`PLAYER_SKILL_HAND1..2`), which is how long an Energy Ball takes to throw. And 183, "Skill
    # recovery", the arm thrown up to the sky that Lightning and Meteorite are cast in: MU gives it
    # no play speed, so it has the exporter's default (pipeline/export_gltf.py DEFAULT_PLAY_SPEED),
    # 13 keys at 0.25 -- the 2.08 s the drawing plays. And 65, `PLAYER_ATTACK_SKILL_WHEEL`,
    # Twisting Slash's own spin (SkillCast.cpp:306-312), 13 keys at 0.24.
    keys = index.get("action_keys", {})
    speeds = index.get("action_speeds", {})
    unassigned = {"183": 0.25}
    actions = []
    for action in list(range(38, 52)) + [60, 61, 62, 63, 64, 65, 147, 148, 183, 187]:
        name = str(action)
        speed = speeds.get(name, unassigned.get(name))
        if name not in keys or speed is None:
            continue
        actions.append(struct.pack("<iif", action, int(keys[name]), float(speed)))

    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)
    size = int(map_data["size"])
    grid_w, grid_h, channels, pixels = read_png(os.path.join(world_dir, map_data["attributes"]))
    if grid_w != size or grid_h != size:
        raise ValueError(f"{world}: attributes.png is {grid_w}x{grid_h} and the world says {size}")
    # MU's word is two bytes and lives in the red and green channels. Both are carried; the
    # high one is zero everywhere in this content and the day it is not, this line is already
    # right.
    words = bytearray(size * size * 2)
    high = 0
    for i in range(size * size):
        low = pixels[i * channels]
        top = pixels[i * channels + 1] if channels > 1 else 0
        high |= top
        struct.pack_into("<H", words, i * 2, low | (top << 8))
    if high:
        print(f"cook: NOTE {world}'s attribute grid uses its high byte")

    # The things to sit on or lean against (version 9), hidden ones included -- in Lorencia the
    # commonest of them IS hidden. The sim walks to `column, row`, the placement's own tile,
    # MU's `Owner->Position / TERRAIN_SCALE`; `x, y` are the placement's exact origin in the
    # sim's tiles (a tile's centre is its integer), which is where the pick box stands. The aim
    # is MU's Angle[2] turned into the sim's facing: MU faces (sin a, -cos a) in (column, row)
    # and the sim faces (cos f, sin f), so f = a - 90 degrees.
    perches = []
    per_tile = float(map_data["units_per_tile"])
    for one in map_data["objects"]:
        rule = PERCHES.get(number, {}).get(one["type"])
        if rule is None:
            continue
        pose, turns, leans, tall = rule
        stored_x, stored_y, _ = one["at"]
        aim = math.radians(float(one.get("angle", (0, 0, 0))[2]) - 90.0)
        aim = math.atan2(math.sin(aim), math.cos(aim))
        column = int(math.floor(stored_x / per_tile)) & (size - 1)
        row = int(math.floor(stored_y / per_tile)) & (size - 1)
        perches.append(struct.pack("<4B2i3f", pose, int(turns), int(leans), int(tall), column,
                                   row, stored_x / per_tile - 0.5, stored_y / per_tile - 0.5,
                                   aim))

    # The safe gate is on index.json's world entry and NOT on the world's own json beside the
    # grids -- which is where this looked first, and a missing gate is not an error there, so a
    # dead character quietly stood up where he fell instead of in town.
    # Every weapon and shield with a combat row, in name order so the table is stable.
    kClass = {"wizard": 1, "elf": 2, "knight": 4}
    # Two-handedness, a bow and a crossbow off the stance the cook already reads from the item's
    # own row. The client's ladder tests MU's model constants; the stance is what MU2's pipeline
    # wrote them down as, and it is the same fact by another name.
    kTwoHanded, kBow, kCrossbow = 1, 2, 4
    arms = []
    arm_names = []
    for one in sorted(index["objects"], key=lambda o: o["name"]):
        if one.get("kind") not in ("weapon", "shield"):
            continue
        stats = one.get("stats") or {}
        if not stats:
            continue
        wants = stats.get("requires") or {}
        classes = 0
        for name in stats.get("classes") or []:
            classes |= kClass.get(name, 0)
        arm_names.append(one["name"])
        stance = one.get("stance", "")
        flags = 0
        if stance in ("two_hand_sword", "scythe", "bow", "crossbow"):
            flags |= kTwoHanded
        if stance == "bow":
            flags |= kBow
        if stance == "crossbow":
            flags |= kCrossbow
        arms.append(write_string(one["name"]) + write_string(one.get("label", one["name"])) +
                    write_string(stance) +
                    struct.pack("<11i", 1 if one["kind"] == "shield" else 0,
                                int(stats.get("minimum_damage") or 0),
                                int(stats.get("maximum_damage") or 0),
                                int(stats.get("attack_speed") or 0),
                                int(stats.get("defense") or 0),
                                int(wants.get("strength") or 0),
                                int(wants.get("agility") or 0), classes,
                                int(stats.get("group", -1)), int(stats.get("number", -1)),
                                flags))

    # Every item with a row, for the bag, the shop and the drop (sprint 7): the whole of
    # index.json's objects[].stats, which is where MU2's pipeline puts an item's row -- the
    # footprint, the drop level, the requirement's raw numbers, the defence, the classes.
    # mu.db's `items` table has names and drop levels alone. In name order, like the arms.
    kItemDrops, kItemJewel, kItemTwoHanded, kItemArmour, kItemShield, kItemWeapon = (
        1, 2, 4, 8, 16, 32)
    items = []
    for one in sorted(index["objects"], key=lambda o: o["name"]):
        stats = one.get("stats") or {}
        if not stats or "group" not in stats:
            continue
        wants = stats.get("requires") or {}
        classes = 0
        for name in stats.get("classes") or []:
            classes |= kClass.get(name, 0)
        flags = 0
        if stats.get("drops_from_monsters", True):
            flags |= kItemDrops
        if stats.get("jewel"):
            flags |= kItemJewel
        if one.get("stance", "") in ("two_hand_sword", "scythe", "bow", "crossbow"):
            flags |= kItemTwoHanded
        if one.get("kind") == "armor":
            flags |= kItemArmour
        if one.get("kind") == "shield":
            flags |= kItemShield
        if one.get("kind") == "weapon":
            flags |= kItemWeapon
        # What a scroll or an orb teaches, which rides with the row it belongs to: the skill's
        # number, what it asks of energy and level, its name, and a line of what it does. Empty
        # on everything else. See pipeline/index.py, "What a scroll teaches".
        teaches = one.get("skill") or {}
        items.append(write_string(one["name"]) + write_string(one.get("label", one["name"])) +
                     write_string(one.get("glb", "")) +
                     write_string(str(teaches.get("name") or "")) +
                     write_string(str(teaches.get("tells") or "")) +
                     # And (version 8) one line of what the item itself does, for the rows a
                     # column cannot state: the Ale, the Antidote, the Town Portal Scroll and
                     # the three jewels. Empty on everything else. See pipeline/index.py.
                     write_string(str(one.get("tells") or "")) +
                     struct.pack("<3i", int(teaches.get("number") or 0),
                                 int(teaches.get("level_requirement") or 0),
                                 int(teaches.get("energy_requirement") or 0)) +
                     struct.pack("<21i", int(stats["group"]), int(stats["number"]),
                                 int(stats.get("drop_level") or 0),
                                 int(stats.get("width") or 1), int(stats.get("height") or 1),
                                 int(stats.get("minimum_damage") or 0),
                                 int(stats.get("maximum_damage") or 0),
                                 int(stats.get("attack_speed") or 0),
                                 int(stats.get("defense") or 0),
                                 int(stats.get("magic_power") or 0),
                                 int(stats.get("durability") or 0), classes,
                                 int(wants.get("level") or 0), int(wants.get("strength") or 0),
                                 int(wants.get("agility") or 0), int(wants.get("energy") or 0),
                                 int(wants.get("vitality") or 0), flags,
                                 int(stats.get("maximum_drop_level") or 0),
                                 int(stats.get("skill") or 0),
                                 int(stats.get("defense_rate") or 0)))

    folk = [write_string(name) + write_string(figure) + struct.pack("<4i", npc, x, y, look)
            for (npc, name, figure, x, y, look) in FOLK_VERSION075.get(number, [])]

    gate = entry.get("gates", {}).get("safe", {})
    blob = struct.pack("<4sIIIIIIIIII4i", b"MU2R", 9, SIM_HZ, len(kinds), len(spawns), len(arms),
                       len(actions), len(items), len(folk), number, size,
                       int(gate.get("x1", 0)), int(gate.get("y1", 0)), int(gate.get("x2", 0)),
                       int(gate.get("y2", 0)))
    blob += (b"".join(kinds) + b"".join(spawns) + b"".join(arms) + b"".join(actions) +
             b"".join(items) + b"".join(folk) + bytes(words))
    blob += struct.pack("<I", len(perches)) + b"".join(perches)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{world}.mur")
    with open(path, "wb") as handle:
        handle.write(blob)

    for line in corrections:
        print(f"cook: corrected {line}")
    for line in remainders:
        print(f"cook: WARNING {line}")
    alive = sum(struct.unpack_from("<I", one, 20)[0] for one in spawns)
    print(f"cook: {len(kinds)} breeds, {len(spawns)} nests holding {alive} monsters, "
          f"{len(arms)} arms, {len(actions)} attack actions, {len(items)} items and "
          f"{len(folk)} townsfolk and {len(perches)} perches -> "
          f"{os.path.relpath(path, ROOT)} ({len(blob)} bytes)")
    return 0


# What every cooked sound is resampled and downmixed to. See cook_showing for why each of
# the two numbers is what it is; the short version is that MONO IS NOT AN OPTIMISATION -- a
# stereo file cannot be panned, and sprint 6 places every emitter by the vector from the hero
# rotated by the camera's yaw.
SOUND_RATE = 22050
SOUND_BITS = 16


def read_wav(path):
    """One wav as mono 16-bit samples at its own rate. Returns (samples, rate).

    MU's sounds are not one format and this is the whole reason this function exists rather
    than a copy: measured over the 104 files, there are NINE -- 47 of them stereo 44.1 kHz
    16-bit, 18 mono 22.05 kHz 16-bit, 10 mono 44.1 kHz 8-BIT, and one lone file at 11127 Hz,
    which is not a rate anybody chooses on purpose. `wave` hands back raw frames and leaves
    all of that to the caller.

    8-bit wav is UNSIGNED with 128 for silence, and 16-bit is signed. Reading the first as
    the second is the classic way to turn a footstep into a scream of white noise.
    """
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        raw = handle.readframes(handle.getnframes())

    if width == 1:
        wide = array.array("h", (int(b - 128) * 256 for b in raw))
    elif width == 2:
        wide = array.array("h")
        wide.frombytes(raw)
        if sys.byteorder == "big":
            wide.byteswap()
    else:
        raise ValueError(f"{path}: {width * 8}-bit wav, and this reads 8 and 16")

    if channels > 1:
        # Averaged rather than taking the left, because several of these are a true stereo
        # pair and one side of a pair is half the sound. The sum cannot overflow the sample
        # type because it is done in Python's own integers and divided before it is stored.
        mixed = array.array("h", (sum(wide[i:i + channels]) // channels
                                  for i in range(0, len(wide) - channels + 1, channels)))
        wide = mixed
    return wide, rate


def resample(samples, rate, target):
    """Linear interpolation to `target` Hz. Returns the samples unchanged when it is a no-op.

    Linear and not a windowed filter on purpose: the material is 1990s game audio, half of
    it already 8-bit, and the aliasing a proper low-pass would save is below what this
    content carries. If a sound ever comes out harsh this is the line to revisit, and the
    cook says which files it touched so there is somewhere to look.
    """
    if rate == target or not samples:
        return samples
    count = max(1, int(len(samples) * target / rate))
    step = len(samples) / count
    out = array.array("h", bytes(2 * count))
    last = len(samples) - 1
    for i in range(count):
        where = i * step
        low = int(where)
        if low >= last:
            out[i] = samples[last]
            continue
        frac = where - low
        out[i] = int(samples[low] + (samples[low + 1] - samples[low]) * frac)
    return out


def write_wav(path, samples, rate):
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        if sys.byteorder == "big":
            samples = array.array("h", samples)
            samples.byteswap()
        handle.writeframes(samples.tobytes())


def cook_showing(out_dir, texcook, threads):
    """Sprint 6's content: the effect sheets compressed, and the sounds made one format.

    Neither is per-world and both sit beside the figures rather than under a map, for the
    figures' own reason: an effect belongs to a blow and a sound to an event, and cooking
    them per world would write the same blood sheet into Lorencia and into Noria.

    The .mus format ("MU2 showing"), version 1, little-endian:

        'MU2S', u32 version, u32 effects, u32 events, u32 sampleRate, u32 channels
        effects: u16 len + name ("hit_blood"), u16 len + .ktx path relative to assets/
        events:  u16 len + name ("melee_hit"), f32 onset seconds, f32 gain dB,
                 u32 files, then per file u16 len + .wav path relative to assets/

    **The sheets are not cut out.** texcook takes a cutout threshold and rescales alpha down
    the mip chain so a leaf holds its coverage, and every effect sheet here is passed -1
    instead. That rule is for alpha TESTING, where a texel is kept or discarded and the
    average of a chain quietly thins a leaf away. An effect is blended: its alpha is the
    gradient that makes it a soft-edged sprite, and holding a coverage in it would grade the
    gradient into a mask. Same file, same format, opposite treatment, and the difference is
    the blend mode rather than anything about the art.

    **The sounds are made mono 16-bit at one rate, and the mono half is not about size.**
    An emitter in this game is placed: the census has the listener at the character, the
    direction taken from the hero and rotated by the camera's yaw alone. A stereo file has
    its own left and right already baked in and cannot be panned to a place -- so 47 of the
    104 files, the ones in the dominant format, are the ones that could not have worked.
    Downmixing is what makes the feature possible.

    The rate is a judgement and it is 22050. One rate means the mixer never resamples a
    voice at play; nine rates means it always might. 22050 is the rate a third of this
    corpus already carries, the top of what its 8-bit files can hold anything in, and the
    era the art is from. The 57 files at 44.1 kHz lose their top octave, and that is the
    cost, written down here rather than discovered later.

    `gain` stays in decibels because that is what the asset says. Converting it to a linear
    factor in the cook would put an engine's arithmetic in a table of facts.
    """
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)

    effects = index.get("effects") or {}
    events = index.get("sounds") or {}
    onsets = index.get("sound_onsets") or {}
    gains = index.get("sound_gains") or {}

    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "sounds"), exist_ok=True)
    started = time.time()

    # ------------------------------------------------------------------ the sheets
    jobs = []
    rows = []
    seen = {}
    missing = []
    raw_in = 0
    for name in sorted(effects):
        source = os.path.join(ASSETS, effects[name])
        if not os.path.exists(source):
            missing.append(name)
            continue
        with open(source, "rb") as handle:
            data = handle.read()
        raw_in += len(data)
        digest = hashlib.sha1(data).hexdigest()[:16]
        if digest not in seen:
            # The role goes in the name, where every other cooked texture carries it and
            # where the audit reads it from: `<what>_<role>_<digest>`. Without it a sheet
            # whose own filename ends in a role word -- `chat_on_normal.png` is real -- is
            # read as that role and failed for being what it is.
            base = safe(os.path.splitext(os.path.basename(effects[name]))[0])
            stem = f"effect_{base}_albedo_{digest}"
            ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
            # -1: blended, not tested. See the note above. Skipped when it is there: the stem
            # carries the sheet's sha1, so existence is the up-to-date test, as in `collect`.
            # Without it adding one sheet re-compressed all 177 on every core (2026-09-28).
            if not os.path.exists(ktx_path):
                jobs.append(("albedo", -1.0, source, ktx_path))
            seen[digest] = os.path.relpath(ktx_path, ASSETS)
        rows.append(write_string(name) + write_string(seen[digest]))

    if jobs:
        job_file = os.path.join(out_dir, "jobs.txt")
        with open(job_file, "w") as handle:
            for role, cutout, source, target in jobs:
                handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")
        command = [texcook, job_file]
        if threads:
            command += ["--threads", str(threads)]
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode

    # ------------------------------------------------------------------ the sounds
    wav_in = 0
    wav_out = 0
    resampled = 0
    downmixed = 0
    gone = []
    cooked = {}
    rows_sound = []
    for name in sorted(events):
        files = []
        for relative in events[name]:
            source = os.path.join(ASSETS, relative)
            if not os.path.exists(source):
                gone.append(relative)
                continue
            if relative not in cooked:
                wav_in += os.path.getsize(source)
                samples, rate = read_wav(source)
                with wave.open(source, "rb") as probe:
                    if probe.getnchannels() > 1:
                        downmixed += 1
                if rate != SOUND_RATE:
                    resampled += 1
                    samples = resample(samples, rate, SOUND_RATE)
                target = os.path.join(out_dir, "sounds",
                                      safe(os.path.basename(relative)))
                write_wav(target, samples, SOUND_RATE)
                wav_out += os.path.getsize(target)
                cooked[relative] = os.path.relpath(target, ASSETS)
            files.append(write_string(cooked[relative]))
        rows_sound.append(write_string(name) +
                          struct.pack("<ffI", float(onsets.get(name, 0.0)),
                                      float(gains.get(name, 0.0)), len(files)) +
                          b"".join(files))

    blob = struct.pack("<4sIIIII", b"MU2S", 1, len(rows), len(rows_sound),
                       SOUND_RATE, 1)
    blob += b"".join(rows) + b"".join(rows_sound)
    path = os.path.join(out_dir, "showing.mus")
    with open(path, "wb") as handle:
        handle.write(blob)

    ktx = sum(os.path.getsize(os.path.join(out_dir, "textures", f))
              for f in os.listdir(os.path.join(out_dir, "textures")))
    if missing:
        print(f"cook: {len(missing)} effect sheets named and not on disk: "
              f"{', '.join(missing[:6])}{'...' if len(missing) > 6 else ''}")
    if gone:
        print(f"cook: {len(gone)} sound files named and not on disk: {', '.join(gone[:4])}")
    print(f"cook: {len(rows)} effect sheets ({len(jobs)} distinct), "
          f"{raw_in / 1e6:.1f} MB of png -> {ktx / 1e6:.1f} MB of .ktx with mips")
    print(f"cook: {len(cooked)} sounds over {len(rows_sound)} events, "
          f"{downmixed} downmixed to mono, {resampled} resampled to {SOUND_RATE} Hz, "
          f"{wav_in / 1e6:.1f} MB -> {wav_out / 1e6:.1f} MB")
    print(f"cook: showing -> {os.path.relpath(path, ROOT)} ({len(blob)} bytes), "
          f"{time.time() - started:.1f} s")
    return 0


def read_obj(path):
    """One Wavefront .obj as (positions, normals, uvs, groups).

    MuExtract's `export-obj` writes exactly one dialect and this reads exactly that one:
    `v`/`vn`/`vt`, one `g` per MU mesh named for the sheet that mesh wears, and triangles
    only, every corner `v/t/n` and one-based. Anything else is refused rather than guessed
    at -- a quad silently dropped is a hole in a rock nobody can explain later.

    `groups` is a list of (name, [(v, t, n), (v, t, n), (v, t, n)]) in file order, because
    the group IS the part: it is the only record of which faces sample which sheet, which is
    what ObjCommands.cs says it wrote the grouping down for.
    """
    at, normals, uvs, groups = [], [], [], []
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            if line.startswith("v "):
                at.append(tuple(float(x) for x in line.split()[1:4]))
            elif line.startswith("vn "):
                normals.append(tuple(float(x) for x in line.split()[1:4]))
            elif line.startswith("vt "):
                uvs.append(tuple(float(x) for x in line.split()[1:3]))
            elif line.startswith("g "):
                groups.append((line[2:].strip(), []))
            elif line.startswith("f "):
                corners = line.split()[1:]
                if len(corners) != 3:
                    raise ValueError(f"{path}:{number}: a face of {len(corners)} corners, and "
                                     f"this reads triangles")
                if not groups:
                    raise ValueError(f"{path}:{number}: a face before any `g`, so nothing says "
                                     f"which sheet it wears")
                triangle = []
                for corner in corners:
                    parts = (corner.split("/") + ["", ""])[:3]
                    triangle.append(tuple(int(p) if p else 0 for p in parts))
                groups[-1][1].append(tuple(triangle))
    return at, normals, uvs, groups


def obj_part(at, normals, uvs, triangles, per_tile):
    """One group's triangles as (vertices, indices), in metres and with tangents.

    **The .obj is already on this engine's axes and is NOT in metres**, which is the one
    thing about these files that is worth writing down. MuExtract builds every mesh with
    `ConvertAxes` on (BmdMeshBuilder, AxisConvention.ToEngine), so MU's `(x, y, z)` with z up
    was turned into `(x, z, -y)` when the .obj was written -- the same swap
    docs/conventions.md states, done once at import. What is left to do here is the other
    half of that page: MU counts 100 units to a tile and a tile is a metre, so the whole
    thing is divided by the worlds' own `units_per_tile` rather than by a 100 written in.
    A second axis swap here would lay every missile on its side.

    The V coordinate is flipped BACK. ObjCommands.cs writes `1 - v` because OBJ's texture
    origin is the bottom-left and MU's is the top-left; glTF's is the top-left too, so every
    cooked mesh in this engine carries MU's own V and a missile must as well.

    Tangents are derived here because an .obj has none and content/mesh.h's vertex has a
    slot for one: the cook is where that is paid for, once, rather than in a shader that
    would re-derive it per pixel. Averaged per vertex over the triangles that share it and
    orthogonalised against the normal, which is glTF's own definition, with w the bitangent's
    sign. A degenerate UV triangle -- MU's effect cards have a few -- contributes nothing
    instead of a NaN.
    """
    unique = {}
    vertices = []
    indices = []
    tangents = []
    bitangents = []
    for triangle in triangles:
        corner_index = []
        for v, t, n in triangle:
            key = (v, t, n)
            if key not in unique:
                if v <= 0 or v > len(at):
                    raise ValueError(f"a face names vertex {v} of {len(at)}")
                position = [c / per_tile for c in at[v - 1]]
                normal = list(normals[n - 1]) if 0 < n <= len(normals) else [0.0, 1.0, 0.0]
                uv = ([uvs[t - 1][0], 1.0 - uvs[t - 1][1]] if 0 < t <= len(uvs) else [0.5, 0.5])
                unique[key] = len(vertices)
                vertices.append([position, normal, uv])
                tangents.append([0.0, 0.0, 0.0])
                bitangents.append([0.0, 0.0, 0.0])
            corner_index.append(unique[key])
        indices += corner_index

        a, b, c = (vertices[i] for i in corner_index)
        edge1 = [b[0][i] - a[0][i] for i in range(3)]
        edge2 = [c[0][i] - a[0][i] for i in range(3)]
        du1, dv1 = b[2][0] - a[2][0], b[2][1] - a[2][1]
        du2, dv2 = c[2][0] - a[2][0], c[2][1] - a[2][1]
        area = du1 * dv2 - du2 * dv1
        if abs(area) < 1e-12:
            continue
        r = 1.0 / area
        tangent = [(edge1[i] * dv2 - edge2[i] * dv1) * r for i in range(3)]
        bitangent = [(edge2[i] * du1 - edge1[i] * du2) * r for i in range(3)]
        for i in corner_index:
            for axis in range(3):
                tangents[i][axis] += tangent[axis]
                bitangents[i][axis] += bitangent[axis]

    packed = bytearray()
    low = [1e30] * 3
    high = [-1e30] * 3
    for index, (position, normal, uv) in enumerate(vertices):
        for axis in range(3):
            low[axis] = min(low[axis], position[axis])
            high[axis] = max(high[axis], position[axis])
        t = tangents[index]
        dot = sum(t[i] * normal[i] for i in range(3))
        t = [t[i] - normal[i] * dot for i in range(3)]
        length = math.sqrt(sum(c * c for c in t))
        if length < 1e-8:
            # No usable UV gradient on any triangle that shares this vertex. Any direction
            # across the normal will do: nothing here carries a normal map, and a zero
            # tangent would make the shader's basis singular.
            t = [1.0, 0.0, 0.0] if abs(normal[0]) < 0.9 else [0.0, 0.0, 1.0]
            dot = sum(t[i] * normal[i] for i in range(3))
            t = [t[i] - normal[i] * dot for i in range(3)]
            length = math.sqrt(sum(c * c for c in t)) or 1.0
        t = [c / length for c in t]
        cross = [normal[1] * t[2] - normal[2] * t[1],
                 normal[2] * t[0] - normal[0] * t[2],
                 normal[0] * t[1] - normal[1] * t[0]]
        sign = -1.0 if sum(cross[i] * bitangents[index][i] for i in range(3)) < 0.0 else 1.0
        packed += struct.pack("<3f3f4f2f", *position, *normal, *t, sign, *uv)
    return packed, len(vertices), indices, low, high


# The version of the missile table, and the one the reader checks. Bumped whenever the row
# below gains or loses a field, because a reader that defaulted one would draw a missile of
# the wrong size or throw it at the wrong speed and look like it had worked.
MISSILE_VERSION = 1


def cook_missiles(out_dir, texcook, threads):
    """The things a blow throws: 18 meshes and their sheets, cooked like any other model.

    They sit beside the effect sheets and the sounds in cooked/showing for the reason that
    directory exists at all: a missile belongs to a blow and not to a map, so cooking it per
    world would write the same bone into Lorencia and into Noria.

    **The point of this pass is that a missile stops being a special case.** It was two .obj
    files parsed at run time into raw unlit triangles, which is right for a burning rock and
    wrong for a bone lying on the grass. What comes out here is a .mum exactly as the town's
    models and the wardrobe's items are -- version 3, 48-byte vertices, one part per MU mesh,
    one material each -- so game code loads it with `parseCookedMesh` and draws it as an
    ordinary lit `gfx::Drawable`, with no parser and no second material model.

    What each part's `blend` becomes is the one decision here. `additive` sets the glow flag
    (bit 1), which is MU's BlendMesh and already means exactly this to the renderer: out of
    the shadow, out of the prepass, out of the shade, and added to the frame in the
    transparent pass. Everything else -- `opaque`, and Poison01's `flat` -- is an ordinary
    lit material. So Arrow01 is one mesh with a lit shaft and an additive flame on it, drawn
    in one submission, and no fourth blend flag was invented for it.

    Two departures, both marked, because MU's effect geometry has no entry in the material
    library and never will:

      * `invention`: roughness 1.0 and metal 0 on every part, stated in the factors as
        docs/conventions.md requires of a material with no ORM map. A thrown bone is not
        metal and MU painted no grain for it.
      * `invention`: every part is two-sided. MU's effect meshes are open shells and single
        cards -- a flame is four cards through a ball -- and a card seen from its back face
        would vanish. Figures are drawn two-sided here for the same reason.

    The `poses` some missiles carry (Ice01's six, Poison01's eleven, Storm01's nineteen --
    keys of an action baked as separate .obj, see ObjCommands.cs) are NOT cooked: nothing
    reads them yet and cooking them would write 34 meshes the game never asks for. The
    `mesh` each row names is the one cooked, and this note is what says the rest are owed.

    The .mup format ("MU2 projectiles"; .mum is a mesh and .mus is the showing), version 1,
    little-endian:

        'MU2P', u32 version, u32 missiles
        missiles: u16 len + name ("Bone01"), u16 len + .mum path relative to assets/,
                  f32 scale, f32 frames, f32 lift, f32 muzzle[3], f32 sideways,
                  f32 sidewaysSpread, u32 parts,
                  parts: u16 len + .ktx path relative to assets/, u8 additive

    `lift`, `muzzle`, `sideways` and `sidewaysSpread` are left in MU's units exactly as
    index.json states them, and that is deliberate rather than an omission: they are read
    together with MU's own per-frame arithmetic -- a lift of 150 a frame at 25 Hz -- and
    halving the conversion between the file and the flight is how a number gets divided by a
    hundred twice. The GEOMETRY is in metres because a mesh has no arithmetic left to do.
    """
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)
    missiles = index.get("missiles") or {}
    worlds = index.get("worlds") or []

    # docs/conventions.md: never hard-code 100. A missile belongs to no world, so the value
    # is taken from the worlds that do state one and they are required to agree -- the day a
    # map arrives on another scale, this says so instead of silently halving a rock.
    scales = sorted({float(one["units_per_tile"]) for one in worlds if "units_per_tile" in one})
    if len(scales) != 1:
        print(f"cook: the worlds disagree about units_per_tile ({scales}), and a missile "
              f"belongs to none of them", file=sys.stderr)
        return 2
    per_tile = scales[0]

    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    mesh_dir = os.path.join(out_dir, "meshes")
    os.makedirs(mesh_dir, exist_ok=True)
    started = time.time()

    # --- the sheets, deduplicated by their own bytes as every other cooked texture is ----
    # Same `effect_<base>_albedo_<digest>` stem cook_showing uses, on purpose: a sheet that
    # is both a named effect and a missile's part is then one file cooked once, by the path
    # its digest gives it.
    jobs = []
    sheets = {}
    missing = []
    raw_in = 0
    for name in sorted(missiles):
        for part in missiles[name]["parts"]:
            relative = part["sheet"]
            if relative in sheets:
                continue
            source = os.path.join(ASSETS, relative)
            if not os.path.exists(source):
                missing.append(relative)
                continue
            with open(source, "rb") as handle:
                data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            base = safe(os.path.splitext(os.path.basename(relative))[0])
            ktx_path = os.path.join(out_dir, "textures", f"effect_{base}_albedo_{digest}.ktx")
            if not any(job[3] == ktx_path for job in jobs):
                raw_in += len(data)
                # -1: an effect sheet is blended and not tested, so there is no coverage to
                # hold down its mip chain. cook_showing's note says why at length.
                jobs.append(("albedo", -1.0, source, ktx_path))
            sheets[relative] = os.path.relpath(ktx_path, ASSETS)

    if jobs:
        job_file = os.path.join(out_dir, "jobs.txt")
        with open(job_file, "w") as handle:
            for role, cutout, source, target in jobs:
                handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")
        command = [texcook, job_file]
        if threads:
            command += ["--threads", str(threads)]
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode

    # --- the meshes, one .mum each ------------------------------------------------------
    rows = []
    triangles = 0
    mesh_bytes = 0
    gone = []
    for name in sorted(missiles):
        row = missiles[name]
        source = os.path.join(ASSETS, row["mesh"])
        if not os.path.exists(source):
            gone.append(row["mesh"])
            continue
        at, normals, uvs, groups = read_obj(source)
        wanted = {part["group"]: part for part in row["parts"]}
        faces = {}
        for group, group_faces in groups:
            faces.setdefault(group, []).extend(group_faces)
        unknown = sorted(set(faces) - set(wanted))
        if unknown:
            print(f"cook: {name}'s mesh carries groups index.json does not name "
                  f"({', '.join(unknown)}), and a part with no sheet cannot be drawn",
                  file=sys.stderr)
            return 2

        vertices = bytearray()
        indices = bytearray()
        parts = []
        materials = bytearray()
        vertex_count = 0
        low = [1e30] * 3
        high = [-1e30] * 3
        for part in row["parts"]:
            group = part["group"]
            if group not in faces:
                print(f"cook: {name} names a part `{group}` its mesh has no faces for",
                      file=sys.stderr)
                return 2
            packed, count, part_indices, part_low, part_high = obj_part(
                at, normals, uvs, faces[group], per_tile)
            first_index = len(indices) // 4
            for value in part_indices:
                indices += struct.pack("<I", vertex_count + value)
            vertices += packed
            parts.append((first_index, len(indices) // 4 - first_index, len(parts)))
            vertex_count += count
            for axis in range(3):
                low[axis] = min(low[axis], part_low[axis])
                high[axis] = max(high[axis], part_high[axis])

            additive = part.get("blend") == "additive"
            # Bit 0 two-sided (invention, see above), bit 1 the glow: MU's BlendMesh, which
            # is what `additive` already means everywhere else in this engine.
            flags = 1 | (2 if additive else 0)
            materials += struct.pack("<fB", -1.0, flags)
            materials += write_string(group)
            materials += write_string(sheets.get(part["sheet"], ""))
            for _ in range(3):      # normal, orm, emissive: MU paints none of the three
                materials += write_string("")
            # invention: rough and not metal, stated in the factors because there is no ORM
            # map to multiply. docs/conventions.md's rule for a material with no map.
            materials += struct.pack("<2f", 1.0, 0.0)

        header = struct.pack("<4sIIIII", b"MU2M", 3, vertex_count, len(indices) // 4,
                             len(parts), len(row["parts"]))
        header += struct.pack("<3f3f", *low, *high)
        body = bytes(vertices) + bytes(indices)
        for part in parts:
            body += struct.pack("<III", *part)
        body += bytes(materials)

        out_path = os.path.join(mesh_dir, f"{safe(name)}.mum")
        blob = header + body
        # Idempotent by what it holds: the same .obj and the same row cook to the same
        # bytes, so a second run leaves the file's mtime alone and nothing downstream
        # rebuilds. Same argument as the digest in a texture's name.
        if not (os.path.exists(out_path) and open(out_path, "rb").read() == blob):
            with open(out_path, "wb") as handle:
                handle.write(blob)
        triangles += len(indices) // 12
        mesh_bytes += len(blob)

        part_rows = b"".join(write_string(sheets.get(part["sheet"], "")) +
                             struct.pack("<B", 1 if part.get("blend") == "additive" else 0)
                             for part in row["parts"])
        muzzle = [float(v) for v in (row.get("muzzle") or [0.0, 0.0, 0.0])]
        rows.append(write_string(name) +
                    write_string(os.path.relpath(out_path, ASSETS)) +
                    struct.pack("<3f3f2fI", float(row.get("scale", 1.0)),
                                float(row.get("frames", 0.0)), float(row.get("lift", 0.0)),
                                *muzzle, float(row.get("sideways", 0.0)),
                                float(row.get("sideways_spread", 0.0)), len(row["parts"])) +
                    part_rows)

    blob = struct.pack("<4sII", b"MU2P", MISSILE_VERSION, len(rows)) + b"".join(rows)
    path = os.path.join(out_dir, "missiles.mup")
    with open(path, "wb") as handle:
        handle.write(blob)

    if missing:
        print(f"cook: {len(missing)} missile sheets named and not on disk: "
              f"{', '.join(missing[:4])}")
    if gone:
        print(f"cook: {len(gone)} missile meshes named and not on disk: {', '.join(gone[:4])}")
    ktx = sum(os.path.getsize(os.path.join(out_dir, "textures", f))
              for f in os.listdir(os.path.join(out_dir, "textures")))
    print(f"cook: {len(rows)} missiles, {triangles} triangles in "
          f"{mesh_bytes / 1e3:.1f} KB of .mum; {len(jobs)} sheets, "
          f"{raw_in / 1e6:.1f} MB of png -> {ktx / 1e6:.1f} MB of .ktx with mips in this "
          f"directory")
    print(f"cook: missiles -> {os.path.relpath(path, ROOT)} ({len(blob)} bytes), "
          f"{time.time() - started:.1f} s")
    return 0


def cook_figures(world, out_dir, texcook, threads, with_monsters=True, only=None):
    """Every figure the world reaches: its textures, its meshes, its clips and a manifest.

    `with_monsters` false cooks the world's people without its breeds -- Noria's townsfolk
    stood before its monsters were judged -- and a breed with no figure is held back by the
    game rather than walked invisible (Play::open)."""
    models, characters, monsters, standalone, placements, index = figure_set(world)
    # A breed already in the world's table stays in it. `only` and --no-monsters rebuilt the
    # table from the breeds named alone, so cooking a world's townsfolk or its next breed threw
    # out every breed approved before: Noria stood with none of its eight and Devias with none
    # of its four (2026-09-30). Only a whole cook, which names none, starts from the index.
    if only is not None or not with_monsters:
        try:
            with open(os.path.join(out_dir, figures_table(world))) as handle:
                cooked = {one["name"] for one in json.load(handle).get("monsters", [])}
        except (OSError, ValueError):
            cooked = set()
        only = set(only or ()) | cooked
    # `only`: just these breeds, by name -- a world's monsters brought in one at a time, each
    # judged before the next (the user, 2026-09-28: "don't cook all at the same time, we need
    # to approve"). The rest are left out exactly as --no-monsters leaves them all.
    everything = dict(models)
    keep = [one for one in monsters if only is not None and one["name"] in only]
    if only is not None:
        monsters = [one for one in monsters if one["name"] not in only]
        with_monsters = False
    if not with_monsters:
        for one in monsters:
            for part in (one["mesh"], one.get("right_hand"), one.get("left_hand")):
                if part and not any(part in c["parts"] or part in (c["right_hand"], c["left_hand"])
                                    for c in characters) \
                        and not any(part == s["mesh"] for s in standalone):
                    models.pop(part, None)
        monsters = keep
        for one in keep:
            for part in (one["mesh"], one.get("right_hand"), one.get("left_hand")):
                if part:
                    models[part] = everything[part]
    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "meshes"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "clips"), exist_ok=True)
    raw_dir = os.path.join(out_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)

    # --- the images, deduplicated by their own bytes and their role, as the town's are ---
    jobs = []
    manifest = {}
    seen = {}
    done = 0
    for name, path in sorted(models.items()):
        document, binary = read_glb(path)
        for image, role, cutout in roles_of(document):
            data = image_bytes(document, binary, image)
            if data is None:
                uri = document["images"][image].get("uri")
                if uri is None:
                    continue
                with open(os.path.join(os.path.dirname(path), uri), "rb") as handle:
                    data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            key = f"{digest}-{role}"
            source_key = f"{name}#{image}:{role}"
            if key in seen:
                manifest[source_key] = seen[key]
                continue
            label = document["images"][image].get("name") or f"image{image}"
            stem = f"{safe(name)}_{safe(label)}_{role}_{digest}"
            ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
            relative = os.path.relpath(ktx_path, ASSETS)
            seen[key] = relative
            manifest[source_key] = relative
            # The stem carries the sha1 of the source bytes AND the role, so a .ktx that is
            # there is a .ktx of these bytes in this role: existence is the up-to-date test,
            # and the figures' 132 images are forty minutes of BC7 to redo for nothing.
            if os.path.exists(ktx_path):
                done += 1
                continue
            raw_path = os.path.join(raw_dir, stem + ".bin")
            with open(raw_path, "wb") as handle:
                handle.write(data)
            jobs.append((role, cutout, raw_path, ktx_path))

    job_file = os.path.join(out_dir, "jobs.txt")
    with open(job_file, "w") as handle:
        for role, cutout, source, target in jobs:
            handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")
    print(f"cook: {len(models)} figure models, {len(jobs)} images to compress, "
          f"{done} already cooked, after deduplicating "
          f"{len(manifest) - len(jobs) - done} repeats")
    if jobs:
        command = [texcook, job_file] + (["--threads", str(threads)] if threads else [])
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode
    # Merged, not written over: the textures are shared by every world's figures, and a
    # world's cook naming only its own would leave another's meshes pointing at nothing listed.
    try:
        with open(os.path.join(out_dir, "textures.json")) as handle:
            merged = json.load(handle).get("textures", {})
    except (OSError, ValueError):
        merged = {}
    merged.update(manifest)
    with open(os.path.join(out_dir, "textures.json"), "w") as handle:
        json.dump({"version": 1, "textures": merged}, handle, indent=1, sort_keys=True)

    # --- the meshes, and the clip library each one names ------------------------------
    # Two rows share one glb -- EliteBullFighter01 is BullFighter01 at 1.15 with a spear --
    # so the table is keyed by MESH and the first row that names a hidden primitive wins.
    # Keyed by row instead, the elite's own null would overwrite the Bull Fighter's 0 and
    # both would be drawn wearing two axes.
    hidden_of = {}
    # A row that names the part it puts away (`hidden_part`, a glb material name) gets a cut of
    # its own, `<mesh>~<part>`, with the base's clips: the Hell Hound drops the bare head `fur`
    # and keeps the helm the plain Hound's `hidden` part is. Done first, so neither the
    # hidden_mesh table nor the whole copies below see it on the base mesh.
    parted_of = {}
    for one in monsters:
        if one.get("hidden_part"):
            variant = one["mesh"] + "~" + one["hidden_part"]
            parted_of[variant] = one["mesh"]
            models[variant] = models[one["mesh"]]
            hidden_of[variant] = one["hidden_part"]
            one["mesh"] = variant
    for one in monsters:
        if one.get("hidden_mesh") is not None and one["mesh"] not in parted_of:
            hidden_of.setdefault(one["mesh"], one["hidden_mesh"])
    # And a row on that mesh which keeps the hidden part gets a whole copy of its own. The
    # Elite keeps the crest the plain Bull Fighter puts away, which is the whole of what tells
    # them apart, so one .mum cannot serve both. Its clips are the base mesh's: see clip_of.
    whole_of = {}
    for one in monsters:
        if one["mesh"] in hidden_of and one.get("hidden_mesh") is None:
            variant = one["mesh"] + "~whole"
            whole_of[variant] = one["mesh"]
            models[variant] = models[one["mesh"]]
            one["mesh"] = variant
    whole_of.update(parted_of)                        # the cuts take their base's clips too
    mesh_table = {}
    clip_of = {}
    libraries = {}
    triangles = vertices = 0
    for name, path in sorted(models.items()):
        out_path = os.path.join(out_dir, "meshes", name + ".mum")
        scroll = next((one.get("scroll", 0.0) for one in monsters if one["mesh"] == name), 0.0)
        tris, verts, _size, bones = cook_mesh(name, path, out_path, manifest,
                                              hidden_of.get(name), scroll_per_second=scroll)
        triangles += tris
        vertices += verts
        mesh_table[name] = {"mesh": os.path.relpath(out_path, ASSETS), "bones": bones,
                            "triangles": tris}
        document, _binary = read_glb(path)
        if name in whole_of:
            continue                                  # its clips are its base mesh's
        if document.get("animations"):
            clip_of[name] = name                      # its own, embedded: a monster
            libraries[name] = path
        else:
            # The library the model itself names, and the .glb twin of the .res it names:
            # the .res is Godot's AnimationLibrary and this engine reads the .glb beside it.
            named = (document.get("extras") or {}).get("actions")
            if not named:
                continue
            library = os.path.normpath(os.path.join(os.path.dirname(path), named))
            if library.endswith(".res"):
                library = library[: -len(".res")] + ".glb"
            if not os.path.exists(library):
                print(f"cook: {name} names {os.path.relpath(library, ASSETS)} and it is not "
                      f"here -- tools/sync.sh copies it", file=sys.stderr)
                continue
            stem = os.path.basename(library)[: -len(".actions.glb")]
            libraries[stem] = library
            clip_of[name] = stem

    for variant, base in whole_of.items():
        if base in clip_of:
            clip_of[variant] = clip_of[base]

    monster_actions = index.get("monster_actions", {})
    holds = set(index.get("monster_holds", []))
    # `monster_holds` is [6], and 6 is a MONSTER slot. The player library has deaths of its
    # own -- `actions` names 232 "Die 1" and 233 "Die 2" -- and no table in index.json covers
    # them, so they are read off the label. A death that wraps is a corpse half getting up as
    # it falls, which the sprint listed among the three things most likely to go wrong and
    # then shipped in the data.
    player_holds = {int(slot) for slot, label in index.get("actions", {}).items()
                    if label.startswith("Die")}
    travel_of = {}
    for one in monsters:
        travel_of.setdefault(one["mesh"], one.get("action_travel", {}))
    clip_table = {}
    clip_count = frame_count = clip_bytes = closed_total = spread_total = 0
    for stem, path in sorted(libraries.items()):
        document, binary = read_glb(path)
        embedded = stem in models
        names = monster_actions if embedded else index.get("actions", {})
        clips, frames, size, closed, spread = cook_clips(
            document, binary, os.path.join(out_dir, "clips", stem + ".muc"),
            names if embedded or path.endswith("player.actions.glb") else {},
            travel_of.get(stem, {}) if embedded else index.get("action_travel", {}),
            holds if embedded else player_holds,
            cloth=path.endswith("player.actions.glb"))
        clip_table[stem] = os.path.relpath(os.path.join(out_dir, "clips", stem + ".muc"),
                                           ASSETS)
        clip_count += clips
        frame_count += frames
        clip_bytes += size
        closed_total += closed
        spread_total += spread

    # What each item IS, for the engine to place it by: a crossbow is slung one way, a bow's
    # quiver another, a shield a third, and everything else takes the sword's arrangement.
    # MU2's `Model.OnBack` switches on exactly these two fields.
    items = {}
    for one in index.get("objects", []):
        glb = one.get("glb", "")
        if not glb:
            continue
        name = os.path.splitext(os.path.basename(glb))[0]
        if name in models:
            items[name] = {"kind": one.get("kind", ""), "stance": one.get("stance", "")}

    out = {"version": 1, "world": world, "meshes": mesh_table, "clips": clip_table,
           "clip_of": clip_of, "characters": characters, "monsters": monsters,
           "standalone": standalone, "placements": placements, "items": items,
           "monster_actions": monster_actions,
           # MU marks a safe zone per tile -- the 0x0001 bit of the attribute grid, 3.9% of
           # Lorencia -- and the client reads it off the figure's own tile to decide whether
           # the weapon is in the hand or on the back. The rect the world carries is the
           # gate's, and is not that bit; the engine reads the grid.
           "safe_attribute": 1}
    # The character screen's busts (NewFace01..03) are cooked into this table by cook_one and
    # are no world's figures, so a figure cook that rebuilt the table from the world alone
    # took them out -- the create window stood with no face (2026-09-28). Carried over.
    try:
        with open(os.path.join(out_dir, figures_table(world))) as handle:
            before = json.load(handle)
    except (OSError, ValueError):
        before = {}
    busts = {one["name"] for one in index.get("objects", []) if one.get("kind") == "lobby"}
    for name in sorted(busts & set(before.get("meshes", {}))):
        out["meshes"].setdefault(name, before["meshes"][name])
        if name in before.get("clips", {}):
            out["clips"].setdefault(name, before["clips"][name])
        if name in before.get("clip_of", {}):
            out["clip_of"].setdefault(name, before["clip_of"][name])
        if not any(one["name"] == name for one in out["standalone"]):
            out["standalone"].append({"name": name, "mesh": name})
    out["standalone"].sort(key=lambda one: one["name"])
    with open(os.path.join(out_dir, figures_table(world)), "w") as handle:
        json.dump(out, handle, indent=1, sort_keys=True)

    cooked = sum(os.path.getsize(os.path.join(out_dir, "textures", f))
                 for f in os.listdir(os.path.join(out_dir, "textures")))
    print(f"cook: {len(mesh_table)} meshes, {triangles} triangles, {vertices} vertices; "
          f"{len(clip_table)} clip libraries, {clip_count} clips, {frame_count} frames, "
          f"{closed_total} of them closed with a key MU left off, "
          f"{spread_total} whose body closed and whose cloth drift was spread over the cycle, "
          f"{clip_bytes / 1e6:.2f} MB of .muc; {cooked / 1e6:.1f} MB of .ktx")
    print(f"cook: {len(characters)} characters, {len(monsters)} breeds spawned here, "
          f"{len(standalone)} standalone, {len(placements)} figures placed in the town")
    return 0


def cook_wardrobe(out_dir, texcook, threads):
    """Every suit of armour and every weapon in index.json, for the viewer to walk.

    Separate from `cook_figures` and deliberately so. The figures cook takes what the world
    REACHES -- sprint 3's rule, and the reason Lorencia loads in five seconds rather than
    fifty -- and the game loads every mesh in that manifest at startup. The wardrobe is the
    opposite case: ninety item files nobody is wearing, which exist so that a person can look
    at them one at a time. Putting them in figures.json would put all ninety into the hands
    of a game that wants five, so they get their own manifest, their own directory, and a
    loader the game never calls.

    No clips are cooked here. Armour is worn on the player rig and animates out of
    `player.muc`, which the figures cook already wrote; a bow and a crossbow carry a rig of
    their own and MU draws them at its first key, which their vertices already are.
    """
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)

    # The pieces of a suit, in the order a figure wears them. MU names them by piece and
    # suffix -- HelmMale10, ArmorMale10 -- and the suffix is the suit.
    pieces = ("Helm", "Armor", "Pant", "Glove", "Boot")
    rows = {}
    for one in index.get("objects", []):
        glb = one.get("glb", "")
        if not glb or one.get("kind") not in ("armor", "weapon", "shield"):
            continue
        name = os.path.splitext(os.path.basename(glb))[0]
        full = os.path.join(ASSETS, glb)
        if not os.path.exists(full):
            print(f"cook: {name} is in index.json and not on disk; tools/sync.sh copies it",
                  file=sys.stderr)
            continue
        rows[name] = (one, full)

    models = {name: full for name, (one, full) in rows.items()}

    # --- the suits ---------------------------------------------------------------------
    suits = {}
    for name, (one, _full) in rows.items():
        if one.get("kind") != "armor":
            continue
        for piece in pieces:
            if name.startswith(piece):
                suits.setdefault(name[len(piece):], {})[piece] = one
                break
    sets = []
    for suffix, worn in sorted(suits.items()):
        torso = worn.get("Armor")
        if torso is None:
            continue
        # The suit's name off its torso's: MU labels the pieces "Bone Helm", "Bone Armor",
        # and the word they share is what the suit is called.
        label = torso.get("label", suffix)
        if label.endswith(" Armor"):
            label = label[: -len(" Armor")]
        classes = (torso.get("stats") or {}).get("classes") or []
        # An open helm is worn over the wearer's own head, not in place of it: MuMain's
        # SetCharacterScale draws the bare class head under the few it names. index.json
        # carries it as the helm row's `keeps_head`; see HelmMale01's keeps_head_from in MU2.
        sets.append({"name": suffix, "label": label,
                     "parts": [piece + suffix for piece in pieces if piece in worn],
                     "classes": classes,
                     "defense": (torso.get("stats") or {}).get("defense", 0),
                     "keeps_head": bool((worn.get("Helm") or {}).get("keeps_head"))})

    # --- the arms ----------------------------------------------------------------------
    arms = []
    for name, (one, _full) in sorted(rows.items()):
        if one.get("kind") not in ("weapon", "shield"):
            continue
        stats = one.get("stats") or {}
        arms.append({"name": name, "label": one.get("label", name),
                     "mesh": name, "kind": one["kind"],
                     "stance": one.get("stance", ""),
                     "two_handed": bool(one.get("two_handed")),
                     "classes": stats.get("classes") or []})

    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "meshes"), exist_ok=True)
    raw_dir = os.path.join(out_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)

    # --- the images, deduplicated by their own bytes and their role, as the figures' are --
    jobs = []
    manifest = {}
    seen = {}
    done = 0
    for name, path in sorted(models.items()):
        document, binary = read_glb(path)
        for image, role, cutout in roles_of(document):
            data = image_bytes(document, binary, image)
            if data is None:
                uri = document["images"][image].get("uri")
                if uri is None:
                    continue
                with open(os.path.join(os.path.dirname(path), uri), "rb") as handle:
                    data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            key = f"{digest}-{role}"
            source_key = f"{name}#{image}:{role}"
            if key in seen:
                manifest[source_key] = seen[key]
                continue
            label = document["images"][image].get("name") or f"image{image}"
            stem = f"{safe(name)}_{safe(label)}_{role}_{digest}"
            ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
            relative = os.path.relpath(ktx_path, ASSETS)
            seen[key] = relative
            manifest[source_key] = relative
            if os.path.exists(ktx_path):
                done += 1
                continue
            raw_path = os.path.join(raw_dir, stem + ".bin")
            with open(raw_path, "wb") as handle:
                handle.write(data)
            jobs.append((role, cutout, raw_path, ktx_path))

    job_file = os.path.join(out_dir, "jobs.txt")
    with open(job_file, "w") as handle:
        for role, cutout, source, target in jobs:
            handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")
    print(f"cook: {len(models)} wardrobe models, {len(jobs)} images to compress, "
          f"{done} already cooked")
    if jobs:
        command = [texcook, job_file] + (["--threads", str(threads)] if threads else [])
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode
    with open(os.path.join(out_dir, "textures.json"), "w") as handle:
        json.dump({"version": 1, "world": "wardrobe", "textures": manifest}, handle, indent=1,
                  sort_keys=True)

    # --- the meshes --------------------------------------------------------------------
    mesh_table = {}
    triangles = vertices = 0
    for name, path in sorted(models.items()):
        out_path = os.path.join(out_dir, "meshes", name + ".mum")
        tris, verts, _size, bones = cook_mesh(name, path, out_path, manifest)
        # A weapon with a rig of its own -- a bow's or a crossbow's string -- carries its one
        # clip in the same glb, as a world object does; baked beside the mesh for the figure
        # that holds it to play (crowd.cpp, Figure::poseHeld). Never a staff, whose 60 bones
        # are the player's and whose clips are the player's library.
        if 0 < bones <= 16:
            cook_world_clip(name, path, os.path.join(out_dir, "clips", name + ".muc"))
        triangles += tris
        vertices += verts
        mesh_table[name] = {"mesh": os.path.relpath(out_path, ASSETS), "bones": bones,
                            "triangles": tris}

    out = {"version": 1, "meshes": mesh_table, "sets": sets, "arms": arms}
    with open(os.path.join(out_dir, "wardrobe.json"), "w") as handle:
        json.dump(out, handle, indent=1, sort_keys=True)
    cooked = sum(os.path.getsize(os.path.join(out_dir, "textures", f))
                 for f in os.listdir(os.path.join(out_dir, "textures")))
    print(f"cook: {len(mesh_table)} meshes, {triangles} triangles, {vertices} vertices; "
          f"{len(sets)} suits, {len(arms)} arms; {cooked / 1e6:.1f} MB of .ktx")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", default="lorencia")
    parser.add_argument("--out", default=os.path.join(ASSETS, "cooked"))
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--texcook", default=os.path.join(ROOT, "build", "texcook"))
    parser.add_argument("--only", choices=("textures", "ground", "meshes", "placements",
                                           "figures", "tables", "showing", "missiles",
                                           "wardrobe", "all"),
                        default="all")
    parser.add_argument("--monsters", default=None,
                        help="with --only figures: just these breeds, comma-separated")
    parser.add_argument("--no-monsters", action="store_true",
                        help="with --only figures: the world's people without its breeds")
    parser.add_argument("--chunk", type=int, default=32,
                        help="a chunk's side in tiles; 32 gives Lorencia an 8x8 grid")
    args = parser.parse_args()

    if args.only == "meshes":
        return cook_meshes(args.world, os.path.join(args.out, args.world))

    if args.only == "placements":
        return cook_placements(args.world, os.path.join(args.out, args.world), args.chunk)

    if args.only == "tables":
        return cook_tables(args.world, os.path.join(args.out, args.world))

    if args.only in ("showing", "missiles"):
        if not os.path.exists(args.texcook):
            print(f"cook: {args.texcook} is not built. cmake --build build --target texcook",
                  file=sys.stderr)
            return 2
        # Beside the figures and not under a world, for the figures' own reason.
        showing_dir = os.path.join(args.out, "showing")
        if args.only == "missiles":
            return cook_missiles(showing_dir, args.texcook, args.threads)
        # The missiles ride with `showing` rather than standing alone in `all`: they write
        # into the same directory, they share its sheet naming, and a blow whose effect was
        # recooked without the thing it throws is half a blow. So the two are one answer to
        # "cook what a blow shows", and `--only missiles` is the narrower cut for a session
        # that touched only the meshes.
        result = cook_showing(showing_dir, args.texcook, args.threads)
        return result or cook_missiles(showing_dir, args.texcook, args.threads)

    if args.only == "wardrobe":
        if not os.path.exists(args.texcook):
            print(f"cook: {args.texcook} is not built. cmake --build build --target texcook",
                  file=sys.stderr)
            return 2
        # Its own directory and NOT part of `all`: this is ninety item files the game never
        # loads, cooked so the viewer can show them. Run it by hand when the wardrobe
        # changes, which is when index.json gains an item.
        return cook_wardrobe(os.path.join(args.out, "wardrobe"), args.texcook, args.threads)

    if args.only == "figures":
        if not os.path.exists(args.texcook):
            print(f"cook: {args.texcook} is not built. cmake --build build --target texcook",
                  file=sys.stderr)
            return 2
        # The figures are cooked into their own directory and not into the world's: a body,
        # a suit of armour and a monster are reached by several maps, and cooking them per
        # world would write the same Bull Fighter into every one of them.
        only = (set(filter(None, args.monsters.split(",")))
                if args.monsters is not None else None)
        return cook_figures(args.world, os.path.join(args.out, "figures"), args.texcook,
                            args.threads, with_monsters=not args.no_monsters, only=only)

    if not os.path.exists(args.texcook):
        print(f"cook: {args.texcook} is not built. cmake --build build --target texcook",
              file=sys.stderr)
        return 2

    out_dir = os.path.join(args.out, args.world)
    raw_dir = os.path.join(out_dir, "raw")
    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    os.makedirs(raw_dir, exist_ok=True)

    started = time.time()
    # `ground` is the land's sheets alone: a world stood up bare, before any of its objects are
    # judged -- Noria's first step (2026-09-28). The manifest it writes names the ground and
    # nothing else, so a later `textures` cook rewrites it whole with the models added.
    if args.only == "ground":
        jobs, manifest, drawn, missing, models = [], {}, 0, [], 0
    else:
        jobs, manifest, drawn, missing, models = collect(args.world, out_dir, raw_dir)
    jobs += ground_jobs(args.world, out_dir, manifest)
    print(f"cook: {args.world}, {drawn} of {models} models have a .glb "
          f"({len(missing)} have none: {', '.join(missing[:6])}"
          f"{'...' if len(missing) > 6 else ''})")
    print(f"cook: {len(jobs)} images to compress, after deduplicating "
          f"{len(manifest) - len(jobs)} repeats")

    job_file = os.path.join(out_dir, "jobs.txt")
    with open(job_file, "w") as handle:
        for role, cutout, source, target in jobs:
            handle.write(f"{role}\t{cutout}\t{source}\t{target}\n")

    command = [args.texcook, job_file]
    if args.threads:
        command += ["--threads", str(args.threads)]
    result = subprocess.run(command)

    manifest_path = os.path.join(out_dir, "textures.json")
    with open(manifest_path, "w") as handle:
        json.dump({"version": 1, "world": args.world, "textures": manifest}, handle, indent=1,
                  sort_keys=True)

    cooked = sum(os.path.getsize(os.path.join(out_dir, "textures", f))
                 for f in os.listdir(os.path.join(out_dir, "textures")))
    print(f"cook: {cooked / 1e6:.1f} MB of .ktx, manifest {os.path.relpath(manifest_path, ROOT)}, "
          f"{time.time() - started:.1f} s")
    if args.only == "all":
        mesh_result = cook_meshes(args.world, out_dir)
        if mesh_result:
            return mesh_result
        cook_placements(args.world, out_dir, args.chunk)
        cook_tables(args.world, out_dir)
        cook_showing(os.path.join(args.out, "showing"), args.texcook, args.threads)
        cook_missiles(os.path.join(args.out, "showing"), args.texcook, args.threads)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
