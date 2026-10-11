# Order plan: one place for each thing, and one tool to make it

Written 2026-10-11, at 84940c86. The user: *"we actually need a plan how to make everything make
sense much better"*, and *"build actual instruments for world build, monster building, basically
all in one cooking software where you can work and do any task you need to build game"*.

This page supersedes `docs/structure-plan.md` (2026-10-06) as the plan. That page's evidence
is still good, and its items are cited here by their numbers (M4, S3, ...). Two read-only
audits back this one: the code after the server sprints (17-25), and how content is made
today. Both are summarised below, with their counts.

## 1. Where we stand

### The code

- **The structure plan mostly did not happen.** Sprint 16 landed M1 (libraries), M2 (save
  keys), half of M3 (spawn, no despawn) and S6 (RealmConfig). Everything else is still open:
  - not started: M4, M6, M7, S1, S3, S4, S7-S11;
  - partly done: M5 (MapRow), S2 (Realm::Player) and S5.
- **The hubs grew instead of shrinking.** Lines on 10-06 → today:

  | file | then | now |
  |---|---|---|
  | `play.cpp` | 2654 | 3094 |
  | `realm.h` | 1780 | 2410 |
  | `play.h` | 1479 | 1923 |
  | `desk.cpp` | 1838 | 2176 |
  | `sim_test.cpp` | 10357 | 12913 |

  17 files are over 1500 lines; there were 8. `Play::update` is a single function of 2541
  lines. `PlayMode::frame` is 834 lines and `Desk::update` about 930.
- **Every kind of feature still lands in the same hubs.** `play.h` took 191 commits in two
  weeks: 61 map, 55 monster, 54 effect or sound, 52 item, 31 net and 25 UI.
- **The server is one file.** `server/src/main.cpp` is 1877 lines doing ten jobs: world host,
  sessions, accounts, hello, parties, chat, landing, tick, persistence glue and stats. It has
  no tests.
- **Facts the lockstep depends on are copied.**
  - Raising a realm from a Welcome is written three times: the server, `play_open.cpp` and
    netbot. They must match bit for bit.
  - Prediction from a snapshot is written twice: `remote_link.cpp` and netbot.
  - The rain clock is written twice: the server and `weather.cpp`.
  - The world table is written twice: `sim/maps.cpp` and `brain.h`. They already disagree on
    Lorencia's arrival tile (142,126 vs 138,124).
- **Client networking lives in `game/`.** As a result, `remote_link.cpp` is compiled twice,
  and netbot reaches into the game layer. `tools/` is outside `layercheck`.
- **The client knows maps and breeds by name.** There are 70 `world == "..."` compares in 25
  files, 56 `== kXFigure` branches and 39 figure constants in `play_tuning.h`. Per-breed rules
  sit in 7 C++ lists in `realm_tuning.h`.
- **Folders are flat.** `game/` has 45 files at its root (play, figures, net, audio, saves,
  bench), `sim/` has 64 and `fx/` 91.
- **The docs describe a single-player game:** `README.md`, `architecture.md`, `roadmap.md`
  and `server/README.md` (which still says protocol 4; it is 24).

### The content

- **A fact lives in five or more places.**
  - A gate: `mu.db` gates, `index.py` GATE_BOXES_BY_MAP, `gates.cpp` and `maps.cpp`.
  - A monster: `mu.db`, its recipe, `cook.py` RESPAWN fixes, `realm_tuning.h` and
    `play_tuning.h`.
  - An NPC: `cook.py` FOLK_VERSION075. `mu.db`'s `npc_spawns` table is never read.
  - A world: about 20 per-world dicts in `cook.py`, about 17 in `terrain.py`, and 2 in
    `index.py`, which repeats one of `terrain.py`'s.
- **Five scripts overlap, and each covers a different part of the job:** `cook.py`,
  `cook_one.py`, `cook_batch.py`, `sync.sh` and `sync_one.py`. The order between them carries
  weight and nothing enforces it. Getting it wrong fails silently:
  - meshes cooked before textures come out white;
  - figures cooked before spawns are merged into the index leave the monsters invisible;
  - a new item needs the tables re-cooked in all 12 worlds;
  - a `mu.db` write sits in the WAL until it is checkpointed.

  Those traps are known only from memory files.
- **Only lighting is live.** The sheets reload in 250 ms. Anything else means editing a file,
  running the scripts and relaunching:
  - minutes for an object;
  - a C++ rebuild for any monster, effect or map value.
- **Nothing in the game writes back.** No bench edits a value and saves it.
  `sheets/materials.json` is applied by nothing.
- **Adding one thing costs this much today:**

  | thing | example | cost |
  |---|---|---|
  | monster | Queen Rainer | 7 code files plus about 8 tools |
  | effect | Inferno | 38 files |
  | quest | Tarkan's | 23 files |
  | UI window | the target frame | 23 files |
  | full map port | | about 40 code files and 36 touch points |

## 2. The idea underneath: one thing, one place

The audits show a single cause. **There is no home for "a thing"**, whether a monster, a map,
an item, an effect or a quest. Every new thing gets smeared into lists, dicts and switch arms
across C++, Python and SQL. That is why:

- the hubs grow;
- the scripts need an order that only memory knows;
- an editor has nothing to edit;
- an agent (me) reads 3000-line files and collides with other sessions.

The fix runs in three layers, each standing on the one before:

1. **Content as data, one folder per thing.** A monster is
   `source/monsters/<breed>/` holding its stats, rules, show, sounds, spawns and model. A world
   is `source/worlds/<w>/world.json` holding its rules, show, gates, NPCs and pipeline knobs.
   The cook turns these into what the game and the server read. C++ keeps behaviour, such as
   bespoke bosses and effect code, behind registered hooks. It does not keep facts.
2. **Code with seams, one file per feature.** A feature lands in its own file and registers
   into a list. The hubs stop growing because nothing has to edit them.
3. **One tool, two front ends.**
   - **`mu`**, a command line over a dependency graph, for scripts and for me.
   - **`mu2 --studio`**, the game binary in an editor mode with panels, for the user.

   Both run the same commands and write to the same `source/` files, which git can diff.

## 3. The target layout

### Code

    src/core        unchanged
    src/content     cooked readers (no bgfx: tables, grid, placement, showing; gpu: mesh, texture)
    src/sim/        core/ (realm, body, tick, dice)   rules/ (fight, items, skills, move)
                    systems/ (party, vault, machine, market)   events/ (castle, square, raid, invasion: one Instance each)
    src/net         wire only
    src/client      NEW: link, remote_link, predict (shared with netbot), roster, local save
    src/gfx         unchanged
    src/game/       play/ (Play, the happening handlers, requests, pointer)   figures/   audio/
                    fx/ (+ fx_common.h, the Effect registry)   world/   ui/{widgets,windows}/
    src/studio      NEW: the editor panels; links game + app, never linked into the server
    src/app         modes: lobby, play, bench, studio
    server/src      main (flags + loop), world_host, session, accounts, party, chat, store
    tools/mu/       NEW: the `mu` command (Python), the graph, and the old scripts as its steps
    tools/bot       brain.h split into hand, plan, fight, trade, quest

### Content

    source/worlds/<w>/world.json     rules (MapRules) + show (MapShow) + gates + npcs + pipeline knobs
    source/worlds/<w>/spawns.json    monster spawns (from mu.db, git-diffable)
    source/worlds/<w>/objects/       world object recipes (today's source/world/<w>/)
    source/monsters/<breed>/         breed.json (stats + rules + show + sounds + effects hook) + model
    source/items/ ...                unchanged; stats already in the recipe
    source/quests/<giver>.json       steps, gates, rewards, text, voice; the sim keeps the logic
    sheets/                          live look: lighting, worlds, time, and later materials

**Decided by the user, 2026-10-11: `mu.db` becomes a cook output, built from these json
files.** The question was: I recommend yes, for three reasons:

- json in folders diffs in git, and I can read and edit it;
- no more WAL checkpoints;
- one truth.

`mu.db`'s player tables (accounts, characters, inventory) are not content. They belong to the
server's store.

## 4. The tool

### `mu`, the command (for me, for scripts, and for the studio underneath)

- **`mu make <target>`** runs over a dependency graph: recipe → Blender build → sheets →
  index → sync → cook → tables, for every world that needs them.
  - It knows what is stale by content hash and does only that.
  - It wraps today's scripts as steps rather than rewriting them.
  - Every order trap in §1 becomes an edge in the graph, so it can no longer be forgotten.
- **`mu check`** runs in under 30 s: layercheck (now covering `tools/` too), the size
  ratchet, cookcheck, one-truth checks (a fact defined twice, a db column nobody reads), and
  a new item present in every world's tables.
- **`mu new monster|object|item|world|quest|effect <name>`** creates the folder from a
  template.
- **`mu where <thing>`** lists every file that names it. Over time it should shrink to one
  folder.
- **`mu shot` and `mu compare`** take shots at absolute positions, produce labelled
  before/after sheets with a diff number, and make turntables. These are today's
  studio.py, cook_one and shotcheck behind one name.
- **Every command answers in JSON** and names the source file and line when something is
  wrong.
- **Jobs claim the files they cook,** so two sessions never cook the same thing at once.

### `mu2 --studio`, the editor (for the user)

- It is the game binary with the game's renderer. What you judge is what players see. There
  is no second editor with its own picture.
- **Panels are Dear ImGui on bgfx** (the user's pick, 2026-10-11), for the editor only. The game's own UI stays as it is.
- **It is live.** Recipes, sheets, placements and breed json reload when they change. An
  asset rebuild runs `mu make` in the background and swaps in the result.
- **It writes back.** Moving a tree or changing a breed's speed writes one line in `source/`,
  plus an entry in a journal (`studio/journal.log`) that I can read.
- **It has a control socket.** `mu do "world aida; camera 90 174 30; shot"` drives a headless
  studio. It never opens a window for an agent (see the "never open the client" memory).
- **The panels, in order:**
  1. **World.** Browse, place, move and delete objects; paint ground; set gates and NPC
     spots; edit the world sheet live.
  2. **Monster.** Stats, rules, show, aura and sounds, spawn areas painted on the map, and a
     sim arena button (today's `--arena`).
  3. **Item and material.** Stats, the turntable at +0 to +11, material knobs, and
     `sheets/materials.json` finally applied.
  4. **Effect.** Preview a registered effect on a dummy with its numbers exposed. The code
     stays a one-to-one MuMain port.
  5. **Quest and NPC.** Steps, text, voice, giver placement, and a "play it" button.
- It reuses what exists: the four BenchMode kinds and the browser list, live sheet reload,
  the scripted-input and shot flags, the headless realm, the arena and raid setups, and
  cook.py's functions.

## 5. The order of work

Each sprint keeps the seeded `sim_test` fingerprint, `shotcheck` and the user's saves
unchanged unless it says otherwise. One sprint per session, as `PLAN.md` asks.

| # | sprint | what it does | size | leaves |
|---|---|---|---|---|
| A | **Tidy and truth** | Delete the root scratch files (`a.out`, `parse_mur*`, `test_*.cpp`, the 0 B `mu.db`). Rewrite `README`, `architecture.md` and `server/README.md` for client + server. Mark structure-plan items done. Land the size ratchet (M7) and extend layercheck to `tools/`. Write `docs/recipes.md`, the touch list per kind of thing (S11). | S | the truth written down; the hubs stop growing |
| B | **`mu` and its graph** | `tools/mu`: `make` over today's scripts with the order traps as edges, `check`, `where`, `shot`/`compare`, JSON output. The old scripts stay and are called by it. | M | no more order traps; my loop gets shorter at once |
| C | **One seam for the wire** | `sim::raiseFrom(Welcome)` once, and `src/client/` with prediction shared with netbot. Split `server/main.cpp` into world_host, session, accounts, party and chat. The rain clock and the bot's world table read from `sim`. Server tests for hello, party and store. | M | lockstep facts defined once; the server legible |
| D | **The world manifest** (M5) | `source/worlds/<w>/world.json` takes the cook, terrain and index dicts plus gates and NPCs. MapShow drives the client's systems through a bitset, and the 70 name compares go. `kBloodCastleMap`-style checks become MapRules. | M-L | a new map is data plus art |
| E | **The breed row** (M4 + S4) | Per-breed rules and show move into `source/monsters/<breed>/breed.json`, cooked into the `.mur` and `showing`. Bespoke bosses keep code behind a `behaviour` hook. `mu.db` is built from the json, if the user agrees. | M-L | a plain monster is data plus art |
| F | **The live studio** | `--studio` mode, the control socket, hot reload of recipes, placements and breeds, and write-back with the journal. | M | edit to see in seconds |
| G | **World panel** | Place, move and paint, gates and NPC spots, the world sheet. | M | the user builds Aida in the studio |
| H | **The client hubs** (M6, S3, S9) | `fx_common.h` and the Effect registry. `Play::update` becomes a table of handlers, one file per happening family. `PlayMode::frame` is split. The desk keeps a window list. | L | an effect or window is one file plus one line |
| I | **Monster panel**, then item/material, effect, quest | One panel a sprint. Quest prose moves to data (S10). | M each | |
| — | alongside, when touched | Split `sim_test` (S7) and `brain.h`. Castle, square, invasion and raid each become an `Instance` (S1). Args groups (S8). The folder moves of §3, done in one mechanical commit per layer when no other session is mid-edit. | | |

Why this order:

- A and B pay off the next day and touch no game code.
- C removes the copies that can break lockstep, which is the riskiest debt now that the game
  is an MMORPG.
- D and E are what the studio edits. A panel built before them would edit the old smear.
- H is large, and safest after the registry pattern is proven on worlds and breeds.

**The progress measure** is the touch lists in `docs/recipes.md`, re-counted after each
sprint:

| thing | today | target |
|---|---|---|
| monster | 7 code files | 0 (1 for a bespoke one) |
| bare map | 3 code files and about 36 touch points | 0 |
| effect | 4-5 | 1 file plus 1 line |
| quest | 23 | the json and a test |
| window | 23 | its file plus 1 line |

## 6. Sprint A, as done

Begun 2026-10-11, the user's pick of where to start.

- **The root scratch moved aside**, not deleted (the user's word): `a.out`, `parse_mur*`,
  `parse_perches.cpp`, `test_*.cpp`, `test_load` and the empty `mu.db` are in
  `scratch/old-2026-10-08/`, and `/scratch/` is ignored.
- **`tools/sizecheck.py` is in `checks`.** A `.cpp`, `.py` or `.sh` over 800 lines or a header
  over 1000 fails, unless `tools/sizecheck.allow` lists it; a listed one never grows past its
  size there. It covers `src/`, `server/src/`, `tests/`, `tools/` and `pipeline/`: 50 files over
  the limit, recorded. Proved: `play.cpp` held to 3000 fails at its real 3094.
  `tests/sim_test.cpp` is exempt by name until it is split (S7): within the hour another session
  had added 39 lines of its invasion tests, and every feature's test lands there.
- **The docs say client and server:** `README.md`, `docs/architecture.md` (`net` and `server`
  layers, the writable-realm rule), `server/README.md` (protocol 24, the snapshot, worlds that
  live for ever, the account), `link.h`'s comment, and a status block on `structure-plan.md`.
- **`docs/recipes.md`** is the touch list per kind of thing: the measure the later sprints
  shrink.
- **Not in A, moved to C:** layercheck over `tools/`. It would fail today on netbot's
  `game/remote_link.h`, which sprint C moves into `src/client`.

## 7. Leave alone

These are kept from structure-plan §5, and still right:

- the layer diagram;
- the fixed `step()` order and its dice streams;
- `Request` and `Happening` as plain data;
- `Satchel`, `Vault` and `Machine` as value types;
- the renderer API;
- the cooked binary readers with magic and version;
- effects as one-to-one C++ ports;
- `hud`, `tip` and `describe` long because MU's lines are long.

And from today: the wire (`src/net`), the store, and the live lighting sheets.
