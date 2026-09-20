#!/bin/zsh
# Fetches what the build needs and git does not keep: bgfx (with bx and bimg), cgltf and
# stb_image. Run once; build.sh calls it when extern/ is missing.
#
# Pinned by commit. An unpinned bgfx moves under the frame and a number measured last week
# stops meaning anything.
set -e
cd "$(dirname "$0")"

# bgfx.cmake, with bgfx, bx and bimg as submodules. This is the revision MU4 was built and
# measured against; its submodules come with it.
BGFX_CMAKE_REV=0fb9ec06bdaa7c3ae31ce6a3521e211183a0cead

mkdir -p extern
if [ ! -d extern/bgfx.cmake/bgfx ]; then
  git clone --recursive https://github.com/bkaradzic/bgfx.cmake.git extern/bgfx.cmake
  git -C extern/bgfx.cmake checkout --quiet $BGFX_CMAKE_REV
  git -C extern/bgfx.cmake submodule update --init --recursive
fi
[ -f extern/cgltf.h ] || curl -sSL -o extern/cgltf.h https://raw.githubusercontent.com/jkuhlmann/cgltf/master/cgltf.h
[ -f extern/stb_image.h ] || curl -sSL -o extern/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

for t in cmake ninja; do
  which $t >/dev/null || { echo "brew install cmake ninja glfw ccache"; exit 1; }
done
echo "extern ready"
