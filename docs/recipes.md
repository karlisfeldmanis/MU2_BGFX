# Recipes: what a new thing touches

Written 2026-10-11 (`docs/order-plan.md` sprint A, structure-plan S11) from two audits of the
tree at 84940c86. Each list is **every place a new thing of that kind has to be written today**,
traced from a recent commit. It is the progress measure of the order plan: after each sprint
the lists are counted again, and a list that does not shrink means the sprint missed.

**Rule for whoever uses this page:** if a recipe here sent you to a place it did not name, or
named one you did not need, fix the list in the same commit.

The order traps are marked **(order)**: a step done out of turn fails silently. **Since sprint
B, `./mu make <Name>` runs the build-to-cook steps of a world object, an item and a monster in
that order and checks the traps after** (`./mu make <Name> --plan` shows what it would do), so
in the lists below the numbered build, index, sync and cook steps are one command. The
hand-written parts -- recipes, mu.db rows, C++ tables -- are still the list.

## A world object (a batch of a map's ObjectNN)

Traced from 84940c86 (Aida's trees) and c571b2cd (ruins and graves).

1. `dotnet run --project tools/MuExtract -- export-obj <Data>/WorldN/ObjectNN.bmd source/world/<w>/ObjectNN.obj`
   (and `export-rig` if it sways).
2. `python3 pipeline/model_sheets.py <bmd>`, then `pipeline/decode_texture.py` per sheet into
   `source/textures/<prefix>_<name>.png`. The prefix (`ai_`, `ad_`, ...) is required: names
   collide across worlds.
3. Write `source/world/<w>/ObjectNN.json` by hand: sheets, sheet_materials, cutout.
4. `tools/asset.sh <w>/ObjectNN --build-only` per kind (Blender, minutes each).
5. `python3 pipeline/index.py source workshop` **(order: `--build-only` skips it)**.
6. `tools/sync.sh --world <w> --only-world`.
7. `tools/cook.py --world <w> --only textures`, then `--only meshes`, then `--only placements`
   **(order: meshes before textures cook white)**.
8. Maybe: the world manifest's `pipeline` block (`hidden`, `grounded_types`, `lifted`,
   `chromed`, `flat_over_void`...; since D2), `tools/cook_anchors.py` ANCHOR_KINDS_BY_WORLD,
   `game/world/ornaments.cpp` for a glow.
9. Shots: `./run.sh --world <w> --at X,Y --frames 240 --shot 120 --shot-path <abs> --mute --save <abs>/demo.json`.

**Since sprint B:** `./mu new object <w>/ObjectNN` does steps 1-3 but the materials, and
`./mu make <w>/ObjectNN` steps 4-7.

**Today:** 1 hand-written json per kind, about 7 tools, 0-3 code files. **Goes to:** `mu make`
(sprint B) and the world panel (G).

## A monster

Traced from ebe25845 (Queen Rainer).

1. `export-rig --actions=all` (without `--actions=all` it has one idle), `export-obj`, decoded
   sheets. Two models into one body: `pipeline/merge_rigs.py`.
2. `source/monsters/<Name>.json` by hand. Read MuMain's code for its effects, sounds and lights
   first.
3. `tools/asset.sh <Name>`.
4. `sqlite3 source/mu.db`: rows in `monster_kinds` and `monster_spawns`, then
   `pragma wal_checkpoint(TRUNCATE)` **(order: a write sits in the WAL)**.
5. `pipeline/index.py source workshop`, then merge that map's spawns into `assets/index.json`
   by hand **(order: figures before the merge leave it invisible)**, then `tools/sync_one.py`.
6. `tools/cook.py --world <w> --only tables` for **every world**.
7. `tools/cook.py --world <w> --only figures --monsters <Name> --threads 4`.
8. C++, 7 files:
   - `sim/realm_tuning.h`: kResistances, kDropRates, kBosses, kPoisoners, kChillers;
   - `game/play_tuning.h`: a figure constant, aura lights, eye trails;
   - `game/play.cpp`, `play.h`, `play_open.cpp` (sounds), `play_show.cpp` (its show);
   - a `game/fx/` pair for its own effect, plus `source/sounds/sounds.json` and
     `cook.py --only showing` (about 9 min).
9. Show it in its world beside its spawn (memory: show new monsters in their world).

**Since sprint B:** `./mu new monster <number> --db` does steps 1-4 but the materials and the
hands, and `./mu make <Name>` steps 5-7.

**Today:** 7 code files, 2 data stores. **Goes to:** `source/monsters/<breed>/` (sprint E) and
the monster panel (I). Code only for a bespoke behaviour.

## A map

Traced from Aida's bring-up (c3d5e637) and Tarkan (about 40 code files over 35 commits);
`docs/aida-port.md` §2 and §6 hold the full touch tables (R1-R25, P1-P11).

1. Bare land:
   - `pipeline/terrain.py <Data>/WorldN <w> source/world N`, after the manifest's `map` and its
     `pipeline` knobs (hidden types, grass, void, light...) are written (since D2);
   - the tile sheets decoded, and `ground.json` by hand;
   - `tileset.py` and `ground.py`, the `gates` row in `mu.db` (checkpoint), index and sync;
   - `cook.py --only ground`, and `--only tables` here and in the neighbours.
   Since sprint B, `./mu make ground <w> --folder N` runs all but the dicts, ground.json and
   the gates row.
2. `source/world/<w>/world.json`: its `name` and `show` block (copy the nearest world's;
   sprint D1). Code: `sim/maps.cpp` kMaps, `sheets/worlds/<w>.json`, maybe the
   `content/ground.cpp` heuristics. Rebuild.
3. Gates: the manifest's `gates` (exits and enters), then `./mu make gates`, `sim_test`, and
   rebuild the server and run `server/deploy.sh`. The index keeps the same boxes open.
4. Air and music: `sounds.json` and the wav, `--only showing`, then the manifest's `air` and
   `music`.
5. Objects (above), then monsters (above). Also:
   - `game/world/ornaments.cpp` glows (by world and model), `cook.py` ANCHOR_KINDS_BY_WORLD;
   - `index.py` EFFECTS. A system of its own is a `game/world/` file, a field in
     `world_show.h`, and the manifest naming it.
   - Birds and crawlers: the manifest's `show` `boids` or `crawls`, which the cook also builds
     (D2); `pipeline.airs_from` when the model is another world's.
6. Travel and the rest: `sim/realm_travel.cpp` kRows, `sim/items.h` map hooks, and the bot's
   `tools/bot/brain.h` kWorlds, which is a copy and already wrong for Lorencia.

**Re-counted after D1 (2026-10-11):** bare land is 1 code file (`sim/maps.cpp`, was 3: arrival
and minimap went into the manifest). For a full port, the air, reverb and music arms and the
name gates of 20 world systems are gone: 72 compares in 22 files, down to 5 in 2. A full port
still touches the sim's tables, gates, travel, the bot, the pipeline dicts and any new
system's own file. **After D2:** `terrain.py`'s 17 per-map dicts, `index.py`'s copy of
OPERABLE_BY_MAP and `cook.py`'s 11 per-world dicts and PERCHES are the manifest's `pipeline`
block: a new map writes no Python. Left: `cook_anchors.py` ANCHOR_KINDS_BY_WORLD and
the NPCs (D3b). **Goes to:** `source/world/<w>/world.json` (sprint D: D2 the pipeline
dicts, D3 gates and NPCs, D4 MapRules).

## An item or weapon

1. The recipe with `stats` and a `mu.db` `items` row (checkpoint).
2. `asset.sh`, then `index.py`, then `sync_one.py`, then `cook_one.py`.
3. `cook.py --world lorencia --only wardrobe --threads 3`, and `--only tables` for **every
   world**.
4. Chrome: `tools/chrome_sheets.py`. Judge it: `tools/studio.py <Name> [--plus 9]`.
5. If it is sold: `sim/market.cpp`. If it does something: `sim/items.cpp`, `game/ui/describe.cpp`.

**Since sprint B:** `./mu new item "<name>" --db` writes the recipe, stats and mu.db row of a
weapon, shield or armour piece; `./mu make <Stem>` does 2-3.

**Today:** 0 code files for a plain item, which is the model the others should copy.

## A skill effect

Traced from Inferno (38 files) and 4ff387d2 (poison smoke, 5 files).

1. `game/fx/<x>.{h,cpp}` and its line in `CMakeLists.txt`.
2. `game/play.h` (include and member), `play_show.cpp` (trigger), `play_tuning.h` (numbers).
   If it opens, gathers or lights: `app/modes/play_mode.cpp` too.
3. Sim, if it is a new skill: `skills.cpp/h`, `realm_skills.cpp`, `realm_tuning.h`, `audit.cpp`,
   `save.cpp`, `game/ui/describe.cpp`, `sim_test`.
4. Art: `source/effects/<x>/`, an EFFECTS row in `pipeline/index.py`, then
   `cook.py --only showing`. Icon: `pipeline/skill_icons.py`. The scroll: an item recipe.
5. Judge it in game (the user judges effects in game): `--arena BREED --arena-learn N --ui-skill F:K`.

**Today:** 4-5 files for the look, about 38 with a new skill. **Goes to:** an `Effect` in its
own file plus one registration (sprint H).

## A quest or NPC

Traced from 630833af (Tarkan's quests, 23 files).

1. `sim/quests.cpp` (the rows **and the dialogue text**), `sim/quests.h` (kQuests, which is the
   save layout), `sim/realm_travel.cpp` unlocks.
2. `game/ui/quest_dialog.cpp`, `ui/speech.cpp`, `ui/tracker.cpp`, `game/play.cpp`.
3. The NPC: a FOLK_VERSION075 tuple in `tools/cook.py`; `mu.db npc_spawns` is not read. Plus an
   npc recipe, `asset.sh` and a figures cook.
4. Voice: `tools/voice.py <giver>`, which reads the text out of `quests.cpp`, then `sync.sh`.
5. `sim_test`, and `--talk NAME --quest-ready` shots.

**Today:** 9 code files plus 2 tools. **Goes to:** `source/quests/<giver>.json` with the sim
keeping the logic (sprint I, structure-plan S10).

## A UI window

Traced from dc8ad39f (the target frame, 23 files).

1. `game/ui/<window>.{h,cpp}` and `CMakeLists.txt`.
2. `game/ui/desk.cpp/h`: routing, and one more term in the pointer OR (`desk.cpp:1033`, about 27
   today).
3. `game/play.h`, `play_pointer.cpp`, `play_show.cpp` for what it reads; `app/modes/play_mode.cpp`
   to draw it.
4. Often `ui/controls.*` and `ui/hud.*`.

**Today:** about 6 hub files beside its own. **Goes to:** a window list in the desk (sprint H).

## A sound

`source/sounds/sounds.json` and the wav, then `./mu make sounds` (the index, the sync and the
showing cook, under a second when little changed). Then a load in `game/play_open.cpp` (57 of them, by hand)
and the call where it plays.

## Lighting and a material

- **Lighting is live:** edit `sheets/lighting.json`, `sheets/time/*.json` or
  `sheets/worlds/<w>.json` while the game or `viewer.sh` runs. It reloads in 250 ms; T steps the
  time of day.
- **A material is not live:** `source/materials/<m>.json` or the recipe's `material_overrides` /
  `sheet_roughness`, then `asset.sh --rebuild` per wearer **(order: a changed sheet needs
  `--rebuild`)**, then `sync_one.py`, `cook_one.py` and `tools/studio.py`.
  `sheets/materials.json` is matcheck's rulings, and nothing applies it.
