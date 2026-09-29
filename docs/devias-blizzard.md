# Devias's blizzard

MU 0.75 has no blizzard: Devias's only weather is `CreateDeviasSnow` (docs/devias-effects.md §1) and its only air is `SOUND_WIND01`. Everything here is this game's, and marked so where it is set.

What the user asked for (2026-09-29):

- **The rain's logic.** A calm and a storm in turn, one share walked between them, and everything reading that share.
- **Darker, not white.** The light goes to a cold, dark storm, as the rain darkens Noria. A little more haze, never a whiteout; the game stays playable.
- **A strong snow wind.** The wind is the effect.
- **The hero feels it, visually only.** Snow gusts whip past and around them, and cloth and hair flutter harder. No camera shake (the user said no) and no gameplay change.

## Pass 1: spell, light, sound (landed)

- **Spell** (`game/world/weather.cpp`): Devias's wet spell is the blizzard. It uses the calm for the dry spell's 10-18 min and storms for 2-4 min, building over 18 s where rain takes MU's 4 s. It has no drops (peak 0, so `pour()` is 0) and no thunder. `--weather rain` holds it in the storm and `--weather dry` holds the calm. Both need `--play`, since weather only opens with the realm.
- **Light** (`sheets/worlds/devias_rain.json`, blended by `TimeOfDay::rain` like any wet spell):
  - the sun weak and grey, the sky slate, the colour and contrast eased;
  - the haze 0.004 to 0.009, greyer (about 16% at the camera's distance);
  - the lamps and glows up, so the fires glow warm through it.
- **Sound** (`world_blizzard`, `source/sounds/ablizzard.wav` cut by `tools/blizzard.py`):
  - the user's whiteout loop, at −16 dB, about −30 dB at a full storm;
  - its level is the share;
  - under a roof it is muffled to a quarter over 0.6 s rather than cut;
  - MU's wind loop plays under it.

## Pass 2: the wind

Landed 2026-09-30:

- **The snow storms** (`world/leaves.cpp`, `setStorm`):
  - the wind blows it along −x at up to 9 m/s in gusts, and it falls 1.5 to 3 m/s faster;
  - it spawns upwind and streaks along its flight past 2.5 m/s;
  - landed snow blows away;
  - the pool fills from 150 to 180.
- **Softened** ("snow flakes too much visible"): at a full storm a flake shows 25% of its light and streaks half as wide (half and 30% were too visible, 15% too faint).
- **The haze** 0.009 to 0.02, then 0.032 and paler ("stronger snow dust effect", twice).
- **The wind wanders** ("we need some randomness like wind changes"): every 6 to 14 s a new heading within about 55° of −x and a new strength from 55 to 100%. The wind turns to them at 0.35 rad/s and 0.25 a second, with the gust swell on top, and flakes spawn upwind of wherever it blows from.

Still to do:

- **Gusts from the sound.** The gust is a fixed swell (`stormGust`, five seconds with a two-second flurry). The plan is to read it off the blizzard loop's loudness instead, as the thunder's flash is read off its claps, so a gust in the eye lands with a swell in the ear.
- **Ground drift:** low, fast snow skimming the ground along the wind.
- **Grass and trees:** the grass wind and the objects' sway pushed by the gust.
- **The hero:** a denser band of gusting snow around them, and cloth and hair fluttering harder if the figures' sway can take a wind.
- **Budget:** measure the flake count at 2K before and after (docs/budget.md).

## Open

- **The chasms** stay black under the storm: the abyss is applied after the haze. That suits a darker storm, and nobody has asked otherwise.
- **The night:** the base sheet is Devias's winter day, and the storm blends over whatever the clock has made of it.
- **Levels:** the blizzard's loudness and the sheet are judged from review shots, not yet heard or seen in game.
