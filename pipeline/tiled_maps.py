"""Surface maps for a sheet that tiles, generated in the sheet's own space.

    python3 tiled_maps.py <slots.json> <materials dir>

The baked path traces its maps into the layout clean_lowpoly unwrapped: a grain normal from
the material definition, an occlusion pass from the mesh. Neither can follow a sheet that
repeats. A boulder wraps its rock face round itself twice, so there is no unique layout to
trace into, and that is the whole reason the tiled path exists.

What a repeating sheet can have is a map of its own size that repeats with it. This writes
one, and takes it from the art rather than from a noise function.

That is the substantive difference from build_maps, and it is worth stating plainly. There,
the grain is invented - a cast or brushed pattern generated to a scale and depth the material
declares, because MU's item art is a painting of a surface and carries no relief that a
normal could be recovered from. World art is not like that. ston01 is a photograph of a rock
face reduced to 128 pixels, and its light and dark *are* its cracks and its high spots: the
luminance is a height field that somebody already drew. Reading the slope off it gives a
normal that agrees with the picture, crack for crack, which no amount of generated noise can
do. Where the art has nothing to say the map comes out flat, which is honest.

The material still sets how far it goes. rock declares depth 0.30 and gets deep relief;
foliage declares no grain at all and gets no map, because the geometry is a flat card and the
shape the eye reads is the alpha - embossing a leaf texture across it announces that it is a
picture printed on a board.

Occlusion at the scale of the *mesh* is not written and cannot be. The same texels are on the
sunlit face of the boulder and under its overhang, so a repeating sheet has no way to hold
one, and there is no version of that which is not a lie.

Occlusion at the scale of the *surface* is a different question and it does get written. How
much light reaches the bottom of a crack in a rock, or the gap between two blades of grass,
is a property of the surface rather than of the object — it is the same wherever that texel
lands — and the height field the normal comes from is enough to compute it. That is a cavity
map, and on ground it is most of the difference between gravel and a photograph of gravel.

Roughness is written with it, and varies rather than sitting at the material's number. Real
ground is not uniformly rough: the dark parts of a grass tile are damp and shaded and smoother,
the pale parts are dry and dusty and rougher. The material sets the middle and the art sets the
spread, the same division of labour the normal already uses.
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

#: How far the slope is read across, in texels of the sheet.
#:
#: One. A normal from a height field is a difference between neighbours, and on art this
#: small anything wider is a blur: ston01 is 128 pixels holding a whole rock face, so its
#: cracks are one and two texels across and a five-texel kernel averages them away. The
#: sheet has already been upscaled by the time this runs, so "one texel" is one texel of the
#: enlarged sheet and the reach in terms of MU's original art is finer still.
REACH = 1

#: What a unit of luminance is worth as height, before the material's depth scales it.
#:
#: Chosen so that a material declaring depth 1.0 on art that swings from black to white gets
#: a slope of about 45 degrees, which is the steepest thing that still reads as a surface
#: rather than as a fault. Everything in the library declares far less than 1.0 - rock, the
#: deepest, asks for 0.30.
RELIEF = 4.0


def luminance(sheet: Image.Image) -> np.ndarray:
    """The picture as a height field, in 0..1.

    Rec. 709 weights, which is the same set decode_texture and upscale use for the same
    reason: green carries most of what the eye reads as brightness, and a flat average turns
    a green surface into a bright one.
    """
    plane = np.asarray(sheet.convert("RGB"), dtype=np.float32) / 255.0
    return 0.2126 * plane[..., 0] + 0.7152 * plane[..., 1] + 0.0722 * plane[..., 2]


def normal_map(height: np.ndarray, depth: float | np.ndarray) -> Image.Image:
    """normal_vectors, written as the RGB image glTF reads."""
    return Image.fromarray(
        ((normal_vectors(height, depth) * 0.5 + 0.5) * 255.0).round().clip(0, 255)
        .astype(np.uint8), "RGB")


def normal_vectors(height: np.ndarray, depth: float | np.ndarray) -> np.ndarray:
    """A tangent-space normal from a height field, wrapping at every edge.

    np.roll rather than an edge-clamped gradient, and that is the point of doing this here
    at all: the sheet is going to be laid next to itself, so the slope at its right edge has
    to be computed against its left edge or the seam shows as a hard crease running down
    every wall in the town.
    """
    scale = depth * RELIEF

    # Blender and glTF both take +Y as up in a tangent normal, and the height field's rows
    # run downward, so the Y difference is taken the other way round.
    dx = (np.roll(height, -REACH, axis=1) - np.roll(height, REACH, axis=1)) * scale
    dy = (np.roll(height, REACH, axis=0) - np.roll(height, -REACH, axis=0)) * scale

    vector = np.stack([-dx, -dy, np.ones_like(height)], axis=-1)
    vector /= np.linalg.norm(vector, axis=-1, keepdims=True)

    return vector


#: How far a glow's brightness is pushed before it becomes its own alpha. See `glow`.
GLOW_STRENGTH = 1.7

#: How hard the cavity map darkens, and the least it may leave.
#:
#: Both were far too strong, and the reason is a mistake worth naming: a cavity read off
#: luminance cannot tell a crevice from a dark patch of paint, so on art with a large
#: luminance swing it darkens what the artist already darkened. Painted dark is albedo;
#: occlusion is geometry. Multiplying one by the other counts it twice.
#:
#: At a strength of 2.0 the first percentile of the building sheets came out at 0.49 to 0.69
#: and the floor at 0.07 — a further halving of art already at luma 0.33, in ambient light,
#: which is how a house came out black. At 0.7 the worst texel loses under a third and a
#: crevice still reads as a crevice.
CAVITY_STRENGTH = 0.7
CAVITY_FLOOR = 0.65


def cavity(height: np.ndarray, reach: int = 6) -> np.ndarray:
    """How enclosed each texel is, in 0..1, where 1 is open sky.

    A texel that sits below its surroundings has less of the sky visible from it. Comparing
    the height field against a blurred copy of itself is the cheap standing-in for that, and
    on a surface it is the right cheap version: what is being asked is not "what is in the
    way" — nothing is, it is one continuous surface — but "how deep in a crevice is this",
    which is exactly what the difference from the local average measures.

    Wrapped, like everything else here, so the darkening does not stop at the sheet's edge
    and leave a bright seam running down the middle of a road.
    """
    blurred = height.copy()

    # A box blur by repeated rolls. Separable and wrap-aware, which is the whole requirement;
    # a Gaussian would be better shaped and needs a dependency this file does not have.
    for axis in (0, 1):
        stack = sum(np.roll(blurred, shift, axis=axis)
                    for shift in range(-reach, reach + 1))
        blurred = stack / (2 * reach + 1)

    # Only the hollows darken. A texel standing proud of its neighbours is not more lit than
    # open ground - it is just open ground - so the bright half is clamped away.
    return np.clip(
        1.0 + (height - blurred) * CAVITY_STRENGTH, CAVITY_FLOOR, 1.0)


def packed(height: np.ndarray, roughness: float | np.ndarray,
           spread: float | np.ndarray,
           metallic: float | np.ndarray = 0.0,
           normal: np.ndarray | None = None) -> Image.Image:
    """The occlusion-roughness-metallic map glTF asks for, as one RGB image.

    Red is occlusion, green roughness, blue metallic - which is glTF's own arrangement
    rather than a choice made here, and the same one build_maps packs for the baked path.

    Metallic is flat across the sheet and is the material's own, which it did not used to be.
    It was hardcoded to zero on the grounds that everything reaching this file was a
    dielectric - ground, rock, bark, timber, foliage - with a note saying a tiling metal would
    want a number here if one ever turned up. Lorencia's iron railings turned up, declared
    metallic 1.0, and got zero: the glTF sets metallicFactor to 1.0 because a factor
    multiplies its texture, so the product was nought and the bars rendered as a dielectric
    that happened to be dark. Found by checking what the maps carried rather than that they
    existed.
    """
    occlusion = cavity(height)

    # Bright is dry and rough, dark is damp and smooth. Centred on the material's own figure
    # so that the spread is a variation about a decision somebody made, rather than a
    # replacement for it.
    varied = np.clip(roughness + (height - float(height.mean())) * 2.0 * spread, 0.0, 1.0)

    # Then raised where this sheet's own normals would alias. See specular_floor.
    if normal is not None:
        varied = specular_floor(normal, varied)

    varied = np.maximum(varied, ROUGHNESS_FLOOR)

    plane = np.stack(
        [occlusion, varied, np.broadcast_to(np.asarray(metallic, np.float32), height.shape)], axis=-1)
    return Image.fromarray((plane * 255.0).round().clip(0, 255).astype(np.uint8), "RGB")


def keyed(slot: dict) -> None:
    """Gives a sheet an alpha channel cut from its own darkness, if it has none."""
    sheet = Path(slot["sheet"])

    if not sheet.exists():
        return

    opened = Image.open(sheet)

    if "A" in opened.getbands() and opened.getchannel("A").getextrema()[0] < 250:
        return

    image = opened.convert("RGB")
    height = luminance(image)

    # A hard step rather than a fade, because this is going to be alpha-tested and a fade
    # would only be thresholded later anyway. The level is the baked path's, which was
    # settled on the plume: the feather is painted on near-black and a tenth is comfortably
    # above the black and comfortably below anything drawn.
    mask = (height > 0.10).astype(np.uint8) * 255

    out = sheet.with_name(f"{sheet.stem}_keyed.png")
    cut = image.convert("RGBA")
    cut.putalpha(Image.fromarray(mask, "L"))
    cut.save(out, "PNG", optimize=True)

    print(f"  {slot['name']:10s} {slot['material']:9s} cut out on darkness, "
          f"{float((mask > 0).mean()) * 100:.0f}% kept  -> {out.name}")

    slot["sheet"] = str(out)


def edge(length: int, fraction: float) -> np.ndarray:
    """One axis of a border fade: one across the middle, nothing at either end."""
    if length < 2:
        return np.ones(length, dtype=np.float32)

    # Distance from the nearer end, as a fraction of the axis, so the two ends are
    # symmetrical however long it is.
    at = (np.arange(length, dtype=np.float32) + 0.5) / length
    inward = np.minimum(at, 1.0 - at) / max(fraction, 1e-6)

    # Smoothstep, so the fade has no corner of its own where it meets the middle.
    held = np.clip(inward, 0.0, 1.0)
    return held * held * (3.0 - (2.0 * held))


def glow(slot: dict, material: dict) -> None:
    """Turns an additively-drawn sheet into one a blended material can stand in for.

    MU draws its glows by adding them to what is already on screen. Black adds nothing, so
    the artist paints the light on black and gets a soft shape with no edge to it and no
    need for a mask - streetlight_brightness2 is two white streaks on a 32-square of it.

    glTF has no additive blend. What it has is alpha blending, and on a dark object the two
    are near enough indistinguishable if the alpha is taken from the brightness: where the
    sheet is black it contributes nothing either way, where it is white it contributes all of
    itself, and in between it is a fade rather than an add. Against a bright background an
    add would keep going and a blend will not, which is the one case this is wrong in - and
    a lamp is looked at against a night street rather than against the sky.

    So the alpha becomes the luminance and the colour is left alone. The material that reads
    it does the other half: black base, the sheet as emission, so the quad is lit by nothing
    and covers nothing.
    """
    sheet = Path(slot["sheet"])
    if not sheet.exists():
        return

    image = Image.open(sheet).convert("RGB")

    # Softened, before its alpha is taken off its brightness — so the fade the alpha
    # inherits is the soft one rather than the painted edge.
    #
    # The feather below dissolves the quad's *border*; this dissolves the shape inside it. A
    # lit window with MU's pattern still legible reads as a picture of light hanging in an
    # opening rather than as light coming through one, and the pattern is what gives it away.
    #
    # Measured against the short axis, which is the correction. These sheets are strips —
    # light_02 is six pixels by 192 — and a radius taken off the long side blurs the short
    # one out of existence, which is exactly what turned a window into a flat yellow band on
    # the first attempt at this.
    blur = float(material.get("emissive_blur", 0.0))

    if blur > 0.0:
        # Capped, because the fraction is the right shape and the wrong scale at both ends.
        #
        # A window's strip is eighteen pixels across its short axis and wants about two; a
        # bonfire's glow sheet is square and 192, and the same fraction asks for fifty-four,
        # which does not soften a flame so much as delete it. Eight is about the most a glow
        # can take and still be the shape MU painted.
        radius = min(max(0.5, blur * min(image.size)), 8.0)
        image = image.filter(ImageFilter.GaussianBlur(radius))
        print(f"  {slot['name']:10s} {slot['material']:9s} glow blurred by {radius:.1f} px "
              f"({blur * 100:.0f}% of its short axis)")

    # The alpha is the brightness, lifted.
    #
    # Taken raw it is too weak: a flame's own art averages about half brightness, so a flame
    # came out half transparent and read as a pale sheet of cellophane with the ground showing
    # through it. Additive blending does not have that problem - what it adds, it adds - and
    # this is standing in for additive with a blend, so the mask has to be pushed to
    # compensate. At 1.7 the bright body of a flame reaches opaque and the black around it is
    # still nothing, which is the shape an add would have had.
    lift = np.clip(luminance(image) * GLOW_STRENGTH, 0.0, 1.0)

    # And taken to nothing before it reaches any border.
    #
    # The one thing light has no business having is an edge, and the edge that showed was
    # never the picture's — it was the quad's. A lit window drew as a bright card with four
    # straight sides, and no amount of softening *inside* the sheet can reach a border the
    # sheet runs all the way up to. Feathering the alpha does: where the sheet is
    # transparent at its rim the quad has no rim left to show.
    #
    # Per axis rather than by radius, because these are not squares. light_02 is six pixels
    # by 192, and a radius taken off the long side wipes the short one out entirely — which
    # is exactly what a Gaussian blur did to it, turning a window into a flat yellow band.
    feather = float(material.get("emissive_feather", 0.0))

    if feather > 0.0:
        wide, tall = image.size
        across = edge(wide, feather)[None, :]
        down = edge(tall, feather)[:, None]
        lift = lift * across * down

    keyed = image.convert("RGBA")
    keyed.putalpha(Image.fromarray(
        (lift * 255.0).round().clip(0, 255).astype(np.uint8), "L"))

    out = sheet.with_name(f"{sheet.stem}_glow.png")
    keyed.save(out, "PNG", optimize=True)

    covered = float((luminance(image) > 0.04).mean())
    print(f"  {slot['name']:10s} {slot['material']:9s} additive; alpha keyed off brightness, "
          f"{covered * 100:.0f}% of the square carries any  -> {out.name}")

    slot["sheet"] = str(out)
    slot["emissive"] = True

    # How far past white it sits, which is what decides whether the frame blooms it. See
    # lamplight: a glow at exactly one never crosses the threshold and reads as a panel.
    if material.get("emissive_energy"):
        slot["emissive_energy"] = float(material["emissive_energy"])


def toned(slot: dict, into: Path) -> None:
    """Saturation into the albedo, and metallic into the ORM's blue.

    Both come off the Objects tab, where they were judged by looking, and neither can be
    committed as a factor the way a tint or a roughness can.

    Saturation because glTF has no such factor at all: baseColorFactor multiplies, and a
    multiply moves a colour towards black or towards another colour but never towards its own
    grey. The one place the sum can happen is the pixels, so it happens here, once, at build
    time — which is also the cheap place for it.

    Metallic and roughness because a factor is the wrong instrument even though glTF has one
    for each. A factor multiplies its channel, so what it expresses is "some fraction of what
    the map says" and what the bench expresses is a number. For metallic the two are not even
    close: every ORM this pipeline ships or imports has blue at exactly zero, so the factor has
    nothing to scale and asked for metallic 1 the honest answer is 0. Roughness is subtler and
    the same mistake — a factor of 0.33 over a map averaging 0.82 lands at 0.27, and the asset
    would quietly hold a number nobody chose.

    That is not a quirk to work around, it is the format saying the map is authoritative. So
    both values are written into the map, and the map then says what the bench said.
    """
    saturation = float(slot.get("saturation", 1.0))
    metal = slot.get("metal_value")
    rough = slot.get("roughness_scale")

    if saturation != 1.0 and Path(slot["sheet"]).exists():
        sheet = Image.open(slot["sheet"]).convert("RGB")
        pixels = np.asarray(sheet, dtype=np.float32)

        # Rec. 601, which is the luminance every other saturation in this pipeline is
        # measured against, so one asset's number means what another's does.
        grey = (pixels @ np.array([0.299, 0.587, 0.114], dtype=np.float32))[..., None]
        moved = np.clip(grey + (pixels - grey) * saturation, 0.0, 255.0)

        # Named after the sheet and the number, not after the slot.
        #
        # The slot has to be out of it in both directions. Naming only the sheet lets one slot
        # overwrite another's map — that is how seven building assets came to wear a texture
        # nobody had chosen, when two Fab packs were installed as brick_wall_fab. Naming the
        # slot fixes that and causes the opposite waste: House01 wears one photoscan on all
        # four of its slots at the same saturation, and four identical copies of a 1024 sheet
        # went into the file because four names pointed at the same picture.
        #
        # The number is what actually distinguishes the results, so the number is the name.
        out = into / f"{Path(slot['sheet']).stem}_sat{saturation:.2f}.png"

        if not fresh(out, Path(slot["sheet"])):
            Image.fromarray(moved.astype(np.uint8), "RGB").save(out, "PNG", optimize=True)

        slot["sheet"] = str(out)
        print(f"  {slot['name']:10s} saturation {saturation:.2f} -> {out.name}")

    if (metal is None and rough is None) or not slot.get("orm"):
        return

    if not Path(slot["orm"]).exists():
        return

    packed = np.asarray(Image.open(slot["orm"]).convert("RGB")).copy()
    said = []

    # Green is roughness and blue is metallic, which is glTF's packing and what the whole
    # pipeline reads. Flat, because these are answers rather than variations: the bench asked
    # for a number, and a number is not a distribution.
    # Roughness is moved to the asked mean rather than flattened onto it.
    #
    # The bench flattens, because a slider that only scales a map cannot be read as a number
    # and the point of the control is to answer "how rough should this be". Baking that
    # answer flat would throw away the one thing the photoscan was imported for: real ground
    # is not uniformly rough, the damp shaded parts are smoother and the dry raised parts
    # rougher, and that variation is measured and sitting in the channel. So the map keeps its
    # spread and the whole of it slides until its average is the number that was chosen.
    if rough is not None:
        green = packed[..., 1].astype(np.float32)
        wanted = min(max(float(rough), 0.0), 1.0) * 255.0
        packed[..., 1] = np.clip(green + (wanted - green.mean()), 0, 255).astype(np.uint8)
        said.append(f"roughness to a mean of {float(rough):.2f}")

    if metal is not None:
        packed[..., 2] = int(round(min(max(float(metal), 0.0), 1.0) * 255))
        said.append(f"metallic {float(metal):.2f}")

    # Same again: by what was asked for rather than by who asked, so two slots wanting the
    # same surface at the same numbers share one map and two wanting different ones cannot
    # collide. See the saturated sheet above.
    out = into / (f"{Path(slot['orm']).stem}"
                  + (f"_r{float(rough):.2f}" if rough is not None else "")
                  + (f"_m{float(metal):.2f}" if metal is not None else "") + ".png")
    if not fresh(out, Path(slot["orm"])):
        Image.fromarray(packed, "RGB").save(out, "PNG", optimize=True)

    slot["orm"] = str(out)
    print(f"  {slot['name']:10s} {' and '.join(said)} into the ORM -> {out.name}")


def spread_over(shape: tuple[int, int], base: float, regions: list[dict],
                key: str) -> np.ndarray:
    """One material constant as a field over the sheet, with regions painted into it.

    Every number the two derivations below take — depth, roughness, its spread, metallic —
    used to be a scalar per sheet, and the scalar is the thing that cannot describe an atlas.
    MU drew Well02 as one 128-square with a plank roof above a stone shaft, and there is no
    single roughness that is right for both.

    Made a field rather than resolved by splitting the mesh, which was the other way to do
    it. Splitting means cutting faces by which region their UVs land in, and MU's UVs are not
    laid out to be cut: the cannon's islands each span most of the atlas. Painting the
    constants instead leaves the geometry exactly as it is and changes only what the derived
    maps say at each texel — which is where the difference between iron and oak lives anyway.

    Boxes are u0, v0, u1, v1 in 0..1, with v down from the top of the image, because that is
    how somebody reads the picture they are looking at when they write one down.
    """
    field = np.full(shape, base, dtype=np.float32)
    rows, columns = shape

    for one in regions:
        u0, v0, u1, v1 = one["box"]
        top, bottom = int(round(v0 * rows)), int(round(v1 * rows))
        left, right = int(round(u0 * columns)), int(round(u1 * columns))
        field[max(0, top):bottom, max(0, left):right] = float(one[key])

    return field


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
#: A floor is the blunt end of the standard fix, and at 0.72 it flattened every tiled surface
#: that asked for less: all of Lorencia's iron — railings, gate, sign, lamp, the cannon's
#: barrel — shipped at 0.722 where wrought_iron asks 0.50, and Noria's water at 0.722 where
#: water asks 0.08. build_maps replaced its copy with the careful version on 2026-09-05
#: (63778a3) and said in its commit that this one was left behind.
#:
#: So the same careful version here: see specular_floor. The constant that remains is
#: build_maps' backstop, below anything the library asks for but water.
ROUGHNESS_FLOOR = 0.18

#: How hard the normals' own variation pushes roughness up. build_maps' SPECULAR_AA, and 1.0
#: for the same reason: the physical figure, variances adding.
SPECULAR_AA = 1.0


def soften(field: np.ndarray, passes: int = 2) -> np.ndarray:
    """build_maps' small box blur, wrapping rather than clamping because this sheet repeats."""
    out = field
    for _ in range(passes):
        out = (np.roll(out, 1, 0) + np.roll(out, -1, 0)
               + np.roll(out, 1, 1) + np.roll(out, -1, 1) + 4.0 * out) / 8.0
    return out


def specular_floor(normal: np.ndarray, roughness: np.ndarray) -> np.ndarray:
    """Roughness raised wherever the normals disagree within a shading footprint.

    build_maps.specular_floor, which argues it at length: alpha is roughness squared and a
    variance, the normals' spread inside a footprint is a variance too, and variances add.
    `1 - |mean normal|²` is that spread with no constant in it — 0 on a flat patch, so a smooth
    bar keeps its 0.50, and larger across a grain ridge, which is where the blue came from.
    Copied rather than imported because build_maps needs Blender to load.
    """
    mean = np.dstack([soften(normal[..., channel]) for channel in range(3)])
    agreement = np.clip(np.linalg.norm(mean, axis=2), 0.0, 1.0)

    alpha = np.square(np.clip(roughness, 0.0, 1.0))
    alpha += SPECULAR_AA * (1.0 - np.square(agreement))

    return np.sqrt(np.clip(alpha, 0.0, 1.0))


def fresh(out: Path, *inputs: Path) -> bool:
    """Whether this map is already on disk and newer than everything it was made from.

    Every derivation in this file is a pure function of a sheet, a material and a number, so
    a result that exists and post-dates all three cannot differ from the one another run
    would produce. Without this the whole set was rebuilt on every pass: a cached run over
    Lorencia's fourteen ground slots — nothing installed, nothing upscaled, one tile's repeat
    count moved by a hundredth — took three and a half minutes, all of it re-deriving normals
    that came out byte-identical.

    That is the difference between changing one tile and rebuilding a world, and it is the
    reason the cost was never questioned: it looked like the price of the ground rather than
    the price of a decision. ground.py, which actually rebuilds the terrain mesh, takes two
    and a half seconds.
    """
    if not out.exists():
        return False

    when = out.stat().st_mtime

    return all(not one.exists() or one.stat().st_mtime <= when for one in inputs)


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 tiled_maps.py <slots.json> <materials dir>", file=sys.stderr)
        raise SystemExit(2)

    listing = Path(sys.argv[1])
    library = Path(sys.argv[2])
    slots = json.loads(listing.read_text())

    print(f"\n=== tiling maps for {len(slots)} sheet(s) ===")

    for slot in slots:
        definition = library / f"{slot['material']}.json"
        material = json.loads(definition.read_text()) if definition.exists() else {}
        grain = material.get("grain")

        # Carried onto the slot so that whatever consumes it does not have to open the
        # library a second time to find out what the surface is.
        slot["metallic"] = float(material.get("metallic", 0.0))
        slot["roughness"] = float(material.get("roughness", 0.6))

        # Whether this surface's normals are the geometry's or the shape's. See foliage.
        if material.get("normals"):
            slot["normals"] = material["normals"]

        # How much of its own colour a surface shows where no light reaches it. See foliage:
        # a leaf in shadow is lit through rather than not at all.
        if material.get("translucency"):
            slot["translucency"] = float(material["translucency"])

        # A region's slot wears its parent's maps, which already carry the region painted in
        # texel by texel (see spread_over). mu2.sh says so where it makes one — "same sheet and
        # same maps as its parent" — and this is where that is kept. It used to derive a map of
        # its own from the region's material across the whole sheet, so every face the split
        # sent here was that material edge to edge: Cannon01's crate planks shipped at metallic
        # 1.0, and a chest whose brass bands frame its panels could not be given a region at
        # all, since each panel's corners sit on the band and the majority rule sends the whole
        # board with them. Filled in after the loop, once the parent's maps exist.
        if slot.get("of"):
            continue

        if slot.get("additive"):
            glow(slot, material)
            continue

        # A cut-out whose sheet has no alpha to cut with.
        #
        # MU does not always keep this in the file. mushroom.OZJ is opaque and the client
        # still draws it see-through, because the art is painted on near-black and the black
        # is the part that is not there — the same convention its effects use. The asset says
        # so and the alpha is keyed off darkness here, which is what the baked path has done
        # for the Plate helm's plume since that came back as two brown rectangles.
        if slot.get("cutout"):
            keyed(slot)

        if not grain:
            print(f"  {slot['name']:10s} {slot['material']} declares no grain; no normal map")
            continue

        sheet = Path(slot["sheet"])
        if not sheet.exists():
            continue

        image = Image.open(sheet)
        height = luminance(image)
        # A sheet that brought its own maps keeps them.
        #
        # This is the whole reason a Fab surface is worth importing rather than repainting.
        # Everything below derives relief from the albedo's own luminance, which is a guess
        # that works on hand-painted art and fails in one specific way: paint that is dark
        # because it is dark, rather than because it is deep, comes out as a dent. A
        # photoscanned normal knows the difference, and so does a measured roughness.
        given = {k: slot.get(f"given_{k}") for k in ("normal", "orm")}

        if given["normal"] or given["orm"]:
            for kind, path in given.items():
                if path and Path(path).exists():
                    slot[kind] = path

            print(f"  {slot['name']:10s} {slot['material']:9s} "
                  + "brought its own "
                  + " and ".join(k for k, v in given.items() if v)
                  + "; nothing derived")
            continue

        depth = float(grain.get("depth", 0.0))

        if depth <= 0.0:
            print(f"  {slot['name']:10s} {slot['material']} declares depth 0; no normal map")
            continue

        # Only where the sheet is opaque. Under a cut-out the colour is whatever upscale's
        # bleed grew there, which is a continuation rather than a picture, and taking a
        # slope off it would emboss the shape of the mask onto the material.
        if "A" in image.getbands():
            mask = np.asarray(image.getchannel("A"), dtype=np.float32) / 255.0
            height = height * mask + float(height[mask > 0.5].mean() if (mask > 0.5).any()
                                           else 0.0) * (1.0 - mask)

        # Named after the sheet *and* the material, because one sheet can now be two
        # materials at once. Well02 ships as a rock slot and a timber slot over one 128
        # square, and named after the sheet alone both derivations wrote the same file: the
        # second overwrote the first and both parts wore whichever material ran last, which
        # is a silent wrong answer of exactly the shape this pipeline keeps producing.
        #
        # The same rule the saturated sheets and the ORM variants already follow: the name
        # carries what distinguishes the result, so two slots wanting the same thing share
        # one file and two wanting different things cannot collide.
        out = sheet.with_name(f"{sheet.stem}_{slot['material']}_normal.png")
        maps = sheet.with_name(f"{sheet.stem}_{slot['material']}_orm.png")

        if (not (slot.get("regions") or [])
                and fresh(out, sheet, definition, Path(__file__))
                and fresh(maps, sheet, definition, Path(__file__))):
            slot["normal"], slot["orm"] = str(out), str(maps)
            print(f"  {slot['name']:10s} {slot['material']:9s} maps are up to date")
            continue

        regions = slot.get("regions") or []

        normal = normal_vectors(height, spread_over(height.shape, depth, regions, "depth"))
        Image.fromarray(((normal * 0.5 + 0.5) * 255.0).round().clip(0, 255).astype(np.uint8),
                        "RGB").save(out, "PNG", optimize=True)
        slot["normal"] = str(out)

        packed(
            height,
            spread_over(height.shape, float(material.get("roughness", 0.6)),
                        regions, "roughness"),
            spread_over(height.shape, float(grain.get("roughness_range", 0.0)),
                        regions, "roughness_range"),
            spread_over(height.shape, float(material.get("metallic", 0.0)),
                        regions, "metallic"),
            normal,
        ).save(maps, "PNG", optimize=True)
        slot["orm"] = str(maps)

        swing = float(height.max() - height.min())
        print(
            f"  {slot['name']:10s} {slot['material']:9s} depth {depth:.2f}  "
            f"art swings {swing:.2f} of full range  -> {out.name}, {maps.name}")

    parents = {slot["name"]: slot for slot in slots}
    for slot in slots:
        parent = parents.get(slot.get("of", ""))
        if parent is not None:
            for kind in ("normal", "orm"):
                if parent.get(kind):
                    slot[kind] = parent[kind]

    for slot in slots:
        toned(slot, listing.parent)

    listing.write_text(json.dumps(slots, indent=2))


main()
