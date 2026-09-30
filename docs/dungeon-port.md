# Dungeon port: the record, the data, and how it plugs in

Research notes, 2026-09-30. The Dungeon is MU server map **1**, client `Data/World2` + `Data/Object2`
(MuMain's `WD_1DUNGEON`), entered from Lorencia by the stair under `DoungeonGate01`. Three research
passes wrote this page and it follows `docs/devias-port.md`'s shape. Part A is the record: gates,
warps, spawns, breeds, drops and rules. Part B is the data and the look. Part C is the engine
side, ending in the build steps. Nothing but this page was edited, and no cook or window was run.

Paths: MuMain source `LEGACY/reference/MuMain/src/source/` (MM), MuMain data
`LEGACY/reference/MuMain/src/bin/Data/` (MMD, or D), OpenMU init
`LEGACY/reference/openmu/src/Persistence/Initialization/` (OM), OpenMU game logic
`LEGACY/reference/openmu/src/GameLogic/` (OMGL). The scratch folder
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/cf726526-cf00-4031-ad27-6547a1f84c3f/scratchpad/dungeon/`
holds the following:
- `pipeline/terrain.py` output for World2;
- every Object2 model and the six missing monsters exported through MuExtract;
- decoded sheets and previews;
- the flood-fill script.

## The one thing to know first

**Unlike Devias, the Dungeon's grid is 0.75.** MuMain's `EncTerrain2.att` is byte for byte OpenMU's
Season 6 `Terrain2.att`. Inside the three walkable floors, not one tile's blocking differs from
`075_Terrain2.att`. The 1214 tiles that differ are unreachable strips on the map's border. The
trap placements sit exactly on OpenMU 075's trap spawn tiles. So the client grid can be used as
is, and there is no "not 0.75" corner to rule on (A §1.5, B opening).

## In one screen

- **One map, three floors.** Dungeon 1 (south, about 14 390 tiles), Dungeon 2 (north, 9 426) and Dungeon 3
  (west pocket, 3 415) are three disconnected regions of one 256² grid. The gates are the only way
  between them, and six of those gates lead to the same map. The engine cannot do that today (C §4).
- **The ways in.** Lorencia's gate 1 (121,232-123,233) asks level 20, leads to exit 2 (107-110,247) and
  sits under the arch. The inner stairs all ask 20 and the way out asks 0. The warp list asks
  3000/3500/4000 zen at level 30/40/50.
- **No safe tile anywhere.** Death and the Town Portal go to **Lorencia** (OpenMU: no spawn gate means
  Lorencia). `Realm::haven` cannot leave the map (C §2.5, §4.1).
- **Twelve breeds, levels 19-55**, all respawning in 10 s: Skeleton Warrior, Larva, Cyclops, Ghost,
  Skeleton Archer, Hell Hound, Hell Spider, Elite Skeleton, Thunder Lich, Poison Bull, Dark Knight
  and the Gorgon (55, 6000 HP, boss by numbers only). There are also **58 traps**, which cannot be
  hit, never die and fire on a timer. mu.db has only the Skeleton Warrior.
- **Figures.** The Hell Hound, Poison Bull, Thunder Lich and Skeleton Warrior re-dress figures that
  are already cooked. **New:** Monster04 Dark Knight, 07 Larva, 08 Ghost, 09 Hell Spider, 11 Cyclops
  and 12 Gorgon; the Skeleton02/03 bodies; the Cyclops's Crescent Axe (Axe09); and the three trap
  objects.
- **The look is MU's black.** The clear colour is black, and there is no sky, fog, weather or grass.
  TerrainLight (blue-violet, mean 108,108,140) carries the ground. **120 flickering torches** are the
  only moving light. Types 22-24 scroll their UVs, 29 hidden markers drop stones, and up to 5 bats and
  3 rats run about. MU loops `aDungeon.wav` and plays `Dungeon.mp3` (B §2).
- **Size.** There are 4 488 placements over 61 kinds, 1.6× Lorencia. The walls are objects:
  1 262 `Object01` blocks, and the ground itself is flat. 40% of the map is NoGround void, which
  Devias's chasm work already draws as black.

## Traps a first build would fall into

1. **Grass in the dungeon.** Slot 0 is named `TileGrass01` but is the stone floor, 75.7% of the
   tiles. Set `GRASS_BY_MAP[1] = []` and do not give it the `grass` recipe (C §3 row 5).
2. **Leaves and wind underground.** `World::raiseAirs` opens leaves for every world but Devias, and
   `windy_` is true for every world but Noria (C §3 rows 6-7).
3. **Indoors flips at random.** `kIndoorFloor = 4` is TileGround03 here, 0.6% of tiles (C §3 row 8).
4. **Same-map gates lock up.** `travel()` returns when the target world is the current one, and
   `gated_` is never cleared, so every later gate goes dead (C §4).
5. **`GROUNDED_TYPES` in `tools/cook.py` is not keyed by world** for types 20-27 (B §3).
6. **Sheet name collisions.** Object2's `bons`, `cobweb`, `wood01` and `TileRock01` collide, and so do
   the tiles, so every Dungeon sheet takes a prefix. **Use `dn_`**: the reports proposed both `dn_`
   and `dg_`, and this page settles on `dn_`.

## Where the three reports disagree, settled

- **Gate 1 is already in the gate table, sealed.** Part C §2.1/§2.6 says gates 1 and 4 are missing.
  That was true of the committed tree. **Another session's uncommitted `src/sim/gates.cpp`**
  (untracked, modified 2026-09-30 11:56) already carries `{1, 0, {121,232,123,233}, 20, -1, "The
  Dungeon"}`, sealed, and its `play.cpp`/`play_mode.cpp` edits are in flight. Build step 2 unseals
  that row. Read `git diff` and the file first, and land after that session's gate work, never over it.
- **Which placements are hidden.** Part B lists `HIDDEN_BY_MAP[1] = {39, 40, 51, 52, 60}`; Part C lists
  `{52, 60}`. MU hides 39/40/51 as scenery because the live trap monster draws the same model.
  **Take B's set.** The traps come back as bodies in the trap step, and until then the dungeon shows
  no trap, as MU would without its server.
- **Dungeon 1's size.** It is 14 392 tiles on the 075 grid and 14 386 on the client grid. The six tiles
  are border slivers and change nothing.

## Decisions for the user

Each has a recommendation. None blocks step 1.

| # | question | recommendation |
|---|---|---|
| 1 | OpenMU lists the spawn table **twice**: 512 monsters on 258 spots, two to a tile. | **258** (one per spot) until a 0.75 MonsterSetBase says otherwise. Two bodies on one tile read as a transcription slip, and 512 is dense for a single player. |
| 2 | All 27 **Lance Traps** (and one Iron Stick) stand on blocked tiles, so OpenMU's "fire when pressed" never fires. | Fire them at their own `AttackRange` 4 in the facing direction, as the Fire Trap does. Mark it ours. |
| 3 | The client has a **59th trap** (Fire, 69,73) that OpenMU lacks. | Leave it out: OpenMU is the record. |
| 4 | The **Gorgon's respawn** is 10 s, like every breed. | Keep 10 s (OpenMU), unless you want a boss pace like the Ice Queen's 50 s. |
| 5 | Exit 6 overlaps enter 7 on two tiles. | Draw the landing outside every enter box (ours, small). |
| 6 | **Levels.** 20 on foot, 30/40/50 by warp. The M key is free today. | Enforce 20 at the gates (the table already carries it). Leave M free, as it is for Noria and Devias. |
| 7 | **Music.** MU plays `Dungeon.mp3` through the whole map. The house rule is that music is rare and in fights. | Ambient `aDungeon.wav` always, and the track only as a fight track. Your call. |
| 8 | **The sun.** MU draws body shadows in every map. | Try a weak straight-down "shadow sun" against no sun on the first shots. No sun saves about 1 ms. |
| 9 | **TileWater01** is near-black ground, not water. | Hold it still (`water_flow 0`), as Noria and Devias do. |

**No 0.97d cross-check was possible.** `OM/Version097d/` holds only `Items/Jewels.cs`. 0.95d and
Season 6 inherit Version075's Dungeon unchanged (A, version note).

## The build, in order

Part C §6 has the file and line touch-points and a headless check for each step. In short:

1. **Bare land, reachable by M.** Set the terrain.py tables (hidden, operable, grass none), run
   `terrain.py $D/World2 dungeon source/world 2`, add `ground.json` with `dn_` sheets, then content,
   sync, `cook --only ground` and `--only tables`. Add the `kMaps` row `{"dungeon", 1, {108,247}}`,
   the arrival banner and the minimap name.
2. **Lorencia's gate 1 and the way back.** Unseal gate 1 and add exits 2/4 and enter 3. Add a
   sim_test for level 20 and 19.
3. **The three floors.** A same-map gate in `Realm::throughGate` with its own `What`, which the game
   draws through `Play::warped()`. Add gates 5-16.
4. **The look.** `sheets/worlds/dungeon.json`, and an "underground" switch for leaves, wind,
   indoors and the room. Run `--budget` on 4 488 placements.
5. **Objects.** One recipe per kind by count: Object01, 04, 48, 49 first. Torches carry fire
   emitters, and types 22-24 scroll.
6. **Death and the portal home.** `safe_map` in the `.mur` (v10), and a travel to Lorencia. While
   there, fix Devias's missing gate-22 spawn row.
7. **Monsters, breed by breed.** Re-dresses first, then the six new models, then Skeleton02/03 and
   Axe09.
8. **Traps.** A trap body in the sim. Its own sprint.
9. **Airs.** Bats, rats, falling stones, and the ambient loop.

**Side finding.** Devias has no safe gate today. mu.db has no row 22, so a death or a Town Portal
in Devias revives him where he stands (C §2.5).

---

## Part A: the record

Research notes, 2026-09-30. The Dungeon is MU server map **1**, client `Data/World2` + `Data/Object2`
(MuMain's `WD_1DUNGEON`). This page is the map's *record* -- map row, gates, warp, monsters, traps,
drops, rules -- in the style of `docs/devias-port.md` §1-2. Nothing in the repo was edited.

Paths: OpenMU init `LEGACY/reference/openmu/src/Persistence/Initialization/` (OM), OpenMU game
logic `LEGACY/reference/openmu/src/GameLogic/` (OMGL), MuMain source
`LEGACY/reference/MuMain/src/source/` (MM), MuMain data `LEGACY/reference/MuMain/src/bin/Data/`
(MMD). Scratch (the region script and its pickle):
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/cf726526-cf00-4031-ad27-6547a1f84c3f/scratchpad/dungeon/`.

**Version cross-check, once for the whole page.** OpenMU has no separate Dungeon for the other
versions: `OM/Version095d/GameMapsInitializer.cs:31` yields `Version075.Maps.Dungeon` itself, and
`OM/VersionSeasonSix/Maps/Dungeon.cs:12,25` subclasses it and changes only
`TerrainVersionPrefix => string.Empty` (so Season 6 reads `Terrain2.att` instead of
`075_Terrain2.att`). `OM/Version097d/` holds only `Items/Jewels.cs` -- there is no 0.97d map or gate
data in this tree. So: spawns, breeds, traps are one table for 0.75 / 0.95d / S6; gates and warps
are compared per file below; the terrain differs (§1.5).

---

### 1. The record, from OpenMU Version075

#### 1.1 Map

`OM/Version075/Maps/Dungeon.cs:21` `Number = 1`, `:26` `Name = "Dungeon"`. Everything else comes
from the shared `OM/BaseMapInitializer.cs:109-130`:

- **Terrain**: `TerrainVersionPrefix => "075_"` (`OM/Version075/Maps/BaseMapInitializer.cs:27`), and
  the file is `Terrain{Number+1}` (`OM/TerrainUpdateHelper.cs:48-50`), so **`OM/Resources/075_Terrain2.att`**
  (65539 bytes: 3-byte header, then one byte a tile, index `y*256 + x`,
  `OMGL/GameMapTerrain.cs:48,136-140`). Walkable is byte 0 or 1, safe is byte 1.
- **Exp multiplier 1** (`OM/BaseMapInitializer.cs:119`, the same for every map; nothing overrides it).
- **No map requirements** (`CreateMapAttributeRequirements` is not overridden in Dungeon.cs).
- **Drop groups**: the defaults only (`InitializeDropItemGroups` not overridden, `:171-179`) -- see §1.7.
- **No spawn gate, so no safe zone of its own and death/Town-Portal land in Lorencia.**
  `OM/BaseMapInitializer.cs:91`: `SafezoneMapNumber => ExitGates.Any(g => g.IsSpawnGate) ? this map : Lorencia`.
  None of the Dungeon's seven exit gates is a spawn gate (§1.2), so `SafezoneMap = Lorencia (0)`.
  See §1.8.
- **No NPCs** (`CreateNpcSpawns` not overridden).

#### 1.2 Gates (`OM/Version075/Gates.cs`)

Direction numbers are OpenMU's `Direction` (`OM/../DataModel/Configuration/Direction.cs`: 1 West,
2 SouthWest, 3 South, 4 SouthEast, 5 East, 6 NorthEast, 7 North, 8 NorthWest). The (dx,dy) is
`OMGL/DirectionExtensions.cs:25-33` `CalculateTargetPoint`, the way `src/sim/gates.h:31-33` stores
a facing: **1 West = (-1,-1)**, **3 South = (+1,-1)**.

Exit gates (where you come out), `CreateExitGate(map, x1, y1, x2, y2, direction, isSpawnGate)`:

| gate | map | box | dir | (dx,dy) | role | line |
|---:|---|---|---|---|---|---|
| **2** | Dungeon | 107,247 - 110,247 | 1 West | (-1,-1) | out of Lorencia's gate 1: the Dungeon 1 arrival; **Move-window target "Dungeon"** | `Gates.cs:119` |
| **6** | Dungeon | 231,126 - 234,127 | 1 West | (-1,-1) | out of gate 5: Dungeon 2 arrival; **warp "Dungeon2"** | `:120` |
| 8 | Dungeon | 240,149 - 241,151 | 3 South | (+1,-1) | out of gate 7: back up in Dungeon 1 | `:121` |
| **10** | Dungeon | 3,83 - 4,86 | 3 South | (+1,-1) | out of gate 9: Dungeon 3 arrival; **warp "Dungeon3"** | `:122` |
| 12 | Dungeon | 3,16 - 6,17 | 3 South | (+1,-1) | out of gate 11: back up in Dungeon 2 | `:123` |
| 14 | Dungeon | 29,125 - 30,126 | 1 West | (-1,-1) | out of gate 13: Dungeon 3, second way in | `:124` |
| 16 | Dungeon | 5,32 - 7,33 | 1 West | (-1,-1) | out of gate 15: back up in Dungeon 2 | `:125` |
| **4** | Lorencia | 121,231 - 123,231 | 1 West | (-1,-1) | out of the Dungeon's gate 3: in front of the dungeon arch | `:114` |

None has `isSpawnGate = true`.

Enter gates (boxes you walk into), `CreateEnterGate(number, target, x1, y1, x2, y2, level)`:

| gate | on | box | to exit | level | line |
|---:|---|---|---|---:|---|
| **1** | Lorencia | 121,232 - 123,233 | 2 (Dungeon 1) | **20** | `Gates.cs:185` |
| **3** | Dungeon | 108,248 - 109,248 | 4 (Lorencia) | 0 | `:186` |
| 5 | Dungeon | 239,149 - 239,150 | 6 (Dungeon 2) | 20 | `:187` |
| 7 | Dungeon | 232,127 - 233,128 | 8 (Dungeon 1) | 20 | `:188` |
| 9 | Dungeon | 2,17 - 2,18 | 10 (Dungeon 3) | 20 | `:189` |
| 11 | Dungeon | 2,84 - 2,85 | 12 (Dungeon 2) | 20 | `:190` |
| 13 | Dungeon | 5,34 - 6,34 | 14 (Dungeon 3) | 20 | `:191` |
| 15 | Dungeon | 29,127 - 30,127 | 16 (Dungeon 2) | 20 | `:192` |

So every internal gate asks **level 20**, the same as the way in; only the way out (3) asks 0.
Every exit box sits a tile or two off its paired enter box, facing away from it, as Lorencia's
and Devias's do: exit 2 (row 247) is one row below enter 3 (row 248); exit 6 (rows 126-127)
overlaps enter 7 (rows 127-128) on row 127 at x 232-233 -- **a character landing on 232,127 or
233,127 is standing in gate 7** (see Open questions); exit 8 (x 240-241) is one column past enter 5
(x 239); exit 10 (x 3-4, y 83-86) is beside enter 11 (x 2, y 84-85); exit 12 (y 16-17) is beside
enter 9 (x 2, y 17-18); exit 14 (y 125-126) is below enter 15 (y 127); exit 16 (y 32-33) is below
enter 13 (y 34).

0.95d is identical (`OM/Version095d/Gates.cs:117,122-128,194-201`), and so is Season 6
(`OM/VersionSeasonSix/Gates.cs:142,148-154,502-509`). No 0.97d gate data exists in the tree.

**The three floors are three disconnected regions of one 256x256 map.** Flood-filling the walkable
tiles of `075_Terrain2.att` (8-neighbour, byte 0 or 1; scratch `dungeon/regions.py`):

| floor | tiles | bounding box | gates in it | how you get there |
|---|---:|---|---|---|
| **Dungeon 1** | 14392 | x 1-250, y 141-250 | exit 2 + enter 3 (south edge, x 107-110), enter 5 + exit 8 (east, x 239-241, y 149-151) | from Lorencia by gate 1 |
| **Dungeon 2** | 9426 | x 1-249, y 2-129 | exit 6 + enter 7 (east, x 231-234, y 126-128), enter 9 + exit 12 (west, x 2-6, y 16-18), enter 13 + exit 16 (west, x 5-7, y 32-34) | from D1 by gate 5 |
| **Dungeon 3** | 3415 | x 1-46, y 50-132 | exit 10 + enter 11 (x 2-4, y 83-86), exit 14 + enter 15 (x 29-30, y 125-127) | from D2 by gate 9 **or** gate 13 |

Every gate tile lies on byte 0 in its floor's region, except exit 8's 241,151 (byte 4, blocked --
the realm must pick one of the other five). Season 6's `Terrain2.att` gives the same three regions
to the tile (14392 / 9426 / 3415). Dungeon 3 is a pocket in the west of the map, beside D2, and
the two D2->D3 pairs (9->10 and 13->14) both come out in it; nothing links D3 onward in 0.75.
The 075 grid also has a 609-tile strip at y 252-255 and edge pieces at x 243-255 that no gate
reaches -- unreachable, no spawns (§1.5).

#### 1.3 Warp list (the Move window)

`OM/Version075/Gates.cs:48-50`:

| index | name | zen | level | lands on |
|---:|---|---:|---:|---|
| 5 | Dungeon | **3000** | **30** | gate 2 (D1, 107-110,247) |
| 6 | Dungeon2 | **3500** | **40** | gate 6 (D2, 231-234,126-127) |
| 7 | Dungeon3 | **4000** | **50** | gate 10 (D3, 3-4,83-86) |

0.75 had no graphical Move menu, only the text command (`:40`). 0.95d is the same with the same
indices (`OM/Version095d/Gates.cs:46-48`); Season 6 renumbers them 8/9/10 with the same zen, level
and gates (`OM/VersionSeasonSix/Gates.cs:54-56`). Note the walk-in asks level 20 but the warp asks
30/40/50. MU2_BGFX's M key is free and unlevelled (devias-port.md §1.3), so neither is enforced yet.

#### 1.4 Lorencia's arch: DoungeonGate01 against gate 1

`source/world/lorencia/lorencia.json` `objects[1583]`: `{"type": 55, "model": "DoungeonGate01",
"at": [12250.0, 23400.0, 165.0], "angle": [0,0,360], "scale": 1.0}` -- at 100 units a tile,
**tile (122.5, 234.0)**, 1.65 m up. The only one in Lorencia. (`placements.json` does not list
objects; its note at line 486 files "the dungeon gate" among things "you walk on or through", so
it is not a solid.)

Against the record: enter gate 1 is x 121-123 (centre **122.5**) by y 232-233 (232.0 to 234.0),
and exit 4 is x 121-123 on row 231. So the arch stands exactly on the gate's centreline, with its
face on the far (y = 234.0) edge of the enter box: you walk north up rows 231 -> 232 into the
arch's mouth and are taken. It matches. `075_Terrain1.att` agrees: rows 232-233 are open only at
x 121-123 (a three-wide pocket), row 234 and above are all byte 4 (blocked), and row 231 opens
out from x 120 eastward:

```
       119 120 121 122 123 124 125
 234     4   4   4   4   4   4   4
 233     4   4   0   0   0   4   4     <- enter gate 1
 232     4   4   0   0   0   4   4     <- enter gate 1
 231     4   0   0   0   0   0   0     <- exit gate 4 (121-123)
```

`src/sim/gates.cpp` today has no gate 1/4: its tables are Lorencia<->Noria (23-26),
Lorencia<->Devias (18-21) and the sealed Lost Tower 28 (`gates.cpp:9-32`).

#### 1.5 Terrain attributes (075 vs Season 6)

`075_Terrain2.att` and `Terrain2.att` differ on **2214 of 65536 bytes**. Byte counts:

| file | 0 open | 4 blocked | 6 | 8 no ground | 12 | 14 | safe (1) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 075_Terrain2.att | 28503 | 10778 | 3 | 22587 | 3665 | -- | **0** |
| Terrain2.att (S6) | 27289 | 11994 | -- | 21592 | 4660 | 1 | **0** |

**Not one safe tile** in either: the Dungeon has no safe zone at all. The three floors are the
same regions, tile for tile, in both; the 1214 extra open tiles in 075 are exactly its unreachable
pieces: 609 (y 252-255) + 341 (x 243-255, y 140-255) + 72 + 68 (x 252-255) + 180 in 21 slivers =
1270 in 075, against 56 in S6. Nothing that can be walked to differs in openness.
**The client grid is fine for 0.75.** `MMD/World2/EncTerrain2.att`, decoded with
`pipeline/terrain.py`'s own `attributes()` (copied to scratch, not run in place), is
**byte-for-byte Season 6's `Terrain2.att`** (100.0% of tiles) and agrees with `075_Terrain2.att` on
96.6%; inside the three floors **not one tile's blocking differs** from 075. Words used: 0, 4, 8,
12, 14; no safe bit, nothing in the high byte. MuMain's own sanity tile passes:
`MM/Render/Terrain/ZzzLodTerrain.cpp:207-208` asserts `TerrainWall[120*256 + 227] == 4` for the
Dungeon, and the decoded grid has 4 at column 227, row 120. Unlike Devias (devias-port.md §1.6),
there is no "not 0.75" corner to rule on here.

#### 1.6 Monsters

**Spawns.** `OM/Version075/Maps/Dungeon.cs:47-558`, traps `:560-618`. Unlike Lorencia, Noria and Devias there is
**not one area row**: every row is the single-tile overload
`CreateMonsterSpawn(id, def, x, y[, direction])` (`OM/BaseMapInitializer.cs:269-270`), i.e. box
x..x, y..y, quantity 1. So the Dungeon is 512 fixed spots, each a monster that respawns where it
stood -- MonsterSetBase's "spot" rows, whose scatter distance OpenMU's regex drops
(`OM/BaseMapInitializer.cs:189-192`, the same caveat as devias-port.md §1.5).

**The table is in twice.** Ids 101-359 (`:47-304`, 258 rows) and ids 361-616 (`:305-558`, 254 rows)
are the same spots in the same order: every row of the second block is a row of the first, and the
first has four the second lacks -- Skeleton Warriors at 98,221 / 100,220 / 100,225 / 108,232
(ids 107-110, `:53-56`). So each spot holds **two** monsters on the same tile (four spots hold one).
Whether that doubling is 0.75's MonsterSetBase (two copies of the section, or a count of 2 per
spot) or a transcription slip is not answerable from this tree -- see Open questions. It halves or
doubles the whole map's density, so it wants a ruling before cooking. Ids skip 300, 360, 400, 500,
600; nothing hangs on that.

Every non-trap spot stands on a byte-0 tile inside one of the three floors (checked against
`075_Terrain2.att`), except four Thunder Lich rows: **18,29 and 18,23 (ids 206/207 and 463/464,
`:152-153,406-407`) are each on a sealed 3x3 island** (x 17-19, y 28-30 and y 22-24) in Dungeon 2's
west, near gates 9/13 -- no path reaches them, and a Lich has attack range 4, so they are turrets
that shoot into the passage and cannot be reached on foot. The client grid has the same two
islands (§1.5), so this is the map's intent, not a transcription error.

Per floor, both blocks counted (ids 101-616), traps apart:

| breed | # | D1 | D2 | D3 | total |
|---|---:|---:|---:|---:|---:|
| Hell Hound | 5 | -- | 28 | -- | 28 |
| Poison Bull | 8 | -- | 14 | 26 | 40 |
| Thunder Lich | 9 | -- | 22 + 4 on islands | 16 | 42 |
| Dark Knight | 10 | -- | -- | 14 | 14 |
| Ghost | 11 | 104 | 10 | -- | 114 |
| Larva | 12 | 44 | -- | -- | 44 |
| Hell Spider | 13 | -- | 18 | -- | 18 |
| Skeleton Warrior | 14 | 60 | 2 | -- | 62 |
| Skeleton Archer | 15 | 14 | 22 | -- | 36 |
| Elite Skeleton | 16 | -- | 42 | 2 | 44 |
| Cyclops | 17 | 56 | 6 | -- | 62 |
| **Gorgon** | 18 | -- | -- | **8** | 8 |
| **monsters** | | **278** | **168** | **66** | **512** |
| Lance Trap | 100 | 2 | 25 | -- | 27 |
| Iron Stick Trap | 101 | 9 | 17 | -- | 26 |
| Fire Trap | 102 | 1 | 4 | -- | 5 |

(One monster per spot instead: D1 141, D2 84, D3 33 -- 258 spots; the first block alone.)

So: **Dungeon 1** is the skeleton-and-ghost floor (levels 19-34: Skeleton Warrior, Larva, Cyclops,
Ghost, a few archers); **Dungeon 2** the middle (32-48: Elite Skeletons, Hell Hounds, Thunder Liches,
archers, Hell Spiders, Poison Bulls, and the trap corridors); **Dungeon 3** the deep pocket (44-55:
Poison Bulls, Thunder Liches, Dark Knights and the four Gorgon spots).

The spots, first block only (the second repeats them), by floor:

**D1**

- Ghost (11), 52 spots: 69,167; 136,149; 133,212; 135,212; 151,212; 168,220; 46,146; 49,182; 44,162; 45,180; 45,183; 164,232; 92,157; 102,174; 113,166; 222,223; 163,184; 164,186; 150,193; 142,218; 240,247; 242,247; 241,226; 246,220; 243,201; 244,192; 213,248; 148,149; 227,248; 181,246; 86,150; 98,145; 45,188; 53,188; 52,166; 62,154; 70,154; 77,152; 110,145; 119,146; 120,159; 111,188; 125,195; 135,196; 144,187; 125,218; 197,245; 247,211; 231,186; 223,187; 213,186; 209,175
- Larva (12), 22 spots: 65,205; 87,177; 70,195; 61,213; 69,222; 57,247; 49,246; 86,196; 46,210; 46,205; 137,241; 130,244; 134,240; 193,224; 194,222; 194,221; 79,154; 78,189; 100,198; 95,192; 90,187; 73,241
- Skeleton Warrior (14), 32 spots: 120,232; 98,221; 100,220; 100,225; 108,232; 107,230; 84,244; 94,242; 108,195; 94,208; 62,181; 31,247; 3,246; 29,246; 83,198; 33,209; 45,201; 120,243; 152,247; 156,247; 97,187; 105,208; 85,208; 84,220; 85,229; 68,227; 61,238; 73,245; 3,232; 3,219; 7,219; 17,211
- Skeleton Archer (15), 7 spots: 61,169; 217,229; 193,148; 188,146; 206,152; 212,151; 247,176
- Cyclops (17), 28 spots: 18,220; 230,173; 236,167; 241,173; 173,200; 165,171; 208,162; 220,161; 176,188; 168,150; 156,171; 176,161; 185,161; 181,171; 198,168; 190,172; 128,232; 13,235; 27,236; 143,232; 50,211; 220,172; 247,168; 52,222; 50,235; 38,234; 16,227; 25,220

**D2**

- Hell Hound (5), 14 spots: 170,126; 163,114; 163,125; 145,118; 123,120; 138,86; 120,75; 81,58; 71,53; 69,88; 98,111; 87,114; 65,127; 70,76
- Poison Bull (8), 7 spots: 187,15; 158,17; 119,47; 116,47; 122,46; 193,30; 205,24
- Thunder Lich (9), 13 spots: 28,18; 22,4; 18,29; 18,23; 34,4; 118,11; 38,23; 160,10; 194,18; 213,12; 221,14; 243,16; 233,66
- Ghost (11), 5 spots: 199,116; 247,7; 240,9; 241,9; 183,128
- Hell Spider (13), 9 spots: 112,93; 104,65; 183,27; 114,20; 247,87; 149,94; 126,94; 168,96; 196,100
- Skeleton Warrior (14), 1 spots: 99,49
- Skeleton Archer (15), 11 spots: 141,110; 98,54; 114,110; 68,110; 76,111; 156,125; 175,127; 175,118; 183,123; 68,125; 80,97
- Elite Skeleton (16), 21 spots: 108,9; 159,41; 115,7; 124,20; 90,6; 212,83; 211,90; 226,58; 239,56; 235,23; 86,105; 88,104; 240,77; 247,77; 248,20; 244,9; 229,11; 155,32; 137,33; 122,29; 48,11
- Cyclops (17), 3 spots: 235,108; 228,105; 214,116

**D3**

- Poison Bull (8), 13 spots: 42,122; 4,73; 12,56; 2,78; 25,100; 21,82; 16,55; 35,55; 39,112; 26,111; 15,113; 12,97; 44,56
- Thunder Lich (9), 8 spots: 22,90; 33,78; 42,117; 30,105; 27,53; 10,68; 33,90; 12,121
- Dark Knight (10), 7 spots: 14,107; 22,65; 39,77; 29,66; 41,88; 19,120; 37,98
- Elite Skeleton (16), 1 spots: 29,80
- Gorgon (18), 4 spots: 43,111; 8,123; 7,56; 26,67

**Traps** (ids 701-768, one each, all floors)

- D1 Lance Trap (100), 2: 186,151 SouthEast; 202,150 SouthWest
- D1 Iron Stick Trap (101), 9: 48,193 SouthWest; 49,193 SouthWest; 90,164 SouthWest; 92,164 SouthWest; 91,164 SouthWest; 128,212 SouthWest; 130,213 SouthWest; 143,214 SouthWest; 155,230 NorthEast
- D1 Fire Trap (102), 1: 45,224 SouthEast
- D2 Lance Trap (100), 25: 126,99 SouthWest; 123,99 SouthWest; 120,99 SouthWest; 117,99 SouthWest; 136,95 SouthWest; 139,95 SouthWest; 172,12 SouthWest; 166,12 SouthWest; 178,12 SouthWest; 177,103 SouthWest; 180,103 SouthWest; 183,103 SouthWest; 186,103 SouthWest; 189,103 SouthWest; 196,33 SouthWest; 193,33 SouthWest; 202,93 SouthWest; 205,93 SouthWest; 198,130 SouthWest; 232,46 SouthEast; 232,40 SouthEast; 232,37 SouthEast; 227,61 SouthWest; 229,93 SouthWest; 232,93 SouthWest
- D2 Iron Stick Trap (101), 17: 10,26 SouthWest; 11,26 SouthWest; 27,12 SouthWest; 24,5 SouthWest; 24,4 SouthWest; 27,11 SouthWest; 23,24 SouthWest; 27,21 SouthWest; 19,19 SouthWest; 22,24 SouthWest; 23,29 SouthWest; 23,28 SouthWest; 33,9 SouthWest; 35,9 SouthWest; 39,18 SouthWest; 39,17 SouthWest; 39,16 SouthWest
- D2 Fire Trap (102), 4: 66,71 SouthEast; 80,61 SouthWest; 169,12 SouthWest; 175,12 SouthWest

(The two islands, client grid, x 14-23 by y 20-32: `0` 3x3 pads at x 17-19 in `8` NoGround, the
passage at x 22-23 two tiles east -- inside a Lich's range 4.)

**Breeds.** Defined in `OM/Version075/Maps/Dungeon.cs:622-1035`, except the Skeleton Warrior (14),
which is Lorencia's (`OM/Version075/Maps/Lorencia.cs:263-288`). Column meanings as in
devias-port.md §1.5; move/atk/view are tiles, delays ms, resistances are OpenMU's `n/255` and given
as n (P poison, I ice, W water/lightning, F fire). Every breed: `Attribute = 2`
(`MonsterDefinition.cs:277-283`: "Not sure what this is"), `NumberOfMaximumItemDrops = 1`.

| # | name | lvl | HP | dmg | def | atk rate / def rate | move/atk/view | move/atk ms | respawn | skill | resist | line |
|---:|---|---:|---:|---|---:|---|---|---|---:|---|---|---|
| 12 | Larva | 25 | 750 | 90-95 | 31 | 125/31 | 3/1/4 | 400/1800 | 10 s | **Poison (1)**, PoisonDamageMultiplier 0.03 | -- | `:778-806` |
| 17 | Cyclops | 28 | 850 | 100-105 | 35 | 140/35 | 3/1/4 | 400/1600 | 10 s | -- | P2 | `:894-921` |
| 11 | Ghost | 32 | 1000 | 110-115 | 40 | 160/39 | 3/1/5 | 400/1400 | 10 s | -- | P2 I2 W2 F2 | `:746-776` |
| 15 | Skeleton Archer | 34 | 1100 | 115-120 | 45 | 170/41 | 2/**5**/7 | 400/2000 | 10 s | -- (ranged by range 5) | -- | `:838-864` |
| 5 | Hell Hound | 38 | 1400 | 125-130 | 55 | 190/45 | 3/1/4 | 400/1200 | 10 s | -- | F3 | `:624-651` |
| 13 | Hell Spider | 40 | 1600 | 130-135 | 60 | 200/47 | 3/4/7 | 400/1600 | 10 s | **Power Wave (11)** | P3 | `:808-836` |
| 16 | Elite Skeleton | 42 | 1800 | 135-140 | 65 | 210/49 | 2/1/4 | 400/1600 | 10 s | -- | -- | `:866-892` |
| 9 | Thunder Lich | 44 | 2000 | 140-145 | 70 | 220/55 | 3/4/7 | 400/2200 | 10 s | **Lightning (3)** | W3 | `:684-712` |
| 8 | Poison Bull | 46 | 2500 | 145-150 | 75 | 230/61 | 3/1/5 | 400/1400 | 10 s | **Poison (1)**, multiplier 0.03 | P6 | `:653-682` |
| 10 | Dark Knight | 48 | 3000 | 150-155 | 80 | 240/70 | 3/1/5 | 400/1400 | 10 s | -- | P3 I3 W3 F3 | `:714-744` |
| **18** | **Gorgon** | **55** | **6000** | 165-175 | 100 | 275/82 | 3/1/7 | 400/1600 | **10 s** | -- | P6 I6 W6 F6 | `:923-953` |
| 14 | Skeleton Warrior | 19 | 525 | 68-74 | 22 | 93/22 | 2/1/4 | 400/1400 | 10 s | -- | -- | `Lorencia.cs:263-288` |

Traps (`Dungeon.cs:955-1034`), all `ObjectKind = Trap`, `MoveRange 0`, `Attribute 1`, **no drops**,
**respawn 3 s**, attack every 1000 ms, level 80, HP 1000, atk rate 400, def rate 500, cannot be hit
(`OMGL/NPC/TrapIntelligenceBase.cs` `RegisterHit` throws):

| # | name | dmg | atk/view | behaviour (`OMGL/NPC/...`) | line |
|---:|---|---|---|---|---|
| 100 | Lance Trap | 100-110 | 4/4 | `AttackSingleWhenPressedTrapIntelligence`: hits whoever stands **on its tile**, each tick | `:955-980` |
| 101 | Iron Stick Trap | 110-130 | 0/1 | same, on its tile | `:982-1007` |
| 102 | Fire Trap | 130-150 | 2/1 | `AttackAreaTargetInDirectionTrapIntelligence`: everyone within 2 tiles **in the direction it faces**, not on a safe tile | `:1009-1034` |

Two things in the trap record look wrong and want a word before they go in:

- **The Lance Traps can never fire as OpenMU has them.** All 27 stand on tiles nobody can enter
  (25 on byte 4 or 12, two -- 196,33 and 232,93 -- on byte 8), and "attack when pressed" needs a
  body on the trap's tile. Their `AttackRange = 4` suggests 0.75 fired them at range. All 5 Fire
  Traps also stand on blocked tiles (fine for a directional trap), and one Iron Stick (92,164,
  byte 4) does (not fine for a pressed one); the other 25 Iron Sticks are on byte 0.
- **The traps are MuMain's own world objects.** `MM/Engine/Object/ZzzObject.cpp:5077-5110`
  `SaveTrapObjects` wrote the MonsterSetBase trap rows out of the Dungeon's objects: type **39 ->
  100** Lance, **40 -> 101** Iron Stick, **51 -> 102** Fire (and Lost Tower's 25 -> 103 Meteorite).
  The client draws a trap monster as that world object (`ZzzCharacter.cpp:14274-14282`,
  `CreateCharacter(Key, 39/40/51, ...)`; model file `Object2/Object40.bmd`, `Object41.bmd`,
  `Object52.bmd`), and holds the Iron Stick's action 0 (`ZzzCharacter.cpp:3516-3518`;
  `MapManager.cpp:1131-1134` plays its action 1 at 0.4). Decoding `MMD/World2/EncTerrain2.obj`
  (4488 objects, 61 kinds): **59 trap objects vs OpenMU's 58 rows**, identical tile for tile except
  one **Fire Trap object at 69,73** with no OpenMU row.

**What MuMain draws** (`MM/Engine/Object/ZzzCharacter.cpp`; the file is
`Data/Monster/Monster{MODEL+1:02}.bmd`, `ZzzOpenData.cpp:2561`, MODEL numbers
`MM/Core/Globals/_enum.h:4150-4168`; skeletons are `Data/Skill/Skeleton0{1,2,3}.bmd`,
`ZzzOpenData.cpp:4171`):

| # | model | scale | arms | notes | line |
|---:|---|---:|---|---|---|
| 5 Hell Hound | Monster02 (the Hound) | 1.1 | Falchion + Plate Shield | HiddenMesh 1, Level 1 | `:14120-14139` |
| 8 Poison Bull | Monster01 (the Bull Fighter) | 1.0 | Great Scythe | Level 2, carries the Poison debuff look (`g_CharacterRegisterBuff eDeBuff_Poison`) | `:14069-14097` |
| 9 Thunder Lich | Monster05 (the Lich) | 1.1 | Thunder Staff | Level 1 | `:14157-14173` |
| 10 Dark Knight | Monster04 | 0.8 | Double Blade | Level 1 | `:14149-14156` |
| 11 Ghost | Monster08 | 1.0 | -- | AlphaTarget 0.4 (40% visible), MoveSpeed 15, Blood | `:14099-14106` |
| 12 Larva | Monster07 | 0.6 | -- | | `:14107-14112` |
| 13 Hell Spider | Monster09 | 1.1 | Serpent Staff | | `:14113-14119` |
| 14 Skeleton Warrior | player + Skeleton01 | 0.95 | Gladius + Buckler | Blood | `:14184-14196` |
| 15 Skeleton Archer | player + Skeleton02 | 1.1 | Elven Bow (Weapon[1]) | Level 1, Blood | `:14209-14217` |
| 16 Elite Skeleton | player + Skeleton03 | 1.2 | Tomahawk + Skull Shield | Level 1, Blood | `:14218-14227` |
| 17 Cyclops | Monster11 | 1.0 | Crescent Axe | | `:14061-14068` |
| 18 Gorgon | Monster12 | **1.5** | Gorgon Staff | BlendMesh 1, BlendMeshLight 1 | `:14046-14054` |

All six new Monster files (04, 07, 08, 09, 11, 12) and Skeleton02/03 are on disk in MMD.

**Bosses.** Version075 has no boss flag, no special drop group and no summoning for any Dungeon
breed. The **Gorgon** is the Dungeon's boss only by numbers: level 55, 6000 HP (twice the Dark
Knight's), resist 6 across the board, 4 spots in D3 (8 with the doubling), and the **same 10 s
respawn as everything else** -- unlike Devias's Ice Queen (50 s). If 0.75 respawned the Gorgon
slowly, OpenMU does not have it (Open questions).

**Skills in the sim.** Poison: `SkillsInitializerBase.cs:215-216`, the Poisoned effect lasts **20 s**
(10 for every other poison-type skill), and `OMGL/PoisonMagicEffect.cs:20,46` deals **3% of the
target's current HP every 3 s**, the multiplier being the attacker's `PoisonDamageMultiplier`
(0.03 on both the Larva and the Poison Bull). Lightning (Thunder Lich) and Power Wave (Hell Spider)
are the skills' own damage/showing. MU2_BGFX reads no `attack_skill` but the Lich's meteor today
(devias-port.md §2).

**Experience and Zen per kill**, from `src/sim/rules.cpp:267-283` (`(L+25)*L/3`, *1.25, cut when
the killer is more than 10 levels over), Zen = exp + 7 (`OMGL/DefaultDropGenerator.cs`
`BaseMoneyDrop = 7`):

| L | breed | exp (killer <= L+10) | Zen |
|---:|---|---:|---:|
| 19 | Skeleton Warrior | 348 | 355 |
| 25 | Larva | 520 | 527 |
| 28 | Cyclops | 618 | 625 |
| 32 | Ghost | 760 | 767 |
| 34 | Skeleton Archer | 835 | 842 |
| 38 | Hell Hound | 997 | 1004 |
| 40 | Hell Spider | 1083 | 1090 |
| 42 | Elite Skeleton | 1172 | 1179 |
| 44 | Thunder Lich | 1265 | 1272 |
| 46 | Poison Bull | 1360 | 1367 |
| 48 | Dark Knight | 1460 | 1467 |
| 55 | Gorgon | 1833 | 1840 |

#### 1.7 Drops

Nothing Dungeon-specific: `InitializeDropItemGroups` is not overridden, so the map carries
`GameConfigurationInitializerBase.cs:173-212`'s defaults. For Version075 the option types are only
Option and Luck (`OM/Version075/GameConfigurationInitializer.cs:28-33`), so **no Excellent group**
(`:192` is guarded by `OptionTypes.Contains(Excellent)`). One roll per kill
(`NumberOfMaximumItemDrops = 1`), groups in ascending chance (`DefaultDropGenerator.cs`
`SelectRandomGroup`):

| group | chance | what | line |
|---|---:|---|---|
| Jewels | 0.001 | Bless (dl 25), Soul (dl 30), Chaos (dl 12, max 66); `AddItemToJewelItemDrop` also puts the Ale (15), Town Portal Scroll (30) and the pets in it (`src/sim/realm_items.cpp:713-717`) -- no 12-level gap for this group | `GameConfigurationInitializerBase.cs:205-211`; `OM/Version075/Items/Jewels.cs:44,65,86-87` |
| Random item | 0.3 | any `DropsFromMonsters` item with `L-12 < DropLevel <= L` (and `L <= MaximumDropLevel`), at **+`min((L - DropLevel)/3, max)`**, luck/option at 25% each, skill 50% | `:184-190`; `DefaultDropGenerator.cs:20-21,170,220-223,263-271,578-593` |
| Money | 0.5 | exp of the kill + 7 | `:176-182`; `DefaultDropGenerator.cs:19,517` |
| nothing | 0.199 | | |

So in the Dungeon, with levels 19-55, the window runs from drop level 8 (Skeleton Warrior) to 55
(Gorgon); the refinement is `(L - DL)/3` with `L - DL <= 11`, so **+0 to +3** (a gap of 9-11 gives
+3). Examples from mu.db `items` (drop_level window only):
at L 44 (Thunder Lich) Blade/Light Saber/Legendary Sword, Thunder Staff, Tiger Bow, Spirit armour,
Legendary Gloves; at L 55 (Gorgon) the Double Blade, Giant Sword, Crescent Axe, Great Scythe, Gorgon Staff, Serpent
Crossbow, Legendary Shield, and the Plate, Legendary, Guardian and Dragon pieces of drop level 44-55. Scrolls
(`OM/Version075/Items/Scrolls.cs:33-44`) that newly fall here: **Flame (35), Twister (40), Evil
Spirit (50 -- the Gorgon alone, 50 > 55-12)**, plus Poison (30), Ice (25), Meteorite (21) at the
low end. The Jewel of Bless and Soul are first seen here: Lorencia's monsters (max level 19) never
leave them (`src/sim/realm_items.cpp:712-716`).

**MU2_BGFX already does all of this generically** (`src/sim/realm_items.cpp:672-800`
`Realm::leave`): the 12-level window, `(L-DL)/3` refinement, jewels without the gap, Zen = exp+7.
Its declared differences from 0.75: item chance **0.1, not 0.3** (the user's, 2026-09-22), an
excellent group (`kExcellentChance`) that 0.75 lacks, sockets (invention), Zen straight to the
purse. Nothing new is needed for the Dungeon's drops.

#### 1.8 Rules of the place

- **Level to enter: 20** on foot (gate 1, `Gates.cs:185`); **30 / 40 / 50** by warp to D1/D2/D3
  (`:48-50`). Every internal stair also asks 20; leaving asks 0.
- **No safe zone anywhere** (0 safe tiles, §1.5). A monster may follow you onto any tile; there is
  no town, no NPC, no vault, no shop.
- **Death: you wake in Lorencia.** `OMGL/Player.cs:1559-1562` respawns at
  `CurrentMap.Definition.SafezoneMap`'s spawn gate; the Dungeon's `SafezoneMap` is Lorencia
  (`OM/BaseMapInitializer.cs:91`, no spawn gate of its own), so the landing is Lorencia's **gate 17,
  133-151 x 118-135** (`Gates.cs:113`).
- **Town Portal Scroll: to Lorencia**, same rule
  (`OMGL/PlayerActions/ItemConsumeActions/TownPortalScrollConsumeHandlerPlugIn.cs:33-38`), and the
  client allows it in the Dungeon (`MM/World/MapInfra/PortalMgr.cpp:51`).
  **MU2_BGFX note:** `Realm::haven()` (`src/sim/realm_fight.cpp:752-770`) uses the *current* map's
  `safeGate`; with none it leaves the hero where he stands. Both `reviveHero()` (`:784-799`) and the
  scroll (`realm_items.cpp:474-500`) go through it, so a Dungeon death or scroll must become a
  map change to Lorencia, not a local setDown.
- **Exp multiplier 1**, no map requirements, no PK exception in the record (single player makes PK
  moot).
- **Sound and music:** `SOUND_DUNGEON01` loops (`MM/Scenes/SceneManager.cpp:859-861`, stopped
  elsewhere `:940-943`) and `MUSIC_DUNGEON` plays (`:1038-1041`) -- the Dungeon is one of the places
  MU has music by the map, not by the fight (compare memory "music is rare and in fights").
- **Operable objects:** in the Dungeon object type 60 is a pose spot and 59 a seat
  (`MM/Engine/Object/ZzzInterface.cpp:1707-1713`, cursor `:4053`).

---

### 2. What mu.db has of the Dungeon: one breed

`sqlite3 -readonly source/mu.db` (WAL read too):

- `gates`: 17 (map 0) and 27 (map 3) only -- and the Dungeon has **no spawn gate to add**. Its
  gates live in `src/sim/gates.cpp` like the others: exits 2, 4, 6, 8, 10, 12, 14, 16 and enters
  1, 3, 5, 7, 9, 11, 13, 15 (§1.2).
- `monster_kinds`: 0-4, 6, 7, 14, 19-33. **Of the Dungeon's twelve breeds only 14 (Skeleton
  Warrior) is there**; 5, 8-13, 15-18 and the traps 100-102 are not.
- `monster_spawns`: maps 0, 2, 3. **No map 1.**
- `items`: groups 0-11 and 14 (no 12/13/15 rows; scrolls, orbs and jewels come from
  `source/items` via `pipeline/index.py`).

Figures MU2_BGFX already has (`source/monsters/`): Hound01, BullFighter01, Lich01, Skeleton01 +
SkeletonWarrior, so Hell Hound, Poison Bull, Thunder Lich and Skeleton Warrior are re-dresses
(scale/arms/HiddenMesh). **New figures:** Monster04 Dark Knight, 07 Larva, 08 Ghost, 09 Hell Spider,
11 Cyclops, 12 Gorgon, and the Skeleton02 (archer) / Skeleton03 (elite) bodies; trap objects 40/41/52
from Object2. **Arms:** all in `source/items` (Falchion Sword08, Plate Shield Shield10, Great Scythe
Spear09, Thunder Staff Staff04, Double Blade Sword14, Serpent Staff Staff03, Gladius Sword07, Buckler
Shield05, Elven Bow Bow03, Tomahawk Axe04, Skull Shield Shield07, Gorgon Staff Staff05) **except the
Cyclops's Crescent Axe (Axe09, `OM/Version075/Items/Weapons.cs:114`)**, whose bmd is in `MMD/Item`.

Rows to add, in devias-port.md §2's form (spawn ids: check `select max(id)` first):

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (5,'Hell Hound',38,1400,125,130,55,3,1,4,400,1200,190,45,10,NULL),
 (8,'Poison Bull',46,2500,145,150,75,3,1,5,400,1400,230,61,10,1),
 (9,'Thunder Lich',44,2000,140,145,70,3,4,7,400,2200,220,55,10,3),
 (10,'Dark Knight',48,3000,150,155,80,3,1,5,400,1400,240,70,10,NULL),
 (11,'Ghost',32,1000,110,115,40,3,1,5,400,1400,160,39,10,NULL),
 (12,'Larva',25,750,90,95,31,3,1,4,400,1800,125,31,10,1),
 (13,'Hell Spider',40,1600,130,135,60,3,4,7,400,1600,200,47,10,11),
 (15,'Skeleton Archer',34,1100,115,120,45,2,5,7,400,2000,170,41,10,NULL),
 (16,'Elite Skeleton',42,1800,135,140,65,2,1,4,400,1600,210,49,10,NULL),
 (17,'Cyclops',28,850,100,105,35,3,1,4,400,1600,140,35,10,NULL),
 (18,'Gorgon',55,6000,165,175,100,3,1,7,400,1600,275,82,10,NULL);
-- traps 100-102: no columns for trap kind / facing / pressed-vs-directional; carry them in code.
-- monster_spawns: 512 one-tile rows (x1=x2, y1=y2, count 1), or 258 with count 2 -- see §1.6.
```

No column holds resistances or `PoisonDamageMultiplier`; like Devias's, they need a home.

---

### Open questions

1. **The doubled spawn table.** OpenMU lists the Dungeon's 258 spots twice (ids 101-359 and
   361-616, the second missing four Skeleton Warriors). 512 monsters or 258? Check against a 0.75
   (or 0.97d) MonsterSetBase if one can be found; none is in this tree.
2. **Spot scatter.** All spots are single tiles; MonsterSetBase spot rows carry a scatter distance
   OpenMU drops. Two monsters on one tile is what OpenMU would do.
3. **Lance Traps on blocked tiles.** OpenMU's "attack when pressed" can never fire for any of the 27
   Lance Traps (all on blocked tiles), nor the Iron Stick at 92,164. What did 0.75 do -- fire at range 4? Or treat the
   trap's own tile as walkable (the object sits in a groove)?
4. **The 59th trap.** The client's EncTerrain2.obj has a Fire Trap object at 69,73 with no OpenMU
   row. Spawn it or not?
5. **Gorgon respawn.** 10 s in OpenMU like every Dungeon breed. Was 0.75's boss slower (the
   Monster.txt respawn column)? No evidence either way here.
6. **Gate 6/7 overlap.** Exit 6's box (231-234 x 126-127) shares tiles 232,127 and 233,127 with
   enter 7 (232-233 x 127-128). A warp or walk into D2 that lands on those two tiles stands inside
   the gate back up. MU2_BGFX should exclude enter-gate tiles when drawing an exit tile (or trust
   `CheckGate` to fire only on a step in). Exit 8 also has one blocked tile (241,151).
7. **Warp vs walk levels** (30/40/50 by warp, 20 on foot): enforce both, or neither (M is free
   today)?
8. **0.97d has no map data in this tree** (`OM/Version097d` is Jewels only), so "cross-check
   against 0.97d" for gates, spawns and breeds could not be done; 0.95d and Season 6 inherit
   Version075's Dungeon unchanged.
9. **`Attribute = 2`** on every breed (traps 1): OpenMU itself does not know what it is.

---

## Part B: the data and the look

Research notes, 2026-09-30, read-only on the repo. The Dungeon is MU server map **1**
(`WD_1DUNGEON`, `MM/World/MapInfra/MapManager.h:10`). The client ships it as `Data/World2` and
`Data/Object2`. Paths: MM = `LEGACY/reference/MuMain/src/source/`, D = `LEGACY/reference/MuMain/src/bin/Data/`,
OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`.

Scratch outputs are under `.../scratchpad/dungeon/`:
- `dungeon/` is `pipeline/terrain.py D/World2 dungeon <scratch> 2` (height/tiles/attributes/light.png plus dungeon.json).
- `obj/` holds every Object2 .bmd through MuExtract (`export-obj` + `export-rig --actions=all`), with a .log for each.
- `mon/` holds the six missing monsters and the three skeletons, exported the same way.
- `tex/` holds every Object2/World2 sheet decoded; `sheet_textures.png` is a contact sheet of them.
- `prev_l1_attr_height_light.png` shows layer 1 | attributes | height | light; `minimap_768.png` is the client's mini_map.OZT.
- `terrain.log`, `cmp.py`.

**The one thing to know first.** Unlike Devias, the Dungeon's grid is **gameplay-equivalent to
0.75**:
- The client's `EncTerrain2.att` is byte-identical to OpenMU's Season 6 `Terrain2.att` (100.00%).
- Against `075_Terrain2.att` it agrees on 96.62% of bytes and 98.15% of walkability.
- All 1214 walkability differences are tiles that 0.75 opens and the client closes (1213 of them 4->0 and one 8->0). They sit in isolated strips on the map's own border: x 0-201 at y 252-255 (609 tiles), x 243-255 at y 140-255 (341), and x 252-255 in six more strips.
- **None of them joins a walkable floor.** The three big floors are identical in both grids: 14386, 9426 and 3415 tiles.
- The remaining 996 differences are 12 -> 8 (NoMove|NoGround against NoGround), which blocks the same.
- The client also ships an unused plain `World2/Terrain.att` that is 99.63% `075_Terrain2.att` (245 tiles differ, 244 of them a 2 -> 0 bit).
- The objects agree with 0.75 as well. The trap placements (types 39, 40, 51) sit on exactly OpenMU 075's trap spawn tiles: 27/27 lance, 26/26 iron stick and 5/5 fire. The client has one extra fire-trap placement at tile (69,73).

So the client's grid can be used as is.

---

### 1. Raw data inventory

#### 1.1 World2 (`D/World2`, 22 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain2.map | 196610 | yes (`MapManager.cpp:1227` pattern) | 3 planes: layer1, layer2, alpha |
| EncTerrain2.att | 65540 | yes | 2 bytes a tile; high byte is 0 |
| EncTerrain2.obj | 134644 | yes | **4488 placements, 61 kinds** |
| TerrainHeight.OZB | 66620 | yes | header says 1078, read at 1080 |
| TerrainLight.OZJ | 63426 | yes | 256² baked light |
| Tile{Grass01,Grass02,Ground01,Ground02,Ground03,Water01,Wood01,Rock01,Rock02}.OZJ | 22-110 KB | yes | 9 tile sheets, **no .OZT: MU grows no grass here** |
| Terrain.att / Terrain.map | 65539 / 196609 | no | plain copies. Terrain.att is the older grid, ~075 (see above) |
| Terrain2.att / Terrain2.map | 65539 / 196609 | no | BUX-xored att, =EncTerrain2 (100%) |
| mini_map.OZT | 4194352 | Season UI | 1024² minimap |
| Minimap.bmd | 11649 | Season UI | |

There is no leaf, snow or rain sheet in World2.

terrain.py (`terrain.log`):

```
height     256x256 tiles, 0.00 to 3.11 m  (mean 1.72; 5th/50th/95th pct 1.695/1.695/1.92 -- FLAT)
tiles      9 slots: 0 TileGrass01 75.7%, 7 TileRock01 11.8%, 6 TileWood01 5.4%, 5 TileWater01 5.1%,
           1 TileGrass02 1.3%, 4 TileGround03 0.6%, 8 TileRock02 0.2%, 2/3 ~0
walkable   41.6%, 0.0% safe (no safe zone anywhere)
light      mean [108, 108, 140]  -- blue-violet, with white-hot pools at the torches
objects    4488 placed, 61 kinds; "missing: Object61" (type 60, the hidden pose box -- harmless)
```

**Attributes (client):**

| value | meaning | tiles |
|---|---|---:|
| 0 | open | 27289 |
| 4 | NoMove | 11994 |
| 8 | NoGround | 21592 |
| 12 | NoMove + NoGround | 4660 |
| 14 | | 1 |

- 40.1% of the map is NoGround void between the corridors.
- The sanity tile passes. MM `Render/Terrain/ZzzLodTerrain.cpp:206-207` asserts `TerrainWall[120*256+227]==4`, and the extracted grid is 4 at row 120, column 227 (0 transposed). So [y,x] is right.

**Three floors = Dungeon 1/2/3.** OM `Version075/Gates.cs:118-126` gives the exit gates and `:185-191` the enter gates, walked into the walkable components:

| floor | tiles | box | gates |
|---|---:|---|---|
| **Dungeon 1** | 14386 | x 1-250, y 141-250 (south half) | exit 2 at 107,247-110,247 (from Lorencia); enter 3 at 108,248 -> Lorencia; enter 5 at 239,149 -> D2; exit 8 at 240,149 |
| **Dungeon 2** | 9426 | x 1-249, y 2-129 (north half) | exit 6 at 231,126; enter 7 at 232,127 -> D1; enter 9 at 2,17 -> D3; exit 12 at 3,16; enter 13 at 5,34; exit 16 at 5,32 |
| **Dungeon 3** | 3415 | x 1-46, y 50-132 (west block) | exit 10 at 3,83; enter 11 at 2,84 -> D2; exit 14 at 29,125 |

- Warp list: Dungeon 3000 zen lvl 30 -> gate 2; Dungeon2 3500/40 -> gate 6; Dungeon3 4000/50 -> gate 10 (`Gates.cs:48-50`).
- Every inner enter gate is level 20 (`:187-191`).
- Lorencia's enter gate 1 at 121,232-123,233 is level 20 (`:185`). MU2_BGFX has that gate sealed today (`src/sim/gates.cpp:32-34`).

**Tile sheets** (decoded and looked at in `sheet_textures.png`). The names lie, as in Noria and Devias:

| slot | name | px | actually | layer1 % | walk % of its tiles |
|---|---|---|---|---:|---:|
| 0 | TileGrass01 | 128 | **grey-brown stone floor slabs, 2x2 with joints** (the dungeon floor) | 75.7 | 34 (half of it lies under the void) |
| 1 | TileGrass02 | 128 | pale sand/dust | 1.3 | 67 |
| 2 | TileGround01 | 256 | dark gravel | ~0 (18 tiles) | |
| 3 | TileGround02 | 128 | smooth grey stone | ~0 (7) | |
| 4 | TileGround03 | 128 | cobblestones | 0.6 | 92 |
| 5 | TileWater01 | 256 | **near-black** dark earth/water | 5.1 | 30 |
| 6 | TileWood01 | 128 | brown earth | 5.4 | 72 |
| 7 | TileRock01 | 256 | dark mossy green-grey rock | 11.8 | 74 |
| 8 | TileRock02 | 128 | tan earth/rock | 0.2 | 88 |

- Pairs (base, overlay; tiles with alpha): (0,6) 5000, (0,1) 2126, (7,7) 1989, (6,6) 1622, (5,7) 1612, (0,4) 1047, (0,8) 566.
- 8409 tiles are fully overlaid and 48669 carry no alpha.
- In layer 1, slot 7 plus slot 5 draw the winding dark-rock "cave" corridors (the green snakes in the preview). Everything else is the slab floor.
- Name collisions: Lorencia's unprefixed `tilegrass01...` are already in `source/textures`, so the Dungeon wants its own prefix (e.g. `dn_`), as `nr_` and `dv_` did.

#### 1.2 Object2 (`D/Object2`, 92 files)

The file holds 60 `ObjectNN.bmd` (01-60), `DungeonStone01.bmd`, `Bat01.bmd`, `Rat01.bmd`, and 29 sheets. `Object61.bmd` (type 60) is not on disk; it is the hidden pose box. The MuExtract numbers below come from `obj/*.log`; sizes are the OBJ bbox in MU units (cm), x/y/z with y up.

The identification column reads the sheet plus the size. Nothing was rendered, so it is **not eyeballed** except for the sheets themselves. Type = model index - 1 (`MapManager.cpp:1121-1123` loads `Object{i+1}`).

| type | model | placed | tris | bones/keys | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 0 | Object01 | **1262** | 58 | 1 | deep_wall01, 02 | wall block 3 m wide, 2.9 m tall, 1.1 m thick (bbox 300x292x114): the corridor wall |
| 1 | Object02 | 58 | 8 | 1 | cobweb.tga | cobweb, alpha |
| 2 | Object03 | 27 | 130 | 1 | deep_wall01/02 | wall with carved face (deep_wall02 is a demon mask relief) |
| 3 | Object04 | **588** | 90 | 1 | deep_wall01/03 | 1.2 x 3.2 m wall segment/pilaster |
| 4 | Object05 | 59 | 52 | 1 | deep_wall01 | wall piece |
| 5 | Object06 | 179 | 54 | 1 | deep_wall04 | 1.2 m, hangs **3 m down** from its origin (y -300..0). deep_wall04 fades to black at the bottom, so this is the pit/ledge face into the void |
| 6-9 | Object07-10 | 48/11/13/24 | 74-152 | 1 | deep_wall01/04/05, steel_01.tga, wire_netting01 | wall variants, railing, grating |
| 10 | Object11 | 53 | 144 | 1 | wire_netting01 | iron grate/fence |
| 11 | Object12 | 52 | 162 | 8 / **30 keys** | squid01 | animated organic (ribbed bone/tentacle), 1.2 x 2.6 m |
| 12-14 | Object13-15 | 50/17/217 | 9-56 | 1 | deep_wall04/01 | Object15 (217) is a flat 1.2 m slab, 9 tris: floor/ledge tile |
| 15, 16 | Object16, 17 | 4, 3 | 460, 452 | 1-2 | deep_wall*, **deep_dragon**, wire_netting | big 6 x 5 x 3.4 m dragon-relief structure (gate or altar) |
| 17 | Object18 | 43 | 8 | 1 | steel_02.tga | iron bars (alpha) 1.8 x 2.9 m |
| 18, 19 | Object19, 20 | 3, 3 | 710, 224 | 1-2 | deep_wall10/11, wood01, bons | sarcophagus/altar with bones |
| 20, 21 | Object21, 22 | 33, 20 | 40, 20 | 1 | wood01 | wooden props (crates/beams/planks) |
| **22-24** | Object23, 24, 25 | 17, 17, 50 | 130-180 | 7 / **40 keys** | squid01, squid02 | 4.5-5 m long, 0.8 m high, animated. **MoveObject: `StreamMesh = 1`, V scrolled one sheet a second, mesh 1 unlit** (`ZzzObject.cpp:3880-3885`). The flowing piece — its nature is unverified, look at it on the bench |
| 25-27 | Object26-28 | 21/12/24 | 28-516 | 1 | deep_wall07/09, flower_vase | inscribed plaques (deep_wall09 has carved text); Object28 is a vase group |
| 28, 29 | Object29, 30 | 11, 10 | 78, 288 | 1 | flower_vase, deep_wall04 | urns/vases |
| 30 | Object31 | 23 | 50 | 1 | deep_wall07/10 | |
| 31, 32 | Object32, 33 | 20, 5 | 486 | 1 | bons | skeleton lying (0.7 x 1 x 1.1) |
| 33-35 | Object34-36 | 10/8/18 | 12-76 | Object36: 8 / **30 keys** | deep_wall09, wood01, **logo01.tga** | Object36 is a blue waving banner (alpha) |
| 36 | Object37 | 112 | 86 | 1 | deep_wall07/08/01 | 1 x 3.1 m pillar/column |
| 37 | Object38 | 16 | 30 | 1 | deep_wall07/08 | |
| 38 | Object39 | 51 | 224 | 1 | wire_netting01 | iron cage/grille |
| **39** | Object40 | 27 | 20 | 1 | deep_wall01/04/06 | **Lance trap**, hidden as scenery (`:3893-3897`), server NPC 100 |
| **40** | Object41 | 26 | 33 | 4 / 2 actions (1, 4 keys) | deep_wall05, wire_netting | **Iron Stick trap**, hidden, NPC 101; action 1 PlaySpeed 0.4 (`MapManager.cpp:1131-1134`) |
| **41** | Object42 | 64 | 44 | 1 | wood01, fire0a | wall torch: `CreateFire(0, o, 0, -30, 240)` (`ZzzObject.cpp:3887-3889`) |
| **42** | Object43 | 56 | 70 | 1 | wood01, fire0a | 1.66 m standing torch: `CreateFire(0, o, 0, 0, 190)` (`:3890-3892`) |
| 43-47 | Object44-48 | 15/21/25/79/**385** | 284-582 | 1-2 | bons (+wood01) | skeletons, skulls, bone piles. Object48 (385) is a 0.9 x 1.4 m bone pile |
| 48, 49 | Object49, 50 | **235**, 41 | 398 | 1 | **TileRock01.jpg** (own copy in Object2) | cave rocks 0.9 x 1.6 m, scale up to 3.2 and 5.1 |
| 50 | Object51 | 178 | 56 | 1 | deep_wall12 | 3 m wide, 2.9 m tall dark cracked wall block (the cave walls) |
| **51** | Object52 | 6 | 56 | 1 | deep_wall01/06/04 | **Fire trap**, hidden, NPC 102 |
| **52** | Object53 | 29 | 5 | 1 | cobweb.jpg | **hidden** 1x3x1 marker: **falling stones**, `if (rand_fps_check(3)) CreateEffect(MODEL_DUNGEON_STONE01,...)` (`:3874-3879`) |
| 53 | Object54 | 5 | 648 | 9 / **30 keys** | squid01, bons | animated bone/organic heap |
| 54-58 | Object55-59 | 1/12/19/24/9 | 10-160 | 1 | deep_wall07/09/10/11 | small wall bits, steps, plaques |
| **59** | Object60 | 30 | 10 | 1 | deep_wall07 | **seat**: `CreateOperate` + MOVEMENT_OPERATE Sit (`ZzzObject.cpp:4657-4659`, `ZzzInterface.cpp:1707-1712`) |
| **60** | Object61 | 9 | — | — | — (file missing) | **hidden pose box** 40x40x160, lean cursor (`ZzzObject.cpp:4660-4664`, `ZzzInterface.cpp:4053`) |

Extras loaded only for the Dungeon and Lost Tower (`MapManager.cpp:50-57`):
- `DungeonStone01.bmd`: 8 tris, deep_wall01. The falling-stone effect.
- `Bat01.bmd`: 38 tris, 8 bones, 4 keys, bat01.jpg + bat02.tga (alpha wing).
- `Rat01.bmd`: 36 tris, 6 bones, 5 keys, mouse.jpg.

Animated placed kinds: Object12, 23, 24, 25, 36, 41 and 54 (plus the bat and rat).

The height is flat (1.695 m nearly everywhere), and **the walls are all objects**. Object z runs from -676 to +556, so rocks and walls stand down into the void.

#### 1.3 Sounds and music (`D/Sound`, `D/Music`)

| file | bytes | use |
|---|---:|---|
| Music/**Dungeon.mp3** | 5830032 | `MUSIC_DUNGEON` (`_enum.h:175`), played always on WD_1DUNGEON (`SceneManager.cpp:1038-1045`) |
| Sound/**aDungeon.wav** | 733678 | ambient loop `SOUND_DUNGEON01` (`ZzzOpenData.cpp:4733`; `SceneManager.cpp:859-861`, stopped elsewhere `:940-943`) |
| Sound/aBat.wav | 109130 | bat, 1/256 a frame (`ZzzOpenData.cpp:4746`; `GOBoid.cpp:1490-1494`) |
| Sound/aMouse.wav | 115718 | rat, within 600 units, 1/256 (`:4747`; `GOBoid.cpp:1875-1877`) |
| Sound/aGrate.wav | 53254 | `SOUND_TRAP01`, lance and iron-stick traps (`:4748`; `ZzzCharacter.cpp:1223-1232`) |
| Sound/sFlame.wav | 123030 | fire trap (`:4796`; `ZzzCharacter.cpp:1233-1237`) |

Monster sets: mDarkKnight*, mLarva1-2, mHellSpider*, mGhost*, mOgre* (Cyclops), mGorgon* (`ZzzOpenData.cpp:3409-3481`). mHound* and mWizard* (Lich) are already used by Lorencia's breeds. MU2_BGFX `source/sounds` has sFlame.wav only. **aDungeon, aBat, aMouse and aGrate are not imported.**

---

### 2. MuMain's special cases for WD_1DUNGEON

Grep `WD_1DUNGEON`: 17 hits, every one below. No `WorldActive == 1` literal was found.

**Frame, light, sky**
- **Clear/fog colour: black.** The Dungeon has no arm in `SetWorldClearColor`, so it takes the default `SetClearAndFogColor(0,0,0)` (`Scenes/SceneManager.cpp:402`). That makes the NoGround void (40% of the map) pure black.
- No sky, no fog value of its own, no weather, no leaves or rain (no Dungeon arm in the weather code, no leaf sheets in World2).
- **Terrain light** is TerrainLight.OZJ times the per-vertex normal term, as on every map (`devias-ground.md` §3). The baked light is the whole look: mean (108,108,140), blue-violet, with near-white pools at the torches. terrain.py now applies the bottom-up flip (`terrain.py` `baked_light`, `np.flipud`), and the flipped light lines up with the corridors in `prev_l1_attr_height_light.png`.
- **Torches are the only dynamic light.** 120 of them: type 41 (64 wall torches) and 42 (56 standing). `CreateFire(0,...)` (`Render/Effects/ZzzEffectFireLeave.cpp:61-83`) gives a BITMAP_FIRE particle every other frame, plus `AddTerrainLight(pos, (L, .6L, .4L), 4 tiles)` with L random 0.6-1.1 each frame, which flickers. The same fire as Lorencia's DoungeonGate/bonfire emitters.
- No RenderObjectVisual arm for the Dungeon (`ZzzObject.cpp:2776-2864` covers 0, 2 and 3), so objects carry no sprites or glows.
- No BlendMesh additive arm in CreateObject for the Dungeon (`:4655-4666` has only the operates).

**Animated and moving world**
- **Types 22-24 stream** (`ZzzObject.cpp:3880-3885`): `Models[type].StreamMesh = 1; BlendMeshTexCoordV = -(WorldTime%1000)*0.001`. Mesh 1's V scrolls one sheet a second, and a StreamMesh draws unlit, flat BodyLight (`Render/Models/ZzzBMD.cpp:1344-1369`, `:1819`).
- **Falling stones** (`:3874-3879`): each of the 29 type-52 markers (hidden) spawns `MODEL_DUNGEON_STONE01` one frame in three. The effect init is at `Render/Effects/ZzzEffect.cpp:2875-2882`: LifeTime 24-39, scale 0.6-1.3, gravity 0..-3, spawned 200-327 up and 50-81 back, ±32 side. Debris dropping from the ceiling.
- **Bats** (`Engine/AI/GOBoid.cpp:1304-1334`): the boid slot takes MODEL_BAT01 in the Dungeon and Lost Tower, at most 5 alive (default cap, `:1261-1264`), `MoveBat` (`:1428`), aBat sound.
- **Rats** (`GOBoid.cpp:1685,1718-1721`): the fish slot takes MODEL_RAT01, at most 3 (`:1667-1675`). They spawn within ±512 of the hero on a tile `< TW_NOGROUND`, scale 0.4-0.7, velocity 0.6/scale, billboard alpha. They turn back at NoGround (`:1849`) and die past 1500 units.

**Traps** (server NPCs drawn with a world model)
- The placements of types 39, 40 and 51 are `HiddenMesh = -2` (`ZzzObject.cpp:3893-3897`). The live trap is a character made from the same model:
  - `MONSTER_LANCE_TRAP` 100 -> model 39
  - `IRON_STICK` 101 -> 40
  - `FIRE` 102 -> 51
  - (`ZzzCharacter.cpp:14274-14282`; enum `_enum.h:4475-4477`)
- `ZzzObject.cpp:5091-5104` is the editor dump that turns these placements into MonsterSetBase rows. That is why the tiles match OpenMU's exactly.
- Attack visuals (`ZzzCharacter.cpp:1223-1237`):
  - lance: `CreateEffect(MODEL_SAW, ...)` + aGrate. MODEL_SAW is `Data/Skill/Saw` (`ZzzOpenData.cpp:4196`), LifeTime 10, raised 130 and thrust 60 forward (`ZzzEffect.cpp:1825-1830`).
  - iron stick: `SetAction(1)` + aGrate. Action 1 plays at 0.4 (`MapManager.cpp:1131-1134`); at rest it is forced to action 0 (`ZzzCharacter.cpp:3516-3519`).
  - fire: `CreateEffect(BITMAP_FIRE+1, ...)` + sFlame.
- OpenMU's AI: `AttackSingleWhenPressedTrapIntelligence` (100, 101) and `AttackAreaTargetInDirectionTrapIntelligence` (102) (OM `Version075/Maps/Dungeon.cs:958-1017`).

**Operates, camera, other**
- Seats (59) and the hidden lean box (60) are listed above.
- There is **no camera or ceiling limit** for the Dungeon: nothing in CameraMove or ZzzScene names it. The dungeon is open-topped and seen from MU's usual camera over the black.
- The town portal can be used from the Dungeon (`World/MapInfra/PortalMgr.cpp:51`).
- The mini-map exists only in the Season UI.

**Dungeon monster visuals in MM**
- Poison Bull, Level 2, is registered with `eDeBuff_Poison` (`ZzzCharacter.cpp:14088-14094`).
- Dark Knight and Larva leave a smoke trail. While alive, 1 frame in 4, a `BITMAP_SMOKE+1` spawns within ±32/±16 of the body. Devias gets BITMAP_SMOKE instead (`ZzzCharacter.cpp:6148-6168`).
- **Gorgon**: `BlendMesh = 1, BlendMeshLight = 1` at creation (`:14046-14053`). Mesh 1 is `boss_eye.jpg`, 4 tris. `BlendMeshLight = (rand()%10)*0.1` every frame, so the eyes flicker additively (`:6061-6062`). Its Level-2 fire and red light (`:6063-6076`) are not the Dungeon's Gorgon, which is level 0. The Gorgon Staff glow is `BITMAP_SHINY+1` at (0,-90,0) off the link bone, size 2, colour (0.4,0.8,0.6)·L (`:10270-10275`).
- **Ghost**: `AlphaTarget = 0.4`, a translucent draw. `MoveSpeed 15`, `Blood = true` (no blood) (`:14099-14106`).
- Hell Hound: `HiddenMesh = 1`, so it wears the helmet head (hound_head01). Falchion + Plate Shield, Level 1, which gives it no RenderEye because that is Bull-only (`:14121-14140`).
- Thunder Lich: Thunder Staff, Level 1, scale 1.1 (`:14158-14173`).
- Level 1 is also set on DK, Skeleton Archer and Elite Skeleton, and has no visual on those models.
- Link bones (`:12037-12063`): Gorgon 30/39, DK 26/36, Cyclops/Lich 41/32, Hound 19/14, Hell Spider 29/38.
- Bounding boxes: Larva 50x50x80 and Gorgon 70x70x250 (`:11807-11818`).

---

### 3. Pipeline route (how Devias came in), and what changes for the Dungeon

The chain is in `docs/content.md`:
- import: `source/` via MuExtract, decode_texture.py and terrain.py
- build: `tools/content.sh` / `tools/asset.sh` into `workshop/`
- sync: `tools/sync.sh` into `assets/`
- cook: `tools/cook.py` into `assets/cooked/`

The exact Devias sequence is `docs/devias-port.md` §6 (stages 0-6) and `docs/devias-ground.md` §6. The Devias world files arrived with the subtree import (`c8f4d276 "The world starts from MU's own Lorencia, Noria and Devias"`). The later Devias commits on world/devias are townsfolk `c2e2e60c`, chasms `3a150323`/`596b4e86`, perches `a3e26b08` and hearths `f3683e56`.

For the Dungeon (`D=../LEGACY/reference/MuMain/src/bin/Data`, run from MU2_BGFX/):

```sh
# 0. tables first, in pipeline/terrain.py (and index.py for OPERABLE):
#    HIDDEN_BY_MAP[1] = {39, 40, 51, 52, 60}     (traps, stone emitter, pose box)
#    OPERABLE_BY_MAP[1] = {59, 60}               (index.py:43-46 too)
#    BLEND_MESH_BY_MAP: none. GRASS_BY_MAP: none (World2 has no TileGrass*.OZT)
python3 pipeline/terrain.py $D/World2 dungeon source/world 2
#    expect: 0.00-3.11 m, 9 slots, 41.6% walkable, 0.0% safe, 4488 placed / 61 kinds, missing Object61
for n in TileGrass01 TileGrass02 TileGround01 TileGround02 TileGround03 TileWater01 TileWood01 TileRock01 TileRock02; do
  python3 pipeline/decode_texture.py $D/World2/$n.OZJ source/textures/dn_$(echo $n | tr A-Z a-z).png; done
#    source/world/dungeon/ground.json on Devias's shape: 9 sheets -> dn_*, sheet_materials
#    (slot 0 flagstone, NOT grass; 5 dark rock/earth, not water; 7 rock; 1/6/8 sand/earth; 4 cobble)
./tools/content.sh --world dungeon && ./tools/sync.sh --world dungeon --only-world && ./tools/sync.sh
./tools/cook.py --world dungeon --only ground && ./tools/cook.py --world dungeon --only tables
# objects: per kind, dn_-prefixed sheets (Object2 sheet names collide: bons, cobweb, wood01, TileRock01 ...)
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-obj $D/Object2/Object01.bmd source/world/dungeon/Object01.obj
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-rig $D/Object2/Object12.bmd source/world/dungeon/Object12.rig.json --actions=all   # animated kinds only
./tools/asset.sh dungeon/Object01 --build-only && python3 pipeline/index.py source workshop
./tools/cook.py --world dungeon --only textures / meshes / placements
```

What is different for a dungeon (grep the per-world code as devias-port §5 did):

- **The void.** 40% of the map is NoGround. The Devias chasm work already stops ground being drawn there (`3a150323`) and fades anything below the rim to the clear colour (`596b4e86`, `Ground::buildAbyss`, `abyss()` in common.sh). Object06's 3 m pit faces and the sunk rocks will melt into it. The clear must be black (MU's default, `SceneManager.cpp:402`).
- **No grass.** World2 ships no TileGrass .OZT, and slot 0 (TileGrass01) is a stone floor. It must not take the `grass` recipe, or `src/content/ground.cpp`'s `recipe == "grass"` sows a sward in the dungeon.
- **No weather.** `src/game/world/weather.cpp:113-116` opts in by world name, so the Dungeon gets none by default. Leaves and snow are likewise per world.
- **No sky or sun look.** It needs a `sheets/worlds/dungeon.json` on the base sheet: no sky, black clear, low or zero sun and shadows, and the TerrainLight carrying the blue. Torch flicker should be the light.
- **Water.** Slot 5 (TileWater01) is a near-black sheet, and MU would slide it (every water base tile scrolls, `ZzzLodTerrain.cpp:3595`). Hold it still (`water_flow 0`, as Noria and Devias do). Flag for the user: it is dark ground, not water.
- **Boids.** `src/game/world/boids.cpp:55-72` names Bird01 and Butterfly01 by world. The Dungeon wants Bat01 (5) and Rat01 (3, ground-bound, turns at NoGround). Both are new assets (Object2 Bat01/Rat01).
- **Ambient.** aDungeon.wav should loop everywhere on the map and the wind stop. Dungeon.mp3 plays always in MU, but house rule says music is rare and in fights, so that is the user's call.
- **Emitters.** 120 torches: recipe `emitters` on Object42 at [0,-30,240] and Object43 at [0,0,190], kind fire, as `DoungeonGate01.json` does.
- **Scrolling meshes.** The UV scroll on Object23-25 mesh 1 is like Devias's Ice Monster glow scroll, which the cook already carries (`f0d4b119`).
- **New effects.** Falling stones (29 hidden markers) and DungeonStone01 debris.
- **Traps** are a sim and figure question for the record agent: 59 trap NPCs whose figure is the world model.
- **kMaps and gates.** `src/game/world/maps.cpp:19-23` needs a row (map 1; arrival e.g. the middle of exit gate 2, 108,247). Gate 1 in `src/sim/gates.cpp:34` must be unsealed, and inner gates 3/5/7/9/11/13 plus exits 2/4/6/8/10/12/14/16 added. Also `minimap.cpp` `mapName` case 1.
- **Safe zone.** The Dungeon has none (0 tiles). The arrival and the town-portal rules must cope, since OpenMU's spawn gate is Lorencia's.
- **GROUNDED_TYPES** (`tools/cook.py`, devias-port §4.1): types 20-27 here are wood props, the squid streams and plaques. Key it by world before cooking the Dungeon.

---

### 4. Monster assets

OM `Version075/Maps/Dungeon.cs`:
- Breeds are at `:627-952`, and traps 100-102 at `:958-1017`.
- The spawn line counts (grep of NpcDictionary) are: Ghost 11 ×114, Skeleton 14 ×62, Cyclops 17 ×62, Larva 12 ×44, Elite Skeleton 16 ×44, Thunder Lich 9 ×42, Poison Bull 8 ×40, Skeleton Archer 15 ×36, Hell Hound 5 ×28, Hell Spider 13 ×18, Dark Knight 10 ×14 and Gorgon 18 ×8, plus traps 27/26/5.
- The Skeleton (14) is Lorencia's breed row.

Model file = `Monster{MONSTER_MODEL+1}.bmd` (`ZzzOpenData.cpp:2556-2561`). Default PlaySpeeds (`:2597-2604`): stop 0.25/0.2, walk 0.34, attack 0.33, shock 0.5, die 0.55. Per model: DK (model 3) ×1.2 on all actions (`:2608-2610`), Larva walk 0.6, Hell Spider 0.7 and Cyclops 0.28 (`:2620-2623`).

| # | breed | CreateMonster (`ZzzCharacter.cpp`) | file | scale | arms | MU2_BGFX status |
|---:|---|---|---|---:|---|---|
| 5 | Hell Hound | `:14121-14140` | Monster02 (shared) | 1.1 | Falchion=Sword08 + Plate Shield=Shield10; HiddenMesh 1 | **mesh have**: `source/monsters/Hound01` (cooked, figures.json). Needs a variant row like EliteBullFighter01 (hidden_mesh 1). Sword08 and Shield10 are cooked (wardrobe) |
| 8 | Poison Bull | `:14071-14094` | Monster01 (shared) | 1.0 | Great Scythe=Spear09; poison debuff | mesh have (BullFighter01). Variant row needed. Spear09 is cooked |
| 9 | Thunder Lich | `:14158-14173` | Monster05 (shared) | 1.1 | Thunder Staff=Staff04 | mesh have (Lich01). Variant row. Staff04 is cooked |
| 10 | Dark Knight | `:14149-14156` | **Monster04** | 0.8 | Double Blade=Sword14 (cooked) | **missing**. 788 tris, 48 bones, 7 actions; baron.tga (34 tris, alpha) + baron01.jpg |
| 11 | Ghost | `:14099-14106` | **Monster08** | 1.0 | — | **missing**. 260 tris, 35 bones; eagle.tga (66, alpha) + eagle01.jpg. Draw at alpha 0.4 |
| 12 | Larva | `:14107-14112` | **Monster07** | 0.6 | — | **missing**. 238 tris, 11 bones; snake.jpg + snake02.tga. Walk 0.6; smoke trail |
| 13 | Hell Spider | `:14113-14119` | **Monster09** | 1.1 | Serpent Staff=Staff03 (cooked) | **missing**. 587 tris, 46 bones; big_spider.jpg. Walk 0.7 |
| 14 | Skeleton Warrior | `:14184-14207` | Skill/Skeleton01 on the player rig | 0.95 | Gladius=Sword07 + Buckler=Shield05 | **have**: SkeletonWarrior + Skeleton01 (cooked, Lorencia) |
| 15 | Skeleton Archer | `:14209-14216` | **Skill/Skeleton02** | 1.1 | Elven Bow=Bow03 (cooked) | **missing** part. 784 tris; bons + bons_a.jpg + bons_a2.tga |
| 16 | Elite Skeleton | `:14218-14226` | **Skill/Skeleton03** | 1.2 | Tomahawk=Axe04 + Skull Shield=Shield07 (both cooked) | **missing** part. 676 tris; bons + bons_b.jpg |
| 17 | Cyclops | `:14061-14067` | **Monster11** | 1.0 | **Crescent Axe=Axe09: no recipe in source/items** | **missing**. 738 tris, 44 bones; ogre.jpg. Walk 0.28 |
| 18 | Gorgon | `:14046-14053` | **Monster12** | 1.5 | Gorgon Staff=Staff05 (cooked; glow sprite) | **missing**. 852 tris, 41 bones; boss.jpg + boss_eye.jpg (BlendMesh 1, flicker) |
| 100-102 | traps | `:14274-14282` | Object2 Object40/41/52 | — | — | missing (world models) |

- Six monster models to import: Monster04, 07, 08, 09, 11 and 12. There are also two skeleton bodies, Skeleton02/03, which follow the Skeleton01 pattern, and three variant rows (Hell Hound, Poison Bull, Thunder Lich).
- One weapon is missing (Axe09). Everything else the Dungeon holds is already cooked in `assets/cooked/wardrobe`.
- `figures_devias.json` shows the per-world figure table, so the Dungeon needs `figures_dungeon.json`.
- MuExtract exports are in scratch `mon/`.

---

### 5. The Lorencia side: DoungeonGate01

- **Placement.** It is Lorencia type 55, one placement, `at [12250, 23400, 165]`, angle z 360, scale 1 (`source/world/lorencia/lorencia.json`). That is tile **(122.5, 234)**: the gate stands over OpenMU's enter gate 1 box 121,232-123,233 (`OM Version075/Gates.cs:185`, level 20).
- **Model.** `D/Object1/DoungeonGate01.bmd` (`MapManager.cpp:1043`, `MODEL_DUNGEON_GATE = 55`, `_enum.h:765`). It has one sheet, copra_gate (flagstone), and the recipe is `source/world/lorencia/DoungeonGate01.json` (1217-line .obj).
- **Effect.** MuMain gives it two braziers: `CreateFire(0, o, -150, -150, 140)` and `CreateFire(0, o, 150, -150, 140)` (`ZzzObject.cpp:3815-3818`). This is already transcribed as the recipe's two `emitters` (fire, (1,.6,.4), 0.6-1.1, 4 tiles). Nothing else is drawn on it.
- **Sim.** `src/sim/gates.cpp:32-34` has gate 1 as `{1, 0, {121,232,123,233}, 20, -1, "The Dungeon"}`, sealed. Unsealing it means pointing its target at exit gate 2 (107,247-110,247, facing 1).
- **Flavour.** `src/game/play.cpp:1296` and `src/sim/quests.cpp:38-82` already talk about the Dungeon.

---

## Part C: the engine side

Research, 2026-09-30, read-only (nothing in the repo edited, no window opened). Paths are
relative to `MU2_BGFX/` unless marked. OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`,
MM = `LEGACY/reference/MuMain/src/source/`. `docs/devias-port.md` §5 is the checklist the third
world walked and is the template; this page is what is different about a fourth world that is
underground and has three levels on one terrain.

A scratch extraction of World2 was made to answer the engine questions (not into the repo):
`scratchpad/dungeon/dungeon/{dungeon.json,attributes.png,height.png,light.png,tiles.png}` via
`python3 pipeline/terrain.py $D/World2 dungeon <scratch> 2`. NB: `scratchpad/dungeon/` also holds
another session's files (075.pkl, client.pkl, cmp.py, regions.py, terrain_lib.py, obj/); left alone.

---

### 0. The record the engine has to carry (OpenMU Version075, MuMain)

- `OM/Version075/Maps/Dungeon.cs:21` Number = **1**. World folder is `Data/World2` + `Object2`
  (terrain.py map_number 2 -> index number 1).
- **No exit gate on map 1 is a spawn gate** (`OM/Version075/Gates.cs:118-125`), so
  `OM/BaseMapInitializer.cs:91` SafezoneMapNumber falls back to **Lorencia**: death and a Town
  Portal in the Dungeon land in Lorencia. The engine has no "safe zone on another map" (§2.5).
- Warp list `Gates.cs:48-50`: Dungeon 3000 zen lvl 30 -> exit 2; Dungeon2 3500 lvl 40 -> exit 6;
  Dungeon3 4000 lvl 50 -> exit 10. Three warp targets on ONE map.
- Exit gates on map 1 (`Gates.cs:119-125`, dir as OpenMU Direction): 2 (107,247-110,247 W),
  6 (231,126-234,127 W), 8 (240,149-241,151 S), 10 (3,83-4,86 S), 12 (3,16-6,17 S),
  14 (29,125-30,126 W), 16 (5,32-7,33 W). Lorencia's exit **4** (121,231-123,231, W) is where you
  come back out.
- Enter gates (`Gates.cs:185-192`): Lorencia **1** 121,232-123,233 lvl **20** -> 2;
  Dungeon 3 108,248-109,248 lvl 0 -> 4 (to Lorencia); 5 239,149-239,150 -> 6; 7 232,127-233,128 -> 8;
  9 2,17-2,18 -> 10; 11 2,84-2,85 -> 12; 13 5,34-6,34 -> 14; 15 29,127-30,127 -> 16 (all lvl 20).
- **The three levels are three disconnected regions of one grid** (flood fill of open tiles,
  8-connected, on the client's World2 grid):

  | region | open tiles | gates in it | MU name |
  |---|---:|---|---|
  | A | 14 392 | exit 2 / enter 3 (Lorencia), enter 5, exit 8 | Dungeon 1 |
  | B | 9 426 | exit 6 / enter 7, enter 9, exit 12, enter 13, exit 16 | Dungeon 2 |
  | C | 3 415 | exit 10 / enter 11, exit 14 / enter 15 | Dungeon 3 |

  So 5: A->B, 7: B->A, 9: B->C, 11: C->B, 13: B->C, 15: C->B. **Every one of those six is a
  same-map gate**, and the router can never walk between regions: the gates are the only way.
  Every gate tile is word 0 (open) except exit 8's 240,151 (word 4, blocked -- the landing's
  nearestOpen covers it).
- **Exit 6 overlaps enter 7** on tiles 232,127 and 233,127. A landing drawn there stands inside
  the gate back; the realm only tests a gate on a step that changes tile
  (`src/sim/realm_move.cpp:160-163`), so it fires on his next step within the box, not on landing.
  MU behaves the same (CheckGate re-fires on the tile once LoadingWorld is 0,
  `MM/Engine/Object/ZzzInterface.cpp:2710-2728`). Keep, or draw the landing outside enter boxes
  (invention) -- a decision, not a bug.
- Grid facts (scratch extraction): height 0.00-3.11 m; 41.6% walkable, **0 safe tiles**;
  words {0: 27289, 4: 11994, 8: 21592, 12: 4660, 14: 1}; **26 253 NoGround tiles (40%)**; **no
  TW_CAMERA_UP (0x80) tile**, so MuMain's CustomDistance camera lift
  (`MM/Camera/DefaultCamera.cpp:703-724`) never fires here. MU's sanity tile
  (`MM/Render/Terrain/ZzzLodTerrain.cpp:207-208`) `TerrainWall[120*256+227] == 4`.
  Light mean (108,108,140). **4488 placements over 61 kinds** (Lorencia 2870/109, Devias 2237) --
  1262 Object01, 588 Object04, 385 Object48, 235 Object49; Object61 has no .bmd.
- Tile slots: **slot 0 is TileGrass01 at 75.7% of the floor**, TileRock01 11.8%, TileWood01 5.4%,
  TileWater01 5.1%, TileGround03 0.6% (slot 4). See §3 on why that matters.
- Spawns `OM/Version075/Maps/Dungeon.cs:45-~620`: **571 rows, almost all single-tile**
  (`CreateMonsterSpawn(id, def, x, y)` = box of 1, count 1, `OM/BaseMapInitializer.cs:269-270`).
  Breeds 5 Hell Hound, 8 Poison Bull, 9 Thunder Lich, 10 Dark Knight, 11 Ghost (114 rows),
  12 Larva, 13 Hell Spider, 14 Skeleton (Lorencia's breed), 15 Skeleton Archer, 16 Elite Skeleton,
  17 Cyclops, 18 Gorgon; and **100 Lance Trap, 101 Iron Stick Trap, 102 Fire Trap (58 rows)** --
  `ObjectKind = Trap`, MoveRange 0, cannot be attacked (`GameLogic/NPC/TrapIntelligenceBase.cs`,
  `RegisterHit` throws), ticking every AttackDelay. MuMain made the trap spawn list from the
  map's placed objects 39/40/51 (`MM/Engine/Object/ZzzObject.cpp:5077-5105`, SaveTrapObjects).
- mu.db today (`sqlite3 -readonly source/mu.db`): kinds 0-4, 6, 7, 14, 19-25, 26-33 -- **none of
  5, 8-13, 15-18, 100-102**; spawns only maps 0, 2, 3; gates rows 17 and 27 only.

---

### 1. How a world is defined and loaded

#### 1.1 The registry

There is no single registry; a world is a folder name found by convention, plus three tables:

| what | where | holds |
|---|---|---|
| **`kMaps`** | `src/game/world/maps.cpp:19-23` | `{world, MU map number, arrive tile}`; `mapOf` `:27`, `mapNumbered` `:34` (a gate's target), `mapAfter` `:41` (the M key's order) |
| index.json `worlds[]` | `pipeline/index.py:1381-1480` `worlds()` | globs `source/world/*/*.json` with `objects` + `height`, needs `build/world/<name>/<name>_ground.glb`; `number = map_number - 1` (`:1465`) |
| safe gate | `pipeline/index.py:1755-1770` `gate_rows` + `:2838-2843` | `gates WHERE spawn = 1` by map -> `world["gates"]["safe"]` |
| `kPlaces` (arrival banner) | `src/game/ui/arrival.cpp:57-61` | name shown on arrival |
| `mapName` | `src/game/ui/minimap.cpp:246-253` | "Gate to X" on the minimap |

#### 1.2 Source files per world

`source/world/<name>/`: `<name>.json` (terrain.py's output: `world, map_number, size,
units_per_tile, height_factor, height/tiles/attributes/light` png names, `tile_slots`,
optional `grass_slots`, `water_flow`, `void`, `objects[]`, `blocked[]`), the four PNGs,
`ground.json` (`tileset, mu_name, profile, sheets, sheet_from, sheet_materials` -- the tile
sheets, read by `tools/content.sh` -> tileset.py/ground.py), `placements.json` (corrections, NPC
`spawns`), and one `ObjectNN.json` recipe (+ .obj/.rig.json) per model.
Per-map constants that terrain.py/index.py bake in, keyed by **map number - 0-based**:
`HIDDEN_BY_MAP` `pipeline/terrain.py:118`, `BLEND_MESH_BY_MAP` `:135`, `GRASS_BY_MAP` `:148`,
`WATER_FLOW_BY_MAP` `:158`, `OPERABLE_BY_MAP` `:169` and again `pipeline/index.py:44`,
`GATE_BOXES_BY_MAP` `pipeline/index.py:55` (gate tiles kept open by stamp_blocked, `:1339-1345`),
grass sheets `pipeline/index.py:555-563`.
The ground reader's `void` block (`src/content/ground.cpp:642-646`: `fade, sink, start, depth`) and
the NoGround abyss (`buildAbyss` `:1042`) are generic -- Devias's chasms proved them.

#### 1.3 The cook (`tools/cook.py main` `:3151-3270`)

`--world W --only {textures, ground, meshes, placements, figures, tables, showing, missiles,
wardrobe, all}`:
- `ground` -- the land's sheets alone (Noria/Devias first day), `:3240-3245`.
- `textures` -- every model's sheets + ground (`collect` `:226`, `ground_jobs` `:289`) -> `cooked/W/textures`.
- `meshes` `:700`, `placements` `:1276` (chunks; `GROUNDED_TYPES` `:1108` keyed by world,
  `ANCHOR_KINDS` `:1124` by model name, `HIDDEN_BY_WORLD` `:1134`) -> `cooked/W/W.mut`.
- `tables` `:1845-2160` -> `cooked/W/W.mur` v9: **all** breed kinds, this map's spawns
  (`spawn["map"] != number` filter `:1974`), arms, actions, **every item** (`:2066-2120`),
  `FOLK_VERSION075[number]` `:1746`, grid words, `PERCHES[number]` `:1821`, and the safe gate
  from index.json's world entry (`:2131-2136`; all zeroes = "nowhere to send anybody").
  `RESPAWN_VERSION075` `:1719` is keyed by breed number.
- `figures` -> `cooked/figures` (shared across worlds; `figures_W.json` per world, `:1552-1564`).
- `showing`/`missiles`/`wardrobe` are world-less.

**"New items need every world's tables"** is structural: the item table is copied into each
world's `.mur` (`cook_tables` `:2066`), so after any item import every world needs
`tools/cook.py --world W --only tables`. Nothing loops the worlds for you (only
`tools/content.sh:36` globs `source/world/*/ground.json`, and it does not cook tables). Adding
`dungeon` makes it four runs: lorencia, noria, devias, dungeon.

#### 1.4 The runtime load

`PlayMode::open` (`src/app/modes/play_mode.cpp:133-`):
1. `readSave` -- `saved_.world == args.world` resumes, else "starting new here" (`:46-55`).
2. `ctx.time.setScene(mapSheet(sheets, world))` (`:139`) -- `sheets/worlds/<world>.json` laid over
   `sheets/lighting.json`; `setWet(<world>_rain)` (`:141`). Layering in `TimeOfDay::set`
   (`src/app/context.cpp:28-53`): base -> time overlay (`sheets/time/dusk|night.json`, bench only;
   play stays at index 0) -> scene -> wet blend.
3. `World::open` (`src/game/world/world.cpp:92-163`): ground (`assets/world/<name>`), town
   (`.mut`), lamps, sway, doors (Devias only, `doors.cpp:44`), ornaments, shades, grass, portal
   (Noria only, `portal.cpp:40`), focus = `mapOf(name)->arrive` or 142,126, figures + crowd.
4. `setMapEdge` (`play_mode.cpp:167-175`), generic.
5. `World::play` -> `Play::open` (`src/game/play_open.cpp:22-`): `cooked/W/W.mur`; breeds with no
   cooked figure are **held back** (`:108-131`, "play: N monsters of W held back"); Noria's
   roads (`:144`); `realm_.raise`.
6. `World::raiseAirs` (`world.cpp:180-205`): boids (`boidOf`/`airsOf`, `boids.cpp:50-75`), leaves
   (snow iff devias, `world.cpp:206`), doors' sounds, `Weather::open` (`weather.cpp:103-135`).

#### 1.5 What is per world, and where it switches

| thing | switch | dungeon today |
|---|---|---|
| light/sky/fog/grass colours | `sheets/worlds/<w>.json` (Lighting fields `src/gfx/lighting.h:25-170`) | no sheet -> Lorencia's moonlit night |
| wet spell | `sheets/worlds/<w>_rain.json` + `Weather::open` list `weather.cpp:113-116` | not listed -> dry, correct |
| leaves pool | `World::raiseAirs` `world.cpp:206` -- every non-Devias world gets **leaves** | **leaves would blow in the dungeon** |
| birds | `boidOf` `boids.cpp:55-59`, `AIRS` `cook.py:202` | none (MU flies Bat01, and Rat01 in the fish slot, `MM/World/MapInfra/MapManager.cpp:51-58`, `MM/Engine/AI/GOBoid.cpp:1333,1718`) |
| wind loop | `Play::open` `windy_ = world != "noria"` `play_open.cpp:30`, played in `Play::hear` `play_sound.cpp:241` | **wind would howl underground**; MU plays SOUND_DUNGEON01 instead (`MM/Scenes/SceneManager.cpp:859-861`) |
| reverb room | `Play::hear` `play_sound.cpp:244`, `Sound::Room {Dry, Open, Roofed}` `sound.h:172` | Open unless on slot 4; `docs/spatial-sound.md:166-169` already plans a dungeon preset |
| footsteps | `play_sound.cpp:87` (snowy_) | grass/soil by slot |
| indoors | `World::indoors` `world.cpp:326-337`, `kIndoorFloor = 4` `:47`, `deviasFloors_` `:94` | slot 4 is TileGround03 in World2 -> 0.6% of tiles flip "indoors" at random |
| grass | `Ground` grass slots `ground.cpp:685-735`, `GRASS_BY_MAP` | **slot 0 TileGrass01 (75.7%) grows a sward** unless its ground_surfaces recipe is not "grass" or `GRASS_BY_MAP[1] = []` |
| music | `play_mode.cpp:750-790` by name (pub in Lorencia; hunt music off) | none; MU plays MUSIC_DUNGEON on the map (`SceneManager.cpp:1038-1041`) |
| arrival banner | `arrival.cpp:57-61` | missing -> no banner |
| minimap gate label | `minimap.cpp:246-253`, gates from `sim::enterGateAt` `:591-608` | "another map" |
| NPCs | `FOLK_VERSION075` + `placements.json spawns` | none in 0.75's dungeon (no NPC rows) |
| grade cube | `source/grades/<w>.json` -> index.py `:2801-2843` | **MU2's viewer only; nothing in src/ reads it** -- the look is the sheet |

---

### 2. Travel today

#### 2.1 Gates are code, not cooked

`src/sim/gates.cpp:9-32`: `kExits` 26, 24, 21, 19 and `kEnters` 23, 25, 18, 20, 28 (28 sealed,
target -1). Header `src/sim/gates.h:20-51` (box, dx/dy facing, level, target). mu.db's `gates`
holds only spawn rows and is read only for the safe box. **Lorencia's gate 1 (dungeon) and exit 4
are not in the committed table.** *(Settled above: another session's uncommitted `gates.cpp` already
carries gate 1, sealed.)*

#### 2.2 Walking into a gate

`Realm::step` -> on a tile change, `throughGate` (`src/sim/realm_move.cpp:160-163, 185-213`):
sealed -> `Barred(number, 0)`; under level -> `Barred(number, level)`; else a random tile in the
exit box (seeded dice), hero stopped and cleared (`rise`, `dropBlow`, orders, trade, bank),
`setDown` **on his current tile**, `say(What::Gated, number, column, row)`. The realm does NOT
move him; the map change is the game's.
`Play` catches the first `Gated` for the hero into `gated_` (`src/game/play.cpp:404-408`, only
while `gated_ == 0`; **never reset**, since a map change builds a new Play); `Barred` says
"Only characters of level N..." or the hard-coded "The Lost Tower is sealed" (`play.cpp:409-420`).
`PlayMode::frame` (`play_mode.cpp:546-561`) reads `gated()`, resolves
`enterGateNumbered -> exitGate -> mapNumbered(out->map)`, and calls `travel(world, col, row,
atan2(dy, dx))`, or logs "gate %d leads to a map this game has no world for".

#### 2.3 The map change

`PlayMode::travel` (`play_mode.cpp:1220-1244`): returns at once if the row is missing **or
`world == ctx.args.world`**; sets `arrive*_`, `travelTo_`, rewrites `args.world`, `args.at*`,
`args.savePath`, clears `--fresh`. `next()` returns `Next::Play` (`play_mode.h:33-35`), so the
Application tears the whole mode down and opens a new PlayMode on the new world (full unload/load
behind the preloader's spinner and the entrance fade). `keep()` (`:66-86`) writes the save with
`world = args.world` and the hero at the arrival tile. Save field: `"world"` string
(`src/game/save.cpp:131,240`). The new realm's `raise` moves him to the nearest standable tile.

#### 2.4 M, the stand-in Move window

`play_mode.cpp:563-570`: M (or `--travel-at F`) -> `travel(mapAfter(world))`, landing on the
row's `arrive`. Free, unlevelled, no zen.

#### 2.5 Death and the Town Portal

Both go through `Realm::haven` (`src/sim/realm_fight.cpp:752-770`): a random tile of
`tables_->safeGate`, else **where he stands**. Revive `reviveHero` `:783-800`; Town Portal
`src/sim/realm_items.cpp:476-505` (`say(What::Warped)`, and the game's `Play::warped`
`src/game/play_show.cpp:413-437`, called from `play_requests.cpp:86`). No path leaves the map.

**Side finding: Devias has no safe gate today.** mu.db has no row 22, so `assets/index.json`'s
devias entry has no `gates` and `devias.mur`'s safe box is zeros: a death or a Town Portal in
Devias revives him where he stands. `docs/devias-port.md` §2's `insert into gates ... (22, 2, 197,
35, 218, 50, 1)` was never run. (Checked: `sqlite3 -readonly source/mu.db "select * from gates"`
-> rows 17 and 27; index.json worlds: lorencia/noria have `safe`, devias `None`.)

#### 2.6 What happens if you walk into Lorencia's dungeon gate now

*(With the uncommitted sealed row, he is Barred with "The Dungeon" instead; see above.)* Committed tree: nothing. Tiles 121-123 x 232-233 are word 0 (open) in `assets/world/lorencia/attributes.png`; the
placement `DoungeonGate01` (type 55) stands at 122.5,234.0 behind them. `enterGateAt(0, ...)` finds
no row, so he walks onto the stairs and stands there.

---

### 3. What is outdoor-shaped, and where each switch goes

| # | thing | today | where the switch goes | cost/risk |
|---|---|---|---|---|
| 1 | **Sun + its shadow split** | always drawn: `Renderer` view 0 `src/gfx/renderer.cpp:637-738`, framed on the camera by `fitSplit`; sun from `azimuth/elevation/sun_*` | Sheet can set `sun_strength` low, but the pass still runs (~1.0 ms budget, `docs/budget.md:12`). Add a skip: when `lighting.sunStrength <= 0` clear the shadow map's depth and submit nothing (the shade then reads "lit"). **Decision:** MU draws body shadows in every map including the Dungeon, so a weak straight-down "shadow sun" may be the faithful look; try both in the bench. | small |
| 2 | Sky | no sky is drawn (camera looks down; shade clears to black, `renderer.cpp:848`); the sky exists as the hemisphere ambient and the reflection's closed form (`shaders/common.sh:60-80`, `fs_shade.sc:159-178`) | sheet only: `sky_colour`, `ground_colour`, `horizon_paleness`, `ambient_strength` dark and cold | none |
| 3 | Fog/haze | `dust_colour`, `dust_density` (lighting.h:45-46) | sheet: black-ish dust = MU's black clear/fog default (`MM/Scenes/SceneManager.cpp:401`, Dungeon falls to the default black) | none |
| 4 | Clear/void | shade clear black; NoGround tiles not drawn, abyss fade by depth (`ground.cpp:1042-1100, 1190-1310`), `void` block in world json | generic -- 40% of World2 is NoGround and will read as MU's black | none |
| 5 | Grass | `Ground::grassSlots_` by recipe/name (`ground.cpp:685-735`) | **must** set `GRASS_BY_MAP[1] = []` in `pipeline/terrain.py:148` (empty list = none, `ground.cpp:726-733`) and/or `"grass": 0` in the sheet (`Lighting::grass`, lighting.h:119). Otherwise slot 0 TileGrass01 (75.7% of the floor) grows a lawn | a trap if missed |
| 6 | Leaves / rain / snow | `World::raiseAirs` `world.cpp:206` opens leaves for every world but Devias; `Weather::open` `weather.cpp:113-116` | a world flag (e.g. `MapRow::underground`, or `Airs`) that skips `leaves_.open` and the wind; bats later in `boidOf` | small |
| 7 | Wind loop + room | `play_open.cpp:30`, `play_sound.cpp:241-244` | `windy_ = false` for dungeon; loop `Dungeon01.wav` (SOUND_DUNGEON01) instead; `Room::Roofed` (or a new `Cave`) always | small |
| 8 | Indoors | `world.cpp:47,94,326-337` | dungeon: treat as indoors everywhere (roofs: none to hide; wind/leaves off) -- a third branch beside `deviasFloors_`, or `MapRow` flag | small |
| 9 | Day/night | no cycle in play; the base sheet IS Lorencia's night, `sheets/time/*.json` bench only (`context.cpp:28-53`) | the dungeon sheet overrides whatever it needs; nothing to switch off | none |
| 10 | Reflection probe | live cube round the player, one face a frame, 0.3 ms (`src/gfx/renderer_probe.cpp`, gated by `lighting.probe`, `renderer.cpp:509-516`); its background is the closed-form sky (`fs_probe_sky.sc`) | works underground once the sky colours are dark; `"probe": 0` in the sheet saves 0.3 ms if the dungeon has no metal worth it | none |
| 11 | Camera | fixed 9.5 m, pitch -48.5, yaw 45, focus ground+1.5 m (`world.cpp:14-45, 247-270`); no occlusion handling | World2 has **no CAMERA_UP tiles**, so MU never lifted it here either. Walls (Object01 x1262) south-east of the hero may cover him -- judge on the first `--shot`; a wall fade would be ours | watch |
| 12 | Water | `water_flow` sheet knob + `WATER_FLOW_BY_MAP` (terrain.py:158) | 5.1% TileWater01; default MU slide; `"water_flow": 0` if it reads as a river in a hole (Devias/Noria did) | none |
| 13 | Lamps | point lights come from each model recipe's `lights` + `ANCHOR_KINDS` by name (cook.py:1124); 255 max (`renderer.h:373`) | torches are content: dungeon ObjectNN recipes carry their lights | content |
| 14 | Map edge | `setMapEdge` 8 m band (`play_mode.cpp:167-175`) | generic | none |
| 15 | Music | by name `play_mode.cpp:750-790` | MUSIC_DUNGEON is MU's; the user keeps music rare ("music is rare and in fights") -- ask | ask |

---

### 4. Multi-level: a gate whose target is the same map

**Today it silently fails.** The realm stops him and says `Gated`; Play latches `gated_`;
`PlayMode::frame` resolves the target row -> `travel()` returns at `world == ctx.args.world`
(`play_mode.cpp:1222`). He stands in the gate, not moved; `gated_` is never cleared, so every
later gate is ignored (`play.cpp:404` tests `gated_ == 0`) and `travel()` is re-called (and
returns) every frame.

**What it needs (the realm owns it, the game draws it):**
1. `Realm::throughGate` (`realm_move.cpp:185-213`): when `out->map == tables_->map`, do what the
   Town Portal does (`realm_items.cpp:487-504`): `setDown(hero, column, row)` at the drawn tile
   run through `router_.nearestOpen` (as `haven` does, `realm_fight.cpp:760-765`), face
   `atan2(dy, dx)`, drop every monster's quarry on him, dismiss the summon, and say a new
   `What::Stairs` instead of `Gated` (not `Warped`: its log line is "reads a town portal",
   `src/sim/realm.cpp:883-886`, and the seeded logs read those bytes). Return true.
2. `Play` event loop (`play.cpp:~404`): on that event for the hero, call `warped()`
   (`play_show.cpp:413`) -- it already snaps the drawn body, dismisses the move marker and plays
   the warp's landing. Jumps > 2 tiles already snap (`play.cpp:37-40`, `play_show.cpp:647`); the
   camera snaps at > 4 m (`world.cpp:59`, `kSnapMetres`).
3. Optional: the arrival banner with "Dungeon 2/3" on a region change (invention; MU shows nothing).
4. The minimap's "Gate to" label (`minimap.cpp:809-812`): a same-map target should read
   "Stairs down"/"Dungeon 2", not "Gate to Dungeon".
5. `Barred` text (`play.cpp:412-416`) hard-codes the Lost Tower for level 0; fine while only 28
   is sealed, but key it on the gate if any dungeon gate is ever sealed.
6. sim_test: a hero at level 20 walked into gate 5 comes out in exit 6's box on the same realm,
   tables unchanged; level 19 is Barred. Needs the dungeon's `.mur` (sim_test loads Lorencia's
   at `tests/sim_test.cpp:3905`; the existing `testGates` is at `:3359-3400`).

The router cannot cross regions (A/B/C are separate components), so a click into another level
from the wrong one routes nowhere -- that is correct and needs nothing.

#### 4.1 Cross-map "safe zone"

The Dungeon's death and Town Portal must send him to **Lorencia** (§0). Proposed:
- cook: a `safe_map` on the world entry (index.py `:2838-2843`, from OpenMU's rule "no spawn
  gate -> Lorencia"), written into the `.mur` header (bump v9 -> v10: `tools/cook.py:2131`,
  `src/content/tables.cpp:43`, `tests/cooked_test.cpp` and cookcheck move together, per
  `docs/architecture.md` "content").
- sim: `haven()` / `reviveHero` / the portal branch: when `safeMap != map`, set him down where he
  is and say `What::Homeward` (or Gated with a sentinel).
- game: `Play` latches it like `gated_`; `PlayMode::frame` calls `travel("lorencia")` (arrive
  142,126, inside gate 17).

---

### 5. Spawns and tables

- Breeds and nests come from mu.db `monster_kinds` / `monster_spawns(map)`, through
  index.json `breeds[].spawns` (index.py `:1625-1715` per devias-port.md) into `cook_tables`.
- NPCs: `FOLK_VERSION075[number]` (`tools/cook.py:1746`) + `placements.json` `spawns` for the
  figure; the dungeon has none.
- Held back until cooked: a breed with no figure never raises in the window
  (`play_open.cpp:108-131`); headless raises everything (`headless.cpp:148-170`).
- How Devias's monsters went in (the pattern to copy):
  - `6d209434`: mu.db rows for 7 kinds + 9 spawn boxes (`RESPAWN_VERSION075` keyed by breed),
    first two recipes `source/monsters/Worm01.json`, `Assassin01.json` (+ .obj, .rig.json),
    their sounds in `source/sounds/m*.wav`, `docs/cook-log.md`.
  - `fb667b0d` Elite Yeti: recipe + texture, breath hook in `play_open.cpp` / `play_show.cpp` /
    `play_tuning.h` (`Play::snort`, `Drawn::snortAlways`).
  - `f0d4b119` Ice Monster: recipe, `pipeline/index.py` (+5, the glow), `tools/cook.py` and
    `cook_one.py` (glow scroll into the .mum), `game/fx/ice.*`, `play.cpp/h` (`iceCasts_`),
    `play_show.cpp` (`sandOnDeath` shatter), `play_tuning.h`.
  Per breed: `cook.py --only figures --world W --monsters Name01`, judged one at a time.
- Dungeon specifics for the sim:
  - 571 one-tile nests: the nest table is a flat vector; fine. `zoneLevels` (`maps.cpp:49-62`)
    will say "Level 1x-80" once the traps (level 80) are in -- exclude traps.
  - Existing mu.db ids are not OpenMU's; take `max(id)+1`.
  - Breed 14 (Skeleton) is Lorencia's -- shared; its figure is already cooked.
  - Monster skills: Poison (Poison Bull 8, Larva 12), Lightning (Thunder Lich 9) --
    `play.cpp` special-cases attack skills by number (Lich's meteor, Ice Monster's Ice); each is
    showing work.
  - **Traps (100-102) are a new kind of body**: stationary, untargetable, deathless, fire on a
    timer at whoever is in range (OpenMU `AttackSingleWhenPressedTrapIntelligence` etc.), drawn
    as the placed objects 39/40/51 (Iron Stick at `Models[40].Actions[1].PlaySpeed = 0.4`,
    `MM/World/MapInfra/MapManager.cpp:1131-1133`). Until the sim has a trap temper, leave 100-102
    out of mu.db (or let them be held back for want of a figure). Separate sprint.
- Tables rule: after adding the world, `--only tables` for all four worlds after any item change
  (§1.3).

---

### 6. Touch-points, as small steps with a headless check each

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. Window runs are `--frames`
reviews read through `mu2.log` + the PNG (MU2_BGFX allows those; ask the user before any window).

**Step 1 -- bare land stands, reachable by M.**
- `pipeline/terrain.py`: `HIDDEN_BY_MAP[1] = {39, 40, 51, 52, 60}` (settled above: the traps too) (52 is the falling-stone emitter,
  60 the lean box, `MM/Engine/Object/ZzzObject.cpp:3872-3878, 4655-4665`), `OPERABLE_BY_MAP[1] =
  {59, 60}` (also `pipeline/index.py:44`, `MM/Engine/Object/ZzzInterface.cpp:1707-1712`),
  **`GRASS_BY_MAP[1] = []`**. Types 22-24 are a V-scrolled StreamMesh (`ZzzObject.cpp:3880-3884`),
  not a blend -- the Ice Monster's glow scroll (`f0d4b119`, cook.py/cook_one.py) is the precedent.
- `python3 pipeline/terrain.py $D/World2 dungeon source/world 2`; `source/world/dungeon/ground.json`
  (prefix `dn_` for its tile sheets, as `nr_`/`dv_`); `tools/content.sh --world dungeon`;
  `tools/sync.sh --world dungeon --only-world`; `tools/sync.sh`;
  `tools/cook.py --world dungeon --only ground`; `--only tables`.
- `src/game/world/maps.cpp:19-23`: `{"dungeon", 1, {108, 247}}` (exit gate 2's middle; there is no
  spawn gate) appended last; comments in `maps.h:20-23`.
- `arrival.cpp:57-61` `{"dungeon", "Dungeon", ""}`; `minimap.cpp:246-253` `case 1: "Dungeon"`.
- Check: `./run.sh --headless --world dungeon --ticks 200 --no-hand` logs
  `tables: ... grid 256 tiles a side ... map 1`; `python3 -c` on `assets/cooked/dungeon/dungeon.mur`
  header (`'MU2R', 9, ..., map 1`). Window, when allowed: `./run.sh --world devias --play --fresh
  --travel-at 60 --frames 300 --shot 250` -> log `travel: devias to dungeon`, `world dungeon:
  looking at tile 108,247`.

**Step 2 -- Lorencia's gate 1 and the way back.**
- `src/sim/gates.cpp`: exits `{2, 1, {107,247,110,247}, -1,-1}`, `{4, 0, {121,231,123,231},
  -1,-1}`; enters `{1, 0, {121,232,123,233}, 20, 2}`, `{3, 1, {108,248,109,248}, 0, 4}`;
  header comment `gates.h:11-13`.
- `pipeline/index.py:55` `GATE_BOXES_BY_MAP[0] += (121,231,123,233)`, `[1] = [...]` every box.
- `tests/sim_test.cpp`: gate 1 at level 20 -> Gated 1 with a tile in 107-110,247; level 19 ->
  Barred 20. Check: `cmake --build build --target checks`.
- Window check: `./run.sh --world lorencia --play --fresh --level 20 --at 122,229 --walk-to
  122,232 --frames 400` -> `travel: lorencia to dungeon, coming in at 10x,247`.

**Step 3 -- the three levels (same-map gates).**
- §4 items 1-2 (`realm_move.cpp:185-213`, a new `What` in `src/sim/realm.h` / `realm.cpp:883-891`
  log names, `play.cpp` event branch -> `warped()`), then enters 5/7/9/11/13/15 and exits
  6/8/10/12/14/16 in `gates.cpp`.
- Check: sim_test "gate 5 lands in exit 6's box on the same map, no Gated, hero id unchanged";
  headless: `--at 238,149 --level 20` plus a scripted step, or the test alone.

**Step 4 -- the look (underground switches).**
- `sheets/worlds/dungeon.json`: dark sky/ground/ambient, black dust, sun low or off, `grass 0`,
  `water_flow` as judged, `probe` as judged. MU's TerrainLight (mean 108,108,140) already carries
  the torch pools on the ground and objects.
- Code: shadow skip at `sunStrength <= 0` (`renderer.cpp:637`) if the sun goes off; a
  `MapRow::underground` (or name test) for: no leaves (`world.cpp:206`), no wind + dungeon loop
  (`play_open.cpp:30,428`; `Dungeon01.wav` to `source/sounds`), indoors everywhere
  (`world.cpp:326-337`) -> Roofed room.
- Check: `--frames 300 --shot 250 --shot-path <abs>` in the dungeon; the log has no
  `leaves:` line and a frame line; `--budget` against docs/budget.md (4488 placements is 1.6x
  Lorencia's -- read the chunk/draw counts it logs every second).

**Step 5 -- objects.** One `source/world/dungeon/ObjectNN.json` per kind (`dg_` sheets), worklist
by count (Object01 1262, Object04 588, Object48 385, Object49 235). Check per kind:
`cook_one.py ObjectNN --world dungeon` + the studio sheet; then `--only textures/meshes/placements`.

**Step 6 -- death and the portal home.** §4.1 (`safe_map`, `.mur` v10, `What::Homeward`,
`PlayMode` travel to Lorencia). Check: sim_test "hero killed on map 1 -> Homeward, not Rose in
place"; cooked_test on v10. (While there: add Devias's missing gate-22 spawn row, §2.5.)

**Step 7 -- monsters, breed by breed.** mu.db `monster_kinds` 5, 8-13, 15-18 and the 513
non-trap spawn rows for map 1 (then `pragma wal_checkpoint(TRUNCATE)`); recipes from
`$D/Monster`; `cook.py --only figures --world dungeon --monsters X01`. Check: headless
`--world dungeon --ticks 30000 --level 40` population and the audit; window log's "held back"
list shrinks per breed.

**Step 8 -- traps.** A trap temper in the sim (no move, no target, no death, timer attack),
kinds 100-102 and their 58 rows, drawn as placements 39/40/51. Separate sprint.

**Step 9 -- airs and details.** Bats (Bat01) and rats (Rat01) in `boidOf`/`AIRS`; the falling
DungeonStone from type 52 (`ZzzObject.cpp:3875-3877`); the warp list's Dungeon2/3 targets if a
Move window ever exists.
