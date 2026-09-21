"""Moves MU's painted art off its shared sprite sheet and onto the item's own texture.

    Blender --background --python pipeline/transfer_albedo.py -- \
        workshop/items/Sword01/Sword01_low.blend workshop/items/Sword01/sword02_hd.png \
        workshop/items/Sword01/Sword01_albedo.png 2048 Linear

MU does not give an item a texture. It gives it a region of one: sword02 is a 64x16 sheet
holding a blade and a grip side by side, and which part belongs to which surface is not
written down anywhere except in the model's own UVs. Several items share a sheet, and one
item's two faces usually share the same texels — Sword01's are stacked on top of each
other, which is why the layout measures as 100% overlapped.

None of that survives contact with a modern material, which wants one texture per item with
every surface somewhere different on it. So the art has to be moved rather than copied: read
through the coordinates MU shipped, written through the layout built for baking.

That is exactly what a bake is, so a bake is what this does. Emission rather than diffuse,
because the question is "what colour is painted here" and not "how does it answer light":
emission passes the texel through untouched, where diffuse would fold in a shading term
nobody asked for and cannot be removed afterwards.

The result is the first honest MU2 asset — a single texture, correctly laid out, that a PBR
material can be built on. It carries MU's own colour and nothing else: no invented detail,
no sharpening, no material response folded in. Every later map is judged against it.

*What it reads.* Not MU's sheet directly, in the ordinary case, but the HD sheet enlarge.py
made from it — the magnification from 64x16 to a 2048 atlas is around thirty times, and
which reconstruction does that, and how it is kept from smearing one part of an item into
the next, is enlarge.py's whole subject. This step still works on MU's raw sheet; it just
has to be told to sample it `Closest` when it does. See the note in `material_for`.
"""

import sys
from pathlib import Path

import bpy

ORIGINAL_UV = "mu_original"
BAKE_UV = "bake"

#: How dark a texel has to be, on a keyed sheet, to count as not there.
#:
#: Low, because the thing being cut away is painted black and the thing being kept is a
#: gold feather with dark barbs in it. Set high enough to catch the barbs and the feather
#: comes back with holes through it.
KEY = 0.10


def material_for(obj, source: Path | None, interpolation: str, slot: int = 0,
                 wrap: bool = False):
    """A shader that emits one sheet, read through MU's own coordinates.

    One of these per material slot. A BMD is several meshes and each wears its own sheet —
    the Plate chest is upper_11 over 174 faces and hide over 24 — and clean_lowpoly keeps
    that as slots on a single object. Giving each slot its own emitting material means one
    bake writes all of them into the atlas, in the right places, with no compositing step
    and no second pass to get out of register with the first.
    """
    material = bpy.data.materials.new(name=f"mu_transfer_{slot}")
    material.use_nodes = True

    tree = material.node_tree
    tree.nodes.clear()

    output = tree.nodes.new("ShaderNodeOutputMaterial")
    emission = tree.nodes.new("ShaderNodeEmission")
    texture = tree.nodes.new("ShaderNodeTexImage")
    coords = tree.nodes.new("ShaderNodeUVMap")

    # A slot with no sheet emits flat black rather than reaching for a file that is not
    # there. It used to be handed a path called /nonexistent, which raised inside Blender
    # and took the whole bake down — so one unmatched slot cost the item its colour
    # entirely, rather than costing that slot its colour.
    if source is None:
        black = tree.nodes.new("ShaderNodeRGB")
        black.outputs[0].default_value = (0.0, 0.0, 0.0, 1.0)

        tree.links.new(black.outputs[0], emission.inputs["Color"])
        tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])

        if slot < len(obj.data.materials):
            obj.data.materials[slot] = material
        else:
            obj.data.materials.append(material)

        return material, tree

    texture.image = bpy.data.images.load(str(source))

    # How the sheet is sampled, and it depends entirely on which sheet.
    #
    # Closest for MU's own: a 64-pixel sheet whose internal edges are the boundary between
    # one part of an item and the next, where a linear tap mixes the neighbour in and the
    # neighbour is a large fraction of every sample. Sword01's guard is 2.4 pixels wide.
    #
    # Linear for a sheet enlarge.py has already taken to HD. There the boundaries have
    # been resolved region by region — see its note — and a tap now reaches a sixteenth of
    # a source pixel, so the contamination Closest was protecting against is gone and what
    # is left is the resampling through the mesh's UVs. Closest on an HD sheet just puts
    # stair-steps back into art that no longer has any.
    texture.interpolation = interpolation

    # What happens where the UVs leave the sheet, and it is not the same answer for every
    # asset.
    #
    # CLIP for the ordinary case, which is nearly all of them. A weapon or an armour sheet is
    # an atlas — a blade in one region, a grip in another — and every triangle samples inside
    # it exactly once. Repeating one of those would bring the far edge of the atlas back in at
    # the border, which is the same fault the weapon profile's tiling_why records from the
    # other direction.
    #
    # REPEAT where the asset says its sheet wraps, and the Apple is why this option exists.
    # Potion01's fruit island runs from u = -0.25: MU wraps a sixteen-pixel sheet around a
    # sphere, which is the one thing docs/items-and-armour.md warns the bake assumes is never
    # true - "the bake assumes each triangle samples its sheet once". Clipped, the quarter of
    # the apple below zero sampled nothing and baked as a black gash down its side.
    #
    # Declared per asset rather than detected from the UV bounds. It is a claim about what the
    # sheet *is*, exactly as the interpolation and the cut-out are, and the same three attempts
    # to sniff a cut-out from the art that the docs record went wrong would go wrong here: a
    # bounds test cannot tell a sheet meant to wrap from an asset with a stray vertex.
    texture.extension = "REPEAT" if wrap else "CLIP"

    # The layer that says which part of the sheet this surface is. Named explicitly rather
    # than left to the active layer, because the active layer during a bake is the target's
    # — that is what the bake writes through — and reading and writing must not be the same
    # coordinates or the transfer is a copy of itself.
    coords.uv_map = ORIGINAL_UV

    tree.links.new(coords.outputs["UV"], texture.inputs["Vector"])
    tree.links.new(texture.outputs["Color"], emission.inputs["Color"])
    tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])

    if slot < len(obj.data.materials):
        obj.data.materials[slot] = material
    else:
        obj.data.materials.append(material)

    return material, tree


def carry_alpha(
        trees, target, size: int,
        cutout: "set[str] | None" = None, keep: "set[str] | None" = None) -> None:
    """Bakes the sheet's alpha into the albedo's, in a second pass.

    MU cuts shapes out with alpha rather than modelling them. The Plate helm's plume is two
    flat quads and the feather is a hole in the middle of them; the same is true of a
    shield's fretwork and of hair. Those sheets ship as .OZT, which is a TGA and carries an
    alpha channel, where the opaque ones ship as .OZJ and do not.

    A bake writes what the shader emits, and the shader was emitting the texture's Colour
    output — so the alpha was simply never read, and every albedo came out fully opaque.
    The plume rendered as the two rectangles it is drawn on, background and all.

    Alpha cannot ride along in the same pass, because an emission bake writes the emitted
    colour into RGB and 1.0 into A whatever it is fed. So it is a second pass over the same
    layout with the texture's Alpha wired where its Colour was, which lands the mask in the
    red channel of a second image, and that is copied into the first one's alpha.

    Skipped entirely when no sheet has an alpha channel to carry, which is most of them.
    """
    sources = []

    for _, slot_name, tree, _ in trees:
        texture = next(
            (node for node in tree.nodes
             if node.type == "TEX_IMAGE" and node.image is not None and node.image != target),
            None)
        emission = next((node for node in tree.nodes if node.type == "EMISSION"), None)
        written = next(
            (node for node in tree.nodes if node.type == "TEX_IMAGE" and node.image == target),
            None)

        if texture is not None and emission is not None and written is not None:
            sources.append((tree, texture, emission, written, slot_name))

    cutout = cutout or set()
    keep = keep or set()

    # A slot the asset calls a cut-out is keyed off brightness, unless the asset also says
    # its mask is the sheet's own alpha.
    #
    # MU does not always keep the mask in the file. guard_hair.ozt — the Plate helm's plume —
    # holds perfectly good colour with an alpha the old client ignores; it decides that mesh
    # is see-through in code, through a BlendMeshIndex it sets per item. Nothing usable is in
    # the sheet, so the asset says so instead, and what is keyed is darkness: the feather is
    # painted on near-black and the black is the part that is not there, the same convention
    # MU's effects use. A Greater Than at KEY makes it a hard mask rather than a fade,
    # because this is going to be alpha-tested anyway.
    #
    # <b>But `cutout` answers two questions and only one of them is this one.</b> It is also
    # what marks the built material a mask, so an asset must list every slot that cuts —
    # including the ordinary ones whose .OZT carries exactly the mask that is wanted. Keying
    # one of those off brightness reads the picture as the mask, and on art that is not
    # painted on black the two have nothing to do with each other: the Dark Knight's bust
    # came out with two pale wedges floating over his head, because mid-brown hair against a
    # bright ground keys to roughly the opposite of the hair.
    #
    # So `cutout_alpha` names the slots whose mask is already in the file. It is a second
    # declaration rather than a sniff of the art for the reason the first one is: the alpha
    # of a MU sheet is not always transparency — Shield10's is an emblem, and read as a mask
    # it shipped for months as a shield full of holes.
    keyed = [entry for entry in sources
             if entry[4].lower() in cutout and entry[4].lower() not in keep]
    brightness = {entry[4] for entry in keyed}

    if not keyed and not keep and not any(
            texture.image.depth == 32 for _, texture, _, _, _ in sources):
        print("  alpha       none of the sheets carry one")
        return

    mask = bpy.data.images.new("alpha", width=size, height=size, alpha=False)

    for tree, texture, emission, written, name in sources:
        for link in list(emission.inputs["Color"].links):
            tree.links.remove(link)

        if name in brightness:
            grey = tree.nodes.new("ShaderNodeRGBToBW")
            step = tree.nodes.new("ShaderNodeMath")
            step.operation = "GREATER_THAN"
            step.inputs[1].default_value = KEY

            tree.links.new(texture.outputs["Color"], grey.inputs["Color"])
            tree.links.new(grey.outputs["Val"], step.inputs[0])
            tree.links.new(step.outputs["Value"], emission.inputs["Color"])
        else:
            tree.links.new(texture.outputs["Alpha"], emission.inputs["Color"])

        written.image = mask

    bpy.ops.object.bake(type="EMIT")

    # Straight from one buffer into the other. foreach_get rather than list(), because a
    # 768 square is two and a third million floats and Python is not the place for that.
    cut = [0.0] * (size * size * 4)
    mask.pixels.foreach_get(cut)

    carried = [0.0] * (size * size * 4)
    target.pixels.foreach_get(carried)

    for index in range(0, len(carried), 4):
        carried[index + 3] = cut[index]

    target.pixels.foreach_set(carried)

    cutout = sum(1 for index in range(3, len(carried), 4) if carried[index] < 0.5)
    print(f"  alpha       carried; {cutout * 100 // (size * size)}% of the square is cut away")


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 3:
        print("usage: ... -- <in.blend> <source_texture> <out.png> [size] [Closest|Linear]")
        return

    blend, destination = Path(argv[0]), Path(argv[1])
    size = int(argv[2]) if len(argv) > 2 else 1024

    # Stated by the caller rather than inferred from the filename, because it is a claim
    # about what the sheet is — see the note in material_for — and a claim belongs in the
    # command that makes it.
    interpolation = argv[3] if len(argv) > 3 else "Closest"

    # group=path, one per sheet this asset wears. The group is the material slot's name,
    # which clean_lowpoly took from the OBJ group, which the exporter took from the BMD
    # mesh's texture. That chain is the only thing that knows which faces sample what.
    # Slots whose transparency is keyed off brightness rather than read from an alpha
    # channel. See carry_alpha for why MU leaves some of them with nothing to read.
    # Folded with the sheets, and for the same reason. See below.
    cutout = {
        argument.split("=", 1)[1]
        for argument in argv[4:] if argument.startswith("--cutout=")}
    cutout = {one.lower() for one in cutout}

    # The subset of those whose mask is the sheet's own alpha. See carry_alpha.
    keep = {
        argument.split("=", 1)[1].lower()
        for argument in argv[4:] if argument.startswith("--keepalpha=")}

    # Slots whose sheet MU wraps around the model rather than laying out as an atlas. See
    # material_for, and the Apple, which is the asset that needed it.
    wrapped = {
        argument.split("=", 1)[1].lower()
        for argument in argv[4:] if argument.startswith("--wrap=")}

    # Folded, because MU's own naming is not consistent and nothing downstream is either.
    #
    # The slot names come from the .bmd's texture names and the asset declares them back:
    # Object75 writes `so_jgwall04.jpg` and the Dark Knight's bust writes `DK_body.JPG`
    # beside `DK_hair1.tga`, in one file. export_gltf folds every one of these comparisons
    # and this one did not, so an asset that spelled its sheets in a different case matched
    # nothing here, baked a black square, and then built and exported and indexed without a
    # single error — the model arrives fully metallic over an empty albedo, which draws as a
    # silhouette. See the note above about a bake that writes nothing still reporting success.
    sheets = {
        key.lower(): value for key, value in (
            argument.split("=", 1) for argument in argv[4:]
            if "=" in argument and not argument.startswith("--"))}

    bpy.ops.wm.open_mainfile(filepath=str(blend))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print(f"no mesh in {blend}")
        return

    obj = meshes[0]

    slots = [m.name if m else "" for m in obj.data.materials] or [""]
    trees = []

    for index, name in enumerate(slots):
        path = sheets.get(name.lower())
        if path is None:
            # No sheet named for this slot: emit nothing rather than somebody else's art.
            # A slot with no sheet is a slot nobody has decided about, and black is what
            # that looks like — visible, and obviously not finished.
            print(f"  slot {index} '{name}' has no sheet; it will bake black")

        material, tree = material_for(
            obj, Path(path) if path else None, interpolation, index, name.lower() in wrapped)

        trees.append((index, name, tree, path))

    target = bpy.data.images.new("albedo", width=size, height=size, alpha=True)

    # The bake writes into whichever image node is selected and active, which is a quiet
    # piece of Blender's interface leaking into its API — and it has to be true of *every*
    # material on the object, not just one, or the bake fails on the first slot that has
    # no target. Added last, selected, and left unconnected in each: a connected target
    # would also be read while being written.
    for _, _, tree, _ in trees:
        node = tree.nodes.new("ShaderNodeTexImage")
        node.image = target
        node.select = True
        tree.nodes.active = node

    # Written through the packed layout, read through MU's.
    obj.data.uv_layers.active = obj.data.uv_layers[BAKE_UV]

    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 1
    scene.cycles.use_denoising = False
    scene.render.bake.use_clear = True

    # Bled outward past the island edge, because a texel on a seam is sampled from both
    # sides once the texture is filtered and mipmapped. Without it every island wears a
    # transparent rind at distance.
    scene.render.bake.margin = max(4, size // 128)

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    bpy.ops.object.bake(type="EMIT")

    carry_alpha(trees, target, size, cutout, keep)

    destination.parent.mkdir(parents=True, exist_ok=True)
    target.filepath_raw = str(destination)
    target.file_format = "PNG"
    target.save()

    # What actually landed, since a bake that writes nothing still reports success: an
    # empty result means the read coordinates were wrong, which is the failure this whole
    # step is most likely to have.
    pixels = list(target.pixels)
    opaque = sum(1 for i in range(3, len(pixels), 4) if pixels[i] > 0.5)
    total = size * size
    coloured = sum(
        1 for i in range(0, len(pixels), 4)
        if pixels[i] + pixels[i + 1] + pixels[i + 2] > 0.02)

    print(f"\n=== transferred -> {destination.name} ===")
    for index, name, _, path in trees:
        print(f"  slot {index}      {name or '(unnamed)'} <- "
              f"{Path(path).name if path else 'nothing'}")
    print(f"  sampled     {interpolation.lower()}")
    print(f"  target      {size}x{size}")
    print(f"  written     {coloured / total:.1%} of the square carries colour")
    print(f"  opaque      {opaque / total:.1%}")
    print(f"  saved       {destination}")


def texture_size(material) -> str:
    for node in material.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image and node.image.size[0] > 1:
            return f"{node.image.size[0]}x{node.image.size[1]}"
    return "unknown"


main()
