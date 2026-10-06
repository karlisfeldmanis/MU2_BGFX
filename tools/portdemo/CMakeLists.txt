# The port test, docs/windows-linux-test.md. Built by build.sh: cross-compiled for Windows with
# mingw.cmake, or natively inside a Linux machine. bgfx is MU2's own copy in extern/.
cmake_minimum_required(VERSION 3.16)
project(portdemo CXX C)
set(CMAKE_CXX_STANDARD 20)
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
get_filename_component(EXTERN ${CMAKE_SOURCE_DIR}/../../extern ABSOLUTE)
add_subdirectory(${EXTERN}/bgfx.cmake bgfx EXCLUDE_FROM_ALL)
add_executable(portdemo main.cpp)
# This bgfx.cmake does not hand its include directories on to a consumer, so they are named.
target_include_directories(portdemo PRIVATE ${EXTERN} ${EXTERN}/bgfx.cmake/bgfx/include
                           ${EXTERN}/bgfx.cmake/bx/include)
target_compile_definitions(portdemo PRIVATE BX_CONFIG_DEBUG=0)
if(WIN32)
  # GLFW's prebuilt mingw library, fetched by build.sh; everything static, so the .exe runs
  # under Wine with no DLLs beside it.
  set_target_properties(portdemo PROPERTIES WIN32_EXECUTABLE TRUE)
  target_include_directories(portdemo PRIVATE ${GLFW_DIR}/include ${EXTERN}/bgfx.cmake/bx/include/compat/mingw)
  target_link_libraries(portdemo PRIVATE bgfx bx ${GLFW_DIR}/lib-mingw-w64/libglfw3.a gdi32)
  target_link_options(portdemo PRIVATE -static -static-libgcc -static-libstdc++)
else()
  find_package(glfw3 3.3 REQUIRED)
  target_link_libraries(portdemo PRIVATE bgfx bx glfw)
endif()
