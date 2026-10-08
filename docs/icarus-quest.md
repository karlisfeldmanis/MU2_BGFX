# Icarus's quests: The Sky Door and The Phoenix's Contract

Written 2026-10-07. The user: 'there will be not quest giver in icarus, but there will be quest in LT
quest giver. we need lore for her why char has to go to icarus, what lvl and what is required'.
**Built 2026-10-08** (the user: 'do it'): `tersiaSkyDoor` and `tersiaPhoenix` in
src/sim/quests.cpp, keys `tersia_sky_door` and `tersia_phoenix`, sim::kSkyDoor and
kPhoenixContract; Icarus's travel row ("icarus", level 160, 10,000 Zen, landing 15,13), opened by
taking link 8 and refused without wings or a Dinorant (TravelRefusal::Wings); her voice as
`tersia_8` and `tersia_9`. Link 9 asks 180 as written, not the class change. Tersia, the Lost Tower's giver (docs/lost-tower-quest.md), gets two more contracts,
links 8 and 9 of her chain. Icarus has no safe tile and no NPC (icarus-port §0), so both are
taken and handed in at her desk in the tower's hall.

## What the lore stands on

MU's own, the rest ours:
- **Icarus is entered from the top of the Lost Tower**: the door at the south end of floor 7,
  "Lost Tower 8F" in WebZen's own notes (icarus-port §0, §2.1). Nothing else in MU leads there.
- **"You can enter Icarus only with wings, dinorant"**: MuMain's own message
  (`Localization/*.en.resx`). The door asks a flying thing; there is no ground, only cloud
  (MainScene.cpp:463 draws none).
- **"Dark Phoenix monsters which dominate over ... Icarus"**: MuMain's own words, in the third
  class's quest text (`Localization/*.en.resx`). The Phoenix rules the sky.
- **Loch's Feather falls in Icarus**: MU's drop (gObjMonster.cpp:4620), the second wings'
  ingredient. Ours in Atlans and the Lost Tower too, and in Icarus since 2026-10-07.
- Its breeds and levels are MU's (icarus-port §4): Alquamos 75, Mega Crust 78, Queen Rainer 82,
  Drakan 86, Alpha Crust 92, Phantom Knight 96, Great Drakan 100, the Dark Phoenix 108.

**Ours:** the guild's contracts, the raids on the tower from above, and every word Tersia says.
They follow her text pass of 2026-10-02: she never went past the third floor, and what she knows
of the heights comes from the shrine's old records and from what comes down the stairs.

## Lore

### Above the tower (ours, on MU's facts)

The shrine was built at the top of the world for a reason: above its last floor there is a door,
and beyond the door there is only sky. The shrine's keepers called that sky Icarus, after the
first of them who went through and did not come back. The way across is a road of cloud and old
stone that never touched the ground, and no one can stand on it who cannot fly.

When Kundun plundered the shrine, his creatures took the sky as well as the floors. The Balrog
held the top of the tower, and while it lived nothing passed its floor in either direction. Now
the Balrog is dead, and the door stands open. At night the guard in the hall hears wings over the
roof. Things come down out of the cloud: star-bodied Alquamos, the violet Queen Rainers, Crusts
in plate with blades of lightning. Over all of them rules the Dark Phoenix, a bird of fire with a
rider on its back, at the far end of the cloud road.

### Why the hero goes

1. **The door is open because the hero opened it.** Killing the Balrog ("The Scythe") cleared the
   last floor, and with it the only thing standing between the sky and the tower.
2. **The guild posts a new contract.** The creatures coming down through the roof are Tersia's
   problem: the hall is the last safe room in the tower. The guild pays to hold the door, and then
   to break what rules the sky.
3. **And the sky holds what the hero needs next.** Loch's Feather, the second wings' ingredient,
   falls up there; Tersia knows it from the records. This is the in-world reason a hero who wants
   the second wings keeps going back.

### Short forms

- **Icarus** -- "A road of cloud above the Lost Tower. Only the winged may walk it."
- **The Dark Phoenix** -- "The fire bird that rules Icarus, and its rider."

## The requirements

| | The Sky Door (8) | The Phoenix's Contract (9) |
|---|---|---|
| level | **160**, the door's own level (gate 62) and the Dinorant's | **180**, the first wings' level |
| before it | The Scythe (link 7) handed in at least once | The Sky Door handed in at least once |
| to get in | wings or a Horn of Dinorant worn: the door refuses otherwise ("You need wings or a Dinorant to enter Icarus."), and in Icarus the last one cannot be taken off | the same |
| where | the north road and the middle band, 0-140 steps from the door | the south lane and its end, 136-289 steps |
| repeat | every 12 hours, as all her links | every 12 hours |

Tersia offers link 8 from 160 whatever the hero wears, and tells him what the door asks; the door
enforces it. A hero who takes it without a flying thing is told so at the door, as now.

**Travel:** taking link 8 opens Icarus's row on the Tab list (step 8 of the port: "Icarus", 10,000
Zen, landing 15,13, refused without flight), as taking a Lost Tower link opens its floor.

## Text

The card font has no em dash: "--" prints literally.

### 8. The Sky Door

- **Offer:**
  - "Since the Balrog fell, something comes down through the roof at night. I hear wings over the
    hall."
  - "The shrine's records say there is a door above the last floor, and beyond it only sky. The
    keepers called it Icarus. The first of them went through and never came back."
  - "The guild wants that door held. Clear the things on the cloud road -- the Alquamos, the
    Crusts, the Queens -- and push them back from the tower."
  - "You cannot walk on cloud. Wear wings, or ride a Dinorant, or the door will not let you
    through."
- **Underway:** "I still hear them on the roof. The road is not clear yet."
- **Hand-in:**
  - "Quiet. For the first time since the Balrog fell, the roof is quiet."
  - "The guild pays for a held door. And the records say something else falls up there -- Loch's
    Feather, the thing the wing-makers want. Keep what you find."
- **Not yet (repeat):** "The sky does not stay empty. When the wings come back, so does the
  contract."
- **Steps:** Alquamos 20 (11 on the map), Mega Crusts 15 (10), Queen Rainers 8 (6), Drakans 8 (6);
  Return to Tersia.

### 9. The Phoenix's Contract

- **Offer:**
  - "The things on the road were sent. Something at the far end of the sky rules them."
  - "The records call it the Dark Phoenix: a bird of fire, with a rider on its back. Everything up
    there answers to it."
  - "Its guard walks the south lane -- Phantom Knights, and Drakans black and red. Go through them
    and bring the bird down."
  - "No guild hand has been that far. If you come back, you will be the first name in a new book."
- **Underway:** "The bird still flies. I can see its fire from the roof at night."
- **Hand-in:**
  - "The fire is gone from the sky. I watched it go out from the roof."
  - "First name in the new book. The guild pays its best for this one."
- **Not yet (repeat):** "A new bird always rises where the old one burned. When it does, come back."
- **Steps:** Alpha Crusts 10 (6), Phantom Knights 20 (12), Great Drakans 15 (12), the Dark Phoenix
  1 (1); Return to Tersia.

## Rewards

The user, 2026-10-07: 'dont give 2nd wings but give feathers, jewels, epic & legendary runes, Dark
Breaker'. Above the Balrog's (link 7: 250k exp, 250k Zen, 5 Bless, 2 Soul, 1 Chaos a clear).

| # | every clear (12 h) | first clear |
|---|---|---|
| 8. The Sky Door | 300k exp, 300k Zen, 5 Bless, 2 Soul, 1 Life | 1.2M exp; **2 Loch's Feathers**; Rune: **Spirit Plague** (Epic, every class) |
| 9. The Phoenix's Contract | 400k exp, 400k Zen, 6 Bless, 3 Soul, 1 Chaos, 1 Life, **1 Loch's Feather** | 1.6M exp; **2 Loch's Feathers**; a two-socket weapon (knight **Dark Breaker**, the Phantom Knights' own blade, worn as a Blade Knight; wizard Chaos Lightning Staff; elf Aquagold Crossbow); a Legendary rune (knight **Hellfire**; wizard **Bulwark**, Soul Barrier with his two-handed staff; elf Piercing Volley) and an Epic one (knight **Immolate**, wizard **Scorch**, elf **Plague Arrows**); Rune: **Evil Spirit** (Legendary, every class), moved here from link 8 on 2026-10-08 |

- **No wings.** The feathers are the way: four from the first clears and one a Phoenix repeat, beside
  the one in 500 that drops; the wings are still made in the Chaos Machine.
- **Runes new to quests:** Evil Spirit, Spirit Plague, Hellfire, Immolate, Scorch and Plague Arrows
  are paid by no other quest, nor is Bulwark, the wizard's since 2026-10-07 (the user: 'we need
  also for DW'). No elf-only Legendary is left unpaid, so hers repeats Piercing Volley, as Frost
  Arrow came twice in the tower.
- **Held for the class change:** the Legendaries here are all the second class's (Evil Spirit and
  Hellfire since 2026-10-07, the user: 'it make sense that most strongest runes s for 2n class';
  Bulwark and Piercing Volley before), so like Kantur's Legion's runes they are earned before 200
  and set as a Blade Knight, Soul Master or Muse Elf. The Epics work at once.
- **The weapons:** the Dark Breaker is the Blade Knight's (class level 2), so a knight before
  Sevina's class change carries it until he can wear it. The wizard and the elf have no second-class
  weapon built; the Balrog already gave the elf the Chaos Nature Bow, so hers is the Aquagold.

## Open

- Whether link 9 should ask the class change (Sevina's, from 200) instead of 180.
