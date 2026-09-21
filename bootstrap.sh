#!/bin/zsh
# Fetches what the build needs and git does not keep: bgfx (with bx and bimg), cgltf,
# stb_image and miniaudio. Run once; build.sh calls it when extern/ is missing.
#
# Pinned by commit. An unpinned dependency moves under the frame and a number measured last
# week stops meaning anything -- which this file said at the top while fetching two of its
# three headers from `master`. Both are pinned now, each to the commit that was verified
# BYTE-IDENTICAL to the copy in extern/ that every number in docs/ was measured against, so
# the pins record what is here rather than changing it.
set -e
cd "$(dirname "$0")"

# bgfx.cmake, with bgfx, bx and bimg as submodules. This is the revision MU4 was built and
# measured against; its submodules come with it.
BGFX_CMAKE_REV=0fb9ec06bdaa7c3ae31ce6a3521e211183a0cead
CGLTF_REV=85cd62382dfea638278962690cf515023f33ed00
STB_REV=2c980bb59875b0d32144a71867fbdebb2f77cd20
# miniaudio 0.11.25, and the tag is the commit. One header, no library to link, and it opens
# CoreAudio itself -- which is the whole reason it is the choice here over a device layer
# that would need a second dependency to decode a wav. Sprint 6 uses it for one device, a
# flat mix and two voices an event; the cook has already made every sound mono 16-bit at
# 22050 Hz, so nothing asks it to resample at play.
MINIAUDIO_REV=9634bedb5b5a2ca38c1ee7108a9358a4e233f14d

mkdir -p extern
if [ ! -d extern/bgfx.cmake/bgfx ]; then
  git clone --recursive https://github.com/bkaradzic/bgfx.cmake.git extern/bgfx.cmake
  git -C extern/bgfx.cmake checkout --quiet $BGFX_CMAKE_REV
  git -C extern/bgfx.cmake submodule update --init --recursive
fi
[ -f extern/cgltf.h ] || curl -sSL -o extern/cgltf.h https://raw.githubusercontent.com/jkuhlmann/cgltf/$CGLTF_REV/cgltf.h
[ -f extern/stb_image.h ] || curl -sSL -o extern/stb_image.h https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_image.h
[ -f extern/stb_truetype.h ] || curl -sSL -o extern/stb_truetype.h https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_truetype.h
[ -f extern/miniaudio.h ] || curl -sSL -o extern/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/$MINIAUDIO_REV/miniaudio.h
# The overlay's typeface, and it is MU2's by inheritance rather than by choice of ours: MU2
# sets no theme font in either of its Godot projects, so every label it draws is Godot's own
# fallback, which is Open Sans SemiBold. Taking the same file here is what makes a viewer
# shot and a MU2 shot read as one project. Pinned to a tag, as the headers are to commits.
# The commit that last touched the file, not a tag: the repository's tags do not carry the
# built ttf at this path, and a pin that 404s writes a 14-byte "Not Found" into extern/ and
# leaves the viewer drawing its fallback with nothing in the log to say why.
OPEN_SANS_REV=bd7e37632246368c60fdcbd374dbf9bad11969b6
[ -f extern/OpenSans-SemiBold.ttf ] || curl -sSL -o extern/OpenSans-SemiBold.ttf https://raw.githubusercontent.com/googlefonts/opensans/$OPEN_SANS_REV/fonts/ttf/OpenSans-SemiBold.ttf

# glfw is the one PLAN.md point 12 names that is NOT pinned here, and saying so is better
# than the file implying otherwise: it comes from Homebrew, as a built dylib, and pinning it
# would mean vendoring and building it. It opens a window and reads a mouse; it is not in the
# frame and it does not move a measurement the way bgfx does. If it ever does, this is the
# line that has to change.
for t in cmake ninja; do
  which $t >/dev/null || { echo "brew install cmake ninja glfw ccache"; exit 1; }
done
echo "extern ready"
