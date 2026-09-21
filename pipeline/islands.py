"""Finds the pieces an item is made of, and writes the file that says what each one is.

    Blender --background --python pipeline/islands.py -- \
        workshop/items/Sword01/Sword01_low.blend source/items/Sword01/Sword01.json

A material has to be attached to *something*, and the something this pipeline uses is the
UV island: a run of faces joined across the surface and cut off from the rest by a seam.
That is not an arbitrary choice of unit. clean_lowpoly already had to decide where an item
stops being one surface and starts being another — a blade cannot be unwrapped flat
together with the grip it is bolted to — and the seams it cut are exactly those places.
The parts an item has and the parts a bake needs are the same parts.

Sword01 comes out as nine of them, and reading them off is how the sword turns out to have
four parts rather than one: twenty-two triangles of blade on each face, a four-piece
crossguard, five triangles of grip a side, and a pommel cap. Nothing in the mesh says
"blade". What it says is "these twenty-two triangles are one surface, they run from Z 12.7
to Z 64.7, and they sample the left two thirds of MU's sheet", which is enough.

This writes a JSON file listing them, each with a material and, beside it, the measurements
it was identified by. Run it again on a mesh that has been re-unwrapped and the assignments
already made are kept where the island still matches and flagged where it does not — see
`merge`. That is the whole reason the measurements are written down: an island number on
its own is a promise about face ordering, and face ordering is not a promise this pipeline
can keep across a re-unwrap.
"""

import json
import sys
from pathlib import Path

import bmesh
import bpy

#: The layout everything is baked into. See clean_lowpoly.
BAKE_UV = "bake"

#: MU's own coordinates, which say which part of its sheet an island samples.
ORIGINAL_UV = "mu_original"

#: What an unassigned island gets. Deliberately not one of the real materials: an item
#: that has never been looked at should say so rather than quietly render as steel.
UNASSIGNED = "unassigned"


def islands_of(bm) -> list[list[int]]:
    """Faces grouped into runs that share a surface, split at the seams.

    A flood fill over faces, crossing an edge only where the unwrap did — so an edge
    marked as a seam, or one with only one face on it, stops the fill. This is the same
    partition the unwrapper used, recovered from the mesh rather than recorded by it.
    """
    bm.faces.ensure_lookup_table()

    neighbours: dict[int, set[int]] = {face.index: set() for face in bm.faces}
    for edge in bm.edges:
        if edge.seam or len(edge.link_faces) != 2:
            continue

        first, second = edge.link_faces
        neighbours[first.index].add(second.index)
        neighbours[second.index].add(first.index)

    found: list[list[int]] = []
    seen: set[int] = set()

    for face in bm.faces:
        if face.index in seen:
            continue

        stack = [face.index]
        group: list[int] = []

        while stack:
            current = stack.pop()
            if current in seen:
                continue

            seen.add(current)
            group.append(current)
            stack.extend(neighbours[current] - seen)

        found.append(sorted(group))

    return found


def describe(bm, group: list[int], layers) -> dict:
    """The measurements an island is recognised by, and which say what it probably is.

    Four of them, and each answers a different question. The triangle count and the surface
    area say how big a thing it is. The span along the item's long axis says where on the
    item it sits, which is what separates a grip from a blade. And the box it occupies on
    MU's own sheet says which piece of the original painting it wears — the one measurement
    that survives a re-unwrap untouched, because MU's art is not ours to change.
    """
    bake, original = layers
    faces = [bm.faces[index] for index in group]

    corners = [corner for face in faces for corner in face.loops]
    points = [vertex.co for face in faces for vertex in face.verts]

    def box(layer):
        us = [corner[layer].uv.x for corner in corners]
        vs = [corner[layer].uv.y for corner in corners]
        return [round(min(us), 3), round(min(vs), 3), round(max(us), 3), round(max(vs), 3)]

    return {
        "triangles": len(group),
        "area": round(sum(face.calc_area() for face in faces), 1),

        # MU models stand along Z, so this is "how far up the item" — from the pommel at
        # the bottom to the tip at the top.
        "along": [round(min(p.z for p in points), 1), round(max(p.z for p in points), 1)],
        "mu_uv": box(original),
        "bake_uv": box(bake),
    }


def merge(fresh: list[dict], existing: Path) -> list[dict]:
    """Keeps the assignments already made, where the island they were made about still exists.

    Matched on the measurements rather than on the index, because the index is the one
    thing that will not survive. An island is the same island when it has the same triangle
    count and sits in the same place on MU's sheet; when nothing matches, the new island
    comes through unassigned rather than inheriting whatever happened to be at its number.
    """
    if not existing.exists():
        return fresh

    was = json.loads(existing.read_text()).get("islands", [])
    by_shape = {(item["triangles"], tuple(item["mu_uv"])): item for item in was}

    kept = 0
    for island in fresh:
        previous = by_shape.get((island["triangles"], tuple(island["mu_uv"])))
        if previous is not None and previous.get("material", UNASSIGNED) != UNASSIGNED:
            island["material"] = previous["material"]
            if "note" in previous:
                island["note"] = previous["note"]

            # And the flags that make an island a part of its own - the missile on a bow,
            # the mesh a variant hides - with whatever was written beside them. These are
            # assignments as much as the material is, and a rebuild that dropped them put
            # the Bull Fighter's crest back on the plain bull's head once already.
            for flag in ("nocked", "hidden"):
                for key in (flag, f"{flag}_from"):
                    if key in previous:
                        island[key] = previous[key]

            kept += 1

    if kept:
        print(f"  kept {kept} of {len(was)} assignments from the existing file")

    # Losing one is not a detail, and it used to be printed like one.
    #
    # An assignment that cannot be matched is a judgement somebody made about what a
    # surface *is*, thrown away — and what replaces it is the profile's default, which is a
    # plausible material rather than an obviously wrong one. That is the worst possible
    # failure mode: the Dark Knight's bare torso and legs came back as plate steel when the
    # bake layout changed under them, and rendered as pitted metal for a whole session
    # without a word, because "kept 1 of 1" reads like success when the 1 is what is left of
    # 3.
    lost = [item for item in was
            if item.get("material", UNASSIGNED) != UNASSIGNED
            and (item["triangles"], tuple(item["mu_uv"])) not in
            {(i["triangles"], tuple(i["mu_uv"])) for i in fresh}]

    if lost:
        print(f"  WARNING: {len(lost)} assignment(s) no longer match any island and have "
              f"been dropped.")
        for item in lost:
            print(f"           island {item.get('island')} was {item['material']}"
                  f" ({item['triangles']} tris)")
        print("           the islands they described are gone — the layout changed. What "
              "replaced\n           them is the profile default, which is a guess. Look at "
              "the item before trusting it.")

    return fresh


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        print("usage: ... --python islands.py -- <in.blend> [out.json] [--default=NAME]")
        return

    blend = Path(argv[0])
    item = blend.stem.removesuffix("_low")

    # What a freshly found island is called when nothing has been said about it. A profile
    # can name one — a plate gauntlet is steel over almost its whole surface — so that an
    # asset with twenty-two islands is buildable at once and refined per island afterwards,
    # rather than being twenty-two decisions before it can be looked at even once.
    default = UNASSIGNED
    for argument in argv:
        if argument.startswith("--default="):
            default = argument.split("=", 1)[1]
    positional = [a for a in argv[1:] if not a.startswith("--")]
    destination = (
        Path(positional[0]) if positional
        else blend.parent.parent / "materials" / f"{item}.json")

    bpy.ops.wm.open_mainfile(filepath=str(blend))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print(f"no mesh in {blend}")
        return

    mesh = meshes[0].data
    bm = bmesh.new()
    bm.from_mesh(mesh)

    if BAKE_UV not in mesh.uv_layers:
        raise SystemExit(f"error: no '{BAKE_UV}' UV layer — run clean_lowpoly first")

    layers = (
        bm.loops.layers.uv[BAKE_UV],
        bm.loops.layers.uv[ORIGINAL_UV if ORIGINAL_UV in mesh.uv_layers else BAKE_UV],
    )

    print(f"\n=== {blend.name} ===")

    found = islands_of(bm)
    islands = []

    for index, group in enumerate(found):
        island = {"island": index, "material": default}
        island.update(describe(bm, group, layers))
        islands.append(island)

    bm.free()

    islands = merge(islands, destination)

    # Whatever else the file already said is kept. The island list is this step's to
    # rewrite; the sheet an item wears, and anything added beside it later, is not.
    document = {"item": item}
    if destination.exists():
        document.update(
            {k: v for k, v in json.loads(destination.read_text()).items() if k != "islands"})

    document["islands"] = islands

    destination.parent.mkdir(parents=True, exist_ok=True)

    # As it was written, not escaped. The prose in these files is full of em-dashes, and
    # dumping them as — turns every rebuild of any asset into a diff across all of its
    # notes — the islands change by a triangle and the file reports thirty changed lines of
    # text nobody touched. The notes are meant to be read in the file.
    destination.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n")

    for island in islands:
        print(
            f"  {island['island']:2d}  {island['material']:<14} "
            f"{island['triangles']:3d} tris  area {island['area']:7.1f}  "
            f"along Z [{island['along'][0]:6.1f},{island['along'][1]:6.1f}]  "
            f"mu uv {island['mu_uv']}")

    unassigned = sum(1 for island in islands if island["material"] == UNASSIGNED)
    print(
        f"\n  {len(islands)} islands, {unassigned} unassigned\n"
        f"  wrote {destination}")


# Guarded, where the rest of the pipeline calls main() outright, because this module is
# also imported: build_maps recovers the islands the same way rather than trusting the file
# it was handed. An unguarded main() ran the whole report on import — reopening the .blend
# out from under its caller and rewriting the assignments it was about to read.
if __name__ == "__main__":
    main()
