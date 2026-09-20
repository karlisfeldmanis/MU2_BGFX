#!/usr/bin/env python3
"""Checks the pose arithmetic against the one pose whose answer is known: the bind.

The rest pose IS the bind pose in MU2's exports -- every joint's composed world matrix times
its own inverse bind is the identity, to 1e-5, on every figure measured in
`docs/sprints/04-figures.census.md`. So composing the rest transforms with the SAME
conventions `src/core/maths.h` uses -- a row-vector quaternion matrix, `local x parent`, then
`inverseBind x world` -- must give sixty identities. If the quaternion is transposed, or the
multiply is the other way round, or the inverse bind is applied on the wrong side, this
prints it as a number instead of the screen printing it as a figure turned inside out.

It is a second implementation of the same arithmetic in a different language, which is the
point: `Figure::pose` agreeing with itself is not evidence.

    tools/posecheck.py [--rig assets/players/rig/player.actions.glb]
                       [--mesh assets/cooked/figures/meshes/ArmorMale10.mum]
                       [--clip action15]      # and then pose that clip's first frame instead

Exits non-zero when the bind does not come back as the identity.
"""

import argparse
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def read_string(data, at):
    (length,) = struct.unpack_from("<H", data, at)
    return data[at + 2:at + 2 + length].decode("utf-8"), at + 2 + length


def read_mum_bones(path):
    """The skeleton of a skinned .mum: (name, parent, inverse bind) a bone."""
    data = open(path, "rb").read()
    magic, version, vertices, indices, parts, materials = struct.unpack_from("<4sIIIII", data, 0)
    if magic != b"MU2M":
        raise ValueError(f"{path} is not a .mum")
    if version != 2:
        raise ValueError(f"{path} is version {version}; only a skinned version 2 has bones")
    at = 24 + 24
    (bones,) = struct.unpack_from("<I", data, at)
    at += 4 + vertices * 56 + indices * 4 + parts * 12
    for _ in range(materials):
        at += 5
        for _ in range(5):
            _text, at = read_string(data, at)
    out = []
    for _ in range(bones):
        name, at = read_string(data, at)
        parent = struct.unpack_from("<i", data, at)[0]
        inverse = struct.unpack_from("<16f", data, at + 4)
        at += 68
        out.append((name, parent, list(inverse)))
    return out


def read_muc(path):
    """(bone names, clip table, pose rows) of a .muc."""
    data = open(path, "rb").read()
    magic, version, clips, bones = struct.unpack_from("<4sIII", data, 0)
    if magic != b"MU2C" or version != 1:
        raise ValueError(f"{path} is not a version 1 .muc")
    at = 16
    names = []
    for _ in range(bones):
        name, at = read_string(data, at)
        names.append(name)
    table = []
    for _ in range(clips):
        name, at = read_string(data, at)
        label, at = read_string(data, at)
        slot, frames, duration, travel, hold = struct.unpack_from("<iIffI", data, at)
        at += 20
        table.append({"name": name, "label": label, "slot": slot, "frames": frames,
                      "duration": duration, "travel": travel, "hold": hold})
    return names, table, data[at:]


def rest_pose(glb_path):
    """Each joint's rest rotation and translation, in the skin's own order."""
    with open(glb_path, "rb") as handle:
        struct.unpack("<III", handle.read(12))
        length, _kind = struct.unpack("<II", handle.read(8))
        document = json.loads(handle.read(length))
    nodes = document["nodes"]
    joints = document["skins"][0]["joints"]
    return [(tuple(nodes[j].get("rotation", (0.0, 0.0, 0.0, 1.0))),
             tuple(nodes[j].get("translation", (0.0, 0.0, 0.0)))) for j in joints], \
           [nodes[j].get("name", "") for j in joints]


def quat_to_matrix(q):
    """src/core/maths.h's own: row-vector, `v * M`."""
    x, y, z, w = q
    return [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
            0, 0, 0, 1]


def compose(rotation, translation):
    m = quat_to_matrix(rotation)
    m[12], m[13], m[14] = translation
    return m


def multiply(a, b):
    return [sum(a[row * 4 + k] * b[k * 4 + column] for k in range(4))
            for row in range(4) for column in range(4)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesh", default=os.path.join(
        ROOT, "assets/cooked/figures/meshes/ArmorMale10.mum"))
    parser.add_argument("--rig", default=os.path.join(
        ROOT, "assets/players/rig/player.actions.glb"))
    parser.add_argument("--clips", default=os.path.join(
        ROOT, "assets/cooked/figures/clips/player.muc"))
    parser.add_argument("--clip", default="", help="pose this clip's first frame as well")
    parser.add_argument("--tolerance", type=float, default=1e-4)
    args = parser.parse_args()

    bones = read_mum_bones(args.mesh)
    pose, rig_names = rest_pose(args.rig)
    print(f"posecheck: {os.path.basename(args.mesh)} has {len(bones)} bones, "
          f"{os.path.basename(args.rig)} has {len(pose)}")

    order = {name: index for index, name in enumerate(rig_names)}
    failures = 0
    worst = 0.0
    worst_bone = ""
    world = [None] * len(bones)
    for index, (name, parent, inverse) in enumerate(bones):
        at = order.get(name)
        if at is None:
            print(f"  FAIL {name} is not in the rig")
            failures += 1
            world[index] = IDENTITY
            continue
        rotation, translation = pose[at]
        local = compose(rotation, translation)
        world[index] = local if parent < 0 else multiply(local, world[parent])
        skin = multiply(inverse, world[index])
        error = max(abs(a - b) for a, b in zip(skin, IDENTITY))
        if error > worst:
            worst, worst_bone = error, name
    if worst > args.tolerance:
        print(f"  FAIL the bind pose does not come back as the identity: {worst:.6f} on "
              f"{worst_bone}")
        failures += 1
    else:
        print(f"  the bind pose composes to the identity, worst {worst:.2e} on {worst_bone}")

    if args.clip:
        names, table, rows = read_muc(args.clips)
        index = next((i for i, one in enumerate(table)
                      if args.clip in (one["name"], one["label"])), -1)
        if index < 0:
            print(f"  FAIL no clip called {args.clip}")
            failures += 1
        else:
            clip = table[index]
            first = sum(one["frames"] for one in table[:index]) * len(names)
            moved = 0
            for bone in range(len(names)):
                values = struct.unpack_from("<7f", rows, (first + bone) * 28)
                if any(abs(a - b) > 1e-5 for a, b in zip(values[:4], pose[bone][0])):
                    moved += 1
            print(f"  {clip['name']} ({clip['label']}): {clip['frames']} frames, "
                  f"{clip['duration']:.3f} s, travel {clip['travel']:.4f} m, "
                  f"{'holds' if clip['hold'] else 'loops'}; {moved} of {len(names)} bones "
                  f"differ from the bind on its first frame")

    print(f"posecheck: {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
