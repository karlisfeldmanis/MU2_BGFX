#!/usr/bin/env python3
"""Bakes an Archangel weapon into a Statue of Saint's mesh, where MuMain links it.

    tools/merge_statue_weapon.py STATUE.obj STATUE.rig.json WEAPON.obj OUT.obj \\
        --angle 90 0 90 --offset 0 80 120 --scale 0.7

RenderLinkObject for MONSTER_STATUE_OF_SAINT_1..3 (ZzzCharacter.cpp:6684-6699, the Link
branch): Matrix = AngleMatrix(angle) with the offset as its translation, and ParentMatrix =
BoneTransform[1] * Matrix; the weapon is drawn at BodyScale `scale` (0.7 for the staff and the
sword, 0.9 for the crossbow, :8952-8966) against the statue's 0.8 -- so in the statue's own
model space a weapon vertex is scale/0.8 * Bone01 * Matrix * v. Done in MU's axes (z up); the
OBJs are in the engine's (x, z, -y), MuExtract's AxisConvention.

The statue is stone and never moves, and a figure with no clip carries no bone matrix for a
held item, so the weapon is part of its mesh. The weapon's groups come out named `weapon_<its
group>`, with its own `# texture:` lines. First written for the Divine Staff as merge_staff.py
(2026-10-03, SaintStatue01); the sword's and the crossbow's statues are the other two.
"""
import argparse
import json
import math

STATUE_SCALE = 0.8  # ZzzCharacter.cpp:13416-13423, MONSTER_STATUE_OF_SAINT: Scale = 0.8f


def angle_matrix(a):  # MuMain ZzzMathLib.cpp:194, rows of a 3x3
    sy, cy = math.sin(math.radians(a[2])), math.cos(math.radians(a[2]))
    sp, cp = math.sin(math.radians(a[1])), math.cos(math.radians(a[1]))
    sr, cr = math.sin(math.radians(a[0])), math.cos(math.radians(a[0]))
    return [[cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy],
            [cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy],
            [-sp, sr * cp, cr * cp]]


def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def app(m, v):
    return [sum(m[i][k] * v[k] for k in range(3)) for i in range(3)]


def add(a, b):
    return [a[i] + b[i] for i in range(3)]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("statue_obj")
    parser.add_argument("statue_rig")
    parser.add_argument("weapon_obj")
    parser.add_argument("out_obj")
    parser.add_argument("--angle", type=float, nargs=3, required=True)
    parser.add_argument("--offset", type=float, nargs=3, required=True)
    parser.add_argument("--scale", type=float, required=True)
    args = parser.parse_args()

    rig = json.load(open(args.statue_rig))
    world = []
    for bone in rig["skeleton"]:
        r, t = quat_matrix(bone["r"]), bone["t"]
        if bone["parent"] < 0:
            world.append((r, t))
        else:
            pr, pt = world[bone["parent"]]
            world.append((mul(pr, r), add(app(pr, t), pt)))
    br, bt = world[1]  # Bone01, the statue's LinkBone 1
    mr = angle_matrix(args.angle)
    k = args.scale / STATUE_SCALE

    def to_mu(e):
        return [e[0], -e[2], e[1]]

    def to_engine(m):
        return [m[0], m[2], -m[1]]

    def place(e):
        return to_engine([k * c for c in add(app(br, add(app(mr, to_mu(e)), args.offset)), bt)])

    def turn(e):
        n = app(br, app(mr, to_mu(e)))
        length = math.sqrt(sum(c * c for c in n)) or 1.0
        return to_engine([c / length for c in n])

    src = open(args.statue_obj).read().splitlines()
    nv = sum(1 for line in src if line.startswith("v "))
    nt = sum(1 for line in src if line.startswith("vt "))
    nn = sum(1 for line in src if line.startswith("vn "))
    out = ["", "# An Archangel weapon, baked in where MuMain links it (tools/merge_statue_weapon.py)"]
    counts = {}
    group = None
    for line in open(args.weapon_obj).read().splitlines():
        if line.startswith("g "):
            group = "weapon_" + line.split()[1]
            out.append("g " + group)
            counts[group] = 0
        elif line.startswith("# texture:"):
            out.append(line)
        elif line.startswith("v "):
            out.append("v %.6f %.6f %.6f" % tuple(place(list(map(float, line.split()[1:4])))))
            counts[group] += 1
        elif line.startswith("vn "):
            out.append("vn %.6f %.6f %.6f" % tuple(turn(list(map(float, line.split()[1:4])))))
        elif line.startswith("vt "):
            out.append(line)
        elif line.startswith("f "):
            parts = []
            for p in line.split()[1:]:
                a = p.split("/")
                a[0] = str(int(a[0]) + nv)
                if len(a) > 1 and a[1]:
                    a[1] = str(int(a[1]) + nt)
                if len(a) > 2 and a[2]:
                    a[2] = str(int(a[2]) + nn)
                parts.append("/".join(a))
            out.append("f " + " ".join(parts))
    open(args.out_obj, "w").write("\n".join(src + out) + "\n")
    print(counts)


if __name__ == "__main__":
    main()
