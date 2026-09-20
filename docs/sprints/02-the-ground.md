# Sprint 2: the ground

**Proved by:** Lorencia's bare land from MU's own play camera at 1080p, inside the prepass
and shade accounts.

**Landed** 2026-09-20, awaiting review. Planned from the data rather than from a guess.

## What the data turned out to be

Read before writing anything, and it changes the sprint.

**`lorencia_ground.glb` is a terrain mesh that is already built.** 14 MB, one mesh, **44
primitives**, 262 144 vertices and **131 072 triangles** — which is 256 × 256 tiles × 2,
one quad per tile with its own four unshared vertices. It is already in **metres** and
already has rows running **−z** — `min [0, 0, −256]`, `max [256, 3.825, 0]` — so the
conventions page's scale and axis rules are satisfied by the file itself. No heightfield
needs building from `height.png` to draw the land.

Each primitive is one **surface pair**, and the material names say which:
`TileGrass01__TileGround01` is that base under that overlay, and a name with no `__` is a
base with no overlay (9 of the 44). The nine slots are in `lorencia.json`'s `tile_slots`.

The vertex attributes carry the rest:

- **`TEXCOORD_0` is in tiles, not in [0,1]** — it runs 0..256 across the map. Each half of
  a surface multiplies it by its own `repeat` from `ground_surfaces.json` (0.25 or 0.5), so
  one texture covers four tiles or two.
- **`COLOR_0` is `VEC4` float and is two different things.** `rgb` (0.167..0.527) is MU's
  baked `TerrainLight` per vertex, and it **multiplies the albedo, before any lighting and
  with no factor** — which is what glTF says a vertex colour does and what MU2's own
  `GroundSource` does. `a` (0..1) is the weight MU painted from base to overlay, before the
  height blend bites into it.

> **The three figures above were wrong in this file until the review.** They were the
> accessor bounds of *primitive 0 alone* — a 590-triangle scrap — read as the whole mesh's.
> The error reached the code: `vs_ground_depth.sc` justified the ground's cheapness in the
> shadow pass with "Lorencia rises only 2.2 m", when it rises 3.825 and its steepest
> single-tile step is 3.36 m over one metre, a 73-degree face.
- There is **no `TANGENT`**, and none is needed: the ground's UVs are world-axis-aligned, so
  the tangent frame is analytic in the shader.

`ground_surfaces.json` holds 44 entries keyed by the same index, each with a `base` and an
`overlay` half (albedo, normal, ORM, `repeat`, `relief`, `water`), 27 distinct images
between them. **Ten surfaces carry `water: true`** — the moat and the shore. Water is not
built this sprint; those surfaces draw as ordinary ground and are listed as owing.

## What this sprint has to build

1. **A ground vertex layout and a ground shader.** The ground genuinely does not fit the
   closed material model: it blends *two* full material sets by a vertex weight, and it
   carries a baked light. That is a second shader, not a flag, and `conventions.md` gains a
   sentence saying so rather than the model quietly growing a fourth flag.
2. **The loader**: `lorencia.json` → the grids and the surface table; `ground_surfaces.json`
   → 44 surface pairs; the glb → 44 batches with position, normal, uv-in-tiles and colour.
3. **MU's own camera**, which is measured and not chosen (`client/core/Walk.cs`):
   fov **55°**, pitch **−48.5°**, yaw **45°**, distance **8 m**, focus **1.5 m** up the
   body, near 0.05, far 1200. Distance and fov only mean anything together.
4. **The grids on the CPU**: `height.png` (one byte a tile × `height_factor` ÷ 100) and
   `attributes.png`, so the camera can stand on the land and sprint 5's sim has its walkable
   test. Point sampled, never mipped.
5. **`--world lorencia`** as a mode beside `--bench model`.

## What is deliberately deferred

- **Water** (10 surfaces). Drawn as ground for now, and it will look wrong where the moat is.
- **Grass, leaves and every placed object.** `lorencia.json` has 2845 of them; that is
  sprint 3, and the draw count is meaningless until they are there.
- **Culling.** 44 draws for the whole map, always submitted. Whether that needs chunking is a
  measurement this sprint takes and sprint 3 acts on — the ground is one draw per surface,
  so frustum culling it means cutting each primitive into tile blocks, and 131 k triangles
  may simply be cheaper to draw than to sort.

## The two things most likely to go wrong

Both are things sprint 1's bench could not have caught, which is why they are written down
before the code:

- **The ground is the first single-sided surface in the project.** Every material in MU2's
  build is `doubleSided`; this glb's are `doubleSided: false`. If the handedness or the
  winding is wrong, the land is invisible or inside out, and there is nothing else to blame.
- **The shadow's texel snap has never been tested with a moving camera.** Sprint 1 ran
  `--still` throughout, and the snap's sign bug was invisible for exactly that reason. The
  ground under a moving camera is the first thing that will show a crawling shadow edge.

## Measured

`./run.sh --frames 600 --world lorencia`, Release, vsync off, 1080p, 4x MSAA, **136 draws**
(44 surfaces x 3 geometry passes, plus 4 screen passes), 131 072 triangles, 570 frames after
warmup, at `b34e5bc`. Three runs of each, alternating, **no `--shot`**.

| | still camera | camera moving |
|---|---|---|
| **frame (mean wall ms)** | 2.393, 2.422, 2.363 → **2.39** | 2.430, 2.306, 2.463 → **2.40** |
| fps | 418 | 417 |
| the same, measured independently by review | **2.161** (n=6) | **2.153** (n=6) |
| gpu frame (reported, counts waiting) | — | 2.30 |
| budget | kept | kept |

Against a 5.5 ms frame. **Still and moving are not distinguished** — which is not the same
as equal, and the weaker statement is the true one. Our three runs each land 0.01 ms apart;
the review's six pairs put moving 0.155 ms above still with three pairs flat and three
positive. Both sit inside a machine spread that reaches 2 ms between identical segments, so
neither set separates them.

> **This table used to say 3.075 ms still and 4.085 moving, and both were wrong.** They were
> taken with `--shot` on, under a command line written here that says they were not. A
> screenshot stalls its frame to about 250 ms and `Stats::sample` averaged it in like any
> other, worth roughly 1.6 ms of mean. The statistics now skip the frame a shot was requested
> on, which removes that.
>
> **Whether a residual remains is not established, and this file has now had the sign wrong
> in both directions.** It first claimed `--shot 200` was *cheaper* (2.238 against 2.301),
> off one pair. Six ordered pairs then said dearer in six of six, +0.15 ms, and that went in
> here as fact. Both were beneath the noise.
>
> `--repeat` settled it by measuring six segments inside one process, world loaded once,
> nothing changed between them: spread **1.773 ms** on that invocation, and 0.2 to 2.1 ms
> across six further invocations of the same command. The machine's own
> variation between identical segments is more than ten times the effect either claim
> described, and the six pairs were run in a fixed order, so a warming trend alone would
> produce that sign. **No number for the residual is published here.** The log still warns
> when `--shot` meets `--budget` or `--stats`, because taking numbers from a run with no
> shots costs nothing and removes the question.

The review's absolute numbers sit about 0.23 ms below these, consistently, on the same
commit and the same command. Neither of us can explain it beyond machine state, and it is
recorded rather than reconciled: **the conclusion is the same in both sets and does not
depend on whose absolute is right.**

**What that costs is an argument this file used to make and can no longer make.** It closed
by saying the still/moving gap showed the cost tracking what is *visible* rather than what
exists, and therefore that sprint 3 should open with chunking rather than with the town.
There is no gap. The evidence was an artefact of the measurement, and it is withdrawn.

What is still true, and is not evidence for any particular answer: 131 072 triangles are
submitted three times a frame with **no culling of any kind**, while the camera sees perhaps
a fiftieth of the map and the sun's split covers 60 m of a 256 m one. The bare land, with
nothing standing on it, spends 44% of the frame. **Whether sprint 3 opens with the chunking
or with the town is the senior's call and is not settled here.**

## What the data proved about the blend

31% of the map's tiles grade across themselves, 67% are pure base and 2% pure overlay. So
the blend is real and works, and the hard-edged grass patches in the shots are MU's own
per-tile surface assignment showing through, not a bug: a tile at full overlay beside one at
full base cannot gradate between them, because they are different surface pairs and so
different draws. Whether MU2's Godot client makes the same edges is a side-by-side question
and belongs to sprint 8.

## Still owing

- **Water is a flat blue texture.** Ten surfaces carry `water: true` and nothing reads the
  flag yet; the moat draws as ordinary ground. It is in the shots and it looks like what it
  is.
- **The look is now MU2's, by derivation rather than by eye.** MU2's own `GroundSource` was
  read and followed: the baked light multiplies the albedo with no factor, the dry ground
  takes no specular and no sky reflection (its art has lighting painted in, and MU's
  48-degree camera makes the whole frame grazing), and the base/overlay blend is height-aware
  with a 0.35 bite tapered at both ends. What it is *not* is judged side by side against MU2
  running — that is still sprint 8.
- **The tiling repeats visibly** from this camera, which is the same problem MU4 solved on
  its arena floor by reading the texture a second time at a third of the scale, turned. Not
  built here.
- **No culling**, as above.
