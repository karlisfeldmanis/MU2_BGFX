cmake_minimum_required(VERSION 3.22)
# The oldest macOS the Mac's binaries open on, the game's Info.plist's LSMinimumSystemVersion.
# Unset, clang stamps the SDK's own -- the build Mac's macOS -- into every binary, and Finder
# refuses the app on any older one as "incompatible with this version of macOS" (-10825): a
# tester on 26.6 met that with a launcher built on 27, 2026-10-20. Before project(), which reads
# it; a plain variable, so a build folder configured with it empty takes it too.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0)
endif()
project(mu2 LANGUAGES C CXX)
# What mu bootstrap fetches: bgfx, the headers, zstd and the faces (docs/plans/layout.md, third_party/).
set(MU_EXTERN ${CMAKE_SOURCE_DIR}/third_party/extern)
# The server's own build, on a box with no screen (server/README.md phase 4; the Hetzner
# box): core, the content it reads, the sim and their tests, and no bgfx, glfw, shader or
# Metal. `cmake -S . -B build-server -G Ninja -DMU2_SERVER_ONLY=ON`.
option(MU2_SERVER_ONLY "build only what a server links: core, content data, sim, tests" OFF)
# The player's build (docs/plans/layout.md, "What a player is given"): the game and nothing else -- no
# studio, bench or developer switch -- reading its data from the packs inside the bundle and
# writing into the player's own folder. Configured in a folder of its own by `./mu dist`.
option(MU2_SHIP "the player's build: lobby only, player switches only, data from packs" OFF)
# Windows is only ever the player's client (docs/plans/windows-client.md): the studio, the bench
# and the server stay on the Mac and the box.
if(WIN32 AND NOT MU2_SERVER_ONLY)
  set(MU2_SHIP ON)
endif()
# Linux's client is only ever the player's too (docs/plans/linux-release.md), built by `./mu build
# linux` in the x86_64 Ubuntu 22.04 machine. A switch of its own, so the box's server build and
# every other Linux build stay as they are.
option(MU2_LINUX_CLIENT "the player's client for Linux: Vulkan, X11 and Wayland" OFF)
if(MU2_LINUX_CLIENT)
  if(APPLE OR WIN32 OR MU2_SERVER_ONLY)
    message(FATAL_ERROR "MU2_LINUX_CLIENT is a Linux client build")
  endif()
  set(MU2_SHIP ON)
endif()
if(MU2_SHIP)
  add_compile_definitions(MU2_SHIP)
endif()
if(APPLE AND NOT MU2_SERVER_ONLY)
  # OBJC for glfw's .m files: with only OBJCXX, CMake compiles them as Objective-C++, which refuses them.
  enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
# No fused multiply-add, anywhere: Apple's clang fuses a*b+c into one rounding on ARM and GCC on
# x86 does not, so the same rule gave different bits on the Mac and on the server (server/README.md).
# The client's mirror steps the server's realm in lockstep from phase 4 and must agree with it to
# the bit; the rules' own transcendentals are sim/fmath.h. Whole build, because a rule inlined in a
# header is compiled into the client's files too.
add_compile_options(-ffp-contract=off)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows (docs/plans/windows-client.md): one static .exe, no mingw DLLs beside it, and
# <windows.h> without its min/max macros, wherever a file includes it.
if(WIN32)
  # -O2, not -O3: mingw's GCC 15.2 vectorised Renderer::setPointLights at -O3 with a runtime
  # overlap check that read each light's colour four bytes early, so every lamp lit in
  # (position y, position z, reach) -- negative green, the bloom blew it into white blobs. -O2
  # vectorises only loops that need no such check. The rules' results are the same at either
  # level (-ffp-contract=off, no fast math); sim_test.exe proves it (docs/plans/windows-client.md).
  foreach(lang C CXX)
    string(REPLACE "-O3" "-O2" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
  endforeach()
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
    _POSIX_THREAD_SAFE_FUNCTIONS)   # localtime_r and gmtime_r, as on the Mac and the server
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()
# Linux: -O2 for the reason Windows is (the same GCC 15.2), and the C++ runtime inside the
# program, so a player's distro and its libstdc++ do not matter; glibc stays the system's, and
# building on 22.04's 2.35 is what lets the one program run on every newer one.
if(MU2_LINUX_CLIENT)
  foreach(lang C CXX)
    string(REPLACE "-O3" "-O2" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
  endforeach()
  add_link_options(-static-libgcc -static-libstdc++)
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# Ninja is what mu build generates. ccache, when it is installed, makes a rebuild after a
# header change a matter of seconds; bgfx alone is a few hundred translation units.
find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
endif()

if(NOT MU2_SERVER_ONLY)
# ---- bgfx, bx and bimg (extern/, fetched by mu bootstrap) --------------------------------
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)        # shaderc, for the shaders below
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
# The renderer still runs on the one thread the game submits from -- Window::open calls
# bgfx::renderFrame() before bgfx::init, which is bgfx's own switch for that -- so there is no
# render thread and no frame of latency. The build is multithreaded for one thing only: it is
# what compiles in bgfx's resource lock, so the preloader's worker may create textures and
# buffers while the main thread keeps presenting the spinner. src/main.cpp, "The preloader".
set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "" FORCE)
# Cross-compiled for Windows (docs/plans/windows-client.md), shaderc would be a Windows program
# this Mac cannot run: the Mac's own builds the shaders, named by MU2_HOST_SHADERC (`./mu build
# windows` passes the one out/build made). bgfx's shader functions exist once a bgfx::shaderc does.
if(CMAKE_CROSSCOMPILING)
  set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  if(NOT MU2_HOST_SHADERC)
    set(MU2_HOST_SHADERC ${CMAKE_SOURCE_DIR}/out/build/third_party/extern/bgfx.cmake/cmake/bgfx/shaderc)
  endif()
  add_executable(bgfx::shaderc IMPORTED GLOBAL)
  set_target_properties(bgfx::shaderc PROPERTIES IMPORTED_LOCATION ${MU2_HOST_SHADERC})
endif()
add_subdirectory(third_party/extern/bgfx.cmake EXCLUDE_FROM_ALL)
foreach(t bx bimg bimg_decode bimg_encode bgfx shaderc)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE -w)   # third-party warnings are not ours to fix
  endif()
endforeach()
# Direct3D 11's shaders (docs/plans/low-end-windows.md, P7) made on this Mac: shaderc hands the
# HLSL to Microsoft's own d3dcompiler_47.dll, run under Wine by bgfx's d3d4linux.exe (the copy
# beside shaderc is in the shaders block below). bgfx's header turns this on for Linux only;
# the code is the Mac's too. tools/cmake/d3d4mac holds the one header bgfx's copy lacks.
if(APPLE AND TARGET shaderc)
  find_program(MU_WINE wine)
  target_compile_definitions(shaderc PRIVATE SHADERC_CONFIG_HAS_D3DCOMPILER=1)
  if(MU_WINE)
    target_compile_definitions(shaderc PRIVATE D3D4LINUX_WINE="${MU_WINE}")
  endif()
  target_include_directories(shaderc PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/cmake/d3d4mac
    ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/d3d4linux/include
    ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/directx-headers/include/directx
    ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/directx-headers/include
    ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/directx-headers/include/wsl/stubs)
endif()

if(WIN32 OR APPLE OR MU2_LINUX_CLIENT)
  # GLFW 3.4 from source (mu bootstrap), static, for Windows' window and input -- and Linux's,
  # X11 and Wayland both, picked when the game starts. The Mac's too: Homebrew's dylib is built
  # for the macOS Homebrew bottled it on (26 in 2026-10), whatever CMAKE_OSX_DEPLOYMENT_TARGET says.
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  if(MU2_LINUX_CLIENT)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(${MU_EXTERN}/glfw ${CMAKE_BINARY_DIR}/glfw EXCLUDE_FROM_ALL)
  target_compile_options(glfw PRIVATE -w)
else()
  find_package(glfw3 3.3 REQUIRED)
endif()

# ---- shaders ------------------------------------------------------------------------------
# Metal on the Mac (docs/reference/conventions.md); SPIR-V for Vulkan on Windows
# (docs/plans/windows-client.md), and DXBC beside it for Direct3D 11, the renderer of the cards
# Vulkan does not serve (docs/plans/low-end-windows.md, P7). MU_SHADER_PROFILE is the folder
# the build names; the game turns spirv into dxbc when it runs on Direct3D 11.
if(WIN32)
  set(MU_SHADER_PROFILE spirv)
  set(MU_SHADER_PROFILES spirv s_5_0)
elseif(MU2_LINUX_CLIENT)
  set(MU_SHADER_PROFILE spirv)   # Vulkan only (docs/plans/linux-release.md)
  set(MU_SHADER_PROFILES spirv)
else()
  set(MU_SHADER_PROFILE metal)
  set(MU_SHADER_PROFILES metal)
endif()
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
# Where shaderc finds bgfx_shader.sh. Without it every shader that includes common.sh fails
# on the include and not on anything of ours.
set(BGFX_SHADER_INCLUDE_PATH ${MU_EXTERN}/bgfx.cmake/bgfx/src)
# CONFIGURE_DEPENDS so that adding a shader is adding a file. Without it the glob is read
# once at configure time and a new shader builds nothing, links nothing, and is discovered
# at run time as "cannot read fs_something.sc.bin".
file(GLOB VS_FILES CONFIGURE_DEPENDS ${SHADER_DIR}/vs_*.sc)
file(GLOB FS_FILES CONFIGURE_DEPENDS ${SHADER_DIR}/fs_*.sc)
if(VS_FILES)
  bgfx_compile_shaders(TYPE VERTEX SHADERS ${VS_FILES}
    VARYING_DEF ${SHADER_DIR}/varying.def.sc OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR VS_OUT PROFILES ${MU_SHADER_PROFILES})
endif()
if(FS_FILES)
  bgfx_compile_shaders(TYPE FRAGMENT SHADERS ${FS_FILES}
    VARYING_DEF ${SHADER_DIR}/varying.def.sc OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR FS_OUT PROFILES ${MU_SHADER_PROFILES})
endif()
# Compute: the town's scenery culled and drawn by the GPU itself (gfx/scenery.h).
file(GLOB CS_FILES CONFIGURE_DEPENDS ${SHADER_DIR}/cs_*.sc)
if(CS_FILES)
  bgfx_compile_shaders(TYPE COMPUTE SHADERS ${CS_FILES}
    VARYING_DEF ${SHADER_DIR}/varying.def.sc OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR CS_OUT PROFILES ${MU_SHADER_PROFILES})
endif()
# And the includes. bgfx_compile_shaders makes each shader depend on its own .sc and on
# varying.def.sc and on nothing else, so an edit to common.sh, shadow.sh or lights.sh rebuilt
# no shader at all and the run drew with the old one -- found in sprint 8a, when a falloff
# changed in lights.sh measured identical to the one it replaced. Every shader depends on every
# include: there are few of both and a rebuild of all of them takes seconds.
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_DIR}/*.sh)
foreach(out ${VS_OUT} ${FS_OUT} ${CS_OUT})
  add_custom_command(OUTPUT ${out} APPEND DEPENDS ${SHADER_INCLUDES})
  # SPIR-V's buffer types renamed apart from their variables, for MoltenVK under Wine
  # (tools/cmake/spirv_names.py).
  if(out MATCHES "/spirv/")
    add_custom_command(OUTPUT ${out} APPEND
      COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/cmake/spirv_names.py ${out}
      DEPENDS ${CMAKE_SOURCE_DIR}/tools/cmake/spirv_names.py)
  endif()
endforeach()
# Direct3D's compiler and the Wine program that runs it, where the Mac's shaderc looks for them:
# ../windows beside itself. Both ship in bgfx's tools/bin/windows.
if(WIN32)
  if(CMAKE_CROSSCOMPILING)
    get_filename_component(MU_SHADERC_DIR ${MU2_HOST_SHADERC} DIRECTORY)
  else()
    set(MU_SHADERC_DIR $<TARGET_FILE_DIR:shaderc>)
  endif()
  set(MU_D3D_TOOLS ${MU_EXTERN}/bgfx.cmake/bgfx/tools/bin/windows)
  set(MU_D3D4LINUX ${MU_SHADERC_DIR}/../windows/d3d4linux.exe ${MU_SHADERC_DIR}/../windows/d3dcompiler_47.dll)
  add_custom_command(OUTPUT ${MU_D3D4LINUX}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MU_SHADERC_DIR}/../windows
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MU_D3D_TOOLS}/d3d4linux.exe ${MU_D3D_TOOLS}/d3dcompiler_47.dll ${MU_SHADERC_DIR}/../windows
    DEPENDS ${MU_D3D_TOOLS}/d3d4linux.exe ${MU_D3D_TOOLS}/d3dcompiler_47.dll)
  foreach(out ${VS_OUT} ${FS_OUT} ${CS_OUT})
    add_custom_command(OUTPUT ${out} APPEND DEPENDS ${MU_D3D4LINUX})
  endforeach()
endif()
add_custom_target(mu2_shaders DEPENDS ${VS_OUT} ${FS_OUT} ${CS_OUT})

endif()  # NOT MU2_SERVER_ONLY: bgfx, glfw, shaders

# ---- the game -----------------------------------------------------------------------------
# The layers of docs/reference/architecture.md, one static library each, and each linking only what is
# below it. tools/layercheck.py enforces the include arrows in the `checks` target below; the
# link enforces the rest: mu_sim cannot reach a bgfx symbol because nothing it links has one.
# That is what the server needs -- core + content_data + sim, and no window (sprint 16, step 1,
# and server/README.md phase 4). Each source is listed once, here; the tools link libraries.
set(MU_WARNINGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# Every top-level folder, named once here (docs/plans/layout.md) and read back through core::folders().
# Moving one is a change to this block and nothing in src/.
set(MU_ROOT ${CMAKE_SOURCE_DIR})
# Everything a build, a cook or a run makes: deleting it costs a rebuild and a cook, nothing else.
set(MU_OUT ${MU_ROOT}/out)
set(MU_ASSETS ${MU_OUT}/assets)
set(MU_DIRS
  MU2_SHADER_DIR="${SHADER_OUT}/${MU_SHADER_PROFILE}"
  MU2_ASSET_DIR="${MU_ASSETS}"
  MU2_SHEET_DIR="${MU_ROOT}/content/sheets"
  MU2_ROOT_DIR="${MU_ROOT}"
  MU2_FONT_DIR="${MU_EXTERN}"
  MU2_CONTENT_DIR="${MU_ROOT}/content"
  MU2_WORK_DIR="${MU_OUT}/workshop"
  MU2_USER_DIR="${MU_OUT}/saves"
  MU2_SHOT_DIR="${MU_OUT}/shots"
  MU2_CRASH_DIR="${MU_OUT}/logs"
  MU2_LOG_FILE="${MU_OUT}/logs/mu2.log"
  MU2_RUN_DIR="${MU_OUT}/build"
)

# Logging, arguments, json, files. Depends on nothing.
add_library(mu_core STATIC
  src/core/log.cpp
  src/core/watch.cpp
  src/core/jobs.cpp
  src/core/args.cpp
  src/core/files.cpp
  src/core/json.cpp
  src/core/paths.cpp
  src/core/browser.cpp
)
target_include_directories(mu_core PUBLIC src)
# Windows' own black box: SEH and minidumps in place of signals and backtrace().
if(WIN32)
  set_source_files_properties(src/core/watch.cpp PROPERTIES HEADER_FILE_ONLY ON)
  target_sources(mu_core PRIVATE src/core/watch_win.cpp)
  target_link_libraries(mu_core PUBLIC dbghelp shell32 ole32)
endif()
target_compile_definitions(mu_core PRIVATE ${MU_DIRS})
target_compile_options(mu_core PRIVATE ${MU_WARNINGS})

# The player's packs (core/pack.h) and zstd under them. Apart from mu_core so that the server,
# which never reads a pack, builds without zstd: core::readFile asks the packs through a hook
# (core::setFileSource) that mounting them sets. The game links this; so does the packer.
if(NOT MU2_SERVER_ONLY)
  file(GLOB MU_ZSTD_SOURCES
    ${MU_EXTERN}/zstd/lib/common/*.c
    ${MU_EXTERN}/zstd/lib/compress/*.c
    ${MU_EXTERN}/zstd/lib/decompress/*.c)
  add_library(mu_zstd STATIC ${MU_ZSTD_SOURCES})
  target_include_directories(mu_zstd PUBLIC ${MU_EXTERN}/zstd/lib)
  target_compile_definitions(mu_zstd PRIVATE ZSTD_DISABLE_ASM)
  add_library(mu_pack STATIC src/core/pack.cpp)
  target_link_libraries(mu_pack PUBLIC mu_core mu_zstd)
  target_compile_options(mu_pack PRIVATE ${MU_WARNINGS})
endif()

# What the cook wrote, read back, with no GPU in it: the tables, the grids, the cooked meshes'
# bytes, the showing and the missiles. The half of content a server reads.
add_library(mu_content_data STATIC
  src/content/cooked.cpp
  src/content/placement.cpp
  src/content/grid.cpp
  src/content/tables.cpp
  src/content/showing.cpp
  src/content/missiles.cpp
)
target_link_libraries(mu_content_data PUBLIC mu_core)
target_compile_options(mu_content_data PRIVATE ${MU_WARNINGS})

# The realm and its rules.
add_library(mu_sim STATIC
  src/sim/rules.cpp
  src/sim/swings.cpp
  src/sim/route.cpp
  src/sim/items.cpp
  src/sim/market.cpp
  src/sim/wear.cpp
  src/sim/vault.cpp
  src/sim/jeweler.cpp
  src/sim/talents.cpp
  src/sim/machine.cpp
  src/sim/gates.cpp
  src/sim/maps.cpp
  src/sim/realm_snapshot.cpp
  src/sim/traps.cpp
  src/sim/realm.cpp
  src/sim/realm_items.cpp
  src/sim/realm_jeweler.cpp
  src/sim/realm_elite.cpp
  src/sim/realm_machine.cpp
  src/sim/realm_deal.cpp
  src/sim/broker.cpp
  src/sim/realm_broker.cpp
  src/sim/realm_quests.cpp
  src/sim/realm_commands.cpp
  src/sim/cradle.cpp
  src/sim/quests.cpp
  src/sim/realm_travel.cpp
  src/sim/realm_move.cpp
  src/sim/realm_square.cpp
  src/sim/square_instance.cpp
  src/sim/castle_instance.cpp
  src/sim/realm_fight.cpp
  src/sim/realm_burn.cpp
  src/sim/realm_skills.cpp
  src/sim/realm_watch.cpp
  src/sim/realm_traps.cpp
  src/sim/realm_folk.cpp
  src/sim/realm_summon.cpp
  src/sim/invasion_instance.cpp
  src/sim/realm_party.cpp
  src/sim/realm_raid.cpp
  src/sim/raid_party.cpp
  src/sim/skills.cpp
  src/sim/audit.cpp
)
target_link_libraries(mu_sim PUBLIC mu_content_data)
target_compile_options(mu_sim PRIVATE ${MU_WARNINGS})

# The wire (sprint 18): TCP and the protocol, which the client's RemoteLink and
# the server both speak. Above sim, because a message carries a sim::Command.
add_library(mu_net STATIC
  src/net/socket.cpp
  src/net/wire.cpp
  src/net/broker_wire.cpp
  src/net/pass.cpp
  src/net/regions.cpp
  src/launcher/sha256.cpp
)
# zlib packs the Welcome's snapshot (net::pack): the system's on the Mac, zlib1g-dev on the box.
# Windows has no system zlib: mu bootstrap fetches its source, built here.
if(WIN32)
  file(GLOB MU_ZLIB_SOURCES ${MU_EXTERN}/zlib/*.c)
  add_library(mu_zlib STATIC ${MU_ZLIB_SOURCES})
  target_include_directories(mu_zlib PUBLIC ${MU_EXTERN}/zlib)
  target_compile_options(mu_zlib PRIVATE -w)
  add_library(ZLIB::ZLIB ALIAS mu_zlib)
else()
  find_package(ZLIB REQUIRED)
endif()
target_link_libraries(mu_net PUBLIC mu_sim ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(mu_net PUBLIC ws2_32)
endif()
# The rules' source folded into one number (net::rulesPrint, src/net/rules_print.cmake): a game
# built from other rules than the server's is refused, whatever its protocol says.
file(GLOB MU_RULES_SOURCES CONFIGURE_DEPENDS src/sim/*.h src/sim/*.cpp src/sim/*.inc
     src/content/tables.h src/content/tables.cpp)
set(MU_RULES_PRINT ${CMAKE_BINARY_DIR}/generated/net/rules_print.h)
add_custom_command(OUTPUT ${MU_RULES_PRINT}
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${MU_RULES_PRINT}
          -P ${CMAKE_SOURCE_DIR}/src/net/rules_print.cmake
  DEPENDS ${MU_RULES_SOURCES} ${CMAKE_SOURCE_DIR}/src/net/rules_print.cmake
  VERBATIM)
target_sources(mu_net PRIVATE ${MU_RULES_PRINT})
target_include_directories(mu_net PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_options(mu_net PRIVATE ${MU_WARNINGS})

# The server (server/src, server/README.md): every map's realm for whoever connects, no window.
# Built in both modes, so the Mac can run one for the client beside it.
# Its characters live in a SQLite file of its own (server/src/store.h): the system's library,
# which the macOS SDK has and the box from libsqlite3-dev. Never the client's (foundation 11).
# Not on Windows: the server runs on Linux, and a Windows build is the player's client.
if(NOT WIN32)
find_package(SQLite3 REQUIRED)
# CMake 4.3 renamed its target, and the box's is 4.2.
if(TARGET SQLite3::SQLite3)
  set(MU_SQLITE SQLite3::SQLite3)
else()
  set(MU_SQLITE SQLite::SQLite3)
endif()
add_executable(mu2_server
  server/src/main.cpp
  server/src/world.cpp
  server/src/chat.cpp
  server/src/party.cpp
  server/src/guild.cpp
  server/src/guild_store.cpp
  server/src/accounts.cpp
  server/src/signin.cpp
  server/src/store.cpp
  server/src/economy.cpp
  server/src/broker.cpp
  server/src/keeper.cpp
  server/src/market_link.cpp
  server/src/transfer.cpp
)
target_include_directories(mu2_server PRIVATE src)
target_link_libraries(mu2_server PRIVATE mu_net ${MU_SQLITE})
target_compile_definitions(mu2_server PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(mu2_server PRIVATE ${MU_WARNINGS})

# The server's own checks, with no socket: the store, the account's rules, the chat's allowance,
# and a world raised by the server against a mirror raised from its Welcome (docs/plans/order-plan.md
# sprint C). Its jobs' files, without main.cpp's loop.
add_executable(server_test EXCLUDE_FROM_ALL
  tests/server_test.cpp
  server/src/world.cpp
  server/src/chat.cpp
  server/src/party.cpp
  server/src/guild.cpp
  server/src/guild_store.cpp
  server/src/accounts.cpp
  server/src/signin.cpp
  server/src/store.cpp
  server/src/economy.cpp
  server/src/broker.cpp
  server/src/keeper.cpp
  server/src/market_link.cpp
  server/src/transfer.cpp
)
target_include_directories(server_test PRIVATE src server/src)
target_link_libraries(server_test PRIVATE mu_net ${MU_SQLITE})
target_compile_definitions(server_test PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(server_test PRIVATE ${MU_WARNINGS})
endif()  # NOT WIN32: the server

if(NOT MU2_SERVER_ONLY)
# The half of content that uploads: textures, meshes and the ground, into bgfx.
add_library(mu_content_gpu STATIC
  src/content/texture.cpp
  src/content/mesh.cpp
  src/content/ground.cpp
)
target_include_directories(mu_content_gpu PUBLIC ${MU_EXTERN})   # cgltf, miniaudio, stb
target_link_libraries(mu_content_gpu PUBLIC mu_content_data bgfx bimg_decode)
target_compile_options(mu_content_gpu PRIVATE ${MU_WARNINGS})

add_library(mu_gfx STATIC
  src/gfx/window.cpp
  src/gfx/views.cpp
  src/gfx/stats.cpp
  src/gfx/callback.cpp
  src/gfx/lighting.cpp
  src/gfx/renderer.cpp
  src/gfx/renderer_setup.cpp
  src/gfx/renderer_probe.cpp
  src/gfx/renderer_scenery.cpp
  src/gfx/scenery.cpp
  src/gfx/renderer_outline.cpp
  src/gfx/renderer_lights.cpp
  src/gfx/effects.cpp
  src/gfx/face.cpp
  src/gfx/overlay.cpp
  src/gfx/readout.cpp
  src/gfx/interface.cpp
  src/gfx/stage_pass.cpp
)
target_link_libraries(mu_gfx PUBLIC mu_content_gpu glfw)
target_compile_definitions(mu_gfx PRIVATE ${MU_DIRS})
target_compile_options(mu_gfx PRIVATE ${MU_WARNINGS})
if(APPLE)
  target_sources(mu_gfx PRIVATE src/gfx/metalfx.mm src/gfx/window_mac.mm)
  target_compile_definitions(mu_gfx PRIVATE GLFW_EXPOSE_NATIVE_COCOA)
elseif(WIN32)
  # No MetalFX and no menu bar: gfx/window_win.cpp says so (docs/plans/windows-client.md).
  target_sources(mu_gfx PRIVATE src/gfx/window_win.cpp)
  target_compile_definitions(mu_gfx PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
  target_include_directories(mu_gfx PUBLIC ${MU_EXTERN}/bgfx.cmake/bx/include/compat/mingw)
elseif(MU2_LINUX_CLIENT)
  # No MetalFX and no menu bar, as on Windows: gfx/window_linux.cpp.
  target_sources(mu_gfx PRIVATE src/gfx/window_linux.cpp)
  target_compile_definitions(mu_gfx PRIVATE GLFW_EXPOSE_NATIVE_X11 GLFW_EXPOSE_NATIVE_WAYLAND)
endif()

# MU2Launcher (docs/plans/launcher.md, L3): signs in on the website, installs and updates the game
# from the box, starts it. One exe: its two shaders, its fonts, the wallpaper and the logo are
# inside it (tools/cmake/embed.py). Drawn with the game's canvas (mu_gfx), none of the game linked.
# HTTPS is the system's: WinHTTP on Windows, libcurl on the Mac and Linux (opened at run time there,
# docs/plans/linux-release.md).
if(WIN32 OR APPLE OR MU2_LINUX_CLIENT)
  # Its two shaders for each renderer it may open on, by profile folder: spirv/ and dxbc/ on
  # Windows (Vulkan, else Direct3D 11), metal/ on the Mac. launcher/ui.cpp asks for its own.
  set(MU_LAUNCHER_EMBED)
  foreach(profile ${MU_SHADER_PROFILES})
    if(profile STREQUAL "s_5_0")
      set(profile dxbc)
    endif()
    list(APPEND MU_LAUNCHER_EMBED
      ${profile}/vs_overlay.sc.bin=${SHADER_OUT}/${profile}/vs_overlay.sc.bin
      ${profile}/fs_interface.sc.bin=${SHADER_OUT}/${profile}/fs_interface.sc.bin)
  endforeach()
  list(APPEND MU_LAUNCHER_EMBED
    OpenSans-SemiBold.ttf=${MU_EXTERN}/OpenSans-SemiBold.ttf
    AlegreyaSansSC-Bold.ttf=${MU_EXTERN}/AlegreyaSansSC-Bold.ttf
    AlegreyaSans-Medium.ttf=${MU_EXTERN}/AlegreyaSans-Medium.ttf
    wallpaper.jpg=${CMAKE_SOURCE_DIR}/src/launcher/art/wallpaper.jpg
    logo.png=${CMAKE_SOURCE_DIR}/src/launcher/art/logo.png)
  set(MU_LAUNCHER_EMBED_FILES)
  foreach(pair ${MU_LAUNCHER_EMBED})
    string(REGEX REPLACE "^[^=]*=" "" file ${pair})
    list(APPEND MU_LAUNCHER_EMBED_FILES ${file})
  endforeach()
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/launcher_embedded.cpp
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/cmake/embed.py ${CMAKE_BINARY_DIR}/generated/launcher_embedded.cpp ${MU_LAUNCHER_EMBED}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/cmake/embed.py ${MU_LAUNCHER_EMBED_FILES} mu2_shaders
    COMMENT "the launcher's files, inside it")
  # HTTPS and the crash reports sent home (launcher/crash_upload.h): the launcher's, and linked
  # by the game too, which sends what is waiting when it was started without the launcher.
  add_library(mu_report STATIC src/launcher/crash_upload.cpp src/launcher/bug_report.cpp)
  target_link_libraries(mu_report PUBLIC mu_core)
  target_compile_options(mu_report PRIVATE ${MU_WARNINGS})
  if(WIN32)
    target_sources(mu_report PRIVATE src/launcher/https_win.cpp)
    target_link_libraries(mu_report PUBLIC winhttp)
  elseif(MU2_LINUX_CLIENT)
    # libcurl's header only: the library is opened when a report goes (launcher/https_curl.cpp),
    # so the game starts on a distro whose libcurl is built apart from the one built against.
    find_package(CURL REQUIRED)
    target_sources(mu_report PRIVATE src/launcher/https_curl.cpp)
    target_include_directories(mu_report PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(mu_report PUBLIC ${CMAKE_DL_LIBS})
  else()
    find_package(CURL REQUIRED)
    target_sources(mu_report PRIVATE src/launcher/https_curl.cpp)
    target_link_libraries(mu_report PUBLIC CURL::libcurl)
  endif()
  if(WIN32 OR APPLE OR MU2_LINUX_CLIENT)
  add_executable(mu2_launcher
    src/launcher/main.cpp
    src/launcher/ui.cpp
    src/launcher/engine.cpp
    src/launcher/self_update.cpp
    src/launcher/sha256.cpp
    ${CMAKE_BINARY_DIR}/generated/launcher_embedded.cpp
  )
  add_dependencies(mu2_launcher mu2_shaders)
  # mu_net: the regions and their pings (net/regions.cpp), net::Socket's connect.
  target_link_libraries(mu2_launcher PRIVATE mu_gfx mu_core mu_report mu_net)
  target_compile_options(mu2_launcher PRIVATE ${MU_WARNINGS})
  if(WIN32)
    target_sources(mu2_launcher PRIVATE src/launcher/gpu_win.cpp src/launcher/launcher.rc)
    set_source_files_properties(src/launcher/launcher.rc PROPERTIES
      OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/launcher/launcher.manifest;${CMAKE_SOURCE_DIR}/src/app/mu2.ico")
    target_link_libraries(mu2_launcher PRIVATE crypt32 shell32 ole32 uuid dxgi setupapi advapi32)
    set_target_properties(mu2_launcher PROPERTIES OUTPUT_NAME MU2Launcher WIN32_EXECUTABLE TRUE)
  elseif(MU2_LINUX_CLIENT)
    # Its probe asks Vulkan itself, opened by hand as bgfx opens it, with the headers bgfx carries
    # (docs/plans/linux-release.md, X5).
    target_sources(mu2_launcher PRIVATE src/launcher/gpu_linux.cpp)
    target_include_directories(mu2_launcher PRIVATE ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/khronos)
    target_link_libraries(mu2_launcher PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(mu2_launcher PROPERTIES OUTPUT_NAME MU2Launcher)
  else()
    target_sources(mu2_launcher PRIVATE src/launcher/gpu_mac.mm)
    target_link_libraries(mu2_launcher PRIVATE "-framework Cocoa" "-framework Metal"
      "-framework QuartzCore" "-framework IOKit")
    set_target_properties(mu2_launcher PROPERTIES OUTPUT_NAME MU2Launcher)
  endif()
  endif()  # the launcher
endif()

# The client's line to the realm (src/client, docs/plans/order-plan.md sprint C): the Link, the
# in-process LocalLink and the RemoteLink a mirror plays the server's ticks through. Below game,
# so the net bot links it without the renderer.
add_library(mu_client STATIC
  src/client/remote_link.cpp
)
target_link_libraries(mu_client PUBLIC mu_net)
target_include_directories(mu_client PUBLIC src)
target_compile_options(mu_client PRIVATE ${MU_WARNINGS})

add_library(mu_game STATIC
  src/game/bench.cpp
  src/game/bench_reload.cpp
  src/game/studio_stage.cpp
  src/game/stand_show.cpp
  src/game/studio_folk.cpp
  src/game/world/world.cpp
  src/game/world/maps.cpp
  src/game/world/world_show.cpp
  src/game/world/weather.cpp
  src/game/world/portal.cpp
  src/game/world/town.cpp
  src/game/world/grass.cpp
  src/game/world/lamps.cpp
  src/game/world/void_clouds.cpp
  src/game/world/castle_sparks.cpp
  src/game/world/crystal_sparks.cpp
  src/game/world/ruin_effects.cpp
  src/game/world/desert_vents.cpp
  src/game/world/waterfall_spray.cpp
  src/game/world/chasm_mist.cpp
  src/game/world/small_fires.cpp
  src/game/world/skull_ghosts.cpp
  src/game/world/sky_clouds.cpp
  src/game/world/sand_haze.cpp
  src/game/world/bubbles.cpp
  src/game/world/sway.cpp
  src/game/world/doors.cpp
  src/game/world/drawbridge.cpp
  src/game/world/skulls.cpp
  src/game/world/flight.cpp
  src/game/world/boids.cpp
  src/game/invasion_sky.cpp
  src/game/world/scurry.cpp
  src/game/world/trap_show.cpp
  src/game/world/leaves.cpp
  src/game/save.cpp
  src/game/roster.cpp
  src/game/pedestals.cpp
  src/game/bust.cpp
  src/game/shine.cpp
  src/game/world/ornaments.cpp
  src/game/world/shades.cpp
  src/game/figures.cpp
  src/game/figure_shows.cpp
  src/game/crowd.cpp
  src/game/pets.cpp
  src/game/wings.cpp
  src/game/headless.cpp
  src/game/play.cpp
  src/game/play_invasion.cpp
  src/game/play_raid.cpp
  src/game/play_doll.cpp
  src/game/play_open.cpp
  src/game/play_sound.cpp
  src/game/play_show.cpp
  src/game/play_breeds.cpp
  src/game/play_ribbons.cpp
  src/game/play_pointer.cpp
  src/game/helper.cpp
  src/game/quest_auto.cpp
  src/game/play_jeweler.cpp
  src/game/play_requests.cpp
  src/game/fx/showing.cpp
  src/game/fx/marker.cpp
  src/game/fx/omen.cpp
  src/game/ui/arrival.cpp
  src/game/ui/reborn.cpp
  src/game/ui/rebirth_window.cpp
  src/game/ui/chat.cpp
  src/game/ui/party_bar.cpp
  src/game/ui/capsule.cpp
  src/game/ui/player_menu.cpp
  src/game/ui/inspect.cpp
  src/game/ui/helper_window.cpp
  src/game/ui/talent_window.cpp
  src/game/ui/quest_log.cpp
  src/game/ui/target_frame.cpp
  src/game/portrait.cpp
  src/game/fx/aura.cpp
  src/game/fx/set_shine.cpp
  src/game/fx/warp.cpp
  src/game/fx/rite.cpp
  src/game/fx/bones.cpp
  src/game/fx/breath.cpp
  src/game/fx/snort.cpp
  src/game/fx/dust.cpp
  src/game/fx/eyes.cpp
  src/game/fx/eye_trails.cpp
  src/game/fx/body_flames.cpp
  src/game/fx/staff_ring.cpp
  src/game/fx/staff_fire.cpp
  src/game/fx/held_lights.cpp
  src/game/fx/wing_motes.cpp
  src/game/fx/shadow_stars.cpp
  src/game/fx/golem_water.cpp
  src/game/fx/witch_queen.cpp
  src/game/fx/hell_maine.cpp
  src/game/fx/aida_sparks.cpp
  src/game/fx/tree_roots.cpp
  src/game/fx/forge.cpp
  src/game/fx/meteor.cpp
  src/game/fx/effect_mesh.cpp
  src/game/fx/wave.cpp
  src/game/fx/arrow.cpp
  src/game/fx/thunder.cpp
  src/game/fx/blink.cpp
  src/game/fx/ice.cpp
  src/game/fx/blizzard.cpp
  src/game/fx/weakness.cpp
  src/game/fx/pollution.cpp
  src/game/fx/stains.cpp
  src/game/fx/poison.cpp
  src/game/fx/flame.cpp
  src/game/fx/spirits.cpp
  src/game/fx/nova.cpp
  src/game/fx/firework.cpp
  src/game/fx/wheel.cpp
  src/game/fx/fury.cpp
  src/game/fx/hellfire.cpp
  src/game/fx/storm.cpp
  src/game/fx/inferno.cpp
  src/game/fx/comet.cpp
  src/game/fx/aqua.cpp
  src/game/fx/deathstab.cpp
  src/game/fx/impale.cpp
  src/game/fx/firebreath.cpp
  src/game/fx/bolt.cpp
  src/game/sound.cpp
  src/game/sound_files.cpp
  src/game/miniaudio.cpp
  src/game/ui/panel.cpp
  src/game/ui/sheet.cpp
  src/game/ui/cursor.cpp
  src/game/ui/hud.cpp
  src/game/ui/debuff_cards.cpp
  src/game/ui/card.cpp
  src/game/ui/bag.cpp
  src/game/ui/shelf.cpp
  src/game/ui/chest.cpp
  src/game/ui/deal.cpp
  src/game/ui/desk_deal.cpp
  src/game/ui/desk_split.cpp
  src/game/ui/broker.cpp
  src/game/ui/broker_draw.cpp
  src/game/ui/jeweler.cpp
  src/game/ui/mixer.cpp
  src/game/ui/amount.cpp
  src/game/ui/report.cpp
  src/game/ui/whats_new.cpp
  src/game/ui/square_result.cpp
  src/game/ui/menu.cpp
  src/game/ui/slab.cpp
  src/game/ui/lobby.cpp
  src/game/ui/endurance.cpp
  src/game/ui/describe.cpp
  src/game/ui/desk.cpp
  src/game/ui/tip.cpp
  src/game/ui/tally.cpp
  src/game/ui/vitals.cpp
  src/game/ui/boss_bar.cpp
  src/game/ui/speech.cpp
  src/game/ui/beacon.cpp
  src/game/ui/tracker.cpp
  src/game/ui/travel.cpp
  src/game/ui/go_back.cpp
  src/game/ui/line_chip.cpp
  src/game/ui/guild.cpp
  src/game/ui/herald.cpp
  src/game/ui/minimap.cpp
  src/game/ui/quest_dialog.cpp
  src/game/ui/controls.cpp
  src/game/ui/specimen.cpp
  src/game/ui/items_stage.cpp
  src/game/ui/browser_list.cpp
  src/game/item_models.cpp
  src/game/fx/litter.cpp
  src/game/fx/streak.cpp
  src/game/fx/gleam.cpp
  src/game/ui/outline.cpp
)
target_link_libraries(mu_game PUBLIC mu_gfx mu_sim mu_net mu_client mu_pack)
target_compile_definitions(mu_game PRIVATE ${MU_DIRS})
target_compile_options(mu_game PRIVATE ${MU_WARNINGS})
# miniaudio is one vendored header of tens of thousands of lines; its warnings are not ours.
set_source_files_properties(src/game/miniaudio.cpp PROPERTIES COMPILE_OPTIONS "-w")

# ---- the studio's panels (docs/plans/order-plan.md §9c, the Cooker) ------------------------------
# Dear ImGui, bgfx's own 3rdparty copy (so its version is the one mu bootstrap pins), copied
# into the build with src/gfx/gui_config.h as its imconfig.h: imgui.h includes the imconfig.h
# beside it whatever IMGUI_USER_CONFIG says, and bgfx's wants bx, its allocator and its widgets.
# Over it the studio: gfx/gui.cpp draws ImGui on bgfx in ViewGui, studio/ holds the cook's
# client and the panels. Only mu2 links them; a game frame never opens them, and the server
# never sees them.
set(IMGUI_FROM ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty/dear-imgui)
set(IMGUI_DIR ${CMAKE_BINARY_DIR}/imgui)
foreach(f imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui.h imgui_internal.h
          imstb_rectpack.h imstb_textedit.h imstb_truetype.h)
  configure_file(${IMGUI_FROM}/${f} ${IMGUI_DIR}/${f} COPYONLY)
endforeach()
configure_file(src/gfx/gui_config.h ${IMGUI_DIR}/imconfig.h COPYONLY)
add_library(mu_imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(mu_imgui PUBLIC ${IMGUI_DIR} src ${MU_EXTERN}/bgfx.cmake/bgfx/3rdparty)  # imstb_*.h are stb/*
target_compile_options(mu_imgui PRIVATE -w)

add_library(mu_studio STATIC
  src/gfx/gui.cpp
  src/studio/cooker.cpp
  src/studio/panels.cpp
  src/studio/checklist.cpp
  src/studio/inspector.cpp
  src/studio/light_panel.cpp
)
target_include_directories(mu_studio PRIVATE ${MU_EXTERN}/bgfx.cmake/bgfx/examples/common/imgui)
target_link_libraries(mu_studio PUBLIC mu_imgui mu_gfx)
target_compile_options(mu_studio PRIVATE ${MU_WARNINGS})

add_executable(mu2
  src/main.cpp
  src/app/alert.cpp
  src/app/application.cpp
  src/app/context.cpp
  src/app/preloader.cpp
  src/app/sweep.cpp
  src/app/tuner.cpp
  src/app/modes/play_mode.cpp
  src/app/modes/director.cpp
  src/app/modes/lobby_mode.cpp
)
# The bench and the studio are a developer's, and not in the player's build.
if(NOT MU2_SHIP)
  target_sources(mu2 PRIVATE
    src/app/modes/bench_mode.cpp
    src/app/modes/studio_desk.cpp
    src/app/modes/bench_scenes.cpp
    src/app/modes/studio_place.cpp
    src/app/modes/studio_marks.cpp
    src/app/modes/studio_paint.cpp
    src/app/modes/studio_arena.cpp
    src/app/modes/studio_control.cpp)
  target_link_libraries(mu2 PRIVATE mu_studio)
endif()
target_include_directories(mu2 PRIVATE src ${MU_EXTERN} ${CMAKE_BINARY_DIR}/generated)
# The game's version, MAJOR.MINOR.PATCH in VERSION (1.0.0 is the finished game; until then the
# 0.x line), and the commit this build was made from: the loading screen's corner
# (app/preloader.cpp) and the bundle's version.
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION MU2_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/VERSION)
add_custom_target(build_version
  COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR} -DVERSION=${MU2_VERSION}
          -DOUT=${CMAKE_BINARY_DIR}/generated/build_version.h
          -P ${CMAKE_SOURCE_DIR}/tools/cmake/build_version.cmake
  BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/build_version.h
  VERBATIM)
add_dependencies(mu2 build_version)
# bimg_encode writes the screenshots.
target_link_libraries(mu2 PRIVATE mu_game bimg_encode)
if(TARGET mu_report)
  target_link_libraries(mu2 PRIVATE mu_report)
  target_compile_definitions(mu2 PRIVATE MU2_CRASH_UPLOAD)
endif()
target_compile_definitions(mu2 PRIVATE ${MU_DIRS})
target_compile_options(mu2 PRIVATE ${MU_WARNINGS})
if(WIN32)
  # MU2.exe: no console window; the manifest and version (src/app/mu2.rc); binmode.o, so every
  # fopen and open is binary as on the Mac, not Windows' text mode that turns \n into \r\n;
  # winmm for timeBeginPeriod (src/main.cpp).
  string(REPLACE "." ";" MU2_VERSION_PARTS ${MU2_VERSION})
  list(GET MU2_VERSION_PARTS 0 MU2_VERSION_MAJOR)
  list(GET MU2_VERSION_PARTS 1 MU2_VERSION_MINOR)
  list(GET MU2_VERSION_PARTS 2 MU2_VERSION_PATCH)
  target_sources(mu2 PRIVATE src/app/mu2.rc)
  set_source_files_properties(src/app/mu2.rc PROPERTIES
    COMPILE_DEFINITIONS "MU2_VERSION_MAJOR=${MU2_VERSION_MAJOR};MU2_VERSION_MINOR=${MU2_VERSION_MINOR};MU2_VERSION_PATCH=${MU2_VERSION_PATCH}"
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/app/mu2.manifest;${CMAKE_SOURCE_DIR}/src/app/mu2.ico")
  # The launcher's version is the game's (src/launcher/launcher.rc).
  if(TARGET mu2_launcher)
    set_source_files_properties(src/launcher/launcher.rc PROPERTIES
      COMPILE_DEFINITIONS "MU2_VERSION_MAJOR=${MU2_VERSION_MAJOR};MU2_VERSION_MINOR=${MU2_VERSION_MINOR};MU2_VERSION_PATCH=${MU2_VERSION_PATCH}")
  endif()
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=binmode.o
                  OUTPUT_VARIABLE MU_BINMODE OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_link_libraries(mu2 PRIVATE ${MU_BINMODE} winmm)
  set_target_properties(mu2 PROPERTIES OUTPUT_NAME MU2 WIN32_EXECUTABLE TRUE)
endif()
if(MU2_LINUX_CLIENT)
  set_target_properties(mu2 PROPERTIES OUTPUT_NAME MU2)
endif()
if(APPLE)
  target_compile_definitions(mu2 PRIVATE GLFW_EXPOSE_NATIVE_COCOA)
  target_link_libraries(mu2 PRIVATE "-framework Cocoa" "-framework Metal" "-framework MetalFX" "-framework QuartzCore" "-framework IOKit")
  # The game is build/MU2.app, so macOS reads it as a game (src/app/Info.plist.in) and Game
  # Mode can take it. build/mu2 is a link to the binary inside, which is what every script
  # runs; macOS resolves the link and finds the bundle. A double click with no switches plays
  # as mu play does (src/main.cpp). The data stays in this folder: MU2_ROOT_DIR above.
  # The icon (tools/make_icons.py) in Contents/Resources, named by the plist's CFBundleIconFile.
  target_sources(mu2 PRIVATE src/app/mu2.icns)
  set_source_files_properties(src/app/mu2.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(mu2 PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME MU2
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/app/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME MU2
    MACOSX_BUNDLE_GUI_IDENTIFIER com.mu2.game
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${MU2_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION 1)
  add_custom_command(TARGET mu2 POST_BUILD
    COMMAND ln -sfh MU2.app/Contents/MacOS/MU2 ${CMAKE_BINARY_DIR}/mu2
    COMMAND codesign --force --sign - ${CMAKE_BINARY_DIR}/MU2.app
    VERBATIM)
endif()
add_dependencies(mu2 mu2_shaders)

# ---- the cook ------------------------------------------------------------------------------
# Built on demand and never by the game's own build: `cmake --build build --target texcook`,
# which is what pipeline/cook.py runs. It links bimg's encoders directly rather than turning
# BGFX_BUILD_TOOLS_TEXTURE on, because texturec builds its mip chain internally and the one
# rule this cook exists to keep -- a cutout's coverage held down the chain -- has to be
# applied between the levels. See the header of pipeline/cook/texcook.cpp.
add_executable(texcook EXCLUDE_FROM_ALL pipeline/cook/texcook.cpp)
# The packer: the player's .mupak files from `./mu dist`'s lists (tools/pack/mupack.cpp).
add_executable(mupack EXCLUDE_FROM_ALL tools/pack/mupack.cpp)
target_link_libraries(mupack PRIVATE mu_pack)
target_compile_options(mupack PRIVATE ${MU_WARNINGS})
target_link_libraries(texcook PRIVATE bimg_decode bimg_encode bimg bx)
target_compile_options(texcook PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Reads the cook's own output back and checks it against what it claims: formats, chains,
# and the coverage a cutout's mips are supposed to hold. `cmake --build build --target
# cookcheck`, then `build/cookcheck assets/cooked/lorencia`.
add_executable(cookcheck EXCLUDE_FROM_ALL pipeline/cook/cookcheck.cpp)
target_link_libraries(cookcheck PRIVATE bimg_decode bimg bx)
target_compile_options(cookcheck PRIVATE -Wall -Wextra -Wno-unused-parameter)

endif()  # NOT MU2_SERVER_ONLY: content_gpu, gfx, game, mu2, the cook

# ---- tests --------------------------------------------------------------------------------
# Foundation 9: what can be checked without a window is checked without one. The cooked
# formats are parsed by the engine's own reader here, which is a different failure from the
# one cookcheck hunts -- that one reads the files with a second implementation and catches a
# cook that wrote nonsense; this one catches a reader that disagrees with the cook.
add_executable(cooked_test EXCLUDE_FROM_ALL
  tests/cooked_test.cpp
)
target_link_libraries(cooked_test PRIVATE mu_content_data)
target_include_directories(cooked_test PRIVATE src)
target_compile_definitions(cooked_test PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(cooked_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(NOT MU2_SERVER_ONLY)
# The studio's half with no window: the cook's client fed mu serve's events and the panels
# drawn by ImGui into nothing, then the client against a real `mu serve` if one is running.
add_executable(cooker_test EXCLUDE_FROM_ALL
  tests/cooker_test.cpp
  src/studio/cooker.cpp
  src/studio/panels.cpp
  src/studio/checklist.cpp
  src/studio/inspector.cpp
  src/studio/light_panel.cpp
)
target_link_libraries(cooker_test PRIVATE mu_imgui mu_core)
target_compile_definitions(cooker_test PRIVATE MU2_ROOT_DIR="${MU_ROOT}")
target_compile_options(cooker_test PRIVATE ${MU_WARNINGS})

# The birds, whose every rule is arithmetic and whose every rule is nearly impossible to review
# from a picture: a flock arrives from off the frame, crosses in two seconds and then the sky is
# empty for fifteen to forty-five. `game/world/flight.cpp` is the half with no window in it, so
# this runs thousands of seconds of sky against a flat field in a millisecond.
add_executable(boids_test EXCLUDE_FROM_ALL
  tests/boids_test.cpp
  src/game/world/flight.cpp
)
target_include_directories(boids_test PRIVATE src)
target_compile_options(boids_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The mix, which cannot be looked at: the game's own Sound opened with no device over the
# cooked showing, played at, and measured -- pan, the frame's edge, the air, the merge and the
# budget. docs/reference/spatial-sound.md. Writes sound_test.wav beside itself for the ear.
add_executable(sound_test EXCLUDE_FROM_ALL
  tests/sound_test.cpp
  src/game/sound.cpp
  src/game/sound_files.cpp
  src/game/miniaudio.cpp
)
target_link_libraries(sound_test PRIVATE mu_content_data mu_pack)
target_include_directories(sound_test PRIVATE src ${MU_EXTERN})
target_compile_definitions(sound_test PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(sound_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(APPLE)
  target_link_libraries(sound_test PRIVATE "-framework CoreAudio" "-framework AudioToolbox"
    "-framework CoreFoundation")
endif()

endif()  # NOT MU2_SERVER_ONLY: the birds and the mix

# The placement transform against MU's own AngleMatrix. It was wrong twice, in the sign and
# then in the order, and both looked right on most of the map.
add_executable(placement_test EXCLUDE_FROM_ALL
  tests/placement_test.cpp
)
target_link_libraries(placement_test PRIVATE mu_content_data)
target_include_directories(placement_test PRIVATE src)
target_compile_options(placement_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Sprint 5's own: the rules against OpenMU's numbers, the router against the map, and two
# realms raised on one seed that must walk the same walk. Foundation 9 -- "the sim runs without
# a window" -- is what this target exists to keep true.
add_executable(sim_test EXCLUDE_FROM_ALL
  tests/sim_test.cpp
  tests/sim_test_broker.cpp
  tests/sim_test_jeweler.cpp
  tests/sim_test_rebirth.cpp
  tests/sim_test_elite.cpp
)
target_link_libraries(sim_test PRIVATE mu_net mu_sim)
target_include_directories(sim_test PRIVATE src)
target_compile_definitions(sim_test PRIVATE MU2_ASSET_DIR="${MU_ASSETS}"
                                          MU2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(sim_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Sprint 16, step 3: a save names quests, travel rows and skills by what they are, not where
# they sit. A version 2 character read, written as version 3 and read back the same, and read
# the same again with its quests reordered and an unknown one in them (tests/save_test.cpp).
# save.cpp is game's, but it reads nothing above core, content and sim.
add_executable(save_test EXCLUDE_FROM_ALL
  tests/save_test.cpp
  src/game/save.cpp
)
target_link_libraries(save_test PRIVATE mu_sim)
target_compile_definitions(save_test PRIVATE MU2_ASSET_DIR="${MU_ASSETS}"
                                             MU2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(save_test PRIVATE ${MU_WARNINGS})

# The Lorencia bot: a character played from level one with no window -- hunting, potions,
# loot, gear and the town's counters -- and what happened, when (tools/bot/bot.cpp).
add_executable(bot EXCLUDE_FROM_ALL
  tools/bot/bot.cpp
)
target_link_libraries(bot PRIVATE mu_sim)
target_include_directories(bot PRIVATE src)
target_compile_definitions(bot PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(bot PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The net bot: Lorencia characters played on the server as a client plays them -- a mirror of
# the server's world and nothing asked but commands -- for a world with people in it
# (tools/netbot/netbot.cpp). RemoteLink is the client's own (mu_client), linked without the renderer.
# The Mac's alone: the bots run here, and the box is sent neither netbot.cpp nor remote_link.cpp.
if(NOT MU2_SERVER_ONLY)
add_executable(netbot EXCLUDE_FROM_ALL
  tools/netbot/netbot.cpp
)
target_link_libraries(netbot PRIVATE mu_client)
target_include_directories(netbot PRIVATE src)
target_compile_definitions(netbot PRIVATE MU2_ASSET_DIR="${MU_ASSETS}")
target_compile_options(netbot PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Botwatch: a window over a netbot run -- the bots, the Broker, the economy log, the simulation's
# speed and the cores it takes -- that can start and stop a private run of its own
# (tools/botwatch/botwatch.cpp). ImGui on bgfx, as the studio's panels; the Mac's alone.
if(NOT MU2_SERVER_ONLY)
add_executable(botwatch EXCLUDE_FROM_ALL
  tools/botwatch/botwatch.cpp
)
target_link_libraries(botwatch PRIVATE mu_studio mu_sim)
target_include_directories(botwatch PRIVATE src)
target_compile_definitions(botwatch PRIVATE MU2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(botwatch PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Ship: a window over a release -- what is live, what HEAD adds, the steps with a Run button each,
# and the front page's Latest updates (tools/ship/ship.cpp; `./mu ship app`). It asks `./mu ship
# --json` for all it knows. ImGui on bgfx, as botwatch; the Mac's alone.
if(NOT MU2_SERVER_ONLY)
add_executable(mu_ship EXCLUDE_FROM_ALL
  tools/ship/ship.cpp
)
target_link_libraries(mu_ship PRIVATE mu_studio)
target_include_directories(mu_ship PRIVATE src)
target_compile_definitions(mu_ship PRIVATE MU2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(mu_ship PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# The raid, headless: the Golden Dragon against content/raid/party.json, played to a kill or a
# wipe as many times as asked (tools/raid/raid.cpp, docs/game/golden-dragon-raid.md §2a).
add_executable(raid EXCLUDE_FROM_ALL
  tools/raid/raid.cpp
)
target_link_libraries(raid PRIVATE mu_sim)
target_include_directories(raid PRIVATE src)
target_compile_definitions(raid PRIVATE MU2_ASSET_DIR="${MU_ASSETS}"
                                        MU2_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(raid PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The server build's own: every test that runs without a window, a sound device or the client's
# meshes (cooked_test reads those; a server has only the tables).
if(MU2_SERVER_ONLY AND WIN32)
  return()   # Windows' tests run under Wine, by hand (docs/plans/windows-client.md)
endif()
if(MU2_SERVER_ONLY)
  add_custom_target(checks
    DEPENDS placement_test sim_test save_test server_test
    COMMAND $<TARGET_FILE:placement_test>
    COMMAND $<TARGET_FILE:sim_test>
    COMMAND $<TARGET_FILE:save_test>
    COMMAND $<TARGET_FILE:server_test>
    COMMENT "the server build's checks"
    USES_TERMINAL)
  return()
endif()

# ---- checks ---------------------------------------------------------------------------------
# One target that runs everything which can say "no" without a window: the three tests, the
# cook's own reader, and the material audit. A check that has to be remembered is a check that
# is not run, and the material audit in particular exists to stop a list growing -- which it
# cannot do if nobody calls it. `cmake --build build --target checks`.
#
# matcheck is a gate rather than a report: it fails on a material fault that is not in
# content/sheets/materials.baseline.json, and on a recorded one that has since been fixed. See
# docs/content/materials.md.
#
# layercheck is the other gate, and it is here because the line above the source list --
# "gfx never includes game or sim, and sim never includes gfx; nothing enforces that but
# review" -- was a rule with nobody keeping it. It reads every include in src/ against the
# arrows in docs/reference/architecture.md and fails on one that is not allowed. It costs milliseconds.
#
# sizecheck is the third: a file over 800 lines (a header 1000) fails unless tools/sizecheck.allow
# lists it, and a listed one may never grow past its size there. It is what stops the hubs
# growing back (docs/plans/order-plan.md, sprint A).
# Not on Windows: the checks run on the Mac, and the Windows build's own (sim_test.exe under Wine)
# by hand (docs/plans/windows-client.md).
if(WIN32 OR MU2_LINUX_CLIENT)
  return()
endif()
add_custom_target(checks
  DEPENDS cooked_test placement_test sim_test boids_test sound_test save_test server_test cooker_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/layercheck.py
  COMMAND ${CMAKE_SOURCE_DIR}/tools/sizecheck.py
  COMMAND $<TARGET_FILE:cooked_test>
  COMMAND $<TARGET_FILE:placement_test>
  COMMAND $<TARGET_FILE:sim_test>
  COMMAND $<TARGET_FILE:boids_test>
  COMMAND $<TARGET_FILE:sound_test>
  COMMAND $<TARGET_FILE:save_test>
  COMMAND $<TARGET_FILE:server_test>
  COMMAND $<TARGET_FILE:cooker_test>
  COMMAND ${CMAKE_SOURCE_DIR}/tools/matcheck.py
  COMMENT "running every check that does not need a window"
  USES_TERMINAL
)

# And the one that DOES need a window, which is why it is its own target and not a line in
# `checks` above. It draws three pinned scenes and demands they come out exactly as the
# references in tests/reference/ -- the frame's answer to what `sim_test`'s fingerprint has
# done for the rules since sprint 5. Twenty seconds and a display.
#
# `cmake --build build --target shotcheck`, before a commit that touched the renderer, a
# shader, a pass or the frame loop. See the header of tools/shotcheck.py for what the three
# scenes cover and for the rule about blessing a reference.
add_custom_target(shotcheck
  DEPENDS mu2
  COMMAND ${CMAKE_SOURCE_DIR}/tools/shotcheck.py
  COMMENT "drawing the pinned scenes and comparing them to the references"
  USES_TERMINAL
)
