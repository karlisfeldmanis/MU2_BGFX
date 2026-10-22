# Kanturu port: the record, the data, and how it plugs in

Research notes, 2026-10-21. Kanturu is three MU server maps: **37 Kanturu Ruins** (client
`Data/World38` + `Object38`), **38 Kanturu Relics** (`World39`/`Object39`) and **39 the Refinery
Tower**, the Maya/Nightmare event (`World40`/`Object40`). Season 2, 2006. Entered from Tarkan's
west edge, through the sealed arch (Object86-88) Tarkan already stands. It is where the Ancient
runes are to fall (the user, 2026-10-21: "just drop new runes in katuru").

Three research passes wrote this page in `aida-port.md`'s shape: Part A is the record, Part B the
data and the look, Part C the engine side and the build. Nothing but this page was edited, nothing
was cooked, no mu.db was written and no window was opened. B ran `terrain.py` on World38-40 into
scratch only.

The passes' scratch is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/65a47bc4-0559-4919-933f-a8857d745944/scratchpad/kanturu/`
(it is /private/tmp, so it will not last): `a/` grids, spawns, client gate/Movereq tables, WebZen
1.00.93 re-cloned in `wz/`; `b/` terrain runs, model exports, decoded sheets and the contact sheets
(`prev38_l1_attr_height_light_painted.png`, `prev38_objects_on_attr.png`, `sheet_objects38.png`,
`sheet_world38.png`, `light38_big.png`, and the 39/40 twins); `c/` the walk graph, the Ruins'
spawn ladder and a dry import of World38.

## Where it stands

Research only (2026-10-21). Nothing built. Next: the user's decisions below, then step 1.

## The one thing to know first

**The Ruins are a broken ring of dark, mossy industrial ruins round a black chasm, with a cratered
plateau in the middle.** 44.8% of the map is NoGround (Aida 34.5%); MuMain clears and fogs it
black. The ground is as dark as Aida's (luminance 29) but browner, and its baked light is the
busiest yet: gold, teal and pink lamp pools. The ring is one 12,432-tile walk, 599 steps from the
camp by the Tarkan door to the Relics door, with eight breeds laddered cleanly along it (Splinter
Wolf 80 at the camp up to Genocider 105 by the Relics door). The plateau (5,514 tiles, the 2010
Warriors) is warp-only.

**But MU's levels (80-129) sit below our raised Aida (110-140), and Ancient runes need 141+
(`src/sim/items.h:990-994`).** Kanturu has to be raised past Aida to be the Ancient map.

Everything else is on disk and agrees: the grids (client = OpenMU tile for tile), gates, spawns,
bodies (Monster107-117), sounds (`Sound/w37`, by map number) and music (`kanturu_1st.mp3`).

## Decisions for the user

Merged from the three passes; recommendations in italics. Full reasoning: A §Decisions, B §9, C §8.

**Scope and levels**
1. **Ruins first, Relics next, event parked** (Maya/Nightmare, Moonstone Pendant, Elpis, the
   Gateway Machine). *Yes.*
2. **The 2006 Ruins only** (151 spots); the plateau and its Warriors (553-556) closed until later,
   as a higher band. *Yes.*
3. **Raise every breed past 141, keeping MU's order**, e.g. Splinter Wolf 142 ... Genocider 156;
   Relics 160-165; Warriors 168-172. *Raise, as Aida was raised past Icarus.*
4. **Door levels.** MU: 150 in, 130 out, 220 to the Relics. Ours: Aida's door is 300. *The
   Tarkan door just above Aida's (e.g. 320), the way out free, the Relics door higher.*
5. **Door now?** A sealed door at Tarkan now ("Kanturu is sealed. Its door will not open.",
   `gates.h:46`), or nothing until the Ruins are playable. *Nothing in the live game until it's
   playable; the studio doesn't need the door.*
6. **Death and Town Portal.** Death at the camp (gate 138) everywhere. Portal: WZ sends it to
   Lorencia, OM to the camp. *Portal to Tarkan's town: the camp has no merchant.*
7. **A camp NPC** (potion seller, as Aida's Amy) or none, as MU. 

**Drops**
8. **Ancient runes** are the reward (decided). Also MU's scrolls here (Nova, Ice Storm, Decay)?
   No Gemstone, no Moonstone. MoneyRate / MaxItemLevel: S17's (14 / 3-4) or Aida's?

**Look** (each needs a first shot before it's settled)
9. **The void:** A (Aida's cut and abyss, 6 m deep for the 5-14 m cliff faces), or B (A plus a
   cool mist in the chasm, from MU's 58 mist boxes over it that MuMain never draws).
10. **The outer border:** MU's bare black edge, or filled like Aida's.
11. **Grass:** MU's olive tufts on slot 0 only, or also its pale blue-white wisps on slot 1.
12. **Three white placeholder sheets** (72 tiles): *remap to real sheets.*
13. **Baked light's colour pools:** as MU baked them, or softened (a `light_chroma` row).
14. **Ponds** still or MU's 20 s slide; **music** `kanturu_1st` in the camp only (house rule) with
    `kan_ruin_global.wav` as the bed.
15. **Monster skills:** Kentauros' multishot and Iron Rider's arrows as ranged, the rest as looks.

**Settled by the record, no choice needed:** arrival gate 126 overlaps exit 127 on row 220, so land
on 138's box (the camp); the spawn gate needs a `gates.spawn` row or a death rises in Lorencia;
`void_fill` needs two seeds, (20,218) and (70,187), or the plateau cooks as void.

## The build, in order

1. **Bare Ruins.** `content/world/kanturu/world.json` (`map: 37`, the `pipeline` block from B §3:
   hidden, grass, void, void_fill), `ground.json`, then `./mu make ground kanturu --folder 38
   --plan` (the MCP's `cook` can't pass `--folder` yet, `mcp_server.py:239-244`), ask, cook; a
   light sheet `kanturu.json` (B §6 has a first guess: black clear). Shown in the studio.
2. **The realm knows it:** `rules` / `show` / `gates` blocks, `mu make realm`, rebuild.
3. **Objects** in batches of 3-6, the cliff faces Object06/76 and the big walls first; waterfalls
   reuse Aida's spray, fires and lamps after.
4. **The Tarkan door**, both sides at once with `mu edit --insert` (`mu gate add` numbers from 200
   and can't write MU's 125-128), and `testKanturuGates`.
5. **Monsters one breed at a time**, Splinter Wolf to Genocider: `./mu research <n>`, `new monster
   --db`, raised levels, judged by the user in the arena.
6. The travel row (`realm_travel.cpp`, `kTravels` 17 to 18), an Elite row (`elite.h`), the drops.
7. Later: the Relics (map 38), then the Tower event (map 39), which swaps between two terrains at
   runtime (`EncTerrain401`), something no world does yet.

## Side findings (outside Kanturu)

- `MU2_BGFX/source/mu.db` no longer exists; `content/mu.db` holds accounts, monsters are in
  `content/monsters/*.json`. Older port docs still name the old path.
- `third_party/MuSource_S17` is a private emulator (Dimension Gamers / VNdev), not WebZen's code.
  WebZen's 1.00.93 was re-cloned into the scratch's `wz/`.
- `items.h:1141` calls Ice Storm "AT_SKILL_BLAST_FREEZE"; MuMain names skill 39 `AT_SKILL_ICE_STORM`.
- MuMain loads sound files that don't exist (`kenta_skill-01` as attack 2; no `gigan_idle-02`).
- MU's `_R` additive test is case-sensitive: the Relics' 300 `cho_scarp_01_r` are not additive.

---

## Part A: the record (Kanturu)

Research pass A, 2026-10-21. Nothing tracked was edited, nothing was cooked or run, no window was
opened. No mu.db was written. (`MU2_BGFX/source/mu.db` no longer exists. The live `content/mu.db` holds
accounts and characters only. Monster rows now live in `content/monsters/*.json` `combat` and gates
in `content/world/<w>/world.json`, so both were read as files.)

Paths:

- **OM**: OpenMU init, `LEGACY/reference/openmu/src/Persistence/Initialization/`. Kanturu exists
  only in **VersionSeasonSix**: `Maps/KanturuRuins.cs` (654 lines), `Maps/KanturuRelics.cs` (304),
  `Maps/KanturuEvent.cs` (102), `Gates.cs`. Its attribute files are in `Resources/Terrain38/39/40*.att`.
- **MM**: MuMain source, `LEGACY/reference/MuMain/src/source/`. **D** is its `src/bin/Data/`.
- **WZ**: WebZen GameServer 1.00.93, `github.com/ptr0x-real/Mu-GS-Webzen-MC-10093`, freshly
  cloned for this pass to `scratchpad/kanturu/wz/` (the older clones are gone). Its sources are
  CP949, converted to UTF-8 line for line in `a/wzu/`. Line numbers below are WZ's own.
- **WZD**: that clone's `_Server Files/Data` (Elion's 2012 "99z" repack). It has **Kanturu.dat**,
  **KanturuMonsterSetBase.txt** and `Terrain38/39/40(_CLOSE/_OPEN).att`. It has **no** Kanturu row
  in `Monster.txt`, `MonsterSetBase.txt`, `Gate.txt` or `MoveLevel.txt`, and its
  `MonsterSkill*.txt` / `MonsterAI*.txt` are 0 bytes.
- **S17**: `MU2_BGFX/third_party/MuSource_S17`. **Read this before trusting it: it is not WebZen's
  code.** It is Dimension Gamers' / VNdev's Season 17 *emulator* ("Copyright 2008-2017 Dimension
  Gamers", `GameServer/Kanturu.cpp:1-6`; `README.md:1-5`). Its MySQL data is
  `_MuServer/Databases/mu_online_game.sql` (templates, spawns, skills) and `game.sql`
  (`item_drop`). It ranks below WZ and beside OM: a third, later, private-server opinion.
- **M135**: the Main 1.03.35 client, `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source`.
- **Client tables** (BuxConvert, `MM/Core/Globals/_crypt.h:4-10`): `D/gate.bmd` (512 rows of
  `GATE_ATTRIBUTE`, `MM/Core/Globals/_struct.h:121-134`, read by `MM/Engine/Object/ZzzInfomation.cpp:152-176`),
  `D/Local/Movereq.bmd` (`MM/Network/MoveCommandData.cpp:11-21`) and `D/Local/MonsterSkill.bmd`
  (`ZzzInfomation.cpp:178-200`).
- **Web**: MuHistory (github `AlighieriDemiurgs/MuHistory` README).

Scratch (`scratchpad/kanturu/a/`). Scripts: `om.py` (OM spawns and breeds), `clienttables.py`
(gate.bmd, Movereq.bmd), `monskill.py`, `s17.py` / `s17extra.py` / `s17drops.py` (S17 data),
`grids.py` (every attribute grid, with the cipher from `pipeline/terrain.py:401-465`), `spawns.py`
(gates, spawns, walks), `more.py` (grid variants, ASCII maps), `relics.py`, `tarkandoor.py` (our
Tarkan grid). Outputs: `*_out.txt`, `map37_out.txt` / `map38_out.txt` / `map39_out.txt` (every
2nd tile: `.` walk, `,` safe, `#` NoMove, space NoGround), and `grids.pkl`. Compass as in
tarkan-port: x grows east, y grows south.

### 0. The one thing to know first

**Kanturu is Season 2, 2006, a year younger than Aida.** WebZen coded the field maps under
`ADD_NEW_MAP_KANTURU_COMMON_20060523`, the boss map under `ADD_NEW_MAP_KANTURU_BOSS_20060627`, and
the event under `ADD_KANTURU_20060627` (`WZ define2.h:1835-1838, 3070-3084`). MuHistory puts it
live on **2006-08-29**, the first day of Season 2: "Introduction of map Kanturu Ruins and Kanturu
event. Entry level requirement of 150". The Jewel of Harmony, the Gemstone and the Refining Stone
arrived the same day. **Season 5 Part 4 (2010-04-27) "expanded" Kanturu**, as it did Aida: the four
*Warrior* breeds (553-556) live on a closed-off island inside the Ruins that only a warp reaches.

**The Ruins is the best-shaped levelling map MU has, as raw data.** From the camp beside the
Tarkan door (south-west), one region of 12 432 tiles winds clockwise for 599 steps to the door to
the Relics (89,89, centre-west). The breeds climb along the walk with almost no overlap:

| steps from the camp | breed (MU level) |
|---|---|
| 29-168 | Splinter Wolf (80) |
| 151-252 | Iron Rider (82) |
| 184-330 | Satyros (85) |
| 318-418 | Blade Hunter (88) |
| 358-473 | Kentauros (93) |
| 375-575 | Berserker (100) |
| 478-504 | Gigantis (98) |
| 506-572 | Genocider (105) |
| 598 | the Relics door |

The Relics behind that door hold 117-119 breeds, and behind the Relics is the instanced
Maya/Nightmare event (§7).

**Our raised ladder already passes these levels.** Aida now runs 110-140 (above Icarus's
75-108), and `kAncientLevel = 141` (`MU2_BGFX/src/sim/items.h:993-997`). MU's own Kanturu levels
(80-129) sit **under** Aida, so not one Kanturu breed would drop an Ancient rune as MU numbers
them. **Porting Kanturu as the Ancient-rune map means raising it past 141** (decision 2).

**The door already exists.** Tarkan gate 125 (6,199-6,201) is the Kanturu gate. Our cooked Tarkan
grid has all six tiles of the door and its exit open, in the town's region, **266 steps from
Tarkan's town**. We already built and drew Object86-88 there as a sealed arch.

### 1. Map

#### 1.1 Era (the evidence)

- **OM**: only VersionSeasonSix. `GameMapsInitializer.cs:60-62` lists KanturuRelics, KanturuRuins
  and KanturuEvent. Version075 and 095d have no Kanturu.
- **WZ**:
  - `define2.h:1835-1838` "칸투르 보스전 추가" `ADD_KANTURU_20060627`.
  - `:3070-3073`: `KANTURU_MOONSTONEPENDANT_20060804`, `ADD_NEW_MAP_KANTURU_BOSS_20060627`,
    `ADD_NEW_MAP_KANTURU_COMMON_20060523`, `KANTURU_PROTOCOL_20060705`.
  - `:3079-3084`: no exp from the boss monsters, the Jewel of Harmony and Moonstone drops, the Maya
    hand and Nightmare bags, the NPCs.
  - `:3967-3972`: `MAX_MAP` 39, then 40.
  - `:4105-4111`: `MAP_INDEX_KANTURU1 = 37`, `KANTURU2 = 38`, `KANTURU_BOSS = 39`.
- **MuHistory**: Season 2 (2006-08-29 to 2007-12-06) brings Kanturu Ruins and the event at entry
  level 150, and Gemstones, the JoH and the Refining Stone. Season 5 Part 4 (2010-04-27) brings
  "Expansion of maps and addition of new monsters. Maps affected: Aida, Kanturu, ...".
- **Cross-check versions**: 0.75, 0.95d and 0.97d are absent. S3 ep6 and S6 are present
  (MuMain `WD_37KANTURU_1ST..WD_39KANTURU_3RD`, `MM/World/MapInfra/MapManager.h:32-34`).
- **Lore hook we already wrote**: "Kantur" is WebZen's underground empire, named in Tarkan's
  Golden Tantalos bag (`docs/worlds/tarkan-port.md:730-739`). Our quests "The Road of Kantur" and
  "Kantur's Legion" lead the hero to Tarkan's Keeper of Kantur
  (`MU2_BGFX/src/sim/quests.cpp:1464-1571`). So Kanturu is the place those quests point at.

#### 1.2 Map facts

| | Ruins (37) | Relics (38) | Refinery Tower / boss (39) |
|---|---|---|---|
| WZ name | 칸투르 1차맵 (`define2.h:4106`) | 칸투르 2차맵 (`:4107`) | 칸투르 보스맵 (`:4111`) |
| OM | "Kanturu_I" (`KanturuRuins.cs:19-24`) | "Kanturu_III", "Kanturu Relics, Kanturu Remain" (`KanturuRelics.cs:20-25`) | "Kanturu Event" (`KanturuEvent.cs:18-23`) |
| S17 | 'Kanturu', spawn gate 138 | 'Kanturu Core', 139 | 'Kanturu Boss', 137, with Close/Open states (`world_template.sql:78-80, 117`) |
| MuMain | `WD_37KANTURU_1ST`, D/World38 + D/Object38 (208 files) | `WD_38KANTURU_2ND`, World39 / Object39 (131) | `WD_39KANTURU_3RD`, World40 / Object40 (181) |
| music (MuMain) | `kanturu_1st.mp3` over the whole map (`MM/Scenes/SceneManager.cpp:1099-1102`) | `kanturu_2nd.mp3` (`M38Kanturu2nd::PlayBGM`, `GM_Kanturu_2nd.cpp:1350`) | `KanturuMayaBattle.mp3`, `KanturuNightmareBattle.mp3`, `KanturuTower.mp3` (`MM/Core/Globals/_enum.h:192-196`) |
| ambience | `Sound/w37/kan_ruin_{waterfall,elec,wheel,plant,global}.wav` (`MapManager.cpp:376-384`) | `w38/kan_relic_{gear,incubator,hole,global}` (`:439-442`) | `w39` Maya/Nightmare voices, `kan_boss_*` (`:491-510`) |
| Town Portal (client) | allowed (`MM/World/MapInfra/PortalMgr.cpp:57-58`) | allowed | not allowed |
| map name | `MapManager.cpp:1691-1701` | | |

- All five tracks are already in `MU2_BGFX/out/assets/music/` (`kanturu_1st.mp3`,
  `kanturu_2nd.mp3`, `KanturuMayaBattle.mp3`, `KanturuNightmareBattle.mp3`, `KanturuTower.mp3`).
  `content/music/` holds none of them.
- **NPCs**:
  - Ruins: none in any source (no merchant, no warehouse).
  - Relics: the **Gateway Machine** (367) at 141,191 facing south. It is the event's entrance
    (`KanturuRelics.cs:46`; S17 `npc_talk_kanturu_gateway`). MuMain draws it as
    `MODEL_KANTURU2ND_ENTER_NPC` at scale 4.76 (`GM_Kanturu_2nd.cpp:106-110`).
  - Boss map: **Elpis/Elphis** (368) at 77,177 facing south-west, the Gemstone-to-JoH refiner
    (`KanturuEvent.cs:51`; `OM NpcInitialization.cs:486-491`, `NpcWindow.ElphisRefinery`;
    `OM ChaosMixes.cs:69, 715-728`).

#### 1.3 Grids (decoded, `a/grids_out.txt`)

Walkable means `(flags & (NoMove|NoGround)) == 0`.

**Ruins (37).**

| grid | walk | regions (largest) | NoMove | NoGround | safe (walkable) |
|---|---:|---|---:|---:|---|
| client `D/World38/EncTerrain38.att` = `EncTerrainTest38` = plain `Terrain.att` = **OM `Terrain38.att`** | **17 954** | 12 432 + **5 514** + 6 crumbs | 47 499 | 29 348 | 1 738 (639) |
| WZD `Terrain38.att` | 12 440 | 12 432 + crumbs | 53 013 | 41 817 | 1 192 (497) |
| S17 `Terrain38.att` | 17 936 | 12 311 + 5 617 | 47 534 | 30 350 | 641 (640) |

- **Client = OM, tile for tile** (0 differences). This is the grid to use, as for every earlier
  world.
- **WZD is exactly the client minus the 5 514-tile second region**, which is the 2010 island
  (x 45-194, y 79-188, in the middle of the map). The island has its own safe camp,
  60-80,174-199 (142 walkable safe tiles), and holds all of OM's and S17's Warrior spawns. No
  walk reaches it from the main region (`a/spawns_out.txt`: "island flood touches spawn-gate
  region? False"). It is entered by the warp "Kanturu_ruin_island" (gate 334) only.
- S17's grid is a re-cut: 992 tiles differ in walkability. Ignore it.
- **The main region**: the camp at 9-50,195-233 (497 walkable safe tiles), an arc east along the
  south (y 196-240), north up the east side (x 165-236), west across the north (y 7-71) and down
  the west (x 31-104) to the Relics door at 89,89. The farthest tile is 599 steps away. It is
  6 crumbs at most beyond the island.
- No water bit (16) anywhere. 174 tiles are "13" (safe+NoMove+NoGround) and 924 are "5"
  (safe+NoMove): the camp's walls.

**Relics (38).**

| grid | walk | regions | safe (walkable) |
|---|---:|---|---|
| client `EncTerrain39` = plain `Terrain.att` = **WZD `Terrain39.att`** | **4 529** | 4 454 + 73 + 2 | 559 (183), box 69-93,98-114 (cluster 52-93,98-114) |
| OM `Terrain39.att` | 5 181 | 4 595 + 511 (rows 254-255, a file artifact) | 323 (323) |
| S17 | 4 649 | 1 | 192 |
| client `D/World39/TerrainServer.att` | junk | 30 | values 0-255 in every bit: not a Relics grid |

- Client = WZD here. OM's differs by 656 tiles (its "server" re-cut).
- One region, x 69-202, y 66-186. The camp is at its west end (gate 137, 71-82,102-109). The far
  walk is 167 steps. The Gateway Machine's tile is reached at 141,186, 150 steps in. Gate 136
  (137-143,162-163, where the tower's closing throws players out) is 126 steps in.

**Boss map (39).** MuMain swaps grids by the event's outcome: `EncTerrain40` before the Nightmare
falls and `EncTerrain401` after (`MM/World/MapInfra/MapManager.cpp:1278-1287`, `IsSuccessBattle`).

| grid | walk | regions |
|---|---:|---|
| client `EncTerrain40` = WZD `Terrain40.att` | 2 260 | Maya's room 1 202 (174-217,54-83); Nightmare's hall 1 033 (57-98,85-144); 25 (188-192,110-114) |
| client `EncTerrain401` (after the win) | 2 617 | the hall grows to 1 390 (57-98,85-194): 357 tiles open at 67-88,145-194, the way to the tower and Elpis (77,177) |
| WZD/OM/S17 `Terrain40_CLOSE` / `_OPEN` | 2 771 / 3 128 | the same two states (+ OM's 511-tile artifact) |

- WZ loads both server states (`WZ Gamemain.cpp:1731-1732`). There are **no safe tiles** on map 39.

### 2. Gates and warps

#### 2.1 Gate rows (`a/clienttables_out.txt`; OM `Gates.cs`; S17 `gate_template.sql:163-179, 368`)

Levels are given as client gate.bmd / OM / S17. "open" counts tiles open on the client grid, out of
the box.

| gate | kind | map | box | to | level | OM line | open |
|---:|---|---|---|---:|---|---|---|
| **125** | enter | Tarkan (8) | 6,199-6,201 | 126 | **150** / 150 / 150 | `:537` | 3/3 on **our** cooked Tarkan grid |
| **126** | exit | Ruins | 17,219-21,220 | | (150) | `:279` | 10/10, safe |
| **127** | enter | Ruins | 17,220-19,222 | 128 | **130** / 130 / 130 | `:538` | 9/9, safe |
| **128** | exit | Tarkan | 7,199-7,201 | | (130) | `:201` | 3/3 on ours |
| **138** | spawn | Ruins | 19,217-21,219 | | (160) | `:276` (spawn) | 9/9, safe: death and warp "Ruins1" |
| 141 | exit | Ruins | 205,36-208,41 | | (160) | `:277` | 24/24, not safe: warp "Ruins2", 326 steps from the camp |
| 334 | exit | Ruins (island) | 66,183-74,191 | | (160) | `:278` | 51/81, safe, island only |
| **129** | enter | Ruins | 89,89-89,92 | 130 | **220** / 220 / **290** | `:539` | 4/4, 598 steps from the camp |
| 132 | exit | Ruins | 85,89-86,92 | | (150) | `:280` | 8/8 |
| **130** | exit | Relics | 70,104-70,107 | | (220) | `:285` | 4/4, safe |
| **131** | enter | Relics | 69,104-69,107 | 132 | **150** / 150 / 150 | `:540` | 4/4, safe |
| **137** | spawn | Relics | 71,102-82,109 | | (220) | `:283` (spawn) | 96/96, safe: WZ "death or regen from the boss map" |
| 139 | exit | Relics | 71,104-72,107 | | (230) | `:284` | 8/8: warp "Kanturu_Remain", and death in the Relics |
| 136 | exit | Relics | 137,162-143,163 | | (220) | `:286` | 14/14: thrown out of the tower |
| 133 | exit | boss | 196,56-201,57 | | (220) | `:271` (spawn) | Maya's room, via the NPC only |
| 134 / 135 | exit | boss | 78,93-82,95 | | (220) | `:272-273` | the Nightmare's hall / the open tower, via the NPC only |

- WZ names 133-139 (`WZ KanturuDefine.h:21-30`):
  - `KANTURU_MAYA_MAP_GATE` 133
  - `KANTURU_NIGHTMARE_MAP_GATE` 134
  - `KANTURU_TOWER_OPEN_MAP_GATE` 135
  - `KANTURU_KICK_OUT_FROM_TOWER` 136
  - `KANTURU_DEFAULT_REGEN_MAP_GATE` 137
  - `KANTURU_DIEREGEN_GATE_MAP_1` 138
  - `KANTURU_DIEREGEN_GATE_MAP_2` 139
- **Trap: gate 126's arrival box overlaps door 127** on three tiles (17-19,220). A character
  dropped on one of them stands on the way back to Tarkan. Gate 138's box (y 217-219) does not
  overlap.
- MuMain's max levels (350/400) are the S3+ master-level cap. Not ours.

#### 2.2 Warp list (`D/Local/Movereq.bmd`, `a/clienttables_out.txt`; OM `Gates.cs:72-74, 82`; S17 `teleport_template.sql:69-72`)

| warp | gate | level client / OM / S17 | zen |
|---|---:|---|---:|
| Kanturu_ruin1 (28) | 138 | **160** / 160 / 160 | 9 000 |
| Kanturu_ruin2 (29) | 141 | 160 / 160 / 160 | 9 000 |
| Kanturu_ruin_island (45) | 334 | 160 / 160 / **380** | 15 000 |
| Kanturu_Remain (30) | 139 | **230** / 230 / **300** | 12 000 |

- OM's `FixWarpLevelUpdatePlugIn.cs:62-68` re-sets them to the same values.
- MU's order by warp level: Tarkan 140, Aida 150, **Kanturu 160**, Karutan 170, Icarus 170, Remain
  230. The boss map has no warp.
- **WZ /move**: only a test-server "칸투르2" teleport to 38:141,184 (`WZ MoveCommand.cpp:1019-1027`,
  `TESTSERVER == 1`). The live rows come from the MoveReq data.

#### 2.3 What it takes to get in

- **Ruins and Relics**: level only (150 through the door, 220 to the Relics; S17 290). WZ has a
  global kill-switch, `BlockKanturuMapEnter`, which refuses both maps and logs players out of them
  to Lorencia (`WZ protocol.cpp:26329-26338`; `user.cpp:3375-3385`; `Gamemain.cpp:4020`).
  **No item is needed for 37/38.**
- **The boss map (event)**: only through the Gateway Machine, only from map 38, only while the
  event admits (`WZ Kanturu.cpp:591-788`, `CheckEnterKanturu`). It needs:
  1. a free place in a party of at most `MAX_KANTURU_BATTLE_USER` **15** (`WZ KanturuDefine.h:7`);
  2. the **Moonstone Pendant (13,38)** in a *ring* slot with durability > 0 (`Kanturu.cpp:808-831`);
     the check is a Kanturu.dat switch, `KANTURU_DATAFILE_LOAD_CHECK_MOONSTONE`, `:134-150`;
  3. **wings, a cape, a Dinorant, a Dark Horse or a Fenrir** (`:632-660`);
  4. **no Uniria** (`:664`) and **no transformation ring** (13,10 / 39 / 40 / 41 / 68 / 76,
     `:674-750`).
- The same wings-or-mount and no-ring rule guards any other entry to 39 (`WZ user.cpp:26393-26440`;
  `protocol.cpp:26304-26327`, `CGReqMapMove`). The client asks the same before it sends
  (`M135 NewUIKanturuEvent.cpp:522-556`, "POPUP_NOT_HELPER", "POPUP_NOT_MUNSTONE").
- OM models only the pendant (`KanturuEvent.cs:43-46`, `Stats.MoonstonePendantEquipped`) and says
  events are not implemented.
- **Moonstone Pendant**:
  - WZ wears it in a ring slot. It loses 1 durability about every 90 s while worn on the right
    hand (`WZ user.cpp:11006-11017`, "대략 90초에 1씩"). It cannot be sold in a personal shop or
    traded (`:17414-17417, 18877`), and only one may be worn (`:16116-16117`).
  - OM: "Moonstone Pendant", level 21, durability 120, not a monster drop (`OM Items/Jewelery.cs:45-50`).
  - S17 names it "Moonstone Ring".
  - Without it, Maya's **Broken Shower kills outright** (`WZ KanturuMaya.cpp:90-125`; S17
    `ai_kanturu.cpp:150-165`).
- **No oxygen, no water rule.** "Flight" applies only to the boss map, as Icarus: S17 drops a
  player without wings or a mount (`S17 PlayerCharacter.cpp:10877-10900`), and WZ forbids
  transformation rings there as in Icarus (`WZ user.cpp:17867-17885`).

### 3. Terrain attributes (client grids)

| map | 0 | 1 safe | 2 | 4 NoMove | 5 | 8 NoGround | 9 | 12 | 13 | walkable |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 37 `EncTerrain38` | 17 314 | 639 | 1 | 17 310 | 924 | 82 | 1 | 29 091 | 174 | 17 954 |
| 38 `EncTerrain39` | 4 345 | 183 | 1 | 33 319 | 140 | -- | 2 | 27 312 | 234 | 4 529 |
| 39 `EncTerrain40` | 2 259 | -- | 1 | 39 559 | -- | 3 | -- | 23 714 | -- | 2 260 |
| 39 `EncTerrain401` | 2 616 | -- | 1 | 39 202 | -- | 9 | -- | 23 708 | -- | 2 617 |

- The Ruins is mostly void (NoGround 44.8%) with NoMove walls. Its play space is a ribbon about 15-30
  tiles wide.
- Joins: Ruins to Relics only by gate 129 (89,89-92) to 130. Ruins to the island only by warp 334.
  Relics to the boss map only by the NPC (gates 133/134/135).

### 4. Monsters

#### 4.1 Ruins spawns (OM `KanturuRuins.cs:45-265`; S17 `mu_online_game.sql:20062-22107`)

Every spot is single-tile in OM. S17's are single-tile with spawn_distance 4. **Both tables are
open and non-safe on the client grid** (`a/spawns_out.txt`). There is no WZ table on disk:
WZD's `MonsterSetBase.txt` has no map 37/38.

| # | breed | OM spots | OM lines | S17 spots | where | walk from the camp, min/median/max (OM) |
|---:|---|---:|---|---:|---|---|
| 351 | Splinter Wolf | 22 | `:74-95` | 33 | the south arc, x 36-189, y 196-240 | 29 / 118 / 168 |
| 352 | Iron Rider | 21 | `:97-117` | 19 | south-east, x 165-236, y 113-205 | 151 / 215 / 252 |
| 353 | Satyros | 25 | `:119-143` | 23 | the east side, x 166-236, y 35-181 | 184 / 264 / 330 |
| 354 | Blade Hunter | 22 | `:145-166` | 20 | the north-east, x 116-221, y 7-59 | 318 / 386 / 418 |
| 355 | Kentauros | 13 | `:168-180` | 11 | the north, x 72-181, y 15-71 | 358 / 398 / 473 |
| 350 | Berserker | 28 | `:45-72` | 29 | north and west, x 31-188, y 11-104 | 375 / 471 / 575 |
| 356 | Gigantis | 9 | `:182-190` | 7 | the north-west, x 41-90, y 19-40 | 478 / 496 / 504 |
| 357 | Genocider | 11 | `:192-202` | 7 | the west, x 32-63, y 44-104 | 506 / 532 / 572 |
| | *2006 Ruins* | **151** | | **149** | | |
| 553 | Berserker Warrior | 12 | `:204-215` | 5 | island | (warp only) |
| 554 | Kentauros Warrior | 18 | `:217-234` | 13 | island | |
| 555 | Gigantis Warrior | 16 | `:236-251` | 14 | island | |
| 556 | Genocider Warrior | 13 | `:253-265` | 5 | island | |
| | *2010 island* | **59** | | **37** | | |

- OM and S17 share 100 of their spots. OM has 110 of its own and S17 86, mostly Splinter Wolves
  (S17 +20) and Warriors (S17 has 37 to OM's 59). Nothing settles the difference. **WZ outranks
  both but has no table, and S17 is an emulator, so take OM's**, as Aida did.
- Unlike Aida, **the level climbs cleanly with the walk**. Only the Berserker (100) spreads over the
  north and the west (375-575).

#### 4.2 Relics spawns (OM `KanturuRelics.cs:52-145`; S17 `:16652-20327`)

| # | breed | OM | S17 | where | walk min/med/max |
|---:|---|---:|---:|---|---|
| 359 | Twin Tale | 24 (`:80-103`) | 24 | x 99-199, y 74-160 | 18 / 82 / 153 |
| 358 | Persona | 27 (`:52-78`) | 27 | x 97-195, y 67-159 | 37 / 102 / 163 |
| 360 | Dreadfear | 12 (`:105-116`) | 12 | x 110-177, y 110-162 | 74 / 116 / 144 |
| 105 | Canon Trap | 14 (`:119-132`) | 28 | x 97-201, y 70-153 | |
| 106 | Laser Trap | 12 (`:134-145`) | 26 | the corridor x 138-144, y 166-183 to the Gateway Machine (7 of OM's stand in walls) | |
| 367 | Gateway Machine | NPC (`:46`) | 1 | 141,191 (a NoMove tile) | |

The Relics breeds are mixed along the walk, not laddered. S17 doubles the traps (+14 +14). Spots
are otherwise identical.

#### 4.3 Boss map

- **OM**: only 38 Laser Traps (`KanturuEvent.cs:58-95`, 28 of them in walls) and Elpis. **OM has
  no Maya, Maya hands or Nightmare at all** (no breeds 361-364 anywhere in OM init).
- **WZD `KanturuMonsterSetBase.txt`**, per event state:
  - state 0: Maya (364) at 188,110 + 30 Blade Hunters + 10 Dreadfears;
  - state 1: Maya + 12 Twin Tails + 8 Dreadfears;
  - state 2: Dreadfears, Personas, Genociders in the Nightmare's hall (75-87,98-136), with the
    Nightmare (361 at 78,137) commented out;
  - states 3-5: Maya (the hands come from AI groups 20-22, `WZD Kanturu.dat` section 5);
  - state 6: 7 Dreadfears.
- **S17**: the same, in `monster_event` rows (`mu_online_game.sql:27429-…`, event_id 7).

#### 4.4 Breeds

Numbers are OM S6 (`KanturuRuins.cs:274-652`, `KanturuRelics.cs:154-302`). Resistances are OM `n/255`
(P I L F, all four equal unless noted). Respawn is 10 s for all. `NumberOfMaximumItemDrops = 1` and
`Attribute = 2` for all. Speeds are move / attack in ms.

| # | name | lvl | HP | dmg | def | atk/def rate | move/atk/view | speed | res | OM lines |
|---:|---|---:|---:|---|---:|---|---|---|---|---|
| 351 | Splinter Wolf | 80 | 16 000 | 310-340 | 230 | 460/163 | 3/2/7 | 500/1500 | 20 | `:306-332` |
| 352 | Iron Rider | 82 | 18 000 | 335-365 | 250 | 490/168 | 3/2/5 | 500/1500 | 21 | `:338-364` |
| 353 | Satyros | 85 | 22 000 | 365-395 | 280 | 540/177 | 3/2/7 | 500/1500 | 22 | `:370-396` |
| 354 | Blade Hunter | 88 | 32 000 | 408-443 | 315 | 587/195 | 3/1/7 | 500/1500 | 23 | `:402-428` |
| 355 | Kentauros | 93 | 38 500 | 470-505 | 370 | 645/220 | 3/1/7 | 500/1500 | 24 | `:434-460` |
| 356 | Gigantis | 98 | 43 000 | 546-581 | 430 | 715/250 | 3/2/7 | 500/1500 | 25 | `:466-492` |
| 350 | Berserker | 100 | 44 370 | 555-590 | 443 | 728/255 | 3/2/7 | 500/1550 | 25 | `:274-300` |
| 357 | Genocider | 105 | 48 500 | 640-675 | 515 | 810/290 | 3/2/7 | 500/1600 | 26 | `:498-524` |
| 359 | Twin Tale | 117 | 87 500 | 830-1085 | 865 | 1080/440 | 3/4/7 | 500/1500 | 28 | Relics `:244-270` |
| 358 | Persona | 118 | 68 000 | 1168-1213 | 615 | 1190/485 | 3/**5**/7 | 500/1600 | 29 | Relics `:212-238` |
| 360 | Dreadfear | 119 | 94 000 | 946-996 | 783 | 1015/906 | 3/2/5 | 500/1500 | 27 | Relics `:276-302` |
| 553 | Berserker Warrior | 123 | 184 370 | 915-990 | 543 | 1557/755 | 3/2/9 | 700/2500 | 20, L 150 | `:530-556` |
| 554 | Kentauros Warrior | 126 | 198 500 | 970-1005 | 570 | 1258/920 | 3/4/9 | 700/1500 | 20, L 150 | `:562-588` |
| 555 | Gigantis Warrior | 128 | 203 000 | 1046-1181 | 630 | 1575/750 | 3/2/9 | 700/2500 | 20, L 150 | `:594-620` |
| 556 | Genocider Warrior | 129 | 218 500 | 1240-1375 | 715 | 1251/990 | 3/2/9 | 700/2500 | 20, L 150 | `:626-652` |
| 105 | Canon Trap | 80 | 1 000 | 200-203 | 0 | 400/500 | 0/4/2 | -/1000 | trap, 3 s | Relics `:154-177` |
| 106 | Laser Trap | 80 | 1 000 | 150-152 | 0 | 400/500 | 0/1/1 | -/1000 | trap, 3 s | Relics `:183-206` |

**Event breeds (S17 only; `mu_online_game.sql:43623-43626`).**

| # | name | lvl | HP | dmg | def | respawn |
|---:|---|---:|---:|---|---:|---|
| 361 | Nightmare | 135 | 1 500 000 | 1225-1468 | 1000 | 12 h |
| 362 | Left Hand of Maya | 135 | 400 000 | 732-932 | 315 | |
| 363 | Right Hand of Maya | 135 | 350 000 | 732-932 | 800 | |
| 364 | Maya | 111 | 700 000 (a type-128 prop) | | | |

None of the event breeds give exp (`WZ user.cpp:14506-14513`).

**S17 vs OM**:
- S17 equals OM on every 2006 breed except the Berserker (S17: atk rate 1000, def rate 360, move
  800 ms, attack 1700 ms, resistances 50) and Dreadfear's def rate (905).
- S17 gives the Warriors attack rates 540-750 (OM 1251-1575), resistances 23-26 with no lightning
  150, and the Berserker Warrior attack range 1.
- S17 gives MoneyRate 14 (20 for the Warriors) and MaxItemLevel 3-4 (`monster_template` columns
  `zen_rate` / `item_max_level`). **OM has neither, so S17's are the only ones on disk.**
- WZD's Monster.txt stops before 350, so **no WebZen numbers exist for any Kanturu breed.**

**Skills**:
- The client's `MonsterSkill.bmd` (`a/monskill_out.txt`) gives:
  - Kentauros 355: 14 (multishot)
  - Persona 358: 15
  - Twin Tale 359: 16
  - Dreadfear 360: 17
  - Nightmare 361: 18-21
  - Maya hands 362/363: 23, 24
  - Maya 364: 25, 26
  - Berserker Warrior 553: 70
  - Genocider Warrior 556: 71
- S17 names them (`skill_special`, `mu_online_game.sql:50190-50247`):
  - 14 "Kentauros - Multishoots"
  - 15 "Persona - Evil Spirits"
  - 16 "Twin Tale - Hair Attack"
  - 17 "Dreadfear - 2 Wings Hits"
  - 18-22 the Nightmare's Poison / Wind / Summon / Fire Camp / Warp
  - 23 "Maya's Hand - Pressure", 24 "Power Wave", **25 "Ice Storm"**, 26 "Broken Shower"
  - 70 Berserker Warrior Stun, 71 Genocider Warrior Push
- OM creates none of these: every Kanturu breed only melees in OM.

#### 4.5 What MuMain draws

The body is `D/Monster/Monster{MODEL+1}.bmd` (models `MM/Core/Globals/_enum.h:4256-4271, 4347-4350`).
Files were checked in `D/Monster/`.

| # | body | scale | what MuMain adds | lines |
|---:|---|---:|---|---|
| 350 Berserker | Monster107 | 0.95 | bright texture-scrolled meshes 0/1 (`BITMAP_BERSERK_EFFECT`, `_WP_EFFECT`); smoke at the mouth bone | `GM_Kanturu_1st.cpp:408-418, 908-927, 1090-1131` |
| 351 Splinter Wolf | Monster108 | 0.8 | two red `BITMAP_JOINT_ENERGY` eye trails (1.5,0.01,0) | `:441-457, 954-966, 1221-1248` |
| 352 Iron Rider | Monster109 | 1.3 | fires `MODEL_IRON_RIDER_ARROW` with sparks; smoke | `:459-471, 690-715, 1249-1294` |
| 353 Satyros | Monster110 | 1.3 | bright/chrome meshes | `:487-495, 998-1014, 1295-1319` |
| 354 Blade Hunter | Monster111 | 1.3 | hovers (Gravity 0); `MODEL_BLADE_SKILL` + explosions on attack; energy ring/field decals; chrome | `:472-486, 716-742, 973-997, 1320-1372` |
| 355 Kentauros | Monster112 | 1.1 | four `MODEL_KENTAUROS_ARROW` (multishot) + smoke | `:496-519, 743-796, 1015-1024, 1373-1422` |
| 356 Gigantis | Monster113 | 1.2 | light sprites | `:420-428, 928-945, 1132-1180` |
| 357 Genocider | Monster114 | 1.2 | `MODEL_STONE1/2` debris | `:429-439, 946-953, 1181-1220` |
| 553-555 Warriors | Monster198 / 199 / 200 | 1.15 / 1.3 / 1.5 | as their 2006 kin | `:520-563` |
| 556 Genocider Warrior | Monster201 (`MODEL_SOCCERBALL`) | 1.35 | as the Genocider | `:564-575` |
| 358 Persona | Monster115 | 1.0 | | `GM_Kanturu_2nd.cpp:57-70, 684-692, 763-868` |
| 359 Twin Tail | Monster116 | 1.3 | | `:71-85, 693-714, 869-984` |
| 360 Dreadfear | Monster117 | 1.3 | | `:86-105, 715-728, 985-1088` |
| 105 Canon Trap | `MODEL_TRAP_CANON` | 1.0 | | `:1385-1390` |
| 361 Nightmare | monster122 (`DARK_SKULL_SOLDIER_5`) | 1.6 | | `GM_Kanturu_3rd.cpp:667-706` |
| 362/363 Maya hands | monster119 / 120 | 2.28 | | `:707-772` |
| 364 Maya | **no body** | 0.2, HiddenMesh -2 | Maya herself is scenery in Object40 | `:773-781` |

- The 1.03.35 client agrees on every scale (`M135 GM_kanturu_1st.cpp:456-633`).
- Sounds: `D/Sound/w37/` holds ber_, blade_, geno_, gigan_, ir_, kenta_, sati_, swolf_ and the
  five `kan_ruin_*` loops; `w38` and `w39` hold the rest.

#### 4.6 What MU2_BGFX has

- **No Kanturu breed** in `content/monsters/*.json`: no `object_type` 350-364 or 553-556.
- **No world**: `content/world/` has aida, atlans, bloodcastle, charscene, devias, devilsquare,
  dungeon, icarus, lorencia, losttower, noria, tarkan.
- **No gate 125**: Tarkan's `world.json` `gates.enters` has only 55.
- Tarkan holds Object86-88, the Kanturu arch, its pillars and its steps, at 4.5,199.6, built "a
  sealed ruin with no gate behind it" (`content/world/tarkan/Object86.json`,
  `docs/worlds/tarkan-port.md:68, 989, 1463`).
- **Our levels today** (`content/monsters/*.json` `combat.level`):
  - Tarkan 72-93
  - Icarus 75-108
  - **Aida 110-140**: Death Tree 110 … Hell Maine 130, Bloody Orc 134, Bloody Death Rider 136,
    Bloody Witch Queen 140 (`docs/worlds/aida-port.md:425-447`)
  - Aida's door is level **300** (Noria gate 120 in `content/world/noria/world.json`)
  - `move_order`: Tarkan 6, Aida 7, Icarus 8

### 5. Drops

**WZ** (`gObjMonster.cpp:5542-5577`, under `g_bKanturuSpecialItemDropOn`):
- **Moonstone Pendant (13,38)**: any kill on **map 38 only**, at `KanturuMoonStoneDropRate` /10000.
- **Gemstone (14,41)**, the raw stone that Elpis refines into the Jewel of Harmony (14,42): any
  kill on **maps 37, 38 and 39**, at `KanturuJewelOfHarmonyDropRate` /10000.
- Both go to the top-damage player. The rates are `commonserver.cfg` keys (`WZ Gamemain.cpp:4037-4039`),
  default 0. **WZD's cfg has no Kanturu keys, so WebZen's rates are not on disk.**
- **Maya's hands and the Nightmare** open item bags `eventitembag32` / `eventitembag33`
  (`Gamemain.cpp:5316-5344`; `gObjMonster.cpp:3735-3790`), at `KanturuMayaHandItemDropRate` /
  `KanturuNightmareItemDropRate`. The bag files are not in WZD.

**S17** (`game.sql` `item_drop`, `a/s17drops_out.txt`; the roll is rate/10000, `S17 ItemMgr.cpp:3385-3402`):
- Moonstone (13,38): world 37 and 38 at 500 (`:18352-18353`).
- Gemstone (14,41): 37 and 38 at 500, 39 at 10000 (`:18355-18357`).
- **Ruins scrolls**:
  - monsters up to level 82: Orb of Greater Fortitude, Orb of Ice Arrow, Scroll of Ice Storm
    (15,17), Weakness / Innovation Parchment, Breche Scroll (`:18565-18570`);
  - monsters 85 and up: Scroll of Decay (15,16), **Scroll of Nova (15,18)**, Gigantic Storm,
    Wizardry Enhance (`:18583-18586`);
  - Mass Teleport from 351-354 (`:18723-18726`).
- The Warriors drop named weapons at 2000 (Bone/Explosion Blade, Phoenix Soul Star, Soleil Scepter,
  Sylph Wind Bow, Grand Viper Staff, Storm Blitz Stick; `:18648-18710`).
- MU's own item table backs the scroll levels: Scroll of Decay 96, **Ice Storm 93**, Nova 100,
  Kanturu's band (`WZD lang/kor/item(Kor).txt:396-398`). The Soul Master's three Season 2 spells
  (`AT_SKILL_DECAY 38`, `AT_SKILL_ICE_STORM 39`, `AT_SKILL_NOVA 40`, `MM _enum.h:326-328`) are
  Kanturu's drops. `items.h:207` already says "MU's [Nova] comes off Kanturu's bags".

**OM**: no Kanturu drop group, no Gemstone or Moonstone drop. The Moonstone is
`dropsFromMonsters = false`.

**Golden invasion** (post-2009, `ADD_GOLDEN_EVENT_RENEWAL`):
- **Golden Satyros (497)** spawns on a random Ruins tile (`WZ EledoradoEvent.cpp:887-930`).
- **Golden Twintail (498)** spawns on the Relics (`:932-975`).
- Both drop a **Box of Kundun +4 (14,11, level 11)** (`gObjMonster.cpp:4253-4267`). This agrees
  with `docs/game/kundun-box.md:26`'s "+5 Satyros, Twintail" one tier off.
- The Golden Great Dragon (501) goes to Ruins or Raklion (`EledoradoEvent.cpp:1066-1078`).

**Ours (decision, not record)**: the reward is **Ancient runes, not the Jewel of Harmony**
(`MU2_BGFX/src/sim/items.h:819-823`, `Rarity::Ancient`; `:993-997`, `kAncientLevel 141`; share
0.11, `:843-845`). The rune draw admits a rarity by the *killed monster's level*, so a breed below
141 never drops one, whatever map it is on. Note the fit: MU's **Ice Storm is a Kanturu skill
twice over**. Maya's hands cast it (S17 skill 25; WZD `Kanturu.dat` section 6 "Maya Ice Storm
Battle Skill Rate"), and its scroll falls in the Ruins. Ice Storm is already our first Ancient
(`items.h:1141-1152`).

### 6. Rules of the place

- **Entry**: Tarkan gate 125 at 150 leads to the Ruins camp. The way back is gate 127 at 130. The
  Relics door is 129 at 220 (S17 290), back by 131 at 150.
- **Death**:
  - Ruins: gate **138**, the camp (`WZ user.cpp:22219-22233`).
  - Relics: gate **139**, its camp (`:22234-22248`).
  - Boss map: gate **137**, the Relics camp, removing the player from the battle (`:22198-22216`).
  - A login on map 39 also lands at 137 (`:3253-3262`).
  - OM: the spawn gates 138 / 137 / 133 (`Gates.cs:271, 276, 283`).
- **Town Portal**:
  - WZ has **no Kanturu branch**, so the scroll falls through to **Lorencia, gate 17**
    (`WZ protocol.cpp:17877-18036`, the `else` at `:18036`).
  - The client allows it in 37/38 and not 39 (`PortalMgr.cpp:57-58`).
  - S17: no special case found.
- **Move Charm** works in 37 and 38 (`WZ user.cpp:34140-34141`).
- **Summoning a party (Dark Lord)** is not allowed into map 39 (`WZ MoveCommand.cpp:279-283`).
- Monsters on map 39 never respawn (`WZ user.cpp:21655-21660`).
- No PK rule, no exp multiplier, no oxygen.
- **Flight**: the boss map only (§2.3).

### 7. The Nightmare event (summary; flag as LATER)

State machine `CKanturu`: None, Battle Standby, Battle of Maya, Battle of Nightmare, Tower of
Refinement, End (`WZD Kanturu.dat` sections 0-9; `WZ Kanturu.cpp`; S17 `EventData/Kanturu.txt`).

1. **Standby**: 300 s, then a 60 s notice (WZD). S17 uses 60/60.
2. **Battle of Maya**, three scenes of 900 / 900 / 1200 s. Each scene runs: standby (120 / 60 / 120 s),
   notice 15 s, a wave of Blade Hunters, Dreadfears, Twin Tails and Personas, then the hand(s):
   - scene 1: the **left hand**;
   - scene 2: the **right hand**;
   - scene 3: **both** (`WZ KanturuBattleOfMaya.cpp:566-575, 733-745`).

   The hands cast Pressure, Power Wave, Ice Storm and **Broken Shower**, the last of which kills
   anyone without the pendant. Up to 15 players enter from the Relics through the Gateway Machine,
   only during the standby. The battle **fails** if every player dies or time runs out, and all
   are moved back to the Relics (`:282-291, 785-807`).
3. **Battle of Nightmare**: 20 min (1200 s). The Nightmare (1.5 M HP) warps between four points,
   summons and poisons (`S17 ai_kanturu.cpp:326-348`; skills 18-22). Winning swaps the boss grid
   (`EncTerrain401`, §1.3).
4. **Tower of Refinement**: open for 82 500 s in WZD (S17 21 600). A notice comes 300 s before it
   closes, then a 2 220 s close phase and 180 s more, after which everyone is thrown out to Relics
   gate 136 (`WZ KanturuTowerOfRefinement.cpp:237-304`). **Elpis** stands inside and refines
   Gemstones into the Jewel of Harmony.
5. **Rewards**: the hands' and Nightmare's bags; no exp for 361-363.

**For us**: this is a 15-player instanced raid, with an item gate (the pendant), a flight gate and a
Harmony NPC we do not have. It is out of scope for the first build. Even the Relics can ship
without it.

### 8. Where the sources disagree

| point | OM S6 | WZ C++ | client tables | WZD | S17 | settled / flagged |
|---|---|---|---|---|---|---|
| era | S6 only | 2006-05/06 switches | | | | **Season 2, 2006** (decision 1) |
| Ruins grid | = client | | client | pre-2010 (no island) | re-cut | **client** |
| Relics grid | re-cut (+656) | | client | **= client** | re-cut | **client** |
| Ruins spawns | 151 + 59 island | none on disk | | none | 149 + 37 | **OM's** |
| stats | as §4.4 | none | | none | = OM except Berserker and the Warriors | **OM**, unchecked by WebZen |
| MoneyRate / MaxItemLevel | none | cfg keys | | none | 14-20 / 3-4 | S17's are the only numbers |
| Relics door | 220 | gate data | 220 | | **290** | 220 (ours: decision 3) |
| warp Remain / island | 230 / 160 | | 230 / 160 | | 300 / 380 | client |
| Town Portal | spawn gate | **Lorencia (17)** | allowed in 37/38 | | | decision 4 |
| skills | none | data-driven (empty) | 14-26, 70, 71 | empty | named, same ids | client / S17 names |
| Maya / Nightmare | absent | full | bodies | timings | full | later |
| Moonstone | equip requirement only, no drop | ring slot, durability, map-38 drop | | | map 37+38 drop | later |
| Gemstone drop | none | 37-39 (cfg rate) | | | 5% / 5% / 100% | **ours: Ancient runes instead** |

### 9. Open questions

1. Our `content/mu.db` no longer has monster or gate tables. Aida's "insert into monster_kinds" path is
   gone, and kinds are `content/monsters/<Name>01.json` `combat`. The build goes through the
   Cooker (`mu new monster`, `mu edit`).
2. Breed numbers 350-364 and 553-556 are under and over 255. Aida already uses 304-309 and 549-552,
   so the >255 path is proven.
3. Maya has no body file. If the event is ever built, she is Object40 scenery.
4. The Golden Satyros and Twintail are Kundun-box work, not this port.

### Where Kanturu sits

Our worlds by level:

| world | levels |
|---|---|
| Lorencia | 2-19 |
| Devias | 20-52 |
| Lost Tower | 47-66 |
| Atlans | 43-74 |
| Tarkan | 72-93 |
| Icarus | 75-108 |
| **Aida** | **110-140** |

MU's Kanturu:

| part | MU levels |
|---|---|
| Ruins | 80-105 |
| Relics | 117-119 |
| island | 123-129 |
| event | 135 |

**All of it is below our Aida.** Ancient runes start at 141 (`items.h:996`). So a faithful-number
port lands between Icarus and Aida and drops no Ancient. An Ancient-rune Kanturu must be raised as
Aida was, with the Ruins' first breed above the Bloody Witch Queen (140). MU's own ladder and its
gate levels (150 in, 160 warp) put Kanturu after Aida, which matches.

### Side findings (outside Kanturu)

1. **`MU2_BGFX/source/mu.db` is gone.** `content/mu.db` is the account database. The brief's
   `sqlite3 -readonly MU2_BGFX/source/mu.db` fails. (The aida/tarkan docs still cite `source/mu.db`.)
2. **The S17 server source is an emulator** (Dimension Gamers / VNdev), not WebZen's. The memory
   note "WebZen server source outranks OpenMU" refers to WZ 1.00.93 (re-cloned here), not S17.
3. `items.h:1141-1145` calls Ice Storm "MU's AT_SKILL_BLAST_FREEZE (S17 skill_template 39)". MuMain
   names skill 39 `AT_SKILL_ICE_STORM` (`MM _enum.h:327`), the Season 2 Soul Master scroll
   (15,17) that drops in Kanturu.
4. `D/World39/TerrainServer.att` is not a Relics grid (every value 0-255 occurs).

---

### The one thing to know first

Kanturu is Season 2 (2006). Its Ruins are one long 599-step loop from a camp beside our already-open
Tarkan door (266 steps from Tarkan's town) to the Relics door, with eight breeds laddered cleanly
along the walk. But MU's levels (80-129) all sit **below our raised Aida (110-140)**. As Ancient
runes need 141+, Kanturu must be raised past Aida to be the Ancient-rune map. Everything else (the
grid, the gates, the spawns, the bodies, the music) is on disk and agrees.

### Decisions for the user

1. **Port a Season 2 world?** It is a year after Aida, with its record in OM, the client tables, WZ's
   rules and S17. *Recommended*: yes, Ruins first.
2. **Levels**: raise every Kanturu breed past 141 so the Ancient runes fall, keeping MU's order. An
   example: Splinter Wolf 142 up to Genocider 156; the Relics 160-165; the island Warriors (if
   ever) 168-172. Or keep MU's numbers and give up on Kanturu as the Ancient map. *Recommended*:
   raise, as Aida was raised.
3. **Gate levels**: MU uses 150 in, 130 out, 220 to the Relics, warp 160/230. Ours: Aida's door is 300.
   *Recommended*: the Tarkan door just over Aida's (e.g. 320 or by the raised breeds), the way out
   free, the Relics door higher.
4. **Town Portal**: WZ sends it to Lorencia; OM to the camp; death is the camp in all. *Recommended*:
   death at the camp (gate 138), Town Portal to Tarkan's town (gate 57). The Ruins have no merchant,
   and Tarkan is 266 steps from the door.
5. **Which Ruins**: the 2006 map only (151 OM spots), with the 2010 island and its Warriors left
   closed. *Recommended*: 2006; the island later as a higher band.
6. **Relics and the event**: the Relics (Persona, Twin Tale, Dreadfear, traps) as a second phase; the
   Maya/Nightmare event, the Moonstone Pendant, Elpis and the Gateway Machine **later or never**.
   *Recommended*: Ruins now, Relics next, event parked.
7. **Drops**: Ancient runes as the map's reward (decided). Also: the Scroll of Nova (and Ice Storm /
   Decay scrolls?) as Kanturu drops, as MU had them? No Gemstone, no Moonstone.
8. **NPCs in the camp**: MU has none. Add a potion seller (as Aida's Amy) or none?
9. **Monster skills**: the Kentauros multishot and the Iron Rider's arrows (ranged), the Blade
   Hunter's blade burst, the Splinter Wolf's red eyes. *Recommended*: the ranged two as ranged,
   the rest as looks.
10. **Gate-box overlap**: arrival 126 overlaps door 127. Land on 138's box (the camp) instead.
11. **MoneyRate / MaxItemLevel**: only S17's (14 / 3-4). Use them, or Aida's?

---

## Part B: the data and the look (Kanturu)

Research notes, 2026-10-21. Nothing tracked was edited. Nothing was cooked or written to mu.db, and no window was opened. Every pipeline run went into scratch.

Kanturu is three server maps:
- **37 Kanturu Ruins** (`WD_37KANTURU_1ST`, `MM/World/MapInfra/MapManager.h:32`), client `D/World38` + `D/Object38`
- **38 Kanturu Relics** (`WD_38KANTURU_2ND`, `:33`), `D/World39` + `D/Object39`
- **39 Refinery Tower** (`WD_39KANTURU_3RD`, `:34`), `D/World40` + `D/Object40`

The folder is the map number plus 1. Every table key below is the **map** number (37/38/39). terrain.py is run with the **folder** number (38/39/40) and keys its tables by `number - 1` (`pipeline/terrain.py:594, 600, 769`). The build starts with the Ruins, so §1-§7 go deep there and §8 covers the Relics and the Tower.

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- S17 = `MU2_BGFX/third_party/MuClient_S17_v1.19.74/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- S = `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/65a47bc4-0559-4919-933f-a8857d745944/scratchpad/kanturu/b/`

Scratch outputs in S:
- `kanturu38/`, `kanturu39/`, `kanturu40/`: `pipeline/terrain.py D/World{38,39,40} kanturu{N} S {N}` (height, tiles, attributes and light .png, and kanturu{N}.json). No manifest names maps 37-39, so nothing is flagged hidden. The runs are `terrain38.log`, `terrain39.log` and `terrain40.log`.
- `kanturu40s/`: the Tower after the battle. It is the same run on a scratch copy of World40 with `EncTerrain401.att/.map/.obj` put in place of the 40 files (`terrain40s.log`).
- `obj38/`, `obj39/`, `obj40/`: every .bmd in Object38, Object39 and Object40 through MuExtract `export-obj` and `export-rig --actions=all`, with a .log for each. `objinfo{38,39,40}.log` gives, per model type: placements, tris, bones, actions, keys, bbox (cm, y up), scale range, height over the ground (dz, m; median and range), how many stand on NoGround or walkable tiles, the tile x/y range, and the sheets with their tri counts.
- `s17obj38/`: every S17 Object38 .bmd through `export-obj`, for the version comparison (§1.4).
- `tex/World{38,39,40}/`, `tex/Object{38,39,40}/`: every .OZJ decoded. The .OZT files are decoded into a separate `ozt/` subfolder, because twins share one stem (§3).
- `cmp/`, `tex/World{1,3,4,9,34}/`: Lorencia, Devias, Noria, Tarkan and Aida re-extracted for the colour comparison (§6). My numbers for those five match Aida's Part B §6 table to the unit.
- **Contact sheets:**
  - `sheet_objects38.png` (also `sheet_objects39.png`, `sheet_objects40.png`): every model, flat-shaded with one texel colour per triangle, on a magenta background.
  - `sheet_object38_textures.png` and `sheet_object38_ozt.png`: every Object38 sheet, with alpha shown as magenta.
  - `sheet_world38.png`: the 14 tile sheets, TerrainLight and leaf02. `sheet_world38_ozt.png`: the two grass strips, leaf01, rain and the Season minimap.
  - `prev{38,39,40}_l1_attr_height_light_painted.png`: five panels with row 0 at the bottom. They are layer 1 by slot (void dimmed); the attributes (grey open, dark red NoMove, black NoGround, green safe, dark green safe+NoMove); height; TerrainLight; and sheet mean × light, which is what MU's ground paints. Magenta marks a slot whose sheet the folder does not ship.
  - `prev38_objects_on_attr.png`: placements over the attribute map, with the hanging cliffs Object06/76 in orange.
  - `light{38,39,40}_big.png`: TerrainLight at 3×.
- Scripts are in `scripts/`: `dec.py`, `stats.py`, `prev.py`, `objinfo.py`, `island.py`, `look.py`, `render.py`, `sheet.py`. Logs: `stats{38,39,40}.log`, `island.log`, `look.log`, `tex.log`.

### The one thing to know first

**Kanturu Ruins is a broken ring of dark, mossy ruin-country round a black chasm, with a separate plateau of dark earth in the chasm's middle.** The ring carries ruined stone factories, rope bridges, carnivorous plants and pink crystals. Its inner lips are hung with 14 m cliff faces and waterfalls. The plateau is cratered by seven pits full of pink crystals, and it is reached only by warp. The painted ground is as dark as Aida's (walkable sheet × light luminance **29**, the same as Aida, against Lorencia's 46 and Noria's 49). It is browner and more saturated (hue 43°, saturation 0.49 against Aida's 47° / 0.37). The light is a near-neutral, slightly green grey, speckled with baked coloured lamp pools: gold, teal, pink.

- **Almost half the map is never drawn.** 29,348 tiles (**44.8%**) are NoGround, against Aida's 34.5%. MuMain skips them, and the clear colour shows. Kanturu has no arm in `SetWorldClearColor`, so the clear is **black** (`MM/Scenes/SceneManager.cpp:381-402`, default arm `:401-402`). Fog is that same black (`SetClearAndFogColor`, `:356-364`).
- **The chasm's walls are deep.** Object06 and Object76 (`han_kcliff2`, 394 placements, 369 of them over NoGround) are 14.5 m faces that hang 13.6 m below their lip (bbox y −1361…+90). At their scales (0.37-1.03) they reach 5-14 m down. They are twice as deep as Aida's walls.
- **The plateau has no walls.** Its rim slopes from 2.25 m down to 0 m, and MU painted its TerrainLight black there (pit and slope light mean 34). The plateau fades into the chasm by paint and slope; MU did not put walls round it.
- **Life on the map:** 63 flickering fire lights (58 amber, 5 red-orange), 58 butterfly emitters, 52 animated waterfalls with 113 spray boxes, 16 smoke vents, 46 spitting carnivorous plants, 128 glowing crystals with blue sparks, a turning wheel and gears, electric arcs, about 900 rigged placements and pale sunbeams. There are no birds, no fish or crawlers, no leaves and no weather. The 148 mist boxes are dead in MuMain (§2).
- **Season content.** Ruins wavs are dated Jul 2006 and the music Aug 2006. Every rule is in `MM/World/GameMaps/GM_Kanturu_1st.cpp` (`M37Kanturu1st::`). Pass A owns the dating.

---

### 1. Raw data inventory

#### 1.1 World38 (`D/World38`, 34 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain38.map / .att / .obj | 196610 / 131076 / 109354 | yes | layers, 16-bit attributes, **3645 placements of 100 kinds** |
| EncTerrainTest38.map/.att/.obj | same | no | **byte-identical** to the Enc files (unlike Aida's) |
| Terrain.att/.map/.obj | 131075 / 196609 / 109353 | no | plain copies; nothing reads them |
| TerrainHeight.OZB | 66620 | yes | read at 1080 (header says 1078, as every world) |
| TerrainLight.OZJ | 67898 | yes | 256² baked light, mean (120, 123, 118), speckled with colour (§1.2) |
| TileGrass01-02, TileGround01-02, TileRock01-03, 05-07, TileWater01 .OZJ | 101-150 KB | yes (`MM/World/MapInfra/MapManager.cpp:1362-1447`) | the painted sheets |
| **TileGround03, TileRock04, TileWood01 .OZJ** | 14320 each | yes | **one file three times (same md5): a flat white 256² square.** Placeholders. Worn by 6 + 1 + 28 drawn layer-1 tiles and 3 + 4 + 0 overlays |
| **TileGrass01.OZT** | 65584 | yes, BITMAP_MAPGRASS (`MapManager.cpp:1465-1466`) | 256×64 strip of **dark olive bushy tufts** (shrub clumps, not blades), 39% coverage, mean over alpha (58, 58, 29) |
| **TileGrass02.OZT** | 65584 | yes, MAPGRASS+1 (`:1469-1470`) | 256×64 strip of **pale blue-white wispy blades**, 22.6% coverage, mean (219, 231, 233) |
| leaf01.OZT / leaf02.OZJ | 1072 / 4445 | loaded (`:1474-1480`), never used | Kanturu is not in `RequireLeavesEffect` (`MM/Scenes/MainScene.cpp:78-100`) |
| rain01/02.OZT | 560 / 2096 | no | MU loads rain from World1 (`MapManager.cpp:1482-1494`); Kanturu has no weather |
| mini_map.OZT, Minimap.bmd | | Season UI | 1024² minimap, void transparent, drawn on a diagonal |

terrain.py (`S/terrain38.log`, `S/stats38.log`):

```
height     0.00 to 3.83 m; drawn p5/50/95 0.00/2.25/2.68; walkable 2.01/2.25/2.51; void flat 2.25 (2,031 void tiles at 0)
tiles      14 slots in use (0-13)
walkable   27.4% (17,954), 2.7% safe (1,738 tiles, 639 of them walkable)
NoGround   44.8% (29,348) -- undrawn
light      mean [120,123,118]; drawn [105,109,101]; walkable [127,132,122]; safe walkable [135,132,128]; walkable lum p10/50/90 75/126/194
objects    3645 placed, 100 kinds; every placed model is on disk (no "missing" line)
```

**Attributes:**

| value | tiles | meaning |
|---:|---:|---|
| 0 | 17314 | open |
| 1 | 639 | Safe |
| 2 | 1 | |
| 4 | 17310 | NoMove |
| 5 | 924 | Safe+NoMove |
| 8 | 82 | NoGround |
| 9 | 1 | Safe+NoGround |
| 12 | 29091 | NoMove+NoGround |
| 13 | 174 | Safe+NoMove+NoGround |

- There is no Water bit (0x10) anywhere.
- Height: walkable ground is nearly flat (p5-p95 spread 0.5 m). The relief is the plateau's rim and pits falling to 0 m, plus the objects.

**Map shape** (`prev38_*.png`, `prev38_objects_on_attr.png`, `island.log`). Directions below are tile coordinates, with "north" meaning low y.

- **The ring:** one walk region of **12,432 tiles** (x 16-242, y 3-243). It is a broken C round the chasm: the safe hall in the south-west corner, the west and north-west ruins, the north side and the east arm.
- **The safe hall:** x 9-80, y 174-233. It has 1,738 safe tiles, 639 of them walkable. The walkable safe tiles cover x 16-80, y 174-222, centred at 34,208. Its floor is TileRock03 (270), TileRock07 (108), TileRock06 (107) and TileGround01 (73). Its light is (135, 132, 128).
  - OM's spawn gate 138 is at (19-21, 217-219), and Tarkan's door lands at 126 (17-21, 219-220).
  - Tarkan's enter gate 125 is at Tarkan's west edge (6, 199-201).
  - The Ruins' enter gate 127 to Tarkan is at (17-19, 220-222).
  - Source: OM `VersionSeasonSix/Gates.cs:276, 279, 537-538`, with Tarkan's exit 128 at `:201`. Pass A owns the gates.
  - **Tarkan is already built**, so the Ruins join a world we have.
- **The plateau:** a second walk region of **5,514 tiles** (x 45-194, y 79-188). It has its own safe box (142 walkable safe tiles) at about x 60-80, y 175-200, which is warp gate 334, "KanturuRuins3" (66-74, 183-191; `Gates.cs:82, 278`).
  - **No walk joins it to the ring.** The nearest plateau walk tile is 18.4 tiles from the ring's closest point.
  - That closest point is the tip of a thin pier on the ring's south-west, the Relics door: enter gate 129 at (89, 89-92) and Relics' arrival 132 at (85-86, 89-92) (`Gates.cs:280, 539-540`).
  - So the plateau is a warp-only zone. Pass A should confirm whether that is intended.
- **The plateau's look:** dark earth TileRock07 (4,466 of its walkable tiles). Seven NoMove **pits** sit at 0 m on TileRock05 pebbles (3,968 tiles counting the rim slope), painted black (light 34). They hold the pink crystals Object96 (59 on the plateau) and butterfly boxes. The plateau's rim is a 2,992-tile NoMove ring sloping from 2.25 m to about 1.2 m, then the slope runs on to 0 m.
- **The chasm:** the void between ring and plateau, and round the map's edge. 2,031 void tiles carry height 0 (the plateau's foot); the rest sit at a flat 2.25 m.
- **Stray tiles:** 8 open tiles in 7 crumbs on rows 251-255, which nothing reaches. There are also 12 tiny drawn components (206 tiles) on the map's edge.
- **Water:** TileWater01 is on 789 drawn tiles, 356 of them walkable. Most of it sits in the east arm (277 tiles in x 192-255, y 128-191) and the west (153 in x 0-63, y 64-127): pools under the waterfalls.

**Tile sheets** (`sheet_world38.png`; "drawn" = not NoGround):

| slot | name | actually | l1 drawn (% of drawn) | l1 walkable | layer-2 overlays drawn |
|---|---|---|---:|---:|---:|
| 0 | TileGrass01 | **mossy olive ground with green clover clumps** | 7263 (20.1) | 3387 | 22 |
| 1 | TileGrass02 | dark brown earth with dead grass | 2249 (6.2) | 980 | 2115 |
| 2 | TileGround01 | bright olive moss | 186 (0.5) | 126 | 1126 |
| 3 | TileGround02 | moss over buried brick | 379 (1.0) | 222 | 556 |
| 4 | TileGround03 | **white placeholder** | 43 (0.1) | 6 | 3 |
| 5 | TileWater01 | green-grey water | 789 (2.2) | 356 | 1 |
| 6 | TileWood01 | **white placeholder** | 28 (0.1) | 28 | 0 |
| 7 | TileRock01 | **the same painting as TileGrass01** (md5 differs, mean 71/66/41 against 72/66/38) | 1327 (3.7) | 991 | 1265 |
| 8 | TileRock02 | dark red-brown cracked earth | 1640 (4.5) | 673 | 5 |
| 9 | TileRock03 | brown dirt and pebbles (the safe hall, the east arm) | 5207 (14.4) | 1847 | 2421 |
| 10 | TileRock04 | **white placeholder** | 1 | 0 | 4 |
| 11 | TileRock05 | brown pebbles and stones (the plateau's pits and rim) | 5554 (15.3) | 402 | 957 |
| 12 | TileRock06 | mossy flagstone paving (the west ruins) | 2584 (7.1) | 2053 | 79 |
| 13 | TileRock07 | dark brown packed earth (**the plateau's floor**, 24.7% of drawn) | 8938 (24.7) | 6883 | 505 |

Unlike Aida's red carpet, no sheet here is a stray from another map. But three slots (4, 6 and 10) are worn by 72 drawn tiles and 7 overlays of a **flat white** sheet.

**Light map** (`light38_big.png`):
- **Base:** a neutral grey over the ring, near-black on the plateau's pits and in a halo round the plateau's rim, and bright white rings round the plateau's walkways.
- **Coloured specks:** hundreds of small baked lamp pools in gold and amber (R−B > 30 on 4,538 drawn tiles, mean (126, 108, 79)) across the west, south and north ruins. Teal and blue on 2,695 tiles, mean (83, 138, 132), mostly as **a broad teal wash over the east arm**, the waterfall and pond country. Some pink-violet in the south-east. Green on 291 tiles (x 128-239, y 85-189).
- **How it differs:** this is the busiest TerrainLight of any world ported. Aida's light is a neutral grey with dark halos; this one is a baked render full of coloured point lights.

#### 1.2 Object38 (`D/Object38`, 208 files)

There are 111 `ObjectNN.bmd` and 97 sheets (80 .OZJ, 17 .OZT). None are monster sheets. Type = file number − 1.

**No placed model is missing.** Shipped and unplaced:
- Object49 (a byte-for-byte twin of Object10's geometry)
- Object56
- Object71 (a machine with `machine04_R`)
- Object101-107: `Mcanwall*`, `Meffec_R`, `Tileeffec01_R`, `han_dd1`/`handd2_R`, `han_x2`. This is refinery tech; Object106 and Object107 are the Relics' Object09 and Object36 again.
- Object111

MuMain has arms for some of these unplaced types (70, 102, 103, 105, 110). The arms also run in `WD_40AREA_FOR_GM` (`GM_Kanturu_1st.cpp:44, 130, 310, 343`), which may place them. Not checked.

Placed models (`S/objinfo38.log`; rig "b" = bones, keys = longest action):

| type | model | placed | tris | rig | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 0-3 | Object01-04 | 9/3/3/5 | 469-1420 | 1 | cho_obj01-05 | **ruined wooden-and-stone houses** (Tarkan's `cho_obj` set), 5-8 m |
| 4 | Object05 | 1 | 1897 | 5 b, 9 | cho_obj*, han_w1/2 | the big ruin with a moving part, at (148, 38) |
| **5, 75** | **Object06, Object76** | **208 / 186** | 233 / 241 | 1-2 | han_kcliff2 | **hanging cliff faces**: 5 × 14.5 m, hanging **13.6 m below the lip**; 193 / 176 over NoGround. The chasm's walls, on the ring's inner lips only (`prev38_objects_on_attr.png`, orange) |
| 6, 72 | Object07, 73 | 84 / 27 | 72 / 87 | 1 | stone_01 | stones, small rock heaps |
| 7-8, 10 | Object08, 09, 11 | 4/8/6 | 56-160 | 1 | cho_obj* | pillars, barrels |
| 9, 13 | Object10, 14 | 3 / 5 | 348 / 354 | 1 | cho_factory*, han_moss4/5 | **ruined factories** with hanging moss |
| 11-19 | Object12-20 | 1-45 | 19-134 | 1 | han_w1-3, song_k0w2 | broken walls, wall ends, columns (2-3 m) |
| 20, 21 | Object21, 22 | 31 / 78 | 56 / 75 | 1 | han_b2 / han_mwall* | ground slabs and low wall runs, **flat at scale 0.2-0.6** |
| 22, 34 | Object23, 35 | 50 / 33 | 234 / 58 | 1 | mingwall*, song_k0r1 | thatched / rubble walls |
| 23-25, 28 | Object24-26, 29 | 5/11/16/5 | 71-361 | 1 | song_k0* | **thatched huts and lean-tos** (the "song_k0" village set, shared with Object1) |
| 26 | Object27 | 30 | 54 | 1 | song_k0w1, han_w1 | wall planks |
| **27** | Object28 | 26 | 568 | 1 | kan_tree | **dead twisted trees**, 5.6 m |
| 29-31 | Object30-32 | 22/21/19 | 54-81 | 1 | han_puzzle, han_w* | carved stone blocks |
| **32** | Object33 | 67 | 162 | 7 b, 13 | han_fftreeleaf.tga, han_fftreebody | **palm/fern trees**, swaying |
| 33, 41 | Object34, 42 | 6 / 4 | 61-68 | 7-13 b, 12-13 | song_k0a2.tga (cloth) | **hanging banners / sailcloth**, swaying |
| 36 | Object37 | 50 | 80 | 1 | cho_tree_b | fallen logs, 6 m long |
| 38, 39 | Object39, 40 | 3 / 1 | 12-45 | 5-9 b, 9-17 | song_k0a4.tga / song_k204a2.tga | **nets and chains**, swaying |
| 40 | Object41 | 13 | 98 | 12 b, 17 | cho_obj03 | arched roots, animated |
| 42, 87 | Object43, 88 | 17 / 1 | 70-104 | 1 | song_k3mmater03 | iron vats |
| 43 | Object44 | 14 | 134 | 10 b | song_k20* | machine housing |
| **44** | Object45 | 22 | 92 | 1 b, 6 | song_k202w1 | **a gear wheel**, 2 m, `Velocity 0.02` (§2) |
| 45 | Object46 | 38 | 68 | 1 | song_k202w1 | a half-wheel frame |
| **46** | Object47 | 43 | 68 | 2 b, 6 | song_k202w1 + **songk3matereff_R** | **the glowing wheel**: `Velocity 0.01`, its _R mesh pulsing, the wheel sound (§2) |
| 47, 88, 93 | Object48, 89, 94 | 14 / 6 / 6 | 162-288 | 1 | ming_dari01, ming_rope | **rope bridges**; Object89/94 span the gaps at x 62-198, y 37-50 |
| 49-55 | Object50-55 | 2-93 | 8-96 | 1-2 | han_mwall*, han_moss4/5 | mossy walls and logs (the north-west ruins, x 15-104) |
| 57 | Object58 | 46 | 40 | 1 | song_k3metal0202, song_k202w1 | iron girders |
| 58 | Object59 | 15 | 288 | 1 | cho_factory05, han_moss* | a 5.8 m factory chimney, moss-hung |
| **63, 94** | Object64, 95 | 6 / 10 | 312 / 605 | 22 b, 13 / 17 b, 14 | cho_tree_a.tga / song_k3mtree* | **big leafy trees**, swaying; Object95 is 10 m |
| 64 | Object65 | 15 | 184 | 1 | cho_tree_a.jpg | dead tree |
| **65** | **Object66** | **232** | 61 | 1 | Demorock | **rubble rocks**, the most placed kind |
| 66, 86 | Object67, 87 | 17 / 25 | 50 / 30 | 1 | cho_gate_c / cho_gate_b | gate posts and fence boards (Tarkan's sheets) |
| **67, 68** | **Object68, 69** | **198 / 225** | 24 / 48 | 5 b, 13 | cho_grass_a.tga / cho_grass_c.tga | **tall grass and fern tufts**, swaying (1.2-1.4 m) |
| 69 | Object70 | 34 | 96 | 11 b, 7 | Pool001.tga | **ferns**, swaying |
| 71 | Object72 | 91 | 100 | 1 | song_k3metal0202, song_k202w1 | iron pipes and beams, many sunk into the void |
| **73** | **Object74** | 51 | 12 | 6 b, **20** | **Treeange_R** | Aida's glowing 16 m "tree angel" branch, here as **additive light streaks**, scale 0.28-1.0, dz +2.5 m |
| 74 | Object75 | 47 | 106 | 1 | wall_01 | a 10 m earth bank, flat at scale 0.26-0.67 |
| **76** | Object77 | 10 | 2 | 1 | sunlight2.tga | **sunbeam planes**, 3 × 8.7 m (not `_R`: alpha 0.5 plus a streamed bright pass, §2) |
| **77** | **Object78** | 52 | 28 | 16 b, 16 | **waterfall_R** | **waterfalls**, 5.7 m strip ±2.8 m about the root; 21 over void (§2) |
| 78 | Object79 | 25 | 92 | 1 | mingwall | stone towers |
| 79, 80 | Object80, 81 | 123 / 83 | 168 / 60 | 1 | han_cwall | **earth cliff-banks** on the map's north edge (x 11-168, y 203-247), at scale 0.2-0.65 |
| 84 | Object85 | 34 | 94 | 1 | mingtorch | **torch stands** (34 of the 58 fire boxes stand by one, §2) |
| **85** | Object86 | 3 | 436 | 1 | cho_gate_a*, **chogatea02_R** | **the gate to Tarkan**, by the safe hall (x 15-71, y 191-223). Tarkan's own Object86 is the same gate (448 tris, `content/world/tarkan/Object86.obj`) |
| **89** | Object90 | 16 | 2 | 4 b, 14 | **han_R** | a flat 1.5 m additive plane of green motes, animated, 0.9 m up: **fireflies** over walkable ground |
| 90 | Object91 | 19 | 128 | 1 b, 10 | song_k202w1 | a wheel, `Velocity 0.04` |
| 91, 92 | Object92, 93 | 1 / 2 | 16 | 1-3 | **songk3mmater0302_R** | blue liquid tanks; **Object93 throws electric arcs** (§2) |
| **95** | **Object96** | **128** | 161 | 11 b | **songk3crystal_R** | **pink crystal clusters**, up to 4 m; 59 in the plateau's pits; chrome and blue sparks (§2) |
| 96 | Object97 | 1 | 36 | 1 | cho_gate_d.tga | the gate's portal veil, streamed (§2), at (16, 223) |
| **98** | **Object99** | 46 | 174 | 26 b, 13 | iplant, ipetc.tga, ipuper.tga | **carnivorous plants** that spit green water and gurgle (§2) |
| 99 | Object100 | 80 | 54 | 14 b, 12 | iplant | small plant sprouts, swaying |
| 109 | Object110 | 5 | 58 | 1 | cho_scarp_02 | 4 m scaffold poles, sunk 7.6 m into the void |
| **37, 59-62, 81-83, 97, 107, 108** | Object38, 60-63, 82-84, 98, 108, 109 | 58/16/5/58/60/26/35/52/1/77/11 | 12 | 1 | angeflo_R | **hidden emitter boxes**, 50 cm (§2). Every angeflo_R box in the map is one of these 11 types, **399 placements** |

**Placement counts, most first:** Object66 232, Object69 225, Object06 208, Object68 198, Object76 186, Object96 128, Object80 123, Object55 93, Object72 91, Object07 84, Object81 83, Object100 80, Object22 78, Object108 77, Object33 67, Object63 60, then Object38, Object62, Object78, Object84, Object74, Object23, Object37 and Object75 at 47-58 each.

**Rigged placements: 897** (`objinfo38.log`): Object69 225, Object68 198, Object100 80, Object33 67, Object78 52, Object74 51, Object99 46, Object47 43, Object70 34, Object45 22, Object91 19, Object90 16, Object41 13, Object95 10, Object64 6, Object34 6, Object42 4, Object39 3, Object40 1, Object05 1. That is about half of Aida's 1,676. 423 of them (the two grass-tuft kinds) share one 13-key clip.

**Kinds by role** (for the recipe order):
- **grass and plants:** Object68, 69 tufts; Object70 ferns; Object100 sprouts; Object99 carnivorous plants
- **trees:** Object28 dead, Object33 palms, Object64/95 leafy, Object65; Object37 logs
- **rocks and banks:** Object66 rubble, Object07/73 stones; Object80/81 earth banks; Object75; **cliffs Object06/76**
- **ruins:** Object01-05, 10, 14, 59 factories and houses; Object12-27, 29-32, 50-55 walls, columns and blocks; Object23/35/79 thatch and stone walls; Object24-26/29 huts; Object21/22 slabs
- **machines and iron:** Object43-47, 58, 72, 88, 91-93
- **bridges and rope:** Object48, 89, 94; banners and nets Object34, 39-42
- **lights and glows:** Object47, 74, 77, 85 torches, 86 gate, 90, 92/93, 96 crystals
- **water:** Object78 waterfalls; the ponds are terrain
- no roofs proper; the huts are open lean-tos

#### 1.3 Shared art (name collisions, §3)

Object38 shares many sheets with Tarkan (Object9: cho_gate_a, cho_gate_a_01, cho_gate_a_03, cho_gate_b, cho_gate_c, chogatea02_R). It shares the `cho_obj*`, `song_k0*`, `song_k20*`, `han_w*` and `kan_tree` sets with D/Object1. It shares a great deal with Object39, Object40 and Object69 (refinery, crystal and moss sheets), and `Treeange_R`/`sunlight2` with Aida's Object34.

#### 1.4 The S17 client (`S17/World38`, `S17/Object38`)

- **Map files:** S17's `EncTerrain38.att/.map/.obj` are a newer container. They start `MAP\x01`, the .att and .map are 38 bytes longer, and the .obj is 114,304 bytes against 109,354, so it plausibly holds about 165 more placements. **terrain.py cannot read them.**
- **TerrainLight** is an `.OZB` (a 24-bit BMP) instead of `.OZJ`.
- **TerrainHeight.OZB** differs on **3,379 tiles** (816 of them walkable, 987 void), a mean of −0.14 m. That is a touch-up of the same map, not a new one.
- The tile sheets and grass strips are **byte-identical**.
- **Object38's 111 .bmd** are BMD version 14 against MuMain's 12, and MuExtract reads every one (`check-bmd`: 0 failed). **Every model has the same triangle count** in both clients (`S/s17obj38/`).
- **Use MuMain's World38/Object38.** S17 adds nothing visible we can read.

---

### 2. MuMain's special cases for Kanturu Ruins (WD_37KANTURU_1ST)

Every rule runs through `M37Kanturu1st::IsKanturu1st()` (`GM_Kanturu_1st.cpp:29-32`). The object arms are also live in the GM Summon Zone, `WD_40AREA_FOR_GM`.

Hooks:
- CreateObject `MM/Engine/Object/ZzzObject.cpp:4820`. The arm is empty: `CreateKanturu1stObject` returns true and does nothing (`GM_Kanturu_1st.cpp:34-40`).
- MoveObject `:4240`
- RenderObjectVisual `:3282`
- RenderObjectMesh `:1153`
- After-character pass `:3499`, which is enabled for 37-39 at `:3513`

**Frame, light, fog, sky**
1. **Clear and fog: black**, the default arm (`MM/Scenes/SceneManager.cpp:401-402`; fog colour = clear colour, `:356-364`). There is no sky.
2. **NoGround is undrawn** (`ZzzLodTerrain.cpp:2178`, as for Aida): 44.8% of the map.
3. **No birds** (the boid list omits Kanturu, `MM/Engine/AI/GOBoid.cpp:1304-1312`). **No fish or crawlers** (`:1668-1690` list only Lorencia, Dungeon, Stadium, Atlans, Hellas, Aida, Tarkan and Crywolf). **No leaves** (`MM/Scenes/MainScene.cpp:78-100`). **No rain.**
4. **Terrain lights** (`AddTerrainLight`, MoveKanturu1stObject, per frame):
   - type 60, **Object61 ×5: red-orange** (0.9, 0.2, 0.1) × a random 0.3-0.6 each frame, radius 3 tiles, hidden (`GM_Kanturu_1st.cpp:70-76`)
   - type 61, **Object62 ×58: amber** (1, 0.6, 0.2) × random 0.3-0.6, radius 3, hidden (`:77-83`). It also throws BITMAP_TRUE_FIRE 1 frame in 3 (`:157-164`). These are **fires**, 34 of them standing by an Object85 torch stand and 15 by an Object67 post. 63 flickering lamps in all.
   - type 70 (Object71, a sine-pulsed warm light r4 plus sparks at bone 6, `:84-90, 174-185`) is **placed 0 times** in World38.

**Terrain**
5. **Water:** slot 5 TileWater01 takes the water flag (`ZzzLodTerrain.cpp:1948-1949`) and slides one sheet in **20 s**, the default (`:3583-3597`; no Kanturu arm).
6. **Grass** grows on layer-1 slot N from `TileGrassN+1` where all four corner alphas are 0 (`ZzzLodTerrain.cpp:2076-2120`). The card is the strip's height × 2 = **1.28 m** tall (`:2090`). Wind is the default rolling wave (`:3303`).
   - **Slot 0 grows TileGrass01.OZT's olive tufts** on 5,340 drawn tiles (2,249 walkable).
   - **Slot 1 grows TileGrass02.OZT's pale blue-white blades** on 1,353 drawn tiles (336 walkable).
   - Slot 2 has no strip.
7. There is no caustics, lava or Water attribute, and no special slot.

**Model sheets that are additive without a BlendMesh.** No Kanturu arm sets `BlendMesh`. What adds is MU's TextureScript: a sheet name with an uppercase `R` in the 1-4 characters after its first underscore is bright (`MM/Render/Sprites/TextureScript.cpp:16-58`), and RenderBody makes that mesh the blend mesh (`MM/Render/Models/ZzzBMD.cpp:2236-2242`).
- Placed meshes on `_R` sheets: Object47 (songk3matereff_R), Object74 (Treeange_R), Object78 (waterfall_R), Object86 (chogatea02_R), Object90 (han_R), Object92/93 (songk3mmater0302_R) and Object96 (songk3crystal_R). The hidden boxes wear angeflo_R.
- **`cho_scarp_01_r` (lowercase r) is not bright.** The test is case-sensitive (`'R'`, `:37`). In the Ruins only the unplaced Object104 wears it, but the Relics place 300 of it (§8).
- So `BLEND_MESH_BY_MAP[37]` stays empty, and the recipes mark these sheets additive, as Tarkan's `tk_chogatea02_r`.

**MoveKanturu1stObject** (`GM_Kanturu_1st.cpp:42-126`):
8. **59, 62, 81, 82, 83, 107, 108: `HiddenMesh = -2`** every frame (`:52-60`). **60, 61** are hidden in their own arms (`:75, :82`). **97** is hidden (`:107-108`). **37** hides itself in RenderVisual (`:146`). Together that is `HIDDEN_BY_MAP[37]`: 11 types, 399 placements, every one an angeflo_R box.
9. **44 (Object45, gears):** `Velocity 0.02` (`:61-63`). **90 (Object91):** `Velocity 0.04` (`:98-100`).
10. **46 (Object47, the glowing wheel ×43):** `Velocity 0.01`, and `BlendMeshLight = sin(WT·0.0015)·0.8 + 1.0`. The _R mesh pulses between 0.2 and 1.8 over about 4.2 s. The wheel sound plays (`:64-69`).
11. **76 (Object77, sunbeams):** Alpha 0.5 (`:91-93`). In the after-character pass, mesh 0 is drawn again RENDER_BRIGHT at BodyLight 0.52, streamed with U = `−(WT % 100000)·0.00001`, one sheet in 100 s (`:348-359`).
12. **77 (Object78, waterfalls ×52):** `BlendMeshTexCoordV = −(WT % 10000)·0.0002`: **0.2 sheet a second downwards** by wall clock (not a per-frame rate), wrapping every 10 s. The waterfall sound plays (`:94-97`).
13. **92 (Object93):** the electric sound (`:101-103`). **98 (Object99):** the plant sound (`:115-117`).
14. **102 (Object103):** `BlendMeshLight = sin(WT·0.001) + 1`. Unplaced.
15. `PlayBuffer(SOUND_KANTURU_1ST_BG_GLOBAL)` runs from every object's move, every frame (`:123`), so the ambient loop is always on.

**RenderKanturu1stObjectVisual** (`GM_Kanturu_1st.cpp:128-306`):
16. **37 (Object38 ×58):** in the first 10 ms of every 1024 ms, a MODEL_BUTTERFLY01 effect, subtype 3 (`:137-148`): **butterflies**, about one a second per box. 17 stand by rubble, 12 by crystals, and 48 are on the plateau. MuMain loads the butterfly from `Data/Object1` (`MapManager.cpp:377-378`).
17. **59 (Object60 ×16):** BITMAP_SMOKE subtype 21, 1 frame in 3 (`:149-156`): **smoke vents** by the factories.
18. **62 (Object63 ×60), 107 (Object108 ×77), 108 (Object109 ×11):** 20 BITMAP_CLOUD a frame (subtypes 20, 2 and 7; grey 0.04, 0.06 and 0.2), **but only `if (HiddenMesh != -2)`**, which MoveKanturu1stObject sets every frame first (`:165-173, 273-292`).
    - **They emit.** The doc had them dead, as in Aida, but Aida turns its own boxes off with HiddenMesh -2 (GMAida.cpp:85) and Kanturu's CreateKanturu1stObject sets nothing, so RenderObjectVisual runs them every frame they are seen (ZzzObject.cpp:3443-3462): 62 rising clouds (SubType 20), 107 hovering ones (SubType 2), 108 darkening ones (SubType 7, minus). Carried 2026-10-22 by game/world/chasm_mist.h.
    - They show where MU meant mist. **58 of Object63's 60 stand over the chasm's void.** Object108 stands 44 on the plateau (pits and rim) and 30 over void.
    - They are a record of intent for void clouds, not MU's frame.
19. **81 (Object82 ×26):** BITMAP_WATERFALL_1 subtype 2, 1 frame in 2. **82 (Object83 ×35):** BITMAP_WATERFALL_3 subtype 4 every frame. **83 (Object84 ×52):** BITMAP_WATERFALL_2 subtype 2, 1 in 3 (`:186-202`).
    - This is **waterfall spray**: 92 of the 113 boxes stand within 2 m of an Object78 fall.
    - These are the same particle arms Aida's spray uses (`game/world/waterfall_spray.h` in MU2_BGFX, anchor kinds 18-20).
20. **85 (Object86, the gate):** mesh 3 drawn RENDER_BRIGHT and again RENDER_CHROME|RENDER_DARK with BITMAP_CHROME (`:203-213`). The gate's glowing panel is chromed.
21. **92 (Object93 ×2):** 1 frame in 30, a BITMAP_JOINT_THUNDER bolt from bone 1 to bone 2 (`:214-226`): **electric arcs** on the tanks.
22. **96 (Object97, the gate veil):** mesh 0 streamed again with V = `−(WT % 20000)·0.00005`, one sheet in 20 s, alpha-test 0 (`:227-236`).
23. **98 (Object99 ×46, carnivorous plants):** 1 frame in 3, BITMAP_TWINTAIL_WATER at bone 1, coloured (0.5, 0.6, 0.1) (`:237-247`): **green spit**.
24. **95 (Object96 ×128, crystals)**, after-character: mesh 0 RENDER_BRIGHT, then RENDER_CHROME7|RENDER_DARK, then **ten blue BITMAP_SPARK+1 sprites** (0.1, 0.1, 0.3) at size 7.5 on bones 0-9 (`:361-381`). That is about 1,280 sprites if none are culled.
25. **105 (Object106, unplaced):** an energy core with thunder joints and a chrome pass (`:248-272, 383-390`). **110 (Object111, unplaced):** a pulsing BITMAP_LIGHT at bone 1 (`:293-302`).
26. **103 (Object104, unplaced):** mesh 1 blended at a sine light (`:314-325`).
27. **Drawn after characters** (`m_bRenderAfterCharacter`, `:327-334`): types 76, 77, 91, 92, 95 and 105, so the beams, falls, tanks and crystals draw over characters.

**Ambient and music**
28. Ambient wavs, all 22 kHz stereo, all loaded at `MapManager.cpp:380-384`:
    - `SOUND_KANTURU_1ST_BG_GLOBAL` = `Sound/w37/kan_ruin_global.wav` (6.6 s), always on (`:123`)
    - `kan_ruin_wheel.wav` (5.5 s) on Object47
    - `kan_ruin_waterfall.wav` (4.0 s) on Object78
    - `kan_ruin_elec.wav` (9.5 s) on Object93
    - `kan_ruin_plant.wav` (4.0 s) on Object99
    - **The folder is `w37`, the map number, not the data folder's 38.** None of these are in `content/sounds`.
29. 40 monster wavs (ber, blade, geno, gigan, ir, kenta, sati, swolf; `MapManager.cpp:385-424`): pass A/C.
30. `MUSIC_KANTURU_1ST` = `data/music/kanturu_1st.mp3` (2,801,478 B) plays the whole time on map 37 (`SceneManager.cpp:1099-1102`; `MM/Core/Globals/_enum.h:192`). House rule: music in safe zones only.
31. S17's `Sound/w37` holds the same 45 file names.

**Other**
32. Town Portal works (`MM/World/MapInfra/PortalMgr.cpp:57`). Map names are at `MapManager.cpp:1691-1702` (Ruins "Kanturu", Relics "Kanturu3", Tower "RefineryTower").
33. There is no Kanturu arm in ZzzLodTerrain for map 37, and none in ZzzCharacter for heights or swimming.

---

### 3. The pipeline route: step 1, bare land

The chain is Tarkan's and Aida's. From MU2_BGFX/, with `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. the manifest first: content/world/kanturu/world.json, "map": 37 (terrain.py is run with folder 38).
#    Its "pipeline" block (pipeline/world_manifest.py; each knob {"value", "note"}):
#      hidden      = [37, 59, 60, 61, 62, 81, 82, 83, 97, 107, 108]
#          # MoveKanturu1stObject, GM_Kanturu_1st.cpp:52-60, 70-83, 107-108; 37 in RenderVisual :146.
#          # 37 butterflies (58), 59 smoke vents (16), 60/61 red and amber fire lights (5/58),
#          # 62/107/108 mist boxes (60/77/11, dead in MuMain :165-173, 273-292),
#          # 81/82/83 waterfall spray (26/35/52), 97 the bubble box (1). 399 placements, all angeflo_R boxes.
#      grass       = ["TileGrass01", "TileGrass02"]
#          # MU's rule (ZzzLodTerrain.cpp:2076-2120): olive tufts on slot 0 (5,340 drawn tiles),
#          # pale blue-white blades on slot 1 (1,353). Decision 3 may drop slot 1.
#      void        = {"start": 0.5, "depth": 6.0, "blend": 1.5}
#          # Devias's way, Aida's shape, deeper: Object06/76's 394 cliff faces hang 5-14 m into the
#          # chasm (Aida's walls 4.7 m). `blend` for the bare ring lips where no cliff stands.
#          # The plateau needs no blend: MU sloped it to 0 m and painted it black (§5).
#      void_fill   = [[20, 218], [70, 187]]
#          # from the safe hall (spawn gate 138) AND the plateau (warp gate 334). From the spawn
#          # alone the walker would void the whole plateau (5,514 tiles). With both: exactly the
#          # 8 stray open tiles on rows 251-255 (checked). Ours, marked.
#      border_fill : decision 2 (as Aida: walls = [5, 75], the ring's outer edge has no cliffs)
#      blend_keep_near = {"walls": [5, 75], "reach": 2.5}   # if void.blend is kept, as Aida
#      flat_over_void : true    # waterfalls (21), pipes Object72 (17), scaffold Object110, cliffs over void
#      airs_from   = "lorencia" # MODEL_BUTTERFLY01 from Data/Object1 (MapManager.cpp:377-378)
#      blend_mesh  : none. No Kanturu arm sets BlendMesh; the glows are _R sheets (TextureScript.cpp:16-58).
#      perches / OPERABLE : none (CreateKanturu1stObject is empty, GM_Kanturu_1st.cpp:34-40).
#      water_flow  : none. MU slides every pond one way, a sheet in 20 s.
#      crawls_from : none (no crawlers in Kanturu, GOBoid.cpp:1668-1690).
#      light_depth / light_chroma / figure_light / sky / open / floor / lava_spill: none to start
#          (light_chroma is a candidate: the busiest coloured TerrainLight yet, §1.1).
#      anchor_kinds: spray Object82/83/84 -> 18/19/20 (Aida's waterfall_spray kinds; same particle arms),
#          Object62 fires -> small_fires (21) if its TRUE_FIRE reads close enough, Object60 smoke -> new,
#          Object99 spit -> new.
#      gates : OM VersionSeasonSix/Gates.cs:276-280, 537-540 (pass A): 138 spawn (19,217-21,219),
#          126 from Tarkan (17,219-21,220), enter 127 to Tarkan (17,220-19,222), 141 Ruins2 (205,36-208,41),
#          334 Ruins3 (66,183-74,191, on the plateau), enter 129 to Relics (89,89-89,92), 132 from Relics
#          (85,89-86,92). Tarkan's side: enter 125 at (6,199-6,201).
python3 pipeline/terrain.py $D/World38 kanturu content/world 38
#    expect: 0.00-3.83 m, 14 slots (0-13), 27.4% walkable (27.4% after VOID_FILL: 8 tiles), 2.7% safe,
#            3645 placed / 100 kinds, no missing line.
for n in TileGrass01 TileGrass02 TileGround01 TileGround02 TileWater01 TileRock01 TileRock02 \
         TileRock03 TileRock05 TileRock06 TileRock07; do
  python3 pipeline/decode_texture.py $D/World38/$n.OZJ content/textures/kr_$(echo $n | tr A-Z a-z).png; done
python3 pipeline/decode_texture.py $D/World38/TileGrass01.OZT content/effects/grass/kanturu_TileGrass01.png
python3 pipeline/decode_texture.py $D/World38/TileGrass02.OZT content/effects/grass/kanturu_TileGrass02.png
#    content/world/kanturu/ground.json on Aida's shape, sheets -> kr_*, materials below
./pipeline/content.sh --world kanturu && ./pipeline/sync.sh --world kanturu --only-world && ./pipeline/sync.sh
./pipeline/cook.py --world kanturu --only ground && ./pipeline/cook.py --world kanturu --only tables
#    then index.py (house rule), and every world's tables after new items. `plan` before any cook.
```

**Sheet prefix: `kr_`** (Ruins). `kl_` and `kt_` are free for the Relics and the Tower. Nothing in `content/textures` (1,197 stems) uses any of the three, and no world folder is called `kanturu`.

Name collisions:
- **With sheets already in `content/textures`:** tilegrass01, tilegrass02, tileground01, tileground02, tileground03, tilerock01, tilerock02, tilewater01 and tilewood01 (exact, unprefixed names), and **angeflo_r**.
- **With Tarkan's (`tk_`):** cho_gate_a, cho_gate_a_01, cho_gate_a_03, cho_gate_b, cho_gate_c and chogatea02_R (D/Object9). Object86 is Tarkan's Object86 gate (436 against 448 tris). **Consider reusing `content/world/tarkan/Object86-88` and their `tk_` sheets** rather than a second copy.
- **With future ports:** Object39 and Object40 share the refinery and crystal sheets (cho_scarp_*, han_dd1/handd2_R, han_x2, Mcanwall*, mingwall*, songk3*_R, song_k202w1, Tileeffec01_R, han_kcliff2). D/Object1 shares the `cho_obj*` and `song_k0*` sets, and Aida's Object34 shares Treeange_R, sunlight2 and Pool001.
- **Generic stems to keep prefixed:** `stone_01`, `wall_01`, `drop01`, `han_R`, `iplant`, `kan_tree`, `waterfall_R`, `sunlight2`, `Pool001`.
- **.OZJ/.OZT twins under one stem:** TileGrass01 and TileGrass02 (World38); `cho_tree_a` (Object38, the only twin). **Object64 wears `cho_tree_a.tga`** (the alpha leaves) and **Object65 wears `cho_tree_a.jpg`** (bark). The decoder must name each twin apart.

**ground.json `sheet_materials` (proposed; read off the art):**

| slot | sheet | material | note |
|---|---|---|---|
| 0 | TileGrass01 | grass | mossy olive ground, 20.1% of drawn |
| 1 | TileGrass02 | earth | dark earth with dead grass (an overlay sheet too: 2,115) |
| 2 | TileGround01 | grass | bright moss (mostly overlays, 1,126) |
| 3 | TileGround02 | cobble | moss over buried brick |
| 4 | TileGround03 | — | **white placeholder: map to `kr_tilegrass02.png`** (ours, marked) |
| 5 | TileWater01 | water | pools under the falls |
| 6 | TileWood01 | — | **white placeholder: map to `kr_tilerock03.png`** (28 walkable tiles; ours, marked) |
| 7 | TileRock01 | grass | the same painting as TileGrass01 |
| 8 | TileRock02 | earth | red-brown cracked earth |
| 9 | TileRock03 | earth | dirt and pebbles |
| 10 | TileRock04 | — | **white placeholder: map to `kr_tilerock07.png`** (1 tile + 4 overlays) |
| 11 | TileRock05 | rock | pebbles and stones of the pits |
| 12 | TileRock06 | flagstone | mossy paving |
| 13 | TileRock07 | earth | the plateau's packed earth |

Each takes its `_over` twin, as Tarkan's and Aida's do.

---

### 4. What differs from the worlds already built

1. **The chasm is deeper and wider.** 44.8% NoGround. Object06/76 hang 5-14 m, so `void.depth` must be well past Aida's 4.5.
2. **Two kinds of lip.**
   - The ring's inner lips carry cliffs (Devias's way).
   - The ring's outer lips at the map's edge are bare NoMove ground (Aida's `border_fill` question).
   - The plateau's rim slopes to 0 m and is painted black. A void blend there would darken an already black slope, and a `start` measured from the 2.25 m void height would put the plateau's foot (0 m) under the abyss's start. Check where the abyss is measured from before the first shot.
3. **A warp-only region.** VOID_FILL needs two seeds (§3). The walker and the realm must accept a region no walk reaches.
4. **Hidden types survive as markers and anchors:**
   - butterflies 37 (58)
   - smoke 59 (16)
   - fires 60/61 (63, with lights)
   - spray 81-83 (113)
   - bubble 97 (1)
   - the mist boxes 62/107/108 (148): MU's dead intent, kept for void clouds
5. **Lights: 63 flickering fire lamps** (radius 3 tiles, larger than Aida's 2), plus 1,280 crystal sparks and the pulsing wheels, the Treeange streaks and the firefly planes. Noria's "reduce sun so we see the light emitters" applies.
6. **Additive or streaming kinds:**
   - Object47 (the pulsing _R mesh)
   - Object74, Object86 and Object90 (_R)
   - Object78 (V slide 0.2 sheet/s)
   - Object77 (half alpha, plus a bright pass streamed in U at 1/100 s)
   - Object97 (streamed, 1/20 s)
   - Object96 (bright, chrome7 and sparks)
   - Object86 mesh 3 chrome
7. **Rigged placements 897**, with Object45/47/91 at Velocity 0.02/0.01/0.04.
8. **Ground height:** the walkable floor is about 2.25 m. Aida's was 1.95 and Tarkan's similar.
9. **Do not ground** Object74 streaks (+2.5 m), Object77 beams (+3.9 m), Object90 firefly planes (+0.9 m), Object62 fire boxes (+2.1 m), Object110 (−7.6 m) or the cliffs. The tuft kinds Object68/69 sit at −0.6/−0.1 m and are fine.
10. **Recipe priority** (scenery first, structure first):
    - cliffs Object06/76 (394)
    - rubble Object66 (232)
    - tufts Object68/69 (423)
    - crystals Object96 (128)
    - earth banks Object80/81 (206)
    - walls Object55/22/23 (221)
    - pipes Object72 (91)
    - stones Object07 (84)
    - sprouts Object100 (80)
    - palms Object33 (67)
    - logs Object37, trees Object28
    - the factories and houses Object01-05/10/14/59
    - waterfalls Object78 and their spray
    - the gate (reuse Tarkan's)
    - bridges, wheels, plants, torches

---

### 5. The void

MU draws a black cut and black fog. Cliffs hang into the chasm on the ring side, and the plateau slopes into black paint. Options:
- **A. Devias's and Aida's way** (recommended to start): MU's cut and the default abyss, `void = {start 0.5, depth 6.0, blend 1.5}`, with `blend_keep_near` on the cliffs. The cliffs go dark down their 14 m length.
- **B. A plus mist in the chasm** (ours, MU's intent): Tarkan's `shallow_` void clouds in a cool grey, only where a cloud lies wholly over void. MU placed 58 grey mist boxes (Object63) over the chasm and 77 more (Object108) over the plateau's pits and rim, and never let them emit (§2.18). With 52 waterfalls falling into it, the chasm would read best with mist under the falls.
- **C. The outer border:** whether to spread ground over the outer void (Aida's `border_fill`) so the map's edge does not read as holes at 16:9. The ring's outer edge has no cliffs.

---

### 6. A look direction

**MU's paint, measured** (`S/look.py`, `S/look.log`; walkable tiles; sheet means blended by the alpha layer, times TerrainLight; the white placeholders included at their weight):

| world | walk light | walk sheet mean | sheet × light | lum p50 | hue / sat |
|---|---|---|---|---:|---|
| Lorencia | (160, 160, 161) | (92, 81, 32) | (58, 51, 20) | 46 | 49° / 0.65 |
| Devias | (140, 179, 204) | (203, 214, 217) | (110, 149, 174) | 148 | 203° / 0.36 |
| Noria | (181, 181, 181) | (82, 74, 36) | (58, 52, 25) | 49 | 50° / 0.56 |
| Tarkan | (138, 135, 135) | (169, 149, 113) | (91, 79, 60) | 77 | 36° / 0.34 |
| Aida | (138, 137, 139) | (64, 59, 40) | (35, 32, 22) | 29 | 47° / 0.37 |
| **Kanturu Ruins** | **(127, 132, 122)** | **(69, 58, 37)** | **(34, 30, 17)** | **29** | **43° / 0.49** |
| Kanturu Relics | (170, 170, 167) | (61, 55, 51) | (41, 36, 33) | 37 | 26° / 0.18 |
| Refinery Tower | (120, 132, 135) | (34, 36, 35) | (16, 19, 19) | 19 | 185° / 0.14 |

- The Ruins' ground is **Aida's darkness with more brown and more saturation**: an earth-brown, a little warmer than Aida's olive. The light leans faintly green (G +5).
- Its colour is in the baked lamp pools (gold), the teal wash on the east arm, the pink crystals, the blue sparks and the green firefly motes.
- MU's grass strips are a dark olive tuft (58, 58, 29) and a pale cold blade (219, 231, 233).

**It should read as overgrown industrial ruins at dusk-day:** dark earth and moss, broken stone and timber, iron machinery still turning, crystals and fire glowing in the shade, and waterfalls falling into a black chasm. It is not a night map, by the house sun at 52°, but it is darker than Noria and closer to Aida.

**What the sheet must carry:**
- **The sun at 52°, softer than Noria's**, a pale warm-gold at strength ~3.4, so the 63 fire lamps and the crystals read.
- **Fill from a grey-teal sky**, picking up the east arm's teal wash. The shade stays brown-teal, not black.
- **Exposure up** to carry the dark albedo (lum 29, as Aida), about 1.45. Expect Aida's fight between lifting the floor and washing the blacks.
- **Haze:** a cool grey chasm mist, faint (0.003). MU placed the boxes and never drew them.
- **Grass:** MU grows olive tufts on slot 0 and pale wisps on slot 1. The house rule says grass must read lush. Slot 0 is only 20% of the floor (Aida's was 42%), so this is not a meadow world. Keep it dense where it grows.
- **Grade:** warmth in the light tint, the shade a touch teal; saturation about 1.0 (the sheets are already saturated for their darkness).
- **Water:** MU slides the pools a sheet in 20 s. Noria's precedent is to hold them still (`water_flow 0`). Under 52 waterfalls a slow flow may read right here: decision 4.

**Proposed starting `content/sheets/worlds/kanturu.json`** (shape from aida.json; every value is invention for the first shots):

```json
{
  "note": "Kanturu Ruins' light, laid over lighting.json while a character stands in map 37. MU draws it on its default black clear and fog (SceneManager.cpp:401-402), 44.8% of the map NoGround and undrawn, the ground lit by a near-neutral, faintly green TerrainLight (walkable mean 127, 132, 122) speckled with baked gold, teal and pink lamp pools, over sheets as dark as Aida's (walkable sheet x light luminance 29) but browner (hue 43, saturation 0.49), with 63 flickering fire lights (GM_Kanturu_1st.cpp:70-83), 128 glowing crystals, pulsing wheels and 52 waterfalls into a black chasm. So: overgrown industrial ruins by day, the house 52-degree sun softer than Noria's so the fires and crystals read, a grey-teal fill, exposure up for the dark paint, a faint cool chasm mist (MU placed mist boxes over the chasm and never let them emit, :165-173), the grade warm in the light and a touch teal in the shade. Invention on MU's look; first cut 2026-10-21 from the Kanturu Part B, unshot.",
  "elevation": 52.0,
  "sun_colour": [1.0, 0.9, 0.7],
  "sun_strength": 3.4,
  "sky_colour": [0.28, 0.38, 0.38],
  "horizon_paleness": 0.7,
  "ground_colour": [0.1, 0.09, 0.06],
  "ambient_strength": 0.65,
  "exposure": 1.45,
  "dust_colour": [0.55, 0.6, 0.6],
  "dust_density": 0.003,
  "lamp_strength": 3.14159,
  "glow_strength": 1.5,
  "bloom_threshold": 0.95,
  "bloom_strength": 0.35,
  "contrast": 0.6,
  "saturation": 1.0,
  "split": 0.45,
  "tint_low": [0.9, 1.0, 1.0],
  "tint_high": [1.1, 1.02, 0.86],
  "grass_colour": 0.4,
  "grass_root_colour": [0.14, 0.22, 0.1],
  "grass_tip_colour": [0.5, 0.62, 0.3],
  "grass_dry": 0.3,
  "grass_vary": 1.2,
  "grass_meadow": 0.0,
  "grass_height": 0.45,
  "grass_density": 0.7,
  "water_flow": 0.0,
  "water_sheen": 0.5
}
```

Expect the first-shot fights to be:
1. the floor too dark or the blacks washed
2. the 14 m cliffs and the black chasm, and whether the plateau's black-painted slope reads as a drop or a hole
3. whether the fires and crystals read at day
4. the busy colour specks of the TerrainLight

---

### 7. Traps

1. **Map 37, folder 38.** Every table is keyed 37, and terrain.py's fourth argument is 38. The **sound folder is `w37`** (the map number), while the World and Object folders are 38. The Relics are map 38, folder 39, sounds `w38`. The Tower is map 39, folder 40, sounds `w39`.
2. **VOID_FILL from the spawn alone voids the plateau** (5,514 walkable tiles reached only by warp 334). Seed both (20,218) and (70,187).
3. **Three white placeholder sheets** (TileGround03, TileRock04, TileWood01, one file) on 72 drawn tiles and 7 overlays. Remap them, or white patches show.
4. **TileRock01 is TileGrass01's painting.** Materials go by the art (§3).
5. **The `_R` sheets are the glow**, not a BlendMesh. `cho_scarp_01_r` (lowercase) is **not** bright in MU. That matters in the Relics (300 placements).
6. **The mist boxes 62/107/108 emit in MuMain** (corrected 2026-10-22; game/world/chasm_mist.h carries them).
7. **Wall-clock rates:** the waterfall's V slide is `WT·0.0002` per ms (0.2 sheet/s), not a per-frame rate. Do not ×25.
8. **`cho_tree_a` is a twin:** Object64 takes the .tga leaves and Object65 the .jpg bark.
9. **Object86-88 duplicate Tarkan's gate.** Reuse rather than re-export, or prefix apart from `tk_`.
10. **Don't ground** the streaks, beams, firefly planes, fire boxes or the sunk scaffold (§4.9). **Do add `kanturu` to `flat_over_void`.**
11. **S17's map files are a new `MAP\x01` container** terrain.py cannot read. Use MuMain's.
12. **The Ruins' object arms also run in the GM area (WD_40)**, which explains arms for types the Ruins never place (70, 102, 103, 105, 110). Do not build those.
13. **The safe hall is 2.7% of the map** with 1,099 NoMove safe tiles. The hall's walkable floor is only 639 tiles (x 16-80, y 174-222).

---

### 8. The Relics and the Tower (inventory and look)

#### 8.1 Kanturu Relics (map 38, `D/World39` + `D/Object39`)

**World39 (24 files):**
- EncTerrain39 .att/.map/.obj (60,424 B; **2,014 placements of 70 kinds**), plain Terrain.att/.map copies, and `TerrainServer.att`
- TerrainHeight.OZB; TerrainLight.OZJ (20 KB)
- 11 tile sheets (TileGrass01-02, TileGround01-03, TileRock01-04, TileWater01, TileWood01)
  - TileGround03 and TileRock01 are one 128² stone sheet; TileWood01 is the Ruins' TileRock03 dirt
  - **No grass .OZT**, so no grass grows
- leaf01/leaf02 (unused) and the minimap

**Object39** has 131 files.

**Grid** (`stats39.log`):
- Flat at **1.70 m** everywhere.
- 6.9% walkable (4,529); 42.0% NoGround.
- One region of 4,454 tiles (x 69-202, y 66-186): a **maze of corridors and halls** in a 1.7 m floor, with a ring of drawn NoMove floor round it and black beyond.
- Safe box x 52-93, y 98-114 (183 walkable, centre 78,106): OM's spawn gate 137 (71-82, 102-109) and the Ruins' arrival 139/130 (`Gates.cs:283-286`).
- Floor: TileGrass01 (a grey-brown stone, 87.5% of drawn) and TileRock04.

**Light:** neutral, bright white on the corridors (walk mean 170), dark elsewhere, with no colour.

**Look (MU's paint):** walkable sheet × light (41, 36, 33), lum 37, a desaturated grey-brown (26° / 0.18).

**Objects** (`objinfo39.log`, `sheet_objects39.png`):
- 300 refinery scaffold poles hung into the void: Object01-03 `cho_scarp_*` (539 + 125 + 175 placed, sunk 6-9.5 m). Their `cho_scarp_01_r` panels are **not** additive in MU.
- 9 chairs Object04, which MU makes **operable** (`CreateOperate`, `GM_Kanturu_2nd.cpp:34-49`), so `OPERABLE_BY_MAP[38]` gets a row.
- Glowing crystal pylons Object11 (`songk21crystal02_R`).
- Magma-veined walls Object13-17 (`song_k21*`).
- **Incubator tanks** Object32/34/36/37 (`songk3mmater0302_R`); Object34 is a 29 m column hanging into the void.
- Big gearwork Object10 (`Velocity 0.04`, `BlendMeshLight` clamped sine, `:221-235`) and Object45 (`Velocity 0.02`).
- Machines Object28 (streamed, `GM_Kanturu_2nd.cpp:553-600`).
- 107 flat `Tileeffec01_R` floor-glyph decals (Object39, `BlendMeshLight` sine, `:236-240`), on walkable ground.
- 166 hidden emitter boxes (types 45-56 and 65, `:252-268`):
  - clouds, most of them **gated dead** as in the Ruins (`:387-440`)
  - live: type 48 grey clouds, 50 and 51 blue and red void glows, 52 waterfall specks, 53 water, 54/56/65 thunder joints, 55 Fenrir thunder (`:405-503`)

**11 placed types (26 placements) have no model in Object39**, in either client: Object22, 23, 26, 29, 40, 41, 43, 44, 52, 70, 85. MU skips them silently.

**Sound:** `w38/kan_relic_global.wav` (12.0 s) always; gear on type 9; incubator on 31/35-37 (`GM_Kanturu_2nd.cpp:194-210`). **Music:** `kanturu_2nd.mp3` (`:1350-1360`).

**The door:** MU has no door object. The way in is the NPC Gateway Machine and its UI (`MM/UI/NewUI/Events/NewUIKanturuEvent.cpp`, `Npc/khs_kan2gate*.jpg`, `MapManager.cpp:435-437`), plus the trap cannon `g_TrapCanon` (`:427`).

**In a paragraph:** an indoor-reading refinery maze lit like a lab. Pale stone corridors are lit bright down their middle and fall to black at their edges. Iron scaffolds and 29 m tank columns drop into the void. Blue-glowing tanks, crystal pylons, magma-veined walls and turning gearwork fill the halls.

#### 8.2 Refinery Tower (map 39, `D/World40` + `D/Object40`)

**World40 (20 files):**
- EncTerrain40 and **EncTerrain401** .att/.map/.obj: the after-victory set. MuMain swaps only the **.att** when the server reports the tower revitalised (`MapManager.cpp:1278-1285`; `GM_Kanturu_3rd.cpp:59-90`). The .map is identical; 401's .obj drops types 73/74 and adds 3 of type 47.
- EncTerrainTest copies; a stray `EncTerrain39.obj` (byte-identical to World39's).
- TerrainServer.att, TerrainHeight.OZB, TerrainLight.OZJ (9 KB).
- **Only three tile sheets:** TileGrass01-02 and TileGround01, all near-black (30-36). The other slots MuMain asks for are missing (slot 11 TileRock05 is worn by 1,486 drawn tiles, magenta in `prev40_*.png`).
- **`AlphaTileGround02.OZT`**, loaded as slot 3 in the Tower alone (`MapManager.cpp:1381-1384`) and drawn alpha-tested (`ZzzLodTerrain.cpp:1590-1598`). It is **fully transparent** (coverage 0).
- The Tower also skips its terrain during the Maya scene (`MainScene.cpp:453-458`) and draws `RenderTerrain_After` (`ZzzLodTerrain.cpp:3635-3641`).

**Grid:** flat 1.70 m; 3.4% walkable; 36.2% NoGround; no safe tiles. Two separated regions:
- **Maya's arena**, an octagon with a corridor north (1,202 tiles, x 174-217, y 54-83)
- **Nightmare's hall** (1,033 tiles, x 57-98, y 85-144)
- After victory (`kanturu40s`): 2,617 walkable, and the hall opens north to the crystal (1,390 tiles, x 57-98, y 85-194)
- OM gates 133-135 (`Gates.cs:271-273`)

**Light:** a flat grey (140) away from the play spaces, with teal, green and violet pools in the hall (walk mean 120, 132, 135).

**Look:** sheet × light (16, 19, 19), lum **19**, cold teal (185° / 0.14). This is the darkest ground of any world here.

**Objects** (`objinfo40.log`, `sheet_objects40.png`):
- **Maya:** Object01, 4,390 tris, 100 bones, lowered 20 m at create, risen in the cinematic (`GM_Kanturu_3rd.cpp:92-100, 205-247`). Her hands are Object58/59.
- Chromed, bright-streamed tower machinery Object20/21 (`han_maya*`).
- Streamed floor plates Object16-18 (`sok31bwa2_R`).
- 94 void spikes Object34 (`sok34wao03_R`, hanging up to 22 m).
- **The crystal**, Object41-43 (`Mcry_R`), at x 75-82, y 181-186.
- **The barrier** Object74 (`hanmcrystal2_R`, drawn streamed only until victory, `:625-631`) and its 12 thunder boxes Object75 (type 74, hidden after victory, `:299-307`).
- An NPC tank Object71 (`kannpc*`).
- About 400 `test12_H` emitter boxes (`_H` = HiddenMesh by TextureScript): cloud, thunder, smoke, true-blue particles, and white terrain lights on types 45/50/54 (`:118-196, 254-392`).

**Sound:** `w39/kan_boss_global.wav` (8.0 s) always; crystal/field/gear/incubator on types 40-42, 73, 71 and 25 (`MapManager.cpp:505-510`; `GM_Kanturu_3rd.cpp:132-185`). **Music:** KanturuTower, KanturuMayaBattle and KanturuNightmareBattle by event state (`:1744-1777`).

**In a paragraph:** a black, cold boss instance and not a field map. Two lit islands of near-black floor sit in a grey-black void: Maya's octagon, where a 20 m Maya rises out of the floor, and Nightmare's hall, behind a crackling crystal barrier that falls when the event is won and opens the way to the refinery crystal. Teal and violet glows, streamed bright floor plates and spikes hanging into the dark carry all the colour. It is driven by the server's event state, so it belongs with the event work, after the Ruins and the Relics.

---

### 9. Decisions for the user (look and data only)

1. **The void:** A (Aida's cut and abyss, deeper at 6 m), or B (A plus a cool mist in the chasm under the falls, which MU placed over the chasm and never drew).
2. **The outer border:** leave MU's bare black edge, or fill it with ground as Aida's `border_fill` did.
3. **Grass:** MU's two strips (olive tufts on slot 0, pale blue-white wisps on slot 1), or slot 0 only. The wisps are an unusual look (frosted or ghostly) and worth one shot first.
4. **Ponds:** held still (Noria), or MU's 20 s slide, under 52 waterfalls.
5. **The white placeholder slots:** remap them (recommended), or leave MU's white.
6. **The gate to Tarkan:** reuse Tarkan's Object86-88 or port Kanturu's own (12 fewer tris, the same sheets).
7. **The TerrainLight's colour specks:** keep MU's baked gold, teal and pink pools as they are, or soften them (a `light_chroma` row).
8. **Music:** `kanturu_1st.mp3` in the safe hall only (house rule), or not at all, with `kan_ruin_global.wav` as the bed.
9. **The plateau:** warp-only as MU and OM have it (Ruins3, 15,000 zen), or a walkable bridge of ours. Pass A should weigh in.

---

## Part C: the engine side and the build steps (Kanturu)

Written 2026-10-21. Research only: no tracked file was edited, nothing was cooked, mu.db was not touched, no window was opened, nothing was stashed. One scratch dry run of `pipeline/terrain.py` was written into this folder (`world/kanturu/`). It writes only to its out dir (`pipeline/terrain.py:617-618`). One analysis file, `attributes.png.d.npy`, was briefly written into `content/world/tarkan/` and deleted at once. `git status content/world/tarkan` is clean.

Paths are relative to `MU2_BGFX/` unless marked. Abbreviations:
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data`

Scratch, beside this file:
- `world/kanturu/`: the dry import of World38;
- `grid.py`, `spawns.py`: region, gate-box and walk-distance checks;
- `ruins_spawns.txt`: OM's 210 Ruins spawns.

**What changed since Aida's Part C.** Aida's doc (`docs/worlds/aida-port.md` Part C) predates the world-manifest sprint (D2/D3a, 2026-10-11 onwards). Most of Aida's checklist rows are now data in `content/world/<w>/world.json`:
- the realm's tables are generated from those manifests;
- the minimap and arrival names come from the manifest's `name`;
- music, air and boids come from `show`;
- the hidden, void, grass and anchor knobs come from `pipeline`.

So Kanturu's registration is mostly one new manifest. What stays in C++ is short (§2.3).

---

### 0. The short version

1. **Three maps, three folders.** Each is keyed by MU's server number in `world.json` `map`. terrain.py is run with the client folder number, and reads its knobs at `number - 1` (`pipeline/terrain.py:594, 600, 646-776`).

   | map | name | client folder | suggested `world` folder |
   |---:|---|---|---|
   | 37 | Kanturu Ruins | World38/Object38 | `kanturu` |
   | 38 | Relics | World39/Object39 | `kanturu_relics` |
   | 39 | Refinery Tower | World40/Object40 | `kanturu_tower` |

   World40 also ships `EncTerrain401.*`, the tower's second (opened) terrain.

2. **The Ruins' dry import** (scratch, no knobs set):
   - height 0.00-3.83 m;
   - 14 tile slots (TileRock07 covers 40.5%);
   - 27.4% walkable, 2.7% safe;
   - light mean (120, 123, 118);
   - **3 645 placements of 100 kinds, none missing from Object38**.

3. **The walkable ground is two regions**, not one:
   - **12 432 tiles**: every base breed (350-357), the safe town (639 walkable safe tiles, x 16-80, y 174-222), the Tarkan door and the Relics door;
   - **5 514 tiles**: only OM's Season 4 "Warrior" breeds (553-556, 59 spawns) and S6's late warp 334 "KanturuRuins3". No gate walks into it.

   So the base port is region one. Region two waits for a warp row (or stays empty).

4. **Its door is Tarkan's sealed ruin.** Object86-88 already stand at Tarkan (4.5, 199.6). On our cooked Tarkan grid the door boxes are open:
   - enter 125 at 6,199-6,201: 3 of 3 open;
   - exit 128 at 7,199-7,201: 3 of 3 open;
   - **274 steps from Tarkan's town** (195,65).

   Tarkan's manifest has **no 125/128 rows today** (`content/world/tarkan/world.json` `gates`), so nothing fires there. The sim can already seal a gate (`src/sim/gates.h:46`, `sealed`), which says "<place> is sealed. Its door will not open." (`src/game/play.cpp:1086-1091`).

5. **Ancient runes need levels of 141 or more.** OM's Ruins breeds are levels 80-105 (base) and 123-129 (Warriors). `kAncientLevel` is 141 (`src/sim/items.h:990-994`). **At OM's numbers no Kanturu breed drops an Ancient.** They must be raised past Aida's top breed, the Bloody Witch Queen at 140, as Aida was raised past Icarus (aida-port "Raised past Icarus").

6. **Nothing needs a protocol bump for step 1.** The rules print (`src/net/rules_print.cmake`) folds every `src/sim/*.inc`. A new map row or gate row therefore refuses a client and server built from different sources by itself. Client and server must still deploy together.
   - `net::kVersion` (45, `src/net/wire.h:164`) and `kKeptLayout` (10, `:174`) move only if the bytes change: a quest count change, or a new Kept field.
   - A travel row did bump the protocol for Aida (wire.h:121-122, protocol 35), as a rules change. With the print automatic, check with `mu ship`'s release check (memory: release checks the server protocol) rather than bumping by reflex.

---

### 1. The record the engine has to carry

**Gates** (`OM/VersionSeasonSix/Gates.cs`). Box tiles were checked on the scratch Ruins grid and on our cooked Tarkan grid.

| gate | map | box | role | level (OM) | line | open |
|---:|---|---|---|---:|---|---|
| 125 | Tarkan (8) | 6,199-6,201 | enter, to 126 | 150 | `:537` | 3/3 |
| 126 | Ruins (37) | 17,219-21,220 | arrival from Tarkan | -- | `:278` | 10/10, safe |
| 127 | Ruins | 17,220-19,222 | enter, to 128 | 130 | `:538` | 9/9, safe |
| 128 | Tarkan | 7,199-7,201 | arrival from Ruins | -- | `:201` | 3/3 |
| 138 | Ruins | 19,217-21,219 | **spawn gate** (death, Town Portal, warp "KanturuRuins1") | -- | `:276` | 9/9, safe |
| 141 | Ruins | 205,36-208,41 | warp "KanturuRuins2" only | -- | `:277` | 24/24, 329 steps |
| 334 | Ruins | 66,183-74,191 | warp "KanturuRuins3" (region two) | -- | `:278` | 51/81 |
| 129 | Ruins | 89,89-89,92 | enter, to Relics 130 | 220 | `:539` | 4/4, **599 steps** |
| 132 | Ruins | 85,89-86,92 | arrival from Relics | -- | `:280` | 8/8 |
| 130 / 131 / 137 / 139 / 136 | Relics (38) | see `:282-287`, `:540` | arrival, way back, spawn, warp, tower | -- | | later |
| 133-135 | Tower (39) | `:270-273` | event map | -- | | later |

Warp list (`Gates.cs:72-74, 82`):
- "KanturuRuins1": 9 000 zen at 160, to gate 138;
- "KanturuRuins2": 9 000 zen at 160, to 141;
- "KanturuRelics": 12 000 zen at 230, to 139;
- "KanturuRuins3": 15 000 zen at 160, to 334.

**Trap: gate 126's box overlaps gate 127's.** On row 220, x 17-19 lie in both. Our gate fires only on a step into a box (`Realm::throughGate`, `src/sim/realm_move.cpp:234-260`). So a landing inside it is quiet, but the first step south bounces him back to Tarkan. Trim exit 126 to `17,219-21,219`, or move it east (ours, marked).

**Monsters** (`OM/VersionSeasonSix/Maps/KanturuRuins.cs`, levels at `:286-638`). All 210 spawns stand on open tiles. "Steps" is the walking distance from the spawn gate's middle, 20,218.

| # | breed | OM level | spawns | steps | region |
|---:|---|---:|---:|---|---|
| 351 | Splinter Wolf | 80 | 22 | 30-169 | 1 |
| 352 | Iron Rider | 82 | 21 | 152-253 | 1 |
| 353 | Satyros | 85 | 25 | 185-331 | 1 |
| 354 | Blade Hunter | 88 | 22 | 319-419 | 1 |
| 355 | Kentauros | 93 | 13 | 359-474 | 1 |
| 356 | Gigantis | 98 | 9 | 479-505 | 1 |
| 350 | Berserker | 100 | 28 | 376-576 | 1 |
| 357 | Genocider | 105 | 11 | 507-573 | 1 |
| 553-556 | the four Warriors | 123-129 | 59 | -- | **2** |

The map reads as a ladder: the wolves by the town, the Genocider at the far end. Every breed number is above 255, as Aida's 304-309 were, which is proven safe now.

**What MuMain does in the Ruins** (`MM/World/GameMaps/GM_Kanturu_1st.cpp`, 1 928 lines). From `MoveKanturu1stObject` `:42-125` and `RenderKanturu1stObjectVisual` `:128-`:

- **Hidden types:**
  - 59, 62, 81, 82, 83, 107, 108 (`:52-59`);
  - 60 and 61, which also add terrain lights, orange and amber (`:70-83`);
  - 97, the bubble emitter (`:107-114`);
  - 37, the butterfly emitter (`:137-146`).

  In the dry import these are **399 placements**: 37 x58, 59 x16, 60 x5, 61 x58, 62 x60, 81 x26, 82 x35, 83 x52, 97 x1, 107 x77, 108 x11.
- **Moving and lit types:**
  - 44, 46, 70 and 90 play at their own clip speeds; 46 also pulses its BlendMeshLight;
  - 70 pulses a terrain light;
  - 76 and 96 are drawn at alpha 0.5;
  - 77 is a V-scrolling waterfall;
  - 102 pulses its light.
- **Point sounds** play from objects: waterfall (77), elec (92), wheel (46), plant (98). There is also a global bed, `kan_ruin_global.wav`.
- **Sounds on disk:** `D/Sound/w37/` has 45 wavs (MapManager.cpp:376-424). These trip the Aida trap again:
  - MuMain loads `kenta_skill-01.wav` as attack 2;
  - no `gigan_idle-02` exists.
- **Butterflies:** MuMain loads `MODEL_BUTTERFLY01` from Object1 for the Ruins (`MapManager.cpp:377-378`), as for Aida.
- **Music:**
  - `MUSIC_KANTURU_1ST` = `data/music/kanturu_1st.mp3` (`MM/Core/Globals/_enum.h:192`; played `SceneManager.cpp:1099-1102`);
  - Relics: `kanturu_2nd.mp3`;
  - the tower: `KanturuTower.mp3`, `KanturuMayaBattle.mp3`, `KanturuNightmareBattle.mp3`.

  All five are in `D/Music` and in `third_party/MuClient_S17_v1.19.74/Data/Music`, but not yet in `out/assets/music/` (where `Aida.mp3` sits; gitignored).

---

### 2. Every place a new world is registered (verified at HEAD 264eecb9)

#### 2.1 Content (the manifest carries most of it)

| # | where | Kanturu needs |
|---|---|---|
| C1 | **`content/world/kanturu/world.json`**, new | The manifest, on Aida's shape: `world`, `name` "Kanturu", **`map: 37`**, `note`, then the blocks below. **It must exist before the terrain import**, because terrain.py reads its `pipeline` knobs through `world_manifest.by_map` (`pipeline/terrain.py:120-370`), and `mu new monster` needs it to place nests (`tools/mu/new_figure.py:206-224`, `world_of_map`). There is no `mu new world` verb: it is a new file, written once by hand, then edited only through `mu edit` |
| C2 | its `pipeline` block | `hidden`: the 11 types above. `grass`: from pass B (World38 ships TileGrass01/02 .OZT). `void` / `void_fill` / `border_fill`: pass B, after the first shot. `airs_from: "lorencia"` for the butterflies. `anchor_kinds`: from step 2 on. Knob shapes are in `pipeline/world_manifest.py:108-150` |
| C3 | its `rules` block | `arrive [20, 218]` (gate 138's middle: open and safe), `home 0` (own safe zone), `event false`, `floors false`, **`move_order 8`** (after Aida's 7: it is the M key's walk, `realm_tables.py` docstring). `drops` (`feather`? `nova_scroll`: MU's Nova comes off Kanturu's bags, `src/sim/items.h:207`), `second_class true`. A manifest with no `rules` is "not a world anyone stands in" (`pipeline/realm_tables.py:27-28`): step 1 can import and look before the realm knows it |
| C4 | its `gates` block | `spawn {number 138, box [19,217,21,219]}` (the .mur's safe box: `world_manifest.spawn_gates`, `:86-96`; **without it a death in Kanturu rises in Lorencia**, the gate-22 lesson). Plus `exits` 126 (trimmed) and `enters` 127 (§4) |
| C5 | its `show` block | `air` (`kan_ruin_global` as `world_kanturu`, everywhere, `room: "open"`); `music {track "/music/kanturu_1st.mp3", where "safe"}`; `indoors`; `boids` (Butterfly01 on Aida's numbers); later the emitters. Read by `src/game/world/world_show.h`. A system opens only where its field is set, so step 1 may leave `show` nearly empty |
| C6 | `content/world/kanturu/ground.json`, new | On Aida's shape (`tileset`, `mu_name`, `profile "tiles"`, `sheets`, `sheet_from`, `sheet_materials`). **Prefix `kt_`**: `ls content/textures` shows no `kt_`, `ka_` or `kr_`. One flat pool, so the bare Tile* names collide. `mu make ground` refuses without it (`tools/mu/make.py:438-439`). Sheets are decoded with `pipeline/decode_texture.py $D/World38/<Tile>.OZJ content/textures/kt_<tile>.png` for the slots in use |
| C7 | `content/sheets/worlds/kanturu.json`, new | The light layer over `lighting.json`, found by name. Tuned live through the MCP's `light world=kanturu`. A file must exist first: an empty `{ "note": ... }` the light tool then writes keys into |
| C8 | `content/sounds/sounds.json` + `content/sounds/kan_ruin_global.wav` | `world_kanturu` event (loop, gain about -20 dB as `world_aida`), then `./mu make sounds` (the showing cook) |
| C9 | `content/world/kanturu/nests.json` | Written by `./mu new monster <n> --world kanturu --db` per breed (`new_figure.py:206-224`). Ids continue the global run (`world_manifest.nests` refuses a duplicate) |
| C10 | `content/world/tarkan/world.json` `gates` | The door rows (§4) |

#### 2.2 Pipeline and cook (automatic once C1 exists)

- `pipeline/terrain.py`: all knobs come from manifests. No per-world code.
- `pipeline/index.py`: gate boxes from `world_manifest.gate_boxes()` (`:74-84`); the world list from the cooked tree. No edit.
- `pipeline/cook.py` / `cook_anchors.py`:
  - `flat_over_void`, `grounded_types`, `lifted`, `anchor_kinds` and `crawls_from` are manifest knobs;
  - a new emitter kind number (after Aida's 22) needs a drawer in `src/game/world/` plus a row in `src/content/cooked.h:201-205`'s comment table. That is from step 2 on, not step 1.
- `tools/mu/things.py:161` `cooked_worlds()`: every world's tables are cooked by `mu make ground`. That makes **thirteen** worlds once Kanturu is cooked.

#### 2.3 Runtime code still named per world

| # | file:line | today | Kanturu |
|---|---|---|---|
| R1 | `src/sim/maps_table.inc:5-17` | generated by `pipeline/realm_tables.py` from `rules` | automatic: `./mu make realm` |
| R2 | `src/sim/gates_table.inc` | generated from `gates` | automatic: `./mu make realm` |
| R3 | `src/game/ui/minimap.cpp:335-339` `mapName` | the manifest's `name` via `worldShow` | automatic. Tarkan's door then reads "Gate to Kanturu" |
| R4 | `src/game/ui/minimap.cpp:318-333` `floorName` | Dungeon and Lost Tower floors by exit gate | nothing (Ruins 2/3 only if their warps come) |
| R5 | `src/sim/realm_travel.cpp:20-64` `kRows`; `src/sim/travel.h:32` `kTravels = 17` | Aida last (`:63`) | Append `{"Kanturu", 37, L, 9000, 20, 218, 0, 0, "kanturu"}`, `kTravels` 18. Append only: `found` is a u32 bit set (`src/sim/realm.h:791, 2488`), so 14 bits are left. The opening rule sits beside Aida's (`:168`); the user picks it (a quest or "stood there") |
| R6 | `src/game/ui/travel.cpp:248` `kComing` | Lost Tower, Atlans, Tarkan, Icarus | nothing, or a dimmed "Kanturu" card before the row exists |
| R7 | `src/sim/elite.h:24-32` `kEliteMaps` | the Elites' maps (Lorencia first, 2026-10-21) | a row when Kanturu gets its Elite (the user: "for each map") |
| R8 | `src/sim/items.h:990-994` `kAncientLevel`, `kRuneRarityLevel` | 141 | nothing, if the breeds are raised to 141 or more (§5) |
| R9 | `src/game/play_mode.cpp` music | `show.music` | automatic. Copy `kanturu_1st.mp3` into `out/assets/music/` (gitignored; **lower-case name**, case-sensitive on Linux and Hetzner) and into the dist |
| R10 | `server/src/world.cpp:23, 83-92` | raises any world by name from `cooked/<w>/<w>.mur` | automatic: one server serves every map (memory). `server/deploy.sh` ships every `.mur`. **The server binary must be rebuilt** because the generated sim tables change |
| R11 | `tools/bot/brain.h` `kWorlds` | -- | only when bot work resumes (paused) |
| R12 | `tests/sim_test.cpp` | `testAidaGates` pattern | `testKanturuGates`: Tarkan 7,200 to 6,200 at L goes Gated 125 and lands in 126's box; at L-1 Barred; Ruins back through 127; `kanturu.safeGate == 138`'s box; `gateToward(8, 37)` is 125 and `gateToward(0, 37)` routes through Atlans and Tarkan |
| R13 | `src/net/wire.h:164, 174` | 45 / 10 | not for step 1-4 (§0.6). A Kanturu quest changes the quest count, so `kKeptLayout` goes up (memory: the kept layout follows every count) |
| R14 | release check (`tools/mu/release.py:62, 155`; `mu ship`) | reads `kVersion` | run it. The rules print alone already refuses a mixed client and server |

Load path, unchanged: `PlayMode::travel` -> `World::open` -> `Play::open` -> `cooked/kanturu/kanturu.mur`, then the `show` systems.

---

### 3. What Tarkan's and Aida's machinery Kanturu reuses

| machinery | where it lives | Kanturu |
|---|---|---|
| **void / abyss** (`pipeline.void {start, depth, blend}`, the ground shader's abyssEdge) | Aida, Tarkan, Devias manifests | the Ruins' NoGround share is for pass B to read from `attributes.png`. Start from Tarkan's `{start 0.3, depth 3.0}`, and add Aida's `blend 1.5` only if bare lips show black |
| **world walls / border fill** (`border_fill`, `blend_keep_near`, `void_fill`) | Aida | the same knobs, once pass B names the wall types |
| **black clear** | the light sheet's clear and fog (Aida's and Tarkan's sheets) | yes. MU draws Kanturu with no sky arm. Copy Tarkan's or Aida's sheet as the start, then tune live |
| **underground air vs open** (`show.indoors.everywhere`, `show.air.room`) | Aida: open forest; Tarkan: indoors everywhere | Ruins: open (`room "open"`, `indoors false`) so the butterflies fly. Tower: indoors |
| **butterflies** (`show.boids` Butterfly01 + `airs_from lorencia`) | Aida | the same block. MU's type-37 emitter can stay hidden, as Aida's |
| **waterfall scroll and spray** (`game/world/waterfall_spray.h`, sheets `effects/aida/waterfall1-3.png`) | Aida | type 77's falls: the V scroll is a recipe glow scroll. Spray only if pass B finds MU throws it |
| **small fires, terrain-light anchors** (`small_fires.h`; light-only anchors, `cook_anchors.py:35`) | Aida | types 60/61's orange lights as light-only anchors |
| **bubbles** (`game/world/bubbles`) | Atlans | type 97's single emitter |
| **chrome and blend meshes** (`blend_mesh`, `chromed`) | Tarkan | types 46 and 102's pulsing BlendMeshLight |
| **sealed gate** (`gates.enters[].sealed`, `target -1`) | gates.h:44-47; play.cpp:1086 | the door's interim state, and the Relics door until step 7 |
| **the studio arena** (`show kind=monster`) | Aida's breeds were judged there | each Kanturu breed at its spawn |

---

### 4. The door from Tarkan

Today Object86 (the arch with `chogatea02_R`), Object87 (pillars) and Object88 (steps) stand at Tarkan (451.9, 19955.7) and around it as scenery. No gate row stands under them, so walking into the arch does nothing.

1. **Rows, with MU's numbers.** `mu gate add` takes new numbers from 200 and 2x2 boxes on the arrival tile (`tools/mu/gates.py:13-19, 32`), so it cannot write MU's 125-128. Use `mu edit --insert` on both manifests, in one sitting:
   - Tarkan `gates.enters` += `{"number": 125, "box": [6,199,6,201], "level": L, "target": 126, "note": "OM S6 Gates.cs:537 ..."}`;
   - Tarkan `gates.exits` += `{"number": 128, "box": [7,199,7,201], "faces": [1,0], "note": "Gates.cs:201"}` (facing east, away from 125);
   - Kanturu `gates.enters` += `{"number": 127, "box": [17,220,19,222], "level": 0, "target": 128}` (out free, the house rule);
   - Kanturu `gates.exits` += `{"number": 126, "box": [17,219,21,219], "faces": [0,-1]}` (trimmed off 127).
2. **Interim, if the door goes in before the Ruins are playable:** add only 125 with `"target": -1, "sealed": "Kanturu"`. The game then says "Kanturu is sealed. Its door will not open." The arch explains itself, which is Aida's decision 8 for Karutan.
3. **Level** (the user decides). OM asks 150 in and 130 out. The house rule sets a door near its breeds (Aida's door is 300). If the breeds are raised past 140 for the Ancients, the door belongs at about Aida's 300 or above.
4. Then:
   - `./mu make realm` (the generated tables);
   - a rebuild of `mu2` and `mu2_server`;
   - `./mu make tables tarkan` and `tables kanturu` (the gate boxes stay open in the grid: `world_manifest.gate_boxes`);
   - a sim_test.
   - The Hetzner server needs a redeploy before a client with the door can play: ask first.
5. **Seen:** `show` Tarkan at 8,200 in the studio (`do "scene tarkan; place; mode gates; mark 125"`), then `shot`. Gate outlines show in Place's gates mode.

---

### 5. The Ancient rune drop

- **The wiring.** A monster's rune rarity is drawn among the rarities its level reaches (`kRuneRarityLevel`, `src/sim/items.h:994`; draws at `src/sim/realm_items.cpp:2512, 2529`). An Ancient takes a share of 0.11 of 1.36 (`:840-842`). Ice Storm is the only Ancient power so far (`items.cpp:645-648`). Nothing is keyed by map: **level ≥ 141 is the whole gate.**
- **What it means for Kanturu.**
  - Each breed's recipe `combat.level` (`content/monsters/<Name>.json`, as `BloodyWitchQueen01.json:30`) must be 141 or more for Ancients to fall.
  - Set it with `mu edit <Name> combat.level <v>`, then `./mu make realm` and the tables.
  - Proposal (ours, for the user): wolves 141 up to the Genocider about 160, HP and damage scaled past the Bloody Witch Queen (175 000 HP, 1250-1320).
  - Or keep the early breeds under 141 and let only the far half drop Ancients. That is the user's call.
- **A trap in the drop window.** Items drop from the window [L-15, L] at the top of the item list (aida-port "Raised past Icarus"). At 141-160 the window holds only the top items (the Staff of Kundun and the other 140s), so the pool is thin. Check how many items each level draws before the user judges the loot.
- **Second-class gear and feathers** are `rules.second_class` and `rules.drops`, as Aida's.

---

### 6. The build, in order

Rules for every step:
- Work in the user's open studio through the cooker MCP: `status` first, `show` the thing, `shot` before and after.
- Edits go through `edit` / `gate` / `light` / `do`, or `./mu edit`. Never hand-dump JSON (new files are the exception: written once).
- `plan` before every `cook`. **Ask before a ground cook, every world's tables, or realm plus a rebuild.**
- `nice`, at most 6 threads (`./mu cores`).
- Commit with explicit paths after `git diff` (`src/app/modes/*`, `src/game/ui/hud.cpp` and others are dirty from other sessions today).

**Step 1: the Ruins' bare land, shown in the studio.**
1. Write `content/world/kanturu/world.json` (C1) with `map 37`, `pipeline.hidden` (the 11 types), `pipeline.airs_from`, `gates.spawn` 138, and **no `rules` yet**.
2. Decode the tile sheets in use to `content/textures/kt_*.png`, and write `content/world/kanturu/ground.json` (C6).
3. `./mu make ground kanturu --folder 38 --plan`, from the CLI. **The MCP's `cook` cannot pass `--folder`**: `t_cook` sends only the target (`tools/mu/mcp_server.py:239-244`), while `mu serve` takes `folder` (`tools/mu/serve.py:71, 167, 227`) and the CLI hands it over (`tools/mu/jobs.py:123-125`).
   - Expect the plan: terrain ("never imported"), tileset, ground, index, sync, `ground kanturu`, then tables for every cooked world (`make.py:416-532`).
   - Expect the import: 0.00-3.83 m, 14 slots, 27.4% walkable, 3 645 / 100 kinds.
4. Ask, then run `./mu make ground kanturu --folder 38`. It goes to `mu serve`, so the studio shows the bars. After this first run the folder is remembered (`make.py:442-443`), and the MCP's `plan`/`cook ground kanturu` work.
5. `content/sheets/worlds/kanturu.json` (C7). Then `do "scene kanturu; tab light"`, `light world=kanturu key=...` live, `do "time dusk"`, `shot`.
6. Seen: `do "scene kanturu"`, then `shot` at the town (20,218), the Relics door (89,90), the 141 clearing (206,38) and region two (70,187). Expect bare ground over the black cut, no objects. **Show the user.**
7. Then the `rules` block (C3) and the `show` block (air, music): `./mu make realm --plan`. Ask, cook, and rebuild `mu2` (and the server later). `--world kanturu` in the game, and `checks`.

**Step 2: objects in small batches** (3-6 kinds, then show; memory):
- Per kind: `./mu new object kanturu/ObjectNN`, then `plan`/`cook world/ObjectNN`. Textures before meshes: `mu make` holds the order.
- Then `placements kanturu`. `show kind=object name=ObjectNN`, `shot`.
- Order: the big counts and walls first (Object66 x232, 69 x225, 06 x208, 68 x198, 76 x186), so the void edges close; then ruins and machines; then the lit, scrolling and moving types (44, 46, 70, 76, 77, 90, 96, 102); last the anchors and emitters (60/61 lights, 97 bubble, 37 butterflies), with `anchor_kinds` and their drawers.
- Watch the cook's buried and grounded counts for `flat_over_void` / `grounded_types`.

**Step 3: the door from Tarkan** (§4): the four rows by `mu edit --insert`, `plan realm`, ask, cook, rebuild, `testKanturuGates`. Show it at Tarkan 8,200 in Place's gates mode. The server redeploy waits for the user.

**Step 4: monsters, one breed at a time,** in ladder order: Splinter Wolf (351), Iron Rider, Satyros, Blade Hunter, Kentauros, Gigantis, Berserker, Genocider. For each:
1. `research name=351`;
2. `./mu new monster 351 --world kanturu --db`, which writes the recipe and its 22 nests;
3. `research name=SplinterWolf01 write=true`, the MuMain checklist;
4. `mu edit` its `combat.level` and stats (§5), and its `rules` (drops, resists);
5. `plan` and `cook` (ask: every world's tables, realm and a rebuild);
6. `show kind=monster name=SplinterWolf01`, the arena at its spawn. The user judges; `arena_log` shows how the fight went.

Sounds come from `D/Sound/w37` (name the files that exist) and go through `./mu make sounds`. The Warriors (553-556, region two) wait for the "Ruins 3" warp decision.

**Step 5: the travel row** (R5), the Elite row (R7), and the drops (`feather`, `nova_scroll`) as the user decides. Then the release check, and one deploy of client and server together.

**Later: Relics (map 38) and the Tower event (map 39).**
- Relics: manifest `map 38`, World39, gates 129/130/131/132 (the Ruins door asks 220 in OM; it is sealed until then), spawn 137. Its breeds are in `OM/VersionSeasonSix/Maps/KanturuRelics.cs` (105/106 traps, the Maya hands).
- Tower: an event map, `rules.event true`, like Blood Castle and Devil Square, with Maya and Nightmare. Its two terrains (`EncTerrain40` and `401`, swapped when the barrier opens) are new engine work: no world swaps its ground at runtime today. Read `OM/VersionSeasonSix/Maps/KanturuEvent.cs` and `MM/.../GM_Kanturu_3rd.cpp` before planning it.

---

### 7. Risks and traps

1. **The manifest must come first.** terrain.py reads knobs from `world.json`. An import before it ignores the hidden types, and the 399 emitter boxes show as meshes.
2. **The MCP's `cook ground kanturu` has no `--folder`.** The first import is CLI-only.
3. **A ground cook re-cooks every world's tables** (thirteen worlds). Ask, and cook nothing stale because of other sessions' edits.
4. **The spawn gate row** (`gates.spawn` 138). Without it a death goes to Lorencia. Check the `.mur` header's safe box reads 19,217,21,219.
5. **Gate 126 overlaps 127.** Trim it.
6. **Two regions.** Region two (5 514 tiles, the Warriors) has no walk in. Leave it unspawned, or give it a warp row. The multi-landing flood in `realm_travel.cpp` (`settleFound`, still sequential) is the Aida trap again if two rows land on one map.
7. **Ancients need level 141 or more.** At OM's levels none drops. Raising the levels thins the item window.
8. **The 599-step walk to the Relics door.** A warp row or "Ruins 2" (141) will be wanted.
9. **The server must be rebuilt** for every sim table change (realm, gates, breeds), and deployed together with the client: the rules print refuses a mixed pair. The Hetzner redeploy is the user's call.
10. **Music file names**: `kanturu_1st.mp3` in lower case. It must also go into `out/assets/music` and the dist (gitignored).
11. **`mu gate add` numbers from 200.** MU's 125-128 go in with `mu edit --insert`, both sides in one sitting. Never leave half a pair.
12. **Shared files are dirty** (`src/app/modes/play_mode.cpp`, `director.*`, `src/game/ui/hud.cpp`, `src/client/remote_link.*` staged by another session). Diff before committing. Commit with `git commit -F - -- <paths>` only.
13. **The Tower's two terrains** need runtime ground swapping, which nothing has today. That is planned work, not a port step.

---

### 8. Decisions for the user (engine side)

1. **Breed levels** (Ancients from 141): raise all eight past 140, or only the far half.
2. **Door levels**: Tarkan to the Ruins (OM 150; ours near the breeds, about 300+); the way back free.
3. **Interim door**: sealed ("Kanturu is sealed") now, or nothing until the Ruins are playable.
4. **Region two and the Warriors (553-556)**: leave them out, or add a "Ruins 3" warp later.
5. **Travel rows**: one "Kanturu" row (recommended), plus how it opens (a quest, or standing there).
6. **Music**: `kanturu_1st.mp3` in the safe town only (house rule).
7. **Drops**: feather, Nova scroll (MU's Nova came from Kanturu's bags), second-class gear.
