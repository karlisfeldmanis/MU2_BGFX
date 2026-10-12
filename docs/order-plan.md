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
| E | **The breed row** (M4 + S4) | Per-breed rules, combat and show move into the breed's recipe, `source/monsters/<Name>.json` (the user, 2026-10-11), cooked into the `.mur` and `showing`; spawns into each world's manifest. Bespoke bosses keep code behind a `behaviour` hook. `mu.db` is built from the json, if the user agrees. | M-L | a plain monster is data plus art |
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

## 7. Sprint B, as done

Begun 2026-10-11 straight after A, on the user's "next".

- **`./mu`** (`tools/mu/`, five small modules) with `make`, `check`, `where`, `shot`,
  `compare` and `cores`. Each takes `--json` and then ends with one object.
- **`mu make`** plans each step with the reason it runs or skips, and `--plan` runs nothing.
  It handles one recipe, `world <w>`, `sounds` and `tables`.

  The traps it closes:
  - a sheet newer than the build forces `--rebuild`;
  - the WAL is checkpointed;
  - the index runs after any build;
  - sync copies index.json whole;
  - textures cook before meshes, and every `.mum` is checked for texture links;
  - an item cooks the wardrobe and every world's tables;
  - a monster's tables cook before its figure;
  - a `jobs.txt` over 60 images is called out.

  Cooks hold a lock per world (`workshop/.mu/`), and every step runs under `nice`.
- **Two staleness rules are narrower than asset.sh's.**
  - Only the pipeline scripts a build runs count, read out of asset.sh itself; an edit to
    `terrain.py` had made every model stale.
  - A clean tracked script counts from its last commit, not its file date.
    `tiled_maps.py`'s date said 10-11 when its content was 09-30's.
- **Cores** (the user: *"we need probably also some setting how many cores to use"*):
  - `./mu cores N` keeps the count per machine in `workshop/.mu/settings.json`. `MU_CORES`
    and `--cores` override it for one run, and the default is 60% of the machine (6 of 10).
  - The count reaches the cook's `--threads`, Blender (a `--threads` wrapper given to asset.sh
    as `$BLENDER`) and torch and numpy (the OMP/MKL/VECLIB variables).
- **Proved on `lorencia/Beer01`:**
  - Blender (21 s), index (41 s), sync, then textures (0 new images), meshes (linked),
    placements and tables, all in 71 s.
  - Run again, everything skipped in 1 s.
  - Touched and rebuilt through the Blender wrapper.
  - `mu make X | head` no longer kills the make when the pipe closes. The first cut did,
    mid-chain.
- **What `mu check` found on its first day:** another session's `realm_raid.cpp` 4 lines past
  its recorded size. `mu where Balrog` names 35 files, the baseline for sprint E.
- **Then the three it lacked** (the user quoted them back: "still missing from mu"):
  - **`mu new object <w>/<ObjectNN>`** writes a recipe from MU's model. It exports the mesh with
    MuExtract and reads its sheets from the `.obj`'s `# texture:` lines; `model_sheets.py`
    cannot read Aida's version-12 models. It decodes each sheet under the world's prefix (read
    off its recipes), takes the cutout from decode_texture's alpha verdict, and reads the
    placements and type from the terrain import. Each material is left `"CHOOSE"`, and
    `mu make` refuses the recipe until one is chosen, naming the library. Tried on
    `tarkan/Object61` (82 placements, type 60), then removed.
  - **`mu make ground <w>`** runs tileset, ground, index (it copies the grids), sync, the cook
    and every world's tables.
    - The terrain import runs only on a world never imported (`--folder N`) or on `--import`.
      The Aida session's `terrain.py` edit had made every world look stale, and an automatic
      re-import would have rewritten Lorencia's tracked source.
    - The cook is `--only ground` on a bare world and `--only textures` on one with objects:
      `--only ground`'s manifest holds the ground alone, and meshes cooked against it are
      white.
    - Run on Dungeon (`ground.py` changed 10-03, its ground was 09-30's): 72 s, then shot at
      238,149 by `mu shot`, which now keeps its own `run.log` (another session's run had
      replaced `mu2.log` mid-read).
  - **The showing cook is 45 s → 0.44 s, not 9 min.** The 9 min was stale: the sheets had a
    skip since 09-28. What remained:
    - the 35 missile sheets were re-compressed every time; they now skip on the sha1 in their
      name;
    - all 278 sounds were rewritten every time; they now skip by a content digest per source,
      plus the cook code's own (`sounds/digests.json`). That is content and not a date,
      because a replaced wav can carry an older date. Proved: one byte changed with its date
      set back a day re-cooks that one; restored, the output is byte-identical.
    - The sound helpers moved to `tools/cook_sound.py` to pay the ratchet. `cook.py` is
      3878 → 3813, and all 278 cooked sounds and `showing.mus` are byte-identical across the
      move.
- **`mu new monster <number> [--db]` and `mu new item "<name>" [--db]`** (the user, again:
  "still not built: mu new for monsters and items"). The facts are copied from the two sources
  every recipe cites, each with its `file:line` (`tools/mu/sources.py`):
  - **A monster's row and spawns** come from OpenMU: Version075 first, then 0.95d, 0.97d and
    Season 6. A block counts only if it names a monster and gives it a level, read from that
    monster's own `CreateNew`. A brace back had reached the previous monster's attack skill.
  - **Its model, scale and weapons** come from MuMain's case for it: `CreateMonster`, or the
    later maps' own files (`GM_Kanturu_1st.cpp`).
  - **An item's stats** come from its OpenMU call, read against the helper's declared
    parameters (the overload by argument count).
  - **An item's model** comes from MuMain's own load map in `ZzzOpenData.cpp`: loops with
    offsets (`MODEL_HELM + i + 10` is HelmElf), named models (`MODEL_KNIGHT_BLADE =
    MODEL_SWORD + 20` is Sword21) and bare file names (`HDK_Sword`). This agrees with all
    156 of mu.db's weapon and armour rows. MU's "number + 1" agreed with only some.
- **Checked against what exists:**
  - 197 of the 212 hand-made weapon, shield and armour recipes come out the same. The 15
    others are `skill: 0` written out versus left out, or hand choices: arrows at durability
    1000, the Elven Shield kept elf-only, three gloves without an attack speed.
  - 50 of mu.db's 110 monster rows equal OpenMU. The rest are recorded choices: WebZen's
    attack rates and respawns, and Flame of Evil's 50 where OpenMU has its generic 150 (the
    sim reads 50 as fire).
  - So `mu new` lists an existing row's differences and never overwrites one.
- **The recipe carries the row and the spawns** (`stats`, `spawns`), as json that mu.db will be
  cooked from (sprint E). Until then, `--db` writes the row when it is absent: spawn ids follow
  the highest, and the WAL is checkpointed.
- **Left to judgement:** monster materials (`CHOOSE`) and the hands (`hands_todo` quotes
  MuMain's weapon lines). Rings, jewels, scrolls and wings stay hand-made: their helpers mean
  other things by their numbers.
- **Tried:**
  - the Berserker (350, Kanturu: Season 6's row, 28 spawns, model 106 at 0.95, the full rig,
    two sheets under `berserker_`), `--db` included;
  - Bone Blade (`HDK_Sword.bmd`) and Crimson Glory (`Shield_18.bmd`, its flare sheet named as
    outside Data/Item).

  Each was then removed, and mu.db was restored byte for byte.
- **Not yet:** a socket to a running game (sprint F).

## 8. Sprint C, as done

Begun 2026-10-11 on the user's "continou".

- **One raise.** `sim::raiseFrom(realm, tables, RaiseOrder, between)` (`sim/cradle`) is the
  sequence: configure, raise, castle, square, `between`, outfit, the kept character.
  `net::raiseOrder(Welcome)` reads one off the frame. The server now fills its Welcome first
  and raises its own realm from it, the mirror raises from the same frame, and netbot does too.
  - The three copies had drifted in shape. The server outfitted a new character *or* restored
    a kept one; the mirrors did both. They were equal only because outfitting with no arms
    spends nothing (`spendFor`).
  - Play's local harness (an arena's breed, the bench's heroes, Noria's roads) is `between`,
    run where it always ran.
- **One rain rhythm.** `sim/rain.h` holds the four spell lengths and the server's `RainClock`.
  The server's copy of the constants, with its comment owning up to the copy, is gone, and
  `game/world/weather.cpp` reads them too.
- **The bot's world table** takes names and arrival tiles from `sim::mapNumbered` and keeps
  what is the bot's: which maps it grinds, and Lorencia's birth tile 138,124, which every
  seeded run starts from (the 10-11 audit took it for drift). The other nine already agreed.
- **Not a duplicate after all:** "prediction in two places" is only the three-call idiom
  snapshot → restore → lookAs. RemoteLink uses it to predict the hero's steps; netbot uses it
  to try a decision on a scratch copy. Left as it is.
- **Proved:**
  - `sim_test`'s whole output against HEAD's, built in a worktree: 17,070 lines, 0 differ;
    7335 checks, 0 failed.
  - Live, on a private port: three netbot runs (new characters, the same kept, then all four
    classes) and two client joins. Every link: "N of the server's hashes agreed with the
    mirror, none disagreed".
  - Builds were private (`scratchpad/build-c`), never the shared `build/`.
- **The server, split.** `server/src/main.cpp` went from 1850 lines to 427, and a script
  (verbatim moves, no retyping) made the rest:
  - `host.h`: shared types, constants, the globals as `inline` variables, every prototype;
  - `world.cpp` (646), `party.cpp` (270), `accounts.cpp` (144) and `chat.cpp` (123).

  It compiled on the first try, with no warnings.
- **`server_test`** (in both `checks`) runs with no socket and covers:
  - the store's round trip;
  - the account key and world names;
  - chat's burst;
  - a world from `raiseWorld` against a mirror from its Welcome, new and kept, 600 ticks each,
    hash-equal at the server's cadence.

  To prove it can fail: a mirror on another seed is told apart. 27 checks.
- **`src/client`**: `Link`, `LocalLink`, `RemoteLink` and `AccountLink` left `game/` as the
  `mu_client` library, under `game`. netbot links it instead of compiling a `game/` file, and
  **layercheck now reads `tools/`**, refusing gfx, game and app there (proved with a probe
  including the renderer and Play).
- **Proved again after each step:** the private build of every target; `server_test` 27/0;
  `sim_test`'s only failure is another session's uncommitted sim work. Live: four bots in one
  party, then the client, every link agreeing with the server.

Sprint C is done. **Next: D, the world manifest.**

## 9. Sprint D, as done (slice D1: MapShow)

Begun 2026-10-11. The user picked the folder (*"keeping source/world/<w>/world.json"*): the
existing one, not §3's `source/worlds/`, so no recipe moved. Then *"do D1"*. D is cut into
slices so each one can be shown before the next: D1 MapShow (client), D2 the pipeline dicts,
D3 gates and NPCs, D4 MapRules.

- **One manifest per world**, `source/world/<w>/world.json`, for all eleven. It has a `name`
  and a `show` block, and `tools/sync.sh` copies it to `assets/world/<w>/`. The citations from
  the code comments it replaces moved with the values, as `note` fields.
- **`game/world/world_show.{h,cpp}`** reads a world's manifest the first time it is asked for,
  keeps it, and fills one `WorldShow`. The application points it at the assets at start.
  A system opens only where its field is set. A world without a manifest gets MU's wind and
  nothing of its own, and the log says so.
- **What moved out of the C++:**
  - **Names:** the display name (`arrival.cpp`'s kPlaces and `minimap.cpp`'s switch) and the
    `places`, the Dungeon's 3 floor boxes and the Lost Tower's 7 (`placeName`).
  - **Sound:** the air loop, whether it plays under a roof, the room and Blood Castle's bed
    (the seven air bools in `play.h`); the steps; swimming; the Dinorant flying (`pets.h`);
    and the music, where it plays and whether it loops (`play_mode.cpp`'s chain, 50 lines
    down to 14).
  - **Weather and land:** the weather kind, peak and jungle bed; the leaves; which floors are
    indoors and the open bridges (`world.cpp`'s `deviasFloors_` and `underground_`); the
    caustic; Noria's road sheets.
  - **Creatures and looks:** the birds' model and Airs (`boidOf`/`airsOf` are gone) and the
    crawlers' (`crawlOf`/`crawlRowOf`); the haze looks and the void-cloud colours.
  - **Systems switched on by data:** bubbles, doors, portal, skulls, sky clouds, desert vents,
    waterfall spray, castle sparks, drawbridge, beacon and traps.
- **Name compares in the client:** 72 in 22 files, down to 5 in 2:
  - `ornaments.cpp`'s four per-object arms (Aida's Object76, the Lost Tower's and Noria's
    lanterns, Blood Castle's Object12/14), which belong in the object recipes (sprint G);
  - `headless.cpp`'s Lorencia birth tile 138,124, the seeded tests' rule (D4).
- **Proved, with nothing the player sees moving:**
  - **Field by field:** `check_show` (scratch) wrote out every choice the C++ made by name at
    8f9b2e1f and read the manifests against it: 721,685 checks, 0 differ. That includes the
    place name of every tile of every map. It fails as it should: a butterfly's flap changed
    is one difference, and a Dungeon 3 box one column wider is 85 tiles.
  - **Shots and logs:** HEAD and HEAD plus this change, each built in its own worktree (other
    sessions' uncommitted work left out of both), shot muted with `--play`, a fixed seed and
    step, at every world's arrival tile.
    - **The logs** match line for line in all eleven worlds, 870 to 1780 lines each: birds,
      weather, leaves, caustics, roads, traps, bridges, town and figures. The only lines that
      differ are two frame timings.
    - **The pictures** differ only where the OS mouse pointer stood: the run reads the real
      pointer (`pointer: tile 144,126` against `173,118`), and the marker under it lands
      elsewhere. Atlans's 0.14 mean is the same as HEAD against itself.
    - Blood Castle's run exits 1 on HEAD too: an existing nest error, "1 monsters had no
      standable tile".
    - No new warnings in the files touched.
- **Not in D1:** `sim/maps.cpp`'s `underground` still means "no weather air" for Atlans,
  Tarkan and Aida. The client no longer reads it, so D4 can rename it or drop it. The
  pipeline's AIRS and CRAWLS in `cook.py` must still agree with the manifests' `boids` and
  `crawls` models; D2 makes the cook read them.

### Slice D2: the pipeline block

On the user's "next". The Aida session agreed to keep out of these files until D2 landed, and
adds Aida's own rows after it (its birds, crawlers and small fires, and a `crawls_from`).

- **The manifest gains `map`**, MU's map number, and a `pipeline` block. Each knob is a value
  or `{"value", "note"}`. The note is the comment that sat inside the old dict's row, lifted
  by a script and not retyped. `source/world/charscene/world.json` is new: the character
  screen's land (map 74) has pipeline knobs and no show.
- **`pipeline/world_manifest.py`** reads the manifests and rebuilds each dict in its old shape,
  through one converter per knob: sets, tuples, integer keys, `range`.
  - **`terrain.py`'s 17 per-map dicts**, from HIDDEN_BY_MAP to WATER_FLOW_BY_MAP, are one call
    each. HIDDEN_TYPES and OPERABLE_TYPES are gone: they were Lorencia's rows under another
    name. STEEP_MAPS stays, because it is the login scene's data folder, not a world.
  - **OPERABLE_BY_MAP was written three times:** in `terrain.py`, in `index.py`, and as
    `cook.py` PERCHES's types. Now it is one `perches` table a world, one object a type
    (pose, turns, leans, tall and the model's comment), and OPERABLE is its keys.
  - **`cook.py`'s AIRS and CRAWLS** read the models from D1's `show.boids` and
    `show.crawls`, so the client and the cook cannot name different birds.
  - **The rest of `cook.py`:** TRAP_MODELS, AIRS_FROM, FLAT_OVER_VOID, LIGHT_REACH,
    OBJECT_LIGHT_DEPTH, GROUNDED_TYPES, LIFTED and CHROMED are knobs.
  - The hubs shrank: `terrain.py` 932 → 791 lines, `cook.py` 3813 → 3706, `index.py`
    3174 → 3138.
- **Proved:**
  - **The values:** every one of the 29 dicts, pickled from HEAD's modules and from these,
    is equal in value and in shape, and the two removed aliases equal Lorencia's rows. It
    fails as it should: one `turns` flipped in Tarkan's perches is caught.
  - **`terrain.py`:** HEAD's (`git archive`) and this one, run on MU's Data for all twelve
    worlds into scratch folders: all 73 files byte-identical; the printouts differ only in
    the output folder's name.
  - **The cook:** `./mu make tables` re-cooked 11 worlds' `.mur`, every one byte-identical.
    `--only placements` for Tarkan and Atlans is byte-identical. The Lost Tower's `.mut` came
    out different, but HEAD's own cook gives the same bytes as this one (c652c2bf): the file
    on disk was stale from an earlier change.
- **Left:** `cook_anchors.py` ANCHOR_KINDS_BY_WORLD (the Aida session is adding to it now);
  gates and NPCs (D3); MapRules (D4).

### Slice D3: gates (D3a)

On the user's "next". The user picked how the sim gets them: **a generated table**, not a cooked
one read at runtime. The server, every mirror and netbot must hold the same gates bit for bit,
and a table compiled into the binary cannot drift between them.

- **Each world's manifest has `gates`:** `exits` (number, box, faces) and `enters` (number, box,
  level, target, `fly`, `sealed`). The 65 rows and their comments were parsed out of
  `sim/gates.cpp`, never retyped. Blood Castle's and Devil Square's off-map talk gates sit on
  the Messenger's and Charon's maps.
- **`pipeline/gates_table.py`** writes `src/sim/gates_table.inc`, and `gates.cpp` includes it
  in place of its two arrays.
  - `./mu make gates` runs it; a rebuild follows by hand.
  - `./mu check` has a `gates` line that fails when the include is not what the manifests
    say.
- **Proved:**
  - **Every lookup, old against new:** HEAD's `gates.cpp` compiled into a renamed namespace,
    run against the generated one: `enterGateAt` on every tile of map numbers 0-127,
    `enterGateNumbered` and `exitGate` for -5 to 2099, and `gateToward` between 17 maps from
    every third tile. 10,844,451 checks, 0 differ. One level changed in Devias's manifest is
    37,854 differences, and `--check` names it.
  - **sim_test:** HEAD and HEAD plus this, built in private worktrees, give the same 16,951
    lines (timestamps stripped): 7339 checks, 0 failed. server_test passes 27 of 27 on both.
  - **Live:** `mu2_server` on port 44521, then four netbots (dk, elf, dw, mg), each "36 of the
    server's hashes agreed with the mirror, none disagreed", and a client join, 12 of 12. The
    server was stopped afterwards.
- **Then the user approved the two questions it left** (2026-10-11):
  - **Aida's gates** (OpenMU Season Six Gates.cs:534-535, 170, 267):
    - Noria's door 120 asks 300 (MU's 130; the user: 'lets ask LVL 300 its a late game
      activity'; 200 for the Magic Gladiator) and lets out on Aida's 113;
    - Aida's 121 back is free (MU's 10) and lets out on Noria's 122.

    `testAidaGates` in sim_test proves it: a level 300 knight walks through, a level 299 is
    told 300, the way back is free, and the tracker points at the door. 12 checks; the rest of
    sim_test's output is unchanged. Live, bots and a client joining in Noria by the door all
    agree with the server.
  - **`pipeline/gate_boxes.py` is gone.** It was a second list of the boxes the index keeps
    open, and it lacked 26 of the sim's. The index now takes every gate box in the manifests
    (`world_manifest.gate_boxes`). Proved first: in all twelve worlds, the tiles the stamping
    pass closes are the same with either list. The extra boxes were never stamped, and Aida's
    two are gates now.
- sim_test's one failure ("twelve townsfolk stand in Devias -- got 13") fails on HEAD too. The
  shared cooked Devias tables already hold the Broker session's uncommitted Kesra.

### Slice D4a: the world table

On the user's "keep working on world manifests", while the NPC table waited on the Broker
session.

- **Each world's manifest has `rules`:** `arrive` (the tile a character comes in on), `home`
  (the town a death goes to), `event`, `floors` and `move_order`. They were `sim/maps.cpp`'s
  kMaps rows, parsed out with their comments.
- **`pipeline/realm_tables.py`** (it replaces `gates_table.py`) writes `src/sim/maps_table.inc`
  as well as the gates, and `maps.cpp` includes it. `./mu make realm` writes both (`gates` still
  works), and `./mu check`'s `realm` line covers both.
- **`MapRow::underground` is gone.** Since D1 nothing read it; it had stood in for "no weather
  air" for Atlans, Tarkan and Aida, which is now the show's `air`, `leaves` and `indoors`.
- The table's order is the M key's walk (`mapAfter`), so it is a field, `move_order`, and not
  the order the manifests are read in.
- **Proved:**
  - **Old against new:** HEAD's `maps.cpp` in a renamed namespace against the generated one:
    `mapOf` and `mapAfter` for every world and five that are not, `mapNumbered` -5 to 199.
    235 checks, 0 differ.
  - **sim_test:** identical to HEAD's (HEAD plus this, in a worktree). server_test passes 27
    of 27.
  - **Live** on port 44521: three netbots and a client joining in Devias, every link "none
    disagreed"; the server was stopped.

### Slice D3b: the townsfolk

- **Each world's manifest has `npcs`:** one object a townsperson (`npc`, `name`, `figure`, `at`,
  `look`, and the comment that stood over the row as its note). `cook.py`'s FOLK_VERSION075 is
  `world_manifest.npcs()`. mu.db's `npc_spawns` (Lorencia only, read by nothing) goes when
  sprint E builds mu.db from the json.
- **The Broker session** had three uncommitted rows in the old literal (its Brokers, npc 900).
  It agreed to the move. The rows were carried into its working copies of Lorencia's, Noria's
  and Devias's manifests, uncommitted, for it to commit as its own.
- **Proved:**
  - The table read from the manifests equals HEAD's: 52 rows on seven maps, every row the same
    tuple.
  - HEAD's cook and this one, each run on every world's tables from a scratch copy, write all
    12 `.mur` byte-identical. The shared cooked tables were backed up first and put back, so
    the Broker's cooked Kesra stands as it did.

### Slice D4b: the map rules, first part

- **Four rules are now fields of `rules`:**
  - `summons: false` (Icarus);
  - `drops`: `feather` (Tarkan, Icarus) and `nova_scroll` (the Lost Tower, Atlans, Tarkan,
    Icarus);
  - `invasion: true` (Lorencia, Devias, Noria).

  Each is a `MapRow` field. `sim::rulesOf(map)` gives a map's row, or the defaults for a map
  not in the table.
- **`featherMap`, `novaScrollMap` and `invasionMap` read them**, so their callers in files the
  Broker session has open did not change. So does `realm_summon.cpp`'s Icarus test. The
  generated table now names its fields (designated initializers).
- **Proved:**
  - The old map tests, verbatim, against `rulesOf` for map numbers 0-299: 1200 checks, 0
    differ.
  - sim_test identical to HEAD's (both built in worktrees), server_test 27 of 27.
  - Live: three netbots and a client, every link "none disagreed".
- **Left, and why:**
  - The firecracker's maps include the later castles 12-17, which have no manifest.
  - The `kBloodCastleMap` and `kDevilSquareMap` checks are the events' own logic: the plan's
    `Instance` (S1), not map rules.
  - Atlans's class treasure shares its test with the Lost Tower's seventh floor.
  - The Icarus flight rules (`realm.cpp`, `realm_items.cpp`) are in files the Broker session
    has open.

### Slice D4c: the firecracker and the class treasure

- **Two more rules:**
  - `drops`: `firecracker` (the Dungeon, the Lost Tower, Devil Square, Blood Castle);
  - `treasure`: `"map"` (Atlans) or `"last_floor"` (the Lost Tower, its travel list's last row).

  `MapRow` gains `firecracker` and `treasure` (`sim::Treasure`). `firecrackerMap` and
  `Realm::treasureGround` read them, so `realm_items.cpp` (the Broker's) did not change.
  `realm_tables.py` refuses a drop or treasure name it does not know.
- **Castles 2-7 are not maps 12-17** (the user, 2026-10-11: 'Drop 12-17'). Every castle runs on
  map 11 (`Realm::setCastle` swaps castle 1's nests' bodies), so the old range's 12-17 was never
  asked. `bloodCastleMap` (11-17) is the Instance's, left as it is.
- **Proved:**
  - The old `firecrackerMap` and `treasureGround`'s map tests, verbatim from HEAD, against
    `rulesOf` for map numbers 0-299: 600 checks, only the six castles 12-17 differ; a changed
    treasure and a misspelt drop are each caught.
  - sim_test's 16,960 lines identical to HEAD's (both built in worktrees), server_test 27 of 27.
  - Live: three netbots and a client, every link "none disagreed".
- **Left:** Icarus flight and Atlans's second-class gear (`realm.cpp`, `realm_items.cpp`, the
  Broker's); `cook_anchors.py` ANCHOR_KINDS_BY_WORLD (the Aida session's).

### Slice D4d: flight and the second class's gear

- **Four more rules:**
  - `flight: true` (Icarus): `Realm::heroBefore` sends home one who cannot fly, `moveItem`
    refuses taking off the last wing, and the travel list refuses the trip without one.
  - `drops`: `first_wings` (Icarus, kIcarusWingOdds) and `second_gear` (Atlans's strongest,
    kAtlansGearOdds).
  - `second_class: true` (Tarkan, Icarus): the second class's gear is in the ordinary pools.
- **Left in code, and why:** Blood Castle 6's second-class gear (the run's castle, the
  Instance's); the quests' map numbers (`quests.cpp`, `realm_travel.cpp` discover): quest
  content, not map rules.
- **Edited in the Broker's working files** with its agreement: only these hunks committed,
  built on HEAD's blobs, so its uncommitted work (which needs its untracked files) stays out.
- **Proved:**
  - HEAD's six map tests, lifted from its source text, against `rulesOf` for map numbers
    0-299: 1800 checks, 0 differ; Tarkan's `second_class` turned off is caught.
  - sim_test's 16,960 lines identical to HEAD's (both built in worktrees), server_test 27 of 27.
  - Live: three netbots and a client, every link "none disagreed".

### Slice D5: the world anchors, as done

- **Sprint D's last per-world tables:** `tools/cook_anchors.py`'s ANCHOR_KINDS_BY_WORLD,
  BRAZIER_BOWLS, SHOWN_LATER and HIDDEN_BY_WORLD are each world's manifest's `pipeline` knobs
  `anchor_kinds`, `brazier_bowls`, `shown_later` and `hidden_models` (`hidden` was taken: it is
  types), each with the comment that stood over its rows as its note; cook_anchors.py reads them
  through `world_manifest.by_world` under the same names, so cook.py did not change. Seven
  worlds (charscene, losttower, bloodcastle, atlans, tarkan, icarus, aida). The kinds' shared
  tables (ANCHOR_KINDS, ANCHOR_LIGHT, SPRAY_KINDS, LIGHT_TWINS) stay: they hold for every world.
- **Proved:** HEAD's cook_anchors.py against the new one, every table row by value, type and
  order (37 rows), 0 differ; one changed kind is caught; cook.py imports; the cook reads these
  only by `.get(world)`, so nothing is recooked.

## 9b. Sprint E, the plan: the breed row

Surveyed 2026-10-11 at 27765619. **Decided (the user, 2026-10-11):** a breed's facts go in its
existing recipe, `source/monsters/<Name>.json` (no new folder; 109 recipes, joined to the
realm by `object_type`), and the spawns go in each world's manifest. `mu.db` becomes a cook
output (decided earlier the same day).

**Where one breed's facts live today:**
- the recipe: model, scale, hands, look (read by `pipeline/index.py` monster_entry);
- `mu.db`: `monster_kinds` (110 rows, 13 combat columns, `respawn_seconds`, `attack_skill`) and
  `monster_spawns` (1473 rows), read only by `index.py`; `npc_spawns` (14) read by nothing;
- the sim: `realm_tuning.h`'s kDropRates (59), kResistances (36), kBosses (11), kPoisoners,
  kBoltBlowers, kEveryBoltBlowers, kChillers, kSplitBlows and `bulkOf`; cook.py's
  RESPAWN_VERSION075;
- the client: `play_tuning.h`'s 42 figure constants, kAuraLights, kEyeTrailRows, bone lists and
  five breed numbers copied from the sim; `play_open.cpp`'s 49 figure-name branches setting
  `Drawn` flags; `crowd.cpp`'s IceQueen01;
- `sounds.json`'s 221 monster events, by label.

**The slices (each proved as D's were: old against new, sim_test diff, a live run):**
- **E1, the sim's rules.** The eight `realm_tuning.h` lists and `bulkOf` become the recipe's
  `rules` (drop rate, resistances, boss, poison, bolts, chill, split blow, bulk), and
  `realm_tables.py` writes `src/sim/breeds_table.inc`. A pure data move.
- **E2, combat.** `monster_kinds`' columns become the recipe's `combat`, each with its citation;
  `mu.db`'s table is written from the json. Ask the user about `respawn_seconds`: read by
  nothing, and it disagrees with the code (Balrog 150 in the db, 10 in C++).
- **E3, spawns.** `monster_spawns` become each world.json's `spawns` (breed, box, count); the
  index reads them; `npc_spawns` goes (the manifests' `npcs` replaced it in D3b).
- **E4, the show.** Figure constants, aura lights, eye trails, bone lists, the `Drawn` flags
  and the copied breed numbers become the recipe's `show`, cooked into the figures; the client
  reads a row, not a name. Proved by shots and run.logs from both builds.
- **Stays code:** the bosses' own behaviour (Hydra, Queen Rainer, the dragons) behind a
  `behaviour` name in `show`/`rules`; the events (square.h, event.h, invasion, raid: S1's
  Instance); quests' kill targets; the traps (`traps.cpp`, a parallel table to look at later).

### Slice E1: the sim's breed rules, as done

- **Each recipe has `rules`:** `drops` (money_rate, max_plus, regen), `resist` (ice, poison),
  `poisons`, `chill_odds`, `bolt` ("some", "every"), `boss`, `split` (parts, melee_first,
  after, every) and `bulk`, with a `note` lifted from the rows' own comments and their world's
  citation. 60 recipes; the Yeti (19), which has no recipe, is in `source/monsters/unbuilt.json`
  (the user, 2026-10-11: 'Keep in a tiny side file') until it is built.
- **`realm_tables.py` writes `src/sim/breeds_table.inc`:** the same nine arrays under their old
  names (kPoisoners, kBoltBlowers, kEveryBoltBlowers, kChillers, kBosses, kSplitBlows,
  kResistances, kDropRates, and kBulks, which replaced `bulkOf`'s ternary), in breed order.
  `realm_tuning.h` keeps every block comment and includes the table once; the lookups sit below
  it, so no caller changed. `./mu make realm` watches the recipes; `./mu check`'s `realm` line
  checks the third table. The generator refuses an unknown key, a bolt it does not know, and a
  breed with rules in two files.
- **Proved:**
  - HEAD's `realm_tuning.h` (in a renamed namespace) against the new one, eleven lookups for
    every breed number -50 to 999: 1050 probed, 0 differ; the Hydra's bulk changed in its
    recipe is caught.
  - sim_test's 16,966 lines identical to HEAD's (both built in worktrees, 7441 checks, 0
    failed), server_test 27 of 27.
  - Live: three netbots and a client, every link "none disagreed".

### Slice E2a: combat out of mu.db, as done

- **Each recipe has `combat`:** level, health, minimum and maximum damage, defense, move,
  attack and view range, move and attack delay, attack and defense rate, attack skill, and
  (for now) `respawn_seconds`, moved row for row from `mu.db`'s `monster_kinds`; 110 recipes,
  the Yeti's in `unbuilt.json`. The Death Tree's `stats` (from `./mu new monster`) became its
  `combat`. Labels stayed the recipes' `mu_name`, which equalled every row's `name`.
- **`index.py` `combat_rows` reads the recipes** (and unbuilt.json), not `mu.db`.
  `./mu new monster` writes `combat` and, with `--db`, only the spawns.
- **`monster_kinds` is dropped from `mu.db`** (the user, 2026-10-11: 'Drop the table'); the
  other eleven tables are unchanged, dump for dump.
- **Proved:**
  - HEAD's `combat_rows` on HEAD's `mu.db` against the new one on the new recipes: 111 rows,
    1665 fields, 0 differ in value or type; one changed number is caught. That is the only
    function of `index.py` that changed, so `index.json`'s `breeds` and the cooked `.mur`
    kinds are the same, and nothing needs a recook.
  - `./mu new monster 307` (the Forest Orc, in a scratch worktree, the export stubbed): a recipe
    with `combat` and `spawns`, 36 spawns in `mu.db`, and `combat_rows` finds it.

### Slice E2b: the dead respawn out, as done

- **`respawn_seconds` is gone** from every recipe's `combat` (and the Yeti's), from
  `index.py`'s COMBAT_COLUMNS, from `./mu new monster`, from `MonsterKind` (`respawnTicks`),
  and from the `.mur`, now version 10. The cook's RESPAWN_VERSION075 (the Bull Fighter's and
  the Budge Dragon's 3 s, which nothing read either) went with it. A breed's respawn is
  `rules.drops.regen` alone (sim/realm_tuning.h kDropRates, realm_fight.cpp's kill).
- **Recooked:** every world's tables, from the shared tree.
- **Proved:**
  - HEAD's cook and this one, each cooking all twelve worlds' tables from HEAD's sources into
    scratch: every kind row (1332) and every byte after the kinds the same but the version and
    the respawn; one changed health is caught.
  - sim_test's 16,966 lines identical to HEAD's, each build reading its own scratch-cooked
    tables (7441 checks, 0 failed), server_test 27 of 27.
  - Live on the version-10 tables: three netbots and a client, every link "none disagreed".
  - The real recook against HEAD's version 9: the same, but Lorencia's world part, which
    9435097e (the Broker's move) changed.

### Slice E3: the nests out of mu.db, as done

- **Each world has `source/world/<w>/nests.json`** (the user, 2026-10-12: 'Sibling nests.json'
  over a block in world.json, which is edited by hand and would have doubled in the Lost
  Tower): `{"id", "breed", "box": [x1, y1, x2, y2], "count"}` one nest a line, in id order.
  1538 nests in eleven worlds, row for row from `mu.db`'s `monster_spawns`; the ids are kept,
  because they order the nests the realm raises. Not `spawns`: placements.json already uses
  that word for townspeople.
- **`world_manifest.py` reads and writes them** (`nests()`, `world_nests`, `format_nests`,
  `world_of_map`); `index.py`'s `spawn_rows` reads `nests()`. `./mu new monster --db` writes
  the new breed's nests into its world's nests.json, and no longer copies its spawns into the
  recipe (the Death Tree's and the Forest Orc's copies are gone). `./mu make` reindexes when a
  nests.json is newer than the index.
- **`monster_spawns` and `npc_spawns` are dropped from `mu.db`** (the second was never read;
  the manifests' `npcs` replaced it in D3b). `mu.db` now holds no monster: accounts,
  characters and their keys, items, messages and gates.
- **A bug fixed on the way:** `new_figure.world_of` still parsed sim/maps.cpp's kMaps literal,
  which D4a moved into the generated maps_table.inc, so `./mu new monster` without `--world`
  found no world. It asks the manifests now.
- **Proved:**
  - HEAD's `spawn_rows` and `combat_rows` (on HEAD's `mu.db`) against the new ones (on the
    nests and recipes), each in its own process: 76 breeds, 1538 nests and 112 combat rows,
    identical in order, value and type; one changed count is caught. These are the only
    inputs of `index.json`'s `breeds` that changed, so nothing is recooked.
  - `mu.db`'s other nine tables unchanged, dump for dump.
  - `./mu new monster 306` (the Death Rider) in a scratch worktree, the export stubbed and no
    `--world`: Aida found by its spawns, a recipe with `combat` only, 39 nests appended to
    Aida's nests.json with fresh ids, and the index sees them.

### Sprint E4, the plan: the client's show

Surveyed 2026-10-12 at ddec22c0. The client keyed a breed's look on its figure name: 45 names in
`play_tuning.h` (`kHydraFigure` and the rest), 40 compares in `play_open.cpp`'s `Play::fit` and
about 20 in `play.cpp`, `play_show.cpp`, `play_pointer.cpp` and `crowd.cpp`; ~20 tables keyed by
name (aura lights, eye trails, bone lists, the Hydra's heads, the arcs, the infernos); and
`play.cpp`'s boss blows by breed number. The slices (the user, 2026-10-12: 'yes'):
- **E4a, tags:** every name compare becomes `has("tag")`, the tags in each recipe's `show`.
- **E4b, named bones:** the bone lists and single bones into `show`, one resolver.
- **E4c, structured shows:** the arcs, burn segments, Hydra heads, claws.
- **E4d, the boss blows:** `play.cpp`'s number checks and kInfernoBlows into a `blow` tag.
- **Stays code:** each show's own drawing; only which breed takes it becomes data.

### Slice E4a: the figure names became tags, as done

- **Each of the 45 figures' recipes has `show.tags`**, one per constant it matched (IronWheel01:
  sands, walk_sands, spreads; the Ice Queen: fans, self_lit, shadowless -- crowd.cpp's two
  literal "IceQueen01"s). `tools/cook.py --only shows` writes them, straight from the recipes,
  into `cooked/figures/shows.json`; `./mu make realm` runs it beside the sim's tables.
- **`FigureBody` has `tags` and `has(tag)`;** `Figures::addMonster`, which every one of the 45
  passes through (each is in the cooked tables' `monsters` alone), gives a body its tags from
  shows.json, read once. Every `x->name == kXFigure` is `x->has("tag")`; the constants' lines
  in play_tuning.h are comments naming the tag, every comment above them kept.
- **Proved:**
  - shows.json against HEAD's constants: the same tags for all 45 figures and none on any
    other; a wrong tag is caught. With every check passing through `addMonster`, `has(tag)` is
    `name == kXFigure` for every body, so the drawing is the same.
  - The C++ diff is the swap alone (and comments); both builds compile; sim_test identical to
    HEAD's (7441 checks, 0 failed), server_test 27 of 27.
  - Live: three netbots and a client, every link "none disagreed", no missing shows.json.

### Slice E4b, first part: the aura lights and eye trails, as done

- **`show.aura` {bone, colour, note} and `show.eye_trail` {right, left, note}** in 16 recipes
  (the Balrog's is its first `show`), kAuraLights' and kEyeTrailRows' rows with their comments
  as notes. They were the last tables in the client keyed by a figure's name.
- **`FigureBody` has `auraBone`, `auraColour` and `eyeTrail[2]`** from shows.json, which the cook
  now writes with them; `Play::fit` resolves the bones as it did, the row now the body's own.
- **Proved:** shows.json's aura and eye-trail rows equal HEAD's two tables for all 16 figures and
  none elsewhere, E4a's tags unchanged; the lookups resolve the same names the same way;
  sim_test identical to HEAD's (7441 checks, 0 failed), server_test 27 of 27; live in Tarkan, whose
  monsters carry the eye trails, three netbots and a client, every link "none disagreed", 0 errors.

### Slice E4b, second part: the bones, as done

- **`show.bones` {role: {names, note}}** in 22 recipes, the names Play::fit looked up out of
  nine play_tuning.h constants and seven literals in its own code: `hand_flame` (4),
  `foot_fire` (2), `burning` (35), `star`, `snort` (the Yeti's breath; the bulls' smok_bone),
  `ember` (the Great Drakan's stars13; the Death Knight's pelvis), `stars` (the Alquamos's
  nine, the Death Tree's six horns), `shade` (the Shadows' twelve joints), `head`, `light`,
  `hands` (the Devil's, Vepar's and Lizard King's links), `eyes` (the Crusts'; the elite bulls'
  top bones). And the Death Tree's `star_tint`. Each note points at the comment by its tag.
- **`FigureBody` has `boneRoles`, `bonesFor(role)`, `named(role, i, bone)` and `starTint()`.**
  Every lookup keeps its loop and order; only the name it compares against moved.
- **Proved:** shows.json's bones and tint equal HEAD's constants and literals for all 22 and none
  elsewhere, earlier fields unchanged;
  sim_test identical to HEAD's (7441 checks, 0 failed), server_test 27 of 27; live in the Lost
  Tower (its Shadows, Death Knight, Devil), three netbots and a client, every link "none disagreed",
  0 errors.
- **Left for E4c:** the Queen's and the Drakan's arc blocks (with the Drakan's stars and blue),
  kHydraHeads with their mouths, kForestOrcClaws' bone pairs and kBurnSegments.

### Slice E4c: the structured shows, as done

- **In five recipes' `show`:** the Queen's `bones.stars` (her head) and `arcs` {pairs, a_frame,
  half}; the Drakan's `bones.stars` (21), `star_tint` (its blue) and `arcs`; the Hydra's `heads`
  (four bones and mouths) and `beam_material`; the Forest Orc's `claws` ([first, last] pairs);
  the Death Beam Knight's `burn` {segments, body, head}, indices into its `bones.burning`.
  Their tunings -- colours, dims, the claws' lengths and light -- stay in play_tuning.h.
- **Play::fit's Queen and Drakan branches became one block:** both now take their stars, tint
  and arcs from their own recipes, the Queen's tint null as before. play_show.cpp reads the
  burn and the Hydra's mouths off the body (`one.figure.body()`).
- **The size ratchet, missed since E4a:** E4a, E4b and E4b's second part had each grown
  `figures.cpp` past its recorded 1510 (to 1554), and the second part `play_open.cpp` past 1167
  (to 1170); their checks' output was read only at its tail. The shows' reading and giving
  moved out to `game/figure_shows.cpp` (`Figures::readShows`, `Figures::addShown`), so
  figures.cpp is 1510 again, and the merged block brought play_open.cpp to 1163.
- **Proved:** shows.json's new rows equal HEAD's tables for all five figures (14 and 4 arc
  pairs, 21 stars, four heads, three claws, 38 segments) and none elsewhere, earlier fields
  unchanged; the moved code is verbatim, the merged block gives the Queen and the Drakan what
  their branches gave; sim_test identical to HEAD's (7441 checks, 0 failed), server_test 27 of
  27; live in Atlans (the Hydra's), three netbots and a client, every link "none disagreed",
  0 errors; sizecheck green for every file here.

### Slice E4d: the boss blows, as done

- **`show.inferno` {bombs, mesh, attack_frame?, first?}** in eight recipes (the Tantallos, Zaikan,
  Beam Knight, Death Beam Knight, both Crusts, both Drakans), kInfernoBlows' rows by breed
  number with their comments. `attack_frame` is the CheckAttackTime kept as its number, so the
  wait is `attackTime(n)` computed as before, not a float written down.
- **`show.blow`** names a boss's Flame of Evil show: `gorgon_ring` (the Death Gorgon),
  `staff_ring` (the Zaikan), `staff_ring_comets` (the Death Beam Knight), `phantom_spirits`,
  `phoenix_spirits`, and `none` (the Hydra, the Tantallos, both Drakans); absent, the Balrog's
  ring and storm (the Balrog, the Golden Dragon). The Phantom Knight's is its first `show`.
- **play.cpp asks the swinger's figure**, not `tables_.kinds[...].number`: the inferno, the
  dispatch, and the Dark Phoenix's bolt in fire (its `phoenix` tag). kHydraNumber,
  kDrakanNumber, kGreatDrakanNumber, kPhantomKnightNumber and kDarkPhoenixNumber are gone.
  Each breed number has one recipe and one figure, so the figure answers as the number did.
- **Proved:** shows.json's inferno and blow rows equal HEAD's table and dispatch for all twelve
  breeds and none elsewhere, earlier fields unchanged; sim_test identical to HEAD's (7441
  checks, 0 failed), server_test 27 of 27; live in Tarkan (its Infernos and staff rings),
  three netbots and a client, every link "none disagreed", 0 errors; sizecheck green.

### Sprint E, as finished

A breed is its recipe now. What the C++ held by breed number or figure name, and mu.db held in
tables, is in `source/monsters/<Name>.json` (and the Yeti's in unbuilt.json, its nests in each
world's nests.json):
- `rules` (E1): drops, resistances, poison, chill, bolts, boss, split blow, bulk, generated
  into `sim/breeds_table.inc`;
- `combat` (E2): the stats; mu.db's monster tables are gone (E2a, E3), and the dead respawn
  with them (E2b, the .mur at version 10);
- nests (E3): `source/world/<w>/nests.json`;
- `show` (E4): tags, aura, eye trails, bones, star tint, arcs, heads, beam material, claws
  (the Aida session added from, to, light and until for the Death Rider), burn, inferno and
  blow, cooked into `cooked/figures/shows.json`.

`./mu make realm` writes both the sim's table and the client's shows. What stays code: each
show's own drawing and its tunings (colours, dims, lengths, cadences) in play_tuning.h, the
bosses' bespoke fights (raid, invasion), the events, quests' kill targets and the traps. A plain
new breed is a recipe and its art; a new kind of show is a tag or a field and its drawing.

## 10. Leave alone

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
