# Tarkan port: the record, the data, and how it plugs in

Research notes, 2026-10-05. Tarkan is MU server map **8**, client `Data/World9` + `Data/Object9`
(MuMain's `WD_8TARKAN`), entered from Atlans's south-west lagoon. Three research passes wrote this
page in `docs/atlans-port.md`'s shape. Part A is the record, Part B is the data and the look, and
Part C is the engine side, ending in the build steps. Nothing but this page was edited, nothing was
cooked or written to mu.db, and no window was run. Pass B ran `terrain.py` and MuExtract into
scratch only.

The passes' scratch is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/2dfb4978-e009-4851-9d8e-04ebd11cc2ff/scratchpad/tarkan/`:
- `a/`: the grid decodes, the spawn and stat comparisons, the drop pools, and WebZen Korea's 2002-2003 notices (the 0.84 patch note and two press releases) saved as text;
- `b/`: the terrain.py run (`tarkan/`), every Object9 model and Monster42-46, Sword17 and Staff09 exported (`obj/`, `mon/`), the decoded sheets (`tex/`), and the contact sheets `sheet_objects.png`, `sheet_monsters.png`, `sheet_world9.png`, `sheet_object9_textures.png`, `prev_l1_l2_grass_attr_height_light.png`, `light_big.png` and `att_client_om_diff.png`;
- `c/`: the per-world grep, the MuMain `WD_8TARKAN` list, and the grid and split scripts.

## Where it stands

**Steps 1 and 2 are built (2026-10-05, the user: 'lets start with cooking the ground', 'also make atlans -> tarkan -> atlans gates').** They added:
- `source/world/tarkan`: World9 through `terrain.py` (0-3.83 m, 10 slots, 36.4% walkable, 2.9% safe, 3381 placements of 82 kinds, Object01/04/05/36/39 missing as in MuMain);
- `terrain.py` rows for map 8 (`HIDDEN_BY_MAP {60, 63, 64, 70, 76, 83}`, `BLEND_MESH_BY_MAP`, `GRASS_BY_MAP ["TileGrass01"]`, `VOID_BY_MAP {"rim": True, "blend": 1.5}` as Blood Castle's, `OPERABLE_BY_MAP {78}`), and `index.py`'s (`OPERABLE_BY_MAP`, `GATE_BOXES_BY_MAP` for 55, 54 and 57, and Atlans's 53 and 56);
- ten `tk_` sheets and a `ground.json`. Slot 5's sand ripples take `water`, as the Lost Tower's lava does, so they slide at MU's half speed (`water_flow` 0.5) with no sheen;
- mu.db `gates (57, 8, 187, 54, 203, 69, 1)`, so a death and the Town Portal rise in the town;
- the `kMaps` row `{"tarkan", 8, {195, 65}, true}` (underground for now: no leaves or wind, and the Roofed reverb until step 3's air), the banner and minimap names, and `sheets/worlds/tarkan.json`, B §6's starting sheet;
- gates 53 (Atlans to Tarkan, **100**) and 55 (back, **70**), exits 54 and 56, spawn gate 57: decision 2 taken at pass C's recommendation until the user says otherwise. `testTarkanGates` walks both ways, checks the bars at 99 and 69, and the town's safe box. Starting it two tiles south of the door let the lagoon's Hydras kill the hero first.

Shot muted at the town (195,65), the Atlans door (247,42), the middle plateau (95,150) and the forge corner (20,215), at 110-125 fps with the HUD on. The only error in the log is the missing `tarkan.mut`, because no objects are cooked yet. Not yet judged by the user. Tarkan's Tab card is gone until a travel row names map 8 (trap above).

**The ground's look, 2026-10-05.**
- **Desert grass**: a sheet knob of ours, `grass_tufts` (`lighting.h`, `u_grassShape.z` in `grass.sh`): a share of metre cells left bare and the rest a round domed tuft, so the sward is straw clumps on sand. Tarkan's 0.5, regraded tan to straw (the user: 'procedual grass has to be more desert style').
- **The voids are Devias's** (the user: 'in tarkan there is same tehniq which was in devias'): `VOID_BY_MAP[8]` removed, so MU's cut and the default abyss, with the cliff walls standing in it.
- **A guard in `content/ground.cpp`'s `void_floor`** (another session's uncommitted Lost Tower lava): an empty name matched Tarkan's unused slot 2 and floored every void tile.

**Objects, batches 0-3 (2026-10-05, 'they should looks devias ... not just black holes', then 'do it').** 17 kinds, `tk_` sheets, 1144 placements drawn:
- walls: Object41 (427 cliff faces), Object40, and the mesas Object42-44;
- rocks and tufts: Object07 (529 dry tufts on their own clip), Object24, 28, 29, 81;
- trees and shrubs: Object32, 33, 34 (bark and `test2_tga` leaves), 47, and ferns Object27 and Object31 (its own clip);
- lights: Object08, the 98 lava glows, MU's added pulse and its own orange terrain light as 98 lamps (3 tiles, `ZzzObject.cpp:4108-4119`), lifted 0.3 m (`LIFTED_BY_WORLD`) because at MU's height they cut the ground into streaks.

Shot muted at 20,215, 207,80, 162,90, 108,116, 90,102, 234,54 and 8,243. The forge corner reads as lava pools in the dark; on the noon plateau a glow is a faint wash. Not yet: the streaming and bright kinds (sand falls, lava flows, gears, the orb), the stone hands, heads and statues, the hidden emitters, the seats, the bugs.

**Lights and emitters (2026-10-05, the user: 'now work on tarkan light and fire emiters').** MU's Tarkan has no fire at all; its light is these:
- glowing objects, `tk_` sheets: Object83 (60 light shafts, light01 added), Object62 (2 sun-gears) and Object66 (the forge gear, its own clip), gearB added and slid a sheet a second, pulsing, each with MU's orange terrain light over 2 tiles; Object68 (6 gears, no arm, plain); Object82 (the golden orbs, brass for MU's chrome pass); Object03 (4 cyan rings, U slid); Object73 (red lava, added, V slid a sheet in 5 s); Object74/76/80 (orange lava falls, runs and pools: MU's StreamMesh carried as an added slide, a marked departure as the Dungeon's tentacles);
- **`game/world/desert_vents.*`**, the hidden emitters cooked as world anchors 7-10 (`ANCHOR_KINDS_BY_WORLD["tarkan"]`): Object71's falling sand (3), Object77's steam vents (19, in step, the last 0.5 s of every 5), Object84's sand geysers (10, 0.5 s of every 10, staggered by a position hash in place of MU's turn, throwing the meteor's stones), Object64's cyan Impack03 glow sprites (18, the showing's new `impact`). Only within 28 m of the camera; smoke at half MU's light. Object61's one-off dust burst is left out;
- **`tools/cook.py`'s roof and water-sway sets are per world now**: Devias's roofs Object82/83 had made Tarkan's orbs and shafts roofs, which an underground map hides.

Seen in muted 60 fps runs, a shot every half second: the forge corner (8,245), the crater geysers at 109,237 bursting with stones (frames 210-330), the shrine (70,185), the rings and a shaft (155,235).

**The rest of Tarkan's objects, the scarabs, the air and the storm (2026-10-05, 'continou').** Every placed kind is now built, 69 models and 3192 placements:
- the landmarks: the stone hands (Object02/06), colossal heads standing and fallen (15/55), fallen statues (69/70/72/75), rock arches (45/46/48), cacti (21/22/25/26), thorn bushes and creepers (16-20), totems and boulders (10/11), coffins and coffin-thrones (53/56/59/60), skeletons and bones (49/50), grave shields (38), rock spires and litter (52/57), stumps (37) and seats (79, `PERCHES[8]`, 13 perches), the scarecrow banner (30), the rags (09, at MU's 4x, `play_speed` 0.64), the debris clouds and rock falls (51/58, their unreferenced vertices stripped from the OBJ and the rig together so Blender's import and the rig agree), the sand falls and whirlpool (12/13/14, still: MU slides them unlit and opaque, which this engine does not do yet);
- `FLAT_OVER_VOID` takes Tarkan, so the 28 animated placements reaching into the void play rather than hold;
- **the scarabs**: `Scurry` takes a per-world `CrawlRow` (`crawlRowOf`), Tarkan's ten Bug02 at Scale 0.8-1.1, Velocity 2.5/Scale, turning 9 a frame, LifeTime 100, silent; `CRAWLS["tarkan"]`. Their energy trail is not drawn yet;
- **the air**: `world_desert` (desert.wav, -20.4 dB to the -44 dB bed) on the wind's slot through `Play::desertAir_`, in the Open room though the map is underground; `tarkan.mp3` in the town as Atlans's and the tower's tracks play in theirs;
- **the sandstorm**, `game/world/sand_haze.*`: MU's two RenderOutSides layers, sand01 at 0.3 of the sheet sliding 0.2 a second and sand02 at 3x2 sliding 1 a second, the quarter-sheet lean, (0.3, 0.3, 0.25) times 0.6 (ours), as placed quads half a metre before the eye; the showing's `sand` and `sand_fine`.

Shot muted at 20,220 (the forge's arches and whirlpool), 95,185 (hands and head), 145,215 (arches over the void), 138,52 (the cacti), 216,56 (the scarecrow in the town) and 90,55 (the statues): 225-275 fps with the HUD on.

**The world finished (2026-10-05, 'first lets finish the world than make monsters').**
- **Opaque streams**: a recipe's glow entry may say `stream` on a sheet that is no glow; the cook marks it (flag 16, axis "uv" mode bit 64), `Material::scrollAlongUV`, and the renderer passes the slide in `u_sway.zw`, which `vs_static` adds to the uv. Object12/14's sand falls slide V 0.2 a second and Object13's whirlpool U and V 0.05 (MoveObject, ZzzObject.cpp:4120-4133), lit by the sun where MU draws them unlit, ours. Measured: the fall's pixels change five times what still ground does over a second.
- **Travel**: the row "Tarkan", level 100, 8,000 Zen, landing at 195,65, after Atlans's (another session's, uncommitted); `kTravels` 15. No giver, so it opens as he stands in Tarkan. The Tab card is a real row again.
- **The Dinorant flies**: `dinorantFlies(world)` (game/pets.h), the rider 90 over the ground and the dragon 10 under him in Tarkan (ZzzCharacter.cpp:6387-6390, GOBoid.cpp:517-520), its fire breath from that height. Shot against Atlans's 30.
- **The scarabs' trails**: `CrawlRow::trail`, 20 tails a reference frame apart, flat 30 units across, MU's (0.3, 0.15, 0.1) added on `joint_energy`. Over Tarkan's noon sand they barely show, as MU's numbers make them.

Monsters started before the user turned it to the world: kinds 57-63 and the 217 spawns are in mu.db (ids 1266-1482, checkpointed), every world's tables recooked (96 breeds), `kResistances` and `kDropRates` rows (MoneyRate 30, RegTime 10/20/150), and the Mutant (Monster46) and Bloody Wolf (Monster44) cooked with their sounds; the other five breeds are held back, their exports and sheets in source/ for later. `strip_unused.py` (scratch) strips OBJ vertices no face uses from the OBJ and the rig together, which Blender's import drops.

**Every Tarkan breed cooked (2026-10-06, the user: 'lets work on cooking tarkan monsters').** The Iron Wheel (CrossBow07 on Bone04, so it shoots the bolt), Tantalos (Sword17 on Bone01, bv03 added), Zaikan (Staff09; bv02_2/bv01_2 on every mesh, all added, half a shadow, as the Valkyrie), Beam Knight (mane soft-alpha) and Death Beam Knight (BlendMesh -2: every mesh added) join the Mutant and Bloody Wolf; all seven in `figures_tarkan.json`, their cries (iron, jaikan, death) in sounds.json and the showing cook, every world's tables recooked. Zaikan01 and DeathBeamKnight01 carry their own copy of Monster43/45, because a glow is baked into the glb. `actions.json` gains `action_overrides` (the Tantalos's 0.35 attacks, the Beam Knight's 0.3 death, ZzzOpenData.cpp:2633-2639), which `monster_speeds.py` applies after the walks. **`pipeline/islands.py` now gives a new island its sheet's `sheet_materials`** in place of the profile's skin: the Mutant and Wolf had shipped all skin, and were rebuilt. The Mutant's blade went brass, whited out, and is painted_steel now. Judged on cook_one stage sheets at noon, dusk and night. Not drawn: the eye trails, the sand smoke, the Zaikan's foot fires and dark ground, the bosses' rings and blasts, the Tantalos's Inferno, the Wheel's three-bomb spread. The Tantalos has 276 bv02 triangles inside hide-majority islands, which take skin. It reads right, so it stays.

**The last objects and effects (2026-10-05, 'continoy to migrate world obejcts and effects').**
- **The Kanturu gate**, Object86-88 (the arch with chogatea02_R added, as MU's TextureScript adds every _R sheet; its pillars; its steps): Season content, a sealed ruin with no gate behind it. Every placed kind with a model on disk is now built: 72 models, 3200 placements.
- **Object61's dust clouds**, `EmitterKind::DustCloud` (11) in `desert_vents`: not the one-off burst Part B read. MU's SubType 6 resets its LifeTime to 10 every frame (ZzzEffectParticle.cpp:5412-5418), so its 20 puffs a box never die: 82 standing clouds of 1640 puffs, each bobbing 20 units on a 31 s sine and swelling 1.3-2.3, lit (0.36, 0.3, 0.24), here at half that, within 28 m of the camera. 589 sprites in the busiest frame shot, none refused.
- Left: MU's out-of-step grass ripple (x 50, ZzzLodTerrain.cpp:3282-3285); our grass wind is its own model.

**Animations, lights and effects audited (2026-10-05, 'now work on baked in animations and light or other effects for tarkan objects if they are not done').**
- Every Object9 .bmd was exported with all its actions: nine carry a clip of more than one key (Object07 8, 09 11, 30 6, 31 6, 51 32, 58 31, 60 16, 66 20, 68 20), all nine have rigs and all 662 of their placements play (Sway's log, none held). MU sets a Velocity for type 8 alone (Object09, 4x), carried.
- Every MoveObject, RenderObjectVisual and RenderObject arm for WD_8TARKAN is carried, and one more now: **Object05**, a model the client does not ship, whose arm still adds a white pulsing terrain light over three tiles (ZzzObject.cpp:4095-4106) -- cooked as a world lamp anchor (kind 0, `ANCHOR_LIGHT[0]`). 102 lights in all.
- Object82's RENDER_CHROME | RENDER_BRIGHT second pass (ZzzObject.cpp:1067-1073), then the user: 'Not done: the golden orbs' chrome shine'. `CHROMED_BY_WORLD` in tools/cook.py sets a placement's instance bit 5, and `Town::drawableOf` draws it at the +7 chrome in white (`Drawable::refine`, which the town's records already packed), as a figure's `plus` carries the Silver Valkyrie's. Shot at the shrine, 70,186: a white chrome streak over the gold.

**The desert vibe (2026-10-05, the user after exploring: 'make thos bugs smaller also there is some flaoting grass objects, lets also work on tonemaping and dust effect which has devias', then 'tarkan storm has to happpen all the time but not so strong like its in devias').**
- **Plants laid on the terrain**: MU perches 71 of Tarkan's plants on rocks, trees and cliff tops, up to 3 m up (68 within 2 m of a solid object). `GROUNDED_TYPES["tarkan"]`, the plant types {6, 15-22, 24-26, 30, 53}: 1019 placements laid on the ground, ours.
- **Scarabs smaller**: `CrawlRow::drawn` 0.55, about 20 cm for MU's 35; their motion and stride stay MU's.
- **A steady sandstorm**, Devias's blizzard machinery: `Weather` takes Tarkan as a storm world held at `kSandShare` 0.4 (`steady_`, no calm and no spell; `--weather` still wins), so the storm sheet `sheets/worlds/tarkan_rain.json` (dimmed warm sun, ochre fill, sepia, a sand haze) is 40% in, the wind 40% up and the blizzard loop at 40%. The sand rides Devias's snow pool (`Leaves::setSand`): flakes in a sand colour, 0.1-1.8 m up, settling slowly, no glints, about 80 in the air; open air though the map is underground (`Leaves::openAir`, and the weather is told so).
- **The tone**: `tonemap` 1 (Hill's ACES) so the bright sand rolls off golden, exposure 1.3, a warmer gold in the light, and a sand haze of 0.004 calm / 0.016 storm (0.009 as held). A first cut at 0.006 / 0.022 read milky.

**Tuning after exploring (2026-10-05/06).** Storm 0.4 to 0.3 to 0.2 (`kSandShare`), sun 4.4 to 5.2 to 4.8, the screen sand 0.6 to 0.4 (`sand_haze` `kLevel`); the rags (Object09) laid on the ground with the plants; the vents and dust on smoke01, not smoke02, whose bright square edges added up as blocks ('this smoke effect is little bit pixelated'), at 0.75 for its dimmer heart. Then 'floating lava decals also keep working on stronger shadows, and some better balance between lighting and shadows, also use BC clouds in voids':
- the lava glows (type 7) laid on the terrain and lifted 0.1 m, not 0.3 over MU's height;
- fill 0.65 to 0.45, the sand bounce darker, contrast 0.56, exposure 1.36, `ssao_strength` 1.2: the shade under a rock deeper against the same sunlit sand;
- Blood Castle's void clouds in a warm dust colour, but `shallow_`: Tarkan's void is drawn barely under its plateaus, so at the castle's depths every cloud lay under it unseen. Its top layer alone, 0.05-0.3 m under the rim, and only where the whole cloud (18-28 m) is over void -- the wide voids, not the gaps between lands ('lets use cloud in tarkan only when void is taking a lot of screen not between lands'). Seen at 18,156, faint banks; none at 108,116.

Two things were already in the code from earlier work:
- `sim::kTarkanMap = 8` and `secondGearHere` (`src/sim/items.h:388-397`, `src/sim/realm_items.cpp:1130-1137`) already let the second class's gear drop on map 8. The user said on 2026-10-04: "there will be tarkan map where 2nd class drop".
- `assets/music/tarkan.mp3` is already in place.

## The one thing to know first

**Tarkan is not 0.75. It is the first world MU added after it**, on the Korean live servers with
patch 0.84 on 2003-01-07, about seven weeks after Korea's 0.75 (A §1.1, from WebZen's own notices).
That patch also brought excellent items and the Jewel of Life. Everything this game already carries
from after 0.75 came later than Tarkan: the class change and the 2nd wings (2003-07), the Box of
Kundun (2003-08) and Blood Castle (2003-09). OpenMU has Tarkan only from Version095d, and that
version has **no gate, no warp row and no spawn gate** for it. The way in and the death rule come
from WebZen's official 0.99.60T data (WZO), the WebZen C++ (WZ) and Season 6, which agree with each
other. This is decision 1.

**Half the map is never drawn.** 51.6% of the tiles are NoGround, which MuMain skips, so Tarkan is
pale desert plateaus floating on the black clear colour. Their edges are walled by 427 Object41 cliff
faces, most of them sunk into the void (B, the one thing to know first). How we show the void is a
look decision.

**The desert wind is two full-screen additive sand layers, not particles.** That is MU's
`RenderOutSides` (`ZzzInterface.cpp:3677-3689`). Two existing placed sprite quads can draw it, at
about 0.036 ms (C §3.1).

## In one screen

- **The record is clean.** OM 095d's 217 spawns are WZO's (and the repack's), row for row in the
  same order. OM's stats are WZO's 1.00m numbers to the digit (A §4).
- **One map, one walkable region** of 23 832 tiles, with one safe town in the north-east (1 872 tiles,
  x 155-251, y 28-97). The town has **no NPC in any official source** (WZO, OM 095d, OM S6). The
  repack's Thompson, Baz and Amy are the repack's.
- **The client grid can be used as is.** It is WZO's but for 133 tiles round a Season 6 door. OM's
  S6 file opens 1 102 tiles more (A §1.3, B §1.1).
- **Seven breeds, levels 72-93, and the level climbs with the walk out from the town, with no gap:**

  | # | breed | lvl | HP | spots | body | MU's show |
  |---:|---|---:|---:|---:|---|---|
  | 62 | Mutant | 72 | 10 000 | 41 | Monster46 @1.5 | eye trails, sand puffs |
  | 60 | Bloody Wolf | 76 | 13 500 | 32 | Monster44 @2.2 | eye trails, sand puffs |
  | 57 | Iron Wheel | 80 | 17 000 | 23 | Monster42 @1.4, Aquagold Crossbow (cooked) | a fan of three bolts |
  | 58 | Tantalos | 83 | 22 000 | 63 | Monster43 @1.8, **Sword17** (built 2026-10-06) | Inferno on each blow; WZ's 1-in-5 area blow |
  | 61 | Beam Knight | 84 | 25 000 | 56 | Monster45 @1.5 | Energy Ball (free), hand flames, Inferno |
  | 59 | **Zaikan** | 90 | 34 000 | 1 | Monster43 @2.1, **Staff09** (built 2026-10-06) | bright re-skin, an 18-staff ring, dark ground |
  | 63 | **Death Beam Knight** | 93 | 40 000 | 1 | Monster45 @1.9 | a burning body, rain of Blasts, the staff ring |

  Respawns are 11 / 21 / 151 s (WZO's RegTime + 1). MoneyRate is **30** on all seven, against 14
  for Atlans and the tower. None of the seven is in mu.db, and none of the five bodies is built.
- **Getting there.**
  - Atlans gate 53 (14,225-15,230, beside the Hydras) leads to Tarkan exit 54 (248,40-251,44). The way
    back is Tarkan gate 55 (246,40-247,44) to Atlans exit 56 (16,225-17,230).
  - MU asks **130** both ways (140 at launch). WebZen gives a second-class Magic Gladiator or Dark
    Lord a two-thirds discount; we have neither class.
  - Atlans's side is **not solid rock**: 6 of the 12 tiles of 53 and all 12 of 56 are open on our
    cooked grid. `atlans-port.md:1325-1326` is wrong about this.
  - The walk from our Atlans basin is 745 steps.
- **Death and the Town Portal land in Tarkan's town** (WZ = S6). That needs one mu.db row,
  `gates (57, 8, 187, 54, 203, 69, 1)`. Without it every death goes to Lorencia, which is Devias's
  gate-22 bug again. WZ's middle tile 195,61 is a closed statue block, so the arrival is **195,65**.
- **The warp list**: WZO and S6 have "Tarkan" (gate 57) and "Tarkan2" (gate 77, WZO 96,143-100,146)
  at level 140 and 8 000 / 8 500 zen. A second row needs a multi-source flood in `settleFound`
  (C §4.3). The split it makes puts 7 959 tiles in Tarkan 1 (Mutants, Wolves) and 15 873 in Tarkan 2
  (Tantalos, Beam Knights, both bosses).
- **The look** (B §6): MU's paint is the brightest yet (walkable mean 138,135,135, neutral), with
  the warmth in the sand sheets. Pass B proposes a bright dusty noon under the house 52° sun, a warm
  sand bounce, exposure 1.15, a trace of sand dust and the storm as its own layer, with a starting
  `sheets/worlds/tarkan.json`. The orange comes from 98 pulsing lava glows (Object08), plus lava flows
  and gears in a south-west forge.
- **The objects**: 3 381 placements of 82 kinds. About 660 are rigged, 529 of them Object07's dry
  tufts. Five kinds are placed but missing from the client (Object01/04/05/36/39), and MuMain draws
  none of them either. Grass grows only on slot 0 in the town (2 144 tiles), as MU's straw tuft.
- **The extras on the engine's existing machinery:**
  - 4 hidden emitter kinds (dust, steam vents, sand geysers that throw stones, cyan glow sprites);
  - 10 Bug02 scarabs (`Scurry`, widened from 3 slots to 10);
  - slot 5's sand that slides at half MU's water speed;
  - the Dinorant flying at 90 units, as in Icarus.
- **Sound**: `desert.wav` loops on the whole map, with no wind. There are 18 monster wavs to import.
- **What it costs**: the fixed effects come to under 0.15 ms. Two lines can overdraw the 0.3 ms
  effects account: MU's sand puff on every frame of every walker (0.03-0.08 ms in a pack), and
  Inferno on every Tantalos and Beam Knight swing (0.05-0.1 ms in a pull) (C §3.7). About 100 lava
  lights are shade work, priced inside Atlans's 216.
- **Where it sits**: 72-93 is the next open-world band after Atlans's 43-74, and the only one below
  Blood Castle 6's 99. At 100x experience, about 41 Tantalos kills take a hero from 130 to 150.

## Where the passes disagree, settled

| point | the pass that differs | settled |
|---|---|---|
| Stats and town NPCs | C's "WZ" is the 2012 repack's data (WZD in A): Iron Wheel 12 000 HP, Death Beam Knight 30 000, Thompson/Guard/Amy in the town | **OM 095d = WZO** for stats; **no NPC** in any official source (A §4.2, §1.2). C's decisions 3 and 13 read with that in mind |
| The void | C §0.1 and P5 say no NoGround and no void | **51.6% NoGround, undrawn** (A §3, B §1.1). The FLAT_OVER_VOID and void-look rows apply |
| Spawn gate 57 | B's index.py line uses OM S6's 187,63 | **WZO = WZ: 187,54-203,69**, the whole safe hall |
| Slot 5 | C: `water_flow` 0.5 on the sheet | it is **sand**, not water: it needs a slide with no sheen or wet darkening (B §3), then 0.5 |
| Grass | C P4: a row only if slot 0 should not grow | B: `GRASS_BY_MAP[8] = ["TileGrass01"]`, MU's straw tuft, which only grows in the town |
| Music | B: "rare and in fights" | the code today plays a world's track in its safe zone (Atlans, the tower, Noria; C R18). Follow that, decision 8 |
| Tantalos's area blow | A: WZ's (all 63); C: no | decision 6 |

## Traps a first build would fall into

- **The death row.** No `gates` row 57 means every death in Tarkan rises in Lorencia. Check the
  `.mur` header's safe box after step 1.
- **The arrival tile.** 195,61 (WZ's middle) is a closed statue block, so use 195,65.
- **The Tab card.** `travel.cpp:195`'s dimmed "Tarkan" card vanishes the moment map 8 exists and no
  travel row names it (C R8).
- **Indoor reverb.** `underground` (no leaves, no wind, as MU) makes `indoors()` true everywhere,
  so the desert air needs its own Open room, or the sound takes the Roofed reverb (C R12).
- **Slot 5 is not water.** Given the `water` material it will shine and darken.
- **Sheet names collide.** Every Tile*, `light01`, `flag`, `statue`, `col`, `face` and the `test*`
  set clash with other worlds' sheets, so use the `tk_` prefix. `flag`, `test2`, `TileGrass01` and
  `sander` ship as both .OZJ and .OZT under one stem. Decode them apart, or one overwrites the other.
- **Buried cliffs.** Object41/40/42-44/51 stand 3-5 m down in the void. Add `tarkan` to
  `FLAT_OVER_VOID` or the buried test flags them.
- **Two warp rows.** The second row opens only on its exact landing tile until `settleFound` floods
  from every landing at once.
- **The Destruction weapons.** Sword17 and Staff09 are built as item models (2026-10-06), so the
  Tantalos and Zaikan can hold them; Staff09 is also the bosses' ring, not yet thrown.
- **Every world's tables.** After the kinds go in, all nine worlds' tables must be recooked, or the
  breeds are unknown elsewhere.
- **Another session has mu.db open** (`-shm` touched while C read it). Write it only when no session
  is live, then checkpoint.

## Decisions for the user

1. **Port a post-0.75 world?** Tarkan is 0.84, the earliest world after 0.75 (A §1.1). The game
   already carries later things (excellent items, 2nd class, Blood Castle), and `kTarkanMap` is
   waiting. If yes: *recommended* **OM 095d = WZO's numbers** (the post-July-2003 Tarkan every file
   carries), not the harder launch table, which survives only as five columns in the 0.84 note.
2. **Gate level, Atlans <-> Tarkan.** MU says 130 both ways. Our gates double MU's except Atlans,
   which was set near its monsters (in 70). *Pass C recommends* **in 100, out 70**, just over the
   breeds' 72-93, as Atlans's 70 sits over its 43-74.
3. **Second-class gear by the window** (A §5, Q3). As built, the five ordinary breeds reach only
   Grand Soul gloves, boots and helm and Divine gloves and boots. Every Dark Phoenix piece and every
   armour piece in Tarkan hangs on the two 151 s bosses, and **Dark Phoenix pants and armour never
   fall here**. Keep it as it is, raise those rows on map 8, or let map 8 ignore the lower window
   for second-class rows. This is the user's stated purpose for Tarkan, so it needs an answer.
4. **Warp rows.** None, one "Tarkan" (the hall), or both (Tarkan 2 is the hard half, and needs the
   flood fix). *Recommended*: **one row**, its level matching the gate's.
5. **The void.** MU's black cut behind the Object41 walls, Blood Castle's darkened rim, or warm
   `void_clouds` below the plateaus (ours). *Recommended*: start with MU's black plus the rim and
   judge in a shot.
6. **Infernos, sand puffs and the area blow.**
   - *Recommended*: MU's Inferno on the two bosses only, a faint small one or none for the rank and
     file, and the puffs thinned to one in four (monster auras are subtle).
   - Whether all 63 Tantalos carry WZ's one-in-five area blow: A says yes (WZ's rule), C says no
     (too much on every pull).
7. **The sandstorm.** MU's two screen layers at 0.3, or ours on top (Devias's storm streaks
   re-skinned in sand, with a calm and gust cycle). *Recommended*: **MU's first**, faint, then judge.
8. **Music.** `tarkan.mp3` in the safe hall only, as Atlans, the tower and Noria do now.
   *Recommended*.
9. **Eye trails** on every breed (MU's MoveEye ribbons, new code). *Recommended*: **yes, faint**.
   They are the breeds' look.
10. **Dinorant at 90 units** in Tarkan (MU's, shared with Icarus). *Recommended*: yes.
11. **Kundun +5.** Tarkan is MU's home for the Golden Tantalos and its ten Golden Wheels (0.97d,
    every 120 min). Nothing was ever built or re-homed (`docs/kundun-box.md:3-4`). Building
    Tarkan's bodies builds theirs. *Recommended*: re-home +5 here when the boxes are built, but not
    as part of this port.
12. **Sevina's class change.** Restore MU's "Atlans, the Lost Tower and Tarkan" in her words, and
    add map 8 to the treasure ground (MU's 62-76 band means Mutants and Wolves). *Recommended*: the
    words and the treasure ground, not the hunt.
13. **Town NPCs.** None, as every official source. Or ours, as Atlans got Baz, since a potion
    seller is otherwise a long walk or the M window away. *Recommended*: **none** to start.
14. **Smaller calls:**
    - Loch's Feather in Tarkan (MU's until 0.98c): *recommended* yes.
    - The Sword and Staff of Destruction as props only, for now.
    - The Season-only Kanturu gate (Object86-88): a sealed ruin or hidden.
    - Slot 5's sliding sand: needs a dry-slide path, or still sand.
    - Bugs: the crawlers first, their trails after judging.

## The build, in order

The detail, with checks and what is seen at each step, is C §6. Each step ends in a muted shot or run.

0. *(optional)* The air as a `MapRow` field in place of the four name-keyed bools. No behaviour
   change; the headless logs stay byte-identical.
1. **Bare land**:
   - the terrain.py and index.py rows (`HIDDEN {60,63,64,70,76,83}`, `OPERABLE {78}`,
     `GRASS ["TileGrass01"]`, `BLEND_MESH`, gate boxes for 8 and Atlans's 7);
   - World9 into `source/world/tarkan`, `tk_` sheets and `ground.json`;
   - mu.db gate 57;
   - `maps.cpp {"tarkan", 8, {195, 65}, true}`, the arrival card, the minimap name, and the
     `tarkan.json` sheet.
2. **The gates** 53-57 and `testTarkanGates`.
3. **The air and the haze**: `desert.wav` in an Open room, the safe-hall music, and the two sand
   layers.
4. **Objects** in small batches: cliffs and tufts first, then the braziers with their lamps, the
   streaming and bright kinds, the chrome orb, the rag and the seats.
5. **Hidden emitters**: smoke, vents, geysers and glow sprites.
6. **The bugs.**
7. **Monsters**: the 7 kinds and 217 spawns, every world's tables, then figures one batch at a time:
   - (a) Mutant + Wolf, with puffs and eye trails;
   - (b) Iron Wheel;
   - (c) Beam Knight;
   - (d) Tantalos, after Sword17;
   - (e) the two bosses, after Staff09.
8. **Travel rows**, and the flood fix if there are two.
9. **The side hooks**, per the decisions: Dinorant lift, feather, Sevina, Kundun +5, NPCs, and
   the mount card text.

## Side findings (outside Tarkan)

- **`atlans-port.md:1325-1326`** (Part C §0) says the Atlans side of the Tarkan door is "solid
  rock". It is half open on our grid, as its own Part A (`:425-427`) says.
- **`kAtlansGearFromLevel` 43** (`src/sim/items.h:388-397`, `docs/second-class-gear.md:97`) names
  "Valkyrie 46, Vepar 45, Bahamut 43" as Atlans's strongest. They are its weakest, so every Atlans
  kill rolls the second-class gear. Either the wording or the intent is off.
- **atlans-port §4.2's experience figures** leave out the bonus term for levels 65 and up
  (`rules.cpp:353-358`): the Hydra is 3 284, not 3 052.
- **`GATE_BOXES_BY_MAP`** (`pipeline/index.py:57-71`) still lacks maps 4 and 11. **`OPERABLE_BY_MAP`**
  is still two copies (terrain.py and index.py).
- **`tools/bot/bot.cpp:68-74`** knows five worlds: no Atlans and no Blood Castle.
- **OM 095d is not a complete 0.95d**: its Tarkan and Icarus are unreachable, and it lacks the Black
  Dragon set.

---

## Part A: the record

Research pass A, 2026-10-05. Nothing in the repo was edited, cooked or run; no window was opened.

Paths are as in `docs/lost-tower-port.md` and `docs/atlans-port.md`:

- **OM**: OpenMU init, `LEGACY/reference/openmu/src/Persistence/Initialization/`; **OMGL** its `GameLogic/`.
- **MM**: MuMain source, `LEGACY/reference/MuMain/src/source/`; **MMD** (= **D**) its `src/bin/Data/`.
- **WZ**: WebZen GameServer 1.00.93 C++, `Source/Server Side/GameServer/` of github
  `ptr0x-real/Mu-GS-Webzen-MC-10093`. A full clone is already on disk at
  `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/9142efc4-e29e-4d88-9ee6-bb4831450426/scratchpad/wz/`
  (no new clone was made). Its files are CP949; grep them with `LC_ALL=C grep -a`.
- **WZD**: the same clone's `_Server Files/Data` (the 2012 repack), trusted for nothing here.
- **WZO**: WebZen's official 0.99.60T server data (`Monster.txt` 1.00m 2005-03-07,
  `MonsterSetBase.txt` 0.99 2005-01-20, `gate.txt` 0.99 2004-08-16, `movereq(kor).txt` 0.98n
  2004-03-29, `MoveLevel.txt` 0.99 2004-07-23, `item(Kor).txt` 1.00m, `Quest(Kor).txt` 2003-11-13),
  at the Atlans pass's `.../6a47dc36-.../scratchpad/atlans/a/w996/Files/Data/`.
- New to this pass: **KR** = WebZen Korea's own 2002-2003 notices: the 0.84 patch note
  (muonline.co.kr `update_patch` guid 291, via the Wayback Machine) and two WebZen press releases
  (company.webzen.com seq 2109 and 2147), plus the community history **MuHistory**
  (github `AlighieriDemiurgs/MuHistory` README, which cites them). Saved as text in `a/`.

Scratch (`scratchpad/tarkan/a/`): `grids.py` (decodes eight Tarkan attribute files with a copy of
`pipeline/terrain.py`'s cipher + BuxCode), `lab.py` (the Atlans pass's 8-neighbour flood fill),
`sets.py` / `cmp.py` (OM, WZO, WZD map-8 spawn rows), `regions.py` / `more.py` (regions, gates,
walks, `map_out.txt`), `atlansdoor.py` (the Atlans side of the door, on our cooked grid),
`items.py` (WZO item levels), `drops.py` (our drop pools), `press.py`; outputs `regions_out.txt`,
`map_out.txt`, `drops_out.txt`, `om_rows.txt`, `wzo_map8.txt`, `wzd_map8.txt`, `items_wzo.txt`,
`patch291.txt` (the 0.84 note), `press446.txt`, `press448.txt`, `muhistory.md`.

Compass as in atlans-port: x grows east, y grows south. In `a/map_out.txt` the top row is y 252.

### 0. The one thing to know first

**Tarkan is not 0.75. It is the next world after it: WebZen put "죽음의 사막 타르칸" (Tarkan, the
Desert of Death) on the Korean test server on 2002-12-29 and on the live servers with patch 0.84
on 2003-01-07**, about seven weeks after Korea's 0.75 (between 0.74 of 2002-10-22 and 0.77 of
2002-11-21). The same patch brought the **excellent items**, the Jewel of Life, the Black Dragon
set, the three Destruction weapons, Inferno and Blast. Everything this game already carries from
after 0.75 came later than Tarkan: Icarus, the class change and the 2nd wings (2003-07), the Box
of Kundun (2003-08), Blood Castle (2003-09). Evidence in §1.1; it is decision #1.

**One map, one region, a safe town in the north-east and no NPC in it.** Entered only from
Atlans's south-west lagoon ("the Hydra zone"), 745 steps from the Atlans basin, at level 130
(140 at launch). 217 single-tile monsters of seven breeds, levels 72-93, with the level climbing
cleanly with the walk from the town; one Zaikan and one Death Beam Knight are the bosses.

**The sources agree almost everywhere.** OM 095d's spawn table is WZO's (and WZD's) row for row,
217 of 217 in the same order; OM 095d's stats are WZO's 1.00m numbers to the digit. The client
grid is WZO's but for 133 tiles round a Season 6 door. What OM 095d lacks is the way in: **it
defines Tarkan with no gate, no warp row and no spawn gate**, so in OM 095d the map cannot be
reached and a death there goes to Lorencia. The gates, the warp rows and the death and portal
rules come from WZO, WZ and Season 6, which agree with each other.

**The launch numbers were harder.** The 0.84 note lists Tarkan's breeds 4-40% stronger than OM
and WZO carry; WebZen's 2003-07-01 release says the worlds' monsters were then moved, multiplied
and made easier. OM 095d and WZO are the post-July-2003 Tarkan.

### 1. Map

#### 1.1 Is it 0.75? (decision #1: the evidence)

**OpenMU.**
- **Version075** has eight maps and no Tarkan: `OM/Version075/GameMapsInitializer.cs:30-37`
  (Lorencia, Dungeon, Devias, Noria, Lost Tower, Exile, Arena, Atlans). No Tarkan file exists
  under `OM/Version075/`.
- **Version095d** adds it: `OM/Version095d/GameMapsInitializer.cs:38` `typeof(Tarkan)`, and
  `OM/Version095d/Maps/Tarkan.cs:28` `MapNumber => 8`, `:31` `"Tarkan"`. The class carries spawns
  (`:34-252`) and seven breeds (`:256-484`) and nothing else: no NPC spawns, no drop groups, no
  terrain prefix (so it reads `Resources/Terrain9.att`, the Season 6 file).
- **Version095d's gates have no Tarkan at all** (`OM/Version095d/Gates.cs:111-166` exits,
  `:192-216` enters, `:39-56` warps, which stop at LostTower7; `:41` "todo: update for 0.95d").
  With no spawn gate on map 8, `SafezoneMapNumber` falls to Lorencia
  (`OM/BaseMapInitializer.cs:91`).
- **Version097d** is not a full version in OM: it holds only `Items/Jewels.cs` ("Jewels for MU
  Version 0.97d", `OM/Version097d/Items/Jewels.cs:11`). OM's selectable versions are season6,
  0.75 and 0.95d (`LEGACY/reference/openmu/src/Startup/Readme.md:53`).
- **SeasonSix** reuses the 095d class unchanged (`OM/VersionSeasonSix/GameMapsInitializer.cs:38`
  `typeof(Version095d.Maps.Tarkan)`) and adds the gates and warps (§2).

**WebZen's own record (KR).**
- **Press release, 2002-12-29** (`a/press446.txt`; company.webzen.com seq 2109, cited by
  MuHistory ref 446): "뮤의 일곱번째 월드인 '죽음의 사막-타르칸'" -- MU's seventh world, the
  Desert of Death Tarkan -- "지난 5월 추가된 수중월드 '아틀란스' 이후" (after the underwater world
  Atlans added in May), joining Atlans and the underground empire 'Kantur' (planned for 2003 H1);
  "타르칸은 수중월드 아틀란스의 히드라존과 연결되어 있으며, 레벨 제한은 140이다. 이동 명령어를
  사용하기 위해서는 150레벨 이상" -- connected to Atlans's Hydra zone, level limit 140, /move from
  150; the Jewel of Life, excellent items, Sword of Destruction, Staff of Destruction, Saint
  Crossbow, Blast and Inferno with it; "테스트서버를 통해 우선 적용 ... 금주 내에 본서버" (on the
  test server first, live within the week).
- **Patch note 0.84, 2003-01-07 13:09** (`a/patch291.txt`; muonline.co.kr update_patch guid 291
  via web.archive.org/web/20040218155953, MuHistory ref 16): "[패치안내]금일 본서버에 적용된
  0.84패치 내용". Item 1: "죽음의 사막 타르칸이 본서버에 패치됩니다. 레벨제한은 걸어서 이동할 경우
  140 레벨, 이동 명령 사용시 150 레벨 이상 ... 게이트는 아틀란스 히드라존에 위치". It lists the
  breeds' adjusted numbers (§4.2), then excellent items (item 2), the Black Dragon set and the
  Destruction weapons (3), Inferno, Blast and Summon Soldier (4), the Jewel of Life (5), and
  fixes "유니리아 타고 아틀란스로 이동되는 버그" (riding a Uniria into Atlans) (11.4).
- **Press release, 2003-07-01** (`a/press448.txt`; seq 2147, MuHistory ref 448): with Icarus and
  the class change, "'죽음의 사막-타르칸'은 레벨 제한이 140에서 130으로 낮아졌으며, 아틀란스의 경우
  이동 명령어 사용 가능 레벨이 100에서 70" and "빠른 레벨 업을 위해 몬스터의 위치를 조절하고 수를
  증가시키는 한편 난이도를 하향 조정" (monsters moved, more of them, difficulty lowered).
- **MuHistory** (`a/muhistory.md:278-304`): 0.74 22.10.2002, 0.7? 05.11 and 16.11.2002, 0.77
  21.11.2002, then "**0.84 aka 0.84.0 (07.01.2003)**: Added map Tarkan. Entry level requirement of
  140 ... Introduction of Excellent items". It has no dated 0.75 entry; it calls the 0.75 client
  "the publicly available 0.75 KOR protocol client" (`:224`), the one OM's Version075 targets.
  Later dates from the same file: Devil Square 0.89c 11.02.2003 (`:318`), Icarus and the 140->130
  cut 0.94b 06.06.2003 (`:350-352`), Box of Kundun 0.95 06.08.2003 (`:370`), Blood Castle 0.96
  04.09.2003 (`:406-408`), Global MU on 0.95d in December 2003 (`:456`).

**The cross-check versions.** 0.75: absent. 0.95d (Global): present in OM, without its gates.
0.97d: present in WZ (`WZ define2.h:4045` `MAP_INDEX_TARKAN = 8`, `WZ MapClass.cpp:201`,
`WZ protocol.cpp:17942-17945`, `WZ EledoradoEvent.cpp:503-571`) and WZO (0.98n-1.00m). Season 3 /
Season 6: present (WZ's S3 code paths, OM S6, MuMain `WD_8TARKAN`,
`MM/World/MapInfra/MapManager.h:17`; data `D/World9` + `D/Object9`).

**What this game already carries from after 0.75** (for weighing, not a recommendation): excellent
items (0.84, Tarkan's own patch), the Scroll of Inferno (0.84; `source/items/misc/Book14.json`
says "0.95d's and not 0.75's"), the class change and 2nd wings (2003-07), Blood Castle (0.96,
2003-09), the Rune/Jewel of Creation (0.94b). Tarkan is the earliest world MU added after 0.75.

#### 1.2 Map facts

- WZ: `MAP_INDEX_TARKAN = 8` (`WZ define2.h:4045`), regen box **`gRegenRect[8] = 187,54-203,69`**,
  commented "사막" (desert) (`WZ MapClass.cpp:201`). MuMain `WD_8TARKAN`, data `D/World9`,
  `D/Object9` (world index + 1).
- **Exp multiplier 1**, no map requirement (`OM/BaseMapInitializer.cs:119`; Tarkan.cs overrides
  nothing). **No drop groups** of its own.
- **NPCs: none** in OM 095d (no `CreateNpcSpawns`), none in OM S6 (the same class), **none in WZO**
  (its NPC section has no map-8 row; its map-8 rows are the 217 monsters,
  `WZO MonsterSetBase.txt:856-1076`). WZD adds Thompson 231 at 191,74, Baz 240 at 197,58 and Amy
  253 at 205,62 (`WZD MonsterSetBase.txt:36-38`); the repack's.
- The town is a safe zone with nothing in it. MuMain lets a hero sit on object 78 there
  (`MM/Engine/Object/ZzzInterface.cpp:1743-1748`).

#### 1.3 One grid, one region

Eight files decoded (`a/grids.py`, `a/regions.py`). Walkable = `(flags & (NoMove|NoGround)) == 0`.

| grid | walkable | regions | safe tiles (walkable) | safe box |
|---|---:|---:|---|---|
| client `D/World9/EncTerrain9.att` (2-byte) = `EncTerrainTest9.att` = `Terrain.att` (plain 2-byte) | 23 832 | **1** | 1 872 (1 107) | x 155-250, y 34-97 |
| WZO `Terrain9.att` (2005) = WZD = `D/World9/Terrain9.att` (3-byte XOR) | 23 791 | 1 | 1 872 (1 107) | same |
| OM `Resources/Terrain9.att` (S6, plain) | 24 934 | 3 (24 404; 511 in rows 254-255; 19 at x 167-171, y 34-38) | 1 698 (1 698) | x 155-251, y 34-97 |
| `D/World9/TerrainServer.att` (plain) | 24 741 | 26 | 4 255 (1 715) | odd bits (below) |

- **client vs WZO: 133 tiles** (87 client-open, 46 WZO-open), in three clusters at x 2-53,
  y 194-234, round Season 6's Kanturu door (gate 128, 7,199-7,201). The Atlans grid had the same
  shape: WZO's map but for a Season 6 door.
- client vs OM S6: 1 102; OM S6 vs TerrainServer.att: 193. OM's S6 file is a server file opened
  wider (every safe tile walkable), not the client's.
- `TerrainServer.att` carries bit 2 on 2 921 tiles, mostly on NoGround/NoMove tiles (values 10,
  11, 14, 15): not the Atlans 075 dump's "standing" marks. Flag only.
- **The safe zone is one region** of 1 107 walkable tiles: the town (WZ's regen box 187,54-203,69
  in it) and a safe corridor south-east to the arrival box from Atlans (248,40-251,44). The
  arrival is 54 steps from the town's middle (195,62), all on safe tiles.

**Settled: the client grid**, as every earlier world: WebZen's 2005 map but for the Season 6 door,
and every spawn and gate box below is open on it.

The shape (`a/map_out.txt`): the town in the north-east; Mutants round it to the west and south
(x 132-205, y 15-155); a northern belt of Bloody Wolves and Iron Wheels (x 21-181, y 26-123); the
west and the whole south held by Tantalos and Beam Knights (x 5-183, y 87-246); the Zaikan in the
far south-west corner (11,241) and the Death Beam Knight in a southern pocket (161,225). Nothing
spawns east of x 205 or south-east of the town's column. The farthest open tile is 279 steps from
the town.

### 2. Gates and warps

Directions are OpenMU's (`OMGL/DirectionExtensions.cs:21-35`).

#### 2.1 The door from Atlans

| gate | flag | map | box | to | dir | level | WZO gate.txt | OM S6 Gates.cs | WZD |
|---:|---|---|---|---|---|---:|---|---|---|
| **53** | enter | Atlans | 14,225 - 15,230 | 54 | -- | **130** | l.102 | `:524` | l.95, 130 |
| **54** | exit | Tarkan | 248,40 - 251,44 | -- | 7 North | -- | l.103 "아틀란스 -> 타르칸" | `:200` | l.96 |
| **55** | enter | Tarkan | 246,40 - 247,44 | 56 | -- | **130** | l.105 | `:525` | l.98, 130 |
| **56** | exit | Atlans | 16,225 - 17,230 | -- | 3 South | -- | l.106 "타르칸 -> 아틀란스" | `:194` | l.99 |
| **57** | spawn | Tarkan | WZO **187,54**-203,69 / OM S6 **187,63**-203,69 | -- | 0 | (60) | l.108 "타르칸(안전지대)" | `:198` (spawn gate) | l.101 |

- **Not in OM 095d** (above). Both enter gates ask 130 in every source that has them; the way back
  to Atlans asks 130 too. WZ gives the MG and DL two thirds (`WZ user.cpp:27545-27567`); no class
  of ours pays less. KR: 140 at launch (2003-01-07), 130 from 2003-07 (§1.1).
- **Open tiles** (client grid): 54 12 of 20 (19 safe); 55 9 of 10 (all safe); 57 OM box 118 of
  119, WZO box 253 of 272, all safe.
- **The Atlans side is not solid rock.** atlans-port Part C says gate 53's box "is solid rock
  here". On our cooked `source/world/atlans/attributes.png`, on the client `EncTerrain8.att`, on
  OM 075/S6 and on WZD alike, **column x 14 is NoMove (4) and column x 15 is open: 6 of the 12
  tiles of gate 53 are walkable**, and all 12 of exit 56 (16-17, 225-230) are open
  (`a/atlansdoor.py`). It sits in the SW lagoon beside the Hydras at 24,227 and 19,230
  (atlans-port §4.1): KR's "Hydra zone". **Walk from our Atlans basin (21,17) to it: 745 steps**,
  the far end of Atlans.
- Gate 128 (Tarkan 7,199-7,201, from Kanturu Ruins gate 127, `OM/VersionSeasonSix/Gates.cs:201,
  538`) is Season 6 only.

#### 2.2 Warp list

- 0.75 and 0.95d: no Tarkan row (`OM/Version095d/Gates.cs:42-55`).
- **WZO `movereq(kor).txt:31-32`** (0.98n): **Tarkan 8 000 zen, level 140 -> gate 57; Tarkan2
  8 500, level 140 -> gate 77.** OM S6 the same (`OM/VersionSeasonSix/Gates.cs:67-68`). WZD
  likewise (`movereq(Kor).txt:30-31`).
- **Gate 77, Tarkan2**: WZO **96,143-100,146**, flag 0, level 130 (`gate.txt:144`); OM S6
  **91,160-93,161** (`Gates.cs:199`). Both open, not safe, in the Tantalos band (172 and 188 steps
  from the town); WZO's has 9 Tantalos and 2 Beam Knights within 20 tiles.
- WZO `MoveLevel.txt:13`: map 8 at **130**. KR: /move 150 at launch.
- MU2_BGFX's M key is free and opens rows on arrival (lost-tower-port Decision 1);
  `src/game/ui/travel.cpp:195` lists Tarkan (8) among the dimmed "coming" worlds.

### 3. Terrain attributes

| file | 0 | 1 safe | 2 | 4 NoMove | 5 | 8 NoGround | 9 | 12 | 13 | walkable |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| client `EncTerrain9.att` | 22 724 | 1 107 | 1 | 7 284 | 591 | 15 161 | 28 | 18 494 | 146 | 23 832 |
| WZO / WZD / `D/World9/Terrain9.att` | 22 683 | 1 107 | 1 | 7 181 | 591 | 23 758 | 28 | 10 041 | 146 | 23 791 |
| OM `Terrain9.att` (S6) | 23 236 | 1 698 | -- | 7 244 | -- | 33 358 | -- | -- | -- | 24 934 |

- **63.6% of the map is void (NoGround)** on the client grid; 36.4% walkable, 1.7% safe-and-open.
- The client and WZO differ in the NoGround/NoMove split on 8 694 tiles (8 vs 12: void marked also
  NoMove), but in walkability only on the 133 door tiles.
- One "2" tile (character standing) in every non-OM file; no baked spawn snapshot here (unlike
  Atlans's 075 file). No other safe area than the town.

### 4. Monsters

#### 4.1 Spawns -- one table in every source

`OM/Version095d/Maps/Tarkan.cs:36-252`: **217** single-tile `CreateMonsterSpawn(id, def, x, y)`,
ids 100-316, no direction, no doubled tile (`a/om_rows.txt`). **WZO `MonsterSetBase.txt:860-1076`
has the same 217 rows in the same order** (type 2, distance 30, dir -1); WZD's type-2 rows are the
same again (`a/cmp.py`: om == wzo == wzd). Every spot is open and non-safe on all eight grids.

| # | breed | spots | where (client grid) | walk from the town (min / median / max) | OM lines |
|---:|---|---:|---|---|---|
| 62 | Mutant | **41** | round the town, x 132-205, y 15-155 | 43 / 70 / 93 | `:42-88` (interleaved) |
| 60 | Bloody Wolf | **32** | the northern belt, x 74-181, y 26-123 | 48 / 87 / 121 | `:57-159` |
| 57 | Iron Wheel | **23** | the north-west, x 21-123, y 50-100 | 72 / 133 / 174 | `:91-252` |
| 58 | Tantalos | **63** | the west and south, x 8-176, y 87-244 | 146 / 184 / 273 | `:38-251` |
| 61 | Beam Knight | **56** | the south, x 5-183, y 125-246 | 153 / 245 / 276 | `:36-248` |
| 59 | **Zaikan** | **1** | 11,241, the SW corner (6 Beam Knights, 3 Tantalos within 20) | 270 | `:40` |
| 63 | **Death Beam Knight** | **1** | 161,225, a southern pocket (7 Beam Knights, 2 Tantalos within 20) | 270 | `:222` |
| | **total** | **217** | | | |

By walk from the town: 0-60 steps 10 Mutants and a Wolf; 60-120 Mutants, Wolves, 7 Wheels;
120-180 Wheels and Tantalos; 180-240 Tantalos and Beam Knights; 240-280 Beam Knights, the Zaikan
and the Death Beam Knight. **The level climbs with the walk, 72 at the gate to 93 at the far end,
with no gap** (unlike Atlans's 46 -> 66 jump).

**Flags:**
- WZ type-2 rows respawn on a random open non-safe tile within ±3 (`WZ MonsterSetBase.cpp:133-152`);
  MU2_BGFX keeps OM's tile and draws the respawn tile anew (webzen-audit #13).
- WZD adds five "spot" rows (`WZD MonsterSetBase.txt:128-132`): 6 Beam Knights, 6 Tantalos,
  12 Mutants, 6 Bloody Wolves. The repack's.
- **The table is post-July-2003.** KR's release of 2003-07-01 says the monsters were moved and
  multiplied then; WZO's file is dated 2005-01. The 0.84 launch table is not on disk.

#### 4.2 Breeds (`OM/Version095d/Maps/Tarkan.cs:256-484`; WZO `Monster.txt:55-74`)

Columns as in atlans-port §4.2. Resistances OM `n/255` as n (P I W F). Every breed: `Attribute = 2`,
`NumberOfMaximumItemDrops = 1`, move range 3, view 7, move 400 ms, **attack 1400 ms**.

| # | name | lvl | HP | dmg | def | atk / def rate | atk range | respawn | skill (OM) | P I W F | OM line |
|---:|---|---:|---:|---|---:|---|---:|---:|---|---|---|
| 62 | Mutant | 72 | 10 000 | 250-280 | 190 | 365 / 120 | 1 | 10 s | -- | 8 8 8 8 | `:424` |
| 60 | Bloody Wolf | 76 | 13 500 | 260-300 | 200 | 410 / 130 | 2 | 10 s | -- | 8 8 8 8 | `:359` |
| 57 | Iron Wheel | 80 | 17 000 | 280-330 | 215 | 446 / 150 | **4** | 10 s | -- (shoots) | 9 9 9 9 | `:261` |
| 58 | Tantalos ("Tantallos") | 83 | 22 000 | 335-385 | 250 | 500 / 175 | 2 | 20 s | `MonsterSkill` | 9 9 9 9 | `:293` |
| 61 | Beam Knight | 84 | 25 000 | 375-425 | 275 | 530 / 190 | 4 | 20 s | **Energy Ball** | 10 10 10 10 | `:391` |
| 59 | **Zaikan** | 90 | 34 000 | 510-590 | 400 | 550 / 185 | 5 | **150 s** | `MonsterSkill` | 13 13 13 15 | `:326` |
| 63 | **Death Beam Knight** | 93 | 40 000 | 590-650 | 420 | 575 / 220 | 5 | **150 s** | `MonsterSkill` | 13 13 13 17 | `:456` |

**WZO's official 1.00m `Monster.txt` agrees with every number above, respawn included** (RegTime
10 / 10 / 10 / 20 / 20 / 150 / 150; lines 55, 57, 59, 61, 62, 73, 74), and adds:

| | Mutant | Bloody Wolf | Iron Wheel | Tantalos | Beam Knight | Zaikan | Death Beam Knight |
|---|---|---|---|---|---|---|---|
| A.Type | 0 | 0 | 0 | **150** | **17** | **150** | **150** |
| ItemRate / MoneyRate / MaxItemLevel | 170 / **30** / 3 | 170 / 30 / 3 | 160 / 30 / 3 | 160 / 30 / 3 | 160 / 30 / 3 | 160 / 30 / 3 | **130** / 30 / 3 |

- WZO files the Zaikan and the Death Beam Knight under **"//보스" (bosses)**, beside the Ice Queen,
  Gorgon, Balrog and Hydra (`Monster.txt:68-74`). The other five sit in the main list.
- **MoneyRate 30**: every Tarkan breed, against 14 for Atlans and the Lost Tower. The only other
  rows at 30 are the golden pair (82, 83), the Cursed King (66) and two later monsters (293, 295).
- **A.Type 150** (WZ): one blow in five is the area spell, Flame of Evil, on every player within
  5 tiles, and the monster closes to attack range + 2 (`WZ gObjMonster.cpp:631-634, 1738-1752,
  1849-1925`). **The ordinary Tantalos has it**, all 63 of them, not only the bosses. OM 095d
  never creates `MonsterSkill` (`OM/Version095d/SkillsInitializer.cs` has none), so in OM all
  three only melee. **A.Type 17** is Energy Ball (`SkillsInitializer.cs:60`, range 4).

**Launch numbers, KR 0.84 note (2003-01-07, "adjusted this afternoon"):**

| | lvl | HP | dmg | def | vs OM/WZO |
|---|---:|---:|---|---:|---|
| Mutant | **76** | 14 000 | 270-330 | 220 | +4 levels, +40% HP |
| Bloody Wolf | **78** | 17 000 | 300-360 | 240 | +2, +26% |
| Iron Wheel | 80 | 20 000 | 330-390 | 260 | +18% |
| Tantalos | **82** | 24 000 | 360-430 | 280 | -1 level, +9% |
| Beam Knight | 84 | 28 000 | 400-470 | 300 | +12% |
| Zaikan | 90 | 35 000 | 520-610 | 400 | +3% |
| Death Beam Knight | 93 | 41 000 | 600-700 | 440 | +3% |

The same note cut Atlans's Hydra's top damage from 330 to 310 (OM/WZO's 310).

**Experience per kill** (`src/sim/rules.cpp:345-361`, `(L+25)·L/3`, plus `(L-64)·L/4` from 65,
`·1.25`; times `kExperienceRate` 100, `rules.h:355`): Mutant 3 090 (309 000), Bloody Wolf 3 483,
Iron Wheel 3 900, Tantalos 4 228, Beam Knight 4 340, Zaikan 5 044, Death Beam Knight 5 415
(541 500). Per hit point the Mutant pays best (0.31), the bosses worst (0.14).

**Bosses.** WZO names them: the Zaikan and the Death Beam Knight (single spots, 150 s, resist
13/15-17, A.Type 150), and MuMain draws boss effects for both (§4.3). No boss flag in OM.

#### 4.3 What MuMain draws (`MM/Engine/Object/ZzzCharacter.cpp`; body `D/Monster/Monster{MODEL+1}.bmd`, `MM/Engine/Object/ZzzOpenData.cpp:2549-2561`; models `MM/Core/Globals/_enum.h:4191-4195`, monsters `:4432-4438`, golden `:4457-4458`)

| # | body | scale | arms | notes | lines |
|---:|---|---:|---|---|---|
| 62 Mutant | **Monster46** (MODEL_MUTANT) | 1.5 | -- | two `BITMAP_JOINT_ENERGY` trails; sand smoke while walking | `:13703-13711`, `:5836-5838` |
| 60 Bloody Wolf | **Monster44** | **2.2** | -- | energy trails; sand smoke | `:13730-13737`, `:5955-5957` |
| 57 Iron Wheel | **Monster42** (MODEL_GOLDEN_WHEEL) | 1.4 | **Aquagold Crossbow** | walk at 0.18; shoots **three** `MODEL_ARROW_BOMB`, straight and ±20°; sand smoke | `:13759-13768`, `:1859-1878`, `ZzzOpenData.cpp:2632` |
| 58 Tantalos | **Monster43** (MODEL_TANTALLOS) | 1.8 | **Sword of Destruction** | BlendMesh 2, light 1; attacks at 0.35; `CreateInferno` on its blow; fire particles off the body | `:13738-13757`, `:1880-1898`, `:5960-5977` |
| 59 Zaikan | Monster43 | **2.1** | **Staff of Destruction** | SubType 1; the metal/chrome gilding pass at brightness 0.5; its boss blow a ring of 18 `MODEL_STAFF_OF_DESTRUCTION` | `:13738-13757`, `:1889-1895`, `:8716-8721` |
| 61 Beam Knight | **Monster45** (MODEL_BEAM_KNIGHT) | 1.5 | -- | Energy Ball beams from the hands (six joints); `MODEL_SKILL_INFERNO` on its blow; die at 0.3 | `:13712-13729`, `:1808-1846`, `:2152ff`, `:5093` |
| 63 Death Beam Knight | Monster45 | **1.9** | -- | BlendMesh -2, light 1; a burning body (flame particles along the bones); Inferno on its blow; boss blow: Blasts scattered within 400 units and a ring of 18 Staff of Destruction | `:13712-13729`, `:1810-1835`, `:5843-5950` |

`Monster42-46.bmd` are all in `MMD/Monster/`. Sounds (`ZzzOpenData.cpp:3679-3716`): `iron1`,
`iron_attack1` (Wheel); `jaikan1/2`, `jaikan_attack1/2`, `jaikan_die` (Tantalos/Zaikan, plus skins
`Monster/bv01_2.jpg`, `bv02_2.jpg`); `blood1`, `blood_attack1/2`, `blood_die`; `death1`,
`death_attack1`, `death_die` (Beam Knights); `mutant1/2`, `mutant_attack1`. All present in
`MMD/Sound/`.

**The golden pair uses the same bodies** (`:13544-13567`): Golden Tantalos (82) on Monster43 at
1.8 with an excellent (63) Sword of Destruction; Golden Wheel (83) on Monster42 at 1.4 with an
excellent Aquagold Crossbow; both with the energy trails and the gilding passes (`:8716-8723`).
Building Tarkan's bodies builds theirs.

#### 4.4 What MU2_BGFX has

`sqlite3 -readonly source/mu.db`: `monster_kinds` holds 0-41, 45-52 (Atlans, minus the Sea Worm),
84-130, 132-134, 150; **none of 57-63, none of 82-83**. `monster_spawns` maps 0, 1, 2, 3, 4, 7, 11
(Atlans 336). `gates` rows 17, 22, 27, 42, 49; no 57. `items` (the 0.75 list) stops at the Chaos
weapons (75). `source/monsters/` has no Mutant, Wolf, Wheel, Tantalos or Beam Knight body.
`attack_skill` 17 (Energy Ball) is carried by the Vepar; 50 (Flame of Evil) by `kBosses`
(`src/sim/realm_tuning.h:342` `{35, 38, 49}`).

Rows to add, in the Atlans form:

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (62,'Mutant',           72,10000,250,280,190,3,1,7,400,1400,365,120, 10,NULL),
 (60,'Bloody Wolf',      76,13500,260,300,200,3,2,7,400,1400,410,130, 10,NULL),
 (57,'Iron Wheel',       80,17000,280,330,215,3,4,7,400,1400,446,150, 10,NULL),
 (58,'Tantalos',         83,22000,335,385,250,3,2,7,400,1400,500,175, 20,50),
 (61,'Beam Knight',      84,25000,375,425,275,3,4,7,400,1400,530,190, 20,17),
 (59,'Zaikan',           90,34000,510,590,400,3,5,7,400,1400,550,185,150,50),
 (63,'Death Beam Knight',93,40000,590,650,420,3,5,7,400,1400,575,220,150,50);
-- kDropRates: {62,30,3,10},{60,30,3,10},{57,30,3,10},{58,30,3,20},{61,30,3,20},{59,30,3,150},
--   {63,30,3,150}  -- WZO MoneyRate/MaxItemLevel/RegTime; respawn = RegTime + 1 (11 / 21 / 151 s).
-- kResistances {number, ice, poison}: {62,8,8},{60,8,8},{57,9,9},{58,9,9},{61,10,10},{59,13,13},{63,13,13}
--   (fire 15 / 17 on the two bosses if a fire resistance is carried).
-- kBosses += 58, 59, 63 (A.Type 150; 58 is no boss but has the blow).
-- monster_spawns: 217 one-tile rows on map 8 (Tarkan.cs:36-252); gates: (57, 8, 187,54,203,69, 1).
-- OM spells it "Tantallos" (as MuMain's enum); WZO "탄탈로스"; the name is the user's pick.
```

### 5. Drops

**Nothing Tarkan-specific in OM or WZO** beyond the breeds' rates: no drop groups
(`Tarkan.cs`), `NumberOfMaximumItemDrops = 1`, MaxItemLevel 3, so MU2_BGFX's window
(`src/sim/realm_items.cpp:1239-1254`: drop level in [L-15, L], plus `(L-DL)/3` no more than
maxPlus) is **[L-11, L]**, +0..+3.

**The game's own wiring is already in place.** `sim::kTarkanMap = 8` (`src/sim/items.h:388-397`)
and `secondGearHere` (`src/sim/realm_items.cpp:1130-1137`) admit the second class's gear into the
pools only on map 8 and in Blood Castle 6 (`docs/second-class-gear.md:89-100`; the user,
2026-10-04: "there will be tarkan map where 2nd class drop"). A port switches it on with no code.

**What each breed reaches today** (cooked rows, `a/drops_out.txt`; [2] = second class only):

| killer | L | window | pool |
|---|---:|---|---|
| Mutant | 72 | 61-72 | Bill of Balrog 63, Orb of Penetration 64, Crystal Morning Star 66, Bluewing 68, Staff of Resurrection 70, Aquagold 72, Crystal Sword 72; Grand Soul Gloves 70 [2], Divine Gloves 72 [2] |
| Bloody Wolf | 76 | 65-76 | + Scroll of Aqua Beam 74, Grand Soul Boots 76 [2] |
| Iron Wheel | 80 | 69-80 | the same from 70 |
| Tantalos | 83 | 72-83 | Aquagold, Crystal Sword, Aqua Beam; Divine Gloves, Grand Soul Boots, Divine Boots 81, Grand Soul Helm 81 [2] |
| Beam Knight | 84 | 73-84 | Aqua Beam; Grand Soul Boots, Divine Boots, Grand Soul Helm [2] -- **4 rows** |
| Zaikan | 90 | 79-90 | Scroll of Inferno 88; Divine Boots/Helm/Pants, Grand Soul Helm/Pants, **Dark Phoenix Gloves 86** [2] |
| Death Beam Knight | 93 | 82-93 | Inferno; Divine Helm/Pants/Armor 92, Grand Soul Pants/Armor 91, Celestial Bow 92, **Dark Phoenix Gloves/Helm 92/Boots 93** [2] |

**So the second class's gear is mostly the two bosses'.** The five ordinary breeds (216 spots)
reach only Grand Soul gloves/boots/helm and Divine gloves/boots. Every Dark Phoenix piece the
knight can get in Tarkan (gloves 86, helm 92, boots 93) and the Divine and Grand Soul armour come
only from the Zaikan and the Death Beam Knight, one each, 151 s. **Dark Phoenix Pants (96) and
Armor (100) never fall in Tarkan** (BC6 only). Open question 3.

**Built 2026-10-06** (the user: 'lets skip Storm Crow set, but make all others'): the Sword
(`Sword17`) and Staff of Destruction (`Staff09`), the Saint Crossbow (`CrossBow17`), the Black
Dragon set (`*Male17`), and the second class's Dragon Spear (`Spear11`), Grand Soul Shield
(`Shield16`) and Elemental Shield (`Shield17`), every one in both drop windows. The Storm Crow
set (8-11,15) is the Magic Gladiator's alone and is left out. Each recipe's notes say which of
MuMain's held sprites and sliding glows it does not carry.

**MU's own Tarkan items, as researched.** WZO `item(Kor).txt` (drop flag 1): Sword of Destruction 0,16
(82), Staff of Destruction 5,8 (90), Saint Crossbow 4,16 (84), Black Dragon set 7-11,16 (helm 82,
armour 90, pants 84, gloves 76, boots 78; knight only per KR 0.84), Scroll of Blast/Cometfall 15,12
(80), Orb of Summon Soldier (76, KR item 4), Dark Breaker 0,17 (104), Thunder Blade 0,18 (105).
MuMain has every body (`D/Item/Sword17.bmd`, `Staff09.bmd`, `CrossBow17.bmd`, `Book13.bmd`,
`D/Player/*Male17.bmd`). **OM 095d has the three Destruction weapons and Cometfall but no Black
Dragon set** (`OM/Version095d/Items/Armors.cs:53-122` stop at index 14; `Weapons.cs:110, 159, 169`;
`Scrolls.cs:31-32`); OM S6 has it (`OM/VersionSeasonSix/Items/Armors.cs:80, 133, 192, 251, 306`).
MU2_BGFX has the Jewel of Life recipe (`source/items/misc/Jewel03.json`, drop 72,
`drops_from_monsters: false`, as OM `Version095d/Items/Jewels.cs:42-43`) and the Scroll of Inferno.

**Jewels.** Chaos stops at 66 (atlans-port §5): no Tarkan breed drops it. Bless and Soul reach all.

**Class treasures, MU's way.** WZO `Quest(Kor).txt:53-55`: the Broken Sword (14,24), Tear of Elf
(14,25) and Soul of Wizard (14,26) drop from **any monster of level 62-76** at 10 in 1 000 while the
quest stands. In Tarkan that is the Mutant (72) and the Bloody Wolf (76); MuMain's dialog names
"Atlans, the Lost Tower and Tarkan" (`LEGACY/reference/MuMain/src/Localization/Dialog.en.resx` Text_60, 77, 82, lines
156, 296, 348). The Scroll of Emperor's band 45-60 (`:30-31`) misses Tarkan.

**Not 0.84, later in KR:**
- **Golden Tantalos (82) + Golden Wheels (83)**, §5.1.
- **Gold Medal** (14,11 level 6) on maps 4, 7 and 8 during the medal event (`WZ gObjMonster.cpp:5233-5245`).
- **Loch's Feather** dropped in Tarkan until 0.98c (2004-02-05) moved it to Icarus only
  (`a/muhistory.md:464`; `WZ gObjMonster.cpp:4620` `m_bFeatherOnlyIcarus`). Ours (`Quest04`, 78)
  is `drops_from_monsters: false`.

#### 5.1 The Golden Tantalos and the Box of Kundun +5

- **WZ 0.97d** (`EledoradoEvent.cpp:503-571`, `RegenKantur`): the Golden Tantalos ("칸투르 1",
  Kantur) is placed on **map 8 at a random free tile in 50-200** (`GetBoxPosition(8, 50, 50, 200,
  200)`, 13 411 open non-safe tiles on the client grid), and its **ten Golden Wheels** ("칸투르 2")
  within ±10 of it. Every **120 min** (`WZ Gamemain.cpp:1258`; 360 if read from the cfg,
  `:3696`). The Tantalos drops **one Box of Kundun +5** (14,11 level 12) to the top damage dealer
  (`WZ gObjMonster.cpp:4180-4192`); the Wheels drop nothing special. In WZO both are parked on map 0
  in the event section (`MonsterSetBase.txt:25-26`, "82는 83보다 언제나 앞에 와야 한다").
- **WZO stats** (`Monster.txt:280-281`): Golden Tantalos 90 / 30 000 / 450-560 / def 300 / A.Type
  150 / range 2 / ItemRate 180; Golden Wheel 77 / 15 000 / 320-360 / def 230 / range 4.
- **The bag** (WZO `eventitembag12.txt`, "칸투르의지하군단", Kantur's underground legion): at +4 the
  Chaos, Soul, Bless and Life jewels, the **Black Dragon set**, the Storm Crow (Atlans) set, Staff
  of Destruction, Saint Crossbow, Sword of Destruction; excellent: the four rings/pendants, Dragon,
  Guardian and Legendary sets, Legendary Staff, Aquagold and Bluewing, and so on. The 2003-08-05
  launch notice lists the same (`docs/kundun-box-sources.md:303`).
- **2009 renewal** (Season 4.5): the Tantalos and Wheel both drop **+3** (`WZ gObjMonster.cpp:4240,
  4278`; kundun-box-sources §3.2). **OM S6**: Golden Wheel 77 / 15 000, Golden Tantallos 90 /
  32 000 with Box +5 (`OM/VersionSeasonSix/InvasionMobsInitialization.cs:169-227`); invasion
  defaults 20 Wheels and 10 Tantalos on Tarkan (`OMGL/PlugIns/InvasionEvents/InvasionConfigurationDefaults.cs:32-33`).
  OM 095d has no golden Tarkan monster (`OM/Version095d/InvasionMobsInitialization.cs:32-201`).
- **What the game has.** Nothing is built: `docs/kundun-box.md:3-4` "Research only; nothing built".
  Its §6 table (`:170-171`) left "+5 Tantalos + Wheels | Tarkan -- we have none | re-home (Lost
  Tower 7? Blood Castle?)", and direction B (`:181-185`) proposed +5 at Lost Tower 7 / Blood Castle.
  **A Tarkan port gives the +5 box its own home back**, with its own bodies (Monster42/43, §4.3),
  and turns the re-homing question into "MU's way or B's". The +5 bag's MU contents (Black Dragon,
  the Destruction weapons) are themselves not built.

### 6. Rules of the place

- **Entry**: from Atlans gate 53 (14,225-15,230, the SW lagoon by the Hydras) at **130** to exit 54
  (248,40-251,44) in Tarkan's safe corridor; back by gate 55 (246,40-247,44, 130) to Atlans exit 56.
  The walk in is the whole of Atlans (745 steps) and 54 more to the town. MU2_BGFX doubles MU's gate
  levels (`src/sim/gates.cpp:55-58`, "Every level here is MU's doubled, ours"), but brought Atlans
  back to 70; MU's 130 doubled is 260. Open question 1.
- **Death -> the town.** WZ: `GetMapPos` keeps map 8 (`WZ MapClass.cpp:310` `if( Map != 8 )`, so the
  `Map > 4 && Map != 7 -> 0` rule never applies) and picks a random open tile of `gRegenRect[8]`
  (`:201`), via the death branch's final `else` (`WZ user.cpp:22362`). OM S6: gate 57 is a spawn
  gate on map 8. OM 095d: Lorencia (no spawn gate). **Settled: the town** (WZ = S6).
- **Town Portal -> the town**: `WZ protocol.cpp:17942-17945` `MapNumber == 8 -> gObjMoveGate(57)`;
  the client allows it (`MM/World/MapInfra/PortalMgr.cpp:54`). With no merchant in the town
  (WZO), potions mean the M window or the long walk back through Atlans.
- **Mounts: no rule.** WZ refuses the Uniria and Dinorant only for Atlans
  (`WZ MoveCommand.cpp:299-317`, `user.cpp:27147-27190`) and asks wings or a Dinorant only for
  Icarus (`MoveCommand.cpp:318ff`). Tarkan asks nothing. On foot the Atlans rule still stops a
  Uniria rider at gate 45; a warp would not. **Wings are not needed.**
- **The Dinorant flies here.** MuMain lifts a Dinorant rider 90 units off the ground in Tarkan and
  Icarus, 30 elsewhere (`MM/Engine/Object/ZzzCharacter.cpp:6387-6390, 11786-11789`,
  `MM/Camera/DefaultCamera.cpp:539-542`, `MM/Network/Server/WSclient.cpp:1339, 2109, 2182`), and
  disables its flying clips there "for the jumping animation" (`ZzzCharacter.cpp:584-592`).
  `docs/mount.md:196` already notes it. Showing only.
- **Sound and music**: `SOUND_DESERT01` loops (`MM/Scenes/SceneManager.cpp:882-884`, stopped
  `:956-959`; `D/Sound/desert.wav`), `MUSIC_TARKAN` (`:1061-1066`; `D/Music/tarkan.mp3`). Our rule
  (memory "Music is rare"): the desert bed, no track.
- **Sand in the air**: two scrolling sand sheets drawn over the whole screen,
  `Object9/sand01.jpg`, `sand02.jpg` (`MM/Engine/Object/ZzzInterface.cpp:3677-3689`,
  `MM/World/MapInfra/MapManager.cpp:143-145`); crawling bugs, `Object9/Bug` boids
  (`MapManager.cpp:146-147`, `MM/Engine/AI/GOBoid.cpp:1668-1727`), KR's "전갈을 닮은 작은 곤충류"
  (small scorpion-like insects); monsters kick up sand as they walk (§4.3). Part B's.
- **Nothing else**: no traps, no PK rule, no map item, exp multiplier 1. The Tantalos's one-in-five
  area blow is the map's mechanic, and it is common here (63 Tantalos), not a boss's rarity.

### 7. Where the sources disagree

| point | OM 095d | OM S6 | WZO (2004-05) | WZ C++ (0.97d+) | WZD | KR | settled / flagged |
|---|---|---|---|---|---|---|---|
| in 0.75 | -- (075 has no Tarkan) | | | | | 0.84, 2003-01-07 | **not 0.75** (decision #1) |
| reachable | **no gates, no warp** | gates 53-57, 77, 128 | gates 53-57, 77 | regen, portal | = WZO | Hydra zone door | WZO/S6 |
| gate level | -- | 130 / 130 | 130 / 130 | 2/3 for MG/DL | 130 | 140 (2003-01), 130 (2003-07) | **130** (ours doubled?: Q1) |
| warp level | -- | 140 | 140 (movereq), 130 (MoveLevel) | -- | 140 | /move 150 at launch | 140 |
| spawn box 57 | -- | 187,**63**-203,69 | 187,**54**-203,69 | regen 187,54-203,69 | = WZO | | **WZO = WZ** |
| Tarkan2 (77) | -- | 91,160-93,161 | 96,143-100,146 | -- | = WZO | | WZO (Q2) |
| grid | S6 file, 1 102 off | | 133 off (S6 door) | | = WZO | | **client** |
| spawns | 217 | = 095d | **same 217** | type-2 ±3 | 217 + 30 | moved/multiplied 2003-07 | **217** |
| stats | as §4.2 | = 095d | **identical** | | Wheel 12 000, DBK 30 000... | launch higher | **OM = WZO** |
| respawn | 10 / 20 / 150 | = | RegTime 10 / 20 / 150 | + 1 | 5 / 15 | | **11 / 21 / 151 s** (memory's +1 rule) |
| A.Type 150 | not created: melee | = | Tantalos, Zaikan, DBK | 1 in 5 Flame of Evil | 150 | | **WZ's** |
| NPCs | none | none | none | -- | Thompson, Baz, Amy | | **none** (Q4) |
| death | Lorencia | the town | -- | the town | -- | | **the town** |
| Black Dragon set | missing | present | present | -- | | 0.84 | flag |
| Golden Tantalos | -- | +5 (Wheel none) | event rows | +5 (0.97d), +3 (2009) | 300 000 HP | 2003-08-05 notice | Q6 |

### 8. Open questions

1. **Decision #1: port a post-0.75 world.** Evidence in §1.1. If yes: which era's numbers? OM 095d
   = WZO (post-2003-07, easier) is what every file carries; the 0.84 launch table survives only as
   the KR note's five columns (no rates, no spawn table). Recommendation: **OM 095d = WZO**, as for
   Atlans.
2. **Gate level.** MU says 130 both ways (140 at launch). Our gates double MU's except Atlans (70
   in). Options: 130 as MU; 260 by the doubling rule; or a level near the monsters' (Atlans was
   cut back from 120 to 70 because 120 made it trivial). With 100x experience a Tantalos kill is
   ~2 levels at 130 (`a/` computation: 41 Tantalos kills take 130 to 150). **Ask.**
3. **Second class gear by the window.** As built, Tarkan's ordinary breeds reach only five pieces
   (§5); the knight's set and every armour piece hang on two 151 s bosses, and Dark Phoenix's pants
   and armour never fall. Options: keep it (the bosses are the prize); raise those rows' odds on
   map 8; or let map 8 ignore the lower window for [2] rows. **Ask** -- it is the user's stated
   purpose for Tarkan.
4. **No NPC in the town.** WZO and OM have none; WZD's Thompson/Baz/Amy are the repack's.
   Recommendation: none, as MU; the M window is the way to a merchant. Or Baz, as Atlans got.
5. **Warp rows.** Tarkan (gate 57) on first arrival; Tarkan2 (77) when he first stands near it --
   WZO's box (96,143-100,146) or S6's (91,160-93,161). Recommendation: WZO's.
6. **Golden Tantalos / Box +5.** Tarkan is its 0.97d home. In scope for this port, or left to the
   Kundun-box work with its home now available?
7. **A.Type 150 on 63 ordinary Tantalos.** WZ's rule makes one blow in five an area spell from every
   Tantalos. `kBosses` would carry a non-boss. Recommendation: WZ's, rename the table's comment.
8. **Class change text and drops** (`docs/class-change-quest.md:20-25`, `src/sim/quests.cpp:787-799`):
   MU's Sevina names "Atlans, the Lost Tower and Tarkan" and MU drops the treasures from levels
   62-76 anywhere. A port can (a) put "and Tarkan" back in `sevinaTrial().offer[2]`, (b) add map 8
   to `Realm::treasureGround` (`src/sim/realm_quests.cpp:225-234`; MU's band would mean only
   Mutants and Wolves), (c) add Tarkan breeds to the trial's hunt. All the user's; the quest's
   shape (level 200, LT7 + Atlans) is ours.

### Where Tarkan sits

Our worlds by monster level (`source/mu.db` spawns): Lorencia 2-19, Noria 3-18, Devias 20-52,
Dungeon 19-55, Lost Tower 47-66, Atlans 43-74 (66-74 past the NE corner), Blood Castle 1-6 to 99
(instanced). **Tarkan's 72-93 is the next open-world band after Atlans**, the only one between
Atlans's 74 and Blood Castle 6's 99, and its Mutants (72) start where Atlans's Hydra (74) ends. MU's
own order is the same: Atlans (60-70) -> Tarkan (130) -> Icarus (160). At 100x experience a hero
reaches Atlans's gate quickly; Tarkan's kills pay 3 090-5 415 x100, about 1.7 Tantalos kills a level
at 130 and 5.5 at 200 (the class change's level).

---

### Side findings (outside Tarkan)

1. **atlans-port Part C: gate 53's box is half open, not solid rock** (§2.1): x 14 NoMove, x 15
   open, on our cooked grid as on every source grid; exit 56 fully open.
2. **`kAtlansGearFromLevel` 43 admits every Atlans breed.** `src/sim/items.h:388-397` and
   `docs/second-class-gear.md:97` call "Valkyrie 46, Vepar 45, Bahamut 43" Atlans's strongest; they
   are its weakest (the strongest are 66-74), and 43 is Atlans's lowest level, so every Atlans kill
   rolls the 1-in-400 gear. The test (`testSecondClassDrops`, 40 000 kills at 46) matches the code;
   the wording or the intent is off.
3. **atlans-port §4.2's experience figures leave out the 65+ term** (`rules.cpp:353-358`):
   Great Bahamut 2 544 not 2 502, Silver Valkyrie 2 720, Lizard King 2 902, Hydra 3 284 not 3 052.
4. **OM 095d's Tarkan and Icarus are unreachable in OM itself** (no gates or warp rows in
   `OM/Version095d/Gates.cs`), and its armour list lacks the Black Dragon set: OM 095d is not a
   complete 0.95d.
5. **WZO's `MoneyRate` is 30 for Tarkan's seven** (14 for Atlans and the Lost Tower; elsewhere only the golden pair, the Cursed King and rows 293 and 295).
6. The KR 0.84 note's bug-fix list confirms the Atlans Uniria ban was live in Korea by 2003-01
   ("유니리아 타고 아틀란스로 이동되는 버그 수정"), before 0.97d.

---

## Part B: the data and the look

Research notes, 2026-10-05. Nothing tracked was edited, nothing was cooked, no window was opened. Tarkan is MU server map **8** (`WD_8TARKAN`, `MM/World/MapInfra/MapManager.h:17`). The client ships it as `D/World9` + `D/Object9` (folder = map + 1).

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- WZ = WebZen 1.00.93 data extracted by an earlier session (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/5770e6b4-410c-4720-aca8-388135fec063/scratchpad/wz/Data`, cp949, not in the repo)
- S = `.../scratchpad/tarkan/b/`

Scratch outputs in S:
- `tarkan/`: `pipeline/terrain.py D/World9 tarkan S 9` (height/tiles/attributes/light.png plus tarkan.json; there are no table rows for map 8 yet, so nothing is flagged hidden). `terrain.log` is the run.
- `obj/`: every Object9 .bmd (80 ObjectNN plus Bug02) through MuExtract `export-obj` + `export-rig --actions=all`, a .log each. `objinfo.log` summarises them (type, placements, tris, bones, bbox, sheets).
- `mon/`: Monster42-46 (export-obj + rig, all 7 actions), Sword17 and Staff09.
- `tex/`: every World9/Object9 sheet decoded, plus the five monsters' and the two weapons'. `tex/ozt/` holds the .OZT (alpha) twins separately, because `flag`, `test2`, `TileGrass01`, `sander` and `wand10` ship as both .OZJ and .OZT under one stem, and a flat folder overwrites one with the other.
- Sheets to look at:
  - `sheet_world9.png`: the tiles, the TerrainLight, the grass .OZT, the Season minimap, the sandstorm layers sand01/sand02, Impack03 and redB
  - `sheet_object9_textures.png`: every Object9 sheet, alpha shown as magenta, the .OZT twins prefixed T
  - `sheet_objects.png` and `sheet_monsters.png`: flat-shaded renders, texel colour per triangle
  - `prev_l1_l2_grass_attr_height_light.png`: layer 1 | layer 2 | grass-able slot 0 (green), slot 5 (blue), lava slot 8 (red) | attributes (white open, dark red NoMove, black NoGround, green safe) | height | light
  - `light_big.png`: the TerrainLight at 3×
  - `att_client_om_diff.png`: client grid | OM grid | difference
- Scripts: `prev.py`, `objinfo.py`, `render.py`, `sheet.py`, `dec.py`, `attdiff.py`. Logs: `stats.log`, `drawn.log`, `objinfo.log`, `placements_special.log`, `tex.log`.

### The one thing to know first

**Tarkan is a cluster of pale desert plateaus floating in black, under a moving sandstorm drawn on the screen itself.**

- **More than half the map is never drawn.** 33,829 tiles (51.6%) carry NoGround (0x08). MuMain's terrain returns before drawing any face on a NoGround tile (`MM/Render/Terrain/ZzzLodTerrain.cpp:2178`, `:2311`). The clear colour shows through, and for Tarkan that is the **default black** (`MM/Scenes/SceneManager.cpp:371-402`, no Tarkan arm).
  - The drawn ground is 31,707 tiles (48.4%), 23,832 of them walkable.
  - The plateau edges are walled by **Object41**, a cliff-face piece placed 427 times. 367 of those stand on NoGround, sunk a median 3.5 m.
  - Object40 and Object42-44 add further sunk walls and mesas.
  - The Season minimap `World9/mini_map.OZT` agrees: everything outside the plateaus is transparent there.
  - Dinorant riders fly 90 cm up here, the same height as in Icarus, not the 30 cm used elsewhere (`MM/Engine/Object/ZzzCharacter.cpp:6387-6390`). MU treats Tarkan as a high place.
- **The sandstorm is a full-screen overlay, not particles** (`RenderOutSides`, `MM/Engine/Object/ZzzInterface.cpp:3677-3687`, called first thing in `RenderInterface`, `:3664`). Two additive full-screen quads (GL_ONE, GL_ONE, `MM/Render/Textures/ZzzOpenglUtil.cpp:395-403`), tinted (0.3, 0.3, 0.25), cover the screen above the 45 px HUD strip:
  1. `Object9/sand01.jpg` (BITMAP_CHROME+2), a soft grey cloud, at 0.3×0.3 of its sheet, scrolling 0.2 sheet per second
  2. `Object9/sand02.jpg` (BITMAP_CHROME+3), sparse white specks on black, tiled 3×2 and scrolling 1 sheet per second

  The UVs are skewed a quarter sheet top to bottom (`ZzzOpenglUtil.cpp:1729-1733`), so the streaks lean. The result is a constant, faint, beige brightening that streams sideways across the whole frame, with grit in it. Nothing else in the map makes the "desert wind".
- **What carries the colour:**
  - The TerrainLight is **neutral and bright**: walkable mean (138, 135, 135), against Atlans's (34, 104, 139). The warmth all comes from the sand and rock sheets.
  - There is a red pool in the south, and violet and navy blots in the north-east and east.
  - 98 pulsing orange lava glows (Object08) light the ground around them.
  - Lava flows (Object73/74/76/80) and a forge corner in the south-west, with smoke vents, rotating gears and golden orbs.
- **Sounds:** `desert.wav` loops always. `tarkan.mp3` plays always (`SceneManager.cpp:882-884, 1061-1066`).

**The grids agree here, unlike Atlans.**
- WZ's official `Terrain9.att` matches the client's walkability on **99.80%** of tiles.
- OM's `Resources/Terrain9.att` matches on 98.32% (`S/attdiff.py`). The 1102 tiles where they differ are all tiles that OM **opens** and the client closes:
  - 591 Safe+NoMove tiles and 471 NoMove+NoGround tiles round the north-east town
  - the whole of row 255
- All 217 of OM's 0.95d spawns (`OM/Version095d/Maps/Tarkan.cs:37-253`) stand on tiles the client leaves open, and none is in the safe zone.
- Recommendation: use the client grid.

---

### 1. Raw data inventory

#### 1.1 World9 (`D/World9`, 26 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain9.map | 196610 | yes | 3 planes: layer1, layer2, alpha |
| EncTerrain9.att | 131076 | yes | 16-bit attributes. **No sanity tile for Tarkan** (`ZzzLodTerrain.cpp:202-219` checks maps 0-4 only) |
| EncTerrain9.obj | 101434 | yes | **3381 placements, 82 kinds** |
| EncTerrainTest9.map/.att | 196610 / 131076 | no | byte-identical copies of the above (`cmp`); nothing reads them |
| TerrainHeight.OZB | 66620 | yes | read at offset 1080 |
| TerrainLight.OZJ | 77677 | yes | 256² baked light, near-white and neutral |
| Tile{Grass01,Grass02,Ground01,Ground02,Ground03,Water01,Wood01,Rock01,Rock02,Rock03,Rock04}.OZJ | 32-118 KB | yes (`MapManager.cpp:1365-1420`) | 11 sheets, all 14 slot names but Rock05-07 |
| **TileGrass01.OZT** | 65584 | yes, as BITMAP_MAPGRASS (`MapManager.cpp:1452-1453`) | 256×64 strip of **dry straw-coloured blades**, mean (150, 128, 94), 25% coverage. MU grows it on layer-1 slot 0 |
| TileGrass03.OZT | 131120 | yes (MAPGRASS+2, `:1459-1460`) | **fully transparent** (alpha 0 everywhere); it would grow on slot 2, which no tile uses |
| Terrain.att / Terrain.map / Terrain9.att / TerrainServer.att | | no | plain copies. Terrain9.att and TerrainServer.att are unencrypted 8-bit grids, 98.3% and 98.6% the client's |
| mini_map.OZT, Minimap.bmd | | Season UI | 1024² minimap, the void transparent |

There is no leaf01/leaf02: **Tarkan has no leaves**. It is absent from `RequireLeavesEffect` (`MM/Scenes/MainScene.cpp:78-100`).

terrain.py (`S/terrain.log`, `stats.log`, `drawn.log`):

```
height     0.00 to 3.83 m; drawn ground 5/50/95 pct 1.55/1.82/2.40; walkable 1.59/1.80/2.28
tiles      10 slots in use (0,1,3,4,5,6,7,8,9,10)
walkable   36.4% (23,832), 2.9% safe (1872 tiles, one region, x 155-251 y 28-97, the north-east)
NoGround   51.6% (33,829) -- undrawn
light      mean [158,155,156]; drawn [126,122,123]; walkable [138,135,135]; walkable luminance p10/50/90 70/132/212
objects    3381 placed, 82 kinds; missing: Object01, Object04, Object05, Object36, Object39
```

**Attributes:**

| value | tiles | meaning |
|---:|---:|---|
| 0 | 22724 | open |
| 1 | 1107 | Safe |
| 2 | 1 | |
| 4 | 7284 | NoMove |
| 5 | 591 | Safe+NoMove |
| 8 | 15161 | NoGround |
| 9 | 28 | Safe+NoGround |
| 12 | 18494 | NoMove+NoGround |
| 13 | 146 | Safe+NoMove+NoGround |

- There is no Water bit (0x10).
- Height barely correlates with walkability (corr 0.09). The plateaus are a low relief at about 1.8 m. The void's own heights (1.5-2.0 m) sit just under them, because MU never drew them.
- **765 of the 1872 safe tiles are closed** (NoMove or NoGround). The town's safe box takes in its walls and its edge.

**Map shape** (`prev_...png`; "north" means low y):
- **North-east: the safe town** (x 155-251, y 28-97).
  - Atlans's gate lands at (248-251, 40-44) and the way back is at (246-247, 40-44) (OM `VersionSeasonSix/Gates.cs:200, 525`; WZ Gate.txt rows 54/55).
  - The warp box is at (187-203, 63-69) in OM (`Gates.cs:198`) and at (187-203, 54-69) in WZ.
  - The ground is straw-grassed sand (slot 0).
- **A big northern plateau** (x 10-150, y 15-110), riddled with rock islands.
- **A big middle plateau** (x 10-130, y 110-200), with a grey cracked-stone square (slot 3) at x 64-127, y 128-160. Tarkan2's warp is at (91-93, 160-161) in OM and (96-100, 143-146) in WZ.
- Two south lobes:
  - the south-middle lobe at x 60-130, y 195-245, the red-lit one, holding the gear-and-orb shrine at (67-74, 182-189)
  - the east lobe at x 140-190, y 175-235
- **The south-west forge corner** (x 0-50, y 195-252): lava flows, smoke vents, gears, coffins, most of the lava glows.
  - The Season Kanturu gate (Object86-88) stands at (4.5, 199.6), on OM's gate 125 at (6, 199-201) (`Gates.cs:537`).
- Narrow causeways join the plateaus, one bridging the middle plateau to the town.

**Tile sheets** (looked at in `sheet_world9.png`; "drawn" means not NoGround):

| slot | name | px | actually | layer1 drawn % | layer2 overlays drawn |
|---|---|---:|---|---:|---:|
| 0 | TileGrass01 | 256 | **dry straw over sand**: the north-east town floor | 7.6 (2400) | 12 |
| 1 | TileGrass02 | 256 | fine tan gravel-sand | 0 | 903 |
| 3 | TileGround02 | 256 | **grey cracked stone**: the middle square and a south band | 2.5 | 424 |
| 4 | TileGround03 | 128 | dark ochre earth | 0.2 | 1537 |
| **5** | **TileWater01** | 256 | **sand ripples**: drifting sand, slid as water (§2) | 1.6 (515; 298 walkable) | 0 |
| 6 | TileWood01 | 128 | pale bleached rock/bark | 0.7 | 863 |
| 7 | TileRock01 | 256 | **pale cracked desert sand-stone**: the main floor | **65.4** | 1580 |
| **8** | **TileRock02** | 256 | **lava cracks**: glowing orange-red veins in grey crust | 0.2 (77) | 60 |
| 9 | TileRock03 | 128 | pale sand with bone-white rubble | 0.05 | 66 |
| 10 | TileRock04 | 256 | darker brown cracked earth: the rock islands and the edges | 21.7 | 4389 |

- Slot 7 is the desert; slot 10 is laid over its edges (4389 overlays), so the islands blur in.
- Slot 8's lava sits in two places: the north edge above the town (x 176-207, y 0-31) and the south lobes. Most of it is under NoGround or NoMove; none of it is walkable.
- Slot 5's sand sheets lie in small patches on the floor: the north plateau (32-63, 32-63) and the forge corner.

**Light map** (`light_big.png`):
- Near-white and grey, desaturated. The plateau edges and every object's footprint are dotted with dark baked shadow.
- Colour patches:
  - a **red-orange pool** under the south-middle lobe (x 84-123, y 204-236, 650 tiles R−B > 60)
  - **violet/navy** blots in the north-east town and on the east (x 125-213, y 41-193, 667 tiles B−R > 30)
  - small yellow, teal and green flecks on the north plateau
- The town's safe tiles average (182, 178, 184).

#### 1.2 Object9 (`D/Object9`, 141 files)

The folder holds:
- 80 `ObjectNN.bmd` (02-88 with gaps)
- `Bug02.bmd`
- 60 sheets

Every sheet a model names is on disk. Type = file number − 1 (`MapManager.cpp:1121-1128`). Sizes are the OBJ bbox in cm, x × y(up) × z. "dz" is placement height minus the ground under it.

**Models missing from the folder but placed** (MuMain loads nothing and draws nothing):
- type 0 Object01 ×2 at (2.5, 202.5) and (−2, 201.7)
- type 3 Object04 ×2
- type 4 Object05 ×1 at (18.5, 187.5). MoveObject still adds its pulsing terrain light (§2).
- type 35 **Object36 ×38**, along the west edge (x 16-39, y 52-174)
- type 38 Object39 ×6, sunk 3.7 m

Types 2-4 have MoveObject arms shaped like the Lost Tower's (`ZzzObject.cpp:4090-4106`), which suggests the map was started from a copy of it. Unplaced: Object35/63/67/78 and the absent 65/85.

| type | model | placed | tris | rig | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 1, 5 | Object02, 06 | 5 / 5 | 474 | 1 | hand1, finger, finger1 | **giant stone hands** rising from the sand, 2.7-3.1 m |
| **2** | Object03 | 4 | 72 | 1 | R (cyan glint) | a 2.1 m broken ring, BlendMesh 0, scrolled in U one sheet a second (§2) |
| 6 | **Object07** | **529** | 18 | 7 bones, **8 keys** | Df (alpha) | **dry desert grass tufts**, 0.6 m, swaying; the most placed object |
| **7** | **Object08** | 98 | 2 | 1 | redB | a flat 3 m quad of **orange lava glow**: additive, pulsing, adds orange terrain light (§2). Scales −0.08 to 2.3 |
| **8** | Object09 | 10 | 36 | 7 bones, 11 keys | cl (alpha) | a 78 cm **cloth rag** strip, in pairs, about 1.7 m up. Its clip runs at **4×** (§2) |
| 9, 10 | Object10, 11 | 25 / 23 | 240 / 120 | 1 | col2, col | a boulder cluster; a carved totem pillar, 2.5 m |
| **11, 13** | Object12, 14 | 6 / 3 | 112 | 1 | sf | **sand falls**: tall curved columns of rippled sand, mesh 0 streaming (§2) |
| **12** | Object13 | 5 | 84 | 3 | sf2 | a 3.9 m sunk **sand whirlpool bowl**, streaming diagonally |
| 14, 54 | Object15, 55 | 16 / 16 | 158 | 1 | face, face2 | **colossal stone heads** (4 m), Object55 tipped over |
| 15, 16 | Object16, 17 | 33 / 48 | 120 | 5 (static) | tree_08 (alpha) | dead creepers lying flat, 5 m wide |
| 17-19 | Object18-20 | 34 / 41 / 15 | 112-128 | 4 (static) | tree_01/02/09 (alpha) | dry thorn bushes |
| 20, 21, 24, 25 | Object21, 22, 25, 26 | 36 / 21 / 4 / 8 | 103-141 | 1 | test3, test4 | **cacti**, green, 1-2.4 m |
| 22 | Object23 | 5 | 48 | 1 | p (alpha) | a green grass clump |
| 23, 80 | Object24, 81 | 98 / 63 | 48 | 1 | test12, test22 | boulders, 1.3-2 m, many sunk |
| 26, 30 | Object27, 31 | 148 / 88 | 36 / 18 | 1 / 8 bones (**6 keys**) | test15, test1 (alpha) | dark dead fern shrubs; Object31 sways |
| 27, 28 | Object28, 29 | 318 / 67 | 48 / 40 | 1 | test16, test17 | large sandstone rocks, 1.7-1.9 m; Object28 often sunk (median −0.5 m) |
| 29 | Object30 | 1 | 528 | 17 bones, 6 keys | flag (both), arrow | a **scarecrow with a tattered skull banner** at (219, 54.5) in the town; the cloth moves |
| 31-33, 46 | Object32-34, 47 | 50 / 151 / 44 / 53 | 150-346 | 1 | test2 (.jpg bark, .tga leaves) | dead desert trees, 3.2-5.1 m; Object34 has foliage |
| 36, 78 | Object37, **Object79** | 23 / **13** | 190 | 1 | test18 | tree stumps. **Object79 is the seat** (§2), scale 0.32-0.38: at (39-44, 53-56) and in the town (189-207, 60-76) |
| 37 | Object38 | 12 | 108 | 1 | test20 | a shield and sword lying on the sand (grave markers) |
| 39 | Object40 | 38 | 44 | 1 | test22 | dark monolith/wall slab, 3.3 m, sunk ~4 m at the void's edge |
| **40** | **Object41** | **427** | 130 | 1 | test23 | **the cliff face**: 4.7 × 3.7 m rock wall, 367 stand on NoGround, sunk median 3.5 m. It is the plateau's edge |
| 41-43 | Object42-44 | 7 / 8 / 8 | 54-85 | 1 | test24 | flat-topped **mesa slabs**, 5 m, sunk 1.1-1.3 m, all on walkable tiles: raised stone tables |
| 44, 45, 47 | Object45, 46, 48 | 32 / 17 / 13 | 88-160 | 1 | ston | **natural rock arches**, up to 8 m long |
| 48, 49 | Object49, 50 | 19 / 52 | 496 / 364 | 1 | flag (skull) | scattered **skeletons and bones** |
| **50** | Object51 | 18 | 1307 | 4 bones, **32 keys** | stonD | a cloud of **rock debris**, animated, scale 2.4-4.4, sunk 3.4 m (rubble churning up from the edges) |
| 51 | Object52 | 74 | 220 | 1 | ston2 | spiky rock cones (termite-mound spires) |
| 52, 55, 58, 59 | Object53, 56, 59, **60** | 6 / 11 / 13 / 6 | 39-70 | 1, Object60 4 bones **16 keys** | casket | **coffins and a coffin-throne**; Object60's lid moves |
| 53 | Object54 | 9 | 12 | 1 | Dr (alpha) | a spiky star plant |
| 56 | Object57 | 135 | 130 | 1 | test37 | pebble and rock litter |
| **57** | Object58 | 3 | 1651 | **35 bones, 31 keys** | stonD | a 5 m **vertical column of debris**, animated, in the forge corner: falling rock/dust stream |
| **60** | Object61 | 82 | 12 | 1 | test12 | **hidden smoke burst** (box), §2 |
| **61** | Object62 | 2 | 138 | 1 | gear, gearB | a 1.9 m **sun-gear disc**, BlendMesh 1 (gear.tga's magenta veins), scrolling, pulsing orange light. At the shrine (66.7, 181.9), (73.7, 188.9), floating 2.9 m |
| **63** | Object64 | 18 | 12 | 3 | test12 | **hidden cyan glow sprite** (box), §2 |
| **65, 67** | Object66, 68 | 1 / 6 | 138 | 3 bones, **20 keys** | gear | **rotating gears** in the forge; 65 also glows |
| 68, 69, 71, 74 | Object69, 70, 72, 75 | 7 / 5 / 4 / 3 | 690-1050 | 1 | statue, statue2 | **fallen warrior statues** |
| **70, 76, 83** | Object71, 77, 84 | 3 / 19 / 10 | 12 | 1 | test12 | **hidden smoke vents** (boxes), §2 |
| **72** | Object73 | 12 | 112 | 1 | sf3 | **lava flow** (red-black crust), BlendMesh 0, streaming |
| **73, 75, 79** | Object74, 76, 80 | 13 / 40 / 22 | 112 | 1 | sf4 | **lava/fire flows** (orange), streaming. Object76 ×40 tiny (0.14-0.38) around (80-110, 166-180) |
| **81** | Object82 | 2 | 30 | 1 | DDD | a **golden orb** drawn twice, texture + chrome bright (§2). It floats 2.9-3.0 m over the two gears |
| **82** | Object83 | 60 | 6 | 3 | **light01** | **light shafts**: crossed 3.4 × 4.6 m planes, additive (§2); 37 on walkable floor |
| 85-87 | Object86-88 | 1 / 5 / 2 | 448 / 30 / 50 | 2 / 1 / 1 | cho_gate_a/_a_01/_a_03/_b/_c, chogatea02_R | the **Kanturu gate** at (4.5, 199.6) and its pillars and steps. **Season content**: a 0.97 Tarkan had no Kanturu |

**Placement counts, most first:**
- Object07 529, Object41 427, Object28 318, Object33 151, Object27 148, Object57 135
- Object24 98, Object08 98, Object31 88, Object61 82, Object52 74, Object29 67, Object81 63, Object83 60, Object47 53, Object50 52, Object32 50, Object17 48, Object34 44, Object19 41, Object76 40
- everything else 38 or fewer

**Animated placed kinds:**
- Object07 (529 tufts, 8 keys)
- Object31 (88, 6 keys)
- Object09 (10, at 4×)
- Object30 (1)
- Object51 (18, 32 keys)
- Object58 (3)
- Object60 (6)
- Object66/68 (7 gears, 20 keys)

That is **~660 rigged placements**, about a third of Atlans's ~2000. Object16-20 carry 4-5 bones but a single key, so they are static.

**Bug02** (`MapManager.cpp:143-148`: `MODEL_BUG01 + 1`, `Object9/Bug.bmd` #2): 19 tris, 6 bones, 3 keys, sheet `bug` (16×8). A 35 cm black **scarab** with spiked legs. It is Tarkan's floor crawler (§2).

Map-wide effect sheets in Object9 (not on models):
- `sand01`, `sand02`: the sandstorm
- `Impack03` → BITMAP_IMPACT (`MapManager.cpp:146`), a white-violet star flare for the glow sprites

---

### 2. MuMain's special cases for WD_8TARKAN

A grep for `WD_8TARKAN` gives 37 hits. `WorldActive == 8` never occurs. Every hit is below.

**Frame, light, fog, sky**
- **Clear and fog: black**, the default (`SceneManager.cpp:371-402`). There is no sky.
- **NoGround is undrawn** (`ZzzLodTerrain.cpp:2178`). With 51.6% NoGround, the frame is plateaus over black, walled by Object41.
- **No birds** (the boid spawn list, `MM/Engine/AI/GOBoid.cpp:1304-1312`, omits Tarkan) and **no leaves** (`MainScene.cpp:78-100`).
- **Lights that add to the terrain** (`AddTerrainLight`, all in MoveObject):
  - type 4 (Object05, unshipped, 1 placement): white, radius 3, pulsing `sin(WT·0.002)·0.35+0.65` (`ZzzObject.cpp:4095-4106`)
  - **type 7, Object08, 98 lava glows**: (L, 0.6L, 0.2L) orange, radius 3 tiles, `L = sin((WT + angle_z·100)·0.002)·0.35+0.65`, so each glow pulses on a 3.1 s period with its own phase (`:4108-4119`)
  - type 61 (Object62 gear) and types 65/66 (Object66, forge gear): orange, radius 2, `sin(WT·0.002)·0.35+0.65` (`:4134-4141, 4148-4156`)

**The sandstorm overlay** (`ZzzInterface.cpp:3677-3687`): described in the summary above.
- The colour is `glColor3f(0.3, 0.3, 0.25)`. It overrides the `glColor4f(1, 1, 1, 0.5)` before it, so the blend is a plain 30% additive.
- Layer 1: sand01 with `u = (WT % 100000)·0.0002`, a 0.3×0.3 window.
- Layer 2: sand02 with `u = (WT % 100000)·0.001`, 3×2 repeats.
- It is screen space, so it does not follow the ground and does not fade with distance.
- Karutan has a one-layer copy (`:3689-3697`); Crywolf and Battle Castle reuse the same pair (`GMCrywolf1st.cpp:2113-2116`, `GMBattleCastle.cpp:612`).

**Terrain**
- **The drifting-sand tiles.** Slot 5 (TileWater01, sand ripples) is drawn with the water flag, so its UV slides by `WaterMove` (`ZzzLodTerrain.cpp:1750-1754`). In Tarkan `WaterMove = (WT % 40000)·0.000025`: **one sheet per 40 s**, half the 20 s default (`:3583-3596`). On 515 drawn tiles the sand slowly creeps.
- **Grass**: `TileGrass01.OZT` grows on layer-1 slot 0 wherever all four corner alphas are 0 (`:2077-2120`). That is 2144 drawn tiles, almost all in the north-east town (the other 13,288 slot-0 tiles are NoGround, never drawn).
  - Tarkan's grass wind runs at ten times the spatial frequency: `sin(WindSpeed + x·50)·10` (`:3282-3285`) against the default `x·5` (`:3302`). Neighbouring columns wave out of step: a jittery shimmer, not a rolling wave.
- No caustics, no lava flag on any slot (slot 8 is a plain sheet), no Water attribute.

**Object arms**
- **Model setup** (`MapManager.cpp:1135-1143`): `Models[11, 12, 13, 73, 75, 79].StreamMesh = 0`. Mesh 0 of the sand falls, the whirlpool and three lava flows is lit flat by BodyLight, with no vertex lighting, and takes the object's UV scroll (`MM/Render/Models/ZzzBMD.cpp:1344-1369`).
- **MoveObject** (`ZzzObject.cpp:4087-4179`):
  - **2 (Object03):** BlendMesh 0, U scroll 1 sheet a second.
  - **4:** see lights above (BlendMesh 0, V scroll 1 sheet / 10 s).
  - **7 (Object08):** BlendMesh 0, light = the pulse; the lava glow.
  - **11, 13 (Object12, 14):** V scroll `(WT % 10000)·0.0002`, a sheet every 5 s: sand pouring.
  - **12 (Object13):** U and V `(WT % 50000)·0.00005`, a sheet every 20 s diagonally: the whirlpool turning.
  - **61 (Object62):** BlendMesh 1, V scroll 1/s, orange light.
  - **63, 64:** `HiddenMesh = -2`.
  - **65, 66:** BlendMesh 1, V scroll 1/s, orange light radius 2.
  - **72 (Object73):** BlendMesh 0, V scroll a sheet / 5 s.
  - **73, 75, 79 (Object74, 76, 80):** V scroll a sheet / 5 s (StreamMesh, not additive).
  - **82 (Object83):** BlendMesh 0, Light (1, 1, 1): the light shafts, plain additive.
- **Animation speed** (`:3727-3735`): type 8 (Object09) plays at `Velocity × 4^FPS_ANIMATION_FACTOR`, four times its default rate. The rag flaps fast in the wind.
- **RenderObjectVisual** (`ZzzObject.cpp:2994-3066`), the hidden emitters:
  - **60 (Object61, 82 boxes):** on its first visible frame, 20 BITMAP_SMOKE subtype 6 (`ZzzEffectParticle.cpp:1302-1311`: 1.2 s life, scale 1.8-2.0, scattered ±1 m·scale). Then `HiddenMesh = -2`. **A one-time dust burst when it first comes into view**, nothing after.
  - **63 (Object64, 18):** a BITMAP_IMPACT sprite at bone 2, cyan (L/1.7, L, L), scale 1.5·L, `L = sin((WT + angle·5)·0.002)·0.3+0.7`. 16 are in the forge corner and the shrine. **64** is the same in red (L, 0.32L, 0.32L), but nothing places it.
  - **70 (Object71, 3):** hidden; smoke subtype 7, 1 frame in 5 (rising puffs).
  - **76 (Object77, 19):** hidden; smoke subtype 4 every frame for the last 0.5 s of each 5 s cycle, all 19 in step. **Synchronised steam vents.**
  - **83 (Object84, 10):** hidden. In a 0.5 s window of every 10 s, offset by `angle_z·10` ms per vent, smoke subtype 8, plus 1 frame in 3 a half-size puff within ±64 and a **thrown MODEL_STONE1/2 rock**: **sand geysers** spitting stones (`:3042-3065`).
- **RenderObject** (`ZzzObject.cpp:1067-1072`): **81 (Object82)** is drawn as mesh 0 textured, then again as RENDER_CHROME | RENDER_BRIGHT with the global BITMAP_CHROME: the golden orb's environment shine.
- **CreateObject** (`:4778-4786`): **78 (Object79)** CreateOperate. Its MOVEMENT_OPERATE arm is **Sit** (`MM/Engine/Object/ZzzInterface.cpp:1743-1748`): 13 stump seats.

**Floor crawlers: the scarabs** (`GOBoid.cpp` MoveFishs)
- Tarkan is exempt from the 3-slot cap, so all **10 slots** fill (`:1668-1675`). Each spawns within ±5.12 m of the hero on a tile whose wall is 0 or TW_CHARACTER (`:1677-1700`).
- Spawn values (`:1726-1733`, shared with Aida): `MODEL_BUG01 + 1`, scale 0.8-1.1, Velocity 2.5/scale (fast), Gravity 9, LifeTime 100. Each trails a **BITMAP_JOINT_ENERGY subtype 4** ribbon: 20 tails following the bug, dull brown (0.3, 0.15, 0.1) (`ZzzEffectJoint.cpp:262-288, 445`).
- They run action 0 and stick to the ground (the always-true height test, `:1822-1825`). They turn back on NoMove/NoGround and die after two turns (`:1830-1841, 1860`) or past 15 m (`:1869-1874`).
- Drawn with a 20% shadow, like every MoveFishs model (`:1640-1648`).
- MU2_BGFX's `src/game/world/scurry.*` (the Dungeon's rats) is this system already. Tarkan needs its model, the 10-slot cap and the trail.

**Characters and mounts** (every arm is shared with Icarus, `WD_10HEAVEN`)
- **Dinorant flies 90 cm up**, not 30 (`ZzzCharacter.cpp:6387-6390, 11786-11790`; `MM/Network/Server/WSclient.cpp:1339, 2109, 2182`; `MM/Camera/DefaultCamera.cpp:539-541`).
- Its ride-run action is suppressed "because the animation jumps" (`ZzzCharacter.cpp:584-592`).
- The Pegasus/Dinorant model plays its flying set: actions 1/3/5/7 instead of 0/2/4/6 (`GOBoid.cpp:517-586`). The Rider skill uses PLAYER_SKILL_RIDER_FLY (`MM/GameLogic/Combat/SkillCast.cpp:315`; `WSclient.cpp:4739`).
- No swimming, no special shadow, no head effects.

**Ambient and music**
- `SOUND_DESERT01` = `Data/Sound/desert.wav` (1,433,670 B) loops always (`SceneManager.cpp:882-884`; stopped elsewhere `:956-959`; loaded `MM/Engine/Object/ZzzOpenData.cpp:4737`).
- `SOUND_WIND01` is stopped here (`:932-935`).
- **`MUSIC_TARKAN` = `data/music/tarkan.mp3` (3,049,328 B) plays always** (`SceneManager.cpp:1061-1066`; `MM/Core/Globals/_enum.h:178`).

**Other**
- The town portal works (`MM/World/MapInfra/PortalMgr.cpp:54`).
- The map-name card is `tarcan.tga` (`MM/UI/Legacy/UIMapName.cpp:55`; `D/Local/Eng/ImgsMapName/tarcan.OZT`).

---

### 3. The pipeline route, and what changes for World9

The chain is Atlans's (`docs/atlans-port.md` B §3). From MU2_BGFX/, with `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. tables first. Keys are map number - 1 = 8 (terrain.py is run with folder number 9).
#    pipeline/terrain.py
#      HIDDEN_BY_MAP[8]     = {60, 63, 64, 70, 76, 83}
#                             # 60 smoke burst (ZzzObject.cpp:2997-3006), 63/64 glow sprites (:4143-4145),
#                             # 70/76/83 smoke vents and geysers (:3026-3065). 64 is unplaced; listed as MU has it.
#      OPERABLE_BY_MAP[8]   = {78}            # stump seats, Sit (ZzzInterface.cpp:1743-1748; ZzzObject.cpp:4778-4786)
#      GRASS_BY_MAP[8]      = ["TileGrass01"] # MU's TileGrass01.OZT; grows only on drawn slot 0 = the NE town
#      BLEND_MESH_BY_MAP[8] = {2: 0, 4: 0, 7: 0, 61: 1, 65: 1, 66: 1, 72: 0, 82: 0}   # MoveObject :4090-4178
#      WATER_FLOW_BY_MAP[8] : NOT water. Slot 5 is sand that slides one sheet / 40 s (ZzzLodTerrain.cpp:3583-3586).
#                             It needs a "slide without wet" path (Part C); no sources/sinks, MU slides all one way.
#      VOID_BY_MAP[8]       : decision 2 below (MU: undrawn black). Candidate: {"rim": True, "blend": 1.5} as Blood Castle.
#      LIGHT_DEPTH / LIGHT_CHROMA / FIGURE_LIGHT: none to start (neutral, bright paint)
#      LAVA_SPILL / OPEN / FLOOR / VOID_FILL: none
#    pipeline/index.py
#      OPERABLE_BY_MAP[8]   = {78}
#      GATE_BOXES_BY_MAP[8] = [(246, 40, 247, 44), (248, 40, 251, 44), (187, 63, 203, 69)]
#                             # OM VersionSeasonSix/Gates.cs:525, 200, 198 (enter 55 -> Atlans, exit 54, warp 57);
#                             # WZ Gate.txt has the warp box at (187,54)-(203,69). Tarkan2 (91,160)-(93,161) / WZ (96,143)-(100,146)
#                             # and the Kanturu gate (6,199)-(6,201) / (7,199)-(7,201) are Part A's call.
#      GATE_BOXES_BY_MAP[7] += [(14, 225, 15, 230), (16, 225, 17, 230)]   # Atlans's side, Gates.cs:524, 194
python3 pipeline/terrain.py $D/World9 tarkan source/world 9
#    expect: 0.00-3.83 m, 10 slots, 36.4% walkable, 2.9% safe, 3381 placed / 82 kinds,
#            missing Object01, Object04, Object05, Object36, Object39 (MuMain has none of them either)
for n in TileGrass01 TileGrass02 TileGround02 TileGround03 TileWater01 TileWood01 TileRock01 TileRock02 TileRock03 TileRock04; do
  python3 pipeline/decode_texture.py $D/World9/$n.OZJ source/textures/tk_$(echo $n | tr A-Z a-z).png; done
#    TileGround01 is shipped but no tile wears it; TileGrass03.OZT is empty.
python3 pipeline/decode_texture.py $D/World9/TileGrass01.OZT source/textures/tk_tilegrass01_blades.png   # if MU's painted tuft is used
python3 pipeline/decode_texture.py $D/Object9/sand01.OZJ source/textures/tk_sand01.png    # the storm overlay
python3 pipeline/decode_texture.py $D/Object9/sand02.OZJ source/textures/tk_sand02.png
python3 pipeline/decode_texture.py $D/Object9/Impack03.OZJ source/textures/tk_impack03.png
#    source/world/tarkan/ground.json on Atlans's shape, sheets -> tk_*
./tools/content.sh --world tarkan && ./tools/sync.sh --world tarkan --only-world && ./tools/sync.sh
./tools/cook.py --world tarkan --only ground && ./tools/cook.py --world tarkan --only tables
#    then index.py (house rule: build-only skips the index), and every world's tables after new items
# objects: export per kind into source/world/tarkan/, sheets tk_-prefixed
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-obj $D/Object9/Object07.bmd source/world/tarkan/Object07.obj
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-rig $D/Object9/Object07.bmd source/world/tarkan/Object07.rig.json --actions=all
#   rigs for 07, 09, 30, 31, 51, 58, 60, 66, 68 and Bug02
./tools/asset.sh tarkan/Object07 --build-only && python3 pipeline/index.py source workshop
./tools/cook.py --world tarkan --only textures   # textures before --only meshes (house rule)
./tools/cook.py --world tarkan --only meshes / placements
```

**Sheet prefix: `tk_`.** Nothing in `source/textures` uses it today. Names that collide without one:
- every Tile*
- `light01` (the Lost Tower's and Atlans's light shafts, and MuMain's global BITMAP_SHINES)
- `flag`, `statue`, `col`, `face`
- the generic `test*` set
- `.OZJ`/`.OZT` stem twins: flag, test2, TileGrass01 (and the monster `sander`). The decoder must name each twin apart, or one overwrites the other, as it did in this pass's first run.

**ground.json `sheet_materials` (proposed):**

| slot | sheet | material |
|---|---|---|
| 0 | TileGrass01 | sand (straw-grassed) |
| 1 | TileGrass02 | sand |
| 3 | TileGround02 | rock (grey cracked stone) |
| 4 | TileGround03 | sand (ochre earth) |
| **5** | **TileWater01** | **sand that slides**. It must not take `water` (no sheen, no wet darkening). Needs a slide-only flag (Part C). |
| 6 | TileWood01 | rock (bleached) |
| 7 | TileRock01 | sand (the desert floor; cracked, dry, matte) |
| **8** | **TileRock02** | rock with **emissive lava veins**. Nearest existing: the Lost Tower's lava (`water_glow`). Unwalkable, small (77 + 60 tiles) |
| 9 | TileRock03 | sand (rubble) |
| 10 | TileRock04 | rock (dark earth) |

**What else differs from the worlds already built:**
- **The void is half the map and its edge is walled.** Unlike the Lost Tower's causeways (bare edges into black), Tarkan's plateaus end at Object41 cliff faces standing in the void. Two consequences:
  - The default void cut may suffice under the walls.
  - The buried test in cook.py (`FLAT_OVER_VOID`, `tools/cook.py:1206`) must not flag Object41/40/42-44/51 for reaching 3-5 m under the void's heights. Add `tarkan`, or skip by kind.
- **Hidden types must survive as markers:**
  - 60 dust bursts ×82
  - 63 cyan glows ×18
  - 70/76/83 vents and geysers ×32

  They are emitters, as the Lost Tower's flame vents were.
- **cook.py tables:**
  - `CRAWLS["tarkan"] = "Bug02"` (from Object9). `scurry` caps at 3, as MU does off Atlans/Tarkan, so Tarkan needs a 10.
  - `AIRS`: none.
  - `GROUNDED_TYPES`: none.
  - `LIFTED_BY_WORLD`: probably Object08 (98 flat glow quads at dz 0.2 m median, some at 0). Check for z-fighting, as Atlans's Object24 needed.
- **Rigged placements: ~660.**
  - Object07 tufts 529 (the bulk; small, 18 tris, one shared 8-key clip; a shared phase per kind is enough)
  - Object31 88, Object51 18, Object09 10, gears 7, Object60 6, Object58 3, Object30 1
- **Additive and streaming kinds:**
  - Object08 (whole mesh, pulse, terrain light)
  - Object03 (U scroll)
  - Object62/66 (mesh 1, V scroll, pulse, light)
  - Object73 (whole, V scroll)
  - Object83 (whole)
  - Object12/13/14/74/76/80 are opaque but **UV-streaming and unlit on mesh 0** (StreamMesh).
  - Object82 needs a chrome-bright second pass.
  - The cards (Object07/16-20/22/27/31/54) are alpha-cut .OZT, not additive.
- **Dynamic light:** 98 lava glows + 3 gears + type 4 = ~102 small pulsing orange point lights on the ground. `src/game/world/lamps.*` is the likely home. Budget question for Part C.
- **Recipe priority by placement count** (scenery first; the void walls are structural):
  1. Object41 (427 cliff faces, the map's silhouette)
  2. Object07 (529 tufts, rigged)
  3. Object28 (318 rocks)
  4. Object33 (151 dead trees)
  5. Object27 (148 shrubs)
  6. Object57 (135 pebbles)
  7. Object24 / Object08 (98 / 98)
  8. Object31 (88)
  9. Object52 (74)
  10. Object29 (67)
  11. Object81 (63)
  12. Object83 (60 shafts)
  13. Object47 (53)
  14. Object50 (52)
  15. Object32 (50)
  16. Object17 (48)
  17. Object34 (44)
  18. Object19 (41)
  19. Object76 (40)
  20. Object40 (38)

  Then the landmarks: the stone hands (02/06), colossal heads (15/55), statues (69/70/72/75), arches (45/46/48), mesas (42-44), cacti, the gear shrine (62 + 82 + 64), the forge (66/68, lava flows, coffins), the sand falls (12/13/14). The Kanturu gate (86-88) is pending decision 3. Object61/64/71/77/84 need no mesh.

---

### 4. Monster assets

OM `Version095d/Maps/Tarkan.cs`:
- Spawns: `:37-253` (217). Breeds: `:258-485`.
- 0.75 has no Tarkan; OM's 075 set stops at Atlans.

| # | breed | spawns |
|---:|---|---:|
| 58 | Tantallos | 63 |
| 61 | Beam Knight | 56 |
| 62 | Mutant | 41 |
| 60 | Bloody Wolf | 32 |
| 57 | Iron Wheel | 23 |
| 59 | Zaikan (boss) | 1 at (11, 241), the forge corner |
| 63 | Death Beam Knight (boss) | 1 at (161, 225), the east lobe |

The body file is `Monster{MONSTER_MODEL+1:02}.bmd` (`_enum.h:4191-4195`). CreateMonster arms are `ZzzCharacter.cpp:13703-13768`. MuExtract output is in `S/mon/`. **None of the five bodies is in `source/monsters`** (the model_index values built there are 0-37, 46-47, 57-62).

Every one of the five spawns two BITMAP_JOINT_ENERGY ribbons, subtypes 2 and 3. These are purple (0.5, 0.1, 1.0) eye trails tied to EyeLeft/EyeRight (`ZzzEffectJoint.cpp:262-288, 443-444`), which RenderEye/MoveEye fills each frame.

| # | breed | file | scale | arms / extras | tris, sheets | MU2_BGFX |
|---:|---|---|---:|---|---|---|
| 57 | Iron Wheel | **Monster42** | 1.4 | **Aquagold Crossbow** (CrossBow07, **cooked**), LinkBone 23 = Bone04 (`:11989-11990`). Shoots three MODEL_ARROW_BOMB at ±20° (`:1859-1876`). Dust puffs while walking and at death (`:5981-5984`). Walk PlaySpeed 0.18 (`ZzzOpenData.cpp:2632`). BoneHead 3 | 1306; wheel, w | **missing**. An armoured rider on a 2.4 m spiked iron wheel |
| 58 | Tantallos | **Monster43** | 1.8 | **Sword of Destruction** = Sword17 (**not cooked**; 242 tris, sword077), LinkBone 43 = Bone01 (`:11986-11987`). **BlendMesh 2 (bv03) at light 1**, its eyes and glow (`:13741-13743`). Inferno on attack (`:1880-1886`). Dust on walk and death (`:5960-5979`). Attacks at PlaySpeed 0.35 (`ZzzOpenData.cpp:2633-2636`). BoneHead 20; eyes 24/25 | 1560; bv02, bv01, bv03 | **missing**. A 2 m black-and-gold armoured brute |
| 59 | Zaikan | Monster43 | 2.1 | **Staff of Destruction** = Staff09 (**not cooked**; 72 tris, wand10). SubType 1: the whole body drawn RENDER_BRIGHT with the alternate skins bv02_2/bv01_2 (`ZzzObject.cpp:1240-1248`; loaded `ZzzOpenData.cpp:3692-3693`), so it is a glowing figure. BITMAP_FIRE at both feet (bones 6, 13) every frame, and it **darkens the ground under it** (AddTerrainLight −1.3, `ZzzCharacter.cpp:5961-5972`). Rendered at half brightness in the silver-body branch (`:8716-8722`). Boss: 18 Staff-of-Destruction projectiles in a ring (`:1887-1897`) | — | re-dress of Tantallos + 2 alt sheets |
| 60 | Bloody Wolf | **Monster44** | 2.2 | no weapon. Dust on walk (`:5955-5958`). BoneHead 7; eyes 11/12 | 834; bulldug, BGcl.tga, bulldughair.tga | **missing**. A tusked boar-wolf ("bulldug"), mane cards |
| 61 | Beam Knight | **Monster45** | 1.5 | **two hands empty in the data** but LinkBones 55 (Box03) / 70 (Box27) are set (`:11982-11984`): its attack throws 6 sparks off them (`:2152-2160`). MODEL_SKILL_INFERNO on attack (`:1838-1846`). Energy ball skill (`:5093`). Die PlaySpeed 0.3 (`ZzzOpenData.cpp:2637-2639`). Eyes 8/9, BoneHead 6 | 1252; devil, devilhair.tga, devilwing | **missing**. A 3 m bat-winged dark knight |
| 63 | Death Beam Knight | Monster45 | 1.9 | **BlendMesh −2 at light 1** (`:13716-13720`). Inferno + MODEL_SKILL_INFERNO (`:1810-1815`). Boss: random MODEL_SKILL_BLAST within ±4 m and an 18-way Staff-of-Destruction ring (`:1816-1834`). Dust on walk and death (`:5843-5853`) | — | re-dress of Beam Knight |
| 62 | Mutant | **Monster46** | 1.5 | no weapon. Dust on walk (`:5836-5840`). Eyes 8/9, BoneHead 6. ExtraMon chrome pass (`ZzzObject.cpp:1220-1233`), Season only | 740; sander, sander.tga, sander2 | **missing**. A hooded sand-ghoul |

- **To import:** Monster42-46 (5 bodies) and Sword17 + Staff09 (2 weapons). Staff09 is also the boss projectile. Plus figure rows for Zaikan (Tantallos body, alt skins bv01_2/bv02_2, bright) and Death Beam Knight.
- Already in hand: CrossBow07 (Aquagold).
- Needs `figures_tarkan.json`, and the six monster sheet sets decoded under per-monster prefixes, as Atlans did (`ironwheel_`, `tantallos_`, `bloodywolf_`, `beamknight_`, `mutant_`).
- The shared "sand smoke" (BITMAP_SMOKE+1 within ±1 m each walk frame, `ZzzCharacter.cpp:5571-5582`; 20 puffs at death frame 8, `:5552-5569`) is one small system for all five.

---

### 5. Sounds and music

| file | bytes | in `source/sounds`? | where MuMain plays it |
|---|---:|---|---|
| Sound/**desert.wav** | 1433670 | **no** | `SOUND_DESERT01`, ambient loop always (`ZzzOpenData.cpp:4737`; `SceneManager.cpp:882-884`) |
| iron1, iron_attack1 | 180066, 170682 | no | Iron Wheel (`ZzzOpenData.cpp:3679-3684`; SetMonsterSound 143,143,144,144,144) |
| jaikan1, jaikan2, jaikan_attack1, jaikan_attack2, jaikan_die | 328658, 110438, 232702, 89098, 327878 | no | Tantallos and Zaikan (`:3685-3695`) |
| blood1, blood_attack1, blood_attack2, blood_die | 248214, 107798, 134190, 291802 | no | Bloody Wolf (`:3696-3703`) |
| death1, death_attack1, death_die | 259222, 255022, 561682 | no | Beam Knights (`:3704-3710`) |
| mutant1, mutant2, mutant_attack1 | 320006, 345490, 137198 | no | Mutant (`:3711-3717`) |
| Music/**tarkan.mp3** | 3049328 | no | `PlayMp3(MUSIC_TARKAN)` always on the map (`SceneManager.cpp:1061-1066`) |

That is 18 new wavs and one mp3. There are no footstep changes and no wind loop (`SOUND_WIND01` is stopped here). New sounds go through `index.py` and then `tools/cook.py --only showing` (house rule).

---

### 6. A look direction

**What MuMain draws:**
- black clear and fog, black void round the plateaus
- floors of pale cracked sandstone and straw sand under a near-white painted light, with a red pool and violet blots
- dark brown rock islands
- no sky, no birds
- a faint beige sandstorm streaming across the whole screen
- life from:
  - swaying dry tufts
  - scarabs dragging brown trails
  - flapping rags
  - steam vents and stone-spitting geysers
  - 98 pulsing orange lava glows
  - lava flows and turning gears in the south-west forge
  - light shafts
  - a golden orb shrine

**It should read as a bright, hot, dusty noon on a high desert plateau, not a dark ruin.**
- MU's paint is the brightest of the maps ported so far: walkable median luminance 132, against Atlans's 86.
- This fits the house rules: "not too dark, not foggy" (`lorencia-evening-look`; Noria and Devias both lost veils at 0.0022-0.004).
- The sun stays at 52° (Devias was brought to Lorencia's shadow angle, 2026-09-30).

**What the engine's sheet must carry:**
- **A warm, strong sun at 52°.**
  - Colour a pale gold-white, so the cream sand does not go orange.
  - Strength between Devias's 3.8 and Noria's 5.0.
  - Sharp shadows; this is the one map where hard noon shadows fit.
- **Fill from a pale sky and a sand bounce.**
  - The sky fill is a dusty pale blue, low.
  - The ground bounce is warm sand, so the shade under rocks is warm brown and not grey.
- **Exposure below Noria's.** Bright paint times bright albedo clips easily; Devias's snow argument applies.
- **Haze: the storm, not a fog.**
  - MU's dust is a screen overlay. The engine's `dust` should stay a trace (Devias's 0.0033 or less), in a pale sand colour.
  - The streaming sand layer is a separate, ours-or-MU's effect (Part C): a faint additive screen- or camera-space layer of sand01/sand02 at MU's 0.3 strength or less.
  - Expect the user to want it subtle, per "monster auras are subtle" and "rain faint".
- **The void.** A black frame around a noon desert reads as a hole. Options (decision 2):
  - MU's black cut with the Object41 walls hiding the edge
  - Blood Castle's `rim` + `blend` so the plateau lips darken
  - `void_clouds` in a warm dust colour below the plateau edges (ours)
- **Grade:**
  - the shade warm-neutral (no teal, unlike Atlans and Noria)
  - the light slightly gold
  - saturation a touch under 1, since the sand sheets already carry the warmth
  - per the house sepia rule, sepia goes in the light/shade tint, not the saturation slider
- **Grass:** MU's straw tuft, only in the north-east town (2144 tiles). Dry, short, sparse, no green, no meadow flowers. MU's out-of-step wind (`x·50`) suggests a faster, jittery stir.
- **Lamps:** the lava glows want `lamp_strength` near the base so their orange shows at noon. Noria's note ("reduce sun so we see the light emitters") is the precedent.

**Proposed starting `sheets/worlds/tarkan.json`** (shape from devias.json and noria.json; all values are invention, for the first shots):

```json
{
  "note": "Tarkan's light, laid over lighting.json while a character stands in Tarkan (game/world/maps.h). MU draws it on its default black clear and fog (SceneManager.cpp:371-402), half the map NoGround and undrawn (ZzzLodTerrain.cpp:2178), the plateaus lit by a near-white, neutral TerrainLight (walkable mean 138, 135, 135, the brightest paint ported) with the warmth all in the sand sheets, and a faint additive sandstorm streamed across the screen (ZzzInterface.cpp:3677-3687, colour 0.3, 0.3, 0.25). So: a strong gold-white noon sun at the house 52 degrees with hard shadows, a pale dusty sky fill and a warm sand bounce so the shade stays brown, exposure under Noria's because bright paint times bright sand clips, a trace of sand-coloured dust (the storm is its own layer), the grade warm and a little under full saturation. Invention on MU's look; first cut 2026-10-05, unshot.",
  "elevation": 52.0,
  "sun_colour": [1.0, 0.92, 0.76],
  "sun_strength": 4.4,
  "sky_colour": [0.46, 0.52, 0.6],
  "horizon_paleness": 0.85,
  "ground_colour": [0.38, 0.31, 0.22],
  "ambient_strength": 0.65,
  "exposure": 1.15,
  "dust_colour": [0.9, 0.8, 0.64],
  "dust_density": 0.0025,
  "lamp_strength": 3.14159,
  "glow_strength": 1.2,
  "bloom_threshold": 1.0,
  "bloom_strength": 0.3,
  "contrast": 0.45,
  "saturation": 0.92,
  "split": 0.3,
  "tint_low": [1.0, 0.97, 0.94],
  "tint_high": [1.08, 1.02, 0.88],
  "grass_dry": 1.0,
  "grass_vary": 0.3,
  "grass_meadow": 0.0,
  "grass_through": 0.0,
  "grass_height": 0.3,
  "grass_density": 0.35,
  "water_flow": 0.5,
  "water_sheen": 0.0,
  "probe": 0.0
}
```

- `water_flow 0.5` stands for MU's half-speed slide of slot 5, **if** slot 5 is given a dry slide. Otherwise 0 and the sand holds still.
- A `sand_storm` knob (overlay strength and colour) does not exist yet: Part C.

Expect the first-shot fights to be:
1. sand going orange or the floor clipping (exposure versus saturation)
2. the void's black frame
3. how much storm is "minimal"
4. whether 98 lava glows read at noon at all

**House rules applied:**
- **Music is rare and in fights.** MuMain's always-on `tarkan.mp3` becomes at most a fight track; `desert.wav` is the bed.
- **Subtle effects:**
  - the storm faint
  - the 19 Object77 vents de-synchronised (MU fires all 19 in the same 0.5 s window)
  - the scarab trails thin
  - the Object61 bursts only on first sight, as MU does, or dropped
- **Test runs muted, judged in game by the user. Lorencia-only audits do not apply here** (a new world).

**Decisions for the user:**
1. **Grid:** the client's (99.8% WebZen's official Terrain9.att), or OM's, which opens 1102 tiles round the town (591 of them Safe+NoMove) and row 255. Recommend the client's.
2. **The void:** MU's black cut behind the Object41 walls, or a blended rim and/or warm `void_clouds` below the plateaus (ours).
3. **The Kanturu gate** (Object86-88 at the west edge, OM gate 125): Season content with no 0.97 counterpart. Keep it as a sealed ruin, hide it, or wire it later.
4. **The sandstorm:** MU's screen-space overlay at 0.3 additive, a world-space drifting sand layer (ours), or both, and how strong.
5. **Slot 5:** MU's slow sliding sand (needs a dry-slide path) or still sand.
6. **The missing Object36** (38 placements along the west edge; no .bmd in this client): leave empty, as MuMain draws it.
7. **Music:** whether `tarkan.mp3` plays in fights at all.
8. **Dinorant at 90 cm:** carry the Tarkan/Icarus flying height, or keep the 30 cm ride.

---

## Part C: the engine side (Tarkan)

Written 2026-10-05, read-only: no repo file was edited, no cook was run, no window was opened, no stash was made. Paths are relative to `MU2_BGFX/` unless marked. Abbreviations, as in atlans-port.md Part C:
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`;
- MM = `LEGACY/reference/MuMain/src/source/`;
- D = `LEGACY/reference/MuMain/src/bin/Data`;
- WZ = WebZen 1.00.93's 99z repack data that an earlier session extracted (`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/5770e6b4-410c-4720-aca8-388135fec063/scratchpad/wz/Data`, cp949, not in the repo).
  - **Read with care:** this "WZ" is the 2012 repack (Part A's WZD), not WebZen's official data. Its HP figures and its three town NPCs are the repack's. Where it differs from OM 095d, OM 095d = WZO wins (see "Where the passes disagree" at the top).

Line numbers are HEAD `12462a8d` plus the working tree as found. The tree has uncommitted edits from another session in `play_mode.cpp`, `play.cpp`, `play_open.cpp`, `figures.*`, `pedestals.*`, `lobby_mode.cpp`, `fx/arrow.cpp`, `pipeline/index.py`, `docs/cook-log.md` and `source/world/atlans/placements.json`. Any Tarkan step that touches those files must read `git diff` first (memory: another session edits the same files).

Template: atlans-port.md Part C (`docs/atlans-port.md:1281-1791`), checked against what Atlans's build really needed (its "Where it stands", `:15-170`). Pass B's terrain extraction is in `../b/tarkan/` (attributes.png, tiles.png, tarkan.json); the grid numbers below were computed from it (scratch scripts `c/grid.py`, `c/split.py`).

---

### 0. The record the engine has to carry (short; pass A owns it)

**Map and folders.**
- Tarkan is map **8** (`OM/Version095d/Maps/Tarkan.cs:28-31`) and MuMain's `WD_8TARKAN` (`MM/World/MapInfra/MapManager.h:17`). Its folders are `D/World9` + `D/Object9` (terrain.py map_number 9).
- **It is not 0.75.** OpenMU has it from Version095d, and 095d's `Gates.cs` has **no Tarkan gate at all** (`OM/Version095d/Gates.cs:150-167, 203-226`). The warp list there is a "todo" that stops at LostTower7 (`:40-55`).
- World9 has a **TileWater01** (unlike Atlans), TileGrass01/02, TileGrass01.OZT and TileGrass03.OZT, TileGround01-03, TileRock01-04 and TileWood01.
- Object9 holds Object02-88 (Object01, 04, 05, 36 and 39 are missing on disk; tarkan.json places them 2, 2, 1, 38 and 6 times). It also holds `Bug02.bmd` + `bug.OZJ`, `sand01.OZJ`, `sand02.OZJ`, `Impack03.OZJ` and `light01.OZJ`.
- Sound and music are present: `D/Music/tarkan.mp3` (already in `assets/music/tarkan.mp3`) and `D/Sound/desert.wav`. The monster sounds are iron1, iron_attack1, jaikan1/2, jaikan_attack1/2, jaikan_die, blood1, blood_attack1/2, blood_die, death1, death_attack1, death_die, mutant1/2 and mutant_attack1 (`MM/Engine/Object/ZzzOpenData.cpp:3680-3716`).

**Gates.** They exist only in Season Six and WZ.

| gate | map | box OM S6 | box WZ | dir | role | source |
|---:|---|---|---|---|---|---|
| 53 | Atlans | 14,225-15,230 | same | -- | enter, level **130**, to 54 | `OM/VersionSeasonSix/Gates.cs:524`; WZ `Gate.txt:95` |
| 54 | Tarkan | 248,40-251,44 | same | 7 N | arrival from Atlans | S6 `:200`; WZ `:96` |
| 55 | Tarkan | 246,40-247,44 | same | -- | enter, level **130**, to 56 | S6 `:525`; WZ `:98` |
| 56 | Atlans | 16,225-17,230 | same | 3 S | arrival from Tarkan | S6 `:194`; WZ `:99` |
| 57 | Tarkan | 187,**63**-203,69 | 187,**54**-203,69 | 0 | **spawn gate**: death, Town Portal, warp | S6 `:198`; WZ `:101` |
| 77 | Tarkan | 91,160-93,161 | 96,143-100,146 | 0 | warp "Tarkan2" only | S6 `:199`; WZ `:137` |
| 125/128 | Tarkan | 6,199-6,201 / 7,199-7,201 | -- | -- | S6 only, level 150, to a later map | S6 `:201, 537` |

**Warp and move levels.** S6's warp list: "Tarkan" 8000 zen, level 140, to gate 57; "Tarkan2" 8500 zen, level 140, to gate 77 (`OM/VersionSeasonSix/Gates.cs:67-68`). WZ `MoveLevel.txt` gives Tarkan **130** for the whole map.

**Monsters.** OM 095d `Tarkan.cs` has **217** one-tile spawns (`:36-252`):

| # | breed | count | level | HP | skill | respawn |
|---:|---|---:|---:|---:|---|---|
| 57 | Iron Wheel | 23 | 80 | 17 000 | -- | 10 s |
| 58 | Tantallos | 63 | 83 | 22 000 | MonsterSkill | 20 s |
| 59 | Zaikan | 1 | 90 | 34 000 | MonsterSkill | 150 s |
| 60 | Bloody Wolf | 32 | 76 | 13 500 | -- | 10 s |
| 61 | Beam Knight | 56 | 84 | 25 000 | Energy Ball | 20 s |
| 62 | Mutant | 41 | 72 | 10 000 | -- | 10 s |
| 63 | Death Beam Knight | 1 | 93 | -- | MonsterSkill | 150 s |

The definitions are at `:253-485`. WZ `MonsterSetBase.txt` has 222 rows of the same seven breeds plus three NPCs: 231 Thompson the Merchant at 191,74, 240 the Guard at 197,58 and 253 Amy at 205,62. WZ `Monster.txt` gives other HP (Iron Wheel 12 000, Tantallos 15 000, Zaikan 24 000, Death Beam Knight 30 000), A.Type 150 on 58, 59 and 63, and resistances 9/9 (57, 58), 13/13 (59, 63), 8/8 (60, 62) and 10/10 (61). OM 095d has no NPCs in Tarkan.

#### 0.1 The grid, from pass B's extraction (`../b/tarkan/attributes.png`, read [y,x])

- 36.4% walkable, 2.9% safe (pass B's log). Height 0.00-3.83 m.
- **Walkable ground is one 8-connected region of 23 832 tiles**, like Atlans and unlike the Dungeon or the tower. No OM 095d spawn stands on a closed tile, and none on a safe one.
- **The safe zone is one region of 1 872 tiles** (x 155-251, y 28-97): a long town corridor from the middle west to the Atlans door in the north-east.
  - WZ's spawn box 187,54-203,69 is all safe (253 of 272 tiles open). S6's 187,63-203,69 is all safe too (118 of 119 open).
  - A 4x5 closed block sits at 193-196 x 57-61, a statue or fountain, so **WZ's middle tile 195,61 is closed**. Use **195,65** as the arrival.
- Gate tiles:
  - Tarkan exit 54: 12 of 20 open, all safe.
  - Tarkan enter 55: 9 of 10 open.
  - S6 Tarkan2 box: 6 of 6 open. WZ's: 19 of 20 open.
  - Exit 128: 3 of 3 open.
- **The Atlans door is NOT solid rock in our grid.** In `source/world/atlans/attributes.png`, exit 56 (16-17 x 225-230) is **12 of 12 open**. Enter 53 (14-15 x 225-230) is 6 of 12: column 15 is open and column 14 is rock. All of it is in Atlans's single walkable region. atlans-port.md Part A says this (`:425-427`, "already open"); its Part C §0 (`:1325-1326`, "its box is solid rock here") is wrong and should be corrected.
- **Walking distances from the arrival 195,62:** 52 tiles to the Atlans door at 247,42, 172 to WZ's Tarkan2 box, 188 to S6's, and 270 to each boss (Zaikan at 11,241, Death Beam Knight at 161,225).
- **Splitting the map by nearest landing** (a multi-source flood from 195,66 and 92,160):
  - area 1 holds 7 959 tiles: Mutant 41, Bloody Wolf 28, Iron Wheel 3;
  - area 2 holds 15 873 tiles: Tantallos 63, Beam Knight 56, Iron Wheel 20, Bloody Wolf 4, and both bosses.
  - So "Tarkan 2" is the hard half.
- *(Superseded: Parts A and B find 51.6% NoGround, undrawn. See the top.)* Pass B's word histogram has no NoGround word (no 0x08 word on its own; 8 and 12 are NoMove combinations). Pass B confirms the void question.

---

### 1. How a world is registered and loaded today: the checklist for map 8

There is still **no single registry**, and `MapRow` has not been widened since atlans-port.md asked for it (`src/game/world/maps.h:18-34` keeps world, number, arrive, underground, home, event). Atlans was added with name tests in about 15 places (§1.3). The name is `tarkan`. The pipeline's keys are **MU's map number** (terrain.py's `map_number - 1`): Tarkan is **8**, Atlans 7.

#### 1.1 Pipeline (source side)

| # | where (file:line) | Tarkan needs |
|---|---|---|
| P1 | `pipeline/terrain.py main` | `python3 pipeline/terrain.py $D/World9 tarkan source/world 9`. Pass B's dry run: 10 slots, 3381 placements of 82 kinds, 5 models missing |
| P2 | `pipeline/terrain.py:118-137` `HIDDEN_BY_MAP` | `8: {60, 63, 64, 70, 76, 83}`: the types whose RenderObjectVisual arm sets `HiddenMesh = -2` (`MM/Engine/Object/ZzzObject.cpp:2994-3065`). 60 sets it after one smoke burst (Object61 x82); 63 is Object64 x18; 64 is unplaced; 70 is Object71 x3; 76 is Object77 x19; 83 is Object84 x10. **Not** 78 (Object79, the seat: CreateOperate, still drawn, `:4778-4786`). Pass B confirms against the meshes |
| P3 | `pipeline/terrain.py:151-163` `BLEND_MESH_BY_MAP` | `8: {2: 0, 4: 0, 7: 0, 61: 1, 65: 1, 66: 1, 72: 0, 82: 0}` (MoveObject's Tarkan arm, `ZzzObject.cpp:4087-4180`). Pass B owns the look |
| P4 | `pipeline/terrain.py:170-188` `GRASS_BY_MAP` | MU grows grass in Tarkan: it ships TileGrass01.OZT and TileGrass03.OZT, and `ZzzLodTerrain.cpp:3619` turns grass off for Atlans only. A row only if slot 0 should not take its recipe's grass (pass B: its TileGrass01 is 23.9% of the ground) |
| P5 | `terrain.py:203` `VOID_BY_MAP`, `:238` `LAVA_SPILL`, `:251` `OPEN_BY_MAP`, `:272` `VOID_FILL`, `:324` `LIGHT_DEPTH`, `:342` `WATER_FLOW_BY_MAP` | None expected (no void and no lava). TileWater01 is 0.9%: a `WATER_FLOW_BY_MAP[8]` entry only if its pools should run one way; otherwise MU's slide, at half speed (§3.7) |
| P6 | `pipeline/terrain.py:367-373` and `pipeline/index.py:44-50` `OPERABLE_BY_MAP` (two copies) | `8: {78}` in both: MOVEMENT_OPERATE's Tarkan arm, `case 78: Sit = true` (`MM/Engine/Object/ZzzInterface.cpp:1743-1749`), on Object79 x13 |
| P7 | `pipeline/index.py:57-71` `GATE_BOXES_BY_MAP` | `8: [(187,54,203,69), (248,40,251,44), (246,40,247,44), (91,160,93,161) or (96,143,100,146)]`. Add `(14,225,15,230), (16,225,17,230)` to `7` for Atlans's door. Maps 4 and 11 are still missing from this table (a side finding three Part Cs old) |
| P8 | `source/world/tarkan/ground.json` -> `tools/content.sh` | `tk_` prefix (as `at_`, `lt_`, `bc_`); 11 sheets from World9 including its real TileWater01. Pass B |
| P9 | `source/mu.db` `gates` | `insert into gates values (57, 8, 187, 54, 203, 69, 1)` (WZ's box, the whole safe hall). **This one row keeps a death in Tarkan** (§4.4) |
| P10 | `pipeline/index.py worlds()` (`:1476-`) | Automatic once the ground is built. Run index.py after `--build-only` (memory) |
| P11 | `pipeline/index.py:88-` `EFFECTS` (Atlans's rows at `:596-605`) | New rows: `"sand"` (Object9/sand01), `"sand_fine"` (Object9/sand02), `"impact"` (Object9/Impack03) and the bug's trail sheet if built; then `tools/cook.py --only showing` |

#### 1.2 The cook (`tools/cook.py`)

| key | line | Tarkan |
|---|---|---|
| `AIRS` | `:202-205` | none: MU flies no boid in Tarkan (`MM/Engine/AI/GOBoid.cpp:1300-1350` has no Tarkan arm) |
| `CRAWLS` | `:209` | `"tarkan": "Bug02"`: MoveFishs' Tarkan arm makes `MODEL_BUG01 + 1` (`GOBoid.cpp:1727-1735`), loaded from `Object9\Bug02` (`MM/World/MapInfra/MapManager.cpp:143-149`). Unplaced, so it must be named here or it is never cooked |
| `AIRS_FROM` | `:216` | none (Bug02 is Object9's own) |
| `FLAT_OVER_VOID`, `LIGHT_REACH_BY_WORLD`, `OBJECT_LIGHT_DEPTH_BY_WORLD` | `:1206-1214` | only if the look asks |
| `LIFTED_BY_WORLD` | `:1230` | only if a decal fights the ground, as Atlans's Object24 did |
| `ANCHOR_KINDS_BY_WORLD` | `:1261-1268` | `"tarkan": {"Object71": 4, "Object77": <periodic smoke>, "Object84": <sand geyser>, "Object64": <impact glow>}` (§3.3). `ANCHOR_LIGHT` (`:1285-1293`) gets a row per new kind |
| `PERCHES` | `:2082-2114` | `8: {78: (2, False, False, False)}`, Object79, a seat (Sit, not turned: the arm sets no angle) |
| `figure_file` | -- | automatic `figures_tarkan.json` |
| `RESPAWN_VERSION075` | `:1941` | none; mu.db `respawn_seconds` written true |
| `FOLK_VERSION075` | `:1968-` | `8: [...]` only if decision 13 takes WZ's three NPCs (OM 095d has none) |
| this map's spawns | `cook_tables` `:2117-` | automatic |
| tables after any item or kind change | (no loop) | now **nine** runs: lorencia, noria, devias, dungeon, losttower, bloodcastle, atlans, charscene, tarkan (`assets/cooked/*/*.mur` lists eight today; memory: new items need every world's tables) |

#### 1.3 The runtime: every place a world is named (verified against today's code)

| # | file:line | today | Tarkan row |
|---|---|---|---|
| R1 | `src/game/world/maps.cpp:19-40` `kMaps` | 7 rows; Atlans `{"atlans", 7, {21, 17}, true}` `:33` | `{"tarkan", 8, {195, 65}, true}`: the middle of spawn gate 57, moved off the closed statue tile. `underground` true for the same reason Atlans has it: MU blows no leaves in Tarkan (`MM/Scenes/MainScene.cpp:78-100` has no `WD_8TARKAN`) and plays no wind (§2 #1) |
| R2 | `src/game/world/maps.h:18-34` `MapRow` | not widened | the place for `air` etc. (§2) |
| R3 | `src/game/ui/arrival.cpp:55-65` `kPlaces` | Atlans `:63` | `{"tarkan", "Tarkan", ""}` |
| R4 | `src/game/ui/minimap.cpp:289-299` `mapName` | `case 7` `:296` | `case 8: return "Tarkan";`. Atlans's door then labels "Gate to Tarkan" (`:933`) |
| R5 | `src/game/ui/minimap.cpp:272-287` `floorName` | Dungeon and tower | `case 77: "Tarkan 2"`, only if its warp row comes |
| R6 | `src/game/world/maps.cpp:81-96` `placeName` | tower and Dungeon by box | returns `world` unless Tarkan 2 is named (§4.3) |
| R7 | `src/sim/realm_travel.cpp:18-39` `kRows`; `src/sim/travel.h:30` `kTravels = 14` | Atlans last `:38` | decision 1: append 1 or 2 rows (`kTravels` 15 or 16; u32 `found_`, append only so saves keep their bits) |
| R8 | `src/game/ui/travel.cpp:195` `kComing` | lists `{"Tarkan", 8}` | **once `mapNumbered(8)` exists and no row names map 8, the dimmed card vanishes** (`:197` skips it; `:147-150` builds cards only from rows), as Atlans's nearly did |
| R9 | `sheets/worlds/tarkan.json` | -- | new, found by name (`maps.cpp:98-101` `mapSheet`). Pass B's look |
| R10 | `src/game/play_open.cpp:31-41` | `windy_`, `dungeonAir_`, `towerAir_`, `castleAir_`, `waterAir_`, `underwater_`, `grassy_`, `snowy_` by name | `desertAir_ = world == "tarkan"` (§2 #1). `grassy_` stays false: MU walks Tarkan on SOUND_HUMAN_WALK_GROUND (`MM/Engine/Object/ZzzCharacter.cpp:5380-5383`) |
| R11 | `src/game/play_open.cpp:464-476` | air loads | `else if (desertAir_) heard_.wind = sound_.load("world_desert", false);` |
| R12 | `src/game/play_sound.cpp:415` `loop(wind, ...)`, `:424-427` `room(...)` | | add `\|\| desertAir_` to the loop. **The room must be Open, not Roofed**: an underground map's `indoors()` is always true (`world.cpp:408`), so without its own arm Tarkan would take the Roofed reverb |
| R13 | `src/game/world/world.cpp:97, 234-236` leaves | open unless underground; Atlans's motes | nothing if R1 is underground (no leaves, as MU) |
| R14 | `src/game/world/world.cpp:101-115` caustic | Atlans by name | nothing |
| R15 | `src/game/world/boids.cpp:66-77` `boidOf`, `:80-119` `airsOf` | | nothing (returns empty) |
| R16 | `src/game/world/scurry.cpp:32-35` `crawlOf`; `scurry.h:47` `kMaxRats = 3` | Rat01 in the Dungeon | Bug02 and 10 slots (§3.4) |
| R17 | `src/game/world/weather.cpp:113-116` | wet worlds by name | nothing: Tarkan is dry, as MU has it |
| R18 | `src/app/modes/play_mode.cpp:961-1018` music | Atlans and the tower in their safe zones `:994-1003` | add `"tarkan"` to that arm with `/music/tarkan.mp3` (decision 7). MU plays MUSIC_TARKAN on the whole map (`MM/Scenes/SceneManager.cpp:1061-1066`) |
| R19 | `src/app/modes/play_mode.cpp:117-121, 728-742` home, `takeHome`, M key | | nothing: gate 57 keeps `home` unused. `mapAfter` cycles to it |
| R20 | `src/sim/gates.cpp:9-49` `kExits`, `:54-101` `kEnters` | | §4.1 |
| R21 | `source/mu.db` | | §5 |
| R22 | `tools/bot/bot.cpp:68-74` `kWorlds` | 5 worlds, no Atlans and no Blood Castle | add `{8, "tarkan", {195, 65}}` only if bot runs should reach it |
| R23 | `src/game/headless.cpp:166-168` | `mapOf(world)->arrive` | automatic |
| R24 | `src/game/ui/describe.cpp:485`, `src/game/ui/hud.cpp:701` | "Ridden in Lorencia, Devias, Noria and Atlans" | text: add Tarkan |
| R25 | `src/sim/items.h:395` `kTarkanMap = 8`; `src/sim/realm_items.cpp:1130-1136` `secondGearHere` | **already wired**: second-class gear falls in Tarkan by the level window | nothing; it works the moment map 8 has kills |
| R26 | `src/sim/items.h:192` `featherMap` (4, 7) | Loch's Feather in the tower and Atlans | decision 12 |
| R27 | `src/sim/realm_quests.cpp:222-228` treasure ground; `src/sim/quests.cpp:788-830` Sevina's trial | Tarkan left out "because this game has no Tarkan" (docs/class-change-quest.md:24-25) | decision 11 |
| R28 | `src/game/world/{doors,trap_show,lava_smoke,skulls,castle_sparks,drawbridge,void_clouds,portal,bubbles}.cpp` | each returns early off its world | nothing |
| R29 | `src/game/world/ornaments.cpp:276, 294, 334, 377` | tower, castle and Noria glows; Devias beacon | Object64's impact sprites could go here (§3.3) |

Load path unchanged: `PlayMode::travel` -> `World::open` (`world.cpp:90-`) -> `Play::open` (`play_open.cpp:24-`, `cooked/tarkan/tarkan.mur`; breeds without a figure are held back) -> `boids_.open` / `leaves_` / `weather_.open` (`world.cpp:225-245`).

---

### 2. Every hard-coded per-world check, and what Tarkan needs from each

Grep: `grep -rnI -E '"(dungeon|devias|noria|lorencia|losttower|bloodcastle|atlans)"' src tools/*.py tools/bot pipeline` (99 hits today, list in `c/grep_worlds.txt`).

| # | where | today | Tarkan needs (MU's rule) | smallest change |
|---|---|---|---|---|
| 1 | `play_open.cpp:33-41`, `:464-476`; `play_sound.cpp:415, 424-427` | the air by name | **desert.wav looped on the whole map** (`MM/Scenes/SceneManager.cpp:882-884`, stopped elsewhere `:956-959`; `SOUND_DESERT01`, `MM/Engine/Object/ZzzOpenData.cpp:4737`); no wind; no roof | `desertAir_`, a `world_desert` event in `source/sounds/sounds.json` beside `world_water` (`:1462`), `source/sounds/desert.wav`, index.py, `--only showing` (memory: new sound needs the showing cook). The room stays Open. **This is the fourth air keyed by name.** The `MapRow.air` refactor (atlans-port.md `:1465-1469`) is now cheaper than another bool |
| 2 | `play_sound.cpp:111-116` footsteps | grass only in Lorencia and Noria | soil everywhere (`ZzzCharacter.cpp:5380-5383`) | nothing |
| 3 | `world.cpp:234-236` leaves | open unless underground | none (MainScene.cpp:78-100) | R1's `underground` |
| 4 | `boids.cpp:66-119` | birds by name | none | nothing |
| 5 | `scurry.cpp:32-35`, `scurry.h:47` | Rat01, 3 slots | **10 bugs** (Tarkan is in MoveFishs' list that skips the `i >= 3` cut, `GOBoid.cpp:1668-1675`), on open tiles (`TerrainWall == 0 \|\| TW_CHARACTER`, `:1692-1694`), turned back by walls (`:1830-1836`) | §3.4 |
| 6 | `weather.cpp:113-116` | | dry | nothing |
| 7 | `ornaments.cpp` | per-world glows | Object64's BITMAP_IMPACT pulse at bone 2 (`ZzzObject.cpp:3003-3023`) | §3.3 |
| 8 | `maps.cpp:81-96`, `minimap.cpp:272-299` | names by world and number | `case 8`; Tarkan 2 only with its row | rows |
| 9 | `realm_travel.cpp:76-125` `settleFound` | floods each row's landing **in turn**; the first row takes a connected map | **must change if Tarkan gets two rows** (one region, §0.1) | multi-source flood (§4.3) |
| 10 | `play_mode.cpp:994-1003` music | Atlans and tower in safe zones | MU: tarkan.mp3 everywhere | add to the arm |
| 11 | `src/game/pets.h:37` `kDinorantLift = 0.30`; `play_show.cpp:1038` | 30 units on every map | **90 units in Tarkan** (and Icarus), the dragon drawn 10 under him rather than 30, and its odd "flying" clips 1/3/5/7 (`MM/Camera/DefaultCamera.cpp:539-541`; `ZzzCharacter.cpp:6387-6390, 11786-11789`; `GOBoid.cpp:517-590`); the rider skill plays PLAYER_SKILL_RIDER_FLY (`MM/GameLogic/Combat/SkillCast.cpp:315-318`) | decision 8: a per-map lift and clip offset on the Dinorant path |
| 12 | `src/sim/realm_tuning.h:342` `kBosses = {35, 38, 49}` | Flame of Evil one blow in five | WZ A.Type 150 on **58, 59, 63** (Tantallos, Zaikan, Death Beam Knight) | add 59 and 63; 58 is a decision (63 of them would make one Tantallos blow in five an area blow) |
| 13 | `realm_tuning.h:395-421` `kResistances` | per breed | WZ's 9/9, 9/9, 13/13, 8/8, 10/10, 8/8, 13/13 (ice, poison) for 57-63 | rows (pass A's source) |
| 14 | `realm_tuning.h:436-451` `kDropRates` | per breed | MoneyRate and MaxItemLevel from pass A's chosen Monster.txt (WZ 99z: 30 and 3; WZO, Atlans's source, to be read); RegTime 10/20/150 (OM) | rows |
| 15 | `realm_tuning.h:301, 328, 362` `kPoisoners`, `kChillers`, `kSplitBlows` | | none in MU for Tarkan's breeds; a split blow for the Beam Knight is "ours" only if asked | nothing |
| 16 | `play_pointer.cpp:471-510` `shoots` | a monster holding a bow or crossbow shoots | **Iron Wheel** holds the Aquagold Crossbow (`ZzzCharacter.cpp:13759-13767`): automatic through its `right_hand`. MU throws **three** MODEL_ARROW_BOMB, at 0 and +-20 degrees (`:1859-1877`) | a fan of three for one figure name (`kSplitBlows` shape or a play row); memory: missiles are two looks |
| 17 | `play.cpp:1158-1270` attack shows by `attackSkill` | Energy Ball for a monster exists (Vepar, `:1261-1267`) | Beam Knight 61 = 17, free; plus MU's six BITMAP_JOINT_THUNDER from both hands (`ZzzCharacter.cpp:2152-2165`) | free (energy ball); hand lightning reuses the Lizard King's |
| 18 | `play_open.cpp:625-640` `sands` from `kSandingFigure = "Giant01"` (`play_tuning.h:336`) | one figure | MonsterDieSandSmoke on the Golden Wheel model (Iron Wheel), Tantallos, Beam Knight and Death Beam Knight; MonsterMoveSandSmoke on those plus Mutant and Bloody Wolf (`ZzzCharacter.cpp:5552-5582, 5836-5986`) | a figure list, and a walking-puff caller of `fx/breath` (§3.5) |
| 19 | `play_tuning.h:464-488` `kAuraLights` | faint light per breed | Zaikan and Death Beam Knight **darken** the ground (`AddTerrainLight(-1.3)`, `ZzzCharacter.cpp:5910-5913, 5964-5967`); Tantallos/Zaikan's BlendMesh 2 glow | rows (a negative light is new; decision) |
| 20 | `src/sim/items.h:910-912` `firecrackerMap` | dungeons and castles | nothing (Tarkan is not a dungeon) | nothing |

---

### 3. Tarkan's special features against the engine

All 34 `WD_8TARKAN` references in MuMain were read (list in `c/mm_tarkan.txt`). What MU does in Tarkan:
- desert.wav air, tarkan.mp3 music, black clear (`SceneManager.cpp:371-403`: no Tarkan arm);
- **two screen-space sand layers over the scene** (RenderOutSides) and **ten crawling bugs** with glowing trails;
- water sliding at half speed and a choppy grass wind;
- 98 pulsing braziers and four other warm glows that light the ground;
- four hidden smoke and sand emitters and one hidden pulsing light sprite;
- six UV-scrolling models and one chrome-bright model, and a seat;
- the Dinorant flying higher;
- the monsters' eye trails, sand dust, infernos, flames and staff rings.

Prices use `docs/budget.md`: 5.5 ms at 1080p; effects account 0.3 ms; shade 2.3; spare 0.2; **0.018 ms per full screen of blended overdraw at 1080p** (`budget.md:241-247`). The reference point is Atlans with everything in at 4.27-4.92 ms (atlans-port.md `:122`).

#### 3.1 The sandstorm haze: MU's RenderOutSides (lacks; no new gfx code needed)

- **MU** (`MM/Engine/Object/ZzzInterface.cpp:3677-3689`, called first in RenderInterface `:3664`, so over the 3D scene and under the HUD). Two quads cover the screen above the 45-pixel bottom bar:
  - colour (0.3, 0.3, 0.25), under EnableAlphaTest + EnableAlphaBlend (additive);
  - `BITMAP_CHROME + 2` = `Object9/sand01.jpg` at UV size 0.3 x 0.3, drifting `WorldTime % 100000 * 0.0002` (0.2 UV a second, so a full screen in 1.5 s);
  - `BITMAP_CHROME + 3` = `Object9/sand02.jpg` at 3 x 2 repeats, drifting 1.0 UV a second (a screen in 3 s);
  - sheets loaded at `MM/World/MapInfra/MapManager.cpp:143-145`. This is Tarkan's whole "sandstorm": a warm grain streaming sideways across the view, always on. (Karutan uses the same layer, `ZzzInterface.cpp:3690-3698`.)
- **Engine today**: no full-screen overlay helper. But `gfx::Sprite` has `placed` quads with free corners and per-corner UVs and the `Additive` blend (`src/gfx/effects.h:33, 59-67`). Two placed quads set just inside the near plane, sized to the frustum and with UVs advanced by the clock, draw MU's two layers with no renderer change. Where they live: a small `game/world/sand_haze.{h,cpp}` opened on Tarkan, gathered into `gfx::Effects` after the world's sprites. Alternatively, it could be a present-view pass in gfx with a uniform (new code).
- **Cost**: two full screens of additive overdraw, about **0.036 ms**, in the effects account. Sprites do not take `dusty()`; the haze is its own.
- **Ours, a decision (4)**: blowing sand in the world as well, from Devias's storm mode in `Leaves` (`setStorm`, sideways streaks, docs/devias-blizzard.md pass 2) re-skinned with a sand mote and a calm/gust cycle from `Weather`. MU has none. It costs today's snow storm (the pool's 150-180 small streaks, well under 0.05 ms).

#### 3.2 Braziers and warm glows (has it: glows and lamps)

The MoveObject Tarkan arm (`ZzzObject.cpp:4087-4180`):
- type 2 (Object03 x4): BlendMesh 0 scrolling U.
- type 4 (Object05 x1, **model missing**): white pulse with a 3-tile terrain light.
- **type 7 (Object08 x98)**: BlendMesh 0 pulsing `sin((t + yaw*100)*0.002)*0.35 + 0.65`, plus a warm (1, 0.6, 0.2) 3-tile terrain light.
- types 61, 65, 66 (Object62 x2, Object66 x1): BlendMesh 1 scrolling V, with warm 2-tile lights.
- types 72, 73, 75, 79 and 11-13 (Object73, 74, 76, 80, 12-14): UV scroll, with `StreamMesh = 0` on models 11, 12, 13, 73, 75 and 79 (`MapManager.cpp:1135-1143`).
- type 82 (Object83 x60): BlendMesh 0 at light 1.
- type 81 (Object82 x2): RenderObject draws mesh 0 again in `RENDER_CHROME | RENDER_BRIGHT` (`ZzzObject.cpp:1067-1073`).
- type 8 (Object09 x10): plays its clip **4x** faster (`:3727-3735`).

The engine has all of these:
- glow materials with pulse and scroll (`tools/cook.py` glow pulse, `Material` scroll modes, `content/mesh.h:97`);
- recipe lamps for the terrain light (the tower's and Atlans's precedent: 216 Atlans lamps, dense cells dropping their farthest);
- the chrome second pass (`plus.colour`, the Silver Valkyrie's);
- per-placement clip speed (Sway `play_speed`).

The 98 braziers give about 100 point lights. That is shade work, already priced on Atlans's 216 (4.27-4.92 ms all in). The pulse phase by yaw is a material wander, as Atlans's breathing was. Cost: 0 new code, ~0 ms beyond lights.

#### 3.3 Hidden emitters (has the anchor machinery; 2-3 new kinds)

From RenderObjectVisual's Tarkan arm (`ZzzObject.cpp:2994-3065`):

| type (model, count) | MU | engine route | kind |
|---|---|---|---|
| 60 (Object61, 82) | 20 BITMAP_SMOKE sub 6 **once**, then hidden | nothing (a load-time puff nobody sees); hide it | -- |
| 63 (Object64, 18) | hidden; a BITMAP_IMPACT (Impack03) sprite at bone 2, pulsing `sin((t + yaw*5)*0.002)*0.3 + 0.7`, scale 1.5 L, cyan (L/1.7, L, L) | the bone's rest offset baked by the cook into an anchor; a glow sprite drawn by Lamps or Ornaments (ornaments.cpp's Noria/tower glows are the shape, but they ride Sway's pose and these are hidden, so bake the offset) | new: impact glow |
| 70 (Object71, 3) | hidden; BITMAP_SMOKE sub 7, one frame in 5 | the chimney's rising smoke (Blood Castle's Object38) | 4 |
| 76 (Object77, 19) | hidden; BITMAP_SMOKE sub 4 for 0.5 s of every 5 s | kind 4 with a duty cycle, or a new periodic kind (the Lost Tower's vent kind 5 is the timed precedent, `cook.py:1255-1258`) | new or 4 |
| 83 (Object84, 10) | hidden; for 0.5 s of every 10 s (phase by yaw) a burst of BITMAP_SMOKE sub 8 plus scattered sub 4 at half scale, and one in three frames a MODEL_STONE1/2 thrown: a **sand geyser** | an anchor kind that Lamps/Play fires on MU's timer, throwing smoke and `fx/meteor.h`'s stones (lent as Inferno lends them) | new: geyser |

- **Cost**: in view at once, about 2-4 emitters, each a few dozen sprites under a metre (the smoke01/smoke02 sheets). That is **< 0.02 ms**. A geyser's burst is half a second of perhaps 40 puffs: about 0.5 screens, **~0.01 ms while it lasts**.
- Object79's seat is a perch (§1.2), not an emitter.

#### 3.4 The bugs (has the pool; it is too small and has no trail)

- **MU** (`GOBoid.cpp:1664-1760, 1826-1840`): ten slots; `MODEL_BUG01 + 1` (Bug02, sheet bug.jpg); born within 5.12 m on an open tile; scale 0.8-1.1; `Velocity = 2.5 / scale`; turning `Gravity 9`; `LifeTime 100`; turned 180 degrees with a strike on a wall or the void. Each carries a **BITMAP_JOINT_ENERGY subtype 4 trail** following its EyeLeft (`:1733`; `MM/Render/Effects/ZzzEffectJoint.cpp:3032-3080`). Aida's arm is the same.
- **Engine**: `Scurry` (`src/game/world/scurry.h`) is MU's MoveFishs for one model and 3 slots, with the void turn-back and the bursts. The route:
  - `kMaxRats` becomes a per-world count (10 in Tarkan);
  - Tarkan's numbers come from a small per-world row (velocity, scale, turn, lifetime), as `airsOf` does for boids;
  - `crawlOf("tarkan") = "Bug02"` and CRAWLS in the cook.
  - The trail is new: a short ribbon of the showing's `joint_energy` (already loaded for Charon's wisps, `play.h:1198-1213`), 8 tails, additive. Decision 9: build without the trail first.
- **Cost**: 10 small skinned figures (palette rows from the shared 512), **~0.01 ms GPU**. Trails: 10 thin ribbons, **< 0.01 ms**.

#### 3.5 The monsters' shows

| breed (model) | MU (`MM/Engine/Object/ZzzCharacter.cpp`) | engine has | cheapest route | cost |
|---|---|---|---|---|
| **all seven** | two **BITMAP_JOINT_ENERGY sub 2/3 eye trails** following the eyes MoveEye sets from two bones (`:13730-13767`, `:5836-5986`; Mutant 8/9, Beam Knight 8/9, Wolf 11/12, Tantallos 24/25, Wheel 8/9) | no eye trail (`fx/eyes.h` is RenderEye's static almond, not MoveEye's ribbon) | one `fx/eye_trails` ribbon per eye: 8 tails, additive, faint, to the monster auras' taste | ~20 ribbons on screen in a pack, **< 0.02 ms** |
| Mutant 62 (Monster46 @1.5) | MoveEye; MonsterMoveSandSmoke while walking | `fx/breath` puff (`BITMAP_SMOKE + 1`, smoke02, Dust blend) | a walking-puff caller beside `sandOnDeath` | see below |
| Bloody Wolf 60 (Monster44 @2.2) | MoveEye; move sand | same | same | -- |
| Iron Wheel 57 (Monster42 = the Golden Wheel model @1.4, Aquagold Crossbow) | three MODEL_ARROW_BOMB at 0, +20, -20 degrees each swing (`:1859-1877`); move and death sand | crossbow shot through `shoots` (`play_pointer.cpp:493-508`) | a fan of three (§2 #16) | 3 missiles, ~0 |
| Beam Knight 61 (Monster45 @1.5) | Energy Ball (OM 095d `:401`); MODEL_SKILL_INFERNO each swing (`:1838-1845`); two BITMAP_FLAMEs at its hands' bones 55-62 and 70-77 every frame (`:5914-5950`); 6 JOINT_THUNDER from the hands on its skill (`:2152-2165`); move and death sand | Energy Ball for monsters (`play.cpp:1261`); `fx/inferno`; `fx/flame` plumes; lightning joints (Lizard King's) | rows; the hand flames as two faint sprites | per knight ~0.01 ms; **Inferno on 56 knights is the risk**, below |
| Tantallos 58 (Monster43 @1.8, BlendMesh 2 lit, **Sword of Destruction**) | `CreateInferno` each swing (`:1880-1886`); move and death sand | `fx/inferno` (the wizard's, judged and softened) | inferno per swing, or ours fainter (decision 5) | as below |
| Zaikan 59 (Monster43 @2.1, SubType 1, **Staff of Destruction**) | Inferno each swing; on its boss skill a **ring of 18 MODEL_STAFF_OF_DESTRUCTION** flung up and out (`:1887-1900`; effect `MM/Render/Effects/ZzzEffect.cpp:1330-1337`: 30 frames, +280 units, spin); BITMAP_FIRE at bones 6 and 13 each frame; **dark ground** -1.3 (`:5960-5967`); gilded chrome pass at Bright 0.5 (`:8716-8722`) | inferno; chrome pass; the staff mesh via `fx/effect_mesh` | new: the staff ring (18 instances of one mesh, 30 frames); a negative lamp is new | ring ~0.02 ms for 1.2 s |
| Death Beam Knight 63 (Monster45 @1.9, BlendMesh -2 = every mesh added) | Inferno plus MODEL_SKILL_INFERNO; on its skill MODEL_SKILL_BLAST rain within 4 m every frame and the 18-staff ring; **flames along 35 bones every frame** (wings, limbs, body, head; `:5843-5913`); dark ground | inferno; Balrog's meteor storm (`kBalrogStormSeconds`, `play_tuning.h:448-449`) for the blasts; `fx/flame`; additive body as the Valkyrie's glow | a body-flame emitter on a bone list (the Shadows' `ShadowStars` is the shape: a bone list fed each frame); the boss blasts as the Balrog's storm | one boss; flames ~40 sprites at 0.2-1.3, ~0.5 screens, **~0.01 ms**; the storm as priced for the Balrog |

- **The two lines that can overdraw the effects account**:
  1. **Sand puffs.** MonsterMoveSandSmoke is `rand_fps_check(1)`: a puff every frame per walking monster, life 32 frames, 0.3-0.6 of smoke02. Five walkers is about 150 live puffs near a metre. Budget's table puts 128 sprites at 1 m half-extent at +0.18 ms. Route: the Budge Dragon's one-in-four (`breath.h:9-11`), or a per-figure cap. Estimate **0.03-0.08 ms in a pack**. Measure.
  2. **Infernos on every swing.** 63 Tantallos and 56 Beam Knights each play `fx/inferno` (24 flares at twice the blast, 16 flipbook blasts, 7 smoke puffs) on every attack. Three at once could be 1-2 screens each: **0.05-0.1 ms**. That is the effects account's third, and on every pull. Route: MU's for the two bosses; a faint quarter-size version, or none, for the rank and file (decision 5; memory: monster auras are subtle).
- **Weapons as props**: the Sword of Destruction (`MODEL_SWORD + 16`, `D/Item/Sword17.bmd`) and the Staff of Destruction (`MODEL_STAFF + 8`, `D/Item/Staff09.bmd`) are **not in mu.db** (items stop at 0/15 and 5/7). Monster recipes name a held model by built name (`source/monsters/Valkyrie01.json` `right_hand: "CrossBow06"`), so both must be built as item models before Tantallos and Zaikan are judged. A mu.db row is needed only if they should drop (decision 14). The Iron Wheel's Aquagold Crossbow (4/14, CrossBow07) is in mu.db.

#### 3.6 Ground details (has them; sheet knobs)

- **Water**: MU slides Tarkan's water at half the usual rate: `WorldTime % 40000 * 0.000025`, one sheet in 40 s against 20 s (`MM/Render/Terrain/ZzzLodTerrain.cpp:3581-3597`). The engine has `water_flow` on the sheet (`src/gfx/lighting.h:87-89`): **0.5** in `sheets/worlds/tarkan.json`. Zero cost.
- **Grass wind**: Tarkan's phase runs `xf * 50` against the default `xf * 5` (`ZzzLodTerrain.cpp:3282-3285` vs `:3300-3302`), a ripple ten times shorter along x. That is a grass-wave length knob if pass B wants MU's choppy desert grass; otherwise nothing.
- **Clear and fog**: black, MU's default. `dusty()` with a warm `dust_colour` on the sheet gives the desert haze for free (atlans-port.md `:1506-1511`).

#### 3.7 What it costs, all together

| line | account | estimate |
|---|---|---|
| sand haze, 2 full-screen layers | effects | 0.036 ms |
| braziers and glows (~100 lights) | shade | as Atlans's 216 lights, measured inside 4.3-4.9 ms |
| hidden smoke, geysers, impact glows | effects | < 0.03 ms |
| 10 bugs and trails | shade + effects | ~0.02 ms |
| eye trails on monsters | effects | < 0.02 ms |
| boss flames and staff rings | effects | ~0.03 ms while live |
| **sand puffs (MU's every frame)** | effects | **0.03-0.08 ms in a pack: measure** |
| **infernos on every rank-and-file swing** | effects | **0.05-0.1 ms in a pull: decision 5** |
| sun shadow pass | shadow | 3381 placements, unculled as Atlans's 4366 were (`atlans-port.md:122`) |

The fixed lines total under 0.15 ms; the two flagged lines are the effects account's risk. Measure the bare land, then with objects, then a pack fight, with `--budget --stats --repeat 3`, timers off, on the display the log names (memory: measure on the display you think).

---

### 4. Travel: gates, warp rows, safe zone and death landing

#### 4.1 Gates (rows only; all the machinery exists)

`src/sim/gates.cpp` (`kExits :9-49`, `kEnters :54-101`, lookups `:105-124`), the walker `Realm::throughGate` (`realm_move.cpp`), and the mode's map change on `Gated` (`play_mode.cpp`, via `mapNumbered`) are all generic since the Dungeon. Directions as stored: 7 North = (-1,+1), 3 South = (+1,-1), 0 none.

- `kExits` +=
  - `{57, 8, {187, 54, 203, 69}, 0, 0}`: Tarkan's hall, for death, Town Portal and the warp;
  - `{54, 8, {248, 40, 251, 44}, -1, 1}`: Tarkan, from Atlans;
  - `{56, 7, {16, 225, 17, 230}, 1, -1}`: Atlans, from Tarkan.
- `kEnters` +=
  - `{53, 7, {14, 225, 15, 230}, L_in, 54}`: Atlans to Tarkan;
  - `{55, 8, {246, 40, 247, 44}, L_out, 56}`: Tarkan to Atlans.
  - MU asks **130** both ways (S6 `:524-525`, WZ `Gate.txt:95, 98`). Ours is decision 2.
- **Grid**: every box has open tiles (§0.1). Atlans's enter 53 is half rock, which the walker's own-tile test copes with, as the tower's niches do.
- **index.py**: `GATE_BOXES_BY_MAP[7] += (14,225,15,230), (16,225,17,230)` and `[8]` (P7), so the solid-stamping never closes a door tile. Then recook **both** Atlans's and Tarkan's tables.
- **The barred speech** (`play.cpp`, "Only characters of level N or higher can enter.") is generic.
- **sim_test** `testTarkanGates`, on Atlans's pattern (`testAtlansGates`):
  - a hero at L_in on Atlans 15,227 goes Gated 53 and lands in 248-251 x 40-44;
  - at L_in - 1 he is Barred with L_in;
  - Tarkan 247,42 goes Gated 55 and lands in Atlans 16-17 x 225-230;
  - a death in Tarkan rises inside 187-203 x 54-69.
- **The minimap** labels both doors once `mapName(8)` exists (R4).

#### 4.2 The warp rows (decision 1)

0.95d's list has none. S6 has two at level 140: "Tarkan" (8000 zen, gate 57) and "Tarkan2" (8500 zen, gate 77). WZ's move level is 130. The options:
- (a) none: Tarkan is reached only through Atlans's lagoon, and its Tab card vanishes (R8);
- (b) one row, "Tarkan", landing 195,65, facing none;
- (c) both, Tarkan 2 landing on S6's 92,160 or WZ's 98,144. This needs §4.3.

Append after "Atlans" (`realm_travel.cpp:38`): `kTravels` becomes 15 or 16 (`travel.h:30`).

How a row opens follows Atlans's way (`realm_travel.cpp:113-125`). A map with a quest giver opens on speaking to them; one with no giver and several rows opens "by floor" (`byFloor_`). With no giver and one row, it opens the first time he stands there.

#### 4.3 Two rows need the flood fix

`Realm::settleFound` (`realm_travel.cpp:76-125`) floods walkable tiles from each row's landing **in turn**, and a tile already claimed stays with the first row. **Tarkan is one connected region of 23 832 tiles (§0.1).** Row "Tarkan" would claim every tile, "Tarkan 2" would own one, and since Tarkan has no giver, `byFloor_` would open "Tarkan 2" only on its exact landing tile.

The fix, which atlans-port.md §4.3 wrote and Atlans never needed, is ~10 lines: seed one queue with every landing and flood once (multi-source BFS). The Dungeon and the tower are unchanged (one landing per region). On Tarkan it gives the split of §0.1: 7 959 tiles to "Tarkan" (Mutants, Wolves) and 15 873 to "Tarkan 2" (Tantallos, Beam Knights, both bosses). Add a sim_test that standing near 92,160 opens "Tarkan 2" while the tower's floors still open as before.

#### 4.4 Safe zone, death landing, Town Portal: the gate-22 lesson

- Tarkan **has its own safe zone** (1 872 tiles), so death and the Town Portal stay in Tarkan. MuMain allows the Portal there (`MM/World/MapInfra/PortalMgr.cpp:54`).
- This needs exactly **one mu.db row**, `gates (57, 8, 187, 54, 203, 69, 1)`. With it:
  - index.py writes `gates.safe`;
  - the cook writes it into `tarkan.mur`'s header (`tools/cook.py` `cook_tables :2117-`; the header's safe gate x1..y2);
  - `haven()` finds it, and `MapRow.home` stays 0.
- **Without the row every death in Tarkan silently goes to Lorencia: Devias's gate-22 bug of `1ad73855` again** (docs/lost-tower-port.md:30-38, 1431-1436).
- Check after step 1: the `.mur` header reads `map 8 ... 187, 54, 203, 69`. A zero box is the bug.
- `arrive` {195, 65} is inside the box and open. The realm's `nearestOpen` covers the statue block anyway.
- The save resumes in Tarkan (not an `event` map). The Go Back! portal works as it does in the tower and Atlans.

---

### 5. Spawns and monster tables

#### 5.1 mu.db today (`sqlite3 -readonly source/mu.db`, 2026-10-05 19:2x)

- WAL is **0 bytes** and `-shm` was touched at 19:24 (another session has it open). Never write while a session is live; after writes, `pragma wal_checkpoint(TRUNCATE)` before committing (memory: mu.db writes sit in the WAL).
- `monster_kinds` holds 89 rows: 0-41, 45-49, 51, 52, 84-99, 111-130, 132-134, 150. **None of 57-63.** The columns are `number, name, level, health, minimum_damage, maximum_damage, defense, move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate, respawn_seconds, attack_skill`. The Hydra's `attack_skill` is stored as **50** (MonsterSkill as the Balrog's), Energy Ball as 17.
- `monster_spawns` (`id, number, x1, x2, y1, y2, count, map`; x2 comes before y1):
  - map 0: 9, map 1: 258, map 2: 9, map 3: 8, map 4: 448, map 7: 336 (ids 930-1265), map 11: 96;
  - **map 8 has none**; `max(id) = 1265`, so new rows start at **1266**.
- `gates` rows: 17 (0), 22 (2), 27 (3), 42 (4), 49 (7). **No 57.**
- `items`: `max(drop_level) = 75`. Tarkan's breeds are 72-93, so every row up to 75 reaches them, and the window (`realm_items.cpp:1130-1145`, `kGap` 1220-1222) picks the top band. The second-class gear (70-100) also falls here (R25).

#### 5.2 Steps to add Tarkan's spawns (source: pass A's and the user's call; OM 095d is the default)

1. `gates`: `(57, 8, 187, 54, 203, 69, 1)`.
2. Seven kinds, 57-63, from the chosen file. OM 095d is `Tarkan.cs:253-485`:
   - level, health, damage, defence, ranges, move delay 400 and attack delay 1400 (both divide by the 50 ms tick), rates;
   - `respawn_seconds` 10/20/150 written true;
   - `attack_skill`: 58, 59 and 63 = **50** (stored as the Hydra's); 61 = **17**; 57, 60 and 62 = 0.
   - The breeds share models with Golden Tantallos (82) and the Golden Wheel (83), the Kundun +5 pair (§7, decision 10). Their kinds are added only with that decision.
3. **Spawns**: load the 217 one-tile rows from `Tarkan.cs:36-252` (`CreateMonsterSpawn\(\d+, this\.NpcDictionary\[(\d+)\], (\d+), (\d+)\)`) as `x1 = x2 = x, y1 = y2 = y, count = 1, map = 8`, ids 1266-1482, in one transaction. Then check:
   - the count is 217, by kind 57x23, 58x63, 59x1, 60x32, 61x56, 62x41, 63x1;
   - every kind is in 57-63;
   - none is on rock (already checked on pass B's grid: 0 closed, 0 safe).
4. Checkpoint the WAL; run `pipeline/index.py`; run `tools/cook.py --world tarkan --only tables`, **then the other eight worlds' tables** (kinds are global in every `.mur`; memory: new items need every world's tables).
5. Figures one breed at a time: `--only figures --world tarkan --monsters <Name>`. The headless run raises every breed; the window holds back uncooked ones (`play_open.cpp`).
6. `realm_tuning.h` rows: `kResistances`, `kDropRates`, and `kBosses` (+59, +63; 58 is decision 5's twin).
7. Folk (decision 13): `FOLK_VERSION075[8]` from WZ's three, or none.
8. Sounds: `sounds.json` monster events for the seven from the wavs in §0; `index.py`; `--only showing`.

---

### 6. The build, in order

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. No window without asking; `--mute` and a scratch `--save` on any review run (memory). `checks` = `cmake --build build --target checks`. Read `git diff` of every file before staging, and commit through a private index if another session has staged.

**Step 0: the per-world air as a MapRow field** (optional, recommended; no content, no behaviour change). `maps.h` += `const char* air` (null = wind) and `bool openRoom`. Fill the eight rows. Read them at `play_open.cpp:33-41, 464-476` and `play_sound.cpp:415, 424-427`, in place of `windy_`, `dungeonAir_`, `towerAir_`, `waterAir_` and the new `desertAir_`. Check: `checks`; headless `--world dungeon|losttower|atlans --ticks 2000 --seed 7` logs are byte-identical. Seen: nothing, by design. If skipped, step 3 adds one more bool, as Atlans did.

**Step 1: bare land, reachable by `--world tarkan`.**
- `terrain.py`: `HIDDEN_BY_MAP[8]`, `BLEND_MESH_BY_MAP[8]`, `OPERABLE_BY_MAP[8] = {78}`, and the same in index.py. index.py: `GATE_BOXES_BY_MAP[8]` and `[7] +=`.
- Run `python3 pipeline/terrain.py $D/World9 tarkan source/world 9` (expect pass B's 36.4% walkable, 2.9% safe, 3381 placed).
- `source/world/tarkan/ground.json` with `tk_` sheets (pass B), then `tools/content.sh --world tarkan`, sync, `--only ground`.
- mu.db gate 57; checkpoint; index.py; `--only tables`.
- `maps.cpp` row `{"tarkan", 8, {195, 65}, true}`, `arrival.cpp`, `minimap.cpp case 8`, `sheets/worlds/tarkan.json` with `water_flow` 0.5 and a warm `dust_*`.
- Check: `./run.sh --headless --world tarkan --ticks 200 --no-hand` logs `grid 256 ... map 8`; the `.mur` header's safe box reads 187,54,203,69; `checks`. **Seen**: muted shots at the hall (195,65), the Atlans door (247,42), Tarkan 2 (95,150) and the far south-west (11,241), with fps from the log.

**Step 2: the gates.** `gates.cpp` 53/54/55/56/57 (§4.1, levels per decision 2); Atlans's tables recooked for its boxes; `testTarkanGates`. Check: `checks`; headless Atlans `--at 15,227 --level L` logs `Gated 53`, and one level lower `Barred L`. **Seen**: walking the lagoon door both ways in a muted run.

**Step 3: the air and the haze.** `world_desert` (desert.wav) in `sounds.json` + index.py + `--only showing`. `desertAir_` (or step 0's field), with the room Open. Music in the safe-zone arm. The `sand` and `sand_fine` effect sheets; `game/world/sand_haze` (§3.1). Check: the log names the air `world_desert` and `sand haze: 2 layers`; `--budget --stats` on the bare land, before and after the haze. **Seen**: a muted shot pair, haze off and on.

**Step 4: objects**, in pass B's small batches (memory: 3-6 objects built, cooked, shot and shown before the next). `tk_` sheets; `cook_one.py ObjectNN --world tarkan`, then `--only textures`, then `meshes` and `placements` (textures before meshes). The batches:
- the braziers Object08 with their lamps;
- the scrolling and bright kinds;
- Object82's chrome;
- Object09's 4x clip;
- the seat perch.

Check each batch with `--budget --stats`; the sway's posed count; the town log's sun placements. **Seen**: each batch in muted runs.

**Step 5: hidden emitters.** `ANCHOR_KINDS_BY_WORLD["tarkan"]` (§3.3): smoke kind 4 for Object71; a periodic kind for Object77; the geyser for Object84; the impact glow for Object64 (`impact` sheet). Check: the open line counts 3 + 19 + 10 + 18 anchors; the effects account. **Seen**: a geyser firing, shot on its 10 s beat.

**Step 6: the bugs.** Scurry per-world count and row; `CRAWLS["tarkan"] = "Bug02"`; the trail later (decision 9). Check: the log names 10 bugs; boids_test is unchanged. **Seen**: muted run at the hall's edge.

**Step 7: monsters, by breed batches** (§5.2's 1-4 first: the kinds, 217 spawns and every world's tables). Figures one breed at a time, each batch shown before the next:
- (a) **Mutant + Bloody Wolf**: melee, the walking sand puffs (thinned) and the eye trails (new `fx/eye_trails`). Measure the puffs in a pack here.
- (b) **Iron Wheel**: Monster42 with the Aquagold Crossbow, the three-bolt fan, death sand.
- (c) **Beam Knight**: Energy Ball (free), hand flames, hand lightning, inferno per decision 5.
- (d) **Tantallos**: Sword17 built as an item model first, BlendMesh 2, inferno per decision 5, death sand.
- (e) **The bosses, Zaikan and Death Beam Knight**: Staff09 built; the 18-staff ring; body flames; the dark ground light; `kBosses`.

Monster sounds go in `sounds.json`. Check after each batch: `sqlite3 -readonly source/mu.db "select number,count(*) from monster_spawns where map=8 group by number"`; headless `--world tarkan --ticks 30000 --level 90` reports 217 monsters; `checks`; `--budget --stats` in the densest Tantallos and Beam Knight pack (164,196: six of them within 8 tiles; they range over 5-183 x 87-246). **Seen**: arena shots (`--arena Monster43`), then a muted hunt.

**Step 8: travel rows** (decision 1). For two rows, the multi-source `settleFound` (§4.3) with its sim_test, `floorName case 77`, and `placeName` for Tarkan 2. Check: `checks`; the travel log opens "Tarkan 2" near 92,160 only.

**Step 9: folk, drops, mount and the side hooks**, per decisions 8 and 10-13: the Dinorant's lift, the feather map, Sevina's words, the Kundun +5 home, WZ's NPCs, and the mount card text.

---

### 7. Decisions for the user

1. **Warp rows**: none, one "Tarkan" or S6's two. *Recommend one row* landing in the hall (195,65), priced at S6's 8000 zen. Its level should match decision 2's gate rather than S6's 140, which is far above the breeds' 72-93. Atlans kept its warp level equal to its gate's (70). Two rows only if "Tarkan 2" is wanted for the hard half, which costs §4.3's fix.
2. **Gate levels Atlans<->Tarkan**: MU asks 130 both ways. The house rules so far: gates doubled, except Atlans, which was set near its monsters (in 70, out 60, against breeds 43-74). *Recommend in 100, out 70*: 100 sits just above the breeds' 72-93, as Atlans's 70 does over its 43-74, and level with the tower's last stairs. 70 is Atlans's own way in, so whoever can stand in Tarkan can stand in Atlans.
3. **Spawn source** (pass A): OM 095d's 217 with its stats, or WZ's 222 with its HP. *Recommend OM 095d* for the spawns, as Atlans took OM's, with WZ's RegTime, MoneyRate, MaxItemLevel and resistances as Atlans took WZO's.
4. **The sandstorm**: MU's two screen layers only, or ours on top (blowing sand in the world, a calm and gust cycle like Devias's blizzard). *Recommend MU's first* (0.036 ms), then judge in game.
5. **Monster infernos and sand**: MU puts an Inferno on every Tantallos and Beam Knight swing and a sand puff on every frame of every walker. *Recommend*: MU's inferno for the two bosses only; rank and file get a faint small version or none; puffs thinned one in four (the Budge Dragon's rate), after the monster auras' "subtle" rule. Also whether Tantallos (63 of them) joins `kBosses`' one-in-five area blow: *recommend no*.
6. **Eye trails** (MU's MoveEye ribbons on every Tarkan breed, new code): *recommend yes*, faint. They are the breeds' look in MU.
7. **Music**: tarkan.mp3 in the safe hall only, as Atlans, the tower and Noria do now. *Recommend yes.*
8. **Dinorant flight**: MU lifts the ridden Dinorant to 90 units in Tarkan (30 elsewhere) and plays its flying clips. *Recommend yes*: one per-map constant and a clip offset.
9. **Bugs**: MU's ten Bug02 crawlers. *Recommend the crawlers first and the energy trail after judging.*
10. **Kundun +5 home**: Tarkan is MU's home for Golden Tantallos (82, Monster43 @1.8, BlendMesh 2, HiddenMesh 2) and its ten Golden Wheels (83, Monster42) (`docs/kundun-box.md:40, 171`; `ZzzCharacter.cpp:13544-13551`). Porting Tarkan builds both models. *Recommend re-homing +5 here when the boxes are built* (kundun-box.md direction A or B), not to Lost Tower 7.
11. **Sevina's class change**: restore MU's "Atlans, the Lost Tower and Tarkan" in her words (`docs/class-change-quest.md:24-25`), and decide whether Tarkan joins the trial's hunt or the treasure ground (`realm_quests.cpp:222-228`). *Recommend the words and the treasure ground*, but not the hunt, which is already long.
12. **Loch's Feather**: add map 8 to `featherMap` (`items.h:192`, "rare drop, high maps"). *Recommend yes.*
13. **NPCs**: WZ stands Thompson the Merchant (231), a Guard (240) and Amy (253) in the hall; OM 095d stands none. *Recommend WZ's Guard and Amy* (a potion seller at the far end of Atlans), and Thompson only if a merchant is wanted there.
14. **Sword and Staff of Destruction**: build them as monster props only, or also as items. They are Season 1 items, outside the 0.75 table. *Recommend props now*; whether they drop belongs to the second-class gear list (docs/second-class-gear.md).
15. **Gold Medal** (docs/drop-boxes.md:37): when medals are built, Tarkan is one of MU's three gold-medal maps (4, 7, 8). Nothing to decide now.

---

### Side findings

- `docs/atlans-port.md:1325-1326` (Part C §0) says the Atlans side of the Tarkan door is "solid rock here". The current `source/world/atlans/attributes.png` has exit 56 fully open and enter 53 half open, all in the main region, which Part A `:425-427` already said. Correct Part C.
- `pipeline/index.py:57-71` `GATE_BOXES_BY_MAP` still lacks maps 4 and 11 (the tower's boxes, Devias's 28/44, Blood Castle's 66). This is the third Part C to note it.
- `pipeline/terrain.py:367-373` and `pipeline/index.py:44-50` are still two copies of `OPERABLE_BY_MAP`.
- `src/sim/realm_travel.cpp:76-125`'s floor flood still assumes one landing per connected region. Atlans sidestepped it with one row; Tarkan's second row would hit it.
- `tools/bot/bot.cpp:68-74` knows five worlds: no Atlans, no Blood Castle.
- `MapRow` was not widened for Atlans (`maps.h:18-34`). The air is now four name-keyed bools (`play_open.cpp:33-41`), with Tarkan's the fifth.
- OpenMU's VersionSeasonSix tree here has Tarkan's gates (`Gates.cs:197-201`) but no `Maps/Tarkan.cs`. Its spawns exist only in Version095d and WZ.
