# Tarkan's quests: The Road of Kantur, Kantur's Legion and Kantur's Captain

**In two since 2026-10-21** (the user: "we need tarkan on 2 parts, because now there is confusion"):
the Keeper's clear below is now two quests, each every 12 hours on its own clock.

**First quest, first class; second quest, second class** (the user, the same night: "first quest
gives first class rewards, 2nd quest gives second class rewards").

| part | quest (key) | steps | who | every clear | first clear |
|---|---|---|---|---|---|
| 1 | Kantur's Legion (`kanturs_legion`, 21) | Mutants 40, Bloody Wolves 35, Iron Wheels 30, Tantalos 30, Beam Knights 25: 160 kills | everyone | 150,000 exp, 250,000 Zen, a Life, 2 Bless, 2 Soul, a Chaos, a Loch's Feather, a Box of Kundun +3 | 600,000 exp; the class's two runes, the Pendant of Fury +4; the first class's weapon: Sword of Destruction +7, Staff of Resurrection +7, Saint Crossbow |
| 2 | Kantur's Captain (`kanturs_captain`, 27) | the Zaikan, the Death Beam Knight | past the class change, and the Magic Gladiator (`QuestRow::second`); not shown to a first class | 150,000 exp, 250,000 Zen, a Life, a Loch's Feather | 600,000 exp; Knight Blade +7, Staff of Kundun, Great Reign Crossbow, or the Gladiator's Dark Reign Blade +7 / Staff of Kundun by his path |

Part 2 is offered once part 1 has been handed in. The 3 Firecrackers come with each. Kantur's
Captain is the table's 28th quest: protocol 46, kept layout 12 (src/net/wire.h). Voices: `keeper_1`
(the legion) and `keeper_2` (the captain), read 2026-10-22.
The rest of this page is the one-quest design as it was, kept for its lore and history.

Written 2026-10-06 (the user: 'we need a lore for atlans quest give and min lvl requirment to go
to tarkan and meet next quest giver'), **built the same day** ('lets implement it together with
kill quests and rewards'): quests 20 and 21 in `src/sim/quests.cpp` (`roadOfKantur`,
`kantursLegion`), the Keeper as NPC 701 (`sim::kKeeperNumber`) at 197,68, Tarkan's travel row
opened by meeting him or by the Road handed in (`realm_travel.cpp`), voiced as `lirien_2`,
`keeper` and `keeper_1`, and `testRoadOfKantur` in sim_test. The shape is Atlans's
(docs/game/lirien-quest.md): a hook with no kills, handed in to the next world's giver, then that
giver's map clear.

## What the lore stands on

MU's own facts, the rest ours:
- **Kantur built Atlans** (already in Lirien's lines, `src/sim/quests.cpp` drownedSong).
- **Tarkan is "the Desert of Death"**, 죽음의 사막 타르칸 in WebZen's 2002 notice (tarkan-port §0).
- **Kantur's sealed gate stands in Tarkan**: the Kanturu arch, Object86-88, built as a sealed ruin
  (tarkan-port, "The last objects").
- **WebZen names Tarkan's golden Tantalos and Wheels "Kantur 1" and "Kantur 2"**, and their
  event "칸투르의 지하군단", *Kantur's underground legion* (WZO `eventitembag12.txt`,
  tarkan-port §5.1). So the Iron Wheels and the Tantalos are read here as Kantur's own legion.
- **The way in is Atlans gate 53, beside the Hydra's lagoon** (tarkan-port §1). The Hydra's
  death in Lirien's quest opens the road.
- MU's Tarkan has no NPC (tarkan-port §1.2). The giver is ours.

## The giver: Keeper Aurel

Kantur's last gatekeeper, at the town's stone hall where the road from Atlans comes up (the
safe hall, 187,54-203,69; spot to be picked on the grid, near 195,65). The figure is
**`content/npc/NpcSenatus.json`**: MU's Senior (MODEL_NPC_SENATUS), an old white-bearded man in
a deep blue hooded robe with a thin staff. It's built but stands nowhere yet, so it would need
indexing and cooking. NPC number ours, as Lirien's 700 (701). Voice: an old man's, picked in
`pipeline/voice.py`.

## 1. The Road of Kantur (Lirien, the hook)

Offered once The Drowned Halls has been handed in at least once, from **the gate's level**
(see below). Once. One uncounted step, "Find the Keeper in Tarkan", settled by talking to him and
handed in to him (`receiver`), as The Drowned Song is to Lirien.

Offer (Lirien's voice, `lirien_2`):
> "Not all of Kantur's people waited for the sea. Their king's engineers cut a road under the
> lagoon, south, to the desert they called Tarkan."
>
> "The Hydra nested over that road for three hundred years. It is down now, and the door by its
> trench stands open."
>
> "Kantur left a keeper at the far end, to watch the road and their gate in the sand. If any of
> them still lives, he is where the road comes up. Find him. Tell him that Atlans is quiet again."

Underway:
> "The door is in the south-west, past the Hydra's trench. The keeper waits where the road
> comes up."

Hand-in (the Keeper's voice):
> "The Hydra is dead? And the road is open again... Kantur waited a long time to hear
> that."

Resting: "Sit, traveller. The road is long and the sand is longer."

Pay as The Drowned Song, scaled: proposed **100,000 experience, 60,000 Zen** (+ the 3
Firecrackers every quest pays). Opens the Tarkan Tab row, as the Song opens Atlans's.

## 2. Kantur's Legion (the Keeper, the clear)

Offered once the Road is handed in, every 12 hours, to every class. Every breed, weakest first on
Marlon's ladder, the two bosses last. Populations from Tarkan's cooked nests.

| # | step | breed | level | count | in Tarkan |
|---|---|---:|---:|---:|---:|
| 1 | Mutants | 62 | 72 | 40 | 41 |
| 2 | Bloody Wolves | 60 | 76 | 35 | 32 |
| 3 | Iron Wheels | 57 | 80 | 30 | 23 |
| 4 | Tantalos | 58 | 83 | 30 | 63 |
| 5 | Beam Knights | 61 | 84 | 25 | 56 |
| 6 | Zaikan | 59 | 90 | 1 | 1 (150 s respawn) |
| 7 | Death Beam Knight | 63 | 93 | 1 | 1 (150 s respawn) |
| 8 | Return to the Keeper | | | 1 | |

162 kills, fewer than Lirien's 184 because each one is harder (10,000-40,000 HP against
Atlans's top at the Hydra).

Offer:
> "I did not think I would see a living face here again. Three hundred years I have watched
> that road, and nothing came up it but sand."
>
> "So, Lirien still sings, and she sent you. Then hear what waits at this end of the road."
>
> "Kantur built a legion to keep this desert: wheels of iron, and giants in black and gold.
> When the city drowned, the legion kept its last order. Let no one reach the gate."
>
> "Then Kundun gave it a new master. The Mutants and the Bloody Wolves came with him out of the
> sand. The Beam Knights came on wings."
>
> "The legion's own captain leads them now, the Zaikan, shining with what Kundun poured into
> him. And past the dunes the Death Beam Knight burns, and never burns out."
>
> "Break the legion from the town outward. Then the captain. Then the burning knight."

Underway: "The legion still marches. I can hear the wheels."

Hand-in:
> "The dunes are quiet. I had forgotten what wind sounds like with nothing in it."
>
> "Take these, with Kantur's thanks, what is left of it. The legion mends itself. When it
> marches again, come back."

Resting: "Rest. By morning the wheels will turn again."

**Rewards, as built** (the user, 2026-10-06: 'lets give 2nd class rewrads and some legendary runes
for each class', then 'jewels also, and feather'). Every clear: 150,000 experience, 250,000 Zen, a
**Jewel of Life** (MU brought it with Tarkan in 0.84; ours never drops), 2 Jewels of Bless, 2 Jewels
of Soul and a **Loch's Feather**, with the 3 Firecrackers every quest pays. The first clear,
600,000 experience in place of 150,000, and each class's own, all of which ask its second class:

| class | gear (two sockets) | Legendary runes |
|---|---|---|
| knight | Dark Phoenix Armor (`ArmorMale18`) | Whirlwind, Greater Ascendance |
| wizard | Dragon Soul Staff (`Staff10`) | Pyroblaster, Greater Ascendance |
| elf | Great Reign Crossbow (`CrossBow20`) | Piercing Volley, Greater Ascendance |

The three pieces are drop level 100, which no Tarkan or Blood Castle 6 breed reaches: the quest is
their only home outside Atlans's 1-in-400 gear roll. The runes are the second class's Legendaries
no quest paid before. Eleven things paid and the Firecrackers make twelve, `kQuestPaid`.

**Since 2026-10-09** the table above is Icarus's and the class change's, and the first clear pays the
first class's best weapon, three sockets (`kantursLegion` has the whole list). **Since 2026-10-21**
(the user: "this quest has to give better weapon for SM and same for BK and ME and MG") the weapon
goes by rank (`QuestItem::rank`, checked in `Realm::questItemFits`):

| hero | weapon, three sockets |
|---|---|
| Dark Knight | Sword of Destruction +7 (`Sword17`) |
| Dark Wizard | Staff of Resurrection +7 (`Staff07`) |
| Fairy Elf | Saint Crossbow (`CrossBow17`) |
| Blade Knight | Knight Blade +7 (`Sword21`), one-handed |
| Soul Master | Staff of Kundun (`Staff12`), one-handed |
| Muse Elf | Great Reign Crossbow (`CrossBow20`) |
| Magic Gladiator, Melee | Dark Reign Blade +7 (`Sword22`), his own |
| Magic Gladiator, Magic | Staff of Kundun (`Staff12`) |

The Magic Gladiator has no second class and counts as promoted.

## Decisions open

1. **The gate level** (Atlans gate 53, the Tab row and both quests' `minLevel`; all one number):
   **100** as built, the Atlans rule, just over the breeds' 72-93. MU asks 130 (140 at launch).
2. **The Keeper's name.** "Keeper Aurel" in the folk row; the quests call him "the Keeper".
3. **The Senior's scale.** MU stands him at 1.1; townsfolk here have no scale, so he is drawn at 1.0.
