# The road to the Lost Tower -- Devin, Tersia and Thompson

The user's of 2026-10-01: once Devias's quest is done, the chain goes on to the Lost Tower.
**Apostle Devin's hand-in sends the hero there to meet Tersia**, who gives the tower's quests, and
speaking to her opens fast travel to its hall (LT1). The user chose Tersia and Thompson from MU's
own NPCs that MU2_BGFX had not drawn yet. Neither is 0.75's.

(The giver was Oracle Layla, NPC 259, `Npc/kalnpc.bmd`, until the user saw her: MU's model sits on a
boat of potions, Kalima's shop, and "does not make sense". From a contact sheet of 27 unused MuMain
NPC models the user picked Senatus (NPC 223, `Npc/NpcSenatus.bmd`), "he is old man and make sense";
then, liking a woman's voice in the auditions ("third woman voice was nice"), Tersia. Layla's and
Senatus's figure files stay in
`source/npc/kalnpc.*` and `source/npc/NpcSenatus.*`, unplaced.)

(The first plan had Thompson send the hero, an errand handed in to the tower's giver. The user took it back
the same day: Devin sends the hero straight to her. Thompson stays in Devias with lines only.)

| | Thompson the Merchant | Tersia |
|---|---|---|
| MU | NPC 231, `MONSTER_THOMPSON_THE_MERCHANT`, `MODEL_DEVIAS_TRADER` (`Npc/DeviasTrader01.bmd`, `ZzzCharacter.cpp:14344-14346`) | NPC 566, `MONSTER_MERCENARY_GUILD_FELICIA`, `MODEL_TERSIA` (`Npc/tersia.bmd`; MuMain's Korean ID 길드관리인 테르시아, "Guild Manager Tersia"; `ZzzCharacter.cpp:15005-15010`, scale 0.93, idles slowed to 0.35 and 0.3) |
| where MU stands them | nowhere in OpenMU's versions | the Mercenary Guild (OpenMU: Mercenary Guild Felicia) |
| here | Devias, west square, **195,39**, facing 4 (ours) | the Lost Tower's hall, **204,72**, facing 3, toward its door, at MuMain's 0.93 (ours) |
| figure | `DeviasTrader01` | `tersia`: a purple-haired woman in dark purple leather armour and boots |

## What is true and what is ours

**MU's own:**
- Webzen's guide: *"The Lost Tower is a huge tower located to the north of the MU continent. The
  Lost Tower had once been used as a shrine, but after the plunder of Kundun, it has become an
  utter wasteland. Kundun has assigned various forms of dreadful monsters to each floor of this
  tower."* And the Balrog, *"located on the top floor"* (InfinityMU wiki's copy of the guide,
  wiki.infinitymu.net/index.php?title=Lost_Tower).
- The blue lightning over the gate: MU draws two spinning `BITMAP_LIGHTNING+1` sprites over gate 28
  (`ZzzObject.cpp:2821-2828`, our `game/world/ornaments.cpp`).
- The floors' breeds (lost-tower-port.md A §4.1).
- **Not used yet, for later:** MuMain's own quest text (`Localization/Dialog.en.resx` Text_50-82)
  places the Scroll of the Emperor and the three treasures (Broken Sword, Tear of Elf, Soul of
  Wizard) "around the Lost Tower". Those are Sevina's class change in Devias, and the reason
  later players called this the quest tower.

**Ours:** Thompson brought the shrine's keeper bread and lamp oil until the road was closed, and
Tersia is that keeper, the shrine's last guard, still holding the hall. Their words and the rewards are ours.

## Lore

Written 2026-10-01 for the user ("we need some lore text"). Every sentence of **The tower** and
**Before the tower fell** is MU's own, from the sources under each paragraph; **Since** is ours, and
says only what Thompson and Tersia say. Not in the game yet.

### The tower

In the north of the continent of MU stands a huge tower. It was a shrine once. Then Kundun
plundered it and left it an utter wasteland. To every floor he assigned his creatures: Shadows
and Poison Shadows, Cursed Wizards, Death Cows, Devils, Death Knights and Death Gorgons. On the
top floor waits the Balrog, a foul beast with a scythe, and few warriors in all of MU can stand
against him. It is called the Lost Tower.

*Webzen's guide: "a huge tower located to the north of the MU continent ... had once been used as
a shrine, but after the plunder of Kundun, it has become an utter wasteland. Kundun has assigned
various forms of dreadful monsters to each floor of this tower. Balrog, a foul beast located on
the top floor that flourishes a scythe ... There are few warriors in the land of MU who can
compete with him." The breeds are the tower's own (lost-tower-port.md A §4.1).*

### Before the tower fell

Long ago, Kundun was sealed. Etramu, the greatest wizard of Arka, made the seal, and set the
Titan to guard the sealed stone. Muren, one of the three heroes who ended the chaos of Sekneum's
invasion, united the continent and became the first emperor of MU. When peace came, he broke his
sword, *Apocalypse*, so that no war would erupt again. The peoples swore loyalty and gave him
treasures: the Broken Sword from the knights, the Tear of Elf from the elves, the Soul of Wizard
from the wizards. His record of that peace, the Scroll of the Emperor, is said to have been seen
in the Lost Tower. But the seal did not hold. Kundun rose again, and his creatures spread across
the continent.

*MuMain `Localization/Game.en.resx`: TitanIsAGiantWhoGuards ("guards Cathawthorm where the Kundun
is sealed ... created by Eturamu to protect the sealed stone"), MurenIsOneOfTheHeroes,
AfterTheRessurectionOfKundunSome; `Dialog.en.resx` Text_50, 57-59, 62, 66, 69 (Muren, Sekneum,
Apocalypse, the three treasures, "Etramu, the greatest wizard of Arka", the Scroll seen "around
the Lost Tower"). MuMain spells him both Etramu and Eturamu; Etramu here. "The Seal of Etramu ... the very seal
which maintained Kundun's imprisonment": muonlinefanz.com/guide/systems/story.*

### Since (ours)

One keeper of the shrine never left. Tersia, the last of its guard, holds its hall, the last room in the tower
that is still safe, and listens to Kundun's creatures on the stairs. For years Thompson, a
merchant of Devias, carried her bread and lamp oil through the gate under the blue lightning,
until the road grew too dangerous even for him. Now he waits for someone who can walk it.

*Ours, the user's of 2026-10-01: Tersia as the shrine's last guard (MU makes her the Mercenary Guild's manager), Thompson's
supply road. The blue lightning is MU's own effect over Devias gate 28.*

### Short forms

For a caption, a tooltip or the travel list, if the user wants one:
- **Lost Tower** -- "Once a shrine. Kundun plundered it and gave every floor to his creatures."
- **Tersia** -- "The last guard of the Lost Tower's shrine."
- **Thompson the Merchant** -- "A Devias trader who once supplied the shrine in the Lost Tower."

## The chain

1. **Devin's "The White Silence"** (docs/devin-quest.md). His hand-in now ends: "Take these. Your
   road goes on to the Lost Tower. Find Tersia in its hall. She will tell you the rest." The
   banner's tip: "Seek Tersia in the Lost Tower".
2. **The walk**: Devias gate 28 under the blue lightning (level 40), LT1's arrival, about 184
   steps to the hall.
3. **Tersia**: spoken to, she **opens the hall's row** on the travel list. The other six rows wait
   on her links handed in (below); the stairs down need only their levels.
4. **Tersia's chain**, seven links, a floor each, the first offered once Devin's is handed in
   (`afterAny` bit 2). Before that she wears a grey "!" and answers with a line.

**Thompson** stands in Devias and gives nothing: spoken to, he says one of three lines about the
tower he used to supply (`Realm` answers him with `Shout::Greet`, as the Guild Master).

## Text

The card font has no em dash: "--" prints literally.

### Thompson's lines

- "I carried bread and lamp oil to the Lost Tower's keeper for years. No wagon goes there now."
- "The gate under the blue lightning leads to the tower. I do not take it anymore."
- "It was a shrine once, that tower. Kundun's creatures hold every floor of it now."

### Tersia's chain -- seven floors

The user's of 2026-10-01: "similar like golden archer where char has to clear each floor and gets
rewards". Seven links in `src/sim/quests.cpp` (`tersiaDoor` ... `tersiaBalrog`), a floor each, each
offered once the one before is handed in, the first once Devin's is; every link repeatable every
12 hours. **The words are in quests.cpp**; what she says of the tower is Webzen's (the shrine,
Kundun's plunder, a creature on every floor, the Balrog's scythe and "few warriors in all of MU
can stand against" it) or the game's own (the Cursed Wizards' reach, a Devil's push, the Death
Gorgons' rolling fire, the burning plates). Before Devin's is handed in she answers with a line.

**Travel** (`Realm::travelQuest`): the hall's row opens as she is spoken to; the row to floor *k*
(2-7) opens only once link *k* is **handed in** -- the user's: "character can manually use gates if
he wants and has levels, but fast travel only works when quests are done". The stairs ask only
their levels. (The Dungeon's rows open with the link taken, as before.)

| # | title | floor | steps (count, floor's population) | every clear | first clear |
|---|---|---|---|---|---|
| 1 | The Shrine's Door | LT1 | Shadows 40 (71), Poison Shadows 15 (27) | 100k exp, 100k Zen, 2 Bless | 400k exp; top gloves with a socket, Rune: Undying |
| 2 | The Poisoned Floor | LT2 | Poison Shadows 35 (60), Cursed Wizards 12 (21) | 110k, 110k, 2 Bless | 350k exp |
| 3 | The Wizards' Floor | LT3 | Cursed Wizards 15 (28), Death Cows 20 (32) | 130k, 130k, 3 Bless, 1 Soul | 400k exp |
| 4 | The Red Floor | LT4 | Death Cows 12 (19), Devils 10 (15) | 150k, 150k, 3 Bless, 1 Soul | 450k exp |
| 5 | The Knights' Floor | LT5 | Devils 12 (19), Death Knights 12 (18) | 170k, 170k, 4 Bless, 1 Soul | 500k exp |
| 6 | The Gorgons' Floor | LT6 | Devils 8 (12), Death Knights 8 (13), Death Gorgons 8 (11) | 200k, 200k, 4 Bless, 2 Soul | 600k exp |
| 7 | The Balrog | LT7 | Death Gorgons 25 (50), Death Knights 20 (41), the Balrog 1 (2) | 250k, 250k, 5 Bless, 2 Soul, 1 Chaos | 1M exp |

Kills count by breed wherever they fall, as the Golden Archer's do. **All rewards are proposals**;
new runes for the chain are the user's to choose (see below).

## Open

- Voices: Tersia is voiced, `tersia_1` to `tersia_7` (tools/voice.py `VOICES["tersia"]`: since
  2026-10-02 Kokoro's bf_isabella reading a hushed line as the reference, cloned by Chatterbox at
  0.5, a semitone and a half down with a soft echo -- the user's "lower more mystical" pick of four;
  bm_fable's shaky read before it). Devin's hand-in was read again for Tersia on 2026-10-02
  (docs/devin-quest.md).
- Text pass 2026-10-02 (the user: "not logical"): the floors are *up there*, as the tower rises to
  the Balrog; she never went past the third floor, so what she knows of the fifth is a Death Knight
  that came down the stairs, of the sixth the shrine's old records, and the twelve never reached the
  Balrog. All seven re-read with each full stop held 0.6 s (VOICES["tersia"] sentence_gap).
- Thompson's and Tersia's tiles are ours; move them on the user's word.
- New runes for links 2-7 (proposed in chat 2026-10-01, not chosen).
- Sevina's Scroll of the Emperor, which MuMain places in this tower.
