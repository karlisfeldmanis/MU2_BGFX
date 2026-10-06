# Lirien: The Drowned Song and The Drowned Halls

Atlans's quests, both ours (the user, 2026-10-05). Lirien, the elf envoy Noria's queen kept in
Atlans before the sea took it, stands in the safe basin at 25,24 by the arch, facing the vault:
`source/npc/ElfEnvoy.json` (Divine set +7, Celestial Bow +7, Wings of Spirits), NPC number 700
(ours), voiced by Kokoro-82M's af_heart (`tools/voice.py` VOICES `"lirien"`).

## 1. The Drowned Song (Peia, quest 18)

Peia offers it in Noria from level 70, beside Noria's Song. Her envoy's song stopped; go through
the north-east gate and find her. One uncounted step, "Find Lirien in Atlans": talking to Lirien
settles it (`Realm::questMet`) and it is handed in to her (`QuestRow::receiver`), never to Peia.
Once: 60,000 experience, 40,000 Zen, 3 Firecrackers. Offer in Peia's voice (`peia_2`), hand-in
in Lirien's (`receiverVoice`).

## 2. The Drowned Halls (Lirien, quest 19)

Offered once the Drowned Song is handed in, from level 70, every 12 hours, to every class. Her
offer opens with Peia having sent the hero, then the halls: Kundun's creatures, her drowned guard
made Silver Valkyries, the Hydra in the deep trench.

Kills on Marlon's ladder, weakest first (40, 35, 30, 30, 25, 20), and the boss last, all of it.
Populations from Atlans's cooked nests.

| # | step | breed | level | count | in Atlans |
|---|---|---:|---:|---:|---:|
| 1 | Bahamuts | 45 | 43 | 40 | 29 (8 s respawn) |
| 2 | Vepars | 46 | 45 | 35 | 45 |
| 3 | Valkyries | 47 | 46 | 30 | 43 |
| 4 | Great Bahamuts | 51 | 66 | 30 | 65 |
| 5 | Silver Valkyries | 52 | 68 | 25 | 85 |
| 6 | Lizard Kings | 48 | 70 | 20 | 65 |
| 7 | Hydras | 49 | 74 | 4 | 4 (150 s respawn) |
| 8 | Return to Lirien | | | 1 | |

184 kills: Marlon and Peia ask 210, Devin 170.

Rewards (the user, 2026-10-05): every clear 120,000 experience, 200,000 Zen, 2 Jewels of Soul,
3 Firecrackers. The first clear adds 500,000 experience, a weapon of Atlans's band and runes.

Weapons, held against the Balrog's (two sockets each), since both are hunted from about 70:

| class | weapon | sockets | against the Balrog's |
|---|---|---:|---|
| knight | Crystal Sword (72, 89-120, speed 40, two hands) | 2 | Bill of Balrog 76-102, speed 25 |
| wizard | Staff of Resurrection (70, magic 70, one hand) | 2 | Legendary Staff, magic 59 |
| elf | Aquagold Crossbow (72, 78-92) | 3 | Chaos Nature Bow 88-106: less damage, one more socket |

The Chaos Dragon Axe and Chaos Lightning Staff are left for the Chaos Machine.

Runes, each one no quest paid before: knight Ice, wizard Thunder (raises his Lightning and his
Stormcall), elf Frost Arrow (unlocked for the first-class elf that day, as Arcane Echo is the
wizard's), and Second Wind for every class.
