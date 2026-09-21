"""Reads a MU world out of the client's data, into things the pipeline can build from.

    python3 pipeline/terrain.py <Data/World1> lorencia source/world

A map is four separate files that only mean anything together:

    TerrainHeight.OZB   the ground's shape, one byte a tile
    EncTerrain1.map     which two tile textures each tile wears, and their blend
    EncTerrain1.obj     every tree, wall and lamp standing on it, with its angle and size
    TerrainLight.OZJ    the light MU baked into the ground, one colour a tile

Three of those are encrypted with the same cipher and none of them is large: the whole of
Lorencia is 256 by 256 tiles, which at MU's hundred units to a tile is 256 metres square.

What comes out is deliberately not one big JSON document. The grids are written as PNGs
because that is what they are — 256-square images of a byte a pixel — and a PNG of them is
forty times smaller than the same numbers spelled out in text, opens in anything, and can be
looked at, which for a heightmap is most of how you tell whether you read it correctly. Only
the object list becomes JSON, because a list of two thousand records with names on them is
the one part that is not an image.

The heightmap needs no key at all. It ships beside the .OZB as a plain .bmp, and the one
thing to know is that the client reads its pixels from a fixed offset of 1080 rather than
from the 1078 its own header declares - so a reader that trusts the header is two bytes out
for the whole map and produces a picture that looks almost right.
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

#: The grid is this on a side, in tiles, on every map MU ships.
SIZE = 256

#: MU units to a tile, and to a metre. A tile is a metre.
SCALE = 100.0

#: What a height byte is worth, for an ordinary in-game map.
#:
#: The login scene uses 3.0 instead, to exaggerate its terrain for a camera that only ever
#: looks at one corner of it. Every other map, the character scene's included, uses this.
HEIGHT_FACTOR = 1.5

#: The one map that uses 3.0, by its data folder's number: WD_55LOGINSCENE, and a folder is
#: its world plus one (`GMBattleCastle.cpp:89`), so World56. The character scene's World75 is
#: not in it and takes the ordinary 1.5. `OpenTerrainHeight`, ZzzLodTerrain.cpp:732.
STEEP_MAPS = {56}

#: Where the client reads eight-bit height data, in place of the offset the bitmap declares.
EIGHT_BIT_PIXELS = 1080

#: The four bytes an .OZB carries in front of the bitmap that is the whole of its content.
OZB_PREFIX = 4

#: The map cipher, from MapFileDecrypt in ZzzLodTerrain.h.
#:
#: Length-preserving and trivially reversible: each byte is XORed against a rotating
#: sixteen-byte table, then has a running key subtracted, and the running key is derived from
#: the *previous ciphertext* byte rather than from the plaintext. It is obfuscation rather
#: than encryption and was never anything else.
MAP_XOR_KEY = bytes([
    0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
    0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2,
])
MAP_KEY_SEED = 0x5E
MAP_KEY_INCREMENT = 0x3D

#: Lorencia's object table, from the client's own. See WorldObjectCatalog.cs.
#:
#: Every other world numbers its objects straight through, so the type index is the file
#: number. Lorencia is the exception and runs in named blocks with gaps between them, which
#: is why this has to be written down rather than computed: type 30 is Stone01 because Stone
#: starts at 30, and there is nothing in the map file that says so.
LORENCIA_RANGES = [
    (0, 13, "Tree"), (20, 8, "Grass"), (30, 5, "Stone"), (40, 3, "StoneStatue"),
    (43, 1, "SteelStatue"), (44, 3, "Tomb"), (50, 2, "FireLight"), (52, 1, "Bonfire"),

    # Spelled this way in the client, and so on disk.
    (55, 1, "DoungeonGate"),

    (56, 2, "MerchantAnimal"), (58, 1, "TreasureDrum"), (59, 1, "TreasureChest"),
    (60, 1, "Ship"), (65, 3, "SteelWall"), (68, 1, "SteelDoor"), (69, 6, "StoneWall"),
    (75, 4, "StoneMuWall"), (80, 1, "Bridge"), (81, 4, "Fence"), (85, 1, "BridgeStone"),
    (90, 1, "StreetLight"), (91, 3, "Cannon"), (95, 1, "Curtain"), (96, 2, "Sign"),
    (98, 4, "Carriage"), (102, 2, "Straw"), (105, 1, "Waterspout"), (106, 4, "Well"),
    (110, 1, "Hanging"), (111, 1, "Stair"), (115, 5, "House"), (120, 1, "Tent"),
    (121, 6, "HouseWall"), (127, 3, "HouseEtc"), (130, 3, "Light"), (133, 1, "PoseBox"),
    (140, 7, "Furniture"), (150, 1, "Candle"), (151, 3, "Beer"),
]

#: Types the client places and then never draws, for Lorencia.
#:
#: All are marked HiddenMesh = -2, which its render path checks before drawing anything, so
#: in game they do not appear. Two kinds. The pose box at 133 is a click target that lets a
#: player sit down — 53 of them. Light01 to Light03 at 130 to 132 are anchors for fire:
#: MoveObject calls CreateFire on each and hides the model in the same breath, so what a
#: player sees at those 25 placements is flame and never the thing carrying it.
#:
#: They are carried through with a flag rather than dropped, because "78 placements exist here
#: and are deliberately invisible" is a fact about the map worth keeping — and because the 25
#: fire anchors are the map telling us where its braziers are, which is wanted later even
#: though the model is not.
HIDDEN_TYPES = {130, 131, 132, 133}

#: The same, per world, by the server's map number. Lorencia's is HIDDEN_TYPES above.
#:
#: Read off each world's arm of CreateObject in ZzzObject.cpp, which is where every
#: `HiddenMesh = -2` in the client is written. Noria has exactly one: type 38, which
#: CreateOperate also takes — a hang box, 53 of them, invisible and clickable, and the same
#: shape of thing as Lorencia's 133. Building it would put fifty-three of something into the
#: town that the game does not have, and it would look exactly like the next job on the
#: worklist, which is how Lorencia's PoseBox01 nearly got built.
HIDDEN_BY_MAP = {
    0: HIDDEN_TYPES,
    3: {38},
}

#: What each world draws additively, by map number and placement type.
#:
#: MU's BlendMesh: the client sets `o->BlendMesh = n` on a placement and its render path then
#: draws submesh n with RENDER_BRIGHT, which is one-plus-one blending — a glow rather than a
#: surface. It is per world and per type and it is written nowhere but that switch, which is
#: why it is transcribed rather than sniffed: a sheet cannot be looked at to find out whether
#: the client adds it or multiplies it, and the ones that are wrong look merely dull.
#:
#: Noria's six are the elf town's lights and its water. Carried onto the placement so that the
#: asset for a model can say which of its sheets is additive; see index.py and the `additive`
#: key in an object's own json.
BLEND_MESH_BY_MAP = {
    3: {1: 1, 9: 3, 17: 0, 18: 2, 19: 0, 37: 0},
}

#: Which types a character can pose against, per world, for the same reason OPERABLE_TYPES
#: exists: their tiles must never be stamped solid behind the client's back.
#:
#: Noria's two are MOVEMENT_OPERATE's own — 8 sits, 38 hangs — and 38 is also the hidden one.
OPERABLE_BY_MAP = {
    0: {6, 133, 145, 146},
    3: {8, 38},
}

#: Types a character walks onto and poses against, for Lorencia.
#:
#: The pose box at 133, the fallen log at 6, and the tavern's two benches at 145 and 146 —
#: MOVEMENT_OPERATE's own switch, transcribed in full in client/core/Poses.cs. Here for one
#: reason only: **their tiles must never be stamped NoMove.**
#:
#: The client gates the click on the placement's own tile — `wall == TW_HEIGHT || wall <
#: TW_CHARACTER` — and then walks the character onto it. MU's grid leaves those tiles open on
#: purpose, and the solid-stamping pass below would close 24 of the 110 behind its back: the
#: benches at 146 are in the solids list and would block their own seat, and nineteen of the
#: lean boxes stand close enough to a wall to be taken by the wall's footprint. The symptom is
#: the worst kind — a click that walks you over and then does nothing, on some walls and not
#: others.
#:
#: The stamping exists to fix places MU *under*-marked, so it must not overrule MU where MU
#: deliberately left a tile open. See index.py's stamp_blocked.
OPERABLE_TYPES = {6, 133, 145, 146}

#: The tile textures, by the slot the map file stores. Slots 14-29 are the extension set.
TILE_SLOTS = [
    "TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03",
    "TileWater01", "TileWood01", "TileRock01", "TileRock02", "TileRock03",
    "TileRock04", "TileRock05", "TileRock06", "TileRock07",
]

#: What the overlay layer stores to mean "nothing here".
NO_TEXTURE = 255

#: A second XOR layer the attribute file carries on top of the map cipher.
BUX_CODE = bytes([0xFC, 0xCF, 0xAB])

#: What a tile's attribute bits mean, from the client's TerrainFlags.
#:
#: Only the first five are read here and the rest are carried. NoMove and NoGround together
#: are the whole of "he cannot stand there", which is what a walker needs; SafeZone is what
#: puts the weapons on his back, and Water is why some of the map is passable and wet rather
#: than solid.
SAFE_ZONE = 0x0001
NO_MOVE = 0x0004
NO_GROUND = 0x0008
WATER = 0x0010


def decrypt(data: bytes) -> bytes:
    """The map cipher, undone."""
    out = bytearray(len(data))
    key = MAP_KEY_SEED

    for index, cipher in enumerate(data):
        out[index] = (cipher ^ MAP_XOR_KEY[index % 16]) - key & 0xFF
        key = (cipher + MAP_KEY_INCREMENT) & 0xFF

    return bytes(out)


def model_for(kind: int, number: int = 1) -> str | None:
    """The .bmd this object type stands for, or nothing when the slot is unused.

    Every world but Lorencia keeps its objects in one straight run: the type is the model
    index, and the client loads index *i* from `Object{i + 1}.bmd`, padded to two digits
    below ten (`MapManager.cpp:1121`, `LoadData.cpp:28`). Lorencia is the exception — it runs
    in named blocks with gaps, and the names it gets here are the ones the rest of MU2 knows
    it by. Naming another world's objects out of Lorencia's table is how `Tree06` came to
    stand in the character scene, where nothing of the kind is placed.
    """
    if number != 1:
        return f"Object{kind + 1:02d}"

    for start, count, prefix in LORENCIA_RANGES:
        if start <= kind < start + count:
            return f"{prefix}{kind - start + 1:02d}"

    return None


def attributes(world: Path, number: int) -> np.ndarray:
    """What each tile permits, as the client's sixteen flag bits.

    Two ciphers rather than one: the map cipher, then a three-byte XOR the attribute file
    alone carries. And two layouts, one byte a tile or two, with nothing in the header saying
    which — it is inferred from the file's length, which is what the client does.
    """
    data = bytearray(decrypt((world / f"EncTerrain{number}.att").read_bytes()))

    for index in range(len(data)):
        data[index] ^= BUX_CODE[index % 3]

    body = bytes(data[4:])
    tiles = SIZE * SIZE

    if len(body) >= tiles * 2:
        return np.frombuffer(body[:tiles * 2], dtype="<u2").reshape(SIZE, SIZE)

    return np.frombuffer(body[:tiles], dtype=np.uint8).astype(np.uint16).reshape(SIZE, SIZE)


def baked_light(world: Path) -> np.ndarray | None:
    """MU's own light for the ground, one colour a tile.

    The client does not light its terrain. MuSunlight is explicit that its one directional
    light contributes nothing and exists so that a shadow map has somewhere to land — every
    bit of brightness in a MU world comes from this file, painted per tile by whoever built
    the map, with the sun's direction and the buildings' shade already in it.

    Carried as it is rather than converted. What to do with it is a decision about what kind
    of remaster this is: multiplied into a properly lit ground it gives the town MU's own
    light and shade, and used as the only light it would throw away every material in the
    project. It is written out so that either is possible.
    """
    source = world / "TerrainLight.OZJ"

    if not source.exists():
        return None

    # OZJ is MU's JPEG with a 24-byte prefix, which decode_texture already knows about.
    import subprocess
    import tempfile

    with tempfile.TemporaryDirectory() as scratch:
        target = Path(scratch) / "light.png"
        subprocess.run(
            [sys.executable, str(Path(__file__).resolve().parent / "decode_texture.py"),
             str(source), str(target)],
            check=True, capture_output=True)

        return np.asarray(Image.open(target).convert("RGB"), dtype=np.uint8)


def heights(world: Path) -> np.ndarray:
    """The ground's shape, as bytes. Multiply by HEIGHT_FACTOR for MU units."""
    plain = world / "TerrainHeight.bmp"
    packed = world / "TerrainHeight.OZB"

    # The .OZB is not encrypted and not a format: it is the same bitmap behind four bytes of
    # prefix, and stripping them gives a file identical to the .bmp Lorencia happens to ship
    # beside it. Most maps ship only the .OZB, so that is the one to read from.
    if plain.exists():
        raw = plain.read_bytes()
    elif packed.exists():
        raw = packed.read_bytes()[OZB_PREFIX:]
        plain = packed
    else:
        raise SystemExit(f"error: no TerrainHeight.bmp or .OZB in {world}")

    declared = struct.unpack_from("<I", raw, 10)[0]

    if declared != EIGHT_BIT_PIXELS:
        print(f"  height     header says pixels at {declared}; "
              f"reading {EIGHT_BIT_PIXELS} as the client does")

    grid = np.frombuffer(
        raw[EIGHT_BIT_PIXELS:EIGHT_BIT_PIXELS + SIZE * SIZE], dtype=np.uint8)

    if grid.size != SIZE * SIZE:
        raise SystemExit(f"error: {plain.name} holds {grid.size} height bytes, not {SIZE**2}")

    return grid.reshape(SIZE, SIZE)


def mapping(world: Path, number: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Which two tile textures each tile wears, and how much of the second."""
    source = world / f"EncTerrain{number}.map"
    data = decrypt(source.read_bytes())

    # One version byte, one map number, then three whole planes back to back.
    at = 2
    planes = []

    for _ in range(3):
        planes.append(
            np.frombuffer(data[at:at + SIZE * SIZE], dtype=np.uint8).reshape(SIZE, SIZE))
        at += SIZE * SIZE

    return tuple(planes)


def objects(world: Path, number: int) -> list[dict]:
    """Everything standing on the map, with where and how it stands."""
    data = decrypt((world / f"EncTerrain{number}.obj").read_bytes())

    count = struct.unpack_from("<h", data, 2)[0]
    stride = 2 + 12 + 12 + 4
    placed = []

    for index in range(count):
        at = 4 + index * stride
        kind = struct.unpack_from("<h", data, at)[0]
        position = struct.unpack_from("<3f", data, at + 2)
        angle = struct.unpack_from("<3f", data, at + 14)
        scale = struct.unpack_from("<f", data, at + 26)[0]

        entry = {
            "type": kind,
            "model": model_for(kind, number),

            # Left in MU's own units and MU's own axes, deliberately.
            #
            # This file's job is to say what the client's data holds, and converting here
            # would make it say what one engine wants instead — after which nothing could
            # check it against the original. A tile is 100 units and MU is Z-up; the viewer
            # does that arithmetic where it does the same arithmetic for everything else.
            "at": [round(v, 3) for v in position],
            "angle": [round(v, 3) for v in angle],
            "scale": round(scale, 4),
        }

        # Per map, because a type number means a different thing in every world: 38 is
        # Noria's hang box and Lorencia's thirty-ninth object, and there is nothing in the
        # file that says which. The tables are each world's own arm of CreateObject.
        if kind in HIDDEN_BY_MAP.get(number - 1, set()):
            entry["hidden"] = True

        # And which of its submeshes the client draws additively, where it draws one that
        # way. Written onto the placement because that is where the type is; an asset for
        # the model reads it back through index.py and declares the sheet additive.
        if (blended := BLEND_MESH_BY_MAP.get(number - 1, {})).get(kind) is not None:
            entry["blend_mesh"] = blended[kind]

        placed.append(entry)

    return placed


def main() -> None:
    if len(sys.argv) < 4:
        print("usage: python3 terrain.py <world dir> <name> <assets dir> [map number]",
              file=sys.stderr)
        raise SystemExit(2)

    world, name, assets = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    number = int(sys.argv[4]) if len(sys.argv) > 4 else 1

    out = assets / name
    out.mkdir(parents=True, exist_ok=True)

    print(f"\n=== {world.name} -> {out} ===")

    grid = heights(world)
    factor = 3.0 if number in STEEP_MAPS else HEIGHT_FACTOR
    metres = grid.astype(np.float32) * factor / SCALE
    Image.fromarray(grid, "L").save(out / "height.png")
    print(f"  height     {SIZE}x{SIZE} tiles, {metres.min():.2f} to {metres.max():.2f} m "
          f"over {SIZE * SCALE / 100:.0f} m square  -> height.png")

    layer1, layer2, alpha = mapping(world, number)

    # One image, three planes, which is what they are. Base in red, overlay in green, the
    # blend between them in blue — the same packing glTF uses for occlusion-roughness-
    # metallic, and for the same reason: three single-channel maps of one thing.
    Image.fromarray(np.stack([layer1, layer2, alpha], axis=-1), "RGB").save(out / "tiles.png")

    used = sorted(set(layer1.flatten()) | (set(layer2.flatten()) - {NO_TEXTURE}))
    named = [(slot, TILE_SLOTS[slot] if slot < len(TILE_SLOTS) else f"ExtTile{slot - 13:02d}")
             for slot in used]

    print(f"  tiles      {len(named)} of the slot table in use  -> tiles.png")
    for slot, tile in named:
        share = float((layer1 == slot).mean()) * 100
        print(f"               {slot:3d}  {tile:14s} {share:5.1f}% of the ground")

    flags = attributes(world, number)
    blocked = (flags & (NO_MOVE | NO_GROUND)) != 0
    safe = (flags & SAFE_ZONE) != 0

    # Low byte in red and high byte in green, so every flag survives rather than only the
    # two a walker happens to need today. Blue carries the answer to the one question that
    # gets asked every frame, already worked out.
    Image.fromarray(np.stack([
        (flags & 0xFF).astype(np.uint8),
        (flags >> 8).astype(np.uint8),
        np.where(blocked, 0, 255).astype(np.uint8),
    ], axis=-1), "RGB").save(out / "attributes.png")

    print(f"  walkable   {(~blocked).mean() * 100:.1f}% of the map, "
          f"{safe.mean() * 100:.1f}% is a safe zone  -> attributes.png")

    lit = baked_light(world)

    if lit is not None:
        Image.fromarray(lit, "RGB").save(out / "light.png")
        print(f"  light      {lit.shape[1]}x{lit.shape[0]}, mean "
              f"{lit.reshape(-1, 3).mean(axis=0).round().astype(int).tolist()}  -> light.png")

    placed = objects(world, number)
    kinds: dict[str, int] = {}
    for one in placed:
        kinds[one["model"] or f"type {one['type']}"] = kinds.get(
            one["model"] or f"type {one['type']}", 0) + 1

    (out / f"{name}.json").write_text(json.dumps({
        "world": name,
        "map_number": number,
        "size": SIZE,
        "units_per_tile": SCALE,
        "height_factor": factor,
        "height": "height.png",
        "tiles": "tiles.png",
        "attributes": "attributes.png",
        "light": "light.png" if lit is not None else "",
        "tile_slots": {str(slot): tile for slot, tile in named},
        "objects": placed,
    }, indent=1) + "\n")

    print(f"  objects    {len(placed)} placed, {len(kinds)} kinds  -> {name}.json")
    for model, howmany in sorted(kinds.items(), key=lambda pair: -pair[1])[:12]:
        print(f"               {howmany:5d}  {model}")

    missing = sorted({one["model"] for one in placed if one["model"] is None
                      or not (world.parent / f"Object{number}" / f"{one['model']}.bmd").exists()}
                     - {None})
    if missing:
        print(f"  missing    {len(missing)} named model(s) not on disk: {', '.join(missing)}")


main()
