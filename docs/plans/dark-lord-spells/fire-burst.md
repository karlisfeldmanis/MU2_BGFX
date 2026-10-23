# Fire Burst (skill 61) -- research dossier

Researched 2026-10-23 before any build (the user: "we do very deep reserach for each DL spell then we make it").
Nothing here is built. Paths: **W** = WebZen 1.00.93 GameServer (`Source/Server Side/GameServer`, cp949),
**WD** = its `_Server Files/Data`, **M** = MuMain `src/source` (data `src/bin/Data`), **O** = Main 1.03.35
(`third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source`, where the skill is `AT_SKILL_LONGPIER_ATTACK`),
**OM** = OpenMU `Persistence/Initialization/VersionSeasonSix`.

## 1. MU's rule (server)

- **Row** WD `lang/kor/skill(Kor).txt:50` (and `lang/main/skill.txt:66`): `61 FireBurst 74 100 25 0 6 0 20 ...`,
  so **level 74**, damage 100, mana 25, AG 0, reach 6, energy 20 raw. The raw energy becomes **79** through
  `20 + rEne*rLvl*4/100` (W MagicDamage.cpp:664-671). OM SkillsInitializer.cs:179 agrees on 100/6/25/79. OM
  leaves out the level and gives `implicitTargetRange: 1`; **WebZen's 3 wins**.
- **Scroll** OM Items/Orbs.cs:57: group 12 number 21, drop level 74, 115,000 zen. Our 12/21 is Skillshot, so it
  needs a free number (plan section 7).
- **Cast** W ObjUseSkill.cpp:3675-3777 `SkillFireBurst`:
  - `gObjAttack` on the aimed body **at once** (:3689-3690).
  - Then every monster in his viewport (also a duel foe or a guild-war foe; not a summon) with
    `gObjCalDistance(target, it) < 3` (:3755) gets a delayed hit of **500 + (rand*17)%300 ms**
    (500-799, :3757-3762) through `gObjAddAttackProcMsgSendDelay(MSG_ATTACKPROC_ENTIREMAGIC)`.
- **Distance** W user.cpp:7042-7053 is `(int)sqrt(dx^2 + dy^2)` on whole tiles. So `< 3` takes (2,2) and leaves out
  (3,0), which is a circle of about 2.83 tiles **round the target**, not round him.
- **No thinning**: `rand()%3` (:3741) applies only once `loopcount > 0`, and `loopcount` never moves. Every monster
  in range is hit.
- **The delayed hit** W user.cpp:7378-7386 is a full `gObjAttack` with its own hit/miss roll, dealt only if the caster
  is still alive, with **no distance check when it lands**.

## 2. Damage

- Skill damage W MagicInf.cpp:237-238 rolls **100..150** (`max = dmg + dmg/2`). It is added to the weapon's min/max
  (W ObjAttack.cpp:3152-3153, 3326-3331), plus **Str/25 + Ene/50** (:3259-3266), before defence.
- Then ×**(200 + Ene/20)%** (ObjAttack.cpp:1452-1463). This is the same path as Force: `sim::force` and
  `Realm::strikeAt` already do both for any `Kin::DarkLord` row.

## 3. Client (MuMain, checked against 1.03.35)

**When it fires.**
- The click calls `UseSkillWarrior`, which plays **no local animation** (M SkillCast.cpp:328-332), turns him to the
  target (:378-381) and sends the request at once (:405).
- So **the server's blow on the aimed body lands before his swing starts**.
- The echo (M WSclient.cpp:4261-4278) starts the clip: `PLAYER_ATTACK_STRIKE` on foot (our 80),
  `PLAYER_ATTACK_RIDE_STRIKE` on a Uniria, Dinorant or Dark Horse outside a safe zone (our 82), and the Fenrir
  strike on a Fenrir. It also plays `eFirebust.wav`.
- The effect spawns on the first tick the strike's `AnimationFrame >= 3` (M ZzzCharacter.cpp:2876-2883,
  4140, 5065-5086; O :3212-3220). That is about 12 ticks (0.48 s) after the echo with no attack speed (STRIKE
  PlaySpeed 0.25 + AttackSpeed1, :949).

**What spawns** (at bone 0 × (40,0,10) in MuMain, **(80,0,20) in 1.03.35**, O ZzzCharacter.cpp:5987):
- **Three heads**, `MODEL_PIER_PART` subtype 0, owner = the target, yaw at his facing +90 / 0 / −90. The +90 head
  aims at the target's top (BoundingBoxMax z); the other two aim at half height.
  - Created with LifeTime 20, Velocity 10, Gravity 2, Scale 1.2, Direction (0,−26,0), drawing mesh 0 only
    (ZzzEffect.cpp:3381-3394).
  - **Flight** (MoveHandlers.cpp:5598-5643): homing (`MoveHumming`, at most Velocity° a step, Velocity +0.4 a
    step) at 1, then 2, then 3 steps of 26 units a frame, 48 steps in all. The aim follows a moving target.
  - Each step stamps a trail piece.
  - **No hit test and no impact effect**: the `Distance<40 && LifeTime==5` block is dead code. The heads live all
    20 frames (0.8 s) and circle the aim.
  - Simulated (2000 seeds), frames from spawn to within 40 units of the aim:

    | target | side heads | middle head |
    |---|---|---|
    | 3 tiles | median 8-9 (0.32-0.36 s), p90 15-16 | median 7 (0.28 s) |
    | 6 tiles | median 13 (0.52 s) | median 12 (0.48 s) |

  - The side heads swing **±120-155 units** sideways, peaking about 115 units down-range. **No head ever goes behind
    him.**
- **Trail pieces**, subtype 1:
  - Mesh 1 of PierPart, crossed quads with `m_Piercing02.OZT` (32×32 RGBA), scale 0.5.
  - They stay where they are stamped and die with their head.
  - Alpha = (20 − head life)/5, then ÷1.3 a frame in the last 5.
  - Each piece spawns one `BITMAP_FIRE+1` particle, `Effect/Fire02.OZJ` 64×64, **additive**:
    - LifeTime 12, scale 0.8 → about 2.1 (51 → 136 units).
    - It drifts about 150 units along the head's heading and rises about 26.
    - Its grey light is LifeTime·0.2 (ZzzEffectParticle.cpp:569-574, 4767-4775).
  - About **144 particles a cast**: this is the bulk of the fire.
- **The head mesh**: mesh 0, an ellipsoid along the flight about 36×90×36 at 1.2, `m_Piercing.OZT` 64×64 RGBA, lit,
  normal alpha blend (ZzzBMD.cpp:1532-1550).
- **Two `MODEL_DARKLORD_SKILL`** (`DarkLordSkill.bmd`: one 206×114 quad, `KingS_R.OZJ` 128×128):
  - Additive, unlit, colour (1, 0.6, 0.3), LifeTime 10, no motion.
  - **Angles fixed at (45, ±45, 0) with yaw 0: a world-fixed X that ignores his facing** (ZzzEffect.cpp:3449-3477).
  - 1.03.35 (O ZzzEffect.cpp:14791-14799) grows it from scale 0.2 to about 2.1 (up to 433×239 units) and fades it
    ÷1.8 a frame under life 7.
  - **MuMain lost this**: `Move_MODEL_DARKLORD_SKILL` is empty (MoveHandlers.cpp:5681-5685), and the old code is
    unreachable at ZzzEffect.cpp:8045-8072. Port 1.03.35's.
- **Sounds**: `eFirebust.wav` (1.75 s) on the echo. `eFirebustBoom.wav` (1.29 s) is asked every head frame, but a
  playing DirectSound buffer ignores Play, so **one boom a cast**, from the heads' first frame
  (ZzzOpenData.cpp:4869-4870; DSPlaySound.cpp:325-342).
- **Splash victims**: plain damage numbers, nothing drawn for Fire Burst (WSclient.cpp:3237-3410).
- **Files present in Data**: Skill/PierPart.bmd, DarkLordSkill.bmd, m_Piercing.OZT, m_Piercing02.OZT, KingS_R.OZJ,
  Effect/Fire02.OZJ, Sound/eFirebust.wav, eFirebustBoom.wav.

## 4. 1.03.35 against MuMain

These are only the two differences above: the bone offset (80,0,20) against (40,0,10), and the X flash's growth and
fade, which MuMain lost. Everything else matches: the create values, the flight, the trail, the particle, frame 3,
the clips and the sounds.

## 5. Footage

"Mu Online Dark Lord Skill FireBurst" (youtube fzyLarVB7fw, zigzagmu.com, 2020; a later-season private server, so
the client is probably newer than both sources). A Dark Lord casts about ten in 20 s at a dragon, one cycle about
1.1 s. Stills: `fire-burst-footage.jpg` (frames +0, +0.1, +0.2, +0.43 s from the strike).

| from the strike | what shows |
|---|---|
| 0 s | the strike pose; a white triangular blade at his hand, here pointing back and to his side (the world-fixed X: where it points is chance); crystal shards bursting in front of him |
| +0.07 s | the shards fan out; an orange-red pyramid (the other X quad) by his feet, about 0.5 s |
| +0.1-0.25 s | two burning chain ribbons, one high and one low, curve from his hand onto the target (the side heads' trails) |
| +0.3-0.75 s | the ribbons gather into a big fireball on the target (the Fire02 particles drifting and growing), gone by about 0.75 s |
| damage | the number is already on the target at the strike, before the fire arrives (the server's early blow) |

## 6. Fit to our code (branch darklord)

- **Row**: appended last in `kRows` (src/sim/skills.cpp) as Force's is: `Kin::DarkLord`, `families arms::kNone`,
  `anyHand`, `damage 100`, `mana 25`, `reach 6`, `clip 80`, `release 3/7`, `needLevel 74`, plus a `coolTicks`
  (ours; 20 ticks a second). `kSkills` 41 → 42. Wire layout 15 already sizes cools by `kSkills`.
- **Splash**: `Realm::rain` (realm_fight.cpp:1917) already hits every monster round the aimed one, nearest first,
  stopping at walls (`seen`). It wants `splash` about 2.83 (a `< 3` whole-tile test to match WebZen exactly) and a
  per-victim delay.
- **Delay**: `Realm::loose(..., delay)` honours `delay` only on a fall (`fallTicks`, :1391). The splash needs it for
  a non-flying blow as well: 10-15 ticks off seeded dice, and the aimed body 0.
- **Damage**: the DL multiplier and the Str/25 + Ene/50 flat already apply to every DL row.
- **Clip**: 80 and 82 are already on cook.py's action list (Force), so `castTicks` knows their length.
- **Loosed drawing**: add an `else if` in play.cpp beside Force's, or it falls through to Energy Ball's bolt.
- **Effect**: a new `src/game/fx/fire_burst.*` on fx/force's pattern. Models PierPart (2 meshes) and DarkLordSkill,
  sheets m_Piercing, m_Piercing02, KingS_R, Fire02 (Fire02 may already be cooked for another effect: check
  content/effects). Sounds eFirebust and eFirebustBoom go into content/sounds (the showing cook).
- **Scroll**: group 12 is free at 23, 24 and 26 up. 23 and 24 are MU's Critical Damage and Electric Spike, so keep
  those for them. Fire Burst gets **12/26**. Mesh `skillScroll.bmd`. Every world's tables cook.

## 7. Decided (2026-10-23, from this research and the user's standing taste; the user: "you have all info about all spells why you ask me?")

1. **The X flash** is turned to his facing so both arms open forward ("only frontal"), with 1.03.35's growth and fade.
2. **The aimed body is struck when the middle head reaches it** (`flies` 10 less the tile a bolt stops short: 0.25 s at
   three tiles, 0.35 s at four, 0.55 s at six), not as MU's server does before the swing. The splash is 10-15 ticks
   after the let-go, every monster at whole-tile distance under 3, no wall test, nothing drawn on them.
3. **The spawn point** is 1.03.35's (80,0,20), the older client's, which has the working flash.
4. **25 mana** (MU's) and a **5 s cooldown** (100 ticks before agility's haste), for the bench to tune.
5. **MU's level 74 and energy 79** stand on the row. The scroll (12/26), Marlon (a level 1-30 quest, so not his
   Fire Burst) and Hanzo come after the spell is judged in game.

Built (sim): the row, `Realm::fireBurst`, `burstDice_`, tests in tests/sim_test_lord.cpp.
