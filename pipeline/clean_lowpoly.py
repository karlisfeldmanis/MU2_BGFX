"""Turns MU's drawn triangle soup into a mesh that can be unwrapped and baked onto.

    Blender --background --python pipeline/clean_lowpoly.py -- \
        source/items/Sword01/Sword01.obj workshop/items/Sword01/Sword01_low.blend

Three things are wrong with a BMD as geometry, and all three stop a bake:

*It is split everywhere.* MU stores a vertex per face corner wherever the UVs break, so
Sword01 is 83 vertices holding 68 triangles and 82 of its edges are open. Nothing can be
subdivided cleanly across a seam that is actually a hole, and a normal bake across one
lands on whichever side the ray happened to hit.

*Its normals are per-face and arbitrary.* They were authored to be drawn unshaded, so
nobody ever had to look at them. A bake reads the low-poly's normals to decide where its
cage sits; wrong normals put the cage inside the surface and the bake picks up the far
side of the blade.

*Its UVs are stacked.* Every texel that Sword01 uses is used twice — the two faces of the
blade are the same texels, which is exactly right for a painted sheet and impossible for a
baked one. A bake writes one value per texel and cannot answer two questions with it.

So this welds, re-normals and re-unwraps — and keeps MU's own UVs alongside the new ones
rather than replacing them. That second layer is not sentiment: it is how the original art
gets onto the new layout later, by sampling the old sheet through the old coordinates and
writing it through the new ones. Throwing it away here would mean hand-matching a 64x16
painting to a fresh atlas afterwards.
"""

import json
import sys
from pathlib import Path

import bmesh
import bpy

#: MU's UVs, kept for transferring the original art.
ORIGINAL_UV = "mu_original"

#: The new, non-overlapping layout everything is baked into.
BAKE_UV = "bake"

#: Where the bone index lives once it is on the mesh. An integer per vertex, because MU's
#: skinning is rigid and there is exactly one.
BONE_ATTRIBUTE = "mu_bone"

#: How far apart two vertices can be and still be the same vertex, in world units.
#:
#: MU's split vertices are exact duplicates — the same position written twice — so this
#: only has to beat floating-point noise. Set wide enough to close a modelling gap and it
#: would also collapse the blade's edge, which on a 3.4-unit-thick sword is a real feature
#: a fraction of a unit across.
WELD = 0.0001

#: Angle past which an edge is a crease rather than a curve, in degrees.
#:
#: Used both for shading and for where the unwrap is allowed to cut. Sixty-six is chosen to
#: sit above a blade's bevel, which wants to stay smooth across, and below the spine and
#: the guard, which are corners and should read as corners.
SHARP = 66.0


def clear() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def load(path: Path, rig: Path, asset: Path | None = None):
    """Imports the OBJ as one object, with each of MU's meshes kept as a material slot.

    A BMD is several meshes and the exporter writes each as an OBJ group named for the
    texture it wears — `upper_11`, `hide`, `guard_hair`. That grouping is not decoration:
    it is the only record of which faces sample which sheet, and an item that wears three
    sheets cannot be textured without it.

    Blender's importer throws it away by default. There are no `usemtl` lines in the file,
    so with `use_split_groups` off the groups vanish into one undifferentiated mesh — which
    is what happened, and why the Plate chest could not be built while the gloves could:
    the gloves are one group and lost nothing.

    So it is imported split, each piece is given a material named after its group, and they
    are joined back into one object. Material slots survive a join, so what comes out is a
    single mesh — which is what everything downstream wants — that still knows which face
    belongs to which of MU's sheets.
    """
    bpy.ops.wm.obj_import(
        filepath=str(path), forward_axis="NEGATIVE_Z", up_axis="Y", use_split_groups=True)

    pieces = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not pieces:
        return []

    # The binding is attached here, piece by piece, and that is the whole point of doing
    # it before the join rather than after.
    #
    # Blender does not hand the groups back in the order the file lists them. ArmorMale10
    # is upper_11 then hide on disk and comes back hide then upper_11, so a vertex_bones
    # array applied to the joined mesh by index lands 32 vertices out of step and every
    # vertex after it takes somebody else's bone. It renders as the torso in pieces while
    # the skeleton underneath moves correctly — which is exactly what it did, and why the
    # single-group parts were fine and the two-group parts were not.
    #
    # Matched by group name instead, which is the texture the mesh wears. Attributes
    # survive a join, so once each piece carries its own the order stops mattering.
    binding = groups_from(rig)
    names = bone_names(rig)

    # What the asset says about MU's binding, if it says anything. See rebind.
    asked = {}

    if asset is not None and asset.exists():
        asked = json.loads(asset.read_text()).get("rebind") or {}

    for piece in pieces:
        # Blender names a split group after the group, sometimes with the file stem in
        # front of it. The tail is the group, and the group is the texture's name.
        group = piece.name.rsplit("_", 0)[-1] if "_" not in piece.name else piece.name
        group = group.removeprefix(path.stem + "_")

        material = bpy.data.materials.new(group)
        piece.data.materials.clear()
        piece.data.materials.append(material)

        bones = binding.pop(group, None) if binding else None

        # A rig that describes groups and not this one is a broken join, and the way it
        # breaks is invisible: the piece takes no bone attribute, the join swallows it, and
        # what comes out the far end is a model with no skin and no complaint. That is how
        # the Budge Dragon built the first time — its OBJ said p_d_jpg and its rig still
        # said p_d — and the only sign was JOINTS_0 missing from the .glb.
        if binding is not None and bones is None and groups_from(rig):
            raise SystemExit(
                f"error: group '{group}' has no bones in {rig.name}, which describes "
                f"{', '.join(sorted(groups_from(rig)))}.")

        if bones is not None:
            # Before it goes on the mesh, because after the join the pieces are one and
            # which vertex belonged to which group is gone.
            repaired = mirror_binding(piece.data.vertices, bones, names)
            if repaired:
                print(f"  mirrored   {repaired} vertices of '{group}' onto the bones their "
                      f"twin already uses")

            for source, target, count in rebind(bones, names, asked):
                print(f"  rebound    {count} vertices of '{group}' from {source} to "
                      f"{target}, as the asset asks")

            if len(bones) != len(piece.data.vertices):
                raise SystemExit(
                    f"error: group '{group}' has {len(piece.data.vertices)} vertices and "
                    f"{rig.name} describes {len(bones)} for it.")

            attribute = piece.data.attributes.new(BONE_ATTRIBUTE, "INT", "POINT")
            attribute.data.foreach_set("value", bones)

    if len(pieces) > 1:
        bpy.ops.object.select_all(action="DESELECT")
        for piece in pieces:
            piece.select_set(True)

        bpy.context.view_layer.objects.active = pieces[0]
        bpy.ops.object.join()

    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def groups_from(rig: Path) -> dict[str, list[int]]:
    """The bone each vertex belongs to, keyed by the group that owns it.

    muextract writes one entry per BMD mesh, named for its texture, with the bones already
    in engine order — see BoneOrder. The OBJ names its groups the same way, so the two are
    joined by that name rather than by position, which is the one thing about the import
    that cannot be relied on.
    """
    if not rig.exists():
        return {}

    document = json.loads(rig.read_text())

    # The name muextract wrote, where it wrote one. "group" is the texture's basename for
    # every model whose sheets have distinct names, and the name with the extension put back
    # for the ones that clash — the Budge Dragon is both meshes on p_d. Rigs exported before
    # that field existed carry only a texture, and for those the basename is the answer,
    # because a clash is exactly what they do not have.
    return {
        mesh.get("group") or Path(mesh["texture"]).stem: mesh["vertex_bones"]
        for mesh in document.get("meshes", [])
    }


def bone_names(rig: Path) -> list[str]:
    """The part's own bone names, in the order its vertex_bones index them."""
    if not rig.exists():
        return []

    return [b["name"] for b in json.loads(rig.read_text()).get("skeleton", [])]


def mirror_binding(vertices, bones: list[int], names: list[str]) -> int:
    """Gives a limb the bones its mirror image already has.

    MU's Plate gauntlet binds its left hand to Bip01 L Hand and Bip01 L Finger01, and its
    right hand to nothing at all — every vertex past the right wrist is on Bip01 R Forearm.
    So the right gauntlet turns with the forearm while anything hung off the right hand
    turns with the wrist, and the two part by the eleven degrees the sword idle rotates it.
    That is what "the weapon is not inside the palm" turned out to be.

    It is not something the pipeline did: the .rig.json says the same before anything here
    touches it, 77 vertices on the right forearm and none on the right hand. The asymmetry
    is the whole argument for repairing it — somebody bound one gauntlet properly in 2001
    and did not bind the other, and a mirrored pair of hands disagreeing about which bones
    exist is an oversight rather than an intention.

    So the repair is only ever to copy what the other side already says. For a bone whose
    mirror twin is used and which is not, each vertex on the twin is reflected across the
    body's centre line and the nearest vertex there is moved onto it. Nothing is invented:
    if the left gauntlet had no hand bones either, this does nothing, and it will not
    rebind a limb whose own side already has an opinion.
    """
    index_of = {name: i for i, name in enumerate(names)}
    used = set(bones)

    if not vertices:
        return 0

    # A tolerance in the mesh's own units: a mirrored vertex should land almost exactly on
    # its twin, and anything further away is not a twin.
    spread = max(
        max(v.co[axis] for v in vertices) - min(v.co[axis] for v in vertices)
        for axis in range(3))
    near = (spread * 0.03) ** 2

    moved = 0
    claimed = set()

    for left, name in enumerate(names):
        if " L " not in name:
            continue

        right = index_of.get(name.replace(" L ", " R ", 1))

        # Only where one side uses it and the other does not.
        if right is None or left not in used or right in used:
            continue

        for source in [k for k, b in enumerate(bones) if b == left]:
            at = vertices[source].co
            target = (-at[0], at[1], at[2])

            # One twin each. Two vertices of a left hand can both be nearest to the same
            # vertex of the right one, and without this the second simply overwrites the
            # first — the right hand came back with 25 bones where the left had 35, and the
            # ten that went missing were still on the forearm, still tearing.
            best, closest = -1, near
            for other, vertex in enumerate(vertices):
                if bones[other] == left or other in claimed:
                    continue

                gap = sum((vertex.co[axis] - target[axis]) ** 2 for axis in range(3))
                if gap < closest:
                    best, closest = other, gap

            if best >= 0:
                bones[best] = right
                claimed.add(best)
                moved += 1

    return moved


def rebind(bones: list[int], names: list[str], asked: dict) -> list[tuple[str, str, int]]:
    """Moves a part's vertices off a bone it should not be on, where the asset says so.

    MU binds rigidly — one bone per vertex, no weights — so a part whose vertices are split
    across two bones tears at the seam between them by however far the two rotate apart.
    That is survivable where the seam is buried and ruinous where it is not, and MU is not
    consistent about which it has done.

    The bare head is the case this was written for. `HelmClass02` puts 79 vertices on
    `Bip01 Head` and 7 on `Bip01 Spine1`, so twelve of its 119 triangles — a ring at the
    base of the neck — span the two. MU's own poses turn the head a long way from the
    chest: 5.5 degrees in the standing idle, 26 in the crossbow stance, 56 in action 5. The
    ring gets wrung by that much, and with one bone per vertex there is nothing to spread it
    over, so the head sits visibly askew on the neck and turns against it.

    `HelmMale10`, the plate helm over the same skull, puts every vertex on `Bip01 Head` and
    has no seam at all — which is why this was only ever visible on the character wearing
    nothing, and why it is read as an oversight rather than as an intention.

    Declared and not detected, like the cut-outs and the stances, and for the same reason:
    a part split across two bones is usually correct. A jaw belongs to the head and a collar
    to the chest, and only the asset knows which of those it is looking at.
    """
    index_of = {name: i for i, name in enumerate(names)}
    moved = []

    for source, target in asked.items():
        if source not in index_of or target not in index_of:
            raise SystemExit(
                f"error: rebind names a bone this rig does not have: {source} -> {target}")

        was, now = index_of[source], index_of[target]
        count = sum(1 for b in bones if b == was)

        if count:
            bones[:] = [now if b == was else b for b in bones]
            moved.append((source, target, count))

    return moved


def kept_apart(obj, asset: Path | None) -> set[int]:
    """The material slots the weld must not merge into anything else.

    Two ways in, and both are the asset's own word rather than a guess about the geometry.

    `hidden_mesh` puts the mesh the client hides in here, always. It is the .bmd's mesh
    index, which is the order the OBJ lists its groups and the order the asset lists its
    sheets — see the Bull Fighter's hidden_mesh_from. Each piece was given a material named
    after its group before the join, so the sheet's name at that index is the name of the
    slot to look for. Matched by name rather than by position because the split-group import
    does not return the pieces in the file's order, which is the fault the docstring at the
    top of load() records.

    `separate_meshes` puts every mesh in, and is for a model whose meshes are different
    materials that the weld would otherwise join and average. The Hound is both: its helm
    has to stay apart so the client can hide it, and its head has to stay apart from its body
    so a snout is fur and a cuirass is not.
    """
    if asset is None or not asset.exists():
        return set()

    document = json.loads(asset.read_text())
    sheets = document.get("sheets")
    slots = {m.name: i for i, m in enumerate(obj.data.materials) if m is not None}

    if document.get("separate_meshes"):
        return set(slots.values())

    hidden = document.get("hidden_mesh")

    if hidden is None or not isinstance(sheets, dict):
        return set()

    names = list(sheets)

    if not 0 <= int(hidden) < len(names):
        raise SystemExit(
            f"error: {asset.name} hides mesh {hidden} and names {len(names)} sheets.")

    wanted = names[int(hidden)]

    if wanted not in slots:
        raise SystemExit(
            f"error: {asset.name} hides '{wanted}', which is not one of this mesh's "
            f"groups: {', '.join(sorted(slots))}.")

    return {slots[wanted]}


def weld(obj, apart: set[int] | None = None) -> tuple[int, int]:
    """Merges the duplicate vertices MU split, without merging across bones.

    Two vertices at the same position bound to different bones are not duplicates. They sit
    together in the bind pose and they are meant to come apart — that is the seam between a
    thigh and a pelvis, and welding it staples the leg to the hip. It is silent, too: the
    mesh looks perfect until something animates it.

    So the weld runs per bone rather than over everything. `remove_doubles` takes the
    vertices to consider, so each bone's are handed to it separately and nothing can be
    merged with a vertex belonging to somebody else. On a mesh with no binding — an item,
    which has no skeleton to care about — this is one group and exactly the old behaviour.

    `apart` is the material slots that are each additionally kept to themselves — the MU
    meshes the asset says must not be merged into anything. See kept_apart.

    `c->Object.HiddenMesh` skips one mesh of a model, and the built asset expresses that by
    flagging the islands that came out of it so the exporter can ship them as a part called
    "hidden". That only works while the hidden mesh *is* its own island, and a weld across
    the boundary ends it. The Hound found this. Monster02 is a helm, a bare head and a body;
    the plain Hound hides the helm and the Hell Hound hides the head. The weld merged 68 of
    the helm's 172 triangles into the body through the shoulders, where the two sit at the
    same points on the same bones, and those 68 could no longer be hidden — so the plain
    Hound was drawn wearing both heads at once and rendered as a figure in shards.

    Declared per asset rather than applied to every mesh boundary, and that is deliberate.
    Welding across MU's meshes is arguably always wrong — a .bmd's meshes are separate draws
    on separate sheets and MU never merges them — but 78 of the assets in the tree have
    coincident vertices across one, so the general rule would relay out the islands of 78
    models whose materials have already been assigned and looked at. That is its own pass,
    and it should be taken: this is the same shape of finding as the map size the Spider had
    to override per asset. Until then an asset that needs it says so.
    """
    before = len(obj.data.vertices)

    bm = bmesh.new()
    bm.from_mesh(obj.data)

    layer = bm.verts.layers.int.get(BONE_ATTRIBUTE)

    # Which side of the hidden boundary a vertex is on, taken off the faces that use it.
    #
    # Read before anything merges, and that is what makes it safe: at this point a vertex is
    # still in exactly one piece, so every face on it carries the same material.
    def side(vertex) -> int:
        if not apart:
            return 0

        return next(
            (f.material_index + 1 for f in vertex.link_faces
             if f.material_index in apart), 0)

    if layer is None and not apart:
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=WELD)
    else:
        bm.verts.ensure_lookup_table()

        groups: dict[tuple[int, int], list] = {}
        for vertex in bm.verts:
            key = (vertex[layer] if layer is not None else 0, side(vertex))
            groups.setdefault(key, []).append(vertex)

        # One call per group. The vertex references from the other groups stay valid
        # because nothing outside the group being merged is touched.
        for key in sorted(groups):
            live = [v for v in groups[key] if v.is_valid]
            if len(live) > 1:
                bmesh.ops.remove_doubles(bm, verts=live, dist=WELD)

    # Per-loop UVs survive this: welding joins vertices, and a UV belongs to a face corner
    # rather than to a vertex, so MU's coordinates come through the merge intact.
    bm.to_mesh(obj.data)
    bm.free()

    obj.data.update()
    return before, len(obj.data.vertices)


def renormal(obj, flat: bool = False) -> None:
    """Points the normals outward and creases the mesh where its faces disagree.

    `flat` clears MU's custom split normals afterwards, which hands the shading to the
    crease marks below. See the block at the end for when that is the right thing and why
    it is per-asset rather than a rule.
    """
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)

    # Smooth by default, with the creases cut in as edge flags. MU's faces are flat because
    # nothing lit them; a blade is a curved surface and reads as one once it is shaded.
    #
    # Marked here rather than left to shade_smooth_by_angle, which since Blender 4.1 does
    # not mark anything: it hangs a Smooth by Angle node group off the object and leaves the
    # mesh untouched. The modifier is honoured in a viewport and by nothing this pipeline
    # does afterwards, so every asset built until now was smoothed across its creases with
    # no sign of it.
    #
    # What it is worth is much less than it looks, and that has to be said here so nobody
    # spends a day rebuilding on the strength of the paragraph above. Marking the creases
    # changes almost nothing today, because MU's OBJ carries a normal per face corner, the
    # importer keeps them as custom split normals, and custom split normals outrank a crease
    # flag entirely. All sixty assets sampled carry them. Rebuilt with this fix, the Bronze
    # Boots moved 2.5 grey levels out of 255, the Scale Armour 0.8, and Ale not one pixel.
    #
    # So this is correct and inert: it is what the shading falls back on the moment those
    # normals are cleared or repaired, and until then the marks sit there unused. The Sword
    # of Assassin, which is what turned this up, had 41 edges past the crease angle and one
    # of them marked - but what actually fixed its blade was undoing a winding reversal, not
    # this.
    #
    # The live defect is those custom normals. Measured against MU's own convention - it
    # winds clockwise, so its normals oppose Blender's face normals and export_gltf reverses
    # the winding to match - 7.9% of the sampled area is shaded more than 60 degrees from
    # where its face points, reaching 43% on the Elf helm and 21% on the Plate gloves. That
    # is the black torso between lit shoulders. Clearing them repairs the armour and inverts
    # the sword, so one rule does not yet serve both, and it is not fixed here.
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.mesh.select_mode(type="EDGE")
    bpy.ops.mesh.edges_select_sharp(sharpness=SHARP * 3.14159265 / 180.0)
    bpy.ops.mesh.mark_sharp()
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.object.mode_set(mode="OBJECT")

    bpy.ops.object.shade_smooth()

    # And for an asset that says so, the custom normals go and every face keeps its own
    # normal. This is the escape hatch the paragraph a few lines up describes as not built.
    #
    # The jewels are what asked for it, and they are the clean case the sword is not.
    # Measured on MU's own .obj, corner by corner against the face each corner belongs to:
    #
    #   Jewel01   36 corners   0.0 deg from face
    #   Jewel02   66 corners   0.0 deg
    #   Jewel15   96 corners   0.0 deg
    #   Gem05    144 corners  27.9 deg
    #   Sword01  204 corners  11.4 deg
    #
    # Zero. MU authored the jewels with exact per-face normals, because a cut gem is facets
    # and nothing else. What arrives in the .glb is not that: the weld above merges the
    # corners at each shared position - a jewel is 8 to 18 positions holding 12 to 32
    # triangles, so almost every vertex is a facet junction - and `remove_doubles` averages
    # the custom normals it merges. Measured on the built Jewel15, the shading normal sits
    # 37 degrees from its face on average and 56 at worst, and 16 of its 18 positions come
    # out fully smoothed. The facets stop stepping and the gem reads as a blob.
    #
    # No material fixes that. Roughness decides how wide a highlight is, not whether there
    # is an edge for it to break on, so a mirror-polished blob is a blob. This is the fix
    # and the material is the second half of it.
    #
    # Shaded flat rather than left to the crease marks above, and that is the second thing
    # this had to learn. Clearing the normals and trusting SHARP was tried first and did
    # almost nothing: a jewel's facets do not meet at blade angles. Measured on MU's own
    # meshes, the angle between adjacent faces at each shared edge -
    #
    #   Jewel01  18 edges  min 40.4  mean 55.6  max 74.0   past 66: 6
    #   Jewel02  33 edges  min  0.0  mean 39.4  max 69.3   past 66: 6
    #   Jewel15  48 edges  min  0.0  mean 35.3  max 76.7   past 66: 8
    #
    # - so five edges in six are under the threshold and stay smooth, and the rebuild came
    # back at 34 degrees from its faces where it had been 37. SHARP is 66 because that sits
    # above a blade's bevel and below its spine; a gem is neither, and lowering it for the
    # gem's sake would flatten every curved thing in the catalogue.
    #
    # So for these the answer is not a threshold at all. Every face keeps its own normal,
    # which is what MU authored and what a cut gem is.
    #
    # Per-asset and not a rule, and the paragraph above says exactly why: clearing these
    # repairs the armour and inverts the sword. It is safe here because MU's normals for
    # these three ARE the face normals, so this reproduces MU's own shading rather than
    # replacing it. An asset whose normals are 27.9 degrees off its faces, like Gem05, is
    # being told something by them and must not get this.
    # And the faces are turned over on the way, which is the third thing this had to learn
    # and the one that looks wrong until it is measured.
    #
    # MU winds clockwise, so a face's own normal by the right-hand rule points *into* the
    # solid, and the normal MU stores against it is the outward one - measured on the .obj,
    # MU's normals sit at exactly -1.000 against their faces on all three jewels. Blender
    # keeps that winding (normals_make_consistent above finds nothing to fix, because a
    # closed solid is already consistent, just consistently inward), and export_gltf then
    # reverses every triangle on the way out - which is what makes the exported winding
    # agree with the custom normals it also writes.
    #
    # Clearing the custom normals takes one side of that pair away and leaves the other.
    # The shading normal becomes the face's own inward one, the export still reverses, and
    # the result is a gem lit from inside: the first attempt at this rendered pure black
    # with one lit facet at the bottom. Measured on the exported .glb, face normal against
    # written normal:
    #
    #   Potion01  +1.000   Gem05  +0.807      (custom normals kept)
    #   Jewel15   -1.000   Jewel01  -1.000    (cleared, not flipped)
    #
    # So the flip restores what the custom normals were carrying. It belongs here and not in
    # export_gltf, because the reversal there is right for every asset that keeps its
    # normals, which is all of them but these.
    # Flipping the faces does not do it either, and that is worth one line so nobody tries
    # it again: a flip reverses the winding and the face normal together, so the sign
    # between them is invariant and the gem stays inside out.
    #
    # What works is to put back what the weld destroyed. MU's normals are already exactly
    # per-face; the only thing wrong with them is that `remove_doubles` averaged them at
    # every facet junction. So they are recomputed rather than cleared - one normal per
    # loop, the negated face normal, which is MU's own convention - and written back as
    # custom split normals. The winding stays as MU wrote it, the export's reversal still
    # pairs with them the way it does for every other asset, and the facets come back hard.
    if flat:
        mesh = obj.data
        mesh.calc_loop_triangles()

        # Negated because MU winds clockwise: the face's own normal points into the solid
        # and the outward one is its opposite. See the measurement above.
        loop_normals = [(0.0, 0.0, 0.0)] * len(mesh.loops)

        for polygon in mesh.polygons:
            outward = -polygon.normal

            for loop in polygon.loop_indices:
                loop_normals[loop] = (outward.x, outward.y, outward.z)

        mesh.normals_split_custom_set(loop_normals)
        print(f"  normals     {len(mesh.polygons)} faces re-flattened after the weld")


#: How much of MU's own layout may be stacked and still be baked into.
#:
#: A bake writes one value per texel, so two faces sharing a texel is a question with two
#: answers. A little is survivable — the two faces get whichever was rasterised last, and on
#: a mirrored pair that is the same answer twice. A lot is not.
STACK_LIMIT = 0.02


def unwrap(obj, unstack: bool = True) -> None:
    """Gives the mesh a layout to bake into: MU's own where it can be, a new one where not.

    MU's own, wherever it is not stacked, and this is the whole of what makes the bake
    carry the picture rather than smear it.

    Everything baked afterwards is MU's art sampled through MU's UVs. A fresh projection
    lays shells out by 3D surface area, which is unrelated to where MU spent its texels — a
    chest emblem drawn large on the sheet sits on a small part of the body, so the projector
    hands it a fraction of the room its art needs and the emblem is averaged away. Using
    MU's layout, the transfer is the identity: every texel lands on the texel it came from.

    The reason this was not done originally is real and still applies to some of the set:
    MU stacks. The two faces of a blade are the same texels, and a bake cannot write two
    values into one. But *stacked* is a property to be measured, not assumed — the Dark
    Knight's torso turns out to have none at all, and its emblem came back the moment it
    stopped being re-projected. So the layout is tried first and kept if it is clean.

    Where it is not clean the old path runs: project, rescale each island to the share of
    the map its own art occupies, pack. That is worse than MU's layout and better than the
    projection alone.
    """
    mesh = obj.data

    layer = mesh.uv_layers.get(BAKE_UV) or mesh.uv_layers.new(name=BAKE_UV)
    source = mesh.uv_layers.get(ORIGINAL_UV)

    if source is not None:
        for index in range(len(mesh.loops)):
            layer.data[index].uv = source.data[index].uv

        mesh.uv_layers.active = layer

        # An asset may refuse the un-stacking, and one has to.
        #
        # The Dark Wizard's torso came out of it with the buckled vest gone: 102 of its 150
        # triangles have UV centres on that vest, the island list agreed, and the baked atlas
        # held nothing but a bare arm and featureless grey. Turning this step off puts the
        # vest back, buckles, straps and all - which is how it was found, after the sheet,
        # the texel density, the UV convention and the transfer had each been cleared.
        #
        # So the step is wrong for him and right for the plate chest, which arrives 83%
        # stacked and needs its halves separated. Rather than guess a threshold that tells
        # them apart, the asset says. The default is unchanged, so nothing else in the tree
        # moves.
        #
        # This is a workaround and should be read as one: 69 of his 150 faces are moved off
        # their twin here, and the ones that lose their art are among them. The step writes
        # to BAKE_UV and leaves ORIGINAL_UV alone, which is the right shape, so why the moved
        # faces then sample the wrong region is not yet understood.
        if not unstack:
            print("  uv mirrored     refused by the asset; stacked UVs kept as MU laid them")
            return

        if coverage(mesh, BAKE_UV)[1] <= STACK_LIMIT:
            return

        # Stacked, but stacked for a reason that can be undone.
        #
        # MU mirrors by flipping the UVs: a left pauldron and a right one are the same
        # texels read backwards, which is why the plate chest comes in 83% stacked while the
        # bare torso comes in at nothing. A flip reverses the winding of a triangle in UV
        # space, so the two halves are told apart exactly — by the sign of their area, not
        # by a guess about symmetry — and the mirrored half is moved off the original.
        #
        # Packing afterwards brings them back into the square side by side, each still the
        # size and shape MU drew it. The art is duplicated where it was shared, which is
        # what a bake needs and costs nothing but map.
        unfold(obj)

        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.uv.select_all(action="SELECT")
        bpy.ops.uv.pack_islands(margin=0.02, rotate=False)
        bpy.ops.object.mode_set(mode="OBJECT")

        if coverage(mesh, BAKE_UV)[1] <= STACK_LIMIT:
            return

        # Still stacked: something overlaps that is not a mirror. Put MU's layout back so
        # the projection below starts from the same place it always did.
        #
        # The layers are fetched again rather than reused, and that is not tidiness: this
        # line segfaulted Blender on FireLight01. Everything between here and the top of the
        # branch runs through edit mode - unfold, then pack_islands - and a UV layer handle
        # taken before entering it is not guaranteed to survive leaving it. The crash was
        # inside MeshUVLoopLayer_data_lookup_int, which is Blender indexing a layer whose
        # data no longer matches the mesh it was taken from.
        #
        # The length is taken from the shorter of the two for the same reason. Reading one
        # element past the end of a Blender collection is not an IndexError, it is a
        # backtrace.
        mesh = obj.data
        layer = mesh.uv_layers[BAKE_UV]
        source = mesh.uv_layers.get(ORIGINAL_UV)

        if source is not None:
            for index in range(min(len(layer.data), len(source.data))):
                layer.data[index].uv = source.data[index].uv

    mesh.uv_layers.active = layer

    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")

    # Cut along the creases first so the projector has somewhere sensible to open the
    # shell, then project. Left to itself the projector cuts by facing angle alone, which
    # on a blade slices it lengthwise down the middle of a flat it should have kept whole.
    #
    # Deselected between the clear and the pick, and that is not tidiness. Selecting sharp
    # edges *adds* to what is already selected, so marking seams straight after clearing
    # them — which needs everything selected — marks every edge in the mesh. The blade came
    # back as sixty-odd separate triangles, each its own island with its own seam, which is
    # the worst possible layout: no texel density, and a normal discontinuity across every
    # edge of the model.
    bpy.ops.mesh.mark_seam(clear=True)
    bpy.ops.mesh.select_all(action="DESELECT")
    bpy.ops.mesh.select_mode(type="EDGE")
    bpy.ops.mesh.edges_select_sharp(sharpness=SHARP * 3.14159265 / 180.0)
    bpy.ops.mesh.mark_seam(clear=False)

    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method="ANGLE_BASED", margin=0.02)

    # Packed with a margin, because a bake bleeds: a normal map sampled at the edge of a
    # shell picks up whatever is next to it, and mipmapping widens the reach. The margin is
    # what keeps one island's bleed out of its neighbour.
    bpy.ops.object.mode_set(mode="OBJECT")
    match_density(obj)
    bpy.ops.object.mode_set(mode="EDIT")

    # Packed with a margin, because a bake bleeds: a normal map sampled at the edge of a
    # shell picks up whatever is next to it, and mipmapping widens the reach. The margin is
    # what keeps one island's bleed out of its neighbour.
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(margin=0.02, rotate=True)

    bpy.ops.object.mode_set(mode="OBJECT")


def signed_uv_area(face, layer) -> float:
    """A face's area in UV space, negative where the layout has flipped it over."""
    points = [loop[layer].uv for loop in face.loops]
    total = 0.0

    for index in range(len(points)):
        a, b = points[index], points[(index + 1) % len(points)]
        total += (a.x * b.y) - (b.x * a.y)

    return total * 0.5


def unfold(obj) -> None:
    """Moves the mirrored half of MU's layout off the half it was folded onto.

    One unit along U, which is a whole tile, so nothing can land back on anything. Where it
    ends up does not matter — the pack that follows decides that — only that it stops
    sharing texels with the face it mirrors.
    """
    mesh = obj.data

    bm = bmesh.new()
    bm.from_mesh(mesh)

    uv = bm.loops.layers.uv.get(BAKE_UV)
    moved = 0

    for face in bm.faces:
        if signed_uv_area(face, uv) < 0.0:
            for loop in face.loops:
                loop[uv].uv.x += 1.0
            moved += 1

    bm.to_mesh(mesh)
    bm.free()
    mesh.update()

    print(f"  uv mirrored     {moved} of {len(mesh.polygons)} faces moved off their twin")


def uv_area(face, layer) -> float:
    """The area a face covers in one UV layer."""
    points = [loop[layer].uv for loop in face.loops]
    total = 0.0

    for index in range(len(points)):
        a, b = points[index], points[(index + 1) % len(points)]
        total += (a.x * b.y) - (b.x * a.y)

    return abs(total) * 0.5


def uv_islands(bm, layer) -> list[list]:
    """Faces grouped into UV islands: connected where the layout does not cut them apart."""
    island_of: dict[int, int] = {}
    islands: list[list] = []

    for start in bm.faces:
        if start.index in island_of:
            continue

        here = len(islands)
        island_of[start.index] = here
        stack, group = [start], []

        while stack:
            face = stack.pop()
            group.append(face)

            for loop in face.loops:
                for other in loop.edge.link_faces:
                    if other is face or other.index in island_of:
                        continue

                    # Joined only where both faces put the shared edge in the same place.
                    # An edge the unwrap cut has two different pairs of coordinates on it,
                    # and that is exactly what makes it a boundary between islands.
                    shared = {v.index for v in loop.edge.verts}
                    mine = {l.vert.index: l[layer].uv.copy()
                            for l in face.loops if l.vert.index in shared}
                    theirs = {l.vert.index: l[layer].uv.copy()
                              for l in other.loops if l.vert.index in shared}

                    if all((theirs[v] - mine[v]).length < 1e-6 for v in mine if v in theirs):
                        island_of[other.index] = here
                        stack.append(other)

        islands.append(group)

    return islands


def match_density(obj) -> None:
    """Rescales each bake island to the share of the map MU's own layout gave it.

    This is the difference between a bake that carries MU's picture and one that smears it.

    The unwrap above lays shells out by *3D surface area*, which is the right rule when the
    texture is going to be painted afterwards and no rule at all when it is going to be
    filled from an existing painting. MU spent its texels where it wanted them — a face and
    a belt buckle get far more of the sheet than their size on the model would justify, and
    a thigh gets far less — so a layout that ignores that gives the buckle a tenth of the
    room its art needs and the thigh ten times more than it can fill.

    Measured across the set before this existed: every asset had islands starved to a third
    of the density they needed while others sat at three, eight, a hundred times theirs.
    PantMale10's worst was 0.04 — one texel where the art had twenty-five, which is not
    blur, it is the detail being discarded and then interpolated back as a smear.

    So each island is scaled to the fraction of the layout its own art occupies on MU's
    sheet. Nothing is redrawn and nothing moves relative to anything inside an island; the
    packing afterwards decides where they sit and one uniform scale decides how big they
    all are.
    """
    mesh = obj.data

    if mesh.uv_layers.get(BAKE_UV) is None or mesh.uv_layers.get(ORIGINAL_UV) is None:
        return

    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()

    bake = bm.loops.layers.uv.get(BAKE_UV)
    original = bm.loops.layers.uv.get(ORIGINAL_UV)

    islands = uv_islands(bm, bake)
    measured = []

    for group in islands:
        drawn = sum(uv_area(face, original) for face in group)
        laid = sum(uv_area(face, bake) for face in group)
        measured.append((group, drawn, laid))

    total_drawn = sum(drawn for _, drawn, _ in measured)
    total_laid = sum(laid for _, _, laid in measured)

    if total_drawn <= 0 or total_laid <= 0:
        bm.free()
        return

    for group, drawn, laid in measured:
        if laid <= 0 or drawn <= 0:
            continue

        # The area this island should occupy, if the layout were shared out the way MU
        # shared out its sheet. Square-rooted because UVs scale linearly.
        wanted = (drawn / total_drawn) * total_laid
        factor = (wanted / laid) ** 0.5

        if abs(factor - 1.0) < 1e-4:
            continue

        loops = [loop for face in group for loop in face.loops]
        centre_x = sum(loop[bake].uv.x for loop in loops) / len(loops)
        centre_y = sum(loop[bake].uv.y for loop in loops) / len(loops)

        for loop in loops:
            uv = loop[bake].uv
            uv.x = centre_x + ((uv.x - centre_x) * factor)
            uv.y = centre_y + ((uv.y - centre_y) * factor)

    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def coverage(mesh, layer_name: str) -> tuple[float, float]:
    """Fraction of the UV square used, and how much of that is stacked.

    The triangles are rasterised properly rather than stamped as bounding boxes. That
    distinction is the whole measurement here: two islands packed tightly beside each other
    have overlapping bounding boxes and no overlapping texels, so a box stamp reports a
    clash that is not there — it claimed 8.5% on a layout the packer had just guaranteed
    was clean. Since "nothing is stacked" is exactly what this step exists to establish,
    a measure that cries wolf about it is worse than none.
    """
    size = 512
    seen = [0] * (size * size)

    bm = bmesh.new()
    bm.from_mesh(mesh)
    layer = bm.loops.layers.uv.get(layer_name)

    if layer is None:
        bm.free()
        return 0.0, 0.0

    def stamp(a, b, c) -> None:
        """Fills one UV triangle, testing the centre of each texel against its edges."""
        u0 = max(0, min(size - 1, int(min(a[0], b[0], c[0]) * size)))
        u1 = max(0, min(size - 1, int(max(a[0], b[0], c[0]) * size) + 1))
        v0 = max(0, min(size - 1, int(min(a[1], b[1], c[1]) * size)))
        v1 = max(0, min(size - 1, int(max(a[1], b[1], c[1]) * size) + 1))

        area = ((b[0] - a[0]) * (c[1] - a[1])) - ((c[0] - a[0]) * (b[1] - a[1]))
        if abs(area) < 1e-12:
            return

        for v in range(v0, v1 + 1):
            py = (v + 0.5) / size
            for u in range(u0, u1 + 1):
                px = (u + 0.5) / size

                # Barycentric sign test. Inside means all three weights share the sign of
                # the triangle's own area, which also makes it wind-order agnostic.
                w0 = ((b[0] - a[0]) * (py - a[1])) - ((px - a[0]) * (b[1] - a[1]))
                w1 = ((c[0] - b[0]) * (py - b[1])) - ((px - b[0]) * (c[1] - b[1]))
                w2 = ((a[0] - c[0]) * (py - c[1])) - ((px - c[0]) * (a[1] - c[1]))

                if (w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0):
                    seen[(v * size) + u] += 1

    for face in bm.faces:
        points = [(loop[layer].uv.x, loop[layer].uv.y) for loop in face.loops]
        # Fanned, so an n-gon is measured as the triangles it is drawn as.
        for i in range(1, len(points) - 1):
            stamp(points[0], points[i], points[i + 1])

    bm.free()

    used = sum(1 for c in seen if c > 0)
    covered = used / (size * size)
    overlapped = sum(1 for c in seen if c > 1) / max(1, used)
    return covered, overlapped


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: ... --python clean_lowpoly.py -- <in.obj> <out.blend> [asset.json]")
        return

    source, destination = Path(argv[0]), Path(argv[1])
    asset = Path(argv[2]) if len(argv) > 2 else None

    clear()
    objects = load(source, source.with_suffix(".rig.json"), asset)
    if not objects:
        print(f"nothing imported from {source}")
        return

    obj = objects[0]
    obj.name = source.stem.replace("_low", "") + "_low"

    # Named before anything measures it. It was renamed inside unwrap() instead, which ran
    # after the "before" reading was taken — so the original layer was reported as 0%
    # covered when it was in fact intact, and the number said the art had been lost.
    if obj.data.uv_layers and obj.data.uv_layers[0].name != ORIGINAL_UV:
        obj.data.uv_layers[0].name = ORIGINAL_UV

    print(f"\n=== {source.name} ===")

    if BONE_ATTRIBUTE in obj.data.attributes:
        values = [0] * len(obj.data.vertices)
        obj.data.attributes[BONE_ATTRIBUTE].data.foreach_get("value", values)
        print(f"  bound       {len(set(values))} bones, one per vertex (rigid, as MU is)")

    # The mesh the client hides, as a material slot, so the weld can keep it whole. The
    # asset names its sheets in the .bmd's mesh order and each piece was given a material
    # called after its group, so hidden_mesh indexes the first and the name finds the
    # second. See weld.
    apart = kept_apart(obj, asset)

    before, after = weld(obj, apart)
    print(f"  welded      {before} -> {after} vertices")

    if apart:
        names = ", ".join(sorted(obj.data.materials[i].name for i in apart))
        print(f"  kept apart  {names}")

    # Whether this asset's shading is its crease marks rather than MU's custom normals.
    # See renormal, which measures the three jewels this exists for.
    flat = False

    if asset is not None and asset.exists():
        flat = bool(json.loads(asset.read_text()).get("flat_shaded", False))

    renormal(obj, flat)

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    open_edges = sum(1 for e in bm.edges if len(e.link_faces) < 2)
    bad_edges = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    bm.free()
    print(f"  open edges  {open_edges}   non-manifold {bad_edges}")

    # Whether this asset will have its mirrored UVs separated. See unwrap.
    unstack = True

    if asset is not None and asset.exists():
        unstack = bool(json.loads(asset.read_text()).get("unstack", True))

    was = coverage(obj.data, ORIGINAL_UV)
    unwrap(obj, unstack)
    now = coverage(obj.data, BAKE_UV)

    print(f"  uv {ORIGINAL_UV:<12} {was[0]:.1%} covered, {was[1]:.1%} of it stacked  (kept)")
    print(f"  uv {BAKE_UV:<12} {now[0]:.1%} covered, {now[1]:.1%} of it stacked  (new)")

    obj.data.calc_loop_triangles()
    print(f"  triangles   {len(obj.data.loop_triangles)}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
    print(f"  saved       {destination}")


main()
