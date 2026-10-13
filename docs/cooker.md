# The Cooker: how an agent makes content

The Cooker is how this game's content is made, improved and fixed, **with the user watching**.
The user keeps the studio open (`build/mu2 --studio`), and the agent works inside it through
the **cooker MCP server** (tools/mu/mcp_server.py, registered in `.mcp.json`). The user sees
every step on their screen as it happens:

- the thing put where it lives: a monster at its spawn in its own world, fighting a character
  inside the studio;
- the source edited, with the inspector showing the new value;
- the cook's progress bars;
- the new version swapped in when the cook finishes;
- the map's light changing.

The user, 2026-10-12: "cooker has to work similar like MCP, that it's opened and we see realtime
how objects are made and improved and bug fixed ... if we cook a new monster on Aida, we see it
on the Aida map where it will be placed and a character fighting the monster." And: the old
workflow "asks for server and other stuff to open/close client all the time". So **don't
open and close clients and servers to look at your work. Work in the open studio.**

Underneath are `./mu` (tools/mu/, the command line the MCP tools call) and `./mu serve` (the
job runner). How it was built is one line a slice in docs/order-plan.md §6 (sprints F, G and H);
what is under `./mu` is "The pipeline underneath", at the end of this page.

## Working through the MCP

Start every piece of content work with:

1. **`status`**: is the studio open, and what is on its bench? If no studio is open, ask the
   user to open it. Call `studio_open` only when they ask you to: a window takes their focus.
2. **`show`** the thing you are working on: `show kind=monster name=DeathTree01`. That opens
   Aida, puts the Death Tree on the bench at its spawn, and starts the arena there, inside the
   studio: a level-80 knight with Kris fights it, the panels over the fight. `do "arena off"`
   ends it. For an object, weapon, armour set, wings or a person, it shows the thing in its
   Home scene. From now on the user is watching it.
3. **`shot`** to see what the user sees, before and after each change. Read the picture.
4. Change the source with **`edit`** (`how` set, insert or remove), **`gate`**, **`light`**,
   or **`do`** with the studio's words (Place's `pick`, `nudge`, `add`, `duplicate`, `delete`,
   `mode`, `mark`, `budge`, `look`, `way`, `brush`, `dab`, `undo`). Both move things on screen at
   once: the studio follows a journalled edit of the scene world's objects (`objects.N.at`,
   `angle.2`, `scale`, an insert or a remove) as soon as it is written.
5. **`plan`**, then **`cook`** (only when you may, see "Cooking" below), then **`jobs`**
   with `wait`. The studio shows the bars, and when the cook is of what is on the bench, it
   swaps the new version in, and a monster's arena restarts on the new rules.
   **`arena_log`** says how the fight went.
6. **`journal`** shows what the user changed by hand in the studio (`who=studio`). Read it
   before you touch the same files.

| tool | what |
|---|---|
| `status` | the studio's bench and scene, mu serve's last jobs |
| `show` | a thing in its place for the user to watch; a monster fighting |
| `shot` | the studio as the user sees it, as a picture (`clean` hides the panels) |
| `do` | any of the studio's words, below |
| `plan`, `cook`, `jobs` | what a make would run; queue it; watch or wait for it, and the cores it cooks on (`./mu cores`) |
| `recipe`, `edit` | read a recipe; change source/ in place, journalled |
| `research` | everything MuMain does for a monster, as a checklist in its recipe (below) |
| `gate` | a gate added or removed with its pair |
| `light` | a map's lighting: sun, sky, shadows, tonemap, bloom; dusk, night, rain layers |
| `journal`, `arena_log`, `check` | the edits so far, the fight, the quick gates |

### A new monster: its MU checklist

A monster is not done until each thing MuMain does for it is carried or set aside: its body, its
effects, lights, chrome, sounds and their files, its play speeds (the user, 2026-10-06 and
2026-10-12). **`research`** finds every block of MuMain's source that names it (MONSTER_X, its
MONSTER_MODEL_X, its MODEL_X, and the files its SOUND_s load) and lists them with where they are,
the function, the actions they wait on and what they call. A case with no `break` is said, and
another breed's body on a shared model is set aside.

1. `research name=305` before the recipe exists: what you will have to carry.
2. `./mu new monster 305 --world aida --db`, then `research name=BlueGolem01 write=true`: the
   list goes into the recipe as `mu_checklist`, journalled. Run it again any time: a block's
   state and note are kept.
3. As each block is carried, `edit` `mu_checklist.N.state` to `done` (or `skip`) and
   `mu_checklist.N.note` to how ours does it or why not.
4. The studio's inspector draws the list at the top of the Recipe tab, coloured, the count in its
   header, MuMain's own lines on hover; a click on a state turns it. The user sees what is left.

### A map's light

The game lays its light in layers:

- `sheets/lighting.json` under every map;
- the map's own `sheets/worlds/<w>.json` over it;
- `sheets/time/dusk.json` or `night.json` over that;
- in rain, the map's `<w>_rain.json` blended in.

Each layer is reloaded live. To tune a map:

- `do "scene <w>; tab light"`, so the user sees its Light tab.
- `light world=<w> key=<k>` reads a knob, and shows the base's value beside it.
- `light world=<w> key=<k> value=<v>` sets it. A knob the map inherits is written in.
- `do "time dusk"` shows dusk, then `shot` to look.

Keep a change to one map in that map's sheet. `layer=base` changes every map: ask first.

## The one idea

**`source/` is the truth. Everything else is cooked from it.** Recipes, world manifests,
placements and grids all live under `source/`. `./mu make` turns them into what the game
reads, under `assets/` and `workshop/`. So a change is:

1. Edit the source, with `mu edit`, `mu gate` or `mu paint`, never by hand-rewriting a file.
2. Run `./mu make <target> --plan` to see what is now stale, and why.
3. Run `./mu make <target>`, the cook, only when you may (see "Cooking" below).

Every write through `mu` is a line in **workshop/.mu/journal.log**. Edits the user makes in
the studio land there too: **read the journal to learn what the user changed**, before you
touch the same files.

## The commands you will use

All of them take `--json` and then print one JSON object as their last line. Read that rather
than the prose.

| to | run |
|---|---|
| see what a cook would do and why | `./mu make <target> --plan` |
| cook | `./mu make <target>` (handed to `mu serve` when it is running) |
| watch or stop cooks | `./mu jobs`, `./mu jobs --watch`, `./mu jobs cancel N [--now]` |
| read a recipe | `./mu recipe <Name>` |
| change one value | `./mu edit <Name or file> <dotted.path> <value>` |
| put a value in, or take one out | `./mu edit <file> <path> '<json>' --insert`, `./mu edit <file> <path> --remove` |
| add or remove a gate with its pair | `./mu gate add <w> <to> <col> <row>`, `./mu gate remove <w> <number>` |
| paint the ground | `./mu paint <w> <col> <row> <slot> [--overlay --alpha A --radius R]`, `--undo` |
| check the gates | `./mu check` (layers, sizes, WAL, index, sync, white meshes, tables) |
| find what names a thing | `./mu where <name>` |
| scaffold new content | `./mu new object <w>/<ObjectNN>`, `./mu new monster <n>`, `./mu new item "<name>"` |
| what MuMain does for a monster | `./mu research <Monster|number> [--write]` |

**Targets:**

- one recipe: `Sword01`, `world/Bonfire01`, `BudgeDragon01`;
- `SetMale07`, an armour set;
- `world <w>` and `ground <w>`;
- `placements <w>`, after objects moved;
- `tables`, or `tables <w>` after townsfolk moved;
- `realm`: gates, world rules, breed rules, compiled into the sim;
- `sounds`;
- `rehearsal`, which exercises the runner and cooks nothing.

### Edits keep the file's own text

`mu edit` changes only the touched value's span of text: a scalar is replaced, an insert
copies its neighbours' separator and indent, and a remove cuts the value and the separator
before it. Every other byte stays, so `git diff` shows only your change. That is why you edit
through it and never by loading a JSON file and dumping it back: 39 recipes mix indents, and a
re-dump reformats the whole file.

- A path is dotted, and list indices are numbers: `islands.0.material`, `objects.2234.at.0`,
  `npcs.9.look`.
- A value is JSON (`7`, `0.35`, `true`, `"brass"`); a bare word is a string.
- `--insert` at a list's length appends; at a block's new key, it adds the key.
- An edit that would change more than its own value is refused, and nothing is written.

## Recipes by task

**Move, add or delete a world object.** Objects are `source/world/<w>/<w>.json` `objects`,
each `{type, model, at: [x, y, height] in units (100 a tile), angle: [.., .., yaw degrees],
scale}`.

- Move: `./mu edit <w> objects.N.at.0 <value>`.
- Add: `--insert` a copy of a same-model object at `objects.<length>`.
- Delete: `--remove objects.N`. Every later index moves up one.
- Then `./mu make placements <w>`.

The game's x is `at[0] / 100` metres, and its z is `-at[1] / 100`. In the open studio the
object moves as soon as the edit is written, before the cook. Objects the index adds
(assets/ holds more than source/) are not in the file and cannot be edited this way.

**Move or add a townsperson.**

- Edit `source/world/<w>/world.json` `npcs.N.at` (`[column, row]` in tiles) and `npcs.N.look`
  (1 West, clockwise, to 8 NorthWest).
- Add or remove one with `--insert` / `--remove` on `npcs`.
- Then `./mu make tables <w>`.

**Move, retune, add or remove a gate.**

- A gate's box, level and target are `world.json` `gates.enters.N` / `gates.exits.N`.
- To add or remove one, use `mu gate`, which writes both worlds' rows together. Never add half
  a pair by hand.
- New pairs take numbers from 200.
- A gate a `k...Gate` constant names in src/ is refused.
- Then `./mu make realm` and a rebuild of `mu2` and `mu2_server`. The gates are compiled into
  the sim, so the Hetzner server needs a redeploy too: ask first.

**Paint the ground.**

- Use `mu paint` on `source/world/<w>/tiles.png`: red is the base slot, green the overlay
  slot, blue the overlay's strength. A slot is an index into `<w>.json` `tile_slots`.
- Every stroke is journalled with its old values, so `--undo` is exact.
- The land is a cooked mesh: nothing shows in the game until `./mu make ground <w>`, a heavy
  cook (tile sheets, ground, index, textures, placements, every world's tables). Ask first.

**Tune an item's or a monster's recipe.** Read it with `./mu recipe <Name>`, set values with
`mu edit`, then `--plan`.

- An item's materials are baked by a Blender build, so its plan is the full rebuild.
- A monster's plan ends with `realm` and "compile the game": its rules are compiled.

**See what you made, when the user has no studio open** (a batch run, or they are away): a
self-quitting studio run, never a window left open:

```
MU_STUDIO_DO="20:scene lorencia;100:place;110:pick Bonfire01;130:shot /abs/path/a.png" \
  build/mu2 --studio --mute --frames 150 > /abs/path/run.log 2>&1
```

- `MU_STUDIO_DO` is `<frame>:<words>;...`. Give a world scene 80-100 frames to open.
- `shot` needs an absolute path, and its picture is written when that frame is drawn.
- `hide` and `show` take the panels off and put them back for a clean picture. Hidden panels
  also hide Place's overlays (the gate outlines, the townsfolk's marks, the ground's tint).
- Every word's answer is in the log: `grep "MU_STUDIO_DO" run.log`.
- A run with `MU_STUDIO_DO` leaves `studio.sock` alone: if the user does have a studio open,
  the MCP and `mu do` still talk to theirs. (Before 2026-10-12 the last studio opened took it.)
- Then Read the PNG and look at it. A visual change is not done until you have.

**Drive a studio the user has open:** `./mu do "subject Monsters|Budge; scene Home; wait 30;
shot /abs/x.png; status"`. Each line answers `ok ...` or `error ...`, and the first error
stops the list.

### The studio's words

Shared by `MU_STUDIO_DO` and `mu do`; the list is at the top of
src/app/modes/studio_control.cpp.

| words | what |
|---|---|
| `subject <tab>\|<name>` | put a thing on the bench; tabs: World objects, Monsters, People, Armour sets, Weapons, Wings, Figure parts |
| `scene Stage\|Home\|<world>` | the plain stage, the subject's own place, or a world |
| `plus <N>`, `swap`, `arena`, `arena off` | the item's shine at +N, read the cooked subject again, a monster's fight in the studio |
| `place`, `mode objects\|gates\|townsfolk\|ground` | open Place, and what it edits |
| `pick <model>`, `nudge <dx> <dz>`, `add <model>`, `duplicate`, `delete` | objects, at the view's middle (the subject's spot) |
| `mark <n>`, `budge <dc> <dr>`, `look <n>`, `way <world>` | a gate by number or a townsperson by index; a new gate pair |
| `brush <slot> base\|overlay <alpha> <radius>`, `dab`, `undo` | the ground |
| `tab light`, `time noon\|dusk\|night` | the Light tab; the time of day the scene is lit at |
| `hide`, `show`, `shot <abs.png>`, `wait <N>`, `status`, `quit` | the rest (`quit` closes the user's studio: don't) |

## Cooking: when you may

- **`--plan` is always free.** Run it before every cook and read every line.
- **Ask the user before a cook that changes what the game shows**: a Blender build, a ground
  cook, every world's tables, `realm` plus a rebuild. Cooks are slow and other sessions share
  the cooked files. Since 2026-10-07 every weapon's recipe has been newer than its build, so
  one "small" item cook is a Blender build and every world's tables.
- **`./mu make rehearsal`** exercises `mu serve` and the job bars without cooking anything.
- A step that is stale because of **another session's** edits is theirs to cook. Say so
  rather than cook it for them. `realm` was stale this way on 2026-10-12.

## Rules that bite

- **Work in the user's open studio; don't open and close clients and servers.** Opening a
  window takes the user's focus: `studio_open` only when asked, and the self-quitting shot runs
  only when no studio is open. Never leave a window you opened behind.
- **Put back what you test.** If you change a real source file to prove something, undo it
  through `mu` (`mu edit` back, `--insert` the journal's `old` after a `--remove`,
  `mu paint --undo`) so the journal shows the undo. Then check that the file's hash and
  `git status` are as before. Never undo with `git checkout`: other sessions keep uncommitted
  edits in the same files.
- **Another session edits the same files.** Run `git diff <file>` before you commit, and
  commit with explicit paths (`git commit -F - -- <paths>`), never `git add -A`.
- **The studio needs `mu serve`.** It starts one itself. `mu serve` stops itself when
  tools/mu/*.py changes and it is idle; the next client starts a fresh one. After you change
  tools/mu, a running server answers with the old code until then.
- **A build broken in a file you did not touch** is another session mid-edit. Wait for it
  (retry the object every minute), don't fix it. An odd crash in untouched code means stale
  objects: delete `build/CMakeFiles/mu*.dir/**/*.o` and rebuild.
- **Tests:** `build/cooker_test` (27 checks) and `build/cooker_test --live` with `mu serve`
  running (38); `python3 tools/layercheck.py`; `python3 tools/sizecheck.py`, where only the
  violations in your own files are yours.

## The pipeline underneath

What `./mu make` runs, in the order it runs it. Carried over from MU2 on 2026-09-21 so this tree
makes its own content on any machine, without MU2's Godot projects.

| stage | from | to | tool `./mu make` calls | in git |
|---|---|---|---|---|
| import | MU's `Data/` (`tools/fetch_mumain.sh`) | `source/` | MuExtract, `decode_texture.py`, `terrain.py`; or `./mu new` | yes |
| build | `source/` | `workshop/` | `tools/asset.sh` (Blender, the upscaler), `tools/content.sh` | no |
| index and sync | `workshop/` | `assets/` | `pipeline/index.py`, `tools/sync.sh` | no |
| cook | `assets/` | `assets/cooked/` | `tools/cook.py`, `tools/cook_one.py` | no |

The order carries weight (textures before meshes, the index after any build, a new item's tables
in every world): `./mu make` holds it as the edges of its graph, so reach for the scripts by hand
only when `mu` has no word for the job.

MU's own files come from [sven-n/MuMain](https://github.com/sven-n/MuMain/tree/main/src/bin/Data)
at commit `c0d74c4ed5edfad527b71899337f72d39bfc49f3`, the one `tools/fetch_mumain.sh` checks out.

**A new machine.** `./bootstrap.sh` (bgfx and the headers), then `./tools/content.sh` (hours: every
sheet through the upscaler, every model through Blender), `./tools/sync.sh`, and `./mu make tables`
or a world's cook. It needs Blender at `/Applications/Blender.app` (or `BLENDER=`) and python3
with numpy, Pillow and torch; the upscaler's two checkpoints (RealESRGAN_x4plus,
4x_NMKD-Siax_200k, 67 MB each) are fetched into `weights/` on first use and checked by sha256.

**A build is not byte-reproducible**: `build_maps.py` bakes a random grain, so two builds differ
(Sword01's ORM by a mean 2.8 of 255). Compare builds by eye or by statistics, not by hash.

**Importing a new asset by hand**, when `./mu new` cannot:

    ./tools/fetch_mumain.sh
    dotnet run --project tools/MuExtract -- export-obj <X.bmd> source/<...>/<Name>.obj
    dotnet run --project tools/MuExtract -- export-rig <X.bmd> source/<...>/<Name>.rig.json [--actions=all]
    python3 pipeline/decode_texture.py <X.OZJ> source/textures/x.png
    python3 pipeline/terrain.py <Data/WorldN> <world> source/world <N>

Then write the recipe beside the `.obj`; `islands.py` fills in its island list on the first build.

**Where things came from:** `pipeline/` is `MU2/pipeline/*.py`, writing into `source/`;
`tools/asset.sh` is MU2's per-asset build without the viewer and Godot (the clips stay the
`.actions.glb`; `sync.py` takes it where a part still names a `.res`); `tools/MuExtract/` is MU2's
C# importer (`dotnet`, net10.0). The interface sheets (`hud_cut.py`, `lobby_cut.py`,
`skill_icons.py`, `buff_icons.py`) were cut from MuDream's Season 6 data, not on this machine;
their output in `source/interface` is kept.
