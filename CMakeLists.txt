cmake_minimum_required(VERSION 3.22)
project(mu2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# Ninja is what build.sh generates. ccache, when it is installed, makes a rebuild after a
# header change a matter of seconds; bgfx alone is a few hundred translation units.
find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
endif()

# ---- bgfx, bx and bimg (extern/, fetched by bootstrap.sh) --------------------------------
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
add_subdirectory(extern/bgfx.cmake EXCLUDE_FROM_ALL)
foreach(t bx bimg bimg_decode bimg_encode bgfx shaderc)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE -w)   # third-party warnings are not ours to fix
  endif()
endforeach()

find_package(glfw3 3.3 REQUIRED)

# ---- shaders ------------------------------------------------------------------------------
# Metal only, by decision; see docs/conventions.md.
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
# Where shaderc finds bgfx_shader.sh. Without it every shader that includes common.sh fails
# on the include and not on anything of ours.
set(BGFX_SHADER_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/extern/bgfx.cmake/bgfx/src)
# CONFIGURE_DEPENDS so that adding a shader is adding a file. Without it the glob is read
# once at configure time and a new shader builds nothing, links nothing, and is discovered
# at run time as "cannot read fs_something.sc.bin".
file(GLOB VS_FILES CONFIGURE_DEPENDS ${SHADER_DIR}/vs_*.sc)
file(GLOB FS_FILES CONFIGURE_DEPENDS ${SHADER_DIR}/fs_*.sc)
if(VS_FILES)
  bgfx_compile_shaders(TYPE VERTEX SHADERS ${VS_FILES}
    VARYING_DEF ${SHADER_DIR}/varying.def.sc OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR VS_OUT PROFILES metal)
endif()
if(FS_FILES)
  bgfx_compile_shaders(TYPE FRAGMENT SHADERS ${FS_FILES}
    VARYING_DEF ${SHADER_DIR}/varying.def.sc OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR FS_OUT PROFILES metal)
endif()
# And the includes. bgfx_compile_shaders makes each shader depend on its own .sc and on
# varying.def.sc and on nothing else, so an edit to common.sh, shadow.sh or lights.sh rebuilt
# no shader at all and the run drew with the old one -- found in sprint 8a, when a falloff
# changed in lights.sh measured identical to the one it replaced. Every shader depends on every
# include: there are few of both and a rebuild of all of them takes seconds.
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_DIR}/*.sh)
foreach(out ${VS_OUT} ${FS_OUT})
  add_custom_command(OUTPUT ${out} APPEND DEPENDS ${SHADER_INCLUDES})
endforeach()
add_custom_target(mu2_shaders DEPENDS ${VS_OUT} ${FS_OUT})

# ---- the game -----------------------------------------------------------------------------
# The layers of docs/architecture.md, and the rule that holds them apart: gfx never includes
# game or sim, and sim never includes gfx. tools/layercheck.py enforces it in the `checks`
# target below -- it used to say "nothing enforces that but review", and that was true.
add_executable(mu2
  src/main.cpp
  src/app/application.cpp
  src/app/context.cpp
  src/app/preloader.cpp
  src/app/modes/play_mode.cpp
  src/app/modes/bench_mode.cpp
  src/core/log.cpp
  src/core/args.cpp
  src/core/files.cpp
  src/core/json.cpp
  src/content/texture.cpp
  src/content/mesh.cpp
  src/content/ground.cpp
  src/content/cooked.cpp
  src/content/placement.cpp
  src/content/grid.cpp
  src/content/tables.cpp
  src/content/showing.cpp
  src/content/missiles.cpp
  src/gfx/window.cpp
  src/gfx/views.cpp
  src/gfx/stats.cpp
  src/gfx/callback.cpp
  src/gfx/lighting.cpp
  src/gfx/renderer.cpp
  src/gfx/renderer_setup.cpp
  src/gfx/renderer_probe.cpp
  src/gfx/renderer_outline.cpp
  src/gfx/renderer_lights.cpp
  src/gfx/effects.cpp
  src/gfx/face.cpp
  src/gfx/overlay.cpp
  src/gfx/readout.cpp
  src/gfx/interface.cpp
  src/gfx/stage_pass.cpp
  src/game/bench.cpp
  src/game/world/world.cpp
  src/game/world/town.cpp
  src/game/world/lamps.cpp
  src/game/world/sway.cpp
  src/game/save.cpp
  src/game/world/ornaments.cpp
  src/game/figures.cpp
  src/game/crowd.cpp
  src/game/headless.cpp
  src/game/play.cpp
  src/game/play_open.cpp
  src/game/play_sound.cpp
  src/game/play_show.cpp
  src/game/play_pointer.cpp
  src/game/play_requests.cpp
  src/game/fx/showing.cpp
  src/game/fx/marker.cpp
  src/game/ui/arrival.cpp
  src/game/fx/aura.cpp
  src/game/fx/bones.cpp
  src/game/fx/breath.cpp
  src/game/fx/meteor.cpp
  src/game/sound.cpp
  src/game/miniaudio.cpp
  src/game/ui/panel.cpp
  src/game/ui/cursor.cpp
  src/game/ui/hud.cpp
  src/game/ui/card.cpp
  src/game/ui/bag.cpp
  src/game/ui/shelf.cpp
  src/game/ui/describe.cpp
  src/game/ui/desk.cpp
  src/game/ui/tip.cpp
  src/game/ui/vitals.cpp
  src/game/ui/items_stage.cpp
  src/game/ui/browser_list.cpp
  src/game/item_models.cpp
  src/game/fx/litter.cpp
  src/game/ui/outline.cpp
  src/sim/rules.cpp
  src/sim/swings.cpp
  src/sim/route.cpp
  src/sim/items.cpp
  src/sim/market.cpp
  src/sim/realm.cpp
  src/sim/realm_items.cpp
  src/sim/realm_move.cpp
  src/sim/realm_fight.cpp
  src/sim/realm_skills.cpp
  src/sim/skills.cpp
  src/sim/audit.cpp
)
target_include_directories(mu2 PRIVATE src extern)
# bimg_decode reads the PNGs, bimg_encode writes the screenshots.
target_link_libraries(mu2 PRIVATE bgfx bimg_decode bimg_encode glfw)
target_compile_definitions(mu2 PRIVATE
  MU2_SHADER_DIR="${SHADER_OUT}/metal"
  MU2_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
  MU2_SHEET_DIR="${CMAKE_SOURCE_DIR}/sheets"
  MU2_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(mu2 PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# miniaudio is one vendored header of tens of thousands of lines; its warnings are not ours.
set_source_files_properties(src/game/miniaudio.cpp PROPERTIES COMPILE_OPTIONS "-w")
if(APPLE)
  target_compile_definitions(mu2 PRIVATE GLFW_EXPOSE_NATIVE_COCOA)
  target_link_libraries(mu2 PRIVATE "-framework Cocoa" "-framework Metal" "-framework QuartzCore" "-framework IOKit")
endif()
add_dependencies(mu2 mu2_shaders)

# ---- the cook ------------------------------------------------------------------------------
# Built on demand and never by the game's own build: `cmake --build build --target texcook`,
# which is what tools/cook.py runs. It links bimg's encoders directly rather than turning
# BGFX_BUILD_TOOLS_TEXTURE on, because texturec builds its mip chain internally and the one
# rule this cook exists to keep -- a cutout's coverage held down the chain -- has to be
# applied between the levels. See the header of tools/cook/texcook.cpp.
add_executable(texcook EXCLUDE_FROM_ALL tools/cook/texcook.cpp)
target_link_libraries(texcook PRIVATE bimg_decode bimg_encode bimg bx)
target_compile_options(texcook PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Reads the cook's own output back and checks it against what it claims: formats, chains,
# and the coverage a cutout's mips are supposed to hold. `cmake --build build --target
# cookcheck`, then `build/cookcheck assets/cooked/lorencia`.
add_executable(cookcheck EXCLUDE_FROM_ALL tools/cook/cookcheck.cpp)
target_link_libraries(cookcheck PRIVATE bimg_decode bimg bx)
target_compile_options(cookcheck PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- tests --------------------------------------------------------------------------------
# Foundation 9: what can be checked without a window is checked without one. The cooked
# formats are parsed by the engine's own reader here, which is a different failure from the
# one cookcheck hunts -- that one reads the files with a second implementation and catches a
# cook that wrote nonsense; this one catches a reader that disagrees with the cook.
add_executable(cooked_test EXCLUDE_FROM_ALL
  tests/cooked_test.cpp
  src/content/cooked.cpp
  src/content/showing.cpp
  src/content/missiles.cpp
  src/core/files.cpp
  src/core/log.cpp
)
target_include_directories(cooked_test PRIVATE src)
target_compile_definitions(cooked_test PRIVATE MU2_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_options(cooked_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The placement transform against MU's own AngleMatrix. It was wrong twice, in the sign and
# then in the order, and both looked right on most of the map.
add_executable(placement_test EXCLUDE_FROM_ALL
  tests/placement_test.cpp
  src/content/placement.cpp
)
target_include_directories(placement_test PRIVATE src)
target_compile_options(placement_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Sprint 5's own: the rules against OpenMU's numbers, the router against the map, and two
# realms raised on one seed that must walk the same walk. Foundation 9 -- "the sim runs without
# a window" -- is what this target exists to keep true.
add_executable(sim_test EXCLUDE_FROM_ALL
  tests/sim_test.cpp
  src/sim/rules.cpp
  src/sim/swings.cpp
  src/sim/route.cpp
  src/sim/items.cpp
  src/sim/market.cpp
  src/sim/realm.cpp
  src/sim/realm_items.cpp
  src/sim/realm_move.cpp
  src/sim/realm_fight.cpp
  src/sim/realm_skills.cpp
  src/sim/skills.cpp
  src/sim/audit.cpp
  src/content/grid.cpp
  src/content/tables.cpp
  src/core/files.cpp
  src/core/log.cpp
)
target_include_directories(sim_test PRIVATE src)
target_compile_definitions(sim_test PRIVATE MU2_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_compile_options(sim_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- checks ---------------------------------------------------------------------------------
# One target that runs everything which can say "no" without a window: the three tests, the
# cook's own reader, and the material audit. A check that has to be remembered is a check that
# is not run, and the material audit in particular exists to stop a list growing -- which it
# cannot do if nobody calls it. `cmake --build build --target checks`.
#
# matcheck is a gate rather than a report: it fails on a material fault that is not in
# sheets/materials.baseline.json, and on a recorded one that has since been fixed. See
# docs/materials.md.
#
# layercheck is the other gate, and it is here because the line above the source list --
# "gfx never includes game or sim, and sim never includes gfx; nothing enforces that but
# review" -- was a rule with nobody keeping it. It reads every include in src/ against the
# arrows in docs/architecture.md and fails on one that is not allowed. It costs milliseconds.
add_custom_target(checks
  DEPENDS cooked_test placement_test sim_test
  COMMAND ${CMAKE_SOURCE_DIR}/tools/layercheck.py
  COMMAND $<TARGET_FILE:cooked_test>
  COMMAND $<TARGET_FILE:placement_test>
  COMMAND $<TARGET_FILE:sim_test>
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
