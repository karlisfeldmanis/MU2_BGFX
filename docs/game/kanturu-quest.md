# Kanturu's quests: Kantur's Gate, The Ruins of Kantur and The Road to the Relics

Written 2026-10-25, **built the same day** (8233863c): quests 28 and 29, `testKantursGate`, protocol 53 / kept layout 18. Voices not yet made. Step 5 of opening the Ruins (the user: "step 5"). The shape is
Atlans's, Tarkan's and Aida's (docs/game/lirien-quest.md, tarkan-quest.md, aida-quest.md): a hook
with no kills, handed in to the next world's giver, then that giver's map clear.

Decided by the user, 2026-10-24/25:
- **The giver is Elpis** ("we will use Elpis", after renders of Elpis, Master01 and Oracle Layla),
  kept in the Ruins camp at 30,202.
- **No Ancient rune as quest pay**: Ancients stay drop-only, and **fall from the Relics and the
  boss on, never the Ruins** (the user, 2026-10-25: "regarding kanturu runes, we will give them
  starting with relics only and boss"): `MapRow::ancientRunes`, the Relics' `ancient_runes` drop.
  Only the wizard has Ancients so far (Ice Storm, Weakness, Pollution, Drain Life); **the other
  classes' are still to design** ("we will have to design them also"), before the Relics open.
- **The first clear pays a piece or two of the class's 380 set**; the weapons and the rest of the
  sets stay drops (sim::kanturuGear, Kanturu only, 1 kill in 44-84).
- **The clear is about 40 minutes** for a geared hero: about 200 kills.

## What the lore stands on

MU's own facts, the rest ours:
- **Kanturu is the ruin of Kantur's underground empire**, the place our Tarkan quests point at
  (docs/worlds/kanturu-port.md 1.1). WebZen names Tarkan's golden monsters "Kantur 1" and
  "Kantur 2" and their event "Kantur's underground legion" (tarkan-port 5.1).
- **The way in is Tarkan's west arch** (gate 125 at 6,199-201), which the Keeper watches. Shut
  while Kanturu ships closed (4435673c); step 7 reopens it at level 320.
- **Kanturu has three parts**: the Ruins (map 37), the Relics (38) and the Tower of Refinement
  (39), Season 2, 2006.
- **Elpis is MU's** (NPC 368): the keeper of the Tower of Refinement, who refines Gemstones into
  Jewels of Harmony (OpenMU `KanturuEvent.cs:51`, at 77,177 on map 39). Her figure is built
  (`Elpis`). In MU she stands in the Tower; ours stands in the Ruins camp because the Tower is not
  playable (the user, 2026-10-23). Her refining is not built.
- MU's Ruins have no quest giver and no camp NPCs. Baz, Amy and Elpis's place are ours.

## The giver: Elpis

Before the north ruin wall at the head of the camp, a shrine, facing the camera (look 3). NPC
number MU's own, **368**. Voice: a woman's, calm and tired, made in `pipeline/voice.py` as the
Keeper's and the Seed Master's were (to audition; the words below are the script).

Her story, ours: she is the last of Kantur's refiners. The Tower of Refinement made the jewels
that powered the empire's machines. When the empire fell, its war machines and the beasts bred to
guard the refinery went on with their orders. She has kept the camp's shrine lit ever since,
waiting for someone strong enough to clear the road to the Relics.

## 1. Kantur's Gate (the Keeper, the hook)

Offered by the **Keeper** in Tarkan once Kantur's Legion has been handed in at least once, from
**level 320** (the arch's level). Once. One uncounted step, "Find Elpis in the Ruins of Kantur",
settled by talking to her and handed in to her (`receiver`), as The Road of Kantur is handed in to
the Keeper.

Offer (the Keeper's voice, `keeper_3`):
> "You broke Kantur's legion in the sand. I did not think I would live to see it."
>
> "The arch in the west is Kantur's own gate. I have kept it shut for three hundred years,
> because what lies beyond it still obeys a dead king."
>
> "But someone on the other side has kept a light burning all that time. Elpis, the last of the
> refiners. If you are strong enough to walk where I cannot, go to her. The gate will open for
> you."

Underway:
> "The arch is at the western edge of town. Elpis keeps her light at the head of the camp beyond
> it."

Hand-in (Elpis's voice):
> "A living face, through Kantur's gate... The Keeper still remembers me, then."
>
> "Sit. You will need your strength. Nothing out there has stopped fighting."

Resting: "The shrine stays lit. It always has."

Pay: **4,000,000 experience, 1,000,000 Zen**, the 3 Firecrackers. Opens the Kanturu row of the
Move window, as the Road opens Tarkan's.

## 2. The Ruins of Kantur (Elpis, the near road)

**2026-10-25: the road in two halves** (the user: "we need kanturu on 2 parts, easy aprt and hard
part at end with options to players get more gear"; picked: split the Ruins road, the hard half
paying a Box of Kundun +4 and more 380 gear). The near road is this quest, the far road §3.

Offered once the hook is handed in, from **320**, every 12 hours, to every class. The four breeds
the walk meets first from the camp; counts follow each breed's nests
(`content/world/kanturu/nests.json`), scaled as the whole road was to about 200 kills.

| # | step | breed | level | count | nests |
|---|---|---|---:|---:|---:|
| 1 | Splinter Wolves | 351 | 142 | 30 | 22 |
| 2 | Iron Riders | 352 | 144 | 28 | 21 |
| 3 | Satyros | 353 | 146 | 32 | 25 |
| 4 | Blade Hunters | 354 | 148 | 28 | 22 |
| 5 | Return to Elpis | | | 1 | |

118 kills.

Offer:
> "Kantur's engineers bred beasts to guard the refinery road, and built machines to ride them.
> When the empire died, nobody told them."
>
> "The wolves came first, to run down thieves. Then the riders and their iron. The Satyros and
> the hunters were the road's wardens."
>
> "Further on, nearer the Relics, the vats bred worse. Leave the far road for now."
>
> "Clear the near road, from this camp to the hunters' ground. They will be back by morning: the
> old breeding vats still work. But for one night, the near road is ours."

Underway: "I can still hear them on the road."

Hand-in:
> "Quiet. I had forgotten what the Ruins sound like without them."
>
> "Take these. Kantur's armourers made them for the ones who walked this road. It is only right
> they go to you. Come back when the vats have done their work."

Resting: "Rest. The vats are slow, but they never stop."

## 3. The Road to the Relics (Elpis, the far road)

Offered once the near road is handed in (`afterAny` The Ruins of Kantur), from 320, every 12
hours, to every class: quest **30** (`road_to_relics`, `sim::kRoadToRelics`), voice `elpis_2`.

| # | step | breed | level | count | nests |
|---|---|---|---:|---:|---:|
| 1 | Kentauros | 355 | 158 | 18 | 13 |
| 2 | Gigantis | 356 | 160 | 12 | 9 |
| 3 | Berserkers | 350 | 162 | 36 | 28 |
| 4 | Genocider | 357 | 164 | 16 | 11 |
| 5 | Return to Elpis | | | 1 | |

82 kills. **The hard half**: its four breeds raised eight levels (150-156 to 158-164,
`sim::kKanturuHardLevel` 158), life x1.3, damage x1.2, defence x1.15, attack and defence rates
x1.1 (content/monsters/*01.json `combat`, each note says so). Ours. Each kill there also leaves
a piece of the 380 gear one time in 60 (`sim::kKanturuHardGearOdds`, `Realm::leave`, on the
Atlans gear roll's dice), any class's, +0, luck and the option as any drop: their level puts
most 380 pieces (drop levels 111-147) below the [L-15, L] window. Measured in `testKanturuHalves`:
263 pieces off 30,000 near-road kills (level 148), 1,121 off 30,000 far-road ones (162).

Offer:
> "The far road is another matter. The Kentauros were its archers, the Gigantis its walls."
>
> "The Berserkers and the Genocider were made for war, and kept nearest the Relics. The vats there
> run hotter, and what comes out of them is stronger every year."
>
> "Kantur's armourers kept their best work near the door. What those beasts guard, they guard
> well."
>
> "Clear the far road, to the Relics door, and come back to me alive."

Underway: "The far road is still loud."

Hand-in:
> "The door stands quiet. Nobody has seen that in a long age."
>
> "Kantur sealed his treasures in boxes such as this one. Take it, and open it far from the vats."

Resting: "The far road's vats are breeding again. Come back when they are done."

**The Relics open with it** (the user, 2026-10-25: "what about relics? how characters will get
there?", picked "After the far road"): the Ruins' door to the Relics, gate 129 at 89,89 where the
far road ends, is back (shut 4c2a0165) and asks The Road to the Relics handed in and level 320
(`"quest": "road_to_relics"` on the gate, `EnterGate::quest`; a hero without it is told "The door
to the Relics opens to those who cleared Elpis's far road.", `Barred` -3). The hand-in also opens
a 19th M-list row, Kanturu Relics, 10,000 Zen at 320, landing on the door's exit 130 (70,105).
The Relics hold Persona (161), Twin Tale (160) and Dreadfear (162), 63 in all.

## Rewards

The ladder's acts stop at Aida's IX (docs/game/quest-rewards.md); Kanturu at 320 is **act X**.
Its rules still hold: a rune no rarer than its map, a rune with the socket it fits, one whole set
per chain (a piece or two a link).

- **Every clear:** 4,000,000 experience (Aida's 3,000,000 a step up), 750,000 Zen, a Jewel of
  Life, a Jewel of Chaos, 2 Jewels of Bless, 2 of Soul, a Box of Kundun +3, the 3 Firecrackers.
  The map's x1.25 exp bonus is on kills, not on this.
- **The first clear:** 16,000,000 experience in place of 4,000,000, and **two pieces of the
  class's 380 set at +7, 2 sockets**: the armour and the helm. The knight's Dragon Knight, the
  wizard's Venom Mist, the elf's Sylphid Ray; the Magic Gladiator's Volcano has no helm, so his
  are the armour and the pants. They ask level 380 (a4aea731), so a hero at 320 carries them as
  a goal; the other three pieces and the weapon are the Ruins' drops.
- **No rune.** No Ancient (the user: "No Ancient as quest pay"), and the ladder pays no weapon
  rune twice.
- **The far road, every clear:** 6,000,000 experience (20,000,000 the first), 1,000,000 Zen,
  2 Jewels of Life, a Jewel of Chaos, 3 of Bless, 3 of Soul, **a Box of Kundun +4** (the user:
  "we need to give kundum box+4 when finish kanturu"), the 3 Firecrackers. No set pieces.
- **A quest's box at its plus** (fixed 2026-10-25, `Realm::completeQuest`'s pay): a box stacks,
  and the stacking path gave it at +0, so every quest's Box of Kundun +1 to +3 had opened as a +1.

## To build

- `src/sim/quests.cpp`: quests **28** (`kanturs_gate`) and **29** (`ruins_of_kantur`), `kQuests`
  30 (src/sim/quests.h). The hook's giver is the Keeper (`kKeeperNumber`, 701), its receiver and
  the clear's giver Elpis (`kElpisNumber`, 368). The clear asks `kanturs_gate` handed in; the hook
  asks `kanturs_legion` handed in and level 320.
- The first clear's set pieces through `QuestItem::kin` per class, as Icarus pays Storm Crow to
  the Gladiator (`sim::questPaysHero`).
- `realm_travel.cpp`: the hook opens the Kanturu row.
- Protocol and kept layout up one (src/net/wire.h); the client and the servers ship together.
- Voices: `keeper_3` (the Keeper's offer), `elpis_1` (hand-in and the clear), auditioned first.
- `testKantursGate` in tests/sim_test.cpp: the hook offered only past Kantur's Legion at 320, the
  clear's eight steps counting only Ruins kills, the first clear's pieces by class.
- Needs Tarkan's door open (step 7) to be playable; the sim test does not.

## Decisions open

1. **The hook's prerequisite**: Kantur's Legion handed in (as drafted), or Aida's clear too.
   Aida's door asks 300 and the arch 320, so most heroes will have done Aida.
2. **The set pieces' plus**: +7 like Aida's weapons, or +9 since they wait until 380.
3. **A Relics quest** (Twin Tale and the Relics' breeds) when the Relics open, Elpis's second.
4. **Elpis's refining** (Gemstone to Jewel of Harmony), MU's trade for her, not built.
