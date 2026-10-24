# Kanturu's Gateway Machine (NPC 367) -- build dossier

The Relics' (map 38) way into the Refinery Tower (map 39). Research only, 2026-10-24; nothing here
is built. Companion to `docs/worlds/kanturu-port.md` (**Port**), whose §2.1 (gates), §2.3 (entry
rules) and §7 (the event) this narrows to the machine.

Sources and their short names:

- **MM** -- MuMain, `LEGACY/reference/MuMain/src/source` (the clean client).
- **Loc** -- MuMain's `src/Localization/Game.en.resx`, by `legacy_id`.
- **WZ** -- WebZen GameServer 1.00.93, `github.com/ptr0x-real/Mu-GS-Webzen-MC-10093`,
  `Source/Server Side/GameServer/` (cp949; clone to the scratchpad, `iconv -f cp949`). Outranks OM.
- **WZD** -- that repo's `_Server Files/Data`: a 2012 private repack. Its `Kanturu.dat` is the only
  timing table on disk; the numbers look stock, but treat them as WZD's, not WebZen's.
- **OM** -- OpenMU (`LEGACY/reference/openmu`): it has the NPC and the packets, **no event**.

## 1. What the player sees and does

**Approach.** The machine is a townsperson, not a monster: `MONSTER_GATEWAY_MACHINE` makes
`MODEL_KANTURU2ND_ENTER_NPC` (`Data/Npc/to3gate.bmd`, `MM Engine/Object/ZzzOpenData.cpp:2034-2037`)
at scale 4.76, offset -20 x and -200 y from its tile, no shadow, held on action 0
(`MM World/GameMaps/GM_Kanturu_2nd.cpp:106-114`). It faces 90° and gets an edge scale of 1.0
(`MM Network/Server/WSclient.cpp:2898-2902`). Its pick box is grown: -100 in x, +100 in z, axes
+100 (`MM Input/Selection.cpp:166-171`). Clicked, the hero talks from **550 units (5.5 tiles)**
without walking; farther, he paths to it first (`MM Engine/Object/ZzzInterface.cpp:3263-3281`).
Ours stands at 141,191 on a NoMove tile; the walk reaches 141,186 (Port §1.3, lines 274-276).

**The talk.** The server answers the talk with the event's state, not a dialog: NPC 367 is
`NPC_KANTURU_MACHINE` (`WZ NpcTalk.h:86`), dispatched to `NpcMainatenceMachine`
(`WZ NpcTalk.cpp:477-479`), which sends `0xD1 0x00` state info (`:703-707`;
`WZ KanturuEntranceNPC.cpp:35-50`): state, detail state, `btEnter` (can enter now),
`btUserCount` (battle users), `iRemainTime` (s). Its arrival opens the window
(`MM UI/NewUI/Events/NewUIKanturuEvent.cpp:197, 369-372`).

**The window** (`CNewUIKanturu2ndEnterNpc`, `INTERFACE_KANTURU2ND_ENTERNPC`):

- 230 × 267, the message box's top/middle×10/bottom/back plates (`NewUIKanturuEvent.h:24-25`;
  `.cpp:401-408, 536-556`). Opening it hides group B windows (`MM UI/NewUI/NewUISystem.cpp:957-960`).
- A bold subject, blue `0xFF49B0FF`, centred at y+30, wrapped at 52 characters; then up to three
  state lines, the first green `0xFF61F191`, the rest bright yellow, 15 px apart (`.cpp:564-597`).
- Three 53 × 23 buttons at y+220: **Refresh** x+17, **Enter** x+87, **Close** x+157
  (`.cpp:419-439`). Enter is greyed (100,100,100) and locked unless `btEnter == 1` (`:213-224`).
- While open it re-asks `0xD1 0x00` every 2000 ms (`:112-119`; `MM Core/Globals/_define.h:576`);
  Refresh asks at once and then locks for 1000 ms (`:445-458`; `_define.h:577`). Escape closes
  with `SOUND_CLICK01` (`:97-108`). A new state is ignored while the machine is turning or a
  message box is up (`:199-207`).

**What it says, by state** (`.cpp:226-366`; texts are Loc ids):

| state (detail) | subject | lines |
|---|---|---|
| Standby, START | 2158 "Boss Battle will start soon." | 2159 "Force of the Nightmare has invaded the Tower ... restricted for %d minutes" (remain/60); 2160; 2161 |
| Standby, other | 2158 | 2162 "You will be able to approach Maya shortly."; 2160; 2161 |
| Maya, STANBY1 | 2163 "More players are needed to open the path to the Tower." | 2164 "You may now enter." (under 15 users, or Enter lit), else 2172 |
| Maya, STANBY2 / 3 | 2163 | 2165 / 2166 "Nightmare has lost the control of Maya's left / right hand. Currently there are %d survivors."; 2167 "More power from %d players are needed." (15 - users) |
| Maya, fighting (MONSTERn, MAYAn, ENDn) | 2152 "Battle with Maya is ongoing." | 2153 "%d players are trying to open the path ..."; 2154/2155/2156 "Currently %d players are in battle with Maya's left / right / both hands" |
| Nightmare | 2152 | 2153; 2157 "Currently %d players are in battle with Nightmare." |
| Tower, REVITALIZATION or NOTIFY | 2149 "You may now proceed to the Refinery Tower." | 2150 "Path ... is now opened."; 2151 "... will be closed in %d hours." (remain/3600) |
| Tower, CLOSE and after | 2174 "You can't warp to the Refinery Tower." | 2160 "Defeat the Nightmare that controlling the Maya ..."; 2161 "... 'Moonstone Pendant' is required." |

**Enter.** The window hides (`:466-470`). In the Tower state the client checks nothing and starts
the machine at once (`:472-477`). Otherwise it checks, in order, and stops with a box on the first
failure (`:479-521`; texts `:170-195`):

1. Horn of Uniria in the helper slot: "You cannot warp while riding on a unicorn" (POPUP_UNIRIA 5).
2. A transformation ring in either ring slot: 2175 (POPUP_CHANGERING 6).
3. No wings (1st/2nd/3rd, the MG and DL capes), Dinorant, Dark Horse or Fenrir: 2176 "You can only
   warp riding a Dynorant, Dark Horse, Fenrir or wearing the wings, cloak." (POPUP_NOT_HELPER 7).
4. The **Moonstone Pendant (13,38) in a ring slot**, or 2172 (POPUP_NOT_MUNSTONE 2).

Passed, the machine plays its opening (§3); at key 42 the client sends `0xD1 0x01`
(`GM_Kanturu_2nd.cpp:1196-1205`). The server checks again (`WZ Kanturu.cpp:591-788`):
on map 38 only (else 4, `:597-606`); Maya state with entry permitted (`:609`): 15 users max
(1, `:611-619`; `WZ KanturuDefine.h:7`), the pendant **with durability > 0** (2, `:621-629, 810-834`;
switchable by `Kanturu.dat` section 10), wings/mount (7, `:632-660`), no Uniria (5, `:664-671`), no
transformation ring 13,10/39/40/41/68/76 (6, `:674-750`). Tower state with entry permitted: **no
checks at all** (`:762-771`). Anything else: 3 (`:772-780`).

**Consumed: nothing.** The pendant is only worn. It loses 1 durability about every 90 s while
worn (Port §2.3, `WZ user.cpp:11006-11017`); only one may be carried or worn
(`WZ protocol.cpp:4612-4626`; `user.cpp:16116-16123`), never sold in a personal shop
(`user.cpp:17414-17419`). It drops on map 38 at `g_iKanturuMoonStoneDropRate`/10000
(`WZ gObjMonster.cpp:5543-5555`).

**Where he goes** (`WZ KanturuEntranceNPC.cpp:55-130`; gate boxes Port §2.1, lines 309-313):

- Maya state: **gate 133**, Maya's room, 196,56-201,57 on map 39; he is added to the battle users
  and sent `0xD1 0x02` current state (`:66-101`).
- Tower state: **gate 135**, the Nightmare's hall, 78,93-82,95 on map 39 (`:104-111`).
- Failure: `0xD1 0x01` with the code (`:119-129`); the client shows Loc 2170 + code (1 "15 players
  have already entered", 2 "'Moonstone Pendant' authentication has failed"), 3/4 "Failed to enter.",
  5-7 as above, then stops the machine and deletes its energy joints (`.cpp:170-195, 375-385`).
  On success no `0x01` comes; the machine finishes at key 50 (`GM_Kanturu_2nd.cpp:1208-1214`).
- Gate names (`WZ KanturuDefine.h:21-31`): 133 Maya, 134 Nightmare (used to move the battle users,
  `WZ KanturuBattleOfNightmare.cpp:329`), 135 tower open, 136 kick-out from the tower to the Relics
  (137,162-143,163), 137 death/regen in the Relics (71,102-82,109).

## 2. The event behind it

`CKanturu` runs without a schedule: from None it goes straight to Standby (`WZ Kanturu.cpp:402-406`),
and End goes back to None (`:479-483`), so it loops for ever. States (`WZ KanturuDefine.h:109-175`;
the client's enum is the same numbers, `MM Core/Globals/_enum.h:3702-3753`):

| state | flow (`WZ Kanturu.cpp:349-483`) | WZD time (`Kanturu.dat`) | entry |
|---|---|---|---|
| 1 Standby | START → NOTIFY → END, then Maya | 300 s + 60 s notice (sec. 1) | no |
| 2 Maya | 3 rounds; success → Nightmare, failure → End (`:422-442`) | scenes 900/900/1200 s (sec. 2); round: STANBY 120/60/120 s, NOTIFY 15 s (1st only), START, MAYA, END 5 s, ENDCYCLE 3/3/10 s; END 3 s (sec. 3) | **only in STANBY1/2/3** (`WZ KanturuBattleOfMaya.cpp:515, 610, 693`) |
| 3 Nightmare | IDLE 5 → NOTIFY 10 → START 1200 → END 5 s (sec. 7); success → Tower, else End (`:444-464`) | | no |
| 4 Tower | REVITALIZATION 82 500 s → NOTIFY1 300 s → CLOSE 2 220 s → NOTIFY2 180 s → END (sec. 9); opens the map's walls (`:380-389`) | | REVITALIZATION and NOTIFY1 (`WZ KanturuTowerOfRefinement.cpp:245-266`) |
| 5 End | users reset (`:391-400`) | | no |

At the tower's END everyone on map 39 is moved to gate 136 (`WZ KanturuTowerOfRefinement.cpp:293-315`).
A failed battle moves the users to gate 137 (`WZ KanturuBattleOfNightmare.cpp:396`;
`KanturuBattleUserMng.cpp:146-178`). S17's tower lasts 21 600 s (Port §7).

**Packets, all `0xD1` subcodes** (server `WZ KanturuUtil.cpp:40-124`; client
`MM WSclient.cpp:11915-11993, 14425-14456`):

| sub | way | what | the client |
|---|---|---|---|
| 00 | CG ask / GC answer | state info (above) | fills and opens the machine's window |
| 01 | CG ask / GC answer | enter; the answer only on failure | message box, machine stopped |
| 02 | GC | current state, on arrival in 39 | `Kanturu3rdState`, `CheckSuccessBattle` |
| 03 | GC | state change | in 39: shows the info window in MONSTER1-3/MAYA1-3 and Nightmare BATTLE, else hides it (`:11935-11968`) |
| 04 | GC | battle result | SUCCESS / FAILED banner, 372 × 99, fading in 0.01 a frame, held 5 s (`MM GM_Kanturu_3rd.cpp:1651-1742`) |
| 05 | GC | time limit (ms) | the info window's mm:ss (`NewUIKanturuEvent.cpp:768-772`) |
| 06 | GC | Maya's wide attack (Ice Storm / Broken Shower) | `MayaSceneMayaAction` |
| 07 | GC | monsters and users left | the info window's counts |

**What 39 shows by state** (`MM GM_Kanturu_3rd.cpp`): the info window, 99 × 78
(`newui_Figure_kantru.tga`), "Character: %d" and "Monster : %d" or "Monster : Boss" in the Maya
fights, and a blinking mm:ss (`NewUIKanturuEvent.cpp:693-746`). Music: Maya battle in Standby and
Maya, Nightmare battle in Nightmare, KanturuTower otherwise (`:1744-1777`). The grid: Tower
REVITALIZATION/NOTIFY loads `EncTerrain401.att` and plays `SOUND_KANTURU_3RD_MAP_SOUND02`; any
other state goes back to `EncTerrain40.att` (`:59-90`).

## 3. The machine itself

**Actions** (`MM _define.h:579-580`; ours `content/npc/GatewayMachine.json` rig_note): 0 STOP, one
key, held; 1 ROT, the opening, 51 keys. NPC actions play at **0.25 keys a reference frame**
(`ZzzOpenData.cpp:2230-2231`), 6.25 keys/s: the enter request goes at key 42 (**6.7 s**), the
machine stops at key 50 (**8.0 s**) and drops back to STOP (`GM_Kanturu_2nd.cpp:1196-1214`).

**Mesh look** (`GM_Kanturu_2nd.cpp:729-745`): mesh 0 plain; meshes 1 and 2 chrome
(`BITMAP_CHROME`) + a bright overlay (`BITMAP_KANTURU_2ND_NPC3` / `NPC2`) + plain; mesh 3 chrome
with blend mesh 3 scrolling V at `-WorldTime * 0.0003`, then bright `NPC1`, then plain. Ours carries
none of it yet (`GatewayMachine.json` effects_note).

**The hole sound.** `SOUND_KANTURU_2ND_MAPSOUND_HOLE`, `Data/Sound/w38/kan_relic_hole.wav`, one
channel (`MM World/MapInfra/MapManager.cpp:441`), 1.99 s, 22 050 Hz stereo (afinfo). `PlayBuffer`
is called **every frame of action 1** (`GM_Kanturu_2nd.cpp:1091-1093`); a playing buffer is not
restarted (`MM Audio/DSPlaySound.cpp:307-353`), so it sounds as a loop, about four times over the
opening. Flat (no object passed). Not in `content/sounds/sounds.json` (the Relics carry global,
gear, incubator, `:3038-3060`).

**Effects during action 1** (`GM_Kanturu_2nd.cpp:1089-1195`; bones registered at `:119-132`;
`rand_fps_check(4)` is a one-in-four draw each 25 Hz reference frame, `MM Engine/AI/ZzzAI.cpp:706-709`):

| keys | what | where | numbers |
|---|---|---|---|
| 0-39 | `BITMAP_SPARK + 1`, subtype 18, scale 1.0, light (1,1,2), at `rand_fps_check(4)` | bones 37-43 (seven) | `:1099-1119` |
| all | `BITMAP_CLUD64`, subtype 2, scale 2.0, light (1,1,2), at `rand_fps_check(4)` | bones 6, 7, 8, 15, 16, 17 (bone 10 is registered, unused) | `:1121-1136` |
| all | sprite `BITMAP_LIGHTNING + 1`, scale 1.0, turning `WorldTime / 10` | position + (40, -100, 310) | light 0 to key 10, `(key-11)·0.06` grey over keys 11-28, 1.0 from 29 (`:1138-1159`; the `== 10` branch is dead) |
| 12-25 | two `BITMAP_JOINT_ENERGY`, subtype 42, scale 10, every frame | from position + (rand ±300, -(350..549), -100..499) to position + (0,0,290) | `:1161-1172` |
| 1-20 | `BITMAP_JOINT_SPARK`, subtype 0, angle (290..349, 0, 90..119) | position + (260, -115, 50), x +4·key for keys 1-9, then 36 - 4·(key-9) for 10-18 | `:1174-1194` |

"Position" includes the -20/-200 create offset, which ours does not carry (`GatewayMachine.json`
scale_from): the offsets above must be measured from MU's spot, not our tile centre. No light is
added to the ground.

## 4. What MU2_BGFX already has

- **The NPC**: `content/world/kanturu_relics/world.json:89-99` (npc 367, 141,191, look 3, figure
  `GatewayMachine`); `content/npc/GatewayMachine.{json,obj,rig.json}` (51 bones, actions 0 and 1).
  Talked to, it does nothing: `Realm` routes talks by number (`src/sim/realm_folk.cpp:305-352`) and
  367 matches no branch.
- **The door pattern, twice**: the Messenger (233) and Charon (237) set `gating` on a talk
  (`src/sim/realm_folk.cpp:338-342`; `src/sim/realm.h:219-231, 1270-1296, 1344`). The window is
  the quest dialog's gate page (`src/game/ui/quest_dialog.h:20-27, 55, 64`): words, requirements
  struck when met, Not now / Enter; Enter goes to the realm (`src/game/ui/desk.cpp:650-656`).
- **Entry checked now, passed next tick**: `Realm::enterCastle` / `passCastle`
  (`src/sim/realm_move.cpp:689-711`) and `enterSquare` (`src/sim/realm_square.cpp:68-75`): refusal,
  an owed field in `EventSeat` (`src/sim/event.h:31-49`), then `passGate` to an enter gate. Ours
  box-less enter gates (1058+, `src/sim/square.h:14-24`) show how to add 133/135 without MU boxes.
- **Event maps**: `EventKind` / `eventOf` keyed by map number (`src/sim/event.h:20-25`); the maps
  table's `event` flag ("a save never resumes inside it", `src/sim/maps.h:33-35`); per-run
  instances (`src/sim/castle_instance.*`, `square_instance.*`).
- **Timers and banners**: the herald's "event coming" line with clock and place
  (`src/game/ui/herald.h:1-14`), the raid boss bar (`src/game/ui/boss_bar.h`), the square's result
  (`src/game/ui/square_result.*`), Go Back!'s countdown frame (`src/game/ui/go_back.h`).
- **Folk effects keyed to a clip's keys**: Hanzo's sparks between keys 5 and 6 off a bone
  (`src/game/play_show.cpp:976-1012`; state on `Play::Standing`, `src/game/play.h:2185-2230`).
- **The effect pieces exist**: `spark_flash` = `BITMAP_SPARK + 1` and `joint_energy` =
  `BITMAP_JOINT_ENERGY` (`src/game/fx/twin_tale.cpp:47, 50`); `iron_cloud` = `BITMAP_CLUD64`
  (`src/game/fx/iron_rider.cpp:55`); `JOINT_SPARK` streaks (`src/game/fx/forge.h:1-30`, `wheel.h`);
  `BITMAP_LIGHTNING` in `src/game/invasion_sky.*`. Whether subtypes 18 / 2 / 42 match theirs must
  be checked per effect.
- **Missing**: map 39 has no `rules` (`content/world/kanturu_tower/world.json`; no row in
  `src/sim/maps_table.inc`, which has 37 and 38 at `:17-18`); gates 133-137 are not in
  `src/sim/gates_table.inc` (Kanturu has 126-132 only, `:43-45, 82-84`); no Moonstone Pendant in
  `content/items/jewellery/`; no runtime grid swap (Port lines 1686, 1704); the Tower has ground
  only, no objects, monsters or Elpis.

## 5. Proposed build, smallest first

Each step ends in something to judge in a played run (or the studio's `show`). **T** needs the
Tower's objects (Object40), **B** the Maya/Nightmare bosses; neither exists.

1. **The machine's look.** Chrome and the three bright overlays on meshes 1-3, mesh 3's scrolling
   chrome (§3). Judge: the machine in the Relics, at rest.
2. **The hole sound.** `kan_relic_hole.wav` into `content/sounds/sounds.json` beside the Relics'
   (`kanturu_gateway_hole`), flat, looped while action 1 plays; the showing cook. Judge: heard in 3.
3. **The opening.** Action 1 once on demand (8.0 s at 6.25 keys/s), back to 0; the five effect
   rows of §3 off bones 6-8, 15-17 and 37-43, Hanzo's way. First trigger: the talk itself. Judge:
   click it, watch and hear it turn.
4. **The window.** Talk opens a gate page (quest dialog, as the Messenger's): MU's subject and
   lines for the state, Refresh-less (ours redraws on change), Close / Enter. With no event yet,
   the state is fixed at **Tower, REVITALIZATION**: Loc 2149-2151, Enter lit. Enter starts 3, and
   the opening plays on Enter, no longer on the talk.
5. **Into the Tower ground.** Map 39 gets `rules` (`event true`, home the Relics, arrival in the
   hall), an enter gate for 135 (78,93-82,95) and exits 136/137 back to the Relics; at key 42
   (6.7 s) the realm passes him through. MU's Tower state asks **nothing** of him (no pendant, no
   wings: `WZ Kanturu.cpp:762-771`; client `:472-477`), so this is faithful as it stands. A way out
   is needed: MU has none but the tower's close; ours could be a gate at the hall or Town Portal.
   Decide which grid the Tower stands on while there is no Nightmare: `EncTerrain40` (the hall only)
   or `401` (the hall plus the way to Elpis, Port §1.3). **This is the first useful version**: the
   machine turns, hums and sends you to the Tower's ground.
6. **The Moonstone Pendant (13,38).** A ring-slot item, one per character, durability that wears,
   dropping in the Relics; the window's lines and the client's four checks (Uniria, ring, wings,
   pendant) with MU's texts. New item: every world's tables. Judge: refused without it.
7. **The clock.** The `CKanturu` loop in the realm with WZD's timings (§2), the window's text by
   state, the herald's line in the Relics, entry only in the Maya standbys and the tower's first
   two phases, the kick to gate 136 at the tower's end. Without the bosses the Maya and Nightmare
   states have nothing in them, so this is only worth building with 8.
8. **The Maya battle (B, T).** Gate 133, the rounds' waves (Port §4.3), the hands 362/363, Maya
   as scenery, Broken Shower killing the pendant-less, the info window (counts, mm:ss), the
   SUCCESS/FAILED banner, the battle music.
9. **The Nightmare and the open Tower (B, T).** Nightmare 361 in the hall, the runtime swap to
   `EncTerrain401` with `kan_boss` sound 02, KanturuTower music, Elpis (368) at 77,177.

Steps 1-5 need neither T nor B. Steps 1-3 are client-only; 4-5 touch the sim (talk branch, gates,
map row) and therefore the rules print and the protocol.
