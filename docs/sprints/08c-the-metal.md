# Sprint 8c: the metal

**Proved by:** a Dark Knight's plate by Lorencia's bonfire reads as steel by day and catches the
fire at dusk, from a reflection of the town round him and not of a sky gradient; judged by the
user from shots; the frame stays inside 5.5 ms.

Opened 2026-09-21 on the user's asking for metal reflections that are "very nice", after they
asked whether plate caught the bonfire at all. It did, as a lamp highlight; everything else it
reflected was the closed-form sky.

## Why the plan's cubemap is not what was built

PLAN.md's sprint 8 row says "one prefiltered cubemap per world, baked offline by a bench". Two
things since make a baked cube wrong here, and this is the senior's call, marked:

1. **The light moves.** The user has a day and night timer. A cube baked at noon reflects noon
   at midnight: bright sky in the plate of a man standing in the dark.
2. **The fires.** Sprint 8a/8b made the lamps and fires the thing worth reflecting at night, and
   a bake cannot hold a flicker or know which fire the player stands by.

So the cube is **live, round the player**, and re-rendered a face at a time.

## What was built

- **The probe** (`Renderer::drawProbe`, views 17 to 59, after the frame, read by the next one).
  A 128-texel RGBA16F cube taken 1.2 m above the camera's target: the chest of whoever it
  follows. One face every other frame, drawn with the shade program itself (sun, split shadow,
  lamps, MU's baked light) over the closed-form sky (`fs_probe_sky`), with the glows added.
  Figures are left out (a cube taken from inside the player reflects his armour's inside), and
  so is foliage (hundreds of small cutout draws that are only green at a reflection's blur).
  It draws the casters' list, which is the wider one: the camera's leaves out what is behind it.
- **The chain** (`fs_probe_down`): after each face, its six levels, each the box average of the
  raw texels under it. Its own texture, since a level cannot be drawn while the level above is
  read from the same one.
- **The filter** (`fs_probe_filter`): once all six faces are new, in a turn of its own, a
  prefiltered copy whose five mips are GGX lobes of roughness 0, 0.25, 0.5, 0.75 and 1, by
  filtered importance sampling (32 samples, each reading the chain at the level its solid angle
  asks for). A cycle is six faces and the filter: fourteen frames.
- **The shade pass** reads the copy at `roughness × 4`, with specular occlusion from the AO
  (Lagarde and de Rousiers) and a horizon fade for normal maps that bend `r` under the surface.
  Without a probe, and inside the probe's own faces, it keeps the closed-form sky.
- **`metal_gain`** (sheet, 2.5). **Invention.** MU paints its metal dark, with the shading in the
  paint: the plate and the shields sit near 0.05 linear, where iron's reflectance is about 0.55.
  Read as it is, armour returns a twentieth of the town round it and reads as grey card. The gain
  lifts a metal's painted albedo into its reflectance, keeping the engraving. Judged by eye
  against 1, 3 and 5: at 3 the plate is steel and the shield washes out when it faces the sun, at
  5 the plate goes flat. 2.5 is the pick, and the user's to move.
- **The check**: the sheet's `probe_view` N draws the cube's mip N−1 where the town is, looked up
  from the probe towards each pixel. Near the player that is the frame itself; a face turned or
  mirrored would show the town in the wrong place. It showed the right place first time.
- **Sheet**: `probe` 0 puts the closed-form sky back, which is how the cost is priced.

## Three traps, found on the way

1. **Attachment::init asks for a mip chain by default**, and bgfx refuses a frame buffer whose
   *depth* attachment asks for one. The face buffer failed to build with no message beyond
   "invalid". Depth is attached with `BGFX_ATTACHMENT_NONE`.
2. **bgfx's own mip generation was not trusted and is not used.** On Metal it runs when the next
   view binds a different frame buffer. The first design used it for the raw cube's chain, and
   the filter read black at every mip above 0. Whether the chain was late or never made was not
   settled before the explicit chain replaced it, and this file does not claim to know.
3. **Some of the town's far meshes shade to NaN.** A cluster at Lorencia's east horizon, seen from
   (184,135), found by painting `isnan` magenta in the probe's faces. The camera rarely sees them;
   the probe sees everything, and one NaN texel averaged into the chain became a NaN level that
   the wide lobes then spread over whole faces as black. The chain now skips texels that are not
   finite. **The NaN itself is not fixed**: which meshes, and which term, is open. A guarded
   tangent was tried and changed nothing, so it was taken out again.

## The frame

Lorencia, 1920×1080, Release, vsync off, moving camera, 600 × 3, twice each:
**+0.28 ms** (4.15 against 3.87). Parts, before the every-other-frame rule: faces 0.25, chain
nothing measurable, filter 0.04, the shade pass's cube read about 0.22. The probe account is
opened at 0.3, which makes the accounts sum to 5.8 against the 5.5 frame: see
`docs/budget.md`. The enforced wall frame is well inside.

## Open

- The NaN meshes (above).
- Which account gives up the 0.3.
- The shield at `metal_gain` above 2.5 whites out when it faces the low sun. The sun's own
  specular on smooth metal is right in kind; whether it is right in amount is the user's eye.
- Water: its roughness is 0.08 and it now reflects the probe. Not looked at.
- A walk: the reflection moves on in steps every fourteen frames. Judged still, not walking.
