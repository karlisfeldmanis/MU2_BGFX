# Sevina's class change -- the second class

Begun 2026-10-04 (the user: "lets start on second class quest where char beccomes second class with
new skins"). Sevina the Priestess (MU's NPC 235) in Devias gives it; until now she wore a grey "!"
kept for exactly this.

## The user's decisions

- **Two parts.** "This quest first part will be kill quest on lt7 and atlans, and it starts at lvl
  200. Second part is quest item drop which can drop in lt7 or atlans."
- **Order**: the quest first, Atlans after. Atlans has no monsters yet (atlans-port.md step 8), so
  the trial's Atlans steps are behind `kAtlansHunted` in `src/sim/quests.cpp`, and the treasure
  already falls in Atlans by map (`Realm::treasureGround`), so it waits on nothing.
- **The item**: per class, MU's own: the knight's Broken Sword (14,24), the elf's Tear of Elf
  (14,25), the wizard's Soul of Wizard (14,26; MU's item list says Soul Shard of Wizard).
- **The reward**: the name and the skins only. Later rules (level 2 wings, say) can ask
  `Realm::promoted()`.
- **The skins, MU's way**: the second class's bare body under any slot left empty, nothing on the
  armour.

## What is MU's

- The giver, the three treasures and most of the words: MuMain `Localization/Dialog.en.resx`
  Text_50-82 (Muren, the three treasures, Lugard, Etramu, Lunedil). Tarkan is left out of "Atlans,
  the Lost Tower and Tarkan", because this game has no Tarkan.
- The names: Blade Knight, Soul Master, Muse Elf.
- The bodies: `Data/Player/{Helm,Armor,Pant,Glove,Boot}Class20{1,2,3}.bmd`, loaded as
  `MODEL_BODY_* + MAX_CLASS + i` (ZzzOpenData.cpp:121-127), with skins 7-9 for CLASS_SOULMASTER,
  BLADEKNIGHT and MUSEELF (_enum.h:3250-3252). The sheets are named against their classes, as the
  first bodies' are: **201 (Soul Master) wears `level_man022`, 202 (Blade Knight) `level_man01`**,
  and 203 (Muse Elf) `level_man033`.
- The treasure models: `Data/Item/Quest01-03.bmd` with Ksword, Elf_E and W_S
  (ZzzOpenData.cpp:929-932). Sizes come from OpenMU `VersionSeasonSix/Items/Quest.cs:69-71`.

## What is ours

- The two-part shape. MU asked for the Scroll of the Emperor at 150 and the treasure at 220,
  each a single find.
- The trial's hunt: 40 Death Gorgons and 30 Death Knights on LT7, plus 30 Lizard Kings, 40 Great
  Bahamuts and 40 Silver Valkyries in Atlans once it is hunted. Its pay is a proposal: 1,000,000
  experience, 500,000 Zen, 5 Bless and 2 Soul.
- The drop: one kill in about 150 (`kTreasureIn10000` 67), on LT7 or in Atlans, only while the
  class's quest stands and he neither carries one nor has one lying. It uses its own dice
  (`treasureDice_`), so a seeded hunt without the quest is unchanged. It lies 120 s, twice an
  ordinary drop. It can never be sold. Sold or lost before the hand-in, its step opens again and
  it falls again.
- Neither part repeats.
- Sevina's voice (2026-10-04, the user: 'its a women give some samples, very mythical quest giver
  low voice', the first of eight). Kokoro's bf_emma cloned by Chatterbox at 0.8, three semitones
  down with a faint octave-under voice and a temple's echo (tools/voice.py `VOICES["sevina"]`).
  Every page of her four rows, `sevina_1` (the trial) to `sevina_4` (the elf's treasure): the
  offer, "not found yet", the hand-in and the rest. The treasures' rest line is
  `sevinaTreasure`'s, which voice.py now reads through the helper.

## Code

- `src/sim/quests.*`: `QuestStepKind::Find`, `questCounted`, `QuestRow::minLevel / promotes /
  boon`, rows 13-16 (`kSevinaTrial`, `kTreasureQuests`), `promoted()` and `className()`.
- `src/sim/realm_quests.cpp`: the level gate in `questLocked`, `questSettle`, `questFound` (from
  `Realm::take`), `treasure` (from `Realm::leave`), and the hand-in that takes the treasure and sets
  `Body::second`.
- The game side: `bareBody()` (game/roster.h) picks the body, and falls back to the first class's
  until the second's is cooked. `Play::redress` wears it, as does the hand-in. The card, the lobby
  plate and the pedestals show it. The dialog prints the boon where the purse would be.
- `tests/sim_test.cpp` `testClassChange`: the gate at 199/200, no drop before the quest or below
  LT7, one drop on LT7, the pickup making it Ready, the hand-in, and a restart keeping the class.

## Open

- The Soul Master's and Muse Elf's bodies: built after the Blade Knight is judged.
- The trial's offer names Atlans before its steps do.
- MU's shine on the Tear and the Soul (Level 8, glow colour 2, ZzzObject.cpp:6552, 9609-9610).
