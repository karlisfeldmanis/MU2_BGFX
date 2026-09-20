# Conventions

Written before the first loader, on purpose. Every line here is something that has one
right answer and a silent wrong one. Most were paid for once already in MU4; the rest come
from MU2's build. Nothing in `src/` may quietly disagree with this page — change the page
and the code together.

## Space

- **The world is metres, and one tile is one metre.** This is MU2's own scene scale, kept
  on purpose so that every number MU2 tuned — light ranges, the shadow's reach, camera
  distances, speeds — carries over unchanged. Lorencia is 256 m on a side.
- **The content is not in metres, and is converted on the way in.** MU's own files count
  100 units to a tile (`units_per_tile: 100.0` on every world in `index.json`), so a loader
  divides by the world's own value. `World.cs:222` in MU2 is the same division:
  `MetresPerTile => perTile / 100f`. Read the world's value; never hard-code 100.
- **MU's axes are not ours.** MU stores a placement with **z up** and **y running south**.
  A stored `(x, y, z)` becomes `(x, z, -y) / units_per_tile`, which is `World.cs:350` and
  the three lines like it. Skip the swap and the town lies on its side.
- **A map is `size` tiles on a side**, 256 for Lorencia, from the same entry.
- **Column is +x and row is −z.** A tile's centre in metres is
  `(column + 0.5, y, -(row + 0.5))`. The row is negated because MU's world runs south as −z
  while the attribute grid's rows count south as increasing index — `Terrain.Tile` in MU2's
  shared code does the same division and the same negation. Getting the sign wrong mirrors
  the map about the x axis, which still looks like a map.
- **A height byte is worth `height_factor` units**, 1.5 for Lorencia, and is divided by
  `units_per_tile` like everything else.
- MU4's world was also 1 m to a tile, but nothing else of MU4's scale transfers: its arena
  was its own.
- **Y is up. Right-handed, as glTF is — and bx must be told so every single time.**
  `bx::mtxLookAt`, `bx::mtxProj` and `bx::mtxOrtho` all default to `Handedness::Left`, so a
  call that omits the argument silently builds a left-handed frame. Every one of them takes
  `bx::Handedness::Right` explicitly, with no exceptions.
  A left-handed view does not look broken, which is what makes it expensive: it mirrors the
  image left to right, so a map still reads as a map, and it makes view-space z **positive**
  in front of the eye. That second half turned the prepass's `-v_vpos.z` depth negative on
  every pixel, which sent the SSAO down its "this is sky" path for the whole screen — a pass
  that ran, cost time and changed nothing. It also made a correctly wound ground plane
  invisible, which looked like a winding bug and was not.
- bgfx's homogeneous depth and origin are asked for at run time
  (`bgfx::getCaps()->homogeneousDepth`, `originBottomLeft`) and never assumed; measured on
  this Mac's Metal, both are false.
- **A model looks down +z.** MU2's build exports them so. A figure's yaw is therefore the
  negative of the direction of travel's angle under `bx::mtxSRT`, which was MU4's trap 4.
- **The map grid is `[y][x]`**, row-major, and so are `height.png`, `light.png` and
  `attributes.png`. Tile (x, y) is pixel (x, y) with y counted from the top of the image.
  Reading it the other way makes a map that looks plausible and is transposed.
- **A tile's height** is `height.png`'s red channel; the world y in metres is on the
  world's json, not guessed.

## Matrices

- Row-vector, as bx and bgfx use: `v * M`, translation in row 3.
- **`bx::mtxFromQuaternion` writes a column-vector matrix into that layout**, which is the
  inverse rotation. Do not use it. `core/maths` builds its own, and it is the only place a
  quaternion becomes a matrix.
- Bone palettes are the skin matrix already multiplied by the inverse bind, three `vec4`
  rows a bone in an RGBA32F texture.

## Colour

Everything is lit in linear space and written out once.

| role | file | sampler |
|---|---|---|
| albedo, emissive | BC7 sRGB | sRGB flag on, the hardware converts |
| normal | BC5 two-channel, z rebuilt | linear, never sRGB |
| ORM (occlusion, roughness, metal) | BC7 linear | linear |
| height, light, attributes | 8-bit PNG, uncooked | linear, point sampled |

`light.png` is MU's baked terrain light, which is already a lit result and not an albedo:
it multiplies the ground's diffuse and does not go through the sRGB sampler twice.

**A picture has mips; a grid does not.** Albedo, normal, ORM and emissive carry a full mip
chain and are sampled trilinear with anisotropy — MU's shallow camera minifies hard, and
without the chain the town shimmers and reads more texels than it shows. `height.png`,
`attributes.png`, `light.png` and the tile grid are data and are point-sampled with no mips:
a mipped attribute grid averages walkable together with blocked and nothing complains.

**A cutout's alpha is not rescaled yet, and this page used to say it was.** Averaging alpha
down a chain thins a leaf until it vanishes at distance, and the answer is to rescale each
level's alpha so it holds the coverage the top level had. `content/texture.cpp` does not do
it: it averages alpha flat and says so on the line that does it. That rescale belongs in
`tools/cook.py` in sprint 3, with the BC7 blocks, because it wants the whole chain in hand
and it is not worth paying for at load — and until sprint 3 there is no cutout content in
this engine to thin. A rule nothing keeps is worse than no rule, so it is written here as
what is owed rather than as what is done.

The present pass is the only place tonemapping and the sRGB write happen: ACES, then the
backbuffer. Nothing else writes sRGB.

## Metal, and what it refuses

These are bgfx-on-Metal facts, found by MU4 the slow way.

- **An unsigned byte vertex attribute reads only into an unsigned shader type.** Joint
  indices are `uvec4`, never `ivec4`. Get it wrong and the pipeline is refused at link and
  the draw silently draws nothing — no error, no geometry.
- **`gl_FrontFacing` is the opposite sense** from the winding `BGFX_STATE_CULL_CW` keeps.
  Shaders flip a normal on the view direction instead of on that flag.
- **A depth texture can be read twice**, once through the compare sampler and once as plain
  depth on another stage. The soft shadow needs both.
- **Debugging a refusal**: a second build directory configured with `-DBX_CONFIG_DEBUG=ON`
  (`./build.sh --trace`) routes bgfx's own trace into our log, which is where the refusals
  become visible at all.

And this bgfx revision's own API, which is newer than most writing about it (pinned in
`bootstrap.sh`; these four cost an afternoon between them):

- **The window goes on `Init::swapChain.nwh`**, with the size, the colour and depth formats
  and the frame latency beside it. `Init::resolution` is gone, and `platformData.nwh` alone
  starts a renderer with nothing to present to.
- **`bgfx::reset(flags, &swapChain)`**, not `reset(width, height, flags)`. A resize means a
  `SwapChain` carrying the new size.
- **Per-view GPU times need `bgfx::setDebug(BGFX_DEBUG_PROFILER)`.** `Init::profile = true`
  is not enough: on Metal `profileViews` tests that debug bit, and without it `Stats::viewStats`
  stays empty and every budget account reads 0.000 — a frame that appears to cost nothing.
  There is no `BGFX_RESET_PROFILER` any more. The per-view timers cost about 0.1 ms of GPU
  over seven views, and for a pass that does almost nothing the per-view numbers are timer
  granularity rather than work: they add up to more than the frame's own total.
- **`requestScreenShot`'s path is used whole.** bgfx appends no extension; ours ends in
  `.png` or the shot is a file nothing opens.
- **`bgfx::Init`'s constructor fills the swap chain in; a bare `SwapChain` does not.** Init
  sets `formatColor` to BGRA8, `formatDepthStencil` to D24S8 and two back buffers, while a
  default-constructed `SwapChain` leaves the formats as `TextureFormat::Count`. Hand that
  bare one to `init` or to `reset` and CAMetalLayer throws on `setPixelFormat`. A resize
  keeps the description from init and changes only the size — passing a fresh one also drops
  `nwh`, which bgfx documents as a request for a *headless* device.

## Materials

One model, closed. Albedo, normal, ORM, emissive, and three flags: **cutout**, **two-sided**,
**skinned**. The shader variants are those three flags and nothing else. Anything MU2's
material library says beyond that is resolved at cook time into these fields.

- **MU's figures are single sheets with mixed winding** and are drawn two-sided.
- **Cutout is decided once at load, off glTF's `alpha_mode` and `alpha_cutoff`**, which is
  what MU2's pipeline writes — not by inspecting the albedo's pixels, and not guessed in the
  shader. It moves into the cook in sprint 3. A cutout discards in every pass including the
  shadow and the prepass. A mobile-renderer
  lesson kept: a shader that writes alpha without the discard goes transparent wherever
  depth write is off, which is what made MU2's grass blink.

## The frame

Six views in this order, and their ids are fixed so the budget accounts line up:

| id | view | account |
|---|---|---|
| 0 | shadow | shadow |
| 1 | prepass | prepass |
| 2 | ssao | ssao |
| 3 | blur | ssao |
| 4 | shade | shade |
| 5 | present | present |
| 6 | hud | present |

Depth is laid by the prepass; the shade pass tests `EQUAL` and writes no depth. Nothing is
shaded twice.

## Time

- The sim ticks at a fixed **20 Hz**, as MU does, on its own accumulator. It never reads the
  frame's delta and never allocates in a step.
- A per-frame rate needs its order: a speed takes one factor of the tick rate, an
  acceleration two. Getting that wrong reads as slow motion, not as a wrong number.
- What is drawn may lag what the sim decided, and smoothing between ticks is presentation
  only: aim, reach and hits use the sim's own values.

## Numbers

- Every measurement is taken **vsync off**, at 1920x1080, in a Release build, over a short
  run. A number taken against vsync is not a number.
- The budget is in `docs/budget.md` and its accounts are enforced by `--budget`.

## Rules

Fresh and smaller than MU2's, never looser. Every constant is traced to OpenMU's Version075,
MuMain's own C++ or `mu.db`, with the source named in a comment. A departure is allowed and
is marked `invention` on the line that makes it. MU2's `shared/` is read one file at a time
as a worked example; it is not ported.
