# Sprint 8a: the lamps

**Proved by:** Lorencia's lamps, torches, bonfires and candles light the ground and whoever
stands near them, their painted glows are drawn as glows and not as cut-out cards, and the fires
burn with MU's own flame. Judged by the user from shots; the frame stays inside 5.5 ms.

The first line of sprint 8 ("lamps and fires as point lights"), taken ahead of the rest of it.
Opened 2026-09-21.

## What MU does, from MuMain

A lamp in MU is **three separate things**, and MU2's `Lamps.cs` already said so:

1. **The light.** `AddTerrainLight(x, y, Light, Range, PrimaryTerrainLight)`
   (`ZzzLodTerrain.cpp:950`) adds `Light × (Range − d) / Range` to every terrain vertex within
   `Range` tiles, where `d` is in tiles on the ground plane. Height does not enter it. That buffer
   is the same one `light.png` fills, and it **multiplies the albedo**. An object is tinted by
   the buffer at its own feet. Re-rolled every frame.
   - `CreateFire(0, …)` (`ZzzEffectFireLeave.cpp:61`): `L = rand[0.6, 1.1)`, colour
     `(L, 0.6L, 0.4L)`, range 4, at the offset turned by the object's angle and **not scaled**,
     jittered ±8 units. Bridge01, DoungeonGate01, FireLight01/02, Bonfire01 and the hidden Light01.
   - StreetLight01: `L = 0.6 or 0.7`, `(L, 0.8L, 0.6L)`, range 3. Candle01: `L = 0.3–0.6`,
     `(L, 0.6L, 0.2L)`, range 3 (the index says 1.5).
2. **The glow.** `o->BlendMesh`: one submesh drawn additively at `BlendMeshLight`, which flickers
   on the bonfire (0.4–0.9), House03 and HouseWall02 (0.4–0.7). These are the eleven `BLEND`
   materials in MU2's glb: the street light's `streetlight_brightness2`, the torch's `light3`,
   the bonfire's `fire_02`, the candle's `candle2`, the lit windows' `light_02`, the waterspout.
   **Today the cook turns them into 0.5 cutouts drawn opaque and cast into the shadow map**,
   marked "until sprint 8".
3. **The flame.** `CreateFire(0)` also spawns `BITMAP_FIRE` subtype 0 on half the frames:
   `Effect/Fire01` (four 64-pixel cells), 24 frames of life, born at scale 1.28–1.92 and shrinking
   0.04 a frame, a cell every six frames, drawn a quarter of the strip wide, drifting
   3.2–4.7 units a frame along the object's −y turned by its yaw, rising on a lift that builds
   0.004 a frame. Its colour is held; the size is what fades. MU2's `Kindling.cs` reads the same
   lines the same way.

## The counts, from `lorencia.json` and `index.json`

121 lights: FireLight01 26, FireLight02 18, Bridge01 16 × 2, StreetLight01 10, Bonfire01 9,
Candle01 6, HouseWall02 15 windows, DoungeonGate01 2, House03/04/05 one each. Plus the hidden
Light01 × 2 (fire), Light02 × 18 and Light03 × 5 (chimney smoke, not built here). 88 of the 121
are fires.

## The design

**One system, four layers, each in the layer the plan puts it in.**

- **The cook** (`tools/cook.py`). The `.mut` goes to version 2: after the models, every model's
  emitters in its own frame (metres, our axes, unscaled, since MU does not scale them) and its
  glow flicker per material. Then the world's own anchors: the hidden Light01/02/03, written as
  fires and smokes in world metres. The `.mum`'s `twoSided` byte becomes a flags byte, and bit 1
  says **glow**: the `BLEND` materials stop being cutouts. The version is unchanged, since
  every cooked file already holds 0 or 1 there.
- **gfx** knows point lights and glow parts, never lamps.
  - `Renderer::setPointLights` takes the static set once. It builds a **light grid on the
    ground**: 2 m cells over the map, each listing up to eight lights whose sphere reaches its
    column, in an RGBA8 texture. A pixel reads its own cell and loops only over those lights.
    The lights never move, so this is settled once at load, as foundation 7 asks of culling.
    No per-frame sort, no light popping as the camera moves, and any count of lights.
  - `setPointLightLevels` takes the flicker each frame: 121 multipliers into one 256×2 RGBA32F
    texture (8 kB).
  - The shade and the ground call one `lampLight()` in `lights.sh`: the same GGX as the sun,
    no shadow, and a falloff of `(1 − d²/R²)²`, which reaches zero at the reach with no ring.
    **Invention**, against MU's linear cone. The reach is `√(R² + h²)`, with R MU's range in
    tiles and h the light's height, so the pool on the ground under it is MU's R across.
    A lamp lights the **texture's own albedo, not the albedo times `light.png`**: MU adds the
    lamp into the light buffer rather than multiplying it by the buffer, so a lamp in a dark
    corner lights the corner.
  - Glow parts leave the shadow, prepass and shade lists and draw in the transparent view with
    `fs_glow`: additive, depth-tested, no depth write, two-sided, `albedo × level × glow_strength`.
    The level rides in the instance's fifth vec4 `.w`, which was always 1 and never read.
- **game** (`game/lamps`). Resolves every emitter to the world through
  `content::placementTransform` (scale 1), flickers each by MU2's rate and smoothing (MU re-rolls
  every frame, which on a light is a strobe), flickers each placement's glow, and keeps
  MU's flame particles for the fires, spawned and aged on MU's 25 Hz reference clock. Flames are
  simulated everywhere and drawn within 45 m of the camera's target, so none pop in.
- **The sheet.** `lamp_strength` and `glow_strength` in `lighting.json`, live-reloaded.
  `lamp_strength` starts at π: at the centre of a pool a lamp of L=1 then adds what MU's own
  arithmetic adds, the same light a flat surface takes from the sun at `sun_strength` π.

## Not in this sprint

Chimney smoke (Light02/03); bloom; the waterspout's scroll; the night. The light model is built
so the night is only a sheet: a dusk `lighting.json` is how the lamps are judged alone.

## The gate

- Shots over the square, the bridge and a bonfire, before and after.
- The frame's mean wall time over the town with and without lamps, `--repeat 3`.
- The grid's worst cell reported at load. Eight is the cap, and a cell that wanted more is
  logged with its count.

## Results, 2026-09-21

**Built:** everything under "The design". 123 lights (121 carried, 2 hidden Light01), 89 fires,
31 flickering glows. The grid is 128×128 cells of 2 m, 1470 of them lit, and the worst wants
6 of its 8 slots, so nothing is dropped.

**One design change after the first shot.** The first falloff used 3D distance with a reach of
`√(R² + h²)`. At the foot of a street lamp, 2.26 m under its head, that gave 41% of what MU gives,
and the pools were invisible at MU's own strength. Now the distance is flat on the ground, and
height counts only above the light or below the ground (`lights.sh`). With that, `lamp_strength`
π, MU's own weight, reads: warm pools under the street lamps and bonfires at dusk, and a subtle
warm floor under them by day, which is what MU's daytime does.

**A build trap, fixed.** The shader rule depended on the `.sc` and `varying.def.sc` only, so an
edit to `common.sh`, `shadow.sh` or `lights.sh` rebuilt nothing, and the run drew with the old
shader while looking like a finished change. CMakeLists now appends every `shaders/*.sh` to
every shader's dependencies.

**The frame.** Lorencia with the moving camera, 600 frames × 3, vsync off, Release:

| | mean of means | spread |
|---|---|---|
| lamps on | 10.427 ms | 0.226 |
| `--no-lamps` | 10.094 ms | 0.087 |

**+0.33 ms.** These numbers are NOT at 1080p. The window came up at the Retina backbuffer,
3456×1894 (3.2× the pixels), even with the default `--width 1920`. That is why the whole frame
reads 10 ms here against the ~2.5 ms the town measured before. The lamps' cost is per pixel, so
at 1080p it should be about a third of this, but that is an estimate, not a measurement. Measure
again at 1080p once the backbuffer size is fixed.

**Left for judgement and for next:**

- **The flames are weak.** MU's `Fire01` is a dim red strip, and times `(L, 0.6L, 0.4L)` it adds
  a red-orange wisp. It is faithful, and it is not what "looks really good" means. The bonfire's
  glow card (`fire_02`) reads as a small orange shape. Candidates, in order: bloom on the HDR
  target (the glows and flames are the only things over 1), then a `flame_strength` sheet knob,
  marked invention.
- Chimney smoke from the 23 Light02/03 anchors is cooked and not drawn.
- The two Light01 anchors have no angle in the cook, so their flames stand still.
- Figures cooked before this sprint still carry `BLEND` as a 0.5 cutout. A re-cook makes them
  glows, which is also MU's BlendMesh.
- The night is only a sheet away: a dusk `lighting.json` (`sun_strength` 0.25, `ambient` 0.12,
  `elevation` 20) is how these shots were judged.

## 8b: the fire made good, 2026-09-21

The user's verdict on 8a was that the fire "has to be very nice". Up close it was three faults:

1. **The bonfire drew orange squares.** `fire_02` is an orange band running hard up to the
   texture's top edge; MU's JPEG had black there, MU2's pipeline kept a soft alpha instead, and
   the glow ignored it. `fs_glow` now multiplies by the sheet's alpha.
2. **MU's flame is dim red paint.** Our `fire01.png` is byte-identical to MU's `Fire01.OZJ`
   (checked). Its linear peak is 0.82, and its last three cells are 9-19x darker than the first
   (p99 0.631, 0.072, 0.033, 0.033), so every flame faded to nothing in a quarter of its life
   and nothing ever passed 1.0. MU's motion is also odd: it drifts a metre sideways along the
   holder's -y and rises 12 cm. No Lorencia fire has a pitch that would turn that upward.
3. **Nothing glowed**, because the frame had no bloom.

What was built, all invention except where it says MU:

- **Bloom** (`fs_bloom_down`, `fs_bloom_up`, views 6-14). Five levels from half to 1/32, with
  Jimenez' 13-tap downsample, Karis' weighted average and a soft threshold on the first level,
  then a tent upsample added back up the chain. The present adds level 0 before ACES. Sheet:
  `bloom_threshold` 1.2 (over what the sun makes of a white wall, so by day only the fires and
  glows bloom), `bloom_knee` 0.6, `bloom_strength` 0.25.
- **`fs_flame`**: MU's flame sheet read as a shape, its red channel times a per-cell gain, and
  coloured by the particle's heat up a ramp from ember red through orange to a yellow-white far
  over 1.0. `flame_strength` 6 is full heat.
- **The flame's motion**: born at the fuel, rising on buoyancy (0.45-0.9 m/s, +1.8 m/s²),
  swaying on two sines of its own, swelling then tapering, taller than wide, cooling as it goes.
  Kept from MU: the four painted cells, the lean along the holder's -y (0.12 m/s), the size and
  every place a fire burns. Two profiles: torch (38 flames/s) and bonfire (60/s, wider, taller,
  its flames starting at the logs 35 cm under MU's emitter point).
- **Embers**: `flare01` through the same ramp, twinkling, kicked about by the air. 12/s from a
  bonfire, 0.7/s from a torch.
- **Smoke** from bonfires: `fs_smoke` rounds smoke02's square edge into a disc and takes the
  sheet's brightness rather than its brown. The game lights it by the day (`daylight` from the
  sheet's ambient and sun) plus a warm underside from the fire while it is young.
- Fires burn only within 50 m of the camera and are drawn within 45 m, so the whole map never
  simulates. About 880 sprites at peak over the town, of 2048, none refused.

**The frame**, at the same 3456×1894 Retina backbuffer as 8a (not 1080p), camera walking the
town, 600 × 3: lamps and fire 10.877 ms against `--no-lamps` 10.523, so +0.35 ms. Bloom runs in
both. Its own cost reads as about +0.43 ms at this size (`--no-lamps` was 10.094 before bloom),
but that compares two runs rather than an on/off. At 1080p both should be about a third.

Shots: the bonfire (184,135) and the torch field (110,70) at 4 s, by day and at dusk.
