#!/bin/zsh
# Configures once and builds with Ninja. Release by default, because every number in the
# docs is a Release number. --debug builds beside it; --trace builds bgfx's own trace in,
# which is where Metal's pipeline refusals become visible.
set -e
cd "$(dirname "$0")"
[ -d third_party/extern/bgfx.cmake/bgfx ] || ./bootstrap.sh

dir=out/build; type=Release; extra=()
case "$1" in
  --debug) dir=out/build-debug; type=Debug ;;
  --trace) dir=out/build-trace; type=RelWithDebInfo; extra=(-DBX_CONFIG_DEBUG=ON) ;;
esac
[ -f $dir/build.ninja ] || cmake -S . -B $dir -G Ninja -DCMAKE_BUILD_TYPE=$type $extra
cmake --build $dir --target mu2
