# The Windows client

2026-10-14. The user: "we need to make a plan how we will make window client", "we need a plan
which will work on windows", and "we use wine on this macos to test it".

**The idea:** we write and build a real Windows program, and we test it under Wine on this Mac.
Everything happens on this Mac: we build it here (mingw), compile its shaders here (SPIR-V from
our shaderc), pack it here (`./mu dist windows`), and play it here (Wine). The port test
(docs/reference/windows-linux-test.md) already proved this chain with a Death Knight demo.

The server does not change. A Windows player joins the same Hetzner server as a Mac player.

## The decisions

| Question | Choice | Why |
|---|---|---|
| How we test | **Wine on this Mac** ("Wine Stable" from Homebrew, x86_64 on Rosetta) | The user's choice. It is already installed and proven. |
| Renderer | **Vulkan** | It is what Wine here runs properly: winevulkan → MoltenVK → Metal. Our Wine translates Direct3D 11 to old OpenGL 4.1, which is no fair test. On real Windows, Vulkan runs on every NVIDIA, AMD and Intel GPU of the last eight years. |
| Compiler | **mingw-w64 GCC**, cross-compiled here | It is installed and proven. The server is built with GCC, so the lockstep rules (`-ffp-contract=off`, `sim/fmath.h`) hold in the same compiler family. Linked static: one exe, no DLLs, no Visual C++ runtime to install. |
| Shaders | **SPIR-V from our own shaderc** | It needs no Windows machine. Direct3D shaders would need Microsoft's compiler. |
| Upscaling | **None**: drawn at native resolution, with a quality preset | MetalFX is Apple-only. |
| Studio, Cooker, bench | **Mac only** | `MU2_SHIP` leaves them out already. |

## What Wine proves, and what it can't

Wine proves that:
- the program starts, reads its packs, draws, plays sound, connects and plays;
- the Windows API calls are right;
- the lockstep log matches the server's (Rosetta runs the exe's own x86 instructions, so the
  bits are the exe's).

Wine can't prove:
- **frame time on PC hardware**, because Wine, Rosetta and MoltenVK sit in between;
- **driver differences** between GPUs;
- **some Windows strictness**, which Wine is looser about.

For the strictness, we write the code by the Windows rules even where Wine forgives a mistake,
and we set Wine up to act like the awkward cases:

| Real Windows | In the code | Under Wine |
|---|---|---|
| Laptops run games on the weak GPU | export `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` | check the exports with `nm` |
| `Sleep(1)` can take 15.6 ms | `timeBeginPeriod(1)` or a high-resolution waitable timer, for frames and the net tick | Wine sleeps precisely, so it can't show this; it is in the code by rule |
| User names like Jānis in paths | `std::filesystem::path` and wide (`W`) APIs only, never `char*` with `fopen` | a Wine prefix whose user folder is named `Jānis` |
| 125% and 150% display scale | DPI-aware manifest (PerMonitorV2) and GLFW's content scale | `winecfg` → Graphics → 120 and 144 dpi |
| Open files are locked | saves, settings and logs write a temporary file, then `MoveFileExW(REPLACE_EXISTING)` | Wine enforces this, so a mistake shows |
| A console window opens with the game | `-mwindows`, plus an icon and version info (`.rc`) | visible in Wine |
| Alt-tab, second monitor | borderless fullscreen, not exclusive mode | partly |
| Winsock | `WSAStartup`, `ioctlsocket`, `TCP_NODELAY` | Wine enforces this |

## What is Mac-only today

Measured 2026-10-14 over `src/` (~154k lines):

| File | Uses | On Windows |
|---|---|---|
| `gfx/window.cpp` | `RendererType::Metal`, `glfwGetCocoaWindow` | Vulkan, `glfwGetWin32Window` |
| `gfx/window_mac.mm`, `gfx/metalfx.mm` | Cocoa, MetalFX | not built |
| `net/socket.cpp` | BSD sockets, `poll` | Winsock |
| `core/jobs.cpp` | pthread QoS | `std::thread` and `SetThreadPriority` |
| `core/watch.cpp` | signals, `execinfo`, `posix_spawn` | `SetUnhandledExceptionFilter`, `MiniDumpWriteDump` |
| `core/paths.cpp` | `$HOME` | the exe's folder, and `%APPDATA%\MU2` |
| `core/log.cpp`, `pack.cpp`, `files.cpp`, `app/application.cpp` | `unistd.h`, `stat` | `std::filesystem` |
| `CMakeLists.txt` | Metal profile, Cocoa frameworks, Homebrew GLFW, absolute root | per platform |

## The steps

After each step, the Mac game must be unchanged: the Mac build, `sim_test` and Lorencia.

**W1. A platform layer, and lockstep under Wine.** Add `core/platform.h` with Mac and Windows
halves: threads, timers, sockets, paths, file replace, the watchdog. `<windows.h>` appears only
in the Windows half, with `NOMINMAX` and `WIN32_LEAN_AND_MEAN`. Then cross-compile the server-only
part (core, sim and tests, no graphics) for Windows, and run `sim_test.exe` under Wine.
**Done when:** its seeded log is byte-identical to the Mac's and the Linux server's.

**W2. `./mu build windows`.** CMake per platform:
- GLFW 3.4 from `third_party/`;
- shaders in `metal` and `spirv`;
- the `.mm` files only on Apple;
- Vulkan in `window.cpp`;
- a root relative to the exe in ship builds.

Then fix compile errors (`long` is 32 bits, `%zu`, missing includes). It writes
`out/build-windows/MU2.exe`. **Done when:** it links, and a dev build starts under Wine, reads
`out/assets` through `Z:\`, and shows the lobby.

**W3. It plays under Wine.** Run it against a private server port:
- the lobby, then Lorencia;
- walk, fight, loot, the bag, chat, a gate to Dungeon;
- then the same on the live server.

Compare shots with the Mac's (`shotcheck`). Then go through the "under Wine" column above: the
`Jānis` prefix, 120 and 144 dpi, and saves while the game runs. **Done when:** 15 minutes of play
with no fault and a clean log, and the shots match the Mac's.

**W4. `./mu dist windows`.** Build from a clean commit. Write `dist/windows/MU2/`, holding:
- `MU2.exe`;
- `data/*.mupak`: the same packs as the Mac's, with `spirv` shaders in place of `metal`;
- `logs/`.

Settings and saves go in `%APPDATA%\MU2`. Then `mupack --check`, and zip it. **Done when:** the zip,
unpacked into a fresh Wine prefix, starts and logs in.

**W5. A quality preset.** Our frame budget was measured on an M1 Pro with MetalFX. Add Low,
Medium and High (shadow size, MSAA, grass distance), so a mid-range PC at native resolution has
somewhere to go. Wine can't measure this; the first players' `--stats` lines will.

**W6. The first Windows players.** Give the zip to a few players. `logs/` (`mu2.log` and the crash
dump) comes back to us. This is the first time real GPUs and drivers see it. If someone's Vulkan
driver fails, then we add Direct3D 11: its shaders need a GitHub Actions `windows-latest` runner,
the one part that can't be done on this Mac.

**W7. Ready for players.** An Authenticode certificate, so SmartScreen and antivirus don't warn
players off an unsigned exe. Then an installer (Inno Setup) or the signed zip, and a protocol
check in the lobby, so an old client says "update" instead of failing.

## Cost

- W1-W2: one to two sessions.
- W3: a session.
- W4-W5: a session each.
- W6-W7: mostly waiting, for players and for the certificate.

Nothing new needs to be installed for W1-W4.
