# The 2K audit: what the frame could still give up

Written 2026-10-05 from a read of the renderer, the shaders and bgfx's Metal backend, and a
search of Apple's, bgfx's and the literature's own advice. **Nothing here was run or
measured.** Every saving below is an estimate, and this project has a record of estimates
running high: the store-action patch was guessed at 1-2 ms and measured 0.23
(`docs/budget.md`), because Apple's GPU hides most bandwidth behind the next pass's work. So
read the ms column as an order for trying things, and measure each one with `--repeat 3`.

The machine is an **M1 Pro, 16-core GPU, 200 GB/s**. The frame is about 5.6 ms at 2560x1273
with the timers off. Its known split, from the Metal System Trace in `budget.md`: shade and
sprites 3.0 ms of fragment work, shadow 0.59 fragment and 0.63 vertex, present 0.54, SSAO
0.33 and its blur 0.21, prepass 0.24 fragment and 0.46 vertex, first bloom level 0.20. The
frame is fill-bound, and most of the fill is **ALU and texture reads in fs_shade and
fs_ground**. The items are ranked with that in mind.

## What came of it, 2026-10-05

Measured the same afternoon. Lorencia, 2560x1273, `--still`, vsync off, `--repeat 3` of 600,
the two binaries interleaved, load 8-10 (Defender and Chrome). Each step is against the build
before it; the picture is checked with shots at frame 120 under `--fixed-dt`, which are
bit-stable run to run.

| step | where | before | after | saved | picture |
|---|---|---|---|---|---|
| A1, the disc as tables | town 140,126 | 8.59 | 7.18 | **1.41** | 25 pixels off by one level |
| A2, Private targets | town | 7.160 | 7.167 | none | identical; **reverted** |
| A3, grass first | field 190,110 | 7.15 | 7.02 | **0.13** | 247 pixels, max 85 (below) |
| A4, layer means | town | 7.18 | 7.16 | 0.017 at most | not built: the bound, with a constant |
| A5, SSAO | town | 7.23 | 7.09 | **0.14** | no pixel off by more than one level |
| A6, prepass varyings | town | 7.08 | 7.08 | none | bit-identical; **reverted** |
| **all kept** | town | **8.61** | **7.05** | **1.56** | 116 fps to 142 |
| | field | **8.31** | **6.92** | **1.40** | |

**A1 was worth ten times its estimate.** Not the trig alone: the tap count rode a uniform, so
neither loop could unroll, and every tap was a dependent sqrt, cos and sin in front of its
texture read. Fixed-length loops over constant tables let the compiler issue the reads
together. A shader loop whose bound is a uniform is the first thing to look for next time.

**A3 found a latent fault.** `submitGrass` bound none of the shade pass's shared inputs -- the
sun, the sky, the shadow map, the lamps -- and had been drawing on whatever the ground's
draws left bound before it. Drawn first, the whole field went dark. It binds its own now
(`bindShadeInputs`). Its remaining pixels and the town's ~1000 scattered ones were traced to
another session's game changes compiled into the newer build, not to this: with both A3
edits taken out again the same pixels differed.

**A2 and A6 were wrong guesses**, and the cost of finding out was a build each. Either
Apple's GPU already compresses these Managed targets or the bandwidth was hidden; and the
vertex outputs were not on the frame's path.

## A. Same picture: try these first

| # | what | where | est. at 2K | effort |
|---|---|---|---|---|
| A1 | PCSS disc offsets as constants, not sqrt+cos+sin per tap | shaders/shadow.sh:17, 82-123 | 0.1-0.3 | small |
| A2 | Render targets in Private storage (lossless compression) | bgfx renderer_mtl.cpp:4519 via patches/ | 0.05-0.3 | small |
| A3 | Grass drawn before the ground in the shade view | src/gfx/renderer.cpp:1005, 1027 | 0.05-0.2 in fields | small |
| A4 | Ground layer means as uniforms, not three mip-16 reads | shaders/fs_ground.sc:238-240 | 0.02-0.08 | small |
| A5 | SSAO spiral by one rotation; blur reads depth from the SSAO target | fs_ssao_body.sh:49, fs_blur_body.sh:19 | 0.05-0.15 | small |
| A6 | A prepass vertex shader that writes only what fs_prepass reads | shaders/vs_static.sc, vs_skinned.sc | 0.02-0.1 | small |
| A7 | Tonemap through a LUT in the present's five sharpen taps | shaders/fs_present.sc:143-170 | 0.1-0.2 | medium |

**A1, the shadow disc.** `vogel()` computes `sqrt`, `cos` and `sin` for every tap, and the
default still mode takes 9 + 16 = 25 taps. The tap count depends on a uniform branch, so the
compiler cannot fold any of it. That is 75 transcendentals per lit pixel, in both fs_shade
and fs_ground, on most of the screen. In still mode the phase is 0, so the offsets are fixed
numbers: write two `const vec2` tables (9 and 16) and loop over them. For the turned mode,
take the table and rotate it once by `cos(phase)`/`sin(phase)`, which is one sincos per pixel
instead of 25. The output is the same up to float rounding. **This is the best quality-
neutral item on the list**, since it removes ALU from the frame's largest pass. The
`--shadow-noise screen` row (0.91 ms for 12 fewer taps) shows how much each tap costs.

**A2, Private render targets.** bgfx creates every render target that is neither write-only
nor depth as `MTLStorageModeManaged` on macOS (`renderer_mtl.cpp:4519`). Apple's GPUs
compress textures and render targets losslessly in memory, but only Private textures get
this automatically, and Managed/Shared textures do not. So the shade resolve, the
prepass, the SSAO pair, all ten bloom textures and the probe cubes are probably written and
read uncompressed. A render target is never read back by the CPU here, so it can be Private.
That is a one-line change in the existing patch: `|| renderTarget` beside `writeOnly`. Check
it in an Xcode GPU frame capture first, whose texture inspector shows each attachment's
compression. `upscaled_` carries COMPUTE_WRITE (ShaderWrite), which also blocks compression,
but MetalFX needs that flag.

**A3, grass first.** Grass is drawn in the shade view with LESS and a depth write, and the
ground is drawn EQUAL against the prepass. Inside a view, bgfx sorts by program, and the
ground's program was created first, so today the ground under the field is fully shaded
(two or three material sets, PCSS, the lamp loop) and then covered. `grass.md` priced this
at +0.13 ms at 1080p when the grass left the prepass. Drawn first, the grass writes its
nearer depth, and the ground then fails EQUAL on every pixel the grass covers on all four
samples, so it skips. The picture does not change: those samples already showed grass.
Either give the grass its own view on `shadeFb_` ordered before `ViewShade` with
`bgfx::setViewOrder`, which keeps one render pass because the target is the same, or set the
shade view to Sequential. The saving only shows where the field is dense.

**A4, the ground's layer means.** The relief blend reads `texture2DLod(..., 16.0)` for each
layer to get its mean brightness. That number is constant for the draw. Compute it once on the
CPU at load (the last mip's texel) and pass it as a vec4 beside `u_groundRelief`. This
removes three dependent reads from every ground pixel.

**A5, SSAO and its blur.** The spiral computes `cos` and `sin` for each of its 12 taps.
Rotate a constant table by one per-pixel sincos instead, the same change as A1. The blur
takes 17 `texelFetch` reads from the 4x MSAA RGBA16F prepass (a sample-interleaved texture
at full resolution) only to get depth. Have the SSAO write `vec2(ao, depth)` into an RG16F
target at half resolution, and the blur reads 16 compact texels instead. The output is
near-identical: the blur's taps land on half-texel offsets, so a few may pick a
neighbouring full-res pixel's depth.

**A6, varyings.** On Apple's GPUs everything a vertex shader outputs is written to memory
between binning and shading. `vs_static` writes 26 floats (104 B) per vertex in every
pass, and fs_prepass reads 8 of them (uv, view normal, view position). fs_shade never reads
`v_vnormal` or `v_vpos`. A `vs_static_prepass` and `vs_skinned_prepass` that output only
those three need only a fs_prepass that names the same list, so bgfx's Metal link rule
still holds. The prepass's 0.46 ms of vertex time is the account this comes out of. That
work overlaps fragment work, so wall time will move less than that.

**A7, the present.** With sharpen on, `seen()` runs five times per pixel, and each run is a
NaN clean-up plus the full tonemap (AgX's `log2` and polynomial, or Hill's two matrices). This
is the measured 0.32 ms of the sharpen. Bake the tonemap into a 32³ RGBA16F 3D texture over a
log2 shaper, as Unreal and Unity do, so each tap costs one cached 3D fetch. Rebuild it when the
sheet's `tonemap` changes. The grade, sRGB and contrast still run once, after the sharpen.
This is close to the same picture rather than identical, so compare it with
`tools/shotcheck.py`. When the world is stretched (`--scale 0.99`) the sharpen is already
off, so this does not affect the user's play setting.

## B. Small picture changes: A/B with shots, then the user judges

| # | what | est. at 2K | the risk |
|---|---|---|---|
| B1 | Shade target RG11B10F instead of RGBA16F | 0.1-0.4 | banding in dark blue gradients: test the night look |
| B2 | Prepass resolved to one sample instead of MSAA_SAMPLE | 0.1-0.3 | a thin AO halo at silhouettes |
| B3 | PCSS blocker search on a 1024 min-depth copy of the shadow map | 0.1-0.3 | a slightly different penumbra width |
| B4 | Bloom chain in RG11B10F, and dual-filter taps for the levels below the first | 0.05-0.15 | the glow's shape |
| B5 | Probe cubes in RG11B10F; skip the cube read on rough dielectrics | 0.05-0.2 | a faint sheen lost on rough stone |

**B1.** The shade colour is 4x MSAA RGBA16F, which is 32 B per pixel of tile memory plus 16
for D32F. RG11B10F halves the colour. That means larger tiles, and half the resolve and every
later read (bloom, present, MetalFX). Apple's GPUs render to and blend into it. Destination
alpha must be unused: fs_shade writes alpha for the source factor of the fade blend, and
nothing downstream appears to read the target's alpha, but check this. The risk is
precision: 6-bit mantissas on red and green and 5-bit on blue, so a slow dark-blue gradient
can band and shift hue (Bart Wroński's write-up). The moonlit Lorencia night is exactly
that case, so judge it there.

**B2.** The prepass colour carries `MSAA_SAMPLE` so SSAO can read one real sample, which
means all four samples of an RGBA16F are stored: about 104 MB a frame at 2560x1273, and the
largest write-back the store patch cannot drop. Resolving instead stores 26 MB and gives
SSAO a plain texture. The cost is that edge pixels hold an averaged normal and depth. At
half resolution under a depth-aware blur that is probably invisible, but `common.sh` gives
the reason the current choice was made, so the user should compare shots of a figure
against far ground. A lighter version keeps MSAA_SAMPLE and only packs the prepass into 4 B,
for example an octahedral normal in RG8 and depth in BA as 16 bits. That halves the store
with no averaging, at the cost of some pack/unpack ALU.

**B3.** `budget.md` found that the 4096 map's cost is cache misses in the PCSS taps, not fill:
at half scale, 1024 measured the same as 4096. The blocker search only needs "is something
nearer, and how far on average". Build a 1024 min-depth (or min/max) downsample once after
the shadow pass, as one screen pass, and run the 9 search taps on it. They then stay in cache,
and the 16 filter taps keep the full map. The penumbra estimate becomes conservative rather
than exact.

**B4.** Ten bloom textures in RGBA16F, nine render passes. The first level (13 taps at half
resolution, 0.20 ms) is the expensive one. RG11B10F halves every level's bytes, and the levels
below the first could take Bjørge's dual-filter (4 taps down, 8 up) in place of 13 and 9.
Keep the 13-tap Karis first level: it is what stops a flame's pixel flickering.

**B5.** The cube read was priced at 0.22 ms at 1080p. The cubes are RGBA16F with mips and
could be RG11B10F. On a dielectric at roughness near 1, the reflection's weight
(`envBRDFApprox` at f0 0.04) is a few percent, and `skyPrefiltered` (pure ALU) would stand in
for it there.

## C. Structural: worth a sprint each, not a tweak

**C1. Cache the static shadow.** The sun is fixed (52°), and Lorencia's 2753 placements
never move. Render static casters into a cached map that is rebuilt only when the
split's snapped window has moved past a margin. Each frame, copy that map into the working
one and draw only figures, fading things and swaying meshes over it. The shadow is 0.59
fragment + 0.63 vertex in the trace. The literature puts static/dynamic splitting near a 2x
saving of the shadow pass. Sway is the complication: trees that sway must stay in the
dynamic list, or their shadows freeze.

**Measured 2026-10-05, then declined by the user** ('lets better not cache shadows'). With the
map frozen after warm-up, the frame fell 0.76 ms at the town and 0.65 in the field (7.04 to
6.28, 6.88 to 6.23); drawing the skinned casters over the frozen map gave back 0.11 and 0.07.
So about 0.65 ms of the shadow pass is static casters, before a cache's own costs (a restore
of the map each frame, and the scroll as the split follows the hero). Not built.

**C2. MetalFX temporal at 0.67-0.75 scale, in place of 4x MSAA.** This is the largest
possible saving: the 0.62 of MSAA, plus everything that scales with pixels at a quarter to
half fewer of them, minus MetalFX's own cost. It needs a velocity buffer (skinned and
swaying meshes included), sub-pixel jitter in the projection, and a reactive mask for the
additive effects. The picture changes, and painted MU art under TAA-style resolve can look
soft. It is the user's decision, and a sprint of its own.

**C3. Half precision by hand.** bgfx's shaderc turns `mediump` into a no-op, and SPIRV-Cross
drops RelaxedPrecision on the way to MSL (bgfx #1086, SPIRV-Cross #208). So `half` is
reachable only through hand-written `.metal` source packed into bgfx's shader binary. On M1,
fp16 runs at the same rate as fp32. The benefit is fewer registers (more threads in flight)
and smaller varyings, not double speed. It would mean twin shaders to maintain for fs_shade
and fs_ground. Not recommended before everything in A and B is done.

### C4. The town's scenery culled and drawn by the GPU, built 2026-10-09

The user: "we want to try to move more processes to gpu". A Release profile of Lorencia's
town first (`sample`, 8 s at ~190 fps): the main thread waited on the GPU for about 1% of
its time, and **about 54% of it was bgfx's Metal encoding**: per draw, textures, samplers and
pipeline state bound again and the draw issued, for 1,100-1,500 draws a frame. Renderer::draw
built the batches in another ~15%, posing the crowd ~5% (0.13 ms, so animation on the GPU
would buy nothing), and bgfx making a new MTLBuffer for every buffer update ~5%. By pass, in
mesh draws a frame: shadow 280 (161 of them solid static parts of the resident casters),
prepass 195, shade 195, probe 88 on its frames, sprites ~300.

`gfx/scenery.h` takes the 161: every static town model's vertices and indices in one buffer
pair, three compute steps on the sun's view (`shaders/cs_cast_*.sc`: nought a count a
model, test each placement's sphere against the split and copy the ones inside into their
model's run, write one indirect draw a solid part), then two submits that Metal runs back to
back with nothing bound between them. Cutouts, casting glows and skinned models keep their
own draws. `--cpu-scenery` is the old way, for A/B.

**The picture is identical**: shots with and without, bit for bit, in Lorencia, Atlans (the
swaying plants), Devias, Noria, the Lost Tower and the Dungeon. Measured in Lorencia's town,
1080p, interleaved, two rounds each, load 12 (other sessions on the machine):

| | GPU cull | batches | |
|---|---|---|---|
| draws a frame | ~1040 | ~1140 | |
| frame, 1080p, `--repeat 3` | 5.098, 5.098 | 5.113, 5.114 | -0.015 |
| frame, `--scale 0.5 --no-metalfx` (the CPU's share) | 2.685, 2.657 | 2.746, 2.700 | -0.05 |
| shadow view timer, `--views` | 2.168, 2.171 | 2.272, 2.277 | -0.10 |

**Small, and why.** The shadow pass is the cheap end of the encoding: one texture a draw,
against four and the shine in the prepass and shade. And the frame is near even between the
CPU and the GPU at 1080p, so taking work off one shows little until the other is cut too.

**The camera's prepass and shade, the same day** (the user: "do next"). Lorencia's 142 solid
static parts use only 12 sizes of sheet, and a part's albedo, normal and ORM always share one,
so each size's three sheets become texture arrays (blitted on the GPU at load, over a few
frames: a frame holds 1,024 blits and Lorencia wants ~1,750), a layer a part. The instance grew
a seventh vec4 (`Renderer::kInstanceFloats` 28, 112 bytes) that carries a merged part's layer
and material numbers, and `fs_*_merged` read their sheets by it (`common.sh`'s
`MU2_MATERIAL_ARRAYS`). Each frame, on the prepass's view: `cs_scene_rank` (a thread a model)
walks the model's placements in the cook's order and ranks the ones in the camera's frustum,
`cs_scene_cull` copies each to its rank in each of its parts' runs, `cs_scene_args` writes a
draw a part. Lorencia: 137 part draws become 9 groups, two submits each in the prepass and the
shade.

Three things found on the way, each now in the code:
- **One vertex shader, not a twin.** A `vs_static_merged` drew the merged bodies, and Noria's
  mushrooms lost their purple: a glow lies exactly on its body and is depth-tested against it,
  and two compiled shaders computing the same position do not land on the same bits. vs_static
  passes the seventh vec4 on for every static draw instead (and vs_skinned a constant, as the
  fragment shaders are shared).
- **Never a one-layer array.** bgfx makes one layer a plain 2D texture, which an array sampler
  reads as nothing; a size with one part is padded to two layers.
- **Ranked, never atomic.** An atomic append put a run's placements in a different order every
  frame, and where two parts meet at equal depth the order decides the pixel: two GPU runs of
  the same command differed by ~2,600 pixels, a shimmer on a still screen. Ranked, the GPU path
  is identical run to run.

Against `--cpu-scenery`, the picture differs only at seams where two parts meet at the same
depth, which is now settled in a different (fixed) order: shot at frame 150, pixels off by
more than 32 of 255 -- Lorencia 4, Noria 21, Devias 28, Atlans 83, the Lost Tower 121, Tarkan
414 (thin lines along the dead trees' seams, invisible at 1:1), the Dungeon 0.

Measured in a private worktree at HEAD (the shared tree held another session's crash), Lorencia's
town, two rounds each:

| | GPU | batches | |
|---|---|---|---|
| CPU time a frame (process user+sys, 3,000 frames less 600) | 3.187, 3.162 | 3.425, 3.420 | -0.25 (7%) |
| wall frame, 1080p, `--repeat 3` | 5.022, 5.024 | 5.112, 5.127 | -0.10 |
| wall frame, `--scale 0.5 --no-metalfx` | 2.681, 2.689 | 2.744, 2.704 | -0.04 |

**The CPU is no longer what holds the frame.** It spends ~3.2 ms of the 5.0 at 1080p; the rest
is the GPU and the drawable. What is left on the CPU's encoding is the figures (skinned, a
palette row each), the cutouts (foliage reads its alpha), the glows and the sprites. The next
CPU saving is the sprites' 270-380 draws; the next frame saving is GPU work.

**The sprites, the same day: 375 draws to 1, and no CPU to show for it.** Counted first: of the
run breaks in the town's sorted list, four in five were a change of blend or program (a lamp's
flame beside its glow), one in five a change of sheet, and the town has only six sheets. So
texture arrays alone would have taken a fifth. Instead `fs_effect` is now the only program,
the old fs_flame, fs_smoke, fs_dust and fs_breath its functions, picked by a kind on the
vertex; each draw binds seven sheets (s_albedo and s_sheet1..6 on stages 9-11 and 13-15) and
the vertex says which; and every kind but Minus shares (ONE, INV_SRC_ALPHA), an added kind
writing alpha 0, which is exactly (ONE, ONE). A run ends only at a Minus sprite or an eighth
sheet. The town's ~1,200 sprites are one draw, a Meteorite shower's 300 draws one, the frame
941 draws to 563.

The picture is the same: shots against HEAD at 1280x720 under `--fixed-dt` are identical in the
Meteorite and Fire Ball benches (Fire Ball's smoke is Dust), and off by one level in 1-2 pixels
in the town; the Budge Dragon and Lost Tower Shadow arenas (Breath, Minus) differ only as much
as HEAD differs from itself there, the Devil Square clock and a few drifting pixels.

But the time: CPU a frame (3,000 frames less 600, three rounds interleaved, load 9-14) 3.20 ms
against 3.16, inside the spread; the wall frame 5.32 both. A sprite draw bound one sheet on
the frame's transient buffers, which bgfx encodes for next to nothing -- unlike a scenery draw
with four sheets, the shine and its instance data. **Draw count is not the CPU's cost; what a
draw binds is.** What is left on the CPU's encoding is the figures and the cutouts, each with a
material's worth of sheets, and the next frame saving is GPU work.

### At 2K, after C4: what the frame's pixels cost, and two cuts, 2026-10-09

The user, on the sprites: "on 2k it will be improvement". It was not: 6.51 ms before and after at
2560x1273, since a draw saved is CPU and the CPU waits on the GPU. So the GPU's own accounts,
priced by switching each off (Lorencia 140,126, `--still`, 2560x1273, `--repeat 3`, two rounds,
load 9-14): SSAO ~0.5 ms, the PCSS taps (`--shadow-noise screen`) ~0.45, the reflections ~0.4,
the lamps ~0.4, bloom ~0.3. And **Hi-Z occlusion culling is not worth building**: skipping
every one of the camera's GPU-culled scenery draws, seen or hidden, moved the frame by nothing
(7.34 against 7.31 and 7.37), and occlusion could only ever take the hidden part of that.

Kept, against the build before, three rounds each:
- **A lamp's reach is tested before its third row is read** (`lights.sh` lampOne): a cell lists
  every light reaching any of its two metres, so most of the lights a pixel walks miss it, and
  each now costs two fetches, not three. 7.335 to 7.273, 7.338 to 7.274; 1-2 pixels off by one.
- **The probe turns every eighth frame while its eye stands still** (`drawProbe`), every other
  while it moves or before the first cube is whole: a cube three times a second at 180 fps,
  which still carries a lamp's flicker or a passer-by. 7.31-7.34 to 7.08 standing; no pixel off
  by more than one level. Walking costs what it did.

- **The turned shadow taps are the default** (`app/options.h`), 5+8 turned by screen noise for
  the still 9+16: the user, shown both at 2560x1273, "this is good". The Shadows row is the
  map's side alone now, Low 1024, Medium 2048, High 4096; `--shadow-noise none` is the still
  taps. 7.04 to 6.62. With the two above, the town at 2K is **7.34 to 6.62 ms**, 136 fps to 151.

Tried and reverted: **a PCSS early out**, one compare where all nine search taps found a blocker,
for the sixteen filter taps: 7.04 to 7.08-7.09, slower in all three rounds -- a pixel in shade
does not skip its taps unless its neighbours do too. And **the SSAO's taps on a half-resolution R16F depth copy** (a compute step on
the SSAO view before its draw), so that eleven of its thirteen reads leave the 104 MB
multisampled prepass. No change (8.53 against 8.51, under load): the cache already held them.

## Looked at and left out

- **Clustered or tiled light culling.** The lamp grid already bins static lights per 2 m
  cell at load, and the four transient lights are a short uniform loop. Clustering pays
  only with many more lights. One cheap leftover: each lamp is three RGBA32F `texelFetch`
  reads (`lights.sh:107-109`), which could pack into two RGBA16F reads.
- **Memoryless allocation.** bgfx never creates `MTLStorageModeMemoryless` (bgfx #1296, open
  since 2017). The store-action patch already gets the bandwidth half of that. What is left
  is RAM, not frame time.
- **Tile shaders, imageblocks, framebuffer fetch.** These would merge the prepass, AO and
  lighting on chip, but bgfx exposes none of them. They would need raw Metal outside bgfx's
  frame.
- **Dropping the prepass for Apple's hidden surface removal.** HSR would cover the opaque
  overdraw, but SSAO needs the depth and normals before the shade, and 43% of instances are
  cutouts that defeat HSR. The prepass is right for this frame.
- **Variable rasterization rate.** It is built for eye-tracked foveation. A top-down camera
  has no periphery to coarsen.
- **bgfx's render thread, Metal 4 argument tables, residency sets.** These are CPU-side, and the
  frame is the GPU's (`budget.md` measured the render thread).

## Sources

- Apple, *Harness Apple GPUs with Metal*, WWDC20 10602: TBDR, HSR, load/store actions.
  https://developer.apple.com/videos/play/wwdc2020/10602/
- Apple, *Optimize Metal Performance for Apple silicon Macs*, WWDC20 10632, and *Metal
  Enhancements for A13 Bionic*: lossless compression of textures and render targets.
  https://developer.apple.com/videos/play/tech-talks/608/
- Apple, *Create image processing apps powered by Apple silicon*, WWDC21 10153: lossless
  compression, the storage modes and usage flags that disable it.
  https://developer.apple.com/videos/play/wwdc2021/10153/
- Apple, *Boost performance with MetalFX Upscaling*, WWDC22 10103.
  https://developer.apple.com/videos/play/wwdc2022/10103/
- bgfx #1296 (memoryless), #1086 (precision for Metal); SPIRV-Cross #208 (RelaxedPrecision).
  https://github.com/bkaradzic/bgfx/issues/1296 https://github.com/bkaradzic/bgfx/issues/1086
- Bart Wroński, *Small float formats – R11G11B10F precision*.
  https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/
- Marius Bjørge, *Bandwidth-efficient rendering*, SIGGRAPH 2015 (dual filter).
  https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_slides.pdf
