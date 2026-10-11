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
8. Maybe: `cook.py` dicts (FLAT_OVER_VOID, GROUNDED_TYPES, ANCHOR_*), `terrain.py`
   HIDDEN_BY_MAP, `game/world/ornaments.cpp` for a glow.
9. Shots: `./run.sh --world <w> --at X,Y --frames 240 --shot 120 --shot-path <abs> --mute --save <abs>/demo.json`.

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

**Today:** 7 code files, 2 data stores. **Goes to:** `source/monsters/<breed>/` (sprint E) and
the monster panel (I). Code only for a bespoke behaviour.

## A map

Traced from Aida's bring-up (c3d5e637) and Tarkan (about 40 code files over 35 commits);
`docs/aida-port.md` §2 and §6 hold the full touch tables (R1-R25, P1-P11).

1. Bare land:
   - `pipeline/terrain.py <Data>/WorldN <w> source/world N`, after its per-map dicts are added;
   - `index.py` GATE_BOXES_BY_MAP;
   - the tile sheets decoded, and `ground.json` by hand;
   - `tileset.py` and `ground.py`, the `gates` row in `mu.db` (checkpoint), index and sync;
   - `cook.py --only ground`, and `--only tables` here and in the neighbours.
2. Code: `sim/maps.cpp` kMaps, `game/ui/arrival.cpp` kPlaces, `game/ui/minimap.cpp` mapName,
   `sheets/worlds/<w>.json`, maybe the `content/ground.cpp` heuristics. Rebuild.
3. Gates: `sim/gates.cpp` kExits and kEnters, `sim_test`, then rebuild the server and run
   `server/deploy.sh`.
4. Air and music:
   - `sounds.json` and the wav, `--only showing`;
   - the air flag in `game/play_open.cpp` (by name);
   - the reverb arm in `play_sound.cpp`;
   - the music arm in `play_mode.cpp`.
5. Objects (above), then monsters (above). Also:
   - `game/world/ornaments.cpp` glows, `cook.py` ANCHOR_KINDS_BY_WORLD;
   - `index.py` EFFECTS and a new `game/world/` system if it has one;
   - every system that gates itself by `world == "..."`: 70 such compares in 25 files today.
6. Travel and the rest: `sim/realm_travel.cpp` kRows, `sim/items.h` map hooks, and the bot's
   `tools/bot/brain.h` kWorlds, which is a copy and already wrong for Lorencia.

**Today:** 3 code files for bare land, about 40 for a full port. **Goes to:**
`source/worlds/<w>/world.json` (sprint D).

## An item or weapon

1. The recipe with `stats` and a `mu.db` `items` row (checkpoint).
2. `asset.sh`, then `index.py`, then `sync_one.py`, then `cook_one.py`.
3. `cook.py --world lorencia --only wardrobe --threads 3`, and `--only tables` for **every
   world**.
4. Chrome: `tools/chrome_sheets.py`. Judge it: `tools/studio.py <Name> [--plus 9]`.
5. If it is sold: `sim/market.cpp`. If it does something: `sim/items.cpp`, `game/ui/describe.cpp`.

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

`source/sounds/sounds.json` and the wav, then `pipeline/index.py`, then
`cook.py --only showing` (minutes). Then a load in `game/play_open.cpp` (57 of them, by hand)
and the call where it plays.

## Lighting and a material

- **Lighting is live:** edit `sheets/lighting.json`, `sheets/time/*.json` or
  `sheets/worlds/<w>.json` while the game or `viewer.sh` runs. It reloads in 250 ms; T steps the
  time of day.
- **A material is not live:** `source/materials/<m>.json` or the recipe's `material_overrides` /
  `sheet_roughness`, then `asset.sh --rebuild` per wearer **(order: a changed sheet needs
  `--rebuild`)**, then `sync_one.py`, `cook_one.py` and `tools/studio.py`.
  `sheets/materials.json` is matcheck's rulings, and nothing applies it.
