# Flame Strike (skill 236, the Magic Gladiator's Season 4 sword skill) as an Ancient rune -- research dossier

Researched 2026-10-25 before any build, for the Magic Gladiator's one Ancient rune (one Ancient a class, decided
2026-10-25), on his weapon. The Soul Master's is Requiem (78, b5895e24; Sleep 79, ee85188e); the Dark Knight's is
Strike of Destruction (80, being built by another session, uncommitted in src/sim and src/game/fx today). Shaped as
strike-of-destruction.md: section 1 the server, 2 the client, 3 the footage, 4 the fit to our code and the budget,
5 the proposed rune and what is decided, 6 the open calls. Sources: WebZen GameServer 1.00.93 (github
ptr0x-real/Mu-GS-Webzen-MC-10093, shallow-cloned into this session's scratchpad, cp949, read through iconv with line
numbers unchanged) -- **it has skill 236**, so WebZen's rules stand first here; the S17 server
(`third_party/MuSource_S17`), OpenMU `LEGACY/reference/openmu`, MuMain `LEGACY/reference/MuMain`, Main 1.03.35
(`third_party/Main 1.03.35`, comments read as GBK), muextract on MuMain's Data, and YouTube footage.

**In one paragraph.** Flame Strike (Korean 플레임 스트라이크; the orb is the Crystal of Flame Strike, 12/47) is the
Magic Gladiator's Season 4 sword skill. He raises his blade, it **grows a sheath of fire nearly four metres long**,
and he **whirls it right round him at shoulder height** -- about 250 degrees in a quarter of a second -- so the
screen shows a **white-hot disc of fire four metres across** round him that cools to orange with a red rim and is
gone in half a second, leaving a pale trail in front of him. The server strikes at once, with no wait: every monster
in a **wide, shallow band in front of him** -- 5 tiles either side at his feet, narrowing to 2 either side 4 tiles out
-- **the first 8 surely and every one past the 8th on a coin flip, with no cap**, and **each one twice** (WebZen's
2008-10-15 fix calls the blow twice). Fire, skill damage **140**, the gladiator's blade double (x2) on it, mana 20,
AG 25, range 3, no cooldown. Each body struck takes a 1 s buff that does nothing but **set it flaring with fire for
0.8 s** on the client. It has no slow, no burn and no other debuff.

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93; cp949)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/`
- `S17/` = `MU2_BGFX/third_party/MuSource_S17/` (`GameServer/`, `_MuServer/Databases/`)
- `OM/` = `LEGACY/reference/openmu/src/`

## 1.1. Name and version
- WebZen enum: `AT_SKILL_FLAMESTRIKE = 236`, "플레임 스트라이크", in the `ADD_SEASON4_NEW_SKILL_20080522` set beside
  Strike of Destruction 232 ... Gigantic Storm 237 (WZC/include/public.h:349). **1.00.93 has it, so this dossier
  does not need S17 for the rules.** The define is on (strike-of-destruction.md 1.1, WZ/define2.h:943), and so is
  the fix it reads, `MODIFY_MAGUMSA_SKILL_BUGFIX_20081015` (WZ/define2.h:695; WZC/include/define.h:1169).
- It is listed among the skills that may be used (WZ/ObjUseSkill.cpp:488) and sent from `UseSkill` to
  `SkillFlameStrike(aIndex, lpMagic, TargetPos, aTargetIndex)` (WZ/ObjUseSkill.cpp:951-953). TargetPos here is
  the client's **angle byte**, not a tile.
- **WebZen's data has no row for it**: every WZD/lang/*/skill.txt ends at 202 (as for SoD). So the numbers come from
  S17 and OpenMU, and the rules from WebZen's code.
- It is an **orb skill**: item 12/47 gives 236 (WZ/MagicInf.cpp:205, "플레임 스트라이크"), with no energy needed
  (WZ/MagicDamage.cpp:790). OpenMU's orb is `CreateOrb(47, FlameStrike, 1, "Crystal of Flame Strike", drop level 100,
  level 220, ..., 380000 zen, ..., magicGladiatorClassLevel 1)` (OM/Persistence/Initialization/VersionSeasonSix/Items/Orbs.cs:54):
  every Magic Gladiator, from his first class.
- S17: `SKILL_FLAME_STRIKE = 236` (S17/GameServer/SkillDef.h:139). OpenMU: `SkillNumber.FlameStrike = 236`, with the
  master forms Strengthen 492 and Mastery 494 (OM/Persistence/Initialization/Skills/SkillNumber.cs:114, :372-374).
  MuMain: `AT_SKILL_FLAME_STRIKE = 236` (MM Core/Globals/_enum.h:439).
- The Empire Guardian boss Gaion has his own copy, `SKILL_SPECIAL_GAION_FLAME_STRIKE` (S17/GameServer/SkillDef.h:579,
  SkillHandler.cpp:7063-7065), with its own client model `MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE`. Not ours.

## 1.2. Skill row
- S17 `skill_template` 236 (S17/_MuServer/Databases/game.sql:45691), columns per game.sql:45543-45592:
  `level 100, damage 140, mana 20, stamina(AG) 25, distance 3, effect_radio 0, delay 0, type 0, attribute 3,
  buff_icon 83, base 236, allow_mg 1, animation 1, elemental_attribute 1, elemental_bonus 135, attack_delay 0,
  pvm 100, pvp 100, stat_1 1 / stat_value_1 200`.
  - attribute 3 = **FIRE** (`Element::{ICE, POISON, LIGHTNING, FIRE, ...}`, S17/GameServer/CommonDef.h:238-250).
  - buff_icon 83 = `BUFF_FLAME_STRIKE` (S17/GameServer/BuffDef.h:79); buff_template 83 is "Flame Strike", with no
    description and no effect (game.sql:126).
  - allow_mg 1 = the Magic Gladiator's first class. delay 0 and attack_delay 0: **no cooldown**.
  - stat_1 1 / 200 = strength / 200 added to the skill damage (S17 only; not WebZen's).
  - Master additions 492 and 1093 raise it (S17/GameServer/PlayerSkillUse.cpp:1122-1126). Not ours.
- OpenMU: `CreateSkill(FlameStrike, "Flame Strike", AllMGs, DamageType.Physical, damage 140, distance 3, AG 25,
  mana 20, levelRequirement 100, elementalModifier Fire, AreaSkillAutomaticHits)`
  (OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:233). The same 140, 3, 25 and 20 as S17.
- For scale, his other blade skills in S17's table: Fire Slash 55 is `damage 80, mana 17, AG 24, distance 3`
  (game.sql:45645), Power Slash 56 is `damage 0, mana 15, distance 5` (game.sql:45646). Flame Strike's **140** is the
  largest of the gladiator's, and 140/110 of Strike of Destruction's.

## 1.3. The shape and the targets (WebZen)
`SkillFlameStrike` (WZ/ObjUseSkill.cpp:7193-7290):
- **The shape**: `SkillFrustrum3(aIndex, bAngle, 2.f, 4.f, 5.f, 0.f)` (:7199). SkillFrustrum3 (:7166-7190, "a
  rhombus (-tx1, ty1), (tx1, ty1), (-tx2, ty2), (tx2, ty2) turned by bAngle about (0, 0)") lays the four corners
  **(-2, 4), (2, 4), (5, 0), (-5, 0)** in tiles, turns them by `bAngle * 360 / 255` degrees, truncates each to a whole
  tile and adds his own tile. `SkillTestFrustrum` (WZ/user.cpp:32093-32105) keeps a body whose tile is on the inner
  side of all four edges (edges inclusive).
  So it is a **trapezoid that is wide at him and narrow ahead**: **11 tiles across his own row** (5 either side),
  narrowing to **5 tiles across 4 tiles out** (2 either side), 4 deep, nothing behind him. It covers about 28 tile²
  (`(10 + 4) / 2 x 4`), fewer once the corners are truncated at an angle. It is the reverse of Power Slash's wedge
  (corners (-1, 0) (1, 0) (6, 6) (-6, 6), narrow at him; src/sim/skills.h:343-350): the band a whirl sweeps round his
  front, not a fan thrown forward.
- **Who**: his viewport's monsters (not summoned ones), the forced target, a duel enemy, anyone during a castle siege,
  a guild-war enemy (:7213-7256).
- **The count**: `if (HitCount >= 8) { if (rand()%2) bAttack = TRUE; } else bAttack = TRUE;` and `HitCount++` for
  every body in the shape, struck or not (:7262-7290). So **the first 8 are always struck, and every body past the
  8th on a coin flip, with no cap** (only the viewport's size limits it).
- **Twice each**: under `MODIFY_MAGUMSA_SKILL_BUGFIX_20081015` the blow is `gObjAttack(lpObj, &gObj[tObjNum], lpMagic, 1, 0)`
  **called twice** for each body chosen (:7279-7288). Before the fix it was called once. Each call is a whole attack:
  its own hit roll, its own damage and its own numbers.
- **When**: at once. There is no `gObjAddAttackProcMsgSendDelay` here, unlike SoD's 500 ms and Requiem's 1000 ms.
- The point the client sends is not used. Only his angle and the target index matter.

## 1.4. Damage (WebZen)
- 236 takes the **knight-skill branch** of the damage (WZ/ObjAttack.cpp:754, listed with Multi-Shot, Strike of
  Destruction and Chaotic Diseier): weapon damage plus the skill's 140, rolled `dmg .. dmg + dmg/2` (WZ/MagicInf.cpp:236-238).
- In the per-skill switch (WZ/ObjAttack.cpp:1810-1815):
  ```
  case AT_SKILL_FLAMESTRIKE:
      AttackDamage = AttackDamage * 2;
      gObjAddBuffEffect( lpTargetObj, BUFFTYPE_FLAMESTRKE, EFFECTTYPE_NONE, 0, 0, 0, 1 );
  ```
  The `* 2` is **every gladiator blade skill's** multiplier, not Flame Strike's own. Twisting Slash, Blood Attack and
  Spiral Slash take `AttackDamage*2` for CLASS_MAGUMSA where a knight takes `(200 + Energy/10)%`, and Power Slash
  (SWORD6) takes `*2` too (WZ/ObjAttack.cpp:1380-1440). So nothing is special to Flame Strike beyond its 140 and the
  double call.
- `BUFFTYPE_FLAMESTRKE` (WZ/BUFFTYPEDEF.h:115; the 83rd, counted from Berserker 81 and Lightning Shock 85 beside it)
  lasts **1 s** with `EFFECTTYPE_NONE`: **it does nothing on the server**. It exists so the client draws the fire on
  the body (2.6).
- **Per body, then**: two blows of (weapon + 140..210) x 2, fire. No resistance roll, because there is no debuff to
  resist.

## 1.5. S17
- `SkillAttackFlameStrike` (S17/GameServer/SkillHandler.cpp:1926-1962): `SkillAngleCalculate(angle, 2.0, range + 1
  = 4, 5.0, 0.0)` is **WebZen's trapezoid exactly** (the range is 3, so 3 + 1 = 4); `if (hit_count < 8 || RANDOM(2))`
  then `AttackProc` **twice** and `hit_count++`. That is the same 8 sure, coin flips after, no cap, twice each. One
  difference: hit_count only counts bodies struck.
- The damage: Flame Strike is in the long blade-skill case list, where a non-knight, non-Slayer player takes
  `damage * 2` (S17/GameServer/Unit.cpp:3060-3096). That is WebZen's x2. The buff: `AddBuff(BUFF_FLAME_STRIKE, 1, ...)`,
  **1 s** (Unit.cpp:2285-2288).

## 1.6. OpenMU (S6)
- `AddAreaSkillSettings(FlameStrike, frustum true, startWidth 5, endWidth 2, distance 4)`
  (OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:234; again in
  OM/.../Updates/AddAreaSkillSettingsUpdatePlugIn.cs:66). Its FrustumBasedTargetFilter lays the corners
  `(-end, distance), (end, distance), (start, 0.99), (-start, 0.99)` (OM/GameLogic/PlayerActions/Skills/FrustumBasedTargetFilter.cs:218-246):
  **the same trapezoid** in floats, starting just in front of him.
- One hit a target (`maximumHitsPerTarget` default 1), **no cap**, at once. There is no x2 and no flaring buff.

## 1.7. Differences
1. Shape: the same trapezoid in all three: 5 either side at him, 2 either side 4 tiles out.
2. Count: WebZen and S17 strike 8 surely and the rest on coin flips, uncapped; OpenMU strikes every one, uncapped.
3. Blows: WebZen (after the 2008-10-15 fix) and S17 **strike twice**; OpenMU strikes once.
4. Timing: **at once** in all three.
5. Debuff: none. WebZen and S17 lay a 1 s buff that only draws fire.
6. The same in all: 140, fire, range 3, mana 20, AG 25, no cooldown, the gladiator's from his first class.

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data, **103** = Main 1.03.35's
`MU/[11.07.12] MU_Chs_1.03.35/source`. Ticks are 25 a second.

## 2.1. The cast
- The gladiator's arm of `AttackCharacter` (MM GameLogic/Combat/ClassAttack.cpp:786-815): it turns him to the target
  point, checks the tile is in range (`CheckTile`), sends a one-step `SendCharacterMove` to his own tile, then
  0x1E MagicContinue with **his angle as a byte** (`(Angle + 180) / 360 * 255`, the server's bAngle) and the target
  key, and at once `SetAttackSpeed(); SetAction(o, PLAYER_SKILL_FLAMESTRIKE)`.
- **A sword in his right hand**: the skill bar greys it out unless his right hand holds a sword
  (103 NewUIMainFrameWindow.cpp:3110-3136, "검을 들어야 활성화" -- active only with a sword). Under
  `YDG_FIX_MACE_FLAMESTRIKE` this widens to swords through spears (MODEL_SWORD .. MODEL_SPEAR: swords, axes, maces,
  spears). The server does not check the weapon.
- Others see it from the echo: `ReceiveMagicContinue` plays `PLAYER_SKILL_FLAMESTRIKE` on the caster
  (MM Network/Server/WSclient.cpp:5449-5450) and sets `AttackTime = 1` (:5481). `AttackStage` jumps AttackTime
  straight to 15 for 236 (MM Engine/Object/ZzzCharacter.cpp:2997-3000). On the next frame it passes
  g_iLimitAttackTime and creates the effect: `CreateEffect(MODEL_EFFECT_FLAME_STRIKE, his position, his angle, his
  light, subtype 0, him)` and `PlayBuffer(SOUND_SKILL_FLAME_STRIKE)` (ZzzCharacter.cpp:4755-4760). **Frame 0 = the
  echo; the effect is born at frame 1.**

## 2.2. The pose (PLAYER_SKILL_FLAMESTRIKE)
- Action **184** of Player/player.bmd (MM _enum.h:3097, eight after BLOW_OF_DESTRUCTION's 176; muextract reads **14
  keys**). PlaySpeed **0.69 + MagicSpeed2** (ZzzCharacter.cpp:996), so at base speed the 13 key-steps take **19
  frames, 0.75 s**. It is the fastest skill pose in MU, and it gets faster with his speed.
- muextract export-rig, with forward kinematics on bone 33 (`knife_gdf`, the right-hand weapon bone, child of
  Bip01 R Hand), and the blade measured 350 units down its local -Y, where the flame model lies (2.3). MU faces -Y;
  yaw is measured from his front toward his right:
  | key | frame (base) | hand height | blade tip, yaw / reach | |
  |---|---|---|---|---|
  | 0-2 | 0-3 | 0.8 → 0.4 m | swinging about at his side | the wind-up: he crouches (root 107 → 60) |
  | 3 | 4 | 1.0 m | +30°, 3.0 m | rising (root 108) |
  | 4 | 6 | **1.9 m** | -88°, 3.3 m | blade overhead, held out to his left |
  | 5 | 7 | 1.9 m | -219° (behind him), 3.3 m | **the whirl starts** |
  | 6 | 9 | 1.8 m | -330°, 4.1 m | round to his front right |
  | 7 | 10 | 1.75 m | -407° (= -47°), 4.4 m | past his front |
  | 8-9 | 12-13 | 1.6 → 1.3 m | -453 → -463°, 4.3 m | coming to rest on his left |
  | 10-13 | 14-19 | 1.1 → 0.8 m | -82 → -47°, 4.1 → 3.8 m | the follow-through drops, root back to 101 |
  So from key 4 to key 9 (frames 6-13, a quarter of a second) the blade **turns about 375°**, a whole turn, at
  shoulder height, its flaming tip **3.3-4.4 m** from his middle. Keys 5-9, inside the ribbon's window, are about
  245°. That is the disc in the footage, and it agrees with the server's band: 4-5 tiles out, round his front and sides.

## 2.3. The flaming blade (MODEL_EFFECT_FLAME_STRIKE)
- Model `D/Effect/FlameStrike.bmd` (MM Engine/Object/ZzzOpenData.cpp:4590-4591). muextract: **10 bones, one action
  of 14 keys, 2 meshes, 280 triangles**: mesh 0 `firehik02.jpg` **272 tris** (the flame) and mesh 1 `flamestani.jpg`
  **8 tris** (a flip-book strip). It is about 1 m thick and lies along its local -Y. At keys 0 and 10-13 it is
  **2.5 m long** (-0.7 to +1.8 m); at **keys 1-9 it is 4.5 m** (to +3.8 m): bones Bone04 / Sphere02 / Sphere03 go out
  from 1.53-1.83 m to 3.47-3.78 m at key 1 and back at key 10. So the flame **shoots out to its full length as he
  raises the blade** and draws back as he finishes. Its 14 keys are the pose's 14: it is made to ride the swing.
- Create (MM Render/Effects/ZzzEffect.cpp:4842-4851; 103 ZzzEffect.cpp:5683-5694): LifeTime **35**, Alpha 0,
  Velocity = the pose's PlaySpeed, AI 0, `m_iAnimation = rand()` (the ribbons' ids).
- Move (MM Render/Effects/Behaviors/MoveHandlers.cpp:7706-7795; 103 ZzzEffect.cpp:16835-16930):
  - Alpha **+0.1 a frame** to 1 (in by frame 10), then **-0.1 a frame once LifeTime < 20** (from frame 15). It dies
    when it falls under 0.1 or he is gone, so it is **out by about frame 25 (1.0 s)**.
  - Its AnimationFrame is **copied from his** while he plays PLAYER_SKILL_FLAMESTRIKE, so the flame keeps key with
    his blade.
- Render (MM ZzzEffect.cpp:9248-9274): placed at **his bone 33** (`RotationPosition(BoneTransform[33])` + his
  position), animated as a child of that bone, `BodyLight` his.
  - Mesh 0: `RENDER_TEXTURE | RENDER_BRIGHT` at Alpha, which is **added** (firehik02, an orange-yellow flame sheet).
  - Mesh 1: added at Alpha, its V stepped through a **4-frame flip-book**, `fV = ((int)(WorldTime * 0.05) % 16 / 4) * 0.25`,
    a new quarter every **80 ms** (flamestani.jpg, a vertical strip of flame frames).

## 2.4. The three ribbons (CreateObjectBlur off the flame's bones)
- While his AnimationFrame is **between keys 5 and 13** (frames 7-19 at base speed), **ten sub-steps a frame**
  (both his pose and the flame model re-animated at each tenth), three ribbons are laid
  (MoveHandlers.cpp:7737-7790):
  | ribbon | from bone → to bone (flame model) | span | type → texture | light |
  |---|---|---|---|---|
  | A | 9 Sphere04 (0.45 m) → 6 Sphere02 (3.57 m) | 3.1 m of the blade | 2 → `BITMAP_BLUR + 2`, Effect/motion_blur_r.jpg | (1, 1, 1) |
  | B | the same | the same | 2 → motion_blur_r.jpg | (1, 1, 1) |
  | C | 8 Sphere01 (0.35 m) → 5 Sphere03 (3.47 m) | 3.1 m | 5 → `BITMAP_LAVA`, Effect/Lava.jpg (ZzzOpenData.cpp:5409) | (1, 1, 1) |
  Texture by type: MM Render/Effects/ZzzEffectBlurSpark.cpp:352-364. A and B are the same strip laid twice, which
  doubles its brightness.
- An object blur (ZzzEffectBlurSpark.cpp:256-330): each sample pushes a new edge at the head, up to 599. **LifeTime
  30 frames from its first sample, never renewed**. Each frame it loses one edge (`Number - 1`). It is drawn
  **added** (`EnableAlphaBlend` = `glBlendFunc(GL_ONE, GL_ONE)`, MM Render/Textures/ZzzOpenglUtil.cpp:395-402) as
  quads with light `(n - j) / n` and U `j / n` along the tail (:342-407): brightest at the blade, fading back along
  the sweep.
- So: ten edges a frame for about 12 frames, about 110 edges round some 245° of a 0.35-3.5 m band. That is a
  **near-complete annulus of white-red-orange light 7 m across**. It then shrinks one edge a frame and dies at frame
  ~37 (1.5 s). The two white motion_blur_r ribbons over the lava one are the white-hot disc in the footage.
- The destructor removes ribbon subtypes 1, 2 and 3 (ZzzEffect.cpp:105-113), which are not the `rand() + 1..3` ids
  the ribbons were made with. So the ribbons always live out their 30 frames.

## 2.5. Sound
- `SOUND_SKILL_FLAME_STRIKE` = `Data/Sound/flame_strike.wav` (**1.20 s**, 44.1 kHz mono, 110,012 bytes), played once
  at the effect's birth (ZzzOpenData.cpp:4926; enum MM Audio/DSPlaySound.h:809; ZzzCharacter.cpp:4759).

## 2.6. The fire on a struck body (eDeBuff_FlameStrikeDamage = 83)
- `eDeBuff_FlameStrikeDamage = 83` (MM _enum.h:3977), the server's 1 s buff. When it is laid, once:
  `CreateEffect(BITMAP_FIRE_CURSEDLICH, body, subtype 2)` (MM WSclient.cpp:15295-15298). Its removal does nothing
  (the delete is commented out, :15492-15495).
- Subtype 2, added for this skill (103 ZzzEffect.cpp:975-980 under YDG_ADD_SKILL_FLAME_STRIKE): LifeTime **20**
  (MM ZzzEffect.cpp:659-662). Every frame (MoveHandlers.cpp:902-940), **8 times**, at a **random bone** of the body:
  - a `BITMAP_LIGHT` sprite (Effect/flare01.jpg) at **scale 4**, in **(1.0, 0.2, 0.0), orange-red**, for that frame;
  - and one fire particle at scale 1.3-1.7, chosen evenly from:
    - `BITMAP_FIRE_HIK1` sub 0 (Effect/firehik01.jpg): life 27-31, scale 0.72-1.44 (x the call's), **rising 6.4-8.8
      units a frame**, shrinking 0.05-0.07 a frame, alpha in at 0.2-0.3 a frame, out 0.2 a frame once life < 15
      (MM Render/Effects/ZzzEffectParticle.cpp:1843-1852, :6090-6125);
    - `BITMAP_FIRE_HIK3` sub 0 (Effect/firehik03.jpg): the same with life 17-21 (:1920-1929);
    - `BITMAP_FIRE_CURSEDLICH` sub 4 (Effect/firehik02.jpg): life 12-16, the same rise (:318-326).
  Textures: ZzzOpenData.cpp:5218, :5235-5236. So **for 0.8 s the body bursts into rising flame** from all its bones,
  under an orange glow, with about 100 flames alive at once. This is the client's whole picture of the hit.

## 2.7. Not ours
- The pose (action 184): a rune's proc plays no pose on him (Requiem and SoD play none).
- `MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE`, Gaion's copy (MoveHandlers.cpp:8583-8588).

## 2.8. MuMain vs 1.03.35
- The create, the move (frames 5-13, ten sub-steps, the ribbon types and bones, the alpha steps), the render (bone 33,
  the flip-book) and the body fire's subtype 2 are identical (103 ZzzEffect.cpp:5683-5694, :8708-8744, :16835-16930,
  :975-980). The only difference: 1.03.35 has the sword-only gate (2.1).

## 2.9. Timeline (25 frames a second, base attack speed, frame 0 = the echo / the pose's start)
| frame | s | what |
|---|---|---|
| 0 | 0 | the pose begins (the caster's own client began it at the send); the server has already struck every body twice |
| 1 | 0.04 | the flame is born on his blade (alpha 0); flame_strike.wav (1.2 s); the struck bodies burst into flame (20 frames) |
| 1-4 | 0.04-0.16 | the flame shoots out to 4.5 m (key 1) and fades in; the wind-up crouch |
| 6 | 0.24 | the blade is overhead, held out to his left; the flame at alpha 0.5 |
| 7-13 | 0.28-0.52 | **the whirl**: the blade turns about 245° at 1.3-1.9 m, the tip 3.3-4.4 m out; three ribbons ten edges a frame -- the white-hot disc |
| 10 | 0.4 | the flame at full alpha |
| 13-19 | 0.52-0.76 | the follow-through drops; the ribbons still laid to key 13 (frame 19) |
| 14 | 0.56 | the flame draws back to 2.5 m (key 10) |
| 15-25 | 0.6-1.0 | the flame fades out (-0.1 a frame) |
| 21 | 0.84 | the body flames stop spawning; the last die by ~frame 50 |
| 19-37 | 0.76-1.5 | the ribbons shrink an edge a frame and die at frame ~37 |

## 2.10. Assets (under D)
| What | File | Size | In our content |
|---|---|---|---|
| flaming blade | Effect/FlameStrike.bmd | 10 bones, 14 keys, 280 tris (272 + 8) | no |
| flame sheet | Effect/firehik02.OZJ | -- | content/textures/firehik02.png (an item texture: Sword27, Shield19) |
| flip-book strip | Effect/flamestani.OZJ | -- | content/textures/flamestani.png (the same item set) |
| ribbons A, B | Effect/motion_blur_r.OZJ | -- | **yes**, content/effects/trail/motion_blur_r.png (Requiem's crescent uses it) |
| ribbon C | Effect/lava.OZJ | -- | content/textures/lava.png (not under effects) |
| body glow | Effect/flare01.OZJ | -- | **yes**, content/effects/light/flare01.png |
| body flames | Effect/firehik01.OZJ, firehik03.OZJ | -- | no |
| the pose (for baking the path) | Player/player.bmd action 184 | 14 keys | the rig is in our pipeline; the action is not used |
| sound | Sound/flame_strike.wav | 1.20 s | no |

# 3. Footage

YouTube "Mu Online Magic Gladiator Skill Flame Strike" (id TwgZD_t6vwE, 1280x720, 29.97 fps, 22.5 s; the same series
and dark Kanturu-style floor as the SoD and Requiem clips): a Magic Gladiator with a long sword casting Flame Strike at
a Dragon again and again. The casts come about every 0.9-1.5 s, so his attack speed is high. Still strip
`flame-strike-footage.jpg`, cropped to the fight: eight frames of the cast at 5.0 s in the clip, at +0.07, +0.13,
+0.17, +0.20 (top row) and +0.23, +0.30, +0.40, +0.53 s (bottom row). There was no Flame Strike clip in git's
MU2/docs/reference/skill-clips/magic-gladiator/ (fire-slash.mp4 and power-slash.mp4 are there and were used for the
comparison in 4.3).

| still | what shows |
|---|---|
| +0.07 | he lifts the sword; a gold-white glint runs up the blade (the flame fading in) |
| +0.13 | the blade sweeps up past the dragon's side; a short white streak |
| +0.17 | **a white-hot disc** round him, about 4 m in radius, washing out the floor and the dragon's legs; red-black flame shapes at its centre over him |
| +0.20 | the same, at its widest and whitest |
| +0.23 | cooling: yellow-white at the outer ring, **red flames inside**, he at the centre in a dark hole |
| +0.30 | orange-pink with a **red rim**, translucent; the inner red flames leaving |
| +0.40 | the disc is gone; a **pale pink wedge** before him where the last ribbon edges lie, fading |
| +0.53 | nearly nothing; he stands with the sword out low |

The footage agrees with the trace. The flame grows on the blade, the whirl lays a full ring of white over orange over
red, and it is gone in about half a second, faster than the code's 1.5 s ribbon life (the edges lose light along
the tail, so the last ones are faint well before they die). Two notes for us. First, **MU's is not subtle**: for
three or four frames the white additive disc washes out everything within four metres of him. Second, the fire on
the struck body (2.6) is hard to see on an orange dragon. The flaring is clearer in other clips on dark monsters
("Skill: Flame Strike, Gigantic Storm", xhtWsEkyNpM, 208 s, was found but not downloaded).

# 4. Fit to our code (main, 2026-10-25) and the budget

## 4.1. Where a gladiator's weapon rune rolls today
- **A plain swing that lands**: `stormcall` from strikeAt walks both hands' sockets and calls `callDown` for every
  weapon power he may set (src/sim/realm_fight.cpp:519-543). **An Ancient in his weapon already rolls here.**
- **A spell he casts** (the gladiator casts the wizard's spells; `skillFor`, src/sim/skills.cpp:1283-1296): `Realm::land`
  calls `stormcall` once a wizardry cast (realm_fight.cpp:1535-1543), so the Ancient rolls once a cast with no new code.
  A channel goes through `channelRunes` (:545-563), which already walks only the Ancients.
- **His blade skills**: these are Fire Slash, Power Slash, and the knight's Twisting Slash, Cyclone and others that he
  shares (skillFor). The arm the SoD session added to `Realm::land` (uncommitted, realm_fight.cpp:1543-1551) is
  **`hero.kin == Kin::DarkKnight` only**: a non-wizardry skill that struck something rolls `channelRunes` on the first
  body struck (`struckSince`, :1554-1565). The gladiator needs that test widened to
  `Kin::DarkKnight || Kin::MagicGladiator`.
- **Two traps for the gladiator's blades**:
  1. **Per-body rolls on a sweep.** `strikeAround` strikes a sweep's bodies with `row == nullptr` and `sweep_` set
     (src/sim/realm_skills.cpp:858-861), so `stormcall` runs once a body. SoD skips itself there
     (`power == StrikeOfDestruction && sweep_ != nullptr`, realm_fight.cpp:538). Flame Strike needs the same skip,
     or better, the test made "any Ancient" so the next class's needs no new line.
  2. **Power Slash strikes later.** Its waves are `loose`d with a wait of a tick or more each (`Realm::powerSlash`,
     realm_fight.cpp:2604-2622), so when `land` asks `struckSince` straight after `release`, nothing has been struck
     and **the Ancient never rolls on Power Slash**. Either roll it once a Power Slash on the aimed body regardless, or
     roll it from the first wave that lands (a flag on the hero). See 6.4.
- `runeAim` (realm_fight.cpp:1305-1318): what he struck, else the nearest he sees within kStormcallReach 4 (items.h:1236).

## 4.2. The rune
- **Power**: **81**. `Sleep = 79` is the last committed Power (HEAD src/sim/items.h:884). **80 is claimed by Strike of
  Destruction** in the uncommitted working tree (`StrikeOfDestruction = 80`, src/sim/items.h:906, with its row at
  items.cpp:791 and its constants at items.h:1455-1476). The numbers must run unbroken (drawRunePower walks
  `powerOf(p)` to the first gap, src/sim/realm_items.cpp:2874), so **Flame Strike is 81, and it must land after SoD's
  80**. If it is built before SoD is committed, the two sessions must agree on the order. No other session claims 81
  (`git diff src/sim`, checked 2026-10-25).
  The row goes after SoD's in `powerOf` (items.cpp ~795) as `kGladiatorOnly, kInWeapon, Rarity::Ancient, false,
  true`. **`second` is false**: our Magic Gladiator has no second class (sim::promoted covers only the wizard, elf and
  knight, src/sim/quests.h:326-331), and MU gives him the orb from his first class. It also goes into the "Doesn't
  stack" list (items.cpp:806-821).
- **The skill number**: `sim::skill::kFlameStrike = 236` beside kRequiem 224, kSleep 219 and kStrikeOfDestruction 232
  (src/sim/skills.h:205-212). It is MU's number and never a row, so kSkills and the learned mask keep their layout.
  236 is free.
- **The shape**: the band is WebZen's trapezoid. `Spread::Frustum` already measures one in floats for Power Slash
  (src/sim/realm_skills.cpp:794-803, constants skills.h:343-357), with near/far/deep as constants. Flame Strike needs
  its own three (`kFlameNear 5`, `kFlameFar 2`, `kFlameDeep 4`), and the test should become a small shared function
  (`inTrapezoid(hero, aim, body, near, far, deep, behind)`) so the rune and Power Slash use the same arithmetic.
  The band is laid along his line to `runeAim`'s body (on a plain swing he already faces it).
- **The roll**: a `Power::FlameStrike` branch in `Realm::callDown` (realm_fight.cpp:600) on SoD's pattern
  (:1026-1060): runeAim; the bodies in the band, alive and in sight, **nearest first** (as Power Slash orders them,
  where MU's order is the viewport's); said as `What::Loosed`, skill 236, `rune = true`, x/y his own feet and his
  heading in `a`'s spare field (as Twister's and SoD's), because the drawing whirls round him, not round the target.
- **The blows**: **at once**, as MU strikes. `runeStrike(hero, target, force)` (realm_fight.cpp:1320-1333) **twice for
  each body**, on the same tick, as WebZen calls gObjAttack twice in the same loop: two hit rolls and two numbers.
  `runeForce(hero, Element::Fire)` raises it with his fire runes and talents (as SoD's ice did). **There is no held
  list and no snapshot change** (unlike Requiem's `requiemBlows_`).
- **No debuff**: MU's buff 83 only draws. The body's fire is drawn from the struck Hits (a flag on the Hit, or the
  Loosed's victims), with no new Body field.
- **The drawing**: a `sim::skill::kFlameStrike` branch in play.cpp's Loosed switch beside Sleep's, Requiem's and SoD's
  (src/game/play.cpp:1489-1530), and a new `game/fx/flame_strike.{h,cpp}` on fx/requiem's pattern, wired in play.h,
  play_open.cpp, play_mode.cpp and CMakeLists.txt as b5895e24 did. Two pieces of Requiem carry straight over:
  - **its object-blur ribbon** (Requiem's crescent is `CreateObjectBlur` type 2 on motion_blur_r: 30-frame life, an
    edge lost a frame, `(n - j)/n` light, U `j/n`; fx/requiem.h) for ribbons A/B and C;
  - **its baked bone path** (`kBladeEnds`, tenths of a key from muextract export-rig, slerped as MU's Animation).
    Here it is bone 33 of action 184 baked **relative to his feet and facing**, with the flame model's four sphere
    bones on it.
  Models through muextract (`FlameStrike.bmd`, its 14 keys baked whole, as SoD's cracks), textures through
  decode_texture (firehik01/02/03, flamestani, lava under content/effects/flame_strike/, not the item copies), and
  pipeline/index.py's `_models(root, build, "flame_strike")` beside `"destruction"` (pipeline/index.py:2169-2171).
- **Sound**: flame_strike.wav into content/sounds, with one sounds.json key through `mu edit` (as `spell_requiem`),
  cooked with `./mu make sounds`.
- **Tests**: a `testFlameStrikeRune` in tests/sim_test.cpp beside testRequiemRune / testSleepRune /
  testDestructionRune. A level-200 gladiator hunting with plain swings, Fire Ball, Fire Slash and Power Slash should
  roll near the chance on each; one roll a skill use (never once a body on Fire Slash); never a body behind him or
  outside the band; never past the cap; **two Hits on each body, on the roll's tick**; and never a roll for any other
  class.

## 4.3. How it must read against his two fire blades
| | Fire Slash (55, src/sim/skills.cpp:930-933; fx/fire_slash) | Power Slash (56, :961-964; fx/power_slash) | **Flame Strike (rune)** |
|---|---|---|---|
| where | a front half-disc, 2.5 tiles | a wedge narrow at him, 6 wide at 6 out | **a band wide at him (11 across), 5 across at 4 out** |
| the shape seen | a 187° crescent 1.45 m round a pivot 1 m up, three frames | five blue-white waves running out to 7 m | **a 4 m flaming blade whirled ~250° round him at shoulder height: a full ring** |
| colour | fire on motion_blur_r2, blue lightning crackle on the blade | blue-white smoke and haze, a blue ground light | **white-hot to orange to a red rim; no blue anywhere** |
| on the struck | three skulls (the cut) | nothing | **each body bursts into rising flame for 0.8 s** |
| timing | the swing's key 3 | waves at 15 tiles a second | **at once, the ring 0.3-0.5 s, gone by ~1 s** |
- **What separates it**: its size (a ring twice Fire Slash's radius, all the way round him), its whiteness at the
  peak, the long flame on the blade, and the bodies catching fire. Fire Slash is a short, low crescent with blue
  lightning; Power Slash is blue and travels.
- **What to borrow**: from fx/fire_slash, the "colour over one as a second layer" addLit trick for MU's 1.0+
  overbright (fire_slash.h:54-58) and its warm ground light under the sweep, which MU does not have but our sunlit
  fields need. From fx/requiem, the object-blur ribbon and the baked bone path. Fire01/firehik flames are read
  through fs_effect's breath as fx/breath does (fire_slash.h:58-61).
- **A trap**: if the rune draws only ribbon C (lava) and the flame, it will read as a bigger Fire Slash. A and B (the
  white motion_blur_r) are what make it Flame Strike: keep at least one of them.

## 4.4. Budget (the transparent pass, 16,384, src/gfx/effects.h)
One Flame Strike at its busiest (frames ~12-20, 8 bodies struck):
| piece | count | tris / sprites |
|---|---|---|
| flaming blade (mesh 0 + flip-book) | 1 | 280 |
| ribbons A + B (motion_blur_r) | 2 x ~110 edges | ~220 quads |
| ribbon C (lava) | ~110 edges | ~110 quads |
| ground light | 1 | 0 (a point light) |
| body fire, each body: 8 flare01 a frame (one frame each) + ~95 flames alive | 8 bodies | ~830 |
| **total** | | **~1,450** |
- About half a Strike of Destruction (~2,600) and a quarter of a +11 Requiem (~5,250). With 12 bodies (past the 8th
  on coin flips) it is ~1,850. Two procs alive at once is ~3,000. It is cheap enough that "one at a time" is a taste
  choice, not a budget need.

# 5. Proposed rune and what is decided (from MU's numbers)

1. **Power 81 `Power::FlameStrike`, "Flame Strike"**, the Magic Gladiator's (kGladiatorOnly, `second` false: he has
   no second class, and MU's orb is his from the first), one to a thing, in his weapon, Rarity::Ancient, drawn among
   the Ancients' share as the others; it drops only in the Kanturu Relics and from their boss, as every Ancient. The
   Magic Gladiator's one Ancient. It is 81 because SoD holds 80; it lands after SoD's commit.
2. **Chance 15%** (`kFlameStrikeChance = 0.15`), to match its two siblings. The project's rule is about 10%, but
   Requiem and SoD both now stand at 15% (items.h, the user, 2026-10-25: "they have to match"; "hits harder, procs
   more often"). 10% if the user wants the older rule (6.3). It rolls on a **plain swing that lands** (stormcall), **once
   a spell he casts** (land's wizardry arm), **once a blade skill that struck something** (land's knight arm widened
   to him), and once a channel. It never rolls once a body on a sweep.
3. **Where**: a band along his line to what he struck; on a cast or skill that struck none, to the nearest he sees
   within kStormcallReach (`runeAim`). No body there, no proc.
4. **Who**: every monster in sight in **WebZen's trapezoid**: **5 tiles either side at his feet, narrowing to 2
   either side 4 tiles ahead, 4 deep, none behind him** (`kFlameNear = 5.0f`, `kFlameFar = 2.0f`, `kFlameDeep = 4.0f`,
   measured in floats as OpenMU does, the near edge through his own tile as Power Slash's `kFrustumBehind`), nearest
   first. **The first 8 surely, the 9th to 12th each on a coin flip on the rune dice, none past 12**
   (`kFlameSure = 8`, `kFlameMost = 12`). That is WebZen's and S17's 8; the cap of 12 is ours, where MU has none.
5. **Force: two blows of 1.1, fire** (`kFlameStrikeForce = 1.1f`, `kFlameStrikeBlows = 2`). MU's 140 on the Ancients'
   scale is 2.1 a blow (140 against Ice Storm's 80 at 1.2, as Requiem took 65/80 and SoD 110/80). MU strikes twice,
   which would make 4.2 a body. Ours **halves that, splitting one MU blow in two**: 2 x 1.1 = 2.2 a body, the same as
   SoD's 2.2 blow (and Requiem's 2.0). It is toned down from MU's double as Requiem's bleed was cut to a fifth. Two
   hit rolls and two numbers keep MU's double-hit feel. Each is his rune's blow (`runeStrike`) with `runeForce(hero,
   Element::Fire)`. WebZen's x2 is the gladiator's blade multiplier, not the skill's, so it is not carried; and
   `kKnightSkillBoost` is not applied (a rune's blow is not a skill row).
   - Against its siblings: SoD lands 2.2 on 4-8 bodies and then burns them for +100% (kSoDBurnShare 0.25 x 4), about
     4.4 a body. Requiem lands 2.0 and bleeds +1.0, about 3.0 a body. **Flame Strike is 2.2 a body with no damage over
     time, on up to 12 bodies in a wider band.** Its edge is the count and the band; the others' is the lingering
     damage. The Ancient bench in sim_test (the Kanturu bench SoD and Requiem were measured on) is the knob (6.2).
6. **When**: **at once**, both blows on the roll's tick, as all three servers strike. The ring takes 0.3-0.5 s to sweep,
   so the numbers come a little before the fire reaches the far side. This is MU's own order, and it needs no held
   list and no snapshot change.
7. **No debuff.** MU's buff 83 is a 1 s picture with no effect, so nothing is laid on the Body. There is no burn, so
   it does not double SoD's (now fire) burn.
8. **The tooltip**: "15% on a hit, a spell or a skill: a flaming blade whirls round you, striking up to 12 monsters in
   front of you twice with fire".
9. **The look, MU's, tuned to "subtle and clear"**:
   - flame_strike.wav at the Loosed;
   - **a ghost whirl, not his pose**: the FlameStrike.bmd flame (its 14 keys, firehik02 added, the flamestani
     flip-book a quarter every 80 ms) carried round him on MU's baked bone-33 path of action 184, relative to his feet
     and facing, played at **0.69 keys a frame** (MU's base PlaySpeed) from key 0. His own pose is untouched (as
     Requiem's Neil is a ghost beside the caster). Alpha in at +0.1 a frame and out at -0.1 from frame 15, as MU;
   - **the ribbons from key 5 to 13**, ten sub-steps a frame, 30-frame life, an edge lost a frame: **ribbon A once**
     (motion_blur_r, white) and **ribbon C** (lava). **Ours: B is dropped**, and A is drawn at about 0.6 of MU's
     light. MU's doubled white washes out four metres of screen in the footage, and our rule is subtle and clear.
     The ring's shape and its white-to-red cooling stay;
   - a warm ground light under the ring, fading with it (ours, as Fire Slash's);
   - **on each body struck, MU's flare for 0.8 s**: orange (1, 0.2, 0) flare01 and firehik01/02/03 flames from random
     bones. Ours: **4 a frame, not 8**, so eight burning bodies stay a flicker and not a bonfire.
   - No camera shake (MU has none for this skill).
10. **Said**: `What::Loosed`, skill 236, `whom` the first struck, `c` the count chosen, x/y his feet, his heading in
    thousandths of a radian (as Twister's and SoD's). The blows are ordinary `Hit`s, two a body.

# 6. Open calls for the user

1. **Two fire Ancients.** The Dark Knight's Strike of Destruction, MU's ice, was turned to **fire with a burn** in the
   SoD session ("more like fire", items.h, uncommitted). Flame Strike is MU's fire. Proposed: Flame Strike stays fire
   (it is MU's, and it is the gladiator's element: Fire Slash, his Inferno talents), but it has **no burn**, and its
   look (a ring round him, no ground burst) keeps the two apart. Or SoD goes back to MU's ice and Cold.
2. **Force.** Two blows of 1.1 (2.2 a body, SoD's blow, no damage over time), or MU's full double on our scale (2 x 2.1
   = 4.2), or one blow of 2.2. The gladiator's hour on the Kanturu bench decides; if he falls behind the knight with
   SoD's burn, 2 x 1.3 is the next step.
3. **Chance: 15% or 10%.** 15% matches Requiem and SoD as they stand ("they have to match"); 10% is the original
   Ancient rule.
4. **Rolling on his blade skills.** (a) Widen the knight's skill arm in `Realm::land` to the gladiator (proposed).
   (b) Power Slash's delayed waves never count as "struck something" at land time: roll once a Power Slash on what it
   was aimed at, or from the first wave that lands (proposed: the first wave, so a Power Slash that hits nothing rolls
   nothing, as for the knight). (c) The sweep skip at realm_fight.cpp:538 made "any Ancient" rather than one line per class.
5. **The count.** WebZen's 8 sure with coin flips after, capped at 12 (proposed), or MU's uncapped, or the siblings'
   4 sure / 8 most.
6. **The look's strength.** MU's two white ribbons wash out the screen for a few frames (the footage). Proposed: one
   white ribbon at 0.6 plus the lava one, and 4 flames a frame on a struck body, not 8. Or MU's full ring.
7. **The name.** **talents_table.inc already has a talent called "Flame Strike"** (src/sim/talents_table.inc:241, in
   the Inferno tree: "Hellfire damage", icon 483). A rune and a talent with the same name will read as one thing in
   tooltips. Rename the talent (for example "Brimstone" or "Hellfire Core"), or name the rune something else.
   Proposed: rename the talent, since the rune is MU's skill name.
8. **His Season skills.** The gladiator's Season skills are being researched one at a time (Fire Slash, Power Slash;
   docs/plans/mg-skills/). If Flame Strike is ever meant to be his learned skill, making it his Ancient rune takes it.
   Decide now: rune only (proposed), or keep it for a later skill and choose another Ancient (Gigantic Storm 237 is
   the other Season 4 gladiator skill).

## Decisions for the user, in short
- Fire for both SoD and Flame Strike (Flame Strike with no burn), or SoD back to ice?
- Force 2 x 1.1 a body (proposed), 2 x 2.1 (MU's), or 1 x 2.2?
- Chance 15% (matching Requiem and SoD) or 10%?
- Roll on Power Slash from its first wave that lands (proposed), or once a cast?
- Count: 8 sure, coin flips to 12 (proposed), uncapped, or 4/8?
- Look: one white ribbon at 0.6 and lighter body fire (proposed), or MU's full white-out?
- Rename the "Flame Strike" talent (proposed), or rename the rune?
- Flame Strike as his Ancient (proposed), or keep it free for a later learned skill?

## 7. Decided (the user, 2026-10-25)

- **Both fire:** Flame Strike stays MU's fire beside Strike of Destruction's fire.
- **15%** a roll, as Requiem and Strike of Destruction.
- **The talent is renamed** (talents_table.inc:241's "Flame Strike", Hellfire damage); the rune keeps MU's name.
- **Ancient rune**, the Magic Gladiator's one -- not kept back as a learned skill.
