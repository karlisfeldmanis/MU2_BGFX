# Icarus port: the record, the data, and how it plugs in

Research notes, 2026-10-06. Icarus is MU server map **10**, client `Data/World11` + `Data/Object11`
(MuMain's `WD_10HEAVEN`, WebZen's "sky land"), entered from the Lost Tower's seventh floor. Three
research passes wrote this page in `docs/tarkan-port.md`'s shape. Part A is the record, Part B is the
data and the look, and Part C is the engine side, ending in the build steps. Nothing but this page was
edited, nothing was cooked or written to mu.db, and no window was run. Passes B and C ran
`terrain.py` and MuExtract into scratch only.

The passes' scratch is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/129c72c0-811f-4da1-b4a6-e27b489786ba/scratchpad/icarus/`:
- `a/`: the grid decodes, the spawn and stat comparisons, WebZen Korea's 2003 patch notes (Wayback copies), and `wzsrc/`, the WZ files the shared clone has lost (`MapClass.cpp`, `MoveCommand.cpp`);
- `b/`: the terrain.py run, every Object11 model and Monster51-57 exported, the decoded sheets, and the contact sheets `sheet_world11.png`, `sheet_object11_textures.png`, `sheet_objects.png`, `sheet_monsters.png`, `sheet_monster_textures.png`, `placements_map.png`, `light_big.png`, `att_client_om_diff.png`;
- `c/`: a second terrain.py run, the grid scripts, MuMain's `WD_10HEAVEN` list and the per-world grep.

## Where it stands

**Step 1 is built (2026-10-06, the user: 'lets work on that ground/flat plane').** MU's way: no
ground drawn, the hero on the unseen plane over a navy clear. It added:
- `source/world/icarus`: World11 through `terrain.py` (flat 2.85 m, 6 slots, 10.7% walkable, 0% safe, 670 placements of 15 kinds, none missing);
- `terrain.py` rows for map 10: `HIDDEN_BY_MAP {0..5}` (the cloud emitters), `GRASS_BY_MAP []`, and a new `SKY_BY_MAP = {10}` that writes `"sky": true` into the world json;
- `content::Ground`'s `sky`: the grid, heights and painted light load as on any world, and `draws()` refuses every part, so no ground is drawn, shadowed or prepassed;
- a lighting-sheet key, `clear_colour` (`gfx::Lighting::clearColour`, black by default): the shade view clears to it through bgfx's float palette (slot 1), since the packed 8-bit clear rounds a dark linear blue away on the HDR target;
- one decoded sheet, `ic_tilegrass01.png`, and five copies of it under the names World11's slots ask for but don't ship (`ic_tilegrass02`, `ic_tilerock01-04`; the ground pairs surfaces by sheet name, as Atlans's parked slot 5). The ground is built only because `index.py worlds()` and `Ground::load` need it;
- the `kMaps` row `{"icarus", 10, {15, 13}, true, 2}` (arrival gate 63's middle; underground for now, so no leaves or wind until step 3; home Devias), the banner and minimap names;
- `sheets/worlds/icarus.json`: B §6's starting sheet plus `clear_colour (0.004, 0.03, 0.07)`, which shows as (5, 30, 66) on screen against MU's (3, 25, 44): a sky, not a hole.

Shot muted at the door (15,13): the hero standing on nothing over the navy, 150-190 fps with the HUD
on, 77 draws. The only error in the log is the missing `icarus.mut` (no objects cooked yet). Tarkan
shot after it, unchanged. Not yet judged by the user. No gates yet, so only `--world icarus` reaches
it, and the travel window's dimmed "Icarus" card is gone until a travel row names map 10 (C §1.3 R7).

**Steps 2 and 3 are built (2026-10-06, the user: 'continue with basic stuff before walls and
monsters').** The decisions taken are the passes' recommendations, until the user says otherwise:
door 160 in and free out (2), home Devias (5), and WebZen's three no-wingless rules (8).
- **Gates**: `src/sim/gates.cpp` enter 62 (Lost Tower 7, 17-19,250, level 160, `fly`) to exit 63 (Icarus 14-16,13, facing east), and enter 64 (Icarus 14-16,12, free) to exit 65 (floor 7, 17-19,249, facing west). `index.py` `GATE_BOXES_BY_MAP[4]` and `[10]`; both worlds' tables recooked.
- **The flight rule**, one test, `sim::canFly(tables, bag)`: a wing worn (its presence, as WebZen asks), or a Horn of Dinorant with life, and never the Horn of Uniria worn.
  - Gate 62 asks it: Barred with level -1, said as "You need wings or a Dinorant to enter Icarus."
  - In Icarus, `Realm::moveItem` refuses any move that would leave him unable to fly (tried on a copy of the bag): the last wing or Dinorant coming off, Uniria going on.
  - In Icarus, one who cannot fly is sent home at once (`Realm::step`, once, `grounded_`), through `warpHome`, the Town Portal's warp moved out of `useItem`. The game now hears a realm-made `Warped` home in the happenings loop, so the mode takes him to Devias.
  - No summons in Icarus (`Realm::conjure`).
- **The air**: `world_heaven` (aHeaven.wav, -25.2 dB, on the same -44 dB bed as the desert and the water), looped over the whole map, the room Open. `dinorantFlies` takes Icarus: the rider at 90 cm, the dragon 10 under him.
- **The rain**: `Weather` holds a steady faint share there (`kSkyShare` 0.25, ours, under Lorencia's drizzle; MU rains always), silent: no rain loop and no thunder, as MU. `Leaves::setSky`: open air though the map is "underground", no leaves while it rains, and no ring where a drop dies at the unseen plane.
- `sim_test` `testIcarusGates`: in at 160 winged and on a Dinorant alone, Barred at 159, Barred unwinged and with Uniria over wings, back free from 15,12, the wings locked on in Icarus and freed once a Dinorant is worn, Uniria refused, an unwinged knight sent home, no safe box. 6371 checks, the standing 17 failing.

Seen in muted `--play` runs at the door (15,13): flying on Satan's wings in a faint slanted rain (406 fps), riding the Dinorant in flight (397 fps), and unwinged, sent to Devias's 207,42 within the first tick. Not yet: the travel row (step 8; `realm_travel.cpp` is held open by another session), the summon's death at the door, a Dinorant's flap wake in the clouds.

**The cloud road (2026-10-06, the user: 'lets migrate other effects which icarus had for that sky
effect').** Step 5's banks, MU's own (decision 3's first half):
- `game/world/sky_clouds.*`: the 335 Object01-06 boxes cooked as world anchors, kinds 12-17 (`EmitterKind::Cloud0` + the box's type; `tools/cook.py` `ANCHOR_KINDS_BY_WORLD["icarus"]`, no light). Each throws MU's 20 (types 0-2) or 10 (3-5) standing `clouds.jpg` puffs: scattered +-2.5 m and 20-39 units up, Scale 1.8-2.0 of the 256-texel sheet (4.6-5.1 m), light 0.1, added, bobbing 20 units on a 31 s sine, turning about a turn in sixteen seconds one way or the other by the type (or by index for 0 and 3). 3800 puffs in all; drawn within 30 m of the camera's point (400 at the door, 730 at 55,75).
- The lit edge: on MU's flash (one frame in fifty), one time in ten a bank in reach takes `cloudLight.jpg` at Scale 0.5 in a dim random colour, shrinking out over eight frames.
- Effect sheets `cloud` and `cloud_light` (`source/effects/clouds/`, decoded from `Effect/clouds.OZJ` and `cloudLight.OZJ`).

Shot muted at the door: a bright bank under the hero, thinning to navy at its edges, as MU's.

**The clouds reworked (2026-10-06, the user: 'clouds needs more work', 'look repetetive and not
realistical').** MU's clouds.jpg has one hot white spot at its foot; stamped 400 times and turning,
that is what repeated. Ours, marked in `sky_clouds.h`: MU's 335 banks stand where they were, but
each wears 2 (types 0-2) or 1 (3-5) of nine lit clouds seen from above, a 3x3 `sky_clouds` sheet
from `pipeline/cloud_sheet.py` under Icarus's seed (10), alpha-blended, 3-5 m across, stretched,
shaded in the moon's blue-grey at 0.6-1 of it, thinned to 0.5-0.8, drifting at most 0.02 rad/s;
and one bank in two hangs a big dark blue cloud 3-7 m under the road. 553 in all. A first cut at
five and three a bank, white and near opaque, was a blanket with no navy left. Shot muted at the
door and at 55,75: a moonlit cloud sea with depth, GPU 2.7 ms. Some banding shows in the clouds'
gradients, likely the effect sheet's compression.

**Thinner and alive (2026-10-06, the user: 'clouds is much to light they have to be much more
transparent and i think they shoould animate').** Ours: the road's clouds at 0.16-0.30 of opaque
and the deck's at 0.14-0.24, so the navy shows through every one. Each cloud wanders 1.5-2.5 m
(the deck's 3-5 m) about its place on two sines of 35-70 s, breathes 12% on 15-30 s, turns at
most 0.04 rad/s, and cross-fades on 20-45 s between its own cloud and another of the nine, the
second turned 0.6 rad against it so the change reads as a roll: two sprites a cloud, 548 clouds.
The sheet is read as the PNG (9 MB with mips), not the showing's BC7, whose sixteen alpha steps a
block drew the thin edges as lines; and `fs_present` now adds half an 8-bit step of interleaved
gradient noise before the write, so slow dark gradients fall into grain, not contours (every
world; invisible otherwise). Shot muted at 55,75, GPU 3.3 ms.

**Fainter, and always changing (2026-10-06, the user: 'we need that cloud change shape and are less
vissible more transparent').** The road's clouds at 0.08-0.15 of opaque, the deck's 0.07-0.12. The
change no longer goes back and forth between two clouds: two layers half a cycle apart, each
weighed sin^2 so they sum to one, each taking a new cloud of the nine (hashed by the cloud and the
cycle) and a new turn the moment its weight is nought, every 8-16 s; and the outline deforms, the
width and height swelling 20% out of step. Two shots 8 s apart at 55,75 show different clouds.
GPU 4.0 ms with other sessions on the machine.

**Smoke, not clouds (2026-10-06, the user: 'we need them not shapy but more blury and smoky, and they
have to be not look like puffs').** `sky_clouds.png` is now nine smoky wisps, `pipeline/cloud_sheet.py
out SEED GRID smoke` (seed 10, 3x3): noise warped twice and pulled into streaks, a broad soft oval
fade, the whole blurred, flat grey with no lit side. Each bank wears 3 (types 0-2) or 2 (3-5),
quads 9-15 m wide at 0.07-0.13 of opaque, and the deck's 20-30 m; 863 wisps in all, so they run
together as one smoky layer. Drawn within 24 m of the camera's point (was 30), faded over the last
4 m. Shot at the door and 55,75: a continuous blurred mist, no puffs. The user, 2026-10-06: 'clouds is nice now'.

**The sky's light (2026-10-06, the user: 'lets now work on light and fire emiters').** MU's Icarus
has no fire and no object light: MoveObject's WD_10HEAVEN arm is empty. All of its light is MU's
own sky, now in `game/world/sky_clouds.*`:
- **The flash** (MoveHeavenThunder, `ZzzObject.cpp:4254-4335`): one reference frame in fifty, L 0.2-0.35, a passing point light of L x (0.3, 0.3, 0.081) two tiles round a point 1.5 m from the hero, for two frames (handed to the renderer with the other transient lights), and under him, ours for MU's cloud.bmd at Scale 10, a big added clouds.jpg of the same colour.
- **The bolts**: one flash in five, MU's two BITMAP_JOINT_THUNDER + 1 between two points from its four fixed layouts, 3 m under him, through `Thunder::fork`.
- **The glints** (MoveObjectSetting, BITMAP_LIGHT sub 0, `ZzzEffect.cpp:1045-1060, 6989-7000`): one frame in ten, a light 10 m under a point 25 m round him, thrown up at 70 degrees on a heading of 30, slowing 0.01 units a frame, 400 frames, shedding a flare01 spark each frame that rises, jitters and shrinks over 10-20 frames. Ours: the sparks at 0.07 of light, not MU's 0.3 (fifteen-odd overlapping summed to a white blot), and at 0.4 of MU's size (the user, 2026-10-06: 'those flying "souls" can be smaller'); thrown only within 24 m of the camera's point.
- **The crackles** (MoveObjectOnEffect, `ZzzObject.cpp:4384-4405`; BITMAP_JOINT_THUNDER sub 6, `ZzzEffectJoint.cpp:1145-1153, 4727-4805, 4911-4915`), added 2026-10-06: with each lit edge, two scribbles out of the bank. Each waits 2-21 frames dark, then shows four, walked afresh from the bank each frame: 49 strides of 15-25 units, aimed at a point 21-23 m north-east and 100 m down and thrown 20-120 degrees off in tilt and turn; JointThunder01 twice along it, scrolling, 10-29 units wide, added in a grey of 0.1-0.7. Ours: which way "off" leans (MU's AngleMatrix signs untraced).

Shot muted at 30,40: faint glints rising through the mist. The crackles shot muted at 30,40 with edges forced by a temporary switch (removed): thin jagged white scribbles dropping from banks; in play about one lit edge in 20 s, four frames each. Not yet judged by the user. The flash and bolts are random and unshot. **Cost unmeasured**: another
session's texcook was at 563% CPU and the view timers summed past the frame; the runs read
5.5-7.3 ms GPU with waiting counted. Measure quiet before the next effects. **Unmeasured**: the run gave a 7.4 ms GPU frame, but two other `mu2` runs and Siri were on the machine; measure quiet before tuning the reach or the count. Not yet: the flash's cloud mesh, the glints ten metres down, Object11's motes (an object step), the wisps and the dragons.

**Objects batch 1: the balustrade (2026-10-06, the user: 'lets start to work with icarus objects
if we are done with effects').** Object11, 180 placements, from `source/world/icarus/Object11.{json,obj}`:
sheets `ic_top02_r` and `ic_gyg_r` (both marble), built static (its six bones hold one key; bone 3,
Bone01 at -139.9, 0, 500.6 units, is where the motes will rise from). `tools/cook.py` `FLAT_OVER_VOID`
takes icarus, so none of the placements, which stand 3 m under the plane as MU's, is held as buried.
MU's height kept (decision 4). MU draws the gyg_R statue card opaque (a 0.65 x 1.7 m card on the
pillar's face, two on the arch's spandrels); in our light they read as photographs in black boxes
(the user: 'this looks wrong'), so ours cuts the black to alpha; cut out, the dark figures stood out
('to much vissivle has to be more intergrated on wall'), so the card is repainted in the pillar's own
frosted stone, shaded softly by MU's figure: a faint carving, seen up close. Shot muted at the door and at 30,40: the arches line the road with their statues on
top, 360 fps. Not yet judged by the user. Effects still to do: the wisps, the dragons, the item bob,
and Object11's motes.

**Objects batch 2: the rest of the static stone (2026-10-06, the user: 'perfect do next objects').**
Object07 and 16 (35 floor plates, the sheet's dark foot and white crown), Object13 (31 domed temples,
miniatures off the road), Object14 (11 shard scatters) and Object15 (39 stair-platforms), all
top02_R marble and built static (their bones hold one key), at MU's heights. Object14 takes
`sheet_roughness` 0.65 (ours): at marble's 0.35 every shallow shard flashed a white spot at its apex.
Shot muted at 16,28, 32,33, 26,63 and 52,68, 280-330 fps. Not yet judged by the user.

**Objects batch 3: the floating pillars (2026-10-06, the user: 'do next objects').** Object08-10, 39
pillars 4.5 m tall on top02_R marble, built with their 3-bone clips (Object08's 12 keys rock it,
09's and 10's 24 bob it and turn it about its axis), played at the 0.16 every object starts with, as
MoveObject's WD_10HEAVEN arm is empty. Shot muted at 16,31 and 26,62, two frames apart: the tipped
pillar by the road turns. 325-335 fps. Every placed kind of Object11 now stands.

**Object11's motes (2026-10-06, the user: 'next').** In `game/world/sky_clouds.*`: every Object11
throws a BITMAP_LIGHT sub 0 spark at its bone 3, the statue's top, every frame (`ZzzObject.cpp:3171-3178`),
Scale 0.5-1, 10-19 frames, rising 2.5 units and shrinking 0.05 a frame. The 180 points are turned and
scaled with each placement at open. Every spark, glint's or mote's, now wanders MU's +-0.2 units a frame
(`ZzzEffectParticle.cpp:7948-7958`). Ours: only those within kReach are thrown; 0.18 of MU's light
at 0.7 of its size (the glints' 0.07 and 0.4 left them unseen); MU's particle wind left out. Shot
muted at 30,40: faint streams rising off the statues. The objects step is done.

**The wind wisps (2026-10-06, the user: 'next').** In `game/world/sky_clouds.*`: ten of Icarus's
thirteen boids (`GOBoid.cpp:1226-1240`; the other three are the dragons) are unseen MODEL_SPEARSKILL
bugs at his height, born within 5 m, drifting 2.2 units a frame on a slowly wandering heading
(MoveHeavenBug), gone past 15 m or one frame in 5120 and born again. Each carries a MODEL_SPEARSKILL
sub 1 joint (`ZzzEffectJoint.cpp:1538-1567, 4476-4546`) whose head turns round it on three slow sines,
0.7 m out and 1.4 m up or down, leaving thirty tails of JointSpirit01, 25 units wide, added in MU's
blue (0.2, 0.2, 0.2-0.6) (the briefing's "grey 0.2" was the creation colour; the move sets the blue
every frame). The crackles and wisps now share one strip drawer. Shot muted at 30,40: pale blue
ribbons drifting round him, 258 fps. At MU's light; not yet judged.

**The dragons (2026-10-06, the user: 'next').** MU's Monster32 (MODEL_DRAGON_, the Red Dragon's
body) built as `source/world/icarus/Dragon01` with its one clip, action 7 (MONSTER01_DIE + 1), its
sheets `ic_drr_01-03` (chitin), and cooked through `tools/cook.py` AIRS. `game/world/flight`'s new
dragon mode is CreateDragon and MoveBoidGroup (`GOBoid.cpp:824-851, 1140-1196`): three, born again
the frame one is gone, within 20 m of him and 6 m under him, a random heading, Scale 0.30-0.40,
Velocity 0.2-0.38: gliding straight at Velocity x 25 units a frame (MoveBoid would turn one by
(int)Gravity degrees, and 0.5-0.95 truncates to none), gone past 40 m; the clip played at its
Velocity, keys a frame. `game/world/boids` draws them unlit in MU's BodyLight (0.02, 0.05, 0.15)
(`ZzzObject.cpp:487-493`), no calls. Shot muted at 30,40 over 1500 frames: a blue-grey dragon gliding
head-first under the mist. boids_test passes. Then ours, the birds' arrival (the user: 'make them arrive from outside the
frame, as our birds do'): a dragon is born on a ring 18-30 m out, redrawn until its middle and four
points 1.5 m round it are all off screen, aimed back across him within 55 degrees; it goes past
40 m, or once it has crossed the frame, only while out of it. Shot muted over 3000 frames.

**Drops lie still (2026-10-06).** MU bobs every lying item +-10 cm in Icarus (RenderItems,
`ZzzObject.cpp:6497-6500`); built, then turned down by the user: 'dont animate droped item thay have
to stay solid'. Not carried; don't add it back. The sky-effects phase is done; next is the monsters.

**The ambient (2026-10-06, the user: 'lets play freesound_community-horror-ambient-14590.mp3 in icarus
on loop').** Ours, in place of MU's MUSIC_ICARUS: `source/music/icarus_ambient.mp3` (freesound
community, 2:08, mean -22.3 dB, 5 dB under the loading ambient) loops over the whole map at the
themes' 0.6 (`PlayMode`'s music block, a `loops` track with no rest), under aHeaven's air.

## The one thing to know first

**Icarus has no ground.** MuMain skips `RenderTerrain` on map 10 (`MM/Scenes/MainScene.cpp:463`).
The hero walks an invisible flat plane at 2.85 m (the height file is byte 190 everywhere). Under and
round him is the clear colour, navy (3, 25, 44)/256 (`SceneManager.cpp:385`). The "road" is 335 hidden
emitters (Object01-06) throwing additive cloud puffs about 1.2 m under his feet
(`ZzzObject.cpp:3090-3169`). Every object stands about 3 m under the walkers. So the first build is
not a ground cook in the usual sense. It is a world that is built for the grid and the route but
never drawn, plus a sky colour, which the engine has no switch for today.

The second thing: **one must fly to enter.** Wings or a Dinorant, never a Uniria
(`OM/Version095d/Maps/Icarus.cs:46-50`; WZ `user.cpp:27201-27220`; MuMain's inventory refuses Uniria
there). Inside, WZ will not let him take off his last flying item, a broken Dinorant with no wings
sends him to Devias, and summons are refused. No source has a fall.

## In one screen

- **Version.** Not 0.75. OM has it from 0.95d, but with no gate, so OM's 0.95d cannot reach it (as
  with Tarkan). It went live in Korea on 2003-07-15, in the patch that brought the class change, the
  second-class gear and the Dinorant. All three are already in this game (A §1.1).
- **The grid.** One S-shaped strip of 7,026 walkable tiles (10.7%), x 9-97, y 12-241. Everything else
  is NoMove. It has no NoGround, no safe tile and no town. The client's grid is used: OM's
  `Terrain11.att` has 369 corrupt tiles in rows 157-255, and WZ's doesn't match at all (B §1.1).
- **Gates.** 62 at the south end of the Lost Tower's floor 7 (17-19,250, level 160) takes him to 63 (14-16,13). The way back is 64 (14-16,12, level 50 in S6 or 80 in WZ) to 65 (17-19,249). LT7's arrival is 225 steps from the door.
  The S6 warp costs 10,000 zen at level 170. Icarus touches no other world.
- **Death.** WZ and WebZen Korea's own note send it to **Devias**. OM's Lost Tower hall is OM's own
  choice. This settles lost-tower-port's open split.
- **Monsters.** 64 one-tile spawns (the Dark Phoenix included) of eight breeds, levels 75-108. They
  get harder down the road: Alquamos at the door, the Phoenix at 34,238, 290 steps away. OM's spawns match WZO's but for one tile, and its stats match WZO 1.00m to the digit. Its respawns don't: 3 s for four breeds and 15 s for the Phoenix, where WZO has 10-30 s and 150 s. Bodies are Monster51-57. Two monster swords, Thunder Blade and Dark Breaker, are unbuilt. The Phoenix (108) would be the highest monster in the game.
- **Life in the sky.** Constant rain and silent lightning. Up to three dark dragons (Monster32) glide below, and ten wind wisps circle the hero. 180 pillar-arches line the road, each throwing a rising mote. The 39 floating pillars are the only animated placements, and MoveObject has no arm for this map.
- **Sound.** aHeaven.wav loops; icarus.mp3 is in `assets/music` already; 16 monster wavs, none in
  `source/sounds`.
- **Engine gaps.** These are missing:
  - a world that is built but never drawn (`Ground` has no `sky` switch);
  - a clear colour per sheet (`renderer.cpp:979` hard-codes black);
  - the whole flight rule;
  - `MapRow.home` for a map with no safe zone (left at 0, deaths go to Lorencia);
  - the cloud emitter row;
  - cloth;
  - the Phoenix's two-model figure.

  The fly stance and the Dinorant's flight already exist.
- **Frame.** Expected well under Tarkan's: no ground, no grass, 670 placements. The cloud puffs are an
  estimated 0.05-0.09 ms, unmeasured.

## Where the passes disagree, settled

| question | A | B | C | settled |
|---|---|---|---|---|
| spawn count | 64 incl. the Phoenix | 64 | "64 + boss = 65", ids 1483-1547 | **64** (`Icarus.cs:55-118`, 64 lines, the Phoenix on `:118`). Ids 1483-1546. C's step 7 checks must read 64 |
| our Dinorant's level | 160 | -- | "110" | **160**, the user's (`source/items/pets/Helper04.json:28, 36`, 2026-10-02, OM's 110 declined). So no one enters before 160 whatever the door asks |
| death landing | Devias (WZ, KR) | Devias | Devias, `home 2` | **Devias**, pending decision 4 |
| Phoenix respawn | WZO's 150 s +1 | -- | WZ's 30 min | open, decision 6 (WZ `Monster.txt` 1800 s is the 2012 repack's WZD; WZO's 150 s is the official) |
| the grid | client | client (OM's corrupt) | client | **the client's** |

## Traps a first build would fall into

- **Marking the void NoGround closes the road.** The walker's threshold counts 0x08 (B §3). The
  "don't draw" switch must be per world, not per tile.
- **`index.py worlds()` needs a built ground glb** (`:1519-1527`). An undrawn world still builds one.
- **`home` left at 0 sends every Icarus death to Lorencia.** No safe zone means no spawn gate to fall
  back on (C §4.3).
- **The Lost Tower has no `GATE_BOXES_BY_MAP[4]` row** in index.py. The door is on its side, so map 4
  is recooked in step 2.
- **The 3 m object offset.** With no ground, `FLAT_OVER_VOID` must take icarus before any object is
  cooked, or the cook "buries" or lifts the islands (C step 4).
- **MU's TerrainLight is grey 50 with white streaks along the west stem.** Taken raw, it flickers a
  walker dark to bright (B §2, decision 3).
- **Textures before `--only meshes`**, as for every world, or the islands go white.
- **The shared WZ clone has lost `MapClass.cpp` and `MoveCommand.cpp`.** Read them from `a/wzsrc/`.
- Kind 76 (the Phoenix's shield) is defined but never placed. WZ raises it in code
  (`user.cpp:4953-4980`), so a row-driven port leaves it out unless the sim does the same.

## Decisions for the user

Merged from the three passes (A §8, B's list, C §7). The recommendations are the passes'.

1. **Port a post-0.75 world at all.** *Recommend yes*, with OM = WZO numbers, as Atlans and Tarkan were.
2. **Door levels.** MU asks 160 to go in (62) and 50/80 to come out (64). The flying item is the real bar,
   since our Dinorant asks 160 and the first wings 180. *Recommend 160 in*, matching the Dinorant and
   MU, *and free out*. A lower door only means something if the Dinorant's 160 moves too.
3. **The ground and the cloud road.** MU draws none: a navy clear and 335 emitters' puffs under the walkers. The options:
   - keep MU's (an undrawn world and the puffs);
   - add `void_clouds` decks beneath (ours);
   - draw a cloud-textured ground under the road (ours).

   *Recommend MU's puffs first*, then judge whether the void needs the decks.
4. **Object heights.** MU stands every object 3 m under the walkers, so the hero walks on cloud over
   ruins. Keep that, or raise the road's stones (Object07/14/15/16) to the plane (ours). *Recommend MU's
   first, judged in game.*
5. **Death and Town Portal home.** *Recommend Devias* (WZ, KR, and the user's WebZen picks of 2026-09-30).
   The Lost Tower hall is the alternative.
6. **Respawns.** *Recommend WZO's +1* (11 / 21 / 31 s, the Phoenix 151 s), or OM's 3 s if the road feels empty.
7. **The Phoenix's shield.** WZ toggles it every 6 s, changing only its look and spell, and row 76 never fights. The options:
   - show the toggle only (A);
   - make it hit everyone within 2 tiles while up (C, from WZ's dead `gObjSkylandBossSheildAttack`);
   - drop it.

   *A recommends show only*, and the dead code argues for A.
8. **The no-wingless rules.** WZ's three rules: the unequip lock, the broken-Dinorant send-off, and the summon ban. *Recommend all
   three*, the bag refusing and saying why. A fall is in no source.
9. **Second-class gear on map 10.** As built, the Phantom Knight, the Great Drakan and the Phoenix (25 of 64
   spots) reach only 3, 2 and 0 ordinary drop rows. Add map 10 to `secondGearHere`
   (`realm_items.cpp:1130-1137`), or leave the south to Zen and jewels. *Ask*; it's Tarkan's question 3 again.
10. **Loch's Feather.** MU from 0.98c drops it in Icarus only; ours drops it in Atlans and the Lost Tower.
    *Recommend adding map 10* and keeping the other two.
11. **A Tab row.** *Recommend one*: "Icarus" at 10,000 zen, landing 15,13, refused without flight.
12. **Music.** icarus.mp3 plays on the whole map in MU, but Icarus has no safe zone, which is where we play a world's theme. *Recommend once on
    arrival*, then the house rule (music is rare).
13. **Shadows.** MU draws none here. *Recommend ours* on the islands, off by sheet if they read wrong.
14. **Cloth** (the Crusts' capes, the Phoenix's mane). *Recommend skipping the sim*, with a swung card or a
    baked drape only if they read bare.
15. **The two swords.** *Recommend props now*, items later with the second-class gear list.
16. **The Golden Crust** (S4.5). *Recommend no.*

## The build, in order

Part C §6 in full. In short, each step has a headless check and is then shown:

0. (optional) Per-world `MapRow` fields: `air`, `openRoom`, `sky`, `fly`. These replace today's string tests, with no change in behaviour.
1. **Cook the ground and set the lighting.** Add `SKY_BY_MAP`, `HIDDEN_BY_MAP[10] = {0..5}` and `GRASS []`. Build a one-sheet ground that is never drawn, and give `Ground` a `sky` switch. The sheet gets a `clear_colour` that reaches the renderer. Add the `kMaps` row `{"icarus", 10, {15, 13}, false, home}`, the banner and minimap names, and `sheets/worlds/icarus.json` (B §6: a moonlit night above the clouds, navy not black).
2. Gates 62-65 and the flight rule, then recook the Lost Tower's tables.
3. The air: aHeaven, Dinorant flight on map 10, the steady rain.
4. Objects in small batches, after `FLAT_OVER_VOID` takes icarus.
5. The cloud banks (the 335 emitters) and the pillar motes.
6. Thunder, wisps, dragons.
7. Monsters in five breed batches: 64 spawns, kinds 69-77, then every world's tables.
8. The Tab row. 9. Drops and side hooks.

## Side findings (outside Icarus)

- **The Dinorant recipe.** KR's 2003 note and OM 0.95d ask 3 Horns of Uniria and 250,000 Zen. Ours follows WZ 1.00.93: 10 horns and 500,000 (`src/sim/machine.h:208-215`). It's the user's call.
- `renderer.cpp:979` hard-codes a black clear, and Devias's light-blue MU clear isn't carried either.
- `src/game/pets.h:43` and `src/sim/items.h:185-188` say "Icarus is not in this game". Both comments change with the port.
- `pipeline/index.py` `GATE_BOXES_BY_MAP` lacks maps 4 and 11. `OPERABLE_BY_MAP` is still kept in two copies (terrain.py, index.py).
- `tools/bot/bot.cpp:68-74` knows five worlds: no Atlans, Blood Castle or Tarkan.
- WZ's Golden Crust regen compiles to Lorencia (`EledoradoEvent.cpp:852-857`), and `gObjSkylandBossSheildAttack` has no caller.

---

## Part A: the record

Research pass A, 2026-10-06. Nothing in the repo was edited, cooked or run; no window was opened;
`source/mu.db` was read with `sqlite3 -readonly` only.

Paths are as in `docs/tarkan-port.md` Part A:

- **OM**: OpenMU init, `LEGACY/reference/openmu/src/Persistence/Initialization/`; **OMGL** its `GameLogic/`
  (`LEGACY/reference/openmu/src/GameLogic/`).
- **MM**: MuMain source, `LEGACY/reference/MuMain/src/source/`; **D** its `src/bin/Data/`.
- **WZ**: WebZen GameServer 1.00.93 C++, github `ptr0x-real/Mu-GS-Webzen-MC-10093`. The clone at
  `.../9142efc4-.../scratchpad/wz/` holds only `user.cpp`, `protocol.cpp`, `gObjMonster.cpp`, `define2.h`
  and `user.h` of `Source/Server Side/GameServer/` now. The files it lacks (`MapClass.cpp`,
  `MoveCommand.cpp`, `ObjAttack.cpp`, `EledoradoEvent.cpp`, `MonsterItemMng.cpp` and five more) were
  fetched raw from the same repository into `a/wzsrc/` (its `gObjMonster.cpp` is byte-identical to
  the clone's). CP949; grep with `LC_ALL=C grep -a`.
- **WZD**: the same repository's `_Server Files/Data` (the 2012 repack): `Monster.txt` and
  `MonsterSetBase.txt` from the clone, `Gate.txt`, `MoveLevel.txt`, `movereq(Kor).txt` and
  `commonserver.cfg` fetched into `a/wzd/`. Trusted for nothing.
- **WZO**: WebZen's official 0.99.60T data at the Atlans pass's `.../6a47dc36-.../scratchpad/atlans/a/w996/Files/Data/`
  (present): `Monster.txt` 1.00m 2005-03-07, `MonsterSetBase.txt` 0.99 2005-01-20, `gate.txt` 0.99
  2004-08-16, `lang/Kor/movereq(kor).txt` 0.98n 2004-03-29, `MoveLevel.txt` 0.99 2004-07-23,
  `lang/Kor/item(Kor).txt` 1.00m, `Terrain11.att`, `commonserver.cfg`.
- **KR**: WebZen Korea's own notices, via the Wayback Machine, saved as text in `a/`: the Icarus
  patch note of 2003-07-12 (`ref39.txt`, muonline.co.kr news guid 1291), the 0715 update preview
  page (`ref62.txt`, `community/preview/update_0715.htm`), the 2003-04-24 and 2003-04-30
  announcements (`ref19.txt`, `ref20.txt`, guids 902 and 910), the 0.99 patch note of 2004-05-10
  (`ref22.txt`, guid 1632), and WebZen's press release of 2003-07-01 (`press448.txt`, seq 2147, from the
  Tarkan pass). **MuHistory**: github `AlighieriDemiurgs/MuHistory` README (`a/muhistory.md`, the
  Tarkan pass's copy).

Scratch (`scratchpad/icarus/a/`): `grids.py` (the Tarkan pass's decoder, pointed at World11),
`lab.py` (the 8-neighbour flood fill and walk), `regions.py`, `sets.py` / `cmp.py` (OM, WZO, WZD
map-10 spawn rows), `more.py` (spawns, gates, walks), `mapascii.py` (`map_out.txt`), `breeds.py`
(OM against WZO, breed by breed), `objs.py` (the placed objects), `ltdoor.py` (the Lost Tower side
of the door, on our cooked grid), `drops.py` (our drop pools), `press.py`; outputs `more_out.txt`,
`map_out.txt`, `om_rows.txt`, `wzo_map10.txt`, `wzd_map10.txt`, `drops_out.txt`.

Compass as in tarkan-port: x grows east, y grows south. In `a/map_out.txt` the top row is y 12.

### 0. The one thing to know first

**Icarus is a sky map, and the sky is the rule.** No character stands in it without a flying thing
equipped: a wing, or a Horn of Dinorant (KR `ref39.txt:21`; WZ `MoveCommand.cpp:319-343`,
`user.cpp:26299-26339`, `user.cpp:27202-27242`; OM `Version095d/Maps/Icarus.cs:47-50`, `Stats.CanFly`).
WZ and KR go further than the door. In Icarus the last flying piece cannot be taken off or dropped
(WZ `user.cpp:16880-16905`, `protocol.cpp:5357-5375`; MM `UI/NewUI/Inventory/NewUIMyInventory.cpp:1491-1503`;
KR `ref62.txt:56-60`). A Dinorant that wears to 0 with no wings and no spare horn in the bag sends him
to Devias (WZ `user.cpp:10813-10842`; KR `ref39.txt:22-23`). A Horn of Uniria cannot be worn there,
and a Uniria rider cannot enter (WZ `MoveCommand.cpp:338`; MM `NewUIMyInventory.cpp:351-354`). The
elf cannot summon there, and her summon dies at the door (WZ `user.cpp:29568-29572`,
`MoveCommand.cpp:337-342`; MM `GameLogic/Combat/ClassAttack.cpp:117-126`; KR `ref62.txt:71-74`).
**A death sends him to Devias** (WZ `user.cpp:22077-22081`; KR `ref39.txt:24`). There is no
fall: nothing in WZ, OM or MM drops a wingless hero out of the sky. The rules stop him getting
wingless instead.

**Icarus is not 0.75. It is the world after Tarkan**: KR's eighth world, on the Korean live servers
on 2003-07-15 (`ref39.txt:7-11`), half a year after Tarkan's 0.84 and with the first class change.
It is in 0.95d (OM `Version095d/GameMapsInitializer.cs:39`), and not in 0.75
(`Version075/GameMapsInitializer.cs:30-37`). Evidence in §1.1; it is decision #1.

**One region, one long road, no town.** 7 026 open tiles (10.7% of the map) in one S-shaped strip
down the west third of the map. The way in is at the north-west corner, from the south end of Lost
Tower 7 ("Lost Tower 8F" in WZ and KR), at level 160. The way back is the row above it. There is not
one safe tile. 64 single-tile monsters of eight breeds, levels 75-108, climb with the walk. The **Dark
Phoenix** (108) is the boss, alone at the far south end, 289 steps from the door.

**The sources agree on the map, the spawns and the stats; they disagree on the respawns and on
where a death goes.** OM 095d's 64 spawns are WZO's but for one tile. OM's stats are WZO's 1.00m
numbers to the digit. OM's respawns for five breeds are not WZO's (3 s against 10-30 s; the
Phoenix 15 s against 150 s). OM 095d defines Icarus with no gate and no warp row, as it did Tarkan,
and sends its dead to the Lost Tower. WZ and KR send them to Devias.

**The launch numbers were harder** (KR `ref62.txt:133-201`, §4.2), and WebZen cut Icarus's
hit points again in 0.99 (`ref22.txt:20-25`). OM and WZO carry the 2005 numbers.

### 1. Map

#### 1.1 Is it 0.75? (decision #1: the evidence)

**OpenMU.**
- **Version075** has eight maps and no Icarus: `OM/Version075/GameMapsInitializer.cs:30-37`
  (Lorencia, Dungeon, Devias, Noria, Lost Tower, Exile, Arena, Atlans). No Icarus file exists under
  `OM/Version075/Maps/`. Its wings already carry `Stats.CanFly` (`OM/Version075/Items/Wings.cs:115-117`),
  an attribute with nowhere to use it there.
- **Version095d** adds it: `OM/Version095d/GameMapsInitializer.cs:39` `typeof(Icarus)`, and
  `OM/Version095d/Maps/Icarus.cs:20` `Number = 10`, `:25` `"Icarus"`. The class carries the
  requirement (`:47-50`), 64 spawns (`:55-118`), nine breeds (`:123-414`) and its safe zone
  (`:44`, the Lost Tower). No NPC, no drop group, no terrain prefix (so `Resources/Terrain11.att`,
  the Season 6 file).
- **Version095d's gates have no Icarus** (`OM/Version095d/Gates.cs`, no `maps[10]` row and no gate
  62-65; `:41` "todo: update for 0.95d"). In OM 095d Icarus cannot be reached.
- **SeasonSix** subclasses the 095d class (`OM/VersionSeasonSix/Maps/Icarus.cs:12`) and adds two
  drop groups (`:25-48`, §5); its gates and warp are in `OM/VersionSeasonSix/Gates.cs` (§2).

**WebZen's own record (KR).**
- **2003-04-24** (`ref19.txt:3-14`): "천공의 맵 (가칭)", the sky map (working name), announced for
  June 7, with "a large update".
- **2003-04-30** (`ref20.txt:30`): the MU Level Up 2003 ("Fly to the Sky") event; "레벨 180이상의
  날개를 가진 캐릭터만 로스트타워 8층을 통해 입장" -- only level 180+ characters with wings, through
  the Lost Tower's 8th floor. The Box of Heaven and the Rena event (`:17-24`) were its lead-in.
- **2003-07-01 press release** (`press448.txt`): the eighth world "천공로-이카루스", the character
  change-up system and the Jewel of Creation; "이카루스에 입장한 모든 캐릭터들이 날개 혹은 날아다니는
  이동수단을 사용" (every character in Icarus uses wings or a flying mount); Satan, Heaven, Elf wings or
  the new wings coming in July, or the Dinorant "for those without wings"; "160이상의 레벨자들은
  로스트타워 8층을 통해서 걸어서 입장" (160+ walk in through the Lost Tower's 8th floor); Tarkan cut
  from 140 to 130, Atlans /move from 100 to 70; test server first, live in July.
- **2003-07-12 patch note** (`ref39.txt:7-25`): "천공로 이카루스가 전체서버에 패치됩니다", live on the
  2003-07-15 maintenance. Entry level 160; /move 170 for 15 000 Zen; the gate at "로스트타워 8층 좌표
  [18, 250]"; wings or a Horn of Dinorant, no Uniria; Dinorant at 0 life or death -> Devias town; no
  elf summon. The same patch: the first class change (Sevina in Devias, `:26-49`), the second
  class's gear (`:54-65`), the Dinorant (`:68-84`), and the game-server split (`:12, 111-115`).
- **MuHistory** (`a/muhistory.md:344-357`): "0.94b aka 0.94.2 (06.06.2003): Added map Icarus. Entry
  level requirement of 160", with the Jewel of Creation and the Horn of Dinorant. The Korean live
  date is KR's, 2003-07-15. Later: 0.98c 2004-02-05, the Loch's Feather drops only in Icarus
  (`:464`); 0.99 2004-05-11, "Monster HP adjusted in map Icarus" (`:492`; `ref22.txt:20-25`).
  Global MU ran 0.95d from December 2003 (`:456`), which is why OM's 095d has Icarus.

**WZ's own flags.** `define2.h:2817-2825` groups the first sky phase ("천공 1차"):
`MONSTER_SKILL`, `NEW_SKILL_FORSKYLAND` (every Icarus rule above sits under it), `GAMESERVER_DIVISION`
(the server split of KR's same patch). `:2827-2844` the second ("천공 2차"): `NEW_FORSKYLAND2`,
`EXP_CAL_CHANGE`, `NEW_FORSKYLAND3`. `MAP_INDEX_ICARUS = 10` (`:4047`). blood-castle-port.md §1 reads
WZ's `MAX_MAP` ladder the same way: 10, then 11 with Icarus, then 17 with Blood Castle.

**The cross-check versions.** 0.75: absent. 0.95d (Global): present in OM, without its gates.
0.97d: present in WZ and WZO. Season 3 / Season 6: present (OM S6, MuMain `WD_10HEAVEN`,
`MM/World/MapInfra/MapManager.h:19`; data `D/World11` + `D/Object11`).

**What this game already carries from Icarus's own patch**: the class change and its quest items
(`docs/class-change-quest.md`), the second class's gear (`docs/second-class-gear.md`), the Horn of
Dinorant (`source/items/pets/Helper04.json`), the 2nd wings and the Loch's Feather
(`docs/second-wings.md`, `src/sim/items.h:185-192`), the Rune of Creation (`source/items/misc/Jewel22.json`,
MU's Jewel of Creation repurposed) and the Golden Archer of the Box of Heaven event
(`docs/golden-archer.md`). Icarus is the map they were all made for.

#### 1.2 Map facts

- WZ: `MAP_INDEX_ICARUS = 10` (`define2.h:4047`). **No regen box** (`a/wzsrc/MapClass.cpp:192-212`
  set rects for 0-8, Aida, Crywolf and Elbeland only). MuMain `WD_10HEAVEN`, data `D/World11`, `D/Object11`
  (world index + 1); map name `I18N::Game::Lookup(55)` (`MM/World/MapInfra/MapManager.cpp:1742-1745`).
- **Exp multiplier 1**; one map requirement, `Stats.CanFly >= 1` (`OM/Version095d/Maps/Icarus.cs:49`).
  `CanFly` comes from every wing (`OM/Version095d/Items/Wings.cs:114-117`) and from the Dinorant
  (`OM/CharacterClasses/CharacterClassInitialization.cs:158`, `CanFly = IsDinorantEquipped`); in S6 also
  Fenrir (`OM/VersionSeasonSix/Items/Pets.cs:82`). Its description: "You can enter Icarus only with
  wings, dinorant, fenrir." (`OMGL/Attributes/Stats.cs:1405`).
- **NPCs: none** in OM 095d, OM S6, WZO (its map-10 rows are the 64 monsters,
  `WZO MonsterSetBase.txt:784, 790-852`) or WZD.
- **No safe zone.** OM 095d: `SafezoneMapNumber => LostTower` (`Icarus.cs:44`). Every grid has 0
  safe tiles in the field (§1.3).
- 670 placed objects of 15 kinds (`D/World11/EncTerrain11.obj`, types 0-15 less 11; `a/objs.py`),
  all at x 4-104, the strip's side of the map. Kinds 0-5 sit 135-460 units up, 6-15 at 0 to -285
  units down. `D/Object11` holds `Object01-11.bmd`, `Object13-16.bmd` (no Object12, matching the
  unplaced type 11) and `cloud.bmd`. Part B's.

#### 1.3 One grid, one region

Five files decoded (`a/grids.py`, `a/regions.py`). Walkable = `(flags & (NoMove|NoGround)) == 0`;
OM's plain file by OMGL's rule, byte 0 or 1 (`OMGL/GameMapTerrain.cs:139-140`).

| grid | walkable | regions | box of the open tiles | safe |
|---|---:|---:|---|---:|
| client `D/World11/EncTerrain11.att` (2-byte) | **7 026 (10.7%)** | **1** | x 9-97, y 12-241 | 0 |
| WZO `Terrain11.att` (2005) = WZD = `D/World11/Terrain11.att` (3-byte XOR) | 48 134 (73.4%) | 3 (39 765 / 7 025 / 1 344) | x 1-250, y 1-250 | 0 |
| OM `Resources/Terrain11.att` (S6, plain) | 7 334 | 35 (7 026 + edge junk) | | 6 (junk, rows 254-255) |

- **The client grid has no NoGround bit at all**: 7 025 tiles 0, one tile 2 (41,89), 58 510 tiles
  4 (NoMove). The void is NoMove, not NoGround (Tarkan's void is NoGround, tarkan-port §3).
- **WZO's 7 025-tile region is the client's strip less one tile** (26,34, NoMove in WZO). Its other
  two regions are open sky the client refuses: 39 765 tiles at x 69-250, y 1-250 (the whole east
  half), and 1 344 at x 1-16, y 83-216. Neither touches the strip. A server walker could never
  reach them; a server *spawner* could (§4.1, WZD's Balrogs; §5.1, the Golden Crust).
- **OM S6 = the client grid** in the field (0 tiles differ there). Its 308 extra open tiles are edge
  strips (rows 249-250 at x 69-100 and 136-141; x 248-250 at y 157-164) and garbage in rows 254-255
  (255 tiles with values like 18, 124, 255), as Tarkan's OM file had.
- **MuMain draws no terrain in Icarus** (`MM/Scenes/MainScene.cpp:463`
  `if (WorldActive != WD_10HEAVEN ...) RenderTerrain`). The tiles are walked; what is seen is the
  placed objects and the clouds over a blue clear colour (`MM/Scenes/SceneManager.cpp:385-386`,
  `rgb8(3, 25, 44)`). Part B's.

**Settled: the client grid**, as every earlier world. It is WZO's walkable strip to the tile but one,
and OM S6's to the tile. Every spawn and gate box below is open on it.

The shape (`a/map_out.txt`), as a walk:

- **The landing**, x 10-22, y 12-22: the door from the Lost Tower at the top (14-16,12-13).
- **The north road**, x 10-98, y 23-46: east along the top. Alquamos, then Mega Crusts.
- **The east turn**, x 87-98, y 47-71: down the east edge. Mega Crusts, a Queen Rainer.
- **The middle band**, x 25-97, y 62-87: back west, in two lobes. Queen Rainers, Drakans.
- **The south lane**, x 27-60, y 88-241, 17-25 tiles wide: south the length of the map. Alpha
  Crusts, then Phantom Knights, then Great Drakans.
- **The south end**, x 25-55, y 215-241: Great Drakans and Phantom Knights round the **Dark Phoenix**
  at 34,238.

The farthest open tile is 292 steps from the door (41,241). Nothing is open east of x 98.

### 2. Gates and warps

Directions are OpenMU's (`OMGL/DirectionExtensions.cs:21-35`).

#### 2.1 The door from the Lost Tower

| gate | flag | map | box | to | dir | level | WZO gate.txt | OM S6 Gates.cs | WZD Gate.txt |
|---:|---|---|---|---|---|---:|---|---|---|
| **62** | enter | Lost Tower | 17,250 - 19,250 | 63 | -- | **160** | l.116 | `:526` | l.109, 160 |
| **63** | exit | Icarus | 14,13 - 16,13 | -- | 5 East | -- | l.117 "로스트타워 8층 -> 이카루스" | `:213` | l.110 |
| **64** | enter | Icarus | 14,12 - 16,12 | 65 | -- | **80** (OM S6 **50**) | l.119 | `:527` | l.112, 80 |
| **65** | exit | Lost Tower | 17,249 - 19,249 | -- | 1 West | -- | l.120 "이카루스 -> 로스트타워 8층" | `:181` | l.113 |

- **Not in OM 095d** (above). The way in asks 160 in every source that has it, and KR (`ref39.txt:17`);
  WZ gives the MG and DL two thirds (`WZ user.cpp:27545-27567`, tarkan-port §2.1). The way out asks
  80 (WZO, WZD) or 50 (OM S6). KR's gate is "Lost Tower 8F [18,250]" (`ref39.txt:20`), the middle of
  gate 62.
- **Every source also asks for the flying item at this door** (§0). The level is the lesser bar:
  our Dinorant asks 160 (`source/items/pets/Helper04.json`, the user's of 2026-10-02) and our wings
  180 (`docs/wings.md:41-46`; WZO `item(Kor).txt:394-396` 180, 2nd wings 215 `:397-400`). At 160-179
  only the Dinorant gets him in.
- **Open tiles**: 63 3 of 3 and 64 3 of 3 on all five grids (`a/more_out.txt`).
- **The way back is the row above the arrival.** Exit 63 (row 13) sets him down one step south of
  enter 64 (row 12), facing East (+1,+1). A step north takes him straight back out.
- **The Lost Tower side.** On our cooked `source/world/losttower/attributes.png` gate 62 (17-19,250)
  and exit 65 (17-19,249) are open and flag 0; the posts at x 15 and x 21 that lost-tower-port §3
  found are there, blocked, framing the door (`a/ltdoor.py`). **Walk from LT7's arrival (8,86) to the
  door: 225 steps**, past the Balrogs (lost-tower-port §1.2). From Devias that is about 950 + 40 steps
  and six stairs.
- The client draws the door as a Lost Tower 7 change: lost-tower-port §3 lists "LT7 x 15 and x 21,
  y 248-250, the posts of S6's Icarus door" as the only LT7 difference between the 075 and client grids.

#### 2.2 Warp list

- 0.75 and 0.95d: no Icarus row (`OM/Version095d/Gates.cs`).
- **WZO `lang/Kor/movereq(kor).txt:33`** (0.98n): **Icarus 10 000 Zen, level 170 -> gate 63.** OM S6 the
  same (`OM/VersionSeasonSix/Gates.cs:69`); WZD likewise (`a/wzd/movereq(Kor).txt:32`).
- KR: /move 170 for **15 000** Zen at launch (`ref39.txt:18`).
- WZO `MoveLevel.txt:15`: map 10 at **160** (WZD `MoveLevel.txt:18`: 170).
- MU2_BGFX's M window lists Icarus (10) among the dimmed "coming" worlds (`src/game/ui/travel.cpp:238`).
  `src/sim/realm_travel.cpp:18-45` holds the rows; Tarkan's is the last.

### 3. Terrain attributes

| file | 0 | 2 | 4 NoMove | other | walkable |
|---|---:|---:|---:|---|---:|
| client `EncTerrain11.att` | 7 025 | 1 (41,89) | 58 510 | -- | 7 026 |
| WZO / WZD / `D/World11/Terrain11.att` | 48 133 | 1 (64,69) | 17 402 | -- | 48 134 |
| OM `Terrain11.att` (S6) | 7 328 | 4 | 57 941 | 1 x6, junk 255 tiles in rows 254-255 | 7 334 |

- **89.3% of the map is NoMove on the client grid**, 10.7% walkable, nothing safe.
- No NoGround (8) anywhere, in any file. No other flag.
- The one "2" (character standing) tile sits inside the strip in both files, at different tiles;
  neither is a spawn snapshot.

### 4. Monsters

#### 4.1 Spawns

`OM/Version095d/Maps/Icarus.cs:55-118`: **64** single-tile `CreateMonsterSpawn(id, def, x, y)`, ids
100-163, no direction, no doubled tile (`a/om_rows.txt`). **WZO has the same 64** (type 2, distance 30,
dir -1) in two blocks: the Dark Phoenix alone at `MonsterSetBase.txt:784`, under
"//이카루스_03년 7월 11일_어둠의불사조" (`:783`), then 63 rows at `:790-852`, under "//이카루스_03년 9월
10일_이카루스 몬스터" and "9월 10일 일부 메가 크러스트 삭제(좌표 잘못 기입)" (some Mega Crusts deleted,
coordinates entered wrong) (`:787-788`). OM's order is WZO's with the Phoenix moved to the end
(`a/cmp.py`).

**One tile differs**: a Great Drakan at **30,231** in OM (`Icarus.cs:109`, id 154) and **25,231** in
WZO (`:844`) and WZD. 25,231 is NoMove on every grid; 30,231 is open on every grid. WZ's type-2 rows
draw a tile within ±3 of the row (`WZ MonsterSetBase.cpp:133-152`, tarkan-port §4.1), so WebZen's
row still spawned; OM moved it onto the floor. Every other spot is open on all five grids.

| # | breed | spots | where (client grid) | walk from the door (min / median / max) | OM lines |
|---:|---|---:|---|---|---|
| 69 | Alquamos | **11** | the north road, x 12-80, y 27-41 | 13 / 24 / 64 | `:87-113` |
| 71 | Mega Crust | **10** | the north road and the east turn, x 53-93, y 27-70 | 37 / 55 / 88 | `:72-115` |
| 70 | Queen Rainer | **6** | the east turn and the middle band, x 60-91, y 38-88 | 73 / 102 / 108 | `:55-85` |
| 73 | Drakan | **6** | the middle band, x 26-70, y 69-79 | 98 / 132 / 142 | `:58-74` |
| 74 | Alpha Crust | **6** | the top of the south lane, x 29-39, y 85-111 | 136 / 154 / 162 | `:63-77` |
| 72 | Phantom Knight | **12** | the south lane, x 33-53, y 113-226 | 164 / 199 / 277 | `:61-116` |
| 75 | Great Drakan | **12** | the south lane's foot and the south end, x 27-59, y 162-238 | 213 / 251 / 289 | `:96-117` |
| 77 | **Dark Phoenix** | **1** | **34,238**, the south end (4 Great Drakans, 2 Phantom Knights within 20) | 289 | `:118` |
| | **total** | **64** | | | |

By walk from the door: 0-60 steps 10 Alquamos and 6 Mega Crusts; 60-120 Queen Rainers, 4 Mega
Crusts, 2 Drakans; 120-180 Drakans, Alpha Crusts, 4 Phantom Knights; 180-240 Phantom Knights and 3
Great Drakans; 240-300 9 Great Drakans, 3 Phantom Knights and the Phoenix. **The level climbs with the
walk, 75 at the door to 108 at the far end, with no gap**: Alquamos 75, Mega Crust 78, Queen Rainer
82, Drakan 86, Alpha Crust 92, Phantom Knight 96, Great Drakan 100, Phoenix 108.

**The Dark Phoenix Shield (76) has no spawn row in any source.** WZ makes it when it makes the
Phoenix (§4.3).

**Flags:**
- 64 monsters on 7 026 tiles is thin: one to 110 tiles (Tarkan 217 on 23 832, one to 110; Atlans 336).
  The respawns (§4.2) are what keep it busy.
- WZD adds ten "spot" rows (`WZD MonsterSetBase.txt:136-145`, "Mobs Icarus (SPOT)"): 58 more
  monsters, among them **12 Metal Balrogs (67) at 250,10 and 250,23**, in WZO's unreachable east
  (open on the server grid, NoMove on the client's). WZD also **has no Dark Phoenix**: its row
  `:1016` puts a Great Drakan on the Phoenix's tile. The repack's.
- The table is WZO's of 2003-09-10 (the Phoenix's of 2003-07-11), after the launch. The launch table
  is not on disk.

#### 4.2 Breeds (`OM/Version095d/Maps/Icarus.cs:123-414`; WZO `Monster.txt:56-66, 75-76`)

Columns as in tarkan-port §4.2. Resistances OM `n/255` as n (P I W F). Every breed: `Attribute = 2`,
`NumberOfMaximumItemDrops = 1`, move 400 ms. Respawn as OM / WZO.

| # | name | lvl | HP | dmg | def | atk / def rate | move / atk / view range | atk delay | respawn OM / WZO | skill (OM) / A.Type (WZO) | P I W F | OM line |
|---:|---|---:|---:|---|---:|---|---|---:|---|---|---|---|
| 69 | Alquamos | 75 | 11 500 | 255-290 | 195 | 385 / 125 | 3 / 5 / 5 | 1400 | 10 / 10 s | Energy Ball / 17 | 9 9 11 9 | `:127` |
| 71 | Mega Crust | 78 | 15 000 | 270-320 | 210 | 430 / 140 | 3 / 1 / 5 | 1400 | **3 / 10 s** | -- / 0 | 9 9 12 9 | `:193` |
| 70 | Queen Rainer ("Queen Rainer") | 82 | 19 000 | 305-350 | 230 | 475 / 160 | 3 / 3 / 5 | 1400 | 10 / 10 s | Energy Ball / 17 | 9 11 12 9 | `:160` |
| 73 | Drakan | 86 | 29 000 | 425-480 | 305 | 570 / 210 | 3 / 5 / 5 | 1400 | 20 / 20 s | MonsterSkill / **150** | 12 12 13 12 | `:258` |
| 74 | Alpha Crust | 92 | 34 500 | 489-540 | 360 | 620 / 240 | 3 / 1 / 5 | 1400 | **3 / 10 s** | -- / 0 | 13 13 14 13 | `:291` |
| 72 | Phantom Knight | 96 | 41 000 | 560-610 | 425 | 690 / 270 | 3 / 2 / 5 | 1400 | **3 / 30 s** | MonsterSkill / **150** | 14 14 16 14 | `:225` |
| 75 | Great Drakan ("자이언트드라칸", Giant Drakan) | 100 | 50 000 | 650-700 | 495 | 800 / 305 | 3 / 5 / 5 | 1400 | **3 / 30 s** | MonsterSkill / **150** | 15 15 17 18 | `:323` |
| 76 | Dark Phoenix Shield ("방어막") | 106 | 73 000 | 850-960 | 580 | 840 / 305 | 3 / 3 / 5 | 1400 | 10 / 10 s | Lightning / 3 | 30 30 35 30 | `:356` |
| 77 | **Dark Phoenix** ("어둠의불사조") | 108 | 95 000 | 950-1000 | 600 | 900 / 350 | **1** / **6** / 7 | **1500** | **15 / 150 s** | MonsterSkill / **150** | 30 30 35 30 | `:389` |

**WZO's official 1.00m `Monster.txt` agrees with every number above but the respawn** (`a/breeds.py`):
OM gives the Mega Crust, Alpha Crust, Phantom Knight and Great Drakan **3 s** where WZO's RegTime is
10, 10, 30, 30, and the Phoenix **15 s** where WZO's is **150**. Tarkan's OM respawns were WZO's to
the digit (tarkan-port §4.2); Icarus's are not. WZO adds:

| | Alquamos | Mega Crust | Queen Rainer | Drakan | Alpha Crust | Phantom Knight | Great Drakan | Shield | Dark Phoenix |
|---|---|---|---|---|---|---|---|---|---|
| ItemRate / MoneyRate / MaxItemLevel | 160 / 14 / 3 | 160 / 14 / 3 | 160 / 14 / 3 | 160 / 14 / 3 | 160 / 14 / 3 | 150 / 14 / 3 | 140 / 14 / 3 | 190 / 14 / 3 | **120** / 14 / **4** |

- WZO files 69-75 in the main list (`Monster.txt:56-66`) and the Shield and the Phoenix under
  **"//보스"** (`:68, 75-76`), beside the Zaikan and the Death Beam Knight.
- **MoneyRate 14**, as Atlans and the Lost Tower (Tarkan's is 30, tarkan-port §4.2).
- **A.Type 150** (WZ): one blow in five is the area spell, Flame of Evil, on every player within 5
  tiles (`WZ gObjMonster.cpp:1739-1748, 1919-1924`; tarkan-port §4.2). The Drakan, Phantom Knight,
  Great Drakan and Phoenix carry it: 31 of the 64. OM 095d never creates `MonsterSkill`
  (`OM/Version095d/SkillsInitializer.cs` has none), so in OM those four only melee. **A.Type 17** is
  Energy Ball (`SkillsInitializer.cs:60`), Alquamos at range 5 and the Queen at 3. **A.Type 3** is
  Lightning (`:43`).
- **The Phoenix's area blow has no reach limit in WZ.** For class 77 `gObjMonsterMagicAttack` hits
  every character in its viewport (`WZ gObjMonster.cpp:1910-1916`); for every other class only those
  within 6 tiles (`:1919-1924`). The Drakan and the Phoenix (with the Cursed King, 66) also send their
  ordinary blow as a magic attack, number 1 (`:2365-2380`); what number 1 resolves to for a monster
  is not traced.

**The Phoenix's shield (WZ).** When WZ sets up a class 77 it makes a class 76 on map 10 and links it
(`WZ user.cpp:4953-4975`, "천공맵 보스몹은 벙어막을 가진다", the sky map's boss has a shield). Every 6
seconds (`gObjSkillUseProc`, `user.cpp:25410-25430`, called once a second from `gObjSecondProc`
`:22608`) the Phoenix toggles: shield up, it shows `STATE_REDUCE_ATTACKDAMAGE` and the Soul Barrier
effect (`AT_SKILL_MAGICDEFENSE`); shield down, both are cleared. While the shield is up its Energy
Ball becomes Lightning (`a/wzsrc/ObjAttack.cpp:237-243`). The 76's life is refilled when the Phoenix
respawns (`user.cpp:21918-21928`). `gObjSkylandBossSheildAttack` (`gObjMonster.cpp:3153-3200`), the
shield striking those within 2 tiles, is **never called**. So in WZ the shield is a show and a
spell swap; the 73 000-HP row 76 is never fought. KR's lore agrees it is the rider's: "나이트 퀸은
전자파를 이용해 방어막을 형성" (the Night Queen who rides it raises a shield of electromagnetic waves)
(`ref62.txt:127-132`).

**Launch numbers, KR 0715 preview** (`ref62.txt:133-201`, the table WebZen published for the
2003-07 launch):

| | lvl | HP | dmg | def | def rate | vs OM/WZO |
|---|---:|---:|---|---:|---:|---|
| Alquamos | **78** | 16 000 | 310-370 | 240 | 175 | +3 levels, +39% HP |
| Mega Crust | **81** | 23 000 | 350-370 | 270 | 185 | +3, +53% |
| Queen Rainer | **85** | 30 000 | 460-540 | 350 | 210 | +3, +58% |
| Drakan | **88** | 36 000 | 520-600 | 390 | 255 | +2, +24% |
| Alpha Crust | 92 | 39 000 | 580-680 | 430 | 265 | +13% |
| Phantom Knight | 96 | 50 000 | 650-760 | 480 | 275 | +22% |
| Giant Drakan | 100 | 58 000 | 730-840 | 520 | 290 | +16% |
| Dark Phoenix | 108 | **150 000** | 1 100-1 200 | 800 | -- | +58% |

KR 0.99 (`ref22.txt:20-25`, 2004-05-11) cut Drakan 31 000 -> 30 000, Alpha Crust 37 000 -> 35 500,
Phantom Knight 46 500 -> 43 500, Giant Drakan 57 000 -> 53 000, Phoenix 150 000 -> 135 000. WZO
(2005-03) is lower again. Three steps down in two years.

**Experience per kill** (`src/sim/rules.cpp:345-361`; times `kExperienceRate` 100, `rules.h:355`):
Alquamos 3 383 (338 000), Mega Crust 3 689, Queen Rainer 4 117, Drakan 4 569, Alpha Crust 5 290,
Phantom Knight 5 800, Great Drakan 6 333, Shield 7 177, Dark Phoenix 7 470 (747 000). Per hit point
the Alquamos pays best (0.29), the Phoenix worst (0.08). A killer more than ten levels over pays
`(L+10)/killer` of it (`:349-351`): at 160 an Alquamos pays 0.53 of its row.

**Bosses.** WZO names one, with its shield. MuMain draws the Phoenix and the Phantom Knight with
boss blows (§4.3). No boss flag in OM.

#### 4.3 What MuMain draws (`MM/Engine/Object/ZzzCharacter.cpp`; body `D/Monster/Monster{MODEL+1}.bmd`; models `MM/Core/Globals/_enum.h:4200-4206, 4225`, monsters `:4444-4452`)

| # | body | scale | arms | notes | lines |
|---:|---|---:|---|---|---|
| 69 Alquamos | **Monster51** (MODEL_ALQUAMOS) | 1.0 | -- | nine star sprites on its bones and sparks; four `BITMAP_FLARE` joints on its blow | `:13573-13578`, `:8792-8813`, `:2140-2150` |
| 71 Mega Crust | **Monster53** (MODEL_CRUST) | 1.1 | **Thunder Blade +5, Legendary Shield +0** | BlendMesh 1; a cloth cape (`iui02.tga`); Inferno on its blow | `:13588-13611`, `:8820-8838`, `:1696-1708` |
| 70 Queen Rainer | **Monster52** | 1.3 | -- | BlendMesh -2, no shadow; a light on bone 20; 20 `BITMAP_BLIZZARD` on its target | `:13579-13586`, `:8815-8818`, `:1681-1695` |
| 73 Drakan | **Monster55** (MODEL_DRAKAN) | 0.8 | -- | blue lights and thunder joints along its bones; chrome passes; Inferno and five falling `MODEL_PIERCING+1` on its blow | `:13621-13638`, `:8840-8888`, `:1734-1783`, `:2123-2138` |
| 74 Alpha Crust | Monster53 | 1.3 | **Thunder Blade +9, Legendary Shield +9** | the cape on `iui03.tga`; Inferno on its blow | `:13588-13611`, `:8820-8838` |
| 72 Phantom Knight | **Monster54** | 1.45 | **Dark Breaker +5** | boss blow: 36 `BITMAP_JOINT_SPIRIT` | `:13613-13619`, `:1710-1732` |
| 75 Great Drakan | Monster55 (not `MONSTER_MODEL_GREAT_DRAKAN`, whose Monster76.bmd is not shipped) | 1.0 | -- | a fire on bone 18; `RENDER_EXTRA` | `:13621-13638`, `:8867-8880` |
| 77 Dark Phoenix | **Monster56** (MODEL_DARK_PHEONIX_SHIELD, the rider) **+ Monster57** (the bird, drawn as `Type+1`) | 1.0 | -- | chrome pulse; a red hair cloth (`BITMAP_PHO_R_HAIR`); thunder from the bird; boss blow 40 spirit joints twice | `:13640-13648`, `:8890-8925`, `:1785-1800`, `:2108-2121`, `:2283-2293` |

- **The client draws 77 as one figure of two bodies**, the shield model and the phoenix; it has no
  case for a 76 of its own. The rider is the "shield" (KR's Night Queen).
- `Monster51-57.bmd` are in `D/Monster/`. Sounds (`MM/Engine/Object/ZzzOpenData.cpp:3738-3793`):
  `mMegaCrust1`, `mMegaCrustAttack1`, `mMegaCrustDie`; `mAlquamosAttack1`, `mAlquamosDie`;
  `mRainner1`, `mRainnerAttack1`, `mRainnerDie`; `mPhantom1`, `mPhantomAttack1`, `mPhantomDie`;
  `mDrakan1`, `mDrakanAttack1`, `mDrakanDie`; `mPhoenix1`, `mPhoenixAttack1` (no die sound). All in
  `D/Sound/`. The map: `aHeaven.wav` looped, `aThunder01-03.wav` loaded but never played (lost-tower-port
  §B), `D/Music/icarus.mp3`.
- **The held weapons need two swords not built**: Dark Breaker (`MODEL_SWORD+17`, `Sword18.bmd`) and
  Thunder Blade (`MODEL_SWORD+18`, `Sword19.bmd`) (`_enum.h:1512-1513`). The Legendary Shield
  (`MODEL_SHIELD+14`, `:1637`) is our `Shield15`.
- **Golden Crust (496)** is the Mega Crust's body, scale and arms (`:14895-14904`). Building the
  Crust builds it.

#### 4.4 What MU2_BGFX has

`sqlite3 -readonly source/mu.db`: `monster_kinds` holds 0-41, 45-52, 57-63 (Tarkan, since
2026-10-06), 84-130, 132-134, 150; **none of 69-77, no 496**. `monster_spawns` maps 0, 1, 2, 3, 4, 7, 8,
11. `gates` rows 17, 22, 27, 42, 49, 57; no Icarus. The highest monster level in the table is 99.
`source/monsters/` has no Icarus body. `attack_skill` 17 (Energy Ball) is carried by the Vepar and the
Beam Knight; 50 (Flame of Evil) by `kBosses` (`src/sim/realm_tuning.h:344` `{35, 38, 49, 58, 59, 63}`).

Rows to add, in the Tarkan form (WZO's respawns):

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (69,'Alquamos',      75,11500,255, 290,195,3,5,5,400,1400,385,125, 10,17),
 (71,'Mega Crust',    78,15000,270, 320,210,3,1,5,400,1400,430,140, 10,NULL),
 (70,'Queen Rainer',  82,19000,305, 350,230,3,3,5,400,1400,475,160, 10,17),
 (73,'Drakan',        86,29000,425, 480,305,3,5,5,400,1400,570,210, 20,50),
 (74,'Alpha Crust',   92,34500,489, 540,360,3,1,5,400,1400,620,240, 10,NULL),
 (72,'Phantom Knight',96,41000,560, 610,425,3,2,5,400,1400,690,270, 30,50),
 (75,'Great Drakan', 100,50000,650, 700,495,3,5,5,400,1400,800,305, 30,50),
 (77,'Dark Phoenix', 108,95000,950,1000,600,1,6,7,400,1500,900,350,150,50);
-- 76 (Dark Phoenix Shield, 106 / 73000 / 850-960 / 580, Lightning) only if the shield is modelled
--   as WZ's linked row (Q6); MuMain never draws it on its own.
-- kDropRates: MoneyRate 14 for all; MaxItemLevel 3, the Phoenix 4; respawn = RegTime + 1.
-- kResistances {number, ice, poison}: {69,9,9},{71,9,9},{70,11,9},{73,12,12},{74,13,13},{72,14,14},
--   {75,15,15},{77,30,30}.
-- kBosses += 73, 72, 75, 77 (A.Type 150).
-- monster_spawns: 64 one-tile rows on map 10 (Icarus.cs:55-118, OM's 30,231); no gate row (no safe box).
-- The view range is 5 for 69-76, 7 for the Phoenix: the shortest of any breed in the game's sky map.
```

### 5. Drops

**Nothing Icarus-specific in OM 095d**: no drop group, `NumberOfMaximumItemDrops = 1`, MaxItemLevel 3
(4 for the Phoenix). MU2_BGFX's window (`src/sim/realm_items.cpp:1021-1022, 1236-1250`: drop level
in [L-15, L], and `(L-DL)/3` no more than MaxItemLevel for the refinable groups) is [L-11, L] for
refinable gear (+0..+3), [L-14, L] for the Phoenix.

**What each breed reaches today** (cooked rows, `a/drops_out.txt`; [2] = second class only, which
falls only on map 8 and in Blood Castle 6, `src/sim/realm_items.cpp:1130-1137`, `src/sim/items.h:388-397`):

| killer | L | window | pool, [2] marked |
|---|---:|---|---|
| Alquamos | 75 | 64-75 | Orb of Penetration 64, Crystal Morning Star 66, Bluewing 68, Staff of Resurrection 70, Aquagold 72, Crystal Sword 72, Scroll of Aqua Beam 74; [2] Elemental Shield, Grand Soul Gloves, Divine Gloves, Grand Soul Shield |
| Mega Crust | 78 | 67-78 | + Black Dragon Gloves 76, Boots 78; [2] + Grand Soul Boots |
| Queen Rainer | 82 | 71-82 | Aquagold, Crystal Sword, Aqua Beam, Black Dragon Gloves/Boots/Helm 82, Cometfall 80, Sword of Destruction 82; [2] 5 rows |
| Drakan | 86 | 75-86 | Black Dragon Gloves/Boots/Helm/Pants 84, Cometfall, Sword of Destruction, Saint Crossbow 84; [2] Divine Helm, Dark Phoenix Gloves 86, ... 6 rows |
| Alpha Crust | 92 | 81-92 | Black Dragon Helm/Pants/Armor 90, Sword and Staff of Destruction, Saint Crossbow, Inferno 88; [2] **12 rows** |
| Phantom Knight | 96 | 85-96 | Black Dragon Armor, Staff of Destruction, Inferno -- **3 rows**; [2] 12 rows |
| Great Drakan | 100 | 89-100 | Black Dragon Armor, Staff of Destruction -- **2 rows**; [2] 11 rows incl. Dark Phoenix Armor 100 |
| Dark Phoenix | 108 | 94-108 | **nothing**; [2] Dark Phoenix Pants 96, Armor 100, Great Reign Crossbow 100, Dragon Soul Staff 100 |

**So without map 10 in `secondGearHere`, the south half of Icarus drops almost nothing but Zen and
jewels.** Every item row the game has stops at 100 (the Archangel weapons at 100-104 do not drop).
The Phantom Knight has three rows, the Great Drakan two, the Phoenix none. With map 10 added, Icarus
is where the whole Dark Phoenix set falls by the window, armour 100 included, which Tarkan's bosses
cannot give (tarkan-port §5, its open question 3). Open question 3.

**MU's Icarus items, as researched.**
- **Loch's Feather** (13,14), the 2nd wings' ingredient. WZO `item(Kor).txt:332` drop flag 1, level
  78: it fell from any monster of its level, Tarkan's and Blood Castle's too, until **0.98c (2004-02-05)
  made it Icarus-only** (`a/muhistory.md:464`; `WZ gObjMonster.cpp:4618-4623` `m_bFeatherOnlyIcarus`;
  WZD `commonserver.cfg:308` `DropFeatherOnlyIcarus = 1`). OM S6 gives Icarus a drop group: 0.1% from
  monsters of level 82 or over (`OM/VersionSeasonSix/Maps/Icarus.cs:30-37`), so not the Alquamos or the
  Mega Crust. OM 095d has no feather at all. **Ours** (`Quest04`, `drops_from_monsters: false`):
  1 in 500 from monsters of 60 or over in Atlans and the Lost Tower, "this game has no Icarus"
  (`src/sim/items.h:185-192`; `docs/second-wings.md:30-31`). An Icarus port gives it MU's home back.
- **The Crest of Monarch** (13,14 level 1, the Dark Lord's cape) shares S6's second group
  (`OM/VersionSeasonSix/Maps/Icarus.cs:39-47`) and WZ's `m_bMonarchOnlyIcarus`
  (`gObjMonster.cpp:5465-5478`; WZD `commonserver.cfg:334`), with the Dark Horse and Dark Spirit souls
  (`:5480-5510`). Dark Lord items, Season 1+; not ours.
- **The Jewel of Creation** (14,22): KR's preview puts it in the sky map ("천공 맵에는 봉인석 중의 하나인
  '창조의 보석' 이 있으며", `ref62.txt:9-12`); WZO `item(Kor).txt:380` drop flag 1, level 78. Ours is
  the Rune of Creation, repurposed, from level 15 (`src/sim/items.h:742-754`). Showing only.
- **The second class's gear**: KR's same patch brought it (`ref39.txt:54-65`): "1차 전직 아이템
  엑설런트 드롭 레벨 : 기존 아이템 드롭 레벨 + 35" (excellent second-class gear drops at its level + 35).
  WZ also makes a second-class item less likely to come excellent (`gObjMonster.cpp:4532-4550`, under
  `NEW_SKILL_FORSKYLAND`).
- **No golden monster in Icarus's era** (§5.1).

**Jewels.** Chaos stops at 66 (atlans-port §5): no Icarus breed drops it. Bless and Soul reach all.
Life (72) does not drop from monsters here (`source/items/misc/Jewel03.json`).

**Class treasures.** MU's second-quest items drop from levels 62-76 (KR `ref39.txt:46`; WZO
`Quest(Kor).txt:53-55`, tarkan-port §5): in Icarus that is the Alquamos (75) alone.

#### 5.1 The Golden Crust and the Box of Kundun +3

- **WZ only, 2009** (`ADD_GOLDEN_EVENT_RENEWAL_20090311`, Season 4.5): `RegenGoldenCrust` puts the
  Golden Crust (496) on a random open tile of the whole map, 1-255 (`a/wzsrc/EledoradoEvent.cpp:841-882`).
  It drops **one Box of Kundun +3** (14,11 level 10) to the top damage dealer (`WZ gObjMonster.cpp:4240-4252`).
  `docs/kundun-box-sources.md:222-231` already lists it.
- Two traps in that code. The map line is `#ifdef GAME_VERSION < G_V_S4_5`, which tests only that
  `GAME_VERSION` is defined, so the Lorencia branch is the one compiled (`:852-857`). And on Icarus
  "1-255" draws on WZ's server grid (`gMSetBase.GetBoxPosition`), whose open east half (§1.3) the
  client cannot reach.
- **Not in OM** (no Golden Crust in `OM/VersionSeasonSix/InvasionMobsInitialization.cs`), not in WZO.
  Not 0.95d. Not proposed.

### 6. Rules of the place

- **Entry**: from Lost Tower 7's south end, gate 62 (17,250-19,250), at **160** in every source, to
  exit 63 (14,13-16,13) facing East; back by gate 64 (14,12-16,12) at 80 (WZO) or 50 (OM S6) to exit 65
  (17,249-19,249). MU2_BGFX doubles MU's gate levels but has brought Atlans and Tarkan back near their
  monsters (`src/sim/gates.cpp:63-87`: Atlans 70 in, Tarkan 100 in). Open question 2.
- **Wings or a Dinorant, at the door and the warp.** WZ: the helper must be a Horn of Dinorant (13,3)
  or (later) Fenrir or Dark Horse, or the wing slot a wing 12,0-12,6 (later any wing, the Cape of Lord
  13,30); and no Horn of Uniria (13,2) and no Ring of Transformation (13,10)
  (`MoveCommand.cpp:319-343`, gates `user.cpp:27202-27242` and `:26299-26339`, message 1604). OM:
  `CanFly`, checked only at a gate and a warp (`OMGL/PlayerActions/WarpGateAction.cs:52`,
  `WarpAction.cs:51`). In this game the 1st wings ask 180 and the Dinorant 160, so at 160-179 it is
  the Dinorant.
- **Once in, he cannot become wingless.** WZ refuses to move (`user.cpp:16880-16905`) or drop
  (`protocol.cpp:5357-5375`) the Dinorant when no wing is worn, or the wing when no Dinorant is
  ridden; MuMain refuses to pick either up (`NewUIMyInventory.cpp:1491-1503`); KR says so
  (`ref62.txt:56-60`). OM has no such rule: an OM player can take his wings off in Icarus and stays.
  There is **no fall** in any source.
- **The Dinorant wears out.** At 0 life in Icarus WZ equips a spare horn from the bag, or with no
  spare and no wing sends him to Devias, gate 22 (`user.cpp:10813-10842`; KR `ref39.txt:22-23`). Our
  Dinorant takes life for each blow (`src/sim/items.h:882`), so this rule has work to do here.
- **No Uniria.** WZ refuses it at the door; MuMain will not equip it in Icarus
  (`NewUIMyInventory.cpp:351-354`); KR (`ref62.txt:67-69`). mount.md §Maps already lists it
  (`docs/mount.md:74, 199`).
- **No elf summon.** WZ `gObjMonsterCall` returns at once on map 10 (`user.cpp:29568-29572`); entering
  kills the summon (`MoveCommand.cpp:337-342`); MuMain does not even send the cast
  (`ClassAttack.cpp:117-126`); KR (`ref62.txt:71-74`). The elf's summons count in this game
  (memory "Elf summon counts"), so this is a real rule for us.
- **No Ring of Transformation** (WZ `user.cpp:17850-17866`). We have none.
- **Death -> Devias.** WZ: the death branch sets `MapNumber = 2` for map 10 and draws a tile of
  Devias's regen box 197,35-218,50 (`user.cpp:22077-22081`, `MapClass.cpp:195`). KR: "이카루스에서 사망할
  경우 데비아스 마을로 소환됨" (`ref39.txt:24`; `ref62.txt:65`). OM 095d and S6: the Lost Tower hall
  (`Icarus.cs:44`; `OMGL/Player.cs:1559-1562`, its spawn gate 42). **Settled: Devias** (WZ = KR; OM's is
  its own). lost-tower-port §1.1 noted the split and left it; this is the answer.
- **Town Portal**: WZ's scroll has no map-10 row, so it falls to the last `else`, **gate 17, Lorencia**
  (`protocol.cpp:17924-18036`). OM: the map's `SafezoneMap`, the Lost Tower hall
  (`OMGL/PlayerActions/ItemConsumeActions/TownPortalScrollConsumeHandlerPlugIn.cs:33`). KR is silent.
  MuMain allows the scroll there (`MM/World/MapInfra/PortalMgr.cpp:56`). This game sends a death and a
  Town Portal from a map with no safe box to its row's `home` alike (`src/sim/realm_fight.cpp:1614-1623`,
  `src/app/modes/play_mode.cpp:736-750`, as Blood Castle's `home` is Devias). Open question 4.
- **Mounts fly.** MuMain lifts a Dinorant rider 90 units in Tarkan and Icarus (`ZzzCharacter.cpp:6387-6390`,
  `DefaultCamera.cpp:539-542`, `WSclient.cpp:1339-1342, 2109-2112, 2182-2185`) and plays the rider skill's
  flying clip there (`SkillCast.cpp:315-318`, `WSclient.cpp:4739-4742`). Ours has the Tarkan arm:
  `src/game/pets.h:39-44` `dinorantFlies(world) { return world == "tarkan"; }` -- "Icarus is not in this
  game". One string to add.
- **Sound and music**: `SOUND_HEAVEN01` (`aHeaven.wav`) loops (`MM/Scenes/SceneManager.cpp:885-895`,
  stopped `:960-963`); its two thunder lines are commented out. `MUSIC_ICARUS` (`:1054-1059`,
  `D/Music/icarus.mp3`). Our rule (memory "Music is rare"): the wind bed, no track.
- **The sky's showing**, Part B's: no terrain drawn (`MainScene.cpp:463`); clear colour 3,25,44
  (`SceneManager.cpp:385-386`); `cloud.bmd` and `clouds.jpg` (`MapManager.cpp:150-156`); object type 0
  breathes 20 cloud particles (`ZzzObject.cpp:3090-3105`); lightning sprites and thunder joints on
  types 0-5 and a light flash 1 000 units below the hero one frame in ten (`ZzzObject.cpp:4349-4363,
  4388-4398`); no shadow for any character, wing, boid or object (`ZzzObject.cpp:9500`,
  `ZzzCharacter.cpp:8406, 8594, 8670`, `GOBoid.cpp:1586, 1641`); items on the ground bob
  (`ZzzObject.cpp:6497-6500`); up to three flying dragons as boids (`GOBoid.cpp:826-840, 1236-1240`);
  rain always (`src/game/world/weather.h:9`, from `ZzzEffectFireLeave.cpp`).
- **Nothing else**: no traps, no PK rule, no NPC, no merchant, no town; exp multiplier 1. A hero who
  needs potions takes the M window, a scroll, or the walk back through seven floors.

### 7. Where the sources disagree

| point | OM 095d | OM S6 | WZO (2004-05) | WZ C++ (0.97d+) | WZD | KR | settled / flagged |
|---|---|---|---|---|---|---|---|
| in 0.75 | -- (075 has no Icarus) | | | | | 2003-07-15 (MuHistory 0.94b 2003-06-06) | **not 0.75** (decision #1) |
| reachable | **no gates, no warp** | gates 62-65, warp 23 | gates 62-65, warp 23 | flying-item check | = WZO | "LT 8F [18,250]" | WZO/S6 |
| way in | -- | 160 | 160 | 2/3 for MG/DL | 160 | 160 | **160** (ours?: Q2) |
| way out (64) | -- | **50** | 80 | | 80 | | flag |
| warp | -- | 170, 10 000 | 170, 10 000 | | 170 | 170, **15 000** | 170 |
| flying item | `CanFly` at gate/warp | + Fenrir | | wing 12,0-6 or Dinorant; no Uniria, no ring | | wings or Dinorant, no Uniria | **WZ = KR** |
| unequip in Icarus | allowed | allowed | | refused (last one) | | refused | **WZ = KR = MM** |
| death | Lost Tower hall | = | | **Devias** | | **Devias** | **Devias** |
| Town Portal | Lost Tower hall | = | | Lorencia (fall-through) | | -- | Q4 |
| elf summon | allowed | = | | refused, killed at the door | | refused | **WZ = KR = MM** |
| grid | S6 file = client + junk | | 41 109 sky tiles open | | = WZO | | **client** |
| spawns | 64 | = 095d | **63 + Phoenix, one tile off** | type-2 ±3 | 64 - Phoenix + 59 spots | | **OM's 64** (30,231 open) |
| stats | as §4.2 | = 095d | **identical** | | Shield 83 000, Phoenix 1050-1560 / def 1200 | launch +13-58% HP | **OM = WZO** |
| respawn | Mega/Alpha/PK/GD **3 s**, Phoenix **15 s** | = | 10 / 10 / 30 / 30, **150** | + 1 | 5 / 10, Phoenix 1800 | | **WZO, +1** (Q5) |
| A.Type 150 | not created: melee | = | Drakan, PK, GD, Phoenix | 1 in 5 Flame of Evil | 150 | | **WZ's** |
| Phoenix shield | row 76, never spawned | = | row 76 under bosses | linked row, 6 s toggle, never fights | | "Night Queen's shield" | Q6 |
| Loch's Feather | none | 0.1% from L82 | drop level 78 | Icarus-only (cfg) | Icarus-only | Icarus-only from 0.98c | Q7 |
| Golden Crust | -- | -- | -- | +3 box (2009), compiled to Lorencia | | | not ours |

### 8. Open questions

1. **Decision #1: port a post-0.75 world.** Evidence in §1.1. Icarus is half a year after Tarkan,
   which the game took; it came in the patch that brought the class change, the second-class gear
   and the Dinorant, all already built here. If yes: which era's numbers? OM 095d = WZO (2005) for
   stats; the launch table (KR `ref62.txt`) has no rates and no spawn table. Recommendation: **OM =
   WZO**, as Atlans and Tarkan.
2. **Gate level.** MU: 160 in, 80 (or 50) out. Our gates doubled MU's and then came back near the
   monsters (Atlans 70 for 43-74, Tarkan 100 for 72-93). Icarus's breeds are 75-108, Tarkan's band
   moved up by four levels at the bottom and fifteen at the top. **But the flying item is the real
   bar**: our Dinorant asks 160 and the 1st wings 180, so any door level under 160 still lets no one
   in before 160. Options: 160 as MU, matching the Dinorant; a lower door with the Dinorant's 160
   moved; or 160 with the way out at 80. **Ask.**
3. **Second-class gear on map 10.** As built, the Phantom Knight, Great Drakan and Phoenix (25 of
   64 spots) reach three, two and zero ordinary rows (§5). Options: add map 10 to `secondGearHere`
   (`realm_items.cpp:1130-1137`), making Icarus the Dark Phoenix set's home (MU's own: the set is named
   for its boss); or leave the south half to Zen and jewels. **Ask** -- Tarkan's question 3 is the same
   question.
4. **Where death and the Town Portal land.** WZ and KR: death to Devias; WZ's scroll to Lorencia;
   OM both to the Lost Tower hall. Our `home` field takes one map for both. Recommendation: **Devias**
   (MU's death rule, and the Lost Tower's own door is in Devias).
5. **Respawns.** OM's 3 s for four breeds and 15 s for the Phoenix are not WZO's 10-30 s and 150 s.
   With 64 monsters on a 292-step road, 3 s keeps the road full; 30 s thins the south.
   Recommendation: **WZO's, +1** (11 / 21 / 31 / 151 s), as the memory's rule. Or OM's for the
   ordinary breeds if the road feels empty.
6. **The Phoenix's shield.** MU draws a rider; WZ toggles a "shield" every 6 s that only changes its
   spell and its look; row 76 never fights. Options: show only (the toggle and its effect); make it
   mean something (damage taken cut while up, ours); or drop it. Recommendation: **show the toggle,
   cut nothing**, as WZ.
7. **Loch's Feather.** MU from 0.98c: Icarus only. Ours: Atlans and the Lost Tower, 1 in 500 from 60
   (the user, 2026-10-04: 'Rare drop, high maps'). Options: move it to Icarus (MU's), add Icarus to
   `featherMap` (`src/sim/items.h:192`), or leave it. **Ask.**
8. **The no-wingless rules.** WZ's unequip lock, the Dinorant-at-0 sendoff and the summon ban are all
   needed to make "no fall" true. Recommendation: **all three, WZ's**, with the bag refusing and
   saying why. A fall (lose the wing, drop to death) is in no source; ours if wanted.
9. **The Golden Crust.** S4.5 and not 0.95d. Recommendation: no.

### Where Icarus sits

Our worlds by monster level (`source/mu.db` spawns): Lorencia 2-19, Noria 3-18, Devias 20-52, Dungeon
19-55, Lost Tower 47-66, Atlans 43-74, Tarkan 72-93, Blood Castle (instanced, to 99). **Icarus's 75-108
is the top of the open world**, overlapping Tarkan's 72-93 for its first four breeds (35 of 64 spots)
and passing Blood Castle 6 with the Great Drakan and the Phoenix. The Phoenix (108) would be the
highest monster in the game. MU's own order is the same: Atlans (60-70) -> Tarkan (130) -> Icarus
(160), and KR tied it to the Lost Tower: the door is past LT7's Balrogs, 225 steps from LT7's arrival.
At 100x a Great Drakan pays 633 000; a level at 160 asks 792 000 (`rules.cpp` `neededExperience`).

The graph: **Devias -> Lost Tower 1 ... 7 -> (gate 62) -> Icarus**, one way in and the same way out,
plus the warp. Icarus touches no other world.

---

### Side findings (outside Icarus)

1. **lost-tower-port §1.1's open split is settled**: WZ sends Icarus deaths to Devias, and KR's own
   patch note says so (`ref39.txt:24`); OM's Lost Tower hall is OM's.
2. **The Dinorant's recipe has two sources that disagree.** KR's 2003-07 note and OM 095d: **3** Horns
   of Uniria + 1 Chaos + 250 000 Zen, 70% (`ref39.txt:71-73`; `OM/Version095d/ChaosMixes.cs:242-268`).
   Ours follows WZ 1.00.93: **10** horns and 500 000 (`src/sim/machine.h:208-215`). Flag; the user's.
3. **KR's launch Sevina stood in Devias at 183,32** (`ref39.txt:39`), as our class-change doc has her
   in Devias.
4. **OM's Icarus respawns are not WZO's** (§4.2), unlike Tarkan's. OM's "Queen Rainer" and "Great
   Drakan" are WZO's "퀸레이너" and "자이언트드라칸" (Giant Drakan); KR's English is "Queen-Rainer",
   "Giant Drakan", "Phoenix of Darkness" (`ref62.txt:92, 115, 127`).
5. **WZ's Golden Crust regen compiles to Lorencia** (`EledoradoEvent.cpp:852-857`, an `#ifdef` with
   a comparison), and the other renewal regens carry the same `#ifdef` shape
   (kundun-box-sources §3.2 noted the Lorencia lines). Not ours.
6. **`gObjSkylandBossSheildAttack` is dead code in WZ** (`gObjMonster.cpp:3153`, no caller in the
   files on disk).
7. **`src/game/pets.h:43` says "Icarus is not in this game"**, and `src/sim/items.h:186` the same
   for the feather: both comments change with a port.
8. **The clone at `.../9142efc4-.../scratchpad/wz/` is no longer full**: `MapClass.cpp` and
   `MoveCommand.cpp`, which tarkan-port cites, are gone from it. They are in `a/wzsrc/` now.

---

## Part B: the data and the look

Research notes, 2026-10-06. Nothing tracked was edited, nothing was cooked, no window was opened. Icarus is MU server map **10** (`WD_10HEAVEN`, `MM/World/MapInfra/MapManager.h:19`; WebZen's `MAP_INDEX_ICARUS = 10`, `define2.h:4047`). The client ships it as `D/World11` + `D/Object11` (folder = map + 1).

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- WZS = WebZen GameServer 1.00.93 source, cloned by an earlier session (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9142efc4-e29e-4d88-9ee6-bb4831450426/scratchpad/wz/Source/Server Side/GameServer`, cp949, not in the repo)
- S = `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/129c72c0-811f-4da1-b4a6-e27b489786ba/scratchpad/icarus/b/`

Scratch outputs in S. The scripts are the Tarkan pass's (`.../2dfb4978-.../scratchpad/tarkan/b/`), copied with tarkan→icarus and World9→World11 swapped; `prev.py` was patched for a map with no safe zone, no NoGround and a flat height.
- `icarus/`: `pipeline/terrain.py D/World11 icarus S 11` (height/tiles/attributes/light.png plus icarus.json; there are no table rows for map 10 yet, so nothing is flagged hidden). `terrain.log` is the run.
- `obj/`: every Object11 .bmd (Object01-11, 13-16) and `cloud.bmd` through MuExtract `export-obj` + `export-rig --actions=all`, a .log each. `objinfo.log` summarises them (type, placements, tris, bones, bbox, sheets).
- `mon/`: Monster51-57 (the Icarus breeds) and Monster32 (the sky dragon boid), each export-obj + rig with every action; Sword18, Sword19 and Shield15 (the monsters' arms).
- `tex/`: every World11/Object11 sheet decoded, the Effect sheets Icarus loads (clouds, cloudLight, JointSpirit, JointThunder, Thunder01), World1's rain sheets (MU's rain is World1's, §2), and the monsters' and arms' sheets. `tex/ozt/` holds the .OZT (alpha) sheets separately, because `iui02`, `iui03`, `drt02` and `drt03` ship as both .OZJ and .OZT under one stem.
- Sheets to look at:
  - `sheet_world11.png`: the TerrainLight, the one tile sheet, leaf02, World1's rain sheets, and the Effect sheets Icarus draws its sky with (clouds, clouds2/3, cloudLight 1-3 and _red, JointSpirit01/02, JointThunder01, Thunder01)
  - `sheet_object11_textures.png`: every Object11 sheet
  - `sheet_objects.png` and `sheet_monsters.png`: flat-shaded renders, texel colour per triangle (Monster56, the phoenix, is in a stretched bind pose)
  - `sheet_monster_textures.png`: the monsters' and arms' sheets, alpha as magenta, .OZT twins prefixed T
  - `prev_l1_l2_grass_attr_height_light.png`: layer 1 | layer 2 | grass-able | attributes (white open, dark red NoMove) | height (flat, so black) | light
  - `light_big.png`: the TerrainLight at 3×
  - `placements_map.png`, `placements_map_crop.png`: every placement over the walk mask, by type (red Object11 balustrade, white-violet cloud emitters, magenta Object13 temples, blue Object15 stairs, cyan Object14 shards, orange/yellow pillars 08-10, green floor plates 07/16)
  - `minimap_small.png`: the Season `mini_map.OZT`
  - `att_client_om_diff.png`: client grid | OM grid | difference
- Logs: `stats.log`, `objinfo.log`, `placements.log`, `spawns.log`, `attdiff.log`, `tex.log`.

### The one thing to know first

**Icarus has no ground. MuMain never draws its terrain: the map is a road of additive cloud sprites, lined with pillar-arches, hanging over a deep navy clear colour, with stone ruins standing in the dark 2-3 m under the road.**

- **The terrain is skipped outright.** `RenderTerrain` is called for every map but Icarus (`MM/Scenes/MainScene.cpp:463`: `if (WorldActive != WD_10HEAVEN && WorldActive != -1)`). The terrain still exists for walking: every character stands on `RequestTerrainHeight`, which here is a flat **2.85 m** everywhere (TerrainHeight.OZB is the byte 190 in all 65,536 cells, × 1.5). Nothing marks that plane on screen.
- **The clear colour is navy, (3, 25, 44)/256** = (0.012, 0.098, 0.172) (`MM/Scenes/SceneManager.cpp:385-386`), the only blue sky among the 0.97 maps (Devias's is a pale snow blue). Fog is off in MuMain's default camera (`MainScene.cpp:640`), so the colour is only the background.
- **What you walk on is cloud.** 335 hidden emitter boxes (Object01-06, types 0-5, §1.2) each scatter 10 or 20 `BITMAP_CLOUD` particles (`Effect/clouds.jpg`, a soft white cumulus puff) the first frame they are drawn, and are hidden after (`MM/Engine/Object/ZzzObject.cpp:3090-3169`). The puffs are about 4.6-5.1 m sprites (`clouds.jpg`'s 256 px × scale 1.8-2.0, `MM/Render/Effects/ZzzEffectParticle.cpp:3103-3117, 8923`), drawn **additively** (3-channel sheet, `:8925-8928`) at light 0.1 each, turning slowly in place and bobbing ±20 cm on a 31 s period (`:7911-7938`). They live as long as their emitter is in view, and are re-thrown when it comes back (`:7915-7922`). The emitters sit at z 1.35 m, so the cloud bank lies about 1.1-1.3 m under the walking plane. The Season minimap (`minimap_small.png`) paints exactly that road in teal-white cloud.
- **The road's shape:** 7,026 walkable tiles (10.7%) in one connected band 15-25 tiles wide, x 9-97, y 12-241: from the landing at (14-16, 13) east along the north, round a loop in the north-east, then a long stem south to the boss's end at (34, 238) (`placements_map_crop.png`). Everything else is NoMove (0x04). There is **no NoGround tile, no safe tile and no water** in the grid.
- **What stands in the void.** Every object is placed at about z −0.15 m, three metres under the walkers' feet:
  - **Object11, 180 pillar-arches** (5.4 m tall, `top02_R`: white at the top fading to black at the foot), lined along both edges of the road at about 1.5 tiles' spacing (red in the placement map). Their tops stand 2.4 m over the walkers; their feet vanish into the dark. Each carries a small winged-statue card (`gyg_R`) and throws a white rising light mote every frame (§2).
  - Object08-10, 39 **floating pillars** that bob 10 cm and turn about their axis.
  - Object13, 31 **domed temples with spires** set off the road at scale 0.24-0.52 (2.3-5 m), down in the void: distant floating shrines.
  - Object15 stair-platforms (39), Object14 broken flagstones (11), Object07/16 floor plates (35): the "ruins", 2.5-2.7 m under the road, seen through the additive cloud.
- **Weather: it rains, always.** MoveLeaves sets `RainTarget = MAX_LEAVES/2` on Icarus (`MM/Render/Effects/ZzzEffectFireLeave.cpp:431-434`), and CreateHeavenRain makes every one of the 80 leaf slots a rain streak (`:246-270`), spawned 2-4 m over the hero and dying at the 2.85 m plane with no splash ring (`:364-382`). It is World1's `rain01.tga` (`MapManager.cpp:1489`); World11 ships its own `rain01.OZT` that nothing loads.
- **Lightning.** Once a frame in 50, `MoveHeavenThunder` flashes `cloud.bmd` under the hero and adds a pale yellow terrain light; one flash in five also throws two lightning bolts 3 m down in the clouds (`ZzzObject.cpp:4254-4335`). Separately, a visible cloud emitter, chosen at random, sometimes lights from inside with `cloudLight.jpg` and two small bolts (`:4381-4397`), and once a frame in 10 a white glint appears 10 m under the hero (`:4349-4362`). The thunder sounds are commented out: **MU's Icarus lightning is silent** (`SceneManager.cpp:885-895`).
- **Life in the sky.** Up to **3 red dragons** (Monster32), tinted near black-blue, glide 6 m under the hero (`MM/Engine/AI/GOBoid.cpp:826-880, 1236-1241`; `ZzzObject.cpp:487-492`). And 10 invisible ground boids each drag a faint grey `JointSpirit` ribbon round the hero at foot height: wind wisps (`GOBoid.cpp:850-878`; `ZzzEffectJoint.cpp:1562-1567`).
- **No shadows at all.** Characters, monsters, wings, items and boids skip their projected shadows here (`ZzzObject.cpp:794, 869, 1004, 9500, 10579`; `ZzzCharacter.cpp:8406, 8594, 8670`; `GOBoid.cpp:1586, 1641`). Nothing would receive them.
- **You must fly.** The server lets nobody in without wings (12,0-6), the Dinorant (13,3), or later the Dark Horse, Fenrir or the Cape (WZS `user.cpp:26299-26320, 27202`; OM `Version095d/Maps/Icarus.cs:47-50`, `Stats.CanFly`). Off a safe tile a winged hero plays PLAYER_FLY (`ZzzCharacter.cpp:612-623`), and Icarus has no safe tile, so **every winged hero flies the whole map**. A Dinorant rider hovers 90 cm up as in Tarkan (`:6387-6390`).
- **Sounds:** `aHeaven.wav` (wind high in the air) loops always; `icarus.mp3` plays always (`SceneManager.cpp:885-886, 1054-1059`).

**The grids:** OM's `Resources/Terrain11.att` matches the client's walkability on **99.44%**. The 369 tiles where they differ are all tiles OM **opens** and the client closes, scattered over rows 157-255 at odd values (14, 27, 109, 237...): corrupt bytes in OM's file, not a design. WebZen's `Terrain11.att` in the extracted server data matches on only 37% (48,134 walkable): a different revision or a different map, not used. All 64 of OM's 0.95d spawns stand on client-open tiles. **Recommendation: use the client grid.**

---

### 1. Raw data inventory

#### 1.1 World11 (`D/World11`, 13 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain11.map | 196610 | yes | 3 planes: layer1, layer2, alpha. **Never drawn** (`MainScene.cpp:463`) |
| EncTerrain11.att | 131076 | yes | 16-bit attributes: 7025 open, 1 value 2, 58,510 NoMove |
| EncTerrain11.obj | 20104 | yes | **670 placements, 15 kinds** |
| TerrainHeight.OZB | 66620 | yes | read at 1080: **190 in every cell**, a flat 2.85 m |
| TerrainLight.OZJ | 22699 | yes | 256² baked light, **grey** (R=G=B), mean 52 |
| TileGrass01.OZJ | 111398 | loaded, unseen | dry brown grass. 99.7% of layer 1. Undrawn |
| leaf01.OZT, leaf02.OZJ | 1072 / 4445 | half | 16² sprites. MuMain asks for `leaf01.jpg` on this map (`MapManager.cpp:1477`), which is not here, and every leaf slot is rain anyway |
| rain01.OZT | 560 | **no** | MuMain loads World1's `rain01.tga` for every map (`MapManager.cpp:1489`) |
| Terrain.map, Terrain11.att | 196609 / 65539 | no | plain copies. Terrain11.att here is not an 8-bit grid the decoder reads (89% raw agreement) |
| mini_map.OZT, Minimap.bmd | | Season UI | 1024² minimap: the road in cloud, the rest transparent |

There is no TileGrass .OZT: no grass would grow even if the terrain were drawn.

terrain.py (`S/terrain.log`, `stats.log`):

```
height     2.85 to 2.85 m (flat; byte 190 everywhere). Header says pixels at 54; reading 1080 as the client does
tiles      6 slots in use: 0 TileGrass01 99.7%, 1 TileGrass02 0.3%, 7-10 Rock01-04 ~0 (only as layer-2 overlays)
walkable   10.7% (7,026), one region, x 9-97, y 12-241; 0.0% safe
NoGround   0
light      mean [52,52,52]; walkable [66,66,66]; walkable luminance p10/50/90 0/50/186
objects    670 placed, 15 kinds; missing: none (Object12 is not shipped and nothing places type 11)
```

**Attributes:**

| value | tiles | meaning |
|---:|---:|---|
| 0 | 7025 | open |
| 2 | 1 | Character (a stray) |
| 4 | 58510 | NoMove |

**Tile sheets.** Only TileGrass01 (slot 0) is laid; TileGrass02, TileRock01-04 appear as small layer-2 overlays (2,539, 49, 23, 2 and 39 tiles) and their .OZJ files are not even shipped. None of it is drawn. TileGrass01 is a dry, brown, straw-like grass: a placeholder the map was started from.

**Light map** (`light_big.png`):
- Grey and colourless: R=G=B on every cell.
- Off the road it is a flat 50.
- Along the western stem (x 2-25, y 0-250) it is a **bright white glow** (180-255) studded with black dots where objects stand, and darker pools round x 15-50, y 100-250.
- The north-east loop has only a faint ring of dark dots on 50 grey: its painted light does not match the road there.
- It reads as baked from an earlier layout of the road. Since nothing draws the ground, MU uses it only to light objects and characters (BodyLight samples it) and as the canvas the thunder flash adds to.

#### 1.2 Object11 (`D/Object11`, 22 files)

The folder holds:
- 15 `ObjectNN.bmd` (01-11, 13-16)
- `cloud.bmd` (MODEL_CLOUD, the thunder flash, `MapManager.cpp:150-156`)
- 6 sheets: `cloud`, `gyg_R`, `test12_H`, `top02_R`, `top03_B`, `top04_B`

`top03_B` (teal stars) and `top04_B` (violet stars) are on disk but no model names them.

Type = file number − 1. Sizes are the OBJ bbox in cm, x × y(up) × z. "z" is MU's stored height; the walking plane is at 285.

| type | model | placed | tris | rig | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| **0-5** | Object01-06 | 16 / 21 / 8 / **115** / 61 / **114** | 12 | 1 | test12_H (2×2) | **hidden cloud emitters** (50 cm boxes). Each throws BITMAP_CLOUD subtype = its type, 20 puffs for 0-2 and 10 for 3-5, then hides (`ZzzObject.cpp:3090-3169`). z 1.35 m (a few up to 4.6 m), scale 0.14-1.96. 335 in all, on and off the road |
| 6, 15 | Object07, 16 | 17 / 18 | 10 | 1 | top02_R | **floor plates**, 1.7 × 1.5 m, 6 cm thick; Object07 takes the dark end of the sheet, Object16 the white. 35, in the north-west and north-east corners, at z −0.15 (some tipped 40-90°): 2.7 m under the road |
| **7-9** | Object08-10 | 13 / 12 / 14 | 46 | 3 bones, **12 / 24 / 24 keys** | top02_R | **floating pillars**, 4.5 m, white tops fading black. Bone 0 bobs ~10 cm; Object09/10 turn about their axis, Object08 only rocks. Mostly in the south half; a few tipped |
| **10** | **Object11** | **180** | 172 | 6 (static) | top02_R, **gyg_R** | **the balustrade**: a 4 × 5.4 m pillar with a pointed arch springing from it, a 10-tri card of a **winged statue** (`gyg_R`: two figures on black) on top. Along both road edges. **Throws a BITMAP_LIGHT mote at bone 3 every frame** (`ZzzObject.cpp:3171-3178`): a white spark that rises and drifts on the wind (`ZzzEffectParticle.cpp:7941-7960`) |
| 12 | Object13 | 31 | 411 | 1 | top02_R | **domed temples** with four spires, 9.7 m at scale 1, placed at 0.24-0.52: 2.3-5 m miniatures standing off the road in the void, z −2.35 to +1.15 |
| 13 | Object14 | 11 | 100 | 1 | top02_R | **broken flagstone shards**, a 4.3 × 4.7 m scatter, 8 cm tall, on the road at z −0.15 |
| 14 | Object15 | 39 | 620 | 4 (static) | top02_R | **stair-platforms**: a stepped stone landing, 5.2 × 6.2 m, 1.1 m tall, along the middle of the road, z −0.15 |
| — | cloud | (effect) | 32 | 1 | cloud (grey marble-like cloud) | **MODEL_CLOUD**, a flat 3.6 m 25-vertex sheet. The thunder flash: drawn at **scale 10** (36 m), BlendMesh 0 (additive), 2 m north of and 1.9 m under the hero, for 2 frames (`MM/Render/Effects/ZzzEffect.cpp:2966-2972, 9014-9016`) |

**Placement counts, most first:** Object11 180, Object04 115, Object06 114, Object05 61, Object15 39, Object13 31, Object02 21, Object16 18, Object07 17, Object01 16, Object10 14, Object08 13, Object09 12, Object14 11, Object03 8.

**Animated placed kinds:** Object08-10 (39 pillars). That is all; everything else is static. **The map's motion is its particles**: the cloud puffs, the 180 mote streams, the rain, the lightning, the dragons and the wisps.

**Map shape** (`placements_map_crop.png`; "north" means low y):
- **North-west: the landing** at (14-16, 13), facing south (OM `VersionSeasonSix/Gates.cs:213`, gate 63, direction 5). The way back to the Lost Tower is the box just north of it, (14-16, 12) (`:527`, gate 64 → Lost Tower 7's (17-19, 249)). Floor plates lie round it.
- **The north road** east along y 15-45 to x 95, then a loop down to y 90 and back west: Alquamos and Mega Crust country.
- **The long stem** south from (25, 60) to (40, 240), widening at y 120-240: Phantom Knights, Drakans, Alpha Crusts, then Great Drakans thick at the south end.
- **The south end**, (25-60, 200-245): the floating pillars cluster here, and the Dark Phoenix holds (34, 238).

---

### 2. MuMain's special cases for WD_10HEAVEN

A grep for `WD_10HEAVEN` gives 74 hits; `WorldActive == 10` never occurs. Every hit is below.

**Frame, sky, fog**
- **Clear colour navy (3, 25, 44)/256** (`SceneManager.cpp:385-386`). Fog colour is set to the same but fog is off in the default camera (`MainScene.cpp:640`).
- **No terrain** (`MainScene.cpp:463`). No grass, no water slide, no caustics, nothing the terrain path does.
- **Object culling is looser**: `range = -10` widens the 2D frustum test for every object block, so objects 10 units further out are kept (`ZzzObject.cpp:3301-3305, 3430, 3534-3538, 3624`). With nothing under them, a pillar that pops is visible against the sky.
- **No projected shadows** anywhere (listed above).

**The clouds** (RenderObjectVisual, `ZzzObject.cpp:3090-3169`; particle `ZzzEffectParticle.cpp:3103-3117`, `:7911-7938`, `:9169-9200`)
- Types 0-5 (Object01-06) are emitters. On the first frame one is drawn: 20 (types 0-2) or 10 (types 3-5) BITMAP_CLOUD particles of subtype = type, light (0.1, 0.1, 0.1), then `HiddenMesh = -2`.
- Each puff: scattered ±2.5 m in x and y and 0.2-0.4 m up from the emitter; scale 1.8-2.0 (so 4.6-5.1 m across); a random phase.
- It stays put, bobbing `sin((WT + phase)/5000) × 20` cm. It turns: subtypes 1 and 4 one way, 2 and 5 the other, 0 and 3 alternating by particle index, at `WT × 0.02 × (scale + 0-0.3)` degrees.
- Its LifeTime is reset to 50 while its emitter is visible; 50 frames after the emitter leaves the view the puff dies and the emitter is un-hidden, so it throws again when seen. The 3000-particle cap (`_define.h:460`) bounds the total; 335 emitters would ask 3,800 if all were in view.
- Additive (GL_ONE, GL_ONE): white puffs at 0.1 over navy, so one puff is a faint veil and the road's dense overlap builds to a bright cloud bank.

**Thunder and light** (MoveObjectSetting, `ZzzObject.cpp:4344-4363`; MoveHeavenThunder `:4254-4335`; MoveObjectOnEffect `:4381-4397`)
- 1 frame in 10: a BITMAP_LIGHT effect at a random point within ±25 m of the hero and **10 m under him**: glints deep below.
- 1 frame in 50: AddTerrainLight of L × (0.3, 0.3, 0.081), L 0.2-0.35, radius 2 at a point ±1.5 m from the hero; and MODEL_CLOUD (`cloud.bmd` at scale 10, additive, at that light) under him for 2 frames: **a pale yellow flash in the cloud sheet**.
  - 1 in 5 of those: two `BITMAP_JOINT_THUNDER + 1` bolts between two points 4-15 m off at one of four fixed headings, 3 m under the hero (`:4280-4334`).
- For one random visible cloud emitter, 1 frame in 10: a `BITMAP_CLOUD + 1` sprite (`cloudLight.jpg`, a violet-white lightning-lit cloud edge) in a random dim colour, and two `BITMAP_JOINT_THUNDER` subtype 6 crackles at it: **lightning flickering inside the clouds**.
- Sounds for these are commented out (`SceneManager.cpp:888-895`).
- Effect sheets loaded for the map only: `Effect/clouds.jpg` → BITMAP_CLOUD, `Effect/cloudLight.jpg` → BITMAP_CLOUD + 1 (`MapManager.cpp:150-155`).

**Objects**
- **Object11's motes** (`ZzzObject.cpp:3171-3178`): every frame (FPS-checked), a white BITMAP_LIGHT particle at bone 3. It rises 2.5 units a frame with the global particle wind and a small random walk (`ZzzEffectParticle.cpp:7941-7960`). With 180 placements, the balustrade smokes white sparks into the sky.
- Types 6-9 and 11-15 have empty arms (`:3179-3193`), and MoveObject's `WD_10HEAVEN` arm is empty (`:4182-4183`): **no BlendMesh, no UV scroll, no StreamMesh, no light** on any Icarus object.
- Dropped items bob ±10 cm in the air (`ZzzObject.cpp:6497-6500`).

**Weather** (`ZzzEffectFireLeave.cpp`)
- Icarus is in RequireLeavesEffect (`MainScene.cpp:85`) and rains always: RainTarget 100, so every one of the 80 slots is BITMAP_RAIN (`:246-270`, `:431-434`, `:456-459`).
- A drop starts at hero ±8 m (x), −5 to +9 m (y), **+2 to +4 m** (z), tipped 30°, falling 0.2-0.43 m a frame (MU's 25 fps). It dies on the 2.85 m plane with **no ripple** (`:369-382`).
- Drawn as a 1 × 20 unit plane, alpha-blended (`:525-527, 604-613`), World1's `rain01.tga`.

**Boids** (`GOBoid.cpp`)
- 13 slots (`:1236-1241`; 5 elsewhere).
- **Slots 0-2: dragons.** `MONSTER_MODEL_DRAGON` = Monster32 (`_enum.h:4181`), scale 0.3-0.4 (a 2-2.7 m red dragon), at hero ±20 m and **6 m under him**, playing action 7 (`MONSTER01_DIE + 1`) at half speed, flying a slow arc and retired past 40 m (`:826-849, 1389-1394, 1180-1186`). Lit by (0.02, 0.05, 0.15) in Icarus (`ZzzObject.cpp:487-492`): **a dark blue silhouette under the cloud**. No sound outside the Golden event.
- **Slots 3-12: wind wisps.** MODEL_SPEARSKILL boids on the ground AI, never drawn as models (`GOBoid.cpp:1530-1533`), each trailing a SPEARSKILL joint subtype 1: 30 tails of `JointSpirit` in grey (0.2, 0.2, 0.2), additive (`ZzzEffectJoint.cpp:1562-1567`). Faint streaks weaving round the hero.

**Characters and mounts** (most arms shared with Tarkan, `WD_8TARKAN`)
- **Flying:** generic. A hero with wings off a safe tile plays PLAYER_FLY / PLAYER_FLY_CROSSBOW (`ZzzCharacter.cpp:612-623`) and makes no footstep (`:5349-5353`). Icarus has no safe tile.
- **Dinorant flies 90 cm up**, not 30 (`ZzzCharacter.cpp:6387-6390, 11786-11790`; `MM/Camera/DefaultCamera.cpp:539-541`; `WSclient.cpp:1339, 2109, 2182`). It plays its flying set: actions 1/3/5/7 (`GOBoid.cpp:519-586`). It kicks no dust (`:542`). Its ride-run is suppressed (`ZzzCharacter.cpp:584-592`). The Rider skill uses PLAYER_SKILL_RIDER_FLY (`SkillCast.cpp:315`; `WSclient.cpp:4739`).
- **The Horn of Uniria cannot be equipped here** (`MM/UI/NewUI/Inventory/NewUIMyInventory.cpp:351-354`). Wings cannot be taken off unless riding a Dinorant, Dark Horse or Fenrir, nor the mount unless wearing wings (`:1491-1510`). WebZen's server also refuses the transformation rings (`user.cpp:17870-17902`).
- **Elf summons are refused** on Icarus (`MM/GameLogic/Combat/ClassAttack.cpp:120`).
- Two-handed sword runs throw no dust (`ZzzCharacter.cpp:7299, 7702`); Dark Horse and Fenrir make shock-waves off their hooves in the air (`GOBoid.cpp:274-300, 348-372`). Season content.
- The Chaos Card's model wears Wings of Heaven (`ZzzCharacter.cpp:14642`): no Icarus meaning.

**Ambient and music**
- `SOUND_HEAVEN01` = `Data/Sound/aHeaven.wav` (1,188,308 B) loops always (`SceneManager.cpp:885-886`; stopped elsewhere `:960-963`; loaded `ZzzOpenData.cpp:4752`).
- `SOUND_WIND01` is stopped here (`:932-935`).
- **`MUSIC_ICARUS` = `data/music/icarus.mp3` (3,292,999 B) plays always** (`SceneManager.cpp:1054-1059`; `_enum.h:177`).

**Other**
- The town portal works (`PortalMgr.cpp:56`).
- The map name is text 55 (`MapManager.cpp:1742-1745`).
- WebZen's server ties Loch's Feather, the Crest of Monarch and the Dark Horse/Raven souls to Icarus behind config flags (`gObjMonster.cpp:4620, 5469-5500`); OM Season 6 adds a 0.1% feather group (`VersionSeasonSix/Maps/Icarus.cs`). Part A's.

---

### 3. The pipeline route, and what changes for World11

The chain is Tarkan's (`docs/tarkan-port.md` B §3), but **Icarus is the first map with no drawn ground**, so the ground step changes most. From MU2_BGFX/, with `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. tables first. Keys are the server's map number = folder - 1 = 10 (terrain.py is run with folder number 11).
#    pipeline/terrain.py
#      HIDDEN_BY_MAP[10]     = {0, 1, 2, 3, 4, 5}   # the cloud emitters (ZzzObject.cpp:3092-3169), 335 boxes
#      BLEND_MESH_BY_MAP[10] : none (MoveObject's WD_10HEAVEN arm is empty, ZzzObject.cpp:4182-4183)
#      GRASS_BY_MAP[10]      = []                   # no TileGrass .OZT, and no ground drawn
#      OPERABLE_BY_MAP[10]   : none (no CreateOperate arm)
#      WATER_FLOW_BY_MAP[10] : none
#      VOID_BY_MAP[10]       : NOT the void cut. There is no NoGround; the whole ground is undrawn.
#                              Needs a new switch, e.g. GROUND_DRAWN_BY_MAP[10] = False, so the
#                              walkable plane stays for Route and heights but no terrain mesh is
#                              cooked or drawn (Part C). Setting NoGround on every tile would close
#                              the road, since the walker's threshold counts 0x08.
#      LIGHT_DEPTH / LIGHT_CHROMA / FIGURE_LIGHT: decision 4 (MU's grey paint lights objects only)
#      LAVA_SPILL / OPEN / FLOOR / VOID_FILL: none
#    pipeline/index.py
#      GATE_BOXES_BY_MAP[10] = [(14, 12, 16, 12), (14, 13, 16, 13)]
#                              # OM VersionSeasonSix/Gates.cs:527 (enter 64 -> Lost Tower 7) and :213 (landing 63)
#      GATE_BOXES_BY_MAP[4] += [(17, 250, 19, 250), (17, 249, 19, 249)]
#                              # the Lost Tower 7's door (Gates.cs:526, enter 62) and the landing back (:181, exit 65).
#                              # index.py has no map-4 row today; src/sim/gates.cpp has the Lost Tower's others.
python3 pipeline/terrain.py $D/World11 icarus source/world 11
#    expect: 2.85 m flat, 6 slots (TileGrass01 99.7%), 10.7% walkable (7,026 tiles, one region x 9-97 y 12-241),
#            0% safe, 0 NoGround, light mean 52 grey, 670 placed / 15 kinds, none missing
#    No tile sheets need decoding while the ground is undrawn. If decision 1 draws something under the road:
for n in clouds clouds2 cloudLight; do
  python3 pipeline/decode_texture.py $D/Effect/$n.OZJ source/textures/ic_$n.png; done
python3 pipeline/decode_texture.py $D/Object11/cloud.OZJ source/textures/ic_cloud.png        # the flash sheet
python3 pipeline/decode_texture.py $D/Object11/top02_R.OZJ source/textures/ic_top02_r.png
python3 pipeline/decode_texture.py $D/Object11/gyg_R.OZJ source/textures/ic_gyg_r.png
#    source/world/icarus/ground.json: no sheet_materials while undrawn
./tools/content.sh --world icarus && ./tools/sync.sh --world icarus --only-world && ./tools/sync.sh
./tools/cook.py --world icarus --only tables
#    then index.py (house rule: build-only skips the index), and every world's tables after new items
# objects: export per kind into source/world/icarus/, sheets ic_-prefixed
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-obj $D/Object11/Object11.bmd source/world/icarus/Object11.obj
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-rig $D/Object11/Object11.bmd source/world/icarus/Object11.rig.json --actions=all
#   rigs for 08, 09, 10 (animated); 11 and 15 carry static bones
./tools/asset.sh icarus/Object11 --build-only && python3 pipeline/index.py source workshop
./tools/cook.py --world icarus --only textures   # textures before --only meshes (house rule)
./tools/cook.py --world icarus --only meshes / placements
```

**Sheet prefix: `ic_`.** Nothing in `source/textures` uses it today. Names that collide without one: `cloud` (generic), `top02_R` (the Lost Tower and others have `top*` sheets), `test12_H`, Effect's `clouds` (Blood Castle's void clouds use their own sheet, `pipeline/cloud_sheet.py`).

**What else differs from the worlds already built:**
- **No ground mesh at all** (above). Every placement stands three metres under the walking plane, so:
  - `FLAT_OVER_VOID` (`tools/cook.py:1233`) must include `icarus`, or the buried test flags every object (their feet are 3 m under the "ground").
  - `GROUNDED_TYPES` must stay empty: grounding would lift the ruins onto the invisible plane.
  - The figure's light: MU lights a character by the TerrainLight under it (BodyLight). Icarus's paint is grey 50 off the strip and white 180-255 along the west stem; it would flicker a walker from dark to bright as he crosses its blots. Decision 4.
- **Hidden types survive as emitters:** 335 cloud anchors, a new `ANCHOR_KINDS_BY_WORLD["icarus"]` kind (e.g. 12, "cloud bank", no light) for a new `game/world/cloud_road.*` or an extension of `void_clouds`. They carry the road's whole look.
- **Object11's 180 mote streams**: another anchor-like effect on a placed, visible mesh (like Tarkan's CHROMED pass, a per-kind flag). The mote is `BITMAP_LIGHT` subtype 0.
- **cook.py tables:**
  - `AIRS["icarus"] = "Dragon01"` from `Monster/Monster32.bmd` (not in `source/monsters`; MuMain loads it from Monster, not Object11). Its tint (0.02, 0.05, 0.15) and depth (6 m under) are the world's, beside it in `game/world/boids.h`.
  - `CRAWLS`: none (the wisps are joints, not models).
  - `LIFTED_BY_WORLD`: none.
- **Rigged placements: 39** (Object08-10), the fewest of any map.
- **Additive kinds:** none on meshes. Object11's statue card `gyg_R` is a .jpg on black drawn opaque; over navy its black box nearly vanishes, but in our lit frame it will show as a black plate. Candidate for additive (ours, marked) or an alpha cut.
- **Code that says Icarus is absent:** `src/game/pets.h:43` ("Icarus is not in this game") gates the Dinorant's 90 cm flight to Tarkan; `src/game/ui/travel.cpp:238` lists Icarus (10) as coming.
- **Recipe priority by placement count:**
  1. Object11 (180, the balustrade; the map's silhouette)
  2. Object15 (39 stair-platforms)
  3. Object08-10 (39 floating pillars, rigged)
  4. Object13 (31 temples)
  5. Object07/16 (35 floor plates)
  6. Object14 (11 shards)
  7. `cloud` (the flash; only if decision 2 keeps it)

  Object01-06 need no mesh. All six models share one sheet, `top02_R` (a white-to-black vertical gradient with frost specks): one material recipe covers the whole map.

---

### 4. Monster assets

OM `Version095d/Maps/Icarus.cs`:
- Spawns: `:55-118` (64, single points). Breeds: `:121-418`.
- The map's respawn town is the Lost Tower (`:44`). The map requires `CanFly` (`:47-50`).
- 0.75 has no Icarus; OM's 075 set stops at Atlans. WebZen's 0.97d has it.

| # | breed | spawns | where |
|---:|---|---:|---|
| 69 | Alquamos | 11 | north road |
| 70 | Queen Rainer | 6 | north road, the loop |
| 71 | Mega Crust | 10 | north road, the loop |
| 72 | Phantom Knight | 12 | the stem, a few at the south end |
| 73 | Drakan | 6 | the loop and upper stem |
| 74 | Alpha Crust | 6 | upper stem |
| 75 | Great Drakan | 12 | the south end |
| 77 | Dark Phoenix (boss) | 1 at (34, 238) | the south end |
| 76 | Dark Phoenix Shield | 0 | the boss's first form, unspawned in OM's 0.95d |

The body file is `Monster{MONSTER_MODEL + 1:02}.bmd` (`MM/Engine/Object/ZzzOpenData.cpp:2561`; `MONSTER_MODEL_ALQUAMOS = 50` … `DARK_PHOENIX = 56`, `_enum.h:4200-4206`). CreateMonster arms are `ZzzCharacter.cpp:13573-13647`; RenderCharacter extras `:8792-8920`. MuExtract output is in `S/mon/`. **None of these bodies is in `source/monsters`**, nor Monster32. Every breed's death plays at 0.22 (`ZzzOpenData.cpp:3740-3790`). **No monster here casts a shadow** (`ZzzCharacter.cpp:8664-8670`).

| # | breed | file | scale | arms / extras | tris, bones, keys; sheets | MU2_BGFX |
|---:|---|---|---:|---|---|---|
| 69 | Alquamos | **Monster51** | 1.0 | no weapon. BlendMesh 0. **9 white-blue light sprites on its "star" bones** and 3 sparks a frame off random bones (`ZzzCharacter.cpp:8792-8814`). Attack: 4 BITMAP_FLARE joints at the target (`:2140-2148`) | 794; 69 bones; 6/6/7/7/7/7/7; starp00 (teal star-field) | **missing**. A dark teal starry wraith with tattered wing-sails, 2.6 m |
| 70 | Queen Rainer | **Monster52** | 1.3 | no weapon. **BlendMesh −2 at light 1: drawn whole additive**, no shadow (`:13579-13586`). A light sprite at bone 20 (`:8815-8818`). Attack: 20 BITMAP_BLIZZARD at the target (`:1681-1696`) | 919; 57; kuo00/01/02 (violet) | **missing**. A violet winged woman, 3.1 m, floating 0.7 m |
| 71 | Mega Crust | **Monster53** | 1.1 | **Thunder Blade** (Sword19, +5) and **Legendary Shield** (Shield15). BlendMesh 1 at light 1 (mesh 1, `iui02` flame, additive). A **physics cloth cape** (`iui02.tga`), CPhysicsCloth on bone 19 (`:8820-8838`). Inferno on attack (`:1698-1708`) | 964; 55; iui01 (.tga, alpha), iui02 | **missing**. A winged armoured knight with a red banner-wing |
| 74 | Alpha Crust | Monster53 | 1.3 | Thunder Blade +9, Legendary Shield +9; cape `iui03.tga` (teal); RENDER_EXTRA second pass (`:8645`) | — | re-dress of Mega Crust |
| 72 | Phantom Knight | **Monster54** | 1.45 | **Dark Breaker** (Sword18, +5) (`:13613-13618`). Boss-skill arm only (`:1710-1717`) | 1238; 67; lpo01 (.tga, alpha) | **missing**. A spiky black winged knight |
| 73 | Drakan | **Monster55** | 0.8 | no weapon. Meshes 1-2 additive, 0/3/4 not (`:13621-13634`). **Blue light sprites along bones 13-26 and 52-58, blue lightning between them** (`:8840-8865`); body drawn again chrome-bright and chrome2-lightmap (`:8882-8888`). Attack 1: Inferno; attack 2: a piercing bolt + thunder joint (`:2123-2134`) | 2040; 59; 13/13/13/7/7/7/7; drt00-05 (dark blue scales, gold eye) | **missing**. A long black-blue serpent dragon |
| 75 | Great Drakan | Monster55 | 1.0 | as Drakan, but **fire at bone 18** instead of the blue lights, chrome-bright RENDER_EXTRA (`:8867-8879, 8645`) | — | re-dress of Drakan |
| 77 | Dark Phoenix | **Monster56 + Monster57** | 1.0 | Drawn as MODEL_DARK_PHEONIX_SHIELD (Monster56, the **fire phoenix**, `Pnix`, StreamMesh 0: its flame sheet scrolls) with **Monster57 (the dark rider, `magic_BB`)** drawn on the same skeleton (`o->Type++`), a chrome-bright pulse over it and meshes 2-3 additive at a breathing light (`:8890-8912`). A physics-cloth mane `magic_H.tga` (`ZzzOpenData.cpp:5292`). Attack: piercing bolt + thunder joint (`:2283-2295`); boss skill (`:1785-1790`) | 1054 + 646; 61 + 48; 8/8/8/7/7/4/19 | **missing**. A dark winged rider on a 9.5 m flaming bird |
| — | Red Dragon (boid) | **Monster32** | 0.3-0.4 | sky boid only (§2). Action 7 | 1100; 94; 15/16/9/14/14/14/21/15; drr_01-03 | **missing** (`BudgeDragon01` is Monster03, not this) |

- **To import:** Monster51-57 (7 bodies), Monster32 (the boid), Sword18 (Dark Breaker) and Sword19 (Thunder Blade). Plus figure rows for Alpha Crust (cape iui03, +9 arms, extra pass) and Great Drakan (fire, extra pass).
- Already in hand: **Shield15** (Legendary Shield, `source/items/shields/Shield15.json`). The Tarkan five are now built (`IronWheel01`, `Tantalos01`, `Zaikan01`, `BloodyWolf01`, `BeamKnight01`, `DeathBeamKnight01`, `Mutant01`) but none appears here.
- Needs `figures_icarus.json`, and the monster sheet sets decoded under per-monster prefixes, as Atlans and Tarkan did (`alquamos_`, `rainer_`, `crust_`, `phantom_`, `drakan_`, `phoenix_`, `dragon_`).
- **Two physics cloths** (the Crusts' capes, the Phoenix's mane): MU2_BGFX has no cloth. Candidates: a rigged card swung by the nearest bone, or drop (decision 6).
- **Five of eight forms draw additive passes** (Queen Rainer whole, Crust mesh 1, Drakan meshes 1-2 plus chrome, the Phoenix's flame). Icarus's bestiary is the glowiest yet. Keep it subtle, per "monster auras are subtle".

---

### 5. Sounds and music

| file | bytes | in `source/sounds`? | where MuMain plays it |
|---|---:|---|---|
| Sound/**aHeaven.wav** | 1188308 | **no** | `SOUND_HEAVEN01`, ambient loop always (`ZzzOpenData.cpp:4752`; `SceneManager.cpp:885-886`) |
| aThunder01-03 | 64830 each | no | `SOUND_THUNDERS01-03`, loaded (`ZzzOpenData.cpp:4753-4755`); **commented out on Icarus** (`SceneManager.cpp:888-895`). We have our own `thunder1-7.wav` for the rain sheets |
| mAlquamosAttack1, mAlquamosDie | 148080, 137616 | no | Alquamos (`ZzzOpenData.cpp:3754-3760`; idle uses the attack sound, `mAlquamos1.wav` 201336 is commented out) |
| mRainner1, mRainnerAttack1, mRainnerDie | 335836, 20182, 63070 | no | Queen Rainer (`:3761-3767`) |
| mMegaCrust1, mMegaCrustAttack1, mMegaCrustDie | 22062, 69988, 47962 | no | both Crusts (`:3738-3746`) |
| mPhantom1, mPhantomAttack1, mPhantomDie | 5746, 125538, 311340 | no | Phantom Knight (`:3768-3774`) |
| mDrakan1, mDrakanAttack1, mDrakanDie | 62428, 14092, 228982 | no | both Drakans (`:3775-3781`) |
| mPhoenix1, mPhoenixAttack1 | 215314, 132234 | no | Dark Phoenix (`:3782-3788`; no death sound, `-1`). `mPhoenix2.wav` 96740 and `ePhoenixExp.wav` 26270 ship but this path does not load them |
| Music/**icarus.mp3** | 3292999 | no | `PlayMp3(MUSIC_ICARUS)` always on the map (`SceneManager.cpp:1054-1059`) |

That is 16 monster wavs, the ambient bed and one mp3. There are no footsteps while flying (`ZzzCharacter.cpp:5349-5353`) and no wind loop (`SOUND_WIND01` stopped). New sounds go through `index.py` and then `tools/cook.py --only showing` (house rule).

---

### 6. A look direction

**What MuMain draws:**
- a deep navy void, no ground
- a road of soft white additive cloud, turning and bobbing slowly
- along its edges, 180 pillar-arches white at the crown and fading to black at the foot, each crowned with a small winged statue and streaming white sparks upward
- stone stairs, plates and shards two to three metres below the cloud; domed shrines in miniature further out in the dark
- steady rain; silent lightning flashing in the cloud under your feet and crackling inside cloud banks
- three dark dragons gliding far below; grey wind wisps round the hero
- no shadows anywhere

**It should read as a night above the clouds: a cold, moonlit sky-road, bright where the cloud is and deep blue everywhere else, never black and never foggy.**
- This is the one map where the house's "Lorencia night with a little sepia" fits literally: a night sky. But MU's colour is navy (0.012, 0.098, 0.172), not black. Keep it blue so the clouds read as cloud and not smoke.
- The cloud road is the floor the eye trusts. It must be bright enough that the hero clearly stands on something: the clouds' sum, not a ground mesh, carries the frame's exposure.
- "Not too dark": the moon must light the tops of the pillars and the cloud's upper faces; the depth below falls off to navy, not to black.
- "Not foggy": no depth haze over the road. The sense of depth comes from things far below (ruins, dragons, glints) getting dimmer and bluer, a `dust` trace in navy at most.

**What the engine's sheet must carry:**
- **A moon, not a sun.** Cool white-blue key at the house 52°, strength well under Tarkan's noon (around Lorencia's night key). Soft shadows; MU draws none, so they matter only where the pillars throw them across the cloud (they cannot fall on an undrawn ground; decision 3).
- **A navy sky fill and a cloud bounce from below.** The sky colour is MU's navy brightened enough to be a colour, not a hole. The ground bounce is the cloud's pale grey-blue, because the light under a pillar comes up off the cloud.
- **Background = sky colour.** There is no ground to hide it, so the clear colour is the frame. A `clear_colour` or a gradient (navy above, a paler blue toward the horizon) is a new knob (Part C); today's sheets have no such key.
- **Grade:** shade cool blue, light a touch warm (the house's sepia goes in `tint_high`, per "sepia is a hue problem"), saturation under 1 so the navy does not go electric (Devias's cobalt lesson), contrast moderate.
- **Bloom** a little above other maps' for the additive cloud and motes, threshold high enough that the road does not wash to white.
- **Rain faint** ("rain faint"; Noria's sheet). MU rains here always; ours a thin drizzle at most, or the `_rain` sheet only.
- **Lightning subtle and rare**, silent or with a distant roll; the flash a short brightening in the cloud, not a frame-wide strobe.
- **No grass** (`grass` 0).

**Proposed starting `sheets/worlds/icarus.json`** (shape from tarkan.json, atlans.json and devias.json; all values invention for the first shots):

```json
{
  "note": "Icarus's light, laid over lighting.json while a character stands in Icarus (game/world/maps.h). MU draws no terrain here (MainScene.cpp:463): the road is additive cloud puffs (ZzzObject.cpp:3090-3169, ZzzEffectParticle.cpp:3103-3117) over a navy clear colour, (3, 25, 44)/256 (SceneManager.cpp:385-386), lined by 180 pillar-arches white at the crown and black at the foot, with no shadow anywhere and steady rain and silent lightning. So: a night above the clouds, the house's moonlit night taken literally. A cool white-blue moon at the house 52 degrees and soft; a navy sky fill brighter than MU's colour, so the void reads as sky and not as a hole; the bounce from below in the cloud's pale blue-grey, since the light under a pillar comes up off the cloud; exposure set by the cloud road, which must read as a floor; a trace of navy dust for depth only, no veil over the road; the shade cool, the light a touch warm (the house's little sepia, in the tints), saturation under 1 so the navy does not turn electric; bloom a little up for the cloud and the motes. No grass, no water. Invention on MU's look; first cut 2026-10-06, unshot.",
  "elevation": 52.0,
  "sun_colour": [0.78, 0.86, 1.0],
  "sun_strength": 2.2,
  "sky_colour": [0.1, 0.2, 0.36],
  "horizon_paleness": 0.6,
  "ground_colour": [0.42, 0.46, 0.55],
  "ambient_strength": 1.0,
  "exposure": 1.35,
  "dust_colour": [0.08, 0.16, 0.3],
  "dust_density": 0.0015,
  "lamp_strength": 3.14159,
  "glow_strength": 1.3,
  "bloom_threshold": 1.0,
  "bloom_strength": 0.35,
  "contrast": 0.45,
  "saturation": 0.88,
  "split": 0.35,
  "tint_low": [0.92, 0.97, 1.08],
  "tint_high": [1.06, 1.02, 0.94],
  "grass": 0.0,
  "water_flow": 0.0,
  "water_sheen": 0.0,
  "probe": 0.0,
  "tonemap": 1,
  "ssao_strength": 0.8
}
```

- A `clear_colour` / sky gradient knob (the frame's background) does not exist yet: Part C. Proposed start (0.03, 0.09, 0.2) at the zenith to (0.12, 0.2, 0.34) at the horizon.
- A cloud-road layer (MU's 335 emitters, or `void_clouds` re-aimed to lie under the walkable band instead of over NoGround) does not exist yet: Part C. Its strength is the map's exposure.
- An `icarus_rain.json` weather blend (rain and lightning, as `tarkan_rain.json` carries the storm) is the natural home for MU's always-on rain, at the user's "faint".

Expect the first-shot fights to be:
1. whether the hero reads as standing on the cloud or floating over nothing (the cloud layer's height and density against the invisible 2.85 m plane)
2. the void: navy enough to be sky, dark enough to be night
3. the pillars' black feet, which over navy read as holes in the sky
4. how much of MU's glow (cloud, motes, five additive bestiaries, lightning) is "subtle"

**House rules applied:**
- **Music is rare and in fights.** MuMain's always-on `icarus.mp3` becomes at most a fight track; `aHeaven.wav` is the bed.
- **Subtle effects:** the motes thin and slow; the lightning rare; the dragons far and dark, as MU has them; the wisps faint.
- **Runs muted and judged in game by the user.** Lorencia-only audits do not apply here (a new world).

**Decisions for the user:**
1. **The ground:** MU draws none (cloud road over navy). Keep that, with a cloud layer as the floor; or draw a cloud-textured ground under the road (ours); or both.
2. **The cloud road:** MU's 335 emitters and their 3,800 puffs (additive billboards), or our `void_clouds` decks laid under the walkable band, or both.
3. **Object heights:** MU stands every object 3 m under the walkers. Keep MU's (the ruins seen through cloud, the walkers on the cloud), or raise the road's stones (Object15/14/07/16) to the walking plane so the hero walks on stone (ours).
4. **Figure light:** MU's grey TerrainLight (white along the west stem, grey 50 elsewhere, with object-shadow blots) lights the walkers; take it, soften it (FIGURE_LIGHT), or ignore it and light by the sheet alone.
5. **Rain and lightning:** MU rains always and flashes silently; how often, how faint, with or without thunder.
6. **The Crusts' capes and the Phoenix's mane** (physics cloth): a swung card, or leave them off.
7. **Music:** whether `icarus.mp3` plays in fights at all.
8. **Entry:** MU's rule (wings 12,0-6 or a Dinorant; no Uniria, no transformation ring), and what happens to a hero who loses his wings inside (Part A).
9. **The grid:** the client's (OM's adds 369 corrupt tiles in rows 157-255). Recommend the client's.

---

---

## Part C: the engine side (Icarus)

Written 2026-10-06, read-only: no repo file was edited, no cook was run, no window was opened, no stash was made, and mu.db was read with `sqlite3 -readonly` only. Paths are relative to `MU2_BGFX/` unless marked. Abbreviations, as in tarkan-port.md Part C:
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`;
- MM = `LEGACY/reference/MuMain/src/source/`;
- D = `LEGACY/reference/MuMain/src/bin/Data`;
- WZ = the 99z repack data an earlier session extracted (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/5770e6b4-410c-4720-aca8-388135fec063/scratchpad/wz/Data`, cp949, not in the repo). The 2012 repack, not WebZen's official data; read its numbers with care.
- WZS = WebZen GameServer 1.00.93's partial source an earlier session saved (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9142efc4-e29e-4d88-9ee6-bb4831450426/scratchpad/wz/Source/Server Side/GameServer/`: `user.cpp`, `protocol.cpp`, `gObjMonster.cpp` only). Icarus there is `NEW_SKILL_FORSKYLAND` and `MAP_INDEX_ICARUS`.

Line numbers are HEAD `ecf5089e` plus the working tree as found. `git status` in MU2_BGFX at the start: modified `docs/tarkan-quest.md`, `source/npc/NpcSenatus.json`, `tools/cook.py` (+7), `tools/voice.py` (+8); untracked `source/voice/keeper/`, `keeper_1/`, `lirien_2/`, `source/voice/ref/keeper_cv_eighties.wav` and `source/world/tarkan/placements.json`. Another session is live: `source/mu.db-shm` was touched at 17:03 today, the WAL is 0 bytes. Any Icarus step that touches `tools/cook.py` must read `git diff` first (memory: another session edits the same files).

Template: tarkan-port.md Part C (`docs/tarkan-port.md:1472-1923`), checked against what Tarkan's build really needed (its "Where it stands", `:16-91`, and "Traps", `:183-205`) and atlans-port.md Part C (`:1281-1791`).

Scratch, in `c/` beside this page:
- `c/icarus/`: `pipeline/terrain.py D/World11 icarus c 11`, run read-only into scratch (attributes.png, height.png, tiles.png, light.png, icarus.json); its log is `c/terrain_log.txt`;
- `c/grid.py`: the regions, the spawn check, the gate boxes and the walking distances below;
- `c/mm_heaven.txt`: all 61 `WD_10HEAVEN` references in MuMain;
- `c/grep_worlds.txt`: the per-world name grep (116 hits).

---

### 0. The record the engine has to carry (short; pass A owns it)

**Map and folders.**
- Icarus is map **10** (`OM/Version095d/Maps/Icarus.cs:20`) and MuMain's `WD_10HEAVEN` (`MM/World/MapInfra/MapManager.h:19`). WebZen's own name for it is the "sky land" (`NEW_SKILL_FORSKYLAND`). Its folders are `D/World11` + `D/Object11` (terrain.py map_number 11).
- **It is not 0.75.** OM has it from Version095d, whose `Gates.cs` has **no Icarus gate**. Its gates are Season Six's and WZ's (below).
- `D/World11` holds almost nothing a ground needs: `TileGrass01.OZJ`, `leaf01.OZT`, `leaf02.OZJ`, `rain01.OZT`, `TerrainLight.OZJ`, `TerrainHeight.OZB`, the attribute and map files and a minimap. **MU never draws Icarus's terrain**: `RenderGameWorld` skips `RenderTerrain` for `WD_10HEAVEN` (`MM/Scenes/MainScene.cpp:463`). What a player stands on are the island objects.
- `D/Object11` holds Object01-16 (no Object12), `cloud.bmd` + `cloud.OZJ`, and four more sheets (`gyg_R`, `test12_H`, `top02_R`, `top03_B`, `top04_B`). `MapManager.cpp:150-155` loads `Effect/clouds.jpg` (BITMAP_CLOUD), `Effect/cloudLight.jpg` (BITMAP_CLOUD + 1) and `Object11/cloud` (MODEL_CLOUD).
- Sound and music: `D/Music/icarus.mp3` (**already in `assets/music/icarus.mp3`**) on the whole map (`MM/Scenes/SceneManager.cpp:1054-1059`); `D/Sound/aHeaven.wav` looped (SOUND_HEAVEN01, `SceneManager.cpp:885-895`, stopped elsewhere `:960-963`; loaded `MM/Engine/Object/ZzzOpenData.cpp:4752`). The thunder lines beside it are commented out. Monster sounds (`ZzzOpenData.cpp:3754-3790`): mAlquamos1/Attack1/Die, mRainner1/Attack1/Die, mMegaCrust1/Attack1/Die, mPhantom1/Attack1/Die, mDrakan1/Attack1/Die, mPhoenix1/2/Attack1, ePhoenixExp. None is in `source/sounds/` yet.
- Clear colour: **blue, rgb (3, 25, 44)/256** (`SceneManager.cpp:385-386`), the only map but Devias with a coloured clear.

**Gates.** Season Six (`OM/VersionSeasonSix/Gates.cs`) and WZ `Gate.txt:109-113` agree on the boxes:

| gate | map | box | dir | role | level OM S6 / WZ | source |
|---:|---|---|---|---|---|---|
| 62 | Lost Tower 7 | 17,250-19,250 | -- | enter, to 63 | **160** / 160 | S6 `:526`; WZ `:109` |
| 63 | Icarus | 14,13-16,13 | 5 E (+1,+1) | arrival from LT7 | -- | S6 `:213`; WZ `:110` |
| 64 | Icarus | 14,12-16,12 | -- | enter, to 65 | **50** / 80 | S6 `:527`; WZ `:112` |
| 65 | Lost Tower 7 | 17,249-19,249 | 1 W (-1,-1) | arrival from Icarus | -- | S6 `:181`; WZ `:113` |

There is **no spawn gate and no safe zone** in Icarus. Warp: S6 "Icarus", 10 000 zen, level 170, to gate 63 (`OM/VersionSeasonSix/Gates.cs:69`); WZ `MoveLevel.txt` 170.

**Entry.** One must fly:
- OM 095d: `CreateRequirement(Stats.CanFly, 1)` (`Icarus.cs:46-50`). CanFly comes from any wing (`OM/Version095d/Items/Wings.cs:114-116`) or a worn Dinorant (`OM/CharacterClasses/CharacterClassInitialization.cs:158`).
- WZS `gObjMoveGate` (`user.cpp:27201-27220`) refuses map 10 unless a Dinorant (13/3), cape, Fenrir or Dark Horse is worn **or** any wing is, and refuses outright if the **Horn of Uniria** (13/2) or a **Transformation Ring** (13/10, 13/39) is worn.
- In Icarus, WZS refuses to take off the Dinorant without a wing on, or the wing without a Dinorant on (`protocol.cpp:5356-5375`, `user.cpp:16879-16925`). If the Dinorant breaks and no wing is worn, he is sent to **Devias gate 22** (`user.cpp:10812-10842`). No summoning there (`user.cpp:29567-29571`; the client's own refusal `MM/GameLogic/Combat/ClassAttack.cpp:120`). The client will not equip Uniria there (`MM/UI/NewUI/Inventory/NewUIMyInventory.cpp:351-354`).

**Death.** WZS: a death on map 10 rises in **Devias** (`user.cpp:22077-22081`). OM 095d: `SafezoneMapNumber => LostTower` (`Icarus.cs:44`). The Town Portal is allowed (`MM/World/MapInfra/PortalMgr.cpp:56`).

**Monsters.** OM 095d `Icarus.cs` has **64 one-tile spawns plus the boss** (`:53-117`), definitions `:122-418`:

| # | breed | count | level | HP | skill | respawn | MU model |
|---:|---|---:|---:|---:|---|---|---|
| 69 | Alquamos | 11 | 75 | 11 500 | Energy Ball | 10 s | Monster51 @1.0 |
| 70 | Queen Rainier | 6 | 82 | 19 000 | Energy Ball | 10 s | Monster52 @1.3, all added |
| 71 | Mega Crust | 10 | 78 | 15 000 | -- | 3 s | Monster53 @1.1, Thunder Blade +5, Legendary Shield |
| 72 | Phantom Knight | 12 | 96 | 41 000 | MonsterSkill | 3 s | Monster54 @1.45, Dark Breaker +5 |
| 73 | Drakan | 6 | 86 | 29 000 | MonsterSkill | 20 s | Monster55 @0.8 |
| 74 | Alpha Crust | 6 | 92 | 34 500 | -- | 3 s | Monster53 @1.3, Thunder Blade +9, shield +9 |
| 75 | Great Drakan | 12 | 100 | 50 000 | MonsterSkill | 3 s | Monster55 @1.0 |
| 76 | Dark Phoenix Shield | (0) | 106 | 73 000 | Lightning | 10 s | none drawn |
| 77 | Dark Phoenix | 1 | 108 | 95 000 | MonsterSkill | 15 s | Monster56 + Monster57 |

Models and scales are MuMain's `CreateMonster` (`MM/Engine/Object/ZzzCharacter.cpp:13573-13650`). 76 is never placed: WZS raises it beside every 77 and links them (`user.cpp:4953-4980`). WZ `Monster.txt` gives the Phantom Knight level 98, the Shield 83 000 HP, the Phoenix RegTime **1800 s** and resistances 9-35; its `MonsterSetBase.txt` places a different set on map 10 (12 Alquamos, 8 Rainier, 11 Mega Crust, 13 Phantom, 7 Drakan, 8 Alpha, 13 Great Drakan, 2 of kind 67, and no 77). Pass A chooses.

#### 0.1 The grid, from the scratch extraction (`c/icarus/attributes.png`, read [y,x])

- terrain.py: **height 2.85 m on every tile** (one value; the header's pixel offset is 54, read at 1080 as the client does), TileGrass01 on 99.7%, **10.7% walkable (7 026 tiles), 0.0% safe**, 670 placements of 15 kinds, no model missing.
- **No tile carries NoGround (0x08).** The words are 4 (NoMove) on 58 510 tiles, 0 on 7 025 and 2 on one. The void is NoMove, not NoGround, so the engine's chasm machinery (abyss, void clouds, the ground's undrawn quads) sees **no void at all** today.
- **Walkable ground is one 8-connected region** of 7 026 tiles, x 9-97, y 12-241: a long chain of islands down the west third of the map. Every OM spawn stands on it, none on a closed tile.
- Gate 63's box (14-16,13) is 3 of 3 open; gate 64's (14-16,12) 3 of 3. The arrival is **15,13**.
- Walking distance from 15,13: Alquamos 14-65 tiles, Mega Crust 38-89, Queen Rainier 74-109, Drakan 99-143, Alpha Crust 137-163, Phantom Knight 165-278, Great Drakan 214-290, **the Dark Phoenix at 34,238, 290 tiles**. The breeds stand in bands down the chain, weakest at the door.
- **The Lost Tower side.** `source/world/losttower/attributes.png`: gate 62 (17-19,250) and gate 65 (17-19,249) are all open, word 0; the posts at x 15 and 21, rows 248-250, are closed (lost-tower-port.md `:387`). LT7's arrival 8,86 reaches the door in **226 tiles**.

---

### 1. How a world is registered and loaded today: the checklist for map 10

`MapRow` is still not widened (`src/game/world/maps.h:18-34`: world, number, arrive, underground, home, event), and the air is now five name-keyed bools (`src/game/play_open.cpp:33-42`). The name is `icarus`. The pipeline's keys are MU's map number (terrain.py's `map_number - 1`): Icarus is **10**.

#### 1.1 Pipeline (source side)

| # | where (file:line) | Icarus needs |
|---|---|---|
| P1 | `pipeline/terrain.py main` (`:635-`) | `python3 pipeline/terrain.py $D/World11 icarus source/world 11`. The scratch run above: flat 2.85 m, 10.7% walkable, 670 placed |
| P2 | `terrain.py:118-147` `HIDDEN_BY_MAP` | `10: {0, 1, 2, 3, 4, 5}`: RenderObjectVisual's Heaven arm sets `HiddenMesh = -2` on types 0-5 after throwing their clouds (`MM/Engine/Object/ZzzObject.cpp:3090-3169`). Object01-06, 335 placements, every one an emitter |
| P3 | `terrain.py:156-177` `BLEND_MESH_BY_MAP` | none: MoveObject's Heaven arm is empty (`ZzzObject.cpp:4182-4183`); RenderObjectVisual's cases 6-15 are commented out (`:3180-3191`). Pass B owns the look |
| P4 | `terrain.py:179-214` `GRASS_BY_MAP` | `10: []`, said outright, so slot 0's TileGrass01 sows no lawn on a ground nobody sees |
| P5 | `terrain.py:216-259` `VOID_BY_MAP`, `:274` `OPEN_BY_MAP`, `:295` `VOID_FILL_BY_MAP` | new: a **sky** row (§3.1). The cheapest form is a world-json key, `"sky": true`, that terrain.py writes from a `SKY_BY_MAP = {10}` set and the runtime reads (no attribute changes) |
| P6 | `terrain.py:390-397` and `pipeline/index.py:44-51` `OPERABLE_BY_MAP` | none: MOVEMENT_OPERATE has no Heaven arm |
| P7 | `pipeline/index.py:58-78` `GATE_BOXES_BY_MAP` | `10: [(14, 12, 16, 12), (14, 13, 16, 13)]`, and **a map 4 row**, which does not exist yet: at least `(17, 249, 19, 249), (17, 250, 19, 250)` for the LT7 door, and the tower's own stairs while it is open (the fourth Part C to note that 4 and 11 are missing). Then recook **both** worlds' tables |
| P8 | `source/world/icarus/ground.json` -> `tools/content.sh` | **a ground must still be built**: `index.py worlds()` skips a world whose `<name>_ground.glb` is missing (`:1519-1527`), and `Ground::load` needs its surfaces. The cheapest: one sheet for the flat TileGrass01, built and never drawn (P5). Prefix `ic_` if any sheet is named (pass B) |
| P9 | `source/mu.db` `gates` | **no row**: Icarus has no spawn gate. The death landing is `MapRow.home` (§4.4) |
| P10 | `pipeline/index.py worlds()` (`:1503-`) | automatic once the ground is built; run index.py after `--build-only` (memory) |
| P11 | `pipeline/index.py` `EFFECTS` | new rows: `"cloud"` (`Effect/clouds.jpg`, BITMAP_CLOUD) and `"cloud_light"` (`Effect/cloudLight.jpg`). Already there: `joint_spirit` (`:575`), `rain` (`:641`), `void_clouds` (`:337`), `light`. Then `tools/cook.py --only showing` |
| P12 | the effect model `Object11/cloud.bmd` | MoveHeavenThunder's MODEL_CLOUD (§3.4). An `_models(root, build, "icarus")` folder beside `"tarkan"` (`index.py:2057`), if it is built |

#### 1.2 The cook (`tools/cook.py`)

| key | line | Icarus |
|---|---|---|
| `AIRS` | `:202-205` | `"icarus"` only if the dragons are built (§3.4: Monster32, the Red Dragon, flying 6 m under the hero) |
| `CRAWLS` | `:210` | none (MoveFishs has no Heaven arm) |
| `AIRS_FROM` | `:217` | none |
| `FLAT_OVER_VOID` | `:1233` | **add `"icarus"`**: every island stands 0.15-2.85 m under the walk plane (placements at z -285 to -15 against the ground's 285), and a swaying one would read as buried (tarkan-port.md trap "Buried cliffs") |
| `GROUNDED_TYPES`, `LIFTED_BY_WORLD` | `:1243`, `:1270` | **none, and never**: laying an island on the terrain would lift it 3 m. Pass B checks that the islands' tops meet 2.85 m |
| `ANCHOR_KINDS_BY_WORLD` | `:1306-1322` | `"icarus": {"Object01".."Object06": <cloud bank>, "Object11": <spark>}` (§3.3). `ANCHOR_LIGHT` gets a row per new kind |
| `PERCHES` | -- | none |
| `figure_file` | `:1831` | automatic `figures_icarus.json` |
| `FOLK_VERSION075` | -- | none: Icarus has no NPC in any source |
| this map's spawns | `cook_tables` | automatic |
| tables after any item or kind change | (no loop) | now **ten** runs: lorencia, noria, devias, dungeon, losttower, bloodcastle, atlans, charscene, tarkan, icarus (memory: new items need every world's tables) |

#### 1.3 The runtime: every place a world is named (verified against today's code)

| # | file:line | today | Icarus row |
|---|---|---|---|
| R1 | `src/game/world/maps.cpp:19-46` `kMaps` | 8 rows; Tarkan `:39`, Blood Castle `:45` with `home` 2 | `{"icarus", 10, {15, 13}, false, 2}` (decision 3 for the home). **Not underground**: MU rains and blows its leaf slots there (`MM/Scenes/MainScene.cpp:85`), and the room is open sky. `event` false: the save resumes in Icarus (but see §4.4) |
| R2 | `src/game/world/maps.h:18-34` `MapRow` | not widened | the place for `air`, `sky` and `fly` if step 0 is taken |
| R3 | `src/game/ui/arrival.cpp:55-66` `kPlaces` | Tarkan `:64` | `{"icarus", "Icarus", ""}` |
| R4 | `src/game/ui/minimap.cpp:289-301` `mapName` | `case 8` `:297` | `case 10: return "Icarus";`. LT7's door then labels "Gate to Icarus" |
| R5 | `src/game/world/maps.cpp:86-97` `placeName` | tower and Dungeon | nothing (one region) |
| R6 | `src/sim/realm_travel.cpp:18-48` `kRows`; `src/sim/travel.h:30` `kTravels = 15` | Tarkan last `:47` | decision 1: append "Icarus" (`kTravels` 16; u32 `found_`, append only) |
| R7 | `src/game/ui/travel.cpp:238` `kComing` | lists `{"Icarus", 10}` | **the dimmed card vanishes once `mapNumbered(10)` exists and no row names map 10**, the Tarkan trap again |
| R8 | `sheets/worlds/icarus.json` | -- | new, found by name (`maps.cpp` `mapSheet`). Pass B's look, plus the clear colour (§3.1) |
| R9 | `src/game/play_open.cpp:33-42` | `windy_`, `dungeonAir_`, `towerAir_`, `castleAir_`, `waterAir_`, `desertAir_`, `underwater_`, `grassy_`, `snowy_` | `heavenAir_ = world == "icarus"`, and `windy_` false there: MU plays aHeaven.wav, not the wind |
| R10 | `src/game/play_open.cpp:476-480` | air loads | `else if (heavenAir_) heard_.wind = sound_.load("world_heaven", false);` |
| R11 | `src/game/play_sound.cpp:416`, `:424-428` | loop and room | add `\|\| heavenAir_` to the loop; room **Open**. Not underground, so `indoors()` already answers from the roofs (none), but say it outright as Tarkan's arm does |
| R12 | `src/game/world/world.cpp:236-245` leaves | open unless underground | it would open Lorencia's leaves. Icarus wants rain only (§3.5): an arm beside Tarkan's |
| R13 | `src/game/world/world.cpp:95, 104` | Devias floors, Atlans caustic | nothing |
| R14 | `src/game/world/boids.cpp:61-119` `boidOf`, `airsOf` | | the dragons and the spirit wisps, if built (§3.4) |
| R15 | `src/game/world/scurry.cpp:34-45` | | nothing |
| R16 | `src/game/world/weather.cpp:107-131` | Tarkan's steady storm by name | a steady light rain, no rings, no rain or thunder sound (§3.5) |
| R17 | `src/game/world/void_clouds.cpp:109-111` `wanted`, `:139-149` | castle, Dungeon, Tarkan; void = NoGround | the cloud sea, if decision 4 takes ours (§3.1) |
| R18 | `src/game/world/desert_vents.cpp:55` | opens on Tarkan only | opens on Icarus too for the cloud banks and sparks (§3.3), or a sibling `sky_vents` |
| R19 | `src/game/pets.h:44` `dinorantFlies` | `world == "tarkan"` ("Icarus is not in this game") | `\|\| world == "icarus"`: MU lifts him 90 there too (`ZzzCharacter.cpp:6387-6390`, `MM/Camera/DefaultCamera.cpp:539-541`) |
| R20 | `src/app/modes/play_mode.cpp:1011-1023` music | safe-zone themes | Icarus has no safe zone, so this arm never plays it (decision 6) |
| R21 | `src/app/modes/play_mode.cpp:736-752` `takeHome` | `here->home` | automatic with R1's `home` |
| R22 | `src/sim/gates.cpp:9-123` | | §4.1 |
| R23 | `source/mu.db` | | §5 |
| R24 | `tools/bot/bot.cpp:68-74` `kWorlds` | 5 worlds | `{10, "icarus", {15, 13}}` only if bot runs should reach it (it would need wings) |
| R25 | `src/game/headless.cpp:166` | `mapOf(world)->arrive` | automatic |
| R26 | `src/sim/items.h:192` `featherMap` (4, 7) | "this game has no Icarus" (`:185-188`) | decision 8 |
| R27 | `src/sim/items.h:917` `rideMap(uint32_t)` | true everywhere | must know the mount: Uniria refused on map 10 (§3.2) |
| R28 | `src/game/world/{doors,trap_show,lava_smoke,skulls,castle_sparks,drawbridge,portal,bubbles,sand_haze}.cpp` | return early off their worlds | nothing |
| R29 | `src/content/ground.cpp:637-653` world json keys | `void`, `void_floor`, `figure_light`, ... | `sky` (§3.1) |
| R30 | `src/gfx/renderer.cpp:979` | the shade view clears to **0x00000000**, fixed | a clear colour from the sheet (§3.1) |

Load path unchanged: `PlayMode::travel` -> `World::open` -> `Play::open` (`play_open.cpp:24-`, `cooked/icarus/icarus.mur`; breeds without a figure are held back) -> boids, leaves, weather (`world.cpp:225-245`).

---

### 2. Every hard-coded per-world check, and what Icarus needs from each

Grep: `grep -rnI -E '"(dungeon|devias|noria|lorencia|losttower|bloodcastle|atlans|tarkan)"' src tools/*.py tools/bot pipeline` (116 hits today, list in `c/grep_worlds.txt`). The sim side is keyed by map number.

| # | where | today | Icarus needs (MU's rule) | smallest change |
|---|---|---|---|---|
| 1 | `play_open.cpp:33-42`, `:476-480`; `play_sound.cpp:416, 424-428` | the air by name | **aHeaven.wav looped on the whole map** (`SceneManager.cpp:885-895, 960-963`); no wind; open sky | `heavenAir_`, a `world_heaven` event in `source/sounds/sounds.json` beside `world_desert`, `source/sounds/aHeaven.wav`, index.py, `--only showing` (memory: new sound needs the showing cook). **The sixth air keyed by name**: step 0's `MapRow.air` is overdue |
| 2 | `play_sound.cpp` footsteps | grass in Lorencia and Noria | none heard: he flies or rides everywhere off a safe tile (§3.2) | nothing |
| 3 | `world.cpp:236-245` leaves | open unless underground | rain only, 80 slots (§3.5) | an Icarus arm |
| 4 | `boids.cpp:61-119` | birds by name | 3 dark dragons 6 m below, 10 spirit wisps at his height (`MM/Engine/AI/GOBoid.cpp:824-881, 1236-1240`) | §3.4 |
| 5 | `weather.cpp:107-131` | rain in Lorencia and Noria, storms in Devias and Tarkan | steady rain, `RainTarget = MAX_LEAVES / 2` (`MM/Render/Effects/ZzzEffectFireLeave.cpp:431-434`), **no splash ring** (`:376-377`) | §3.5 |
| 6 | `void_clouds.cpp:109-149` | void = NoGround | a cloud sea under the islands where there is no NoGround (§3.1) | decision 4 |
| 7 | `pets.h:44` `dinorantFlies` | Tarkan | Icarus too | one name |
| 8 | `maps.cpp:86-97`, `minimap.cpp:272-301` | names | `case 10` | rows |
| 9 | `realm_travel.cpp:84-140` `settleFound` | | one region, one row, no giver: opens as he stands there. **No flood change** | nothing |
| 10 | `play_mode.cpp:1011-1023` music | safe zones | MU: icarus.mp3 everywhere; there is no safe zone | decision 6 |
| 11 | `realm_move.cpp:151-155` `riding`, `flying` | `!safe` | automatic: no tile is safe, so a winged hero flies on every tile, as MU's PLAYER_FLY does off a safe zone | nothing |
| 12 | `realm_move.cpp:229-237` `throughGate`'s bar | level only | gate 62 also asks for flight (§3.2) | a `fly` flag on `EnterGate` and a Barred reason |
| 13 | `realm_travel.cpp:186-205` `travelRefusal` | level, zen, quest | the Icarus row asks for flight | `TravelRefusal::Wings` (`travel.h:39`) |
| 14 | `items.h:917` `rideMap` | every map | Uniria refused on 10 (`NewUIMyInventory.cpp:351`; WZS refuses the gate with it worn, `user.cpp:27217`) | `rideMap(map, number)` |
| 15 | the summon key (`realm_skills.cpp:138-145, 224-`) | anywhere off a safe tile | **no summons in Icarus** (`ClassAttack.cpp:120`; WZS `user.cpp:29567-29571`) | one map test |
| 16 | the bag's equip and unequip | free | in Icarus, the last thing that lets him fly cannot be taken off (WZS `protocol.cpp:5356-5375`) | one test in the move |
| 17 | `realm_tuning.h:344` `kBosses = {35, 38, 49, 58, 59, 63}` | | 77, the Dark Phoenix; WZ A.Type 150 also on 72, 73, 75 | decision 7 |
| 18 | `realm_tuning.h` `kResistances`, `kDropRates` | per breed | rows for 69-77 (pass A's source) | rows |
| 19 | `play_tuning.h:618-623` `kInfernoBlows` | Tarkan's four | Mega and Alpha Crust (`ZzzCharacter.cpp:1697-1708`), Drakan and Great Drakan (`:1734-1782`) | rows (decision 5) |
| 20 | `play_tuning.h:467` `kAuraLights`, `:529` `kEyeTrailRows`, `:338` `kSandingFigures` | | no eye trails, no sand; the Alquamos's and Drakan's star lights (§3.6) | rows |
| 21 | `src/sim/items.h:911` `firecrackerMap` | dungeons and castles | nothing | nothing |
| 22 | shadows | every figure and placement casts | **MU draws no shadow in Icarus**: none for the hero (`ZzzCharacter.cpp:8594`), monsters (`:8670`), wings (`:8406`), items on the ground (`ZzzObject.cpp:9500`) or boids (`GOBoid.cpp:1586, 1641`) | decision 9 |
| 23 | dropped items | lie still | **bob 10 units on a sine** in Icarus (`ZzzObject.cpp:6497-6500`) | a world flag in the drops' draw |

---

### 3. Icarus's special features against the engine

All 61 `WD_10HEAVEN` references in MuMain were read (`c/mm_heaven.txt`). What MU does in Icarus:
- no terrain drawn, a blue clear, and a sea of cloud puffs thrown from 335 hidden emitters, flickering with lightning;
- no shadows anywhere;
- every hero flies (wings) or rides a Dinorant 90 units up; Uniria, summons and transformation are refused;
- steady rain with no splash, aHeaven.wav, icarus.mp3;
- three dark dragons gliding below and ten grey spirit trails wheeling at his height;
- a thunder flash about every 2 s, a cloud mesh flashed under him, and bolts far below;
- 180 Object11 placements throwing a rising light mote every frame;
- the monsters' shows: star lights, lightning, infernos, cloth capes, spirit rings and the Phoenix's shield.

Prices use `docs/budget.md`: 5.5 ms at 1080p; effects account 0.3 ms; spare 0.2; **0.018 ms per full screen of blended overdraw at 1080p**. Effects' sprite pool is 8 192 (`src/gfx/effects.h:80`). Reference: Tarkan, 3 200 placements, grass and ground, 225-275 fps with the HUD on.

#### 3.1 A sky with no ground under the islands (lacks the switch; the pieces exist)

**MU.** Nothing under the islands but the clear, rgb (3, 25, 44)/256, and BITMAP_CLOUD puffs (§3.3). The terrain's heights (2.85 m everywhere) are still read for every foot, drop and missile; only the drawing is skipped (`MainScene.cpp:463`).

**The engine today:**
- the ground skips a quad only on **NoGround** tiles (`src/content/ground.cpp:1486-1500`), and the clear shows through. Icarus has none (§0.1), so today `--world icarus` would draw a flat TileGrass01 plain at 2.85 m under and through every island;
- the clear is black, fixed, in the HDR shade view (`renderer.cpp:979`). The sheet has `skyColour` for lighting only (`lighting.h:33`), no clear colour;
- the abyss darkening and VoidClouds both key on NoGround (`ground.cpp:1114-1119`, `void_clouds.cpp:123-140`);
- the pointer marches `heightAt` (`play_pointer.cpp:55, 122, 203`), so a click over the void lands on the invisible plane, as MU's does. **Has it.**

**Route:**
1. **`"sky": true`** in `icarus.json` (P5): `Ground::load` keeps the heights, grid and light and builds no draw parts, so no ground draw, no ground shadow receiver, no grass. One flag in the part loop. Cost: **saves** the ground pass and the grass.
2. **`clear_colour`** in the lighting sheet: `setViewClear(ViewShade, BGFX_CLEAR_COLOR, rgba)` from it, in linear and pre-exposure, so the tonemap carries it as it carries the rest. About ten lines in `lighting.h/.cpp` and `renderer.cpp`. Zero cost. A warm `dust_colour` set to the same blue fades far islands into it for free (`dusty()`).
3. **The cloud sea** (decision 4):
   - (a) **MU's own**: the emitters' puffs (§3.3), which sit around the islands from 1.5 m below the walk plane to 1.75 m above it (placements at z 135-460 against 285).
   - (b) **Ours on top**: VoidClouds' three decks (86 sheets, 18-68 m half widths), white, under the islands. It needs its void defined as "not walkable" when `sky` is set (`voidAt`, `void_clouds.cpp`), its floor from the walk plane, and an Icarus colour beside `kTarkanColour`. Cost: Blood Castle's, about **0.03-0.05 ms** (the sheets are big but faint, 1-3 screens).
4. **Lift** (`abyssLift_`) and the abyss stay off: the islands' undersides hang lit into the blue, as MU's do.

**Cost**: a net saving (no ground, no grass), plus decision 4's clouds.

#### 3.2 Flying: the entry rule and the stance (has the stance; lacks the rule)

**What the engine has.**
- **The fly stance is built** (docs/wings.md step 4, `src/game/wings.h`, `figures.h:233-236`, `play_show.cpp:1333-1355`): `sim::Body::flying` is "a wing worn with life left, off a safe tile, not riding" (`realm_move.cpp:152`, `realm.h:376-383`). MU's stop fly 11 where he stands and fly 34 where he goes (12 and 35 with a crossbow), the wing's flap at 4x, speed `kFlyFactor` 15/12. It outranks Atlans's swim. Icarus has no safe tile, so **every winged hero flies on every tile with no new code**.
- **The Dinorant's flight is built** for Tarkan (`pets.h:36-44`, `play_open.cpp` `flying_`, `play.cpp:867`): rider 90 over the ground, the dragon 10 under him. Icarus needs its name in `dinorantFlies` (R19).
- What it lacks is MU's GOBoid Heaven arm for a Dinorant (`GOBoid.cpp:274-305`): on two keys of its flap a BITMAP_SHOCK_WAVE and 5-9 smoke puffs, a wake in the clouds. Optional; about 0.01 ms.

**What the engine lacks: the rule.** One sim predicate, `canFly(bag)`: the wing slot (`kWings`, `items.h:37`) holds a wing, **or** the mount slot holds a Dinorant (13/3) with life left (WZS checks the wing's presence, not its life; a worn-out wing still lets him in). Then:
1. `EnterGate` gains `fly`; gate 62 sets it; `throughGate` says Barred with a new reason, and the drawing's line reads "You need wings or a Dinorant to enter Icarus." (MuMain's own text is GT 1194-ish; pass A to find it).
2. `TravelRefusal::Wings` for the Icarus row.
3. Uniria (13/2) worn refuses both (WZS `user.cpp:27217`), and Uniria is not ridden on map 10 (`rideMap` takes the item).
4. In Icarus, an unequip that would leave him unable to fly is refused (WZS `protocol.cpp:5356-5375`).
5. If he stands in Icarus unable to fly (the Dinorant's life ran out with no wing on; a save loaded without them), he is sent home, **Devias gate 22** (WZS `user.cpp:10812-10842`). One check a tick on map 10, through `takeHome`.
6. No summons on map 10 (R2 #15).

Cost: sim only, zero frame. **sim_test**: `testIcarusEntry` (a hero at 18,250 in LT7 with no wing is Barred; with Satan's wings at the level he is Gated 62 and lands in 14-16,13; with Uniria worn and wings he is Barred; with a Dinorant alone he enters; taking the wing off in Icarus with no Dinorant is refused).

**Level against flight.** The first wings ask level **180** (docs/wings.md `:41-46`, kept by the user) and the Dinorant **110** (docs/mount.md `:183`). So in practice Icarus opens at 110 on a Dinorant, at 180 on wings. The breeds are 75-108. Decision 2.

**A hovering stance for the unwinged?** Not needed: MU has none (he cannot be there unwinged). The Atlans swim was a stance chooser's rule plus clips the figures already had; here even that is in.

#### 3.3 The cloud banks and the motes: hidden emitters (has the machinery; two rows)

From RenderObjectVisual's Heaven arm (`ZzzObject.cpp:3090-3191`) and the particle code (`MM/Render/Effects/ZzzEffectParticle.cpp:3103-3118` create, `:7911-7938` move, `:9169-9213` draw):

| type (model, count) | MU | engine route |
|---|---|---|
| 0-2 (Object01-03, 16 + 21 + 8) | hidden; on its first visible frame **20 BITMAP_CLOUD** puffs, sub 0-2, scattered 2.5 m round it and 0.2-0.4 m up, scale 1.8-2.0, light 0.1, Luminosity 0.6. **They never die while the emitter is in view** (`LifeTime = 50` every frame while `Target->Visible`), bob 20 units on a 31 s sine, and sub 1, 2 (and 4, 5) turn slowly one way or the other; 0 and 3 by their index | **Tarkan's DustCloud, kind 11** (`content/cooked.h:188`, `desert_vents.cpp:72`): the same "never dies, bobs on a 31 s sine" shape on BITMAP_SMOKE. A per-world row: the `cloud` sheet, 20 or 10 puffs, the turn |
| 3-5 (Object04-06, 115 + 61 + 114) | the same, **10** puffs each | the same row |
| 10 (Object11, 180) | drawn; **a BITMAP_LIGHT mote every frame** at bone 3 (`:3171-3178`): sub 0, life 10-20 frames, rising, shrinking from 0.5-1.0 | a new emitter kind (a rising mote), the bone's rest offset baked by the cook as Tarkan's glow sprites are |
| 6-9, 11-15 | drawn, nothing more | objects |

And MoveObjectOnEffect (`ZzzObject.cpp:4384-4405`): one frame in ten, on a random visible cloud emitter, a `cloudLight` sprite (scale 0.5, a random dim colour) and **two BITMAP_JOINT_THUNDER sub 6**: lightning flickering inside a bank, about 2.5 a second. `fx/thunder` is a target-to-target bolt; a short world bolt between two points in a bank is its shape. Cost ~0.

**Counts and cost.** 335 emitters throw 3 800 puffs in all. Measured on the scratch grid: within 30 m of a walkable tile there are **600 puffs on average, 800 at the 95th percentile, 940 at most**. desert_vents throws within 28 m of the camera. At 2.4 m a puff (128 texels at Scale 1.9), faint (0.06), that is about 3-5 screens of additive overdraw, **0.05-0.09 ms**, inside the 8 192 pool. The motes: about 40 Object11 in reach x ~15 alive = 600 small sprites, **< 0.01 ms**. Measure in a pack at the densest cloud field.

#### 3.4 The air: dragons, spirits and thunder (lacks; three small pieces)

- **Dragons** (`GOBoid.cpp:824-847`): boids 0-2 are MODEL_DRAGON_ (the Red Dragon, `Monster32.bmd`, not built here; `source/monsters/` has none) at Scale 0.3-0.4, 6 m under the hero, within 20 m, flying until 40 m off, dead action 1 at half speed, lit a dark blue (0.02, 0.05, 0.15) (`ZzzObject.cpp:487-493`). Boids takes one model per world (`boidOf`); a row with a depth under the hero is new. Cost: 3 skinned figures, **~0.01 ms**, and the dragon's cook.
- **Spirit wisps** (`GOBoid.cpp:850-879, 1081-1104`): boids 3-12 are invisible (MODEL_SPEARSKILL, BOID_GROUND) at his height within 5 m, each trailed by a **BITMAP_JOINT_SPIRIT sub 1** joint: 30 tails, grey (0.2), looping. `joint_spirit` is in the showing (Evil Spirit's, `fx/spirits.h`), and Scurry's `CrawlRow::trail` (`scurry.h:54-56`) is a 20-tail ribbon on a crawler. A boid with a trail is new. Cost: 10 ribbons, **< 0.01 ms**.
- **Thunder** (MoveHeavenThunder, `ZzzObject.cpp:4254-4340`; MoveObjectSetting `:4349-4363`): one frame in 50 (every 2 s), a dim yellow terrain light round him (0.06-0.1) and MODEL_CLOUD (`Object11/cloud.bmd` at Scale 10, added, 2 m north and 1.9 m down, two frames); one time in five of those, two JOINT_THUNDER + 1 bolts 3 m down at an angle, far off. And one frame in ten a BITMAP_LIGHT glow 10 m under him within 25 m. Weather's flash (`weather.cpp`, `flashes_`) is Noria's thunder light without its sound; the cloud mesh is one effect mesh (`fx/effect_mesh`). Cost: one big added mesh for two frames, **~0.02 ms when it fires**.

#### 3.5 Rain (has it; a steady arm)

MU: all 80 leaf slots are rain in Icarus (`RainTarget = MAX_LEAVES / 2` = 100, Rainly = 200 >= 80, `ZzzEffectFireLeave.cpp:246-270, 431-434`), falling 20-43 units a frame at a 30-degree lean, killed at the plane with **no ring** (`:376-377`); no rain sound and no thunder sound. The engine: `Leaves` draws rain and its rings (`leaves.h`), `Weather` holds a steady share for Tarkan (`weather.cpp:126-131`). Route: an Icarus arm, steady and light, rings off, sounds off (the loop is aHeaven's). Cost: 80 streaks, **~0**.

#### 3.6 The monsters' shows

| breed (model) | MU (`MM/Engine/Object/ZzzCharacter.cpp`) | engine has | cheapest route | cost |
|---|---|---|---|---|
| Alquamos 69 (Monster51 @1.0, BlendMesh 0) | 9 BITMAP_LIGHT sprites on its `g_chStar` bones, pale blue (`:8792-8813`), 3 sparks a frame off random bones; Energy Ball with 4 BITMAP_FLARE joints at its target (`:2140-2150`) | monster Energy Ball (`play.cpp`, Vepar's); bone lights (`kAuraLights`, the Shadows' `ShadowStars`) | a star row on a bone list; sparks thinned | ~0.01 ms a body |
| Queen Rainier 70 (Monster52 @1.3, **every mesh added**, no shadow) | a light at bone 20 (`:8815-8819`); 20 BITMAP_BLIZZARD on her target at attack frame 5 (`:1681-1696`) | added bodies (the Valkyrie's), Energy Ball; a blizzard is new (`fx/ice` is the closest) | row; ice shards for the blizzard | small |
| Mega / Alpha Crust 71, 74 (Monster53 @1.1/1.3, Thunder Blade, Legendary Shield) | **a physics cloth cape** (`CPhysicsCloth`, `:8820-8838`); **CreateInferno + MODEL_SKILL_INFERNO on every attack** (`:1697-1708`); weapon blur | `fx/inferno` and `kInfernoBlows`; **no cloth** anywhere in the engine | inferno rows (decision 5); the cape drawn as the model's own mesh or skipped | inferno as Tarkan's |
| Phantom Knight 72 (Monster54 @1.45, Dark Breaker +5) | on its boss skill **36 BITMAP_JOINT_SPIRIT sub 1** flung out a metre up (`:1710-1732`) | `fx/spirits` (8 joints) | the spirits, 36 at once | ~0.02 ms for a second |
| Drakan / Great Drakan 73, 75 (Monster55 @0.8/1.0) | the Drakan: 21 blue BITMAP_LIGHT sprites on bones 13-26 and 52-58, JOINT_THUNDER between 14-16 and 23 every frame, chrome and lightmap passes (`:8840-8893`); the Great: a fire at bone 18. Both: Inferno + 5 falling MODEL_PIERCING + 1 on attack 1 (`:1734-1782`), a piercing and a bolt at the target on attack 2 (`:2123-2138`) | inferno, `fx/thunder`, `fx/comet` (falling), chrome pass (`Drawable::refine`) | rows; the bolt as the wizard's | ~0.02 ms a Drakan; **12 Great Drakans' infernos** as Tarkan's risk |
| Dark Phoenix 77 (Monster56 shield-cage + Monster57 bird, **two models**) | the cage pulsing chrome, its meshes 2 and 3 added, then the bird drawn (`:8894-8922`), a hair **cloth**; 40 JOINT_SPIRIT sub 3 twice per boss skill (`:1785-1806`); a piercing and a bolt at frame 14 (`:2108-2121`); 4 JOINT_THUNDER at the target from frame 8 (`:2283-2295`); die clip at 0.22 (`ZzzOpenData.cpp:3788-3791`) | a figure is one body today; a second body on the same bones is the Dinorant's rider and mount pair, not a monster's | **two figures stacked** (the Dinorant's way) or one merged glb (pass B); spirits; thunder | one boss, **~0.03 ms in a fight** |
| Dark Phoenix Shield 76 | **no model, no client arm**. WZS: raised beside 77 and linked (`user.cpp:4953-4980`); 77's shield toggles every ~6 s (`user.cpp:25409-25428`, STATE_REDUCE_ATTACKDAMAGE, the Soul Barrier look), and while it is up the boss strikes every character within 2 tiles (`gObjMonster.cpp:3153-3199`). The damage rule itself is in ObjAttack.cpp, which WZS lacks | the hero's guard cage (`play.h:632`, Defense / Soul Barrier) | a boss phase in the sim (decision 7), drawn with the guard's cage | ~0 |

- **Weapons as props**: Thunder Blade (0/18, `Sword19.bmd`) and Dark Breaker (0/17, `Sword18.bmd`) are **not built and not in mu.db**; the Legendary Shield (6/14, `Shield15`) is (`source/items/shields/Shield15.*`). Build both swords as item models before the Crusts and the Phantom Knight are judged (the Destruction weapons' trap again). Whether they drop belongs to docs/second-class-gear.md (`:18` lists Dark Breaker as a later arm).
- **The risk line** is the infernos: 16 Crusts at **3 s respawn** and 18 Drakans, each throwing an Inferno per swing. Tarkan settled its own in `kInfernoBlows` (`play_tuning.h:612-623`, `bombs`/`mesh`); Icarus's take the same flags.
- **Cloth** (the Crusts' capes, the Phoenix's hair): the engine has no cloth. Skip, or bake the drape into the mesh. Decision 10.

#### 3.7 Falling, pushing and the edge (has it)

MU has no fall. A body only stands on open tiles, and the walk plane is flat. The engine agrees:
- `Realm::shove` takes a neighbour only if the grid opens it (`realm_skills.cpp:644-663`);
- drops land on open tiles (`realm_items.cpp:1298-1321`);
- the walker never routes through NoMove.

Where MU's grid is a little wider than an island's mesh, a figure stands over air at the edge, in MU and here alike. Pass B can measure it by rasterising the islands' tops against the open tiles. Nothing to build.

#### 3.8 What it costs, all together

| line | account | estimate |
|---|---|---|
| no ground, no grass | ground, grass | **saves** both passes |
| 335 placements | objects, shadow | a tenth of Tarkan's 3 200 |
| cloud puffs (MU's 600-940 in reach) | effects | 0.05-0.09 ms: **measure** |
| void-cloud decks (ours, if taken) | effects | 0.03-0.05 ms |
| Object11 motes | effects | < 0.01 ms |
| rain, spirits, dragons, thunder | effects, shade | ~0.03 ms together |
| monster stars, lightning, spirits | effects | ~0.02 ms a pack |
| **infernos on Crust and Drakan swings** | effects | **0.05-0.1 ms in a pull: decision 5** |
| shadows | shadow | MU has none (decision 9); ours as cheap as the placements are few |

Expected well under Tarkan's frame. Measure the bare islands, then with the puffs, then a Great Drakan pack (47-59 x 162-200, seven of them), with `--budget --stats --repeat 3`, timers off, on the display the log names (memory: measure on the display you think).

---

### 4. Travel: gates, warp rows, safe zone and death landing

#### 4.1 Gates (rows; the walker exists, the flight test does not)

`src/sim/gates.cpp` and `Realm::throughGate` (`realm_move.cpp:219-277`) are generic; the map change is `mapNumbered`. Directions as stored: 5 East = (+1,+1), 1 West = (-1,-1).
- `kExits` +=
  - `{63, 10, {14, 13, 16, 13}, 1, 1}`: Icarus, from the Lost Tower;
  - `{65, 4, {17, 249, 19, 249}, -1, -1}`: Lost Tower 7, from Icarus.
- `kEnters` +=
  - `{62, 4, {17, 250, 19, 250}, L_in, 63}` **with `fly`**: LT7 to Icarus;
  - `{64, 10, {14, 12, 16, 12}, L_out, 65}`: Icarus back to LT7.
  - MU asks 160 in and 50 (S6) or 80 (WZ) out. Ours is decision 2.
- **Grid**: every box is open on both maps (§0.1).
- **index.py**: `GATE_BOXES_BY_MAP[10]` and a new `[4]` (P7), so the solid-stamping never closes a door tile. Recook **both** worlds' tables.
- **The barred speech** needs its second reason (§3.2).
- **sim_test** `testIcarusGates`: a flying hero at L_in on LT7 18,250 goes Gated 62 and lands in 14-16,13 facing east; at L_in - 1 he is Barred with L_in; unwinged at L_in he is Barred for flight; at 15,12 in Icarus he goes Gated 64 and lands in LT7 17-19,249.
- **The minimap** labels the LT7 door "Gate to Icarus" once `mapName(10)` exists (R4).

#### 4.2 The warp row (decision 1)

0.95d's list has none. S6: "Icarus", 10 000 zen, level 170, to gate 63 (`OM/VersionSeasonSix/Gates.cs:69`). The options:
- (a) none: Icarus is reached only through LT7's far corner, 226 tiles from the floor's arrival, and its Tab card vanishes (R7);
- (b) one row "Icarus", landing 15,13, facing east, refused without flight (§3.2).

Append after "Tarkan" (`realm_travel.cpp:47`): `kTravels` becomes 16 (`travel.h:30`). No giver and one row: it opens the first time he stands there (`settleFound`'s `!giver && !byFloor_`). No flood fix (one region).

#### 4.3 Safe zone, death landing, Town Portal: the gate-22 lesson, the other way round

- **Icarus has no safe zone** (0 tiles). So a death and the Town Portal leave the map, and the landing is `MapRow.home`, as Blood Castle's is (`maps.cpp:45`, `play_mode.cpp:736-752`).
- **The trap**: `home` defaults to 0. A row written `{"icarus", 10, {15, 13}}` sends every death in Icarus **silently to Lorencia**, Devias's gate-22 bug of `1ad73855` in a new form (tarkan-port.md `:184-186`, lost-tower-port.md `:30-38`). Write the home outright.
- Which home: WZS sends the dead to **Devias** (`user.cpp:22077-22081`, gate 22's map, `207,42` here); OM 095d's SafezoneMap is the **Lost Tower** (hall gate 42, `208,75`). Decision 3. `home = 2` follows the user's WebZen picks (memory: WebZen server source).
- **No mu.db `gates` row** is wanted: the `.mur` header's safe box stays zero, and that is right here. Check after step 1: the header reads `map 10` with a zero safe box, and a headless death lands in the home's town.
- The Town Portal is allowed in Icarus (`PortalMgr.cpp:56`): it goes home, and the Go Back! portal returns him to where he read it (memory: Go Back! portal).
- The save: `event` false, so he resumes in Icarus. He must also be able to fly when he resumes, or §3.2 rule 5 sends him home.
- Monsters may chase to the door: there is no safe tile to stop at. The nearest Alquamos is 14 tiles from 15,13, with a view of 5.

---

### 5. Spawns and monster tables

#### 5.1 mu.db today (`sqlite3 -readonly source/mu.db`, 2026-10-06 ~17:10)

- The WAL is **0 bytes**; `-shm` was touched at 17:03 (another session has it open). Never write while a session is live; after writes, `pragma wal_checkpoint(TRUNCATE)` before committing (memory: mu.db writes sit in the WAL).
- `monster_kinds` holds **96 rows**: 0-41, 45-49, 51, 52, 57-63, 84-99, 111-130, 132-134, 150. **None of 69-77.** Columns as Tarkan's Part C wrote them.
- `monster_spawns` by map: 0: 9, 1: 258, 2: 9, 3: 8, 4: 448, 7: 336, 8: 217 (ids 1266-1482), 11: 96. **Map 10 has none.** `max(id) = 1482`, so new rows start at **1483**.
- `gates`: 17 (0), 22 (2), 27 (3), 42 (4), 49 (7), 57 (8). No row for 10, and none wanted (§4.3).
- `items`: 166 rows, `max(drop_level) = 75`. No Thunder Blade, no Dark Breaker. The wings, the Dinorant and Loch's Feather are not mu.db rows (they come from `source/items/` through index.py). The breeds are 75-108, so every row reaches them and `kGap` picks the top band.

#### 5.2 Steps to add Icarus's spawns (OM 095d unless pass A says otherwise)

1. **No `gates` row** (§4.3).
2. Nine kinds, 69-77, from the chosen file (OM 095d `Icarus.cs:122-418`, §0's table):
   - level, health, damage, defence, ranges, move delay 400 and attack delay 1400 (1500 for 77), rates;
   - `respawn_seconds` 10/10/3/3/20/3/3/10/15 written true (or WZ's RegTime; the Phoenix's 1800 is a decision);
   - `attack_skill`: 69, 70 = **17** (Energy Ball); 72, 73, 75, 77 = **50** (MonsterSkill, stored as the Hydra's); 76 = the Lightning's number; 71, 74 = 0.
   - 76 is a kind but never a spawn row: the sim raises it with 77 if decision 7 takes WZ's shield.
3. **Spawns**: the 65 one-tile rows from `Icarus.cs:53-117` (`CreateMonsterSpawn\(\d+, this\.NpcDictionary\[(\d+)\], (\d+), (\d+)\)`) as `x1 = x2 = x, y1 = y2 = y, count = 1, map = 10`, ids 1483-1547, in one transaction. Check:
   - the count is 65, by kind 69x11, 70x6, 71x10, 72x12, 73x6, 74x6, 75x12, 77x1;
   - none on a closed tile (already checked on the scratch grid: 0 of 64 closed);
   - the Phoenix at 34,238.
4. Checkpoint the WAL; run `pipeline/index.py`; `tools/cook.py --world icarus --only tables`, **then the other nine worlds' tables**.
5. Figures one breed at a time: `--only figures --world icarus --monsters <Name>`. Monster51-57 are in `D/Monster`; none is in `source/monsters/` yet.
6. `realm_tuning.h` rows: `kResistances`, `kDropRates`, `kBosses` (+77; decision 7).
7. Sounds: `sounds.json` monster events from §0's wavs, plus `world_heaven`; `index.py`; `--only showing`.

---

### 6. The build, in order

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. No window without asking; `--mute` and a scratch `--save` on any review run (memory). `checks` = `cmake --build build --target checks`. Read `git diff` of every file before staging, and commit through a private index if another session has staged.

**Step 0: the per-world air and look as `MapRow` fields** (optional, recommended now; no behaviour change). `maps.h` += `const char* air` (null = wind), `bool openRoom`, `bool sky`, `bool fly`. Fill the nine rows; read them in `play_open.cpp:33-42, 476-480` and `play_sound.cpp:416, 424-428` in place of the six bools. Check: `checks`; headless `--world dungeon|losttower|atlans|tarkan --ticks 2000 --seed 7` logs are byte-identical. If skipped, step 3 adds a seventh bool.

**Step 1: cook the ground and set the lighting** (bare sky, reachable by `--world icarus`).
- `terrain.py`: `HIDDEN_BY_MAP[10] = {0..5}`, `GRASS_BY_MAP[10] = []`, `SKY_BY_MAP = {10}` writing `"sky": true`. index.py `GATE_BOXES_BY_MAP[10]` and `[4]`.
- Run `python3 pipeline/terrain.py $D/World11 icarus source/world 11` (expect flat 2.85 m, 10.7% walkable, 0.0% safe, 670 placed).
- `source/world/icarus/ground.json` (one TileGrass01 sheet, built so `worlds()` lists it, never drawn), `tools/content.sh --world icarus`, sync, `--only ground`, `index.py`, `--only tables`.
- Engine: `Ground` honours `sky`; the sheet's `clear_colour` reaches `renderer.cpp:979`; `maps.cpp` row `{"icarus", 10, {15, 13}, false, 2}`; `arrival.cpp`; `minimap.cpp case 10`; `sheets/worlds/icarus.json` with `clear_colour` (3, 25, 44)/256 in linear and `dust_colour` matching it (pass B's look on top).
- Check: `./run.sh --headless --world icarus --ticks 200 --no-hand` logs `grid 256 ... map 10` and the ground's part count **0**; the `.mur` header's safe box is zero; a headless death lands in Devias at 207,42; `checks`. **Seen**: muted shots at the door (15,13), the Alquamos (25,35), the Drakans (55,75) and the Phoenix's island (34,236): blue under everything, no grass plain, fps from the log.

**Step 2: the gates and the flight rule.** `gates.cpp` 62/63/64/65 (levels per decision 2), `EnterGate::fly`, the Barred reason and its line, `canFly`, Uniria's refusal, the unequip refusal, the home-when-grounded check, no summons on 10; the Lost Tower's tables recooked for its boxes; `testIcarusEntry`, `testIcarusGates`. Check: `checks`; headless LT7 `--at 18,250 --level L --give <wing stem>` logs `Gated 62`, and without the wing `Barred` for flight. **Seen**: a muted walk through the door both ways, flying.

**Step 3: the air.** `world_heaven` (aHeaven.wav) in `sounds.json` + index.py + `--only showing`; `heavenAir_` (or step 0's field), room Open, no wind; `dinorantFlies("icarus")`; the steady rain with no rings; music per decision 6. Check: the log names the air `world_heaven`, the rain's share, `dinorant: flying`. **Seen**: a muted shot on a Dinorant.

**Step 4: objects**, in pass B's small batches (memory: 3-6 objects built, cooked, shot and shown before the next). `cook_one.py ObjectNN --world icarus`, then `--only textures`, then `meshes` and `placements` (textures before meshes). `FLAT_OVER_VOID` += icarus first. Check each batch with `--budget --stats` and that no island was lifted, lowered or "buried". **Seen**: each batch muted, the hero's feet on each island's top.

**Step 5: the cloud sea and the motes.** `ANCHOR_KINDS_BY_WORLD["icarus"]` (Object01-06 as cloud banks on the `cloud` sheet, Object11 as motes); desert_vents (or a sibling) opened on Icarus; the in-bank lightning; then decision 4's VoidClouds decks if taken. Check: the open line counts 335 cloud emitters and 180 motes; refused sprites 0; the effects account before and after. **Seen**: muted shots at the door and the densest bank.

**Step 6: the sky's life** (each optional, each shown before the next): the thunder (flash, cloud mesh, far bolts), the spirit wisps, the dragons (Monster32 cooked first). Check: boids_test unchanged; the log names 3 dragons and 10 wisps. **Seen**: one muted run with all three.

**Step 7: monsters, by breed batches** (§5.2's 1-4 first: the kinds, 65 spawns and every world's tables). Each batch shown before the next:
- (a) **Alquamos + Queen Rainier**: Energy Ball (free), the star lights, the Queen's added body and blizzard.
- (b) **Mega + Alpha Crust**: Thunder Blade built first, the Legendary Shield, infernos per decision 5, the cape per decision 10.
- (c) **Drakan + Great Drakan**: the blue lights and lightning, the falling piercings, infernos.
- (d) **Phantom Knight**: Dark Breaker built first, the 36 spirits.
- (e) **The Dark Phoenix** (and its shield per decision 7): the two-model figure, the spirits and thunder, `kBosses`.

Check after each batch: `sqlite3 -readonly source/mu.db "select number,count(*) from monster_spawns where map=10 group by number"`; headless `--world icarus --ticks 30000 --level 110` reports 65 monsters; `checks`; `--budget --stats` in the Great Drakan pack (47-59 x 162-200, seven of them). **Seen**: arena shots (`--arena Monster55`), then a muted hunt.

**Step 8: the travel row** (decision 1): "Icarus" after "Tarkan", `TravelRefusal::Wings`. Check: `checks`; the travel log opens "Icarus" on arrival; Tab refuses it unwinged.

**Step 9: drops and the side hooks**, per decisions 8 and 11: Loch's Feather's map, the Crest of Monarch, the two swords as drops, the floating drops' bob, the shadow choice.

---

### 7. Decisions for the user

1. **Warp row**: none, or one "Icarus". *Recommend one row* landing 15,13, at S6's 10 000 zen, its level equal to decision 2's door, refused without flight. Without it the map is a 226-tile walk from LT7's arrival and its Tab card disappears.
2. **Door levels LT7<->Icarus**: MU asks 160 in, 50 (S6) or 80 (WZ) out; the warp 170. The breeds are 75-108, and flight already gates it: a Dinorant at 110, wings at 180. The house rule set Tarkan's door just over its breeds (100 for 72-93). *Recommend in 110* (the Dinorant's own level, just over the breeds' 75-108), *out free* (0), as the tower's way out is nearly free (15). Then a Dinorant rider can come at 110 and a winged one at 180.
3. **Death and Town Portal home**: WebZen's Devias (gate 22) or OM 095d's Lost Tower hall (gate 42). *Recommend Devias*, the user's WebZen picks (2026-09-30); the Lost Tower is the alternative if a death should leave him a door away.
4. **The cloud sea**: MU's emitters' puffs only (600-940 faint puffs round the islands, 0.05-0.09 ms), or VoidClouds' decks beneath as well (ours, 0.03-0.05 ms more). *Recommend MU's puffs first, then judge whether the void under them needs the decks.*
5. **Infernos**: MU throws one on every Mega/Alpha Crust swing (3 s respawn) and every Drakan attack 1. *Recommend* Tarkan's settled flags (`kInfernoBlows`): the bombs and the mesh as Tarkan's Tantallos takes them, judged in the Great Drakan pack.
6. **Music**: MU plays icarus.mp3 on the whole map; this game plays a world's theme only in its safe zone, and Icarus has none. *Recommend* once on arrival through the door or Tab, then the rule's rest (memory: music is rare).
7. **The Dark Phoenix and its shield**: (a) OM's plain boss; (b) WZ's shield phase (every ~6 s on/off, hitting everyone within 2 tiles while up, the 76 body linked to it). *Recommend (b)*, drawn with the guard's cage, and 77 in `kBosses`. Also its respawn: OM 15 s or WZ 30 min. *Recommend WZ's 30 min*: it is the map's boss.
8. **Loch's Feather**: MU drops it in Icarus alone (`m_bFeatherOnlyIcarus`, WZS `gObjMonster.cpp:4620`); this game moved it to Atlans and the tower because there was no Icarus (`items.h:185-192`). *Recommend* adding map 10 to `featherMap` and keeping the other two, so no player loses a source. S6's Crest of Monarch (13/14 level 1) only with the 2nd-wings work.
9. **Shadows**: MU draws none in Icarus. *Recommend ours* (the sun's shadows on the islands): the user has asked for stronger shadows elsewhere, and with so few placements they cost little. Turn them off by sheet if the islands read wrong.
10. **Cloth capes**: the Crusts and the Phoenix wave cloth MU simulates. *Recommend* skipping the sim and judging the bare models; a baked drape if they read bare.
11. **The two swords** (Thunder Blade 0/18, Dark Breaker 0/17): props now, items with the second-class gear list. *Recommend props now.*

---

### Side findings

- `src/game/pets.h:43` and `src/sim/items.h:185-188` say "Icarus is not in this game"; correct both when map 10 lands.
- `pipeline/index.py:58-78` `GATE_BOXES_BY_MAP` still lacks maps 4 and 11. Icarus's door is in the tower, so map 4 can no longer wait.
- `pipeline/terrain.py:390-397` and `pipeline/index.py:44-51` are still two copies of `OPERABLE_BY_MAP`.
- `src/gfx/renderer.cpp:979` hard-codes a black clear. MU has two coloured clears among our maps' kin (Devias light blue, `SceneManager.cpp:383-384`; Icarus blue). Devias's is not carried today either.
- `index.py worlds()` (`:1519-1527`) requires a built ground glb, so a world that draws none still needs one built.
- `tools/bot/bot.cpp:68-74` knows five worlds: no Atlans, Blood Castle or Tarkan.
- WZ's `MonsterSetBase.txt` for map 10 has no Dark Phoenix and two of kind 67; OM 095d has the Phoenix at 34,238. Pass A should settle the spawn source before step 7.
- OM 095d `Icarus.cs` defines kind 76 (the Shield) with HP and Lightning but never places it; WZS raises it in code. A row-driven port would leave it unplaced unless the sim does what `user.cpp:4953-4980` does.
