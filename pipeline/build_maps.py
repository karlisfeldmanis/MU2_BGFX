"""Turns the material assignment into the maps a modern renderer actually reads.

    Blender --background --python pipeline/build_maps.py -- \
        workshop/items/Sword01/Sword01_low.blend source/items/Sword01/Sword01.json \
        workshop/items/Sword01 2048 source/materials

MU gave an item one picture and expected it to answer every question: what colour is this,
how shiny is it, which way is it facing. A metallic-roughness renderer asks those
separately, and this is where the separate answers come from.

The albedo is not one of them, and that is the point of the arrangement. MU's painting
stays MU's painting — see decode_texture on what happens when it does not. What gets built
here is everything *around* the colour: which texels are metal, how rough each one is, and
what the surface does at a scale no 64x16 sheet could ever have held.

*How a texel learns what it is.* The islands file says island 3 is steel. This rasterises
the mesh's baked UV layout, so every texel the blade occupies is marked steel, every texel
the grip occupies is marked leather, and the rest is marked as nothing. From there the maps
are a lookup: steel's metallic is 1.0, leather's is 0.0, and the boundary between them is a
boundary in the picture rather than a number somebody tuned.

*Why the grain is procedural.* Because there is no other honest source for it. Detail at
this scale does not exist in the original art and cannot be recovered from it — the
blade is forty texels of blurred JPEG. What can be stated truthfully is what the surface
*is*: rolled steel has a directional grain, cast brass has pitting, a leather cord wrap has
bands running across it. Those are facts about the material, they are written down in
source/materials, and generating them from there is a description rather than a guess.

*Padding.* Every map is dilated past the edge of its island. A renderer filtering a texel
on the seam mixes it with its neighbours, and a neighbour that is empty pulls the edge of
the blade towards whatever empty happens to be. How far is not a constant: it is matched to
how far the albedo's own bake bled, which depends on the sheet's size and not on this map's.
Eight was the constant it used to be, and see `padding_for` for the rim of dull dielectric
that drew around every island on the parts MU painted smallest.
"""

import json
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np

BAKE_UV = "bake"
ORIGINAL_UV = "mu_original"

#: Texels of bleed past each island's edge, as a floor. See `padding_for`.
PADDING = 8


def padding_for(size: int, albedo: Path) -> int:
    """How far to grow each island, matched to how far the albedo bake bled.

    A flat 8 was wrong in a way that only showed on the parts with the smallest sheets, and
    the arithmetic says exactly which. `transfer_albedo` bakes with a margin of
    `max(4, n // 128)` texels *in the albedo's own resolution*, and the material maps are
    written at whatever MATERIAL_MAP asks for - so on the Plate gauntlet, a 192 albedo
    against a 1024 map, those 4 texels are 21 of these, against a dilation of 8.

    What lives in the gap is a rim, one per island edge, where the albedo has colour and the
    owner map has nobody: metallic falls to 0 and roughness to its 0.5 default, so the band
    renders as dull dielectric around every plate. Measured on the shipped set, 11.6% of the
    gauntlet's texels and 14.2% of the boot's, against 1.4% for the breastplate - whose sheet
    is twice the size, so its bleed is 11 texels and very nearly covered. That is the whole
    of "the armour looks right and the other pieces do not": the same material, with a
    dielectric edge drawn round every piece of it, worst where MU painted smallest.

    So the dilation is derived from the same two numbers the bleed is, rather than guessed
    once and left. The floor stands for an item with no sheet at all, which bakes nothing.
    """
    if not albedo.exists():
        return PADDING

    import bpy

    picture = bpy.data.images.load(str(albedo), check_existing=False)

    try:
        wide = picture.size[0]
    finally:
        bpy.data.images.remove(picture)

    if wide <= 0:
        return PADDING

    # transfer_albedo's own figure, read here rather than imported, because the two files do
    # not otherwise depend on each other and a constant shared by copying is what this bug
    # was. If that margin moves, this comment is the thing that has to be found.
    margin = max(4, wide // 128)

    return max(PADDING, -(-margin * size // wide))

#: How much of a metal's sheet may sit against the white ceiling after its f0 correction.
#:
#: The correction lands the stated reflectance where the art has room and stops where it does
#: not. 0.10 rather than a rounder number because it is the figure that separates the two
#: cases actually seen: the Plate shield reaches its f0 spending 8.7% and looks like steel,
#: and the breastplate wanted 49% and came back paper white with its engraving gone.
CEILING_BUDGET = 0.10

#: Below this luminance a JPEG's chroma is its own rounding, not the painter's colour, and
#: the F0 pull fades a texel's tint toward grey in proportion. 24/255 as the file stores it:
#: `base_colour` reads its pixels through Blender, and for an 8-bit sRGB image `pixels`
#: hands back the encoded bytes, not linear light - measured on the Mace, texel (19, 10, 10)
#: came back as 0.0745, which is 19/255. The first cut of this floor was converted to
#: linear and sat under every texel in the sheet, and the build changed nothing.
BLACK_FLOOR = 24.0 / 255.0

#: Colours for the diagnostic map, which is the one that makes a wrong assignment obvious.
#: A grip that comes out steel-blue here is a grip pointing at the wrong line in the JSON.
CLASS_COLOURS = {
    "steel": (0.35, 0.45, 0.85),
    "plate_steel": (0.30, 0.60, 0.70),
    "brass": (0.90, 0.72, 0.20),
    "leather_wrap": (0.45, 0.26, 0.16),
    "unassigned": (1.00, 0.00, 0.55),
}


def value_noise(shape: tuple[int, int], cells: tuple[int, int], rng) -> np.ndarray:
    """Smooth random field: a coarse grid of random values, interpolated up.

    Deterministic given the seed, which matters more here than the quality of the noise —
    a map that changes every time it is built is a map nobody can compare against the last
    one. Bilinear rather than anything smoother because it is fed through a gradient to
    make normals, and the artefacts of a cheap interpolation are below the scale the grain
    is generated at.
    """
    height, width = shape
    rows, columns = max(2, cells[0]), max(2, cells[1])

    grid = rng.random((rows, columns))

    # Up one axis, then the other. np.interp is 1-D, so this is two passes.
    vertical = np.empty((height, columns))
    positions = smoothstep(np.linspace(0, rows - 1, height))
    for column in range(columns):
        vertical[:, column] = np.interp(positions, np.arange(rows), grid[:, column])

    out = np.empty((height, width))
    positions = smoothstep(np.linspace(0, columns - 1, width))
    for row in range(height):
        out[row] = np.interp(positions, np.arange(columns), vertical[row])

    return out


def smoothstep(positions: np.ndarray) -> np.ndarray:
    """Eases the fractional part of each sample position, leaving the whole part alone.

    Straight linear interpolation between the cells of a random grid is visibly a grid. The
    value is continuous across a cell boundary but its slope is not, and the eye reads that
    crease as an edge — so a noise field meant to be pitting comes out as squares, and a
    normal map built from its gradient comes out as squares with hard diagonals through
    them. That is what "the PBR is pixelated" turned out to be.

    The original note said the artefacts of a cheap interpolation sit below the scale the
    grain is generated at. That was true while the maps were 2048 and every cell was a few
    texels across a surface seen small. It stopped being true when the maps came down to
    match the art, because the same cell then covers four times as much of the model.

    3t^2 - 2t^3 is the usual cure: it is zero-sloped at both ends, so neighbouring cells
    meet without a crease. Applied to the position rather than the value, which is what lets
    np.interp keep doing the lookup.
    """
    base = np.floor(positions)
    t = positions - base
    return base + (t * t * (3.0 - (2.0 * t)))


def grain_height(kind: str, shape: tuple[int, int], settings: dict, rng) -> np.ndarray:
    """A height field for one material's surface, in 0..1.

    Three kinds, because three is what the sword needs and inventing a fourth before
    something wants it is how a library stops being readable.
    """
    height, width = shape

    # Cells across the map, used as written.
    #
    # This is worth being exact about, because it was got wrong in both directions. `scale`
    # is a count, not a frequency in texels: the size of a cell *on the model* depends only
    # on how many of them there are, because the map covers the whole model however big it
    # is. The map size decides one thing only — whether a cell is large enough in texels to
    # be drawn without aliasing, which wants two or more.
    #
    # Scaling the count down with the map, which is what this did for a while, holds the
    # cell at a constant number of texels and therefore makes it sixteen times larger on a
    # model whose map went from 2048 to 128. That is the "big pixels" — pitting the size of
    # a shoulder plate.
    #
    # So the count stands as the library wrote it, and the map is made big enough to carry
    # it instead. See MATERIAL_MAP in settings: these maps are generated from a description
    # of a surface rather than resampled from MU's sheet, so nothing about their size is
    # bound by how many texels MU painted.
    wanted = max(2, int(settings.get("scale", 200)))

    # And never finer than the map can draw it.
    #
    # A cell of noise needs several texels to come out as a shape rather than as static.
    # steel asks for 520, which on a 1024 map is one cell every 1.97 texels — right at the
    # limit, and it showed: the axe head and its grip band came out in hard vertical lines
    # while the albedo underneath them was perfectly smooth. That is aliasing, and it is the
    # same fault as the wet-snakeskin plate, arriving from the other direction.
    #
    # Four texels a cell, which is enough for the smoothstep between them to read as a
    # slope. A material that asks for more is asking for something this map cannot hold, so
    # it is given what it can and told.
    scale = min(wanted, max(2, min(shape) // 4))

    if scale != wanted:
        print(f"  grain      {kind} asked for {wanted} cells; {scale} is what "
              f"{min(shape)} texels can draw")

    along = settings.get("along", "v")

    if kind == "brushed":
        # Streaks: noise that is fine across the grain and long along it. Rolling and
        # grinding both leave this, and it is what makes a blade's highlight stretch into
        # a line rather than sitting as a dot.
        #
        # The two counts are the way round they are because value_noise takes cells per
        # axis, and a streak running along V is one that barely changes along V — so few
        # cells down the axis it runs along, and many across it. Written the other way
        # first, which put broad horizontal bands across the blade and looked, correctly,
        # like corrugated iron: the grain was running across the blade rather than along
        # it, at a fortieth of the intended frequency.
        few = max(2, scale // 40)
        cells = (few, scale) if along == "v" else (scale, few)
        return value_noise(shape, cells, rng)

    if kind == "weave":
        # Bands across the grip, with a slow wobble along it so the winding is not a ruler.
        axis = np.linspace(0, scale * np.pi, width if along == "u" else height)
        bands = 0.5 + (0.5 * np.sin(axis))
        bands = bands ** 0.6                               # flatten the tops, deepen the gaps

        field = np.tile(bands, (height, 1)) if along == "u" else np.tile(bands[:, None], (1, width))
        return (0.82 * field) + (0.18 * value_noise(shape, (scale // 2, scale // 2), rng))

    if kind == "cast":
        # Pitting: isotropic, and lumpy at two scales because a cast surface has both
        # bubbles and the coarse texture of the mould.
        coarse = value_noise(shape, (scale // 4, scale // 4), rng)
        fine = value_noise(shape, (scale, scale), rng)
        return (0.6 * coarse) + (0.4 * fine)

    return np.full(shape, 0.5)


def rasterise(mesh, size: int, island_of: dict[int, int]) -> np.ndarray:
    """Which island owns each texel of the baked layout, or -1 for none.

    A plain barycentric fill per triangle. There are 68 of them and the map is 2048 square,
    so nothing here needs to be clever — and a rasteriser that can be read is worth more
    than one that is fast, since every later map inherits whatever this gets wrong.
    """
    owner = np.full((size, size), -1, dtype=np.int32)

    uv = mesh.uv_layers[BAKE_UV].data
    mesh.calc_loop_triangles()

    for triangle in mesh.loop_triangles:
        island = island_of.get(triangle.polygon_index)
        if island is None:
            continue

        # Texel centres, so a triangle that covers half a texel does not claim it.
        points = np.array([[uv[corner].uv[0] * size, uv[corner].uv[1] * size]
                           for corner in triangle.loops])

        low = np.maximum(np.floor(points.min(axis=0)).astype(int) - 1, 0)
        high = np.minimum(np.ceil(points.max(axis=0)).astype(int) + 1, size)

        if np.any(high <= low):
            continue

        xs = np.arange(low[0], high[0]) + 0.5
        ys = np.arange(low[1], high[1]) + 0.5
        grid_x, grid_y = np.meshgrid(xs, ys)

        (ax, ay), (bx, by), (cx, cy) = points
        denominator = ((by - cy) * (ax - cx)) + ((cx - bx) * (ay - cy))
        if abs(denominator) < 1e-12:
            continue

        first = (((by - cy) * (grid_x - cx)) + ((cx - bx) * (grid_y - cy))) / denominator
        second = (((cy - ay) * (grid_x - cx)) + ((ax - cx) * (grid_y - cy))) / denominator
        third = 1.0 - first - second

        # A small negative tolerance closes the cracks between neighbouring triangles,
        # which otherwise show as single unfilled texels along every shared edge.
        inside = (first >= -0.002) & (second >= -0.002) & (third >= -0.002)

        window = owner[low[1]:high[1], low[0]:high[0]]
        window[inside] = island

    return owner


def dilate(owner: np.ndarray, steps: int) -> np.ndarray:
    """Grows every island outwards into the empty space around it."""
    grown = owner.copy()

    for _ in range(steps):
        empty = grown < 0
        if not empty.any():
            break

        for shift, axis in ((1, 0), (-1, 0), (1, 1), (-1, 1)):
            neighbour = np.roll(grown, shift, axis=axis)
            take = empty & (neighbour >= 0)
            grown[take] = neighbour[take]
            empty = grown < 0

    return grown


def save(path: Path, pixels: np.ndarray, colour: bool = False) -> None:
    """Writes an RGB or RGBA array, in Blender's own bottom-up row order.

    The same order the albedo bake produced, so every map in the set lines up with every
    other one. Getting this wrong flips a normal map upside down, which reads as light
    coming from the floor and is remarkably hard to see as a flip.

    `colour` is the difference between a map and a picture. Every other map here is data -
    a roughness, a normal, an occlusion - and is written raw, which is what Non-Color means.
    A base colour is looked at, so it is written sRGB-encoded: the values handed in are
    linear either way, and this decides whether Blender encodes them on the way out.

    A fourth channel, where the caller hands one in, is a cut-out shape and survives the
    write. It exists because base_colour once dropped it: the corrected base colour wins
    over the albedo at export, so the Plate helm's plume - a feather MU makes as alpha in
    the sheet, carried faithfully through transfer_albedo - shipped as two solid quads the
    moment its item gained a metal with an f0. The exporter declared MASK and the file had
    nothing to cut with.
    """
    height, width = pixels.shape[:2]
    carries_alpha = pixels.shape[2] == 4

    image = bpy.data.images.new(path.stem, width=width, height=height, alpha=carries_alpha)
    image.colorspace_settings.name = "sRGB" if colour else "Non-Color"

    rgba = np.ones((height, width, 4), dtype=np.float32)
    rgba[:, :, :3] = np.clip(pixels[:, :, :3], 0.0, 1.0)
    if carries_alpha:
        rgba[:, :, 3] = np.clip(pixels[:, :, 3], 0.0, 1.0)

    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = str(path)
    image.file_format = "PNG"
    image.save()

    bpy.data.images.remove(image)


#: How far a ray looks for something to be occluded by, in MU units.
#:
#: MU's parts are thin shells — a chest is a front surface and a back surface a few units
#: apart with nothing between them — so an unlimited trace has every texel on the outside
#: staring at the inside of the other wall and calls it a cave. Baked that way the plate
#: came back at a mean of 84 out of 255, which is not occlusion, it is a black model.
#:
#: Ten units is about a hand's breadth at MU's scale: near enough to catch the gap under a
#: pauldron and the step between two overlapping bands, far enough to miss the far wall.
OCCLUSION_REACH = 10.0

#: How much of the traced occlusion is kept.
#:
#: Not all of it, because MU's art is not unlit. The sheet already has some shading painted
#: into it — the artist drew where the plate turns away from the light — so a full-strength
#: bake lays a second set of shadows on top of the first and the creases go to black. Half
#: is enough to seat the parts on each other without arguing with the painting.
OCCLUSION_STRENGTH = 0.5


def soften(field: np.ndarray, passes: int = 2) -> np.ndarray:
    """A small box blur, run a couple of times to round it off.

    Occlusion is a low-frequency signal — it says a recess is in shadow, not which texel of
    it — so the speckle a path tracer leaves is all error and none of it is detail. Blurring
    is the cheapest denoise there is and the only one that needs no extra dependency, and at
    this scale it is indistinguishable from a better one.
    """
    out = field
    for _ in range(passes):
        padded = np.pad(out, 1, mode="edge")
        out = (padded[:-2, 1:-1] + padded[2:, 1:-1]
               + padded[1:-1, :-2] + padded[1:-1, 2:]
               + (4.0 * out)) / 8.0

    return out


def bake_occlusion(obj, size: int, samples: int = 512) -> "np.ndarray | None":
    """How much of the sky each texel can see, traced against the mesh itself.

    The one channel here that is measured rather than described. Roughness and metallic are
    statements about what a surface is made of, and the grain is a statement about how it
    was made — all three come out of the material library and none of them knows anything
    about this particular model. Occlusion is the opposite: it is a fact about the shape,
    and the only way to get it is to trace the shape.

    It matters most on exactly what MU's art cannot supply. The sheet is lit flat, painted
    to be drawn unshaded, so a plate's overlapping bands and the gap under a pauldron carry
    no shadow at all — they are the same brightness as the surface around them, and they
    read as a printed pattern rather than as parts standing off a body. A traced occlusion
    puts the dark back where the geometry says it belongs.

    Self-occlusion only, and worth being clear about the limit: each part is baked on its
    own, so this is the shadow a shoulder plate casts into its own recess and not the one
    the chest casts onto the arm. A character is five files and they never meet in here.
    """
    materials = [slot.material for slot in obj.material_slots if slot.material]
    if not materials:
        return None

    target = bpy.data.images.new("occlusion", width=size, height=size, alpha=False)

    # Every material needs the target selected and active, not just one: the bake fails on
    # the first slot that has nowhere to write. Left unconnected — a connected target would
    # be read while it is being written.
    for material in materials:
        material.use_nodes = True
        node = material.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = target
        node.select = True
        material.node_tree.nodes.active = node

    obj.data.uv_layers.active = obj.data.uv_layers[BAKE_UV]

    scene = bpy.context.scene

    # The world is what a bake traces against for sky visibility, and a .blend cleaned by
    # clean_lowpoly has none. Made here, with the reach set on it.
    if scene.world is None:
        scene.world = bpy.data.worlds.new("occlusion")

    # Set by name where the name exists. Blender has moved these between releases — the
    # enable flag is gone in 5.0 and the distance is not — so this asks rather than assumes,
    # and a version that has neither simply traces unlimited and is caught by the strength.
    lighting = scene.world.light_settings
    if hasattr(lighting, "distance"):
        lighting.distance = OCCLUSION_REACH

    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.bake.use_clear = True
    scene.render.bake.margin = max(4, size // 128)

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    try:
        bpy.ops.object.bake(type="AO")
    except RuntimeError as error:
        print(f"  occlusion  not baked: {error}")
        return None

    pixels = np.asarray(target.pixels[:], dtype=np.float32).reshape(size, size, 4)

    # Blender writes bottom-up and everything else here is top-down.
    traced = np.flipud(pixels[..., 0])

    # Normalised against its own brightest, because what is wanted is relative.
    #
    # A thin shell sees its own far wall from everywhere, so the absolute sky visibility of
    # a plate chest is about a third whichever texel is asked — and the reach that was
    # supposed to stop that turns out not to be respected by this version's bake. Taken
    # literally it is a uniform grey wash over the whole model, which is not shading, it is
    # a dimmer.
    #
    # What survives normalising is the part that varies: the gap under a pauldron against
    # the open plate beside it. The brightest twentieth is taken as "unoccluded" rather than
    # the single brightest texel, so one stray sample cannot set the scale for the map.
    traced = soften(traced)

    lit = traced[traced > 0.0]
    if lit.size:
        traced = np.clip(traced / max(float(np.percentile(lit, 95)), 1e-3), 0.0, 1.0)

    # Eased toward unoccluded, so this shades the model rather than repainting it.
    return 1.0 - (OCCLUSION_STRENGTH * (1.0 - traced))


#: How much of an item's relief and gloss comes from the art rather than from the grain.
#:
#: The ground has read its own sheets since tiled_maps was written — a cobbled street is its
#: mortar joints and a meadow is the shadow between its tufts, and neither is in a noise
#: field. Baked items never did: both the normal and the roughness here are a procedural
#: grain at the material's own scale, and MU's painting reaches the model as colour and
#: nothing else. Measured on Plate Armor, the roughness across the whole breastplate varied
#: by 0.019 — flat metal with a synthetic sparkle on it.
#:
#: That was defensible while the art was 128 pixels with little in it to read. It stopped
#: being when the sheets came back at six times that: MU painted the fluting, the rivets and
#: the wear into this armour and at 768 they are legible, and the surface was still being
#: invented underneath them.
#:
#: Half, because both sources are true. The grain is the substance — cast plate is pitted
#: wherever you look at it, and that is a fact about steel rather than about this breastplate
#: — and the art is this particular object. Neither should win outright.
FROM_THE_ART = 0.5


#: The roughest a surface is allowed to be *shiny*, in the packed ORM.
#:
#: Specular aliasing, and it took a day to find because every part of it looked like
#: something else. A metal plate with roughness varying around 0.5, lit by a sun at 3.2,
#: produces sub-pixel highlights that the renderer cannot resolve — and where they blow past
#: what the frame can hold, the red channel clamps to zero and what is left reads as
#: saturated blue. On a boot's edges and a pauldron's rim, which is exactly where the normals
#: vary fastest.
#:
#: Measured rather than reasoned about, by replacing the roughness map with a constant and
#: rendering the same frame: at a flat 0.8 there are no such pixels at all, at a flat 0.5
#: there are 57, and with the map itself — whose own minimum is 0.5 — there are 3971. So it
#: is the *variation* rather than the level, which is what specular aliasing is.
#:
#: A floor is the blunt end of the standard fix, and it was blunt enough to take the whole
#: material library with it. Measured on the maps it had already written: ArmorMale10's
#: roughness ran from 0.722 to 0.749 across the entire sheet and Axe01's did the same. Steel's
#: 0.32, brass's 0.44, wrought iron's 0.50, plate steel's own 0.68, every grain's
#: `roughness_range` and every bit of gloss `painted_relief` read out of MU's art — all of it
#: sat under the floor and arrived as one flat number. The blade and the gauntlet holding it
#: shipped the same surface, which is precisely the distinction plate_steel.json was split off
#: from steel.json to make.
#:
#: So the careful version, which the note above already named: raise roughness where the normal
#: map varies fastest, and leave it alone where the surface is flat. See specular_floor. The
#: constant that remains is an absolute backstop, below anything the library asks for, there so
#: a material with no grain at all cannot ask for a mirror.
ROUGHNESS_FLOOR = 0.18


#: How hard the normal map's own variation pushes roughness up. See specular_floor.
#:
#: Specular aliasing is a mismatch between two things the renderer keeps separately: the normals
#: within a shading footprint, and the width of the highlight it convolves them with. Where the
#: first is wider than the second, the highlight breaks into sub-pixel pieces the frame cannot
#: resolve — and that is a statement about the map, so it can be answered in the map.
#:
#: 1.0 is the physical figure: the variance of the normals inside the footprint adds to the
#: squared roughness, which is the standard result and what a renderer with a screen-space
#: roughness limiter computes per frame. Godot's is Forward+ only and this project renders
#: Mobile, so it is computed here instead, once, at the size the map is written at.
SPECULAR_AA = 1.0


def specular_floor(normal: np.ndarray, roughness: np.ndarray) -> np.ndarray:
    """Roughness raised wherever the normals disagree within a shading footprint.

    A highlight is the reflection of a light in a *set* of normals — every one the renderer
    averages into one pixel — and roughness is how wide that reflection is spread. Where the
    normals inside the footprint are spread wider than the highlight is, the two are saying
    different things and the picture goes to pieces: the sub-pixel highlights the frame cannot
    hold, the ones the floor above was raised to 0.72 to kill.

    The standard answer, and the one Godot itself implements screen-space in Forward+, is to
    fold the one into the other. Roughness enters the shading as `alpha = roughness²`, alpha is
    a variance, and variances of independent things add — so the spread of the normals is added
    to alpha and the surface is shaded as though it were that much rougher. Which it is: a
    surface whose normals point in a range of directions *is* a rough surface, and that the
    range was written into a map rather than into the mesh does not change the physics.

    The spread is measured rather than assumed. Every normal here is a unit vector, so the
    length of their local mean says how much they agree — 1.0 for a flat patch, less as they
    scatter — and `1 - |mean|²` is their variance directly, no constant in it.

    What it buys is that the answer is local. The floor was a claim about the whole sheet made
    from its worst texel; this is a claim about each texel, so a blade's flat face keeps steel's
    0.32 and the ridge of its grain, four texels away, gets what it actually needs.
    """
    # A footprint of a few texels rather than one. The map is written at 1024 and the studio
    # draws an item about 800 pixels wide, so a screen pixel covers something a little over one
    # texel at the closest the camera comes and more at any other distance — and the mip chain
    # the renderer builds from here is averaging exactly this neighbourhood.
    mean = np.dstack([soften(normal[..., channel]) for channel in range(3)])
    agreement = np.clip(np.linalg.norm(mean, axis=2), 0.0, 1.0)

    alpha = np.square(np.clip(roughness, 0.0, 1.0))
    alpha += SPECULAR_AA * (1.0 - np.square(agreement))

    return np.sqrt(np.clip(alpha, 0.0, 1.0))


def base_colour(item: str, out: Path, owner: np.ndarray,
                materials: dict, library: dict) -> "Path | None":
    """MU's colour with a metal's reflectance put back into it.

    For a dielectric the base colour is a colour and MU's sheet is the right thing to hand
    over. For a metal it is not a colour at all - it is the reflectance, the whole of what
    the surface returns - and MU's sheet is a painted picture of an object, with the
    artist's own shading and speculars already in it. Handing that over as F0 is a category
    error rather than a matter of taste, and it has a measurement: on the Small Axe the
    steel texels average 0.206 in linear light where steel is 0.56, so the head reflected a
    third of what steel reflects and read as dark plastic with a smear on it.

    So each metal is pulled onto its own stated `f0`, keeping `f0_from_art` of the sheet's
    variation about it: `out = f0 * (art / mean) ** k`. The shape is the argument. A plain
    gain (k=1) clips - 23% of that head goes to white and the painted highlights go with it.
    A power curve that lands the same mean flattens - the art's contrast falls from 187x to
    3.5x, which is a grey card with a hint of axe. A power *about the mean* can do neither,
    and k says how much painting survives.

    Written beside the albedo rather than into it, and that is not tidiness. `transfer_albedo`
    states the invariant this has to respect - the albedo "carries MU's own colour and nothing
    else: no invented detail, no sharpening, no material response folded in. Every later map
    is judged against it" - and reflectance is material response. Writing back into it would
    also mean a rebuild correcting an already-corrected sheet, which is a guard that holds
    exactly once; this is derived from the albedo every build and cannot stack.

    Returns the path written, or None where an item has no metal and the albedo already is
    the base colour.
    """
    albedo = out / f"{item}_albedo.png"
    target = out / f"{item}_basecolor.png"

    metals = sorted({
        name for name in materials.values()
        if float(library[name].get("metallic", 0.0)) >= 0.5 and "f0" in library[name]
    })

    if not albedo.exists() or not metals:
        # An item that had metal and no longer does must not keep the old file: the exporter
        # prefers it where it exists, so a stale one outlives the reason for it.
        target.unlink(missing_ok=True)
        return None

    picture = bpy.data.images.load(str(albedo), check_existing=False)
    try:
        wide, tall = picture.size
        flat = np.empty(wide * tall * 4, np.float32)
        picture.pixels.foreach_get(flat)
    finally:
        bpy.data.images.remove(picture)

    # Blender hands back the file's own bytes as floats - for the 8-bit sRGB albedo that is
    # the sRGB-encoded value, not linear light, measured: (19, 10, 10) reads as 0.0745 - and
    # bottom-up. Every number below, `was`, the pull and `f0` itself, is therefore in that
    # encoded space, and the library's figures were swept under it; moving the arithmetic
    # to linear would move every metal and is its own pass, not a line here.
    # Not flipped: `owner` is bottom-up as well, rasterised with row = v * size and written
    # by `save` without a flip, so the two already agree. This used to flip here and back on
    # the way out, which laid every material mask upside down on the art - measured on the
    # Bronze Armor, the lift went to 5010 texels of skin (the arms came out paper white) and
    # missed 93038 of the cuirass; flipped, 788 and 1045, which is the dilation's rim. It hid
    # on items that are one metal nearly throughout and showed on every mixed one. All four
    # channels: the alpha is the plume's shape and the correction below only touches colour.
    lit = flat.reshape(tall, wide, 4).copy()

    # The material each albedo texel belongs to. `owner` is built at the map size, which on a
    # small item is ten times the sheet's - the axe is a 96 albedo against a 1024 map - so it
    # is sampled down by nearest neighbour. Nearest and not bilinear: this is an index, and
    # the average of steel and wood is neither.
    rows = (np.arange(tall) * owner.shape[0] // tall).clip(0, owner.shape[0] - 1)
    columns = (np.arange(wide) * owner.shape[1] // wide).clip(0, owner.shape[1] - 1)
    index = owner[np.ix_(rows, columns)]

    for name in metals:
        definition = library[name]
        mask = np.isin(index, [i for i, m in materials.items() if m == name])
        if not mask.any():
            continue

        f0 = float(definition["f0"])
        keep = float(definition.get("f0_from_art", 0.35))

        # One mean across all three channels rather than one each: per channel would neutralise
        # the hue, and brass is not steel that happens to be yellow. The colour channels
        # only - the fourth is the cut-out shape and is not a reflectance.
        rgb = lit[..., :3]

        # Measured over the texels the artist actually painted, not over every texel the
        # island owns.
        #
        # `owner` is dilated well past each island's edge on purpose - see padding_for - and
        # the albedo is not: outside its own bake margin it is black. For a large island that
        # rim is a rounding error in the mean. For a small one it is the mean. The Brass
        # armour's two gold quills are two triangles each, so at 22 texels of dilation their
        # mask is almost entirely bleed, and `was` came out at 0.000 - which sends the pull's
        # denominator to nothing, the gain to its ceiling, and the quills out black. They had
        # rendered as bright gold before, uncorrected, so this arrived as a regression at the
        # exact moment the correction started reaching them.
        #
        # The statistic is the art's, so it is taken from the art. The transform is still
        # applied to the whole dilated mask, because the bleed has to keep matching what it
        # bleeds from or every seam becomes an edge.
        painted = mask & (rgb.max(axis=2) > 1.0 / 255.0)

        # Black has no hue, and the pull must not give it one.
        #
        # The pull is a power per channel about one mean, which keeps each texel's channel
        # ratios - by design, so brass stays yellow when it is lifted. But MU's sheets are
        # JPEGs, and a JPEG stores chroma at a few counts of absolute error regardless of
        # how dark the texel is. At (19, 10, 10) that error *is* the colour: a 2:1 red ratio
        # that no eye can see at black and that the pull carried up to mid-grey, where it
        # read as a mauve band along the neck of the Mace's head. Zero pink texels in the
        # albedo, thirty-eight in the basecolor, all from texels under 24/255.
        #
        # So the chroma of a texel is trusted in proportion to how far it sits above the
        # JPEG's floor, and faded to its own grey below it: a texel at the floor keeps its
        # whole tint, one at black keeps none. Applied to the art before `was` and the pull
        # so the correction sees the same sheet it lands. 24/255 was judged on the Mace in
        # Blender from the angle the band showed; 16 left ten texels pink.
        floor = BLACK_FLOOR
        weights = np.array([0.2126, 0.7152, 0.0722], np.float32)
        luma = (rgb[mask] * weights).sum(axis=-1, keepdims=True)
        trust = np.clip(luma / floor, 0.0, 1.0)
        rgb[mask] = luma + (rgb[mask] - luma) * trust

        if not painted.any():
            print(f"  {name:<14} f0 skipped, its texels are black")
            continue

        was = float(rgb[painted].mean())
        if was <= 1e-6:
            print(f"  {name:<14} f0 skipped, its texels are black")
            continue

        pulled = f0 * (rgb[mask] / was) ** keep

        # And then landed on the target, which the line above does not do on its own.
        #
        # `(art / was) ** keep` averages to one only when keep is one. Below it the curve is
        # concave, so by Jensen the mean of the powers is under the power of the mean - and
        # the wider the art's spread the further under. The result was a stated f0 that no
        # metal in the project ever reached, undershooting by an amount nobody could predict
        # from the number they were editing: measured across the Plate set, the shield landed
        # 12% short, the boot 20%, the gauntlet 22%, and the breastplate - the most contrasty
        # sheet of the five, black engraving against white plate - 41% short, reflecting 0.339
        # where steel is 0.575.
        #
        # That is most of "the armour is dark and does not look like metal", and it explains
        # why it looked worst on exactly the pieces whose art has the most contrast in it. The
        # f0 in a material file is a physical constant with a source; it should be what the
        # surface reflects, not an upper bound the art quietly discounts.
        #
        # A gain about the achieved mean rather than a re-solved exponent, because the
        # exponent is the thing `f0_from_art` names - how much of the sheet's variation
        # survives - and it must not move to fix a level.
        #
        # Solved rather than scaled, because the clip is part of the arithmetic.
        #
        # Scaling by `f0 / mean` lands the target only while nothing clips, and on the darkest
        # sheets almost everything does: the Plate helm's steel averages 0.092, so a texel at
        # 0.9 comes out of the power at 1.78 and the ceiling takes two thirds of it away. Open
        # loop that left the helm at 0.174 and the greaves at 0.247 against a stated 0.575 -
        # better than the 0.092 they started from and still not the number the file claims.
        #
        # `mean(clip(gain * pulled))` rises with the gain and saturates at one, so it is
        # monotonic and a bisection finds the gain that lands f0 exactly. Forty steps is far
        # more precision than a texel has and costs nothing at this size.
        def solve(test) -> float:
            low, high = 0.0, 64.0
            for _ in range(40):
                middle = (low + high) * 0.5
                if test(middle):
                    low = middle
                else:
                    high = middle
            return (low + high) * 0.5

        # Solved on the painted texels for the same reason `was` is: a mean dominated by
        # black bleed would drive the gain to its ceiling on any small island.
        sample = f0 * (rgb[painted] / was) ** keep

        wanted = solve(lambda g: float(np.clip(sample * g, 0.0, 1.0).mean()) < f0)

        # And held back to whatever the sheet can take without going white, which is the
        # correction's other half and was missing on the first attempt.
        #
        # Landing f0 exactly is right when the art has room for it and wrong when it does
        # not: the Plate breastplate reached 0.575 with 49% of its texels against the
        # ceiling, which is half the plate flattened to paper white with the engraving gone.
        # "Too white, losing that realism" is what a mean hit by clipping looks like, and the
        # arithmetic already said so - the ceiling figure was in the report and I read it as a
        # diagnostic rather than as a limit.
        #
        # So the gain is the smaller of what lands f0 and what spends CEILING_BUDGET of the
        # sheet. A metal whose art is bright enough gets its stated reflectance; one painted
        # too dark and too contrasty gets as much as it can hold, and says how much it fell
        # short rather than paying for the number with its own detail.
        allowed = solve(
            lambda g: float((sample * g > 1.0).mean()) < CEILING_BUDGET)

        gain = min(wanted, allowed)
        held = gain < wanted * 0.999

        rgb[mask] = np.clip(pulled * gain, 0.0, 1.0)

        # What the ceiling cost, which is the honest reading of "this sheet is too dark to be
        # this reflective". A high figure is not a fault to fix here: it says the art's spread
        # cannot fit under one at this level, and the lever for that is `f0_from_art`.
        clipped = float((sample * gain > 1.0).mean())

        # An optional pull toward grey, after the correction so the stated f0 still lands.
        # A painted sheet's warmth is a picture of light, and on a metal it comes back as a
        # permanent tint in every reflection - steel that reflects slightly orange reads as
        # dirty brass. 1.0 (the default) is a no-op; plate_steel ships 0.75, judged on the
        # assembled set in Blender against the bench's own dome.
        grey_pull = float(definition.get("f0_desaturation", 1.0))
        if grey_pull < 1.0:
            weights = np.array([0.2126, 0.7152, 0.0722], np.float32)
            grey = (rgb[mask] * weights).sum(axis=-1, keepdims=True)
            rgb[mask] = np.clip(grey + (rgb[mask] - grey) * grey_pull, 0.0, 1.0)

        print(f"  {name:<14} f0 {was:.3f} -> {float(rgb[painted].mean()):.3f} "
              f"(target {f0:.2f}{' - held off, sheet too dark' if held else ''}, "
              f"{keep:.2f} of the art kept, {clipped * 100.0:.1f}% at the ceiling), "
              f"{int(mask.sum()) * 100.0 / mask.size:5.1f}% of the sheet")

    save(target, lit, colour=True)
    return target


def painted_relief(albedo: Path, size: int) -> "np.ndarray | None":
    """A height field from the baked albedo's own luminance, high-passed.

    High-passed because the low frequencies are not relief. A breastplate is bright down the
    middle and dark at its edges because it is curved and lit, and reading that as height
    would emboss the lighting onto the shape — a dome on top of a dome. What is wanted is
    what varies *faster* than the form: the fluting, the rivet heads, the scratches.

    Centred on zero, so it adds detail to the grain rather than replacing its level.
    """
    if not albedo.exists():
        return None

    # Blender's own loader, because this whole file runs inside Blender and its Python has
    # no PIL. Found the way these things are found: the import raised, the step died, and the
    # build went on to export a model with the maps it already had — every other line
    # reporting success. See the note on why that is the shape of failure this project keeps
    # having to go back and look for.
    import bpy

    picture = bpy.data.images.load(str(albedo), check_existing=False)

    try:
        wide, tall = picture.size
        flat = np.empty(wide * tall * 4, np.float32)
        picture.pixels.foreach_get(flat)
    finally:
        bpy.data.images.remove(picture)

    # Bottom-up in Blender, and left that way: every map this relief feeds is rasterised
    # bottom-up and saved without a flip. Flipping here put the painted relief and gloss
    # upside down on the art - inside the Bronze cuirass the normal's slope matched the
    # flipped sheet (0.10) and not the real one (0.00).
    rgba = flat.reshape(tall, wide, 4)
    lit = rgba[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)

    if (wide, tall) != (size, size):
        # Bilinear, and it used to be nearest — which is what "pixelated grain" was.
        #
        # The albedo is MU's sheet at its own upscale and the material maps are generated as
        # large as the grain needs, so on a small item these differ by a lot: a 64-pixel
        # shield sheet arrives here as a 192 albedo against a 1024 map. Sampled with integer
        # division that is one albedo texel repeated across five, and the field is then
        # differentiated to make normals — so every block edge becomes a step, and a surface
        # whose grain should be a fine pitting comes out as hard squares five texels across.
        #
        # The same two-pass np.interp value_noise uses, for the same reason: what is wanted is
        # a field smooth enough to take a gradient of.
        vertical = np.empty((size, wide), np.float32)
        rows = np.linspace(0, tall - 1, size)

        for column in range(wide):
            vertical[:, column] = np.interp(rows, np.arange(tall), lit[:, column])

        lit = np.empty((size, size), np.float32)
        columns = np.linspace(0, wide - 1, size)

        for row in range(size):
            lit[row] = np.interp(columns, np.arange(wide), vertical[row])

    # A box blur wide enough to be the form and no wider, by repeated rolls.
    wide = max(2, size // 64)
    slow = lit.copy()

    for axis in (0, 1):
        run = slow.copy()
        for step in range(1, wide):
            run = run + np.roll(slow, step, axis=axis) + np.roll(slow, -step, axis=axis)
        slow = run / (2 * wide - 1)

    high = lit - slow

    # Clipped, because the biggest excursions are not detail.
    #
    # The bake leaves black between the islands, so every island border is a step from art to
    # nothing — and a high pass reads that step as the loudest thing on the sheet. Measured on
    # Plate Armor it reached 0.93 where the detail inside an island is a few hundredths, and
    # unclipped it went into the height field as a cliff at every seam: a hard crease in the
    # normal map exactly where two pieces of one object meet, which is the fault that looks
    # like a modelling mistake.
    #
    # A tenth is comfortably above anything painted and comfortably below a border.
    return np.clip(high, -0.1, 0.1)


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: ... --python build_maps.py -- <in.blend> <asset.json> [outDir] [size] [libraryDir]")
        return

    blend = Path(argv[0])
    assignment = Path(argv[1])
    out = Path(argv[2]) if len(argv) > 2 else blend.parent
    size = int(argv[3]) if len(argv) > 3 else 2048

    item = blend.stem.removesuffix("_low")
    document = json.loads(assignment.read_text())

    # Named rather than assumed to sit beside the assignment. The library is shared by
    # every asset in the project — steel is steel whether it is a blade or a helm — so it
    # lives once, under assets/materials, and the caller says where that is.
    library_dir = Path(argv[4]) if len(argv) > 4 else assignment.parent.parent.parent / "materials"
    library = {}
    for definition in library_dir.glob("*.json"):
        library[definition.stem] = json.loads(definition.read_text())

    bpy.ops.wm.open_mainfile(filepath=str(blend))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        print(f"no mesh in {blend}")
        return

    mesh = meshes[0].data

    # The islands are recovered from the mesh rather than trusted from the file, and then
    # checked against it. An assignment made about a mesh that has since been re-unwrapped
    # is worse than no assignment: it is a confident statement about the wrong faces.
    bm = bmesh.new()
    bm.from_mesh(mesh)

    sys.path.insert(0, str(Path(__file__).parent))
    from islands import describe, islands_of      # noqa: E402  (same directory, by design)

    layers = (
        bm.loops.layers.uv[BAKE_UV],
        bm.loops.layers.uv[ORIGINAL_UV if ORIGINAL_UV in mesh.uv_layers else BAKE_UV],
    )

    groups = islands_of(bm)
    recorded = {island["island"]: island for island in document["islands"]}

    if len(groups) != len(recorded):
        raise SystemExit(
            f"error: the mesh has {len(groups)} islands and {assignment.name} describes "
            f"{len(recorded)}.\n       Re-run islands.py — it keeps the assignments that "
            f"still match.")

    island_of: dict[int, int] = {}
    materials: dict[int, str] = {}

    for index, group in enumerate(groups):
        measured = describe(bm, group, layers)
        island = recorded[index]

        if measured["triangles"] != island["triangles"] or measured["mu_uv"] != island["mu_uv"]:
            raise SystemExit(
                f"error: island {index} does not match {assignment.name} — the mesh has "
                f"{measured['triangles']} triangles at {measured['mu_uv']}, the file says "
                f"{island['triangles']} at {island['mu_uv']}.\n"
                f"       Re-run islands.py — it keeps the assignments that still match.")

        materials[index] = island["material"]
        for face in group:
            island_of[face] = index

    bm.free()

    # And a sheet may overrule the island it landed in.
    #
    # An island is a UV shell, and a shell is not always one surface: the unwrapper crosses
    # a material boundary whenever the geometry does, so MU's six triangles of hair on the
    # Bone Helm come through welded into the helm's own shell and take its material. The
    # island is the unit everywhere else in this pipeline and that is the right unit - it is
    # a piece of geometry, which is what a material is a claim about - but it is one unit
    # too coarse when the artist drew two surfaces on one shell and told them apart by the
    # sheet each samples.
    #
    # So a sheet may be named, and every face wearing it is lifted into an island of its
    # own. Nothing else moves: the recorded islands are still checked against the mesh
    # above, the material a face had is still the island's, and an asset that names no
    # sheet here is built exactly as before. That is the whole reason it is opt-in rather
    # than a seam marked at every sheet boundary, which would re-partition thirty of the
    # forty-seven multi-sheet assets and quietly drop the assignments that no longer match.
    #
    # The slot names are MU's own sheet stems - clean_lowpoly writes them and nothing after
    # it renames them - so an asset names the sheet it means and not a number.
    overrides = document.get("sheet_overrides") or {}

    next_island = len(groups)

    if overrides:
        slots = [material.name if material else "" for material in mesh.materials] or [""]
        lifted: dict[str, int] = {}
        counted: dict[str, int] = {}

        for face in mesh.polygons:
            sheet = slots[face.material_index] if face.material_index < len(slots) else ""
            wanted = overrides.get(sheet)

            if wanted is None:
                continue

            if sheet not in lifted:
                lifted[sheet] = next_island
                next_island += 1
                materials[lifted[sheet]] = wanted

            island_of[face.index] = lifted[sheet]
            counted[sheet] = counted.get(sheet, 0) + 1

        for sheet, index in sorted(lifted.items()):
            print(f"  sheet      {sheet} takes {materials[index]} on its own, "
                  f"{counted[sheet]} face(s) lifted out of the island they share")

        for sheet in sorted(set(overrides) - set(lifted)):
            print(f"  sheet      {sheet} is named in sheet_overrides and no face wears it")

    # And a *region* of MU's own sheet may overrule the island, which is the same argument
    # as the one above made one level finer.
    #
    # `sheet_overrides` answers the case where the artist drew two surfaces on one shell and
    # told them apart by the sheet each samples. It cannot answer the case where they are on
    # the *same* sheet and told apart by where on it they land — and that is the Dark
    # Wizard's torso, whose own island note has said so all along: "the robe and the bare
    # arms in one shell, as MU lays them out". One shell, one sheet, 120 triangles, and the
    # whole of it took the robe's material — so both bare arms were shaded as woven cloth
    # and came out under `cast` pitting at depth 0.04. That is the grainy skin, and it is
    # not a lighting fault or an upscaler fault: measured through the chain, MU's painting
    # of that arm is smooth at every stage and the grain is entirely this map's.
    #
    # A rectangle in MU's own UV space is the honest unit for it. It is MU's layout rather
    # than ours — the unwrapper never touches `mu_original` — so the rule is a statement
    # about the artist's sheet and can be checked by looking at it. Matched on the face's
    # UV centroid, so a face belongs to exactly one region however its corners straddle a
    # border, and first rule wins.
    #
    # Opt-in, like the sheet form and for the same reason: a boundary drawn automatically
    # at every colour change would re-partition assets nobody has looked at.
    regions = document.get("uv_overrides") or []

    if regions:
        uv = mesh.uv_layers[
            ORIGINAL_UV if ORIGINAL_UV in mesh.uv_layers else BAKE_UV].data
        placed: dict[str, int] = {}
        tally: dict[int, int] = {}

        for face in mesh.polygons:
            middle_u = sum(uv[loop].uv[0] for loop in face.loop_indices) / face.loop_total
            middle_v = sum(uv[loop].uv[1] for loop in face.loop_indices) / face.loop_total

            for rule, region in enumerate(regions):
                low_u, high_u = region["u"]
                low_v, high_v = region["v"]

                if not (low_u <= middle_u <= high_u and low_v <= middle_v <= high_v):
                    continue

                wanted = region["material"]

                if wanted not in placed:
                    placed[wanted] = next_island
                    next_island += 1
                    materials[placed[wanted]] = wanted

                island_of[face.index] = placed[wanted]
                tally[rule] = tally.get(rule, 0) + 1
                break

        for rule, region in enumerate(regions):
            if rule in tally:
                print(f"  region     u{region['u']} v{region['v']} takes "
                      f"{region['material']}, {tally[rule]} face(s) lifted")
            else:
                print(f"  region     u{region['u']} v{region['v']} is named in "
                      f"uv_overrides and no face's centre lands in it")

    unknown = {name for name in materials.values() if name not in library}
    if unknown:
        raise SystemExit(
            f"error: no definition in {library_dir} for: "
            f"{', '.join(sorted(unknown))}")

    print(f"\n=== {item} materials at {size} ===")

    # Grown to wherever the albedo's own bake bled to, rather than a fixed eight. See
    # padding_for for the rim of dielectric this was drawing round every island.
    padding = padding_for(size, out / f"{item}_albedo.png")

    if padding != PADDING:
        print(f"  padding    {padding} texels, matched to the albedo's bake margin")

    owner = dilate(rasterise(mesh, size, island_of), padding)

    # What the bake put down a moment ago, if it ran. An item with no sheets has no albedo
    # and keeps the grain alone, which is what it had before any of this.
    art = painted_relief(out / f"{item}_albedo.png", size)

    if art is not None:
        print(f"  art        {item}_albedo.png read for relief and gloss, "
              f"±{np.abs(art).max():.3f} about the form")
    covered = int((owner >= 0).sum())

    # One asset may say its own surface is smoother or rougher than the library's, per
    # material, without redefining what that material is everywhere else.
    #
    # The library states what steel *is* and that is the right place for it, but a grain is
    # generated at a cell count across the map and then judged against the art underneath —
    # and the art is not the same size on every piece. plate_steel's grain was settled on the
    # Plate armour, whose sheet is 384; the Plate shield's is 192, so the identical field
    # lands twice as coarse against its painting and reads as speckle rather than hammering.
    # Halving its depth was found in Blender under the game's own rig and is a fact about
    # this shield, not about steel.
    #
    #     "material_overrides": {"plate_steel": {"grain": {"depth": 0.025}}}
    #
    # Merged one level deep, so naming `grain.depth` keeps the kind, the scale and the
    # roughness range the library gave it.
    tweaks = document.get("material_overrides") or {}

    if tweaks:
        library = {name: dict(definition) for name, definition in library.items()}

        for name, over in tweaks.items():
            if name not in library:
                print(f"  override   {name} is named in material_overrides and nothing wears it")
                continue

            for key, value in over.items():
                if isinstance(value, dict) and isinstance(library[name].get(key), dict):
                    library[name][key] = dict(library[name][key], **value)
                else:
                    library[name][key] = value

                print(f"  override   {name}.{key} -> {value}")

    metallic = np.zeros((size, size), dtype=np.float32)
    roughness = np.full((size, size), 0.5, dtype=np.float32)
    height = np.full((size, size), 0.5, dtype=np.float32)
    classes = np.zeros((size, size, 3), dtype=np.float32)

    # One noise field per material rather than per island, so the two faces of a blade
    # carry the same grain and an edge-on view does not show them disagreeing.
    for name in sorted(set(materials.values())):
        definition = library[name]
        grain = definition.get("grain", {})

        # Seeded from the material's name, so rebuilding produces the same map and two
        # different materials never share a field.
        rng = np.random.default_rng(abs(hash(name)) % (2**32))
        field = grain_height(grain.get("kind", "smooth"), (size, size), grain, rng)

        mask = np.isin(owner, [index for index, m in materials.items() if m == name])
        if not mask.any():
            continue

        spread = float(grain.get("roughness_range", 0.0))

        # The grain, and then what MU actually painted on this one.
        #
        # Brighter is higher, which is the same assumption the ground makes and the one that
        # works on hand-painted art: what is light in MU's armour is light because it catches
        # the light, and what catches the light stands proud. Brighter is also smoother, for
        # the reason armour is: a raised surface is the part that gets rubbed.
        # How much of it, which a material may refuse outright.
        #
        # A body is the case that refuses. The constant exists because MU painted fluting and
        # rivets into a breastplate and at 768 they are legible — real relief, worth reading.
        # Bare skin and a cloth robe have no such detail painted on them, so what gets read
        # instead is whatever the enlarger left behind: the Dark Wizard's chest came out
        # covered in fine bumps, an isotropic normal map at step 1.66 where the Dark Knight's
        # torso has no normal map at all. His is one skin island and skin was already asking
        # for nothing; the difference was never the sheet.
        #
        # So it is per material, beside the grain and for the same reason. See the note on
        # FROM_THE_ART for why the default is a half rather than none.
        share = float(definition.get("from_the_art", FROM_THE_ART))
        painted = art[mask] * share if art is not None else 0.0

        metallic[mask] = float(definition.get("metallic", 0.0))
        roughness[mask] = (float(definition.get("roughness", 0.5))
                           + (spread * (field[mask] - 0.5))
                           - (spread * 2.0 * painted))
        height[mask] = field[mask] + painted

        colour = CLASS_COLOURS.get(name, (0.5, 0.5, 0.5))
        classes[mask] = colour

        print(
            f"  {name:<14} metallic {definition.get('metallic', 0.0):.2f}  "
            f"roughness {definition.get('roughness', 0.5):.2f} ±{spread:.2f}  "
            f"grain {grain.get('kind', 'smooth')}  "
            f"{int(mask.sum()) * 100.0 / (size * size):5.1f}% of the sheet")

    # Normals from the height field. The gradient is scaled by the material's own depth
    # before it is taken, so a soft leather wrap and a faint brushed steel come out of the
    # same code at genuinely different strengths.
    depth = np.full((size, size), 0.0, dtype=np.float32)
    for index, name in materials.items():
        depth[owner == index] = float(library[name].get("grain", {}).get("depth", 0.0))

    # The constant is calibrated rather than derived, and it was calibrated by looking. A
    # first pass without it put the blade under corrugated iron: the grain read as ridges
    # a millimetre deep, which is what happens when a height field whose features are four
    # texels wide is differentiated and then scaled by the map size. What a depth of 1.0
    # should mean is "as strong as this kind of surface ever gets", and for brushed steel
    # that is a disturbance you notice in the highlight and nowhere else.
    #
    # Divided by the map size rather than multiplied, so that rebuilding at 4096 gives the
    # same surface at finer resolution instead of a deeper one. The first version had this
    # the wrong way round and the depth in every material definition secretly meant
    # something different at every size.
    GRADIENT = 96.0

    scaled = height * depth * GRADIENT * (512.0 / size)
    dy, dx = np.gradient(scaled)

    normal = np.dstack([-dx, -dy, np.ones_like(scaled)])
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)

    out.mkdir(parents=True, exist_ok=True)
    written = []

    # Occlusion, roughness and metallic in one file's three channels, which is not a
    # space-saving trick — it is the arrangement glTF defines. A metallic-roughness
    # material reads roughness from green and metallic from blue of a single texture, and
    # occlusion from red of one that may be the same texture. Writing them separately and
    # packing them at export time would mean the file on disk and the file in the .glb
    # were different pictures, and only one of them could be looked at.
    #
    # Red is the traced occlusion, where there was one to trace. It used to be a constant
    # 1.0 — fully lit everywhere — because occlusion is baked from the mesh rather than
    # stated by a material and there was no step that did the tracing. There is now.
    #
    # Outside the islands it stays 1.0. A texel no face occupies is not occluded, it is
    # nothing, and darkening it would bleed shade inward across every seam.
    occlusion = bake_occlusion(meshes[0], size)

    if occlusion is None:
        occlusion = np.ones_like(roughness)
    else:
        occlusion = np.where(owner >= 0, occlusion, 1.0)

    # Roughness as the material asked for it, raised where this model's own grain would alias.
    # See specular_floor for why that is the same quantity, and ROUGHNESS_FLOOR for the backstop
    # underneath it. Written from the same normal array that is about to be written out, so the
    # two maps cannot disagree about the surface they describe.
    shading = np.maximum(specular_floor(normal, roughness), ROUGHNESS_FLOOR)

    if covered:
        skin = owner >= 0
        print(f"  roughness  asked {roughness[skin].min():.2f}-{roughness[skin].max():.2f}, "
              f"shaded {shading[skin].min():.2f}-{shading[skin].max():.2f} "
              f"(+{(shading - roughness)[skin].mean():.3f} mean for the grain)")

    orm = np.dstack([occlusion, shading, metallic])

    # A normal map that says "flat" everywhere is not written at all.
    #
    # Every material states a grain, and skin states none — a pore is far smaller than a
    # texel on a sheet that is one body wide, so inventing one would be inventing. An item
    # made only of skin therefore gets a normal map whose every texel is the same
    # unperturbed vector, and the two pure-skin parts were each shipping a megapixel of it.
    # Measured: their XY variation is 0.0 against 25 to 28 on anything with a grain.
    #
    # The exporter attaches what exists, so not writing it is the whole of removing it. A
    # renderer with no normal map uses the geometric normal, which is exactly what a
    # constant map was telling it to do the long way round.
    flat = float(np.abs(normal[..., 0]).max()) < 1e-6 and float(np.abs(normal[..., 1]).max()) < 1e-6

    maps = [("orm", orm), ("classes", classes)]
    if flat:
        print("  normal     flat everywhere — not written, the geometry already says it")
        (out / f"{item}_normal.png").unlink(missing_ok=True)
    else:
        maps.insert(1, ("normal", (normal * 0.5) + 0.5))

    for suffix, pixels in maps:
        path = out / f"{item}_{suffix}.png"
        save(path, pixels)
        written.append(path)

    # And the base colour, where a metal on this item asks for one. See base_colour: it is
    # derived from the albedo each build and written beside it, never into it.
    corrected = base_colour(item, out, owner, materials, library)
    if corrected is not None:
        written.append(corrected)

    print(
        f"\n  coverage   {covered * 100.0 / (size * size):.1f}% of the sheet "
        f"({padding} texels of bleed)\n"
        f"  wrote      {', '.join(p.name for p in written)}")


main()
