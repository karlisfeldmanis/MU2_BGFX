# The Windows client, cross-compiled on the Mac by Homebrew's mingw-w64 (docs/plans/windows-client.md):
# `./mu build windows`, or cmake -DCMAKE_TOOLCHAIN_FILE=tools/cmake/mingw.cmake. GCC, as the
# server is, so the lockstep rules hold in one compiler family.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
