# The rules' source, folded into one number (net::rulesPrint): every file of src/sim and the
# tables' reader, each by its path and its text with any \r taken out, so a Windows checkout
# folds as the Mac's and the box's do. Run by the build (CMakeLists.txt, mu_net) whenever one
# of them changes:
#
#   cmake -DSRC=<MU2_BGFX> -DOUT=<header> -P src/net/rules_print.cmake
#
# The header is written only when the number moves, so wire.cpp is the one file rebuilt.
file(GLOB files RELATIVE ${SRC}
  ${SRC}/src/sim/*.h ${SRC}/src/sim/*.cpp ${SRC}/src/sim/*.inc
  ${SRC}/src/content/tables.h ${SRC}/src/content/tables.cpp)
list(SORT files)
set(all "")
foreach(f IN LISTS files)
  file(READ ${SRC}/${f} text)
  string(REPLACE "\r" "" text "${text}")
  string(SHA256 one "${text}")
  string(APPEND all "${f} ${one}\n")
endforeach()
string(SHA256 sum "${all}")
string(SUBSTRING ${sum} 0 16 print)
list(LENGTH files count)
set(header "// Made by src/net/rules_print.cmake from ${count} files: never edit.
#pragma once
#include <cstdint>
namespace mu::net {
constexpr uint64_t kRulesSource = 0x${print}ull;
}
")
set(old "")
if(EXISTS ${OUT})
  file(READ ${OUT} old)
endif()
if(NOT old STREQUAL header)
  file(WRITE ${OUT} "${header}")
endif()
