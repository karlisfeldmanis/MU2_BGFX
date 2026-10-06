# Windows and Linux: the port test

2026-10-06. The user asked how hard a Windows version would be, and whether we could test it
under Wine. The game itself stays Metal-only (conventions.md). This page records a small demo
built to answer the question without porting the game: what ran, how to run it again, and
the traps on the way.

**Result:** bgfx, GLFW, cgltf, stb_image and our own shaders all work off the Mac. A Windows
`.exe` cross-compiled here runs under Wine through Vulkan, and the same source builds natively
on Linux and runs there. Both play a real MU monster's clips from its glb. The renderer path
for a port is Vulkan with SPIR-V, both on Windows and on Linux.

## The demo: `tools/portdemo/`

`main.cpp` is about 300 lines. It loads one monster glb from `assets/assets/monsters/`
(Death Knight by default), skins it on the CPU, cycles its seven clips (stand, stand 2,
walk, attack 1, attack 2, hit, die; four seconds each) and draws it textured with a key
light and a rim light. The glow part is drawn additively. The camera circles the model.
Keys: 1-7 pick a clip, Space next, A back to auto-cycle, Esc quits. `shaders/` holds one
vertex and one fragment shader.

```
tools/portdemo/build.sh windows       # then: cd build/portdemo-windows && wine portdemo.exe
tools/portdemo/build.sh linux         # builds and runs in the OrbStack machine, writes two PNGs
tools/portdemo/build.sh linux --monster GiantOgre01
```

The output goes to `build/portdemo-<os>/`: the program, `shaders/spirv/`, and the glb. The
first build compiles bgfx and takes a few minutes; later builds compile only the demo. For a
Windows renderer other than Vulkan, pass `d3d11` or `opengl` as an argument. Neither has
shaders built for it yet.

## Windows, under Wine

- **Toolchain:** `x86_64-w64-mingw32-gcc` 15.2 from Homebrew, using `mingw.cmake`. GLFW is
  the official 3.4 WIN64 zip (its `lib-mingw-w64`), which `build.sh` fetches into
  `build/portdemo-glfw/`. The exe is linked fully static, about 16 MB, and needs no DLLs.
- **Wine:** "Wine Stable" from Homebrew, an x86_64 build. **It needs Rosetta**: without it,
  `wine` fails with "bad CPU type in executable". Rosetta was installed with
  `softwareupdate --install-rosetta --agree-to-license`.
- **Vulkan:** this Wine includes MoltenVK, so the chain is .exe → Wine → winevulkan →
  MoltenVK → Metal on the M1 Pro. bgfx reported `Vulkan`.
- **Numbers:** 100-180 fps with v-sync on, while the real game was running beside it. These
  say nothing about real Windows hardware, and Wine + Rosetta + MoltenVK sit in between.
- **Seeing it:** the Wine window opens behind other windows, and the Dock shows it as
  "exec". To capture it alone, get its window id from `CGWindowListCopyWindowInfo` (a
  three-line Swift script) and run `screencapture -l<id>`.
- **Plain Wine without Vulkan** translates Direct3D to OpenGL 4.1, which macOS deprecated.
  It is not a fair test of a renderer. Do not judge D3D11 that way.

## Linux, in OrbStack

- **Machine:** OrbStack (`brew install --cask orbstack`) with an Ubuntu 24.04 arm64 machine
  named `mu2linux`. It mounts this Mac's `/Users` and `/private/tmp` at the same paths.
  Packages: `build-essential cmake ninja-build libglfw3-dev libx11-dev libxext-dev libgl-dev
  libwayland-dev libxkbcommon-dev mesa-vulkan-drivers libvulkan1 vulkan-tools xvfb x11-apps
  imagemagick`.
- **Running:** the VM has no GPU and no screen. The demo draws through Mesa's software Vulkan
  (lavapipe) on an Xvfb display, and `xwd | convert` captures it. You cannot watch it live,
  and its frame rate means nothing.
- **Porting:** the only code change from Windows is the window handle. It comes from
  `glfwGetX11Window` + `glfwGetX11Display` (as `nwh` and `ndt`) instead of `glfwGetWin32Window`.
- **Not covered:** an x86_64 Linux build (a normal PC; it needs an x86 toolchain or
  container), and Wayland.
- **Housekeeping:** `orb stop mu2linux` stops the machine; `orb delete mu2linux` removes it.

## Traps found

- **`bgfx::copy` before `bgfx::init` crashes**, as a null read inside `bgfx::alloc`. The first
  build created textures while loading the glb, before bgfx was started. Start bgfx first.
  To find a crash like this: Wine prints the faulting address. The exe's image base is
  `0x140000000`, so the nearest `x86_64-w64-mingw32-nm -C` symbol below the address names the
  function. addr2line gave nothing even with `-g`.
- **This bgfx is newer than most examples.** `bgfx/platform.h` is gone, and the window goes on
  `Init::swapChain` (`nwh`, `ndt`, `width`, `height`), not on `platformData`.
  `reset(w, h, flags)` is now `reset(flags, &swapChain)`; conventions.md has more.
- **bgfx.cmake does not pass its include directories on** to a target outside it. The demo
  names `bgfx/include` and `bx/include` itself, plus `bx/include/compat/mingw` for Windows.
- **Shaders compile on the Mac.** The `shaderc` that MU2's build already makes
  (`build/extern/bgfx.cmake/cmake/bgfx/shaderc`) writes SPIR-V with
  `--platform windows -p spirv`, and the same `.bin` works on Linux. Direct3D 11/12 bytecode
  is the awkward one: it needs Microsoft's compiler, so a Windows runner, or shaderc under
  Wine.
- **A process started with `&` in a tool's shell dies when the shell exits.** Use
  `(nohup wine ... &)` or a background task, or the window closes after a second.

## What a real port of the game would need

From the survey on the same day; it was not attempted.

- `src/gfx/window.cpp`: the renderer type (hard-coded Metal), and the native window handle
  per platform as above.
- `CMakeLists.txt`: shaders built for `spirv` beside `metal`; the Cocoa/Metal frameworks and
  the app bundle only on Apple; GLFW from source or prebuilt instead of Homebrew.
- `src/gfx/metalfx.mm`: Apple only. Elsewhere, `--scale` would stretch without MetalFX.
- `src/core/watch.cpp`, the freeze and crash watchdog: `pthread_kill`/`SIGUSR2`,
  `posix_spawn` and `execinfo`. This mostly works on Linux; Windows needs its own version or
  a stub at first. `src/core/log.cpp` includes `unistd.h`.
- `MU2_ROOT_DIR` is an absolute Mac path baked into the binary. It works under Wine (the Mac
  disk is `Z:\`), but a shipped build needs paths relative to the program.
- No change needed: the cooked textures (BC7/BC5 `.ktx`, read by every desktop GPU),
  miniaudio for sound, and depth conventions (the code already reads
  `caps->homogeneousDepth`).
- After that, the usual mingw/gcc compile fixes across 83k lines.

Estimate: a session or two to cross-compile the game and smoke-test it under Wine on Vulkan.
Real judging, and a Direct3D path, need an actual Windows PC or VM; a GitHub Actions
`windows-latest` runner can at least build it and compile the D3D shaders.
