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
   pay (Halls, Kantur's Captain) went in with the Kanturu quest commit 53f1f082.
5. **The audit against MU** (2026-10-25, the user: "check we ported all birds and pet mechanics
   correctly", then "fix it"). Fixed: the ball strikes every monster within 4 of its target, four at
   most (RangeAttack's one-in-three never runs, its loopcount never raised; server.md corrected), the
   level/10 floor last; the dive and the ball aim 50 over the feet, not 100; a worn raven is drawn at
   0 life and while he lies dead (84147a4b). Then its look: the flight holds its pitch across the
   250-350 band and climbs by its speed (Direction rotated by Angle); it swings in onto
   hand_bofdgne01 at its speed's turn; the charge glides; the blue Spark03 off a bone each step it
   flies; the dash's streaks off the bird its first two steps and MODEL_DARKLORD_SKILL's flash; the
   charge's BITMAP_LIGHT on every other bone; the ball's fifteen spirits, pitched up and fanned
   +-60, homing by MoveHumming with MU's rolled aim and kicked pitch (fx/raven.h). Waiting on a cook
   (asked): MODEL_AIR_FORCE's puffs at its head as it dives (Skill/AirForce.bmd), and the level-40
   chrome and pulsing glow on dkthreebody (ZzzObject.cpp:981-1001). The 350 over the Dark Horse is
   MU's intent, dead code in both clients (Helper.Type holds a MODEL_): kept, the user's (2026-10-25,
   "Keep 350"). Both cooks done since (dfa023a5): the level-40 chrome and glow, the dash's puffs.
   Then the rest of the audit (2026-10-25, second session): the Spirits each their own 0.1% on one
   roll (the Raven's from 96, the Horse's from 102); a horse or raven sells for a third at any life;
   the exp share split by what is worn, dead or not; the raven's timer starts again on every Run past
   its gates, and a strike at speed 100+ lands the one still in the air; the Trainer's price rounds
   as a repair; the Dinorant's absorb costs the striker 1 life (sim::PetPower::strikerToll); the
   Guardian Angel absorbs 20%, not 30% (the user: "20%, MU's"); no Uniria or Dinorant into Atlans or
   put on there (the user: "Ban them, like MU"); kHorseLevel gone. Client: the horse's level up is
   twelve flares round its hooves and a level down nothing; other riders' mounts raise dust and the
   Dark Horse's hooves sound for every rider; no dust in Icarus; the Angel starts facing its owner.
   Then the rest of that list: the Dark Horse's rings and smoke off its hooves over Icarus
   (GOBoid.cpp:346-385; Earthshake::hoof, Pets::hoof), MU's size kept (the user, 2026-10-25: "use mu
   rings"); Uniria's and the dragon's stand on their own 0.34; the mount faded out with its rider's
   Teleport (a respawn still hides it at once, the user's); an off-screen raven set flying
   (CSPetSystem.cpp:645-651, tested on the ground under it); bred pets' skill, luck and option rolls
   (MixSystem.cpp:3187-3218). The birds' calls stay 3D, not MU's
   ground distance (the user, 2026-10-26: "Keep ours (3D)"); the crow notes say his tile, cooked
   (517d72f3). The audit is closed.
