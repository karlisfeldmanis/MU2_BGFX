# The budget

5.5 ms of GPU at 1920x1080 over Lorencia's town, with the play camera and a crowd, vsync
off, Release. That is MU2's studio target carried over, and it leaves room for 180 fps with
the CPU alongside it.

The accounts are enforced. `--budget` exits non-zero when one is overdrawn, and a sprint
that overdraws stops and says so rather than borrowing from spare.

| account | views | ms | what lives here |
|---|---|---|---|
| shadow | 0 | 1.0 | the sun's split, depth only, every caster |
| prepass | 1 | 0.7 | view normal and linear depth, and the depth the shade pass tests against |
| ssao | 2, 3 | 0.5 | half resolution, and the depth-aware blur |
| shade | 4 | 2.3 | the one lit pass: PBR, shadow lookup, sky reflection, AO |
| effects | 5 | 0.3 | the transparent pass: sprites, blended, sorted back to front |
| present | 15-18 | 0.5 | ACES and sRGB, the hover ring and its mask, the HUD, the debug text |
| probe | 19-61 | 0.3 | sprint 8c's reflection probe: a face, its chain and the filter, and the shade pass's cube read |
| spare | — | 0.2 | unspent on purpose |

The view numbers moved when the hover ring took 16 and 17 (`docs/sprints/09-the-ring.md`); they had also been stale in this table since sprint 6, which is why present read "6, 7" against a present view of 15.

**The grass owes this page nothing, and that is measured and not assumed** (`docs/grass.md`).
Lorencia's open field is its worst case, and there it measures **3.90 against 4.02 with the
field switched off** — two runs each, 400 frames, `--crowd 0 --no-figures`, 2026-09-23. The
field is FASTER than no field: MU's painted cards cover ground that would otherwise run two
blended material sets, a thirteen-tap PCSS lookup, an SSAO fetch and a lamp loop, and a card's
own shader is one texture read and a short list of multiplies. Treyarch measured the same thing
on Black Ops 4 for the same reason.

So no account gives anything up, and no account gains one either: a saving that depends on how
much of the frame is lawn is not an allowance anything can spend. `grass: 0` in the lighting
sheet is how it is switched off to be priced again.

A geometric-blade field was built first and measured at **+0.79 ms**, all of it fill. It was
replaced by the cards on that number. `docs/grass.md` has both tables.

**Those add to 5.8, and that is sprint 8c's doing, said here rather than hidden.** The probe
measured +0.28 ms of wall frame (below) and the accounts had no room for it: the spare is 0.2.
The frame it was measured in is 4.15 ms, well inside the enforced 5.5, so nothing fails; what is
owed is a decision on which account gives up 0.3, which is the user's to make. The shade
account's 2.3 is the likeliest, since the frame has never measured near it at 1080p.

Before sprint 8c they added to 5.5. **They did not before sprint 6**: this table said spare 0.2 while
`views.cpp` said 0.5, so the table summed to 5.2 against a 5.5 ms frame and the two had
disagreed since the file was written. The 0.3 the effects account now holds is what closed it.

## The 2K prices, 2026-09-24

Lorencia's square, the play camera still, 2560x1273 (a window on the 2560x1440 display),
Release, vsync off, `--repeat 3` of 600 frames, mean of means; the spread inside a launch was
0.006 to 0.06 ms on every row but the two marked. Each row is one thing switched off or
changed against the same 6.345 ms baseline, so a row is what that thing COSTS, not what
it is worth.

| change | ms | saved | picture |
|---|---|---|---|
| baseline | 6.345 | — | |
| `--shadow-noise screen` (5+8 turned taps for 9+16 still) | 5.437 | 0.91 | changes |
| `--msaa 1` | 5.720 | 0.62 | changes |
| `--shadow-size 2048` | 5.974 | 0.37 | changes; the cost is the 4096 map's cache misses in the PCSS taps, not its fill — at half scale 1024 measured the same as 4096 |
| bloom passes skipped (nine encoders) | 6.001 | 0.34 | changes; spread 0.14 |
| `sharpen` 0 (the present's four extra taps) | 6.024 | 0.32 | changes |
| `--no-lamps` | 6.097 | 0.25 | changes |
| `discard` compiled out of the prepass and the shade | 6.107 | 0.24 | breaks foliage; see below |
| `probe` 0 | 6.123 | 0.23 | changes |
| `grass` 0 | 6.205 | 0.14 | changes |
| `--no-figures --crowd 0` | 6.290 | 0.05 | changes |
| sun's split drawing the camera's 497 placements instead of all 2753 | 6.302 | 0.04 | none |
| `--no-air` | 6.329 | 0.02 | changes |
| probe faces drawing the camera's list instead of all 2753 | 6.482 vs 6.424 | none | none; spread 0.2 |
| `--scale 0.5` | 3.178 | 3.17 | changes |

**What that says.** About 2.1 ms of the frame does not scale with pixels (the half-scale row)
and the rest is fill: PCSS taps, MSAA, lamps, the sharpen's taps, the probe read. The
geometry is not the cost: the sun drawing five times as many placements as it needs is
0.04 ms, and the plan's foundation-7 caster cull is worth that and no more on this GPU.
Nothing on the list is both free of the picture and worth a tenth of a millisecond, so at
2560x1440 native, 4x MSAA and the still shadow taps, 180 fps is a choice among the rows
marked "changes" -- `--scale` being the one already taken -- and not something a
quality-neutral change reaches. Fullscreen is 2560x1440, 13% more pixels than the window
these were taken in.

**The discard row is not a saving.** A fragment program that can `discard` is denied its
early depth resolve on this GPU, and twin programs without the discard were built for
every material that never cuts. They measured no different (13.0 vs 13.0 ms on the
3456x1894 panel the display had become by then, twice each, interleaved). The 0.24 was the
discards that actually run on the foliage cards -- 18% of the draws and 43% of the instances
-- and the picture needs those. The twins were taken out again; `Renderer::submitBatches`
says so.

**Two things found on the way and fixed**, neither of them frame time: a fading figure cast
no shadow at all in play (the sun's list dropped every instance with a fade under 1, so the
dither in fs_shadow was never reached), and `Renderer::draw` grouped the frame's three
thousand drawables into vectors built fresh each frame, against foundation 7's "no
allocation"; they live on the renderer now.

**The shine (`14-the-shine.md`) costs nothing measurable.** The hero at 148,142 in a +9 Plate suit
with a +7 axe and a +5 shield, against the same save with every plus at 0, 2560x1273, timers
off, `--repeat 3` of 600: 6.658 ms against 6.654, spreads 0.010 and 0.026. The ladder is a
branch on a per-instance number in the shade pass and two small samples on +7 pixels, with no
draw added. The bag's picture taken every frame while something in it shines added 39 draws
and moved nothing past noise at 1080p (docs/sprints/14-the-shine.md, step 7). The baseline is
above the table's 6.345 because it is another spot on another day's town; only the difference
is the price.

**The gleam's lights are not free.** A +7 or +9 hero's gear is a light source at night (fx/
gleam.h, ours): up to two of the four moving lights, each a line along the suit or the blade,
and the chrome brightened. With a +9 suit and a +9 Blade on the field east of town, at night
**+0.29 ms at 2560x1273** (7.113 and 7.115 against 6.820 and 6.829, spreads 0.015 or less) and
**+0.18 ms at 1920x1080** (5.044 against 4.864, inside the 5.5 budget with 0.46 to spare); at
noon nothing, the lights being off. Every lit pixel runs the moving-light loop once there is
one. `kMaxLights` in gleam.h is the knob if the frame needs it back.

**bgfx runs single-threaded here** (`bgfx::renderFrame()` before init, for the preloader's
worker), so its `waitRender` counter is always 0 and cannot split the frame into CPU work and
GPU wait. Tried and taken out the same day. Letting bgfx run its own render thread measured
the same at 2K (6.34 and 6.31 ms against 6.31 and 6.93) and 0.08 ms faster at half scale:
the frame is the GPU's, and a render thread costs a frame of latency, so it stays off.

### bgfx wrote back tile memory nothing read, 0.25 ms at 2K, patched 2026-10-02

Apple's GPUs render a pass in on-chip tiles and write each attachment back to memory at its
end. bgfx's Metal backend asked for every write back: all four samples of the shade pass's
RGBA16F colour beside its resolve, and the D32F depth after the shade and transparent pass,
which nothing reads again. `patches/bgfx-metal-store-actions.patch` (applied by bootstrap.sh)
plans the frame from its sort keys and drops a resolved MSAA colour's samples, and a
write-only depth, after the last pass that binds them. The prepass's depth is still written
back, since the shade pass loads it; its MSAA_SAMPLE normals are untouched, since SSAO reads
them. `MU2_BGFX_KEEP_STORES=1` turns it off for an A/B in one binary.

Lorencia 140,126 `--still`, 2560x1273, vsync off, `--repeat 3` of 600, interleaved, load ~14:

| | launch 1 | launch 2 |
|---|---|---|
| patched | 7.399 | 7.337 |
| `MU2_BGFX_KEEP_STORES=1` | 7.591 | 7.608 |

**About 0.23 ms**, spreads 0.08 or less. Shots at frame 120 differ only on what moves (the
crowd, the fountain); walls and ground are pixel-identical. The estimate before measuring was
1-2 ms of bandwidth: the GPU hides most of a write back behind the next pass's work.

### The view timers were 0.77 ms of every frame, and are off by default now

Found the same afternoon in a Metal System Trace (attached to a running game with
`xctrace record --attach`; launched by xctrace, the game cannot read under Documents). The
transparent pass was an encoder of its own at 774 us, with the 4x MSAA colour and the depth
stored by the shade pass and loaded straight back. The cause is bgfx's Metal backend: with
`BGFX_DEBUG_PROFILER` set it ends the render pass at every view so it can time each one
(`renderer_mtl.cpp`, `|| profileViews`), where otherwise it keeps one pass per target. The
window set that flag on every run, the game included.

| 2560x1273, `--repeat 3` | timers on | timers off |
|---|---|---|
| first pair | 6.322 | 5.592 |
| second pair | 6.315 | 5.498 |
| after the switch landed | 6.388, 6.385 | 5.642, 5.650 |

The two pictures differ only in the frame-rate counter's digits. The timers are now on only
with `--views`, `--stats` (its csv has a column a view) or a named `--budget NAME=MS` claim;
a bare `--budget` enforces the wall frame and runs without them. **Every number on this page
before this section was taken with the timers on**, so each is about 0.75 ms high at 2K and
proportionally less at 1080p; the differences between rows stand, except where a row adds or
removes whole passes (the bloom row's 0.34 is partly per-pass overhead the timers caused).

The trace's own division of the frame, for the next hunt: shade and sprites 3.0 ms of
fragment work, shadow 0.59 fragment and 0.63 vertex, the present 0.54, SSAO 0.33 and its blur
0.21, the prepass 0.24 fragment and 0.46 vertex, the first bloom level 0.20, a probe face
0.15 and 0.23, and every other pass under 0.06.

## The probe account, and the measurement that set it

Sprint 8c, `docs/sprints/08c-the-metal.md`. Lorencia's town, 1920x1080, Release, vsync off,
the moving camera, 600 frames x 3, `probe` 1 against 0 in the sheet, twice each:

| | mean of means |
|---|---|
| probe on | 4.152, 4.148 ms |
| probe off | 3.877, 3.855 ms |

**+0.28 ms.** Taken apart before the every-other-frame rule, with each part switched off in
turn: the faces 0.25, the chain nothing measurable, the filter 0.04 (it runs one frame in
seven), and the shade pass's cube read about 0.22. The per-view timers cannot price any of
it, for the reason the next section but one gives.

## The effects account, and the measurement that set it

Sprint 6 built the transparent pass with the account at **0.0** and measured it before
writing a number here, because the plan forbids publishing one taken before the thing was
measured on its own worst case.

Measured on Lorencia's town at 1080p, Release, vsync off, 400 frames, three runs each, with
`--effects N --effect-size M --effect-sheet blood`. The figure is the **median wall frame
time**, which is the one enforced number:

| sprites | half-extent | frame ms | over baseline |
|---|---|---|---|
| 0 | — | 2.52 | — |
| 64 | 0.5 m | 2.52 | 0.00 |
| 128 | 1 m | 2.70 | +0.18 |
| 256 | 1 m | 2.96 | +0.44 |
| 32 | 4 m | 2.83 | +0.31 |
| 16 | 8 m | 2.59 | +0.07 |
| 128 | 2 m | 3.44 | +0.92 |
| 8 | 30 m | 2.54 | +0.02 |
| 64 | 30 m | 3.71 | +1.19 |

**The cost is fill rate and not sprite count, exactly as the sprint file predicted.** Eight
sprites 60 m across — each far larger than the screen — cost 0.02 ms, while 128 sprites two
metres across cost 0.92. What the two expensive rows have in common is not how many sprites
they hold but how many screens of blended pixels they cover: both work out at about sixty
full screens, and both land near a millisecond. Across the whole table the rate is about
**0.018 ms per full screen of blended overdraw at 1080p**, and that single number predicts
every row in it.

So the account is **0.3 ms**, which buys about sixteen full screens of overdraw. MU's fight
does not come close: a blood burst is ten particles scaled to the target, and a spider is a
metre. The rows that would overdraw this account are not effects, they are mistakes — a
sprite scaled in metres where it should have been scaled in units of the target, which is
the trap the sprint file names.

**It is paid for out of the spare, which goes from 0.5 to 0.2.** That is a deliberate,
recorded reallocation with the measurement above beside it, and it is the thing the working
rules allow; what they forbid is an overdrawn sprint quietly helping itself to the spare.
The numbers above are the whole argument, and if the user would rather the spare stayed at
0.5 the effects account has to come out of `shade` instead — which cannot be justified until
the per-view timers below can be believed.

The noise floor deserves stating: three runs of the same configuration spread by up to
0.17 ms, so the +0.18 and +0.07 rows are at the edge of what this method can see. The +0.92
and +1.19 rows are far above it, and the rate derived from them is what the account rests on.

## The per-view GPU timers cannot price this pass, and that is now demonstrated

Measured on the town at 1080p with the transparent pass **drawing nothing at all**: the
`effects` account reported a median of 2.577 ms and a p99 of 4.783 ms. An empty view cannot
cost 2.577 ms. The log already says why on the line underneath — the view timers summed to
15.866 ms inside a 3.550 ms frame — and this is the cleanest demonstration of it yet,
because here the true answer is known to be zero.

Whatever a view timer is measuring on this backend, it is encoder gaps as much as work, and
it is useless as an account for a pass this small. **The effects account will be priced by
the frame's own wall time with the pass full against the same scene with it empty**, which
is the one number this file already says is enforced. The share column stays readable; the
median per view does not become the evidence.

**4x MSAA's cost is not yet honestly measured.** The figures first published here (1x 2.88 ms,
2x 3.26, 4x 3.15, 8x 3.29) were GPU medians, and wall frame time was flat at 2.11/2.17/2.15/
2.19 ms across the same four settings — so throughput does not corroborate them, and the
ordering 4x < 2x does not survive a longer run. Since then the prepass stopped being resolved
at all (the SSAO reads one sample itself), which changes the cost again. It is re-measured on
the town in sprint 2, in wall time, where geometry rather than resolution decides it.

The accounts above are a division of GPU work and are **advisory**: they say where the time
is meant to go, and the share column says where it went. The frame's wall time is what
actually fails a run. Of that frame, the sim gets 0.5 ms once it exists.

## How it is measured

`--stats <file.csv>` writes a row a frame: frame number, CPU ms, GPU ms, draws, and a
column per view's GPU time from bgfx's own view timers (the `BGFX_RESET_PROFILER` flag is
on for this). At the end of the run the median and the 99th percentile of each account go
into the log.

**The median is what the account is judged on; the 99th percentile is read but not
enforced.** A tail is a hitch and gets hunted on its own terms — MU3 priced animation at
about 0 ms on average while the tail was the whole story.

The first 30 frames are dropped from every summary: they hold the pipeline compiles and the
first upload of everything.

## The one enforced number is the wall time of a frame

**Mean wall frame time, 5.5 ms, which is 180 fps.** That is the goal stated as the thing that
delivers it, and it is the only figure here measured without a GPU timer that counts waiting.

Two things follow, and both were learned the hard way.

**It is a mean, never a median.** Frame time on this machine is not one distribution: it is
two. Measured over 370 frames, 184 of them submit in about 0.24 ms and the other 186 wait
about 3.9, with *nothing at all* between 1.0 and 2.4 — the frames alternate between one that
submits and one that waits for the drawable. A median of that lands in the empty gap and
flips between identical runs: sprint 1 published 1.824 ms and 640 fps, and two back-to-back
re-runs of the same command measured 2.254 and 1.538 ms, 489 fps and 1486. The mean is stable
across the same runs to within 2%. The summary names both humps so nobody has to rediscover
this.

**The GPU figure is reported and not enforced.** `gpuTimeEnd - gpuTimeBegin` is larger than
the wall time of the frame it sits inside, which is impossible for work alone — it counts
waiting, exactly as the per-view timers do. It is useful for comparing one change against
another in the same run, and it is not a budget.

## How to measure, and the noise floor that decides what is measurable

**Most of the time, one run.** The usual question is "is this inside 5.5 ms", and the answer
is rarely close enough to need a second opinion: `--frames 600 --world lorencia` answers it
in a few seconds and the gate exits non-zero if it does not. Do not repeat a run that already
said 2.4 against 5.5.

**`--repeat 3` when two configurations have to be compared**, and never several launches. A
launch spends about twenty seconds decoding and mipping a world's textures to measure a
second and a half of frames, so a dozen launches is four minutes of loading for eighteen
seconds of data. `--repeat` measures that many segments in one process with the world loaded
once, prints each segment's mean and the spread across them. Three is enough to see whether
a difference clears the spread; six was measured once, to establish the figure below, and
there is no reason to pay for it again.

**The spread is the number that says what is measurable at all.** Measured once, six
segments of the same 600 frames, same process, same camera, nothing changed between them:

    segment 1  2.202 ms      segment 4  3.975 ms
    segment 2  2.286 ms      segment 5  2.305 ms
    segment 3  2.645 ms      segment 6  2.429 ms
    6 segments: mean of means 2.640 ms, spread 1.773 (2.202 to 3.975)

**And the spread is itself not one number.** Six further invocations of the same
`--repeat 6` gave spreads of 0.203, 0.306, 0.350, 0.975, 2.054 and 2.072 ms — an order of
magnitude apart, the wide ones caused by a single segment landing near 1.4 or 3.5. So the
floor on this machine is **somewhere between 0.2 and 2.1 ms**, and which one a given
afternoon hands you is not knowable in advance.

That is the rule: a claimed difference of a tenth of a millisecond is not a difference, it is
this. Anything below the spread needs interleaved paired segments and a consistent sign, and
even then the magnitude is not worth publishing. This project put a difference in a sprint
file with the sign reversed twice before this line existed.

### The spread is hitches, not variance, and the mean of means is ten times finer

Re-measured immediately afterwards, on the same build and the same camera, with one
difference: **the machine was quiet.** The 1.773 ms above was taken while the texture cook
held six to seven cores at 600–750% for the better part of an hour, and the fifteen-minute
load average over that window was 14.7 against about 5 now.

Four launches, six segments each:

| launch | mean of means | spread inside the launch |
|---|---|---|
| 1 | 2.028 | 0.942 |
| 2 | 2.014 | 2.258 |
| 3 | 2.106 | 0.368 |
| 4 | 2.074 | 0.233 |

**The spread moves between 0.23 and 2.26 ms while the mean of means holds inside 0.09 ms.**
That is the shape of *occasional stalls*, not of noisy frames: one launch logged a single
frame at 1003 ms of CPU, and one such frame in 570 is worth about 1.7 ms of that segment's
mean on its own. A hitch lands in some segments and not others, so the spread measures
whether a hitch happened, and the mean over six segments largely absorbs it.

So, in order of what to trust:

- **Mean of means over six segments resolves about 0.1 ms**, measured across four launches.
  That is what an A/B comparison uses, and it is ten times finer than the spread suggests.
- **A spread above about 1 ms means a hitch landed in that launch** — read it as a warning
  about the machine, not as an error bar. Worth re-running rather than reasoning around.
- **Nothing is measured while something else is compiling, cooking or indexing**, which
  includes this project's own cook, another agent's build, and the virus scanner working
  through whatever the cook just wrote. The load average is part of a measurement's
  conditions and belongs beside it.

The original 1.8 ms figure is not withdrawn — it was correctly measured and it is what this
machine does under load. It is simply not the floor when nothing else is running.

## What the gate enforces, and what it only reports

The per-view timers on this Mac do not divide the frame — they count the gaps between
encoders and the wait for the drawable, and sum to five times the frame's own GPU time. So:

- **Enforced on every run**: the mean wall frame time against 5.5 ms. Nothing else.
- **Reported, not enforced**: the documented per-account allowances. `present` exceeds its
  share on every run because the drawable wait lands in whichever view presents; failing
  every run on that would make the gate noise.

## Overriding, which is how the gate is proved to fail

`--budget shade=1.0` is not a relaxation, it is **a claim the caller is making about this
run**, and it is checked whether the timers are coherent or not — against the account's
share of the measured frame. `--budget frame=2.0` claims the enforced figure and
`--budget gpu=2.0` the diagnostic one; both read the mean. An account name nothing recognises fails the run rather than
being ignored.

This matters because a gate that cannot fail is not a gate. Sprint 0's proving sentence was
"`--budget` fails when told a false budget", and for a while after sprint 1 it had quietly
stopped being true: the account check sat behind a condition that was false on every run, so
`--budget shade=0.001` passed. Proved again after the fix: an honest run exits 0, a false
claim on `shade`, on `gpu`, or on an account that does not exist all exit 2.

## The sweep

What the sweep cannot tell yet, and the improvements it needs before it can drive
optimisation that keeps the picture: `docs/perf-sweep.md`.

Found missing on 2026-10-01: the Lost Tower was over budget, and that was known only because
somebody measured it by hand at tiles they picked (the safe hall 205,79 at 5.29 ms, the lava
163,40 at 5.54, the Balrog's room 31,209 at 6.31 mid-fight and 6.05 peaceful with the timers
on, the Dungeon's 120,229 at 4.47; 1920x1080, `--repeat 3`). `tools/perfsweep.py` walks a world
for its bad places instead.

    python3 tools/perfsweep.py --world losttower --preset light            # ~2.5 min a world
    python3 tools/perfsweep.py --world losttower,dungeon --preset light --update-baseline
    python3 tools/perfsweep.py --world lorencia --preset full              # occasional
    python3 tools/perfsweep.py --report build/perfsweep/<run>              # re-read, no launch

**Light** is the hand list in `tools/perfsweep_spots.json` (town squares, every travel arrival,
the boss rooms, the five tiles above), three passes of 90 settling and 400 measured frames a
tile. Run it after a batch that touches rendering, effects or a world's content. **Full** adds
every standable tile on a 16-tile lattice, read by the game off the cooked attribute grid (a
wall or the void is never measured), one pass of 300 frames: about 135 tiles on the Lost Tower,
a few minutes a launch.

**How it measures.** Two launches a world, each loading it once: a fight launch (the map's
monsters, the hero at `--level 400` so he stands, hitting back at what comes within six tiles)
and a `--peaceful` one. The difference is the `monsters` column, what the fight costs on that
tile apart from the room. Inside a launch the game's `--sweep SPOTS --sweep-out ROWS` mode
(`src/app/sweep.cpp`) puts the hero down on each tile with the realm's own `setHeroDown`, no
warp drawn or heard, settles, measures, writes one JSON row, and goes round the list again for
each pass. Vsync off, muted, 1920x1080 by default; the resolution printed is the log's
`Metal, WxH`, never assumed. The numbers are the same wall frame `--budget` enforces, means
and never medians, and a tile's figure is the mean of its pass means; its spread is the pass
means' max minus min.

**What a row holds**: mean, p99, worst frame, GPU (diagnostic, as above), draws, triangles,
the sim's milliseconds a tick, the most monsters awake, the mean within 12 tiles, blows traded,
kills and deaths, and the worst frame while settling after the put-down with what caused it
(the arrival: figures and sheets met for the first time).

**Hitches.** A frame over twice its tile's mean is written with what was logged during it
(a texture or mesh read, a shader, a warning) and the realm's happenings of any tick that ran
in it or the frame before -- spawns, deaths, blows begun, casts, traps, landings, named by
breed. Every hitch with a cause is listed. A 2-4x frame with nothing logged and nothing
happening is counted per tile, not listed: on this Mac it is the drawable wait landing twice,
the second hump of the distribution above, and there are a handful in every 400 frames. A frame
over 250 ms is a machine stall (the ~1 s frame this page already knew about); its pass is left
out of the tile's mean when another pass can stand for it, and the report says so.

**Reading the report** (stdout ranked worst first, `report.md` and a heat map per world and
launch in `build/perfsweep/<run>/`). Flags, in the order to read them:

- `launch-shift`: the whole launch moved -- 80% of tiles the same way past 0.3 ms against the
  baseline, or peaceful slower than fight on most tiles, which monsters cannot cause. The
  machine changed under it (a scanner, an indexer, another session's build or bot). Re-run
  before believing any tile. It was raised on the run that taught it: the Dungeon's peaceful
  launch at 6.2-6.9 ms against 4.18 by hand, under a VS Code indexer at load 13.
- `slower`: a tile past its baseline by more than the larger of the two spreads, never under
  0.1 ms, and 0.2 when a single pass gave no spread. Exit status 1. Inside a shifted launch it
  is `slower-launch` and is not counted.
- `over`: a tile's mean over 5.5 ms. A fact about the world, not a regression.
- `new-hitch`: a hitch cause, numbers stripped, that the baseline never saw on that tile.

The baselines are `tools/perf/baseline_<world>.json`, merged tile by tile by
`--update-baseline`, with the commit, resolution and load average they were taken at. Take one
on a quiet machine; `run.json` and the report name what else was running.

**First numbers**: the baselines, 2026-10-01, 1920x1080, light, mean of 3 passes, taken at
load 15 (another session's bot, Defender and VS Code's indexer); the confirming run at load 21
landed every tile within 0.19 ms of them and flagged nothing:

| tile | fight | peaceful | by hand |
|---|---|---|---|
| Lost Tower, Balrog's room 31,209 | 5.84 | 5.30 | 6.31 mid-fight, 6.05 peaceful with timers |
| Lost Tower, lava 163,40 | 5.25 | 4.96 | 5.54 |
| Lost Tower, safe hall 205,79 | 5.09 | 4.75 | 5.29 |
| Dungeon 1, 120,229 (lands on 120,230) | 4.34 | 4.18 | 4.47; 4.31 and 4.18 again that evening |

The Balrog's room is the one tile of the fourteen over budget, and peaceful at 5.30 it is the
room before it is the monsters (+0.55 ms of fight). Its blows (8-10 a pass) caused no hitch.
The sweep reads 0.2-0.3 ms under the hand numbers of the afternoon; by hand that evening, the
Dungeon's tile read the same as the sweep, so the difference is the day, not the method.

**The ~1 s frame.** Three of the first runs held one frame of 1004-1008 ms, nothing logged and
nothing happening, on a different tile each time -- the same length every time, which is a
timeout's shape rather than a load's. CAMetalLayer's `nextDrawable` waits up to a second; that
is a guess, not checked. The sweep leaves such a pass out (above), so it moves no baseline.
