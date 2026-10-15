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
