# Sprint 0: foundations

**Proved by:** `./run.sh --frames 300 --shot 100 --stats s.csv` leaves a PNG, a log and a
csv, and `--budget` fails when told a false budget.

**Done**, 2026-09-20.

## What is here

- **The layers** of `PLAN.md` as directories: `core` (log, args), `gfx` (window, views,
  stats, bgfx callback). `content`, `sim` and `game` are empty until they have something
  to hold.
- **The build.** `bootstrap.sh` fetches bgfx, bx and bimg pinned to the revision MU4 was
  measured against, plus cgltf and stb_image. `build.sh` configures Ninja once and builds
  Release, with ccache when it is installed; `--debug` and `--trace` build beside it, and
  `--trace` is the one that shows Metal's pipeline refusals. A cold build is about 130
  translation units, nearly all of them bgfx's, and under a minute; a rebuild of our own
  code is a second or two.
- **The log** (`core/log`), opened before the arguments are parsed so a complaint about
  them is not lost, keeping one previous run beside it, counting its own errors so that a
  run's exit code can carry them.
- **The review loop** (`main.cpp`, `gfx/stats`): `--frames`, `--shot`, `--shot-path`,
  `--log`, `--stats`, a frame line a second, and a summary table. Paths on the command line
  must be absolute and are refused otherwise, because a shot written relative to a working
  directory nobody set is a shot nobody finds.
- **The budget gate** (`gfx/views`, `docs/budget.md`): the accounts, their allowances,
  `--budget` to enforce them and `--budget name=ms` to replace one. Exit 0 kept, 2
  overdrawn, 1 for bad arguments or a failed start.
- **The six views** exist and are submitted from the first frame, ids fixed, each charged
  to its account. Sprint 1 fills them.
- **`tools/sync.sh`** and `sync.py`: MU2's three copy rules, run over its real build.
- **`docs/conventions.md`**, written before the first loader.

## Measured

`./run.sh --frames 300`, Release, vsync off, 1920x1080, seven empty views:

| account | median ms | p99 ms | budget |
|---|---|---|---|
| shadow | 0.131 | 0.395 | 1.0 |
| prepass | 0.138 | 0.346 | 0.7 |
| ssao | 0.197 | 0.669 | 0.5 |
| shade | 0.069 | 0.337 | 2.3 |
| present | 0.125 | 0.668 | 0.5 |
| **gpu total** | **0.256** | 0.681 | 5.5 |
| **cpu** | **2.44** | 6.79 | 3.0 |

fps 440 median. **These account figures are not work**, they are the per-view timers' own
granularity: they sum to 0.66 ms while the frame's total GPU is 0.256. What the run proves
is that the wiring reaches from a view id to a number in a table; sprint 1 is the first
measurement that means anything.

**The CPU number is the one to watch.** 2.44 ms a frame with nothing drawn, against a 3 ms
allowance, and a 6.8 ms tail. That is bgfx's Metal encoding and the present, and MU4 found
the same: its CPU side (2.4 ms) was the ceiling, not its GPU. If it does not come down, the
frame is CPU-bound before the game exists. Sprint 1 measures it again with real draws
before anything is concluded — an empty frame is not a fair sample, and a window nobody
watches may be throttled differently from one on screen.

## The dry run of the sync

756 files, 522 MB, of MU2's 1.9 GB build: 325 models and 269 images. Not yet verified
complete — sprint 1 and 2 prove that by loading what they name.

## Found on the way

All four are now in `docs/conventions.md`. The bgfx pinned here is newer than most writing
about it, and its `Init` has no `resolution`, its `reset` takes a `SwapChain`, its per-view
timing needs `setDebug(BGFX_DEBUG_PROFILER)` rather than a reset flag, and it appends no
extension to a screenshot path.

**And one thing the plan had wrong:** `PLAN.md` and the first draft of the conventions said
a tile was one metre. It is 100 units — `units_per_tile: 100.0` on every world in
`index.json`, and MU2 calls a unit a metre, so its Lorencia is 25.6 km on a side. MU4's 1 m
per tile was its arena's own scale. Every distance, light range and speed in MU2's numbers
is in these units, and none of MU4's transfer.
