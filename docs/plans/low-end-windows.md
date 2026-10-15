# MU2 on a weak Windows PC

2026-10-15. The user: "we need some plan how to optimise game for windows for potato PCs with
intergrated video cards".

**The goal:** a laptop with Intel UHD 620 / Iris Xe or an AMD Vega 3-8 APU plays Lorencia at a
steady 30 fps at 1280x720 or more, in a crowd. On a mid-range desktop GPU, the game holds 60.

**Where it stands today:**
- The launcher probes the adapter (src/launcher/gpu_win.cpp) and picks the first run's preset
  (gpu.h `presetFor`). An integrated card gets **Medium**: 85% scale, 2x MSAA, SSAO, bloom, one
  shadow level. Nothing has ever been measured on such a card. Medium is a guess, and probably
  two steps too heavy.
- Every number in docs/reference/budget.md is from this Mac (a 2560x1273 frame of 6.3 ms). About
  2.1 ms of that frame doesn't scale with pixels; the rest is fill: PCSS shadow taps, MSAA,
  lamps, the present's sharpen, the probe. An integrated Intel GPU has very roughly a tenth to a
  fifteenth of this Mac's fill. At 1080p on High, that is 40-100 ms a frame.
- The cooked data is 1.4 GB of BC7/BC5 textures, and every one goes to the GPU at full size. On an
  integrated card that is shared system memory, on a laptop with 8 GB total.
- We test Windows under Wine on the Mac, which tells us nothing about a weak GPU's speed.

## P1. Measure before changing anything

1. **A real weak machine.** Leo's PC or any Intel-graphics laptop. One run of the game with a
   frame log, the same Lorencia square every time, at each preset. Without this, every step
   below is a guess.
2. **The game reports its own frame.** Once per session, MU2.exe sends home its preset, its
   resolution and scale, and its median and 99th-percentile frame time in town and in a fight.
   It goes beside the launcher's machine report (`/api/machine`, machines.jsonl), with nothing
   about the account. This gives us every player's PC as a test bench, and it measures each step
   below.
3. **Read machines.jsonl** for the GPUs players already have. That decides which cards P3
   targets. (It is on the box; reading it needs the user's go.)

## P2. A "Lowest" preset and an honest first pick

**Built 2026-10-15** (the user: "we need more settings in client so users can turn off more fancy
staff ... for potato laptops with integrated UHD cards its not working on 60fps"). The Graphics
page now has Shadows **Off** (no sun pass; every lookup lit at once), **Point lights** On/Off
(live; flames and spell light stay), **Textures** Full/Half (Half drops each world texture's top
mip at load, from the next start), and the **Lowest** preset: 50%, no MSAA, shadows, SSAO,
bloom, reflections, grass or point lights, half textures. The launcher's first run: integrated
is Low, Intel UHD/HD Graphics Lowest. On this Mac in Lorencia's square at 1080p, Lowest is
2.5 ms of GPU against High's 7.4. Not done from the list below: the sharpen and the 60 fps cap
in Lowest, and no numbers from a real UHD card yet (P1).

Cheap, and it needs no new rendering: every switch below already exists.
- **Lowest**, for integrated cards: 50-60% scale, no MSAA, no shadows, no SSAO, no bloom, no
  reflections probe, grass thinned, no lamps' point lights (flames stay), sharpen off, and a
  60 fps cap (a laptop runs cooler and holds its clock).
- `presetFor`: integrated, which today means `dedicated` under 2 GB, opens on Lowest or Low by the
  adapter's name and generation (UHD 6xx and Vega 3 on Lowest; Iris Xe, 680M and 780M on Low).
  Fix the table once P1 has numbers.
- The Graphics page explains what each row costs, so a player knows what to turn off first.

## P3. Dynamic resolution

The scale is already a live setting (`args.scale`, the present magnifies the world, and the HUD
stays sharp). A small controller holds the frame time near the target by moving the scale
between 50% and the preset's own, in 5% steps, with hysteresis. It measures the GPU's time from
bgfx's own timers, not the wall clock, so a slow CPU doesn't drop the picture. On the Mac it
stays off unless asked for. This is the step that makes "steady 30" true on cards we have never
seen.

## P4. Cheaper paths in the shaders, for Lowest and Low

What budget.md says scales with pixels, made cheaper where it is turned down instead of off:
- **Shadows:** a hard 1-4 tap lookup on a 1024-2048 map, in place of the 13-tap PCSS. One
  shadow is worth keeping on a weak card; the soft one is not.
- **Shade:** a variant without the probe read and the lamp loop.
- **Bloom:** half the passes, at quarter resolution.
- **SSAO:** stays off on Lowest.

Each variant is a shader permutation picked at the preset, priced on the P1 machine against what
it costs the picture.

## P5. Memory: textures and draw count

- **Texture budget:** on Lowest and Low the loader skips each texture's top mip. A quarter of the
  memory and bandwidth, and at 720p it is barely seen. The cooked files stay the same, since a
  KTX holds its mips already.
- **Draw distance:** the town's placements, grass and figures fade nearer on Lowest.
- **CPU:** an integrated laptop has 2-4 slow cores. Check the 99th-percentile frame on it in a
  crowd (gear streaming, figures, the mirror) after P1, not before.

## P6. Windows itself

- **The integrated card when there is a better one.** On a laptop with both Intel and NVIDIA,
  MU2.exe already asks for the discrete one (`NvOptimusEnablement` and
  `AmdPowerXpressRequestHighPerformance`, exported by bgfx; src/main.cpp). Check that bgfx's
  Vulkan device then picks the discrete adapter too, and that the launcher's probe names the
  same card the game runs on. Otherwise, many "potato" reports are a gaming laptop running on its
  Intel chip.
- **Power:** the 60 fps cap by default on battery.
- **Drivers:** the launcher already says when the driver is years old. On Intel, an old driver
  is often a broken Vulkan driver too: say so plainly, with the download link.

## P7. Direct3D 11 beside Vulkan

**Built 2026-10-15** (the user: "lets do Direct3D"). bgfx already has the renderer; what was
missing was our shaders for it. Why: Intel's HD 4000-5500 have no Vulkan on Windows at all, so
the game could not start there; Intel's pre-Xe chips run a legacy driver whose Vulkan is its
weakest part; and the Vulkan swap chain is where our two Windows hangs were.

- **Shaders.** `cmake` on Windows makes `shaders/dxbc/` beside `shaders/spirv/` (profile
  `s_5_0`). The Mac's shaderc compiles them with Microsoft's own `d3dcompiler_47.dll`, which
  bgfx ships, run under Wine by bgfx's `d3d4linux.exe` (CMakeLists.txt, "Direct3D 11's
  shaders"; `tools/cmake/d3d4mac/sal.h` is the one header bgfx's copy lacks). Two shader fixes
  were needed, both drawing the same on Metal and Vulkan: `pass` is an HLSL keyword
  (shine.sh's `chromePass`), and fs_outline's taps take `at`'s gradients from outside its loops
  (`texture2DGrad`), since fxc cannot unroll loops that break.
- **Packs.** `mu dist windows` packs both (`shaders/spirv/`, `shaders/dxbc/`).
- **Choice.** `renderer` in options.txt, `--renderer auto|vulkan|d3d11`: 0 is Vulkan and, when
  Vulkan does not start, Direct3D 11 in the same run (gfx/window.cpp); 2 is Direct3D 11. The
  game reads the folder that matches what came up (`gfx::shadersFor`).
- **Launcher.** It draws itself through the same fallback, with both of its shader pairs inside
  it. The first run writes `renderer 2` on a low-end PC -- what the probe opens on Lowest or
  Low: an integrated card, no Vulkan loader or the software adapter (gpu.h `rendererFor`); its machine report carries `renderer`
  (web/server.py keeps it once deployed). "This driver can't run the game" is gone: no Vulkan
  is no longer that.
- **Not done:** a Renderer row on the Graphics page; numbers from a real card (P1). Under Wine
  on this Mac, Direct3D 11 runs through wined3d on OpenGL 4.1 -- no compute, so the scenery takes
  its CPU path (gfx/scenery.cpp checks the caps) -- which proves it starts and draws, not its speed.

## Order

P1 first, and nothing else until it has numbers. Then P2 and P6's discrete-card check (both
small, both shipped with the next release). Then P3. P4 and P5 only where P1's numbers say the
frame still misses 30.
