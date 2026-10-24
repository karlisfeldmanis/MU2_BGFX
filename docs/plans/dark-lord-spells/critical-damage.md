# Critical Damage (skill 64) -- research dossier

Researched 2026-10-24 before any build (the user: "we do very deep reserach for each DL spell then we make it").
Nothing here is built. Drafted by a research agent from the sources below; every number is read, not recalled.

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`. **WZC** = `.../ws/Source/Base Common Src/Common/` (built into the GameServer, `WZ/GameServer.dsp:2302`).
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- MU time: one reference tick = 1/25 s. Our realm: 20 ticks/s.

Names: WZ `AT_SKILL_ADD_CRITICALDAMAGE` (64), MM `AT_SKILL_ADD_CRITICAL` (plus master-tree
`_STR1..3` = 511/515/517, `MM/Core/Globals/_enum.h:577-583`), OM `SkillNumber.IncreaseCriticalDamage`
("Increase Critical Damage"), skill.txt "크리티컬데미지증가" / "Increase Critical Dmg". Server buff
`BUFFTYPE_CRITICAL_DMG_INC` = 5 (`WZ/BUFFTYPEDEF.h:26`), client `eBuff_AddCriticalDamage` = 5
(`MM/Core/Globals/_enum.h:3894`), OM `MagicEffectNumber.CriticalDamageIncrease` = 5
(`OM/Persistence/Initialization/Skills/MagicEffectNumber.cs:77`): the same wire number in all three.

## 1. MU's rule (server)

### The row
`WZD/lang/main/skill.txt:69` (same in `lang/kor/skill(Kor).txt:53`):
`64 "크리티컬데미지증가" 82 0 50 50 0 0 25 300 -1 -1 0 ... DL=1 ... duration 0 ... skilltype 1`

| Field | Value | Source |
|---|---|---|
| Level needed | **82** | row; checked when learning (`WZ/user.cpp:28426-28430`, as Electric Spike's) |
| Damage | 0 | row |
| Mana / AG | **50 mana, 50 AG** | row; spent in `UseSkill(aIndex, aTargetIndex, lpMagic)` only after `RunningSkill` returns TRUE (`WZ/ObjUseSkill.cpp:1008-1032`) |
| Distance | 0 (no target; the packet's target is ignored) | row |
| Delay | 0 | row |
| Energy needed | 20 + 25·82·4/100 = **102** | formula `WZ/MagicDamage.cpp:664-676`; OM agrees, `OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:183` |
| Command needed | **300** | row; `gObjMagicAdd` check `WZ/user.cpp:28114-28125`; OM `leadershipRequirement: 300` (:183) |
| Duration column | 0 -- the duration is in code (below) | row |
| Skill type | 1 = buff | row; header `skill.txt:12` |
| Scroll | (12,23): `case 23: skillnumber = AT_SKILL_ADD_CRITICALDAMAGE` | `WZ/MagicInf.cpp:184`; OM "Scroll of Critical Damage", level 82, energy 25 raw, Command 300, **220,000 zen** (`OM/.../VersionSeasonSix/Items/Orbs.cs:59`) |

### How it is cast
- Client sends 0x19 (`SendRequestMagic(Skill, HeroKey)`, target = himself) at the click and plays **nothing**
  locally (`MM/GameLogic/Combat/ClassAttack.cpp:855-861`); no weapon needed (`:493-496`, the any-hand list).
- Server: `CGMagicAttack` (`WZ/protocol.cpp:15185`). Its safe-zone block only turns away Hellfire 2's start
  (`:15211-15224`), so **Critical Damage can be cast in a safe zone**. Speed-hack and skill-distance checks,
  then `UseSkill` -> `RunningSkill` -> `SkillAddCriticalDamage(aIndex, lpMagic->m_Level)` (`WZ/ObjUseSkill.cpp:1171`).
- The handler `WZ/ObjUseSkill.cpp:4121-4214`. Returns at once unless a Dark Lord (`:4137`; `:4134-4135` also
  drops a non-character with no summoner).

### The numbers (`WZ/ObjUseSkill.cpp:4173-4175`)
- **Bonus = (Leadership + AddLeadership)/25 + (Energy + AddEnergy)/30**, integer divisions, item and set adds included.
- **Duration = 60 + (Energy + AddEnergy)/10 seconds.** No cap in WebZen.
- Both read off the **caster** at the cast and are written whole onto every receiver: a receiver's own stats play no part.

### Who receives it
- **Not in a party** (`PartyNumber == -1`): himself only (`:4177-4192`).
- **In a party**: every member (himself included, distance 0) **on the same map** with `gObjCalDistance < 10`
  (`:4149-4170`) -- `(int)sqrt(dx²+dy²)` over tiles (`WZ/user.cpp:7042-7052`), so Euclidean under 10.0.
  No life check and no class check: a dead member in range is "given" it (and loses it on regen, below).
  The buff slot refuses a body not yet in game (`WZ/BuffEffectSlot.cpp:63-66`).
- Per receiver (`:4196-4213`): `gObjAddBuffEffect(..., BUFFTYPE_CRITICAL_DMG_INC, EFFECTTYPE_CRITICAL_DMG, value, 0, 0, SkillTime)`,
  then `MsgOutput(aIndex, 1240, SkillTime)` ("lasts %d seconds") **to the caster once per receiver**, then
  `GCMagicAttackNumberSend(caster, 64, receiver, 1)`: one 0x19 per receiver, source the caster
  (`WZ/protocol.cpp:15442-15500`; the caster gets it, the viewport gets it, Chaos Castle does not hide it).
- `MODIFY_OLD_BUFF_EFFECT_SYSTEM_20071004` is on (`WZ/define2.h:1180`), so the buff-slot branch runs; the older
  `SkillAddCriticalDamage`/`STATE_ADD_CRITICAL_DAMAGE` fields and their per-second countdown (`WZ/user.cpp:25512-25525`)
  are compiled out.

### Stacking: it refreshes, never stacks
`CBuffEffectSlot::SetEffect` (`WZ/BuffEffectSlot.cpp:48`): a standing buff of the same effect category is removed
(`:91-95`) and the new one written in the first free slot (`:160-176`) -- value **and** duration replaced by the new cast's,
even if the new value is lower. The client is told the old one ended (`GCUseBuffEffect(..., 2, ...)`) and the new
one began (`WZ/BuffEffectSlot.cpp:536-549`). A weaker lord's cast overwrites a stronger lord's.

### What the clients are sent
- 0x2D to the receiver: buff index 5, option 0 (on) / 2 (off), time left (`WZ/BuffEffectSlot.cpp:1050-1078`),
  and `GCBuffStateInfoSend` to the viewport (`:1070-1077`) -- how strangers see it on him.
- 0x19 skill 64 per receiver, as above: the cast animation and the client-side buff registration (§3).

### How long and what ends it
- Counted down **once a second** in `gObjCheckBuffEffectList` (`WZ/BuffEffectSlot.cpp:429-434`, `EffectDuration--` at :431), removed at 0.
- **Death**: `gObjClearBuffEffect(CLEAR_TYPE_NON_PCS_ITEM_EFFECT)` in `gObjUserDie` (`WZ/user.cpp:9232`), again on
  regen (`:21339`, `:21965`). Logout clears it (`:1561`, `:6250`). Castle-siege clears (`:24654`, `:24797`).
- Entering Blood Castle-type maps would remove it (`WZ/user.cpp:26143-26146`, `:27046-27049`), but that block is
  under `ADD_EVENT_MAP_BUFF_EFFECT_DELETE_20090421`, **which is not defined** in 1.00.93: it survives map moves.
- The master skill "Removal Buff" strips it on the client (`MM/Network/Server/WSclient.cpp:4640`).

### OpenMU differs
`OM/Persistence/Initialization/Skills/CriticalDamageIncreaseEffectInitializer.cs:28-66`: same bonus
(Ene/30 + Leadership/25) and duration (60 + Ene/10), but **duration capped at 180 s** (`:40`) where WebZen has no cap;
`StopByDeath` (`:37`) as WebZen. Receivers are `SkillTarget.ImplicitParty` = party members **in his observer list**
(his view), not "< 10 tiles" (`OM/GameLogic/PlayerActions/Skills/TargetedSkillDefaultPlugin.cs:174-182`). OM's
attribute math is float, WebZen's integer. **WebZen wins** on all three.

## 2. The buff's numbers

### Where the bonus enters (WebZen)
It is a **flat add on a critical hit**, read with `gObjGetValueOfBuffIndex(lpObj, BUFFTYPE_CRITICAL_DMG_INC, &iValue)`
(`WZ/BuffEffectSlot.cpp:1022`). Four places:

| Path | Who uses it | When it adds | Where |
|---|---|---|---|
| `GetAttackDamage` (physical) | DK, Elf, DL, MG physical, and every DL skill | `cDamage` is set by a **critical or an excellent** roll (`:3283-3306`); both hands go to the top of their bands, then + set crit add, + Harmony crit add, **+ value on the right hand and + value on the left** | `WZ/ObjAttack.cpp:3350-3386` |
| `GetAttackDamageWizard` | DW, MG spells | **critical only**: `ad = damagemax - def + set + Harmony + value`; an excellent roll after it rewrites `ad` without it | `WZ/ObjAttack.cpp:3696-3724`, excellent `:3734-3749` |
| `GetAttackDamageSummoner` | Summoner curses | critical only, as the wizard's | `WZ/ObjAttack.cpp:4131-4150` |
| Fenrir's Plasma Storm | Fenrir riders | critical only | `WZ/ObjAttack.cpp:814-845` |

Consequences, plainly:
- **Physical, excellent hits get it too** (the excellent's 20% is added after, `:3390-3394`); on spells they do not.
- **A dual-wielding knight gets it twice** (right and left are summed later, as for the set crit add).
- It is added **before** the target's defence comes off (physical, `:3418` onwards), **after** it on spells (`ad = damagemax - def` first).
- On a DL skill the whole of it then rides the DL multiplier **× (200 + Ene/20)/100** (`WZ/ObjAttack.cpp:1452-1463`).
- The Dark Raven does not read it (`WZ/DarkSpirit.cpp:172-178` rolls its own critical and goes through `CWeapon::Attack`).
- It does nothing for a body with **no critical chance**. A character's chance is 0 (`WZ/ObjCalCharacter.cpp:535`)
  plus **4 per lucky piece** (`WZC/zzzitem.cpp:3023-3025`, `AT_LUCK: *Value += 4`) plus sets/sockets/rings.
  (OpenMU and ours give 5 a lucky piece -- `sim::kLuckCritical` 0.05, `src/sim/items.h:654`.)

### OpenMU's
`OM/GameLogic/AttackableExtensions.cs:104-114`: physical adds `CriticalDamageBonus` on a critical **and** on an
excellent (matches WebZen), once (not per hand); wizardry/curse/Fenrir add it on a critical only (`:157-169`), after the
defence (matches WebZen).

### Worked numbers (WebZen formula)
| Lord | Bonus | Duration |
|---|---|---|
| Cmd 300, Ene 102 (the minimum to learn) | 12 + 3 = **15** | 70 s |
| Cmd 800, Ene 300 | 32 + 10 = **42** | 90 s |
| Cmd 1500, Ene 1000 | 60 + 33 = **93** | 160 s |
| Cmd 2000, Ene 2000 | 80 + 66 = **146** | 260 s |

Small: a level-200 lord's buff is +40-ish on a crit, on a hit already at the top of its band.

## 3. Client (MuMain S6, cross-checked with 1.03.35)

### When it plays
- The hero's own click plays nothing (`MM/ClassAttack.cpp:855-861`); `Attacking = -1` (`MM/GameLogic/Combat/SkillExecution.cpp:243-246`).
  **The animation starts on the server's 0x19 echo**, for him and for everyone who sees it.
- `ReceiveMagic` (`MM/Network/Server/WSclient.cpp:4038`): for each 0x19 the caster's `Skill` is set to 64
  (`:4086-4087`), and **if the caster is not the hero he is turned to face the receiver** (`:4079-4080`). A party
  cast arrives as several packets, so on others' screens he swivels to each member in turn and ends facing the last.
- Case 64 (`:4683-4701`): `SetAttackSpeed()`, `g_CharacterRegisterBuff(receiver, eBuff_AddCriticalDamage)`, then
  `SetAction(caster, ...)` and `AttackTime = 1`:

| Mount | Action | MuMain raw # | Keys in player.bmd | Play speed | Clip length (no magic speed) |
|---|---|---|---|---|---|
| on foot, Uniria, Dinorant, or any mount in a safe zone | `PLAYER_SKILL_HAND1` | **146** | 6 | 0.29 + MagicSpeed2 (`MM/Engine/Object/ZzzCharacter.cpp:939-940`) | 0.83 s |
| Dark Horse, not safe zone | `PLAYER_ATTACK_RIDE_ATTACK_MAGIC` | **86** | 7 | 0.30 + ms (`:953`) | 0.93 s |
| Fenrir, not safe zone | `PLAYER_FENRIR_ATTACK_MAGIC` | **97** | 7 | 0.37 + ms (`:960`) | 0.76 s |

  Raw numbers counted from `MM/Core/Globals/_enum.h:2902-3064` (PLAYER_SET = 0; `PLAYER_FLY_RIDE = PLAYER_ATTACK_END`
  is an alias, so everything after it is line - 2: STRIKE 80 and FLASH 152, as the other dossiers). Key counts from
  `content/players/rig/player.rig.json`. `actions.json` labels these one high (HAND1 at 147, ride magic at 87, Fenrir
  magic at 98), as Electric Spike's dossier found from 75 up. **The game already plays 147 for the wizard's hand casts**
  (`pipeline/cook.py:2325`; `src/sim/skills.cpp:609,641,712`) -- by the raw count that is `PLAYER_SKILL_HAND2`, the
  other hand of the same 6-key pair (MuMain's wizard picks `HAND1 + rand()%2`, `MM/ZzzCharacter.cpp:1339`). 146 is not on
  cook.py's list. A bench look settles which hand 146 raises.

### The let-go: 0.56 s after the echo
Critical Damage has **no case in `AttackStage`** (`MM/ZzzCharacter.cpp:2609-2960`), so `AttackTime` just counts
`+= FPS_ANIMATION_FACTOR` a tick from 1 (`:4133-4138`) to `g_iLimitAttackTime` 15 (`:172`, `:2616`); at 15 the
let-go runs (`:4140` -> `:4367-4384`). **14 ticks = 0.56 s** whatever the clip or the magic speed -- on foot about key
4.1 of 6.

### The cast flash (`MM/ZzzCharacter.cpp:4367-4384`; 103 `ZzzCharacter.cpp:5145-5158`)
- Light **(1.0, 0.6, 0.3)** -- orange.
- If the right hand's weapon is not arrows: `MODEL_DARKLORD_SKILL` **subtype 0** at the right weapon's link bone (offset 0).
- If the left hand holds neither bolts nor a **shield**: subtype 1 at the left weapon's link bone. A lord with a scepter
  and a shield gets **one** flash.
- `PlayBuffer(SOUND_CRITICAL, o)`: `Data/Sound/sDarkCritical.wav`, 1.78 s (`MM/Engine/Object/ZzzOpenData.cpp:4875`).
- Nothing is drawn on the receivers at the cast. Nothing flies to them.

### `MODEL_DARKLORD_SKILL` subtypes 0/1
- Model `Data/Skill/DarkLordSkill.bmd` (`MM/ZzzOpenData.cpp:4320`): one quad 206 × 114 units wearing `KingS_R`
  (Electric Spike's dossier; already in `content/effects/fire_burst/`). Rendered `RENDER_TEXTURE` with `BodyLight = o->Light`
  (`MM/Engine/Object/ZzzObject.cpp:975-979`).
- Create (`MM/Render/Effects/ZzzEffect.cpp:3449-3472`; 103 `ZzzEffect.cpp:4152-4178`): life **10**, scale 0.2, velocity
  0.1, angle **(45, +45, 0)** for 0 and **(45, -45, 0)** for 1 (the `rand_fps_check(2)` / `rand()%2` branch sets the same
  values). The angle **replaces** the passed one: yaw 0 in the world, not his facing.
- Move (103 `ZzzEffect.cpp:14791-14799`): `Scale += Velocity; Velocity += 0.02` a tick, `BlendMeshLight /= 1.8` a tick
  once life < 7. So 0.2 -> ~2.1× (about 430 units across) in 0.4 s, dimming over the last 0.24 s.
- **MuMain bug**: `Move_MODEL_DARKLORD_SKILL` is empty (`MM/Render/Effects/Behaviors/MoveHandlers.cpp:5681-5685`) and the
  grow code is unreachable after a `break` (`MM/ZzzEffect.cpp:8045-8072`). In MuMain the quad never grows. **Use 1.03.35's.**
  (This is the same flash our Fire Burst already draws, §6.)

### The lasting look, on every receiver, while it stands (`MM/ZzzCharacter.cpp:10028-10070`; 103 `:11972-12013`)
In `RenderCharacter`, only if `RenderCharacterBackItem` says the weapons are **in his hands** (`Bind` false,
`MM/ZzzCharacter.cpp:10028`): not in a safe zone, not emoting, not swimming in Atlans/Kalima (Blood and Chaos Castle
always count as in hand) (`RenderCharacterBackItem`, same file), and not cloaked. Players only.
- **A pulse every 1.2 s**: MuMain `LastCritDamageEffect < WorldTime - 1200` per character (`:10034-10037`); 1.03.35
  `(MoveSceneFrame % 30) == 0` (`103/ZzzCharacter.cpp:11980`), the same rate at 25 fps but every buffed body in step.
- Each pulse, at each hand holding a weapon (right: not arrows, and in MuMain not empty; left: not bolts, not a shield):
  1. `BITMAP_FLARE_FORCE` **subtype 1** at the link bone, with `PKKey` = 1 for a bow else -1 and `SkillIndex` = the link bone
     (`:10048`, `:10058`). It spawns **three joints**, subtypes 5, 6, 7 (`MM/ZzzEffect.cpp:3431-3436`).
  2. **One pulse in twenty** (`renderSkillWave = rand()%20 ? true : false`, `:10038`): also the orange `MODEL_DARKLORD_SKILL`
     0/1 flash above at that hand.
  3. **`SOUND_CRITICAL` every pulse** (`:10063`) -- the crackle is heard from every buffed player each 1.2 s.

### The three spirals (`BITMAP_FLARE_FORCE` joints 5-7)
- Create (`MM/Render/Effects/ZzzEffectJoint.cpp:2489-2575`; 103 `ZzzEffectJoint.cpp:2570-2650`): life **15** (0.6 s),
  texture **`BITMAP_FIRECRACKER` = `Effect/Fire04.jpg`** (`:2567-2569`; `MM/ZzzOpenData.cpp:5296`), light **(1, 0.8, 1)**
  (pale violet-white; the passed light is overwritten, `:2499`), radius `TargetPosition[0]` 30, phase `SubType × 90` =
  90°/180°/270°, step `Direction` (0, -4, 0), or 0 for a bow (`:2549-2558`); MaxTails 1, cap `Weapon` 30; no start tail (`:171-174`).
- Move (`MM/ZzzEffectJoint.cpp:6521-6583`; 103 `:7096-7150`), every tick the whole ribbon is rebuilt from the weapon bone:
  start = link bone × (0, 20, 0); for each of `MaxTails` steps the centre moves by the step rotated into the bone's frame
  (**along the weapon**), the step grows 0.1 a step; the ribbon point sits on a circle of the radius (shrinking 0.15 a
  step) turned ±40° a step (5 and 7 one way, 6 the other) about the world X axis (`AngleMatrix((θ,0,0))`); every other
  step a `BITMAP_FLARE` sprite (`Effect/Flare.jpg`, `MM/ZzzOpenData.cpp:5297`) of scale `Light[0]/2` = 0.5 sits on the
  centre line. `MaxTails` grows one a tick, so the coil **runs up the weapon**: ~15 steps, ~70 units, by the end.
  Light × (1/1.5) a tick once life < 7 (`:6580-6583`).
- A bow: step 0, so the three coil in place at the grip.
- Render: the joints' default additive blend (`RENDER_TYPE_ALPHA_BLEND`, `MM/ZzzEffectJoint.cpp:83`, `:6991-6993`).
  MuMain lays each tail on bone 0's matrix (`:6562`); 1.03.35 on the weapon bone's (§4).

### The buff icon
`eBuff_AddCriticalDamage` = 5 -> cell 4 of `Interface/newui_statusicon` (column 4, row 0, 20 × 28 at u = 80 px)
(`MM/UI/NewUI/HUD/NewUIBuffWindow.cpp:336-349`, sizes `:19-20`); the master upgrades fold onto it (`:99-101`).
The modern footage shows it as an orange-red raised fist.

### Files (all confirmed in MMD)
| Thing | File | In our content? |
|---|---|---|
| flash quad | `Skill/DarkLordSkill.bmd` + `Skill/KingS_R.OZJ` | yes, `content/effects/fire_burst/DarkLordSkill.obj`, `KingS_R.png` |
| spiral ribbons | `Effect/Fire04.OZJ` (`BITMAP_FIRECRACKER`) | **no** (`content/effects/firecracker/` holds other sheets) |
| spiral sprites | `Effect/Flare.OZJ` (`BITMAP_FLARE`) | yes, `content/effects/levelup/flare.png` |
| sound | `Sound/sDarkCritical.wav` (1.78 s) | **no**, not in `content/sounds/sounds.json` |
| icon | `Interface/newui_statusicon` cell 4 | **no** (`content/interface/buffs/` has none for it) |

## 4. 1.03.35 differences
- Cast, light, sound, the 1/20 wave and the spirals' numbers are the same: `103/WSclient.cpp:5699-5716`,
  `103/ZzzCharacter.cpp:5145-5158`, `:11972-12013`, `103/ZzzEffectJoint.cpp:2570-2650`; files at `103/ZzzOpenData.cpp:5904`
  (DarkLordSkill), `:8489` (sDarkCritical), `:9355` (Fire04), `:9357` (Flare).
- The flash **grows and fades** in 1.03.35 (`103/ZzzEffect.cpp:14791-14799`); dead code in MuMain.
- The pulse clock: 1.03.35 `MoveSceneFrame % 30`, global, frame-locked; MuMain 1200 ms a character.
- The right hand: 1.03.35 tests only "not arrows" (`MODEL_BOW+15`), so an **empty** right hand still pulses at its link
  bone; MuMain also needs a weapon there (`Type != -1`).
- The spiral's tail frame: 1.03.35 `CreateTail(o, BoneTransform[MultiUse])` (the weapon bone), MuMain bone 0
  (`MM/ZzzEffectJoint.cpp:6562`, commented as changed). That turns which way each ribbon's flat faces.

## 5. Footage

Clip (Webzen's own channel "Mu Online", a modern client, a training dragon, no party):
- `uIeuh4Dhmc0` "Mu Online Dark Lord Skill Critical Damage", 21 s, 720p (4:3 letterboxed), the cast looped about
  every 0.8 s (the clip's own length). `4yG1dR0tXHs` is the same channel's 1 s cut. A classic-server clip
  (`u_L9smJD-uM`, "MuPlayring DarkLord Critical DMG") is no longer available.

Stills (`critical-damage-footage.jpg` beside this file, cropped round him, 0.10 / 0.20 / 0.30 / 0.40 / 0.60 s):
- **0.10 s** before: standing, a scepter down at his right side; the fist icon blinks in the buff strip (it blinks
  while the clip runs, as each recast refreshes it).
- **0.20 s** cast: right arm swung up across the chest, a white streak on the scepter, and the first **pale cream-yellow**
  glow opening at the hand.
- **0.30 s** height: two broad, soft **cream-to-orange crescents** spread either side of him at chest height, each about his
  own height across -- the 206 × 114 `KingS_R` quad near its top scale. They sit at the hand, beside and behind his body,
  not turned to the dragon.
- **0.40 s** fading: scepter raised high, only a faint orange haze left round the arm.
- **0.60 s** after: back to stance; nothing on him.
- The lasting coil on the weapon is **not seen** at this size and recast rate; the crackle cannot be judged (no audio checked).
  No damage numbers are shown.

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

Reusable:
- **The flash is already built**: `src/game/fx/fire_burst.h:23-26` draws exactly `MODEL_DARKLORD_SKILL` 0/1, additive
  (1, 0.6, 0.3), (45, ±45, 0), 1.03.35's grow and fade, from `content/effects/fire_burst/DarkLordSkill.obj` + `KingS_R.png`.
- **A weapon-hand position per body**: `ElectricSpike::update(seconds, grip, facing)` takes `grip(id, out)`
  (`src/game/fx/electric_spike.h:63-66`); `src/game/fx/held_lights.*` already reads a held weapon's hand frame, which the
  spirals need (they step along the weapon bone).
- `content/effects/levelup/flare.png` for the spirals' `BITMAP_FLARE` sprites.
- **The 10-tile party rule**: `Realm::partyNear` counts members on whole-tile `(int)sqrt < 10`, himself included
  (`src/sim/realm_fight.cpp:2073-2083`); `Realm::partyInView` (`src/sim/realm_party.cpp:23`), `kPartyReach` 10
  (`src/sim/rules.h:462`), `Body::party`. A realm is one map, so "same map" is free.
- **Greater Damage is the template for a timed number on a body**: `Body::might`/`mightUntil` (`src/sim/realm.h:612-613`),
  set in `throwSkill` (`src/sim/realm_skills.cpp:349-364`, a recast replaces), lapsed in `src/sim/realm.cpp:1181-1184`,
  folded into the fighter by `rearm` (`src/sim/realm_items.cpp:75`, `arms.greaterDamage`), cleared on death
  (`src/sim/realm_fight.cpp:2315-2316, 2353-2354, 2376`), mirrored (`src/sim/realm.cpp:574-576`), saved
  (`src/game/save.cpp:227, 450-451`), shown in the HUD strip (`src/game/ui/desk.cpp:2130-2135`, icon name by skill
  `src/game/ui/hud.cpp:146-148`) and the target frame (`src/game/ui/target_frame.cpp:93`), and re-cast by the helper
  (`src/game/helper.cpp:387, 456`) and quest bots (`src/game/quest_auto.cpp:535`).
- Command is a stat already (`HeroPoints::command`, `src/sim/realm.h:384`; Electric Spike reads `p.command`,
  `src/sim/realm_fight.cpp:57-60`).
- The icon cutter: `pipeline/buff_icons.py` (`STATES`, `:79`) cuts cell N-1 of the status sheet; this one is state 5.

Missing, and where each would go:
- **A flat "on a critical" add in the blow.** Ours has `Fighter::criticalDamage`, a **percent** of the top (the Pendant of
  Fury's, `src/sim/rules.h:183-185`), applied at `src/sim/rules.cpp:45` (swing), `:60` (off-hand), `:102` (spell). MU's buff is
  a flat value: a new `Fighter` int beside `greaterDamage` (`rules.h:201`), set in `sim::reckon` from `arms` as
  `greaterDamage` is (`src/sim/rules.cpp:364`), added in those branches. WebZen's shape would also add it on a **physical
  excellent** (`rules.cpp:39-42` today ignores crit adds) and to **each hand** of a dual wielder (`:58-61`), not on a
  spell's excellent (`:96-99`).
- **A body field pair** (bonus, until) beside `might`/`mightUntil`, with the same lapse, death clear, mirror, save and rearm.
- **A party-wide self-cast.** `SkillRow::onSelf()`/`aura()` know boons, mends, mights and summons (`src/sim/skills.h:482-485`);
  `aids()` is one other player (`:492`, used `src/sim/realm_skills.cpp:289-311`). Nothing in `throwSkill` lays a buff on
  every member near him -- a new branch in the `onSelf` block (`realm_skills.cpp:245-385`), and a column on `SkillRow`
  (as `mightTicks`, `skills.h:414`) so `onSelf()`/`aura()` count it.
- **The row**: `skill::kCriticalDamage = 64` in `src/sim/skills.h` (beside `kElectricSpike`, `:131`), a row at the end of the
  table in `src/sim/skills.cpp` (after Electric Spike, `:943-948`), `kSkills` 43 -> 44 (`skills.h:520`); level 82, the scroll's
  energy 102 and Command 300; the clip (146, plus 86/97 for the mounts later) on `pipeline/cook.py:2325`'s list so
  `castTicks` knows it.
- **Presentation**: a `src/game/fx/critical_damage.*` (the cast flash can call Fire Burst's flash or share its code; the lasting
  spirals are new), a `Loosed`/cast hook in `src/game/play.cpp` beside Electric Spike's, a per-body "buff standing" loop like
  `Play::guardStep` (`src/game/play_show.cpp:1153-1185`) for the pulse on every buffed player in sight.
- **Content**: `Effect/Fire04` into `content/effects/` (new sheet), `sDarkCritical.wav` into `content/sounds/sounds.json`
  (the showing cook), `buff_critical_damage.png` (cell 4) into `content/interface/buffs/` via `pipeline/buff_icons.py`, and the
  scroll item (12/23, kept free for it, fire-burst.md §6) in every world's tables.

## 7. Open calls (for the design; not decided here)

1. **Is it a party buff with WebZen's reach** (party within < 10 whole tiles, himself always; himself alone outside a party),
   or OpenMU's "members in his view"? And may it be cast on a safe tile (WebZen: yes)?
2. **What a critical means here.** Ours already has a critical as a percent over the top (Pendant of Fury). Does the buff stay
   MU's flat add (Cmd/25 + Ene/30, +15 to +150), or become a percent to fit ours? Does it also add on an **excellent** physical
   hit, as WebZen and OpenMU do, and twice for a dual wielder (WebZen only)?
3. **It does nothing without a critical chance** (luck pieces, talents, Keen Eye). Is that acceptable for a skill a lord spends a
   quickbar slot on, or does it need a chance of its own (later seasons' masteries add one)?
4. **Duration**: WebZen's uncapped 60 + Ene/10 s, OpenMU's 180 s cap, or this project's convention for buffs (Greater Damage and
   Greater Defense were made five minutes, `src/sim/skills.cpp:528-536`)?
5. **Recast over a stronger one**: WebZen lets a weaker lord's cast replace a stronger one (value and time); the elf's ward refuses
   a weaker guard (`realm_skills.cpp:295-308`). Which rule here?
6. **Cost and cooldown**: MU spends 50 mana and 50 AG and has no delay; we have no AG and every QWER skill has a cooldown (plan §3).
7. **Which clip**: raw 146 (`PLAYER_SKILL_HAND1`, MU's) or the 147 the wizard's hand casts already use; and the 0.56 s let-go
   (MU's fixed AttackTime) vs a share of the clip as the others use (`release`).
8. **The lasting look**: MU pulses spirals up each held weapon every 1.2 s with a flash one pulse in twenty, **and plays
   sDarkCritical every pulse on every buffed player**, hidden in safe zones. Keep, thin (the user's "monster auras are subtle"),
   or silence the repeating sound?
9. **The flash's facing**: MU's yaw is world-fixed 0 and the footage puts it beside and behind him; Fire Burst's copy turns it to
   his facing ("only frontal"). Same here?
10. **Others' view**: MuMain swivels the caster to face each receiver's 0x19 in turn on strangers' screens -- a quirk to drop or keep.
11. **Learning**: level 82, energy 102, Command 300 from the Scroll of Critical Damage 12/23 (220,000 zen in OpenMU); who sells or
    drops it (plan §5 names Devin's reward).

## 8. Decided (2026-10-24, from this research and the user's standing taste: MU's numbers, "only frontal", the hit with the visual)

The research's numbers were re-checked against WebZen before the build: the formula and the party loop
(`WZ/ObjUseSkill.cpp:4140-4200`), and the flat add on each hand's top on a critical or excellent swing
(`WZ/ObjAttack.cpp:3355-3386`) and on a spell's critical after the defence (`:3705-3721`).

1. **Reach** (7.1): WebZen's -- every member of his party on the map within whole-tile distance under 10, himself
   always; himself alone outside a party. A safe tile refuses it, as every cast here (Realm::throwSkill).
2. **The bonus** (7.2): MU's flat `Command/25 + Energy/30`, added to each hand's top on a critical or excellent swing
   (before the defence; the excellent's x1.2 rides it) and after the defence on a spell's critical (`Fighter::criticalAdd`,
   src/sim/rules.cpp). Our Pendant of Fury's percent stays beside it.
3. **No chance of its own** (7.3): it does nothing without a critical or excellent chance, as in MU.
4. **Duration** (7.4): MU's `60 + Energy/10` seconds, uncapped; ends on death; **not saved** -- MU ends it on logout
   (`Body::keen`, `keenUntil`).
5. **Recast** (7.5): it replaces rather than stacks, but a weaker lord's cast does not replace a stronger one still
   standing (the user's rule for a ward, 2026-10-21). Equal or stronger refreshes it.
6. **Cost** (7.6): MU's 50 mana; Greater Damage's 3 s cooldown (no AG here; MU has no delay).
7. **Clip** (7.7): 147, the hand cast the wizard's spells already play and cook (one of MuMain's HAND1/HAND2 pair;
   146 is not on cook.py's list). The let-go at MU's 0.56 s as a share of HAND's 0.83 s clip as played. Judge the arm
   on frames; 146 can follow with a tables cook.
8. **The lasting look** (7.8): MU's coils up each held weapon every 1.2 s on every player under it, off a safe tile,
   with the flash one pulse in twenty -- **silent**: sDarkCritical on the cast alone (MU plays it every pulse of
   every buffed player; the user's "auras are subtle").
9. **The flash** (7.9): at each weapon hand, turned to his facing and slid ahead of the hand ("only frontal").
10. **Others' view** (7.10): no swivel to each receiver.
11. **Learning** (7.11): level 82 on the row; the scroll (12/23), energy 102 and Command 300 come with the other scrolls.
12. **The Helper** keeps it up off his own clock (`keenUntil`), as an aura, and never casts it "on" a party member.

Built: the row (src/sim/skills.cpp), `keenOf`/`keenTicksOf`, the party cast in Realm::throwSkill, the lapse, the death
clear, `Fighter::criticalAdd`; fx/critical_damage.* (the flash, the coils, the flares); the icons skill_64 and
buff_critical_damage (MuMain's sheets at 3x); content/effects/critical_damage/fire04.png; sDarkCritical in sounds.json.

