# Sprint 14: the shine

Opened 2026-09-24 on the user's asking how +3, +7 and +9 should look. The plus is carried
everywhere already (`sim::Held::refinement`, `ui::Standing::refinement`) and read by nothing
but the tooltip's colours. This sprint makes it show on the body, on the ground and in the bag.

**Proved by:** a +9 Dark Knight in the town shows the chrome band sliding over his plate and the
Shiny01 star on it, and a +3 sword lying on the grass pulses red, in bench shots and in the bag,
and the frame stays inside budget.

## What MuMain draws

`RenderPartObjectEffect` (`ZzzObject.cpp:9472`, tiers from 10284), read in sven-n/MuMain at
`2f5e176` because the copy in `LEGACY/reference/MuMain` is a fork that rewrote the tiers as
tables and a GLSL shader. The passes are the same in both.

| plus | base pass, times the item's light | additive passes after it, `GL_ONE, GL_ONE`, no depth write |
|---|---|---|
| 0-2 | as it is | none |
| 3-4 | `(L, 0.6L, 0.6L)` | none |
| 5-6 | `(0.5L, 0.7L, L)` | none |
| 7-8 | `x 0.8` | CHROME: Chrome01, repeat |
| 9-10 | `x 0.9` | CHROME, then METAL: Shiny01, clamp |
| 11+ | `x 0.9` | CHROME2 / CHROME4. A later season, and past `Refine.Cap` |

- `L = sin(WorldTime * 0.004) * 0.15 + 0.6` (`SceneManager.cpp:1283`): 0.45 to 0.75, a pulse
  of about 1.6 s. WorldTime is milliseconds.
- CHROME's UV (`ZzzBMD.cpp:1631-1686`): `u = n.z * 0.5 + wave`, `v = n.y * 0.5 + wave * 2`,
  `wave = WorldTime % 10000 * 0.0001`, a ten-second sawtooth. METAL's is still:
  `u = n.z * 0.5 + 0.2`, `v = n.y * 0.5 + 0.5`. `n` is the bone-rotated VERTEX normal, in MU's
  world axes, with no normal map and no view term.
- The additive passes are flat and unlit: the vertex colour is `BodyLight` set by
  `PartObjectColor` (`ZzzObject.cpp:6536`), keyed by an armour's set index and falling to entry
  0, orange `(1, 0.5, 0)`, for everything not listed, which is most weapons. **That table is
  Season 6's and is checked against 0.75, Season 3 ep 6 and 0.97d before it is kept.**
- The same ladder runs worn (the character's light), on the ground (terrain light + 0.3) and in
  the bag (unlit, white). Only the light going in differs.
- No particle is tied to +7 or +9. The drop sparkle (`CreateShiny`) is every drop's, whatever
  its plus, and is owed in `fx/litter.h`, not here.

## What MU2 did, and what is not taken

`client/core/Shine.cs` and `docs/refining.md:119`. Godot's Mobile renderer never draws an
overlay pass, so the ladder went into a per-surface copy of the standard material: the tint on
ALBEDO, chrome and metal onto EMISSION. That shape is taken, because it is also the cheap one
here. Its inventions are not taken without being judged again on this frame: the view-space
normal, the sheen coloured by the albedo's own hue in place of `PartObjectColor`, the pulse
softened to 0.85-1.0 at half strength, the roughness "polish", the +9 star as a camera-facing
matcap. Kept: no shine on materials named `skin` or `hair` (MuMain's HideSkin).

## The shape here

**Folded into the shade pass, not a second draw.** MuMain's extra passes are additive and
unlit, so adding their result inside `fs_shade` is the same picture with no extra draws, no
depth fight and no batch split: instances of one mesh can carry different pluses.

1. **Textures.** Already in `assets/effects` and `index.json` (`chrome`, `shiny`). Nothing to do.
2. **The plus reaches the instance.** `gfx::Drawable::refine`, into the instance's free float
   (skin row z), out as a flat varying. `Play::redress` hands each slot's plus to the figure,
   `Litter` reads `Lying::what`, the stages read `Standing`.
3. **The shader**, `fs_shade` and `fs_stage`: tint the diffuse before lighting, add chrome and
   metal after it, off `ng` in MU's axes. `u_refine` carries `L` and `wave` off the play clock,
   bound per draw (`bindShadeInputs`).
4. **The colour table**, cooked, after the version check.
5. **Sheet knobs**, marked invention: `refine_strength` for how an unlit add reads under this
   frame's light and bloom, `refine_tint` for how much of the literal darkening +3/+5 keep.
6. **Bench**: `--plus N` on the model and character benches; +3, +5, +7, +9 by day and night;
   the band must slide over the plate as the figure turns, which is what proves the axes.
7. **Windows**: a stage holding anything at +3 or more redraws every frame; priced.
8. **Measured**: a +9 suit on screen at 2K in `budget.md`; +0 pixels cost nothing new.

Not in this sprint: +11 and over, excellent and ancient (nothing drops them), the drop
sparkle, the ordinary swing's refined streak (`fx/streak.h`).

## Log

- 2026-09-24, step 1: Chrome01 (64x64, mean 57/255) and Shiny01 (16x16 star) were already
  cooked into `assets/effects/refine` and `assets/effects/drop` and named in `index.json`.
  Not upscaled: they are looked up by the normal, not painted on a surface, and MuMain
  filtered them linear at this size. Shiny01 is the one to revisit if the +9 star reads too
  soft on the bench.
- 2026-09-24, step 2: the plus reaches the fragment. `gfx::Drawable::refine` goes into the
  instance's `i_data5.z` and out as `v_refine` (TEXCOORD6), declared by all four fragment
  shaders the two vertex shaders pair with (prepass, shade, glow, stage) so Metal still
  links them. `Figures::dress` takes each worn piece's plus and the two hands' and keeps them
  beside the parts (`FigureBody::partRefine`, `HeldItem::refine`); `Play::redress` reads them
  off the satchel. `Litter` keeps each drop's, the stages read `Standing::refinement`. Nothing
  reads `v_refine` yet: a 600-frame Lorencia run links every program and looks as before.
  Landed inside `4997b117` ("the shade under the bridges is back"): staged in the shared
  index as another session committed, and not split out afterwards while that session works
  on top of it.
- 2026-09-24, step 3: the ladder draws. `shaders/shine.sh` holds it, included by `fs_shade`
  and `fs_stage`: the tint multiplies everything lit, the chrome and the star are added after,
  with MuMain's UVs off the vertex normal in MU's axes. `u_refine` is (g_Luminosity, wave,
  strength, tint amount), off the play clock; Chrome01 on stage 9 (repeat), Shiny01 on 10
  (clamp), handed over from the showing table by the play mode (`Renderer::setShine`).
  **Trap, found in the first shot:** stages 9 to 11 are the land's second layer in
  `fs_ground`, which binds them and then calls `bindShadeInputs`, so binding the shine there
  laid Chrome01 over the whole town's ground. `bindShine` runs in the mesh path only.
  A copy of the save with the suit at +9, the axe +7 and the shield +5: the plate shows the
  chrome bands, orange as `PartObjectColor` 0 makes them, and at strength 1 they saturate to
  yellow under exposure 1.5 and the bloom -- step 5's knob, judged on the bench (step 6).
