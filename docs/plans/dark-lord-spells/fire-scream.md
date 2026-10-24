# Fire Scream (skill 78) and Explosion (79) -- research dossier

Researched 2026-10-24 before any build (the user: "we do very deep reserach for each DL spell then we make it").
Nothing here is built. Drafted by a research agent from the sources below; every number is read, not recalled.

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`. **WZP** = `.../ws/Source/Base Common Src/include/public.h`.
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- MU time: one reference tick = 1/25 s. Our realm: 20 ticks/s.

Names: WZ `AT_SKILL_FIRESCREAM = 78` and `AT_SKILL_EXPLOSION = 79` ("폭발 -- used for Fire Scream's explosion damage",
`WZP:288-289`); MM `AT_SKILL_FIRE_SCREAM` / `AT_SKILL_EXPLODE` (`MM/Core/Globals/_enum.h:378-379`, the enum after
`AT_SKILL_INFINITY_ARROW` 77); 103 `AT_SKILL_DARK_SCREAM` / `AT_SKILL_EXPLODE` (`103/_enum.h:1963-1964`); OM
`SkillNumber.FireScream = 78`, `Explosion79 = 79` (`OM/Persistence/Initialization/Skills/SkillNumber.cs:87-88`). Compiled under
`ADD_SKILL_DARKLOAD_FIRESCREAM_20060601` (`WZ/define2.h:1845`, "[update] Dark Lord skill Fire Scream added") -- a Season 2
skill. Effect models `MODEL_DARK_SCREAM` (darkfirescrem**02**) and `MODEL_DARK_SCREAM_FIRE` (darkfirescrem**01**).

**The headline**: Fire Scream is **not a server-shaped area skill**. The client flies three ground-hugging fire lanes out of him and
itself sends the server the list of whatever each lane's head passes within one tile of; WebZen deals one plain DL blow to each
body named. The damage number therefore comes **when the fire reaches the body** -- MU's own timing is the user's rule. And
WebZen's Explosion is rolled on **every landed Fire Scream blow** (3.01%), not only on a double-damage one: the "double damage"
call is there too, but a second, unconditional call at the end of every attack is the one that always runs (section 1.4).

## 1. MU's rule (server)

### 1.1 The row

**WebZen's server files carry no row 78 or 79.** `WZD/lang/main/skill.txt` (its header dated 2011-03-08) jumps 73 -> 200, and
`lang/kor/skill(Kor).txt`, `chs`, `jpn`, `tai` likewise stop before 78 (checked by listing every row number of all five). So the
numbers come from the client table and OpenMU:

| Field | MuMain `Skill.bmd` rec 78 | OpenMU (`OM/Persistence/Initialization/VersionSeasonSix/SkillsInitializer.cs:199`) | wins |
|---|---|---|---|
| Level | **102** | none (the orb's drop level 102, `Items/Orbs.cs:61`) | client |
| Damage | **130** (rolled 130..195, `WZ/MagicInf.cpp:237-238`) | 130 | -- |
| Mana / AG | **45 / 10** | 45 / 10 | -- |
| Distance | **6** | 6 | -- |
| Delay | **0** | -- | -- |
| Energy | raw **32** -> 20 + 32·102·4/100 = **150** by `WZ/MagicDamage.cpp:663-677`, but WebZen then sets `m_RequireEnergy[78] = 0` and `[79] = 0` (`WZ/MagicDamage.cpp:745-747`) -- **no energy asked on the server** | skill 70, orb 32 raw | see below |
| Command | **70** | skill 150, orb 70 | client |
| Class | DL only | `AllLords` | -- |

`MMD/Local/Skill.bmd` decoded as the Summon dossier did (80-byte records, XOR FC CF AB): rec 78 = `66 00 82 00 2d 00 0a 00 06 00 ...
20 00 00 00 46 00` -> level 102, damage 130, mana 45, AG 10, distance 6, delay 0, energy 32, Command 70, DL bit set.
Rec 79 (Explosion) = everything 0 but **distance 2** and the DL bit (OM agrees: `SkillsInitializer.cs:201`, `distance: 2`).
OpenMU's skill row has energy 70 / Command 150 and its orb energy 32 raw / Command 70: the skill row looks **swapped**; the client
table and OM's own orb agree on Command 70. The client would ask **150 energy** (its own formula on raw 32); WebZen's server asks none.

**Scroll**: (12,35): `case 35: skillnumber = AT_SKILL_FIRESCREAM` (`WZ/MagicInf.cpp:195-198`); MM `ITEM_SCROLL_OF_FIRE_SCREAM =
ITEM_WING + 35`, model `Data/Item/SkillScroll` (`MM/Core/Globals/_enum.h:2636`, `MM/Engine/Object/ZzzOpenData.cpp:1065`). OM
"Scroll of Fire Scream", drop level 102, energy 32 raw, Command 70, **300,000 zen** (`OM/.../VersionSeasonSix/Items/Orbs.cs:61`).
It drops from monsters in Season 2: 35 is in the scroll `BallTable` (`WZ/MonsterItemMng.cpp:217-224`,
`MODIFY_ITEM_DROP_FIRESCREAMSCROLL_20060830` on, `WZ/define2.h:1784`).

### 1.2 How it is cast -- two packets

1. **The cast, 0x1E** (`CGDurationMagicRecv`, `WZ/protocol.cpp:16772`): refused on a safe tile (`:16781-16788`,
   `MODIFY_SAFEZONE_CHECK_ATTACK_20080903` on, `WZ/define2.h:801`). `UseSkill(aIndex, lpMagic, x, y, dir, ...)` (`WZ/ObjUseSkill.cpp:662`)
   pays mana and AG, stamps `UseMagicTime` and zeroes `UseMagicCount` (`:715-720`), and echoes 0x1E (his tile and facing) to him and
   his viewport (`:725-786`). **Nothing is struck here.** Fire Scream is in `EnableSkill`'s list (`:446-448`).
2. **The hits, 0x1D/`PACKET_MAGIC_ATTACK`** (`CGBeattackRecv`, `WZ/protocol.cpp:16156`), sent **by the client** each time a lane
   finds bodies (section 3.4):
   - at most **5 targets a packet** (`:16174`); refused on a safe tile (`:16180-16187`);
   - refused if more than **8 s** since the cast (`:16292-16308`) or after **5 packets** for that cast (`UseMagicCount > 4`,
     `:16309-16314`);
   - Fire Scream is on the list that may come this way (`:16363-16382`) and, like Triple Shot, goes through the normal attack's
     speed-hack timer (`:16384-16502`);
   - the anti-hack `CheckFireScreamSkill` (`WZ/MultiAttackHackCheck.cpp:149-191`, on by `EnableCheckPenetrationSkill = 1`,
     `WZD/commonserver.cfg:52`, and `CheckFireScreamSerial` default 1): **at most 3 hits on one body per lane serial**
     (`FireScreamMaxAttackCountSameSerial` default 3), the table reset when the serial passes 96 (`SerialInitCount` 100 - 4);
     the defaults are `WZ/SkillAdditionInfo.cpp:79-85`, and **no `SkillAdditionInfo.dat` ships** in WZD, so the defaults stand;
   - a body named twice in one packet kicks the client (`:16680-16693`);
   - **no distance check at all** on the server -- the client's list is taken as sent;
   - each body: `SkillFireScream(aIndex, tNumber, lpMagic)` (`:16735-16738`).

### 1.3 The handler (`WZ/ObjUseSkill.cpp:5502-5524`)
`SkillFireScream`: returns if `FireScreamSkill` is 0 in the ini (default 1), if neither side is connected, **if he is not a
character or not `CLASS_DARKLORD`** (`:5516-5520`); then `gObjAttack(lpObj, lpTargetObj, lpMagic, 0, 0)` (`:5523`) -- one ordinary
blow, `magicsend` 0 (so no 0x19 echo per victim; the victim sees only the damage number). Its own hit/miss roll (`MissCheck`,
`WZ/ObjAttack.cpp:621-647`). Monsters and players alike (a player only where PvP would let him be hit).

**Area, targets, timing on the server**: none of its own -- whatever the client names, 0 ms after the packet arrives.

### 1.4 Damage (`WZ/ObjAttack.cpp`)
- Skill damage **130..195** added to the weapon's min/max (`:3150-3153`, `:3326-3331`), plus **Str/25 + Ene/50** flat for a Dark Lord
  (`:3259-3266`), before defence.
- Then ×**(200 + Ene/20)%**: `case AT_SKILL_FIRESCREAM` sits with Force, Fire Burst, Earthshake and Electric Spike on
  `AttackDamage*(200+Energy/20)/100` (`:1452-1463`, the case at `:1460-1462`).
- The usual critical/excellent/defence/shield path after it. Physical (OM `DamageType.Physical`).

### 1.5 Explosion 79 -- what it is and when it fires

`FireScreamExplosionAttack(lpObj, lpTargetObj, iAttackDamage)` (`WZ/ObjUseSkill.cpp:5530-5629`) is called from **two** places:

| caller | when | damage handed in |
|---|---|---|
| `WZ/ObjAttack.cpp:2901-2914` | inside `if( rand()%100 < lpObj->SetOpDoubleDamage )` -- a **double-damage** blow -- for skill 78 | the blow **before** it is doubled |
| `WZ/ObjAttack.cpp:3060-3065` -> `SpecificSkillAdditionTreat` (`WZ/ObjUseSkill.cpp:5738-5757`) | **at the end of every `Attack` that got that far** (a miss returns earlier, at `:621-647`) with `lpMagic` 78 | the blow as dealt (after doubling and less the shield part) |

So in WebZen 1.00.93 **every landed Fire Scream blow rolls**, and a double-damage one rolls twice. The comment on the function ("after
Fire Scream, when a double damage shows, the monsters around take damage too") is the design; the code is wider. Then:

1. **The roll**: `if( rand() % 10000 > FireScreamExplosionRate ) return` with the rate's default **300** (`:5536`,
   `WZ/SkillAdditionInfo.cpp:82`) -> passes on 0..300, **301 in 10,000 = 3.01%**.
2. **Damage = that blow / 10**, integer; under 1 -> nothing (`:5540-5544`).
3. **Who**: every object in **his** viewport list (`VpPlayer2`, `:5550-5622`) that is a monster not summoned by anyone
   (`m_RecallMon < 0`), not himself, with `gObjCalDistance(struck body, it) <= FireScreamExplosionAttackDistance`, default **1**
   (`:5613`, `WZ/SkillAdditionInfo.cpp:81`). `gObjCalDistance` is `(int)sqrt(dx²+dy²)` on whole tiles (`WZ/user.cpp:7042-7053`),
   so **the 3×3 tiles round the struck body -- the struck body itself included** (distance 0). Players: with
   `MODIFY_FIRESCREAM_SPLASH_DAMAGE_BUGFIX_20081121` on (`WZ/define2.h:636`) only the struck player himself or a rival guild's
   (`:5566-5577`); Chaos Castle everyone (`:5579-5584`); Castle Siege the other side (`:5586-5604`). No wall test.
4. **When**: `gObjAddMsgSendDelay(lpObj, MSG_ATTACKPROC_FIRESCREAM, victim, 100, damage)` (`:5615`): **100 ms** later
   (`WZ/user.cpp:7138-7166`), picked up by the next object pass.
5. **The blow** (`WZ/user.cpp:7298-7309`): only if **he is still alive** (`lpObj->Live`), `gObjAttack(lpObj, victim,
   &DefMagicInf[AT_SKILL_EXPLOSION], magicsend 1, 0, damage)`. With `AttackDamage` already set, the whole damage block is skipped
   (`WZ/ObjAttack.cpp:621` `if( AttackDamage == 0 )` ... `:2303`): **no miss roll, no defence, no DL multiplier, no critical**. It
   is **not reflected** (`:2372-2395`). Being skill 79, it never chains another explosion. `magicsend` 1 sends a **0x19 skill 79**
   from him to the victim to the viewport (`:2765-2768`) -- that is what draws the bombs (section 3.6) -- then the damage number.

`ControlFireScreamDoubleAttackDistance` (a GM command, `WZ/GMMng.cpp:430-431`, `:3614-3617`, `:4474-4478`) can change the
distance live. **Where `SetOpDoubleDamage` comes from** in WZ: ancient set options (`WZ/ObjCalCharacter.cpp:2210`) and socket
options (`WZ/ItemSocketOptionSystem.cpp:1911-1930`) only.

### 1.6 OpenMU differs (`OM/GameLogic/PlayerActions/Skills/FireScreamSkillPlugIn.cs`)

| | WebZen 1.00.93 | OpenMU | wins |
|---|---|---|---|
| Who is struck | the client's list, ≤5 a packet, ≤5 packets, ≤3 hits a body a serial | server-side `AreaSkillAutomaticHits` frustum **2 wide at him, 3 at the end, 6 long**, one hit each (`SkillsInitializer.cs:199-200`, `AddAreaSkillSettings(true, 2f, 3f, 6f)`) | WZ (rule); OM's shape is a fair stand-in |
| Explosion chance | 3.01% | **30%** (`:31`, `Rand.NextRandomBool(0.3)`) | WZ |
| Explosion trigger | every landed blow (+ again on a double) | every hit (`AfterTargetGotAttackedAsync`) | WZ (same idea) |
| Damage | blow / 10 | (health + shield damage) / 10 (`:36-37`) | same |
| Radius | whole-tile distance ≤ 1 (3×3) | **2** (`:26`, `:79-80`) | WZ |
| Who | monsters (not summons); struck player / rival guild / CC / siege | monsters, destructibles, duel and guild-war players; a summon by its owner (`:43-76`) | WZ |
| Delay | 100 ms | 100 ms (`:88`) | -- |
| Damage path | no miss, no defence | `ApplyBleedingDamageAsync` -- also no defence (`:95`) | -- |

## 2. Hit shape, as the client makes it (it is the rule)

From the effect motion in section 3.3 and the hit test in 3.4, in MU units (100 = a tile):

- **Three lanes** from his feet: the middle one along his facing, the side ones started **80 units to either side** and turned
  **±10°** (`MM/Engine/Object/ZzzCharacter.cpp:4509-4531`). Each lane's head is born about **80 units ahead** of where it was set
  ((-10,-60,135) rotated then 20 more forward, `MM/Render/Effects/ZzzEffect.cpp` create, 3.3), runs **35 units a frame** along the
  ground for **19 frames** (0.76 s): **80 -> 710 units** out.
- Each frame the head names every monster (and PK-keyed player) within **100 units** of it, up to 5 (`103/ZzzEffect.cpp:123-240`).
- So the struck area is a **wedge about 8 tiles long** (710 + 100) and about **3.6 tiles wide near him, ~6 tiles wide at the far
  end** (side lanes 80 + s·sin 10° out, plus 100). Nothing behind him: the heads start ahead and only go forward.
- **When a body is struck**: when a head first comes within 100 units: frame ≈ (d − 180)/35 after the let-go for a body d units
  ahead on a lane -- 3 tiles **≈ 3-4 frames (0.14 s)**, 6 tiles **≈ 12 frames (0.48 s)**, 8 tiles ≈ 18 frames (0.72 s) -- plus the
  round trip. A body within 1.8 tiles is named on the head's first frame.
- **How often**: with `PJH_FIX_4_BUGFIX_7` **off** in 1.03.35 (`103/Winmain_New.h:71`, "being fixed, not patched") a head
  re-asks every frame, so a lane passing a body names it for about 200/35 ≈ 5-6 frames; the server's caps then decide: 5 packets a
  cast, ≤3 hits a body a serial. **In practice a body can take more than one blow from one cast**; how many depends on the packet
  serials, which this research could not settle (open call 2).

## 3. Client (MuMain S6, cross-checked with 1.03.35)

### 3.1 The cast and the clip
- **Hero**: `AttackKnight`'s case (`MM/GameLogic/Combat/ClassAttack.cpp:866-905`): if in range (`CheckTile`), turns him to the
  target point, sends 0x1E (`SendRequestMagicContinue`, his tile, facing, target key) **and starts the clip at once**:
  `PLAYER_ATTACK_RIDE_STRIKE` on a Uniria/Dinorant/Dark Horse off a safe tile, the Fenrir strike on a Fenrir, else
  `PLAYER_ATTACK_STRIKE`. Not enough mana -> drinks a mana potion and returns (`MM/GameLogic/Combat/SkillCast.cpp:251-262`).
  No weapon needed (`ClassAttack.cpp:504`).
- **Everyone, on the 0x1E echo** (`ReceiveMagicContinue`, `MM/Network/Server/WSclient.cpp:5079`): others start the same clip with
  `AnimationFrame = 0` (`:5159-5175`, `:5473`), plays `SOUND_FIRE_SCREAM`; and **for all, the hero too, `AttackTime = 1`**
  (`:5481`). There is no Fire Scream case in `ReceiveMagic` other than this.

| Mount | Action | raw # | keys (`content/players/rig/player.rig.json`) | PlaySpeed (`MM/Engine/Object/ZzzCharacter.cpp`) |
|---|---|---|---|---|
| foot, or any mount in a safe zone | `PLAYER_ATTACK_STRIKE` | **80** | 7 | 0.25 + AttackSpeed·0.004 (`:949`) |
| Uniria / Dinorant / Dark Horse | `PLAYER_ATTACK_RIDE_STRIKE` | **82** | 6 | 0.20 + … (`:950`) |
| Fenrir | `PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE` | **92** | 7 | 0.20 + … (`:956`) |

Raw numbers counted from `MM/Core/Globals/_enum.h:2902-3067` (PLAYER_SET 0; ATTACK_FIST 38 .. RIDE_SKILL 155).
These are the clips Force and Fire Burst already use.

### 3.2 The let-go: key 5 of the strike
`AttackStage` has **no Fire Scream case**; the default (`MM/Engine/Object/ZzzCharacter.cpp:3045-3059`) sets `AttackTime = 15` once
`IsAttackImpactFrame` -- `AnimationFrame >= 5` on a player attack action (`:2589-2606`; 80, 82, 92 are all in
`PLAYER_ATTACK_FIST..PLAYER_RIDE_SKILL`, `MM/Engine/Object/PlayerActionState.h:13-16`). Then `:4140` lets it go into the case at
**`:4500-4549`**. So **key 5 of 7**: with no attack speed 5/0.25 = **20 ticks, 0.80 s after the echo**; at AttackSpeed 50 (Agi 500)
5/0.45 ≈ 11 ticks (0.44 s). (Fire Burst and Force let go at key 3.) 1.03.35 the same (`103/ZzzCharacter.cpp:4851-4856`,
`:5298-5347`).

### 3.3 What spawns (`MM/Engine/Object/ZzzCharacter.cpp:4500-4549`; `103/ZzzCharacter.cpp:5298-5347`)
Three **pairs**, each a `MODEL_DARK_SCREAM` and a `MODEL_DARK_SCREAM_FIRE`, owner him, `PKKey` his target:
- pair 1 at his position, his facing;
- pair 2 at **(80,0,0)** in a frame turned **+10°**, facing +10°;
- pair 3 at **(−80,0,0)** in a frame turned **−10°**, facing −10°.
Then `SetAction` to the strike again (already playing) and `PlayBuffer(SOUND_FIRE_SCREAM)` again.

**Create** (`MM/Render/Effects/ZzzEffect.cpp:1648-1725`, the arrow block; 103 `:2138-2221`, `:2308`):
`Velocity 1`, offset **(−10,−60,135)** rotated by its angle; then for both `LifeTime 19`, `Direction (0,−35,0)` (35 units a frame
forward); `Scale` **2.3** (FIRE) / **0.9** (SCREAM). The `MODEL_DARK_SCREAM` only: z = terrain + 3; a ground ribbon
`CreateJoint(BITMAP_JOINT_FORCE, …, 7, o, 150, 40)` (sub-type 0; sub-type 1 is Erkanne the monster's, 400 wide); z += 20; 20 units
forward; one `BITMAP_BLUE_BLUR` sub-type 1 particle, light (0,1,0). `o->Weapon = PacketSerial`. MuMain multiplies the +20 by
`FPS_ANIMATION_FACTOR` (1 at 25 fps).

**Move** (`MM/Render/Effects/Behaviors/MoveHandlers.cpp:4626-4648`; 103 `ZzzEffect.cpp:13224-13246`), each frame:
- FIRE: `Scale −= 0.14` (2.3 -> under 0.1, i.e. **0, by frame 16**); SCREAM: `Scale −= 0.04` (0.9 -> 0.18 at death);
- z = terrain + 3 (they hug the ground);
- one `BITMAP_FLAME` sub-type 8 particle at the head, scale (Scale − 0.4)·3.5 (FIRE 6.65 falling; SCREAM 1.75 falling, nothing once
  negative);
- the SCREAM only: `CheckClientArrow` -> the hit test (3.4);
- then the shared tail moves it 35 units along its yaw (`MoveParticle`, `ZzzEffect.cpp:6231-6247`, called at `:8562-8566`) and
  `LifeTime −= 1`; dead at 0 (`:8569-8573`).

**Render** (`MM/Engine/Object/ZzzObject.cpp:1557-1564`; 103 `ZzzObject.cpp:1715-1722`): SCREAM `RENDER_TEXTURE` (normal blend,
lit); FIRE `RENDER_TEXTURE | RENDER_BRIGHT` (**additive**), BlendMeshLight 1.

**The models** (parsed from `MMD/Skill/*.bmd`, version 0x0C, by the reader in `pipeline/MuExtract/MuAssets/Models/BmdReader.cs`;
loaded `MM/Engine/Object/ZzzOpenData.cpp:4248-4251`, textures from `Data/Skill/`). MU models face −Y, so **+Y is behind**:
- **darkfirescrem01** (= `MODEL_DARK_SCREAM_FIRE`), 1 bone, 1 key:
  - mesh 0 `motion_blurr.jpg` (32×64): 78 verts / 68 tris, **x −99..101, y −75..313, z −3..160** -- a flame sheet about 2 m wide,
    1.6 m tall, trailing **3 m behind** its head;
  - mesh 1 `SwordEffor2.jpg` (64×64): 31 verts / 48 tris, x −12..13, y −54..156, z −1..55 -- a narrow streak in its middle.
  - At its birth scale 2.3: **~460 wide, ~370 tall, trailing ~720 units behind the head** -- the head is ~100 ahead of him, so
    **the sheet reaches ~6 tiles behind him on its first frame** and shrinks to nothing by frame 16 while running forward.
- **darkfirescrem02** (= `MODEL_DARK_SCREAM`), 5 bones, 1 key: mesh 0 `chainring3.tga` (128×64, 32-bit): 12 verts / 8 tris,
  x ±70, y −13..146, z ±70 -- crossed quads, a ring-like flare trailing 1.5 m, at 0.9 shrinking.

**The ground ribbon** `BITMAP_JOINT_FORCE` sub-type 7 (`MM/Render/Effects/ZzzEffectJoint.cpp:2366-2378` create, `:6419-6438` move):
texture `BITMAP_INFERNO` = `Effect/Inferno.jpg` (`ZzzOpenData.cpp:5408`), 13 tails, LifeTime 20, Velocity 10 growing by
`Direction[2]` (1, then +3.5 once, then +0.5 a frame), so its head runs ahead of everything, accelerating, **≈ 1,470 units in its
20 frames** (simulated); z = terrain + 3; its light ÷1.3 a frame in the last 5. A streak of fire racing along the ground.

**Particles**:
- `BITMAP_FLAME` sub-type 8 (`Effect/Flame01.jpg` 64×64, `ZzzOpenData.cpp:5194`; create `MM/Render/Effects/ZzzEffectParticle.cpp:590-605`,
  move `:4836-4852`): LifeTime 33, scale given − 0..0.19, rises Gravity/2 a frame (Gravity (1.8..2.79)·Scale), shrinks
  Gravity/95 a frame, spins ±2°, light ÷1.007 a frame; sprite 64·Scale -- a FIRE head's first flame is ~425 units across.
  **One per head per frame: 6 heads × 19 frames ≈ 100 flames a cast**, most from the FIRE heads. This is the white-orange wall in
  every clip.
- `BITMAP_BLUE_BLUR` sub-type 1 (texture swapped to `Effect/PoundingBall.jpg`, `ZzzEffectParticle.cpp:932-950`, `:3983-3998`):
  LifeTime 30, scale 0.64-1.27 growing 0.19 a frame, rising 5 a frame, light life/20: one puff per lane at the spawn.

**Lifetimes** (25 fps frames): heads 19 (0.76 s); FIRE mesh visible ~16 (0.64 s); flames up to 33 more (to ~2 s after the let-go,
fading); ribbon 20; puff 30.

### 3.4 The hit test -- the client names who is struck
`CheckClientArrow` for `MODEL_DARK_SCREAM` (`103/ZzzEffect.cpp:7641-7657`; MM `ZzzEffect.cpp:6492-6498`):
`AttackCharacterRange(Hero->CurrentSkill, o->Position, 100, Hero->Object.Weapon, o->PKKey, o->AttackPoint[0])`.
`AttackCharacterRange` (`103/ZzzEffect.cpp:123-240`): every live, visible character not the hero within **100 units** that is a
monster, or the player whose key is `PKKey`, or anyone in a running Castle Siege, up to **5**; if any, `SendRequestMagicAttack`
(`103/wsclientinline.h:1765-1830`, packet `PACKET_MAGIC_ATTACK`). With `PBG_FIX_DARK_FIRESCREAM_HACKCHECK` on
(`103/Winmain_New.h:1337`) the serial byte is `(BYTE)SkillSerialNum` (`103/ZzzEffect.cpp:217-222`).
**MuMain stubbed this out** -- `AttackCharacterRange` returns false, "we don't send this packet anymore" (`MM/Render/Effects/ZzzEffect.cpp:62-65`),
because MuMain plays against OpenMU, which picks targets itself (1.6).
Caution: the test is made for **every** `MODEL_DARK_SCREAM`, not only the hero's (no owner test at `103/ZzzEffect.cpp:7641`), with
the hero's current skill; another lord's lanes passing monsters may make an onlooker's client send packets. The server's 8-s and
5-packet gates make that mostly harmless; noted, not ours to copy.

### 3.5 Sounds
- `SOUND_FIRE_SCREAM` = **`Data/Sound/Darklord_firescream.wav`** (`MM/Engine/Object/ZzzOpenData.cpp:4900-4901`; 103 `:8524-8525`):
  **1.91 s, stereo, 22,050 Hz** (`afinfo`). Played at the press/echo and again at the let-go; a playing DirectSound buffer ignores
  Play (fire-burst.md, `DSPlaySound.cpp:325-342`), so normally **once**, from the echo.
- The Explosion's bombs: each `BITMAP_EXPLOTION` plays `SOUND_EXPLOTION01` = `eExplosion.wav` at its birth
  (`MM/Render/Effects/ZzzEffectParticle.cpp:2464-2481`, `ZzzOpenData.cpp:4801`) -- three bombs, one voice.

### 3.6 Explosion 79's look (`MM/Network/Server/WSclient.cpp:4789-4802`; 103 `WSclient.cpp:5825-5838`)
On the server's 0x19 skill 79 (`ReceiveMagic`): **three `CreateBomb(Pos, true)`** at the victim's position + (rand%160, rand%160)
-- **only positive** offsets, so all three land in the +x/+y quarter of the victim, up to 1.6 tiles off. `CreateBomb`
(`MM/Render/Effects/ZzzEffect.cpp:6275-6347`), sub-type 0, 80 units up:
- 20 `BITMAP_SPARK` sub-type 2 (`Effect/Spark02.jpg`), pitch 150-209°, yaw 0-29°, life 24-39, scale 0.8-1.4, speed ×3, gravity 6-21
  (`ZzzEffectParticle.cpp:2014-2030`);
- one `BITMAP_EXPLOTION` (`Effect/Explotion01.jpg`, `ZzzOpenData.cpp:5210`), light 0.7 grey-white, LifeTime 20, a frame of the
  sheet every 2 ticks (`ZzzEffectParticle.cpp:4198-4199`), and the sound.
Side effect worth knowing: `ReceiveMagic` also **turns the lord toward the victim and sets `sc->Skill = 79`** (`:4078-4089`) on
onlookers' screens. The explosion's own damage number follows in the same `Attack` (`WZ/ObjAttack.cpp:2765-2768` then `:2982`):
**the bombs and the number come together, 100 ms after the Fire Scream blow's number.**

### 3.7 Files (all in MMD)
| Thing | File | In our content? |
|---|---|---|
| fire sheet + streak | `Skill/darkfirescrem01.bmd` (`motion_blurr.OZJ` 32×64, `SwordEffor2.OZJ` 64×64) | **no** |
| head flare | `Skill/darkfirescrem02.bmd` (`chainring3.OZT` 128×64) | **no** |
| ground ribbon | `Effect/inferno.OZJ` (`BITMAP_INFERNO`) | **no** (`content/effects/inferno/` has Inferno01's meshes and ring3/4, not this sheet) |
| flames | `Effect/Flame01.OZJ` | yes, `content/effects/flame/flame01.png` |
| spawn puff | `Effect/PoundingBall.OZJ` | yes, `content/effects/aida/poundingball.png` |
| bomb blast | `Effect/Explotion01.OZJ` | yes, `content/effects/meteor/explotion01.png` |
| bomb sparks | `Effect/Spark02.OZJ` | yes, `content/effects/hit/spark02.png` |
| cast sound | `Sound/Darklord_firescream.wav` (1.91 s) | **no**, not in `content/sounds/sounds.json` |
| bomb sound | `Sound/eExplosion.wav` | yes, `"explosion"` (`content/sounds/sounds.json:301`) |
| skill icon | `newui_skill2`, index 78 − 57 = **21: column 5, row 2** (RenderSkillIcon's generic second-sheet branch, `MM/UI/NewUI/NewUIMuHelper.cpp:1771-1776`; no special case for 78) | **no**: add `78: ("Dark Lord", "Fire Scream")` to `pipeline/skill_icons.py` (beside `:126-133`) |
| scroll | (12,35), `Item/SkillScroll` model | no item yet |

## 4. 1.03.35 differences

1.03.35 **has** Fire Scream (as `AT_SKILL_DARK_SCREAM`) and is the better source for **the hit test**, which MuMain removed (3.4).
Otherwise line for line: create `103/ZzzEffect.cpp:2198-2221` = MM, let-go `103/ZzzCharacter.cpp:5298-5347` = MM, move
`:13224-13246` = MM (without the FPS factors), render `103/ZzzObject.cpp:1715-1722` = MM, the explosion `103/WSclient.cpp:5825-5838` =
MM, files `103/ZzzOpenData.cpp:5807-5809`, `:8525`. Differences: the echo's mount test (`103/WSclient.cpp:5797-5823`, under
`KJH_FIX_CHAOTIC_ANIMATION_ON_RIDE_PET`, with an `||` bug when `PBG_FIX_CHAOTIC_ANIMATION` is off -- always the ride clip); the
master-tree variants `AT_SKILL_FIRE_SCREAM_UP..+4` (Season 4, not ours); and Erkanne's monster Fire Scream (`LDS_ADD_EFFECT_FIRESCREAM_FOR_MONSTER`,
sub-type 1, `103/GMEmpireGuardian1.cpp:551-584`).

## 5. Footage

Clips:
- `nJygYJ7GiNQ` "Mu Online Dark Lord Skill Fire Scream" (Webzen's own channel), 20 s, 360p, a modern client, a lord casting ~15 times
  at a training dragon in a dark arena: the clearest. Cycle about **1.3 s** (high attack speed).
- `pzlcEXEPcRU` "DL-Scream using proxy mu" (AtomMu1), 159 s, 8 fps, an older Season-era client, PvP in Stadium -- **a speed
  proxy is in use**, so the cadence is not MU's; used only for two stills of the shape.
- `NQt8pu_o2G8` "Fire Scream bug Mu Online 6 season" -- looked at, not used (crowded event map, many skills).

Stills (`fire-scream-footage.jpg`; top row and first three of the bottom row from `nJygYJ7GiNQ`, the last two from `pzlcEXEPcRU`):

| # | time | what is seen |
|---|---|---|
| 1 | 0.0 s | he stands, facing the dragon (screen right), scepter low, a light at his hand |
| 2 | 0.1 s | the strike: scepter up |
| 3 | 0.2 s | **the burst**: a white-hot orange flame sheet from **behind him** (screen left, 1-2 tiles back) through him toward the dragon; red-orange streaks fanning out behind him; small flames running along the ground in front toward the dragon's feet. Darkfirescrem01 at scale 2.3, its tail behind him, exactly as the model says |
| 4 | 0.3 s | the sheet shorter, the white core between him and the dragon (2-4 tiles in front) |
| 5 | 0.5 s | the core now at the dragon; streaks still behind him but fainter |
| 6 | 0.7 s | a yellow **damage number** (~22,3xx) on the dragon, **0.5 s after the burst** -- the lanes reached it (it stands ~3 tiles off; the clip's latency) |
| 7 | 1.0 s | flames dying round the dragon; the next number |
| 8 | 1.3 s | clear; next cast starts |
| 9 | proxy | a lord on a Dark Horse in Stadium: long orange streaks fanning **from behind** him forward, a number on the target |
| 10 | proxy +0.5 s | a broad white-orange fan about 5-6 tiles wide and long round the target |

No explosion bombs were identifiable in either clip (3% a blow, and buried in the flames). Size: the white wall reaches ~5-6 tiles
along his facing and ~3-4 across; the streaks behind him ~2 tiles back in the Webzen clip (its camera is close).

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

Reusable:
- **The row's neighbours**: Force (`src/sim/skills.cpp:912-916`), Fire Burst (`:927-932`), Electric Spike (`:943-948`) -- same kin,
  `families arms::kNone`, `anyHand`, clip 80, DL multiplier and the Str/25 + Ene/50 flat already applied to every `Kin::DarkLord` row
  (`kLordForcePerEnergy`, `:984-986`; fire-burst.md section 2).
- **The let-go**: `Realm::release` (`src/sim/realm_fight.cpp:1301`) has per-skill branches (Electric Spike `:1311-1314`, Fire Burst
  `:1342-1345`); Fire Scream wants one more. `Spread` (`src/sim/skills.h:273`) has `Line`, `Fan`, `Beam`, `Arc`; **none is three
  lanes with a per-body arrival time**, but `Realm::electricSpike` (`realm_fight.cpp:2020-2071`) is the model: pick the bodies, compute
  each one's wait from how far along his facing it stands (`sparkReaches`), `loose(..., wait)`, and `say(What::Loosed, ...)` the ones
  the drawing must show. Fire Scream's wait is the simpler `(along·100 − 180)/35` frames.
- **The double-hit / proc pattern**: `Realm::loose` already runs a proc off a landed blow (Fire Ball's Pyroblast,
  `realm_fight.cpp:1418-1428`, reading `What::Hit`'s damage from `happenings_`), with its own dice stream (`runeDice_`) so the
  seeded log is untouched for everyone else. Explosion is that shape: on a landed Fire Scream `What::Hit`, roll, and if it fires,
  a `Flight` 2 ticks on (100 ms) of `damage/10` on every monster in the 3×3 round the struck body.
- **Blows that skip the band**: the Flight lands through `strikeAt`; a fixed-damage, no-miss, no-defence blow needs a flag (poison's
  pulses and the bleeding-style hits are the precedent to check).
- **Clip** 80/82/92 are on cook.py's list (Force, Fire Burst), so `castTicks` knows them; `release` would be **5/7**.
- **Effect pattern**: `src/game/fx/fire_burst.*` (meshes + Fire02 flames, `effect_mesh`), `fx/flame` (flame01), `fx/meteor` (the
  `explotion01` blast and `explosion` sound) cover most of the drawing; the hook beside Fire Burst's in `src/game/play.cpp:1595`
  and `:1983`.

What is different here:
- **No double damage exists in this game.** `sim::Blow` has `hit`, `critical`, `excellent`, `blocked` (`src/sim/rules.h:222-233`);
  `Excellence`/`Fighter` carry critical, excellent, ignore-defence, block, but nothing like `SetOpDoubleDamage` (grep of `src/sim`).
  The plan row's "Explosion on a double hit" has no hook to hang on (open call 3).
- MU's hit list is the client's; ours is the realm's. The three-lane wedge must be measured in the sim, as Aqua Beam's circles were
  (`kBeam*`, `skills.h:288-301`, "struck once, as OpenMU's frustum strikes it -- ours, and marked").

Missing, and where each would go:
- **The row**: `skill::kFireScream = 78` in `src/sim/skills.h` (beside `kSummon`, `:139`), a row on the end of `kRows` after Summon
  (`skills.cpp:973-977`), `kSkills` 45 -> **46** (`skills.h:538`) and the kept layout's cools; Explosion 79 needs **no row** (never
  learned, never on the bar -- a number for the log and the drawing, as `kIceStorm` 39 is used, `skills.h:155-157`).
- **The sim**: `Realm::fireScream` (the wedge, the waits, `What::Loosed` per lane or once), the Explosion proc in `loose`/`arrive`, a
  dice stream for it, tests beside `tests/sim_test_lord.cpp`.
- **The drawing**: `src/game/fx/fire_scream.*` -- three lanes of darkfirescrem01 (additive) + darkfirescrem02 (alpha), the Inferno ribbon,
  ~100 Flame01 flames, three PoundingBall puffs; `Explosion` -- three bombs (Spark02 ×20 + Explotion01 + `explosion` sound) on a
  `Loosed` 79.
- **Content**: cook `darkfirescrem01.bmd` and `darkfirescrem02.bmd` with `motion_blurr`, `SwordEffor2`, `chainring3` into a new
  `content/effects/fire_scream/` (json beside each, as `fire_burst/PierPart.json`); `Effect/inferno.OZJ` as a sheet;
  `Darklord_firescream.wav` into `content/sounds/sounds.json` (**the showing cook**); the icon cell (above); the scroll (12/35 if free in
  our group 12 -- not checked here) in every world's tables. Plan section 6 makes it the **Pit** quest's pay.

## 7. Open calls (for the design; not decided here)

1. **Where the area comes from**: MU's three lanes (wedge ~8 tiles long, ~3.6 -> ~6 wide, struck as the heads pass), or OpenMU's
   frustum (2 -> 3 wide, 6 long, all at once). The plan row says "6 tiles"; the lanes reach 8.
2. **Hits a body per cast**: WebZen can take up to three per body (≤3 a serial) and up to 25 names a cast (5 packets × 5);
   OpenMU and Aqua Beam's precedent here strike once.
3. **Explosion's trigger**: the plan row's "on a double hit" -- there is no double damage here. WebZen's code rolls **3.01% on every
   landed blow** (and again on a double). Options: (a) WebZen's every-blow 3%; (b) on a critical or excellent blow, as our nearest
   "double"; (c) OpenMU's 30% every hit (much louder).
4. **Explosion's reach**: WebZen's 3×3 round the struck body, **the struck body included** (it takes 1/10 again), or OpenMU's radius 2.
5. **The fire sheet behind him**: MU's darkfirescrem01 at scale 2.3 trails ~6 tiles behind its head, so for its first frames it lies
   **behind him** -- in the footage too. "Only frontal" says cut it at his shoulders; "true on actual MU" says keep it.
6. **Cost and cooldown**: MU 45 mana + 10 AG, delay 0 -- in MU it is spammed like a primary. Ours: every QWER skill has a cooldown;
   Force is his primary.
7. **Requirements**: level 102 (client), Command 70 (client, OM's orb), energy 150 (client's own formula) or 0 (WebZen server) or 70
   (OM's skill row, probably swapped).
8. **The let-go**: key 5 of 7 (0.8 s at no attack speed) is later than Force's and Fire Burst's key 3.
9. **Mounted**: clip 82 on the Dark Horse; Fenrir (92) is not ours.

## 8. Decided

Settled 2026-10-24 from sections 1-7, re-checked against the sources before the build: the explosion
(`WZ/ObjUseSkill.cpp:5530-5629`, rate 300 and distance 1 by default `WZ/SkillAdditionInfo.cpp:81-82`, both callers
`WZ/ObjAttack.cpp:2901-2914` and `:3063`), the lanes (`103/ZzzCharacter.cpp:5298-5347`, LifeTime 19 `103/ZzzEffect.cpp:2198-2200`,
the 100-unit hit test `:7641-7657`, `:123-240`). Against "DL has to be true on actual MU", "only frontal" and "the number with
the visual":

1. **MU's three lanes are the shape**, measured in the sim (`Realm::fireScream`): one down his aim, one 0.8 tiles to each side turned
   10 degrees out; each head 0.8 tiles out at the let-go and on 0.35 a frame for 19 frames; a body is struck at the first frame any
   head comes within one tile of it, that frame's ticks after the let-go (3 ticks at three tiles, 12-13 at seven). Nothing behind him.
2. **Struck once a cast** (ours, marked, as Aqua Beam), every monster the lanes reach; WebZen's three a lane serial and five a packet
   are the client's to name and not copied.
3. **Explosion as WebZen's code has it**: on every landed Fire Scream blow, `rand() % 10000 > 300` refuses (301 in 10,000), a tenth
   of that blow, nothing under one, 100 ms (2 ticks) on, to every monster within whole-tile distance 1 of the struck body (itself
   included), named at the roll; only while he lives; no miss, no defence, never chaining. Its own dice stream. Drawn as MU's three
   bombs with eExplosion, said (`Loosed` 79) on the tick its numbers come. The plan row's "on a double hit" is "on a hit": WebZen
   rolls on every hit and again on a double, and there is no double damage here.
4. **The fire sheet kept frontal**: darkfirescrem01's trail behind its head is clipped where it would pass behind his shoulders; the
   lanes, flames and ribbon run forward from him as MU's do.
5. **MU's row**: level 102, damage 130, 45 mana, six tiles of aim, the strike (80), let go at key 5 of 7; no AG. **Ours**: a 3 s
   cooldown (MU has none; every QWER skill here has one). Command 70 and energy 150 (the client's) go on the scroll.
6. **MU's look at MU's numbers**: the three pairs, the Inferno ribbon, the flames, the puffs; Darklord_firescream.wav once at the cast
   through the showing cook; icon cell 21 of newui_skill2.
7. **The scroll** (12/35 if free, else the next free number) as plan section 6's Pit reward, with the Dark Lord's items step.
