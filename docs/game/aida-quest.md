# Aida's quests: The Sealed Wood and The Seed Master's Clearing

Written 2026-10-16. The user: 'do we have quest lore for Noria quest giver to go to meet Aida
quest giver? and who is Aida quest giver?', then, shown MuMain's four goblin NPC models,
'goblinmaster.bmd will work'. **Lore and plan only, nothing built.** The shape is Atlans's and
Tarkan's (docs/game/lirien-quest.md, docs/game/tarkan-quest.md): a hook with no kills, handed in
to the next world's giver, then that giver's map clear.

## What the lore stands on

MU's own facts, the rest ours:
- **Aida is "a mysterious forest long cut off from the MU continent and ruled by monsters"**
  (뮤 대륙에서 오랫동안 격리된 채 마물들의 지배를 받고 있던 신비한 숲, Gamemeca 2005-07-28;
  aida-port §1.1).
- **Hell Maine is "a demon in league with Kundun"** (쿤둔과 계약을 맺은 마족, the same notice).
- **The way in is a warp gate in Noria** (the same notice): Noria gate 120 at 220,30-226,30, in
  the north-east, 82 steps from Noria's town. **Since 46b5dd27 it asks level 300** (the user:
  'lets ask LVL 300 its a late game activity'), 200 for the Magic Gladiator.
- **The giver's figure is MU's Seed Master** (NPC 452, `MODEL_SEED_MASTER`,
  `Data/NPC/goblinmaster.bmd`, `ZzzOpenData.cpp:2105`): an old goblin sage with long ears, a
  white beard, a red and blue robe and a staff. In MU he is Season 4's socket NPC, who makes Seed
  Spheres for socketed gear, and he stands in Elbeland (OM S6 `Elvenland.cs:55`), not in Aida.
  So the figure and his trade are MU's. His place in Aida and his story are ours.
- **MU's Aida has no quest giver** (aida-port §1.2, decision 9). OM S6's two Aida NPCs,
  Osbourne and Jerridon (`os.bmd`, `je.bmd`), are goblins too, Season 2's Jewel of Harmony pair.
  Their tiles hold Potion Girl Amy (78,13) and Baz the Vault Keeper (86,14) since 01ddbf88.
- **Peia, Noria's giver** (MU's Elf Soldier, 257, at 171,118) is the one who sends the hero, as
  she sent him to Lirien.

## The giver: the Seed Master

A goblin who walked into the sealed wood a lifetime ago to study what grows there and never came
out. He has learned how Aida's dead trees seed themselves, and how Kundun's power seeded the
monsters along with them. He knows the seeds that hold power, which ties him to our Rune of
Creation (MU's Jewel of Creation, renamed): what MU's Seed Master does with seeds and spheres,
ours does with runes.

**The figure:** `goblinmaster.bmd`, 1 256 triangles on one sheet (`goblinmaster.OZJ`), 1.54 m in
its bind pose, drawn at 1.1 (`ZzzCharacter.cpp:14751`, outline 1.2). Four actions: idle 0 two
times in three, else one of 1-3 at random (`:3594-3600`). One slow white pulse on bone 39, a
`BITMAP_FLARE` at scale 2, light (sin(t/1000)+1)·0.3+0.4 (`:11346-11354`). Not yet a recipe in
`content/npc/`: `./mu new` it from MuMain's file, then index and cook its world.

**Where:** Aida's safe hall (x 74-93, y 5-17, 165 tiles), clear of Amy (78,13), Baz (86,14) and
the spawn box (82,8-87,14). The tile is picked on the grid when built. NPC number ours, the next
after the Keeper's 701: **702**. Voice: an old goblin's, made in `pipeline/voice.py` as the
Keeper's was.

## 1. The Sealed Wood (Peia, the hook)

Offered from **the gate's level, 300**, once Peia's own quests are behind the hero (Noria's Song
handed in at least once). Once. One uncounted step, "Find the Seed Master in Aida", settled by
talking to him and handed in to him (`receiver`), as The Drowned Song is to Lirien.

Offer (Peia's voice):
> "North-east of here the trees end in a gate no elf walks through. Beyond it is Aida, a wood
> older than Noria, cut off from the rest of the world for as long as our songs remember."
>
> "Something lives in there that is not a monster. Every spring, seeds come through the gate on
> the wind, and they are planted. In rows. Monsters do not plant."
>
> "Whoever is doing it has been alone in that wood a very long time. Go through the gate and find
> them, before the wood finishes what it started."

Underway:
> "The gate is north-east of town, past the last of the trees. Whoever plants the seeds will be
> where the wood is quietest."

Hand-in (the Seed Master's voice):
> "A visitor. A real one, with boots. Hm. The elf sent you? Then my seeds reached her. Good.
> Fifty years of throwing them over the wall, and somebody finally counted the rows."

Resting: "Mind the beds. Everything here grows, even what should not."

Pay as the Road of Kantur, scaled to its level: proposed **300,000 experience, 150,000 Zen**
(+ the 3 Firecrackers every quest pays). It opens an Aida Tab row, as the Road opens Tarkan's
(`realm_travel.cpp`), if Aida gets one (aida-port decision 3).

## 2. The Seed Master's Clearing (the Seed Master, the clear)

Offered once the hook is handed in, from 300, every 12 hours, to every class. Every 2005 breed,
weakest first on Marlon's ladder (40, 35, 30, 30, then the rare ones by their nests), the boss
last. Populations from Aida's cooked nests (`content/world/aida/nests.json`).

| # | step | breed | level | count | in Aida |
|---|---|---:|---:|---:|---:|
| 1 | Death Trees | 308 | 72 | 40 | 29 |
| 2 | Forest Orcs | 307 | 74 | 35 | 36 |
| 3 | Death Riders | 306 | 78 | 30 | 39 |
| 4 | Blue Golems | 305 | 84 | 30 | 38 |
| 5 | Witch Queens | 304 | 94 | 10 | 6, the central maze |
| 6 | Hell Maine | 309 | 98 | 1 | 3 (150 s respawn) |
| 7 | Return to the Seed Master | | | 1 | |

146 kills, a few fewer than Tarkan's 162: the Witch Queens are only six in the whole map.

Offer:
> "Sit, sit. Let me tell you what you walked into."
>
> "This wood was a garden once. Mine is the last bed of it. Then a demon came to the old elf
> roads and made a bargain with Kundun: the wood's seeds for his power. Hell Maine, he calls
> himself. Every seed he touched grew wrong."
>
> "The trees you passed on your way in were trees. Now they walk and they bite. The orcs came for
> the sap and stayed for the meat. The riders were the wood's own wardens; they still ride the
> same paths, and they do not know what they guard. The golems were stone circles. The Witch
> Queens were the ones who tended them."
>
> "Cut down the rows from my hall outward. The trees, then the orcs, the riders, the golems. Then
> the queens in the middle of the maze. Then him, in his fire."
>
> "Cut it all down and it all grows back by morning. That is what seeds do. But for a night, the
> wood will be quiet, and I will plant something better."

Underway: "Still growing out there. I can hear the roots."

Hand-in:
> "Quiet, quiet. Do you hear that? That is a wood with nothing wrong in it, for one night."
>
> "Take these. I grew them myself. Come back when it all grows back, and it will."

Resting: "Let the beds rest. By morning they will be full again."

## 3. Later: The Red Wood (the Bloody four)

Season 5 Part 4's west strip and southern band (aida-port §0), once all four are built: Bloody
Orc 549 (114, 23 nests), Bloody Death Rider 550 (115, 25), Bloody Golem 551 (117, not built yet),
Bloody Witch Queen 552 (120, 19). The same giver, a second 12-hour clear offered once the
Clearing is handed in: "the beds he lost first, to the west, grew red". Written when the Golem
is built.

## Rewards: proposed, the user's to pick

The ladder's acts stop at Sevina's 200 (docs/game/quest-rewards.md). Aida at 300 is a new act
(IX), so its pay is new. The ladder's rules still hold: a rune no rarer than its map, a rune with
the socket it fits.

- **Every clear:** 300,000 experience, 500,000 Zen, a Jewel of Life, a Jewel of Chaos, 2 Jewels of
  Bless, 2 of Soul, a Box of Kundun +3, the 3 Firecrackers.
- **The first clear:** a **Rune of Creation** of the class's choice is the Seed Master's own
  pay, the thing he grows. What it carries (Legendary, or a new power of his) is open.

## Decisions open

1. **The level.** The door asks 300, but Aida's breeds are still MU's 72-120 (OM S6 numbers). A
   300 hero kills them in a blow and earns little from them. Either the breeds are raised for
   the 300 band (life, damage, defence, experience), or the quest asks a lower level than the
   door, which a quest cannot (the gate is the way in).
2. **His name.** MU's is "Seed Master" (시드마스터). Keep it, or give him a name ("the Seed
   Master" in the quests either way).
3. **The rewards** above.
4. **His spot** in the hall, picked on the grid when he is built.
5. **His light.** MU's bone-39 flare pulses white at scale 2. Keep MU's, or smaller (the user's
   rule for glows: small first).

## His voice

Picked 2026-10-16 from six auditions of the hand-in, then three quieter takes of the user's
favourite ('E is pretty good but can we make not sot noisy?', then 'E1 is the best'). Kept as
`content/voice/ref/seed_master_audition_e1.wav`.

- **Read:** Chatterbox cloning the Keeper's reference, `keeper_cv_eighties.wav` (a man in his
  eighties, Common Voice, CC0), exaggeration 0.6, cfg_weight 0.3, temperature 0.8, seed 7.
- **The goblin:** `pipeline/world_shift.py --pitch 0.95 --formant 1.25 --jitter 0.03`: the
  spectral envelope raised a quarter (a small throat) with the pitch a touch lower, and a slow
  wobble in it. No `--rasp`: at 0.2 it was the noise the user heard.
- **The finish** (ffmpeg): voice.py's SQUEEZE, then `highpass=f=110`, `equalizer=f=320:t=q:w=1:g=-3`,
  `equalizer=f=1400:t=q:w=1.3:g=5` (the nose), `aecho=0.8:0.5:60|130:0.15|0.08` (a faint cave),
  and voice.py's POLISH (compressor, 0.4 s pad, loudnorm -16).

**Voiced 2026-10-16** (the user: 'lets finish goblin voice for quest text and make also noria
questu givei audio'): `pipeline/voice.py` VOICES `seed_master`, whose `world` key runs the WORLD step
between the take and the ffmpeg chain. The rows are not in quests.cpp yet, so voice.py reads
their pages from `content/voice/ref/pending_pages.json` (in ref/, kept out of the game). When
the rows are written, use these voices: `peia_3` (the hook's offer and underway, Peia's voice),
`seed_master` as the hook's `receiverVoice` (its hand-in), `seed_master_1` (the Clearing's four
pages); and the rows' text must match the file's, which then can go. Two lines were reworded
for his voice, which loses a 'cl' and a lone first word on every seed: 'Clear the rows' is
'Cut down the rows', and 'Quiet.' is 'Quiet, quiet.'. The Clearing's offer and hand-in are
seed 23 (RETAKES).
