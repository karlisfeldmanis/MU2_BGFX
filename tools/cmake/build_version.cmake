# Writes build_version.h with the commit this build was made from, for the loading screen's
# corner (app/preloader.cpp). Run on every build by the build_version target; the header is
# rewritten only when the line changes, so an unchanged commit recompiles nothing.
execute_process(COMMAND git log -1 --format=%h%x20%cs
                WORKING_DIRECTORY ${SOURCE}
                OUTPUT_VARIABLE COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT COMMIT)
  set(COMMIT "unknown")
endif()
string(REPLACE " " ";" PARTS "${COMMIT}")
list(GET PARTS 0 HASH)
list(LENGTH PARTS COUNT)
if(COUNT GREATER 1)
  list(GET PARTS 1 DAY)
  set(LINE "v${VERSION} (${HASH}, ${DAY})")
else()
  set(LINE "v${VERSION} (${HASH})")
endif()
set(TEXT "#pragma once\n// Written by tools/cmake/build_version.cmake on every build.\n#define MU2_BUILD_VERSION \"${LINE}\"\n")
if(EXISTS ${OUT})
  file(READ ${OUT} OLD)
endif()
if(NOT "${OLD}" STREQUAL "${TEXT}")
  file(WRITE ${OUT} "${TEXT}")
endif()
