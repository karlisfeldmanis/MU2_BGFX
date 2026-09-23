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
# The map name's face, Cinzel Medium (SIL OFL). Upstream ships only Regular, Bold and Black as
# static files and stb_truetype draws a variable font at its default weight, so this is Google
# Fonts' own static 500 instance, the one the chosen design page was drawn in. The v26 path is
# versioned by Google; the checksum is what makes it a pin.
CINZEL_URL=https://fonts.gstatic.com/s/cinzel/v26/8vIU7ww63mVu7gtR-kwKxNvkNOjw-uTnTYo.ttf
CINZEL_SHA256=bd933cb739b5125a1fda907d61ea237beadb4ac382ac7a5f9af0a8bbedaf48b7
if [ ! -f extern/Cinzel-Medium.ttf ]; then
  curl -sSL -o extern/Cinzel-Medium.ttf $CINZEL_URL
  echo "$CINZEL_SHA256  extern/Cinzel-Medium.ttf" | shasum -a 256 -c - || { rm -f extern/Cinzel-Medium.ttf; exit 1; }
fi
# The fight's figures, Barlow Semi Condensed Bold (SIL OFL): the face the chosen design page was
# drawn in, and narrow for the reason the page gives -- an area skill puts five numbers across one
# tile and a normal-width face has them touching. Google Fonts' static 700 instance; the v16 path
# is versioned by Google and the checksum is what makes it a pin.
BARLOW_URL=https://fonts.gstatic.com/s/barlowsemicondensed/v16/wlpigxjLBV1hqnzfr-F8sEYMB0Yybp0mudRfw6-PAA.ttf
BARLOW_SHA256=6098b66ce7d608dcea8c6a2eeacfdeeee5416e0e0c12a2318595d3dd8c5babef
if [ ! -f extern/BarlowSemiCondensed-Bold.ttf ]; then
  curl -sSL -o extern/BarlowSemiCondensed-Bold.ttf $BARLOW_URL
  echo "$BARLOW_SHA256  extern/BarlowSemiCondensed-Bold.ttf" | shasum -a 256 -c - || { rm -f extern/BarlowSemiCondensed-Bold.ttf; exit 1; }
fi

# glfw is the one PLAN.md point 12 names that is NOT pinned here, and saying so is better
# than the file implying otherwise: it comes from Homebrew, as a built dylib, and pinning it
# would mean vendoring and building it. It opens a window and reads a mouse; it is not in the
# frame and it does not move a measurement the way bgfx does. If it ever does, this is the
# line that has to change.
for t in cmake ninja; do
  which $t >/dev/null || { echo "brew install cmake ninja glfw ccache"; exit 1; }
done
echo "extern ready"
