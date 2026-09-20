# Sprint 2: the ground

**Proved by:** Lorencia's bare land from MU's own play camera at 1080p, inside the prepass
and shade accounts.

**Landed** 2026-09-20, awaiting review. Planned from the data rather than from a guess.

## What the data turned out to be

Read before writing anything, and it changes the sprint.

**`lorencia_ground.glb` is a terrain mesh that is already built.** 14 MB, one mesh, **44
primitives**, 262 144 vertices and **131 072 triangles** — which is 256 × 256 tiles × 2,
one quad per tile with its own four unshared vertices. It is already in **metres** and
already has rows running **−z** (`min [32, 0, −249]`, `max [245, 2.23, −14]`), so the
conventions page's scale and axis rules are satisfied by the file itself. No heightfield
needs building from `height.png` to draw the land.

Each primitive is one **surface pair**, and the material names say which:
`TileGrass01__TileGround01` is that base under that overlay, and a name with no `__` is a
base with no overlay (9 of the 44). The nine slots are in `lorencia.json`'s `tile_slots`.

The vertex attributes carry the rest:

- **`TEXCOORD_0` is in tiles, not in [0,1]** — it runs 32..245 across the map. Each half of
  a surface multiplies it by its own `repeat` from `ground_surfaces.json` (0.25 or 0.5), so
  one texture covers four tiles or two.
- **`COLOR_0` is `VEC4` float and is two different things.** `rgb` (0.18..0.53) is MU's
  baked `TerrainLight` sampled per vertex — already a lit result, so it multiplies the
  diffuse and does not go through an sRGB sampler. `a` (0..1) is the **blend weight** from
  base to overlay.
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
warmup.

| | still camera | camera moving |
|---|---|---|
| **frame (mean wall ms)** | **3.075** | **4.085** |
| fps | 325 | 245 |
| gpu frame (reported, counts waiting) | 2.226 | 2.402 |
| budget | kept | kept |

Against a 5.5 ms frame. **This is the finding of the sprint, and it is not a good one: the
bare land, with nothing standing on it, spends three quarters of the frame's whole
allowance.** No house, no tree, no grass, no figure, no HUD — and 2845 objects are due in
sprint 3.

Where it goes: 131 072 triangles are submitted three times a frame, to the sun's split, to
the prepass and to the shade pass, with **no culling of any kind**. The camera sees perhaps
a fiftieth of the map. The shadow split covers 60 m of a 256 m map and is handed all of it.

So sprint 3 does not start with the town. It starts with the chunking that foundation 7 of
`PLAN.md` describes, and the town is placed into a frame that can already refuse to draw
what nobody is looking at. The alternative — place 2845 objects first and cull afterwards —
is how the budget gets spent before anyone notices.

The two numbers that say which chunking is worth building: a still camera costs 3.075 ms and
a moving one 4.085. The difference is the sun's split re-framing every frame and more
surfaces entering view, so the cost tracks what is *visible*, not what exists — which is
exactly the shape frustum culling helps.

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
- **The look is not judged.** MU's baked TerrainLight is multiplied in at x2, which is an
  invention with no source — MU2's Godot shader uses it differently and the two have to be
  put side by side before either number means anything. The land reads pale.
- **The tiling repeats visibly** from this camera, which is the same problem MU4 solved on
  its arena floor by reading the texture a second time at a third of the scale, turned. Not
  built here.
- **No culling**, as above.
