# Rings and pendants

Started 2026-10-01 (the user: "we need to drop also rings and pendants how it was in MuMain").
The four 0.75 pieces, group 13. WebZen's GameServer 1.00.93 first, OpenMU's
`Version075/Items/Jewelery.cs` where WebZen is silent.

| item | 13/n | MuMain model | slot | drop level | durability | resistance |
|---|---|---|---|---|---|---|
| Ring of Ice | 8 | `Item/Ring01.bmd` | ring (either hand) | 20 | 50 | ice |
| Ring of Poison | 9 | `Item/Ring02.bmd` | ring | 17 | 50 | poison |
| Pendant of Lightning | 12 | `Item/Necklace01.bmd` | amulet | 21 | 50 | lightning |
| Pendant of Fire | 13 | `Item/Necklace02.bmd` | amulet | 13 | 50 | fire |

Every class wears every piece; the level asked is the drop level (OpenMU's
`CreateItemRequirementIfNeeded(item, Stats.Level, level)`). MuMain loads the models as
`AccessModel(MODEL_HELPER + i + 8, "Ring", i + 1)` and `(MODEL_HELPER + i + 12, "Necklace",
i + 1)` (ZzzOpenData.cpp:711-716), all four on one 16x16 sheet, `Item/ring.OZJ`; nothing
tints them apart. The ring goes to `EQUIPMENT_RING_RIGHT` and the pendant to
`EQUIPMENT_AMULET` (ZzzInfomation.cpp:1085-1094); either ring slot takes a ring.

## What a piece does (WebZen)

- **Resistance**: item.txt gives each a 1 in its own element, and `CItem::Convert` multiplies it
  by the plus (`m_Resistance[i] = p->Resistance[i] * m_Level`, zzzitem.cpp:433), so a +0 piece
  resists nothing. The character's resistance in each element is the **largest** of the two rings
  and the pendant (`Max3`, ObjCalCharacter.cpp:1333-1345), not their sum. The roll is the
  monsters' own: the element is turned aside when `rand() % (r + 1) != 0`. In 0.75 only Ice (the
  Ice Monster's chill) and Poison act on the hero, so the pendants' resistances do nothing here.
- **The option** is life regeneration, +1% to +3% from a drop (`AT_LIFE_REGENERATION`,
  zzzitem.cpp:1121-1137). Off rest, `gObjRestPotionFill` adds both rings' and the pendant's
  percent of maximum life every seventh second (user.cpp:23387-23520). The rest bonus (3% plus
  one piece's option every five seconds) is not carried: the sim's rest is OpenMU's.
- **Excellent**: a ring takes the armour family (life, mana, damage decrease, reflect, defence
  rate, zen) and a pendant the weapon family (zzzitem.cpp:1380-1440). The Pendant of Lightning
  takes the staff's wizardry lines.
- **Price** is the group-13 formula, `100 + L^3` with `L = drop level + 3 x plus`, times
  `1 + option` (zzzitem.cpp:2532).

## How it drops (WebZen)

Not the jewel group: `(type == 13 && index < 8)` only (MonsterItemMng.cpp:401). A ring or a
pendant is an ordinary item: in the pool from fifteen levels under the monster, at plus
`(monster level - drop level) / 3`, never past +4 (`MODIFY_DROP_PREVENT_OF_RING_N_NECKLACE_LV_5_OVER`,
zzzitem.cpp:5976-5997) or the breed's MaxItemLevel; the option and an excellent roll as any
item's. No luck: nothing in `CItem::Convert` reads it on a ring.

## Ours

- The Pit's elf gets a **Ring of Ice with one socket** in place of the shield her bow cannot
  hold, and Evil Spirit for it (the user, 2026-10-01: "give ring with +1 sockets, and give only
  evil spirits rune").
- A ring's and a pendant's sockets take every armour rune -- Undying, Keen Eye, Bloodwell,
  Frenzy, Renewal and Evil Spirit -- by any class, and no weapon's (the user, 2026-10-02: "allow
  to put runes on jewels and pendants"). Jewellery drops roll sockets as weapons and armour do
  (kSocketChance, kMoreSocketChance), but a ring holds one socket at most, from anywhere (the
  user, 2026-10-02: "maximum amount of sockets for rings is 1"; sim::mostSocketsOf). The Pit's
  Ring of Ice is back to one socket, and its boots have two, every class's.
- The knight's and the wizard's Pit shields have two sockets, for Frenzy and Evil Spirit.

## Powers (ours, 2026-10-03)

The user: "lets make new rings and pendants, which make sense from classical ARPG experience,
like exp gain, zen gain", then "idea is that there is really good rings or pendants which has
multiple things", "if there is only 1 options its green, if more blue, purple, legendary", and
after three rounds on the numbers, "its better, lets implement". The proposal page:
claude.ai/artifact/RQANq4YNoKpejyUYd29V5u.

Five pieces in group 13's free numbers, each on MuMain's own model with its stones re-hued on a
copy of ring.png (source/textures/ring_*.png, necklace_fury.png):

| item | 13/n | file | drop level | signature power | +0 | +4 | +6 | +9 |
|---|---|---|---|---|---|---|---|---|
| Ring of Wisdom | 21 | RingWisdom (Ring01, sapphire) | 24 | experience from kills | 1% | 2% | 4% | 8% |
| Ring of Wealth | 22 | RingWealth (Ring01, amber) | 18 | Zen from kills | 2% | 4% | 6% | 12% |
| Ring of Fortune | 23 | RingFortune (Ring01, amethyst) | 40 | item find | 1% | 3% | 5% | 10% |
| Ring of the Leech | 24 | RingLeech (Ring01, crimson) | 32 | life per landed blow | 1 | 2 | 3 | 5 |
| Pendant of Fury | 25 | NecklaceFury (Necklace01, fire-orange) | 48 | critical damage | 2% | 4% | 6% | 12% |

- **A value** is `lo + (hi - lo) x (plus / 9)^2`, rounded (sim::affixValue): weak at +0, most of
  it in the last pluses. Every worn piece adds its own; two rings of a kind both count.
- **What each does.** Wisdom multiplies a kill's experience (the hero's own, a summon's for her,
  a guard's he helped). Wealth multiplies the Zen rate beside the excellent armour's x1.4.
  Fortune multiplies both item rolls of a kill (the excellent and the plain), out of the Zen and
  the nothing below them. The Leech adds life on every wound he lands on a monster, per body.
  Fury raises a critical's top of the band, swing and spell alike; it draws no dice.
- **Colour is the count.** A drop rolls how many powers (sim::kAffixCountShare): one 55%
  (green), two 28% (blue), three 13% (purple, from monster level 40), four 4% (legendary, from
  60); a count a kill cannot reach is out and its share goes to the rest. The extra powers are
  drawn from the four its signature leaves, none twice, and kept in `Held::affixes`. Excellent
  lifts the name to purple at least and a socket to blue.
- **Where they drop.** As an ordinary item in the 15-level band; past it a deep kill still draws
  them on kDeepJewellery (15%) of its item drops, or purple and legendary could never fall. At
  monster level 35 about 0.4% of kills leave one, at 75 about 0.6% (sim_test).
- **Refining and luck.** Bless and Soul take every ring and pendant, MU's four too, at armour's
  odds and to the armour's caps (sim::refinable). A ring or pendant drop rolls luck at 4 in 100;
  luck worn on one adds kLuckCritical and the Soul's +25%. MU's four keep their resistance, a
  point a plus.
- **Quests** pay one on the first clear, every class's: the Ring of Wealth +1 (Marlon, Peia),
  Wisdom +2 with Wealth (Devin), the Leech +2 with Fury (Catacombs), Fortune +3 with Wealth
  (Knights' Halls), the Pendant of Fury +3 with the Leech and Wisdom (the Pit), Wealth +3 with
  Wisdom and Fortune (The Red Floor), and the one legendary, Wisdom +4 with Wealth, Fortune and
  the Leech (The Scythe). Quest jewellery comes lucky with its option, as quest gear does.
- **Resistances** (the user, 2026-10-04: "we need more resistance options for rings and
  pendants, all of them as potential options"). Four more powers any of the five may draw as a
  further power, none a signature: Ice, Poison, Lightning and Fire resistance, 1 point at +0, 2
  at +4 and +6, 4 at +9 (MU's own ring at +4). Every worn piece's adds on top of the largest of
  MU's four worn (Max3), so MU's Pendants of Fire and Lightning now count too. A drop's extra
  powers are drawn from eight, so about half are resistances. `--give` takes A6 Ice, A7 Poison,
  A8 Lightning, A9 Fire. See "Resistances" below for what each element stops.
- **Not yet:** the shop price is MU's group-13 formula and ignores the powers. `--give` takes
  `A<n>` for each further power (1 Wisdom ... 5 Fury): `--give RingWisdom::+9LA2A3A4W`.

## Resistances: who casts what (2026-10-04)

The user: "we need to map all monsters which use some spell, and what type of damage is that so
resistances make sense". WebZen never lessens damage by resistance: ResistanceCheck
(ObjBaseAttack.cpp:558-700, 1.00.93) rolls the hero's resistance r against a spell's side effect
only, turning it aside r times in r + 1 (retResistance). Skill.txt's attribute column names each
spell's element (-1 none, 0 ice, 1 poison, 2 lightning, 3 fire, 4 earth). The breeds' spells are
Monster.txt's A.Type, which agrees with our mu.db `attack_skill` for every breed to 52.

| element | what it does to him | resistance does | breeds (number) |
|---|---|---|---|
| Poison (skill 1) | poisoned, 3% of life left every 3 s for 20 s (kPoisoners) | turns the poison aside, r in r+1 (WebZen) | Poison Bull (8), Larva (12), Poison Shadow (39) |
| Ice (skill 7) | iced, half speed for 10 s (kChillers) | turns the chill aside, r in r+1 (WebZen) | Ice Monster (22), Silver Valkyrie (52) |
| Lightning (skill 3) | pushed a tile (kBeastPushDelay) | turns the push aside, r in r+1 (WebZen's AT_SKILL_THUNDER) | Thunder Lich (9), Devil (37), Lizard King (48), Blood Castle's Magic Skeletons (89, 95, 112, 118, 124, 130); and the Hydra's (49) head beams, which push (ours) |
| Fire | damage only | 5% less damage a point, to half (ours: MU's fire turns nothing aside) | Meteorite (skill 2, WebZen's earth, a burning rock here): Lich (6), Cursed Wizard (34); Fire Ball (skill 4): Blood Castle's Giant Ogres (87, 93, 99, 116, 122, 128); Flame of Evil (A.Type 150), one blow in five: Death Gorgon (35), Balrog (38), Hydra (49), Drakan (73) |
| none | damage only | nothing | Power Wave (11): Hell Spider (13), Ice Queen (25); Energy Ball (17): Yeti (19), Vepar (46), Alquamos (69), Queen Rainer (70) |

Every other breed swings and has no spell; Icarus's Mega Crust (71) among them (WZO A.Type 0). Open: the Ice Queen's is Power Wave in both WebZen and
OpenMU, so she neither chills nor answers to Ice resistance; making her ice would be ours.

