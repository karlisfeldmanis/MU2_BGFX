"""Writes the item out as glTF, with a PBR material the engine can read directly.

    Blender --background --python pipeline/export_gltf.py -- \
        workshop/Sword01_low.blend workshop/Sword01_albedo.png workshop/Sword01.glb

This is the hand-off. Everything upstream is Blender's business — welding, unwrapping,
baking, and later the sculpt — and everything downstream is the engine's. glTF is the seam
because it is the one format that carries a metallic-roughness material as a *definition*
rather than as a screenshot of one renderer's node graph: base colour, metallic, roughness,
normal and occlusion, in the arrangement every modern engine already expects.

Packed into a single .glb rather than a .gltf beside its images. An item is one thing and
should be one file; a folder of loose maps is a folder that arrives half-copied.

The file is written here, by hand, rather than by ``bpy.ops.export_scene.gltf``. That is
not preference. The stock exporter does not return under ``--background`` on this machine:
the step hangs with no output and no error, and a pipeline stage that never finishes is
worse than one that fails, because it takes the whole run down with it and says nothing
about why. Everything else in MU2 is a headless Blender call, so an exporter that only
works interactively is an exporter this pipeline cannot use.

Writing it directly turns out to be the smaller thing anyway. glTF's binary container is a
twelve-byte header, a chunk of JSON and a chunk of bytes, and what has to go in it is fully
known here: one mesh, one material, two UV sets. That is the whole of what follows. It also
means no ``bpy.ops`` at all beyond opening the file — no operator context to get wrong, no
addon to be enabled, and no selection state deciding what gets exported.

The material is deliberately plain. Metallic and roughness are left at values that say
"nothing has been measured yet" rather than at a guess dressed up as an answer — the maps
that will fill them come from the sculpt and from MU's own material classifier, and until
those exist an honest placeholder is worth more than a plausible invention.
"""

import json
import math
import os
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Vector

#: The layout everything is baked and textured through. See clean_lowpoly.
BAKE_UV = "bake"

#: MU's own coordinates, carried along. See the note in `mesh_attributes`.
ORIGINAL_UV = "mu_original"

#: Where clean_lowpoly recorded the bone each vertex belongs to.
BONE_ATTRIBUTE = "mu_bone"

#: The clock every MU animation is stated against, in frames a second.
#:
#: Not a frame rate. The client runs as fast as it can and divides: CalcFPS sets
#: FPS_ANIMATION_FACTOR to REFERENCE_FPS / FPS, and PlayAnimation advances the key counter
#: by `Speed * FPS_ANIMATION_FACTOR` — so a clip takes the same wall-clock time at 30 fps as
#: at 144, and the number that fixes it is this one. ZzzAI.cpp, REFERENCE_FPS = 25.
REFERENCE_FPS = 25.0

#: How many keys a second one play speed is worth, then, and what a clip is exported at.
#:
#: This was 8.0 keys a second flat, and the comment beside it said so: "A guess, and flagged
#: as one. MU keeps a play speed per action in the client rather than in the .bmd, so the
#: file this is built from does not say. Correct it against the real client before anyone
#: calls the timing right." It has now been corrected against the real client.
#:
#: The rule is `keys_per_second = play_speed * REFERENCE_FPS`, and the play speed is a
#: per-action number the client assigns at load. Where it comes from depends on the kind of
#: thing:
#:
#:   townspeople   ZzzOpenData.cpp's OpenNpc, which walks every action of every NPC model
#:                 and sets 0.25 — a flat rate, overridden afterwards for a handful of
#:                 late-season NPCs, none of them Lorencia's. Hence the default below.
#:   the player    the same file, action by action, from PLAYER_STOP_MALE at 0.28 down to
#:                 the bows at 0.22. Passed in with --speeds; see players/rig/actions.json.
#:   world objects CreateObject's o->Velocity, which is the same number under another name:
#:                 MoveObject hands it to PlayAnimation exactly where a character's play
#:                 speed goes. 0.16 by default, 0.3 for a street lamp, a candle and a sign.
#:                 Passed in with --play-speed.
#:
#: At 0.25 a six-key idle is 0.96 s and a thirty-key one 4.8 s. At the old 8.0 they were
#: 0.75 and 3.75 — every townsperson in the town ran 28% fast.
DEFAULT_PLAY_SPEED = 0.25

#: What MU keeps when it alpha-tests a sheet: any texel whose alpha is greater than this.
#:
#: `BeginOpengl`'s `glAlphaFunc(GL_GREATER, 0.25f)`, and nothing in the world-object path
#: changes it — `SetAlphaFuncRef` exists for a couple of late-season set pieces and Lorencia
#: and Noria are not among them. The test itself is reached on `alpha < 0.99f ||
#: texture->Components == 4`, which is to say on any sheet shipped as an .OZT.
ALPHA_TEST_REF = 0.25

#: MU works in units where a tile is 100 and a character 182, so a hundred of them is a
#: metre. glTF is metres by definition and every engine that reads one assumes so;
#: exporting at MU's scale gives a 75-metre sword, which makes nonsense of every physical
#: quantity downstream — light falloff, camera distance, and the texel density this whole
#: exercise is measured in.
MU_UNITS_PER_METRE = 100.0

# glTF's enumerations, spelled out because the file is written by hand here and a bare
# 5126 in the middle of an accessor is unreadable a month later.
FLOAT = 5126
UNSIGNED_SHORT = 5123
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
TRIANGLES = 4
LINEAR = 9729
LINEAR_MIPMAP_LINEAR = 9987
REPEAT = 10497

#: Vertices are merged when every one of their attributes matches to this many decimals.
#: Six is far below what a float can hold and far above what any of these quantities
#: mean — it exists so that two corners that Blender computed separately but identically
#: do not become two vertices.
PRECISION = 6


class Binary:
    """The .bin chunk, and the views and accessors that index into it.

    glTF keeps its numbers in one flat buffer and describes them from the JSON side: a
    buffer view is a range of bytes, an accessor says how to read that range as typed
    elements. Nothing here is interleaved — each attribute gets its own view — because a
    68-triangle item has nothing to gain from a packed vertex layout and the unpacked form
    is the one that can be read in a hex dump when something looks wrong.
    """

    def __init__(self) -> None:
        self.blob = bytearray()
        self.views: list[dict] = []
        self.accessors: list[dict] = []

    def view(self, payload: bytes, target: int | None = None) -> int:
        # Accessors are required to start on a boundary of their component size. Four
        # covers every type used here, so every view simply starts on a multiple of four.
        while len(self.blob) % 4:
            self.blob.append(0)

        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target

        self.blob.extend(payload)
        self.views.append(view)
        return len(self.views) - 1

    def attribute(self, values: list[tuple], kind: str) -> int:
        """A float accessor — VEC2, VEC3 or VEC4 — over one vertex attribute."""
        width = {"VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        flat = [component for value in values for component in value]

        self.accessors.append({
            "bufferView": self.view(struct.pack(f"<{len(flat)}f", *flat), ARRAY_BUFFER),
            "componentType": FLOAT,
            "count": len(values),
            "type": kind,

            # Required on POSITION and optional elsewhere. It is what a viewer frames and
            # culls by, and it is the first thing to read when a model arrives invisible
            # or a thousand times too large, so every accessor here carries it.
            "min": [min(value[i] for value in values) for i in range(width)],
            "max": [max(value[i] for value in values) for i in range(width)],
        })

        return len(self.accessors) - 1

    def joints(self, values: list[tuple]) -> int:
        """The joint indices, as unsigned shorts — glTF does not accept floats here."""
        flat = [component for value in values for component in value]

        self.accessors.append({
            "bufferView": self.view(struct.pack(f"<{len(flat)}H", *flat), ARRAY_BUFFER),
            "componentType": UNSIGNED_SHORT,
            "count": len(values),
            "type": "VEC4",
        })

        return len(self.accessors) - 1

    def indices(self, values: list[int]) -> int:
        """The triangle list, in the narrowest integer that holds it."""
        narrow = max(values) < 65536
        payload = struct.pack(f"<{len(values)}{'H' if narrow else 'I'}", *values)

        self.accessors.append({
            "bufferView": self.view(payload, ELEMENT_ARRAY_BUFFER),
            "componentType": UNSIGNED_SHORT if narrow else UNSIGNED_INT,
            "count": len(values),
            "type": "SCALAR",
        })

        return len(self.accessors) - 1


def evaluated(obj):
    """The mesh as it will be drawn: modifiers applied, in world space.

    Two corrections in one. Modifiers are applied by asking the dependency graph for the
    evaluated object rather than by running the apply operator, which would edit the .blend
    the pipeline is meant to leave alone. World space is taken from the object's own
    matrix, which is where the OBJ import's axis conversion ends up — reading the mesh data
    alone would export a sword lying on its side.
    """
    graph = bpy.context.evaluated_depsgraph_get()
    mesh = obj.evaluated_get(graph).to_mesh()
    mesh.calc_loop_triangles()
    return mesh


def corner_normals(mesh) -> list:
    """Per-corner normals, whichever way this Blender exposes them.

    Blender 4.1 removed `MeshLoop.normal` in favour of a `corner_normals` collection. Both
    spellings are handled because the pipeline is not worth pinning to one Blender, and a
    missing normal is not a small difference here: it is the whole of how the item answers
    light once it leaves the flat-shaded world MU drew it in.
    """
    if hasattr(mesh, "corner_normals"):
        return [corner.vector.copy() for corner in mesh.corner_normals]

    return [loop.normal.copy() for loop in mesh.loops]


def tangents(mesh) -> list[tuple] | None:
    """Per-corner tangents, or None when this mesh cannot carry them.

    A normal map is meaningless without the tangent frame it was baked in, and the frame
    the engine computes for itself is not necessarily the frame the bake used. So they are
    exported rather than left to be re-derived.

    Blender computes them with mikktspace, which handles triangles and quads and refuses
    ngons outright. A refusal is not fatal — there is no normal map yet — so it is reported
    and the export continues without them.
    """
    try:
        mesh.calc_tangents(uvmap=BAKE_UV)
    except (RuntimeError, TypeError) as failure:
        print(f"  tangents   not exported: {failure}")
        return None

    return [(loop.tangent.copy(), loop.bitangent_sign) for loop in mesh.loops]


def mesh_attributes(obj, mesh, tiled: bool = False,
                    upright: "set[int] | None" = None,
                    repeat: "dict[int, float] | None" = None,
                    parts: "dict[int, str] | None" = None) -> tuple[dict, dict, dict]:
    """Splits MU's triangle soup into glTF vertices: one per distinct corner.

    A glTF vertex is a single tuple of attributes, so a corner where the UV or the normal
    breaks has to become its own vertex — the opposite of what clean_lowpoly did upstream,
    and correct for the same reason. Welding was about the *surface*, so it could be
    unwrapped and baked across; splitting is about the *stream*, which has no way to say
    "this position, with two different UVs". Only corners that genuinely differ are split,
    which is why this deduplicates rather than emitting three vertices per triangle.

    Two coordinate conventions change on the way out, and both are silent failures if
    missed. Blender is Z-up and glTF is Y-up, so (x, y, z) becomes (x, z, -y) — a rotation,
    which leaves handedness and therefore the cross products in the tangent frame alone.
    And glTF's texture origin is the top-left corner where Blender's is the bottom-left, so
    V is flipped. That flip reverses the direction V increases in, and the bitangent with
    it, which is why the exported sign is the negative of Blender's.
    """
    if BAKE_UV not in mesh.uv_layers:
        raise SystemExit(f"error: no '{BAKE_UV}' UV layer — run clean_lowpoly first")

    to_world = obj.matrix_world

    # Normals transform by the inverse transpose, not by the matrix: under any non-uniform
    # scale the plain matrix bends them off the surface.
    to_world_normal = to_world.to_3x3().inverted_safe().transposed()

    scale = 1.0 / MU_UNITS_PER_METRE
    normals = corner_normals(mesh)
    frames = tangents(mesh)

    bake = mesh.uv_layers[BAKE_UV].data
    original = mesh.uv_layers[ORIGINAL_UV].data if ORIGINAL_UV in mesh.uv_layers else None

    # Which layer leads, and it is not a preference.
    #
    # An item is baked, so TEXCOORD_0 is the baked layout because that is where every map it
    # will ever have was written. A tiled asset is not baked at all: it ships MU's sheets and
    # samples them at MU's own coordinates, so those have to be the ones the material reads.
    # The other set still travels, because it costs two floats and answers questions later.
    lead, follow = (original, bake) if tiled and original is not None else (bake, original)

    # Which bone owns each vertex, put there by clean_lowpoly. Absent on an item, which
    # has no skeleton and needs none.
    binding = mesh.attributes.get(BONE_ATTRIBUTE)

    positions: list[tuple] = []
    out_normals: list[tuple] = []
    out_tangents: list[tuple] = []
    uv0: list[tuple] = []
    uv1: list[tuple] = []
    joints: list[tuple] = []
    weights: list[tuple] = []
    groups: dict[int, list[int]] = {}
    seen: dict[tuple, int] = {}

    upright = upright or set()
    repeat = repeat or {}

    for triangle in mesh.loop_triangles:
        # A BMD is several meshes, one per sheet, and clean_lowpoly keeps them as material
        # slots on one object. Untiled that is ignored, because everything ends up in one
        # baked atlas and one material; tiled it is the whole structure of the file.
        # Which primitive this triangle belongs in.
        #
        # The material slot, normally, because a BMD is several meshes and clean_lowpoly
        # keeps them as slots on one object. But a baked item is often one mesh with two
        # substances painted on it — Axe01 is a single sheet holding a wooden haft and a
        # steel head — and the slot cannot tell those apart. The island can, which is what
        # the asset's island list has been assigning materials to all along, and `parts`
        # carries that assignment in as one name per face.
        indices = groups.setdefault(
            (parts or {}).get(triangle.polygon_index, triangle.material_index), [])

        # Whether this slot's normals are replaced with straight up. See the note on the
        # foliage material: a bush is crossed cards standing on edge, every true normal
        # points sideways, and a sun most of the way up the sky lights a sideways surface at
        # almost nothing — which is why every clump of grass in the town was a black
        # silhouette on lit ground. The normal a leaf card wants is the canopy's, not the
        # quad's; the quad is a drafting convenience.
        standing = triangle.material_index in upright

        # How many times this slot's sheet crosses the surface it is on.
        #
        # MU's own UVs put exactly one copy of a sheet across a face, which was right when a
        # sheet was a 128-pixel painting of a whole wall. A photoscanned surface is a
        # photograph of about two metres of real stone, and stretched across a whole wall it
        # reads as a few enormous blotches rather than as masonry — which is what "the colours
        # are wrong" turns out to be most of the time, because at that size a stone is a
        # patch of colour rather than a stone.
        #
        # Applied to the coordinates rather than to a sampler so that it survives glTF: the
        # format has no texture transform every engine reads, and multiplying UVs is
        # something every one of them does.
        over = repeat.get(triangle.material_index, 1.0)

        # The corners of this triangle, gathered so they can be written back to front.
        #
        # MU is a DirectX-era format and winds its front faces clockwise. glTF defines the
        # front face as counter-clockwise, and nothing between the two was reversing it — so
        # every triangle in every model shipped with its winding opposite to its own normal.
        # Measured rather than deduced: 70 of 70 triangles in HouseWall02, 67 of 67 in
        # Sword01, and the terrain the only thing in the build that was right, because
        # ground.py builds its own triangles and this was found and fixed there once already.
        #
        # It survived because every material is double-sided, so nothing ever vanished. What
        # happened instead is subtler and worse: a double-sided material flips the shading
        # normal on a back face, and by this winding the face you look at *is* the back face,
        # so every surface in the town was lit by a normal pointing into the wall it belongs
        # to. That is a wall which takes no sun, and therefore a wall on which a shadow
        # changes nothing — the character's shadow ran up to the tavern and stopped dead at
        # it. It is also, most likely, a fair part of why buildings kept reading as blackish
        # whatever was done to their materials.
        corners = []

        for corner in triangle.loops:
            point = to_world @ mesh.vertices[mesh.loops[corner].vertex_index].co
            normal = (Vector((0.0, 0.0, 1.0)) if standing
                      else (to_world_normal @ normals[corner]).normalized())

            vertex = (
                (point.x * scale, point.z * scale, -point.y * scale),
                (normal.x, normal.z, -normal.y),
                (lead[corner].uv[0] * over, (1.0 - lead[corner].uv[1]) * over),
                (follow[corner].uv[0], 1.0 - follow[corner].uv[1]) if follow else None,
            )

            if frames is not None:
                tangent, sign = frames[corner]
                tangent = (to_world_normal @ tangent).normalized()
                frame = (tangent.x, tangent.z, -tangent.y, -sign)
            else:
                frame = None

            # The bone is part of the key. Two corners that agree on everything else but
            # belong to different bones are different vertices — merging them here would
            # undo, at the last possible moment, exactly what the bone-aware weld protected.
            bone = binding.data[mesh.loops[corner].vertex_index].value if binding else -1

            key = (bone,) + tuple(
                tuple(round(component, PRECISION) for component in part)
                for part in (*vertex, frame)
                if part is not None)

            index = seen.get(key)
            if index is None:
                index = len(positions)
                seen[key] = index

                positions.append(vertex[0])
                out_normals.append(vertex[1])
                uv0.append(vertex[2])

                if vertex[3] is not None:
                    uv1.append(vertex[3])

                if frame is not None:
                    out_tangents.append(frame)

                if binding:
                    # Rigid: one joint, all the weight. glTF wants four of each regardless,
                    # so the other three are joint 0 at weight 0 and contribute nothing.
                    joints.append((max(0, bone), 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))

            corners.append(index)

        indices.extend(reversed(corners))

    return (
        {
            "POSITION": positions,
            "NORMAL": out_normals,
            "TANGENT": out_tangents,
            "TEXCOORD_0": uv0,
            "TEXCOORD_1": uv1,
            "JOINTS_0": joints,
            "WEIGHTS_0": weights,
        },
        groups,
        {"corners": len(mesh.loops), "triangles": len(mesh.loop_triangles)},
    )


def to_gltf_point(v, scale: float) -> tuple:
    """MU's Z-up, in metres, as glTF's Y-up."""
    return (v[0] * scale, v[2] * scale, -v[1] * scale)


def to_gltf_rotation(q) -> tuple:
    """The same axis change, applied to a quaternion.

    The mapping (x, y, z) -> (x, z, -y) is a rotation of -90 degrees about X. Rotating a
    rotation means rotating its axis and leaving its angle alone, so the quaternion's
    vector part goes through the identical permutation and the scalar part is untouched.
    Writing the Euler angles out and rebuilding them the other side would reach the same
    place by a longer road with a convention to get wrong on the way.
    """
    return (q[0], q[2], -q[1], q[3])


def compose(parent: tuple, child: tuple) -> tuple:
    """Puts a child's local transform into its parent's space."""
    (pt, pr), (ct, cr) = parent, child
    return (add(pt, rotate(pr, ct)), multiply(pr, cr))


def multiply(a, b) -> tuple:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        (aw * bx) + (ax * bw) + (ay * bz) - (az * by),
        (aw * by) - (ax * bz) + (ay * bw) + (az * bx),
        (aw * bz) + (ax * by) - (ay * bx) + (az * bw),
        (aw * bw) - (ax * bx) - (ay * by) - (az * bz))


def rotate(q, v) -> tuple:
    x, y, z, w = q
    vx, vy, vz = v

    tx, ty, tz = 2 * ((y * vz) - (z * vy)), 2 * ((z * vx) - (x * vz)), 2 * ((x * vy) - (y * vx))
    return (
        vx + (w * tx) + ((y * tz) - (z * ty)),
        vy + (w * ty) + ((z * tx) - (x * tz)),
        vz + (w * tz) + ((x * ty) - (y * tx)))


def add(a, b) -> tuple:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def invert(transform: tuple) -> tuple:
    """The transform that undoes another. Rotation and translation only, so no scale to
    worry about."""
    t, r = transform
    back = (-r[0], -r[1], -r[2], r[3])
    return (rotate(back, (-t[0], -t[1], -t[2])), back)


def rebind(attributes: dict, binds: list[dict], bones: list[dict]) -> int:
    """Moves the mesh out of the part's bind pose and into the rig's.

    A .glb describes one skeleton, and until this existed ours described two. The node
    hierarchy carried the player rig's rest pose, because that is the rig the animations
    were written against and every part has to be able to stand up on its own. The inverse
    bind matrices carried the part's own bind, because that is the space its vertices are
    in. MU authors a worn part in a slightly different arm pose than the player's rest, so
    the two disagreed by nineteen centimetres at the hand — the glove's own vertices sat
    1.1 cm from the hand bone it was skinned with and 17.6 cm from the one written beside
    it in the file.

    Skinning survived that, because an animation writes an absolute local transform for
    every bone and the mismatched inverse binds carried the vertices into it. What did not
    survive is anything that reads a bone's transform and expects to find the mesh there —
    which is exactly what hanging a weapon off a hand does, and why the grip sat against
    the edge of the fist rather than inside it.

    So the vertices are moved instead. Each one is taken out of its own bone's bind pose
    and put into the same bone's rest pose on the rig, after which the part's bind is no
    longer referred to by anything and the inverse binds can come from the rig like the
    nodes do. MU's skinning is rigid — one bone per vertex, weight one — so this is exact
    rather than an approximation over a blend.
    """
    by_name = {bone["name"]: bone for bone in bones}
    moves = {}

    for index, bone in enumerate(binds):
        target = by_name.get(bone["name"])
        if target is not None:
            moves[index] = compose(target["global"], invert(bone["global"]))

    positions = attributes.get("POSITION") or []
    normals = attributes.get("NORMAL") or []
    tangents = attributes.get("TANGENT") or []
    joints = attributes.get("JOINTS_0") or []

    shifted = 0

    for index, joint in enumerate(joints):
        move = moves.get(joint[0])
        if move is None:
            continue

        offset, turn = move
        positions[index] = add(offset, rotate(turn, positions[index]))

        if index < len(normals):
            normals[index] = rotate(turn, normals[index])

        if index < len(tangents):
            spun = rotate(turn, tangents[index][:3])
            tangents[index] = (spun[0], spun[1], spun[2], tangents[index][3])

        shifted += 1

    return shifted


def inverse_bind(transform) -> list[float]:
    """The 4x4 that takes a vertex from model space into a bone's own space.

    glTF skins a vertex by moving it into each joint's space, applying that joint's current
    pose, and moving it back — so it needs the "into" half stated once per joint, as a
    matrix. That is simply the inverse of the joint's rest transform, and because a rest
    transform here is a rotation and a translation with no scale, the inverse is the
    conjugate rotation applied to the negated translation.

    Column-major, which is what glTF stores and the opposite of how it reads on the page.
    """
    translation, rotation = transform

    x, y, z, w = rotation
    conjugate = (-x, -y, -z, w)
    offset = rotate(conjugate, (-translation[0], -translation[1], -translation[2]))

    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z

    # The conjugate's rotation matrix is the transpose of the rotation's, which is why the
    # off-diagonal signs read the other way round from the usual formula.
    return [
        1 - 2 * (yy + zz), 2 * (xy - wz),     2 * (xz + wy),     0.0,
        2 * (xy + wz),     1 - 2 * (xx + zz), 2 * (yz - wx),     0.0,
        2 * (xz - wy),     2 * (yz + wx),     1 - 2 * (xx + yy), 0.0,
        offset[0],         offset[1],         offset[2],         1.0,
    ]


def skeleton_of(rig: dict) -> list[dict]:
    """The bones, with their rest transform in glTF space and their rest transform resolved.

    Local for the nodes, because a glTF node is placed relative to its parent. Global for
    the inverse bind matrices, because those are stated in model space. Both come from the
    same walk, parents before children — which MU's ordering guarantees, since a bone's
    parent index is always lower than its own.
    """
    scale = 1.0 / MU_UNITS_PER_METRE
    bones = []

    for bone in rig["skeleton"]:
        local = (to_gltf_point(bone["t"], scale), to_gltf_rotation(bone["r"]))
        parent = bone["parent"]

        glob = local if parent < 0 else compose(bones[parent]["global"], local)
        bones.append({
            "name": bone["name"] or f"bone{bone['index']}",
            "parent": parent,
            "local": local,
            "global": glob,
        })

    return bones



def is_glow(document: dict, primitive: dict) -> bool:
    """Whether a primitive is a self-luminous submesh rather than a surface."""
    material = document["materials"][primitive["material"]]
    pbr = material.get("pbrMetallicRoughness", {})

    return (material.get("alphaMode") == "BLEND"
            and "emissiveTexture" in material
            and pbr.get("baseColorFactor") == [0.0, 0.0, 0.0, 1.0])


def write_skin(
        document: dict, binary: Binary, bones: list[dict], mesh_node: int,
        binds: list[dict] | None = None) -> int:
    """The joint nodes, and the skin that binds the mesh to them.

    Two skeletons meet here, doing two different jobs, and conflating them is what broke
    this four times over.

    The *joints* are the rig's — player.bmd's. That is the skeleton the clips are written
    against, and it is where a bone rests when the clip says nothing about it. MU leaves
    ten of sixty bones untracked in a typical idle, so that fallback is not an edge case.

    The *bind poses* are the part's own. A bind undoes the pose the geometry was baked in,
    and that is the part's rest whatever is animating it afterwards.

    They are joined by **name**, which is the whole mechanism and the thing MU2 was
    missing. The old client gets this for free — a part's skin binds slots to names and
    Godot resolves them against whatever skeleton the mesh points at — but glTF has no such
    indirection: JOINTS_0 indexes straight into skin.joints. So the resolution has to
    happen here, at export, or not at all.
    """
    first = len(document["nodes"])

    for index, bone in enumerate(bones):
        translation, rotation = bone["local"]
        node = {"name": bone["name"]}

        if any(abs(v) > 1e-9 for v in translation):
            node["translation"] = list(translation)

        if abs(rotation[3] - 1.0) > 1e-9 or any(abs(v) > 1e-9 for v in rotation[:3]):
            node["rotation"] = list(rotation)

        children = [first + i for i, b in enumerate(bones) if b["parent"] == index]
        if children:
            node["children"] = children

        document["nodes"].append(node)

    # Matched by name. Unique, non-empty names are what makes that safe, and muextract
    # guarantees both — an unnamed slot arrives as dummy_<n>. A joint the part has no bone
    # for keeps the rig's own rest, which is inert: nothing is bound to it.
    by_name = {bone["name"]: bone for bone in (binds or bones)}
    matrices = [
        value
        for bone in bones
        for value in inverse_bind(by_name.get(bone["name"], bone)["global"])]
    payload = struct.pack(f"<{len(matrices)}f", *matrices)

    binary.accessors.append({
        "bufferView": binary.view(payload),
        "componentType": FLOAT,
        "count": len(bones),
        "type": "MAT4",
    })

    document["skins"] = [{
        "joints": [first + i for i in range(len(bones))],
        "inverseBindMatrices": len(binary.accessors) - 1,
    }]

    document["nodes"][mesh_node]["skin"] = 0

    # The roots join the scene beside the mesh. A joint that is in no scene is a joint some
    # readers will drop, taking the pose with it.
    document["scenes"][0]["nodes"].extend(
        first + i for i, bone in enumerate(bones) if bone["parent"] < 0)

    return first


def conjugate(q) -> tuple:
    return (-q[0], -q[1], -q[2], q[3])


def material_culls(name: str) -> bool | None:
    """Whether a named material asks to be culled, or nothing if it does not say."""
    if not name:
        return None

    definition = (Path(__file__).resolve().parent.parent
                  / "source" / "materials" / f"{name}.json")

    if not definition.exists():
        return None

    try:
        said = json.loads(definition.read_text()).get("two_sided")
    except (OSError, ValueError):
        return None

    return None if said is None else not bool(said)


def read_speeds(path: Path | None) -> tuple[dict[int, float], set[int]]:
    """MU's per-action play speeds, and which actions stop rather than wrap.

    Two answers out of one file because they are two columns of one table — the client
    assigns both at load, action by action, and neither is in the .bmd. Read here rather
    than at each call site so the rig's clips are written the same way whether they are
    going into a model or into a library beside it.

    An absent table is not a failure. It leaves every action at the caller's default play
    speed and every clip closed, which is what a townsperson wants and what every worn part
    got before there was a table at all.
    """
    if not path or not path.exists():
        return {}, set()

    listed = json.loads(path.read_text()).get("actions", {})

    speeds = {
        int(index): float(entry["play_speed"])
        for index, entry in listed.items()
        if isinstance(entry, dict) and "play_speed" in entry
    }

    holds = {
        int(index)
        for index, entry in listed.items()
        if isinstance(entry, dict) and entry.get("hold_at_end")
    }

    return speeds, holds


def read_closes(path: Path | None) -> set[int]:
    """The locked actions this project closes anyway, by action number.

    A third column of the same table and the second departure from MU in the rig's clips,
    asked for by name one action at a time: `close_loop: true`. A locked action is wrapped
    a key early on the understanding that the animator made its last key a copy of its
    first; where he did not, MU pops at the wrap and so did this. PLAYER_STOP_FEMALE is the
    case it was added for — the elf's idle hands back 43 degrees of hair and cloth in a
    single frame, forever, while she stands in town. A closed one gets the free action's
    closing key, so the pop becomes one more interval of movement. See actions.json.
    """
    if not path or not path.exists():
        return set()

    listed = json.loads(path.read_text()).get("actions", {})

    return {
        int(index)
        for index, entry in listed.items()
        if isinstance(entry, dict) and entry.get("close_loop")
    }


def returns(values: list, share: float = 0.15) -> bool:
    """Whether a track's last authored key is already back at its first.

    Measured against the track's own motion rather than against a fixed number, because the
    two tracks this has to separate differ by a factor of a thousand in the units they move
    in. A bone that rotates comes back to a quaternion equal to the byte; a bone that swings
    across the model comes back 2.7 mm from where it began, having travelled 168 mm between
    every other pair of keys. The first is exactly closed and the second is closed for every
    purpose the eye has, and an absolute epsilon that admits one rejects the other.

    So: the gap between the ends, against the median step inside the clip. Median rather than
    mean because a pendulum spends its turning points barely moving, and a handful of
    near-zero steps drag an average down toward the very gap being tested.

    A track that never moves answers no, which is right — it has nothing to stall.
    """
    if len(values) < 3:
        return False

    def apart(a, b) -> float:
        return max(abs(one - two) for one, two in zip(a, b))

    steps = sorted(apart(values[i], values[i + 1]) for i in range(len(values) - 1))
    middle = steps[len(steps) // 2]

    return middle > 0.0 and apart(values[0], values[-1]) <= middle * share


def geared_track(values: list, gear: int, rotation: bool) -> list:
    """One looping track spread over `gear` cycles of the action, closed on its first key.

    Sampled between its keys the way the client blends them — NLERP on the short side for a
    rotation, a straight line for a position — so a bone keyed 45 degrees apart turns a
    steady 45/gear per interval instead of holding and lurching.
    """
    count = len(values)
    out = []

    for step in range(count * gear):
        at, part = divmod(step, gear)
        one, two = values[at], values[(at + 1) % count]
        s = part / gear

        if rotation:
            if sum(a * b for a, b in zip(one, two)) < 0.0:
                two = [-b for b in two]

            mixed = [a + (b - a) * s for a, b in zip(one, two)]
            length = math.sqrt(sum(c * c for c in mixed)) or 1.0
            out.append([c / length for c in mixed])
        else:
            out.append([a + (b - a) * s for a, b in zip(one, two)])

    return out + [values[0]]


def write_animations(
        document: dict, binary: Binary, rig: dict, first_joint: int, joints: int,
        speeds: dict[int, float] | None = None,
        play_speed: float = DEFAULT_PLAY_SPEED,
        holds: set[int] | None = None,
        unstall: bool = False,
        geared: tuple[set[int], int] | None = None,
        closes: set[int] | None = None) -> list[str]:
    """One glTF animation per exported MU action, at the rate the client plays it.

    A channel per bone per property, which is more channels than a hand-authored clip would
    have — MU keys every bone in every action whether it moves or not. The ones that do not
    move are dropped here rather than shipped as sixty flat curves apiece.

    `speeds` is MU's per-action play speed where the model has one, and `play_speed` is the
    single rate for every action where it does not. Both are play speeds rather than key
    rates, because that is the unit the client states them in; see DEFAULT_PLAY_SPEED.

    `holds` names the actions that stop on their last key instead of wrapping — a monster's
    death, and in the client nothing else shared by every model. It is here rather than left
    to the viewer because it changes what is *written*: see the closing key below.

    `unstall` is the one departure from MU in this function, and it is asked for by name per
    model. See `evenly` below for what it does and Carriage01.json for why. It is off unless
    a model asks for it because it takes a track off the action's clock, and a skeleton is
    only in step while every bone in it is on the same one.
    """
    scale = 1.0 / MU_UNITS_PER_METRE
    speeds = speeds or {}
    holds = holds or set()
    closes = closes or set()
    document["animations"] = []
    names = []

    for animation in rig.get("animations", []):
        keys = animation["keys"]
        if keys < 2:
            continue

        # A cycle is as long as its key count, not one key short of it.
        #
        # MU's clips do not end where they begin. Of the 284 actions in player.bmd exactly
        # one has a last key equal to its first; the rest run off the end and are meant to be
        # carried back round. The client does it in one line — BMDUtils.cpp interpolates
        # frame0 to `(frame0 + 1) % numKeys`, so while the frame counter is in the last
        # interval the pose is a blend of key N-1 and key 0 — and ZzzBMD.cpp's PlayAnimation
        # wraps the counter at NumAnimationKeys to match. N keys, N intervals.
        #
        # Written as N keys ending at (N-1)/fps, which is what this did, the clip ends *on*
        # its last key and there is no interval left to carry anything back over. The pose
        # jumps from the last key to the first between one frame and the next. It shows worst
        # on the idles because that is where the two ends are furthest apart: the seam on
        # PLAYER_STOP_MALE is 0.53 in quaternion terms against a walk's 0.04.
        #
        # So a free action gets a closing key at N/fps holding key 0's pose, which gives the
        # wrap the one interval it is owed and makes the loop exactly as long as MU means it
        # to be. glTF has no loop mode to express this with, which is why it has to be a key.
        closing = not animation.get("locked") or animation["action"] in closes

        # An action that holds on its last key is never carried back round, so it must not be
        # given the key that carries it.
        #
        # The closing key above holds key 0's pose. On a looping action that is exactly
        # right. On a monster's death it is a corpse that spends its final interval rising
        # back toward the pose it was standing in, which is a body that half gets up as it
        # dies — and it is worse than it sounds, because the death is the one clip whose two
        # ends are meant to be as far apart as possible. Playing it once rather than looping
        # does not save it: the bad interval is inside the clip.
        if animation["action"] in holds:
            closing = False

        # A locked action already carries one and must not be given a second.
        #
        # Those are the walks and runs, whose last key is the animator's own duplicate of the
        # first - the client wraps them at NumAnimationKeys - 1 for exactly that reason. It
        # sits at (N-1)/fps, which is where the clip ends, so the loop point is already the
        # right pose at the right moment and there is nothing to add.
        stamps = keys + 1 if closing else keys

        # Bones geared down against the rest of the body, which is the second departure and
        # is asked for by name, per model, on a looping action only.
        #
        # Object40 is the Chaos Machine, and its one clip holds the goblins pushing and the
        # drum they push: eight keys, one push, and the drum a whole revolution in those
        # eight keys at 45 degrees apiece. That is MU's, and on screen the goblins shuffled
        # while the drum whirled — slow the clip for the drum and the goblins crawl. So the
        # named bones take `loops` of the action to go round once, and everything else
        # plays the action that many times over the same span: the clip is `loops` actions
        # long, each bone on it runs its own track once per its own cycle, and it still
        # closes, because a geared bone's cycle ends on its first key exactly as an ordinary
        # one's does.
        gear = geared[1] if geared and closing and geared[1] > 1 else 1
        stamps = keys * gear + 1 if gear > 1 else stamps

        # And how fast it runs, which the client decides per action and not per model.
        # PLAYER_STOP_MALE at 0.28 and PLAYER_STOP_BOW at 0.22 are both six keys and are not
        # the same length; a townsperson's actions are all 0.25 and so all are.
        rate = speeds.get(animation["action"], play_speed) * REFERENCE_FPS
        times = [key / rate for key in range(stamps)]
        payload = struct.pack(f"<{stamps}f", *times)

        binary.accessors.append({
            "bufferView": binary.view(payload),
            "componentType": FLOAT,
            "count": stamps,
            "type": "SCALAR",
            "min": [times[0]],
            "max": [times[-1]],
        })

        clock = len(binary.accessors) - 1
        samplers: list[dict] = []
        channels: list[dict] = []

        # A second clock, for the tracks inside a free action that already close.
        #
        # `closing` is decided once for the whole action, because MU decides the wrap once:
        # N keys, N intervals, the last carrying key N-1 back to key 0. That is right for
        # the action and it is not right for every track in it. A track whose last authored
        # key already *is* its first has nothing to carry — its wrap interval is a frame in
        # which nothing moves, and the eye reads that as a stall at the same point every
        # cycle. Carriage01's hanging lamp is the case: bone 1 comes back to its own first
        # rotation exactly, while bone 6 ends a quarter of a unit away and genuinely needs
        # the interval.
        #
        # So such a track is not given the extra key, and is stretched over the same span
        # instead: the same number of keys it was authored with, spread across the length the
        # rest of the action runs to. It keeps the loop closed, keeps every bone in step, and
        # costs the track five percent on each interval rather than a whole interval of
        # nothing. MU itself stalls here — it plays the interval and the interval is empty —
        # so this is a deliberate departure and not a repair. It is the only one in this file.
        #
        # And it is asked for per model, because a second clock is only free on a model whose
        # bones are not read against each other. The lamp is one bone hanging off a cart and
        # nothing in the cart is judged by whether it swings in time. A character is the
        # opposite: five percent is a fifth of an interval on a seven-key swing, and a track
        # a fifth of an interval behind the shoulder it hangs from is a limb that arrives
        # late and catches up at the loop point. Applied to every model, this stretched 5273
        # tracks across 243 of the player's 250 free actions, and the swing staggered.
        stretched: int | None = None

        def evenly() -> int:
            """The stretched clock, made once and shared by every track that needs it."""
            nonlocal stretched

            if stretched is not None:
                return stretched

            span = times[-1]
            spread = ([key * span / (keys - 1) for key in range(keys)] if keys > 1
                      else [0.0])

            binary.accessors.append({
                "bufferView": binary.view(struct.pack(f"<{keys}f", *spread)),
                "componentType": FLOAT,
                "count": keys,
                "type": "SCALAR",
                "min": [spread[0]],
                "max": [spread[-1]],
            })

            stretched = len(binary.accessors) - 1
            return stretched

        for track in animation["tracks"]:
            # The player's skeleton is longer than the part's, and only the front of it is
            # shared. player.bmd has 60 bones where a worn part has 56: the first 51 are
            # the Biped and agree name for name, and the tail is helper objects — Mesh01,
            # gfh — that belong to whichever model declared them and are bound by nothing.
            #
            # A channel aimed past the end of the skin is a channel aimed at a node that
            # does not exist. The bind pose was perfect and the animated pose collapsed,
            # which is what that looks like from the outside.
            if track["bone"] >= joints:
                continue

            joint = first_joint + track["bone"]

            positions = track["t"]

            # MU bakes the stride into the root bone, and its own client throws it away.
            #
            # A locked action is one whose root travels. PLAYER_WALK_SWORD carries Bip01
            # 242 units — 2.4 metres, one stride — across its seven keys, because the
            # animator walked the character rather than animating him on the spot. The
            # client does not use that: it moves the character itself, at a speed of its
            # own, and pins the root's horizontal translation to the first key so the clip
            # supplies only the legs. See MuModel.BuildAnimation in the old client, which
            # is where the rule is written down.
            #
            # Honouring both is the character walking out of his own body and being
            # snapped back into it once per cycle, which is exactly how it looks. It also
            # breaks the loop: the last key of a locked action repeats the first, so with
            # the travel left in, the two ends no longer meet and every stride starts with
            # a jump.
            #
            # MU is Z-up, so X and Y are the pair being pinned. Z is the vertical rise and
            # fall of the body, which has to stay — without it a walk glides.
            if animation.get("locked") and track["bone"] == 0 and positions:
                anchor = positions[0]
                positions = [[anchor[0], anchor[1], key[2]] for key in positions]

            # Played exactly as MU stores it otherwise. The joints are the rig's, so an
            # absolute pose is what belongs on them, and muextract has already left out
            # the bones this action says nothing about.
            for path, values, kind in (
                    ("translation",
                     [to_gltf_point(t, scale) for t in positions], "VEC3"),
                    ("rotation",
                     [to_gltf_rotation(r) for r in track["r"]], "VEC4")):

                if len(values) != keys:
                    continue

                # Which clock this track runs on, and whether it is owed a closing key.
                # See `evenly` above: a track that already ends where it began is not given
                # one, and rides the stretched clock so that it still fills the action.
                beat = clock

                if gear > 1:
                    values = (geared_track(values, gear, path == "rotation")
                              if track["bone"] in geared[0]
                              else values * gear + [values[0]])
                elif closing:
                    if unstall and returns(values):
                        beat = evenly()
                    else:
                        values = values + [values[0]]

                # Every track is written, including the ones that never change, and this
                # is not laziness about file size.
                #
                # Dropping a constant track leaves that joint holding whatever its node
                # says — and the node says the *part's* rest pose, while the animation
                # comes from the *player's*. They are close, a few centimetres over the
                # whole skeleton, but they are not the same, so dropping some tracks and
                # keeping others assembles a pose out of two different skeletons, one bone
                # at a time. It renders as the armour splayed apart in mid air, which is a
                # long way from the three centimetres the two rest poses differ by.
                #
                # With every channel present the node's own rest never participates, and
                # the pose is whatever the animation says it is, whole.

                samplers.append({
                    "input": beat,
                    "output": binary.attribute(values, kind),
                    "interpolation": "LINEAR",
                })

                channels.append({
                    "sampler": len(samplers) - 1,
                    "target": {"node": joint, "path": path},
                })

        if not channels:
            continue

        name = f"action{animation['action']}"
        document["animations"].append(
            {"name": name, "samplers": samplers, "channels": channels})

        names.append(
            f"{name} ({len(channels)} channels, {keys} keys, "
            f"{times[-1]:.3f}s{', closed' if closing else ''})")

    if not document["animations"]:
        del document["animations"]

    return names


def slot_skipped(slots: list[dict], name: str) -> bool:
    """Whether this slot's geometry is left out of the file entirely.

    For the things MU draws that this renderer draws for itself. Bridge01 carries a
    bridge_shadow01 sheet — a two-pixel-wide dark strip stretched under the span, which is
    the shadow the client paints because it has no shadow map. Drawn here it is a second
    shadow laid over a real one, in the wrong place whenever the sun is not where MU assumed.
    """
    for slot in slots:
        if slot["name"].lower() == name.lower():
            return bool(slot.get("skip"))

    return False


def tiled_materials(document: dict, binary: Binary, slots: list[dict]) -> dict[str, int]:
    """One material per sheet, sampling MU's art at MU's own coordinates.

    The other path bakes: it unwraps the mesh into a new layout, moves MU's painting onto it,
    and ships one atlas. That is right for everything a character wears or carries, because
    every one of those triangles samples its sheet exactly once, so a layout can hold all of
    them side by side and a map twice the sheet is twice the art.

    Scenery is not built that way and cannot be shipped that way. Stone01's boulder wraps a
    128-square rock face round itself two and a third times over — its UVs run from -0.44 to
    1.33 — and the thirteen grass cards standing at its foot are thirteen quads that each map
    the whole of the same 32-square tuft. Baked, the first wants two and a third sheets of
    unique texels for one island and the second asks for thirteen sheets to say one sheet
    thirteen times. What came out was the boulder holding 18% of the map while its weeds took
    59% of it, which is not a size that can be tuned: repeating art is the property the sheet
    was drawn to have, and an atlas is the format that cannot express it.

    So nothing is baked. Each sheet goes in as MU drew it, upscaled and no more, and the mesh
    keeps the coordinates MU gave it. Tiling tiles, because the sampler repeats and the UVs
    are allowed to leave the square. Thirteen identical cards cost one sheet between them
    because they genuinely are one sheet. And the material stops being a property of an
    island and becomes a property of a sheet, which for scenery is what it always was: ston01
    is rock and ston02 is foliage, and no island had to be looked at to know it.

    What is given up is the generated surface — the grain normal and the traced occlusion,
    which are baked into an atlas and have nowhere to live here. Roughness and metallic still
    come from the material library, as factors. See the note in the world profile.
    """
    images: list[dict] = []
    textures: list[dict] = []
    materials: list[dict] = []
    by_name: dict[str, int] = {}

    # One image per file, however many slots ask for it.
    #
    # Two slots on one model routinely name the same sheet — and once a Fab surface replaces
    # both c_wall04 and c_wall06 they name the same 2048-square picture, with a normal and an
    # ORM each. Emitted once per slot that was six copies of ten megabytes in one wall:
    # StoneMuWall02 came out at 56.5 MB. Keyed by resolved path rather than by content,
    # because the paths are what the slots carry and hashing ten megabytes to learn what the
    # file name already says is work for nothing.
    seen: dict[str, int] = {}

    def attach(path: Path) -> int:
        """The texture index for a file, emitting its image the first time it is asked for."""
        key = str(path.resolve())

        if key in seen:
            return seen[key]

        data = path.read_bytes()
        images.append({
            "name": path.stem,
            "bufferView": binary.view(data),
            "mimeType": "image/jpeg" if data.startswith(b"\xff\xd8") else "image/png",
        })

        textures.append({"sampler": 0, "source": len(images) - 1})
        seen[key] = len(textures) - 1
        return seen[key]

    for slot in slots:
        sheet = Path(slot["sheet"])

        material = {
            "name": slot["name"],
            "pbrMetallicRoughness": {
                "metallicFactor": float(slot.get("metallic", 0.0)),
                "roughnessFactor": float(slot.get("roughness", 0.6)),
            },

            # As the old client drew every MU model, and scenery needs it more than an item
            # does: a grass card is one quad seen from both sides, and a house is a shell.
            #
            # Per material, because on a body the answer differs between two materials of the
            # same mesh. HelmClass02's cull_note has the whole argument: the skull is a closed
            # solid and culling it removes the crown wedge - you look at the back of the head
            # and see the inside of the face through the gaps between the hair fins - while
            # the fins are single-sided sheets and a culled fin does not turn away from the
            # camera, it vanishes. Culling the part as a whole was tried and reverted because
            # it made the silhouette pop, which was read as the head rotating on its own.
            #
            # The bake keeps them apart - a built head carries a hair material and a skin
            # material over two primitives - so the choice can be made where it belongs.
            "doubleSided": bool(slot.get("two_sided", True)),
        }

        # A tint the sheet is multiplied by, where the asset asks for one.
        #
        # This is what baseColorFactor is for and the one colour control that commits as
        # itself: a Fab photoscan carries the cast of the sky it was measured under, and
        # nudging it towards MU's palette is a judgement made by eye on the bench against
        # the art it will stand next to. Written linear, because glTF's factor is, and
        # what the panel showed was sRGB.
        if slot.get("tint"):
            hexed = str(slot["tint"]).lstrip("#")
            channels = [int(hexed[at:at + 2], 16) / 255.0 for at in (0, 2, 4)]

            material["pbrMetallicRoughness"]["baseColorFactor"] = [
                one / 12.92 if one <= 0.04045 else ((one + 0.055) / 1.055) ** 2.4
                for one in channels
            ] + [1.0]

        if sheet.exists():
            material["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": attach(sheet), "texCoord": 0,
            }

            # Per sheet rather than per asset, which the baked path could not manage.
            #
            # Stone01 is one model wearing an opaque rock and a cut-out grass, and with one
            # material for the whole thing the choice was between a boulder with its alpha
            # tested and weeds that were olive rectangles. A sheet knows what it is: the
            # .OZT carries the alpha channel and the asset names the slot.
            if slot.get("cutout"):
                material["alphaMode"] = "MASK"

                # A quarter, unless the sheet says otherwise, because that is MU's own.
                #
                # `BeginOpengl` sets `glAlphaFunc(GL_GREATER, 0.25f)` and the client never
                # changes it for a world object: ZzzBMD's mesh path reaches `EnableAlphaTest()`
                # on `alpha < 0.99f || texture->Components == 4` — any sheet with an alpha
                # channel at all — and tests it against that quarter.
                #
                # It was a half here, which is the value everybody reaches for and is a
                # quarter too strict. Every texel MU keeps between 0.25 and 0.5 was being
                # discarded, which is exactly the soft edge of a thing painted on a card: the
                # outer rank of a leaf, the tips of a grass tuft, the fringe of a flower. The
                # shape stayed recognisable, which is why it survived — what it cost was the
                # edge, on every cut-out in the project at once. Noria is 2225 flower cards
                # and 1963 leaf cards and is where it stopped being invisible.
                #
                # Declared per slot where a sheet wants otherwise, and one does: guard_hair is
                # 0.2% fully solid with a mean of 48 out of 255, and even a quarter leaves the
                # mage's hair as confetti. See Wizard01, which asks for a tenth.
                material["alphaCutoff"] = float(slot.get("cutout_at", ALPHA_TEST_REF))

            # A glow is not a surface, so it is not shaded like one.
            #
            # The base colour is forced to black while the same sheet goes in again as
            # emission: the quad then contributes its own light and none of anybody else's,
            # which is what "drawn additively" meant in the client. Blended rather than
            # masked, because a glow has no edge to cut along - tiled_maps has already put
            # the sheet's own brightness in its alpha, so the fade is the shape.
            if slot.get("emissive"):
                pbr = material["pbrMetallicRoughness"]
                pbr["baseColorFactor"] = [0.0, 0.0, 0.0, 1.0]
                pbr["roughnessFactor"] = 1.0
                pbr["metallicFactor"] = 0.0

                material["alphaMode"] = "BLEND"
                material["emissiveFactor"] = [1.0, 1.0, 1.0]
                material["emissiveTexture"] = dict(
                    material["pbrMetallicRoughness"]["baseColorTexture"])

                # And how far past white it sits.
                #
                # emissiveFactor is capped at one by the specification, which is exactly the
                # brightness at which a renderer stops treating something as a light: the
                # frame blooms what is over one, so a glow pinned at one gets no bloom and
                # draws as a flat panel with a border. KHR_materials_emissive_strength is
                # the extension that exists for this and the only way to say it in glTF.
                #
                # Declared by the material rather than here — see lamplight — because how
                # bright a lamp is is a fact about lamps, not about exporting.
                energy = float(slot.get("emissive_energy") or 0.0)

                if energy > 1.0:
                    material.setdefault("extensions", {})[
                        "KHR_materials_emissive_strength"] = {"emissiveStrength": energy}

                    used = document.setdefault("extensionsUsed", [])

                    if "KHR_materials_emissive_strength" not in used:
                        used.append("KHR_materials_emissive_strength")

        # Light that came through rather than round.
        #
        # A leaf is a tenth of a millimetre of green, and once foliage normals point up every
        # card in a canopy faces the sun and shadows the ones beneath it — which gave a tree
        # a lit crown over a black underside. What reaches the shaded side of a canopy is
        # mostly light transmitted by the leaves above it.
        #
        # Carried as emission from the sheet's own colour, because glTF has no transmission a
        # game engine reliably reads and the shape of the answer is the same: in shadow the
        # surface shows a fraction of itself instead of nothing.
        if slot.get("translucency") and "baseColorTexture" in material["pbrMetallicRoughness"]:
            through = float(slot["translucency"])
            material["emissiveTexture"] = dict(
                material["pbrMetallicRoughness"]["baseColorTexture"])
            material["emissiveFactor"] = [through, through, through]

        # The relief, where the sheet has any. Read off the art's own luminance by
        # tiled_maps rather than generated, because world art is a photograph of a surface
        # and its light and dark really are its cracks. Absent for a material that declares
        # no grain, which for foliage is deliberate.
        relief = Path(slot["normal"]) if slot.get("normal") else None

        if relief is not None and relief.exists():
            material["normalTexture"] = {
                "index": attach(relief), "texCoord": 0,

                # How far the measured normal is pushed. One is the map as it was made; the
                # asset may say otherwise, because a photoscan's relief is measured on the
                # real thing and MU's walls are not the size of the real thing.
                "scale": float(slot.get("relief", 1.0)),
            }

        # And the packed occlusion-roughness-metallic, where the sheet has one. The factors
        # go to 1.0 because in glTF a factor multiplies its texture, and leaving the
        # material's own roughness in place beside a roughness map is the standard way to
        # ship a surface at a fraction of the roughness it was measured at.
        maps = Path(slot["orm"]) if slot.get("orm") else None

        if maps is not None and maps.exists():
            packed = attach(maps)
            pbr = material["pbrMetallicRoughness"]
            pbr["metallicRoughnessTexture"] = {"index": packed, "texCoord": 0}
            pbr["metallicFactor"] = 1.0

            # One, always, because in glTF a factor multiplies its texture and the map is
            # where the answer lives. An asset that names a roughness for a slot has already
            # had it written into the ORM's green channel by tiled_maps — a factor here would
            # apply it a second time. See toned() for why the map rather than the factor.
            pbr["roughnessFactor"] = 1.0

            # The same file again as occlusion, which is what its red channel is.
            material["occlusionTexture"] = {
                "index": packed, "texCoord": 0, "strength": 1.0,
            }

        by_name[slot["name"]] = len(materials)
        materials.append(material)

    if images:
        document["images"] = images
        document["textures"] = textures
        document["samplers"] = [{
            "magFilter": LINEAR,
            "minFilter": LINEAR_MIPMAP_LINEAR,

            # The reason the whole path works. MU's world UVs leave the unit square and mean
            # it, and a repeating sampler is what turns a coordinate of 1.33 back into 0.33.
            "wrapS": REPEAT,
            "wrapT": REPEAT,
        }]

    document["materials"] = materials
    return by_name


def pbr_material(
        document: dict, binary: Binary, maps: dict[str, Path],
        cutout: bool = False, cull: bool = False) -> int:
    """A metallic-roughness material, which is what glTF understands.

    Every texture rides inside the .glb as bytes rather than as a path beside it, for the
    reason the module note gives: one file, or a folder that arrives half-copied. Each PNG
    is embedded exactly as written — no re-encode, so what the engine samples is the file
    the pipeline produced, bit for bit.

    Three maps, and what they are is decided upstream:

    - *base colour* is MU's own painting, moved onto the baked layout by transfer_albedo
      and otherwise untouched. See decode_texture on why it is not something more.
    - *metallic-roughness* is build_maps' packed file: roughness in green, metallic in
      blue, which is glTF's own arrangement rather than a choice made here.
    - *normal* is the surface grain, generated from the material definitions.

    The factors are 1.0 where a map exists, because in glTF a factor multiplies its
    texture. Leaving metallicFactor at 0 with a metallic map attached is the standard way
    to ship an item where nothing is metal and wonder why.

    - *occlusion* is the red channel of that same packed file, traced from the mesh by
      build_maps. It is attached as its own entry pointing at the same texture, which is
      what glTF's occlusion-roughness-metallic arrangement is for. It was left off while
      that channel was a constant 1.0, because claiming an occlusion texture that says "no
      occlusion anywhere" is a claim rather than a placeholder.
    """
    material = {
        "name": "mu2",
        "pbrMetallicRoughness": {},

        # Two-sided unless the asset asks to be culled, and a cut-out is two-sided whatever
        # it asks.
        #
        # This said "as the old client drew every MU model — culling disabled outright",
        # and that is not what the client does. ZzzOpenglUtil.cpp turns GL_CULL_FACE on at
        # the top of every frame, and the only things that turn it off again are
        # EnableAlphaTest and EnableAlphaBlend: MU culls its opaque geometry and spares its
        # cut-outs. That is the rule this now follows.
        #
        # Two-sided stays the default all the same, because the observation behind the old
        # comment is true of items even where the reason given for it was not — Sword01 is
        # a 68-triangle shell with eight open edges in it, and culling those turns them into
        # holes. What is not an open shell is a body. See profiles/armour.json for what the
        # difference cost.
        "doubleSided": bool(cutout) or not cull,
    }

    images: list[dict] = []
    textures: list[dict] = []

    def attach(name: str) -> int | None:
        """Embeds one map and returns the texture index that points at it."""
        path = maps.get(name)
        if path is None or not path.exists():
            return None

        data = path.read_bytes()
        images.append({
            "name": path.stem,
            "bufferView": binary.view(data),
            "mimeType": "image/jpeg" if data.startswith(b"\xff\xd8") else "image/png",
        })

        textures.append({"sampler": 0, "source": len(images) - 1})
        return len(textures) - 1

    albedo = attach("albedo")
    packed = attach("orm")
    normal = attach("normal")

    pbr = material["pbrMetallicRoughness"]

    if albedo is not None:
        # texCoord 0 explicitly: the baked layout is where every map this item will ever
        # have is written, and it is TEXCOORD_0 because that is the layer the mesh writes
        # there. MU's own coordinates are TEXCOORD_1 and nothing samples them.
        pbr["baseColorTexture"] = {"index": albedo, "texCoord": 0}

        # Cut out rather than blended, where the *asset* says so.
        #
        # Said by the asset and not worked out from the baked maps, after two goes at the
        # latter. Every bake map is transparent outside its islands because nothing was
        # written there, so "does the albedo have any transparency" is true of everything
        # ever built; masking that by the ownership map does not help either, because
        # build_maps dilates ownership eight texels past each island for bleed and the
        # alpha pass does not fill the same rind — so every asset came back a cut-out both
        # times, which is exactly the claim this was supposed to avoid making.
        #
        # The asset knows. It declares a cut-out slot, or one of its sheets carries a real
        # alpha channel, and mu2.sh passes that through.
        #
        # MU makes shapes with alpha instead of modelling them: the Plate helm's plume is
        # two flat quads with the feather as a hole in the middle, and a shield's fretwork
        # and a character's hair are the same trick. Those sheets ship as .OZT, a TGA with
        # an alpha channel, and transfer_albedo carries it into the bake.
        #
        # glTF treats a material as opaque unless it is told otherwise, so without this the
        # alpha is present and ignored — which is exactly what the plume looked like, two
        # rectangles with their background on.
        #
        # MASK and not BLEND, because MU alpha-*tests* these. A blended plume would need
        # sorting against itself, and two quads crossing at a right angle is the case that
        # has no correct order. A test has no order to get wrong.
        if cutout:
            material["alphaMode"] = "MASK"
            material["alphaCutoff"] = ALPHA_TEST_REF

    if packed is not None:
        pbr["metallicRoughnessTexture"] = {"index": packed, "texCoord": 0}
        pbr["metallicFactor"] = 1.0
        pbr["roughnessFactor"] = 1.0

        # And the same file again as the occlusion, which is what its red channel is for.
        #
        # This used to be left off deliberately, because the red channel was a constant 1.0
        # and claiming an occlusion texture that says "no occlusion anywhere" is a claim
        # rather than a placeholder. build_maps traces it from the mesh now, so the claim is
        # true and the map is worth reading.
        #
        # One texture, two slots. glTF defines occlusion-roughness-metallic as three
        # channels of one image for exactly this reason, and pointing both entries at the
        # same index is how a file says so.
        material["occlusionTexture"] = {"index": packed, "texCoord": 0, "strength": 1.0}
    else:
        # No maps yet: fully dielectric and fairly rough, so the item reads as an
        # unfinished surface rather than as polished chrome. An honest placeholder is
        # worth more than a plausible invention.
        pbr["metallicFactor"] = 0.0
        pbr["roughnessFactor"] = 0.6

    if normal is not None:
        material["normalTexture"] = {"index": normal, "texCoord": 0, "scale": 1.0}

    if images:
        document["images"] = images
        document["textures"] = textures
        document["samplers"] = [{
            "magFilter": LINEAR,
            "minFilter": LINEAR_MIPMAP_LINEAR,
            "wrapS": REPEAT,
            "wrapT": REPEAT,
        }]

    document["materials"] = [material]
    return 0


def write_glb(path: Path, document: dict, blob: bytearray) -> None:
    """The container: a header, the JSON chunk, then the bytes.

    Both chunks are padded to four bytes — the JSON with spaces and the buffer with zeros,
    which is what the spec asks for so that a reader can pad-strip either one without
    knowing which it has.
    """
    while len(blob) % 4:
        blob.append(0)

    document["buffers"] = [{"byteLength": len(blob)}]

    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)

    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("wb") as file:
        file.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        file.write(struct.pack("<II", len(text), 0x4E4F534A))     # "JSON"
        file.write(text)
        file.write(struct.pack("<II", len(blob), 0x004E4942))     # "BIN\0"
        file.write(blob)


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: ... --python export_gltf.py -- <in.blend> <albedo|-> <out.glb>")
        return

    blend = Path(argv[0])
    albedo = None if argv[1] == "-" else Path(argv[1])
    destination = Path(argv[2]) if len(argv) > 2 else blend.with_suffix(".glb")

    # The rest of the set is found rather than passed, beside the albedo and named for the
    # item. build_maps writes them there and nowhere else, and a command line with five
    # texture paths on it is a command line nobody types correctly twice.
    item = blend.stem.removesuffix("_low")
    beside = albedo.parent if albedo else blend.parent

    # The skeleton comes from the part's own rig — its rest pose is what the inverse bind
    # matrices are stated against — and the motion comes from the player's, because a worn
    # part carries one bind-pose action and nothing else. See muextract's note: the bones
    # agree by name and index between them, which is the whole reason this works.
    rig_path = None
    animation_path = None

    # And how fast the client plays what it finds there. Neither number is in the .bmd — MU
    # assigns play speeds at load time, in code — so both arrive from outside. See
    # DEFAULT_PLAY_SPEED for which kind of model gets which.
    speeds_path = None
    play_speed = DEFAULT_PLAY_SPEED

    # Asked for by the model that wants it, and by no other. See write_animations.
    unstall = False
    geared: tuple[set[int], int] | None = None

    for argument in argv[3:]:
        if argument.startswith("--rig="):
            rig_path = Path(argument.split("=", 1)[1])
        elif argument.startswith("--animation="):
            animation_path = Path(argument.split("=", 1)[1])
        elif argument.startswith("--speeds="):
            speeds_path = Path(argument.split("=", 1)[1])
        elif argument.startswith("--play-speed="):
            play_speed = float(argument.split("=", 1)[1])
        elif argument == "--unstall":
            unstall = True
        elif argument.startswith("--geared="):
            listed, loops = argument.split("=", 1)[1].split(":")
            geared = ({int(bone) for bone in listed.split(",")}, int(loops))

    speeds, holds = read_speeds(speeds_path)

    # A tiled asset names its sheets in a file rather than on the command line, because each
    # one carries a path, a material, two numbers and a flag, and that is a JSON document
    # being spelled out in argv.
    slots: list[dict] = []
    islands_file = None

    for argument in argv[3:]:
        if argument.startswith("--islands="):
            islands_file = Path(argument.split("=", 1)[1])

    for argument in argv[3:]:
        if argument.startswith("--slots="):
            slots = json.loads(Path(argument.split("=", 1)[1]).read_text())

    cutout = "--cutout" in argv

    # Where the clips went, for a part that no longer carries them.
    #
    # Written into the file rather than into the index, because it is a fact about this part
    # and not about any character wearing it. Three different paths load a worn part — the
    # viewer's character tab collapses five onto one skeleton, the town stands each on its
    # own, and the crowd builds a champion — and a fact carried in the file is one that all
    # three get without being told. Plumbing it through the index instead meant threading it
    # through the catalogue, the placement groups and the crowd's constructor for the same
    # answer each time.
    #
    # Relative to the .glb's own directory, so the pair can be moved or copied together and
    # neither has to know where the build tree is rooted.
    library = None

    for argument in argv[3:]:
        if argument.startswith("--actions="):
            library = Path(argument.split("=", 1)[1])

    # Whether the clips this part borrows are shipped inside it, or once beside the rig.
    #
    # A worn part is not the model the clips belong to. Five of them make a character and
    # all five are skinned to player.bmd's sixty bones, so writing the motion into each is
    # writing it five times — and it was worse than five, because the same rig dresses
    # three armour classes and the fifteen files held fifteen byte-identical copies of the
    # same 283 actions. 13.7 MB apiece, of which 13.4 was the copy.
    #
    # The viewer never wanted them either. Model.Load collapses the parts onto one skeleton
    # and keeps the first AnimationPlayer, so four fifths of what was parsed at load was
    # freed on the next line: two seconds of glTF parsing to throw away.
    #
    # So the clips come out, and export_actions writes them once into an AnimationLibrary
    # the viewer binds to whichever skeleton it assembled. Only for parts that borrow their
    # motion from a rig — a monster or a street lamp carries its own actions and is one
    # file, which is the shape this is trying to get worn parts back to.
    inline_clips = "--no-clips" not in argv

    # Whether this one is culled the way the client culls. Off unless asked, so an item
    # keeps the two-sided draw its open shell needs; see pbr_material.
    cull = "--cull" in argv
    maps = {"albedo": albedo} if albedo else {}

    # The corrected base colour wins where build_maps wrote one. It is the same painting with
    # each metal pulled onto its own reflectance - see base_colour there - and it exists only
    # for items that carry metal, so the albedo stays the base colour for everything else.
    # The albedo itself is never overwritten, which is why this is a choice made here.
    corrected = beside / f"{item}_basecolor.png"
    if corrected.exists():
        maps["albedo"] = corrected
        print(f"  base       {corrected.name}, MU's colour with the metal's reflectance in it")

    for name in ("orm", "normal"):
        candidate = beside / f"{item}_{name}.png"
        if candidate.exists():
            maps[name] = candidate

    bpy.ops.wm.open_mainfile(filepath=str(blend))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print(f"no mesh in {blend}")
        return

    obj = meshes[0]
    mesh = evaluated(obj)

    print(f"\n=== {blend.name} -> {destination.name} ===")

    # Which slots want their normals replaced, by the material each one declares. Read here
    # rather than in mesh_attributes because that function knows about meshes and not about
    # the material library.
    names = [m.name.lower() if m else "" for m in obj.data.materials]
    upright = {
        index for index, slot in enumerate(slots)
        if slot.get("normals") == "up"
        for _ in [0]
    } if slots else set()

    if slots:
        wanted = {s["name"].lower() for s in slots if s.get("normals") == "up"}
        upright = {i for i, n in enumerate(names) if n in wanted}

    # And how many times each slot's sheet repeats across its surface. See mesh_attributes.
    scaled = {s["name"].lower(): float(s["scale"]) for s in (slots or []) if s.get("scale")}
    repeat = {i: scaled[n] for i, n in enumerate(names) if n in scaled}

    # What each face is made of, for a baked item that is more than one substance.
    #
    # Only for the baked path, and only where the asset says. A tiled asset is already
    # divided by its sheets, which is the division that matters there; a baked one has been
    # flattened into a single atlas and a single part called "mu2", and on an axe that means
    # the haft and the head cannot be spoken to separately. The asset's island list has been
    # naming them for as long as build_maps has been generating maps from it — this reads the
    # same file and the same islands, so the two cannot disagree about which face is which.
    parts = None

    # Tiled, and divided by which part of its sheet a face samples.
    #
    # The other divider here is the island list, and it cannot help on scenery: Cannon01's
    # eleven islands each span most of its atlas, because MU wrapped one 256-square round a
    # cart, two crates, a wheel and a gun. The faces are another matter — a face samples a
    # small patch of the sheet, so 76 of the cannon's 181 land wholly inside an iron box and
    # 95 wholly outside, with ten straddling.
    #
    # Those ten go where most of their corners are. A face that is half barrel and half
    # crate has to be one or the other and there is no third answer; a majority is the answer
    # that is wrong for the smallest area.
    if slots and any(one.get("boxes") for one in slots):
        boxed = [one for one in slots if one.get("boxes")]
        names = [m.name if m else "" for m in obj.data.materials] or [""]
        parts = {}

        # MU's own layer, which is what the boxes were read off and what a tiled asset ships
        # as TEXCOORD_0. Not the active one: clean_lowpoly leaves `bake` active, and tested
        # there Cannon01's split came out inverted — the 81 faces on the barrel stayed timber
        # and the 6 that landed in no box, the crate's planks, shipped as wrought iron.
        layer = mesh.uv_layers.get(ORIGINAL_UV) or mesh.uv_layers.active

        for face in mesh.polygons:
            slot = names[face.material_index] if face.material_index < len(names) else ""
            mine = [one for one in boxed if one.get("of", "").lower() == slot.lower()]

            if not mine:
                parts[face.index] = slot
                continue

            corners = [layer.data[i].uv for i in face.loop_indices]
            best, most = slot, 0

            for one in mine:
                inside = sum(
                    1 for uv in corners
                    if any(u0 <= (uv[0] % 1.0) <= u1 and v0 <= (1.0 - (uv[1] % 1.0)) <= v1
                           for u0, v0, u1, v1 in one["boxes"]))

                if inside > most and inside * 2 >= len(corners):
                    best, most = one["name"], inside

            parts[face.index] = best

        if len(set(parts.values())) < 2:
            parts = None

    if islands_file and not slots and islands_file.exists():
        recorded = json.loads(islands_file.read_text()).get("islands") or []

        if recorded:
            import bmesh                                # noqa: E402 (Blender only)

            bm = bmesh.new()
            bm.from_mesh(mesh)
            sys.path.insert(0, str(Path(__file__).parent))
            from islands import islands_of              # noqa: E402 (same directory)

            found = islands_of(bm)
            bm.free()

            if len(found) != len(recorded):
                # Not fatal. build_maps checks this properly and refuses; here the worst
                # case is the model ships as one part, which is what it did before.
                print(f"  islands    mesh has {len(found)} and the asset describes "
                      f"{len(recorded)}; shipping as one part", file=sys.stderr)
            else:
                # A nocked missile gets a part of its own, whatever it is made of.
                #
                # The arrow lying on a bow and the bolt in a crossbow's groove are their own
                # mesh in MU, on their own sheet and their own bone - the artists split them
                # out, and the client has a HiddenMesh switch that skips a mesh by index. It
                # never uses it on a weapon, because it never plays the weapon's animation
                # either: MU draws every bow loaded, always, and what flies is a separate
                # effect. Playing the clip is what makes the difference visible, and hiding
                # the thing once it has left needs it to be a primitive of its own.
                #
                # Grouped by name here, so the flag is enough: two islands of the same
                # material land in one primitive unless one of them is called something else.
                # In the baked path a part name is only a label on a copy of the one material,
                # so this costs a few hundred bytes of JSON and nothing at all in the render.
                # And so does a mesh the client hides on a variant. c->Object.HiddenMesh
                # skips one mesh of a model by index - the plain Bull Fighter's crest, mesh
                # 0 of Monster01.bmd, which the Elite wearing the same model keeps. The
                # bake has no mesh index any more, so the asset flags the islands that
                # came out of that mesh and they ship as a part called "hidden", which the
                # client blanks by name. See Model.Conceal.
                named = {
                    one["island"]: (
                        "nocked" if one.get("nocked")
                        else "hidden" if one.get("hidden")
                        else one.get("material", ""))
                    for one in recorded
                }
                parts = {
                    face: named.get(index) or "mu2"
                    for index, group in enumerate(found) for face in group
                }

                if len(set(parts.values())) < 2:
                    parts = None

    attributes, groups, counts = mesh_attributes(
        obj, mesh, tiled=bool(slots), upright=upright, repeat=repeat, parts=parts)

    # The rig, read before any accessor is built — because the vertex bones are rewritten
    # here and an accessor packed from the old numbering cannot be taken back.
    part = motion = bones = binds = None

    if rig_path and rig_path.exists() and attributes["JOINTS_0"]:
        part = json.loads(rig_path.read_text())
        motion = (
            json.loads(animation_path.read_text())
            if animation_path and animation_path.exists() else None)

        # The joints come from the rig when there is one. Without a rig the part is its
        # own skeleton, which is right for something worn by nobody.
        bones = skeleton_of(motion or part)
        binds = skeleton_of(part)

        # Every vertex's bone is rewritten from the part's numbering into the rig's, by
        # name. This is the step that has no glTF equivalent and therefore has to be done
        # by hand: after it, JOINTS_0 indexes the rig, which is what skin.joints is.
        joint_of = {bone["name"]: index for index, bone in enumerate(bones)}
        remap = [joint_of.get(bone["name"], 0) for bone in binds]

        stranded = sum(1 for bone in binds if bone["name"] not in joint_of)
        if stranded:
            print(f"  bones      {stranded} of {len(binds)} are not on the rig; "
                  f"anything bound to them is anchored to joint 0")

        # Out of the part's bind pose and into the rig's, before the joint numbers are
        # rewritten — the move is looked up by the part's own bone index, which is what
        # JOINTS_0 still holds at this point.
        shifted = rebind(attributes, binds, bones)
        if shifted:
            print(f"  rebound    {shifted} vertices into the rig's rest pose")

        attributes["JOINTS_0"] = [
            (remap[joint[0]] if joint[0] < len(remap) else 0, 0, 0, 0)
            for joint in attributes["JOINTS_0"]]

    binary = Binary()

    # One vertex stream, shared. Only the index lists differ per slot, so a model wearing
    # four sheets is four small accessors and not four copies of its geometry.
    shared = {
        name: (
            binary.joints(values) if name == "JOINTS_0"
            else binary.attribute(
                values,
                "VEC4" if name in ("TANGENT", "WEIGHTS_0")
                else "VEC2" if name.startswith("TEXCOORD")
                else "VEC3"))
        for name, values in attributes.items() if values
    }

    if slots:
        primitives = [
            {"attributes": shared, "indices": binary.indices(indices), "mode": TRIANGLES,
             **({"part": key} if isinstance(key, str) else {})}
            for key, indices in sorted(groups.items(), key=lambda pair: str(pair[0]))
            if indices
        ]
    elif parts:
        # Baked, and divided by what each piece is made of.
        #
        # Everything still lands in one atlas and wears one set of maps — that is what
        # baking is — so these primitives are not about textures. They are about being able
        # to name a piece and speak to it: an axe is a wooden haft and a steel head, and
        # until this split there was one part called "mu2" and no way to say which half of
        # it should be rougher. The material each one gets is a copy of the same maps under
        # its own name, which is what the Objects tab's part list reads.
        primitives = [
            {"attributes": shared, "indices": binary.indices(indices), "mode": TRIANGLES,
             "part": name}
            for name, indices in sorted(groups.items(), key=lambda pair: str(pair[0]))
            if indices
        ]
    else:
        # Baked with nothing to divide it by: every triangle lands in the same atlas and the
        # slots stop meaning anything, so they are flattened into the one primitive this has
        # always written.
        merged = [index for _, indices in sorted(
            groups.items(), key=lambda pair: str(pair[0])) for index in indices]
        primitives = [
            {"attributes": shared, "indices": binary.indices(merged), "mode": TRIANGLES}]

    primitive = primitives[0]

    document: dict = {
        "asset": {"version": "2.0", "generator": "MU2 pipeline/export_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": blend.stem, "mesh": 0}],
        "meshes": [{"name": blend.stem, "primitives": primitives}],
    }

    if slots:
        # Matched by the slot's own name, which clean_lowpoly took from the OBJ group, which
        # muextract took from the BMD's mesh. By name and not by position: a slot the asset
        # never mentioned would otherwise silently wear its neighbour's sheet.
        #
        # Folded to lower case, because MU's own naming is not consistent and never meant to
        # be. Tree01 wears tree.jpg and Tree_a.tga - same model, same artist, two spellings -
        # and decode_texture writes every sheet out lower-cased, so an asset that named the
        # slot as MU spells it would be wrong exactly as often as one that did not. The tree's
        # canopy shipped as white rectangles on the strength of that capital T.
        names = [m.name if m else "" for m in obj.data.materials] or [""]
        assigned = {name.lower(): index for name, index in
                    tiled_materials(document, binary, slots).items()}

        for primitive, (index, _) in zip(
                primitives, (pair for pair in sorted(
                    groups.items(), key=lambda pair: str(pair[0])) if pair[1])):
            # A split primitive already knows which slot it is: the group was keyed by the
            # slot's own name rather than by a Blender material index, because two primitives
            # here come from one Blender slot and an index cannot tell them apart.
            said = primitive.pop("part", None)
            name = said if said is not None else (names[index] if index < len(names) else "")

            if slot_skipped(slots, name):
                primitive["skip"] = True
            elif name.lower() in assigned:
                primitive["material"] = assigned[name.lower()]
            else:
                # Loud, and on stderr. A primitive with no material is not a broken file —
                # it loads, it draws, and it draws in plain white, which is the shape of
                # failure this project keeps having to go back and find.
                print(f"error: slot {index} '{name}' is not in the asset's sheets; "
                      f"it ships untextured and will render white",
                      file=sys.stderr)
    else:
        # One material, and then a copy of it per part carrying only a different name.
        #
        # Copies rather than one shared entry, because the name is the whole point: the
        # Objects tab lists a model's parts by their material names, so two primitives
        # sharing a material are one part however the mesh is divided. They point at the
        # same textures, so this costs a few hundred bytes of JSON and nothing else.
        base = pbr_material(document, binary, maps, cutout, cull)

        for one in primitives:
            name = one.pop("part", None)

            if name is None:
                one["material"] = base
                continue

            copy = json.loads(json.dumps(document["materials"][base]))
            copy["name"] = name

            # And whether this part may be culled, which is the one thing a copy does not
            # inherit — because it is the one thing that differs between two parts of the
            # same mesh.
            #
            # A body is the case. The skull is a closed solid and drawn two-sided you look
            # at the back of a head and see the inside of the face through the gaps between
            # the hair fins; the fins are single-sided sheets, and a culled fin does not turn
            # away from the camera, it vanishes. Culling the part as a whole was tried and
            # reverted for exactly that reason - the silhouette popped between frames and was
            # read as the head rotating on its own. Per material both get what they need.
            #
            # Read off the material library rather than passed in, because the answer belongs
            # to the substance and not to the asset wearing it: skin is a body wherever it
            # appears. Silent when the library has nothing to say, which leaves the base
            # material's own answer standing.
            if (wants := material_culls(name)) is not None:
                copy["doubleSided"] = not wants

            document["materials"].append(copy)
            one["material"] = len(document["materials"]) - 1
    clips: list[str] = []

    if bones is not None:
        # No binds passed: rebind has put the mesh in the rig's rest pose, so the rig is
        # the only skeleton this file describes and its own rest is what the inverse binds
        # must state.
        first_joint = write_skin(document, binary, bones, 0)

        if motion and inline_clips:
            clips = write_animations(document, binary, motion, first_joint, len(bones),
                                     speeds, play_speed, holds, unstall, geared,
                                     read_closes(speeds_path))
        elif motion and library:
            # glTF's own escape hatch, which every reader is required to carry through
            # untouched and none is allowed to interpret. Godot hands the whole document
            # back through GltfState.GetJson, which is where Model.Animate reads this.
            document["extras"] = {
                "actions": os.path.relpath(library.resolve(),
                                           destination.resolve().parent),
            }

    # Anything a slot asked to leave out. Done here rather than earlier so the accessors it
    # already built stay valid for everything else — a few unreferenced views cost bytes and
    # nothing else.
    kept = [p for p in primitives if not p.pop("skip", False)]

    if len(kept) != len(primitives):
        print(f"  skipped    {len(primitives) - len(kept)} primitive(s) the asset leaves out")
        document["meshes"][0]["primitives"] = kept

    # The glow comes off the model and onto a node of its own.
    #
    # It is one mesh with five primitives until here, and a renderer's shadow switch is per
    # object rather than per primitive — so a lit window's glow quad was cast into the
    # shadow map along with the wall it hangs in, and dropped a hard diagonal band down the
    # planks below it. A light does not have a shadow. That is the fault this fixes, and it
    # cannot be fixed anywhere downstream while the quad and the wall are the same object.
    #
    # It buys the other half too. The glow is the one part of a model that moves — its
    # brightness is written every frame, see Glows — and the town draws its placements in
    # batches. Separate nodes mean the moving part and the still part can be treated
    # differently without either being a special case of the other.
    #
    # Recognised by what the emissive branch above writes, rather than by a name: base
    # colour forced to black, blended, and the sheet in again as emission. Nothing else in
    # this exporter produces that combination.
    glowing = [p for p in kept if is_glow(document, p)]
    solid = [p for p in kept if p not in glowing]

    if glowing and solid:
        document["meshes"][0]["primitives"] = solid
        document["meshes"].append(
            {"name": f"{blend.stem}_glow", "primitives": glowing})

        node = {"name": f"{blend.stem}_glow", "mesh": len(document["meshes"]) - 1}

        # The same skin, where there is one. A candle's flame is bound to its own bone and
        # has to keep moving with it; a node carrying skinned vertices and no skin draws
        # the bind pose and never leaves it.
        if "skin" in document["nodes"][0]:
            node["skin"] = document["nodes"][0]["skin"]

        document["nodes"].append(node)
        document["scenes"][0]["nodes"].append(len(document["nodes"]) - 1)

        print(f"  glow       {len(glowing)} primitive(s) split onto their own node, "
              f"which casts no shadow")

    document["bufferViews"] = binary.views
    document["accessors"] = binary.accessors

    write_glb(destination, document, binary.blob)

    span = binary.accessors[shared["POSITION"]]
    size = [high - low for low, high in zip(span["min"], span["max"])]

    print(
        f"  triangles  {counts['triangles']}\n"
        f"  vertices   {len(attributes['POSITION'])} "
        f"(from {counts['corners']} corners)\n"
        f"  attributes {sorted(shared)}\n"
        + (f"  sheets     {', '.join(s['name'] + ' (' + s['material'] + ')' for s in slots)}"
           f"  in {len(primitives)} primitive(s), tiled\n" if slots else "")
        + f"  size       {size[0]:.3f} x {size[1]:.3f} x {size[2]:.3f} m (Y up)\n"
        + (f"  maps       {', '.join(f'{k} ({v.name})' for k, v in maps.items()) or 'none'}\n"
           if not slots else "")
        + (f"  skin       {len(document.get('skins', [{}])[0].get('joints', []))} joints\n"
           if "skins" in document else "")
        + ("".join(f"  clip       {clip}\n" for clip in clips))
        + (f"  clips      in {document['extras']['actions']}, beside the rig\n"
           if "extras" in document else "")
        + f"  wrote      {destination} ({destination.stat().st_size // 1024} KB)")


# Guarded, because this file is now read as well as run.
#
# export_actions writes the same clips out of the same rig with the same closing keys and
# the same play speeds, and there is exactly one correct version of that reasoning. It
# imports write_animations from here rather than keeping a second copy of it — and an
# unguarded main() at the bottom means importing this file exports a model, which under
# --background means Blender opening a .blend that was never named.
if __name__ == "__main__":
    main()
