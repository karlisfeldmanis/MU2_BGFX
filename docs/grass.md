# Grass

The research this was designed from, and then what was built off it. The research half is
below from **The finding** to **The question this page cannot answer**; every number in it
marked **computed** is arithmetic off the camera constants in `src/game/world/world.cpp`, and
every number marked **theirs** is somebody else's shipped measurement on somebody else's
hardware. **What was built** at the foot is this engine's own, and every number in it was
measured here.

The user's call on the one question the research could not answer was the **middle road**:
real blades in the near band, MU's card grass and the ground texture beyond.

## The finding

MU's camera is fixed, near, and steeply down. That single fact removes about four fifths of
what a modern grass system is for.

Every published grass system worth reading — Sucker Punch's, Treyarch's, the GPU-driven
open-world ones — is built for a camera that can stand in the field, lie in it, look along it
to the horizon, and turn. All the expensive machinery answers that camera: Hi-Z occlusion
culling, tile-ring streaming, four LODs, impostor clump cards, terrain-blend at 120 m. MU has
none of that camera. It has one pitch, one yaw, and a zoom with a floor and a ceiling.

**The whole visible field is smaller than Ghost of Tsushima's nearest LOD band.**

## The frame this grass lives in

From `world.cpp:14-22`:

| | |
|---|---|
| field of view | 55° vertical, fixed |
| pitch | −48.5°, fixed |
| yaw | 45°, fixed |
| distance | 8 m measuring, 6 m played, 3.5 m nearest |
| lift | the character sits 0.12 of a frame above centre |

Pitch and yaw never change. The camera never rolls. Only the distance moves, and only between
3.5 and 8 m. Grass casts no shadow in MU2 and should cast none here (`Turf.cs:420` — "a field
of quads a metre tall would fill the shadow map with noise, and the client's grass has no
shadow at all"), so the grass has exactly one consumer: the camera frustum, at one orientation.

### The field, in numbers

**Computed**, 1920×1080, flat ground, ignoring the lift and the terrain's relief:

| | at 8 m (measuring) | at 6 m (played) |
|---|---|---|
| camera above ground | 7.49 m | 5.99 m |
| near edge of ground | 1.9 m out | 1.5 m out |
| far edge of ground | 19.5 m out | 15.6 m out |
| depth of the strip | 17.6 m | 14.1 m |
| width, near → far | 14.3 → 38.7 m | 11.4 → 30.9 m |
| **ground area in frame** | **≈ 470 m²** | **≈ 300 m²** |

Call it 500 m² with the lift and a margin, and about 1 200 m² if the resident set is a 20 m
disc round the character so the zoom and a step never catch it short.

For comparison, the 2026 GPU-driven system below budgets 2.7 ms for a **million** blades over
a 120 m radius. At 300 blades/m² — a dense lawn — MU's whole frame is **150 000 blades**. At
Ghost-of-Tsushima near density it is half a million, and still every one of them is inside
what they call LOD0/LOD1 range.

### How big a blade is on screen

1080 px over 55° is 1 037 px per metre at 1 m. **Computed**, for a 25 cm blade 1.5 cm wide:

| | near edge (7.7 m) | far edge (20.9 m) |
|---|---|---|
| blade height | 33 px | 12 px |
| blade width | 2.0 px | 0.74 px |

Two things fall out of that table.

**The camera never gets close to the grass.** Two pixels wide at the nearest point the zoom
allows. Cubic Bézier curvature, 15 vertices a blade, view-space thickening, per-blade
specular — all of that is detail for a camera that can put a blade across forty pixels. Here a
blade is a tapered strip of **three to five vertices** and nothing is lost. The engine does not
need an LOD0 at all; it needs what everyone else calls LOD1, everywhere.

**The far edge is sub-pixel, and there is no TAA.** `--msaa` defaults to 4 and there is no
temporal pass. A 0.74 px blade under 4× MSAA without temporal reconstruction is a crawling
speckle as the camera walks. This is the one real aliasing problem, and it is solved the way
the mesh-shader people solve it: past some distance, **drop blade count and widen the
survivors** so coverage stays but each blade stays above a pixel — then hand the last few
metres to the ground texture.

## What the field does, and what of it applies

### Ghost of Tsushima — Sucker Punch, GDC 2019

The canonical talk. One compute thread per blade; evenly spread positions with jitter; frustum
and distance cull on the GPU; blade as a cubic Bézier; **15 vertices near, 7 far, and a
terrain texture very far**, with vertex positions blended across the switch so nothing pops.
View-space thickening pushes an edge-on blade's vertices toward the camera so it never reads as
paper. Voronoi clumping drives height, colour, facing and lean together. Shadows are not the
grass: the terrain's own vertices are raised to grass height and depth is written in a dithered
pattern, with screen-space shadows for the fine detail. Interaction comes from a displacement
buffer the compute shader reads — no CPU in the loop.

*Applies here:* the Bézier blade (cheaply — quadratic is enough at 3–5 vertices), the clumping,
the displacement buffer, the "terrain texture very far" ending. *Does not apply:* the LOD chain,
the occlusion culling (they call its win marginal even in an open world), the thickening (a
fixed camera plus a fixed yaw means a blade's facing can be **authored** relative to the camera
rather than corrected per frame).

### Call of Duty: Black Ops 4 — c0de517e's retrospective

The most useful page of the lot, because it is about how grass should *look* rather than how it
should be *generated*, and it is honest about the fakery.

- **Instanced triangle geometry, not alpha cards.** And the headline: rendering *with* grass was
  **faster than without**, because solid blades occluded an expensive multi-layer terrain shader.
- **Grass has three frequency bands.** Low: the field behaves like velvet, a textile BRDF. Mid:
  bumpy clumps that shadow each other. High: individual blade specular. Chase the aggregate, not
  the blade.
- **Translucency without forward shading:** outdoors is sun-only, so when a normal faces away
  from the sun, flip it and boost albedo saturation. That is the green backlit look for free.
- **Normals decoupled from coverage:** a procedural V-profile normal, blending the geometric
  normal toward the tangent by distance from the blade's centre line, thins the specular
  highlight without thinning the triangle.
- **Roughness, not F0**, is what gets modulated, or Fresnel washes the field out at grazing
  angles — and MU's camera is grazing-ish by construction.
- **A height ramp of ambient occlusion** — dark at the root, bright at the tip — is called the
  single most effective depth cue, and is one multiply.

*Applies here: all of it.* This is the section to implement first. The occlusion win in
particular is real for this engine, where the ground pays a PBR shade pass with SSAO and a cube
read on every covered pixel.

### Helio, 2026 — a current GPU-driven foliage system

Worth reading for the budget table and the determinism, not for the architecture, which is
built for the camera MU does not have. Four compute stages (place, tile cull, cluster cull,
finalize), a tile ring with at most 24 tiles placed a frame, Hi-Z against dilated tile AABBs,
4×4 blade clusters, four LODs with stochastic cross-fade, 16 bytes an instance.

**Theirs**, 1 M blades at 1080p: place 0.20, cull 0.15, L0 raster 0.55, L1 0.60, L2/L3 0.95,
impostors 0.15 — **2.70 ms total**. Their L0 band is 0–8 m and L1 is 8–20 m; **MU's entire
frame is 1.9–19.5 m**, so the honest read of that table for this engine is *their L1 band
alone, at a fraction of the area* — call it a small part of 0.6 ms, before anything MU-specific.

Two ideas from it are worth taking whatever shape the rest lands in:

- **Placement as a pure function** of (tile, lane, seed). No stored positions to stream, no
  ordering to get wrong across a reload, and a save/reload grows the identical field.
- **LOD cross-fade by height, not by alpha.** Blades shrink into the ground over a 2 m band.
  Alpha-fading thin geometry without TAA is exactly the crawl this engine cannot absorb.

### Mesh shaders — GPUOpen, after Jahrmann & Wimmer 2017

One work group draws a whole patch, up to 32 blades, 8 vertices each. The LOD trick is the one
named above: `bladeCount = lerp(max, 2, dist/end)`, the last blade gets `width *= frac(count)`
so it thins out instead of vanishing, and survivors get `width *= max/count` so the patch keeps
its coverage. No performance numbers published.

Not available here — **bgfx has no mesh shaders**, and none of this engine's 40 shaders is a
compute shader. The `width *= max/count` compensation is the transferable part, and it works
just as well from a vertex shader reading an instance buffer.

### The Godot ones — hexaquo, 2Retr0

Closest to where MU2 already is. Both land on: `MultiMeshInstance3D` batching, blade normals
transferred from a **cylinder** so the flat strip shades round, an AO ramp up the blade,
backlight for translucency, roughness 0.4 and specular pushed *down* to 0.2, cellular-noise
clumping driving size/colour/bend together, and **shadows off on the grass entirely**. hexaquo
states the modern-hardware position plainly: real blade geometry is preferable to the overdraw
of transparent billboards. 2Retr0 is honest that CPU-side placement in a MultiMesh makes LOD
swaps pop at tile boundaries.

## The decisions, and a recommendation on each

### 1. Cards or blades

**Blades.** Not close. Alpha-tested cards are the wrong trade on every axis this engine has:
they overdraw, they defeat early-Z, they alias without TAA, and they cannot take the V-profile
normal that makes grass read as grass. The one thing they buy — fewer triangles — is the cheap
resource here, since the field is 500 m² and not a horizon. Treyarch's result that solid blades
came out *net faster* than no grass is the case for the defence.

Keep MU2's painted `wild.png` meadow plants as cards, though. A daisy is a picture; a blade is
a shape.

### 2. Where blades come from

**A static instance buffer per ground chunk, expanded in the vertex shader.** Not compute — not
yet.

The engine has no compute path, and adding one is a sprint of its own against a field whose
worst case is 150 k blades over a frustum that never turns. A vertex shader reading a packed
per-blade instance (Helio's 16 bytes: position as a tile fraction, packed tint, 16-bit seed) and
emitting a 5-vertex strip from `gl_VertexID` needs no vertex buffer, no index buffer, and no new
pipeline stage. Chunk the field at the ground's own chunking, cull chunks on the CPU against the
existing frustum (`src/game/frustum.h`), submit one instanced draw a chunk.

Revisit compute only if a measurement says the vertex pass is the cost — and note the trap it
would answer first is not culling but **per-blade placement from the height and attribute
grids**, which is a build-time job here anyway, because Lorencia does not change.

### 3. The far edge

Three metres of trouble, from about 14 m out. In order of what to try:

1. Thin the blade count with distance and widen the survivors, `width *= max/count`, so
   coverage holds and no blade goes sub-pixel.
2. Sink the last band into the ground over ~2 m of height interpolation, never alpha.
3. Let the ground texture carry it past that. Ghost of Tsushima, Helio and BO4 all end the same
   way, and MU's ground is already a painted grass tile — the thing it fades into is the thing
   the game shipped with.

### 4. Shading

The paint-over list, in the order it pays:

- **AO ramp up the blade.** Dark root, bright tip. One multiply, the biggest single win.
- **V-profile normal.** Blend the geometric normal toward the tangent by distance from the
  centre line. Thins the specular without thinning the triangle.
- **Sun-only translucency.** Normal faces away from the sun → flip it, boost albedo saturation.
- **Roughness modulation, never F0.** Grazing angles are MU's default angle.
- **Clump-driven variation.** Cellular noise driving height, tint, lean and facing *together*,
  so the field reads as patches rather than static.

The engine's existing PBR shade pass (`fs_shade.sc`) and SSAO want checking against this. Thin
geometry in the prepass will give SSAO something it cannot do anything sensible with, and the
first question a sprint asks is whether grass writes into the prepass at all.

### 5. Wind and interaction

Wind: scrolled Perlin at two frequencies for direction and strength, plus a per-blade sine
phased off the blade's own hash so they do not move as one. MU2's Turf already does the shape
of this, including the walker's shove and the wake behind him, and `Turf.cs:87` holds the
lesson worth carrying — **cap how far the shader may move a blade from where it was built**, or
the chunk bounds are a lie and blades bend into frame out of nothing.

Interaction: Ghost of Tsushima's displacement buffer, a camera-relative texture snapped to its
own texel grid, is cleaner than MU2's footprint list and costs the same either way. Not a first
sprint.

## What it would cost, and what pays

This is where the page stops and the user decides, because **the budget has no room in it.**

`docs/budget.md`: the accounts add to 5.8 against an enforced 5.5, the spare is 0.2, and sprint
8c's probe already owes 0.3 that no account has given up. Grass is a new account on a page that
is over.

A defensible first ask is **0.4 ms**, on the grounds that the field is a twentieth of the area
the 2.7 ms systems cover and the blades are their cheap LOD. Against that sits the Treyarch
finding that grass can be *free or better* by occluding the ground's shade pass — which in this
engine means a PBR evaluation, a shadow lookup, a cube read and an SSAO fetch per covered pixel,
and that is not nothing. **Whether grass costs 0.4 ms or −0.1 ms is a measurement, not an
argument**, and it should be the first thing a sprint does: draw a field of untextured blades
over Lorencia's town and read the wall frame, before any of the shading above is written.

## The question this page cannot answer

**Is MU's grass wanted, or is grass wanted?**

MU2's `Turf.cs` is explicit that MU has no blades. MU draws one quad per grass tile, standing on
the tile's own edge, leaning half a tile along −X, textured from four 64-pixel columns of a
painted sheet with a per-row roll so the columns never line up. It reads as corduroy. It is
faithful, it is nearly free, and `Turf.cs:37` calls a scattered field "not wrong so much as a
different game's grass."

Everything above is that different game's grass. It is the better-looking answer and it is the
one the research supports; it is not MU's. The middle road exists and may be the right one:
**MU's card grass as the base layer everywhere, real blades only in the near band**, where the
camera can actually resolve them — which is to say the first 8 metres, which is a third of the
frame's depth and rather less of its area. That would cost a fraction of the 0.4 ms and would
keep the silhouette MU has.

That is the user's call, and it should be made before a sprint, not during one.

## Traps, collected

- **No TAA.** Every alpha-fade, dither-fade and stochastic cross-fade in the literature assumes
  temporal reconstruction. None of them can be copied as written.
- **MSAA 4× is the only antialiasing**, and it is paid on the prepass, the shared depth and the
  shade target — thin geometry is exactly what it is good at, which is an argument for blades
  and against cards.
- **Grass casts no shadow.** Do not let it into the shadow pass; do not let it into the shadow
  pass by accident through a shared drawable list.
- **A shader that moves a vertex must not move it outside the chunk's bounds** (`Turf.cs:87`).
- **The measuring camera is 8 m**, not the played 6, and every frame number in `docs/budget.md`
  was taken there. A grass measurement taken at 6 m is not comparable to anything on that page.
- **Per-view GPU timers do not work on Metal** (`docs/budget.md`). The wall frame with the field
  on against the field off is the only number that will mean anything.

## What was built

2026-09-23. Two of them, and the second replaced the first. Both are in this section because
the first one is the measurement that justifies the second.

### First: geometric blades. Built, measured, replaced.

One tapered five-vertex strip per blade, 256 a square metre, no texture at all — the field the
research above argued for. It worked and it looked like grass. It was replaced anyway, on its
own numbers: **+0.79 ms** in the open field, all of it fill, with the whole vertex pass (30 000
blades, 92 000 triangles, twice a frame) costing only 0.14 of it. Taken apart with each knob in
turn:

| | median frame | over baseline |
|---|---|---|
| grass off | 4.05 | — |
| height ~0 (all the vertex work, no fill) | 4.11 | +0.06 |
| width ~0 (all the vertex work, almost no fill) | 4.19 | +0.14 |
| radius 8 rather than 11 | 4.71 | +0.66 |
| density 0.5 | 4.58 | +0.53 |
| everything on, 96 blades/m² | 4.91 | +0.87 |
| everything on, 256 blades/m² | 4.84 | +0.79 |

The one thing that came back out of that was **`sunShadowHard`**: the ground reads the sun's
split with thirteen taps for its penumbra, and a blade two pixels wide has no penumbra anybody
can see. One bilinear compare instead. **0.41 ms for no visible difference**, confirmed on a
shot before and after, and it is kept in the card field below.

### Second, and current: MU's painted cards. Turf, with the placement rebuilt.

MU does not model blades. It paints a tuft — four 64-pixel columns of one, in
`assets/effects/grass/<world>_TileGrassNN.png` — and stands cut-out cards of it on the land;
MU2's Godot client did the same and scattered them. **A painted column is eight or ten blades
of grass for the fill of one quad**, and that ratio is the whole argument.

- **49 cards a square metre**, six vertices each (root pair, middle pair, top pair; the middle
  row is what lets a card arch, and MU's grass leans hard). Stratified on a 5×5 jitter.
- **Nothing is stored.** The CPU sends one instance a square metre — corner, four corner
  heights, MU's baked light, the density it is allowed, a seed off its own tile — and every
  card is hashed out of that. The vertex buffer is 2.3 KB and holds no geometry.
- **One instanced draw per painted sheet**, two sheets on Lorencia, in the prepass and again in
  the shade pass off **one** vertex shader: the shade pass tests depth `EQUAL`, so the two must
  place a card at the same bit. `fs_grass_prepass` runs the same cutout at the same threshold
  off the same sheet — without it the prepass lays a wall of depth over the turf behind every
  mostly-empty card and the turf comes out black.
- **Never the shadow pass.**
- **Every random number has a job**: the card's own height draw, its bunch's, its patch's
  vigour, and a one-in-twelve step for the rank tufts that stand through the sward (without
  that step a field reads as mown however much jitter is on it). One stiffness draw feeds the
  lean, the arch and the wind response together. Roll, column, tint and dryness each their own.
  Two clump fields — 4.5 m for vigour and dryness, 0.6 m for bunches — read off the world, so a
  tuft does not stop at a patch edge.
- **The density that thins with distance is a ramp, not a step.** It has to be: that number
  falls as the camera walks towards a patch, and on a step the cards whose hash sits near it
  blink on and off frame by frame.

### The shimmer, and what fixed it

The first card landing shimmered, and it was measured rather than argued about. With the camera
moving and **the wind switched off**, the mean per-pixel change between consecutive frames was
**13.12 levels with the field against 4.89 without** — so all of it was crawl and none of it was
motion. Card density was not the driver either: at 12% density it still measured 12.0, so it was
not the number of cut-out edges.

It was the cut-out itself. A hard alpha test gives every painted blade a binary edge, and a
binary edge a few pixels wide under 4x MSAA with no TAA flips whole runs of pixels between grass
and ground on half a texel of camera movement.

Two things were done, in this order:

1. **Cut-out mips now hold their coverage.** `content/texture.cpp` had said in as many words
   that this rescale was owed and that there was no cutout content in the engine to suffer
   without it. There is now. `TextureRole::Cutout` rescales every level's alpha by bisection so
   the share of texels over the threshold matches the top level's, and `grass.sh` caps the level
   a card may read at 3 — below that the sampler, which clamps at the picture's edge and not at
   a column's, averages in the tufts either side and a card dissolves into a smudge that changes
   as the camera moves. **Necessary and nowhere near sufficient.**
2. **Alpha to coverage.** `fs_grass` turns the cutout into a ramp about one PIXEL wide across
   the threshold — `fwidth` of the sheet's alpha, so the ramp stays a pixel wide whatever mip
   the card is reading, which is exactly where the crawl lives — writes it to alpha, and
   `BGFX_STATE_BLEND_ALPHA_TO_COVERAGE` turns it into sample coverage. A card edge lands on 1,
   2, 3 or 4 of the four samples instead of all or none.

**13.12 → 4.53 levels.** The field is now *more* temporally stable than the bare ground it
grows out of (4.89).

**What it cost to do that: the prepass.** Alpha-to-coverage reads the fragment's alpha as a
coverage mask, and the prepass writes the view DEPTH in its alpha channel. So the field left the
prepass and lays its own depth in the shade pass instead, `DEPTH_TEST_LESS | WRITE_Z` — tested
against what the prepass did write, so a tuft behind a house is still hidden by the house, and
written, so a tuft behind another tuft is hidden by it. SSAO no longer sees the field, which was
already what `fs_grass` wanted.

### What it costs: nothing. It still pays for itself.

Lorencia, 1920x1080, Release, vsync off, 4x MSAA, 400 frames, moving camera, two runs each.
**Median wall frame**, the only enforced number and the only one that means anything on Metal.

| | grass off | grass on | grass |
|---|---|---|---|
| in the prepass, hard cutout — open field | 4.03, 4.04 | 3.79, 3.76 | −0.26 ms |
| in the prepass, hard cutout — field, crowd 30 | 4.10 | 3.85 | −0.25 ms |
| in the prepass, hard cutout — town, crowd 30 | 4.32 | 4.29 | −0.03 ms |
| out of the prepass, alpha to coverage, 25 cards/m² — open field | 4.01, 4.04 | 3.90, 3.90 | −0.13 ms |
| **and at 49 cards/m², the finer sward — open field** | **4.03, 4.04** | **4.10, 4.09** | **+0.06 ms** |

Leaving the prepass gave back half the saving and no more. It was expected to cost about +0.3 ms
— the ground under the field is shaded and then covered now, where its own prepass depth used to
make that ground fail EQUAL and skip two blended material sets, a PCSS lookup and a lamp loop.
What paid most of that back is that the field's own prepass draw went away with it.

At the 25-cards sward the field was still faster than no field. Doubling the count to 49 to get
the finer blade look spends that saving and about 0.06 ms more. **So the field costs essentially
nothing**, and it no longer shimmers: crawl re-measured at the higher count is **4.78 levels
against the bare ground's 4.89**. This is Treyarch's result out of the Black Ops 4
retrospective above, arriving here for their reason: a card covers ground that would otherwise
run the ground shader, and the ground shader is two full blended material sets, a thirteen-tap
PCSS lookup, an SSAO fetch and a lamp loop. The card's shader is one texture read and the paint-
over list. Swapping one for the other on most of the ground is a saving, and it is bigger than
what the cards cost to raster.

So **the grass asks the budget for nothing**, which is the opposite of what the geometric field
asked for. `docs/budget.md` says so too.

### The other fault found and fixed

- **The field came out black.** The root tint was pitched at 0.55 and the AO floor at 0.30, and
  with MU's baked light over them the root of every tuft landed under a fifth of the turf it
  grows out of — dark blotches on grass rather than grass. A tint that MULTIPLIES painted art
  is a nudge and belongs near one. Compounding it, the ambient was read off the card's own
  normal, which on a vertical-ish quad points at the horizon and takes barely half the sky the
  turf beside it takes — a systematic darkening of the whole field. The hemisphere is now read
  off a normal pulled 62% back towards straight up, which is also the better answer physically:
  an open tuft sees most of the sky, not what one sideways blade sees. Frame mean at night went
  from 30.6 with grass against 31.3 without, to 31.6.
### Two more faults, found by eye and fixed

- **The whole field was blurred by two mip levels.** `grassSheet` worked a card's level out from
  `uv * sheetWidth` on BOTH axes, and MU's sheets are 256 by 64 — so a uv step of 1 along v was
  being called 256 texels when it crosses 64. Four times the derivative is two whole levels in
  `log2`, and every card in the field was reading two levels softer than it should. The level is
  per axis now, off the batch's own sheet size (Lorencia's two are 256×64, Noria's third is
  256×128), with a −0.4 sharpening bias on top that alpha-to-coverage has earned: the edges are
  antialiased by the coverage, so the filter no longer has to pick a level that avoids aliasing
  on its own. The cap past which a card averages in its neighbours' columns is computed from the
  sheet rather than passed, so it cannot go stale.
- **The hash was degenerate, and that is what "the randomness is not perfect" was.** It was
  `fract(sin(n * 12.9898) * 43758.5453)` off a seed the CPU fused as
  `column * 37 + row * 131 + index * 17.13`. Two independent failures: the seed reaches forty
  thousand on a 256-tile map, and at 5e5 radians consecutive float32 values are further apart
  than a twentieth of a radian — so `sin` was sampled on a coarse lattice and returned banded
  values, and the randomness repeated in bands across the map. And a seed fused by adding
  multiples collides: tile (c+11, r) card 0 lands within half a unit of tile (c, r) card 24, so
  whole cards were copies of cards a few tiles away, on a lattice. It is Dave Hoskins' hash now,
  taking the tile and the card index **small and separate** — the tile comes off `d0.xy`, which
  is already the corner the card grows from, so nothing is fused and no seed is sent at all.

  With the hash honest, the spreads were widened to what they were always meant to be: length
  0.46–1.48 of the sward (it was 0.62–1.28), yaw mostly the card's own rather than its bunch's,
  roll half again as wide, and **width off its own draw rather than off the colour tint** — the
  two had been the same number wearing two hats, which is what made the variation read as one
  axis instead of several.

### What this still owes

- **The field is not in the prepass, so SSAO does not see it.** Intended — `fs_grass` never
  read the SSAO buffer anyway, because half a resolution has nothing useful to say about a
  cut-out edge — but it means the turf under a tuft gets no contact darkening from SSAO. The
  height ramp is what stands in for it.
- **`--msaa 1` has no coverage to spread.** The field falls back to a hard cut there and will
  shimmer as it did before. Every frame number in this engine is a 4x number, so nothing that
  is measured is affected; a player who turns MSAA off gets the old edge.
- **The card grass is the whole field.** Past `grass_radius` the ground texture takes over with
  nothing between. There is no far band.
- **Nothing interacts with it.** No walker parts it, no wake lies behind him. MU2's Turf had
  both; Ghost of Tsushima's displacement buffer is the shape for it.
- **`wild.png` is unused.** MU2's meadow — seed heads, broadleaf, clover, daisies, buttercups,
  bellflowers, eight painted cells of them — is in `assets/effects/grass/` and nothing reads it.
- **The greens are first guesses** and have been judged on two shots, not by the user's eye.

### The knobs

All live-reloaded out of `sheets/lighting.json`, because a green is only ever judged with the
window open: `grass` (0 prices it), `grass_radius`, `grass_fade`, `grass_density`,
`grass_height`, `grass_aspect`, `grass_lean`, `grass_rank`, `grass_dry`, `grass_widen`,
`grass_root_tint`, `grass_tip_tint`, `grass_root_ao`, `grass_roughness`,
`grass_wind_strength`, `grass_wind_degrees`.

## Sources

- [Advanced Graphics Summit: Procedural Grass in 'Ghost of Tsushima'](https://gdcvault.com/play/1027033/Advanced-Graphics-Summit-Procedural-Grass) — Eric Wohllaib, Sucker Punch, GDC 2019
- [Grass in Ghost of Tsushima](https://tigerabrodi.blog/grass-in-ghost-of-tsushima) — a readable summary of the above
- [From the archive: Making a greener grass](https://c0de517e.com/017_vegetation_part2.htm) — the Black Ops 4 grass retrospective
- [Rendering a Million Blades of Grass: Helio's GPU-Driven Foliage System](https://pulsarnative.com/blog/2026-08-02-helio-foliage-system) — the 2026 budget table
- [Procedural grass rendering with mesh shaders](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/) — GPUOpen, after Jahrmann & Wimmer 2017
- [Grass Rendering Series Part 2: Full-Geometry Grass in Godot](https://hexaquo.at/pages/grass-rendering-series-part-2-full-geometry-grass-in-godot/)
- [2Retr0/GodotGrass](https://github.com/2Retr0/GodotGrass)
- [Techniques for a Procedural Grass System](https://ensapra.com/2023/05/techniques-for-a-procedural-grass-system) — chunking, density-vs-width, Hi-Z
- [Grass Rendering in Game Engine](https://haoranliang.com/grass-rendering) — a Ghost of Tsushima reimplementation
- [Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis) — GPU Gems 3, the original wind model
