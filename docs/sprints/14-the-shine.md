# The shine

Misnumbered, as `09-the-ring.md` is: `docs/roadmap.md` gives sprint 14 to the debts. The file
keeps its name because the code cites it.

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
- 2026-09-24, step 4: the colour and the level are MuMain's, per item. Code, not a cook:
  both are functions of MU's own group and number, which `ItemRow` carries, like the damage
  tables in `sim/items`. `game/shine.cpp`'s `shineOf(row, plus)`:
  * **Colour**, `PartObjectColor` cut to our rows: Bill of Balrog 1 (1, 0.2, 0); Silver Bow
    and Bluewing Crossbow 5 (white); Lighting Sword and Legendary Staff 2 (0, 0.5, 1); the
    armour by set -- Dragon 1, Legendary 3 (blue), Bone 5, Scale 6 (0.6, 0.8, 0.4), Plate and
    Wind 2 (blue), Spirit 4 (0, 0.8, 0.4), Guardian 5 -- and orange (1, 0.5, 0) for the rest.
  * **Level**, `RenderPartObjectEffect`'s overrides cut the same way: every jewel (Bless,
    Soul, Chaos) draws at +8, so a jewel always carries the chrome; the orb of summoning and
    the wings at +0; the later orbs at +9; bolts and arrows at `plus * 2 + 1`. Group 12 is
    numbered alike in OpenMU and MuMain (`docs/mu-scrolls-and-orbs.md`).
  The colour rides the instance's last free float at hundredths (`gfx::packRefineColour`),
  taken apart in the vertex shaders, so `v_refine` is now `(plus, r, g, b)`. The body keeps a
  `ShineLook` per part and per hand; drops and stages make theirs from the row. A +9 Plate
  suit on the save's copy shines blue where the Leather one shone orange.
  **The version check.** No 0.75, 0.97d or Season 1-3 client source exists to read. The
  oldest is a decompiled 0.97k main (~2005, github.com/aldomigge/Mu-97k-Client-Source:
  `PartObjectColor` at 0x00503CF0, `RenderPartObjectEffect` at 0x00504B50, the addresses
  confirmed by MuEmu-0.97k-kayito's `Offsets.h`). It has the same ladder -- the red and blue
  tints, chrome at +7, chrome and metal at +9 -- and a colour table equal to MuMain's for
  every entry this table uses (colours 0-6, sets up to 14). So the look is traced to 0.97k,
  not to 0.75, which nothing attests either way.
- 2026-09-24, step 5: the two knobs, `refine_strength` and `refine_tint` in the lighting sheet
  (`gfx::Lighting`, live like the rest), both 1 by default, which is MuMain's own. Marked
  invention. Swept on a +9 Plate suit in the town through private `--sheet` copies, so the
  shared sheet stayed as it was: the plate's brightest blue pixels read (36, 202, 230) at 1,
  (36, 159, 199) at 0.5 and (36, 115, 157) at 0.25 -- linear in the knob, so it works and the
  chrome is that bright. The same suit at +7 is nearly as blue, so the chrome, not the +9
  star, is what fills the plate. MuMain added the same bands onto an 8-bit frame and its +7
  Plate was strongly blue too; here they are added in linear light and then lifted by
  exposure 1.5 and the bloom. No value is written to the sheet yet: it is a look, judged on
  the bench (step 6) where an item is larger than the 40 pixels a hero is in the town.
- 2026-09-24, step 6: the studio shows a plus. `--plus N` puts every item on the viewer's
  subject at +N (`ModelBench::setPlus`, by mesh name against the world's item rows; the bare
  body's parts are not items and stay as they are) and hands the renderer the two sheets
  (`game::lendShine`, shared with the play mode). `tools/studio.py` passes `--plus`, `--sheet`
  and `--dist` through and names its sheet `<name>_plus<N>`.
  The Plate set at 3.5 m, +0/+3/+5/+7/+9 against strength 1, 0.5, 0.25 and 0.12, at noon and
  at night (shots/studio/shine/plate_noon.png, plate_night.png):
  * +3 and +5 read as MU's at the literal tint: warm red, cold blue, the steel under both.
  * +7 and +9 at 1 and 0.5 are solid cyan: the plate is gone. At 0.25 it reads as blue
    armour with the plate in it; at 0.12 as steel with a blue sheen.
  * At night the chrome glows in the dark at every strength, because it is added unlit, as
    MuMain adds it.
  * +9 is hardly told from +7: the star is lost in the chrome at this size.
  * The band does not slide as the studio turns. MuMain's UVs are off the WORLD normal, so
    the bands are fixed to the world and move only with the wave; a turning camera sees the
    same band on the same plate. That is MU's, and it is why MU2 used the view normal.
  **Picked by the user: `refine_strength` 0.25** -- blue armour with the plate readable by
  day, a clear glow at night. `refine_tint` stays at MuMain's 1.
- 2026-09-24, the fire side (the user's report: "near fire blue shine disappears on
  shoulder"). The pauldron turned to the studio's bonfire measured 255 in every channel on the
  +0 suit already, so the chrome was being added to white and could not show. MuMain's fire
  never lit metal that hot. **Invention**, `shineLamps` in shine.sh: on +7 and up the lamps'
  share alone is compressed (Reinhard, under the clip after exposure) and coloured 85% of the
  way to the chrome's hue. The fire-side pauldron went from (253, 251, 247) to (120, 178, 188)
  at noon and (100, 171, 184) at night; the sun, the sky, every +0 item and the town by day are
  as they were. Three tries on the whole lit colour failed first and are named in the shader so
  they are not tried again: scaling it, pulling it to the hue under a band only (a flat face has
  one Chrome01 texel, and it was a dark one), and pulling it above a brightness threshold (blue
  flecks on pale steel -- what the user saw as "something weird with the shoulder").
- 2026-09-24, step 7: the windows. An item stage is taken on change only, so a +3 in the bag
  never pulsed and a +7's chrome never scrolled. `ItemStage::stand` now notes whether anything
  standing is drawn at +3 or more (`shineOf`, so a jewel's fixed +8 counts) and such a stage is
  taken every frame. Priced with the inventory open on a +9 suit, +7 axe and +5 shield against
  the same save at +0, 900 frames each at 1080p: the bag stage drew on 837 of 837 measured
  frames against none; the frame's own numbers moved by noise -- GPU median 6.40 ms against
  6.54, CPU 4.99 against 4.86, 39 more draws. (The stage's view timer read 4.5 ms, but the
  same run's view timers summed to 63 ms inside a 6.5 ms frame: waiting, not work.)
  The strength in the bag was 0.25 like the world's and the shine hardly showed. The stage's
  picture is clamped 8-bit with no exposure and no bloom, as MuMain's whole frame was, so it
  takes MuMain's own 1 through a knob of its own, `refine_stage_strength`. The +7 axe's head
  shines orange there and the +9 Plate pieces carry blue bands on their edges and ridges; a
  flat face seen face on takes one Chrome01 texel, as MuMain's does.
- 2026-09-24, step 8: the price. `docs/budget.md`'s method -- 2560x1273, timers off,
  `--repeat 3` of 600, mean of means -- on the hero at 148,142 in the +9 Plate suit, +7 axe and
  +5 shield against the same save at +0: **6.658 ms against 6.654**, spreads 0.010 and 0.026.
  Nothing measurable. (The test saves' durability was raised to the top first: made +9 by hand
  at their +0 durability they sat at half of the +9 maximum and lit the worn-gear column.)

## Closed

2026-09-24. Proved as the top of this file asks: in bench shots, in town and in the bag, and
inside budget. What is left, named and not done:
* ~~**+9 is hardly told from +7.**~~ Done the same day: the star has its own gain over the
  chrome's strength, `refine_star` (u_refineStar.x), invention. The Plate set at +7 and +9,
  noon and night, against gains 1, 2, 4 and 8 (shots/studio/shine/plate_star.png): at 1 a +9
  is a +7 with a brighter helm crown; at 4 it carries bright cyan highlights on the crown, the
  chest ridge and the boots with the plate still read; at 8 the helm is a neon blob at night.
  **The user picked 4.** In the bag, at the stages' own strength of 1, the +9 pieces take
  brighter cyan edges and nothing blows out.
* **The chrome is fixed to the world, not the view** (MuMain's world normal): a turning camera
  sees the same band on the same plate, and a flat face takes one texel, which may be a dark
  one for a few seconds of the wave.
* **Other players' levels** are MuMain's 3-bit `LevelConvert` (+8 drawn as +7); there are no
  other players here, so nothing to do until there are.
* The drop sparkle (`CreateShiny`, every drop), the ordinary swing's refined streak, +11 and
  over, excellent and ancient: out of scope, as written above.
