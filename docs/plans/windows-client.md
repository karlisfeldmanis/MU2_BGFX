# The Windows client

2026-10-14. The user: "we need to make a plan how we will make window client". This plan builds
on the port test (docs/reference/windows-linux-test.md), which proved bgfx, GLFW, our shaders and
a mingw-built `.exe` under Wine. It also builds on the player build (docs/plans/layout.md:
`MU2_SHIP`, `.mupak` packs, `./mu dist`). The server does not change: it already runs on Linux,
and a Windows player connects to the same Hetzner server as a Mac player.

## The decisions, with a recommendation each

| Question | Recommended | Why |
|---|---|---|
| Compiler | **mingw-w64 GCC, cross-compiled on this Mac** | Already installed and proven by the port demo. It is the same compiler family as the server (GCC on x86_64), and that matters for lockstep: see W4. One machine builds Mac and Windows. MSVC can come later, if ever. |
| Renderer | **Vulkan first**, Direct3D 11 later as a fallback | The SPIR-V shaders compile on the Mac with the shaderc we already build. Direct3D bytecode needs Microsoft's compiler, so a Windows machine or a CI runner. |
| Upscaling | **None at first**: draw at native resolution | MetalFX is Apple-only. `--scale` is switched off on Windows. FSR 1 is a later option. |
| Testing | **Wine on this Mac** for the smoke test, then **a real Windows PC** | Wine (Rosetta + MoltenVK) proves that the game runs. It cannot measure speed, drivers or feel. |
| What we ship | **A zip**: `MU2.exe`, `data/*.mupak`, `logs/` | It is the same as the Mac app's insides. An installer and code signing come at the end. |
| Studio, Cooker, bench | **Mac only** | `MU2_SHIP` leaves them out already. The Windows build is the player's client only. |

## What is Mac-only today

Measured 2026-10-14 over `src/` (~154k lines). There are few platform calls, and they sit in few
files:

| File | What it uses | On Windows |
|---|---|---|
| `gfx/window.cpp` | `RendererType::Metal`, `glfwGetCocoaWindow` | Vulkan, `glfwGetWin32Window` |
| `gfx/window_mac.mm`, `gfx/metalfx.mm` | Cocoa, MetalFX | not built; `--scale` is off |
| `net/socket.cpp` | BSD sockets, `poll` | Winsock: `WSAStartup`, `closesocket`, `WSAPoll` |
| `core/jobs.cpp` | pthread QoS classes | `std::thread` with `SetThreadPriority` |
| `core/watch.cpp` | signals, `execinfo`, `posix_spawn` | a stub first; later SEH and `MiniDumpWriteDump` |
| `core/paths.cpp` | `$HOME`, `unistd.h` | the exe's folder (`GetModuleFileNameW`) and `%APPDATA%\MU2` |
| `core/log.cpp`, `core/pack.cpp`, `core/files.cpp`, `app/application.cpp` | `unistd.h`, `stat` | `std::filesystem`, or `<io.h>` |
| `app/modes/studio_*`, `studio/cooker.cpp` | unix sockets, `posix_spawn` | not in the ship build |
| `CMakeLists.txt` | Metal shader profile only, Cocoa frameworks, Homebrew GLFW, absolute `MU2_ROOT_DIR` | also `spirv`; frameworks only on Apple; GLFW from `third_party/`; relative root |

There are no Cmd-key shortcuts (`GLFW_MOD_SUPER` is not used), so there is nothing to remap
there. The cooked data (BC7/BC5 `.ktx`, glb, tables) and miniaudio already work on Windows.

## The steps

Each step leaves the Mac game exactly as it was. The check after each step is the Mac build,
`sim_test`, and the game in Lorencia.

**W1. A platform layer.** Add `core/platform.h` with `platform_mac.cpp`, `platform_posix.cpp`
and `platform_win.cpp`. It holds threads and their priority, sockets, the exe path, the user's
data folder, and the watchdog. `<windows.h>` is included only in `platform_win.cpp`, with
`NOMINMAX` and `WIN32_LEAN_AND_MEAN`, so its `min`, `max`, `near`, `far` and `ERROR` macros
never reach game code. The files in the table above call the layer. This is mostly moving code.

**W2. CMake for Windows.** Reuse `tools/portdemo/mingw.cmake`. Build GLFW 3.4 from source in
`third_party/` on every platform, instead of Homebrew. Compile shaders with
`PROFILES metal spirv`. Build the `.mm` files and link the frameworks only on Apple. The renderer
is chosen per platform. `MU2_ROOT_DIR` is used only in dev builds. Out of this comes
`./mu build windows` → `out/build-windows/MU2.exe`.

**W3. Compile fixes.** mingw is stricter than Apple clang in places: missing includes, `long` is
32 bits on Windows, `%zu` in printf, path separators and UTF-8 paths (`std::filesystem::path`
from `u8string`). Fix until it links, then until it starts.

**W4. The Wine smoke test, and lockstep.** A dev `.exe` reads `out/assets` through `Z:\`.
Lobby, then log in to a private server on another port (verify-lockstep-on-a-private-port), then
walk to Lorencia, fight, open the bag, pick up loot, chat.

The gate that matters most is **lockstep**. The client's mirror must step the realm to the same
bits as the server (`-ffp-contract=off`, `sim/fmath.h`). Build `sim_test` for Windows, run it
under Wine, and diff its seeded log against the Mac's and the Linux server's. They must be
byte-identical. mingw GCC on x86_64 is the server's own compiler family, so this should hold.
MSVC would need its own proof.

Also check the renderer: the Metal-only workarounds in `conventions.md` ("Metal, and what it
refuses"), the compute shaders (`cs_*`, culling and ranking), and the y-flip on readbacks
(`caps->originBottomLeft`). `shotcheck` against Mac shots tells us if the picture matches.

**W5. `./mu dist windows`.** Build from a clean commit with `MU2_SHIP`. Write
`dist/windows/MU2/`, which holds `MU2.exe` (static, no DLLs), `data/*.mupak` (the same packs,
with `spirv` shaders in place of `metal`), and `logs/`. Settings and saves go to
`%APPDATA%\MU2`. Then `mupack --check`, and zip it. `tools/mu/dist.py` already has the shape;
only the bundle and the shader folder change.

**W6. Real Windows.** This needs a PC: the user's own, or a cloud GPU machine. Check:
- frame time against our budget (docs/reference/budget.md)
- DPI scaling on 125% and 150% displays
- fullscreen and alt-tab
- non-English keyboard layouts
- audio device changes
- at least one NVIDIA, one AMD and one Intel GPU

What we find decides whether we need Direct3D 11. If so, its shaders are built on a GitHub
Actions `windows-latest` runner, or by shaderc under Wine.

**W7. Ready for players.** Crash dumps (`MiniDumpWriteDump` into `logs/`). Authenticode signing,
so SmartScreen doesn't warn players off the game. An installer (Inno Setup) or the zip. A
protocol check in the lobby, so an old client says so instead of failing.

## Cost

W1-W4: one to two sessions, as the port test estimated. W5: under a session. W6 depends on the
hardware we have. W7 is mostly buying a certificate and waiting for it.

## Risks

- **Lockstep on another compiler.** This is the one that can quietly break play. W4 tests it
  before anything else is judged.
- **Vulkan driver variety.** Older Intel and AMD drivers on laptops. Direct3D 11 (W6) is the
  answer if they fail.
- **Frame cost.** Our budget was measured on an M1 Pro with MetalFX helping. On a mid-range PC
  at native 1440p, shadows and MSAA (~7 ms on the Mac) may need a Windows quality preset.
- **Other sessions' uncommitted edits.** W1 touches `core/` and `net/`. Diff before staging,
  and commit through explicit paths.
