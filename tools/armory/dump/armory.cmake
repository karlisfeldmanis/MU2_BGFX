# The Armory's dump (tools/armory/dump, docs/plans/armory.md): the game's own rules linked in, the
# release's packs read. Included by MU2_BGFX/CMakeLists.txt; tools/armory/dump.py also hands it to
# a snapshot of an older release whose CMakeLists.txt does not include it yet, as
# CMAKE_PROJECT_mu2_INCLUDE (after project(), before the libraries it links are declared, which
# CMake resolves at generate time).
if(TARGET armory_dump OR MU2_SERVER_ONLY)
  return()
endif()
set(MU_ARMORY_DUMP ${CMAKE_CURRENT_LIST_DIR})
add_executable(armory_dump EXCLUDE_FROM_ALL
  ${MU_ARMORY_DUMP}/main.cpp
  ${MU_ARMORY_DUMP}/json.cpp
  ${MU_ARMORY_DUMP}/cards.cpp
  ${MU_ARMORY_DUMP}/systems.cpp
  ${MU_ARMORY_DUMP}/drops.cpp
  ${MU_ARMORY_DUMP}/rules.cpp
  ${MU_ARMORY_DUMP}/odds.cpp
  ${MU_ARMORY_DUMP}/look.cpp
  # The card builder is the game's, and reads nothing above sim and bgfx's headers.
  ${CMAKE_SOURCE_DIR}/src/game/ui/describe.cpp
)
target_link_libraries(armory_dump PRIVATE mu_sim mu_content_data mu_core mu_pack)
target_include_directories(armory_dump PRIVATE ${CMAKE_SOURCE_DIR}/src
                                               $<TARGET_PROPERTY:bgfx,INTERFACE_INCLUDE_DIRECTORIES>
                                               $<TARGET_PROPERTY:bx,INTERFACE_INCLUDE_DIRECTORIES>
                                               $<TARGET_PROPERTY:bimg,INTERFACE_INCLUDE_DIRECTORIES>)
# Set here as well: included as a project include, it comes before CMakeLists.txt's own.
set_target_properties(armory_dump PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(armory_dump PRIVATE -ffp-contract=off -Wall -Wextra -Wno-unused-parameter)
# play_tuning.h reaches bx's headers, which ask for this; nothing of bx is linked.
set_source_files_properties(${MU_ARMORY_DUMP}/look.cpp PROPERTIES COMPILE_DEFINITIONS BX_CONFIG_DEBUG=0)
