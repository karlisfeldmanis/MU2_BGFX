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
| present | 5, 6 | 0.5 | ACES and sRGB, the HUD, the debug text |
| spare | — | 0.5 | unspent on purpose |

CPU: **3 ms** a frame, of which the sim gets 0.5 and the draw submission the rest.

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

## What the gate enforces, and what it only reports

The per-view timers on this Mac do not divide the frame — they count the gaps between
encoders and the wait for the drawable, and sum to five times the frame's own GPU time. So:

- **Enforced on every run**: the frame's own `gpuTimeEnd - gpuTimeBegin` against 5.5 ms, and
  the CPU against 3 ms. Both are measured directly and neither can be argued with.
- **Reported, not enforced**: the documented per-account allowances. `present` exceeds its
  share on every run because the drawable wait lands in whichever view presents; failing
  every run on that would make the gate noise.

## Overriding, which is how the gate is proved to fail

`--budget shade=1.0` is not a relaxation, it is **a claim the caller is making about this
run**, and it is checked whether the timers are coherent or not — against the account's
share of the measured frame. `--budget gpu=0.5` and `--budget cpu=1.0` claim the two figures
that are measured directly. An account name nothing recognises fails the run rather than
being ignored.

This matters because a gate that cannot fail is not a gate. Sprint 0's proving sentence was
"`--budget` fails when told a false budget", and for a while after sprint 1 it had quietly
stopped being true: the account check sat behind a condition that was false on every run, so
`--budget shade=0.001` passed. Proved again after the fix: an honest run exits 0, a false
claim on `shade`, on `gpu`, or on an account that does not exist all exit 2.
