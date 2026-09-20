# Sprint 4: figures

**Proved by:** a Dark Knight in armour and 30 animated monsters in the town, inside budget.

**Planned** 2026-09-20, before any code, from the rig, the clip libraries and the placement
list rather than from a guess — the same posture sprint 3 was planned in. Sprint 3 is not
built yet; nothing here touches its files, and the gate below says what has to be true of it
before this sprint starts at all.

## The gate, stated before the work

Sprint 2 left the bare land at **3.075 ms still, 4.085 ms moving**. Sprint 3's own gate is
the bare land at or under **1.5 ms** after chunking, with the town placed on top of that.
This sprint's number is not knowable until sprint 3 lands, so the gate is written as a rule
and not as a figure:

- **If the finished town, moving, costs more than 4.5 ms, sprint 4 does not start.** It says
  so, and sprint 3 is reopened. A crowd cannot be fitted into 1 ms of a frame that has 1 ms
  left, and discovering that after the bodies are in is how the measurement gets argued with
  instead of obeyed.
- **The crowd's own allowance is 1.0 ms of GPU** over the same camera with the crowd removed
  — more geometry through shadow, prepass and shade, and nothing else new.
- **The pose costs at most 0.5 ms of CPU** for 31 figures. The frame has 3 ms of CPU and the
  sim takes 0.5 of it in sprint 5. MU3 priced animation at about 0 ms on the average frame
  and found the whole story in the 99th percentile, so the tail is read here too even though
  the mean is what fails the run.

## What the data turned out to be

### One rig, 283 clips, and they are not in the models

- **The 15 player parts carry a 60-bone skin and no animation at all** — 7.3 MB, 3625
  triangles, 39 images between them. That is deliberate: `pipeline/export_actions.py` took
  the clips out because fifteen parts were shipping fifteen byte-identical copies, 13.4 MB
  of each 13.7 MB file, and five parts of a character parsed in 2.05 s against 81 ms without
  them.
- **The clips live in `build/players/rig/player.actions.glb`**: 13.42 MB, **283 clips**,
  60 joints, a skin with no mesh on it. Across all 283, **3012 frames**; the longest is 101
  frames and 12.5 s, the shortest 0.27 s.
- **Every clip's channels share one key-time list** — all 283, checked — and the channels are
  **`translation` and `rotation` only, no scale**, touching 50 of the 60 joints. So a flat
  per-frame table is exact: nothing has to be resampled, and no sampler search survives the
  cook.
- **Baked flat that is 8.67 MB** as three `vec4` rows a bone a frame, or 5.06 MB kept as
  quaternion plus translation. Either is nothing; the reason to bake is that it removes the
  accessor indirection, not that it saves space.
- **It was never copied into `assets/`.** `tools/sync.py:126` looks for the library by
  convention, as `<stem>.actions.glb` beside the model. The real pointer is in the glb's own
  glTF `extras`: `{"actions": "../../rig/player.actions.res"}` on a player part,
  `"../../../players/rig/player.actions.res"` on a worn item, `"../Female01.actions.res"` on
  an NPC part. So `assets/players/` holds `body/` and nothing else, and **not one clip of the
  283 is in this project**. The fix is a sentence long — read `extras.actions`, resolve it
  against the glb's directory, take the `.glb` twin of the `.res` — and it is the first thing
  this sprint does, because everything after it is invisible without it.
- The `.res` is Godot's `AnimationLibrary` and is of no use here. The `.glb` beside it is
  `export_actions.py`'s own output and is what this engine reads.

### Worn parts share the rig exactly

`ArmorMale10`'s joint list is **identical in name and in order** to `ArmorClass02`'s — 60
names, `Bip01` first, checked rather than assumed. So dressing a character is swapping which
meshes draw against one set of bone rows, not loading a second body. All 45 armour item files
are skinned to that rig.

### A weapon is not skinned, except when it is

| kind | files | skinned | what it is |
|---|---|---|---|
| swords, axes, maces, spears, shields | 45 | no | rigid, placed at one bone's transform |
| bows and crossbows | 5 | to **their own 12–13 bone rig**, with one clip of their own | the string |
| staffs | 4 | to the **full 60-bone player rig** | a worn part, not an attachment |

Sending a rigid weapon down the skinning path binds its vertices to every bone whose name
happens to match and draws it stretched across the character; `Model.cs:740` records this as
the mistake that reads as a fault in the model.

### The grips are named bones, and the grip is identity

`knife_gdf` (right) and `hand_bofdgne01` (left) on the player rig, `Bone05` between the
shoulders for a slung item. They sit where a grip belongs rather than at the wrist, so there
is no correction to derive — and MU2 records three different derivations of the correction
that the wrong bone needs. Monsters name their own: `knife`, `shield`, `left_bone`,
`knife_bone`, `hand_bone01`, `Bone_left`, `knife_gdf`, carried per monster in `index.json`
as `right_hand_bone` / `left_hand_bone`. **All 13 of those names were checked against their
own model's node list and all 13 are there.** By name and never by index, which is what
survives a rig re-exported with a bone inserted.

### Monsters carry their own everything

14 files, 20.6 MB, 6581 triangles, 36 images, **7 clips each** named `action0`…`action6`,
27 to 50 joints — the Chain Scorpion has 9 and the Lich 50. Two exceptions worth knowing
before they bite:

- **`Skeleton01` has 60 joints and no clips**, and its `extras` point at the player library.
  A monster on the player rig.
- **`EliteBullFighter01` and `EliteGoblin01` are the same glb as their plain versions**, at a
  different scale with a different weapon. Loading by name rather than by file loads them
  twice.

**A monster's clip numbers are not a player's.** `action4` is `PLAYER_STOP_SWORD` on a
character and `MONSTER01_ATTACK2` on a spider, and both files call the clip `action4`.
`index.json` carries both tables — `actions` with 286 entries and `monster_actions` with 12 —
and reading a monster's slots out of the player's table labels its second swing "Stop sword"
and looks, at a glance, exactly like it is working.

### Speed, travel, and the key that is not a frame

- **MU's play speed is three ordered passes** (`pipeline/monster_speeds.py`): every action
  gets 0.25; a multiplier scales models 3, 5, 25, 37 and 42 **on every action but the
  death**; then a handful of walks are **set outright**, an assignment that discards the
  multiplier rather than compounding with it. The order is not commutative and applying the
  multiplier to the death makes the Giant take four seconds to fall over. This is already
  resolved into the exported clips' own durations — it is written down here so the cook does
  not apply it a second time.
- **`action_travel` is metres per cycle**: 2.4288 for the player's walk, and per monster
  0.7277 for the Spider, 2.4288 for the Bull Fighter, 3.4012 for the Stone Golem. That is
  what matches playback rate to move speed instead of sliding the feet, and sprint 5 needs
  it as much as this one does.
- **A looping clip was written with one extra key holding the first pose**, so the wrap has
  an interval to happen over. A clip that *holds* has none. `monster_holds` is `[6]` — the
  death. Verified on the Bull Fighter: `action_keys` says 20 frames for action 0 and the glb
  has 21 keys. A flat bake that counts that key as a frame plays the first pose twice; a
  death that wraps is a corpse half getting up as it falls.

### The stances are a table, and one row asks who is standing in it

From `Clips.cs`, which took it from the client's own `PLAYER_` enum: sword (4, 17),
two-handed sword (5, 18), spear (6, 19), scythe (7, 20), bow (8, 21), crossbow (9, 22),
wand (10, 23), and empty hands **(1, 15) for a man and (2, 16) for a woman**. Every armed row
is shared — a crossbow is held one way whoever holds it — which is why `female` is read in
that row and in no other.

### The town's own figures

**14 NPC placements**, the ones sprint 3 left standing as gaps:

- **10 built from character parts** — 5 `BerdyshGuard`, 1 `CrossbowGuard`, `LumentheBarmaid`,
  `PotionGirlAmy`, 2 `WanderingMerchant`. The guards wear the `Male10` plate set and carry a
  `Spear08` or a `CrossBow04`; the three townsfolk are `npc/body` parts on their own smaller
  rigs (37–48 joints) with **2 clips each**, in `Female01/Man01/Girl01.actions.glb`.
- **4 standalone models** — `Smith01`, `Wizard01` and two `Storage01` — 6 files, 6.2 MB, with
  2 to 3 clips of their own inside the glb.

### Items are the load time again

106 item files, **148.5 MB and 310 images for 10 010 triangles**. The figures in `index.json`
reach exactly **21 of them, 29.7 MB and 63 images**. The cook takes what is reached and
nothing else — the same rule sprint 3's 105 world models are cooked under.

### Sprint 3's trees arrive here

20 world models carry a skin and one clip, 331 placements, drawn in bind pose in sprint 3 on
the condition that nothing in the instance path assumes an instance is static. They go
through this sprint's bone texture unchanged, and they are the cheapest possible test of it:
one clip, no blending, no stance.

## What this sprint builds, in this order

1. **The clip library reaches `assets/` at all.** `sync.py` reads `extras.actions`, resolves
   it against the glb, and copies the `.glb` twin. Proof: `assets/players/rig/` exists and
   holds 13.42 MB.
2. **The skinned vertex layout and the bone texture.** `SkinnedVertex` is the 48-byte
   `Vertex` plus four `uint8` joints and four normalised `uint8` weights, 56 bytes; 60 bones
   fit in a byte with room. One `RGBA32F` texture, **three rows a bone**, the skin matrix
   already multiplied by the inverse bind as `conventions.md` states. **The instance carries
   its first-bone offset**, so the one instance buffer sprint 3 built is read unchanged by
   shadow, prepass and shade, and a skinned draw differs from a static one by a shader flag
   and an integer. 31 figures at 60 bones is 5580 texels and one upload a frame.
   `conventions.md` gains the layout, the `uvec4`-joints-on-Metal line it already warns
   about, and the row order, in the same commit as the code.
3. **The cook bakes the clips flat.** Per clip: a frame count, a duration, the loop-or-hold
   flag, the travel, and `frames × bones` of local rotation and translation — no scale, no
   key times, no accessors, because the key times are uniform and the frames are what is
   left. Versioned, beside the mesh, read whole.
4. **The pose, composed in local space.** Two frames, `nlerp` the rotations, lerp the
   translations, one walk of the hierarchy, one multiply by the inverse bind, three rows
   written. **Local space and not model space**: blending two model-space poses is cheaper
   and skips the hierarchy walk, and it slides a limb through the body when the two poses
   differ by much — over MU2's 0.18 s crossfade it would mostly hide, which is what makes it
   the wrong kind of cheap. The crossfade is MU2's `BlendSeconds = 0.18`, traced, and it is
   short because a walk cycle is under a second.
5. **A character is one skeleton and five worn parts.** Wearing is swapping which meshes draw
   against the same bone rows — the skeleton, the clip and the things in the hands do not
   move. Weapons hang off the named bone with an identity grip; a staff is worn, not hung.
6. **The crowd, instanced by model.** A figure is culled by sprint 3's chunks and casts by
   sprint 3's sun rule, with no figure-shaped exception. Drawn and culled counts stay in the
   log for both passes.
7. **`--bench monster`.** One figure on the bench ground, a clip chosen by number, named out
   of the right table of the two, and the clock — position *and* length — printed every
   second. A clip that was started and froze on its first frame reports the same name as one
   that is running, and that difference is most of what goes wrong with an animation and
   none of it is visible in a still.
8. **The 14 NPCs stand in the town**, each in its own idle.

## What is deliberately deferred

- **The crowd is placed and animated, not alive.** Spawn tables, AI, pathing and walking to
  somewhere are sprint 5; the 30 monsters here stand and idle where the bench puts them.
- **Equipment as items** with requirements, drops and a bag: sprint 7. This sprint wears the
  dummy sets `index.json` already names.
- **Attacks, deaths and flinches as events** hung off a landing cue: sprint 6. The clips are
  all here and playing one is a bench command, not a game rule.
- **The bow's own clip** and the weapon-on-back arrangements (`WeaponOnBack`, `ShieldOnBack`,
  `QuiverOnBack`, `CrossbowOnBack`, and the Skull Shield's exception) — listed as owing, and
  they belong with the items.
- **Hair, capes, blend shapes.** None exist in this content.

## The three things most likely to go wrong

Written before the code, as sprint 2's and sprint 3's were.

- **The missing clip library reads as a rig bug.** A body whose clips never arrived loads
  cleanly, stands in bind pose and animates nothing, and the first instinct is to go looking
  at the skinning. The log says how many clips each figure found, on the line that says it
  loaded, so the answer is on screen before the question is asked.
- **The extra loop key becomes a frame.** It is one key in 3012 and it makes an idle stutter
  once a cycle and a death half stand up. It is dropped in the cook, by the flag, and the
  proof is a frame count that matches `action_keys` exactly for all seven of a monster's
  slots.
- **A figure faces the wrong way and still looks right.** A model looks down **+z**, so a
  figure's yaw under `bx::mtxSRT` is the **negative** of the direction of travel's angle —
  MU4's trap 4 — and `bx::mtxFromQuaternion` writes the **inverse** rotation into bx's
  layout, which is why `core/maths` owns the only quaternion-to-matrix in the project. Both
  are already in `conventions.md`; both are about to be exercised for the first time, on
  every bone of every body, where being wrong looks like a bad export.

## What was built, in the order above

**The gate was checked first and it passed.** Sprint 3 left the town at **2.262 ms** with the
cook wired through, moving, against the 4.5 ms that would have stopped this sprint. The
crowd's own allowance is the 1.0 ms of GPU the gate names.

1. **`sync.py` reads `extras.actions`.** The convention it had — `<stem>.actions.glb` beside
   the model — matched **nothing at all**, which is why not one clip of the 283 was in this
   tree. The model names its own library in its glTF `extras`, relative to its own directory,
   and the `.res` it names is Godot's `AnimationLibrary` with the `.glb` this engine reads
   beside it. Four libraries arrived, 13.7 MB: the player's 283 clips and Man01, Female01 and
   Girl01 with 2 each.
2. **The skinned vertex and the bone palette.** `SkinnedVertex` is the 48-byte `Vertex` with
   four joint bytes and four normalised weight bytes, 56. The palette is one RGBA32F texture,
   a row a figure and three texels a bone, and which row a figure occupies rides in the
   instance data — so the one instance buffer sprint 3 built is read unchanged by shadow,
   prepass and shade, and a skinned draw differs from a static one by its program and an
   integer. `docs/conventions.md` gained the layout, the transpose and the local-space rule
   in the same commit.
3. **The cook bakes the clips flat.** `.muc`, version 1: a frame count, a duration, the
   loop-or-hold flag, the travel and `frames x bones` of local rotation and translation. The
   player library is **283 clips, 3012 frames, 5.07 MB** — the census predicted 5.06 — and
   nothing is resampled, because every channel of every clip shares one key-time list at MU's
   own rate. `.mum` gained a version 2 for a skinned mesh; version 1 is untouched, so the
   town's 105 models were not re-cooked and their vertices did not grow by eight bytes for
   joints they do not have.
4. **The pose is composed in local space**, two frames nlerped per bone, one walk of the
   hierarchy, then the inverse bind, then the transpose the shader reads. The crossfade is
   MU2's `BlendSeconds = 0.18`, traced.
5. **A character is one skeleton and five worn parts**, and a staff is worn rather than held:
   an item skinned to the full player rig joins the parts, and anything else hangs off
   `knife_gdf` or `hand_bofdgne01` by name. A bow carries its own 12-bone rig and is drawn in
   bind pose against a shared row of identities — its own clip is owed with the items.
6. **The crowd**, in Lorencia's own spawn mix, posed whether or not it survives the cull,
   because the sun's pass draws what the camera's does not and both read the one palette.
7. **`--bench monster`** is `--figure NAME [--clip N]`, and it prints the clip's name, its
   slot, **where its clock stands and how long the clip is**, once a second.
8. **The 14 NPCs stand in the town**, each in its own idle, placed by `placementTransform` —
   the same MU `AngleMatrix` the town's own placements were corrected to.

### Two checks that are not the screen

- **`tools/posecheck.py`** composes the rest pose with the same conventions `core/maths.h`
  uses and checks it against the inverse bind. The bind pose must come back as the identity,
  and it does: **worst 4.0e-06 on the player rig, 1.2e-05 on the Bull Fighter's**. A
  transposed quaternion, a reversed multiply or the inverse bind on the wrong side each show
  here as a number rather than on screen as a figure turned inside out.
- **`cooked_test`** gained the figure half: that a skinned `.mum` is not also a static one,
  that every vertex's four weights sum to 255, that no bone stands before its own parent,
  that every clip's frames lie inside the pose array, and that a baked rotation is a unit
  quaternion.

## What went wrong, and both of them were mine

**The cook started writing the town's sway models as skinned, and they vanished.** Twenty of
Lorencia's 105 models carry a skin and one clip — `Tree01`, `Tree11` (165 placements),
`Sign01`, `Curtain01`, `StreetLight01`, `House04`, `Carriage01`, `Waterspout01` and the rest,
**331 placements between them**. Sprint 3 cooked them as static meshes and drew them in bind
pose. The moment `cook_mesh` learned about skins they became version 2, and the town — which
has no poses and passes `paletteRow = -1` — sent them down the skinned path with a row index
outside the palette. Every bone came back as zeroes and each of the 331 collapsed into a
point at the origin. **The user found it by looking at Lorencia and asking where the fountain
had gone**, which is `Waterspout01`: one of the twenty.

The fix is a rule rather than a patch: **row 0 of the palette is the bind row**, written every
frame as identities, and a skinned drawable naming no row of its own draws against it. That
restores sprint 3's picture exactly, and it is also the right answer for a bow, whose own
12-bone rig has no clip in this sprint. `-1` never reaches the shader now.

**A missing ORM map was read as white, which is metal 1.0.** `docs/conventions.md` has said
since sprint 1 that a material with no ORM takes occlusion 1, roughness 1, **metal 0** — and
`Mesh::buildFromCooked`, the path every cooked model actually loads through, used
`textures.white()`. White's blue is metal 1, a metal surface has no diffuse, and **39 of the
316 real materials in this content have no ORM**: the grass, seven trees, both merchant
animals, the fire lights, the candles, the carriage's horse. They had been drawn as mirrors
since sprint 3 and it was read as "PBR looking glossy". The glTF path six lines above had
always been right, which is exactly how it survived: the rule was kept in the branch nothing
runs.

## Measured

`./run.sh --world lorencia --at 140,126 --still --frames 900 --repeat 3`, Release, vsync off,
1080p, 4x MSAA. **The machine was not quiet** — a second session was building and running
throughout, load average 3 to 11 — so the wall figures below carry more spread than sprint
3's and the small differences between them are not resolved.

| | wall frame | gpu frame (median) | draws | figures drawn |
|---|---|---|---|---|
| the town, no figures at all | 2.485 ms | **2.348** | 702 | — |
| and the Dark Knight and the town's own 14 | 2.517 ms | — | 751 | 1 of 15 |
| and 30 monsters in Lorencia's spawn mix | 2.497 ms | **2.556** | 839 | 29 of 45 |
| and all **290** the spawn table names | 2.406 ms | **2.725** | 839 | 137 of 305 |

**The wall frame does not move and the GPU frame does**, and the wall figures are not the
number here: over three alternating runs the town alone measured 2.485, 2.485, 2.519 and the
crowd of 290 measured 2.445, 2.397, 2.375 — *lower*, which no amount of extra geometry can
be. At this size the frame is not GPU-bound, so wall time is measuring the pacing rather than
the work. The GPU frame median is coherent and is what the crowd is charged:

- **the crowd of 45 figures costs 0.21 ms of GPU**, against the 1.0 ms the gate allowed it;
- **all 305 figures cost 0.38 ms**, which is Lorencia's whole population at once.

**The pose costs far less than its account.** From the log, over the same runs:

| figures | bones | pose, CPU |
|---|---|---|
| 15 | 821 | **0.026 ms** |
| 45 | 2 082 | **0.071 ms** |
| 305 | 12 666 | **0.377 ms** |

against the 0.5 ms the sprint allowed for 31 figures — so a crowd ten times the size fits
inside the allowance, and the per-figure cost is 1.6 microseconds. That is the mean; MU3's
lesson was that animation's story is in the tail, and the tail is not read here because the
99th percentile of the pose is not yet a column in `--stats`. **It is owed**, and it is the
one number this sprint claims without having measured its spread.

**The palette.** 128 bones a row, 512 rows, RGBA32F: 3.1 MB resident, and what is uploaded is
the rows written — 6 KB a figure, so 0.27 MB a frame for 45 figures and 1.8 MB for 305. The
rig sizes that forced 128: `Storage01`, which stands in the town, has **69** bones, and the
lobby's faces have 100 to 115.

**Load.** 51 figure meshes, 12 785 triangles, 14 clip libraries with **340 clips over 3 775
frames** and 6.01 MB of `.muc`, read in **0.11 s** warm and 0.60 s cold. The cook itself took
53 minutes, almost all of it BC7: 132 images, 50.6 MB in, **140.5 MB of blocks with mips**
against 421.4 MB of RGBA8 at the top level alone. It is idempotent now — an image whose
`.ktx` is already there is not compressed again, because the file's name carries the hash of
its source bytes and its role.

**What is drawn.** A Dark Knight is 10 primitives and 782 triangles across 7 meshes sharing
one palette row; 30 monsters in Lorencia's mix are another ~16 000 triangles. Of the 45
figures at the fountain, **29 survive the camera's frustum and 16 are culled**, and every one
of the 45 is posed, because the sun's pass draws what the camera's does not.

## Still owed out of this sprint

- **The 99th percentile of the pose**, as a column in `--stats` rather than a line in the log.
- **The crowd is placed, not alive**: a spiral about the camera's focus, not the spawn
  rectangles. Sprint 5 owns that, and the breeds and their weights are already read from the
  map's own table.
- **A bow's own clip**, and the weapon-on-back arrangements. Listed as owing since the plan.
- **The town's twenty sway models still draw in bind pose.** They have one clip each and now
  go through a palette that could play it; what they lack is a per-placement clock, which is
  331 more palette rows and a decision about whether a tree's sway is worth them.
