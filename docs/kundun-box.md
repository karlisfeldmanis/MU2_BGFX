# The Box of Kundun, deep (2026-10-02)

The user: "lets dig deep on Box of Kundun because our game is around that lore". Research only;
nothing built. The broad survey of every drop-to-open box is drop-boxes.md; this file is the
Kundun box and everything around it that carries Kundun's name.

Sources, highest first: WebZen GameServer 1.00.93 (`WZ`, under `Source/Server Side/GameServer/`,
0.97d base unless a flag is named), MuMain (`MM`, `src/source/`), OpenMU (`OM`). The repo's
`_Server Files/Data` is a 2010 repack and is not used here. WebZen's own data is the official
0.99.60T server package (`WZD`, github.com/makaytrue/0.99.60T, files of 2004-2005 with Korean
comments): its `eventitembag8..12.txt`, `commonserver.cfg`, `Monster.txt`, `MonsterSetBase.txt`.
§8 is the history and the web's side.

## 1. What it is

**Item 14,11 at levels 8-12**, named "Box of Kundun +1" .. "+5" (`MM ZzzInventory.cpp:1704`,
level - 7). The same item at level 0 is the Box of Luck. It is dropped by a golden monster,
always and only by it, one box per kill, owned by whoever did the most damage
(`WZ gObjMonster.cpp:4107-4170`, `gObjMonsterTopHitDamageUser`). The player opens it by
dropping it on the ground (`WZ protocol.cpp` `CGItemDropRequest`, the `14,11` branch).

The code calls it the **Eldorado box** (`EledoradoBoxOpenEven`, "엘도라도의 상자"); that is
WebZen's internal name only. Players always saw **쿤둔의 상자, Box of Kundun**, from the first
notice (§8). The bag files name the golden troops **Kundun's underground legion**: bag 9 is
"지하군단 습격" (the underground legion's raid), bag 12 "칸투르의 지하군단" (Kantur's underground
legion); the launch event was "황금 정예부대의 역습", the Counterattack of the Golden Elite Troops.
So in MU's own words the golden monsters are Kundun's elite, striking back, and the box is theirs.

## 2. Who drops which box, and where (0.97d, before the 2009 renewal)

`WZ EledoradoEvent.cpp`, regen times from `Gamemain.cpp:1254-1258, 3693-3697`. Each golden boss
is a single monster placed in `MonsterSetBase` and moved by the event to a random free tile.

| Box | Golden monster (class) | Lvl / HP (WZD) | Map | Escort (WZD count) | Every |
|---|---|---|---|---|---|
| +1 | Golden Goblin (78) | 20 / 3,100 | **Lorencia or Noria**, random, tiles 50-200 | none | 1 h |
| +2 | Golden Titan (53) | 53 / 8,000 | Devias | 10 Golden Soldiers (54; 46 / 3,500) within 4 tiles | 1 h |
| +3 | Golden Derkon (79), "Golden Dragon" in the client | 80 / 23,000 | Lorencia, Noria or Devias, tiles 80-170; **two**, both on one map | none | 4 h |
| +4 | Golden Lizard King (80) | 83 / 25,000 | Atlans | 10 Golden Vepars (81; 61 / 6,300) | 2 h |
| +5 | Golden Tantalos (82, "Kantur" in the code) | 90 / 30,000 | Tarkan | 10 Golden Wheels (83; 77 / 15,000) | 2 h |

Intervals are the compiled defaults (`Gamemain.cpp:1254-1258`) and the launch notice's; WZD's cfg
sets none. Each spawn is shouted to the server with the map's name.

The Golden Budge Dragon (43) is the same invasion's low end and drops the **Box of Luck** (level 0),
not a Kundun box. Golden Soldiers, Vepars and Wheels drop nothing special.

**Their look** (`MM ZzzCharacter.cpp:8716-8785`): the ordinary monster's model drawn three times,
plain, then `RENDER_METAL | RENDER_BRIGHT`, then `RENDER_CHROME | RENDER_BRIGHT` -- the Balrog's
gilding. The Goblin carries a +9 axe; the Lizard King an excellent Chaos Lightning Staff; Tantalos
an excellent Sword of Destruction with two energy joints trailing; the Wheel an excellent Aquagold
Crossbow. The Titan and its soldiers arrive with an "appear" fade (`WSclient.cpp:2779`).

**The 2009 renewal** (`ADD_GOLDEN_EVENT_RENEWAL_20090311`, Season 4) moved the Goblin to Noria only,
the Derkon to Lorencia or Lost Tower and re-tiered the boxes by monster: +1 from the Goblin, Golden
Rabbit and Golden Dark Knight; +2 Titan and Soldier; +3 Lizard King, Vepar and Golden Devil (Lost
Tower); +4 Tantalos, Wheel, Golden Stone Golem, Crust; +5 Satyros, Twintail; Iron Knight and Napin
+5 as well; the Golden Derkon drops **five** boxes of +1..+3 and the Great Golden Dragon five of
+4/+5, scattered around the corpse (`gObjMonster.cpp:4183-4335`).

## 3. Opening one

`WZ Event.cpp:603 EledoradoBoxOpenEven(lpObj, boxLevel, addlevel, zen)`, called with:

| Box | Bag | addlevel | Zen if empty |
|---|---|---|---|
| +1 | eventitembag8 | 2 | 50,000 |
| +2 | eventitembag9 | 2 | 100,000 |
| +3 | eventitembag10 | 2 | 150,000 |
| +4 | eventitembag11 | 1 | 200,000 |
| +5 | eventitembag12 | 0 | 250,000 |

The roll:

1. `rand()%100 < ItemDropRate` (`EledoradoXxxItemDropRate`, the table below) or Zen.
2. The bag is two pools, split by the fifth column of each row (`ItemBag.cpp LoadItem`:
   `group index level skill exc option`, `exc` set -> excellent pool).
3. `rand()%100 < ExItemDropRate` and the excellent pool has rows -> an
   **excellent** item: level 0, skill on, luck 50%, option (20% +12, else +0/+4/+8 when skill or
   luck is missing), excellent options from `NewOptionRand(0)`.
4. Otherwise a **plain** row: level = row's level + `rand()%addlevel` (so +1..+3 boxes can add one
   level, +5 none), skill 50%, luck 50%, option as above.
5. Jewels come bare; rings and pendants at level 0. Zen otherwise, at the feet.

WebZen's shipped rates (WZD `commonserver.cfg`; the code's fallback is 10/10 for all five):

| Box | Item | of which excellent | Excellent overall | Plain item level | Zen otherwise |
|---|---|---|---|---|---|
| +1 | 30% | 5% | 1.5% | +5/+6 | 50,000 |
| +2 | 25% | 4% | 1.0% | +4/+5 | 100,000 |
| +3 | 20% | 3% | 0.6% | +4/+5 | 150,000 |
| +4 | 15% | 2% | 0.3% | +4 | 200,000 |
| +5 | 10% | 1% | 0.1% | +4 | 250,000 |

The better the box, the rarer the item and the more Zen: a +5 box is a quarter million Zen nine
times in ten, and its prize is rare. The launch notice gave each range one level wider (+5~+7,
+4~+6, +4~+5), so the 2003 server may have used larger `addlevel`s.

**The lists** (WZD). Each box's excellent pool is the previous box's plain tier; rings of Ice and
Poison and pendants of Lightning and Fire are excellent in every box; none holds a Mark of Kundun
or a Lost Map.

| Box | Plain pool | Excellent pool |
|---|---|---|
| +1 | Chaos, Soul, Bless; Guardian Angel, Imp, Horn of Uniria (+5); Leather, Pad, Vine sets; Horn and Kite Shield, Buckler; Skull, Angelic, Serpent Staff; Kris, Rapier, Sword of Assassin, Hand and Double Axe, Mace, Morning Star, Dragon Lance; Bow, Elven Bow, Crossbow, Golden Crossbow, Arquebus | Leather, Pad, Vine sets; the same shields and starter weapons, Short Sword |
| +2 | Chaos, Soul, Bless, Life; Guardian Angel, Imp; Wind, Brass, Sphinx sets; Light Saber, Legendary Sword, Double Blade, Axe of Nike, Larkan Axe, Great Hammer, Berdysh, Light Spear, Serpent Spear, Lightning Staff, Tiger Bow, Battle Bow, Light Crossbow | Scale, Bone, Silk sets; Serpent Staff, Light Saber, Blade, Double Blade, Nike, Larkan, Great Hammer, Berdysh, Light Spear, Light Crossbow, Tiger Bow, Serpent Crossbow |
| +3 | the four jewels; Plate, Spirit, Sphinx sets; Legendary Sword, Serpent Spear, Silver Bow, Gorgon Staff, Serpent Crossbow, Giant Sword, Lightning Sword | Wind, Brass, Sphinx sets; Lightning Staff, Legendary Sword, Giant Sword, Crescent Axe, Serpent Crossbow |
| +4 | the four jewels; Dragon, Guardian, Legendary sets, Dragon Shield; Legendary Staff, Aquagold and Bluewing Crossbow, Crystal Sword, Crystal Morning Star, **Balrog's Scythe** | Plate, Spirit sets; Serpent Spear, Lightning Sword, Silver Bow, Gorgon Staff |
| +5 | the four jewels; Black Dragon, Atlans sets; Staff of Destruction, Saint Crossbow, Sword of Destruction | Dragon, Guardian, Legendary sets, Dragon Shield; Legendary Staff, Aquagold and Bluewing Crossbow, Crystal Sword, Crystal Morning Star, Balrog's Scythe |

(drop-boxes.md's earlier lists, with Kundun Staff in +5, were the 2010 repack.)

No fanfare or firework on a Kundun box (the medals and hearts have them). On the ground the box
glows at item level `(L-8)*2+1`: +1 glows as +1, +2 as +3, +3 as +5, +4 as +7, +5 as +9
(`MM ZzzObject.cpp:9648`), at scale 0.2 (`ZzzObject.cpp:5723`), model `Data/Item/MagicBox08.bmd`
(`ZzzOpenData.cpp:979`).

**The renewal's bag** (`MODIFY_KUNDUN_EVENTBAG_RENEWAL_2009_02_04`): `CProbabilityItemBag`,
`DropEventItem` -- per-row weights instead of the flat 10/10, same five files.

## 4. Everything else with Kundun's name on it

These make one chain in MU, and the box is not on it:

1. **Symbol / Mark of Kundun (14,29).** Drops from any kill at `KundunMarkDropRate`/10,000,
   50 in WZD's cfg, so 0.5% (`WZ gObjMonster.cpp:5269-5330`), its level from the monster's: below 25 none (+0), 25-46 +1,
   47-65 +2, 66-77 +3, 78-83 +4, 84-91 +5, 92-113 +6, 115+ +7. Not inside Kalima.
   It stacks; the tooltip counts "n / 5" and "n more needed to make a Lost Map"
   (`MM GMHellas.cpp:250`).
2. **Lost Map (14,28).** Five marks of one level stacked turn into a Lost Map of that level
   (`WZ user.cpp:17747`, `MAX_KALIMAGATE_STONE_OVERLAP 5`; it was 20, then 10).
3. **Throw the map.** Dropping a Lost Map on the ground raises a **Kalima gate** (a "magic stone")
   at that tile (`WZ protocol.cpp:6216 CreateKalimaGate`), one per player at a time; walking in
   takes the party to Kalima N. It is the same throw-on-the-ground trick as the box.
   Entry levels (`MM GMHellas.cpp:38`): Kalima 1-7 at 40, 131, 181, 231, 281, 331, 350
   (Magic Gladiator and Dark Lord 20, 111, 161, 211, 261, 311, 350).
4. **Kundun (275)**, at the end of Kalima 7. On death, five drops scattered round him: each an
   ancient set piece at the ancient rate, else a row of `eventitembag17` (the "Kundun" bag,
   `CItemBagEx::DropKundunEventItem`, `WZ ItemBagEx.cpp`), always an item
   (`gObjMonster.cpp:3895-3960`).
5. **Staff of Kundun (5,11)** in the +5 bag; the Red Dragon invasion's dragon is named "쿤둔"
   (Kundun) in MuMain's monster setup (`ZzzCharacter.cpp:13831`).

So in MU itself the Box of Kundun is Kundun's name on the golden invasion's loot, and Kalima is
reached by the marks, not the box.

## 5. Our lore already has Kundun at the centre

- Marlon (`quests.cpp:36-72`): Kundun bound in Kalima, the knights of Lorencia sworn to hold the
  land while the seal holds, the seal cracking, "every night more of his brood climbs out of the
  Dungeon".
- Peia (`quests.cpp:123-156`): Noria's forest sick since the seal began to crack.
- Devin (`quests.cpp:216`): Kundun's Assassins in Devias.
- The Dungeon (golden-archer.md): the Knights' Halls built "when Kundun fell, to hold what he
  left beneath Lorencia".
- Lost Tower (`quests.cpp:702`, lost-tower-quest.md): Kundun plundered the shrine and left his
  Balrog on the last floor.
- Blood Castle: MU's own "Kundun minions".

Our worlds are Lorencia, Noria, Devias, Dungeon 1-3, Lost Tower 1-7, Blood Castle (in progress).
No Atlans, no Tarkan, no Kalima.

## 6. How MU's pieces map onto ours

| MU | In our worlds | Gap |
|---|---|---|
| +1 Golden Goblin | Lorencia / Noria, as 0.97d had it | none: Goblin01 is cooked (Noria); so is BudgeDragon01 for the Box of Luck's dragon |
| +2 Golden Titan + Soldiers | Devias | Titan and Soldier models not cooked |
| +3 Golden Derkon | Lorencia / Noria / Devias | Derkon (Dragon_) model not cooked |
| +4 Lizard King + Vepars | Atlans -- we have none | re-home (Lost Tower? Dungeon 3?) |
| +5 Tantalos + Wheels | Tarkan -- we have none | re-home (Lost Tower 7? Blood Castle?) |
| Gilding | metal + chrome passes | we already draw a chrome pass (+7 items, the iced body, f1bc67be) |
| Marks, Lost Map, Kalima gate | -- | Kalima not ported; the marks would have nowhere to go |

## 7. Directions (for the user to pick)

**A. MU as it was.** The Eldorado event: five golden bosses on timers, each drops its box,
10%/1%/Zen. Faithful (0.97d) but needs three new monster models, two re-homed bosses, and
mostly hands out Zen.

**B. The box as the seal's loot.** Ours, on MU's bones: the golden monsters are Kundun's brood
gilded by the cracking seal (or carrying what he plundered from the Lost Tower shrine), so the
box is literally his hoard. Same five tiers mapped onto our worlds by monster level: +1 Lorencia,
+2 Noria/Dungeon, +3 Devias, +4 Lost Tower, +5 Lost Tower 7 / Blood Castle. Rates tuned to our
100x exp; the +4/+5 bags could hold the Rune of Creation and Jewel of Creation.

**C. Box + marks.** B, plus MU's mark chain kept for later: marks drop now and stack to Lost Maps,
and a Lost Map thrown on the ground opens a gate the day Kalima is ported -- Marlon's "bound in
Kalima" becomes a place the player can reach.

Open questions under any of them: rates (WebZen's 1.5% down to 0.1% excellent vs ours), bag contents (our tables,
tiered by drop level), whether a box shows a show on opening (MU's does not), where Zen goes
(our kills purse it; a box's Zen would too), and level gates.

## 8. History and the web's side

- **Box of Luck**: patch 0.48, 2001-12-18, the Golden Budge Dragon winter event (archived
  muonline.co.kr notice, via github.com/AlighieriDemiurgs/MuHistory).
- **Box of Kundun**: 2003-08-06, Korea, 0.95 era. WebZen's notice of 2003-08-05
  (web.archive.org/web/20031219015407/http://www.muonline.co.kr/news/news/read.asp?ann__guid=1331)
  announced "황금 정예부대의 역습", fourteen days, "each golden monster drops a different Box of
  Kundun; excellent items have a low drop chance", with per-monster lists almost line for line
  WZD's. A notice of 2003-08-11 moved the golden monsters from the map's edges to its centre,
  dropped the loot at the monster rather than the attacker, and made the Derkon two. It became the
  standing invasion later (date not found); 0.97d and 0.99 carry it.
- **Kalima, the marks and the Lost Map** came a year later, 2004-09-21 (Kalima 1-6, the
  Illusion of Kundun), so the box predates them and never touches them. The marks auto-combine
  in the inventory; WebZen's Chaos Machine has no Lost Map recipe.
- **The box's own text** is only "Throw it and you may receive some Zen or items" and "Cannot be
  sold"; no story.
- **Kundun in MU's story**: the Devil of Darkness from before the MU Empire, imprisoned by the
  Seal of Etramu; Antonias touched the seal and its eight pieces scattered across the continent
  (wiki.infinitymu.net); Kalima is where Kundun and his most loyal were banished (muonlinefanz).
  Fandom's claim that the jewels are pieces of the sealing stone is a fan reading.
- **Players then**: "it must be dropped onto the ground. It could drop zen, a good regular item
  (+4/5 +option and maybe +luck), an excellent item, or nothing"; the escorts "don't drop Boxes of
  Kundun, only items or zen" (muonline.fandom.com). Today's Global boxes give Zen and jewels only.
- Not found: Global or Japanese dates, WebZen's Season 4 probability files, Korean player odds.

Full notes with a URL per claim: kundun-box-sources.md.
