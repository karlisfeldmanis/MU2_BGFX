# Lost Tower port: the record, the data, and how it plugs in

Research notes, 2026-10-01. The Lost Tower is MU server map **4**, client `Data/World5` + `Data/Object5`
(MuMain's `WD_4LOSTTOWER`), entered from Devias's south-west corner by gate 28, which `gates.cpp`
carries sealed today. Three research passes wrote this page in `docs/dungeon-port.md`'s shape:
- Part A is the record: gates, warps, grid, spawns, breeds, drops and rules, read in OpenMU 075/095d/S6
  and checked against WebZen 1.00.93.
- Part B is the data and the look.
- Part C is the engine side, ending in the build steps.

Nothing but this page was edited, and no cook or window was run.

Paths:
- MuMain source: `LEGACY/reference/MuMain/src/source/` (MM)
- MuMain data: `LEGACY/reference/MuMain/src/bin/Data/` (MMD, or D)
- OpenMU init: `LEGACY/reference/openmu/src/Persistence/Initialization/` (OM)
- OpenMU game logic: `LEGACY/reference/openmu/src/GameLogic/` (OMGL)
- WebZen C++: `Source/Server Side/GameServer/` of github `ptr0x-real/Mu-GS-Webzen-MC-10093` (WZ)
- WebZen's repack data (WZD), trusted for Monster.txt only

The scratch folder
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/35929550-30ce-42bd-8b26-8e28d95df725/scratchpad/losttower/`
holds:
- the flood-fill, gate and spawn scripts and their outputs;
- a sparse WZ clone with WZD's `Gate.txt`, `Monster.txt`, `MonsterSetBase.txt` and `Terrain5.att`;
- in `B/`: `terrain.py`'s World5 output, every Object5 model, and Monster27-31, Sword14/15 and Mace04
  exported through MuExtract, plus decoded sheets and contact sheets.

## The one thing to know first

**Seven floors, one-way down, with a safe hall of its own.** The Lost Tower is seven disconnected
regions of one 256² grid. Six same-map stairs join them, and **every stair goes down**. OM, WZ, WZD
and Season 6 agree that no gate leads back up.

Unlike the Dungeon, the tower **has a safe zone**: a 28×14 hall on floor 1 holding spawn gate 42.
Death and the Town Portal therefore land in the tower, not in Lorencia. That needs one mu.db row,
`gates (42, 4, 203,70,213,81, 1)`. Without it, every death silently goes to Lorencia, which is
Devias's gate-22 bug again.

**The client grid can be used as is.** MuMain's `EncTerrain5.att` is byte for byte WZD's. It differs
from OM's 075 file on 1 271 tiles, but only 80 of those are inside the floors, and none is a spawn,
a trap or a whole gate.

## In one screen

- **In 0.75: yes, all seven floors.** OM 075, 0.95d (the same class) and S6 (a subclass that changes
  only the terrain file) have the same floors and gates. WZ's C++ agrees (A §1).
- **The floors** (A §1.2, B §1.1, C §0.1):

  | floor | grid box | tiles | arrival | stair down | breeds | traps |
  |---|---|---:|---|---|---|---:|
  | LT1 | x 162-247, y 0-138 | 7 258 | 29 (from Devias), hall 42 | 30 | Shadow, Poison Shadow | 14 |
  | LT2 | x 162-248, y 164-250 | 5 954 | 31 | 32 | Poison Shadow, Cursed Wizard | 7 |
  | LT3 | x 80-135, y 165-250 | 4 028 | 33 | 34 | Cursed Wizard, Death Cow | 37 |
  | LT4 | x 80-135, y 85-140 | 2 501 | 35 | 36 | Death Cow, Devil | 20 |
  | LT5 | x 80-135, y 5-60 | 2 396 | 37 | 38 | Devil, Death Knight | 0 |
  | LT6 | x 2-57, y 5-60 | 2 270 | 39 | 40 | Devil, Death Knight, Death Gorgon | 0 |
  | LT7 | x 2-57, y 85-250 | 5 826 | 41 | none | Death Gorgon, Death Knight, Devil, **2 Balrogs** | 70 |

  The floor boxes do not overlap, so plain box tests can name a floor.
- **The ways in and out.**
  - Devias gate 28 (2,248-3,249) asks **level 40** and lands on exit 29.
  - The stairs ask **40, 40, 50, 50, 50, 50**.
  - LT1's gate 43 (162,0-166,1) asks 15 and leads to Devias exit 44 (2-3,246-247).
  - The warp list has seven rows on one map: 5 000-8 000 zen at levels 50/50/50/60/60/70/70.
    "LostTower" lands in the hall.
  - WZD's 80 on every gate is the repack's, not trusted.
  - The walk from Devias to the Balrogs is about 950 steps over six stairs.
- **Eight breeds, levels 47-66, and 148 Meteorite Traps.**
  - The breeds: Shadow 47, Poison Shadow 50, Cursed Wizard 54, Death Cow 57, Devil 60,
    Death Knight 62, Death Gorgon 64, and the **Balrog** (66, 9 000 HP, two side by side on LT7).
  - There are **448 monsters on one-tile spots**. The table is not doubled this time.
  - None of the eight is in mu.db.
  - Five new bodies: Monster27-31 (Devil, Balrog, Shadow/Poison Shadow, Death Knight, Death Cow).
  - The Death Gorgon re-dresses `Gorgon01`. The Cursed Wizard is a player body in the Legendary set
    +9 with Staff06 and Shield15.
  - Every weapon is cooked except the Death Cow's **Great Hammer (Mace04)**.
- **What already works through code we have:**
  - Cursed Wizard: the Lich's meteor (`attack_skill 2`)
  - Devil: the Thunder Lich's lightning (`3`)
  - Poison Shadow: `kPoisoners` already lists 39
  - New code: the bosses' area blow, and the trap's "pressed to arm, area to hit" trigger
- **Traps.** 148 Meteorite Traps sit on open tiles. In OM a hero standing on the plate gets Flame
  of Evil on everyone within 3 tiles, every second. The client's hidden type-25 objects are exactly
  these 148 tiles. The showing is the meteor rain that `fx/meteor.h` already draws.
- **The look is not the Dungeon's.**
  - MuMain gives the tower **no torches, no thunder and no weather**. The "lightning" players
    remember is the floor machines' spinning blue sprites and the monsters' attack beams.
  - `TileWater01` is **lava**, sliding one sheet every 20 s.
  - The painted TerrainLight carries the floors: grey on LT1-3, lava red on LT4, LT5 and LT7,
    violet on LT6.
  - What moves:
    - red light streaming through slots in 610 wall pieces (a Chrome01 underlay)
    - 163 blue additive lamp posts
    - 95 hidden vents firing the wizard's Flame (`fx/flame.h` is that effect)
    - 1 112 skulls and stones the hero kicks as he walks
  - Bats fly here as in the Dungeon. No rats.
  - `aTower.wav` loops always. `lost_tower_a.mp3` plays always in MU. `lost_tower_b.mp3` is never
    played.
- **Size.**
  - 5 380 placements of 39 kinds, about 1.95× Lorencia. The 2K table says geometry is not this GPU's
    cost.
  - 46.1% walkable.
  - `Object41` has no `.bmd`. `Object01`, `Object03` and `Bug01` are never placed.

## Where the three passes disagreed, settled

| point | A / B / C said | settled |
|---|---|---|
| Grid difference | A: 1 271 bytes. B: 973 walkability flips | Both are right: A counted bytes, B counted walkability. What matters is A's 80 tiles inside floors. **Client grid** |
| `attack_skill` for the Death Gorgon and Balrog | A: 50 (WZ's Flame of Evil). C: 150 or NULL, read only by the showing | `attackSkill` only picks the drawing (`content/tables.h:39`). Write **50**, and build the one-in-five area blow in the sim as new code |
| Respawn | C: write 10 s and Balrog 150 s into mu.db. A: the user's WebZen rule (webzen-audit #9) gives 6 s and Balrog 11 s through `kDropRates` | Write OM's numbers into mu.db, as the Dungeon did. **The live value follows the user's standing WebZen rule**, so it is Decision 3 for the Balrog only |
| `HIDDEN_BY_MAP[4]` | B: {24, 25, 40}. C: {24, 25} | **{24, 25}**. Type 40 (Object41) has no model, and terrain.py already reports it missing |
| Object41's star | B: perhaps a floating white star. C: MU never draws it (no model) | Unverified. Skip it; it is one placement (Decision 6) |
| Object10's warm glow (a missing `break` falls into Stadium's case) | B: flag or drop as a bug. C: keep and mark | Decision 5 |
| Trap radius | A: OM 3. WZ 5 is a generic monster-magic radius | **3** (Decision 4) |
| World5 tile list | C listed six sheets | B's eleven (C read only the first few) |

## Decisions for the user

Each has a recommendation. None blocks step 1.

| # | question | recommendation |
|---|---|---|
| 1 | **Warp rows open per floor, or all seven at once.** Today a map with no quest giver opens every row the first time he stands on it. On the tower that would open LT7's warp from the hall. | **Per floor reached.** Each row opens when he stands on its floor (`travelFloor()` already knows which). About 5 lines in `settleFound`. |
| 2 | **No way back up.** Every stair goes down. From LT7, the only ways out are a scroll, the warp list or death. | **Keep it.** The record is unanimous, and the Town Portal lands in the hall. |
| 3 | **Balrog respawn.** OM: 150 s. Your WebZen rule: 11 s. | **Ask.** At 150 s the two Balrogs are an event. At 11 s, LT7 becomes a Balrog farm. |
| 4 | **Meteorite Trap radius.** OM 3, WZ 5. | **3.** The traps stand in rows across passages, and neither server checks walls, so 5 would reach into the next corridor. |
| 5 | **Object10's 200 warm glows**, from MuMain's missing `break`, at an undefined position. | **Leave them out.** It is a bug, and its position is uninitialised. If the slabs read too cold, add it as ours at bone 1. **Settled 2026-10-01: the sprite is not drawn; each slab carries a faint warm light instead, ours, one per 3 m** (the user remembered 'more lighting thingies', then: 'still to much vissible points, but i like emiters'). |
| 6 | **Object41**: one placement with no model, possibly a lone white star on LT7. | Skip it. |
| 7 | **Baz and Amy** (vault and potions) in the hall. OM 075 has them; 0.75 cannot be proven. | **Keep them**, through `FOLK_VERSION075[4]`. A vault and potions at the top of a seven-floor walk are the hall's point. |
| 8 | **Lava**: an emissive flowing recipe, or MU's literal sliding sheet lit only by TerrainLight. | **Emissive and slow**, at MU's one sheet per 20 s. It is the tower's only warm light. |
| 9 | **Music.** MU plays `lost_tower_a` throughout. | `aTower.wav` as the bed, and no track, per your rule. `lost_tower_b` is free if a fight track is ever wanted. |
| 10 | **Kickable skulls** (CheckSkull, 1 112 pieces, `mBone2.wav`). The town is static instanced chunks. | **Later.** Leave them still in the first build. It needs a small dynamic-placement path of its own. |
| 11 | **Footsteps underground.** We play the grass step on slot 0 everywhere but Devias. MU plays it only in Lorencia and Noria, so the Dungeon's flagstones sound like grass. | Fix it in step 0 and say so in the commit. The Dungeon changes too. |
| 12 | **Spawn scatter.** OM uses fixed tiles; WZ scatters each monster within ±3 tiles on respawn. | Follow whatever the other worlds do today. Nothing new. |

## The build, in order

Part C §6 has the file and line touch-points and a headless check for each step. In short:

0. **Per-world switches become a row.** Give `MapRow` the fields `underground`, `air`, `grassy` and
   `boid`. Make `placeName` a floor-box table and give `ExitGate` a floor name. This removes about
   ten `world == "dungeon"` string tests. Fix `kWhy[6]` and the footsteps. Check: the sim log is
   byte-identical.
1. **Bare land.**
   - Fill the terrain.py tables: hidden {24, 25}, no grass, blend {18:1, 19:4, 20:4, 23:1}.
   - Add `GATE_BOXES_BY_MAP[4]` (and gate 28 and exit 44 for Devias).
   - Run `terrain.py $D/World5 losttower source/world 5`.
   - Add `ground.json` with `lt_` sheets (slot 5 is lava).
   - Insert mu.db gate 42 and cook `--only ground` and `--only tables`.
   - Add the `kMaps` row `{"losttower", 4, {208,75}}`, the banner, the minimap name and the sheet,
     copied from the Dungeon's final numbers.
   - Check: the `.mur` header carries the safe box (203,70,213,81). A zero box there is the gate-22
     bug.
2. **Gates and warps.**
   - Exits 29-41, 42 and Devias's 44. Enters 30-40 and 43. Unseal gate 28.
   - Seven travel rows appended, `kTravels = 13`. Append only, or saved `found` bits shift.
   - Add `testTowerGates` to sim_test.
3. **Underground.** `aTower.wav` as `world_tower`, then index.py and `--only showing`. Bats through
   `AIRS_FROM`. The cook's sunk-placement exemption becomes a set.
4. **Objects, by count:** Object02 872, Object39 777, Object17 418, Object05 399, Object06 357,
   Object07 351, Object40 335, Object04 211, Object10 200, Object09 172, Object24 163, and so on.
   Bring the floor machines Object20/21 forward: there are 20, one per floor, and they are
   landmarks. The red wall slots on Object04/05/19/20/21 need the **chrome underlay**, the one new
   material trick. The Dungeon's glow scroll with an `echo` sheet is the nearest mechanism.
5. **Glows.** `kNoriaGlows` becomes a per-world table. The machines' six spinning sprites go in it.
6. **Fire vents.** 95 hidden Object25 placements become world anchors (`ANCHOR_KINDS_BY_WORLD`).
   A driver rolls a Flame one frame in 64. Raise `kFires` from 8 to about 16, and give each vent a
   static lamp-grid light rather than a transient slot.
7. **Monsters.**
   - mu.db kinds 34-41 and 448 spawn rows from id 386, then a checkpoint.
   - `--only tables` for all five worlds.
   - Eight `kDropRates` rows.
   - Figures one breed at a time: the re-dresses first (Death Gorgon, Cursed Wizard, Poison Shadow
     after Shadow), then Monster27-31, then Mace04.
   - Then the bosses' one-in-five area blow and the Balrog's showing (fire circle, Hellfire, meteor
     rain).
8. **The Meteorite Trap.** Kind 103 with an `area` trigger beside `pressed`, and 148 spots.
   `testTraps` must count map 1's spots only, or it fails the day these rows land. A `Trapped` 103
   goes to `meteor()`.
9. **Last.** Kickable skulls, Baz and Amy, the lava's final look, and a `--budget` run. Price the
   Dungeon first, as the proxy.

## Side findings (outside the tower)

- `src/game/play_requests.cpp:355-356`: `kWhy` has six strings, but `TravelRefusal::Quest` is 6, so a
  quest-locked Dungeon row reads past the array.
- `src/game/play_sound.cpp:97-99`: the grass footstep plays on slot 0 in the Dungeon (Decision 11).
- `src/game/world/world.cpp:268-271`: the Devias beacon gives the Dark Knight the two-thirds level
  rule. MuMain's `CLASS_DARK` is the **Magic Gladiator**, so a 0.75 Dark Knight should see the
  beacon at 50 like everyone else.
- `tests/sim_test.cpp:3832` counts every map's trap spots as the Dungeon's 58.
- `pipeline/index.py:56-66`: `GATE_BOXES_BY_MAP` lacks Lorencia's Dungeon boxes and Devias's gate 28
  and exit 44. All of them are open today by the grid, not by the table.
- `src/sim/gates.h:11-13`: the comment still says only Lorencia's and Noria's pair is here.
- An untracked 0-byte `MU2_BGFX/mu.db`. The store is `source/mu.db`.

---

## Part A: the record

### 1. Map, floors and the one grid

#### 1.1 Map

`OM/Version075/Maps/LostTower.cs:21` `Number = 4`, `:26` `Name = "Lost Tower"`. From the shared
`OM/BaseMapInitializer.cs`:

- **Terrain** `075_Terrain5.att` (`TerrainVersionPrefix "075_"`, file `Terrain{Number+1}`), 65 539
  bytes, 3-byte header then one byte a tile at `y*256 + x` (`OMGL/GameMapTerrain.cs:136-145`).
  Walkable is byte 0 or 1, safe is byte 1. Season 6 reads `Terrain5.att`.
- **Exp multiplier 1**, no map requirements, default drop groups (nothing overridden in LostTower.cs).
- **NPCs**: `CreateNpcSpawns` **is** overridden (`LostTower.cs:45-49`): Potion Girl Amy (253) at
  **207,76** facing SouthWest and Baz The Vault Keeper (240) at **201,76** facing SouthEast, both
  on safe tiles. WZD MonsterSetBase lines 42-43 has Baz at 198,75 dir 2 and Amy at 202,81 dir 3
  (`Monster.txt` names "Baz, Storage Guard", "Amy, Potion Girl").
- **It has a spawn gate**, so its `SafezoneMap` is itself: `BaseMapInitializer.cs:91`
  `SafezoneMapNumber => ExitGates.Any(g => g.IsSpawnGate) ? this map : Lorencia`, and exit gate 42
  is `isSpawnGate: true` (`OM/Version075/Gates.cs:138`). In 0.95d, **Icarus's safe zone is the Lost
  Tower** (`OM/Version095d/Maps/Icarus.cs:44`), and WZ sends Icarus deaths to Devias
  (`user.cpp` death branch `MapNumber == 10 -> 2`) -- both post-0.75.
- **Client**: `WD_4LOSTTOWER` (`MM/World/MapInfra/MapManager.h:13`); MuMain's attribute sanity tile is
  `TerrainWall[75*256 + 193] == 5` (`MM/Render/Terrain/ZzzLodTerrain.cpp:216-217`) -- safe+blocked,
  the hall's west wall. The decoded client grid has 5 there; so does `075_Terrain5.att`.

#### 1.2 The seven floors on one 256x256 grid

Flood fill, 8-neighbour, walkable = byte 0/1 on server grids, `(flags & (NoMove|NoGround)) == 0` on the
client grid (`regions.py`, `floors.py`). The floors are named by the gate chain (§2):

| floor | grid box | 075 tiles | S6 tiles | client tiles | safe (075 / client) | in it |
|---|---|---:|---:|---:|---:|---|
| **LT1** | x 162-247, y 0-138 (NE quarter) | 7 331 | 7 260 | 7 258 | **315 / 294** | arrival 29, door 43 to Devias, hall/spawn 42, stair 30 down |
| **LT2** | x 162-248, y 164-250 (SE) | 5 954 | 5 954 | 5 954 | 0 | exit 31, stair 32 |
| **LT3** | x 80-135, y 165-250 (S middle) | 4 028 | 4 028 | 4 028 | 0 | exit 33, stair 34 |
| **LT4** | x 80-135, y 85-140 (centre) | 2 502 | 2 501 | 2 501 | 0 | exit 35, stair 36 |
| **LT5** | x 80-135, y 5-60 (N middle) | 2 396 | 2 396 | 2 396 | 0 | exit 37, stair 38 |
| **LT6** | x 2-57, y 5-60 (NW) | 2 270 | 2 270 | 2 270 | 0 | exit 39, stair 40 |
| **LT7** | x 2-57, y 85-250 (W) | 5 832 | 5 832 | 5 826 | 0 | exit 41; the Balrogs; (S6+) door 62 to Icarus |
| total of the seven | | 30 313 | 30 241 | 30 233 | | |

(075 counts are the flood regions; its 893 further open tiles are unreachable edge strips -- 357
at x 160-255/y 163-255, 151 + 145 on row 252-254, 112 + 56 + 55 on the east edge, 6 at 234-236,179-180, 11 in slivers --
that no gate or spawn touches.) The client and WZD grids give **exactly seven regions**, so every
open tile belongs to a floor.

Walking distances on the 075 grid (`paths.py`; the client grid differs by 2 steps on one leg):
LT1 arrival -> stair 30 **357 steps** (28 tiles apart as the crow flies: LT1 is a spiral round the hall),
arrival -> hall 184, hall -> stair 30 249; LT2 140; LT3 114; LT4 58; LT5 36; LT6 56;
LT7 arrival -> the Balrogs 185 (-> the S6 Icarus door 225). So a walk from Devias to the Balrog is
about **950 steps** (357+140+114+58+36+56+185) and six stairs.

The safe hall (075 grid, `s` safe, `S` safe+blocked, `#` blocked, `.` open; client is the same but
row 84 is open/blocked rather than safe, and the x 186 strip is blocked):

```
  x 180 ........................... 216
  85 #######.........###..................     the hall opens north at x 187-195
  84 #######sssSSSssSSSSSSSssssSssssSssS##
  80 ######.ssssSSsSSSSSSSSssssssssssssS#.     spawn gate 42 is x 203-213, y 70-81
  76 ######.sssssssSSSSSsssssssssssssssS#.     Baz 201,76  Amy 207,76
  70 ######.sssssssssssssssssssssssssssS##
  69 ###########ss########################     and south by a 2-wide door at x 191-192
  68 ######..###ss#...................####
```

### 2. Gates and warps

#### 2.1 Exit gates (where you come out), `OM/Version075/Gates.cs:138-145` (+ Devias's 44, `:130`)

Directions are OpenMU's (`Gates.cs:90-91`: Gate.txt's number cast straight, 0 = undefined), as
`OMGL/DirectionExtensions.cs:25-33` turns them into a tile, the way `src/sim/gates.h:31-33` stores
them: 1 West (-1,-1), 2 SouthWest (0,-1), 3 South (+1,-1), 5 East (+1,+1).

| gate | map | box | dir | (dx,dy) | role | OM 075 | WZD Gate.txt |
|---:|---|---|---|---|---|---|---|
| **42** | LT | 203,70 - 213,81 | 0 | -- | **spawn gate: the hall.** Warp "LostTower", death, Town Portal, login | `:138` (`isSpawnGate`) | l.72, flag 0, level 80 |
| **29** | LT | 162,2 - 166,3 | 5 East | (+1,+1) | LT1 arrival from Devias gate 28 | `:139` | l.52 |
| 31 | LT | 241,237 - 244,238 | 1 West | (-1,-1) | LT2 arrival; warp "LostTower2" | `:140` | l.55 |
| 33 | LT | 86,166 - 87,168 | 3 South | (+1,-1) | LT3 arrival; warp "LostTower3" | `:141` | l.58 |
| 35 | LT | 87,86 - 88,89 | 3 South | (+1,-1) | LT4 arrival; warp "LostTower4" | `:142` | l.61 |
| 37 | LT | 128,53 - 131,54 | 1 West | (-1,-1) | LT5 arrival; warp "LostTower5" | `:143` | l.64 |
| 39 | LT | 52,53 - 55,54 | 1 West | (-1,-1) | LT6 arrival; warp "LostTower6" | `:144` | l.67 |
| 41 | LT | 8,85 - 9,87 | 1 West | (-1,-1) | LT7 arrival; warp "LostTower7" | `:145` | l.70 |
| **44** | Devias | 2,246 - 3,247 | 2 SouthWest | (0,-1) | back in Devias, under the beacon | `:130` | l.75 |

Every exit tile is byte 0 on both grids (`gates_out.txt`). 0.95d is identical
(`OM/Version095d/Gates.cs:133,152-159`), and so is Season 6 (`OM/VersionSeasonSix/Gates.cs:163,173-180`),
which adds **exit 65** (LT, 17,249-19,249, West, `:181`) out of Icarus.

#### 2.2 Enter gates (boxes you walk into), `OM/Version075/Gates.cs:197-204`

| gate | on | box | to exit | OM level | WZD level | open tiles of the box (075 / client) | line |
|---:|---|---|---|---:|---:|---|---|
| **28** | Devias | 2,248 - 3,249 | 29 (LT1) | **40** | 80 | (Devias grid) | `:197` |
| **43** | LT1 | 162,0 - 166,1 | 44 (Devias) | 15 | 15 | 10 / **5** (client blocks row 0) | `:204` |
| 30 | LT1 | 190,6 - 191,8 | 31 (LT2) | **40** | 80 | 3 / **2** (client blocks 191,8) | `:198` |
| 32 | LT2 | 166,163 - 167,166 | 33 (LT3) | **40** | 80 | 6 / 6 | `:199` |
| 34 | LT3 | 132,245 - 135,246 | 35 (LT4) | **50** | 80 | 2 / 2 | `:200` |
| 36 | LT4 | 132,135 - 135,136 | 37 (LT5) | **50** | 80 | 2 / 2 | `:201` |
| 38 | LT5 | 131,15 - 132,18 | 39 (LT6) | **50** | 80 | 2 / 2 | `:202` |
| 40 | LT6 | 6,5 - 7,8 | 41 (LT7) | **50** | 80 | 3 / 3 | `:203` |

Each stair box sits in a wall niche: only 2-3 of its tiles are open, so `throughGate` must test the
walker's tile against the box, not the box's corner. 0.95d (`OM/Version095d/Gates.cs:206-213`) and
Season 6 (`OM/VersionSeasonSix/Gates.cs:514-521`) are identical; Season 6 adds **enter 62** (LT7,
17,250-19,250, level 160, to Icarus exit 63, `:526`) and Icarus's way back (`:527`). On the client
grid the S6 door is framed by two new posts at x 15 and x 21, rows 248-250 -- the only change in
LT7 (§3).

**No gate leads up.** There is no enter box on LT2-LT7 whose target is a higher floor, in OM 075,
0.95d, S6 or WZD -- unlike the Dungeon, whose every stair has a partner. Leaving LT2-7 is by
scroll, death, warp, or (S6) the Icarus door.

**Levels.** OM: the Devias door and the first two stairs ask **40**, the last four **50**; the door out
asks 15. WZ's C++ does not hard-code a level: `gObjMoveGate` takes the level from Gate.txt and
compares `userlevel < level`, after giving the Magic Gladiator and Dark Lord two thirds
(`WZ user.cpp:27545-27567`). WZD's file (Elion's "Version 99z", 2012) says 80 on every Lost Tower
row, the source of webzen-audit.md's "Lost Tower gate is level 80"; its Dungeon rows (40/50 where OM
says 20) show the same hand raising every level, so **80 is the repack's**. MuMain shows Devias's
beacon (Devias object type 100) only to a hero of level >= 50 (MG/DL/RF 33) (`MM/Engine/Object/ZzzObject.cpp:3446-3455`)
-- a client cue that later servers asked 50 at gate 28, but no 0.75 evidence. **Settled: OM's 40/50.**

MU2_BGFX today: `src/sim/gates.cpp:38-41` carries gate 28 sealed (`target -1`, level 40, "The Lost
Tower"); exits 29-44 and enters 30-43 are absent. **Side finding:** `src/game/world/world.cpp:268-271`
gives the beacon's two-thirds rule to `Kin::DarkKnight`; MuMain's `CLASS_DARK` is the **Magic
Gladiator** (`MM/Core/Globals/_enum.h:3217-3220`: WIZARD, KNIGHT, ELF, DARK), so a 0.75 Dark Knight
should see the beacon at 50 like everyone else.

#### 2.3 Warp list (the Move window)

| index (075/095d) | S6 index | name | zen | level | lands on | OM 075 line |
|---:|---:|---|---:|---:|---|---|
| 8 | 14 | LostTower | **5 000** | **50** | gate 42 (the hall, LT1) | `Gates.cs:51` |
| 9 | 15 | LostTower2 | 5 500 | 50 | gate 31 | `:52` |
| 10 | 16 | LostTower3 | 6 000 | 50 | gate 33 | `:53` |
| 11 | 17 | LostTower4 | 6 500 | 60 | gate 35 | `:54` |
| 12 | 18 | LostTower5 | 7 000 | 60 | gate 37 | `:55` |
| 13 | 19 | LostTower6 | 7 500 | 70 | gate 39 | `:56` |
| 14 | 20 | LostTower7 | 8 000 | 70 | gate 41 | `:57` |

0.75 had no Move window, only the text command (`Gates.cs:40`). 0.95d: same rows
(`OM/Version095d/Gates.cs:49-55`); Season 6: same zen/levels/gates at indices 14-20
(`OM/VersionSeasonSix/Gates.cs:60-66`). WZD's `lang/kor/movereq(Kor).txt` (HermeX "1.02c", 2008)
has the same zen and gates at 90/90/100/110/110/110/120 -- the repack's levels again (its Lorencia
asks 50). The warp to "LostTower" lands in the **safe hall**, not at the Devias door, so the warp
skips LT1's 184-step walk in. MU2_BGFX's M key is free and unlevelled (dungeon-port.md, Decision 6).

### 3. Terrain attributes: 075 against the client

Six attribute grids were read (`regions.py`; the client's with a copy of `pipeline/terrain.py`'s
own `decrypt()` + `attributes()` in `dec.py`, not run in place):

| file | 0 open | 1 safe | 2 stand | 4 blocked | 5 safe+blk | 6 | 8 no ground | 10 | 12 | walkable* |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OM `075_Terrain5.att` | 30 891 | 315 | -- | 18 204 | 111 | 1 | 8 161 | -- | 7 853 | 31 206 |
| OM `Terrain5.att` (S6) | 29 947 | 294 | -- | 19 183 | 98 | -- | 7 898 | 1 | 8 115 | 30 241 |
| WZD `Terrain5.att` | 29 938 | 294 | 1 | 19 191 | 98 | -- | 7 899 | -- | 8 115 | 30 232 |
| MMD `World5/EncTerrain5.att` (client) | 29 938 | 294 | 1 | 19 191 | 98 | -- | 7 899 | -- | 8 115 | 30 233 |
| MMD `World5/Terrain5.att` (3-byte XOR only) | = client, 0 tiles differ | | | | | | | | | |
| MMD `World5/Terrain.att` (plain, older) | 30 672 | 294 | 238 | 18 218 | 98 | 2 | 8 161 | -- | 7 853 | |

\* server rule (byte 0 or 1) for the server files; `(flags & 0x0C) == 0` for the client, which also
lets its one byte-2 tile (121,88) through.

Tile-for-tile differences: **client = WZD = MMD Terrain5.att (0)**; client vs S6 **10**; client vs
075 **1 271**; MMD `Terrain.att` vs 075 **281** (238 of them byte-2 "stand" tiles sprinkled over every
floor where 075 has 0, 21 safe tiles 075 has and it lacks, 13 safe+blocked turned blocked) -- an
older client export that is close to 075's walls and the S6 hall.

**Safe tiles: only LT1.** 075 has 426 tiles with the safe bit (315 walkable) in x 187-214, y 67-84;
S6/client have 392 (294 walkable) in x 187-214, y 70-83. The 075 hall is one row taller (row 84
safe; rows 67-69 safe in its south door, x 191-192). No other floor has a safe tile in any file.

**Where 075 and the client differ inside the floors: 80 tiles** (`floors_out.txt`, `gates_out.txt`);
the other 1 191 are unreachable edge strips (rows 252-255, x 252-255, the 357-tile south-east strip).

| where | tiles | 075 | client | effect |
|---|---:|---|---|---|
| LT1 row 0, x 162-166 | 5 | open | blocked | door 43's back row; row 1 still enters it |
| LT1 191-192, 8 | 2 | open | blocked | one tile off stair 30 (191,6 and 191,7 still enter it) |
| LT1 x 186, y 51-137 (+185, 94-97; 163,111) | 66 | open | blocked | a one-tile corridor along the hall's west wall, closed; LT1 stays one region |
| LT4 94,103 | 1 | open | blocked | nothing |
| LT7 x 15 and x 21, y 248-250 | 6 | open | blocked | the posts of S6's Icarus door |

The 10 tiles between OM's S6 file and the client are the six LT7 posts (open in OM S6, so they are
WZ/client-era, not S6-OpenMU), 161,75-76 (open -> blocked, outside the hall's wall), 63,43 (10 -> 8)
and 121,88 (0 -> 2).
**No gate is closed, no monster spot or trap is on a changed tile** (`spawns.py`: all 448 monster
spots and all 148 traps are byte 0 on both grids; Amy and Baz are on byte 1). Reachability and the
walk lengths of §1.2 agree to 2 steps.

**Settled: the client grid is good for 0.75 as is.** The 66-tile x 186 corridor is the only
real change of shape, it runs beside the hall where no spawn sits, and closing it costs nothing;
the Icarus posts are scenery round a door we will not open. If a pure-075 grid is wanted later, the
80 tiles above are the whole patch.

### 4. Monsters

#### 4.1 Spawns (`OM/Version075/Maps/LostTower.cs:52-652`)

Every row is the single-tile overload `CreateMonsterSpawn(id, def, x, y[, direction])`, ids 100-697,
**596 rows: 448 monsters + 148 Meteorite Traps**, plus the two NPCs (`:47-48`). **No doubled table**
(the Dungeon's slip is not repeated): only three tiles carry more than one row -- Shadow 195,40 (`:69`, `:490`), Death Knight 6,98 (`:54`,
`:292`) and the trap 45,110 (`:535-537`, two facing SouthWest, one NorthEast). So 446 distinct
monster tiles. Every monster spot and every trap is on a byte-0 tile of both the 075
and the client grid, inside its floor; none is on a safe tile (`spawns_out.txt`).

| breed | # | LT1 | LT2 | LT3 | LT4 | LT5 | LT6 | LT7 | total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shadow | 36 | 71 | 1 | | | | | | 72 |
| Poison Shadow | 39 | 27 | 60 | | | | | | 87 |
| Cursed Wizard | 34 | | 21 | 28 | | | | | 49 |
| Death Cow | 41 | | | 32 | 19 | | | | 51 |
| Devil | 37 | | | | 15 | 19 | 12 | 8 | 54 |
| Death Knight | 40 | | | | | 18 | 13 | 41 | 72 |
| Death Gorgon | 35 | | | | | | 11 | 50 | 61 |
| **Balrog** | 38 | | | | | | | **2** | 2 |
| **monsters** | | **98** | **82** | **60** | **34** | **37** | **36** | **101** | **448** |
| Meteorite Trap | 103 | 14 | 7 | 37 | 20 | 0 | 0 | 70 | 148 |
| monsters per 100 walkable tiles | | 1.3 | 1.4 | 1.5 | 1.4 | 1.5 | 1.6 | 1.7 | |

So the floors climb in level: **LT1** Shadows (47) with Poison Shadows (50); **LT2** Poison Shadows
and Cursed Wizards (54); **LT3** Cursed Wizards and Death Cows (57); **LT4** Death Cows and Devils
(60); **LT5** Devils and Death Knights (62); **LT6** Devils, Death Knights, Death Gorgons (64);
**LT7** Death Gorgons, Death Knights, a few Devils and the **two Balrogs at 31,212 and 28,212**,
side by side, 185 steps from the arrival. LT5 and LT6 have no traps; LT7 has 70.

The spots, by floor (x,y; traps with OM's facing). Full lists are in `spawns_out.txt`; the
trap and boss rows are given here because they are what a build must place by hand:

- LT1 traps (14), all SouthEast: 202,45-48; 196,56-58; 196-199,62; 226,115-117.
- LT2 traps (7), all SouthWest: 219,234-237; 226-228,233.
- LT3 traps (37), all SouthEast: rows of 3-4 across corridors at 82-85,175; 82-84,184; 82-84,201;
  93,243-246; 100-103,185; 98-101,225; 110-113,227; 107,242-244; 120-122,208; 132-133,178; 128-130,221.
- LT4 traps (20), all SouthEast: 84-86,120; 93,130-132; 126,104-107; 125-127,115; 123,132-135; 132-134,126.
- LT7 traps (70): 4-6,175; 15,172-174; 14,207-208; 4-6,194; 5-7,237; 26,101-104; 30,99-101;
  26-29,125 (SW); 27,128-130 (SW); 21-23,243; 45,108-110 (NE, and 45,110 twice SW); 38,108-110 (NE);
  33-36,125 (SW); 37,119-120; 34,128-130 (SW); 47,131-132 (SW); 41,132-133 (SW); 36,133-134 (SW);
  46,169-172; 42,245-246; 52-54,114; 52-54,178; 51,198-201 (unmarked = SouthEast).
- Balrog: 31,212 and 28,212 (ids 369 and 499, `:323`, `:453`).
- Death Knights 100-106 (`:54-60`): a knot of seven on LT7's arrival at 5-15, 98-110 -- WZD has
  instead a spot row of five round 20,104 (`MonsterSetBase.txt:120`).

**WZD's MonsterSetBase is the same table, re-shaped** (`wz/MonsterSetBase.txt`, map-4 rows at lines
42-43 NPCs, 113-124 "SPOT", and the "Sueltos"/"TRAMPAS" blocks): its 442 single rows (type 2) and
148 traps (type 0) match OM's 596 rows tile for tile except 9 OM-only monster rows (the seven-DK knot,
one doubled Shadow, Poison Shadow 198,245) and 3 WZD-only Poison Shadows (200,245; 203,50; 236,127);
its 12 extra spot rows add 67 (Shadow 12 at 190-200,96-114; Shadow 6 at 203-228,207-224; Death Cow
6+5+5+5 in LT3; DK 5+4+5; Death Gorgon 4; Devil 5; Poison Shadow 5) for **509**. The trap facings
agree once OM's +1 is undone (WZD 3/1/5 = OM SouthEast/SouthWest/NorthEast, 113/29/6). WZD's single
rows are arrange type 2, which **places each monster at a random open, non-safe tile within +-3 of
its point** (`WZ MonsterSetBase.cpp:133-152` on respawn, `:224-236` on load). The user kept
OpenMU's placement over WebZen's counts (webzen-audit #13), so: **448 + 148, OM's tiles.**

#### 4.2 Breeds (`OM/Version075/Maps/LostTower.cs:655-946`; WZD `Monster.txt`)

OM columns as in dungeon-port.md §1.6 (move/atk/view in tiles, delays in ms, resistances OM's
`n/255` given as n: P poison, I ice, W lightning, F fire). Every breed: `Attribute = 2`,
`NumberOfMaximumItemDrops = 1`, `ObjectKind` monster.

| # | name | lvl | HP | dmg | def | atk rate / def rate | move/atk/view | move/atk ms | respawn OM | skill (OM) | resist P I W F | OM line |
|---:|---|---:|---:|---|---:|---|---|---|---:|---|---|---|
| 36 | Shadow | 47 | 2800 | 148-153 | 78 | 235/67 | 3/1/5 | 400/1400 | 10 s | -- | 3 3 3 5 | `:723-753` |
| 39 | Poison Shadow | 50 | 3500 | 155-160 | 85 | 250/73 | 3/1/5 | 400/1400 | 10 s | **Poison (1)**, multiplier 0.03 | 6 4 4 6 | `:821-853` |
| 34 | Cursed Wizard | 54 | 4000 | 160-170 | 95 | 270/80 | 3/**4**/7 | 400/2000 | 10 s | **Meteorite (2)** | 5 5 7 7 | `:657-688` |
| 41 | Death Cow | 57 | 4500 | 170-180 | 110 | 285/85 | 3/1/7 | 400/1600 | 10 s | -- | 5 5 5 7 | `:887-917` |
| 37 | Devil | 60 | 5000 | 180-195 | 115 | 300/88 | 3/**4**/7 | 400/2000 | 10 s | **Lightning (3)** | 5 5 5 7 | `:755-786` |
| 40 | Death Knight | 62 | 5500 | 190-200 | 120 | 310/91 | 3/1/7 | 400/1800 | 10 s | -- | 6 6 6 7 | `:855-885` |
| 35 | Death Gorgon | 64 | 6000 | 200-210 | 130 | 320/94 | 3/1/7 | 400/2000 | 10 s | `MonsterSkill` (150) | 6 6 6 8 | `:690-721` |
| **38** | **Balrog** | **66** | **9000** | 220-240 | 160 | 330/99 | 3/**2**/7 | 400/1600 | **150 s** | `MonsterSkill` (150) | 10 10 10 15 | `:788-819` |

Trap (`:919-945`): **103 Meteorite Trap**, `ObjectKind = Trap`, `AttackAreaWhenPressedTrapIntelligence`,
level 90, HP 1000, dmg 160-190, atk rate 450, def rate 500, move 0 / **atk 3** / view 1, move 500 ms /
attack 1000 ms, **respawn 3 s**, `Attribute 1`, **no drops**, skill **Flame of Evil (50)**, no resistances.

**WZD Monster.txt agrees on every stat above** (lines 53-60 and 307: level, HP, damage, defence,
attack rate, defence rate, move range, attack range, view range, move/attack speed, the four
resistances -- WZ reads them as `m_Resistance[0..3]`, `MonsterAttr.cpp:233-236`, the order
webzen-audit.md #1 already settled), with these differences:

| | OM 075 | WZD Monster.txt / WZ C++ | settled |
|---|---|---|---|
| respawn, 7 breeds | 10 s | RegTime 5 -> **6 s** (`regen + 1`, webzen-audit #9) | the user's rule: **6 s** (as every cooked breed) |
| respawn, Balrog | **150 s** | RegTime **10** -> 11 s | Decision 3 |
| respawn, trap | 3 s | RegTime 3 | 3-4 s, nothing hangs on it |
| Death Gorgon, Balrog skill | `MonsterSkill` (150) -- **never created** in 075's `SkillsInitializer` (`OM/Version075/SkillsInitializer.cs` has no 150), so in OM 075 they only melee | `A.Type 150`: **one attack in five** (`rand()%5 == 0`) is skill 150-100 = **50, Flame of Evil**, a magic blow on **every player within 5 tiles** (`gObjCalDistance < 6`), and the approach range is atk range + 2 (`WZ gObjMonster.cpp:631-634, 1738-1752, 1849-1925, 2844-2848`) | **WZ's**: OM's null skill is a hole, the client draws exactly this (`MM ZzzCharacter.cpp:1959-1992`, `AT_SKILL_BOSS = 50`, `_enum.h:358`) |
| trap attack | area **3** when pressed, Flame of Evil | `A.Range 0`: fires when a player stands **on** it (`gObjMonsterTrapAct`, `gObjTrapAttackEnemySearch`, `gObjMonster.cpp:3025-3044, 3092-3121`), then `A.Type 50` makes the blow the area magic attack on everyone **within 5** (`:1752`) | both "area when pressed"; radius 3 (OM) or 5 (WZ) -- Decision 5 |
| trap def rate | 500 | 200 (`Success`) | no player hits a trap; moot |
| ItemRate / MoneyRate / MaxItemLevel | -- | 200 / 14 / **3** for all eight but the Death Gorgon (ItemRate 180) and Balrog (**160**); trap 200 / 10 / 0 | `kDropRates` rows `{n, 14, 3}` (§5) |
| Magic defence, MP | -- | 0 | -- |

Experience and Zen per kill (`src/sim/rules.cpp:308-323`, `(L+25)*L/3 * 1.25`, cut when the killer is
more than 10 levels over; Zen = exp + 7): Shadow 1 410 / 1 417, Poison Shadow 1 562, Cursed Wizard
1 777, Death Cow 1 947, Devil 2 125, Death Knight 2 247, Death Gorgon 2 372, **Balrog 2 502**.

**Bosses.** No boss flag in any source. The **Balrog** is the boss by numbers (66, 9 000 HP, resist
10/10/10/15, the only respawn above 10 s in OM, two in the map) and by the client: MuMain draws him at
**scale 1.6** with the **Bill of Balrog +9** (`ZzzCharacter.cpp:13868-13875`), gives him the
gold/silver body-light pass the golden monsters get (`:8716`), and on his skill blow a fire circle,
`SOUND_HELLFIRE` and meteors raining at random within 5 tiles (`:1974-1990`). The **Death Gorgon**'s
skill blow is a ring of 18 fires with `SOUND_METEORITE01` (`:1959-1972`). The Meteorite Trap's
showing is the Balrog's meteor rain (`:1994-2000`).

#### 4.3 What MuMain draws (`MM/Engine/Object/ZzzCharacter.cpp`; body file `Data/Monster/Monster{MODEL+1:02}.bmd`, models `MM/Core/Globals/_enum.h:4176-4180`)

| # | body | scale | arms (item, MU2_BGFX file) | notes | line |
|---:|---|---:|---|---|---|
| 36 Shadow | Monster29 (MODEL_SHADOW) | 1.2 | -- | | `:13883-13888` |
| 39 Poison Shadow | Monster29 | 1.2 | -- | `Level = 1` (glow); skill blow draws thunder joints from its weapon bone | `:13861-13867`, `:2344-2360` |
| 34 Cursed Wizard | **player** | -- | Legendary Staff (5,5 -> `Staff06`) + Legendary Shield (6,14 -> `Shield15`), Legendary helm/armour/pants/gloves/boots (`Male04` set) at **+9** (the weapons' +9 is commented out) | thunder joints + energy particles on attack | `:13908-13932`, `:2315-2329` |
| 41 Death Cow | Monster31 | 1.1 | **Great Hammer (2,3 -> `Mace04`, not cooked)** | | `:13842-13849` |
| 37 Devil | Monster27 | 1.1 | -- | `SOUND_EVIL`; laser joints + fire from both hands | `:13877-13881`, `:2295-2313` |
| 40 Death Knight | Monster30 | 1.3 | Lightning Sword (0,14 -> `Sword15`) | | `:13851-13859` |
| 35 Death Gorgon | Monster12 (the Gorgon) | 1.3 | Crescent Axe x2 (1,8 -> `Axe09`) | BlendMesh 1, `Level = 2` | `:13897-13906` |
| 38 Balrog | Monster28 | **1.6** | Bill of Balrog +9 (3,9 -> `Spear10`) | body light | `:13868-13875` |
| 103 Meteorite Trap | world object 25 of Object5 (`Object26.bmd`), hidden mesh | -- | -- | `CreateCharacter(Key, 25, ...)` | `:14283-14284`; `ZzzObject.cpp:4026-4028` |

Monster12/27/28/29/30/31.bmd are all on disk in `MMD/Monster/`. MU2_BGFX `source/monsters/` has
`Gorgon01` (re-dress for the Death Gorgon) and no Devil/Balrog/Shadow/DeathKnight/DeathCow; the
weapons `Staff06`, `Shield15`, `Sword15`, `Axe09`, `Spear10` and the `Male04` armour set are in
`source/items/`; **`Mace04` (Great Hammer) is missing**.

#### 4.4 The traps are the client's world objects

`MM/Engine/Object/ZzzObject.cpp:5077-5110` `SaveTrapObjects` wrote MonsterSetBase's trap rows out
of the world: for `WD_4LOSTTOWER`, object **type 25 -> monster 103** (`:5092, :5100`). The client hides
type 25 as scenery (`HiddenMesh = -2`, `:4026-4028`) and draws the live trap monster from the same
model. Decoding `MMD/World5/EncTerrain5.obj` (`objs.py`: 5 380 placements, 39 kinds): **148 type-25
objects, the same 148 tiles as OM's trap rows, multiset for multiset** -- no extra trap (the Dungeon's
59th has no Lost Tower twin).

#### 4.5 What MU2_BGFX has

`sqlite3 -readonly source/mu.db` (WAL read; `mu.db` at the repo root and `MU2_BGFX/mu.db` are
0-byte files): `monster_kinds` holds 0-33 (the Dungeon's 5-18 are in now), **none of 34-41 or 103**;
`monster_spawns` maps 0-3 (map 1: 258 rows, the Dungeon's one-per-spot); `gates` rows 17, 22, 27 --
**no 42**; `npc_spawns` has **no map column** (Lorencia's 14 rows; other worlds' folk are in
`tools/cook.py` `FOLK_VERSION075`, `:1772ff`). `src/sim/realm_tuning.h:271-276` already lists the
Poison Shadow (39) as a poisoner (`kPoisoners = {8, 12, 39}`).

Rows to add, in dungeon-port.md §2's form (spawn ids after `select max(id)` = 385 today):

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (34,'Cursed Wizard',54,4000,160,170, 95,3,4,7,400,2000,270,80,10,2),
 (35,'Death Gorgon', 64,6000,200,210,130,3,1,7,400,2000,320,94,10,50),
 (36,'Shadow',       47,2800,148,153, 78,3,1,5,400,1400,235,67,10,NULL),
 (37,'Devil',        60,5000,180,195,115,3,4,7,400,2000,300,88,10,3),
 (38,'Balrog',       66,9000,220,240,160,3,2,7,400,1600,330,99,150,50),
 (39,'Poison Shadow',50,3500,155,160, 85,3,1,5,400,1400,250,73,10,1),
 (40,'Death Knight', 62,5500,190,200,120,3,1,7,400,1800,310,91,10,NULL),
 (41,'Death Cow',    57,4500,170,180,110,3,1,7,400,1600,285,85,10,NULL);
-- attack_skill 50 for 35/38 is WZ's one-in-five Flame of Evil area blow, not a plain skill: it needs code.
-- 103 Meteorite Trap: the Dungeon's trap sprint's body, area-when-pressed, Flame of Evil.
-- kDropRates (realm_tuning.h): {34,14,3},{35,14,3},{36,14,3},{37,14,3},{38,14,3,10},{39,14,3},{40,14,3},{41,14,3}
-- monster_spawns: 448 one-tile rows on map 4; resistances have no column (as the Dungeon's).
```

### 5. Drops

**Nothing Lost-Tower-specific in 0.75.** `LostTower.cs` overrides no `InitializeDropItemGroups`, so the
map carries `GameConfigurationInitializerBase.cs:173-212`'s defaults, the same three groups as the
Dungeon (dungeon-port.md §1.7): jewel 0.001, random item 0.3, money 0.5, one roll a kill,
`NumberOfMaximumItemDrops = 1`; the trap drops nothing. Version075 has no Excellent group.

**What the levels reach.** MU2_BGFX's `Realm::leave` (`src/sim/realm_items.cpp:769-800`) takes WZ's
window -- anything from 15 levels below the monster, inclusive, at `+(L - DL)/3`, a row whose plus
would pass the breed's MaxItemLevel left out -- and with MaxItemLevel **3** for every Lost Tower
breed that is DL in **[L-11, L]**, +0 to +3 (the same twelve levels OM's window gives). So:

| killer | L | DL window | first seen here (DL > 55, the Dungeon's top) |
|---|---:|---|---|
| Shadow | 47 | 36-47 | -- |
| Poison Shadow | 50 | 39-50 | Scroll of **Evil Spirit (50)** at +0 |
| Cursed Wizard | 54 | 43-54 | -- |
| Death Cow | 57 | 46-57 | Heliacal Sword (0,12; 56), Silver Bow (4,5; 56), Legendary Armor (56), Dragon Helm (57), Guardian Armor (57) |
| Devil | 60 | 49-60 | + Lighting Sword (0,14; 59), Legendary Staff (5,5; 59), Dragon Armor (59), Dragon Shield (60), **Scroll of Hellfire (60)** |
| Death Knight | 62 | 51-62 | as the Devil's, Hellfire at +0 |
| Death Gorgon | 64 | 53-64 | + **Bill of Balrog (3,9; 63)** |
| Balrog | 66 | 55-66 | + **Crystal Morning Star (2,4; 66)**, his alone |

(OM 075 lines: `Version075/Items/Weapons.cs:101,103,120,133,140,157`, `Armors.cs:49,54,71,73,84`,
`Scrolls.cs:41-42`.) So in 0.75 the **Scroll of Hellfire is a Lost Tower drop** (Devil and below it,
LT4-LT7), and the Bill of Balrog falls only to the Death Gorgon and the Balrog. Jewels: Bless (25),
Soul (30) and Chaos (12, `MaximumDropLevel 66`) all reach every breed here, Chaos included at the
Balrog's 66 (OM `L <= MaximumDropLevel`; WZ "13-66", webzen-audit). MU2_BGFX needs only the eight
`kDropRates` rows of §4.5 (MoneyRate 14 = Zen on 5 kills in 7 of those that leave no item).

**WZ's map-4 extras, none of them 0.75:**
- **Gold Medal** (14,11 level 6) while the medal event runs, on map 4, 7 or 8 (`WZ gObjMonster.cpp:5233-5245`).
- **Golden Devil** (494) raised at a random tile of map 4 by the Eldorado event's 2009 renewal
  (`WZ EledoradoEvent.cpp:346, 760-775`).
- **Class quests** (0.97+): WZD `lang/main/quest.txt` drops the Scroll of Emperor (14,23) from
  monster levels **45-60** and the Broken Sword / Tear of Elf / Soul of Wizard (14,24-26) from
  **62-76**, at 20 in 1000 (`WZ QuestInfo.cpp:924, 1026` `rand()%1000 < NeedDropRate`). Those level
  bands are the Lost Tower's (and Atlans's) -- the reason later players knew it as the quest
  tower. MU2_BGFX's quests are its own (`src/sim/quests.cpp:195-230` names the Lost Tower "next").

### 6. Rules of the place

- **Entry level**: 40 on foot from Devias (OM), 40/40/50/50/50/50 on the stairs; 50-70 by warp
  (§2). WZ halves nothing for a 0.75 class (the two-thirds rule is the MG's and DL's).
- **The safe hall** (LT1, x 187-214, y 70-83/84): monsters cannot be placed on a safe tile in WZ
  (`MonsterSetBase.cpp:116-120`, `:142-145`) and OM spawns only on walkable non-safe tiles
  (`OMGL/GameMapTerrain.cs:150` `BuildSpawnPoints`). The Meteorite Trap ignores targets on safe tiles in
  OM (`AttackAreaWhenPressedTrapIntelligence.cs`), moot as no trap is near the hall.
- **Death -> the hall.** OM: `OMGL/Player.cs:1559-1562` respawns at the current map's `SafezoneMap` spawn gate, which
  for map 4 is map 4's gate 42 (§1.1). WZ: the death branch's final `else` calls
  `GetMapPos(MapNumber)` (`user.cpp:22360-22363`), and `GetMapPos` keeps maps 0-4 and 7 as they are
  (`MapClass.cpp:308-337`) and picks a random non-block, non-hollow tile of `gRegenRect[4] =
  201,70-213,81` (`MapClass.cpp:197`). **Agreed: a Lost Tower death wakes in the LT1 hall**, not in
  Devias. (Contrast the Dungeon, where WZ's first death branch sends maps 0 and 1 to Lorencia,
  `user.cpp:22057-22062`, as OM does.)
- **Town Portal -> the hall.** WZ `protocol.cpp:17934-17937` `MapNumber == 4 -> gObjMoveGate(42)`;
  OM's scroll goes to the `SafezoneMap`'s spawn gate
  (`OMGL/PlayerActions/ItemConsumeActions/TownPortalScrollConsumeHandlerPlugIn.cs:33-38`), the same hall. The client lets the scroll be
  used here (`MM/World/MapInfra/PortalMgr.cpp:53`).
- **Login**: a character saved in the Lost Tower comes back at gate 42; under the gate's level he is
  sent to Lorencia (Noria for an elf) (`WZ user.cpp:3445-3476`). Single player: land him in the hall.
- **MU2_BGFX:** `Realm::haven()` uses the current map's `safeGate`; map 4 needs `gates` row **42,
  map 4, 203,70,213,81, spawn 1** in mu.db (today only 17, 22, 27), and then death and the scroll stay
  on the map -- no cross-map travel needed, unlike the Dungeon (dungeon-port.md C §2.5).
- **PK**: nothing map-specific in OM or WZ for map 4; single player makes it moot.
- **Exp multiplier 1**, no map requirement items (no Lost Map / pass in 0.75; Kalima's Lost Map is 0.97+
  and comes from Kundun drops, not this map).
- **Traps**: 148 Meteorite Traps, Flame of Evil, 160-190, every 1 s while someone stands on one,
  area 3 (OM) or 5 (WZ), respawn 3 s, cannot be hit (`OMGL/NPC/TrapIntelligenceBase.cs`).
- **The two bosses' area blow** (WZ, §4.2): Death Gorgon and Balrog, one blow in five on everyone
  within 5 tiles. Nothing else on the map is a special mechanic: **no darkness, no weather, no
  lightning strikes** in either server; the "lightning" is the client's art (Devil, Cursed Wizard and
  Poison Shadow attack joints; blue `BITMAP_LIGHTNING+1` stars on objects 19/20/40;
  `MM/Engine/Object/ZzzObject.cpp:2931-2970`).
- **Sound and music**: `SOUND_TOWER01` loops (`MM/Scenes/SceneManager.cpp:873-875`, stopped elsewhere
  `:948-950`) and `MUSIC_LOSTTOWER_A` plays for the whole map (`:1068-1073`).
- **Ambient life**: MuMain loads the Dungeon's stone, bat and rat for the Lost Tower
  (`MM/World/MapInfra/MapManager.cpp:51-58`; boids `MM/Engine/AI/GOBoid.cpp:1307, 1333`), and the
  skull objects 38/39 react to the walker (`CheckSkull`, `ZzzObject.cpp:3999-4001`). Part B's.

### 7. Where the sources disagree, settled or flagged

| point | OM 075 / 095d | WZ C++ (0.97d-S3/S4) | WZD data (repack) | S6 (OM / MuMain) | settled |
|---|---|---|---|---|---|
| gate levels | 40 / 40 / 40 / 50 x4, door out 15 | from Gate.txt | 80 everywhere, door out 15 | as 075; client beacon at 50 | **OM** (repack raised every map) |
| warp levels | 50/50/50/60/60/70/70, 5 000-8 000 zen | from movereq | 90-120, same zen | as 075 | **OM** |
| safe hall size | 426 safe-bit tiles, y 67-84 | -- | 392, y 70-83 | 392 | client's (only the hall's border row) |
| NPCs in the hall | Amy 207,76; Baz 201,76 | -- | Baz 198,75; Amy 202,81 | as 075 | **OM's tiles** (Decision 4) |
| monster count | 448 on 446 tiles | type-2 rows scatter +-3 | 509 (adds 12 spot rows) | as 075 | **OM's 448**, the user's own pick for placement |
| respawn | 10 s; Balrog 150 s; trap 3 s | RegTime + 1 | 5 / Balrog 10 / trap 3 | as 075 | **WZ's** by the user's rule (6 s; Balrog 11 s -- Decision 3) |
| Death Gorgon / Balrog skill | skill 150, not created: melee only | 1 in 5 an area Flame of Evil within 5 | A.Type 150 | client draws the boss blow | **WZ's** |
| trap | area 3 when pressed | on-tile trigger, area within 5 | A.Range 0, A.Type 50 | client: meteor rain within ~5 tiles (`rand()%1024-512` units) | Decision 5 |
| death / scroll | the hall (spawn gate) | the hall (`gRegenRect[4]`, gate 42) | -- | client allows the scroll | **agreed** |
| one-way stairs | yes | yes | yes | yes + Icarus door at LT7 (160) | **agreed**, no Icarus |
| grid | 075 file | -- | = client | OM S6 = client but 10 tiles | **client grid** (§3) |

### 8. Open questions

1. **Whether 0.75 had Baz and Amy in the hall.** OM's Version075 file carries them; no 0.75
   MonsterSetBase is on disk to confirm, and the 0.75 client (if one existed in this tree) is not.
   Recommendation: keep them (Decision 4).
2. **The Balrog's respawn** -- OM's 150 s vs WZ's 11 s (Decision 3). Nothing in this tree is 0.75's
   own Monster.txt.
3. **Trap radius** 3 (OM) or 5 (WZ). OM's `AttackRange = 3` is its only number; WZ's 5 is the
   generic monster-magic radius, not a trap number. Recommendation: 3 (OM) -- the traps stand in
   rows of three or four across a passage (e.g. 82-85,175), so 3 already covers the passage, and
   neither server checks walls, so 5 would reach into neighbouring corridors.
4. **Flame of Evil's damage** on the bosses' fifth blow: WZ sends it through the ordinary monster
   attack (`gObjMonsterMagicAttack` builds a hit list, the damage is the monster's own band). OM's
   skill row says `damage: 120` (`OM/Version075/SkillsInitializer.cs:74`) for the trap's cast. Which
   one a single-player build uses for the Balrog's area blow wants one look in sim_test.
5. **The MG/DL two-thirds rule** applies to no 0.75 class, but MU2_BGFX's beacon gives it to the
   Dark Knight (`src/game/world/world.cpp:271`). Recommendation: drop the DK branch (side finding, §2.2).
6. **A way back up.** None in any source. If the user wants one (LT7 is a long way from the hall),
   it would be ours: e.g. a one-way gate back to the hall from each floor's arrival. Recommendation: no.
7. **0.97d data** is still absent from OM (`Version097d` = Jewels only); WZ's C++ stood in for it,
   with WZD's data trusted only for Monster.txt, which agrees with OM on every stat but respawn,
   ItemRate/MoneyRate/MaxItemLevel and the two bosses' attack type.
8. **`npc_spawns` has no map column** and Lost Tower NPCs need one (or `FOLK_VERSION075[4]` in
   `tools/cook.py`, as Noria's folk are carried).

---

### Build notes for the later passes (record-side only)

- Gates: exits 29, 31, 33, 35, 37, 39, 41, 42 (spawn), 44 (Devias); enters 28 (unseal), 30, 32, 34,
  36, 38, 40, 43. Six same-map gates -- the Dungeon's step 3 (`Realm::throughGate` same-map `What`)
  covers them. mu.db `gates` row 42 (spawn). `kMaps` row `{"losttower", 4, {164, 3}}` or the hall.
- Breeds 34-41 + 103; eight `kDropRates` rows; `kPoisoners` already has 39; `attack_skill` 2 (Cursed
  Wizard) and 3 (Devil) already have showings -- the Lich's meteor and the Thunder Lich's Lightning
  (`src/game/play.cpp:872-877, 939-943`; `attackSkill` is a showing choice, `content/tables.h:39`);
  the bosses' one-in-five area blow is new code.
- Figures: Monster27-31 new, Gorgon01 re-dressed (two Axe09, scale 1.3, Level 2), Cursed Wizard on
  the player rig in Male04 +9 with Staff06 + Shield15, Mace04 (Great Hammer) to bake.
- Traps: 148 type-25 placements already in `EncTerrain5.obj`; hide them as scenery and raise the
  trap bodies in the Dungeon's trap sprint's code.
- The x 186 corridor, row 0 at the door, 191-192,8 and the Icarus posts are the whole client-vs-075
  delta inside the floors (80 tiles), if a 075 patch is ever wanted.

---

## Part B: the data and the look

Research notes, 2026-10-01. Nothing in the repo was edited and no window was run. The Lost Tower is MU server map **4** (`WD_4LOSTTOWER`, `MM/World/MapInfra/MapManager.h:13`). The client ships it as `D/World5` + `D/Object5`.

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- S = `.../scratchpad/losttower/B/`

Scratch outputs in S:
- `losttower/`: `pipeline/terrain.py D/World5 losttower S 5` (height/tiles/attributes/light.png plus losttower.json). `terrain.log` is the run.
- `obj/`: every Object5 .bmd through MuExtract (`export-obj` + `export-rig --actions=all`), with a .log for each. `objinfo.log` and `rigs.log` summarise them.
- `mon/`: Monster27-31 (export-obj + rig, actions 0-6), plus Sword14/15 and Mace04.
- `tex/`: every World5/Object5 sheet decoded. `sheet_textures.png` is the contact sheet (alpha shown as magenta).
- `prev_l1_attr_height_light.png`: layer 1 | attributes | height | light. `minimap_512.png` is mini_map.OZT, which is flipped in y against the grid.
- Scripts: `stats.py`, `comps.py`, `prev.py`, `objinfo.py`. Logs: `stats.log`, `comps.log`.

### The one thing to know first

**The Lost Tower is seven separate floors on one 256² map, with lava and no torches.**
- Every floor is a flat slab at 1.695 m over a black NoGround void.
- Lava pits are sunk 1.7 m to height 0 and are NoMove. TileWater01 is a molten orange sheet, not water. MU slides it like water.
- MuMain gives the map **no fire emitters at all**. TerrainLight carries the whole look: cool grey on floors 1-3, lava red on 4, 5 and 7, bright violet on 6.
- What moves:
  - red light seen through slots and holes in the walls (a "chrome underlay" trick)
  - blue additive lamp posts
  - cyan and orange spinning sprites on the floor machines
  - Flame-spell fire vents
  - 148 meteorite pressure plates
  - 1112 skulls and stones that the hero kicks as he walks

**There is no lightning weather or thunder strike in MuMain's Lost Tower.**
- `aThunder01-03.wav` are loaded (`ZzzOpenData.cpp:4753-4755`), but the only use is commented out, and it is for Icarus (`SceneManager.cpp:891-895`).
- The "lightning" a player remembers is three things:
  - the BITMAP_LIGHTNING+1 sprites on the Object21 machines
  - the Devil's, Poison Shadow's and Cursed Wizard's BITMAP_JOINT_THUNDER/LASER attack beams
  - the Death Knight's Lighting Sword

The client grid **is close to 0.75 but not identical**, which differs from the Dungeon:
- Against OM `Resources/075_Terrain5.att`, it agrees on 98.06% of bytes and 98.52% of walkability.
- All 973 walkability differences are tiles that 0.75 opens and the client closes (0 → 4). Most lie on the map border, but some touch floors:
  - floor 1: a one-tile column at x 185-186, y 51-137, in short runs, plus 5 tiles at (162-166, 0)
  - floor 7: two 3-tile strips at x 15 and x 21, y 248-250, next to the missing Object41 placement
  - floor 4: one tile at (94, 103)
- The 0.75 safe zone is x 187-214, y 67-84 (426 tiles). The client's is x 187-214, y 70-83 (392).
- OM's Season `Terrain5.att` is 99.98% the client's.
- This is the record agent's call. The client grid is the one that matches the walls the client draws.

---

### 1. Raw data inventory

#### 1.1 World5 (`D/World5`, 22 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain5.map | 196610 | yes | 3 planes: layer1, layer2, alpha |
| EncTerrain5.att | 65540 | yes | sanity tile `TerrainWall[75*256+193]==5` (`MM/Render/Terrain/ZzzLodTerrain.cpp:216-217`). The extract has 5 at row 75, col 193, so [y,x] is right |
| EncTerrain5.obj | 161404 | yes | **5380 placements, 39 kinds** (type 0 and type 2 unused) |
| TerrainHeight.OZB | 66620 | yes | header says 1078, read at 1080 |
| TerrainLight.OZJ | 67218 | yes | 256² baked light |
| Tile{Grass01,Grass02,Ground01,Ground02,Ground03,Water01,Wood01,Rock01,Rock02,Rock03,Rock04}.OZJ | 17-121 KB | yes (`MapManager.cpp:1365-1420`) | 11 sheets, **no .OZT, so MU grows no grass** |
| leaf02.OZJ | 4445 | loaded for every world (`MapManager.cpp:1479`) | 16² black. No leaves fall here |
| Terrain.att / Terrain.map / Terrain5.att | 65539 / 196609 / 65539 | no | plain copies |
| mini_map.OZT, Minimap.bmd | | Season UI | 1024² minimap |

terrain.py (`S/terrain.log`, `stats.log`):

```
height     0.00 to 3.19 m; floors 1.695 m (byte 113); 5/50/95 pct 0 / 1.695 / 2.10
tiles      10 slots in use (0,1,2,4,5,6,7,8,9,10; slot 3 TileGround02 unused, 7 only in layer2 at alpha 0)
walkable   46.1%, 0.6% safe (392 tiles, x 187-214 y 70-83, floor 1)
light      mean [89, 81, 83]; floors [94, 84, 86]; max 255 white
objects    5380 placed, 39 kinds; "missing: Object41" (type 40, one placement)
```

**Attributes:**

| value | meaning | tiles |
|---|---|---:|
| 0 | open | 29938 |
| 4 | NoMove | 19191 |
| 12 | NoMove + NoGround | 8115 |
| 8 | NoGround | 7899 |
| 1 / 5 | Safe / Safe + NoMove | 294 / 98 |
| 2 | | 1 |

The void (NoGround, 16014 tiles) is height 0 under flat light 110 grey. The client never draws it: `ZzzLodTerrain.cpp:2311`. It is black.

**Seven floors.** The walkable components are below (`comps.log`). Floor numbers come from OM `Version075/Gates.cs:137-145` (warp targets) and `:51-57` (warp list).

| floor | tiles | box | warp (zen / lvl) |
|---|---:|---|---|
| Lost Tower 1 | 7258 | x 162-247, y 1-138 (safe zone here, gate 42 at 203,70-213,81) | 5000 / 50 |
| Lost Tower 2 | 5954 | x 162-248, y 164-250 (gate 31 at 241,237) | 5500 / 50 |
| Lost Tower 3 | 4028 | x 80-135, y 165-250 (gate 33 at 86,166) | 6000 / 50 |
| Lost Tower 4 | 2501 | x 80-135, y 85-140 (gate 35 at 87,86) | 6500 / 60 |
| Lost Tower 5 | 2396 | x 80-135, y 5-60 (gate 37 at 128,53) | 7000 / 60 |
| Lost Tower 6 | 2270 | x 2-57, y 5-60 (gate 39 at 52,53) | 7500 / 70 |
| Lost Tower 7 | 5826 | x 2-57, y 85-250 (gate 41 at 8,85) | 8000 / 70 |

Gate 29 (162,2-166,3) is floor 1's back door from the Dungeon. No floor joins another on the grid. Travel between floors is by gates, as in the Dungeon.

**Tile sheets.** These were decoded and looked at in `sheet_textures.png`. The names lie:

| slot | name | px | actually | layer1 % | walk % of its tiles | overlay tiles |
|---|---|---:|---|---:|---:|---:|
| 0 | TileGrass01 | 256 | **dark grey cracked stone slabs** (the tower floor) | 64.7 | 48 (the rest is under void/walls) | 9 |
| 1 | TileGrass02 | 256 | dark green-grey ornamental tiling (carved grid) | 3.0 | 82 | 40 |
| 2 | TileGround01 | 128 | black carved ornament band (a frieze) | 0.9 | 2 | 0 |
| 3 | TileGround02 | 256 | **bright blue water**, the only water sheet. **Unused** | 0 | | |
| 4 | TileGround03 | 256 | brown-grey cracked stone | 5.3 | 1 (pit rims) | 1107 |
| 5 | TileWater01 | 256 | **molten lava**, orange-red with dark crust | 10.5 | 0 (5367 NoMove at height 0, 1480 under void) | 0 |
| 6 | TileWood01 | 128 | tan dusty earth | 0.3 | 57 | **18084** (the dust overlay everywhere) |
| 7 | TileRock01 | 128 | pale grey cracked stone | 0 | | 0 |
| 8 | TileRock02 | 256 | **blue-and-gold diamond mosaic** (floor 6, floor 4 centre) | 8.0 | 79 | 400 |
| 9 | TileRock03 | 256 | dark teal radial carved floor (rose-window medallions) | 7.1 | 80 | 66 |
| 10 | TileRock04 | 256 | grey-tan cracked dry rock | 0.3 | 16 | 1787 |

- Pairs (base, overlay; tiles with alpha): (0,6) 11277, (9,6) 2046, (8,6) 1883, (5,6) 1578, (0,10) 1237, (1,6) 955, (8,4) 499, (0,4) 431, (0,8) 392.
- 6304 tiles are fully overlaid and 44027 carry no alpha.
- **Lava** (slot 5) lines floor 1's west side and fills the 3×3 pits of floor 4, the ring round floor 5's centre and floor 7's rows of pits (layer-1 preview, orange). It also fills the void strip at x 140-180, which is never drawn.

**Light map, per floor** (`light.png` is flipped by terrain.py, and it lines up with the floors):

| floor | mean RGB | p10/50/90 luminance | red-dominant tiles |
|---|---|---|---:|
| 1 | 85, 91, 91 | 30 / 84 / 147 | 0% |
| 2 | 97, 98, 98 | 35 / 93 / 164 | 0% |
| 3 | 61, 70, 67 | 13 / 51 / 138 | 0.7% |
| 4 | 93, 68, 72 | 22 / 56 / 166 | 46% |
| 5 | 91, 70, 69 | 20 / 55 / 181 | 42% |
| 6 | 136, 116, 132 | 50 / 123 / 237 | 17% (violet/blue pools) |
| 7 | 111, 73, 75 | 23 / 78 / 146 | 60% |

The baked light is painted with white pools (1792 tiles over 200). The skull and stone placements sit in them (53%/44% of types 38/39 within a tile of a >200 tile), so the pools are lit floor patches, not lamps. The pits glow red into the floor round them.

#### 1.2 Object5 (`D/Object5`, 63 files)

The folder holds:
- 40 `ObjectNN.bmd` (01-40) and `Bug01.bmd`
- 22 sheets: bons, d_003, guard_gold2, light01, re02.tga, re_008, re_02, re_04, re_05, re_06_2, re_06_3, re_07, re_08, t17-t20, taa01-03.tga, worm.tga

**Every sheet a model names is on disk.** `Object41.bmd` (type 40) is not.

Type = model index - 1 (`MapManager.cpp:1121-1128` loads `Object{i+1}` from `Object5`). Sizes are the OBJ bbox in cm, x × y(up) × z. Identification reads the sheet plus the size; nothing was rendered.

| type | model | placed | tris | rig | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 0 | Object01 | **0** | 380 | 1 | re_02 | unplaced (3 × 2.8 m horned panel) |
| 1 | Object02 | **872** | 212 | 1 | re_04 | **the main wall**: 3 m wide, 3.95 m tall, 1.1 m thick (dark carved metal-stone) |
| 2 | Object03 | **0** | 419 | 1 | re_02, re_06_2, re_06_3 | unplaced |
| **3** | Object04 | 211 | 158 | 1 | re_05, **re02.tga**, re_07 | 3 × 2.9 m wall with a **red flowing slot** (mesh 1 = re02.tga, whose alpha cuts a band). Chrome underlay, see §2 |
| **4** | Object05 | **399** | 58 | 1 | re_05, **re02.tga** | 1 × 2.9 m pilaster with the same red slot |
| 5 | Object06 | 357 | 146 | 1 | re_04 | 1.2 × 4.7 m column (re_04, stands 1 m down) |
| 6-8 | Object07-09 | 351 / 65 / 172 | 38-94 | 1-2 | re_05 | low 1.1 m blocks, 2 × 1 / 2 × 2 / 1 × 1 m: plinths, parapets |
| **9** | Object10 | 200 | 24 | 1 | re_08 | 2 × 0.43 × 1 m slab (step/kerb). **Stadium fall-through glow**, see §2 |
| 10-12 | Object11-13 | 14 / 8 / 11 | 650-770 | 4-5 bones, static | **guard_gold2** | black-and-gold **fallen knights**, lying, slumped or sat among the skulls (armour atlas with a face). The sheet differs from the NPC guard_gold2 |
| 13-15 | Object14-16 | 25 / 10 / 7 | 44-54 | 1 | re_04 | small wall bits (1 m) |
| 16, 17 | Object17, 18 | **418**, 51 | 480, 284 | 1 | bons | flat bone litter, 0.8 × 0.3 × 1.2 m |
| **18** | Object19 | 68 | 228 | 3 | re_04 + **light01** | wall 3 × 3.1 m with a light shaft. **BlendMesh 1**: light01 added (warm brown-orange glow streak) |
| **19, 20** | Object20, 21 | 14, 6 | 264 | **22 bones, 30 keys** | t17, t18, **taa01/taa02.tga**, taa03, **t20** | the **floor machines**, 3.8 × 3.1 × 4.7 m. Object21 stands once on each of floors 1-6; Object20 9× on floor 7. Mesh 2 (taa) has two round holes, so you see red chrome through them. Mesh 4 (t20 cyan orb) is added and U-scrolled. Sprites at bones 15, 19, 21 (§2) |
| 21, 22 | Object22, 23 | 86, 123 | 72 | 1 | re_08 | rubble/rock chips 0.9 × 0.3 m |
| **23** | Object24 | 163 | 106 | 4 bones, **12 keys** | re_07 + **re_008** | 0.66 × 1.67 m **blue lamp post**: BlendMesh 1, re_008 (blue light dots) added |
| **24** | Object25 | 95 | 12 | 1 | t19 | **hidden** 40 cm box: **fire vent**, the wizard's Flame spell 1 frame in 64 (§2) |
| **25** | Object26 | 148 | 4 | 1 | t17 | **hidden** 1.25 m square plate: **meteorite trap** (NPC 103 draws this model as its body) |
| 26-33 | Object27-34 | 15/142/15/25/18/21/14/32 | 108-1620 | 1 | **d_003** | brown horn/claw/root forms (d_003 is a dragon-horn sheet). Object28 (142, 1620 tris) scales 0.44-1.9 and sinks to z -166 into the pits. Organic "dragon bone" growths |
| 34-37 | Object35-38 | 57 / 13 / 20 / 21 | 486 | **56 bones, 6 keys** | bons | skeletons, **floor 7 only**. 35-37 stand 1.6-1.75 m (hanging/chained, animated). Object38 lies (1.6 × 2.15 m). Played at CreateObject's default speed |
| **38** | Object39 | **777** | 80 | 1 | bons | 24 cm **skull**: CheckSkull (kicked) |
| **39** | Object40 | **335** | 12 | 1 | re_08 | 18 cm stone chip: CheckSkull (kicked) |
| **40** | Object41 | 1 | — | — | **file missing** | at (18.57, 250.45) z 191, floor 7. RenderObjectVisual gives it a white lightning sprite (§2) |
| — | Bug01 | 0 | 136 | 11 bones, 6+7 keys | worm.tga | **unused here**: MODEL_BUG01 is Stadium's, loaded from Object7 (`MapManager.cpp:114-116`). This copy differs from Object7's |

**Placement counts in order:**
- Object02 872, Object39 777, Object17 418, Object05 399, Object06 357, Object07 351, Object40 335, Object04 211, Object10 200, Object09 172, Object24 163, Object26 148(h), Object28 142, Object23 123, Object25 95(h), Object22 86, Object19 68, Object08 65, Object35 57, Object18 51.
- Everything else is 34 or fewer.

**Per floor:**

| floor | placements |
|---|---:|
| 1 | 920 |
| 2 | 789 |
| 3 | 421 |
| 4 | 486 |
| 5 | 385 |
| 6 | 794 |
| 7 | 1569 |
| void | 16 |

Animated placed kinds:
- Object20/21 (30 keys)
- Object24 (12)
- Object35-38 (6)

Extras loaded for the Lost Tower (`MapManager.cpp:51-58`, shared with the Dungeon):
- DungeonStone01: loaded, never spawned here
- Bat01: the boid
- Rat01: loaded, but no rats run here (GOBoid's fish slot names only the Dungeon)

MU2_BGFX already has all three in `source/world/dungeon`.

---

### 2. MuMain's special cases for WD_4LOSTTOWER

Grep `WD_4LOSTTOWER`: 12 hits, every one below. `LostTower` adds only the move-window string (`UI/NewUI/HUD/NewUIMoveCommandWindow.cpp:31`) and the music enums. `World5` has no literal hit.

**Frame, light, fog**
- **Clear and fog colour: black.**
  - `SetWorldClearColor` has no Lost Tower arm, so the default `SetClearAndFogColor(0,0,0)` applies (`Scenes/SceneManager.cpp:401-402`).
  - The void and the distance are black. There is no sky, no weather and no leaves.
  - No camera arm.
- **Terrain light** is TerrainLight.OZJ times the normal term, as everywhere.
- **No object adds light, except while it burns:**
  - no CreateFire on any Lost Tower type
  - no AddTerrainLight in its MoveObject arm
  - the dynamic light is the fire vents' `(1, 0.4, 0)·L` over 3 tiles while one burns (`MoveHandlers.cpp:1815-1816`), the Death Gorgon's orange, and the Death Knight's fire particle
- **Lava slides.** Every layer-1 slot-5 tile is "water" (`ZzzLodTerrain.cpp:1944-1948`, `2040-2043`):
  - U += WaterMove = `(WorldTime % 20000) * 0.00005`, one sheet every 20 s (`:3594-3596`)
  - plus a wind ripple of `TerrainGrassWind * 0.002` (`:1759-1766`)
  - So the lava creeps one way across the map, as Lorencia's water does.

**Object render arms** (`Engine/Object/ZzzObject.cpp:1035-1066`, RenderObject)
- **Types 3, 4 (Object04/05): red slot underlay.**
  - Pass 1: BodyLight `(1, 0.2, 0.1)`, `StreamMesh = 1`, RenderBody with every mesh's texture overridden by BITMAP_CHROME (`Effect/Chrome01.jpg`, `ZzzOpenData.cpp:5268`).
  - Mesh 1 draws unlit at that flat red (`ZzzBMD.cpp:1365-1369`) with U scrolled by `BlendMeshTexCoordU = -(WorldTime%1000)*0.001`, one sheet a second (MoveObject `:4003-4005`, EnableWave `ZzzBMD.cpp:1353-1357`).
  - Pass 2: the old BodyLight, `StreamMesh = -1`, the normal draw.
  - The result is that re02.tga's transparent band shows **red Chrome01 streaming sideways behind the stone**.
  - The `Light` vector set to `L·(0.4, 0.8, 1.0)`, L 0.6-0.7, is overwritten before use, so it is dead.
- **Types 19, 20 (Object20/21): the same, with `StreamMesh = 2`.**
  - taa01/taa02's two round holes glow streaming red.
  - MoveObject sets `BlendMesh = 4` and the U scroll (`:4006-4009`), so mesh 4 (t20, cyan orb) is **added** and scrolls in both passes.
- **Type 23 (Object24):** plain RenderBody. MoveObject sets `BlendMesh = 1` (`:4013-4017`), so re_008's blue dots are added at BlendMeshLight 1. The commented-out lines show a BITMAP_LIGHT at bone 1 that was dropped.
- **Type 18 (Object19):** `BlendMesh = 1` (`:4010-4012`), so light01 is added.

**RenderObjectVisual** (`ZzzObject.cpp:2931-2989`). Luminosity is rolled 0.70-0.99 per call (`:2781`).
- **Types 19/20:**
  - Object20 (type 19): sprite `BITMAP_MAGIC+1` (`Effect/Magic_Ground2.jpg`), colour `L·(1, 0.2, 0)`, orange-red.
  - Object21 (type 20): `BITMAP_LIGHTNING+1` (`Effect/lightning2.jpg`), `L·(0.4, 0.8, 1.0)`, cyan.
  - Rotation `(int)(WorldTime*0.1) % 360`, so 100°/s, drawn twice at ±rotation.
  - Bones 15 and 19 at size 0.3, bone 21 at size **1.5**.
- **Type 40 (Object41, missing model):**
  - two BITMAP_LIGHTNING+1 at the placement +260 up, size **2.5**, `L·(1,1,1)`, ±rotation 100°/s
  - A commented-out laser-column block sits under it.
  - Whether the client draws it with no model depends on `o->Visible`, which is gated by `:3443`. Unverified: it may be a floating white star with no body on floor 7 (18.6, 250.4).
- **Fall-through (a MuMain bug, live):**
  - The `case WD_4LOSTTOWER:` arm has no `break`, so it falls into `case WD_6STADIUM` (`:2981-2991`).
  - Its `case 9` therefore runs for the Lost Tower's **type 9 (Object10, 200 slabs)**: a `BITMAP_LIGHT` (flare01) sprite, size `L·5` (3.5-5), colour `L·(0.6, 0.3, 0.1)`.
  - Its position is `TransformPosition(BoneTransform[1], Position, p)` with `Position` **uninitialised**, so it is undefined.
  - Read literally, 200 warm glows on floors 1, 2 and 7. Flag for the user: transcribe at the slab's bone-1 origin, or drop as a bug.

**MoveObject** (`ZzzObject.cpp:3996-4029`)
- **Types 38, 39 (skulls, stones): `CheckSkull`** (`Render/Effects/ZzzEffectFireLeave.cpp:94-120`).
  - The trigger is a walking or running hero within **50 units** (half a tile), while the piece is at rest (`Direction[0] < 0.1`).
  - The kick: `Direction = -d·0.4`, spin `HeadAngle = (-dy·4, -dx·4)`, and `SOUND_BONE2` (`mBone2.wav`).
  - Each frame both damp by ×0.6. Position += Direction and Angle += HeadAngle.
  - A kicked skull skids about 0.67·d (geometric sum) and tumbles. There is no gravity, and it stays where it stops. That is 1112 kickable pieces.
- **Type 24 (Object25): `HiddenMesh = -2`; `if (rand_fps_check(64)) CreateEffect(BITMAP_FLAME, pos)`, SubType 0.** This is the wizard's Flame:
  - LifeTime 40 (`ZzzEffect.cpp:1080-1084`)
  - each frame 6 BITMAP_FLAME particles at ±25 (`MoveHandlers.cpp:1802-1809`)
  - particle LifeTime 20, rising `(128-255)·0.15` u/frame, scale 0.64-1.27 (`ZzzEffectParticle.cpp:580-584`)
  - a stone (MODEL_STONE1/2) one frame in 8
  - terrain light `L·(1, 0.4, 0)` over 3 tiles
  - the Flame01 decal on the ground, 2×2, at 0.8-1.1 (`ZzzEffect.cpp:10015-10021`)
  - The damage branch needs `Owner == Hero`, so a vent hurts nobody.
  - At 25 fps, 1/64 a frame is about one burst every 2.6 s per vent, over 95 vents.
  - MU2_BGFX's `src/game/fx/flame.h` is this effect already.
- **Type 25 (Object26): `HiddenMesh = -2`.**
  - This is `SaveTrapObjects` type 103 (`:5091-5100`).
  - The live trap is `MONSTER_METEORITE_TRAP` 103, `CreateCharacter(Key, 25, ...)` (`ZzzCharacter.cpp:14283-14285`): a pressure plate drawn as Object26.
  - Its attack with AT_SKILL_BOSS throws `MODEL_FIRE` at ±512 around the plate plus `SOUND_METEORITE01` (`eMeteorite.wav`) (`ZzzCharacter.cpp:1994-2000`).
  - In OM it is `AttackAreaWhenPressedTrapIntelligence` (`Version075/Maps/LostTower.cs:922-931`), with **148 spawns** (= the 148 placements).
  - MU2_BGFX's `src/game/fx/meteor.h` is the rock.
- No CreateObject arm (`ZzzObject.cpp:4640-4790` has none), so there are no operates, no seats and no lean boxes.

**Boids** (`Engine/AI/GOBoid.cpp:1304-1334`)
- **Bats**: MODEL_BAT01, as the Dungeon: Velocity 1, cap 5, aBat 1/256.
- No rats: the fish slot is Dungeon-only.
- No Bug01.

**Ambient and music** (`Scenes/SceneManager.cpp`)
- `SOUND_TOWER01` (`aTower.wav`, 837 KB) loops always (`:873-875`) and is stopped elsewhere (`:948-951`).
- No wind: `:943-946` stops SOUND_WIND01 off the four wind worlds.
- **`MUSIC_LOSTTOWER_A` (`lost_tower_a.mp3`) plays always** on the map (`:1068-1073`).
- `lost_tower_b.mp3` has an enum (`Core/Globals/_enum.h:180`) and **no player anywhere**. It is unused in this client.

**Other**
- The town portal works (`World/MapInfra/PortalMgr.cpp:53`).
- The sanity tile is above.

---

### 3. The pipeline route, and what changes for World5

The chain is the Dungeon's (`docs/dungeon-port.md` Part B §3): import, then build, sync and cook. From MU2_BGFX/, with `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. tables first, pipeline/terrain.py (keys are map number - 1 = 4):
#    HIDDEN_BY_MAP[4]     = {24, 25, 40}   # fire vent, meteorite plate (the trap NPC draws it), Object41 (no .bmd)
#    OPERABLE_BY_MAP[4]   = set()          # no CreateOperate arm; index.py:43-46 likewise
#    GRASS_BY_MAP[4]      = []             # no TileGrass .OZT; TileGrass01 is the stone floor
#    BLEND_MESH_BY_MAP[4] = {18: 1, 19: 4, 20: 4, 23: 1}   # MoveObject :4006-4017
#    WATER_FLOW_BY_MAP[4]: none -- the lava slides one way at MU's pace (see below)
python3 pipeline/terrain.py $D/World5 losttower source/world 5
#    expect: 0.00-3.19 m, 10 slots, 46.1% walkable, 0.6% safe, 5380 placed / 39 kinds, missing Object41
for n in TileGrass01 TileGrass02 TileGround01 TileGround03 TileWater01 TileWood01 TileRock01 TileRock02 TileRock03 TileRock04; do
  python3 pipeline/decode_texture.py $D/World5/$n.OZJ source/textures/lt_$(echo $n | tr A-Z a-z).png; done
#    source/world/losttower/ground.json on the Dungeon's shape, sheets -> lt_*
./tools/content.sh --world losttower && ./tools/sync.sh --world losttower --only-world && ./tools/sync.sh
./tools/cook.py --world losttower --only ground && ./tools/cook.py --world losttower --only tables
# objects: export per kind into source/world/losttower/, sheets lt_-prefixed
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-obj $D/Object5/Object02.bmd source/world/losttower/Object02.obj
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-rig $D/Object5/Object20.bmd source/world/losttower/Object20.rig.json --actions=all  # 20, 21, 24, 35-38 only
./tools/asset.sh losttower/Object02 --build-only && python3 pipeline/index.py source workshop
./tools/cook.py --world losttower --only textures / meshes / placements
```

**Sheet prefix: `lt_`.**
- Nothing in `source/textures` starts with `lt_` today.
- Collisions without one:
  - `bons` (Object5's is byte-identical to Object2's, Monster's and Skill's; `bons.png` and `dn_bons.png` both exist, so reuse is possible)
  - `guard_gold2` (differs from the NPC copy `npc_guard_gold2.png`)
  - all 10 Tile* names (Lorencia's and the Dungeon's)
  - `TileGround02` also differs from World2's

**ground.json `sheet_materials` (proposed):**

| slot | sheet | material |
|---|---|---|
| 0 | TileGrass01 | flagstone |
| 1 | TileGrass02 | flagstone (ornamental) |
| 2 | TileGround01 | rock |
| 4 | TileGround03 | rock |
| 5 | TileWater01 | **a lava recipe, not water**. Emissive, flowing at MU's one sheet per 20 s. The engine's `water` recipe would put a wet sheen on it. Decision for the user and the pipeline engineer |
| 6 | TileWood01 | sand |
| 7 | TileRock01 | rock |
| 8 | TileRock02 | flagstone (mosaic) |
| 9 | TileRock03 | flagstone (carved) |
| 10 | TileRock04 | rock |

What else differs from the Dungeon:
- **Seven floors, not three.**
  - `maps.cpp:25` needs a row: map 4, arrival in the safe zone, e.g. (208, 76), the middle of gate 42.
  - `maps.cpp:67-75` `placeName` needs a floor table (the boxes in §1.1).
  - `ui/arrival.cpp:61` needs a name row.
- **`underground_` / `dungeonAir_`** (`world.cpp:95`, `play_open.cpp:32-33`) are keyed `== "dungeon"`. The Lost Tower wants the same no-leaves and no-wind behaviour, but its own loop, `aTower.wav`. Generalise the flag (e.g. a set {dungeon, losttower}) rather than copy it.
- **Boids**:
  - `boids.cpp:60,77` and `cook.py:202` `AIRS` need `"losttower": "Bat01"`, plus `AIRS_FROM["losttower"] = "dungeon"`, since MuMain loads it from Object2.
  - No `CRAWLS` entry.
- **`TRAP_MODELS`** (`cook.py:209`): the Lost Tower needs Object26 as the meteorite trap's figure. `trap_show.cpp:70` returns early off the Dungeon.
- **GROUNDED_TYPES** (`cook.py:1130`): none. Every Lost Tower placement stands where it is stored.
- **The sunk-crown test** (`cook.py:1463`) skips only the Dungeon. Object35-38 sway and some stand partly below z 0, so add the Lost Tower to that skip.
- **New engine work:**
  1. **Chrome underlay** for types 3, 4, 19 and 20. Draw the alpha-cut mesh's holes as an additive red (1, 0.2, 0.1) Chrome01 glow sliding one sheet a second. The Dungeon's `glow.scrolls_per_second` with an `echo` sheet is the nearest existing mechanism (`source/world/dungeon/Object23.json`). The mask is the inverse alpha of re02/taa01/taa02.
  2. **Ornament table** for the Lost Tower (`game/world/ornaments.cpp`, like `kNoriaGlows`): Object20/21 bones 15, 19 and 21, Object41's star, and, if kept, the Object10 fall-through glow.
  3. **Kickable skulls**: CheckSkull on 1112 placements. These must become instances that move after open, which nothing in the town cook does today.
  4. **Fire vents**: reuse `fx/flame` on 95 hidden markers, 1/64 a frame at 25 fps.
  5. **Lava slide**: slot 5 slides one way at `0.00005/ms`. If the engine's water flow is used, `water_flow` in the sheet must stay MU's 1 (not the Dungeon's 0.35), with no WATER_FLOW_BY_MAP sources.
- **Recipe priority by placement count:**
  1. Object02 (872, the wall)
  2. Object39 (777 skulls; tiny, 80 tris)
  3. Object17 (418 bone litter)
  4. Object05 (399; needs the underlay)
  5. Object06 (357)
  6. Object07 (351)
  7. Object40 (335 chips)
  8. Object04 (211; underlay)
  9. Object10 (200)
  10. Object09 (172)
  11. Object24 (163; blue additive)
  12. Object28 (142; 1620 tris)
  13. Object23 (123)
  14. Object22 (86)
  15. Object19 (68; additive)
  16. Object08 (65)
  17. Object35 (57; rigged)
  18. Object18 (51)

  Then the 4-34-count tail, and the machines Object20/21 (20 total, but one per floor, so they are landmarks and should come earlier). Object26 is built as the trap figure. Object25 needs no mesh. Object01 and 03 are unplaced: skip them.

---

### 4. Monster assets

OM `Version075/Maps/LostTower.cs`:
- Breeds are at `:660-920`. The trap is at `:922-931`.
- Spawn counts (grep of NpcDictionary):

| # | breed | spawns |
|---:|---|---:|
| 39 | Poison Shadow | 87 |
| 40 | Death Knight | 72 |
| 36 | Shadow | 72 |
| 35 | Death Gorgon | 61 |
| 37 | Devil | 54 |
| 41 | Death Cow | 51 |
| 34 | Cursed Wizard | 49 |
| 38 | Balrog | 2 |
| 103 | Meteorite Trap | 148 |
| 240 | Baz the Vault Keeper | 1 |
| 253 | Potion Girl Amy | 1 |

The model file is `Monster{MONSTER_MODEL+1}.bmd`. MuExtract output is in `S/mon/`.

| # | breed | CreateMonster (`ZzzCharacter.cpp`) | file | scale | arms | MU2_BGFX status |
|---:|---|---|---|---:|---|---|
| 34 | Cursed Wizard | `:13908-13931` | **player rig** (MODEL_PLAYER) | SetCharacterScale | Legendary set Helm/Armor/Pants/Gloves/Boots = `*Male04` **+9** + Legendary Staff = Staff06 + Legendary Shield = Shield15; PK murderer colour | **re-dress, all parts cooked** (HelmMale04-BootMale04, Staff06, Shield15 in source/items). Needs a figure row like SkeletonWarrior's. Attack: 4× JOINT_THUNDER + ENERGY from both hands (`:2315-2328`) |
| 35 | Death Gorgon | `:13897-13906` | Monster12 | 1.3 | **two Crescent Axes = Axe09** (now in source/items) | **mesh cooked** (`source/monsters/Gorgon01`). Variant row: Level 2 gives 10 BITMAP_FIRE a frame on random bones + `L·(1,0.2,0)` terrain light, 2 tiles (`:6061-6076`). Skill: 18 MODEL_FIRE ring + eMeteorite (`:1959-1972`) |
| 36 | Shadow | `:13883-13888` | **Monster29** | 1.2 | — | **missing**. 414 tris, 54 bones; shadow_c.jpg (412) + shadow_b.jpg (2). Walk 0.3 (`ZzzOpenData.cpp:2630`) |
| 39 | Poison Shadow | `:13861-13867` | Monster29 | 1.2 | — | **re-dress of Shadow**, Level 1 (green light branch `:11310-11320`). Attack: JOINT_THUNDER ×2 + ENERGY from (0,-130,0) off link bone (`:2344-2360`) |
| 37 | Devil | `:13877-13882` | **Monster27** | 1.1 | — | **missing**. 662 tris, 55 bones; satan_a.tga (alpha). U scroll `-(WT%10000)·0.0001` (`:6016-6018`). Attack: JOINT_LASER+1 ×4 + fire + sEvil (`:2295-2313`) |
| 38 | Balrog | `:13868-13876` | **Monster28** | 1.6 | Bill of Balrog = Spear10 (cooked) **Level 9** | **missing**. 1450 tris, 80 bones; ho9_11.jpg + ho9_2.jpg. `StreamMesh = 1` (ho9_2 unlit, V scroll `-(WT%1000)·0.001`) (`ZzzOpenData.cpp:3597-3606`, `ZzzCharacter.cpp:6013-6015`). Walk sound mBone2 (`:762-763`). Skill: MODEL_CIRCLE + CIRCLE_LIGHT + sHellFire (`:1974-1985`). Two spawns: the floor-7 boss |
| 40 | Death Knight | `:13851-13860` | **Monster30** | 1.3 | **Lighting Sword = Sword15** (cooked) | **missing**. 1328 tris, 49 bones; as.jpg, Polearms03.jpg, as_1.jpg, shadow_b. `BlendMesh = 3` (as_1 added, V scroll 1/s) + a BITMAP_FIRE at bone 2 every other frame (`:6003-6011`) |
| 41 | Death Cow | `:13842-13850` | **Monster31** | 1.1 | **Great Hammer = Mace04: no recipe** (MuExtract: 54 tris, maul03.jpg) | **missing**. 1109 tris, 45 bones; wwo.jpg. RenderEye bones 22/23 (`:11209-11211`). Dies into MODEL_BONE1 + 10 BONE2 + mBone2 (`:1480-1486`) |
| 103 | Meteorite Trap | `:14283-14285` | Object5 Object26 | — | — | world model, not yet built |

- Link bones (`:12004-12049`): Devil 16/25, Death Knight 30/39, Balrog 17/28, Death Cow 42/33 (Bull's), Gorgon 30/39.
- Monster sounds (`ZzzOpenData.cpp:3380-3615`):
  - Death Cow = mBull*
  - Death Knight = mDarkKnight*
  - Devil = mYeti1, mSatanAttack1, mYetiDie
  - Balrog = mBalrog1/2, mWizardAttack2, mGorgonAttack2, mBalrogDie
  - Shadow = mShadow1/2, mShadowAttack1/2, mShadowDie
  - Death Gorgon = mGorgon*
- **To import:** Monster27, 28, 29, 30 and 31 (5 models); Mace04 (1 weapon); figure rows for Cursed Wizard (player re-dress), Poison Shadow (Shadow re-dress) and Death Gorgon (Gorgon variant).
- Already in hand: Gorgon01, Axe09, Spear10, Sword15, Staff06, Shield15, the Legendary set.
- Needs `figures_losttower.json` beside `assets/cooked/figures/figures_dungeon.json`.

---

### 5. Sounds and music

| file | in MU2_BGFX `source/sounds`? | where MuMain plays it |
|---|---|---|
| Sound/**aTower.wav** (837 KB) | **no** | `SOUND_TOWER01`, ambient loop always (`ZzzOpenData.cpp:4735`; `SceneManager.cpp:873-875`, stop `:948-951`) |
| Sound/aBat.wav | yes (abat.wav) | bats, 1/256 a frame (`ZzzOpenData.cpp:4746`; GOBoid) |
| Sound/mBone2.wav | yes | CheckSkull kick (`ZzzEffectFireLeave.cpp:110`); Balrog's walk; Death Cow's death |
| Sound/eMeteorite.wav | yes | meteorite trap, Death Gorgon skill (`ZzzCharacter.cpp:1999, 1969`) |
| Sound/sFlame.wav | yes | not played by the vents. The Flame effect has no sound of its own here (CreateEffect has no PlayBuffer at `:4023`) |
| Sound/sHellFire.wav | **no** | Balrog skill (`ZzzCharacter.cpp:1981`) |
| Sound/sEvil.wav | **no** | Devil attack (`:2297`) |
| mBalrog1/2/Die, mShadow1/2/Attack1/2/Die, mSatanAttack1 | **no** (9 files) | breed sets above |
| mYeti1, mYetiDie, mBull*, mDarkKnight*, mGorgon*, mWizardAttack2 | yes | breed sets above |
| aThunder01-03.wav | no | loaded, **never played on any map** (Icarus's line is commented out) |
| Music/**lost_tower_a.mp3** (4.0 MB) | no | `PlayMp3(MUSIC_LOSTTOWER_A)` always on the map (`SceneManager.cpp:1068-1073`) |
| Music/lost_tower_b.mp3 (3.1 MB) | no | **unused** (`_enum.h:180` only) |

New sounds go through `index.py` and then `tools/cook.py --only showing` (house rule).

---

### 6. A look direction

**What MuMain draws:**
- black clear and fog
- floors lit only by their painted TerrainLight
- no torches anywhere

**The character comes from the floors' paint:**
- Floors 1-3: cool neutral grey, dim. Floor 3 is the darkest, with a median luminance of 51/255.
- Floors 4, 5 and 7: lava-red. 42-60% of their tiles are red-dominant, and the lava itself slides and glows.
- Floor 6: bright violet-pink over the blue-gold mosaic, the brightest floor (median 123).

**What moves and glows:**
- streaming red light through wall slots (610 placements)
- blue additive lamp posts (163)
- cyan and orange spinning stars on the floor machines
- Flame-spell vents bursting from the floor
- kicked skulls rattling

The tower should feel like **a cold stone ruin heated from below**: grey air, blue lamps, red seams in the walls, and lava pits as the warm light source. That is the opposite of the Dungeon's torch-led look.

**House rules applied:**
- **Music is rare and in fights.** MuMain's always-on `lost_tower_a` becomes at most a fight track, WoW-rare. `aTower.wav` is the bed, and the wind stays off. `lost_tower_b` is a free second fight track if the user wants one. It is unused in MuMain, so it would be ours.
- **Night looks are not too dark or too foggy.** The Dungeon's sheet (`sheets/worlds/dungeon.json`) was pushed brighter twice by the user ("to dark": exposure 1.5 → 1.9, fill 1.1 → 1.6), and its mist only rose to 0.009 on request. Start the Lost Tower from the Dungeon's final numbers, not its first:
  - exposure ~1.9, ambient ~1.6, tonemap 1 (Hill ACES), lamp_shadow 0.85
  - Floor 3's dark median (51) and the 4/5/7 red will read darker than the Dungeon's 108-140 mean. Expect the user to ask for more light there.
- **What to change against the Dungeon:**
  - lamp/flame strength matters less, since there are no torches
  - **lava emissive** carries the warm light. It must glow without blooming out: bloom threshold ~0.85 as the Dungeon
  - the split should run warm-from-below (lava amber/red highlights) against cold grey-teal shade
  - the key is a faint cool overhead, as in the Dungeon, so bodies still cast MU's short shadow
  - the mist is a grey stone dust at the Dungeon's ~0.007-0.009, perhaps tinted faintly warm near the lava floors
  - not black: the haze was rejected as "nothing but dark" in the Dungeon
- **Per-floor variation** would need per-floor sheets. The engine's look is per world today (`sheets/worlds/<world>.json`), so the TerrainLight must carry the floor-to-floor difference, as it does in MU.

**Decisions for the user:**
1. Lava recipe: emissive flowing lava, or MU's literal sliding sheet lit only by TerrainLight.
2. The Object10 fall-through glows (200 warm sprites from a MuMain bug).
3. Object41's lone star on floor 7 with no model.
4. Client grid or 0.75 grid: 973 tiles, a few touching floors 1, 4 and 7, and a 34-tile larger safe zone.
5. Whether `lost_tower_a`/`_b` play in fights at all.

---

## Part C: the engine side

Written 2026-10-01, read-only (no repo file edited, no cook, no window). Paths relative to
`MU2_BGFX/` unless marked. OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`,
MM = `LEGACY/reference/MuMain/src/source/`, D = `LEGACY/reference/MuMain/src/bin/Data`.
Line numbers are the tree at HEAD e6864d1b (plus whatever was uncommitted on 2026-10-01).

### 0. The record the engine has to carry (OpenMU Version075)

- Map **4**, "Lost Tower" (`OM/Version075/Maps/LostTower.cs:21-26`). MuMain's world folder is
  `D/World5` + `D/Object5` (terrain.py map_number 5 -> index number 4). Raw data present:
  World5 has `EncTerrain5.att/.map/.obj`, `TerrainHeight.OZB`, `TerrainLight.OZJ`, six tiles
  (TileGrass01/02, TileGround01-03, TileRock01); Object5 has Object01-40 + Bug01.bmd.
- **The Lost Tower HAS a spawn gate**: exit 42, `CreateExitGate(maps[4], 203, 70, 213, 81, 0,
  true)` (`OM/Version075/Gates.cs:138`). So `SafezoneMapNumber` (`OM/BaseMapInitializer.cs:91`)
  is 4 itself: death and a Town Portal in the tower land in the tower's own safe box, not in
  Devias. **This is the one big difference from the Dungeon**, and it means the `safe_map`
  machinery is not needed for this map (§3.4).
- Exit gates on map 4 (`Gates.cs:138-145`, OpenMU Direction): 42 spawn (203,70-213,81),
  29 (162,2-166,3, dir 5 = East), 31 (241,237-244,238, 1 W), 33 (86,166-87,168, 3 S),
  35 (87,86-88,89, 3 S), 37 (128,53-131,54, 1 W), 39 (52,53-55,54, 1 W), 41 (8,85-9,87, 1 W).
  Devias's exit **44** (2,246-3,247, dir 2) is where the way out lands (`Gates.cs:132`).
- Enter gates (`Gates.cs:197-204`): Devias **28** 2,248-3,249 lvl **40** -> 29;
  Lost Tower 30 190,6-191,8 lvl 40 -> 31; 32 166,163-167,166 lvl 40 -> 33;
  34 132,245-135,246 lvl 50 -> 35; 36 132,135-135,136 lvl 50 -> 37; 38 131,15-132,18 lvl 50 -> 39;
  40 6,5-7,8 lvl 50 -> 41; **43** 162,0-166,1 lvl **15** -> 44 (back to Devias).
  So the chain is one-way downward: 30 (floor 1->2), 32 (2->3), 34 (3->4), 36 (4->5),
  38 (5->6), 40 (6->7). **There is no "up" gate** -- unlike the Dungeon's 7/11/15, the tower
  has no stairs back; the way out is death, a Town Portal or the warp list.
- Warp list (`Gates.cs:51-57`): LostTower 5000 zen lvl 50 -> **gate 42** (the safe box);
  LostTower2 5500 lvl 50 -> 31; LostTower3 6000 lvl 50 -> 33; LostTower4 6500 lvl 60 -> 35;
  LostTower5 7000 lvl 60 -> 37; LostTower6 7500 lvl 70 -> 39; LostTower7 8000 lvl 70 -> 41.
  Seven warp targets on ONE map.
- Monsters (`LostTower.cs:655-940`): 34 Cursed Wizard (lvl 54, Meteorite, range 4), 35 Death
  Gorgon (64), 36 Shadow (47), 37 Devil (60, Lightning, range 4), 38 Balrog (66, respawn 150 s,
  2 spots: 31,212 and 28,212), 39 Poison Shadow (50, Poison), 40 Death Knight (62), 41 Death
  Cow (57), and trap **103 Meteorite Trap** (lvl 90, `AttackAreaWhenPressedTrapIntelligence`,
  FlameofEvil skill, range 3, 148 rows). Spawn counts in the file: 34x49, 35x61, 36x72, 37x54,
  38x2, 39x87, 40x72, 41x51 = **448 monsters + 148 traps**, all one-tile rows.
- NPCs (`LostTower.cs:45-49`): 253 at 207,76 and 240 at 201,76 -- both inside the safe box
  (OpenMU's later-season Potion Girl and Safety Guardian; whether 0.75 had them is pass A's).

#### 0.1 The grid, from a scratch extraction

`python3 pipeline/terrain.py D/World5 losttower <scratchpad>/losttower/extract 5` (into the
scratchpad, nothing in the repo): height 0.00-3.19 m; **46.1% walkable, 0.6% safe**; words
{0: 29938, 4: 19191, 12: 8115, 8: 7899, 1: 294, 5: 98, 2: 1}; **no TW_CAMERA_UP (0x80) tile**
(as in the Dungeon); light mean (89,81,83), darker and warmer than the Dungeon's (108,108,140);
**5380 placements over 39 kinds** (Dungeon 4488/61, Lorencia 2870) -- 872 Object02, 777
Object39, 418 Object17, 399 Object05, 357 Object06, 351 Object07, 335 Object40; **Object41 has
no .bmd** (one placement). Tile slots: **0 TileGrass01 64.7%** (the floor again, as in the
Dungeon), 5 TileWater01 10.5%, 8 TileRock02 8.0%, 9 TileRock03 7.1%, 4 TileGround03 5.3%,
1 TileGrass02 3.0%. World5 ships no TileGrass*.OZT, so like the Dungeon it must grow nothing.

**The seven floors are seven disconnected regions of one grid** (8-connected flood fill of
walkable tiles; no small islands at all):

| floor | region (cols x rows) | open tiles | lands here | its gate on | monsters (OpenMU rows) |
|---|---|---:|---|---|---|
| 1 | 162-247 x 1-138 | 7 258 | exit 29 (from Devias), **spawn 42 (safe box)** | enter 30 (down), enter 43 (out to Devias) | Shadow 71, Poison Shadow 27; 14 traps |
| 2 | 162-248 x 164-250 | 5 954 | exit 31 | enter 32 | Poison Shadow 60, Cursed Wizard 21, Shadow 1; 7 traps |
| 3 | 80-135 x 165-250 | 4 028 | exit 33 | enter 34 | Death Cow 32, Cursed Wizard 28; 37 traps |
| 4 | 80-135 x 85-140 | 2 501 | exit 35 | enter 36 | Death Cow 19, Devil 15; 20 traps |
| 5 | 80-135 x 5-60 | 2 396 | exit 37 | enter 38 | Devil 19, Death Knight 18 |
| 6 | 2-57 x 5-60 | 2 270 | exit 39 | enter 40 | Death Knight 13, Devil 12, Death Gorgon 11 |
| 7 | 2-57 x 85-250 | 5 826 | exit 41 | none | Death Gorgon 50, Death Knight 41, Devil 8, **Balrog 2**; 70 traps |

Every exit box lies wholly on open tiles of its floor (word 0, the spawn box word 1). Every
enter box is part open, part word 4 (blocked) -- the step test only ever sees the open part.
The safe flag spans cols 187-214, rows 70-83 (392 tiles), all on floor 1. The floors' bounding
boxes do not overlap, so a floor can be named by box (unlike the Dungeon's, where 2 and 3 share
one -- `travel` flood-fills for that reason, §3.3).

---

### 1. How a world is registered and loaded today

The Dungeon walked this path on 2026-09-30/10-01 and it held; nothing below is new machinery,
only rows. **There is still no single registry**: a world is a folder name found by convention,
plus a handful of tables in code. The name to use: `losttower` (one word, as terrain.py's
output folder; every lookup is by this string).

#### 1.1 The source side (pipeline)

| step | where | Lost Tower needs |
|---|---|---|
| extraction | `pipeline/terrain.py main` (`:433-440`, args `<World> <name> <out> <map_number>`) | `python3 pipeline/terrain.py D/World5 losttower source/world 5` |
| per-map tables, keyed **map_number - 1** | `HIDDEN_BY_MAP` `pipeline/terrain.py:118-126`; `BLEND_MESH_BY_MAP` `:139-142`; `GRASS_BY_MAP` `:152-158`; `WATER_FLOW_BY_MAP` `:165-179`; `OPERABLE_BY_MAP` `:190-195` and again `pipeline/index.py:44-49`; `GATE_BOXES_BY_MAP` `pipeline/index.py:56-66` | `HIDDEN_BY_MAP[4] = {24, 25}` (MoveObject sets HiddenMesh -2 on both, `MM/Engine/Object/ZzzObject.cpp:4020-4028`: 24 the flame emitter x95, 25 the Meteorite trap pad x148); `BLEND_MESH_BY_MAP[4] = {18: 1, 19: 4, 20: 4, 23: 1}` (`ZzzObject.cpp:4007-4017`) -- pass B owns the look; **`GRASS_BY_MAP[4] = []`** (World5 ships no TileGrass .OZT; slot 0 is 64.7% of the floor); `WATER_FLOW_BY_MAP[4]` only if TileWater01 (10.5%) is judged a stream; `OPERABLE_BY_MAP[4]`: none found (no WD_4LOSTTOWER arm in MOVEMENT_OPERATE -- confirm in pass B); `GATE_BOXES_BY_MAP[4]` = all 15 boxes of §0, and `[2] +=` gate 28 (2,248,3,249) and exit 44 (2,246,3,247), which today are open by luck of the grid (Devias tiles 2-3 x 246-249 are word 0) rather than kept |
| ground sheets | `source/world/<w>/ground.json` -> `tools/content.sh:54-60` (tileset.py, ground.py); content.sh globs every `source/world/*/ground.json` (`:35-36`) | `source/world/losttower/ground.json`, a `lt_` prefix as `dn_`/`dv_`/`nr_` |
| objects | one `source/world/losttower/ObjectNN.json` recipe per kind | 39 kinds, Object41 has no .bmd (one placement, type 40, the lightning sprite stand -- §5) |
| index | `pipeline/index.py worlds()` `:1392-1480` (globs worlds, `number = map_number - 1` `:1476`); safe box `gate_rows` `:1755-1780` -> `world["gates"]["safe"]` `:2866-2869` | automatic once the ground is built and **mu.db has gate 42** (§3.4). index.py reads `source/mu.db` (`:2201`). NB a stray 0-byte untracked `MU2_BGFX/mu.db` appeared 2026-10-01 09:49; harmless, but not the store |

#### 1.2 The cook (`tools/cook.py`)

`main` `:3216-`; `--only {textures, ground, meshes, placements, figures, tables, showing,
missiles, wardrobe, all}` (`:3222`). Per-world keys in cook.py:

| key | line | Lost Tower |
|---|---|---|
| `AIRS` | `:202` `{"lorencia": "Bird01", "noria": "Butterfly01", "dungeon": "Bat01"}` | add `"losttower": "Bat01"` (MuMain loads MODEL_BAT01 for both, `MM/World/MapInfra/MapManager.cpp:51-58`; `GOBoid.cpp:1333`) |
| `AIRS_FROM` | `:212` | add `"losttower": "dungeon"` -- MU loads Bat01 from Data/Object2 for the tower; `boid_glb` `:218-220` then takes the Dungeon's built glb |
| `CRAWLS` | `:206` | **nothing**: MoveFishs' rat is WD_1DUNGEON only (`GOBoid.cpp:1720-1722`), the tower has no fish slot arm |
| `TRAP_MODELS` | `:209` | `"losttower": ()` or leave out: the Meteorite trap throws MODEL_FIRE, the Lich's rock, already a showing effect (§5). MapManager loads DungeonStone01 for the tower too (`MapManager.cpp:53-54`) but no tower object emits it (no type-52-like case in `ZzzObject.cpp:3996-4024`) |
| `GROUNDED_TYPES` | `:1130` | none expected |
| `ANCHOR_KINDS_BY_WORLD`, `HIDDEN_BY_WORLD` | `:1151`, `:1156` | only if a recipe asks |
| sunk-placement rule | `:1463` `if world != "dungeon" and ...` | **the tower wants the Dungeon's exemption**: the same flat sheet over a void. Turn the test into a set, `NOT_BURIED = {"dungeon", "losttower"}` |
| `figure_file` | `:1585` | automatic (`figures_losttower.json`) |
| `RESPAWN_VERSION075` | `:1746` | nothing if mu.db's `respawn_seconds` is written true (Balrog 150, the rest 10, as Devias did) |
| `FOLK_VERSION075` | `:1773-` keyed by map number | `4: [(240, "Baz The Vault Keeper"?, ...201,76), (253, ...207,76)]` if pass A keeps OpenMU's two NPCs |
| `PERCHES` | `:1856-1883` | none unless pass B finds an operate arm |
| `cook_tables` | `:1885-2200` | automatic: every breed, this map's spawns (`spawn["map"] != number` `:2002`), the safe box from index.json (`:2176-2180`). `.mur` stays **v9** -- the tower needs no new header field (§3.4) |

**Every world's tables after an item change** is still five runs now
(lorencia, noria, devias, dungeon, losttower); nothing loops them (`tools/content.sh` cooks
no tables).

#### 1.3 The runtime registry -- every place a sixth world is named

| # | place | what it holds | Lost Tower row |
|---|---|---|---|
| 1 | `src/game/world/maps.cpp:19-26` `kMaps` | `{world, map number, arrive}` | `{"losttower", 4, {208, 75}}` -- the middle of spawn gate 42 (203,70-213,81), and update the comment `:12-18` and `maps.h:20-23` |
| 2 | `src/sim/realm_travel.cpp:14-21` `kRows`, `src/sim/travel.h:29` `kTravels = 6` | the warp list | seven rows (`Gates.cs:51-57`, §0) and `kTravels = 13`; `found_` is a u32 of row bits, saved as `"found"` (`src/game/save.cpp:151,256`) -- 13 bits fit, and **row indices must only be appended**, or every save's bits shift |
| 3 | `src/game/ui/arrival.cpp:55-62` `kPlaces` | banner name | `{"losttower", "Lost Tower", ""}` |
| 4 | `src/game/ui/minimap.cpp:257-265` `mapName` | "Gate to X" | `case 4: return "Lost Tower";` |
| 5 | `src/game/ui/minimap.cpp:248-255` `floorName(exitGate)` | "Stairs to Dungeon 2" | cases 42/29 -> "Lost Tower 1", 31 -> 2, 33 -> 3, 35 -> 4, 37 -> 5, 39 -> 6, 41 -> 7 |
| 6 | `src/game/world/maps.cpp:67-75` `placeName` | floor names by tile, Dungeon only | the tower's floors by box (§0.1 table): non-overlapping, so seven box tests |
| 7 | `src/game/ui/travel.cpp:170` `kComing` | "Lost Tower" greyed | drops out by itself once `mapNumbered(4)` exists (`:172`) |
| 8 | `sheets/worlds/losttower.json` | the look, found by name (`maps.cpp:77-80`) | new (§5.3) |
| 9 | `src/sim/gates.cpp` | gates | §3 |
| 10 | `source/mu.db` | kinds, spawns, gate 42 | §4 |

The load path is the Dungeon's, unchanged: `PlayMode::open` -> `ctx.time.setScene(mapSheet(...))`
-> `World::open` (`src/game/world/world.cpp:90-114`) -> `Play::open` (`src/game/play_open.cpp:24-`,
`cooked/<w>/<w>.mur`, breeds without a figure held back) -> `World::raiseAirs` (`world.cpp:189-215`).
Arrival banner on map entry `play_mode.cpp:333-337` and on a warp `desk.cpp:257-261`, both
through `placeName`. `zoneLevels` (`maps.cpp:52-65`) reads nests only, and traps are not nests
(they live in `sim/traps.cpp`), so the tower will say "Level 47-66" without any exclusion.

---

### 2. Every hard-coded per-world check, and what to do with each

Grep: `grep -rnI -E '"(dungeon|devias|noria|lorencia)"|lost ?tower' src tools pipeline`, plus
the map-number tables. 31 files hit; the ones that decide behaviour:

| # | where | today | Lost Tower needs it? | smallest change |
|---|---|---|---|---|
| 1 | `src/game/world/world.cpp:94-95` `deviasFloors_ = name == "devias"; underground_ = name == "dungeon";` and `:328-341` `indoors()` | Dungeon is "under a roof on every tile" -> Roofed room, roofs hidden, air on | **yes**, the tower is a closed interior: underground_ true | add `bool underground` to `MapRow` (`maps.h:18-24`): `{"dungeon", 1, {108,247}, true}`, `{"losttower", 4, {208,75}, true}`; `underground_ = mapOf(name) && mapOf(name)->underground` |
| 2 | `world.cpp:208` `if (!underground_) leaves_.open(..., name == "devias")` | no leaves underground | yes (free via #1) | none beyond #1 |
| 3 | `src/game/play_open.cpp:32-34` `windy_ = world != "noria" && world != "dungeon"; dungeonAir_ = world == "dungeon"; snowy_ = world == "devias";` + `:446-449` the loop's name + `play_sound.cpp:397` `loop(wind, !indoors \|\| dungeonAir_)` | the air loop by name | **yes**: MU loops SOUND_TOWER01 = `Data/Sound/aTower.wav` on the whole map (`MM/Scenes/SceneManager.cpp:873-875`, `ZzzOpenData.cpp:4735`) | a per-world air name: `const char* air` on `MapRow` ("world_wind", nullptr for Noria (its jungle is weather's), "world_dungeon", "world_tower"); `windy_` becomes `air != nullptr`, `dungeonAir_` becomes `underground`. New `source/sounds/sounds.json` entry `world_tower: aTower.wav` beside `world_dungeon` (`sounds.json:1329-1332`) and the wav copied in |
| 4 | `src/game/play_sound.cpp:97-99` footsteps: Devias snow, else `floor == kGrassFloor` (`play_tuning.h:368`, slot 0) -> grass | **a bug today in the Dungeon too**: MU's PlayWalkSound plays the grass step on HeroTile 0 only in Lorencia and Noria (`MM/Engine/Object/ZzzCharacter.cpp:5348-5368`); everywhere else it falls to the soil/stone step. The Dungeon's and the tower's slot 0 is the flagstone floor (75.7% / 64.7%), so the hero walks on "grass" underground | yes | `grassy_ = world == "lorencia" \|\| world == "noria"` beside `snowy_`, or a `MapRow` flag; `floor == kGrassFloor && grassy_` |
| 5 | `src/game/world/boids.cpp:50-61` `boidOf`, `:63-86` `airsOf` | bat only for "dungeon" | **yes**, MU's bat flies in the tower (`GOBoid.cpp:1307, 1333-1334`) | `world == "dungeon" \|\| world == "losttower"` in both (or a 2-row table); cook side `AIRS`/`AIRS_FROM` (§1.2) |
| 6 | `src/game/world/scurry.cpp:32-35` `crawlOf` | rats only in the Dungeon | **no** -- MoveFishs' tower has no arm (`GOBoid.cpp:1712-1730`) | nothing |
| 7 | `src/game/world/trap_show.cpp:67-70` `if (world != "dungeon") return true;` and its 29 `kMarkers` (`:24-54`), Dungeon-only data | Dungeon's saw/stick/fire meshes and ceiling stones | **no** -- the tower's one trap kind draws MODEL_FIRE meteors (`MM/Engine/Object/ZzzCharacter.cpp:1994-2000`), Play's `fx/meteor.h`; the pad is hidden (`ZzzObject.cpp:4026-4027`) | leave the test; route trap 103's show to `Play::meteor()` (§5.1) |
| 8 | `src/game/world/maps.cpp:67-75` `placeName`: `if (world != "dungeon") return world;` + row/column cuts | floor names | **yes** | replace with a small table `{world, x1,y1,x2,y2, name}`; the tower's seven boxes are disjoint (§0.1) so plain box tests do; the Dungeon's existing three cuts become rows |
| 9 | `src/game/ui/minimap.cpp:248-255` `floorName(exitGate)` switch, `:257-265` `mapName(map)` switch | names by number | yes | cases 29/42, 31, 33, 35, 37, 39, 41 and `case 4`. Better: give `ExitGate` a `const char* floor` field in `gates.cpp` and drop the switch, since the gate table is where the floor is known |
| 10 | `src/sim/realm_travel.cpp:96-112` `travelQuest`: `constexpr int32_t kDungeon = 1; kGoldenArcher = 236` | a Dungeon floor's row is locked behind its link of the Golden Archer chain | returns -1 for map 4, so **no lock** -- fine unless a tower giver chain is wanted (pass A) | none now; if a chain comes, move `{map, giver}` into the `TravelRow` |
| 11 | `realm_travel.cpp:79-85` `settleFound`: a map with no quest giver opens **every** row of itself the first time he stands in it | Dungeon: all three floors on entry | **decision**: standing on floor 1 would open the floor 7 warp (8000 zen, lvl 70). 0.75 opens every row to anyone with the level and zen (`travel.h:9-13`). Proposal: open the row of the floor he stands on (`travelFloor()` already knows it, `:88-94`) | ~5 lines in `settleFound` + a call on `Climbed` |
| 12 | `src/app/modes/play_mode.cpp:588-592` `takeHome` -> `mapNumbered(0)` | a death/portal on a map with no safe box goes to Lorencia | **no** -- the tower has spawn gate 42, so `haven()` (`realm_fight.cpp:932-950`) finds a box on the map and `home` (`:984-986`, `realm_items.cpp:559-562`) is 0 | nothing; just the mu.db row (§4) |
| 13 | `play_mode.cpp:800-822` music by name (Lorencia's pub, Devias's roofs) | | MU plays `lost_tower_a.mp3` on the whole map (`SceneManager.cpp:1068-1072`; `D/Music/lost_tower_a.mp3`, `_b.mp3`). The user's rule: music rare and no fight music (memory, 2026-09-30) | **ask**; default nothing |
| 14 | `src/game/world/weather.cpp:113-116` the wet worlds by name | | **no** -- the tower is not listed, so it stays dry, as MU's weather has no tower arm | nothing |
| 15 | `src/game/world/ornaments.cpp:101-107` `kNoriaGlows`, `:229` `world == "noria"`, `:272` `beacon_ = world == "devias"` | Noria's object glows, Devias's beacon | **yes** for the glows (§5.2): types 19/20's six sprites, type 9's torch glow, Object41's star | make `kNoriaGlows` a `{world, model, ...}` table (`kGlows`) and test `world == glow.world`; the beacon stays Devias's |
| 16 | `world.cpp:268-271` beacon level 50 (33 for a knight) | Devias only | no | -- (note: 0.75's gate 28 asks 40, the beacon shows at 50; both are MU's, `ornaments.h:51-52`) |
| 17 | `src/sim/gates.cpp:38-41` gate 28 sealed, target -1, `"The Lost Tower"` | refuses every step | **yes**: open it (§3) | target 29 |
| 18 | `src/sim/realm_tuning.h:270-276` `kPoisoners = {8, 12, 39}` | already lists the Poison Shadow (39) | free | -- |
| 19 | `src/sim/quests.cpp:226-231` Devin's hand-in names the Lost Tower, `row.next = ""` | | pass A | -- |
| 20 | `src/game/headless.cpp:148,166` default world / Lorencia's old start tile | generic through `mapOf` | free | -- |
| 21 | `tools/cook.py:1463` `if world != "dungeon" and ...` (no "buried" flag underground) | | **yes**, same flat-sheet-over-void ground | a set (§1.2) |
| 22 | `tools/cook.py:202-212` AIRS/CRAWLS/TRAP_MODELS/AIRS_FROM | | §1.2 | rows |
| 23 | `pipeline/terrain.py` / `pipeline/index.py` `*_BY_MAP` | | §1.1 | rows |
| 24 | `src/game/play_open.cpp:150-154` Noria's roads; `doors.cpp:44` Devias's doors; `portal.cpp:40` Noria's portal; `lobby_mode.cpp:21`, `roster.cpp:175`, `args.cpp:665`, `bench_mode.cpp:59,102` | defaults and other worlds' features | no | -- |

**The one refactor worth doing first** (it removes #1, #2, #3, #5 and part of #4 as string
tests): widen `MapRow` with three flags -- `underground`, `air` (a sound name) and `boid` --
and read them where the names are tested now. Six worlds is the point where the strings start
to cost more than the row.

---

### 3. Travel as built, and what the tower's seven floors add

#### 3.1 What the Dungeon built (all generic now)

- **Gates are code**: `src/sim/gates.cpp:9-27` `kExits` (12 rows) and `:32-54` `kEnters` (13 rows),
  struct in `src/sim/gates.h:20-43` (box, `dx/dy` facing as OpenMU Direction's target tile, level,
  target, `sealed` name). Lookups `enterGateAt` `:58-63`, `enterGateNumbered` `:65-70`,
  `exitGate` `:72-77` -- linear scans, keyed by map in the box test only.
- **Walking in**: `Realm::step` tests the gate only on a tile change (`src/sim/realm_move.cpp:186-189`);
  `Realm::throughGate` `:211-253`: sealed -> `Barred(n, 0)`; under level -> `Barred(n, level)`;
  else a seeded tile in the exit box, re-drawn up to 16 times off any enter box (`:225-232`,
  ours: the Dungeon's exit 6 overlaps enter 7). Then **same map -> `setHeroDown` and `Climbed`**
  (`:246-249`, `:255-279`: nearestOpen within 8, facing `atan2(dy,dx)`, every monster drops him,
  the summon dismissed); **other map -> `Gated`** and the mode changes map
  (`src/app/modes/play_mode.cpp:575-585` -> `mapNumbered(out->map)` -> `travel()` `:1245-1268`).
- **The drawing of a climb**: `src/game/play.cpp:455` `Climbed` for the hero -> `warped()` (body
  snapped, marker dropped, `warpOwed_` set), so the banner comes up through `desk.cpp:257-261`
  with `placeName`. Refusal text `play.cpp:485-490` uses `gate->sealed` -- generic.
- **Minimap**: gate marks found by scanning every tile through `enterGateAt` once a map
  (`src/game/ui/minimap.cpp:603-621`) -- generic; label "Gate to <map>" / "Stairs to <floor>" /
  "(sealed)" / "(level N)" (`:821-832`), names from the two switches (§2 #9).
- **Death and the Town Portal**: `Realm::haven` (`src/sim/realm_fight.cpp:932-950`) draws a tile
  in `tables_->safeGate`, nearestOpen within 8; revive `:964-987`; portal
  `src/sim/realm_items.cpp:536-562`, refused on a safe tile. With **no** safe box, both say
  `home = 1` and the mode travels to Lorencia (`play_mode.cpp:588-592`, `Play::takeHome`
  `play.h:393-397`). That is the Dungeon's `safe_map` as built: a flag, Lorencia hard-coded.
- **The warp list (Tab, the "M move window" is gone -- `8288c1b7` removed the M key)**:
  `src/sim/realm_travel.cpp:14-21` six rows, `src/sim/travel.h:20-40`; refusals
  Unknown/Here/Dead/Level/Zen/Quest (`realm_travel.cpp:114-131`); a row on the map he is on is
  a `setHeroDown` in place (`:141-143`), any other is the mode's map change
  (`play_requests.cpp:361-362`, `play_mode.cpp:603-614`). **Floors** are flood-filled once a
  raise from each of the map's rows' landing tiles (`settleFound` `:45-75`) whenever a map has
  more than one row (`rows & (rows - 1)`), and `travelFloor()` (`:88-94`) says which he is on --
  generic, and it will fill the tower's seven regions with no change.
- **Level checks**: per enter gate (`gates.h:40`) and per warp row (`travel.h:23`); 0.75 has no
  two-thirds rule for a knight here (the beacon's 33 is a client draw rule only).

#### 3.2 What the tower's chain needs beyond that

1. **Rows, not code.** `gates.cpp`: exits 42 (spawn, but also the warp list's landing), 29, 31,
   33, 35, 37, 39, 41 and Devias's **44** (`{44, 2, {2,246,3,247}, 0, -1}` -- Direction 2,
   SouthWest, is (0,-1) in the traps' convention, `sim/traps.h:42-43`); enters 30, 32, 34, 36,
   38, 40, **43** (`{43, 4, {162,0,166,1}, 15, 44}`), and gate **28 unsealed**:
   `{28, 2, {2,248,3,249}, 40, 29}` (drop `"The Lost Tower"` or keep it as the minimap name).
   Facings: 29 East (+1,+1), 31/37/39/41 West (-1,-1), 33/35 South (+1,-1).
2. **One-way stairs.** There is no up-gate anywhere in the tower (§0), so unlike the Dungeon a
   hero on floor 6 can only go down, die, read a Town Portal (to floor 1's safe box) or pay the
   warp list. Nothing in the engine assumes a gate pair; nothing to build. Worth one line in the
   doc so nobody "fixes" it.
3. **Its own safe box -- no `home` trip.** mu.db needs the spawn row `(42, 4, 203, 70, 213, 81, 1)`;
   then index.py writes `gates.safe`, the cook writes it into `losttower.mur`'s header
   (`tools/cook.py:2176-2180`), `haven()` finds it, and `home` stays 0. **Without the row the
   tower would silently send every death to Lorencia** (the Dungeon's path), which is the
   Devias gate-22 bug of `1ad73855` again. The safe flag on the grid (cols 187-214, rows 70-83)
   is already there for `worth()` and the traps.
4. **Seven travel rows** (§1.3 #2) with `kTravels = 13`: LostTower (lvl 50, 5000, landing 208,75,
   facing none -- it is the spawn box), LostTower2 (50, 5500, 242,237, -1,-1), 3 (50, 6000,
   86,167, +1,-1), 4 (60, 6500, 87,87, +1,-1), 5 (60, 7000, 129,53, -1,-1), 6 (70, 7500, 53,53,
   -1,-1), 7 (70, 8000, 8,86, -1,-1). Appended after the Dungeon's six so saved `found` bits keep
   their meaning. `travel.cpp`'s row list is built from these (`kComing` drops "Lost Tower"
   itself, `travel.cpp:172`).
5. **Opening the rows** (§2 #11): today all seven open the moment he stands on floor 1. Decide.
6. **Floor names** (§2 #8, #9) for the banner, the minimap's "Stairs to", the Tab list's
   "you are here" (`TravelRefusal::Here` via `travelFloor()` is already generic).
7. **A small existing bug the tower's list would hit more**: `Play::travel`'s log table
   `kWhy[]` has six strings (`src/game/play_requests.cpp:355-356`) but `TravelRefusal` has seven
   values (`src/sim/travel.h:37`, `Quest` = 6), so a quest-locked Dungeon row reads past the
   array. Add `"quest not taken"`.
8. **sim_test**: a `testTowerGates` on `testDungeonGates`' walker (`tests/sim_test.cpp:3901-3935`):
   Devias gate 28 at level 39 -> Barred 40, at 40 -> Gated 28 landing in 162-166 x 2-3; on the
   tower's `.mur`, gate 30 at 40 -> Climbed into 241-244 x 237-238 (floor 2), gate 34 at 49 ->
   Barred 50; gate 43 -> Gated 43 landing in Devias 2-3 x 246-247; a kill on floor 7 -> Rose with
   `c == 0` and a tile inside 203-213 x 70-81. No test walks gate 28 today (no "sealed" or
   2,248 in `tests/sim_test.cpp`), so unsealing breaks nothing; the sealed path then has no
   gate left to test it, and a test-only sealed row is not worth adding.

#### 3.3 The Devias end, today

- **Gate 28 exists and is sealed**: `src/sim/gates.cpp:38-41`, level 40, target -1, named
  "The Lost Tower"; a step in says "The Lost Tower is sealed. Its door will not open."
  (`play.cpp:485-490`); the minimap marks it "The Lost Tower (sealed)" (`minimap.cpp:827-829`).
- **Its tiles are open**: Devias's attribute grid has 2-3 x 246-249 at word 0 (checked on
  `source/world/devias/attributes.png`), so both gate 28 and the landing box of exit 44 are
  walkable without a `GATE_BOXES_BY_MAP[2]` entry; add them anyway so a future solid stamp
  cannot close them (the reason that table exists, `index.py:51-55`).
- **What is drawn there**: MU puts no door model on it. What marks it is the hidden type-100
  placement `Object101` at tile 3.3, 247.6 that MU draws as two spinning BITMAP_LIGHTNING+1
  sprites 1.5 m over its origin (`ZzzObject.cpp:2821-2828`), built here as the **beacon** in
  `src/game/world/ornaments.cpp:112-130`, `:272`, shown only at level 50 (33 for a knight)
  (`world.cpp:268-271`, `ornaments.h:51-52`), with our mist. Nearby placements are Devias's
  Object01 walls and Object05s (1.0,247.5; 3.0,244.0; 2.5,250.5). So the gate is "drawn" only
  by its beacon -- the same as MU.
- **Exit 44 is missing** from `kExits` (the way back from the tower), as is gate 43.

---

### 4. Spawns, monster tables and the per-world cooks

#### 4.1 What mu.db holds today (`sqlite3 -readonly source/mu.db`, 2026-10-01)

- `monster_kinds`: numbers **0-33** (Lorencia, Noria, Devias and the Dungeon's twelve). Schema:
  `number, name, level, health, minimum_damage, maximum_damage, defense, move_range,
  attack_range, view_range, move_delay (ms), attack_delay (ms), attack_rate, defense_rate,
  respawn_seconds, attack_skill`. **None of 34-41.**
- `monster_spawns` by map: 0 -> 9 rows / 290; **1 -> 258 rows / 258** (one to a spot); 2 -> 9 / 560;
  3 -> 8 / 1005. `max(id)` = 385, so new rows start at **386**. Columns `id, number, x1, x2,
  y1, y2, count, map` (note x2 before y1).
- `gates`: 17 (map 0), 22 (map 2), 27 (map 3), all `spawn = 1`. **No 42.**
- `npc_spawns` (14 rows) is not what the cook reads for folk; `FOLK_VERSION075` is (`tools/cook.py:1773`).
- The WAL is empty (`source/mu.db-wal` 0 bytes), so the file is current; after the inserts,
  `pragma wal_checkpoint(TRUNCATE)` before committing (memory: "mu.db writes sit in the WAL").

#### 4.2 What the tower needs in mu.db

1. **Eight kinds**, OpenMU `LostTower.cs:655-917` (ids, levels as §0; `move_delay` 400 ms -> 8
   ticks, attack delays 1400-2000 ms all divide by 50, so `ticks_of` logs no remainder,
   `cook.py:1749-1760`). `respawn_seconds` written true: 10, **Balrog 150** (as Devias wrote its
   3 s and 50 s straight into mu.db, `6d209434`). `attack_skill`: Cursed Wizard **2**, Devil **3**,
   Poison Shadow **1**, Death Gorgon / Balrog `MonsterSkill` (150) -- `attackSkill` is read only by
   the drawing (`src/content/tables.h:39`, "a monster's animation and projectile choice"), so
   150 or NULL changes nothing in the sim. What that buys for free:
   - Cursed Wizard -> the Lich's meteor (`src/game/play.cpp:872-880`, `attackSkill == 2`),
     range 4.
   - Devil -> the Thunder Lich's Lightning (`play.cpp:939-947`, `== kLightning`), range 4.
   - Poison Shadow -> already in `kPoisoners` (`src/sim/realm_tuning.h:270-276`), stacking.
   - Balrog's own `AT_SKILL_BOSS` show (MODEL_CIRCLE + CIRCLE_LIGHT + Hellfire + a meteor a
     frame within +-5 tiles, `MM/Engine/Object/ZzzCharacter.cpp:1974-1992`) is new showing work.
2. **448 spawn rows** for map 4, every one a one-tile box of count 1 (`LostTower.cs:52-~650`;
   unlike the Dungeon's table, it is **not doubled**: 596 rows, 592 distinct, 4 repeats). The
   per-floor split is in §0.1. Population 448 sits between Lorencia's 290 and Noria's 1005, so
   no sim budget question.
3. **Gate row 42**: `insert into gates values (42, 4, 203, 70, 213, 81, 1)` -- the one row that
   makes death and the Town Portal stay in the tower (§3.2 #3). Nothing else in `gates` is read.
4. NPCs: not mu.db -- `FOLK_VERSION075[4]` in cook.py if pass A keeps OpenMU's 240 at 201,76 and
   253 at 207,76 (Season-6 additions? pass A).

#### 4.3 Through the pipeline into the game

- index.py reads kinds and spawns (`pipeline/index.py:1683` `SELECT ... FROM monster_kinds`,
  `:1728` spawns) into `index["breeds"][].spawns` with each spawn's `map`.
- `cook.py --world losttower --only tables` -> `assets/cooked/losttower/losttower.mur` (v9):
  **every** breed (the kinds table is global), only map 4's spawns (`:2002`), the safe box. So
  adding eight kinds changes every world's `.mur` kinds block; the other four worlds need a
  `--only tables` too only for consistency (their spawns do not change, and nothing indexes a
  kind by position across worlds -- spawns carry an index into their own file's kinds,
  `:2003`). Do it with the item-table runs anyway: lorencia, noria, devias, dungeon, losttower.
- **Held back** in the window until each breed's figure is cooked
  (`src/game/play_open.cpp:117-141`, "play: N monsters of losttower held back"); headless
  raises everything (`src/game/headless.cpp:148-170`). Per breed:
  `tools/cook.py --world losttower --only figures --monsters <Name>01` into
  `figures_losttower.json` (`cook.py:1585`), one at a time, judged on the stage.
- **Traps are not mu.db**: they are `src/sim/traps.cpp` rows (Dungeon's 58, `:16-~100`) and
  `realm_traps.cpp`. The tower adds:
  - `TrapKind {103, "Meteorite Trap", "Object26", 3, ...}`, level 90, 160-190, attack rate 450,
    defence rate 500 (`LostTower.cs:919-939`);
  - **a third trigger**: OpenMU's `AttackAreaWhenPressedTrapIntelligence` -- fires only when
    somebody stands ON its tile, then hits everyone within AttackRange 3 (square) off a safe
    tile. `realm_traps.cpp:50-59` has `pressed` (own tile) and the facing-area test; the tower's
    is "pressed to arm, area to hit", which for a single hero reduces to "on its tile and not on a
    safe tile". Smallest change: a `bool area` beside `pressed` in `TrapKind` (`traps.h:26-36`).
  - 148 spot rows `{4, 103, x, y, 0, 0}` (146 distinct tiles; two pairs share a tile; one shares
    a tile with a monster spawn).
  - `testTraps` (`tests/sim_test.cpp:3824-3836`) asserts `trapSpots` count == 58 for **all
    maps**; it must count map 1's only, or it fails the day the tower's rows land.

---

### 5. Effects the tower wants, and what the engine has

What MuMain does on WD_4LOSTTOWER is short; all of it is in `ZzzObject.cpp` (13 references to
the map in the whole client, read in full):

| MU | where | count | engine today |
|---|---|---|---|
| type 24 hidden, `CreateEffect(BITMAP_FLAME, o->Position, ...)` one reference frame in 64 | `ZzzObject.cpp:4020-4024` (MoveObject) | **95 vents** (Object25) | the wizard's Flame **is** BITMAP_FLAME sub-type 0 -- `src/game/fx/flame.h` (40 frames, six plumes a frame, a scorch, (1,0.4,0) over three tiles). But its pool is `kFires = 8`, `kMostPlumes = 320` (`flame.h:84-91`) and its light goes through the 4 transient slots (`renderer.h:401`) |
| type 25 hidden | `:4026-4027` | 148 (Object26) | the Meteorite Trap's pad: nothing drawn |
| types 38, 39 `CheckSkull` -- a skull kicked along the floor when he walks within 50 units, with SOUND_BONE2 | `:3999-4001`, body `Render/Effects/ZzzEffectFireLeave.cpp:94-118` | **1 112** (Object39 x777, Object40 x335) | **nothing**: the town is static instanced chunks (`docs/architecture.md`, PLAN foundation 6-7). New engine work, or leave them still |
| types 3, 4 scroll U; 19, 20 BlendMesh 4 + scroll; 18, 23 BlendMesh 1 | `:4003-4017` | 211+399, 14+6, 51+123 | `BLEND_MESH_BY_MAP` + the glow scroll (`f0d4b119`'s cook/cook_one `scroll`) -- content |
| types 3, 4, 19, 20 drawn a second time as a red (1,0.2,0.1) chrome stream mesh | `ZzzObject.cpp:1035-1059` (RenderObject) | as above | Devias's Messenger used the `over` glow for an opaque+added pass (`83d8f729`); the chrome stream is a look for pass B |
| types 19, 20: six rotating sprites at bones 15/19/21 (0.3, 0.3, 1.5), BITMAP_MAGIC+1 red for 19, BITMAP_LIGHTNING+1 blue for 20, `WorldTime*0.1` deg | `:2934-2956` (RenderObjectVisual) | 14 + 6 | **`ornaments.cpp` already draws exactly this kind** for Noria (`kNoriaGlows` `:101-107`: model, bones, scale, colour, spin, sheet `lightning_2`) -- rows in a per-world table |
| type 40: two BITMAP_LIGHTNING+1 at 2.5, 2.6 m up | `:2957-2981` | 1 (Object41) | Object41 has **no .bmd** in D/Object5, so MU never draws it; skip |
| **type 9 falls through into WD_6STADIUM's switch** (no `break` after the tower's inner switch, `:2982-2983`): `CreateSprite(BITMAP_LIGHT, bone 1, Luminosity*5, (0.6,0.3,0.1)*L)` | `:2983-2993` | **200** (Object10) | a lantern row in the same table; whatever Object10 is (pass B), MU hangs an orange flare on it by the missing break -- an accident that is the tower's look, so keep it and mark it |
| air: SOUND_TOWER01 `aTower.wav` looped | `SceneManager.cpp:873-875` | -- | §2 #3 |
| music: `lost_tower_a.mp3` on the whole map | `SceneManager.cpp:1068-1072` | -- | ask (§2 #13) |
| clear/fog: no tower arm -> default black | `SceneManager.cpp:399-400` | -- | the shade's black clear; the sheet |
| boid: MODEL_BAT01, lit, velocity 1 | `GOBoid.cpp:1304-1334` | -- | `boids.cpp` (§2 #5) |
| sanity tile `TerrainWall[75*256+193] == 5` | `ZzzLodTerrain.cpp:216-218` | -- | a cheap extraction check: the scratch grid has word 5 (safe + no-move) at 193,75 |

#### 5.1 Lightning strikes and flashes

- **The Devil's Lightning** is the Thunder Lich's, free (`play.cpp:939-947` -> `fx/thunder.h`, the
  wizard's bolt; blue ground light through a transient slot). **The Cursed Wizard's Meteorite**
  is the Lich's, free (`play.cpp:872-880` -> `fx/meteor.h`).
- **The Meteorite Trap** draws MODEL_FIRE -- the same rock -- at a random point within +-512
  units (+-5 tiles) of the trap, one a frame while its attack is on, with SOUND_METEORITE01
  (`ZzzCharacter.cpp:1994-2000`). Route `What::Trapped` for kind 103 (`play.cpp:460-480`, which
  today asks `trapShow_` for the Dungeon's meshes) to `meteor().cast(...)` a few times, rather
  than to `TrapShow`. The meteor's pool and its light slot are the limit to watch.
- **The Balrog's boss blow** is the same rain plus MODEL_CIRCLE/CIRCLE_LIGHT and Hellfire
  (`ZzzCharacter.cpp:1974-1992`); two Balrogs on floor 7, respawn 150 s. Showing work.
- **A sky flash**: the engine has one -- `Weather::flash()` (`src/game/world/weather.h:28-32,
  71-73`), a curve read off the playing thunder clap, which `TimeOfDay` lays on the light
  (`src/app/context.cpp`). MU has **no** flash in the tower (no weather arm, no thunder), so any
  flash on a Devil's or a trap's strike would be ours; the hook would be a short exposure kick
  from the strike, not Weather (which is a world spell keyed by name, `weather.cpp:113-116`).

#### 5.2 Fire on objects

- **The 95 vents** are the tower's signature and the largest new runtime piece. At one in 64
  reference frames (25 fps; `Random::FpsCheck`, `MM/Core/Utilities/Random.cpp:75`, REFERENCE_FPS
  `MM/Engine/AI/ZzzAI.h:11`) a vent lights every 2.56 s on average and each Flame lives 40
  frames (1.6 s), so about **0.6 flames burn per vent** at any moment -- with 10-15 vents on
  screen, 6-9 flames, against `fx/flame.h`'s `kFires = 8`.
  - Data: the placements are hidden (§1.1), so they never reach the `.mut`. The Dungeon solved
    the same problem for its ceiling stones by copying 29 markers into code
    (`trap_show.cpp:20-54`); 95 is too many to hand-copy. **Better: `ANCHOR_KINDS_BY_WORLD`**
    (`tools/cook.py:1151`), the character screen's precedent for hidden fire placements
    (`:1147-1150`): `{"losttower": {"Object25": <a new kind>}}` writes each vent as a world-anchored
    emitter (`model 0xFFFF`, `:1553`), which `Lamps::open` already collects (`lamps.cpp:150-156`).
  - Drawing: a vent driver (in `Lamps` or a small `game/world/vents.*`) that rolls each anchored
    vent near the camera and calls the Flame's `light(at, yaw)` (`flame.h:38`), with `kFires`
    raised to ~16. The plume budget (320) is the one to price.
  - Light: **not** through the 4 transient slots -- six vent fires would starve every spell's
    ground light. Give each vent a static point light in the lamp grid with its level driven by
    its own flame's life (the lamps already flicker per frame, `setPointLightLevels`,
    `renderer.h:376-379`).
- **The lamp grid's cap**: `kMaxPointLights = 255`, `kLightsPerCell = 8` per 2 m cell
  (`renderer.h:373-375`). 95 vents + 200 type-9 flares = 295 if every flare also lit the ground.
  MU's flare is a sprite only (no AddTerrainLight; the baked TerrainLight carries the pools), so
  **the 200 are sprites (ornaments), not lights**, and the vents fit with 160 to spare. The
  Dungeon spent 120 on its torches (`sheets/worlds/dungeon.json` note).

#### 5.3 Darkness, light and fog: the sheet

- **Template: `sheets/worlds/dungeon.json`** (user-tuned 2026-09-30, every step in its note):
  elevation 60, a faint cold overhead key (`sun_strength 1.1`) so a body still casts MU's short
  shadow, near-black sky/ground ambient, exposure 1.9, pale cellar mist `dust_density 0.009`,
  lamps and flames 8-8.5, bloom 0.4, Hill ACES (`tonemap 1`), `grass 0`, `probe 0`,
  `lamp_shadow 0.85`, `water_flow`/`water_sheen` for its stream. Copy it to
  `sheets/worlds/losttower.json` and retune by eye: the tower's TerrainLight is darker and warmer
  (mean 89,81,83 against 108,108,140), so the grade's cold split probably wants to lean warm.
- **Fog/void**: 16 014 NoGround tiles (24%, words 8 and 12). The Devias abyss
  (`Ground::buildAbyss`, `src/content/ground.cpp:1042`; `void` block `:643`) and the shade's
  black clear handle it generically; the Dungeon's json carries no `void` block, so the default
  is MU's hard cut.
- **Grass**: `GRASS_BY_MAP[4] = []` writes `grass_slots: []` (as the Dungeon's json has), and
  `grass 0` in the sheet; either alone stops slot 0 sowing a lawn.
- **Water**: TileWater01 is 10.5% of the tower's floor -- twice the Dungeon's share. What it is
  (a moat of something, or dark ground misnamed) is pass B's; if it is still water, the Dungeon's
  sheen/flow knobs apply.

#### 5.4 The shadow sun and the budget

- The sun's split is always drawn (`src/gfx/renderer.cpp`, view 0, account 1.0 ms,
  `docs/budget.md:11`). The Dungeon kept it on as a weak overhead key; do the same.
- **No Dungeon or interior frame has been priced** -- `docs/budget.md` has Lorencia's 1080p
  accounts and the 2K table only (`:48-74`: baseline 6.345 ms at 2560x1273; `--no-lamps` 0.25,
  `probe 0` 0.23, `grass 0` 0.14, the sun drawing all 2753 placements vs the camera's 497 only
  0.04). Geometry is not the cost on this GPU (`:75-82`), so the tower's **5380** placements
  (1.95x Lorencia's) should not move the frame much; fill does -- the lamps' PCSS-free loop,
  the vents' plumes (blended overdraw, 0.018 ms per full screen at 1080p, effects account
  0.3 ms, `budget.md` "sprites" section), and bloom on the flares.
- Measure the Dungeon first as the proxy (same kind of map, already built), then the tower's
  bare land, with `--stats` and `--repeat 3` per `docs/budget.md`, timers off (memory: "view
  timers cost the frame"), on the display the log names.

---

### 6. The build, in order

#### 6.0 What the Dungeon already gives, free

Same-map gates as a climb (`realm_move.cpp:242-279`) and their drawing (`play.cpp:455`); landing
off enter boxes (`:225-232`); level and sealed refusals and their text; the minimap's gate scan
(`minimap.cpp:603-621`); floors flood-filled from the travel rows and the Tab list's "Here"
(`realm_travel.cpp:45-131`); a same-map warp row set down in place (`:141-143`); `haven()` on any
map with a safe box; the NoGround abyss and `grass_slots: []`; a tuned underground sheet to copy;
Bat01 built (cook it in by `AIRS_FROM`); the monsters' meteor, lightning and stacking poison by
`attackSkill`/`kPoisoners`; the trap machinery (`realm_traps.cpp`, `What::Trapped`); breeds
held back until cooked; per-world figure tables. **What it does not give**: a safe box on the
map (the tower has one; the Dungeon did not), the third trap trigger, vents, kickable skulls,
seven-floor naming, and the air/footstep/boid switches still being string tests.

#### 6.1 Steps, each small, each with a headless check

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. No window without asking;
`--mute` on any review run.

**Step 0 -- the per-world switches become a row (no content, no behaviour change).**
- `src/game/world/maps.h:18-24` `MapRow` += `underground`, `air`, `grassy`, `boid`;
  `maps.cpp:19-26` fills them for the four worlds as today's strings say.
- Read them at `world.cpp:94-95`, `play_open.cpp:32-34,446-449`, `play_sound.cpp:97-99` (the
  grass step for Lorencia and Noria only -- **this one changes the Dungeon's footsteps to MU's**,
  say so in the commit), `boids.cpp:50-86`.
- `maps.cpp:67-75` `placeName` -> a floor-box table; `minimap.cpp:248-255` `floorName` -> a
  `floor` name on `ExitGate` (`gates.h:27-34`).
- Fix `play_requests.cpp:355-356` `kWhy` (seven strings).
- Check: `cmake --build build --target checks` (sim_test, boids_test, cooked_test); a headless
  `--world dungeon --ticks 2000 --seed 7` log byte-identical before and after (the sim is not
  touched).

**Step 1 -- bare land stands, reachable by Tab/`--travel-at`.**
- `pipeline/terrain.py`: `HIDDEN_BY_MAP[4] = {24, 25}`, `GRASS_BY_MAP[4] = []`, `BLEND_MESH_BY_MAP[4]`
  (§1.1); `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP[4]` (15 boxes) and `[2] +=` 28 and 44.
- `python3 pipeline/terrain.py $D/World5 losttower source/world 5` (expect: 0.00-3.19 m, 10 slots,
  46.1% walkable, 0.6% safe, 5380 placed / 39 kinds, missing Object41 -- §0.1);
  `source/world/losttower/ground.json` with `lt_` sheets; `tools/content.sh --world losttower`;
  sync; `tools/cook.py --world losttower --only ground`.
- `source/mu.db`: `insert into gates values (42, 4, 203, 70, 213, 81, 1)`; checkpoint; index.py;
  `tools/cook.py --world losttower --only tables`.
- `maps.cpp` `{"losttower", 4, {208, 75}, ...}`; `arrival.cpp` `kPlaces`; `minimap.cpp` `mapName`;
  `sheets/worlds/losttower.json` copied from the Dungeon's.
- Check: `./run.sh --headless --world losttower --ticks 200 --no-hand` logs `grid 256 tiles a side
  ... map 4`; `python3 -c` on `assets/cooked/losttower/losttower.mur`'s header:
  `'MU2R', 9, ..., map 4, 256, 203, 70, 213, 81` (a zero box here is the gate-22 bug).

**Step 2 -- the gates, the stairs and the warp rows.**
- `src/sim/gates.cpp`: exits 42, 29, 31, 33, 35, 37, 39, 41 (map 4) and 44 (Devias); enters 30, 32,
  34, 36, 38, 40, 43; gate 28's target 29. Header comment `gates.h:11-13` (still says "only
  Lorencia's and Noria's pair").
- `src/sim/realm_travel.cpp:14-21` seven rows appended, `travel.h:29` `kTravels = 13`.
- The floor-box rows for `placeName` and the exit gates' floor names (step 0's tables).
- `tests/sim_test.cpp`: `testTowerGates` (§3.2 #8).
- Check: `cmake --build build --target checks`; headless on Devias `--at 3,246 --level 40` with a
  scripted walk to 3,248 logs the hero's `Gated 28` (and `--level 39` a `Barred 40`).

**Step 3 -- underground: air, bats, no leaves, no grass.**
- `MapRow` row flags for the tower (underground, `air = "world_tower"`, boid Bat01);
  `source/sounds/sounds.json` `world_tower: aTower.wav` (+ copy `$D/Sound/aTower.wav`), then
  `pipeline/index.py` and `tools/cook.py --only showing` (memory: "new sound needs the showing
  cook"); `tools/cook.py` `AIRS["losttower"] = "Bat01"`, `AIRS_FROM["losttower"] = "dungeon"`,
  and the `:1463` buried-test exemption as a set.
- Check: a `--frames 300 --shot 250 --mute` review only when allowed; headless can only check the
  tables. The log should carry no `leaves:` line and the boids' Bat01; then `--budget --stats`.

**Step 4 -- objects** (pass B's worklist, by count: Object02 872, Object39 777, Object17 418,
Object05 399, Object06 357, Object07 351, Object40 335, Object04 211, Object10 200, Object09 172).
`source/world/losttower/ObjectNN.json` per kind, `lt_`-prefixed sheets; `cook_one.py ObjectNN
--world losttower` and the studio sheet; then `--only textures/meshes/placements`. Cook only
what changed (memory).

**Step 5 -- the glows.** `ornaments.cpp` `kNoriaGlows` -> a per-world table; the tower's rows:
Object20/Object21's six sprites (bones 15, 19, 21; 0.3/0.3/1.5; red magic for Object20, blue
lightning for Object21; spin `WorldTime*0.1`), Object10's orange flare at bone 1 (the stadium
fall-through, marked). Check: the `ornaments:` log line's lantern count (20x6 + 200 = 320).

**Step 6 -- the vents.** `tools/cook.py` `ANCHOR_KINDS_BY_WORLD["losttower"] = {"Object25": vent}`
and an `ANCHOR_LIGHT` row; a vent driver rolling one in 64 reference frames per anchored vent in
range and calling `Flame::light`; `kFires` 8 -> ~16; the vent's static light levelled by its fire.
Check: the `lamps:` log line counts 95 anchors and the light grid's "crowded" cells
(`renderer_lights.cpp:94-98`); `--budget`, effects account.

**Step 7 -- the monsters.** `source/mu.db`: kinds 34-41 (§4.2), 448 one-tile rows for map 4 from
id 386, checkpoint; index.py; `--only tables` for all five worlds; figures one breed at a time
(`--only figures --world losttower --monsters <Name>01`). Check: headless `--world losttower
--ticks 30000 --level 60` reports 448 monsters; the window log's "held back" list shrinks per
breed.

**Step 8 -- the Meteorite Trap.** `src/sim/traps.cpp` kind 103 with an `area` trigger
(`traps.h:26-36`, `realm_traps.cpp:50-59`) and 148 spots on map 4; `testTraps` counts map 1 only
and a new case puts a hero on a 103 tile (hit) and beside it (not); `play.cpp:460-480` sends a
103 `Trapped` to the meteor, not `TrapShow`. Check: `checks`.

**Step 9 -- decisions and details.** The Balrog's boss show; kickable skulls (1 112 of them --
a dynamic-placement path, or leave them still); music (`lost_tower_a.mp3`, the user's rule says
probably none); whether each warp row opens on reaching its floor (§2 #11); whether 0.75 had the
two NPCs in the safe box (pass A).

#### 6.2 Decisions for the user

1. **Warp rows open per floor reached, or all seven on first entry** (today's rule for a map with
   no giver, `realm_travel.cpp:79-85`).
2. **Music**: MU's `lost_tower_a.mp3` everywhere, or silence as the rule "music is rare" suggests.
3. **Kickable skulls** (CheckSkull, 1 112 placements): build a small dynamic path, or leave still.
4. **The two NPCs** at 201,76 / 207,76 (OpenMU lists 240 and 253): keep, or not 0.75.
5. **Footsteps underground**: step 0 moves the Dungeon to MU's stone step; the user may have
   grown used to the grass one.
6. **Any flash or shake on a trap's or Devil's strike**: MU has none; ours if wanted.

---

#### Side findings (outside the tower, cheap to fix)

- `play_requests.cpp:355-356`: `kWhy[6]` read with `TravelRefusal::Quest` (= 6) -- past the end.
- `play_sound.cpp:97-99`: the grass step plays on slot 0 in every non-Devias world, so the
  Dungeon's flagstones sound like grass; MU plays grass on slot 0 only in Lorencia and Noria
  (`ZzzCharacter.cpp:5348-5368`).
- `tests/sim_test.cpp:3832` counts every map's trap spots as the Dungeon's 58.
- `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP[0]` lacks Lorencia's Dungeon boxes (121,231-123,233)
  and `[2]` lacks gate 28 / exit 44; all open today by the grid, not by the table.
- An untracked 0-byte `MU2_BGFX/mu.db` (2026-10-01 09:49); the store is `source/mu.db`.
- `src/sim/gates.h:11-13` still says only Lorencia's and Noria's pair is here.
