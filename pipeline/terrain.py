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
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import world_manifest

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

#: Types the client places and then never draws, as Lorencia shows it.
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

#: Per world, by the server's map number: each world's manifest's `pipeline.hidden`
#: (source/world/<w>/world.json, read by world_manifest.py), with each row's citation.
#:
#: Read off each world's arm of CreateObject in ZzzObject.cpp, which is where every
#: `HiddenMesh = -2` in the client is written. Noria has exactly one: type 38, which
#: CreateOperate also takes — a hang box, 53 of them, invisible and clickable, and the same
#: shape of thing as Lorencia's 133. Building it would put fifty-three of something into the
#: town that the game does not have, and it would look exactly like the next job on the
#: worklist, which is how Lorencia's PoseBox01 nearly got built.
HIDDEN_BY_MAP = world_manifest.by_map("hidden", world_manifest.as_set)

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
BLEND_MESH_BY_MAP = world_manifest.by_map("blend_mesh", world_manifest.as_int_keys)

#: Where a world's grass grows, by map number, where its slots' recipes would say otherwise.
#:
#: MU grows a tuft on a slot when the world ships that slot's TileGrassNN.tga
#: (MapManager.cpp:1465, ZzzLodTerrain.cpp:2077). Devias ships TileGrass02.OZT alone, a strip
#: of frosted white blades, so it grows on the smoother snow and nowhere else -- and that snow
#: wears the snow recipe, not grass. Noria's is what grew there before the engine matched its
#: prefixed slots to their recipes; its TileGround01 takes grass by recipe and has MU's
#: TileGrass03 behind it, which is a look decision rather than an extraction.
GRASS_BY_MAP = world_manifest.by_map("grass")

#: Where each world's rivers are fed and where they drain, as tile (column, row), for the
#: engine's flow (content::buildFlow). Ours: MU slides every water tile the same way. Lorencia:
#: the moat is fed at its north-west corner and runs both ways round the town to the canal at
#: its south-east, which joins the east river; that runs from the north sea to the south-east
#: one; the west river runs down its bend from the map's west edge back to it.
#: How each world's NoGround chasm meets its ground (content::Ground, the world json's `void`).
#: The Lost Tower's causeways and floor edges stand over the void with no wall on them, and
#: their sides fall 1.7 m to the void's corners: `rim` takes a corner touching the void to the
#: walkway's level, and the abyss takes everything below it to black over 1.7 m, so the sides
#: melt into the dark as MU's do (the user's MU shot of floor 7's causeway, 2026-10-01).
#: The Dungeon's pits: its worms and tentacles stand 1.5 to 8.5 m under the floor with their
#: crowns near it, and the default 1.5 m start left most of them fully lit ('tenticles in
#: dungeon still is very good lightened when they are under the ground', 2026-10-02). From
#: the floor to black 2 m down, so the crowns at the lip keep their bronze and the bodies sink.
#: Then 'under the ground there is much less light which means that tenticles should be very
#: dark': `lift` puts the void's own points a metre above the rim, so a crown level with the
#: floor is already three quarters dark, and black half a metre down.
VOID_BY_MAP = world_manifest.by_map("void")
#: Tarkan was Devias's way (the user, 2026-10-05: 'in tarkan there is same tehniq
#: which was in devias which make the deep canions/voids'). Pale plateaus over a NoGround void
#: that is half the map, MU's cut at the edge, and Object41's 427 cliff faces hanging from the
#: rims into it as Devias's ice walls do, taken to black by the abyss's default depth. Blood
#: Castle's rim and blend sloped the lips and would hide the walls behind them.
#: Blood Castle tried the tower's (the user, 2026-10-02: 'we need to add some nice void gradients
#: to ground edges'), blend 2 and then 0.7 tiles: the three-tile bridge went dark from side to
#: side, then a hard dark band ran down its open side ('something dont look correct it was kind
#: of better'). Taken out; the castle keeps the default sink.

#: How far a world's lava floods its void, in tiles. Ours, marked: MU framed its maps for a
#: 4:3 screen, and the Lost Tower's long lava field (floor 1's east strip, 150-185 x 0-140)
#: ends in a hard line against the black where MU's camera never looked; at 16:9 and pulled
#: back, that line is on screen (the user, 2026-10-01: 'we need find a way how we can expand
#: lava zones, because this game was made 25 years ago for 4:3 monitors'). Every void tile
#: within this many tiles of lava, walking through the void alone, becomes the nearest lava
#: tile again -- its sheet, height, painted light and flags, so it is as unwalkable as the lava
#: it extends. It fills the gaps right up to the next floor's edge: stopping halfway left the
#: black on screen (the user: 'laav suppost to fill whole left corner screen'), and at 32 tiles
#: it ended in a straight line across the wide void at 225,138 ('also on this coords i seee
#: lava is ended'). So the whole map: every void tile the lava reaches through the void, 15166
#: of them. What stays void is what no lava touches -- floor 7's causeways and the chasm round
#: them, whose blending into the black the user tuned.
LAVA_SPILL_BY_MAP = world_manifest.by_map("lava_spill")

#: The worlds with no ground drawn (the world json's `sky`; content::Ground::draws). Icarus:
#: MuMain skips RenderTerrain on map 10 (MainScene.cpp:463), and the hero walks an unseen plane
#: at 2.85 m over the clear's navy, the cloud emitters' puffs round him. Every tile keeps its
#: height and flags -- NoGround would close the road, as the walker counts 0x08 -- so the
#: switch is the world's, not the tiles'. docs/icarus-port.md.
SKY_BY_MAP = world_manifest.maps_with("sky")

#: Boxes of a map's grid opened at import, as (x1, y1, x2, y2, bits cleared), inclusive tiles.
#: Ours, marked, and only until the run is built: Blood Castle's entrance, its drawbridge's gap
#: and its door with the courtyard behind it are closed in MU's grid and opened by the event --
#: the entrance at the start, the gap's NoGround at the first quota, the door's NoMove when the
#: Castle Gate dies (WebZen BloodCastle.h:128-173, BloodCastle.cpp:2506-2625; docs/
#: blood-castle-port.md, Part A section 4.3). With no event yet the hero was shut in the safe
#: court (the user, 2026-10-02: 'i cant pass the gates to bridge'). Step 5 takes this out and
#: opens them at run time; the gap's TileRock02 planks then show as ground, MU's lowered bridge.
#: The gap and the door were opened too until the user asked for MU's closed gates back
#: (2026-10-02: 'i remember that gates was closed'): only the entrance stays open, so the bridge
#: can be walked to the raised drawbridge.
OPEN_BY_MAP = world_manifest.by_map("open", world_manifest.as_tuples)

#: Boxes of a map's grid made floor that is not walked, as (x1, y1, x2, y2), inclusive tiles:
#: NoGround cleared and NoMove set, so the ground draws them and VOID_FILL does not take them
#: back. Blood Castle's courtyard hole (the user, 2026-10-03: 'some holes'): three tiles MU's
#: grid marks NoGround inside the courtyard, 9,84 10,84 10,85, which MU draws as floor -- its
#: void is a painted black, not the grid -- and this ground cut out as a pit. Ours, marked.
FLOOR_BY_MAP = world_manifest.by_map("floor", world_manifest.as_tuples)

#: Where a map's own walk starts, for VOID_FILL: every tile MU leaves walkable that no walk from
#: here reaches becomes NoGround, drawn as the void. Ours, marked. Blood Castle's grid is a strip
#: at x 0-35 in a flat filler plain of 32 000 open tiles at 1.65 m, east and south of it, that no
#: path reaches; MU's 4:3 camera never showed it, and at 16:9 pulled back it stood round the
#: castle as a brick field where the user's MuMain shots have black (docs/blood-castle-port.md,
#: Part B decision 1). From the safe court, the statue court and the courtyard's east pocket, which the
#: run opens and the closed grid does not, so the castle itself stays ground.
VOID_FILL_BY_MAP = world_manifest.by_map("void_fill", world_manifest.as_tuples)

#: Which void a world's ground is spread over at the map's edge, by map number. Ours, marked:
#: MU framed its maps for a 4:3 screen, and Aida's outer void -- the frame round the map and
#: the channel between the west ruins and the middle, a third of its NoGround -- is bare black
#: with nothing standing at its lips, which at 16:9 and pulled back reads as holes (the user,
#: 2026-10-11: 'there still was some black holes', then 'Fill outer border'). A walk from the
#: map's edge through the void, four ways, that never comes within `clear` tiles of a `walls`
#: placement standing in a `keep` box: so the walled ravine, which MU means as a chasm, stays
#: void, and the walls along the map's own edge get ground behind them. The enclosed pits are
#: never reached. Every tile it reaches copies its nearest ground tile -- height, sheets,
#: painted light -- and is NoMove. docs/aida-port.md.
BORDER_FILL_BY_MAP = world_manifest.by_map("border_fill", world_manifest.as_walls)

#: Where a world's void.blend leaves the ground lit, by map number: every tile whose middle is
#: within `reach` tiles of a `walls` placement -- round, and close: four tiles square left hard
#: black notches where a bare lip stood near a wall. Ours, marked. Aida's walls stand on the ravine's lips, and the fade
#: took the ground behind them dark too, a black strip between a wall's top and the pond (the
#: user, 2026-10-11: 'this is not perfect'). Written as blend_keep.png beside the world json,
#: which content::Ground reads as a mask beside its blend_keep boxes.
BLEND_KEEP_NEAR_BY_MAP = world_manifest.by_map("blend_keep_near", world_manifest.as_walls)

#: The slot MU's lava (TileWater01) sits in.
LAVA_SLOT = 5


def spill_lava(reach: int, void: np.ndarray, layer1: np.ndarray, layer2: np.ndarray,
               alpha: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Which void tiles the lava floods, and the lava tile each copies, as flat indices.

    A breadth-first walk out from every lava tile, eight ways, through void tiles alone, so a
    floor's ground stops it and a gap between floors fills to the far edge. On the [y, x]
    grid every plane here shares.
    """
    from collections import deque

    lava = ((layer1 == LAVA_SLOT) | ((layer2 == LAVA_SLOT) & (alpha > 127))) & ~void
    size_y, size_x = void.shape
    distance = np.full(void.shape, np.iinfo(np.int32).max, dtype=np.int64)
    source = np.full(void.shape, -1, dtype=np.int64)
    queue = deque()
    for y, x in zip(*np.nonzero(lava)):
        distance[y, x] = 0
        source[y, x] = y * size_x + x
        queue.append((y, x))
    while queue:
        y, x = queue.popleft()
        step = distance[y, x] + 1
        if step > reach:
            continue
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny, nx = y + dy, x + dx
                if 0 <= ny < size_y and 0 <= nx < size_x and void[ny, nx] \
                        and distance[ny, nx] > step:
                    distance[ny, nx] = step
                    source[ny, nx] = source[y, x]
                    queue.append((ny, nx))
    spilled = np.flatnonzero(void & (source >= 0))
    return spilled, source.reshape(-1)[spilled]


def fill_border(rule: dict, void: np.ndarray,
                placed: list[dict]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Which void tiles BORDER_FILL_BY_MAP spreads ground over, the ground tile each copies, as
    flat indices on the [y, x] grid, and how many steps each lies from that tile.
    """
    from collections import deque

    size_y, size_x = void.shape
    held = np.zeros(void.shape, dtype=bool)
    clear = rule["clear"]
    for one in placed:
        if one["type"] not in rule["walls"]:
            continue
        x, y = int(one["at"][0] / 100), int(one["at"][1] / 100)
        if any(x1 <= x <= x2 and y1 <= y <= y2 for x1, y1, x2, y2 in rule["keep"]):
            held[max(0, y - clear):y + clear + 1, max(0, x - clear):x + clear + 1] = True
    open_ = void & ~held
    filled = np.zeros(void.shape, dtype=bool)
    queue = deque()
    for y in range(size_y):
        for x in range(size_x):
            if (y in (0, size_y - 1) or x in (0, size_x - 1)) and open_[y, x]:
                filled[y, x] = True
                queue.append((y, x))
    while queue:
        y, x = queue.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < size_y and 0 <= nx < size_x and open_[ny, nx] and not filled[ny, nx]:
                filled[ny, nx] = True
                queue.append((ny, nx))
    # The nearest ground tile to each, walking out from the ground through what is filled.
    source = np.full(void.shape, -1, dtype=np.int64)
    steps = np.zeros(void.shape, dtype=np.int64)
    queue = deque()
    for y, x in zip(*np.nonzero(~void)):
        source[y, x] = y * size_x + x
        queue.append((y, x))
    while queue:
        y, x = queue.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < size_y and 0 <= nx < size_x and filled[ny, nx] and source[ny, nx] < 0:
                source[ny, nx] = source[y, x]
                steps[ny, nx] = steps[y, x] + 1
                queue.append((ny, nx))
    spread = np.flatnonzero(filled & (source >= 0))
    return spread, source.reshape(-1)[spread], steps.reshape(-1)[spread]


#: How much of MU's baked light's variation the ground keeps, where a world differs from
#: ground.py's LIGHT_DEPTH (0.5, Lorencia's: a baked shadow halved so it does not read as a
#: smudge under the live sun). The Lost Tower keeps all of it: its floors and causeways fade
#: into the void only because MU paints their edge corners 0, and halved they stood grey to
#: a hard edge (the user: 'you did not migrated void blending with long road'). Then 0.6, the
#: user: 'we ened more actual shadows and little bit less baked in' -- the floors' edges now
#: reach the void's black by the void's own blend (VOID_BY_MAP), and most void is lava.
#: Blood Castle keeps all of it: its court is painted bright (about 144-175 against the map's
#: mean of 87) and, halved, sank into the dark earth sheet under it, darker than the grey gravel
#: of the user's MuMain shots (2026-10-02).
LIGHT_DEPTH_BY_MAP = world_manifest.by_map("light_depth")

#: How much of MU's painted light's tint the ground and the objects keep (ground.py
#: light_chroma), where a world differs from all of it. Blood Castle's is cold: the court about
#: (204, 203, 225), the courtyard (83, 83, 112) and the statue hall a violet (77, 66, 86), and the
#: floor under it came out a saturated blue, where the Dungeon's warm lamps carry its own cool
#: paint (the user, 2026-10-03: 'we need that nice efefct which dungeon,lorencia,lost tower
#: has'). Ours.
LIGHT_CHROMA_BY_MAP = world_manifest.by_map("light_chroma")

#: Where a world's figures take MU's painted light where they stand (content::Ground::
#: figureLightAt): the ground's curve raised to this, so a dim tile dims them less than the floor.
#: Blood Castle's exposure is twice Lorencia's to lift its dark paint, and unlit by it every
#: monster stood twice as bright as the stone (the user, 2026-10-03: 'something wrong probably
#: with lightiing or materials, its not well balanced'). MuMain lights a character by its tile's
#: terrain light (BodyLight). Ours, the softening.
FIGURE_LIGHT_BY_MAP = world_manifest.by_map("figure_light")

WATER_FLOW_BY_MAP = world_manifest.by_map("water_flow")

#: Which types a character can pose against, per world, for the same reason OPERABLE_TYPES
#: exists: their tiles must never be stamped solid behind the client's back.
#:
#: Noria's two are MOVEMENT_OPERATE's own — 8 sits, 38 hangs — and 38 is also the hidden one.
#: Devias's seven (ZzzObject.cpp:4682-4690): 91 is a lean box and hidden too; 22, 25, 40 and 55
#: sit and turn to the seat, 45 and 73 sit.
OPERABLE_BY_MAP = world_manifest.by_map("perches", world_manifest.as_operable)

#: Why OPERABLE_BY_MAP exists, as Lorencia shows it: the types a character walks onto and poses
#: against.
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

        # The client decodes this JPEG bottom-up (OpenJpegBuffer, TJFLAG_BOTTOMUP,
        # ZzzTexture.cpp:200), so its row 0 is the picture's last row -- the same order the
        # height bitmap's rows are read in. PIL gives the picture top-down; flipped, it lands
        # on the [y, x] grid the height and the tiles use. Lorencia's and Noria's light.png
        # were extracted before this and are still mirrored in y.
        return np.flipud(np.asarray(Image.open(target).convert("RGB"), dtype=np.uint8)).copy()


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

        # Devias's file places two of its sliding doors (type 86, near x 4450) at a y that
        # is not a number. The client copies it as the door's rest point (ZzzObject.cpp:4718)
        # and never draws them; json cannot carry it, and the engine refuses the whole file.
        if not all(math.isfinite(v) for v in (*position, *angle, scale)):
            print(f"  objects    dropped a type {kind} placed at "
                  f"{[round(v, 1) for v in position]}: not a number in MU's file")
            continue

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
    for x1, y1, x2, y2, bits in OPEN_BY_MAP.get(number - 1, ()):
        flags = np.array(flags)
        flags[y1:y2 + 1, x1:x2 + 1] &= flags.dtype.type(~bits & 0xFFFF)
        print(f"  opened     {x1},{y1} to {x2},{y2}: bits 0x{bits:02x} cleared (OPEN_BY_MAP)")
    for x1, y1, x2, y2 in FLOOR_BY_MAP.get(number - 1, ()):
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                flags[y][x] = (int(flags[y][x]) & ~NO_GROUND) | NO_MOVE
        print(f"  floored    {x1},{y1} to {x2},{y2}: NoGround cleared, NoMove set (FLOOR_BY_MAP)")
    if (seeds := VOID_FILL_BY_MAP.get(number - 1)):
        from collections import deque
        flags = np.array(flags)
        walk = (flags & (NO_MOVE | NO_GROUND)) == 0
        reached = np.zeros(walk.shape, dtype=bool)
        queue = deque()
        for seed in seeds:
            reached[seed[1], seed[0]] = True
            queue.append((seed[1], seed[0]))
        while queue:
            y, x = queue.popleft()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < SIZE and 0 <= nx < SIZE and walk[ny, nx] and not reached[ny, nx]:
                        reached[ny, nx] = True
                        queue.append((ny, nx))
        filler = walk & ~reached
        flags[filler] |= NO_GROUND
        print(f"  void       {int(filler.sum())} open tiles no walk from {seeds} reaches -> NoGround "
              f"(VOID_FILL_BY_MAP)")

    lit = baked_light(world)

    # The ground spread over the outer void, before anything is written (BORDER_FILL_BY_MAP).
    if (rule := BORDER_FILL_BY_MAP.get(number - 1)):
        flags = np.array(flags)
        spread, copied, steps = fill_border(rule, (flags & NO_GROUND) != 0, objects(world, number))
        grid, layer1, layer2, alpha = (np.array(plane) for plane in (grid, layer1, layer2, alpha))
        lit = np.array(lit) if lit is not None else None
        for plane in (grid, layer1, layer2, alpha, flags):
            flat = plane.reshape(-1)
            flat[spread] = flat[copied]
        flags.reshape(-1)[spread] = NO_MOVE
        if lit is not None:
            # Copied whole, MU's dark-painted lip ran out across the fill in straight bands (the
            # nearest tile's light, Voronoi-style; the south-east heap, 2026-10-11). So it starts
            # at its nearest tile's light and reaches the world's walkable mean `soften` steps out.
            flat = lit.reshape(-1, lit.shape[-1])
            walkable = (flags & (NO_MOVE | NO_GROUND)) == 0
            mean = lit[walkable].reshape(-1, lit.shape[-1]).mean(axis=0)
            t = np.clip(steps / rule.get("soften", 6), 0.0, 1.0)[:, None]
            flat[spread] = (flat[copied] * (1.0 - t) + mean * t).round().astype(flat.dtype)
        Image.fromarray(grid, "L").save(out / "height.png")
        Image.fromarray(np.stack([layer1, layer2, alpha], axis=-1), "RGB").save(out / "tiles.png")
        print(f"  border     ground spread over {len(spread)} void tiles from the map's edge "
              f"(BORDER_FILL_BY_MAP)")

    # The lava's spill into the void, before anything is written (see LAVA_SPILL_BY_MAP).
    if (reach := LAVA_SPILL_BY_MAP.get(number - 1)):
        spilled, copied = spill_lava(reach, (flags & NO_GROUND) != 0, layer1, layer2, alpha)
        grid, layer1, layer2, alpha, flags = (np.array(plane) for plane in
                                              (grid, layer1, layer2, alpha, flags))
        lit = np.array(lit) if lit is not None else None
        for plane in (grid, layer1, layer2, alpha, flags):
            flat = plane.reshape(-1)
            flat[spilled] = flat[copied]
        if lit is not None:
            flat = lit.reshape(-1, lit.shape[-1])
            flat[spilled] = flat[copied]
        Image.fromarray(grid, "L").save(out / "height.png")
        Image.fromarray(np.stack([layer1, layer2, alpha], axis=-1), "RGB").save(out / "tiles.png")
        print(f"  lava       spilled into {len(spilled)} void tiles, up to {reach} out")

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

    if lit is not None:
        Image.fromarray(lit, "RGB").save(out / "light.png")
        print(f"  light      {lit.shape[1]}x{lit.shape[0]}, mean "
              f"{lit.reshape(-1, 3).mean(axis=0).round().astype(int).tolist()}  -> light.png")

    placed = objects(world, number)
    chasm = dict(VOID_BY_MAP[number - 1]) if number - 1 in VOID_BY_MAP else None
    if chasm is not None and (rule := BLEND_KEEP_NEAR_BY_MAP.get(number - 1)):
        kept = np.zeros((SIZE, SIZE), dtype=np.uint8)
        reach = rule["reach"]
        rows, columns = np.mgrid[0:SIZE, 0:SIZE] + 0.5
        for one in placed:
            if one["type"] in rule["walls"]:
                x, y = one["at"][0] / 100, one["at"][1] / 100
                kept[(columns - x) ** 2 + (rows - y) ** 2 <= reach * reach] = 255
        Image.fromarray(kept, "L").save(out / "blend_keep.png")
        chasm["blend_keep_mask"] = "blend_keep.png"
        print(f"  blend      kept lit on {int((kept > 0).sum())} tiles near the walls "
              f"-> blend_keep.png (BLEND_KEEP_NEAR_BY_MAP)")
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
        **({"grass_slots": GRASS_BY_MAP[number - 1]} if number - 1 in GRASS_BY_MAP else {}),
        **({"void": chasm} if chasm is not None else {}),
        **({"light_depth": LIGHT_DEPTH_BY_MAP[number - 1]} if number - 1 in LIGHT_DEPTH_BY_MAP else {}),
        **({"light_chroma": LIGHT_CHROMA_BY_MAP[number - 1]} if number - 1 in LIGHT_CHROMA_BY_MAP else {}),
        **({"figure_light": FIGURE_LIGHT_BY_MAP[number - 1]} if number - 1 in FIGURE_LIGHT_BY_MAP else {}),
        **({"water_flow": WATER_FLOW_BY_MAP[number - 1]}
           if number - 1 in WATER_FLOW_BY_MAP else {}),
        **({"sky": True} if number - 1 in SKY_BY_MAP else {}),
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
