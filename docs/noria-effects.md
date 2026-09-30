# Noria's effects

Everything MuMain does in Noria (WD_3NORIA) beyond drawing its objects, and how much of it MU2_BGFX has.
Researched 2026-09-28 by reading every Noria-specific path in MuMain: the per-world switches, boids, leaves, sounds, NPCs, and the eight monsters OpenMU Version075 spawns there.
Line numbers are in `LEGACY/reference/MuMain/src/source`.

**Ownership, as of 2026-09-28:** the Noria cook session (mu2-bgfx-86) took items 1 and 6 below.
Items 2–5 and 7 have no owner.
Files the cook is editing, not to be touched from elsewhere: `source/world/noria/*.json`, `tools/cook*.py`, `sheets/worlds/noria.json`, and `src/game/world/{leaves,weather,maps}`.

## 1. Fidelity bug: Noria's glows are brighter and shimmer (taken by the cook)

Every Noria recipe's `emitters_from` note says its glow breathes on `g_Luminosity`, which is `sin(WorldTime * 0.004) * 0.15 + 0.6` (SceneManager.cpp:1283): 0.45 to 0.75 at 0.637 Hz.
That is not the value the Noria cases read.
`RenderObjectVisual` (Engine/Object/ZzzObject.cpp:2776) opens with its own local variable:

```cpp
float Luminosity = (float)(rand() % 30 + 70) * 0.01f;   // :2781
```

The `case WD_3NORIA` block (:2864 onward) reads that local.
So MU's glows sit at a random 0.70–1.00, re-rolled every frame.
That is about 40% brighter than the recipes, and it is a shimmer rather than a slow pulse.

- **Affected recipes:** Object10, 18, 36 and 40.
- **Fix:** set `low` to 0.70 and `high` to 1.00. Smoothing the level for a point light is still right; a raw per-frame re-roll on a light would strobe the ground.
- **Also:** the notes name `RenderObject` where the function is `RenderObjectVisual`.

## 2. The Chaos Machine's effects (no owner)

Object40–43 (types 39, 41, 42, 43) stand together at (181, 104.5) beside the Chaos Goblin.
Already cooked: the scrolling textures, the gearing and the five bone lamps.
Not yet done: three effects in ZzzObject.cpp ~:2897, `case 39`.

| What | Where | MU's numbers |
|---|---|---|
| A spinning star | bone 57 | Two `BITMAP_LIGHTNING+1` sprites at scale 1, one at `+Rotation` and one at `-Rotation`, with `Rotation = (int)(WorldTime * 0.1) % 360` (about 100°/s). Colour (0.4, 0.8, 1.0) × Luminosity. |
| Glints | bones 61–65 | On `rand_fps_check(32)`: `CreateParticle(BITMAP_SHINY, …)` twice, subtypes 0 and 1, in white. |
| Spark bursts | bone 58 | On `rand_fps_check(8)` (about 3 a second at 25 Hz): 8 × `CreateJoint(BITMAP_JOINT_SPARK)` plus 8 × `CreateParticle(BITMAP_SPARK)`, angle (rand 60–120, 140, rand 0–30). |

The spark burst is the same recipe as Hanzo's anvil, so `src/game/fx/forge.cpp` should be reusable.

## 3. The Chaos Goblin's glow (no owner)

`MODEL_MIX_NPC` (MixNpc01) carries `RenderLight(o, BITMAP_LIGHT, 1.5f, 32, 0, 0, 0)`: a light sprite on bone 32 (ZzzCharacter.cpp ~:11243).
His sound, `npc_mix` on `rand_fps_check(64)`, is already in `source/sounds/sounds.json`.
So is the elf wizard's `npc_harp`.

## 4. Butterflies (no owner)

Noria's boid is `MODEL_BUTTERFLY01`, loaded from `Data/Object1/Butterfly01`.
`boids.cpp` defers it until the model is cooked.
What its note doesn't record is that the butterfly glows.

- **Spawn** (GOBoid.cpp:1335): `Velocity 0.3`, `LightEnable false`, `Light (1, 1, 1)`.
- **Glow** (GOBoid.cpp:1558): a `BITMAP_LIGHT` sprite at scale 1 on its position, colour (0.2, 0.4, 0.4) × rand(0.64–0.96), re-rolled per frame. It reads as a cyan firefly.
- **Flight** (`MoveButterFly`, GOBoid.cpp:932): on `rand_fps_check(32)` it takes a random heading and a new vertical drift. It is pushed up below terrain + 50 and down above terrain + 300, and jitters ±2 units vertically per frame.
- **Flocking:** `MoveBoid` runs only one frame in four for butterflies (GOBoid.cpp:1135).
- **No sound.** The butterfly is not in the bird/bat call chain.

## 5. The decorative warp at tile (223, 30) (no owner)

`MapManager.cpp` ~:88–103 loads `warp01`–`warp03` from `Data/Npc` for Noria and creates a `MODEL_WARP` at (223 × 100, 30 × 100).
No gate in OpenMU Version075 or mu.db points there, so it is set dressing.

- **Stack** (ZzzObject.cpp ~:4722): five discs at z + 350, y offsets 0 / 4 / 8 / 12 / 20. In order they are WARP, WARP2, WARP, WARP2, WARP3.
- **Setup** (ZzzEffect.cpp:553):
  - WARP and WARP2 take scale 1.3–1.8, `Gravity` rand 0–8 and `BlendMesh -2`.
  - WARP3 takes scale 0.6 and `BlendMesh -2`.
- **Motion** (`Move_MODEL_WARP3`, Render/Effects/Behaviors/MoveHandlers.cpp:454): subtype 0 spins `Angle[1]` by 4 + Gravity degrees a frame. Its colour drifts on three sines, `sin(WorldTime × 0.0011 / 0.0017 / 0.0013) × 0.2 + 0.01`.
- **Shimmer** (ZzzObject.cpp ~:3967, MoveObject): `CreateParticleFpsChecked(BITMAP_SPARK+1, …, subtype 9, scale 1.4)` at (x, y − 50, z + 350), light 0.5.
- **Note:** the body copy of `Move_MODEL_WARP3` in MoveHandlers.cpp is wrapped in stray braces. Read it against the original if a number looks off.

## 6. Object02's light sprites (taken by the cook)

Object02, type 1, is the commonest object on the map, with 2,336 placements.
MU hangs three `BITMAP_LIGHT` sprites on each plant: bones 2, 4 and 6, scale 0.5, colour (0.4, 0.7, 1.0) × Luminosity.
That makes about 7,000 small blue points of light, and a large part of why Noria looks enchanted.

The recipe skips them, correctly, because they cannot be point lights.
The fix is a bone-anchored additive billboard, which the project does not have yet.
Once that exists, the sprites on Object10 (bone 1, scale 1.5), Object18 (bones 4/7/10/13, scale 1) and Object36 (bone 3, scale 1.5) should use it too.
Those are point lights today, which is only an approximation of a billboard.

## 7. Noria's monsters (no owner)

OpenMU Version075 Noria spawns monsters 26–33: Goblin, Chain Scorpion, Beetle Monster, Hunter, Forest Monster, Agon, Stone Golem and Elite Goblin.

| Monster | Effect | Source | State |
|---|---|---|---|
| Stone Golem | On death its body vanishes and it bursts into 8 × (MODEL_BIG_STONE1 + BIG_STONE2), with SOUND_BONE2 | ZzzCharacter.cpp:1489 | Not cooked. `source/effects/bigstone` exists, and `play_tuning.h` already expects it. |
| Chain Scorpion | Orange `BITMAP_LIGHT` on bone 7, colour (1, 0.4, 0.2) × Luminosity, scale 1 | ZzzCharacter.cpp:6151 | Glow missing. Its dust is in `fx/breath.h`. |
| Chain Scorpion | `BITMAP_SMOKE + 1` dust round it while alive, on `rand_fps_check(4)` | ZzzCharacter.cpp:6158 | Done, in `fx/breath.h`. |
| Beetle Monster | `BlendMesh = 1`: one mesh drawn additively | ZzzCharacter.cpp:13982 | To check against its recipe. |
| Hunter | Fires arrows from its arquebus (`CreateArrows`) | ZzzCharacter.cpp:4831 | To check. |
| Elite Goblin | The goblin model at scale 1.2, with Morning Star and Horn Shield | ZzzCharacter.cpp:13941 | Recipe. |

## Already covered in MU2_BGFX

| MU | MU2_BGFX |
|---|---|
| `CreateAtlanseLeaf`: `BITMAP_LEAF1` around the hero, where the wind turns round and drops to a crawl near the camera (ZzzEffectFireLeave.cpp:299) | `src/game/world/leaves.cpp` |
| Scrolling textures: type 18 V at 1/s, type 41 V at 0.5/s, types 42 and 43 U in opposite directions (ZzzObject.cpp ~:3967) | The Object19/42/43 recipes (`glow` `scrolls_per_second`) |
| `BlendMesh` per type: 1 → 1, 9 → 3, 18 → 2, 17/19/37 → 0, 39 → 1 (ZzzObject.cpp ~:4740) | The recipes' `additive` |
| Perches: type 8 sit, type 38 kneel and heal (the `PLAYER_HEALING1` pose) (ZzzInterface.cpp:1728) | `tools/cook.py` `PERCHES[3]` |
| `SOUND_WIND01` looping, `SOUND_FOREST01` on `rand_fps_check(512)` (SceneManager.cpp:868) | `world_wind`, `world_forest` |
| Grass footsteps on HeroTile 0 (ZzzCharacter.cpp:5364) | `player_step_grass` |
| Water tile UV scroll (`WaterMove`, ZzzLodTerrain.cpp:3595) | The ground shader's water layer |

Rain in Noria is the user's own addition (see `weather.h`); MU gives Noria leaves only.
