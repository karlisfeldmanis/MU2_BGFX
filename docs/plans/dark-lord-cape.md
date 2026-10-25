# Cape of Lord (13/30) -- research

The user, 2026-10-25: "how cape is made and how it works". Research only; nothing built.

Source shorthand:

- **WZ** = WebZen GameServer 1.00.93, github.com/ptr0x-real/Mu-GS-Webzen-MC-10093 (shallow clone,
  cp949). `GS/` = `Source/Server Side/GameServer/`, `BC/` = `Source/Base Common Src/`. The cape is
  all under `DARKLORD_WORK`, defined inside `#if GAME_VERSION >= G_V_99B` (BC/include/define.h:4042,
  :4059), i.e. the 0.99 base. Other flags are named where they matter; all cited ones are defined in
  this build (BC/include/define.h, GS/define2.h). The repack adds a few `g_GlobalConfig` knobs that
  are **not WebZen's** (marked "repack").
- **MuMain** = LEGACY/reference/MuMain/src/source/ (S3-ish client, later code merged in).
- **1.03** = MU2_BGFX/third_party/Main 1.03.35/.../source/ (GBK; Chinese 1.03.35, 2011).
- **OpenMU** = LEGACY/reference/openmu/src/.

## 1. What it is

| | value | source |
|---|---|---|
| Row | group 13 (helpers) number 30, slot 7 (the wing slot), 2x3 | WZ `_Server Files/Data/lang/main/item.txt:390` (`30 7 0 2 3 1 1 0 "군주의망토" 180 200 ... DL=1`) |
| Name | 군주의 망토, "Cape of Lord" | item.txt:390; MuMain `ITEM_CAPE_OF_LORD = ITEM_HELPER + 30` (Core/Globals/_enum.h:2729) |
| Class | Dark Lord only | item.txt:390 class columns |
| Drop | never drops (drop column 0); Chaos Machine only | item.txt:390 |
| Item level (drop level) | 180 | item.txt:390; OpenMU Items/Wings.cs:88 |
| Level requirement | **180 + 4 a plus** (helper-row rule) | WZ BC/Common/zzzitem.cpp:629-636 (the ring/pendant `else`); MuMain Engine/Object/ZzzInfomation.cpp:749-754 (`ip->Type >= ITEM_HELPER`, addValue 4) |
| | WZ's server then adds +20 when any of the low six option bits is set (zzzitem.cpp:653-663), and every cape carries 0x20 (see 2), so WZ's server asks **200 + 4L**; the client skips the +20 for wings (ZzzInfomation.cpp:854, `!IsWingItem`) and shows 180 + 4L. OpenMU: flat 180 (Wings.cs:88, :211 TODO) | |
| Defence | **15 + 2L**, plus the triangle (L-9)(L-8)/2 from +10 | WZ zzzitem.cpp:902-912 (`MODIFY_ACCUMULATED_BUG_SLEEVEOFLORD_01_20050531` for the triangle); MuMain ZzzInfomation.cpp:647-650, :690-697 (client reads +2/level to +9, then +L-6) |
| Durability | 200 base, + plus by `ItemGetDurability` (+1/plus to +4, then 2L-4 ... +13: 2L+6); never the excellent +15 | WZ zzzitem.cpp:4850-4905, :4922-4924 (`MODIFY_UPGRADE_DARKLORDMANTLE_20060515`); MuMain ZzzInventory.cpp:1628-1640 |
| Max plus | +13 in WZ 0.99-S4 (jewels and the +10..+13 mix take it like a wing) | GS/user.cpp:28486-28497 (`gObjItemLevelUp` refuses `>= 12,7` **except** 13/30); GS/MixSystem.cpp:1660-1671 (the plus mix grades it as a wing, `G_V_99B`) |
| Price | 40,000,000 + (40+L2)·L2²·11 like a 1st/2nd wing (L2 = 180 + 3/plus + bonus) | MuMain ZzzInfomation.cpp:1904-1907 |

Stats by plus (WZ formulas above):

| + | def | req (client / WZ server) | dmg up | Command opt | HP/MP opt | dur |
|---|---|---|---|---|---|---|
| 0 | 15 | 180 / 200 | 20% | +10 | +50 | 200 |
| 5 | 25 | 200 / 220 | 30% | +35 | +75 | 206 |
| 9 | 33 | 216 / 236 | 38% | +55 | +95 | 214 |
| 11 | 38 | 224 / 244 | 42% | +65 | +105 | 221 |
| 13 | 47 | 232 / 252 | 46% | +75 | +115 | 232 |

## 2. How it is made

**The recipe is the 2nd wing mix with the Crest in place of the feather** -- one function,
`CMixSystem::WingChaosMix` ("2차 날개/망토 조합", GS/MixSystem.cpp:2469-2945).

| | | source |
|---|---|---|
| Box | **exactly** 1 first wing (12/0-2), 1 Jewel of Chaos, 1 **Crest of Monarch**, 0 Loch's Feathers | MixSystem.cpp:2523-2531, :2541-2553, :2636-2645 |
| Optional | any number of excellent items at +4 or more, to raise the rate | :2573-2577 (`WING_MIX_MODIFY`) |
| Refused | a 2nd wing, Cape or 14/30 already in the box | :2508-2521 |
| Zen | 5,000,000 (+ castle tax) | :2681-2691 (`MU_CASTLESIEGE_TAX_SYSTEM_20041219`) |
| Rate | `wing price / 4,000,000 + sum(excellent +4 items' price) / 40,000`, capped 100, then + Charms of Luck (`MODIFY_PCS_CHAOSMIX_REFACTORING_20070903`, at most 10) | :2707-2708, :2737-2741, :2560-2563, :2667-2672 |
| | The wing's own plus, luck and option raise it only through its price (zzzitem.cpp:2469-2486 Level2 table; luck x1.25, option +60/140/280/560%, :2533-2573). Nothing else matters (the old `level*4 + luck*5 + opt*4` line is commented out, :2702-2705) | |
| | Worked: +0 plain Satan 13%; +9 plain 33%; +9 luck 41%; +0 luck +4 opt 27% (my arithmetic on that formula) | |
| | repack floor `MinWingMixRate` (Global.cpp:72; =1 in commonserver.cfg:276) -- not WebZen. OpenMU caps at 90 (ChaosMixes.cs:556) | |
| Fail | the whole box is cleared (wing, jewel, crest, excellents) | :2908-2913 |
| Success | Cape of Lord **+0**, into the box | :2762-2767, :2880 (`itemlevel = 0`) |

What the result rolls (:2817-2868):

| roll | chance | result |
|---|---|---|
| Luck | 1 in 5 | luck (crit +5%, Soul success +25%: AT_LUCK, zzzitem.cpp:1072-1076; ZzzInventory.cpp:1945) |
| Add. option | pick 1 of 3 columns, then 4% / 10% / 20% | +3 (12 dmg) 1.33%, +2 (8) 3.33%, +1 (4) 6.67%, none 88.7% |
| Option kind | always **additional damage** | `SLEEVEOFLORD_OPTION_BUG_PATCH` forces `PLUS_WING_OP1_TYPE` (:2861-2864; define.h:3313-3314 "망토 엑셀옵션 버그 수정 -- 무조건 공증옵션"); zzzitem.cpp:1243-1259 |
| Wing extra | 1 in 5, then one of **four** bits (`1<<(rand()%4)`) -- 5% each | 0x01 max HP, 0x02 max MP, 0x04 ignore defence 3%, **0x08 Command** (:2852-2855). 2nd wings roll 1<<(rand()%3), so only the cape can get Command (:2869-2872) |

**Crest of Monarch** = **Loch's Feather 13/14 at item level 1** (군주의 소매, "Sleeve of the Lord"):

- The mix counts a level-0 feather as a feather and any other level as a Crest (MixSystem.cpp:2545-2553).
- Drop (GS/gObjMonster.cpp:5464-5478): when `IsDropDarkLordItem` is on (commonserver.cfg,
  default 0, Gamemain.cpp:3654), any monster of level >= `SleeveOfLordDropLevel` (default **86**,
  :3656) leaves one with `rand()%10000 < SleeveOfLordDropRate` (default **10**, i.e. 0.1%,
  :3655), on its own roll before the normal drop. The "Icarus only" switch (`DropMonarchOnlyIcarus`,
  Global.cpp:110) is the repack's. OpenMU: Icarus, monster level 82+, 0.001 (Maps/Icarus.cs:39-47).
- Sell price 7,500,000 (zzzitem.cpp:1823-1828).
- Drawn as `Data/Item/DarkLordSleeve.bmd` (+ `DarkLordSleeve.OZT` 32x64) on the ground and in the bag:
  MuMain ZzzOpenData.cpp:1015, ZzzObject.cpp:6174-6177, ZzzInventory.cpp:10483-10486, rotated
  (-90,0,0) in the bag (ZzzInventory.cpp:8222-8225). **Not** `Quest05.bmd` -- that is
  MODEL_LOCHS_FEATHER+1 = MODEL_FRUITS (_enum.h:2025-2026; ZzzOpenData.cpp:1010-1013), so
  dark-lord.md's line 183 is wrong on that.

Later: the cape is also a "2nd wing" input of the 3rd wing mix (MixSystem.cpp:5489-5500
`Check2ndWingItem`, `ADD_THIRD_WING_20070525`) -- the road to the Cape of Emperor (12/40). OpenMU
lists it there too, at +9 or more (ChaosMixes.cs:387-390).

## 3. How it works in play

Server, per hit (GS/ObjAttack.cpp; `gObjWingSprite` counts the cape as a wing, GS/user.cpp:10433-10468):

| effect | formula | source |
|---|---|---|
| Damage dealt | x(120 + 2L)% -- **20% + 2 a plus** (2nd wings: 32 + L) | ObjAttack.cpp:1176-1182 (`NEW_FORSKYLAND3`) |
| Life cost | 3 Life per successful hit (non-wizard rule) | ObjAttack.cpp:1146-1160 |
| Damage taken | **none in WZ 1.00.93**: the absorb block is skipped for 13/30 | ObjAttack.cpp:1222-1225 |
| | Clients say **absorb 10 + L %** (MuMain ZzzInventory.cpp:4562-4569; 1.03 ZzzInventory.cpp:6052-6060, both under `PBG_MOD_NEWCHAR_MONK_WING_2`, 2010-10-21, 1.03 Winmain_New.h:173). OpenMU: 10% + 2/plus (Wings.cs:88, :160-163; WingsInitializerBase.cs:108-120). So the absorb came later than this server; open question 1 | |

Stats (GS/ObjCalCharacter.cpp, BC/Common/zzzitem.cpp):

| stat | formula | source |
|---|---|---|
| Defence | item defence above, added like any armour | ObjCalCharacter.cpp:1079 |
| Additional damage (option) | +4 a level (+4/+8/+12, +16 by Jewel of Life), min and max, both hands; scaled down by wear | ObjCalCharacter.cpp:473-477; zzzitem.cpp:3029-3035 |
| Max HP / Max MP (0x01 / 0x02) | +50 + 5L | ObjCalCharacter.cpp:1264-1265; zzzitem.cpp:3039-3044 |
| Ignore defence (0x04) | 3% of hits (only while durability >= 1) | zzzitem.cpp:3599-3619 |
| **Command (0x08)** | **+10 + 5L** to `AddLeadership` (the Command shown in brackets, read by Command requirements and the Command-scaled skills/pets) | ObjCalCharacter.cpp:1268-1273 (`MODIFY_SETITEM_LEADERSHIP_BUGFIX`); zzzitem.cpp:3054-3059, :1505-1508; client ZzzInfomation.cpp:3575-3578, :912-917 |
| Base Command | none -- the item row's `m_Leadership` is never set, so only the 0x08 option gives Command | ObjCalCharacter.cpp:480-483; zzzitem.cpp:97, :688 |
| Luck | as a wing's (crit chance) | ObjCalCharacter.cpp:544 |

Special rules:

- **Counts as wings for maps**: lets him into Icarus (user.cpp:26299-26304, MoveCommand.cpp:319-322)
  and Kanturu (Kanturu.cpp:631-636). Client Move window the same (MuMain
  UI/NewUI/HUD/NewUIMoveCommandWindow.cpp:381).
- **Counts as wings for the body**: the client puts him in `PLAYER_FLY` / the wing run speed (15)
  outside safe zones, as any `c->Wing.Type != -1` (MuMain ZzzCharacter.cpp:615-622, :6320-6335).
  So in MU the cape-wearer floats/glides like a winged hero; it is not "no flight".
- Wear and repair: the wing slot's (durability by the hour; our `kWingWearTicks`, sim/items.h:195-198).
- Preview byte: `CharSet[5] |= 0x0c`, `CharSet[9] |= 0x05` (user.cpp:19940-19946); MuMain decodes 5 to
  MODEL_CAPE_OF_LORD (ZzzCharacter.cpp:12563).
- Tooltip (MuMain): name "Cape of Lord" / "+N" (ZzzInventory.cpp:2522-2528); "Increase damage N%"
  20+2L and "Absorb N% damage" 10+L (:4562-4569); option "Additional damage +N"; luck line; the
  extras "HP +N", "Mana +N", "Ignore opponent's defensive power by 3%", "Command +N"
  (ZzzInfomation.cpp:889-917; text keys ZzzInventory.cpp:1945-2055). Blue when it has an extra,
  yellow at +7 (ZzzInventory.cpp:2222-2240). Durability line shown (a helper row,
  ZzzInventory.cpp:4572-4578).

## 4. How MuMain draws it

**The worn cape is not a mesh -- it is a simulated cloth sheet.** `DarkLordRobe.bmd` is only the
bag/ground model: `RenderLinkObject` skips drawing it on the body (ZzzCharacter.cpp:7006-7012), and
the cape you see is cloth #2 of the Dark Lord's cloth set.

Assets (MuMain ZzzOpenData.cpp; LEGACY/reference/MuMain/src/bin/Data):

| asset | use | source |
|---|---|---|
| `Item/DarkLordRobe.bmd` + `Item/DarkLordRobe.OZT` (128x256 TGA) | item in bag/ground (rot -90,0,0) | ZzzOpenData.cpp:1016, :1405; ZzzInventory.cpp:8218-8221 |
| `Player/DarklordRobe.OZT` (same bytes as the item's) -> BITMAP_ROBE+7 | **the cloth's texture**, front and back | ZzzOpenData.cpp:444 |
| `Monster/iui06.OZJ` (32x64) -> BITMAP_ROBE+6 | his two shoulder/hair tails, cloth 0-1 (always on, cape or not) | ZzzOpenData.cpp:5189 |
| `Player/kaa.OZT` (64x64) -> BITMAP_DARK_LOAD_SKIRT | his armour's skirt, cloth 3 (only with his own default armour) | ZzzOpenData.cpp:5192 |
| `Item/DarkLordSleeve.bmd` | the Crest, not the cape | ZzzOpenData.cpp:1015 |

The Dark Lord's cloth set, built once per character (ZzzCharacter.cpp:9691-9765), all
`CPhysicsCloth::Create(o, bone, x, y, z, cols, rows, width, height, front, back, flags)`:

| # | bone | pin (x,y,z) | grid | w x h | tex | flags | collision spheres (x,y,z,r,bone) |
|---|---|---|---|---|---|---|---|
| 0 | 20 | 0,0,20 | 6x5 | 30x70 | ROBE+6 | CURVED RUBBER2 MASK_LIGHT SHORT_SHOULDER OPT_HAIR, wind 10-50 | (±10,20,20,27,17) |
| 1 | 20 | 0,5,18 | 5x5 | 30x70 | ROBE+6 | CURVED RUBBER2 MASK_BLEND ..., wind 8-40 | same |
| **2 cape** | **19** | **0,8,10** | **10x10** | **180x180** | **ROBE+7** | **CURVED, SHORT_SHOULDER, MASK_ALPHA** (cotton, normal weight) | (±10,-10,-10,25,17), (±10,-10,20,27,17) |
| 3 | 18 | 0,10,-5 | 5x5 | 50x90 | skirt | MASK_ALPHA HEAVY STICKED SHORT_SHOULDER | (0,-15,-20,30,2) |

Cloth 2 is drawn only when the wing is the Cape of Lord / Emperor / small cape, or in a team colour
(soccer, duel: `CloakLight` red 1,0.2,0 / blue 0,0.2,1) (ZzzCharacter.cpp:9928-9937, :9190-9247);
not under Cloaking, in Chaos Castle, or in Illusion Temple (:9925, :9186, :9965). 1.03 has the
same numbers (chr103 ZzzCharacter.cpp:11389-11423, :11618-11627). Each frame:
`Move2(0.005, 5)` -- five steps of 0.005, step 0 when stunned/asleep; a broken cloth is deleted and
rebuilt (ZzzCharacter.cpp:9969-9983).

The simulation (Engine/Physics/PhysicsManager.cpp), enough to rebuild:

1. **Grid** (Create, :327-469): rows `j` top to bottom, columns `i`. Unit = w/(cols-1), h/(rows-1).
   SHORT_SHOULDER narrows row j to `w * (0.6 + 0.4 j/(rows-1))` (:367-372, RATE 0.6 :18) -- a
   trapezoid, 108 wide at the shoulders, 180 at the hem. Rest position `(i·u - w/2, 20, -j·v)`;
   CURVED bows it back `y -= 10·(2|i/(cols-1) - 0.5|)²` (:395-400). Placed with the owner's angle
   and bone's translation.
2. **Links** (:415-466): vertical (spring + strict distance), horizontal (spring + loose), both
   diagonals (loose). Each link keeps [0.8·d, d] of its rest length d.
3. **Pins** (SetFixedVertices, :502-563): the **top row only** is fixed, re-placed every step in
   bone 19's frame at `(i·u' - 0.6w/2 + x, y_pin, z)` with the CURVED bow, axes swapped
   `(z, -y, x)`. Nothing else is attached to the body; spheres keep it off the back.
4. **Forces** (UpdateForce, :49-118; InitForces :629-636): per free vertex
   `F = k·wind - 0.01·vel`, gravity `-9.8 · 0.0025 · 100` on z (180 for HEAVY), all x
   FPS_ANIMATION_FACTOR (25/fps, clamped to 1, Engine/AI/ZzzAI.cpp:726-730). `k` is 0 or 3..6 by a
   seed vertex that moves every 400 ms (a travelling gust). Wind vector =
   `s_fWind · (sin(180+yaw), -cos(180+yaw))` -- blowing from his front, so the cape trails behind
   (:600-603); `s_fWind` random-walks by ±0.1 a frame in [-0.2, 1.0] (:1194-1199). RUBBER2 cloths
   (the tails) roll their own wind in [min, max]/100 and add it upward.
5. **Springs** (MoveVertices, :638-689): an over-stretched link pulls both ends by its excess.
   Integrate `vel += F·400·dt; pos += vel·dt` (:126-137, mass 0.0025).
6. **Constraints** (PreventFromStretching, :691-757): spheres move to their bones and push vertices
   out (averaged one-time moves); loose links relax to rest length (half each side, averaged);
   strict vertical links clamp the lower vertex into [0.8d, d] -- and give up (cloth deleted) past 20d.
7. **Draw** (Render :759-833, RenderFace :835-883): GL_QUADS per cell, uv `(i/(cols-1),
   min(0.99, j/(rows-1)))`, both faces with the same texture, alpha-tested, colour = CloakLight
   (white), no lighting. Shadow: the cloth goes into the body shadow (ZzzCharacter.cpp:8435-8442).

No glow, sprite or particle on the Cape of Lord; its plus shows nothing (the wing render forces
level 0, and the cape is not drawn as a model at all). The MASK_LIGHT light sprites belong to
cloth 0, the tails (PhysicsManager.cpp:794-813).

## 5. What we have and what is missing

| piece | ours today | source |
|---|---|---|
| Kin and Command | `Kin::DarkLord`, `Stats::command`, `ratePerCommand` | src/sim/rules.h:298, :337-369 |
| Wing slot, wear, defence, power | 1st/2nd wings only: `anyWing`, `wingDefense` (2/plus + triangle = the cape's), `wingPower` (2nd wing 132+L / 75-2L) | src/sim/items.cpp:465-511 |
| Wing extras bits | 0x01/0x02/0x04/0x20 -- **no 0x08 Command** | src/sim/items.h:224-232 |
| Option kind | `wingOption` per wing; the cape would be Damage always | src/sim/items.cpp:472-485 |
| 2nd wing mix | `Recipe::SecondWings`, WebZen's rate and rolls, 1,500,000 Zen (ours) | src/sim/machine.h:106-117, machine.cpp:454-480, realm_machine.cpp:386-398 |
| Dark Lord's answer | `secondWingOf/firstWingOf(DarkLord)` fall through, "phase 6" | src/sim/machine.cpp:990-1027 |
| Loch's Feather | `lochsFeather()` ignores its level -- a +1 feather would count as a feather | src/sim/items.cpp:469; machine.cpp:154 |
| Drawing | `WingLook`: a rigid rigged model on Bone05 at (0,0,15), flap clip | src/game/wings.h:1-80 |
| Cloth | **none** in src/game | grep "cloth" src/game: nothing |
| Plan | dark-lord.md row 7 and §7 "Wings, pets, misc" line 182 | docs/plans/dark-lord.md:26, :182 |

Missing, in build order:

1. **Sim row**: Cape of Lord, a wings-number of ours (MU's 13/30 is in our pets group 13? check
   the free numbers; dark-lord.md:182 says "a free wings number"), drop level 180, def 15, dur 200,
   req 180 + 4L, DL only, option Damage, power x(120+2L), absorb per question 1, extras bits
   including **0x08 Command +10+5L** into Command. `anyWing` / `wingDefense` / `wingPower` / the
   map gates (Icarus) learn it.
2. **Crest of Monarch**: Loch's Feather +1 (a `Held` refinement 1) or its own row; `lochsFeather`
   must tell the two apart; a drop rule (WebZen: level 86+, 0.1%, any map; OpenMU: Icarus).
3. **Mix**: `Recipe::SecondWings` with the Crest -> the cape (or its own recipe as OpenMU's
   "Cape of Lord/Fighter", number 24); its roll uses 1<<(0..3) and forces the damage kind.
4. **Draw**: a cloth sheet on bone 19, 10x10, 180x180 MU units, the PhysicsManager steps above, its
   texture DarklordRobe.OZT; plus his two tails (iui06) if wanted. First fallback (dark-lord.md:182):
   a rigged cape like the wings. Bag icon from DarkLordRobe.bmd.
5. Tooltip: the two percent lines, the extras, Command.

## 6. Open questions for the user

1. **Absorb**: WebZen's server (1.00.93) gives the cape **no** damage absorb; the 2010+ clients say
   10 + L % and OpenMU gives 10 + 2L %. Which?
2. **Level requirement**: 180 + 4L (client, ours-style) or WebZen server's 200 + 4L?
3. **Crest drop**: WebZen's (any map, monster 86+, 0.1%) or Icarus only (OpenMU / repack), or our
   feather rule (Tarkan, Icarus, BC6)?
4. **Zen**: WebZen's 5,000,000, or our 2nd-wing 1,500,000?
5. **Flying**: MU puts him in the fly pose with the cape outside towns. Keep it?
6. **Draw**: real cloth (the MuMain sim, a new CPU system per cape-wearer) or a rigged cape first?
7. **His tails and skirt** (cloths 0, 1, 3) are drawn on every Dark Lord in MuMain, cape or not --
   in scope now?

History (web, lowest weight): the Dark Lord and his cape came with the 0.99 / 1.0 era (WebZen's
`DARKLORD_WORK` sits under `G_V_99B`); fan guides of the time list the Cape of Lord as Chaos
Goblin-only, from a 1st wing, a Jewel of Chaos, the Crest and an optional +4 excellent item
(https://ganjateam.forumgratuit.ro/t15-chaos-machine-guide).
