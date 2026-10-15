# The Linux release

2026-10-15. The user: "we need to make a plan for linux release".

**The idea:** a Linux player gets the same thing a Windows player gets. They download the launcher
from the website, sign in, and it installs `MU2` and its packs and keeps them up to date. They
play on the same Hetzner server. It is the Windows client's road again (docs/plans/windows-client.md,
docs/plans/launcher.md), and most of that road is already built:

- **Vulkan with SPIR-V** is the Windows renderer. The same `.bin` files work on Linux. The port
  test proved this in 2026-10 (docs/reference/windows-linux-test.md).
- **The server is already a Linux program**, built by GCC on x86_64 (Ubuntu 26.04 on the box).
  A Linux client therefore uses the same compiler family and instruction set as the realm it
  mirrors. Lockstep is the least risky part of this port.
- **POSIX** already covers sockets, `pread`, `posix_spawn` (the restart after Apply) and the
  watchdog's signals. The Mac and the server share that code.
- **The launcher's engine** (manifest, hashes, resumable fetches, `MU2_TICKET`) runs on libcurl
  on the Mac. Linux uses the same code.

What is missing is mostly the `#else` branches that mean "the Mac" today, plus a way to build,
test and ship without a Linux PC.

## Where it stands (2026-10-15)

**X1 is done, and X2 is done but for `./mu build linux`.**
- **The machine:** OrbStack `mu2linux64`, Ubuntu 22.04.5 amd64 (glibc 2.35), with GCC 15.2 from
  the toolchain PPA (the box's own version) and the client's dev packages.
- **The build:** `-DMU2_LINUX_CLIENT=ON` is its own switch, so the box's server build is
  untouched. It turns on `MU2_SHIP`, -O2 and a static libstdc++, and uses GLFW 3.4 from
  `third_party/` with X11 and Wayland, SPIR-V shaders only, and `gfx/window_linux.cpp`.
  ```
  orb -m mu2linux64 sh -c 'cd <repo> && CC=gcc-15 CXX=g++-15 cmake -S . -B out/build-linux -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DMU2_LINUX_CLIENT=ON && nice cmake --build out/build-linux --target mu2 sim_test -j 6'
  ```
  The whole game needed two compile fixes.
- **What it links:** `ldd MU2` names only libz, libm and libc. GLFW opens X11 or Wayland, and bgfx
  opens Vulkan, when the game starts.
- **Lockstep:** `sim_test` on Linux gives all 18,162 lines the same as the Mac's once the
  timestamps are left out.
- **It runs:** next to the Windows dist's `data/`, on Xvfb with lavapipe, `MU2` reads the 17 packs,
  starts Vulkan and draws the lobby as the Mac does. That runs at 0.5 fps on CPU Vulkan under
  Rosetta, which says nothing about real machines. The live server turns it away with "Cannot
  reach the server", as it does any game started without the launcher's ticket (protocol 32).
  The freeze watch wrote a stack with offsets for `addr2line`.
- **New code:**
  - `paths.cpp` puts `data/` and `logs/` beside the program and the player's files in
    `$XDG_DATA_HOME/MU2`;
  - `main.cpp`'s restart reads `/proc/self/exe`;
  - `alert.cpp` uses zenity or kdialog;
  - start-up failures say "MU2 needs Vulkan";
  - the Renderer row reads Vulkan;
  - native Wayland is opt-in with `MU2_WAYLAND=1` until X6.

**On this Mac's GPU (2026-10-15).** The ARM build runs on the M1 in a podman machine on libkrun.
Mesa's Venus driver (the krunkit author's patched Mesa, EL9 only, so AlmaLinux 9:
`tools/linux/gpu.Containerfile`) speaks Vulkan to krunkit, which runs it on MoltenVK. Vulkan
reports "Virtio-GPU Venus (Apple M1 Pro)".
- **No swap chain.** Venus there draws but cannot present, so `MU2_COPY_PRESENT=1` (a developer's
  switch in `gfx/window_linux.cpp`) starts bgfx with no window. Every screen view draws into a
  frame of the window's size (`gfx::screenTarget()`), and each finished frame is read back and put
  into the X11 window with Xlib.
- **The bgfx patch.** Without a window, bgfx asked for `VK_KHR_swapchain` anyway, so
  `third_party/patches/bgfx-vulkan-headless.patch` asks only when the device has it.
- **How to look.** Xvfb, x11vnc and an SSH tunnel to Screen Sharing on `localhost:5901`.
  xdotool plays it: hold each click 0.1 s, since the game reads the button once a frame.
- **What it showed.** The lobby at about 80 fps (GPU 4.3 ms). Against a private `mu2_server` on
  the Mac (reached as `192.168.127.254`), it created a Dark Knight, entered Lorencia (120-130 fps
  in town, 70-80 in the fields), fought an Elite Bull Fighter, died and came back.
- **What it doesn't test:** the swap chain, x86_64, and any PC driver.
- **Machine traps:**
  - krunkit 1.1.1 does not know podman 6.1.4's `--timesync`; a wrapper in
    `~/.config/containers/helpers/` drops it.
  - virtio-fs shares answer "Connection refused", so the sources and packs go in over SSH with tar.
  - A debug bgfx (`-DBX_CONFIG_DEBUG=ON`) stops on `s_shadowCompare` set twice in one draw. That
    is our renderer's, and harmless in a normal build.

**Next:** crash and log reports from the Linux game, `./mu dist linux` and `./mu release linux`
(X4), then the launcher (X5).

## What is Mac-only today, and needs a Linux half

Surveyed 2026-10-15:

| File | Today's `#else` | On Linux |
|---|---|---|
| `CMakeLists.txt` | `MU2_SHIP` only on `WIN32`; a non-Apple, non-Windows build is the server or a dev build | a `linux` ship build: GLFW from `third_party/` with X11 (and Wayland), `spirv` shaders, `mu_report` on libcurl, the launcher target |
| `gfx/window.cpp` | `RendererType::Metal`, `glfwGetCocoaWindow` | Vulkan, `glfwGetX11Window` + `glfwGetX11Display` (`nwh`, `ndt`); later Wayland's `wl_surface` + `wl_display` with `NativeWindowHandleType::Wayland` |
| `core/paths.cpp` | ship paths from `_NSGetExecutablePath` and `~/Library/Application Support` | `/proc/self/exe` for `data/` beside the program; the player's files in `$XDG_DATA_HOME/MU2` (`~/.local/share/MU2`) |
| `app/alert.cpp` | a fallback `#else` | `zenity` / `kdialog` if present, else stderr and the log |
| `launcher/engine.cpp` | `~/Library/...`, `~/Applications`, sysctl for the CPU and memory | XDG folders, `/proc/cpuinfo` and `/proc/meminfo`, `/etc/os-release` |
| `launcher/engine.cpp` `keep`/`kept` | the Mac's sealing | the Secret Service (libsecret, loaded at run time) when there is one; otherwise a `0600` file, as the Mac does |
| `launcher/gpu_mac.mm` | Metal's device | `gpu_linux.cpp`: ask Vulkan for the device type (integrated, discrete, CPU = llvmpipe) for `presetFor` |
| `launcher/ui.cpp`, `engine.cpp` shortcut | Win32 or nothing | a `.desktop` file and icon in `~/.local/share/applications` |
| `game/ui/menu.cpp` Renderer row | Metal reads as Metal, Windows steps through Vulkan and Direct3D 11 | Vulkan only, shown and not stepped, like the Mac |
| `core/watch.cpp` | Apple or the server's backtrace | the server's `execinfo` path, plus `/proc/self/maps` in the crash file so `addr2line` against the kept symbols can name it |
| `tools/mu/dist.py`, `release.py` | `macos` or `windows` | `linux` as a third target |

No change is needed for the cooked textures (BC7/BC5 read on every desktop Vulkan driver) or for
miniaudio, which finds PipeWire, PulseAudio or ALSA at run time.

## The decisions

| Question | Proposal | Why |
|---|---|---|
| Which machines | **x86_64 only**, glibc **2.35 or newer** (Ubuntu 22.04, Debian 12, Fedora 36+, Arch, Mint 21, SteamOS 3) | It covers almost every Linux gamer. arm64 Linux desktops are rare. |
| How we build | **in an x86_64 Ubuntu 22.04 container in OrbStack** (Rosetta), from `./mu dist linux` on this Mac | It needs no new machine. Building on the oldest glibc we support is what makes one binary run on newer ones. The box is x86_64 too, but it runs the live server, so we don't build there (dont-use-all-cores). |
| Compiler | **GCC, -O2**, `libstdc++` and `libgcc` static, glibc dynamic | It is the server's compiler. -O2 for the reason the Windows build is -O2 (the GCC 15 vectoriser bug). Static C++ runtime means the player's distro version does not matter. |
| Renderer | **Vulkan only** | Mesa's Vulkan (RADV, ANV) and NVIDIA's cover every GPU since about 2015. A machine with no Vulkan gets the clean "could not start its graphics" refusal. OpenGL would need a third shader set and a third renderer to judge. |
| Window system | **X11 first** (XWayland runs it on every Wayland desktop), **native Wayland after**, picked by GLFW 3.4 at run time | X11 is one code path that works everywhere. Native Wayland is better for fractional scaling and is a step of its own (X6). |
| Package | **the launcher plus a plain folder**, the same manifest as Windows under `/game/linux/`; the launcher is one `MU2Launcher` file in `MU2Launcher-linux.tar.gz` | It is the same update path we already run. No root, no distro packages. AppImage or Flatpak could come later around the same folder. **Not Steam**: the game is not released on Steam (the user, 2026-10-15). |
| How a player gets the launcher | **one terminal command** shown on the site: `curl -fsSL https://mu-classic.com/downloads/install-linux.sh \| sh` (the user, 2026-10-15), with the tarball as a plain download beside it | It is the Mac's `install-macos.sh` route. Linux players expect a command, and it needs no root and no browser download. |
| Install place | `~/.local/share/MU2/game` by default; Change picks another | No root needed. It follows XDG. |
| Studio, Cooker, bench | **not built** | `MU2_SHIP` already leaves them out. |

## What we can test here, and what we can't

**OrbStack's x86_64 machine** (Rosetta) runs the real Linux binary. We do not need Wine for this.

- With Mesa's lavapipe (software Vulkan) on Xvfb, it can show:
  - start-up, packs, the lobby and a shot;
  - sound, which starts with no device present;
  - the connection, sign-in and the launcher's whole update cycle against the box over real
    HTTPS (Wine here can't do TLS; Linux has no such limit);
  - `sim_test`'s lockstep log, which must match the box's line for line.
- It can't show:
  - frame time, because lavapipe runs on the CPU;
  - any real driver's quirks (RADV, ANV, NVIDIA);
  - Wayland compositors;
  - display scaling on a real desktop.

**A real Linux machine is the one thing this Mac cannot be.** The options are a player with Linux
(as in W6), or a rented GPU box. (A Steam Deck owner's would do too, as an AMD machine on RADV, but the Deck is not a target.) The
user decides which; X5 waits for it.

## The steps

After each step, the Mac and Windows games must be unchanged: the Mac build, `./mu dist windows`,
`sim_test`, and Lorencia.

**X1. The build machine and lockstep.** Create an OrbStack `amd64` Ubuntu 22.04 machine,
`mu2linux64`, with `build-essential cmake ninja-build` and the X11, Wayland, Vulkan loader, curl
and Xvfb dev packages. Install GCC 13 from the toolchain PPA, because 22.04's GCC 11 is older
than the C++ the tree uses. Then add `tools/cmake/linux.cmake`, build the server-only part there,
and run `sim_test`.
**Done when:** its log is byte-identical to the Mac's and the box's.

**X2. `./mu build linux`.** CMake grows a third player platform. `MU2_SHIP` turns on for a Linux
client build, never for the server build. The build uses GLFW from `third_party/` with X11 and
Wayland, `spirv` shaders, and Vulkan in `window.cpp` with the X11 handle. It also needs the
`paths.cpp` Linux branch and `alert.cpp`. Then fix the compile errors.
**Done when:** `out/build-linux/MU2` links, starts on Xvfb with lavapipe, reads the packs and
writes a lobby shot that matches the Mac's.

**X3. It plays.** On a private server port, from the OrbStack machine:
- the lobby, then Lorencia;
- walk, fight, loot, the bag, chat, a gate to Dungeon;
- Apply's restart (posix_spawn);
- a crash file whose stack `addr2line` can name.

Then the same on the live server. Compare shots with `shotcheck`.
**Done when:** 15 minutes of scripted play with a clean log, the mirror's hashes all agree, and
the shots match.

**X4. `./mu dist linux` and `./mu release linux`.** `dist/linux/MU2/` holds:
- `MU2`, stripped, with its symbols kept as for Windows;
- the same `data/*.mupak` as Windows, minus the `dxbc` shaders;
- `logs/`.

Its `.tar.gz` goes up the same way, and `release.py` puts it under `/srv/mu2-web/game/linux/`.
Check `ldd MU2`: it should name only glibc, libm, libdl/libpthread, and whatever GLFW does not
load at run time. Anything else either goes static or is opened at run time. Test the folder in a
fresh Debian 12 and a Fedora container too.
**Done when:** the tarball unpacked on a clean 22.04 and a clean Fedora starts and logs in.

**X5. The launcher on Linux.** `MU2Launcher` for Linux needs:
- the XDG folders;
- the libsecret-or-file token;
- `gpu_linux.cpp`, so `presetFor` opens an integrated GPU or llvmpipe on Lowest;
- the `.desktop` file;
- self-update.

libcurl differs between distros (its OpenSSL and GnuTLS builds have different symbols), so
the launcher either links a static curl with mbedTLS or opens `libcurl.so.4` at run time. X5
decides which by testing both containers.

**The terminal command.** `install-linux.sh` is written by `dist_launcher` next to
`install-macos.sh`, and `mu release launcher` publishes it with `MU2Launcher-linux.tar.gz`. The
script is POSIX `sh`, because it is piped into `sh`, not bash. It never asks for root. It:
1. checks the machine first and says plainly what is wrong:
   - not x86_64;
   - glibc older than 2.35 (`ldd --version`);
   - no `libvulkan.so.1` (`ldconfig -p`), which only warns, with the package name to install
     (`libvulkan1` / `vulkan-loader`);
2. stops if the launcher is running (`pgrep -x MU2Launcher`), as the Mac's does;
3. fetches the tarball with curl, or wget if there is no curl, into
   `~/.local/share/MU2/launcher/` through a temporary name, then unpacks and replaces;
4. links `~/.local/bin/mu2-launcher` to it, and says so if `~/.local/bin` is not on `PATH`;
5. writes `~/.local/share/applications/mu2-launcher.desktop` and the icon to
   `~/.local/share/icons/hicolor/256x256/apps/mu2.png`, so it appears in the desktop's menu;
6. starts the launcher detached (`nohup ... &`) when there is a display (`$DISPLAY` or
   `$WAYLAND_DISPLAY`); over SSH it only says how to start it.

Running it again is the update and repair. The launcher updates itself after that. The site's
download section gets a Linux tab with the command and a copy button, like the Mac's
(web/site/index.html).

**Done when:** on clean Ubuntu 22.04 and Fedora containers, the command from the site installs the
launcher, which signs in, installs, updates one changed pack, and starts the game signed in. A
second run of the command replaces the launcher and keeps the sign-in and the game. On a machine
without Vulkan, it warns and names the package to install.

**X6. Wayland and scaling.** Native Wayland through GLFW 3.4's platform pick, and the content
scale at 100%, 125%, 150% and 200% (the hover ring and the HUD read `Window::contentScale`).
This is tested under a nested `weston` in OrbStack, and then for real in X7.
**Done when:** the same shots under X11 and Wayland, and at each scale.

**X7. The first Linux players.** Give the launcher to a few players.
Their `logs/` and crash files come back through the launcher's crash upload. This is the first
time a real driver sees it. Watch for:
- NVIDIA's driver versions;
- old Intel on ANV or hasvk;

**Done when:** a week of play from at least two GPU makers, with no unexplained crash.

**X8. Later, if wanted.** Wrapping the same folder for Flathub, and an arm64 build. Not Steam,
and no Steam Deck work (gamepad, its screen): the user, 2026-10-15. None of them is needed for the first release.

## Cost

- X1-X2: one or two sessions. Most of it is CMake and the `#else` branches listed above.
- X3: a session.
- X4-X5: a session each. Most of X5's code exists already; the curl and token choices are the work.
- X6: a session.
- X7: mostly waiting for players or hardware.

Nothing new is installed on the Mac. OrbStack is already here, and the machine is made in X1.

## Open questions for the user

1. *Answered 2026-10-15: the launcher, installed by a terminal command (X5).*
2. **Which real machine for X7:** a player, or a rented GPU box?
3. *Answered 2026-10-15: no. The game is not released on Steam, and the Deck is not a target.*
