# Devias port: the record, and how it plugs in

Research notes, 2026-09-29. Devias is MU server map **2**, client `Data/World3` + `Data/Object3`.
MU2 never imported it. This page is the map's *record* -- gates, safe zone, warp, monsters,
NPCs, the object inventory, name collisions -- and every place MU2_BGFX has to learn a third
world. The terrain is `docs/devias-ground.md`; the effects are `docs/devias-effects.md`.

Paths: MuMain source is `LEGACY/reference/MuMain/src/source/` (MM below), MuMain data
`LEGACY/reference/MuMain/src/bin/Data/`, OpenMU init
`LEGACY/reference/openmu/src/Persistence/Initialization/` (OM below). Nothing here was
committed, and no file but this one was edited. Scratch (terrain.py output, every Object3 model
exported, the inventory TSV):
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/30347dbe-6e16-41b8-abb6-e97e08b80818/scratchpad/devias/port/`.

**The one thing to know first.** The MuMain data is not the 0.75 Devias. It is a later client's
map, and in three corners of it the difference can be seen: the attribute grid agrees with
OpenMU's Season 6 `Terrain3.att` at 99.2% of tiles and with `075_Terrain3.att` at only 88.6%,
and Season 6's `Gates.cs` has gates standing in exactly those corners (§1.6). Treat those corners as
**not 0.75** until the user rules on them.

---

## 1. The record, from OpenMU Version075

### 1.1 Map

`OM/Version075/Maps/Devias.cs:20` `Number = 2`, `:25` `Name = "Devias"`. The terrain OpenMU uses
is `TerrainVersionPrefix => "075_"` (`OM/Version075/Maps/BaseMapInitializer.cs:27`), so the server's
attribute file is `OM/Resources/075_Terrain3.att`. 0.95d and Season 6 set the prefix empty
(`OM/Version095d/Maps/Devias.cs`, `OM/VersionSeasonSix/Maps/Devias.cs:25`) and read `Terrain3.att`.

### 1.2 Gates (`OM/Version075/Gates.cs`)

Exit gates (where you come out), `CreateExitGate(map, x1, y1, x2, y2, direction, isSpawnGate)`:

| gate | map | box | dir (OpenMU `Direction`) | faces (dx,dy) | role | line |
|---:|---|---|---|---|---|---|
| **22** | Devias | 197,35 - 218,50 | 0 Undefined | -- | **isSpawnGate = true**: the SafezoneSpawnGate, Move-window target, Town Portal/death landing | `Gates.cs:128` |
| 19 | Devias | 242,34 - 243,37 | 7 North | (-1,+1) | out of Lorencia's gate 18 | `:129` |
| 44 | Devias | 2,246 - 3,247 | 2 SouthWest | (0,-1) | out of Lost Tower's gate 43 | `:130` |
| 21 | Lorencia | 7,38 - 8,41 | 3 South | (+1,-1) | out of Devias's gate 20 | `:115` |
| 29 | Lost Tower (4) | 162,2 - 166,3 | 5 East | (+1,+1) | out of Devias's gate 28 | `:139` |

The (dx,dy) is `DirectionExtensions.CalculateTargetPoint`, which is how `src/sim/gates.h:31-33`
stores a facing (so Lorencia's 26 West is (-1,-1), as in `src/sim/gates.cpp:10`).

Enter gates (boxes you walk into), `CreateEnterGate(number, target, x1, y1, x2, y2, level)`:

| gate | on | box | to | level | line |
|---:|---|---|---|---:|---|
| **18** | Lorencia | 5,38 - 6,41 | 19 (Devias) | **15** | `Gates.cs:193` |
| **20** | Devias | 244,34 - 245,37 | 21 (Lorencia) | 0 | `:194` |
| 28 | Devias | 2,248 - 3,249 | 29 (Lost Tower) | 40 | `:197` |
| 43 | Lost Tower | 162,0 - 166,1 | 44 (Devias) | 15 | `:204` |

So the Lorencia <-> Devias pair is **18 -> 19** (level 15) and **20 -> 21** (level 0): Lorencia's
west edge to Devias's east edge. Each exit sits two tiles off its enter box, as Lorencia/Noria's
do. 0.95d is identical (`OM/Version095d/Gates.cs:131-133,203,206`); Season 6 adds more (§1.6).

Checked against the client grid (scratch `attributes.png`): gate 19's and 20's eight tiles are
all word 1 (safe, open); gate 22's 352 tiles are all safe and 343 are plain word 1; gates 44/28
are word 0 (open, not safe).

### 1.3 Warp list (the Move window)

`OM/Version075/Gates.cs:47`: `CreateWarpInfo(4, "Devias", 2000, 20, gates[22])` -- index 4,
**2000 zen, level 20**, lands on gate 22. (Lorencia and Noria are 2000 zen at level 10, `:45-46`.)
0.75 has no graphical Move menu, only the text command (`:40`). MU2_BGFX's M key is free and
unlevelled (`src/app/modes/play_mode.cpp:543-549`), so the level 20 and the price are not
enforced anywhere yet.

### 1.4 Safe zone and terrain attributes

terrain.py on World3 (scratch `terrain.log`):

```
height     256x256 tiles, 0.00 to 3.78 m        (header says 1078; read at 1080)
tiles      14 of the slot table in use           (Grass01 43.8%, Grass02 37.6%, Ground01 7.7%, Water 3.9% ...)
walkable   61.6% of the map, 10.2% is a safe zone
light      256x256, mean [115, 149, 178]         (Lorencia 141,144,144; Noria 167,167,168 -- Devias is cold blue)
objects    2237 placed, 105 kinds
```

- Words used: 0, 1, 3, 4, 5, 8, 12, 13. Nothing in the high byte. `src/content/grid.h`'s
  `passable` works unchanged.
- MU's own sanity tile passes: `MM/Render/Terrain/ZzzLodTerrain.cpp:210-211` asserts
  `TerrainWall[55*256 + 208] == 5` for Devias, and the extracted grid has word 5 at column 208,
  row 55 (0 at the transposed 55,208). So the axis order is right.
- Safe zone, 6701 tiles in four pieces: **the town, 6189 tiles, x 169-255, y 0-78**, which
  holds gate 22 and every NPC; and 326 tiles at x 8-29, y 19-34, 151 at x 154-169, y 241-252,
  and 35 at x 7-13, y 75-79. Those three are **not safe in `075_Terrain3.att`** (512 tiles are
  safe only in the client; 12 only in 075). Blocking differs on 1424 tiles, most of them in the
  same three corners and around x 224-255, y 128-160. The bulk of the rest of the 11.4% is
  8 -> 12 (NoGround vs NoGround|NoMove), which blocks the same.
- For comparison: Lorencia's client grid agrees with 075 on 98.2% of tiles (safe 100%), and
  Noria's on 98.1% (safe 100%). Devias is the odd one.
- The **arrival tile** is the middle of gate 22: **207,42** (word 1).
- **Indoors in Devias is not slot 4.** MuMain tests `HeroTile != 3 && HeroTile < 10` for
  "outdoors" everywhere Devias is concerned: snow (`MM/Scenes/MainScene.cpp:81,105,555`), the
  wind loop (`MM/Scenes/SceneManager.cpp:862-866`), the snow footstep
  (`MM/Engine/Object/ZzzCharacter.cpp:5356`). So Devias's floor is slot 3 (TileGround02, 1.1%)
  and slots 10-13 (TileRock04-07, 1.0%). And an NPC standing on slot 3 can be talked to only
  from slot 3 or 11 (`MM/Input/Selection.cpp:192-198`). There is no Devias roof fade
  (`ZzzObject.cpp:3689` is `WorldActive == 0` only).

### 1.5 Monsters

Spawns, `OM/Version075/Maps/Devias.cs:61-69`, `CreateMonsterSpawn(id, def, x1, x2, y1, y2, count)`
(argument order per `OM/BaseMapInitializer.cs:240`):

| id | breed | # | box x | box y | count | open tiles in box (client grid) |
|---:|---|---:|---|---|---:|---|
| 100 | Elite Yeti | 20 | 194 | 165 | 10 | a single tile, word 0 |
| 101 | Elite Yeti | 20 | 36 | 25 | 10 | a single tile, word 0 |
| 102 | Elite Yeti | 20 | 210-242 | 210-220 | 15 | 363/363 |
| 103 | Elite Yeti | 20 | 0-251 | 128-245 | 200 | 20471 of 29736, 77 safe |
| 104 | Ice Queen | 25 | 0-128 | 128-245 | 75 | 11125 |
| 105 | Hommerd | 23 | 0-128 | 0-128 | 75 | 10365, 361 safe |
| 106 | Ice Monster | 22 | 0-128 | 0-128 | 75 | 10365, 361 safe |
| 107 | Worm | 24 | 128-251 | 0-128 | 65 | 6646, **5960 safe** (the town) |
| 108 | Assassin | 21 | 128-251 | 0-128 | 35 | 6646, 5960 safe |

**560 monsters**: 235 Elite Yeti, 75 each of Ice Queen, Hommerd and Ice Monster, 65 Worm, 35
Assassin. 0.95d and Season 6 inherit this table unchanged (`OM/Version095d/Maps/Devias.cs:12`,
`OM/VersionSeasonSix/Maps/Devias.cs:12`), so there is nothing to cross-check it against in this
tree. No MonsterSetBase is on disk. Two things look like transcription and are worth a word
with the user before they go in as fact:

- **100 and 101 are one-tile boxes of ten.** MonsterSetBase's spot rows carry a scatter
  distance that OpenMU's regex drops (`OM/BaseMapInitializer.cs:190-192`), so ten Elite Yetis
  come up on one tile. The realm will spread them as they walk; it is still not what MU did.
- **75 Ice Queens** at level 52 with 4000 HP, in the same half of the map as 200 Elite Yetis
  (level 36). That is a boss population. It is OpenMU's number; I have not seen it confirmed.

Breeds, `OM/Version075/Maps/Devias.cs:73-285`:

| # | name | lvl | HP | dmg | def | move/atk/view | move/atk ms | respawn | atk rate / def rate | skill | MuMain model, scale |
|---:|---|---:|---:|---|---:|---|---|---:|---|---|---|
| 19 | Yeti | 30 | 900 | 105-110 | 37 | 2/4/6 | 400/2000 | **3 s** | 150/37 | EnergyBall (17) | Monster13, 1.1 |
| 20 | Elite Yeti | 36 | 1200 | 120-125 | 50 | 3/1/6 | 400/1400 | **3 s** | 180/43 | -- | Monster14, 1.4 |
| 21 | Assassin | 26 | 800 | 95-100 | 33 | 2/1/7 | 400/2000 | 10 s | 130/33 | -- | Monster15, 0.95 |
| 22 | Ice Monster | 22 | 650 | 80-85 | 27 | 2/1/5 | 400/2000 | 10 s | 110/27 | Ice (7) | Monster16, 1.0; BlendMesh 0 |
| 23 | Hommerd | 24 | 700 | 85-90 | 29 | 3/1/5 | 400/1600 | 10 s | 120/29 | -- | Monster17, 1.15; Larkan Axe + Big Round Shield |
| 24 | Worm | 20 | 600 | 75-80 | 25 | 3/1/4 | 400/1600 | 10 s | 100/25 | -- | Monster18, 1.0 |
| 25 | Ice Queen | 52 | 4000 | 155-165 | 90 | 3/4/7 | 400/1400 | **50 s** | 260/76 | PowerWave (11) | Monster19, 1.1; Angelic Staff, BlendMesh 2, unlit, Level 3 |

MuMain's arms: `MM/Engine/Object/ZzzCharacter.cpp:13997-14045`; the file is
`Monster{Type+1}.bmd` (`MM/Engine/Object/ZzzOpenData.cpp:2561`, `_enum.h:4162-4168`), all seven
on disk in `Data/Monster`. Skill numbers are `OM/Skills/SkillNumber.cs:17-32`. The respawns
are the reason `tools/cook.py:1681`'s `RESPAWN_VERSION075` exists: mu.db's flattening to 10
would make the Elite Yetis refill three times slower and the Ice Queen five times faster.

**The Yeti (19) is defined here and spawned nowhere in Version075** -- no map file names
`NpcDictionary[19]`. It is a breed row with no nest.

### 1.6 What in the client is not 0.75

Season 6's `Gates.cs` puts gates exactly where the client's map disagrees with `075_Terrain3.att`:

| corner | client says | Season 6 gate there | placements there |
|---|---|---|---|
| x 8-29, y 19-34 | 326 safe tiles | 72 "Devias2" warp target 23,24-27,27 (`OM/VersionSeasonSix/Gates.cs:51,158`) | types 105-109 (`cho_wedding_*` sheets), 20 Object100, guards |
| x 154-169, y 241-252 | 151 safe tiles | 262 exit 161,241, enter 259 at 161,245 (`:164,589`) | houses (Object15-17, 86) |
| x 36-58, y 88-97 | blocking differs | 289 exit 52,88, enter 286 at 52,92 level 240 (`:161,601`) | types 110-111 (`devgate*` sheets) |
| x 7-13, y 75-79 | 35 safe tiles | -- | types 101-103: a snow machine whose sheets are Item swords not in Object3, a drum, two sled dogs, a brazier |

And in code, `MM/World/MapInfra/MapManager.cpp:60-86` stands Serbis's donkey and flag at 191,16
and 191,17 (a Season NPC, `MONSTER_LAHAP`, `ZzzCharacter.cpp:14426`) and a **warp at tile 53,92**
(`MODEL_WARP` + 50/+20 units) whose effects `ZzzObject.cpp:4670-4681` stacks 360 up -- that is
Season 6's level-240 gate 286. None of it is Version075. Recommendation: build the 0.75 town
and fields; hold types 101-111 (52 placements), the donkey, the flag and the warp back until the
user rules. The three extra safe corners go in or out with them. That means either
correcting the grid from `075_Terrain3.att`, or accepting the client's grid.

### 1.7 NPCs

`OM/Version075/Maps/Devias.cs:46-54`. The facing is OpenMU's `Direction` number, which is
also MU2's Look (`tools/cook.py:1698-1700`; Noria's rows use it unchanged):

| NPC | name | tile | facing | window / store | MuMain model (`ZzzCharacter.cpp:14416-14468`, `ZzzOpenData.cpp:1874-1895`) |
|---:|---|---|---|---|---|
| 244 | Caren the Barmaid | 226,25 | SouthEast 4 | Merchant: Ale, Town Portal Scroll (`OM/Version075/MerchantStores.cs:424-435`, same as Lumen's) | `Npc/SnowMerchant01.bmd` |
| 245 | Izabel The Wizard | 225,41 | SouthEast 4 | Merchant: apples, potions x1/x3, antidote, Legendary set +3+L, TP scroll, bolts, arrows, Scroll of Flame, Scroll of Twister, Gordon/Legendary Staff, Legendary Shield (`:270-322`) | `Npc/SnowWizard01.bmd` |
| 246 | Zienna The Weapons Merchant | 186,47 | SouthEast 4 | Merchant: Dragon set +3+L, Salamander/Legendary/Double Blade/Lightning/Giant/Heliacal swords, Serpent Crossbow, Bill of Balrog, Great Scythe, Silver Bow, Bluewing Crossbow (`:365-400`); **also repairs** (`src/sim/wear.h:64-65`) | `Npc/SnowSmith01.bmd` |
| 247 | Crossbow Guard | 224,79 | NorthEast 6 | guard | player body in plate + Light Crossbow (`CrossbowGuard` is cooked for Lorencia) |
| 247 | Crossbow Guard | 219,79 | NorthEast 6 | guard | " |
| 247 | Crossbow Guard | 169,45 | NorthWest 8 | guard | " |
| 247 | Crossbow Guard | 169,39 | NorthWest 8 | guard | " |
| 241 | Guild Master | 215,45 | SouthWest 2 | GuildMaster (nothing in single player) | `Npc/Master01.bmd` |
| 240 | Baz The Vault Keeper | 218,63 | South 3 | VaultStorage | `Npc/Storage01.bmd` (source/npc/Storage01 exists) |

Devias's guards speak (text 904/905, `ZzzCharacter.cpp:3441-3446`). Later versions add
Priest Sevina 235 at 183,32 (0.95d, `OM/Version095d/Maps/Devias.cs:35`) and in Season 6 a dozen
more, among them **Priest Devin 406 at 181,35 SouthEast** (`OM/VersionSeasonSix/Maps/Devias.cs:36`;
model `Npc/devin.bmd`, `ZzzCharacter.cpp:14674-14677`, `ZzzOpenData.cpp:2044-2046`). The
quests already point players at him: `src/sim/quests.cpp:74-82,157-162`, "Seek Apostle Devin in
Devias". He would be "ours, not Version075's", as Marlon and Peia are, and needs the user's
nod and a position.

The ninth NPC tile check: Guild Master 215,45 stands inside gate 22's box. That is harmless,
because the realm moves an arrival to the nearest free tile.

---

## 2. What mu.db has of Devias: nothing

`sqlite3 -readonly source/mu.db` (the WAL is read too, as it is live):

- `gates`: rows 17 (map 0) and 27 (map 3) only. **No gate 22.**
- `monster_kinds`: 0-4, 6, 7, 14, 26-33. **None of 19-25.**
- `monster_spawns`: map 0 (9 boxes, 290) and map 3 (8 boxes, 1005). **No map 2.**
- `npc_spawns`: Lorencia's 14 only. Noria's NPCs never went into mu.db either. They are
  `tools/cook.py:1708` `FOLK_VERSION075[3]` and `source/world/noria/placements.json` `spawns`,
  and that is the pattern Devias should follow.
- `items`: names and drop levels only. Shops are `src/sim/market.cpp`.

Rows to add (`index.py` reads `gates WHERE spawn = 1`, `index.py:1720-1745`, and
`monster_kinds`/`monster_spawns`, `:1625-1715`):

```sql
insert into gates (id, map, x1, y1, x2, y2, spawn) values (22, 2, 197, 35, 218, 50, 1);
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (19,'Yeti',30,900,105,110,37,2,4,6,400,2000,150,37,3,17),
 (20,'Elite Yeti',36,1200,120,125,50,3,1,6,400,1400,180,43,3,NULL),
 (21,'Assassin',26,800,95,100,33,2,1,7,400,2000,130,33,10,NULL),
 (22,'Ice Monster',22,650,80,85,27,2,1,5,400,2000,110,27,10,7),
 (23,'Hommerd',24,700,85,90,29,3,1,5,400,1600,120,29,10,NULL),
 (24,'Worm',20,600,75,80,25,3,1,4,400,1600,100,25,10,NULL),
 (25,'Ice Queen',52,4000,155,165,90,3,4,7,400,1400,260,76,50,11);
insert into monster_spawns (id, number, x1, x2, y1, y2, count, map) values
 (200,20,194,194,165,165,10,2),(201,20,36,36,25,25,10,2),(202,20,210,242,210,220,15,2),
 (203,20,0,251,128,245,200,2),(204,25,0,128,128,245,75,2),(205,23,0,128,0,128,75,2),
 (206,22,0,128,0,128,75,2),(207,24,128,251,0,128,65,2),(208,21,128,251,0,128,35,2);
-- then: sqlite3 source/mu.db "pragma wal_checkpoint(TRUNCATE)" before committing
```

(Spawn ids: check `select max(id) from monster_spawns` first. The existing rows' ids are not
OpenMU's.) Writing the true respawns into the new rows makes a `RESPAWN_VERSION075` entry
unnecessary for them. If the house rule is "mu.db says 10", add `19: 3, 20: 3, 25: 50` to
`tools/cook.py:1681` instead. Elemental resistances (`IceResistance` etc.) have no column.

Nothing the sim does today reads `attack_skill` other than 2: `src/game/play.cpp:733-738`
special-cases the Lich's meteor. The Yeti's Energy Ball, the Ice Monster's Ice and the Ice
Queen's Power Wave are showing work (the effects page).

---

## 3. The object inventory

**2237 placements over 105 kinds**, read by `pipeline/terrain.py` into scratch
`devias/devias.json`. The reader runs as-is on World3: `EncTerrain3.obj` decrypts with the map
cipher, 30-byte records (`terrain.py:322-367`). Lorencia is 2870/109 and Noria 9399/44. Devias
is a small, built-up town: 58% of placements are eight kinds --
Object01 451, Object12 288, Object15 184, Object20 136, Object18 87, Object16 61, Object05 52,
Object92 44.

**Naming.** World3 is not Lorencia, so `model_for` gives `Object{type+1:02d}`
(`terrain.py:201-218`), which is MuMain's own rule. `MapManager.cpp:1121-1123` loads
`Data\Object{WorldActive+1}\Object{i+1}.bmd` for every slot, and Devias has no special loader.
The only extra models Devias loads are the ones in §1.6 (`Npc/obj_donkey`, `obj_flag`, `warp01-03`).

**Object3 holds 120 .bmd**: Object01 to Object118, plus `object119.bmd` and `object120.bmd`
in lower case (unplaced). **Every placed type has a .bmd**, so terrain.py prints no `missing`.
But four files are **42-byte stubs** (`BMD` + zeros; MuExtract says "Unknown BMD version 0"):
Object25, Object27, Object33, Object90, the slots of types 24, 26, 32 and 89, none of which is
placed. Also unplaced, and real: Object29, 61, 105 and 113-120 (giftboxes and han_* event
pieces, presumably the disabled Christmas arms).

**Sheets.** 93 image files. Every sheet a placed model names is in Object3 except
**Object102's** nine (hound_*, shield05, sword03/06/11/14), which are Item art, and Object105's
sword03, which is unplaced. `han_ctreeax2`, `han_ctreeax4` and `han_xtree` are named by no model.

**Animated:** 12 placed kinds, 702 placements, have more than one key: Object01 (the snowy tree,
451, 30 keys), Object20 (aurora, 136, 30), Object76 (3, 30), Object84/85/86 (banners, 66,
25-26), Object10/11/36 (18, 20-21), Object55/57 (candles, 20, 7), Object104 (sled dogs, 2 actions).
Per `porting-a-world.md` §4c they play at `CreateObject`'s 0.16; Devias's arm sets no velocity.

### 3.1 What MuMain does per type for WD_2DEVIAS

These are the tables `pipeline/terrain.py` and `tools/cook.py` carry per map. The visuals in
detail are the effects page's.

- **Hidden** (`HiddenMesh = -2`): type **91** (Object92, 44 placements; also `CreateOperate`, a
  40x40x160 lean box, `ZzzObject.cpp:4687-4690`) and type **100** (Object101, 1 placement at
  tile 3.3,247.6, `:4712-4714`). Type 100 is the Lost Tower door's marker: `RenderObjectVisual`
  hangs two counter-rotating `BITMAP_LIGHTNING+1` sprites 150 up on bone 0 (`:2821-2828`), and
  the render loop draws it only when the hero is level 50, or 33 for DK/DL/RF (`:3446-3460`,
  again at `:3637`). -> `HIDDEN_BY_MAP[2] = {91, 100}`.
- **Operable** (`CreateOperate`, `:4682-4690`), with the pose from MOVEMENT_OPERATE
  (`MM/Engine/Object/ZzzInterface.cpp:1713-1726`) and the lean cursor on 91 only (`:4054`):
  91 pose + turn; 22, 25, 40, 55 sit + turn; 45, 73 sit. -> `OPERABLE_BY_MAP[2] = {22, 25, 40,
  45, 55, 73, 91}` and `PERCHES[2]` (§5).
- **Additive** (`BlendMesh`, `:4691-4702`): 19 -> mesh 0, 92 -> 0, 93 -> 0, 54 -> 1, 56 -> 1,
  78 -> 3. `MoveObject` gives 78 a random `BlendMeshLight` of 0.4-0.7 each frame (`:3902-3904`),
  so its window flickers. -> `BLEND_MESH_BY_MAP[2] = {19: 0, 92: 0, 93: 0, 54: 1, 56: 1, 78: 3}`.
- **Doors** (`:4703-4711`, `MoveObject :3918-3963`): 20, 65, 88 swing open when the hero comes
  within 200 units (`SOUND_DOOR01`), and 86 slides (`SOUND_DOOR02`). This is behaviour, not
  art: the first world whose objects move for the player.
- **Emitters** (`MoveObject :3905-3917`): type 30 throws `BITMAP_TRUE_FIRE` + `BITMAP_SMOKE`
  160 up, and type 66 `CreateFire(0)` 50 up. That makes 9 braziers, Lorencia's anchor-light kind 1.
- **Event-only** (`#ifdef`, off in this client, `Winmain.h:46`): 105, 106, 110.
- **Sled dog** 103: a random action (`:2829-2835`).

The full per-kind table (placements, meshes, triangles, bones, keys, sheets, and what MuMain
does with it) is in [Appendix A](#appendix-a-every-object3-model).

---

## 4. Name collisions

### 4.1 Model names: world-qualified, and it holds, with two leaks

Devias places **38 names that Noria has a recipe for** (every Object01-43 Devias uses) and
**16 that the character scene has** (Object06, 15, 30, 35, 39, 50, 65, 80, 97, 98, 99, 103, 104,
107, 111, 112). Four (06, 15, 30, 35) are in all three worlds.

What holds:

- `pipeline/index.py:2293-2294` writes `"world": <folder>` on every scenery entry, and
  `built()` resolves a name against the asset's own folder first (`index.py:739-767`).
- `tools/cook.py` reads a world's models from `assets/world/<world>/<model>/<model>.glb`
  (`boid_glb`, `cook.py:205-210`; `collect :224-240`) and writes `cooked/<world>/meshes`, so
  two worlds' `Object06` never meet in the cook. `carried` (lights and glows) is world-gated
  (`cook.py:1288-1290`). Texture stems carry a content digest (`cook.py:262`).
- `tools/asset.sh` takes `devias/Object06` (`asset.sh:37-42,94-97`), and `content.sh` names a
  world recipe `<world>/<stem>` (`content.sh`, the `world/*` case).

What leaks, both keyed on bare names or type numbers with no world:

1. **`src/game/world/ornaments.cpp:101-106,204`**: `kNoriaGlows` is matched by `name ==
   glow.model` for every world. The anchor fails harmlessly when the bone is out of range
   (Devias's Object02, 18 and 40 have 1, 1 and 4 bones). But **Devias's Object10 (3 bones,
   6 placements) and Object36 (5 bones, 6 placements) would each sprout Noria's blue light**
   on bone 1 and bone 3. Gate the table on the world.
2. **`tools/cook.py:1082,1392`**: `GROUNDED_TYPES = range(20, 28)` is "Lorencia's grass and
   fern block" (`:1074`). It is applied by **type number in every world**, and it drops pitch
   and roll and puts the model at terrain height. In Devias, types 20-27 are the doors
   (Object21), the benches (Object23, 22 placements), desks (Object24), benches (Object26) and
   a notice board (Object28). Indoor furniture set down on the terrain under a raised floor
   would sink. Key it by world: `{"lorencia": range(20, 28)}`. (Noria's types 20-27 are
   grounded today as well; worth a look.)

Also by name, and fine for Devias: `roofs` (`cook.py:1281`) is only HouseWall05/06;
`ANCHOR_KINDS` (`:1098`) is Light01-03; `sway.cpp:22`, `lamps.cpp:107,227` and
`ornaments.cpp:169-190` name Lorencia-only models.

### 4.2 Textures: prefix `dv_`

`source/textures` is one flat pool of 426 files. **39 of Object3's sheet names already exist
there bare**: apple, badge_01-03, bookshelf, boots_11, bottle, candle, candle2, chair2,
desk_big, drum, fire_01, fire_02, gloves_11, gold, guard_hair, head_11, hide, javelins03,
light, light2, light3, light_02, lower_11, plate, plate2, pot, pot2, pot3, shield02, shield03,
sword04, sword08, tile_02, treasure_chest, tree_03, upper_11, winecup. Hashed against every
other `Data/` folder, some are byte-identical to Object1's or Item's (apple, bottle, candle, the
lights, pot*, winecup), but **badge_01-03, bookshelf, chair2, desk_big, drum, fire_01/02, gold,
plate, plate2, tree_03 and treasure_chest differ** from Object1's. The guard's `*_11` parts
match Object10's and differ from Player's. So decode **all** of Object3 under `dv_`, as Noria
did with `nr_` (`source/world/noria/Object05.json` `sheet_from`). The ground page reaches the
same prefix for World3's tiles, where nine of the fourteen collide (`devias-ground.md` §2).
Nothing in the pool starts with `dv_` today.

---

## 5. Every place MU2_BGFX must learn a third world

Grepped `src/`, `tools/`, `pipeline/`, `sheets/`, `tests/` for noria, lorencia, map numbers and
world lists. Generic already, needing nothing: `pipeline/index.py` `worlds()` (globs
`source/world/*/*.json`, number = `map_number - 1`, sorts by number, `:1356-1472`),
`tools/content.sh` (every `source/world/*/ground.json`), `tools/sync.sh --world`,
`tools/cook.py --world` (all per-world paths), `mapSheet` (`maps.cpp:61-64`), the per-world
figure table (`cook.py:1531-1535`, `figures_devias.json`), the held-back breeds
(`src/game/play_open.cpp:100-126`), `zoneLevels`, and the gate walk in the realm
(`src/sim/realm_move.cpp:185-212`).

**Needs Devias added:**

| where | what is there | Devias needs |
|---|---|---|
| `pipeline/terrain.py:117-120` `HIDDEN_BY_MAP` | 0, 3 | `2: {91, 100}` (before the extraction, so the json carries `hidden`) |
| `pipeline/terrain.py:133-135` `BLEND_MESH_BY_MAP` | 3 | `2: {19: 0, 92: 0, 93: 0, 54: 1, 56: 1, 78: 3}` |
| `pipeline/terrain.py:141-144` and `pipeline/index.py:43-46` `OPERABLE_BY_MAP` | 0, 3 | `2: {22, 25, 40, 45, 55, 73, 91}` in both |
| `pipeline/index.py:537-542` grass sheets | `grass_lorencia_0-2`, `grass_noria_0,2` | `grass_devias_1` only: World3 ships TileGrass02.OZT and no 01/03 OZT (`MapManager.cpp:1461-1472`); the ground page's §4 |
| `tools/cook.py:1082,1392` `GROUNDED_TYPES` | every world | key by world (§4.1) |
| `tools/cook.py:1681` `RESPAWN_VERSION075` | 0, 2 | 19: 3, 20: 3, 25: 50, unless mu.db carries them (§2) |
| `tools/cook.py:1708` `FOLK_VERSION075` | 0, 3 | `2: [...]`, the nine of §1.7 |
| `tools/cook.py:1751` `PERCHES` | 0, 3 | `2: {91: (3, True, True, True), 22: (2, True, False, False), 25: (2, True, False, False), 40: (2, True, False, False), 55: (2, True, False, False), 45: (2, False, False, False), 73: (2, False, False, False)}` |
| `tools/cook.py:200-203` `AIRS` | lorencia, noria | nothing: Devias flies no boid (`MM/Engine/AI/GOBoid.cpp:1304-1312` omits it) |
| `source/world/devias/placements.json` | Noria's `spawns` | the NPC figures: SnowMerchant01, SnowWizard01, SnowSmith01, Master01, Storage01, CrossbowGuard x4 |
| `source/npc/` | Storage01, CrossbowGuard ... | new recipes SnowMerchant01, SnowSmith01, SnowWizard01, Master01 (+ devin if wanted) |
| `source/mu.db` | §2 | gate 22, kinds 19-25, 9 spawn boxes |
| `src/game/world/maps.cpp:12-20` `kMaps` | lorencia, noria | `{"devias", 2, {207, 42}}` **appended last**, so M still goes Lorencia -> Noria and then Noria -> Devias -> Lorencia (`mapAfter`, `:38-44`); update the comment and `maps.h:20` |
| `src/sim/gates.cpp:9-20` | 23/24/25/26 | exits `{19, 2, {242,34,243,37}, -1, +1}` and `{21, 0, {7,38,8,41}, +1, -1}`; enters `{18, 0, {5,38,6,41}, 15, 19}` and `{20, 2, {244,34,245,37}, 0, 21}`. Leave 28/43/44 out until Lost Tower exists, or the realm will gate him and PlayMode will log "leads to a map this game has no world for" (`play_mode.cpp:537`) with him already stopped; update `gates.h:11-13` |
| `src/game/ui/minimap.cpp:214-218` `mapName` | 0, 3 | `case 2: return "Devias";` (it names the far side of a gate) |
| `src/game/ui/arrival.cpp:60` | has `devias` already | -- |
| `src/game/world/world.cpp:46,297-302` `kIndoorFloor = 4` | one number for all worlds | per world: Devias "indoors" is `tile == 3 \|\| tile >= 10` (§1.4). Also feeds `setRoofsHidden` (`:255`), the leaves/rain `inside` and the wind |
| `src/game/play_sound.cpp:81,233` | grass on slot 0, wind off indoors | Devias walks on snow outdoors (`pWalk(Snow).wav`, `ZzzOpenData.cpp:4741`; `ZzzCharacter.cpp:5356`); wind loops except indoors |
| `src/game/world/weather.cpp:102-104` | rain in noria/lorencia | Devias: no rain; its snow is the leaves pool (`CreateDeviasSnow`, `ZzzEffectFireLeave.cpp:274-297`) -- effects page |
| `src/game/world/leaves.*` | Lorencia's and Noria's creators | Devias's snow creator -- effects page |
| `src/game/world/ornaments.cpp:101-106,204` | Noria glows by bare name | gate on world (§4.1); Devias's own sprites (type 100 lightning, the braziers) -- effects page |
| `src/game/world/portal.cpp:40` | Noria's warp only | nothing for 0.75 (the 53,92 warp is Season 6's, §1.6) |
| `src/game/world/boids.cpp:55-72` | lorencia, noria | nothing (no boid) |
| `src/sim/market.cpp:161-172` `stockOf` | 248/250/251/253/254/255/242/243 | 244 -> the barmaid's table (`kBarmaid`), 245 Izabel, 246 Zienna -- each offer must be a cooked item (`tests/sim_test.cpp:417-431`'s rule) |
| `src/sim/wear.cpp:44` `repairsAt` | 251, 243 | + 246 (Zienna) |
| `src/app/modes/play_mode.cpp:738,759-760` | pub/hunt music by name | nothing now (music off; `SceneManager.cpp:1007-1025` has Devias.mp3 in the safe zone and Church.mp3 at 205-214,13-31, both on disk in `Data/Music`) |
| `src/game/roster.cpp:172` | home by class | nothing: no 0.75 class starts in Devias |
| `src/sim/quests.cpp:74-82,157-162` | "Seek Apostle Devin in Devias" | a Devias quest giver when the user wants one (§1.7) |
| `sheets/worlds/devias.json` (+ `devias_rain.json` not needed) | noria.json | Devias's light. MU clears to (0.75, 0.85, 1.0) "light snowy blue" (`SceneManager.cpp:383-384`), and the TerrainLight mean is 115,149,178; seed from Lorencia's base and tune with the user |
| `tests/sim_test.cpp:3269-3320` | Lorencia <-> Noria gate test | the same for 18/19 and 20/21, and the level-15 refusal |
| `docs/roadmap.md:127`, `PLAN.md:179` | "Devias" as a later item | the sprint line |

`src/game/headless.cpp:166` starts any non-Lorencia world on its `arrive`, so `--headless
--world devias` works once the row exists.

---

## 6. The plan, mirroring Noria

Noria took the path "extract the whole record, stand the bare land up with M travel, then
objects one by one, then NPCs and monsters breed by breed, then effects"
(`porting-a-world.md`, commit `9a2f6110`). Devias the same, in MU2_BGFX's own content chain
(`docs/content.md`). `$D` is `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/bin/Data`,
and everything runs from `MU2_BGFX/`.

**Stage 0 -- source.** Done in this research: World3 has `EncTerrain3.{map,att,obj}`,
`TerrainHeight.OZB`, `TerrainLight.OZJ`, 14 tile sheets, `TileGrass02.OZT`, `leaf01/02` (the
snow); Object3 has 120 .bmd (4 stubs) and 93 sheets. The only models from elsewhere are the NPCs
in `Data/Npc`, the monsters in `Data/Monster` and their weapons in `Data/Item`.

**Stage 1 -- grids and the record** (sprint 1):

```sh
# first: HIDDEN_BY_MAP[2], BLEND_MESH_BY_MAP[2], OPERABLE_BY_MAP[2] in pipeline/terrain.py (+ index.py)
python3 pipeline/terrain.py $D/World3 devias source/world 3
# expect: 0.00-3.78 m, 14 slots, 61.6% walkable, 10.2% safe, 2237 placed / 105 kinds, no missing
# decide the §1.6 corners; if 0.75's grid wins, rewrite attributes.png's safe bit from 075_Terrain3.att
sqlite3 source/mu.db < devias.sql            # §2's rows; then pragma wal_checkpoint(TRUNCATE)
```

**Stage 2 -- tile sheets.** The ground page's checklist (`devias-ground.md` §6): 14 sheets
decoded as `source/textures/dv_*.png`, `source/world/devias/ground.json`, and the grass sheet
`effects/grass/devias_TileGrass02.png` plus `grass_devias_1` in `index.py`.

**Stage 3 -- the ground, standing and travellable. This is the first sprint's finish line.**

```sh
./tools/content.sh --world devias            # tileset.py + ground.py + index.py
./tools/sync.sh --world devias --only-world
./tools/sync.sh                              # index.json with the devias world entry and gates.safe
./tools/cook.py --world devias --only ground # the land's sheets alone, as Noria's first day
./tools/cook.py --world devias --only tables # devias.mur: grid, safe gate 22, spawns, folk, perches
```

Code in the same sprint: the `kMaps` row, the two gate pairs, `mapName`, the per-world indoor
test (without it, snow and wind decisions are wrong, but nothing breaks), the
`ornaments.cpp` and `GROUNDED_TYPES` world gates, and a sim test for 18 -> 19 and 20 -> 21. Then:

```sh
./build.sh
./run.sh --headless --world devias --ticks 30000 --level 30   # the sim alone raises all 560 at 207,42;
                                                              # the audit must count none in the safe zone
./run.sh --world devias --play --fresh --frames 300 --shot 250  # a --frames review: read mu2.log + shots/
# its log should say "play: 560 monsters of devias held back, their figures not cooked"
./run.sh --world lorencia --play --fresh --level 20 --travel-at 60 --frames 400   # M: lorencia -> noria (unchanged)
./run.sh --world noria --play --fresh --travel-at 60 --frames 400                 # M: noria -> devias
```

Walking gate 18 at Lorencia 5,38 as a level-15 hero, and gate 20 back, is the proof the pair is
right. (Per the house rules, no window is opened in a research or QA pass. The runs above are
the user's or a `--frames` review read through `mu2.log`, and a window run in a session needs
the user's say-so.) Done when: the bare snow land stands under its own sheet, M and both gates
walk to it and back, the save resumes on it, and no monster stands invisible.

**Stage 4 -- objects.** One recipe per kind under `source/world/devias/ObjectNN.json` with
`dv_` sheets, in worklist order. Object01 (451), Object12 (288), Object15 (184) and Object20
(136) are half the map:

```sh
dotnet run --project tools/MuExtract -- export-obj $D/Object3/Object01.bmd source/world/devias/Object01.obj
dotnet run --project tools/MuExtract -- export-rig $D/Object3/Object01.bmd source/world/devias/Object01.rig.json
python3 pipeline/decode_texture.py $D/Object3/stree.OZJ source/textures/dv_stree.png   # and stree.OZT
./tools/asset.sh devias/Object01 --build-only && python3 pipeline/index.py source workshop
./tools/sync_one.py ... && ./tools/cook_one.py Object01 --world devias                 # judge in world
./tools/cook.py --world devias --only textures && ./tools/cook.py --world devias --only meshes \
  && ./tools/cook.py --world devias --only placements
```

(`dotnet bin/Debug/net10.0/muextract.dll` from `tools/MuExtract` is ten times faster than
`dotnet run` for a batch.) Skip the hidden 91 and 100, and the unplaced and stub slots. The
Season corners (types 101-111) wait on the ruling.

**Stage 5 -- NPCs and monsters.** The four new NPC recipes and `placements.json` spawns,
`FOLK_VERSION075[2]`, `PERCHES[2]` and the three shops. Then the breeds one at a time, each
judged before the next (`cook.py --only figures --world devias --monsters EliteYeti01`, the
Noria rule). Worm and Assassin (level 20 and 26, by the town) come first, and the Ice Queen last.
Hommerd needs the Larkan Axe and the Big Round Shield. The Ice Queen needs the Angelic Staff.

**Stage 6 -- effects.** The effects page: the snow (`CreateDeviasSnow`, alpha-blended leaf
sprites, none indoors), the aurora and ice additives, the flickering windows, 9 braziers, the
type-30 fire, the Lost Tower door's lightning, the doors swinging open, the ambient wind, the
snow footsteps, the monster skills, and the grade.

---

## Appendix A: every Object3 model

Type = placement type = model index; model = `Object{type+1}`. Meshes, triangles, bones and keys are
MuExtract's (`export-obj` at action 0 key 0; `export-rig`). "keys" is the longest action.

| type | model | placed | meshes | tris | bones | keys | sheets | MuMain (WD_2DEVIAS) |
|---:|---|---:|---:|---:|---:|---:|---|---|
| 0 | Object01 | 451 | 2 | 140 | 12 | 30 | stree.jpg, stree.tga |  |
| 1 | Object02 | 8 | 2 | 44 | 1 | 1 | sston.jpg, sston2.jpg |  |
| 2 | Object03 | 4 | 1 | 356 | 1 | 1 | deer.jpg |  |
| 3 | Object04 | 26 | 1 | 60 | 1 | 1 | drum.jpg |  |
| 4 | Object05 | 52 | 1 | 170 | 1 | 1 | ice_block07.jpg |  |
| 5 | Object06 | 10 | 1 | 26 | 2 | 1 | snow_ston.jpg |  |
| 6 | Object07 | 12 | 1 | 17 | 2 | 1 | snow_ston.jpg |  |
| 7 | Object08 | 2 | 1 | 31 | 1 | 1 | ice01.jpg |  |
| 8 | Object09 | 2 | 1 | 19 | 1 | 1 | ice01.jpg |  |
| 9 | Object10 | 6 | 1 | 30 | 3 | 20 | ice01.jpg |  |
| 10 | Object11 | 12 | 1 | 30 | 3 | 21 | ice01.jpg |  |
| 11 | Object12 | 288 | 1 | 170 | 1 | 1 | ice_block01.jpg |  |
| 12 | Object13 | 18 | 3 | 156 | 1 | 1 | bridge01.jpg, snow_ston.jpg, sston.jpg |  |
| 13 | Object14 | 16 | 1 | 66 | 1 | 1 | bridge01.jpg |  |
| 14 | Object15 | 184 | 1 | 50 | 1 | 1 | blockhead.jpg |  |
| 15 | Object16 | 61 | 4 | 34 | 1 | 1 | sston.jpg, titel_a03.jpg, titel_a05.jpg, titel_a06.jpg |  |
| 16 | Object17 | 40 | 4 | 56 | 1 | 1 | sston.jpg, titel_a03.jpg, titel_a04.jpg, titel_a05.jpg |  |
| 17 | Object18 | 87 | 4 | 61 | 1 | 1 | titel_b03.jpg, titel_b04.jpg, titel_b05.jpg, titel_b06.jpg |  |
| 18 | Object19 | 39 | 4 | 64 | 1 | 1 | titel_b01.jpg, titel_b03.jpg, titel_b04.jpg, titel_b06.jpg |  |
| 19 | Object20 | 136 | 1 | 12 | 7 | 30 | aurora.jpg | additive mesh 0 |
| 20 | Object21 | 6 | 2 | 192 | 6 | 1 | wall_door01.jpg, wall_door02.jpg | door: swings open within 200 units, SOUND_DOOR01 |
| 21 | Object22 | 1 | 1 | 73 | 1 | 1 | ti06.jpg |  |
| 22 | Object23 | 22 | 1 | 94 | 1 | 1 | ti03.jpg | CreateOperate sit+turn |
| 23 | Object24 | 6 | 1 | 34 | 1 | 1 | desk_big.jpg |  |
| 24 | Object25 | 0 | - | - | - | - |  | **42-byte stub .bmd**; slot empty in MU |
| 25 | Object26 | 15 | 1 | 72 | 1 | 1 | ti03.jpg | CreateOperate sit+turn |
| 26 | Object27 | 0 | - | - | - | - |  | **42-byte stub .bmd**; slot empty in MU |
| 27 | Object28 | 3 | 1 | 38 | 1 | 1 | snotice.jpg |  |
| 28 | Object29 | 0 | 1 | 66 | 1 | 1 | gold.jpg | unplaced |
| 29 | Object30 | 6 | 1 | 60 | 1 | 1 | drums.jpg |  |
| 30 | Object31 | 2 | 3 | 48 | 1 | 1 | light.tga, light2.jpg, light3.jpg | BITMAP_TRUE_FIRE + BITMAP_SMOKE at +160 every frame |
| 31 | Object32 | 1 | 1 | 78 | 1 | 1 | pot.jpg |  |
| 32 | Object33 | 0 | - | - | - | - |  | **42-byte stub .bmd**; slot empty in MU |
| 33 | Object34 | 4 | 5 | 34 | 1 | 1 | sston.jpg, ti01.jpg, titel_a03.jpg, titel_a05.jpg, titel_a06.jpg |  |
| 34 | Object35 | 8 | 5 | 56 | 1 | 1 | sston.jpg, ti01.jpg, titel_a03.jpg, titel_a04.jpg, titel_a05.jpg |  |
| 35 | Object36 | 6 | 3 | 32 | 5 | 20 | sdoorknob.tga, snotice.jpg, ssignboard.tga |  |
| 36 | Object37 | 3 | 3 | 86 | 2 | 1 | fireplace.tga, fireplace_down.tga, house_stone_out.jpg |  |
| 37 | Object38 | 15 | 2 | 110 | 3 | 1 | shield03.jpg, sword04.jpg |  |
| 38 | Object39 | 14 | 2 | 117 | 1 | 1 | shield02.jpg, sword08.jpg |  |
| 39 | Object40 | 1 | 1 | 66 | 4 | 1 | treasure_chest.jpg |  |
| 40 | Object41 | 13 | 2 | 54 | 1 | 1 | chair.jpg, chair2.tga | CreateOperate sit+turn |
| 41 | Object42 | 3 | 1 | 42 | 1 | 1 | desk_big.jpg |  |
| 42 | Object43 | 5 | 2 | 26 | 1 | 1 | snotice.jpg, snotice03.jpg |  |
| 43 | Object44 | 1 | 1 | 36 | 1 | 1 | bottle.tga |  |
| 44 | Object45 | 4 | 1 | 36 | 1 | 1 | winecup.jpg |  |
| 45 | Object46 | 1 | 1 | 42 | 1 | 1 | chair.jpg | CreateOperate sit |
| 46 | Object47 | 2 | 1 | 38 | 1 | 1 | desk_big.jpg |  |
| 47 | Object48 | 1 | 5 | 216 | 5 | 1 | bottle.tga, plate.jpg, plate2.jpg, pot3.tga, winecup.jpg |  |
| 48 | Object49 | 1 | 5 | 232 | 6 | 1 | apple.jpg, bottle.tga, plate.jpg, pot3.tga, winecup.jpg |  |
| 49 | Object50 | 2 | 2 | 108 | 3 | 1 | bottle.tga, winecup.jpg |  |
| 50 | Object51 | 2 | 3 | 500 | 13 | 1 | bookshelf.jpg, bottle.tga, pot2.jpg |  |
| 51 | Object52 | 2 | 2 | 464 | 13 | 1 | bookshelf.jpg, bottle.tga |  |
| 52 | Object53 | 1 | 4 | 272 | 6 | 1 | bookshelf.jpg, bottle.tga, pot.jpg, pot2.jpg |  |
| 53 | Object54 | 2 | 1 | 20 | 2 | 1 | books.jpg |  |
| 54 | Object55 | 4 | 2 | 116 | 9 | 7 | candle.jpg, candle2.jpg | additive mesh 1 |
| 55 | Object56 | 2 | 2 | 54 | 1 | 1 | chair.jpg, chair2.tga | CreateOperate sit+turn |
| 56 | Object57 | 16 | 2 | 116 | 9 | 7 | candle.jpg, candle2.jpg | additive mesh 1 |
| 57 | Object58 | 1 | 2 | 114 | 2 | 1 | plate2.jpg, pot3.tga |  |
| 58 | Object59 | 29 | 2 | 52 | 1 | 1 | snotice.jpg, snotice02.jpg |  |
| 59 | Object60 | 23 | 2 | 52 | 1 | 1 | snotice.jpg, snotice02.jpg |  |
| 60 | Object61 | 0 | 1 | 54 | 1 | 1 | pot2.jpg | unplaced |
| 61 | Object62 | 12 | 1 | 86 | 1 | 1 | ti05.jpg |  |
| 62 | Object63 | 8 | 5 | 61 | 1 | 1 | ti05.jpg, titel_b03-06.jpg |  |
| 63 | Object64 | 10 | 5 | 64 | 1 | 1 | ti05.jpg, titel_b01/03/04/06.jpg |  |
| 64 | Object65 | 1 | 1 | 73 | 1 | 1 | ti01.jpg |  |
| 65 | Object66 | 1 | 2 | 192 | 6 | 1 | wall_door02.jpg, wall_door03.jpg | door: swings open, SOUND_DOOR01 |
| 66 | Object67 | 9 | 2 | 110 | 4 | 1 | fire_01.jpg, fire_02.jpg | CreateFire(0) at +50 (a brazier) |
| 67 | Object68 | 2 | 3 | 237 | 2 | 1 | show_ston01.jpg, snotice02.jpg, ti05.jpg |  |
| 68 | Object69 | 15 | 1 | 100 | 2 | 1 | show_ston01.jpg |  |
| 69 | Object70 | 28 | 1 | 85 | 2 | 1 | show_ston01.jpg |  |
| 70 | Object71 | 24 | 1 | 80 | 1 | 1 | show_ston01.jpg |  |
| 71 | Object72 | 6 | 2 | 40 | 1 | 1 | sky.tga, wall_door03.jpg |  |
| 72 | Object73 | 3 | 2 | 300 | 1 | 1 | show_ston01.jpg, sky.jpg |  |
| 73 | Object74 | 32 | 1 | 26 | 1 | 1 | snow_ston.jpg | CreateOperate sit |
| 74 | Object75 | 13 | 1 | 273 | 1 | 1 | tree_03.jpg |  |
| 75 | Object76 | 3 | 2 | 140 | 12 | 30 | stree.jpg, stree.tga |  |
| 76 | Object77 | 20 | 2 | 56 | 1 | 1 | house_stone_out.jpg, wall_door03.jpg |  |
| 77 | Object78 | 25 | 2 | 36 | 1 | 1 | house_stone_out.jpg, wall_door03.jpg |  |
| 78 | Object79 | 23 | 4 | 70 | 3 | 1 | house_stone_out.jpg, light_02.jpg, wall_door01.jpg, wall_door03.jpg | additive mesh 3; BlendMeshLight flickers 0.4-0.7 |
| 79 | Object80 | 8 | 1 | 20 | 1 | 1 | wall_door03.jpg |  |
| 80 | Object81 | 3 | 2 | 70 | 2 | 1 | desk.jpg, feather.tga |  |
| 81 | Object82 | 20 | 2 | 30 | 1 | 1 | show_ston01.jpg, tile_wood04.jpg |  |
| 82 | Object83 | 13 | 2 | 28 | 1 | 1 | show_ston01.jpg, tile_wood04.jpg |  |
| 83 | Object84 | 10 | 2 | 36 | 8 | 26 | Javelins03.jpg, badge_03.tga |  |
| 84 | Object85 | 27 | 2 | 36 | 8 | 25 | Javelins03.jpg, badge_01.tga |  |
| 85 | Object86 | 29 | 2 | 36 | 8 | 25 | Javelins03.jpg, badge_02.tga |  |
| 86 | Object87 | 6 | 1 | 108 | 1 | 1 | tile_02.jpg | gate: slides open within 200 units, SOUND_DOOR02 |
| 87 | Object88 | 8 | 8 | 760 | 5 | 1 | boots/gloves/head/lower/upper_11.jpg, guard_hair.tga, hide.jpg, sword08.jpg | a guard statue, on the guard's own sheets |
| 88 | Object89 | 3 | 2 | 192 | 6 | 1 | wall_door02.jpg, wall_door03.jpg | door: swings open, SOUND_DOOR01 |
| 89 | Object90 | 0 | - | - | - | - |  | **42-byte stub .bmd**; slot empty in MU |
| 90 | Object91 | 9 | 2 | 86 | 1 | 1 | ti01.jpg, ti02.jpg |  |
| 91 | Object92 | 44 | 1 | 5 | 1 | 1 | aurora.jpg | **hidden**, CreateOperate lean/pose, box 40x40x160 |
| 92 | Object93 | 4 | 1 | 192 | 3 | 1 | ice.jpg | additive mesh 0 |
| 93 | Object94 | 22 | 1 | 128 | 2 | 1 | ice.jpg | additive mesh 0 |
| 94 | Object95 | 1 | 2 | 78 | 3 | 1 | desk.jpg, wall_door03.jpg |  |
| 95 | Object96 | 7 | 4 | 130 | 10 | 1 | books.jpg, bookshelf.jpg, desk.jpg, feather.tga |  |
| 96 | Object97 | 3 | 1 | 34 | 1 | 1 | house_stone_out.jpg |  |
| 97 | Object98 | 5 | 2 | 18 | 1 | 1 | art.jpg, ti02.jpg |  |
| 98 | Object99 | 20 | 2 | 84 | 1 | 1 | show_ston01.jpg, titel_a04.jpg |  |
| 99 | Object100 | 25 | 2 | 84 | 1 | 1 | show_ston01.jpg, titel_b06.jpg | 20 of the 25 in the wedding corner |
| 100 | Object101 | 1 | 1 | 5 | 1 | 1 | aurora.jpg | **hidden**; Lost Tower door lightning, level >= 50 (33) |
| 101 | Object102 | 1 | 10 | 841 | 1 | 1 | snowmachine2.JPG + 9 Item sheets not in Object3 | Season corner, x 7-13 y 75-79 |
| 102 | Object103 | 1 | 1 | 68 | 1 | 1 | drum2.JPG | Season corner |
| 103 | Object104 | 2 | 2 | 518 | 47 | 6 | dog_g99.jpg, dog_g991.jpg | sled dog, random action; Season corner |
| 104 | Object105 | 0 | 3 | 658 | 38 | 20 | knifestone.JPG, man.JPG, sword03.jpg | unplaced |
| 105 | Object106 | 1 | 6 | 1810 | 3 | 1 | cho_wedding_01/01_a/03-06 | DEVIAS_XMAS_EVENT only (off); wedding corner |
| 106 | Object107 | 1 | 2 | 52 | 2 | 1 | cho_wedding_02.JPG, cho_wedding_07.tga | DEVIAS_XMAS_EVENT only (off); wedding corner |
| 107 | Object108 | 8 | 1 | 40 | 1 | 1 | cho_wedding_01.JPG | wedding corner |
| 108 | Object109 | 13 | 4 | 208 | 1 | 1 | cho_wedding_03-06.tga | wedding corner |
| 109 | Object110 | 10 | 1 | 24 | 1 | 1 | cho_wedding_01.JPG | wedding corner |
| 110 | Object111 | 1 | 3 | 1611 | 5 | 1 | devgate1.jpg, devgateice.jpg, devgateice_R.jpg | DEVIAS_XMAS_EVENT2007 only (off); at 53,93 by the Season warp |
| 111 | Object112 | 14 | 2 | 156 | 2 | 1 | devgateice.jpg, devgateice_R.jpg | x 38-56, y 91-95 |
| 112-119 | Object113-118, object119-120 | 0 | | | | | giftbox_*, han_* | unplaced event pieces |

## Sources

- `OM/Version075/Maps/Devias.cs` (NPCs `:46-54`, spawns `:61-69`, breeds `:73-285`),
  `OM/Version075/Gates.cs` (warp `:47`, exits `:113-135`, enters `:185-206`),
  `OM/Version075/NpcInitialization.cs:64-150`, `OM/Version075/MerchantStores.cs:270-435`,
  `OM/Resources/075_Terrain3.att` and `Terrain3.att`; `OM/Version095d/...`,
  `OM/VersionSeasonSix/Maps/Devias.cs`, `OM/VersionSeasonSix/Gates.cs:50-53,156-164,511-601`.
- `MM/World/MapInfra/MapManager.cpp:60-86,1100-1123,1461-1472`;
  `MM/Engine/Object/ZzzObject.cpp` (`RenderObjectVisual :2819-2862`, render gate `:3446-3460`,
  `MoveObject :3900-3966`, `CreateObject :4668-4715`); `MM/Engine/Object/ZzzInterface.cpp:1713-1726,4054`;
  `MM/Engine/Object/ZzzCharacter.cpp:3441,5356,13997-14045,14416-14468,14674`;
  `MM/Scenes/MainScene.cpp:81,105,555`; `MM/Scenes/SceneManager.cpp:383,862-866,932,1007-1025`;
  `MM/Render/Effects/ZzzEffectFireLeave.cpp:274-297,525,552-568`;
  `MM/Render/Terrain/ZzzLodTerrain.cpp:210-211`; `MM/Input/Selection.cpp:192-198`;
  `MM/Engine/AI/GOBoid.cpp:1304-1345`.
- `MU2/docs/porting-a-world.md`; MU2_BGFX commits `9a2f6110`, `502e0afa`, `a6bae788`.
