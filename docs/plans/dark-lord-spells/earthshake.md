# Earthshake (skill 62) -- research dossier

Researched 2026-10-25 before any build. Nothing here is built. Drafted by a research agent from the sources below;
every number is read, not recalled. Earthshake is the Dark Horse's skill as Fire Breath is the Dinorant's, so it is
described **against Fire Breath's mounted-skill precedent** and against what of the Dark Horse exists (nothing yet).

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line
  numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`. **WZC** = `.../ws/Source/Base Common Src/Common/`.
  **WZP** = `.../ws/Source/Base Common Src/include/public.h`.
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- MU time: one reference frame = 1/25 s. Our realm: 20 ticks/s. A tile = 100 MU units.

Names: WZ `AT_SKILL_DARKHORSE_ATTACK` ("다크호스 어택", `WZP:252`, the enum after `AT_SKILL_FIREBURST` 61), compiled under
`DARKLORD_WORK` (`WZ/define2.h:3298`); its master-tree strengthening `AT_SKILL_ML_STRENGTHEN_DARKHORSE_ATTACK = 515` (`WZP:406`,
not ours). MM calls it **`AT_SKILL_EARTHSHAKE`** (`MM/Core/Globals/_enum.h:362`; `_STR`/`_MASTERY` are master-tree, not ours);
**`AT_SKILL_DARK_HORSE` is 1.03.35's name** ("다크홀스 어택.(어스퀘이크)", `103/_enum.h:1947`; master variants
`AT_SKILL_ASHAKE_UP..+4`). OM `SkillNumber.Earthshake = 62` (`OM/Persistence/Initialization/Skills/SkillNumber.cs:72`). The
client table names it アースクェイク ("Earthquake", MMD `Local/Skill.bmd` rec 62); skill.txt "어스 쉐이크", the Korean
file "Earthshake". Ours: no `skill::` constant yet (`src/sim/skills.h:124-171` stops at Force/Force Wave, Fire Burst, Electric
Spike, Critical Damage, Summon, Fire Scream).

**The headline**: Earthshake is **a round stomp, not a frontal skill**. Cast only while the Dark Horse is worn (the horn puts 62 in
his list, the client also refuses it at no horse life), it strikes **every monster within whole-tile distance under 5 of him --
a disc of 69 tiles, all round, behind him too** -- **500 ms after the cast reaches the server**, at 150..225 + Str/10 + Cmd/5 +
10·horse level (that bonus **in place of** the lord's usual Str/25 + Ene/50), ×(200 + Ene/20)%, and **knocks each body 2 tiles
straight away from him** (3 for knight-type classes), even on a miss. MU's own cooldown is **10 s** (client-enforced). The look
is round too: a 20-tile shock-wave decal on the ground shrinking in, two rings of six rock spikes at 1.5 and 3 tiles all round
the horse with a camera jolt, and a Rageful-Blow crater 0.8 tile ahead -- so **the plan's "5 tiles" is right, and the user's
"only frontal" cannot hold without departing from MU** (open call 1). **Blocker**: nothing of the Dark Horse exists in our
content or sim; Earthshake cannot be shown until the horse is cooked, worn, ridden and drawn (section 6.4).

## 1. MU's rule (server)

### 1.1 The row

`WZD/lang/main/skill.txt` (header dated 2011-03-08), read against its own column header (`:12`):

| Field | **WZ `skill.txt:67`** | MuMain `Skill.bmd` rec 62 | OM (`VersionSeasonSix/SkillsInitializer.cs:180`) |
|---|---|---|---|
| Level | **0** | 0 | -- |
| Damage | **150** | 150 (0x96) | 150 |
| Mana / AG | **0 / 50** | 0 / 50 | 10 range, **50** (its `abilityConsumption`) |
| Distance | **10** | 10 | 10 |
| Delay | **10000 ms** | 10000 (0x2710) | -- |
| Energy / Command | **0 / 0** | 0 / 0 | -- |
| 속성 (element) | **2, lightning** | element byte 02 | `ElementalType.Lightning`, `SkipElementalModifier` (`Skills/SkillsInitializerBase.cs:218-223`) |
| 속성2 (type) | **0, physical** | -- | `DamageType.Physical` |
| Class | DL | DL bit | `AllLords` |
| "무기 스킬" | 0 | -- | -- |

`lang/kor/skill(Kor).txt:51` ("Earthshake" 0 150 0 50 10 10000 0 0 2 0 ...) agrees. `MMD/Local/Skill.bmd` decoded as the Force
Wave dossier did (80-byte records, XOR FC CF AB): rec 62 = `アースクェイク` then `00 00 96 00 00 00 32 00 0a 00 00 00 10 27 00 00
00 00 00 00 ... 02` -> level 0, damage 150, mana 0, AG 50, distance 10, delay 10000, energy 0, Command 0, element 2.
Rolled range: `CMagicInf::Set` -> `m_DamageMin = damage; m_DamageMax = damage + damage/2` (`WZ/MagicInf.cpp:266-286`, the two at
`:282-283`), so **150..225**. `m_RequireEnergy[62] = 0` (`WZ/MagicDamage.cpp:750-752`, `MODIFY_DARKLORD_EARTHQUAKE_REQENERGY_BUGFIX_20060914`
on at `WZ/define2.h:1729`). The plan's row (docs/plans/dark-lord.md:60) says nothing of the 10 s delay or the AG.

### 1.2 Who may cast it: the horn grants it

1. **The item puts the skill on itself**: `CItem::Convert` -- `if (m_Type == MAKE_ITEMNUM(13, 4)) { m_Option1 = 1;
   m_Special[m_SpecialNum] = AT_SKILL_DARKHORSE_ATTACK; }` ("다크호스는 무조건 옵션이 들어 간다", the Dark Horse always carries the
   option, `WZC/zzzitem.cpp:1025-1031`), exactly as the Dinorant's 1018-1022 carries Fire Breath. OM: `CreatePet(4,
   SkillNumber.Earthshake, ...)` (`OM/.../VersionSeasonSix/Items/Pets.cs:66-71`), lords only (`:71`).
2. **Worn, it is in his list; off, it is not**: `gObjInventoryInsertItemPos` adds any equipped item's `m_Special[0]` when
   `m_Option1` is set (`WZ/user.cpp:16682-16694`); `gObjInventoryDeleteItem` removes it (`:14929-14943`). It is never saved as a
   learned skill: the DB skill list skips it (`WZ/user.cpp:3738-3751`, "장비에 있는 스킬은 여기서 추가 안함", skills on gear are not
   added here).
3. **Slot**: the Dark Horse is worn in `EQUIPMENT_HELPER` (WZ's slot 8, `WZ/ObjAttack.cpp:3229`, `:3805-3822`); our mounts have their
   own slot 12 (`sim::kMount`, docs/game/mount.md "Done").
4. **Riding**: WebZen has no separate "riding" test -- worn off a safe tile is riding; the cast is refused on a safe tile
   (`WZ/protocol.cpp:16782-16790`). **Horse life**: WebZen's server does **not** test the horse's durability for the cast (the
   skill stays in the list while it is worn); **the client refuses it at 0** (`MM/GameLogic/Combat/ClassAttack.cpp:536-541`,
   `:558-563`; the skill bar greys it, `MM/UI/NewUI/HUD/NewUIMainFrameWindow.cpp:2175-2183`, `:2279-2289`; 103
   `ZzzInterface.cpp:6489-6517`), and in Chaos Castle (`ClassAttack.cpp:543-555`). The client casts it only with the Dark Horse
   under him off a safe tile (`ClassAttack.cpp:907-913`, `if (c->Helper.Type != MODEL_DARK_HORSE_ITEM || c->SafeZone) break;`).
5. **Class**: `SkillDarkHorseAttack` asks no class; only a lord can wear 13/4 (its requirement: level `218 + 2·pet level`,
   `WZC/zzzitem.cpp:578-583`; OM `Pets.cs:66` 218).

### 1.3 The cast

The client sends **0x1E** (`SendRequestMagicContinue`, his own tile, his facing toward the aimed point,
`MM/GameLogic/Combat/ClassAttack.cpp:915-930`), so `CGDurationMagicRecv` (`WZ/protocol.cpp:16772-16891`): safe tile -> refused
(`:16782-16790`), skill looked up in his list (`:16793-16817`; a horse taken off has removed it), speed-hack check, `EnableSkill`
(62 is on its list, `WZ/ObjUseSkill.cpp:423-426`), then `UseSkill(aIndex, lpMagic, x, y, dir, TargetPos, aTargetIndex)`
(`WZ/ObjUseSkill.cpp:660-969`):
- **Mana**: `MODIFY_BUGFIX_20081119` (on, `WZ/define2.h:633`) sets `usemana = gObj[aIndex].Mana` for 62 -- "다크호스 어스퀘이크 스킬
  시전시 마나 소비되는 버그 수정" (fix: casting the Dark Horse Earthquake spent mana) (`WZ/ObjUseSkill.cpp:692-697`). **No mana.**
- **AG**: `gObjMagicBPUse` takes the row's **50 AG** (`:713-721`).
- **Delay**: `SkillDelay.Check` is asked (`:705-709`) but **`CSkillDelay::Check` returns TRUE on its first line**
  (`WZ/SkillDelay.cpp:28-31`): the server never enforces the 10 s. **The client does**: `CSkillManager::CheckSkillDelay` refuses
  while `SkillDelay[slot] > 0` and arms it with the row's 10000 (`MM/GameLogic/Skills/SkillManager.cpp:140-172`), asked at every cast
  (`MM/GameLogic/Combat/SkillCast.cpp:187`, `:217`, `:938`). So in MU it is a **10-second** skill.
- Echoes 0x1E to him and his viewport (`:723-760`), then `SkillDarkHorseAttack(lpObj->m_Index, aTargetIndex, lpMagic)`
  (`:837-840`). (Through 0x19 it would reach the same handler, `RunningSkill`, `:1168`.)

### 1.4 The handler (`WZ/ObjUseSkill.cpp:3779-3890`, `SkillDarkHorseAttack`)

1. `GCMagicAttackNumberSend(lpObj, 62, lpObj->m_Index, 1)` (`:3794`) -- a 0x19 **aimed at himself** (MuMain has no `ReceiveMagic`
   case for 62; the only client case is the 0x1E echo, section 3.1).
2. Every object in **his** viewport (`VpPlayer2`, `:3798-3888`) is a candidate if (`:3807-3846`): not a trap / gate / statue (Class
   100-110); a **monster not summoned** by anyone; or the aimed body or his duel partner; or, in a running Castle Siege, any player
   (`MODIFY_RANGEATTACK_CHECK_20050113` on, `WZ/define2.h:3237`); or (through its summoner) a guild-war enemy.
3. **Range**: `gObjCalDistance(him, it) < 5` (`:3850`); `gObjCalDistance` is `(int)sqrt(dx²+dy²)` on whole tiles
   (`WZ/user.cpp:7042-7053`), so **dx² + dy² < 25**: a disc of radius just under five tiles, **69 tiles, his own included** (9 + 2·9 +
   2·9 + 2·7 + 2·5 by column). **All round him** -- nothing about his facing is read. The plan's "5 tiles round him" is this.
4. **The cap that never runs**: `HitCount` is declared 0 (`:3786`) and tested (`> 15` none, `>= 10` a coin, `:3853-3867`) but **never
   incremented**, so every candidate is struck -- the same dead counter Force Wave's `loopcount` has. OM implements the intent (1.6).
5. **When**: each one goes through `gObjAddAttackProcMsgSendDelay(lpObj, MSG_ATTACKPROC_ENTIREMAGIC, it, 500, 62)` (`:3871-3874`):
   **500 ms later**, read by `gObjStateAttackProc` (`WZ/user.cpp:7368-7388`): **only if he is still alive**, `gObjAttack(him, it,
   lpMagic, magicsend 0, MSBFlag 1, 0, 0)` -- the list and the range are fixed at the cast; a body that walked out in the half second
   is still struck, one that walked in is not. One blow each. No wall test. `magicsend 0`: no per-victim echo, only the number.

### 1.5 The push (`WZ/ObjBaseAttack.cpp:594-599`)

`ResistanceCheck` -- called at the head of every `gObjAttack`, **before the miss roll** (`WZ/ObjAttack.cpp:578` then `:621-648`) --
does `if (skill == AT_SKILL_DARKHORSE_ATTACK) gObjBackSpring2(lpTargetObj, lpObj, 3);`. So **every struck body is pushed, hit or
missed**. `gObjBackSpring2` (`WZ/user.cpp:7663-7837`):
- immune: admins, castle NPCs and mercenaries, Blood Castle statues/gates (131-134), Crywolf altars/statues and its Tanker, Kundun
  (275), Raklion's Selupan and eggs (459-462), a body stoned / stunned / restricted (`:7676-7796`);
- direction: `GetPathPacketDirPos(it − him)` -- **straight away from him**, one of eight (`:7798`);
- steps: `count` 3, but the third step only `if (lpObj->Class == CLASS_DARKLORD || CLASS_MAGUMSA || CLASS_KNIGHT)` (`:7805-7817`):
  **2 tiles** for monsters and for wizards/elves, **3 for knight-type players**. (A monster's `Class` is its monster number, so by
  accident monsters numbered 1, 3, 4 take three.)
- each step `BackSpringCheck` (`:7633-7660`): onto a safe, blocked, hollow or water tile it does not move and **turns round** (`tdir +=
  4`), so the next step goes back toward him -- a bounce off walls.
- `RecvPositionSetProc` puts it there at once (`:7819-7835`); the client slides it.
No stun in 1.00.93 (OM's stun is the master-tree Mastery's, `SkillsInitializer.cs:81`).

### 1.6 Damage (`WZ/ObjAttack.cpp`)

- Skill band `lpMagic->m_DamageMin/Max`, 150..225 (`:3151-3154`), + set-option skill attack (`:3163-3167`), + **the attacker's own
  lightning resistance bonus** `m_AddResistance[2]` (the row's element, `:3169-3190`; a WebZen quirk on every elemental skill),
  + harmony (`:3200-3207`).
- **Then the else-if chain** (`:3209-3269`): Electric Spike's head, then **`else if (lpMagic->m_Skill == AT_SKILL_DARKHORSE_ATTACK)`**
  (`:3221-3227`): `if (pInventory[EQUIPMENT_HELPER].m_Type == MAKE_ITEMNUM(13,4))` ("다크호스를 타고 있어야 된다", must ride the Dark
  Horse) **+ Str/10 + Command/5 + pet level·10** to both ends (`:3228-3245`; a commented-out scepter share, `:3235-3238`). The lord's
  flat **Str/25 + Ene/50 is the chain's last `else`** (`:3259-3269`), so **Earthshake does not get it**. Horse absent (impossible
  in practice): no bonus at all. No durability test here.
- Weapon swing + that, before defence; then `case AT_SKILL_DARKHORSE_ATTACK:` ×**(200 + Ene/20)%** with Force, Fire Burst, Electric
  Spike, Fire Scream (`:1452-1464`). The usual hit roll, critical / excellent, defence, shield. Physical
  (`gObjCheckAttackTypeMagic` -> FALSE for 62, `WZ/user.cpp:32745-32756`).
- So the plan's "+Str/10 + Cmd/5 + 10·horse level" is **verified**; add: **instead of** Str/25 + Ene/50, and the multiplier is the
  lord's ×(200 + Ene/20)%.
- Horse level: `m_PetItem_Level`, 1 at birth (`WZC/zzzitem.cpp:94-96`), 1..50 (`MAX_DARK_HORSE_LEVEL`, `:3237-3251`).

### 1.7 OpenMU differs

| | WebZen 1.00.93 | OpenMU | wins |
|---|---|---|---|
| Who | viewport monsters with whole-tile distance < 5 of him, every one | attackables within 10 of the packet's point (his tile) **and** within diameter 10 / 2 = 5 of it (`SkillsInitializer.cs:181` `targetAreaDiameter: 10`; `GameLogic/PlayerActions/Skills/AreaSkillAttackAction.cs:129-160`, the filter at `:145-148`) -- the same disc | same |
| Cap | none (dead counter) | **9 sure, then each further one a coin, up to 15** (`minimumHitsPerAttack: 9, maximumHitsPerAttack: 15`, `:181`; `AreaSkillAttackAction.cs:236-237`, `:306-323`) -- WebZen's intent | WZ (code) |
| The aimed body | only if inside the disc | **always added** if within range + 2 = **12** (`:109-115`) | WZ |
| When | 500 ms after the cast | at once (no delay settings) | WZ |
| Push | 2 tiles (3 for knight types), bounces off walls, even on a miss, before the roll | `AfterTargetGotAttackedAsync`: 3 tiles straight away (random if on his tile), **stops** at an unwalkable or (for a monster) safe tile, only a live target (`EarthShakeSkillPlugIn.cs:25-56`) | WZ distance; OM's stop is saner |
| Damage | 150..225 + Str/10 + Cmd/5 + 10·level, ×(200 + Ene/20)% | 150 + Str/10 + Leadership/5 + 10·HorseLevel (`Updates/FixDamageCalcsPlugInSeason6.cs:297-299`; not in the lord flat list `:307-312`); DL skill multiplier 2 + Ene·0.0005 (`CharacterClasses/ClassDarkLord.cs:83`, `:130`) | same |
| Cost | 0 mana, 50 AG | 50 AG (`SkillsInitializer.cs:180`) | same |

## 2. Hit shape

- **A disc round his tile**: every tile with dx² + dy² < 25 (radius 4.9), 69 tiles. Rows by |dx|: 0-2 reach |dy| ≤ 4, 3 reaches 3,
  4 reaches 2. **Behind him exactly as much as in front.** Nothing aims it: the 0x1E's target point only turns him
  (`ClassAttack.cpp:928`); the server reads his tile.
- **Reach to aim**: distance 10 (+2 on the Dark Horse, `MM/GameLogic/Skills/SkillManager.cpp:128-138`) is only how far the clicked
  point may be; it moves nothing.
- **When**: all bodies in the list at once, **500 ms after the packet** (zero ping: 12.5 frames after the press).
- **Then**: each pushed 2 tiles outward (1.5), so the disc empties into a ring 5-7 tiles round him.

## 3. Client (MuMain S6, cross-checked with 1.03.35)

### 3.1 The cast and the clips

- **Hero** (`MM/GameLogic/Combat/ClassAttack.cpp:907-958`): only with `MODEL_DARK_HORSE_ITEM` and off a safe tile; `CheckTile` in
  reach, turns him to the target point, sends 0x1E, `SetAttackSpeed()`, **`SetAction(o, PLAYER_ATTACK_DARKHORSE)` and
  `PlayBuffer(SOUND_EARTH_QUAKE)` at the press** (`:948-952`).
- **Everyone, on the 0x1E echo** (`ReceiveMagicContinue`, `MM/Network/Server/WSclient.cpp:5184-5189`): the same clip and sound.
- **The rider's clip**: `PLAYER_ATTACK_DARKHORSE`, raw **87** (counted from `MM/Core/Globals/_enum.h:2989` `PLAYER_ATTACK_STRIKE` = 80:
  78 `STOP_RIDE_HORSE`, 79 `RUN_RIDE_HORSE`, ... 87 `ATTACK_DARKHORSE`, 88/89 the two horse idles, `:2987-2998`). **11 keys**
  (`content/players/rig/player.rig.json` action 87). Play speed **0.3** at run time (`MM/Engine/Object/ZzzCharacter.cpp:1013`, a
  constant, no attack speed; ZzzOpenData's load-time 0.2, `MM/Engine/Object/ZzzOpenData.cpp:384`). `content/players/rig/actions.json`
  labels it one slot high, as "87 Attack ride attack magic" at 0.3 -- the phase-3 speed fix already gave the real 87 its 0.3
  (docs/plans/dark-lord.md:236-237). Last key 10/0.3 ≈ **33 frames, 1.33 s**.
- **The let-go**: `AnimationFrame >= 5` on that clip -> `AttackTime = 15` (`MM/Engine/Object/ZzzCharacter.cpp:2896-2903`): **key 5 of
  11**, 5/0.3 ≈ 17 frames, **0.67 s**. Nothing is spawned or sent there -- the server already has the cast; the let-go only ends
  the client's attack state.
- **The horse**: while its rider plays 87, GOBoid sets the Dark Horse's **clip 3 at velocity 0.34** (`MM/Engine/AI/GOBoid.cpp:338-342`;
  103 `GOBoid.cpp:394-398`). `MMD/Skill/DarkHorse.bmd` (version 0x0C): **13 meshes, 60 bones, 7 clips of 7, 7, 6, 10, 41, 51, 31 keys**
  (parsed for this dossier), so clip 3 is **10 keys, ~29 frames (1.18 s)**. Its other clips: 1 run, 2 the ride strikes, 5/6 the
  idles at 1.0, 0 standing at 0.3 (`GOBoid.cpp:343-428`). A white `CreateBlur` trail off its **bone 9** every frame of clip 3
  (`:452-462`; owner `Hero`, a bug for onlookers).

### 3.2 What spawns -- `RenderDarkHorseSkill` (`MM/Engine/AI/GOBoid.cpp:727-769`; 103 `GOBoid.cpp:856-895`)

Run every drawn frame of the horse's clip 3 (`GOBoid.cpp:464-467`). `o->WeaponLevel` (a `BYTE`, `MM/Engine/Object/w_ObjectInfo.h:146`)
counts its frames, zeroed whenever the rider is not in 87 (`:423-427`):
1. **The shock wave**: `CreateEffect(BITMAP_SHOCK_WAVE, horse position)` sub-type 0 **every 400 ms** (`HorseEffectInterval`,
   `:727`, `:738-742`; 103: every tenth `MoveSceneFrame`, `:866-869`). Born `LifeTime 30, Scale 20` (`MM/Render/Effects/ZzzEffect.cpp:3538-3543`),
   `Scale −= 1` a frame (`MM/Render/Effects/Behaviors/MoveHandlers.cpp:5766-5769`), drawn **on the terrain** by
   `RenderTerrainAlphaBitmap` (`ZzzEffect.cpp:10030-10040`; `MM/Render/Terrain/ZzzLodTerrain.cpp:2402-2440`: the sheet spans `Scale`
   terrain cells): a **ground ring 20 tiles across closing to nothing in 20 frames** (a negative size draws nothing), round him;
   ~3 of them over the clip. Sheet `Effect/ShockWave.jpg` (`MM/Engine/Object/ZzzOpenData.cpp:5193`; ours: the `shockwave` sheet
   `fx/golem_water.cpp:45`, `fx/nova.cpp:25`).
2. **The Rageful-Blow crater** at `WeaponLevel == 19` (`:764-768`; 103 `:889-894`, "분노의 일격의 중앙 틈들", Rageful Blow's central
   cracks): `CreateEffect(MODEL_SKILL_FURY_STRIKE, horse position, ..., SubType 0, Owner horse, PKKey −1, SkillIndex 0, Skill 2)` --
   the last argument is `o->Kind` (`ZzzEffect.cpp:334`, `:373-374`). **Kind 2** of the knight's Fury Strike (create
   `ZzzEffect.cpp:1866-1880`, move `MoveHandlers.cpp:2940-3177`): **no weapon** (drawn only for Kind 0, `ZzzEffect.cpp:8650-8652`), no
   tails, sparks or MODEL_WAVE (Kind 0 only, `MoveHandlers.cpp:2993-3012`, `:3122-3151`); at its LifeTime 11 (9 frames on) the crater
   at **the horse + (−25, −80) turned by its facing -- 0.8 tile in front of him, a quarter tile aside** (`:2958-2970`), a
   `BITMAP_EXPLOTION` at 0.5 (`:2991`), crater pieces EarthQuake03/01/02 (`:3016-3024`) and five patches 04/05 100-250 units round
   it (`:3026-3055`); at LifeTime 10 the five four-segment cracks 07/08 and **SOUND_FURY_STRIKE3** (`:3057-3108`). That is
   `src/game/fx/fury.h` without its weapon, tails and sparks. It then sets `WeaponLevel = −3` (253), which wraps to 0 three frames
   later.
3. **The rock spikes** while the horse's frame is **8 to 9.5** (`:744-763`): six `MODEL_GROUND_STONE` or `_STONE2` at random, on a
   ring round the horse at `150 · (WeaponLevel / 2)` units, 60 degrees apart from a random start -- **all round him**. Counting from
   the wrap: frames 8-9.5 of clip 3 at 0.34 are its 25th-28th drawn frames, `WeaponLevel` 3-6, so rings at **150, 300, 300, 450**
   units, each frame on a coin (`rand_fps_check(2)`, `MM/Engine/AI/ZzzAI.cpp:706-709`). **1.03.35 makes it fixed**: odd
   `WeaponLevel` only (`103/GOBoid.cpp:871-886`) -> **two rings of six, at 1.5 and 3 tiles**, about **1.0 and 1.08 s** after the
   press. A stone (`ZzzEffect.cpp:3480-3500`): refused on no-move / no-ground / water tiles; `LifeTime 40`, scale **1.2-1.49**, a
   random yaw, its one 5-key clip at 0.3, not looping (`ZzzOpenData.cpp:4321-4324`); at LifeTime 33-36 smoke (`BITMAP_SMOKE` 11)
   and `MODEL_STONE1/2` chips sub-type 10 thrown off it; fading `/1.3` alpha and `/2` light a frame in its last 8; a warm terrain light
   (0.79, 0.72, 0.49) radius 2 (`MoveHandlers.cpp:5687-5714`). **The camera**: `EarthQuake = (rand()%3 − 3)·0.7` -- −0.7 to −2.1
   degrees of pitch -- every frame of that window (`:762`), plus the crater's own EarthQuake01 shake (fx/fury.h).
4. Nothing on the rider; nothing at the let-go.

**Timeline from the press** (25 fps, no ping): 0 clip, sound, first ground ring, packet; **0.5 s the server's numbers** (and pushes);
0.4/0.8/1.2 s the next ground rings; 0.67 s let-go; 0.76 s the crater driver; **~1.0-1.1 s the spikes and the camera jolt**; ~1.12 s
the crater and its explosion; ~1.16 s the cracks and eRageBlow_3; 1.33 s the rider's clip ends. **In MU the numbers come about half
a second before the ground breaks** (the footage shows less, section 5).

### 3.3 Sounds

- `SOUND_EARTH_QUAKE` = **`Data/Sound/sDarkEarthQuake.wav`** (`MM/Engine/Object/ZzzOpenData.cpp:4874`): **3.81 s, stereo, 22,050 Hz**
  (`afinfo`), at the press and the echo. **Not in `content/sounds/sounds.json`**: the showing cook.
- `SOUND_FURY_STRIKE3` (`eRageBlow_3.wav`) from the crater: ours already, `rage_blow_3` (`content/sounds/sounds.json:2302-2306`).
- The horse's hooves `pHorseStep1-3` (riding, not the skill): ours, `mount_hoof` (`sounds.json:2308`).

### 3.4 Files (all in MMD)

| Thing | File | In our content? |
|---|---|---|
| the ridden horse | `Skill/DarkHorse.bmd` + 13 sheets `dkfurop.OZT`, `dkfrontleg`, `dkhead2`, `dkhip`, `dksaddle`, `dkbackleg`, `dkneck`, `dkfrontShd`, `dkhorsebase`, `dkfrontleg2`, `dkbackleg2`, `dkheadwing`, `dkneck2` (`.OZJ`) | **no** |
| the horn (bag item) | `Item/DarkHorseHorn.bmd` (dkhead2, dkheadwing; `ZzzOpenData.cpp:719`) | **no** |
| Spirit of Dark Horse | `Item/DarkHorseSoul.bmd` (`change.jpg`, `:720`) | **no** |
| rock spikes | `Skill/GroundStone.bmd`, `GroundStone2.bmd` (2 meshes each, `DhorSS.OZJ` + `DhorSS_R.OZJ`, 5-key clip) | **no** |
| stone chips | `Skill/Stone01/02.bmd` | yes, `content/effects/meteor/Stone01/02` |
| crater, patches, cracks | `Skill/EarthQuake01-05, 07, 08.bmd` + fury sheets | yes, `content/effects/fury/` (06 is not used by Fury) |
| ground ring | `Effect/ShockWave.jpg` | yes, the `shockwave` sheet |
| explosion | `Effect/Explotion01.jpg` | yes, `content/effects/meteor/explotion01.png` |
| cast sound | `Sound/sDarkEarthQuake.wav` | **no** |
| icon | `newui_skill2`, 62 − 57 = **cell 5** (`MM/UI/NewUI/NewUIMuHelper.cpp:1771-1776`) | **no**: `62: ("Dark Lord", "Earthshake")` in `pipeline/skill_icons.py` beside `:124-134` |

## 4. 1.03.35 differences

Behaviour the same, two drawing details differ: the ground ring on every tenth global `MoveSceneFrame` instead of every 400 ms of
its own (`103/GOBoid.cpp:866-869`), and **the spikes on odd frame counts only**, so exactly two rings (1.5 and 3 tiles) where MuMain
throws a coin each frame (`:871-886`). Cast `103/ZzzInterface.cpp:6985-7039` (`MODEL_HELPER+4`, the same clip and sound), echo
`103/WSclient.cpp:6264-6272`, let-go key 5 `103/ZzzCharacter.cpp:3238-3247`, horse life gate `103/ZzzInterface.cpp:6489-6517`
-- MuMain's line for line. 1.03.35 also carries Season 4's master variants (`AT_SKILL_ASHAKE_UP..+4`) and Vermont's monster copy
(`RenderSkillEarthQuake`, `103/GOBoid.cpp:898-`), not ours. **The better source for the spikes is 1.03.35** (deterministic).

## 5. Footage

Clips:
- `cBZEJlW0oy0` **"Mu Online Dark Lord Skill Dark Horse"** (Webzen's own channel, the series of Force's `v7Z2mh3qJho` and Fire
  Scream's `nJygYJ7GiNQ`), 21 s, 360p: a lord on the Dark Horse casting about eight times at a training dragon. The clearest. The
  cycle is about **2.5 s** -- a modern client, not MU's 10 s delay.
- `ABZexbQJoAs` "earthquake rapido y seguido" (muv2fans), 16 s, 360p, a crowded Castle-Siege-like yard, players and monsters round a
  lord: the spikes, the ground cracks and many bodies at once.
- Looked at, not used: `tZMn2ntXqiQ` (a private server's "rework"), `k5cKyEgxXU0` / `x0uFOSISlSQ` (bug clips), `aiPL5yVT4FA` (Lord
  Emperor combo, Season 3+).

Stills (`earthshake-footage.jpg`; top row `cBZEJlW0oy0`, bottom row `ABZexbQJoAs`):

| # | time | what is seen |
|---|---|---|
| 1 | 0.5 s | he sits the black horse at the dragon's flank, scepter low; the floor's cyan pattern |
| 2 | 1.2 s | **the horse rears**, scepter raised -- clip 3 / rider 87 |
| 3 | 1.6 s | the horse down; a pale glow on the floor round its hooves (the shrinking ground ring); a new damage number (29,160) on the dragon at ~1.53-1.6 s |
| 4 | 1.9 s | **the spikes**: a ring of tall brown rock cones round the horse -- in front, to the sides **and behind him** (screen left/bottom), the nearer ring at about a tile and a half, the far at about three; dark chips flying |
| 5 | 2.4 s | spikes still standing, fading; the camera has jolted (the frame shifts down) |
| 6 | 7.0 s | the yard: a bright pale disc on the ground round the lord (the ground ring) |
| 7 | 7.5 s | the spikes ring him, players and monsters among them on every side |
| 8 | 8.5 s | the spikes at their tallest, two rings round him |
| 9 | 9.5 s | spikes gone; **glowing orange cracks** in the ground a tile ahead of him -- the Fury crater and its five cracks |
| 10 | 10.5 s | the cracks fading, the next cast's ring beginning |

Read: **the shape is round** -- spikes and the ground ring encircle him in both clips, nothing favours his facing but the crater.
**Timing**: in Webzen's clip the number lands **~0.1 s before the first spikes** (frames at 1/15 s: the number at 1.53-1.6 s, the
spikes from 1.67 s) -- closer together than the code's 0.5 s server delay against the spikes' ~1.0 s (that client's clip speeds
differ from MuMain's). The cracks come after the spikes, as the code orders them. Pushes cannot be told (the dragon does not move;
the yard is too crowded).

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

### 6.1 What exists of the Dark Horse: nothing

- **Content**: `content/items/pets/` holds Helper01-04 (Angel, Imp, Uniria's horn, Dinorant's horn) and Rider01/02 (Uniria, Dinorant)
  only; no DarkHorse, DarkHorseHorn, DarkHorseSoul, no `dk*` sheet (the `content/textures/dk_*` files are the knight's body).
- **Sim**: `placeOf` sends only 13/2 and 13/3 to `kMount` (`src/sim/items.cpp:922`); `petPower` knows 13/0-3 (`:102-137`); `canFly`
  asks 2 and 3 (`:91-100`); `timedPet` 0-2 (`:139-141`). No pet level or pet exp anywhere (`sim::Held` has none, `src/sim/items.h:71-95`;
  the plan makes them new kept bytes, docs/plans/dark-lord.md:87-89).
- **Drawing**: `src/game/pets.h` draws Rider01/02 (`:18`, `:38` `kDinorantLift`, `:76`). No third mount.
- **Rider clips**: in player.muc already (all 284 actions are in `player.rig.json`): 78 stop-ride-horse (6 keys), 79 run-ride-horse
  (6), 82 ride-strike (6), 87 Earthshake (11), 88/89 horse idles (51, 31). MuMain plays 78/79 for a lord on the Dark Horse
  (`MM/Engine/Object/ZzzCharacter.cpp:271-273`, `:559`, `:1108`). **87 is not on cook.py's clip list** (`pipeline/cook.py:2327`), so
  `sim::castTicks` does not know its length.

### 6.2 The mounted-skill precedent (Fire Breath, skill 49)

- `SkillRow::mounted` (`src/sim/skills.h:453-458`): known while a horn with life is worn, no learned bit, nothing saved, thrown only
  while ridden. Fire Breath's row (`src/sim/skills.cpp:789-807`): `.mounted = true`, `.anyHand = true`, a 4 s cooldown (ours).
- `Realm::knows` -> `row.mounted ? skillFor(row, kin) && dinorantWorn()` (`src/sim/realm_skills.cpp:77-85`) and `dinorantWorn` **is
  hard-wired to 13/3** with durability > 0 (`:87-95`). The cast gates: `:137` (known), `:189` (`row.mounted && !hero.riding`),
  `:837-840` (`armed`). **Earthshake needs the horn generalised**: a `mount` item number on the row (3 for Fire Breath, 4 for
  Earthshake) and `mountWorn(number)` in place of `dinorantWorn()` (declared `src/sim/realm.h:946`). Durability > 0 is MuMain's
  client rule exactly (1.2.4). `Body::riding` comes from `pet.mount` (`src/sim/realm_move.cpp:155`), so 13/4's `petPower` must set
  `mount = true`.

### 6.3 Where it would branch, and what is reusable

- **Row**: `skill::kEarthshake = 62` in `src/sim/skills.h` beside the lord's (`:124-148`); a row on the end of `kRows` after Fire
  Scream (`src/sim/skills.cpp:986`), `kSkills` 46 -> **47** (`src/sim/skills.h:547`) and the kept layout's cools (the plan's one
  bump, docs/plans/dark-lord.md:217). `.kin = Kin::DarkLord`, `.mounted = true` (with the horn's number), `.anyHand = true`,
  `.clip = 87`, `release` 5/11, `.damage = 150`, mana 0, `spread` round.
- **Release**: `Realm::release` (`src/sim/realm_fight.cpp:1305-1399`) has per-skill branches at its head (Electric Spike `:1315`,
  Force Wave `:1320`, Fire Scream `:1325`, Fire Burst `:1356`); Earthshake is one more, `Realm::earthshake`, before the generic
  `strikeAround` (`:1365-1368`, which serves Cyclone's `Spread::Ring` of one tile, `src/sim/skills.h:271-281`). It gathers
  monsters with whole-tile dx² + dy² < 25 of his tile, and `loose(..., wait)` each with the chosen wait (open call 3), as Fire
  Scream and Electric Spike already wait per body. Victims are capped by `kVictims` 24 (`src/sim/skills.h:563`) -- above OM's 15.
- **Damage**: the lord's flat in `src/sim/realm_fight.cpp:50-68` is already an else-if on Electric Spike, WebZen's own chain; add
  `kEarthshake ? p.strength/10 + p.command/5 + 10·horseLevel`. The ×(200 + Ene/20)% comes with `Kin::DarkLord`
  (`kLordForcePerEnergy`, `src/sim/skills.cpp:999`, `:1111`).
- **Push**: `Realm::push` (`src/sim/realm_skills.cpp:1149-1173`) is one tile straight away, refused onto a wall or a safe tile, no
  dice drawn, `say(What::Shoved)`; Earthshake wants a 2-tile variant (open call 4). `fixed`/`shrugs` already spare the raid's dragon
  and statues (`:1150`).
- **Drawing**: `src/game/fx/fury.*` is the crater (needs a weaponless, tail-less, spark-less "Kind 2" `cast`); `fx/quake.h` the camera
  (`fury_.quakeFor`, `src/game/play.cpp:2543`); the `shockwave` sheet the ground ring (`fx/nova.cpp:25`, `fx/golem_water.cpp:45`);
  `fx/meteor`'s stones the chips (`src/game/play.cpp:2919`). New: the GroundStone spikes (`fx/earthshake.*`), the horse's blur.
- **Runes and talents**: Tremor / Quake / Earthbreaker are "earthshake around you" procs (docs/plans/dark-lord.md:99) -- they will
  reuse this look; the talent column's "while mounted" hold (`:200-202`).

### 6.4 What must be built of the Dark Horse before Earthshake can be shown at all

1. **Cook the horse**: `Skill/DarkHorse.bmd` (13 meshes, 60 bones, 7 clips) with its 13 `dk*` sheets into `content/items/pets/` as
   Rider01/02 were (docs/game/mount.md step 1), and `Item/DarkHorseHorn.bmd` as the bag item carrying the 13/4 row (lords only,
   level 218); a Blender build -- ask first.
2. **Sim**: 13/4 in `placeOf` -> `kMount`, `petPower` (mount, and the horse's own defence / absorb from plan section 4), its wear,
   the class gate; `Body::riding` follows.
3. **Ridden**: `game/pets.h` a third mount -- its clip off his (0 stand at 0.3, 1 run, 2 ride strikes, 3 Earthshake, 5/6 idles at 1.0,
   GOBoid.cpp:338-428), the rider on 78/79 (stop / run ride horse) and 82 for strikes, scale 1.0; hooves already (`mount_hoof`).
   Clip 87 on cook.py's list.
4. **A way to have one** for the studio: `--give` the horn (the Spirit, the Trainer and the breeding are phase 5). Pet level can
   wait: MU's horse is born level 1 (`WZC/zzzitem.cpp:95`), so +10.

Only then the skill: row, sim, `GroundStone/GroundStone2.bmd` + `DhorSS` sheets cooked, `sDarkEarthQuake.wav` through the showing
cook, icon cell 5, and the fx.

## 7. Open calls (for the design; not decided here)

1. **Round, against "only frontal"** -- the conflict. MU strikes a disc all round him (1.4.3), pushes outward on every side (1.5), and
   draws the ground ring and both spike rings round him, behind him included (3.2; stills 4, 7, 8); only the crater is in front.
   "Only frontal" was said of Force's flare (docs/plans/dark-lord.md:238). Options: (a) **MU's disc and MU's round look**; (b) a
   front half-disc and front-only spikes (invented; no MU source draws or strikes it). **Recommended: (a), and ask the user
   explicitly** -- "DL has to be true on actual MU" and "the hit lands with the visual" both say round; a frontal Earthshake would
   strike nothing behind him while MU's spikes there are the skill's whole picture.
2. **Who and how many**: WebZen strikes every monster in the disc (cap dead); OM 9-15. **Recommended: every monster in the disc,
   monsters only (as Electric Spike and Force Wave here), capped only by `kVictims` 24** -- WebZen's code; the aimed body only if it
   stands in the disc (WZ), not OM's 12-tile extra.
3. **When the numbers come**: WebZen 500 ms after the cast (10 ticks); MuMain's spikes ~1.0 s (horse frames 8-9.5, ~20 ticks);
   Webzen's footage puts them within ~0.1 s. **Recommended: the spikes' moment** -- every struck body on the tick the first spike
   ring rises (1.03.35's first odd frame, ≈ 25 frames ≈ 20 ticks after the cast), ours and marked, so the number comes with the ground
   breaking ("the hit lands with the visual"); and draw the spikes off the sim's say, not the clip, so the two cannot part. The
   alternative, MU's 10 ticks, puts numbers up half a second before anything breaks.
4. **The push**: WebZen 2 tiles away (3 for knight-type players), on a miss too, before the roll, bouncing back off walls; OM 3,
   stopping at walls; ours today 1 (`Realm::push`). **Recommended: WebZen's 2 tiles straight away, every struck monster hit or
   missed, slid as `push` slides; a wall or safe tile stops it** (the bounce back toward him is a quirk; stopping is OM's). Bosses and
   fixed bodies spared as now.
5. **Cooldown and cost**: MU 0 mana, 50 AG, 10 s (client). QWER skills have cooldowns here. **Recommended: MU's 10 s (200 ticks) as the
   cooldown -- MU's own number, for once the same as our rule; no mana; no AG** (none exists here; Fire Scream dropped its AG,
   fire-scream.md section 8.5).
6. **Damage**: **Recommended: MU's** -- 150..225 + Str/10 + Command/5 + 10·horse level in place of Str/25 + Ene/50, ×(200 +
   Ene/20)%, physical; WebZen's lightning-resistance add is dropped (a quirk on every elemental skill, nothing here has it).
7. **Horse level** before pet levels exist: **Recommended: level 1 (+10) until phase 5's pet levels**, MU's newborn horse.
8. **Horse life**: WebZen's server ignores it; MuMain refuses at 0. **Recommended: MuMain's -- refused at no horse life** (it is
   what `dinorantWorn` does for Fire Breath).
9. **Aim**: MU needs a target point in reach 12 (it only turns him). **Recommended: castable with no body under the pointer**, as a
   ring skill; he turns to the pointer, the disc is round him.
10. **The let-go and the clip**: 87 at 0.3, key 5 of 11 (0.67 s), held by `castUntil` while it plays; the horse's clip 3 driven off
    it. **Recommended: as MU**; the strike waits for the spikes (call 3), not for key 5.
11. **The crater's place**: MU lays it 0.8 tile ahead of him -- the one frontal piece. **Recommended: keep it** (MU's, and not a flare
    behind him).
12. **Mounted only**: MU's rule and the plan's. Off the horse he has no Earthshake; on a safe tile not either (`Body::riding`).

Blocks a build: **the Dark Horse does not exist** -- no model, no horn, no item row, no mount power, no drawing (6.4 items 1-4);
after it, the row needs `kSkills` 47 and the kept layout's cools, and the showing cook for `sDarkEarthQuake.wav`. Everything else
(the release branch pattern, the push, fx/fury, the camera, the shockwave sheet, the stone chips) exists.
