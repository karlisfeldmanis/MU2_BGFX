# Devias's effects

Everything MuMain does in Devias (WD_2DEVIAS: server map 2, `Data/World3` and `Data/Object3`) beyond drawing its objects, and how much of it MU2_BGFX has.
Researched 2026-09-29 by reading every WD_2DEVIAS path in MuMain (37 references in 13 files, plus the three `WorldActive == 2` checks), the placements in `World3/EncTerrain3.obj`, the textures each Object3 model wears, and OpenMU Version075's Devias.
Line numbers are in `LEGACY/reference/MuMain/src/source`.
MU runs its logic at 25 frames a second, so every speed below is per frame; MU2_BGFX's metres are MU's units / 100.

**Types.** A Devias placement's type *n* is `Object3/Object{n+1}.bmd` (`pipeline/terrain.py` `model_for`). Counts and places come from decoding `EncTerrain3.obj` (2,237 placements) with `pipeline/terrain.py`'s `objects()`.

**Seasons.** MuMain's Devias data is a Season-6-era file. Types 105–111 and the Object106–120 models (wedding hall `cho_wedding_*`, Christmas `giftbox_*`, `han_xtree`, mistletoe, `devgateice`) are later events, as are the Serbis donkey and flag and the Santa-Village warp. Each is marked **(later)** where it comes up. There is no 0.75 Devias data in the tree to diff against.

**No owner yet.** Nothing here is taken.

## 1. Snow (`CreateDeviasSnow`)

Devias's weather is MU's leaf pool with a snowflake in it.
The creator is `CreateDeviasSnow` (Render/Effects/ZzzEffectFireLeave.cpp:274), the mover is the shared `MoveEtcLeaf` (:401), and the pool is `MoveLeaves` (:422).

| What | MU's number | Source |
|---|---|---|
| Pool | 80 live flakes. `iMaxLeaves = 80` outside the Devil Square; the array holds `MAX_LEAVES` 200 | ZzzEffectFireLeave.cpp:429, Core/Globals/_define.h:461 |
| Refill | A dead slot respawns in the same frame `MoveLeaves` finds it dead, so the pool is always full | ZzzEffectFireLeave.cpp:474-487 |
| Kind | `BITMAP_LEAF1` (World3/leaf01) at `Scale 5`. On `rand_fps_check(10)` it is `BITMAP_LEAF2` (World3/leaf02) at `Scale 10` instead | :278-284 |
| Spawn box | hero + (rand −800…799, rand −500…899, rand 200…399) | :285-288 |
| Launch | `Angle (−30, 0, 0)`, velocity (0, 0, −rand 8…23) rotated by that angle. Through AngleMatrix (Core/Math/ZzzMathLib.cpp:194) this gives (0, −0.5 v, −0.866 v): a fall of 6.9–19.9 units a frame, drifting along −Y at half that speed | :289-293 |
| Frame factor | The launch velocity is **not** scaled by `FPS_ANIMATION_FACTOR`. Lorencia's leaf is (:236). Only the integration is, so snow speed is correct at any frame rate | :291 vs :236 |
| Flight | `MoveEtcLeaf`: every frame each velocity axis gains rand(−8…7) × 0.1, then the position steps by the velocity. This is an unbounded random walk, with no damping and no gravity | :413-419 |
| Landing | At or below `RequestTerrainHeight` the flake is pinned to the ground and its `Light` drops 0.05 a frame on all three channels. It dies at 0, after 20 frames (0.8 s) | :403-412 |
| Roofs | Only the terrain height is tested, so a flake falls through a roof and lands on the floor under it | :403 |
| Draw | `EnableAlphaBlend`, which is `glBlendFunc(GL_ONE, GL_ONE)` with depth write and culling off: additive | :533-535, Render/Textures/ZzzOpenglUtil.cpp:395-405 |
| Shape | Devias (with Ice City and Santa Town) draws a camera-facing `RenderSprite(Type, Position, Scale, Scale, Light)`: a square billboard 5 units (flake) or 10 units (star) across. Every other world's leaf is a `RenderPlane3D` turned by its own angle | :568-570, ZzzOpenglUtil.cpp:1100-1112 |
| Colour | `Light (1, 1, 1)` at birth, drawn with `glColor3f(1,1,1)` | :480, :549 |
| Indoors | `RequireLeavesEffect` and `ShouldRenderLeaves` are true in Devias only when `HeroTile != 3 && HeroTile < 10` (tile indices in §7). Indoors the pool is not moved, so it freezes in place, and the sprite-batch draw is skipped | Scenes/MainScene.cpp:78-117, :275-278, :520-523 |

At 25 fps the fall is 1.7–5.0 m/s with a 1.0–2.9 m/s drift along −Y.
That is fast for snow: a flake spawned 2–4 m above the hero lives about a second in the air, then fades on the ground for 0.8 s.
The random walk spreads the velocity by about 0.46 units a frame per √frame, so older flakes wander.

**A doubled draw, probably from the fork.** `RenderMainScene` calls `RenderLeaves()` unconditionally at MainScene.cpp:497-498, in the object pass.
It calls it again inside the sprite batch at :520-523 for Devias outdoors.
`RenderLeaves` sets its own blend each time, so outdoors each additive flake is drawn twice, at twice the brightness.
Indoors the first draw still runs, so the frozen flakes stay visible.
The 0.97 `ZzzScene.cpp` is not in the tree to check against. Treat the brightness as one draw and judge it by eye.
(`MainScene.cpp:555` is a third call inside the water-terrain pass, which Devias never takes.)

**The sheets.** Decoded with `pipeline/decode_texture.py` into the scratchpad. Both are 16×16 with no alpha, so the black is what makes the additive blend work.

- `World3/leaf01.OZJ` is the flake: a soft round white disc, full white in the middle, fading to black over about 5 px, with a faint 14/255 grey JPEG border.
- `World3/leaf02.OZJ` is the glint: a four-to-eight-pointed star or sparkle, peaking at 252, with noise elsewhere. One flake in ten, at double size.
- `World3/leaf01.OZT` (32×32, a soft disc with alpha) is loaded into `BITMAP_LEAF1` first and then overwritten. For every map except 0, 3 and 63, the second load takes `leaf01.jpg` (World/MapInfra/MapManager.cpp:1475-1480). The TGA is unused in Devias.

`DEVIAS_XMAS_EVENT` would double the draw loop to `MAX_LEAVES_DOUBLE` 400 (ZzzEffectFireLeave.cpp:550-558). It is commented out (App/Platform/Windows/Winmain.h:46, "more snow in devias"), so it is not 0.75.

**MU2_BGFX:**

- *Already there:* `src/game/world/leaves.{h,cpp}` has the pool, the `move` random walk, the landing fade, the indoor fade and the `Effects` gather. `weather.{h,cpp}` has the per-world switch, and "no wind indoors" is already the right answer to MU's freeze-in-place.
- *Missing:*
  - a snow kind: a camera-facing square sprite, additive, with two sheets and the 1-in-10 star at double size;
  - the −30° slant and the no-double-factor speed;
  - the two decoded sheets in `source/`;
  - Devias in `weather.h` (no rain, no forest).

## 2. Sky, fog, light and the ice

| What | MU | Source |
|---|---|---|
| Clear colour | `SetClearAndFogColor(0.75, 0.85, 1.0)`, "light snowy blue". The clear colour and the fog colour are set to the same value. Lorencia is (10, 20, 14)/256, and most maps are black | Scenes/SceneManager.cpp:383-384, :356-364 |
| Fog | Linear, from `ViewFar × 1.0` to `× 1.25`, in `FogColor`. **This is the fork's:** the Default camera turns fog off (`FogEnable = false`) and only its Orbital camera uses it. So in MU's own view the pale blue shows only as the backdrop past the terrain's edge | Render/Textures/ZzzOpenglUtil.cpp:752-788, Scenes/MainScene.cpp:636-642 |
| Sky dome | None. Devias has no sky object and no per-world sky code. The `sky` texture on Object72/73 is window glass | — |
| Luminosity | Nothing Devias-specific. `g_Luminosity = sin(WorldTime × 0.004) × 0.15 + 0.6` is global. `RenderObjectVisual`'s local `Luminosity` is rand 0.70–1.00, re-rolled every frame (see noria-effects §1) | SceneManager.cpp:1283, Engine/Object/ZzzObject.cpp:2781 |
| Terrain light | The cold look is painted into `World3/TerrainLight.OZJ`. Its mean is (115, 149, 178), blue-shifted, against Lorencia's neutral (141, 144, 145) and Noria's (167, 167, 168). There is no code tint | decoded in the scratchpad |
| Water / ice tile | Layer-1 tile 5, `TileWater01` (2,553 tiles), is dark teal ice-water. It takes the default `WaterMove` UV scroll, `(WorldTime % 20000) × 0.00005`, i.e. one texture width per 20 s, plus the grass-wind wobble, like every map's tile 5 | Render/Terrain/ZzzLodTerrain.cpp:2041-2043, :3593-3596, :1751-1759 |
| Terrain "grass" | The grass pass draws `BITMAP_MAPGRASS + layer1` cards, 128 units tall (the texture height × 2), on tiles with no alpha blend. `World3` has no `TileGrass01.tga`, so tile 0 gets nothing. Tile 1 (24,668 tiles) stands `TileGrass02.OZT`, a 256×64 sheet of white wisps: snow tufts | ZzzLodTerrain.cpp:2076-2110, World/MapInfra/MapManager.cpp:1465-1470 |
| Terrain check | The loader refuses a Devias `.att` unless tile (208, 55) is 5 (TW flags). This is a sanity check, not an effect | ZzzLodTerrain.cpp:210-211 |

**MU2_BGFX:**

- *Already there:* the ground shader's water layer, grass and the per-world lighting sheets (`sheets/worlds/*.json`).
- *Missing:* a Devias sheet: the pale-blue clear colour, cold light, and snow-tuft grass on tile 1. `docs/devias-ground.md` is the ground's own doc and still empty.

## 3. The objects: what MU does per type

From `RenderObjectVisual` (ZzzObject.cpp:2819-2855), `MoveObject` (:3711-3718, :3900-3958), `CreateObject` (:4668-4720) and `MOVEMENT_OPERATE` (Engine/Object/ZzzInterface.cpp:1715-1727).
The textures come from `tools/MuExtract export-obj` group names.

### 3a. Glows and fires

| Type (model, textures) | Placed | What MU does | Source |
|---|---|---|---|
| 19 (Object20, `aurora`) | **136**, spread over the whole map (x 8–249, y 9–243), floating 2.2–5.8 m above the ground (median 5.1 m), scale 0.9–1.6 | `BlendMesh = 0`: the whole mesh is drawn additively. Each is a flat 2.4 × 2.6 m upright panel painted with a violet-to-magenta curtain. **This is Devias's aurora**: 136 glowing curtains hanging over the snow | ZzzObject.cpp:4697-4701 |
| 92, 93 (Object93, Object94, `ice`) | 4 + 22, in the north-west (x 7–76, y 184–243), sunk 0.2–0.4 m | `BlendMesh = 0`: the whole mesh is additive. These are ice formations up to 3.6 m, with a blue-white crystal sheet, so they glow rather than shade | :4697-4701 |
| 78 (Object79, `wall_door01/03`, `house_stone_out`, `light_02`) | 23 wall pieces in town (x 186–236, y 12–62) | `BlendMesh = 3`, the `light_02` window, an amber gradient, is additive. `BlendMeshLight = rand(4…7) × 0.1`, re-rolled every frame: a 0.4–0.7 flicker. `Alpha()` also caps `BlendMeshLight` at the object's alpha | :4706-4708, :3903-3905, Engine/AI/ZzzAI.cpp:188 |
| 54, 56 (Object55, Object57, `candle`/`candle2`) | 4 + 16, 0.9–1.6 m up, in the church and houses | `BlendMesh = 1`: the flame submesh is additive, with no flicker code | :4702-4705 |
| 66 (Object67, `fire_01`/`fire_02`) | 9, one per camp or house: (11.5, 77.5), (108.6, 242.4), (174, 192), (162.6, 233.6), (189.5, 153.4), (202.5, 62), (232, 27.5), (225, 44.5), (238.5, 194.5) | `CreateFire(0, o, 0, 0, 50)`: each frame, at 50 units up, jittered ±8, on `rand_fps_check(2)` a `BITMAP_FIRE` subtype rand 0–3. `AddTerrainLight` radius 4 of (L, 0.6 L, 0.4 L), with L = rand 0.6–1.1 | :3918-3920, Render/Effects/ZzzEffectFireLeave.cpp:61-91 |
| 30 (Object31, `light`/`light2`/`light3`) | 2, at (30.8, 23.5) and (30.8, 28.4), either side of the south-west hall's doors | Each frame (`CreateParticleFpsChecked`): a `BITMAP_TRUE_FIRE` at z + 160, scale `o->Scale`, life 24, velocity (rand −5…4 × 0.4, 0, rand 5…14 × 0.2), shrinking 0.02 a frame, rising 1 a frame, light = life/25. Plus a `BITMAP_SMOKE` subtype 21 at scale 0.5–1.3: subtractive (`EnableAlphaBlendMinus`) dark smoke, life 100, spread ±100 units, rising 2 a frame while life > 50. **Probably (later):** `BITMAP_TRUE_FIRE` is `Effect/fantaF.jpg`, whose loader comment names Aida and Battle Castle, and the hall it lights holds the wedding objects | ZzzObject.cpp:3906-3917, Render/Effects/ZzzEffectParticle.cpp:3335-3375, :8271-8335, :3043-3052, :7847-7856, :9013, Engine/Object/ZzzOpenData.cpp:5185 |
| 100 (Object101, `aurora`) | 1, at (3.3, 247.6), on the Lost Tower gate | `HiddenMesh = -2`, so the box is not drawn. `RenderObjectVisual` draws two `BITMAP_LIGHTNING+1` sprites at scale 2.5, at bone 0 + (0, 0, 150), in white × Luminosity (0.7–1.0 re-rolled). One turns at `+Rotation` and one at `−Rotation`, with `Rotation = (int)(WorldTime × 0.1) % 360` (about 100°/s). **Level-gated:** drawn only if the hero is level ≥ 50 (≥ 33 for the MG, DL and RF, which are later classes) in the main pass, and ≥ 80 / 53 in the after-character pass. It marks the Lost Tower entrance: OpenMU V075's enter gate 28 is (2–3, 248–249), level 40 (Persistence/Initialization/Version075/Gates.cs:197). MuMain's 50 is a later number | ZzzObject.cpp:4717-4719, :2822-2829, :3446-3463, :3637-3654 |

### 3b. Moving and animated objects

| Type | Placed | What MU does | Source |
|---|---|---|---|
| 20 (Object21), 65 (Object66), 88 (Object89): **swinging doors** | 6 + 1 + 3. Two of the 88s sit at (0.3, 0.0), junk placements at the origin | On creation the yaw is wrapped to `% 360`, the rest yaw is kept in `HeadAngle` and the spawn point in `HeadTargetAngle`. When the hero is within 200 units (2 m) of that point, the door's yaw is set from its rest: 90 → `30 − (200 − d) × 0.5`, 270 → `330 + …`, 0 → `300 − …`, 180 → `240 + …`, and `SOUND_DOOR01` (aDoor.wav) plays every frame. So the door jumps 60° open at 2 m and swings to 160° at the threshold. Out of range it turns back at 10° a frame (`TurnAngle2`). Not in the editor | ZzzObject.cpp:4709-4716, :3921-3955 |
| 86 (Object87, `tile_02`): **sliding doors** | 6: pairs at (44.5, 25–29) and (223.6/227.7, 207.4). Two more have a NaN y in the file | Within 2 m the door slides `(200 − d) × 2` units along its axis (X for yaw 0/180, Y for 90/270), up to 4 m, set directly each frame, and `SOUND_DOOR02` (aCastleDoor.wav) plays. Out of range it eases back 0.2 of the gap a frame | :3930-3942, :3956-3958 |
| 103 (Object104, `dog_g99`): **sled dogs** | 2, at (7.6, 76.5) and (8.4, 76.0), by the camp fire at (11.5, 77.5) | At the last key of each clip: on `rand_fps_check(32)` play action 1, otherwise action 0. An idle with a rare fidget | :2830-2837 |
| MODEL_WARP subtype 1, at (53 × 100 + 50, 92 × 100 + 20) | 1 **(later)** | Three discs 360 units up: WARP at +0, WARP2 at y + 4, WARP3 at y + 20. Unlike Noria's five subtype-0 discs, subtype 1 takes a fixed green-teal `(0, 0.2, 0.1) + sin(WorldTime × 0.0011) × 0.05` and turns `2 + Gravity`° a frame. It stands inside type 110, Object111 `devgateice`, the ice gate at (53.4, 93.1), which is the Christmas Santa-Village portal (`SendMoveToDeviasBySnowmanRequest`). Not 0.75 | World/MapInfra/MapManager.cpp:75-85, ZzzObject.cpp:4671-4682, Render/Effects/Behaviors/MoveHandlers.cpp:454-474 |
| Serbis donkey and flag (`obj_donkey`, `obj_flag`) at (191, 16) and (191, 17) | **(later)** | Plain objects for Lahap/Serbis (NPC 256, Season 4). No code beyond creation | MapManager.cpp:61-73 |

### 3c. Sitting and posing (`CreateOperate`)

| Type | Placed | Pose |
|---|---|---|
| 91 (Object92, hidden `HiddenMesh -2`, bounding box raised to (40, 40, 160)) | 44 | `PLAYER_POSE1` / `PLAYER_POSE_FEMALE1`, facing the object's angle. The cursor is `BITMAP_CURSOR + 6`, the pose hand |
| 22 (Object23, `ti03`) and 25 (Object26, `ti03`): the church pews | 22 + 15, most at (204–213, 14–24) | `PLAYER_SIT1`, facing the object |
| 40 (Object41, chair) | 13 | sit, facing |
| 45 (Object46, chair) | 1 | sit, keeping his own facing |
| 55 (Object56, chair) | 2 | sit, facing |
| 73 (Object74, `snow_ston`): stones round the camp fires | 32 | sit, keeping his own facing |

Sources: ZzzObject.cpp:4684-4696 and ZzzInterface.cpp:1715-1727, :1812-1830. The pose cursor is at ZzzInterface.cpp:4052-4057.

### 3d. What is not there

- **RENDER_BRIGHT/CHROME submeshes:** types 105, 106 (wedding) and 110 (ice gate) have them, but only under `DEVIAS_XMAS_EVENT` / `DEVIAS_XMAS_EVENT2007` (ZzzObject.cpp:2838-2856). Both flags are off. **(later)**
- **Smoke from chimneys, snow off roofs, torches on walls:** MU has none in Devias. Nothing but the table above.

**MU2_BGFX:**

- *Reuse:*
  - `lamps.{h,cpp}`: the BlendMesh glow and its flicker level, the `CreateFire` flames and the terrain light. This covers types 66, 78, 54 and 56.
  - `ornaments.{h,cpp}`: bone-anchored `BITMAP_LIGHT` and `BITMAP_LIGHTNING+1` sprites, already with the Chaos Machine's ±Rotation star. This covers type 100, plus a level gate.
  - `portal.{h,cpp}`: the warp discs, if the later warp is ever wanted.
  - `sway`: rigged world objects, for the dogs' clip.
  - `tools/cook.py` `PERCHES`: sit and pose.
  - `pipeline/terrain.py`'s `BLEND_MESH_BY_MAP` and `HIDDEN_BY_MAP`: the tables to fill for map 3's number.
- *Missing:*
  - doors of either kind, with their sounds;
  - the dogs' action-picking idle;
  - the `BITMAP_TRUE_FIRE` / subtractive smoke pair;
  - Devias's rows in `BLEND_MESH_BY_MAP` (`3: {19: 0, 54: 1, 56: 1, 78: 3, 92: 0, 93: 0}`, keyed by map number 3 = World3) and `HIDDEN_BY_MAP` (`{91, 100}`);
  - its `PERCHES` row.

## 4. Boids, bats, fish

**None.** Devias's `MapManager::Load` arm loads no bird, bat or butterfly (MapManager.cpp:60-89).
`CreateBoid`'s world list is Lorencia, Dungeon, Noria, Lost Tower, Heaven, Atlans, Blood Castle, Hellas and Elbeland (Engine/AI/GOBoid.cpp:1304-1313).
`MoveFishs` swims only on Lorencia's tile 5, Dungeon, Stadium, Atlans, Hellas, Aida, Tarkan and Crywolf (GOBoid.cpp:1664-1697).

The only Devias rule in GOBoid is the **mounts' dust**: a ridden horse, Fenrir, Uniria or Dinorant kicks up `BITMAP_SMOKE`, white snow puffs, in Devias, where every other map gets `BITMAP_SMOKE + 1`, brown dust (GOBoid.cpp:314, :399, :552).
Fenrir and the Dark Horse are later; so is any mount at all in 0.75.

**MU2_BGFX:** `boids.h` `boidOf("devias")` should return nothing, which is already the default for an unnamed world. There is no mount system, so there is nothing to do.

## 5. Sound and music

| What | MU | File | Source |
|---|---|---|---|
| Wind | `SOUND_WIND01` looped, stopped while `HeroTile == 3 \|\| HeroTile >= 10` (indoors). `StopInactiveAmbientSounds` leaves it running only in Lorencia, Devias, Noria, Ice City boss and the Marketplace | `Data/Sound/aWind.wav` | Scenes/SceneManager.cpp:862-866, :932-935, Engine/Object/ZzzOpenData.cpp:4731 |
| Footsteps | `SOUND_HUMAN_WALK_SNOW` for the hero on every tile except 3 and ≥ 10, at `AnimationFrame` 1.5 and 4.5 of a walk. Indoors, `SOUND_HUMAN_WALK_GROUND` | `pWalk(Snow).wav`, and `pWalk(Soil).wav` | Engine/Object/ZzzCharacter.cpp:5348-5382, :6236-6252, ZzzOpenData.cpp:4741 |
| Doors | `SOUND_DOOR01` (swinging) and `SOUND_DOOR02` (sliding), every frame the hero is within 2 m (§3b) | `aDoor.wav`, `aCastleDoor.wav` | ZzzObject.cpp:3942, :3954, ZzzOpenData.cpp:4749-4750 |
| Music | In the safe zone, `MUSIC_CHURCH` while the hero is at X 205–214, Y 13–31 (the church), otherwise `MUSIC_DEVIAS`. Stopped on every other world. Outside the safe zone the track is left playing, not stopped | `Data/Music/Devias.mp3` (2001), `Church.mp3` (file dated 2020; whether 0.75 had it is not shown) | SceneManager.cpp:1007-1026, Core/Globals/_enum.h:172-173 |
| No blizzard | No other ambient. `SnowMan_Walk01.wav` and `xmasjumpsnowman.wav` are the Christmas event's **(later)** | — | — |

**MU2_BGFX:**

- *Already there:*
  - `world_wind` (`awind.wav`), switched by `indoors` (`src/game/play_sound.cpp:233`);
  - `assets/music/Devias.mp3` and `Church.mp3`;
  - `play_mode.cpp:745-820`, which picks the per-world track (town music is off by the user's rule; see `music-is-rare-and-in-fights`).
- *Missing:*
  - `pWalk(Snow).wav` as a `player_step_snow` entry (`sounds.json:485` says it is not carried yet);
  - the two door wavs;
  - a Devias arm in the step switch;
  - a church region if the music comes back.

## 6. Roof fade

`MoveObject` (ZzzObject.cpp:3711-3718): in Devias, types **81, 82, 96, 98 and 99** take `AlphaTarget = 0` while `HeroTile == 3 || HeroTile >= 10`, and `1` otherwise.
`Alpha()` walks toward it by 0.05 a frame if `AlphaEnable`, else 10% of the gap a frame (Engine/AI/ZzzAI.cpp:171-190).
Every roof in the map goes at once, the same blunt rule as Lorencia's tile 4.

| Type | Model (textures) | Placed |
|---|---|---|
| 81 | Object82 (`show_ston01`, `tile_wood04`): snowy roof | 20 |
| 82 | Object83 (same) | 13 |
| 96 | Object97 (`house_stone_out`) | 3 |
| 98 | Object99 (`show_ston01`, `titel_a04`) | 20, the north-east hall (218–234, 225–237) |
| 99 | Object100 (`show_ston01`, `titel_b06`) | 25, the south-west hall (11–16, 18–34) |

**Selection follows the same floor.** An NPC standing on tile 3 cannot be clicked unless the hero is also on tile 3, or on tile 11 (Input/Selection.cpp:192-199).

**MU2_BGFX:**

- *Already there:* `Town::setRoofsHidden` (all-or-nothing, which is MU2's judgement against a half-transparent roof) and `tools/cook.py`'s `roof_fade` marking.
- *Missing:* `World::indoors` tests a single Lorencia floor, `kIndoorFloor = 4` (`src/game/world/world.cpp:46`). Devias needs the set {3, 10, 11, 12, 13}. The leaves, the boids, the wind and the steps all read that one test, so making it per-world fixes all four together.

## 7. Everything else

- **The floor tiles, which decide "indoors".** Layer-1 indices are 0 `TileGrass01` (snow), 1 `TileGrass02` (snow), 2 `TileGround01` (grey gravel), 3 `TileGround02` (wooden planks: the house floor), 4 `TileGround03` (cobbles in snow), 5 `TileWater01` (ice-water), 6 `TileWood01` (blue ice), 7 `TileRock01` (pale ice), 8–9 rock, 10 `TileRock04` (flagstone with an inset), 11 `TileRock05` (red carpet), 12 `TileRock06` (black marble), 13 `TileRock07` (granite). From MapManager.cpp:1365-1448; the pictures were checked in the scratchpad.
- **Monster and NPC dust.** Monsters that trail dust while alive (`MODEL_DARK_KNIGHT`, `MODEL_LARVA`, `MODEL_CHAIN_SCORPION`, on `rand_fps_check(4)`) trail white `BITMAP_SMOKE` in Devias rather than `SMOKE + 1` (ZzzCharacter.cpp:6155-6164). None of those three lives in Devias, so this only matters for summons or visitors.
- **Event NPCs:** skeleton-costume player NPCs in Lorencia and Devias face angle 0 in Devias (ZzzCharacter.cpp:3697-3707) and chatter texts 904/905 on idle (:3441-3449). **(later)** event.
- **Portal usable** in Devias (World/MapInfra/PortalMgr.cpp:50). The cash shop's check at GameShop/NewUIInGameShop.cpp:659 is not an effect.
- **Not in MuMain at all:** footprints in snow (`BITMAP_FOOT` has a render case in Render/Effects/ZzzEffectPointer.cpp:40 but nothing creates it), breath, wind gusts, falling snow off roofs, ice reflections.

## 8. Devias's NPCs and monsters (OpenMU Version075)

**NPCs** (Persistence/Initialization/Version075/Maps/Devias.cs:47-56, names from NpcInitialization.cs:66-142, models from ZzzCharacter.cpp:14416-14475):

| NPC | Tile | Model | Effects in MuMain |
|---|---|---|---|
| 244 Caren the Barmaid | (226, 25) | `MODEL_SNOW_MERCHANT` (Npc/SnowMerchant) | none |
| 245 Izabel the Wizard | (225, 41) | `MODEL_SNOW_WIZARD` (Npc/SnowWizard) | none |
| 246 Zienna the Weapons Merchant | (186, 47) | `MODEL_SNOW_SMITH` (Npc/SnowSmith) | none. Unlike Hanzo's `MODEL_SMITH` she has no sparks, forge light or sound |
| 247 Crossbow Guard ×4 | (224, 79), (219, 79), (169, 45), (169, 39) | `MODEL_PLAYER` in Plate armour with a Light Crossbow and bolts | none |
| 241 Guild Master | (215, 45) | `MODEL_MASTER` (Npc/Master) | none in Devias; its Crywolf chatter (ZzzCharacter.cpp:3398) is later |
| 240 Baz the Vault Keeper | (218, 63) | `MODEL_STORAGE` (Npc/Storage) | none |

`MODEL_DEVIAS_TRADER` (Thompson) has anvil sparks and a forge terrain light (ZzzCharacter.cpp:6112-6131) and an idle on `rand_fps_check(32)` (:3746-3753). He is not in OpenMU V075, so he is **(later)**.

**Monsters** (Devias.cs:61-69). Not in scope yet; this is only the list.

| Monster | Spawns | Note from MuMain |
|---|---|---|
| 20 Elite Yeti | (194, 165) ×10, (36, 25) ×10, (210–242, 210–220) ×15, (0–251, 128–245) ×200 | Scale 1.4. `BITMAP_SMOKE` breath at bone 22 on `rand_fps_check(4)` (ZzzCharacter.cpp:6181-6189) |
| 25 Ice Queen | (0–128, 128–245) ×75 | Scale 1.1, `BlendMesh 2` at `BlendMeshLight 1`, `LightEnable false`, Angelic Staff. Power Wave throws three `MODEL_MAGIC2` at ±10° (:13997-14007, :5046-5055). No shadow (:8668) |
| 23 Hommerd | (0–128, 0–128) ×75 | Scale 1.15, Larkan Axe and Big Round Shield (:14013-14020) |
| 22 Ice Monster | (0–128, 0–128) ×75 | `BlendMesh 0` at light 1; `BlendMeshTexCoordV` scrolls −0.5/s (`WorldTime % 2000 × −0.0005`). No shadow (:14021-14027, :6078-6080, :8668) |
| 24 Worm | (128–251, 0–128) ×65 | Walk play speed 0.5 (ZzzOpenData.cpp:2626) |
| 21 Assassin | (128–251, 0–128) ×35 | Scale 0.95, no idle sound (ZzzCharacter.cpp:14028-14033, :1411) |
| 19 Yeti | *defined but not spawned in V075 Devias* | Its missile is `MODEL_SNOW1`, a thrown snowball (ZzzCharacter.cpp:5125-5128) |

Gates (Gates.cs:128-130, :194-197): the arrival area is (197–218, 35–50); enter gate 20 at (244–245, 34–37); enter gate 28 to the Lost Tower at (2–3, 248–249), level 40.

## 9. Already covered, and the gaps, in one table

| MU | MU2_BGFX has | Missing |
|---|---|---|
| Snow pool (§1) | `world/leaves.cpp` pool, random walk, landing fade, indoor fade | Snow kind: square additive sprite, two sheets, 1-in-10 star, −30° slant |
| Clear colour, cold light (§2) | per-world sheets | `sheets/worlds/devias.json` |
| Water tile scroll (§2) | ground shader water layer | Devias mapping of tile 5 |
| Snow-tuft grass on tile 1 (§2) | `world/grass.cpp` | Devias's grass sheet and tile rule |
| Aurora curtains, ice, candles, windows (§3a) | BlendMesh additive via the recipes, `lamps` flicker | Devias's `BLEND_MESH_BY_MAP` row; flicker 0.4–0.7 on type 78 |
| Camp and house fires, type 66 (§3a) | `world/lamps.cpp` `CreateFire` | Placement hookup |
| Lost Tower beacon, type 100 (§3a) | `world/ornaments.cpp` star | Level gate |
| Doors (§3b) | — | All of it, with sounds |
| Sled dogs (§3b) | `world/sway.cpp` clips | Action-picking idle |
| Sit and pose (§3c) | `tools/cook.py` `PERCHES` | Devias row |
| Roofs (§6) | `Town::setRoofsHidden` | Per-world indoor floor set {3, 10–13} |
| Wind (§5) | `world_wind` | — |
| Snow steps, door sounds, church music (§5) | `player_step_*`, `Sound::music` | `pWalk(Snow).wav`, `aDoor.wav`, `aCastleDoor.wav` |
| Boids, fish (§4) | — | None needed |
