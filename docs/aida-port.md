# Aida port: the record, the data, and how it plugs in

Research notes, 2026-10-11. Aida is MU server map **33**, client `Data/World34` + `Data/Object34`
(MuMain's `WD_33AIDA`), entered from Noria's north-east. Three research passes wrote this page in
`docs/tarkan-port.md`'s shape: Part A is the record, Part B is the data and the look, and Part C is
the engine side, ending in the build steps. Nothing but this page was edited, nothing was cooked or
written to mu.db, and no window was run. B and C ran `terrain.py` on World34 into scratch only.

The passes' scratch is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9b2c4cc5-bd84-45e7-b350-052f3cd63116/scratchpad/aida/`
(it is /private/tmp, so it will not last):
- `a/`: grid decodes, spawn tables, the client's `gate.bmd` and `Movereq.bmd` decoded (`gates_mmd.txt`, `movereq_mmd.txt`), `monskill_out.txt`, `drops_out.txt`, `map_out.txt`;
- `b/`: the terrain.py run (`aida/`), the model exports (`obj/`), the decoded sheets (`tex/`), and the contact sheets `sheet_objects.png`, `sheet_object34_textures.png`, `sheet_object34_ozt.png`, `sheet_world34.png`, `sheet_world34_ozt.png`, `tiles_big.png`, `prev_l1_attr_height_light_painted.png`, `prev_objects_on_attr.png`, `light_big.png`;
- `c/`: `grid.py`/`grid.txt`, `mm_aida.txt` (all 154 MuMain Aida lines), and its own terrain.py dry run.

**Paths that moved since Tarkan:** the real database is `MU2_BGFX/source/mu.db` (`MU2/mu.db` is a
0-byte file); the WZ clone at `9142efc4-.../scratchpad/wz/` is empty now, and pass A used the full
copy under `d7be4edb-...`. WebZen's 0.99 server data (WZO) is gone from scratch, and it predates Aida
anyway.

## Where it stands

**Step 1, the bare land, is built (2026-10-11, the user: 'lets start with ground').** It added:
- `source/world/aida`: World34 through `terrain.py` (0-3.27 m, 11 slots, 35.2% walkable after the
  fill, 0.8% safe, 3 752 placements of 74 kinds, none missing);
- `terrain.py` rows for map 33: `HIDDEN_BY_MAP` (the nine emitter kinds), `GRASS_BY_MAP
  ["TileGrass01"]`, `VOID_BY_MAP {"start": 0.5, "depth": 4.5}`, and `VOID_FILL_BY_MAP [(83, 10)]`,
  which voids the 177 stray tiles. `index.py`'s `GATE_BOXES_BY_MAP` gains Aida's four boxes and
  Noria's door and landing;
- eleven `ai_` sheets and a `ground.json`. TileRock05, the red edge carpet, wears
  `ai_tilerock05.png`, a copy of the hay sheet. It is a copy and not the same file because the
  ground's loader names each surface by its pair of images: with two slots on one file the world
  refused to open;
- the `kMaps` row `{"aida", 33, {84, 11}, true}` (underground air for now), the banner and minimap
  names, and `sheets/worlds/aida.json`, B §6's starting sheet, unjudged.

Cooked: `--only ground` and `--only tables` for Aida, and `--only tables` for Noria. Headless, map 33
is raised with the player at 84,11. Shot muted at noon at the hall (84,11), the Aida 2 clearing
(188,175), the south basin (116,145) and the ravine (150,60), at 220-315 fps. The only error is the
missing `aida.mut`, because no objects are cooked yet.

**The world walls, 2026-10-11** (the user, of the black cuts: 'what about those black spots?',
then 'Build the walls first'). Object18-20, 350 placements, built as recipes in
`source/world/aida` with the `ai_aidaworldwallc` face and the near-black
`ai_aidaworldwallcunder` foot, both `rock`, and cooked (textures, meshes, placements). `aida` is in
the cook's `FLAT_OVER_VOID`. Shot at the same four spots, with no errors. The ravine now has rock
lips whose faces fall into the black. **The two pits have no walls in MU:** their lips are hidden by
Object25's foliage cards (26 within 14 tiles of the basin shot), and Aida 2's edge by the rocks
Object10/21 and foliage. So the next batch is Object25 and Object07 (the foliage) and the rocks
Object09/10/21-23. Cost unmeasured: the ravine read 107-114 fps, GPU-bound at about 16 ms, on a
machine at load 9 with another session's game and netbot running.

**Rocks and foliage, then the holes MU leaves, 2026-10-11** (the user: 'still was some holes',
'Build rocks+foliage first', then 'there still was some black holes').
- **Batch 2**: Object25/07 (the shrub cards, 915, swaying, kept 3-4 m up where MU stores them),
  Object09/10 (qw rocks, 403) and Object43 (carved outcrops, 198). These are the kinds standing on
  the most void edge the walls leave bare. The basin's pit lip is outcrops now, and Aida 2's edge
  is boulders under foliage.
- **What MU leaves bare.** Of the 2 685 void edge tiles within 10 of walkable ground, 1 151 had
  nothing within 2.5 m once every built kind was counted. The unbuilt kinds would cover only about
  100 more: the rest is MU's own jagged cut, mostly the map's outer frame (MU's 4:3 camera never
  showed it) and a few inner pit lips. The user chose two fixes of ours:
  - **`BORDER_FILL_BY_MAP`** (`pipeline/terrain.py`, `fill_border`): a walk from the map's edge
    through the void, four ways, never within 4 tiles of a wall standing in the ravine's box
    (65,0-240,165). Every tile it reaches copies its nearest ground tile (height, sheets, painted
    light) and is NoMove: 15 752 tiles, the outer frame and the west channel. The walled ravine
    and the two enclosed pits stay void. Walls on the map's own edge get floor behind them.
    Walkable ground is unchanged at 35.2%. Its painted light starts at the nearest ground
    tile's and reaches the world's walkable mean 6 steps out (`soften`). Copied whole, MU's dark
    lip ran across the fill in straight-edged bands: a black rectangle of grass by the
    south-east heap.
  - **`blend: 1.5`** on Aida's void: the ground fades to black over a tile and a half at every
    lip. Only `fs_ground` reads it, so the walls and rocks keep their light. Blades stood lit
    over the fading strip as black spikes, so **`Grass` grows nothing within `ceil(blend)` tiles
    of NoGround** in any world with a blend. Only Aida, the Lost Tower and Blood Castle have one,
    and the latter two grow no grass.
- **The fade stays off behind the walls.** It had taken the ground behind them dark as well: a
  black strip between a wall's top and the pond (the user: 'this is not perfect').
  `BLEND_KEEP_NEAR_BY_MAP` writes `blend_keep.png`, every tile whose middle is within 2.5 tiles
  of a wall (4 655 tiles). The world json names it as `void.blend_keep_mask`, `index.py` copies
  it, and `content::Ground` reads it beside the `blend_keep` boxes. A first cut at four tiles
  square left hard black notches where a bare lip stood near a wall.
- After both: 1 614 void edge tiles near walkable ground, 423 of them with nothing on them, and
  those fade softly. Shot at the basin, the west channel and two pit lips, 202-236 fps.

**The ruins and graves, 2026-10-11** (the user, of a black slit by the basin: 'there will be
some objects there?', then 'I'd suggest the next batch be the ruins and graves'). Object34-41,
the temple pieces of the west and south (MU builds 35-41 tiny and places them at scale 6-16),
and the gravestones and Celtic crosses Object46-49: twelve kinds, 19 new `ai_` sheets plus
`branch01_line`'s hanging-vine cutout. Read off the art: rock (friezes, masonry, gravestones),
plaster (ida_11), bark (ida_12, qwwood, boss01), wood (qwwoodna). 2 107 placements of 20 models.
The basin's slit between two Object43 outcrops is still there: it is MU's own gap.
**A trap:** a gravestone at 90,174 looks as if it stands in the void. It stands on drawn ground,
but MU's baked light falls from about 90 to 1-30 across the graveyard's south edge toward the pit
(MU paints the forest's edges dark into the light), so the ground is black in MU's own client too.
A 0.8-tile fade was tried, changed nothing there, and went back to 1.5. Lifting the light floor
is a look decision for later.

**Every plain object kind, 2026-10-11** (the user: 'lets just continou to add objects and lets
see how map develops', then 'lets take smaller batches'). In batches of four or five, each shot
before the next:
- the trees (Object03/04/11/15);
- the stumps, root shrine and dead sapling (12-14/17, and 65 at MU's 0.05);
- the ground plants (05/06/24/53/54);
- the flower beds and vines (27/28/30, 16);
- the boulders, heap and stone tables (08/21-23/44);
- the statues and temple blocks (32/45/33/50/51);
- the coffins, sword and skeletons (52/55/56/62/69/70).

That made 3 263 placements of 54 models, at about 190-260 fps, unmeasured. The 20 kinds left
all need effect work, plus the nine hidden emitter kinds. Left after the two batches below:
- the hidden emitters 60 (butterflies), 67 (small fires) and 70 (the skull ghost); the mist boxes
  59/62/63 never emit in MU (no decision yet).

**The shot camera** looks along +y, its centre about 12 tiles ahead of the hero and a little to
the right, with the hero at the bottom left. To see an object, stand about 4 tiles -y of it.

**The glow sheets, 2026-10-11** (the user picked 'Glow sheets batch' first). Four kinds whose whole
body is one `_R` sheet, written by `mu new object` and marked `additive` + `lamplight`:
- Object01 (Treeange_R, 2): a faint grey haze in the north-east canopy, its 20-key bob kept;
- Object26 (waterfall_R, 40, its 15-key sway kept) and Object29 (the same sheet, 4, one key): the
  falls, `glow.scrolls_per_second` 0.375 (MU's V -0.015 a frame at 25 Hz, the water running down);
- Object42 (sunlight2_R, 21): the sunbeams, steady at glow 1.0. MU's two passes (Alpha 0.5, then
  mesh 0 again RENDER_BRIGHT at 0.52) are folded into one full added pass.

Shot muted at noon at the north-east pit (186,108), the south falls (117,207) and the graveyard's
beams (100,164), 0 errors, about 210-236 fps (unmeasured). The falls read bright cyan with a
white core under the world's glow_strength 1.3, a candidate to dim if the user finds them hot.

**The serpents, the ring statue and the water, 2026-10-11** (the user: 'next batch also work on
water if there is any').
- **Serpents**: Object66/67 (snake, green scales, 3 + 8, in the east) and Object78/79 (snake02,
  dark blue-grey scales, 10 + 6, their 20-key writhe, in the west and south-west), chitin. MU draws
  mesh 0 a second time as StreamMesh with its U slid (GMAida.cpp:227-238, 308-318); here the
  body's own sheet slides, lit (`glow` `stream`, `axis` u): +0.05 for 66/67 (a sheet in 20 s), -0.2
  for 78/79 (a sheet in 5 s the other way).
- **Object76**, the ring statue south of the hall (82,20): two rock sheets, its 10-key clip, and
  MU's BITMAP_FLARE at bone 4, Scale 3, turning 100 degrees a second, as an Ornaments arm
  (`src/game/world/ornaments.cpp`). MU's light is white, but Flare's sheet is gold with a
  white core, so it burns gold in the ring.
- **Waterfall spray**: the 84 hidden boxes are world anchors 18-20 (`tools/cook_anchors.py`),
  each carrying its box's Scale in the emitter's `high`, drawn by `game/world/waterfall_spray.h`
  on MU's numbers: Object57's cyan drops falling off the lip, Object58's white specks bursting up
  and dropping about 4 m, Object59's grey mist, from `Effect/waterFall1-3` (decoded to
  `source/effects/aida/`, showing names `waterfall_drops`, `waterfall_spray`, `waterfall_mist`).
  Only boxes within 28 m of the camera's point throw; at most 1 200 sprites.
- **The ponds** (2 301 TileWater01 tiles): held still, MU's rippled green-grey sheet read as
  wet rock. `sheets/worlds/aida.json` now slides them at half MU's pace (`water_flow` 0.5) with
  the Lost Tower's `water_variety` 1.0, whose turned copy crosses the first, so a pond shimmers
  rather than drifting like a river (the user's Noria rule), and MU's tile grid breaks up.
  Judged only in stills so far.
- Shot muted at the hall (79,9), the dark serpents (25,197), the green serpents (228,80), the
  south falls (117,207) and the north-east pit (186,108), 0 errors, about 200-230 fps
  (unmeasured).
- To pay the size ratchet: cook.py's world-anchor tables moved to `tools/cook_anchors.py`,
  index.py's `GATE_BOXES_BY_MAP` to `pipeline/gate_boxes.py`, and play_mode's void clouds,
  castle sparks and spray calls into `World::throwAround`. aida.mut, tarkan.mut, lorencia.mut
  and workshop/index.json were byte-identical across the moves.

**The light-flowers, 2026-10-11** (the user: 'continou'). Object31 (155, blue bulbs) and Object72
(28, salmon-red), their 13-key sway: aidalightflower stems as opaque foliage (MU ships it only as
a JPEG), the `flowerlight_R`/`flowerlight2_R` bulbs added lamplight. Each carries a recipe lamp,
MoveAidaObject's AddTerrainLight: green (0.4, 0.6, 0.4) at 1.0-1.1 or red (0.9, 0.2, 0.2) at
1.0-1.04, 2 tiles, a new level 12.5 times a second; 183 lights. And Ornaments' `kAidaGlows`:
at bones 6, 7, 8, 12, 13 and 17 a Spark03 glow (blue (0.1, 0.1, 0.3) at Scale 4-5.5, or red
0.25-0.5 at 5-6) and a dim core at 3, steady, 12 sprites a flower, drawn only where Sway poses
the flower. MU's few-unit offsets in each bone's frame are left out (at most 8 cm). Shot at the
red pair by the shrine (116,38) and the graveyard's blue ones (76,72), 0 errors.
Then (the user: 'flowers could look better', 'do it'), ours: each bulb was a white chip, MU's
glow and grey core added on Spark03's hard white disc over the bulb mesh under Aida's exposure.
Now the coloured glow is drawn on flare01, the lamps' soft sheet, 1.3 times wider and twice as
bright; the core keeps Spark03 in the flower's own hue at 0.6 of the glow; the bulb mesh glows
at 0.35 (0.2 for the pale salmon red); each bulb breathes 0.75-1 on its own beat over about six
seconds; and the green lamp is 1.4-1.5, as at MU's level the pool was lost on the dark moss. The
blue read as blue beads in soft orbs. The red bulbs stayed pale chips inside their glow (MU's
sheet is one flat salmon, (251, 188, 157)), so Object72 wears `ai_flowerlight2_r_deep.png`, ours: the
same flat sheet in a deep red (232, 64, 48), as saturated as the blue, at the blue's 0.35.

**Butterflies, scarabs, small fires, air, music, rain and leaves, 2026-10-11** (the user: 'next',
then 'check what ambient and music is using aida, also we have rule that we play music on
safezones only', then 'also check if there is leaves in aida, if not use noria leaves. we will
use also rain for aida how it was on noria'). In `source/world/aida/world.json` (the manifest):
- `boids`: Noria's butterfly row (MU throws them off 20 hidden Object61 boxes; ours is Noria's
  pool round the hero), built from Lorencia's Butterfly01 (`airs_from`).
- `crawls`: Tarkan's scarab, Bug02, at MU's 3 (GOBoid.cpp:1668-1675), built from Tarkan's
  (`crawls_from`, a new knob: `tools/cook.py` CRAWLS_FROM, `boid_glb` redirects a crawler too).
- `small_fires`: Object68's 11 boxes as world anchors of kind 21 (`tools/cook_anchors.py`), drawn
  by `game/world/small_fires.h` on MU's BITMAP_FLAME SubType 7 numbers: a tongue a reference
  frame, Flame01 added at 0.3, rising (1.8-2.8) x Scale units a frame, shrinking by that / 95.
- `indoors.everywhere` false: the first cut's 'underground air' held the butterflies on the ground.
- `air`: `world_aida`, MU's `aida_ambi.wav` (always on in MU, GMAida.cpp:92), everywhere, at
  -20 dB (its RMS -24.0 dBFS, the -44 dB bed of the other airs).
- `music`: `Aida.mp3`, which MU plays map-wide, in the safe hall only (the house rule).
- `leaves`: Noria's `leaf`; MU has none here (CreateMist returns false). `weather`: Noria's rain,
  ours, its light `sheets/worlds/aida_rain.json`: Aida's dry numbers times Noria's wet-over-dry
  ratios, unjudged.
- The safe hall exists where one arrives: gate 121 from Noria, its landing and spawn gate 119 all
  stand in one clearing of 165 walkable safe tiles (about 15 x 12, ringed by void, one way out
  east). Room for a few NPCs; whether to widen it is the user's call.
Committed before it was shot: another session's half-written desk.cpp kept the game from linking.

**Gate 119 is in `source/mu.db`** (the user approved it 2026-10-11): `(119, 33, 82, 8, 87, 14, 1)`,
checkpointed, carried by index.py's `gate_rows` into the world's `gates.safe` and Aida's tables. A
death in Aida now rises in the hall. Step 2 (the doors 120/121) is still to come.

## The one thing to know first

**Aida is a Season 1 world, from 2005.** It went on Korea's test server on 2005-07-28 and live with
patch 1.01b KOR on 2005-08-17. WebZen's server code names the switch `ADD_NEW_MAP_AIDA_20050617`.
That is about two years after Tarkan, Icarus and Blood Castle. It is in no 0.75, 0.95d or 0.97d
source, only S3 and S6 (A §1.1). This is decision 1.

**There are two Aidas.** The 2005 map has six breeds at levels 72-98 on 151 spots. Season 5 Part 4
(2010) added four "Bloody" breeds at levels 114-120 with 85-130k HP, on 95 more spots in a new west
strip and southern band. The client grid we have is the 2010 one, with 8 557 tiles more than the
repack's grid. The six-breed Aida runs on it unchanged (A §1.3).

**The sources are thin.** OpenMU S6 `Maps/Aida.cs` is the only stat and spawn table. The 2012 repack
(WZD) has no Aida rows, and no MoneyRate, ItemRate or MaxItemLevel exists anywhere on disk. The
client's own `gate.bmd` and `Movereq.bmd`, decoded for the first time, match OM's gates and warps
exactly (A §2).

**It is dark.** A dark olive moss forest under neutral baked light, with all its colour in the tile
sheets. Ground luminance (sheet × light) is 29, against Noria's 49, Lorencia's 46 and Tarkan's 77
(B §6). Grading it up to read is the main look problem.

## In one screen

- **Map and grid.** Height 0.00-3.27 m and nearly flat (walkable ground 1.6-2.16 m). 11 tile slots
  (0-9, 11). 35.5% walkable as **one region of 23 077 tiles**, the farthest 558 steps from town.
  34.5% NoGround. 177 stray open tiles on rows 253-255 no walk reaches (B `VOID_FILL` removes them).
- **The town is tiny**: 165 walkable safe tiles in the north-west (x 74-93, y 5-17). Spawn gate 119 is
  82,8-87,14, and its middle **84,11** is open and safe, so the `kMaps` row is
  `{"aida", 33, {84, 11}, true}`.
- **The ravine.** A NoGround ravine runs from the entrance to the north-east. 350 world walls
  (Object18-20, 13 m tall, sunk 4.7 m) line it, and 40 waterfalls pour into it. The clear colour is
  MuMain's default black, and there is no sky (B §5).
- **Breeds, the 2005 six** (OM S6 `Aida.cs:310-621`):

  | # | breed | lvl | HP | spots | body | MU's show |
  |---:|---|---:|---:|---:|---|---|
  | 308 | Death Tree | 72 | 10 000 | 29 | Monster105 @1.2 | tree_eff on ATTACK2, blur |
  | 307 | Forest Orc | 74 | 11 000 | 36 | Monster104 @1.15 | three blur trails |
  | 306 | Death Rider | 78 | 14 000 | 39 | Monster103 @1.1 | blade blur, sliding chrome |
  | 305 | Blue Golem | 84 | 25 000 | 38 | Monster102 @1.35 | blue light, sliding chrome |
  | 304 | Witch Queen | 94 | 38 500 | 6 | Monster101 @1.4 | red light, force bolt |
  | 309 | **Hell Maine** | 98 | 50 000 | 3 | Monster106 @1.8 | red chrome, five storms; 150 s respawn |

  These are OM-only numbers; respawn is 10 s except Hell Maine's (A §4.2). The Blue Golem and Death
  Rider are spread over the whole map, so the level does not climb cleanly with the walk. The ids
  are the first monster numbers above 255. The
  database and `.mur` store them as 32-bit and a grep found no 8-bit path, but a headless raise must
  confirm it. The Bloody four (549-552, Monster194-197, levels 114-120) are held back.
- **Getting there.**
  - Noria gate 120 (220,30-226,30) leads to Aida exit 113 at MU's level 130. The way back is Aida
    gate 121 (74,9-74,13) to Noria exit 122 at level 10 (OM S6 `Gates.cs:534-535, 267, 170`).
  - On our cooked Noria grid 5 of the 7 door tiles are open, in the main region, **82 steps from
    Noria's town**. Aida joins a world we already have, so no stand-in door is needed.
  - Warp list: "Aida1" (gate 119) and "Aida2" (gate 140), both level 150 and 8 500 zen.
- **Death and the Town Portal.** No mu.db row means every death in Aida rises in Lorencia. The row
  is `gates (119, 33, 82, 8, 87, 14, 1)`. WebZen sends death to Aida's town but the Town Portal to
  Noria's gate 27 (`protocol.cpp:17963-17968`). OM sends both to Aida.
- **NPCs.** None in 2005. OM S6's Osbourne and Jerridon are Season 2's Jewel of Harmony NPCs, which
  this game does not have.
- **The objects.** 3 752 placements of 74 kinds, none missing. 1 676 are rigged, 915 of them two
  shrub-card kinds. 183 flickering flower lights (155 green, 28 red). The serpent meshes scroll
  their scales. Glows come from `_R` sheet names (Treeange_R, waterfall_R, flowerlight_R/2_R,
  sunlight2_R), not BlendMesh.
- **What MU runs there.**
  - Hidden emitters `{56, 57, 58, 59, 60, 62, 63, 67, 70}`: waterfall spray, butterflies, small
    fires, a rare skull ghost and three kinds of mist box.
  - Three Bug02 scarabs, Tarkan's.
  - No birds, leaves or weather.
  - `aida_ambi.wav` loops everywhere, and `Aida.mp3` plays everywhere.
- **The new code it needs** (C §3):
  - waterfall emitters (84 anchors; we have no waterfall sheets yet);
  - capped butterflies (20 anchors, Noria's Butterfly01);
  - an `ornaments.cpp` flower-glow arm for the 183 flowers. That is 2 196 sprites map-wide, so it
    must be culled by distance;
  - `CRAWLS_FROM`, so Aida can borrow Tarkan's Bug02;
  - the air bed `world_aida`.
- **What it costs.** The fixed effects come to under 0.1 ms of the 0.3 ms effects account. The two
  to measure are the spray at a waterfall and the flower sprites.
- **Server.** It needs no Aida code, since it raises any world by name. But `gates.cpp` and
  `maps.cpp` are shared sim, so Hetzner must be rebuilt and redeployed after step 2.
- **Where it sits.** The 2005 band, 72-98, repeats Tarkan's 72-93, entered from Noria instead of from
  Atlans. MU's own warp order is Tarkan 140, Aida 150, Kanturu 160, Icarus 170.

## Where the passes disagree, settled

| point | the passes | settled |
|---|---|---|
| Sheet prefix | C writes `ad_`; B checked collisions and proposes `ai_` | **`ai_`**: B checked it is free |
| Grass | C step 1: MU's `["TileGrass01", "TileGrass02"]` | **`["TileGrass01"]`**: `TileGrass02.OZT` is fully transparent, so slot 1 grows nothing (B §1.1). TileWood01 (the same painting) is the lever if it should read lusher |
| Tiles to decode | C lists 12 by name | **B §3's list**: the slots in use are 0-9 and 11. TileRock05 is a red carpet from another map, drawn only on edge tiles; B maps its sheet to forest floor (ours, marked) |
| Stray tiles | only B | `VOID_FILL_BY_MAP[33] = [(83, 10)]`, checked to remove exactly the 177 |
| Mist and clouds | C: 91 cloud emitters never fire; B: the three mist-box kinds never emit | The same finding. GMAida hides them every frame before the emitter checks. MU draws no mist in Aida. Void mist is an option (ours) |
| Town Portal | A: WZ to Noria, OM to Aida | decision 4 |

## Traps a first build would fall into

- **The death row.** Without mu.db gate 119, deaths rise in Lorencia. Check the `.mur` header's safe
  box (82,8,87,14) after step 1.
- **Map 33, folder 34.** Every table is keyed 33; `terrain.py` runs with folder number 34.
- **Don't ground the high foliage.** Object07/25 (shrub cards draped over rocks 3-4 m up), the
  Object16 vines and the Object42 sunbeams belong up there. Tarkan's grounding rule must not be
  copied. Add `aida` to the cook's `FLAT_OVER_VOID`, or the sunk walls are flagged as buried.
- **Sheet names collide.** 9 Tile* names already exist in `source/textures`, and Crywolf's Object35
  shares about 60 sheet names with Aida, so use `ai_`. Six names ship as both .OZJ and .OZT. Decode
  them apart, or one overwrites the other.
- **`_R` sheets are additive.** No BlendMesh row will catch them. The recipes must mark them.
- **Monster numbers above 255.** These are the first. Confirm a headless raise before cooking figures.
- **Sound file names.** MuMain names `dr_idle1.wav`, but the folder ships `dr_idle01.wav`. The music
  is `Aida.mp3` with a capital A, which matters on Linux.
- **mu.db is shared.** Another session had `source/mu.db` open during the research. Write it only
  when no session is live, then checkpoint.
- **Every world's tables** after the breeds go in. That is all twelve now.

## Decisions for the user

1. **Port a 2005 world?** It is the latest world yet, but it joins Noria, which we have. If yes,
   *recommended*: **the 2005 six breeds** on the client grid, with OM S6's numbers, and the Bloody
   four held for later.
2. **Gate level, Noria <-> Aida.** MU says 130. *A and C recommend* **100 in, free out**, by the
   "just over the breeds" rule. Note that this repeats Tarkan's 100 for the same band.
3. **Warp rows.** *Recommended*: **one**, "Aida" at the hall. Two need the multi-source
   `settleFound` flood.
4. **Town Portal.** *Recommended*: **WebZen's**. Death rises in Aida's 165-tile hall and the Portal
   goes to Noria, where the merchants are.
5. **Second-class gear on map 33.** *A recommends* **out**, so Tarkan stays the second-class map. As
   built, the Witch Queen's pool is 10 rows and Hell Maine's 6; with the gear in they would be 28 and
   25.
6. **The look.** It is MU's darkest ground (luminance 29). B §6's starting sheet: a forest day, the
   house 52° sun at strength 3.6, exposure 1.5, a faint green-grey mist at 0.0025, lush olive grass,
   still ponds. Judge it in shots at the hall and the ravine.
7. **The ravine.** MU's black cut with the walls and waterfalls hanging into it
   (`VOID_BY_MAP {"start": 0.5, "depth": 4.5}`, Devias's way). Or add a void mist of ours.
   *Recommended*: **MU's first**, then judge.
8. **MU's dead extras.** *Recommended*: capped butterflies yes; no clouds, mist or skull ghost to
   start.
9. **Town NPCs.** *Recommended*: **none**, since Noria is 82 steps away.
10. **Hell Maine's skills.** *Recommended*: one area blow from MU's MonsterSkill table (wind,
    firelay), with the other breeds' skills as visuals only.
11. **Smaller calls.**
    - MoneyRate and MaxItemLevel: borrow Tarkan's 30 and 3.
    - The Golden Stone Golem (Box of Kundun +3): leave it to the Kundun-box work.
    - The Karutan door (237,166-168): sealed.
    - Music: `Aida.mp3` in the safe hall only, as the other worlds do.

## The build, in order

The detail, with commands, checks and what is seen at each step, is C §6. Each step ends in a muted
shot or run.

0. *(optional)* The air as a `MapRow` field in place of the name-keyed bools. This is the third port
   to ask for it. The headless logs stay byte-identical.
1. **Bare land.**
   - `terrain.py` and `index.py` rows (B §3);
   - the `ai_` sheets and `ground.json`;
   - `tileset.py` + `ground.py`, not `content.sh`;
   - mu.db gate 119;
   - index and sync;
   - cook `--only ground` and tables for Aida, then tables for Noria;
   - the `kMaps` row, arrival and minimap names, and `sheets/worlds/aida.json`.

   Shoot the hall (84,11), the Aida 2 clearing (188,175), the south basin (116,145) and the Karutan
   corner (238,167). **Show the user** before step 2.
2. **Gates.** `gates.cpp` rows and `testAidaGates`, then a local server with a netbot, then a Hetzner
   redeploy at the milestone.
3. **Air and music.** `aida_ambi.wav` at -20 dB gain, `world_aida`, and `Aida.mp3` in the hall.
4. **Objects**, in batches of 3-6 kinds. Trees and rocks first (Object25, 07, 10), then the walls,
   the flowers, the scrolling kinds.
5. **Ornaments and emitters**: flower glows, waterfalls, butterflies, Object68's fire.
6. **Bugs.** Tarkan's Bug02, capped at 3.
7. **Monsters.** Six kinds 304-309 and spawns 1575-1725 (today's `max(id)` is 1574). Merge the index, cook all twelve worlds'
   tables, then figures one breed at a time.
8. **The travel row.**
9. **Side hooks**: feather, Nova scroll, second-class gear, the bot's world row.

## Side findings (outside Aida)

- `gates.cpp` and `realm_travel.cpp` comments, and tarkan-port's figures, still say 100x experience.
  It is 20x (`rules.h:449`).
- `kundun-box.md:55-60` has the renewal tiers one step too high. WZ `gObjMonster.cpp:4196-4285` gives
  Tantalos, Wheel, Golem and Crust +3, as `kundun-box-sources.md` says.
- MuMain `ZzzOpenData.cpp:2691` has no break after the Golden Stone Golem case, so the Blue Golem plays
  the Death Rider's animation speeds.
- `GATE_BOXES_BY_MAP` still lacks maps 9 and 11, and `settleFound` still floods each row's landing in
  turn.

---

## Part A: the record

Research pass A, 2026-10-11. Nothing in the repo was edited, cooked or run; no window was opened;
mu.db was read with `sqlite3 -readonly`.

Paths as in `docs/tarkan-port.md` Part A:

- **OM**: OpenMU init, `LEGACY/reference/openmu/src/Persistence/Initialization/`; **OMGL** its `GameLogic/`.
  Aida exists only in **VersionSeasonSix** (`OM/VersionSeasonSix/Maps/Aida.cs`, 621 lines).
- **MM**: MuMain source, `LEGACY/reference/MuMain/src/source/`; **MMD** (= **D**) its `src/bin/Data/`.
- **WZ**: WebZen GameServer 1.00.93 C++. The clone named in the brief
  (`.../9142efc4-.../scratchpad/wz/`) is now an empty shell (no `Source/`, no `Data/`), so this pass
  read the identical full clone at
  `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/d7be4edb-cc64-4170-bc39-7c16534ae081/scratchpad/wz/`
  (488 source files, the same repo). CP949, grepped with `LC_ALL=C grep -a`.
- **WZD**: that clone's `_Server Files/Data` (the 2012 repack). **It has no Aida at all**: no map-33
  row in `Gate.txt`, `MoveLevel.txt` or `MonsterSetBase.txt`, no breed 304-309 in `Monster.txt`,
  no Aida row in `lang/*/movereq*.txt`; `MonsterSkill*.txt` are 0 bytes. Only its
  `Terrain34.att` is Aida's (an older grid, §1.3).
- **WZO** (the official 0.99.60T data the Tarkan pass used) predates Aida, and is no longer on disk.
- **MMD client tables**, decoded here for the first time (BuxConvert, `MM/Core/Globals/_crypt.h:4`):
  `D/gate.bmd` (14-byte rows: flag, map, x1, y1, x2, y2, target u16, dir, pad, level u16, max
  level u16) and `D/Local/Movereq.bmd` (`MM/Network/MoveCommandData.cpp:11-21`), and
  `D/Local/MonsterSkill.bmd` (`MM/Engine/Object/ZzzInfomation.cpp:178-200`). These are the only
  WebZen-made rows for Aida's gates and warps on disk.
- **KR / web**: Gamemeca's preview of the test-server update, 2005-07-28
  (https://www.gamemeca.com/view.php?gid=55479), and MuHistory
  (github `AlighieriDemiurgs/MuHistory` README).
- **Ours**: `MU2_BGFX/src`, and mu.db at **`MU2_BGFX/source/mu.db`**. The brief's
  `MU2/mu.db` is a 0-byte file.

Scratch (`scratchpad/aida/a/`): `grids.py` (seven Aida attribute files, the cipher copied from
`pipeline/terrain.py`), `spawns.py` (spawns, gate boxes, walks), `more.py` / `seam.py` (client vs
WZD, the expansion's seam, `map_out.txt`), `noriadoor.py` / `noriawalk.py` (the Noria side),
`gates.py`, `movereq.py`, `monskill.py` (client tables), `drops.py` (our drop pools); outputs
`grids_out.txt`, `spawns_out.txt`, `more_out.txt`, `map_out.txt`, `noriadoor_out.txt`,
`gates_mmd.txt`, `movereq_mmd.txt`, `monskill_out.txt`, `drops_out.txt`, `om_rows.txt`.

Compass as in tarkan-port: x grows east, y grows south. In `a/map_out.txt` the top row is y 0.

### 0. The one thing to know first

**Aida is Season 1, 2005, the youngest world this game would carry by two years.** WebZen put it on
the Korean test server with the first update of "뮤 온라인 에피소드: 쿤둔의 역습" (Kundun's
Counterattack) on 2005-07-28, and live with patch 1.01b KOR on 2005-08-17, the first day of
Season 1, beside Crywolf and Kalima 7. The server code dates its switch `ADD_NEW_MAP_AIDA_20050617`
(`WZ define2.h:2272-2273`). Tarkan (0.84, 2003-01), Icarus (2003-06), the 2nd class and Blood
Castle (2003-07/09) all came before it. Evidence in §1.1; it is decision 1.

**There are two Aidas.** The 2005 map had **six breeds**: five "new monsters" and the boss
**Hell Maine** (Gamemeca 2005-07-28) -- Death Tree 72, Forest Orc 74, Death Rider 78, Blue Golem
84, Witch Queen 94, Hell Maine 98, **151 spots**. Season 5 Part 4 (2010-04-27) "expanded" the map
and added four **Bloody** breeds at levels **114-120** with **85 700-129 700 HP**, **95 spots** in a
new west strip and southern band. OM S6 carries both (246 spots). The WZD repack's
`Terrain34.att` is the older grid: every one of the 95 Bloody spots is closed on it, and the
8 557-tile expansion is open only on the client grid (§1.3). Which Aida to port is decision 2.

**One source for the numbers.** WebZen's own data for Aida is not on disk: WZD has none, WZO
predates it. OM S6 is the only stat and spawn table. WZ's C++ gives the rules (death, portal,
the /move command) and the client's `gate.bmd` and `Movereq.bmd` agree with OM's gates and
warps to the tile. MoneyRate, ItemRate and MaxItemLevel have **no source** on disk.

**Entered from Noria, at MU's 130.** A gate in Noria's north-east, 220,30-226,30, 82 steps from
Noria's town, leads to a small safe town in Aida's north-west corner (82,8-87,14). Its tiles
are open on our cooked Noria grid (5 of 7), which is 0.75's grid there too.

### 1. Map

#### 1.1 Is it 0.75? (decision 1: the evidence)

**OpenMU.**
- **Version075** and **Version095d** have no Aida: `grep -i aida` over `OM/` hits only
  `VersionSeasonSix/GameMapsInitializer.cs:43`, `VersionSeasonSix/Gates.cs` and
  `VersionSeasonSix/Maps/Aida.cs`.
- **SeasonSix**: `Aida.cs:19` `Number = 33`, `:24` `"Aida"`; two NPC spawns (`:43-47`), 246
  monster spawns (`:50-307`), ten breeds (`:310-621`). No drop groups, no terrain prefix (it reads
  `Resources/Terrain34.att`).

**WebZen's C++ (WZ).**
- `define2.h:2272-2273` "아이다 맵 추가 (apple)" `#define ADD_NEW_MAP_AIDA_20050617`, in the block
  with the Monster SKILL system (`ADD_NEW_MONSTER_SYSTEM_01_20050714`) and Hidden Kalima
  (`HIDDEN_KALIMA_20050706`); `:3982-3983` `MAX_MAP 34` "아이다 맵이 추가 되었다" (between
  Devil Square's extension 33 and Crywolf's 35); `:4090-4091` `MAP_INDEX_AIDA = 33`.

**The record (web).**
- **Gamemeca, 2005-07-28 15:39** (gid 55479): "'뮤 온라인 에피소드: 쿤둔의 역습' 1차 업데이트" on the
  test server: Aida, "뮤 대륙에서 오랫동안 격리된 채 마물들의 지배를 받고 있던 신비한 숲" (a mysterious
  forest long cut off and ruled by monsters); "신규 몬스터 5종" with skills and attack patterns;
  "보스 몬스터 헬마이네가 출현한다", Hell Maine "쿤둔과 계약을 맺은 마족" (a demon in league with
  Kundun); "전체적인 난이도는 중저레벨 유저에게 맞춰져" (pitched at low-to-mid levels); "노리아 맵에
  있는 워프 게이트를 통해 이동가능" (through a warp gate in Noria). Crywolf and Kalima 7 came in the
  second update.
- **MuHistory**: 17.08.2005, "Season 1 officially begins with the 1.01b KOR aka 1.01.2 KOR
  patch ... Introduction of map Aida", no entry level for the first 7 days; 17.10.2005 "Adjustments
  to Aida, Crywolf and Kalima 6 entry level requirements" (values not given); 27.03.2007 (Season 2)
  "Changes to Aida map location on the 'M' Teleport Window"; **27.04.2010 (Season 5 Part 4)
  "Expansion of maps and addition of new monsters. Maps affected: Aida, Kanturu, Swamp of Peace,
  LaCleon"**: the Bloody breeds. Jewel of Harmony (Osbourne's and Jerridon's trade) is Season 2,
  2006-2007.

**The cross-check versions.** 0.75: absent. 0.95d: absent. 0.97d: absent (Aida is 2005). S3 ep6 /
S6: present (WZ's 1.00.93 code; OM S6; MuMain `WD_33AIDA`, `MM/World/MapInfra/MapManager.h:28`,
data `D/World34` + `D/Object34`).

**What this game carries from after 0.75**, for weighing: Tarkan (2003-01), Icarus and the class
change (2003-06/07), Blood Castle (2003-09), Devil Square (2003-02). Aida is about two years after
the newest of them, and the first world built for WebZen's 2005 monster-skill system.

#### 1.2 Map facts

- WZ: `MAP_INDEX_AIDA = 33` (`define2.h:4091`); regen box **`gRegenRect[33] = 82,8-87,14`** "아이다"
  (`WZ MapClass.cpp:203-205`). MuMain `WD_33AIDA`, data `D/World34`, `D/Object34` (world index + 1);
  `GMAida.cpp` (1 570 lines) is its whole world module.
- Exp multiplier 1, no map requirement in OM (Aida.cs overrides nothing; `OM/BaseMapInitializer.cs`).
- **NPCs** (OM S6, `Aida.cs:45-46`): **Osbourne** (369) at 78,13 facing south-east, the refining-
  stone maker (`OM/VersionSeasonSix/NpcInitialization.cs:496-499`, `NpcWindow.RefineStoneMaking`),
  and **Jerridon** (370) at 86,14 facing south-west, who removes Jewel of Harmony options (`:506-509`).
  Both are Season 2 Harmony NPCs (2006+), so the 2005 town had none of them; **no merchant** in
  any source. Jerridon's tile 86,14 is NoMove on every grid (an NPC may stand there).
- MuMain: Aida's own music `data\music\Aida.mp3` (`MM/Core/Globals/_enum.h:186`), played on the
  **whole map** (`MM/Scenes/SceneManager.cpp:1089-1094`), and the loop `Sound/w34/aida_ambi.wav`
  (`MapManager.cpp:328-340`). `assets/music/Aida.mp3` is already in our tree.
- Map name `I18N::Game::Aida` (`MapManager.cpp:1687-1689`); the Town Portal is allowed here
  (`MM/World/MapInfra/PortalMgr.cpp:55`).

#### 1.3 One grid, one region (and the 2010 expansion)

Seven files decoded (`a/grids.py`). Walkable = `(flags & (NoMove|NoGround)) == 0`.

| grid | walkable | regions (largest) | NoGround | safe (walkable) | safe box |
|---|---:|---|---:|---|---|
| client `D/World34/EncTerrain34.att` (2-byte) = `Terrain.att` (plain) = **OM `Terrain34.att`** | **23 254** (35.5%) | 8 (23 077; then 111, 42, 10...) | 22 640 (34.5%) | 550 (**165**) | x 74-93, y 5-17 |
| client `EncTerrainTest34.att` | 23 197 | 17 (23 003) | 22 829 | 550 (165) | same |
| client `terrainserver.att` (1-byte) | 23 624 | 2 (23 077; 547 in rows 254-255) | 22 416 | 550 (165) | same |
| WZD `Terrain34.att` (1-byte) | 16 012 | 5 (14 552, 919, 481...) | 12 464 | 550 (214) | x 73-93, y 5-19 |
| OM `Terrain34_Close.att` | 479 | 1 | 0 | 525 (479) | x 67-93, y 0-19 (the town alone; unused by OM's code) |

- **The client grid = OM's, tile for tile** (0 differences). Settled: the client grid, as every
  earlier world.
- **One reachable region** of 23 077 tiles from the town; the farthest open tile is **558 steps**
  away (Tarkan's was 279). The seven small pockets are unreachable scraps.
- **The safe town is tiny**: 165 walkable safe tiles in the north-west corner, x 74-93, y 5-17
  (Tarkan's had 1 107). WZ's regen box 82,8-87,14 is 40 of 42 open.
- **Client vs WZD: 10 020 tiles.** 8 631 are open only on the client, in one 8 557-tile piece
  (x 16-243, y 39-242: the west strip x 10-56 and the southern band y 180-242) joined to the old map
  along 48 seam tiles between x 80-193, y 138-181, plus a 74-tile piece at the Season 6 Karutan
  door (233-240, 155-172). **All 95 Bloody spawns are closed on WZD**, and of the 151 original
  spawns only one Death Rider (90,203) is. So WZD's file is the pre-2010 Aida, and the client
  grid is the Season 5 Part 4 map (A §0). 1 389 tiles are open only on WZD (mostly the same
  regions re-cut).
- No "2" standing-snapshot tile beyond one; NoGround 34.5%, much less than Tarkan's 63.6%: Aida is
  mostly ground with NoMove forest walls (42 148 NoMove tiles), not plateaus over a void.

The shape (`a/map_out.txt`): the town in the north-west corner; Death Trees and Forest Orcs in the
northern woods (x 86-236, y 8-171); Death Riders and Blue Golems in the middle and east; the Witch
Queens and Hell Maines in the central maze (x 88-141, y 47-147), far by walk (331-532 steps) though
close as the crow flies; the Bloody breeds in the west strip and the south.

### 2. Gates and warps

#### 2.1 The door from Noria

Rows from the client's `gate.bmd` (`a/gates_mmd.txt`); OM S6 `Gates.cs` agrees on every box,
target and level.

| gate | flag | map | box | to | dir | level | OM S6 Gates.cs | open (client grid) |
|---:|---|---|---|---|---|---:|---|---|
| **120** | enter | Noria | 220,30 - 226,30 | 113 | -- | **130** | `:534` | 5 of 7 on our cooked Noria, the client's, OM S6's and **OM 075's** Noria grids; 82-86 steps from Noria's town (gate 27) |
| **113** | exit | Aida | 76,9 - 78,16 | -- | 0 | (10) | `:267` | 13 of 24, all safe |
| **121** | enter | Aida | 74,9 - 74,13 | 122 | -- | **10** | `:535` | 4 of 5, safe |
| **122** | exit | Noria | 220,31 - 226,34 | -- | 0 | (10) | `:170` | 26 of 28 |
| **119** | spawn | Aida | 82,8 - 87,14 | -- | 0 | (130) | `:265` (spawn gate) | 40 of 42, safe; = WZ's regen box |
| **140** | exit | Aida | 186,173 - 190,177 | -- | 0 | (130) | `:266` | 25 of 25, not safe; 251-255 steps from the town |
| 336 | enter | Aida | 237,167 - 240,168 | 337 (Karutan, map 80) | -- | 160 | `:614` | 8 of 8; closed on WZD |
| 339 | exit | Aida | 237,166 - 240,166 | -- | 1 | (160) | `:268` | 4 of 4 |

- The way in asks **130**, the way back **10**. MuHistory says the entry level was re-tuned on
  2005-10-17; the 2005 value is not recorded. WZ has no gate-level special case for Aida (only the
  MG/DL two-thirds rule that Tarkan's pass found).
- The door's tiles on Noria's side were already open in 0.75's grid; what stands there in our
  Noria is pass B's question.
- Gates 336/339 (Karutan) are Season 6 Episode content; not ours.

#### 2.2 Warp list

- **Client `Movereq.bmd`** (`a/movereq_mmd.txt`): **"Aida1" level 150, 8 500 zen -> gate 119;
  "Aida2" level 150, 8 500 zen -> gate 140.** OM S6 the same (`Gates.cs:70-71`, warp indexes 25
  and 27).
- WZ's chat command "/이동 아이다" -> gate **119**, no zen, level from `MoveLevel` for map 33
  (`WZ MoveCommand.cpp:899-907`).
- MU's order by warp level: Tarkan 140, **Aida 150**, Kanturu 160, Icarus 170.
- Aida2 (186,173-190,177) is in the old map, 251 steps out, ringed by Blue Golems, Death Riders and
  Forest Orcs; a second row needs the multi-source flood (`tarkan-port.md` C §4.3).

### 3. Terrain attributes

| file | 0 | 1 safe | 2 | 4 NoMove | 5 | 8 NoGround | 12 | 13 | walkable |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| client `EncTerrain34.att` = OM | 23 088 | 165 | 1 | 19 314 | 328 | 134 | 22 449 | 57 | 23 254 |
| client `terrainserver.att` | 23 459 | 165 | -- | 19 168 | 328 | 89 | 22 270 | 57 | 23 624 |
| WZD `Terrain34.att` | 15 797 | 214 | 1 | 36 781 | 279 | 127 | 12 280 | 57 | 16 012 |

No water bit (16) anywhere: Aida's lakes and waterfalls are drawn, not flagged.

### 4. Monsters

#### 4.1 Spawns (OM S6 only)

`OM/VersionSeasonSix/Maps/Aida.cs:52-306`: **246** single-tile `CreateMonsterSpawn(id, def, x, y)`,
no direction (`a/om_rows.txt`). Every spot is open and non-safe on the client grid. No other
spawn table for Aida is on disk.

| # | breed | spots | where (client grid) | walk from the town (min / median / max) | OM lines |
|---:|---|---:|---|---|---|
| 308 | Death Tree | **29** | the northern woods, x 103-228, y 8-130 | 16 / 108 / 192 | `:175-203` |
| 307 | Forest Orc | **36** | north and east, x 86-236, y 10-171 | 39 / 125 / 398 | `:138-173` |
| 306 | Death Rider | **39** | the middle and east, x 85-237, y 12-203 | 101 / 220 / 516 | `:98-136` |
| 305 | Blue Golem | **38** | everywhere, x 81-235, y 19-175 | 93 / 398 / 541 | `:59-96` |
| 304 | Witch Queen | **6** | the central maze, x 88-141, y 49-125 | 331 / 508 / 526 | `:52-57` |
| 309 | **Hell Maine** | **3** | 112,47; 123,103; 108,147 (the maze) | 376 / 479 / 532 | `:205-207` |
| | *2005 Aida* | **151** | | | |
| 549 | Bloody Orc | 23 | the south band, x 38-240, y 194-238 | 274 / 310 / 425 | `:209-231` |
| 550 | Bloody Death Rider | 25 | west strip and south, x 20-206, y 53-231 | 276 / 343 / 544 | `:233-257` |
| 551 | Bloody Golem | 28 | west strip and south, x 22-236, y 48-232 | 281 / 397 / 549 | `:259-286` |
| 552 | Bloody Witch Queen | 19 | the west strip, x 20-56, y 53-227 | 419 / 464 / 544 | `:288-306` |
| | *2010 expansion* | **95** | | | |
| | **total** | **246** | | | |

By walk from the town (`a/spawns_out.txt`): 0-80 steps Death Trees and Forest Orcs only; 80-160
the same plus Death Riders and the first Blue Golems; 160-280 thinning Orcs, Riders, Golems; past
280 the Bloody band and the maze. **The level does not climb cleanly with the walk**: Blue Golems
(84) and Death Riders (78) are spread over the whole map, and the 94/98 pair sits in the middle
of the map but 330-530 steps away by the winding path.

**Flags:**
- OM's Witch Queen and Hell Maine "spots" are single tiles; WZ's type-2 respawn (random within ±3)
  is not knowable for Aida with no WZ table.
- The expansion's 95 spots need the client grid; on the 2005 grid (WZD) none is reachable.

#### 4.2 Breeds (`OM/VersionSeasonSix/Maps/Aida.cs:310-621`)

Resistances OM `n/255` as n (Poison Ice Fire; no wind/water rows). `NumberOfMaximumItemDrops = 1`,
`Attribute = 2` for all.

| # | name | lvl | HP | dmg | def | atk / def rate | move rg | atk rg | view | move / atk ms | respawn | P I F | OM line |
|---:|---|---:|---:|---|---:|---|---:|---:|---:|---|---:|---|---|
| 308 | Death Tree | 72 | 10 000 | 248-272 | 187 | 362 / 120 | 4 | 1 | 5 | 450 / 1400 | 10 s | 18 18 18 | `:436-465` |
| 307 | Forest Orc | 74 | 11 000 | 258-282 | 197 | 390 / 127 | 4 | 1 | 5 | 450 / 1400 | 10 s | 19 19 19 | `:405-434` |
| 306 | Death Rider | 78 | 14 000 | 260-294 | 222 | 402 / 144 | 4 | 1 | 5 | 450 / 1400 | 10 s | 20 20 20 | `:374-403` |
| 305 | Blue Golem | 84 | 25 000 | 288-322 | 277 | 507 / 170 | 4 | 2 | 7 | 450 / 1500 | 10 s | 21 21 21 | `:343-372` |
| 304 | Witch Queen | 94 | 38 500 | 408-442 | 357 | 637 / 230 | 4 | 3 | 7 | 450 / 1700 | 10 s | 22 22 22 | `:312-341` |
| 309 | **Hell Maine** | 98 | 50 000 | 550-600 | 520 | 850 / 300 | 6 | 5 | 10 | 600 / 2000 | **150 s** | 50 50 50 | `:467-496` |
| 549 | Bloody Orc | 114 | 117 000 | 675-710 | 420 | 900 / 820 | 4 | 2 | 9 | 700 / 1500 | **8 s** | 20 20 150 | `:498-527` |
| 550 | Bloody Death Rider | 115 | 88 000 | 795-830 | 460 | 1100 / 430 | 4 | 2 | 9 | 700 / 2500 | 10 s | 20 20 150 | `:529-558` |
| 551 | Bloody Golem | 117 | 129 700 | 715-750 | 470 | 900 / 960 | 4 | 2 | 9 | 700 / 1500 | 10 s | 20 20 150 | `:560-589` |
| 552 | Bloody Witch Queen | 120 | 85 700 | 835-870 | 480 | 1100 / 430 | 4 | 3 | 9 | 700 / 2500 | 10 s | 20 20 150 | `:591-620` |

- **No WebZen numbers to check these against.** WZD's Monster.txt stops before 304; WZO predates
  Aida; WZ's C++ names no Aida class. Flag every number as OM-only.
- **MoneyRate, ItemRate, MaxItemLevel**: no source on disk (OM carries none of them for any map).
- **The 2005 six against Tarkan's seven**: almost the same curve -- Death Tree = Mutant
  (72, 10 000 HP), Blue Golem = Beam Knight (84, 25 000). The Witch Queen (94, 38 500) is a
  Zaikan-class monster on 6 ordinary 10 s spots. Hell Maine is the strongest non-instanced monster
  this game would have (98, 50 000, def 520).
- **The Bloody four are a different game**: 85 700-129 700 HP and 675-870 damage, against the
  game's present ceiling (Icarus 108; Blood Castle 6's 99). Their fire resistance is 150/255.
- Name spelling: OM "Hell Maine", MuMain `MONSTER_HELL_MAINE`, KR "헬마이네".

**Skills** (client `D/Local/MonsterSkill.bmd`, `a/monskill_out.txt`; MuMain plays them through
`CheckMonsterSkill`, `GMAida.cpp:1515-1561`):
- **Hell Maine: skills 2 and 3**, `ATMON_SKILL_WIND` and `ATMON_SKILL_FIRELAY`
  (`MM/Core/Globals/_enum.h` ATMON list; sounds `w34/hm_bloodywind.wav`, `hm_firelay.wav`); its
  ATTACK1 throws five `MODEL_STORM` (`GMAida.cpp:1486-1499`).
- Bloody Golem 68, Bloody Witch Queen 69 (their own attack skills, `GMAida.cpp:1530-1556`).
- Witch Queen and Blue Golem take the skill path too (`:1522-1525`) with no row in the client
  table; the Witch Queen's ATTACK2 throws a `BITMAP_JOINT_FORCE` (`:1464-1473`), the Death Tree's
  ATTACK2 its `tree_eff` (`:1475-1484`).
- OM creates none of these (no `AttackSkill` in Aida.cs): in OM all ten only melee.

**Experience per kill** (`src/sim/rules.cpp:380-396`; `kExperienceRate` is **20** now,
`src/sim/rules.h:449`): Death Tree 3 090, Forest Orc 3 284, Death Rider 3 689, Blue Golem 4 340,
Witch Queen 5 542, Hell Maine 6 064; Bloody 8 384 / 8 541 / 8 860 / 9 350; times 20.

#### 4.3 What MuMain draws (`MM/World/GameMaps/GMAida.cpp:331-483`; body `D/Monster/Monster{MODEL+1}.bmd`, `MM/Engine/Object/ZzzOpenData.cpp:2549-2561`; models `MM/Core/Globals/_enum.h:4250-4255, 4343-4346`, monsters `:4650-4655`, golden `:4823`)

| # | body | scale | notes | lines |
|---:|---|---:|---|---|
| 304 Witch Queen | **Monster101** (MODEL 100) | 1.4 | red terrain light (0.7,0.1,0.1) over 3 tiles; force bolt on ATTACK2; chaos-thunder sound; die at 0.2 | `:336-353`, `:489-494`, `ZzzOpenData.cpp:2688-2689` |
| 305 Blue Golem | **Monster102** (MODEL 101, `MODEL_GOLDEN_STONE_GOLEM`) | 1.35 | blue terrain light (0,0,0.7); mesh 0 sliding + bright chrome pass | `:355-366`, `:496-501`, `:1339-1356` |
| 306 Death Rider | **Monster103** | 1.1 | blade blur trails; mesh 3 sliding + chrome | `:368-378`, `:532-563`, `:1358-1377` |
| 307 Forest Orc | **Monster104** | 1.15 | three blur trails | `:380-387`, `:564-599` |
| 308 Death Tree | **Monster105** | 1.2 | horn bones; `Object34/tree_eff` on ATTACK2; blur | `:389-404`, `:600-627`, `:1475-1484` |
| 309 Hell Maine | **Monster106** | **1.8** | red terrain light (1,0,0); red chrome over meshes 0 and 2; five storms on ATTACK1 | `:406-422`, `:503-508`, `:1379-1411`, `:1486-1499` |
| 549 Bloody Orc | Monster194 | 1.35 | blur trails | `:424-432` |
| 550 Bloody Death Rider | Monster195 | 1.2 | as the Death Rider | `:434-445`, `:1413-1432` |
| 551 Bloody Golem | Monster196 | 1.4 | blue light, chrome | `:447-459`, `:1434-1455` |
| 552 Bloody Witch Queen | Monster197 | 1.75 | red light; force bolt | `:461-480` |

All ten bodies are in `MMD/Monster/` (some lower-case: `monster101.bmd`). Sounds: 30 wavs in
`MMD/Sound/w34/` (wq_, bg_, dr_, fo_, dt_, hm_ prefixes, plus `aida_ambi.wav`). Pass B owns the
assets; this table is for the record.

#### 4.4 What MU2_BGFX has

`sqlite3 -readonly MU2_BGFX/source/mu.db`: **none of 304-309, 549-552 or 495** in `monster_kinds`;
`monster_spawns` maps 0, 1, 2, 3, 4, 7, 8, 9, 10, 11 (no 33); `gates` rows 17, 22, 27, 42, 49, 57
(no 119). Nothing in `src/` names Aida or map 33 (`grep -i aida` hits only unrelated words);
`src/game/ui/travel.cpp:241` `kComing` lists Lost Tower, Atlans, Tarkan, Icarus, not Aida.

Rows to add if the 2005 Aida is chosen (OM S6 numbers; respawn RegTime + 1 by the house rule):

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (308,'Death Tree', 72,10000,248,272,187,4,1, 5,450,1400,362,120, 10,NULL),
 (307,'Forest Orc', 74,11000,258,282,197,4,1, 5,450,1400,390,127, 10,NULL),
 (306,'Death Rider',78,14000,260,294,222,4,1, 5,450,1400,402,144, 10,NULL),
 (305,'Blue Golem', 84,25000,288,322,277,4,2, 7,450,1500,507,170, 10,NULL),
 (304,'Witch Queen',94,38500,408,442,357,4,3, 7,450,1700,637,230, 10,NULL),
 (309,'Hell Maine', 98,50000,550,600,520,6,5,10,600,2000,850,300,150,NULL);
-- kDropRates: no MoneyRate/MaxItemLevel on disk; decision 11 (Tarkan's {30,3} is the nearest).
-- kResistances {number, ice, poison}: {308,18,18},{307,19,19},{306,20,20},{305,21,21},{304,22,22},{309,50,50}
-- monster_spawns: 151 one-tile rows on map 33 (Aida.cs:52-207); gates: (119, 33, 82,8,87,14, 1).
-- The Bloody four (549-552, Aida.cs:209-306 and :498-620) only if decision 2 takes the 2010 map.
```

### 5. Drops

**Nothing Aida-specific in OM**: no drop groups, `NumberOfMaximumItemDrops = 1`. No ItemRate or
MaxItemLevel anywhere on disk. No Aida-only item: Gamemeca's preview names none, and the Season 1
items (Crywolf's) are not Aida's.

**What each breed reaches in our tables** (`a/drops_out.txt`: recipes in `source/items/*/*.json`
with `drops_from_monsters`, window [L-15, L] as `src/sim/realm_items.cpp:1135, 1320-1328`;
[2] = `class_level` 2, admitted only where `secondGearHere`):

| killer | L | window | rows | of them [2] | without [2] |
|---|---:|---|---:|---:|---:|
| Death Tree | 72 | 57-72 | 21 | 3 | 18 |
| Forest Orc | 74 | 59-74 | 22 | 4 | 18 |
| Death Rider | 78 | 63-78 | 18 | 5 | 13 |
| Blue Golem | 84 | 69-84 | 22 | 7 | 15 (Black Dragon, Sword of Destruction, Saint Crossbow, Cometfall...) |
| Witch Queen | 94 | 79-94 | 28 | **18** | 10 (Black Dragon Armor, Staff of Destruction, Inferno, Elemental Mace, Dragon Spear...) |
| Hell Maine | 98 | 83-98 | 25 | **19** | 6 |
| Bloody Orc / Rider | 114 / 115 | 99-115 | 13 | 9 | 4 (Dark Breaker, Divine Staff of Archangel...) |
| Bloody Golem | 117 | 102-117 | 9 | 6 | 3 |
| Bloody Witch Queen | 120 | 105-120 | 6 | 4 | 2 |

- **`secondGearHere` does not name map 33** (`src/sim/realm_items.cpp:1322-1323`: Tarkan, Icarus,
  Blood Castle 6). As is, the Witch Queen and Hell Maine would drop from thin pools (10 and 6
  rows). Decision 5.
- Excellent drops use the kill's level minus 25 (`src/sim/items.h:610`): 47-73 here.
- **Class treasures** (WZO `Quest(Kor).txt` per tarkan-port §5): any monster of level 62-76 while
  the quest stands: in Aida the Death Tree and Forest Orc. MuMain's Sevina text does not name Aida.

#### 5.1 The Golden Stone Golem and the Box of Kundun +3

- **WZ** (`EledoradoEvent.cpp:796-850`, `RegenGoldenStoneMonster`): class **495** "황금돌괴물"
  (golden stone monster) is placed on **Aida** at a random free tile anywhere
  (`GetBoxPosition(MapNumber, 1, 1, 255, 255)`) in the post-2009 build, every
  `gGoldenStoneMonsterRegenTIme` **60 min** (`Gamemain.cpp:1264`). It drops **one Box of Kundun
  level 10 = +3** (14,11) to the top damage dealer, with the Golden Tantalos, Wheel and Crust
  (`gObjMonster.cpp:4240-4250`). This is `docs/kundun-box-sources.md:230`'s "Golem Aida".
- The branch is `#ifdef GAME_VERSION < G_V_S4_5` / `MAP_INDEX_RORENCIA` / `#else` /
  `MAP_INDEX_AIDA` (`EledoradoEvent.cpp:808-812`). `#ifdef` tests only that `GAME_VERSION` is
  defined, so a build that defines it puts the Golem on Lorencia; the intent (Aida from S4.5) is
  plain from the code.
- **Body**: the Blue Golem's, Monster102 at 1.35, with a blue light and the chrome pass
  (`MM/Engine/Object/ZzzCharacter.cpp:14885-14893`; `GMAida.cpp:1339-1356`). Building the Blue
  Golem builds it.
- **Stats: none on disk** (OM has no 495; WZD's Monster.txt stops earlier).
- Not 2005: the golden renewal is 2009 (`ADD_GOLDEN_EVENT_RENEWAL_20090311`).

### 6. Rules of the place

- **Entry**: Noria gate 120 (220,30-226,30, Noria's north-east, 82 steps from its town) at **130**
  to exit 113 in Aida's town; back by gate 121 (74,9-74,13) at **10** to Noria exit 122. The two
  boxes sit 2-4 tiles apart in Aida's town.
- **Death -> Aida's town.** WZ: the death branch for map 33 calls `GetMapPos(33)`
  (`WZ user.cpp:22152-22158`), which keeps the map (`MapClass.cpp:309-317`) and picks an open tile
  of `gRegenRect[33]` = 82,8-87,14. OM S6: gate 119 is a spawn gate on map 33. Agreed.
- **Town Portal -> Noria.** WZ: `MapNumber == MAP_INDEX_AIDA -> gObjMoveGate(27)`, "아이다였다면
  노리아로 이동" (`WZ protocol.cpp:17963-17968`), as Crywolf (`:17970-17974`). OM uses the spawn
  gate (the town). The two disagree; WZ outranks OM.
- **Mounts and wings: no rule** (no Aida branch in `WZ MoveCommand.cpp` beyond the /move row).
  The Move Charm works here (`WZ user.cpp:34138`).
- **Music**: `Aida.mp3` over the whole map in MuMain; our house rule is the safe zone only.
- **The second section's mist**: MuMain's `IsInAidaSection2` box, x 54.5-178.2, y 67.8-224.2 tiles
  (`GMAida.cpp:23-28`), gates the map's mist (pass B).
- **Crawlers**: Tarkan's Bug02 scarabs, the same row (`MM/Engine/AI/GOBoid.cpp:1726-1735`), and
  butterflies (`MapManager.cpp:328-330`) (pass B).
- Nothing else: no traps, no PK rule, exp multiplier 1.

### 7. Where the sources disagree

| point | OM S6 | WZ C++ | client tables (gate.bmd, Movereq.bmd) | WZD | web | settled / flagged |
|---|---|---|---|---|---|---|
| in 0.75 / 0.97d | -- (S6 only) | 2005 switch | | none | 2005-08-17, Season 1 | **not ours by era** (decision 1) |
| map | 246 spots, 10 breeds | -- | -- | 2005 grid (`Terrain34.att`) | 2010 expansion | **two Aidas** (decision 2) |
| grid | = client | -- | client | older, 10 020 off | | **client** |
| gate in / out | 130 / 10 | -- | 130 / 10 | -- | level re-tuned 2005-10-17 | **130 / 10** (ours: decision 3) |
| warp | Aida1/Aida2, 150, 8 500 | /move -> 119 | same | -- | | 150 |
| death | town (spawn gate 119) | town (regen 82,8-87,14) | | | | **the town** |
| Town Portal | town | **Noria gate 27** | | | | **WZ: Noria** (decision 4) |
| stats | as §4.2 | -- | -- | -- | | **OM only, unchecked** |
| MoneyRate / MaxItemLevel | -- | -- | -- | -- | | **no source** (decision 11) |
| monster skills | none (melee) | data-driven, data empty | Hell Maine wind + firelay | empty files | "skills and attack patterns" | MuMain's (decision 9) |
| NPCs | Osbourne, Jerridon (Harmony) | -- | | | none named in 2005 | **none** for a 2005 port (decision 7) |
| Golden Stone Golem | absent | Aida (S4.5+), Box +3 | body = Blue Golem | absent | | decision 8 |

### 8. Open questions

1. **Decision 1: port a Season 1 world.** Aida is 2005-08, two years after everything else carried
   (§1.1). Every post-0.75 world so far (Tarkan, Icarus) had WebZen's own data to check OM against;
   Aida has only OM S6 and the client. *Recommended if yes*: OM S6's numbers, checked against the
   client's gate and warp tables, which agree.
2. **Decision 2: which Aida.** (a) **The 2005 Aida**: six breeds, 151 spots, 72-98, on the client
   grid with the 2010 expansion left empty; (b) the 2010 Aida with the Bloody four at 114-120;
   (c) (a) now, (b) later as the game's first band above 108. *Recommended*: **(a)**, holding (b)
   for when the level curve reaches it. The expansion's terrain is in the client map either way;
   whether it is closed or open-and-empty is pass B/C's call.
3. **Decision 3: gate level.** MU: 130 in, 10 out; warp 150. Our convention (tarkan-port decision
   2): just over the breeds -- Atlans 70 over 43-74, Tarkan 100 over 72-93, Icarus 160 = MU's.
   *Recommended*: **100 in, out free**, the 2005 six being 72-98. With the Bloody band, the
   expansion would need its own bar (no door separates it in MU).
4. **Decision 4: Town Portal.** WZ sends it to Noria (gate 27), OM to Aida's town; death is Aida's
   town in both. *Recommended*: **WZ's split**: death in Aida's town, portal to Noria -- the town has
   no merchant, and Noria is 82 steps from the door.
5. **Decision 5: second-class gear in Aida.** As built, map 33 is not in `secondGearHere`, so the
   Witch Queen and Hell Maine fall to 10 and 6 rows; with it, 28 and 25, 18-19 of them second-
   class pieces, on a map whose band equals Tarkan's. Tarkan is the user's stated second-class map.
   *Recommended*: **keep Aida out**, so Tarkan keeps its purpose; ask.
6. **Decision 6: warp rows.** Aida1 (the town) and Aida2 (186,173-190,177, needs the multi-source
   flood). *Recommended*: **one row**, Aida1, its level matching the gate's, opening on arrival.
7. **Decision 7: NPCs.** OM S6's Osbourne and Jerridon trade in the Jewel of Harmony, which the
   game does not have and which is Season 2. *Recommended*: **none**, as 2005; or one potion seller
   if the walk back proves long (the door to Noria is 2-4 tiles from the town).
8. **Decision 8: the Golden Stone Golem / Box +3.** WZ's post-2009 home for it is Aida; nothing is
   built (`docs/kundun-box.md`), no stats on disk. *Recommended*: leave to the Kundun-box work;
   building the Blue Golem builds its body.
9. **Decision 9: monster skills.** MU's Hell Maine casts wind and firelay and throws five storms;
   the Witch Queen a force bolt; the Death Tree its root attack. OM melees. *Recommended*: Hell Maine
   as a boss with one area blow (as `kBosses`' Flame of Evil), the others' shows as effects only.
10. **Decision 10: Hell Maine and the Witch Queen.** OM has the Witch Queen (94, 38 500) on six
    10-second spots and Hell Maine on three 150-second spots. Keep, or make the Witch Queen
    slower? *Recommended*: keep OM's.
11. **Decision 11: MoneyRate / MaxItemLevel.** No source. *Recommended*: Tarkan's (MoneyRate 30,
    MaxItemLevel 3), the nearest era and band.
12. **Smaller calls**: Aida.mp3 in the town only (house rule; MuMain plays it everywhere); the Karutan
    door at 237,167 as scenery; the Season 2 "M window" relocation (2007) is irrelevant to us.

### Where Aida sits

Our worlds by monster level (`mu.db` spawns, read-only): Lorencia 2-19, Noria 3-18, Devias 20-52,
Dungeon 19-55, Lost Tower 47-66, Atlans 43-74, **Tarkan 72-93**, Devil Square 28-86 (squares),
**Icarus 75-108**, Blood Castle 1-6 to 99 (instanced; tarkan-port). **The 2005 Aida's 72-98 is
Tarkan's band again**, from a different town: Tarkan is reached through Atlans's far corner (745 +
54 steps), Aida 82 steps from Noria's town. MU itself orders them Tarkan 140 -> Aida 150 -> Kanturu
160 -> Icarus 170 by warp level, and Gamemeca's 2005 preview pitches Aida at low-to-mid players.
The Bloody four (114-120) would sit above every band the game has.

---

### Side findings (outside Aida)

1. **`docs/kundun-box.md:55-60`'s renewal tiers are one step high.** WZ gives the Titan and Soldier
   level 8 (+1), the Lizard King, Vepar and Golden Devil 9 (+2), the Tantalos, Wheel, Stone Golem and
   Crust 10 (+3), the Satyros and Twintail 11 (+4), the Iron Knight and Napin 12 (+5)
   (`WZ gObjMonster.cpp:4196-4285`). `docs/kundun-box-sources.md:226-232` has it right.
2. **WZ's `#ifdef GAME_VERSION < G_V_S4_5`** (`EledoradoEvent.cpp:808, 853, 898`) is a preprocessor
   slip: `#ifdef` reads only the name, so the "Lorencia line under an #ifdef" kundun-box-sources
   §3.2 calls a test switch is this bug.
3. **MuMain `ZzzOpenData.cpp:2691-2692`**: `case MONSTER_MODEL_GOLDEN_STONE_GOLEM` has no `break`,
   so the Blue Golem (and the Golden Stone Golem) take the Death Rider's play speeds (stand 0.15,
   attack 0.23, walk 0.3, die 0.15). A port that copies speeds per model should know it is MU's.
4. **`MU2/mu.db` is 0 bytes**; the live db is `MU2_BGFX/source/mu.db`.
5. **The WZ clone at `9142efc4-.../scratchpad/wz/` is empty now**; `d7be4edb-.../scratchpad/wz/`
   (and several others) hold the full repo.
6. **WZD's `MonsterSkill.txt`, `MonsterSkillUnit.txt`, `MonsterSkillElement.txt` are 0 bytes**, so
   WebZen's monster-skill data (2005+) is nowhere on disk; the client's `MonsterSkill.bmd` is the
   only skill table.
7. **The experience rate is 20** (`src/sim/rules.h:449`); tarkan-port's "100x" figures are stale.
8. **OM's `Terrain34_Close.att`** (the town alone open) is shipped but referenced by no OM code.

---

## Part B: the data and the look (Aida)

Research notes, 2026-10-11. Nothing tracked was edited, nothing was cooked or written to mu.db, no window was opened. Aida is MU server map **33** (`WD_33AIDA`, `MM/World/MapInfra/MapManager.h:28`). The client ships it as `D/World34` + `D/Object34` (folder = map + 1). Every table key below is **33**; terrain.py is run with folder number **34**.

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- S = `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9b2c4cc5-bd84-45e7-b350-052f3cd63116/scratchpad/aida/b/`

Scratch outputs in S:
- `aida/`: `pipeline/terrain.py D/World34 aida S 34` (height/tiles/attributes/light.png and aida.json; no table rows for map 33 exist, so nothing is flagged hidden). `terrain.log` is the run.
- `obj/`: every Object34 .bmd (Object01-79, plus `centi`, `skullghost`, `tree_eff`) through MuExtract `export-obj` + `export-rig --actions=all`, a .log each. `objinfo.log`: per model type, placements, tris, bones, keys, bbox (cm, y up), scale range, placement height over the ground (dz, m), how many stand on NoGround / walkable tiles, x/y range, sheets.
- `tex/`: every World34/Object34 .OZJ decoded; `tex/ozt/` the .OZT (alpha) sheets apart, because TileGrass01/02, Pool001, ma_001, sunlight2_R and waterfall_R ship as both .OZJ and .OZT under one stem.
- `cmp/`, `cmptex/`: Lorencia, Devias, Noria, Lost Tower, Atlans and Tarkan re-extracted for the colour comparison (§6).
- **Contact sheets:**
  - `sheet_objects.png`: every Object34 model, flat-shaded, texel colour per triangle, magenta = background
  - `sheet_object34_textures.png` and `sheet_object34_ozt.png`: every Object34 sheet; alpha as magenta
  - `sheet_world34.png` (tiles, TerrainLight, leaf02, the `_org` water) and `sheet_world34_ozt.png` (the grass strips, leaf01, rain, the Season minimap); `tiles_big.png` the twelve worn/shipped tile sheets at 256 px
  - `prev_l1_attr_height_light_painted.png`: five panels, row 0 at the bottom: layer 1 by slot (void dimmed) | attributes (grey open, dark red NoMove, black NoGround, green safe) | height | TerrainLight | sheet mean × light (what MU's ground paints)
  - `prev_objects_on_attr.png`: placements over the attribute map, the world walls Object18-20 in orange
  - `light_big.png`: TerrainLight at 3×
- Scripts: `stats.py`, `prev.py`, `look.py` (cross-world colour), `objinfo.py`, `render.py`, `sheet.py`, `dec.py`. Logs: `stats.log`, `look.log`, `objinfo.log`, `tex.log`.

### The one thing to know first

**Aida is a dark, mossy, olive-brown forest of winding clearings, cut through by a NoGround ravine that runs from the entrance to the north-east, its edges walled by 13 m rock walls with waterfalls pouring into the void.** It is the darkest painted ground of any world ported so far that is not a dungeon: walkable sheet × light luminance 29, against Lorencia 46, Noria 49, Tarkan 77 (§6).

- **The light is neutral; the colour is all in the sheets.** TerrainLight walkable mean (138, 137, 139), the same grey-white as Tarkan's. The tile sheets average (65, 59, 40) on walkable ground: dark olive moss, brown hay, grey-green rock.
- **A third of the map is never drawn.** 22,640 tiles (34.5%) are NoGround; MuMain skips them (`MM/Render/Terrain/ZzzLodTerrain.cpp:2178`) and the clear colour shows: Aida has no arm in `SetWorldClearColor`, so **black** (`MM/Scenes/SceneManager.cpp:371-402`), and fog is that same black (`:356-363`). The void sits at the ground's own height (flat 1.95 m), so only the engine's abyss can give it depth.
- **No sky, no birds, no leaves, no fog colour, no weather.** Life is: 3 crawling scarabs (Tarkan's Bug02), butterflies from 20 hidden boxes, waterfall spray, 183 glowing light-flowers (green and red terrain lights with spark sprites), 11 flame boxes, 5 skull-ghost boxes, ~1,676 swaying rigged plants.
- **Season content, not 0.75.** Aida is 2005 data (the w34 wavs are dated July-August 2005) and every MuMain rule for it lives in `MM/World/GameMaps/GMAida.cpp`, a Season module (`M33Aida::`). Pass A owns the dating.

---

### 1. Raw data inventory

#### 1.1 World34 (`D/World34`, 36 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain34.map / .att / .obj | 196610 / 131076 / 112564 | yes | layers, 16-bit attributes, **3752 placements of 74 kinds** |
| EncTerrainTest34.map/.att/.obj | | no | an editor copy; **not** identical (differs from byte 37,611 of the map and 501 of the att). Nothing reads it |
| Terrain.att/.map/.obj, terrainserver.att | | no | plain copies. `terrainserver.att` (3-byte header + 8-bit grid) agrees with the client's walkability on **99.44%**: it opens 370 tiles, all on rows 254-255, and closes none; safe tiles agree 100% |
| TerrainHeight.OZB | 66620 | yes | read at 1080 (header says 1078, as every world) |
| TerrainLight.OZJ | 61953 | yes | 256² baked light, neutral, mean (119, 118, 119) |
| Tile{Grass01,Grass02,Ground01-03,Water01,Wood01,Rock01-07}.OZJ | 12-143 KB | yes (`MM/World/MapInfra/MapManager.cpp:1362-1447`) | all 14 slot names shipped |
| **TileGrass01.OZT** | 65584 | yes, BITMAP_MAPGRASS (`MapManager.cpp:1464-1466`) | 256×64 strip of **dark olive blades**, 36.5% coverage, mean over alpha (77, 74, 31) |
| TileGrass02.OZT | 65584 | yes, MAPGRASS+1 (`:1469-1470`) | **fully transparent** (coverage 0): grows nothing on slot 1 |
| leaf01.OZT / leaf02.OZJ | 1072 / 4445 | loaded (`:1474-1480`), never used (§2) | a 16² pink petal (2% coverage) / a black square |
| rain01/02.OZT | | no | MU loads rain from World1 (`:1482-1494`) |
| tilewater01_org.ozj | 95456 | no | an older water sheet |
| mini_map.OZT, minimap.bmd | | Season UI | 1024² minimap, the void transparent |

terrain.py (`S/terrain.log`, `S/stats.log`):

```
height     0.00 to 3.27 m; drawn ground p5/50/95 1.64/1.95/2.14; walkable 1.60/1.95/2.16; void flat 1.95
tiles      11 slots in use (0-9, 11)
walkable   35.5% (23,254), 0.8% safe (550 tiles, 165 of them walkable)
NoGround   34.5% (22,640) -- undrawn
light      mean [119,118,119]; drawn [112,111,112]; walkable [138,137,139]; safe [171,171,171]; walkable lum p10/50/90 73/130/230
objects    3752 placed, 74 kinds; every placed model is on disk (no "missing" line)
```

**Attributes:**

| value | tiles | meaning |
|---:|---:|---|
| 0 | 23088 | open |
| 1 | 165 | Safe |
| 2 | 1 | |
| 4 | 19314 | NoMove |
| 5 | 328 | Safe+NoMove |
| 8 | 134 | NoGround |
| 12 | 22449 | NoMove+NoGround |
| 13 | 57 | Safe+NoMove+NoGround |

- No Water bit (0x10) anywhere. Height is nearly flat (the walkable p5-p95 spread is 0.56 m); the relief is all in objects.
- **One walkable region of 23,077 tiles** (x 16-243, y 5-242), plus **177 stray open tiles in 7 crumbs on rows 253-255** that nothing reaches (§7 trap 2).
- **The safe zone** is one box at x 67-93, y 0-24 (the map's north edge, "north" = low y): 550 tiles, only 165 walkable (x 74-93, y 5-17, centre 83,10), painted bright (210, 209, 209). Its floor is TileGround01 (119 of 165), TileGrass01 (25), TileWood01 (11). OM's Season 6 gates land here: 119 at (82-87, 8-14), 113 at (76-78, 9-16), and the way back to Noria, 121, is the strip (74, 9-13) (OM `VersionSeasonSix/Gates.cs:265, 267, 535`). Pass A owns the gates.

**Map shape** (`prev_...png`, `prev_objects_on_attr.png`):
- **The ravine:** a NoGround band from beside the entrance (about 80, 22) north-east to about (217, 163), joined to the outer void. Its lips are lined with the world walls Object18-20 (350 placements, 13 m tall, sunk a median 4.7 m) and 40 Object26 waterfalls pouring into it (dz −4.7 m median).
- Two enclosed voids: x 53-184, y 125-199 (2,401 tiles) and x 95-128, y 55-152 (1,089).
- **The west ruins** (x 15-147, y 18-236): the ida_* temple pieces (Object34-41, 50, 51), the brick floor TileRock02 (3,108 drawn tiles, x 31-141), and the darkest, most patterned light: black rings round bright pools at the shrines.
- **The east forest** (x 140-245): green TileGrass01 meadows, the light near-white along the ravine walls.
- **Ponds:** TileWater01 on 2,301 drawn tiles, 1,977 of them NoMove: pools, not paths.

**Tile sheets** (`tiles_big.png`; "drawn" = not NoGround):

| slot | name | px | actually | l1 drawn (% of drawn) | l1 walkable | layer-2 overlays drawn |
|---|---|---:|---|---:|---:|---:|
| 0 | TileGrass01 | 256 | **dark mossy forest floor, green clover and moss** | 17866 (41.6) | 9508 | 0 |
| 1 | TileGrass02 | 256 | grass and straw over red-brown earth | 1981 (4.6) | 593 | 20 |
| 2 | TileGround01 | 256 | **dry brown hay / dead grass** | 13144 (30.6) | 8161 | 901 |
| 3 | TileGround02 | 256 | rounded cobbles in dark moss, a red-brown cast | 939 (2.2) | 898 | 1428 |
| 4 | TileGround03 | 256 | ochre moss with grey lichen | 6 (0.0) | 6 | **3055 (overlay sheet)** |
| 5 | TileWater01 | 256 | green-grey water | 2301 (5.4) | 324 | 0 |
| 6 | TileWood01 | 256 | **the same painting as TileGrass01** (different encoding, md5 differs, same mean 58,59,39) | 2463 (5.7) | 1411 | 2 |
| 7 | TileRock01 | 256 | grey-green cracked rock | 2733 (6.4) | 1530 | 1395 |
| 8 | TileRock02 | 256 | mossy stone brick (the ruins' floor) | 1032 (2.4) | 717 | 3310 |
| 9 | TileRock03 | 256 | pale flagstones in green grass | 2 | 0 | 0 |
| 11 | TileRock05 | 256 | **a red carpet with an ornate border**: a leftover of whatever map Aida was started from. 12,877 layer-1 tiles, 429 drawn, all on the map's edge rows/columns (288 on rows ≥250, 135 on columns ≥250) | 429 (1.0) | 106 | 19 |

Shipped, worn by nothing: TileRock04 (tan flagstones), TileRock06 (128 px black marble with gold corners), TileRock07 (dark brown with a blue/orange border). TileGround03 is effectively an overlay-only sheet.

**Light map** (`light_big.png`):
- A neutral 128-grey base over most of the middle, near-white along the east forest's ravine walls and the safe zone, and **black halos round bright pools** in the west ruins and the north (MU painted the forest's dark edges into the light, not the sheets).
- Colour: **blue-violet flecks** on 490 drawn tiles (B−R > 30, x 86-157, y 92-237), a few red-orange pools (91 tiles, x 108-161, y 79-231), almost no green (8 tiles).
- Hard painting seams, mostly in the void: column 60 and 63 (rows 1-205, 21 drawn tiles), row 1 (x 81-225, 33 drawn), row 192 (x 72-254, 4 drawn). None on walkable ground.

#### 1.2 Object34 (`D/Object34`, 193 files)

79 `ObjectNN.bmd`, plus `centi.bmd` (a centipede), `skullghost.bmd`, `tree_eff.bmd` (the Death Tree's attack, `MapManager.cpp:331-332`), `object01_盔夯.bmd` (a byte copy of Object01's size), and 111 sheets. About a third of the sheets are monsters' and Crywolf's (`cr_*`, `epiboss*`, `forestgost*`, `treemonster*`, `FMammor`, `ma*`, `manto`, `mono3*`). Type = file number − 1.

**No placed model is missing.** Shipped and unplaced: Object02 (aidastone01 rock), 73 (skull), 74 (twlight disc), 75, 77 (the song_k ring statue).

| type | model | placed | tris | rig (keys) | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 0 | Object01 | 2 | 12 | 6 b, **20** | Treeange_R | a 16 m glowing branch ("tree angel"), _R = additive. At (186-201, 116-117) |
| 2 | Object03 | 97 | 35 | 1 | Atree01 | a 5 m thin bare trunk |
| 3 | Object04 | 131 | 688 | 1 | Atree02 | **dead twisted tree**, 5.6 m |
| **4** | **Object05** | **171** | 96 | 10 b, 6 | Pool001.tga | **fern** clumps, 3 m wide, swaying |
| 5 | Object06 | 80 | 16 | 7 b, 11 | Pool002.tga | tall grass/reed cards, 2 m |
| **6, 24** | **Object07, Object25** | **427 / 488** | 16 | 7 b, 9 | Pool003.tga / Pool004.tga | **shrub-mass cards**: one flat 2.6 m leafy square, tilted (60% / 87% have an X angle), placed **3.3 / 3.9 m up** (abs. z 3.5-7.7 m), clustered on each other and on the big rocks Object09/10 and statues. **Foliage draped over the rock walls**, the forest's hedges. 164 of Object25 stand over NoGround |
| 7 | Object08 | 4 | 126 | 1 | qwq | a mossy rock heap, (188-197, 213-218) |
| 8, 9 | Object09, 10 | 168 / 235 | 186 / 100 | 1 | qw | **big mossy rock masses**, 5 m |
| 10 | Object11 | 8 | 836 | 6 b, 1 | milepost, milepostleaf | a carved milestone tree with yellow leaves |
| 11-13 | Object12-14 | 14 / 11 / 22 | 366-803 | 1-2 | qwwood, qww, qwwa, qwwoodna, branch01_line | **stumps and root tangles** |
| 14 | Object15 | 106 | 95 | 1 | woodba | a 7.5 m tree trunk |
| 15 | Object16 | 56 | 8 | 1 | branch01_line.tga | **hanging vines**, 1 m cards, dz +3.4 m: hung from trees, not on the ground |
| 16 | Object17 | 7 | 1280 | 1 | boss, boss01, boss02 | a large shrine of roots and stone, 4.8 m |
| **17-19** | **Object18-20** | **131 / 132 / 87** | 190-198 | 1 | aidaworldwallC, aidaworldwallCunder | **the world walls**: 4.8 × 13 m rock faces, scale 0.80-0.88, **sunk 4.6-4.8 m**. The edge of every void and the ravine |
| 20-22 | Object21-23 | 17 / 36 / 24 | 63-74 | 1 | aidarockA | boulders, some sunk 8-13 m (in the void) |
| 23 | Object24 | 85 | 49 | 13 b, 7 | Toran.tga | big taro-leaf plant, swaying |
| **25, 28** | **Object26, Object29** | 40 / 4 | 80 | 7 b, 15 / 1 | waterfall_R | **waterfalls**, 2.3 m strips at scale 2.8-5.7 (6.5-13 m), dz −4.7/−6.2 m: they fall from the lips into the void. _R = additive; V slid (§2) |
| 26, 27, 29 | Object27, 28, 30 | 11 / 21 / 17 | 32 | 8 b, 13 | aidaflower.tga | flat 3 m **flower-bed cards** (pink, blue, white flowers), dz +1.1 m in the OBJ's pose (check posed before grounding) |
| **30, 71** | **Object31, Object72** | **155 / 28** | 142 | 18 b, 13 | aidalightflower + flowerlight_R / flowerlight2_R | **light-flowers**: drooping stems with glowing bulbs; green / red terrain light and spark sprites (§2) |
| 31, 44 | Object32, 45 | 6 / 6 | 542 | 1 | GirlDongSang | a girl statue with wings |
| 32 | Object33 | 13 | 312 | 1 | boss01 | a fallen dead-wood statue |
| 33-40 | Object34-41 | 55 / 10 / 40 / 14 / 3 / 9 / 2 / 7 | 70-1717 | 1-2 | ida_*, qww, qwwood, boss01, branch01_line | **the ruins**: shrine houses and pillars. Object35-41 are built tiny and placed at **scale 6-16** |
| **41** | **Object42** | 21 | 2 | 1 | sunlight2_R | **sunbeam planes** 8.7 × 3 m, dz +2.4 m, additive at alpha 0.5 (§2) |
| 42, 43 | Object43, 44 | 198 / 27 | 75 / 150 | 1 | ida_st01 | carved stone outcrops / stone tables |
| 45-48 | Object46-49 | 39 / 31 / 22 / 9 | 36-163 | 1 | Myobi01-04 | **gravestones and Celtic crosses** |
| 49, 50 | Object50, 51 | 6 / 3 | 634-690 | 1 | ida_*, qww | ruined temple blocks |
| 51, 54, 55 | Object52, 55, 56 | 2 / 6 / 3 | 76-368 | 1-2 (Object55: 6 keys) | Guan01, mangbon | **coffins**; Object55's lid moves |
| 52, 53 | Object53, 54 | 44 / 19 | 82 | 17 b, 6-7 | plant2a / plant3a .tga | broad-leaf plants, swaying |
| **56-59, 62, 63, 67** | Object57-60, 63, 64, 68 | 22/23/39/20/47/24/11 | 12 | 1 | BosBB | **hidden emitter boxes** (§2) |
| **60** | Object61 | 20 | 12 | 1 | BosBB | **hidden butterfly emitter** |
| 61 | Object62 | 6 | 24 | 1 | AidaWeapon | a sword stuck in the ground, 1.9 m |
| **64** | **Object65** | 65 | 160 | 13 b, 12 | Atree02 | a twisted dead sapling, played at Velocity 0.05 (§2) |
| **65, 66** | Object66, 67 | 3 / 8 | 200 | 1 | snake | a green scaled **serpent body** coiled on the ground; mesh 0 streamed (§2) |
| 68, 69 | Object69, 70 | 21 / 11 | 256 / 431 | 1 | mangbon | **skeletons**, lying and sitting |
| **70** | Object71 | 5 | 80 | 1 | skull_R | **hidden skull-ghost emitter** |
| 75 | Object76 | 1 | 314 | 6 b, 10 | song_k202w1, song_k3metal0103 | a ring statue with a flare at bone 4, at (82, 20) by the entrance |
| **77, 78** | Object78, 79 | 10 / 6 | 200 | 8 b, **20** | snake02 | a brown **serpent**, animated, mesh 0 streamed (§2) |

**Placement counts, most first:** Object25 488, Object07 427, Object10 235, Object43 198, Object05 171, Object09 168, Object31 155, Object19 132, Object04 131, Object18 131, Object15 106, Object03 97, Object20 87, Object24 85, Object06 80, Object65 65, Object16 56, Object34 55, Object63 47, Object53 44; everything else 40 or fewer.

**Rigged placements: 1,676** (`S/objinfo.log`): Object25 488, Object07 427, Object05 171, Object31 155, Object24 85, Object06 80, Object65 65, Object53 44, Object26 40, Object72 28, Object28 21, Object54 19, Object30 17, Object27 11, Object78 10, Object79 6, Object55 6, Object01 2, Object76 1. About Atlans's ~2000 and two and a half times Tarkan's 662. The bulk (915) is two 16-tri cards on one 9-key clip, so a shared phase per kind is enough.

**Kinds by role** (for the recipe order):
- grass/tufts and ground plants: Object05, 06, 24, 27/28/30, 53, 54 (alpha cards)
- foliage masses: Object07, 25 (alpha cards, high up); hanging vines Object16
- trees and wood: Object03, 04, 11, 12-14, 15, 65; the root shrine 17
- rocks and walls: Object08, 09, 10, 18-20 (walls), 21-23, 43, 44
- water: Object26, 29 (waterfalls); the ponds are terrain
- ruins and graves: Object32-41, 45-52, 55, 56, 62, 69, 70
- lights: Object01, 31, 42, 72, 76
- no roofs.

**Bug02** comes from `D/Object9/Bug.bmd` #2 (`MapManager.cpp:333-334`); `source/world/tarkan/Bug02.*` already holds it. Butterfly01 comes from `D/Object1` (`:329-330`).

---

### 2. MuMain's special cases for WD_33AIDA

`WD_33AIDA` appears 7 times outside sound tables; the rest of Aida's rules run through `M33Aida::IsInAida()`, which is **true in Aida and in the old character scene, WD_54** (`MM/World/GameMaps/GMAida.cpp:18-21`). MU2_BGFX's character scene is world 74, which this does not touch. Hooks: CreateObject `MM/Engine/Object/ZzzObject.cpp:4818`, MoveObject `:4238`, RenderObjectVisual `:3280`, RenderObjectMesh `:1151`.

**Frame, light, fog, sky**
1. **Clear and fog: black**, the default arm (`MM/Scenes/SceneManager.cpp:371-402`; fog colour = clear colour, `:356-363`). No sky.
2. **NoGround is undrawn** (`ZzzLodTerrain.cpp:2178`): 34.5% of the map.
3. **No birds**: the boid spawn list omits Aida (`MM/Engine/AI/GOBoid.cpp:1303-1312`).
4. **No leaves in effect.** `RequireLeavesEffect` lists Aida (`MM/Scenes/MainScene.cpp:89`), but Aida's creator `M33Aida::CreateMist` returns false on every path (`GMAida.cpp:1563-1570`; called `MM/Render/Effects/ZzzEffectFireLeave.cpp:491`), and no other creator claims the slot. The mist was stubbed out; `leaf01.OZT` (a pink petal) ships unused. `IsInAidaSection2` (`GMAida.cpp:23-28`, x 54.5-178.2, y 67.8-224.2 tiles) is read only there.
5. **Terrain lights** (`AddTerrainLight`, MoveAidaObject, per frame):
   - type 30, **Object31 light-flowers ×155: green** (0.4-0.44, 0.6-0.64, 0.4-0.44), a random 0-0.04 flicker each frame, radius 2 tiles (`GMAida.cpp:58-64`)
   - type 71, **Object72 light-flowers ×28: red** (0.9-0.94, 0.2-0.24, 0.2-0.24), radius 2 (`:65-71`)
   - the bosses add their own: Witch Queen red (0.7, 0.1, 0.1) r3, the golems blue (0, 0, 0.7) r3, Hell Maine red (1, 0, 0) r3 (`:485-523`; pass A/C)

**Terrain**
6. **Water:** slot 5 TileWater01 takes the water flag in every world (`ZzzLodTerrain.cpp:1948-1949`) and slides one sheet in **20 s**, the default (`:3583-3597`, no Aida arm).
7. **Grass:** grows on layer-1 slot N from `TileGrassN+1.tga` where all four corner alphas are 0 (`:2077-2120`), not in Atlans or the castles (`:364-366`, `:3619`). Aida: **slot 0 grows TileGrass01.OZT's olive blades on 14,473 drawn tiles** (6,547 walkable); slot 1's TileGrass02.OZT is empty, so its 1,478 grass-able tiles grow nothing; slot 2 has no TileGrass03. Wind is the default `sin(WindSpeed + x·5)·WindScale` (`:3300-3303`), a rolling wave, not Tarkan's ×50 jitter.
8. No caustics, no lava, no Water attribute, no special slot. `CreateWaterTerrain` is not Aida's.

**Model sheets that are additive without a BlendMesh.** `CreateAidaObject` is empty (`GMAida.cpp:30-36`) and MoveAidaObject sets no BlendMesh. What adds is MU's TextureScript: a sheet name with `_R` after its underscore is bright (`MM/Render/Sprites/TextureScript.cpp:16-58`), and RenderBody makes that mesh the blend mesh (`MM/Render/Models/ZzzBMD.cpp:2236-2242`). Placed meshes on `_R` sheets: **Object01 (Treeange_R), Object26/29 (waterfall_R), Object31 (flowerlight_R), Object72 (flowerlight2_R), Object42 (sunlight2_R)**, and the hidden Object71 (skull_R). So `BLEND_MESH_BY_MAP[33]` stays empty; the recipes mark these sheets added, as Tarkan's `chogatea02_R`.

**MoveAidaObject** (`GMAida.cpp:38-95`):
9. **25, 28 (Object26, 29, waterfalls):** `BlendMeshTexCoordV -= 0.015` a frame (`:48-57`). The _R mesh is the blend mesh, so it slides: at MU's 25 reference frames a second, **0.375 sheet a second** downwards (house rule: per-frame rates ×25).
10. **41 (Object42, sunbeams):** Alpha 0.5 (`:72-76`), and RenderAidaObjectVisual draws mesh 0 again as RENDER_TEXTURE|RENDER_BRIGHT at BodyLight 0.52 (`:151-165`). Two added passes of a half-alpha beam.
11. **56, 57, 58, 59, 62, 63, 67, 70: `HiddenMesh = -2`** every frame (`:77-86`). **60** hides itself in RenderAidaObjectVisual (`:195-204`). That is HIDDEN_BY_MAP[33].
12. **64 (Object65, the dead sapling):** `Velocity = 0.05` (`:87-89`): its 12-key clip plays slowly.
13. `PlayBuffer(SOUND_AIDA_AMBIENT)` from every object's move, every frame (`:92`): the ambient loop is simply always on.

**RenderAidaObjectVisual** (`GMAida.cpp:97-320`). RenderObjectVisual runs for every visible placement whatever its HiddenMesh (`ZzzObject.cpp:3445-3459`; only RenderObject is gated, `:423`).
14. **30 (Object31):** six BITMAP_SPARK+1 sprite pairs at bones 6, 7, 8, 12, 13, 17: a blue glint (0.1, 0.1, 0.3) at 4-5.5 plus a grey (0.15) core at 3 (`:106-150`). **71 (Object72):** the same bones in red, (0.25-0.5, 0, 0) at 5-6 plus (0.15, 0, 0) at 3 (`:253-297`).
15. **56 (Object57 ×22):** BITMAP_WATERFALL_1 subtype 2, 1 frame in 2 (`:166-172`); **57 (Object58 ×23):** BITMAP_WATERFALL_3 subtype 4 every frame (`:173-176`); **58 (Object59 ×39):** BITMAP_WATERFALL_2 subtype 2, 1 in 3 (`:177-183`). The **waterfall spray and mist**, standing with the Object26 falls (81 of the 84 boxes stand within 2 m of an Object26 fall). Particle arms: `MM/Render/Effects/ZzzEffectParticle.cpp:3383, 3499, 3578`; sheets `Effect/WATERFALL1-3.jpg` (`MM/Engine/Object/ZzzOpenData.cpp:5330-5332`).
16. **59, 62, 63 (Object60 ×20, Object63 ×47, Object64 ×24):** 20 BITMAP_CLOUD subtype 1 a frame, tinted bluish (0.01, 0.03, 0.05), grey (0.05) and dull red (0.05, 0.02, 0.02) (`:184-194, 205-226`) **but only `if (HiddenMesh != -2)`, which MoveAidaObject sets every frame first**. As written they never emit. They are where MU meant mist: **43 of the 47 grey boxes stand in the void, sunk 1.8 m** (the ravine), the red ones over the ground at x 108-155, y 41-113. A record of intent for our void clouds (§5), not MU's frame.
17. **60 (Object61 ×20):** 1 frame in 25, a MODEL_BUTTERFLY01 effect subtype 3 (`:195-204`; `MM/Render/Effects/ZzzEffect.cpp:596`): **butterflies**.
18. **65, 66 (Object66, 67):** mesh 0 drawn a second time as StreamMesh (unlit, BodyLight) with U `−(WT % 100000)·0.00005`: a sheet in 20 s (`:227-238`). **77, 78 (Object78, 79):** the same at `(WT % 100000)·0.0002`, a sheet in 5 s (`:308-318`). The serpents' scales crawl.
19. **67 (Object68 ×11):** BITMAP_FLAME subtype 7 every frame at light 0.3 (`:239-242`): **small fires** (x 81-204, y 18-205, dz +2.5 m median: on rocks or ruins).
20. **70 (Object71 ×5):** in the first 30 ms of every 1024 ms, 1 in 5, a MODEL_GHOST at light (0.1, 0.1, 0.1) (`:243-252`; `ZzzEffect.cpp:572-588`): a **drifting skull ghost**, rare.
21. **75 (Object76):** a white BITMAP_FLARE at bone 4, size 3, spinning (`:298-307`).

**Floor crawlers: the scarabs** (`GOBoid.cpp`, MoveFishs)
22. Aida is **not** exempt from the 3-slot cap (`:1668-1675`), so **3** at a time. Spawn within ±5.12 m on a tile whose wall is 0 or TW_CHARACTER (`:1691`). Values shared with Tarkan (`:1726-1733`): `MODEL_BUG01 + 1` (Bug02), scale 0.8-1.1, Velocity 2.5/scale, Gravity 9, LifeTime 100, a BITMAP_JOINT_ENERGY subtype 4 trail. Turn back on NoMove/NoGround (`:1830-1841`). MU2_BGFX: `CRAWLS["aida"]` and a `CrawlRow` like Tarkan's but capped at 3.

**Ambient and music**
23. `SOUND_AIDA_AMBIENT` = `Data/Sound/w34/aida_ambi.wav` (893,040 B), looped (`MapManager.cpp:338`; played `GMAida.cpp:92`). Not in `source/sounds`.
24. `MUSIC_BC_ADIA` = `data/music/Aida.mp3` (2,988,639 B), always on the map (`SceneManager.cpp:1089-1094`; `MM/Core/Globals/_enum.h:186`). House rule: music is rare and in fights.
25. 31 monster wavs in `Sound/w34` (`MapManager.cpp:339-369`): pass A/C.

**Other**
26. Town portal works (`MM/World/MapInfra/PortalMgr.cpp:55`). Map name `MapManager.cpp:1687-1690`.
27. No Aida arm in ZzzCharacter for mounts, swimming or heights; no arm in ZzzLodTerrain.

---

### 3. The pipeline route: step 1, bare land

Chain as Tarkan's. From MU2_BGFX/, `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. tables first. Keys are map number 33 (terrain.py is run with folder number 34).
#    pipeline/terrain.py
#      HIDDEN_BY_MAP[33]     = {56, 57, 58, 59, 60, 62, 63, 67, 70}
#          # Aida's (MoveAidaObject, GMAida.cpp:77-86; 60 in RenderAidaObjectVisual :195-204):
#          # 56/57/58 waterfall spray boxes, 59/62/63 mist boxes (dead in MuMain, :184-226),
#          # 60 butterflies, 67 small fires, 70 the skull ghost. 211 placements, all emitters.
#      GRASS_BY_MAP[33]      = ["TileGrass01"]
#          # MU's olive blades on slot 0 (ZzzLodTerrain.cpp:2077-2120); TileGrass02.OZT is empty.
#      VOID_BY_MAP[33]       = {"start": 0.5, "depth": 4.5}
#          # Devias's way, Tarkan's shape: MU's cut, the world walls Object18-20 (13 m, sunk
#          # 4.7 m) and the waterfalls hanging into the dark. Deeper than Tarkan's 3.0 because
#          # the walls and falls reach 4.7-6 m down.
#      VOID_FILL_BY_MAP[33]  = [(83, 10)]
#          # from the safe zone: the 177 open tiles in 7 crumbs on rows 253-255 that no walk
#          # reaches become void (checked: exactly 177). Ours, marked.
#      BLEND_MESH_BY_MAP[33] : none. Aida sets no BlendMesh; its glows are _R sheets
#                              (TextureScript.cpp:16-58), marked additive in the recipes.
#      OPERABLE_BY_MAP[33]   : none (CreateAidaObject is empty, GMAida.cpp:30-36; no MOVEMENT_OPERATE arm).
#      WATER_FLOW_BY_MAP[33] : none. MU slides every pond one way, a sheet in 20 s.
#      LIGHT_DEPTH / LIGHT_CHROMA / FIGURE_LIGHT / SKY / OPEN / FLOOR / LAVA_SPILL: none to start.
#    pipeline/index.py
#      GATE_BOXES_BY_MAP[33] = [(74, 9, 74, 13), (76, 9, 78, 16), (82, 8, 87, 14), (186, 173, 190, 177)]
#          # OM VersionSeasonSix/Gates.cs:535 (enter 121 -> Noria 122), :267 (exit 113, from Noria),
#          # :265 (warp 119 Aida1), :266 (warp 140 Aida2). 339/336 (237,166-168) lead to map 80: pass A.
#      GATE_BOXES_BY_MAP[3] += [(220, 30, 226, 30), (220, 31, 226, 34)]   # Noria's side, Gates.cs:534, 170
#      OPERABLE_BY_MAP[33]   : none
python3 pipeline/terrain.py $D/World34 aida source/world 34
#    expect: 0.00-3.27 m, 11 slots (0-9, 11), 35.5% walkable (35.4% after VOID_FILL), 0.8% safe,
#            3752 placed / 74 kinds, no missing line.
for n in TileGrass01 TileGrass02 TileGround01 TileGround02 TileGround03 TileWater01 TileWood01 \
         TileRock01 TileRock02 TileRock03 TileRock05; do
  python3 pipeline/decode_texture.py $D/World34/$n.OZJ source/textures/ai_$(echo $n | tr A-Z a-z).png; done
python3 pipeline/decode_texture.py $D/World34/TileGrass01.OZT source/effects/grass/aida_TileGrass01.png  # MU's blades, if used
#    source/world/aida/ground.json on Tarkan's shape, sheets -> ai_*, materials below
./tools/content.sh --world aida && ./tools/sync.sh --world aida --only-world && ./tools/sync.sh
./tools/cook.py --world aida --only ground && ./tools/cook.py --world aida --only tables
#    then index.py (house rule), and every world's tables after new items
```

**Sheet prefix: `ai_`.** Nothing in `source/textures` (1,072 files) uses it, and no world folder is called `aida`.

Name collisions:
- **with sheets already in `source/textures`:** tilegrass01, tilegrass02, tileground01, tileground02, tileground03, tilerock01, tilerock02, tilewater01, tilewood01 (exact, unprefixed names).
- **with other MuMain worlds' sheets (future ports):** Crywolf's Object35 shares about 60 Aida names (aidaflower, aidalightflower(_R), aidarockA, aidastone01, aidaworldwallC(under), atree01/02, boss/01/02, branch01_line, flowerlight_R, girldongsang, ida, ida_01…ida_12, ida_st01, leaves, milepost(leaf), plant2a/3a, pool001-004, qw*, sunlight2(_R), toran, waterfall(_R), woodba, …); mangbon in Object42/43/57/64/67; atree02 and branch01_line in Object42/43/57/64; guan01 and myobi01-04 in Object57; bosbb in Object25.
- **generic stems to keep prefixed:** `boss`, `ida`, `it`, `ma`, `root`, `leaves`, `snake`, `waterfall`, `sunlight2`, `branch`.
- **.OZJ/.OZT twins under one stem:** TileGrass01, TileGrass02 (World34), Pool001, ma_001, sunlight2_R, waterfall_R (Object34). The decoder must name each twin apart (Tarkan's first-run trap). The models name: Pool001.**tga**, waterfall_R.**jpg**, sunlight2_R.**jpg**.
- `_org` copies (Pool004_org, aidaflower_org, ida_st01_org, tilewater01_org) are unused; skip them.

**ground.json `sheet_materials` (proposed; read off the art, the names lie):**

| slot | sheet | material | note |
|---|---|---|---|
| 0 | TileGrass01 | grass | dark moss and clover, 41.6% of drawn |
| 1 | TileGrass02 | grass | |
| 2 | TileGround01 | grass | dry hay; grows nothing (GRASS_BY_MAP pins growth to slot 0) |
| 3 | TileGround02 | cobble | |
| 4 | TileGround03 | grass | moss, an overlay sheet (3,055 overlays) |
| 5 | TileWater01 | water | ponds |
| 6 | TileWood01 | grass | the same painting as TileGrass01 |
| 7 | TileRock01 | rock | |
| 8 | TileRock02 | flagstone | the ruins' brick |
| 9 | TileRock03 | flagstone | 2 tiles |
| 11 | TileRock05 | — | **the red carpet: map `TileRock05` to `ai_tileground01.png` (ours, marked)**: 323 drawn NoMove carpet tiles stay on the map's edge after VOID_FILL, and at 16:9 a red strip beyond the walls would show |

Each with its `_over` twin, as Tarkan's.

---

### 4. What differs from the worlds already built

1. **The walls are tall and deep.** Object18-20 (350) are 11 m high at their scale and sunk 4.6-4.8 m, at every void lip. The buried test (`FLAT_OVER_VOID`, tools/cook.py) must take `aida`: Object25 (164 animated cards over void), the waterfalls (20 over void) and the walls reach well under the void's height.
2. **Do not ground the high foliage.** Object07/25 (915 cards, 3.3-3.9 m up on rocks), Object16 (56 vines, +3.4 m) and the sunbeams Object42 (+2.4 m) are meant to be up. Tarkan's `GROUNDED_TYPES` must not be copied. The flower beds Object27/28/30 read +1.1 m in the OBJ's pose; check a posed bake before deciding (their root bone is at the origin, the cards are not).
3. **Hidden types survive as markers / anchors:** spray 56/57/58 (84), butterflies 60 (20), fires 67 (11), ghosts 70 (5); the mist boxes 59/62/63 (91) are MU's dead intent, kept for void clouds.
4. **Lights: 183 small flickering lamps** (155 green, 28 red, radius 2 tiles), plus spark sprites on their bulbs and 11 small fires. Noria's "reduce sun so we see the light emitters" applies.
5. **Additive or streaming kinds:** Object01, 26/29 (V slide 0.375/s), 31/72, 42 (two half-alpha passes) via `_R`; Object66/67 and 78/79 streamed (U 1/20 s and 1/5 s) as a second unlit pass, Tarkan's `stream` glow entry.
6. **Rigged placements 1,676**, Object65 at Velocity 0.05.
7. **Scarabs** capped at 3 (Tarkan has 10).
8. **Recipe priority** (scenery first, walls structural): Object18-20 walls (350), Object25 + 07 foliage (915), Object10 + 09 rocks (403), Object43 (198), Object05 ferns (171), Object31 light-flowers (155), Object04 trees (131), Object15 trunks (106), Object03 (97), Object24 (85), Object06 (80), Object65 (65), Object16 vines (56), Object34 ruins (55); then the waterfalls and their spray, the ruins and graves, the statues, the serpents.

---

### 5. The void

MU: black cut, black fog, walls and falls hanging into it. Options:
- **A. Devias's way** (recommended to start, as the user chose it for Tarkan): MU's cut and the default abyss, `VOID_BY_MAP[33] = {"start": 0.5, "depth": 4.5}`, the walls and waterfalls going dark down their length.
- **B. A + the ravine mist (ours, MU's intent):** Tarkan's `shallow_` void clouds in a cool green-grey, only where the cloud lies wholly over void. MU itself placed 43 grey mist boxes sunk 1.8 m in the ravine (§2.16) and never let them emit. The waterfalls falling into mist rather than black would read best.
- Blood Castle's `rim` + `blend` slopes the lips and would hide the walls; not recommended.

---

### 6. A look direction

**MU's paint, measured** (`S/look.py`, walkable tiles; sheet means blended by the alpha layer, times TerrainLight):

| world | walk light | walk sheet mean | sheet × light | lum p50 | hue / sat |
|---|---|---|---|---:|---|
| Lorencia | (160, 160, 161) | (92, 81, 32) | (58, 51, 20) | 46 | 49° / 0.65 |
| Devias | (140, 179, 204) | (203, 214, 217) | (110, 149, 174) | 148 | 203° / 0.36 |
| Noria | (181, 181, 181) | (82, 74, 36) | (58, 52, 25) | 49 | 50° / 0.56 |
| Lost Tower | (94, 84, 86) | (65, 62, 61) | (24, 21, 21) | 19 | 2° / 0.14 |
| Atlans | (34, 104, 139) | (158, 152, 136) | (21, 62, 74) | 55 | 193° / 0.71 |
| Tarkan | (138, 135, 135) | (169, 149, 113) | (91, 79, 60) | 77 | 36° / 0.34 |
| **Aida** | **(138, 137, 139)** | **(65, 59, 40)** | **(35, 32, 22)** | **29** | **46° / 0.37** |

- Aida's ground is **Lorencia's and Noria's hue** (olive-gold, 46°) at **60% of their brightness and two-thirds of their saturation**. The light is neutral, so the gloom and the olive are in the sheets.
- MU's grass strip is olive (77, 74, 31); its plants are dark (Pool003 (80, 84, 50), Pool004 (52, 50, 34), Pool001 (53, 60, 13)). The glowing accents are blue sparks, green and red flower lights, white sunbeams.

**It should read as an old, deep, mossy forest by day: dim, green-gold under a canopy, with shafts of light and glowing flowers.** Not a night map: the house sun is 52°, and the paint is dark enough without taking the light down too.

**What the sheet must carry:**
- **The sun at 52°, softer than Noria's** (a canopy forest): pale warm-gold, strength ~3.6 against Noria's 5.0, so the 183 flower lamps and the beams read.
- **Fill from a green-grey canopy**, a little stronger than Tarkan's (0.7), a mossy ground bounce, so the shade is deep green-brown, not black.
- **Exposure up** to carry the dark albedo (lum 29 against Noria's 49): about 1.5 against Noria's 1.32. Expect a fight between lifting the floor and washing the blacks.
- **Haze: a forest mist, faint.** MU has no fog colour here, but it placed mist boxes (§2.16). A cool green-grey at 0.0025, under Noria's 0.0016-0.004 range's top ("not foggy").
- **Grass must read lush** (house rule): MU's slot 0 is 42% of the drawn floor and 14,473 grass-able tiles, so this is the meadow world after Noria. Dense, a little taller, olive-green with variation, no tropical accents (that is Noria's identity).
- **Grade:** per "sepia is a hue problem", the warmth goes in the light tint and the shade leans a touch green-teal; saturation about 1.05, since the sheets are under-saturated.
- **Water:** MU slides the ponds a sheet in 20 s. Noria's precedent ("a puddle reads as a river in a hole"): hold still, `water_flow 0`.

**Proposed starting `sheets/worlds/aida.json`** (shape from noria.json and tarkan.json; every value is invention for the first shots):

```json
{
  "note": "Aida's light, laid over lighting.json while a character stands in Aida (game/world/maps.h). MU draws it on its default black clear and fog (SceneManager.cpp:371-402), a third of the map NoGround and undrawn (ZzzLodTerrain.cpp:2178), the ground lit by a neutral TerrainLight (walkable mean 138, 137, 139) over the darkest forest sheets ported (walkable sheet x light luminance 29 against Noria's 49, Lorencia's hue at two-thirds the saturation), with 183 flickering green and red flower lights (GMAida.cpp:58-71), sunbeams and waterfalls. So: an old mossy forest by day, the house 52-degree sun softer than Noria's so the flower lamps read, a green-grey canopy fill and a mossy bounce so the shade stays green-brown, exposure up to carry the dark paint, a faint cool forest mist (MU placed mist boxes in the ravine and never let them emit, GMAida.cpp:184-226), the grade warm in the light and a touch green in the shade, the grass lush and olive. Ponds held still (Noria's rule). Invention on MU's look; first cut 2026-10-11 from docs/aida-port.md B 6, unshot.",
  "elevation": 52.0,
  "sun_colour": [1.0, 0.92, 0.74],
  "sun_strength": 3.6,
  "sky_colour": [0.34, 0.44, 0.42],
  "horizon_paleness": 0.7,
  "ground_colour": [0.12, 0.13, 0.08],
  "ambient_strength": 0.7,
  "exposure": 1.5,
  "dust_colour": [0.62, 0.7, 0.64],
  "dust_density": 0.0025,
  "lamp_strength": 3.14159,
  "glow_strength": 1.3,
  "bloom_threshold": 0.95,
  "bloom_strength": 0.35,
  "contrast": 0.5,
  "saturation": 1.05,
  "split": 0.4,
  "tint_low": [0.95, 1.0, 0.98],
  "tint_high": [1.1, 1.04, 0.84],
  "grass_colour": 0.4,
  "grass_root_colour": [0.16, 0.3, 0.12],
  "grass_tip_colour": [0.6, 0.86, 0.32],
  "grass_dry": 0.15,
  "grass_vary": 1.2,
  "grass_meadow": 0.0,
  "grass_height": 0.45,
  "grass_density": 0.7,
  "water_flow": 0.0,
  "water_sheen": 0.5
}
```

Expect the first-shot fights to be:
1. the floor too dark or the blacks washed (exposure against contrast)
2. the black void and the 13 m walls standing in it
3. whether the flower lamps read at day
4. olive versus green: the user called Noria's three stacked greens "too green"; Aida's paint is olive and should stay so.

**GRASS_BY_MAP[33] = ["TileGrass01"]** (MU's rule). The lushness lever, if the user wants more: add `"TileWood01"` (the same painting, 2,463 drawn tiles; ours, marked). Do not add TileGround01 (hay) or TileGrass02 (MU's strip there is empty).

---

### 7. Traps

1. **Map 33, folder 34.** Every table is keyed 33; terrain.py's fourth argument is 34. `M33Aida` is also active in the old character scene (WD_54); our charscene is 74 and not affected.
2. **Stray tiles on rows 253-255:** 177 open, unreachable tiles in 7 crumbs. Without `VOID_FILL_BY_MAP[33]` the walker counts them and they draw as ground beyond the wall.
3. **The red carpet** (slot 11, TileRock05) is drawn on 429 edge tiles (323 after the fill). Map its sheet to the forest floor, or a red strip shows past the walls at 16:9.
4. **TileWood01 is not wood**, TileGround01 is not ground, TileGrass02's grass strip is empty: materials by the art (§3).
5. **Twins under one stem** (TileGrass01/02, Pool001, ma_001, sunlight2_R, waterfall_R): decode .OZT into its own folder or name.
6. **The `_R` sheets are the glow**, not a BlendMesh: if the recipes miss them, the waterfalls, beams and flower bulbs come out as opaque grey planes.
7. **Do not ground** Object07/25, Object16, Object42 (§4.2). Do add `aida` to `FLAT_OVER_VOID`.
8. **The mist boxes 59/62/63 are dead in MuMain** (gated on a HiddenMesh MoveAidaObject sets first). Building them as MU's emitters would be ours, not MU's.
9. **Per-frame rates:** the waterfall's 0.015 a frame is 0.375 sheets a second at MU's 25 Hz reference.
10. **Light seams** at columns 60/63 and rows 1/192 are almost all over void; a `light_depth` above the default would bring the 54 drawn seam tiles forward.
11. **Object35-41 are placed at scale 6-16** (built tiny): a recipe's size check must expect it.
12. **Bug02 lives in Object9**: reuse `source/world/tarkan/Bug02.*`, do not re-export from Object34 (it is not there).
13. `EncTerrainTest34.*` differ from the real files. Use `EncTerrain34.*` (terrain.py does).

### 8. Decisions for the user (look and data only)

1. **The void:** A (Devias's cut and abyss) or B (A plus a cool mist in the ravine, which MU placed and never drew).
2. **Grass:** MU's slot 0 alone, or slot 0 + TileWood01 for more meadow.
3. **Ponds:** held still (Noria) or MU's 20 s slide.
4. **The red edge carpet:** remapped to forest floor (recommended) or left as MU's.
5. **Music:** Aida.mp3 in fights only, or not at all; aida_ambi.wav as the bed.

---

## Part C: the engine side (Aida)

Written 2026-10-11, read-only: no repo file was edited, nothing was cooked, mu.db was read with `?mode=ro&immutable=1`, no window was opened and nothing was stashed. Paths are relative to `MU2_BGFX/` unless marked. Abbreviations, as in tarkan-port.md Part C:
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`;
- MM = `LEGACY/reference/MuMain/src/source/`;
- D = `LEGACY/reference/MuMain/src/bin/Data`.

Line numbers are HEAD `390d0e7c` plus the working tree as found. Every file a step below touches was clean in `git status` (`pipeline/`, `tools/cook.py`, `src/sim/gates.cpp`, `src/sim/maps.cpp`, `src/game/ui/{minimap,arrival}.cpp`, `src/game/play_{open,sound}.cpp`, `src/app/modes/play_mode.cpp`, `src/game/world/`, `sheets/`, `source/sounds/`). Other sessions hold uncommitted edits in `server/src/{main,store}.cpp`, `src/sim/{realm,rules}.cpp`, `src/sim/{realm,skills}.h`, `src/app/modes/play_mode.h` and `src/game/{remote_link,ui/bag,ui/tip}.cpp`. Read `git diff` before staging any of those (memory: another session edits the same files; commit through a private index).

Scratch (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9b2c4cc5-bd84-45e7-b350-052f3cd63116/scratchpad/aida/c/`):
- `world/aida/`: a dry run of `pipeline/terrain.py` on World34, scratch only;
- `grid.py` and `grid.txt`: the grid facts below;
- `mm_aida.txt`: every `WD_33AIDA`, `M33Aida` and `AIDA` line in MuMain (154).

**Brief correction:** `MU2/mu.db` is a 0-byte file, and so is `MU2_BGFX/mu.db`. The live database is `MU2_BGFX/source/mu.db` (143 kB, WAL 0 bytes, `-shm` touched 00:26 today, so another session has it open). Everything below was read from that file.

---

### 0. The short version

1. **Aida is map 33 to the server and the realm, and World34/Object34 to the pipeline.** terrain.py is run with folder number **34**, and its per-map tables are keyed **33** (`map_number - 1`, `pipeline/terrain.py:663`, `HIDDEN_BY_MAP.get(number - 1)` `:635`). The world name is `aida`.
2. **It joins a world we already have.** Its only way in is Noria's gate 120, a north-east door at 220,30-226,30 asking level 130, to Aida's exit 113. The way back is Aida's gate 121 to Noria's exit 122 (`OM/VersionSeasonSix/Gates.cs:267, 534-535, 170`). On our cooked Noria grid the door is open (5 of 7 tiles, and 26 of 28 for the landing), it is in Noria's main region, and it is **89 steps from Noria's spawn**. No substitute entry is needed.
3. **The grid is one map, one region.**
   - 35.5% walkable: 23 077 tiles in one 8-connected region (x 16-243, y 5-242), plus three slivers on the map's last rows.
   - 34.5% NoGround.
   - The safe zone is **165 walkable tiles** at x 74-93, y 5-17. It is the north-west entry hall, and both NPCs and gates 113, 119 and 121 stand in it.
   - Spawn gate 119 (82,8-87,14) is 40 of 42 open and all safe. Its middle, **84,11**, is open and safe, so that is the arrival.
4. **Most of it is existing machinery and rows.** Six things are new:
   - the hidden waterfall emitters (84 anchors, MU's BITMAP_WATERFALL 1-3);
   - the butterfly emitters (20 anchors throwing MODEL_BUTTERFLY01);
   - Aida's flower glows on bones, an `ornaments.cpp` arm like Noria's;
   - an `aidaAir_` bed (or step 0's `MapRow.air`);
   - `CRAWLS_FROM` so Aida can borrow Tarkan's Bug02;
   - the first monster numbers above 255 (304-309).
5. **MU's three cloud emitters in Aida never emit** (§3.4, my reading of `GMAida.cpp:38-90` against `:169-207`). Build no clouds as "MU's" without checking that first.
6. **Cost:** the fixed lines come to about 0.05-0.1 ms of the 0.3 ms effects account. The two risks are the waterfall spray at the falls and the ~1 860 flower sprites if they are not culled by distance. About 183 flower lights go to the shade account, inside the 511-light cap (`src/gfx/renderer.h:429`).

---

### 1. The record the engine has to carry (short; pass A owns it)

**Map and folders.**
- Server map **33** (`OM/VersionSeasonSix/Maps/Aida.cs:18`); MuMain `WD_33AIDA = 33` (`MM/World/MapInfra/MapManager.h:28`).
- Client folders `D/World34` + `D/Object34`. World34 has the usual Enc files, the same sizes as World9's (`.att` 131 076 bytes, a 2-byte word a tile, handled by `terrain.py:495-514`), and TileGrass01/02 (.OZJ and .OZT), TileGround01-03, TileRock01-07, TileWater01, TileWood01, leaf01.OZT, leaf02.OZJ and rain01/02.OZT.
- The dry run: 0.00-3.27 m, 11 slots, 35.5% walkable, 0.8% safe (550 tiles, of which 165 are walkable), light mean (119, 118, 119), **3 752 placements of 74 kinds, none missing from Object34**.

**Gates** (`OM/VersionSeasonSix/Gates.cs`):

| gate | map | box | role | level | line |
|---:|---|---|---|---:|---|
| 120 | Noria (3) | 220,30-226,30 | enter, to 113 | 130 | `:534` |
| 113 | Aida | 76,9-78,16 | arrival from Noria (13 of 24 open, all safe) | -- | `:267` |
| 121 | Aida | 74,9-74,13 | enter, to 122 (4 of 5 open) | 10 | `:535` |
| 122 | Noria | 220,31-226,34 | arrival from Aida (26 of 28 open) | -- | `:170` |
| 119 | Aida | 82,8-87,14 | **spawn gate** (`true`): death, Town Portal, warp "Aida1" | -- | `:265` |
| 140 | Aida | 186,173-190,177 | warp "Aida2" only (25 of 25 open, not safe) | -- | `:266` |
| 336 / 339 | Aida / map 80 | 237,167-240,168 / 237,166-240,166 | to and from Karutan (map 80, later content) | 160 | `:614-615, 268` |

- Warp list: "Aida1" 8 500 zen at level 150 to gate 119; "Aida2" 8 500 zen at level 150 to gate 140 (`Gates.cs:70-71`).
- Walking distance from 84,11: **256 steps** to the Aida2 box and 232 to the Karutan door.

**Monsters** (`OM/VersionSeasonSix/Maps/Aida.cs`), as far as the engine cares:

| # | breed | level | count |
|---:|---|---:|---:|
| 304 | Witch Queen | 94 | 6 |
| 305 | Blue Golem | 84 | 38 |
| 306 | Death Rider | 78 | 39 |
| 307 | Forest Orc | 74 | 36 |
| 308 | Death Tree | 72 | 29 |
| 309 | Hell Maine (respawn 150 s) | 98 | 3 |

- That is 151 spawn rows, every one on an open tile.
- The file also has Season 4+'s "Bloody" breeds 549-552 (levels 114-120, 95 rows) and two NPCs: Osbourne (369) at 78,13 and Jerridon (370) at 86,14. Jerridon stands on a safe NoMove tile (0x05), as NPCs do.
- The base six span **72-98**, the same band as Tarkan's 72-93.

---

### 2. Every place a world is named: the checklist for map 33

There is still no single registry. Icarus and Devil Square each added one row in `sim/maps.cpp` and name tests elsewhere. Every place, verified today:

#### 2.1 Pipeline (source side)

| # | where (file:line) | Aida needs |
|---|---|---|
| P1 | `pipeline/terrain.py main` (`:656-`) | `python3 pipeline/terrain.py $D/World34 aida source/world 34`. Expect the dry run's numbers (§1) |
| P2 | `pipeline/terrain.py:118-150` `HIDDEN_BY_MAP` | `33: {56, 57, 58, 59, 60, 62, 63, 67, 70}`. The first eight are set `HiddenMesh = -2` in MoveAidaObject (`MM/World/GameMaps/GMAida.cpp:75-84`); 60 is hidden in RenderAidaObjectVisual (`:199-207`). Pass B confirms against the meshes |
| P3 | `pipeline/terrain.py:160-181` `BLEND_MESH_BY_MAP` | MoveAidaObject sets no BlendMesh. Object42 (type 41) gets an extra RENDER_BRIGHT pass of mesh 0 at alpha 0.5 (`GMAida.cpp:62-66, 148-162`), which is an added glow on the recipe (pass B), not a row |
| P4 | `pipeline/terrain.py:183-223` `GRASS_BY_MAP` | World34 ships TileGrass01.OZT and TileGrass02.OZT, so MU grows on slots 0 and 1 (30.6% + 3.1%). A row only if pass B wants other than the recipes give. Grass is priced (docs/grass.md) |
| P5 | `terrain.py:230` `VOID_BY_MAP`, `:282` `SKY_BY_MAP`, `:295` `OPEN_BY_MAP`, `:316` `VOID_FILL_BY_MAP`, `:368-384` light rows, `:386` `WATER_FLOW_BY_MAP` | None to start. The 34.5% NoGround is cut as Tarkan's is (default abyss); the look decides. TileWater01 (4.4%) slides at MU's default |
| P6 | `terrain.py:411-418` and `pipeline/index.py:44-51` `OPERABLE_BY_MAP` | none: there is no Aida arm in MOVEMENT_OPERATE, and `CreateAidaObject` creates nothing (`GMAida.cpp:30-36`) |
| P7 | `pipeline/index.py:58-81` `GATE_BOXES_BY_MAP` | `33: [(74, 9, 74, 13), (76, 9, 78, 16), (82, 8, 87, 14), (186, 173, 190, 177)]`; `3 += [(220, 30, 226, 30), (220, 31, 226, 34)]` for Noria's door. Then recook **Noria's** tables too |
| P8 | `source/world/aida/ground.json` | sheets under a fresh prefix. **`ad_` is the suggestion; check `ls source/textures | grep '^ad_'` is empty first.** One flat pool (tarkan-port trap: sheet names collide). Pass B |
| P9 | `source/mu.db` `gates` | `insert into gates values (119, 33, 82, 8, 87, 14, 1)`. **This one row keeps a death in Aida** (the gate-22 lesson). Today's rows are 17, 22, 27, 42, 49, 57; no 119 |
| P10 | `pipeline/index.py worlds()` (`:1536-`) | automatic once the ground is built |
| P11 | `source/sounds/sounds.json` (`world_desert` at `:1503-1511`, `world_heaven` `:1512-1520`) | a `world_aida` event, `aida_ambi.wav` into `source/sounds/` (§5) |

#### 2.2 The cook (`tools/cook.py`)

| key | line | Aida |
|---|---|---|
| `AIRS` / `AIRS_FROM` | `:205-222` | none: MU flies no boid in Aida (no `WD_33AIDA` arm in GOBoid's MoveBoids) |
| `CRAWLS` | `:215` | `"aida": "Bug02"`: MoveFishs' Aida arm is Tarkan's (`MM/Engine/AI/GOBoid.cpp:1726-1733`), and MapManager loads `Object9\Bug02` for Aida too (`MapManager.cpp:331-332`). **`crawling()` (`:233-238`) only looks in `assets/world/<world>/Bug02/`.** Either add a `CRAWLS_FROM = {"aida": "tarkan"}` to `crawling()` and the mesh cook, as `AIRS_FROM`/`boid_glb` do (`:222-230`; recommended, about 5 lines), or build a copy of the recipe under `source/world/aida/Bug02` |
| `FLAT_OVER_VOID` | `:1342` | probably `aida`, as Tarkan: objects at the plateau edges over 34.5% NoGround. Decide from the cook's "buried" count after the first object batch |
| `GROUNDED_TYPES` | `:1352-1361` | only if floating plants show (Tarkan's lesson) |
| `LIFTED_BY_WORLD`, `CHROMED_BY_WORLD` | `:1379, 1383` | none expected |
| `ANCHOR_KINDS_BY_WORLD` | `:1414-1435` | `"aida": {"Object57": 18, "Object58": 19, "Object59": 20, "Object61": 21, "Object68": 1}` (§3.3; numbers follow Icarus's 12-17). Kind 1 is the existing fire anchor for Object68's flame. Object60/63/64 (types 59/62/63) only if decision 5 builds them. Object71's ghost (type 70) is left out to start |
| `ANCHOR_LIGHT` | `:1453-1470` | one row per new kind, all dark (as 7-17). Kind 1 already has its light |
| `HIDDEN_BY_WORLD` | `:1447` | none (terrain.py's flag carries it) |
| `PERCHES` | `:2284-2321` | none |
| weathering | `:463-489` `weathering_of` | automatic from each recipe's `sheet_materials` (wood, timber, rock and so on). Nothing per world |
| roofs and water sway | `:1631-1641` | automatic, per world, from index.json's `roof_fade`/`sway` |
| figures | `figure_file` | automatic `figures_aida.json` |
| tables after any kind or item change | -- | **twelve** worlds now: lorencia, noria, devias, dungeon, losttower, bloodcastle, atlans, charscene, tarkan, icarus, devilsquare, aida (`assets/cooked/*/*.mur` lists eleven today) |

#### 2.3 The runtime: every place a world is named

| # | file:line | today | Aida row |
|---|---|---|---|
| R1 | `src/sim/maps.cpp:13-55` `kMaps` | 10 rows; Tarkan `{"tarkan", 8, {195, 65}, true}` `:33` | `{"aida", 33, {84, 11}, true}`: spawn gate 119's middle, open and safe. `underground` because MU blows no leaves there (§3.5) and plays no wind (`MM/Scenes/SceneManager.cpp:842-900` has no Aida case); `home` 0 since it has its own safe zone; not an event, not floors. Append after Devil Square so the M key's order (`mapAfter`) is unchanged. The server lands players by this table too (`sim/maps.h:1-3`) |
| R2 | `src/sim/maps.h:15-36` `MapRow` | still no `air` field | step 0 (§6) |
| R3 | `src/game/ui/arrival.cpp:55-68` `kPlaces` | 10 rows | `{"aida", "Aida", ""}` |
| R4 | `src/game/ui/minimap.cpp:304-317` `mapName` | `case 0..11` | `case 33: return "Aida";`. Noria's door then labels "Gate to Aida" (`:987`) |
| R5 | `src/game/ui/minimap.cpp:287` `floorName` | Dungeon, tower | nothing unless "Aida 2" gets a warp row |
| R6 | `src/game/world/maps.cpp` `placeName` | tower and Dungeon by box | nothing unless "Aida 2" is named |
| R7 | `src/sim/realm_travel.cpp:20-56` `kRows`; `src/sim/travel.h:32` `kTravels = 16` | Icarus last `:55` | decision 3: append "Aida" (`kTravels` 17), landing 84,11. Append only, so saves keep their bits (`found` is u32, `realm_travel.cpp:152`) |
| R8 | `src/game/ui/travel.cpp:241` `kComing` | Lost Tower, Atlans, Tarkan, Icarus | Aida is not listed, so no dimmed card vanishes. A row gives it a card |
| R9 | `sheets/worlds/aida.json` | -- | new, found by name (`game/world/maps.cpp` `mapSheet`). Pass B's look; MU's clear is black (`SceneManager.cpp:371-403`: no Aida arm) |
| R10 | `src/game/play_open.cpp:33-44` | `windy_`, `dungeonAir_`, `towerAir_`, `castleAir_`, `waterAir_`, `desertAir_`, `heavenAir_` by name | `aidaAir_ = world == "aida"` (**the eighth name-keyed bool**; step 0 replaces them). `grassy_` (`:45`) stays false unless pass B wants grass steps (MU: PlayWalkSound's grass is Lorencia/Noria only) |
| R11 | `src/game/play_open.cpp:481-488` | air loads | `else if (aidaAir_) heard_.wind = sound_.load("world_aida", false);` |
| R12 | `src/game/play_sound.cpp:418`, `:427-434`; `src/game/play.h:1410-1418` | | add `\|\| aidaAir_` to the loop, **and an Open room arm**: `underground` makes `indoors()` true on every tile, so without its own arm Aida takes the Roofed reverb |
| R13 | `src/game/world/world.cpp:236-257` leaves | open unless underground; Atlans, Tarkan, Devil Square, Icarus by name | nothing for MU's (none); a row only for decision 6 |
| R14 | `src/game/world/boids.cpp:66-119` | birds by name | nothing |
| R15 | `src/game/world/scurry.cpp:32-55` `crawlOf`, `crawlRowOf`; `scurry.h:65` `kMaxRats = 10` | Tarkan's ten | `crawlOf("aida") = "Bug02"`; the row is Tarkan's numbers with **`most = 3`**: Aida is not in MoveFishs' list that skips the `i >= 3` cut (`GOBoid.cpp:1668-1675`). The trail is the same (`CreateJoint BITMAP_JOINT_ENERGY` sub 4, `:1733`) |
| R16 | `src/game/world/weather.cpp:177-215` | rain/snow/sand/sky by name | nothing: MU has no Aida weather (its `CreateMist` returns false, `GMAida.cpp:1563-1571`) |
| R17 | `src/app/modes/play_mode.cpp:1233-1244` music | Atlans, tower, Tarkan in their safe zones | add `"aida"` to that arm with `"/music/Aida.mp3"` (**capital A**: the file is `assets/music/Aida.mp3`, and Linux and the Hetzner box are case-sensitive). Decision 7 |
| R18 | `src/sim/gates.cpp:9-71` `kExits`, `:73-139` `kEnters` | | §4.1 |
| R19 | `source/mu.db` | | §4.3 and §5 |
| R20 | `tools/bot/brain.h:61-77` `kWorlds` | 10 rows | `{33, "aida", {84, 11}}` only if the bots should grind there (bot work is paused, memory) |
| R21 | `src/sim/gates.cpp` `gateToward` (`gates.h:61`) | derived from `kEnters` | automatic: the quest tracker's pointer reaches Aida through Noria once the gates exist. Add a `checkEqual(toward(0, 33, 100, 100), 23, ...)` beside `tests/sim_test.cpp:5496` |
| R22 | `src/sim/items.h:210-212` `novaScrollMap`, `featherMap`; `:1206-1207` `firecrackerMap`; `realm_items.cpp:1322` `secondGearHere` | 4/7/8/10; 8/10; dungeons and 11-17; Tarkan, Icarus, BC | nothing by default. Decision 9 (feather, nova, second-class gear) |
| R23 | `server/src/main.cpp:855-925` `worldName`, `raiseWorld` | any lower-case name with a cooked `.mur` | **automatic**: one server serves every map by name (`assets/cooked/<w>/<w>.mur`). `server/deploy.sh` ships every `*.mur` |
| R24 | `src/game/world/{doors,trap_show,lava_smoke,skulls,castle_sparks,drawbridge,void_clouds,portal,bubbles,desert_vents,sky_clouds,sand_haze}.cpp` | each returns off its world | nothing |
| R25 | `src/game/world/ornaments.cpp:341-380` (Noria's `kNoriaGlows`, `:92-101`) | per-world bone glows | Aida's flower glows (§3.2) |

Load path unchanged: `PlayMode::travel` -> `World::open` -> `Play::open` (`play_open.cpp:25-`, `cooked/aida/aida.mur`) -> boids, leaves, weather (`world.cpp:234-262`).

---

### 3. Aida's special features against the engine

All 154 MuMain lines were read (`c/mm_aida.txt`). Placement counts are from the dry run's `aida.json`. **A placement of type t is model `Object{t+1}`.**

#### 3.1 What MU does in Aida, in one list

| type (model, placed) | MU (`MM/World/GameMaps/GMAida.cpp`) | engine | new? |
|---|---|---|---|
| 25 (Object26, 40), 28 (Object29, 4) | `BlendMeshTexCoordV -= 0.015` a frame (`:48-55`) | `Material` V scroll (the cook's glow scroll) | row |
| 30 (Object31, **155**) | green terrain light (0.4-0.44, 0.6-0.64, 0.4-0.44), 2 tiles, flickered by `rand()%5*0.01` (`:56-61`); 12 BITMAP_SPARK+1 sprites at bones 6, 7, 8, 12, 13, 17, blue (0.1, 0.1, 0.3) at 4-5.5 plus grey at 3.0 (`:105-147`) | recipe lamps (the lights) + an `ornaments.cpp` arm (the sprites) | arm |
| 71 (Object72, 28) | the same, red: light (0.9-0.94, 0.2, 0.2) (`:62-67`); sprites red (0.25-0.5, 0, 0) (`:232-274`) | the same | same arm |
| 41 (Object42, 21) | `Alpha = 0.5`; mesh 0 drawn again RENDER_BRIGHT, BodyLight 0.52, StreamMesh (`:68-72, 148-162`) | an added glow entry on the recipe (Tarkan's opaque-stream / glow path) | row |
| 56 (Object57, 22) | hidden; BITMAP_WATERFALL_1 sub 2 one frame in 2 (`:163-169`) | an anchor emitter | **new kind** |
| 57 (Object58, 23) | hidden; BITMAP_WATERFALL_3 sub 4 every frame (`:170-173`) | the same | **new kind** |
| 58 (Object59, 39) | hidden; BITMAP_WATERFALL_2 sub 2 one frame in 3 (`:174-180`) | the same | **new kind** |
| 59, 62, 63 (Object60/63/64; 20, 47, 24) | hidden; "20 BITMAP_CLOUD if HiddenMesh != -2" (`:181-207, 208-227`) | -- | see §3.4 |
| 60 (Object61, 20) | one MODEL_BUTTERFLY01 (sub 3, Scale 0.9, 250 frames, Velocity 0.3) one frame in 25, then hidden (`:199-207`; `MM/Render/Effects/ZzzEffect.cpp:596-609`) | Noria's Butterfly01 boid model (`AIRS_FROM noria -> lorencia`) | **new kind** |
| 64 (Object65, 65) | `Velocity = 0.05` (`:86-88`): its clip at MU's 0.05 | Sway `play_speed` | row |
| 65, 66 (Object66/67; 3, 8) | mesh 0 U-slid `-t*0.00005` (0.05 a second), opaque (`:228-238`) | Tarkan's `stream` glow entry (`Material::scrollAlongUV`) | row |
| 67 (Object68, 11) | hidden; BITMAP_FLAME sub 7 every frame, light 0.3 (`:239-242`) | fire anchor, kind 1 (`ANCHOR_KINDS`) | row |
| 70 (Object71, 5) | hidden; a MODEL_GHOST effect, one frame in 5 inside a 30 ms window of every 1 024 ms (`:243-251`) | none | leave out (decision 5) |
| 75 (Object76, 1) | a BITMAP_FLARE at bone 4, Scale 3, spinning (`:276-285`) | ornaments arm | same arm |
| 77, 78 (Object78/79; 10, 6) | mesh 0 U-slid `t*0.0002` (0.2 a second), opaque (`:286-295`) | `stream` | row |
| every object | `PlayBuffer(SOUND_AIDA_AMBIENT)` (`:92`) | the air bed | §5 |
| -- | 3 Bug02 crawlers with energy trails (`GOBoid.cpp:1691, 1726-1733, 1830`) | Scurry | row |
| -- | `IsInAidaSection2` (x 54.5-178.2, y 67.8-224.2 tiles) gates `CreateMist`, which returns false either way (`:23-28, 1563-1571`) | -- | nothing |
| -- | `IsInAida()` is also true in `WD_54CHARACTERSCENE` (`:18-21`) | our `charscene` is world 74's (EmpireGuardian4, `tools/cook.py:1404-1450`), so no overlap | nothing |

Nothing in the terrain code names Aida: no grass rule, water rule or clear colour (`grep` of `ZzzLodTerrain.cpp` and `SceneManager.cpp:371-403`). MuMain draws its ground, unlike Icarus.

#### 3.2 The flower glows (has the machinery; one ornaments arm)

- `src/game/world/ornaments.cpp:341-380` is Noria's: a `kNoriaGlows` table of `{model, bones, scale, colour, steady}` (`:92-101`) anchored on a bone through Sway's pose. Aida's is the same shape: `kAidaGlows` for Object31 (blue) and Object72 (red), bones {6, 7, 8, 12, 13, 17}, MU's offsets per bone, two sprites each (colour and grey core), and Object76's spinning flare at bone 4.
- **Count:** 155 + 28 placements x 12 sprites = **2 196 sprites map-wide**. At the camera's ~20-30 m reach perhaps 25-40 flowers are in view, so 300-480 small sprites (BITMAP_SPARK+1 at Scale 3-5.5, a few decimetres). Cull by distance as `desert_vents` does (28 m). If the flowers are rigged and swaying, their anchors ride Sway's pose, which is per-placement CPU. Pass B says whether they have clips.
- Their **terrain lights** (2 tiles, green or red) are recipe lamps on Object31/Object72: 183 point lights, inside `kMaxPointLights` 511. The flicker is a small wander.

#### 3.3 The hidden emitters (has the anchor machinery; 3-4 new kinds)

- **Waterfalls** (types 56/57/58, 84 anchors): MU's three falling-water particles.
  - WATERFALL_1 sub 2: life 30, Scale 1.6 + the placement's Scale, falling 7-11 a frame, light 0.2 (`MM/Render/Effects/ZzzEffectParticle.cpp:3383-3402`).
  - WATERFALL_2 sub 2: life 40, Scale 0.6-1.1 + Scale (`:3499-3515`).
  - WATERFALL_3 sub 4 (`:3578-`).
  - Sheets `Effect/WATERFALL1-3.jpg` (`ZzzOpenData.cpp:5330-5332`). **The engine has none of these**, and no `waterfall` row in `pipeline/index.py`'s EFFECTS.
  - Route: three EFFECTS rows (`"waterfall"`, `"waterfall_splash"`, `"waterfall_spray"`), then `--only showing`, and a small `game/world/falls.{h,cpp}` drawer on `desert_vents`' pattern (anchors in reach, a seeded roll a frame, a sprite pool). Kinds 18-20.
- **Butterflies** (type 60, 20 anchors): one butterfly a second per anchor, each living 10 s, so about **10 live per anchor**.
  - The model is Noria's Butterfly01 (cooked into `cooked/noria`).
  - Route: kind 21, drawn by `Boids` (as a second "released" pool) or a small `game/world/flutters` using the boid's mesh and clip. The cook needs the glb found the way `AIRS_FROM` finds Noria's (`tools/cook.py:225-230`).
  - **Cap it** (ours): 3 per anchor and only within reach. MU's ten each over 20 anchors would be up to 200 skinned figures.
- **Flame** (type 67, 11 anchors): the existing fire anchor, kind 1 (`ANCHOR_KINDS`, `cook.py:1403`), as Lorencia's Light01. Light 0.3 is dimmer than CreateFire's; the anchor's light row may want a dimmer twin.
- **Ghost** (type 70, 5 anchors): MODEL_GHOST is a figure effect we do not have, and it fires about once a second for a frame or two. Leave it out (decision 5).

#### 3.4 The cloud emitters that never emit (a trap)

- MoveAidaObject sets `HiddenMesh = -2` on types 59, 62 and 63 every frame (`GMAida.cpp:75-84`).
- RenderAidaObjectVisual then throws its 20 BITMAP_CLOUD puffs only `if (pObject->HiddenMesh != -2)` (`:181-189, 208-227`).
- MoveObjects runs before RenderObjects. Nothing in between resets HiddenMesh: `ZzzObject.cpp:1090` resets it only on a shadow-casting path for other types.
- So **in MuMain, Aida's 91 cloud anchors are dead**, though MapManager loads `clouds.jpg` for Aida (`MapManager.cpp:334`).
- Pass B should confirm this against a reference shot of MU's Aida. If it holds, the clouds are ours (decision 5): Icarus's `sky_clouds` (kinds 12-17) or Blood Castle's `void_clouds`, re-coloured to MU's (0.01, 0.03, 0.05), (0.05, 0.05, 0.05) and (0.05, 0.02, 0.02). They would be faint ground mist, not "MU's".

#### 3.5 Leaves, mist, weather (nothing, by MU)

- `RequireLeavesEffect` lists Aida (`MM/Scenes/MainScene.cpp:89`), but no Create* in the leaves loop takes it: `M33Aida::CreateMist` returns false whatever the section (`ZzzEffectFireLeave.cpp:491`, `GMAida.cpp:1563-1571`). Aida is not in RenderLeaves' blend list (`ZzzEffectFireLeave.cpp:525-547`).
- World34 ships `leaf01.OZT`/`leaf02.OZJ`, which MapManager loads for every world (`MapManager.cpp:1475-1480`), but nothing spawns them in Aida.
- So MU's Aida has **no leaves, no mist and no rain**. The `underground` flag gives exactly that.
- Decision 6: our own falling leaves from World34's `leaf01` on the Lorencia pool (`Leaves::open` with a sheet override) would cost what Lorencia's do (well under 0.05 ms).

#### 3.6 Monsters' shows (step 7; routes only)

| breed | MU (`GMAida.cpp`) | engine has | route |
|---|---|---|---|
| Witch Queen 304 (Monster100?, Scale 1.4) | red terrain light 3 tiles (`:489-494`); 5 BITMAP_LIGHT+1 sprites on bones; smoke, lightning joints on her skill (`:703-797`) | `kAuraLights` (`play_tuning.h`), `fx/thunder` | rows; a hand-glow list |
| Blue Golem 305 (`MODEL_GOLDEN_STONE_GOLEM`, Scale 1.35) | blue terrain light (`:496-500`); mesh 0 V/U slide + RENDER_CHROME\|BRIGHT pass (`:1339-1351`); waterfall-2 drips on 3 bones; shock wave and energy joint on attack | chrome pass (`Drawable::refine`), aura light | rows; needs the waterfall sheet (§3.3) |
| Death Rider 306 (Scale 1.1) | weapon blur on attack, bones 18-20 and 25-26 (`:532-552`); a light sprite + sparks; mesh 3 chrome slide | `fx/streak` (the skill blur ribbon) | a monster caller of `fx/streak` |
| Forest Orc 307 (Scale 1.15) | blurs on 3 bone pairs on attack (`:554-580`) | `fx/streak` | same |
| Death Tree 308 (Scale 1.2) | blur on attack; MODEL_TREE_ATTACK (`Object34/tree_eff`, `MapManager.cpp:329-330`), sparks, smoke | `fx/effect_mesh` | tree_eff built as an effect model |
| Hell Maine 309 (Scale 1.8, boss) | red light; eye and arm glows; true fire `fantaF.jpg` (`ZzzOpenData.cpp:5185`); BITMAP_BOSS_LASER; MODEL_STORM | `fx/storm`, `fx/flame`, `fx/body_flames` | rows + a laser |

- Breeds over 255: `monster_kinds.number` is INTEGER, `.mur` kinds are i32 (`tools/cook.py:2324-2340`), and `kResistances`/`kDropRates` are int32 (`src/sim/realm_tuning.h:425-494`). No u8 carries a monster number in `src/sim` or `src/net` by grep. **Still run `checks` and a headless Aida raise before trusting it.** These are the first breeds past 255 (mu.db's highest is 150).

#### 3.7 What it costs, all together

Prices from `docs/budget.md`: effects account 0.3 ms at 1080p; **0.018 ms per full screen of blended overdraw** (`:246`); point lights cap 511.

| line | account | estimate |
|---|---|---|
| flower sprites, culled to ~28 m (300-480 small sprites) | effects | 0.01-0.03 ms; **uncapped map-wide 2 196 would be the CPU risk** |
| 183 flower lights + monster aura lights | shade | as Atlans's 216 lamps (measured inside its 4.3-4.9 ms frame) |
| waterfalls at a fall (3-6 emitters in reach, ~15-30 live puffs each at 1-2 m) | effects | **0.02-0.06 ms while standing at the falls: measure** |
| butterflies, capped 3 per anchor in reach | shade + CPU | ~0.01 ms GPU; per-figure CPU as Noria's boids |
| 11 flames | effects | < 0.01 ms |
| 3 bugs with trails | -- | ~0 |
| blurs on monster swings (Death Rider, Orc, Tree) | effects | ribbons, < 0.02 ms in a pull |
| grass on slots 0+1 (33.7% of tiles) | grass | as Noria's |
| sun shadow pass | shadow | 3 752 placements, unculled as Tarkan's 3 381 |

The fixed lines are under 0.1 ms. Measure at each step with `--budget --stats --repeat 3`, timers off, on the display the log names (memory: measure on the display you think; view timers cost the frame).

---

### 4. Travel: gates, warp rows, safe zone and death landing

#### 4.1 Gates (rows only; the walker, the barred speech and gateToward are generic)

`src/sim/gates.cpp`. Directions as stored: 0 none; (dx, dy) as the Tarkan rows. OM gives every Aida/Noria exit Direction 0.
- `kExits` += (after Icarus's at `:66-67`):
  - `{119, 33, {82, 8, 87, 14}, 0, 0}`: Aida's hall, for death, Town Portal and the warp;
  - `{113, 33, {76, 9, 78, 16}, 0, 0}`: Aida, from Noria. 13 of 24 open; the walker lands on the nearest open tile;
  - `{122, 3, {220, 31, 226, 34}, 0, 0}`: Noria, from Aida.
- `kEnters` += (beside Noria's `{45, 3, ...}` at `:89`):
  - `{120, 3, {220, 30, 226, 30}, L_in, 113}`: Noria to Aida;
  - `{121, 33, {74, 9, 74, 13}, L_out, 122}`: Aida to Noria.
  - **Exit 122's box (rows 31-34) is directly under enter 120's (row 30)**, as 24/25 sit at Noria's north edge, so someone landing from Aida does not stand in 120. Check the landing tile is not row 30.
- Optional, decision 8: `{336, 33, {237, 167, 240, 168}, 0, -1, "Karutan"}`, a sealed door (`gates.h:42-43`; the drawing's "is sealed" line exists, `src/game/play.cpp:784-789`).
- `pipeline/index.py` `GATE_BOXES_BY_MAP[33]` and `[3] +=` (P7), then **both Aida's and Noria's tables** recooked.
- **sim_test** `testAidaGates`, on `testTarkanGates`' pattern (`tests/sim_test.cpp:6776-6837`, called at `:12732`):
  - Noria 223,32 walking to 223,30 at L_in goes Gated 120 and lands in 76-78 x 9-16;
  - at L_in - 1 he is Barred with L_in;
  - Aida 76,11 walking to 74,11 goes Gated 121 and lands in Noria 220-226 x 31-34;
  - `aida.safeGate == {82, 8, 87, 14}`;
  - `toward(0, 33, ...) == 23` (through Noria).

#### 4.2 The warp rows (decision 3)

- S6 has "Aida1" (gate 119) and "Aida2" (gate 140), both 8 500 zen at 150.
- `Realm::settleFound` (`src/sim/realm_travel.cpp:120-180`) still floods each row's landing **in turn**, and Aida is one region of 23 077 tiles. Two rows would leave "Aida 2" owning one tile, opening only on its exact landing (the trap tarkan-port §4.3 wrote up, still unfixed).
- One row needs nothing new. Two need the ~10-line multi-source flood: one queue seeded with every landing, flooded once. That also unlocks a Tarkan 2 later.
- Opening: Aida has no quest giver, so with one row it opens the first time he stands there (`:163-178`). If Osbourne or Jerridon is ever a giver, that changes.

#### 4.3 Safe zone, death landing, Town Portal

- Aida has its own safe zone (165 walkable tiles) and MuMain allows the Portal there (`MM/World/MapInfra/PortalMgr.cpp:55`).
- One mu.db row, `gates (119, 33, 82, 8, 87, 14, 1)`, is all it needs: index.py writes it, the cook puts it in `aida.mur`'s header, `haven()` finds it, and `MapRow.home` stays 0.
- **Without it every death in Aida silently rises in Lorencia** (`realm_items.cpp:758`'s fallback; Devias's gate-22 bug).
- Check after step 1: the `.mur` header's safe box reads 82, 8, 87, 14.

---

### 5. Music and sound

- **Music**: `assets/music/Aida.mp3` (2.99 MB, MU's 2005 file) is present but gitignored (`MU2_BGFX/.gitignore:5`, `assets/`), so a fresh checkout lacks it, as `tarkan.mp3`.
  - MU plays MUSIC_BC_ADIA (`MM/Core/Globals/_enum.h:186`) on the whole map (`SceneManager.cpp:1089-1094`).
  - The house rule now: a world's theme in its safe zone (Atlans, the tower, Tarkan; `play_mode.cpp:1233-1244`) or a whole-map loop by the user's choice (Icarus `:1245-1251`); "music is rare and in fights" (memory).
  - Binding is the string in that arm; nothing is cooked for music.
- **The air**: `D/Sound/w34/aida_ambi.wav`. It is 10.1 s, stereo, 16-bit, 22 050 Hz, **RMS -24.0 dBFS**.
  - MU loads it 3D-enabled with one channel (`MapManager.cpp:338`) and replays it unplaced from MoveAidaObject every frame (`GMAida.cpp:92`), which makes it a continuous bed over the whole map, as desert.wav is Tarkan's.
  - Route: copy it to `source/sounds/aida_ambi.wav`, and add `world_aida` to `source/sounds/sounds.json` beside `world_heaven` (`:1512-1520`) with `"loop": true` and **`"gain_db": -20.0`** (puts it on the same -44 dB bed). Then `python3 pipeline/index.py source assets` and `python3 tools/cook.py --only showing` (memory: new sound needs the showing cook; it is mono 22 kHz after the cook).
  - Check: the log has no "no cooked event 'world_aida'" line, and `afinfo assets/cooked/showing/sounds/aida_ambi*` shows about 10.1 s.
- **Monster sounds** (step 7): 31 wavs in `D/Sound/w34/` (`MapManager.cpp:339-369`).
  - **Trap:** MuMain asks `dr_idle1.wav`, but the folder ships `dr_idle01.wav`. Name the file that exists.
  - Plus `eElec1/2.wav` (SOUND_CHAOS_THUNDER, `:370-371`), loaded for Aida.

---

### 6. The build, in order

`$D` = `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/bin/Data`; `$S` = a scratch folder. Run from `MU2_BGFX/`. Rules for every step:
- `nice` and about 6 of 10 threads (memory: don't use all cores);
- every run `--mute` and `--save $S/demo.json` (memory);
- `checks` = `cmake --build build --target checks`;
- one commit per batch, straight to main, through a private index if another session has staged.

**Step 0 (optional, recommended): the air as a `MapRow` field.** This is the third port to ask for it.
- `sim/maps.h` += `const char* air = nullptr` (the sound event; null means the wind if not underground) and `bool openAir = false` (an Open room though underground).
- Fill the rows: dungeon/losttower `world_dungeon`/`world_tower` (Stone room stays by name), atlans `world_water` (Water room), tarkan `world_desert`, icarus `world_heaven`.
- Read them at `play_open.cpp:33-44, 481-488` and `play_sound.cpp:418, 427-434` in place of the seven bools.
- Check: `checks`; headless `--world dungeon|losttower|atlans|tarkan --ticks 2000 --seed 7` logs byte-identical before and after.
- Seen: nothing, by design. If skipped, step 3 adds `aidaAir_` as an eighth bool.

**Step 1: bare land, reachable by `--world aida`.** Files: `pipeline/terrain.py`, `pipeline/index.py`, `source/world/aida/*`, `source/textures/ad_*.png`, `source/mu.db`, `src/sim/maps.cpp`, `src/game/ui/arrival.cpp`, `src/game/ui/minimap.cpp`, `sheets/worlds/aida.json`. The same set as Devil Square's `699c9d72` and Icarus's `e3fa917b`.
1. `pipeline/terrain.py`:
   - `HIDDEN_BY_MAP[33] = {56, 57, 58, 59, 60, 62, 63, 67, 70}` with the GMAida citations;
   - `GRASS_BY_MAP[33]` per pass B (MU's: `["TileGrass01", "TileGrass02"]`).
   - No OPERABLE row and no BLEND_MESH row.
2. `pipeline/index.py`: `GATE_BOXES_BY_MAP[33] = [(74, 9, 74, 13), (76, 9, 78, 16), (82, 8, 87, 14), (186, 173, 190, 177)]` and `[3] += [(220, 30, 226, 30), (220, 31, 226, 34)]`. The boxes are in now so step 2 needs no second ground pass.
3. Extract: `nice python3 pipeline/terrain.py $D/World34 aida source/world 34`. Expect 0.00-3.27 m, 11 slots, 35.5% walkable, 0.8% safe, 3 752 placed / 74 kinds, no `objects dropped` line.
4. Decode the tile sheets pass B names under `ad_`, each with `python3 pipeline/decode_texture.py $D/World34/<Tile>.OZJ source/textures/ad_<tile>.png`. Only the slots in use: TileGrass01/02, TileGround01-03, TileWater01, TileWood01, TileRock01-03, TileRock05.
5. Write `source/world/aida/ground.json` on Tarkan's shape (`tileset`, `mu_name`, `profile: "tiles"`, `sheets`, `sheet_from`, `slot_note`, `sheet_materials` including the `_over` slots).
6. Build the ground only, not `tools/content.sh --world aida`, which rebuilds every stale asset in the tree (memory):
   `python3 pipeline/tileset.py source/world/aida/ground.json workshop/world/aida && python3 pipeline/ground.py source/world/aida workshop/world/aida source/textures`.
   If a sheet is replaced later: `tools/asset.sh <recipe> --rebuild` (memory: the x3 cache is keyed by name).
7. mu.db, only when no session is live (the `-shm` was touched at 00:26):
   `sqlite3 source/mu.db "insert into gates values (119, 33, 82, 8, 87, 14, 1); pragma wal_checkpoint(TRUNCATE);"`
8. Index and sync: `python3 pipeline/index.py source workshop && ./tools/sync.sh --world aida --only-world`.
9. Cook what changed, in order, and check `wc -l assets/cooked/aida/jobs.txt` stays small:
   - `python3 tools/cook.py --world aida --only ground --threads 4`
   - `python3 tools/cook.py --world aida --only tables`
   - `python3 tools/cook.py --world noria --only tables` (Noria's gate boxes)
   - Skip `--only placements` until objects exist.
10. Code:
    - `src/sim/maps.cpp`: append `{"aida", 33, {84, 11}, true}` with its comment (spawn gate 119's middle, OM S6 Gates.cs:265; underground for its air until step 3; MU's black clear and no leaves);
    - `src/game/ui/arrival.cpp`: `{"aida", "Aida", ""}`;
    - `src/game/ui/minimap.cpp:304-317`: `case 33: return "Aida";`.
11. `sheets/worlds/aida.json`: pass B's starting sheet (black clear by default).
12. Build: `cmake --build build -j 6`.

- **Check:**
  - `./run.sh --headless --world aida --ticks 200 --no-hand` logs the grid and map 33 with no error;
  - `python3 -c` (or `grep -a`) shows the `.mur` header's safe box 82,8,87,14;
  - `checks`;
  - `sqlite3 'file:source/mu.db?mode=ro' "select * from gates where id=119"`.
- **Seen**, muted, one launch with several shots: `./run.sh --world aida --at 84,11 --time noon --frames 240 --shot 120 --shot-path $S/shots --mute --save $S/demo.json`, repeated `--at` for:
  - the hall (84,11);
  - the Aida 2 clearing (188,175);
  - the south basin in Section 2 (116,145);
  - the Karutan corner (238,167).

  Note the fps from the log. Expected: bare tiled ground over the black cut, no objects, only the missing `aida.mut` error. **Show the user before step 2** (memory: small batches, shown early).

**Step 2: the gates.**
- `src/sim/gates.cpp` rows (§4.1), levels per decision 1. The comment cites OM S6 `Gates.cs:534-535, 267, 170, 265`.
- `tests/sim_test.cpp`: `testAidaGates` and the `toward(0, 33, ...)` line; call it beside `:12732`.
- Check: `checks` (sim_test green but for the standing failures; compare the count with HEAD's) and the headless door walk.
- Seen: a muted `--play` run walking Noria's north-east door both ways (`--world noria --at 223,33 --level L_in`).
- **Server:** gates and `kMaps` are sim, so the lockstep server must carry them. Run a local `mu2_server` with a netbot first, kill it after, then `server/deploy.sh` at the milestone (memory: Hetzner builds at milestones; stop test servers).

**Step 3: the air and the music.**
- `aida_ambi.wav` + `world_aida` (§5); `index.py source assets`; `--only showing` (several minutes; check `jobs.txt`).
- `aidaAir_` (or step 0's field) with the room Open; `"aida"` in the safe-zone music arm with `/music/Aida.mp3`.
- Check: the log names `world_aida` with no "no cooked event"; `--budget --stats` on the bare land.
- Seen: a muted run, log only. The user listens in game (memory: user judges effects in game).

**Step 4: objects**, in pass B's small batches (3-6 kinds built, cooked, shot and shown before the next):
- recipes under `source/world/aida/ObjectNN.json` with `ad_` sheets;
- `tools/asset.sh aida/ObjectNN --build-only` per kind;
- `python3 pipeline/index.py source workshop`; `./tools/sync.sh --world aida --only-world`;
- then `--only textures`, then `--only meshes`, then `--only placements` (**textures before meshes**, or every `.mum` loses its links; memory).

Suggested order: the trees and rocks (Object25 x488, Object07 x427, Object10 x235); then the walls; the flowers Object31/Object72 with their recipe lamps; the scrolling kinds (26, 29, 66, 67, 78, 79) and Object42's added pass; Object65's 0.05 clip.
- Check each batch: the cook's buried/grounded counts (`FLAT_OVER_VOID`/`GROUNDED_TYPES` if it calls for them), the town log's placement count, and `--budget --stats`.
- Seen: muted shots at the batch's own tiles.

**Step 5: ornaments and emitters.**
- `ornaments.cpp` `kAidaGlows` (§3.2), distance-culled.
- `ANCHOR_KINDS_BY_WORLD["aida"]` and `ANCHOR_LIGHT` rows (§2.2); the waterfall sheets in `index.py` EFFECTS + `--only showing`; `game/world/falls.*`; the capped butterflies; Object68's fire.
- Check: the open line counts 22 + 23 + 39 waterfalls, 20 butterfly anchors and 11 flames; effects account at a fall.
- Seen: a waterfall and a flower field, noon and night.

**Step 6: the bugs.**
- `CRAWLS["aida"] = "Bug02"` + `CRAWLS_FROM`; `crawlOf`/`crawlRowOf("aida")` with `most = 3`; `--only meshes`.
- Check: the log names 3 bugs; Tarkan's ten unchanged.

**Step 7: monsters**, after pass A's call on the breeds (base six recommended).
1. Six kinds 304-309 into `monster_kinds`, the 151 spawns as ids **1575-1725** (today's `max(id)` is 1574), and the NPCs if decided. Checkpoint the WAL.
2. Run `pipeline/index.py source workshop`, then **merge Aida's spawns into `assets/index.json`'s `breeds` and `monsters` rows** (memory: cook traps 2026-10-11).
3. `--only tables` for **all twelve worlds**; `kResistances`/`kDropRates` rows.
4. Figures one breed at a time (`--only figures --world aida --monsters <Name> --threads 4`; print the list first and watch `cooked/figures/jobs.txt`), shown in their world beside the spawn (memory).
5. Order: Death Tree + Forest Orc (72-74), Death Rider, Blue Golem (needs the waterfall sheet), Witch Queen, Hell Maine.
- Check: `select number, count(*) from monster_spawns where map=33 group by number` reads 304x6, 305x38, 306x39, 307x36, 308x29, 309x3; headless `--world aida --ticks 30000 --level 100` raises 151.

**Step 8: the travel row(s)** (decision 3). One row "Aida", landing 84,11, `kTravels` 17. For two rows, add the multi-source `settleFound` and its sim_test, plus `floorName`/`placeName` for "Aida 2".

**Step 9: side hooks** per decisions 9-10: feather, Nova scroll, second-class gear, bot world row, NPCs.

---

### 7. Risks and traps

1. **The death row.** No mu.db gate 119 means every death in Aida rises in Lorencia. Check the `.mur` header after step 1.
2. **The Roofed reverb.** `underground` makes `indoors()` true everywhere. Without an Open arm the forest takes the small-room reverb (`play_sound.cpp:427-434`).
3. **The dead clouds** (§3.4). Don't port 91 cloud emitters as MU's.
4. **Bug02 lives in Tarkan's folder.** `crawling()` won't find it for Aida without `CRAWLS_FROM` or a copy.
5. **Butterflies uncapped** would be up to ~200 skinned figures. Cap them (ours).
6. **Flower sprites map-wide** are 2 196 sprites. Cull by distance.
7. **Breed numbers above 255** for the first time (304-309, and 549-552 if taken). Grep found no u8 path, but raise them headless before trusting it.
8. **`dr_idle01.wav`** on disk vs `dr_idle1.wav` in MuMain.
9. **`Aida.mp3`'s capital A** on a case-sensitive server or Linux port. Music is client-only, but the Windows/Linux port test runs it.
10. **Every world's tables** (twelve now) after the kinds go in, plus the index merge for the figure cook.
11. **Noria's tables** must be recooked after `GATE_BOXES_BY_MAP[3]` changes, or the solid-stamping may close a door tile.
12. **The lockstep server.** `gates.cpp` and `maps.cpp` change the sim: the Hetzner server must be rebuilt and redeployed before a client with Aida can play there. Protocol is unchanged, but the sim hash is not.
13. **mu.db is open in another session.** Write only when none is live, then `wal_checkpoint(TRUNCATE)` (memory). The brief's `MU2/mu.db` path is a 0-byte file.
14. **`tools/content.sh --world aida`** rebuilds every stale asset in the tree. Use `tileset.py` + `ground.py` + `asset.sh` by name.
15. **`--only showing`** recompresses every effect sheet (minutes). Batch the waterfall sheets and the air into one showing cook.

---

### 8. Decisions for the user (engine side), with recommendations

1. **Gate levels, Noria <-> Aida.** MU asks 130 in and 10 out. House rule: gates doubled, except where set near the breeds (Atlans in 70 over 43-74; Tarkan in 100 over 72-93). Aida's breeds are 72-98, Tarkan's band. *Recommend* **in 100, out free (0)** as Icarus's way back; Tarkan's 70 out was there to keep Atlans in reach, and Noria needs no guard.
2. **Which breeds.** The base six (304-309, 151 spawns, Season 1-3's Aida) or with Season 4's Bloody four (549-552, 114-120, 95 spawns). *Recommend the base six*; the Bloody breeds would be the hardest monsters in the game by 15 levels.
3. **Warp rows.** None, one "Aida", or S6's two. *Recommend one row* at the gate's level (100) and S6's 8 500 zen. Two only with the multi-source flood (which would also serve a Tarkan 2).
4. **MapRow.air (step 0).** *Recommend yes now*: this is the eighth name-keyed air bool, and the refactor costs no behaviour.
5. **Emitters MU does not draw or we lack.** The dead clouds (none, or ours as faint ground mist), the ghost (leave out), and the butterflies (capped at 3 per anchor). *Recommend:* butterflies capped, no clouds and no ghost to start; judge in game.
6. **Leaves.** MU has none in Aida. *Recommend none* (MU's), or World34's own leaf on the existing pool if the forest reads static.
7. **Music.** *Recommend* `Aida.mp3` in the safe hall only, as Tarkan.
8. **The Karutan door** (gate 336, level 160, to map 80): no row, or a sealed door with the existing "is sealed" line. *Recommend sealed*. It is visible in MU's art and a sealed door explains it.
9. **Drops and hooks:** Loch's Feather, Nova scrolls and the second-class gear on map 33 (`items.h:210-212`, `realm_items.cpp:1322`). *Recommend* pass A's call. Engine-wise each is a one-term edit.
10. **Bots.** Add `{33, "aida", {84, 11}}` to `tools/bot/brain.h` only when bot work resumes (paused since 2026-10-10).

---

### Side findings

- **Brief:** `MU2/mu.db` is 0 bytes; the project's database is `MU2_BGFX/source/mu.db`.
- `src/sim/gates.cpp:75-78` and `realm_travel.cpp` comments still say the experience is 100x. The rate is 20x since 2026-10-07 (memory: exp rate is 20x).
- `src/sim/realm_travel.cpp:120-150`'s floor flood is still sequential (tarkan-port §4.3). Every multi-row open map needs the fix.
- `GATE_BOXES_BY_MAP` (`pipeline/index.py:58-81`) still lacks map 11 (Blood Castle) and map 9 (Devil Square's four landings, devil-square-port D2), and most of map 4's stairs.
- MuMain's `IsInAida()` also covers `WD_54CHARACTERSCENE` (`GMAida.cpp:18-21`). Our `charscene` is world 74's, so nothing changes, but a future port of world 54 would inherit Aida's object rules.
