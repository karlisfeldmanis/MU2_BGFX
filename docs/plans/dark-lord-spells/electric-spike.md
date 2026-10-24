# Electric Spike (skill 65) -- research dossier

Researched 2026-10-23/24 before any build (the user: "do next spell. first reserach than code"). Drafted by a
research agent and **re-checked against the sources** on 2026-10-24: the skill.txt row, `SkillElectricSpark` and its
flat 500 ms, the hit table (decoded again here), the flat add and `GetPartyMemberCount`, the client's cast
(ClassAttack.cpp:916-946), frame 5.5 (ZzzCharacter.cpp:2885-2895), the bolt (:4391-4404), the gather, wisps and
rings (:10512-10560), the joints' create (ZzzEffectJoint.cpp:2489-2582) and the footage stills.

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`.
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- The WZ repo has **no client source** (`_Client Files/` is empty).
- MU time: one reference tick = 1/25 s (`FPS_ANIMATION_FACTOR` = 1 at 25 fps). Our realm: 20 ticks/s (`realm_fight.cpp:1379`).

Names: WZ `AT_SKILL_ELECTRICSPARK` (65), MM `AT_SKILL_THUNDER_STRIKE`, OM `SkillNumber.ElectricSpike`, the scroll is "Scroll of Electric Spark" (12,24).

## 1. MU's rule (server)

### The row
`WZD/lang/main/skill.txt:70` (same in `lang/kor/skill(Kor).txt:54`):
`65 "일렉트릭 스파이크" 92 250 0 100 10 5000 29 340 -1 1 ... DL=1`

Columns from the loader `WZ/MagicDamage.cpp:336-360` (and the file's own header, `skill.txt:12`): number, name, **level 92**, **damage 250**, **mana 0**, **AG 100**, **distance 10**, **delay 5000 ms**, **energy 29 (raw)**, **command 340**, attribute -1 (none), attr2 1, then use type, brand, kill count, guild ranks, class flags (DL column = 1).

| Field | Value | Source |
|---|---|---|
| Level needed to learn | 92 (checked only when `m_bCheckSkill`) | `WZ/user.cpp:28426-28430` |
| Energy needed | 20 + 29·92·4/100 = **126** | formula `WZ/MagicDamage.cpp:664-676`; OM agrees, `VersionSeasonSix/SkillsInitializer.cs:184` |
| Command needed | **340**, checked when learning | `WZ/user.cpp:28114-28125` (`gObjMagicAdd`) |
| Damage | 250, rolled **250..375** (`min = dmg`, `max = dmg + dmg/2`) | `WZ/MagicInf.cpp:237-238` |
| Mana / AG | 0 mana, **100 AG** | skill.txt; spent in `WZ/ObjUseSkill.cpp:686-708` before anything is hit |
| Reach | 10 (client: the clicked ground tile must be within 10 tiles, Euclidean, `MM/Engine/Object/ZzzInterface.cpp:492-503`, called `MM/GameLogic/Combat/ClassAttack.cpp:916`) | |
| Cooldown | 5000 ms. **The server does not enforce it**: `CSkillDelay::Check` returns TRUE on its first line (`WZ/SkillDelay.cpp:28-30`). The client does (`MM/GameLogic/Skills/SkillManager.cpp:140-171`) | |
| Scroll | Scroll of Electric Spark (12,24), item level 92, price 295,000 | `OM/.../VersionSeasonSix/Items/Orbs.cs:60` |

### How it is cast
- Client sends packet 0x1E (`SendRequestMagicContinue`) **at the click**, before the animation starts (`MM/GameLogic/Combat/ClassAttack.cpp:916-930`), with the target tile packed in `TargetPos` (±8 tiles, 4 bits each).
- Server: `CGDurationMagicRecv` (`WZ/protocol.cpp:16772`) refuses if he is **teleporting** or **standing on a safe-zone tile** (`MODIFY_SAFEZONE_CHECK_ATTACK_20080903`, on in `WZ/define2.h:801`), then speed-hack check, then `UseSkill` (`WZ/ObjUseSkill.cpp:660`) spends mana/AG and broadcasts 0x1E, then `SkillElectricSpark` (`WZ/ObjUseSkill.cpp:833-836` -> `4219-4319`).
- No weapon needed: Electric Spike is in the "any hand" list (`MM/ClassAttack.cpp:498`).

### Shape (`SkillElectricSpark`, `WZ/ObjUseSkill.cpp:4219-4319`)
- Target tile = his tile minus (8 - TargetPos nibble) (`:4223-4224`). Angle = `GetAngle(his tile, target tile)` (`:4233`; `GetAngle` at `:3485-3493`: `atan2(y-ty, x-tx)·180/π + 90`, wrapped to 0..359).
- Every object in his **viewport** list is tested (`:4238-4312`). It may be struck if: a monster that is not someone's summon (`:4249-4252`); or the body he force-targeted, or his duel partner (`:4253-4261`); or any non-NPC during a castle siege on the siege map (`:4262-4270`); or a guild-war enemy (`:4272-4286`).
- Hit test: `SkillElectricSparkHitBox.HitCheck(angle, his x,y, target x,y)` (`:4290`). The box is a **lookup table** loaded from `WZD/skillElect.hit` (`WZ/Gamemain.cpp:3221`; class `WZ/SkillHitBox.cpp:39-58`, 36 directions of 10°, a 21×21 tile grid centred on him, `SkillHitBox.h:11-21`).
- Decoded `skillElect.hit` (and again on 2026-10-24) (15,876 bytes = 36×441). For direction 0 (target straight +Y) it is a strip **3 tiles wide**, running from **1 tile behind him to 10 tiles ahead**, his own tile excluded, 35 tiles in all:

```
row -1:  ###      (one row BEHIND him is hit)
row  0:  #o#      (o = his tile, not hit; the tiles beside him are)
rows 1..10: ###
```
  The diagonal directions are a staggered 4-wide band (35-53 tiles, e.g. 43 at 40°); every direction reaches ~10 tiles (max cell distance 10.0-10.2).
  **The table is irregular** (hand-drawn: 90° is a 2-3 row band reaching 10 tiles out with a stray 4 at row -2): no
  strip rule fits it -- the best of width x back x length over 36 directions is still 523 cells off. It is carried as is.
- **No victim cap**: every enemy in the strip is hit.
- Each victim gets `gObjAddAttackProcMsgSendDelay(..., MSG_ATTACKPROC_ENTIREMAGIC, victim, 500, skill)` (`:4292-4299`): the blow lands **500 ms after the server handled the cast**, every victim at the same moment (no per-distance delay). The delayed handler is `WZ/user.cpp:7378-7388` -> `gObjAttack(lpObj, target, lpMagic, magicsend 0, MSBFlag 1, 0, ...)`.
- Hit or miss: `gObjAttack` runs the normal `MissCheck` (attack rate vs defense rate) for each victim (`WZ/ObjAttack.cpp:620-647`).

### The party drain (decision 6 drops this)
If **any** victim was found (`Hit`), `DecPartyMemberHPandMP` (`WZ/ObjUseSkill.cpp:4314-4315`, body `4321-4369`): every **other** party member on the same map with `gObjCalDistance < 10` loses **20% of current HP and 5% of current mana** (`:4351-4355`). It happens at the cast, not when the blows land.
OM does the same, to every party member in his view (`OM/GameLogic/PlayerActions/Skills/AreaSkillAttackAction.cs:361-371`, ×0.8 HP, ×0.95 mana).

### Distance
`gObjCalDistance` = `(int)sqrt(dx²+dy²)` over tiles (`WZ/user.cpp:7042-7052`), so "< 10" is Euclidean distance strictly under 10.0 tiles.

### OpenMU differs
`OM/.../VersionSeasonSix/SkillsInitializer.cs:184-185`: physical, 250 damage, reach 10, 100 AG, `AreaSkillAutomaticHits`, frustum **1.5 wide at both ends, 12 long**, deferred hits **10 ms per tile of distance** (WZ: a 3-wide table, 10 long, a flat 500 ms). Bonus `+50 × NearbyPartyMemberCount` and `+Leadership/10` (`:617-618`). OM's `NearbyPartyMemberCount` counts party members in his observer list (`OM/GameLogic/Party.cs:511`), which does **not** obviously include himself; WZ's count does (see §2).

## 2. Damage

Path: `gObjAttack` -> DL is not wizard/MG/summoner, so the **final else -> `GetAttackDamage` (physical)** (`WZ/ObjAttack.cpp:907-934`), with `targetdefense` already reduced by debuffs.

Inside `GetAttackDamage` (`WZ/ObjAttack.cpp:3074+`):
1. Skill band = 250..375 (`:3150-3152`), + set-option skill damage (`:3159-3163`), + element resistance add if the skill has an attribute (it has none, -1).
2. **Electric Spike's flat add** to both ends of the band (`:3210-3220`):
   `Command/10 + 50 × GetPartyMemberCount`.
   `GetPartyMemberCount` (`WZ/ObjBaseAttack.cpp:1347-1380`) counts party members on his map at distance < 10, **including himself** (no self-exclusion; he is at distance 0). Not in a party -> the party index fails `CHECK_LIMIT` and it returns 0 (with an error log). So a full party of 5 standing together is +250.
   Note: Electric Spike takes this branch **instead of** the generic DL `Str/25 + Ene/50` (`:3259-3266`, an `else if` chain).
3. Weapon band + skill band rolled: `min + rand()%(max-min+1)` (`:3326-3345`). Critical (`m_CriticalDamage` %) or excellent (`m_ExcelentDamage` %) -> both ends at max (`:3350-3363`), + set crit add, + Harmony crit add, + **Critical Damage buff value** (`:3376-3386`); excellent adds 20% of max more (`:3388-3394`).
4. Minus target defense.
5. Back in `gObjAttack`: **× (200 + Energy/20) / 100** (`WZ/ObjAttack.cpp:1452-1463`, the case list includes `AT_SKILL_ELECTRICSPARK` at `:1459`).

## 3. Client (MuMain S6, cross-checked with 1.03.35)

### Cast action
Hero, at the click (`MM/GameLogic/Combat/ClassAttack.cpp:933-946`):

| Mount | Action | MuMain raw # (= our player.muc clip) | Keys in player.bmd | Play speed |
|---|---|---|---|---|
| on foot (and on Uniria/Dinorant, and anything in a safe zone) | `PLAYER_SKILL_FLASH` | **152** | 13 | 0.40 + MagicSpeed·0.002 (`MM/Engine/Object/ZzzCharacter.cpp:943`) |
| Dark Horse only (`MODEL_DARK_HORSE_ITEM`), not safe zone | `PLAYER_ATTACK_RIDE_ATTACK_FLASH` | **85** | 7 | 0.40 + ms (`:952`) |
| Fenrir, not safe zone | `PLAYER_FENRIR_ATTACK_DARKLORD_FLASH` | **95** | 8 | 0.40 + ms (`:958`) |

- Raw numbers counted from `MM/Core/Globals/_enum.h` (PLAYER_SET = 0; STRIKE comes out 80, which matches the plan's "player.muc's 80"). Key counts from `player.bmd` via muextract export-rig. Our `content/players/rig/actions.json` labels from 75 up are one slot high: the labels for 85 and 95 sit at 86 and 96. At 152 the label already says "Skill flash" and the bench saw Aqua Beam's arm-throw there (2026-10-02 note in that file), so **clip 152 is right**. The load-time speeds in `ZzzOpenData.cpp:382,408` (0.3) are overwritten by `SetAttackSpeed` at runtime (0.40).
- Other players see it through 0x1E (`MM/Network/Server/WSclient.cpp:5178-5183`): Fenrir -> 95, otherwise **152, even on a Dark Horse** (no horse branch). This is a client quirk.
- `PLAYER_SKILL_FLASH` is the Dark Wizard's Aqua Beam clip, shared.

### When the effects fire (frame = `AnimationFrame` in keys)
`RenderCharacter`, `MM/Engine/Object/ZzzCharacter.cpp:10512-10560` (103: `ZzzCharacter.cpp:12548-12603`, identical), on any of the three actions:

| Frame | Time at base speed (0.40) | What | Where |
|---|---|---|---|
| 1.2-1.6 | 0.12 s | `BITMAP_GATHERING` subtype 2 + **`SOUND_ELEC_STRIKE_READY`** (`sDarkElecSpikeReady.wav`) | on his hand (see below) |
| < 2.0 | 0-0.2 s, every frame | if he is in **your** party: from each other party member, 150 up, two `BITMAP_JOINT_ENERGY` joints (subtypes 12 and 13, scale 20) that fly to him | **from wherever the party stands, possibly behind him** |
| >= 5.5 | **0.55 s** | the bolt: `AttackStage` holds `AttackTime` at 10 until frame 5.5, then sets 15 (`MM/ZzzCharacter.cpp:2885-2895`; 103 `:3225-3235`), and at 15 the let-go runs (`:4140-4142` -> `:4391-4404`) | see below |
| 7-8 | 0.70-0.80 s | two `MODEL_DARKLORD_SKILL` subtype 2, light (0.8, 0.5, 1.0): one at angle (180, 45, 0), one at (0, 0, his yaw) | **at his right weapon's link bone** |

Speed note: with magic speed the clip runs faster; the frame numbers stay.

### The bolt (`AT_SKILL_THUNDER_STRIKE` let-go, `MM/ZzzCharacter.cpp:4391-4404`; 103 `:5166-5179`)
- Origin: `CalcAddPosition(o, 0, -90, -50)` = **90 units ahead** of him (MU's forward is local -Y: `MoveCharacterPosition` moves by `(0, -speed, 0)`, `MM/ZzzCharacter.cpp:6377`), 50 down; +80 up on a Dark Horse, +40 on Fenrir. Then `CreateEffect(BITMAP_FLARE_FORCE, pos, his angle, light, 0, owner)` and **`SOUND_ELEC_STRIKE`** (`sDarkElecSpike.wav`).
- `BITMAP_FLARE_FORCE` subtype 0 lives 0 ticks; it only spawns five joints (`MM/Render/Effects/ZzzEffect.cpp:3418-3429`; 103 `ZzzEffect.cpp:4120-4131`): subtype 1 (scale 100), **0 (scale 250, the main beam)**, 2, 3, 4 (scale 100).
- Joint create (`MM/Render/Effects/ZzzEffectJoint.cpp:2489-2575`): life 20 ticks, 30 tails max, `Position.z += 150` (so the beam runs ~100 units above his feet), light (1, 0.8, 1), velocity -3. Subtypes 1-4 use texture `BITMAP_JOINT_THUNDER`, start on a circle of radius 80 round the origin, and wait 2-4 ticks (subtypes 1, 2) or 0 (3, 4) before growing; subtypes 2 and 4 turn the other way. Subtype 0 is also `BITMAP_JOINT_THUNDER` (`:2570-2575`).
- Move (`ZzzEffectJoint.cpp:6521-6665`):
  - Subtype 0: every tick, `MultiUse-1` steps (MultiUse starts 1, +2 a tick), each moving `Velocity` along his facing and then `Velocity -= 2`. My integration: 0, 8, 40, 120, 272, 520 units over ticks 1-6, **~960 units = ~9.6 tiles**, reached about **6 ticks (0.24 s)** after the let-go.
  - Subtypes 1-4: the same accelerating run, each step also swinging ±20° about its own axis on a radius that shrinks 2.5 a step, so they **coil round the main beam** as it shoots forward; 3 and 4 are mirrored (`VectorSubtract`).
  - All: light ×(1/1.3) per tick once life < 10 (`:6660-6664`), so they fade over the last 0.4 s.
- Render (`ZzzEffectJoint.cpp:7127-7134, 7275-7281`): tail brightness ramps along the ribbon, `- Scroll` where Scroll = `(WorldTime % 1000)·0.001`, i.e. the texture flows along the beam once a second.
- **The whole bolt points forward** (his yaw): nothing of it goes behind him.

### The gather (`BITMAP_GATHERING` subtype 2)
Create `MM/Render/Effects/ZzzEffect.cpp:1295-1310`: life 20. Move `MM/Render/Effects/Behaviors/MoveHandlers.cpp:1604-1660`: rides **bone 33** of his body (which hand is not verified) every tick; up to 3 a tick: a `BITMAP_JOINT_THUNDER` subtype 3 joint from a random point 120 units out into the hand (every other tick), and a `BITMAP_SHINY+1` sprite of scale 1.6-3.0 at the hand. A ball of lightning gathering into the hand.

### The two rings at frames 7-8 (`MODEL_DARKLORD_SKILL` subtype 2)
- Model `Data/Skill/DarkLordSkill.bmd` (`MM/Engine/Object/ZzzOpenData.cpp:4320`): one quad 206 × 114 units wearing `KingS_R` (muextract export-obj). Rendered with `BodyLight = o->Light`, `RENDER_TEXTURE` (`MM/Engine/Object/ZzzObject.cpp:975-979`; 103 `ZzzObject.cpp:1066-1070`).
- Create (103 `ZzzEffect.cpp:4152-4178`, MM `ZzzEffect.cpp:3449-3475`): life 12, scale 0.2, velocity 0.4.
- Move (103 `ZzzEffect.cpp:14800-14807`): `Scale += Velocity; Velocity -= 0.06` a tick; BlendMeshLight /1.5 a tick once life < 8. So it swells to ~1.7× (about 360 units across) by tick 6 and settles at ~1.0 as it fades.
- **MuMain bug**: MuMain's registered `Move_MODEL_DARKLORD_SKILL` is empty (`MM/Render/Effects/Behaviors/MoveHandlers.cpp:5681-5685`) and the original scale code is left unreachable after a `break` (`MM/ZzzEffect.cpp:8045-8072`). In MuMain the quad never grows. **Use 1.03.35's.**

### Files (all confirmed in MMD)
| Thing | File |
|---|---|
| main beam, side arcs, gather bolts | `Effect/JointThunder01.OZJ` (`BITMAP_JOINT_THUNDER`, `ZzzOpenData.cpp:5286`) -- already ported, `content/effects/thunder/joint_thunder01.png` |
| `BITMAP_FLARE_FORCE` own texture | `Effect/NSkill.OZJ` (`ZzzOpenData.cpp:5319`); the subtype 0-4 joints swap it for JointThunder01 |
| party-energy joints | `Effect/JointLaser01.OZJ` (`BITMAP_JOINT_ENERGY`, `:5288`) |
| ring quad | `Skill/DarkLordSkill.bmd` + `Skill/KingS_R.OZJ` |
| gather sprites | `BITMAP_SHINY+1` (`Effect/Shiny02.OZJ`, already in `content/effects/summon/`) |
| sounds | `Sound/sDarkElecSpikeReady.wav` (1.96 s), `Sound/sDarkElecSpike.wav` (1.62 s) (`ZzzOpenData.cpp:4872-4873`). Neither is in our `content/sounds/sounds.json` yet |

## 4. 1.03.35 differences
- Effects, timings, colours and sounds are the same as MuMain: `103/ZzzCharacter.cpp:3225-3235` (frame 5.5), `:5166-5179` (bolt), `:12548-12603` (gather, party energy, rings), `103/ZzzEffect.cpp:4120-4131`.
- The only difference found: MODEL_DARKLORD_SKILL's grow/fade (`103/ZzzEffect.cpp:14791-14815`) runs in 1.03.35 and is dead code in MuMain (above).

## 5. Footage

Clips (Webzen's own channel "Mu Online", a modern client, a training dummy dragon; no party, so no energy wisps):
- `DwzNBzxfVtk` "Mu Online Dark Lord Skill Electric Spark", 2020-05-29, 720p, 21 s, ~8 casts.
- `pH0OX1K3kJw` same footage, 2019-05-30, 350p, 1.4 s.

One cast (`DwzNBzxfVtk`, from t = 0.1 s, 0.1 s steps):
- 0.1 s: weapon raised over his head, a white-blue glow at the hand (the gather).
- 0.2 s: arm comes forward; first purple-white streaks leave him toward the dragon.
- 0.3 s: a **purple-white lightning ribbon** runs from in front of him into the dragon, several tiles long, with thinner arcs.
- 0.4 s: the ribbon at its brightest and widest, curling arcs round it.
- 0.5-0.7 s: a **large pale blue-white crescent** (the frames 7-8 rings) flares at his weapon; in this view it sits **beside and slightly behind his body**, on the weapon side, while the beam fades ahead.
- 0.8-1.0 s: beam gone, a faint purple residue on the ground ahead; he returns to stance.
- No damage numbers are shown in this demo clip, so number-vs-bolt timing could not be read off the footage.

Stills: `electric-spike-footage.jpg` beside this file (0.15 s the gather, 0.40 s the beam, 0.60 s the crescent).

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

Reusable:
- `SkillRow` (`src/sim/skills.h:287-500`): `mana` (0) -- AG is not modelled, so his 100 AG has no column; `reach` 10; `damage` 250; `coolTicks` (5 s = 100 realm ticks, MU's own client delay); `clip` 152; `release` = **5.5/13** (as Force's 3/7 is key/keys); `sound`; `kin = Kin::DarkLord`; `anyHand = true`; `force`/`kLordForcePerEnergy` already give `(200 + Ene/20)%` for DL rows (`src/sim/skills.cpp:921-924, 1036`).
- `Spread::Line` (`skills.h:259-272`) and `Realm::looseLine`/`gather` (`src/sim/realm_fight.cpp:1675-1686`) are Power Wave's: half width 0.75 tiles, 12 tiles long, each victim flown at `flies`. Close to OpenMU's 1.5 × 12 frustum. WebZen's box is 3 wide (half width ~1.5), 10 long, plus the row behind him.
- `aimsAtPointer()` already true for Line: cast at the ground under the pointer, as MU's client does (`CheckTile` on the mouse tile).
- Party membership: `Body::party` (`src/sim/realm.h:735`), `partyInView` (`realm.h:1993`).
- Bolt drawing: `src/game/fx/thunder.cpp` already ribbons `BITMAP_JOINT_THUNDER` for the wizard's Lightning.

New:
- A per-row line **length and width** (today `kLineTiles`/`kLineHalfWidth` are globals for Power Wave), or WebZen's table itself.
- A **fixed 0.5 s delay for every victim** instead of a per-tile flight (`flies`), or a `flies` chosen so the far end lands with the drawn bolt (see §7).
- The flat `Command/10 + 50 × (party members within 10 tiles, himself included)` added to the skill band before the roll (`Realm::strike`/`strikeAt` take the generic DL Str/25 + Ene/50 today; Electric Spike must take this instead, not both).
- An AG pool does not exist; mana 0 means the row costs nothing unless we choose a cost.
- fx: the 5-joint bolt, the gather on bone 33, the KingS_R ring (1.03.35's grow), the party wisps. Two new sounds into `sounds.json` (needs the showing cook).

## 7. Decided (2026-10-24, from this research and the user's standing taste: MU's numbers, "only frontal", the hit with the visual)

1. **The box is WebZen's table**, all 36 directions, carried as 21 row bitmasks a direction (src/sim/electric_spike_box.h,
   made from `skillElect.hit` by pipeline/electric_spike_box.py) and read as `HitCheck` reads it -- the row behind him
   and the tiles beside him included: that is the rule, not a picture. Every enemy in it is struck; no cap.
2. **Each body struck when the bolt reaches it**: let go at frame 5.5 of PLAYER_SKILL_FLASH's 13 keys (clip 152,
   `release` 5.5/13), the bolt ~900 units out in 6 MU frames, so `flies` 40 tiles a second. WebZen strikes them all
   500 ms after the click, which at the base speed is about the let-go.
   **Changed 2026-10-24** ("the number comes with the visual"): each body is struck on the frame MU's tip passes it
   down his line -- made 90 units ahead, f(f-1) steps of strides 3, 5, 7... by frame f -- and the second frame at the
   soonest, so a body beside or behind him takes it with the bolt's first light (Realm's `sparkReaches`): 2 ticks
   at a tile, 4 at three, 5 at nine.
3. **Damage**: the band 250..375, plus Command/10 + 50 x (his party within 10 tiles, himself included; 0 outside a
   party) **in place of** Str/25 + Ene/50, then (200 + Ene/20)%.
4. **No party drain** (decision 6), and so **no wisps** from the party, which only draw it.
5. **The rings** (two DarkLordSkill at frames 7-8, colour 0.8, 0.5, 1.0) with 1.03.35's swell and fade, set at his
   weapon hand's height and slid ahead of him -- "only frontal" -- where MU's crescent flares beside and behind.
6. **Cooldown 5 s** (MU's own client delay; the server never checked it). **Mana 40** (ours: MU spends 0 mana and
   100 AG, and we have no AG).
7. **Learning**: level 92, energy 126, Command 340 (MU's), from the Scroll of Electric Spark at **12/24** (MU's number,
   free here) -- built with the other scrolls. The Dark Horse (85) and Fenrir (95) clips wait for the horse.
