# Force Wave (skill 66) -- research dossier

Researched 2026-10-24/25 before any build. Nothing here is built. Drafted by a research agent from the sources below;
every number is read, not recalled. Force (60) is built and approved (docs/plans/dark-lord.md:238), so Force Wave is
described **as a delta from it**.

Path keys:
- **WZ** = WebZen GameServer 1.00.93, `.../44e82997-.../scratchpad/forceaudit/ws/Source/Server Side/GameServer/` (cp949; line
  numbers are of the file as is). **WZD** = `.../ws/_Server Files/Data/`. **WZC** = `.../ws/Source/Base Common Src/Common/`.
  **WZP** = `.../ws/Source/Base Common Src/include/public.h`.
- **OM** = `LEGACY/reference/openmu/src/`.
- **MM** = `LEGACY/reference/MuMain/src/source/` (Season 6 client). **MMD** = `LEGACY/reference/MuMain/src/bin/Data/`.
- **103** = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source/` (cp949).
- MU time: one reference tick = 1/25 s. Our realm: 20 ticks/s. A tile = 100 MU units.

Names: WZ `AT_SKILL_LONGSPEAR` ("롱스피어", `WZP:257`), compiled under `DARKLORD_WORK`; MM `AT_SKILL_FORCE_WAVE` (the enum's
66, `MM/Core/Globals/_enum.h:360-366`) and its master-tree `AT_SKILL_FORCE_WAVE_STR = 509` (`:575`, not ours); 103
`AT_SKILL_LONG_SPEAR` with the comment **"롱스피어 used only on the server"** (`103/_enum.h:1951`), Force there being
`AT_SKILL_STRONG_PIER = 60` (`:1944`); OM `SkillNumber.ForceWave = 66` (`OM/Persistence/Initialization/Skills/SkillNumber.cs:76`).
Ours: `skill::kForceWave = 66` already reserved, "no row of its own ... the number is the drawing's" (`src/sim/skills.h:120-125`).

**The headline**: Force Wave is **not a skill he casts** -- it is how WebZen's server deals **Force** when the right hand holds an
item whose skill option is skill 66 (every scepter, `+S`). The client sends Force (60), plays Force's clip and draws **Force's
effect, line for line -- MuMain and 1.03.35 have no Force Wave drawing at all**. The server then swaps the skill damage to
**50..75** and, instead of the one body, strikes **every monster WebZen's hand-drawn table `skillSpear.hit` marks toward the aimed
body**: a strip about **1-2 tiles wide and 5 to 8.5 tiles long** (10 tiles on average), not the plan's "1-wide, 4-long" -- that is
**OpenMU's** frustum (1, 1, 4). All of them **at once**, as the cast arrives, each once. **Blocker**: there are no scepters in
our content, and **this game gives no item its skill option** ("skills are orbs here", the user 2026-09-27) -- so as things
stand no lord could ever hold a Force Wave scepter.

## 1. MU's rule (server)

### 1.1 The row

`WZD/lang/main/skill.txt` (header dated 2011-03-08) has both rows; read against its own column header (`:12`):

| Field | Force 60 (`skill.txt:65`) | **Force Wave 66** (`skill.txt:71`) | MuMain `Skill.bmd` rec 66 | OM (`VersionSeasonSix/SkillsInitializer.cs:186`) |
|---|---|---|---|---|
| Level | 0 | **0** | 0 | -- |
| Damage | 10 | **50** | 50 (0x32) | 50 |
| Mana / AG | 10 / 0 | **10 / 0** | 10 / 0 | 10 |
| Distance | 4 | **4** | 4 | 4 |
| Delay | 0 | **0** | 0 | -- |
| Energy / Command | 0 / 0 | **0 / 0** | 0 / 0 | -- |
| Class | DL | **DL** | DL bit | `AllLords` |
| 속성2 (type) | 1 | 1 | -- | `DamageType.Physical` |
| "무기 스킬" (weapon skill) | **0** | **1** | -- | -- |

`skill(Kor).txt:55` ("Force Wave" 0 50 10 0 4 ...) agrees. `MMD/Local/Skill.bmd` decoded as the Fire Scream dossier did (80-byte
records, XOR FC CF AB): rec 66 = name `フォースウェーブ` (Shift-JIS) then `00 00 32 00 0a 00 00 00 04 00 ...` -> level 0, damage 50,
mana 10, AG 0, distance 4, delay 0, energy 0, Command 0, DL bit set; rec 60 identical but damage 10 (`0a 00`).
The rolled range: `CMagicInf::Set` -> `m_DamageMin = damage; m_DamageMax = damage + damage/2` (`WZ/MagicInf.cpp:266-286`, the two
at `:282-283`), so **50..75**. The one row difference that matters is damage 10 -> 50; mana, reach and requirements are Force's.

### 1.2 How the scepter grants it (and that nothing is "learned")

1. **The item**: in `item.txt` the "Skill" column of every scepter is **66** (`WZD/lang/main/item.txt:60-64`: Battle 8, Master 9,
   Great 10, Lord 11, Great Lord 12 -- this 1.00.93 file stops at 12). OM's S6 table adds Divine Archangel 13, Soleil 14, Shining 15,
   Absolute 17, Stryker 18, all skill 66; Frost Mace 16 has none (`OM/.../VersionSeasonSix/Items/Weapons.cs:190-200`).
2. **The option**: `CItem::Convert` (`WZC/zzzitem.cpp:977-995`): with `Option1` (the `+S`) set and the row's skill being
   `AT_SKILL_LONGSPEAR`, it sets **`m_SkillChange = TRUE`** and puts **0** in `m_Special` -- so, unlike every other `+S` weapon,
   **the skill is never added to his skill list** (other skills go in `m_Special` and become a castable skill).
3. **Worn**: `gObjCalCharacter` clears `SkillLongSpearChange` (`WZ/ObjCalCharacter.cpp:170-171`) and sets it TRUE when the
   **right-hand** item has `m_SkillChange` (`:509-516`). Note the test sits inside `if( Right->m_Type != -1 )` (`:492`) but
   **outside** `if( Right->m_IsValidItem )` (`:494`): a scepter he cannot yet wear properly (unmet requirement) or one at no
   durability still turns Force into Force Wave. Left hand: never.
4. **The tooltip**: the client shows the Force skill named "Force Wave" when the right hand's `Special` list holds 66
   (`MM/UI/NewUI/HUD/Skills/SkillTooltipModel.cpp:620-632`; `103/ZzzInventory.cpp:8284-8294`, GlobalText 1200), and the item line
   "Force wave skill (Mana: 10)" (GT 1186, `103/ZzzInventory.cpp:2625-2628`; MM `ZzzInventory.cpp:2023-2026`;
   `docs/reference/mu-tooltip-lines.md:170`).

### 1.3 The cast: Force's packet

The client casts Force (60): `CGMagicAttack` (0x19, `WZ/protocol.cpp:15185`) checks the class, then **`gCheckSkillDistance`
against Force's distance 4** (`:15405`; `WZ/user.cpp:33994-34030`: |dx| and |dy| each ≤ 4 + a slack `g_iSkillDistanceCheckTemp`),
then `UseSkill(aIndex, target, lpMagic)` (`:15436`; `WZ/ObjUseSkill.cpp:971`) pays **Force's** mana (`GetUseMana`, `:1008-1030`;
`SkillGetMana(lpMagic->m_Skill)`, `:512-518`) -> `RunningSkill` -> `case AT_SKILL_SPEAR: return SkillSpear(...)` (`:1166`).
`lpMagic` stays skill 60 throughout; 66 is only read for its damage.

### 1.4 The handler (`WZ/ObjUseSkill.cpp:3570-3673`, `SkillSpear`)

1. `iangle = GetAngle(his tile, the aimed body's tile)` (`:3577`; `GetAngle` `:3485-3493`: `(int)(atan2(y−ty, x−tx)·180/π + 90)`,
   wrapped to 0..359). The OpenMU-like `SkillFrustrum2(aIndex, iangle, 1.7f, 6.f)` is **commented out** (`:3579-3580`).
2. `GCMagicAttackNumberSend(lpObj, 60, aimed, 1)` (`:3586`) -- the echo everyone draws, **skill 60 even for Force Wave**.
3. **No option** (`!SkillLongSpearChange`): one `gObjAttack` on the aimed body and return (`:3589-3593`) -- that is our Force.
4. **With the option**: every object in **his** viewport (`VpPlayer2`, `:3603-3671`) is a candidate if it is a monster not summoned by
   anyone (`m_RecallMon < 0`), or the aimed body, or his duel partner, or (through its summoner) a guild-war enemy (`:3611-3636`).
   `attackcheck` is TRUE when `loopcount == 0`, else a 1-in-3 roll (`:3643-3653`) -- but **`loopcount` is set 0 (`:3583`) and never
   incremented**, so the roll never runs: every candidate is tested. Then
   **`SkillSpearHitBox.HitCheck(iangle, his x,y, its x,y)`** (`:3657-3658`) and, if marked, `gObjAttack(lpObj, it, lpMagic, 0, 1, 0)`
   (`:3660`). No victim cap (only the viewport's size, `:3667`), **no wall test**, one blow each, **all in the same server pass**.
5. **The aimed body is not struck separately**: if the table does not mark its tile, it takes nothing (section 2 -- 4 of 80 aimed
   tiles within reach).

### 1.5 Damage (`WZ/ObjAttack.cpp`)
- `if( lpMagic->m_Skill == AT_SKILL_SPEAR && lpObj->SkillLongSpearChange )` the skill damage is
  **`DefMagicInf[AT_SKILL_LONGSPEAR]`'s min/max, 50..75** (`:3144-3148`), else Force's 10..15 (`:3150-3153`). Added to the weapon's
  min/max, then **Str/25 + Ene/50** flat for a Dark Lord (`:3259-3266`), before defence.
- Then `case AT_SKILL_SPEAR` (still 60) ×**(200 + Ene/20)%** with Fire Burst, Earthshake, Electric Spike and Fire Scream
  (`:1452-1463`).
- The usual hit roll, critical/excellent, defence and shield path. So **Force Wave = Force + 40..60 skill damage, to every body in
  the strip**, everything else Force's.

### 1.6 OpenMU differs

| | WebZen 1.00.93 | OpenMU | wins |
|---|---|---|---|
| Who | `skillSpear.hit` toward the aimed body (section 2) | frustum **1 wide at him, 1 at the end, 4 long** (`SkillsInitializer.cs:187`, `AddAreaSkillSettings(ForceWave, true, 1f, 1f, 4f)`; `Updates/AddAreaSkillSettingsUpdatePlugIn.cs:62`), among attackables within **range + 4 = 8** not in a safe zone (`GameLogic/PlayerActions/Skills/ForceSkillAction.cs:52-67`) -- **the plan's "1-wide, 4-long" is this** | WZ |
| The aimed body | struck only if the table marks it | **always added** (`ForceSkillAction.cs:73-76`) | open call 3 |
| How it is known | `m_SkillChange` on the right-hand item, never in the skill list | the item's `HasSkill` puts 66 in the skill list (`GameLogic/SkillList.cs:67`, `:218`); the client's 60 is swapped for 66 when the list holds it (`ForceSkillAction.cs:30-42`); 66 is hidden from the client's list (`GameServer/RemoteView/Character/SkillListViewPlugIn.cs:59-62`, `:157`) and its animation sent as 60 (`RemoteView/World/ShowSkillAnimationPlugIn.cs:52-55`) | same in effect |
| Damage | 50..75 + Str/25 + Ene/50, ×(200 + Ene/20)% | 50 base (`SkillsInitializer.cs:186`); Str/25 + Ene/50 (`Updates/FixDamageCalcsPlugInSeason6.cs:307-312`) | same |
| `+S` on a drop | 6 in 100 (WebZen `gObjMonster.cpp:4690-4714`, as cited in our `src/sim/items.h:640-648`; that file is not in this tree) | **50%** on any item that can carry it (`GameLogic/DefaultDropGenerator.cs:21`, `:170`), every excellent and ancient one (`:194`, `:213`) | WZ |
| Price | -- | Force Wave is a **"worthless skill"**: a `+S` scepter is not priced up (`GameLogic/ItemPriceCalculator.cs:20-28`, `:527`) | note for our market |
| Scepter "magic power" | not read for skills | `ScepterRise` feeds only the raven (`Persistence/Initialization/CharacterClasses/ClassDarkLord.cs:84`) | -- |

## 2. Hit shape -- `skillSpear.hit`, decoded

`WZD/skillSpear.hit`, 15,876 bytes = 36 directions × 21 × 21 (`WZ/SkillHitBox.h:11-21`), loaded at `WZ/Gamemain.cpp:3220`, read as
`m_Table[dir/10][(ty−y+10)·21 + (tx−x+10)]` (`WZ/SkillHitBox.cpp:39-57`) -- the same class and reading as Electric Spike's
`skillElect.hit` (electric-spike.md section 1; `src/sim/electric_spike_box.h`). Decoded here (`o`/`O` = his tile, unmarked /
marked; `#` = struck). The direction's forward vector in table space is (−sin a, cos a): dir 0 runs +Y, dir 9 −X, dir 18 −Y,
dir 27 +X.

```
dir 0 (0°)        dir 4 (40°)       dir 9 (90°)              dir 19 (190°)    dir 35 (350°)
     o            O                 y-1 ######.               y-6  .#          o
     ##          ##                 y 0    ###O               y-5  .#          ##
     ##         ##                                            y-4  .#          ##
     ##        ##                                             y-3  ##          .#
     ##       #                                               y-2  ##          .#
     ##      #                                                y-1  ##          .#
     #                                                        y 0  O           .#
     #                                                                         .#
```

Over all 36 directions (script: count, distance along the facing, distance across it):
- **8 to 14 tiles struck, 10.0 on average**; **5.0 to 8.56 tiles long** along his facing (dirs 2, 3, 22, 34 reach 8.5; dirs 7, 10,
  18 stop at 5); **across ≤ 1 tile** in all but dirs 19 (1.8) and 28 (1.6) -- a strip one to two tiles wide, drawn by hand, its
  cells stepping like a rasterised line.
- **Nothing behind him** in any direction (the least "along" is 0, his own tile). His own tile is marked in 33 of 36 directions --
  harmless, nothing else stands there.
- **The aimed body can miss its own Force Wave**: of the 80 tiles within Force's reach (|dx|,|dy| ≤ 4), 4 are not marked in the
  direction they themselves give: (−4,0) at 90°, (0,−4) and (0,−3) at 180°, (4,−1) at 255° (table space). The table's cardinal
  strips are drawn a column off-axis. WebZen strikes nothing there (1.4.5); OpenMU always adds the aimed body.
- **When**: every marked body at once, the moment the 0x19 reaches the server -- in MU's client that packet leaves at the press,
  before the swing; ours lands Force at the let-go, key 3 of 7 (`src/sim/skills.cpp:894-916`, `release` 3/7). No flight.

So the plan's line (docs/plans/dark-lord.md:238, "1-wide, 4-long") is OpenMU's; WebZen's strip is **as wide or wider and about twice
as long**: on average 10 tiles struck where OM's 1×4 frustum holds ~4.

## 3. Client (MuMain S6, cross-checked with 1.03.35)

**There is no Force Wave drawing.** Every client branch for 66 is a `case` stacked on Force's:
- **The cast/echo** (`MM/Network/Server/WSclient.cpp:4243-4259`): `case AT_SKILL_FORCE: case AT_SKILL_FORCE_WAVE: case
  AT_SKILL_FORCE_WAVE_STR:` -- `PLAYER_ATTACK_RIDE_STRIKE` on a Uniria/Dinorant/Dark Horse off a safe tile, the Fenrir strike on a
  Fenrir, else `PLAYER_ATTACK_STRIKE`; `AttackTime = 1`; `PlayBuffer(SOUND_SKILL_SWORD1)`. And the server echoes **60** anyway
  (1.4.2), so others' screens take the Force case regardless.
- **The let-go** (`MM/Engine/Object/ZzzCharacter.cpp:2866-2874`): the same three cases, `AnimationFrame >= 3` on the strike clips
  -> `AttackTime = 15`: **key 3 of 7**, Force's.
- **What spawns** (`MM/Engine/Object/ZzzCharacter.cpp:5057-5063`): the same three cases -- two `MODEL_WAVES` sub-type 1, one
  `MODEL_PIERCING2`, `SOUND_ATTACK_SPEAR`. Create `MM/Render/Effects/ZzzEffect.cpp:3190-3262`, move
  `MM/Render/Effects/Behaviors/MoveHandlers.cpp:5266-5400`. That is exactly `src/game/fx/force.h/.cpp` (piercing Scale 2 for 10
  frames laying sub-type 2 waves; two waves 0.1-0.6 growing to 2 over 15 frames; each wave's two `BITMAP_PIERCING` joints of **30
  tails × 20 units = 600 units, a six-tile streak down his facing, full length on its first frame**, fading /1.4 for 10 frames).
- **Cast gating** (`MM/GameLogic/Combat/SkillCast.cpp:128-145`, `:318-335`, `:358-359`, `:419-420`, `:453-454`;
  `MM/GameLogic/Combat/SkillExecution.cpp:340-360`; `MM/Engine/Object/ZzzInterface.cpp:1335-1348`): the same lists as Force.
- **Sounds**: `SOUND_SKILL_SWORD1` at the echo, `SOUND_ATTACK_SPEAR` (sDarkSpear) at the let-go -- Force's, already cooked.
- **Files**: none new. `Skill/m_waves.bmd`, `Skill/m_Piercing.bmd`, `Effect/Piercing.jpg` and the streak sheet are in
  `content/effects/force/` (`src/game/fx/force.cpp:18-35`). A scroll/icon: none -- 66 is never on the bar (the tooltip renames Force).
- **Hit test**: none on the client (it names only the aimed body; WebZen's server picks the rest).

What sells "a wave" in MU is therefore only Force's own six-tile streaks, which already run from his chest down his line through
whatever the strip strikes -- the numbers on the bodies down the line are the difference the player sees.

## 4. 1.03.35 differences

None in behaviour. 1.03.35 calls Force `AT_SKILL_STRONG_PIER` and Force Wave `AT_SKILL_LONG_SPEAR` ("used only on the server",
`103/_enum.h:1944-1951`); only Force has cases: echo `103/WSclient.cpp:5224-5238`, let-go `103/ZzzCharacter.cpp:3204-3211`
(`AnimationFrame >= 3`), effects `103/ZzzCharacter.cpp:5969-5974` (two `MODEL_WAVES` sub-type 1 + `MODEL_PIERCING2` +
`SOUND_ATTACK_SPEAR`) -- line for line MuMain's. `AT_SKILL_LONG_SPEAR` appears only in the tooltips (`103/ZzzInventory.cpp:2625`,
`:8288`; `103/CGFxMainUi.cpp:3666`).

## 5. Footage

**No clean clip of Force Wave was found.** YouTube searches ("mu online dark lord force wave", "forcewave mu online", "mu online
force wave skill", "mu online scepter force wave", "mu online dark lord all skills", and others) return Grim Dawn's own "Forcewave"
skill, and MU clips named "ForceWave Multihack" (`Zt2_Rbt3-EE`, `a3crL_dFJwc`, ReporterOfMU, 2012) -- **"ForceWave" there is the name
of a hack tool** (its panel is titled ForceWave; the chat reads "assisted by ForceWave"), the footage crowded Lorencia PvP. Skill
guides looked at and not used: `2EjpopIr8As` (MegaMU, labels Fire Burst and Electric Spark only), `HQ6s1NRN4lo` (S6 DL PvP guide,
no clear Force Wave segment). Since MU's client draws Force Wave as Force (section 3), Webzen's own "Mu Online Dark Lord Skill
Force" (`v7Z2mh3qJho`, 20 s, 360p, the same series as Fire Scream's `nJygYJ7GiNQ`) is the look; whether the lord's scepter there has
`+S` cannot be told.

Stills (`force-wave-footage.jpg`; top row `v7Z2mh3qJho`, bottom row `Zt2_Rbt3-EE`):

| # | time | what is seen |
|---|---|---|
| 1 | 9.0 s | he stands at the dragon's feet, scepter low |
| 2 | 9.3 s | the strike: a **yellow cone flaring up and behind his head** (m_Piercing, MU's own placement; ours is all in front by the user's rule) and the **white-violet streak** from his chest into the dragon |
| 3 | 9.4 s | the streak fading; the small waves and a warm flare low at his feet |
| 4 | 9.6 s | the streak gone; the damage number on the dragon came with the flash. Cycle about **0.8 s** (frames 9.3, 10.1, 11.0, 11.8) |
| 5-8 | 94-97.5 s | PvP in Lorencia (hack in use, so cadence and targets are not MU's): **long white-cyan streaks running straight from a lord through several bodies 4-6 tiles down the line**, numbers on more than one of them -- the shape a struck strip would draw; who cast them cannot be told |

## 6. Fit to our code (read-only look at `muremaster2-darklord/MU2_BGFX`)

**How our items carry the skill option**:
- `sim::Held::skill` exists, "a shop's `+S` line ... **Nothing reads it for a fight yet**", carried for price only
  (`src/sim/items.h:77-80`); saved in item links (`src/sim/item_link.h:50`, `:115`) and the broker (`src/sim/broker.cpp:114`).
- `content::ItemRow::skill` holds the row's skill number, "a skill it grants while it is held" (`src/content/tables.h:111-114`); every
  weapon json carries it (`content/items/weapons/Mace02.json:36` 19, bows 24, ...).
- **No drop ever carries `+S`**: "No skill: skills are orbs here" (`src/sim/realm_items.cpp:1569`), "The skill's 6 in 100 has no
  place here: skills are orbs" (`src/sim/items.h:647-648`); the firecracker and Kundun boxes roll it "and not given"
  (`realm_items.cpp:2349-2354`, `:2494-2498`); the Chaos weapon's +S roll is not taken (`src/sim/machine.h:43`,
  docs/game/chaos-machine.md:35). Only shop shelves set it (`src/sim/market.cpp:15`), and the user removed the +S shelf copies
  (`market.cpp:56-80`, `:158-181`).
- **The precedent for "known while worn"**: Fire Breath, the horn's (`Realm::knows`/`dinorantWorn`, `src/sim/realm_skills.cpp:77-95`).

**Which scepters exist**: none. `content/items/weapons` has Mace01-08 and Mace17 (Frost Mace) only; the plan's ladder (Mace09 Battle
54 ... Mace13 Great Lord 140, Mace14-18) is uncooked (docs/plans/dark-lord.md:146-160, phase 3 at :235-238).

**How Force's row and fx would branch**:
- **No new row, no `kSkills` change** (46, `src/sim/skills.h:545`): Force stays the row (`src/sim/skills.cpp:912-916`), cost,
  reach 4, clip 80/82, `release` 3/7, no cooldown. A `Realm::forceWave()`-style test -- the right hand (`kWeaponRight`,
  `src/sim/items.h:31`) holds a row with `skill == 66` and `Held::skill` set -- read at the let-go.
- **The let-go**: `Realm::release` (`src/sim/realm_fight.cpp:1301-1390`) has per-skill branches ahead of the plain one-body
  `strikeAt` (`:1389`); Force Wave is one more, as Electric Spike's (`:1311-1314`): `Realm::electricSpike`
  (`:2031-2082`) already reads a WebZen `.hit` table -- GetAngle on his aim, `dir = angle / 10`, the 21×21 bitmask
  (`src/sim/electric_spike_box.h`, made by `pipeline/electric_spike_box.py` from `skillElect.hit`). The same script run on
  `skillSpear.hit` gives `kSpearBox`. Differences from Electric Spike: the angle is **toward the aimed body**, not the ground;
  all bodies on the let-go tick (no `sparkReaches` wait); damage 50 in place of Force's 10.
- **The blow**: `strike`'s skill-damage band takes the row's `damage` (10) -- Force Wave needs 50 (rolled to 75) on the same row:
  a `damage` override for the swing, or a field like `waveDamage` on Force's row. The DL flat and multiplier are already applied
  by kin (`kLordForcePerEnergy`, `skills.cpp:984-986`).
- **The drawing**: nothing new needed, MU's way -- `fx/force` is it (`src/game/play.cpp:1593-1594`, `:1993-1995`). If the strip's
  far bodies need a visual of their own (open call 2), a `say(What::Loosed, ...)` per body as Electric Spike does for its off-bolt
  bodies, drawn as a short streak.
- **Content**: the scepters (Mace09-13 at least) with `skill: 66` and their held lights; a way for `+S` to exist (open call 1);
  the tooltip's "Force Wave" rename and "Force wave skill (Mana: 10)" line.

## 7. Open calls (for the design; not decided here)

1. **Where `+S` comes from** -- the blocker. MU: a scepter dropped with the skill option, 6 in 100 (WZ); OM 50%. Here "skills are
   orbs" and no drop has `+S`. Options: (a) WebZen's 6 in 100 on scepters only (excellent always); (b) **every scepter carries
   Force Wave** -- no roll; (c) an orb/scroll. **Recommended: (b)**, ours and marked: MU's own drops give it often enough that a
   lord without it is a lord who has not looked, Force Wave is not a learnable skill in MU (no orb ever existed, so (c) is invented),
   and (a) would bring back a roll the user removed everywhere else. If "true on actual MU" weighs more: (a), scepters only.
2. **The strip's length vs the streaks**: WebZen's table reaches 5-8.5 tiles; Force's streaks are 6 tiles. Bodies 6.5-8.5 out
   would take a number with no light reaching them. **Recommended: WebZen's table, cut at 6.5 tiles along** (ours, marked), as
   Electric Spike dropped the table's row behind him because "a hit there had no visual" (electric-spike.md section 7) -- the
   number lands with the streak. The alternative, the whole table, is MU's numbers exactly.
3. **The aimed body off its own table** (4 of 80 tiles): **Recommended: always strike the aimed body** (OM does), ours and marked --
   equipping a better weapon must never lose the blow Force itself would land.
4. **The shape source**: WebZen's hand-drawn table (as Electric Spike) or OpenMU's 1×4 frustum (the plan's text). **Recommended:
   WebZen's table** -- the plan's line came from OpenMU; the server rule is the table, and the precedent is already built.
5. **Timing**: WebZen strikes as the packet arrives (before the swing); ours lands Force on key 3 with the piercing.
   **Recommended: every struck body on key 3, at once** -- the streaks are full length on their first frame, so the whole line
   lights together; no per-tile wait.
6. **Who**: monsters not summoned (WZ); players only when aimed / dueled / at guild war. **Recommended: monsters only, as Electric
   Spike, plus whatever PvP rule Force already has for the aimed player.**
7. **A broken or unworn-for-stats scepter** still gives Force Wave in WZ (1.2.3). **Recommended: only a scepter that counts as
   worn** (our usual valid-item rule) -- a broken weapon here gives nothing.
8. **Cost and cooldown**: MU's 10 mana, delay 0, Force's own -- **Recommended: unchanged; the right-click primary keeps no cooldown**
   and no new mana price, as in MU.
9. **The look**: MU draws nothing new. **Recommended: Force's fx as built**, all in front of him; no added "wave" model. The
   difference shows as numbers down the line.
10. **Market**: OM prices a `+S` scepter as a plain one ("worthless skill"). If (1b), there is nothing to price.

Blocks a build: **no scepter is cooked** (group 2 ends at Mace08 + Mace17), and **no item can carry the skill option** until open
call 1 is settled. Everything else (the table reader, the release branch, the effect) exists.

## 8. Decided

Settled 2026-10-25. The user picked call 1 and the first scepter ("Every scepter", "Yes, Battle Scepter"); the rest were
settled from section 7's recommendations against "DL has to be true on actual MU", "only frontal" and "the number with the visual":

1. **Every scepter is Force Wave** (ours, marked): a right-hand item whose row's skill is 66 makes Force Force Wave, no `+S` asked;
   no drop gets a skill roll ("skills are orbs" stands). `Realm::forceWaveHeld`.
2. **WebZen's table, `skillSpear.hit`, cut at 6.5 tiles along his aim** (ours, marked: Force's streaks reach six), as
   `src/sim/spear_box.h`, made by `pipeline/electric_spike_box.py` (now naming its symbols).
3. **The aimed body is always struck** (OpenMU's; four of the 80 tiles in reach fall off their own table).
4. **All on the let-go**, key 3 of the strike, each thrown as Force is (`loose`, flies 1000) and landed the next tick; said once.
5. **Monsters only** on the strip, as Electric Spike; the aimed body whatever it is, as Force.
6. **Only a scepter he may wear and not broken** (`fits`, durability > 0) -- WebZen asks neither.
7. **Force's cost and no cooldown**; the skill damage 50..75 in place of 10..15 (`skill::kForceWaveDamage`, `Realm::strikeAt`).
8. **The look is Force's**; MU draws nothing more. Settled again 2026-10-25 (the user: "you decide"): Force's streaks run their
   full six tiles down his facing whatever he aimed at (fx/force.h), and the strip is a tile wide (two in dirs 19 and 28) and cut
   at 6.5, so every body struck is under the streaks -- the number comes with the visual, and nothing invented is added. The tooltip names Force "Force Wave" while a scepter is held, and the scepter's
   card carries "Force wave skill (Mana: 10)" -- with the Battle Scepter (Mace09, MU 2/8, skill 66), cooked in the studio.
