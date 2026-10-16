# The docs

Every page in `docs/`, what it is for, and the rules that keep the set small. Written 2026-10-13,
when the user asked: "we also need to get rid of old docs which is not relevant and also organize
actual docs which we need". The finished docs were deleted then (git keeps them, last in
1666bfcf), and the rest are listed here by what they answer.

## Start here

- **`order-plan.md`**: where the tree stands, what was done (one line a slice, §6), what is still
  open (§7), and the plan for one place for each thing.
- **`layout.md`**: where every folder goes, and what a player is given. The folder tidy, in steps.
- **`../CLAUDE.md`**: the rules every session follows, content work above all.
- **`../README.md`**: what the game is, how to build, run and cook it.

## Code

- **`architecture.md`**: the layers and the arrows between them, the twelve foundations, where a
  new file goes, what is checked.
- **`conventions.md`**: what has one right answer and a silent wrong one: axes, matrices, colour,
  Metal.
- **`../server/README.md`**: the box, the service, the character store, and how the game became
  client and server (phases 1-7, sprints 17-25).

## Making content

- **`cooker.md`**: how an agent makes content in the user's open studio, through the cooker MCP;
  `./mu` and the pipeline underneath.
- **`recipes.md`**: what a new thing of each kind touches today.
- **`materials.md`**: the material audit, and `tools/matcheck.py`.
- **`cook-log.md`**: one line per item cooked, and whether its shots passed.

## Performance and rendering

- **`budget.md`**: the frame's accounts, every measurement that set them, the 2K audit, GPU culling,
  the crowd and the fight, and the sweep.
- **`shadow-probe.md`**: why the shadows shimmered on a walk, and the tests that stay.
- **`grass.md`**: the research and the build of the grass.
- **`spatial-sound.md`**: what ARPGs do with sound, and ours.
- **`claude-bot.md`**: the Claude bot, a player's seat for testing every system.
- **`windows-linux-test.md`**: the Windows and Linux port test.

## Game systems

- **Items:** `jewellery.md` (rings and pendants), `wings.md` (the 1st and 2nd wings, Darkness),
  `second-class-gear.md`, `pets.md` (the Guardian Angel and the Imp), `mount.md` (the Horn of
  Uniria), `kundun-box.md` (the Box of Kundun, the boxes you throw, the Firecracker),
  `mu-tooltip-lines.md` (MU's item card, line by line).
- **Crafting:** `chaos-machine.md`, `refining.md` (jewels: how they drop and refine).
- **Skills and classes:** `skills-dk.md` (the knight, and §0: skills are Diablo 3's shape),
  `skills-dw.md` (the wizard, and the right button every class shares), `mu-scrolls-and-orbs.md`
  (the skill items), `mg-port.md` (the Magic Gladiator).
- **Social:** `party.md`, `guild.md`, `broker.md` (the market).
- **Events:** `golden-dragon-raid.md`; Blood Castle and Devil Square are under Worlds.
- **Quests:** `quest-rewards.md` (the reward ladder), `golden-archer.md` (the Dungeon),
  `devin-quest.md` (Devias), `class-change-quest.md` (Sevina), `lost-tower-quest.md`,
  `lirien-quest.md` (Atlans), `tarkan-quest.md`, `icarus-quest.md`, `aida-quest.md`.
- **The rules against WebZen:** `webzen-audit.md`.

## Worlds

| world | page | state |
|---|---|---|
| Lorencia | none (the first world, built in sprints 2-3) | built |
| Noria | `noria-effects.md` | built |
| Devias | `devias-port.md` (the record, the effects, the blizzard) | built |
| Dungeon 1-3 | `dungeon-port.md` | built |
| Lost Tower 1-7 | `lost-tower-port.md` | built |
| Atlans | `atlans-port.md` | built |
| Tarkan | `tarkan-port.md` | built |
| Icarus | `icarus-port.md` | built |
| Aida | `aida-port.md` | in progress: the six 2005 breeds built, the Bloody four held |
| Blood Castle 1-6 | `blood-castle-port.md` | built |
| Devil Square 1-4 | `devil-square-port.md` | built |

## How we work

From the first plan (2026-09-20), still the rules:

- **A session reads the plan, the open sprint and the code it touches**, not surveys of MU2's
  docs. To take a rule from MU2, read the one `shared/` file concerned.
- **One sprint per session**, each ending with a shot read and a number written down.
- **Rules are traced or marked "invention".** Smaller than 0.75 is allowed; made-up and unmarked is
  not.
- **Look is judged by shot**, in the benches and the studio. A number found there goes into a
  sheet, and a sheet that settles is folded into the cook or the code.
- **A sprint that overdraws its frame account stops and says so** rather than borrowing from spare.
- **The window runs only with `--frames`**, and the log and the PNG are read before reporting.

**The team, and who does what.** The point of the split is that **the one who wrote a thing does
not get to be the one who says it works.**
- **Senior** (the main session): the architecture, the plan, and anything whose right answer is
  not yet known -- what a pass costs, how the layers divide, which numbers carry over. Writes the
  code a sprint turns on, and reviews the junior's work before it lands.
- **QA** (a subagent that did not write the code): reviews against the sentence that proves the
  sprint, runs the thing, reads the log and the shot, and hunts the failure the author would not
  look for. Reports, does not fix. Its verdict is DONE or SENT BACK, overruled only by evidence.
- **Junior** (a subagent): contained work whose answer is known -- a wrong constant, an
  off-by-one, a comment that contradicts its code, a rename. A finding is the junior's when a
  sentence can say what the fixed code must do; one that needs a judgement about cost or
  correctness is the senior's.
- **Report honestly.** Sprint 1 was sent back twice, the second time because a fix was claimed in
  three places and never made.

## Rules for docs

- **Decisions at the top**: what the user picked, in their words, with the date.
- **"Where it stands" is updated in the same commit** as the work it describes.
- **Finished logs are deleted**, not archived: git keeps them. What is still live in one is
  pulled into the doc that stays first, and the commit names where it went.
- **Moving or deleting a doc rewrites its citations in the same commit**, in code comments too.
  Comments cite a deleted sprint as "sprint N" (below), and a foundation as "foundation N"
  (`architecture.md`).

## Planned folders

`layout.md`'s step 3 sorts these pages into five folders, with their citations rewritten in the
same commit, once no other session holds the files that cite them:

- **`plans/`**: `order-plan.md`, `layout.md`.
- **`reference/`**: `architecture.md`, `conventions.md`, `budget.md`, `shadow-probe.md`,
  `grass.md`, `spatial-sound.md`, `claude-bot.md`, `windows-linux-test.md`, `webzen-audit.md`,
  `mu-tooltip-lines.md`, `mu-scrolls-and-orbs.md`.
- **`content/`**: `cooker.md`, `recipes.md`, `materials.md`, `cook-log.md`.
- **`game/`**: the items, crafting, skills, social, events and quests pages above.
- **`worlds/`**: every `*-port.md` and `noria-effects.md`.

## The sprints, one line each

Each had a file under `docs/sprints/` until 2026-10-13: `git show
1666bfcf:MU2_BGFX/docs/sprints/<file>` reads one. Code comments cite them as "sprint N".

| # | sprint | what it built |
|---|---|---|
| 0 | foundations | the layers, CMake, the log, shots, stats, the budget gate |
| 1 | the frame | the glb loader, the material model, the six views |
| 2 | the ground | the height, the tile grid, the blended ground, MU's camera, the sun's split |
| 3 | the town | placements, instancing, chunk culling, cutouts, grass, the cook |
| 4 | figures | body parts, rigs, clips, equipment, monsters through the bone texture |
| 5 | rules I | the tick, pathing, spawns, AI, 0.75's hit and damage, levels, the headless run |
| 6 | the showing | the transparent pass, the landing cue, effect sheets and pools, sound |
| 7 | rules II and the windows | items, drops, the bag, equipment, potions, the shop, the HUD |
| 8a | the lamps | lamps and fires as point lights, glows, MU's flame |
| 8b | the fire burns | bloom, a heat ramp, embers and smoke |
| 8c | the metal | the reflection probe, plate that catches the fire |
| 9 | the ring | MU2's hover outline, migrated off Godot |
| 11 | the monsters fight back | attack skills, the attack actions, the meteor |
| 12 | the figures over the fight | the damage numbers, the user's concept B2 |
| 14 | the shine | +3, +7 and +9 on the body, the ground and the bag |
| 15 | the elf | bows, crossbows, the quiver, her skills and summons |
| 16 | the foundations | layer libraries, `RealmConfig`, stable save keys, spawn after raise |
| 17 | the seam | server phase 1: commands, the `Link`, the mirror realm |
| 18 | the wire | server phase 4: TCP, lockstep, the Hetzner box |
| 19 | many heroes | server phase 2: several players in one world |
| 20 | the world host | server phase 3: every world on one loop, characters kept |
| 21 | the snapshot | the realm as bytes for a newcomer |
| 22 | Go Back!, and nothing the client says | the town portal's return; the audit of what the client decides |
| 23 | the account | server phase 6: the character screen asks the server |
| 24 | the quiet line | UDP ticks, the cushion, the hero ahead |
| 25 | the clear thread | the server's own timing |
