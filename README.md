# MU2 on bgfx

MU2 rebuilt as a **single player** game in C++ on bgfx, Metal on this Mac, for a frame at
1920x1080 with cast and received shadows, PBR, reflections and SSAO. Begun 2026-09-20.

`PLAN.md` is the map: what was decided, what the foundations are, and the sprints.
`docs/roadmap.md` is where that map stands — what is done, what is not in the tree yet, and
what the remaining sprints are.
`docs/conventions.md` is the page that settles every question with one right answer and a
silent wrong one — read it before writing a loader. `docs/budget.md` holds the frame's
accounts, and `docs/sprints/` what each sprint proved and measured.

No server: the simulation lives in the process. The rules are written fresh and smaller
than MU2's, and still traced to 0.75 or marked `invention`. MU2's `pipeline/`, `build/` and
`mu.db` stay the content source; nothing is read from MU2 at run time.

## Build and run

    ./bootstrap.sh        # once: fetches bgfx, bx, bimg, cgltf, stb_image into extern/
    ./build.sh            # Ninja, Release, ccache when it is there
    ./build.sh --trace    # bgfx's own trace in the log, where Metal's refusals show
    ./main.sh             # the game: a new Dark Knight in Lorencia. esc quits
    ./run.sh              # the same binary with nothing decided for you; esc quits

`main.sh` is the one that plays. It makes a character the way MU makes one — the naked class
body, level one, with the Small Axe his class is given and nothing else — puts him down in
Lorencia's square, and turns vsync on, because a frame paced to the display is what a game
wants and a number is what `run.sh` is for. Left click walks, left click on a monster fights
it, right click stops. The spiders are the nest east of the town.

`run.sh` is the same binary with every switch left to the caller, which is how every bench
and every measurement below is taken.

Needs `brew install cmake ninja glfw ccache`.

## Reviewing a run without watching it

Every run writes `mu2.log`: what it loaded, what it refused, a frame line a second, and a
summary table of the budget accounts. Shots land in `shots/` as PNG.

    ./run.sh --frames 300 --shot 100
    ./run.sh --frames 300 --stats /tmp/s.csv --budget

`--stats` writes a row a frame with the CPU and GPU times, the draw count and a column per
view. `--budget` turns an overdrawn account into exit code 2; `--budget shade=1.0` replaces
one account's allowance for that run. Every number is taken vsync off, at 1080p, in a
Release build. A number taken against vsync is not a number.

Paths on the command line must be absolute.

## Content

    ./tools/content.sh                           source/ -> workshop/, whatever is stale
    ./tools/asset.sh Sword01                     one recipe, the same way

MU2's pipeline, carried here on 2026-09-21 so this tree builds its own content without MU2 or
Godot: `source/` holds the recipes and MU's imported art (MU2's `assets/`, and `mu.db`),
`pipeline/` the scripts, `workshop/` their output (MU2's `build/`). Needs Blender and
python3 with numpy, Pillow and torch. MU's own data is
[sven-n/MuMain](https://github.com/sven-n/MuMain/tree/main/src/bin/Data), fetched by
`tools/fetch_mumain.sh`. See `docs/content.md`.

    ./tools/sync.sh                              everything index.json reaches
    ./tools/sync.sh --world lorencia --only-world

Copies `workshop/` into `assets/` here by MU2's own three rules — plus the clip library a
model names in its own glTF `extras`, which is where the 283 player animations live.
`MU2_BUILD=../MU2/build` copies from MU2's build instead.

    ./tools/cook.py --world lorencia             the land, the town and their textures
    ./tools/cook.py --world lorencia --only figures    the bodies, the armour and the clips
    ./tools/cook.py --only showing               the effect sheets and the sounds

The cook turns `assets/` into what the engine loads: BC7 and BC5 `.ktx` with their mip
chains, flat `.mum` meshes, the town's `.mut`, the figures' `.muc` clip libraries baked
frame by frame, and the showing's `.mus`. It is idempotent — an image whose `.ktx` is already
there is not compressed again, because the file's name carries the hash of its source bytes
and its role.

`--only showing` is sprint 6's and is not per-world: an effect belongs to a blow and a sound
to an event, so both land in `cooked/showing` beside the figures. It compresses the 151
effect sheets and makes every sound **mono 16-bit at 22050 Hz**. The mono is not about size:
MU's 104 files are in nine formats and 47 are stereo, and a stereo file has its left and
right baked in and cannot be panned to a place, so the format most of them are in is the one
positioned audio could not have used.

## Checks

    cmake --build build --target checks          everything that can say no without a window

Runs the three tests, and `tools/matcheck.py` — the material audit. That one asks whether every
surface wears the material it ought to: a name that resolves to `index.json`'s library, a baked
ORM that agrees with it, a relief the normal map actually carries, a cutout with holes to cut,
a cooked texture in its role's format. It is a gate, not a report: the faults known on the day
it was written are in `sheets/materials.baseline.json`, and it fails on a new one or on a
recorded one that has since been fixed, so the list can only shrink. `docs/materials.md` has
what it found and what to do about each kind.

## Benches

    ./run.sh --figure BullFighter01 --clip 2 --frames 300 --shot 100
    ./run.sh --world lorencia --crowd 30 --frames 600 --repeat 6 --still

`--figure` puts one figure on the bench ground under the game's own light and prints, every
second, which clip is running, **where its clock stands and how long the clip is** — a clip
that froze on its first frame reports the same name as one that is running, and no still
shows the difference. `--clip` is MU's own action number, read out of the right table of the
two: a monster's 4 is its second swing and a player's 4 is "Stop sword".

`--crowd N` is how many monsters stand in the town, in the map's own spawn mix; `--crowd -1`
is every one its spawn table names, which for Lorencia is 290.

    ./run.sh --world lorencia --frames 400 --effects 64 --effect-size 2 --effect-sheet blood

`--effects N` puts N sprites through the transparent pass, to price it. It is a probe and
not a look: the cost of that pass is **fill rate**, so what decides its account is how much
of the screen the sprites cover rather than how many there are, and `--effect-size` is what
drives that. 64 sprites half a metre across cost nothing measurable; 64 sprites 60 m across
cost 1.19 ms. `docs/budget.md` has the table and the rate derived from it.

    ./run.sh --world lorencia --still --no-figures --frames 60 --shot 1 --shot-path /abs/slide --shadow-slide 3
    tools/shimmer.py /abs/slide --heat /abs/heat.png --allow 200

The shadow shimmer test. The camera holds and only the sun's split moves, so any pixel that
changes between two frames is the shadow's fault. `--shadow-log PATH` writes the split's
texel phase on a world-fixed grid and the camera's lag behind the character, one row a frame,
and works on a `--play` walk. `docs/shadow-probe.md` has how to read both, and the three
faults they found. `tools/pan.py` measures the same with the camera moving, at fixed spots on
the ground, and `tools/shadowref.py` measures the character's own shadow against a much finer
map on identical frames (`--fixed-dt`). `--shadow-view` draws the sun's visibility alone.
