# Apostle Devin's quest -- "The White Silence"

Devias's quest giver, agreed with the user 2026-09-29, not yet in `src/sim/quests.cpp`. The same
for every class (`natives` all three, no stranger line), repeatable every 12 hours like Marlon's
and Peia's. Devin is MU's own (Priest Devin 406, Season 6's `Devias.cs:36`, 181,35 SouthEast;
`Npc/devin.bmd`), not Version075's; his story is ours.

His character, on the user's word: dry humour and irony, a scholar who writes the dead down; he
says plainly that Devias is the real deal, not a joke; the Ice Queens are the real problem; he
names both Marlon and Peia; and he will not talk about the Lost Tower yet. Short: it was cut to
Marlon's five offer and two hand-in paragraphs because the first drafts ran long.

The card font has no em dash: "--" prints literally, as in Marlon's.

## Text

**Offer**
1. "Another hero. Marlon sends me swords, Peia sends me bows. I write each name down -- it saves time later, on the other list."
2. "I am Devin, an apostle. I came north to study Kundun's seal in peace. The peace lasted a week."
3. "You cleared Lorencia? Spiders and little dragons. Here the Yetis are taller than the walls, and the Ice Queens freeze you where you stand. This is not a joke. People die in Devias. I know -- I write it down."
4. "The rest is weather. The Ice Queens are the problem. They bring the storms, and Kundun's Assassins hide inside them. Clear the Worms, the Ice Monsters, the Hommerds, the Assassins and the Yetis first. Then go for the Queens."
5. "Dress warmly. Come back at all, and you are already ahead of most."

**Underway:** "Still alive? Good. Still windy? Then the Queens are too. Go on."

**Hand-in**
1. "Back, and with all your fingers. The wind has dropped. I have nothing clever to say, which is a first."
2. "Take these -- your name goes on the short list. And no, I will not talk about the Lost Tower. Let us see how you deal with this first."

**Resting:** "The Queens will bring the storm back by morning. They always do. And still no, about the tower."

**Next:** none until the Lost Tower is decided.

## Steps

Weakest first, counts on Marlon's ladder shape (OpenMU Version075 `Devias.cs` populations in
brackets). Devias's breeds (19-25) are not in mu.db yet.

| # | step | breed | level | count |
|---|---|---:|---:|---:|
| 1 | Worms | 24 | 20 | 40 (65) |
| 2 | Ice Monsters | 22 | 22 | 35 (75) |
| 3 | Hommerds | 23 | 24 | 30 (75) |
| 4 | Assassins | 21 | 26 | 25 (35) |
| 5 | Elite Yetis | 20 | 36 | 30 (235) |
| 6 | Ice Queens | 25 | 52 | 10 (75) |
| 7 | Return to Devin | | | 1 |

## Voice

`tools/voice.py` VOICES `"devin"`: low, a trace of a Nordic accent. The user heard three full
accents (Swedish, Norwegian, Danish, from Chatterbox's multilingual model) as too strong, and the
Swedish read cloned by the English model as perfect. The reference is `source/voice/ref/devin_sv.wav`.
Read with `tools/voice.py devin` once his row carries `row.voice = "devin"`.

## Open

- Rewards: per class like Marlon's and Peia's, or one set for all.
- His position (Season 6: 181,35 SouthEast) and the user's nod on it.
- Whether clearing the Queens should really still the planned blizzard until the repeat.
