# Windows crash hunt

**Task: find bugs that can crash or freeze the MU2 Windows client, and fix them.**

You're working in `MU2_BGFX`, the game client of an MMO written in C++ on bgfx and GLFW. The
Windows client `MU2.exe` and the launcher `MU2Launcher.exe` are cross-compiled with mingw on the
Mac and render with Vulkan on Windows. Read `CLAUDE.md` and `docs/plans/windows-client.md` first.

## Rules you must follow

- Commit straight to `main` with explicit paths: `git commit -F - -- <paths>`, after reading
  `git diff <file>`.
  - Other sessions keep uncommitted edits in the same files. Commit only your own hunks.
  - Never use `git add -A` or `git stash`.
- **Never push, release or deploy unless the user asks in that message.** `./mu release`,
  `server/deploy.sh` and `web/deploy.sh` all count.
- Build at low priority on a few cores: `MU_CORES=4 nice -n 15`.
- Don't run `tools/vkcheck.sh` or any Vulkan-validation run without asking. One froze the user's
  Mac. Plain Wine runs are fine, one at a time, small (`--width 1280 --height 720 --cap 30`).
- Test against a private server here, never the live box (`ssh hetzner`). Stop your test
  processes by port, for example `pkill -f "mu2_server --port 44597"`. A pattern like
  `server.py --db` also kills other sessions' local sites.
- `dist/windows/MU2/` is exactly what `mu release` uploads. Anything you put there for a test (a
  DLL, logs, a second exe) must be removed afterwards, by full path.

## Already found and fixed (2026-10-15)

Don't redo these; do check them for regressions.

- **Freeze while the window isn't on screen.** bgfx's `vkAcquireNextImageKHR` waited forever
  inside NVIDIA's driver. Now it waits at most 100 ms: `third_party/patches/bgfx-vulkan-acquire.patch`.
- **No Vulkan on the PC.**
  - bgfx fell back to Direct3D and crashed; now `Init::fallback = false` in
    `src/gfx/window.cpp` and `src/launcher/ui.cpp`.
  - The launcher's `close()` shut down a bgfx that never started.
  - The game and the launcher now show a message box in both cases.
- **Minimised window (0×0).** It no longer resets the renderer: `Window::pump`.
- **Launcher, non-ASCII user names.** The launcher now has a UTF-8 manifest
  (`src/launcher/launcher.manifest`).
- **Antivirus file locks.** Settings files are replaced, not deleted first, and update file swaps
  retry.
- **Install folder and update paths.** An unwritable install folder is named in the error, and
  update file paths with drive letters or backslashes are refused.
- **Window focus.** The game brings itself to the front, and every loss of focus is logged with
  the window that took it.

## Known and not yet fixed

- **Title-bar drag.** In windowed mode, holding or dragging the title bar puts GLFW's
  `glfwPollEvents` inside Windows' modal loop. No frames run, so the network pump stops
  (`src/game/play.cpp`, `RemoteLink::pump` on the main thread). The freeze watchdog files a false
  report after 5 s, and the server drops the player after `kStalledSeconds` (20 s).
- **A device-lost page fault.** About 1 run in 13 hit `VK_ERROR_DEVICE_LOST` in the first frames
  after the loading screen. See `docs/plans/windows-client.md` for how to catch it.
- **Older Windows 10 (before version 2004).** `WSAPoll` doesn't report a refused connect, so a
  refused port waits out the full timeout (`src/net/socket.cpp`).

## What to hunt: anything that ends the process or stops the main thread

- **Crashes:**
  - null or dangling pointers on failure paths, especially after a failed init or a lost device;
  - uncaught exceptions (`std::filesystem` without `error_code`, `std::stoi`, `.at()`);
  - stack overflow from large stack arrays on threads;
  - integer wrap on sizes read from files or the network;
  - shutdown order (threads still running while globals are destroyed).
- **Freezes:** any wait without a timeout on the main thread. That includes GPU fences, swapchain
  present, sockets, futures and joins, and the server wait in `RemoteLink::join`.
- **The GPU side:**
  - swapchain recreation on resize, Alt+Tab and display changes;
  - a monitor unplugged or a resolution changed while running;
  - a driver reset (TDR) and device loss;
  - capture tools (OBS `graphics-hook64.dll` and NVIDIA's overlay were loaded in the real hang
    reports).
- **Malformed input:**
  - data the game reads from the server or from packs (`src/net/wire.cpp` parsers,
    `src/core/pack.cpp`);
  - save and options files (`options.txt`) that are truncated or corrupt, for example after a
    power cut.
- **The Windows environment:** paths (OneDrive-redirected folders, long paths, read-only
  installs), a second copy of the game started, low memory.

## How to work

- **Real crash data:** Windows players' crash and freeze reports are on the box.
  `ssh hetzner 'curl -s http://127.0.0.1:8080/internal/crashes'` gives the counts, and
  `/var/lib/mu2-web/crashes/<day>/` holds each `.txt` and `.dmp`.
  - Minidumps: parse the ModuleList stream (4) and the ThreadList stream (3); RIP and RSP are at
    context offsets +0xF8 and +0x98.
  - Scan each thread's stack for addresses inside modules.
  - Name MU2.exe addresses with `x86_64-w64-mingw32-nm -C`, taking the nearest symbol below;
    addr2line gives files only.
  - Unstripped binaries: `out/symbols/<commit>/windows/MU2.exe`, or
    `hetzner:/var/lib/mu2-symbols/<commit>/`.
- **Build:** `./mu dist windows` builds `dist/windows/MU2/` from HEAD, so commit before building
  to test. `cmake --build out/build-launcher-win --target mu2_launcher` builds the Windows
  launcher from the working tree.
- **Run under Wine:** `wine MU2.exe --lobby --windowed --width 1280 --height 720 --cap 30 --mute
  --server 127.0.0.1:<port>`, from inside `dist/windows/MU2/`.
  - The player's build accepts only `--lobby --windowed --fullscreen --width --height --cap
    --mute --server` and a few graphics switches. There is no `--frames`, so stop runs with
    `wineserver -k`.
  - Sign-in needs a ticket. Run a private `web/server.py --db <scratch> --port <N>` and
    `out/build/mu2_server --port <M> --store <scratch> --accounts 127.0.0.1:<N> --launcher-only`.
    Register, log in with `-A MU2Launcher/4`, `POST /api/ticket`, and pass
    `MU2_TICKET=ticket:secret`.
  - Set `MU2_CRASH_SITE=http://127.0.0.1:<N>` so crash reports go to your private site, not the
    live one. Each game run needs a fresh ticket; a reused one is refused as a replay.
  - To get into a world, seed a character in the scratch `characters.db`. Bring Wine to the front
    (`osascript … set frontmost`), click the pedestal with a small mingw helper (`SetCursorPos`
    plus a posted `WM_LBUTTONDOWN`), then post `VK_RETURN`. This moves the user's pointer for an
    instant, so keep it to a few clicks.
  - Watch `logs/mu2.log`: it writes a line each second; "HANG" lines and `crash-*.txt` in `logs/`
    are failures. Capture the window with `screencapture -l <CGWindowID>`.
- **Simulating faults Wine doesn't have:** put a stand-in `vulkan-1.dll` beside the exe and run
  with `WINEDLLOVERRIDES="vulkan-1=n,b"`.
  - It exports the six functions bgfx dlsyms (`vkCreateInstance`, `vkGetInstanceProcAddr`,
    `vkGetDeviceProcAddr`, `vkEnumerateInstanceExtensionProperties`,
    `vkEnumerateInstanceLayerProperties`, `vkEnumerateInstanceVersion`) and forwards them to
    `C:\windows\system32\vulkan-1.dll`.
  - From there you can make any Vulkan call fail or stall: `VK_ERROR_DEVICE_LOST`,
    `VK_ERROR_OUT_OF_DATE_KHR`, `VK_ERROR_SURFACE_LOST_KHR`, out-of-memory, or a timeout.
  - Write one if none is at hand (~60 lines).
  - Use `WINEDLLOVERRIDES="vulkan-1=d"` to test a PC with no Vulkan loader. Don't disable
    `winevulkan`: Wine's own DXGI needs it, so that combination crashes Wine rather than our code.
- **Prove every fix:** reproduce the bug first with the old build, then show the new build
  survives the same test. Wine can't show PC frame times or NVIDIA-specific behaviour, so say so
  where it applies.

## Deliverable

1. A ranked list of findings, most severe first. For each: file:line, the scenario that triggers
   it, and whether you reproduced it.
2. Fixes for the critical ones, each tested as above and committed with a clear message.
3. A short report for the user: what was fixed and proven, what was fixed but not proven, what is
   still open, and the builds ready to release. Don't release them.

## The first pass (2026-10-15)

No new crash data: the five reports on the box were all from builds before the acquire patch.
The hunt was a reading of the code in three parts (the network, the files and the Windows
environment, the GPU and the threads), then a fix and a Wine test for each item below. The
tools are in that session's scratchpad: a stand-in `vulkan-1.dll` that fails the acquire, the
surface, the swapchain or the device on a flag file and counts presents; `hold.exe` (the
system's move loop, checked with `GUI_INMOVESIZE`); `boxtext.exe` and `boxok.exe` (read and press
a dialog box).

**Fixed and shown under Wine** (before, then after):

| What | Commit | Before | After |
|---|---|---|---|
| Title bar held: no frames, the line unread, a false hang report | ef19f3b8 | HANG at 5.2 s, two reports | none; pings answered through 30 s |
| Surface lost and not made again; swapchain refused | c275a873 | picture never back; Wine's assert (a PC's driver fault) | back within a frame of the fault ending |
| Window closed while connecting: the load could not give up | a80a2a21 | 8.7 s to exit (up to ~80 s) | 0.7 s |
| Device lost: the game vanished with no word | 5073aa58 | gone, crash report only | a box saying what to do, then gone |
| A pack's header damaged: up to 8 GB asked for before the crash watch | 0d8f2caf | 3 s zeroing 8 GB here (bad_alloc on a small PC) | refused in 13 ms |

**Fixed, not reproducible here:** the crash upload's thread meeting the static destructors at
exit (66e4e9c5, `_Exit` in the player's Windows build); folder walks that threw part-way
(a08ad97d, b580c87a); a refused connect on Windows 10 before 2004 (a80a2a21, `select`).

**Wine traps found on the way:** a dispatched `WM_TIMER` ends Wine's keyboard move loop, so the
hold timer is taken by a `WH_MSGFILTER` hook instead; a `MessageBoxW` with `MB_TOPMOST |
MB_SETFOREGROUND` closes itself after ~5 s under Wine.

**Still open**, most severe first:
1. **The hang watch dumps its own process** (`core/watch_win.cpp`, `dumpInto` from `hangInto`).
   `MiniDumpWriteDump(GetCurrentProcess())` suspends the other threads; if one holds the heap
   or loader lock, the watcher can block for ever and a stall the game would have survived
   becomes a freeze. The five real dumps were written fine. Fix: `PssCaptureSnapshot` and dump
   the snapshot, or drop the .dmp for hangs. Wine may not have `PssCaptureSnapshot`.
2. **No Vulkan pipeline cache** (`gfx/callback.h`, `cacheRead`/`cacheWrite` empty): every run
   compiles its pipelines on the first world frame, a hitch that can file a false hang.
3. **The server can drop a slow-loading player**: the join comes at about half the load, and
   nothing reads the line until the load ends (`play_mode.cpp` load stages); a slow PC in a busy
   town owes more than kStalledSeconds. Fix: pump the link between stages, or join last.
4. **`net::take` erases the buffer's front once per frame** (`net/wire.cpp`): quadratic on a
   big backlog, which eats into the join's 60 s.
5. **The launcher's close waits for its worker** (`launcher/ui.cpp` `close()` joins; `check()`
   hashes gigabytes and does not look at `stop_`): "Not Responding" on a close during Checking.
6. **`options.txt` is written in place** (`core/args.cpp`): a power cut leaves it empty, and the
   launcher then never gives back the card's preset. Write and rename, as saves do.
7. **A second copy of the game** truncates the first's `mu2.log` and sends its reports twice.
   A named mutex at start.
8. **Non-ASCII paths** outside the UTF-8 code page (Windows before 1903): narrow strings into
   `std::filesystem::path` in pack, files and roster; `path.string() + ".new"` in the launcher.
   The Jānis prefix check is still not done.

## The second pass (2026-10-17)

The new code since the first pass and every report on the box, read by five agents (network,
GPU and restart, files and launcher, NVIDIA, AMD). The evidence: every NVIDIA device lost was on
Direct3D 11 (a GTX 950 put there by the low-end first run, Gordichan's GTX 1650 laptop at 150 fps
uncapped), none on Vulkan since the acquire patch; no AMD crash at all.

| What | Commit | Shown |
|---|---|---|
| A renderer that does not start tries the other, named or Auto | cac656c5 | Wine, d3d11 forced: old refused, new on Vulkan to the character screen |
| A lost device also caps the next start at 60; minimised draws at 30 | cac656c5 | minimised: 200-250 fps old, 27 new; the loss's cap not run |
| The loss's restart and box pause the freeze watch, not join it; one minidump at a time | cac656c5 | read only |
| Auto-play ends at the character screen | cac656c5 | read only |
| The launcher follows a game that restarts itself (job; process group) | 71731e9d | stand-in under Wine and on the Mac; not with the real pair |
| Launcher close: hidden at once, 3 s for its threads, then _Exit | 97d2aaf0 | Wine, a site that never answers: 14 s old, 3 s new |
| A check asked while the worker is busy is asked again | 97d2aaf0 | read only |
| Low-end first run: Direct3D 11 not for NVIDIA; 2-3.5 GB cards Medium | 97d2aaf0 | a table of eleven cards |
| Vulkan out of memory is a lost device, not silence (bgfx-vulkan-memory.patch) | b14fabe0 | compiled, not run |
| NVIDIA on Direct3D 11 that loses its device starts again on Auto | b14fabe0 | not run |

**Still open:** the first pass's 1-3 and 6-8 (the hang dump itself, the pipeline cache, the
slow-load drop, options.txt in place, a second copy, non-ASCII paths); overlay DLLs (RTSS,
ShadowPlay, OBS) named in the log and the report; the D3D11 swap chain's frame latency
(`maxFrameLatency` 0 is ignored, the default 3 stays); DNS on the main thread in a reconnect's join.
Under Wine the launcher cannot name the card (no DXGI factory), calls it Low and draws itself with
Direct3D 11, which plain Wine lacks: run it with `--renderer vulkan`.

**The full cycle before release (2026-10-17, b4f8cb21, `mu dist windows`):** under Wine against a
private web/server.py behind a small front serving /game/ as the box does, and a private
mu2_server from the same snapshot (the launcher's `--server`, b4f8cb21's own, keeps the game off
the live ones). All passed: sign-in, a 1.54 GB install, Play into Lorencia and 100 s with no HANG
or ERROR; minimised 90 -> 30 fps; Graphics Apply (Textures) restarted the game (pid 18431 ->
23643) with the launcher on In game throughout; the game closed, Play back; the launcher closed at
25% of a whole-install hash, gone in 4 s, "closed with its work still going" (the old one waited
out the hash); **self-update**: launcher A, offered B, fetched it, took its place and started it
with --site/--server/--renderer handed on, "updated to B". A stand-in vulkan-1.dll beside MU2.exe
(WINEDLLOVERRIDES="vulkan-1=n,b"; a flag file `fakevk-lose` makes submit and present return
DEVICE_LOST, `fakevk-oom` fails the next allocation of 1 MB and up with its retries): a lost
device in the world restarted the game into the world in 3 s, options.txt from scale 85, 2x MSAA,
cap 180 to scale 67, 1x, cap 60; then a failed 2.1 MB allocation in an anti-aliasing change was
"Out of video memory: vkAllocateMemory of 2129920 bytes failed", Lowest, back in the world in
5.6 s; the launcher followed all three processes. Not shown here: NVIDIA's or AMD's own drivers,
and Direct3D 11.
