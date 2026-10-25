# Sleep (skill 219, the Summoner's curse) as an Ancient rune -- research dossier

Researched 2026-10-25 before any build, for the seventh Ancient rune on the Soul Master's weapon, after Ice Storm (74),
Weakness (75, 142d7486), Pollution (76, bf2e7354), Drain Life (77, 064e1331) and Requiem (78, b5895e24). The user
picked it with Requiem and Berserker ("we need this on our side"). Shaped as requiem.md: section 1 the server, 2 the
client, 3 the footage, 4 the fit to our code and the budget, 5 the proposed rune and what is decided, 6 the open
calls. Sources: WebZen GameServer 1.00.93 (the shallow clone in this session's scratchpad, `wz/webzen`, cp949, read
through iconv with line numbers unchanged), the S17 server (`third_party/MuSource_S17`), OpenMU
`LEGACY/reference/openmu`, MuMain `LEGACY/reference/MuMain`, and YouTube footage.

**In one paragraph.** Sleep is the Summoner's first curse (Korean 슬립): **one monster or player, no damage**, put to
sleep for a few seconds -- it neither walks, thinks, swings, casts nor teleports, its animation stops in its pose --
**until any blow lands on it**, which wakes it at once. Whether it takes is a roll off the caster's energy and his
book's curse power (monsters `20 + energy/30 + curse/6` %, players `15 + energy/37 + curse/6` %), and how long off
energy against the target's level (monsters `5 + energy/100 - level/20` s). MU draws it as **two counter-rotating
violet rings** round the target's middle with a flare and violet streaks drawn into them for 1.4 s, a **violet magic
circle** growing on the ground under the caster, and, while it sleeps, **soft violet puffs rising off its body**.
It is the first crowd control among the Soul Master's Ancients that holds a monster without striking it.

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93; cp949)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/`
- `S17/` = `MU2_BGFX/third_party/MuSource_S17/` (`GameServer/`, `_MuServer/Databases/`)
- `OM/` = `LEGACY/reference/openmu/src/`

## 1.1. Name and version
- WebZen enum: `AT_SKILL_SLEEP = 219`, "슬립", beside `AT_SKILL_BLIND = 220` (WZC/include/public.h:320-321), under
  `ADD_SEASON_3_NEW_CHARACTER_SUMMONER_20070912` everywhere it is used (e.g. WZ/ObjUseSkill.cpp:1272-1290): a **Season 3**
  skill, the Summoner's launch set. It is learned from a scroll, item index 24 of the scroll group
  (`Index == 24 → AT_SKILL_SLEEP`, WZ/MagicInf.cpp:119-122), with no energy requirement of its own in the code
  (WZ/MagicDamage.cpp:761).
- **WebZen's data has no row for it**: WZD/lang/main/skill.txt ends at 202 (as found for Requiem). So mana, range and
  the scroll's requirement come from S17 and OpenMU; the rules from WebZen's code.
- S17: `SKILL_SLEEP = 219` (S17/GameServer/SkillDef.h:127), `BUFF_SLEEP = 72` (S17/GameServer/BuffDef.h:65),
  buff_template 72 "Sleep", "Short period of nap", sent, a debuff (S17/_MuServer/Databases/game.sql:115; columns
  game.sql:24-39). "Sleep Parchment" 15/24, level 40, energy 180 (game.sql:23938).
- OpenMU: `SkillNumber.Sleep = 219` (OM/Persistence/Initialization/Skills/SkillNumber.cs:103), magic effect 0x48 = 72
  (OM/.../Skills/MagicEffectNumber.cs:247). MuMain: `AT_SKILL_ALICE_SLEEP = 219` (MM Core/Globals/_enum.h:429),
  `eDeBuff_Sleep = 72` (MM _enum.h:3966). The master-tree "Sleep Strengthener" 454 is not ours.

## 1.2. Skill row
- S17 `skill_template` 219 (game.sql:45680; columns game.sql:45543-45590): `level 40, damage 0, mana 20, stamina(AG) 3,
  distance 6, effect_radio 0, delay 0, type 1, attribute -1 (none), buff_icon 72, required_energy 180, base 219,
  allow_sum 1, pvm 100 / pvp 100`. **No damage, no element, single target, range 6.**
- OpenMU: `CreateSkill(Sleep, "Sleep", AllSummoners, distance: 6, abilityConsumption: 3, manaConsumption: 20,
  energyRequirement: 180, skillType: Buff)` (OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:215).
  The same numbers.
- S17 server settings: `Skill.SleepLimit 70` (the chance cap, %), `Skill.SleepLimitTime 70` (the duration cap, s),
  `Skill.SleepReuseTime 1000` (ms between two Sleeps by one caster) (game.sql:42724-42726; read at
  S17/GameServer/GameServer.cpp:606-607, :1134).

## 1.3. The cast (WebZen)
- `CObjUseSkill::SkillSleep` (WZ/ObjUseSkill.cpp:6371-6470): sends the skill number at once
  (`GCMagicAttackNumberSend(..., 1)`, :6383), walks the viewport for the one aimed target -- a monster that is not a
  summon, a duel enemy, anyone in Chaos Castle / Illusion Temple, the siege, a guild-war enemy (:6397-6450) -- and, if
  it is within the skill's distance (`gCheckSkillDistance`), calls `gObjAttack` on it **and stops** (:6452-6462).
  **One target.** No area.
- In `CObjAttack::Attack` the blow first passes the ordinary **MissCheck** (WZ/ObjAttack.cpp:629-646), so a Sleep can
  miss on the attack-rate roll before its own roll; then in the per-skill switch, `case AT_SKILL_SLEEP`
  (WZ/ObjAttack.cpp:1563-1630):
  ```
  // MODIFY_SLEEP_SKILL_SUCCESSRATE_BUGFIX_20080125 branch
  monster:  skillSuccessRate = 20 + Energy/30 + m_Curse/6;     nEffectTime = 5 + Energy/100 - target.Level/20;
  player:   skillSuccessRate = 15 + Energy/37 + m_Curse/6;     nEffectTime = 4 + Energy/250 + (Level - target.Level)/100;
  if (skillSuccessRate < rand()%100) → "miss" (GCDamageSend 0), return
  if (nEffectTime < 1)               → "miss", return
  if (gObjCheckPowerfulEffect(target, BUFFTYPE_SLEEP, nEffectTime, 0)) → refused, return   // a longer sleep stands
  gObjAddBuffEffect(target, BUFFTYPE_SLEEP, EFFECTTYPE_SLEEP, nEffectTime, 0, 0, nEffectTime); return TRUE;
  ```
  The older branch (`#else`) was `15 + (Energy/9*Curse/8)/100` and `10 + Energy/150 - Level/10` (:1576-1585,
  :1599-1607). The repo's PatchDefine.h is empty, so the define cannot be read here, but **S17 and OpenMU both carry
  the bugfix branch's formulas** (below), so that is the live one.
- **No damage**: the case returns TRUE (:1628) before the blow's damage is applied (`gObjLifeCheck`,
  WZ/ObjAttack.cpp:2982-3021), and before the wake-up below, so a Sleep never wakes itself.
- `m_Curse` is the Summoner's curse power (her book's): the curse-branch switch lists Sleep among the book skills
  (WZ/ObjAttack.cpp:3989-4001). The Soul Master holds no book: **0**, as the Weakness rune already reads it
  (src/sim/items.h, `weaknessOdds`, `0 / 6`).
- The duration is in **seconds** (the buff's Duration, counted down once a second by gObjSecondProc, as Requiem's bleed;
  see requiem.md 1.5). EFFECTTYPE_SLEEP changes no stat (WZ/BuffEffect.cpp:155-156, :425-426): the sleep is the
  checks below.

## 1.4. What sleeping means (WebZen)
`gObjCheckUsedBuffEffect(obj, BUFFTYPE_SLEEP)` is asked at:
- a monster finding a target's place: refused (WZ/gObjMonster.cpp:602-607); teleporting: refused (:2478-2483,
  WZ/TMonsterSkillElement.cpp:882-887); moving: refused (WZ/gObjMonster.cpp:2600-2605, WZ/TMonsterAIUtil.cpp:127-132);
  **attacking: refused** (WZ/gObjMonster.cpp:2674-2677); its path step skipped (:2708-2718, WZ/user.cpp:889-895);
- a player: no attack/skill packet (WZ/protocol.cpp:14152-14161), no teleport (:15738-15746, :15990-15992).
- **Woken by a landed blow**: in `CObjAttack::Attack`, after the miss check and once the blow is taken, any sleep on
  the target is removed (`gObjRemoveBuffEffect(lpTargetObj, BUFFTYPE_SLEEP)`, WZ/ObjAttack.cpp:2570-2575), next to
  the stone-curse countdown. Any attacker, any skill. A miss does not wake it; damage-over-time does not either
  (the DoT pulse is `GiveDamageEffect`, WZ/BuffEffect.cpp:572-650, which never calls Attack nor touches the sleep).
- **Bosses**: WebZen's Sleep path asks no boss flag. What stops it is the generic immunity before any skill lands:
  `BUFFTYPE_MONSTER_MAGIC_IMMUNE`, `m_ImmuneToMagicCount`, the monster-skill immune timers (WZ/ObjAttack.cpp:318-391) --
  the event bosses' own shields. No per-class Sleep immunity.

## 1.5. S17
`SkillHandler::SkillBuffSleep` (S17/GameServer/SkillHandler.cpp:1314-1414):
- range check (:1326-1330); **element resist** on the skill's attribute (:1332-1336, none for -1); attack allowed
  (:1338-1342); the caster's 1000 ms reuse timer (:1344-1351);
- monster: `20 + energy/30 + bookCurse/6` %, `5 + energy/100 + targetLevel/20` s -- **plus** the level, where WebZen and
  OpenMU subtract it (:1356-1360; read as S17's slip); player: `15 + energy/37 + curse/6` %, `4 + energy/250 +
  (level - targetLevel)/100` s (:1361-1365);
- capped at SleepLimit 70% and SleepLimitTime 70 s (:1367-1375), + the master tree (:1377), refused at <= 0, clamped
  to 100, rolled (:1379-1394);
- `IsDebuffInmune` -- Iron Defense, or Errtel immunity for elemental ones (S17/GameServer/UnitSkill.cpp:310-326) --
  refuses (:1396-1400); a creature's **stun resistance** (`UNIT_FLOAT_RESIST_STUN`) rolls it off (:1402-1409). The raid
  and event bosses set that to **100** (S17/GameServer/ai_raklion.cpp:126, ai_ferea.cpp:100, :274, :494,
  ai_swamp_of_darkness.cpp:234, :737, ai_custom_boss.cpp:25): **S17's bosses never sleep.**
- Asleep: a monster's reactions stop (S17/GameServer/Monster.cpp:579-584); a player cannot act
  (S17/GameServer/PlayerCharacter.cpp:6474-6477). The wake-up on a landed blow survives only in the Dark Raven's
  attack (`RemoveBuff(BUFF_SLEEP)` when damage > 0, S17/GameServer/PlayerDarkSpirit.cpp:490-493); the ordinary attack
  file is not in this source tree, so S17's general wake-up cannot be cited.

## 1.6. OpenMU (S6)
- `SleepEffectInitializer` (OM/Persistence/Initialization/Skills/SleepEffectInitializer.cs:28-106): chance
  `0.20 + energy/3000 + bookRise/600` (= 20 + energy/30 + rise/6 %, :41-55), PvP `0.15 + energy/3700 + rise/600`
  (:57-71); duration `5 + energy/100`, **max 20 s** (:73-82), PvP `4 + energy/250 + level/100`, **max 10 s** (:84-99);
  `DurationDependsOnTargetLevel`, divisors **20** for monsters and **100** for players (:37-39), applied as
  `duration -= target.Level / divisor`, and under 1 s it does not take (OM/GameLogic/AttackableExtensions.cs:886-901).
  `StopByDeath` (:36). It sets `Stats.IsAsleep` (:101-105).
- Rolled once (`Rand.NextRandomBool(chance)`, AttackableExtensions.cs:348-352), no hit roll (a Buff skill,
  OM/GameLogic/PlayerActions/Skills/TargetedSkillDefaultPlugin.cs:298-302); a walking target is stopped where it is
  (AttackableExtensions.cs:943-955).
- Asleep: a monster's AI does nothing (OM/GameLogic/NPC/BasicMonsterIntelligence.cs:219-222); a player cannot hit, cast
  or move (OM/GameLogic/PlayerActions/HitAction.cs:36-40, .../Skills/TargetedSkillDefaultPlugin.cs:62,
  .../Skills/AreaSkillAttackAction.cs:176, OM/GameLogic/PlayerMovement.cs:88).
- **Woken when a blow does health damage**: a monster (OM/GameLogic/NPC/AttackableNpcBase.cs:123-128), a player after
  any non-zero hit (OM/GameLogic/Player.cs:684-698). No boss immunity anywhere for it.

## 1.7. Differences
1. Chance: the same in all three (monster `20 + energy/30 + curse/6`); S17 caps it at 70%.
2. Duration: WebZen `5 + energy/100 - level/20`, no cap; OpenMU the same capped at 20 s; S17 `+ level/20`, capped 70 s.
3. Hit roll first: WebZen yes (MissCheck); OpenMU and S17 no.
4. Bosses: WebZen only through the generic magic immunity; S17 never (stun resistance 100); OpenMU always.
5. Wake: WebZen any landed blow; OpenMU any blow with health damage; both ignore damage-over-time.
6. Re-cast on a sleeper: WebZen refuses one shorter than what is running; S17/OpenMU replace.

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data. 25 frames a second.

## 2.1. The cast
- The echo `ReceiveMagic` for 219: `AttackTime = 1`, the PLAYER_SKILL_SLEEP pose (or the mount's), and
  `PlayBuffer(SOUND_SUMMON_SKILL_SLEEP)` at once (MM Network/Server/WSclient.cpp:4818-4840); the local cast the same
  (MM GameLogic/Combat/SkillCast.cpp:685-740). The pose plays at `0.3 + MagicSpeed2` (MM Engine/Object/ZzzCharacter.cpp:975).
- **The hand**, every frame of the pose (MM ZzzCharacter.cpp:10593-10742), at bone 37 "Bip01 L Hand": two Shiny
  sprites (`BITMAP_SHINY + 5`) at 1.0 and 0.7 turning both ways in **(0.5, 0.2, 0.8)**; two `BITMAP_PIN_LIGHT` at 1.7
  and 1.5 in **(0.7, 0, 0.8)**; and a frame in one a `BITMAP_LIGHT + 2` particle (Effect/cra_04) and a
  `BITMAP_CLUD64` cloud in **(0.6, 0.1, 0.8)**. The violet glow round her hand in the footage. Weakness's rune left
  MU's hand sprites out (src/game/fx/weakness.h, "Ours"): the Soul Master casts no Sleep of his own.

## 2.2. The let-go (AttackTime 15, frame 14, 0.56 s after the cast)
In the block that runs when the cast's AttackTime reaches its limit of 15 (MM ZzzCharacter.cpp:4140-4150, the switch
at :5160-5207):
- **Under the caster**: `BITMAP_MAGIC + 1` subtype 11 (Effect/Magic_Ground2.jpg, MM Engine/Object/ZzzOpenData.cpp:5197)
  in **(0.7, 0.3, 0.8)** (:5167-5182). LifeTime 20 (MM Render/Effects/ZzzEffect.cpp:1216-1219); drawn on the terrain,
  additive, scale `(20 - LifeTime) x 0.15` -- **0 to 3 tiles across in 20 frames** -- full bright, fading over its
  last 5 frames (ZzzEffect.cpp:9787-9799, :9819-9822, :9868-9870, :9882). A violet rune circle opening under him.
- **On the target**: `MODEL_ALICE_BUFFSKILL_EFFECT` and `MODEL_ALICE_BUFFSKILL_EFFECT2`, subtype 0, in **(0.8, 0.3, 0.9)**
  (:5188-5193).

## 2.3. The rings (MODEL_ALICE_BUFFSKILL_EFFECT / EFFECT2, subtype 0)
- Models `Data/Effect/elshildring.bmd` and `elshildring2.bmd`, textures from Effect/ (MM ZzzOpenData.cpp:4509-4512).
  muextract: each **80 triangles, one bone, one key**, one mesh on `macardkmono.jpg` (D/Effect/macardkmono.OZJ).
  elshildring is a ring 100 units in radius and 60 tall (a band leaning in, x/z +-100, y +-30); elshildring2 a flat
  ring 109 units in radius, 9 tall.
- Create (MM ZzzEffect.cpp:875-901, :904-914): LifeTime **34** (EFFECT2 **35**), lifted **100 units** (a metre), Alpha 0,
  BlendMeshLight 0, Scale **0.1** (EFFECT2 **0.15**), the given light.
- Move (MM Render/Effects/Behaviors/MoveHandlers.cpp:2213-2336): follows the target (+100 up); for LifeTime > 20 Alpha
  and BlendMeshLight **+0.05 a frame** (to 0.7 by frame 14), then -0.05 a frame till gone; turns **+8 degrees a
  frame** (EFFECT2 **-8**); **Scale +0.035 a frame** (to ~1.29 / ~1.37: rings ~1.3-1.5 m in radius); and every frame:
  - two `BITMAP_LIGHT` (Effect/flare01.jpg, ZzzOpenData.cpp:5195) at scale **5** in (0.8, 0.1, 0.9) x Alpha (:2253-2269);
  - Shiny at **2.0** and **1.0**, turning both ways, in (0.7, 0.6, 0.9) x Alpha (:2271-2297). `BITMAP_SHINY + 5` is
    loaded twice; the later, **Effect/shiny05.jpg**, wins (ZzzOpenData.cpp:5267, :5313);
  - **three streaks** `BITMAP_JOINT_HEALING` subtype 15 (Effect/JointEnergy01.jpg, ZzzOpenData.cpp:5289) in
    (0.7, 0.5, 0.7), born 200 units out at random above the centre (:2299-2328): LifeTime 10, 2 tails, width 5
    (MM Render/Effects/ZzzEffectJoint.cpp:567-573), homing on the centre at 10 degrees a frame, speeding up 4 a frame,
    the light x 1/1.08 a frame, each frame a `BITMAP_SHINY + 1` (Effect/Shiny02.jpg, ZzzOpenData.cpp:5263) sparkle at
    0.8-1.4 at the centre (ZzzEffectJoint.cpp:3598-3608). Violet threads drawn into the rings.
- Render: `RENDER_TEXTURE | RENDER_BRIGHT` (additive) at Alpha with BlendMeshLight (MM Engine/Object/ZzzObject.cpp:629-639);
  only subtypes 0-2 are drawn as models (ZzzEffect.cpp:9162-9169).

## 2.4. While it sleeps (eDeBuff_Sleep, MODEL_ALICE_BUFFSKILL_EFFECT subtype 4)
- On the debuff: `CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, ..., (0.7, 0.1, 0.9), 4, o)` (MM WSclient.cpp:15252-15257),
  kept alive each frame in **(0.8, 0.3, 0.9)** while the buff holds (MM ZzzCharacter.cpp:10888-10899), deleted with it
  (WSclient.cpp:15460-15463) or at death (MM ZzzCharacter.cpp:3208-3214). LifeTime 100, Scale 1 (ZzzEffect.cpp:888-899),
  never drawn as a model.
- Each frame (MoveHandlers.cpp:2338-2378), on a random non-dummy bone of the body, **one frame in two**, 20 units
  below it, a `BITMAP_TWINTAIL_WATER` particle subtype 2 (Effect/water.jpg, ZzzOpenData.cpp:5428): LifeTime 60-69,
  scale 1.0-1.6, scattered +-20 / 0-40 units, random turn, rising 2-3 units a frame, shrinking 0.026 a frame, alpha
  -0.05 a frame (gone in 20), the light x 1/1.02 a frame (MM Render/Effects/ZzzEffectParticle.cpp:1131-1141,
  :5253-5267), alpha-blended (:9006-9010). **A soft violet haze bubbling up off the sleeper**, ~10 puffs visible.
- **The body stops**: its animation is not played (MM ZzzCharacter.cpp:2553-2556), held weapons' and cloth's too
  (:6979-6982; ZzzObject.cpp:282-290, :8282-8285). Our frozen bodies already hold their pose (src/game/play_show.cpp:1714-1725).

## 2.5. Sound
- `SOUND_SUMMON_SKILL_SLEEP` = `Data/Sound/SE_Ch_summoner_skill03_sleep.wav` (MM ZzzOpenData.cpp:4915; enum
  MM Audio/DSPlaySound.h:772): **1.87 s**, 44.1 kHz mono, played at the cast (WSclient.cpp:4839). No landing sound.

## 2.6. Timeline (frame 0 = the cast)
| frame | s | what |
|---|---|---|
| 0 | 0 | the pose; violet glow at her left hand (sprites, cloud) for the pose; sleep.wav (1.87 s) |
| 0 (server) | 0 | WebZen sleeps the target at once (the attack is the packet) |
| 14 | 0.56 | the violet circle opens under the caster (20 frames, to 3 tiles); the two rings appear at the target's middle |
| 14-28 | 0.56-1.1 | rings fade in to 0.7, turning +-8 deg/frame and growing; two big violet flares, two shinies, 3 streaks a frame into them |
| 28-49 | 1.1-1.96 | rings fade out, still growing (~1.3 m radius) |
| 14-... | | violet puffs off the body every other frame; the body frozen in its pose; until the time runs out or a blow lands |

## 2.7. Assets (under D)
| What | File | Exists | In our content |
|---|---|---|---|
| rings | Effect/elshildring.bmd, elshildring2.bmd (80 tris each) | yes | no |
| ring texture | Effect/macardkmono.OZJ (also Skill/macardkmono.OZJ) | yes | no |
| ground circle | Effect/Magic_Ground2.OZJ | yes | yes, content/effects/levelup/Magic_Ground2.OZJ (magic_ground.png beside it) |
| flare | Effect/flare01.OZJ | yes | yes, content/effects/light/flare01.png |
| shiny | Effect/shiny05.OZJ | yes | yes, content/effects/weakness/shiny05.png |
| sparkle | Effect/Shiny02.OZJ | yes | yes, content/effects/summon/shiny02.png |
| streaks | Effect/JointEnergy01.OZJ | yes | yes, content/effects/summon/joint_energy01.png |
| sleep puffs | Effect/water.OZJ | yes | probably, content/effects/kanturu/water.png (the Twin Tale's; check it is the same pixels) |
| hand (not drawn) | Effect/pin_lights, cra_04, clud64 | yes | yes (weakness/, drain/, kanturu/) |
| sound | Sound/SE_Ch_summoner_skill03_sleep.wav (1.87 s) | yes | no |
New to us: two 80-triangle models, one texture and one sound.

# 3. Footage

YouTube "Mu Online Summoner Skill Sleep" (id 33tg0SYiWb8, 1280x720, 29.97 fps, 20 s; the same channel as Requiem's
clip): a Summoner casting Sleep again and again at a Red Dragon on a dark rune floor, a cast about every 2 s. Still
sheet `sleep-footage.jpg`, cropped to the two of them, times from the clip's start and from the cast at ~4.7 s.

| still | cast + | what shows |
|---|---|---|
| 4.6 s | -0.1 | before the cast: the Summoner idle, the dragon's leg and belly plain orange |
| 4.8 s | +0.1 | she raises her arm; a **violet glow round her left hand and a violet haze about her** (the hand sprites and cloud) |
| 4.9 s | +0.2 | the hand glow brighter; a violet glow on the floor at her feet |
| 5.0 s | +0.3 | **a violet-pink band appears round the dragon's thigh**, about a metre up (the rings at +100 units); white spark at its centre |
| 5.1 s | +0.4 | the band brighter, wrapping the leg; her hand still glowing, a violet puff before her |
| 5.3 s | +0.6 | the band at its widest and brightest, mottled violet-pink, ~2 m across, its lower edge a bright arc |
| 5.5 s | +0.8 | the band still there, turning; she lowers her arm |
| 5.7 s | +1.0 | the band fading at its edges; a thin blue-green line crosses the frame (another of her spells or a pet's, not Sleep) |
| 5.9 s | +1.2 | the band dimmer, violet; her next cast's hand glow begins |
| 6.1 s | +1.4 | only a violet tint left on the dragon's leg |
| 6.3 s | +1.6 | a last faint violet arc |
| 6.5 s | +1.8 | gone; the dragon plain again |

Reading: the footage agrees with 2.2-2.3 -- a violet hand glow at the cast, then a violet ring band round the target's
middle that grows and turns for ~1.3 s and fades (MU's 34 frames). The ring shows up ~0.3 s after the hand rather than
MU's 0.56 s (a faster cast pose in this clip; not worth matching, as the rune has no cast pose anyway). **The dragon never stops**: it keeps its idle and no violet puffs rise
from it, so in this clip every Sleep failed or the dragon was immune -- the ring is drawn on the let-go whether or not
it took (the effect is laid by the cast, 2.2, not by the debuff). The sleeping body's puffs (2.4) are not in the clip.
A second clip (fWXdPaXA0wA, "Best Summoner with Skill Sleep", a later season's PvP) shows the **violet rune circle on
the ground** opening to ~1.5 m in 0.4 s (at 31.0-31.4 s), in magenta; too busy and too late a client to add more.

# 4. Fit to our code (main, 2026-10-25) and the budget

## 4.1. What our sim has for crowd control
- **Frozen** (`Body::frozenUntil`, src/sim/realm.h:659-660): Frost Arrow / Chill runes (src/sim/realm_fight.cpp:540-560,
  kFrostTicks 40, kChillTicks 20) and Ice Storm (realm_fight.cpp:770-795, kIceStormTicks 30). A frozen body takes no
  step and no turn (`Realm::advance`, src/sim/realm_move.cpp:129) and no thought or swing (`Realm::think`,
  realm_move.cpp:503); it is `halt`ed where it stands; drawn holding its pose (src/game/play_show.cpp:1714-1725) and iced
  (play_show.cpp:2153-2158); shown on the target frame as a bane (src/game/ui/vitals.cpp:398-415, `kBanes` 6,
  src/game/ui/vitals.h:123). **Sleep is exactly frozen plus "a landed blow ends it"** -- and frozen is not ended by blows.
- **Chilled** (`chilledUntil`, `chillSlow`, realm.h:657-658, :535): a slow, not a hold.
- **No stun** exists in the sim (no field or rule named so).
- **Who shrugs holds**: the raid's Golden Dragon (`Realm::shrugs`, src/sim/realm_raid.cpp:387-394, asked before every
  freeze, chill, push and pull: realm_fight.cpp:552, :789, src/sim/realm_skills.cpp:862, :1046, :1089); the Statue of
  Saint (`fixed`, realm.h:2441); the **Elites** clear chill, freeze and push every tick (`Realm::shrugControl`,
  src/sim/realm_elite.cpp:130-140, called from src/sim/realm.cpp:1399). A sleep field must join all three.
- **Name clash to avoid**: `Temper::Asleep` already means a monster with nobody near, not thinking (realm.h:61-62), and
  `Body::wakesAt` a risen monster's five standing seconds (realm.h:459, realm_move.cpp:504); strikeAt's miss branch
  already "wakes" a Temper::Asleep beast (realm_fight.cpp:148-159). The new state wants its own word -- `sleepUntil`
  (or `dozeUntil`) -- and the code should say "Sleep's" wherever it reads it.
- **Damage over time** (poison, burn, the monster bleed: `poisonPulse` realm_fight.cpp:1938, `burn` :2185, `bleedPulse`
  src/sim/realm_burn.cpp:114-138) does not go through `strikeAt`, so a wake-up placed in strikeAt's landed branch
  matches WebZen and OpenMU (DoTs never wake). Every rune blow does go through it (`runeStrike`, realm_fight.cpp:1138-1150),
  so his own Pollution, Requiem's held blows and Ice Storm wake a sleeper -- MU's rule.

## 4.2. The rune
- **Power**: `Power::Sleep = 79` after `Requiem = 78` (src/sim/items.h:836-844); the row in `powerOf`'s table after
  Requiem's (src/sim/items.cpp:726) as `kWizardOnly, kInWeapon, Rarity::Ancient`, and "Doesn't stack" (items.cpp:754).
- **The skill number**: `sim::skill::kSleep = 219` beside kRequiem (src/sim/skills.h:166), MU's number, never a row.
- **Constants** after Requiem's in items.h (:1323-1328), with `sleepOdds(energy) = 20 + energy/30 + 0/6` and
  `sleepSeconds(energy, level) = 5 + energy/100 - level/20` as Weakness's `weaknessOdds/Seconds` are written.
- **The roll**: a `Power::Sleep` branch in `Realm::callDown` (realm_fight.cpp:540) like the others, reached once a cast
  or landed swing from `Realm::land` (:1305) and once a channel from `channelRunes` (:485).
- **The state**: `Body::sleepUntil` (int64, 0 for never) beside frozenUntil; `advance` and `think` return while it runs
  (one line each, beside realm_move.cpp:129 and :503); laid with `halt`; cleared in strikeAt's landed branch on the
  target, in `shrugControl`, at death and rising (where frozenUntil is reset: realm_fight.cpp:2345, :2689). Snapshot
  shape 24 (src/sim/realm_snapshot.cpp:20). A rules and snapshot change: client and servers go out together.
- **Said**: `What::Loosed`, skill 219, `rune = true`, `whom` the sleeper, x/y its place; nothing said when the roll
  fails (no miss text: the rune's 10% already hides it). The waking needs no message: the mirror sees sleepUntil drop.
- **The drawing**: a `sim::skill::kSleep` branch in play.cpp's Loosed switch beside Requiem's; a new
  `game/fx/sleep.{h,cpp}` on game/fx/weakness's pattern (ground circle, sprites) plus two baked rings through
  index.py's `_models(root, build, "sleep")` as Requiem's; the puffs off a sleeping body from play_ribbons as the
  Requiem bleed's mist (src/game/play_ribbons.cpp:150-160); the pose held by extending play_show.cpp:1719's frozen
  test to sleepUntil (not iced: the ice tint stays the freeze's); a seventh bane on the target frame.
- **Sound**: SE_Ch_summoner_skill03_sleep.wav into content/sounds with one sounds.json key (`spell_sleep`) through
  `mu edit`, cooked with `./mu make sounds`.
- **Tests**: a `testSleepRune` on testRequiemRune's pattern (tests/sim_test.cpp:12253): a level-150 Soul Master's
  hunt in Lorencia; never the monster he struck; a sleeper takes no step and no swing until its time or the first blow
  landed on it, and wakes on that blow; DoT pulses never wake it; the Golden Dragon, an Elite and the Statue never
  sleep; none below Aida in the drops.

## 4.3. Budget (the transparent pass, 16,384, src/gfx/effects.h:84-92)
| piece | sprites/triangles at its busiest |
|---|---|
| ground circle (one terrain quad, 20 frames) | 1-2 |
| two rings, 34-35 frames | 160 |
| flares 2 + shinies 2, every frame | 4 |
| streaks: 3 born a frame x 10 frames, 2 tails, each with a sparkle | ~60 + 30 |
| **one let-go** | **~255** |
| each sleeping body, for its sleep: puffs every other frame, ~10 visible (MU keeps 30-35 alive, the rest at alpha 0) | 10-35 |
The cheapest Ancient by far: Requiem is ~3,750 at its +7 tier, Pollution ~2,500. Ten sleepers at once cost ~350.

# 5. Proposed rune and what is decided (from MU's numbers)

1. **Power 79 `Power::Sleep`, "Sleep"**, the Soul Master's (kWizardOnly, second class), one to a thing, in his weapon,
   Rarity::Ancient, beside the other five; drawn among the Ancients' share as they are.
2. **Chance 10%** (`kSleepChance = 0.10`) on a cast or a landed swing, once a channel: the others', ours, to tune.
3. **Who -- never what he struck.** MU's Sleep holds one target and the first blow on it wakes it
   (WZ/ObjAttack.cpp:2570-2575); laid on the monster he is hitting, his very next blow (or the next tick of a channel)
   would end it, so the rune would do nothing. So it puts to sleep **the nearest other monster within
   kStormcallReach (4 tiles) that he can see, preferring one whose quarry is him**, awake, not fixed, not the Golden
   Dragon, not an Elite -- the one about to hit him that he is not fighting. If there is none, the roll does nothing.
   **One** sleeper a roll, MU's count (`kSleepMost = 1`, see open call 1).
4. **Whether it takes**: MU's monster roll `20 + energy/30` % (`curse/6` is 0, he has no book, as Weakness), capped at
   **70%** (S17's SleepLimit) -- rolled after the 10%, as Weakness rolls its own odds after its 10%. No hit roll
   (OpenMU's and S17's; WebZen's MissCheck was a spell's attack-rate roll, which our runes never take).
5. **How long**: MU's `5 + energy/100 - level/20` s (WebZen's and OpenMU's, the target's level subtracted), whole
   seconds as MU's ints, **at most 20 s** (OpenMU's cap; WebZen has none, S17's 70 is a server setting); under 1 s it
   does not take. A level-150 Soul Master with ~750 energy: 45% for 9 s on a level-60 monster, 5 s on a level-140 one;
   with 2,000 energy: 70% (86 capped) for 20 s (23 capped) at level 40, 18 s at level 140.
6. **What sleeping is**: our frozen hold -- no step, no turn, no thought, no swing, halted, its pose held -- in its own
   field (`sleepUntil`). A second Sleep on a sleeper keeps the longer (WebZen's gObjCheckPowerfulEffect).
7. **Woken** by any blow that lands on it, anyone's (strikeAt's landed branch) -- not a miss, not poison, burn or bleed
   pulses (WebZen and OpenMU agree). It keeps its grudge, so it wakes into the fight.
8. **Immune**: the Golden Dragon (`shrugs`, saying Immune), the Statue of Saint (`fixed`), the Elites (shrugControl
   clears it). Monsters only, as every Ancient. No other boss exists in our sim yet; when Kanturu's bosses come, they
   shrug it as S17's bosses resist it whole (stun resistance 100).
9. **The tooltip**: "10% on a cast or a hit: puts the nearest other monster within 4 tiles to sleep for up to 20 s,
   by your energy; any hit wakes it".
10. **The look, MU's**: at the roll, the violet Magic_Ground2 circle opening under him (20 frames, to 3 tiles,
    (0.7, 0.3, 0.8)) and sleep.wav; on the sleeper at once (MU's 14-frame lead is the Summoner's cast pose, which he
    has not), elshildring and elshildring2 a metre up, (0.8, 0.3, 0.9), additive, 34/35 frames, alpha and light
    +0.05 then -0.05, turning +-8 degrees and growing 0.035 a frame from 0.1/0.15, with the two flare01 glows at 5,
    the two shiny05 at 2 and 1, three JointEnergy01 streaks a frame homing in from 2 m with Shiny02 sparkles; while it
    sleeps, MU's violet water.jpg puffs off a random bone every other frame, and its pose held. The hand glow is not
    drawn (Weakness's precedent).
11. **Said**: `What::Loosed`, skill 219, `whom` the sleeper, x/y its place, `rune = true`.

# 6. Open calls for the user

1. **One sleeper or a few.** MU's Sleep is one target; "on our side" as crowd control might want **2-3** of the
   monsters round him (the nearest awake ones that are not his target). One is MU's and keeps the rune modest; three
   makes it the Soul Master's escape button in a pack.
2. **How long at most.** MU's formula with OpenMU's 20 s cap (proposed) gives a high-energy Soul Master 18-20 s on
   Aida's monsters -- long, but any hit ends it and it never takes his target. A tighter cap (8-10 s) keeps a
   forgotten sleeper from standing out of the fight for most of a pull.
3. **Its own odds or always.** Proposed: MU's `20 + energy/30` (max 70%) after the 10%, so a proc lands about half
   the time at mid energy, as Weakness's curse does. The other call is that the 10% alone decides (always sleeps).
4. **The target frame's bane icon.** Our banes are drawn sheets; MU's buff icon 72 (the S17 row's `buff_icon`) would
   need finding in the client's buff sheet, or a sleep glyph of ours.
