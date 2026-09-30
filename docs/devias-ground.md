# Devias ground

What Devias's terrain is, read out of MU's own files, and what MU2_BGFX has to make to stand
it up bare. This is the research for the first stages of a port (MU2's `docs/porting-a-world.md`
§0-3a, the stages Noria went through), done 2026-09-29. Nothing is imported yet: every output
below sits in the scratchpad, and `source/world/devias` does not exist.

Devias is **server map 2**. The client ships it as `Data/World3` + `Data/Object3`
(`LEGACY/reference/MuMain/src/bin/Data/World3`). MuMain paths below are under
`LEGACY/reference/MuMain/src/source/`.

Scratch outputs: `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/30347dbe-6e16-41b8-abb6-e97e08b80818/scratchpad/devias/ground/`
(`devias/` = terrain.py output, `tex/dv_*.png` = every decoded sheet, `prev_*.png` = previews,
`sheet_tiles.png` = all fourteen tile sheets on one page).

---

## 0. Which files the client loads

| file | loaded? | evidence |
|---|---|---|
| `EncTerrain3.map` | **yes** | `World/MapInfra/MapManager.cpp:1227` (`EncTerrain%d.map`, iMapWorld = WorldActive+1) |
| `EncTerrain3.att` | **yes** | `MapManager.cpp:1291` (the default branch; only Battle Castle, Crywolf and Kanturu 3 pick `*10+n` variants) |
| `EncTerrain3.obj` | **yes** | `MapManager.cpp:1305` |
| `TerrainHeight.OZB` | **yes**, as `TerrainHeight.bmp` | `MapManager.cpp:1319`; not an ext-height map, so the 8-bit path |
| `TerrainLight.OZJ` | **yes**, as `TerrainLight.jpg` | `MapManager.cpp:1358`, `OpenTerrainLight` `:1361` |
| `EncTerrainTest3.att/.map` | no | nothing in the source names `EncTerrainTest`. An editor's copy: .att differs from EncTerrain3 in 436 tiles, .map in 525/612/822 tiles per plane |
| `Terrain.att/.map/.obj` | no | the same content as EncTerrain3 **unencrypted** — byte-identical after decryption (0 tiles differ in every plane) |
| `Terrain3.att` | no | BUX-XOR only, 1 byte a tile, an older version (5540 tiles differ; chasm cores are NoGround 8 without NoMove) |
| `Terrain3.map` | no | plain, an older version (644/683/959 tiles differ per plane) |
| `TerrainServer.att` | no (client) | the server's copy, plain 1 byte a tile: differs from the client in 512 tiles, all opening the bottom rows y 240-255 (NoMove 4/12 -> 0) plus one tile near (200,50) |

**The file that matters is `EncTerrain3.*`**, which is exactly what `pipeline/terrain.py` reads.
MuMain checks it on load: `TerrainWall[55*256+208] != 5` is a corrupt file for `WD_2DEVIAS`
(`Render/Terrain/ZzzLodTerrain.cpp:210-211`). Our decode gives 5 at row 55, column 208, which
also confirms the grids are `[y, x]` (row = MU's y).

For the server side, OpenMU ships two Devias grids:
`openmu/src/Persistence/Initialization/Resources/Terrain3.att` (= TerrainServer.att within 4
tiles) and **`075_Terrain3.att`**, the 0.75 one. The 0.75 grid is the same layout but older:
7444 tiles differ from the client's, mostly chasm cores 12 -> 8 (5415, still unwalkable), 926
NoMove tiles open, 244 safe-zone tiles not safe. Walkability agrees on the whole; a strict
0.75 port that walls monsters with OpenMU's grid should read `075_Terrain3.att`.
Preview: `prev_attr_openmu075.png`.

---

## 1. The heightmap and the three grids

```
python3 pipeline/terrain.py <Data/World3> devias <out> 3
```

```
height     header says pixels at 1078; reading 1080 as the client does
height     256x256 tiles, 0.00 to 3.78 m over 256 m square  -> height.png
tiles      14 of the slot table in use  -> tiles.png
walkable   61.6% of the map, 10.2% is a safe zone  -> attributes.png
light      256x256, mean [115, 149, 178]  -> light.png
objects    2237 placed, 105 kinds  -> devias.json        (no model missing on disk)
```

**Height.** 0.00 to 3.78 m (byte x 0.015 m; factor 1.5, `ZzzLodTerrain.cpp:733`), mean 1.65 m,
5th-95th percentile 0.72-2.37 m. Histogram (m): <0.25: 1264, 0.25-0.5: 494, 0.5-1: 3186, 1-1.5:
12770, 1.5-2: 35836, 2-2.5: 9927, 2.5-3: 1769, >3: 290. A plateau, not a valley. The only low
ground is the chasms: 867 tiles sit at 0 m and 748 of them are NoGround; the NoGround area
averages 1.32 m against the walkable 1.74 m. The height map is mostly flat with thin dark
trenches along the chasm edges, and bright dots where MU raised the ground under objects
(`prev_height.png`).

**Attributes** (the low byte; the high byte is 0 everywhere):

| value | tiles | meaning |
|---|---|---|
| 0 | 37432 | open |
| 1 | 2965 | safe zone |
| 3 | 1 | safe + 0x02 (one tile) |
| 4 | 10621 | NoMove |
| 5 | 3685 | safe + NoMove (town walls and houses) |
| 8 | 4 | NoGround only |
| 12 | 10778 | NoMove + NoGround — the chasms |
| 13 | 50 | safe + NoMove + NoGround |

Bits present: 0x01 safe 10.2%, 0x02 one tile, 0x04 NoMove 38.4%, 0x08 NoGround 16.5%. **No
0x10 water bit anywhere**: of 2553 water tiles 1375 are NoMove and the rest open (a frozen moat
you can partly walk). Walkable (neither NoMove nor NoGround) 61.6%, against Lorencia's 71.3%
and Noria's 87.0% — the chasms are why.

The safe zone is the town in the **north-east corner**: x 154-255, y 0-~80 (6340 tiles), plus
361 tiles in the north-west stone yard (x 7-29, y 19-79). The spawn gate (mu.db / OpenMU gate
22, `Version075/Gates.cs:128`) is (197,35)-(218,50), 100% safe, 97% walkable; its middle tile
**(207, 42)** is safe, open and snow (slot 0).

Previews (768 px, row 0 at the top = MU y 0):
`prev_height.png`, `prev_layer1.png`, `prev_layer2.png`, `prev_alpha.png`, `prev_attr.png`
(green open, dark red NoMove, purple NoMove+NoGround, yellow safe, orange safe+NoMove).

**Layer 1 (base) and layer 2 (overlay) usage:**

| slot | name | layer1 tiles | layer1 % | layer2 tiles (any) |
|---|---|---|---|---|
| 0 | TileGrass01 | 28715 | 43.8 | 14431 |
| 1 | TileGrass02 | 24668 | 37.6 | — |
| 2 | TileGround01 | 5050 | 7.7 | 196 |
| 3 | TileGround02 | 711 | 1.1 | 1 |
| 4 | TileGround03 | 91 | 0.1 | 1074 |
| 5 | TileWater01 | 2553 | 3.9 | 66 |
| 6 | TileWood01 | 1280 | 2.0 | 199 |
| 7 | TileRock01 | 21 | 0.0 | 5135 |
| 8 | TileRock02 | 919 | 1.4 | 145 |
| 9 | TileRock03 | 865 | 1.3 | — |
| 10 | TileRock04 | 211 | 0.3 | — |
| 11 | TileRock05 | 144 | 0.2 | — |
| 12 | TileRock06 | 232 | 0.4 | — |
| 13 | TileRock07 | 76 | 0.1 | — |
| 255 | none | — | — | 44289 |

**All fourteen slots are painted**, which no ported map has needed yet (Lorencia declares 9,
Noria 9). No extension slots (14+) are used.

Alpha: 49064 tiles 0, 9089 at 255, 7383 partial. 53 tiles carry alpha with no overlay slot,
4828 name an overlay at alpha 0 — both harmless. 2863 tiles have all four corners at 255 (MU
then draws the overlay alone, `ZzzLodTerrain.cpp:2031-2035`); 44125 are overlay-free.

**The pairing table** (base under overlay, tiles with alpha > 0):

```
TileGround01   3981 under TileGrass01     roads fading into snow
TileGrass01    3017 under TileGrass01     snow over snow (a no-op pairing)
TileWater01    2446 under TileRock01      the frozen moat: ice painted over water
TileGrass02    2213 under TileGrass01
TileGrass01    1279 under TileRock01      ice patches on snow
TileGrass01     773 under TileGround03    the town's snowy flagstones
TileGrass02     568 under TileRock01
TileRock03      563 under TileGrass01     SE yard's edge under snow
TileRock02      546 under TileGrass01     NW yard's edge under snow
TileWood01      330 under TileGrass01
TileGrass02     116 under TileWood01
TileGround02    106 under TileGrass01
...                                       (25 pairs over 5 tiles)
alone          49117
```

**Where each slot is** (by 64-tile block, x then y):
- 0/1 snow: everywhere; 1 fills the chasm floors.
- 2 TileGround01: the road network that crosses the whole map.
- 3 TileGround02: house floors, 455 of 711 in the town block (3,0).
- 4 TileGround03: town streets, mostly as overlay (731 of 822 overlay tiles in (3,0)).
- 5 TileWater01: 988 in the SW channel maze (0,3), the moat ring around the town, and two
  ponds mid-map. 2455 of the 2553 carry TileRock01 over them.
- 6 TileWood01: frost patches, east and south (3,2), (2,3), (1,3).
- 7 TileRock01: almost never a base (21 tiles); 4312 overlay tiles — ice over water in the SW
  maze and the moat.
- 8 TileRock02 + 12 + 13: the NW stone yard (0,0), x ~9-43, y ~7-43.
- 9 TileRock03 + 10: the SE stone yard (3,3), x ~207-245, y ~205-240.
- 11 TileRock05 (red carpet): 84 in the town — the church, x 203-215, y 11-30 (matches the
  church music rectangle 205-214 x 13-31, `Scenes/SceneManager.cpp:1007-1017`) — and 60 in the
  SE yard.

**Light.** `TerrainLight` mean **[115, 149, 178]** — strongly blue, against Lorencia's
141, 144, 144 and Noria's 167, 167, 168. Snow lit by a blue baked light.

> **Orientation finding (affects every map, not only Devias).** MuMain decodes TerrainLight
> with `TJFLAG_BOTTOMUP` (`Render/Textures/ZzzTexture.cpp:200`), so its row 0 is the JPEG's
> *bottom* row. `terrain.py` saves `light.png` top-down through PIL and `ground.py`
> (`corner_light`, `quads`: `light[y, x]`) and `Ground::lightAt` (`src/content/ground.cpp:1084`)
> read it unflipped against the height/tiles grids, which are `[y, x]`. On Devias the flipped
> light lines up with the chasms, the moat ring and the town in `attributes.png`
> (`light_flip_check.png`: as-is | flipped | attributes); unflipped it does not. The
> walkable-to-light correlation is 0.25 as stored vs **0.58 flipped** on Devias, 0.30 vs 0.43
> on Lorencia and 0.51 vs 0.63 on Noria. So the baked light looks vertically mirrored on all
> three maps today. Worth confirming with the ground's owner before Devias depends on it; the
> fix would be one `np.flipud` in terrain.py (or at read) and a re-import of every world's
> `light.png`.

**Objects** (stage 4, noted only): 2237 placements over 105 kinds, every named model on disk
in `Object3`. The most placed: Object01 451, Object12 288, Object15 184, Object20 136. Devias
has no `HIDDEN_BY_MAP` / `BLEND_MESH_BY_MAP` / `OPERABLE_BY_MAP` rows in terrain.py yet — those
come from Devias's arms of `Engine/Object/ZzzObject.cpp` (`:2819`, `:3900`, `:4668`) and are
the objects stage's business. The five roofs MU fades indoors are types 81, 82, 96, 98, 99
(`ZzzObject.cpp:3711-3718`).

---

## 2. The tile sheets

Decoded with `pipeline/decode_texture.py` into `tex/dv_*.png`; all fourteen on one page in
`sheet_tiles.png`. Looked at one by one. **As in Noria, the names lie** — here in a snowy
direction: two "grass" sheets are snow, "wood" is frost, "rock01" is ice.

| slot | name | size | what it actually is | proposed material |
|---|---|---|---|---|
| 0 | TileGrass01 | 256 | fresh snowfield, white with pale blue creases and drifts | snow (new) |
| 1 | TileGrass02 | 128 | smoother, brighter snow, softer blue shadows | snow (new) |
| 2 | TileGround01 | 256 | grey gravel / frozen trodden dirt, dark pits — the road | sand or rock (a gravel read) |
| 3 | TileGround02 | 128 | dark brown wooden floorboards with knots — interior floors | plank |
| 4 | TileGround03 | 128 | crazy-paved flagstones with snow packed in the joints | flagstone |
| 5 | TileWater01 | 256 | dark teal water with pale glints | water |
| 6 | TileWood01 | 128 | **not wood**: mottled blue-grey frost / rime over ground | ice or snow (new) |
| 7 | TileRock01 | 256 | **not rock**: pale blue packed ice / blue snow | ice (new) |
| 8 | TileRock02 | 128 | dark blue-grey stone slab, fine pitting | flagstone or rock |
| 9 | TileRock03 | 128 | brown-grey stone slab, same pitting as 8 | flagstone or rock |
| 10 | TileRock04 | 128 | grey marble floor tile, four slabs a sheet, a dark square inlay in the middle | marble (new) or flagstone |
| 11 | TileRock05 | 256 | red carpet with a gold ornamental border down both sides | cloth |
| 12 | TileRock06 | 128 | black marble with gold veins and gold corner ornaments | marble (new) |
| 13 | TileRock07 | 256 | brown granite / rug with a blue-and-gold ornamental border top and bottom | cloth or marble |

Sizes matter for reach (64 texels a tile, `ZzzLodTerrain.cpp:1722`, `:2021-2024`): a 256 sheet
covers 4 tiles, a 128 sheet 2. Slots 10-13 are **patterned, not texture**: an inlay, a
bordered carpet, cornered marble and a bordered rug each read as one decorated floor piece per
repeat, so seam-healing or a `scale` other than 1 would cut the pattern — they want the sheet
laid as MU lays it.

The material library (`source/materials/`) has no snow, ice or marble. Devias needs at least
`snow` and `ice`. Note that the grass pass decides "grows grass" by the slot's recipe
(`src/content/ground.cpp:460-461`: `recipe == "grass"`), so naming the snow slots `grass`
would sow Lorencia's green sward over the snowfield — the snow slots must not take `grass`.

**TileGrass02.OZT has alpha.** 256 x 64 RGBA, 95.3% of it below full alpha: a strip of white,
frost-covered grass blades (`sheet_grass02_ozt.png`). It is MU's grass sprite for slot 1, not
a ground sheet (the `.OZJ` of the same name is the ground). World3 ships **no
TileGrass01.OZT and no TileGrass03.OZT**.

**leaf01 / leaf02** are snowflakes, not leaves: `leaf01.OZJ` 16 x 16, a soft white dot on black;
`leaf02.OZJ` 16 x 16, a six-pointed starburst on black; `leaf01.OZT` 32 x 32, the soft dot with
real alpha (`sheet_leaves.png`). For Devias the client loads the **jpg** leaf01 (see §3).

### Name collisions in the flat pool

`source/textures` (and `assets/textures`) hold Lorencia's sheets **unprefixed**:
`tilegrass01, tilegrass02, tileground01, tileground02, tileground03, tilerock01, tilerock02,
tilewater01, tilewood01`. Nine of Devias's fourteen names would be answered by Lorencia's art
if decoded unprefixed. `cs_` (character scene) and `nr_` (Noria) own their own sets; `dv_*` is
free. **Use `dv_`** for all fourteen, including TileRock03-07, which do not collide today.

The grass sheets have their own pool, `source/effects/grass/`, keyed `<world>_<TileGrassNN>.png`
with the unprefixed `TileGrass0N.png` as Lorencia's fallback. Devias's slot-1 sprite must be
`devias_TileGrass02.png`: without it `src/game/world/grass.cpp:118-133` falls back to the shared
`TileGrass02.png`, which is Lorencia's green lawn. Lorencia's leaf is `source/effects/leaf/leaf01.png`
and `source/world/lorencia/leaf01.png`; Devias's snowflakes need their own names too.

---

## 3. How MuMain draws Devias's ground

Nothing in the terrain draw itself is Devias-specific; the differences are in the frame around it.

- **Tiles**: slots 0-13 load from the world's own folder as `Tile*.jpg`, GL_NEAREST, repeat
  (`MapManager.cpp:1363-1448`); ExtTile01-16 as slots 14-29 (`:1450-1457`), none in World3.
  The UV is 64 texels a tile per sheet, `Width = 64.f / b->Width` (`ZzzLodTerrain.cpp:1722`,
  shader path `:2021-2024`). No per-world UV scale.
- **Blend**: a tile whose four corners are all alpha 1 draws layer 2 alone; all four 0 draws
  layer 1 alone; otherwise both, overlay weighted by vertex alpha
  (`ZzzLodTerrain.cpp:1941-1989`).
- **Water**: slot 5 as a base scrolls in U by `WaterMove = (WorldTime % 20000) * 0.00005`, one
  sheet every 20 s (`ZzzLodTerrain.cpp:3595`, applied `:1754`), with a small V wobble from the
  grass wind (`:1763`). Only when not fully covered by its overlay (`:1948`). Devias: of 2553
  water tiles 486 are fully covered and 2067 scroll — **under ice**: the overlay (TileRock01)
  does not scroll, because the overlay only animates when it is water too (`:1982`). So MU's
  moat is moving water seen through a partly painted ice sheet (mean alpha 204). No Atlanse
  style animated water (that is `WD_7ATLANSE` only, `:1970`) and no `CreateWaterTerrain`
  (Hellas only, `World/GameMaps/GMHellas.cpp:43-58`).
- **Light**: `TerrainLight.jpg` times `DotProduct(normal, (0.5,-0.5,0.5)) + 0.5` per vertex
  (`CreateTerrainLight`, `ZzzLodTerrain.cpp:517-544`), the same for every map but Battle Castle.
  `OpenTerrainLight` reads it bottom-up (`ZzzLodTerrain.cpp:572` -> `ZzzTexture.cpp:200`).
- **Clear and fog colour**: `SetClearAndFogColor(0.75, 0.85, 1.0)`, "light snowy blue"
  (`Scenes/SceneManager.cpp:383-384`), against Lorencia's (10, 20, 14)/256 dark green (`:381-382`).
- **Grass**: drawn on Devias (the pass skips only Atlanse and Doppelganger 3,
  `ZzzLodTerrain.cpp:3619`; Chaos/Battle Castle turn it off at `:364-366`). See §4.
- **Indoors** is a tile test, `HeroTile = TerrainMappingLayer1[hero's tile]`
  (`Engine/Object/ZzzInterface.cpp:3365`). On Devias "indoors" is **slot 3 or any slot >= 10**
  (planks, marble, carpets): it stops the snow (`Scenes/MainScene.cpp:81`, `:105`, `:555`),
  stops the wind loop (`SceneManager.cpp:862-866`), fades the roofs 81/82/96/98/99
  (`ZzzObject.cpp:3711-3718`), switches the footstep from snow (`ZzzCharacter.cpp:5356-5358`)
  and gates NPC talk across the floor line (`Input/Selection.cpp:192-198`). 1374 tiles are
  indoor by this rule, 888 of them in the safe zone. Lorencia's rule is slot 4 alone, and
  MU2_BGFX has it as a constant: `kIndoorFloor = 4` (`src/game/world/world.cpp:46`, `:302`).
- **Snow**, not leaves: `CreateDeviasSnow` (`Render/Effects/ZzzEffectFireLeave.cpp:274-297`) —
  BITMAP_LEAF1 at scale 5, one in ten BITMAP_LEAF2 at scale 10, spawned in a box -800..799 x
  -500..899 around the hero, 200-399 units above him, falling 8-23 units a frame, tilted -30°.
  Drawn as billboards under EnableAlphaBlend (`:525-535`, `:568-570`), up to MAX_LEAVES 200
  (`Core/Globals/_define.h:461`). For map 2 BITMAP_LEAF1 is **leaf01.jpg** (only maps 0, 3 and
  63 take the .tga, `MapManager.cpp:1477`), BITMAP_LEAF2 leaf02.jpg (`:1479`): black-backed
  sheets.
- **Wind** loop plays outdoors, stops indoors (`SceneManager.cpp:862-866`); Devias music in the
  safe zone, church music in 205-214 x 13-31 (`SceneManager.cpp:1007-1017`).

---

## 4. Grass on Devias

MuMain draws grass as a camera-facing strip on each tile whose four corner alphas are all 0,
using `BITMAP_MAPGRASS + layer1` (`ZzzLodTerrain.cpp:2077-2085`), `Height = sheet height x 2`
(`:2090`) — 64 px -> 128 units, 1.28 m — shifted and swayed by the grass wind. The sheets are
`TileGrass01/02/03.tga` from the world's folder (`MapManager.cpp:1465-1472`), and a missing
one simply draws nothing (`Bitmaps.FindTexture` returns null, `ZzzLodTerrain.cpp:2087`;
`Render/Sprites/GlobalBitmap.cpp:509-519`). The previous world's are unloaded on a map change
(`MapManager.cpp:1543`).

For Devias that means:
- **slot 0 (TileGrass01, 43.8%) grows nothing** — no TileGrass01.OZT in World3;
- **slot 1 (TileGrass02, 37.6%) grows frosted white tufts** from TileGrass02.OZT, on the 20209
  slot-1 tiles with no overlay at any corner (30.8% of the map; the chasm floors among them);
- slot 2 would take TileGrass03.tga, which is absent — the roads grow nothing.

So MU's Devias has a thin white grass on the smoother snow and bare snowfield elsewhere. The
leaf01/leaf02 textures are the snowfall, not grass.

MU2_BGFX's grass (`docs/grass.md`, `src/game/world/grass.cpp`) grows its procedural sward on
slots whose recipe is `grass`, and MU's painted tuft per `<world>_<slot name>.png`. A Devias
that takes `snow` for slots 0-1 grows nothing at all; one that wants MU's frosted tuft on slot 1
needs a way to say "this non-grass slot still carries MU's tuft" (a ground.json flag or a
per-world grass rule) and `source/effects/grass/devias_TileGrass02.png` behind it, plus the
`grass_devias_1` key in `pipeline/index.py`'s EFFECTS table beside `grass_noria_0` (`:541-542`).
Whether the sward shader should draw white frosted blades there is a look decision.

---

## 5. The joins

OpenMU 0.75 (`Version075/Gates.cs`):
- Lorencia's west edge -> Devias: enter gate 18 at Lorencia (5,38)-(6,41) (`:193`) lands on
  Devias exit gate 19, (242,34)-(243,37) (`:129`).
- Devias's east edge -> Lorencia: enter gate 20 at (244,34)-(245,37) (`:194`) -> gate 21.
- Devias SW corner -> Lost Tower: enter gate 28 at (2,248)-(3,249), level 40 (`:197`); the
  return lands on Devias gate 44 at (2,246)-(3,247) (`:130`).
- Devias spawn / town gate 22: (197,35)-(218,50), spawn (`:128`); warp list "Devias" 2000 zen,
  level 20 (`:47`).

So Devias sits west of Lorencia with its east edge on Lorencia's west edge: Devias x 244 lands
at Lorencia x 5 and y 34 at y 38, which is the (-239, +4) offset the combined MU4 heightmap
used. The Lorencia-side gate tiles in Devias (244-245, 34-37) are safe and floored with slots 7
and 4.

mu.db (`source/mu.db`) today has gates 17 (Lorencia) and 27 (Noria) only, and no monster or
NPC rows for map 2.

---

## 6. Checklist: Devias's bare ground in MU2_BGFX

Mirroring Noria's bring-up (`9a2f6110`, 2026-09-28, and the import before it). Run from
`MU2_BGFX/`; `W=../LEGACY/reference/MuMain/src/bin/Data/World3` (this tree has no
`reference/` of its own until `tools/fetch_mumain.sh` runs).

**Stage 1 — grids** (import, kept in git)
```
python3 pipeline/terrain.py $W devias source/world 3
```
-> `source/world/devias/{height,tiles,attributes,light}.png`, `devias.json`. Expect the
numbers in §1. Decide the TerrainLight flip first (§1), since light.png is the file it changes.

**Stage 2 — sheets** (import)
```
for n in TileGrass01 TileGrass02 TileGround01 TileGround02 TileGround03 TileWater01 TileWood01 \
         TileRock01 TileRock02 TileRock03 TileRock04 TileRock05 TileRock06 TileRock07; do
  python3 pipeline/decode_texture.py $W/$n.OZJ source/textures/dv_$(echo $n | tr A-Z a-z).png
done
python3 pipeline/decode_texture.py $W/TileGrass02.OZT source/effects/grass/devias_TileGrass02.png
python3 pipeline/decode_texture.py $W/leaf01.OZJ  <snow flake 1, a devias_ name>
python3 pipeline/decode_texture.py $W/leaf02.OZJ  <snow flake 2, a devias_ name>
```
Then write `source/world/devias/ground.json` on Noria's shape (`tileset: devias`,
`profile: tiles`, all **fourteen** `sheets` -> `dv_*.png`, `sheet_materials` for each slot and
its `_over`, notes). New materials `source/materials/snow.json` and `ice.json` (and `marble`
if slots 10/12 get one) — without them tileset.py falls back to the profile default.

**Stage 3 — the ground** (build, not in git)
```
./tools/content.sh --world devias      # tileset.py + ground.py into workshop/world/devias, then index.py
./tools/sync.sh --world devias --only-world
./tools/cook.py --world devias --only ground
```
(`content.sh:55-61` runs `tileset.py source/world/devias/ground.json workshop/world/devias` and
`ground.py source/world/devias workshop/world/devias source/textures`.) Read the pairing table
in the ground.py log against §1. Expect more primitives than Noria's 30: fourteen slots and 25+
pairs.

**Stage 3a — light, grade and the row** (code/sheets — the other session's files, listed only)
- `sheets/worlds/devias.json`: Devias's light over the base sheet (`game/world/maps.h`
  `mapSheet`). Start from the facts: TerrainLight mean 115/149/178, MU's clear/fog colour
  (0.75, 0.85, 1.0).
- `source/grades/devias.json`: start as an exact copy of `lorencia.json`, then tune.
- `src/game/world/maps.cpp` `kMaps`: `{"devias", 2, {207, 42}}` (the middle of gate 22).
- `source/mu.db` gates: a spawn row for map 2, (197,35)-(218,50), spawn 1 (OpenMU gate 22);
  checkpoint the WAL before committing.
- `src/game/ui/minimap.cpp:214-218` `mapName`: case 2 "Devias" (`arrival.cpp:59-61` already has
  the row).
- `src/game/world/world.cpp:46` `kIndoorFloor`: per world — Devias is slot 3 or >= 10.
- Water: MU scrolls the moat under the ice; Noria's sheet holds water still (`water_flow 0`).
- Grass: §4 — no field on snow unless slot 1 is allowed MU's frosted tuft.
- Snow in place of leaves (`leaves.cpp`), outdoors only — a later stage, noted here because
  it hangs off the same indoor rule.

Stages 4-6 (Object3's 105 kinds, NPCs, spawns, effects) are not part of the bare ground.
