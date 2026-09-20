# MU2 on bgfx: the plan

Written 2026-09-20. MU2 rebuilt as a single player game in C++ on bgfx, Metal on this Mac,
for a frame at 1920x1080 with cast and received shadows, PBR, reflections and SSAO. Worked
in sprints by one agent in one session at a time. This file is the map; each sprint gets a
short file under `docs/sprints/` when it starts and its numbers when it ends.

## Decided on 2026-09-20

| question | answer | what follows |
|---|---|---|
| server | **none; single player** | no socket, protocol, mirror or mirror clock. The simulation is in the process. Parties, guilds, trade and chat between players are out of scope |
| rules | **fresh, smaller, in C++** | written new as MU4's `sim.cpp` was, taking from 0.75 what one player needs. Smaller in scope, not looser: every number is still traced to OpenMU Version075, MuMain or `mu.db`, and every departure is marked "invention". `MU2/shared` is read one file at a time as a worked example, never ported wholesale |
| renderer | **from scratch** | MU4 is reference only. Its README's frame table and trap list are read before sprint 1, and the traps go into `docs/conventions.md` so they are not paid for twice |
| platform | **this Mac, Metal** | one shader target, BC7/BC5 textures, no abstraction for backends that do not exist. GLFW window, 1080p backbuffer |
| studio | **bench modes here; Godot retired for judging the picture** | `--bench model|monster|effect|ground` draws one thing under the game's light, reloads its json sheets live, and writes shots and a log. No editor UI |
| content | **MU2's pipeline and `build/` kept** | `index.json`, the glb and png, and `mu.db` remain the source. A cook step here turns them into what the engine loads. Nothing is read from `MU2/` at run time |

## Foundations: what the first two sprints must get right

Cheap now, a rewrite later.

1. **The whole frame before any content.** Shadow, prepass, SSAO, blur, shade, present exist
   at 1080p before the first map loads. Every later sprint adds content to a complete frame
   and is priced against it. No pass is bolted on at the end.
2. **The review loop before the game.** `--frames N --shot K --shot-path` (absolute), a log
   with every asset loaded and one frame line a second, GPU time per view from bgfx's view
   stats, `--stats file.csv` with median and 99th percentile. Vsync off for every number,
   short 1080p runs. A sprint ends with a shot read and a number written down.
3. **A budget with accounts.** 5.5 ms GPU at 1080p over Lorencia's town. Opening split:
   shadow 1.0, prepass 0.7, SSAO and blur 0.5, shade 2.3, HUD and present 0.5, spare 0.5.
   CPU 3 ms, of which the sim 0.5. `--budget` exits non-zero when an account is overdrawn.
4. **One conventions page**, written before any loader: metres per tile, handedness, which
   way a model looks (+z in MU2's build), yaw sign, `height.png` as `[y,x]`, matrix layout
   (`bx::mtxFromQuaternion` is the inverse of what it looks like), `uvec4` joints on Metal,
   `gl_FrontFacing`'s sense, sRGB or linear per texture role.
5. **One material model, closed.** Albedo (sRGB), normal (BC5), ORM, emissive, and three
   flags: cutout, two-sided, skinned. Shader variants are those flags and nothing else. MU2's
   material library is mapped onto it at cook time.
6. **Draws are batches from the start.** One instance buffer a frame read by every pass,
   bones in one RGBA32F texture, scenery grouped by model into instanced draws, in chunks
   culled against the camera and against the sun's split separately.
7. **The sim is a library that has never heard of the renderer.** Fixed 20 Hz tick, one
   seeded random source, no clock but its own, no allocation in a step. It speaks to the
   game in two lists: requests in (walk here, attack that, use this), events out (spawned,
   stepped, hit, died, dropped). The game draws events; windows raise requests and redraw.
   That is the mirror rule kept without a wire, and it is what makes point 8 possible.
8. **The sim runs without a window.** `--headless --seed S --ticks N` with a scripted hand
   writes the event log; the same seed gives the same bytes. `tests/` holds seeded logs and
   invariants (nothing walks on a blocked tile, no health below zero, no hit on the dead).
   This replaces Instrument, Audit and the bot, and costs a day if built first.
9. **Layers that do not know each other.** `core` (log, files, maths, clock), `gfx` (bgfx,
   views, materials; no game nouns), `content` (index, cooked formats, tables), `sim`
   (rules; includes `core` and `content` tables only), `game` (crowd, world, effects, HUD,
   sound, benches). `gfx` never includes `game` or `sim`; `sim` never includes `gfx`.
10. **Tables are cooked, not queried.** `cook.py` writes `mu.db`'s rows (monsters, spawns,
    items, drops, experience) to flat files with a version. No sqlite in the game. The save
    is one versioned file written whole.
11. **Self-contained and pinned.** No symlinks. `tools/sync.sh` copies what `index.json`
    reaches; `bootstrap.sh` pins bgfx/bx/bimg, glfw, cgltf, stb and miniaudio by commit.

Reflections, stated now so the wrong thing is not built: the base is a sky in closed form in
the shade pass, prefiltered by roughness, occluded by AO. Sprint 8 adds one prefiltered
cubemap per world, baked offline by a bench, and a planar or screen-space pass for water
only. MU2 measured a probe grid at 2.7 ms for a faint sheen on painted art; not repeated.

## Layout

    MU2_BGFX/
      PLAN.md  README.md  CMakeLists.txt  bootstrap.sh  build.sh  run.sh
      docs/conventions.md  docs/budget.md  docs/sprints/NN-name.md
      src/core  src/gfx  src/content  src/sim  src/game  src/main.cpp
      shaders/
      sheets/        (lighting.json and the benches' live-reloaded json)
      tools/sync.sh  tools/cook.py
      tests/
      assets/  extern/  build/     (gitignored)

## Sprints

One to three sessions each, one batch commit on main, one sentence that proves it.

| # | sprint | proved by |
|---|---|---|
| 0 | **Foundations.** Layers, CMake, bootstrap, log, shots, stats, budget gate, `conventions.md`, `sync.sh`, a window that clears and times itself | `./run.sh --frames 300 --shot 100 --stats s.csv` leaves a PNG, a log and a csv; `--budget` fails when told a false budget |
| 1 | **The frame.** glb loader, the material model, six views: PCSS sun shadow, normal-depth prepass, half-res SSAO and blur, GGX shade with the closed-form sky, ACES present. `--bench model`, `sheets/lighting.json` reloaded live | House01 on a plane, lit, shadowed and occluded at 1080p; a per-view GPU table in the sprint file |
| 2 | **The ground.** `height.png`, the tile grid, `ground_surfaces.json` blended with normal and ORM, `light.png`, attributes, MU's camera and its framed sun split. `--bench ground` | Lorencia's bare land from the play camera, inside the prepass and shade accounts |
| 3 | **The town.** Placements, instancing by model, chunk culling for camera and sun, cutouts, grass, leaves. `cook.py`: BC7/BC5 `.ktx`, flat meshes | all of Lorencia standing, loaded in under 5 s from cooked files; draws and frame written down |
| 4 | **Figures.** Body parts, rig, clips baked flat, crossfade, equipment on dummies, monsters, all instanced through the bone texture. `--bench monster` | a Dark Knight in armour and 30 animated monsters in the town, inside budget |
| 5 | **Rules I.** The tick, walking and pathing on the attribute grid, spawns from the cooked tables, monster AI, 0.75 hit chance and damage, death, respawn, experience and levels, the headless run and its tests | a seeded 10 000-tick hunt that logs the same bytes twice; click to walk and click to attack in the window |
| 6 | **The showing.** The landing cue and what hangs off it (blood, number, fall, health bar), effect sheets and pools, skills one story each, sound through miniaudio. `--bench effect` | a fight that reads as MU2's does; no allocation per frame in the pools |
| 7 | **Rules II and the windows.** Items, drops, the bag, equipment and its requirements, potions, the shop; the HUD as one draw and one table, a text atlas, pointer and pick | kill, pick up, equip, sell, in windows that redraw on change only; HUD account kept |
| 8 | **Light and look.** Lamps and fires as point lights, water, the baked cubemap, the grade; paired shots against MU2 | the user's judgement of the pairs |
| 9 | **The game whole.** Character creation and select, the save file, Noria and the gate between maps, the `.app` bundle | a packaged app that makes a character, hunts, quits, resumes, and walks to Noria |

After 9 the backlog is MU2's docs read as stories: more skills, refining and the chaos
machine, summoning, Devias, the single player events worth keeping.

## Working rules, to keep sessions cheap

- A session reads this file, the open sprint file and the code it touches. No subagents, no
  surveys of MU2's docs. To take a rule from MU2, read the one `shared/` file concerned.
- The window may run, always with `--frames`; read the log and the PNG before reporting.
- Look is judged in the benches here, by shot. Numbers found in a bench go into a sheet,
  and a sheet that settles is folded into the cook or the code.
- A sprint that overdraws its account stops and says so rather than borrowing from spare.
- Rules: traced or marked "invention". Smaller is allowed; made-up and unmarked is not.
