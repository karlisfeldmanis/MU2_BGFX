# Structure plan: foundations before the server

Written 2026-10-06, at f88800ec. The user asked for an audit of the client and the realm, and
for the updates the structure must have as the game grows. We are not building the server yet,
but it is coming soon. **The test for every item here: when the server work starts, does this
make it a mechanical job instead of a rewrite?** A second test: can an agent read and edit
each file quickly and safely?

This page is the companion of `docs/server-plan.md`. That page audited the **seam** between
`sim` and `game`: immediate bool methods, ~670 direct Realm reads, one hero built into the
realm, and the client owning time, saves and map changes. Its phases 1-3 remain the plan for
the seam, and nothing here repeats them. This page covers the **inside** of each side, and the
things that make those phases cheap or expensive. Items cite it as SP-phase N.

Two read-only audits back it, with file:line evidence for every count below. They were
written to the session scratchpad and are summarised here.

## 1. The finding underneath everything

The largest files are also the files changed most often (commits touching each between
2026-09-22 and 2026-10-06):

| file | lines | commits in 14 days |
|---|---|---|
| `game/play.cpp` | 2654 | 162 |
| `sim/realm.h` | 1780 | 153 |
| `game/play.h` | 1479 | 152 |
| `app/modes/play_mode.cpp` | 1773 | 136 |
| `game/ui/desk.cpp` | 1838 | 106 |
| `game/play_show.cpp` | 1957 | 101 |
| `sim/realm_fight.cpp` | 1931 | 100 |
| `sim/realm_items.cpp` | 2223 | 89 |

That is no accident. These files are the **hubs every feature has to register in**: `Play`
owns 47 effects and systems by hand, `Realm` owns ~100 members and 318 methods, and
`PlayMode::frame` is one 785-line function that opens, lights and gathers each effect with its
own call. A new monster, effect, event or map therefore edits these files, and so they grow.
They also drive build fan-out, collisions between sessions and slow agent reads.

**So splitting the files is not the cure.** The cure is that a feature lands in its own file
and registers into a list, a table or a manifest, after which the hubs stop growing.

The same hubs are where the client reaches into the realm, so shrinking them *is* server
groundwork.

The other 326 of 376 source files are under 600 lines. The problem is about twenty files, not
the codebase.

### What one feature costs today

Commit f88800ec added one monster (Phantom Knight) and one weapon (Dark Breaker):

- **sim:** 3 hand-written C++ tables in `realm_tuning.h` (`kBosses`, `kResistances`,
  `kDropRates`).
- **client:** 6 files. A boss branch in `play.cpp`, a new queue struct in `play.h` with its
  own tick loop in `play_show.cpp`, constants in `play_tuning.h`, and an enum plus switch arms
  in `fx/held_lights`.
- **The weapon needed no sim code at all.** The cooked `items` row was enough. That is the
  model to copy for monsters and maps.

A new effect costs about 9 edits in 4 files. A new map is a grep hunt: about 85 map-name
strings in 24 client files, plus 32 map-id checks in sim.

## 2. Must have: the foundation

In this order. None of these changes behaviour, and all of them pay off in single player
today.

### M1. CMake libraries per layer (S)

- **Problem:** one executable with 255 listed sources. The sim/content/core source list is
  **copied four times** (mu2, sim_test, bot, raid), so a new sim file must be added in four
  places. Layering is checked by a script but not by the linker.
- **Proposal:** static libraries, each linking only the layers below it:
  - `mu_core`;
  - `mu_content_data` (tables, grid, reader; no bgfx);
  - `mu_sim`;
  - `mu_content_gpu` (mesh, texture, ground, cooked);
  - `mu_gfx`;
  - `mu_game`.

  Every executable links libraries, not lists.
- **Server:** `mu2_server` later becomes `mu_core + mu_content_data + mu_sim + net.cpp`, a
  ten-line target. The linker then refuses a sim file that grows a bgfx dependency.

### M2. Saves keyed by stable ids, not table positions (S)

- **Problem:**
  - Quests are saved **by array position** (`save.cpp:198-203`), and a demo quest already sits
    in the middle at index 17.
  - `learned` and `found` are bitmasks **by table row** (`realm_skills.cpp:66,80`), so
    inserting a skill or travel row silently reassigns them.
  - `learned` is read back through a double (`save.cpp:169`), so bits above 53 are lost, and
    `skills.h:454` already expects more than 64 skills.
- **Proposal:**
  - Quests saved by a stable quest id.
  - Learned skills and found rows saved by their MU *number*, as lists.
  - The reader accepts the old format once.
- **Server:** this is the format characters.db inherits in SP-phase 3. Cheap now, a migration
  later.

### M3. Bodies may be added after raise (S)

- **Problem:**
  - `indexOfId_` is built once at the end of `raise()` (`realm.cpp:290`).
  - Every later body (summon, invader, raid minion, raider) is a pre-reserved slot kept as an
    `int` index.
  - `Body& hero = bodies_[0]` is held across all of `step` (`realm.cpp:888`).
  - A `push_back` after raise would leave `find()` blind and every held reference dangling.
    The invariant exists only as a comment.
- **Proposal:**
  - One `spawn()`/`despawn()` pair that keeps `indexOfId_` true.
  - Slot ints become ids.
  - An assertion that ids stay sorted.
- **Server:** this is the precondition for SP-phase 2. A player joining a realm *is* a spawn
  after raise.

### M4. One source of truth per fact: per-breed rules as cooked columns (M)

- **Problem:**
  - Per-breed rules live in eight C++ lists (resistances, drop rates, bosses, poisoners,
    chillers, bolt blowers, split blows, bulk).
  - Meanwhile mu.db's `respawn_seconds` is read into `MonsterKind::respawnTicks` and **never
    used**, and it disagrees with the code: Balrog is 150 in the db and 10 in C++. The 10 is
    the user's WebZen pick from 2026-10-01, but nothing stops the next reader trusting the
    db.
- **Proposal:**
  - Columns on `monster_kinds`, carried by `content::MonsterKind`.
  - A `source` column keeps the per-row citation (`WZO Monster.txt:56`) so "absolute 0.75
    replica" loses nothing.
  - Delete or repopulate the dead column.
  - After this, a plain new monster needs **zero sim C++**.
- **Server:** server and client must read one table under one build stamp (SP-phase 6). Facts
  written into C++ on one side are how they drift.

### M5. One map manifest (S-M)

- **Problem:**
  - Every world system decides for itself whether it runs (`if (world != "losttower")
    return;` in skulls, lava_smoke and others).
  - Air profiles sit in `play_open.cpp:33-44`, music in `play_mode.cpp:1053-1090`, display
    names in `ui/arrival.cpp`, weather in `weather.cpp:142`.
  - In sim, `kBloodCastleMap` is tested in 7 files, Icarus's grounding rule sits inside
    `step()`, and map ids are defined in four headers.
- **Proposal:** one row per map in two halves:
  - **`MapRules`** (sim): no summons, no auras, needs flight, home on death, feather drops.
  - **`MapShow`** (client): display name, music, air, weather, leaves, caustics, and a
    `systems` bitset that World opens from.

  Systems drop their name gates. Start it as a C++ table in `maps.cpp` (one place), and cook
  it in SP-phase 3, where the server needs map number, arrival point and home.

### M6. An effect registry and shared fx helpers (M)

- **Problem:**
  - About 9 edits in 4 files per effect. `play_mode.cpp` reaches into Play 283 times: a 30-call
    open block, an 18-copy light chain and a gather block.
  - Copied across `fx/`: an xorshift `roll()` (46 files), hand-built quads (28 files, 179
    lines), a fixed pool with an `alive` flag (27 files), and `kReferenceFps`/`kUnit`
    redeclared (31 files).
- **Proposal:**
  1. `fx/fx_common.h` holds plain helpers with no virtuals: `Dice`, `Pool<T,N>`,
     `placedQuad`/`beam`, `Trail<N>` and the constants. Files migrate when they are next
     touched.
  2. A small `Effect` interface (`open`, `update`, `gather(const FxFrame&)`, `lights`,
     `clear`) and an `EffectSet` that Play registers into. PlayMode's three blocks become
     three loops.
  3. Typed members stay, so a cast is still a direct call.

  **Not** a data-driven particle system: each effect is a one-to-one port of MuMain's code, and
  that fidelity is the point.
- **Server:** `clear()` becomes real in SP-phase 3, when a `MapChanged` from the server
  rebuilds the picture without rebuilding the mode.

### M7. A file-size ratchet in `checks` (S)

- **Problem:** nothing stops the hubs growing, and the cost lands on every reader: an agent
  pages a 2,600-line file in chunks, needs a unique anchor in it for an edit, and collides
  with the other session editing the same hub.
- **Proposal:**
  - `tools/sizecheck.py` in the `checks` target. A `.cpp` over **800** lines or a header over
    **1000** fails, unless it is on a frozen list of today's offenders.
  - A listed file may shrink but not grow past its recorded size.
  - Tests and `tools/cook.py` get the same treatment.

  Like `layercheck`, a rule that is enforced instead of reviewed. Comments count. They run
  40-50% of the big headers, and that house style is kept.

### And then: SP-phase 1, a window at a time

Once M1-M7 are in, the next foundation is server-plan's phase 1: commands applied on the tick
instead of immediate bools, and the UI reading a `View` instead of the Realm. §3's S2 and S3
are how it is done without a big bang.

## 3. Should have: done alongside SP-phases 1-2

| # | problem (evidence) | proposal | size |
|---|---|---|---|
| S1 | **Events leak into generic code.** About 340 lines of Blood Castle live in `realm_move.cpp`, the walking file. `kill()` carries castle, raid, invader and warden branches. `step()` calls `castleTick`/`invasionTick`/`raidTick` unconditionally. `Body` has warden, summoner and raider fields on every body. | An `Instance` interface (`raise`, `step`, `onKill`, `onDeath`, `allows`) that the realm holds a list of, built from the map at raise. Do BloodCastle first, then Raid, then Invasion. Each event's state moves into its instance. Phase 7's shared event schedule needs the castle to be a unit someone else opens. | M-L |
| S2 | **Realm is a god object:** 158 public and 160 private methods, ~100 members, and 15 sim headers included, so a raid constant rebuilds 28 files, UI included. Some state is already confined to one file: vault, raid party, traps, projectiles. | Extract `Shop`, `VaultDesk`, `MachineDesk`, `Quests`, `Projectiles` and `Summons`, **state and methods together, each when its phase 1 commands are written** and not before. The per-player ones become phase 2's `Player`. `Satchel`/`Vault`/`Machine` already show the pattern. | M each |
| S3 | **Play is the client's god object:** ~176 members and ~241 methods. `Drawn` has 95 fields, mostly per-breed flags. About 15 hand-written "cast later" queues each have their own struct and tick loop. 56 hard-coded sound loads. 22 files include `play.h`. | Pull out `CastQueue` (one generic `Due<T>`), `PlaySound` (sounds by name), `FolkShow` (townsfolk, wisps, guard speech) and `RaidShow`/`InvasionShow`. **Do it during phase 1**, which rewrites Play's reads anyway. | M |
| S4 | **Per-breed visuals are C++:** `play_tuning.h` is a content table (38 figure constants, ~60 breed names), with ~50 `== kXFigure` branches. | A `breed_show` section in the cooked `showing.mus`: flags, bone lists, aura, sounds and the boss blow. Bespoke monsters (Hydra, Queen Rainer, the dragon) keep code behind a `behaviour` enum. New breeds go in the table and old ones migrate when touched. Do the same for held lights per weapon. | L, gradual |
| S5 | **`Happening` is a fat struct:** 13 bools that only `Hit` uses, Raid and Invasion sub-events packed into `a`, no version, and a hash that reads struct padding. | Keep it plain data. Fold the hit bools into `hitFlags`, flatten the sub-events into their own kinds, and add an explicit field-by-field `write()` with `kHappeningVersion`. Add `audience` in phase 2. This is the wire format of phase 4. | S |
| S6 | **Configuration lives in rule headers.** The castle runs on the 2-minute TEST CLOCK (`event.h:21`, "Back to 3600"). `kQuestTable` is the one mutable global, flipped by `enableQuestDemo()`. Raid size defaults sit in `realm.h`. | A `RealmConfig` handed to `raise()` (castle period, demo quest, raid size). Make the quest table `const`. Two realms in one process, as the phase 3 world host runs, must not share a switch. | S |
| S7 | **`tests/sim_test.cpp` is 10,357 lines** in landing order. `testCastLock` alone is 1,602 lines. One failure fails everything, and a raid edit recompiles all of it. | Seven files over one `tests/sim/harness.h`: core (keeps the determinism fingerprint), fight, skills, items, town, world, events. Phase 2's two-hero test and phase 4's lockstep test need somewhere to go. | M |
| S8 | **`Args` is a grab-bag:** 131 flags in one flat struct in `core/` that knows "raid", "castle" and "quest". | Group the flags: launch, gfx, script, fixture, bench, diag. Each layer parses its own group, and core keeps only the tokenizer. The script and fixture groups are phase 1's GM commands. | S-M |
| S9 | **`PlayMode::frame` is one 785-line function.** | Break it into `gatherScene`, `gatherEffects`, `lights`, `pointer`, `audio` and `hud` steps. This is small once M6 lands; most of the length is effect plumbing. | S |
| S10 | **Quest prose lives in sim:** 581 lines of dialogue strings and voice paths in `quests.cpp`. | Move text and voice into the cooked `messages` table. Sim keeps steps, gates and rewards. A server should not ship dialogue. | M |
| S11 | **An agent cannot tell what a new feature touches.** | `docs/recipes.md`: the exact touch list for a new monster, item, map, effect, window and event. Each M/S item that lands shortens a list. That shrinking list is the progress measure for this whole page. | S |

## 4. Later

- **UI window list with z-order and a press/hover widget.** Which window takes a click is one
  15-term OR in `desk.cpp:815-826`, and each window redoes hover and press by hand. Do this
  **after** phase 1, when every window's signature changes to `(const View&, Requests&)`
  anyway, or 35 windows get touched twice.
- **Folder moves.** For `game/`, sort into `play/`, `scene/`, `audio/`, `world/systems/` and
  `ui/{widgets,windows}/`. For `sim/`, sort into `core/`, `rules/`, `systems/` and `events/`.
  Do it as one mechanical commit when no other session is mid-edit, ideally straight after M1
  while the CMake lists are open anyway. On its own it buys only legibility. The castle code
  leaving `realm_move.cpp` (S1) is the one move worth doing early.
- **Cook the ground** (`.mug`). `ground.cpp` (1826 lines) is a runtime terrain builder with
  naming heuristics, and the one asset without a versioned binary. Do it when a new map breaks
  a heuristic.
- **Gates, trap spots, shop stock, the travel list and bags into the db.** mu.db already has a
  `gates` table with 6 rows, which `kEnters`/`kExits` duplicate.
- **`gfx::Texture`/`RenderTarget` wrappers** for the ~60 real bgfx calls outside gfx. Only if a
  second backend matters.

## 5. Fine as it is: leave alone

- The layer diagram and `layercheck`. Sim has no `rand()`, `getenv` or `std::time`.
- The `realm_*.cpp` split as a first cut. The problem is the header and the shared state, not
  the `.cpp` boundaries.
- The fixed `step()` order and the 19 per-subsystem dice streams, which keep one feature's
  draws from moving another's seeded log.
- `Request`, and `Happening` staying plain data: a variant of 52 structs buys nothing for a
  log that has to cross a wire as bytes.
- `Satchel`, `Vault`, `Machine`, `Router`, `CastleRun` and the market as value types. That is
  already the target pattern.
- Items saved by (group, number), cooldowns by skill number, rules keyed on MU numbers.
- `rules.cpp`, the skill rows, `kPowers` and the event choreography as C++. They are behaviour
  with source citations, not content.
- The renderer API and its file split, the `gfx::Effects` batcher and `effect_mesh`.
- All six cooked binary readers check a magic and a version.
- The world-system shape (`open/update/gather/shutdown`), apart from the name gates.
- UI rebuilds only on a change (`Sheet ==`), and the drawing helpers in `controls`/`panel`.
- `hud.cpp`, `tip.cpp` and `describe.cpp` are long because MU's lines are long, not because
  they are badly shaped.
- Rebuilding the mode on travel, a free reset, until phase 3.

## 6. Order of work

| sprint | items | what it leaves |
|---|---|---|
| A: scaffolding | M1, M2, M3, M7, S7, S11 | the build, saves, body ids and tests ready for many players; growth gated; no behaviour change |
| B: one truth | M4, M5, S6 | a plain monster or a new map is data plus art, with no C++ hub edits |
| C: the client hubs | M6, S9 | an effect is one file plus one registration; `play_mode.cpp` loses most of its length |
| D: SP-phase 1 begins | S1 (castle), S2 + S3 for shop, vault and machine, S5 | the first windows on commands; the castle as an `Instance` |

Sprints A to C keep the shot references and the seeded `sim_test` fingerprint unchanged, which
is how each one proves it moved nothing.

**The MVP, cut from sprint A plus S6 at the user's word (2026-10-06), is
`docs/sprints/16-the-foundations.md`:** layer libraries, `RealmConfig`, stable save keys,
spawning after raise, and the size ratchet.
