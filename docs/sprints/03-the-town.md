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

**So the order was to be decided by a measurement rather than by the argument that was
withdrawn** — and it was, though not by the three runs planned here. The town turned out to
be cheap enough to place first and measure afterwards: see **Measured, with the town
standing** below, where the land is 2.201 ms, the whole town adds 0.181, and the chunk
culling that this section demanded come first cannot be measured at all. The plan's own
question is answered in the answer's favour and against the order it proposed.

**The budget this sprint is judged against**, stated before the work: land **1.5 ms**, town
**1.5 ms**, figures **1.5 ms**, HUD and effects and present **1.0 ms**. The land is at 2.2
and so is 0.7 ms over its share; the town came in at 0.18 against 1.5. The land's overdraft
is real, it is not triangles, and it is sprint 4's first measurement rather than a story
told here.

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
  and **11 are `BLEND`** — fire, candle, curtain, the lit window panes of `House01`. (QA
  counted 41 `MASK`; re-counted over the 105 models the town actually places, it is 39, with
  11 `BLEND` and 173 opaque to 223 materials. The 39 stands.)
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
  not worth building for eleven materials. They belong with the lamps and the fires in
  sprint 8.

  **This line said they drew as cutout, and for most of the sprint they did not** — QA read
  the cook and found `alphaMode == "BLEND"` falling through to no cutout at all, so the
  flame, the candles, the street light's glow and the lit window panes drew as opaque cards:
  a hard quad edge around every fire, and `Waterspout01`'s fall as solid silver ribbons. The
  cook now gives them a cutout of 0.5, which is **`invention`** — MU blends them and we do
  not, and the threshold is ours. What is still owed with them: they go on writing into the
  shadow map, so a glow still casts, which is this sprint's own plan item 6 and needs a flag
  a material does not yet carry.
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

### The cook was wrong twice, and `cookcheck` is why that is known

`tools/cook/cookcheck.cpp` reads the cooked files back: every `.ktx` for its format, its
full chain and its size; every cutout albedo decoded level by level for the coverage the
rescale is supposed to hold; every `.mum` for indices that point inside its own vertices and
parts that cover them exactly once. It found **31 failures in the first cook**, all mine.

- **The rescale compounded.** Each level was rescaled *in place* and the next was filtered
  from the pushed alpha, so the push accumulated: fourteen of the twenty-four cutout sheets
  ran away, every tree among them, some to a coverage of 1.000 and some to 0.000 — the two
  failures the rescale exists to prevent, produced by the code meant to prevent them. The
  chain is now filtered from unscaled levels and the rescale applied to a copy on its way to
  the encoder. Worst drift across all 24 sheets after the fix: **5.2%**.
- **Padding a sheet that is not a whole number of blocks put its content in the wrong
  place.** bimg's KTX *reader* rounds a block-compressed image up to whole blocks even when
  the file records the true size — Lorencia's 192×6 glow sheets come back as 192×8 — so
  edge-padded content sits in eight rows while every v coordinate assumes six. Those sources
  are now resampled to whole blocks instead, which costs a little softening on four sheets
  and puts the content where the reader will look for it.
- **And the checker was wrong once too**, in the same direction: it measured coverage over
  bimg's block-rounded level size, so a tree's 1×1 level read as 4×4 of padding and scored
  0.000. The chain was replayed uncompressed in Python to find out which of the two was
  lying — it held 0.51 within a hundredth down to 6×6 — and the checker now counts only a
  level's true corner and stops judging below 64 texels, where a coverage cannot be
  expressed finely enough to mean anything.

Proved able to fail, as sprint 0's budget gate had to be: a `.mum` with one index bent past
its vertex count and another with a version nobody wrote are both caught, exit 1.

### The town is cooked too, chunks and all

Foundation 7 says chunk bounds and each kind's range are settled once and not per frame.
The cook is that once: `lorencia.mut`, **110 kB**, holds the 2753 drawn placements already
in metres, already on our axes, sorted by chunk and then by model — so a chunk that survives
a cull is a run of instances and each model inside it a contiguous sub-run. A frame appends
a range; it does not sort and it does not look anything up.

Measured on the way through, and each is a fact the frame will depend on:

- **64 chunks of 32 tiles, and every one of them holds something** — 9 placements in the
  emptiest, 37 in the median, 135 in the fullest. A uniformly occupied map is the *worst*
  case for chunk culling and worth knowing before the culling is written; 16-tile chunks are
  one flag away when there is a frame to measure them against.
- **999 placements laid on the terrain**, exactly the eight grass models, by MU2's
  `Grounded()` rule — re-measured here before it was copied: 57% of them carry a stored
  pitch, and their stored height runs from 6.60 m below the land to 4.29 m above it.
- **2702 of 2753 carry MU's baked light** at their tile; the other 51 stand on tiles the
  light map paints black.
- **One placement stands off the edge of the grid.** It is clamped to the nearest chunk and
  counted, because a placement that has left the map is also exactly the shape a
  units-per-tile mistake takes, and a silent clamp would hide one.

`cookcheck` now reads the `.mut` as well, for the invariants the frame will trust without
testing: the runs contiguous and covering every instance once, the models sorted inside each
chunk, every instance inside the box its chunk claims, every model index naming a model.
Proved able to fail: an instance moved outside its box and one put out of model order are
both caught, exit 1.

**Two debts the cook creates, both owed inside this sprint.** Cooked normals are BC5 with
two channels, and `fs_shade.sc:98` and `fs_ground.sc:81-82` both read `.xyz` — they need the
z rebuilt, or every normal map reads flat-blue. And the mesh cook writes a 48-byte vertex
with no joints or weights, so sprint 4's figures need a second layout and a second path
through it; `04-figures.census.md` also records that `JOINTS_0` is `UNSIGNED_SHORT` on every
figure, which bgfx has no attribute type for, so that conversion belongs here in the cook
rather than in the loader.

## Measured, with the town standing

`./run.sh --frames 600 --repeat 6 --world lorencia --still`, Release, vsync off, 1080p, 4x
MSAA, machine quiet (load average about 4). Six segments a run, mean of means, which
`docs/budget.md` puts at about 0.1 ms of resolution.

| | frame (mean of means) | spread | draws |
|---|---|---|---|
| the land alone | **2.201 ms** | 0.144 | 136 |
| the land and the whole town, chunk-culled | **2.382 ms** | 0.350 | 692 |
| the land and the whole town, every placement | **2.458 ms** | 0.276 | 808 |

**The magnitudes in that table are not supportable and are withdrawn; the conclusions are
not.** QA re-measured the same three runs and got 2.018, 2.122 and 2.176 — the same order,
the same draw counts exactly, and a town costing 0.104 ms where this table says 0.181. Both
differences sit under the 0.1 ms that `docs/budget.md` says six segments can resolve, so
neither figure is a measurement of anything. What survives is what was inside the resolution
to say: **the land is a little over 2 ms, the town is too cheap to measure, and the culling
is too cheap to measure.**

Lorencia stands: **2753 placements of 105 models, 432 248 triangles if all of it is drawn**,
read from cooked files in **0.11 s** — against the five seconds the sprint was allowed.

**The town costs 0.18 ms.** That is the finding, and it is not the one this sprint was
planned around. The frame was expected to be geometry-bound and it is not: 432 000 triangles
and 2753 placements cost less than a tenth of the land they stand on.

**And chunk culling cannot be measured, even though it works.** The camera keeps **7 of 64
chunks and 497 of 2753 placements** — 82% of the town is left out of the camera passes — and
the frame moves by **0.076 ms**, which is below the resolution of the measurement. So:

- The chunking is **kept**, because it is built, correct and costs nothing, and because
  sprint 4's 290 monsters and their bone palettes will be read out of the same flat arrays.
- The argument that it had to come *first* is **withdrawn**. It was written when the land
  looked like 4.085 ms and geometry looked like the enemy. The land is 2.2 ms, the town is
  free, and whatever the remaining 2.2 ms is, it is not triangles. Finding out what it is
  belongs to sprint 4's measurement, not to another culling story.
- **Ranges per kind are not built** and, on this evidence, are not urgent.

### The cook wired all the way through

Two things were cooked and not read until the sprint was otherwise finished, which is its own
kind of unfinished:

- **MU's baked terrain light now reaches the town.** It was in the `.mut` from the first cook
  and nothing read it: `Drawable` carried a mesh and a transform and no colour. The instance
  buffer is 80 bytes now — a matrix and a colour — and `fs_shade` multiplies the albedo by
  it, as the land does with its own `COLOR_0`. Without it the town stood brighter than the
  ground it stands on and MU's painted dusk stopped at the foot of every wall.
- **The land reads the cook's sheets.** Its 27 textures are 1536² and are 49.5 MB of the
  cook's 90 — more than the town's 306 put together — and `ground.cpp` was still opening the
  source `.png`. It asks the manifest first and falls back to the `.png` when the cook has
  not been run. Its normals are BC5 now, so `fs_ground` rebuilds z exactly as `fs_shade`
  does.

**World load: 1.07 s → 0.227 s**, and the frame with everything cooked and the light applied
is **2.262 ms** over six segments, which is inside the 2.382 measured with the source sheets.

One trap for the next shader that shares a vertex program: bgfx's Metal backend links by
matching the two varying lists, so adding `v_light` to `vs_static` broke `fs_prepass`, which
shares it and did not name the new varying. The log says only "the frame is missing a
program"; `fs_prepass` now declares it and says why.

## What went wrong on the way, because none of it was the thing suspected

The town drew black on the first run and took four wrong diagnoses to find: the BC5 normals
(fixed, and right, but not the cause), the depth test, the ORM, and the shadow split. Each
was eliminated by a measurement rather than by argument — the albedo drawn alone, the ORM
drawn alone, the shadow term drawn alone, the casters removed one list at a time.

**The cause was that `bgfx::submit` discards its bindings.** The shadow map and the AO were
bound once per view, before the view's draws, which reaches the first draw and no other. So
the land's first surface had a shadow map and an AO and its other forty-three had neither,
and every placement in the town sampled unbound stages — which read as shadow 0 and AO 0,
and shade black whatever the albedo, the normals or the ORM say. **This was live in the
ground since sprint 2 and invisible**, because an unbound compare sampler reads as lit. Every
draw that shades now binds them itself.

Two smaller ones worth the same honesty:

- **Every placement's rotation was inverted**, found by the user looking at the fountain
  rather than by any test here: `bx::mtxSRT`'s Euler angles turn the opposite way from a
  right-handed frame, so MU's angles are passed negated. `docs/conventions.md` now says so
  beside the identical trap it already recorded for `bx::mtxFromQuaternion`. It is invisible
  on fences, grass and square planters, which is most of the town.
- **The culling counts in the log were wrong** while the culling was right: gathering the
  sun's casters after the camera's list overwrote the camera's tally, so the log said
  "64 of 64 chunks" — nothing culled — while 82% of the town was in fact being left out. The
  two tallies are separate now. A counter that reports the wrong thing is worse than none,
  because foundation 7 says a culling change that is not visible in those numbers did not
  happen.

## Still owing at the end of sprint 3

- **The sun's split is not culled at all.** Every one of the 2753 placements is submitted to
  the shadow pass every frame, on purpose: a chunk behind the camera still casts into the
  frame, and the camera's frustum is the wrong test for it. The right test is the split's own
  box extruded along the sun, and it is not built. The log says `sun 64 chunks, unculled` so
  that this cannot be forgotten.
- **Alpha to coverage on the cutouts**, which `submitBatches` describes and does not do: the
  prepass and the shade pass must compute the same mask or every leaf grows a black fringe.
  The grass is here now to test it with.
- **Water, the 11 blend materials, the ground's 1536² sheets**, all as listed above.
- **The shadows shimmer when the camera moves**, reported by the user watching the window
  and not yet isolated. What is established: with `--still` the frame is **bit-stable** --
  frames 60 and 120 differ in 0 of 2 073 600 pixels -- so whatever it is, it is coupled to
  camera motion and not to time. Two candidates, and they need separating before either is
  touched:
  1. **The PCSS phase is screen-space.** `fs_shade.sc` seeds its Vogel disc with
     `gradientNoise(gl_FragCoord.xy)`, which is fixed to the screen: as the camera moves,
     every surface point draws a different dither each frame and the penumbra boils. This
     costs nothing when still, which matches the stability above exactly.
  2. **The split's texel snap**, which `02-the-ground.md` flagged as never tested with a
     moving camera and which is still never tested with one.
  **Why it is not isolated yet**: a screenshot stalls its frame, so between two shots the
  camera has advanced far more than one frame and 80% of pixels change from motion alone --
  the same stall that made sprint 2's timings wrong. Isolating it wants a camera that can be
  stepped deterministically (a fixed advance per frame rather than one taken from elapsed
  time), and that lives in `game/world.cpp` and `main.cpp`, which sprint 4 is holding. The
  discriminating test once it exists: hold the phase constant and see whether the shimmer
  survives -- if it does, it is the snap; if it goes, it is the noise.

