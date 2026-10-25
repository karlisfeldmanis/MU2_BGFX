# The Dark Lord's pets: the Dark Raven, the Dark Horse's levels and the Pet Trainer

Researched 2026-10-25 (the user: "lets work on pet trainer and pets", then "basically finish all related
with pets for DL"). The two research files beside this one are the sources, every rule cited:
**server.md** (WebZen 1.00.93 and OpenMU, which is wrong in six places, its section 9) and
**client.md** (MuMain S6, Main 1.03.35 where it differs). Phase 5 of docs/plans/dark-lord.md.

## Decided (MU-true; ours where marked)

1. **Pet level and exp, both pets**: levels 1-50, the table `T[L] = (10+L)·L³·100` (T[1] = 0); 20% of
   every exp the lord gains to the one pet worn, 10% each with both (on top of his, never taken from it).
   A gain that would lift the pet past what he can wear (the raven's Command `185 + 15·L`, the horse's
   level `218 + 2·L`) is refused. At level 50 nothing more. A pet at 0 life gains nothing.
2. **Kept in the item, ours**: WebZen keeps level and exp per serial on its DataServer; ours is the exp
   as one 32-bit number in the four bytes a pet leaves nought (the three socket powers and the wing
   byte, low byte first -- the timed pets' expiry uses the same four), the level read off the table.
   So the wire, the save, the vault and the Broker carry it, and no kept layout changes. 0 is a newborn,
   level 1.
3. **Death**: at 0 life the pet stays, inert, and loses 10% of its level's exp span (`T[L+1] - T[L]`),
   which may take it a level down (WebZen's; OpenMU never drops a level).
4. **The Dark Horse by its level**: asks `218 + 2·L`, defence `5 + Agi/20 + 2·L`, blows cut by
   `(30 + L)/2` %, Earthshake `+10·L`, price `2,000,000·L`. Its look does not change.
5. **The Dark Raven** (13/5), worn in the **left hand** (no shield), Command `185 + 15·L` to wear and
   to act. Damage `180 + 15·L + Cmd/8` .. `200 + 15·L + Cmd/4`, a worn scepter adding `magic/2` %
   (less as it wears), less the target's defence; speed `20 + 4L/5 + Cmd/50`, a strike every
   `1500 - 10·speed` ms rounded up to WebZen's 500 ms timer, landing 600 ms after the swoop starts.
   Attack rate `1000 + L + L/15` against the defence rate, WebZen's 5%-at-30% when short. Never acts
   on a safe tile. The kill, its exp and its drop are the lord's.
6. **Its four modes** (WebZen's): **Normal** idles; **Random** a monster within 5 tiles of him, 30% a
   dash at the top of the band, else the ball: the target and up to four more within 4 tiles of it,
   each at /1.5; **With owner** what he last struck, within 7 tiles, always the ball (WebZen's bug,
   kept: MU's raven); **Target** the one he picked, within 7, always the dash, 30% a critical, back to
   Normal when it dies. Back to Normal on login, death, map change. Players only by the PvP rules.
7. **Its life**: worn by its own landed hits, `dmg·2/100` into a tally of 800 a point (none under 50).
8. **Spirits** (13/31: +0 the horse's, +1 the raven's), never stacked, 30M / 15M, sold for a third.
   Dropped as OpenMU has it (0.1%, monster level 102+ / 96+; WebZen's C++ drops none) and the quest
   pay plan section 6 already names (Halls: the Raven's, Kantur's Captain: the Horse's).
9. **The Pet Trainer** (226, `NPC/Breeder.bmd`), Lorencia 122,110 facing south-east (Crywolf's is
   not ours). His window, MU's in our shape: **Restore life** (each worn pet's line and price,
   `3·√V·V^¼·(1 - life/255) + 1`, doubled at 0, V the pet's full value -- 1.03.35's, MuMain's reads 0)
   and **Resurrect spirit**, the two mixes: horse = Spirit +0, 5 Bless, 5 Soul, Chaos, Life, 5M;
   raven = Spirit +1, 2 Bless, 2 Soul, Chaos, Life, 1M; 60%, failure loses the box, a level-1 pet out.
   **Jewel of Life where MU asks Creation** (plan decision 3). Pets mend nowhere else.
10. **His client**: the raven flies (darkspirit.bmd at 0.7, 250 over him, 350 on the horse, its three
    looks by level 1/20/40), perches on his left arm in town, the dash and the ball as MU draws them,
    its sounds; a pet window with the four commands; the raven's HUD frame; the cards.

## Batches (each shown in game before the next)

1. **Levels** (sim): lord_pets.h/.cpp, the horse by its level everywhere `kHorseLevel` was, exp share,
   death penalty, the cards' level and exp line. sim_test.
   **Done 2026-10-25** (56cc0bc0).
2. **The Trainer**: NPC, figure, his window (Restore life, the two mixes), the Spirits' rows, drops,
   quest pay. The horse becomes obtainable.
   **Done 2026-10-25** but the quest pay: NPC/Breeder.bmd cooked and stood at Lorencia 122,110; his
   window the Chaos Machine's with his own row (sim::kTrainerServices: Restore Life, Dark Horse, Dark
   Raven); the Spirit (13/31) and its own drop roll.
3. **The raven, sim**: the row, the left hand, its strikes and modes, the mode command (protocol).
   **Done 2026-10-25**: realm_raven.cpp; Command::Kind::RavenMode, What::Ravened / RavenSet; protocol
   56, snapshot shape 26. Monsters only: the PvP of With owner and Target waits for the duel rules.
4. **The raven, look**: model, flight, perch, dash and ball, sounds, the pet window, HUD frame.
   **Done 2026-10-25**: game/pets_raven.cpp (CSPetSystem's flight 250 up, 350 on the horse; the
   perch on Bip01 L Hand in town with his 76/77; the dash and the hovering charge; its body by level
   through gfx::Drawable::onlyMaterial), fx/raven (the dash's gold streaks, the ball's five spirit
   bolts, a level's flares), its three sounds, a boon cell in MU's command icon for its mode with
   **Shift+1-4** (MU's keys; Target the monster under the pointer), the card's damage, speed and
   Command. Ours: a line in the lane when a pet's level moves (MU says nothing). No pet info window:
   the boon's card and the item card carry its numbers. tools/record/raven.cue. The Spirits' quest
   pay (Halls, Kantur's Captain) is written in sim/quests.cpp, uncommitted beside another session's
   Kanturu work there.
