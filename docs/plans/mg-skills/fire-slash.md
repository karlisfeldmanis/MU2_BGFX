# Fire Slash (skill 55, the Magic Gladiator's) -- research dossier

Researched 2026-10-24 before any build (the user: "lets start with 1 with proper reserach and perfect implementation
and finetuning"), on the pattern of the Dark Lord dossiers (branch darklord, docs/plans/dark-lord-spells/).
Section 1 is the server, section 2 the client, section 3 the footage, section 4 the fit to our code, section 5 what
is decided. Sources: WebZen GameServer 1.00.93 (github ptr0x-real/Mu-GS-Webzen-MC-10093, cloned for the research,
cp949), OpenMU `LEGACY/reference/openmu`, MuMain `LEGACY/reference/MuMain`, Main 1.03.35 (`third_party/`).

**In one paragraph.** Fire Slash is an Icarus-era skill (after 0.97; WebZen `AT_SKILL_DEFENSEDOWN`, Korean "Blood
Attack"), learned only from the Orb of Fire Slash (MU 12/16, drop level 60, 51,000 zen, strength 596, no energy).
Damage 80 (rolls 80..120 on top of the weapon), mana 15, AG 20, reach 2, physical, ×2 for the gladiator after defence.
Each body it hits has a **45% chance to lose 10% defence for 10 s** (re-hit refreshes, never stacks), rolled before
the blow so that blow already profits. It plays WHEEL (player action 65, Twisting Slash's clip). On frame 3 a
187° ribbon of fire 150 wide sweeps out at hip height round his front in 3 ticks and fades over 0.8 s, with flames and
blue lightning along it; the hand gathers lightning and sparks first; a debuffed body wears three tiny skulls over its
head while the cut lasts.

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93, fresh shallow clone; cp949, line numbers unchanged by iconv)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/`
- `OM/` = `LEGACY/reference/openmu/src/`

## 1. Name and version
- WebZen enum: `AT_SKILL_DEFENSEDOWN = 55`, Korean "블러드어택" (Blood Attack) (MG): WZC/include/public.h:229. The English data rows call it "Fire Slash": WZD/lang/kor/skill(Kor).txt:47.
- Gated by `NEW_SKILL_FORSKYLAND` ("new skills for 천공", the Icarus update, phase 1), defined unconditionally with no GAME_VERSION guard: WZC/include/define.h:3581. AG cost and the orb strength/agility checks come from `NEW_FORSKYLAND2` (Icarus phase 2): define.h:3604.
- OpenMU: not in Version075, Version095d or Version097d (no FireSlash/orb 16 there; 095d orbs only have Twisting Slash, OM/Persistence/Initialization/Version095d/Items/Orbs.cs:32; 097d only adds Jewels.cs). It first appears in VersionSeasonSix: OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:173.
- Conclusion: it is a post-0.97 (Icarus-era) MG skill, not part of 0.75/0.95/0.97.

## 1. How it is learned
- Orb, item group 12 index 16, "블러드어택구슬" (Orb of Fire Slash): WZD/lang/main/item.txt:351, row `16 -1 0 1 1 1 0 1 "..." 60 0 0 0 0 320 0 0 51000 | DW0 DK0 Elf0 MG1 DL0 Sum0 RF0`.
- Group-12 column order: Level(drop lvl)=60, Defense, Dur, ReqLevel=0, ReqEnergy=0, ReqStrength=320, ReqDex=0, ReqLeadership=0, BuyMoney=51000: WZC/Common/zzzitem.cpp:5265-5286.
- Orb index 16 maps to skill 55: WZ/MagicInf.cpp:177 (`case 16: AT_SKILL_DEFENSEDOWN`).
- Strength requirement is scaled: `20 + req*(ItemLevel + m_Level*3)*3/100` = 20 + 320*60*3/100 = **596 STR**: zzzitem.cpp:457.
- Use of the orb (CGUseItemRecv): orbs 12,8..12,24 go through this path (protocol.cpp:17776). STR+AddStrength < m_RequireStrength → refused (protocol.cpp:17795). Level check (ReqLevel 0 here): :17833. Class check through IsClass, then gObjMagicAdd: :17863.
- gObjMagicAdd checks energy against SkillGetRequireEnergy: user.cpp:28078-28108. The generic formula is `20 + rEnergy*rLevel*4/100` (MagicDamage.cpp:671/676), but 55 is overridden to **0 energy** (MagicDamage.cpp:705). No level requirement (MagicDamage.cpp:683-689 doesn't list 55).
- It is **not weapon-granted**: 55 is absent from the equipment-skill exclusion list (user.cpp:3741-3767), so it is saved as a learned skill. Price is 51000 zen (item.txt:351). Drop level 60.
- Monsters: Kundun's shades in Kalima get it as a monster skill (user.cpp:5161).

## 1. Skill row
- WZD/lang/main/skill.txt:61: `55 "블러드어택" lvl 0, dmg 80, mana 15, AG(어빌리티) 20, dist 2, delay 0, Eng 0, Cha 0, attr 1, attr2 -1, ... MG=1 ... 필요힘 0`. kor row is identical: skill(Kor).txt:47.
- Loader column order: number, name, reqLevel, damage, mana, bp(AG), dis, delay, energy, leadership, attr, type: MagicDamage.cpp:110-136. AG is stored as m_BrainPower: MagicDamage.cpp:652.
- Element: **attr 1 = poison** per the file legend (skill.txt:7: 0 ice, 1 poison, 2 lightning, 3 fire...). WebZen data does not mark it fire. attr2 -1 = neither physical nor magic.
- Damage roll of the skill part: `m_DamageMin = dmg; m_DamageMax = dmg + dmg/2`, giving 80..120: MagicInf.cpp:236-238.
- Physical, not wizardry: gObjCheckAttackTypeMagic returns FALSE for 55 (user.cpp:32732-32734).

## 1. The cast (WebZen live path)
- Packet 1, 0x1E CGDurationMagicRecv (protocol.cpp:16772) runs SpeedHackCheck and EnableSkill (55 whitelisted, ObjUseSkill.cpp:405), then UseSkill(x,y,dir,TargetPos) (ObjUseSkill.cpp:660):
  - mana is deducted (GetUseMana :500), AG through gObjMagicBPUse (user.cpp:27950, ObjUseSkill.cpp:713)
  - it sets UseMagicTime/UseMagicCount=0 (:716-719) and broadcasts 0x1E with x,y,dir (:723)
  - **No hit happens here**: the branch to SkillDefenseDown is under `NEW_ATTACK_PROC` (ObjUseSkill.cpp:866-871), which is commented out (WZ/define2.h:295, define.h:411).
- Packet 2, 0x1D/PACKET_MAGIC_ATTACK CGBeattackRecv (protocol.cpp:16156): the **client picks the targets**.
  - At most 5 targets: `if Count > 5, Count = 5` (:16175).
  - It must come within 8 s of the cast (:16292). At most 5 such packets per cast (`UseMagicCount > 4` returns, :16309/:16314).
  - The same target twice in one packet means a kick for "Multi Attack" (:16691-16701).
  - Per target (:16710-16725): if the caster is a player and `Strength+AddStrength >= 596`, it calls `MaGumSkillDefenseDown(...)` and then `gObjAttack(lpObj, target, lpMagic, 1, 1)`. If the STR check fails, the hit is silently skipped (fix MODIFY_BLOOD_ATTACK_AND_ADD_STRENGTH_BUGFIX_20080701, define.h:1390, about unequipping set gear after learning). Monsters skip the STR check.
  - **There is no distance, shape or facing check on the server** for 55 in this path, and gObjAttack has no distance check (no Distance/GetSkillDistance use in ObjAttack.cpp). Reach 2 is only the client's targeting value.
  - Players can be hit if the client lists them; the guild/party exclusion is commented out (ObjAttack.cpp:206-232). gObjAttack's normal PK rules apply.
- Hits: 1 hit per listed target per packet. No delays or deferred hits.
- Dead server-side shape (NEW_ATTACK_PROC, for reference) SkillDefenseDown at ObjUseSkill.cpp:2543-2594:
  - a line path of 4 tiles toward TargetPos (GetTargetLinePath, :2558)
  - targets with `gObjCalDistance < 3` (:2572) and within ±2 tiles of any path cell (:2577-2578), so roughly a 5-wide band, 2 tiles deep
  - note the precedence bug `X+(8-TargetPos&0xF0)` (:2551)
- Single-target RunningSkill path (0x19, monsters): it only applies the debuff, with no damage (ObjUseSkill.cpp:1174-1177).

## 1. Defence-reduction debuff
- MaGumSkillDefenseDown (ObjUseSkill.cpp:1327-1358):
  - player casters must be MG (:1336-1339)
  - **45% chance**: retCalcSkillResistance(target, TRUE) is `rand()%100 < 45` (user.cpp:8717-8722, call at :1347)
  - on success: `gObjAddBuffEffect(target, BUFFTYPE_DEFENSE_POWER_DEC, EFFECTTYPE_DECREASE_DEFENSE, 10, 0, 0, 10)`, i.e. **value 10, duration 10** (:1350)
- The amount is a percent: `targetdefense -= targetdefense * Σ DECREASE_DEFENSE / 100` (ObjBaseAttack.cpp:1299-1302, also Weapon.cpp:213-219). So it is **−10% defence for 10 s**, on monsters and players alike.
- Duration ticks once a second: gObjSecondProc → gObjCheckBuffEffectList (user.cpp:22556/22610, 1000 ms timer GameServer.cpp:541). EffectDuration-- (BuffEffectSlot.cpp:431-435).
- Stacking: SetEffect removes any existing buff of the same BuffEffectType and inserts the new one. So a re-hit **refreshes to 10 s and does not stack** (BuffEffectSlot.cpp:80-95). The Master-tree version applies the same buff/values (MasterSkillSystem.cpp:1945-1963), so it also just refreshes.
- Order: the debuff roll comes **before** gObjAttack, so a debuff that lands already lowers that same hit's defence (protocol.cpp:16718-16719).
- Sent to clients:
  - GCMagicAttackNumberSend(caster, 55, target, 1) only when the debuff lands (ObjUseSkill.cpp:1356)
  - GCUseBuffEffect(target, BUFFTYPE_DEFENSE_POWER_DEC, add, EffectType1, Duration) (BuffEffectSlot.cpp:540-551), and a remove message when it is replaced
  - Old pre-buff system: STATE_REDUCE_DEFENSE view bit plus GCMagicCancelSend(55) on expiry (user.cpp:25397-25404; ObjUseSkill.cpp:1352-1354)

## 1. Damage path (WebZen)
- In gObjAttack, 55 takes the physical (weapon) branch (ObjAttack.cpp:718-735). targetdefense = GetTargetDefense (with the −10%) (:678). Then GetAttackDamage(..., lpMagic) (:761).
- GetAttackDamage (ObjAttack.cpp:3074+):
  - skill min/max (80/120) is added to the weapon's min/max (:3152-3160)
  - plus SetOpAddSkillAttack (:3163)
  - plus `m_AddResistance[attr]` for its attr (:3169-3187)
  - plus Jewel of Harmony skill attack
  - roll `min + rand()%(max-min+1)` per hand (:3329-3332)
  - critical/excellent use max (:3361+)
  - `ad = dmg - targetDefense` (:3414/:3454)
- Skill multiplier: case AT_SKILL_DEFENSEDOWN, MG or DL → `AttackDamage*2`; other classes get `*(200+Energy/10)/100` (ObjAttack.cpp:1393-1408). The Master-tree version is also ×2 (:1884-1885). So it is **×2 after defence subtraction**, with no energy term for the MG.
- Weapon durability drops on a hit (:779-782).

## 1. OpenMU (S6) and the differences from WebZen
- `CreateSkill(FireSlash, "Fire Slash", AllMGs, Physical, damage 80, distance 2, abilityConsumption(AG) 20, manaConsumption 15, elemental Fire, AreaSkillAutomaticHits)` (SkillsInitializer.cs:173). The parameter order (damage, distance, ability, mana, level, energy) is at OM/Persistence/Initialization/Skills/SkillsInitializerBase.cs:51-71. It has **no energy or level requirement**. (The brief's "mana 20, 15 energy" was wrong: it is AG 20 and mana 15.)
- `AddAreaSkillSettings(FireSlash, useFrustum=true, startWidth 1.5, endWidth 2, distance 2)` (:174, signature at SkillsInitializerBase.cs:132-150). Everything else is default: 1 hit per target, no hit cap, no delays.
- Frustum: a trapezoid in front of the caster, from 0.99 tiles (half-width ±1.5) to 2 tiles (half-width ±2) along the rotation byte (FrustumBasedTargetFilter.cs:220-229). Candidates are attackables within Range 2 of the client's targetAreaCenter (AreaSkillAttackAction.cs:131-133). The explicit target counts if within Range+2 (:109-115). Players are hit only if `AreaSkillHitsPlayer` (:150-153). Every target is hit once, immediately (:207-219).
- Orb: `CreateOrb(16, FireSlash, 1, "Orb of Fire Slash", drop 60, lvl 0, ene 0, **str 320**, agi 0, lead 0, 51000 zen, MG)` (Items/Orbs.cs:46). Orbs are not wearable, so the requirement is the raw 320 (OM/GameLogic/ItemExtensions.cs:315-318).
- Effect: DefenseReduction, 10 s, multiply 0.9 on DefenseDecrement (Skills/DefenseReductionEffectInitializer.cs:32-45). Later updates split it into DefensePvm/DefensePvp ×0.9 (Updates/FixDefenseCalcsPlugInSeason6.cs:65-83). The effect is linked at SkillsInitializer.cs:65/724-728.
- How OpenMU applies it: it rolls damage first, then TryApplyElementalEffectsAsync (AreaSkillAttackAction.cs:387-388). The chance is `1/(FireResistance+1)`, which is 100% at 0 resistance. It is **not re-applied while active** (AttackableExtensions.cs:415-430).
- Damage: skill dmg..dmg+dmg/2 (AttackableExtensions.cs:771-773), with MG SkillMultiplier 2 (CharacterClasses/ClassMagicGladiator.cs:123, applied at AttackableExtensions.cs:226-228).
- **Differences** (WebZen vs OpenMU):
  1. Targets: client-chosen, ≤5 per packet, no range check vs a server-chosen frustum with no cap.
  2. Debuff chance: 45% flat vs 1/(fireRes+1).
  3. Debuff timing: before the hit (that hit benefits) vs after the hit.
  4. Re-hit: refreshes to 10 s vs ignored while active.
  5. Learn STR: 596 (scaled) vs 320 (raw).
  6. Cast STR: re-checked ≥596 on every hit vs no check.
  7. Element: poison (attr 1) in the data vs fire. OpenMU's own Fire Slash Mastery uses Poison (SkillsInitializer.cs:537).
  8. Same in both: 80 dmg, mana 15, AG 20, reach 2, ×2 multiplier, −10%/10 s.

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data, **103** = MU_Chs_1.03.35/source.
Ticks are at 25 fps, so FPS_ANIMATION_FACTOR = 1 and every `rand_fps_check(1)` passes.

## 2.0. Identity
- MM: `AT_SKILL_FIRE_SLASH = 55` (MM Core/Globals/_enum.h:349). The master "STR" version is `AT_SKILL_FIRE_SLASH_STR = 490` (_enum.h:573), and it maps back to 55 (_enum.h:674).
- 103: the same number is called `AT_SKILL_REDUCEDEFENSE = 55` (103 _enum.h:1926). Its master versions are `AT_SKILL_BLOOD_ATT_UP..+4`, and every switch shares one case with them (e.g. 103 ZzzCharacter.cpp:3080-3086).
- To cast it the UI asks for Str+AddStr ≥ 596, a right-hand weapon, and no staff in either hand (MM UI/NewUI/HUD/NewUIMainFrameWindow.cpp:2211-2225). The same 596 check is in MM GameLogic/Combat/ClassAttack.cpp:633-638.
- Kalima Death Centurions also cast 55. They use MONSTER01_ATTACK2 (MM World/GameMaps/GMHellas.cpp:973-976), and their effect is a separate `CreateMonsterSkill_ReduceDef(o, AttackTime, 13, 200.f)` (GMHellas.cpp:1106-1108). That is not the player look and is not covered here.

## 2.1. Cast (local client)
- The MG is CLASS_DARK, so a right-click goes to `AttackKnight` (MM GameLogic/Combat/SkillExecution.cpp:407-409).
- `AttackKnight` first checks the weapon: a right-hand weapon that is not a staff (ClassAttack.cpp:484-489). It also checks mana, AG, cooldown and energy (ClassAttack.cpp:565-590).
- Fire Slash branch (ClassAttack.cpp:630-656):
  - The caster turns to `c->TargetPosition` (`CreateAngle2D`).
  - It checks Str ≥ 596 and that the target is in range (`CheckTile`).
  - It sends **0x1E MagicContinue** `(skill, X, Y, angle byte = yaw/360*256, byValue = GetDestValue(target tile), 0, TKey)`.
  - It calls `SetAttackSpeed()`, then `SetAction(o, PLAYER_ATTACK_SKILL_WHEEL)` locally and stops movement.
  - So the animation starts on the click and the request goes out in the same tick. No effect spawns here and no sound plays.
- When the target is out of range the hero walks (MOVEMENT_SKILL). On arrival MM Engine/Object/ZzzInterface.cpp:1457-1465 calls `AttackKnight` again.
- **MM-only second path.** `SkillWarrior` lists Fire Slash (MM GameLogic/Combat/SkillCast.cpp:127-128). It is reached from SkillExecution.cpp:366-372 only when the hero is not mounted and a target is under the cursor (`CheckAttack()`). From there `CastWarriorSkill` calls `UseSkillWarrior`, which:
  - plays WHEEL (SkillCast.cpp:310-312);
  - spawns particle `BITMAP_SHINY+2` sub 0 at the hand and plays `SOUND_BRANDISH_SWORD01+rand%2` (SkillCast.cpp:357-375);
  - sends MagicContinue with byValue = 0 (SkillCast.cpp:389-399);
  - sends an instant-move request, compiled only under `#ifdef SEND_POSITION_TO_SERVER` (SkillCast.cpp:442-463).
  - 1.03.35 has none of this: in 103, REDUCEDEFENSE appears only in AttackKnight/Attack (103 ZzzInterface.cpp:4268, 6616, 9378).
- `AttackTime` is **not** started by the local cast. Only the server echo starts it (see below), so on the caster's own screen the effects wait for the round trip.

## 2.2. Server echo
- **0x1E `ReceiveMagicContinue`** (MM Network/Server/WSclient.cpp:5079-5489):
  - It sets `sc->Skill`, and sets the yaw from `Angle/255*360` (5094).
  - For every other player, Fire Slash plays `SetAction(so, PLAYER_ATTACK_SKILL_WHEEL)` (5206-5225) and resets `AnimationFrame = 0` (5472). The hero's own frame is not reset.
  - Then for everyone: `sc->AttackTime = 1`, and `SkillX/Y` come from the packet (5481-5485). No sound plays here.
  - The riding variants are behind `#ifdef YDG_ADD_SKILL_RIDING_ANIMATIONS`, which is defined nowhere. They are dead code: the WHEEL_UNI/DINO/FENRIR enum entries are #ifdef'd out too (_enum.h:3100-3107). So the MG plays the **on-foot WHEEL clip even when mounted**. 103 is the same: the define is commented out at 103 Winmain_New.h:60.
- **Player action:** `PLAYER_ATTACK_SKILL_WHEEL` = **index 65** (_enum.h:2971, counted from PLAYER_SET = 0). SWORD1..5 are 60..64.
  - I decrypted D/Player/player.bmd: action 65 has **13 keys** with no lock positions, the bones and keys are present, and the file has 284 actions.
- **0x19 `ReceiveMagic`**, one per hit target (WSclient.cpp:4364-4381). If `SkillSuccess`:
  - `DeleteEffect(BITMAP_SKULL,to,0)`;
  - `CreateEffect(BITMAP_SKULL, to->Position, to->Angle, (1,1,1), 0, to)`;
  - `PlayBuffer(SOUND_BLOODATTACK, to)`, positional on the target;
  - `g_CharacterRegisterBuff(to, eDeBuff_Defense)`.
  - A monster caster instead gets `SetPlayerAttack`, AnimationFrame = 0 and AttackTime = 1.
- `ReceiveMagicFinish` → `UnRegisterBuff(eDeBuff_Defense)` (WSclient.cpp:3851-3853) → `ClearBuffPhysicalEffect` deletes skull sub 0 (WSclient.cpp:15431-15434). The buff can also come from a state packet: `RegisterBuff` → `InsertBuffPhysicalEffect` makes the same skull plus SOUND_BLOODATTACK (WSclient.cpp:15201-15208, 15562-15565).

## 2.3. Timing
- `AttackStage` runs every tick while `(int)AttackTime > 0`, then `AttackTime += 1`. At `g_iLimitAttackTime = 15` it resets to 0 (MM ZzzCharacter.cpp:4133-4143). Fire Slash sets the limit to 15 (ZzzCharacter.cpp:2817).
- Fire Slash in `AttackStage` (MM ZzzCharacter.cpp:2783-2818):
  - It calls `SetAction(o, WHEEL)` every tick.
  - At AttackTime 1 **and** 2 it spawns `BITMAP_GATHERING` sub 1: two instances, one tick apart. `CheckAttackTime(n)` is true once per value (MM Engine/Object/w_CharacterInfo.h:247-254).
  - On the first tick with `AnimationFrame >= 3`:
    - PKKey is set from SelectedCharacter. That is the *local* selection, even for other casters.
    - `PlayBuffer(SOUND_SKILL_SWORD3)` plays as 2D sound.
    - `CreateEffect(BITMAP_SWORD_FORCE, o->Position, o->Angle, o->Light, sub, o, PKKey, FindHotKey(skill))`, with sub 0 for normal and sub 1 for STR.
    - WHEEL PlaySpeed is set to `0.54 + AttackSpeed*0.004`. This is global on Models[MODEL_PLAYER] and uses the *hero's* attack speed.
    - `AttackTime = 15`, which ends the stage.
- PlaySpeed is `0.24 + AttackSpeed*0.004` in `SetAttackSpeed` (MM ZzzCharacter.cpp:813-816, 924). 103 is the same (103 ZzzCharacter.cpp:1020, 3134).
- **Ticks with no attack speed:**
  - Frame 3 is reached at 3/0.24 = 12.5, so on **tick 13 (~0.52 s)** after the animation starts.
  - The rest of the clip, (12 − 3.12)/0.54 ≈ **16.4 ticks**, so the clip lasts **~30 ticks (1.2 s)**. Without the speed-up it would be 12/0.24 = 50 ticks.
- The window is tight. For observers, AttackTime and the clip both start at the echo, so the arc fires at AttackTime ≈ 13 < 15. For the hero the clip started at the click, so with high ping the arc fires on the first echoed tick. If AttackTime reaches 15 before frame 3 (high attack speed does not cause this, a slow PlaySpeed would), no arc spawns.

## 2.4. What spawns (all additive: `EnableAlphaBlend` = `glBlendFunc(GL_ONE,GL_ONE)`, no depth write, no culling; MM Render/Textures/ZzzOpenglUtil.cpp:395-406)

### A. BITMAP_GATHERING sub 1 ×2: the charge at the hand
- Create (MM Render/Effects/ZzzEffect.cpp:1295-1310): LifeTime 20, StartPosition = cast position.
- Move (MM Render/Effects/Behaviors/MoveHandlers.cpp:1603-1647), every tick:
  - The position is caster bone 33 (the right-hand weapon link bone; MM ZzzCharacter.cpp:12071) plus local (0,0,10).
  - It loops 3 times. Each time it picks a random point on a sphere of r = 120 around the hand.
  - On even LifeTime: `CreateJoint(BITMAP_JOINT_THUNDER, pt→hand, sub 3, Scale 10, LifeTime 10, Velocity 10, MaxTails 10)`.
  - On odd LifeTime: particle `BITMAP_SPARK+1` sub 2 at the point, scale 0.10–0.59.
  - It also draws a sprite `BITMAP_SHINY+1` at the hand: scale (8..15)*0.2 = 1.6–3.0, random roll, light = the caster's o->Light.
- Each instance makes 10 thunder ticks and 10 spark ticks. The two overlap, and together they last until about tick 21, past the swing.
- SPARK+1 sub 2 (MM Render/Effects/ZzzEffectParticle.cpp:2127-2133 create, 6619-6630 move):
  - Life 15. Velocity = (hand − pt)/10, so it reaches the hand in 10 ticks.
  - Scale +0.05 per tick. Light = LifeTime/5, which starts at 3.0, so it is overbright.
  - Default sprite render (ZzzEffectParticle.cpp:9274-9276).
- A sprite's size = texture pixels × Scale × AnimationFrame (AnimationFrame runs 1→0.2..1) (MM Render/Effects/zzzeffectsprite.cpp:70-123). Sprite sub 0 is additive (zzzeffectsprite.cpp:162-165).

### B. BITMAP_SWORD_FORCE: a carrier with no render of its own
- Create (ZzzEffect.cpp:2959-2964): LifeTime 30, Light 0.8, `HeadAngle = (0,0, yaw+45)`.
- For a hero owner it goes to the g_SkillEffects pool (MM GameLogic/Skills/SkillEffectMgr.cpp:44-48; ZzzEffect.cpp:340-343).
- Move (MoveHandlers.cpp:4993-5016), only while LifeTime == 30 (its first move): it creates `BITMAP_JOINT_FORCE` at caster position + (0,0,100), angle HeadAngle, Target = caster, Scale **150**, with PKKey/Skill passed through. It uses sub **0** for normal and sub **10** for STR, which also passes Light (1,1,1).
- `case BITMAP_SWORD_FORCE` in the particle code (ZzzEffectParticle.cpp:2808-2811, 7476-7480) is **dead**: nothing calls CreateParticle with that type.

### C. BITMAP_JOINT_FORCE sub 0/10: the fire arc, the main visual
- Create (MM Render/Effects/ZzzEffectJoint.cpp:2277-2295):
  - LifeTime **20**, MaxTails **18**, NumTails −1, no automatic tails, Velocity 0.
  - TargetPosition = the pivot (caster + 100 z). It is fixed at spawn and does not follow the caster.
  - `Angle[2] += 30`, so yaw is +75 from facing. Direction = Angle.
  - Default RenderType ALPHA_BLEND, RenderFace ONE|TWO, Light (1,1,1) (ZzzEffectJoint.cpp:70-105).
- Move (ZzzEffectJoint.cpp:6233-6365). Each tick runs 8 steps while NumTails < 17:
  - pos = pivot + Rotate((0,−145,0), Direction), so the radius is **145**;
  - `CreateTail` with the fixed Angle matrix;
  - `Direction[2] −= 11°`;
  - 50% chance of particle `BITMAP_FIRE` sub 0 at pos;
  - two `BITMAP_JOINT_THUNDER` sub 3 from the previous pos to pos: Scale rand%10+5 and rand%8+4, LifeTime 5, Velocity/MaxTails 10.
- The arc sweeps **+75° → −112° (187°)** clockwise in front of the caster. It draws 8+8+2 tails, so the whole arc appears in **3 ticks (0.12 s)** and then fades out until tick 20 (0.8 s).
- Light per tick: `LifeTime/30` while LifeTime ≥ 10 (0.67→0.33), then `/1.5` per tick.
- Tail shape (MM ZzzEffectJoint.cpp:2848-2952): each tail is a cross of ±75 along the joint's local X and Z. The axes stay fixed and do not follow the arc tangent, so the ribbon thins wherever the tangent lines up with local X.
- Render (ZzzEffectJoint.cpp:6973-7160; 7144-7152 for sub 0):
  - **Sub 0** draws only the flat band (verts 0/1). Colour = `((18−j)/18)*2 * Light[0]`, so the head is about 1.33 on the first tick.
  - U = (NumTails−j)/17, so the head is at U = 1. V runs 0..1 across the band.
  - **Sub 10** (STR) takes the generic branch: both faces (a cross section), colour = o->Light (ZzzEffectJoint.cpp:7277-7316).
- Texture `Effect\motion_blur_r2.jpg` → D/Effect/motion_blur_r2.OZJ, **64×128**, GL_REPEAT (MM Engine/Object/ZzzOpenData.cpp:5295). It is an orange-red flame gradient, white-hot at U = 1.

### D. Children of the arc
- **BITMAP_FIRE sub 0** (create ZzzEffectParticle.cpp:385-396; move 4537-4549; render 9078-9093):
  - Life 24, scale 1.28–1.91, Velocity (0, −3.2..−4.7, 0) along the joint angle, so every flame drifts the same way.
  - Gravity +0.004 per tick, z += Gravity*10, scale −0.04 per tick, Luminosity = LifeTime/24.
  - Frame = (23−LifeTime)/6 over a 4-frame strip.
  - Texture `Effect\Fire01.jpg` → **256×64** (4 cells of 64×64), drawn 64×64 × scale (ZzzOpenData.cpp:5179).
  - About 9 flames per cast.
- **BITMAP_JOINT_THUNDER sub 3** (create ZzzEffectJoint.cpp:1114-1121; move 4727-4960):
  - Light (0.5, 0.5, 1) blue. It homes on TargetPosition (`MoveHumming`, turn 50) and steps by Velocity per tail.
  - Each step has random direction jitter of ±512/Scale°. It dies when farther than 150.
  - Render U = (…)*2 − scroll by WorldTime (ZzzEffectJoint.cpp:7118-7124), both faces.
  - Texture `Effect\JointThunder01.jpg` **256×32** (ZzzOpenData.cpp:5286). About 36 bolts come from the arc and about 30 from the gatherings.

### E. Sprites
- `BITMAP_SHINY+1` = `Effect\Shiny02.jpg` **32×64**. `BITMAP_SPARK+1` = `Effect\Spark03.jpg` **32×32** (ZzzOpenData.cpp:5263, 5204).
- On the MM-only path, `BITMAP_SHINY+2` = `Effect\Shiny03.jpg` **128×16** (ZzzOpenData.cpp:5264):
  - Life 18, at the hand bone + (0,−120,0) (ZzzEffectParticle.cpp:25-41, 2688-2691).
  - Scale = sin(LifeTime·10°)·3, peaking at 3.0, so it is a streak of up to 384×48 (ZzzEffectParticle.cpp:7320-7323).

### F. On hit monsters: the defense-down skull halo
- BITMAP_SKULL sub 0 (create ZzzEffect.cpp:618-620: LifeTime 1000; move MoveHandlers.cpp:583-634):
  - While `eDeBuff_Defense` is set, LifeTime is pinned to 10, which makes the effect permanent.
  - It is hidden while the target has Cloaking.
  - It draws 3 `BITMAP_SKULL` sprites at scale 1.0 every tick:
    - angle = i·120° + 10·0.17 rad. This is constant because LifeTime is pinned, so the skulls do not orbit.
    - radius = 50 + 20·sin(i·15.37 + WorldTime·0.0031), so each one breathes in and out over about 2 s.
    - z = target feet + 200·target Scale.
  - Texture `Skill\Skull.jpg` → D/Skill/Skull.OZJ, **16×16**, GL_NEAREST, drawn additive at 16 units (tiny) (ZzzOpenData.cpp:5294).
- No light is added for this skill anywhere: none of these handlers calls `AddTerrainLight`.

## 2.5. Weapon dependence
- `AttackEffect` sets `o->Weapon/WeaponLevel` from `c->Weapon[0]` (MM ZzzCharacter.cpp:4181-4185; 103 4934-4937). Nothing in the Fire Slash chain reads them, so that is **dead** for visuals.
- The joint's own `Weapon` (the Serial argument of AttackCharacterRange) is never set in CreateJoint for sub 0/10, so it is stale.
- **No weapon trail.** WHEEL (65) is in no `BlurType` branch (MM ZzzCharacter.cpp:3783-3864). The sword branch only covers SWORD_RIGHT1..TWO_HAND_SWORD3 and TWO_HAND_SWORD_TWO (3830-3835). The SKILL_SWORD1..5 branches do not include 65.
- The look is the same for every blade (MG swords included). Nothing is drawn on the sword. The only hand-anchored visual is the GATHERING spark at bone 33.

## 2.6. MuMain vs 1.03.35
- **Hit detection:**
  - 103 does it on the client. While LifeTime > 18 (the first 2 ticks), at j = 0 and j = 5, the arc calls `AttackCharacterRange(skill, pos, 150, …)`, or 225 for sub 10 (103 ZzzEffectJoint.cpp:6890-6900, 6809-6823).
  - So there are 4 probe points: tails 0, 5, 8 and 13 (+75°, +20°, −13°, −68°). Each sends `SendRequestMagicAttack` with up to 5 keys, monsters only, or players matching PKKey (103 ZzzEffect.cpp:123-241).
  - MM stubs `AttackCharacterRange` to `return false` (MM ZzzEffect.cpp:62-65), so all of the joint's attack code is **dead**: the server computes the area from MagicContinue (angle, byValue).
  - `SV_RANGE_ATTACK_CHECK` is defined in neither client.
- **STR:**
  - 103 uses JOINT sub 10 only for `AT_SKILL_BLOOD_ATT_UP+4` (103 ZzzCharacter.cpp:3121-3130, under PJH_FIX_BLOOD_ATTCK, which is defined at Winmain_New.h:1810). Ranks +0..+3 use sub 0.
  - MM uses sub 10 for FIRE_SLASH_STR (ZzzCharacter.cpp:2803-2810).
- **FPS gating:**
  - MM adds `rand_fps_check(1)` to the spawn (ZzzCharacter.cpp:2797) and uses `CreateJointFpsChecked` for the arc spawn, which happens only on the LifeTime == 30 tick. Above 25 fps the arc can be skipped at random.
  - The render uses `*= powf(Light[0],FPS)` where 103 multiplies by `Light[0]` (MM ZzzEffectJoint.cpp:7146; 103 7732). Fire uses `rand_fps_check(2)` where 103 uses `rand()%2`.
  - In the battle-castle no-attack zone, MM does `LifeTime *= powf(1/5,FPS)` and 103 does `/= 5`.
- **Render guards are MM only:** `IsValidTailQuad` and the skip of segments longer than 60 (MM ZzzEffectJoint.cpp:7083, 7097). Arc segments are about 27.8 long, so they are unaffected.
- **Strength 596 check:** in 103 it sits under `PSW_FRUIT_ITEM_CHECK_SKILL`, which is defined only in Winmain_New_Live.h:93/354 (locale blocks) (103 ZzzInterface.cpp:6619-6626). MM checks it always.
- **Unchanged constants:** GATHERING (103 ZzzEffect.cpp:1753-1771, 9588-9643), SWORD_FORCE (103 3590-3595, 13923-13942), JOINT_FORCE create (103 ZzzEffectJoint.cpp:2349-2371), SKULL (103 ZzzEffect.cpp:8374-8420), the 0x19/0x1E echoes (103 WSclient.cpp:5371-5390, 6317) and PlaySpeeds 0.24/0.54 are all identical.
- 103 ran GATHERING while `AttackTime` was in 1..2 (103 ZzzCharacter.cpp:3106-3111). That gives the same count of 2.

## 2.7. Assets (under D)
| What | File | Size | Exists |
|---|---|---|---|
| arc ribbon (BITMAP_JOINT_FORCE) | Effect/motion_blur_r2.OZJ | 64×128 | yes |
| lightning (BITMAP_JOINT_THUNDER) | Effect/JointThunder01.OZJ | 256×32 | yes |
| flames (BITMAP_FIRE) | Effect/Fire01.OZJ | 256×64 (4 frames) | yes |
| hand glow (BITMAP_SHINY+1) | Effect/Shiny02.OZJ | 32×64 | yes |
| sparks (BITMAP_SPARK+1) | Effect/Spark03.OZJ | 32×32 | yes |
| MM-only cast streak (SHINY+2) | Effect/Shiny03.OZJ | 128×16 | yes |
| skull halo (BITMAP_SKULL) | Skill/Skull.OZJ | 16×16 | yes |
| swing (SOUND_SKILL_SWORD3 = 114) | Sound/sKnightSkill3.wav | — | yes (ZzzOpenData.cpp:4787) |
| hit/debuff (SOUND_BLOODATTACK = 133) | Sound/eBloodAttack.wav | — | yes (ZzzOpenData.cpp:4833) |
| MM-only cast (BRANDISH 60/61) | Sound/eSwingWeapon1.wav, eSwingWeapon2.wav | — | yes (ZzzOpenData.cpp:4758-4759) |
| clip | Player/player.bmd action 65 (13 keys) | — | yes |
- No model (.bmd) effect is involved. Skill/skull.bmd exists but the skull is a sprite, not that model.
- OZJ = a 24-byte header followed by a JPEG. All JPEGs are 3-component, so particles draw additive (ZzzEffectParticle.cpp:8922-8931).

# 3. Footage

InfinityMU wiki "Mg-fire-slash-2.gif" (mirrored as MU2/docs/reference/skill-clips/magic-gladiator/fire-slash.mp4,
300×230, 25 fps, 4 s; Season-era client, a heavily geared MG seen from behind). Still strip
`fire-slash-footage.jpg`: frames 22, 24, 26, 30 / 36, 42, 46, 49.

| frame (from the swing's start, ~f20) | what shows |
|---|---|
| +2 | sword drawn back at his right; violet-white lightning crackling at his left and right (the gathering) |
| +4 | the ribbon is out: a fire crescent at hip height from behind his left round his front to his right, white-hot inner edge, orange outer; violet lightning running along its right half |
| +6 .. +16 | the crescent holds, full width about 2.5 body-widths each side; flames lick upward off it; lightning fades by about +14 |
| +16 .. +29 | it fades from the right end first; the left part (where the sweep began) lingers as a soft orange arc; gone by about +30 (1.2 s) |

The footage agrees with the client trace: 187° asymmetric sweep, fixed pivot, additive fire band, blue/violet joint
thunder, ~0.8 s life plus fade. No sword trail. Numbers on the target are "Miss" in the clip (it was swung at a
standing dummy-like monster), so the skull halo does not show.

# 4. Fit to our code (main, 2026-10-24)

- **Row**: `SkillRow` src/sim/skills.h:280-498, table `kRows` src/sim/skills.cpp:42-904, new rows go on the end. A row
  with `kin = Kin::MagicGladiator` passes `skillFor` (skills.cpp:1066-1078) unchanged. `force()` (skills.cpp:1024)
  gives `kKnightSkillBoost` only to `Kin::DarkKnight` rows.
- **kSkills** 40 → 41 (skills.h:511; u64 learned mask, static_assert ≤ 64). It moves `Body::cools[]`,
  `HeroRecord::coolsLeft` (realm.h:793), play.h's `lastVolleyTick_` and `heard_.skill`, and the kept save: wire.cpp:229-231
  reads `layout >= 9 ? kSkills : 39` cools, so a **kKeptLayout 16** (wire.h:192, layouts 9-15 read as 40) and a kVersion
  bump. tests/sim_test.cpp:2916 pins kSkills 40 and Alquamos last.
- **Clash with branch darklord**: it appends Force, Fire Burst, Critical Damage, Electric Spike (kSkills 44) and also took
  kKeptLayout 15 for its cools, which main has since used for the wide vault. Fire Slash on main takes row 40 and
  layout 16; darklord moves its rows up one and takes layout 17 when it merges (nothing of it is deployed, so only its
  test saves see the shift).
- **For darklord's merge** (2026-10-24, with Fire Slash and Power Slash on main): its four rows go on after Power
  Slash's, at indices 42-45 where its own tests and saves had them at 40-43, and kSkills becomes 46; its cools take
  kKeptLayout **17** (main's 16 reads 42 cools, layouts 9-15 read 40), with wire.cpp's `cools` ladder one rung longer
  and its kVersion past main's. Its test saves (layout 15 with 44 cools) do not read after the merge; nothing of it is
  deployed. Its orbs keep 12/23, 24 and 26; Fire Slash's orb is 12/27.
- **And since 2026-10-25, Swell Life on main** (the knight's, row 42, skill 48, the Orb of Greater Fortitude at
  12/29): darklord's four rows go on at 43-46, kSkills 47, and its cools take kKeptLayout **18** (main's 17 reads 43
  cools, 16 reads 42), its kVersion past main's 52.
- **Area**: `Spread::Arc` is ±`kArcHalfAngle` 67.5° (skills.h:555), one constant. `Realm::gather`
  (realm_skills.cpp:677-746, Arc test :719-725) skips safe tiles and walls. `strikeAround` (:748-785) strikes each with
  `sweep_ = &row`; element at :765 via `skillElement` (realm_tuning.h:501-518).
- **Release**: `Realm::begin` (realm_fight.cpp:1198-1216) puts the blow at `row.release` × the clip.
- **Debuff**: none lowers defence yet. Weakness (`weakUntil`/`weakCut`, realm.h:676-679; laid realm_fight.cpp:477-495,
  read :204-206) is the pattern: an `Until > tick_` field, cleared in `raiseBeast` (:2533, :2566-2572), bosses shrug via
  `shrugs()` (realm_raid.cpp:372). Defence enters at rules.cpp:67-68 (`strike`), the target's stats copied at
  realm_fight.cpp:48-50. Body is in the newcomer snapshot (realm_snapshot.cpp; bump `kShape`). Drawing: play_ribbons.cpp
  :94-108 (Weakness), target frame boons src/game/ui/target_frame.cpp:70-90.
- **Client**: `Swung` play.cpp:1640-1890 (clip, pace, sound `heard_.skill`, per-skill hooks like `deathStab_.begin`).
  The white skill streak play_show.cpp:143-213 is drawn for every non-spell skill unless excluded (:158-167). Effect
  module pattern: src/game/fx/fury.{h,cpp} (MuMain trace in the header, `open()` loads content/effects/<name>/), wired at
  play.h:1945, play_mode.cpp:566/576/2021, play.cpp:2776, play_show.cpp:2198, CMakeLists.txt:729.
- **Sounds**: content/sounds/*.wav + sounds.json key; needs the showing cook (`./mu make sounds`).
- **Orb**: content/items/misc/OrbCyclone.json is the template. Group 12 taken on main: 0-22 and 25; darklord's plan
  holds 23, 24, 26. useItem realm_items.cpp:856-875 checks class bit, level, energy (strength?) and calls `learn()`.
- **Icon**: content/interface/skills/skill_55.png, cut by pipeline/skill_icons.py (cell N-1 for N < 57).
- **Tests**: `testGladiatorSkills` sim_test.cpp:1384, `testSkills` :4344, debuff patterns `testWeaknessRune` :11111;
  balance tests/skill_sweep.cpp with the MG kit tests/kits/sweep.json.

# 5. Decided (2026-10-24, from this research and the user's standing taste)

1. **MU's row**: damage 80 (WebZen/OpenMU), mana 15, physical, **fire** (OpenMU and the name; WebZen's data says
   poison, which reads wrong for the look). AG is not a resource here.
2. **Area: the ribbon's own shape.** A front half-disc, ±90° of his facing (the 187° sweep, made symmetric: "only
   frontal"), out to **2.5 tiles** (the ribbon's outer edge is 220 units from a pivot at his feet, plus a body's
   reach). A per-row arc width, since `kArcHalfAngle` is one constant. Everything inside is hit once, no cap
   (OpenMU's server area; WebZen's 5-per-packet is a client artefact).
3. **When**: the blow lands as the ribbon appears, MU's frame 3 of WHEEL: tick 13 of the 30-tick swing (0.43). The
   clip runs MU's two paces, 0.24 to frame 3 and 0.54 after.
4. **The cut**: 45% a body (WebZen), −10% defence for 10 s, a re-hit refreshes, never stacks, rolled before the blow
   so that blow profits. Bosses shrug it as they shrug Weakness. Shown as MU's three skulls over the head and on the
   target frame as a boon icon; `eBloodAttack.wav` on the body when it lands.
5. **Strength 596** to learn (WebZen's scaled 320 at drop level 60, our requirements formula), no energy. **Level
   120** (the user, 2026-10-24: "add some level requirments"): our orbs ask about twice their drop level -- Cyclone 72
   at 36, Slash 104 at 52, Death Stab 120 at 60 -- and this one drops at 60, so Death Stab's tier; on the orb's
   `requires.level`, its `skill.level_requirement` and the row's `needLevel`, which must agree. Any right-hand weapon
   but a staff (MU's client).
6. **Power, ours**: it is his signature key, so it has to beat the knight's Cyclone he also knows (force 2.0, Arc 3
   tiles, 40 ticks). Start at **force 2.3, cooldown 40 ticks (2 s)**, and tune on the skill sweep with a geared MG.
7. **The look, MU's**: the gathering at the hand (×2, lightning in from 120 out, Spark03, Shiny02); the ribbon
   (motion_blur_r2, radius 145, width 150, pivot 100 up and fixed at the spawn, +75° → −112°, 3 ticks out, 20 ticks life,
   light LifeTime/30 then ÷1.5); Fire01 flames at half the steps; two blue JointThunder01 bolts a step;
   `sKnightSkill3.wav` with the ribbon. **No white skill streak** (MU draws no trail on WHEEL). MuMain's cursor-only
   Shiny03 streak is left out (1.03.35 has none).
8. **The orb**: 12/27, drop level 60, drop only (Hanzo sells early orbs), 51,000 zen. Its drop needs every world's
   tables cooked: asked before that cook.
9. **Row 40, kKeptLayout 16** on main (section 4); a note for darklord's merge.

Built (2026-10-24): the sim -- row 40 (`skill::kFireSlash`, kSkills 41, `SkillRow::arcHalf`/`paceAfter`/`paceKey`/
`cut*`), the cut in `Realm::strikeAt` (`Body::defenseCutUntil`, cleared on rising, snapshot shape 21), the orb's
strength at the read, kKeptLayout 16 and protocol 51, `testFireSlash` in tests/sim_test.cpp; force settled at 2.3 on
the sweep (Icarus is reach-bound, not force-bound); the orb content/items/misc/OrbFireSlash.json; the look,
src/game/fx/fire_slash.{h,cpp}, with the two paces in game/play.cpp, the target frame's and the monster bar's cut
cell, skill_55.png, and eBloodAttack (`skill_blood_attack`). Not yet cooked: the showing (sheets, sounds, icon) and
every world's tables (the orb's drop).
