# Power Slash (skill 56, the Magic Gladiator's) -- research dossier

Researched 2026-10-24 before any build (the user: "make next MG spell", after Fire Slash, docs/plans/mg-skills/
fire-slash.md, whose format and sources this follows). Section 1 server, 2 client, 3 footage, 4 fit, 5 decided.

**In one paragraph.** Power Slash (WebZen `AT_SKILL_SWORD6`, 0.99B, October 2003) is a **weapon skill**: never an
orb, never saved, known while an MG blade that carries skill 56 is held -- Dark Reign Blade (0/21), Explosion Blade
(0/23), Sword Dancer (0/25), Imperial Sword (0/28), Rune Blade (0/31), all five already in content/items/weapons with
`"skill": 56`. MU asks the blade to have rolled its skill option. Skill damage 0 (pure weapon damage), mana 15,
physical, no element, ×2 after defence, no debuff. The server hits a trapezoid in front of him, ±1 tile wide at his feet
to ±6 at 6 tiles, 6 deep: the first 5 bodies always, the 6th to 11th at 50% each, none past 11. He plays the blade's
own swing (TWO_HAND_SWORD_TWO, action 145, 7 keys) with its blade trail -- blue lightning, fire on Sword Dancer -- and at
the echo five invisible carriers fan out at −40, −20, 0, +20, +40° at 60 units a tick for 20 ticks (11.4 tiles, twice
the hit area), each leaving Shiny02 and a big blue flare01 sprite, a blue ground light, and smoke: bright puffs (life
10) and a dim haze (life 50, about 2 s). Nothing is drawn on the bodies hit. It cannot be cast mounted.

# 1. Server

Paths:
- `WZ/` = `webzen/Source/Server Side/GameServer/` (WebZen GS 1.00.93; cp949, line numbers unchanged by iconv)
- `WZC/` = `webzen/Source/Base Common Src/`
- `WZD/` = `webzen/_Server Files/Data/` (private repack; rows quoted, code trusted over it)
- `OM/` = `LEGACY/reference/openmu/src/`
- Build facts: `GAME_VERSION = G_V_S2_2` (WZC/include/define.h:48; G_V_99B = 2, define.h:38). APPLY_TESTSERVER is defined at define.h:736, and the block at :739-3764 is live. `NEW_ATTACK_PROC` is commented out (define.h:411).

## 1. Name and version
- Enum: `AT_SKILL_SWORD6 = 56`, with the comment "테스트 스킬 -> 데스브로드" ("test skill -> Death Broad"): WZC/include/public.h:231. MagicDamage.cpp:717 also calls it 데쓰브로드.
- Data name: "파워슬레쉬" in WZD/lang/main/skill.txt:63, and "Power Slash" in WZD/lang/kor/skill(Kor).txt:48. The code comments say 파워슬래쉬 (ObjUseSkill.cpp:804, ObjAttack.cpp:724).
- Version: everything for 56 sits under `ADD_SKILL_20031022` ("스킬추가 (2003.10.21)"), which is defined only `#if GAME_VERSION >= G_V_99B` (define.h:3616-3619). So it arrived in **0.99B (Oct 2003)**, after Fire Slash's Icarus era.
- OpenMU: not in Version075 or Version095d (no PowerSlash or skill-56 weapon in OM/Persistence/Initialization/Version075|Version095d/Items/Weapons.cs). It first appears in VersionSeasonSix/SkillsInitializer.cs:175.
- The data file's header records a 2011-03-08 fix of "파워슬래쉬 무기 스킬 구분 오류" (Power Slash's weapon-skill flag): WZD/lang/main/skill.txt:5.

## 1. How it is obtained: a weapon skill only, never an orb
- item.txt column 3 (Skill) = 56 on two rows only in the WebZen data (both MG-only):
  - `0,21 "Dark Reign Blade"`: drop 140, dmg 115-142, speed 40, dur 100, magic power 115, STR 116, AGI 53, ENE 9 (WZD/lang/main/item.txt:27)
  - `0,31 "Rune Blade"`: drop 100, dmg 104-130, speed 35, dur 93, magic power 104, STR 135, AGI 62, ENE 9 (item.txt:28)
  - The kor file is identical (item(Kor).txt:25-26).
  - The loader reads Slot then SkillType right after the index: WZC/Common/zzzitem.cpp:5107-5108. The weapon column order is at :5128-5151.
- The skill exists on an item only when it rolled the skill option: `if (Option1) if (p->SkillType) m_Special[..] = SkillType` (zzzitem.cpp:980-995). A skill item's price is ×2.5 (zzzitem.cpp:2519-2524).
- Equipping a skill item calls gObjWeaponMagicAdd(m_Special[0]) and sends it to the client (user.cpp:16682-16693, also :18079-18091). Removing it calls gObjMagicDel (user.cpp:14934-14940).
- gObjWeaponMagicAdd checks **class only** (SkillGetRequireClass), with no energy or level check (user.cpp:28315-28368).
- On login, 56 is in the equipment-skill exclusion list, so it is never saved or loaded as a learned skill: `(itype != AT_SKILL_SWORD6)` (user.cpp:3747; the full list is :3738-3763).
- No orb: the type-12 orb switch has no case for 56 (MagicInf.cpp:155-209).
- Requirements:
  - MG only (skill.txt:63, the MG column = 1)
  - RequireEnergy forced to 0 (MagicDamage.cpp:717), which overrides the row's Eng 100
  - no level requirement (MagicDamage.cpp:683-689 doesn't list 56)
  - In practice the requirement is the weapon's own STR/AGI/ENE (and the item level formula).
- Master tree: AT_SKILL_ML_STRENGTHEN_SWORD6 = 505 (public.h:418). While the master skill is held, the base weapon skill is hidden or replaced (MasterSkillSystem.cpp:2136-2143, 2156-2176). It is usable only while a weapon with m_Special[0]==56 is equipped (MasterSkillSystem.cpp:2198-2210).

## 1. Skill row
- `56 "파워슬레쉬" lvl 0, dmg 0, mana 15, AG 0, dist 5, delay 0, Eng 100, Cha 0, attr -1, attr2 -1, ... MG 1 ... 무기스킬 1, 데미지구분 0` (WZD/lang/main/skill.txt:63; header at :12). The kor row is the same (skill(Kor).txt:48).
- **Skill damage is 0**, so m_DamageMin = m_DamageMax = 0 (MagicInf.cpp:281-283). Power Slash is pure weapon damage times a multiplier.
- Element: none (attr -1, legend at skill.txt:7). The resistance bonus is skipped when `SkillAttr == -1` (ObjAttack.cpp:3169-3170).
- Physical, not wizardry: gObjCheckAttackTypeMagic returns FALSE for 56 (user.cpp:32733-32734). The blade's magic power does not apply.

## 1. Cast paths (WebZen)
- **Live path, 0x1E CGDurationMagicRecv** (protocol.cpp:16772):
  - checks: refused in a safe zone (:16785), SpeedHackCheck (:16847), EnableSkill (56 whitelisted at ObjUseSkill.cpp:391-392)
  - then `UseSkill(aIndex, lpMagic, X, Y, Dir, TargetPos, aTargetIndex)` (protocol.cpp:16861-16863; EXPAND_ATTACK_PACKET_0X1E at define.h:3633)
  - UseSkill (ObjUseSkill.cpp:660):
    - SkillDelay check (:705)
    - mana deducted and AG 0 used (:713-722)
    - 0x1E broadcast with x,y,dir (:723-737)
    - then **`SkillPowerSlash(aIndex, lpMagic, x, y, dir, TargetPos, aTargetIndex)` runs live, server-side**. It is under ADD_SKILL_20031022, not NEW_ATTACK_PROC (ObjUseSkill.cpp:802-811).
    - Quirk: the dispatch sits outside the `if(usebp>=0)` block (:714-787), so it would fire even when AG falls short. That is moot here, since AG is 0.
- **The server picks the targets.** SkillPowerSlash (ObjUseSkill.cpp:2863-2968):
  - Shape: `SkillFrustrum(TargetPos, aIndex)` (:2871) builds a trapezoid from local corners (-6,6), (6,6), (1,0), (-1,0) (user.cpp:32074-32077). It is rotated by `bangle*360/255` degrees (integer math, :32080), each corner is truncated to int and added to the caster's tile (:32087-32088). The result is **2 tiles wide at the caster, 12 wide at 6 tiles out, 6 deep**. TargetPos is the client's angle byte.
  - Inside test: point-in-quad by integer cross products, `d<0 → out`, edges inclusive (user.cpp:32093-32105). The near edge runs through the caster's own tile.
  - Candidates are the caster's viewport list VpPlayer2[0..MAXVIEWPORTOBJECT-1], in viewport order (:2885-2966). There is **no other distance check**: the frustum (6) is the reach, and the row's dist 5 is not consulted.
  - Who can be hit (:2895-2931): any monster that isn't a summon; the client's forced target aTargetIndex (from the 0x1E packet); the duel partner (DUEL_SYSTEM_20031028, define.h:4049); anyone during the Castle Siege battle (MODIFY_RANGEATTACK_CHECK_20050113, define.h:4011); guild-war enemies, including their summons. Other players and summons are skipped.
  - **Count cap** (:2937-2957): HitCount counts every eligible target in the frustum, hit or not.
    - targets 1-5: always hit
    - targets 6-11 (HitCount 5..10): 50% each (`rand()%2`)
    - target 12 onward (HitCount > 10): never
    - So at most 11 targets, about 8 on average when 11 or more stand in the frustum.
  - Each hit is `gObjAttack(lpObj, target, lpMagic, 1, 1)` (:2955): **1 hit per target**, immediately, with no delay.
- **0x1D CGBeattackRecv (client target list): rejected for 56.** It is not in the allowed list at protocol.cpp:16355-16378, so it logs "error-L3" and returns (:16380-16381). Clients cannot add hits.
- **0x19 CGMagicAttack (single target): accepted.**
  - It runs gCheckSkillDistance when SkillDistanceCheck is on: Chebyshev |dx|,|dy| ≤ dist 5 + Temp 2 = 7 (protocol.cpp:15405; user.cpp:33994-34021). The code default is off (Gamemain.cpp:4355); the repack turns it on (WZD/commonserver.cfg:210-211).
  - Then UseSkill → RunningSkill `case AT_SKILL_SWORD6: gObjAttack(lpObj, target, lpMagic, 1, 0)` (ObjUseSkill.cpp:1022, 1107-1122). It costs mana and AG like the cast. This is a one-target path the real client doesn't use for 56.
- Master variant MLS_PowerSlash (MasterSkillSystem.cpp:1967-2075):
  - its frustum is (dist+1) by (dist+1), i.e. half-width 6, depth 6 for dist 5 (:1974-1979; MLS_POWERSLASH_DISTANCE_20090216 at define.h:913)
  - it adds `gObjCalDistance ≤ dist` (:2051-2052)
  - its cap has a bug: `if(HitCount > 10)` has no `else` before `if(HitCount >= 5)`, so targets past 11 still get 50% (:2056-2070)

## 1. Damage path (WebZen)
- In gObjAttack, 56 takes the physical/weapon branch → GetAttackDamage(..., lpMagic), and the weapon loses durability on a hit (ObjAttack.cpp:718-783).
- GetAttackDamage:
  - adds skill min/max (0/0) (ObjAttack.cpp:3152-3160)
  - adds SetOpAddSkillAttack (set-option skill damage) (:3163-3167)
  - no element bonus (:3169-3170)
  - then the normal weapon roll, crit/excellent and `−defence`, as for Fire Slash (see fire-slash-server.md)
- Multiplier: `case AT_SKILL_SWORD6: AttackDamage = AttackDamage*2;` (ObjAttack.cpp:1435-1439). There is **no class check and no energy term**. A commented-out line shows it was 190% earlier (:1437). The Master 505 version is also ×2 (:1887-1888).
- So a hit = (weapon damage + set skill bonus − defence) × 2. Skill damage is 0, so it scales purely with the blade.
- **No debuff, no knockback, no status effect.** Nothing in SkillPowerSlash beyond gObjAttack, and 56 is not in any buff or MaGum path.

## 1. OpenMU (S6)
- `CreateSkill(PowerSlash, "Power Slash", AllMGs, Physical, distance 5, mana 15, AreaSkillAutomaticHits)` (OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:175):
  - damage 0, AG 0, no element (defaults at Skills/SkillsInitializerBase.cs:51-71)
  - an update removes any energy requirement (Updates/FixDamageCalcsPlugInSeason6.cs:250-254)
- `AddAreaSkillSettings(PowerSlash, frustum=true, startWidth 1.0, endWidth 6.0, distance 6.0)` (SkillsInitializer.cs:176, also Updates/AddAreaSkillSettingsUpdatePlugIn.cs:60; signature SkillsInitializerBase.cs:129-150). Everything else is default.
- Frustum (GameLogic/PlayerActions/Skills/FrustumBasedTargetFilter.cs:218-240):
  - corners (∓6, 6) and (±1, **0.99**)
  - rotation `rotation*360/255 + 180°`, computed in floats with no int truncation
  - a target on the caster's own tile is always in (:64-67)
- Targets (AreaSkillAttackAction.cs):
  - candidates are attackables within Range 5 of the **client's targetAreaCenter**, then the frustum filter (:129-142)
  - players only if `AreaSkillHitsPlayer` (:150-153); summons excluded (:156)
  - the explicit extra target counts if within Range+2 of the player, **without the frustum test** (:108-114)
  - default settings mean every target is hit once, immediately, with **no count cap** (:76-86, :206-219)
- Weapons with skill 56 (Items/Weapons.cs):
  - 0,21 Dark Reign Blade (:158), 0,23 Explosion Blade (:160), 0,25 Sword Dancer (:162), 0,28 Imperial Sword (:165), 0,31 Rune Blade (:166)
  - such "magic swords" get the staff-rise and wizardry option (:405-412)
- Damage:
  - skill damage 0 + elemental bonus (none) + SkillDamageBonus (AttackableExtensions.cs:772-790)
  - then × SkillMultiplier = 2 for every MG skill (CharacterClasses/ClassMagicGladiator.cs:123; applied at AttackableExtensions.cs:226-246)
- Master: PowerSlashStreng = 482 (Skills/SkillNumber.cs:235; SkillsInitializer.cs:374, 882), versus WebZen's 505.

## 1. Differences

| Topic | WebZen 1.00.93 | OpenMU S6 |
|---|---|---|
| Who targets | Server: the viewport, filtered by the frustum around the caster (ObjUseSkill.cpp:2885-2966) | Server: Range 5 around the client's area center, then the frustum (AreaSkillAttackAction.cs:131-142) |
| Shape | Trapezoid ±1 at 0 → ±6 at 6, int-truncated corners, integer angle | ±1 at 0.99 → ±6 at 6, float, +180° offset |
| Reach | Frustum only (6); dist 5 unused | Frustum 6, and candidates within 5 of the area center |
| Count | 5 sure, 6-11 at 50%, then none (max 11) | Unlimited |
| PvP | Forced target, duel, siege, guild war only | Players only if AreaSkillHitsPlayer; the extra target always (no frustum check) |
| Client hit list (0x1D) | Rejected (protocol.cpp:16355-16381) | n/a |
| Single-target 0x19 | Accepted, one hit, distance ≤7 if checks are on | n/a |
| Granted by | Skill-option roll on 0,21 / 0,31 (repack data) | Skill option on 0,21/23/25/28/31 |
| Same in both | 0 skill dmg, mana 15, AG 0, dist 5, no element, physical, ×2, 1 hit per target, no debuff, MG only, no energy or level requirement | |

# 2. Client

Paths: **MM** = LEGACY/reference/MuMain/src/source, **D** = MuMain src/bin/Data, **103** = MU_Chs_1.03.35/source, **WZ** = webzen (the clone)/Source/Server Side/GameServer.
Ticks are at 25 fps (FPS_ANIMATION_FACTOR = 1, every `rand_fps_check(1)` passes). Shared basics (additive blend, sprite sizing, OZJ = 24-byte header + JPEG, JPEG particles draw additive, Shiny02) are in **fire-slash-client.md** and not repeated.

**Short version:** the five "waves" are invisible carriers. Their mesh (Skill/Magic02.bmd) is hidden. What you see is a fan of five glowing points, each moving 60 units/tick for 20 ticks and leaving blue-white smoke behind. There is also a blue lightning (or, for Sword Dancer, fire) weapon trail on the blade.

## 2.0. Identity
- MM: `AT_SKILL_POWER_SLASH = 56` (MM Core/Globals/_enum.h:350). The master version is `AT_SKILL_POWER_SLASH_STR = 482` (_enum.h:565), which maps back to 56 (_enum.h:675). Every visual switch shares one case for both (e.g. ZzzCharacter.cpp:2820-2821), so STR looks the same.
- MM `ReceiveMagicList` hides 56 from the skill list when 482 is also known (MM Network/Server/WSclient.cpp:1454-1467).
- 103: the same number is `AT_SKILL_ICE_BLADE = 56`. Its CP949 comment reads "파워슬래쉬 (데스블레이드 무기스킬)(순차적인 연쇄공격.)": Power Slash, a Death Blade weapon skill, a sequential chain attack (103 _enum.h:1927). The master ranks are `AT_SKILL_POWER_SLASH_UP..+4` (103 _enum.h:2130), sharing the case under `PJH_SEASON4_MASTER_RANK4`, which is defined at 103 Winmain_New.h:1931.
- **It is a weapon skill, not a learned one.** `IsNonWeaponSkillOrIsSkillEquipped` requires an equipped weapon whose `Special[]` holds 56 (MM GameLogic/Items/CSItemOption.cpp:602-643). `Special` comes from Item.bmd's `m_wSkillIndex` (MM Engine/Object/ZzzInfomation.cpp:1123-1131). AttackKnight checks the same thing (MM GameLogic/Combat/ClassAttack.cpp:521-524).
  - I decoded D/Local/Item.bmd. The records are 84 bytes: `char Name[30]` followed by ITEM_FIELDS (MM Data/GameData/ItemData/ItemStructs.h:23-27, ItemFieldDefs.h:9-13). Each byte is XORed with FC CF AB. Skill is the WORD at offset 36.
  - Items with skill 56, all MG-only (RequireClass[3] = 1): Sword 21 Dark Reign Blade (2H), 23 Explosion Blade (2H), 25 Sword Dancer (2H), 28 Imperial Sword (**1H**), 31 Rune Blade (2H) (names from MM _enum.h:1516-1524).
  - 103 lists the same five (21/23/25/28/31) in a commented-out weapon check (103 ZzzInterface.cpp:6677-6690; NewUIMainFrameWindow.cpp:2733-2740).
- The MU2 rig_note lead in content/items/weapons/Sword22.json (Sword22 = index 21 = Dark Reign Blade): it says "five MODEL_MAGIC2 waves … ZzzCharacter.cpp:2820-2843". **Confirmed**, but the trigger is `PLAYER_ATTACK_TWO_HAND_SWORD_TWO` (145), not RUN. The RUN_TWO_HAND_SWORD_TWO sparks at MM ZzzCharacter.cpp:7700-7720 are this blade's idle run look and are unrelated.

## 2.1. Cast (local client)
- The MG (CLASS_DARK) right-click goes to `AttackKnight`, which first requires a right-hand weapon that is not a staff (ClassAttack.cpp:484-489). See fire-slash §1.
- Power Slash branch (MM ClassAttack.cpp:660-697; 103 ZzzInterface.cpp:6667-6722):
  - It runs only when `Helper < UNIRIA || (Helper > DARK_HORSE && Helper != FENRIR)`. On Uniria, Dinorant, Dark Horse or Fenrir the click silently does nothing.
  - The UI greys the icon on Uniria, Dinorant and Fenrir (MM UI/NewUI/HUD/NewUIMainFrameWindow.cpp:2131, 2159-2163; 103 NewUIMainFrameWindow.cpp:2716-2727).
  - So the `PLAYER_FENRIR_ATTACK_MAGIC` (97) branch inside it (ClassAttack.cpp:688-690) is **dead**.
  - Turn: `o->Angle[2] = CreateAngle2D(pos, TargetPosition)`. Then `CheckTile` checks range; out of range, the hero walks there first, as Fire Slash does.
  - It sends **`SendCharacterMove`** with one step on its own tile, which tells the server the new facing (ClassAttack.cpp:668-673).
  - It sends **0x1E MagicContinue** `(skill, X, Y, dir = yaw/360*255, byValue = GetDestValue(target tile), angle = (yaw+180)/360*255, TKey)` (ClassAttack.cpp:675-684). Note the ×255 here, where Fire Slash uses ×256.
  - Then `SetAttackSpeed()`, `SetAction(o, PLAYER_ATTACK_TWO_HAND_SWORD_TWO)`, `Movement = 0` (ClassAttack.cpp:686-695).
  - No effect spawns and no sound plays here. Power Slash is not in `SkillWarrior` and has no MM-only second path (grep: no hit in GameLogic/Combat/SkillCast.cpp).
- `AttackTime` is **not** started locally. On the caster's own screen the waves wait for the echo (§2).

## 2.2. Server echo and clip
- **0x1E `ReceiveMagicContinue`** (MM WSclient.cpp:5079-5489):
  - Yaw = `Angle/255*360` for everyone, the hero included (5093).
  - For other players, Power Slash plays `SetAction(so, PLAYER_ATTACK_TWO_HAND_SWORD_TWO)` (5228-5231) and resets `AnimationFrame = 0` (5473). The hero's own frame is not reset.
  - Then for everyone: `sc->AttackTime = 1` (5481). No sound plays here.
  - There is no mount variant: 145 plays even on a pet.
  - 103 does the same (103 WSclient.cpp:6138, 6339-6347).
- **Server side** (WZ ObjUseSkill.cpp:801-808 → `SkillPowerSlash` 2863-2967):
  - `SkillFrustrum(Targetangle)` builds a tile trapezoid from the local corners (−6,6) (6,6) (1,0) (−1,0), rotated by angle·360/255 (WZ user.cpp:32069-32105). It is **6 tiles deep**, 2 wide at the caster and 12 wide at the far end.
  - The first 5 targets are hit for sure, the 6th to 11th at 50% each, and none after that (2930-2952). Damage goes through `gObjAttack`.
  - **No 0x19 is sent**, so MM's 0x19 case for 56 is dead in practice. That case (MM WSclient.cpp:4323-4327; 103 5301-5310) would do `SetAction(145)`, `AttackTime = 1` and `PlayBuffer(SOUND_SKILL_SWORD4)`.
- **Clip `PLAYER_ATTACK_TWO_HAND_SWORD_TWO` = index 145** (MM _enum.h:3056, counted from PLAYER_SET = 0; 103 _enum.h:1597 is also 145 and the range has no #ifdef). WHEEL = 65 and FENRIR_ATTACK_MAGIC = 97 also check out.
  - D/Player/player.bmd action 145 has **7 keys** and no lock positions (decrypted; the file has 284 actions). 142-144 (STOP/WALK/RUN_TWO_HAND_SWORD_TWO) have 7 keys and lock = 1.
  - 145 is also the **normal attack** of Dark Reign / Rune / Explosion Blade and Sword Dancer (MM ZzzCharacter.cpp:1178-1181; 103 1429). Power Slash therefore reuses those blades' normal swing.
  - The Imperial Sword (1H) normally swings SWORD_RIGHT1/2. For Power Slash it is forced into 145.

## 2.3. Timing
- PlaySpeed is 0.24 at load (MM Engine/Object/ZzzOpenData.cpp:362; 103 696). `SetAttackSpeed` sets it to **0.25 + AttackSpeed·0.004** (MM ZzzCharacter.cpp:930; 103 1034).
  - This is global on Models[MODEL_PLAYER] and uses the hero's speed, so observers see your speed.
  - Unlike Fire Slash, nothing speeds it up mid-clip.
- Clip length = 6 / PlaySpeed: **24 ticks (0.96 s)** at AttackSpeed 0, about 9.2 ticks at AttackSpeed 100.
- `AttackStage` Power Slash case (MM ZzzCharacter.cpp:2820-2843; 103 3148-3176):
  - It fires on the **first tick** with `AttackTime > 0` **and** `CurrentAction == 145`. There is **no AnimationFrame gate**.
  - It spawns 5 × `CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light, sub 2, o)` with yaw = facing −40, −20, 0, +20, +40.
  - Then `PlayBuffer(SOUND_SKILL_SWORD3)`: 2D, no object.
  - Then `AttackTime = 15`, with limit 15, so it fires **once**.
- Observers: the echo sets frame 0 and AttackTime 1, so the waves leave at **frame 0**, before the swing lands. The same tick, `MoveCharacter` runs before `MoveEffects` (MM Scenes/MainScene.cpp:284-289).
- Caster's own screen: the waves leave on the echo tick, at frame ≈ RTT·PlaySpeed.
  - If the clip has already ended (RTT > 24 ticks ≈ 0.96 s at AttackSpeed 0, or about 0.37 s at AttackSpeed 100), `CurrentAction` is no longer 145 and **no waves draw at all**.
  - The swing sound (§5) still plays only if the waves do.
- At the end of the stage, `o->Weapon/WeaponLevel = c->Weapon[0]` (MM ZzzCharacter.cpp:4186-4190; 103 4939-4949). Nothing reads them for this skill, so it is **dead**.

## 2.4. What spawns (per cast; positions in MU units, 100 = 1 tile; MU forward is −Y)

### A. MODEL_MAGIC2 sub 2 ×5: the carriers
- Create (MM Render/Effects/ZzzEffect.cpp:1991-1998; 103 ZzzEffect.cpp:2530-2538):
  - BlendMesh 0, **LifeTime 20**, Direction (0, −60, 0).
  - sub 2: `o->Weapon = PacketSerial++`, a vestigial hit serial. Neither client calls AttackCharacterRange from MAGIC2 (103 list at ZzzEffect.cpp:1402…13374 has none in 11697-11722).
- Generic init: Scale 0.9, Velocity 0.3, Light = caster's o->Light, Angle = the yaw above (MM ZzzEffect.cpp:346-385). It is not pooled to g_SkillEffects (MM GameLogic/Skills/SkillEffectMgr.cpp:39-48).
- Motion (shared MoveEffect tail):
  - `MoveParticle(o, true)`: pos += Rotate(Direction, Angle) every tick (MM ZzzEffect.cpp:8564, 6231-6247). That is 60 units/tick in a straight line, with **no terrain follow**: z stays at the caster's feet.
  - It lives **20 ticks (0.8 s)**. The move handler runs at offsets 0, 60, …, **1140** (LifeTime−− after the move, ZzzEffect.cpp:8573-8577).
  - Fan spread: adjacent waves are 20° apart, so about 400 units apart at the far end.
  - The visual reaches **11.4 tiles**, but the server hits only within 6 tiles (§2).
- **The mesh is never drawn.** sub 2 sets `o->HiddenMesh = 0` every move (MM Render/Effects/Behaviors/MoveHandlers.cpp:3365; 103 ZzzEffect.cpp:11709).
  - D/Skill/Magic02.bmd has **1 mesh** (16 verts, 16 tris, tex `magic_g.jpg`, bbox x ±45, y −108..211, z 0..244; 1 bone, 1 action of 1 key).
  - `RenderBody` skips mesh `i == HiddenMesh` (MM Render/Models/ZzzBMD.cpp:2269-2273), so nothing of the model renders.
  - The first move runs before the first render (MainScene order above). The `BlendMeshLight = LifeTime·0.1` and `BlendMeshTexCoordU = −LifeTime·0.2` scroll (MoveHandlers.cpp:3352-3353) is therefore moot.
  - The visible Magic02 sheet is **sub 0**, Power Wave's wave (MM ZzzCharacter.cpp:5045-5055).
- Per tick, Move_MODEL_MAGIC2 (MM MoveHandlers.cpp:3346-3381; 103 ZzzEffect.cpp:11697-11722):
  - Luminosity L = rand 0.7–1.0. In the last 4 ticks it drops by (5−LifeTime)·0.2 (MM ZzzEffect.cpp:6645-6650).
  - 4 × particle `BITMAP_SMOKE` sub 3 at pos (light = o->Light, overwritten in move).
  - `AddTerrainLight(pos, (0.3L, 0.6L, L), range 3, Primary)`: a **blue ground light** that runs with each wave.
  - Only for sub 2:
    - 1 × `BITMAP_SMOKE` sub 11 at pos, light (0.3L, 0.6L, L), scale (80..111)·0.015 = 1.20–1.67.
    - At pos + (0, 0, 50):
      - 2 × sprite `BITMAP_SHINY+1`, scale 1.5, light (1, 1, 1), random roll. Shiny02 is 32×64, so **48×96** each.
      - 1 × sprite `BITMAP_LIGHT`, scale 3.5, light (0.3L, 0.6L, L), random roll. flare01 is 64×64, so **224×224**.
    - Sprites are additive sub 0 and drawn for one frame. AnimationFrame starts at 1 and stays at 1 (MM Render/Effects/zzzeffectsprite.cpp:22-37, 42-79).
    - An unused `Angle[2] += rand%10−5` (3369) is computed but never used.
- Totals: 400 sub-3 puffs, 100 sub-11 puffs, 200 Shiny sprites and 100 flare sprites per cast.

### B. BITMAP_SMOKE sub 3: the bright puffs that burst from each point
- Create (MM Render/Effects/ZzzEffectParticle.cpp:1248-1254):
  - Life 10, scale 0.80–1.11.
  - Angle (pitch rand ±45, yaw rand 0–359), so the burst direction is random.
  - Velocity (0, −(40..47), 0). Rotation stays 0 (not set), so it is **not spun**.
- Each tick:
  - LifeTime−−, then `MovePosition(pos, Angle, Velocity)` (ZzzEffectParticle.cpp:3868-3884; MM Engine/AI/ZzzAI.cpp:147-156).
  - Then (5328-5333): L = LifeTime/8, light (0.8L, 0.8L, L), starting at about 1.13 (overbright blue-white) and fading to 0. Velocity ×0.4, scale +0.1.
- Each puff drifts about 67–78 units in total and grows to 1.7–2.0, i.e. 51 → 128 units on smoke01 64×64.

### C. BITMAP_SMOKE sub 11: the lingering blue haze
- Create (ZzzEffectParticle.cpp:1255-1266, shared with sub 14):
  - Life **50**. Position jittered xy ±32, z +32..95.
  - Random pitch and yaw as above, random Rotation, `TurningForce = Light`, Velocity (0, −(40..47), 0).
- Each tick (5334-5341): light = TurningForce·LifeTime/50, a dim blue fading over 2 s. Velocity ×0.4, scale +0.05 (ending at about 3.7–4.2, i.e. 77 → about 260 units), z −1.
- It outlives the waves by about 2 s. The last puffs die about 70 ticks (2.8 s) after the cast.
- Render for both smoke subtypes: `RenderSprite` with o->Rotation (sub 3 and 11 are not in the subtract lists, ZzzEffectParticle.cpp:9012-9025). Smoke01 is a JPEG, so it is additive (8924-8931).
- This haze plus the flare sprites matches power-slash-footage.jpg: a fan of white-blue glows over a blue-white cloud, with no visible mesh.

### D. The weapon trail
- `CreateWeaponBlur` runs every tick for every character (MM ZzzCharacter.cpp:5330).
  - The gate is `AnimationFrame >= 3 || CurrentAction == 145`, so the trail covers the **whole** clip (3767-3770; 103 4456-4458).
  - A sword-type weapon in 145 → BlurType 1. BlurMapping: Dark Breaker = 6, **Sword Dancer = 2**, any other sword = **1** (3830-3846; 103 4517-4532).
- Light is (1, 1, 1) whenever mapping ≠ 0 (3978-3981).
- The ribbon runs from link bone + (0, −20, 0) to (0, −120, 0): about 100 units of blade (3932-3937).
- Player swords outside the 5 legacy exceptions take the sub-stepped path (3984-4022):
  - 10 bone samples per tick across the last PlaySpeed slice, with `CreateBlur(..., Short = true)`.
  - LifeTime 15, one blur per owner, up to 29 points, −1 point per tick (MM Render/Effects/ZzzEffectBlurSpark.cpp:97-172). The trail is therefore about 3 ticks of swing long.
- Render (ZzzEffectBlurSpark.cpp:175-246):
  - Texture `BITMAP_BLUR + mapping`. Mapping 1 = `Effect\motion_blur.jpg` **32×64**, **blue lightning streaks**. Mapping 2 = `motion_blur_r.jpg` 32×64, **white-yellow → red fire** (MM ZzzOpenData.cpp:5271-5272).
  - Colour fades along the trail: (Number−j)/Number.
  - Additive, unless the owner is a murderer (`c->Level = 1` when PK ≥ MURDERER2, MM WSclient.cpp:7006-7009). Then it is **subtractive** (dark trail) at full colour.
- So the look **does** vary by weapon: Sword Dancer gets a fire trail and every other Power Slash blade gets blue lightning. The waves themselves never vary.

### E. On hit monsters
- Nothing. There is no 0x19 echo (§2) and no debuff. The only marks are the normal damage numbers from gObjAttack.
- No hit effect, sound or light is tied to 56.

## 2.5. Sounds
| When | Sound | File | Length |
|---|---|---|---|
| waves spawn (AttackStage), 2D | SOUND_SKILL_SWORD3 = 114 | Sound/sKnightSkill3.wav | 0.42 s |
| dead 0x19 path only | SOUND_SKILL_SWORD4 = 115 | Sound/sKnightSkill4.wav | 0.58 s |

Sources: MM Audio/DSPlaySound.h:109-110, ZzzOpenData.cpp:4787-4788; durations from afinfo, 8-bit 44.1 kHz stereo. There is no cast sound or hit sound.

## 2.6. MuMain vs 1.03.35
- **Constants are identical:** the AttackStage fan (±40/20°, sub 2, SWORD3, AttackTime 15), MAGIC2 create (20 / −60 / BlendMesh 0) and move (4 smoke3, smoke11, 2×Shiny 1.5, Light 3.5, terrain light), PlaySpeeds 0.24/0.25, the clip index, the blur mapping, and the echoes. Citations are in the sections above.
- **FPS gating (MM only):**
  - `rand_fps_check(1)` on the AttackStage spawn (MM ZzzCharacter.cpp:2822). Above 25 fps, the fan can be skipped on its one eligible tick.
  - `rand_fps_check` + `CreateParticleFpsChecked` on the smoke (MoveHandlers.cpp:3356-3358, 3370-3371). 103 uses plain `CreateParticle` (103 ZzzEffect.cpp:11706, 11712).
  - MM's blur sub-step spans a fixed PlaySpeed slice (MM ZzzCharacter.cpp:3997-4022, comment). At 25 fps it equals 103's (103 ZzzCharacter.cpp:4687-4700).
- **Master ranks:** MM has one STR id (482). 103 has 5 ranks (POWER_SLASH_UP+0..4). Neither changes the look.
- **Weapon gate:** MM always checks `IsNonWeaponSkillOrIsSkillEquipped`. 103 checks it under `g_csItemOption.Special_Option_Check()` (103 ZzzInterface.cpp:6471).
- **Mounted:** both clients have the same helper test and the same dead Fenrir branch (103 ZzzInterface.cpp:6674-6721).

## 2.7. Assets (under D)
| What | File | Size | Exists |
|---|---|---|---|
| carrier model (hidden for sub 2) | Skill/Magic02.bmd (ver 0xA, 1 mesh) | 1954 B | yes |
| its texture (unused here) | Skill/magic_g.OZJ | 128×64 | yes |
| puffs / haze (BITMAP_SMOKE) | Effect/smoke01.OZJ | 64×64 | yes |
| big glow (BITMAP_LIGHT) | Effect/flare01.OZJ | 64×64 | yes |
| sparkle pair (BITMAP_SHINY+1) | Effect/Shiny02.OZJ | 32×64 | yes |
| trail, most blades (BITMAP_BLUR+1) | Effect/motion_blur.OZJ (blue lightning) | 32×64 | yes |
| trail, Sword Dancer (BITMAP_BLUR+2) | Effect/motion_blur_r.OZJ (fire) | 32×64 | yes |
| swing | Sound/sKnightSkill3.wav | 0.42 s | yes |
| (dead path) | Sound/sKnightSkill4.wav | 0.58 s | yes |
| clip | Player/player.bmd action 145 (7 keys) | — | yes |
| weapon list | Local/Item.bmd: Sword 21/23/25/28/31 carry skill 56 | — | yes |

- Load lines: Magic02 at MM ZzzOpenData.cpp:4166 (103 ZzzOpenData.cpp:5696). smoke01, flare01 and Shiny02 at MM ZzzOpenData.cpp:5205, 5195 and 5263.
- **For a remake:** draw no mesh. Each wave is a moving point light (two Shiny02 sprites, one big blue flare01, a blue ground light) that sheds 4 bright puffs and 1 slow blue haze puff per tick. Add the per-weapon blade trail.

# 3. Footage

InfinityMU wiki "Mg-power-slash.gif" (git: MU2/docs/reference/skill-clips/magic-gladiator/power-slash.mp4, 300×230,
25 fps, 2.84 s; Season-era, a winged MG with a blue blade at a pack of tree monsters, casting back to back). Still
strip `power-slash-footage.jpg`: frames 8, 10, 16, 17 / 19, 22, 26, 34.

| frame | what shows |
|---|---|
| 8-10 | a flat translucent blue wedge at sword height, fanned out ahead of the swing (the blade's lightning trail, motion_blur) |
| 16-17 | a white-blue bloom bursts ahead of him (the flare01 sprites and bright smoke, five carriers at once) |
| 19-34 | a pale blue-white mist with star flares spreads over the ground ahead and holds across the pack (the haze puffs, life 50); numbers rise on several monsters at once |

Back-to-back casts keep the mist standing. No sheet ever shows apart from the trail, which agrees with the client
trace (Magic02's mesh is hidden on sub 2).

# 4. Fit to our code

As Fire Slash's §4 (docs/plans/mg-skills/fire-slash.md), with Fire Slash now in: kSkills 41, Fire Slash row 40,
kKeptLayout 16, protocol 51 (neither shipped yet). The precedent for a skill known from gear is the Horn of
Dinorant's Fire Breath: `row.mounted`, `Realm::knows` (realm_skills.cpp:77-85) returns `dinorantWorn()` and the skill is
never saved. Items carry `Held::skill` (src/sim/items.h:77-80, the shop's `+S`; "nothing reads it for a fight yet") and
the table row's `skill` (src/content/tables.cpp:204, from each weapon's `"skill": 56`).

# 5. Decided (2026-10-24, from this research and the user's standing taste)

1. **Learned off an orb** (the user, 2026-10-24: "Power Slash has to be orb"; it was first known from the blade, the
   Fire Breath pattern, and that is taken back). The **Orb of Power Slash**, content group 12 number 28
   (content/items/misc/OrbPowerSlash.json; 27 is Fire Slash's, 23, 24 and 26 darklord's), the gladiator's alone,
   drop level 75, drop only, 75,000 zen. Read once, the skill is his for good: the learned bit, saved as every
   learned skill is. Cast with **any weapon in his right hand but a staff**, Fire Slash's rule (`families`
   `arms::kEvery`), and not riding (`SkillRow::afoot`). Ours: MU's Power Slash is a weapon skill of his blades
   (WebZen's AT_SKILL_SWORD6, item.txt's skill column), and MU has no such orb. A weapon's rolled skill option
   (`Held::skill`) still teaches nothing.
2. **MU's row**: mana 15, physical, no element, the weapon's damage only. **Learned at 150** (the user, 2026-10-24),
   on the orb's requirement, its skill block and the row's `needLevel`, which agree: our orbs' ladder of about twice
   the drop level, past Fire Slash's 120 and below his blades' tier. No strength or energy asked.
3. **Area: WebZen's trapezoid**, ±1 tile at his feet widening to ±6 at 6 tiles, 6 deep, facing his aim. **WebZen's
   cap**: the 5 nearest always, the 6th to 11th at 50% each off seeded dice, none past 11 (nearest first, ours; MU's
   order is the viewport's).
4. **When: each body is struck when the wave reaches it** ("the number comes with the visual", the Fire Burst rule):
   waves spawn at the swing's start as MU's do, at 60 units a tick (25 fps), so a body d tiles out is struck after
   d/0.6 MU frames. **The waves fade out past the hit area** (by about 7 tiles), not at MU's 11.4, so nothing is
   drawn where nothing is hit.
5. **Power, ours**: a key with a cooldown, not a primary. Start at force 2.0 and **cooldown 40 ticks (2 s)**; tune on
   the skill sweep so it reads as the wide-and-capped answer to Fire Slash's narrow-and-debuffing one, close to it on a
   pack, below it on one body.
6. **Clip**: the blade's own swing, action 145 (TWO_HAND_SWORD_TWO), at its pace 0.25 + attack speed. **Not when
   mounted** (MU's).
7. **The look, MU's**: the blade trail for the whole clip (motion_blur blue lightning; motion_blur_r fire for Sword
   Dancer); five carriers at −40/−20/0/+20/+40 drawing two Shiny02, one flare01 and a blue ground light a tick, the
   bright smoke puffs and the long blue haze; one `sKnightSkill3.wav` with the waves. Nothing on the bodies.
8. **Row 41, kSkills 42**, folded into Fire Slash's unshipped kKeptLayout 16 and protocol 51 if no other change has
   claimed them since; darklord's rows then move up two.

Built (2026-10-24): the sim -- row 41 (`skill::kPowerSlash`, kSkills 42, refused riding in `throwSkill` and
`armed`), `Spread::Frustum` in `Realm::gather`
(kFrustum*: half a tile behind his middle to six out, +-1 to +-6), `Realm::powerSlash` (the nearest five, the 6th-11th
at even odds off the realm's dice, none past) striking each body as a held-back blow (`Realm::loose`'s `delay`, ported
from darklord's a78205f8) `round(d / 0.75)` ticks after the let-go, which is the clip's first tick (`release` 0);
folded into Fire Slash's unshipped kKeptLayout 16 and protocol 51; `testPowerSlash` in tests/sim_test.cpp. **The clip
is player.muc's 146**, not the 145 of section 2.2: content/players/rig/actions.json, checked on the bench, numbers
PLAYER_ATTACK_TWO_HAND_SWORD_TWO 146 (6 keys at 0.25; 142-145 are 7-key clips) and sim/swings.h measured its overhead
cut there (2026-10-10); pipeline/cook.py now writes 146 into the action table. Force settled at **2.3**, Fire Slash's,
on the skill sweep (tests/kits/sweep.json's GaiusReign, a Dark Reign Blade +11, 6 seeds x 4 min, per hour with the best
talent build; Fire Slash 12.70M / 5,301 a blow on Icarus and 8.20M / 1,997 on Aida): Power Slash 9.71M / 4,609 on Icarus
and 8.56M / 1,622 on Aida -- below on one body, close on Aida's packs; 2.2 and 2.4 read alike within the sweep's noise.
Its card says "Not while riding". The look, src/game/fx/power_slash.
{h,cpp}: five carriers from his feet a tick after the cast along the realm's aim, MU's per-frame Shiny02 pair, blue
flare01, blue ground light, four bright smoke puffs and one blue haze; twelve frames (7.2 tiles), not twenty; the blade
trails the whole clip on motion_blur (motion_blur_r on the Sword Dancer, fx/streak.h's new Lightning sheet);
sKnightSkill3 with the waves. skill_56.png cut by pipeline/skill_icons.py --mumain --only 56. **The orb, the same
day** (5.1, the user: "Power Slash has to be orb"): the blade's grant (`SkillRow::wielded`, `granted()`,
`Realm::wieldsSkill`, the bar's empty key, the card's blade line) taken out; the row learned at 150 with Fire
Slash's weapon rule and `afoot`; the Orb of Power Slash, 12/28, made from the Orb of Fire Slash through `mu edit`;
`testPowerSlash` reads it (unknown before; refused a knight and a gladiator at 149; a plain sword throws it, a staff
and bare hands do not; saved and read back). Not yet cooked: the tables (clip 146's length, and the orb, every
world's, so it drops) and the showing (the icon); not yet seen in game.
