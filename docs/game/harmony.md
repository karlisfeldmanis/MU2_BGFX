# The Jewel of Harmony

Built 2026-10-25, steps 1-7 of docs/plans/jewel-of-harmony.md (the plan keeps the research, MU's
full tables and the user's decisions). This page is how it works here.

## The four items

| item | MU's row | where from | stacks |
|---|---|---|---|
| Gemstone | 14, 41 | a kill in the Ruins or the Relics, 1 in 150, beside the kill's own drop | 20 |
| Jewel of Harmony | 14, 42 | the Chaos Machine's Refine Gemstone | 20 |
| Lower Refining Stone | 14, 43 | the Machine's Smelt, from a plain piece | 20 |
| Higher Refining Stone | 14, 44 | the Machine's Smelt, from an excellent piece | 20 |

All four are named in MU's gold and bold on the ground, glow in their own colour, ring
`jewel_get` landing and picked up, are taken by the Helper's Jewels tick and the auto-loot, can't
be dropped on the ground, and trade at the Broker under Jewels (`brokerKind`). None carries the
jewel flag, which would put it in MU's jewel drop group; `sim::harmonyMaterial` stands in for it.

## On an item

- **Which items.** Swords, axes, maces, spears and bows (the weapon table), staffs (the staff
  table), shields and the five armour pieces (the defence table); a socketed piece too (the
  user's). Never wings, rings, pendants, pets or ammunition (`sim::harmonyFamily`).
- **How many.** One option an item. Worn, seven count: both hands, the helm, armour, pants,
  gloves and boots (`Realm::rearm`). The same option on several pieces adds up.
- **The jewel**, dragged onto the item in the bag, the vault or the Machine's box as a Bless is:
  60%, an option drawn by OpenMU's weights among those the item's plus allows, at that option's
  least level. A miss spends the jewel alone. The item **binds** when it takes (the user's), and a
  bound thing sells for a hundredth (2026-10-22's rule).
- **The stones**, dragged on the same way: the option a level up, never past the item's plus or 11.
  Lower 20%, Higher 80%; a miss takes the option back to its least level. Luck adds nothing.
- **Active** only while the item's plus is at least the option's level: a failed Soul greys it
  out (the card says "(at +N)") until the plus is back.
- **Strength.** Every flat option is three times MU's (`kHarmonyStrength`); Damage taken keeps
  MU's 3-5% a piece. Defense counts half, as WebZen's server applies it.
- A miss that takes a Strength or Agility requirement cut away from a worn piece he no longer
  meets moves it into the bag, as a Soul's outgrown plus does: a stone's miss, and a Soul's, whose
  lower plus switches the option off.

The options, by level 0 / 6 / 11 (MU's values; ours three times, but Damage taken):

| table | option | least plus | values |
|---|---|---|---|
| weapon | Minimum damage | 0 | 2 / 9 / 16 |
| weapon | Maximum damage | 0 | 3 / 10 / 23 |
| weapon, staff | Strength requirement | 0 | 6 / 20 / 35 off |
| weapon, staff | Agility requirement | 0 | 6 / 20 / 35 off |
| weapon | Damage | 6 | - / 7 / 14 |
| weapon | Critical damage | 6 | - / 12 / 22 |
| weapon | Skill damage | 9 | - / - / 16 |
| staff | Wizardry damage | 0 | 6 / 17 / 25 |
| staff | Skill damage | 6 | - / 7 / 22 |
| staff | Critical damage | 6 | - / 10 / 20 |
| defence | Defense | 0 | 3 / 10 / 20 |
| defence | Maximum life | 3 | - / 13 / 23 |
| defence | Life recovery (a 3 s tick) | 6 | - / 1 / 6 |
| defence | Mana recovery (a 3 s tick) | 9 | - / - / 3 |
| defence | Damage taken | 9 | - / - / 5% |

`sim/harmony.cpp` holds every number.

## At the Chaos Machine (HARMONY)

| service | box | chance | Zen |
|---|---|---|---|
| Refine Gemstone | Gemstones, any number; Fill takes the stack | 80% each | none |
| Smelt | weapons and armour with no option and no rune set in them; the 84 cheapest (Kris, Bronze set...) at +4 or more; stones from a bound piece come out free (stacks never bind) | 50% an excellent piece (Higher), 20% any other (Lower), each piece eaten | none |
| Remove Harmony | one item with an option, a Jewel of Harmony and a Jewel of Chaos | 100%, it stays bound; the jewels are spent | 100,000 to 360,000 by level |

MU takes one Gemstone or one piece a mix; ours a box of them, each rolled on its own. Fill from
bag never puts gear in to smelt. Details in docs/game/chaos-machine.md.

## Code

`sim/harmony.h/.cpp` (tables, draw, rules), `sim/items.h` (the four predicates, `kGemstoneOdds`),
`Realm::refine` and `refineAcross` (the drag), `Realm::leave` (the drop, `gemstoneDice_`),
`sim/machine.cpp` and `realm_machine.cpp` (the three services), `game/ui/describe.cpp` (the
card's Harmony line). Protocols 55 (the option on Held), 58 (`What::Harmonized`), 59 (the
services); snapshot shape 25 and 27. `tests/sim_test.cpp` `testHarmony` and the Machine's
every-service test.
