# Architecture

Where code goes, and what may reach what. `docs/conventions.md` is the other half of this
pair and answers a different question: it holds the rules that have one right answer and a
silent wrong one — the axis swap, the matrix layout, the colour space. **This page holds the
shape.** Read this one to find out where a new file belongs; read that one before writing a
line inside it.

The rule this page exists to keep is enforced rather than reviewed: `tools/layercheck.py`
reads every `#include "..."` under `src/` and fails the `checks` target on an arrow that is
not on the diagram below. It used to be a sentence in `CMakeLists.txt` and nothing else, and
a sentence is not a gate.

## The layers

```
                        app          the application object and its modes
                         |           nothing depends on app: it is the top
                         v
                       game          the play loop, the world, the figures,
                         |           the windows, the effects
            +------------+------------+
            v            v            v
          sim           gfx       content        sim: the realm and the rules
            |            |            |          gfx: bgfx, the passes, the overlay
            +------------+------------+          content: what the cook wrote
                         v
                      content
                         |
                         v
                       core          log, args, json, files, maths
                                     depends on nothing
```

| layer | may include | never includes | what lives there |
|---|---|---|---|
| `core` | — | everything else | log, args, json, files, maths |
| `content` | `core` | `sim`, `gfx`, `game`, `app` | cooked meshes, textures, ground, tables, the grid |
| `sim` | `core`, `content` | `gfx`, `game`, `app` | realm, tick, rules, items, route, market, audit |
| `gfx` | `core`, `content` | `sim`, `game`, `app` | window, views, renderer, effects, overlay, interface |
| `game` | `core`, `content`, `sim`, `gfx` | `app` | play, world, town, figures, the windows, the effects |
| `app` | everything | — | application, modes, bootstrap |

Three of those arrows are load-bearing and each one was bought:

- **`sim` never includes `gfx`.** Foundation 9 is *"the sim runs without a window"*, and
  `sim_test` links `sim` with no renderer at all to prove it. One include of `gfx/renderer.h`
  in `sim/` and that test stops linking — which is the good outcome; the bad one is a realm
  that cannot be stepped headless and a seeded run that can no longer be reproduced on a
  machine with no display.
- **`gfx` never includes `sim` or `game`.** A renderer is handed `Drawable`s and draws them.
  One that reads the realm for itself cannot be pointed at the model bench, the viewer or a
  cook sheet, and those three are how every material in this project is judged.
- **`content` never includes `sim`.** The cook's formats are read the same way by the game,
  by `cooked_test` and by `cookcheck`. A reader that needs a realm to parse a file is a
  reader that can only be tested by playing.

`sim` *does* include `content`, and that one is deliberate rather than tolerated: the tile
grid is cooked into the `.mur` because the sim reads it for routing and has no PNG decoder.
See `content/grid.h`, which is the one definition of "blocked".

Subfolders inside a layer — `game/ui/`, `game/fx/` — are there to keep a folder legible and
add no arrows. `layercheck.py` resolves a file to its **top** folder, so `game/ui/hud.cpp` is
`game` and may include anything `game` may.

## Inside a layer

### `core/` — no dependencies

`log`, `args`, `files`, `json`, `maths`. The test of whether something belongs here is
whether it would be equally at home in a different game. `maths.h` is the one place a
quaternion becomes a matrix and the one place a bone palette is transposed; see
`docs/conventions.md`, "Matrices".

### `content/` — what the cook wrote, read back

Meshes, textures, the ground, the placement transform, the tables, the cooked `.mur` reader,
the showing's table, the missiles. It knows file formats and owns no rules. `content/grid.h`
is the single definition of "blocked", shared with the sim's router.

`tools/cook.py` and `tools/cook/texcook.cpp` write what this layer reads, and `cookcheck`
reads it back with a second implementation. When the cook's output changes, three places move
together: the cook, this layer's reader, and `tests/cooked_test.cpp`.

### `sim/` — the realm

The tick at a fixed 20 Hz on its own accumulator, the rules, combat, items, the market, the
router, the audit. Every constant is traced to OpenMU's Version075, MuMain's own C++ or
`mu.db`, with the source named in a comment, and a departure is marked `invention` on the
line that makes it — `docs/conventions.md`, "Rules".

It allocates nothing in a step and never reads the frame's delta.

### `gfx/` — the frame

The window, the eight views, the renderer and its passes, the effects pass, the overlay, the
interface's plates, the stats. The eight view ids are fixed so the budget accounts line up;
the table is in `docs/conventions.md`, "The frame", and `docs/budget.md` holds what each is
allowed to cost.

### `game/` — where the three meet

The largest layer, and the one this refactor split into rooms:

| folder | what it holds | why it is one room |
|---|---|---|
| `game/` | play, figures, crowd, save, sound, bench, headless, item_models, frustum | what the other three rooms are all built on |
| `game/ui/` | hud, panel, bag, shelf, card, desk, describe, vitals, cursor, outline, arrival, stage, items_stage, browser_list | everything drawn flat over the frame, and the pointer's own feedback |
| `game/fx/` | showing, aura, breath, bones, meteor, marker, litter | what a blow, a death, a level and a drop look like |
| `game/world/` | world, town, lamps, ornaments, sway | the town standing there: its chunks, its lights, its own animation |

Two of those placements are worth their reasons:

- **`item_models` is not in `ui/`** although the bag and the shelf are its loudest callers. The
  drops lying on the grass — `fx/litter.cpp` — draw the same meshes, and the point of the store
  is that a sword seen in the bag and the same sword on the grass are one mesh read once.
  A store two rooms share belongs in neither.
- **`sway` is in `world/`, not `fx/`.** It is MU's `MoveObject` playing a clip on twenty placed
  models — the trees, the inn sign, the fountain's spout. It is the town animating itself, not
  something that happens to a body.

`game/play.h` carries the rule the whole layer is built on, and it is worth repeating here:
**it reads the sim and never second-guesses it.** Nothing in `game/` decides whether a blow
lands, where something may stand or who is fighting whom. It asks where a body is, draws it
there, and hands clicks back the other way. What it owns is presentation — the smoothing
between two ticks, which clip a figure plays, which way it is turned.

The same rule in the other direction is sprint 7's: **the interface is a mirror.** A window
never changes what it shows by itself. It raises a request, the realm decides, and the window
redraws from the realm afterwards. `Play`'s `spendPoint`, `moveItem`, `useItem`, `buy` and
`sell` all return a `bool` for that reason — the bool is the realm's answer, not the window's.

### `app/` — the application and its modes

`src/main.cpp` was 1663 lines: argument dispatch, a save reader, a preloader with its own
thread and spinner, a model-browser list widget, three frame loops wearing one `if` ladder,
and a teardown. It is thirteen lines now, and the rest is here.

| file | what it is |
|---|---|
| `application.*` | the window, the device, the loop, the clock, the statistics, the teardown |
| `context.*` | what every mode is handed: `Paths`, `Context`, `TimeOfDay` |
| `mode.h` | the lifecycle a mode keeps |
| `preloader.*` | the worker thread and the spinner in front of it |
| `modes/play_mode.*` | the game |
| `modes/bench_mode.*` | the bench, in its four kinds |

The division of labour, stated once so it is not re-argued per frame: **the Application owns
the frame and the mode owns the picture.** The resize, the lighting sheet's reload, the
palette reset, the effects pool, the screenshot, `bgfx::frame()`, the clock and the statistics
are the Application's and happen either side of `Mode::frame`. A mode never calls
`bgfx::frame()`, never reads the clock for its own delta and never takes a screenshot — it is
handed the frame it is drawing and the seconds the last one took.

There are **two** modes, not one per command-line flag:

| mode | what it is | raised by |
|---|---|---|
| `PlayMode` | the game: a world, a realm behind it, a character in it, the windows over it | `--world` |
| `BenchMode` | one thing to look at, turning in front of the whole frame | everything else |

`BenchMode` has four **kinds** — `Model` (`--model`), `Figure` (`--figure`), `Browser`
(`--browse`) and `Studio` (`--browse --studio`). They differ in what is put on the bench and
in what stands behind it, and not in how a frame is drawn, which is why they are one class and
not four. Splitting them would have put the same twenty lines in four files.

**The headless run (`--headless`) is deliberately not a mode.** It returns from
`Application::run` before GLFW, the device and the textures exist at all, and that is exactly
what makes it a measurement of the tick rather than of a frame with the drawing switched off.
A `Mode` is a thing the frame loop runs; headless never reaches the loop. Calling it
`HeadlessMode` would have meant a class with a `camera()` and a `frame()` that must never be
called, which is a worse lie than an `if` at the top of `run()`.

## Where does a new file go?

1. **Does it decide anything about the game's rules?** → `sim/`, and the constant needs its
   source named in a comment.
2. **Does it read or write a file the cook produced?** → `content/`.
3. **Does it submit draw calls or own a bgfx handle?** → `gfx/`.
4. **Is it a window, a plate, a label or a pointer?** → `game/ui/`.
5. **Is it something a blow, a death or a level looks like?** → `game/fx/`.
6. **Is it the town standing there or animating itself?** → `game/world/`.
7. **Is it a way of running the binary?** → `app/modes/`, and first ask whether it is a new
   *kind* of `BenchMode` rather than a new mode.
8. **Would another game want it unchanged?** → `core/`.
9. **None of the above** → `game/`, and it is worth a second look at 1–8 first.

Then add it to the source list in `CMakeLists.txt` — the list is explicit and not a glob, on
purpose, so that a file nobody meant to add does not quietly enter the build.

## What is checked, and by what

Two gates, split by one question: does it need a window?

| | runs | covers |
|---|---|---|
| `cmake --build build --target checks` | milliseconds to seconds, no window | `layercheck.py` (the arrows above), `cooked_test` (the reader against the cook), `placement_test` (MU's own AngleMatrix), `sim_test` (a seeded hunt against a fingerprint), `matcheck.py` (the material library) |
| `cmake --build build --target shotcheck` | ~20 s, needs a display | three pinned scenes drawn and compared **exactly** to `tests/reference/` |

`shotcheck` is the frame's answer to what `sim_test` has done for the rules since sprint 5.
Before it, every renderer, shader and pass change was verified by a person opening a PNG and
looking at it — which catches a black screen and misses a wrong roughness for weeks. It
demands *exact* equality rather than a tolerance, because a tolerance is a place for a
regression to hide, and exact is achievable: a pinned scene is identical to the last bit over
repeated runs. Measured, it fails on a 1% ambient change whose worst pixel moves by 1 of 255
— which no eye would have caught.

A reference moves only when the change was *meant* to move it: look at both pictures, then
`tools/shotcheck.py --bless <scene>`, and say so in the commit message. Blessing a reference
to turn a red check green is the one thing that would make it worthless.

**A reference is pinned to a cook as well as to code.** These scenes draw what is in `assets/`,
which is gitignored, mutable and rebuilt by `tools/cook.py`. Re-cook the tables or the art and
the picture legitimately moves with no source change at all, and the references must be
blessed again.

That is worth stating because getting it wrong is easy and was got wrong here. On 2026-09-22
this check went red on `town`, and the difference was written up — in this page and in a
commit — as a one-in-fifteen nondeterminism in the engine, on the evidence that ten runs
either side of it came out identical. It was not. Another session had re-cooked the item
tables that afternoon (their format went from version 6 to 7), so the binary was drawing
different *data* from one run to the next. Any single build was byte-identical across three
runs throughout. **"It differs between runs" and "the tree changed between runs" look exactly
alike from inside the check**, and the second is far likelier on a tree more than one person
is working in.

The references are this Mac's Metal. Another GPU will differ, and that is expected: this is a
check against yesterday's build on one machine, not a conformance suite.

## What is owed

Written here rather than left implied, because a rule nothing keeps is worse than no rule:

- **`shotcheck` cannot tell a stale reference from a regression**, because it records nothing
  about the cook it was blessed against. A fingerprint of the cooked files it drew, written
  beside each reference and compared on a mismatch, would turn "this moved and I do not know
  why" into "this moved because the tables were re-cooked" — which is the one question a red
  result actually raises. The `town` reference is stale as of the v7 item tables and has not
  been re-blessed.
- **`assets/assets/`** is 30 MB of orphaned duplicates that nothing in `src/`, `tools/` or
  `pipeline/` references — a sync that once ran with the wrong root. It is inside a gitignored
  folder so it costs nothing but disk and confusion. Not deleted yet.
- **There is no one asset registry, and on inspection that is less wrong than it sounds.**
  Nothing loads the same thing twice: `content::Textures` keeps one handle per path,
  `game::Figures` indexes its meshes and clip libraries by name, and `game::ItemModels` keeps
  a mesh the first time a row asks for it — which is the point of it, so that a sword in the
  bag and the same sword on the grass are one mesh. What is actually missing is narrower:
  **nothing counts the total.** `content/texture.cpp` reports its own resident bytes and the
  meshes, the clips and the cooked tables report nothing, so there is no one number for what a
  run is holding. That is a page of accounting, not a rewrite, and it is what `docs/budget.md`
  would want before anyone argues about memory.
