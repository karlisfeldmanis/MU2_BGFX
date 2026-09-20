# Sprint 3: the town

**Proved by:** all of Lorencia standing, loaded in under 5 s from cooked files; draws and
frame written down.

**Planned** 2026-09-20, before any code, from the placement list and the 105 glb rather than
from a guess. Sprint 2 is still with QA and the junior; nothing here touches its files.

## Where sprint 2 left the frame, and what that forces

**Rewritten after sprint 2's review, at `e42f246`, and the first draft of this section is
withdrawn rather than patched.** It said the land cost 3.075 ms still and 4.085 moving, and
built an argument on the gap between them: that the frame tracks what is *visible* rather
than what exists, and therefore that this sprint had to open with chunking. Both figures
were taken with `--shot` on and the screenshot frames averaged into the mean — a shot stalls
its frame to about 250 ms — and re-measured without them the land costs **2.39 ms still and
2.40 moving**. There is no gap. There never was one, and there could not have been: the
ground submits all 44 of its surfaces in both cases, so nothing about the camera could have
changed the number. The inference was unsound even before the measurement was wrong.

What survives is the plain figure. The bare land is **2.39 ms of a 5.5 ms frame — 43% of it
— with nothing standing on it**, 131 072 triangles submitted three times over, while the
camera sees about a fiftieth of the map. Still due: 2753 placements here, a player and
**290 monsters** in sprint 4 (not the 30 the sprint table says — `04-figures.census.md`
counted the spawn rows), effects, and the HUD.

**So the order is decided by a measurement this sprint takes first, and not by the argument
that was withdrawn.** Three runs, before either chunking or placements are built:

1. the six views with **the ground not submitted at all** — what the frame costs empty;
2. the ground as it is now — the difference is what the land actually costs;
3. the ground with only the handful of surfaces the play camera can see, culled by hand —
   an upper bound on everything chunking could ever buy.

If (3) is close to (2), chunking the ground buys nothing and the town goes in first with the
culling deferred to where the placements make it pay. If (3) is far below (2), chunking
comes first. Either way the sprint has evidence rather than a conviction, which is what the
review took away and what it is owed.

**The budget this sprint is judged against**, stated now so it cannot be negotiated
afterwards: land **1.5 ms**, town **1.5 ms**, figures **1.5 ms**, HUD and effects and
present **1.0 ms**. The land is at 2.39 and so is already 0.9 ms over its share; that is the
debt this sprint either pays or writes down explicitly.

## What the data turned out to be

- **2845 placements, 117 model names, and only 2753 of them are drawn.** 78 are `hidden`
  (`PoseBox01` 53, `Light01/02/03` 25) and are anchors, not geometry — a fire anchor is
  hidden *because* something is drawn there, which is sprint 8's lamps. 14 more are NPCs
  (`BerdyshGuard`, `Smith01`, `PotionGirlAmy`, `WanderingMerchant`, …) with no glb here and
  six of them carrying an `idle` action name; they are sprint 4's figures. **105 models have
  a glb** and those are this sprint's whole content.
- **The models are small and the map is repetitive.** 224 primitives and **17 790 triangles**
  across all 105 models — `Tree02` is the largest at 770 — and **432 248 triangles** if every
  placement is drawn once. That is three times the ground's mesh in triangles and a hundredth
  of it in unique data, which is the shape instancing exists for.
- **Everything is already in metres and y-up**, like the ground: `House01` measures 4.4 × 3.2
  × 6.0, `Tree01` stands 8.25 m. Only the placement's `at` is in MU's units and takes the
  `(x, z, -y) / 100` swap of `conventions.md`.
- **Per-placement transform is a scale, a yaw and a pitch.** `scale` runs 0.30 to 3.28;
  `angle` is z on 2635 of them, x on 1166, and **y on none** — MU's z-up euler, so its z is
  our yaw and its x a real pitch on a third of the town.
- **223 of 224 materials are `doubleSided`**, so two-sided is the rule here and not the
  exception. **39 are `MASK`** (cutout: every `Grass`, seven `Tree`, the signs, the straw)
  and **11 are `BLEND`** — fire, candle, curtain, the lit window panes of `House01`.
- **The textures are embedded and they are the load time.** 587 images inside 97.6 MB of glb,
  **74.2 Mpx** in total: 275 at 384², 100 at 96², 36 at 768². Uncompressed that is ~297 MB of
  VRAM; BC7 with a full chain is ~99 MB. Decoding 587 PNGs and building their mips at load is
  what the five second figure will be spent on if the cook does not exist.
- **999 placements are grass, types 20 to 27**, and MU2 found by measurement that the map
  stores them up to four metres off the terrain with a pitch: `World.cs`'s `Grounded()` lays
  types 20–27 on the land, drops pitch and roll, keeps yaw. That is a **deliberate deviation
  from MU's data in favour of MU's game**, it is traced, and it is re-measured on our own
  copy of the list before it is copied in.
- **20 models carry a skin and one animation** — `Tree01`, `Tree02`, `Tree11` and the rest of
  the swaying set, 331 placements. They are drawn in **bind pose** this sprint. Nothing in
  the instance path may assume an instance is static, because sprint 4 puts these through the
  same bone texture as the figures.

## What this sprint builds, in this order

1. **Chunks, before anything is placed.** The map cut into tile blocks with their own bounds,
   the ground's 44 surfaces cut with them. **The camera and the sun are culled separately** —
   the shadow pass tests the split's own box extruded along the sun, never the camera
   frustum, which is the named bug of foundation 7. Drawn and culled counts go in the log
   every second, **for both passes separately**, or the change did not happen. Block size is
   measured (16, 32, 64 tiles), not chosen.
2. **`tools/cook.py`, and a texture tool beside it.** glb → a flat mesh file; the embedded
   PNGs → BC7/BC5 `.ktx` with a full mip chain; `mu.db`'s tables → flat versioned files.
   `BGFX_BUILD_TOOLS_TEXTURE` is currently `OFF` and `texturec` builds its own chain
   internally, so the **alpha-coverage rescale that `conventions.md` records as owed** cannot
   be expressed through it: the cook generates the levels itself and a small tool linking the
   pinned `bimg_encode` writes them. One binary, no per-level stitching, the same encoder the
   engine decodes.
3. **Instancing.** One instance buffer a frame, read by shadow, prepass and shade alike;
   batches keyed by (model, primitive, chunk); a flat array walked per frame with no
   allocation, no map lookup and no sort. Per instance: the transform and **MU's baked light
   at the placement's tile**, sampled from `light.png` as `World.cs`'s `Lit()` does — without
   it the town stands unlit over ground that is lit.
4. **Cutouts in every pass**, shadow and prepass included, decided at cook time from
   `alpha_mode`/`alpha_cutoff` and never from the pixels.
5. **Ranges per kind.** Grass, clutter and lamps each stop drawing at their own distance and
   **stop casting before they stop drawing**. MU's camera is fixed and close, so this is
   nearly free and worth more than any frustum test.
6. **Two shadow rules MU2 paid for and wrote down.** A glow does not cast — the lit window
   went on printing itself into the shadow map in the one place anybody looks. And size is
   measured off the model, not listed per type.

## What is deliberately deferred

- **The 11 `BLEND` materials.** There is no sorted transparent pass in this frame and one is
  not worth building for eleven materials. They draw as cutout this sprint and are listed as
  owing; they belong with the lamps and the fires in sprint 8.
- **Sway** (20 models, 331 placements) — bind pose here, the bone texture in sprint 4.
- **Water, the NPCs, the fires and the lamps' light.** Named above; each has its own sprint.
- **Occlusion culling**, as `PLAN.md` says: considered only if this sprint's measurement says
  the frustum and the ranges were not enough.

## The two things most likely to go wrong

Written before the code, as sprint 2's were:

- **The mip chain thins the leaves before anyone thinks to look.** 39 cutout materials, most
  of them foliage, are about to get their first mip chain in this engine. Averaging alpha
  down the chain makes grass vanish at distance, and the failure reads as "the culling range
  is too short" rather than as a texture bug. The coverage rescale is written *with* the
  chain, not after it, and the proof is a shot of the same grass at two distances.
- **Chunking the shadow pass with the camera's frustum.** It looks right from the play
  camera, because what it removes is the shadow of whatever is just off screen — and MU's
  camera is close enough that the sun's casters mostly *are* off screen. The two logged
  culled counts exist to make this visible: if the shadow pass's culled count tracks the
  camera's, it is wrong.

## Built: the cook, and what it turned out to cost

`tools/cook/texcook.cpp` and `tools/cook.py`, run on Lorencia 2026-09-20. Nothing loads the
result yet — the loaders are this sprint's own work — but the files exist and the numbers
are real.

| | before | cooked |
|---|---|---|
| model textures | 306 distinct images from 649 uses, embedded in the glb | **40.5 MB** of BC7/BC5 with mips |
| ground sheets | 27 loose PNG, **1536²** each | **49.5 MB** |
| all textures in VRAM | 269.9 MB of RGBA8 at the top level alone, ~360 MB once the engine builds its chains at load | **90.0 MB**, chains included |
| meshes | 17 790 triangles inside **97.6 MB** of glb | **3.5 MB** of `.mum`, 105 files |

Three things worth writing down:

- **The land's 27 sheets cost more than the town's 306.** 49.5 MB against 40.5, because
  every ground surface is 1536² where a model's is 384². Whether MU's camera can tell is a
  look question and it is now a cheap experiment: halve them in the cook and put the two
  shots side by side. Not done here.
- **The disk does not shrink and that was never the win.** 93.5 MB cooked against 97.6 MB
  of glb is a wash; what goes away is decoding 333 PNGs and building their mip chains at
  load, and three quarters of the texture memory.
- **The cook is slow and it only has to be right.** 54 minutes wall, 6.7 core-hours, almost
  all of it BC7's mode search. It is incremental by content hash, so a re-run after a
  changed sheet is seconds.

**Two debts the cook creates, both owed inside this sprint.** Cooked normals are BC5 with
two channels, and `fs_shade.sc:98` and `fs_ground.sc:81-82` both read `.xyz` — they need the
z rebuilt, or every normal map reads flat-blue. And the mesh cook writes a 48-byte vertex
with no joints or weights, so sprint 4's figures need a second layout and a second path
through it; `04-figures.census.md` also records that `JOINTS_0` is `UNSIGNED_SHORT` on every
figure, which bgfx has no attribute type for, so that conversion belongs here in the cook
rather than in the loader.

## Measured

Filled in when the sprint lands: chunk size chosen and why, draws and culled for both passes,
load time from cooked files, the frame still and moving, and what the town cost over the bare
land.
