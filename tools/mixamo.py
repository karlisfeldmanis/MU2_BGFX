#!/usr/bin/env python3
"""Puts a Mixamo clip onto MU's Bip01 rig, into the player's clip library, as an action number.

    Blender --background --python tools/mixamo.py -- \
        source/players/rig/mixamo/SlowRun.fbx --name=action284 --even
    tools/sync_one.py players/rig/player.actions.glb
    tools/cook.py --only figures

The clip lands in workshop/players/rig/player.actions.glb as the animation called by --name
(added, or replaced if it is there already); an `actionN` name is MU action N to the cook and
to `ClipLibrary::find`, so a new clip takes a number past MU's own 283. --seconds overrides
the clip's own length, and --even makes a cycle the same on both feet (see `even`).

Ours, and the list of them is OURS below, because a re-export writes this library afresh from
player.rig.json. It wiped the run once (2026-10-01, a re-export at 16:46, the knight walking
everywhere by evening), so now pipeline/export_actions.py puts every clip in OURS back itself,
and tools/cook.py refuses a player library missing one of them. A new clip goes in OURS.

    action284  source/players/rig/mixamo/SlowRun.fbx --even  the out-of-combat run, weapon on the back

This is MU3's tools/retarget.py by way of MU4's tools/mixamo.py (its account of the retarget is
below): a new clip's channels are cloned from MU's own walk (action15) and every one of them is
then rewritten.

Blender is here only to read the .fbx. Everything after that is arithmetic on the .glb, so
adding one clip is an edit to one animation in one glTF document, and the other 283 are copied
through untouched.

## Why it is not Mixamo's auto-rigger

Because that gives the body a `mixamorig` skeleton, and all 283 clips in this file are keyed
against `Bip01`. A track aimed at a node that is not there animates nothing, so auto-rigging
would trade the run for the whole of the knight's combat. Only the animation crosses over;
the skeleton and the bone lengths stay MU's.

## Why it is not a delta from the rest pose

The obvious retarget — take each Mixamo bone's rotation away from its rest and apply the
remainder to MU's bone — is wrong here, and wrong in a way that looks plausible in a still
and grotesque in motion.

Mixamo rests in a T-pose, with the arms straight out. MU does not: the arms hang, from
`Bip01 R UpperArm` down to `Bip01 R Hand` a third of a metre below it, and the legs are not
even symmetric — the right foot sits 17 centimetres forward of the left. A pose measured
against a T-pose and replayed against a hanging one counts the drop twice, and the knight
runs with his arms through his ribs.

So a *reference pose* is built instead, one bone at a time: the world rotation that carries
the Mixamo rest bone's direction onto MU's (`corrections` below). The clip is then read as a
departure from that, and a Mixamo frame whose arm happens to lie where MU's rest arm lies
produces no rotation at all. It is what a retargeter means by posing both rigs alike before
recording, derived from the two rest poses rather than done by hand.

The alignment is a minimal arc, so it says nothing about twist about a bone's own axis. On a
run that shows only in the forearms and hands, which is the cheapest place to be approximate.

## What is kept from MU

Bone lengths, because the answer wanted is MU's proportions moving in Mixamo's manner and not
a 1.68 m mannequin's limbs grafted on. Only rotations transfer; every translation channel
keeps the value it had. The one exception is the root, `Bip01`, which carries the body's rise
and fall: that is Mixamo's hip height, scaled by the ratio of the two rigs' hip heights, and
the horizontal is held at rest because MU pins a walk or run's travel and moves the character
itself.

The clip also keeps its length. MU's run is 0.8 seconds and this writes 26 keys across the
same 0.8 seconds, so nothing downstream that paces a character against his own stride has to
be told anything.

## Coordinates

glTF is Y-up and metres; MU's own files and Blender are both Z-up. The two differ by one
swap, `(x, y, z) -> (x, z, -y)` going into glTF, and the arithmetic here is done Z-up on both
sides because that is the space the Mixamo clip arrives in.
"""

import json
import struct
import sys
from pathlib import Path

#: The Mixamo joint that drives each MU bone. Mixamo's Spine1 has no counterpart — MU's
#: chest is two bones where Mixamo's is three — and dropping the middle one keeps the
#: shoulders on the bone that actually carries them.
DRIVERS = {
    "Bip01 Pelvis": "mixamorig:Hips",
    "Bip01 Spine": "mixamorig:Spine",
    "Bip01 Spine1": "mixamorig:Spine2",
    "Bip01 Neck": "mixamorig:Neck",
    "Bip01 Head": "mixamorig:Head",
    "Bip01 R Clavicle": "mixamorig:RightShoulder",
    "Bip01 R UpperArm": "mixamorig:RightArm",
    "Bip01 R Forearm": "mixamorig:RightForeArm",
    "Bip01 R Hand": "mixamorig:RightHand",
    "Bip01 L Clavicle": "mixamorig:LeftShoulder",
    "Bip01 L UpperArm": "mixamorig:LeftArm",
    "Bip01 L Forearm": "mixamorig:LeftForeArm",
    "Bip01 L Hand": "mixamorig:LeftHand",
    "Bip01 L Thigh": "mixamorig:LeftUpLeg",
    "Bip01 L Calf": "mixamorig:LeftLeg",
    "Bip01 L Foot": "mixamorig:LeftFoot",
    "Bip01 L Toe0": "mixamorig:LeftToeBase",
    "Bip01 R Thigh": "mixamorig:RightUpLeg",
    "Bip01 R Calf": "mixamorig:RightLeg",
    "Bip01 R Foot": "mixamorig:RightFoot",
    "Bip01 R Toe0": "mixamorig:RightToeBase",
}

#: Which child gives a bone its rest direction, where the first child is not the one meant.
#: Every other driven bone takes its first child, which is right for all of them: the spine
#: runs up before the cape chain, the clavicles come after the head, and a hand points down
#: its first finger before its weapon null.
DIRECTION_CHILD = {
    "Bip01 Pelvis": "Bip01 Spine",   # Up the spine, not down a thigh.
    "Bip01 Neck": "Bip01 Head",      # To the head, not to a clavicle.
}

#: A bone's own long axis, in its local frame. MU's rig runs its bones down +X: `Bip01 L Calf`
#: sits at [49.43, 0, 0] from the thigh that owns it.
AXIS = [1.0, 0.0, 0.0]

#: Up, in the Z-up space this does its arithmetic in.
UP = [0.0, 0.0, 1.0]

#: The MU clip whose pose stands in for the rig's rest, for the bones that need one.
#:
#: MU's walk, because it is the plainest thing the rig does: the character upright, facing his
#: own way, arms down. Only the bones in UPRIGHT read it.
NEUTRAL_CLIP = "action15"

# Every clip this tool has put in the player's library, as (name, .fbx under the project, the
# options it was made with). Read by pipeline/export_actions.py, which re-applies them after
# a re-export, and by tools/cook.py, which refuses a library missing one.
OURS = [
    ("action284", "source/players/rig/mixamo/SlowRun.fbx", ["--even"]),  # the run
]

#: Bones whose long axis is stood upright after posing, however the clip left them.
#:
#: The head, and the knight's head is the reason this exists — the user saw it, and it measures
#: out exactly. **MU holds the head bone dead vertical.** Action 15 is 0.0 degrees off vertical
#: on all seven of its frames and action 1 stays within five; MU's rest pose has it 25 degrees
#: over, and not one clip ever uses that. A MU character does not look where he is going. He
#: carries his head level and the body moves under it.
#:
#: Mixamo's runner does the opposite, and does it correctly for a runner: he leans into the
#: stride and his head goes with it. Transferred faithfully that is 11 to 22 degrees of pitch
#: through the cycle, against MU's flat zero, and on a helmeted low-poly head it reads as the
#: knight studying the ground. Both clips are right about their own game; only one of them is
#: this game.
#:
#: So the pitch is taken back out at the end, by the one rotation that stands the bone's own
#: axis up. Only the axis is corrected: whatever yaw the Mixamo clip gives the head survives,
#: and so does every other bone's lean. The neck still leans; the head sits level on it.
#:
#: **And the head's neutral is read from NEUTRAL_CLIP and not from the rig's rest**, which is
#: the other half of the same fault and the worse-looking one. The rest pose carries 25 degrees
#: of twist about the head's own axis that no MU clip uses — action 15 holds the head at 0.0
#: degrees of yaw from the pelvis, dead ahead, on every frame. Retargeting from rest handed the
#: knight that twist plus Mixamo's own head yaw on top, and measured out as a head turned 11 to
#: 40 degrees to one side and swinging 29 through the cycle. A man running with his face over
#: his shoulder.
#:
#: Neither of these is a fix to the retarget, which was transferring the clip correctly both
#: times. They are two statements about how MU carries a head, applied to a clip that came from
#: somewhere else.
UPRIGHT = {"Bip01 Head"}

#: The root, which holds the body's height and, on a walk or run, its pinned travel.
ROOT = "Bip01"

FLOAT = 5126

#: How many components each glTF element type has. MAT4 is here for the skin's inverse bind
#: matrices, which this reads and writes only to copy them through.
SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}

#: And how wide a component is, by glTF's componentType. Only floats occur in a clip library,
#: but repacking copies every accessor in the file and must not assume that.
WIDTHS = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}


def multiply(a, b):
    """Quaternion a then b, as xyzw."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def inverse(q):
    x, y, z, w = q
    return [-x, -y, -z, w]


def turn(q, v):
    """Vector v rotated by quaternion q."""
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return [vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx)]


def unit(v):
    length = sum(c * c for c in v) ** 0.5
    return [c / length for c in v] if length > 1e-9 else [0.0, 0.0, 1.0]


def between(a, b):
    """The shortest rotation carrying vector a onto vector b, as xyzw."""
    a, b = unit(a), unit(b)
    dot = sum(x * y for x, y in zip(a, b))

    if dot > 1 - 1e-9:
        return [0.0, 0.0, 0.0, 1.0]

    if dot < -1 + 1e-9:
        # Antiparallel: any perpendicular axis will do, so take one well away from a.
        other = [1.0, 0.0, 0.0] if abs(a[0]) < 0.9 else [0.0, 1.0, 0.0]
        axis = unit([a[1] * other[2] - a[2] * other[1],
                     a[2] * other[0] - a[0] * other[2],
                     a[0] * other[1] - a[1] * other[0]])
        return [*axis, 0.0]

    cross = [a[1] * b[2] - a[2] * b[1],
             a[2] * b[0] - a[0] * b[2],
             a[0] * b[1] - a[1] * b[0]]
    w = 1 + dot
    length = (sum(c * c for c in cross) + w * w) ** 0.5
    return [cross[0] / length, cross[1] / length, cross[2] / length, w / length]


def to_zup(value):
    """glTF's Y-up into the Z-up the arithmetic is done in. Four values is a quaternion."""
    if len(value) == 4:
        x, y, z, w = value
        return [x, -z, y, w]

    x, y, z = value
    return [x, -z, y]


def to_gltf(value):
    """Back the other way."""
    if len(value) == 4:
        x, y, z, w = value
        return [x, z, -y, w]

    x, y, z = value
    return [x, z, -y]


def read_glb(path: Path) -> tuple[dict, bytes]:
    raw = path.read_bytes()
    length = struct.unpack("<I", raw[12:16])[0]
    document = json.loads(raw[20:20 + length])
    binary = raw[20 + length + 8:]
    return document, binary


def read_accessor(document: dict, binary: bytes, index: int) -> list[list[float]]:
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    width = SIZES[accessor["type"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor["count"]
    values = struct.unpack_from(f"<{count * width}f", binary, start)
    return [list(values[i * width:(i + 1) * width]) for i in range(count)]


def write_glb(path: Path, document: dict, binary: bytes) -> None:
    """Rewrites the file with every accessor packed afresh, so nothing orphaned survives.

    A clip is replaced by giving its samplers new accessors, which leaves the ones they used
    to point at referenced by nothing. Repacking rather than appending means running this
    twice costs the same as running it once.
    """
    packed = bytearray()
    views = []

    for accessor in document["accessors"]:
        view = document["bufferViews"][accessor["bufferView"]]
        width = SIZES[accessor["type"]] * WIDTHS[accessor["componentType"]]
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        payload = binary[start:start + accessor["count"] * width]

        while len(packed) % 4:
            packed.append(0)

        views.append({"buffer": 0, "byteOffset": len(packed), "byteLength": len(payload)})
        accessor["bufferView"] = len(views) - 1
        accessor.pop("byteOffset", None)
        packed.extend(payload)

    while len(packed) % 4:
        packed.append(0)

    document["bufferViews"] = views
    document["buffers"] = [{"byteLength": len(packed)}]

    chunk = json.dumps(document, separators=(",", ":")).encode()
    chunk += b" " * (-len(chunk) % 4)

    header = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(chunk) + 8 + len(packed))
    path.write_bytes(header + struct.pack("<II", len(chunk), 0x4E4F534A) + chunk
                     + struct.pack("<II", len(packed), 0x004E4942) + bytes(packed))


def add_accessor(document: dict, extra: bytearray, kind: str, values) -> int:
    """One more accessor, its bytes parked on the end until write_glb repacks them."""
    width = SIZES[kind]
    flat = [c for value in values for c in value]
    payload = struct.pack(f"<{len(flat)}f", *flat)

    document["bufferViews"].append(
        {"buffer": 0, "byteOffset": len(extra), "byteLength": len(payload), "_extra": True})
    document["accessors"].append({
        "bufferView": len(document["bufferViews"]) - 1,
        "componentType": FLOAT,
        "count": len(values),
        "type": kind,
        "min": [min(v[i] for v in values) for i in range(width)],
        "max": [max(v[i] for v in values) for i in range(width)],
    })
    extra.extend(payload)
    return len(document["accessors"]) - 1


def read_mixamo(path: Path) -> dict:
    """The clip's rest pose and per-frame world rotations, read through Blender. Z-up."""
    import bpy                                                       # noqa: PLC0415
    from mathutils import Vector                                     # noqa: PLC0415

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(path))

    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")

    if any(o.type == "MESH" for o in bpy.data.objects):
        print("note: the .fbx carries a skin; only its animation is read")

    action = armature.animation_data.action
    first, last = (int(round(v)) for v in action.frame_range)

    rest = {}
    for bone in armature.data.bones:
        matrix = armature.matrix_world @ bone.matrix_local
        rotation = matrix.to_quaternion()
        head = matrix.translation
        tail = armature.matrix_world @ Vector(bone.tail_local)
        rest[bone.name] = {
            "q": [rotation.x, rotation.y, rotation.z, rotation.w],
            "head": list(head),
            "direction": [tail[i] - head[i] for i in range(3)],
        }

    frames = []
    for number in range(first, last + 1):
        bpy.context.scene.frame_set(number)
        frames.append({
            bone.name: {
                "q": [(armature.matrix_world @ bone.matrix).to_quaternion()[i]
                      for i in (1, 2, 3, 0)],
                "head": list((armature.matrix_world @ bone.matrix).translation),
            }
            for bone in armature.pose.bones
        })

    return {"fps": bpy.context.scene.render.fps, "rest": rest, "frames": frames}


def even(clip: dict) -> dict:
    """The clip made the same on both feet: each frame half-way to its own mirror image half
    a cycle on.

    Motion capture limps. Mixamo's Slow Run lifts the hips 4 cm higher off the left foot than
    off the right, and lands the feet 10 frames apart and then 12, so its footsteps come
    short-long like a man favouring a leg. Blending frame f with the mirror of frame f + N/2
    gives a cycle whose second half IS the mirror of its first, whatever the capture did, and
    it still loops, being built from the whole cycle and not by pasting one half onto the other.

    Done here and not on MU's side because Mixamo's rest is a symmetric T-pose, so a bone's
    motion is its rotation away from rest and its partner's is the same motion reflected.
    MU's rest is not symmetric (see the module docstring). Sideways is x, left at +x.
    """
    frames = clip["frames"]
    rest = clip["rest"]
    cycle = len(frames) - 1                        # the last key is the first pose again
    if cycle % 2:
        raise SystemExit(f"--even wants an even cycle, this one is {cycle} frames")

    def partner(bone: str) -> str:
        if "Left" in bone:
            return bone.replace("Left", "Right")
        return bone.replace("Right", "Left")

    def reflected(q):                              # S q S with S = diag(-1, 1, 1)
        return [q[0], -q[1], -q[2], q[3]]

    def mirror(frame: dict) -> dict:
        out = {}
        for bone in frame:
            other = partner(bone)
            if other not in frame or bone not in rest or other not in rest:
                out[bone] = frame[bone]
                continue
            motion = multiply(frame[other]["q"], inverse(rest[other]["q"]))
            head = frame[other]["head"]
            out[bone] = {"q": multiply(reflected(motion), rest[bone]["q"]),
                         "head": [-head[0], head[1], head[2]]}
        return out

    def halfway(a, b):
        if sum(x * y for x, y in zip(a, b)) < 0:
            b = [-c for c in b]
        return unit([(x + y) / 2 for x, y in zip(a, b)])

    evened = []
    for f in range(cycle):
        one, other = frames[f], mirror(frames[(f + cycle // 2) % cycle])
        evened.append({bone: {"q": halfway(one[bone]["q"], other[bone]["q"]),
                              "head": [(x + y) / 2 for x, y in
                                       zip(one[bone]["head"], other[bone]["head"])]}
                       for bone in one})
    evened.append(evened[0])
    return {**clip, "frames": evened}


def rest_pose(document: dict) -> tuple[dict, dict, dict]:
    """Every joint's rest rotation and position in the rig's own Z-up space, and its parent."""
    nodes = document["nodes"]
    named = {node["name"]: index for index, node in enumerate(nodes) if "name" in node}
    parent = {}

    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parent[child] = index

    world: dict[int, tuple[list[float], list[float]]] = {}

    def resolve(index: int):
        if index in world:
            return world[index]

        node = nodes[index]
        local_t = to_zup(node.get("translation", [0.0, 0.0, 0.0]))
        local_r = to_zup(node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
        above = parent.get(index)

        if above is None or "name" not in nodes[above]:
            world[index] = (local_t, local_r)
        else:
            up_t, up_r = resolve(above)
            offset = turn(up_r, local_t)
            world[index] = ([up_t[i] + offset[i] for i in range(3)], multiply(up_r, local_r))

        return world[index]

    for index in range(len(nodes)):
        resolve(index)

    return named, {index: world[index] for index in world}, parent


def clip_pose(document: dict, binary: bytes, name: str, parent: dict,
              world: dict) -> dict[int, list[float]]:
    """Every joint's world rotation on a MU clip's first frame, Z-up.

    For the bones whose rest pose is not the pose MU's own clips depart from. Frame 0 rather
    than an average: MU's clips are six or seven keys and any of them is the character
    standing in his own rig, which is the whole of what is wanted.
    """
    animation = next((a for a in document["animations"] if a.get("name") == name), None)

    if animation is None:
        return {}

    local = {}
    for channel in animation["channels"]:
        if channel["target"]["path"] == "rotation":
            sampler = animation["samplers"][channel["sampler"]]
            local[channel["target"]["node"]] = to_zup(
                read_accessor(document, binary, sampler["output"])[0])

    posed: dict[int, list[float]] = {}

    def resolve(index: int) -> list[float]:
        if index in posed:
            return posed[index]

        nodes = document["nodes"]
        mine = local.get(index, to_zup(nodes[index].get("rotation", [0.0, 0.0, 0.0, 1.0])))
        above = parent.get(index)
        posed[index] = (multiply(resolve(above), mine)
                        if above is not None and "name" in nodes[above] else mine)
        return posed[index]

    for index in world:
        resolve(index)

    return posed


def main(argv=None) -> None:
    if argv is None:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    def option(flag: str, default=None):
        return next((a.split("=", 1)[1] for a in argv if a.startswith(flag + "=")), default)

    if not argv or argv[0].startswith("--"):
        print("usage: ... --python mixamo.py -- <clip.fbx> --name=action<N> "
              "[--seconds=<s>] [--library=<rig.glb>]")
        return

    here = Path(__file__).resolve().parent.parent
    clip_path = Path(argv[0])
    name = option("--name")
    seconds_wanted = option("--seconds")
    library_path = Path(option("--library",
                               str(here / "workshop/players/rig/player.actions.glb")))
    out_path = Path(option("--out", str(library_path)))

    if name is None:
        print("error: --name=<clip> says what the clip is called in the library")
        return

    document, binary = read_glb(library_path)
    animation = next((a for a in document["animations"] if a.get("name") == name), None)
    added = animation is None

    if added:
        # A new clip: MU's walk lends its channel layout, every sampler of which is rewritten
        # below, so what is borrowed is only which node each channel drives and how.
        template = next(a for a in document["animations"] if a.get("name") == NEUTRAL_CLIP)
        animation = {
            "name": name,
            "samplers": [dict(s) for s in template["samplers"]],
            "channels": [{"sampler": c["sampler"], "target": dict(c["target"])}
                         for c in template["channels"]],
        }
        document["animations"].append(animation)

    nodes = document["nodes"]
    named, world, parent = rest_pose(document)
    clip = read_mixamo(clip_path)
    if "--even" in argv:
        clip = even(clip)

    keys = len(clip["frames"])
    if seconds_wanted is not None:
        seconds = float(seconds_wanted)
    elif added:
        seconds = (keys - 1) / clip["fps"]
    else:
        # Replacing one of MU's: keep the length MU gave it, so nothing that paces a
        # character against his own stride has to be told about the new key count.
        was = read_accessor(document, binary, animation["samplers"][0]["input"])
        seconds = was[-1][0]
    times = [[i * seconds / (keys - 1)] for i in range(keys)]

    standing = clip_pose(document, binary, NEUTRAL_CLIP, parent, world)

    # The reference pose, one bone at a time. See the module docstring for why this and not
    # each rig's own rest.
    corrections = {}

    # Where each driven bone is taken to rest. The rig's own rest, except where that is not
    # the pose MU's clips depart from; see UPRIGHT.
    neutral = {}

    for bone, driver in DRIVERS.items():
        index = named.get(bone)

        if index is None or driver not in clip["rest"]:
            continue

        neutral[bone] = world[index][1]

        if bone in UPRIGHT:
            neutral[bone] = standing.get(index, neutral[bone])
            corrections[bone] = between(clip["rest"][driver]["direction"], UP)
            continue

        child = DIRECTION_CHILD.get(bone)
        below = named[child] if child else next(
            (c for c in nodes[index].get("children", []) if "name" in nodes[c]), None)

        if below is None:
            corrections[bone] = [0.0, 0.0, 0.0, 1.0]
            continue

        direction = [world[below][0][i] - world[index][0][i] for i in range(3)]
        corrections[bone] = between(clip["rest"][driver]["direction"], direction)

    def posed(bone: str, frame: dict) -> list[float]:
        """A driven bone's world rotation on this frame, Z-up."""
        driver = DRIVERS[bone]
        reference = multiply(corrections[bone], clip["rest"][driver]["q"])
        delta = multiply(frame[driver]["q"], inverse(reference))
        turned = multiply(delta, neutral[bone])

        if bone in UPRIGHT:
            turned = multiply(between(turn(turned, AXIS), UP), turned)

        return turned

    hip_rest = clip["rest"]["mixamorig:Hips"]["head"][2]
    rise_scale = world[named["Bip01 Pelvis"]][0][2] / hip_rest
    root_rest = nodes[named[ROOT]].get("translation", [0.0, 0.0, 0.0])

    extra = bytearray()
    clock = add_accessor(document, extra, "SCALAR", times)
    driven = 0

    for channel in animation["channels"]:
        node = channel["target"]["node"]
        bone = nodes[node].get("name", "")
        path = channel["target"]["path"]
        sampler = animation["samplers"][channel["sampler"]]
        sampler["input"] = clock

        # What the rig rests at, for everything Mixamo says nothing about. Read off the node
        # and not the template's first key, so a new clip does not inherit the walk's pose.
        if path == "rotation":
            held = list(nodes[node].get("rotation", [0.0, 0.0, 0.0, 1.0]))
        else:
            held = list(nodes[node].get("translation", [0.0, 0.0, 0.0]))

        if bone == ROOT and path == "translation":
            values = []
            for frame in clip["frames"]:
                rise = (frame["mixamorig:Hips"]["head"][2] - hip_rest) * rise_scale
                values.append([root_rest[0], root_rest[1] + rise, root_rest[2]])
            sampler["output"] = add_accessor(document, extra, "VEC3", values)
            continue

        if path == "rotation" and bone in corrections:
            values = []
            for frame in clip["frames"]:
                above = parent.get(node)
                up = (posed(nodes[above]["name"], frame)
                      if above is not None and nodes[above].get("name") in corrections
                      else world[above][1] if above is not None and "name" in nodes[above]
                      else [0.0, 0.0, 0.0, 1.0])
                values.append(to_gltf(multiply(inverse(up), posed(bone, frame))))
            sampler["output"] = add_accessor(document, extra, "VEC4", values)
            driven += 1
            continue

        kind = "VEC4" if path == "rotation" else "VEC3"
        sampler["output"] = add_accessor(document, extra, kind, [held] * keys)

    for view in document["bufferViews"]:
        if view.pop("_extra", False):
            view["byteOffset"] += len(binary)

    write_glb(out_path, document, binary + bytes(extra))
    print(f"{name}: {'added' if added else 'replaced'}, {keys} keys over {seconds:.3f}s, "
          f"from {clip_path.name} at {clip['fps']} fps; {driven} bones driven, "
          f"{len(animation['channels'])} channels")


if __name__ == "__main__":
    main()
