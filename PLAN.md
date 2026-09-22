# MU2 on bgfx: the plan

Written 2026-09-20. MU2 rebuilt as a single player game in C++ on bgfx, Metal on this Mac,
for a frame at 1920x1080 with cast and received shadows, PBR, reflections and SSAO. Worked
in sprints by one agent in one session at a time. This file is the map; each sprint gets a
short file under `docs/sprints/` when it starts and its numbers when it ends.

**`docs/roadmap.md` is where this map stands**, written 2026-09-22: which sprints are done,
the 72-commit stretch after sprint 8c that was worked without one, the gap list checked
against the code, and sprints 9 to 13. Read it before opening a sprint file.

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
4. **One conventions page**, written before any loader: the world is metres with one tile to
   the metre (MU2's own scene scale), the content is 100 units to a tile and is divided on
   the way in, MU's axes are z-up and y-south and are swapped on the way in, which way a row
   runs, handedness, which way a model looks (+z in MU2's build), yaw sign, `height.png` as
   `[y,x]`, matrix layout
   (`bx::mtxFromQuaternion` is the inverse of what it looks like), `uvec4` joints on Metal,
   `gl_FrontFacing`'s sense, sRGB or linear per texture role.
5. **One material model, closed.** Albedo (sRGB), normal (BC5), ORM, emissive, and three
   flags: cutout, two-sided, skinned. Shader variants are those flags and nothing else. MU2's
   material library is mapped onto it at cook time.
6. **Draws are batches from the start.** One instance buffer a frame read by every pass,
   bones in one RGBA32F texture, scenery grouped by model into instanced draws.
7. **Culling is designed, not retrofitted.** 180 fps is 5.5 ms, and Lorencia places 2845
   objects; nothing reaches that by drawing them all and hoping. The rules, in the order
   they are built:
   - **Chunks, not objects.** The map is cut into tile blocks, each with its own bounds.
     Culling tests a chunk, and an instanced draw is what survives. Per-object tests cost
     more CPU than they save GPU at this count, and the CPU is already the ceiling.
   - **The camera and the sun are culled separately.** A chunk behind the camera can still
     cast into it: the shadow pass tests against the split's own box, extruded along the
     sun, not against the camera frustum. Culling the shadow pass with the camera's frustum
     is the bug that removes the shadow of whatever is just off screen.
   - **Ranges per kind.** Grass, leaves, small clutter and lamps each stop at their own
     distance, and stop casting before they stop drawing. MU's camera is fixed and close,
     so this is nearly free and worth more than any frustum test.
   - **Sorted once, not per frame.** Chunk bounds and each kind's range are settled at load;
     a frame walks a flat array and appends to the instance buffer. No allocation, no map
     lookup, no sort per frame.
   - **Measured, not assumed.** The draw count and the culled count both go in the log every
     second, for the camera pass and the shadow pass separately. A culling change that is
     not visible in those two numbers did not happen.
   Occlusion culling is deliberately **not** on this list: MU's camera looks down over a town
   where little hides much, and the cost of a depth readback or a software rasteriser is real.
   It gets considered only if sprint 3's measurement says the frustum and the ranges are not
   enough.
8. **The sim is a library that has never heard of the renderer.** Fixed 20 Hz tick, one
   seeded random source, no clock but its own, no allocation in a step. It speaks to the
   game in two lists: requests in (walk here, attack that, use this), events out (spawned,
   stepped, hit, died, dropped). The game draws events; windows raise requests and redraw.
   That is the mirror rule kept without a wire, and it is what makes point 9 possible.
9. **The sim runs without a window.** `--headless --seed S --ticks N` with a scripted hand
   writes the event log; the same seed gives the same bytes. `tests/` holds seeded logs and
   invariants (nothing walks on a blocked tile, no health below zero, no hit on the dead).
   This replaces Instrument, Audit and the bot, and costs a day if built first.
10. **Layers that do not know each other.** `core` (log, files, maths, clock), `gfx` (bgfx,
   views, materials; no game nouns), `content` (index, cooked formats, tables), `sim`
   (rules; includes `core` and `content` tables only), `game` (crowd, world, effects, HUD,
   sound, benches). `gfx` never includes `game` or `sim`; `sim` never includes `gfx`.
11. **Tables are cooked, not queried.** `cook.py` writes `mu.db`'s rows (monster kinds,
    monster and npc spawns, items, gates) to flat files with a version. No sqlite in the
    game. The save is one versioned file written whole. **Drops and experience are not among
    them and this line used to say they were**: `mu.db` has no such tables, because MU has an
    expression per level rather than a list of numbers. Both are code, traced in
    `docs/sprints/05-rules-i.md`.
12. **Self-contained and pinned.** No symlinks. `tools/sync.sh` copies what `index.json`
    reaches; `bootstrap.sh` pins bgfx/bx/bimg, glfw, cgltf, stb and miniaudio by commit.

## Mipmaps and antialiasing

Found missing on 2026-09-20, when every texture in the first real run logged `mips 1`.

**Mipmaps are not optional and are the bigger of the two.** MU's camera looks down at a town
at a shallow angle, which is the worst case for minification: without a mip chain every
roof, cobble and wall texture aliases into a boil of shimmer the moment anything moves, and
the GPU reads full-resolution texels for pixels a tenth their size, which is bandwidth spent
to look worse. Anisotropic filtering needs the chain too, and it is what a shallow angle
actually wants. The rules:

- **The cook generates them** (sprint 3), into the `.ktx` with the BC7 blocks, because a
  mip chain built once offline is better and free at load. Until the cook exists they are
  generated at load, which is slower to start and identical to look at.
- **Trilinear with anisotropy** on everything the world wears. The anisotropy level is a
  sheet value and is measured, not assumed.
- **Not on everything**: `height.png`, `attributes.png`, `light.png` and the tile grid are
  data, not pictures, and are point-sampled with no mips at all. A mipped attribute grid
  silently averages walkable with blocked.
- **A cutout's alpha needs care in its mips.** Averaging alpha down a chain thins a leaf
  until it vanishes at distance; the cook rescales each level's alpha to hold the coverage
  the top level had.

**Antialiasing.** Nothing is built yet, and the order matters because the three kinds of
aliasing here have three different answers:

- **Texture minification** — mipmaps and anisotropy, above. This is the most visible one on
  MU's camera and MSAA does nothing for it.
- **Geometry edges** — 4x MSAA on the prepass and the shade pass, which share the depth, and
  one resolve before the present. It fits this frame: the shade pass already tests depth
  `EQUAL` and shades once a pixel, so the cost is bandwidth and the resolve rather than
  shading, and on a tile-based GPU the resolve happens in tile memory. SSAO reads a
  single-sample resolve of the prepass, since a half-resolution occlusion term has no use
  for per-sample normals.
- **Cutout foliage** — MU's grass and leaves are alpha-tested, and a plain MSAA does not
  touch a discarded pixel's edge. Alpha-to-coverage does, and it is nearly free once MSAA is
  on. This is what made MU2's grass legible.

The alternative to MSAA is a post-process (FXAA or SMAA at about 0.2 ms, or TAA). TAA is
**not** chosen: MU's art is painted and full of high-frequency detail, the camera is nearly
static, and TAA's smearing and its ghosting behind a walking figure would cost more than the
edges it fixes. MU2 measured its own MSAA plus supersample at about 3 ms in Godot, which is
more than this budget has; 4x MSAA alone, on a forward pass that shades once a pixel, is a
different and much smaller number, and it is measured in sprint 2 before it is kept. If it
does not fit, SMAA at present is the fallback and the `present` account can afford it.

Neither is in the budget above yet. Both get priced in sprint 2, on the ground, where there
is something to alias.

Reflections, stated now so the wrong thing is not built: the base is a sky in closed form in
the shade pass, prefiltered by roughness, occluded by AO. Sprint 8 adds one prefiltered
cubemap per world, baked offline by a bench, and a planar or screen-space pass for water
only. MU2 measured a probe grid at 2.7 ms for a faint sheen on painted art; not repeated.

## Layout

    MU2_BGFX/
      PLAN.md  README.md  CMakeLists.txt  bootstrap.sh  build.sh  run.sh
      docs/conventions.md  docs/budget.md  docs/sprints/NN-name.md
      src/core  src/content  src/sim  src/gfx  src/game  src/app  src/main.cpp
                     (the layers, and the arrows between them: docs/architecture.md)
      shaders/
      sheets/        (lighting.json and the benches' live-reloaded json)
      tools/sync.sh  tools/cook.py
      tests/
      assets/  extern/  build/     (gitignored)

## Sprints

One to three sessions each, one batch commit on main, one sentence that proves it.

| # | sprint | proved by |
|---|---|---|
| 0 | **Foundations.** Layers, CMake, bootstrap, log, shots, stats, budget gate, `conventions.md`, `sync.sh`, a window that clears and times itself — **done 2026-09-20** | `./run.sh --frames 300 --shot 100 --stats s.csv` leaves a PNG, a log and a csv; `--budget` fails when told a false budget |
| 1 | **The frame.** glb loader, the material model, six views: PCSS sun shadow, normal-depth prepass, half-res SSAO and blur, GGX shade with the closed-form sky, ACES present. `--bench model`, `sheets/lighting.json` reloaded live | House01 on a plane, lit, shadowed and occluded at 1080p; a per-view GPU table in the sprint file |
| 2 | **The ground.** `height.png`, the tile grid, `ground_surfaces.json` blended with normal and ORM, `light.png`, attributes, MU's camera and its framed sun split. `--bench ground` | Lorencia's bare land from the play camera, inside the prepass and shade accounts |
| 3 | **The town.** Placements, instancing by model, chunk culling for camera and sun, cutouts, grass, leaves. `cook.py`: BC7/BC5 `.ktx`, flat meshes | all of Lorencia standing, loaded in under 5 s from cooked files; draws and frame written down |
| 4 | **Figures.** Body parts, rig, clips baked flat, crossfade, equipment on dummies, monsters, all instanced through the bone texture. `--bench monster` | a Dark Knight in armour and 30 animated monsters in the town, inside budget |
| 5 | **Rules I.** The tick, walking and pathing on the attribute grid, spawns from the cooked tables, monster AI, 0.75 hit chance and damage, death, respawn, experience and levels, the headless run and its tests | a seeded 10 000-tick hunt that logs the same bytes twice; click to walk and click to attack in the window |
| 6 | **The showing.** The transparent pass, the landing cue and what hangs off it (blood, number, fall, health plate), effect sheets and pools, sound through miniaudio. `--bench effect`. **No skills** — see below; this row used to say "skills one story each" and contradicted the section that decided otherwise | a fight that reads as MU2's does; no allocation per frame in the pools |
| 7 | **Rules II and the windows.** Items, drops, the bag, equipment and its requirements, potions, the shop; the HUD as one draw and one table, a text atlas, pointer and pick | kill, pick up, equip, sell, in windows that redraw on change only; HUD account kept |
| 8 | **Light and look.** Lamps and fires as point lights, water, the baked cubemap, the grade; paired shots against MU2 | the user's judgement of the pairs |
| 9 | **The game whole.** Character creation and select, the save file, Noria and the gate between maps, the `.app` bundle | a packaged app that makes a character, hunts, quits, resumes, and walks to Noria |

After 9 the backlog is MU2's docs read as stories: refining and the chaos machine,
summoning, Devias, the single player events worth keeping.

## The skills are not MU's, and that is decided

Much later, and written down now so nothing is built that has to be undone. The skill
system is **Diablo 3's shape, not MU 0.75's**:

- A skill is **learned permanently** — by a Dark Knight and by an Elf alike — and not
  re-bought, not carried as an item, not lost.
- A learned skill is **dragged onto Q, W, E or R**. Those four are the bar.
- Every skill has a **real cooldown**, and a character cannot use skills continuously. This
  is the part MU has no equivalent of, and it changes how a fight is paced: the sim needs
  per-skill cooldown state on a figure, and the HUD needs to show it running down.

This is the one place the rules leave 0.75 on purpose rather than by omission, so it is
`invention` on a scale the other departures are not, and it belongs in its own sprint after
9 rather than folded into sprint 6's effects. What sprints 5 to 7 must not do is bake in
MU's own assumptions — a skill fired from an item, or with no cooldown — anywhere they
would have to be dug out again.

## Working rules, to keep sessions cheap

- A session reads this file, the open sprint file and the code it touches, not surveys of
  MU2's docs. To take a rule from MU2, read the one `shared/` file concerned.

### The team, and who does what

Three roles. The point of the split is that **the person who wrote a thing does not get to
be the one who says it works**, and that the senior's time goes on the decisions rather than
on the errands.

- **Senior** (the main session). Owns the architecture, the sprint plan, and anything where
  the *right answer is not yet known*: what a pass costs and whether it earns it, how the
  layers divide, which of MU2's numbers carry over and which are Godot's own, and every
  design call in a QA finding. Writes the code a sprint turns on. Reviews the junior's work
  before it lands — QA is a second net, not the first. Answers to the frame budget.
- **QA** (a subagent, always one that did not write the code). Reviews a sprint before it
  lands, against the one sentence that proves it. Runs the thing, reads the log and the
  shot, and hunts the failure the author would not look for. Reports; does not fix. Its
  verdict is DONE or SENT BACK, and the senior does not overrule it by assertion — only by
  evidence.
- **Junior** (a subagent). The contained work where the answer *is* known and only the
  doing remains: a wrong constant, an off-by-one, a comment that contradicts its code, a
  doc that states a rule nothing keeps, a rename, a unit fix. Given the finding, the file
  and the acceptance test, one thing at a time. Not given anything whose scope it would
  have to decide, and not given a defect whose cause is still a theory.

**What goes to the junior and what does not.** A finding is the junior's when a sentence can
say what the fixed code must do. It is the senior's when the fix needs a judgement about
cost, correctness or what the engine ought to be — a resolve that corrupts an edge, a
statistic that is measuring the wrong thing, a pass that has to be redesigned to be worth
its milliseconds. Sending one of those to the junior gets a plausible patch for the wrong
problem, which is more expensive than doing it once.

**The senior reports honestly or the whole thing is worthless.** Sprint 1 was sent back
twice, the second time because a fix was claimed in three places and had never been made.
A review that has to re-derive whether the author is telling the truth costs more than the
review is worth.
- The window may run, always with `--frames`; read the log and the PNG before reporting.
- Look is judged in the benches here, by shot. Numbers found in a bench go into a sheet,
  and a sheet that settles is folded into the cook or the code.
- A sprint that overdraws its account stops and says so rather than borrowing from spare.
- Rules: traced or marked "invention". Smaller is allowed; made-up and unmarked is not.
