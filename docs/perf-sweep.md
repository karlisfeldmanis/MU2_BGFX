# The performance sweep: what exists, and what it still needs

Written 2026-10-01, the day the sweep landed (c2873ff1). How to run and read it is in
`docs/budget.md`, "The sweep". This file is the other half: what the tool can and cannot
tell us yet, and the improvements it needs before it can drive optimisation that **keeps the
picture exactly as it is**.

## Why it was started

Performance problems were found by luck. The Lost Tower turned out over budget on 2026-10-01
only because somebody happened to run `--budget` at tiles they picked: the safe hall 5.29 ms,
the lava 5.54, the Balrog's room 6.31 mid-fight, the Dungeon 4.47 (1920x1080, vsync off,
`--repeat 3`). The budget is a mean wall frame of 5.5 ms, 180 fps. Nothing said where else a
world might be slow, whether a batch had made a place slower, or what caused a hitch.

## What exists now

- **`mu2 --sweep SPOTS --sweep-out ROWS`** (`src/app/sweep.cpp`). With the world loaded once,
  the hero is put down on each tile in turn (`Play::setDown`, no warp drawn), settles, is
  measured, and one JSON row a tile is written: mean, p99, worst, GPU, draws, triangles, the
  sim's ms a tick, monsters awake and near, blows, kills, and every frame over twice the mean
  with what was logged on it (`core::logTap`) and the realm's happenings of its tick. No die is
  drawn and headless is untouched.
- **`tools/perfsweep.py`**. Two launches a world, with monsters and `--peaceful`, so the room
  and the fight come apart. `light` is the hand list in `tools/perfsweep_spots.json` (towns,
  arrivals, boss rooms, ~2.5 min a world); `full` adds every standable tile on a 16-tile grid
  read off the cooked attribute grid. A ranked stdout list, `report.md` and a heat map a launch
  in `build/perfsweep/<run>/`.
- **Baselines** for the Lost Tower and the Dungeon in `tools/perf/`. A run flags tiles over
  5.5 ms, tiles slower than their baseline past the launch's common drift, a whole launch that
  moved (the machine, not the code), new hitch causes, and ~1 s stalls.

## What it found on its first day

| tile | fight | peaceful |
|---|---|---|
| Lost Tower, Balrog's room 31,209 | 5.84 | 5.30 |
| Lost Tower, lava 163,40 | 5.25 | 4.96 |
| Lost Tower, safe hall 205,79 | 5.09 | 4.75 |
| Dungeon 1, 120,229 | 4.34 | 4.18 |

1920x1080, mean of 3 passes, at load 15; a second run landed every tile within 0.19 ms.

- **The Balrog's room is the one tile over**, and it is the room more than the monsters: 5.30
  with none on the map, the fight adds about 0.55. It has the most draws of any tile measured
  (about 900 against about 500 in the hall).
- **The Lost Tower draws about twice the Dungeon's triangles** (1.5-1.8M a frame against
  0.9M) and runs about 0.5 ms slower with no monsters. Across its tiles the frame tracks
  triangles (r 0.71) more than draws (r 0.62). A hint that it draws much it does not show;
  not yet a finding, on 14 tiles and a busy machine.
- **No hitch could be put down to a cause.** The Balrog's blows made none. The frames over 2x
  are the drawable wait's second hump. Three runs each held one frame of 1004-1008 ms with
  nothing logged; a timeout's shape, perhaps Metal's one-second `nextDrawable` wait (unchecked).

## What it cannot do yet

The sweep says **where** a world is slow, never **why**, and it says nothing about the
picture. That is not enough to optimise without losing quality:

1. **No cost per part.** A tile's 5.84 ms is one number. Which share is the shadow, the lamps,
   MSAA, the probe, the grass, the geometry, the CPU, cannot be read off it. The per-view GPU
   timers cannot give it either (`docs/budget.md`: they count encoder gaps and cost 0.77 ms).
2. **No picture check.** A change can make a tile faster by drawing less, and the sweep would
   call that an improvement. Nothing compares what the tile looked like before and after.
3. **No count of what is drawn against what is seen.** Triangles and draws are totals. Whether
   the tower submits geometry that is off screen, behind a wall or past its range is unknown.
4. **Noisy conditions.** Every number so far was taken at load 9-21 (Defender, VS Code's
   indexer, another session's bot). The tool says so, but the baselines are as good as the
   afternoon they were taken on.
5. **Two worlds of five.** Lorencia, Noria and Devias have hand tiles but no baseline, and no
   world has a full grid baseline.

## The improvements, in the order they pay

### 1. Every speed-up must keep the picture: a shot per tile

The sweep takes one shot per tile at a fixed moment (`--fixed-dt`, the same settle, the same
seed, the hero facing one way), written beside its row and kept as a reference with the
baseline. A run that claims a tile got faster also compares its shot against the reference,
byte for byte as `tools/shotcheck.py` already does for its three pinned scenes. A faster tile
with a changed picture is reported as **"faster, picture changed"** and is not an
optimisation until the user has looked at the pair. A faster tile with the same pixels is a
quality-neutral win, the only kind this file is after.

Taken in the settle and outside the measured frames, because a screenshot stalls its frame
(`application.cpp` keeps such frames out of the statistics for that reason).

### 2. Cost per part: the ablation preset

`--preset ablate` re-runs the hot tiles with one thing switched off at a time, each its own
launch, and subtracts. The switches mostly exist: `--no-lamps`, `--msaa 1`,
`--shadow-size 2048`, `--no-air`, `--no-figures`, `--scale 0.5` (what does not shrink with
pixels is the CPU and the geometry), and `grass 0` and `probe 0` through a lighting sheet
written for the run. The result is the 2K price table in `docs/budget.md` -- done by hand once
for one Lorencia tile -- for every hot tile in every world:

    Balrog's room: shadow 1.1, shade 2.0, lamps 0.4, MSAA 0.6, monsters 0.55, CPU 1.2 ...

(illustrative, not measured). Each row is marked with whether its switch changes the picture,
so the table separates what can be cut freely from what is a trade the user decides. About
8 launches a world, 15-20 minutes; worth it only on a quiet machine.

### 3. Drawn against seen

The sweep logs, per tile, the camera pass's and the sun's placements and triangles submitted
against those whose bounds are on screen or in the split, and how many sit behind the camera,
past their range, or under the floor (the tower's floors stack: a floor below may be drawing).
Over-submission is the cheapest kind of win -- nothing on screen changes -- and the Lost
Tower's triangle count says it is the first place to look. The plan's foundation 7 names
chunk culling and per-kind ranges; this checks whether a world's content keeps to them.

### 4. One trace at the worst tile

A Metal System Trace (`xctrace record --attach`, the way `docs/budget.md` took the 0.77 ms
timer finding) at the Balrog's room, fight and peaceful. It divides the GPU frame by pass
honestly, which settles items 2 and 3 for that tile and checks the ablation's arithmetic.

### 5. Trustworthy numbers

- Take baselines only when the load average is under about 4 and nothing else holds a core;
  the tool refuses `--update-baseline` above a threshold unless forced.
- 600 measured frames rather than 400 on the hot tiles, and six passes for a baseline, which
  `docs/budget.md` measured as the point where the mean of means resolves 0.1 ms.
- Settle the ~1 s stall: if it is the drawable timeout, it is the window's and not the frame's,
  and the sweep can say so instead of guessing.
- Baselines for Lorencia, Noria and Devias, and one full-grid baseline per world, so the heat
  maps cover the whole of a map and not 14 dots.

### 6. Then optimise, one change at a time

With 1-5 in place a change is judged by three numbers the tool gives together: the tiles it
made faster past their noise, the tiles it made slower, and whether any picture moved. A
change lands when the first is non-empty, the second is empty and the third is "none". The
candidates already in view: the Lost Tower's geometry (item 3), the Balrog's room's draws, and
whatever the ablation table puts at the top of the worst tiles.

## Not the goal

Changes that trade the picture for time (`--scale`, `--msaa 1`, the shadow's taps, fewer
lamps) are listed by the ablation so the cost of each is known, but they are the user's
decisions, not optimisations. This work is about finding the time the frame spends on things
nobody sees.
