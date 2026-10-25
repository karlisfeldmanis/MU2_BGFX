# Strike of Destruction (skill 232, the Blade Knight's Season 4 skill) as an Ancient rune -- research dossier

Researched 2026-10-25 before any build, for the Dark Knight's one Ancient rune (one Ancient a class, decided
2026-10-25), on his weapon. The Soul Master's is Requiem (78, b5895e24). Shaped as requiem.md: section 1 the server,
2 the client, 3 the footage, 4 the fit to our code and the budget, 5 the proposed rune and what is decided, 6 the open
calls. Sources: WebZen GameServer 1.00.93 (github ptr0x-real/Mu-GS-Webzen-MC-10093, shallow-cloned into this
session's scratchpad, cp949, read through iconv with line numbers unchanged), the S17 server
(`third_party/MuSource_S17`), OpenMU `LEGACY/reference/openmu`, MuMain `LEGACY/reference/MuMain`, Main 1.03.35
(`third_party/Main 1.03.35`, comments read as cp949), muextract on MuMain's Data, and YouTube footage.

**In one paragraph.** Strike of Destruction (Korean 파괴의 일격, "blow of destruction"; the client calls it
BLOW_OF_DESTRUCTION) is the Blade Knight's Season 4 skill, learned from the Crystal of Destruction orb. The knight
**leaps and drives his sword into the ground**; a moment later the earth **bursts at the point he aimed at, up to 5
tiles off**: a wide **blue-white flare on the floor**, a crown of **sharp violet-blue blades of light** shooting up,
**glowing blue cracks** running out across the ground with light-columns sinking back into them, a **line of
smaller cracks from his sword to the target**, water-like blue spray and white smoke, and a few hot rock chips
thrown out; the camera shakes for 10 frames. The server strikes **every monster in a 7x7 square round the point
(the first 4 surely, the 5th to 8th each on a coin flip, never more than 8), 500 ms after the cast**, with ice-element
knight-skill damage (**110**), and lays **Cold** on each: **10 s of slowed walking and slowed actions** (MuMain draws
them walking at a third of their speed, tinted blue). The burst lasts about a second; the whole effect 1.6 s.

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93; cp949)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/`
- `S17/` = `MU2_BGFX/third_party/MuSource_S17/` (`GameServer/`, `_MuServer/Databases/`)
- `OM/` = `LEGACY/reference/openmu/src/`

## 1.1. Name and version
- WebZen enum: `AT_SKILL_DESTRUCTION = 232`, "파괴의 일격", under `ADD_SEASON4_NEW_SKILL_20080522` beside Expansion of
  Wizardry 233, Recovery 234, Multi-Shot 235, Flame Strike 236, Gigantic Storm 237 (WZC/include/public.h:342-349): the
  **Season 4** second-class skill set. The define is on (WZ/define2.h:943), as are the two later fixes it reads,
  `MODIFY_SEASON4_NEWSKILL_20080909` (define2.h:764) and `MODIFY_MAGUMSA_SKILL_BUGFIX_20081015` (define2.h:695).
- **WebZen's data has no row for it**: every WZD/lang/*/skill.txt ends at 202 (main/skill.txt is 78 lines). So the
  numbers come from S17 and OpenMU, the rules from WebZen's code, as for Requiem.
- It is an **orb skill**: item 12/44 gives 232 (WZ/MagicInf.cpp:202); no energy needed (WZ/MagicDamage.cpp:786).
  OpenMU's orb: `CreateOrb(44, StrikeofDestruction, 1, "Crystal of Destruction", drop level 100, level 220, ...,
  darkKnightClassLevel 2)` -- the **Blade Knight** (second class) only (OM/Persistence/Initialization/VersionSeasonSix/Items/Orbs.cs:50, :65).
- S17: `SKILL_STRIKE_OF_DESTRUCTION = 232` (S17/GameServer/SkillDef.h:135). OpenMU: `SkillNumber.StrikeofDestruction = 232`
  (OM/Persistence/Initialization/Skills/SkillNumber.cs:110). MuMain: `AT_SKILL_STRIKE_OF_DESTRUCTION = 232` and its
  master strengthening `_STR = 337` (MM Core/Globals/_enum.h:437, :485, :691).
- A **combo skill**: WebZen puts it in the knight's combo second stage beside Twisting Slash, Rageful Blow and Death
  Stab (WZ/ComboAttack.cpp:46-53, "파괴의 일겨 콤보 2단계에 추가"); OpenMU as combo step 2 (not 3)
  (OM/.../VersionSeasonSix/SkillsInitializer.cs:589); S17 the same position 1 (S17/GameServer/PlayerSkillUse.cpp:825-835).

## 1.2. Skill row
- S17 `skill_template` 232 (S17/_MuServer/Databases/game.sql:45687; the same in mu_online_game.sql:50536), columns per
  the table at game.sql:45543-45590:
  `level 100, damage 110, mana 30, stamina(AG) 24, distance 5, effect_radio 4, delay 0, type 0, attribute 0,
  buff_icon 86, base 232, allow_dk 2, animation 1, elemental_attribute 2, elemental_bonus 135, attack_delay 2000,
  pvm 100, pvp 100, stat_1 1 / stat_value_1 150`.
  - attribute 0 = **ICE** (`Element::{ICE, POISON, LIGHTNING, FIRE, EARTH, WIND, WATER}`, S17/GameServer/CommonDef.h:238-250).
  - buff_icon 86 = `BUFF_COLD` (S17/GameServer/BuffDef.h:66); buff_template 86 is "Cold", "Detrimental cold reduces
    the mobility speed." (game.sql:129).
  - allow_dk 2 = the second class (Blade Knight); stat_1 1 = strength (S17/GameServer/SkillMgr.cpp:119-124).
  - Master additions 337/807/809/1202 raise it (S17/GameServer/PlayerSkillUse.cpp:979-984; game.sql:45768, 46183-46184,
    46350); 807 adds a second hit (S17/GameServer/SkillHandler.cpp:1971-1974). Not ours.
- OpenMU: `CreateSkill(StrikeofDestruction, "Strike of Destruction", BladeKnightAndBladeMaster, DamageType.Physical,
  damage 110, distance 5, AG 24, mana 30, levelRequirement 100, elementalModifier Ice, AreaSkillAutomaticHits)`
  (OM/.../VersionSeasonSix/SkillsInitializer.cs:228; parameter order OM/.../Skills/SkillsInitializerBase.cs:51-71).
  The same 110 and 5 as S17.
- For scale: MU's knight skills are Twisting Slash 25, Rageful Blow 60, Death Stab 70 damage; SoD's **110** is the
  largest of the knight's, and 110/80 of Ice Storm's 80.

## 1.3. The cast and its targets
- **WebZen** (the live code): the cast goes to `SkillDestruction` (WZ/ObjUseSkill.cpp:947-950), which is one line --
  `SkillAreaMonsterAttack(aIndex, lpMagic, x, y, aTargetIndex, 3, TRUE, 500)` (WZ/ObjUseSkill.cpp:7157-7165). That
  function (WZ/ObjUseSkill.cpp:6990-7150):
  - checks the point is within the skill's distance of the caster (:7022);
  - walks the caster's viewport: monsters (not summoned), the forced target and a duel / guild-war enemy (:7028-7076);
  - takes those in the **7x7 square x-3..x+3, y-3..y+3** round the point (aDistance 3, :7078-7080);
  - **the first 4 always, the 5th to 8th each on `rand()%2`, none past the 8th** (nMinCount 4, nMaxCount 8, :7007-7008,
    :7088-7107);
  - and **delays each blow 500 ms** (`gObjAddAttackProcMsgSendDelay(..., MSG_ATTACKPROC_ENTIREMAGIC, tObjNum, 500, skill)`, :7122).
- **S17**: `SkillAttackStrikeOfDestruction` (S17/GameServer/SkillHandler.cpp:1964-2011): the point is the target's tile
  for a creature caster (:124-148); every attackable in effect_radio **4** of the point (`CheckSkillRadio`, :1987,
  :6210+); `allow_attack = (hit_count <= 4) ? 1 : hit_count > 8 ? 0 : RANDOM(2)` (:1992) -- the same 4-sure, 8-most
  rule; each gets `AddDelayed(DELAYED_ATTACK, 500, ...)` (:1998) -- the same **500 ms**.
- **OpenMU**: no AreaSkillSettings for 232, so every attackable within the skill's range **5** of the client's target
  point (OM/GameLogic/PlayerActions/Skills/AreaSkillAttackAction.cs:129-131, :206-218), monsters only unless area
  skills hit players (:150-152); no cap, hit at once.

## 1.4. Damage (WebZen)
- 232 takes the **knight-skill branch** of the damage (WZ/ObjAttack.cpp:753, listed with Multi-Shot, Flame Strike
  and Chaotic Diseier): weapon damage plus the skill's 110 rolled `dmg .. dmg + dmg/2` (WZ/MagicInf.cpp:236-238).
- In the per-skill switch (WZ/ObjAttack.cpp:1799-1807):
  ```
  case AT_SKILL_DESTRUCTION:
      lpTargetObj->lpAttackObj = lpObj;
      lpTargetObj->DelayActionTime = 1000;
      lpTargetObj->DelayLevel = 1;
      gObjAddBuffEffect( lpTargetObj, BUFFTYPE_COLD, EFFECTTYPE_REDUCE_MOVE_SPEED, 0, 0, 0, 10 );
      AttackDamage = AttackDamage*(200+Energy/10)/100;
  ```
  The multiplier `(200 + energy/10)%` is **every knight skill's** (Falling Slash ... Rageful Blow, WZ/ObjAttack.cpp:1384-1407),
  energy being the caster's (`Energy = lpObj->Energy + lpObj->AddEnergy`, :539). Nothing special to SoD beyond its 110.
- **Every** body it lands on alive is made Cold -- **no resistance roll**, unlike the wizard's Ice which rolls
  `retResistance(RESISTANCE_COLD)` (WZ/ObjBaseAttack.cpp:752-758).

## 1.5. Cold (WebZen)
- `BUFFTYPE_COLD` = 86, "추위" cold (WZ/BUFFTYPEDEF.h:121-122), **10 s**, effect `EFFECTTYPE_REDUCE_MOVE_SPEED`.
- What it does on the server is the two fields set beside it:
  - `DelayLevel = 1`: each tile a monster steps takes **400 + 300 ms** (`DelayTime = 300`, WZ/user.cpp:908-925; the
    monster AI the same, WZ/TMonsterAI.cpp:340-352) -- it walks at **4/7 (0.57) of its pace**.
  - `DelayActionTime = 1000`: each action waits **1000 ms more** (`NextActionTime + DelayActionTime`, WZ/gObjMonster.cpp:1521)
    -- the wizard's Ice sets 800 there (ObjBaseAttack.cpp:756), our kChillSwingTicks 16.
- A quirk: the buff's removal clears `DelayLevel` / `DelayActionTime` only for BUFFTYPE_FREEZE
  (WZ/BuffEffectSlot.cpp:250-258); I found no clear for Cold, so in 1.00.93 the slow may outlast the 10 s until
  something else clears it. Taken as 10 s here.
- A re-hit: gObjAddBuffEffect replaces an effect of the same category (requiem.md section 1, WZ/BuffEffectSlot.cpp:84-95):
  a second SoD restarts the 10 s.

## 1.6. S17
- Cold is laid in `Unit::TargetResistance` (S17/GameServer/Unit.cpp:2154-2177 opens it): after an **ice
  resistance roll** (`ResistElement`, :2364-2392 -- resisted unless `Random(resistance) == 0`), and **not again while
  it holds** (`if (HasBuff(BUFF_COLD)) return false; AddBuff(BUFF_COLD, 10, ...)`, :2240-2250), except from the master
  forms 809/1202. Called once a skill use (`count <= 1`, :2986-2989).
- Cold is a "slowdown buff" (`HasSlowdownBuff`, :1679-1686): a monster's tile step **+300 ms** (S17/GameServer/MonsterMovement.cpp:44, :66-71),
  as WebZen. No action delay in S17.

## 1.7. OpenMU (S6)
- `StrikeofDestruction` gets `MagicEffectNumber.Cold`, `IsIced`, **10 s**, and **skips the ice resistance roll**
  (`SkipElementalModifier = true`) (OM/Persistence/Initialization/Skills/SkillsInitializerBase.cs:195-200; re-made by
  OM/.../Updates/FinishDarkKnightMasterTreePlugInSeason6.cs:130-142).
- Cold multiplies movement speed by **0.33** (`ColdMovementSpeedFactor`, OM/Persistence/Initialization/MovementSpeedConstants.cs:55;
  laid by OM/.../Updates/AddMovementSpeedAttributesPlugInBase.cs:138-153). Iced is 0.5 (:50).
- No `(200 + energy/10)%` here; OpenMU's knight skill damage is its own formula.

## 1.8. Differences
1. Targets: WebZen a 7x7 square, 4 sure and up to 8; S17 radius 4, the same 4/8 rule; OpenMU every one within 5.
2. Timing: WebZen and S17 hold every blow **500 ms**; OpenMU strikes at once.
3. Cold: WebZen always, 10 s, refreshed by a re-hit, walk 0.57 and +1000 ms an action; S17 an ice resistance roll,
   not renewed while it holds, walk 0.57; OpenMU always, 10 s, walk 0.33.
4. Same in all: 110 damage, ice, distance 5, the Blade Knight's.

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data, **103** = Main 1.03.35's
`MU/[11.07.12] MU_Chs_1.03.35/source`. Ticks are 25 a second (FPS_ANIMATION_FACTOR 1).

## 2.1. The cast
- `AttackCharacter`'s knight arm (MM GameLogic/Combat/ClassAttack.cpp:995-1018): turns him to the target point,
  checks the tile within range (`CheckTile`), sends 0x1E MagicContinue (tile, angle, target key) and plays
  `PLAYER_SKILL_BLOW_OF_DESTRUCTION` at once.
- The effect waits for the server's echo: `ReceiveMagicContinue` plays the pose on others (MM Network/Server/WSclient.cpp:5196-5198)
  and for every caster sets `AttackTime = 1`, `SkillX/Y` = the tile (WSclient.cpp:5481-5485). `AttackStage` jumps
  AttackTime straight to 15 for 232 (MM Engine/Object/ZzzCharacter.cpp:3015-3019), so on the next `MoveCharacter`
  it passes `g_iLimitAttackTime` 15 (ZzzCharacter.cpp:172, :4133-4146) and creates the effect: the tile's centre into
  `o->Light`, `CreateEffect(MODEL_BLOW_OF_DESTRUCTION, his position, his angle, ..., subtype 0, him)` and
  `PlayBuffer(SOUND_SKILL_BLOWOFDESTRUCTION)` (ZzzCharacter.cpp:4170-4180). **Frame 0 = the echo.**
- The pose: action 176 of Player/player.bmd (enum MM _enum.h:3089; index checked by muextract), **11 keys** at PlaySpeed
  **0.3** (ZzzCharacter.cpp:1004): 33 frames, 1.33 s. muextract's root height runs 89 → 64, 62 (a crouch, keys 1-2)
  → **181, 203 (a leap, keys 3-4)** → 47, 36 (the slam, keys 5-6) → back to 89: he jumps about a metre and drives the
  sword down. Key 5, the slam, is frame ~17 -- the same frame the ground bursts (2.3).
- His weapon trail: from key 2 to 8 the sword draws the motion blur `BlurType 1, BlurMapping 2` (ZzzCharacter.cpp:3817-3821).

## 2.2. The controller (MODEL_BLOW_OF_DESTRUCTION, no model)
`MODEL_BLOW_OF_DESTRUCTION` has no .bmd (none loaded in ZzzOpenData.cpp); it is an invisible controller in two parts.
Create MM Render/Effects/ZzzEffect.cpp:4771-4808 (103 ZzzEffect.cpp:5586-5615, "부모오프젝트가 바라보는 방향으로 조금
이동" -- moved a little the way the parent faces; "타켓에 이펙트 발생" -- an effect at the target):
- **Subtype 0, the sword's impact**: LifeTime **40**; put at `(-20, -100, 0)` turned by his angle -- **100 units in
  front of him, 20 to the side** (MU's models face -Y); the target point kept in StartPosition; Light (1.2, 1.2, 1.2);
  it creates subtype 1 at the target point (:4773-4786).
- **Subtype 1, the target's burst**: LifeTime **40**, Light (1.2, 1.2, 1.2), `Position[2] = 150` (absolute, as written;
  only its sprite and splash read the height), Scale 5 (:4787-4793).
- Nothing of either is drawn or moved until **LifeTime <= 24, frame 16** (0.64 s): the first 16 frames wait on the
  leap (MoveHandlers below; render ZzzEffect.cpp:9214-9238, :10058-10074).
- Move: MM Render/Effects/Behaviors/MoveHandlers.cpp:7530-7668 (103 ZzzEffect.cpp:16650-16760).

## 2.3. The impact, before him (subtype 0)
From frame 16 (life 24) to 40, every frame:
- **A camera shake**: `EarthQuake = (rand()%8-4)*0.1` while life 24..15, 0 after -- **frames 16-25** (MoveHandlers.cpp:7539-7547).
- **A white flare**: `CreateSprite(BITMAP_SWORD_EFFECT_MONO, +65 up, scale 3.0-3.1, (0.5, 0.5, 1.0))` (ZzzEffect.cpp:9216-9225):
  D/Effect/Swordeff_mono.OZJ, 64x64, a white upward spray of light (ZzzOpenData.cpp:5214), additive, tinted pale blue.
- **A blue floor flare**: `RenderTerrainAlphaBitmap(BITMAP_FLARE_BLUE, 4 x 4 terrain cells)` (ZzzEffect.cpp:10058-10065):
  D/Effect/flareBlue.OZJ, 64x64, a blue star (ZzzOpenData.cpp:5318), laid on the ground 4 tiles across (a terrain
  decal's size is in cells, MM Render/Terrain/ZzzLodTerrain.cpp:2402-2428), turned by the angle.
- Its Light decays **/1.05 a frame** from frame 16 (MoveHandlers.cpp:7578-7580), so both flares fade to ~0.4 by frame 40.
At **frame 17** (life 23), once (MoveHandlers.cpp:7549-7576; 103 ZzzEffect.cpp:16666-16693):
- **Two MODEL_NIGHTWATER_01** in (0.3, 0.3, 1.0), "날카로운 빛 모양 이펙트" -- the sharp light-shaped effect (103 :16669-16671), see 2.5.
- **One MODEL_KNIGHT_PLANCRACK_A** at scale 1.2, see 2.6.
- **The line of cracks to the target**: `iNum = length/100 + 1` MODEL_KNIGHT_PLANCRACK_B, **one every 55 units** from
  the impact toward the target, each turned his way ± 10-29° alternately, scale 1.0 (:7560-7575). So a 5-tile throw
  lays **6** pieces covering the first 3.3 m -- a crack running out from his sword about two thirds of the way.

## 2.4. The burst at the target (subtype 1)
From frame 16 (life 24):
- **A blue light**: `CreateSprite(BITMAP_LIGHT, +100 up, scale 5, Light x (0.5, 0.5, 1.0))` (ZzzEffect.cpp:9227-9237):
  D/Effect/flare01.jpg (ZzzOpenData.cpp:5195), a big soft blue glow over the point.
- **A wider blue floor flare**: `RenderTerrainAlphaBitmap(BITMAP_FLARE_BLUE, 6 x 6 cells)` (ZzzEffect.cpp:10066-10072) --
  6 tiles across, **the server's 7x7 square drawn**.
- Light /1.05 a frame (MoveHandlers.cpp:7637-7639).
- **The splash, frames 16-25** (life 24..15): **15 a frame** at random within ±150 units (all three axes) of the point
  (MoveHandlers.cpp:7585-7604):
  - a `BITMAP_WATERFALL_5` or `_3` (coin flip) subtype 8, (0.5, 0.5, 1.0), scale 1.6-2.5: WATERFALL5 (D/Effect/waterFall5.OZJ,
    grey speckle spray) / WATERFALL3 (waterFall3.OZJ, white droplets) (ZzzOpenData.cpp:5332-5334). Life 30, risen 1-9
    units a frame and falling 0.6 a frame², growing 0.05 a frame, its light /1.1 a frame
    (MM Render/Effects/ZzzEffectParticle.cpp:3474-3479, :3663-3668 create; :8435-8444, :8595-8604 move) -- blue
    droplets thrown up and dimming in about a second;
  - and on a coin flip a `BITMAP_SMOKE` subtype 55, white, life 30, rising and growing, light /1.08 a frame
    (ZzzEffectParticle.cpp:1538-1544, :5734-5743).
  About 150 droplets and 75 smoke puffs over the ten frames.
At **frame 17**, once (MoveHandlers.cpp:7606-7634; 103 ZzzEffect.cpp:16728-16752):
- **Two MODEL_NIGHTWATER_01** at scale **2** and **1**, (0.3, 0.3, 1.0).
- **MODEL_RAKLION_BOSS_CRACKEFFECT** at scale 0.2 (+1 = **1.2**), "바닥에 큰 갈라짐 이펙트" -- the big crack in the
  floor (103 :16738), see 2.6.
- **5-7 rock chips**: `MODEL_STONE1 + rand()%2` subtype 13, each up to 150 units out at a random heading (:7623-7633), see 2.7.

## 2.5. The blades of light (MODEL_NIGHTWATER_01)
- Model `D/Effect/nightwater01.bmd` (ZzzOpenData.cpp:4582-4583). muextract: **one bone, one key, 40 triangles**, one
  mesh on Swordeff_mono.jpg; 4.5 m across, 2.6 m tall (x -249..195, height -35..262, z -170..178): a fan of long
  thin blade-shapes splayed upward and outward -- a crown of light spikes.
- Create: LifeTime 25, Alpha 1, a random turn (ZzzEffect.cpp:4809-4815). Move: Alpha **-0.04 a frame** (MoveHandlers.cpp:7671-7677),
  so gone in 25 frames (1 s). Render: `RENDER_TEXTURE | RENDER_BRIGHT` (additive) in Light x Alpha (MM Engine/Object/ZzzObject.cpp:611-616):
  **deep blue-violet** (0.3, 0.3, 1.0) blades fading -- the violet rays in the footage.
- Four of them a cast: two at his sword (scale 1), two at the target (2 and 1).

## 2.6. The cracks (MODEL_KNIGHT_PLANCRACK_A / _B, MODEL_RAKLION_BOSS_CRACKEFFECT)
All three are the same family of model (ZzzOpenData.cpp:4566-4567, :4584-4589): a flat crack mark on the floor and
**light columns standing in the cracks that sink into the ground** -- their bones carry the columns from 131-135
units tall at key 0 to 6-10 at key 4. Render, all three: `RENDER_TEXTURE | RENDER_BRIGHT` in Light x Alpha; A and B
draw mesh 1 (the columns) with V scrolled by `-WorldTime%2000 * 0.0001` (ZzzObject.cpp:599-603, :617-628).

| model | where | file | tris (muextract) | what | life / fade |
|---|---|---|---|---|---|
| PLANCRACK_A | under his sword | knight_plancrack_a.bmd, 5 keys at PlaySpeed 0.3 (ZzzOpenData.cpp:4586) | **96** (2 `grandmark2` + 94 `lines2`) | a star crack 1.7 m across (D/Effect/grandmark2.OZJ 128x128, a white splash-star) with light columns 1.35 m tall sinking to 0.1 m over keys 0-4 (13 frames) | life 25 (or 20 subtype 1), alpha -0.04 a frame (ZzzEffect.cpp:4816-4833; MoveHandlers.cpp:7680-7695) |
| PLANCRACK_B | the line to the target | knight_plancrack_b.bmd, 5 keys at 0.3 (:4589) | **42** (2 `grandmark` + 40 `lines2`) | a short streak crack 0.8 m (D/Effect/grandmark.OZJ 128x64, a white jagged streak) with columns 0.7 m sinking | life 25, alpha -0.04, lifted 15, turned 90° (ZzzEffect.cpp:4834-4840; MoveHandlers.cpp:7697-7704) |
| RAKLION_BOSS_CRACKEFFECT | at the target | knight_plancrack_grand.bmd, 5 keys | **1,524** (2 `grandmark2` + 68 `grandmark` + 1,454 `lines2`) | the big one: the star at the centre and **five streak cracks running out 1.6 m** (x1.2 = **~1.9 m**, about the 2-tile radius), columns along all of them sinking | life 40, scale 1.2, lifted 30, random turn (ZzzEffect.cpp:1110-1117); alpha -0.03 a frame (MoveHandlers.cpp:1898-1906): gone in 33 frames |

`lines2` (D/Effect/lines2.OZJ, 32x64) is horizontal light streaks: scrolled up the columns it reads as light pouring
up out of the cracks. Tinted (0.3, 0.3, 1.0): **blue cracks**. Keys at 0.3 a frame: the columns are down by frame 30.

## 2.7. The rock chips (MODEL_STONE1/2 subtype 13)
- D/Skill/Stone01.bmd (**10 tris**) and Stone02.bmd (**38 tris**) on fire02.jpg -- a dark crust with orange veins
  (ZzzOpenData.cpp:4160-4162); our content/effects/meteor/Stone01/02 are the same models.
- Create: life 20-35, scale 0.24-1.2 (x the call's), gravity 3-5, a random heading, flung 6.4-19 units a frame outward
  and 15 up (ZzzEffect.cpp:2700-2716). Move: falls, tumbles by its life, on the ground skids at 0.6 a frame and fades
  0.1 a frame (ZzzEffect.cpp:7278-7300). Small hot chips hopping out of the burst.

## 2.8. Cold on the struck body (eDeBuff_BlowOfDestruction = 86)
- `eDeBuff_BlowOfDestruction = 86` (MM _enum.h:3980), the server's buff 86.
- On it, once: `CreateEffect(MODEL_ICE, body)` (`InsertBuffPhysicalEffect`, WSclient.cpp:15311-15314) -- the wizard's
  Ice crystal (D/Skill/Ice01.bmd, **256 tris, 6 keys**, ZzzOpenData.cpp:4157): life 50, scale 0.8, plays to key 5,
  then holds, smokes and fades 0.05 a frame, darkening the ground under it (ZzzEffect.cpp:2187-2195, :7637-7662). Our
  content/effects/ice/Ice01 is this model, already baked.
- While it holds: the body drawn **blue** (BodyLight (0.3, 0.5, 1.0)) with a second chrome-ice pass (blend mesh -2)
  (ZzzObject.cpp:1130-1137, :2626-2636), as Freeze is; equipment the same (ZzzObject.cpp:10252-10255, :10274-10278).
- **Walking at a third**: a character's move speed `*= 0.33` (`CharacterMoveSpeed`, ZzzCharacter.cpp:6284, :6357-6360)
  and a player's walk/run animations `*= 0.33` (ZzzCharacter.cpp:505-508, :525-528, :542-546) -- where Freeze is 0.5.
- Removed: nothing to undo (WSclient.cpp:15507-15510); the skill's own buff is cleared by the 232 entry in the
  buff-off handler (WSclient.cpp:3807-3810).

## 2.9. Sound
- `SOUND_SKILL_BLOWOFDESTRUCTION` = `Data/Sound/BLOW_OF_DESTRUCTION.wav` (**2.00 s**, 44.1 kHz mono, 178,576 bytes)
  at the effect's birth (ZzzOpenData.cpp:4925; enum MM Audio/DSPlaySound.h:808; played ZzzCharacter.cpp:4179). One voice.

## 2.10. Not ours
- The caster's leap (action 176) and its sword blur: a rune's proc plays no pose on him (Requiem did not play the
  Summoner's pose either).
- `MODEL_BLOW_OF_DESTRUCTION` subtype 2 (MM MonkSystem.cpp:1275, the Rage Fighter's) and
  `MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION` (a boss's copy, MoveHandlers.cpp:8960+): other users of the look.

## 2.11. MuMain vs 1.03.35
- The create, the frame-16/17 moves, the counts and colours are identical (103 ZzzEffect.cpp:5586-5615, :16650-16760;
  render :20682, :21666). MuMain only renamed `CreateEffect` to `CreateEffectFpsChecked` and `rand()%2` to `rand_fps_check(2)`.

## 2.12. Timeline (25 frames a second, frame 0 = the echo, when the effect is born)
| frame | s | what |
|---|---|---|
| 0 | 0 | BLOW_OF_DESTRUCTION.wav (2.0 s); the controller placed, nothing drawn. (He is already in his leap: the pose began at the send.) |
| 16 | 0.64 | the burst: blue floor flare 4 cells before him and 6 cells at the target, the white Swordeff_mono flare at his sword, the big blue flare01 glow over the point; camera shake starts; splash 15 a frame |
| 17 | 0.68 | the four violet blade-crowns, the crack star under his sword, the line of cracks toward the target, the big crack at the target, 5-7 rock chips |
| 16-25 | 0.64-1.0 | shake; ~150 blue droplets and ~75 smoke puffs |
| 17-30 | 0.68-1.2 | the light columns sink into the cracks (5 keys at 0.3) |
| 17-42 | 0.68-1.7 | the blade-crowns and small cracks fade (alpha -0.04) |
| 17-50 | 0.68-2.0 | the big crack fades (alpha -0.03) |
| 40 | 1.6 | the controller and its flares end (light down to ~0.4) |
| server +12.5 | +0.5 | WebZen/S17 strike every body chosen; Cold starts: an Ice crystal on each, blue tint, a third the walk for 10 s |

## 2.13. Assets (under D)
| What | File | Size | In our content |
|---|---|---|---|
| blade crowns | Effect/nightwater01.bmd | 1 key, 40 tris | no |
| crack, under the sword | Effect/knight_plancrack_a.bmd | 5 keys, 96 tris | no |
| crack, the line | Effect/knight_plancrack_b.bmd | 5 keys, 42 tris | no |
| crack, at the target | Effect/knight_plancrack_grand.bmd | 5 keys, 1,524 tris | no |
| crack star | Effect/grandmark2.OZJ | 128x128 | no |
| crack streak | Effect/grandmark.OZJ | 128x64 | no |
| light columns | Effect/lines2.OZJ | 32x64 | no |
| white flare / blade texture | Effect/Swordeff_mono.OZJ | 64x64 | no (effects/fire_scream/swordeffor2.png is another sheet) |
| blue floor flare | Effect/flareBlue.OZJ | 64x64 | **yes**, content/effects/defense/flare_blue.png (fx/aura, fx/recall use it) |
| blue glow | Effect/flare01.OZJ | -- | **yes**, content/effects/light/flare01.png |
| splash | Effect/waterFall3.OZJ, waterFall5.OZJ | 64x64 each | no (effects/aida/waterfall3.png not checked as the same) |
| smoke | BITMAP_SMOKE | -- | check the existing smoke sheet |
| rock chips | Skill/Stone01.bmd, Stone02.bmd, Skill/fire02.OZJ | 10 / 38 tris | **yes**, content/effects/meteor/Stone01/02, fire02.png |
| Cold's crystal | Skill/Ice01.bmd, Skill/ice.OZJ | 6 keys, 256 tris | **yes**, content/effects/ice/Ice01 (baked keys) |
| sound | Sound/BLOW_OF_DESTRUCTION.wav | 2.00 s | no |

# 3. Footage

YouTube "Mu Online Dark Knight Skill Crystal of Destruction" (id GRPu5yMtffU, 1280x720, 29.97 fps, 26.5 s; the same
series and dark floor as Requiem's clip): a Blade Knight casting SoD at a Dragon over and over, a cast about every
1.7 s. Still strip `strike-of-destruction-footage.jpg`, cropped to the fight, eight frames of the first cast at
0.5, 0.6, 0.7, 0.8 (top row) and 0.9, 1.0, 1.2, 1.6 s (bottom row) from the clip's start; the wind-up starts at ~0.3 s.

| still | what shows |
|---|---|
| 0.5 s | he crouches, sword low; a pale pink-white arc under the blade (his weapon blur) |
| 0.6 s | he springs up, sword overhead, a short gold trail |
| 0.7 s | at the top of the leap a **wide orange-gold crescent** over his head (the blade's blur); on the floor between him and the dragon a **white-cyan burst of light** spreading, a blue haze rising into the dragon |
| 0.8 s | landed in a crouch, sword driven down; the floor burst white-hot before him; a **violet-blue ray slanting up** through the dragon, thin blue streaks |
| 0.9 s | the burst at its widest, about 3 m of white-blue floor; **violet-white blades of light** spraying up at the dragon's feet; a thin vertical light line above it |
| 1.0 s | the spray at its tallest, blue-violet through the dragon's body; the floor glow still white |
| 1.2 s | dimming to a pale blue mist; damage numbers |
| 1.6 s | nearly gone, a faint blue on the floor; he stands |

The footage agrees with the trace in shape and colour: the burst is **blue-white on the floor, violet blades up**,
centred on the target (here the dragon stands right before him, so impact and target nearly overlap), gone in about a
second. Two things it adds: the burst comes about **0.4 s after the wind-up starts** -- quicker than MuMain's frame 16
from the echo, because the pose ran at his attack speed and the effect is timed from the echo, not the pose; and the
blue cracks themselves are hard to see under the flares at this distance -- what reads is the white floor flare and
the violet blades. The big orange crescent is his sword's blur, not the spell's. A second clip, "Skill: Strike of
destruction" (z8YRtKOolrc, 1010x720, 86 s), shows the same white-blue bursts with violet rays among a crowd in a
fiery map, mixed with a party's other spells; nothing it shows differs.

# 4. Fit to our code (main, 2026-10-25) and the budget

## 4.1. Where a knight's weapon rune rolls today
- **A plain swing that lands**: `Realm::strikeAt` calls `stormcall` for a swing with no skill row (or a Multi-Shot
  arrow) that pays (src/sim/realm_fight.cpp:476-484), and `stormcall` walks both hands' sockets and calls `callDown`
  for every weapon power he may set (realm_fight.cpp:515-534). The knight's Twister and Hellfire runes roll there
  (callDown, realm_fight.cpp:746-798). **An Ancient in his weapon already rolls here with no new code** -- Ice Storm
  rolls on a Soul Master's swing the same way.
- **A skill**: nothing. A knight skill's blows come through strikeAt with a row, so `stormcall` is never called
  (the `row == nullptr` test, realm_fight.cpp:482), and `Realm::land` calls `stormcall` only for a wizardry spell
  (realm_fight.cpp:1447-1450). The Twister rune does not roll on Twisting Slash today.
- **The hook for "and his skills"**: in `Realm::land` (realm_fight.cpp:1409-1451), after `release`, the knight's arm
  beside the wizard's:
  `else if (spell && !spell->wizardry && hero.player && hero.alive()) channelRunes(hero, aimed ? *aimed : hero);`
  `channelRunes` (realm_fight.cpp:537-555) already walks only the Ancients in his weapons and calls callDown once --
  so **once a skill use**, whether or not it struck, never once per body. A knight channel (realm_skills.cpp:685-690,
  `if (hero.player && row.wizardry)`) widens to his rows the same way. No double roll: a skill's blows never reach
  stormcall, and a plain swing (skill 0) never reaches the new arm.
- `runeAim` (realm_fight.cpp:1227-1240): what he struck, else the nearest he sees within kStormcallReach 4 -- right
  for a skill that struck nothing.

## 4.2. The rune
- **Power**: `Power::StrikeOfDestruction = 80`, after `Sleep = 79` (src/sim/items.h:869-881); the numbers must run
  unbroken (drawRunePower and the drop walk `powerOf(p)` until the first gap, src/sim/realm_items.cpp:2867, :2884).
  The row goes after Sleep's in `powerOf`'s table (src/sim/items.cpp:747-767) as `kKnightOnly, kInWeapon,
  Rarity::Ancient, true, true` (second class -- the Blade Knight's, as MU's orb asks -- and one to a thing), and
  into the "Doesn't stack" list (items.cpp:788-793). Client and servers go out together, as the others.
- **If the other DW Ancients go first** (Ice Storm 74, Weakness 75, Pollution 76, Drain Life 77, Sleep 79): with the
  numbers closed up, Requiem would be 74 and SoD 75; but a rune's power is a saved number, so closing up is a save
  migration, or the removed rows stay as rows that never drop and the numbers stand (SoD 80). Not decided here.
- **The skill number**: `sim::skill::kStrikeOfDestruction = 232` beside kRequiem and kSleep (src/sim/skills.h:199-208),
  MU's number, never a row, so kSkills 50 and the learned mask keep their layout (skills.h:654-664); 232 is free.
- **Constants** in items.h after Sleep's (items.h:1401-1420).
- **The roll**: a `Power::StrikeOfDestruction` branch in `Realm::callDown` (realm_fight.cpp:592) on Requiem's pattern
  (realm_fight.cpp:980-1010): runeAim, the victims chosen at the roll, said as `What::Loosed` skill 232 with
  `rune = true`, x/y the point -- and his heading in `a`'s spare field as Twister's (realm_fight.cpp:793), since the
  drawing lays the crack line from him to it.
- **The hold**: Requiem's held-blow list (`requiemBlows_`, 32, src/sim/realm.h:2308-2320, in the snapshot) is the
  shape; SoD wants the same at 10 ticks. Either a second list or one list with the power on each blow (a snapshot
  shape change either way).
- **Cold**: our iced chill is `Body::chilledUntil` (src/sim/realm.h:690-691), walking at `kChillFactor` 0.5
  (src/sim/skills.h:213; src/sim/realm_move.cpp:189) and swinging `kChillSwingTicks` 16 later (skills.h:220-222;
  realm_move.cpp:626-630), drawn as rime and the ice tint (src/game/play.cpp:3385-3392, play_show.cpp:2258-2264).
  MU's Cold is deeper (walk 0.57 on WebZen's server, 0.33 in MuMain and OpenMU; +1000 ms an action, 20 ticks) and
  longer (10 s, 200 ticks). Either a `coldUntil` beside it with its own factor and delay (and the move, swing and
  drawing tests read both), or SoD lays the existing chill for 200 ticks. Statue/Elite/raid dragon shrug it as Ice
  Storm's freeze does (`fixed` / `shrugs`, realm_fight.cpp:839-845).
- **The blow**: `runeStrike(hero, target, force)` (realm_fight.cpp:1242-1255) -- his swing with his energy's band --
  at `kSoDForce * runeForce(hero, Element::Ice)`, so his Glacier runes raise it as they raise Ice Storm's.
- **The drawing**: a `sim::skill::kStrikeOfDestruction` branch in play.cpp's Loosed switch beside Ice Storm's and
  Pollution's (src/game/play.cpp:1457-1511), a new `game/fx/destruction.{h,cpp}` on fx/requiem's pattern, wired in
  play.h, play_open.cpp, play_mode.cpp and CMakeLists.txt as Requiem was (b5895e24). Models through muextract
  (nightwater01, plancrack a/b/grand -- the three cracks' five keys baked whole), textures through decode_texture,
  pipeline/index.py's `_models(root, build, "destruction")` as `"requiem"` (pipeline/index.py:2154). The rock chips
  and Ice01 are already built (effects/meteor, effects/ice); flare_blue and flare01 too.
- **Sound**: BLOW_OF_DESTRUCTION.wav into content/sounds, one sounds.json key through `mu edit` as `spell_requiem`
  (content/sounds/sounds.json:3154-3156), cooked with `./mu make sounds`.
- **Tests**: a `testDestructionRune` in tests/sim_test.cpp beside testRequiemRune / testSleepRune
  (tests/sim_test.cpp:12558, :12651, :12966-12967): a Blade Knight hunting with plain swings and with Twisting Slash,
  rolls near 10% on each, one roll a skill use, never more than 8 struck, none past 3 tiles, blows 10 ticks after the
  Loosed, Cold on each struck for 200 ticks, and a first-class knight's rune never rolling.

## 4.3. Budget (the transparent pass, 16,384 since Tarkan, src/gfx/effects.h:91-96)
One SoD at its busiest (frames 17-25, everything alive):
| piece | count | sprites/tris |
|---|---|---|
| floor flares (terrain decals, ~(size+1)² cell quads) | 4 x 4 + 6 x 6 cells | ~25 + ~49 |
| Swordeff_mono + flare01 sprites | 2 | 2 |
| blade crowns (nightwater01) | 4 x 40 | 160 |
| crack under the sword | 96 | 96 |
| crack line (5 tiles out) | 6 x 42 | 252 |
| big crack at the target | 1,524 | 1,524 |
| rock chips | 5-7 x 10-38 | <=266 |
| splash droplets + smoke (15 a frame, life 30) | ~150 + ~75 | ~225 |
| **total** | | **~2,600** |
| Cold's crystal, each struck body (2 s), up to 8 | 256 each | up to 2,048 |

- Lighter than one Requiem at its +11 tier (~5,250) and about one Requiem +0..6 (~2,900); the big crack is 60% of it.
- With 10% on every landed swing and every skill use, and the look 1.6-2 s, two at once will be usual in a fast
  hunt: ~5,200, plus up to 16 crystals (~4,100) if all bodies are struck twice -- inside 16,384 beside a crowd's other
  spells. One at a time (as Requiem and Pollution draw) halves it.

# 5. Proposed rune and what is decided (from MU's numbers)

1. **Power 80 `Power::StrikeOfDestruction`, "Strike of Destruction"**, the Blade Knight's (kKnightOnly, second class --
   MU's orb asks the second class), one to a thing, in his weapon, Rarity::Ancient; drawn among the Ancients' share
   as the others. The Dark Knight's one Ancient. (75 if the five DW Ancients are removed and the numbers closed up first.)
2. **Chance 10%** (`kSoDChance = 0.10`): on a **plain swing that lands** (stormcall, as every weapon rune), and **once
   a skill use** (the new arm in Realm::land, and once a channel) -- never once per body a skill strikes.
3. **Where**: on what he struck; on a skill that struck none, the nearest he sees within kStormcallReach (`runeAim`).
4. **Who**: that one first, then the others round it within **3 tiles** (`kSoDTiles = 3.0f`, WebZen's 7x7 square, and
   the drawn 6-cell flare) in body order, **the first 4 sure, the 5th to 8th each on a coin flip** on the rune dice,
   **8 at most** (`kSoDSure = 4`, `kSoDMost = 8`) -- WebZen's and S17's count. Chosen at the roll.
5. **Force 1.65, ice** (`kSoDForce = 1.65f`): MU's 110 against Ice Storm's 80 at 1.2 (Requiem took 65/80 the same
   way), his rune's blow (`runeStrike`) with `runeForce(hero, Element::Ice)`. WebZen's `(200 + energy/10)%` is every
   knight skill's own multiplier, not SoD's; not carried.
6. **When**: **held 10 ticks** (`kSoDHoldTicks = 10`, WebZen's and S17's 500 ms); one that died meanwhile is skipped.
7. **Cold** (WebZen): every body the blow lands on, **no resistance roll**, for **10 s** (`kColdTicks = 200`): it walks
   at **4/7** (`kColdFactor = 0.57f`, WebZen's 400 + 300 ms a tile) and each swing waits **20 ticks more**
   (`kColdSwingTicks = 20`, DelayActionTime 1000 ms); a second SoD restarts the 10 s. The Statue of Saint, the
   Golden Dragon and the Elites take the blow and not the Cold (`fixed` / `shrugs`), as Ice Storm's freeze.
8. **The tooltip**: "10% on a hit or a skill: the ground breaks under the target, striking up to 8 monsters within
   3 tiles and slowing them for 10 s".
9. **The look, MU's**: BLOW_OF_DESTRUCTION.wav at the Loosed; **MU's 16 empty frames skipped** (they waited on the leap,
   which a rune does not play), so the burst shows at once: the blue flares (4 cells a metre before him, 6 cells at
   the point), Swordeff_mono at his side and flare01 over the point, decaying /1.05 a frame; the four violet blade
   crowns (0.3, 0.3, 1.0); the crack star before him, the line of cracks toward the target every 55 units, the big
   crack at 1.2 with the columns sinking over five keys; ten frames of splash and smoke; 5-7 rock chips; a camera
   shake ten frames for the one who threw it; and on each body Cold takes, the Ice01 crystal once and the ice tint
   while it holds.
10. **Said**: `What::Loosed`, skill 232, `whom` the first, `c` the count chosen, x/y the point, his heading in
    thousandths of a radian (as Twister's) for the crack line; the held blows are ordinary `Hit`s.

# 6. Open calls for the user

1. **Force.** 1.65 is MU's 110 on the wizard Ancients' scale, against the knight's Legendaries at 0.8-0.9 (Twister,
   Hellfire) -- and on a Blade Knight's swing, which is far harder than a Soul Master's. With the DK kept strongest
   (class balance 2026-10-23), 1.2 (Ice Storm's) is the gentler choice; the Ancient hunt in sim_test will say.
2. **Cold: its own, or our chill.** MU's 0.57 walk / +20 ticks for 10 s (a new `coldUntil`), or the existing chill
   (0.5 / +16) laid for 200 ticks -- simpler, nearly the same to see, but a chill is a 1-3 s thing everywhere else.
   And MuMain draws them at a third the walk (0.33, OpenMU's too) where WebZen's server walks them at 0.57: which one.
3. **Rolling on skills.** Once a skill use (proposed), or only on a skill that struck something? And should the
   knight's Twister/Hellfire Legendaries start rolling on skills too (they never have)? Proposed: no, the Ancient only.
4. **The hold.** 10 ticks puts the numbers half a second after the burst we now show at once; 0 strikes with it.
5. **The count.** MU's 4 sure + coin flips to 8, or a flat 4 as Pollution and Requiem.
6. **One at a time?** Requiem draws one at a time; SoD is light enough (~2,600) for two, but each struck body's Ice01
   crystal adds 256.
7. **The camera shake.** MU shakes the screen ten frames on every SoD; on a 10% proc in a fast hunt that may be too
   often. Keep it, keep it smaller, or drop it.
8. **The number** if the five DW Ancients are removed: 80 now with the removed rows kept as never-dropping rows, or a
   save migration that closes the numbers (SoD 75).
