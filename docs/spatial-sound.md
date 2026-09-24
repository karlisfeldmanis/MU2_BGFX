# Spatial sound: what ARPGs do, and the plan for ours

Written 2026-09-24 on the user's asking how ARPGs make sound feel placed in the world and how
this game can too. It is research and a plan. It has no sprint number: `docs/roadmap.md` holds
the order, and this goes where the user puts it.

## State

**A to D landed 2026-09-24**, in `src/game/sound.{h,cpp}` and `play_sound.cpp`, and are
proved by `build/sound_test`, which is in `checks`. Left out of what landed:

- **A's "effects follow".** Missiles and the meteor still sound at one point. `follow()`
  takes any id through its callback, so this is `Play`'s side: a `Where` that knows effect
  ids as well as bodies.
- **C ducks for the level-up alone.** A heavy hit or the hero's death does not duck yet.
- **Birds calling off the frame are now refused**, as every placed sound is. Before this they
  reached `playAt` without passing `emit` and were heard at 1/d from anywhere.

E to H are not started.

## Where we stand

`src/game/sound.{h,cpp}` and `play_sound.cpp` are MU's `DSPlaySound.cpp` said again through
miniaudio 0.11.25, and they do that well:

- **The ears are at the hero, turned by the camera's yaw.** Pan and falloff come from the
  same point.
- **Flat.** Every position has y = 0.
- **1/d past 2.5 m** (`kCarry`), no maximum distance, no filter, no doppler, no reverb.
- **Two voices per event**, and a new play steals the older one. There is no global cap and no
  priority.
- **A new sound is refused off screen** (`Play::emit`'s frustum test). That is a hard edge, so
  a monster one step past the frame is silent and one step inside is at full 1/d volume.
- **The sync correction** (the silent lead is skipped, and the device latency too for the
  level-up). This is the part most engines get wrong, and it stays as it is.
- One unplaced loop (the wind), switched off indoors.

The assets are mostly **mono at 22 kHz** (about 130 of 200 sampled). Mono is what a spatialiser
wants. At 22 kHz there is almost nothing above 11 kHz, which matters for HRTF (see G).

## What the genre does

From Blizzard's Diablo IV audio posts, the isometric-listener discussions (Unreal/FMOD
forums, the Above Noise and Teedstuff write-ups), Wwise's Spatial Audio feature set, and
Steam Audio's documentation:

1. **Split the listener.** An isometric camera sits 15-40 m up. With the camera as the ears,
   everything sounds far away and equally loud. With the hero as the ears, the pan can
   disagree with the screen. The usual answer is **loudness from the hero, left/right from
   the screen**. Sometimes the listener is simply placed part way between the two. Ours is
   already hero plus camera yaw, which is most of the way there.
2. **Voice budget and importance** before anything spatial. Diablo IV's team: *"the playback
   engine within the game will not trigger too many instances of a sound if they are trying
   to play at the same time"*, and *"an audio importance system that will allow certain
   important monster sounds to poke out when they are needed."* An isometric screen shows
   thirty monsters at once. Without priority, "spatial" just means a louder smear.
3. **Buses and ducking.** UI, hero, combat, world and ambience are mixed separately. The
   hero's big moments (a crit, a level-up, a skill) take a few dB off the ambience for a
   moment.
4. **Distance is heard as tone as well as level.** A low-pass that closes with distance (air
   absorption) makes near and far audible even when the levels are close. This is the
   cheapest real "depth" cue.
5. **Space: reverb by zone.** A dry, short one outdoors, long in a dungeon or indoors, fed from
   a send bus rather than one reverb per voice. Diablo IV mentions *"high-quality reverbs, and
   environment reactive delay/echoes"*.
6. **Occlusion.** A sound behind a wall is muffled and quieter. Diablo IV uses it *"to pay
   close attention to what might be just around the corner"*. Top-down games get away with a
   2D line-of-sight test. Nobody traces rays through the mesh for an ARPG.
7. **Living ambience.** Point emitters on the world's own things (fire, water, a smithy) plus
   scattered one-shots around the listener (birds, crickets, a dog) at random intervals.
   Blizzard's "Living Audio" pillar: *"the soundscape is ever evolving and never static."*
   This is most of what players describe as "3D sound" in a top-down game.
8. **HRTF / binaural, optional.** Diablo IV exposes Windows Sonic/Atmos rather than doing it in
   game. It helps with headphones and does little on speakers. Top-down, "behind" is just "below
   on screen", so the front/back cue HRTF exists for matters less than in a shooter.
   miniaudio ships a Steam Audio example (`examples/engine_steamaudio.c`): a custom `ma_node`
   around `IPLBinauralEffect`, one per voice, with one shared `IPLHRTF`.

## The plan

Seven steps, ordered by effect per line of code. Each one ships and is judged on its own. None
of them touch `sim/` or the seeded log: sound is `game`, reads the realm and never writes it.

Every step below is **an invention against MU 0.75**, which has none of it. They are marked
as such in the code as they land, in the way `emit`'s culling already is.

### A. The heard screen: our own pan and our own falloff

Stop letting miniaudio spatialise, and compute the two numbers ourselves, once per voice per
frame. The sound is `MA_SOUND_FLAG_NO_SPATIALIZATION`, and we drive `ma_sound_set_pan` and
`ma_sound_set_volume` directly.

- **Pan from the screen.** Take the source's projected screen x (the camera `Play` already
  holds, `shot_`), map it to the range −1…1, and scale by `kPanWidth` (about 0.7) so nothing
  ever sits in one ear only. Anything near the hero stays near the middle, because it is near
  the middle of the screen. The pan is exactly what the eye sees, which the yaw-turned listener
  only approximates.
- **Loudness from the hero.** Full volume inside `kCarry`. Past it, a curve that reaches
  **silence at the frame's edge plus a margin**, instead of 1/d forever and a hard refusal at
  the frustum. That removes the pop at the frame's edge and the "anvil across the map" problem
  together, and `emit`'s frustum test becomes a cheap early out rather than the rule.
- **Height kept.** Positions carry y again, so a meteor falling from the sky and the
  level-up's flare can be placed where they are. Only the pan and the curve read it.
- **Effects follow, not just bodies.** `follow()` learns missiles and effects as well as
  `Drawn` ids, so an arrow, a fireball or the Lich's meteor carries its whoosh across the
  screen. This alone is a large part of the "3D" feel.

Cost: a projection and a few multiplies per sounding voice, on the main thread. The mix
itself stays on miniaudio's thread.
Proof: see "How it is judged".

### B. The voice budget and importance

- **A global cap**, `kVoicesTotal` of about 24 placed voices, on top of the per-event two.
- **Importance classes:** the hero's own sounds > the monster he is fighting > monsters
  attacking him > the rest of the crowd > ambience. When the cap is full, a new sound steals
  the quietest voice of lower class (its current gain from A), or is refused.
- **Merging on the same frame:** six spiders biting on one tick become one play, a little
  louder (+3 dB for each doubling, capped), rather than six plays that all steal from each
  other.

Cost: a sort of at most a few dozen entries on a play.
Proof: a log line per second in a crowd, `sound: 17/24 voices, 3 refused, 5 merged`.

### C. Buses and ducking

`ma_sound_group`s: `ui`, `hero`, `combat`, `world`, `ambience`, all under the engine. Each
voice is attached to its group when it is loaded. The level-up, a death and a heavy hit duck
`ambience` and `world` by about 4 dB over 50 ms and let them back over 400 ms. The groups are
also where a volume setting will hang when the options window exists.

Cost: nothing measurable.

### D. Distance as tone

One `ma_lpf_node` (second order) per placed voice, between the sound and its bus. Its cutoff
falls from open to about 3 kHz as A's distance factor goes from 0 to 1, updated by
`ma_lpf_node_reinit` only when the change is more than a small step. On 22 kHz sources the
top of that range is already empty. The audible part is the 2-6 kHz band closing, which is
exactly where "far" lives.

Cost: about 60 biquads on the audio thread. Negligible.

### E. Space: one reverb send

miniaudio has no reverb. Options:

- **verblib** (single header, public domain, Freeverb): a custom `ma_node` of about 60 lines.
  **Recommended.**
- Our own Schroeder reverb: about 150 lines. No gain over verblib.
- Steam Audio's parametric reverb: pulls in the whole SDK for one effect.

The layout is `world` + `combat` → splitter → a reverb node on a send → the engine. Two
presets are chosen by the same `indoors` flag that switches the wind: **outdoors**, short and
mostly dry (a slap off Lorencia's walls, wet about −18 dB); **indoors**, a small room (wet
about −10 dB, longer decay), crossfaded over the same 150 ms as the wind. Dungeons later get
their own preset, and that is where it will matter most.

Cost: one reverb on the audio thread. About 1-2% of a core.

### F. Occlusion off the tile grid

On a play, and then every few frames while a voice sounds, ask `sim::Route::sees(hero, source,
wall)`. It is the line-of-sight test the rules already use, const and on the tile grid. A
blocked voice takes −6 dB and a lower cutoff in D's filter, eased over 100 ms so a monster
stepping round a corner opens up rather than clicks. In Lorencia this means a bull behind a
house, or Hanzo heard through his own walls.

Cost: a grid walk per sounding voice every few frames.
Mark it optional: it proves its worth in dungeons more than in the town.

### G. Living ambience

- **Emitters on the world's own things**, looping, placed, and through A like everything
  else: the bonfires and torches that `08a-the-lamps` already knows (crackle), the fountain
  and water, Hanzo's forge (roar under the hammer). Only the nearest few sound, which B's cap
  gives for free.
- **A scatterer**: every few seconds, one short one-shot placed at a random point 8-20 m from
  the hero on a free tile, picked from a small bank for the time of day and the floor. That
  is crickets and an owl for Lorencia's night, and birds for day when day exists.
- **The assets are the open question.** MU 0.75 has almost none of these. It would take
  CC0/licensed recordings or later MU clients' ambience, cooked through `cook_showing` like
  every other sound.

### H. HRTF: last, and only as a headphones setting

Steam Audio through the miniaudio example's binaural node, one effect per placed voice,
switched on from the options window.

It comes last on purpose:

- Top-down, the front/back cue it is for barely applies.
- Our 22 kHz assets lack the pinna band above 11 kHz, where much of what HRTF uses lives.
- It forces a fixed processing block (256 or 512 frames). That is 5-11 ms more latency, which
  `Sound::open`'s sync correction must then take into account.
- It adds a Steam Audio dylib to the bundle.

Revisit only after A-G, and only if headphone play asks for it.

## How it is judged

Sound cannot be looked at, and window runs are expensive (`qa-runs-sparingly`), so:

- **An offline mixdown bench.** `--bench sound` opens the engine with `noDevice = true`, runs a
  scripted scene (a source orbiting the hero at 2, 6 and 15 m; one walking off the frame's
  edge; six spiders on one tick; a bull behind a house), pulls the mix with
  `ma_engine_read_pcm_frames`, and writes a WAV plus a table of left/right RMS per 100 ms.
  The pan following screen x, the fade reaching zero at the edge with no step, the merge and
  the occlusion dip are all numbers in that table. The WAV is there for the user's ears.
- **The existing log.** `sound: heard …` at shutdown, plus B's per-second voice line.
- **Frame cost:** A's per-voice update is timed in the stats harness. Everything else is on
  the audio thread and does not reach the frame.

## Suggested order

A and B first, as one sprint (done, with C and D): together they are the "ARPG sound" and are pure code on
existing assets. C and D are a day and ride along. E comes next. F waits for a dungeon, and G
waits for its assets. H comes last or never.

## Questions for the user

1. **Departing from MU's model.** A replaces MU's hero-centred 1/d with a screen-derived pan
   and a falloff that ends at the frame. Is that departure wanted, or should A stay a toggle
   beside the faithful model?
2. **Ambience assets for G.** Source CC0 recordings, or wait for later MU clients'
   ambience?
3. **Where this sits in `roadmap.md`.** It could go before sprint 9 or after 13.

## Sources

- Diablo IV quarterly update, October 2021 (sound design):
  https://www.diablofans.com/news/49640-diablo-iv-quarterly-update-october-2021
- A Sound Effect, Diablo IV sound: https://www.asoundeffect.com/diablo-iv-sound/
- Isometric listener placement: https://forums.unrealengine.com/t/audio-listener-override-solution-for-isometric-topdown-game/2351089,
  https://abovenoisestudios.com/blogeng/fmodue5listenereng,
  https://teedteed.wordpress.com/2018/02/01/third-person-audio-listener-position/
- Understanding interactive 3D audio in isometric games (thesis):
  https://www.diva-portal.org/smash/get/diva2:832631/FULLTEXT01.pdf
- Wwise Spatial Audio: https://www.audiokinetic.com/products/wwise-spatial-audio/
- miniaudio + Steam Audio: https://github.com/mackron/miniaudio/blob/master/examples/engine_steamaudio.c
- Steam Audio HRTF: https://valvesoftware.github.io/steam-audio/doc/capi/hrtf.html
