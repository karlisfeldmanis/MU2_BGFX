"""Builds a world's ground into a .glb, from what terrain.py extracted.

    python3 pipeline/ground.py source/world/lorencia workshop/world/lorencia

The heightmap says the shape and the tile map says what each square is made of, and the
awkward part is the second one: every tile wears its own texture, so the ground cannot be one
surface with one material on it.

It is split by texture instead. Every tile that wears TileGrass01 goes into one primitive,
every tile that wears TileWater01 into another, and each primitive gets that tile's sheet.
Lorencia uses nine of the slot table, so the whole 256-metre map is nine draws.

Tiles are not welded to each other. Four vertices per tile, 131072 triangles for the map,
and the duplication is the point rather than a cost worth avoiding: neighbouring tiles wear
different textures and therefore belong to different primitives, so a shared vertex would
have to carry two sets of texture coordinates at once, which is the one thing a vertex cannot
do. It also keeps MU's own faceting, which is what the client draws — the ground is a grid of
flat quads there too, and smoothing it across tile boundaries invents a roundness the art was
never lit for.

The texture coordinate rule is the one thing here that is easy to get wrong and impossible to
see is wrong. From the client's FaceTexture: it puts a fixed **64 texels of a tile sheet on
every terrain tile**, whatever that sheet measures. So a 256-pixel sheet spans four tiles, a
128-pixel one spans two, and a 64-pixel one spans exactly one — the reverse of the intuition,
because the bigger the sheet the larger its pattern is drawn. Baking one repeat figure for
every texture makes half of Lorencia's ground twice the size it should be and looks, at a
glance, entirely fine.

The overlay is the second half of the ground and it is what stops a four-metre tile reading
as wallpaper. A third of Lorencia's tiles name a second sheet and a weight between the two:
grass over sand is 9860 of them, which is every path edge in the town fading rather than
stopping at a line.

It is drawn as a second layer of quads rather than as a second sampler. Two textures in one
draw with a per-vertex weight is the obvious way and it needs a shader; a quad laid five
millimetres above the tile, wearing the overlay sheet and carrying the weight in its vertex
alpha, is the same picture out of a plain glTF material. Tiles whose weight is zero are
skipped, which is a third of the ones that name an overlay, so the extra geometry is 14556
quads against the ground's 65536.

And the light in this town is MU's, not this renderer's. The client does not light its
terrain at all — MuSunlight is explicit that its one directional light contributes nothing
and exists so a shadow has somewhere to land — so every bit of brightness in a MU world comes
from TerrainLight.OZJ, painted per tile with the sun's direction and the buildings' shade
already in it. It is multiplied into the vertex colour, which is a deliberate middle course:
used as the only light it would throw away every material in this project, and ignored it
leaves a 256-metre map under a studio lamp with nothing to shade it. Multiplied, the ground
keeps its roughness and its relief and gets MU's own light and shade over the top.
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

#: Texels of a tile sheet the client puts on one terrain tile, whatever the sheet measures.
TEXELS_PER_TILE = 64.0

#: MU units to a tile, and to a metre.
SCALE = 100.0

#: How far above its tile an overlay quad sits, in metres.
#:
#: Five millimetres. Enough that it never fights the ground it is laid on and far below
#: anything the eye resolves at a tile a metre across — a character's boot is thirty times
#: this. Coplanar would be correct and unrenderable.
OVERLAY_LIFT = 0.005

FLOAT = 5126
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
TRIANGLES = 4
LINEAR = 9729
LINEAR_MIPMAP_LINEAR = 9987
REPEAT = 10497


class Binary:
    """The .bin chunk and the accessors over it. The same shape export_gltf uses."""

    def __init__(self) -> None:
        self.blob = bytearray()
        self.views: list[dict] = []
        self.accessors: list[dict] = []

    def view(self, payload: bytes, target: int | None = None) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)

        entry = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(payload)}
        if target is not None:
            entry["target"] = target

        self.blob.extend(payload)
        self.views.append(entry)
        return len(self.views) - 1

    def floats(self, values: np.ndarray, kind: str) -> int:
        width = {"VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        flat = values.astype(np.float32).reshape(-1)

        self.accessors.append({
            "bufferView": self.view(flat.tobytes(), ARRAY_BUFFER),
            "componentType": FLOAT,
            "count": len(flat) // width,
            "type": kind,
            "min": values.reshape(-1, width).min(axis=0).tolist(),
            "max": values.reshape(-1, width).max(axis=0).tolist(),
        })

        return len(self.accessors) - 1

    def indices(self, values: np.ndarray) -> int:
        flat = values.astype(np.uint32).reshape(-1)

        self.accessors.append({
            "bufferView": self.view(flat.tobytes(), ELEMENT_ARRAY_BUFFER),
            "componentType": UNSIGNED_INT,
            "count": len(flat),
            "type": "SCALAR",
        })

        return len(self.accessors) - 1


def surface(height: np.ndarray, factor: float) -> np.ndarray:
    """Corner elevations in metres, one more than the tile count on each side."""
    size = height.shape[0]
    corners = np.zeros((size + 1, size + 1), dtype=np.float32)

    # A tile's byte is the elevation at its own corner, so the grid of corners is the grid of
    # tiles with one more row and column — wrapped, because the client indexes its terrain
    # with & 255 and a map's east edge really is its west one.
    corners[:size, :size] = height
    corners[size, :size] = height[0, :]
    corners[:size, size] = height[:, 0]
    corners[size, size] = height[0, 0]

    return corners * factor / SCALE


#: What power MU's baked light is raised to before it is used.
#:
#: One would be using it as painted, and using it as painted under-uses it. The ground faces
#: straight up, so a sun 63 degrees above the horizon reaches it at 0.89 of full strength
#: where a wall gets at most 0.45 — the ground is the best-lit surface in the town and also
#: the flattest, because one normal everywhere means one shading value everywhere. Measured:
#: the paving comes out at 1.32 times its own art where a building's wall comes out at 1.20,
#: which is why the tiles read as light against everything standing on them.
#:
#: The baked light is the only thing that varies across the ground, and at a mean of 0.56 laid
#: flat over a strong uniform sun most of its variation is swamped. Raising it to a power
#: pulls the middle down and leaves the bright end where it is: 0.56 becomes 0.44, so the
#: ground loses a fifth of its brightness, and the difference between a lit tile and a shaded
#: one grows — which is the town's own light and shade, becoming visible rather than being
#: averaged away.
LIGHT_CONTRAST = 1.45

#: And a flat scale under the curve, because a curve alone leaves the bright end where it is.
#:
#: A power fixes 1.0: whatever it does to the middle, a tile whose baked light is white stays
#: white. Lorencia's interiors are dark in TerrainLight and got most of the correction, and
#: its plaza is near-white and got none of it — the tavern floor came down and the city
#: paving did not move at all, which is exactly what a power does and was not what was
#: wanted.
#:
#: Sized rather than guessed. The paving comes out at 1.27 times its own art and a building's
#: wall at 1.15, so the ground needs about a tenth off to sit where the things standing on it
#: sit. Forcing every vertex colour to 0.25 and rendering it showed the frame responds at
#: roughly half the multiplier's change - 63% off the colour gave 30% off the picture - so a
#: tenth off the picture wants nearly three tenths off the light. The two together take a mean
#: of 0.56 to 0.31.
LIGHT_SCALE = 0.72

#: How much of the baked light's variation the ground keeps.
#:
#: TerrainLight is a shadow map at one texel per metre, and MU's own shadows are baked into
#: it. At forty pixels a tile that reads as shading. Here a texel is a metre and the camera
#: is close, so a baked shadow arrives as a soft stain several metres across with nothing
#: above it casting one — and now that the live sun genuinely casts and is genuinely
#: received, the two are visibly different kinds of mark on the same floor. The baked one
#: looks like a smudge on the lens.
#:
#: So the variation is kept and halved rather than removed. Removed, the town loses what it
#: is actually for: Lorencia is dark under its eaves and bright on its plaza because somebody
#: baked that, and no light in this scene knows it. Halved, that survives as a broad change
#: in level while stopping short of reading as a shadow in its own right, which is the job
#: the real sun now has.
#:
#: Applied after the curve and the scale, about the town's own mean, so the average tile is
#: exactly where it was and only the spread moves.
LIGHT_DEPTH = 0.5


def corner_light(lit: np.ndarray | None, size: int) -> np.ndarray:
    """MU's baked light at every corner, wrapped, in 0..1. White where there is none."""
    if lit is None:
        return np.ones((size + 1, size + 1, 3), dtype=np.float32)

    grid = np.ones((size + 1, size + 1, 3), dtype=np.float32)
    plane = np.power(lit.astype(np.float32) / 255.0, LIGHT_CONTRAST) * LIGHT_SCALE
    plane = plane.mean() + (plane - plane.mean()) * LIGHT_DEPTH

    grid[:size, :size] = plane
    grid[size, :size] = plane[0, :]
    grid[:size, size] = plane[:, 0]
    grid[size, size] = plane[0, 0]

    return grid


#: MU's one moving tile, by the name its own table gives it.
WATER = "TileWater01"


def at_the_vertex(weight: np.ndarray, size: int) -> np.ndarray:
    """MU's stored weight at each grid vertex, as the client reads it.

    This averaged the four tiles around each corner, and averaging was the wrong fix to a
    real problem. Read as a *face* value every tile blends uniformly and the ground is a
    checkerboard of hard squares, which is what the first version of this did; the average
    made it continuous and cost the data to do it. 5423 tiles MU paints at full strength come
    out with only 1573 corners still reaching it, the spread drops to 86% of what MU wrote,
    and a tile painted 255 with nothing around it arrives at 0.25 — a faint uniform square,
    which is exactly what the ground looked like.

    The old port does not average, and the reason is structural: MU's terrain is a continuous
    grid of *shared* vertices, so a stored value belongs to the vertex at that grid position
    and not to any of the four tiles touching it. TerrainMeshBuilder reads
    `mapping.Alpha[index] / 255f` straight onto the vertex and lets the rasteriser
    interpolate, which is both faithful and continuous — there was never a trade to make.

    So this reads it straight. Continuity comes from every tile that touches a grid position
    reading the same number there, which was always the property that mattered.
    """
    grid = np.zeros((size + 1, size + 1), dtype=np.float32)
    plane = weight.astype(np.float32) / 255.0

    grid[:size, :size] = plane

    # The far edges wrap, the way the client indexes its terrain.
    grid[size, :size] = plane[0, :]
    grid[:size, size] = plane[:, 0]
    grid[size, size] = plane[0, 0]
    return grid


def quads(rows, columns, corners, light, weights, span: float, lift: float):
    """One quad per tile: where it is, what it samples, and what light is on it."""
    count = rows.size
    points = np.empty((count * 4, 3), dtype=np.float32)
    uvs = np.empty((count * 4, 2), dtype=np.float32)
    colours = np.empty((count * 4, 4), dtype=np.float32)
    faces = np.empty((count * 2, 3), dtype=np.uint32)

    for n, (y, x) in enumerate(zip(rows, columns)):
        at = n * 4

        # MU is Z-up and glTF is Y-up. The map's x runs east and its y runs north, so north
        # becomes -Z here, which is the same conversion every held item uses.
        for k, (dx, dy) in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
            points[at + k] = (
                float(x + dx),
                float(corners[y + dy, x + dx]) + lift,
                -float(y + dy),
            )
            uvs[at + k] = ((x + dx) / span, (y + dy) / span)

            # Per corner, not per quad. See at_the_vertex: a weight read as a face value
            # makes every tile blend uniformly and the path comes out as hard squares.
            weight = 1.0 if weights is None else float(weights[y + dy, x + dx])
            colours[at + k] = (*light[y + dy, x + dx], weight)

        # Wound so the face looks up. Worth checking rather than assuming: the corners run
        # anticlockwise in the map's own frame, and negating Y into -Z reverses that, so the
        # obvious order gives a ground whose normal is (0, -1, 0) and is invisible from above.
        faces[n * 2] = (at, at + 1, at + 2)
        faces[n * 2 + 1] = (at, at + 2, at + 3)

    return points, uvs, colours, faces


def beside(images: list[dict], file: Path, out: Path, name: str) -> int:
    """Points the glTF at a texture on disk instead of swallowing it, and says which.

    Every sheet, normal and ORM used to go in through a buffer view, which is what makes a
    .glb self-contained and is the right call for a sword. It is the wrong call for a
    terrain: Lorencia's ground came to 402 MB, of which 342 MB was fourteen slots' worth of
    3072-square PNGs and 60 MB was the actual mesh. Every change to a single tile's repeat
    count rewrote all 402.

    glTF has said `uri` since 2.0 and Godot resolves it against the file's own directory,
    which is exactly where the tileset already put these — so the reference costs a relative
    path and the file drops to its geometry. The old client never embedded them either;
    MuTextureLoader reads them out of the data directory at run time and never imports them
    at all.

    What is given up is a ground that can be moved on its own, and it could not be moved on
    its own anyway: it is written into build/ beside its own art by a pipeline that puts them
    there together.
    """
    images.append({"name": name, "uri": file.name, "mimeType": "image/png"})
    return len(images) - 1


def write_glb(path: Path, document: dict, blob: bytearray) -> None:
    """Header, JSON chunk, binary chunk. Both padded to four."""
    while len(blob) % 4:
        blob.append(0)

    document["buffers"] = [{"byteLength": len(blob)}]
    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)

    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("wb") as out:
        out.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        out.write(struct.pack("<II", len(text), 0x4E4F534A))
        out.write(text)
        out.write(struct.pack("<II", len(blob), 0x004E4942))
        out.write(blob)


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 ground.py <world asset dir> <out dir> [sheets dir]",
              file=sys.stderr)
        raise SystemExit(2)

    source, out = Path(sys.argv[1]), Path(sys.argv[2])
    sheets = Path(sys.argv[3]) if len(sys.argv) > 3 else out

    # The world's own document, found by what it holds rather than by being first in the
    # directory. It shares that directory with every object declared for the map — sixty of
    # them now — and taking whichever the glob returned worked only while there was one.
    world = next(
        (document for path in sorted(source.glob("*.json"))
         if "objects" in (document := json.loads(path.read_text()))
         and "height" in document),
        None)

    if world is None:
        raise SystemExit(f"error: no world document in {source}")
    size = world["size"]

    height = np.asarray(Image.open(source / world["height"]).convert("L"), dtype=np.float32)
    planes = np.asarray(Image.open(source / world["tiles"]).convert("RGB"))
    base, overlay, alpha = planes[..., 0], planes[..., 1], planes[..., 2]

    corners = surface(height, world["height_factor"])

    print(f"\n=== {world['world']} ground ===")
    print(f"  grid       {size} x {size} tiles, {size * SCALE / 100:.0f} m square, "
          f"{corners.min():.2f} to {corners.max():.2f} m")

    binary = Binary()
    primitives: list[dict] = []
    materials: list[dict] = []
    images: list[dict] = []
    textures: list[dict] = []

    slots = world["tile_slots"]

    # What tileset.py prepared, if it has run: the upscaled sheet for each tile and the
    # normal and occlusion-roughness read off it. Keyed by the slot's own name, which is the
    # client's — TileGrass01 — rather than by the file, which is lower-cased.
    prepared: dict[str, dict] = {}
    listing = out / "tiles_slots.json"

    if listing.exists():
        prepared = {one["name"]: one for one in json.loads(listing.read_text())}
        print(f"  tileset    {len(prepared)} sheet(s) prepared with maps")

    lit = None
    if world.get("light") and (source / world["light"]).exists():
        lit = np.asarray(Image.open(source / world["light"]).convert("RGB"))
        print(f"  light      MU's own, mean "
              f"{lit.reshape(-1, 3).mean(axis=0).round().astype(int).tolist()} of 255")

    light = corner_light(lit, size)

    def sheet_for(name: str, blended: bool = False):
        """The prepared sheet for a slot, how far it reaches, and its material.

        An overlay may wear a different sheet from the layer under it, which is the one
        thing about the ground that a world object has no equivalent of. MU draws a tile
        twice — the ground, and then the neighbour fading into it — and both halves have
        always been the same picture at different alphas. They need not be: what fades onto
        a cobbled street at the edge of a square is grass, and saying so with a photoscanned
        grass rather than with a second copy of the cobbles is the difference between a
        transition and a smudge.
        """
        ready = prepared.get(f"{name}_over" if blended else name) or prepared.get(name)
        found = Path(ready["sheet"]) if ready and Path(ready["sheet"]).exists() else None

        if found is None:
            found = next((sheets / n for n in (f"{name.lower()}_x3_hd.png",
                                               f"{name.lower()}_x3.png",
                                               f"{name.lower()}.png")
                          if (sheets / n).exists()), None)

        # How far this sheet reaches, in tiles. 64 texels a tile whatever it measures, so a
        # 256-pixel sheet covers four of them. If the slot explicitly specifies reach, use it;
        # otherwise read off the native texture dimensions.
        reach = 1.0
        if ready and "reach" in ready:
            reach = float(ready["reach"])
        elif found is not None:
            with Image.open(found) as opened:
                native = opened.width // (3 if "_x3" in found.name else 1)
                reach = max(1.0, native / TEXELS_PER_TILE)

        # And what the asset asked for on top of it, as a repeat count and not as a stretch.
        #
        # This divided the wrong way round and the sign of that error is not subtle: the
        # bench sets Uv1Scale from the same number, where a larger value is more repeats and
        # so smaller stones, and here a larger value made one copy of the sheet span more
        # tiles and so bigger ones. Every decision taken on the Ground tab therefore arrived
        # in the build meaning its opposite. It showed up as Lorencia's hex inlay at a repeat
        # and a quarter: read as repeats that is a hex about a foot across, and what got
        # built was one copy of the sheet stretched over forty metres — hexes the size of a
        # room, which is what the tab was looking at when it said the sizes were wrong.
        #
        # The sheet's own texel count still sets what one repeat is worth. The asset is
        # saying how much oftener than that it should happen.
        if ready and ready.get("scale"):
            reach /= float(ready["scale"])

        return ready, found, reach

    layers = []

    def pair(under: str, above, rows, columns, weights) -> None:
        """One surface: a base texture, an overlay over it, and the tiles they share."""
        base_ready, base_sheet, base_reach = sheet_for(under, blended=False)
        over_ready, over_sheet, over_reach = (
            sheet_for(above, blended=True) if above else (None, None, 1.0))

        # UV in tiles, and the repeat handed to the shader as a number.
        #
        # This used to divide the coordinate by the reach, which bakes every tile-size
        # decision into the mesh: moving one slider from 10.6667 to 9.6 rewrote 190,600
        # triangles to say the same thing at a different rate. The repeat is one float per
        # layer and belongs where the old port puts it — a uniform — so the geometry stops
        # being a function of what anything is wearing. What is left in the .glb is the shape
        # of the ground, which only changes when the ground does.
        points, uvs, colours, faces = quads(
            rows, columns, corners, light, weights, 1.0, 0.0)

        normals = np.zeros_like(points)
        normals[:, 1] = 1.0

        material = {
            "name": under + (f"__{above}" if above else ""),
            "pbrMetallicRoughness": {
                "metallicFactor": float(base_ready["metallic"]) if base_ready else 0.0,
                "roughnessFactor": float(base_ready["roughness"]) if base_ready else 0.9,
            },
            "doubleSided": False,
        }

        # The base's own maps go into the glTF as well as into the sidecar, so the file still
        # describes something on its own — opened anywhere else it is a ground with its
        # ground texture on it, rather than an untextured grid.
        if base_sheet is not None:
            beside(images, base_sheet, out, base_sheet.stem)
            textures.append({"sampler": 0, "source": len(images) - 1})
            material["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": len(textures) - 1, "texCoord": 0,
            }

        def maps(ready):
            found = {}

            for file, kind in (((ready or {}).get("normal", ""), "normal"),
                               ((ready or {}).get("orm", ""), "orm")):
                path = Path(file)

                if path.exists():
                    found[kind] = path.name

            return found

        layers.append({
            "surface": len(primitives),
            "base": {
                "albedo": base_sheet.name if base_sheet else "",
                "repeat": round(1.0 / max(base_reach, 1e-6), 6),
                "relief": float((base_ready or {}).get("relief", 1.0)),
                "water": under == WATER,
                **maps(base_ready),
            },
            "overlay": {
                "albedo": over_sheet.name if over_sheet else "",
                "repeat": round(1.0 / max(over_reach, 1e-6), 6),
                "relief": float((over_ready or {}).get("relief", 1.0)),
                "water": above == WATER,
                **maps(over_ready),
            } if above else None,
        })

        primitives.append({
            "attributes": {
                "POSITION": binary.floats(points, "VEC3"),
                "NORMAL": binary.floats(normals, "VEC3"),
                "TEXCOORD_0": binary.floats(uvs, "VEC2"),
                "COLOR_0": binary.floats(colours, "VEC4"),
            },
            "indices": binary.indices(faces),
            "mode": TRIANGLES,
            "material": len(materials),
        })

        materials.append(material)

        print(f"  {under:14s} {rows.size:6d} tiles"
              + (f" under {above}" if above else " alone")
              + f", 1 copy per {base_reach:.2f} tile(s)"
              + ("" if base_sheet else "   NO SHEET - ships untextured"))

    # One surface per pair of textures, which is how the old port builds the same map.
    #
    # This drew the ground twice: an opaque quad per base slot, and a second transparent
    # quad over it per overlay slot. That works and costs what a second layer of geometry
    # costs — every blended tile drawn twice, sorted, and blended with the frame — and it
    # cannot do the one thing a fade most wants, which is to see both pictures at once. The
    # fragment only ever had the overlay in hand, so the height blend it does had to measure
    # the overlay against a fixed level rather than against what is actually underneath it.
    #
    # MuTerrainLoader buckets by (BaseTexture, OverlayTexture) and hands each bucket one
    # material carrying both. Same here. Lorencia comes out at 43 surfaces where it had 16,
    # which is more draw calls and fewer drawn triangles — and none of them transparent.
    #
    # <b>Which overlay a tile binds is the one it names, and the weight is one field.</b>
    #
    # This used to bind, per tile, whichever overlay "reached it most strongly" — every slot
    # got an alpha field of its own, masked down to the tiles naming that slot, and each tile
    # took the winner. It was invented here to solve a real-looking problem (the fade is
    # carried on the corners, so the outermost tile of a run is where the ramp reaches zero
    # and it looked as though such a tile needed a neighbour's overlay bound) and it is not
    # what the client does. RenderTerrainFace reads `TerrainMappingLayer2[TerrainIndex1]`, the
    # tile's own, with no reference to any neighbour, and takes its four corner weights out of
    # one global `TerrainMappingAlpha` — the same number at a grid position whichever of the
    # four tiles touching it is being drawn.
    #
    # The masking is what made the difference visible. A corner shared by two tiles naming
    # different overlays got the stored weight in one field and a zero in the other, so the
    # ramp collapsed to nothing along every boundary between two overlay regions — and 7,622
    # of Lorencia's 20,399 blended tiles sit on such a boundary. That is where the hard
    # straight edges along the tile grid were coming from: not from the blend being too sharp,
    # but from it being cut to zero on one side of a line the data does not draw.
    #
    # Read the client's way the field is continuous by construction, which is the property the
    # averaging in corner_blend was invented to buy and paid for with the data. Same 42
    # surfaces on Lorencia either way.
    weight_of = at_the_vertex(alpha, size)

    def corners_of(plane):
        """The four corner values of every tile, as the client's Index1..4 order."""
        return (plane[:size, :size], plane[:size, 1:], plane[1:, 1:], plane[1:, :size])

    faces = corners_of(weight_of)
    opaque = np.all([one >= 1.0 for one in faces], axis=0)
    clear = np.all([one <= 0.0 for one in faces], axis=0)

    # MU's own two collapses, from the same function.
    #
    #   all four corners at full   ->  tex1 = tex2, the tile is the overlay and nothing else
    #   all four corners at zero   ->  tex2 = tex1, the tile is the base and nothing else
    #
    # Both are one-layer tiles, and saying so is worth more than the draw call it saves: a
    # fully-covered tile drawn as base-plus-overlay-at-one is the overlay, arrived at through
    # a mix that has to sample and discard the picture underneath, and a tile at zero is the
    # base plus a texture that contributes nothing. Neither is wrong on screen. Both put a
    # second sheet in a material that has no use for it, which is a lie about what the ground
    # is made of in the one place — ground_surfaces.json — that this tab reads to find out.
    under_of = np.where(opaque, overlay, base)
    over_of = np.where(opaque | clear, -1, overlay)

    # A tile whose Layer2 is 255 has no second texture: that is the value
    # InitTerrainMappingLayer fills the array with, so it means "never painted" rather than
    # "slot 255". 203 of Lorencia's blended tiles are in that state — an alpha painted over an
    # overlay nobody chose — and the client hands 255 to Bitmaps[] and draws whatever is
    # there. Here it is a base tile, which is what those 203 look like in the client anyway.
    over_of = np.where(np.isin(over_of, [int(k) for k in slots]), over_of, -1)

    surfaces = []

    for one in sorted(slots, key=lambda k: int(k)):
        for two in sorted({int(v) for v in np.unique(over_of[under_of == int(one)])}):
            rows, columns = np.nonzero((under_of == int(one)) & (over_of == two))

            if rows.size:
                surfaces.append((int(one), two, rows, columns))

    for under, above, rows, columns in surfaces:
        pair(slots[str(under)], slots.get(str(above)), rows, columns,
             weight_of if above >= 0 else None)

    document = {
        "asset": {"version": "2.0", "generator": "MU2 pipeline/ground.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": f"{world['world']}_ground", "mesh": 0}],
        "meshes": [{"name": f"{world['world']}_ground", "primitives": primitives}],
        "materials": materials,
        "bufferViews": binary.views,
        "accessors": binary.accessors,
    }

    # What each surface is made of, beside the ground rather than inside it.
    #
    # glTF has one base colour per material and this needs two, so the pairing cannot be
    # said in the file itself. It could have been smuggled into an unused texture slot;
    # a list next to the model is the same information without the pun, and it is the
    # thing that has to be read anyway to build a shader material.
    #
    # It also means a tile's size is now a line of JSON. Changing one is this file
    # rewritten and nothing else — no mesh, no textures, no export.
    (out / "ground_surfaces.json").write_text(json.dumps(layers, indent=2))
    print(f"  wrote      {out / 'ground_surfaces.json'} — {len(layers)} surface(s)")

    if images:
        document["images"] = images
        document["textures"] = textures
        document["samplers"] = [{
            "magFilter": LINEAR,
            "minFilter": LINEAR_MIPMAP_LINEAR,

            # The ground's coordinates run to 256 and mean it.
            "wrapS": REPEAT,
            "wrapT": REPEAT,
        }]

    destination = out / f"{world['world']}_ground.glb"
    write_glb(destination, document, binary.blob)

    triangles = sum(binary.accessors[p["indices"]]["count"] for p in primitives) // 3
    print(f"  wrote      {destination} ({destination.stat().st_size // 1024} KB), "
          f"{len(primitives)} primitive(s), {triangles} triangles")


main()
