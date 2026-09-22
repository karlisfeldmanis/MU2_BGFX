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
| `game/ui/` | hud, panel, bag, shelf, card, desk, describe, vitals, cursor, outline, arrival, stage, items_stage | everything drawn flat over the frame, and the pointer's own feedback |
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

**Not built yet; this section is the design and `layercheck.py` already allows its arrows.**

`src/main.cpp` is 1663 lines: argument dispatch, the preloader, a model-browser list widget
and the whole frame loop in one function. `app/` is where that goes, leaving `main.cpp` as
the entry point and little else.

A **mode** is one way of running this binary, and there are four. They share a lifecycle and
nothing else:

| mode | what it is | raised by |
|---|---|---|
| `PlayMode` | the game: a world, a realm, a character | `--world` with `--play` |
| `BenchMode` | the model bench, one object turning in front of the frame | the default |
| `ViewerMode` | the browser: every cooked model, by category | `--browse` |
| `HeadlessMode` | the sim stepped with no device and no textures | `--headless` |

## Where does a new file go?

1. **Does it decide anything about the game's rules?** → `sim/`, and the constant needs its
   source named in a comment.
2. **Does it read or write a file the cook produced?** → `content/`.
3. **Does it submit draw calls or own a bgfx handle?** → `gfx/`.
4. **Is it a window, a plate, a label or a pointer?** → `game/ui/`.
5. **Is it something a blow, a death or a level looks like?** → `game/fx/`.
6. **Is it the town standing there or animating itself?** → `game/world/`.
7. **Is it a way of running the binary?** → `app/modes/`.
8. **Would another game want it unchanged?** → `core/`.
9. **None of the above** → `game/`, and it is worth a second look at 1–8 first.

Then add it to the source list in `CMakeLists.txt` — the list is explicit and not a glob, on
purpose, so that a file nobody meant to add does not quietly enter the build.

## What is owed

Written here rather than left implied, because a rule nothing keeps is worse than no rule:

- **`assets/assets/`** is 30 MB of orphaned duplicates that nothing in `src/`, `tools/` or
  `pipeline/` references — a sync that once ran with the wrong root. It is inside a gitignored
  folder so it costs nothing but disk and confusion. Not deleted yet.
- **There is no asset registry.** Every loader is handed an asset directory and a name and
  opens a path itself, so nothing knows what is resident and a second request for the same
  mesh loads it twice. `content/texture.cpp` counts its own bytes; nothing counts the rest.
