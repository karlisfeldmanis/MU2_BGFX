# The foundations

Prepared 2026-10-06 from `docs/structure-plan.md`, at the user's word: *"prepare MVP must have
updates which we definitely need to do"*. The server is not being built yet. This sprint is the
short list that a server cannot do without, and that **gets dearer every day it waits**,
because saves pile up, files grow and features keep landing in the hubs.

Not in this sprint, though still wanted (structure-plan M4-M6 and every S item): per-breed
rules as cooked columns, the map manifest, the effect registry, and server-plan phase 1's
commands. They make features cheaper. These five make the server *possible* without a
migration or a rewrite.

**The rule for the whole sprint: nothing the player sees moves.** Every step must end with all
three of these unchanged:

- `sim_test`'s determinism fingerprint;
- `shotcheck`'s three scenes;
- the user's existing saves, which must load to the same hero.

A step that cannot hold all three is not part of this sprint.

## The five

### 1. One library per layer (structure-plan M1), S

**Done 2026-10-07.** `mu_core`, `mu_content_data`, `mu_sim`, `mu_content_gpu`, `mu_gfx` and
`mu_game` are static libraries, each source listed once. `mu2`, `sim_test`, `cooked_test`,
`placement_test`, `bot`, `raid` and `sound_test` link them. Checked in a private build:
`sim_test`'s output identical line for line (6568 checks, the standing 17 failing);
`libmu_sim`, `libmu_content_data` and `libmu_core` hold no bgfx, bimg or glfw symbol; `mu2`'s
exported symbols identical but one, `slab::draw`, which nothing calls and the archive now
leaves out; `cooked_test`'s 4 failures are the same 4 as before (content counts).
Content split by what each file touches: `cooked`, `showing`, `missiles` and `placement` have
no bgfx and went to `mu_content_data` with `tables` and `grid`; `mu_content_gpu` is `texture`,
`mesh` and `ground`, and carries `extern/` (cgltf) for everything above it. Sim compiles once
instead of four times, so a clean build is shorter.

- **Why definitely:**
  - The server target is `core + content_data + sim + net`. Today it cannot be written
    without a fifth copy of the sim source list.
  - The list is already copied four times (mu2, sim_test, bot, raid), so a new sim file
    missed in one copy breaks a tool.
- **Do:** in `CMakeLists.txt`, add these static libraries, each linking only what is below it:
  - `mu_core`;
  - `mu_content_data`: `tables`, `grid`, the reader, with **no bgfx** (check that before the
    split);
  - `mu_sim`;
  - `mu_content_gpu`: `mesh`, `texture`, `ground`, `cooked`, `showing`, `missiles`,
    `placement`, sorted by whether each touches bgfx;
  - `mu_gfx`;
  - `mu_game`.

  `mu2`, `sim_test`, `cooked_test`, `placement_test`, `bot` and `raid` link libraries.
  Explicit source lists stay, once each.
- **Done when:**
  - `checks` and `shotcheck` are green;
  - `mu_sim` links with no bgfx symbol in it;
  - a clean build takes no longer than before.

### 2. Settings out of the rule headers (structure-plan S6), S

- **Why definitely:**
  - The world host steps every map's realm in one process. A mutable global (`kQuestTable`,
    flipped by `enableQuestDemo()`) is shared by all of them.
  - The castle's 2-minute TEST CLOCK (`event.h:21`) is a constant that ships unless someone
    remembers it.
- **Do:**
  - Add `sim::RealmConfig`, handed to `raise()`: castle period, opens-at, entry window, the
    demo quest, raid size and players.
  - `kQuestTable` becomes `const`. The demo quest is a row the config switches on, not a
    mutation of the table.
  - The debug flags fill the config in `app/` instead of poking globals.
  - The default config is the real game, an hourly castle.
  - The test clock moves to a flag (`--castle-period 120`), so testing keeps working.
- **Ask the user first:** does the castle go back to hourly by default now?
- **Done when:**
  - `grep` finds no non-const global in `src/sim`;
  - the fingerprint is unchanged with the config set to today's values.

### 3. Saves keyed by what a row is, not where it sits (structure-plan M2), S-M

- **Why definitely:** every save written today grows the migration later. Today:
  - Quests are an array in table order (`save.cpp:198-203`), with a demo quest already in the
    middle at index 17.
  - `learned` is a bitmask by skill table row and `found` by travel row, so inserting any row
    silently changes what a character knows or has found.
  - `learned` goes through a double (`save.cpp:169`), so bits past 53 are lost, and
    `skills.h:454` expects more than 64 skills.
- **Do:**
  - **Quests get a stable key.** `QuestRow` has no id today, so add `const char* key` (`"marlon"`,
    `"peia"`, …) and save quests as `{key: [state, counts, at, done]}`.
  - **Travel rows get a key** as well, and `found` is saved as a list of keys.
  - **`learned` is saved as a list of MU skill numbers.** That is how cooldowns are saved
    already (`save.cpp:379`).
  - **The reader takes the old format once** and the writer writes only the new one. The demo
    quest's index is excluded from the old-format mapping.
  - **New test, `save_test`:** a fixture save in today's format goes in `tests/fixtures/`. It
    is read, written, read again, and asserted to give the same `HeroRecord`. Inserting a dummy
    quest row in the test table must not change the result.
- **Done when:**
  - `save_test` passes;
  - the user's `saves/` and `characters/` load to the same level, bag, quests and skills, read
    on a **copy** (memory: runs take a scratch save).

### 4. Bodies may join after raise (structure-plan M3), S

- **Why definitely:** a second player arriving *is* a body added to a running realm.
  Server-plan phase 2 cannot start without it. Today:
  - `indexOfId_` is built once at the end of `raise()` (`realm.cpp:290`). Ids come from one
    counter shared with items on the ground (`realm_items.cpp:1075…2182`), so the table is
    sparse by design.
  - Every later body is a pre-reserved slot, kept as an `int` index: `summonSlot_`,
    `invaderSlot_`, `raiderSlots_`, `minionSlots_`.
  - `Body& hero = bodies_[0]` is held across all of `step()` (`realm.cpp:888`).
- **Do:**
  - Add `Realm::spawn(Body)` and `despawn(id)`. They are the only writers of `bodies_` after
    raise, and they keep `indexOfId_` true by growing it to the new id.
  - Slot ints become body ids, looked up through `find()`.
  - Anything holding a `Body&` across a call that could spawn takes it again after the call.
  - Pre-reserving stays (it is why the fingerprint holds), but it now goes through `spawn()`.
  - **New test:** spawn a body mid-run, `find()` it, step 1000 ticks; the hero's reference
    paths still find the hero.
- **Done when:**
  - the new test passes;
  - the fingerprint is unchanged, because the same ids are drawn in the same order.

### 5. A size ratchet in `checks` (structure-plan M7), S

- **Why definitely:** it is the only item that stops the problem growing back.
  - Eight files are over 1,500 lines.
  - The four largest hubs took 136-162 commits each in two weeks.
  - Agents read them in chunks and collide on them.
- **Do:**
  - `tools/sizecheck.py` runs in the `checks` target, beside `layercheck.py`.
  - A `.cpp` over 800 lines or a header over 1,000 fails, unless it is listed in
    `tools/sizecheck.allow` with its size on the day the check landed.
  - A listed file may shrink, and the list is rewritten when it does, but it may never grow
    past its recorded size.
  - The check covers `src/`, `tests/` and `tools/*.py`.
  - It lands **last** in the sprint, so it records sizes after steps 1-4.
- **One consequence to accept:** a feature that must grow a hub has to move something out of
  that hub first. That is the point, but it makes the next few features slower until
  structure-plan M5/M6 land.
- **Done when:** `checks` is green, and a test edit adding 10 lines to `play.cpp` fails it.

## Order and size

1 → 2 → 3 → 4 → 5, one commit each, each verified against the three-part rule above. All five
fit one sprint. Steps 3 and 4 are where the care goes; steps 1, 2 and 5 are mechanical.

Collisions: steps 1 and 5 touch `CMakeLists.txt` and the `checks` target. Step 3 touches
`save.cpp`, `quests.*` and `travel.*`. Step 4 touches `realm.cpp`, `realm_raid.cpp`,
`realm_summon.cpp` and `realm_invasion.cpp`. Before each, check `git status` for another
session's work in those files, and commit through a private index if one is mid-edit.

## After this

In structure-plan order:

- **M4:** per-breed rules as cooked columns, which also settles the dead `respawn_seconds`
  against Balrog's WebZen 10.
- **M5:** the map manifest.
- **M6:** the effect registry.
- Then server-plan phase 1, a window at a time.
