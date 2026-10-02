# The Golden Archer -- the Dungeon's first chain

**First draft, 2026-09-30. Nothing here is agreed yet** except the giver: the user chose the Golden
Archer on 2026-09-30, from the Lorencia NPCs MuMain draws that MU2_BGFX had not used yet. Not in
`tools/cook.py` or `src/sim/quests.cpp` yet. The Dungeon's record (gates, floors, breeds) is
`docs/dungeon-port.md`, and this page only builds a story over it.

## Who he is in MU

- **MU's own, not ours.** Golden Archer is NPC 236 (`MONSTER_GOLDEN_ARCHER`, MuMain `_enum.h:4593`).
  OpenMU stands him in Lorencia from 0.95d on, at 175,120 facing SouthEast
  (`Version095d/Maps/Lorencia.cs:32`). He is not in Version075, as Marlon is not.
- **He is a skeleton.** MuMain builds him as `MODEL_PLAYER` with `SubType = MODEL_SKELETON2` and
  `Level = 8` (`ZzzCharacter.cpp:14383-14391`). That is the Skeleton Archer's body, Data/Skill's
  `Skeleton02.bmd`, the one the Dungeon's breed 15 wears (dungeon-port.md A §1.6). What Level 8
  does to a skeleton's drawing has not been traced; the gold is probably its shine.
- **His MU job was keeping.** He takes the Renas people bring him and counts them
  (`GoldenArcherRegistrationStrategy.cs`, `Stats.RegisteredRenas`). The story below keeps that
  shape: he keeps the gate, and he keeps count.

**Where he stands (proposal):** tile **(125,230)**, facing SouthWest (2), toward the road from
town. That is two tiles east of the arch's mouth: the gate pocket is x 121-123 on rows 232-233,
and exit 4 puts the hero out on row 231 (`075_Terrain1.att`, read 2026-09-30). He blocks neither.
He stands inside 0.75's Lich and Skeleton Warrior field (x 95-168, y 175-244), and the dead
walk past him.

**His figure:** the Skeleton02 body, cooked once for him and for the Dungeon's Skeleton Archer
(build step 7), with a bow.

## The Dungeon, in general

**Ours, built on Marlon's premise** (`quests.cpp` `marlon()`): Kundun is bound in Kalima, the
knights of Lorencia swore to hold the land while the seal held, the seal is cracking, and "every
night more of his brood climbs out of the Dungeon". Webzen's map puts the Dungeon east of
Lorencia, and Marlon says so.

The floors follow the record, one story beat each:

1. **The Catacombs** (Dungeon 1, levels 19-34). Lorencia buried its dead here long before the
   knights swore their oath. As the seal cracks, the dead rise: Skeleton Warriors, Ghosts and a
   few Skeleton Archers. Larvae breed in the old tombs, and Cyclopes have come up from below.
2. **The Knights' Halls** (Dungeon 2, 32-48). When Kundun fell, the knights dug past the tombs
   and built a hold to guard what was left of his brood beneath Lorencia. The traps are **theirs**:
   lances, iron sticks and fire meant for Kundun's creatures, still firing at anyone who walks
   there. Now Elite Skeletons, Hell Hounds, Hell Spiders, Thunder Liches and Poison Bulls hold
   the halls.
3. **The Pit** (Dungeon 3, 44-55). This is the deepest cut, and it is where the Gorgon waits.
   The last company of the oath went down to hold it and never came back. The **Dark Knights**
   on this floor are those knights, turned. (The Dark Knight is MU's breed 10; making them
   Lorencia's fallen knights is ours.)

This lets the Dungeon follow on from Marlon's quest without contradicting him. He believes he is
"what is left of that oath". He is wrong twice: some of his brothers are down in the Pit, and
one of them stands at the gate.

## His story

He was the company's archer. When the last company went down to the Pit, he was the last to fall
before the Gorgon. The others rose as Dark Knights. He rose as bone, but his oath held, and the
oath is the gold. The oath binds him to the gate, and it will not let him go below again. So he
stands where the stair comes up, and nothing climbs past him while he can draw a bow. He counts
everything he kills.

He has not gone to Marlon, and he asks the hero not to tell Marlon what he is. He gives no name
until the chain ends.

His character, as a proposal: calm, formal, slow, and old-fashioned without being archaic. A
soldier making a report. He never jokes (as Devin never does), and every instruction is one
line, in the steps' order. He is short: Marlon's length or less.

## The chain: three links, one a floor

**The user, 2026-09-30:** it is a chain; the hero clears all three levels of the Dungeon, one
link each. **Every link pays its own reward.** The **first time** also pays a socketed item and a
Rune of Creation (a jewel rune), as Marlon's, Peia's and Devin's first clears do.

Each link unlocks the next. The first link asks level 20, as gate 1 does. The breeds come from
dungeon-port.md A §1.6. The counts are proposals on the same weakest-first shape as Marlon's
ladder. The brackets hold the floor's population at one monster to a spot (decision 1 there).

### Link 1 -- "The Catacombs"

| # | step | breed | level | count |
|---|---|---:|---:|---:|
| 1 | Skeleton Warriors | 14 | 19 | 30 (32) |
| 2 | Larvae | 12 | 25 | 25 (22) |
| 3 | Cyclopes | 17 | 28 | 25 (28) |
| 4 | Ghosts | 11 | 32 | 30 (52) |
| 5 | Skeleton Archers | 15 | 34 | 7 (7) |
| 6 | Return to the Golden Archer | | | 1 |

Only seven Skeleton Archers spawn on Dungeon 1. Either keep the count at seven, or let Dungeon
2's count too.

**Offer**
1. "Stop there. You are alive, and you mean to go down. Few who do come back."
2. "Do not be afraid of me. I was a knight of Lorencia once. What you see is what the Dungeon left."
3. "Below us are the Catacombs, where Lorencia buried its dead. They do not stay buried now. Every night they climb, and every night I shoot them back down."
4. "Go down the stair. Kill the Skeleton Warriors, the Larvae and the Cyclopes. Then the Ghosts, and the Skeleton Archers in the deep tombs."
5. "I will count them as they fall. Come back when I am done counting."

**Underway:** "I am still counting. They still climb."

**Hand-in**
1. "The Catacombs are quiet. I have not heard that in a long time."
2. "Take these. You fight like one of us. When you are ready, there is more below."

**Resting:** "They will climb again by morning. I will be here, counting."

### Link 2 -- "The Knights' Halls"

| # | step | breed | level | count |
|---|---|---:|---:|---:|
| 1 | Hell Hounds | 5 | 38 | 20 (14) |
| 2 | Hell Spiders | 13 | 40 | 15 (9) |
| 3 | Elite Skeletons | 16 | 42 | 25 (21) |
| 4 | Thunder Liches | 9 | 44 | 15 (13) |
| 5 | Poison Bulls | 8 | 46 | 10 (7) |
| 6 | Return to the Golden Archer | | | 1 |

**Offer**
1. "Under the Catacombs are the Halls. We built them, when Kundun fell, to hold what he left beneath Lorencia."
2. "The traps are ours. Lances, iron and fire, set for his brood. They cannot tell you from it. Watch the floor."
3. "His creatures hold our halls now. Kill the Hell Hounds, the Hell Spiders and the Elite Skeletons. Then the Thunder Liches, and the Poison Bulls at the far end."
4. "Take our halls back. I cannot go down to do it myself."

**Underway:** "The halls are not ours yet. Go back down."

**Hand-in**
1. "The Halls are ours again, for tonight. You have done what a whole company could not."
2. "Take these. When you come back, I will tell you why I stand here."

**Resting:** "Rest. The halls will fill again, and the traps will still be waiting."

### Link 3 -- "The Pit"

| # | step | breed | level | count |
|---|---|---:|---:|---:|
| 1 | Poison Bulls | 8 | 46 | 15 (13) |
| 2 | Thunder Liches | 9 | 44 | 10 (8) |
| 3 | Dark Knights | 10 | 48 | 7 (7) |
| 4 | The Gorgon | 18 | 55 | 1 (4) |
| 5 | Return to the Golden Archer | | | 1 |

**Offer**
1. "The last company of the oath went down into the Pit to hold it. I went with them. None of us came back as we were."
2. "The Gorgon waits at the bottom. It looked on my brothers, and they rose as Dark Knights. It looked on me, and I rose as this."
3. "Kill the Poison Bulls and the Thunder Liches on the way down. Then find my brothers, and give them rest. Then the Gorgon."
4. "Marlon thinks he is the last of us. Do not tell him what you find down there. Not yet."

**Underway:** "My brothers still walk. Go back down."

**Hand-in**
1. "It is done. I felt it when they fell. My brothers are at rest."
2. "My name was Aldric. Remember it, even if Marlon cannot. Take these, with my thanks."
3. "I will keep the gate. It is what I swore."

**Resting:** "The Gorgon will rise again. They always do. I will be here, counting."

## Open

- **The whole text.** This is a first draft, for the user to cut and rewrite as Devin's was.
- **His name.** "Aldric" is a placeholder, ours. MU gives him none. He could also stay nameless.
- **Marlon's side.** Should Marlon ever learn the truth, in a line or a quest of his own? His
  hand-in points to Devias today. The Dungeon (19-55) overlaps Devias (20-52), so the two could
  both come after Lorencia, or Marlon could point to the Dungeon first.
- **Rewards: settled 2026-09-30.** Every clear pays three Jewels of Bless (the Pit a Jewel of
  Soul too), Zen and experience (proposals: 60k/150k first, 80k/250k, 120k/400k). The first
  clear of each link also pays a socketed armour piece and its rune, the same rune for every
  class (sim/items.h):

  | link | piece (knight / wizard / elf) | rune |
  |---|---|---|
  | Catacombs | helm: Brass Helm / Sphinx Mask / Wind Helm | **Keen Eye**: +10% critical chance |
  | Halls | pants: Plate Pants / Legendary Pants / Spirit Pants | **Bloodwell**: 3% of damage dealt back as life, 5% mana after a kill |
  | Pit | boots: Dragon / Legendary / Guardian Boots, two sockets | none since 2026-10-02 (was **Frenzy**) |

  The Pit's first clear also pays, since 2026-10-01 (the user: "give dungeon 3 other jewel of
  rune and new weapon ... shield with socket and rune which on miss has chance to cast evil
  spirits"), a socketed shield -- Serpent Shield (knight), Legendary Shield (wizard and elf) --
  and **Evil Spirit** for it: a shield's rune, every class's; a monster's blow that misses him has
  a 15% chance to let MU's Evil Spirit go round him -- WebZen's SkillEvil: each monster within ten
  tiles, two in three of them, struck once within two seconds -- raised by his energy. The wizard can now also learn Evil Spirit as a spell (Scroll of Evil
  Spirit, 220 energy, 90 mana, no cooldown).

  Since 2026-10-02 the Pit pays neither rune (the user: "in this quest we give to much. we dont
  give epic runes so early, but we can give that ring with +socket"): its first clear pays the
  boots, a Ring of Ice with one socket for every class, three Bless and a Soul. Frenzy and Evil
  Spirit wait for a later reward.

  Devin's rune changed the same day from the Undying (+20% max health) to **Renewal**: 3% of max
  health back every 3 s, anywhere.
- **Repeat.** Once the chain has been cleared, does the whole chain repeat every 12 hours, as
  Marlon's, Peia's and Devin's do, or does each link repeat on its own?
- **The Gorgon.** Step 4 asks for one kill. The Gorgon's respawn (10 s in OpenMU) is dungeon-port
  decision 4, and a boss pace would make the kill mean more.
- **Voice: chosen 2026-09-30.** `tools/voice.py` VOICES `"golden_archer"`: bm_george, three
  semitones down, with an undertone an octave below, a hollow ring and a crypt echo. The user
  heard five finishes on "Stop there... You are alive, and you mean to go down. Few who do...
  come back." and took the second, "two voices". The pages get read once the text is final and
  in quests.cpp with `row.voice = "golden_archer"`. main() holds every pause to 0.3 s; if his
  "..." beats come out too short, read him as Devin was (per paragraph, pauses stretched).
- **The Renas.** His MU job was counting them. Is there a place for it here, or is "counting"
  enough of a nod?
