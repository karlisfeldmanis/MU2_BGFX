# Drop boxes: Box of Kundun and the other things you throw on the ground (2026-10-02)

Research only, nothing built. Asked: "make a research of kundun box and other things which
player drops and something comes from that".

Sources, highest first: WebZen GameServer 1.00.93 (`WZ`, paths under
`Source/Server Side/GameServer/`), MuMain for the client side (`MM`, `src/source/`), OpenMU
Version095d / VersionSeasonSix (`OM`). The repo's `_Server Files/Data` bags and
`commonserver.cfg` are a 2010 private repack (Nemesis / Mirage Continent, Portuguese comments);
they show the shape of a bag, not WebZen's own lists or rates.

## 1. The mechanic, in one paragraph

A box is an ordinary inventory item. The player **drops it on the ground** like any other
item, and the server, instead of laying it down, deletes it and rolls a bag
(`WZ protocol.cpp:5284 CGItemDropRequest`, the `type == 14,11` branch from ~5600). The roll
either lays **one item** at the player's feet, owned by the player, or, if the roll misses,
**Zen**. Some boxes also send a show to everyone near: `0xF3 0x40` with `CmdType 0` is a
**firework** at the tile (`MM WSclient.cpp:8145`: `CreateEffect(BITMAP_FIRECRACKER0001)`),
`CmdType 2` is the **fanfare** sound (`SOUND_MEDAL`). There is no open button, no window, no
animation of the box itself: drop, poof, loot.

## 2. One item, many boxes: Box of Luck (14,11) by level

Almost every classic box is the same item, **14,11 "Box of Luck"**, told apart by its level.
The client renames and remodels it per level (`MM ZzzInventory.cpp:1687`, `ZzzObject.cpp:6058`,
models `ZzzOpenData.cpp:906, 967-981`, all `Data/Item/MagicBoxNN.bmd`, present in MuMain's
`bin/Data/Item`).

| Level | Name | Model | Comes from (WZ) | Opens into (WZ) | Show |
|---|---|---|---|---|---|
| 0 | **Box of Luck** | MagicBox01 | Golden Budge Dragon (43), always (`gObjMonster.cpp:4107`) | 20%: one item from `eventitembag.txt`, bag level +0/+1, skill 50%, luck 50%, option always (+4/+8, 20% +12). Else 3-6 piles of 1,000 Zen around the feet (`Event.cpp:2311`) | none |
| 1 | Star of Sacred Birth | MagicBox02 | Christmas event: monster level >= 17 at 50% (`StarOfXMasDropRate` 5000/10000) | 2 in 10: item from bag 4, else Zen | firework |
| 2 | Firecracker | MagicBox03 | Firecracker event: monster level >= 17, maps 0-6 (Korea) or partied kills | 2 in 10: bag 5, level +0..+4 on bag level 3 (Korea +0..+1), else Zen | firework |
| 3 | Heart of Love | MagicBox05 | Heart event: monster level >= 17, maps 0-6 | 2 in 20: bag 5, same levels, else 1,004 Zen | firework |
| 5 | Silver Medal | MagicBox06 | Medal event: Dungeon or Devias (maps 1, 2) | 2 in 20: bag 7 (Pad/Leather/Vine, Kris...), level +3..+6, else 100,000 Zen (2,005 after 2005-08) | fanfare |
| 6 | Gold Medal | MagicBox07 | Medal event: Lost Tower, Atlans, Tarkan (4, 7, 8) | 2 in 20: bag 6 (Bone/Bronze/Silk, Blade, Gladius...), level +3..+6, else Zen as silver | fanfare |
| 7 | Box of Heaven ("Box of Gold" in WZ) | MagicBox01 | Golden Archer event-chip event, any kill at its rate | 50%: a **Rena** (14,21) for the Golden Archer; else 2 in 20 a Box-of-Luck item +4/+5; else 1,000 Zen (`Event.cpp:794`) | fanfare |
| 8 | **Box of Kundun +1** | MagicBox08 | Golden Goblin (78) | see §3; else 50,000 Zen | none |
| 9 | **Box of Kundun +2** | MagicBox08 | Golden Titan (53) | §3; else 100,000 Zen | none |
| 10 | **Box of Kundun +3** | MagicBox08 | Golden Derkon (79) | §3; else 150,000 Zen | none |
| 11 | **Box of Kundun +4** | MagicBox08 | Devil Lizard King (80) | §3; else 200,000 Zen | none |
| 12 | **Box of Kundun +5** | MagicBox08 | Kantur (82) | §3; else 250,000 Zen | none |
| 13 | Heart of Dark Lord | MagicBox05 | Dark Lord event | real-world prize lottery, offline | |
| 14/15 | Blue / Red Lucky Pouch | MagicBox03 | China/Japan only | | |

The rates "2 in 20" etc. are WZ's compiled defaults (`Gamemain.cpp:1063-1093, 3698-3797`),
each overridable in `commonserver.cfg`; the event-drop chances are out of 10,000.

## 3. The Box of Kundun roll

`WZ Event.cpp:603 EledoradoBoxOpenEven(lpObj, level, addlevel, zen)`, the pre-2009 body
(`MODIFY_KUNDUN_EVENTBAG_RENEWAL_2009_02_04` swapped it for a probability table in Season 4).
Called from `protocol.cpp` with `addlevel` 2, 2, 2, 1, 0 and Zen 50k..250k for +1..+5.

1. `rand()%100 < ItemDropRate` (default **10**, per box) -- else straight to Zen.
2. Bag `eventitembag8..12.txt` (+1..+5). Each row is `group index level skill excellentFlag option`;
   the fifth column splits the bag in two pools (`ItemBag.cpp LoadItem`: `m_op2` set -> Ex pool).
3. `rand()%100 < ExItemDropRate` (default **10**) and the Ex pool is not empty -> an
   **excellent** item: level 0, skill always, luck 50%, option as below, excellent options from
   `NewOptionRand(0)` (the normal excellent roll).
4. Otherwise a **normal** item from the plain pool: level = row level + `rand()%addlevel`,
   skill 50%, luck 50%; if it lacks skill or luck, option 20% +12, else +0/+4/+8.
5. Jewels (Chaos, Bless, Soul, Life) come with no level or options; rings and pendants at level 0.
6. Nothing picked -> the box's Zen at the feet.

At the code's fallback a Box of Kundun is 1 in 10 an item, 1 in 100 an excellent; WebZen's
shipped config tiered it, 30%/1.5% excellent for +1 down to 10%/0.1% for +5, and WebZen's own lists
run from Leather/Pad/Vine (+1) to Black Dragon/Atlans and the Swords of Destruction (+5). Both are
in kundun-box.md §3; the repack lists first quoted here (Season 2 sets, Kundun Staff) are not WebZen's.

On the ground the client shows a Kundun box at glow level `(L-8)*2+1` -- +1 glows like a +1
item, +5 like a +9 (`MM ZzzObject.cpp:9648`) -- scaled to 0.2 (`ZzzObject.cpp:5723`), named
"Box of Kundun +N" (`ZzzInventory.cpp:6628`).

**Kalima tie-in:** not through the boxes. The Lost Map (Kalima's ticket) is five Marks of
Kundun (14,29) stacked, and the marks drop from ordinary monsters; see kundun-box.md §4.

## 4. Other things you throw on the ground

Same pattern, different item numbers, all later than 0.97d unless said (`protocol.cpp` ~5810-6120):

| Item | Opens into |
|---|---|
| 12,26 Hidden Treasure Box (2005) | lvl 0 -> a crystal: black 10%, red 35%, blue 55%; lvl 1-3 -> a firework only; lvl 4 -> a bag; lvl 5 -> prize lottery or 1,000,000 Zen |
| 12,32-34 Red / Green / Blue Ribbon Box (Christmas 2005) | each its own bag |
| 14,32-34 Pink / Red / Blue Chocolate Box (lvl 0), Candy Box (lvl 1) | Valentine's / White Day bags |
| 14,45 Pumpkin of Luck (Halloween 2006) | a bag |
| 14,51 Christmas Star, 14,63 / 14,99 Firecracker | a bag and a firework |
| 14,84 Cherry Blossom Box (2008) | a bag |
| 13,20 lvl 1 Ring of Heroes / lvl 2 Ring of Warrior | thrown only at character level **40 / 80** or more, else refused with a notice; then a bag |

## 5. What each version had

- **0.75 (OM Version075): none of it.** No 14,11, no golden monsters, no boxes.
- **0.95d (OM Version095d):** Box of Luck from the Golden Budge Dragon (OM opens it at 50% into
  a +6 item from a bag much like WZ's, else 10,000 Zen), and Box of Kundun +2 from the Golden
  Titan. MU2/docs/golden-invasion.md has the invasion that brings them.
- **0.97d (WZ base):** the full table of §2: the five Kundun boxes off the five golden monsters
  of the Eldorado event, medals, hearts, firecrackers and stars on their events.
- **Season 4+ (2009):** Kundun bags turned into probability tables, and the golden roster grew
  (golden rabbit, dark knight, vepar, tantalos, wheel...; Golden Derkon drops five boxes of
  +1..+3, the Great Golden Dragon five of +4/+5, `gObjMonster.cpp:4294-4335`).

## 6. In MU2_BGFX today

Nothing: no 14,11 in `source/items`, no golden monsters, no invasion, no drop-to-open path in
the sim. What exists to build on: drops are already the realm's (`Realm::leave`,
`realm_items.cpp:853`), excellent options roll there, Zen never lies on the ground in ours (a
kill puts it in the purse, `play.cpp:242`), so a box's Zen would go to the purse too; the firework and fanfare are a MuMain effect and a sound, and
the eight MagicBox models are in MuMain's `Data/Item`.

A port would be, smallest first:

1. **The item and the throw.** 14,11 with its per-level names and models; dropping it from the
   inventory runs the box instead of laying it down (one branch where the realm drops an item).
2. **The rolls** of §2-3 as one table, bags as data (a JSON per box beside the drop tables).
3. **The show**: firework at the tile for 1-3, fanfare for 5-7, Kundun boxes glowing by tier.
4. **A source.** Without one the boxes never appear. Options: the Golden Budge Dragon invasion
   of Lorencia (0.95d, the in-period one, golden-invasion.md), the five Eldorado golden monsters
   (0.97d), or ours -- e.g. a rare drop off our own bosses or quest rewards.

## 7. Decisions open

1. **Which boxes at all.** Box of Luck + Kundun +1..+5 is the classic pair; the seasonal ones
   (star, firecracker, heart, medals) need an event switch we do not have.
2. **Where they come from** in a Lorencia-scoped single player game (§6.4).
3. **Rates.** WZ's 10% item / 1% excellent makes most Kundun boxes a Zen pile; OM S6 gives 20%
   excellent. With our 100x exp (exp-rate-is-100x) the old rates may feel stingy.
4. **Bag contents.** WebZen's own 0.97d lists are not in the repo; the repack's +4/+5 hold
   Season 2 sets. Ours would be built from our item tables, tiered by drop level, and could
   add our own pieces (a Jewel of Creation or a Rune row from the +4/+5 boxes?).
5. **Level gate**, as the rings have (40/80), or none.

## 8. The Firecracker, close up (2026-10-02)

The user likes it, from dungeons only. WebZen's own data (0.99.60T package, see kundun-box.md):

- **Drops** (`WZ gObjMonster.cpp` ~5040): only while `FireCrackerEvent` is on; from monsters of
  level 17 or more; in Korea on maps 0-6 (Lorencia, Dungeon, Devias, Noria, Lost Tower, Exile,
  Arena), abroad only from kills made in a party. Rate: 10 / 10,000 (0.1%) in WebZen's cfg; the
  code's fallback is 5,000 (50%).
- **Opens** (`WZ Event.cpp:1201`): 2 in 10 an item from `eventitembag5` (shared with the Heart of
  Love) with a firework at the tile; else **2,004 Zen** (the year). An "effect only" switch made it
  a pure firework.
- **The item**: every row is base level 5, plus `rand()%5` outside Korea -> **+5 to +9** (Korea +5/+6);
  skill 50%, luck 50%, option (20% +12). Jewels of Bless, Soul and Chaos come bare.
- **WebZen's list**: Rapier, Sword of Assassin, Katana, Light Saber, Legendary Sword, Double Blade;
  Hand, Double and Crescent Axe, Axe of Nike, Larkan Axe; Mace, Morning Star, Great Hammer;
  Berdysh, Serpent Spear; Crossbow, Golden, Light and Serpent Crossbow, Tiger Bow; Skull and
  Lightning Staff; Buckler, Horn, Kite, Dragon Slayer and Tower Shield; Leather, Pad, Bronze, Wind,
  Spirit and Sphinx sets; Bless, Soul, Chaos. (The file's own header still says "Christmas star".)
- OpenMU's Season 6 reading is richer: 20% jewels, 30% an item +7 to +9, else 2,004 Zen.

Dungeon-only would be ours: MU let any level-17+ monster on the old maps drop one.

**Built 2026-10-02, the opening only** (the user: "first develop the mechanics, opening
mechanics, sound"). The Firecracker is row 14, 11 (source/items/misc/MagicBox03.json, MU's
MagicBox03.bmd and its firecracker.jpg, cloth). Dragged out of the bag onto the ground it opens
(Realm::crack, sim/items.h kFirecrackerBag): WebZen's rolls exactly, all 61 of bag 5's rows are
items here; Zen goes to the purse with the coins' sound. An item brings MU's firework
(game/fx/firework.h): five rockets, each bursting with sparks, glitter, a shock ring, the seven
star frames, eExplosion.wav and Christmas_Fireworks01.wav. sim_test testFirecracker: 19.9% items
over 20,000 throws, +5 to +9 each seen, luck and option at WebZen's odds. `--give MagicBox03:5`.
**Not built yet:** where it drops -- the Dungeon, the Lost Tower, Blood Castle and, later, Devil
Square only (the user's), at a rate still to choose.
