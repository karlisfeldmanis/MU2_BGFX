# Sprint 1: the frame

**Proved by:** House01 on a plane, lit, shadowed and occluded at 1080p, with a per-view GPU
table here.

**Done** 2026-09-20, after a QA review sent the first attempt back.

## What is here

- **The six views, filled.** A texel-snapped sun split into a 2048² D16 map with a
  contact-hardening PCSS filter; a prepass writing view normal and view depth into one
  RGBA16F; half-resolution SSAO on twelve spiral samples from that target alone; a
  depth-aware 4×4 blur; one PBR shade pass testing depth `EQUAL` against the prepass; an
  ACES present.
- **4x MSAA** on the prepass, the depth they share and the shade target, resolved by bgfx on
  the sample. Alpha-to-coverage rides along for cutout materials — written, but **untested**,
  because House01 has no cutout and the grass arrives in sprint 2.
- **Mipmaps**, generated at load in the light the texels stand for, with a role per texture
  deciding colour space, filtering and whether there is a chain at all.
- **A glTF loader** on cgltf, one closed material model, and instanced draws through one
  buffer every pass reads.
- **The lighting sheet**, re-read four times a second, with MU2's own sun angles.
- **The model bench**: `--model`, `--dist`, `--still`, `--msaa`.

## Measured

`./run.sh --frames 400 --still --msaa 4 --model world/lorencia/House01/House01.glb`,
Release, vsync off, 1080p, 19 draws, 370 frames after warmup.

| account | median ms | p99 | share | budget |
|---|---|---|---|---|
| shadow | 1.648 | 3.176 | 0.342 | 1.0 |
| prepass | 1.776 | 3.303 | 0.368 | 0.7 |
| ssao | 4.762 | 7.799 | 0.988 | 0.5 |
| shade | 3.157 | 4.679 | 0.655 | 2.3 |
| present | 4.581 | 9.418 | 0.950 | 0.5 |
| **gpu frame** | **3.303** | 4.928 | | 5.5 |
| **cpu** | **1.824** | 6.516 | | 3.0 |

fps 640 median, budget kept, exit 0. **Read the share column**: the view timers sum to 15.9 ms
inside a 3.3 ms frame, because on Metal they count the encoder gaps and the drawable wait.

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
  sun's split is orthographic and the penumbra is a distance.
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
- **Alpha-to-coverage is untested** — no cutout content here.
- **The look is not judged against MU2.** The sun's two angles are MU2's exactly and the
  energies are not, because Godot's energy unit is not this pass's. Paired shots are sprint 8.
- The bench's ground is a flat untextured plane. The real ground is sprint 2.
