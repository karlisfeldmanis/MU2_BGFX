# Conventions

Written before the first loader, on purpose. Every line here is something that has one
right answer and a silent wrong one. Most were paid for once already in MU4; the rest come
from MU2's build. Nothing in `src/` may quietly disagree with this page — change the page
and the code together.

## Space

- **A tile is 100 units.** Not one. `index.json` says `units_per_tile: 100.0` per world and
  MU2 calls a unit a metre, so its Lorencia is 25.6 km on a side and every light range,
  camera distance and speed in MU2's numbers is in those units. Read the world's own value;
  never hard-code 100, and never assume MU4's 1 m per tile, which was its arena's own scale
  and is why none of MU4's distances transfer.
- **A map is `size` tiles on a side**, 256 for Lorencia, from the same entry.
- **Column is +x and row is −z.** A tile's centre is
  `((column + 0.5) * units, y, -(row + 0.5) * units)`, which is `Beast.cs` and `Cradle.cs`
  in MU2's shared code. Getting the sign wrong mirrors the map about the x axis, which looks
  like a map.
- **A height byte is worth `height_factor` units**, 1.5 for Lorencia, from the same entry.
- **Y is up.** Right-handed, as glTF is. bgfx's homogeneous depth and origin are asked for
  at run time (`bgfx::getCaps()->homogeneousDepth`, `originBottomLeft`) and never assumed;
  measured on this Mac's Metal, both are false.
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

## Materials

One model, closed. Albedo, normal, ORM, emissive, and three flags: **cutout**, **two-sided**,
**skinned**. The shader variants are those three flags and nothing else. Anything MU2's
material library says beyond that is resolved at cook time into these fields.

- **MU's figures are single sheets with mixed winding** and are drawn two-sided.
- **Cutout is decided at cook time** from the albedo's alpha, not guessed in the shader, and
  a cutout discards in every pass including the shadow and the prepass. A mobile-renderer
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
