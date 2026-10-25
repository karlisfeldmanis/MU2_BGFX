# The quest reward ladder

Since 2026-10-09. The user: "we need very logical and make sense progression, so character can
understand all new runes and they are not given too soon or too late. we need that characters by
completing quests can make armor sets, get weapon upgrades, shields, jewels, firecrackers and
other stuff". The rows are `src/sim/quests.cpp`; `testQuestLadder` in tests/sim_test.cpp guards
the rules below. Where this disagrees with an older quest doc's reward section, this file holds.

## The rules

1. **A rune is never rarer than its map.** A first clear's rune is at most what that map's
   monsters can drop (`kRuneRarityLevel`): Common/Rare at home and in the Catacombs, Epic from
   Devias (Ice Queen 52), the Halls, the Pit and the tower's first three floors, Legendary from
   Atlans (Hydra 74) and the tower's fourth floor (Devil 60) on.
2. **Second-class runes and gear wait for Icarus** (160+), the class change's eve; the class's
   signature Legendary comes with the class change itself.
3. **A rune comes with the socket it fits**: a weapon rune with a weapon, an armour rune with the
   piece it goes in.
4. **A rune family is taught weak first**: Common at home, Epic mid-game, Legendary later.
5. **One weapon an act**, each better than the last, sockets 1, 2, 2, 3, 3, 3. **The wizard's
   are one-handed**, since Magic Shield needs a shield (the user, 2026-10-09). **The knight is
   paid both** a one-hander and a two-hander at Marlon, Devin, Lirien and Tarkan's two (the
   user, 2026-10-22: "we are not giving some 2-hand weapon rewards in quest lines for DKs which
   want to play like that", then "we dont need that choise": the Magic Gladiator's Melee or
   Magic is the one choice a hand-in asks), and the shieldless guard comes as runes: Buckler at Atlans, Twin Guard
   (two matched swords) at Tarkan, Bulwark with the Dark Breaker at the Phoenix. The Balrog pays
   the knight and the wizard jewels, not a third copy of a weapon (2026-10-21).
6. **One whole set per chain**, a piece or two a link, so the five-piece set bonus is earned.
7. **Jewellery, pets and jewels rise with the acts**; a skill orb comes at about its level. The
   Angel and the Imp last 2 hours and Uniria 24, and they stack (1298be3b), so their quests pay
   them **every clear**: 2 Angels (Devin), 2 Imps (the Pit), 1 Uniria (Tersia's first floor).
8. Every quest still pays 3 Firecrackers, the class change 10.
9. **The talents' Attunements get their runes.** Wind counts Tempest, Fire Inferno, Storm Thunder
   and Tempest, Frost and Venom Glacier and Venom. The Sky Door pays the knight and the elf a
   Tempest and the wizard a Tempest and a Venom (a choice of five read clumsy at the hand-in);
   Kantur's Legion pays the wizard Inferno and Thunder and the elf Glacier. The rest drop.
10. **A rebirth replays the experience, not the loot.** The first hand-in of a quest after each
   rebirth pays its first clear's experience again (`sim::questReborn`, `QuestProgress::life`,
   kept layout 14); gear and runes stay the first clear's.
11. **Experience is priced on the curve.** A first clear pays about one to two levels at its own
   level, a repeat a third of one; past 256 the curve climbs steeply, so Aida's are 12M and 3M.
12. **Gear is paid at +0, never upgraded.** Every weapon, piece and shield a quest pays comes
   +0 with Luck, its option (kQuestOption) and its sockets -- no +5, +7 or more (the user,
   2026-10-25: "only +0 and +luck +opt and socket"). Where no better item of a class is built,
   the step is the next item of its own line, never a plus on the last one.

## The acts

| act | quest(s) | level | weapon | armour | runes (first clear) | other |
|---|---|---|---|---|---|---|
| I | Marlon / Peia | 1-30 | DK Falchion and Spear, DW Serpent Staff, Elf Battle Bow (1 socket) | -- | Common: DK Gust + Graze, DW Faint Echo, Elf Chill | Ring of Wealth (green); Lunge / Ice / Greater Damage; 3 Bless, 20 potions, Box of Luck every clear |
| II | Devin | 30-50 | DK Double Blade and Legendary Sword, DW Legendary Staff + Legendary Shield, Elf Tiger Bow (2 sockets) | -- | Lesser Ascendance (Elf: Barrage) + class Rare stat (Might / Insight / Grace); Renewal | Pendant of Fire (1 socket); **2 Guardian Angels every clear**; Flame / Summon +3; 3 Bless, 1 Soul, Box of Luck |
| III | Golden Archer: Catacombs, Halls, Pit | 40-75 | -- | **Set A**: DK Plate, DW Sphinx, Elf Spirit. Catacombs helm+gloves, Halls pants+boots (2 sockets), Pit armour | Keen Eye, Ironskin; Bloodwell, Frenzy; Undying | Halls: DK **Serpent Shield** + Steadfast, Ring of the Leech (blue), first **Jewel of Chaos**. Pit: Pendant of Fury (purple), **2 Imps every clear**, Box of Kundun +1. Skills: Twisting Slash, Cyclone / Twister, Evil Spirit, Hellfire / Summon +4 |
| IV | Lirien (Atlans) | 70-100 | DK Lighting Sword and Crescent Axe, DW Legendary Staff (1H), Elf Aquagold Crossbow (2 sockets) | -- | **First Legendary**: DK Twister, DW Arcane Echo, Elf Frost Arrow; Epic: Eddy / Scorch / Plague Arrows; **Buckler** (DK, DW) | first **Jewel of Life**, 2 Soul, Box of Kundun +2; Aqua Beam |
| V | Tersia, floors 1-5 | 80-120 | -- | **Set B**: DK Dragon, DW Legendary, Elf Guardian. A piece a floor: helm, gloves, boots (2), pants, armour | Greater Vigor; class Greater stat + Kinship; Second Wind; Bloodwell; Renewal | LT1 **Horn of Uniria every clear**; LT2 Ring of Poison (socket); LT4 Ring of Wealth (purple), Slash; LT5 **Dragon Shield / Legendary Shield +5 (2 sockets)** (Elf: Ring of Ice), Swell Life (DK, 2026-10-25) |
| V | Tersia, floors 6-7 | 120-160 | Balrog: Elf Chaos Nature Bow (3 sockets); DK and DW 2 Bless + 2 Soul instead | -- | LT6: Meteor / Spirit Plague / Scatter Volley; Balrog: **Wrath** + Stormcall | LT6 Death Stab, Penetration; Balrog: Ring of Wisdom (legendary), Rageful Blow, Inferno, Summon +5 (Bali), Chaos, Box of Kundun +2. No Dinorant (2026-10-21: the machine makes one) |
| VI | the Keeper (Tarkan): Kantur's Legion | 100-160 | DK Sword of Destruction and Crystal Sword, DW Staff of Resurrection, Elf Saint Crossbow (3 sockets; first class only) | -- | DK Immolate + Ice + **Twin Guard**; DW Inferno + Thunder; Elf **Greater Barrage** + Glacier | Pendant of Fury (legendary); Life, Chaos, Feather, Box of Kundun +3 every clear |
| VI | the Keeper: Kantur's Captain (2nd class and MG only) | 200+ | BK Knight Blade and Daybreak, SM Staff of Kundun, ME **Stinger Bow**, MG Dark Reign Blade (3 sockets) | **The 2nd class's shield** (2 sockets): Crimson Glory, Guardian Shield, Frost Barrier, Salamander Shield | -- | Life, Feather |
| VII | Tersia, Icarus: Sky Door, Phoenix | 160-200 | Phoenix: DK Dark Breaker, DW Dragon Soul Staff, Elf Celestial Bow (3 sockets, 2nd class) | **Set C (2nd class)**: Dark Phoenix, Grand Soul, Divine. Sky Door gloves+boots, Phoenix helm+pants. **Magic Gladiator: Storm Crow** gloves, boots, pants, armour | Sky Door: Greater Ascendance, **element runes** (DK and Elf Tempest; DW Tempest + Venom); Phoenix: Evil Spirit, Bulwark (DK, with the Dark Breaker) | Sky Door: Cometfall (SM), Summon +6 (Alquamos); Feathers, Life, Chaos, Box of Kundun +3 |
| VIII | Sevina | 200 | -- | the treasure: Set C's armour | trial: Second Wind. Treasure: **Whirlwind / Pyroblaster / Piercing Volley** | DW: Grand Soul Shield; 10 Firecrackers each part |
| IX | Peia, the Seed Master (Aida) | 300+ | First clear (3 sockets, any rank): DK Flamberge and **Beuroba**, DW **Chromatic Staff**, Elf **Air Lyn Bow**; MG the spear or the staff by his path | -- | Greater Wellspring | 12M experience first, 3M a repeat; Life, Chaos, 2 Bless, 2 Soul, Box of Kundun +3 |

The Magic Gladiator walks the knight's or the wizard's path (Melee / Magic) as before; helms
and second-class things filter out for him, and runes he cannot set come as their weaker kin
(`sim::gladiatorRune`, now also Twister as Gust, Greater Ascendance as Lesser, Evil Spirit as
Wisp). At Icarus he is paid his own Storm Crow set (`QuestItem::kin` = Magic Gladiator,
`sim::questPaysHero`).

## The rune families

| family | Common (home) | Epic | Legendary |
|---|---|---|---|
| DK Twister | Gust (Marlon) | -- | Twister (Atlans) |
| DK Twisting Slash | -- | Eddy (Atlans) | Whirlwind (Broken Sword, 2nd) |
| DW Arcane Echo | Faint Echo (Marlon) | -- | Arcane Echo (Atlans) |
| DW Fire Ball | -- | Scorch (Atlans) | Pyroblaster (Soul of Wizard, 2nd) |
| Shieldless guard | -- | Buckler (Lirien) | Twin Guard (Tarkan, two matched swords), Bulwark (Phoenix, with the Dark Breaker, 2nd) |
| DK Bleed | Graze (Marlon) | -- | Lacerate, Rend, Hemorrhage drop |
| Elements (Attunement) | -- | -- | Rare: Inferno, Thunder (DW, Tarkan), Glacier (Elf, Tarkan); Tempest (all, Sky Door), Venom (DW, Sky Door) |
| Elf Frost Arrow | Chill (Peia) | -- | Frost Arrow (Atlans) |
| Elf Multi-Shot | -- | Barrage (Devin), Plague Arrows (Atlans) | Greater Barrage (Tarkan) |
| Elf Penetration | -- | Scatter Volley (LT6) | Piercing Volley (Tear of Elf, 2nd) |
| Stats | Rare: class stat (Devin) | Lesser Ascendance (Devin), Greater stat (LT1-2) | Greater Ascendance (Sky Door, 2nd) |
| Damage | -- | Stormcall (Balrog) | Wrath (Balrog) |

## What moved, and why

- **The reward audit, 2026-10-22** (the user: "audit all quest rewards, based on talent system,
  rune system and other progression factors", then "do fixes"): the Attunements' element runes
  (no quest paid a Tempest or a Venom), Buckler (named in the ladder, paid nowhere) and Twin
  Guard (the Dual Wield column's rune), Graze, the four second-class shields and the Stinger Bow
  at Kantur's Captain, the timed pets every clear, Aida's experience on the curve (1.2M first was
  13% of a level at 300), and a rebirth's experience again.

- **Marlon/Peia**: Stormcall (Epic) and Arcane Echo (Legendary) at level ~28 became Commons.
- **Devin**: the top 0.75 armour (a Lost Tower set piece two maps early) became the second
  weapon; the Cyclone orb (level 72) went to the Pit.
- **Dungeon**: helm, pants and boots of three different sets became one set; the Double Blade
  went to Devias; Slash (104) to LT4 and Death Stab (120) to LT6.
- **Lost Tower**: duplicate Double Blade, Meteor, Bloodwell, Undying and the socket pendant are
  gone; one set over floors 1-5 and one 3-socket weapon at the Balrog.
- **Tarkan**: the second class's armour, staff, crossbow and Legendaries (100 levels before the
  class change) became the first class's best weapon and runes; those moved to Icarus and Sevina.
- **Icarus**: the duplicate Aquagold Crossbow and the second Immolate/Scorch/Plague Arrows became
  the second class's set and weapon.
