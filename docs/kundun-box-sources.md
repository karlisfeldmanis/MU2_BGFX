# Box of Kundun (14,11 lvl 8-12): research notes (2026-10-02)

Status: complete. Sections appear in the order they were written (2, 3, 4.1, 1, 4.2-4.4,
5, 6); section 6 lists what stays uncertain.

Legend: **[SRC]** = read directly in a WebZen file (URL given); **[WEB]** = third-party page;
**[?]** = uncertain / not verified.

Primary sources used (both WebZen's own, both on GitHub):

- **WZ0996** = WebZen's official MU server package "0.99.60T" (Files.rar, data dated
  2004-08 .. 2005-07, item text marked "Version 1.00m"), repo
  https://github.com/makaytrue/0.99.60T (archive https://github.com/makaytrue/0.99.60T/blob/master/Files.rar).
  Its `Files/Data/eventitembag8..12.txt` and `commonserver.cfg` are WebZen's own Korean-commented
  files (no private-server repack comments). Extracted copy:
  `scratchpad/wz0996/Files/Data/`; resolved lists: `scratchpad/bags_resolved.txt`.
- **WZ10093** = WebZen GameServer 1.00.93 source (0.97d..Season 4.6 behind #ifdefs),
  https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093 , paths under
  `Source/Server Side/GameServer/`. Its `_Server Files/Data/eventitembag*.txt` are a 2010
  private repack ("// Kundum +1", English comments) -- NOT WebZen's lists.

---

## 2. WebZen's original item lists (+1 .. +5), pre-Season-4 format

### 2.1 File format [SRC]

Row = `type index level skill EXFLAG option` (6 numbers). Loader
`ItemBag.cpp` (WZ10093) reads them into `m_type, m_index, m_level, m_op1, m_op2, m_op3`;
**if `m_op2` (5th column) != 0 the row goes to the Excellent pool, else to the Normal pool**
(`ItemBag.cpp:76-82, 118-124`).
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/ItemBag.cpp

WebZen's own header in eventitembag8.txt says `// skill 이 1이면 엑설런트용 아이템이다`
("if skill is 1 it is an item for excellent") above `// type, index, level, skill, option` --
the comment names the wrong column; the code keys on the 5th. Columns 4 (skill) and 6
(option) are never read by the box-open code (it rolls skill/luck/option itself).
Each file has two sections, `//일반아이템` (normal items) and `//엑설런트` (excellent).

### 2.2 The roll (pre-2009 body) [SRC]

`Event.cpp:603 EledoradoBoxOpenEven(lpObj, boxtype, addlevel, money)` (the `#else` of
`MODIFY_KUNDUN_EVENTBAG_RENEWAL_2009_02_04`), called from `protocol.cpp:5729-5769` when the
player drops 14,11 lvl 8..12 on the ground:

| Box | lvl | bag file | addlevel | Zen if no item |
|---|---|---|---|---|
| +1 | 8 | eventitembag8.txt | 2 | 50,000 |
| +2 | 9 | eventitembag9.txt | 2 | 100,000 |
| +3 | 10 | eventitembag10.txt | 2 | 150,000 |
| +4 | 11 | eventitembag11.txt | 1 | 200,000 |
| +5 | 12 | eventitembag12.txt | 0 | 250,000 |

https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/Event.cpp
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/protocol.cpp

1. `rand()%100 < ItemDropRate` else -> Zen pile at the feet (`MapC.MoneyItemDrop`).
2. Inside that, `ExItemCount>0 && rand()%100 < ExItemDropRate` -> **excellent**: random row of the
   Ex pool, **level 0**, skill = 1 always, luck 50%, `ExOption = NewOptionRand(0)`; if luck
   missed, option +12 at 20% else +0/+4/+8 (rand%3).
   So P(excellent) = ItemDropRate x ExItemDropRate (nested, not independent).
3. Else -> **normal**: random row of the Normal pool, level = row level + `rand()%addlevel`
   (addlevel 0 = exact), skill 50%, luck 50%, option as above when skill or luck missed
   (if both hit, option stays +0). No excellent options.
4. Jewels (12,15 Chaos, 14,13 Bless, 14,14 Soul, and in the normal branch 14,16 Life) are forced
   to level 0, no options. Pets/rings/pendants (13,0 13,1 13,2 13,8 13,9 13,12 13,13) forced to
   level 0.
5. Uniform pick within a pool (every row equally likely, duplicates count twice).

### 2.3 WebZen's rates [SRC]

Compiled defaults (`Gamemain.cpp:3698-3707`): all ten = **10** (ItemDropRate 10%, ExItemDropRate
10% -> 1% excellent).
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/Gamemain.cpp

**WebZen's shipped commonserver.cfg (WZ0996, 0.99.60T) overrides them, tiered:**

```
IsEledoradoEvent = 1
EledoradoGoldGoblenItemDropRate      = 30 ; +1   ExItemDropRate = 5
EledoradoTitanItemDropRate           = 25 ; +2   ExItemDropRate = 4
EledoradoGoldDerconItemDropRate      = 20 ; +3   ExItemDropRate = 3
EledoradoDevilLizardKingItemDropRate = 15 ; +4   ExItemDropRate = 2
EledoradoDevilTantarosItemDropRate   = 10 ; +5   ExItemDropRate = 1
```
(file: Files.rar -> Files/Data/commonserver.cfg, https://github.com/makaytrue/0.99.60T)

Effective odds per box with the shipped cfg (computed from the code in 2.2):

| Box | any item | excellent | normal item | Zen |
|---|---|---|---|---|
| +1 | 30% | 1.5% | 28.5% | 70% x 50k |
| +2 | 25% | 1.0% | 24.0% | 75% x 100k |
| +3 | 20% | 0.6% | 19.4% | 80% x 150k |
| +4 | 15% | 0.3% | 14.7% | 85% x 200k |
| +5 | 10% | 0.1% | 9.9% | 90% x 250k |

Note the inversion: the higher box gives better items but **less often**.

### 2.4 The lists (WZ0996 eventitembag8..12.txt; names from the package's own
`lang/vtm/item(Vtm).txt` English item text and `item(Kor).txt`)

Full resolved dump with Korean names: `scratchpad/bags_resolved.txt`. Source for all of 2.4:
https://github.com/makaytrue/0.99.60T (Files.rar -> Files/Data/eventitembag8.txt .. 12.txt).

**+1 (eventitembag8, header `// 황금고블린` "Golden Goblin")**
- Normal (39 rows, file level 5 -> drops +5/+6; jewels/pets at 0):
  Jewel of Chaos, Jewel of Soul, Jewel of Bless; Guardian Angel, Imp, Horn of Uniria;
  Leather set (helm/armor/pants/gloves/boots), Pad set, Vine helm/armor/pants/gloves
  (the Vine boots row is a WebZen typo: `11 5` Leather Boots twice); Horn Shield, Kite Shield,
  Buckler; Skull Staff, Angelic Staff, Serpent Staff; Kris, Rapier, Sword of Assassin;
  Hand Axe, Double Axe; Mace, Morning Star; Dragon Lance; Bow, Elven Bow, Crossbow,
  Golden Crossbow, Arquebus.
- Excellent (35 rows, level 0): Ring of Ice, Ring of Poison, Pendant of Lighting, Pendant of
  Fire; Leather, Pad, Vine full sets (here Vine Boots is correct); Horn Shield, Kite Shield,
  Buckler; Kris, Short Sword, Rapier, Sword of Assassin; Double Axe; Mace, Morning Star;
  Dragon Lance; Bow, Crossbow, Golden Crossbow; Skull Staff, Angelic Staff.

**+2 (eventitembag9, header `//지하군단 습격` "Underground Legion raid")**
- Normal (33, file level 4 -> +4/+5): Chaos, Soul, Bless, **Life**; Guardian Angel, Imp;
  Wind, Brass, Sphinx sets; Light Saber, Legendary Sword, Double Blade; Axe of Nike,
  Larkan Axe; Great Hammer; Berdysh, Light Spear, Serpent Spear; Lighting Staff;
  Tiger Bow, Battle Bow, Light Crossbow.
- Excellent (31): 4 rings/pendants; Scale, Bone, Silk sets; Serpent Staff; Light Saber,
  Blade, Double Blade; Axe of Nike, Larkan Axe; Great Hammer; Berdysh, Light Spear;
  Light Crossbow, Tiger Bow, Serpent Crossbow.

**+3 (eventitembag10, header `//골드 데르콘 습격` "Gold Derkon raid")**
- Normal (26, level 4 -> +4/+5): Chaos, Soul, Bless, Life; Plate, Spirit, Sphinx sets;
  Legendary Sword, Giant Sword, Lighting Sword; Serpent Spear; Silver Bow, Serpent Crossbow;
  Gorgon Staff.
- Excellent (24): 4 rings/pendants; Wind, Brass, Sphinx sets; Lighting Staff; Legendary
  Sword, Giant Sword; Crescent Axe; Serpent Crossbow.

**+4 (eventitembag11, header `//아틀란스 습격` "Atlans raid")**
- Normal (26, level 4 -> exactly +4, addlevel 1): Chaos, Soul, Bless, Life; Dragon, Guardian,
  Legendary sets; Dragon Shield; Legendary Staff; Aquagold Crossbow, Bluewing Crossbow;
  Crystal Sword, Crystal Morning Star; Balrog's Scythe.
- Excellent (18): 4 rings/pendants; Plate, Spirit sets; Serpent Spear, Lighting Sword,
  Silver Bow, Gorgon Staff.

**+5 (eventitembag12, header `//칸투르의지하군단` "Kantur's Underground Legion")**
- Normal (16, exactly +4): Chaos, Soul, Bless, Life; Black Dragon set; Atlans armor/pants/
  gloves/boots (no helm row: Atlans is a Magic Gladiator set and MG sets have no helm); Staff of
  Destruction; Saint Crossbow; Sword of Destruction.
- Excellent (25): 4 rings/pendants; Dragon, Guardian, Legendary sets; Legendary Staff;
  Aquagold Crossbow, Bluewing Crossbow; Crystal Sword, Crystal Morning Star; Balrog's Scythe.

**Pattern:** each box's Excellent pool is roughly the *previous* box's Normal tier
(+3 ex = +2 normal sets, +4 ex = +3 normal sets, +5 ex = +4 normal sets). Jewel of Life only
from +2 up. Pets (Angel/Imp) only in +1/+2; Uniria only +1. No wings, no Kundun-related item,
**no Lost Map, no Symbol of Kundun in any of the five boxes.**

No dark-lord (Dark Lord/Season 1) or Season 2 items appear: these lists are 0.99/1.00-era.
They contain Black Dragon / Sword of Destruction / Staff of Destruction / Saint Crossbow
(late-0.9x additions [?] exact version) and Golden/Atlans MG gear. [?] whether the 0.97d Korean
lists were identical; no older file found (see 2.6).

### 2.5 Season 4+ (2009-02-04 renewal) format [SRC]

`MODIFY_KUNDUN_EVENTBAG_RENEWAL_2009_02_04` swaps `CItemBag` for `CProbabilityItemBag`
(`ProbabilityItemBag.cpp`): file sections are (0) per-map drop info (isDrop, min/max monster
level), (1) header: drop Zen, event item type/index/level, EventItemDropRate, ItemDropRate,
ExItemDropRate, then rate groups whose weights are out of 10000 (`s_nMaxDropRate`), each
followed by rows `type index minLevel maxLevel skill luck option exc`. The box then calls
`DropEventItem(...)` instead of the hand-written roll.
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/ProbabilityItemBag.cpp
WebZen's own S4 data files for these boxes: NOT found [?].

### 2.6 Other files checked

- WZ10093 `_Server Files/Data/eventitembag8.txt`: begins `// Kundum +1`, English comments,
  every row ex-flag 1 (excellent-only), includes Silk set and Katana/Tomahawk/Short Bow --
  a private repack, not WebZen's.
  https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/_Server%20Files/Data/eventitembag8.txt

### 2.7 OpenMU's version (for comparison, NOT WebZen) [SRC]

OpenMU VersionSeasonSix `Items/BoxOfLuck.cs` makes every Kundun box a 20% **excellent-only**
roll over a wide list (+1 includes Bronze/Silk/Violent Wind/Red Wing sets, Mystery Stick...),
else the box's Zen (50k..250k). Local: `LEGACY/reference/openmu/src/Persistence/Initialization/VersionSeasonSix/Items/BoxOfLuck.cs:360-560`;
upstream https://github.com/MUnique/OpenMU/blob/master/src/Persistence/Initialization/VersionSeasonSix/Items/BoxOfLuck.cs
Its Version095d gives the golden mobs the box at Chance 1 (`AddBoxOfKundunToMonster`, level 7+N).
Neither matches WebZen's 0.99 files (normal + excellent pools, tiered rates).

---

## 3. Golden monsters -> boxes, spawns [SRC first]

### 3.1 Pre-renewal (0.97d .. Season 4.0), WebZen code + 0.99.60T data

Box on death: `gObjMonster.cpp:4107-4192` (the `#ifndef ADD_GOLDEN_EVENT_RENEWAL_20090311`
branch, inside `#ifdef ELEGORADO_EVENT`): one 14,11 at the stated level, owned by the top
damage dealer (`gObjMonsterTopHitDamageUser`).
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/gObjMonster.cpp
Spawn: `EledoradoEvent.cpp` (`RegenGoldGoblen` etc.), random walkable tile in a box of the map.
https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/EledoradoEvent.cpp
Monster stats/names/counts: WZ0996 `Monster.txt`, `MonsterSetBase.txt` (https://github.com/makaytrue/0.99.60T).

| Class | WZ0996 Korean name (Monster.txt) | Lvl | HP | Count (MonsterSetBase) | Map (EledoradoEvent.cpp, pre-renewal) | Drops |
|---|---|---|---|---|---|---|
| 43 | 황금버지드래곤 Golden Budge Dragon | -- | -- | (separate golden-dragon invasion, not Eldorado) | Lorencia etc. | **Box of Luck** (14,11 lvl 0) |
| 78 | 황금고블린 Golden Goblin | 20 | 3,100 | 1 | Lorencia **or** Noria (rand of 2) | **+1** |
| 53 | 황금타이탄 Golden Titan | 53 | 8,000 | 1 | Devias | **+2** |
| 54 | 황금솔져 Golden Soldier | 46 | 3,500 | 10 (escort round the Titan, +-4 tiles) | Devias | normal drop [?] |
| 79 | 황금데르콘 Golden Derkon | 80 | 23,000 | 2 | Lorencia / Noria / Devias (rand of 3, both on the same map) | **+3** |
| 80 | 황금리자드킹 Golden Lizard King ("Devil Lizard King" in code/cfg) | 83 | 25,000 | 1 | Atlans | **+4** |
| 81 | 황금베파르 Golden Vepar | 61 | 6,300 | 10 escort | Atlans | normal drop [?] |
| 82 | 황금탄탈로스 Golden Tantalos ("Kantur"/"Devil Tantaros" in code/cfg) | 90 | 30,000 | 1 | Tarkan | **+5** |
| 83 | 황금휠 Golden Wheel | 77 | 15,000 | 10 escort | Tarkan | normal drop [?] |

So the task's "Kantur +5" = Golden Tantalos (class 82, internally "칸투르"/Kantur 1), and
**Golden Vepar and Golden Wheel already existed pre-renewal as escorts** (and Golden Soldier),
they are not later additions; only in the 2009 renewal do they drop boxes.

Respawn timers (minutes): compiled in `Gamemain.cpp:1254-1258` = Goblin 60, Titan 60,
Derkon 240, Lizard King 120, Tantalos 120; the cfg-read defaults at `Gamemain.cpp:3693-3696`
are 3x those (180/180/720/360) when the key is absent [?] which one a live server used.
WZ0996's commonserver.cfg has no Eledorado*RegenTime keys -> server defaults applied.
Spawns were announced to all (`AllSendServerMsg` with the map name, EledoradoEvent.cpp ~line 427).

### 3.2 Golden renewal (`ADD_GOLDEN_EVENT_RENEWAL_20090311`, Season 4.5 era) [SRC]

Same files, `#else` branch (`gObjMonster.cpp:4196-4335`):

| Box | Monsters (class) | Map (renewal Regen*) |
|---|---|---|
| +1 | Golden Goblin (78), Golden Rabbit (502), Golden Dark Knight (493), **Golden Titan (53) and Golden Soldier (54)** | Noria; Rabbit Noria; Dark Knight Dungeon; Titan Devias |
| +2 | Golden Lizard King (80), Golden Vepar (81), Golden Devil (494) | Atlans; Devil Lost Tower |
| +3 | Golden Tantalos (82), Golden Wheel (83), Golden Stone Golem (495), Golden Crust (496) | Tarkan; Golem Aida; Crust Icarus |
| +4 | Golden Satyros (497), Golden Twintail (498) | Kanturu 1; Kanturu 2 |
| +5 | Golden Iron Knight (499), Golden Napin (500) | Raklion field; Swamp of Calmness |
| 5 boxes, each +1/+2/+3 at random | Golden Derkon (79) | Lorencia or Lost Tower |
| 5 boxes, each +4/+5 at random | Great Golden Dragon (501) | Kanturu 1 or Raklion field |

(Each Regen* has a `MAP_INDEX_RORENCIA` line under an #ifdef above the real map -- looks like a
test build switch [?].) Renewal respawn defaults 60 min each (`Gamemain.cpp:1261-1270`).
Note +2 is no longer the Titan's box after the renewal: the Titan was demoted to +1.

### 3.3 What players said (web) -- see section 5 below (filled after web search).

---

## 4. Kundun, Kalima, Symbol/Mark of Kundun, Lost Map

### 4.1 Lost Map is NOT from boxes, and in WebZen's server NOT from the Chaos Machine [SRC]

- **Kundun's Mark / Symbol of Kundun (14,29, 쿤둔의 표식)** drops from any monster at
  `KundunMarkDropRate` per 10,000 (WZ0996 cfg: **50 = 0.5%**; compiled default 0), its level
  set by the monster's level: <25 none(0), 25-46 +1, 47-65 +2, 66-77 +3, 78-83 +4, 84-91 +5,
  92-113 +6, +7 at 115+ (Hidden Kalima, 2005-07/2005-12 changes). `gObjMonster.cpp:5268-5360`,
  `Gamemain.cpp:3817`. Not dropped inside Kalima until the 2005-12 low-level-support update.
- **Marks stack on pickup; when a stack of the same level reaches `MAX_KALIMAGATE_STONE_OVERLAP`
  it turns by itself into a Lost Map (14,28, 잃어버린 지도) of that level** (`protocol.cpp:4842-4945`).
  The constant's history in `KalimaGate.h:25-28`: **20** (launch) -> **10** ("apple",
  2004-09-06) -> **5** ("b4nfter"). No Chaos Machine recipe for 14,28 or 14,29 exists in
  `MixSystem.cpp` (grep: none).
  https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/KalimaGate.h
  https://github.com/ptr0x-real/Mu-GS-Webzen-MC-10093/blob/master/Source/Server%20Side/GameServer/protocol.cpp
- **Dropping (throwing) the Lost Map on the ground summons a Kalima gate** ("마석", magic stone)
  of its level (`protocol.cpp:6215`, `KalimaGate.cpp CreateKalimaGate`), up to 5 entries
  (`MAX_KALIMAGATE_ENTER_COUNT 5`), lasting 180 s (`MAX_KALIMAGATE_LIFE_TICKTERM 180000`,
  earlier 300000).
- OpenMU agrees: Symbol of Kundun durability 5, stacked plugin `SymbolOfKundunStackedPlugIn`
  (`LEGACY/reference/openmu/src/GameLogic/PlugIns/`), Lost Map MaximumItemLevel 7.
  https://github.com/MUnique/OpenMU/blob/master/src/Persistence/Initialization/VersionSeasonSix/Items/Misc.cs
- So the claim "Lost Map comes from combining 5 Symbols of Kundun in the Chaos Machine" is
  **half right**: 5 Symbols of the same level do make a Lost Map (in the later rule), but by
  stacking in the inventory, not in the Chaos Machine (see 5.x for what guides say).
- Kalima itself: `ADD_NEW_MAP_KALIMA_20040518` -- WebZen's own define dates the Kalima feature
  branch to **2004-05-18**; Kundun Mark drop-level rework 2004-09-06; Hidden Kalima (Kalima 7)
  2005-07-06. The Box of Kundun predates/is independent of Kalima in code (`ELEGORADO_EVENT`,
  undated define).

---

## 1. When the Box of Kundun came in

### 1.1 WebZen Korea's own patch notice, 2003-08-05 (the launch) [SRC, archived official page]

**"[패치안내] 8월6일 전체서버 적용 패치 안내"**, posted 2003-08-05 19:56 on muonline.co.kr:
https://web.archive.org/web/20031219015407/http://www.muonline.co.kr/news/news/read.asp?ann__guid=1331&f_keyWord=&f_sekey=

- Event name: **<황금 정예부대의 역습>** "Counterattack of the Golden Elite Troops".
  "각각의 황금 몬스터에 따라 다른 쿤둔의 상자를 떨어뜨리게 됩니다. 엑설런트 아이템은
  드롭될 확률이 낮습니다." = "Each golden monster drops a different Box of Kundun (쿤둔의 상자).
  Excellent items have a low drop chance."
- Period: **2003-08-06 (after maintenance) to 2003-08-19, 14 days** -- launched as a
  time-limited event, all servers incl. test.
- The notice does NOT use the word 엘도라도 / Eldorado. "Eledorado"/"ELEGORADO_EVENT" is
  WebZen's internal code name (EledoradoEvent.cpp, commonserver.cfg "eldorado event"). The
  Korean in-code comment is `// 엘도라도의 상자(황금고블린)` "Eldorado's box (Golden Goblin)"
  (Event.cpp:555). Player-facing Korean name: **쿤둔의 상자** (Box of Kundun).
- Per monster (translated, item lists abbreviated -- they match the 0.99 files of 2.4 almost line
  for line):

| Notice section | Interval | World | Zen | Jewels | Normal items (level) | Excellent |
|---|---|---|---|---|---|---|
| 황금고블린의 역습 Golden Goblin | 1 h | Lorencia, Noria | 50,000 | Chaos, Soul, Bless | **+5 ~ +7**, option +0~+12, luck random: Leather/Pad/Vine sets, Horn/Kite shield, Buckler, Skull/Angelic/Serpent staff, Kris, Rapier, Sword of Assassin, Hand Axe, Double Axe, Mace, Morning Star, Dragon Lance, Bow, **Short Bow (단활)**, Elven Bow, Crossbow, Golden Crossbow, Arquebus | Ring of Ice/Poison, Pendant of Lightning/Fire; Leather, Pad, Vine sets; Horn, Kite shield; Kris, Rapier, Sword of Assassin, Hand Axe, Double Axe, Mace, Morning Star, Dragon Lance, Crossbow, Bow, Golden Crossbow, Skull Staff, Buckler, Angelic Staff |
| 황금 타이탄, 황금 솔져의 역습 Golden Titan & Golden Soldier | 1 h | Devias | 100,000 | + Life; also Guardian Angel, Imp (사탄) | **+4 ~ +6**: Wind, Brass, Sphinx sets; Light Saber, Legendary Sword, Axe of Nike, Larkan Axe, Great Hammer, Berdysh, Light Spear, Serpent Spear, Lightning Staff, Tiger Bow, Light Crossbow, Battle Bow, Double Blade | rings/pendants; Scale, Bone, Silk sets, **Dragon Slayer Shield, Tower Shield**; Serpent Staff, Light Saber, Blade, Double Blade, Axe of Nike, Larkan Axe, Great Hammer, Berdysh, Light Spear, Light Crossbow, Tiger Bow, Serpent Crossbow |
| 황금 데르콘의 역습 Golden Derkon | 4 h | Lorencia, Noria, Devias | 150,000 | Chaos, Soul, Bless, Life | **+4 ~ +6**: Plate, Spirit, Sphinx sets; Serpent Spear, Silver Bow, Gorgon Staff, Serpent Crossbow, **태양검 = Heliacal Sword (0,12; item(Kor).txt) -- the 0.99 file has Giant Sword (0,15) instead**, Lightning Sword | rings/pendants; Wind, Brass, Sphinx sets; Lightning Staff, Legendary Sword, Heliacal Sword (태양검), Crescent Axe, Serpent Crossbow |
| 황금 리자드킹, 황금 베파르의 역습 Golden Lizard King & Golden Vepar | 2 h | Atlans | 200,000 | Chaos, Soul, Bless, Life | **+4 ~ +5**: Dragon, Guardian, Legendary sets, Dragon Shield; Legendary Staff, Aquagold Crossbow, Bluewing Crossbow, Crystal Sword, Crystal Morning Star, Balrog's Scythe | rings/pendants; Plate, Spirit sets, **Serpent Shield**; Serpent Spear, Lightning Sword, Silver Bow, Gorgon Staff |
| 황금 탄탈로스, 황금휠의 역습 Golden Tantalos & Golden Wheel | 2 h | Tarkan | 250,000 | Chaos, Soul, Bless, Life | **+4 ~ +5**: Black Dragon set, Atlans set; Staff of Destruction, Saint Crossbow, Sword of Destruction | rings/pendants; Dragon, Guardian, Legendary sets, Dragon Shield; Legendary Staff, Aquagold & Bluewing Crossbow, Crystal Sword, Crystal Morning Star, Balrog's Scythe |

Notes on the notice vs the 0.99.60T files:
- Box tiers were never named "+1..+5" in this notice; the client names them by item level
  (MuMain: `"%ls +%d", BoxOfKundun, Level - 7`, see 4.3).
- Item-level ranges in the notice are one step wider than the 0.99 code produces
  (+1: notice +5~+7 vs code +5/+6; +2/+3: +4~+6 vs +4/+5; +4/+5: +4~+5 vs exactly +4).
  Either the 2003 server used larger `addlevel`s, or the notice counts differently. [?]
- Small list differences (Short Bow in +1; Dragon Slayer/Tower shields in +2 excellent;
  Serpent Shield in +4 excellent; Heliacal Sword in +3 where the file has Giant Sword) are absent from the 0.99.60T files. [?]
  whether removed later or never in the data.
- The notice wording says escorts (Golden Soldier, Golden Vepar, Golden Wheel) are part of
  each "counterattack"; whether escorts dropped boxes in 2003 is unclear -- WebZen's code
  only gives boxes to 78/53/79/80/82 pre-renewal. [?]

### 1.2 Follow-up notice 2003-08-11 [SRC, archived official page]

"[점검안내] 8월 12일 서버 정기점검 안내", posted 2003-08-11:
https://web.archive.org/web/20040308214641/http://www.muonline.co.kr:80/news/news/read.asp?ann__guid=1352&f_keyWord=&f_sekey=
- Event monsters now appear in the **centre** of the map instead of the edges.
- Event items now drop **at the monster's position**, not at the attacker's position.
- **Golden Derkon: every 4 h in Lorencia/Noria/Devias, changed from 1 to 2 monsters.**
  (Matches WZ0996 MonsterSetBase count 2 for class 79, and the code's 80..170 spawn box.)

### 1.3 Version context [WEB]

- MuHistory (community-compiled KR version history with archived citations) dates the Box of
  Kundun ("Added Golden Dragon Invasion (Box of Kundun) event") to the **06.08.2003** patch,
  listed as "0.95.?", just before 0.95k (26.08.2003); Box of Luck / Golden Budge Dragon to
  **0.48 (18.12.2001)**; Kalima 1-6 and Illusion of Kundun to **21.09.2004** (0.99 era);
  Kalima 7 later; Symbol of Kundun +7 from monster level 115 in Season 1.
  https://github.com/AlighieriDemiurgs/MuHistory (README, lines ~181-183, 369, 527, 594, 627)
  Its source for Box of Luck: archived official notice
  https://web.archive.org/web/20020213121629/http://www.muonline.co.kr/news/news/read.asp?ann__guid=231&f_keyWord=&f_sekey=
  [?] I have not opened the 2001 notice myself.
- So in Korea the Box of Kundun is **0.95-era (Aug 2003)**, older than Kalima (Sep 2004) and
  the Symbol of Kundun/Lost Map. It was a 14-day event first; it then became the standing
  "golden invasion" (WZ0996 2005 cfg has `IsEledoradoEvent = 1` by default) [?] exact date it
  became permanent.
- 0.97d (the classic private-server base, Korean 0.97 dated 30.10.2003 by some sources) already
  contains it, as does the 0.99.60T package. Global (muonline.com, opened 2003-2004 [?]) and
  Japanese (Gamepot) dates: NOT found. [?]

### 1.4 Box of Luck (14,11 lvl 0): WebZen notice 2001-12-17, patch 0.48 [SRC, archived official page]

"12월 18일 서버정기점검 및 0.48 패치에 관한 안내입니다." posted 2001-12-17:
https://web.archive.org/web/20020213121629/http://www.muonline.co.kr/news/news/read.asp?ann__guid=231&f_keyWord=&f_sekey=
- "겨울방학 이벤트 2 (황금버지드래곤을 잡아라!!!)" "Winter vacation event 2: Catch the Golden
  Budge Dragon": appears at unfixed places in all worlds, throughout the event; killing it
  gives a **행운의 상자 (Box of Luck)**. "Pick it up, take it out of the inventory and put it on
  the ground: at a set chance money or certain items come out."
- Contents then: +8 with skill Katana, Falchion, Blade, Salamander Sword, Light Saber, Tomahawk,
  Nikea Axe, Morning Star, Berdysh, Arquebus, Battle Bow, Light Crossbow; +8 Angelic & Serpent
  Staff; +8 Skull/Elven/Plate/Spiked/Dragon Slayer/Tower shield; +8 Bronze, Scale, Pad, Bone,
  Silk, Wind sets; Guardian Angel, Imp, Horn of Uniria; Ring of Ice/Poison, Pendant of
  Lightning/Fire "2옵" (two options [?] meaning); Jewel of Bless, Jewel of Soul.
- Event 1 of the same patch: the Red Dragon (환수드래곤) raid, 3 times a day, drops Bless/Soul.
- So: **Box of Luck = 0.48, 2001-12-18 (Korea). Box of Kundun = 2003-08-06 (Korea, 0.95 era).**

---

## 4 (cont.). Lore and in-game text

### 4.2 In-game text for the box (client) [SRC]

MuMain (Season 6 client source, local `LEGACY/reference/MuMain`):
- Name: `"%ls +%d", BoxOfKundun, Level - 7` -> "Box of Kundun +1".."+5"
  (`src/source/Engine/Object/ZzzInventory.cpp:1704, 2372, 6630`).
- Tooltip lines for any 14,11 that is not lvl 7 or 14: **"Throw it and you may receive some Zen
  or items"** (white), then **"Cannot be sold."** (red) (`ZzzInventory.cpp:4171-4196`;
  strings `src/Localization/Game.en.resx:2160, 2800`). No story text for the box itself.
  Upstream: https://github.com/sven-n/MuMain
- Symbol of Kundun tooltip: "Kundun mark +%d level", and "Can create lost map." /
  "%d is lacking to create lost map." (`Game.en.resx:4416-4429`) -- the client counts toward
  the auto-combine; no Chaos Machine mention.
- Kundun lore lines in the same client text: "Titan is a giant who guards Cathawthorm where the
  Kundun is sealed and it was created by Eturamu to protect the sealed stone." (`:6465`);
  "After the ressurection of Kundun, some monsters have taken possession of what is called the
  Box of Heaven..." (`:2717`, Golden Archer / Rena event, 0.94b 2003-06).

### 4.3 Kundun lore [WEB]

- Kundun, "the Devil of Darkness ... who had existed 1000 years ago before the MU Empire was
  born", imprisoned by the **Seal of Etramu**; Antonias touched the seal and "the 8 pieces of the
  Sealing Stone scattered far and wide to each corner of the vast continent that is MU".
  https://wiki.infinitymu.net/index.php?title=MU_Online
- Korean story (namu wiki, via search summary only -- page 403'd to me [?]): Etramu moved the
  sealing stone to "Kethotum" (= the client's "Cathawthorm"), split it into eight pieces as a
  magic circle, and Kundun, petrified, fell into Kethotum, "a lost city in space".
  https://en.namu.wiki/w/%EB%AE%A4%20%EC%98%A8%EB%9D%BC%EC%9D%B8/%EC%A7%80%EB%8F%84
- Fandom: "According to the story, each of the jewels is a fragment of the sealing stone that
  held Kundun before it shattered." [? fan claim; infinitymu's text stops short of equating them]
  https://muonline.fandom.com/wiki/MU_Online
- Kalima: "the place where the great lord Kundun has been banished to ... his most loyal
  minions"; entered with a Lost Map; Kalima 7 holds Kundun himself.
  https://muonlinefanz.com/tools/maps/data/mapdb/Kalima.php ,
  https://muonline.fandom.com/wiki/Kalima
- WZ0996 Monster.txt: Kalima 1-5 boss is 쿤둔의잔영 "Illusion of Kundun" (161/181/189/197/267),
  Kalima 6 has 쿤둔 Kundun (275, level 140, 5,000,000 HP). (https://github.com/makaytrue/0.99.60T)
- **Box <-> Kalima:** none in WebZen's data. The box is a 2003 golden-event reward named after
  the villain (the golden troops are framed as "counterattacks" of Kundun's forces); Kalima,
  the Symbol and the Lost Map came a year later (2004-09) and never appear in eventitembag8-12.
  The earlier MU2_BGFX note that "Kundun boxes are where the Lost Map comes from in 0.97+"
  (MU2_BGFX/docs/drop-boxes.md section 3) is **wrong** and should be corrected.
- Kundun Staff: not in any of the five WebZen 0.99 box lists (the MU2_BGFX doc's "+5 ...
  Kundun Staff" came from the 2010 repack). [?] its introduction version (Season 1-2 era).

### 4.4 Lost Map via Chaos Machine? -- Answer

**No.** WebZen's server auto-converts a stack of Symbols of Kundun of one level into a Lost Map
on pickup (20 -> 10 -> 5 per stack over 2004-2005); wikis agree:
"The pieces will automatically combine in your inventory to form the Lost Map"
(https://wiki.infinitymu.net/index.php?title=Kalima); "Stacking x5 will automatically create a
Lost Map" (https://muonlinefanz.com/tools/maps/data/mapdb/Kalima.php). No Chaos Machine recipe
in WebZen's MixSystem.cpp. And no Box of Kundun list contains a Symbol or a Lost Map.

---

## 5. What players / wikis said [WEB]

- Fandom (old text): Golden Budge Dragons drop Boxes of Luck, Golden Goblins Box of Kundun +1;
  Golden Titan (Devias), Golden Dragon (Lorencia/Noria/Devias), Golden Lizard King (Atlans),
  Golden Tantallos (Tarkan) drop +2, +3, +4, +5; "Golden Soldier Troop ... don't drop Boxes of
  Kundun, only items or zen" -- matches WebZen's pre-renewal code.
  https://muonline.fandom.com/wiki/Golden_Troop_Invasion
- Fandom on opening: "it must be dropped from the player's inventory onto the ground. It could
  drop zen, a good regular item (+4/5 +option and maybe +luck), an excellent item, or nothing."
  https://muonline.fandom.com/wiki/MU_Online -- matches 2.2 (the "+4/5" is the +2..+5 boxes).
- Modern Global (muonlinefanz, page dated 2025-05-08): golden monsters spawn daily 18:10 UTC
  with no system message; renewal-style mapping (Goblin/Rabbit/Titan/Dark Knight +1, Vepar/
  Devil/Wheel/Lizard King +2, Golem/Tantalos/Crust +3, Satyros +4, Napin/Twin Tail/Iron Knight
  +5, Derkon and Great Golden Dragon several boxes); boxes now give Zen and jewels (Chaos, Life,
  Creation, Harmony, Soul, Bless; +5 up to x10 Soul/Bless), "Golden monsters have 100% drop
  rates." Differs from the 2009 WebZen code in places (Wheel +2 vs code +3, Twin Tail +5 vs
  code +4, Great Golden Dragon +3/+4/+5 vs code +4/+5) -- later retunes. [?]
  https://muonlinefanz.com/guide/hunting/gold-monster/
- Private-server guides diverge widely (InfinityMU S3E1 puts Phaewang/Titan sets in BOK+5):
  https://forum.infinitymu.net/threads/box-of-kundun-drops.16/ -- not WebZen.
- RageZone threads on configuring eventitembag8-12 (403 to me, not read):
  https://forum.ragezone.com/threads/help-drop-table-for-box-1-and-5.301040/ ,
  https://forum.ragezone.com/threads/how-to-config-box-of-kundun.792901/
- Korean inven / old forum rate reports: NOT found. [?]

---

## 6. Gaps / uncertain

- Exact 2003 roll parameters (the notice's +5~+7 / +4~+6 ranges vs the 0.99 code's addlevels).
- Whether escorts (Soldier, Vepar, Wheel) dropped boxes in Aug 2003.
- When the 14-day event became permanent; Global / Japanese launch dates of the boxes.
- WebZen's own Season-4 probability files for these boxes.
- The 0.97d Korean lists (assumed equal to 0.99.60T's; the 2003 notice is near-identical).
