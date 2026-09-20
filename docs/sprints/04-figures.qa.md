# Sprint 4 QA: the figures

**Verdict: SENT BACK.**

Reviewed at 078adce by QA, which did not write this code. The build is clean
(`cmake --build build --target mu2 cooked_test`, exit 0), `./build/cooked_test` reports
0 failures, `tools/posecheck.py --clip action15` reports 0 failures and the bind pose back
as the identity at 4.03e-06.

**What the sentence asks is largely there.** The Dark Knight stands in the Male10 plate set
on a rig that poses correctly — `DarkKnightBare` at `/tmp/qa_dkbare/00100.png` is a clean MU
character in an idle, and that picture alone retires the whole "a figure turned inside out"
family. The crowd stands and animates in the town (`/tmp/qa_shot/00089.png`), the twenty sway
models are back — Waterspout01's statue and its water are in `/tmp/qa_shotnf/00089.png`, the
fountain is not a point at the origin — and **every frame number in the Measured section
reproduced**, including the drawn/culled counts to the figure. The budget is kept.

What sends it back is animation correctness, not the frame: **every walk and run clip in the
content plays a cycle that is one interval short and pops once round**, the Skeleton Warrior
idles on a clip out of the wrong table, and the crossbow on one of the town's own fourteen is
drawn at the shin. All three are things the census predicted in words and the code then did
not do.

**A concurrent session was editing this tree throughout the review** — `src/sim/`,
`src/content/tables.*`, `src/content/grid.*` and `src/content/reader.h` appeared untracked, and
`tools/cook.py` and `src/content/cooked.cpp` were modified at 20:12 and 20:14 while the runs
were going. Everything above was read at 078adce and measured against a binary built from the
tree at 20:08; the clip analysis was done on the already-cooked `.muc` files. It is also the
other half of the load average.

**The machine was not quiet, and much less quiet than the author's.** `uptime` read load
average 20.92 at the start of the review and 25.40 when the timed runs began. Every figure
below is a median of three alternating 900-frame segments, Release, vsync off, 1080p, 4x MSAA,
no shots in the timed runs.

---

## Findings, most serious first

### 1. Every walk and run clip is missing its closing key, and the clock wraps as if it were there — the senior's

The cook keeps the extra key a looping clip carries and wraps the clock in `[0, duration)`
where `duration = t_last - t_first` (`tools/cook.py:590`, `crowd.cpp:111`). That is correct
for a clip whose last frame repeats its first. **The locomotion clips are not such clips.**

Read straight out of the cooked `.muc`, comparing frame 0 with the last frame over all
bones:

```
BullFighter01 action0 (Idle)  21 frames  max|first-last| 0.00000
BullFighter01 action1 (Idle2) 31 frames  max|first-last| 0.00000
BullFighter01 action3 (Atk1)  10 frames  max|first-last| 0.00000
BullFighter01 action2 (Walk)   7 frames  max|first-last| 0.19929   <-- does not close
player        action15 Walk male       0.03868   <-- does not close
player        action17 Walk sword      0.03600   <-- does not close
```

Scanned over the whole player library: **248 of 283 clips close the loop to 1e-4 and 35 do
not, and the 35 are precisely the locomotion set** — `action15`–`action22` (every Walk),
`action24` Walk swim, `action25`–`action33` (every Run), `action34`/`35` Fly, `36`/`37` Run
ride, `144` Walk two hand sword two — plus the deaths and a handful of emotes. The same
pattern holds across all six monster libraries: `action_keys` and the cooked frame count
agree exactly for `action2` on BullFighter01, Hound01, Lich01, Giant01, Spider01 and
BudgeDragon01 (7, 7, 7, 7, 5, 7), while every other looping slot is `action_keys + 1`.

The census generalised from one clip — *"Verified on the Bull Fighter: `action_keys` says 20
frames for action 0 and the glb has 21 keys"* — and action 0 is one of the clips that does
carry the key.

Two consequences, and the second is the expensive one:

- The cycle snaps from the last frame back to the first with no interval to do it in: the
  "idle that stutters once a cycle" the sprint named, landing on the walk instead.
- **`duration` is one interval short of the true cycle**, 6 intervals where the walk has 7.
  `action_travel` is metres *per cycle* — 2.4288 for the player's walk — so matching playback
  rate to move speed off this duration slides the feet by 1/6, about 17%. The census made
  `action_travel` a headline number and sprint 5 is the sprint that spends it.

The fix is a judgement (does the cook synthesise the closing key, or does the runtime carry a
"wraps to frame 0" flag and interpolate last→first over one extra interval?), so it is the
senior's.

### 2. The Skeleton Warrior reads its slots out of the player's table — the senior's

`figures.h:27` states the trap in full: *"Reading a monster's slots out of the player's table
labels its second swing 'Stop sword' and looks, at a glance, exactly like it is working."*
`figures.cpp:270` then does it: the monster branch unconditionally takes
`idleClip = library->find(0)` and `walkClip = library->find(2)`.

`SkeletonWarrior`'s mesh is `Skeleton01`, which has no animations of its own, so the cook
resolves it to the player library (`figures.json` `clip_of["Skeleton01"] = "player"`, 283
clips confirmed in the load line). Player slot 0 is **`Set`** and slot 2 is **`Stop female`**.
From `/tmp/qa_SkeletonWarrior/a.log`:

```
  Skeleton Warrior: Set (action0, slot 0) at 0.04 s of 0.32, 3 frames, loops, travel 0.000 m
```

A three-frame, 0.32 s character-creation pose is the idle of a monster that spawns in
Lorencia, and its "walk" is a woman standing still. The cook is not at fault — it labelled the
library out of `actions` because it *is* the player's, which is right — the runtime slot pick
is. Note this is the same row that `figure_set` goes out of its way to special-case for its
`glb`-as-a-LIST and `model_index -1`; the branch that was warned about was the parser's, and
the one that bit is the player's.

### 3. The crossbow is drawn at the shin, not in the hand — the senior's

`crowd.cpp:243` gives a bow or crossbow `paletteRow = -1`, so it draws against row 0, the
renderer's bind row of identities, and is then multiplied by the grip bone's world matrix.
With an identity palette the mesh's own 12-bone hierarchy is ignored entirely and the raw
bind-space vertices are used as they sit — which is only correct if those vertices are already
centred on the grip. They are not.

`/tmp/qa_CrossbowGuard/00120.png`: the guard is in profile, his hands are at hip height
(y ~ 650–700 px), and `CrossBow04` renders as the small red-and-blue object at y ~ 850–930 —
roughly 0.9 m below the grip, and rotated. For contrast, `Sword01` and `Shield10` on the same
rig in `/tmp/qa_dk2/00399.png` sit at the hand, and Giant01's two rigid axes
(`/tmp/qa_Giant01/00120.png`) are both correctly in its fists. So the rigid path is right and
the bind-row path is the broken one.

The `CrossbowGuard` is one of the fourteen figures the town places, so this is on screen in
Lorencia, not only in a bench. The sprint file lists "a bow's own clip" as owed; what is
actually owed is its *placement*, which the missing clip does not explain.

### 4. The player's deaths are never marked `hold` — the senior's

`cook.py:1036` passes `holds if embedded else set()`, and `embedded` is false for the player
library. `monster_holds` is `[6]`, a monster slot; the player's own deaths are `action232`
("Die 1") and `action233` ("Die 2"), and both are cooked with `hold = 0`. Confirmed in the
`.muc`: neither closes its loop (0.189 and 0.227 at the wrap) and neither holds.

So a player death will wrap — "a corpse half getting up as it falls", named in the sprint's
own list of the three things most likely to go wrong. Nothing plays it this sprint; it is
baked into the data sprint 6 will read.

### 5. The crossfade is proven only at t = 0, and it extrapolates out of a hold clip — the senior's

It *does* run: `ModelBench::openFigure` calls `play(found, true)` after `stand()` has already
started the idle, so `--figure X --clip N` crosses from the idle into the chosen clip. Tested
by shooting frame 0 of `--figure BullFighter01` with and without `--clip 2`: the two PNGs
differ in **237 bytes of 8 295 480** (i.e. they are the same picture), while frame 2 of the
same pair differs in 1 215 610. That proves `t = 0` selects the outgoing clip exactly. It
proves nothing about the middle.

**And the middle cannot be photographed with the tools as they stand.** `main.cpp:237` feeds
the whole frame's wall time into `deltaSeconds`, and a screenshot stalls that frame to about
250 ms — more than the 0.18 s fade. So the frame after any shot has already finished the
crossfade, and every shot after the first shows a clip a quarter-second further on than a
shotless run would. The stats deliberately exclude the shot frame; the animation clock does
not.

One concrete defect found by reading, unreachable this sprint but not later:
`Figure::update` clamps `time_` for a hold clip but never clamps `previousTime_`
(`crowd.cpp:104` vs `:118`, where the `fmod` is guarded by `!before.hold`). Crossfading *out*
of a death, `previousTime_` grows past `duration`, `sample()` computes
`where = time/duration*(frames-1)` beyond the last frame, clamps `frame` but not `t`, and
`nlerpQuat` is handed a `t` well above 1. For the Bull Fighter's 9-frame 0.582 s death that is
`t ≈ 3.5` — extrapolation, i.e. a limb thrown somewhere it never goes.

### 6. "The wall frame does not move" is not reproduced — the senior's

The sprint publishes the town alone at 2.485 ms wall and the crowd of 290 at 2.406, concludes
the frame is not GPU-bound at this size, and declines to read wall time. Three alternating
900-frame segments here say otherwise:

| | wall frame (median of 3) | the three runs | gpu frame (median of 3) | the three runs |
|---|---|---|---|---|
| `--no-figures` | **2.276** | 2.261, 2.278, 2.276 | **2.340** | 2.323, 2.351, 2.340 |
| `--crowd 30` (45 figures) | **2.365** | 2.445, 2.365, 2.263 | **2.550** | 2.532, 2.550, 2.594 |
| `--crowd -1` (305 figures) | **2.547** | 2.547, 2.587, 2.454 | **2.665** | 2.662, 2.665, 2.820 |

The 305-figure wall frame (2.454–2.587) does not overlap the no-figure wall frame
(2.261–2.278) in any of the six runs, and it rises in the right direction. The author's
inversion — the big crowd cheaper than the empty town — was his machine, and the conclusion
drawn from it ("wall time is measuring the pacing rather than the work") is not supported.
The *budget* conclusion survives unharmed: the crowd of 45 costs 0.21 ms of GPU here (author:
0.21) and 305 figures cost 0.33 (author: 0.38), both inside the 1.0 ms allowance, and wall
time agrees at 0.09 and 0.27 ms. The sentence in Measured needs rewriting, not the sprint.

Everything else in Measured reproduced exactly: 702 / 839 / 839 draws, `29 of 45 figures
drawn, 16 culled`, `137 of 305 figures drawn, 168 culled`, and the pose at 0.061–0.097 ms for
2082 bones and 0.383–0.410 ms for 12 666 bones.

Worth recording separately, and pre-existing: **the wall frame's p99 is over the 5.5 ms budget
in every configuration, figures or none** — 6.86 / 6.03 / 5.70 ms. The tail is not the crowd's
(it is *worst* with no figures) but the budget line says `5.500` and nothing fails.

### 7. The palette's 512-row ceiling is silent — the senior's

`addPalette` returns -1 when full, the caller draws that figure against the bind row, and
nothing says so. `./run.sh --world lorencia --crowd 600` places 615 figures and logs:

```
crowd: 615 figures, 25311 bones: 1 player, 14 of the town's own and 600 monsters
  crowd: 183 of 615 figures drawn, 432 culled, 25311 bones, pose 0.804 ms
120 frames in 1 segment(s), 0 errors
```

103 of those figures cannot have a row and stood in bind pose. No error, no warning, no
`paletteRowsUsed` in the frame line. Lorencia's own table names 290 and Noria is next; this is
a ceiling sprint 5 will reach with the lid on. Foundation 7's rule — *"a culling change that is
not visible in those two numbers did not happen"* — applies to the palette too.

### 8. `addPalette`'s clamped-rig path cannot be reached, and its comment says the opposite — the junior's

`renderer.cpp:307` explains that a rig over `kMaxBones` is clamped *"rather than refused: ...
a silent return of -1 would put the whole figure in bind pose and look like a missing clip"*.
But `Figure::pose` (`crowd.cpp:172`) does `if (count > kMaxBones) return 0;` before
`addPalette` is ever called, and `Crowd::gather` turns that 0 into `row = -1` — the bind row,
silently, with no log line. Both guards are 128, and `scratch_` is sized 128, so the clamp in
the renderer is dead code and the behaviour the renderer's comment rejects is the behaviour
that runs. Nothing in this content exceeds 60 (Storage01's 69 is the largest and it draws), so
this is a comment/code contradiction, not a live bug. One sentence says what the fixed code
must do: either `pose` clamps and logs instead of refusing, or the renderer's comment is
corrected to say a rig over 128 draws in bind pose and says so in the log.

### 9. The sprint file states a rule the cook deliberately does not keep — the junior's

*"The three things most likely to go wrong"* still says the extra loop key *"is dropped in the
cook, by the flag, and the proof is a frame count that matches `action_keys` exactly for all
seven of a monster's slots."* The cook does the opposite on purpose and documents why
(`cook_clips`'s docstring: the key is **KEPT** and the clock wraps). The frame counts are
`action_keys + 1` for six of the Bull Fighter's seven slots. The planned proof was never
produced and the paragraph now misdescribes the code. (It also, read against finding 1, would
have caught finding 1 — `action2` is the slot where the counts *do* match.)

### 10. The Dark Knight stands in the empty-hand stance holding a sword and a shield — the senior's

The census devotes a section to the stance table — sword (4, 17), two-handed (5, 18), spear
(6, 19), bow (8, 21), crossbow (9, 22), empty hands (1, 15)/(2, 16) — and notes that `female`
is read in that one row because every armed row is shared. `figures.cpp:237` implements only
the empty-hand row: `idleClip = idle.empty() ? find(female ? 2 : 1) : find(idle)`, and
`walkClip = find(female ? 16 : 15)`. Nothing looks at what is in the hands.

From `/tmp/qa_DarkKnight/a.log`:

```
  Dark Knight, Plate set: Stop male (action1, slot 1) at 0.14 s of 0.86, 7 frames, loops
```

Slot 1 is the bare-handed stance. In `/tmp/qa_dk2/00399.png` the consequence is visible: the
sword is held out sideways, horizontally, at hip height, because the arm is in the pose of a
man holding nothing. The figure the sprint's own sentence is judged by is standing in the
wrong stance. Whether this sprint owed the armed rows is the senior's call — the build list
says "each in its own idle" and the census says what the idles are — but the gap should be
written down either way.

### 11. `EliteBullFighter01` hides a primitive its own row does not name — the junior's (doc)

`index.json` gives `BullFighter01` `hidden_mesh: 0` and gives `EliteBullFighter01` none, and
the two share one glb. `cook.py` keys `hidden_of` by mesh with `setdefault`, so the elite's
copy loses primitive 0 as well. The cook comments the choice and it is probably the better
picture (the elite carries a Spear08 and would otherwise wear its built-in axe too), but it is
a departure from the data and it is unmarked in the sprint file. One line in the sprint file
naming it as an invention settles it.

### 12. A screenshot's stall is spent on the animation clock — the junior's

`main.cpp:246` keeps the shot frame out of the statistics; `main.cpp:237` does not keep it out
of `deltaSeconds`. A shot stalls the frame to about 250 ms and that quarter-second is then
advanced through the clips. Every shot after the first shows a pose a quarter-second ahead of
where a shotless run would be, and any transient shorter than the stall — the 0.18 s crossfade
first among them — cannot be caught in a picture at all. The fixed code must clamp or drop the
delta of a frame that took a screenshot.

---

## Checked and sound

- **The ORM fallback.** `Mesh::buildFromCooked` (`mesh.cpp:388`) takes `textures.neutralOrm()`,
  and `neutralOrm_ = solid(0xff00ffff)` is ABGR — blue 0, i.e. metal 0, as `conventions.md`
  has said since sprint 1. The glTF path (`mesh.cpp:143`) takes the same handle, so the two
  branches now agree. `textures.white()` survives only as the albedo fallback, which is what it
  was always for; nothing depended on the old behaviour.
- **`hidden_mesh`.** BullFighter01 draws one axe, not two (`/tmp/qa_bf_idle/00000.png`), and
  Hound01 is not wearing a quarter of itself twice.
- **The labels come out of the right table.** Every monster library is labelled from
  `monster_actions` (`action0 Idle`, `action6 Die`, hold 1) and the player library from
  `actions` (`action1 Stop male`, `action15 Walk male`). `action_travel` is per breed and
  correct: Spider 0.7277, Bull Fighter 2.4288, Hound 1.9935, Budge Dragon 1.7125.
- **Row 0 as the bind row, and no regression against sprint 3.** `/tmp/qa_shotnf/00089.png` at
  143,130 has the fountain, its statue, its water, the grass beds and the railings, all in
  place and none at the origin.
- **The grips, by name.** All thirteen monster bone names resolve — no `no bone named ... to
  hang ... on` line in any run, and Giant01's two axes, Lich01's staff (worn, not held, since
  it is skinned to the full rig), Hound01's sword and the Bull Fighter's axe all render at a
  hand. The one that does not is the crossbow, finding 3, and its cause is the bind row rather
  than the bone.
- **The pose's mean cost**, reproduced at 0.071 ms for 45 figures and ~0.40 ms for 305. Its
  99th percentile is still not a column in `--stats`; the sprint says so and it stays owed.
- **Load.** 51 meshes, 12 785 triangles, 14 libraries, 340 clips over 3 775 frames, in 0.04 s
  warm here.

## What would clear the verdict

Findings 1, 2 and 3 are correctness defects inside this sprint's own scope and are visible in
Lorencia or in the data sprint 5 reads. 4 and 5 are latent and cheap to fix now. 6 is a
sentence in Measured that the measurement does not support. The rest can land as the junior's
batch.
