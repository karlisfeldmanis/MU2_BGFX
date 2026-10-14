# Order plan: one place for each thing, and one tool to make it

Written 2026-10-11, at 84940c86. The user: *"we actually need a plan how to make everything make
sense much better"*, and *"build actual instruments for world build, monster building, basically
all in one cooking software where you can work and do any task you need to build game"*.

This page superseded the structure plan of 2026-10-06 (`docs/structure-plan.md`, deleted
2026-10-13), whose items are cited by their numbers (M4, S3, ...); those still open are copied
into §7 with their text. Two read-only
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
- **The docs describe a single-player game:** `README.md`, `architecture.md`, the old roadmap
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
  `content/sheets/materials.json` is applied by nothing.
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
   `content/monsters/<breed>/` holding its stats, rules, show, sounds, spawns and model. A world
   is `content/worlds/<w>/world.json` holding its rules, show, gates, NPCs and pipeline knobs.
   The cook turns these into what the game and the server read. C++ keeps behaviour, such as
   bespoke bosses and effect code, behind registered hooks. It does not keep facts.
2. **Code with seams, one file per feature.** A feature lands in its own file and registers
   into a list. The hubs stop growing because nothing has to edit them.
3. **One tool, two front ends.**
   - **`mu`**, a command line over a dependency graph, for scripts and for me.
   - **`mu2 --studio`**, the game binary in an editor mode with panels, for the user.

   Both run the same commands and write to the same `content/` files, which git can diff.

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

    content/worlds/<w>/world.json     rules (MapRules) + show (MapShow) + gates + npcs + pipeline knobs
    content/worlds/<w>/spawns.json    monster spawns (from mu.db, git-diffable)
    content/worlds/<w>/objects/       world object recipes (today's content/world/<w>/)
    content/monsters/<breed>/         breed.json (stats + rules + show + sounds + effects hook) + model
    content/items/ ...                unchanged; stats already in the recipe
    content/quests/<giver>.json       steps, gates, rewards, text, voice; the sim keeps the logic
    content/sheets/                          live look: lighting, worlds, time, and later materials

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
- **It writes back.** Moving a tree or changing a breed's speed writes one line in `content/`,
  plus an entry in a journal (`studio/journal.log`) that I can read.
- **It has a control socket.** `mu do "world aida; camera 90 174 30; shot"` drives a headless
  studio. It never opens a window for an agent (see the "never open the client" memory).
- **The panels, in order:**
  1. **World.** Browse, place, move and delete objects; paint ground; set gates and NPC
     spots; edit the world sheet live.
  2. **Monster.** Stats, rules, show, aura and sounds, spawn areas painted on the map, and a
     sim arena button (today's `--arena`).
  3. **Item and material.** Stats, the turntable at +0 to +11, material knobs, and
     `content/sheets/materials.json` finally applied.
  4. **Effect.** Preview a registered effect on a dummy with its numbers exposed. The code
     stays a one-to-one MuMain port.
  5. **Quest and NPC.** Steps, text, voice, giver placement, and a "play it" button.
- It reuses what exists: the four BenchMode kinds and the browser list, live sheet reload,
  the scripted-input and shot flags, the headless realm, the arena and raid setups, and
  cook.py's functions.

## 5. The order of work

Each sprint keeps the seeded `sim_test` fingerprint, `shotcheck` and the user's saves
unchanged unless it says otherwise. One sprint per session (`docs/README.md`, "How we work").

| # | sprint | what it does | size | leaves |
|---|---|---|---|---|
| A | **Tidy and truth** | Delete the root scratch files (`a.out`, `parse_mur*`, `test_*.cpp`, the 0 B `mu.db`). Rewrite `README`, `architecture.md` and `server/README.md` for client + server. Mark the structure plan's items done. Land the size ratchet (M7) and extend layercheck to `tools/`. Write `docs/content/recipes.md`, the touch list per kind of thing (S11). | S | the truth written down; the hubs stop growing |
| B | **`mu` and its graph** | `tools/mu`: `make` over today's scripts with the order traps as edges, `check`, `where`, `shot`/`compare`, JSON output. The old scripts stay and are called by it. | M | no more order traps; my loop gets shorter at once |
| C | **One seam for the wire** | `sim::raiseFrom(Welcome)` once, and `src/client/` with prediction shared with netbot. Split `server/main.cpp` into world_host, session, accounts, party and chat. The rain clock and the bot's world table read from `sim`. Server tests for hello, party and store. | M | lockstep facts defined once; the server legible |
| D | **The world manifest** (M5) | `content/worlds/<w>/world.json` takes the cook, terrain and index dicts plus gates and NPCs. MapShow drives the client's systems through a bitset, and the 70 name compares go. `kBloodCastleMap`-style checks become MapRules. | M-L | a new map is data plus art |
| E | **The breed row** (M4 + S4) | Per-breed rules, combat and show move into the breed's recipe, `content/monsters/<Name>.json` (the user, 2026-10-11), cooked into the `.mur` and `showing`; spawns into each world's manifest. Bespoke bosses keep code behind a `behaviour` hook. `mu.db` is built from the json, if the user agrees. | M-L | a plain monster is data plus art |
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

**The progress measure** is the touch lists in `docs/content/recipes.md`, re-counted after each
sprint:

| thing | today | target |
|---|---|---|
| monster | 7 code files | 0 (1 for a bespoke one) |
| bare map | 3 code files and about 36 touch points | 0 |
| effect | 4-5 | 1 file plus 1 line |
| quest | 23 | the json and a test |
| window | 23 | its file plus 1 line |

## 6. What was done, one line a slice

The slice logs, every "as done" with its proof, were cut on 2026-10-13 (the user: "get rid of old
docs which is not relevant"); they are in git, last in 67a5aa93 (`git show
67a5aa93:MU2_BGFX/docs/order-plan.md`). Code comments cite them by section and slice: §9 is
sprint D, §9b sprint E, §9c the Cooker (F, G, H), §9e the Instance.

| sprint | status |
|---|---|
| A, tidy and truth | done 2026-10-11 |
| B, `mu` and its graph | done 2026-10-11 |
| C, one seam for the wire | done 2026-10-11 |
| D, the world manifest (M5) | done 2026-10-11 |
| E, the breed row (M4, S4) | done 2026-10-12 |
| the Instance (S1) | castle, square and invasion done 2026-10-12; the raid open |
| F, the live studio; G, the world panel | done 2026-10-12 |
| H, the client hubs (M6, S3, S9) | not started; H0 and H1 were the Cooker's |
| I, the panels | the monster's checklist only |

**Sprint A** (§6). Root scratch moved to `scratch/old-2026-10-08/`; `tools/sizecheck.py` in
`checks` (M7: 800 lines a `.cpp`, 1000 a header, `tools/sizecheck.allow` for the rest, never
growing); README, architecture and server/README rewritten for client and server;
`docs/content/recipes.md` (S11).

**Sprint B** (§7). `./mu` with `make` (over the old scripts, the order traps as edges, `--plan`),
`check`, `where`, `shot`, `compare`, `cores` (60% of the machine by default), `mu new object`,
`mu new monster`, `mu new item` (facts copied from OpenMU and MuMain with `file:line`), `mu make
ground <w>`; the showing cook 45 s to 0.44 s.

**Sprint C** (§8). `sim::raiseFrom` the one raise for server, mirror and netbot; `sim/rain.h` the
one rain rhythm; the bot's world table from `sim::mapNumbered`; `server/src/main.cpp` split into
`host.h`, `world.cpp`, `party.cpp`, `accounts.cpp`, `chat.cpp`; `server_test`; `src/client` (the
Links) out of `game/`, and layercheck reads `tools/`.

**Sprint D** (§9): one manifest a world, `content/world/<w>/world.json`.
- D1: `show` (MapShow), read by `game/world/world_show.cpp`; 72 name compares in the client down to 5.
- D2: `pipeline` knobs; `pipeline/world_manifest.py` rebuilds the cook's, terrain's and index's dicts.
- D3a: `gates`, generated into `src/sim/gates_table.inc` (the user: a generated table, not a cooked
  one); Aida's gates at level 300 (the user: 'lets ask LVL 300 its a late game activity').
- D4a: `rules` (arrive, home, event, floors, move order) into `src/sim/maps_table.inc`;
  `./mu make realm` writes both.
- D3b: `npcs`, the townsfolk.
- D4b-d: map rules as fields (summons, feather, nova scroll, invasion, firecracker, treasure,
  flight, first wings, second gear, second class); castles 2-7 run on map 11 (the user: 'Drop 12-17').
- D5: the anchors' per-world tables as knobs. D6: the spawn gates out of mu.db.

**Sprint E** (§9b): a breed is its recipe, `content/monsters/<Name>.json`.
- E1: `rules`, generated into `src/sim/breeds_table.inc`; the Yeti in `content/monsters/unbuilt.json`.
- E2a: `combat` (mu.db's `monster_kinds` dropped). E2b: the dead `respawn_seconds` gone, `.mur` v10.
- E3: `content/world/<w>/nests.json` (the user: 'Sibling nests.json'); mu.db holds no monster.
- E4a-d: `show` (tags, aura, eye trails, bones, arcs, heads, claws, burn, inferno, blow), cooked
  into `cooked/figures/shows.json`; the client reads a row, not a name.

**The Instance** (§9e, S1): I1 `Realm::event()`; I2 `SquareInstance`; I3 `CastleInstance`; I4 a
player's event fields as one `EventSeat` (snapshot shape 11, protocol 26); J1 `InvasionInstance`.

**The Cooker** (§9c; the user: "we want to build world, monster, item cooker like actual app where
we can see processes with progress and instant visual feedback"). How to use it is
`docs/content/cooker.md`.
- F1: `./mu serve`, the job queue, `@progress`, `./mu jobs`.
- F2: ImGui panels in `mu2 --studio`: Browser, Subject card, Jobs; `src/studio/` a layer.
- F3: the swap after a cook, before and after by `mu compare`; the camera turns only when dragged
  (the user: "dont rotate camera automatically").
- F3b: the scenes, Stage, Home and any world.
- F4: the inspector and `tools/mu/edit.py`, one writer, journalled. The preloader.
- F5: Simulate combat; a monster's make cooks its shows, rules and the game. The show on the bench
  (`game/stand_show.cpp`).
- F6: Place: pick, drag, turn, scale, written back. F7: the control socket, `./mu do`.
- G1: add, duplicate, delete objects. G2: gates and townsfolk's spots. G3: the townsfolk stand.
  G4: a gate added and deleted with its pair (`./mu gate`). G5: the ground painted (`./mu paint`).
- H0: the Cooker as an MCP (`tools/mu/mcp_server.py`); a world scene wears its map's light; the
  Light tab.
- H1: the fight inside the studio; an agent's object edits shown at once; the cores shown; a second
  fight without the load; MU's checklist (`./mu research`); the Blue Golem, the Witch Queen and Hell
  Maine cooked, each with its Cooker fixes.

## 7. Still open

**Sprints not started.**
- **H, the client hubs** (M6, S3, S9): `fx_common.h` and the Effect registry, `Play::update` as a
  table of handlers, `PlayMode::frame` split, the desk's window list.
- **I, the panels** after the monster's: item and material, effect, quest (quest prose to data,
  S10).
- **The raid as an Instance** (J2, J3): not started, the user, 2026-10-12: 'Invasion only'.
- **The folder moves** of §3 and `docs/plans/layout.md`, one mechanical commit a layer when no other
  session is mid-edit.

**The structure plan's open items**, in its words (2026-10-06; the counts are that day's):
- **M3, the other half: despawn.** One `spawn()`/`despawn()` pair keeping `indexOfId_` true, slot
  ints as ids, an assertion that ids stay sorted. Spawning after raise landed in sprint 16;
  despawn did not.
- **M6, an effect registry and shared fx helpers.** About 9 edits in 4 files per effect, and
  copied across `fx/`: an xorshift `roll()` (46 files), hand-built quads (28), a fixed pool with an
  `alive` flag (27), `kReferenceFps`/`kUnit` redeclared (31). Proposal: `fx/fx_common.h` with plain
  helpers (`Dice`, `Pool<T,N>`, `placedQuad`/`beam`, `Trail<N>`, the constants), files migrating
  when next touched; a small `Effect` interface (`open`, `update`, `gather(const FxFrame&)`,
  `lights`, `clear`) and an `EffectSet` that Play registers into, so PlayMode's open, light and
  gather blocks become three loops. Not a data-driven particle system: each effect stays a
  one-to-one MuMain port.
- **S2, Realm is a god object** (partly done: `Realm::Player`). Extract `Shop`, `VaultDesk`,
  `MachineDesk`, `Quests`, `Projectiles` and `Summons`, state and methods together, each when its
  commands are next written.
- **S3, Play is the client's god object** (~176 members, ~241 methods, `Drawn` with 95 fields,
  ~15 hand-written "cast later" queues, 56 hard-coded sound loads). Pull out `CastQueue` (one
  generic `Due<T>`), `PlaySound` (sounds by name), `FolkShow` and `RaidShow`/`InvasionShow`.
- **S5, `Happening` is a fat struct** (partly done: `audience`). Fold the hit bools into
  `hitFlags`, flatten the raid and invasion sub-events into their own kinds, and write it field by
  field with a `kHappeningVersion` (its hash reads struct padding).
- **S7, `tests/sim_test.cpp` is one file** (13,000 lines now, exempt from the size ratchet by
  name). Seven files over one `tests/sim/harness.h`: core (keeps the determinism fingerprint),
  fight, skills, items, town, world, events.
- **S8, `Args` is a grab-bag** (131 flags then, in `core/`, knowing "raid", "castle" and
  "quest"). Group the flags (launch, gfx, script, fixture, bench, diag); each layer parses its
  own group and core keeps only the tokenizer.
- **S9, `PlayMode::frame` is one long function.** Break it into `gatherScene`, `gatherEffects`,
  `lights`, `pointer`, `audio` and `hud`; small once M6 lands.
- **S10, quest prose lives in sim** (`quests.cpp`'s dialogue and voice paths). Move text and voice
  into cooked data; sim keeps steps, gates and rewards. A server should not ship dialogue.
- **Later:** the UI window list with z-order and a press/hover widget (one 15-term OR in
  `desk.cpp` decides which window takes a click); cooking the ground (`.mug`: `ground.cpp` is a
  runtime terrain builder with naming heuristics); `gfx::Texture`/`RenderTarget` wrappers, only
  if a second backend matters.

**Left by the slices.**
- `ornaments.cpp`'s per-object arms (Aida's Object76 and the rest) belong in the object recipes
  (D1).
- `headless.cpp`'s Lorencia birth tile 138,124 is the seeded tests' rule, not a map rule (D1).
- Blood Castle 6's second-class gear and the quests' map numbers stay code (D4d); the traps
  (`traps.cpp`) are a parallel table to look at later (E).
- A realm's snapshot bytes differ run to run even at tick 0 (uninitialised padding in the pods it
  writes whole), so snapshots compare only within one run (I2).
- The studio: an object the index adds is shown and not moved (F6, G1); the bench's standing show
  lacks the Zaikan's foot fires and the Hydra's heads, beams and claws, and it and Play are two
  feeds until sprint H makes them one (after F7); the arena's minimap and event herald still draw
  under the panels (H1); a picture needs a window, as macOS has no window-less bgfx path here (F7).

**Playing together** (the server plan's phase 7; `server/README.md`): drawn players, chat, party,
trade and guilds are built. **PvP and the PK system** are not, and each is a decision first, with
MU's rules in WebZen's source. The events' doors open on one wall clock for everyone
(`sim/config.h`); a run starts on entry, ours (WebZen starts every castle together at hh:31).

## 8. Leave alone

These are kept from the structure plan's §5, and still right:

- the layer diagram;
- the fixed `step()` order and its dice streams;
- `Request` and `Happening` as plain data;
- `Satchel`, `Vault` and `Machine` as value types;
- the renderer API;
- the cooked binary readers with magic and version;
- effects as one-to-one C++ ports;
- `hud`, `tip` and `describe` long because MU's lines are long.

And from today: the wire (`src/net`), the store, and the live lighting sheets.
