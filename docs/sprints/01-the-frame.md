# Sprint 1: the frame

**Proved by:** House01 on a plane, lit, shadowed and occluded at 1080p, with a per-view GPU
table here.

**Done** 2026-09-20, after QA sent it back **twice**. The second review found, among other
things, that the PCSS penumbra reported fixed on the first review had never been fixed; see
"The penumbra, and a claim this file made falsely" below.

## What is here

- **The six views, filled.** A texel-snapped sun split into a 2048² D16 map with a
  contact-hardening PCSS filter; a prepass writing view normal and view depth into one
  RGBA16F; half-resolution SSAO on twelve spiral samples from that target alone; a
  depth-aware 4×4 blur; one PBR shade pass testing depth `EQUAL` against the prepass; an
  ACES present.
- **4x MSAA** on the prepass, the depth they share and the shade target, resolved by bgfx on
  the sample. Alpha-to-coverage is set for cutout materials and this file used to call it
  "untested". It is not untested, it is **inert**: no shader in either multisampled pass
  writes the albedo's alpha, so the coverage is always full and the state does nothing.
  What to do about it is open at the time of writing and is not decided here.
- **Mipmaps**, generated at load in the light the texels stand for, with a role per texture
  deciding colour space, filtering and whether there is a chain at all.
- **A glTF loader** on cgltf, one closed material model, and instanced draws through one
  buffer every pass reads.
- **The lighting sheet**, re-read four times a second, with MU2's own sun angles.
- **The model bench**: `--model`, `--dist`, `--still`, `--msaa`.

## Measured

`./run.sh --frames 400 --still --msaa 4 --model world/lorencia/House01/House01.glb --budget`,
Release, vsync off, 1080p, 19 draws, 370 frames after warmup. Re-taken after the second
review, twice back to back; both exited 0.

| account | median ms | p99 | share | budget |
|---|---|---|---|---|
| shadow | 1.686 | 2.671 | 0.349 | 1.0 |
| prepass | 1.818 | 2.805 | 0.376 | 0.7 |
| ssao | 4.826 | 6.813 | 0.998 | 0.5 |
| shade | 3.188 | 4.177 | 0.659 | 2.3 |
| present | 4.295 | 7.984 | 0.888 | 0.5 |
| **gpu frame** | **3.271** | 4.246 | | 5.5 |
| **"cpu"** | **2.463** | 6.962 | | 3.0 |

**Read the share column**: the view timers sum to 15.8 ms inside a 3.27 ms frame, because on
Metal they count the encoder gaps and the drawable wait.

**Two figures in that table are not what they are called, and the right answer is not settled
here.** Written down as readings, for whoever settles them:

- The row called `cpu` is wall time across the whole loop, `bgfx::frame()` included, so it is
  a frame time and not CPU work. Its distribution over 370 frames is strictly bimodal and
  alternating: 184 frames averaging 0.210 ms (max 0.663) and 186 averaging 4.035 ms (min
  2.045), with nothing in between. The median therefore lands in the empty gap and is not
  reproducible — 2.463 ms here, 2.402 the next run, 2.614 and 2.959 on two earlier ones,
  against the 1.824 this file used to publish. The **mean is 2.133 ms** and is stable across
  runs (2.202 on the second). The published `fps median` is the same artefact from the other
  side: 489.0 on one run and 1485.9 on the next, same binary, same command, back to back.
- The `gpu frame` figure exceeds the frame time it sits inside: GPU mean 2.885 ms against a
  wall frame mean of 2.133 ms over the same 370 frames. A frame cannot hold 2.89 ms of work
  and arrive every 2.13 ms, so `gpuTimeEnd - gpuTimeBegin` counts waiting as well as work —
  the complaint `docs/budget.md` already makes about the per-view timers, and does not yet
  make about the frame, which is the figure the gate enforces.

MSAA, same scene: 1x 2.88 ms, 2x 3.26, 4x 3.15, 8x 3.29. The cost is the resolve and is
resolution-bound, which is why 4x and 8x sit within noise of one another. 4x changes 0.62% of
the pixels, half of them by a lot — a thin band on the silhouettes, which is what an
antialiased edge looks like and not a global shift.

**The SSAO is over its account** at a 0.99 ms share against 0.5, while shade sits at 0.66
against 2.3. The split was a guess made before anything was drawn and it is not rebalanced on
one house; sprint 2 has the geometry to decide it.

**The CPU is the frame's risk, still.** 1.82 ms median here, but a 6.5 ms p99 on a scene with
19 draws. Sprint 0 saw the same tail on an empty frame, so it is bgfx's Metal encoding and the
drawable wait rather than anything this sprint added — and it leaves little for a town.

## What QA sent back, and what it cost

The first attempt was reviewed by an agent that had not written it and was **rejected**: the
occlusion half of the proving sentence was not met. The root defect was one omitted argument.

- **`bx::mtxLookAt`, `mtxProj` and `mtxOrtho` all default to `Handedness::Left`.** Every call
  omitted it, so the frame was left-handed while `conventions.md` declared right-handed. That
  does not look broken — it mirrors the image, and a mirrored map still reads as a map — and
  it makes view-space z positive in front of the eye. The prepass's `-v_vpos.z` went negative
  on every pixel, so the SSAO took its "this is sky" path for the whole screen: a pass that
  ran, cost time and changed nothing. QA proved it with byte-identical PNGs at twelve times
  the radius and twenty-five times the strength.
- It also made a correctly wound ground plane invisible. That had been "fixed" by flipping the
  winding and **written up as a finding**, which it was not. Both are back.
- **The SSAO had a second, independent fault**: the radius was never projected. Metres over
  metres used as a pixel count put every tap a twentieth of a texel from the centre. Fixing
  the handedness alone would not have shown anything.
- **The texel snap added the remainder instead of subtracting it**, keeping the crawl it was
  meant to remove and adding a full-texel pop at every boundary. Invisible under `--still`.
- **The PCSS penumbra divided by the blocker's depth**, which is the point-light formula; a
  sun's split is orthographic and the penumbra is a distance. **This bullet was written
  before the fix was made, and the fix was not made.** See below.
- **The shadow bias was held in NDC**, hiding that 0.0015 over a 120 m split is 18 cm on a map
  whose D16 quantum is under 2 mm. Metres now.
- **`strtod` reads the byte past a number** and the file buffer is not terminated, so any json
  ending in a digit over-read the heap; QA crashed it with a guard page.
- **A resize reset the swap chain with a default-constructed descriptor**, whose null window
  handle bgfx documents as a request for a headless device.
- **The budget gate could no longer fail on an account**, so `--budget shade=0.001` passed —
  and that was sprint 0's own proving sentence. An override is a claim now, checked against
  the share regardless of timer coherence.
- **Derived tangents always got `w=+1`**, lighting mirrored UV islands from the wrong side.
- An empty frame never cleared the shade target and presented it anyway; two comments
  described behaviour the code did not have.

The lesson worth keeping: **three of these were invisible on this bench and would have been
found in sprint 2 as "the terrain is inside out" or "the shadows crawl".** A bench with one
double-sided model and a fixed camera cannot catch a handedness error, a winding error or a
snapping error. Sprint 2's ground is the first thing that will.

## The penumbra, and a claim this file made falsely

The second review found that the PCSS penumbra **still divided by the blocker's own depth**.
Three places said it had been dealt with: the bullet above, a comment in `renderer.cpp`
beside the scale it computes, and the commit that closed the first review. None of them was
true. The line in `shaders/fs_shade.sc` was untouched, and the divide had been running in
every frame and every shot this sprint published.

The arithmetic of what it cost. The split is orthographic with its depth range cut to
`back * 2 = 120 m` and the light's eye at `shadowRange = 60 m` back from the focus, so a
blocker near the focus sits at about `60 / 120 = 0.5` in the split's own 0..1 depth; QA read
0.475 off the bench. Dividing by that multiplies the penumbra by about **2.1x**, which is a
sun of `2 * atan(2.1 * tan(2°)) = 8.4°` against the sheet's `sun_angle_degrees: 4.0`. The
sheet was being overruled by a formula for a lamp.

The fix is the one subtraction it always was: `penumbra = (receiver - blocker) *
u_shadowParams.y`, because the C++ already folds `tan(halfAngle) * depthRange / shadowRange`
into that uniform. Measured on the bench by A/B — the divide put back, built, shot; taken
out, built, shot, same frame of the same still run: **2.254% of the 1920x1080 frame changed,
by at most 71 of 255, mean 7.4 over the changed pixels, and every changed pixel inside the
shadow's own bounding box.** The rest of the picture is byte-identical, which is what a
penumbra-only change should look like.

The lesson is narrower than the first review's and worth more: **a comment is not a fix, and
three agreeing comments are not three pieces of evidence.** All three were written in the
same sitting from the same belief. Nothing in the first review measured the penumbra.

## What the second review found besides

- **The mip generator dropped the last row and column at every odd level.** `y1 = y0+1 < srcH
  ? y0+1 : y0` against `dstH = srcH/2` means a 3-row source is read as its first two rows,
  level after level, so a chain walks towards the top left of the picture. The level sizes
  have to keep rounding down, because that is what bgfx computes when it walks a chain, so
  the last destination pixel takes the orphan in instead: three rows or columns, averaged
  flat. Even dimensions are byte-identical, proved on a transcription of the loop over 1, 2,
  3, 4 and 5-wide sources. On MU2's 384-square atlases it fires at the 3x3 to 1x1 level only,
  which no 1080p shot of one house can show; it is a cooked non-power-of-two atlas that would.
- **`--budget` could be switched off by asking for a short run.** The summary returned true
  before any check when nothing survived the 30-frame warmup, so `--frames 10 --budget
  gpu=0.001` exited 0. Asking for the gate and giving it nothing to judge is the caller's
  error now: exit 2. Proved — `--frames 10 --budget gpu=0.001` exits 2, `--frames 10` alone
  still exits 0, `--frames 120 --budget` exits 0, `--frames 120 --budget gpu=0.001` exits 2.
- **The log's clock was `clock()`**, which is this process's CPU time and not elapsed. The
  same 300-frame run read 0.32 s of log time and now reads 1.71 s, so every timestamp in
  every log this sprint wrote priced the run about five times fast. Steady wall clock now.
- **`docs/conventions.md` declared a cutout-alpha rescale nothing implements.** The page now
  says it is owed, and names where it lands: the cook, sprint 3. It also claimed cutout was
  decided at cook time from the albedo's pixels; it is decided at load from glTF's
  `alpha_mode`, and the page says that now.
- **Two comments described behaviour the code does not have.** `mesh.cpp` claimed MU2's build
  has one texcoord set a primitive — House01, this bench's own model, carries TEXCOORD_0 and
  TEXCOORD_1 on all four primitives and only the first is read. And `cutoutFor`'s comment
  said it looks for "any pixel that is neither opaque nor clear" while it inspects no pixel
  at all.

## Found on the way

- **Metal has no three-channel texture.** MU2's build is full of RGB8 PNGs; `createTexture2D`
  returns an invalid handle and the material falls back to white while looking perfectly well
  lit. Widened to RGBA8 after an `isTextureValid` check.
- **Every material in MU2's build is `doubleSided`**, so nothing in the town will ever catch a
  winding mistake.
- **bgfx's per-view GPU timers are not a division of the frame** on Metal.
- **An MSAA depth attachment must be `BGFX_TEXTURE_RT_WRITE_ONLY`** (or `MSAA_SAMPLE`), or the
  frame buffer is refused while every texture in it reports valid.
- **`bgfx::Init`'s constructor fills the swap chain in**; a bare `SwapChain` leaves the formats
  as `Count` and CAMetalLayer throws.

## What is deliberately not done

- **No culling.** Foundation 7 is designed and unbuilt; there is nothing to cull with one
  house, and it is the frame's real risk in sprint 3.
- **Alpha-to-coverage is inert** — the state is set, no shader writes the albedo's alpha in
  either multisampled pass, and the shadow pass it is also set on has a single-sampled
  target. Whether it is made real or taken out is an open decision, not a measurement.
- **The look is not judged against MU2.** The sun's two angles are MU2's exactly and the
  energies are not, because Godot's energy unit is not this pass's. Paired shots are sprint 8.
- The bench's ground is a flat untextured plane. The real ground is sprint 2.
