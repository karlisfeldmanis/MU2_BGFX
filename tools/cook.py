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

    tools/cook.py --world lorencia [--only textures|meshes|placements|all] [--chunk 32]

The .mut format ("MU2 town"), version 1, little-endian:

    'MU2T', u32 version, u32 models, u32 chunks, u32 instances,
    u32 size (tiles a side), u32 chunkTiles, f32 metresPerTile
    models:    u16 name length and bytes, u16 mesh path length and bytes,
               f32 bounds min[3], f32 bounds max[3], u32 instances of it
    chunks:    f32 bounds min[3], f32 bounds max[3], u32 firstInstance, u32 instanceCount,
               u16 column, u16 row
    instances: f32 position[3], f32 yaw, f32 pitch, f32 roll, f32 scale,
               u16 model, u16 flags, u8 light[3], u8 spare   -- 36 bytes, and 36 is what a
               C++ struct of those fields is too, with no implicit padding anywhere

`flags` bit 0 says the placement was laid on the terrain rather than used as the map stores
it; the rest are spare. `light` is MU's own baked terrain light at the placement's tile,
which is what stops the town standing unlit on lit ground.

Instances are sorted by chunk and then by model, so one chunk that survives a cull is a run
of instances and each model inside it is a contiguous sub-run: an instanced draw is a range,
found without a sort and without a map lookup.

The .mum format, version 1, little-endian throughout:

    'MU2M', u32 version, u32 vertices, u32 indices, u32 parts, u32 materials
    f32 bounds min[3], f32 bounds max[3]        (the centre and radius are derived at load)
    vertices:  position[3] normal[3] tangent[4] uv[2], 48 bytes, content::Vertex exactly
    indices:   u32 each
    parts:     u32 firstIndex, u32 indexCount, u32 material
    materials: f32 cutout (-1 for none), u8 twoSided, then five strings --
               name, albedo, normal, orm, emissive -- each u16 length and its bytes,
               the four texture strings being paths under assets/ or empty for none

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
import hashlib
import json
import math
import os
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")


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


def collect(world, out_dir, raw_dir):
    """Every image the world's models reach for, written out ready to compress."""
    world_dir = os.path.join(ASSETS, "world", world)
    with open(os.path.join(world_dir, f"{world}.json")) as handle:
        map_data = json.load(handle)

    models = sorted({one["model"] for one in map_data["objects"]})
    jobs = []
    manifest = {}
    seen = {}
    drawn = 0
    missing = []

    for model in models:
        path = os.path.join(world_dir, model, f"{model}.glb")
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
                with open(os.path.join(world_dir, model, uri), "rb") as handle:
                    data = handle.read()
            digest = hashlib.sha1(data).hexdigest()[:16]
            key = f"{digest}-{role}"
            source_key = f"{model}#{index}:{role}"
            if key in seen:
                manifest[source_key] = seen[key]
                continue

            name = document["images"][index].get("name") or f"image{index}"
            stem = f"{safe(model)}_{safe(name)}_{role}_{digest}"
            raw_path = os.path.join(raw_dir, stem + ".bin")
            with open(raw_path, "wb") as handle:
                handle.write(data)
            ktx_path = os.path.join(out_dir, "textures", stem + ".ktx")
            jobs.append((role, cutout, raw_path, ktx_path))
            relative = os.path.relpath(ktx_path, ASSETS)
            seen[key] = relative
            manifest[source_key] = relative

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
                jobs.append((role, -1.0, source, ktx_path))
                relative = os.path.relpath(ktx_path, ASSETS)
                seen[key] = relative
                manifest[f"ground/{name}:{role}"] = relative
    return jobs


COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4),
             5126: ("f", 4)}
COMPONENTS_OF = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


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


def cook_mesh(model, path, out_path, textures):
    """One .glb into one .mum. Returns (triangles, vertices, bytes)."""
    document, binary = read_glb(path)

    for node in document.get("nodes", []):
        if "mesh" in node and not identity(node):
            raise ValueError(f"{model}: a mesh node carries a transform, and this cook bakes "
                             f"none -- see content/mesh.cpp, which flattens the same way")

    vertices = bytearray()
    indices = bytearray()
    parts = []
    vertex_count = 0
    low = [1e30] * 3
    high = [-1e30] * 3

    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            attributes = primitive["attributes"]
            if "POSITION" not in attributes:
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

            base = vertex_count
            for i in range(count):
                p = positions[i]
                for axis in range(3):
                    low[axis] = min(low[axis], p[axis])
                    high[axis] = max(high[axis], p[axis])
                vertices += struct.pack("<3f3f4f2f", *p[:3], *normals[i][:3], *tangents[i][:4],
                                        *uvs[i][:2])
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
        if material.get("alphaMode") == "MASK":
            cutout = float(material.get("alphaCutoff", 0.5))
        maps = {"albedo": "", "normal": "", "orm": "", "emissive": ""}
        for role in maps:
            maps[role] = textures.get(f"{model}#{image_for(document, material, role)}:{role}", "")
        materials += struct.pack("<fB", cutout, 1 if material.get("doubleSided") else 0)
        materials += write_string(material.get("name", "material"))
        for role in ("albedo", "normal", "orm", "emissive"):
            materials += write_string(maps[role])
    # The fallback a primitive with no material of its own draws with, as content/mesh.cpp
    # appends it.
    materials += struct.pack("<fB", -1.0, 0) + write_string("none")
    for _ in range(4):
        materials += write_string("")

    header = struct.pack("<4sIIIII", b"MU2M", 1, vertex_count, len(indices) // 4, len(parts),
                         len(material_list) + 1)
    header += struct.pack("<3f3f", *low, *high)
    body = bytes(vertices) + bytes(indices)
    for part in parts:
        body += struct.pack("<III", *part)
    body += bytes(materials)

    with open(out_path, "wb") as handle:
        handle.write(header + body)
    return len(indices) // 12, vertex_count, len(header) + len(body)


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

    mesh_dir = os.path.join(out_dir, "meshes")
    os.makedirs(mesh_dir, exist_ok=True)

    triangles = vertices = cooked = source = models = 0
    for model in sorted({one["model"] for one in map_data["objects"]}):
        path = os.path.join(world_dir, model, f"{model}.glb")
        if not os.path.exists(path):
            continue
        tris, verts, size = cook_mesh(model, path, os.path.join(mesh_dir, model + ".mum"),
                                      textures)
        triangles += tris
        vertices += verts
        cooked += size
        source += os.path.getsize(path)
        models += 1
    print(f"cook: {models} meshes, {triangles} triangles, {vertices} vertices, "
          f"{cooked / 1e6:.1f} MB of .mum out of {source / 1e6:.1f} MB of .glb")
    return 0


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


# MU lays these on the terrain instead of using the height it stores for them. Types 20 to 27
# are Lorencia's grass and fern block, and this is MU2's own deviation (`World.cs:3672`), not
# our arithmetic being corrected -- re-measured on this copy of the placement list before it
# was copied here: of 999 grass placements, 57% carry a stored pitch or roll, and the stored
# height runs from 6.60 m *below* the terrain to 4.29 m above it, with Grass01's median a
# clear metre over the ground. Reproduced faithfully that is a town with tufts of grass at
# head height. Height comes from the terrain, pitch and roll are dropped, yaw is kept: a tuft
# has a direction it faces and no business leaning.
GROUNDED_TYPES = range(20, 28)


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

    grid_w, grid_h, _channels, heights = read_png(os.path.join(world_dir, map_data["height"]))
    light_w, light_h, light_channels, light = read_png(
        os.path.join(world_dir, map_data["light"]))
    if grid_w != size or light_w != size:
        raise ValueError(f"{world}: a grid is {grid_w} wide and the world says {size}")

    def terrain(column, row):
        x = min(max(int(column), 0), grid_w - 1)
        y = min(max(int(row), 0), grid_h - 1)
        return heights[y * grid_w + x] * height_factor / per_tile

    def lit(column, row):
        x = min(max(int(column), 0), light_w - 1)
        y = min(max(int(row), 0), light_h - 1)
        at = (y * light_w + x) * light_channels
        if light_channels >= 3:
            return light[at], light[at + 1], light[at + 2]
        return light[at], light[at], light[at]

    chunks_across = (size + chunk_tiles - 1) // chunk_tiles
    buckets = {}
    dropped_hidden = dropped_model = grounded = outside = 0

    for one in map_data["objects"]:
        if one.get("hidden"):
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

        # MU's angles are degrees about its own z-up axes: its z is our yaw, its x our pitch.
        pitch, _unused, yaw = (math.radians(a) for a in one["angle"])
        roll = 0.0
        flags = 0
        if one["type"] in GROUNDED_TYPES:
            y = terrain(column, row)
            pitch = 0.0
            roll = 0.0
            flags |= 1
            grounded += 1

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
             lit(column, row), flags))
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

    header = struct.pack("<4sIIIIIIf", b"MU2T", 1, len(models), len(chunk_records), written,
                         size, chunk_tiles, metres_per_tile)
    body = bytearray()
    for name, mesh_path, bounds, count in models:
        body += write_string(name) + write_string(mesh_path)
        body += struct.pack("<6fI", *bounds, count)
    for low, high, first, count, cx, cy in chunk_records:
        body += struct.pack("<6fII2H", *low, *high, first, count, cx, cy)
    body += bytes(instances)

    out_path = os.path.join(out_dir, f"{world}.mut")
    with open(out_path, "wb") as handle:
        handle.write(header + bytes(body))

    print(f"cook: {written} placements in {len(chunk_records)} chunks of {chunk_tiles} tiles, "
          f"{len(models)} models, {grounded} laid on the terrain, "
          f"{dropped_hidden} hidden and {dropped_model} without a mesh dropped, "
          f"{outside} standing off the grid, "
          f"{(len(header) + len(body)) / 1000:.0f} kB")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", default="lorencia")
    parser.add_argument("--out", default=os.path.join(ASSETS, "cooked"))
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--texcook", default=os.path.join(ROOT, "build", "texcook"))
    parser.add_argument("--only", choices=("textures", "meshes", "placements", "all"),
                        default="all")
    parser.add_argument("--chunk", type=int, default=32,
                        help="a chunk's side in tiles; 32 gives Lorencia an 8x8 grid")
    args = parser.parse_args()

    if args.only == "meshes":
        return cook_meshes(args.world, os.path.join(args.out, args.world))

    if args.only == "placements":
        return cook_placements(args.world, os.path.join(args.out, args.world), args.chunk)

    if not os.path.exists(args.texcook):
        print(f"cook: {args.texcook} is not built. cmake --build build --target texcook",
              file=sys.stderr)
        return 2

    out_dir = os.path.join(args.out, args.world)
    raw_dir = os.path.join(out_dir, "raw")
    os.makedirs(os.path.join(out_dir, "textures"), exist_ok=True)
    os.makedirs(raw_dir, exist_ok=True)

    started = time.time()
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
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
