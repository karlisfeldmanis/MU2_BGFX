#!/bin/zsh
# The Windows/Linux port test (docs/windows-linux-test.md): builds the demo and the folder it
# runs from, build/portdemo-<os>/ (the program, shaders/spirv/, and the monster's glb).
#
#   tools/portdemo/build.sh windows      mingw cross-compile on this Mac, then run it:
#                                          cd build/portdemo-windows && wine portdemo.exe
#   tools/portdemo/build.sh linux        build and run inside the OrbStack machine mu2linux,
#                                          on Xvfb with software Vulkan; writes linux1/2.png
#   ... --monster GiantOgre01            another of assets/assets/monsters/ (default DeathKnight01)
#
# The first build compiles bgfx (a few minutes); later ones only the demo.
set -e
cd "${0:A:h}/../.."
ROOT=$PWD
OS=${1:?windows or linux}
MONSTER=DeathKnight01
[[ "$2" == --monster ]] && MONSTER=$3
OUT=$ROOT/build/portdemo-$OS
mkdir -p $OUT/shaders/spirv

# The shaders, by the Mac's own shaderc (build.sh's build makes it); SPIR-V is one file for both.
SHADERC=$ROOT/build/extern/bgfx.cmake/cmake/bgfx/shaderc
SRC=$ROOT/tools/portdemo/shaders
for kind in vertex:vs fragment:fs; do
  $SHADERC -f $SRC/${kind#*:}_skin.sc -o $OUT/shaders/spirv/${kind#*:}_skin.bin --type ${kind%:*} \
    --platform windows -p spirv --varyingdef $SRC/varying.def.sc -i $ROOT/extern/bgfx.cmake/bgfx/src
done
cp $ROOT/assets/assets/monsters/$MONSTER/$MONSTER.glb $OUT/

if [[ $OS == windows ]]; then
  GLFW=$ROOT/build/portdemo-glfw/glfw-3.4.bin.WIN64
  if [[ ! -d $GLFW ]]; then
    mkdir -p $ROOT/build/portdemo-glfw
    curl -sL -o $ROOT/build/portdemo-glfw/glfw.zip \
      https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip
    unzip -q -o $ROOT/build/portdemo-glfw/glfw.zip -d $ROOT/build/portdemo-glfw
  fi
  cmake -S tools/portdemo -B $OUT/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$ROOT/tools/portdemo/mingw.cmake -DGLFW_DIR=$GLFW > /dev/null
  nice cmake --build $OUT/build -j6
  cp $OUT/build/portdemo.exe $OUT/
  echo "built $OUT/portdemo.exe -- cd $OUT && wine portdemo.exe $MONSTER.glb"
elif [[ $OS == linux ]]; then
  # The machine sees this Mac's /Users at the same paths; it builds on its own disk.
  orb -m mu2linux bash -c "
    set -e
    cmake -S $ROOT/tools/portdemo -B ~/portdemo-build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
    nice cmake --build ~/portdemo-build -j6
    cp ~/portdemo-build/portdemo $OUT/
    cd $OUT
    Xvfb :9 -screen 0 1280x800x24 > /dev/null 2>&1 & sleep 2
    DISPLAY=:9 ./portdemo $MONSTER.glb > run.log 2>&1 & sleep 10
    DISPLAY=:9 xwd -root -silent | convert xwd:- linux1.png; sleep 6
    DISPLAY=:9 xwd -root -silent | convert xwd:- linux2.png
    pkill portdemo; pkill Xvfb; true"
  grep -v '^\s' $OUT/run.log | head -4
  echo "shots: $OUT/linux1.png $OUT/linux2.png"
fi
