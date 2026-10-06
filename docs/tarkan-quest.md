# Tarkan's quests: The Road of Kantur and Kantur's Legion

Draft, 2026-10-06 (the user: 'we need a lore for atlans quest give and min lvl requirment to go
to tarkan and meet next quest giver'). Nothing built. The shape is Atlans's (docs/lirien-quest.md):
a hook with no kills, handed in to the next world's giver, then that giver's map clear.

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
**`source/npc/NpcSenatus.json`**: MU's Senior (MODEL_NPC_SENATUS), an old white-bearded man in
a deep blue hooded robe with a thin staff. It's built but stands nowhere yet, so it would need
indexing and cooking. NPC number ours, as Lirien's 700 (701). Voice: an old man's, picked in
`tools/voice.py`.

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
> them still lives, he is where the road comes up. Find him. Tell him Atlans is quiet again."

Underway:
> "The door is in the south-west, past the Hydra's trench. The keeper waits where the road
> comes up."

Hand-in (the Keeper's voice):
> "Someone came up the sea road? Then the Hydra is dead, and the envoy still sings. Kantur
> waited a long time to hear that."

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

**Rewards, proposed.** Every clear: 150,000 experience, 250,000 Zen, **1 Jewel of Life**.
MU brought the Jewel of Life with Tarkan in the 0.84 patch, and ours never drops
(`Jewel03`, `drops_from_monsters: false`), so a quest is its natural home. First clear:
+600,000 experience, and a first-clear item per class, to be chosen (a second-class piece or a
rune, as Lirien's paid weapons and runes).

## Decisions open

1. **The gate level** (Atlans gate 53, the Tab row, the Road's `minLevel`; all three one number).
   Built today at **100**. That's the Atlans rule, just over the breeds' 72-93, as Atlans's 70
   sits over its 43-74. MU asks 130 (140 at launch); our doubled-gate rule would make it 260.
2. **The Keeper's name** and the figure (NpcSenatus, or another).
3. **The first-clear items.**
