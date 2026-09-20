# MU2 on bgfx

MU2 rebuilt as a **single player** game in C++ on bgfx, Metal on this Mac, for a frame at
1920x1080 with cast and received shadows, PBR, reflections and SSAO. Begun 2026-09-20.

`PLAN.md` is the map: what was decided, what the foundations are, and the sprints.
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
    ./run.sh              # esc quits

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

    ./tools/sync.sh                              everything index.json reaches
    ./tools/sync.sh --world lorencia --only-world

Copies MU2's `build/` into `assets/` here by MU2's own three rules — plus the clip library a
model names in its own glTF `extras`, which is where the 283 player animations live. Nothing
is symlinked, so a run measured here does not change because somebody rebuilt MU2.

    ./tools/cook.py --world lorencia             the land, the town and their textures
    ./tools/cook.py --world lorencia --only figures    the bodies, the armour and the clips

The cook turns `assets/` into what the engine loads: BC7 and BC5 `.ktx` with their mip
chains, flat `.mum` meshes, the town's `.mut`, and the figures' `.muc` clip libraries baked
frame by frame. It is idempotent — an image whose `.ktx` is already there is not compressed
again, because the file's name carries the hash of its source bytes and its role.

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
