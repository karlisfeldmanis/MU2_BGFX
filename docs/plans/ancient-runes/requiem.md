# Requiem (skill 224, the Summoner's Book of Neil) as an Ancient rune -- research dossier

Researched 2026-10-25 before any build, for the sixth Ancient rune on the Soul Master's weapon, after Ice Storm (74),
Weakness (75, 142d7486), Pollution (76, bf2e7354) and Drain Life (77, 064e1331; 77 was Chain Lightning for a day).
Shaped as docs/plans/mg-skills/fire-slash.md: section 1 is the server, section 2 the client, section 3 the footage,
section 4 the fit to our code and the budget, section 5 the proposed rune and what is decided, section 6 the open
calls. Sources: WebZen GameServer 1.00.93 (github ptr0x-real/Mu-GS-Webzen-MC-10093, shallow-cloned for the research,
cp949, line numbers unchanged by iconv), the S17 server (`third_party/MuSource_S17`), OpenMU `LEGACY/reference/openmu`,
MuMain `LEGACY/reference/MuMain`, Main 1.03.35 (`third_party/Main 1.03.35`), and YouTube footage.

**In one paragraph.** Requiem is the Summoner's Book of Neil spell (Korean 레퀴엠; the book is item 5/22): the
Summoner calls up **Neil, a woman in dark red armour with a long blade**, as a translucent ghost a metre in front of
her. Neil swings her blade up in a great **red crescent** (a motion-blur ribbon off the blade) and, as the swing
lands, a thicket of **tall red blades bursts out of the ground** under the target -- "thorns" in the client's own
comment -- over a **white-orange glow of burning cracks**. The server waits **1000 ms** for that, then strikes each
body round the point with curse damage (**65**, **wind**), and in WebZen's and OpenMU's rules every body struck
**bleeds 60% of that blow each second for 5 s** (S17 dropped the bleed). The whole look lasts 80 frames (3.2 s).

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93; cp949)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/`
- `S17/` = `MU2_BGFX/third_party/MuSource_S17/` (`GameServer/`, `_MuServer/Databases/`)
- `OM/` = `LEGACY/reference/openmu/src/`

## 1. Name and version
- WebZen enum: `AT_SKILL_REQUIEM = 224`, "레퀴엠", listed as a summon-book skill (환수 스킬) beside
  `AT_SKILL_EXPLOSION_2 = 223` (Book of Samut) and `AT_SKILL_PULLUTION = 225` (Book of Lagle): WZC/include/public.h:328-335.
  Its code sits under `ADD_SEASON_3_NEW_CHARACTER_SUMMONER_20070912` (WZ/ObjAttack.cpp:1491, WZ/user.cpp:3755): a
  **Season 3** skill, the Summoner's launch set. Pollution came later (`UPDATE_ADD_SUMMONER_SKILL_POLLUTION_20080425`).
- **WebZen's data has no row for it**: WZD/lang/main/skill.txt ends at 202 (skill.txt:75-78), and no other language file
  has 224 either. So the numbers come from S17 and OpenMU, the rules from WebZen's code.
- S17: `SKILL_REQUIEM = 224` (S17/GameServer/SkillDef.h:131). OpenMU: `SkillNumber.Requiem = 224`
  (OM/Persistence/Initialization/Skills/SkillNumber.cs:107). MuMain: `AT_SKILL_SUMMON_REQUIEM` = 224 (the entry after
  `AT_SKILL_SUMMON_EXPLOSION = 223`, MM Core/Globals/_enum.h:433-435).
- It is a **book skill, not learned**: WebZen keeps it out of the saved skill list as an equipment skill
  (WZ/user.cpp:3755-3758) and gives it no energy requirement of its own (WZ/MagicDamage.cpp:765); the book carries it.
  S17's Book of Neil is item 5/22 (S17/_MuServer/Databases/game.sql:21684); MuMain `ITEM_BOOK_OF_NEIL = ITEM_STAFF + 22`
  (MM _enum.h:2304). S17 also has a "Requiem Parchment" 15/43 (game.sql:23956), a later learning scroll.

## 1. Skill row
- S17 `skill_template` 224 (S17/_MuServer/Databases/game.sql:45684), columns per the table at game.sql:45543-45590 and
  the loader S17/GameServer/SkillMgr.cpp:77-117:
  `level 75, damage 65, mana 60, stamina(AG) 4, distance 4, effect_radio 3, delay 800 ms, type 1, attribute 5,
  buff_icon 74, required_energy 416, base 224, allow_sum 1, animation 1, elemental_attribute 3, elemental_bonus 125,
  stat_1 3 / stat_value_1 150`.
  - attribute 5 = **WIND** (`Element::{ICE, POISON, LIGHTNING, FIRE, EARTH, WIND, WATER}`, S17/GameServer/CommonDef.h:238-250);
    WebZen's file legend has the same order, 5 = 바람 wind (WZD/lang/main/skill.txt:7).
  - elemental_attribute 3 = pentagram Earth (CommonDef.h:996-1001) -- the S17 pentagram system, not ours.
  - stat_1 3 = energy (SkillMgr.cpp:119-134), so S17 adds **energy / 150** to the skill's damage
    (S17/GameServer/PlayerSkillUse.cpp:1346-1349).
  - Its sibling for scale: Pollution 225 is `damage 80, mana 70, distance 4, radius 3, delay 500, attribute 2
    (lightning)` (game.sql:45685). Requiem is **65/80 of Pollution's damage**.
  - Master enhancement 775 adds to the damage (PlayerSkillUse.cpp:991-994; game.sql:46157): not ours.
- OpenMU: `CreateSkill(Requiem, "Requiem", AllSummoners, DamageType.Curse, damage 65, distance 6, AG 10, mana 110,
  energyRequirement 99, elementalModifier Wind, AreaSkillAutomaticHits)` and `AddAreaSkillSettings(Requiem, no frustum,
  effectRange 2)` (OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:222-223; parameter order
  OM/.../Skills/SkillsInitializerBase.cs:51-71, 129-146). The same 65 in all three.

## 1. The cast and its targets
- **WebZen** (the live code): the cast goes to `SkillSummons` (WZ/ObjUseSkill.cpp:911-914), which
  (WZ/ObjUseSkill.cpp:6170-6250):
  - sends the skill number to everyone first (GCMagicAttackNumberSend, :6181), so clients draw it at once;
  - checks the point is within the skill's distance of the caster (:6184-6185);
  - takes **every** monster in the caster's viewport (not a summoned one), the forced target and a duel/guild-war
    enemy (:6188-6227) standing in the **5x5 square x-2..x+2, y-2..y+2** round the point (:6231-6232);
  - and **delays each blow 1000 ms** (`gObjAddAttackProcMsgSendDelay(..., MSG_ATTACKPROC_ENTIREMAGIC, tObjNum, 1000, skill)`, :6235).
  No cap on the count.
- **S17**: `SkillAttackRequiem` (S17/GameServer/SkillHandler.cpp:2742-2775): the point is the target's tile for a
  creature caster (:124-148), the point must be within range (:2746), then **up to 4** (`hit_count = 4`, :2751) bodies
  within effect_radio **3** (Euclidean, `Util::Distance` truncated, Common/Util.cpp:136-147) each get
  `AddDelayed(DELAYED_ATTACK, 1000, ...)` (:2765) -- the same **1000 ms**. It is Pollution's handler but delayed:
  Pollution strikes at once (:2777-2810).
- **OpenMU**: every attackable within effectRange **2** of the client's target point, not in a safe zone, monsters
  only unless area skills hit players (OM/GameLogic/PlayerActions/Skills/AreaSkillAttackAction.cs:129-160); no cap
  (`MaximumNumberOfHitsPerAttack 0` means unlimited, :236-237); hit at once (no deferred hits configured).

## 1. Damage (WebZen)
- Requiem takes the **summoner (curse) branch**: `GetAttackDamageSummoner` (WZ/ObjAttack.cpp:895; body :3961+). For
  Requiem, Sleep, Blind, Explosion, Weakness, Enervation and Pollution, min/max = skill damage + the caster's curse
  damage (+ Berserker's share), raised by the **book in the left hand** by `curse/2 + level*2` percent
  (WZ/ObjAttack.cpp:3990-4045). The skill row's damage rolls `dmg .. dmg + dmg/2` as every MU skill
  (WZ/MagicInf.cpp:236-238, Fire Slash's dossier).
- After defence, in the per-skill switch (WZ/ObjAttack.cpp:1371-1382 opens it, the case is :1556-1561):
  ```
  case AT_SKILL_REQUIEM:
      int DotDamage = AttackDamage * 60 / 100;
      gObjAddBuffEffect(lpTargetObj, BUFFTYPE_BLOODDOT, EFFECTTYPE_GIVE_DMG_TICK, DotDamage, 0, 0, 5);
  ```
  **Every** body that takes the blow alive gets it -- no chance, no resistance roll. BUFFTYPE_BLOODDOT is "출혈",
  bleeding (WZ/BUFFTYPEDEF.h:96). Explosion (223) lays BUFFTYPE_FIREDOT the same way (:1549-1554).

## 1. The bleed (WebZen)
- Duration 5, counted down once a second by gObjSecondProc → gObjCheckBuffEffectList (WZ/user.cpp:22610;
  WZ/BuffEffectSlot.cpp:360-445): each second the effect fires (`SetActiveBuffEffect`, BuffEffectSlot.cpp:421-422),
  then `EffectDuration--` and it is removed at 0 (:428-433) -- **5 pulses, one a second**.
- A pulse is `GiveDamageEffect` (WZ/BuffEffect.cpp:572-650): the whole DotDamage off life, **no defence**; against a
  player 90% of it goes to the shield first (:600-625); it can kill (`gObjLifeCheck`, :645-648).
- **A re-hit replaces it**: SetEffect removes any buff of the same effect category before adding the new one
  (WZ/BuffEffectSlot.cpp:84-95), so a second Requiem restarts 5 s at the new blow's 60%. It never stacks.
- So a body that takes a Requiem blow of D loses D at once and **3 D more over the next 5 s** (5 x 0.6 D).

## 1. OpenMU (S6) and the differences
- The bleed: the Requiem magic effect is `IsBleeding`, **5 s**, `StopByDeath` (OM/Persistence/Initialization/Skills/RequiemEffectInitializer.cs:27-45;
  re-made by OM/.../Updates/FixSummonerCurseSkillsPlugIn.cs:74-78). Laid only if the hit did at least 1 damage, at
  **0.6 x (health + shield damage)** (OM/GameLogic/AttackableExtensions.cs:908-918), ticked every 1000 ms
  (OM/GameLogic/BleedingMagicEffect.cs:27-35, :57-66). Requiem **skips the wind resistance roll**
  (SkillsInitializerBase.cs:239-243; FixSummonerCurseSkillsPlugIn.cs:86-100), so it always bleeds, but a target
  already bleeding from it is **not re-applied** (AttackableExtensions.cs:422-428).
- `RequiemSkillPlugIn` (OM/GameLogic/PlayerActions/Skills/RequiemSkillPlugIn.cs:30-54) adds a 3 s stun at
  `MasteryStunChance`, which only the master tree's Wind Tome Mastery gives (FixSummonerCurseSkillsPlugIn.cs:121-126). Not ours.
- **S17 has no bleed**: `BUFF_REQUIEM = 74` is commented out (S17/GameServer/BuffDef.h:73); buff_template 74 survives
  as a name only, "Summoner Neil attack" (game.sql:117). The client still draws it (`eDeBuff_NeilDOT = 74`, section 2.6).
- **Differences**:
  1. Targets: WebZen every one in a 5x5 square; S17 up to 4 within 3; OpenMU every one within 2.
  2. Timing: WebZen and S17 hold every blow **1000 ms**; OpenMU strikes at once.
  3. Bleed: WebZen and OpenMU 60% a second for 5 s; S17 none.
  4. Re-hit: WebZen refreshes to the new blow; OpenMU ignores it while one runs.
  5. Same in all: 65 damage, wind, curse damage off the book.

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data, **103** = Main 1.03.35's
`MU/[11.07.12] MU_Chs_1.03.35/source`. Ticks are 25 a second (FPS_ANIMATION_FACTOR 1).

## 2.1. The cast
- `SendRequestSummonSkill` (MM GameLogic/Skills/SummonSystem.cpp:77-121): the target is the selected monster's tile
  (or the cursor's), range-checked by `CheckTile`, then 0x1E MagicContinue (x, y, angle, target key) and at once
  `CastSummonSkill` locally (:120).
- `CastSummonSkill` (SummonSystem.cpp:66-72): turns the caster to the point, plays PLAYER_SKILL_SUMMON (or the mounted
  variant, :47-63), lays `CreateCastingEffect`, hides the book's circling head (`RemoveEquipEffect_Summon`) and calls
  `CreateSummonObject`.
- Others see it from the echo: `ReceiveMagicContinue` sends Explosion, Requiem and Pollution to the same
  `CastSummonSkill` (MM Network/Server/WSclient.cpp:5442-5447).

## 2.2. The casting circle (shared with Pollution, Requiem's colours)
`CreateCastingEffect` (SummonSystem.cpp:190-231) at the caster, all `RENDER_TEXTURE | RENDER_BRIGHT`
(MM Engine/Object/ZzzObject.cpp:1738-1741):
- BITMAP_MAGIC subtype 10 (the subtracted darkness) in (1,1,1) (:194);
- BITMAP_MAGIC subtype 9 (the two turning circles) in **(0.7, 0.7, 1.0)** (:198, :203);
- Suhwanzin1/11/111 in **(0.0, 0.7, 1.0)** (:206, :210-212);
- Suhwanzin2/22/222 in **(0.0, 0.0, 1.0)** (:216, :220-222);
- Suhwanzin4 in **(0.8, 0.5, 1.0)** (:226, :230).
- Their movers are Pollution's (MM Render/Effects/Behaviors/MoveHandlers.cpp:1131-1165; life 40 / 25,
  ZzzEffect.cpp:759-781): already built in game/fx/pollution. A blue circle where Pollution's is violet.

## 2.3. Neil (MODEL_SUMMONER_SUMMON_NEIL)
- Placed by `CreateSummonObject` (SummonSystem.cpp:150-165): **100 units in front of the caster** (`(0,-100,0)` turned
  by his angle; MU's models face -Y), facing his way, with the target's ground point carried in `HeadTargetAngle`, the
  book's tier as subtype: `+0..6 → 0, +7..10 → 1, +11.. → 2` (:125-129). `PlayBuffer(SOUND_SUMMON_NEIL)` (:164).
- Create (MM Render/Effects/ZzzEffect.cpp:797-806): **LifeTime 80**, Scale 1, Alpha 0, **Velocity 0.35** (keys a
  frame), lifted 10 units. 103 is identical (103 ZzzEffect.cpp:1127-1136).
- Model `Data/Skill/summon_neil.bmd`, textures from Skill/ and Effect/ (MM Engine/Object/ZzzOpenData.cpp:4541-4543).
  muextract reads it: **94 bones, one action of 13 keys**, three meshes -- 0 `maskreta01.jpg` 1,566 triangles (her body
  and armour), 1 `gra.jpg` 88 (the blade's edge), 2 `maskreta03.tga` 326 (cape and skirt, alpha cut-out): **1,980
  triangles**. About 2 m tall, holding a long curved blade. Actions never loop (`a.Loop = false`, MM Render/Models/ZzzBMD.cpp:3276),
  so the effect plays keys 0-12 once (`PlayAnimation` at Velocity, MM ZzzEffect.cpp:8521-8530) -- key 12 at frame 34
  (1.37 s) -- and holds; at key 12 she has **sunk into the ground** (export at key 12: only the head and a spike above
  the floor).
- Move (MoveHandlers.cpp:1264-1302; 103 ZzzEffect.cpp:9081-9116):
  - Alpha **+0.04 a frame to 0.7** (frames 0-17), **-0.05 a frame over the last 20** (frames 60-80).
  - **Key > 8** (frame 23, 0.92 s): the blades at the target -- NIFE1, and NIFE2 at subtype >= 1, NIFE3 at >= 2 (:1270-1279).
  - **Key > 10** (frame 29, 1.16 s): GROUND1 60 units in front of Neil, and GROUND1 (+ GROUND2 at >= 1, GROUND3 at >= 2)
    at the target; `PlayBuffer(SOUND_SUMMON_REQUIEM)`, "가시 사운드", **the thorns' sound** (:1281-1297; 103 :9112).
    An EarthQuake shake is commented out (:1299-1300).
- Render (MM Engine/Object/ZzzObject.cpp:1747-1798):
  - While Alpha < 0.7 (fading in or out) meshes 0 and 2 are drawn **additive** at Alpha -- a glowing ghost; at 0.7 they
    are drawn textured (alpha-blended at 0.7) (:1749-1758).
  - Mesh 1, the blade, always **additive in pure red** (`BodyLight (1,0,0)`, :1759-1760).
  - **9 red BITMAP_LIGHT sprites** (Effect/flare01.jpg, ZzzOpenData.cpp:5195) at scale 1 on bones 51-59, every frame (:1762-1769).
  - **The red crescent**: for keys 0-10 (frames 0-29), 10 sub-steps a frame, a `CreateObjectBlur(o, bone 51, bone 59,
    red (1,0,0), type 2)` (:1771-1797): Effect/motion_blur_r.jpg (`BITMAP_BLUR + 2`, ZzzOpenData.cpp:5272), each tail
    living 30 frames, 600 tails at most (MM Render/Effects/ZzzEffectBlurSpark.cpp:31-32, 271-310). Bones 51-59 run
    along her blade, so the ribbon is the blade's sweep -- the big red arc in the footage.

## 2.4. The thorns (MODEL_SUMMONER_SUMMON_NEIL_NIFE1..3)
- Create: LifeTime 50, Scale 1, Alpha 1 (MM Render/Effects/EffectRegistry.cpp:58-61; 103 ZzzEffect.cpp:1138-1143);
  Velocity the default 0.3 (MM ZzzEffect.cpp:371). Move: Alpha -0.05 a frame over the last 20 (EffectBehaviors.cpp:97-103).
- Models `nelleff_nife01/02/03.bmd` (ZzzOpenData.cpp:4547-4552), each **6 keys**, meshes `maskreta01.jpg` (the
  blade metal) and `gra.jpg` (the edge). Render: mesh 0 textured, **mesh 1 additive red** (ZzzObject.cpp:1799-1805).
  At 0.3 a key a frame the 6 keys take 20 frames, then hold.
  | model | tier | triangles | what it is (muextract, keys 0/1/3/5) |
  |---|---|---|---|
  | nife01 | always | 328 (224 gra + 104 metal) | 3-4 blades, 2.8 m tall, at the point; key 0 they are below the floor (y -324..37), **key 1 (frame 3) thrust up 246 units** (bone track 1: z -160 → +86) |
  | nife02 | book +7 | 656 | 8 blades in a ring 1.5-2 m out, leaning outward, rising at key 2 (frame 7) to 2 m |
  | nife03 | book +11 | 1,312 | 16 blades in two outer rings 2.5-4.4 m out, rising in waves at keys 3-4 (frames 10-13) |
- So the thorns come out **from the centre outward** over half a second: MU's spread of the strike, drawn.

## 2.5. The burning ground (MODEL_SUMMONER_SUMMON_NEIL_GROUND1..3)
- Create: LifeTime 50, Alpha 0 (EffectRegistry.cpp:62-65); Move: Alpha **+0.3 a frame** (in four frames), -0.05 over
  the last 20 (EffectBehaviors.cpp:105-111). One key each.
- Models `nell_nifegrund01/02/03.bmd` (ZzzOpenData.cpp:4553-4558). Render: **both meshes additive**, mesh 1's U
  scrolled by -WorldTime x 0.001 (ZzzObject.cpp:1806-1810). `bossrocklight_R.jpg` is also bright by its own `_R` name
  (MM Render/Sprites/TextureScript.cpp:38-40, ZzzBMD.cpp:2236-2240).
  | model | where | triangles | what |
  |---|---|---|---|
  | grund01 | before Neil and at the point | 22 (2 `fury` + 20 `bossrocklight_R`) | a 3 x 2.4 m floor burst (Skill/fury.jpg, an orange star crack) under a 2.8 m box of scrolling flame walls |
  | grund02 | the point, book +7 | 176 (16 `test2` + 160 `bossrocklight_R`) | a ring 1-1.5 m out of 8 flame wedges and orange crack streaks (Skill/test2.jpg) |
  | grund03 | the point, book +11 | 176 | the same, 2-2.7 m out |

## 2.6. The bleed on the struck body (eDeBuff_NeilDOT = 74)
- `eDeBuff_NeilDOT = 74` (MM _enum.h:3968). On it: `CreateDamageOfTimeEffect(REQUIEM)` (WSclient.cpp:15228-15231) lays
  MODEL_SUMMONER_EQUIP_HEAD_NEIL **subtype 1** on the body (SummonSystem.cpp:356-372), LifeTime 100000
  (ZzzEffect.cpp:728-745), never drawn (ZzzEffect.cpp:8891-8895), removed with the debuff (WSclient.cpp:15442-15445;
  SummonSystem.cpp:375-386) or the body's death (MoveHandlers.cpp:1118-1123).
- Each frame it throws one `BITMAP_LIGHT + 2` subtype 5 (MoveHandlers.cpp:1124-1126): a puff of Effect/fi01.jpg
  (`BITMAP_ADV_SMOKE`, 128x128, ZzzOpenData.cpp:5183) off **a random bone**, flung 6-6.1 units a frame outward and 4 up,
  gravity 0.4, life 21, scale 0.08-0.11 growing 0.04 a frame, spinning, alpha -0.1 a frame over its last 10, coloured
  **(1, 0, 0.6) x alpha -- magenta-red**, additive (MM Render/Effects/ZzzEffectParticle.cpp:855-883 create, :4015-4040
  move, :9159-9164 render). About 21 live a bleeding body: a thin magenta blood mist spraying off it for the 5 s.

## 2.7. Sounds
- `SOUND_SUMMON_NEIL` = `Data/Sound/SE_Ch_summoner_skill06_requiem01.wav` (**2.50 s**, 44.1 kHz mono) at the cast;
  `SOUND_SUMMON_REQUIEM` = `SE_Ch_summoner_skill06_requiem02.wav` (**1.37 s**) with the thorns, both one voice
  (ZzzOpenData.cpp:4911-4912; enum MM Audio/DSPlaySound.h:768-769). The S17 client's requiem01 is byte-identical.

## 2.8. Not ours
- The book holder's circling Neil head (MODEL_SUMMONER_EQUIP_HEAD_NEIL subtype 0, `nillsohwanz.bmd` 292 triangles,
  SummonSystem.cpp:331-335, MoveHandlers.cpp:1072-1114): it is the book's idle look, and he holds a staff.
- PLAYER_SKILL_SUMMON: the Summoner's pose; the Soul Master keeps his own cast.

## 2.9. MuMain vs 1.03.35
- Create, move, timings and the casting colours are identical (103 ZzzEffect.cpp:1127-1150, :9081-9131;
  103 SummonSystem.cpp:158-172, :214-249). MuMain moved NIFE/GROUND into EffectRegistry and EffectBehaviors and uses
  `CreateEffectFpsChecked`; the numbers did not change.

## 2.10. Timeline (25 frames a second, frame 0 = the cast)
| frame | s | what |
|---|---|---|
| 0 | 0 | blue casting circle under him (40 frames); Neil appears 1 m before him, invisible; requiem01.wav (2.5 s) |
| 0-17 | 0-0.7 | Neil fades in as an additive ghost to 0.7, then solid at 0.7; her blade red, nine red flares on it |
| 0-29 | 0-1.16 | she swings; the red ribbon (motion_blur_r) follows the blade, each slice 30 frames |
| 23 | 0.92 | thorns spawned at the point, below the floor; nife01 up at frame 26, nife02 at ~30, nife03 at ~33-36 |
| 29 | 1.16 | burning ground under the point and before Neil (fades in by 33); requiem02.wav |
| 25 (server) | 1.0 | WebZen/S17 strike every body; the bleed starts, puffs from each |
| 34 | 1.37 | Neil at key 12, sunk into the ground, held |
| 53-73 | 2.1-2.9 | thorns fade (life 50 from 23); ground fades (50 from 29) to 79 |
| 60-80 | 2.4-3.2 | Neil fades out |
| 25-150 | 1-6 | bleed pulses at +1..+5 s, magenta mist off the body |

## 2.11. Assets (under D, all present; the S17 client has the same files)
| What | File | Size | Exists | In our content |
|---|---|---|---|---|
| Neil | Skill/summon_neil.bmd | 94 bones, 13 keys, 1,980 tris | yes | no |
| Neil's body | Skill/maskreta01.OZJ | 256x256 | yes | no |
| Neil's cape | Skill/maskreta03.OZT | 128x128 RGBA | yes | no |
| blade edge (Neil and thorns) | Skill/gra.OZJ (also Effect/gra.OZJ, same) | 256x64 gradient | yes | no |
| thorns | Skill/nelleff_nife01/02/03.bmd | 6 keys, 328/656/1,312 tris | yes | no |
| burning ground | Skill/nell_nifegrund01/02/03.bmd | 1 key, 22/176/176 tris | yes | no |
| flame walls | Skill/bossrocklight_R.OZJ | 128x128 | yes | no |
| floor burst | Skill/fury.OZJ | 128x64 | yes | **yes**, content/effects/fury/fury.png is the same pixels |
| crack streaks | Skill/test2.OZJ | 128x32 | yes | no (effects/fury/test_2.png is another file) |
| blade flares | Effect/flare01.OZJ | -- | yes | yes, content/effects/light/flare01.png |
| the red ribbon | Effect/motion_blur_r.OZJ | -- | yes | yes, content/effects/trail/motion_blur_r.png |
| bleed mist | Effect/fi01.OZJ | 128x128 | yes | no |
| casting circle | Suhwanzin1/11/111/2/22/222/4, BITMAP_MAGIC sheets | 6x36 + 24 tris | yes | yes (effects/pollution, effects/weakness) |
| cast sound | Sound/SE_Ch_summoner_skill06_requiem01.wav | 2.50 s | yes | no |
| thorns sound | Sound/SE_Ch_summoner_skill06_requiem02.wav | 1.37 s | yes | no |

# 3. Footage

YouTube "Mu Online Summoner Skill Book of Neil Reguiem" (id xfrjJrFOpxs, 1280x720, 29.97 fps, 22 s): a Summoner with
a high book (the thorns' three rings show, so +11 or more) casting Requiem again and again at a Dragon on a dark
floor, a cast about every 3.2 s. Still strip `requiem-footage.jpg`, cropped to the fight, times from the first frame
of the clip (the circle appears at ~0.17 s, so subtract it for MU's frame 0).

| still | t - 0.17 s | what shows |
|---|---|---|
| 0.17 s | 0 | the white-blue casting circle on the floor under her; a faint red streak low before her (Neil's blade starting) |
| 0.67 s | 0.5 | the circle glows blue, a blue haze on her; a **red crescent** sweeps up between her and the dragon, at chest height |
| 1.00 s | 0.83 | the crescent is over her head, deep red, a wide half-moon; a dark translucent figure under it (Neil) |
| 1.17 s | 1.0 | the crescent comes down on the dragon; Neil's dark ghost stands between them, blade forward |
| 1.33 s | 1.16 | the first blades out under the dragon, white-hot glow on the floor round its feet |
| 1.67 s | 1.5 | **a thicket of tall red blades** right through the dragon, leaning outward, pale white-pink glow on the floor; damage numbers |
| 2.33 s | 2.16 | the thicket at its widest, twenty-odd blades, the outer ones leaning far out; the circle gone |
| 2.83 s | 2.66 | the blades fading to a pink haze; the floor glow lingers; **small magenta-red puffs** drift up off the dragon (the bleed) |

The footage agrees with the client trace: the blue circle, Neil a ghost whose blade throws the red arc, the thorns
out about 1.0-1.2 s after the cast from the centre outward, additive red blades over a white-orange burning floor,
the whole gone by about 3 s, the bleed's magenta mist after. The second clip found (h7a5kboqZwA, "Mu Argentina -
REQUIEM", 192x144) is too small to add anything.

# 4. Fit to our code (main, 2026-10-25) and the budget

## 4.1. The rune
- **Power**: `Power::Requiem = 78`, after `DrainLife = 77` (src/sim/items.h:836-842); the numbers must run unbroken
  (drawRunePower walks them, items.h:839-840). The row goes into `powerOf`'s table after Drain Life
  (src/sim/items.cpp:713-726) as `kWizardOnly, kInWeapon, Rarity::Ancient, true, true`, and into the "Doesn't stack"
  case list (items.cpp:739-742). A rules change: client and servers go out together, as the other four.
- **The skill number**: `sim::skill::kRequiem = 224` beside kPollution and kDrainLife (src/sim/skills.h:160-166), MU's
  number, never a row, so kSkills and the save keep their layout.
- **Constants** in items.h after Drain Life's (items.h:1312-1320).
- **The roll**: a branch in `Realm::callDown` (src/sim/realm_fight.cpp:540) like Pollution's, reached once a cast or
  landed swing from `Realm::land` (:479) and once a channel from `channelRunes` (:485-503); the target from `runeAim`
  (:1087-1100: what he struck, else the nearest he sees within kStormcallReach 4); the blow `runeStrike` (:1102-1115)
  with `runeForce(hero, Element::Wind)`; said as `What::Loosed` with skill 224, `rune = true`, x/y the point.
- **The hold**: MU holds the blow 1000 ms. Our runes strike on the tick, but the realm has a held-blow list already
  (`spiritBlows_`, Evil Spirit's 0-2 s holds, src/sim/realm.h:2209, realm_fight.cpp:1117-1150); Requiem needs the same kind of thing: victims
  chosen at the roll, struck 20 ticks later if alive, his while he stands.
- **The bleed**: our monsters already bleed -- `Body::bleed/bleedUntil/bleedNext/bleedDamage/bleedBy`
  (src/sim/realm.h:675-680), pulsed by `Realm::bleedPulse` (src/sim/realm_burn.cpp:114-138, a monster's pulse the whole
  bite, may kill, "Hit" with `bled`), laid by `rend` for the bleed runes (realm_burn.cpp:101-112). kBleedEvery 20 and
  kBleedTicks 100 (src/sim/realm_tuning.h:331-333) are **exactly MU's 1 s and 5 s**: one stack, bleedDamage = 0.6 x the
  landed wound, bleedUntil = tick + 100, bleedNext = tick + 20 gives MU's five pulses. The landed wound is health
  before minus after, as Drain Life measures it (realm_fight.cpp:851-870).
- **The drawing**: a `sim::skill::kRequiem` branch in play.cpp's Loosed switch (src/game/play.cpp:1360-1390, beside
  Pollution's :1373-1380), a new `game/fx/requiem.{h,cpp}` on game/fx/pollution's pattern (baked poses through
  index.py's `_models(root, build, "requiem")`, pipeline/index.py:2124-2125), wired in play.h, play_open.cpp,
  play_mode.cpp and CMakeLists.txt as the other four. The casting circle is Pollution's code with Requiem's colours.
- **Sounds**: requiem01.wav and requiem02.wav into content/sounds with two sounds.json keys (`mu edit`, as
  `spell_pollution`, content/sounds/sounds.json:2775-2781), cooked with `./mu make sounds`.
- **Tests**: a `testRequiemRune` in tests/sim_test.cpp on the Pollution/Drain pattern (:12109-12240): the level-150
  Soul Master's Lightning hunt in Lorencia, never more than kRequiemMost struck, none past kRequiemTiles of the point,
  blows 20 ticks after the Loosed, five bleed pulses each, none below Aida in the drops.

## 4.2. Budget (the transparent pass, one triangle or quad a sprite)
- The pass holds **16,384** since Tarkan, 2026-10-23 (src/gfx/effects.h:84-92), twice what Pollution met (8,192).
- One Requiem at its busiest (frames ~30-60, everything alive at once):
  | piece | +0..6 book | +7..10 book | +11 book |
  |---|---|---|---|
  | casting circle (6 x 36 + 24, BITMAP_MAGIC quads) | ~250 | ~250 | ~250 (gone by frame 40) |
  | Neil | 1,980 | 1,980 | 1,980 |
  | red ribbon (10 slices a frame x 30-frame tails) | <=300 | <=300 | <=300 |
  | blade flares | 9 | 9 | 9 |
  | thorns | 328 | 984 | 2,296 |
  | burning ground (two grund01 + ring) | 44 | 220 | 396 |
  | **total** | **~2,900** | **~3,750** | **~5,250** |
  | bleed mist, each bleeding body, 5 s | 21 | 21 | 21 |
- Compared: one Pollution at its +7 tier is two Laguls, 2,510, plus circle and puffs (bf2e7354). Requiem at +7 is
  half again as much, mostly Neil herself: 1,980 is one and a half Laguls.
- With a 10% roll on every cast and every landed swing, and the look lasting 3.2 s, two live at once will be usual in a
  fast hunt: ~7,500 at the +7 tier, ~10,500 at the top -- inside 16,384 alone, tight with a town's lights, the other
  Ancients and a crowd's spells.

# 5. Proposed rune and what is decided (from MU's numbers)

1. **Power 78 `Power::Requiem`, "Requiem"**, the Soul Master's (kWizardOnly, second class), one to a thing, in his
   weapon, Rarity::Ancient, beside the other four; drawn among the Ancients' share as they are.
2. **Chance 10%** (`kRequiemChance = 0.10`), on a cast or a landed swing, once a channel: the other four's, ours, to tune.
3. **Where**: on what he struck; on a cast that struck none, the nearest he sees within kStormcallReach (`runeAim`).
4. **Who**: that one first, then the others round it within **2 tiles** (`kRequiemTiles = 2.0`) in body order, **up to
   4** (`kRequiemMost = 4`), chosen at the roll. 2 tiles is WebZen's 5x5 square and OpenMU's effect range, and where
   the drawn thorns stand at the +7 tier; 4 is S17's count and Pollution's.
5. **Force 0.975, wind** (`kRequiemForce = 0.975f`): MU's 65 against Pollution's 80 at 1.2, so his wind runes and ring
   raise it; his rune's blow (`runeStrike`).
6. **When**: **held 20 ticks** (WebZen's and S17's 1000 ms), so the numbers land with the thorns; one that died
   meanwhile is skipped.
7. **The bleed** (WebZen and OpenMU): each one the blow lands on bleeds **60% of that blow** (`kRequiemBleedShare =
   0.6f`) **each second for 5 s** -- five pulses, no defence, may kill, his while he stands -- on our existing monster
   bleed (one stack, kBleedEvery/kBleedTicks). A second Requiem on a bleeding body restarts it at the new blow
   (WebZen's refresh). No boss shrug: the bleed runes have none either, and MU asks none.
8. **The tooltip**: "10% on a cast or a hit: Neil's thorns strike up to 4 monsters within 2 tiles; they bleed 60% of
   the hit each second for 5 s".
9. **The look, MU's, at the book's +7 tier** (Pollution drew its +7 book's): the blue casting circle; Neil 1 m before
   him, 13 keys at 0.35, ghost-additive to 0.7 then solid, red blade, nine red flares, the red motion_blur_r ribbon for
   keys 0-10; at key 8 nife01 + nife02 at the point (6 keys at 0.3); at key 10 grund01 before Neil and at the point
   and grund02, flame U scrolling; requiem01 at the cast, requiem02 with the ground; the magenta fi01 mist off each
   bleeding body while it bleeds. Neil baked as Pollution baked the Lagul (keys 0, 2, ... 12 blended, the blade's two
   bones' places at each key kept for the ribbon); the thorns' six keys baked whole.
10. **Said**: `What::Loosed`, skill 224, `whom` the first, `c` the count struck, x/y the point; the held blows and
    bleed pulses are ordinary `Hit`s (`bled` on the pulses).

# 6. Open calls for the user

1. **How hard the bleed bites.** MU's bleed is three times the blow over 5 s, so a proc does about **4x its blow per
   body** (0.975 + 5 x 0.6 x 0.975 = 3.9 force-equivalent) against Pollution's 1.2 -- about **3.2x Pollution's total**
   on the same four bodies, spread over 5 s. Kept as MU's here; if the Ancient hunt in sim_test shows Requiem far ahead
   of its siblings, a lower share (0.2 a pulse makes the whole proc 2x its blow) is the knob.
2. **The tier drawn.** +7 (thorns 984, ~3,750 a proc) as Pollution chose, or the footage's +11 three rings (2,296,
   ~5,250), which reads far better and is what players know from the clips. Either fits 16,384; two at once at +11 is ~10,500.
3. **One at a time?** Pollution draws one ghost set at a time. Requiem could do the same (a proc while one is drawn
   strikes but is not drawn again), or allow two.
4. **The bleed's drawing.** Our monster bleed already throws blood drips and spatter on each pulse (the bleed runes'
   look); MU's Requiem bleed is a magenta mist instead. Draw MU's mist for a Requiem bleed (needs the bleed to know who
   laid it -- a flag on the Body), or keep the one bleed look.
5. **A knight's bleed and a Requiem on the same monster.** Our Body holds one bleed (stacks, damage, owner). A Requiem
   on a body a Dark Knight's Rend already bleeds would overwrite it (WebZen's replace), or should take the larger, or
   add a stack. Proposed: Requiem replaces only when its pulse is larger.
