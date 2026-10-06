"""Joins two MuExtract exports drawn as one figure into one body: their OBJs and their rigs.

    python3 pipeline/merge_rigs.py A.obj A.rig.json B.obj B.rig.json PREFIX out.obj out.rig.json

MU draws a few monsters as two models on one character: the Dark Phoenix is MODEL_DARK_PHEONIX_SHIELD
(Monster56, the bird) with Monster57 (the rider) rendered over it by `o->Type++`
(ZzzCharacter.cpp:8890-8925). Each model plays its own clip at the character's one action and
frame, so the two need the same actions with the same key counts -- this refuses otherwise.

B's bones follow A's, renamed PREFIX + name (the two share Biped names), their parents and the
vertex bones of B's meshes moved past A's; each action's tracks are A's then B's. The OBJ is A's,
then B's groups with their v/vt/vn indices moved past A's.
"""
import json
import sys


def read_obj(path):
    lines = open(path).read().splitlines()
    counts = {"v": 0, "vt": 0, "vn": 0}
    for line in lines:
        head = line.split(" ", 1)[0]
        if head in counts:
            counts[head] += 1
    return lines, counts


def shift_face(line, dv, dt, dn):
    out = ["f"]
    for corner in line.split()[1:]:
        parts = corner.split("/")
        moved = [str(int(parts[0]) + dv)]
        if len(parts) > 1:
            moved.append(str(int(parts[1]) + dt) if parts[1] else "")
        if len(parts) > 2:
            moved.append(str(int(parts[2]) + dn) if parts[2] else "")
        out.append("/".join(moved))
    return " ".join(out)


def main(argv):
    if len(argv) != 8:
        print(__doc__, file=sys.stderr)
        return 2
    a_obj, a_rig, b_obj, b_rig, prefix, out_obj, out_rig = argv[1:]
    ra, rb = json.load(open(a_rig)), json.load(open(b_rig))
    keys_a = [(x["action"], x["keys"]) for x in ra["animations"]]
    keys_b = [(x["action"], x["keys"]) for x in rb["animations"]]
    if keys_a != keys_b:
        print(f"merge_rigs: the actions differ: {keys_a} against {keys_b}", file=sys.stderr)
        return 1
    offset = len(ra["skeleton"])
    merged = dict(ra)
    merged["source"] = f"{ra['source']} + {rb['source']}"
    skeleton = list(ra["skeleton"])
    for bone in rb["skeleton"]:
        one = dict(bone)
        one["index"] = bone["index"] + offset
        one["name"] = prefix + bone["name"]
        one["parent"] = bone["parent"] + offset if bone["parent"] >= 0 else -1
        one["source"] = bone.get("source", bone["index"]) + offset
        skeleton.append(one)
    merged["skeleton"] = skeleton
    merged["bones"] = len(skeleton)
    meshes = list(ra["meshes"])
    for mesh in rb["meshes"]:
        one = dict(mesh)
        one["vertex_bones"] = [b + offset for b in mesh["vertex_bones"]]
        meshes.append(one)
    merged["meshes"] = meshes
    animations = []
    for x, y in zip(ra["animations"], rb["animations"]):
        one = dict(x)
        tracks = list(x["tracks"])
        for track in y["tracks"]:
            moved = dict(track)
            moved["bone"] = track["bone"] + offset
            tracks.append(moved)
        one["tracks"] = tracks
        animations.append(one)
    merged["animations"] = animations
    json.dump(merged, open(out_rig, "w"))

    lines_a, counts = read_obj(a_obj)
    lines_b, _ = read_obj(b_obj)
    out = list(lines_a)
    out.append(f"# merged: {b_obj.split('/')[-1]} after it, its bones prefixed '{prefix}'")
    for line in lines_b:
        if line.startswith("f "):
            out.append(shift_face(line, counts["v"], counts["vt"], counts["vn"]))
        elif line.startswith("#") and "mesh(es)" in line:
            continue
        else:
            out.append(line)
    open(out_obj, "w").write("\n".join(out) + "\n")
    print(f"merge_rigs: {len(skeleton)} bones ({offset} + {len(rb['skeleton'])}), "
          f"{len(meshes)} meshes, {len(animations)} actions -> {out_obj}, {out_rig}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
