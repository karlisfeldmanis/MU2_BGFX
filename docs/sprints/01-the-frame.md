# Sprint 1: the frame

**Proved by:** House01 on a plane, lit, shadowed and occluded at 1080p, with a per-view GPU
table here.

**Landed** 2026-09-20 as `63d53f6`. Under review; the tuning pass is still open.

## What is here

- **The six views, filled.** `src/gfx/renderer.cpp` is the whole frame: a texel-snapped sun
  split into a 2048² D16 map, a prepass writing view normal and view depth into one RGBA16F,
  half-resolution SSAO on twelve spiral samples from that target alone, a depth-aware 4×4
  blur, one PBR shade pass testing depth `EQUAL` against the prepass, and an ACES present.
- **A glTF loader** (`src/content/mesh.cpp`) on cgltf: positions, normals, tangents, one UV
  set, materials, and tangents derived per triangle where a model has a normal map and no
  `TANGENT`.
- **One closed material model**: albedo, normal, ORM, emissive, and the three flags. A
  cutout is decided from glTF's own `alpha_mode` when the model is read, and discards in
  every pass including the shadow and the prepass.
- **Instanced draws.** Drawables are grouped by mesh into one transient instance buffer that
  the shadow, prepass and shade passes all read.
- **The lighting sheet** (`sheets/lighting.json`), stat'd four times a second and re-read
  when it moves, so the look can be tuned with the window open. An unknown key is said in
  the log rather than ignored.
- **The model bench**: `--model` (a path under `assets/`), `--dist`, `--still`.

## Measured

`./run.sh --frames 200 --still --model world/lorencia/House01/House01.glb`, Release, vsync
off, 1080p, 19 draws.

| account | median ms | p99 | share | budget |
|---|---|---|---|---|
| shadow | 1.261 | 2.458 | 0.217 | 1.0 |
| prepass | 1.303 | 2.558 | 0.224 | 0.7 |
| ssao | 2.660 | 5.160 | 0.457 | 0.5 |
| shade | 2.114 | 3.818 | 0.364 | 2.3 |
| present | 4.316 | 7.009 | 0.742 | 0.5 |
| **gpu frame** | **2.004** | 3.689 | | 5.5 |
| **cpu** | **2.451** | 7.793 | | 3.0 |

fps 474 median. The budget is kept and the run exits 0.

**Read the share column, not the median.** The five accounts' medians sum to 11.7 ms inside
a frame whose whole GPU time is 2.0 ms, so the per-view timers are measuring encoder gaps
and the wait for the drawable as much as work. See the trap below. The share column is each
view's proportion of the timers applied to the frame's own measured time; it is indicative,
and it is why `present` is not treated as a real 4.3 ms.

**The CPU is still the thing to watch**: 2.45 ms against a 3 ms allowance with one house on
screen, and a 7.8 ms tail. Unchanged from sprint 0's empty frame, which says it is bgfx's
Metal encoding and the present rather than anything this sprint added — but it leaves very
little for a town and a crowd. Sprint 2 measures it again with real geometry.

## Found by running it

Three that would each have been a long hunt later.

- **Metal has no three-channel texture.** MU2's build is full of RGB8 PNGs, and `RGB8` is
  refused outright, `RGB8` asked for as sRGB refused twice over. `createTexture2D` returns
  an invalid handle, the material falls back to the 1×1 white, and the whole house draws
  white while looking perfectly well lit and normal-mapped. Widened to RGBA8 in
  `content/texture.cpp` after a `bgfx::isTextureValid` check; the cook step in sprint 3 is
  where this should stop happening at run time.
- **Every material in MU2's build is `doubleSided`.** So nothing in the town will ever catch
  a winding mistake. The bench's own ground was wound backwards and was simply not there —
  a black screen with a correctly lit house floating in it — and no model could have shown
  it. Worth remembering when the terrain is built in sprint 2: it is the one surface whose
  winding nothing else will check.
- **bgfx's per-view GPU timers are not a division of the frame.** On Metal each view is its
  own render pass encoder and the timestamps are taken at its edges, so the gaps between
  encoders, and the wait for the drawable landing on whichever view presents, are counted
  inside the views. The gate now enforces the frame's own `gpuTimeEnd - gpuTimeBegin` and
  the CPU, and reports the accounts as shares. An account can still fail a run, but only
  when the timers are coherent enough for its share to mean anything.

## What is not finished

- **The look is not tuned.** PBR, the reflections and the shadow all work and none is
  judged. The contact darkening from SSAO is not visible on the bench and wants proving or
  fixing; the ground reads bright; the sun's strength is a guess, since MU2's `sun_energy`
  is Godot's own unit against Godot's own diffuse. This is the open half of the sprint.
- **No culling at all yet.** Foundation 7 of `PLAN.md` is designed and unbuilt; there is
  nothing to cull with one house. It is sprint 2 and 3's work and the frame's real risk.
- The bench's ground is a flat untextured plane. The real ground is sprint 2.
