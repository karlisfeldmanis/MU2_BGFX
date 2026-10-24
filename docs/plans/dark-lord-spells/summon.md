# Summon (skill 63) -- research dossier

Researched 2026-10-24 before any build (the user: "we do very deep reserach for each DL spell then we make it").
Nothing here is built. Drafted by a research agent from the sources below; every number is read, not recalled.

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`.
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- MU time: one reference tick = 1/25 s. Our realm: 20 ticks/s.

Names: WZ `AT_SKILL_RECALL_PARTY` (63, "파티 소환"), MM `AT_SKILL_PARTY_TELEPORT` (the enum after
`AT_SKILL_EARTHSHAKE`, `MM/Core/Globals/_enum.h:363`), OM `SkillNumber.Summon` = 63
(`OM/Persistence/Initialization/Skills/SkillNumber.cs:73`), skill.txt "셔먼" / "Summon", the scroll
"Scroll of Summon". It is **not** the elf's `AT_SKILL_SUMMON` (30, `MM/_enum.h:344`), which is the monster
summon. No buff, no state, no icon: the only lasting thing is a per-member countdown on the server.

## 1. MU's rule (server)

### The row
`WZD/lang/main/skill.txt:68` (same in `lang/kor/skill(Kor).txt:52`, named "Summon"):
`63 "셔먼" 98 0 70 30 0 30000 34 400 -1 -1 0 ... DL=1 ... duration 0 ... skilltype 0`

| Field | Value | Source |
|---|---|---|
| Level needed | **98** | row; checked when learning (`WZ/user.cpp:28426-28430`) |
| Damage | 0 | row |
| Mana / AG | **70 mana, 30 AG** | row; spent in `UseSkill` after `RunningSkill` returns TRUE (`WZ/ObjUseSkill.cpp:1008-1032`) -- and Summon's case **always** returns TRUE (`:1170` breaks to `return TRUE` at `:1322`), so a refused cast (no party, wrong map, open window) **still costs the mana and AG** |
| Distance | 0 (no target; the packet's target is himself) | row |
| Delay | **30000 ms** in the row -- **not enforced by the server**: `CSkillDelay::Check` is `return TRUE;` before its body (`WZ/SkillDelay.cpp:28-31`). The client enforces it (§3) | row |
| Energy needed | 20 + 34·98·4/100 = **153** | formula `WZ/MagicDamage.cpp:663-677`; OM `energyRequirement: 153` (`OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:182`) |
| Command needed | **400** | row; `gObjMagicAdd` check `WZ/user.cpp:28114-28125`; OM `leadershipRequirement: 400` (:182) |
| Duration column | 0 | row |
| Skill type | **0** = ordinary (Critical Damage is 1, a buff) | row; header `skill.txt:12` |
| Scroll | (12,22): `case 22: skillnumber = AT_SKILL_RECALL_PARTY` | `WZ/MagicInf.cpp:183`; OM "Scroll of Summon", level 98, energy 34 raw, Command 400, **375,000 zen** (`OM/Persistence/Initialization/VersionSeasonSix/Items/Orbs.cs:58`) |

The client's own table agrees: `MMD/Local/Skill.bmd` record 63 (80-byte records, `BuxConvert` XOR FC CF AB,
`MM/Core/Globals/_crypt.h:4-10`) decodes to level 98, damage 0, mana 70, AG 30, distance 0, **delay 30000**,
energy 34, Command 400.

### How it is cast
- Client sends 0x19 (`SendRequestMagic(Skill, HeroKey)`, target = himself) and plays **nothing** locally
  (`MM/GameLogic/Combat/ClassAttack.cpp:845-852`); no weapon needed (`:497`, the any-hand list).
- Server: `CGMagicAttack` (`WZ/protocol.cpp:15185`). Its safe-zone block only turns away Hellfire 2's start
  (`:15211-15224`); `gObjCheckAttackArea` fails on a safe tile (`WZ/user.cpp:29819-29823`, return 6) but the
  caller only logs it and goes on unless the speed-hack-ignore flag is set (`WZ/protocol.cpp:15230-15299`).
  So **Summon can be cast in a safe zone** -- and then everyone lands on his own tile (below).
  The duel refusal at `:15345-15351` is under `ADD_NEWPVP_PKFIELD`, **not defined** in 1.00.93 (no `#define` anywhere).
- `UseSkill` -> `RunningSkill` -> `SkillRecallParty(aIndex, lpMagic->m_Level)` (`WZ/ObjUseSkill.cpp:1170`).

### The handler, in order (`WZ/ObjUseSkill.cpp:3928-4119`)
Compiled with `DARKLORD_WORK` (`WZ/define2.h:3298`), `MODIFY_DARKLORD_SKILL_RECALL_PARTY_20040831` (`:2662`),
`MODIFY_DARKLORD_RECALL_IN_CASTLESIEGE_TIME_20051219` (`:2122`), `MODIFY_DARKLORD_RECALL_BUGFIX_20051103` (`:2169`),
`MODIFY_RECALLSKIL_BUGFIX_20081107` (`:666`), `MODIFY_RECALL_PARTY_SKILL_SUMMONER_BUGFIX_20080829` (`:709`),
`ADD_RAKLION_20080408` (`:971`); `ADD_NEWPVP_PKFIELD` off.

1. A non-character with no summoner returns (`:3934-3935`). **No class check** -- `//if( lpObj->Class != CLASS_DARKLORD )` is commented out (`:3982`); knowing the skill is the gate.
2. **`RecallPartyCheck`** (`:3892-3926`): if any other member **on his map** already has a countdown running, refuse
   with message 1238 "a party member is using the skill" (`:3938-3942`). Members on other maps do not block.
3. **The lord's map** -- `gMoveCommand.CheckMainToMove(lpObj)` (`:3946-3952`, message 1249 "this skill cannot be used on
   this map"). False in **Blood Castle** (11-17, +BC8), **Chaos Castle** (18-23, +CC7), **Kalima**, **Devil Square**,
   **Kanturu's boss map 39**, and **Illusion Temple while he carries a relic** (`WZ/MoveCommand.cpp:257-297`; Kanturu
   `:279-284`).
4. **The lord's windows** -- `CheckInterfaceToMove(lpObj)` (`:3954-3960`, message 1239 "close the window or personal shop
   you are using"): any `m_IfState.use` (trade, vault, NPC shop, chaos box...) or his own personal shop open
   (`WZ/MoveCommand.cpp:357-369`).
5. **Raklion's boss map** while closed, all dead, notify-4 or ended: refused, message 3417 (`:3963-3980`).
6. **The echo**: `GCMagicAttackNumberSend(lpObj, 63, lpObj, 1)` (`:3990`) -- the animation for him and his viewport. Sent
   whether or not anyone will come, **even with no party** (then the loop below is skipped and nothing else happens).
7. **Each member** of his party (`MAX_PARTYUSER`, `:3995-4111`), himself skipped (`:4017`):
   - (duel: `:4004-4015`, compiled out.)
   - **Castle Siege in progress**: a member of the other side is skipped silently (`:4020-4033`).
   - **Level**: `movelevel = GetMoveLevel(lord's map, lord's X, lord's Y, member's class)` (`:4039`) -- the
     **MoveLevel.txt** row whose rectangle holds the **lord's** tile, two thirds (`*2/3`, integer) for a Dark Lord or Magic
     Gladiator member, **-1 if no row covers that tile** (`WZ/MoveCommand.cpp:216-239`). Needs `member.Level >= movelevel`
     and `movelevel != -1`.
   - **The member's map** -- `CheckMainToMove(member)` (`:4043`): a member inside Blood/Chaos Castle, Kalima, Devil
     Square, Kanturu 39 or holding an Illusion Temple relic is **not pulled out**.
   - **The member's gear for the lord's map** -- `CheckEquipmentToMove(member, lord's map)` (`:4046`;
     `WZ/MoveCommand.cpp:299-355`):
     - into **Atlans** (7): not riding a **Uniria (13,2) or a Dinorant (13,3)** (`:303-317`);
     - into **Icarus** (10): wearing wings 12/0-6, the Cape of Lord 13/30, Dinorant 13/3, Fenrir 13/37 (or, with the
       2008 fix, 12/36-43 or the Dark Horse 13/4) **and** not a Uniria and not a **Ring of Transformation 13/10** on
       either hand (`:319-344`); and, as a side effect **of the check itself**, his summoned monster is killed
       (`:346-351`) -- at the cast, before any countdown.
   - **The member's windows** -- `CheckInterfaceToMove(member)` (`:4049`).
   - All pass (`:4054-4055`): **`//if( lpPartyObj->MapNumber == lpObj->MapNumber )` is commented out (`:4060`) -- members on
     any map are summoned.** Then the landing tile (next), `SkillRecallParty_Time = 7`, map, X, Y written on the
     **member** (`:4083-4086`), a Raklion boss-map entry if that is the destination (`:4088-4091`), and **message 1251** to
     the member: "Summoning in process. It may be cancelled if you move." (`:4097-4098`; the English as the footage shows it, §5).
   - Any check fails: **message 1252** "cannot move" to that member (`:4104-4106`), nothing to the lord.
8. If anyone was set: **message 1244** "summoning %d people" to the lord (`:4113-4118`).

No check on: the member being alive (`gObj[].Live` is never read), the member's or lord's **PK** state, the member
being in a **safe zone** (he is pulled out of town), the lord being mounted, the member's distance (one standing
beside him is moved too), or the lord's own level against the map.

### The landing tile (`WZ/ObjUseSkill.cpp:4064-4081`)
- Picked **at the cast**, per member, off the **lord's** position: up to **50** tries of
  `X + (-4 + rand()%9)`, `Y + (-4 + rand()%9)` -- a **9 × 9 square, Chebyshev ≤ 4** -- accepted by
  `gObjCheckTeleportArea(lord, x, y)` (`WZ/user.cpp:29656-29690`):
  - within ±8 of the lord (always true here);
  - the tile's attribute is exactly `MAP_ATTR_NONE` -- **not safe zone (0x01), not occupied (0x02 stand), not wall (0x04),
    not hollow (0x08), not water (0x10)** (`WZ/MapClass.h:21-26`);
  - **and the lord himself not on a safe tile** (`:29683-29687`).
- No tile found (all 50 tries fail, which is certain when **the lord stands on a safe tile**): the member gets the
  **lord's own tile** (`:4077-4081`). So a summons cast in town stacks everyone on him.
- Two members may draw the same tile (the stand attribute is only set when they arrive). The tile is not re-checked at
  arrival 7 s later.
- **The destination is frozen at the cast**: if the lord walks, warps or dies afterwards they still land where he stood.

### The countdown (`WZ/user.cpp:25460-25510`, in `gObjSkillUseProc`, called once a second from `gObjSecondProc`, `:22556`, `:22608`)
- Each second, for a character with `SkillRecallParty_Time`: decrement. While still > 0 and **≤ 5**, **message 1243**
  "Will be summoned in %d seconds." (`:25503-25508`). So the member reads **5, 4, 3, 2, 1** -- the 6 is silent.
- At 0: the three checks again on the member -- `CheckMainToMove`, `CheckEquipmentToMove(member, saved map)`,
  `CheckInterfaceToMove` (`:25466-25485`). Any false: **message 1254** "summon cancelled". Else `gObjRecall` (`:25488-25489`).
  Then map/X/Y/time zeroed (`:25491-25497`).
- The first decrement falls on the next whole-second pass, 0-1 s after the cast, so **they arrive 6.0-7.0 s after the cast**.
  Runs whether the member is alive or dead (`gObjSecondProc` walks every connected object, `:22590-22608`).

### What cancels it
| Event | Cancels? | Where |
|---|---|---|
| **The member walks** (any move packet) | **yes**, message 1254 | `PMoveProc`, `WZ/protocol.cpp:14110-14119` |
| The member takes a gate | yes, 1254 | `gObjMoveGate`, `WZ/user.cpp:26832-26840` |
| The member uses /move or a warp | yes, 1254 | `gObjMovePlayer`, `WZ/user.cpp:25988-25996` |
| The member opens a window, enters an event map, or changes gear so the destination refuses him | **at 0 only** (re-checked then), 1254 | `user.cpp:25466-25485` |
| The member **attacks or casts** in place | no (only walking is a move packet) | -- |
| The member's own Teleport (wizard) | no (`gObjTeleportMagicUse` has no cancel, `user.cpp:25893-25941`) | -- |
| **The member dies** | **no** -- the reset in `RemoveAllCharacterInvalidMagicAndSkillState` (`WZ/ObjUseSkill.cpp:4623`, called from `gObjUserDie`, `user.cpp:9237`) sits inside `#ifndef MODIFY_OLD_BUFF_EFFECT_SYSTEM_20071004` (`:4598-4662`), and that macro is on (`define2.h:1180`) | -- |
| The lord walks, warps, dies, logs out, leaves the party; the party breaks | **no** -- nothing on the lord holds the summons; the member keeps his own saved map/X/Y | -- |
| A second lord's cast | refused while any member on that lord's map counts (step 2) | `ObjUseSkill.cpp:3916-3920` |
| Logout/relog | the field is zeroed on a new object (`user.cpp:1974`) | -- |

### The move itself -- `gObjRecall` (`WZ/user.cpp:32453-32502`)
- **Another map**: X, Y, map set, path cleared, viewport cleared, `GCTeleportSend(member, -1, map, X, Y, dir)` -- a 0x1C
  whose `MoveNumber` (BYTE -1 = 255) becomes 1 (`WZ/protocol.cpp:16123-16148`), so the client **loads the new map**
  (§3); then **his respawn point is set to the landing** (`RegenMapNumber/X/Y`, `RegenOk = 1`, `:32474-32477`).
  (Typo: `TX` is written twice, `TX = y` at `:32460`, `TY` never.)
- **The same map**: a 0x19 **`AT_SKILL_TELEPORT`** from him to himself to him and his viewport (`:32481-32496`) -- the
  wizard's Teleport look -- then `gObjTeleportMagicUse(member, X, Y)` (`:32500`): stand attribute moved, viewport
  rebuilt (`user.cpp:25893-25941`).
- No map-server hop, no zen, no level re-check at arrival.

### Messages (server notices, `GCServerMsgStringSend(..., 1)`; text from the Korean comments, English where the footage shows it)
| # | To | Text |
|---|---|---|
| 1238 | lord | "A party member is already using the skill" (MsgOutput) |
| 1239 | lord | "Close the window or personal shop you are using" |
| 1249 | lord | "The skill cannot be used on this map" |
| 1244 | lord | "Summoning %d people" |
| 1251 | member | **"Summoning in process. It may be cancelled if you move."** |
| 1243 | member | **"Will be summoned in %d seconds."** (5 to 1) |
| 1252 | member | "Cannot move" (refused at the cast) |
| 1254 | member | "Summon cancelled" |
| 3417 | lord | Raklion boss zone closed |

### OpenMU differs (`OM/GameLogic/PlayerActions/Skills/SummonPartySkillPlugin.cs`)
| | WebZen 1.00.93 | OpenMU | wins |
|---|---|---|---|
| Countdown | 7 s, the member reads 5..1 as notices | **5 s** (`:23`), "Summoning in {n} second(s)..." as **party chat** each second (`:58-68`) | WZ |
| Who holds it | each member (his own timer, map, tile) | the lord (one task, `:44-47`) | WZ |
| Cancelled by | **the member walking**, a gate, a warp; re-checks at 0 | **the lord** dying, standing on a safe tile at any second (`:70-74`), teleporting or changing map (`OM/GameLogic/Player.cs:775`, `:830`); members walking do not cancel | WZ |
| Landing | per member, frozen at the cast, 9 × 9 (±4), tile must be attribute-free (no safe zone, no body), else his own tile | at the end, off the lord's position then, **5 × 5 (±2)**, 10 tries, `WalkMap` only (safe tiles allowed), else his tile (`:98-121`) | WZ |
| Safe zone | cast allowed; all land on him | cast allowed but **cancelled** at the first second | WZ |
| Level | MoveLevel.txt rectangle under the **lord**, 2/3 for DL/MG | the lowest warp-list level for the map, total level (`:161-172`) | WZ |
| Gear | Atlans no Uniria/Dinorant; Icarus flight, no Uniria, no transformation ring | the map's own requirement (`TryGetRequirementError`, `:123-127`) | WZ |
| Events | lord and member refused in BC/CC/DS/Kalima/Kanturu 39/IT-relic; Raklion closed | only "not in a mini game" (`:133-153`); the rest a todo (`:139-144`) | WZ |
| No party | mana spent, animation, nothing | refused before the mana (`:32-40`, `:135`) | WZ (but see §7) |
| Cooldown | none on the server (client's 30 s) | none in the plugin | -- |
OpenMU's own landing check has a bug: it tests `player.Position.EuclideanDistanceTo(targetPoint) < 6` on the
unchanged `targetPoint` (`:116`), always 0.

## 2. Where it works: the maps

`WZD/MoveLevel.txt` is the whole table this build ships (`WZ/Gamemain.cpp:3285` loads it; `WZ/MoveCommand.cpp:175-214` reads
level, map, x1, y1, x2, y2). **A lord standing on a tile no row covers summons nobody** (every member gets 1252).

| Lord's map (area) | Member level needed | DL/MG member | File line |
|---|---|---|---|
| Lorencia 0 | 30 | 20 | `MoveLevel.txt:2` |
| Noria 3 | 35 | 23 | `:3` |
| Devias 2 | 40 | 26 | `:4` |
| Dungeon 1 (four rectangles) | 20 / 40 / 40 / 50 by area | 13 / 26 / 26 / 33 | `:5-8` |
| Atlans 7 | 75 (+ no Uniria/Dinorant) | 50 | `:9` |
| Lost Tower 4, floors 1-7 | 90, 95, 100, 105, 110, 115, 120 | 60 ... 80 | `:10-16` |
| Tarkan 8 | 130 | 86 | `:17` |
| Icarus 10 | 170 (+ flight) | 113 | `:18` |
| Exile 5 | 180 | 120 | `:19` |
| **Aida 33, Kanturu 37/38/39, Crywolf, Elbeland, Swamp, Raklion, Blood Castle, Devil Square...** | **no row: nobody comes** (and 9/11-17/39 are refused for the lord anyway) | | -- |

Two cautions. The file is the server-files copy found in `ws`, possibly older than the GameServer source (the source
carries Swamp of Calmness and Raklion branches, `MoveCommand.cpp:225-228`, that no row here reaches); a later WebZen
MoveLevel.txt likely lists Aida and Kanturu. And the levels are **map-entry** levels, not the lord's: the member must
qualify for the place he is pulled to.

Cross-map: **yes**, any map to any map, subject to the rows above -- the same-map test is commented out
(`ObjUseSkill.cpp:4060`), the destination is the lord's map, and `gObjRecall` changes map (`user.cpp:32455-32477`).
`docs/worlds/kanturu-port.md:621` ("not allowed into map 39") is right, and the rule is wider: the **lord** may not cast
in 39 (`CheckMainToMove(lord)`, `ObjUseSkill.cpp:3946`) and a member **in** 39 is not pulled out (`:4043`); MoveLevel has
no Kanturu row at all.

## 3. Client (MuMain S6, cross-checked with 1.03.35)

### The cast gate (hero only)
- Refused, nothing sent, when: **no party** (`PartyNumber <= 0`, `MM/ClassAttack.cpp:516-519`), a **duel** is on
  (`:526-529`), in **Doppelganger** (`:531-534`), mana or AG short (`:565-577`), **the 30 s delay not run out**
  (`gSkillManager.CheckSkillDelay`, `:579-582` -> `MM/GameLogic/Skills/SkillManager.cpp:140-171`: sets
  `SkillDelay[slot] = 30000` ms at the press, before the server answers, and greys the slot from the bottom,
  `MM/UI/NewUI/HUD/NewUIMainFrameWindow.cpp:2530-2552`), or the Command short (`SkillManager.cpp:163-167`).
- In **Illusion Temple** it is sent **only if the hero carries the relic** `ITEM_POTION+64` (`MM/ClassAttack.cpp:846-850`),
  the very case the server refuses (step 3) -- in practice never in IT.
- The skill bar greys it with no party or in Doppelganger (`NewUIMainFrameWindow.cpp:2165-2173`); the tooltip adds
  "Can only be used during party" in red (`MM/UI/NewUI/HUD/Skills/SkillTooltipModel.cpp:548-553`).

### When it plays
- **On the server's 0x19 echo**, for him and his viewport (`ReceiveMagic`, `MM/Network/Server/WSclient.cpp:4038`; the target
  is himself, so no swivel). Case `AT_SKILL_PARTY_TELEPORT` (`:4702-4719`), `AttackTime = 1`:

| Mount | Action | MuMain raw # | Keys in player.bmd | Play speed (fixed, no magic speed) | Clip |
|---|---|---|---|---|---|
| on foot, or any mount in a safe zone | `PLAYER_ATTACK_TELEPORT` | **81** | 8 | 0.28 (`MM/Engine/Object/ZzzCharacter.cpp:1011`) | to key 5.5 in 0.79 s, then the hold |
| Uniria, Dinorant or Dark Horse (`MODEL_HORN_OF_UNIRIA..MODEL_DARK_HORSE_ITEM`), not safe | `PLAYER_ATTACK_RIDE_TELEPORT` | **83** | 9 | 0.30 (`:1012`) | 0.73 s, then the hold |
| Fenrir, not safe | `PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT` | **94** | 7 | 0.30 (`:1014`) | 0.73 s, then the hold |

  Raw numbers counted from `MM/Core/Globals/_enum.h:2902-3064` (PLAYER_SET = 0, the alias handled; the same count gives
  STRIKE 80, HAND1 146, FLASH 152, as the other dossiers). Keys from `content/players/rig/player.rig.json`.
  `actions.json` labels them one high (`82` "PLAYER_ATTACK_TELEPORT" at play_speed 0.2, `84` ride, `95` Fenrir), as Electric
  Spike's dossier found from 75 up; MuMain's 0.28 is the speed. **81 is not on `pipeline/cook.py:2324`'s clip list** (82 is,
  as Force's ride strike).
- **The hold**: past key 5.5 the clip plays at **one tenth** speed (`PlaySpeed *= 0.1`, `MM/ZzzCharacter.cpp:2502-2507`) -- on
  foot the last 2.5 keys take 89 ticks, **3.6 s** (ride 3.5 keys, 4.7 s; Fenrir 1.5 keys, 2.0 s). He stands with the arm up
  for most of the circle's life unless he moves.
- **While held**, every tick past key 5.5: a `BITMAP_LIGHT` particle at **bone 42** (`hand_bofdgne01`, a child of
  `Bip01 L Hand`, the rig's bone 42), light **(0.3, 0.5, 1.0)** blue (`MM/ZzzCharacter.cpp:4122-4131`). Particle
  `Effect/flare01.jpg` (`MM/Engine/Object/ZzzOpenData.cpp:5195`), life 10-19 ticks, scale 0.5-1.0
  (`MM/Render/Effects/ZzzEffectParticle.cpp:3159-3164`), drifting and rising 2.5 units a tick
  (`:7941-7960`): a blue glitter off the raised left hand.

### The let-go: 0.2 s after the echo
`AttackStage` case (`MM/ZzzCharacter.cpp:2904-2910`): once `AttackTime > 5` and the action is one of the three, it jumps to
15 = `g_iLimitAttackTime` (`:172`), and the let-go runs (`:4133-4140` -> `:4385-4390`). **5 ticks = 0.20 s** after the echo, at
about key 1.5 of the clip -- the circle is down before the arm is up.

### The let-go: two circles at his feet, and the sound (`MM/ZzzCharacter.cpp:4385-4390`; 103 `ZzzCharacter.cpp:5160-5165`)
- `CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light, 2, o)` -- `Data/Skill/Circle01.bmd`
  (`MM/ZzzOpenData.cpp:4163`), **the same flat disc Hellfire lays** (400 units across the radius, 41 up --
  `content/effects/hellfire/Circle01.json`), but subtype 2 **swaps its texture for `BITMAP_MAGIC_EMBLEM` =
  `Effect/Magic_b.jpg`** (`MM/ZzzOpenData.cpp:5190`) and lights it **(0.5, 0.5, 1.0)** blue
  (`MM/Engine/Object/ZzzObject.cpp:1540-1544`), `RENDER_TEXTURE`, mesh 0 blended (`BlendMesh = 0`).
  - Create (`MM/Render/Effects/ZzzEffect.cpp:2137-2139`, `:2154-2158`): **LifeTime 250 (10 s)**, Scale 1.
  - Move (`MM/Render/Effects/Behaviors/MoveHandlers.cpp:3760`, `:3787-3797`): `BlendMeshLight` = (250 − life)·0.1 over the
    first 10 ticks, then life·0.1 -- i.e. above 1 (saturated) from 0.4 s until the last 0.4 s: **a full-bright blue
    eight-pointed sigil for ~9.2 s, fading in and out over 0.4 s each end.** It does not follow him (no owner copy).
- `CreateEffect(MODEL_CIRCLE_LIGHT, o->Position, o->Angle, o->Light, 3)` -- `Data/Skill/Circle02.bmd` (`:4164`), Hellfire's
  **ring wall** (about 320 units out, 41 to 456 up, `content/effects/hellfire/Circle02.json`) with its own `magic_a02`
  sheet but lit **(0.1, 0.1, 10)** -- a deep saturated blue (`ZzzObject.cpp:1534-1538`).
  - Create (`ZzzEffect.cpp:2165-2167`, `:2176-2180`): **LifeTime 250**, Scale 1.
  - Move (`MoveHandlers.cpp:3831`, `:3865-3897`): light (250 − life)·0.1 for 10 ticks then life·0.1, **capped at 0.5**; sheet
    scrolled `U = −life·0.01`; terrain light white × that light, **4 tiles** round. **No EarthQuake, no stones** (those are
    Hellfire's subtype 0, `:3840-3863`).
  - **One tick in two** while life > 30 (`rand_fps_check(2)`, `MM/Engine/AI/ZzzAI.cpp:706-709`): at a random point within
    **200 units** round him --
    1. a **`BITMAP_FLARE_BLUE` particle** subtype 0 (`Effect/flareBlue.jpg`, `ZzzOpenData.cpp:5318`): life 30-39, light white,
       rising from z-velocity 0-2 by +0.4 a tick to 8 a tick, scale 0.2, dimming ×0.98 its last 5 ticks
       (`ZzzEffectParticle.cpp:165-171`, `:3949-3962`, sprite `:9140-9144`) -- blue sparks floating up off the sigil;
    2. while life > 40 (the first 8.4 s), a **`BITMAP_FLARE_BLUE` joint subtype 19** 600 units above that point, angle (·, ·, 45),
       scale 40, `MaxTails` 20 (`MM/Render/Effects/ZzzEffectJoint.cpp:1670-1673`, `:1847-1853`): life 25-74; it **hangs** until its
       life is 25, then **falls**: z-speed −(35..54) growing 5 a tick (`:5514-5523`) -- 600 units in about 9 ticks (0.36 s), a
       20-tail ribbon. These are **the pillars of blue light that rain onto the sigil** in every clip (§5).
- `PlayBuffer(SOUND_PART_TELEPORT, o)`: **`Data/Sound/eSummon.wav`**, one voice (`MM/ZzzOpenData.cpp:4871`) -- 5.0 s, stereo
  22,050 Hz (`afinfo`).
- Nothing is drawn on the members at the cast. No countdown is drawn; the member only reads the server's notices.

### On each member when he arrives
- **Same map** -- the server's 0x19 `AT_SKILL_TELEPORT` (`MM/WSclient.cpp:4229-4242`) -> `CreateTeleportBegin`: the wizard's
  `PLAYER_SKILL_TELEPORT`, alpha to 0, a `BITMAP_SPARK + 1` pillar, `SOUND_MAGIC` (`Data/Sound/sMagic.wav`,
  `ZzzOpenData.cpp:4793`) (`MM/Render/Effects/ZzzEffectMagicSkill.cpp:153-161`); and at the new tile `CreateTeleportEnd`
  (`:163-172`; called from `ReceiveTeleport` flag 0, `WSclient.cpp:2127-2130`) -- the same pillar, fading in. Our
  `src/game/fx/blink.h` draws exactly this.
- **Another map** -- 0x1C flag 1 (`ReceiveTeleport`, `MM/WSclient.cpp:2089-2250`): the world is loaded (`:2141`),
  "Welcome to %s" in the log (`:2192`), the map name shown, `CreateEffect(BITMAP_MAGIC + 2, ...)` -- `Effect/Magic_Circle1.jpg`
  (`ZzzOpenData.cpp:5198`) -- at his feet (`:2220`) and alpha from 0. That is the ordinary map-arrival look (gates, warps).
- Others on the lord's map see a stranger's arrival as a viewport create, nothing special.

### Files (all confirmed in MMD)
| Thing | File | In our content? |
|---|---|---|
| sigil disc | `Skill/Circle01.bmd` | **yes**, `content/effects/hellfire/Circle01.obj` (Hellfire's) |
| sigil sheet | `Effect/Magic_b.OZJ` (`BITMAP_MAGIC_EMBLEM`) | **no** |
| ring wall | `Skill/Circle02.bmd` + its `magic_a02` | **yes**, `content/effects/hellfire/Circle02.obj`, `magic_a02.png` |
| rising sparks / falling pillars | `Effect/flareBlue.OZJ` (`BITMAP_FLARE_BLUE`) | yes, as `content/effects/defense/flare_blue.png` (check it is this sheet) |
| hand glitter | `Effect/flare01.OZJ` (`BITMAP_LIGHT`) | yes, `content/effects/light/flare01.png` |
| sound | `Sound/eSummon.wav` (5.0 s) | **no**, not in `content/sounds/sounds.json` |
| member same-map | `BITMAP_SPARK + 1`, `sMagic.wav` | yes (`fx/blink`, sounds.json:543) |
| member other map | `Effect/Magic_Circle1` | yes, `content/effects/warp/magic_circle.png` |
| icon | the skill icon only (no buff) | check `pipeline/skill_icons.py` |

## 4. 1.03.35 differences
None found. The cast, the clip choice (`MODEL_HELPER+2..+4` = Uniria/Dinorant/Dark Horse, `+37` Fenrir), the AttackStage jump,
the two circles and the sound are line for line the same: `103/WSclient.cpp:5718-5737`, `103/ZzzCharacter.cpp:3248-3254`,
`:5160-5165`, play speeds `:1161-1165`, the tenth-speed hold `:2799-2804`, the hand glitter `:4835-4843`; circles
`103/ZzzEffect.cpp:2697-2747` (create), `:12071-12134` (MODEL_CIRCLE move), `:12139-12215` (MODEL_CIRCLE_LIGHT move),
render `103/ZzzObject.cpp:1686-1696`; files `103/ZzzOpenData.cpp:5693-5694`, `:8485` (eSummon), `:9226` (Magic_b),
`:9402` (flareBlue). The IT relic gate `103/ZzzInterface.cpp:6914-6921`. 1.03.35 rolls `rand()%2==0` where MuMain calls
`rand_fps_check(2)` -- the same rate at 25 fps.

## 5. Footage

Clips:
- `YTOOZprY2_U` "Summon DL" (Francis Koji Okuaki), 97 s, 720p at 10 fps, a Season 6-era client: **two clients side by side**,
  the member (left, on a Dark Horse) and the lord (right), both in the Dungeon -- the clearest record found, with the
  server's notices legible.
- `4l5vVc1iOog` "Mu Online Dark Lord Skill Summon" (Webzen's own "Mu Online" channel), 21 s, 720p (4:3 letterboxed), a modern
  client, at a training dragon; `hZwHoFfXrJ0` is its 4 s cut.
- `wSRZkIlMJZg` (Arcana, a custom "Season 2" server), a summons in Kanturu among a crowd -- seen, not used in the sheet
  (custom server; disc to arrival there ≈ 5.5 s).
- Searched and rejected: `OoXPhdXOpxc`, `3E64SbZL9PM` (too crowded to read).

Stills (`summon-footage.jpg` beside this file; top row and the first two of the bottom row from `YTOOZprY2_U`, the last two
from `4l5vVc1iOog`):
1. **21.75 s** before: the lord stands alone in the Dungeon; a white glow at his raised hand.
2. **22.25 s** the cast: the **blue eight-pointed sigil** appears at once, full size, centred on him -- about eight tiles across,
   points like long blue blades (Magic_b on Circle01's 400-unit disc).
3. **23.5 s** the first **pillars of light** fall onto it -- thin, bright cyan-white columns from off the top of the screen,
   two or three at a time (the subtype-19 joints).
4. **29.5 s, the member's own window**: the notices, top left -- *"Summoning in process. It may be cancelled if you move."*
   then *"Will be summoned in 5 seconds."* down to *"... in 1 seconds."* -- WebZen's 1251 and 1243, word for word. He stands
   still on his horse at Dungeon (7, 63); the sigil's tips reach into his screen (the lord at (12, 63), five tiles off).
5. **30.0 s** arrived: the member stands **below** the lord, about three or four tiles away, at (16, 60) by his window's
   title -- inside WZ's ±4. **About 7.3 s after the sigil appeared.** No pillar or flash is visible on him at 10 fps.
6. **32.0 s** the sigil gone (≈ 9.75 s of life, MU's 250 ticks); a last pillar or two still falling.
7. (Webzen) the sigil from close up: a runic ring and the star's blades round him, one pillar on him, his arm up with a blue
   light at the hand.
8. (Webzen) a party member ("Admin", party bar at the left) has arrived at his side under the pillars.

The **ring wall (Circle02 at 0.5 and deep blue) is not seen** at these sizes -- or reads only as the faint blue haze round the
sigil. No sound checked. The countdown is **text only**: no number, bar or mark over anyone.

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

Reusable:
- **The circles are already cooked and drawn**: `src/game/fx/hellfire.h:1-60` draws MODEL_CIRCLE and MODEL_CIRCLE_LIGHT
  subtype 0 from `content/effects/hellfire/Circle01.obj` / `Circle02.obj`. Summon is the same two meshes with a new sheet on the
  disc (`Magic_b`), new lights, a 250-tick life, no quake, no stones, and the two blue flares.
- **Same-map arrival look**: `src/game/fx/blink.h` (CreateTeleportBegin/End). **Other-map arrival**: `src/game/fx/warp.cpp`
  (Magic_Circle1, `content/effects/warp`).
- **A party loop on the lord's map**: Critical Damage's (`src/sim/realm_skills.cpp:354-373`) and `Realm::partyNear`
  (`src/sim/realm_fight.cpp:2073-2083`) walk `bodies_` for `one.party == hero.party`.
- **Putting a hero down on his own map**: `Realm::setHeroDown` (`src/sim/realm_move.cpp:325-354`) -- rises him, drops his
  blow and orders, closes trading/banking/questing/machine, finds the nearest open tile within 8
  (`router_.nearestOpen(..., content::kWallCharacter, 8, ...)`), dismisses the elf's summon, says `What::Climbed`.
- **Sending a hero to another map**: the realm **says** it and the server lands him. `Realm::goBack` (`src/sim/realm_travel.cpp:363-384`)
  halts him, closes windows, dismisses the summon and says `What::WentBack` (map, column, row; `src/sim/realm.h:150`);
  the server's `landingOf` (`server/src/world.cpp:507-558`) turns `Gated`/`Answered Travel`/`WentBack`/`Warped`/`Rose` into a
  `Landing` and, after each step, puts it in `g_due` for that session (`world.cpp:720-730`, `server/src/host.h:132-139`); his
  next Hello is set down there (`world.cpp:324-326`). The client changes map off the same happening
  (`src/app/modes/play_mode.cpp:1066-1076`, `takeWentBack`, `src/game/play.h:1009`).
- **Map entry rules**: `sim::moveLevel` (two thirds for DL/MG, `src/sim/rules.h:299-303`), `sim::canFly` for Icarus
  (`src/sim/items.h:1440`, `MapRow::flight`, `src/sim/maps.h:53-54`), `Realm::travelRefusal` (`realm_travel.cpp:278-303`), event maps
  (`sim::eventOf`, `src/sim/event.h:21-26`; Blood Castle 11, Devil Square 9). The level a map asks is in the travel rows
  (`src/sim/travel.h`) and gates (`src/sim/gates.h:42`) -- not MU's MoveLevel rectangles.
- **Command** is a stat (`HeroPoints::command`, `src/sim/realm.h:384`).

What is different here:
- **Parties are the server's, not a realm's** (`src/sim/realm_party.cpp:1-4`; `server/src/host.h:331-360`, `g_partyOf`,
  `partyMates` at `server/src/party.cpp:10`): a realm is one map and knows only its own bodies' party number
  (`Command::Kind::PartySet`). A member on another map is in **another realm**. So a cross-map summons has to go **lord's realm ->
  server -> member's realm -> server -> client**, which nothing does today.
- Our cast refuses every skill on a safe tile (`src/sim/realm_skills.cpp:180-183`); MU lets Summon be cast in town.
- Our map table has Kanturu as 37 only (`src/sim/maps_table.inc`); 38/39 are not built, so the Kanturu refusal is future work.

Missing, and where each would go:
- **The row**: `skill::kSummon = 63` in `src/sim/skills.h` (beside `kCriticalDamage`, `:135`), a row on the end of the table in
  `src/sim/skills.cpp` (after Critical Damage, `:949-962`), `kSkills` 44 -> 45 (`skills.h:530`) and the kept-layout cools; level 98,
  mana 70, energy 153, Command 400; a column (`recalls`) that `onSelf()` counts (`skills.h:491`) and `aura()` does not (`:494`); the
  clip **81** (83/94 for the mounts later) on `pipeline/cook.py:2324`'s list so `castTicks` knows it.
- **The lord's side** (`Realm::throwSkill`, the `onSelf` block): the refusals (no party, event map, open window, a member on his map
  already counting), then **say** a new `What::Recalling` (lord, map, column, row) once.
- **The server's side**: on `What::Recalling` in the step loop beside `landingOf` (`server/src/world.cpp:720-730`), for each of the
  lord's party mates (`partyMates`), queue a new `Command::Kind::Recall` (lord's map, tile) into **that mate's** world
  (`src/sim/command.h`, applied in `src/sim/realm_commands.cpp`), and tell the lord how many (1244).
- **The member's side** (in his own realm): the checks (level by `moveLevel` against the lord's map, flight for Icarus, not in an
  event, no open window), a landing tile, a **7-second countdown on `Player`** (beside `wayBack`), the five notices, the cancels (his
  walking order, a gate, Tab travel, Go Back!, Town Portal -- the places that already clear `gating`/`trading`), the re-check at 0; then
  **same map**: `setHeroDown`-like with the Blink look; **another map**: halt, close, dismiss the summon, `say(What::Recalled, map, column,
  row)` -> a new `case` in `landingOf` -> `g_due` -> the client's travel as `WentBack`'s.
- **The landing tile across maps**: the member's realm cannot see the lord's grid. Either the lord's realm picks the tiles (one per
  member, WZ's 9 × 9 attribute-free rule against its own `tables_->grid`) and says them, or the landing realm sets the arriving Hello
  down with `nearestOpen` near the lord's tile.
- **Presentation**: a `src/game/fx/summon.*` (two circles off Hellfire's meshes, the flares, the hand glitter, the 4-tile light), a
  cast hook in `src/game/play.cpp` beside Critical Damage's, the countdown notices in the HUD log.
- **Content**: `Effect/Magic_b` into `content/effects/` (new sheet), `eSummon.wav` into `content/sounds/sounds.json` (the showing cook),
  the skill icon, and the scroll (12/22) in every world's tables (plan §6 names it the Tersia LT4 reward, `docs/plans/dark-lord.md:125`).

## 7. Open calls (for the design; not decided here)

1. **Cross-map or this map only?** WZ pulls from every map (subject to level, gear, events); that needs the server relay above.
   A same-map-only first cut is all-realm and small.
2. **Countdown length and who holds it**: WZ's 7 s (6-7 s real) on each member, OpenMU's 5 s on the lord. Our plan row says 7 s.
3. **What cancels**: WZ -- the **member** walking (or a gate/warp; open window/event/gear at 0), never the lord; OpenMU -- the
   lord dying, standing in a safe zone, or moving maps. A member who keeps fighting in place still comes in WZ.
4. **Death**: WZ does not cancel for a dead member (a bug by the look of it: the reset is compiled out) and moves his respawn
   point to the landing. Cancel on death here?
5. **Where they land**: WZ's frozen-at-cast 9 × 9 (±4) on tiles free of safe zone, bodies, walls, water -- or our plan's "within 4
   tiles" as our open-tile search does it. And the safe-zone case: WZ lets him cast in town and stacks everyone on his tile; our
   throwSkill refuses every skill on a safe tile.
6. **Level rule**: MU's MoveLevel rectangles (with holes: Aida, Kanturu, Crywolf... have no row, so nobody comes) or ours -- the
   map's travel/gate level by `sim::moveLevel` (two thirds for DL/MG), wings for Icarus, as Tab travel asks.
7. **Event maps**: refuse the lord casting in, and members being pulled out of, Blood Castle, Devil Square, (later) Chaos Castle,
   Kalima, Kanturu 39, Illusion Temple; and the Castle Siege side rule (no siege here). Also Doppelganger/duels (client-side in MU).
8. **No party**: MU's client refuses (and greys the key); MU's server would take the mana. Refuse without cost here?
9. **Cost and cooldown**: MU 70 mana + 30 AG, and a **30 s** delay that only the client enforces; we have no AG and every QWER skill
   has a cooldown.
10. **Notices**: MU's five "Will be summoned in N seconds." lines are plain notices; ours could be the same lines in the log, a
    countdown chip on the member, or both. And the lord's "Summoning N people".
11. **The clip**: raw 81 `PLAYER_ATTACK_TELEPORT` (8 keys at 0.28, a tenth-speed hold past key 5.5 for 3.6 s) is not cooked for
    `castTicks`; the circle lands 0.2 s after the echo, before the arm is up. Keep the hold (he stands with the arm raised while the
    pillars fall)?
12. **The look's size**: an eight-tile blue sigil for 10 s with pillars raining in -- loud for a utility skill next to "monster
    auras are subtle"; the ring wall at 0.5 deep blue. Keep MU's 10 s, or cut to the countdown's 7?
13. **eSummon.wav is 5 s long** -- keep it whole?
14. **Learning**: level 98, energy 153, Command 400 from the Scroll of Summon 12/22 (375,000 zen in OpenMU); plan §6 has it as the
    Tersia LT4 reward.
15. **Bots and the Helper**: should party bots answer a summons (and stand still for it), and may the Helper ever cast it? (Proposed
    never.)

## 8. Decided

Settled 2026-10-24 from sections 1-7 (re-checked against WebZen: `skill.txt:68`, `ObjUseSkill.cpp:4040-4095`,
`user.cpp:25460-25510`, `:29656-29690`, and the cancels at `protocol.cpp:14110`, `user.cpp:25988`, `:26832`) and the user's
"DL has to be true on actual MU":

1. **This map first, then across maps, in the same release.** The sim and look are judged on one map; the server relay
   (`What::Recalling` -> `Command::Kind::Recall` -> the member's realm -> `landingOf`) is the second step.
2. **WebZen's 7 seconds on each member** (`kRecallTicks` 140), his own clock; "Will be summoned in N seconds." at 5..1.
3. **Cancels: a step of his own** (`Body::walking` -- a click to walk, or walking in to a fight; striking in place does
   not), a gate, Tab travel, Go Back! or a Town Portal (he leaves the realm). Nothing the lord does after the cast.
4. **His death cancels it** (ours: WebZen's reset is compiled out, a leftover); the respawn point is never moved.
5. **The tile is WebZen's**: drawn at the cast and kept; 50 draws in the 9 x 9 round the lord on a tile that is open,
   not safe and not stood on, none while the lord stands on a safe tile. **Ours where none is found**: the nearest free tile
   round him ring by ring, never stacked on his (WebZen stacks everyone on him, always in town).
   **Cast in town, as MU allows** -- the one skill past our safe-tile refusal.
6. **Level**: on his own map nothing is asked (he already stands there). Across maps: `sim::moveLevel` of the lord's map
   and wings for Icarus, as Tab travel asks -- no MoveLevel holes.
7. **Refused for the lord in an event map** (Blood Castle, Devil Square; Kanturu 39 and the others when built); a member in
   one is never pulled out.
8. **Refused with nothing paid** when he has no party, stands in an event map, or one of his party here is already counting
   (MU's client greys the key with no party; WebZen's server would take the mana for all three). "Busy" is said to him.
9. **70 mana, and MU's 30 s delay as its cooldown** (only MU's client holds it); no AG here.
10. **MU's notices word for word** (RecallEvent): "Summoning in process. It may be cancelled if you move.", "Will be
    summoned in N seconds.", "Summon cancelled.", and to him "Summoning N people."
11. **Clip 81** (on cook.py's list, 8 keys at 0.28 = 1.14 s, the realm's hold); MU's tenth-speed hold past key 5.5 is the
    drawing's, broken when he walks; the circles 5 ticks (0.2 s) after the echo (`release` 0.2/1.14).
12. **MU's look at MU's numbers**, 10 s: Circle01 in Magic_b lit blue, Circle02's wall, rising sparks, falling flareBlue
    pillars, the blue glitter off bone 42; judged in game. Same-map arrival: fx/blink's spark pillar.
13. **eSummon.wav whole**, on the cast only, through the showing cook.
14. **Level 98 on the row**; the scroll (12/22, energy 153, Command 400) comes with the Dark Lord's items step (no DL scroll
    exists yet), as the Tersia LT4 reward.
15. **The Helper never casts it** (`HelperKind::Moves`). Party bots answering a summons: with the cross-map step.
