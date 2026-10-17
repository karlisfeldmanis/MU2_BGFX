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
5. **One weapon an act**, each better than the last, sockets 1, 2, 2, 3, 3, 3. **The knight's
   and the wizard's are one-handed**, since Defense and Magic Shield need a shield (the user,
   2026-10-09). Past Atlans no better one-hander exists, so the Balrog pays the top one at +0
   (wizard +4) and Tarkan the same at +7. The only two-hander is the Blade Knight's Dark Breaker,
   paid together with Bulwark, the rune that keeps his guard without a shield ("only point to
   give good two-handers when chars has rune for using defense auras without shields").
6. **One whole set per chain**, a piece or two a link, so the five-piece set bonus is earned.
7. **Jewellery, pets and jewels rise with the acts**; a skill orb comes at about its level.
8. Every quest still pays 3 Firecrackers, the class change 10.

## The acts

| act | quest(s) | level | weapon | armour | runes (first clear) | other |
|---|---|---|---|---|---|---|
| I | Marlon / Peia | 1-30 | DK Falchion, DW Serpent Staff, Elf Battle Bow (1 socket) | -- | Common: DK Gust, DW Faint Echo, Elf Chill | Ring of Wealth (green); Lunge / Ice / Greater Damage; 3 Bless, 20 potions, Box of Luck every clear |
| II | Devin | 30-50 | DK Double Blade, DW Legendary Staff + Legendary Shield, Elf Tiger Bow (2 sockets) | -- | Lesser Ascendance (Elf: Barrage) + class Rare stat (Might / Insight / Grace); Renewal | Pendant of Fire (1 socket); **Guardian Angel**; Flame / Summon +3; 3 Bless, 1 Soul, Box of Luck |
| III | Golden Archer: Catacombs, Halls, Pit | 40-75 | -- | **Set A**: DK Plate, DW Sphinx, Elf Spirit. Catacombs helm+gloves, Halls pants+boots (2 sockets), Pit armour | Keen Eye, Ironskin; Bloodwell, Frenzy; Undying | Halls: DK **Serpent Shield** + Steadfast, Ring of the Leech (blue), first **Jewel of Chaos**. Pit: Pendant of Fury (purple), **Imp**, Box of Kundun +1. Skills: Twisting Slash, Cyclone / Twister, Evil Spirit, Hellfire / Summon +4 |
| IV | Lirien (Atlans) | 70-100 | DK Lighting Sword, DW Staff of Resurrection (1H), Elf Aquagold Crossbow (2 sockets) | -- | **First Legendary**: DK Twister, DW Arcane Echo, Elf Frost Arrow; Epic: Eddy / Scorch / Plague Arrows | first **Jewel of Life**, 2 Soul, Box of Kundun +2; Aqua Beam |
| V | Tersia, floors 1-5 | 80-120 | -- | **Set B**: DK Dragon, DW Legendary, Elf Guardian. A piece a floor: helm, gloves, boots (2), pants, armour | Greater Vigor; class Greater stat + Kinship; Frenzy + Second Wind; Bloodwell; Renewal | LT1 **Horn of Uniria**; LT2 Ring of Poison (socket); LT4 Ring of Wealth (purple), Slash; LT5 **Dragon Shield / Legendary Shield +5 (2 sockets)** (Elf: Ring of Ice) |
| V | Tersia, floors 6-7 | 120-160 | Balrog: DK Sword of Destruction, DW Staff of Resurrection +4, Elf Chaos Nature Bow (3 sockets) | -- | LT6: Meteor / Spirit Plague / Scatter Volley; Balrog: **Wrath** + Stormcall | LT6 Death Stab, Cometfall, Penetration; Balrog: Ring of Wisdom (legendary), **Horn of Dinorant** (Icarus asks 160 and wings ask 180), Rageful Blow, Inferno, Summon +5, Box of Kundun |
| VI | the Keeper (Tarkan) | 100-160 | DK Sword of Destruction +7, DW Staff of Resurrection +7, Elf Saint Crossbow (3 sockets) | -- | DK Immolate + Ice; DW Inferno + Thunder; Elf **Greater Barrage** + Glacier | Pendant of Fury (legendary); Life, Chaos, Feather, Box of Kundun +3 every clear |
| VII | Tersia, Icarus: Sky Door, Phoenix | 160-200 | Phoenix: DK Dark Breaker, DW Dragon Soul Staff, Elf Celestial Bow (3 sockets, 2nd class) | **Set C (2nd class)**: Dark Phoenix, Grand Soul, Divine. Sky Door gloves+boots, Phoenix helm+pants. **Magic Gladiator: Storm Crow** gloves, boots, pants, armour | Greater Ascendance; Evil Spirit; Bulwark (DK, with the Dark Breaker) | Feathers, Life, Chaos, Box of Kundun +3 |
| VIII | Sevina | 200 | -- | the treasure: Set C's armour | trial: Second Wind. Treasure: **Whirlwind / Pyroblaster / Piercing Volley** | DW: Grand Soul Shield; 10 Firecrackers each part |

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
| Shieldless guard | -- | -- | Bulwark (Phoenix, with the Dark Breaker, 2nd) |
| Elf Frost Arrow | Chill (Peia) | -- | Frost Arrow (Atlans) |
| Elf Multi-Shot | -- | Barrage (Devin), Plague Arrows (Atlans) | Greater Barrage (Tarkan) |
| Elf Penetration | -- | Scatter Volley (LT6) | Piercing Volley (Tear of Elf, 2nd) |
| Stats | Rare: class stat (Devin) | Lesser Ascendance (Devin), Greater stat (LT1-2) | Greater Ascendance (Sky Door, 2nd) |
| Damage | -- | Stormcall (Balrog) | Wrath (Balrog) |

## What moved, and why

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
