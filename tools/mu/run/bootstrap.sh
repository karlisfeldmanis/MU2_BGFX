#!/bin/zsh
# Fetches what the build needs and git does not keep: bgfx (with bx and bimg), cgltf,
# stb_image and miniaudio. Run once; mu build calls it when third_party/extern/ is missing.
#
# Pinned by commit. An unpinned dependency moves under the frame and a number measured last
# week stops meaning anything -- which this file said at the top while fetching two of its
# three headers from `master`. Both are pinned now, each to the commit that was verified
# BYTE-IDENTICAL to the copy in third_party/extern/ that every number in docs/ was measured against, so
# the pins record what is here rather than changing it.
set -e
cd "$(dirname "$0")/../../.."

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

mkdir -p third_party/extern
if [ ! -d third_party/extern/bgfx.cmake/bgfx ]; then
  git clone --recursive https://github.com/bkaradzic/bgfx.cmake.git third_party/extern/bgfx.cmake
  git -C third_party/extern/bgfx.cmake checkout --quiet $BGFX_CMAKE_REV
  git -C third_party/extern/bgfx.cmake submodule update --init --recursive
fi
# Our changes to bgfx, kept as patches since third_party/extern/ is not in git. Applied once: a patch
# that is already in reverses cleanly, and is left alone.
for p in third_party/patches/bgfx-*.patch; do
  git -C third_party/extern/bgfx.cmake/bgfx apply --reverse --check "$PWD/$p" 2>/dev/null ||
    git -C third_party/extern/bgfx.cmake/bgfx apply "$PWD/$p"
done
[ -f third_party/extern/cgltf.h ] || curl -sSL -o third_party/extern/cgltf.h https://raw.githubusercontent.com/jkuhlmann/cgltf/$CGLTF_REV/cgltf.h
[ -f third_party/extern/stb_image.h ] || curl -sSL -o third_party/extern/stb_image.h https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_image.h
[ -f third_party/extern/stb_truetype.h ] || curl -sSL -o third_party/extern/stb_truetype.h https://raw.githubusercontent.com/nothings/stb/$STB_REV/stb_truetype.h
[ -f third_party/extern/miniaudio.h ] || curl -sSL -o third_party/extern/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/$MINIAUDIO_REV/miniaudio.h
# zstd 1.5.7, for the player's packs (docs/plans/layout.md): the packer writes them and the game reads
# them. Its lib/ only, built into the game by CMakeLists.txt, so a player's Mac needs no
# Homebrew for it. The release tarball, pinned by the checksum zstd publishes beside it.
ZSTD_URL=https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
if [ ! -f third_party/extern/zstd/lib/zstd.h ]; then
  curl -sSL -o third_party/extern/zstd.tar.gz $ZSTD_URL
  echo "$ZSTD_SHA256  third_party/extern/zstd.tar.gz" | shasum -a 256 -c - || { rm -f third_party/extern/zstd.tar.gz; exit 1; }
  mkdir -p third_party/extern/zstd
  tar -xzf third_party/extern/zstd.tar.gz -C third_party/extern/zstd --strip-components=1 zstd-1.5.7/lib zstd-1.5.7/LICENSE
  rm -f third_party/extern/zstd.tar.gz
fi
# For the Windows client only (docs/plans/windows-client.md): the Mac has both in its SDK and
# Homebrew, a Windows build has neither, so it builds them from source. zlib 1.3.1 packs the
# Welcome's snapshot (net::pack) and GLFW 3.4 opens the window. Pinned by the checksums the
# projects publish.
ZLIB_URL=https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
if [ ! -f third_party/extern/zlib/zlib.h ]; then
  curl -sSL -o third_party/extern/zlib.tar.gz $ZLIB_URL
  echo "$ZLIB_SHA256  third_party/extern/zlib.tar.gz" | shasum -a 256 -c - || { rm -f third_party/extern/zlib.tar.gz; exit 1; }
  mkdir -p third_party/extern/zlib
  tar -xzf third_party/extern/zlib.tar.gz -C third_party/extern/zlib --strip-components=1
  rm -f third_party/extern/zlib.tar.gz
fi
GLFW_URL=https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
GLFW_SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
if [ ! -f third_party/extern/glfw/include/GLFW/glfw3.h ]; then
  curl -sSL -o third_party/extern/glfw.zip $GLFW_URL
  echo "$GLFW_SHA256  third_party/extern/glfw.zip" | shasum -a 256 -c - || { rm -f third_party/extern/glfw.zip; exit 1; }
  unzip -q -o third_party/extern/glfw.zip -d third_party/extern
  mv third_party/extern/glfw-3.4 third_party/extern/glfw
  rm -f third_party/extern/glfw.zip
fi
# The overlay's typeface, and it is MU2's by inheritance rather than by choice of ours: MU2
# sets no theme font in either of its Godot projects, so every label it draws is Godot's own
# fallback, which is Open Sans SemiBold. Taking the same file here is what makes a viewer
# shot and a MU2 shot read as one project. Pinned to a tag, as the headers are to commits.
# The commit that last touched the file, not a tag: the repository's tags do not carry the
# built ttf at this path, and a pin that 404s writes a 14-byte "Not Found" into third_party/extern/ and
# leaves the viewer drawing its fallback with nothing in the log to say why.
OPEN_SANS_REV=bd7e37632246368c60fdcbd374dbf9bad11969b6
[ -f third_party/extern/OpenSans-SemiBold.ttf ] || curl -sSL -o third_party/extern/OpenSans-SemiBold.ttf https://raw.githubusercontent.com/googlefonts/opensans/$OPEN_SANS_REV/fonts/ttf/OpenSans-SemiBold.ttf
# The map name's face, Cinzel Medium (SIL OFL). Upstream ships only Regular, Bold and Black as
# static files and stb_truetype draws a variable font at its default weight, so this is Google
# Fonts' own static 500 instance, the one the chosen design page was drawn in. The v26 path is
# versioned by Google; the checksum is what makes it a pin.
CINZEL_URL=https://fonts.gstatic.com/s/cinzel/v26/8vIU7ww63mVu7gtR-kwKxNvkNOjw-uTnTYo.ttf
CINZEL_SHA256=bd933cb739b5125a1fda907d61ea237beadb4ac382ac7a5f9af0a8bbedaf48b7
if [ ! -f third_party/extern/Cinzel-Medium.ttf ]; then
  curl -sSL -o third_party/extern/Cinzel-Medium.ttf $CINZEL_URL
  echo "$CINZEL_SHA256  third_party/extern/Cinzel-Medium.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/Cinzel-Medium.ttf; exit 1; }
fi
# The fight's own face, Cinzel Bold (SIL OFL). The damage figures, the gain lane and the death
# are set in the SAME family as the map name -- the user's call on 2026-09-23, after the first
# build drew them in a condensed grotesque that read as a different game from the one the
# arrival is in. Google Fonts' static 700 instance; the v26 path is versioned by Google and the
# checksum is what makes it a pin. The Medium above is what the lane and the map name share.
CINZEL_BOLD_URL=https://fonts.gstatic.com/s/cinzel/v26/8vIU7ww63mVu7gtR-kwKxNvkNOjw-jHgTYo.ttf
CINZEL_BOLD_SHA256=2c6b1ff0d368efb73636700b69dbef707fa5602a06d085a4e91e5125082651f0
if [ ! -f third_party/extern/Cinzel-Bold.ttf ]; then
  curl -sSL -o third_party/extern/Cinzel-Bold.ttf $CINZEL_BOLD_URL
  echo "$CINZEL_BOLD_SHA256  third_party/extern/Cinzel-Bold.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/Cinzel-Bold.ttf; exit 1; }
fi
# The damage figures' own face, Philosopher Bold (SIL OFL), the user's pick on 2026-10-02 off a
# page of fourteen faces drawn over a Lorencia fight. Cinzel Bold above stays for the death.
# Google Fonts' static 700 instance, pinned by the checksum as the Cinzels are.
PHILOSOPHER_BOLD_URL=https://fonts.gstatic.com/s/philosopher/v21/vEFI2_5QCwIS4_Dhez5jcWjVamgc.ttf
PHILOSOPHER_BOLD_SHA256=5b2bd1ca0ef9ed2869fbad9511ce3f9e5f08848712a0a94c0549fd4b360921fd
if [ ! -f third_party/extern/Philosopher-Bold.ttf ]; then
  curl -sSL -o third_party/extern/Philosopher-Bold.ttf $PHILOSOPHER_BOLD_URL
  echo "$PHILOSOPHER_BOLD_SHA256  third_party/extern/Philosopher-Bold.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/Philosopher-Bold.ttf; exit 1; }
fi
# The Sanctuary interface's two sans faces (SIL OFL), chosen by the user on 2026-09-27 with the
# rest of game/ui/style.h: Alegreya Sans SC Bold for every button's word, set with no tracking,
# and Alegreya Sans Medium for a control's label. Google Fonts' static instances, versioned by
# the path and pinned by the checksum, as the two Cinzels above are.
ALEGREYA_SC_URL=https://fonts.gstatic.com/s/alegreyasanssc/v26/mtGm4-RGJqfMvt7P8FUr0Q1j-Hf1DvJA0iQ.ttf
ALEGREYA_SC_SHA256=05e00176d329d31cf01488316dd8c73d3e33a27cf6a60591cb0b2f10036da841
if [ ! -f third_party/extern/AlegreyaSansSC-Bold.ttf ]; then
  curl -sSL -o third_party/extern/AlegreyaSansSC-Bold.ttf $ALEGREYA_SC_URL
  echo "$ALEGREYA_SC_SHA256  third_party/extern/AlegreyaSansSC-Bold.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/AlegreyaSansSC-Bold.ttf; exit 1; }
fi
ALEGREYA_URL=https://fonts.gstatic.com/s/alegreyasans/v28/5aUu9_-1phKLFgshYDvh6Vwt5alOmE0.ttf
ALEGREYA_SHA256=9062eb42daabd93e6071ba60f76162b9c52402cef71b305e352956241ade1dd9
if [ ! -f third_party/extern/AlegreyaSans-Medium.ttf ]; then
  curl -sSL -o third_party/extern/AlegreyaSans-Medium.ttf $ALEGREYA_URL
  echo "$ALEGREYA_SHA256  third_party/extern/AlegreyaSans-Medium.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/AlegreyaSans-Medium.ttf; exit 1; }
fi
# The names over the drops: Amarante, a medieval roman with a gothic hand in its capitals and
# an open lowercase -- the user's pick on 2026-10-06 off a sheet of nineteen faces. Its one weight.
AMARANTE_URL=https://fonts.gstatic.com/s/amarante/v30/xMQXuF1KTa6EvGx9bq-3.ttf
AMARANTE_SHA256=76cbeaae692f03153654c3cf2c79252ea61ac6bdf6dcb192c491cb8e3c0dfca7
if [ ! -f third_party/extern/Amarante-Regular.ttf ]; then
  curl -sSL -o third_party/extern/Amarante-Regular.ttf $AMARANTE_URL
  echo "$AMARANTE_SHA256  third_party/extern/Amarante-Regular.ttf" | shasum -a 256 -c - || { rm -f third_party/extern/Amarante-Regular.ttf; exit 1; }
fi

# glfw is the one foundation 12 (architecture.md) names that is NOT pinned here; saying so is better
# than the file implying otherwise: it comes from Homebrew, as a built dylib, and pinning it
# would mean vendoring and building it. It opens a window and reads a mouse; it is not in the
# frame and it does not move a measurement the way bgfx does. If it ever does, this is the
# line that has to change.
for t in cmake ninja; do
  which $t >/dev/null || { echo "brew install cmake ninja glfw ccache"; exit 1; }
done
echo "extern ready"
