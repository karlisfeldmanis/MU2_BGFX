# Devil Square port: the record, the data, and how it plugs in

Research notes, 2026-10-10. Devil Square 1-4 is MU server map **9**, client `Data/World10` + `Data/Object10`
(MuMain's `WD_9DEVILSQUARE`). The four squares are four arenas on that one terrain. You enter from Noria through
Charon (NPC 237), who already stands there at 171,105 and says nothing yet.

Three research passes wrote this page in `docs/blood-castle-port.md`'s shape:
- Part A is the record: version, entry, the schedule, the waves, the score, rewards and monsters. It is read in
  WebZen 1.00.93 (0.97d base) and OpenMU 0.95d / Season 6.
- Part B is the data and the look: World10, Object10, what MuMain draws, monster bodies, ticket models, sound.
- Part C is the engine side: everything Blood Castle touches, what Devil Square can share, the build steps.

Part C was written alongside Part A, so its section 3.1 carries OpenMU's numbers. **Where they differ, Part A
wins** (bands, schedule, score, ticket prices); the decisions below say which.

The passes' scratch (the WebZen clone and its UTF-8 copies, the decoded World10 grid and previews, MuExtract
exports of Object10, the tickets and Monster49, contact sheets) is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/4de96907-2ebb-4791-8d64-614ff2fd7481/scratchpad/ds/`.
It is temporary; anything needed later must be rebuilt from the sources below.

Sources:
- **WZ** = WebZen GameServer 1.00.93 C++, github.com/ptr0x-real/Mu-GS-Webzen-MC-10093 (cp949: `grep -a`,
  `iconv -f cp949`). Paths are under `Source/Server Side/GameServer/` unless said.
- **WZD** = that repo's `_Server Files/Data`, a private-server repack. Cited only where nothing else exists.
- **OM** = OpenMU, `LEGACY/reference/openmu/src/`. `OMI` = `OM/Persistence/Initialization/`.
- **MM** = MuMain, `LEGACY/reference/MuMain/src/source/`. **D** = `LEGACY/reference/MuMain/src/bin/Data/`.

## Where it stands

Nothing is built. What already exists:
- Charon: model `source/npc/DevilNpc01`, his place in Noria (`tools/cook.py` Charon row, `source/world/noria/placements.json`),
  his hand-light, `kCharon = 237`. He has no window.
- Every square 1 and 2 breed is already cooked and in mu.db. mu.db has no spawns or gates for map 9.
- The herald and travel card already announce Devil Square on OpenMU's clock and bands
  (`src/game/ui/herald.cpp:70-74`, `src/game/ui/travel.cpp:69`).
- The Firecracker already rolls 1 in 300 on map 9 (`src/sim/items.h`).
- Item numbers 14/17, 14/18 and 14/19 (Devil's Eye, Devil's Key, Devil's Invitation) are free.

## The one thing to know first

**Devil Square is not 0.75, but it is older than Blood Castle.** OpenMU's Version075 has none. It first appears in
**0.95d** (squares 1-4). WebZen puts map 9 in the base of its map ladder, before Icarus. So, like Blood Castle, the
whole event is **ours / post-0.75**, and the replica target is **WebZen 0.97d: four squares on map 9**. Squares 5-6
(map 32) come in Season 1, square 7 in Season 4.

**The big gap:** WebZen's own `DevilSquare.dat` (the minutes, waves, bosses and reward rows) is in none of our
sources. OpenMU's tables are the best evidence: the repack's rewards are exactly 100x OpenMU's, and both share the
same wave frame (0-7, 5-14, 12-20 min, bosses at 15). Treat the wave and reward tables as likely, not proven.

## The goal and how it is shown (decided 2026-10-10)

**The goal.** Survive the square's 20 minutes and score as many points as you can. A kill's points
go to its last hitter: his share of the monster's HP x its level x the square's number (WZ
`DevilSquare.cpp:2219-2236`; decision 9). At the end everyone still in the square is ranked by points
and paid by rank: 1st, top 30%, top 50%, the rest, each row exp and zen (WZ `DevilSquareGround.cpp:
350-689`; OM's reward table). A death sends him to Noria and out of the ranking.

**How it is shown** (the user picked each, 2026-10-10, 'similiar like it was for BC'):
- **The tracker, in the quest's place, as Blood Castle's run is** (`game/ui/tracker.cpp`,
  `Tracker::rebuildEvent`). Ours: MU shows nothing during the fight (`CSDevilSquareMatch::
  RenderMatchTimes` is empty, `CSEventMatch.cpp:212-215`).
  - The kicker "Event", the title "Devil Square N", and the clock on its line: gold counting to the
    start, bone through the fight, blood in its last minute, "Time is up" at the end.
  - The waves as steps, MU's frame (0-7, 5-14, 12-20 min; the bosses at 15): "First wave",
    "Second wave", "Third wave", then "The bosses rise" with its minute until then and a count of
    the five after. The wave being fought is live, the ones past checked, the rest waiting.
  - Under a rule: "Your points" and "Your rank" (n / players in the square), live.
- **A banner at each wave**, in the quest banner's place (`Tracker::rebuildBanner`), under the kicker
  "Devil Square N": "First Wave" as the fight starts, "Second Wave", "Third Wave", and "The Bosses"
  with "5 minutes left" under it at minute 15. OpenMU's own lines ("Lets continue with some
  stronger enemies ...", OM `Version095d/Events/DevilSquareInitializer.cs:175-207`) were tried
  first and ran the banner across the screen; cut short they said nothing of where the fight
  stood (the user, 2026-10-10: 'show whats make sense, like first wave,seocnd,etc').
- **Built 2026-10-10:** `sim/square.h` and `realm_square.cpp` (the run's clock, waves and points,
  `a.pod(square_)`, a kill scored once in `Realm::kill`), the tracker's panel and banners, and the
  test flag `--square-at S` (the fight S seconds in). The result window is still to come.
- **The waves, square 1, built 2026-10-10:** OpenMU's seven rows in mu.db (map 9, the arena's box
  119-150 x 80-115: Cyclops and Skeleton Archer, Hell Hound and Hell Spider, Poison Bull and Shadow,
  35 each; 5 Gorgons), `kSquareBreeds` and `kSquareBoss` for squares 1-4 (sim/square.h). Every
  monster raised down and held; each wave called up once at its minute (`SquareRun::called`), its
  dead rising again only while it lasts; the bosses once at 15:00; the square cleared at the end.
  Played whole headless and in the game; a death inside goes home to Noria by the map row's home.
- **The result window at the end**, MU's rank table (`RenderMatchResult`, `CSEventMatch.cpp:222-320`)
  in our style: rank, name, points, exp and zen for everyone in the square, his own row marked, one
  button to claim, as Blood Castle's Archangel pays ("Complete", `Realm::claimCastle`). Unclaimed, it
  pays as he is sent home. The rain eases out (step 5), and after a minute's rest everyone goes
  to Noria.
- **Built 2026-10-10:** the result and the pay. As the time is up `Realm::rankSquare` ranks every
  player in the square (points, then the lower level, then the body id) and prices his row by
  WebZen 0.97d's rule (`squarePayRow`: six or fewer 1st / 2nd / the rest; seven or more 1st, top
  30%, top 50%, the rest) from OpenMU's table (`kSquarePay`), the experience at the game's rate
  (x20: 120,000 and 30,000 Zen for square 1's first). `game/ui/square_result.cpp` shows it, centred:
  "Devil Square N", MU's line 648, Rank / Name / Points / Experience / Zen for the top nine and his
  own row (marked), "Home to Noria in m:ss" and Claim. Claim (`Command::ClaimSquare`) pays his row
  and warps him home at once; its cross shuts it; unclaimed rows are paid as the rest runs out and
  each player is sent home by `warpHome` (a `Warped` home, which the mode and the server already
  carry). `Weather::dry` lets the held rain ease out as the time is up. The held rain had been dried
  four seconds after arrival by the "a raining world starts dry" branch; fixed the same day.
  `testSquareResult` in sim_test; `--square-at` works headless too.

## Decisions for the user

Merged from all three parts. Each has a recommendation; nothing is built until these are answered.

| # | decision | options | recommendation |
|---|---|---|---|
| 1 | **How to build it** (C 2.4) | (a) copy Blood Castle's code; (b) build structure-plan S1's `Instance` interface first; (c) move BC into `realm_castle.cpp`, share five small pieces (door + clock, home on death, held-down nests, tracker view, server instance worlds), Devil Square in its own files | **(c)**; `Instance` when a third event (Chaos Castle) comes |
| 2 | **Which squares** | 4 (0.97d), 1 first, or 5-7 too (12 new bodies from later maps) | engine for 4; ship square 1, then 2-3; square 4 after Cursed King's body |
| 3 | **Level bands** | WZ 0.97d: 10-99, 100-179, 180-249, 250+ (MG level x1.5); OM: 15-130, 131-180, 181-230, 231-280 (MG own band) | **WZ**, and fix the herald, which shows OM's |
| 4 | **Schedule** | WZ: play on odd hours, every 2 h; OM: every 4 h from midnight; hourly like BC (entry hh:25-30), offset so they never clash | WZ's every 2 h, as a `RealmConfig` setting like BC's |
| 5 | **Length** | WZ dat lost: code default 2 min entry + 10 play; OM 1 min + 30 s + 20 play; repack 5 + 20 | 5 min entry, 20 min play (the wave frame needs 20) |
| 6 | **One start or on entry** | (a) everyone in a square starts together (MU); (b) BC's way, from his own entry | **(a)**: a ranked contest needs one start |
| 7 | **Monsters** | WZ: 70 fixed spots, a dead one back in 1 s as a breed of the current window; OM: 35 a breed a wave, waves overlap (up to 140) | WZ's spots and respawn, OM's breed table |
| 8 | **Solo** | MU's numbers regardless, or scaled by players (as BC's quotas) | MU's first; ask after a bot run |
| 9 | **Score** | WZ: last hitter's damage share x monster level x square; OM: +square number a kill | **WZ** |
| 10 | **Party and score** | killer alone (WZ, OM), or shared by members in view | killer alone. Hook it once in `kill`, not per member (see the BC bug below) |
| 11 | **Rewards** | OM's table (likely WZ's original) by WZ's rank rule: 1st, top 30%, top 50%, rest; everyone ranked gets a row | as is, then judge with bots, as BC's "better win" came after playing |
| 12 | **Ticket** | WZ: 40k/80k/160k/300k at 80/75/70/60%; OM: 100k-700k at 80% | **WZ** |
| 13 | **Eye/Key drops** | WZ: by monster level (16-40 +1 ... 67+ +4) through the drop list; OM: flat 1% each | WZ's level brackets at OM's 1%, as BC's ingredients were raised ("ours") |
| 14 | **Death inside** | out to Noria, run lost (MU, both agree), or revive inside | out to Noria, as BC |
| 15 | **Player cap** | 10 a square (WZ); BC has none today | 10, and give BC the same while the code is open |
| 16 | **The blocked stone plain** (B) | draw it under fog as MU does, or crop to the four arenas | draw it; MU's black fog hides it |
| 17 | **Lightning** (B) | MU's silent flashes (1 s in every 4 s near the hero), or Noria's thunder | MU's, but rarer and softer, as Noria's was asked |
| 18 | **Music** (B) | MuMain plays none; `devil_square_intro/outro.mp3` ship unused | intro as the start sting, outro with the result |
| 19 | **Arena A's painted light** (B) | keep (only square 1 has it) or flatten to match | keep |
| 20 | **Cursed Wizard's scale** (B) | 1.0 in Devil Square as MuMain, or our 0.9 everywhere | 0.9; not worth a per-world override |
| 21 | **Blood Castle's party bug** (C side findings) | fix it now, separately, or with step 0 | fix it first, its own commit |

**Blood Castle's party bug, found by Part C and checked:** `countKill` calls `castleKill` with no guard
(`src/sim/realm_quests.cpp:187`), and `countPartyKill` calls `countKill` once for each party member in view
(`src/sim/realm_party.cpp:74-77`). The run's counters are the realm's. So with three members together, one kill counts
three toward the bridge and sorcerer quotas, and the statue lays its weapon three times. No test covers it; the sim
tests raise one player.


---

## In one screen

| | DS1-4, settled (WZ 0.97d unless marked) | source |
|---|---|---|
| map / client | server 9; client World10; 4 squares in one map | WZ `define.h:4858`; MM `MapManager.cpp:1598-1601` |
| gatekeeper | Charon 237 in Noria, **171,105** (OM) / 171,104 (repack) | OM `OMI/Version095d/Maps/Noria.cs:32`; WZD `MonsterSetBase.txt:24` |
| ticket | **Devil's Invitation 14/19**, level = square (+0 fits any square) | WZ `protocol.cpp:18682-18749` |
| crafting | Devil's Eye 14/17 + Devil's Key 14/18 (same level) + Jewel of Chaos 12/15 at the Chaos Goblin | WZ `MixSystem.cpp:742-860` |
| craft zen / rate | +1 40k 80%, +2 80k 75%, +3 160k 70%, +4 300k 60% (+0: 40k, 60%) | WZ `MixSystem.cpp:933-957`, `Gamemain.cpp:3612-3617` |
| level bands | DS1 **10-99**, DS2 100-179, DS3 180-249, DS4 **250+**; Magic Gladiator counts as `((L+1)/2)*3` | WZ `protocol.cpp:18714-18723, 18789-18832` |
| players | **10** a square (20 in Thailand) | WZ `DevilSquareGround.h:71-79`, `define.h:618-621` |
| when | play starts on **odd hours** (01:00, 03:00 ... 23:00); the gate is open for OPEN minutes before | WZ `DevilSquare.cpp:843-883` |
| length | OPEN / PLAY minutes come from the dat; code defaults 2 / 10; repack 5 / 20; OM 1 + 0.5 / 20 | WZ `DevilSquare.cpp:153-155, 263-265`; WZD `DevilSquare.dat:4`; OM `Version095d/Events/DevilSquareInitializer.cs:135-137` |
| monsters | 70 spawn spots a square (two breeds x 35, repack/OM); a dead one is back in **1 s** as the breed of the minute | WZ `DevilSquare.cpp:1208-1284, 1301-1341` |
| bosses | spawned once at their minute; never respawn (OM: 5 at minute 15) | WZ `DevilSquareGround.cpp:203-263` |
| score | per kill, to the last hitter: `his damage / max HP x monster level x square number` | WZ `DevilSquare.cpp:2219-2236` |
| rewards | 4 reward rows a square (exp, zen), from the dat; OM DS1 6 000 exp / 30 000 zen for 1st | WZ `DevilSquareGround.cpp:350-689`; OM `DevilSquareInitializer.cs:47-83` |
| death | no exp loss, no item drop; score sent to ranking, then reset; respawn in **Noria**; out for good | WZ `user.cpp:9300-9318, 22067-22074`; `DevilSquare.cpp:1385-1436` |
| end | rank window (0x93) to everyone still in; a minute later all are moved to **gate 27** (Noria) | WZ `DevilSquare.cpp:398-435, 965-979` |

## Part A: the record

### 1. Version: who first has it

- **OpenMU**:
  - Version075 has no Devil Square. Its map list stops at Atlans (`OMI/Version075/GameMapsInitializer.cs:30-38`). No gate 58-61, no NPC 237.
  - Version095d adds DS1-4 (`OMI/Version095d/GameMapsInitializer.cs:40-43`), Charon (`Version095d/NpcInitialization.cs:55-62`),
    the tickets (`Version095d/Items/EventTicketItems.cs:28-31`) and the crafting (`Version095d/ChaosMixes.cs:45, 280-287`).
  - VersionSeasonSix reuses the 0.95d maps for DS1-4 (`OMI/VersionSeasonSix/GameMapsInitializer.cs:88-94`). It adds DS5-7 on
    map 32 (`VersionSeasonSix/Maps/DevilSquare5-7.cs`, `Events/DevilSquareInitializer.cs:29-46`).
- **WebZen** `define.h` (`scratchpad/ds/u8/define.h`):
  - `:36-48`: G_V_97D = 0, G_V_99B = 2, G_V_S1_512 = 4 ... and this build is `GAME_VERSION G_V_S2_2`.
  - `:4858`: `MAP_INDEX_DEVILSQUARE = 9`. `:4815-4821`: `MAX_MAP` is 10 without `NEW_SKILL_FORSKYLAND` (Icarus), so map 9 is in the base.
    No flag turns Devil Square itself on. The server turns it on with the cfg key `DevilSquareEvent` (`Gamemain.cpp:3634`, default 0).
  - `:3038-3041`: `DEVILSQUARE_EXTEND_20050221` ("4 squares to 6") only `#if GAME_VERSION >= G_V_S1_512`.
    `:4796-4797, 4899-4900`: it adds `MAP_INDEX_DEVILSQUARE2 = 32` and raises `MAX_MAP` to 33.
  - `:3777`: `ADD_DEVILSQURE_FOR_MASTER_LEVEL_20071010` (square 7) is inside `MASTER_LEVEL_SYSTEM_20070912`.
  - `:2169`: `UPDATE_DEVIL_SQUARE_START_TIME_20070511` (a start-time list) only `#if GAME_VERSION >= G_V_S3_E1` (`:2147`).
- **How many squares**:

| build | squares | maps | source |
|---|---|---|---|
| 0.95d-0.99B (WZ base) | 4 | 9 | WZ `DevilSquareGround.h:24-32` (`MAX_DEVILSQUARE 4`) |
| Season 1 (G_V_S1_512) | 6 (5, 6 on map 32) | 9, 32 | `:27-28`; `DevilSquare.h:30` |
| Season 4+ (master level) | 7 (7 on map 32, master level only) | 9, 32 | `:24-25`; `DevilSquare.h:23, 55-57` |

### 2. Entry

#### 2.1 Charon (NPC 237)

- WZ `NpcTalk.h:11` `NPC_RUSIPHER 237` ("Lucifer"; the comment at `NpcTalk.cpp:308` says Charon). WZD `Monster.txt:322`
  names him "Charon". OM `Version095d/NpcInitialization.cs:55-62` gives him `NpcWindow.DevilSquare`.
- **Where he stands**: OM `Version095d/Maps/Noria.cs:32` has **171,105, SouthEast**. The repack has 171,104 dir 3
  (`WZD MonsterSetBase.txt:24`). MU2_BGFX already stands him at OM's 171,105 dir 4 (`source/world/noria/placements.json:73-79`,
  `tools/cook.py:2124-2127`, model `DevilNpc01`).
- Talk, `NpcRusipher NpcTalk.cpp:1005-1048`:
  - Event off in the cfg: nothing happens (`:1007-1008`).
  - No invitation (14/19) in the bag: ServerCmd 1,2 (`:1020-1023`). The client shows text 652: "The Devil Square is a place where
    warriors prove their courage ... Go to the Chaos Goblin in Noria with the Devil eye, Devil key, Jewel of Chaos and enough Zen."
    (MM `WSclient.cpp:8164-8172`, `legacy_id=652`).
  - Invitation and the gate is OPEN: 0x30 result 4 opens the Devil Square window (`:1025-1037`; MM `WSclient.cpp:6347-6349`).
  - Invitation and the gate is not open: ServerCmd 1,3, text 653 "You have come too soon ..." (`:1041-1045`).
  - There is **no PK check at Charon**. The PK refusal is at entry (2.2) and on the 0x91 time request (2006 flag, `protocol.cpp:18939-18947`).
- The window: MM `NewUIEnterDevilSquare.cpp:26-40, 246-263` lists the squares and lights only the one for your level. MM's bands
  are the EXTEND ones (15-130 ... 331-400; Magic Gladiator, Dark Lord, Rage Fighter 15-110 ...). It sends the square number and
  slot 0xFF (`:199`). That slot works on OM, which searches the bag (`OM/GameLogic/PlayerActions/MiniGames/EnterMiniGameAction.cs:180-207`);
  WZ subtracts 12 from the slot and would fail with result 1 (`protocol.cpp:18596, 18644-18651`).

#### 2.2 Entering: packet 0x90 (`GCReqmoveDevilSquare`, WZ `protocol.cpp:18587-18923`)

Checks, in this order:
1. A PK (PK level >= default+1) gets **result 6** (`:18614-18642`). This is `ADD_PK_USER_ENTER_LIMIT_EVENT_MAP_20060411`: on in this
   tree (`define.h:2702`), but dated 2006. Text 2043 "Killers are restricted to enter %s."
2. Bad slot or square number: result 1 (`:18644-18660`).
3. Gate not OPEN: **result 2**, "You've come too late to enter the Devil Square." (`:18662-18669`; text 678).
4. Square full (**10**): **result 5**, "Devil Square is full." (`:18671-18678`; text 679). The count only goes up (`:18922`) and is
   reset when the square closes (`DevilSquareGround.cpp:121-124`). A player who dies or leaves does not free his place.
5. The item is not 14/19: result 1, "Bring the Devil's invitation to enter." (`:18680-18694`; text 677).
6. An invitation of level 1-4 must equal the square number; **level 0 passes for any square** (`:18725-18749`).
7. The level band (0.97d branch `:18789-18832`). Magic Gladiator's level is first changed to `((L+1)/2)*3` (`:18714-18723`).
   Too high: **result 3** "You're underestimating yourself. Choose another square." Too low: **result 4** "If you wish to stay
   alive, choose another square." (`:18835-18850`; texts 686, 687).
8. Success: the invitation is deleted (`:18894-18896`), score, exp and money tallies are zeroed (`:18900-18902`), the square index is
   `gate - 58` (`:18906`), and the player warps to the square's gate (`:18919`). Gates 58-61 refuse anyone without this one-use
   pass (`m_bDevilSquareAuth`, `user.cpp:26917-26931`), so the warp list can't reach the squares.
- **The party is untouched.** Nothing in the entry code leaves or checks a party.

**Level bands**, normal / Magic Gladiator:

| square | gate | 0.97d (WZ `protocol.cpp:18789-18832`) | MG, real level (from `((L+1)/2)*3`) | Season 1+ EXTEND (`DevilSquare.h:48-58`) | OM 0.95d / S6 (`DevilSquareInitializer.cs:98-120`; S6 `:29-46`) |
|---:|---:|---|---|---|---|
| 1 | 58 | **10-99** | 7-66 | 15-130 / MG+DL 10-110 | same as EXTEND |
| 2 | 59 | 100-179 | 67-118 | 131-180 / 111-160 | same |
| 3 | 60 | 180-249 | 119-166 | 181-230 / 161-210 | same |
| 4 | 61 | **250+** (no ceiling) | 167+ | 231-280 / 211-260 | same |
| 5 | 111 | -- | -- | 281-330 / 261-310 | S6 same |
| 6 | 112 | -- | -- | 331-MAX / 311-MAX | S6 331-400 / 311-400 |
| 7 | 270 | -- | -- | master level only (`DevilSquare.cpp:512-517`) | S6 master class only |

- The MG column is my arithmetic on the formula (integer division), not a table in the source.
- At 0.97d only Magic Gladiator is special. Dark Lord joins him under `DARKLORD_WORK`, which is `#if GAME_VERSION >= G_V_99B` (`define.h:4042-4059`).
- The 0x91 time request answers 0 when the gate is open, minutes to open when closed, and `remaining + close time` while playing
  (`protocol.cpp:18951-18966`). Texts 643 "You can enter Devil Square now!!" and 644 "Devil Square will open in %d minutes."

#### 2.3 The ticket: Devil's Invitation 14/19

- Items (WZD `lang/kor/item(Kor).txt:364-366`): 14/17 "Devil's Eye", 14/18 "Devil's Key", 14/19 "Ticket to Devil Square", all 1x1.
  OM names 14/19 "Devil's Invitation" (`Version095d/Items/EventTicketItems.cs:29-31`), all 1x1, max level 4 (S6: 7).
- **Crafting** (Chaos Goblin's machine). Check `CheckDevilSquareItem MixSystem.cpp:742-860`, mix `DevilSquareEventChaosMix :862-1050`,
  dispatch `:1051-1130` (and the older path `protocol.cpp:13802-13850`):
  - The box holds exactly one Eye + one Key **of the same level** + one Jewel of Chaos (12/15). Other items: it is not a DS mix.
    Mixed levels: result 7. No Jewel of Chaos: result 7. More than two eyes/keys: result 3 (`:1072-1106`).
  - The player must be **level 10+**: result 4, text 649 "Must be over level 10 to combine the invitation to Devil Square." (`:1108-1114`).
  - Zen and rate at 0.97d (`:933-957`; rates are cfg reads with these defaults, `Gamemain.cpp:3612-3617`):

| level | zen | success |
|---:|---:|---:|
| +0 | 40 000 | 60% |
| +1 | 40 000 | 80% |
| +2 | 80 000 | 75% |
| +3 | 160 000 | 70% |
| +4 | 300 000 | 60% |
| other | result 8 "not a valid level" | -- |

  - Not enough zen: result 2 (`:980-988`). The zen is taken either way (`:1015`).
  - Success: one invitation **of the eyes' level** (`:1004-1013`). Failure: the whole box is cleared (`:1026-1030`).
  - Season 1 EXTEND (`:892-932`; cfg defaults `Gamemain.cpp:3602-3611`): +1..+7 cost 100k, 200k, 400k, 700k, 1.1M, 1.6M, 2M at
    75, 70, 65, 60, 55, 50, 45%. **OM uses the EXTEND prices** (100k ... 2M) but **80% below +5, 70% from +5**
    (`OM/GameLogic/PlayerActions/Craftings/DevilSquareTicketCrafting.cs:21-39`).
- **Eye and Key drops** (WZ):
  - They are in the list of things a monster can drop, like jewels and rings, at any monster level
    (`user.cpp:8262-8268`, old per-monster list; `MonsterItemMng.cpp:401-415`, per-level list).
  - A pick of Eye or Key is kept with `rand() % EyesOfDevilSquareDropRate == 0` (Key: `KeyOfDevilSquareDropRate`).
    The cfg default is **2** for both, so one in two (`Gamemain.cpp:3635-3636`; the globals' 2 and 3 at `:1103-1105` are overwritten).
  - Then **4 in 5** get a level by the monster's level (`user.cpp:8314`). The 0.97d table (`user.cpp:8346-8366`):
    **<=15 none, 16-40 +1, 41-54 +2, 55-66 +3, 67+ +4**. A level-0 one is never added ("from now on 0 items don't drop", `:8373`).
  - So a **+0 Eye or Key cannot drop at 0.97d**, and the +0 mix (and the "any square" +0 invitation) is dead in practice.
  - EXTEND table: <36 +1, <47 +2, <60 +3, <70 +4, <80 +5, else +6 (`user.cpp:8316-8344`).
  - The real chance a kill drops one is not a fixed number: it is the monster's general drop roll times the share of these entries
    in its list. A guess: well under 1%.
  - Caution: in this tree the Eye/Key block is wrapped in `DEVILSQARE_COMBINE_ITEM_DROP_20090114` ("overseas: let them drop
    again", `user.cpp:8279-8294`, `MonsterItemMng.cpp:450-469`), after the 2008 Season 4 market change took them out (`:442-448`).
    Pre-2008 it was surely unconditional (a guess from the comments).
  - The repack edited the C++ here: `MonsterItemMng.cpp:495, 524` say `devilitemlevel = 1; //DEVILSQUARE FIX (Normal es +0)`.
    The `user.cpp` copy still has the original 0 (`:8348`).
- **OM drops**: Eye and Key are "default drop groups" at **1% each** (`EventTicketItems.cs:34-73`, `Chance = 0.01`).
  0.95d: +1 for monster level 2-35, +2 36-46, +3 47-59, +4 60+ (`:29-30`). S6: +5 70-79, +6 80-89, +7 90+
  (`VersionSeasonSix/Items/EventTicketItems.cs:43-44`). OM's brackets are the EXTEND ones, not 0.97d's.
- OM S6 also sells Eyes and Keys: Lumen the Barmaid +1..+7, Caren the Barmaid +1..+2 (`OMI/VersionSeasonSix/MerchantStores.cs:438-452, 485-488`).
  That is S6 only.

### 3. Schedule (WZ 0.97d)

States: CLOSE 0, OPEN 1 (entry), PLAYING 2 (`DevilSquare.h:61-66`). One clock runs all squares (`DevilSquare.cpp:593-640`, 100 ms ticks).

- **When**: `CheckSync DevilSquare.cpp:843-883` (the 0.97d branch). It counts the minutes so that OPEN begins `OpenTime` minutes
  before an **odd hour**. Play starts on odd hours, every **2 hours** (`FOR_JAPAN` flips it to even hours, `:856-860`; off,
  `define.h:255`). If it is past :55 of an even hour, it skips to the next cycle (`:866-870`).
- Season 3 E1+ reads a start-time list (dat section 4) instead (`:308-321, 642-739`). The MM client's help texts show the Season 3
  table **3:00, 7:00, 11:00, 15:00, 19:00, 23:00** (every 4 hours; `legacy_id=2427, 2435, 2443`). OM starts every **240 min
  from midnight** with a 25-minute task (`OM/GameLogic/PlugIns/PeriodicTasks/DevilSquareStartConfiguration.cs:15-23`).
- **Lengths**: `CloseTime / OpenTime / PlayTime` minutes come from dat section 0 (`:258-266`). Code defaults: 2 / 2 / 10 (`:153-155`).
  Repack: 35 / 5 / 20 (`WZD DevilSquare.dat:4`). OM: entry 1 min, 30 s countdown, **20 min** play, exit 3 min
  (`Version095d/Events/DevilSquareInitializer.cs:135-137`; `OM/GameLogic/MiniGames/MiniGameContext.cs:706-790`).
  CloseTime only matters if the clock sync is off (`:415-419`).

| t (play at 13:00, OPEN 5) | what happens | source |
|---|---|---|
| 12:40, 12:45, 12:50 | CLOSE's last 15 min: every 5 min, lMsg 704 "[Devil Square] the gate opens in %d min" to everyone not in BC/CC | `DevilSquare.cpp:985-1019` |
| 12:54:30 | 0x92 type 0 (client 640 "You will enter Devil Square (%d seconds from now)", a 30 s counter) to everyone not in BC/CC | `:902-923`; MM `WSclient.cpp:8470-8494`, `CSEventMatch.cpp:155-159` |
| **12:55** | -> OPEN. lMsg 703 "[Devil Square] the gate has opened". Charon now opens the window | `:925-957` |
| 12:56 ... 12:59 | each minute: lMsg 705 "in %d min the [Devil Square] event starts"; stray monsters cleared | `:1075-1114` |
| 12:59:30 | 0x92 type 1 (client 641 "The gate of Devil Square will close down in %d seconds") to everyone not in BC/CC | `:1044-1066` |
| **13:00** | lMsg 707 "the [Devil Square] event has begun" to players in the squares; lMsg 708 "the gate is closed, latecomers next time" to everyone else. -> PLAYING: scores zeroed, **all monsters spawn** | `:1068-1072, 1343-1381, 455-474` |
| every minute | bosses due this minute spawn; dead monsters return as this minute's breed | `:1190-1193`; `DevilSquareGround.cpp:203-263, 284-316` |
| 13:05, 13:10, 13:15 | every 5 min, lMsg 706 "in %d min the [Devil Square] gate closes", to map 9 only | `:1170-1188` |
| 13:19:30 | 0x92 type 2 (client 642 "The gate of Devil Square is closing down (%d seconds remaining)") to map 9 | `:1139-1158` |
| **13:20** | -> CLOSE: monsters removed; **ranks, rewards and the result window**; square counts reset; next start synced | `:1160-1164, 398-435` |
| ~13:21 | everyone still on map 9 is moved to **gate 27** (Noria town, 171,108-177,117); floor items cleared. This repeats every minute of CLOSE | `:960-979`; WZD `Gate.txt:49`; OM `Version095d/Gates.cs:146` |

- There is **no minimum player count** and no early end. The event runs its full length even if every player died.
  OM ends early when everyone has left (`MiniGameContext.cs:414-422`).
- No running clock shows in the square: MM's `CSDevilSquareMatch::RenderMatchTimes` is empty (`CSEventMatch.cpp:212-215`).
  Only the 30-second counters (`CSEventMatch.cpp:103-163`) and the server notices show time.
- The message texts above are the Korean comments in the code; the server's `Message_kor.wtf` is encrypted. MM has the client's own texts.

### 4. The run

#### 4.1 Map 9: four rooms

Squares are told apart by the **start x of the spawn box** (`DevilSquare.cpp:1228-1261`): 119 DS1, 121 DS2, 49 DS3, 53 DS4 on map 9;
120 DS5, 122 DS6, 50 DS7 on map 32.

| square | arrival gate, box | monster box | source |
|---:|---|---|---|
| 1 | 58: 133,91 - 141,99 | 119,80 - 150,115 | OM `Version095d/Gates.cs:141`, `Maps/DevilSquare1.cs:50-54`; WZD `Gate.txt:104`, `MonsterSetBase.txt:295-296` |
| 2 | 59: 135,162 - 142,170 | 121,152 - 151,184 | OM `Gates.cs:142`, `DevilSquare2.cs:50-54`; WZD `Gate.txt:105` |
| 3 | 60: 62,150 - 70,158 | 49,138 - 79,173 | OM `Gates.cs:143`, `DevilSquare3.cs:53-57`; WZD `Gate.txt:106` |
| 4 | 61: 66,84 - 74,92 | 53,74 - 83,109 | OM `Gates.cs:144`, `DevilSquare4.cs:53-57`; WZD `Gate.txt:107` |
| 5 / 6 / 7 (S1+, S4+) | 111 / 112 / 270 on map 32, same boxes as 58 / 59 / 60 | 120,80-150,115 / 122,152-151,184 / 50,138-79,173 | OM `VersionSeasonSix/Gates.cs:208-210`, `Maps/DevilSquare5-7.cs` |

- The arrival box is **inside** the monster box. Players wait there during OPEN; monsters appear around them at the start.
- OM's 0.95d gates mark the arrival boxes as spawn gates (`, true`); S6's do not (`VersionSeasonSix/Gates.cs:204-210`). WZD gates carry level 10.
- The safe-zone map is Noria for every square (OM `DevilSquare1.cs:45`).

#### 4.2 Spawning over time (WZ mechanics)

- At play start, `SetMonster` puts a monster on **every map-9 spot** of `MonsterSetBase` (`DevilSquare.cpp:1208-1284`). The repack has
  two type-1 spots a square, **35 each**, so **70 a square** (`WZD MonsterSetBase.txt:291-308`: DS1 17+15, DS2 10+39, DS3 41+37, DS4 64+65).
- Each has `MaxRegenTime = 1000` ms (`:1279`). When one dies it is back after about **1 s** (the regen pass, `user.cpp:21644-21652`)
  at a random place in its box (a guess at `gObjMonsterRegen`), as a **random breed** from the dat rows whose minute window holds
  the current minute (`:1301-1341`; `GetMonsterType DevilSquareGround.cpp:284-316`). No row in the window: it does not come back.
  Up to 15 rows a square (`DevilSquareGround.h:69`).
- So the square always holds ~70 monsters, and what they are drifts with time. The first wave is whatever `MonsterSetBase` placed.
- **Bosses** (dat section 2) spawn once, at minute `start` of play, at a random place in their box, and are never reborn
  (`m_PosNum = -1`, `DevilSquareGround.cpp:203-263`; `DevilSquare.cpp:1310-1314`). Up to 15 a square.
- The monster's own `Monster.txt` regen time is ignored in the square.

#### 4.3 The waves

WebZen's 0.97d dat is lost. These are the two tables we have. Minutes are of play.

**OM 0.95d** (`OMI/Version095d/Maps/DevilSquare1-4.cs`; waves `Events/DevilSquareInitializer.cs:175-207`). Two breeds a wave, 35 each,
respawning while the wave lasts (`SpawnTrigger.AutomaticDuringWave`); the boss wave spawns once (`OnceAtWaveStart`).
Waves overlap (0-7, 5-14, 12-20), so up to 140 can be in a square at the overlaps.

| square | wave 1 (0-7) | wave 2 (5-14) "Lets continue with some stronger enemies ..." | wave 3 (12-20) "Still alive? I have more for you." | bosses (15) "Beware of the bosses! You have 5 minutes left." |
|---:|---|---|---|---|
| 1 | Cyclops 17, Skeleton Archer 15 | Hell Hound 5, Hell Spider 13 | Poison Bull 8, Shadow 36 | 5 Gorgon 18 |
| 2 | Dark Knight 10, Poison Shadow 39 | Cursed Wizard 34, Death Cow 41 | Death Knight 40, Death Gorgon 35 | 5 Hydra 49 |
| 3 | Death Cow 41, Devil 37 | Death Gorgon 35, Cursed Wizard 34 (S6: Shriker 180) | Orc Archer 64, Elite Orc 65 | 5 Metal Balrog 67 |
| 4 | Orc Archer 64, Elite Orc 65 | Bloody Wolf 60, Alquamos 69 (S6: Axe Warrior 294) | Iron Wheel 57, Queen Rainer 70 | 5 Cursed King 66 |
| 5 (S6) | Axe Warrior 294, Bloody Wolf 60 | Mega Crust 71, Death Angel 190 | Beam Knight 61, Drakan 73 | 5 Fire Golem 291 |
| 6 (S6) | Lizard Warrior 290, Iron Wheel 57 | Queen Rainer 70, Poison Golem 293 | Alpha Crust 74, Queen Bee 292 | 5 Shadow of Kundun 197 |
| 7 (S6) | Gigantis 434, Berserk 435 | Persona 438, Dreadfear 439 | Balram 436, Soram 437 | 5 Dark Elf 440 |

OM's 0.95d DS3 and DS4 notes say Shriker and Axe Warrior are swapped for Cursed Wizard and Alquamos "of comparable strength"
because the lower version lacks them (`DevilSquare3.cs:63-71`, `DevilSquare4.cs:62-71`). That is OM's choice.

**Repack dat** (`WZD DevilSquare.dat:7-69`), for comparison only:

| square | 0-7 | 5-14 | 12-20 | bosses (15) |
|---:|---|---|---|---|
| 1 | Thunder Lich 9, Hell Hound 5 | Dark Knight 10, Poison Shadow 39 | Ice Queen 25, Death Cow 41 | 5 Gorgon 18 + Golden Goblin 78 |
| 2 | Gorgon 18, Death Cow 41 | Death Gorgon 35, Bahamut 51 | Orc Archer 64, Elite Orc 65 | 5 Hydra 49 + Golden Titan 53 |
| 3 | Great Bahamut 38, Orc Archer 51 | Elite Orc 64, Elite Orc 65 | Mutant 62, Bloody Wolf 60 | 5 Metal Balrog 67 + Golden Dragon 79 |
| 4 | Orc Archer 64, Elite Orc 65 | Mutant 62, Bloody Wolf 60 | Metal Balrog 67, Phantom Knight 72 | 5 Cursed King 66 + Golden Lizard 80 |

- The repack's comments are wrong in places (51 is Great Bahamut, not "Bahamut" or "Orc Archer"; 38 is Balrog; 64 is Orc Archer).
- Both tables share the **time frame**: windows 0-7, 5-14, 12-20 and bosses at minute 15. That frame is likely WebZen's.
- The golden monsters are 0.99+ invasion breeds (Golden Dragon etc.); they are the repack's.
- OM's first wave always equals the repack's `MonsterSetBase` breeds, which is what WZ would really show in the first minutes.

#### 4.4 Rules inside

- **No PvP**: a player can't kill a player in a square (`ObjBaseAttack.cpp:401-408`). No trade (lMsg 711, `protocol.cpp:9100-9108`).
  No duel (lMsg 1204, `protocol.cpp:21149-21154`). No guild war requests (`:12775-12795`). No Chaos Castle entry from here
  (`:20304-20316`). No Kalima gate (`KalimaGate.cpp:115-120`). No `/move` (`MoveCommand.cpp:270-275`).
- **Death** (`user.cpp:9219` `gObjUserDie`, early return `:9300-9318`): no exp loss and no item drop. On respawn (`:22067-22074`):
  `DieProcDevilSquare` tells him "You failed. Your hunting score is %d; try again next time" (lMsg 709), sends the score to the
  ranking server, zeroes it (`DevilSquare.cpp:1385-1436`), and he is reborn in **Noria**. The invitation is spent: **a dead player is out**
  and gets no reward.
  - In that message a Dark Knight adds his class bonus first (`:1404-1419`, `Class == 1` only).
- **Logging in** on map 9 puts you in town: elves in Noria, others Lorencia (`user.cpp:3100-3115`).
- **Party**: kept. Party exp in the square is a copy of the normal party formula (`DevilSquare.cpp:1654-2216`; called from
  `user.cpp:13083-13089`); single exp likewise (`:1439-1651`, from `user.cpp:12804-12811`). The copies only add the run's exp tally
  (`m_nEventExp`). **No exp penalty** like Blood Castle's 50%.
- **Zen**: a kill's zen is not dropped. It is added to the top hitter's `m_nEventMoney` (`gObjMonster.cpp:3557-3570, 4801-4816`).
  At the end that tally is **overwritten** by the reward zen (`DevilSquareGround.cpp:625`), so kill zen is never paid. The rare 1-in-400
  zen pile is off here too (`gObjMonster.cpp:4826-4836`). OM's note agrees: "in the original servers, in DS drops no money!"
  (`OM/GameLogic/MiniGames/DevilSquareContext.cs:41`).
- **Items** drop as usual, except the Icarus-era set items, the Dark Breaker and Thunder Blade, the skill orbs 12/12-19, the Holy Spirit
  bow 4/17 and the Dragon Soul staff 5/9: those drop only **1 in 10** times (`gObjMonster.cpp:3204-3237, 4588-4606`, under `NEW_FORSKYLAND2`).
- **Max players**: 10 a square (`MAX_DEVILSQUARE_OBJCOUNT`, `DevilSquareGround.h:71-79`; `DEC_MAX_DEVILSQUARE_OBJCOUNT` on outside
  Thailand, `define.h:618-621`). OM: 10 (`DevilSquareInitializer.cs:138`).

### 5. Scoring and rewards

#### 5.1 Points (WZ)

- On each monster death, `gObjMonsterScoreDivision` (`DevilSquare.cpp:2219-2236`, called from `user.cpp:14536-14543`):
  - Only the **last hitter** scores (`gObjMonsterLastHitDamageUser gObjMonster.cpp:500-519`).
  - `score = (int)(his damage on it / its max HP x its level)`, then `x (his square index + 1)`.
  - So a solo kill of a level-28 Cyclops in DS1 is 28 points; a level-70 Orc Archer in DS4 is 280. Damage from party members
    is lost to the score unless they land the last hit.
- **Class bonus**: `m_BonusScoreTable` (`DevilSquare.cpp:157-175`): DS1-4 wizard 0/0/0/170, knight 20/90/120/400, elf 10/10/10/200,
  Magic Gladiator 0. It is added as `value / 100`, so +0..+4 points.
  - At the end it is added in `InsertObj` (`DevilSquareGround.cpp:328-348`) from the **ground's** table, which is only filled in the
    destructor (`:60-111`). The ground is a global, so that table is all zero: **the end bonus is always 0** (a WZ bug).
  - The dying knight's message does use the real table (`DevilSquare.cpp:1404-1419`).

#### 5.2 Ranking and rewards (WZ)

- At close, `CalcScore` (`DevilSquare.cpp:2248-2276`) takes every non-GM player **still on map 9**, per square, and sorts by score;
  **ties go to the lower level** (`DevilSquareGround.cpp:32-49`).
- Reward row by rank (`SendScore DevilSquareGround.cpp:350-689`), rows 0-3 from dat section 3:
  - **6 players or fewer**: at 0.97d rank 1 gets row 0, rank 2 row 1, **rank 3 and below row 3** (`count < 3`, `:374-386`).
    Row 2 is unused. EXTEND changes it to `count < 4`, so rank 3 gets row 2.
  - **7 or more**: rank 1 row 0; then `rank x 100 / players` **<= 30** row 1, **<= 50** row 2, else row 3 (`:388-411`).
  - Everyone ranked gets a row: there is no "no reward" rank.
- Each player gets the row's **exp** (with level-up, `:551-623`) and **zen** (capped at max zen, `:625-638`).
- Section 3 is loaded as `SetBonus(rank, exp, zen)` into a function whose parameters are named `(index, zen, exp)`
  (`DevilSquare.cpp:302-306`, `DevilSquareGround.cpp:179-183`). The two swaps cancel: the dat's exp column is paid as exp.
- **The values are dat-only.** The code has no defaults. OM's table (`Version095d/Events/DevilSquareInitializer.cs:47-83`):

| square | 1st | 2nd | 3rd | 4th row |
|---:|---|---|---|---|
| 1 | 6 000 exp, 30 000 zen | 4 000, 25 000 | 2 000, 20 000 | 1 000, 15 000 |
| 2 | 8 000, 40 000 | 6 000, 35 000 | 4 000, 30 000 | 2 000, 25 000 |
| 3 | 10 000, 50 000 | 8 000, 45 000 | 6 000, 40 000 | 4 000, 35 000 |
| 4 | 20 000, 60 000 | 10 000, 55 000 | 8 000, 50 000 | 6 000, 45 000 |
| 5 (S6) | 22 000, 70 000 | 20 000, 65 000 | 10 000, 60 000 | 8 000, 55 000 |
| 6 (S6) | 24 000, 80 000 | 22 000, 75 000 | 20 000, 70 000 | 10 000, 65 000 |
| 7 (S6) | 26 000, 150 000 | 24 000, 140 000 | 22 000, 120 000 | 20 000, 90 000 |

  The repack (`WZD DevilSquare.dat:71-91`) has DS1-4 at **exactly 100x** OM's numbers (600 000 exp / 3 000 000 zen for DS1 1st).
  So OM's table is very likely WebZen's original, and the repack multiplied it (an inference).
- Season 4.5 adds a per-square exp multiplier (dat section 5, `UPDATE_ADD_EVENT_MAP_EXP_RATE_20080320`, `DevilSquare.cpp:322-340`).

#### 5.3 The result window (0x93)

- Packet `PMSG_DEVILSQUARERESULT` (`protocol.h:3076-3093`): MyRank, Count, then rows of `{Name[10], TotalScore, BonusExp, BonusZen}`.
  Row 0 is **your own** line; rows 1-9 are the top 9 of your square (`DevilSquareGround.cpp:364-418, 666-683`). Count includes row 0.
- MM shows it as a message box (`CSEventMatch.cpp:196-326`, `CDevilSquareRankMsgBoxLayout`):
  - "Congratulations!" and text 648 "%s, Your bravery is proven in Devil Square."
  - Columns: rank, name, **Point**, **EXP**, **Reward** (zen); up to 10 rows; your row in orange.
  - Under them, "My Info": your rank, name, points, exp, zen.
- MM numbers its rows `i + 1` from row 0 and takes "my" row as `MyRank - 1`. That does not match WZ's layout (own row at 0).
  MuMain's renderer is a rewrite; how the original drew row 0 is unclear.
- The reward exp also shows as a normal exp gain (`GCKillPlayerExpSend(..., 0xffff, ...)`, `DevilSquareGround.cpp:640-659`).

#### 5.4 OpenMU's scoring (differs)

- **+GameLevel points a kill** to the killer (DS1 kill = 1 point), nothing else (`OM/GameLogic/MiniGames/DevilSquareContext.cs:33-43`).
- Rewards only for **ranks 1-4** (`MiniGameContext.cs:874-879`); rank 5+ gets nothing. Only players still in at the end ("finishers").
- **Bug**: finishers are sorted `OrderBy(state => state.Score)`, ascending, so the **lowest** score is ranked 1st (`DevilSquareContext.cs:59-63`).
- The window shows score, money and the exp reward of the rank.

### 6. Monsters

#### 6.1 Stats

WZD `Monster.txt` (repack data, "Revision 22 August, 2008", header `:6`), against OM 0.75/0.95d. Every breed in either wave table for
DS1-4. Columns: level, HP, damage, defence, attack rate, defence rate, attack range / view range, attack ms, regen s, resistances
(ice/poison/lightning/fire, as the four trailing columns).

| # | name (WZD) | WZD line | lvl | HP | dmg | def | AR | DR | atk/view | atk ms | regen s | res | OM differs |
|---:|---|---|---:|---:|---|---:|---:|---:|---|---:|---:|---|---|
| 17 | Cyclops | `:27` | 28 | 850 | 100-105 | 35 | 140 | 35 | 1/4 | 1600 | 5 | 2/0/0/0 | same (`OMI/Version075/Maps/Dungeon.cs:897`) |
| 15 | Skeleton Archer | `:25` | 34 | 1100 | 115-120 | 45 | 170 | 41 | 5/7 | 2000 | 5 | 0/0/0/0 | same (`Dungeon.cs:841`) |
| 5 | Hell Hound | `:18` | 38 | 1400 | 125-130 | 55 | 190 | 45 | 1/4 | 1200 | 5 | 0/0/0/3 | same (`Dungeon.cs:627`) |
| 13 | Hell Spider | `:24` | 40 | 1600 | 130-135 | 60 | 200 | 47 | 4/7 | 1600 | 5 | 3/0/0/0 | same (`Dungeon.cs:811`) |
| 9 | Thunder Lich | `:20` | 44 | 2000 | 140-145 | 70 | 220 | 55 | 4/7 | 2200 | 5 | 0/0/3/0 | same (`Dungeon.cs:687`) |
| 8 | Poison Bull | `:19` | 46 | 2500 | 145-150 | 75 | 230 | 61 | 1/5 | 1400 | 5 | 6/0/0/0 | same (`Dungeon.cs:656`) |
| 36 | Shadow | `:55` | 47 | 2800 | 148-153 | 78 | 235 | 67 | 1/5 | 1400 | 5 | 3/3/3/5 | same (`LostTower.cs:726`) |
| 10 | Dark Knight | `:21` | 48 | 3000 | 150-155 | 80 | 240 | 70 | 1/5 | 1400 | 5 | 3/3/3/3 | same (`Dungeon.cs:717`) |
| 39 | Poison Shadow | `:58` | 50 | 3500 | 155-160 | 85 | 250 | 73 | 1/5 | 1400 | 5 | 6/4/4/6 | same (`LostTower.cs:824`) |
| 25 | Ice Queen | `:38` | 52 | 4000 | 155-165 | 90 | 260 | 76 | 4/7 | 1400 | 10 | 4/5/4/4 | same (`Devias.cs:256`) |
| 34 | Cursed Wizard | `:53` | 54 | 4000 | 160-170 | 95 | 270 | 80 | 4/7 | 2000 | 5 | 5/5/7/7 | same (`LostTower.cs:660`) |
| 18 | Gorgon | `:28` | 55 | 6000 | 165-175 | 100 | 275 | 82 | 1/7 | 1600 | 5 | 6/6/6/6 | same (`Dungeon.cs:926`) |
| 41 | Death Cow | `:60` | 57 | 4500 | 170-180 | 110 | 285 | 85 | 1/7 | 1600 | 5 | 5/5/5/7 | same (`LostTower.cs:890`) |
| 37 | Devil | `:56` | 60 | 5000 | 180-195 | 115 | 300 | 88 | 4/7 | 2000 | 5 | 5/5/5/7 | same (`LostTower.cs:758`) |
| 40 | Death Knight | `:59` | 62 | 5500 | 190-200 | 120 | 310 | 91 | 1/7 | 1800 | 5 | 6/6/6/7 | same (`LostTower.cs:858`) |
| 35 | Death Gorgon | `:54` | 64 | 6000 | 200-210 | 130 | 320 | 94 | 1/7 | 2000 | 5 | 6/6/6/8 | same (`LostTower.cs:693`) |
| 38 | Balrog | `:57` | 66 | 9000 | 220-240 | 160 | 330 | 99 | 2/7 | 1600 | 10 | 10/10/10/15 | same (`LostTower.cs:791`) |
| 51 | Great Bahamut | `:82` | 66 | 7000 | 210-230 | 150 | 330 | 98 | 2/7 | 1600 | 5 | 6/6/6/6 | same (`Atlans.cs:600`) |
| 64 | Orc Archer | `:68` | 70 | 10000 | 220-250 | 110 | 350 | 115 | 4/7 | 1600 | 10 | 7/7/7/7 | def **170** (`Version095d/Maps/DevilSquare3.cs:85`) |
| 62 | Mutant | `:92` | 72 | 10000 | 250-280 | 190 | 365 | 120 | 1/7 | 1400 | 5 | 8/8/8/8 | same (`Tarkan.cs:424`) |
| 65 | Elite Orc | `:69` | 74 | 14000 | 260-290 | 120 | 400 | 125 | 1/7 | 2000 | 10 | 8/8/8/8 | def **190**; respawn 100 s (`DevilSquare3.cs:117`) |
| 69 | Alquamos | `:100` | 75 | 11500 | 255-290 | 195 | 385 | 125 | 5/5 | 1400 | 5 | 9/9/11/9 | same (`Icarus.cs:127`) |
| 60 | Bloody Wolf | `:90` | 76 | 13500 | 260-300 | 100 | 410 | 130 | 2/7 | 1400 | 5 | 8/8/8/8 | def **200** (`Tarkan.cs:359`) |
| 67 | Metal Balrog | `:71` | 77 | 26000 | 300-360 | 100 | 385 | 150 | 2/7 | 1600 | 10 | 9/9/9/9 | def **220**; respawn 200 s (`DevilSquare3.cs:150`) |
| 57 | Iron Wheel | `:87` | 80 | 12000 | 180-230 | 115 | 446 | 150 | 4/7 | 1400 | 5 | 9/9/9/9 | HP **17000**, dmg **280-330**, def **215** (`Tarkan.cs:261`) |
| 70 | Queen Rainer | `:101` | 82 | 19000 | 305-350 | 230 | 475 | 160 | 3/5 | 1400 | 5 | 9/11/12/9 | same (`Icarus.cs:160`) |
| 49 | Hydra | `:80` | **108** | **89000** | 1050-1510 | 1200 | 430 | 125 | 4/7 | 1400 | 1800 | 12/12/12/12 | lvl **74**, HP **19000**, dmg **250-310**, def **200** (`Atlans.cs:535`) |
| 66 | Cursed King | `:70` | **108** | **89000** | 1000-1570 | 1200 | 2525 | 200 | 4/7 | 1400 | 1800 | 17/17/17/17 | lvl **86**, HP **38000**, dmg **500-570**, def **350**, AR **525**; respawn 70 s (`DevilSquare4.cs:85`) |
| 72 | Phantom Knight | `:103` | 98 | 41000 | 560-610 | 425 | 690 | 270 | 2/5 | 1400 | 5 | 14/14/16/14 | lvl 96 (`Icarus.cs:225`) |
| 78 | Golden Goblin | `:282` | 20 | 10000 | 2000-4000 | 2600 | 9500 | 100 | 3/7 | 2000 | 20 | -- | not in OM 0.75/0.95d (repack boss) |
| 53 | Golden Titan | `:284` | 53 | 50000 | 4000-6000 | 4500 | 10000 | 100 | 4/7 | 2000 | 20 | -- | same |
| 79 | Golden Dragon | `:287` | 80 | 100000 | 6000-8000 | 5000 | 12000 | 100 | 3/7 | 2000 | 5 | -- | same |
| 80 | Golden Lizard King | `:288` | 83 | 200000 | 8000-10000 | 6500 | 15000 | 100 | 4/7 | 2000 | 15 | -- | same |

- OM paths are under `OMI/Version075/Maps/` unless they say `Version095d`. The table was built by script (`scratchpad/ds/mktable.py`).
- **Hydra and Cursed King in WZD are the same edited row** (108, 89 000 HP, def 1200, 1800 s regen). They read as the repack's own
  bosses. OM's 74 / 19 000 and 86 / 38 000 fit MU's curve; prefer OM for these two (a judgment).
- 64, 65, 66, 67 are Devil Square's own breeds: OM defines them in the DS map files (`DevilSquare3.cs:85-150`, `DevilSquare4.cs:85`).
  OM 0.95d marks 65, 66, 67 with the monster skill (`AttackSkill = MonsterSkill`).
- The square forces respawn to 1 s, so the regen column does not apply inside (4.2).
- DS5-7 (S6) breeds: WZD has 60 (above), 61 Beam Knight `:91`, 71 Mega Crust `:102`, 73 Drakan `:104`, 74 Alpha Crust `:105`,
  190 Death Angel `:241`, 197 Shadow of Kundun `:248`. It has **no rows** for 290-294 or 434-440 (Season 4.5+); OM S6 defines them
  (`VersionSeasonSix/Maps/DevilSquare7.cs` for 434-440).

### 7. Where OM, WZ and S6 disagree

| point | WZ 0.97d | OM | S6 / later WZ | notes |
|---|---|---|---|---|
| version | in the base build, before Icarus | 0.95d | -- | OM 0.75 has none |
| squares | 4, map 9 | 0.95d 4; S6 7 | 6 from S1, 7 from S4 (map 32) | `DevilSquareGround.h:24-32` |
| level bands | 10-99, 100-179, 180-249, 250+ | 15-130, 131-180, 181-230, 231-280 | as OM | MU2_BGFX's herald already uses OM's (`src/game/ui/herald.cpp:69`) |
| Magic Gladiator | level x1.5 (`((L+1)/2)*3`) against the normal band | own band, 10-110 ... | own band (+ Dark Lord; client + Rage Fighter) | client MM says 15-110 for DS1 |
| +0 invitation | enters any square | must match level | -- | moot: +0 eyes don't drop at 0.97d |
| ticket price / rate | 40k-300k, 80-60% | 100k-2M, 80% (<+5), 70% | 100k-2M, 75-45% | |
| Eye/Key levels by monster | <=15 none, 16-40, 41-54, 55-66, 67+ | 2-35, 36-46, 47-59, 60+ (EXTEND's) | +5..+7 above | |
| Eye/Key chance | in the drop list, kept 1 in 2, levelled 4 in 5 | flat 1% each | sold by barmaids in OM S6 | |
| schedule | odd hours, every 2 h | every 4 h from 00:00 | S3: 3, 7, 11, 15, 19, 23 h (MM texts) | |
| entry / play | dat; code 2 + 10 min; repack 5 + 20 | 1 min + 30 s, 20 min | dat start list | |
| monsters | 70 fixed spots, breed by minute, 1 s respawn | 35 a breed a wave, waves overlap (up to 140) | -- | |
| waves | dat (lost) | 3 waves + 5 bosses at 15 | repack dat differs, adds golden bosses | time frame agrees |
| score | last hit's damage share x level x square | +square number a kill | -- | |
| class bonus | coded, but always 0 at the end (bug) | none | -- | |
| reward ranks | rows by rank, <=6 players special; everyone gets a row | ranks 1-4 only; ascending sort bug | EXTEND: rank 3 gets row 2 | |
| reward values | dat | DS1 6 000 / 30 000 ... | repack 100x OM | |
| end | result, then moved to Noria a minute later | exit 3 min, then safe zone | -- | |
| early end | never | when everyone has left | -- | |
| PK at entry | 2006 flag: refused | refused (PK 1st stage+) | party PK rule 2007 | |
| players | 10 (Thailand 20) | 10 | -- | |
| Hydra / Cursed King | WZD 108 / 89 000 (repack edit) | 74 / 19 000; 86 / 38 000 | -- | |

### Unclear (Part A)

- **WebZen's own `DevilSquare.dat`**: OPEN and PLAY minutes, the wave rows, bosses and reward rows. OM's tables are the best candidate;
  the 100x match with the repack's rewards supports them, and the shared 0-7 / 5-14 / 12-20 / 15 frame too. Unproven.
- **Which 0.9x version first had Devil Square.** WZ only shows it predates Icarus; OM files it under 0.95d.
- **The real per-kill Eye/Key chance at 0.97d**: it depends on the general drop roll and the drop list's size, and the 2008/2009
  flags show the code changed around it. Pre-2008 code is a guess.
- **The original rank window** for the WZ row-0 layout. MuMain's rewrite reads the packet another way.
- **Monster count a square**: 35 + 35 comes from the repack's `MonsterSetBase` and OM. WebZen's real spot counts are not in the source.
- **Where a reborn monster appears**: assumed random in its spot box (not traced into `gObjMonsterRegen`).
- **The Noria gate 27 move timing** after the end: it fires on the first minute tick of CLOSE, about a minute after the window,
  unless the sync leaves `m_iRemainTime <= 1` (then the seconds path runs and the move waits for the next open; an edge case).
- **The server notice texts** (lMsg 703-711) are paraphrased from the code's Korean comments; the `.wtf` files are encrypted.
- **Charon's tile**: 171,105 (OM) or 171,104 (repack). MU2_BGFX uses OM's.

---

## Part B: the data and the look (Devil Square)

Research notes, 2026-10-10. No repo file was edited, no cook, no window. Devil Square is server map **9** (`WD_9DEVILSQUARE`, `MM/World/MapInfra/MapManager.h:18`; `InDevilSquare()` is `WorldActive == 9` alone, `MapManager.cpp:1598-1601`). Every map folder is world + 1, so it loads **`D/World10` and `D/Object10`** through the generic arm (`MapManager.cpp:1100-1126` objects, `:1205-1227` terrain). There is no `case WD_9DEVILSQUARE` in `CMapManager::Load`'s model switch (`:43-887`), so no extra models load.

**Squares 5-7 (server map 32) use the same files.** `LoadWorld` turns 32 into 9 before anything loads: `if (Map == 32 && WorldActive == 32) Map = WorldActive = 9;` (`MapManager.cpp:1178-1181`). D has no World33/Object33. 32 is only a name lookup after that (`:1746-1749`). OpenMU's Season Six DS5-7 reuse DS1-3's arenas and gates (`OM/Persistence/Initialization/VersionSeasonSix/Gates.cs:203-210`).

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- S = `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/4de96907-2ebb-4791-8d64-614ff2fd7481/scratchpad/ds/b/`

Scratch outputs in S:
- `devilsquare/`: `pipeline/terrain.py D/World10 devilsquare S 10`. Log in `terrain.log`. `att.npy` is the raw attribute grid.
- `stats.log` (from `stats.py`): attribute values, walkable islands, per-square boxes, objects per square.
- `prev_attr_obj_height_light.png`: attributes with placements and the four arrival gates (red) | height | light. y is flipped (north up). `prev_arenas_crop.png` is the four arenas enlarged.
- `tex/`, `sheet_textures.png`: every World10/Object10 sheet, the ticket sheets and `Interface/devilsquare.OZT`, decoded.
- `obj/`: all 7 Object10 .bmd through MuExtract (export-obj, export-rig --actions=all). Summary in `objinfo.log`. `models_object10.png` is a cheap software render.
- `item/`, `models_tickets.png`: Item/Devil00-02. `mon/`: Monster49 (Cursed King).
- `arena.py`, `create_blocks.txt` (every DS breed's CreateMonster block, pulled from MM).

### The one thing to know first

**Devil Square is four bare stone yards in a sea of flat, blocked stone, under teal light and steady rain, with stone soldiers lining two sides of every yard. There is almost nothing else.**
- One map, four arenas, each about 32 × 36 tiles. Each square level is sent to one of them by its gate (§1.1).
- Only one tile slot is used: `TileGrass01`, on all 65,536 tiles. No blend layer.
- The light map is one flat colour, **(63, 103, 115)**, a dark teal, on 98% of tiles. Only DS1's arena has painted shading (§1.1).
- Only 7 object kinds, 564 placements. 391 of them are two kinds of armoured guard statue (Object03, Object04) standing in ranks on the west and east sides, facing in.
- MuMain adds **rain** (200 drops, the full leaf pool), a looped `aRain.wav`, and **silent lightning**: one-frame blue-white terrain flashes near the hero for 1 s in every 4 s (§2).
- Clear and fog are black (the default arm). No music: Data/Music has `devil_square_intro.mp3` and `devil_square_outro.mp3`, but MuMain never plays them.

---

### 1. Raw data inventory

#### 1.1 World10 (`D/World10`, 18 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain10.map | 196610 | yes | 3 planes. Layer 1 is slot 0 everywhere, layer 2 is 255 (none) everywhere, alpha 0 everywhere |
| EncTerrain10.att | 65540 | yes | attributes (below) |
| Terrain.att, Terrain10.att | 65539 each | no | the old unencrypted pair; the client reads `EncTerrain10.att` (`MapManager.cpp:1258`) |
| EncTerrain10.obj | 16924 | yes | **564 placements, 7 kinds** |
| TerrainHeight.OZB | 66620 | yes | header says 1078, read at 1080 |
| TerrainLight.OZJ | 4153 | yes | flat teal (63,103,115) on 64,225 tiles |
| TileGrass01.OZJ | 58659 | slot 0, **100%** | grey-green worn, pitted stone. The whole floor |
| TileGrass02, TileGround01/03, TileRock01-04, TileWood01 .OZJ | | loaded, **unused** | blue-green rock, brown earth, dark stone, pale sandstone, gravel: no tile wears them |
| leaf02.OZJ | 4501 | yes | a 16² dot, unused here (no leaves) |
| rain03.OZT | 4144 | **yes, for every world** | the rain splash ring, BITMAP_RAIN_CIRCLE+1, always read from World10 (`MapManager.cpp:1494-1495`) |

- **No TileGrass .OZT ships here.** MuMain sows grass only where `BITMAP_MAPGRASS + slot` exists (`MM/Render/Terrain/ZzzLodTerrain.cpp:2081-2088`). So no grass.
- No TileWater01, TileGround02 or minimap. The missing slots are harmless; no tile wears them.

terrain.py (`S/terrain.log`):

```
height     0.00 to 3.83 m over 256 m; 62,349 tiles (95%) are byte 149 = 2.235 m (flat)
tiles      1 slot in use: 0 TileGrass01 100.0%
walkable   7.1% of the map (4,642 tiles), 0.5% safe (360 tiles)
light      mean [63, 103, 115]
objects    564 placed, 7 kinds: Object03 200, Object04 191, Object05 55, Object01 52, Object06 44, Object02 12, Object07 10
```

**Attributes.** Only four values occur (`S/stats.log`):

| value | meaning | tiles | where |
|---|---|---:|---|
| 0 | open | 4642 | the four arena floors, nothing else |
| 4 | NoMove | 60533 | everything outside the arenas, plus a few rubble islands inside them |
| 5 | Safe + NoMove | 360 | a blob at x 11-34, y 8-28, and one tile at 107,77. **Unreachable**: there is no waiting room |
| 6 | Character + NoMove | 1 | |

- **No NoGround anywhere.** The plain outside the arenas is real ground: drawn, flat at 2.235 m, NoMove. MU draws it to the horizon (into black fog).
- No TW_ACTION or water bits.

**The four arenas.** Each is one walkable island. They are not copies: fewer than 7 of ~140 placements per arena match at the same offsets (`arena.py`).

| arena | walkable box | tiles | floor height (5/50/95 pct) | used by | arrival gate (OM) |
|---|---|---:|---|---|---|
| A | x 118-151, y 78-115 | 1150 | 2.24 / 2.24 / 2.26 m (max 3.03) | DS1, DS5 | 58 / 111: 133,91 - 141,99 |
| B | x 119-152, y 149-184 | 1142 | 2.24 / 2.24 / 2.26 m (min 2.00, max 3.08) | DS2, DS6 | 59 / 112: 135,162 - 142,170 |
| C | x 49-81, y 137-173 | 1175 | 2.24 / 2.24 / 2.25 m (max 3.04) | DS3, DS7 | 60 / 270: 62,150 - 70,158 |
| D | x 53-84, y 72-109 | 1173 | 2.24 / 2.24 / 2.36 m (max 2.68) | DS4 | 61: 66,84 - 74,92 |

- Gates are `OM/Version095d/Gates.cs:140-144` and `VersionSeasonSix/Gates.cs:203-210`. Every gate tile is attribute 0 at 2.235 m: a flat 9×9 box in the middle of its arena.
- OM's monster spawn boxes match the arenas: DS1 119-150 × 80-115 (`Version095d/Maps/DevilSquare1.cs:51-54`), DS2 121-151 × 152-184 (`DevilSquare2.cs:50-53`), DS3 49-79 × 138-173 (`DevilSquare3.cs:53-56`), DS4 53-83 × 74-109 (`DevilSquare4.cs:53-56`). DS5-7 use the DS1-3 boxes (`VersionSeasonSix/Maps/DevilSquare5-7.cs:50-55`).
- Inside each box 22-48 tiles are NoMove. They are notches in the corners and small islands under rubble blocks (Object05-07).
- **Height:** each arena sits in a raised rim. The south and east edges rise to **3.83 m** (byte 255), about 1.6 m over the floor. The north and west rims are lower. Everything else is 2.235 m.

**Light map** (`S/arena.py`):

| zone | RGB | note |
|---|---|---|
| everything | 63, 103, 115 | flat dark teal |
| arena A (DS1) and its ring, x 96-159, y 64-127 | 0,7,16 to 231,255,255 | 1,311 painted tiles: speckled highlights along the walls and a few dark pools west of the arena (e.g. 106-110, 100-102) |
| arenas B, C, D | 63, 103, 115 | not painted at all |

Arena A's floor mean is (64,104,116), so the painting barely changes the average. It reads as an unfinished pass on one arena.

**Tile sheet.** One, and it is not grass: `TileGrass01` is grey-green worn stone with pits and stains. Recipe: flagstone or rock, never grass. `GRASS_BY_MAP[9] = []` must be said outright (`pipeline/terrain.py:183-200`). Otherwise the slot's name sows a lawn, as the Dungeon and Lost Tower rows explain.

#### 1.2 Object10 (`D/Object10`, 21 files)

- 7 models: Object01-07.bmd.
- 14 sheets: bacc00 (dark blue-grey stone blocks), bacc01 (pale grey-green plaster stone), kni00/kni01 (bronze plate, Object03), upper_11, lower_11, head_11, gloves_11, boots_11 (silver-blue plate), shield06 (blue and gold shield), Javelins03 (spear head), guard_hair.ozt (a pink plume, alpha), hide (2×2).
- Object10's kni00/kni01 differ in bytes from `D/Monster/kni00-01` (the BC Orc's). Its `*_11` sheets differ from `D/Player/*_11`. `shield06.OZJ` is byte-identical to `D/Item/shield06.OZJ`.

Type = model index - 1 (`MapManager.cpp:1121-1123`).

| type | model | placed | tris | rig | sheets | what it is |
|---:|---|---:|---:|---|---|---|
| 0 | Object01 | 52 | 234 | static | bacc00 | **stepped wall section**, 5.9 × 6.8 × 4.1 m. Lines the north and west outer rims, yaw 0 or 90 |
| 1 | Object02 | 12 | 108 | static | bacc00 | **flat brick wall panel**, 6.2 × 5.8 × 6.7 m. Arena corners (3 per arena) |
| 2 | Object03 | **200** | 599 | 38 bones, 1 action × 4 keys | kni00, kni01 | **bronze knight statue** with spear and shield, 4 m tall. On the west side of each arena, yaw 30-120, facing in (east). Rain splashes on its shoulders (§2) |
| 3 | Object04 | **191** | 748 | 45 bones, 1 action × 4 keys | upper/lower/head/gloves/boots_11, shield06, Javelins03, guard_hair, hide | **silver-blue guard** with javelin, kite shield and plume, 2.7 m tall. On the east side, yaw -90, facing in (west) |
| 4 | Object05 | 55 | 12 | static | bacc00 | a stone block (1.2 × 0.6 × 0.7 m), tipped at odd angles: rubble |
| 5 | Object06 | 44 | 28 | static | bacc00, bacc01 | a broken stone block: rubble |
| 6 | Object07 | 10 | 14 | static | bacc00, bacc01 | a smaller rubble chunk |

- Every placement has scale 1.0. All but three stand on NoMove tiles.
- Per arena: about 13 Object01, 3 Object02, 50 Object03, 48 Object04, 10-16 Object05, 7-14 Object06, 2-4 Object07 (`S/stats.log`).
- **Animated kinds:** Object03 and Object04. Each has one 4-key action. MuMain plays every world object's action at `o->Velocity` (`MM/Engine/Object/ZzzObject.cpp:3725-3747`). There is no DS speed override.
- Object03 and Object04 are figures with Bip01 skeletons, not monsters. They never move or fight.
- **Small enough to build every kind.** Two figures, two walls and three rubble pieces.

---

### 2. MuMain's special cases for Devil Square

The greps covered `WD_9DEVILSQUARE`, `InDevilSquare` (31 hits), `WorldActive == 9`, `MODEL_NPC_DEVILSQUARE` and every DS breed. Everything that changes the look is below.

**Frame, fog, camera**
- Clear and fog are black: `SetWorldClearColor` has no DS arm and falls to the default (`MM/Scenes/SceneManager.cpp:371-402`).
- No sky, no camera arm, no grass (§1.1).
- No music. `ManageBackgroundMusic` has no DS arm (`SceneManager.cpp:989-1130`). The two DS mp3s are referenced nowhere in MM.

**Rain** (`MM/Render/Effects/ZzzEffectFireLeave.cpp`)
- `RequireLeavesEffect` includes `InDevilSquare()` (`MM/Scenes/MainScene.cpp:78-84`).
- The pool is the full **MAX_LEAVES = 200** in DS, against 80 elsewhere (`ZzzEffectFireLeave.cpp:429`; `MM/Core/Globals/_define.h:461`).
- `CreateDevilSquareRain` (`:121-170`): every drop starts at hero ± (-800..799, -500..899) units, 300-499 up. Tilt 20-49° about x, plus `RainAngle`. Falls at `-(rand 0-39 + RainSpeed)` a frame; `RainSpeed = sin(WT·0.001)·10 + 30` (`:459-462`).
- `MoveDevilSquareRain` (`:322-345`): on hitting the ground the drop dies. One in four leaves a **BITMAP_RAIN_CIRCLE** ripple.
- Each drop is a `RenderPlane3D(1, 20)` streak (`:584-592`).
- It never stops. There is no RainTarget ramp for DS (`:431-457`).

**Lightning, the only light change** (`MoveObject`, `ZzzObject.cpp:3674-3688`)
- When `WorldTime % 4000 < 1000` (1 s in every 4 s), each visible object rolls 1 in 100 a frame.
- A hit calls `AddTerrainLight` at hero ± 600 units (± 6 tiles), radius 12 tiles, colour `L·(0.2, 0.3, 0.5)` with L 0.4-1.5.
- The light falls off linearly to the edge (`MM/Render/Terrain/ZzzLodTerrain.cpp:950-982`). It lasts **one frame**: `InitTerrainLight` copies the baked light back every frame (`ZzzLodTerrain.cpp:3238-3250`, called from `MainScene.cpp:198-200`).
- With ~100 objects in view that is roughly one flash a frame for a second: a flickering blue storm-light, then three dark seconds.
- The thunder sound is **commented out** (`ZzzObject.cpp:3685`). The lightning is silent.
- The same function plays `SOUND_RAIN01` looped every call (`:3687`). That is `Data/Sound/aRain.wav` (`MM/Engine/Object/ZzzOpenData.cpp:4732`). `StopInactiveAmbientSounds` keeps it alive in DS (`SceneManager.cpp:936-939`).

**Rain on bodies**
- **Object03** (type 2): a BITMAP_RAIN_CIRCLE+1 splash at bones 23 and 31 (R and L UpperArm), offset (-15,0,0). Each 1 in 4 a frame, plus one at bone 23 every frame (`RenderObjectVisual`, `ZzzObject.cpp:3068-3087`). Rain bounces off the bronze knights' shoulders.
- **Every MODEL_PLAYER character** (the hero, other players, and the player-model monsters Skeleton Archer and Cursed Wizard): the same splash at bones 26 and 35, 1 in 4 a frame each (`MM/Engine/Object/ZzzCharacter.cpp:5703-5726`). On the player rig those are R and L UpperArm (`MU2_BGFX/source/players/rig/player.rig.json`).
- The splash particle: LifeTime 20, scale 0.8-1.3, drifting ±5 units (`MM/Render/Effects/ZzzEffectParticle.cpp:704-712`, moved at `:4908-4915`). Texture `World10/rain03.tga`.

**Object visuals.** Nothing else. There is no CreateFire, no BlendMesh and no hidden type for Object10. Objects take the teal TerrainLight at their feet (`ZzzObject.cpp:230-242`).

**Monster tweaks that apply only in DS**
- Cursed Wizard (34): scale **1.0** in DS, not SetCharacterScale's 0.9 (`ZzzCharacter.cpp:13926-13930`).
- Ice Queen: a blue BodyLight (0, 0.3, 1) in DS (`ZzzObject.cpp:494-499`). No square spawns her (dead code here).
- `CreateBalgasBarrackMonster` and `CreateCryWolf1stMonster` accept DS as well as their home maps (`MM/GameLogic/Quests/GM3rdChangeUp.cpp:396-397`, `MM/World/GameMaps/GMCrywolf1st.cpp:577-578`). That is how DS7's Balram, Soram and Dark Elf get made (§3).

**Event UI** (sounds and art in §6).
- Countdown: `CSBaseMatch::RenderTime` writes text 640+ (DS enter/start/close) at x 10, y H-70 in (128,128,255) on a half-black band, for 30 s (`MM/GameLogic/Events/CSEventMatch.cpp:23, 96-160`).
- `CSDevilSquareMatch::RenderMatchTimes` and `SetMatchGameCommand` are empty (`CSEventMatch.cpp:212-220`). No timer or kill counter shows during the fight.
- Result: `SetMatchResult` opens `CDevilSquareRankMsgBox` (`:196-210`). `RenderMatchResult` (`:222-320`) writes "Congratulations" and "%s your bravery is proven", then up to 10 rows of rank, name, points, EXP, zen. Your own row is (200,120,0), others yellow; your rank repeats below at row 11.

---

### 3. Monsters of Devil Square

Spawns are OpenMU's (Part A owns counts and waves): 35 a breed per wave, the boss 5 at wave start (`OM/Version095d/Maps/DevilSquare1.cs:55-65` etc.). OM's 0.95d DS3 and DS4 fall back to Cursed Wizard and Alquamos when Shriker (180) or Axe Warrior (294) is missing (`DevilSquare3.cs:63-71`, `DevilSquare4.cs:63-71`).

Body file: `D/Monster/Monster{MONSTER_MODEL+1:02}.bmd` (`MM/Engine/Object/ZzzOpenData.cpp:2549-2561`).

**mu.db** is `MU2_BGFX/source/mu.db` (the root `mu.db` is 0 bytes). Read with `immutable=1`.
- `monster_spawns` has rows for maps 0-4, 7, 8, 10, 11. **None for 9 or 32.**
- `gates` has no map 9 or 32 row.
- `monster_kinds` (106 rows) has every DS1-DS2 breed and 57, 60, 61, 69-71, 73, 74. It **lacks 64, 65, 66, 67, 180, 190, 197, 290-294, 434-440**.
- `npc_spawns` has no Charon row. He comes from `source/world/noria/placements.json`.

#### 3.1 DS1-DS4 (0.95d)

| # | name | square / wave | CreateMonster | body | scale, weapons | MU2_BGFX |
|---:|---|---|---|---|---|---|
| 17 | Cyclops | 1 / 1 | `ZzzCharacter.cpp:14061-14068` | Monster11 | 1.0, Crescent Axe | **Cyclops01** |
| 15 | Skeleton Archer | 1 / 1 | `:14209-14217` | player model, SubType SKELETON2 | 1.1, Elven Bow | **SkeletonArcher** |
| 5 | Hell Hound | 1 / 2 | `:14121-14140` | Monster02 | 1.1, Falchion + Plate Shield, HiddenMesh 1 | **HellHound01** |
| 13 | Hell Spider | 1 / 2 | `:14113-14119` | Monster09 | 1.1, Serpent Staff | **HellSpider01** |
| 8 | Poison Bull | 1 / 3 | `:14071-14098` | Monster01 | 1.0, Great Scythe, poison debuff look | **PoisonBull01** |
| 36 | Shadow | 1 / 3 | `:13883-13888` | Monster29 | 1.2 | **Shadow01** |
| 18 | Gorgon | 1 / boss | `:14046-14054` | Monster12 | 1.5, Gorgon Staff, BlendMesh 1 | **Gorgon01** |
| 10 | Dark Knight | 2 / 1 | `:14149-14156` | Monster04 | 0.8, Double Blade | **DarkKnight01** |
| 39 | Poison Shadow | 2 / 1 | `:13861-13867` | Monster29 | 1.2, Level 1 | **PoisonShadow01** |
| 34 | Cursed Wizard | 2 / 2, 3 / 2 (fallback) | `:13908-13932` | player model, Legendary set +9 | **1.0 in DS** | **CursedWizard** (json says 0.9) |
| 41 | Death Cow | 2 / 2, 3 / 1 | `:13842-13850` | Monster31 | 1.1, Great Hammer | **DeathCow01** |
| 40 | Death Knight | 2 / 3 | `:13851-13860` | Monster30 | 1.3, Lighting Sword | **DeathKnight01** |
| 35 | Death Gorgon | 2 / 3, 3 / 2 | `:13897-13907` | Monster12 | 1.3, two Crescent Axes, BlendMesh 1, Level 2 (fire) | **DeathGorgon01** |
| 49 | Hydra | 2 / boss | `:13786-13792` | Monster38 | 1.0, BlendMesh 5 | **Hydra01** |
| 37 | Devil | 3 / 1 | `:13877-13882` | Monster27 | 1.1 | **Devil01** |
| 64 | Orc Archer | 3 / 3, 4 / 1 | `:13650-13658` | Monster47 (= OrcArcher01's body) | **1.2, Battle Bow +3, HiddenMesh 1** | body cooked; **no json for 64** (OrcArcher01 is BC's 85: 1.1, Bow04 +1) |
| 65 | Elite Orc | 3 / 3, 4 / 1 | `:13668-13674` | Monster48 (= Orc01's body) | **1.3, HiddenMesh 2**, no weapon | body cooked; **no json for 65** (Orc01 is BC's 84, scale 1.1) |
| 67 | Metal Balrog | 3 / boss | `:13869-13876` | Monster28 (= Balrog01's body) | 1.6, Bill of Balrog +9 | body cooked; **no json for 67**. It differs from Balrog (38): drawn with ExtraMon (`ZzzCharacter.cpp:8645-8651`), which sets BodyLight **(0,0,1), a blue Balrog** (`ZzzObject.cpp:500-503`). Its weapon gets RENDER_EXTRA (`ZzzCharacter.cpp:7012`) |
| 60 | Bloody Wolf | 4 / 2 | `:13730-13737` | Monster44 | 2.2, two energy joints | **BloodyWolf01** |
| 69 | Alquamos | 4 / 2 (fallback) | `:13573-13578` | Monster51 | 1.0, BlendMesh 0 | **Alquamos01** |
| 57 | Iron Wheel | 4 / 3 | `:13759-13768` | Monster42 | 1.4, Aquagold Crossbow, energy joints | **IronWheel01** |
| 70 | Queen Rainer | 4 / 3 | `:13579-13586` | Monster52 | 1.3, BlendMesh -2, no shadow | **QueenRainer01** |
| 66 | Cursed King | 4 / boss | `:13682-13688` | **Monster49** (684 tris, 50 bones, 7 actions; king09.jpg + king08.tga) | 1.7, no weapon; link bones 32/43 (`:11978-11981`) | **missing entirely**: no json, no body |

What MuMain draws for the three new ones (the rest are already in their jsons' `effects_from`):
- **Cursed King**
  - Walking: one frame in 5 it drops a **BITMAP_BLOOD** decal at ± 10/70 of a tile round him (`ZzzCharacter.cpp:5827-5834`). He leaves a trail of blood.
  - Attack: SOUND_THUNDER01, then four **BITMAP_JOINT_LASER+1** beams from his hand bones to the target, each with BITMAP_FIRE (`:2223-2242`).
  - Boss skill: `MODEL_SKILL_INFERNO` sub 1 (`:1848-1856`).
  - Sounds: mCursedKing1, mCursedKing2, mCursedKingDie1, `SetMonsterSound(160, 164, -1, -1, 163)`, BoneHead 20 (`ZzzOpenData.cpp:3730-3735`). **None of the three is in `MU2_BGFX/source/sounds`.**
- **Metal Balrog**
  - Boss skill: MODEL_CIRCLE + MODEL_CIRCLE_LIGHT + SOUND_HELLFIRE. While it lasts, one MODEL_FIRE a frame within ± 512 units, with SOUND_METEORITE01 (`ZzzCharacter.cpp:1974-1990`, shared with Balrog).
  - Sounds: Balrog's (cooked).
- **Orc Archer (64) and Elite Orc (65)**
  - Sounds: mOrcArcherAttack1 / mOrcCapAttack1 / mHunter2 / mBullDie (`ZzzOpenData.cpp:3718-3729`). All cooked.
  - Only the scale, weapon level and HiddenMesh differ from the BC chiefs.

#### 3.2 DS5-DS7 (Season 3+, map 32)

| # | name | square | MuMain create | body |
|---:|---|---|---|---|
| 294 | Axe Warrior | 5 (and 4 when present) | `MM/World/GameMaps/GMHuntingGround.cpp:251-262` | Monster86, scale 0.7 |
| 60, 61, 71, 73 | Bloody Wolf, Beam Knight, Mega Crust, Drakan | 5 | ZzzCharacter (above; `:13713-13729`, `:13588-13611`, `:13621-13639`) | all cooked |
| 190 | Death Angel (Kalima 4) | 5 | `GMHellas.cpp:784-799` | Monster64, scale 1.2, BlendMesh 1 |
| 291 | Fire Golem | 5 boss | `GMHuntingGround.cpp:210-221` | Monster83, scale 1.8 |
| 290, 292, 293 | Lizard Warrior, Queen Bee, Poison Golem | 6 | `GMHuntingGround.cpp:198-249` | Monster82, 84, 85 |
| 57, 70, 74 | Iron Wheel, Queen Rainer, Alpha Crust | 6 | ZzzCharacter | all cooked |
| 197 | Illusion of Kundun 4 | 6 boss | `GMHellas.cpp:897-912` | **Monster70 (MODEL_SHRIKER)**, SubType 9, scale 1.5, two Double Blades. Not Monster65: only Illusion 7 uses that (`:914-924`) |
| 180 | Shriker (Kalima 2) | 3 (when present) | `GMHellas.cpp:880-895` | Monster70, scale 1.2, two Double Blades |
| 434, 435 | Gigantis, Berserk | 7 | `GM_Kanturu_1st.cpp:408-428` | Monster113, Monster107 |
| 438, 439 | Persona, Dreadfear | 7 | `GM_Kanturu_2nd.cpp:57-95` | Monster115, Monster117 |
| 436, 437 | Balram, Soram (Trainee) | 7 | `GM3rdChangeUp.cpp:403-440` | Monster92, Monster95 |
| 440 | Dark Elf | 7 boss | `GMCrywolf1st.cpp:691-701` | Monster93, scale 1.5 |

- All those bodies exist in D/Monster. None is in `MU2_BGFX/source/monsters`.
- The later maps' per-monster visuals sit behind their own `IsIn...()` guards. For example, HuntingGround's object render checks `IsInHuntingGround()` (`GMHuntingGround.cpp:157`). Whether each breed's effects still show in DS needs a breed-by-breed read (open question 3).

---

### 4. Charon (NPC 237)

- MuMain: `case MONSTER_CHARON: OpenNpc(MODEL_NPC_DEVILSQUARE)`, default scale (`ZzzCharacter.cpp:14392-14395`). The model is `Data/NPC/DevilNpc01.bmd` (`ZzzOpenData.cpp:1911-1912`).
- The light in his hands is two BITMAP_LIGHTNING+1 sprites at bone 20 (3.5,-12,10), scale 0.3, turning opposite ways at WorldTime/50, lit `sin(WT·0.002)·0.35+0.65` (`ZzzCharacter.cpp:11249-11268`). The same case adds a BITMAP_JOINT_ENERGY wisp, 1 frame in 30.
- Talking to him opens the enter window on the server's NPC reply, type 4 (`MM/Network/Server/WSclient.cpp:6347-6349`).
- **MU2_BGFX already has all of this.**
  - `source/npc/DevilNpc01.json` (sheet `npc_npc.png`).
  - He stands in Noria at **171,105**, facing 4 (`source/world/noria/placements.json:73-79`), as OM 0.95d does (`Version095d/Maps/Noria.cs:32`).
  - His orb is drawn in code (`src/game/play_tuning.h:788-794`, `src/game/play.h:1682-1690`). `sim::kCharon = 237` (`src/sim/realm.h:176-177`).
  - His lines say Devil Square is still to come (`src/game/play.cpp:2717-2720`, `src/sim/realm.cpp:938`).
- Nothing to import. Only his window and its wiring are missing.

---

### 5. Tickets: Devil's Eye, Key and Invitation

`MODEL_DEVILS_EYE/KEY/INVITATION = MODEL_POTION + 17/18/19` (`MM/Core/Globals/_enum.h:2087-2089`). They are loaded as `Data/Item/Devil{0,1,2}` (`ZzzOpenData.cpp:915-916`, textures `:1408`).

| item | file | tris | rig | sheets | what |
|---|---|---:|---|---|---|
| Devil's Eye (14,17) | Devil00.bmd | 64 | 1 bone | devileye.jpg 32², devileye2.jpg 16² | a faceted red-brown orb |
| Devil's Key (14,18) | Devil01.bmd | 82 | 3 bones | devilkey.jpg 64×32 | a gold key with a jewelled bow |
| Devil's Invitation (14,19) | Devil02.bmd | 160 | 11 bones, 1 action × 6 keys | visitor.jpg 32², visitor2.jpg 16² | a rolled scroll tied with a red ribbon |

How MuMain shows them:
- On the ground:
  - The Eye lies at Angle[0] 90 (`ZzzObject.cpp:5744-5747`). It is drawn with BlendMesh 1, BlendMeshLight `sin(WT·0.002)·10+15.65`, a V-scroll and Alpha 2: **a pulsing, flowing glow** (`:9883-9894`).
  - The Key lies at Angle 270/270 (`:5764-5767`). It has two red **BITMAP_SPARK+1** sprites at bones 1 and 2, colour `L·(2, 0.32, 0.32)`, size L·0.8, with `L = sin(WT·0.002)·0.35+0.65` (`:9895-9911`).
  - The Invitation lies at 270/90 (`:5768-5771`). It has the same red sparks at bones 9 and 10 (`:9912-9928`).
- Level glow: a level of 6 or less is halved; above that it shows as +13 (`ZzzObject.cpp:9592-9604`).
- The name is drawn in **yellow** (`MM/Engine/Object/ZzzInventory.cpp:2186-2189`).
- **MU2_BGFX has none of the three.** `source/items/misc` has `EventBloodCastle00-02` (the BC pattern to copy) but no Devil00-02. `src/sim/market.cpp:30` notes "no Devil Square here".
- `Item/devil_wing.OZT` is a wing sheet, unrelated.

---

### 6. Sounds, music and interface art

**Sounds** (presence in `MU2_BGFX/source/sounds`):

| file | have? | use |
|---|---|---|
| aRain.wav | **yes** (`arain.wav`) | DS's only ambient bed, looped (`ZzzObject.cpp:3687`) |
| eThunder.wav | yes | not played by the DS flashes (commented out, `:3685`); Cursed King's attack plays it (`ZzzCharacter.cpp:2226`) |
| mCursedKing1, mCursedKing2, mCursedKingDie1 | **no** | Cursed King |
| sHellFire, eMeteorite, mBalrog* | yes | Metal Balrog |
| mOrcArcherAttack1, mOrcCapAttack1, mBullDie, mHunter2 | yes | Orc Archer, Elite Orc |
| iEventStart.wav | yes | not tied to DS in MM (it is the ring event's, `ZzzOpenData.cpp:5649`) |

- **Music:** `D/Music/devil_square_intro.mp3` (930 KB) and `devil_square_outro.mp3` (1.8 MB) ship but are **unused by MuMain**. `source/music` has neither.
- House rule "music is rare and in fights" fits the intro as a match-start sting. That is a user call (open question 5).

**Interface.** Both windows are generic frames plus text. No DS art is drawn.
- **Enter window** (`MM/UI/NewUI/Events/NewUIEnterDevilSquare.cpp`):
  - Art: `newui_msgbox_back.jpg`, `newui_item_back01.tga` (top), `newui_item_back02-L/R.tga`, `newui_item_back03.tga` (bottom), `newui_exit_00.tga`, `newui_btn_empty_big.tga` (`:289-298`).
  - Title "Devil Square" (`:161`). Six lines of lore text (`:163-168`).
  - Seven 180×29 buttons from y+155, one per square: "Square %d (Lv %d-%d)", and the 7th "Master level" (`:66-72, 268-280`).
  - Only the button for the hero's level is unlocked. Locked text (150,150,150), open (255,255,255) (`:23-24, 250-266`).
  - Level bands are in `MM/Engine/Object/ZzzInventory.cpp:131-132`: 15-130, 131-180, 181-230, 231-280, 281-330, 331+; DK/MG/DL 15-110 ... 311+. They belong to Part A.
- **Rank window:** `CDevilSquareRankMsgBox` draws IMAGE_MSGBOX_BACK/TOP/MIDDLE/LINE/BOTTOM and four SEPARATE_LINEs, plus an OK button (`MM/UI/NewUI/Dialogs/NewUICustomMessageBox.cpp:2595-2715`). Text as in §2.
- **`D/Interface/devilsquare.OZT`** (166×90, alpha) is a silver logo plate: Korean "데빌 스퀘어" over two scrolls and a skull key. **No MM code loads it** (`S/devilsquare_ozt.png`). It may suit our own enter window, with the Korean replaced.
- `icon_Rank_*.OZT` belong to Crywolf's MVP window (`MM/UI/NewUI/Events/NewUICryWolf.cpp:560-575`), not DS.

---

### 7. What MuMain draws: the look

- **Frame:** black clear and fog, no sky.
- **Ground:** one grey-green worn stone texture, multiplied by a flat **dark teal** (63,103,115) everywhere. The whole map reads as cold, wet, blue-green stone.
- **Arenas:** four flat yards about 32 by 36 m, sunk in a rim that rises 1.6 m on the south and east sides.
  - Stepped dark stone wall sections run along the north and west rims, with brick panels at the corners.
  - Tipped stone blocks sit as rubble on the rims and on two or three islands inside each yard.
- **The audience:**
  - About 50 bronze spear-knights stand in loose ranks down the west side, with a square regiment block at the south-west.
  - About 48 silver-blue javelin guards with blue-and-gold shields stand down the east side.
  - The two lines face each other across the yard: a fight under a silent honour guard.
- **Weather:** steady slanted rain over the hero, ripples on the stone, splashes off the knights' shoulders and every fighter's shoulders. `aRain.wav` loops.
- **Light:** no lamps, no fires, no object lights. The only change is silent storm light: flickering blue-white pools within six tiles of the hero for one second in every four.
- **Monster light** comes from the breeds themselves: Gorgon fire, Hydra horn lightning, Cursed King's lasers and blood trail, Metal Balrog's fire rain, the energy joints of Iron Wheel and Bloody Wolf.

**It should feel like a drowned gladiator pit at night:** teal and black, wet stone, rain and storm flicker, ranks of statues watching.

**House rules:**
- Night looks not too dark or foggy: start from an existing dark sheet (Dungeon / Lost Tower), not black. The black void beyond the rims comes from fog over the flat NoMove plain, not from missing ground.
- Rain should stay faint, as Icarus's and Noria's are (`src/game/world/weather.cpp:4-8`). MU's full pool is 200 drops, everyone's, all the time.
- The flashes: Noria already has thunder with flash-from-loudness (`weather.cpp:51-94`). MU's DS flashes are silent, local and rapid. Decide which to reuse (open question 4).

**The pipeline route** (by analogy with the Lost Tower and Blood Castle):
- `pipeline/terrain.py D/World10 devilsquare <assets> 10` runs clean (`S/terrain.log`).
- Table entries for key 9:
  - `GRASS_BY_MAP[9] = []` is needed.
  - No HIDDEN, BLEND_MESH, VOID or OPERABLE entries.
  - `ANCHOR_LIGHT` in `tools/cook.py:1383-1398` is keyed by object type, not map, so no light entry either.
- Recipes:
  - Object03 and Object04 are rigged figures with one 4-key idle. They need the rigged-world-object path (the 20 rigged objects already sway, per memory).
  - Object01/02/05-07 are bacc00/bacc01 stone.
- Sheets to decode, with a `ds_` prefix: bacc00, bacc01, kni00, kni01, upper/lower/head/gloves/boots_11, Javelins03, guard_hair, hide.
  - shield06 already exists as `source/textures/shield06.png`; it comes from the identical Item sheet. Compare bytes before reusing.
  - Do not reuse `orc_kni00/01.png` or `upper_11.png`: different bytes.

**To import:**
- the 7 Object10 models
- Monster49 (Cursed King) with king09/king08 and its three sounds
- json variants for 64, 65 and 67 on existing bodies
- Item Devil00-02 with devileye/devileye2/devilkey/visitor/visitor2
- `rain03.OZT` for the splash, if `source/effects/rain` (rain01/rain02 only) lacks it
- optionally `devilsquare.OZT` and the two DS mp3s
- DS5-7 is a separate, larger import (12 bodies, §3.2)

---

### Open questions

1. **Which squares?** DS1-4 (0.95d, map 9) need 1 new body (Cursed King) and 3 json variants. DS5-7 (map 32) need 12 new bodies from Kalima, Land of Trials, Kanturu, Balgass and Crywolf. Map 32 can share World10's cook as MuMain does.
2. **The flat NoMove plain** (60k tiles of drawn stone at 2.235 m). Draw it under fog as MU does, or crop to the four arenas plus a margin?
3. **DS5-7 effects:** do the Land of Trials and Kanturu breeds' visuals (gated by `IsInHuntingGround()`, etc.) show in DS in MuMain? Each breed's Move/Render arm needs reading before porting.
4. **Lightning:** MU's silent per-frame flashes (1 s in 4, near the hero), or Noria's thunder with sound? MU's version strobes. The user asked for "lightning not too often" in Noria.
5. **Music:** the unused `devil_square_intro/outro.mp3`: a start sting and a result sting, or nothing, as MuMain does?
6. **Arena A's painted light** (1,311 tiles round DS1 only). Keep it, or flatten it to match B-D?
7. **The unreachable safe blob** (x 11-34, y 8-28, Safe+NoMove): ignore it? Players arrive straight in an arena; there is no lobby in the data.
8. **CursedWizard.json** says scale 0.9. In DS MuMain draws him at 1.0. Override per world, or accept 0.9?

---

## Part C: the engine side

Written 2026-10-10, read-only. No project file was edited. Paths are relative to `MU2_BGFX/`.
Line numbers were read at HEAD 058adb75-b15fb418; another session was committing while this was
written, so they drift. The function and constant names are the stable anchors.
OM = `LEGACY/reference/openmu/src/`. WZ = WebZen GameServer 1.00.93 (Part A's).

Anything about Devil Square's rules here is from general MU knowledge and a quick look at OM's
0.95d files. **It is to be verified against Part A.** It is marked *(A)* where it matters.
---

### 0. The short version

- Blood Castle (BC) is fully built and is the template. It lives in about 25 files: about 340
  lines of sim in `realm_move.cpp`, one header `sim/event.h`, hooks in seven other sim files,
  about 15 client files, the server's world host, the wire, two bots and three tests.
- Devil Square (DS) is a **simpler** event than BC in the grid (no flips, no drawbridge, no
  hand-in) and a **harder** one in two places the engine lacks:
  1. **timed waves** of monsters that start and stop on the run's clock;
  2. **a score for each player** and a **rank table** paid at the end, which needs a result window.
- The netcode needs almost nothing new. The client is a full lockstep mirror of the realm
  (`server/src/main.cpp:1-20`, `net::stateHash`), so the run, the waves and the scores are worked
  out on the client too. The client must be told only **which square** (the Welcome) and **the
  square's clock** (`RealmConfig`). The snapshot must carry the new state as plain data.
- Recommendation: **do not build structure-plan S1's full `Instance` interface first.** Move BC's
  code out of `realm_move.cpp` into its own file, generalise the five small pieces both events
  share (the gatekeeper's door, home on death, the held-down nests, the event's tracker panel, the
  server's instance worlds), and write DS beside BC in its own files. Take S1 when a third event
  (Chaos Castle) comes. Reasons in section 2.

---

### 1. Every place Blood Castle touches: the checklist

One line each: what is there, and what DS needs at that place. "Same" means DS needs a
parallel entry. "Share" means it should be generalised once for both (section 2). "None" means
DS does not need it.

#### 1.1 The rules (`src/sim`)

| # | file:line | what BC has there | DS |
|---|---|---|---|
| S1 | `sim/event.h:1-212` | the whole event table: map 11, wait 60 s, run 15 min, quotas, breeds by castle, statue, rewards, grid boxes, `CastleRun`, `CastleRefusal`, `AngelState` | **same**: a new `sim/square.h` (map 9, bands, wave table, rank rewards, `SquareRun`, `SquareRefusal`) |
| S2 | `sim/config.h:34` `RealmConfig::castle` (a `GateClock`) | the door's hourly clock | **same**: `RealmConfig::square`. `GateClock` (`config.h:12-26`) is already generic |
| S3 | `sim/maps.h:15-36` `MapRow{home, event, floors}` | `home` 2, `event` true | **share**: row `{"devilsquare", 9, {137, 95}, true, 3, true}` (arrive mid gate 58, OM `Version095d/Gates.cs:141`; home Noria) |
| S4 | `sim/maps.cpp:48` `kMaps` | the `bloodcastle` row | same |
| S5 | `sim/gates.cpp:39-41` exit 66; `:125-127` enter 1066 (a box off the map) | the Messenger's door as a gate row | **same**: exits 58-61 on map 9 (OM `Gates.cs:141-144`), enters 1058-1061 on map 3 |
| S6 | `sim/realm.h:167-177` `kMessenger`, `kCharon` | Charon already named | Charon gets a window instead of a line |
| S7 | `sim/realm.h:1000-1027` `gating()`, `castleRefusal`, `cloakSlot`, `enterCastle`, `castleRun`, `castleSecondsLeft`, `castleLiving`, `angeling` ... `claimCastle` | the public API | **share** the door (`gating`, a refusal, `ticketSlot`, `enterEvent`); **same** for the run (`squareRun`, `squareSecondsLeft`, `squareScoreOf`) |
| S8 | `sim/realm.h:1161-1185` `openCastleDoor`, `freeCastle`, `dropCastleBridge`, `setCastle`, `castlePassed(Of)`, `castleWeaponItem` | test switches and the castle number | `openSquareDoor`, `setSquare`, `squarePassed(Of)`, a `--square-wave N` jump |
| S9 | `sim/realm.h:1388-1394` `changeGrid` | BC's live grid | **none** (DS has no flips *(A: check WZ for a square barrier)*) |
| S10 | `sim/realm.h:1840-1842` `ticketDice_` | scroll/bone stream | **same**: its own stream for Devil's Eye/Key, so no other roll moves |
| S11 | `sim/realm.h:1968-1977` `run_`, `payCastle`, `castleTick`, `castleKill`, `fixed`, `passCastle` | private run state and steps | **same**: `square_`, `squareTick`, `squareKill`, `paySquare` |
| S12 | `sim/realm.h:2014` `castleOpen_`; `:2077-2082` `Player::castleOwed`, `castlePassed`, `angeling`, `staffOwed`, `claimOwed` | per-player door state | **share** `eventOwed`/`eventPassed`; **same** a per-player `squareScore` (+ `squareRank`) in `Player` |
| S13 | `realm.cpp:247-250` | the castle copies its tables (`own_`) for the live grid | none |
| S14 | `realm.cpp:293-299` | `run_` reset; Waiting; entrance shut | same: `square_` reset, Waiting on the shared clock |
| S15 | `realm.cpp:335-363` `castleShut` | nests inside shut boxes still raised | none |
| S16 | `realm.cpp:442-461` | every monster raised **down**, rising at the run's start; statue and sorcerers held for the run | **share**: the "held down, called up" pattern is the wave spawner's whole base |
| S17 | `realm.cpp:924-938` | Talk to 232/233 opens a page | same: 237 opens his page (`gating` shared, or `squaring`) |
| S18 | `realm.cpp:933-943` | Charon in the "Greet" group | take Charon out of it |
| S19 | `realm.cpp:1062` `castleTick()` in `step` | the run once a tick | `squareTick()` beside it |
| S20 | `realm.cpp:1169-1173` `heroBefore`: `castleOwed` -> `passCastle` | the door taken at the tick's start | **share**: `eventOwed` -> pass |
| S21 | `realm.cpp:1586-1590` `changeGrid` | | none |
| S22 | `realm_move.cpp:331` `rouse`: BC wakes from further | the road's garrison sees him | same, probably (an arena); *(A)* |
| S23 | `realm_move.cpp:623-986` | door, `setCastle`, `freeCastle`, `castleTick`, `payCastle`, `castleKill`, the Archangel, `castleLiving`, `castleSecondsLeft` | move to `realm_castle.cpp`; DS gets `realm_square.cpp` |
| S24 | `realm_fight.cpp:1814-1821` `kill`: statue never rises, sorcerers by quota | respawn gate per event | **same**: a wave's breed stops rising once its wave ends |
| S25 | `realm_fight.cpp:2019-2023` `Rose`: home on death if no safe box **or map 11** | out to Devias | **share**: test `MapRow::event` instead of `kBloodCastleMap` |
| S26 | `realm_items.cpp:742` (was 733) Town Portal home, same test | | share, as S25 |
| S27 | `realm_items.cpp:1117-1140` scroll/bone roll outside castles | the ticket's halves drop | **same**: Eye/Key roll outside map 9 |
| S28 | `realm_items.cpp:1156-1161, 1272-1277` feather, gear in castle 6 | per-castle drops | none until asked |
| S29 | `realm_items.cpp:1539-1562` archangel weapon unsellable | | none |
| S30 | `realm_quests.cpp:185-187` `countKill` -> `castleKill` | the kill hook | **do not copy this**: see the side finding at the end. Hook DS's score in `kill` once |
| S31 | `realm_party.cpp:72-78` `countPartyKill` calls `countKill` per member in view | | as S30 |
| S32 | `realm_skills.cpp:165-171` auras allowed on BC's safe court | | *(A/B: does the square have a safe landing?)* |
| S33 | `realm_commands.cpp:122-128` `EnterCastle`, `HandInStaff`, `ClaimCastle`, `Window::Gate/Angel` | commands | **share** `EnterEvent`; DS needs no hand-in; maybe `ClaimSquare` if the result window has a button |
| S34 | `sim/command.h:64-83` | the command kinds | as S33 |
| S35 | `realm_snapshot.cpp:157` `a.pod(run_)`; `:205` `castleOpen_`; `:209-222` the changed grid | the run crosses the wire as plain data | **same**: `a.pod(square_)`, and the score rides `heroes_` (`a.vec`, trivially copyable, `:25-58`) |
| S36 | `sim/machine.h:89-98` `Recipe::Cloak = 8`; `:140-166` `Service::Cloak`, `kRowServices`, `byRecipe`; `:220-230` `kCloakRate`, `kCloakZen`, `kCloakFromLevel` | the ticket recipe | **share**: one "event ticket" recipe shape; DS is `Recipe::Invitation = 2` (OM `ChaosMixes.cs:284`) |
| S37 | `machine.cpp:59-95` tally; `:164-167` recipe match; `:216-219` nearest; `:375-389` judge; `:596, 650, 669, 685` names | | as S36 |
| S38 | `realm_machine.cpp:171-176, 211, 254-268` cloak make | the cloak's level from the scroll | as S36 |
| S39 | `sim/items.h:1171-1180` `firecrackerMap` already has 9 | | **done** (MagicBox03.json says "devil square in future") |
| S40 | `sim/items.h:1195-1228` `scrollOfArchangel`, `bloodBone`, `invisibilityCloak`, rates, `bloodCastleMap`, `castleMaterialLevel` | | same: `devilsEye`, `devilsKey`, `devilsInvitation`, `squareMap`, `squareMaterialLevel` |
| S41 | `sim/items.cpp:670-680` the predicates | | same |

#### 1.2 The client (`src/game`, `src/app`, `src/net`)

| # | file:line | what BC has there | DS |
|---|---|---|---|
| C1 | `net/wire.h:69`, `:266-267` `Welcome::castle` (protocol 7) | which castle the world is | **share**: `Welcome::instance` (or add `square`); bump the protocol |
| C2 | `net/wire.cpp:262-275` `RealmConfig::castle` on the wire; `:439`, `:665` the Welcome's castle | | add `square`'s `GateClock`; bump |
| C3 | `game/remote_link.cpp:480-484` no prediction on an event map (its grid changes) | | decision: DS's grid does not change, so it could keep prediction (a `MapRow` flag) |
| C4 | `game/play_open.cpp:38` `castleAir_`; `:59, 82, 214` the Welcome's castle -> `setCastle`; `:483-484` the run bed | | same: `squareAir_`, `setSquare` **before** raise (see 3.3), a bed if chosen |
| C5 | `game/play.h:462-471` test switches and `enterCastle`/`handInStaff`/`claimCastle` | | same |
| C6 | `game/play.h:655-659` `takeHome` on `castleRun().sentOut` | | **share**: any event's `sentOut` |
| C7 | `game/play.cpp:2717-2721, 2776` Charon's three "not open" lines | | replace with his page |
| C8 | `game/play_requests.cpp:678-684` log of Enter/Claim | | share |
| C9 | `game/play_sound.cpp:419-421` loop the bed while Running | | same, if music is chosen |
| C10 | `game/play_show.cpp:1441, 1948`, `play.cpp:862` statue's bar and exposure | | none |
| C11 | `app/modes/play_mode.cpp:150-160` save writes an event map as its home | | **free** via `MapRow::event` |
| C12 | `play_mode.cpp:301-302` `--castle-period` | | `--square-period` |
| C13 | `play_mode.cpp:605-620` `castleNext` -> `setCastle`, test switches | | same (but `setSquare` must come before raise) |
| C14 | `play_mode.cpp:857-860` the Messenger's gate -> `castleNext` | | **share** by enter-gate range |
| C15 | `play_mode.cpp:884-895, 1782-1791` won -> "Event complete" banner in Devias | | same: rank and pay banner in Noria |
| C16 | `app/context.h:118-124` `castleNext`, `castleDone*` | | share (`eventNext`, `eventDone*`) |
| C17 | `game/ui/quest_dialog.h:20-63`, `quest_dialog.cpp:95-154, 296-418` the Messenger's page `kGate` (castles as pages) and the Archangel's `kArchangel` | | **same**: Charon's page `kSquareGate` (four squares as pages); no hand-in page |
| C18 | `game/ui/desk.cpp:271-282, 335-363` which page is open; Enter/Give/Claim wired | | same |
| C19 | `desk.cpp:1112-1113, 1772-1783` auras in the court, statue's bar | | none |
| C20 | `game/ui/tracker.h:71, 123, 145-150`, `tracker.cpp:569-588, 797-` the run's panel (phase, clock, kills, sorcerers, statue) | | **share**: a generic event panel fed by a small view struct; DS shows wave, clock, his score, his place |
| C21 | `game/ui/herald.cpp:65-74` `kGates` already has "Devil Square", 4 squares, `kSquareBands` | the top-of-screen count-down | point it at `RealmConfig::square` (period 0), as BC's row does |
| C22 | `game/ui/travel.cpp:53-69, 278-279` `kEvents` already has DS at 14400 s / 1500 s | the Noria card's count-down | same as C21 |
| C23 | `game/ui/arrival.cpp:66` `kPlaces`; `ui/minimap.cpp:314` `mapName` | | add `devilsquare` / `case 9` |
| C24 | `game/world/world.cpp:133-140, 335-352` drawbridge and sparks opened by name | | none unless Part B asks |
| C25 | `game/world/drawbridge.cpp:20`, `castle_sparks.cpp:51`, `boids.cpp:72-73, 120`, `void_clouds.cpp:110`, `ornaments.cpp:301` | world systems gated by `"bloodcastle"` | Part B's list; each returns early for an unknown name |
| C26 | `core/args.h:213-235`, `args.cpp:207-219, 382-403` `--castle-*` | | `--square N`, `--square-open`, `--square-period S`, `--square-wave N` |
| C27 | **new** | -- | **a result window**: rank, name, score, exp, zen; MU's is the DS rank table *(A)* |

#### 1.3 The server (`server/src/main.cpp`)

| # | line | what BC has | DS |
|---|---|---|---|
| V1 | `:117` `Landing::castle`; `:151` `World::castle` "one world a castle" | the instance key | **share**: `instance` (castle or square) |
| V2 | `:748-787` `raiseWorld`: `setCastle` after raise; Welcome carries it | | `setSquare` **before** raise (3.3) |
| V3 | `:896-917` the Hello takes the castle off `g_due` | | share |
| V4 | `:969-977` a castle world joins its own, never copied, **no player cap** | | share; and add the cap (WZ 10 a castle *(A)*, DS likewise) |
| V5 | `:1104-1114` `landingOf`: `Gated` through 1066 -> castle | | share by enter-gate range |
| V6 | `:1184-1216` run over -> **only `playerAt(0)`** sent to the town ("a known limit of sprint 19") | | **fix while generalising**: every player in the instance is sent out |
| V7 | `:1273-1290` `placeOf`: never saved inside an event map | | free via `MapRow::event` |
| V8 | `:1340-1342` `--castle-period` | | `--square-period` |

#### 1.4 Data, cook, tools, tests

| # | file:line | BC | DS |
|---|---|---|---|
| D1 | `pipeline/terrain.py:134` `HIDDEN_BY_MAP[11]`; `:193` `GRASS_BY_MAP[11] = []`; `:240` `VOID_BY_MAP`; `:292` `OPEN_BY_MAP`; `:304` `FLOOR_BY_MAP`; `:313` `VOID_FILL_BY_MAP`; `:365, 373, 381` light rows | keyed map_number-1 | Part B: key **9** (`World10`), grass `[]` probably, a void fill from the four landings |
| D2 | `pipeline/index.py:58-` `GATE_BOXES_BY_MAP` | BC never added | add the four landings 58-61 |
| D3 | `tools/cook.py:203` `AIRS`; `:1346` anchors; `:1375` `SHOWN_LATER`; `:1398` anchor light | | Part B |
| D4 | `tools/cook.py:2124-2127` Charon at Noria 171,105 dir 4 (`source/world/noria/placements.json:75`) | | **done**; he stays |
| D5 | `tools/cook.py:2199-2202` folk on map 11 (the Archangel) | | DS has no folk inside *(A)* |
| D6 | `source/mu.db` `monster_spawns` (map 11: 96 rows) | | map 9: the waves' nests, four squares (3.2) |
| D7 | `source/sounds/sounds.json:1433` `world_bloodcastle` | | a bed if chosen (memory: new sound needs the showing cook) |
| D8 | `sheets/worlds/bloodcastle.json` | | `sheets/worlds/devilsquare.json` |
| D9 | `source/items/**` | cloak, scroll, bone, Archangel weapons | Devil's Eye 14/17, Key 14/18, Invitation 14/19 (all three numbers are free today) |
| D10 | `tools/bot/brain.h:73-74, 217-219, 331-334, 434-452, 590, 618-619, 861, 902-903, 989-990, 1169-1172, 2337-2596, 2985-2989, 3114, 3497, 3655, 4313-4323` | the whole BC brain | section 4 |
| D11 | `tools/netbot/netbot.cpp:119, 175-179, 442-444, 575-577, 770-839, 989-1011` | Welcome castle, waits on `sentOut`, the party board's "Blood Castle together" | section 4 |
| D12 | `tests/sim_test.cpp` the Messenger's door (~6162-6220), `testCharon` (~7049-7075), the cloak mix (~7190-7217), `testCastleGrid` (~11164-11303) | | `testCharon` must change first; add `testSquare`, the invitation mix |

---

### 2. What can be shared, and how far to generalise

#### 2.1 What is truly shared

| piece | BC today | shared form |
|---|---|---|
| the gatekeeper's door | `castleRefusal`, `cloakSlot`, `enterCastle`, `passCastle`, `castleOwed/Passed`, enter gate 1066 | `EventDoor` rows: {npc, map, enter gate per level, ticket item, bands, clock}. One `enterEvent(level)` path. |
| the door's clock | `GateClock` in `RealmConfig::castle` | already generic; add `square` |
| the ticket recipe | `Recipe::Cloak` with scroll + bone + Chaos | OM's own `BaseEventTicketCrafting` is one class for both. One judge with a row per ticket: {half A, half B, ticket, zen by level, rate, most level, from level} |
| the halves' drop | scroll/bone roll outside map 11-17 | one roll per ticket row, each on its own dice stream |
| home on death and portal | `|| map == kBloodCastleMap` in two places | `MapRow::event` (the row already has the flag) |
| save never inside | `MapRow::event` | done |
| held-down nests called up by the run | `realm.cpp:447-461` and `castleTick` | a helper: hold every monster down; call up those a predicate picks, at a tick |
| run phases and seconds left | `CastlePhase`, `castleSecondsLeft` | each event keeps its own run struct; a tiny `EventView` {title, phase, seconds, three lines} is what the tracker and herald read |
| one world per instance on the server | `World::castle` | `World::instance` + the map name |
| everyone out at the end | `sentOut`, first player only | every player, both events |

#### 2.2 What is BC's alone

The live grid and its boxes, the drawbridge, the statue and its weapon, the Archangel's hand-in,
the sorcerer quota, the per-castle breed swap. DS has none of these.

#### 2.3 What is DS's alone

The wave table, a score per player, ranks, a rank-paid reward, the result window, four squares
in four boxes of one terrain.

#### 2.4 Three ways to do it, weighed

**(a) Copy BC.** A `square.h` and `realm_square.cpp` beside BC's. Each hook gets a second line
(`|| map == kDevilSquareMap`).
- Cost: `realm.h` grows about 60 lines again (BC's share today is about 60 of its 2136). About
  ten hook sites get a second branch. The server's first-player limit is copied too.
- Risk: low for BC. But it is what structure-plan section 1 calls the cause of the hubs' growth.

**(b) Build S1's `Instance` interface first** (`docs/structure-plan.md` S1: `raise`, `step`,
`onKill`, `onDeath`, `allows`), move BC into it, then add DS.
- The snapshot copies plain data only (`realm_snapshot.cpp:25-58`, `static_assert
  is_trivially_copyable`). A virtual `Instance` cannot be pod'd. Its state would have to be a
  separate plain struct, and the logic would need a narrow host API onto `Realm`'s privates
  (`bodies_`, `lay`, `give`, `gain`, `say`, `dice_`, `changeGrid`, `halt`, `dropBlow` ...).
- That is an M-L refactor of a working, tested, bot-played event, done before any DS value.
  Structure plan puts it in sprint D, and the memory says the plan is parked.
- With two events the right shape of the interface is still a guess.

**(c) Recommended: extract, share the five small pieces, write DS in its own files.**
1. A pure move: BC's 340 lines leave `realm_move.cpp` for `realm_castle.cpp`. Structure plan
   names this "the one move worth doing early". The seeded logs must not change.
2. Share the door, the clock, home on death, the held-down helper, the tracker's view, the server
   instance and "everyone out" (the table in 2.1).
3. DS lands as `sim/square.h` + `sim/realm_square.cpp`, `game/ui/square_result.cpp` (the window),
   and a small `squareTick`/`squareKill` hook pair.
4. `realm.h` grows by about 25-35 lines (the run struct lives in `square.h`; the public API is
   five or six methods). `play.cpp`, `play.h`, `play_mode.cpp` and `desk.cpp` get only
   registration lines (a page id, a window, a banner), not logic.
5. The full `Instance` interface waits for Chaos Castle (rule of three). By then two worked
   examples show its true seams.

**Why (c) and not (a):** the shared pieces are exactly the ones that would otherwise be copied
with their bugs (the first-player send-out, the per-member castle kill). **Why not (b):** it
spends the largest effort before the smallest proof, on files another session edits daily.

**The hub honesty.** Even (c) touches `realm.h`, `realm.cpp` (step, talk, raise),
`play_mode.cpp`, `desk.cpp`, `play.h`, `brain.h` (4689 lines) and `sim_test.cpp` (12233 lines).
There is no size ratchet yet (structure-plan M7; `tools/sizecheck.py` does not exist), so nothing
stops them growing. Keep each hub edit to registration. Put a new DS test in its own function, and
in a new file if S7's split has landed by then.

---

### 3. Devil Square's mechanics in engine terms

#### 3.1 The record to carry *(all (A); OM 0.95d as read here)*

*Superseded by Part A where they differ: bands, schedule, score and ticket prices are WebZen 0.97d's there.*

- **Map 9**, client `World10` + `Object10` (`LEGACY/reference/MuMain/src/bin/Data/World10`,
  present, with `Object10`'s 7 models). Four squares on one terrain:

| square | gate | landing box (OM `Version095d/Gates.cs:141-144`) | monster box (OM `Maps/DevilSquareN.cs`) | band (OM `DevilSquareInitializer.cs`) |
|---|---|---|---|---|
| 1 | 58 | 133,91 - 141,99 | 119-150 x 80-115 | 15-130 (MG 10-110) |
| 2 | 59 | 135,162 - 142,170 | 121-151 x 152-184 | 131-180 (111-160) |
| 3 | 60 | 62,150 - 70,158 | 49-79 x 138-173 | 181-230 (161-210) |
| 4 | 61 | 66,84 - 74,92 | 53-83 x 74-109 | 231-280 (211-260) |

- **Ticket**: Devil's Invitation 14/19, level = square. Made at the Chaos Goblin of Devil's Eye
  14/17 + Devil's Key 14/18 of one level + a Jewel of Chaos; OM: 100k/200k/400k/700k Zen,
  80% (OM `DevilSquareTicketCrafting.cs:15-33`, mix number 2). The halves drop by monster-level
  brackets (OM `EventTicketItems.cs:29-31`).
- **Timing (OM)**: entry 1 min, game 20 min, exit 3 min, 10 players, party allowed.
  The herald and travel card already count to "every 4 h from midnight, 25 min"
  (`herald.cpp:73`, `travel.cpp:69`). WZ's is likely different *(A)*.
- **Waves (OM)**: wave 1 at 0-7 min, wave 2 at 5-14, wave 3 at 12-20, the boss wave at 15 min
  (5 bosses, once). Each regular wave is two breeds, 35 each, in the square's box, rising again
  while their wave lasts.
- **Breeds**: square 1 is Cyclops 17, Skeleton Archer 15 / Hell Hound 5, Hell Spider 13 / Poison
  Bull 8, Shadow 36 / boss Gorgon 18. Square 2: Dark Knight 10, Poison Shadow 39 / Cursed Wizard
  34, Death Cow 41 / Death Knight 40, Death Gorgon 35 / boss Hydra 49. Square 3: Death Cow 41,
  Devil 37 / Death Gorgon 35, Cursed Wizard 34 / ... Square 4: Orc Archer 64, Elite Orc 65 /
  Bloody Wolf 60, Axe Warrior 294, Alquamos 69 / ...
  **mu.db already has every square 1 and 2 breed** (5, 8, 10, 13, 15, 17, 18, 34, 35, 36, 39,
  40, 41, 49). Square 3 has all it was read for (37, 41, 35, 34). **Square 4 lacks 64, 65 and
  294**; 64 and 65 are WZ's BC orcs (here BC uses 84-89 for them, *(A)*).
- **Score (OM)**: +`GameLevel` (the square's number) a kill, to the killer only
  (`GameLogic/MiniGames/DevilSquareContext.cs:37-39`). Note for A: OM ranks with `OrderBy`
  (ascending) at `:62`, which looks backwards. WZ's score rule is A's to settle.
- **Rewards (OM)**: by rank 1-4 and square, exp + Zen, e.g. square 1: 6000/30k, 4000/25k,
  2000/20k, 1000/15k (`DevilSquareInitializer.cs:44-82`).
- **Out**: everyone to Noria at the end (OM `SafezoneMapNumber => Noria`, `DevilSquare1.cs:45`).
  A death goes to Noria and is out of the run.

#### 3.2 What exists and what is new

| need | exists? | engine form |
|---|---|---|
| Charon's door | **mostly** (BC's door) | share BC's door (2.1). Charon's page = BC's `kGate` page with squares as pages |
| scheduled entry | **yes** | `RealmConfig::square` `GateClock`; `herald`/`travel` already draw DS |
| one arena per square | **yes, by the server's instance worlds** | one realm per (devilsquare, square n). Only square n's nests raised (3.3) |
| the ticket and its halves | **mostly** | the shared ticket recipe; three item rows; one more dice stream |
| **timed waves** | **no; the base exists** | 3.4 |
| **score per player** | **no** | an `int32_t squareScore` in `Player` (per-player, pod'd in `heroes_`) |
| **rank table and rank pay** | **no** | at the run's end, `squareTick` sorts the players present (score, then id for ties: deterministic), writes `squareRank`, pays exp and Zen by the row |
| **result window** | **no** | the client reads ranks off its mirror and opens a window; no message needed (section 5) |
| exit to Noria | **yes** | `MapRow.home = 3`, `sentOut`, generalised for every player |
| death out | **yes** | `MapRow::event` in `Rose`/portal (S25, S26) |
| a run clock | **yes** (BC's ticks from raise) | **but anchor it to the wall clock**, 3.5 |

#### 3.3 Four squares, one terrain

- One `.mur` for map 9 holds every square's nests. With 35 per breed, square 1 alone is about
  6 x 35 + 5 = 215 bodies, so all four are about 860.
- **Raise only the chosen square's nests.** BC sets its castle **after** raise
  (`main.cpp:769`, `play_open.cpp:214`) and swaps breeds on bodies already made. DS should
  instead be told its square **before** raise, so `raise` skips every nest outside the square's
  box. The raid already does this: `setRaid` before `raise` (`play_open.cpp:205-211`, "handed to
  the realm before it is raised, as the arena's nests are"). That keeps 645 inert bodies out of
  every tick, on the server and in every mirror.
- So the order is `configure` -> `setSquare(n)` -> `raise`, in three places:
  `server/src/main.cpp:raiseWorld`, `play_open.cpp` (from the Welcome or `--square`), and
  `netbot.cpp:119`.

#### 3.4 The wave spawner

The engine already has the parts. BC raises every monster **down** and calls bodies up by
setting `risesAt` (`realm.cpp:447-461`; `castleTick` raises the sorcerers; `kill` stops the
statue rising, `realm_fight.cpp:1814-1821`). A wave is the same thing on a timetable.

- **Table** in `sim/square.h`: `{square, wave, breed, startSecond, endSecond, once}`, OM's
  numbers until A gives WZ's.
- **At raise**: every body of the square is held down, `risesAt = max`.
- **`squareTick`**: when the run's second crosses a wave's start, set `risesAt = tick_` (or a
  short stagger) on that wave's dead bodies. The respawn (`raiseBeast`) already draws a fresh tile
  from the nest box (`realm_fight.cpp:2028-`).
- **In `kill`**: while its wave lasts, a body rises again on its normal regen. After the wave's
  end, a dead one gets `risesAt = max`. This is one more branch beside BC's at
  `realm_fight.cpp:1817`, best as `if (squareHolds(kind)) ...` calling into `realm_square.cpp`.
- **A wave's living bodies at its end**: stay, or are dismissed with `What::Dismissed` as BC's
  win clears the castle (`realm_move.cpp:785-797`). *(A: WZ's rule.)*
- **Bosses**: `once` rows rise once at their start and never again.
- **No new spawn-after-raise is needed.** Structure-plan M3 (`spawn()`) is not a prerequisite:
  the bodies are made at raise and held down.
- **Which body is which wave**: by `Body::nest` (`realm.h:397-399`). Each wave's breed has its
  own nest row in mu.db, so the nest index (or its breed) maps to a wave.

#### 3.5 The clock: shared, not his own

- BC's run starts when the realm is raised, so on the first player's entry (`realm.cpp:293-299`).
  A second player who enters 3 minutes later joins a run 3 minutes old. That was fine for "one
  player alone", the decision of 2026-10-02.
- DS is a ranked contest of several players. They should start together, as MU's do. The
  server-plan already lists "the events on a shared schedule instead of each player's own
  clock" (`docs/server-plan.md:299`).
- **Engine form**: the realm already gets the wall clock in every tick (`Tick::wallClock`,
  `main.cpp:1180-1182`, `Realm::setWallClock`). `squareTick` reads it: in the entry window the run
  is Waiting; at the window's close it records `startsAt = tick_` and runs on ticks from there.
  Every mirror sees the same wall clock in the same tick, so it stays deterministic.
- Headless and the bot have no wall clock (`castleRefusal` treats that as shut). Keep
  `--square-open` to start on entry, as `--castle-open` does.

#### 3.6 Score, rank and pay

- **Where the score is added**: in `Realm::kill`, once, for the player the kill is credited to,
  **not** inside `countKill`, which runs once per party member in view (side finding 1).
- **Whether a party shares points** is a rule *(A)*. If it does, give each member in view the
  points explicitly in `realm_square.cpp`.
- **At the end**: rank the players present, not the dead or the gone. Ties by body id, so every
  mirror agrees. Pay exp through `gain` (crosses levels), Zen into the purse. Store what was paid
  on the run, as `CastleRun::paidExperience/paidZen` do, so the window can show it.
- **The window** shows the table from the mirror's own ranks. It opens at the run's end and closes
  with the rest's end or a button. MU's DS rank window is the model *(A/B for its look)*.

---

### 4. Bots: what `brain.h` and `netbot` need

BC's brain is the pattern (`brain.h:2337-2596`). DS's is shorter: no walk to a statue, no
hand-in, no Archangel.

1. **The map row**: `{9, "devilsquare", {137, 95}}` in the bot's world table (`brain.h:73-74`),
   never chosen to grind.
2. **The ticket economy** (as `cloakWorth`, `makeCloak`, `cloakHalfWanted`,
   `cloakPairHeld`, `castleTicket`):
   - keep Devil's Eye and Key, keep an Invitation, sell spares (`:2303-2334`, `:4313-4323`);
   - mix at the Goblin when an Eye + Key pair of a usable level, a Chaos and the Zen are held
     (`:1573-1589`, `:4032`, `:4159`, `:4182`, `:3481`, `:3489`).
   - Generalise these to "event tickets" rather than copy each function: the halves, the ticket,
     the levels and the bands differ, the logic does not.
3. **The door**: `squareChoice` (the highest square his invitations open and his level reaches,
   whose wave breeds he `takes`), `squareDue` (the clock, as `castleDue` at `:2381-2391`),
   `charon()` (walk to Charon in Noria, talk, Enter, as `messenger()` at `:2393-2420`), and an
   `Aim::Square` beside `Aim::Castle` (`:590`, `:2985-2989`, `:3114`, `:3655`).
4. **Inside** (`square()`, as `castle()` at `:2499-2594`):
   - hunt the nearest monster in the square; bosses first once up; answer whatever is on him
     when low;
   - drink earlier, as in BC (`:1169-1172`: 70%);
   - never leave the square's box; there is no town to fall back to;
   - after the end, wait for `sentOut` and go home (`:902-903` pattern);
   - say the phase changes and his final rank in the log, for review.
5. **Raise order**: `setSquare(owedSquare_)` **before** the raise in the bot's own map change
   (`:618-619` sets the castle after) and in `netbot.cpp:119`.
6. **netbot**: wait on `sentOut` (`:442-444`); the party board's "Devil Square together"
   (as `:770-839`, `:989-1011`): form a party of the free bots due at the same square.
7. **What to measure**: does a bot of the band finish 20 minutes alive, its score, its rank
   against the others, its pay. This is the user's balance check (memory: bot is the
   instrument).
8. Memory notes that apply: rebuilding netbot kills live bots; bot A/B needs a frozen sim worktree.

---

### 5. Netcode and server: what the client must be told

- **Nothing per event in the tick stream.** The client steps the same realm from the same
  commands (`server/src/main.cpp:1-20`). Waves, scores, ranks and the end all fall out of the
  mirror. The hash already covers happenings and players (`net::stateHash`, `wire.cpp:734-759`),
  so a mirror that disagrees says so.
- **The Welcome: which square.** Generalise `Welcome::castle` to `instance` (or add `square`),
  so the mirror calls `setSquare` before its raise. Bump the protocol (`wire.h:63-80` list).
- **The config: the square's clock.** `RealmConfig::square` crosses in the Welcome (`wire.cpp:
  262-275`); bump with it. The `--square-period` test clock rides it, as `--castle-period` does.
- **The snapshot**: `a.pod(square_)` beside `a.pod(run_)` (`realm_snapshot.cpp:157`); the
  score and rank in `Player`, which `a.vec(heroes_)` already carries. Both must stay trivially
  copyable (fixed arrays, no strings), as `CastleRun` is.
- **The server**:
  - an instance world per (devilsquare, n), as castles (`main.cpp:151, 969-977`);
  - the landing through enter gates 1058-1061 (`landingOf`, `:1104-1114`);
  - a cap of players a square *(A: 10)*; BC has none today;
  - at the end, **every** player due in Noria (fix `:1204-1216`'s first-player limit for both);
  - `placeOf` already keeps a quit inside out of the square (`:1285`).
- **Prediction**: the client turns off its predicted realm on event maps because BC's grid
  changes (`remote_link.cpp:480-484`). DS's grid does not change. Either accept no prediction in
  DS, or add a `MapRow` flag (`liveGrid`) and let DS keep it. Decision 11.
- **Server cost**: about 215 bodies a square a world, 10 players. Server capacity memory
  (~14 us a player a tick) suggests this is small. Measure once with bots.

---

### 6. Build steps, in order, each with a done-when check

Each step is small and lands on its own. Run from `MU2_BGFX/`. `checks` = `cmake --build build
--target checks`. No window unless asked; `--mute` on any review run; scratch saves (memory).

**Step 0. Groundwork shared with BC (pure refactor).**
- Move BC's code from `realm_move.cpp:623-986` to `realm_castle.cpp`. Replace the two
  `|| map == kBloodCastleMap` home tests with `MapRow::event`. Fix the per-member `castleKill`
  (side finding 1) in its own commit, since it changes party behaviour.
- Done when: seeded headless logs for lorencia, dungeon and bloodcastle (`--ticks 2000 --seed 7`)
  are byte-identical before and after the move; `checks` green except the known failures.

**Step 1. Ground.**
- `pipeline/terrain.py` rows for key 9 (Part B), `python3 pipeline/terrain.py
  $D/World10 devilsquare source/world 10`, `ground.json` with `ds_` sheets, `tools/content.sh`,
  sync, `cook.py --world devilsquare --only ground` and `--only tables`, `pipeline/index.py`
  (memory: build-only skips the index).
- Rows: `kMaps` `{"devilsquare", 9, {137, 95}, true, 3, true}`, `arrival.cpp` kPlaces,
  `minimap.cpp` `case 9`, `sheets/worlds/devilsquare.json`, `GATE_BOXES_BY_MAP[9]`.
- Done when: `./run.sh --headless --world devilsquare --ticks 200 --no-hand` logs `map 9`;
  the hero raises inside gate 58's box; a router plan from 137,95 reaches the square's monster box
  and not another square's.

**Step 2. Objects and lights.** Part B's list (Object10's 7 models, the air, any void fill).
- Done when: a `--shot` of each square's landing looks right to the user; `--budget --stats`
  once.

**Step 3. Monsters, held down.**
- mu.db `monster_spawns` map 9 for squares 1-4 (one nest row per wave breed, OM's boxes until
  A); checkpoint the WAL (memory); index; `--only tables` for **every** world (memory: new kinds
  need every world's tables). Square 4's missing breeds (64, 65, 294) wait for A.
- `setSquare(n)` before raise; `raise` skips nests outside square n.
- Done when: headless `--world devilsquare --square 1` reports about 215 monsters, all down;
  `--square 2` raises a different 215; none outside the chosen box.

**Step 4. Entry.**
- Item rows 14/17, 14/18, 14/19 and their figures; the shared ticket recipe with an
  `Invitation` row; the Eye/Key drop on its own dice stream; gates 58-61 and 1058-1061; Charon's
  talk opens his page; `RealmConfig::square`; `--square-open`, `--square-period`.
- Server: the instance key, the landing, the cap. Wire: Welcome and config; protocol bump.
- Replace `testCharon` with: no invitation -> refused; outside the window -> "not yet"; inside
  -> `Gated` lands in gate 58's box of map 9 with `squarePassed == 1`.
- Done when: sim_test green; a netbot with an invitation and `--square-open` is carried into
  square 1 by a local server and its mirror's hash agrees.

**Step 5. The run.**
- `sim/square.h` (phases, the wave table, the clock), `realm_square.cpp` (`squareTick`, the wave
  call-ups, the wave-end hold in `kill`, everyone out at the end), `a.pod(square_)`, the tracker
  view.
- Done when (`testSquare`, ticks only): Waiting until the window closes; at 0 s wave 1's bodies
  rise; at the wave's end its dead stay down; the bosses rise once at their second; at the end,
  `sentOut` and every player is owed Noria; a death inside sends him to Noria.
- **The rain stops when the square ends** (the user, 2026-10-10: 'we need that rain never stops at
  devil square only at the end when DS is ended'). Until then it never stops (Weather::open, a
  forced wet spell at kSquarePeak). When the run's phase turns to its end, the client's weather
  is let dry: wet_ false on the forced spell, so it eases out over the rain's own four-second turn
  while the result window shows, before everyone is sent to Noria. Done when: a headless run with
  a short `--square-period` logs the rain easing out at the end, not before.

**Step 6. Score, rank, rewards, the window.**
- `Player::squareScore/squareRank`; the score in `kill` once; the rank and pay at the end; the
  result window; the banner home in Noria.
- Done when: in `testSquare`, three players with scripted kills end ranked by score, ties by
  id; each is paid his row once; a snapshot taken mid-run and restored gives the same ranks; the
  netbot mirror's hash agrees through the end.

**Step 7. Bots.**
- Section 4.
- Done when: `tools/bot` alone, a level-band bot of each class, buys or makes an invitation,
  waits for the door, plays 20 minutes, is ranked and paid, and comes home. Then netbot: four bots
  on a local server go in as a party and finish (memory: stop test servers afterwards).

**Step 8. The show.** Sounds and the bed if chosen (memory: new sound needs the showing cook),
the wave banner lines, monster effects (memory: monsters come with their effects).
- Done when: the user judges it in game (memory: user judges effects in game).

**Step 9. Hetzner.** Only at a milestone (memory: Hetzner, deploy.sh; chat and party deploy
together: local server first).

---

### 7. Decisions for the user

*Merged into the decisions table at the top of this page, which is the one to answer.*

| # | decision | options | recommendation |
|---|---|---|---|
| 1 | **How to build it** | (a) copy BC; (b) S1 `Instance` first; (c) extract BC, share the five pieces, DS in its own files | **(c)** |
| 2 | **Schedule** | (a) WZ's own timetable (A); (b) OM's every 4 h from midnight, 25 min (what the herald and travel card already show); (c) hourly like BC, offset so they never overlap (e.g. hh:55) | WZ's numbers if A finds them; with 200 players, a rarer event fills its squares. Make it `RealmConfig::square` either way |
| 3 | **Shared start or on entry** | (a) every player in a square starts together at the window's close (MU); (b) BC's way, the run from the first entry | **(a)**: a ranked contest needs one start. Leave BC as it is unless the user wants it the same |
| 4 | **Number of squares** | 4 (0.95d/0.97d), or 1 first | build the engine for 4; ship square 1 first, then 2-3; square 4 waits for its missing breeds |
| 5 | **Solo and few players** | (a) waves as MU's regardless; (b) wave size scaled by players present (ours, as BC's quotas scale) | (a) first; ask after a bot run whether one player is swamped |
| 6 | **Rank rewards** | (a) WZ/OM rank table as is; (b) a better win as BC got (exp x2, Zen x5, jewels, runes by square) | (a), then judge with bots; BC's "better win" was asked after playing |
| 7 | **Party and score** | (a) the killer alone scores (OM); (b) party members in view share | A's rule; the engine supports both |
| 8 | **Ticket drop rate** | (a) WZ's; (b) OM's (1% a bracket), as BC's "ours" choice of 2026-10-04 | (b), to match BC |
| 9 | **Death inside** | out to Noria, the run lost (MU), or revive in the square | out to Noria, as BC |
| 10 | **Music** | `devil_square_intro.mp3` / `outro` exist in Data/Music; or silence | the bed during the fight only (memory: music is rare and in fights) |
| 11 | **Prediction in the square** | keep the client's predicted realm (a `MapRow` flag) or turn it off as BC does | keep it; DS's grid never changes |
| 12 | **Player cap a square** | 10 (A to confirm), or none as BC has today | 10, and give BC the same cap while the code is open |
| 13 | **Firecracker in the square** | already 1 in 300 on map 9 (`items.h:1171-1180`) | keep |

---

### Side findings

1. **A party in Blood Castle fills its quotas too fast and can drop several Archangel weapons.**
   `Realm::kill` calls `countPartyKill` (`realm_fight.cpp:~1921`), which calls `countKill` once
   for **each party member in view** (`realm_party.cpp:72-78`). `countKill` starts with
   `castleKill(dead)` (`realm_quests.cpp:185-187`), which has no guard. So in a party of three
   standing together:
   - one road kill counts 3 toward `run_.kills`, while `killsWanted` is 40 x living players;
   - one sorcerer kill counts 3 toward `sorcerers`;
   - the statue lays its weapon three times (`realm_move.cpp:897-905`).
   - Fix: call `castleKill` once from `kill`, not from `countKill`. Hook DS's score the same way.
     Untested by a sim test, which raises one player.
2. **The end of a castle sends only the first player out** on the server (`main.cpp:1204-1216`,
   "a known limit of sprint 19"). Others get home only because their own client sees `sentOut`
   and reconnects, and `placeOf` rewrites the event map to its town. Fix it while generalising.
3. **No player cap in a castle world** (`main.cpp:977`: `castle > 0` skips the cap; no "full"
   refusal, WZ result 5).
4. **The run starts on the first entry**, so a later entrant joins a run already under way (3.5).
   Fine for one player, odd for a party that does not arrive together.
5. **OM's DS rank uses `OrderBy` (ascending)** (`DevilSquareContext.cs:62`), which would rank the
   lowest score first. For Part A to check against WZ.
6. **Structure-plan M7's size ratchet does not exist** (`tools/sizecheck.py` missing). `brain.h`
   is 4689 lines and `sim_test.cpp` 12233, so both are hubs in practice.
