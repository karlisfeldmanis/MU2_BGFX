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
# One thread: the game submits and the renderer runs on the same one. That removes the frame
# of latency bgfx's render thread adds and the copy of every draw into its queue.
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
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
add_custom_target(mu2_shaders DEPENDS ${VS_OUT} ${FS_OUT})

# ---- the game -----------------------------------------------------------------------------
# The layers of PLAN.md, and the rule that holds them apart: gfx never includes game or sim,
# and sim never includes gfx. Nothing enforces that but review.
add_executable(mu2
  src/main.cpp
  src/core/log.cpp
  src/core/args.cpp
  src/core/files.cpp
  src/core/json.cpp
  src/content/texture.cpp
  src/content/mesh.cpp
  src/content/ground.cpp
  src/content/cooked.cpp
  src/content/placement.cpp
  src/gfx/window.cpp
  src/gfx/views.cpp
  src/gfx/stats.cpp
  src/gfx/callback.cpp
  src/gfx/lighting.cpp
  src/gfx/renderer.cpp
  src/game/bench.cpp
  src/game/world.cpp
  src/game/town.cpp
  src/game/figures.cpp
  src/game/crowd.cpp
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
