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

## Overriding

`--budget shade=1.0` replaces one account's allowance for that run. It exists so the gate
can be proved to fail, and so a sprint can tighten an account on itself before the code is
there to keep it.
