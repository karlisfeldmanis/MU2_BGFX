# Apostle Devin's quest -- "The White Silence"

Devias's quest giver, agreed with the user 2026-09-29, not yet in `src/sim/quests.cpp`. The same
for every class (`natives` all three, no stranger line), repeatable every 12 hours like Marlon's
and Peia's. Devin is MU's own (Priest Devin 406, Season 6's `Devias.cs:36`, 181,35 SouthEast;
`Npc/devin.bmd`), not Version075's; his story is ours.

His character, on the user's word: serious, short and clear, no jokes (a dry, joking draft was
tried and dropped, then the text rewritten from scratch): the player has just cleared Lorencia,
Noria or both; he says what Devias is and its worst danger, the Ice Queens and their storms;
every instruction in one line, in the steps' order; the Lost Tower only at the end of the hand-in. Short:
Marlon's five offer and two hand-in paragraphs; the first drafts ran long.

The card font has no em dash: "--" prints literally, as in Marlon's.

## Text

**Offer**
1. "So you are the one. Lorencia is quiet, and Noria's trees sing again. Word travels, even this far north."
2. "I am Devin, and this is Devias. Snow buries the roads, Yetis roam the passes, and Kundun's Assassins hunt anyone who walks alone."
3. "But the Ice Queens are the real danger. They call the storms down on us, and while they live, the storms never stop."
4. "Clear the Worms, Ice Monsters, Hommerds, Assassins and Yetis. Then bring down the Queens."
5. "Dress for the cold, and come back alive."

**Underway:** "Not yet. The storms still rage, and the Queens still live."

**Hand-in**
1. "The sky over Devias is clear again. Thank you."
2. "Take these. Your road goes on to the Lost Tower. Find Tersia in its hall. She will tell you the rest."

(Until 2026-10-01: "Take these. You will need them for what comes next: the Lost Tower. But that is
a story for another day." The recorded `devin_handin.wav` still says that; its `handin2` take
needs reading again.)

**Resting:** "Rest now. By morning, the Queens will call the storms back."

The Lost Tower is mentioned once, at the hand-in, when the quest is finished (the user,
2026-09-29), and nowhere else.

**Next:** "Seek Tersia in the Lost Tower" -- the first of her seven floors, "The Shrine's Door" (docs/lost-tower-quest.md),
offered once this is handed in.

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
Swedish read cloned by the English model as perfect. The pages are already recorded in
source/voice/devin; do NOT re-read them with `tools/voice.py devin` -- its main() reads
sentence-joined, squeezes pauses to 0.3 s and would undo the approved reading below.

The reading the user approved (2026-09-29), recorded by hand, not by `tools/voice.py`'s main():
each paragraph read WHOLE, not sentence by sentence (stitched sentences sounded cropped and
unnatural); reference `devin_sv_dramatic.wav` (a heroic line read as Swedish at exaggeration 0.9 --
the calm devin_sv.wav gave a monotone Devin, "without any personality"); exaggeration 0.9,
cfg_weight 0.3, seed 11; "..." spoken at the long beats and "danger!" in offer 3. Then the gaps at
sentence ends stretched in the raw take -- 0.5 s plain, 0.7 s after a question or a setup, 0.9 s
before a punchline -- and paragraphs joined 0.9 s apart into the four pages in source/voice/devin.
The break finder guesses from the text and missed twice on "Rest now." -- check each break
against the gaps before playing. The script is source/voice/devin/recorded_with.py.txt.

## Open

- Rewards (the user, 2026-09-29): every clear pays Jewels of Bless, Zen and experience; the
  FIRST clear also pays a socketed armour piece and a Rune of Creation. The rune is the same for
  every class: +20% maximum HP, in an armour socket (PowerRow::weapon false), the first armour
  rune; name proposed "Rune of the Undying", not yet confirmed. The armour is each class's top
  0.75 set, a goal a little past the hunt's end (about level 48-50 from 30, at kExperienceRate 10):
  Dragon Armor (knight; 59, asks 232 str 73 agi), Legendary Armor (wizard; 56, 87 str), Guardian
  Armor (elf; 57, 88 str 157 agi) -- OpenMU Version075 Armors.cs:71,73,84 through `asks`.
  Dragon and Legendary were not cooked in MU2_BGFX; Guardian (ArmorElf05) was. No potions.
  Amounts are proposals, not yet agreed: 3 Bless, 100,000 Zen, 60,000 experience; the first
  clear 250,000.
- His position (Season 6: 181,35 SouthEast) and the user's nod on it.
- Whether clearing the Queens should really still the planned blizzard until the repeat.
