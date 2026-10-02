# Blood Castle port: the record, the data, and how it plugs in

Research notes, 2026-10-02. Blood Castle 1 is MU server map **11**, client `Data/World12` + `Data/Object12`
(MuMain's `WD_11BLOODCASTLE1`). All seven castles share that one terrain and object set, and you enter
from Devias through the Messenger of Archangel (NPC 233). Three research passes wrote this page in
`docs/lost-tower-port.md`'s shape:
- Part A is the record: version, entry, the run, the grid's flips, monsters and rewards, read in
  OpenMU S6 and WebZen 1.00.93.
- Part B is the data and the look.
- Part C is the engine side, ending in the build steps and the decisions.

The passes' scratch (the WebZen sources and data, the decoded grid, MuExtract exports of every Object12
model and the castle's monsters, contact sheets) is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/cce4eb1f-fead-4794-86c6-adaad7b40a08/scratchpad/bc/`.

## Where it stands

**Step 1 is built (2026-10-02, the user: 'lets start with ground').** It added:
- `source/world/bloodcastle`: World12 through `terrain.py` (0-3.03 m, 5 slots, 210 placements of 35 kinds, Object38 missing);
- `bc_` sheets and a `ground.json`, with materials read off the art: brick and octagon paving are flagstone, the moss and earth are rock, and the drawbridge's iron-banded boards are plank;
- `GRASS_BY_MAP[11] = []`;
- the `kMaps` row `{"bloodcastle", 11, {13, 8}, true}`, plus the banner and minimap names;
- `sheets/worlds/bloodcastle.json`, started from the Lost Tower's numbers and pulled to the neutral grey of the user's MuMain shots (grey gravel court, grey brick, a black void with drifting orange embers -- MU's flares, ZzzObject.cpp:4365-4381, for step 8).

**Lights and fires (2026-10-02, the user: 'lets work on light emiters and fire emiters').**
- The four objects that carry them are built: Object12, the candle clusters (11); Object14, the monks (4); Object09, the ember heaps (2); and Object34, the floor feathers (20).
- MU's sprites are in `game/world/ornaments.cpp`: seven breathing BITMAP_LIGHTs a cluster and the monks' BITMAP_FLARE. Neither rig has a clip, so the sprites are fixed at the bones' bind points.
- Ours, marked in the recipes: one warm light a cluster, a faint one at each monk's lamp, and flames with CreateFire's light on the heaps.
- The sheet's `glow_strength` is 0.9 and its saturation 0.8.

Nothing else is built: no other objects, monsters, gates, event, or way home to Devias. It is reached by
`--world bloodcastle` or `--travel-at`, and only as bare land.

## The one thing to know first

**Blood Castle is not 0.75.** OpenMU has none in Version075 or Version095d; it exists only in VersionSeasonSix. WebZen's
own build flags put it in the **0.97d base build**, with 6 castles, 10 players and the classic 0.97d rules. The 7th
castle, the party-completion rules and the time-attack score come in at 0.99B. Like Inferno (0.95d), the whole event is
**ours / post-0.75**, and the replica target for it is **WebZen 0.97d** (`FOR_BLOODCASTLE`, `FOR_BLOODCASTLE2` and
`FOR_BLOODCASTLE3` on; `BLOODCASTLE_EXTEND` and `_EVENT_3RD/4TH/5TH` off).

## In one screen

| | BC1, settled (0.97d WebZen unless marked) | source |
|---|---|---|
| map / client | server 11; client World12 + Object12, **shared by all castles** | WZ `define.h:4862`; MM `MapManager.cpp:1104-1106, 1207-1209` |
| ticket | Invisibility Cloak 13/18 **+1** (cloak level = castle) | WZ `protocol.cpp:19695-19707` |
| level band | **15-80**, Magic Gladiator **10-60** | WZ `BloodCastle.h:105-106` |
| players | **10** a castle | WZ `BloodCastle.h:33-34` |
| when | **every hour at hh:30** by default (cfg `BloodCastleStartHour` 2 = every other hour); entry opens OPEN minutes before | WZ `BloodCastle.cpp:546-606, 778-782` |
| length | 60 s wait, then **15 min** of play, then 1 min rest | WZ `:887-889`; MM `NewBloodCastleSystem.cpp:46`; WZD `BloodCastle.dat:4` |
| arrival | gate **66**: map 11, box 12,5-14,10, in a 93-tile safe zone | WZD `Gate.txt:115`; OM S6 `Gates.cs:216` |
| quota 1 | **40 x living players** kills, any monster -> bridge | WZ `BloodCastle.h:46`, `BloodCastle.cpp:4275` |
| gate | Castle Gate 131 at 14,75; can't be hit before quota 1 | WZ `user.cpp:8785-8787` |
| quota 2 | **2 x living players** Spirit Sorcerers (89), **max 10**, spawned when the gate falls | WZ `gObjMonster.cpp:1302-1308`, `user.cpp:21719-21744` |
| statue | Statue of Saint 132/133/134 (random) at 14,95 -> Weapon of Archangel 13/19 +0/+1/+2 | WZ `BloodCastle.cpp:2216`, `user.cpp:21748-21790` |
| win | carry *that* weapon to the Archangel (232) at 10,9 | WZ `NpcTalk.cpp:1461-1588` |
| fail | time out, or every player dead/out | WZ `BloodCastle.cpp:929-959, 1000-1020` |
| reward | gate 20 000 + statue 20 000 + quest 5 000 exp (party-wide), **+160 exp per second left**; zen 20 000 winner side / 10 000 others; 1 Jewel of Chaos to the winners | WZ `BloodCastle.h:192-205, 310`, `BloodCastle.cpp:3100-3577` |
| monsters | 95 placed: 84 x19, 85 x17, 86 x24, 87 x11, 88 x16, 89 x8 | WZD `MonsterSetBase.txt:336-434`; OM `BloodCastle1.cs:36-135` |

## Part A: the record

### 1. Version: who first has it

- **OpenMU**: grepping for blood/archangel/invisib finds nothing in `OM/Version075` or `OM/Version095d` (the only hit is 0.95d's
  "Bloody Wolf"), and `OM/Version097d/` holds only `Items/Jewels.cs`. Blood Castle exists only in `OM/VersionSeasonSix`
  (`Events/BloodCastleInitializer.cs`, `Maps/BloodCastle1-8.cs`, `Items/EventTicketItems.cs:28-32`).
- **WebZen** `define.h` (WZD include, `scratchpad/bc/wz/define.u8`):
  - `:36-48`: G_V_97D = 0, G_V_99B = 2, ... and this build is `GAME_VERSION G_V_S2_2`.
  - `:4026-4035`: **`FOR_BLOODCASTLE`, `FOR_BLOODCASTLE2` and `FOR_BLOODCASTLE3` are unconditional**. The comment on 3 reads
    "b4nfter - 2003.10.21 patch".
  - Only `#if GAME_VERSION >= G_V_99B`: `BLOODCASTLE_EXTEND_20040314` (7 castles: "gate.txt, MonsterSetBase.txt, BloodCastle.dat"),
    `BLOODCASTLE_EVENT_3RD_20040401` and `_4TH_20040531` (time attack), and `_5TH_20050531` ("party-centred").
  - The `MAX_MAP` ladder `:4805-4822` runs 10, then 11 with Icarus (`NEW_SKILL_FORSKYLAND`), then **17 with `FOR_BLOODCASTLE`**
    (maps 11-16), then 18 with EXTEND, then 24 with Chaos Castle. So Blood Castle came after Icarus (0.95d) and before 0.99B: **0.97(d)**.
- Season 6 adds BC8 (map 52, master level; `ADD_BLOODCASTLE_FOR_MASTER_LEVEL_20071010`, `define.h:3778`) and the S4
  hour:minute schedule (`UPDATE_BLOODECASTLE_SCADULE_20080624`, `define.h:1429`, under `ADD_SEASON_4_UPDATE`).
- What differs at 0.97d versus what most references describe (S6/OpenMU):
  - 6 castles. BC6 is open-ended at **281+ (MG 261+)** (`BloodCastle.h:115`); BC7 (331+/311+, map 17, gate 80) is 0.99B.
  - 10 players a castle (15 without FOR_BLOODCASTLE3, `BloodCastle.h:33-37`).
  - The quest weapon is handed in by **one** player, and that ends the event (`NpcTalk.cpp:1569-1588`). At 0.99B+ each
    member of the winner's party has to report too (`:1504-1566`).
  - The time-scaled monster buff `g_iBC_MONSTER_CHANGE_STATE` (`BloodCastle.h:336-340`) is applied only under 5TH
    (`user.cpp:21797-21809`), so it is not in 0.97d.

### 2. Entry

#### 2.1 The Messenger of Archangel (NPC 233)

- `NpcTalk.h:46` `NPC_ANGELMESSANGER 233`. OM S6 `NpcInitialization.cs:95-100` gives him the designation "Messenger of Arch." and
  `NpcWindow.BloodCastle`; WZD `Monster.txt:318` names him "Spirit of Archangel".
- **Where he stands disagrees.** WZD `MonsterSetBase.txt:2536-2537` has Devias **209,30 dir 1** and **219,9 dir 3**. OM S6
  `Maps/Devias.cs:45-46` has **217,29** and **217,20**, both SouthEast. MU2_BGFX already stands him at OM's 217,29 dir 4
  (`tools/cook.py:1943-1946`, `source/world/devias/placements.json:124`, model `BloodCastle02`).
- Talk (`NpcTalk.cpp:1655-1753`):
  - A murderer (PK level > default) is refused (`:1664-1676`; a 2006 flag).
  - With a cloak, and the cloak's castle CLOSED with entry open, the BC window opens (0x30 result 6, `:1731-1742`).
  - With a cloak outside the entry window: ServerCmd 1,20, "not yet" (`:1744-1746`).
  - With no cloak: ServerCmd 1,21 (`:1748-1750`).
- The window (MM `NewUIBloodCastleEnter.cpp:26-42`) lists the bands 15-80 / 81-130 / ... / 331-400 and, for Magic Gladiator
  (and Dark Lord / Rage Fighter in S6, `:266-267`), 10-60 / 61-110 / ...; it sends the castle number (`:243`).

#### 2.2 The ticket: Invisibility Cloak 13/18

- Entry handler `CGRequestEnterBloodCastle`, `protocol.cpp:19629-~20030` (packet 0x9A), in this order:
  1. PK -> result 7 (`:19676-19684`).
  2. Entry closed -> result 2 (`:19687-19692`).
  3. The item at the given slot is not a 13/18 of level 1..6 -> result 1 (`:19695-19752`).
  4. Level check `CheckEnterLevel(cloak level)`: too low -> 4, too high -> 3 (`:19813-19829`).
  5. Castle full -> 5 (`:19892-19896`).
  6. Otherwise the cloak is deleted (`:19920-19922`) and quest items are wiped (`:19953`). The message is "You have come to Blood
     Castle %d" (lMsg 1171), and the warp is `gObjMoveGate(GATE_BLOODCASTLE_1 = 66)` (`:19996+`).
  - The party is kept: the forced party leave is commented out (`:19940-19951`).
  - Client texts: MM `WSclient.cpp:8502-8560`.
- `CheckEnterLevel BloodCastle.cpp:1214-1267`: Magic Gladiator (and Dark Lord under `DARKLORD_WORK`) uses the MAGUMSA band, every
  other class the normal band.
- **Level bands** (`BloodCastle.h:105-120`), normal / MG:

| castle | map | 0.97d | 0.99B+ (EXTEND) | OM S6 (`BloodCastleInitializer.cs:147-194`) |
|---:|---:|---|---|---|
| 1 | 11 | **15-80 / 10-60** | same | same |
| 2 | 12 | 81-130 / 61-110 | same | same |
| 3 | 13 | 131-180 / 111-160 | same | same |
| 4 | 14 | 181-230 / 161-210 | same | same |
| 5 | 15 | 231-280 / 211-260 | same | same |
| 6 | 16 | 281-MAX / 261-MAX | 281-330 / 261-310 | 281-330 / 261-310 |
| 7 | 17 | -- | 331-MAX / 311-MAX | 331-400 / 311-400 |
| 8 | 52 | -- | master level | master, 331-400 |

  The repack's `BloodCastle.dat:72-101` gives BC6 as 281-**310** for the normal band; that is the repack's.
- **Crafting** (Chaos Machine), `BloodCastle.cpp:1294-1577`:
  - The box must hold exactly Scroll of Archangel 13/16 + Blood Bone 13/17 **of the same level** + one Jewel of Chaos 12/15 and
    nothing else (`:1465-1553`). Return codes: 8 other item, 11 one missing, 12 too many, 9 level mismatch or out of range,
    10 no Jewel of Chaos.
  - The player must be at least the BC1 minimum (15 / MG 10, `:1556-1569`).
  - Zen: **50k, 80k, 150k, 250k, 400k, 600k** for +1..+6 (850k +7, 1.05M +8) (`BloodCastle.h:223-236`). Not enough -> 0x86 result 0x0B.
  - Success **80%** (`BloodCastle.h:208-221`). On success you get a cloak of that level (`:1409-1416`). On failure the box is
    cleared and the materials are lost (`:1422-1435`).
  - OM `OMGL/PlayerActions/Craftings/BloodCastleTicketCrafting.cs:17-45` + `BaseEventTicketCrafting.cs:40-121` agrees
    (same prices, 80%).
- **Scroll of Archangel / Blood Bone drops** (`gObjMonster.cpp:5823-5985`):
  - They drop only with BC enabled and **only outside BC maps**.
  - The Scroll rolls `rand()%10000 < AngelKingsPaperDropRate`, the Bone `rand()%10000 < BloodBoneDropRate`. The code
    defaults are **10 and 20** (0.1% and 0.2%, `Gamemain.cpp:1133-1134`); the cfg read defaults to 0 (`BloodCastle.cpp:407-411`).
  - Level by the monster's level: **<32 +1, <45 +2, <57 +3, <68 +4, <76 +5, <84 +6, else +7** (`:5863-5900`).
  - At 0.97d a +7 can drop but can't be mixed (`CHECK_LIMIT(level-1, 6)` fails, `BloodCastle.cpp:1535-1539`).
  - OM: 1% per level bracket (`EventTicketItems.cs:29-30, 90`), with the +1 bracket starting at monster level 2.
    **OM's rate is 5-10x WebZen's.**
- Items (WZD `item.txt:376-379`, OM `EventTicketItems.cs:29-32`): 13/16 1x2, 13/17 1x2, 13/18 2x2, 13/19 1x2. Tooltip lines
  GT 814 / 816 (`docs/mu-tooltip-lines.md:186`).

#### 2.3 What the repack's data adds (not WebZen's)

`BloodCastle.dat` sections 0-2 are WebZen's format (`Load :232-266`); sections 5 (reward items) and 6 (level table) are loaded
by code the repack added (`:297-370`). WebZen's own reward drop is the body the repack commented out (`DropChaosGem :4014-4034`):
one Jewel of Chaos. Section 0's `5 15 1` (open 5 min, play 15, rest 1) is the only timing on record. The constructor's
`OPEN = 10` (`:91`) is a placeholder ("later read from file").

### 3. The run: phases and timings (WebZen 0.97d)

States: NONE 0, CLOSED 1, PLAYING 2, PLAYEND 3 (`BloodCastle.h:62-67`). Each castle runs its own clock (`Run :728-756`, a 1 s tick).

| t (BC1, hourly default) | what happens | source |
|---|---|---|
| hh:25 (OPEN = 5 min before) | entry and party open; "N min to Blood Castle entry" to every player each minute (lMsg 1160, sent by castle 1 only) | `BloodCastle.cpp:778-802` |
| hh:29:30 | 0x92 type 3 (30 s countdown) to everyone not in BC/CC | `:804-826` |
| **hh:30** | -> PLAYING: entry closed; "quest of castle %d starts in 60 s" (lMsg 1163); ServerCmd F3/40 type 1; quest items wiped. Players wait in the safe zone: in this state a non-safe tile is refused (`CheckWalk :1668-1690`) | `:1128-1171` |
| hh:30:30 | 0x92 type 4 (30 s to start) | `:852-858` |
| **hh:31** | timer reset to the full **15 min**; party closed; "Blood Castle %d quest has begun" (lMsg 1161); **entrance barrier lifted** (+client); `SetMonster` (gate + all non-boss monsters); state START (client: everyone plays PLAYER_RUSH1, `iBloodCastle.wav` loops, MM `NewBloodCastleSystem.cpp:41-43`) | `:887-917` |
| each second | state PLAY (normal kills) or PLAY_BOSS (boss kills): remaining s, max/current kills, who holds the weapon (`SendNoticeState :2833-2862`) | `:960-968` |
| quota 1 met | 0x9B state 3 MONSTEREND: the client lowers the bridge (`SetActionObject(world, 36, 20)` = Object37, angle 35->90 deg, SOUND_DOWN_GATE, MM `ZzzObject.cpp:86-140`); server bridge cleared now; client attribute packet **3 s later**; "monsters cleared! attack the castle gate" (lMsg 1168) | `gObjMonster.cpp:1275-1316`, `BloodCastle.cpp:860-885` |
| gate destroyed | "%s destroyed the castle gate" (lMsg 1178); door boxes released (+client); boss quota set; **Spirit Sorcerers spawn** | `user.cpp:14082-14110, 21711-21745` |
| quota 2 met | **statue spawns**; "destroy the statue" (lMsg 1180) | `gObjMonster.cpp:1319-1341` |
| statue destroyed | Weapon of Archangel drops (10 s loot lock for the top hitter, `BloodCastle.h:44`) | `user.cpp:21748-21790` |
| weapon to Archangel | ServerCmd 1,23 "success"; **GiveReward_Win**; -> PLAYEND | `NpcTalk.cpp:1461-1588` |
| hh:45:30 at 30 s left | 0x92 type 5 | `BloodCastle.cpp:920-926` |
| **time out** (hh:46) | GiveReward_Fail; -> PLAYEND | `:1000-1020` |
| everyone dead or out | "infiltration has failed" (lMsg 1162); GiveReward_Fail; -> CLOSED at once | `:929-959` |
| PLAYEND (REST 1 min) | monsters cleared (the gate stays); quest items deleted; 0x92 type 6 at 30 s | `:1024-1048, 1174-1196` |
| -> CLOSED | everyone left is moved to **gate 22** (Devias town, WZD `Gate.txt:41`); door, bridge (hollow) and entrance blocked again; the Archangel is ensured; next start synced | `:1085-1125` |

- Dying in BC sets the player's state to DEAD and drops the quest weapon where he fell (`user.cpp:14249-14254`). He respawns
  in **Devias** (`user.cpp:22084-22089`), and logging in on a BC map also puts him in Devias (`user.cpp:3147-3150`). He cannot
  come back (the cloak is spent): **a dead player is out**.
- Monster experience inside BC is **halved** (`BC_MONSTER_KILL_EXP_PERCENT 50`, `user.cpp:12875-12878`).
- In BC the gate and statue never move or flinch (`gObjMonster.cpp:1524-1529`, `ObjAttack.cpp:2713-2719`), and no BC monster
  knocks the player back (`gObjMonster.cpp:1414-1422`).
- **OpenMU differs**: every 120 min from midnight (`BloodCastleStartConfiguration.cs:15-23`); 1 min entry, then a 30 s
  countdown, **20 min** of play, then 1 min + 30 s exit (`BloodCastleInitializer.cs:210-212`, `MiniGameContext.cs:706-774`).
  It has no 60 s safe-zone wait, and the remarks say "usually 15 or 20 minutes" (`BloodCastleContext.cs:27`). The client
  is built for **15 min** (MM `NewBloodCastleSystem.cpp:46, 73`, `SetMatchInfo(..., 15 * 60, ...)`).

### 4. Gates, the grid and the tiles that change

#### 4.1 Gates

| gate | map | box | dir | role | source |
|---:|---|---|---|---|---|
| **66** | 11 | 12,5 - 14,10 | 0 | BC1 arrival, all 18 tiles safe (attr 1) | WZD `Gate.txt:115`; OM S6 `Gates.cs:216`; `BloodCastle.h:70` |
| 67-71 | 12-16 | the same box | 0 | BC2-6 | WZD `Gate.txt:116-120` |
| 80 | 17 | the same box | 0 | BC7 (0.99B+) | `:121`; `BloodCastle.h:77` |
| 271 | 52 | the same box | 0 | BC8 (S6) | OM S6 `Gates.cs:223` |
| 22 | Devias | 197,35 - 218,50 | 0 | where everyone is sent at the end, and the respawn | WZD `Gate.txt:41`; OM 075 `Gates.cs:128` |

There is no enter gate and no warp-list entry: the only way in is the Messenger. OM's `SafezoneMapNumber => Devias`
(`BloodCastleBase.cs:35`).

#### 4.2 The grid (client `MMD/World12/EncTerrain12.att`, decoded with the lost tower's `dec.py`, `scratchpad/bc/bc1grid.py`)

- **WZD `Terrain12.att` is byte-identical to the client grid** in the low bits and in 0x20. OM's `Resources/Terrain12.att` has the
  0x20 bits stripped and one walkable hole in the bridge, **14,72 = 0** (a 0 where the client has 8). WZ wins.
- The castle is a 3-tile-wide strip on the map's west edge. Everything else (x ~ 50-255) is open, object-less ground nobody reaches.
  The reachable regions: the safe hall (93 tiles, x8-19 y3-14), the bridge road (138, x13-15 y24-69), the courtyard (blocked until the gate falls), and
  the statue hall (67, x12-25 y90-96).
- Flags (MM `_define.h:50-59` = WZ `MapClass.h:21-26`): 0x01 safe, 0x02 character, **0x04 block**, **0x08 no ground (hollow)**,
  0x10 water, **0x20 TW_ACTION** (set on the x13 and x15 edge columns of the road y24-69, the entrance y16-23 and the bridge
  y70-75 -- 92 + 16 + 12 tiles).

```
 x 0 ......... 29       (# block, ~ no ground, s safe, . open)
  3 ~~~~########ssssssss#####~~~~~     gate 66 = 12,5-14,10 (all safe)
  9 ##########sssssssss######~~~~~     Archangel 232 at 10,9
 14 ~~~~~~~######sss###~~~~~~~~~~~
 15 ~~~~~~~~##########~~~~~~~~~~~~     <- entrance barrier x13-15 y15-23 (block, lifted at start)
 23 ~~~~~~~~~~~~~###~~~~~~~~~~~~~~
 24 ~~~~~~~~~~~~~...~~~~~~~~~~~~~~     road x13-15 y24-69: Chief Skeleton Warriors/Archers, Dark Skulls, Ogres
 69 ~~~~~~~~~~~~~...~~~~~~~~~~~~~~
 70 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~     <- bridge x13-15 y70-75 (no ground, filled by quota 1)
 75 ~~~~~~~~~##~~~~~~~##~~~~~~~~~~     Castle Gate 131 at 14,75
 76 ##############################     <- gate x13-15 y76-79 (block, lifted when the gate dies)
 80 ##########################..##     <- courtyard x11-25 y80-89 + x8-10 y80-83 (block, lifted with it)
 90 ############....###.......####     statue hall; Statue of Saint at 14,95
 95 ############.........#########
```

#### 4.3 What flips (WZ `BloodCastle.cpp:2506-2625`, boxes `BloodCastle.h:128-173`)

| box | tiles | bit | set at | cleared at | client packet |
|---|---|---|---|---|---|
| entrance | 13,15 - 15,23 | 0x04 | CLOSED | play start (after 60 s) | 0x46, attr 4, type 1 (`:2666-2697`) |
| bridge | 13,70 - 15,75 | **0x08** | CLOSED | quota 1 (server now, client +3 s) | 0x46, attr 8 (`:2700-2738`); the client also lowers Object37 (MM `WSclient.cpp:8724-8727`) |
| gate | 13,76 - 15,79 | 0x04 | CLOSED / NONE | gate death | 0x46, attr 4, count 3 (`:2741-2780`) |
| courtyard | 11,80 - 25,89 | 0x04 | same | same | same packet |
| altar | 8,80 - 10,83 | 0x04 | same | same | same packet |

The client applies `AddTerrainAttributeRange(x, y, dx, dy, attr, 1 - setType)` (MM `WSclient.cpp:8714-8740`). The server ORs
the bit in or masks it out (`|=` / `&= ~`), so the 0x20 bits survive.

**OM disagrees on two boxes**: courtyard **11,78**-25,89 and altar 8,**78**-**11**,83 (`BloodCastleInitializer.cs:246-264`). It also
releases all three door boxes at event start ("already unlocked because of the monster spawns", `:245`), and lifts the
gate box only when the gate dies (`:292-300`). WZ wins: rows 78-79 outside x13-15 are castle wall in every grid.

### 5. Monsters

#### 5.1 BC1 placements (WZD `MonsterSetBase.txt:314-434`; OM `Maps/BloodCastle1.cs:36-135`, `BloodCastleBase.cs:48-51`)

All single points (the 30 in the range column is ignored: `SetMonster` puts them on the exact tile, `BloodCastle.cpp:2089-2099`).
Facing is random (`:2114`).

| breed | count | where (zone) | spawned |
|---|---:|---|---|
| 84 | 19 | road, x13-15 y23-45 | at play start |
| 85 | 17 | road, x13-15 y21-45 (5 inside the entrance box y21-23) | at play start |
| 86 | 24 | road, x13-15 y41-69 | at play start |
| 87 | 11 | road, x13-15 y48-**70** (WZD; OM 69) | at play start |
| 88 | 16 | 5 on the road (15,62 14,54 14,36 15,68 14,47), 3 in the gate box (13-15,78), 6 in the courtyard, 2 in the statue hall | at play start (the inner ones are boxed in until the gate falls) |
| 89 | 8 | courtyard 12-19,80-88 and 13,93 | **when the gate dies** (`SetBossMonster :2121-2180`) |
| 131 Castle Gate | 1 | 14,75 | at play start, no regen |
| 132 Statue | 1 | 14,95, class re-rolled 132-134 | at quota 2, no regen |
| 232 Archangel | 1 | 10,9 dir 1 | always (`CheckAngelKingExist`) |

- OM's 95 points equal WZD's except one: Giant Ogre **14,69** (OM) against **14,70** (WZD), which is a no-ground bridge tile.
- **BC2-7 use exactly the same 95 points** with their own breeds (a check of WZD maps 12-17 against 11): BC2 90-95, BC3 96-99 + 111-112,
  BC4 113-118, BC5 119-124, BC6 125-130, BC7 138-143. The Spirit Sorcerer by castle is 89, 95, 112, 118, 124, 130 (143, 433)
  (`BloodCastle.cpp:179-191`; OM `BloodCastleInitializer.cs:132`).
- Respawn: the non-boss monsters and the bosses take `MaxRegenTime = m_iBC_MONSTER_REGEN` (`:2111, 2172`). It is **0 ms** both in the
  constructor (`:95`) and in the repack's dat (`BloodCastle.dat:10`), against `Monster.txt`'s 10 s and OM's 10 s. The gate, the statue and anything
  with `m_PosNum == -1` never come back (`user.cpp:21792-21795`).

#### 5.2 Breeds -- the two sources disagree badly

WZD `Monster.txt:112-117, 175-178, 317-318` (columns `:6`: level, HP, dmg, def, magic def, attack rate, defence rate, move/attack type/attack
range/view range, move speed, attack speed, regen s, ...):

| # | WZD name | lvl | HP | dmg | def | AR | DR | ranges (move/atk/view) | atk ms | skill | res |
|---:|---|---:|---:|---|---:|---:|---:|---|---:|---|---:|
| 84 | Orc Warrior | **33** | **1 100** | 50-60 | 50 | 190 | 45 | 3/1/3 | 2000 | -- | 0 |
| 85 | Orc Archer | 37 | 1 400 | 60-70 | 55 | 210 | 49 | 3/5/7 | 1600 | -- | 0 |
| 86 | Elite Orc | 41 | 1 700 | 70-80 | 60 | 230 | 61 | 3/1/3 | 2000 | -- | 0 |
| 87 | Spirit Beast | 44 | 2 100 | 80-90 | 65 | 250 | 73 | 3/5/7 | 1600 | 17 (Energy Ball) | 0 |
| 88 | Spirit Knight | 47 | 2 800 | 90-100 | 70 | 270 | 77 | 3/1/3 | 2000 | -- | 0 |
| 89 | Spirit Sorcerer | 51 | 3 700 | 100-110 | 75 | 300 | 88 | 4/4/6 | 1600 | 150 (monster skill) | 3 |
| 131 | Castle Gate | 0 | 2 000 (overridden) | | | | | | | | 100 |
| 132-134 | Statue of Saint | 0 | 3 000 (overridden) | | | | | | | | 100 |

OM S6 `BloodCastle1.cs:141-333` calls them Chief Skeleton Warrior/Archer 1, Dark Skull Soldier 1, Giant Ogre 1, Red Skeleton Knight 1
and Magic Skeleton 1 (MuMain's names, MM `_enum.h:4459-4464`), and gives **lvl 56/61/66/70/74/79 and HP 5 000/6 500/8 000/9 500/12 000/15 000**.
Those are **WZD's BC3 rows** (WZD 96-99 = 56/5000, 61/6500, 66/8000, 70/9500; WZD 112 = 79/15000), and OM's BC2 likewise sits higher than WZD's
BC2. OM's own `Updates/FixBloodCastleMonsterAttributesUpdatePlugIn.cs:48-60` already had to re-swap BC7 and BC8. **Settle on WZD**:
a level-33-51 garrison fits a 15-80 castle, and the lost tower found WZD's `Monster.txt` matching OM 075 stat for stat. This
needs the user's word (open question 1).

- Gate and statue HP. The repack's `BloodCastle.dat:15-22` gives statue / gate HP of BC1 **65 000 / 150 000** (BC2 105k/205k, BC3 145k/260k,
  BC4 185k/325k, BC5 225k/400k, BC6 265k/480k, BC7 305k/565k), and OM's `BloodCastleBase.cs:17,22` has the same numbers. But
  **WZ gives the gate the statue's field**: `BloodCastle.cpp:1998-1999` `MaxLife = m_iCastleStatueHealth`, and `m_iCastleDoorHealth`
  is read (`:259`) and never used. In WebZen's code, as written, the BC1 gate and the statue both have **65 000**. Flag it
  (open question 2).
- Neither the gate nor the statue drops anything (`gObjMonster.cpp:3980-3990`). The statue's weapon is spawned by the regen pass.
- What MuMain draws (MM `ZzzCharacter.cpp:13393-13520`; body `MMD/Monster/Monster{MODEL+1:02}.bmd`, all present):

| breed | model | file | scale | weapon |
|---|---|---|---|---|
| 84 | MODEL_ORC (47) | Monster48 | 1.1 | |
| 85 | MODEL_ORC_ARCHER (46) | Monster47 | 1.1 | Battle Bow |
| 86 | DARK_SKULL_SOLDIER (59) | Monster60 | 1.0 | 2x Crescent Axe |
| 87 | GIANT_OGRE (58) | Monster59 | 0.8 | |
| 88 | RED_SKELETON_KNIGHT (57) | Monster58 | 1.19 | Chaos Dragon Axe |
| 89 | MAGIC_SKELETON (62) | Monster63 | 1.2 | staff, level 11 |
| 131 | CASTLE_GATE (61) | Monster62 | 0.8, no shadow | |
| 132-134 | STATUE_OF_SAINT (60) | Monster61 | 0.8, no shadow; holds the Divine Staff / Sword / Crossbow of Archangel (`:8952-8966`) | |

#### 5.3 Quotas

- Quota 1 = **40 x players alive and in the map at the start** (`SetMonsterKillCount :4232-4286`).
  - Every monster death on the map counts, except the Spirit Sorcerer breed, which counts towards quota 2
    (`gObjMonster.cpp:1238-1272`). The gate (131) is a monster too.
  - Solo: 40 kills.
- Quota 2 = **2 x living players, max 10**. It is computed when quota 1 is met and again when the gate dies (`gObjMonster.cpp:1302-1308`,
  `user.cpp:21719-21733`). Solo: 2 Spirit Sorcerers.
- The gate can't take damage before quota 1, nor the statue before quota 2 (`user.cpp:8781-8795` in `gObjAttackQ`).
- OM matches: 40 and 2 per player (`BloodCastleInitializer.cs:19-20, 271-273, 308-310`), but with no cap at 10, and its quota 2 counts only the
  Spirit Sorcerer breed (`:308`).

### 6. Rewards (WZ `GiveReward_Win :3100-3577`, `GiveReward_Fail :3580-3714`, once a castle)

Bonus exp per player, BC1 (`g_iBC_Add_Exp`, `BloodCastle.h:308-322`):
- +**20 000** if he, or his party, broke the gate (the top-damage player, `user.cpp:14087-14103`). Party members alive when it broke
  but not in the breaking party get +**10 000** (FOR_BLOODCASTLE3, `:3201-3207`).
- +**20 000** if he or his party broke the statue (`:3209-3223`).
- +**5 000** if he or his party handed in the weapon (`:3225-3239`).
- **On a win only**: + remaining seconds x **160** (BC1; 180, 200, 220, 240, 260 for BC2-6), for every player state (`:3251, 3309, 3367, 3438, 3504`).

The exp goes straight through `LevelUp` and can cross levels (`CalcSendRewardEXP :3717-3931`).

Zen and score on a win (`BloodCastle.h:192-205, 238-252`):

| player | zen (BC1) | score | Jewel of Chaos |
|---|---:|---:|---|
| alive, not in the winner's party | 10 000 | 600 | -- |
| dead | 10 000 | 300 | -- |
| **winner** | **20 000** | 1 000 | 1, at his feet, 5 min loot lock (`:3390-3397`; `BloodCastle.h:42`) |
| winner's party, alive | 20 000 | 800 | 1 each, if still on the map (`:3461-3463`) |
| winner's party, dead | 20 000 | 400 | -- |

- A loss: gate and statue bonuses only, no time bonus, no zen, score **-300** (`:3580-3714`, `BloodCastle.h:255-268`).
- The result window is 0x93 (`:3557-3570`). The client shows exp, then zen (wins only), then score (MM `NewBloodCastleSystem.cpp:127-176`).
- Zen for BC2-6: 50k/25k, 100k/50k, 150k/80k, 200k/100k, 250k/120k. The quest bonus for BC2-6 is 10k, 15k, 20k, 25k, 30k; gate and statue
  are 50k, 80k, 90k, 100k, 110k.
- The score goes to the ranking server only. A single-player game has no use for it beyond the result window.
- **OM agrees** on exp, zen and the Jewel of Chaos (`BloodCastleInitializer.cs:32-42, 326-373`). Its score rows are additive flags that sum to WZ's
  table (600+400 = 1000 and so on, `:44-126`). The remaining-seconds bonus is 160/s in OM too (WZ's S4 schedule variant is 192/s, `BloodCastle.h:292-306`).
- Item drops from BC monsters: the normal monsters' drop rate is left alone (the override is commented out, `:2113`); the gate, the bosses and the
  statue get `m_ItemRate = 100` (`:1991, 2174, 2218`).
- The Divine weapons of Archangel (0/19, 2/13, 4/18, 5/10) are only what the statue holds in the client. **They are not a BC reward.**

### 7. Where the sources disagree, settled or flagged

| # | point | WZ / WZD | OM S6 | settled |
|---|---|---|---|---|
| 1 | version | 0.97d base, 6 castles | S6 only, 8 castles | 0.97d, 6 castles, marked ours |
| 2 | schedule | hourly at hh:30, entry 5 min before, 60 s wait, 15 min play | every 2 h from 00:00, 1 min entry, 30 s countdown, 20 min | WZ (the client's timer assumes 15 min) |
| 3 | BC1 breeds | lvl 33-51, HP 1.1k-3.7k | lvl 56-79, HP 5k-15k (= WZD BC3) | WZD, ask the user |
| 4 | gate HP | 65 000 (the statue's field, a code slip) | 150 000 | open question |
| 5 | door boxes | 11,80-25,89 and 8,80-10,83 | 11,78-25,89 and 8,78-11,83 | WZ |
| 6 | Messenger tiles | Devias 209,30 and 219,9 | 217,29 and 217,20 | already placed at OM's 217,29; keep |
| 7 | scroll/bone drop | 0.1% / 0.2%, +1..+7 by monster level | 1% per level group | WZ |
| 8 | quota 2 cap | max 10 | none | WZ (solo is 2 either way) |
| 9 | ogre point | 14,70 (on a no-ground tile) | 14,69 | OM's 14,69, which is walkable |
| 10 | server grid | = client grid | 0x20 stripped, 14,72 open | client/WZD |
| 11 | BC6 band | 281-MAX / 261-MAX (0.97d) | 281-330 / 261-310 | WZ 0.97d |
| 12 | monster respawn | 0 ms (constructor and repack dat) | 10 s | open question |
| 13 | quest hand-in | one player ends it | one player ends it | agree (0.99B+ made it party-wide) |

### 8. What MU2_BGFX already has

- `source/world/bloodcastle/` holds terrain.py's World12 output (`bloodcastle.json` `"map_number": 12`, height, tiles, attributes,
  light, ground.json): the pipeline's client-number convention.
- The Messenger stands in Devias at 217,29 facing 4 and tells the hero BC is not ready (`src/sim/realm.h:140-142` `kMessenger = 233`;
  `src/game/play.cpp:1677-1680`; `tools/cook.py:1943-1946`).
- The travel card counts down a "Blood Castle" event on Devias every **7200 s, 1200 s long**, taken from OM (`src/game/ui/travel.cpp:44-59`).
  **WZ says hourly at :30 and 15 min of play (16 with the wait).** Re-time it if the user takes WZ.
- Nothing for items 13/16-19, NPC 232, breeds 84-89/131-134, gate 66 or the event itself.

### 9. Open questions

1. BC1 breed stats: WZD (lvl 33-51) or OM (lvl 56-79)? The recommendation is WZD.
2. Gate HP: WebZen's code gives the gate the statue's 65 000; the data intends 150 000. Port the code as written, or the intent?
3. The schedule in a single-player game: WebZen's hourly :30 on the local clock, OM's 2 h, or "on demand when the hero talks to the
   Messenger with a cloak"? The phases inside a run don't depend on the choice.
4. Monster respawn inside BC: WebZen's 0 ms (instant, as the constructor and dat say) or `Monster.txt`'s 10 s? 0 makes 40 kills from 95
   bodies trivial to farm. Solo, 76 road monsters already exceed 40.
5. Party rules are moot solo. With one player: quota 1 = 40, quota 2 = 2, and the winner gets every bonus (20k+20k+5k+160/s exp,
   20 000 zen, 1 Jewel of Chaos, score 1000).
6. The Archangel's own figure and the Messenger's (`BloodCastle02` is already used for the Messenger) belong to Part B.

---

## Part B: the data and the look (Blood Castle 1)

Research notes, 2026-10-02. No repo file was edited and no window was run. Blood Castle 1 is server map **11** (`WD_11BLOODCASTLE1`, `MM/World/MapInfra/MapManager.h:20-21`; `InBloodCastle` covers 11-17 plus 52, `MapManager.cpp:1583-1595`). **All seven castles load one folder pair.** `iMapWorld = WD_11BLOODCASTLE1 + 1` sends every castle to `D/World12` and `D/Object12` (`MapManager.cpp:1104-1107` for objects, `:1207-1210` for terrain). BC1 through BC7 differ only in the server's spawns and in two client tweaks: monster scale +0.05 per 3 castle levels (`ZzzCharacter.cpp:13153-13160`) and a level tint (`:9998-10010`). Both are zero at BC1.

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/`
- WZ = WebZen GameServer 1.00.93 (github ptr0x-real/Mu-GS-Webzen-MC-10093, `Source/Server Side/GameServer/`). Fetched to `S/wz/`: bc.cpp, bc.h, user.cpp, msb.txt
- S = `.../scratchpad/bc/B/`

Scratch outputs in S:
- `bloodcastle/`: `pipeline/terrain.py D/World12 bloodcastle S 12`. The run is `terrain.log`.
- `stats.log`, `comps.log`, `charmap.txt` (attribute map of x 0-35, y 0-117), `light_height.log`, `placements.log`
- `prev_l1_attr_height_light.png`: layer 1 | attributes with placements | height | light. `attr_strip.png` is the attribute strip enlarged.
- `tex/` and `sheet_textures.png`: every World12/Object12 sheet decoded. Alpha shows as magenta.
- `obj/`: all 43 Object12 .bmd through MuExtract (export-obj + export-rig --actions=all). Summary in `objinfo.log`.
- `mon/`: Monster47/48/58-63 and Npc/BloodCastle01/02. Summary in `moninfo.log`.
- `models_object12.png`, `models_mon.png`: cheap software renders. Painter's algorithm, one texel per triangle. They are good enough to identify a model, not to judge a look.
- Scripts: `stats.py`, `comps.py`, `prev.py`, `objinfo.py`, `render.py`, `sheet.py`.

**Another session is already on this.** These are uncommitted (`??`), made 2026-10-02 12:48:
- `MU2_BGFX/source/world/bloodcastle/` (terrain.py output and ground.json)
- `source/textures/bc_tile*.png`
- `GRASS_BY_MAP[11] = []` in `pipeline/terrain.py:168`

That ground.json cites `docs/blood-castle-port.md`, which does not exist yet. Its numbers agree with this extraction. Read `git diff` before staging anything there.

### The one thing to know first

**Blood Castle is a narrow strip down the west edge of a 256² grid: a lit court, then a one-tile-wide walk over a black chasm, then a drawbridge that falls, then a small courtyard. The rest of the grid is filler.**
- The play area is x 0-35, y 0-111:
  - **safe court** x 8-19, y 3-14 (93 safe tiles; the Archangel is at 10,9)
  - **bridge** x 13-15, y 16-69
  - **drawbridge gap** x 13-15, y 70-75
  - **castle door** at 14,75 (NPC 131)
  - **courtyard** y 76-111 (the Saint statue, NPC 132, is at 14,95)
- 83.2% of the grid is "walkable", but 32,009 of those tiles are an empty flat plain at x ≥ ~40 and y ≥ 112. It has no objects and a flat light of 88 grey. Crop it, or block it.
- **Height** is a flat 1.65 m everywhere except the court's rim and the castle (1.20-3.03 m). The void is 0-1.65.
- **Clear and fog are black** (the default arm, `MM/Scenes/SceneManager.cpp:401-402`). There is no sky.
  - NoGround tiles are never drawn (as in the Lost Tower, `ZzzLodTerrain.cpp:2311`).
  - The chasm either side of the bridge is black, fringed by Object06's purple-to-black curtain planes.
- **No music.** `ManageBackgroundMusic` has no Blood Castle arm (`SceneManager.cpp:985-1130`), and Data/Music has no castle track.
  - The only bed is `iBloodCastle.wav` (4.7 MB). It loops from match start (`NewBloodCastleSystem.cpp:42-43`, PlayState 0) and stops at the end (`:63-64`, state 2).
- **The drawbridge is the set piece.**
  - Object37 (type 36), the door/plank at 14.5,76.1, swings down over the gap with a bounce, `eDownGate.wav` and a smoke puff (`ZzzObject.cpp:86-165`).
  - Then it hides, and the bridge deck Object10 and chains Object11 (types 9, 10) appear (`:107-123`, MoveObject `:4185-4199`).
  - At the same moment the client clears NoGround on 13-15 × 70-75 (`:163`). That reveals the TileRock02 plank tiles beneath.

---

### 1. Raw data inventory

#### 1.1 World12 (`D/World12`, 24 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain12.map | 196610 | yes | 3 planes |
| EncTerrain12.att | 65540 | yes | attributes (below) |
| EncTerrain12.obj | 6304 | yes | **210 placements, 35 kinds** (types 6, 7, 24 unused) |
| TerrainHeight.OZB | 66620 | yes | header 1078, read at 1080 |
| TerrainLight.OZJ | **5988** | yes | almost flat 88 grey. Only 2909 tiles differ, all in the strip |
| TileGrass01.OZJ | 79626 | slot 0 | **blue-grey brick courses** (the castle stone, 97.2%) |
| TileGrass02.OZJ | 80505 | slot 1 | green-grey mossy rough rock |
| TileGround02.OZJ | 69735 | slot 3 | dark mossy rough stone |
| TileGround03.OZJ | 72945 | slot 4 | grey octagon paving with olive insets (courtyard) |
| TileRock02.OZJ | 49809 | slot 8 | **dark planks banded in iron**: the drawbridge |
| TileRock01/03/04, TileWater01, TileWood01 .OZJ | | loaded, unused | sand, desert, sand ripples, cracked earth: leftovers from another world |
| TileGrass01.OZT | 65584 | loaded as BITMAP_MAPGRASS | real grass blades. **Never drawn**: `ZzzLodTerrain.cpp:2082` skips grass `InBloodCastle()` |
| TileGrass03.OZT | 131120 | loaded | 256×128 brown streaks, unused |
| leaf01.OZT, leaf02.OZJ, rain01.OZT | tiny | loaded for every world (`MapManager.cpp:1465-1480`) | no leaves or rain in the castle |
| Terrain.map / Terrain12.att | | no | Terrain12.att is encrypted, not a plain copy |

**No TileGround01** ships here. Slot 2 is unused, so the gap in the files is harmless.

terrain.py (`S/terrain.log`, `stats.log`):

```
height     0.00 to 3.03 m; 5/50/95 pct 1.65/1.65/1.65 (flat)
tiles      5 slots in use: 0 TileGrass01 97.2%, 1 TileGrass02 2.0%, 3 TileGround02 0.5%, 4 TileGround03 0.2%, 8 TileRock02 (18 tiles)
walkable   83.2% (54,537), 0.1% safe (93 tiles, x 8-19 y 3-14)
light      mean [87, 87, 87]
objects    210 placed, 35 kinds; missing: Object38 (type 37, 9 placements)
```

**Attributes** (`_define.h:50-62`: 1 Safe, 2 Character, 4 NoMove, 8 NoGround, 0x20 **TW_ACTION**):

| value | meaning | tiles | where |
|---|---|---:|---|
| 0 | open | 54352 | strip plus the filler plain |
| 8 / 12 | NoGround (/ +NoMove) | 4665 / 2619 | the chasm round the bridge |
| 4 | NoMove | 3686 | walls, court rim, **the whole castle block y 76-109 apart from a few pockets** |
| 1 | Safe | 93 | court |
| **32** | ACTION | 92 | bridge rails x 13 and 15, y 24-69 |
| **36** | ACTION + NoMove | 16 | x 13, 15, y 16-23 (x 14 there is plain NoMove): **the closed entrance** |
| **40** | ACTION + NoGround | 12 | x 13, 15, y 70-75 (x 14 there is NoGround): **the drawbridge gap** |
| 14 | | 1 | (12, 71) |

`charmap.txt` is the readable version.
- The bridge is 3 tiles wide, x 13-15, with walkable rails flagged ACTION.
- The castle at y 76-109 is NoMove except two pockets: x 26-27, y 80-88 and the U at x 12-26, y 90-96. That U is the statue court.
- **OM's `Resources/Terrain12.att` equals the client grid minus the TW_ACTION bit.** It agrees on 99.82% of bytes and 100.00% of walkability. All 121 differences are the 0x20 flag on x 13-15, y 16-75.

**What opens during the event** (WZ `bc.h:128-174`, `bc.cpp:2506-2620`, `user.cpp:21736-21740`). All BC levels share one table.

| rectangle (x0,y0 - x1,y1) | server flag | starts | opened when | client sees |
|---|---|---|---|---|
| Entrance 13,15 - 15,23 | BLOCK (4) | blocked | quest start: `ReleaseCastleEntrance` + `SendCastleEntranceBlockInfo` (`bc.cpp:905-907`) | packet 0x46 type 0 → `AddTerrainAttributeRange(..., Add=0)` (`MM/Network/Server/WSclient.cpp:8718-8737`) |
| Bridge 13,70 - 15,75 | HOLLOW (8) = NoGround | hollow | bridge monster quota killed, then 3 s (`BC_MAX_TICK_DOOR_OPEN`, `bc.h:53`; `bc.cpp:861-868`) | NoGround packet → `SetActionObject(world, 36, 0, 1)` (`WSclient.cpp:8724-8727`): instant bridge, no animation. PlayState 3 → `SetActionObject(..., 36, 20, 1)` (`NewBloodCastleSystem.cpp:68-70`): the animated fall |
| Door 13,76 - 15,79 · inside 11,80 - 25,89 · side 8,80 - 10,83 | BLOCK | blocked | Castle Gate (131) dies: `SendCastleDoorBlockInfo` + `ReleaseCastleDoor`, then the boss and statue spawn (`user.cpp:21736-21744`) | same packet |

Even after all three open, y 97-109 and most of y 90-96 stay NoMove in both grids. The playable courtyard is about x 8-27, y 76-96.

**TW_ACTION is a death fling.** A character that dies on an ACTION tile is thrown off the side toward whichever neighbour is NoGround. It spins and falls into the chasm (`ReceiveDie` → `FallingStartCharacter`, `WSclient.cpp:5785-5827, 5852-5855`; per-frame `FallingCharacter`, `ZzzCharacter.cpp:3118-3136, 3255-3258`).

**Light map** (`light_height.log`):

| zone | mean RGB | note |
|---|---|---|
| safe court | 144, 143, **175** | bright, blue-violet |
| bridge | 78, 78, 78 | dim grey. Pools at y 10-15 (192-244) and y 75 (175) |
| gap and door | 153 | the door is lit |
| castle walkable | 76, 73, **87** | dim, faint blue |
| filler plain | 88 flat | unpainted |

The painted look is **cold moonlit stone**: a bright blue court, a dark bridge and a glow at the door.

**Tile sheets.** These were looked at in `sheet_textures.png`. The names lie, and slot 0 is the trap again. Recipes as the other session already wrote them in ground.json:

| slot | name | actually | material |
|---|---|---|---|
| 0 | TileGrass01 | blue-grey brick courses, 97.2% | flagstone (never grass) |
| 1 | TileGrass02 | green-grey mossy rock (365 overlay tiles) | rock |
| 3 | TileGround02 | dark rough mossy stone | rock |
| 4 | TileGround03 | octagon paving | flagstone |
| 8 | TileRock02 | iron-banded planks, 18 tiles exactly under the gap | plank |

The plank tiles sit on NoGround, so MU does not draw them until the bridge drops and the NoGround bit is cleared.

#### 1.2 Object12 (`D/Object12`, 67 files)

- 37 `ObjectNN.bmd` (01-37) plus Crow01, Gate01/02, StoneCoffin01/02 and Shine01
- 25 sheets: bird, br001, br002, bro01, cs001 (a red "TEST TEST TEST" placeholder, unused), doorc01, hop01, ju01, ju01_R, king01, light01, light2_R, light_R, lnn01, ng01, pan01_B, pan_B, pan_R, pant01, pant01_R, ses, skin_special_01, st09, wcd, woodc01

Type = model index - 1. **Object38 (type 37) has no .bmd.** MU uses those 9 placements only as smoke emitters (§2).

| type | model | placed | tris | rig | sheets | what it is |
|---:|---|---:|---:|---|---|---|
| 0-4 | Object01-05 | 2/2/1/2/2 | 52-60 | static | br001 + ses | **bridge parapet walls**, east side x 16, y 19-67, 6 m tall, 1.9-8 m long, alternating |
| 5 | Object06 | 15 | 2 | static | **br002.tga** (purple→black alpha) | **void curtain**: a 6.3×6 m vertical fade plane lining the chasm (bridge and gap) |
| 6, 7 | Object07, 08 | 0 | | | br002 / light_R | unplaced |
| 8 | Object09 | 2 | 360 | static | pan_R | **orange crystal or fire heap** in the courtyard, 12-15.5, ~100 |
| **9** | Object10 | 2 | 10 | static | pant01 | **lowered bridge deck** 3.3×3 m at the gap. Hidden until the bridge falls |
| **10** | Object11 | 2 | 888 | 4 bones, 12 keys | ses | **drawbridge chains** at the gap. Hidden until the bridge falls |
| **11** | Object12 | 11 | 70 | 14 bones, static | ses + ju01_R | **candle clusters** along the bridge end (y 60-72). Seven flickering lights (§2) |
| 12 | Object13 | 1 | 14 | static | pant01 + pant01_R | black slab at the statue (14.5, 97) |
| **13** | Object14 | 4 | 406 | static | ju01 + pan_R + ju01_R | **hooded green-robed monk statues** round the statue court, z 263-268. Flare at bone 3 |
| 14, 15 | Object15, 16 | 5, 9 | 152 / 280 | 3 bones, 12 keys | ses | **hanging chains** swinging along the east parapet |
| 16 | Object17 | 2 | 1450 | static | bro01 | **mossy gargoyle or dragon statues** flanking the gap (scaled 2.3) |
| 17 | Object18 | 2 | 56 | static | ses + br001 | gate-flank walls at the gap |
| 18-20 | Object19-21 | 3/4/10 | 10-28 | static | br001 (+ses) | steps, a block and the **castle front wall** (Object21 ×10 along y 76) |
| 21, 22 | Object22, 23 | 11, 26 | 80 / 30 | static | br001 | **rubble scatter** (z up to 585, so some sits on walls) |
| 23 | Object24 | 6 | 70 | static | br001 + pant01 | **round pillars** |
| 25 | Object26 | 9 | 78 | static | br001 + ses | battlements on the front wall (y 76) |
| 26, 27 | Object27, 28 | 13, 9 | 378 / 60 | static | woodc01 | **wooden scaffold towers and ladders** (siege dressing) |
| **28** | Object29 | 3 | 848 | **47 bones, 6 keys** | ng01 + skin_special_01 + ju01_R | **winged angel-knight statues** in the safe court. A black planar shadow is drawn under them (§2) |
| **29** | Object30 | 1 | 720 | **45 bones, 12 keys** | same | **angel knight with sword**, at 10,9.2, the Archangel's own spot. Shadowed the same way |
| 30 | Object31 | 1 | 716 | static | ng01 | **fallen angel-knight** lying by the statue (16.1, 96.3) |
| 31 | Object32 | 5 | 774 | static | woodc01 + ng01 | **angel-knights crucified on wooden crosses** in the courtyard |
| 32 | Object33 | 6 | 6 | static | light_R | **blue light shafts** (court 2, castle 4) |
| 33 | Object34 | 20 | 16 | static | ju01_R | **flat glow and feather decals** on the floor (court 8, castle 12) |
| 34 | Object35 | 7 | 6 | static | br001 | wall tops or merlons on y 77 |
| 35 | Object36 | 2 | 40 | static | ses | **chain winch or pulley drums** either side of the door (11.4 / 17.4, 75.8) |
| **36** | Object37 | 1 | 96 | static | wcd + pant01 + ses | **the drawbridge door**, 3.3 × 6 m, hinged at 14.5,76.1. Falls over the gap (§2) |
| **37** | Object38 | 9 | — | — | **file missing** | smoke and cloud emitters (§2): court 1, bridge 3, gap 1, castle 4 |
| — | Crow01 | boid | 48 | 12 bones, 2 actions × 5 keys | bird.tga | MODEL_CROW (`MapManager.cpp:167-168`) |
| — | Gate01, 02 | effect | 60 / 28 | static | doorc01 | **door debris**, MODEL_GATE / +1 (`ZzzOpenData.cpp:4305-4306`), burst from the gate's mesh as it dies |
| — | StoneCoffin01, 02 | effect | 28 / 14 | static | skR (Monster folder) | **statue debris**, MODEL_STONE_COFFIN (`ZzzOpenData.cpp:4307-4308`) |
| — | Shine01 | — | 2 | static | light2_R | **never loaded** by MuMain (no reference) |

- **Animated kinds:** Object11 (chains), Object15/16 (chains), Object29/30 (angel knights) and Crow01. Everything else is static.
- **Total:** 210 placements, small enough to build every kind.
  - Recipe priority by visibility: Object21/26/35 front wall, the Object01-05 parapets, Object37 + Object10/11 (the bridge set piece), Object17 gargoyles, Object29/30 angel statues, Object12 candles, Object14 monks, Object32 crosses, Object24 pillars.
  - Then the rubble and decal tail.

---

### 2. MuMain's special cases for Blood Castle

The greps cover `WD_11BLOODCASTLE`, `InBloodCastle` (60 hits) and every Object12 or BC model enum. Everything that changes the look is below.

**Frame, light, fog**
- Clear and fog are black (the default, `SceneManager.cpp:401-402`). There is no sky, no weather (no rain or leaves; `ZzzLodTerrain.cpp:2082` drops grass) and no camera arm.
- `Effect/clouds.jpg` is loaded as BITMAP_CLOUD (`MapManager.cpp:172`) for the Object38 emitters' clouds, not a sky.
- There is no AddTerrainLight and no CreateFire on any castle type. TerrainLight plus sprites carry the whole look.
- **Floating motes everywhere:** `MoveObjectSetting`, `ZzzObject.cpp:4365-4381`.
  - One BITMAP_FLARE particle (SubType 3, scale 0.19) one frame in 4.
  - It spawns within (-300..+600) of the hero on x and y, at 250-300 above him.
  - LifeTime 60, Gravity 1-5 (`ZzzEffectParticle.cpp:764-770`).
  - The air always drifts with faint white sparks.

**Object visuals** (`RenderObjectVisual`, `ZzzObject.cpp:3195-3240`)
- **Type 11, Object12 candles:** seven BITMAP_LIGHT sprites at bones 1, 2, 4, 6, 9, 10, 11, size 0.5.
  - Colour `L·(1, 0.5, 0)`, with `L = sin((Angle[2]·20 + WorldTime)·0.001)·0.5 + 0.5`.
  - A slow orange breathe, phased per placement.
- **Type 13, Object14 monk statues:** BITMAP_FLARE at bone 3, white, `L = sin(WT·0.001)·0.3 + 0.7`, size L + 0.5 (the lamp they hold).
- **Type 37, Object38 emitters** (no model). Every other frame, on a 4-tick cadence:
  - BITMAP_ADV_SMOKE+1 and BITMAP_ADV_SMOKE (sub 0)
  - BITMAP_CLOUD (sub 6) and BITMAP_ADV_SMOKE (sub 1)
  - a pink-white BITMAP_FLARE `(1, 0.8, 0.8)` (sub 4, 0.19)
  - They are rolling mist sources at 9 points: court, bridge, gap and courtyard.

**Object render arm** (`Draw_RenderObject`, `ZzzObject.cpp:1074-1089`)
- **Types 28, 29 (Object29/30 angel knights):** a normal draw, then `RenderBodyShadow` in flat black at terrain height with mesh 2 hidden. A planar drop shadow, the only placed objects that cast one.

**The drawbridge** (`ActionObject`, `ZzzObject.cpp:86-165`, driven from `MoveObjectSetting` at `:4436/4453`, with `g_iActionTime--` once a frame at `:4464`)
- Trigger: `SetActionObject(world, 36, 20, 1)` on PlayState 3 (`NewBloodCastleSystem.cpp:68-70`).
- Frame "20" sets Angle[0] = 35, unhides Object37 and plays **SOUND_DOWN_GATE** (`eDownGate.wav`, 167 KB, `ZzzOpenData.cpp:4838`).
- Each frame after, Angle[0] += v, with v growing by 1.5 a frame. On passing 90 it bounces back by the remaining frame count and v resets to 2.
- At exactly 80° (a float equality, so rarely hit) it throws 10 smoke02 puffs ±150 at y −600.
- At time 0:
  - Object37 is hidden at 90°.
  - Object10/11 show (PKKey 4, `:107-123`).
  - NoGround is cleared on 13,70 + 3×6 (`:163`).
- MoveObject keeps types 9 and 10 hidden until then (`:4185-4199`).
- A late joiner who receives the NoGround packet gets the end state at once (`WSclient.cpp:8724-8727`, lifetime 0).

**The Castle Gate (131) and the Saint Statue (132)** (`ZzzCharacter.cpp:13408-13439`)

Gate (MODEL_CASTLE_GATE, `Monster62.bmd`):
- Scale 0.8, no shadow, does not turn when hit. Drawn 60 units forward in y (`ZzzObject.cpp:268-274`). Bounding box (-120..100, 0..300) (`ZzzCharacter.cpp:11824-11827`).
- Hit: 5 smoke02 puffs ±64 at +200-250 up, plus **eHitGate.wav** (`ZzzCharacter.cpp:1435-1448`).
- Death (`ZzzObject.cpp:1442-1447`): the model stops drawing and **eHitGate2.wav** plays.
- On death `RenderMeshEffect(0, MODEL_GATE)` bursts Gate01/Gate02 debris from the mesh's vertices: Gate02 one vertex in 12, Gate01 one in 50, at light 0.2 (`ZzzBMD.cpp:2103-2127`). Their motion is at `ZzzEffect.cpp:3038-3060`.

Statue (MODEL_STATUE_OF_SAINT, `Monster61.bmd`, skR.jpg):
- Scale 0.8 (the linked weapon is drawn at scale 0.7), no shadow, drawn 120 forward. Bounding box ±90 × ±50 × 200.
- It holds the **Divine Staff of Archangel** (Staff11.bmd) in a Wing slot, play speed 0.2 (`ZzzCharacter.cpp:8952-8971`). BC2 holds the Divine Sword and BC3 the crossbow.
- Drawn three times (`ZzzObject.cpp:1419-1440`):
  - textured
  - RENDER_BRIGHT|CHROME at white
  - RENDER_BRIGHT|METAL at `(0.3, 0.3, 1)`
- The result is a glowing **blue chrome sheen**.
- For its first second alive, and on death, it renders as a StoneCoffin01/02 debris burst plus eHitGate2.wav instead (`ZzzBMD.cpp:2077-2101`).
- Hits on it throw particles too (`ZzzCharacter.cpp:4868-4880`).

**NPCs.** Both are `OpenNpc` from `Data/Npc/BloodCastle{1,2}` (`ZzzOpenData.cpp:1919-1926`), scale 1, KIND_NPC (`ZzzCharacter.cpp:14350-14363`).
- **Archangel (232)** = `BloodCastle01.bmd`: 785 tris, 49 bones, 12 keys. Sheets ng01, skin_barbarian_01, **lnn01.tga** (wings, alpha) and ju01_R. At 10,9 (WZ MonsterSetBase).
- **Messenger (233)** = `BloodCastle02.bmd`: 834 tris, 46 bones. He stands in Devias, not in the castle.
- Both have mesh 0 drawn again added (RENDER_BRIGHT, `ZzzObject.cpp:2644-2647`) and mesh 2 hidden (`ZzzCharacter.cpp:8684-8687`).
- Both stand on a pulsing blue `Magic_Ground2` decal, 2.7 wide, `L·(0.5, 0.5, 1)`, `L = sin(WT·0.0015)·0.3 + 0.8` (`:8697-8711`).

**Boids: crows** (`GOBoid.cpp:1310, 1342-1345`)
- MODEL_CROW flies with MoveBird, the same path as Lorencia's birds (`:1437-1439`).
- Each crow has **two red glowing eyes**: BITMAP_LIGHT at bone 1 ±5, size 0.1, `L·(1, 0.2, 0)`, L 1.28-1.59 (`:1573-1585`).
- **eCrow.wav** plays 1/128 a frame, but only over tiles whose attribute is exactly TW_SAFEZONE (`:1495-1501`). So the crows caw over the court.
- A crow dies at LifeTime ≤ 0 in the castle (`:1025-1031`). The `CreateDragon` BC branch (`:872`) is dead code, since that function returns off Heaven (`:826`).

**No falling stones and no fire on the bridge in MuMain.**
- The "falling" a player remembers is three things:
  - the death fling off TW_ACTION tiles (above)
  - the drawbridge
  - the gate and statue debris bursts
- WZ adds no client effect.

**Sounds** (presence in `MU2_BGFX/source/sounds`):

| file | have? | use |
|---|---|---|
| iBloodCastle.wav (4.7 MB) | **no** | match loop (`MapManager.cpp:173`, `NewBloodCastleSystem.cpp:42/64`) |
| eDownGate.wav | **no** | drawbridge |
| eHitGate.wav / eHitGate2.wav | **no** | gate hit / gate and statue break |
| eCrow.wav (8 KB) | **no** | crows over the court |
| aCastleDoor.wav | yes | not BC (siege) |
| mOrcArcherAttack1, mOrcCapAttack1 | **no** | Chief Skeleton Archer / Warrior (`ZzzOpenData.cpp:3718-3729`) |
| mBlackSkullAttack, mBlackSkullDie | **no** | Dark Skull Soldier (`:3802-3807`) |
| mGhaintOrgerDie | **no** | Giant Ogre (`:3809-3812`) |
| mRedSkull, mRedSkullAttack, mRedSkullDie | **no** | Red Skeleton Knight (`:3814-3818`) |
| mMagicSkull | **no** | Magic Skeleton (`:3793-3796`) |
| mHunter2, mBullDie | yes | shared |

---

### 3. Monster and NPC assets for BC1

Spawns are from WZ `msb.txt` (map 11; OM `VersionSeasonSix/Maps/BloodCastle1.cs` agrees). BC1 has 95 monsters:
- 19 Chief Skeleton Warrior (84) and 17 Chief Skeleton Archer (85), all on the bridge
- 24 Dark Skull Soldier (86) and 11 Giant Ogre (87), bridge
- 16 Red Skeleton Knight (88), the gap and courtyard
- 8 Magic Skeleton (89), courtyard
- Gate 131 at 14,75, Statue 132 at 14,95, Archangel 232 at 10,9

The model file is `Monster{MONSTER_MODEL+1}.bmd` (`_enum.h:4196-4212`). Exports are in `S/mon/`.

| # | name | CreateMonster | file | scale | weapons | tris / bones | MU2_BGFX |
|---:|---|---|---|---:|---|---|---|
| 84 | Chief Skeleton Warrior | `ZzzCharacter.cpp:13440-13451` | **Monster48** (MODEL_ORC: green-hooded soldier, kni00/01/02) | 1.1 | none | 657 / 40, 8 actions | **missing** |
| 85 | Chief Skeleton Archer | `:13453-13466` | **Monster47** (MODEL_ORC_ARCHER) | 1.1 | Battle Bow = Bow04 +1 | 609 / 40 | **missing** (Bow04 cooked) |
| 86 | Dark Skull Soldier | `:13468-13483` | **Monster60** (npc_b1 + bons) | 1.0 | two Crescent Axes = Axe09 | 710 / 57 | **missing** (Axe09 cooked). Link bones 33/20 (`:11950-11953`) |
| 87 | Giant Ogre | `:13485-13496` | **Monster59** (st091) | 0.8 | none | 1934 / 70 | **missing** |
| 88 | Red Skeleton Knight | `:13498-13511` | **Monster58** (hop01 + bons) | 1.19 | Chaos Dragon Axe = **Mace07**: **+8 at BC1-2**, +0 from BC3 (`!int((7 + (World - WD_11BLOODCASTLE_END)) / 3)`, `:13505-13508`) | 1054 / 44 | **missing**, Mace07 not in source/items |
| 89 | Magic Skeleton | `:13393-13406` | **Monster63** (bsh01, bsh02/03.tga, bons) | 1.2 | Skull Staff = Staff01 +11 | 804 / 55 | **missing** (Staff01 cooked) |
| 131 | Castle Gate | `:13408-13414` | **Monster62** (doorc01) | 0.8 | — | 156 / 6, 7 actions (die animates) | **missing**, plus Gate01/02 debris |
| 132 | Statue of Saint | `:13416-13422` | **Monster61** (skR) | 0.8 | Divine Staff of Archangel = **Staff11** | 448 / 2 | **missing**, Staff11 not in source/items, plus StoneCoffin01/02 debris |
| 232 | Archangel | `:14350-14355` | **Npc/BloodCastle01** | 1 | — | 785 / 49 | **missing** |
| 233 | Messenger of Archangel | `:14357-14363` | Npc/BloodCastle02 | 1 | — | 834 / 46 | **cooked** (`source/npc/BloodCastle02.json`, `assets/cooked/figures/meshes/BloodCastle02.mum`, commit 83d8f729), standing in Devias at 217,29 |

- **To import:** Monster47, 48, 58, 59, 60, 61, 62, 63; Npc/BloodCastle01; Object12 Gate01/02, StoneCoffin01/02 and Crow01; items Mace07 and Staff11.
- Sheets to decode with a `bc_` prefix: lnn01, ng01 and ju01_R. The Messenger already has `npc_ng01` and `npc_ju01_r`, which may be shareable; check bytes.
- Monster textures live in `D/Monster`: kni00-02, hop01, st091, npc_b1, bsh01-03, skR, doorc01, bons.
- `br001.png` already exists in `source/textures` (charscene's Object80). Compare bytes before reusing.

---

### 4. A look direction

**What MuMain draws:** black void and clear, cold painted light, no music, and a scatter of small warm and white lights.

- **Court** (bright blue-violet 144/143/175):
  - angel-knight statues with drop shadows
  - the Archangel on a pulsing blue ground glow
  - crows with red eyes cawing overhead
  - blue light shafts
- **Bridge** (dim grey 78):
  - a three-wide plank walk over black
  - purple-to-black curtain planes in the chasm
  - swinging chains on the east parapet
  - orange-breathing candle clusters near the end
  - rolling mist
- **The door** glows (153). It is flanked by mossy gargoyles and chain winches.
- **Courtyard** (dim 76/73/87):
  - crucified angel-knights and a fallen one
  - hooded monks holding white lamps
  - scaffolds and ladders, rubble
  - the blue-chrome Saint statue at the head of the U
- **Air everywhere:** faint white sparks rising round the hero.

**It should feel like a cursed, moonlit fortress over an abyss:**
- cold blue key and black void
- small warm points: candles, the door glow, the crow eyes
- the statue and the Archangel as the two cool, bright landmarks

It is the opposite of the Lost Tower's warmth from below.

**House rules:**
- No music: MuMain plays none here either. `iBloodCastle.wav` is a match bed, which suits "music is rare and in fights".
- Night looks not too dark or foggy: start from the Dungeon's final sheet (exposure ~1.9, ambient ~1.6), not black. The void stays black because nothing is drawn there.

**Decisions for the user:**
1. Crop or block the 32k-tile filler plain.
2. Object38 has no model: build its 9 mist emitters as code (like `kNoriaGlows`).
3. Whether the TW_ACTION death fling is kept.
4. Shine01 and cs001 ("TEST") are unused: skip both.

---


---

## Part C: the engine side

Written 2026-10-02, read-only (no repo file edited, no cook, no window). Paths relative to
`MU2_BGFX/` unless marked. OM = `LEGACY/reference/openmu/src/`, MM =
`LEGACY/reference/MuMain/src/source/`, D = `LEGACY/reference/MuMain/src/bin/Data`.
Line numbers are HEAD c85c4b56 (2026-10-02) plus the working tree as found.
Template: `docs/lost-tower-port.md` Part C (written at e6864d1b) and the Lost Tower's step 1,
commit `1f05c029`. Every Part C location was re-read today; changes since are flagged **[moved]**
or **[new]**.

---

## 0. The record the engine has to carry (short; pass A owns it)

**No 0.75 source has Blood Castle.** OpenMU Version075 and Version095d have no BloodCastle map
(`OM/Persistence/Initialization/Version095d/Maps/` lists Devias, DevilSquare1-4, Icarus,
Lorencia, Noria, Tarkan only). The only OpenMU record is Season Six; WebZen's GameServer 1.00.93
(`BloodCastle.cpp`, GitHub ptr0x-real/Mu-GS-Webzen-MC-10093, not cloned locally) is the 0.97d
one and outranks it (memory: WebZen server source). So every rule below is S6 until pass A reads
WebZen.

- **Maps 11-17 are castles 1-7, one terrain.** MuMain loads `World12`/`Object12` for every
  castle: `InBloodCastle()` forces `iMapWorld = WD_11BLOODCASTLE1 + 1` (`MM/World/MapInfra/MapManager.cpp:1104-1106`,
  `:1590`), model loads for all seven at `:158-174` (MODEL_CROW from Object12, SOUND_BLOODCASTLE
  `iBloodCastle.wav` `:174`). Map 52 is the master-level eighth.
- **Raw data present**: `D/World12` = `EncTerrain12.att/.map/.obj`, `Terrain12.att`,
  `TerrainHeight.OZB`, `TerrainLight.OZJ`, one tile sheet pair `TileGrass01.OZJ/.OZT`;
  `D/Object12` = Object01-37, Gate01/02, StoneCoffin01/02, Shine01, Crow01 + 26 textures;
  `D/Npc/BloodCastle01.bmd`, `BloodCastle02.bmd` (Archangel, Messenger); `D/Music/castle.mp3`;
  `D/Sound/iBloodCastle.wav`, `blood1.wav`, `blood_attack1/2.wav`, `blood_die.wav`,
  `eBloodAttack.wav`.
- **SafezoneMap is Devias** (`OM/Persistence/Initialization/VersionSeasonSix/Maps/BloodCastleBase.cs:35`):
  a death or a portal in the castle lands in Devias -- unlike the Lost Tower, like the Dungeon
  but to Devias, not Lorencia.
- **Entry**: exit gate 66 `CreateExitGate(maps[11], 12, 5, 14, 10, 0)` (`OM/.../VersionSeasonSix/Gates.cs:216`),
  **not** a spawn gate (no `true`). Entered from the Messenger (233) with an Invisibility Cloak
  (13,18) whose level is the castle number (`OM/.../Events/BloodCastleInitializer.cs:214-215`).
- **Timing** (`BloodCastleInitializer.cs:208-210`): enter window 1 min, game 20 min, exit 1 min;
  10 players; opens every 2 h (`OM/GameLogic/PlugIns/PeriodicTasks/BloodCastleStartConfiguration.cs:21-22`,
  already our `kEvents` row).
- **Level brackets** (`:144-193`): castle 1 is 15-80 (MG/DL 10-60), 2 81-130, 3 131-180,
  4 181-230, 5 231-280, 6 281-330, 7 331-400. With our cap and pace only castles 1-2 matter for now.
- **The run, as five terrain flips** (`BloodCastleInitializer.cs:226-320`, each a
  `MiniGameTerrainChange` the client mirrors through `ReceiveSetAttribute`,
  `MM/Network/Server/WSclient.cpp:8714-8740`, `AddTerrainAttributeRange`,
  `MM/Render/Terrain/ZzzLodTerrain.cpp:280`):
  1. event start: clear Blocked on the entrance 13,15-15,23, behind the gate 11,78-25,89 and the
     altar 8,78-11,83;
  2. **kill quota** -- 40 kills of any monster per player -> "Enough monster kills, now destroy
     the Castle Gate!", clear NoGround on the bridge 13,70-15,75 (the client also plays
     `SetActionObject(world, 36, ...)`, the bridge object's drop, `WSclient.cpp:8724-8727`);
  3. **the Castle Gate (131)**, spawned once at event start at 14,75 with 150 000 HP at castle 1
     (`BloodCastleBase.cs:17,47-48`), killed -> clear Blocked 13,76-15,79;
  4. 2 kills per player of the castle's Spirit Sorcerer (89 at castle 1, `:131`) -> spawn the
     **Statue of Saint (132)** at 14,95, 65 000 HP (`:21, :26, :291-305`);
  5. the statue drops the **Weapon of Archangel (13,19)**; carried back to the **Archangel (232)
     at 10,9** (`BloodCastleBase.cs:50`) it wins. Fail on time-out; everyone is sent to Devias.
- **Monsters castle 1** (`OM/.../Maps/BloodCastle1.cs`, 96 spawn rows): 84 Chief Skeleton
  Warrior, 85 Chief Skeleton Archer, 86 Dark Skull Soldier (lvl 66, 8000 HP), 87 Giant Ogre
  (lvl 70, Energy Ball), 88 Red Skeleton Knight, 89 Magic Skeleton (the Spirit Sorcerer), all
  one-tile `AutomaticDuringEvent`, respawn 10 s.
- **Rewards castle 1** (`BloodCastleInitializer.cs:31-41, :323-389`): 20 000 exp gate, 20 000
  statue, 5 000 success, 160 a remaining second; 20 000 / 10 000 zen win/lose; one Jewel of Chaos
  to the living winner.
- **The ticket** (`OM/GameLogic/PlayerActions/Craftings/BloodCastleTicketCrafting.cs:26-45`;
  `OM/.../VersionSeasonSix/ChaosMixes.cs:49, 778-785`): Chaos Goblin mix of Scroll of Archangel
  (13,16) + Blood Bone (13,17) of the same level + a Jewel of Chaos -> Invisibility Cloak (13,18)
  of that level, 80% at 50 000 zen (castle 1) .. 850 000 (7). Item rows
  `OM/.../VersionSeasonSix/Items/EventTicketItems.cs:29-32`. MuMain models `MODEL_HELPER + 16..19`
  (`MM/Core/Globals/_enum.h:2027-2030`).

### 0.1 The grid, from a scratch extraction

`python3 pipeline/terrain.py D/World12 bloodcastle <scratchpad>/bc/extract 12` (into the
scratchpad, nothing in the repo): height 0.00-3.03 m; 5 tile slots, **TileGrass01 97.2%**
(the castle's flagstones, as the Dungeon's and the tower's slot 0 -- grow nothing);
**210 placements, 35 kinds** (26 Object23, 20 Object34, 15 Object06 ...); **Object38 missing**
(no .bmd, 9 placements); light mean (87,87,87), neutral grey.

terrain.py says "83.2% walkable", which is misleading: 54 352 tiles of word 0 lie **outside**
the castle and are unreachable. Words: {0: 54352, 8: 4665, 4: 3686, 12: 2619, 1: 93, 32: 92,
36: 16, 40: 12, 14: 1}. 8-connected flood fill from the start room (13,8), clearing each flip
in turn:

| stage | open tiles reachable | extent |
|---|---:|---|
| as cooked (closed) | 93 | 8-19 x 3-14 -- the safe start room (word 1), Archangel at 10,9, exit gate 66 at 12-14 x 5-10 |
| + entrance, behind-gate, altar cleared (flip 1) | 258 | down the corridor to row 69 |
| + bridge NoGround cleared (flip 2) | 276 | to row 75, the gate's tile |
| + gate Blocked cleared (flip 3) | 566 | 8-27 x 3-96, the statue's room (14,95 is word 0) |

The boxes check out against OpenMU's: entrance 13-15 x 15-23 is words 36 (Action|NoMove) and 4;
bridge 13-15 x 70-75 is 40 (Action|NoGround) and 8; gate 13-15 x 76-79 all 4; behind-gate
11-25 x 78-89 all 4; altar 8-11 x 78-83 all 4. **So the whole event is 566 tiles in a 20 x 94
strip, and four rectangles of attribute bits.** The 54k outside tiles need the void treatment
(pass B: NoGround stamp or `VOID_BY_MAP`), or the router's nearestOpen and the minimap will see
open ground there.

---

## 1. Every place a new world must be named today

There is still **no single registry**. Name to use: `bloodcastle` (one word, as terrain.py's
folder). The Lost Tower's §2 refactor was done only in part: `MapRow` gained `underground`
(`src/game/world/maps.h:24-26`), but `air`, `grassy` and `boid` are still string tests.

### 1.1 Pipeline (source side)

| # | where (file:line today) | what | Blood Castle |
|---|---|---|---|
| P1 | `pipeline/terrain.py main` (args `<World> <name> <out> <map_number>`) | extraction | `python3 pipeline/terrain.py $D/World12 bloodcastle source/world 12` |
| P2 | `pipeline/terrain.py:118` `HIDDEN_BY_MAP` (keyed map_number-1 = **11**) | hidden types | types 9, 10 (`HiddenMesh = -2` unless PKKey 4, `MM/Engine/Object/ZzzObject.cpp:4185-4199`) -- pass B |
| P3 | `pipeline/terrain.py:142` `BLEND_MESH_BY_MAP` | additive types | pass B |
| P4 | `pipeline/terrain.py:158` `GRASS_BY_MAP` | grass slots | **`11: []`** (World12 ships TileGrass01.OZT but slot 0 is the floor, 97.2%) |
| P5 | `pipeline/terrain.py:178` `VOID_BY_MAP` **[new since Part C]** | void rim/abyss | probably `11: {...}` for the bridge chasm and the outside -- pass B |
| P6 | `pipeline/terrain.py:195, :244, :246` `LAVA_SPILL_BY_MAP`, `LIGHT_DEPTH_BY_MAP`, `WATER_FLOW_BY_MAP` **[new]** | | none |
| P7 | `pipeline/terrain.py:271` and `pipeline/index.py:44` `OPERABLE_BY_MAP` | seats | none |
| P8 | `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP` | keep gate tiles open | `11: [(12,5,14,10)]` (exit 66). NB `[4]` and Devias's tower boxes were never added (still only 0-3) |
| P9 | `source/world/bloodcastle/ground.json` -> `tools/content.sh` globs it | ground sheets | one slot in use; `bc_` prefix as `lt_`/`dn_`/`dv_` |
| P10 | `source/mu.db` `gates` | spawn box | **no row for map 11** (see §3.6: the death goes to Devias, not the start room) |
| P11 | `pipeline/index.py worlds()` `:1414-` | index | automatic once the ground is built |

### 1.2 The cook (`tools/cook.py`)

| key | line | Blood Castle |
|---|---|---|
| `AIRS` | `:202` | `"bloodcastle": "Crow01"` (MODEL_CROW, `MapManager.cpp:167-168`; the comment at `:195` already names "Blood Castle crows") |
| `AIRS_FROM` | `:213` | none -- Crow01 is in Object12 itself |
| `CRAWLS`, `TRAP_MODELS` | `:206`, `:209` | none |
| `FLAT_OVER_VOID` | `:1156` **[was the `:1463` test; now a set]** | `+ "bloodcastle"` if the castle sits over a void like the Dungeon/tower (pass B) |
| `LIGHT_REACH_BY_WORLD`, `OBJECT_LIGHT_DEPTH_BY_WORLD` | `:1158`, `:1164` **[new]** | only if the look asks |
| `ANCHOR_KINDS_BY_WORLD`, `HIDDEN_BY_WORLD` | `:1203`, `:1209` | only if a recipe asks |
| `figure_file` | `:1677` | automatic `figures_bloodcastle.json` |
| `RESPAWN_VERSION075` | `:1848` | none if mu.db's `respawn_seconds` is written true |
| `FOLK_VERSION075` | `:1875` (Devias block `:1925-1949`, the Messenger `:1946`) | `11: [(232, "Archangel", "BloodCastle01"?, 10, 9, dir)]` (`BloodCastleBase.cs:50`) |
| spawns of this map only | `:2118` `if spawn["map"] != number` | automatic |

### 1.3 The runtime -- every place a world is named (re-verified)

| # | file:line | holds | Blood Castle row |
|---|---|---|---|
| R1 | `src/game/world/maps.cpp:19-29` `kMaps` | `{world, number, arrive, underground}` | `{"bloodcastle", 11, {13, 8}, true}` -- arrive mid exit 66 (12-14 x 5-10), in the start room |
| R2 | `src/game/world/maps.h:18-27` `MapRow` | fields | **add `home`** (the SafezoneMap number; -1 = itself) -- §3.6 |
| R3 | `src/game/ui/arrival.cpp:57-62` `kPlaces` | banner | `{"bloodcastle", "Blood Castle", ""}` |
| R4 | `src/game/ui/minimap.cpp:278-287` `mapName` | "Gate to X" | `case 11: return "Blood Castle";` |
| R5 | `src/game/ui/minimap.cpp:261-276` `floorName` | stairs names | none (no same-map gates) |
| R6 | `src/game/world/maps.cpp:70-85` `placeName` | floor names | none (`return world` -- the banner shows the folder name unless R3's place name is what prints; check `desk.cpp` banner path) |
| R7 | `src/sim/realm_travel.cpp:14-29` `kRows`, `src/sim/travel.h:29` `kTravels = 13` | Tab warp list | **no row** -- an event map is not on the warp list (S6 has none; entry is the Messenger). Leave `kTravels` alone |
| R8 | `src/game/ui/travel.cpp:170` `kComing` | greyed future maps | not listed (BC is not a Tab place); its countdown already rides Devias's card, `:59` |
| R9 | `sheets/worlds/bloodcastle.json` | look, found by name (`maps.cpp:87-90`) | new; copy `sheets/worlds/losttower.json` or dungeon.json |
| R10 | `src/game/play_open.cpp:32-37` | `windy_`, `dungeonAir_`, `towerAir_`, `grassy_`, `snowy_` by name **[moved, grew: towerAir_ and grassy_ new]** | `windy_` false via `underground`; an air for the castle (MU: none named for BC in SceneManager -- pass B) |
| R11 | `src/game/world/world.cpp:94-97` `deviasFloors_`, `underground_` | room, leaves (`:213`) | free via `underground` |
| R12 | `src/game/world/boids.cpp:55-60` `boidOf`, `:63-86` `airsOf` | boid by name | `world == "bloodcastle" -> "Crow01"`; crow airs (GOBoid.cpp's crow arm -- pass B) |
| R13 | `src/app/modes/play_mode.cpp:636-640` takeHome -> `mapNumbered(0)` | death/portal with no safe box | **must become `mapNumbered(map->home)`** -- Devias (§3.6) |
| R14 | `src/app/modes/play_mode.cpp:862-886` music by name | | decision (castle.mp3) |
| R15 | `src/game/save.cpp:131, :253` `world` | where a save resumes | an event map must save as Devias (§3.6) |
| R16 | `src/game/headless.cpp:146-170` | `--world` load, `mapOf(world)->arrive` | free |
| R17 | `src/game/world/weather.cpp:113-116`, `ornaments.cpp:248/266/309`, `trap_show.cpp:82`, `skulls.cpp:36`, `lava_smoke.cpp:48`, `scurry.cpp:34`, `doors.cpp:44`, `portal.cpp:40` | other worlds' features by name | none -- each returns early for an unknown name |
| R18 | `src/sim/gates.cpp:9-39` `kExits`, `:44-76` `kEnters` | gates | exit 66 only, if entry goes through the gate machinery (§3.1) |
| R19 | `source/mu.db` | kinds, spawns | §4 step 4 |

The load path is unchanged from the tower's: `PlayMode::travel` (`play_mode.cpp:1347-1370`) ->
`World::open` (`world.cpp:90-`) -> `Play::open` (`play_open.cpp:24-`, `cooked/<w>/<w>.mur`,
breeds without a figure held back `:142`) -> `realm_.raise(&tables_, ...)` (`play_open.cpp:148`).

### 1.4 What the Lost Tower's step 1 actually touched (`git show --stat 1f05c029`)

31 files, +81 376 / -5. The recipe, in order:

1. **pipeline/terrain.py** (+8): `HIDDEN_BY_MAP[4]`, `BLEND_MESH_BY_MAP[4]`, `GRASS_BY_MAP[4] = []`.
2. **Extraction into the repo**: `source/world/losttower/{attributes,height,light,tiles}.png` and
   `losttower.json` (81 220 lines of placements).
3. **ground.json** (+42) and its used sheets as `source/textures/lt_*.png` (10 files).
4. **mu.db**: the spawn gate row 42 (binary diff, same size).
5. **sheets/worlds/losttower.json** (+59): the Dungeon's numbers re-tuned (key 1.4, fill 2.2,
   exposure 2.6, mist 0.005, sheen 0) plus a new knob.
6. **Game rows**: `maps.cpp` kMaps row (+12 incl. comments and `placeName`'s seven floors),
   `maps.h` `bool underground = false` (+3), `world.cpp` `underground_ = row && row->underground`,
   `play_open.cpp` `windy_` via the flag, `arrival.cpp` kPlaces (+1), `minimap.cpp` floorName +
   mapName (+9).
7. **A new look knob end to end** (only because lava needed one): `lighting.h/.cpp` (`water_glow`
   read and listed among known keys), `renderer.h/.cpp/_setup.cpp` (uniform create, set, destroy),
   `shaders/fs_ground.sc` (+6).
8. Not in the commit but implied by its message: `tools/content.sh --world losttower`, sync,
   `tools/cook.py --world losttower --only ground` and `--only tables`, `pipeline/index.py`
   (build-only skips the index -- memory). No gates, objects, monsters or air in step 1.

**Blood Castle's step 1 is the same minus 4 and 7**: no mu.db row (Devias is the safe map), no
new shader knob unless the look asks.

---

## 2. What already exists for Blood Castle

| piece | where | state |
|---|---|---|
| **Messenger of Archangel (233)** in Devias | `tools/cook.py:1943-1946` `(233, "Messenger of Archangel", "", 217, 29, 4)` (S6's tile, `VersionSeasonSix/Maps/Devias.cs:45`) | stands, cooked figure (its plate is drawn opaque+added, `pipeline/export_gltf.py:1870`) |
| his number | `src/sim/realm.h:140-142` `kMessenger = 233` | |
| his talk | `src/sim/realm.cpp:596-609`: grouped with Guild Master/Sevina/Charon/Thompson -> `What::Shouted` `Shout::Greet` | the hook a "enter Blood Castle" branch replaces |
| his lines | `src/game/play.cpp:1668-1673` `kMessenger[]` ("not open yet"), chosen at `:1715-1721` | three not-ready lines |
| test | `tests/sim_test.cpp:3889-3908` "walked to the Messenger and he said Blood Castle is not ready" | must change when he opens a door |
| **countdown** | `src/game/ui/travel.cpp:47-60` `kEvents = {{"Blood Castle", 2, 7200, 1200}, {"Devil Square", 3, 14400, 1500}}`, drawn `:199-215` and `:386-405` off `realm.wallClock()` (`src/sim/realm.h:748-749`, set each frame `src/game/ui/desk.cpp:185`) | UI only: gold count-down 30 min before, red "now" for 20 min, on the Devias card. **Nothing gates on it.** Its map is 2 (Devias), not 11 |
| **Chaos Goblin (238)** | `tools/cook.py:1906` in Noria at 180,103; Noria's Chaos Machine sparks `src/app/modes/play_mode.cpp:993-1000`, `src/game/world/ornaments.h:68` | **decoration only** -- no mix. `src/sim/items.h:13` "No refining, no chaos machine, no trade"; `:307-310` `kRefineCap = 9` "until the Chaos Machine exists" |
| Jewel of Chaos (12,15) | `source/items/misc/Jewel15.json`, drop rules `src/sim/items.h:505-515`, a quest pay `src/sim/quests.cpp:673` | exists, droppable |
| Invisibility Cloak 13/18, Blood Bone 13/17, Scroll of Archangel 13/16, Weapon of Archangel 13/19 | `source/items/**` (group 13 has Helper01-04 only: Angel, Imp, Uniria, Dinorant); mu.db `items` holds groups 0-11 and 14 only (166 rows) | **none exist** -- no recipe, no model export, no table row |
| Divine Archangel weapons (0/19, 2/13, 4/18, 5/10) | MuMain `_enum.h:1514-1604` | none |
| **Archangel NPC (232)** | `MM/Engine/Object/ZzzCharacter.cpp:14350-14356` (`MODEL_NPC_ARCHANGEL`, `D/Npc/BloodCastle01.bmd`) | not in cook.py, not in sim |
| BC monsters 84-89, 131, 132 | mu.db `monster_kinds` max number is 41 | none |
| music/sound | `D/Music/castle.mp3`, `D/Sound/iBloodCastle.wav`; `source/sounds/sounds.json` has no BC entry (only `acastledoor.wav`, `:1324`, Devias's door) | none |
| Devil Square's Charon (237) | `src/sim/realm.h:148`, `play.cpp:1674-1679`, `tests/sim_test.cpp:4473-4500` | same "not ready" shape; whatever event machinery BC gets, DS reuses |

---

## 3. Blood Castle is an EVENT map: what the engine lacks, and what it can reuse

Every map so far is open: entered by a gate or Tab, populated once at `Realm::raise`
(`src/sim/realm.cpp:70-`), every dead monster rising on its own clock (`realm.cpp:833-835`,
`raiseBeast` `realm_fight.cpp:1275-`), left by a gate, a death or a portal. Blood Castle needs
seven things none of the maps has.

### 3.1 Entry from the Messenger (lacks; small)

- Today a talk with 233 ends in `Shout::Greet` (`realm.cpp:596-609`). Needed: a branch that
  checks the cloak (or not -- decision), the event window, the level bracket, takes the cloak,
  and emits a map change.
- **Reuse the gate path**: the mode already turns a `Gated` happening into a map change with a
  landing tile and facing (`play_mode.cpp:603-620`: `enterGateNumbered` -> `exitGate(target)` ->
  `mapNumbered(out->map)` -> `travel`). A box-less enter gate row in `src/sim/gates.cpp`
  (e.g. number 66+1000 or a `kMessengerGate` constant, map 2, an empty box so `enterGateAt`
  never matches it, target 66) plus exit 66 `{66, 11, {12,5,14,10}, dx, dy}` lets the Messenger
  branch call the same `throughGate` tail (`realm_move.cpp:211-253`) and say `Gated`. No new
  happening kind, no mode change. Refusals (`Barred` with a level, or a new `Shout`) reuse the
  refusal text at `play.cpp:485-490`.

### 3.2 A run with a clock (lacks; the core new piece)

- The sim has a tick (`tick_`, 20 Hz) and a wall clock (`wall_`, `realm.h:748-749`) but no
  map-scoped state machine. Needed in the realm: an `Event` struct {castle, phase
  (Entering/Running/Won/Failed/Leaving), startTick, endTick, kills, gateDown, statueUp,
  bridgeOpen} raised by `raise()` when `tables_->map` is an event map, stepped once a tick in
  `Realm::step` beside `fireTraps()` (`realm.cpp:798`), emitting new happenings
  (`EventPhase`, `EventKills`, `EventExpel`) the drawing and the log read.
- **Where it lives**: a new `src/sim/realm_event.cpp` + `src/sim/event.h` (the table: boxes,
  quotas, durations, per-castle numbers), as `realm_traps.cpp` + `traps.h` do for traps. The
  realm already splits by concern (`realm_*.cpp`); `realm.h` grows one member and four methods.
- **Single player clock**: real time (event windows off `wall_` like `kEvents`) or a run timer
  from the moment he enters (ticks). Decision 1.
- **Game side**: a countdown plate. Precedent: Go Back!'s 5-minute plate (`src/game/ui/go_back.h:24`,
  `GoBack` state `src/app/context.h:97`) -- a click-free timer over the HUD. MuMain's own is
  `MM/UI/NewUI/Events/NewUIBloodCastleTime.cpp`.

### 3.3 Kill quota (partly there)

- The kill hook exists: `Realm::countKill` (`src/sim/realm_quests.cpp:92-`), called from the
  death path (`realm_fight.cpp:1176-1181`) with the same "his kill" rule. Add one line there to
  `eventKill(dead)`, which counts any monster for flip 2 and the Spirit Sorcerer (89) for flip 4.
- The tracker UI (`src/game/ui/tracker.cpp`) draws quest counts; an event line can ride it.
- **No-respawn**: S6 respawns during the event (10 s); with a quota of 40 and 96 spawns at castle
  1 that is fine. But after `Failed`/`Won` nothing should rise: gate the `risesAt` branch
  (`realm.cpp:833`) on the event phase.

### 3.4 A breakable gate and a statue (mostly there)

- A monster with `moveRange 0` never wanders (`realm_move.cpp:332`), with `viewRange 0`/no damage
  never fights; HP overrides (150 000 / 65 000) are just kind rows. So 131 and 132 can be ordinary
  breeds in mu.db with `respawn_seconds` huge, as long as **they are not raised at map raise when
  they should not be**: the statue spawns only on flip 4. The realm has no "spawn later" nest;
  the nearest precedent is the summon (`src/sim/realm_summon.cpp`, a body pushed into `bodies_`
  mid-run). Needed: a nest flag `dormant` (cook writes it from a mu.db column, or the event table
  names the nest), skipped by `raise` and woken by the event.
- `NotRotateOnMagicHit`, no shadow, scale 0.8 (`MM/Engine/Object/ZzzCharacter.cpp:13408-13438`)
  are drawing flags; the knock (`What::Shoved`) must not move a gate -- check `shove` for a
  `moveRange 0` exemption.
- Figures: `D/Monster/Monster131.bmd`.. per MuMain's `MONSTER_MODEL_CASTLE_GATE` /
  `_STATUE_OF_SAINT` -- the cook's figure path, one breed at a time.

### 3.5 Terrain that changes at runtime (lacks; the one structural change)

- **Can tile walkability change after load? Not today.** `content::Grid`
  (`src/content/grid.h:54-83`) has `set()` and `clear()` for the whole grid and no per-tile
  write. The realm holds `const content::Tables* tables_` (`realm.h:1016`) and the router a
  `const content::Grid*` (`src/sim/route.h:42-43`, `route.cpp:37-50`). The owner is
  `Play::tables_` (`src/game/play.h:813`, non-const) and headless's local `tables`
  (`headless.cpp:151`).
- **Good news: nothing caches the grid.** The router reads `grid_->open()` on every step
  (`route.cpp:57, 70, 85, 99, 131, 143`) and keeps only per-search scratch, so a word changed
  between ticks is seen by the very next plan. 45 sim call sites read `tables_->grid` live.
  The only derived cache is `Realm::settleFound`'s floor flood-fill (`realm_travel.cpp:66-`),
  which runs only on maps with more than one travel row -- not this one.
- **Smallest change**: `Grid::change(x1,y1,x2,y2, uint16_t bits, bool set)` (MU's
  `AddTerrainAttributeRange`), and the realm keeps a **non-const** `content::Grid*` for it -- or,
  cleaner, the realm owns a copy of the grid (`Grid live_`, copied in `raise`, 64k words = 128 KB)
  and the router opens that. The copy keeps `Tables` const and makes a re-raise (fail and retry)
  restore the cooked words for free.
- **Drawing**: the client's ground does not read the grid per frame for walkability; the bridge
  is drawn by its object (type 36, Object37) dropping (`WSclient.cpp:8724-8727`). The game needs
  to animate that one placement on the `EventPhase` happening (the town is static instanced
  chunks; Devias's doors `src/game/world/doors.cpp:40-` are the precedent for moving a placed
  instance), and the minimap's open/blocked layer redrawn once.

### 3.6 Failure, expel and the way home (lacks a field)

- **Death and portal go to Lorencia today** when the map has no safe box: `Rose` with `c = 1`
  (`realm_fight.cpp:1265-1272`), `Warped` with `c = 1` (`realm_items.cpp:609-611`), both ->
  `homeOwed_` (`play.cpp:316`, `play_requests.cpp:96`) -> `takeHome` (`play.h:400-404`) ->
  `mapNumbered(0)` (`play_mode.cpp:636-640`). Blood Castle's SafezoneMap is **Devias**: add
  `int home` to `MapRow` (Dungeon 0, Blood Castle 2) and use it at `play_mode.cpp:637`. Do not
  give map 11 a mu.db spawn row: its start room is safe-flagged (93 tiles) but is not where a
  death lands.
- **Expel**: at the end (won after the exit minute, or failed) the event emits the same home
  request; the mode's existing path takes him to Devias's spawn gate (`maps.cpp:22` arrive
  207,42). Landing where the Messenger stands (217,29) is a nicer touch -- an exit gate row in
  Devias.
- **Save/quit inside**: `save.cpp:253` writes `world`; a resume would raise the castle with no
  run. Rule: an event map saves as its `home` world (one line where the save is written).
- **Go Back!** (`play_mode.cpp:624-633`) must not open into an event map: `goBack.arm` on a portal
  read in the castle would offer a way back into a finished run. Clear it on an event map.

### 3.7 A quest item carried to an NPC (lacks)

- Quests have only `Clear` and `Return` steps (`src/sim/quests.h:31-37`); a hand-in pays, it never
  takes an item. The Weapon of Archangel is: a drop of the statue (`Dropped`), picked into the
  bag (`Picked`), and spoken to the Archangel with it in the bag -> removed, run won.
- Smallest: the Archangel (232) gets a talk branch in `realm.cpp:585-609` beside the Messenger's:
  "has 13/19 in the bag and the event is Running" -> take it, `Won`. The item row needs only a
  model and a 1x2 bag size; it is never sold or worn. No quest-system change.
- S6 drops it only to the statue's killer and loses it on death (dropped on the floor) -- with one
  player both are moot.

### 3.8 What the sim has that does NOT help

- **Traps** (`src/sim/traps.h:1-75`, `realm_traps.cpp`): no body, never die, fire on a clock --
  not a gate. Their pattern (a table in code + `raise*()` + `fire*()` in step) is the template for
  the event file, nothing more.
- **Same-map gates** (`Climbed`, `realm_move.cpp:242-279`): the castle has none.
- **Quests** (`quests.cpp`): 12-hour repeat clock (`QuestProgress::availableAt`) is a model for
  "one run per window" if wanted; nothing else.
- **Tab travel**: event maps stay off it (R7).

### 3.9 Chaos Machine (lacks entirely)

No mix code exists (`items.h:13`). The ticket mix (Scroll + Bone + Jewel of Chaos -> Cloak, 80%,
50k at castle 1) is the smallest possible Chaos Machine: one recipe, one roll, no item options.
It also unblocks +10/+11 refining later (memory: Refine.Cap). Either build that, or make the
cloak a drop/quest reward, or let the Messenger ask nothing (decision 2).

---

## 4. Build order, small steps, each with a headless check

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. No window without asking;
`--mute` on any review run. `checks` = `cmake --build build --target checks` (layercheck,
cooked_test, placement_test, sim_test, boids_test, sound_test, matcheck; `CMakeLists.txt:396-405`).

**Step 1 -- bare land, a sheet, a kMaps row; reached by `--world` / `--travel-at`.**
- `pipeline/terrain.py`: `GRASS_BY_MAP[11] = []`; `HIDDEN_BY_MAP[11] = {9, 10}` only if pass B
  agrees (ZzzObject.cpp:4185-4199); `pipeline/index.py` `GATE_BOXES_BY_MAP[11] = [(12,5,14,10)]`.
- `python3 pipeline/terrain.py $D/World12 bloodcastle source/world 12` (expect: 0.00-3.03 m,
  5 slots, 83.2% "walkable" (see §0.1), 0.1% safe, 210 placed / 35 kinds, missing Object38);
  `source/world/bloodcastle/ground.json` with `bc_` sheets; `tools/content.sh --world bloodcastle`;
  sync; `tools/cook.py --world bloodcastle --only ground` and `--only tables`; `pipeline/index.py`.
- `maps.cpp` row `{"bloodcastle", 11, {13, 8}, true}`; `arrival.cpp` kPlaces; `minimap.cpp`
  `case 11`; `sheets/worlds/bloodcastle.json` from losttower.json.
- No mu.db row; no Tab row. Reached by `--world bloodcastle` (headless) or `--travel-at` (window,
  `play_mode.cpp:643-648`, `mapAfter` -- it is last in `kMaps`, so after the Lost Tower).
- Check: `./run.sh --headless --world bloodcastle --ticks 200 --no-hand` logs
  `tables: 0 breeds ... grid 256 tiles a side (... blocked), map 11`; the `.mur` header's safe box
  is zero (python struct read, as the tower's check); the hero raises in 8-19 x 3-14.
  `checks` green apart from the known three.

**Step 2 -- the way home is Devias (`MapRow.home`).**
- `maps.h` `int home = 0`; Dungeon 0, Blood Castle 2; `play_mode.cpp:637` `mapNumbered(map->home)`;
  the save writes `home`'s world from an event map (`save.cpp:253`).
- Check: sim_test -- raise on bloodcastle.mur, kill the hero -> `Rose` with `c == 1`; a headless
  run that portals logs the travel to devias (add a `--die-at`-style arg only if none exists; else
  a sim_test on `Warped.c`).

**Step 3 -- the live grid.**
- `content::Grid::change(...)`; the realm owns `Grid live_` copied at `raise`, router opened on it,
  every `tables_->grid` read in `src/sim/*.cpp` (45 sites) and `audit.cpp` moved to `grid()`.
- Check: the seeded headless logs for lorencia/dungeon/losttower `--ticks 2000 --seed 7` are
  byte-identical before and after (pure refactor); sim_test: on bloodcastle.mur, `nearestOpen`
  from 13,8 cannot reach 14,40; after `change(13,15,15,23, kNoMove, false)` + the two others, a
  WalkTo 14,40 plans.

**Step 4 -- castle 1's monsters and the two event breeds.**
- mu.db kinds 84-89, 131, 132 (S6 numbers until WebZen is read), 96 one-tile spawn rows for map 11
  from id 834 (current max 833), `respawn_seconds` true; checkpoint (memory: WAL); index.py;
  `--only tables` for all **six** worlds (memory: new items/kinds need every world's tables).
- The `dormant` nest flag for the statue (132), and 131 at 14,75 raised once.
- Check: headless `--world bloodcastle --ticks 3000 --level 40` reports 97 monsters (96 + gate)
  and no statue; held-back list in the window log until figures are cooked.

**Step 5 -- the run: clock, quota, flips, statue, expel.**
- `src/sim/event.h` (castle table: boxes from §0, quotas 40 / 2, 20 min, rewards) +
  `realm_event.cpp`; `countKill` -> `eventKill`; the phase gates `risesAt`; new happenings.
- Check (sim_test `testBloodCastle`, scripted kills by setting monster health as other tests do):
  entrance closed at raise -> open after `EventPhase Running`; 39 kills bridge closed, 40th opens
  it (WalkTo 14,75 plans); killing 131 opens 13-15 x 76-79; two 89 kills raise 132 at 14,95;
  time-out emits `EventExpel` with home 2. All in ticks, no window.

**Step 6 -- the Messenger's door and the Archangel's hand-in.**
- `realm.cpp:596-609`: 233 -> the event entry (box-less enter gate -> exit 66, §3.1); 232 at 10,9
  in `FOLK_VERSION075[11]`; Weapon of Archangel (13,19) item row and model; the statue drops it;
  the Archangel takes it -> `Won` -> rewards (exp, zen, Jewel of Chaos) -> expel after a minute.
- Replace `tests/sim_test.cpp:3889-3908` (the "not ready" test) with: outside the window he still
  refuses; inside it, `Gated` lands in 12-14 x 5-10 of map 11.
- Check: sim_test end to end on the two `.mur`s; the window's first look only when the user asks.

**Step 7 -- tickets (decision 2).** Cloak/Bone/Scroll rows and models; either the Chaos Goblin's
one-recipe machine (new `realm_mix.cpp`, sim_test on the 80% roll with a fixed seed) or a drop.

**Step 8 -- the look and the show.** Objects (35 kinds, pass B), crows (`AIRS`, `boidOf`), the
bridge's drop animation, the gate's and statue's figures, the countdown plate, sounds
(`iBloodCastle.wav` at start, memory: new sound needs the showing cook), and the 54k outside tiles'
void. Check: `--budget --stats` once bare and once full.

**Step 9 -- castles 2-7.** Same terrain, other kind rows (S6 `BloodCastle2-7.cs`). Engine-wise only
the castle number on the event; `kMaps` keeps one row (`bloodcastle`, 11) and the castle is event
state -- unless the user wants seven map numbers (decision 3).

---

## 5. Decisions for the user

**Answered 2026-10-02 (the four the event waited on):**
- Schedule: **(a)**, on the 2-hour wall clock that `kEvents` draws; entry only in its window, 20 min of play.
- Ticket: **the Invisibility Cloak**, so Scroll of Archangel + Blood Bone drops and the one-recipe combine (step 7).
- BC1 stats: **WebZen 1.00.93**, monsters at levels 33-51.
- Death inside: **out to Devias, the run lost** (MU's rule: the cloak is spent).

**Revised the same day -- Blood Castle is a quest (the user: 'BC is a quest and players has to kill
specific count of monsters to open the gates'):**
- ~~Untimed and open at any time~~ -- revised again (the user: 'there has to be time. every 1 hour BC
  is opened. you need a ticket, you talk with NPC in devias to get in. when you are in there is
  timer and BC starts'):
  - The Messenger of Archangel in Devias lets a cloak holder in **every hour, from hh:25 for 5
    minutes** (WebZen's hourly default).
  - The run is timed **on his own clock from entry**: 60 s in the safe court, then **15 minutes**
    (WebZen's numbers; OpenMU's is 20). It starts on entry; that part is ours.
  - The entrance lifts **when the 60 s are up**, as MU's start, not on taking the quest. The
    Archangel's part is the hand-in.
  - All of this is in `sim/event.h` (`castleEntryLeft`), and the Devias travel card counts down
    to it.
- The Archangel (232) takes the weapon at the end. All three gates (entrance, bridge and door) open on kill
  counts, so the Castle Gate monster (131) does not hold the door.
- The end stays MU's: past the door the Saint Statue (132) drops the Weapon of Archangel, and
  bringing it back to the Archangel wins.
- Unchanged: the cloak ticket, WebZen's levels 33-51, and death out to Devias.
- The gates, settled:
  - **Entrance**: lifts when the run starts, after the 60 s wait.
  - **Bridge**: falls at **40 kills** on the road (MU's quota 1).
  - **Door**: opens at **2 Spirit Sorcerers (89)**. Ours: they rise in front of the door as the bridge
    falls, where MU raises them in the courtyard after the gate.
  - Then the statue rises at 14,95. The Castle Gate (131) is not raised; its figure may stand
    as scenery.


1. **Timed event or always open?** (a) on the 2-hour wall clock already drawn by `kEvents`
   (enter only in its window, 20 min); (b) any time, a 20-minute run from entry; (c) any time,
   no timer. Single player makes (a) a wait; (b) keeps the tension.
2. **The ticket**: require an Invisibility Cloak (so build Scroll/Bone drops + a one-recipe Chaos
   Machine), sell/drop the cloak directly, or let the Messenger send him in free (once per window /
   per 12 h like the quests).
3. **Which castle first and how many**: castle 1 only (levels 15-80) for now; later castles as
   event state on one `bloodcastle` world, or seven map numbers 11-17 with seven `kMaps` rows
   pointing at one folder (the latter needs `mapOf` to tolerate a shared folder).
4. **Rules source**: S6 OpenMU numbers (all we have locally) or read WebZen 1.00.93's
   `BloodCastle.cpp` first (memory: WebZen outranks OpenMU; 0.75 has no Blood Castle at all, so
   any choice is a later version's).
5. **Quotas for one player**: S6's 40 kills per player then 2 Spirit Sorcerers; keep, or scale.
6. **Rewards**: S6's exp per goal + per remaining second + zen + Jewel of Chaos, and do the Divine
   Archangel weapons (Archangel's other job) come into scope?
7. **Music**: `castle.mp3` during the run, or silence (memory: music is rare and in fights).
8. **Death inside**: out to Devias and the run lost (S6), or revive in the start room while the
   clock runs (ours).
9. **The Archangel's look**: `D/Npc/BloodCastle01.bmd` as is, or judged on the stage first.

---

## Side findings

- `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP` still has only maps 0-3: the Lost Tower's 15 boxes
  and Devias's 28/44 were never added (the tower's Part C §1.1 asked for them); open today by the
  grid, not kept by the table.
- `src/game/play_open.cpp:34-37` re-grew string tests after Part C proposed `MapRow` flags:
  `dungeonAir_`, `towerAir_`, `grassy_` by name. A seventh world is the moment to finish the
  `MapRow` widening (`air`, `grassy`, `boid`, `home`).
- `src/game/world/maps.h:35-37` still says `mapAfter` is "the stand-in for a Move window ... the
  M key"; only `--travel-at` uses it now (`play_mode.cpp:643-648`).
- `tests/sim_test.cpp:3889-3908` and `:4473-4500` pin "not ready" for the Messenger and Charon;
  they are the first tests to change.
