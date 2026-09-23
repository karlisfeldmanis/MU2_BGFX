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
