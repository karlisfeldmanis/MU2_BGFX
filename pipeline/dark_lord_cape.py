"""Writes the Dark Lord's worn Cape of Lord: content/items/armor/DarkLordCape.obj and its rig.

    python3 pipeline/dark_lord_cape.py

MuMain never draws a cape mesh on him. Its cape is cloth #2 of his cloth set, a 10x10 sheet
180x180 units pinned to bone 19 and simulated every frame (ZzzCharacter.cpp:9691-9765,
Engine/Physics/PhysicsManager.cpp; docs/plans/dark-lord-cape.md section 4). The user, 2026-10-25:
"a rigged cape first", cloth later if wanted. So this is a sheet in the cloth's shape, skinned
rigidly (one bone a vertex, as every MU part is) to the bones of his own rig, so it moves with
his clips:

- its shape is the cloth's: SHORT_SHOULDER's trapezoid (0.6 of the hem's width at the top,
  PhysicsManager.cpp:367-372) and CURVED's bow, the edges brought forward by the square of their
  distance from the middle (:395-400); shorter than MU's 180, which hangs that long only because
  the simulation lifts it into the wind;
- its sheet is MU's own, DarkLordRobe.OZT (the same bytes as Player/DarklordRobe.OZT, the cloth's
  BITMAP_ROBE+7), mapped whole as the cloth maps it, (i/(cols-1), j/(rows-1)); the sheet's alpha
  cuts the trapezoid;
- above the hip joint its rows ride Spine1, Spine and the Pelvis by height; below it the sheet
  splits down the middle into two tails, each on its thigh, overlapping so the split does not
  show at rest, so a stride swings a tail instead of tearing the sheet.

Positions are the bare Dark Lord's at action 0 key 0 (content/players/body/*Class05.obj): Y up,
his toes toward +Z, his back toward -Z (the torso's back at z -15, the hips at y 100).
The rig is ArmorClass05's, the first 51 bones of player.bmd.
"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BODY = ROOT / "content" / "players" / "body"
OUT = ROOT / "content" / "items" / "armor"
NAME = "DarkLordCape"
SHEET = "darklordrobe"

# Rows by height, top to hem, in MU units; the hip joint row is shared by both tails.
TOP, HIP, HEM = 170.0, 100.0, 34.0
UPPER = [170, 160, 150, 140, 130, 120, 110, 100]
LOWER = [88, 76, 64, 52, 42, 34]
COLUMNS = 9           # across the whole sheet
WIDTH_TOP, WIDTH_HEM = 56.0, 108.0
# How far behind him: just off the shoulder blades, out over the hips, trailing at the hem.
BACK_TOP, BACK_HIP, BACK_HEM = -19.0, -19.5, -26.0
BOW_TOP, BOW_HEM = 5.0, 3.0
# Where the two tails overlap, in u either side of the middle, and the left one's step back.
OVERLAP = 0.06
TAIL_STEP = 0.6

# The bones (ArmorClass05.rig.json).
SPINE1, SPINE, PELVIS, L_THIGH, R_THIGH = 18, 17, 2, 3, 10


def along(y):
    """0 at the top row, 1 at the hem."""
    return (TOP - y) / (TOP - HEM)


def back(y):
    if y >= HIP:
        return BACK_TOP + (BACK_HIP - BACK_TOP) * (TOP - y) / (TOP - HIP)
    return BACK_HIP + (BACK_HEM - BACK_HIP) * (HIP - y) / (HIP - HEM)


def point(u, y, step=0.0):
    t = along(y)
    width = WIDTH_TOP + (WIDTH_HEM - WIDTH_TOP) * t
    bow = BOW_TOP + (BOW_HEM - BOW_TOP) * t
    # u 0 is the sheet's left as seen from behind him, which is his left, +X.
    x = width * (0.5 - u)
    z = back(y) + bow * (2.0 * abs(u - 0.5)) ** 2 - step
    return (x, y, z)


def bone_above(y):
    if y >= 140:
        return SPINE1
    if y >= 112:
        return SPINE
    return PELVIS


def main():
    verts, uvs, bones, faces = [], [], [], []

    def sheet(rows, us, bone_of, step=0.0):
        start = len(verts)
        for y in rows:
            for u in us:
                verts.append(point(u, y, step))
                # OBJ's v runs up: the sheet's top row, the cape's shoulders, is v 1.
                uvs.append((min(max(u, 0.001), 0.999), min(max(1.0 - along(y), 0.001), 0.999)))
                bones.append(bone_of(y))
        width = len(us)
        for j in range(len(rows) - 1):
            for i in range(width - 1):
                a = start + j * width + i
                b, c, d = a + 1, a + width, a + width + 1
                faces.append((a, c, b))
                faces.append((b, c, d))

    whole = [i / (COLUMNS - 1) for i in range(COLUMNS)]
    sheet(UPPER, whole, bone_above)
    # The tails: the hip row again on the pelvis, the rows under it on the thigh.
    half = (COLUMNS - 1) // 2
    left = [i / (COLUMNS - 1) for i in range(half)] + [0.5 + OVERLAP]
    right = [0.5 - OVERLAP] + [i / (COLUMNS - 1) for i in range(half + 1, COLUMNS)]
    tail_rows = [UPPER[-1]] + LOWER
    sheet(tail_rows, left, lambda y: PELVIS if y >= HIP else L_THIGH, TAIL_STEP)
    sheet(tail_rows, right, lambda y: PELVIS if y >= HIP else R_THIGH)

    lines = [f"# {NAME} -- the Dark Lord's worn Cape of Lord, written by pipeline/dark_lord_cape.py",
             "# 1 mesh(es)", "", f"g {SHEET}", "# texture: DarkLordRobe.tga"]
    lines += [f"v {x:.4f} {y:.4f} {z:.4f}" for x, y, z in verts]
    lines += ["vn 0 0 -1" for _ in verts]
    lines += [f"vt {u:.4f} {v:.4f}" for u, v in uvs]
    lines += [f"f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}"
              for a, b, c in faces]
    (OUT / f"{NAME}.obj").write_text("\n".join(lines) + "\n")

    rig = json.loads((BODY / "ArmorClass05.rig.json").read_text())
    rig["source"] = f"{NAME} (pipeline/dark_lord_cape.py) on ArmorClass05.bmd's skeleton"
    rig["meshes"] = [{"texture": "DarkLordRobe.tga", "group": SHEET, "vertices": len(verts),
                      "vertex_bones": bones}]
    (OUT / f"{NAME}.rig.json").write_text(json.dumps(rig, indent=1) + "\n")
    print(f"{NAME}: {len(verts)} vertices, {len(faces)} triangles")


if __name__ == "__main__":
    main()
