# The shadow probe: why the shadows shimmered on a walk

Written 2026-09-21. How to test a shadow that shimmers or pops while the character walks,
what the tests found, and what was fixed. The tests stay; run them after any change to the
sun's split, the shadow filter, the shade's uniforms or the camera follow.

The switches they share: `--shadow-view` draws the sun's visibility alone, as grey, so a
changed pixel is a changed shadow. `--fixed-dt MS` advances every frame by exactly MS, so two
runs draw the same frames (a repeat measured 0.000 against its twin). `--shadow-size N` sets
the map's side, and `--shadow-noise screen|world|none` sets where the filter's disc turn is
anchored.

## The first two tests

**The numbers: `--shadow-log PATH`.** One csv row a frame. The split's centre and eye are
read back out of the matrix that is actually drawn with, then put on the sun's own axes
through the world's origin, which is a grid nailed to the ground. A split snapped to its
texels keeps the same fractional texel phase every frame however the camera moves.

| column | what it is | healthy |
|---|---|---|
| `texel_x`, `texel_y` | the split's centre, in shadow-map texels on the world grid | whole steps only |
| `phase_x`, `phase_y` | their fractional part | constant (0 within ~0.001) |
| `depth_quanta`, `depth_phase` | the split's eye along the sun, in D16 steps | whole steps, constant phase |
| `camera_lag_mm` | how far the camera's target trails the drawn character | 0 |
| `dt_ms`, `focus_x/z`, `hero_x/z`, `texel_mm` | the context | |

    ./run.sh --world lorencia --play --click-every 120 --frames 1500 --shadow-log /abs/walk.csv

**The pixels: `--shadow-slide MM`.** Holds everything still and moves only the split, MM
millimetres a frame along both ground axes. With the camera held and no figures, the only
thing that can change a pixel between frames is the shadow. A correct split draws the same
picture every frame. `tools/shimmer.py` counts the changed pixels in each pair of frames and
writes a heat map of where they changed.

    ./run.sh --world lorencia --still --no-figures --frames 60 --shot 1 \
             --shot-path /abs/slide --shadow-slide 3
    tools/shimmer.py /abs/slide --heat /abs/heat.png --allow 200

Run it once with `--shadow-slide 0` as the control. That run must come back at zero, or
something other than the shadow is moving and the test measures nothing.

3 mm a frame is about a tenth of a 29 mm texel, so 60 frames cross several texel boundaries
in x and in y, and the frames between crossings test the sub-texel slide. The exit code is
1 when any pair changes more than `--allow` pixels.

## What it found: three causes, fixed

| | slide test, worst pair | changed at all | phase while walking | camera lag |
|---|---|---|---|---|
| before | 331,535 px (5.1%) | 663,827 px | drifted smoothly, popped a texel on float noise | 16 mm median, 79 max |
| after | 141 px (0.002%) | 157 px | fixed within 0.0007 texel over 21 m | 0 |

1. **The texel snap removed nothing.** It snapped the focus in the light view's own space.
   That view is built around the focus, so the focus always sat at (0,0) and `floor` had
   nothing to take off. The split crawled with the camera, and every shadow edge crawled
   with it. Float noise either side of zero also flipped the floor between 0 and −1, which
   popped the split a whole texel now and then. Depth was never snapped either, so every
   stored D16 depth re-rounded each frame: flickering acne on every wall facing the sun.
   *Fix:* snap the eye in the world-fixed frame, x and y to the texel and z to the D16
   quantum (`renderer.cpp`, the split).

2. **The shade read last frame's shadow matrix on some draws.** A bgfx uniform set once rides
   on the next submit only, and the shade view sorts its draws by program. Any draw sorted
   ahead of the carrier ran on last frame's `u_shadowMtx`, `u_camPos` and sun. Once cause 1
   was fixed this was the whole remaining fault: the frame right after each texel step read
   the new map through the old matrix, and every shadow in the town jumped for one frame. A
   step-and-hold slide showed it exactly. Holds were pixel-identical, and only the first
   frame after each step differed. A readback of the D16 map cleared the map itself: it was
   already correct on that frame. *Fix:* the eight frame uniforms are set on every shade draw
   in `bindShadeInputs`, beside the textures that had the same trap. Measured A/B under the
   same load: no cost (2.48 / 2.46 / 2.49 ms median against 2.61 / 2.47 / 2.64 set once).

3. **The camera trailed the character by a frame.** The frame loop — now
   `app/modes/play_mode.cpp` — placed the camera from the character's position
   before the play advanced. The ground and every static shadow then
   slid under him by his step times the frame time, which changes every frame: up to 2.7
   texels. *Fix:* the pointer is answered and the play stepped first, against the camera
   the player saw, and only then is the camera placed.

## Three more tests, and what they changed

These were written for the three things the first round left open. Its numbers above were
taken on the old settings: a 60 m split, a 2048 map, and the disc turned per screen pixel.

### The camera moving: `--shadow-points` and `tools/pan.py`

tools/shimmer.py needs the camera held. This one compares ground with ground instead: the run
writes where a fixed 60 × 60 grid of ground points lands on screen each frame, and the tool
samples each shot there. Without `--still`, the world camera glides across the town.

    ./run.sh --world lorencia --no-figures --shadow-view --fixed-dt 16.6667 --frames 120 \
             --shot 1 --shot-path /abs/pan --shadow-points /abs/pan.csv [--shadow-noise ...]
    tools/pan.py /abs/pan /abs/pan.csv

It reports the mean change at a spot between frames, over spots in the penumbra.
`--shadow-noise none` is the floor: with no turn, every input is fixed to the snapped grid,
so what remains is resampling the picture as the pixel grid moves.

| split | screen turn | world turn | no turn (floor) |
|---|---|---|---|
| 60 m, 2048, 8 taps | 16.8 | 16.6 | 9.5 |
| same, 16 fixed taps | | | 8.8 |
| fitted, 4096, 16 fixed taps | 31.5 | 30.3 | 13.5 |

The screen turn fizzed at about twice the floor. Anchoring it to the world did not help,
because the turn runs through three cycles a texel, finer than a pixel, so the pixel grid
aliases it anyway. On the finer map it is worse: the widest penumbra spans 78 texels, and
eight turned taps disagree wildly. **Fix:** no turn, 9 search taps and 16 filter taps, all
fixed. The floor rises on the finer map because sharper edges resample worse, which is
expected. The price is faint copies of fine casters in the widest penumbrae, such as a tree
canopy ten metres up. 24 taps softened them without removing them and cost 0.27 ms more.

### The character's own shadow: `tools/shadowref.py`

The character moves, so a changed pixel says nothing. The tool compares a setting against a
far finer one on identical frames (`--fixed-dt`, same seed, `--play --click-every 30`). The
difference is the aliasing alone, and how much it changes from frame to frame is the flicker.
The reference is 8192 fitted, 4.5 mm a texel. Box is the middle of the frame, where the
character stands.

| setting | texel | error (box) | flicker (box) | pixels flickering |
|---|---|---|---|---|
| 60 m, 2048 (old) | 29 mm | 2.04 | 2.47 | 2.84% |
| fitted, 2048 | 17.9 mm | 1.22 | 1.68 | 2.21% |
| 60 m, 4096 | 14.6 mm | 1.07 | 1.41 | 1.90% |
| **fitted, 4096** | **9.0 mm** | **0.59** | **0.90** | **1.41%** |

**Fix, part one: fit the split to the view** (`shadow_fit_below` in the sheet, 3 m). Everything
the camera sees lies between the eye and where its frustum meets a plane 3 m below the
target, so that hull, seen from the sun, is all the split has to cover: 36.7 m, off-centre
from the target. The old 60 m square was centred on the target and spent half its texels
behind the camera. On the same frames the fit dropped about 65 scattered edge pixels and no
region, so no shadow is cut off. The width is kept between frames and moves only on a 2%
change, because a width that wobbles by a float is a texel that changes size.

**Fix, part two: 4096.** 9 mm a texel, about the 7.7 mm a screen pixel covers at the character.

**A bug the comparison turned up.** The blocker search and the widest penumbra were counted
in texels (6 and 24), so a finer map drew a smaller shadow. The search shrank to 5 cm at 4096
and the outer penumbra was cut off, and the finer map measured *cheaper* because fewer pixels
found a blocker at all. Both are now metres, 17.6 cm and 70 cm, which is what the old setting
drew, so resolution changes sharpness and nothing else.

### The residue: `--shadow-view` on the slide test

The visibility view showed the old "70 faint pixels" were not faint. A few pixels jumped by
up to 172/255, and the steps were continuous rather than whole taps, so they were not depth
ties. They were pixels switching between the blocker search's two paths. The cause: the
snapped view was built as the eye plus its remainder, and that sum rounds differently every
frame. **Fix:** build it from whole numbers of texels and quanta. A split that has not
stepped is now the same bits: 22 of 37 slide pairs are identical, where before every pair
differed. What is left, 150–360 pixels and the same ones each time, comes only on frames
where the split steps a texel. That is fp32 rounding of absolute world positions against a
grid that just moved. Removing it needs camera-relative coordinates in the shade, for 0.007%
of one frame.

### The cost

Measured A/B, alternating runs under the same load, Lorencia, 600 frames: **2.74 ms → 3.31 ms**
median frame (+0.56). The fit is free. 4096 with the old screen turn cost +0.15, and the
fixed taps are the rest. The frame stays inside 5.5 ms. The per-view accounts in
docs/budget.md were not re-priced for this.

## What is left

- The copies of fine casters in the widest penumbrae, described above.
- The residue on texel-step frames, described above.
- Nothing here filters over time. With no TAA, fixed taps are the only way to keep a
  penumbra still, and they cost taps. A temporal filter would allow a turned disc and fewer
  taps.
