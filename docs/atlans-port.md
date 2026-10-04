# Atlans port: the record, the data, and how it plugs in

Research notes, 2026-10-03. Atlans is MU server map **7**, client `Data/World8` + `Data/Object8`
(MuMain's `WD_7ATLANSE`), entered from Noria. Three research passes wrote this page in
`docs/lost-tower-port.md`'s shape. Part A is the record, Part B is the data and the look, and Part C
is the engine side, ending in the build steps. Nothing but this page was edited, and no cook or
window was run.

The passes' scratch is in
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/6a47dc36-9db3-4be7-a8fc-13b182c3f6ae/scratchpad/atlans/`:
- `a/`: grid decodes, spawn tables, and WebZen's official 0.99.60T server data (WZO, new to this port);
- `b/`: the terrain.py run, every Object8/Fish/monster export, decoded sheets and contact sheets;
- `partA.md`, `partB.md`, `partC.md`: the passes as written.

## Where it stands

**The spawn source is the user's call: OpenMU `Version075/Maps/Atlans.cs`'s spawns.**

**Step 1 is built (2026-10-03, the user: 'lets starts with making the ground').** It added:
- `source/world/atlans`: World8 through `terrain.py` (0-3.83 m, 8 slots, 35.5% walkable, 0.6% safe, 5215 placements of 40 kinds, none missing);
- `terrain.py` rows for map 7 (`HIDDEN_BY_MAP {22, 39}`, `BLEND_MESH_BY_MAP`, `GRASS_BY_MAP []`, `OPERABLE_BY_MAP {39}`) and `index.py`'s (`OPERABLE_BY_MAP`, `GATE_BOXES_BY_MAP` for gates 47 and 46);
- seven `at_` sheets and a `ground.json`: the sand and pebbly earth are sand, the weed bed and every rock are rock. Slot 5 (the caustic pools; World8 ships no TileWater01) is parked on `at_tilewater01.png`, a copy of the sand under its own name, until step 3;
- the `kMaps` row `{"atlans", 7, {21, 17}, true}` (spawn gate 49's middle; underground for now, so there is no wind and no leaves until step 0's `MapRow` fields), plus the banner and minimap names;
- `sheets/worlds/atlans.json`: B §6's starting sheet, as proposed.

Shot muted at the basin (21,17), the NE landing (225,50), the trench (62,160) and the SW court (30,225), at about 270 fps. The only error in the log is the missing `atlans.mut`, because no objects are cooked yet. Not yet judged by the user.

**The water's effects (2026-10-03, the user: 'now lets work on water animations and effects').** Steps 3 and 5 in part:
- **Caustics, as MU draws them.** `Object8/wt00-31` are cut into one 8x4 sheet, the showing's `caustic`. `World::open` hands it to the ground (`Ground::setCaustic`), which finds TileWater01's slot. The renderer binds the sheet on that layer's albedo stage, and `u_caustic` carries the frame (25 a second), the strength and the layer bits. `fs_ground` takes the layer's weight out of the blend and adds its frame after the lighting, times the TerrainLight, one frame across four tiles. The sun's shadow also takes them out: that part is ours. The sheet's `caustic` is set to 2.0, because at 0.8 the pools read only as a pale wash.
- **Bubbles**, in `game/world/bubbles.*`:
  - The 845 Object23 vents are cooked as anchor kind 6 (`EmitterKind::Bubble`, no light). Each runs MU's 4 s cycle, two seconds on.
  - The hero's head gives one a frame for the first second of every ten.
  - MU's rise and jitter are kept, on drop01's first column.
  - Ours: a vent rolls only within 14 m of the hero, the vents start at seeded phases, and a vent throws one bubble in four of MU's frames, added at 0.55.
- **Motes**: `Leaves` gets a third mode, the showing's `mote` (World8's leaf01), added. It ignores the "underground" indoors and rain.
- **The bed**: `world_water` (aWater.wav, -23 dB, level with aDungeon) on the wind's slot through `Play::waterAir_`, in the open room.

Not yet: a dying body's bubbles and the Bahamut's, the fish, and Object24's caustic sheets (an object step).

**Swimming (2026-10-03, the user: 'lets work on char swiming animations').** Step 7:
- `FigureBody` carries MU's Fly stance: PLAYER_STOP_FLY 11 to tread water (PLAYER_STOP_FLY_CROSSBOW 12 with a crossbow), PLAYER_WALK_SWIM 24, and PLAYER_RUN_SWIM 33 once he runs (ZzzCharacter.cpp:298-326, 624-630). The enum matches player.muc here, and all three were checked on the bench.
- `Play::underwater_` (Atlans) and `play_show.cpp`: a player off a safe tile swims and treads. A horse outranks it. The weapon goes on his back while he strokes and stays drawn while he treads (bBindBack, :15251-15254); out of combat it is slung anyway, by our rule.
- The swim plays at MU's cooked 0.35, nothing planted, and slows with the ground he covers, as the ride does.
- `player_step_swim` (pSwim.wav, -7 dB) on the walk's keys 1.5 and 4.5, off the safe zone (PlayWalkSound, :5368-5371).
- Not taken: MU's gloves +5 run rule. Our run is the realm's, as for every map.

Seen in muted `--play` runs at 21,46: a bare wizard, a knight with Sword05 slung while swimming, and an elf treading with CrossBow02.

**Lights (2026-10-03, the user: 'now lets work on light and fire emiters').** MU's Atlans has no fire and no object light (B §2): its light is the TerrainLight and five additive kinds, now built from Part B's exports under `source/world/atlans`, sheets `at_`-prefixed:
- **Object24**: the caustic pool, 10 of them, the user's "gates", lying in the carved rim.
  - It steps through MU's 32 water frames at 25 a second. That is a new scroll mode, `water_frames` (mode bit 2, `Material::waterFrames`). In it the renderer binds the ground's raw caustic sheet, and `fs_glow` cuts the frame's cell out with the mip read unwrapped. The asset's own upscaled atlas had bled frames together into seams.
  - It is lifted 6 cm (`LIFTED_BY_WORLD` in tools/cook.py), because at MU's 0.5 cm over the floor the ground fought it. The user: 'its not smooth its like it cuttent something wrong'.
  - It breathes 0.012-0.05: MU's 0.2-0.8 went white, and 0.08-0.3 was 'to light'.
- **Object33**: the glyph stones, 105 of them. `wood05` rock, with the `wood052` glyph added, breathing 0.1-1.0 as MU's 0-1.
- **Object35**: the glyph rims, 25 of them, flat frames lying on the floor round the pools. The same stone and glyph as Object33.
- **Object39**: the light shafts, 125 of them, `light01` added and steady. Faint by their own art, as in MU.
- **Object41**: the magic circles, 86 of them, `ioi01` added, breathing 0.15-0.5, under MU's 0.2-0.8. They turn: the rig is two rune discs (Box04, Box05) spinning a whole turn each, opposite ways, over five keys, at MU's `Velocity 0.05` (`play_speed`). The cooked loop is 4.0 s: five key steps at 1.25 a second, if MU wraps key 4 back to key 0 as one more step. Its last key repeats the first, so MU may hold the discs for that step; that is unchecked against MuMain's player. 3 circles buried past 60% are held still by Sway's rule. The others have one-key clips and no other MoveObject or RenderObjectVisual arm, so the glows are their whole effect. The user: 'objects which we gave now in atlans, check if there has to be some animations and effects'.
- **Ours, marked in the recipes**: a faint teal or cyan lamp (0.08-0.14, two or three tiles) at each stone, rim and circle, breathing with it, after the Lost Tower's lamp posts. 216 lights; five dense cells drop their farthest.
- **Breathing**: MU's sine breaths are carried as the world glow's slow wander between two levels, not as a sine.

Seen in muted `--play` runs at the basin's rim (22,24 and 16,22), at 243-255 fps.

**The lights reach the swimmer (2026-10-03).** The user: 'atlans light sources actualy has to make light, so if char swims inside light source we can see it', then 'chat is standing on light source but is not receiving that light'.
- The lamps were raised to light a figure: shafts 1.4-1.5 (new, at the shaft's middle), gates 0.9-1.1 (new), circles 1.0-1.2 (now at the circle's centre), glyph stones and rims 0.6-0.8.
- A light straight over the pool met the figure's sides edge-on, so it lit the sand and not him. New: a lamp's `wrap`, the emitter's spare byte (`TownEmitter::wrap`, `PointLight::wrap`, the lamp texture's row 2 w). `lampAt` takes n.l as (n.l + w) / (1 + w). Atlans's are 1; every other light is 0, as cooked before, and unchanged.

**Sound (2026-10-03, the user: 'lets work on audio effects and ambient on world, also what sound is played what char is swiming').** MuMain's Atlans plays four things:
- aWater, looped over the whole map (SceneManager.cpp:879-881);
- pSwim on the swim's keys;
- atlans.mp3, always (:1047-1052). Off here, by the house's music rule;
- the monsters' own cries.

mDeathBubble is Kalima's (ZzzEffectParticle.cpp:208, GMHellas.cpp); MU's Atlans bubbles are silent. Added, ours:
- **`Sound::Room::Water`**, Atlans's room in place of the open air. Every placed sound's low-pass is capped at 3.5 kHz (a new `ceiling` on the room preset, eased in the log with the rest of the room), with a dark, soft tail at -21 dB.
- **`world_bubbles`** (mDeathBubble, -20 dB, varied in pitch): a gurgle as a vent within 8 m comes on, no closer together than 1.5 s, and as his head's stream starts. In the densest vent field (60,60): 7 in 12 s. The log's missing `cooked/atlans/clips.json` is expected until a rigged object is cooked. Seen in muted `--play` runs at 21,46 and 21,17, at about 260 fps.

**Objects, batch 1 (2026-10-03, the user: 'lets start to work on atlans objects, lets do with small batches and check them in world').** Object06 (yellow branching coral, 421), Object07 (pink sea fan, 475), Object27 (sea-lettuce rosettes, 233), all cut out on `bu01`, `bu02` and `bu0`; Object30 and Object31 (encrusted boulders, 101 each, `ston033`, rock). Passed in docs/cook-log.md; about 230 fps at 184,24, 152,152 and 40,12.

**Still plants sway (the user: 'nature objects is not swaying').** MU stands the three plants still: one bone, one key. Ours, marked in their recipes:
- A recipe's `sway` becomes placement flag bit 4 (16). index.py carries it, and the cook sets it as `in_the_water`, not `swaying`, which names the clip models.
- Town sets `Drawable::sway`, and the renderer packs -1 into the instance's palette slot, which a static draw never reads.
- vs_static and vs_depth bend each vertex by common.sh's `swayed`: by the square of its height in the model, on one slow current phased by the placement's position, times the sheet's `sway` (Atlans 0.035) on the play clock (`u_sway`). The first cut, height^1.5 at 0.06 on two currents, moved the crowns enough that whole plants read as drifting (the user: 'natura cant change they grow position').
- The foot stays planted and the shadow sways with the plant.

**Objects, batch 2 (2026-10-03, the user: 'do next batch').** Object01-03, the sea-worn rocks (17, 6 and 7, `sea01`, rock), and Object04-05, the starfish (72 and 70, `star01`, chitin): MU's small red and pale starfish lying on the sand. 1854 placements stand; about 245-260 fps at 41,116, 16,18, 22,163 and 130,17.

**Objects, batch 3a (2026-10-03, the user: 'do next batch').** The shipwreck's large pieces, Object12-16: the bow with its mast (10), hull sections (13, 14), the stern (10) and a fallen mast with its sail (9). On `wooda` and `wood02` (plank), `wood01` (timber), `wood03` (cloth, the sail) and `wood04` (hardwood). The graveyard lies in the south-west court, 15-40 by 210-240. 1910 placements stand. The ~117 fps of these runs is the user's own fullscreen game sharing the GPU, not the wrecks.

**Objects, batch 3b (2026-10-03, the user: 'continou').** The wreck's small pieces, Object17-20: loose planking (44), broken frames (86), a yard with its sail lying flat (16) and a broken spar (16), on the same sheets. Strewn east of the middle round 168,168. 2072 placements stand; about 250-260 fps. The brown planking reads jade green under the teal light; kept (the user, shown it: 'wrect is nice now').

**Objects, batch 4 (2026-10-03, the user: 'don next batch').**
- Object21, the great rocks (126, `ston00`, rock): stood mostly buried, so they show as crags.
- Object36 and Object37, skeleton remains (57, 46, `bons`, bone), among the wreck timbers.
- Object38, a drifting skeleton (8) on nine bones, playing its own 60-key clip at the default 0.16, since no Atlans arm sets its Velocity (the `case 37` arms are Blood Castle's and Noria's).

2309 placements stand; 94 placements over 2 models play clips, 4 buried ones held. About 220-235 fps.

**Objects, batch 5a (2026-10-03, the user: 'do next batch').** The kelp, Object25 (single blades, 246, 28 bones) and Object26 (the bed, 951, 84 bones), cut out on `bu03`, each on its own 31-key clip at the default 0.16. 3506 placements stand; Sway plays 1291 placements over 4 models.

**Measured before and after, 1080p, 3 x 1200 frames, nothing else running:**

| spot | frame before | frame after | GPU before | GPU after |
|---|---|---|---|---|
| 198,90 | 4.58 ms | 4.62 ms | 4.25 ms | 4.51 ms |
| 138,114 | 4.24 ms | 4.64 ms | 4.12 ms | 4.31 ms |

Under the 5.5 ms budget, so the skinned kelp stays and the shader-sway fallback (decision 11) is not needed. A first run at 198,90 read 6.28 ms with another copy of the game open; it was discarded.

**Objects, batch 5b (2026-10-03, the user: 'do next batch').** The weeds, cut out:
- Object28 and Object29, weed-and-coral clumps (95 and 40, 89 and 91 bones, `bu0`, `bu03` and `bu01`, 31-key clips).
- Object32 and Object34, low olive tufts (296 and 328, 5 bones, `bu04`, 15-key clips).

4265 placements stand; Sway plays 2050 over 8 models. Frame, 1080p, 3 x 1200: 198,90 at 4.59 ms (GPU 4.67), 138,114 at 4.53 (4.46), the densest weeds at 40,56 at 4.25 (4.59). All under 5.5 ms.

**Objects, batch 6 (2026-10-03, the user: 'do last abtch').** The giant octopus, skin, each part on its own clip at the default 0.16:
- Object08, its head (23, `bfish07`, 34 bones, 30 keys);
- Object09, its mantle and sucker base (27, `bfish072`, 60 keys), stood deep in the floor;
- Object10 and Object11, its tentacles (35 and 16, `bfish073`, 30 keys).

It coils round the wrecks, about 56,56 and along the court's west edge. Under the teal light its red reads dark blue-grey.

**Every placed object kind is built: 38 models, 4366 placements.** The two hidden kinds are anchors and lean boxes; Object22 is never placed. Frame with everything in, 1080p, 3 x 1200, nothing else running: 198,90 at 4.51 ms (GPU 4.77), 138,114 at 4.27 (4.53), 56,56 among the octopuses at 4.92 (5.05). All under 5.5 ms. A first 198,90 run at 7.42 ms, with the machine otherwise loaded, was discarded. The sun's shadow pass draws all 4366 placements unculled (the town log's `sun 59 chunks and 4366 placements, unculled`): a cost to watch as monsters arrive.

**The hidden kinds and Object22 (2026-10-03, the user: 'do: two hidden ones (bubble vents and lean boxes) and Object22').**
- Object40, the four lean boxes in the basin (15,23, 28,8, 16,24, 23,26), now lean: `PERCHES[7] = {39: (3, True, False, False)}` in tools/cook.py, after MU's `Pose = true`, turned to the box (ZzzInterface.cpp:1736-1742). They are not on MU's lean-cursor list and have no tall box. The tables cook says 4 perches. Hidden as MU hides them.
- Object23's 845 vents stay hidden: MU draws no mesh there; they are the bubble vents (game/world/bubbles.h).
- Object22 is two triangles of `bu02` that no placement uses; built, it would never show, so it is not.

**The gates (2026-10-04, the user: 'lets migrate noria -> atlans gates and add lvl requirments for gates with speach buble ho you did for other gates').** Step 2, in src/sim/gates.cpp:
- Exits 48 (Noria, 240-241 x 240-243, North), 46 (Atlans, 14-15 x 12-13, South) and 49 (the basin, the spawn gate: death and the Town Portal).
- Enters 45 (Noria, 242-245 x 240-243, to 46) and 47 (Atlans, 9-11 x 9-12, level 60, to 48), from Gates.cs:135, 148-149 and 205-206. Gate 45 asks **120**, ours (the user, 2026-10-04: 'increase lvl requirment to 120'); MU's is 60. The way out keeps 60, so nobody under 120 already in Atlans is shut in.
- A hero under level 60 is Barred with 60, and the play says MU's 'Only characters of level 60 or higher can enter.' over him, as every gate does (play.cpp).
- index.py `GATE_BOXES_BY_MAP[3]` keeps Noria's two boxes open; Noria's and Atlans's tables are recooked.
- sim_test `testAtlansGates`: a level 120 knight goes in and a level 60 one comes out, each onto the right exit box; a level 119 one is refused at 45 with 120, and a level 59 one at 47 with 60. 3838 checks, the three old failures.
- Also `testAtlansPerches`. The 11 basin tiles MuMain's grid closes (12-14 x 17-18, 13-14 x 21-22, 15,23) are opened as WebZen's official grid has them (terrain.py `OPEN_BY_MAP[7]`), so the lean box at 15,23 leans; the one at 23,26 is NoMove in every grid and is refused, as MU's is.

**Every gate's level doubled (the user, 2026-10-04: 'also increase other gate requirments because we have fast paced exp gains').** Ours, in src/sim/gates.cpp's kEnters, after Atlans's 60 went to 120:

| gates | before | now |
|---|---|---|
| Lorencia and Noria (23, 25) | 10 | 20 |
| to Devias (18), out of the Lost Tower (43) | 15 | 30 |
| into the Dungeon and all its stairs | 20 | 40 |
| into the Lost Tower (28) and its first two stairs | 40 | 80 |
| its last four stairs | 50 | 100 |

Free ways out stay free; Atlans's way back keeps 60. The sim_test gate checks follow. The Tab travel window's warp levels are doubled the same way (realm_travel.cpp kRows; the user: 'fix tab travels also'): Lorencia and Noria 20, Devias 40, the Dungeon 60/80/100, the Lost Tower 100/100/100/120/120/140/140. Prices stay MU's.

**Fish, the school (2026-10-04, the user: 'what abour prodecual atlans grass and fish boids?').** Step 6, its first half:
- Fish02 is built as a world model nothing places, as the Bat is: 0.35 m, 5 bones, an 8-key swim, `bfish04`. It is named in tools/cook.py's `AIRS`.
- `Flight` has a fish mode (`setFish`, `school`, `swim`) and a pool of `kMaxSlots` 40; birds still use the first 5, and boids_test is unchanged.
- As CreateAtlanseFish and MoveBoid's Atlans arm (GOBoid.cpp:886-918, 1141-1160):
  - every slot is born again as it empties while the hero is north of row 128, and none south of it;
  - each fish holds 1.5-3.5 m over the floor at Velocity 0.3 or 0.25;
  - it cruises then darts on a 4 s timer (the rolls' means, 1.1-1.8 then 3.6 m/s), flocks, and goes at 15 m or one frame in 512. Unlit, Scale 0.8.
- Ours: a fish is born off the frame on the birds' ring and leaves the frame before its slot empties, and past 7 m it turns back toward the hero (the bat's tether), since born off-frame the school swam out of sight. In the south half the school is sent away.
- Not yet: Fish03 (MU rolls Fish02 or Fish03), the ten bigger swimmers (Scurry: Fish04-07 north, the glowing Fish08/09 south), and MU's turn at the safe zone's edge.
- Procedural grass: MU has none in Atlans; a seagrass carpet would be ours, which is asked of the user.

Nothing else is built: no monsters, warp row or drops. Atlans is reached by `--world atlans`.

## The one thing to know first

**Atlans is 0.75, but OpenMU's 0.75 grid is wrong for it; use the client's.**
- OpenMU's `075_Terrain8.att` closes a 6,584-tile belt (x 40-240, y 54-211): the middle ridge path
  and the east corridor. It also bakes "monster standing" bytes that `WalkMap` reads as walls.
- As a result, 278 of OpenMU's own spawns fail OpenMU's placement check, and 89 stand on closed tiles.
- The client grid (MuMain `EncTerrain8.att`) is the official 2005 one, apart from 11 tiles at a Season 6
  door, and every OpenMU spawn is open on it.

**Settled: the client grid with OpenMU's spawns** (A §3, B opening).

## In one screen

- **One map, one region.** 23,277 walkable tiles, all connected, with no void and no floors. The safe
  zone is the NW basin (10-34 × 10-28). Atlans 1/2/3 are only Season 6 warp landing boxes on that one grid.
- **The way in.**
  - Noria gate 45 (242,240-245,243), level 60, leads to exit 46 beside the basin.
  - Gate 47 (9,9-11,12), level 60, returns to Noria exit 48.
  - Death and the Town Portal stay in Atlans (spawn gate 49).
  - 0.75 has **no warp row**; Season 6 adds three (A §2, C §4).
- **337 OpenMU spawns, all single tiles**, of seven breeds, levels 43-74:
  Bahamut 45 ×29, Vepar 46 ×45, Valkyrie 47 ×43, Lizard King 48 ×65, Hydra 49 ×4, Great Bahamut 51 ×66,
  Silver Valkyrie 52 ×85. The Sea Worm (50) is defined but never placed. OpenMU's guard,
  Baz (240 at 23,17), is extra.
  - **Count settled at 337.** Pass A counted 336; a recount of `Atlans.cs:59-396` gives 66 Great Bahamuts, not 65.
  - The official 2005 table also has 337, but OpenMU moved three spots and dropped one (A §4.1 flags).
- **Stats.** OpenMU equals the official 2005 `Monster.txt`, respawn included (8 s / 15 s, Hydra 150 s).
  - The Hydra joins `kBosses` (Flame of Evil, one in five).
  - The Silver Valkyrie joins `kChillers`.
  - The Valkyrie needs a `shoots()` row (A §4.2, C side findings).
- **Figures.** Every body is new: Monster34-38 (Bahamut, Vepar, Valkyrie, Lizard King, Hydra).
  - Great Bahamut and Silver Valkyrie are re-dresses.
  - The Valkyrie's Bluewing Crossbow is cooked; the Lizard King's Staff07 is not.
- **Drops.** Nothing is Atlans-specific in 0.75.
  - These items reach the pool: Bluewing Crossbow 68, Staff of Resurrection 70, Crystal Sword and
    Aquagold Crossbow 72, Scroll of Aqua Beam 74 (Hydra only).
  - Staff07, Mace06 and CrossBow07 have no assets yet.
- **The look is a sunlit sea floor made of small things, not a fog.** MU's clear and fog are black; the blue is all in
  the painted TerrainLight. Underwater reads from:
  - 32-frame additive caustics on 4,089 tiles;
  - bubbles from 845 hidden vents, heads, deaths and the Bahamut;
  - fish, 40 boids plus 10 larger ones that change north and south of y 128;
  - 80 drifting motes;
  - swimming off the safe zone;
  - 20% shadows;
  - swaying kelp, 125 light shafts and pulsing glyphs (B §2).
- **Size.** 5,215 placements of 40 kinds. **2,276 are rigged across 13 kinds**, against Lorencia's 20;
  Object26 alone is 951 placements at 84 bones each. This is the frame risk to measure first (C §3.6, §3.9).
- **Sounds.** 11 wavs are missing (aWater, pSwim, mBahamut1, mBepar1/2, mValkyrie1/Die,
  mLizardKing1/2, mHydra1/Attack1), plus `atlans.mp3`.
- **Rules.** WebZen and MuMain refuse the Horn of Uniria (and from 0.97 the Dinorant) at the gate and
  on equip; OpenMU does not. Water moves the +5 run from boots to gloves (A §6).

## Traps a first build would fall into

1. **The 075 grid.** Building on OpenMU's `075_Terrain8.att` walls in a fifth of the map and a quarter of the spawns.
2. **Wind and leaves underwater.** `play_open.cpp:33` gives every world but Noria the wind, and
   `raiseAirs` opens leaves. Step 0 turns these into `MapRow` fields (`air`, `grassy`, `underwater`),
   the refactor the Lost Tower and Blood Castle Part Cs also asked for.
3. **Slot 5 is not water.** World8 ships no TileWater01, and its overlay is the caustic flipbook.
   Park it on a neighbour's sheet until the caustics step, and set `WATER_FLOW_BY_MAP[7]` to nothing.
4. **The Tab window drops Atlans.** A `kMaps` row with no travel row vanishes from it (`travel.cpp:195-197`).
5. **Three warp rows on one region.** `settleFound` (`realm_travel.cpp:75-121`) gives a connected
   region's tiles to its first row, so Atlans 2/3 would never open. A multi-source flood fixes it, and
   gives the Dungeon and the Lost Tower the same result as today.
6. **`AIRS`/`CRAWLS` in `tools/cook.py` hold one model a world**; the fish need lists.
7. **Sheet names.** Use the prefix `at_`, which is free.
8. **Another session's Blood Castle work** is uncommitted in `src/sim/realm*`, `tracker.*` and
   `sim_test.cpp`. Read `git diff` before staging Atlans changes there.

## Decisions for the user

Settled already: OpenMU's spawns (the user's call) and the client grid (it is the only one that fits them).

1. **Warp rows.** None (0.75's walk only), one "Atlans" row opened on arrival, or Season 6's three
   (Atlans 2 at 225,50 and Atlans 3 at 62,157, opened per zone as the Lost Tower's floors are; needs trap 5's fix).
   *Recommended: one row, plus 2/3 opened per zone.*
2. **Swimming.** MU's swim walk/run and treading idle off the safe zone with the weapon slung, or keep
   our Mixamo run (the running rule)? And does the gloves +5 run rule come with it?
   *Recommended: swim clips as visuals only, with no speed rule.*
3. **Caustics.** MU's flipbook on its 4,089 tiles (faithful, one sampler, under 0.05 ms), or ours over the
   whole floor and the figures. *Recommended: MU's.*
4. **Haze.** MU's black fog, or a teal water haze (ours) so distance reads as sea, not night.
   *Recommended: teal haze, light.*
5. **Music.** `atlans.mp3` always (MU), as a rare fight track (house rule), or silence. `aWater.wav` is the bed either way.
6. **Mounts.** Keep MU's Uniria/Dinorant ban. *Recommended: ban (WebZen and MuMain agree).*
7. **Respawn.** Official +1: 9 / 16 / 151 s. *Recommended.*
8. **Baz** in the safe basin (OpenMU only; the official data has no Atlans NPC). *Recommended: keep.*
   There is no merchant, so potions mean a walk to Noria.
9. **The Hydra** turns but does not walk (move range 0), as MU draws a rooted body. *Ask.*
10. **Fish height.** MuMain's bug pins the larger fish to the sand. Swim them at 0.5-2 m (ours), or keep MU's.
11. **Kelp cost.** If the 951 Object26 overdraw the frame: a shader sway in place of skinning, or fewer swaying.
12. **An underwater sound room** (a muffled bus, ours; MU has none).

## The build, in order

The full steps are in Part C §6. In short:
- **0:** per-world switches become `MapRow` fields.
- **1:** bare land, reachable by `--world atlans`.
- **2:** gates 45-49.
- **3:** caustics, the water bed sound and the motes.
- **4:** objects, by count, measuring Object26.
- **5:** bubbles.
- **6:** fish.
- **7:** swimming.
- **8:** OpenMU's 337 spawns and seven breeds into `mu.db`, then `--only tables` for every world.
- **9:** travel rows.
- **10:** folk, music, mounts and drops, per the decisions.

## Side findings (outside Atlans)

- **WebZen's official 0.99.60T data is on disk now** (`scratchpad/atlans/a/w996/Files/Data/`). It outranks the 2012 repack.
  - Its `gate.txt` puts every Lost Tower gate at **level 80**, and its `movereq` puts the warp levels at
    90-120. `lost-tower-port.md` §2.2/§2.3 called both values the repack's; they are WebZen's 0.98n-0.99 values.
  - Its Dungeon gate 5 is level 40, where OpenMU says 20.
- **Lost Tower respawn.** Officially it is 10 s (Balrog 150 s). `realm_tuning.h:373-379` carries the repack's
  5 / 10 (6 s / 11 s) under "WebZen's respawn", so Lost Tower Decision 3 was made on the repack's number.
- `pipeline/index.py` `GATE_BOXES_BY_MAP` still lacks the Lost Tower's, Devias's 28/44 and Blood
  Castle's 66 boxes, and its `OPERABLE_BY_MAP` duplicates `terrain.py`'s.
- `tools/bot/bot.cpp:67-71` knows five worlds; Blood Castle is not among them.
- There is an untracked 0-byte `MU2_BGFX/mu.db`; the store is `source/mu.db`.


## Part A: the record

Research pass A, 2026-10-03. Nothing in the repo was edited, cooked or run.

Paths are as in `docs/lost-tower-port.md`: OM, OMGL, MM, MMD (= D), WZ (WebZen GameServer 1.00.93
C++), WZD (the 2012 Elion repack's `_Server Files/Data`). This pass adds one source the earlier
ports did not have on disk:

- **WZO**: WebZen's **official 0.99.60T server package** (github `makaytrue/0.99.60T`,
  `Files.rar`, `Files/Data/`, Korean headers dated 2004-03 to 2005-07: `Monster.txt` "V.1.2.7.9,
  1.00m, 2005-03-07", `gate.txt` "0.99, 2004-08-16", `movereq(kor).txt` "0.98n, 2004-03-29",
  `MoveLevel.txt` "0.99, 2004-07-23"). `docs/kundun-box.md:9-11` already names it WebZen's own
  data. Unpacked to `scratchpad/atlans/a/w996/Files/Data/`.

Scratch (`scratchpad/atlans/a/`): `grids.py` (decodes every attribute grid with a copy of
`pipeline/terrain.py`'s `decrypt()` + BuxCode), `lab.py` (8-neighbour flood fill and walk),
`spawns.py`, `wzset.py` / `wzoset.py` (MonsterSetBase map-7 rows), `items.py` (OM 075 drop levels);
outputs `*_out.txt`, `spawns_rows.txt`, `wz_map7.txt`, `wzo_map7.txt`, `grids.npz`.

### 0. The one thing to know first

**Atlans is in 0.75, as one open 256² sea floor, reached only by Noria's north-east gate at level
60, with no warp-list row.** Its safe zone is its own, so death and the Town Portal stay in
Atlans. "Atlans 1/2/3" are not floors: they are three landing boxes of the later warp list (0.98n
on), all on one connected grid with no gate between them.

**The grid trap.** OM's `075_Terrain8.att` is not usable with OM's own spawn table: it closes a
6 584-tile belt (the east column and the central trench, where "Atlans 3" lands), so **89 of OM's
336 spots sit on blocked tiles**, and it carries 196 baked "monster standing" bytes (value 2) that
OM's `WalkMap` reads as walls, so **278 of the 336 spots fail OM's own placement check** and would
throw (`OMGL/NPC/NonPlayerCharacter.cs:81-96, 207-238`). The client's `EncTerrain8.att`, WebZen's
official 2005 `Terrain8.att` and the repack's are the same open map (11 tiles apart), and every one
of OM's 336 spots is an open tile on it. **Settled: OM 075's spawns (the user's call) on the
client grid.**

**WZO settles the stats.** WebZen's official 2005 `Monster.txt` agrees with OM 075 on every
Atlans number, respawn included (8 / 15 / 150 s). The repack's Hydra (level 108, 89 000 HP) and
its flat RegTime 5 are the repack's edits.

### 1. Map

#### 1.1 Map, and is it 0.75

- `OM/Version075/Maps/Atlans.cs:20` `Number = 7`, `:25` `Name = "Atlans"`; registered in 0.75's
  map list `OM/Version075/GameMapsInitializer.cs:37`. **0.95d** reuses the 075 class as is
  (`OM/Version095d/GameMapsInitializer.cs:37` `typeof(Version075.Maps.Atlans)`). **Season 6**
  subclasses it (`OM/VersionSeasonSix/Maps/Atlans.cs:12`) and changes only the terrain file
  (`:25`, no `075_` prefix) and adds Marlon (229) at 17,35 (`:28-35`).
- WZ: `MAP_INDEX_ATHLANSE = 7` (`WZ define2.h:4044`), regen box `gRegenRect[7] = 14,11-27,23`
  (`WZ MapClass.cpp:200`). MuMain: `WD_7ATLANSE` (`MM/World/MapInfra/MapManager.h:16`), data
  `D/World8` + `D/Object8` (world index + 1). No attribute sanity tile for Atlans in
  `MM/Render/Terrain/ZzzLodTerrain.cpp:202-219` (it checks maps 0-4 only).
- **Answer: Atlans is in 0.75** (OM 075, unchanged in 0.95d), and in 0.97d/0.99 (WZ, WZO). The
  replica target is OM `Version075`, cross-checked against WZO (official 0.98n-1.00m data) and WZ.
- **Exp multiplier 1**, no map requirement (base `OM/BaseMapInitializer.cs:118-129`; Atlans.cs
  overrides none of it). **No drop groups of its own** (no `InitializeDropItemGroups` override).
- **Underwater**: `AdditionalInitialization` calls `AddUnderwaterMovementPowerUp()`
  (`Atlans.cs:44-48`), which sets `Stats.IsUnderwater = 1` on the map
  (`OM/BaseMapInitializer.cs:320-326`) -- §5.
- **NPCs** (`CreateNpcSpawns`, `Atlans.cs:51-54`): one, NPC **240 (Baz the Vault Keeper)** at
  **23,17** facing SouthWest, commented "Guard" (`:53`), on a safe tile (client value 1).
  WZD has Baz 240 at 14,22 dir 2 and **Amy 253 (potions) at 16,24** (`WZD MonsterSetBase.txt:47-48`).
  **WZO has no map-7 NPC row at all** (its MonsterSetBase's map-7 rows are the 337 monsters, lines
  2807-3146). Season 6 adds Marlon (above); WZ's quest NPC can also be teleported to Atlans 17,25
  (`WZ gObjMonster.h:131-132`, 0.97+ quests).
- **Spawn gate / safe-zone map**: exit gate **49** is `isSpawnGate: true`
  (`OM/Version075/Gates.cs:148`), so `SafezoneMapNumber` is Atlans itself
  (`OM/BaseMapInitializer.cs:91`).

#### 1.2 One grid, three landing boxes

Flood fill, 8-neighbour; walkable is `(flags & (NoMove|NoGround)) == 0` on the client grid and byte
0-3 on the server grids (the 2 bit is a baked "standing" mark, §2). Every grid but 075 is **one
region**:

| grid | walkable | regions | safe-bit tiles (walkable) |
|---|---:|---:|---|
| client `D/World8/EncTerrain8.att` (2-byte flags) | 23 277 | 1 | 421 (319), x 10-34, y 7-28 |
| WZO `Terrain8.att` (official, 2005) = WZD = `D/World8/Terrain8.att` (3-byte XOR) | 23 288 | 1 | 359 (330), x 11-34, y 8-28 |
| OM `Terrain8.att` (S6) | 23 379 | 1 | 421 (421) |
| OM `075_Terrain8.att` = `D/World8/Terrain.att` (plain, byte for byte) | 17 528 | 23 (main 16 672; the rest edge strips at x 252-255 / y 252-255) | 426 (330): the start's 359, plus 66 safe+**blocked** at x 75-81, y 146-157 and one at 107,77 |

The **"three Atlantis"** are the warp list's landing boxes (0.98n on, §1.4); there is no gate
between them, unlike the Dungeon's and the Lost Tower's floors:

| area | landing box | walk from the arrival (client) | OM spots nearest it by walk (of 336) |
|---|---|---:|---|
| Atlans 1 (NW, the start) | exit 49, 15,11-27,23 (spawn gate) | 0 | 76: Bahamut 29, Vepar 37, Valkyrie 10 |
| Atlans 2 (NE) | 75, 225,50-228,53 | 318 | 122: Valkyrie 32, Great Bahamut 35, Lizard King 23, Silver Valkyrie 22, Vepar 8, Hydra 2 |
| Atlans 3 (centre-south trench) | 76, 62,157-68,163 | 490 | 138: Silver Valkyrie 63, Lizard King 42, Great Bahamut 30, Valkyrie 1, Hydra 2 |

(Tiles nearest each box by walk: 5 241 / 8 323 / 9 713. Walks from exit 46: NE box 322, trench box
494, the SE Hydras 463-474, the **SW lagoon's Hydras 743-748**, the farthest point of the map.)
The shape: the arrival basin in the north-west; a north band running east to the NE; an east
column falling south to the SE Hydras; a west column falling to the SW lagoon (Lizard Kings and two
Hydras, x 15-60, y 200-237); and a central trench (x 40-180, y 100-180) joining the two columns.
The 4×4-cell map is `a/map_out.txt`.

**The 075 grid closes the east column and the trench** (6 584 tiles, x 40-240, y 54-211; 6 538 of
value 4 and 46 of 5): there the map is the NW basin, the north band to the NE corner, the west
column and the SW lagoon (`a/diffmap_out.txt`). The Atlans-3 box is on blocked tiles in it, and
so are 89 of OM's spots (§4.1).

### 2. Gates and warps

Directions are OpenMU's (`OMGL/DirectionExtensions.cs:21-35`): 3 South = (+1,-1), 7 North = (-1,+1).

#### 2.1 Exit gates (`OM/Version075/Gates.cs:135, 147-149`)

| gate | map | box | dir | role | OM 075 | WZO gate.txt | open tiles (075 / client) |
|---:|---|---|---|---|---|---|---|
| **49** | Atlans | 15,11 - 27,23 | 0 | **spawn gate**: death, Town Portal, warp "Atlans" | `:148` | l.90: **14**,11-27,23, flag 0, level 60 | 169 / 168 of 169 |
| **46** | Atlans | 14,12 - 15,13 | 3 South | arrival from Noria gate 45 | `:149` | l.85, same | 4 / 4 |
| **48** | Noria | 240,240 - 241,243 | 7 North | back in Noria, NE corner | `:135` | l.88, same | 7 / 7 of 8 (Noria grids) |

0.95d identical (`OM/Version095d/Gates.cs:149, 161-163`). Season 6 (`OM/VersionSeasonSix/Gates.cs:169,
189-195`) adds exits **75** (225,50-228,53) and **76** (62,157-68,163), both direction 0, as warp
targets; **56** (16,225-17,230, South) out of Tarkan; **266** (16,19-17,20) out of Elbeland. WZO
lines 142-143 carry 75 and 76 (as 225,53-228,50 and 62,163-68,157, flag 0, level 60).

#### 2.2 Enter gates (`OM/Version075/Gates.cs:205-206`)

| gate | on | box | to | level OM | WZO | WZD | open tiles | line |
|---:|---|---|---|---:|---:|---:|---|---|
| **45** | Noria | 242,240 - 245,243 | 46 (Atlans) | **60** | 60 (l.84) | 60 | 9 of 16 (both Noria grids) | `:205` |
| **47** | Atlans | 9,9 - 11,12 | 48 (Noria) | **60** | 60 (l.87) | 60 | 9 / 9 of 12 | `:206` |

Every source agrees: **60 both ways** (0.95d `Gates.cs:214-215`, S6 `:522-523`, WZO, WZD).
`webzen-audit.md:57` already lists "Missing: Noria->Atlans gate 45". MU2_BGFX's `src/sim/gates.cpp`
carries no 45-49 today, and `src/game/ui/travel.cpp:195` lists Atlans (7) among the dimmed
"coming" worlds. The gate out (47) sits in the safe basin's north-west corner, 5 steps from the
arrival. Season 6 adds **53** (Atlans 14,225-15,230, level 130, to Tarkan 54) in the SW lagoon and
**263** (13,19-14,20, level 10, to Elbeland); both post-0.75. The Tarkan door's tiles are already
open on the 075 grid.

WZ's gate code takes the level from Gate.txt and gives the MG/DL two thirds (`WZ user.cpp:27545-27567`,
lost-tower-port §2.2); no 0.75 class pays less.

#### 2.3 Warp list

- **0.75 and 0.95d have no Atlans row** (`OM/Version075/Gates.cs:44-57`, `OM/Version095d/Gates.cs:42-55`):
  in OM's 0.75, Atlans is reached on foot only. 0.75 had no Move window anyway (`Gates.cs:40`).
- WZO `lang/Kor/movereq(kor).txt:21-23` (0.98n, 2004-03-29): **Atlans 4 000 zen / level 70 -> gate
  49; Atlans2 4 500 / 70 -> 75; Atlans3 5 000 / 70 -> 76**.
- Season 6: same zen, levels **70 / 80 / 90** (`OM/VersionSeasonSix/Gates.cs:57-59`).
- WZO `MoveLevel.txt` (0.99, 2004-07-23): map 7 at **60** (the `/move` command's level).
- MU2_BGFX's M key is free and unlevelled (dungeon-port.md Decision 6); its rows open on arrival
  (lost-tower-port Decision 1).

### 3. Terrain attributes

Seven grids were read (`a/grids.py`, `a/regions.py`; the client's through `pipeline/terrain.py`'s
own cipher, copied, not run in place):

| file | 0 open | 1 safe | 2 stand | 3 | 4 blocked | 5 safe+blk | 6 | other | walkable |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
| OM `075_Terrain8.att` | 17 003 | 329 | 195 | 1 | 47 912 | 96 | -- | -- | 17 528* |
| MMD `World8/Terrain.att` (plain) | = 075, **0 bytes differ** | | | | | | | | |
| WZO `Terrain8.att` (2005) | 22 914 | 330 | 44 | -- | 42 218 | 29 | 1 | -- | 23 288* |
| WZD `Terrain8.att` / MMD `World8/Terrain8.att` (3-byte XOR) | = WZO, 0 bytes differ | | | | | | | | |
| OM `Terrain8.att` (S6) | 22 958 | 421 | -- | -- | 41 646 | -- | -- | 511 of 204 (rows 254-255) | 23 379 |
| MMD `World8/EncTerrain8.att` (client, 2-byte) | 22 958 | 318 | -- | 1 | 42 156 | 102 | 1 | -- | 23 277 |

\* counting the stand bit as walkable (bytes 0-3). OM itself walks only bytes 0 and 1
(`OMGL/GameMapTerrain.cs:132-143`), which leaves its 075 grid 17 332 walkable tiles.

**The 2 bit is a baked server snapshot.** 189 of the 075 file's 196 value-2/3 tiles are exactly
OM spawn tiles (one is 11,10 by the exit gate, a standing player or NPC); WZO's 44 are WZO spawn
tiles. The files were saved from a running server's attribute map with its monsters on it. OM
reads 2 as a wall.

Tile-for-tile: client vs WZO **11 walkable tiles** (all client-blocked: 12-14,17-18 and
13-15,21-23 in the safe basin, round Season 6's Elbeland door 263 at 13-14,19-20); client vs OM
S6 102 (all OM-open, in the safe basin); client vs 075 **7 615** (6 584 opened by the client in the
east column and trench, the rest the stand marks and the safe basin's rim).

**Safe zone**: one, the NW basin (x 10-34, y 7-28 client; 11-34, 8-28 WZO/075), round spawn gate
49 and its exit 46. No other safe tiles in any file, apart from 075's 66 safe+blocked tiles in the
closed trench.

**Settled: the client grid**, as for every earlier world. It is WebZen's 2005 map but for 11
tiles at a Season 6 door, and every OM 075 spot and both gates' boxes are open on it. The 075 file
cannot carry OM's own table (§4.1), and the belt it closes is real, dressed scenery in the client
(871 of `EncTerrain8.obj`'s 5 215 placements stand on or by it).

### 4. Monsters

#### 4.1 Spawns (`OM/Version075/Maps/Atlans.cs:58-396`) -- settled: OM 075's table

**The user's call (2026-10-03): the spawns are OpenMU's.** OM `Version075` has Atlans, so the table
is `Atlans.cs`'s, unchanged in 0.95d and Season 6 (neither overrides `CreateMonsterSpawns`).

Every row is the single-tile `CreateMonsterSpawn(id, def, x, y)`, ids 100-437, no direction, no
count: **336 monsters on 335 tiles**. The one doubled tile is **122,29**, a Vepar (`:133`) and a
Valkyrie (`:221`) on it. No breed is doubled wholesale (the Dungeon's slip is not here). All 336 are
open, non-safe tiles of the client grid (`a/spawns_out.txt`).

| # | breed | spots | where (client grid) | OM lines | on 075-blocked tiles |
|---:|---|---:|---|---|---:|
| 45 | Bahamut | **29** | the NW basin round the start, x 19-78, y 8-125 | `:61-89` | 0 |
| 46 | Vepar | **45** | the NW basin and the north band, x 17-141, y 9-126 | `:90-134` | 0 |
| 47 | Valkyrie | **43** | the north band to the NE, x 33-240, y 9-118 | `:208-250` | 0 |
| 51 | Great Bahamut | **65** | the NE corner and east column (most), the trench, a few in the west column; x 19-240, y 9-187 | `:60`, `:251-309`, scattered to `:389` | 21 |
| 52 | Silver Valkyrie | **85** | the trench (most), the east column, the SW lagoon; x 22-235, y 19-232 | `:171-207`, `:310-383` (interleaved) | 45 |
| 48 | Lizard King | **65** | two packs: the SE column (35: x 137-236, y 52-206) and the SW lagoon (30: x 15-94, y 176-237) | `:59`, `:135-170`, `:316-345`, `:390-396` | 21 |
| 49 | **Hydra** | **4** | **two pairs**: SE 211,192 and 229,203 (`:329-330`, ids 370-371); SW lagoon 24,227 and 19,230 (`:296-297`, ids 337-338) | | 2 |
| 50 | Sea Worm | **0** | defined (`:566-595`), never spawned | | |
| | **total** | **336** | | | **89** |

(Exact spot lists by breed: `a/spawns_rows.txt`; the 4x4 picture with breed letters:
`a/map_out.txt`.) The level climbs with the walk: the basin is levels 43-46 (Bahamut, Vepar,
Valkyrie, 120 spots near the arrival and the north band); everything past the NE corner, the
trench and the lagoons is 66-74. **There is no middle step**: from Valkyrie 46 the next breed is
the Great Bahamut at 66.

**Flags (not choices):**
- **WZO** (official 2005 `MonsterSetBase.txt:2807-3146`): **337 single rows (arrange type 2),
  the same table**. OM moved three of them off blocked tiles (Valkyrie WZO 218,47 -> OM 222,48;
  Great Bahamut 251,51 -> 240,51; Silver Valkyrie 163,138 -> 167,140; the WZO tiles are value 4 in
  every grid) and dropped one more blocked spot (Great Bahamut 7,52). All four Hydras are in WZO at
  OM's tiles. WZ's type-2 rows scatter each monster to a random open, non-safe tile within ±3 on
  respawn (`WZ MonsterSetBase.cpp:133-152`; lost-tower-port §4.1). MU2_BGFX already keeps OM's
  placement with a respawn tile drawn anew from the nest (webzen-audit.md, #13).
- **WZD** (repack, `MonsterSetBase.txt:100-109, 2831-3163`): the same 333 single rows minus all
  four Hydras, plus ten "spot" rows adding 12 Bahamut, 12 Lizard Kings, 16 Great Bahamut,
  **12 Sea Worms** (SW lagoon) and **one Hydra at 24,214**. Not trusted (its Hydra row is also
  re-statted, §4.2).
- **OM's 075 grid** cannot hold its own table: 89 spots on blocked tiles (all in the closed belt,
  column above) plus 189 on baked stand marks, so OM would throw on 278 rows
  (`OMGL/NPC/NonPlayerCharacter.cs:81-96, 225-237` needs `WalkMap`, which is bytes 0/1 only). On
  the client grid all 336 place.
- **What 0.75's own server held is not on disk.** The 075 file's stand marks hint at its table: 189
  of OM's spots are marked, the 58 unmarked open spots and the 89 in the closed belt are not, and 7
  marks are on no OM spot (218,14; 184,21; 214,21; 62,45; 221,51; 231,51; 178,143). That reads as
  a smaller, NW-and-west-only Atlans in the dump, about 196 monsters, with no Great Bahamut,
  Silver Valkyrie or Hydra in the east (`a/marks_out.txt`). The dump's date is unknown, so it is a
  flag only.

#### 4.2 Breeds (`OM/Version075/Maps/Atlans.cs:400-660`; WZO `Monster.txt:39-72`)

Columns as in lost-tower-port §4.2. Resistances are OM's `n/255` as n (P poison, I ice,
W lightning ["Water"], F fire). Every breed: `Attribute = 2`, `NumberOfMaximumItemDrops = 1`.

| # | name | lvl | HP | dmg | def | atk / def rate | move / atk / view | move / atk ms | respawn | skill (OM) | P I W F | OM lines |
|---:|---|---:|---:|---|---:|---|---|---|---:|---|---|---|
| 45 | Bahamut | 43 | 2 400 | 130-140 | 65 | 215 / 52 | **2** / 1 / **4** | 400 / 1600 | 8 s | -- | 1 1 1 1 | `:403-432` |
| 46 | Vepar | 45 | 2 800 | 135-145 | 70 | 225 / 58 | 3 / **4** / 7 | 400 / 1600 | 8 s | **Energy Ball (17)** | 2 2 3 2 | `:435-465` |
| 47 | Valkyrie | 46 | 3 200 | 140-150 | 75 | 230 / 64 | 3 / **5** / 7 | 400 / 1800 | 8 s | -- (shoots: arrows) | 6 2 2 2 | `:468-497` |
| 51 | Great Bahamut | 66 | 7 000 | 210-230 | 150 | 330 / 98 | 3 / 2 / 7 | 400 / 1600 | 15 s | -- | 6 6 6 6 | `:598-627` |
| 52 | Silver Valkyrie | 68 | 8 000 | 230-260 | 170 | 340 / 110 | 3 / **4** / 7 | 400 / 1600 | 15 s | **Ice (7)** | 7 7 7 7 | `:630-660` |
| 48 | Lizard King | 70 | 9 000 | 240-270 | 180 | 350 / 115 | 3 / **3** / 7 | 400 / 1600 | 15 s | **Lightning (3)** | 7 7 7 7 | `:500-530` |
| **49** | **Hydra** | **74** | **19 000** | 250-310 | 200 | 430 / 125 | 3 / **4** / 7 | 400 / **1400** | **150 s** | `MonsterSkill` (150) | 12 12 12 12 | `:533-563` |
| 50 | Sea Worm | 74 | 19 000 | 250-310 | 200 | 430 / 125 | 3 / 4 / 7 | 400 / 1400 | 150 s | -- | 12 12 12 12 | `:566-595` (unspawned) |

**WZO's official `Monster.txt` agrees with every number above, respawn included** (RegTime 8 / 8 /
8 / 15 / 15 / 15 / **150**; lines 39, 41, 43, 52, 53, 54, 72), and adds what OM does not carry:

| | Bahamut | Vepar | Valkyrie | Great Bahamut | Silver Valkyrie | Lizard King | Hydra |
|---|---|---|---|---|---|---|---|
| A.Type | 0 | **17** | 0 | 0 | **7** | **3** | **150** |
| ItemRate / MoneyRate / MaxItemLevel | 185 / 14 / 3 | 185 / 14 / 3 | 185 / 14 / 3 | 175 / 14 / 3 | 175 / 14 / 3 | 175 / 14 / 3 | 175 / 14 / 3 |

WZO's resistance columns (cold, poison, lightning, fire, `MonsterAttr.cpp:233-236`) are OM's P/I
swapped, as webzen-audit #1 found everywhere: the Valkyrie's 6 is **cold** in WebZen (it shrugs off
Ice 6 in 7, takes Poison), and OM's 6 is "poison". WZO has **no Sea Worm row** (index 50 is
absent); the Sea Worm is OM's copy of the Hydra's numbers and the repack's spot rows.

Where the sources differ:

| | OM 075 | WZO (official 2005) | WZD (repack 2012) | settled |
|---|---|---|---|---|
| Hydra | 74 / 19 000 / 250-310 / def 200, RegTime 150 | **same** | **108 / 89 000 / 1 050-1 510 / def 1 200, RegTime 1 800** | **OM = WZO**; the repack's Hydra is a custom boss |
| respawn | 8 / 15 / 150 s | RegTime **8 / 15 / 150** | RegTime 5 for all (Hydra 1 800) | WZO's RegTime with the user's "+1" rule (webzen-audit #9): **9 s / 16 s / Hydra 151 s** (open question 3, §8) |
| Hydra's skill | `MonsterSkill` (150): never created in 075's `SkillsInitializer`, so a melee-only Hydra in OM (lost-tower-port §4.2) | A.Type 150: **one blow in five is Flame of Evil (50) on everyone within 5 tiles**, approach range atk + 2 (`WZ gObjMonster.cpp:631-634, 1738-1752, 1849-1925`) | 150 | **WZ's**, as the Death Gorgon and Balrog (`src/sim/realm_tuning.h` `kBosses = {35, 38}`): add 49 |
| Silver Valkyrie | Ice | A.Type 7 | 7 | Ice: add 52 to `kChillers` (`realm_tuning.h:305`, the Ice Monster's chill) |

**Experience and Zen per kill** (`MU2_BGFX src/sim/rules.cpp:319, 332`, `(L+25)·L/3 ·1.25`; Zen =
exp + 7): Bahamut 1 218 / 1 225, Vepar 1 312, Valkyrie 1 360, Great Bahamut 2 502, Silver Valkyrie
2 635, Lizard King 2 770, **Hydra 3 052**.

**Bosses.** No boss flag anywhere. The **Hydra** is the boss by every number (74, 19 000 HP, resist
12, the only 150 s respawn, four on the map, A.Type 150) and by the client (below). The Lizard King
(70, 9 000) and Silver Valkyrie (body light) are the elite.

#### 4.3 What MuMain draws (`MM/Engine/Object/ZzzCharacter.cpp`; body `D/Monster/Monster{MODEL+1}.bmd`, `MM/Engine/Object/ZzzOpenData.cpp:2549-2561`; model numbers `MM/Core/Globals/_enum.h:4182-4188`)

| # | body | scale | arms | notes | line |
|---:|---|---:|---|---|---|
| 45 Bahamut | **Monster34** (MODEL_BAHAMUT) | **0.6** | -- | the fish: 4 bubbles + 4 `BITMAP_BLOOD+1` off bone 2 every frame-step | `:13812-13815`, `:2002-2013` |
| 51 Great Bahamut | Monster34 | 1.0 | -- | `c->Level = 1` (the bright variant) | `:13775-13780` |
| 46 Vepar | **Monster35** | 1.0 | -- | Energy Ball blow: `SOUND_EVIL`, two `BITMAP_BLUR+1` joints from each hand bone to the target; ATTACK1/2 at play speed 0.5; head bone 20 ("인어", mermaid) | `:13807-13811`, `:2183-2200`, `ZzzOpenData.cpp:3644-3651` |
| 47 Valkyrie | **Monster36** | 1.1 | **Bluewing Crossbow** (4,13 -> `CrossBow06`) | BlendMesh 0, light 1; shoots arrows like the Hunter (`:4831-4834, 4852-4855`) | `:13799-13806` |
| 52 Silver Valkyrie | Monster36 | **1.4** | Bluewing Crossbow | the gold/silver body-light pass (`:8716`) | `:13769-13774` |
| 48 Lizard King | **Monster37** (MODEL_LIZARD) | **1.4** | **Staff of Resurrection** (5,6 -> `Staff07`) | Lightning blow: six `BITMAP_JOINT_THUNDER` from both hand bones to the target | `:13793-13798`, `:2330-2342` |
| 49 Hydra | **Monster38** | 1.0 | -- | BlendMesh 5, light 0; ATTACK1/2 at 0.15, DIE 0.2; every 5th attack frame a `BITMAP_BOSS_LASER+1` from bone 63; its `AT_SKILL_BOSS` blow a ring of nine `BITMAP_BOSS_LASER` beams, blue-white | `:13786-13792`, `:1905-1928`, `ZzzOpenData.cpp:3671-3677` |
| 50 Sea Worm | Monster39 | 1.8 | -- | not spawned | `:13781-13785` |

`Monster34-39.bmd` are all in `MMD/Monster/`. Sounds: `mBahamut1`, `mBepar1/2`, `mValkyrie1`,
`mValkyrieDie`, `mBaliAttack2`, `mLizardKing1/2`, `mGorgonDie`, `mHydra1`, `mHydraAttack1`
(`ZzzOpenData.cpp:3639-3677`). The golden pair (0.97d Eldorado, not 0.75): Golden Lizard King (80)
on Monster37 at 1.4 with the **Chaos Lightning Staff** at excellent 63, Golden Vepar (81) on
Monster35 (`:13532-13543`).

#### 4.4 What MU2_BGFX has

`sqlite3 -readonly source/mu.db`: `monster_kinds` holds 0-41, 84-89, 132, 150; **none of 45-52**.
`monster_spawns` maps 0-4 and 11 (`max(id)` = 929); `gates` rows 17, 22, 27, 42 -- **no 49**.
`source/monsters/` has no Bahamut, Vepar, Valkyrie, Lizard King or Hydra body. Arms:
`CrossBow06` (Bluewing) is cooked; **`Staff07` (Staff of Resurrection, the Lizard King's) is not**.
`attack_skill` 17 (Energy Ball) is in mu.db only for the unspawned Yeti, so the Vepar's beams are a
new showing; 3 (Lightning) has the Thunder Lich's; 7 (Ice) the Ice Monster's chill; 50 the bosses'
Flame of Evil (`src/game/play.cpp:1077-1180`).

Rows to add, in lost-tower-port §4.5's form:

```sql
insert into monster_kinds (number, name, level, health, minimum_damage, maximum_damage, defense,
  move_range, attack_range, view_range, move_delay, attack_delay, attack_rate, defense_rate,
  respawn_seconds, attack_skill) values
 (45,'Bahamut',        43, 2400,130,140, 65,2,1,4,400,1600,215, 52,  8,NULL),
 (46,'Vepar',          45, 2800,135,145, 70,3,4,7,400,1600,225, 58,  8,17),
 (47,'Valkyrie',       46, 3200,140,150, 75,3,5,7,400,1800,230, 64,  8,NULL),
 (48,'Lizard King',    70, 9000,240,270,180,3,3,7,400,1600,350,115, 15,3),
 (49,'Hydra',          74,19000,250,310,200,3,4,7,400,1400,430,125,150,50),
 (51,'Great Bahamut',  66, 7000,210,230,150,3,2,7,400,1600,330, 98, 15,NULL),
 (52,'Silver Valkyrie',68, 8000,230,260,170,3,4,7,400,1600,340,110, 15,7);
-- 50 Sea Worm: leave out (never spawned in OM or WZO).
-- kDropRates (realm_tuning.h:369): {45,14,3,8},{46,14,3,8},{47,14,3,8},{48,14,3,15},{49,14,3,150},
--   {51,14,3,15},{52,14,3,15}  -- WZO's MoneyRate/MaxItemLevel/RegTime; respawn = RegTime + 1.
-- kResistances {number, ice, poison} (WebZen's order, realm_tuning.h:332-336):
--   {45,1,1},{46,2,2},{47,6,2},{48,7,7},{49,12,12},{51,6,6},{52,7,7}
-- kBosses += 49 (Hydra); kChillers += 52 (Silver Valkyrie).
-- monster_spawns: 336 one-tile rows on map 7 (Atlans.cs:58-396); gates: (49, 7, 15,11,27,23, 1).
```

### 5. Drops

**Nothing Atlans-specific in 0.75.** `Atlans.cs` overrides no `InitializeDropItemGroups`, so the
map has the default groups (`OM/GameConfigurationInitializerBase.cs:176-190`: money 0.5, random
item 0.3; plus the jewel group, lost-tower-port §5); `NumberOfMaximumItemDrops = 1` on every breed.

**What the levels reach.** MU2_BGFX's `Realm::leave` (`src/sim/realm_items.cpp:863ff`) takes WZ's
window: drop level DL in [L-15, L] at `+(L-DL)/3`, a row whose plus would pass MaxItemLevel left
out (`:879-890`). WZO gives **MaxItemLevel 3 for all seven breeds**, so the window is **[L-11, L]**,
+0..+3. With OM 075's item rows (`a/items075.txt`, from `OM/Version075/Items/*.cs`):

| killer | L | DL window | first seen in our worlds (DL > 66, the Balrog's top) |
|---|---:|---|---|
| Bahamut | 43 | 32-43 | -- (Dungeon/Lost Tower range) |
| Vepar | 45 | 34-45 | -- |
| Valkyrie | 46 | 35-46 | -- |
| Great Bahamut | 66 | 55-66 | -- (= the Balrog's: Bill of Balrog 63, Crystal Morning Star 66) |
| Silver Valkyrie | 68 | 57-68 | **Bluewing Crossbow (4,13; 68)** at +0 |
| Lizard King | 70 | 59-70 | + **Staff of Resurrection (5,6; 70)** |
| **Hydra** | 74 | 63-74 | + **Crystal Sword (2,5; 72)**, **Aquagold Crossbow (4,14; 72)**, **Scroll of Aqua Beam (15,11; 74)** |

(OM lines: `Weapons.cs:121, 148, 149, 158`, `Scrolls.cs:44`.) The three Chaos weapons (75) are
`dropsFromMonsters: false` (`Weapons.cs:122, 141, 159`). 0.75's armour stops at the Dragon set
(Dragon Armor 59, Dragon Shield 60, `Armors.cs:49, 71`), so **Atlans adds no armour**. So in 0.75
the **Scroll of Aqua Beam drops only from the Hydra** (four on the map, 151 s), and the Staff of
Resurrection only from the Lizard King and the Hydra.

In MU2_BGFX today: Bluewing Crossbow (`source/items/weapons/CrossBow06.json`, drop 68) and the
Scroll of Aqua Beam (`source/items/misc/Book12.json`, drop 74) are cooked, but **no monster
reaches 68**, so they drop nowhere yet. **Not cooked: `Staff07` (Staff of Resurrection), `Mace06`
(Crystal Sword), `CrossBow07` (Aquagold Crossbow)**, though mu.db's `items` names all three. Items
come into the game through `index.json`'s asset rows (`tools/cook.py:2031-2083`), so a drop with no
asset is not in the pool.

**Jewels.** Bless (25) and Soul (30) reach every breed. **Chaos stops at 66** (`OM/Version075/Items/Jewels.cs:86-87`
`MaximumDropLevel = 66`; WZ "13-66", webzen-audit): Bahamut, Vepar, Valkyrie and Great Bahamut
drop it; the Silver Valkyrie, Lizard King and Hydra do not.

**Not 0.75** (each later, none in OM 075):
- **Gold Medal** (14,11 level 6) on maps 4, 7 and 8 while the medal event runs (`WZ gObjMonster.cpp:5233-5245`).
- **Golden Lizard King (80) + ten Golden Vepars (81)**, the Eldorado event, placed at a random
  free tile in 50-200 of Atlans every 2 h (`WZ EledoradoEvent.cpp:436-460`); the King drops the
  **Box of Kundun +4** (`docs/kundun-box.md:39`). WZO `Monster.txt:276-277`: King 83 / 25 000 /
  310-360 / def 240 / A.Type 3, Vepar 61 / 6 300 / 190-200 / A.Type 17. kundun-box.md §6 left
  "+4 Lizard King + Vepars | Atlans -- we have none | re-home"; Atlans is the home.
- **Class quests** (0.97+): the Scroll of Emperor from monster levels 45-60 and the Broken Sword /
  Tear of Elf / Soul of Wizard from 62-76 (lost-tower-port §5) -- Atlans's two bands exactly.

### 6. Rules of the place

- **Entry**: on foot only, from **Noria gate 45** (242,240-245,243) at **level 60**, to exit 46
  beside the safe basin; out by gate 47 (9,9-11,12, level 60) to Noria exit 48 (§2). 0.75 has no
  warp row (§2.3).
- **Safe zone**: the NW basin only (§3). OM spawns nothing on a safe tile (`OMGL/GameMapTerrain.cs:150-167`); WZ
  likewise (`MonsterSetBase.cpp:116-120, 142-145`). Baz (240) stands in it at 23,17.
- **Death -> the basin.** OM: the spawn gate 49 of the map's own `SafezoneMap`
  (`OM/BaseMapInitializer.cs:91`; `OMGL/Player.cs:1559-1562`). WZ: the death branch's final `else`
  calls `GetMapPos(MapNumber)` (`WZ user.cpp:22360-22363`), which keeps map 7 (`MapClass.cpp:335`
  `if( Map > 4 && Map != 7 ) Map = 0;`) and picks a random open tile of `gRegenRect[7] =
  14,11-27,23` (`:200`). **Agreed.**
- **Town Portal -> the basin.** WZ `protocol.cpp:17938-17941` `MapNumber == 7 -> gObjMoveGate(49)`;
  OM's scroll goes to the `SafezoneMap`'s spawn gate; the client allows the scroll in Atlans
  (`MM/World/MapInfra/PortalMgr.cpp:52`).
- **Login**: WZ has no Atlans rule, so a saved character comes back where he stood (on a blocked
  tile, `GetMapPos`; `WZ user.cpp:3351-3356`). Single player: land him in the basin, as the Lost
  Tower.
- **No Horn of Uniria (and no Dinorant).** WZ refuses the move into map 7 with Uniria (13,2) in
  the helper slot (message 702), and with the Dinorant (13,3) under `NEW_SKILL_FORSKYLAND`
  (0.97+, message 1604): `gObjMovePlayer` `WZ user.cpp:26244-26290`, `gObjMoveGate` `:27147-27190`,
  `CMoveCommand::CheckEquipmentToMove` `MoveCommand.cpp:299-317`. It also refuses **equipping**
  either while in Atlans (`gObjIsItemPut` `user.cpp:15986-16001`). The client greys both horns
  out in Atlans (`MM/UI/NewUI/Inventory/NewUIMyInventory.cpp:347-350`). **OM has no such rule**: its
  Uniria even carries an underwater speed (`OM/Version075/Items/Pets.cs:36`). MU2_BGFX has both
  mounts (`src/sim/realm.h:231`, `docs/mount.md:74`: "Atlans refuses Uniria", noted, not built).
  **Settled: WZ/MuMain's** -- refuse the gate (and the warp) while a horn is worn, and refuse
  putting one on in Atlans.
- **Underwater movement.** OM: Atlans sets `IsUnderwater`, and off a safe tile the walker's speed
  is `MovementSpeedUnderwater`, not `MovementSpeed` (`OMGL/PlayerMovement.cs:261-273`). The
  running gear moves from the boots to the **gloves**: +5 gloves give the run underwater, +5 boots
  do not (`OM/Persistence/Initialization/Updates/AddMovementSpeedAttributesPlugInBase.cs:167-178,
  204-210`; `MovementSpeedConstants.cs:15, 20`); wings and pets count in both
  (`:220-224`). The client is the same rule: off a safe tile in Atlans the "dash" animation keys
  on gloves +5 instead of boots +5 (`MM/Engine/Object/ZzzCharacter.cpp:473-480`). MU2_BGFX's run
  is its own (the Mixamo run out of combat, memory "Running rule"), so this is a showing question
  only (open question 5, §8).
- **Swimming** (client only, off safe tiles): every hero is treated as flying for height
  (`ZzzCharacter.cpp:301-302`), walks and runs with `PLAYER_WALK_SWIM` / `PLAYER_RUN_SWIM`
  (`:624-630`), the weapon is slung on the back while swimming (`:15251-15254`), the step sound is
  `SOUND_HUMAN_WALK_SWIM` (`:5368-5371`), and bubbles rise from his head (`:5705-5712`, and
  `:3312-3318` about everyone). Part B's.
- **Sound and music**: `SOUND_WATER01` loops (`MM/Scenes/SceneManager.cpp:879-881`, stopped
  elsewhere `:952-955`), `MUSIC_ATLANS` plays for the whole map (`:1047-1052`). Our rule (memory
  "Music is rare"): the water bed, no track.
- **Ambient life**: fish (Object8 `Fish02-09`, `MM/World/MapInfra/MapManager.cpp:118-127`;
  `GOBoid.cpp` swims them), the "leaves" pass (`ZzzEffectFireLeave.cpp:299-301`), no grass
  (`ZzzLodTerrain.cpp:3619`), animated water tile 5 (`:1971, 2056`). Part B's.
- **Nothing else**: no traps, no weather, no PK rule, no map requirement item, exp multiplier 1.
  The Hydra's one-in-five area blow (§4.2) is the map's only special mechanic.

### 7. Where the sources disagree, settled or flagged

| point | OM 075 / 095d | WZO (official 2004-05) | WZ C++ | WZD (repack) | S6 (OM / MuMain) | settled |
|---|---|---|---|---|---|---|
| in 0.75 | yes | -- | -- | -- | yes | **yes** |
| grid | 075 file closes 6 584 tiles; 278 of its own spots fail | open, = client but 11 tiles | -- | = WZO | open | **client grid** (§3) |
| spawns | 336 one-tile rows | 337 rows, 4 moved/dropped by OM | type-2 scatter ±3 | 333 + spot rows (Sea Worms, 1 Hydra) | = 075 | **OM 075's 336** (the user's) |
| NPC | Baz 23,17 | none | -- | Baz 14,22 + Amy 16,24 | + Marlon 17,35 | **OM's Baz** (open question 4, §8) |
| stats | as §4.2 | **identical** | -- | Hydra 108 / 89 000 | = 075 | **OM = WZO** |
| respawn | 8 / 15 / 150 s | RegTime 8 / 15 / 150 | RegTime + 1 | 5 / Hydra 1 800 | = 075 | WZO + 1: **9 / 16 / 151 s** (open question 3, §8) |
| Hydra skill | 150 not created: melee | A.Type 150 | 1 in 5 Flame of Evil within 5 | 150 | client draws the laser ring | **WZ's** |
| gate levels | 60 / 60 | 60 / 60 | from Gate.txt | 60 / 60 | 60 / 60 (+ Tarkan 130, Elbeland 10) | **60** |
| warp list | none | Atlans 1/2/3 at 70, 4 000-5 000 zen | -- | -- | 70 / 80 / 90 | **none in 0.75** (open question 1, §8) |
| mounts | allowed | -- | Uniria refused (Dinorant 0.97+) | -- | client refuses both | **refuse** (§6) |
| death / scroll | spawn gate 49 | -- | `gRegenRect[7]`, gate 49 | -- | scroll allowed | **agreed: the basin** |

### 8. Open questions

1. **Warp rows.** OM's 0.75 has none for Atlans; 0.98n's movereq has Atlans 1/2/3 at level 70.
   Our M window lists worlds by name and opens rows on arrival. Recommendation: **one row,
   "Atlans" to gate 49, opened on first arrival on foot**, and Atlans 2/3 rows (75, 76) opened
   when he first stands in their zones -- the same per-zone rule as the Lost Tower's floors
   (lost-tower-port Decision 1). Or keep 0.75's walk only.
2. **The level gap.** The map runs 43-46 near the door and 66-74 everywhere past the NE corner,
   with nothing between; a level-60 hero (the gate's level) meets Great Bahamuts six above him
   300 steps in. That is MU's own shape (OM = WZO). Recommendation: keep it.
3. **Respawn.** WZO's RegTime is OM's (8 / 15 / 150); by the user's "+1" rule: 9 / 16 / 151 s.
   Recommendation: **9 / 16 / 151**, and see side finding 2 (the Lost Tower's 6 s / 11 s came from
   the repack's 5 / 10, not WebZen's 10 / 150).
4. **Baz in the basin.** OM 075 puts the vault keeper at 23,17; WZO (official) has no Atlans NPC;
   the repack adds Amy. Recommendation: **keep Baz** (OM, the user's spawn source), as the Lost
   Tower's hall; Amy is the repack's. No merchant: potions mean a walk to Noria.
5. **Underwater speed and the swim.** MU slows nothing outright, but moves the run from +5 boots
   to +5 gloves and swaps the walk for a swim. Ours has its own run. Recommendation: **the swim
   clips and the slung weapon as showing only; no speed rule**, unless the user wants the gloves
   swap.
6. **Hydra placement.** Four Hydras on single tiles, two in the SE (211,192; 229,203) and two in
   the SW lagoon (24,227; 19,230), each pair 5-20 tiles apart. The Hydra has MoveRange 3 in every
   source, yet MU draws a rooted multi-headed body. Recommendation: let it turn but not walk
   (move range 0 in our row) -- **ask**.
7. **The Hydra's area blow damage**, as the Balrog's (lost-tower-port open question 4): WZ's
   magic attack hits at the monster's own band (250-310), which is what our `kBosses` does today.
8. **The Golden Lizard King / Box of Kundun +4** (0.97d, not 0.75). Atlans is the event's own map,
   and the kundun-box research left it homeless. Out of scope for the port; flag for the
   Kundun-box work.
9. **What 0.75's own server spawned** (§4.1 flag): the 075 dump suggests a smaller Atlans of ~196
   monsters with the east and trench closed. Unprovable; the user has chosen OM's table.

---

### Side findings (outside Atlans)

1. **WebZen's official data is on disk now** (WZO, above). It confirms the repack raised more than
   was thought, and also that some "repack" numbers in earlier ports are WebZen's own:
   - **Lost Tower gates are level 80 in the official 2004 `gate.txt`** (lines 58-76: gates 28, 30,
     32, 34, 36, 38, 40 all at 80; 43 at 15), and the warp levels 90/90/100/100/110/110/120 are in
     the official 0.98n `movereq(kor).txt:24-30`. lost-tower-port §2.2 and §2.3 called both "the
     repack's"; they are WebZen's 0.98n-0.99 values. OM's 40/50 may still be 0.75's -- the
     official files are 0.98n-0.99, not 0.75 -- but the stated reason is wrong.
   - WZO's Dungeon gate 5 is level **40** (`gate.txt:22`), where OM says 20.
2. **Lost Tower respawn.** WZO `Monster.txt:44-51, 71`: RegTime **10** for the seven breeds and
   **150** for the Balrog -- OM's own numbers. `src/sim/realm_tuning.h:373-379` gives them the
   repack's 5 and 10 (6 s, 11 s) under "WebZen's respawn"; WebZen's own +1 rule would give 11 s
   and 151 s. Lost-tower-port Decision 3 (the user kept "WebZen's" 11 s Balrog) was made on the
   repack's number. Worth one line to the user.
3. WZO's LT ItemRate is 200 (Death Gorgon 180, Balrog 160), MoneyRate 14, MaxItemLevel 3 --
   unchanged from what kDropRates carries.

---

## Part B: the data and the look

Research notes, 2026-10-03. Nothing in the repo was edited, nothing was cooked and no window was run. Atlans is MU server map **7** (`WD_7ATLANSE`, `MM/World/MapInfra/MapManager.h:16`). The client ships it as `D/World8` + `D/Object8`.

Paths:
- MM = `LEGACY/reference/MuMain/src/source/`
- D = `LEGACY/reference/MuMain/src/bin/Data/`
- OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`
- S = `.../scratchpad/atlans/b/`

Scratch outputs in S:
- `atlans/`: `pipeline/terrain.py D/World8 atlans S 8` (height/tiles/attributes/light.png plus atlans.json; no table rows for map 7 yet, so nothing is flagged hidden). `terrain.log` is the run.
- `obj/`: every Object8 .bmd (41 ObjectNN + Fish02-09) through MuExtract `export-obj` + `export-rig --actions=all`, a .log each. `objinfo.log` summarises (type, placements, tris, bones, bbox, sheets).
- `mon/`: Monster34-39 (export-obj + rig, actions 0-6) and Staff07.
- `tex/`: every World8/Object8 sheet decoded, plus the six monsters' and Staff07's.
- Sheets to look at: `sheet_textures.png` (alpha as magenta), `sheet_objects.png` and `sheet_fish.png` and `sheet_monsters.png` (flat-shaded renders, texel colour per triangle), `wt_frames.png` (every 4th caustic frame), `prev_l1_l2_water_attr_height_light.png` (layer 1 | layer 2 | caustic overlay | attributes | height | light), `minimap_light.png`, `att_client_075_diff.png` (client grid | OM 075 grid | difference).
- Scripts: `prev.py`, `objinfo.py`, `render.py`, `sheet.py`, `dec.py`. Logs: `stats.log`, `objinfo.log`.

### The one thing to know first

**Atlans is an open seabed with no sky, no torches and no water surface. "Underwater" is made by seven small things, not by a fog or a tint.**
- The clear and fog colour are MuMain's **default black** (`MM/Scenes/SceneManager.cpp:380-401`, no Atlans arm). There is no blue fog in this client. The blue is all in the painted TerrainLight: mean (28, 87, 110), walkable floor (34, 104, 139). It is the **bluest, least red** map in the game.
- What does the underwater read:
  1. **Animated caustics on the floor.** Wherever layer 2 is slot 5 (TileWater01, which World8 does not even ship), MU draws one of 32 grey caustic frames `Object8/wt00-31.jpg` **additively** instead, cycling at 25 fps (1.28 s loop), at a quarter of the usual tile scale (one 64-px frame per 4 m). 4089 tiles, **16.7% of the walkable floor**, in scattered pools (`ZzzLodTerrain.cpp:1971-1975, 2056-2060, 1697-1707`; `SceneManager.cpp:326-337`; `MapManager.cpp:118-142`).
  2. **Bubbles** (`BITMAP_BUBBLE` = `Object8/drop01.jpg`, `MapManager.cpp:41`) from 845 hidden floor vents, from every player's head for 1 s in every 10, from every dying character, from pets' breath and from the Bahamut fish.
  3. **Fish**: up to 40 small cichlid boids and 10 larger fish swimming along the floor; in the south half the 10 become glowing deep-sea fish.
  4. **Floating motes**: 80 of Lorencia's "leaves", the same drift, but a soft white dot drawn additively.
  5. **Swimming**: outside the safe zone every player swims (`PLAYER_WALK_SWIM`/`RUN_SWIM`), idles on `PLAYER_STOP_FLY` (treading water), slings his weapons, and steps to `pSwim.wav`.
  6. **Faint shadows**: every body and pet shadow is drawn at **20% black** here, against solid black everywhere else.
  7. **Swaying weed and coral, a giant octopus, shipwrecks, 125 additive light shafts, pulsing teal glyph stones and floating magic circles.**
- The sounds: `aWater.wav` loops always; `atlans.mp3` plays always.

**The client grid and OM's 0.75 grid disagree badly here, unlike the Lost Tower.** OM `Resources/075_Terrain8.att` agrees with the client on only 88.7% of walkability. It **closes a 6584-tile band** (x 40-240, y 54-211): the long middle ridge path and the east corridor. Yet OM's own 0.75 spawns put **89 of 337 monsters** (21 Lizard Kings, 21 Great Bahamuts, 45 Silver Valkyries, 2 Hydras) on tiles that grid closes. OM's Season `Terrain8.att` is 99.84% the client's (102 tile differences). So the 075 file looks wrong for Atlans, not the client. Part A's call, but the client grid is the only one that fits the spawns and the walls. 0.75 also has a 7×12 safe pocket at (75-81, 146-157), sealed in rock; the client has none.

---

### 1. Raw data inventory

#### 1.1 World8 (`D/World8`, 21 files)

| file | bytes | loaded? | what |
|---|---:|---|---|
| EncTerrain8.map | 196610 | yes | 3 planes: layer1, layer2, alpha |
| EncTerrain8.att | 131076 | yes | 16-bit attributes. **No sanity tile for Atlans** (`ZzzLodTerrain.cpp:200-219` checks maps 0-4 only) |
| EncTerrain8.obj | 156454 | yes | **5215 placements, 40 kinds** (type 21 unplaced; types 0-40 otherwise) |
| TerrainHeight.OZB | 66620 | yes | read at 1080 |
| TerrainLight.OZJ | 65471 | yes | 256² baked light, teal-blue |
| Tile{Grass01,Grass02,Ground01,Ground03,Rock01,Rock02,Rock03,Rock04,Wood01}.OZJ | 30-123 KB | yes (`MapManager.cpp:1365-1420`) | 9 sheets. **No TileGround02, no TileWater01, no TileGrass .OZT**, so MU grows no grass. MU also turns grass off here explicitly (`ZzzLodTerrain.cpp:3619`) |
| leaf01.OZJ / leaf02.OZJ | 4501 | yes (`MapManager.cpp:1475-1480`) | 16² soft white dot: the motes (BITMAP_LEAF1) |
| Terrain.att / Terrain.map / Terrain8.att | | no | plain copies |
| mini_map.OZT, Minimap.bmd | | Season UI | 1024² minimap, oriented differently from the grid |

terrain.py (`S/terrain.log`, `stats.log`):

```
height     0.00 to 3.83 m; floor 2.235 m (byte 149), 5/50/95 pct of walkable 2.235/2.235/2.46; ridges up to 3.83
tiles      8 slots in use (0,2,4,5,7,8,9,10); layer1 is only 0 (42.5%), 10 (55.0%), 9 (2.5%) and one tile of 4
walkable   35.5%, 0.6% safe (421 tiles, x 10-34 y 7-28, the north-west corner)
light      mean [28, 87, 110]; walkable [34, 104, 139]; walkable luminance p10/50/90 52 / 86 / 118
objects    5215 placed, 40 kinds; no model missing
```

**Attributes:** 0 open 22958, 4 NoMove 42156, 1 Safe 318, 5 Safe+NoMove 102, 3 and 6 one tile each. **There is no NoGround on the whole map.** Every tile is floor or rock. Height runs inverse to walkability (corr -0.36): the NoMove tiles are the raised rock ridges, at most 1.6 m over the floor. A low relief. It is not a cliff map.

**Map shape** (`prev_l1_...png`): three long walkable bands run diagonally south-west to north-east between mossy rock ridges. Below, "north" means low y.
- the safe corner at (10-34, 7-28), with the Noria gate 47 at (9-11, 9-12)
- a northern loop
- the long middle ridge path (the one 0.75 closes)
- a southern loop ending in a pale-teal flagstone court at x 12-62, y 197-243 (slot 9, TileRock03; where layer 1 is slot 9)

**Tile sheets** (looked at in `sheet_textures.png`):

| slot | name | px | actually | layer1 % | layer2 tiles (alpha > 0) |
|---|---|---:|---|---:|---:|
| 0 | TileGrass01 | 256 | **pale cream sand**, fine ripples: the seabed floor | 42.5 | 3 |
| 1 | TileGrass02 | 256 | dark teal-blue rock/coral crust. **Unused** | 0 | 0 |
| 2 | TileGround01 | 128 | tan-brown pebbly earth | 0 | 607 |
| 4 | TileGround03 | 128 | near-black with olive-gold flecks (seaweed bed) | 1 tile | 566 |
| **5** | **TileWater01** | — | **not shipped. Replaced by the caustic frames (§2)** | 0 | **4089** |
| 6 | TileWood01 | 128 | brown gravel. **Unused** | 0 | 0 |
| 7 | TileRock01 | 128 | pink-tan rock | 0 | 222 |
| 8 | TileRock02 | 256 | lilac-blue rough stone | 0 | 6 |
| 9 | TileRock03 | 256 | pale grey-teal flagstone (the south-west court) | 2.5 | 79 |
| 10 | TileRock04 | 256 | **dark mossy green rock**: every ridge and wall | 55.0 | 12746 |

- Slot 10 is laid over the sand's edges (12746 overlay tiles), so the ridges blur into the floor.
- Slot 5's 4089 overlays are the caustic pools. They sit almost wholly on the floor (3876 walkable), along the bands' edges and in clusters (the blue dots in the preview).

**Light map.**
- Teal-cyan and blue throughout. Red is nearly absent (mean R 28).
- The ridges are painted dark (the black serpentine bands in `minimap_light.png`).
- The north-west safe corner is the brightest, a near-white pool.
- There are bright cyan speckles along the north edge.
- A few warm pink flecks sit in the middle band.
- No lamp pools: nothing in MuMain lights Atlans dynamically (§2).

#### 1.2 Object8 (`D/Object8`, 113 files)

The folder holds:
- 41 `ObjectNN.bmd` and 8 `FishNN.bmd`
- 64 sheets: bfish01-10 / 072 / 073, bons, bu0-04 (.OZT, alpha), drop01, ioi01, light01, sea01, star01, ston00, ston033, wood00-05, wood052, wooda, and **wt00-wt31** (32 caustic frames)

Every sheet a model names is on disk. Type = file number - 1 (`MapManager.cpp:1121-1128`). Sizes are the OBJ bbox in cm, x × y(up) × z. "z" is placement height minus the ground under it.

| type | model | placed | tris | rig (keys) | sheets | read as / MuMain |
|---:|---|---:|---:|---|---|---|
| 0-2 | Object01-03 | 17 / 6 / 7 | 1167 / 384 / 362 | 1 | sea01 | large grey-blue **reef rocks**, 3-4.6 m long, all on NoMove |
| 3, 4 | Object04, 05 | 72, 70 | 40, 110 | 1 | star01 | **starfish** scattered flat on the sand (13 cm high) |
| 5 | Object06 | 421 | 30 | 1 | bu01 (alpha) | yellow branching **coral**, 2 × 1.4 m crossed cards |
| 6 | Object07 | 475 | 20 | 1 | bu02 (alpha) | pink **sea-fan coral** cards, 1.4 m |
| **7-10** | Object08-11 | 23 / 27 / 35 / 16 | 632-1050 | 34 / 9 / 8 / 9 bones, **30 / 60 / 30 / 30 keys** | bfish07 / 072 / 073 | the **giant octopus**, all on NoMove rock and sunk to -0.9 m: Object08 its head (2.5 m tall, the eye), Object09 a coiled tentacle ring (3.1 m), Object10/11 tentacle arches rising out of the rock. Animated, so the arms writhe |
| 11-15 | Object12-16 | 10 / 13 / 14 / 10 / 9 | 126-387 | 1 | wooda, wood01-04 | **shipwreck**: broken hulls, ribs and a whole boat (Object15), sunk 0.8-1.2 m |
| 16, 17 | Object17, 18 | 44, 86 | 65, 50 | 1 | wood01/02/a | plank litter |
| 18, 19 | Object19, 20 | 16, 16 | 86, 50 | 1 | wood04/01/03 | masts and a spar |
| 20 | Object21 | 126 | 266 | 1 | ston00 | tall dark-teal **rock pillar**, 4.6 × 5.8 m, mesh reaching 5.4 m **below** its origin (sunk spires) |
| 21 | Object22 | **0** | 2 | 1 | bu02 | unplaced |
| **22** | Object23 | **845** | 12 | 1 | wood00 | **hidden bubble vent** (49 cm box). HiddenMesh -2 + BITMAP_BUBBLE, §2 |
| **23** | Object24 | 10 | 10 | 1 | **wt00** | a flat 5.5 × 3.6 m sheet that draws the **animated caustic** (wt00 *is* BITMAP_WATER, `ZzzBMD.cpp:1322-1325`), additive and pulsing. Six round the safe zone's edge (20-33, 10-27), four at (224-229, 97-102) |
| **24, 25** | Object25, 26 | 246, **951** | 32, 96 | 28 / 84 bones, **31 keys** | bu03 (alpha) | **swaying kelp**: single blades, 2.4 m (25), and a 4.4 m bed (26, the most placed object) |
| 26 | Object27 | 233 | 84 | 1 | bu0 (alpha) | flat green **sea-lettuce/anemone** rosettes, 2.9 m |
| **27, 28** | Object28, 29 | 95, 40 | 408, 468 | 89 / 91 bones, **31 keys** | bu0 + bu03 + bu01 | **swaying weed-and-coral clumps**, 8 m long |
| 29, 30 | Object30, 31 | 101, 101 | 102 | 1 | ston033 | grey-and-white encrusted **boulders**, 0.46-2.26 scale |
| **31, 33** | Object32, 34 | 296, 328 | 12 | 5 bones, **15 keys** | bu04 (alpha) | small swaying **purple weed tufts** |
| **32** | Object33 | 105 | 92 | 1 | wood05 + **wood052** | 2.7 m dark-teal carved **standing stone with a glowing teal glyph**: BlendMesh 1 pulses, §2 |
| **34** | Object35 | 25 | 1728 | 1 | wood05 + **wood052** | a 5.7 m carved **arch/serpent ring**, the same glyph glow |
| 35-37 | Object36-38 | 57 / 46 / 8 | 284 / 240 / 486 | 1 / 1 / 9 bones (**60 keys**) | bons | bones, skulls, a skeleton (Object38 animated) |
| **38** | Object39 | 125 | 6 | 3 | **light01** | **light shafts**: three crossed 3.4 × 4.6 m planes of light01 (16 × 128 dark-blue gradient), BlendMesh 0 = whole mesh additive. 89% stand on walkable floor |
| **39** | Object40 | 4 | 7 | 1 | bfish072 | **hidden pose box** (CreateOperate + HiddenMesh -2), round the safe zone: (15.5, 23.5), (28.5, 8.5), (16.5, 24.5), (23.5, 26.5) |
| **40** | Object41 | 86 | 4 | 6 bones, **5 keys** | **ioi01** | 2.7 m cyan **magic circle**, additive, pulsing, floating 0-4.35 m (median 2.27 m) over ridges (92% on NoMove). One placement stores scale -0.28 |

- Placement counts in order: Object26 951, Object23 845 (hidden), Object07 475, Object06 421, Object34 328, Object32 296, Object25 246, Object27 233, Object21 126, Object39 125, Object33 105, Object30/31 101 each, Object28 95, Object18 86, Object41 86, Object04 72, Object05 70, Object36 57, Object37 46, Object17 44, Object29 40. Everything else is 35 or fewer.
- North half (y < 128) holds 3061 placements and the south 2154. The fish change at y 128 (§2).
- **Animated placed kinds:** 3 kelp/weed families (25/26 and 28/29 at 31 keys, 32/34 at 15), the octopus (08-11), Object38 and Object41. That is ~2000 rigged placements, against Lorencia's 20 rigged objects. Frame cost is a real question (see Part C).

**Fish** (`Object8/Fish02-09`; `MapManager.cpp:118-124` loads them as MODEL_FISH01+1..+8; all rigged, 8 keys, or 15 for 08/09):

| model | MODEL | tris | sheet | look |
|---|---|---:|---|---|
| Fish02, 03 | FISH01+1, +2 | 30 | bfish04, bfish05 | small blue-and-yellow cichlids, 35 cm: the **boids** |
| Fish04, 05 | +3, +4 | 38 | bfish01, bfish02 | yellow and orange **discus**, 44 cm |
| Fish06, 07 | +5, +6 | 68 | bfish09, bfish08 | **trout**, 72-95 cm |
| Fish08 | +7 | 91 | bfish10 | black deep-sea fish with fins of light, **additive pulse** |
| Fish09 | +8 | 180 | bfish06 | dark blue jelly/ray, **additive pulse** |

World80/81/82 + Object80/81/82 **are not Atlans.** Folder = map + 1 (`MapManager.cpp:1226`):
- World80 is `WD_79UNITEDMARKETPLACE` (Loren Market: Object80 holds MarketBuild/MarketRoof/Gidoong sheets)
- World81 and World82 are `WD_80KARUTAN1`/`WD_81KARUTAN2` (`MapManager.h:67-69`)

Ignore them. Doppelganger 3 (`WD_67DOPPLEGANGER3`) borrows Atlans's fish, caustics, bubbles and swimming wholesale (every check below pairs them).

---

### 2. MuMain's special cases for WD_7ATLANSE

A grep for `WD_7ATLANSE` gives 40 hits, plus `WorldActive == 7` twice (`ZzzObject.cpp:9483, 10562`) and `CreateAtlanseLeaf`/`CreateAtlanseFish`. Every one is below.

**Frame, light, fog**
- **Clear and fog: black**, the default (`SceneManager.cpp:380-401`). Fog is linear from ViewFar×1.0 to ×1.25 in the clear colour (`Render/Textures/ZzzOpenglUtil.cpp:752-788`). No sky, no weather, no camera arm, no wind (`SceneManager.cpp:932-935` stops wind off the four wind worlds).
- **No object adds light:**
  - no CreateFire
  - no AddTerrainLight in the MoveObject arm (`ZzzObject.cpp:4055-4084`)
  - nothing in RenderObjectVisual
  - The baked TerrainLight is the whole lighting.
- **Grass is switched off by name** (`ZzzLodTerrain.cpp:3619`), on top of there being no .OZT.

**The caustics, the signature effect**
- `MapManager.cpp:118-142` (Atlans arm): `Object8/wt00-31.jpg` load into `BITMAP_WATER+0..31` (GL_LINEAR, repeat). Their file names are stored, so a model mesh naming `wt00.jpg` resolves to BITMAP_WATER. Every other map loads them too (`:1010-1022`), for Hellas and Battle Castle.
- `SceneManager.cpp:326-337`: `WaterTextureNumber` steps 0→31 once per reference frame (25 fps), so the loop is 1.28 s.
- Terrain (`ZzzLodTerrain.cpp:2052-2060` legacy, `:1971-1975` VBO port):
  - On a tile whose layer 2 is slot 5, MU skips the normal overlay and draws `BITMAP_WATER + WaterTextureNumber` with `RenderFaceBlend`.
  - That is `EnableAlphaBlend` (MU's additive one-plus-one), weighted by the tile's corner alpha (`:1697-1707`).
  - It uses `FaceTexture(..., Scale = true)`, so UV = 16/64 per tile: one 64-px frame spans 4 tiles (`:1715-1719`).
  - So: grey cellular caustics, light only, flickering through 32 frames over the overlay's soft alpha mask, in pools on the sand.
  - The base layer is never slot 5 here (0 tiles), so the missing TileWater01 sheet is never sampled.
- Objects: **Object24** (type 23), whose mesh wears wt00, draws the same animation. MoveObject sets `BlendMesh = 0`, `BlendMeshLight = sin(WT·0.002)·0.3+0.5` (`ZzzObject.cpp:4067-4071`). An additive caustic sheet, breathing 0.2-0.8 on a 3.1 s period.

**Bubbles** (`BITMAP_BUBBLE` ← `Object8/drop01.jpg`, `MapManager.cpp:41`; a 3×3 frame sheet of white dots on black, `ZzzEffectParticle.cpp:8949-8951`)
- Particle (`ZzzEffectParticle.cpp:195-201, 4144-4152`): LifeTime 30-39 frames, scale 0.12-0.27. SubType 0 rises `(10..29)·2.5·scale` u/frame with a ±(25·scale) jitter in x/y. So a bubble climbs about 1-5 m in 1.2-1.6 s, wobbling, and pops at the end of its life.
- Sources:
  1. **845 hidden vents**, type 22 (Object23) (`ZzzObject.cpp:4057-4065`): `Timer += 0.1` a frame and wraps at 10. While `Timer > 5`, one bubble a frame. So each vent streams 2 s on and 2 s off, 25 bubbles a second while on. The timers start in step, so they would pulse together unless seeded; nothing seeds them.
  2. **Every player's head**, for the first 1000 ms of every 10 s of WorldTime, one a frame at (0, 20, -10) off BoneHead (`ZzzCharacter.cpp:5703-5711`, MoveCharacterVisual, MODEL_PLAYER). The safe zone is not excluded.
  3. **Every dying character**: 4 a frame within ±64 × ±64 × 0-256 of the body, the whole death (`ZzzCharacter.cpp:3312-3321`, DeadCharacter).
  4. **The pet's breath** (Dark Horse/Fenrir slot, bone 27): bubbles here in place of smoke, 1 frame in 3 (`Engine/AI/GOBoid.cpp:436-449`).
  5. **Bahamut** (monster 45), always: 4 bubbles + 4 `BITMAP_BLOOD+1` a frame round bone 2 (`ZzzCharacter.cpp:2002-2012`).

**Motes** (`CreateAtlanseLeaf`, `Render/Effects/ZzzEffectFireLeave.cpp:299-319`)
- The same spawn box and drift as Lorencia's leaves (`:218-240`): ±8 m round the hero, 0.5-3.5 m up, blown along −x, reversed near the camera.
- The sprite is World8's leaf01, a soft white dot, and Atlans draws the pool **additive** (`:525-536`).
- 80 at once (`:429`). Enabled by `RequireLeavesEffect` (`Scenes/MainScene.cpp:83`).
- MU2_BGFX's `src/game/world/leaves.*` is this system already; Atlans needs only its sprite, its additive blend and no landing-darken.

**Fish** (`Engine/AI/GOBoid.cpp`)
- **Boids, 40 slots** (`MAX_BOIDS`, `Core/Globals/_define.h:108`):
  - `CreateAtlanseFish` (`:886-918`): only while the **hero stands at tile y < 128**. Fish02/03 at scale 0.8, Velocity 0.3 (90%) or 0.25, 1.5-3.5 m over the ground, within ±5 m of the hero. In the south half the slot dies.
  - Spawned only on open tiles (`:1309`).
  - They swim with a two-speed Timer: slow cruise with Gravity 15, then a dart at 32-63·v with Gravity 5 (`:1142-1160`).
  - They turn back on the safe zone (`:1459-1465`).
- **"Fishs", 10 slots** (`MAX_FISHS` `_define.h:109`; `MoveFishs` `:1660-1860`; Atlans arm `:1750-1772`):
  - scale 0.8-0.9, `bBillBoard`, lit by the terrain, faded in, spawned on open tiles within ±5 m
  - **North (hero y < 128):** Fish04-07 (discus, trout), Velocity 1/scale.
  - **South:** Fish08/09 at Velocity 0.5/scale, with `BlendMesh = 0` and `BlendMeshLight = sin(Timer)·0.4+0.5`, Timer += 0.1 a frame (`:1775-1779`). **Glowing deep-sea fish, pulsing on a 2.5 s period.**
  - They turn at NoMove/NoGround and die past 15 m.
  - **A MuMain bug, live:** `if (World != 7 || !InHellas() || World != 67)` is always true (`:1793`), so `Position[2] = RequestTerrainHeight`. The fish are **pinned to the sand** after their first frame, not at the 0.5-2 m their spawn sets (`:1771`). Flag: transcribe at swim height, or MU's literal floor-crawl.

**Object arms**
- **MoveObject** (`ZzzObject.cpp:4055-4084`):
  - **22 (Object23):** `HiddenMesh = -2`, the bubble vent above.
  - **23 (Object24):** `BlendMesh 0`, light `sin(WT·0.002)·0.3+0.5`, the caustic sheet.
  - **32, 34 (Object33, Object35):** `BlendMesh 1` (wood052, the teal glyph), light `sin(WT·0.004)·0.5+0.5`. The glyph breathes 0→1 on a 1.57 s period.
  - **38 (Object39):** `BlendMesh 0`, light 1 (a pulse is commented out). The light shafts are plain additive.
  - **40 (Object41):** `BlendMesh 0`, light `sin(WT·0.004)·0.3+0.5`, `Velocity = 0.05`. The magic circle pulses 0.2-0.8, and its 5-key action plays slowly (a turn, most likely; unverified).
- **CreateObject** (`:4769-4776`): **39 (Object40)** `CreateOperate` + `HiddenMesh -2`. Its MOVEMENT_OPERATE arm is `Pose = true`, with the hero turned to the box's angle (`Engine/Object/ZzzInterface.cpp:1736-1742`). Four lean boxes at the safe zone, MU's only seats here.
- There is no RenderObject or RenderObjectVisual arm. The kelp, weed and octopus animate by their own clips at CreateObject's default speed.

**Characters** (`Engine/Object/ZzzCharacter.cpp`; every arm is shared with Hellas and Doppelganger 3)
- **Idle:**
  - Off the safe zone, `Fly = true` (`:300-301`), so idle is `PLAYER_STOP_FLY`, or `PLAYER_STOP_FLY_CROSSBOW` with a crossbow (`:312-326`): treading water.
  - **Moving**: `PLAYER_RUN_SWIM` once `Run >= 40`, else `PLAYER_WALK_SWIM` (`:624-630`). A wing outranks both: a winged hero flies (`:617-623`).
  - On the safe zone: the normal stand and walk.
- **The run is earned by gloves, not boots.** Running starts with gloves +5 here and boots +5 elsewhere (`:473-481`). Dark Knights, Dark Lords and Rage Fighters always run.
- **Weapons slung while swimming** (`bBindBack`, `:15251-15254`).
- **Footsteps**: `SOUND_HUMAN_WALK_SWIM` = `pSwim.wav` off the safe zone (`:5368-5371`; `ZzzOpenData.cpp:4742`).
- **Shadows at 20%**: `glColor4f(0,0,0,0.2)` with alpha test, where every other map draws them opaque black.
  - body and parts: `ZzzObject.cpp:9481-9491, 10560-10570`
  - Dark Horse and Fenrir: `:798-805, 873-880`
  - Dark Spirit: `:1009-1016`
- **Mounts**: Horn of Uniria and Dinorant cannot be equipped in Atlans (`UI/NewUI/Inventory/NewUIMyInventory.cpp:347`). The move command refuses Atlans while riding one, "You cannot go to Atlans while riding a unicorn" (`ZzzInterface.cpp:4283-4329`, also `:2749`).
- `MODEL_BALI` dies 2.5× faster here (`:3222-3225`). Bali is not an Atlans breed, so this is a leftover.

**Ambient and music**
- `SOUND_WATER01` = `aWater.wav` (1.17 MB) loops always (`SceneManager.cpp:879-881`; stopped elsewhere `:952-955`; loaded `ZzzOpenData.cpp:4736`).
- **`MUSIC_ATLANS` = `data/music/atlans.mp3` (4.5 MB) plays always** (`SceneManager.cpp:1047-1052`; `Core/Globals/_enum.h:176`).

**Other**
- The town portal works (`World/MapInfra/PortalMgr.cpp:52`).
- The map-name card is `atlans.tga` (`UI/Legacy/UIMapName.cpp:55`).

---

### 3. The pipeline route, and what changes for World8

The chain is the Lost Tower's (`docs/lost-tower-port.md` B §3). From MU2_BGFX/, with `D=../LEGACY/reference/MuMain/src/bin/Data`:

```sh
# 0. tables first. Keys are map number - 1 = 7.
#    pipeline/terrain.py
#      HIDDEN_BY_MAP[7]     = {22, 39}        # 845 bubble vents (ZzzObject.cpp:4059), 4 pose boxes (:4772)
#      OPERABLE_BY_MAP[7]   = {39}            # Pose (ZzzInterface.cpp:1736-1742)
#      GRASS_BY_MAP[7]      = []              # no .OZT and grass off by name (ZzzLodTerrain.cpp:3619);
#                                             #   TileGrass01 is the sand
#      BLEND_MESH_BY_MAP[7] = {23: 0, 32: 1, 34: 1, 38: 0, 40: 0}   # MoveObject :4067-4083
#      WATER_FLOW_BY_MAP[7]: none -- slot 5 is never base layer here; the overlay is caustics, not a flow
#      LIGHT_DEPTH_BY_MAP / VOID_BY_MAP / LAVA_SPILL: none (no NoGround anywhere)
#    pipeline/index.py
#      OPERABLE_BY_MAP[7]   = {39}
#      GATE_BOXES_BY_MAP[7] = [(9, 9, 11, 12), (14, 12, 15, 13)]   # OM Version075/Gates.cs:149, 206 (enter 47, exit 46)
python3 pipeline/terrain.py $D/World8 atlans source/world 8
#    expect: 0.00-3.83 m, 8 of the slot table, 35.5% walkable, 0.6% safe, 5215 placed / 40 kinds, nothing missing
for n in TileGrass01 TileGround01 TileGround03 TileRock01 TileRock02 TileRock03 TileRock04; do
  python3 pipeline/decode_texture.py $D/World8/$n.OZJ source/textures/at_$(echo $n | tr A-Z a-z).png; done
#    (TileGrass02 and TileWood01 are unused; there is no TileWater01 to decode)
for i in $(seq -w 0 31); do python3 pipeline/decode_texture.py $D/Object8/wt$i.OZJ source/textures/at_wt$i.png; done   # the caustics
#    source/world/atlans/ground.json on Devias's/Noria's shape, sheets -> at_*
./tools/content.sh --world atlans && ./tools/sync.sh --world atlans --only-world && ./tools/sync.sh
./tools/cook.py --world atlans --only ground && ./tools/cook.py --world atlans --only tables
# objects: export per kind into source/world/atlans/, sheets at_-prefixed
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-obj $D/Object8/Object26.bmd source/world/atlans/Object26.obj
dotnet tools/MuExtract/bin/Debug/net10.0/muextract.dll export-rig $D/Object8/Object26.bmd source/world/atlans/Object26.rig.json --actions=all
#   rigs for 08-11, 25, 26, 28, 29, 32, 34, 38, 41 and the fish
./tools/asset.sh atlans/Object26 --build-only && python3 pipeline/index.py source workshop
./tools/cook.py --world atlans --only textures / meshes / placements
```

**Sheet prefix: `at_`.** Nothing in `source/textures` starts with `at_` today. The prefixes in use are dv, nr, lt, dn, bc, cs, npc. Collisions without one:
- `bons` (as in the Lost Tower; reuse `bons.png`/`dn_bons.png` if byte-identical)
- `light01` (Object8's is also MuMain's global `BITMAP_SHINES`, `MapManager.cpp:1024`)
- `sea01`, `wood01-05`, `wooda`
- all the Tile* names
- the `wt*` frames (Hellas and Battle Castle reuse them, but nothing in MU2_BGFX does yet)

**ground.json `sheet_materials` (proposed):**

| slot | sheet | material |
|---|---|---|
| 0 | TileGrass01 | sand (wet seabed: matte, no dust) |
| 2 | TileGround01 | soil |
| 4 | TileGround03 | soil (dark weed bed) |
| **5** | **caustics** | **not a sheet**: an additive 32-frame flipbook at 25 fps, 4 m a frame, weighted by the overlay alpha. Needs engine work (Part C). Nearest existing: `water_glow`/`water_sheen` in fs_ground, which are not this |
| 7 | TileRock01 | rock |
| 8 | TileRock02 | rock |
| 9 | TileRock03 | flagstone |
| 10 | TileRock04 | rock (mossy) |

What else differs from the worlds already built:
- **No void, no lava, no water channels, no grass.** The simplest ground of any map so far, plus caustics.
- **Hidden types need care:** 22 (bubble vents) must survive as markers for a bubble system, as the Lost Tower's fire vents did. 39 are the pose boxes.
- **cook.py tables:**
  - `AIRS["atlans"]` = Fish02/03, from Object8. They are boids, but only for y < 128.
  - `CRAWLS["atlans"]` = the MoveFishs set: Fish04-07 north, Fish08/09 south.
  - Both tables today take one model a world (`cook.py:202-214`). Atlans needs a list, and a split by the hero's y.
  - `GROUNDED_TYPES`: none.
  - The **sunk-crown test**: Object21's pillars and the octopus/wreck pieces stand up to 5.4 m and 0.8-2 m below their origin on purpose. Add Atlans to the skip, or check it doesn't flag them.
  - `FLAT_OVER_VOID`: no.
- **Rigged placements: ~2000** (kelp 1197, weed clumps 135, tufts 624, octopus 101, Object38 8, Object41 86). `src/game/world/sway.*` plays 20 in Lorencia today. Engine-lead question: instancing, or a shared clip phase per kind.
- **Additive kinds:**
  - Object24 (caustic sheet, animated texture)
  - Object33/35 (mesh 1 glyph, pulsing)
  - Object39 (whole mesh)
  - Object41 (whole mesh, pulsing)
  - Object06/07/25-29/32/34 are alpha-cut cards (the bu* .OZT), not additive.
- **Recipe priority by placement count:**
  1. Object26 (951, kelp bed, rigged)
  2. Object07 (475, pink coral)
  3. Object06 (421, yellow coral)
  4. Object34 / Object32 (328 / 296 tufts)
  5. Object25 (246 kelp)
  6. Object27 (233 sea-lettuce)
  7. Object21 (126 pillars)
  8. Object39 (125 light shafts)
  9. Object33 (105 glyph stones)
  10. Object30/31 (101+101 boulders)
  11. Object28 (95)
  12. Object18 (86 planks)
  13. Object41 (86 circles)
  14. Object04/05 (72/70 starfish)
  15. Object36/37 (bones)
  16. Object17
  17. Object29

  Then the octopus (Object08-11, 101 total, a landmark, so earlier), the wreck (Object12-16, 56), the masts, Object24 (10) and Object35 (25 arches). Object23 and Object40 need no mesh. Object22 is unplaced.

---

### 4. Monster assets

OM `Version075/Maps/Atlans.cs`:
- Spawns: `:53-288`. Breeds: `:399-662`. `AddUnderwaterMovementPowerUp` is at `:46` (Part A).
- Spawn counts (grep of NpcDictionary, comments excluded):

| # | breed | spawns |
|---:|---|---:|
| 52 | Silver Valkyrie | 85 |
| 51 | Great Bahamut | 66 |
| 48 | Lizard King | 65 |
| 46 | Vepar | 45 |
| 47 | Valkyrie | 43 |
| 45 | Bahamut | 29 |
| 49 | Hydra | 4 |
| 50 | Sea Worm | **0** (row only; the spawn at `:288` is commented out) |
| 240 | Baz the Vault Keeper ("Guard" in OM's comment) | 1 at (23, 17) |

The body file is `Monster{MONSTER_MODEL+1:02}.bmd` (`Core/Globals/_enum.h:4182-4188`). CreateMonster arms are `ZzzCharacter.cpp:13769-13816`. MuExtract output is in `S/mon/`. **None of the six bodies is in `source/monsters`.**

| # | breed | file | scale | arms / extras | tris, sheets | MU2_BGFX |
|---:|---|---|---:|---|---|---|
| 45 | Bahamut | **Monster34** | 0.6 | always 4 bubbles + 4 BLOOD+1 a frame at bone 2 (`:2002-2012`); spark + shiny at bone 9 (`:11239-11241`) | 396; fs00, fs01.tga, fs02 | **missing**. A spiny red-brown lionfish/dragonfish |
| 51 | Great Bahamut | Monster34 | 1.0 | `Level = 1` adds a SMOKE+1 trail, 1 frame in 4 (`:6169-6176`) | — | re-dress of Bahamut |
| 46 | Vepar | **Monster35** | 1.0 | attack speed 0.5 (`ZzzOpenData.cpp:3644-3651`); lightning, spark and shiny at bones 30/39 (`ZzzCharacter.cpp:11220-11227`); sEvil + energy ball (`:2183-2195`, `:5092`) | 562; hp00, hp01.tga | **missing**. A mermaid/merman |
| 47 | Valkyrie | **Monster36** | 1.1 | **Bluewing Crossbow** (CrossBow06, **cooked**), link bones 30/39 (`:11996-11998`); `BlendMesh 0` at light 1, so its body is drawn additive, a glowing figure (`:13799-13805`; `ZzzObject.cpp:1135-1140`); fires arrows (`ZzzCharacter.cpp:4831-4834`) | 655; gp01, gp02.tga | **missing**. A finned armoured sea-warrior |
| 52 | Silver Valkyrie | Monster36 | 1.4 | Bluewing Crossbow; the silver-body branch (`:8716`, with Balrog and Golden Budge) | — | re-dress of Valkyrie |
| 48 | Lizard King | **Monster37** | 1.4 | **Staff of Resurrection** = Staff07 (**not cooked**; 548 tris, wwa/red/wwe/wwe00), link bones 52/65; RenderEye 42/43 and sparks at bones 26/31/36/41 (`:11228-11238`); 6 JOINT_THUNDER from both hands on attack (`:2330-2343`) | 1058; fr00 | **missing**. A tusked lizard-man with a fishtail |
| 49 | Hydra | **Monster38** | 1.0 | `BlendMesh 5` (bbbb03) ramps to 1 in attack (`:5989-5998`); mesh 0 re-drawn with sheet 6 pulsing (`ZzzObject.cpp:2635-2640`); lightning + shiny at bone 63 (`ZzzCharacter.cpp:11216-11219`); BOSS_LASER+1 from bone 63 and a 9-way skill (`:1905-1925`); attack speed 0.15, die 0.2 (`ZzzOpenData.cpp:3671-3678`); bbox 1.5 m (`ZzzCharacter.cpp:11821`) | 2085; bbbb00, bbb01/02.tga, bbbb02, uo00, bbbb03 | **missing**. A many-headed boss; its bind pose stretches 17.8 m, so read the clip, not the bind |
| 50 | Sea Worm | **Monster39** | 1.8 | — | 275; uo00, uo01.tga | **missing**, and unspawned in 0.75 |
| 240 | Baz | — | — | — | — | already built (Lost Tower) |

- **To import:** Monster34-39 (6 bodies, 5 if the Sea Worm is skipped) and Staff07 (1 weapon); figure rows for Great Bahamut and Silver Valkyrie.
- Already in hand: CrossBow06 (Bluewing).
- Needs `figures_atlans.json`.

---

### 5. Sounds and music

| file | bytes | in `source/sounds`? | where MuMain plays it |
|---|---:|---|---|
| Sound/**aWater.wav** | 1169030 | **no** | `SOUND_WATER01`, ambient loop always (`ZzzOpenData.cpp:4736`; `SceneManager.cpp:879-881`) |
| Sound/**pSwim.wav** | 144726 | **no** | swim footsteps off the safe zone (`ZzzOpenData.cpp:4742`; `ZzzCharacter.cpp:5368-5371`) |
| mBahamut1 | 426578 | no | Bahamut (`ZzzOpenData.cpp:3639-3643`, with mYeti1, which we have) |
| mBepar1, mBepar2 | 161682, 111950 | no | Vepar (`:3644-3647`, with mBalrog1, which we have) |
| mValkyrie1, mValkyrieDie | 331126, 194210 | no | Valkyrie (`:3653-3658`, with mBaliAttack2, which we have) |
| mLizardKing1, mLizardKing2 | 171302, 154778 | no | Lizard King (`:3660-3664`, with mGorgonDie, which we have) |
| mHydra1, mHydraAttack1 | 358534, 152766 | no | Hydra (`:3671-3673`) |
| sEvil | — | yes | Vepar attack |
| Music/**atlans.mp3** | 4526396 | no | `PlayMp3(MUSIC_ATLANS)` always on the map (`SceneManager.cpp:1047-1052`) |

That is 11 new wavs and one mp3. New sounds go through `index.py` and then `tools/cook.py --only showing` (house rule).

---

### 6. A look direction

**What MuMain draws:**
- black clear and fog
- a floor lit only by a painted teal-blue TerrainLight: pale cream sand under cyan-blue light, so it reads aqua; dark mossy-green ridges painted near black
- no lamps
- life and colour from:
  - pink and yellow coral cards
  - green kelp swaying in hundreds
  - the brick-red octopus
  - the cyan glyph stones and magic circles
  - fish
  - caustics rippling across the sand
  - bubbles

**It should read as a sunlit shallow sea seen from the floor, not a dark abyss.**
- MU's paint is bright: walkable median luminance 86, against the Lost Tower floors' 51-123 and the Dungeon's ~108-140, but in blue and green.
- The brightest corner is the safe zone.
- The caustics and light shafts say "sun above the water".
- This fits the house rules: night looks never too dark or foggy, and the user turns down veils ("nothing but dark", "too foggy").

**What the engine's sheet must carry, since MU's black fog would read as night, not sea:**
- **A key from straight above, cool and soft:** the sun through water.
  - elevation high (~75-80°)
  - colour pale cyan-white, at moderate strength
  - MU's 20% shadows say the shadows should be faint: the fill near the key, the shadows soft
  - The Lost Tower's 85° "shadows shrink to the feet" pick is a good precedent.
- **Fill from the water itself:** sky and ground ambient teal, not black, so the ridges' shade goes deep teal, not black.
- **The haze is the water.** A teal-blue scattering, tinted, at a modest density. Start between Lorencia's 0.0055 and the Dungeon's 0.009, and expect the user to want it lighter. This is the one place this map wants more than a trace: distance must fade to water, never to black. Invention, marked: MU's own fog is black.
- **Grade:**
  - shade toward teal, light toward a pale aqua-white
  - saturation near 1 (the TerrainLight already brings the blue, as Devias's did and was eased to 0.82)
  - a faint warm sepia in the light only, per the house sepia rule (hue, not saturation), so the sand stays cream rather than going cyan
- **Bloom:** low threshold for the additive caustics, shafts, glyphs and circles, but strength modest. Monster auras are subtle in this house.
- **No grass, no wind, no rain, no leaves**, but the motes, bubbles and fish.
- **water_flow 0**: there is no flowing water sheet; the caustics are their own flipbook.

**Proposed starting `sheets/worlds/atlans.json`**, built from the Lost Tower/Dungeon sheet's shape (tonemap 1, lamp_shadow, ssao) with Devias's daylight exposure and Noria's sky-fill logic. All values are invention, for the first shots:

```json
{
  "note": "Atlans's light, laid over lighting.json while a character stands in Atlans. MU draws it on its default black clear and fog (SceneManager.cpp:380-401) with no lamp at all: the teal-blue TerrainLight (walkable mean 34, 104, 139) carries the floor, the additive caustics (wt00-31) and light shafts say sunlight through water, and MU draws every shadow at 20% (ZzzObject.cpp:9483). So: a pale cyan key from nearly overhead, a teal fill close behind it so shadows stay faint, and a teal water haze in place of MU's black fog so distance reads as sea, not night. Invention on MU's look; first cut 2026-10-03, unshot.",
  "elevation": 78.0,
  "sun_colour": [0.78, 0.92, 1.0],
  "sun_strength": 2.6,
  "sky_colour": [0.12, 0.32, 0.38],
  "horizon_paleness": 0.4,
  "ground_colour": [0.05, 0.12, 0.12],
  "ambient_strength": 1.2,
  "exposure": 1.5,
  "dust_colour": [0.18, 0.42, 0.48],
  "dust_density": 0.006,
  "lamp_strength": 3.14159,
  "glow_strength": 1.6,
  "bloom_threshold": 0.9,
  "bloom_strength": 0.3,
  "contrast": 0.4,
  "saturation": 0.95,
  "split": 0.4,
  "tint_low": [0.86, 1.0, 1.08],
  "tint_high": [1.08, 1.03, 0.92],
  "ssao_strength": 1.0,
  "grass": 0.0,
  "probe": 0.0,
  "water_flow": 0.0,
  "water_sheen": 0.0,
  "tonemap": 1,
  "lamp_shadow": 0.85
}
```

Expect the first-shot fights to be:
1. the haze density (too milky versus too clear)
2. whether the sand goes cyan (lower saturation or a warmer tint_high)
3. the shadow strength against the key-to-fill ratio

The Dungeon's "can we use more actual shadows" history argues for keeping the key above the fill even here. MU's 20% shadow is a lower bound, not a target.

**House rules applied:**
- **Music is rare and in fights.** MuMain's always-on `atlans.mp3` becomes at most a fight track, WoW-rare. `aWater.wav` is the bed.
- **Subtle effects:** bubbles as small, faint specks, not a fizz, with the 845 vents de-synchronised (MU's timers all start at 0) and thinned by distance. The motes reuse `leaves.*` at Lorencia's tuned speed.
- **Test runs muted, judged in game by the user.**

**Decisions for the user:**
1. **Grid:** the client's, or OM's 075. The 075 file closes the middle ridge path and the east corridor (6584 tiles), where 89 of 0.75's own spawns stand. Recommend the client's.
2. **Swimming:** MU's swim walk/run and treading-water idle off the safe zone, against keeping the land run (the Running rule, `running-is-muMain-s6`, says Mixamo run out of combat). Which clip does a swimmer use, and does the gloves-+5 run rule come with it?
3. **Fish height:** MuMain's bug pins the Fishs to the sand; transcribe at their 0.5-2 m spawn height (ours), or crawl them.
4. **Caustics:** MU's 32-frame flipbook on the 4089 overlay tiles only, or a procedural caustic over the whole floor (ours). The flipbook is faithful and cheap.
5. **Haze:** MU's black fog, or a teal water haze (proposed, ours).
6. **Music:** whether `atlans.mp3` plays in fights at all.
7. **Mounts:** keep MU's Uniria/Dinorant ban in Atlans.

---

## Part C: the engine side

Written 2026-10-03, read-only (no repo file edited, no cook, no window, no stash). Paths relative
to `MU2_BGFX/` unless marked. OM = `LEGACY/reference/openmu/src/Persistence/Initialization/`,
MM = `LEGACY/reference/MuMain/src/source/`, D = `LEGACY/reference/MuMain/src/bin/Data`,
WZ = WebZen 1.00.93's data as an earlier session extracted it
(`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/5770e6b4-410c-4720-aca8-388135fec063/scratchpad/wz/Data`,
not in the repo; cp949). Line numbers are HEAD c0760a9c (2026-10-03) plus the working tree as
found. Template: `docs/lost-tower-port.md` Part C and `docs/blood-castle-port.md` Part C, and the
two step-1 commits `1f05c029` (tower) and `bb672abe` (castle: 17 files, terrain.py +3, maps.cpp +5,
arrival +1, minimap +1, the extraction, ground.json, `bc_*` sheets, the world sheet).

**Another session is live in the sim right now** (Blood Castle's run): uncommitted
`src/sim/{event.h, realm.cpp, realm.h, realm_fight.cpp, realm_move.cpp (+castleTick/castleKill),
realm_quests.cpp, realm_skills.cpp, recovery.h, skills.cpp, skills.h}`, `src/game/ui/tracker.*`,
`tests/sim_test.cpp`, `docs/cook-log.md`, two item recipes. `source/mu.db-shm` was touched 18:54
today (WAL 0 bytes). Every Atlans step that touches `realm*.cpp`, `gates.cpp`, `travel.h`,
`sim_test.cpp` or mu.db must read `git diff` first (memory: another session edits the same files).

---

### 0. The record the engine has to carry (short; pass A owns it)

- **Map 7, "Atlans"** (`OM/Version075/Maps/Atlans.cs:20-25`). MuMain `WD_7ATLANSE`
  (`MM/World/MapInfra/MapManager.h:16`); folders `D/World8` + `D/Object8` (terrain.py map_number 8).
  World8: `EncTerrain8.att/.map/.obj`, `TerrainHeight.OZB`, `TerrainLight.OZJ`, `leaf01.OZJ`,
  TileGrass01/02, TileGround01/03, TileRock01-04, TileWood01 -- **no TileWater01** (slot 5 is
  MU's animated `Object8/wt00-31` caustic, §3.1). Object8: Object01-41, Fish02-09, 31 caustic
  frames `wt00-wt31.OZJ` (64x64 grey), `drop01.OZJ` (the bubble, 3x3 atlas), `light01`, `sea01`.
  `D/Music/atlans.mp3`; `D/Sound/aWater.wav` (air), `pSwim.wav` (step), `mDeathBubble.wav`,
  `mBahamut1`, `mHydra1`, `mHydraAttack1`, `mLizardKing1/2`, `mValkyrie1/Die`, `mWorm1/2/Die`.
- **It has its own spawn gate**: exit 49 `CreateExitGate(maps[7], 15, 11, 27, 23, 0, true)`
  (`OM/Version075/Gates.cs:148`; WZ `Gate.txt` row 49 is **14**,11-27,23). Death and the Town
  Portal stay in Atlans, as in the Lost Tower; the `home` field is not needed. MuMain allows the
  Town Portal there (`MM/World/MapInfra/PortalMgr.cpp:52`).
- **It is reached from Noria in 0.75** (not Devias): Noria enter 45 `242,240-245,243` lvl **60**
  -> exit 46 in Atlans `14,12-15,13` dir 3 (South); Atlans enter 47 `9,9-11,12` lvl 60 -> exit 48
  in Noria `240,240-241,243` dir 7 (North) (`OM/Version075/Gates.cs:135, 149, 205-206`; WZ
  `Gate.txt` 45-48 identical, level 60 on all four). Exit 46 sits inside the spawn box.
- **The warp list**: 0.75 and 0.95d have **no Atlans row** (`OM/Version075/Gates.cs:43-56`,
  `OM/Version095d/Gates.cs:42-55`: they stop at LostTower7). Season Six adds three:
  Atlans 4000 zen lvl 70 -> gate 49; **Atlans2** 4500 lvl 80 -> gate 75 `225,50-228,53`;
  **Atlans3** 5000 lvl 90 -> gate 76 `62,157-68,163` (`OM/VersionSeasonSix/Gates.cs:57-59, 190-192`).
  WZ has the same two warp-only gates (`Gate.txt` 75, 76, level 60) and `MoveLevel.txt` gives
  Atlans **75** for the whole map. These are the "three inner areas". Gate 53/56 (Atlans<->Tarkan,
  14,225) is S6/WZ only and its box is solid rock here; Tarkan is out of scope.
- **Underwater rule**: OpenMU marks the map `IsUnderwater` (`OM/Version075/Maps/Atlans.cs:44-48`,
  `OM/BaseMapInitializer.cs:323-326`), which only swaps `MovementSpeed` for
  `MovementSpeedUnderwater` (`LEGACY/reference/openmu/src/GameLogic/PlayerMovement.cs:268-273`) --
  MU's fast swim is +5 **gloves** in Atlans where elsewhere it is +5 boots
  (`MM/Engine/Object/ZzzCharacter.cpp:466-485`). MuMain forbids equipping a Horn of Uniria or
  Dinorant there (`MM/UI/NewUI/Inventory/NewUIMyInventory.cpp:347-350`).
- **Monsters** 45 Bahamut (43), 46 Vepar (45, Energy Ball), 47 Valkyrie (46, arrows), 48 Lizard
  King (70, Lightning), 49 Hydra, 50 Sea Worm, 51 Great Bahamut (66), 52 Silver Valkyrie (68, Ice)
  (`Atlans.cs:400-660`). **Settled by the user (2026-10-03): the spawns come from OpenMU's
  Version075 `Maps/Atlans.cs`.** The sources disagree, for the record:
  OpenMU 075 (the one used): 337 one-tile rows -- 45x29, 46x45, 47x43, 48x65, **49 Hydra x4 at level 74 /
  19 000 HP**, 51x66, 52x85, **no Sea Worm**; WZ `Monster.txt:76-83` + `MonsterSetBase.txt:47-109`:
  **386** monsters -- 45x41, 46x45, 47x43, 48x77, **49 Hydra x1, level 108 / 89 000 HP, respawn
  1800 s, a "Hydra Boss" spot at 24,214**, 50 Sea Worm x12 (lvl 74), 51x82, 52x85; 333 single rows
  with wander distance 30 plus 9 area rows.
- **NPCs**: OM one Guard (`Atlans.cs:51-54`, line 53) 240 at 23,17 SW (`Atlans.cs:51-54`); WZ 240 at 14,22 and 253 at 16,24
  (`MonsterSetBase.txt:47-48`).

#### 0.1 The grid, from a scratch extraction

`python3 pipeline/terrain.py $D/World8 atlans <scratch>/extract 8` (scratchpad only): height
**0.00-3.83 m**; slots 0 TileGrass01 42.5%, 10 TileRock04 55.0%, 9 TileRock03 2.5% (layer 1);
layer 2 holds **slot 5 on 6 974 tiles, 4 089 with blend > 0** -- the caustic overlay; light mean
**(28, 87, 110)**, the blue baked into TerrainLight (north half (36,96,119), south (21,78,101));
**5 215 placements, 40 kinds** (951 Object26, 845 Object23, 475 Object07, 421 Object06, 328
Object34, 296 Object32, 246 Object25 ...), no model missing; Fish02-09 and Object22 unplaced.

Words: {4: 42 156, 0: 22 958, 1: 318, 5: 102, 3: 1, 6: 1}. **No NoGround tile at all** (no void,
no abyss, no `VOID_BY_MAP`, no `FLAT_OVER_VOID`): the sea floor is ground everywhere, rock walls are
word 4. Safe flag cols 10-34, rows 7-28 (421 tiles). **Walkable ground is one 8-connected region**
of 23 277 tiles (cols 9-245, rows 5-239) -- unlike the Dungeon's and the tower's floors. Split by
walking distance from each S6 landing (multi-source flood): Atlans 5 241 tiles (NW, 9-104 x 5-132),
Atlans 2 8 323 (NE/E), Atlans 3 9 713 (S/SW, holds the Hydra's lair). Walks from the spawn: 318
tiles to gate 75, 490 to gate 76, 739 to the Hydra. WZ's spawns by that split: area 1 = Bahamut 41,
Vepar 37, Valkyrie 10; area 2 = Vepar 8, Valkyrie 31, Lizard King 29, Great Bahamut 38, Silver
Valkyrie 22; area 3 = Lizard King 48, Great Bahamut 42, Silver Valkyrie 62, Sea Worm 12, Hydra 1;
four spots fall on rock (nearestOpen takes them). (WZ's split is shown for the shape of the map
only; the spawns that load are OpenMU's, §5.)

Rigged objects (BMD headers read in the scratchpad; bones > 1 means a clip to sway): Object08 (34
bones, x23), 09 (x27), 10 (x35), 11 (x16), **25 (28 bones, x246)**, **26 (84 bones, x951)**, 28 (89,
x95), 29 (91, x40), 32 (x296), 34 (x328), 38 (x8), 39 (x125), 41 (6 bones, x86) -- **2 276 animated
placements** (Lorencia has 20 rigged models in all). The kelp is the sway system's job (§3.6).

---

### 1. How a world is registered and loaded today -- the checklist for map 7

There is still **no single registry**. Name: `atlans` (one word; every lookup is by it). Keys in the
pipeline tables are **MU's map number** (terrain.py's `map_number - 1`): Atlans is **7**, Noria 3.

#### 1.1 Pipeline (source side)

| # | where (file:line) | Atlans needs |
|---|---|---|
| P1 | `pipeline/terrain.py main` `:560-` | `python3 pipeline/terrain.py $D/World8 atlans source/world 8` |
| P2 | `pipeline/terrain.py:118-136` `HIDDEN_BY_MAP` | `7: {22, 39}` -- type 22 (Object23, x845) is MU's bubble emitter, `HiddenMesh = -2` (`MM/Engine/Object/ZzzObject.cpp:4055-4063`); type 39 (Object40, x4) is a lean box, `CreateOperate` + hidden (`ZzzObject.cpp:4769-4776`) |
| P3 | `pipeline/terrain.py:148-155` `BLEND_MESH_BY_MAP` | `7: {23: 0, 32: 1, 34: 1, 38: 0, 40: 0}` (`ZzzObject.cpp:4064-4083`); 23/32/34/40 also pulse (§3.7) -- pass B owns the look |
| P4 | `pipeline/terrain.py:164-175` `GRASS_BY_MAP` | **`7: []`** -- MU sows no grass in Atlans (`MM/Render/Terrain/ZzzLodTerrain.cpp:3619`), and slot 0 is 42.5% of the sea floor |
| P5 | `terrain.py:194, 216, 229, 238, 290, 292` `VOID_BY_MAP`, `LAVA_SPILL_BY_MAP`, `OPEN_BY_MAP`, `VOID_FILL_BY_MAP`, `LIGHT_DEPTH_BY_MAP`, `WATER_FLOW_BY_MAP` | none (no void, no lava, no river; `LIGHT_DEPTH` only if the blue baked light wants keeping whole -- pass B) |
| P6 | `pipeline/terrain.py:317-322` and `pipeline/index.py:44-49` `OPERABLE_BY_MAP` | `7: {39}` in both -- MOVEMENT_OPERATE's Atlans arm, `case 39: Pose` (`MM/Engine/Object/ZzzInterface.cpp:1736-1741`) |
| P7 | `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP` | `7: [(14,11,27,23), (14,12,15,13), (9,9,11,12), (225,50,228,53), (62,157,68,163)]`; `3 +=` `(242,240,245,243), (240,240,241,243)` (Noria's 45 and 48). NB `4` and `11` and Devias's tower boxes are still missing (side finding) |
| P8 | `source/world/atlans/ground.json` -> `tools/content.sh:35-36, 54-60` | `at_` prefix (as `lt_`/`bc_`); 9 sheets from World8; slot 5 has **no** World8 sheet -- see §3.1 |
| P9 | `source/mu.db` `gates` | `insert into gates values (49, 7, 14, 11, 27, 23, 1)` (WZ box; OM's starts at 15) -- **the one row that keeps a death in Atlans** |
| P10 | `pipeline/index.py worlds()` | automatic once ground is built (`build-only skips the index`: run index.py after) |
| P11 | effects in `pipeline/index.py` (`"leaf"` `:560`, `"snow"` `:565`, `"flare"` `:509`) | new rows `"bubble"` (Object8/drop01), `"caustic"` (wt00-31 as one strip), `"mote"` (World8/leaf01), then `--only showing` |

#### 1.2 The cook (`tools/cook.py`)

| key | line | Atlans |
|---|---|---|
| `AIRS` | `:202-203` | `"atlans": "Fish02"` -- but MU flies two (Fish02/03, `MM/Engine/AI/GOBoid.cpp:886-916`); the table holds one name today (§3.4) |
| `CRAWLS` | `:207` | `"atlans": ...` Fish04-07 north / Fish08-09 south (`GOBoid.cpp:1750-1770`) -- also one name today |
| `AIRS_FROM` | `:214` | none (Object8 is Atlans's own) |
| `FLAT_OVER_VOID` | `:1174` | no |
| `LIGHT_REACH_BY_WORLD`, `OBJECT_LIGHT_DEPTH_BY_WORLD` | `:1176`, `:1182` | only if the look asks |
| `ANCHOR_KINDS_BY_WORLD` | `:1224-1226` | `"atlans": {"Object23": <bubble kind>}` -- the 845 hidden emitters, as Blood Castle's Object38 mist and the tower's vents |
| `figure_file` | `def figure_file` | automatic `figures_atlans.json` |
| `RESPAWN_VERSION075` | `:1873` | none if mu.db's `respawn_seconds` is written true |
| `FOLK_VERSION075` | `:1900-1992` | `7: [(240, ..., 23, 17, SouthWest)]` -- OM Version075's one Guard (`Atlans.cs:53`), the file the spawns come from |
| this map's spawns | `:2148` | automatic |
| tables after any item/kind change | (no loop) | now **seven** runs: lorencia, noria, devias, dungeon, losttower, bloodcastle, atlans (memory: new items need every world's tables) |

#### 1.3 The runtime -- every place a world is named

| # | file:line | Atlans row |
|---|---|---|
| R1 | `src/game/world/maps.cpp:19-35` `kMaps` | `{"atlans", 7, {20, 17}}` -- middle of spawn gate 49; **not** `underground` (it has leaves/motes and its own air, §2) |
| R2 | `src/game/world/maps.h:18-36` `MapRow` | the place to add `air`, `grassy`, `boid`, **`underwater`** (§2's refactor) |
| R3 | `src/game/ui/arrival.cpp:55-64` `kPlaces` | `{"atlans", "Atlans", ""}` |
| R4 | `src/game/ui/minimap.cpp:289-299` `mapName` | `case 7: return "Atlans";` (Noria's "Gate to Atlans" label needs this) |
| R5 | `src/game/ui/minimap.cpp:271-287` `floorName` | none in 0.75 (no same-map gates); `case 75: "Atlans 2"`, `case 76: "Atlans 3"` only if their warp rows come |
| R6 | `src/game/world/maps.cpp:76-90` `placeName` | `return world` unless the three areas are named (§4.3) |
| R7 | `src/sim/realm_travel.cpp:14-29` `kRows`, `src/sim/travel.h:29` `kTravels = 13` | decision 1: none (0.75), or S6's three appended -> `kTravels = 16` (u32 `found_` fits; **append only**, saves keep their bits, `src/game/save.cpp:166, 336`) |
| R8 | `src/game/ui/travel.cpp:195` `kComing` | lists `{"Atlans", 7}`; **once `mapNumbered(7)` exists and no travel row names map 7, the card vanishes** (`:197` skips it, `:147-150` builds cards only from rows) -- Atlans would be off the Tab window entirely |
| R9 | `sheets/worlds/atlans.json` | new, found by name (`maps.cpp` `mapSheet`) |
| R10 | `src/game/play_open.cpp:32-37` | `windy_` would be **true** for Atlans (not Noria, not underground) -- wrong; needs its own air (§2 #1) |
| R11 | `src/game/world/world.cpp:94-97, 215` | leaves open (not underground) with the Lorencia leaf -- needs Atlans's mote mode (§2 #4) |
| R12 | `src/game/world/boids.cpp:51-97` `boidOf`/`airsOf` | fish (§3.4) |
| R13 | `src/app/modes/play_mode.cpp:685-690` `takeHome` | nothing: gate 49's row keeps `home` unused |
| R14 | `src/app/modes/play_mode.cpp:912-942` music | decision (atlans.mp3) |
| R15 | `src/sim/gates.cpp:9-80` | §4 |
| R16 | `source/mu.db` | §5 |
| R17 | `tools/bot/bot.cpp:67-71` | the bot's world table: add `{7, "atlans", {20, 17}}` if bot runs should reach it (Blood Castle is not there either) |

Load path unchanged: `PlayMode::travel` -> `World::open` (`world.cpp:90-`) -> `Play::open`
(`play_open.cpp:24-`, `cooked/atlans/atlans.mur`, breeds without a figure held back) ->
`World::raiseAirs` (`world.cpp:198-225`).

---

### 2. Every hard-coded per-world check, and what Atlans needs from each

Grep: `grep -rnI -E '"(dungeon|devias|noria|lorencia|losttower|bloodcastle)"' src tools pipeline`.

| # | where | today | Atlans needs (MU's rule) | smallest change |
|---|---|---|---|---|
| 1 | `play_open.cpp:32-37` + `:461-465` the air; `play_sound.cpp:407` `loop(wind, !indoors \|\| dungeonAir_)` | `windy_`, `dungeonAir_`, `towerAir_` by name | **aWater.wav looped on the whole map** (`MM/Scenes/SceneManager.cpp:879-881`, stopped elsewhere `:952-955`); no wind | `MapRow.air` = sound name (`"world_water"`), plus "whole map" bit (Dungeon/tower/Atlans all play regardless of roof). `sounds.json` `world_water: aWater.wav` beside `world_tower` (`source/sounds/sounds.json:1399-1405`). This is the refactor both earlier Part Cs asked for; Atlans is the third air keyed by name |
| 2 | `play_sound.cpp:104-108` footsteps | grass (Lorencia/Noria slot 0), snow (Devias), else soil | **pSwim.wav off the safe zone**, soil inside it (`MM/Engine/Object/ZzzCharacter.cpp:5368-5371`) | `swimming_` = underwater && !safe tile; `player_step_swim` event (`sounds.json:576` notes pSwim was left out "until a map") |
| 3 | `play_sound.cpp:412-414` the room | Stone for dungeonAir_, else Roofed/Open | ours: an underwater room (a low-pass on the world bus) would be new DSP; MU has none | none at first; `Room::Open` (a decision if wanted) |
| 4 | `world.cpp:215` `leaves_.open(..., name == "devias")`; `leaves.cpp:164-200` | Lorencia leaf or Devias snow | **CreateAtlanseLeaf**: BITMAP_LEAF1 off **World8/leaf01** (16x16 soft dot), Noria's motion (the same function serves both, `MM/Render/Effects/ZzzEffectFireLeave.cpp:299-319`), alpha-blended in Atlans (`:525`); RequireLeavesEffect true on the whole map (`MM/Scenes/MainScene.cpp:83`) | a third mode in `Leaves::open` (`mote` sheet), and the wind's kWindScale/drift as Noria's |
| 5 | `boids.cpp:51-97` | Bird/Butterfly/Bat/Crow by name; `Flight::kMaxBirds = 5` (`flight.h:60`) | **fish schools**: all 40 boids live in Atlans (no `i >= 5` cut, `GOBoid.cpp:1233-1235`), Fish02/03 random, north half only (hero row < 128), 1.5-3.5 m over the floor, no landing (`GOBoid.cpp:886-916, 1141-1160`), turned back at the safe zone (`:1459-1465`) | §3.4 |
| 6 | `scurry.cpp:32-35` `crawlOf`; `scurry.h:47` `kMaxRats = 3` | Rat01 in the Dungeon | **10** (MAX_FISHS) swimmers: Fish04-07 north, Fish08/09 south (glowing BlendMesh 0), on open tiles (`GOBoid.cpp:1668-1770, 1848-1856`) | §3.4 |
| 7 | `weather.cpp:113-116` | wet worlds by name | none (Atlans is not listed: dry) | nothing |
| 8 | `ornaments.cpp:276, 294, 334, 377` | tower/castle/Noria glows, Devias beacon | MU has **no** RenderObjectVisual arm for Atlans | nothing |
| 9 | `skulls.cpp:36`, `trap_show.cpp:82`, `lava_smoke.cpp:48`, `castle_sparks.cpp:45`, `void_clouds.cpp:55`, `portal.cpp:40`, `doors.cpp:44` | other worlds' features | none | nothing (each returns early) |
| 10 | `maps.cpp:76-90` `placeName` | tower floors by box, Dungeon floors | three areas only if S6 rows (§4.3) | a row |
| 11 | `minimap.cpp:271-299` | names by number | `case 7` | a row |
| 12 | `realm_travel.cpp:75-121` `settleFound` | floods each row's landing; **a connected map gives every tile to row 0** | **must change if Atlans gets three rows** (§4.3) | one multi-source flood |
| 13 | `play_mode.cpp:912-942` music | Lorencia pub, Devias roofs | MU plays `MUSIC_ATLANS` (atlans.mp3) on the whole map (`SceneManager.cpp:1047-1052`) | decision 4 (user's rule: music rare) |
| 14 | `src/sim/realm_move.cpp:146` `riding = pet.mount && !safe` | horse anywhere off safe | MU: horns cannot be equipped in Atlans (`NewUIMyInventory.cpp:347`) | decision 5; smallest: `riding` false on an underwater map |
| 15 | `src/sim/items.h:690-692` `firecrackerMap` | 1, 4, 9, 11-17 | the comment quotes MU's "any map under Atlans" | nothing (Atlans is not a dungeon) |
| 16 | `src/sim/realm_tuning.h:286` `kPoisoners`, `:312` `kBosses = {35, 38}` | | Hydra (49) and Sea Worm (50) carry `MonsterSkill` like the Gorgon/Balrog; the Hydra's own show is MU's BITMAP_BOSS_LASER+1 from bone 63 on `AttackTime % 5 == 1` (`ZzzCharacter.cpp:1905-1912`) | add 49 (and 50?) to `kBosses` only if the "one in five" boss blow is wanted; the beam is §3.8 |
| 17 | `src/game/play_pointer.cpp:412-435` `shoots` | Hunter, bow/crossbow stances | **Valkyrie (and Silver Valkyrie)** shoot arrows: MU's `o->Type == MODEL_VALKYRIE -> CreateArrows` (`ZzzCharacter.cpp:4831-4856`) | a figure-name row beside `kHunterFigure` (MODEL_VALKYRIE is one model for 47 and 52) |
| 18 | `src/game/play.cpp:1077-1160` attack shows by `attackSkill` | meteor 2, ice 7, power wave 11, lightning 3, boss | Vepar 17 Energy Ball (no monster arm today -- check `play_open.cpp:578` `skillNumbered`), Lizard King 3 (free: thunder; MU draws 6 BITMAP_JOINT_THUNDER from both hands, `ZzzCharacter.cpp:2330-2342`), Silver Valkyrie 7 (free: the Ice cast) | Energy Ball for a monster is new showing work unless `skillNumbered(17)` already routes it |
| 19 | `MM ZzzObject.cpp:800-806, 875-881, 1011-1017` | -- | MU draws **every body's shadow at alpha 0.2 in Atlans** (0.7 elsewhere) | sheet only: a weak key and strong fill (no shadow-opacity knob exists; `lamp_shadow` is the lamps') |
| 20 | `tools/cook.py:202-214, 1224-1226`; `pipeline/*_BY_MAP` | | §1 | rows |

**The refactor to do first, now overdue:** three Part Cs in a row have found the air, the grass step,
the boid and now the swim as string tests. Widen `MapRow` (`maps.h:18-36`) with
`const char* air` (sound event, null = wind), `bool airEverywhere`, `bool grassy`,
`bool underwater`, and read them at `play_open.cpp:32-37, 461-465`, `play_sound.cpp:104-108, 407`,
`world.cpp:215`. Pure refactor for the six worlds (byte-identical headless logs), then Atlans is rows.

---

### 3. Underwater: what the engine has, what it lacks, the cheapest route for each

What MU actually does in Atlans (all 41 `WD_7ATLANSE` references read): caustic frames on the
ground's slot-5 overlay; no grass; drifting motes (its "leaves"); bubbles from 845 hidden emitters,
from every character's head once in ten seconds, from dying bodies, from a mount's nostrils and a
Bahamut's body; fish schools and swimmers; swim clips off the safe zone; faint shadows; aWater air;
pSwim steps; atlans.mp3. **No fog colour, no clear colour, no water surface, no caustics on
figures** -- SetWorldClearColor has no Atlans arm, default black (`SceneManager.cpp:371-403`); the
blue is in TerrainLight. Prices are against `docs/budget.md` (5.5 ms at 1080p; effects 0.3,
shade 2.3, spare 0.2; sprite overdraw ~0.018 ms per full screen at 1080p per the tower's Part C §5.4).

#### 3.1 Caustics -- MU's, on the ground (lacks; one sampler)

- **MU**: on any tile whose overlay (layer 2) is slot 5, the overlay pass binds
  `BITMAP_WATER + WaterTextureNumber` instead -- 32 frames, one per reference frame (25 fps, a
  1.28 s loop, `SceneManager.cpp:326-338`) -- and draws it **additively** (`RenderFaceBlend`,
  `ZzzLodTerrain.cpp:2056-2061`; the shader path `:1971-1974`). 4 089 tiles carry it. BMD meshes
  whose texture is the water bitmap cycle it too (`MM/Render/Models/ZzzBMD.cpp:1322-1325`) -- pass B
  must say which Object8 models do.
- **Engine today**: `fs_ground.sc` has three layers a part, flow/slide for water layers
  (`shaders/fs_ground.sc:13-18, 99-131`), no frame sequence. Slot 5 would be built from a missing
  `TileWater01` sheet.
- **Cheapest route**: decode wt00-31 into one 2048x64 strip (or 8x4 atlas 512x256), cooked as an
  effect sheet `caustic`; one sampler and one uniform `u_caustic` (x frame 0-31 from the clock, y
  strength, z the slot whose weight gates it) in `fs_ground`; added after lighting, multiplied by
  the slot-5 weight. In `ground.json` slot 5 becomes `"TileWater01": "caustic"` with a material that
  draws nothing of its own (or the neighbour's sheet). A 64x64 texture stays in cache: **well under
  0.05 ms**, in the shade account. New lighting knob end to end (as the tower's `water_glow`:
  `lighting.h/.cpp` known keys, `renderer.h/.cpp/_setup.cpp` uniform, the shader).
- **Ours, a decision (2)**: caustics on every lit surface (whole floor, objects, figures) as a
  world-projected (x,z) sample in `fs_shade` too -- the modern "underwater" read. One fetch per shaded
  fragment, ~0.05-0.1 ms at 1080p, but it is not MU's and lights figures MU never lit.

#### 3.2 Blue water fog and clear (has it; sheet only)

- `dusty()` (`shaders/common.sh:100-117`) is already the one call every surface makes (ground,
  grass, shade, smoke): `dust_colour` + `dust_density` in `sheets/worlds/atlans.json` give a blue
  haze for free. The Dungeon runs 0.009; underwater wants more (0.02-0.04) and a teal colour. Zero
  cost beyond today.
- The clear is hard-coded black (`src/gfx/renderer.cpp:888, 1005`), and MU's is black too. Atlans
  has no void, so only the far map edge shows it, and `dusty()` already darkens the border
  (`u_edge`). A blue clear would be a new knob; not needed.
- Sprites (`fs_effect`) do not take `dusty()`; distant bubbles will not fog. Acceptable at the
  play camera's range.

#### 3.3 Bubbles (lacks; a world sprite emitter like CastleSparks)

- **MU**: BITMAP_BUBBLE is `Object8/drop01.jpg`, a 3x3 atlas (`MapManager.cpp:41`; render
  `MM/Render/Effects/ZzzEffectParticle.cpp:8949-8952`); life 30-39 frames, scale 0.12-0.27; subtype
  0 jitters and rises 25-75 units x scale a frame, 1 drifts and swells, 2 rises straight and plays
  `mDeathBubble` (`:195-215, 4144-4175`). Sources: type 22's **845 hidden emitters**, one bubble a
  frame for the second half of every 4 s cycle (`ZzzObject.cpp:4055-4063`); every player's head,
  one a frame for 1 s in 10 (`ZzzCharacter.cpp:5698-5711`); a dying body, four a frame
  (`:3312-3320`); a mount's breath (`GOBoid.cpp:430-447`, smoke elsewhere); the Bahamut's body
  (`ZzzCharacter.cpp:2002-2011`).
- **Precedent**: `src/game/world/castle_sparks.{h,cpp}` reads anchors out of `town.cooked()` (cook's
  `ANCHOR_KINDS_BY_WORLD`) and gathers sprites into `gfx::Effects`; `lava_smoke` likewise. Write
  `src/game/world/bubbles.{h,cpp}`: anchors from the cook (845 -> only those within ~12 m of the hero
  roll), plus a `burst(at, n)` the Play calls for heads, deaths and the Bahamut. Pool ~384 sprites,
  sheet `bubble`, alpha blend. ~10 anchors in view x ~10 live bubbles each + heads: a few hundred
  quarter-metre sprites, **~0.02-0.05 ms** of the effects account. Hook in `World::open`
  (`world.cpp:117-119` beside `castleSparks_`).
- The horse/Dinorant breath (`fx/snort.h`) becomes bubbles only if mounts stay allowed (decision 5).

#### 3.4 Fish schools and swimmers (has the pools; both too small and single-model)

- `Boids` (`boids.cpp:99-`) loads **one** model per world and `Flight` (`flight.h:57-143`) is a
  bird's state machine (Fly/Down/Ground/Up) with **5** slots. Atlans wants two school models and
  **40** slots that never land. Route: `Flight::kMaxBirds` -> 40 (only Atlans fills more than 5:
  a per-world `Airs.count`), `setFish(true)` = MU's Atlans steering (`GOBoid.cpp:1141-1160`:
  two speeds by index < 35, Timer cycle, Gravity 15 or 5) and height `RequestTerrainHeight + 150..350`;
  `boidOf` returns a list. Each fish is a skinned Figure: 40 palette rows of
  `kMaxPaletteRows = 512` (`src/gfx/renderer.h:352-353`), 5-8 bones each -- trivial cost, but the
  rows are shared with the crowd and the sway (§3.6), so watch `paletteRowsRefused()`.
- `Scurry` (`scurry.cpp:41-`) the same: one model, `kMaxRats = 3`. Atlans: 10, four north models
  and two south (Fish08/09 glow). A model list keyed by the hero's row.
- Cook: `AIRS`/`CRAWLS` take a tuple per world (`tools/cook.py:202-214`, `boid_glb`), or a separate
  `SCHOOLS` table. Fish02-09 are unplaced, so the cook must name them or they never cook.

#### 3.5 Swimming (has the clips; the stance chooser lacks the rule)

- MU off the safe zone in Atlans: stand **PLAYER_STOP_FLY (11)**, walk **PLAYER_WALK_SWIM (24)**,
  run **PLAYER_RUN_SWIM (33)**, the weapon slung while swimming (`ZzzCharacter.cpp:296-302, 618-626,
  15251-15254`); +5 gloves gives PLAYER_FLY (34) instead (`:466-485`). All four are in the player
  library (`assets/index.json` actions 11 "Stop fly", 24 "Walk swim", 33 "Run swim", 34 "Fly").
- Precedent: the ride clips on `FigureBody` (`src/game/figures.h:190-205`, `rideIdleClip` etc.) and
  their choice in `figures.cpp`. Add `swimIdleClip/swimWalkClip/swimRunClip` read at the same
  place, chosen when the Play says `underwater && !safe`. Presentation only: our run factor already
  applies to everyone off a safe tile (`realm_move.cpp:153`, `realm.h:230`), so the sim needs no
  change unless the gloves rule is wanted (decision 3). The swim is a hover: the figure should ride
  ~0.3-0.5 m higher? MU keeps feet at ground height and the clip floats the body -- check on the
  bench, no code.

#### 3.6 Swaying seaweed (has it: world animation)

- `Sway` (`src/game/world/sway.h:1-110`) stands one Figure per placement of every model the cook
  baked a clip for (`cook_world_clip` -> `cooked/<world>/clips.json`), frustum-culled, seeded
  phase, and it is generic -- Atlans's 13 rigged kinds need only their recipes to keep the clip.
  MU's rates: default object velocity, type 40 (Object41) at 0.05 (`ZzzObject.cpp:4078-4082`).
- **The risk is count, not code**: 2 276 animated placements against Lorencia's handful; the kelp
  Object26 alone is 951 at **84 bones**. On screen the play camera sees ~1-2% of the map, so tens
  of figures, each a palette row (`kMaxPaletteRows = 512`, shared). Measure posedCount and
  `paletteRowsRefused()` on the densest field before shipping; a fallback is to bake the 84-bone kelp
  as a vertex-shader sway (no skinning) -- new shader work, only if measured.

#### 3.7 Pulsing glows (has it)

- Types 23 and 40 `BlendMeshLight = sin(WorldTime*0.002 / 0.004)*0.3 + 0.5`, 32/34
  `sin(WorldTime*0.004)*0.5 + 0.5` (`ZzzObject.cpp:4064-4083`). The cook's glow pulse is
  `sin(WorldTime*0.004)*a + b` (`tools/cook.py:636-671`, `renderer.cpp:220`): 32, 34, 40 fit as-is;
  23's half rate does not (a rate float per material, or accept 0.004).

#### 3.8 The monsters' shows

- Hydra's breath beam is BITMAP_BOSS_LASER+1 off bone 63 -- `fx/aqua.h` is the wizard's
  BITMAP_BOSS_LASER sub 0, the closest thing the engine has; the Hydra also brightens its glow mesh
  while attacking (`ZzzCharacter.cpp:5989-5995`).
- Lizard King and Silver Valkyrie ride existing `attackSkill` shows (§2 #17-18). Vepar's Energy Ball
  and its hand sparks (`ZzzCharacter.cpp:2183-2195`) and the Bahamut's bubbles/blood are new.
- `mDeathBubble` on a death (subtype 2) is one event.

#### 3.9 What it costs, all together

Fog 0; caustics on the ground <0.05 ms (shade); bubbles ~0.05 (effects); fish 40 small skinned
meshes ~0.02 ms GPU and 40 palette rows; motes = today's leaves; kelp unknown until measured --
**the only line that can overdraw an account**. Measure on the bare land first, then with objects,
`--budget --stats --repeat 3`, timers off, on the display the log names (memory).

---

### 4. Travel: gates, the warp rows and the three areas

#### 4.1 Gates (rows only; all machinery exists)

`src/sim/gates.cpp` (`kExits` `:9-50`, `kEnters` `:55-82`, lookups `:85-105`), the walker
`Realm::throughGate` (`realm_move.cpp`), the mode's map change on `Gated`
(`play_mode.cpp` -> `mapNumbered(out->map)`) -- all generic since the Dungeon.

- `kExits`: `{49, 7, {14, 11, 27, 23}, 0, 0}` (spawn; warp/death/portal landing),
  `{46, 7, {14, 12, 15, 13}, 1, -1}` (from Noria; dir 3 South = (+1,-1)),
  `{48, 3, {240, 240, 241, 243}, -1, 1}` (Noria, out of Atlans; dir 7 North = (-1,+1)).
- `kEnters`: `{45, 3, {242, 240, 245, 243}, 60, 46}` (Noria to Atlans),
  `{47, 7, {9, 9, 11, 12}, 60, 48}` (Atlans to Noria). OM and WZ both ask 60 on the way **out** too.
- Grid check: Noria 242-244 x 240-242 open, col 245 and row 243 rock -- the walker's own tile
  test copes (as the tower's niches); exit 48 open but 241,243. Atlans gate 47: 6 open, 3 safe,
  2 rock, 1 safe+nomove; exit 46 all safe. Noria's corner carries four Object38 (type 37,
  BlendMesh 0) at 244.7-247.5 x 238.6-241.2 -- likely the gate's glow; pass B to confirm.
- Header comment `src/sim/gates.h` and the minimap's "Gate to Atlans" (`minimap.cpp` mapName R4).
- sim_test: a `testAtlansGates` on the tower's walker -- Noria 243,241 at 59 -> Barred 60, at 60 ->
  Gated 45 landing in 14-15 x 12-13; on atlans.mur gate 47 -> Gated landing in Noria 240-241 x
  240-243; a death -> Rose with `c == 0` inside 14-27 x 11-23.

#### 4.2 The warp rows (decision 1)

0.75 has none (§0). Options: (a) none -- Atlans reached only by Noria's corner, and the Tab
window loses its "Atlans" card (R8); (b) one row "Atlans" (S6's 4000 zen / 70, or WZ MoveLevel 75)
to gate 49; (c) S6's three. Rows are appended after "Lost Tower 7" (`realm_travel.cpp:14-29`),
`kTravels` 14 or 16 (`travel.h:29`), landing tiles 20,17 / 226,51 / 65,160.

#### 4.3 The three areas need a fix if (c)

`Realm::settleFound` (`src/sim/realm_travel.cpp:75-121`) floods walkable tiles from each row's
landing **in turn**, and a tile already taken stays with the first row. On the Dungeon and the tower
each row's floor is its own disconnected region, so it works. **Atlans is one region (§0.1)**: row 0
would take all 23 277 tiles, rows 1 and 2 would own one tile each, and since Atlans has no giver and
no chain, `byFloor_` (`:116`) would be true and `reachFloor` would open only "Atlans" -- **Atlans 2
and 3 would never open** except by standing on their exact landing tile.

Smallest fix: seed **one** queue with every landing and flood once (multi-source BFS, ~10 lines):
each tile goes to its nearest landing by walk. Identical results on the Dungeon and the tower (one
landing per region), and on Atlans the three areas of §0.1 (5 241 / 8 323 / 9 713 tiles). The same
labels can feed `placeName` (R6) so the banner says "Atlans 2" -- or keep `placeName` by box.

#### 4.4 Atlans has its own safe zone, so

`haven()` and the Town Portal land in gate 49 once the mu.db row exists; `home` (`maps.h:28-31`)
stays 0 and unused; the save resumes in Atlans (not an `event` map). The Go Back! portal works as
on the tower.

---

### 5. Spawns and monster tables

#### 5.1 mu.db today (`sqlite3 -readonly source/mu.db`, 2026-10-03)

- `monster_kinds`: 0-41, 84-89, 132, 150 (Bali). **None of 45-52.** Schema: `number, name, level,
  health, minimum_damage, maximum_damage, defense, move_range, attack_range, view_range, move_delay,
  attack_delay, attack_rate, defense_rate, respawn_seconds, attack_skill` (`attack_skill` uses OM's
  SkillNumber: 2 Meteorite, 3 Lightning, 7 Ice, 17 Energy Ball, `src/sim/skills.h:48-74`).
- `monster_spawns` (`id, number, x1, x2, y1, y2, count, map` -- x2 before y1): maps 0/1/2/3/4/11
  hold 9/258/9/8/448/96 rows; **map 7: none**. `max(id) = 929`; new rows from **930**.
- `gates`: 17 (0), 22 (2), 27 (3), 42 (4). **No 49.**
- WAL 0 bytes; after writes `pragma wal_checkpoint(TRUNCATE)` before committing (memory).

#### 5.2 Steps to add Atlans's -- OpenMU Version075's spawns (the user's decision)

1. `gates`: `(49, 7, 14, 11, 27, 23, 1)` (WZ's box; OM's `Gates.cs:148` is 15,11-27,23 -- either
   holds the arrive tile 20,17).
2. Eight kinds 45-52, the definitions in the same file (`OM/Version075/Maps/Atlans.cs:400-660`:
   level, health, damage min/max, defence, move/attack/view range, move delay 400 ms, attack delay
   1400-1800 ms -- all divide by the 50 ms tick -- attack/defence rate, `RespawnDelay` written true
   into `respawn_seconds`: 8 s for 45-47, 15 s for 48/51/52, **150 s for the Hydra (49) and Sea
   Worm (50)**). `attack_skill`: 46 **17** (EnergyBall, `:447`), 48 **3** (Lightning, `:512`), 49
   MonsterSkill (`:545`) -- store as the Balrog's is stored -- 52 **7** (Ice, `:642`). Sea Worm (50)
   is defined (`:568-594`) but spawned nowhere in this file; its row is harmless.
3. **Spawns: load `Atlans.cs`'s 337 monster rows into `monster_spawns`**, `map = 7`, ids from
   **930**, one row per `CreateMonsterSpawn(n, NpcDictionary[k], x, y)` at `:59-396`, each a
   one-tile box `x1 = x2 = x, y1 = y2 = y, count = 1` -- the tower's shape (448 one-tile rows,
   `docs/lost-tower-port.md` §4.2). Per kind: 45 x29, 46 x45, 47 x43, 48 x65, 49 x4, 51 x66,
   52 x85. Skip line 53 (`NpcDictionary[240]`, the Guard -- folk, step 6). A small script that
   regex-reads the `.cs` (`CreateMonsterSpawn\(\d+, this\.NpcDictionary\[(\d+)\], (\d+), (\d+)\)`)
   and inserts in one transaction, as the tower's 448 were loaded; check the count is 337 and
   that every row's kind is 45-52. A few spots may fall on rock: the realm's nearestOpen places them.
4. Checkpoint; `pipeline/index.py`; `tools/cook.py --world atlans --only tables`, and the other six
   worlds' tables (kinds are global in every `.mur`).
5. Figures one breed at a time (`--only figures --world atlans --monsters <Name>`); headless raises
   all, the window holds back the uncooked (`play_open.cpp:117-141`).
6. Folk: `FOLK_VERSION075[7]` (cook.py `:1900`): the same file's one Guard, 240 at 23,17,
   SouthWest (`Atlans.cs:53`).
7. Drops are level-generic; mu.db `items` tops out at drop level 75 (166 rows) -- covers OM's
   levels 43-74.

---

### 6. The build, in order

`$D` = `LEGACY/reference/MuMain/src/bin/Data`. Run from `MU2_BGFX/`. No window without asking;
`--mute` on any review run; `checks` = `cmake --build build --target checks`. Read `git diff` of
every file before staging (the Blood Castle session).

**Step 0 -- the per-world switches become MapRow fields** (no content, no behaviour change).
`maps.h:18-36` += `air`, `airEverywhere`, `grassy`, `underwater`; `maps.cpp:19-35` fills the six;
read at `play_open.cpp:32-37, 461-465`, `play_sound.cpp:104-108, 407-414`, `world.cpp:215`.
Check: `checks`; headless `--world dungeon|losttower|noria --ticks 2000 --seed 7` logs byte-identical.

**Step 1 -- bare land, reachable by `--world atlans` / `--travel-at`.**
`terrain.py` `HIDDEN_BY_MAP[7] = {22, 39}`, `BLEND_MESH_BY_MAP[7]`, `GRASS_BY_MAP[7] = []`,
`OPERABLE_BY_MAP[7] = {39}` (and index.py's); `index.py` `GATE_BOXES_BY_MAP[7]` and `[3] +=` (§1.1);
`python3 pipeline/terrain.py $D/World8 atlans source/world 8` (expect 0.00-3.83 m, 8 slots, 35.5%
walkable, 0.6% safe, 5215 placed / 40 kinds, none missing); `source/world/atlans/ground.json` with
`at_` sheets, slot 5 parked on a neighbour's sheet until step 3; `tools/content.sh --world atlans`;
sync; `--only ground`; mu.db gate 49; index.py; `--only tables`. `maps.cpp` row `{"atlans", 7,
{20, 17}}`, `arrival.cpp`, `minimap.cpp case 7`, `sheets/worlds/atlans.json` (a blue `dust_*`,
weak key, strong blue fill -- MU's shadows are 0.2). Check: `./run.sh --headless --world atlans
--ticks 200 --no-hand` logs `grid 256 ... map 7`; the `.mur` header's safe box reads 14,11,27,23
(zero is the gate-22 bug); `checks`.

**Step 2 -- the gates.** `gates.cpp` 45/46/47/48/49 (§4.1); `testAtlansGates`. Check: `checks`;
headless Noria `--at 241,241 --level 60` walk logs `Gated 45`, `--level 59` `Barred 60`.

**Step 3 -- the underwater ground and air.** Caustic strip + `u_caustic` in `fs_ground` (§3.1, new
lighting knob end to end); `world_water` (aWater.wav) and `player_step_swim` (pSwim.wav) in
`sounds.json`, index.py, `--only showing` (memory: new sound needs the showing cook); motes mode in
`Leaves` (World8/leaf01 as `mote`). Check: headless log names the air `world_water` and `leaves:
... mote`; `--budget --stats` on the bare land.

**Step 4 -- objects.** Pass B's worklist by count (Object26 951, 23 hidden, 07 475, 06 421, 34 328,
32 296, 25 246, 27 233 ...), `at_`-prefixed sheets, `cook_one.py ObjectNN --world atlans`, studio
sheets, then `--only textures/meshes/placements` (textures before meshes, memory); the 13 rigged
kinds keep their clips (`clips.json`). Check: `--budget --stats`; the sway's posed count and
`paletteRowsRefused()` on the kelp fields (§3.6).

**Step 5 -- bubbles.** `ANCHOR_KINDS_BY_WORLD["atlans"] = {"Object23": bubble}`; `bubble` sheet
(drop01) in the showing; `game/world/bubbles.*` (anchors + bursts from heads, deaths, Bahamut).
Check: the open line counts 845 anchors; effects account.

**Step 6 -- fish.** `Flight` fish mode and per-world count (40), `Scurry` model lists (10), cook
`AIRS`/`CRAWLS` as lists for Fish02-09. Check: `boids_test`; log line names 40 fish.

**Step 7 -- swimming.** Swim clips 11/24/33 on `FigureBody` beside the ride clips, chosen off the
safe zone on an underwater map; swim footsteps (Step 3's event). Check on the stage/bench (the
`--frames --shot --mute` review only when allowed).

**Step 8 -- monsters, from OpenMU Version075's `Maps/Atlans.cs`.** Load its eight kinds 45-52
(`:400-660`) and its **337 spawn rows** (`:59-396`, one-tile, count 1, map 7, ids from 930) into
`source/mu.db` (§5.2), checkpoint the WAL, `pipeline/index.py`, then `--only tables` for all seven
worlds; figures one breed at a time; `shoots()` row for the Valkyrie model; Hydra's beam (aqua) and
Vepar's Energy Ball shows; monster sounds in `sounds.json`; `FOLK_VERSION075[7]` with the file's
Guard (240 at 23,17, SouthWest). Check: `sqlite3 -readonly source/mu.db "select count(*) from
monster_spawns where map=7"` = 337; headless `--world atlans --ticks 30000 --level 70` reports
337 monsters; `checks`.

**Step 9 -- travel rows** (decision 1) and, for three rows, the multi-source `settleFound` (§4.3)
with a sim_test that standing near 226,51 opens "Atlans 2" and the Dungeon/tower floors still open
as before.

**Step 10 -- folk, music, mounts, drops** per the decisions.

---

### 7. Decisions for the user

1. **Warp rows**: none (0.75 has no Atlans row, and its Tab card then disappears), one "Atlans"
   (S6 4000 zen / lvl 70, or WZ's 75), or S6's three (Atlans 2 at 225,50 lvl 80, Atlans 3 at 62,157
   lvl 90) -- the last needs §4.3's flood fix.
2. **Caustics**: MU's only (additive frames on the 4 089 slot-5 overlay tiles), or ours over the
   whole floor and on objects and figures.
3. **Swim**: MU's swim clips off the safe zone (and the weapon slung), or keep our Mixamo run;
   whether +5 gloves matter (MU's fast swim) -- our run is free for everyone today.
4. **Music**: atlans.mp3 on the whole map (MU) or silence (the user's "music is rare" rule).
5. **Mounts**: forbid riding in Atlans as MU forbids the horns, or allow.
6. **Underwater sound room** (a muffled bus, ours) -- MU has none.
7. **Kelp cost**: if the 951 84-bone Object26 overdraw the palette or the frame, a shader sway
   instead of skinning, or fewer swaying.

---

### Side findings

- `pipeline/index.py:56-66` `GATE_BOXES_BY_MAP` still has maps 0-3 only: the Lost Tower's boxes,
  Devias's 28/44 and Blood Castle's 66 were never added (both earlier Part Cs asked).
- `pipeline/index.py:44-49` `OPERABLE_BY_MAP` has 0-3 only, while terrain.py's (`:317-322`) is the
  same set -- two copies to keep in step.
- `src/game/ui/travel.cpp:195-197`: a map in `kMaps` with no travel row vanishes from the Tab window
  (Blood Castle by design; Atlans by accident if decision 1 is "none").
- `src/sim/realm_travel.cpp:75-101`: the floor flood assumes one landing per connected region (§4.3).
- `source/sounds/sounds.json:576` says pSwim/snow were left out "until a map" -- Atlans is the map.
- `tools/bot/bot.cpp:67-71` knows five worlds; Blood Castle is not among them.
- An untracked 0-byte `MU2_BGFX/mu.db` is still there; the store is `source/mu.db`.
