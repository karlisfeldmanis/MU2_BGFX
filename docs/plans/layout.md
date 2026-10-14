# Layout

Where every folder goes, and what a player is given. Written 2026-10-13. The user's asks were "we
need very clear folder structure because now we have src and source a lot of root folders and
chaos" and "we need to make seperated folder where we put or client dist ... hide all source
data and compress data for end user". They picked the full tidy. Nothing below is moved yet.

## The tree

```
MU2_BGFX/
├─ README.md CLAUDE.md CMakeLists.txt VERSION .mcp.json .gitignore
├─ mu            the one command: build, play, viewer, bootstrap, cook, check, dist
│
├─ src/          the game's C++ (inside unchanged)
├─ server/       the server's C++, deploy.sh, the service file
├─ tests/        the C++ tests and shotcheck's reference pictures
├─ shaders/      the .sc sources
├─ content/      was source/; what we author, the Cooker's truth
│   sheets/      was sheets/: lighting, time, lobby, worlds, materials
├─ pipeline/     the Python cook: pipeline/ plus pipeline/cook*.py, sync*, voice, mixamo, ...
├─ tools/        mu's own code, bot, netbot, botwatch, perf, the checks, portdemo
├─ docs/         README.md the index; plans/ reference/ content/ game/ worlds/
│
├─ third_party/  fetched, never ours (ignored; mu bootstrap fills it)
│   extern/ patches/ weights/ downloads/
├─ out/          generated, ignored; deleting it costs a rebuild and a cook, nothing else
│   build/ build-debug/ build-ship/ assets/ workshop/ shots/ saves/ logs/ (mu2.log, crashes)
└─ dist/         what players are given (ignored; `./mu dist` writes it)
    macos/MU2.app   windows/   linux/
```

Three rules keep it clear:
- **Is it in git?** Then it is in one of the eight folders above the first gap.
- **Can it be remade?** Then it is under `out/`.
- **Was it downloaded?** Then it is under `third_party/`.

- `src/` and `content/` no longer look alike. `src/content/` stays as it is: it is the C++ layer
  that reads what the cook wrote, which is a different thing from the content folder.
- `saves/` under `out/` is the developer's saves only. A player's saves are in their user folder
  (see below).
- `mu play`, `mu run`, `mu viewer`, `mu build` and `mu bootstrap` become `./mu play`,
  `./mu run`, `./mu viewer`, `./mu build` and `./mu bootstrap`, with the same switches.

## What moves where

| Now | Becomes | References outside docs |
|---|---|---|
| `content/` | `content/` | ~355 lines |
| `content/sheets/` | `content/sheets/` | ~58 |
| `assets/` | `out/assets/` | ~540 |
| `workshop/` | `out/workshop/` | ~110 |
| `build/`, `build-debug/` | `out/build/`, `out/build-debug/` | ~85 |
| `shots/`, `screenshots/`, `crashes/`, `saves/`, `mu2.log` | `out/...` | ~35 |
| `extern/`, `patches/`, `weights/`, `downloads/` | `third_party/...` | ~70 |
| `pipeline/cook*.py`, `sync*`, `voice.py`, `mixamo.py`, ... | `pipeline/` | ~205 |
| the five root scripts | `./mu` subcommands | the docs and the memory |

- **The game's folders are named once**, in the `MU_DIRS` block of `CMakeLists.txt` (`MU_ROOT`,
  `MU_ASSETS` and one `MU2_*_DIR` per folder), and read back through `core::folders()`
  (`src/core/paths.h`). The tests and tools take `MU2_ASSET_DIR` from the same variable.
- **The tools' folders are named once** in `tools/paths.py` (str, and `P.*` as pathlib) and
  `tools/paths.sh`. Every script in `tools/`, `tools/mu/` and `pipeline/` imports or sources them.

## The order

Each step is one commit and leaves the game, the cook, the Cooker MCP and the server building
and running. Nothing moves while another session has uncommitted work in the folders it touches,
or while the studio is open: the studio's baked paths would point at nothing.

1. **One root, in each language.** Done 2026-10-13: `core::folders()` and `tools/paths.py` /
   `tools/paths.sh`. The binary bakes the same folders as before; `sim_test`, `cooker_test`,
   `./mu --help` and ten `--plan` captures are unchanged. Left over: `src/game/ui/desk.cpp:108`
   still pastes `MU2_ROOT_DIR "/extern/..."` (another session's file then); it becomes
   `gfx::fontPath("Amarante-Regular.ttf")`.
2. **`third_party/` and `out/`.** Done 2026-10-14 (out/build configured fresh). Move the ignored folders. Change `MU_DIRS`, the `extern/`
   include paths in `CMakeLists.txt`, `tools/paths.py`, `tools/paths.sh`, `.gitignore`,
   `mu bootstrap` and `mu build`. This needs a clean rebuild, because `build/` moves.
3. **`content/`.** Done 2026-10-14 (0cb37cf8; recipes through `mu` edit, journalled). Used `git mv source content` and `git mv sheets content/sheets`. Then fix:
   - the roots
   - `mu edit`, `gate`, `light` and `paint`
   - the journal, and the studio's relative `"content/..."` and `"content/sheets/..."` keys
     (`light_panel.cpp`, `studio_place.cpp`, `studio_marks.cpp`, `studio_desk.cpp`)
   - `pipeline/asset.sh:90`, which tests a recipe folder's name against `source`
   - `CLAUDE.md`, `docs/content/cooker.md` and the memory
   - the docs' own move into `plans/ reference/ content/ game/ worlds/` (docs/README.md), with
     every citation of a moved doc rewritten in the same commit

   Check with `./mu check`, a `./mu make X --plan`, and the studio opening on a monster.
   - the citations of docs deleted on 2026-10-13 (6a6d9d6f) that sat in other sessions' files
     then: `docs/game/party.md:5`, `server/src/host.h`, `src/client/link.h`, `remote_link.{h,cpp}`,
     `src/game/play.h`, `src/net/wire.h`, `src/sim/items.h`, `machine.cpp`, `rules.h`,
     `tests/sim_test.cpp` (sprints/NN → "sprint N", server-plan → server/README.md,
     second-wings → wings.md, drop-boxes and kundun-box-sources → kundun-box.md), and in
     `content/` through `./mu edit`: MagicBox01/03/08, Quest04, Wing04-07, the 13 recipes and
     Hommerd01 that say "See docs/worlds/devias-port.md" (drop the sentence), devias' ground.json
     (devias-ground.md), and `content/sheets/materials.json:8` ("PLAN.md's rule").

4. **`pipeline/`.** Done 2026-10-14 (08f08603). Move the cook scripts and `paths.py`/`paths.sh` out of `tools/`, then run a `--plan` cook and
   `./mu check`.
5. **`./mu` subcommands.** Done 2026-10-14 (08f08603): `./mu play|run|viewer|build|bootstrap`, the scripts in `tools/mu/run/`. Retire the root scripts, and update the README and the memory.

Steps 1 and 2 are a session together; steps 3-5 are another.

## What a player is given

**Built 2026-10-13.** `./mu dist macos` makes `dist/macos/MU2.app`:
- **Size:** 1.51 GB. The 2.57 GB the game reads compresses to 1.50 GB in 17 packs.
- **Proof:** all 6166 files read back byte for byte through the game's own reader.
- **The answer to the stale question:** `tools/mu/manifest.py --stale` proves 1.34 GB of cooked
  textures are named by nothing. They are outputs of older cooks that the cook never deletes, and
  the packs leave them out. They still sit in `assets/`.
- **Still to do:**
  - build `dist` from a clean checkout of a commit, not the working tree, so another session's
    half-done edits can't ship;
  - a Developer ID and notarisation, so another Mac opens it without a warning;
  - an x86_64 slice for Intel Macs;
  - a play-through by the user on the live server, whose protocol must match the client's.

The sections below are the design as planned; where they differ from what was built, the code
is right: `core/pack.h`, `core/paths.cpp`, `src/main.cpp`, `tools/mu/dist.py`.

`./mu dist macos` writes `dist/macos/MU2.app`, and later `MU2.dmg`. Nothing outside the bundle is
read. Nothing of `content/`, `out/workshop/`, the glb sources or the studio is in it.

```
MU2.app/Contents/
  MacOS/MU2              the player build (below)
  Resources/
    data/*.mupak         the cooked data, packed and compressed
    shaders/metal/       the 568 KB of .sc.bin
    fonts/               the .ttf the game uses
    icon, Info.plist
```

### The player build

`cmake -DMU2_SHIP=ON` builds the player client.

- **Left out of the build:**
  - `mu_studio`, ImGui and the bench and studio modes
  - `sweep`, `headless` and the studio sockets
  - the `MU_STUDIO_DO` and `MU2_*` environment hooks
- **Kept:** the player's switches (`--server --fullscreen --windowed --vsync --cap --scale
  --graphics --msaa --mute`). Every other switch is refused.
- **Start-up:** it always opens on the lobby. Single player through `LocalLink` (`--world X --play`)
  is not reachable.

Cheats are already refused against a server (`Play::here()`), so this protects nothing on the
wire. It removes the tools and the shortcuts from the player's binary.

### Where it reads and writes

- **Reads:** `core::paths()` finds the data next to the executable: `Contents/Resources` on macOS,
  the exe's folder on Windows and Linux. The dev build keeps the repository root.
- **Writes:**
  - The log (`mu2.log`) and the crash and freeze reports go to `logs/` in the client's folder,
    beside `MU2.app` (the user, 2026-10-13). Where that folder can't be written, they go to the
    user folder's `logs/` instead: a downloaded app runs from a read-only copy until it is moved.
  - The options, `account.key`, the characters and the layouts go to the player's user folder:
    - `~/Library/Application Support/MU2` on macOS
    - `%APPDATA%\MU2` on Windows
    - `$XDG_DATA_HOME/mu2` on Linux

  An installed app must not write inside itself; on macOS, signing forbids it.
- **Server:** it defaults to the live server (already `kDefaultHost` in `src/net/wire.h:115`).

### The packs

- **Format:** a `.mupak` is a table of contents plus entries, each compressed with zstd. zstd is a
  new dependency in `third_party/`; the wire's system zlib is too slow to decode at load.
- **Logical paths:** entries keep their paths (`cooked/lorencia/meshes/House01.mum`), so the
  strings inside the cooked binaries need no change.
- **The read layer:**
  - `core::readFile` reads the pack first and the loose file second, so the dev build keeps
    loose files.
  - The two libraries that open files themselves get memory loads: cgltf through `cgltf_parse` on
    bytes, and miniaudio through `ma_decoder_init_memory`.
- **Pack split:** one pack per world, plus `common`, `figures`, `wardrobe`, `music` and `voice`. A
  patch then replaces a few packs, not 3 GB.

| | Now | Shipped |
|---|---|---|
| Cooked textures, meshes, clips, tables | 3.1 GB | ~2.0-2.4 GB |
| `cooked/*/raw/*.bin` (cook intermediate) | 1.7 GB | 0 |
| Possibly stale figure and wardrobe .ktx | 0.75-1.3 GB of the 3.1 | 0, once checked |
| World grids and ground glbs | 171 MB (whole folder 1.1 GB) | ~120 MB |
| Music, voice | 267 MB | ~180 MB, voice as ogg |
| Items still loaded as glb (jewellery, misc, pets) | 34 MB | cooked to .mum |
| **Total** | 6.9 GB in `assets/` | **~2.3-2.8 GB** |

The .ktx files are BC7/BC5 and already GPU-compressed, so zstd takes less off them (about 15-30%)
than off the meshes, clips and tables.

### What "hidden" means

- **What it stops:** packs stop browsing and copying: no folder of PNGs and glbs, and no recipes.
  An XOR-scrambled table of contents (what MuDream does) stops casual unpacking tools.
- **What it cannot stop:** it does not stop a determined person. The client has to decode
  everything it draws, and every MU client's data has been ripped.
- **What really protects the game:** the server. It decides everything, and the source, recipes,
  journal and tools never leave this machine.

### Windows and Linux

- **Status:** `dist/windows` and `dist/linux` wait for the port. Today only `tools/portdemo` runs
  there; see `docs/reference/windows-linux-test.md`.
- **Code that needs porting:**
  - the renderer choice in `window.cpp`
  - SPIR-V and D3D shader builds
  - a MetalFX fallback
  - `watch.cpp`, `log.cpp` and the sockets
- **Work that is already shared:** the pack format, `core::paths()` and `MU2_SHIP` are the same on
  every platform, so that work is done once.
