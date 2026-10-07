#!/bin/zsh
# Puts the server's half of MU2_BGFX on the Hetzner box, builds it there and runs its checks.
#
#   server/deploy.sh            copy, build, test
#   server/deploy.sh --copy     copy only
#
# What goes over is what MU2_SERVER_ONLY builds (CMakeLists.txt): src/core, src/content, src/sim,
# the save, the tests and tools that link only those, and every map's cooked tables (*.mur, ~2 MB
# of the 3.9 GB in assets/cooked). No textures, meshes, shaders or sound. server/README.md has
# the box itself.
set -e
cd "$(dirname "$0")/.."
host=hetzner          # ~/.ssh/config: root@37.27.158.226
dest=/opt/mu2

ssh $host "mkdir -p $dest"
rsync -az --delete --prune-empty-dirs \
  --include='/CMakeLists.txt' \
  --include='/src/' --include='/src/core/***' --include='/src/content/***' --include='/src/sim/***' \
  --include='/src/server/***' \
  --include='/src/game/' --include='/src/game/save.cpp' --include='/src/game/save.h' \
  --include='/tests/' --include='/tests/*.cpp' --include='/tests/fixtures/***' \
  --include='/tools/' --include='/tools/bot/***' --include='/tools/raid/***' \
  --include='/source/' --include='/source/raid/***' \
  --include='/assets/' --include='/assets/cooked/' --include='/assets/cooked/*/' \
  --include='/assets/cooked/*/*.mur' \
  --include='/server/***' \
  --exclude='*' ./ $host:$dest/
echo "copied to $host:$dest"
[ "$1" = "--copy" ] && exit 0

# One job: the box has one core and 1 GB (2 GB swap). sim_test.cpp is the slow one, a few minutes.
ssh $host "cd $dest && \
  { [ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMU2_SERVER_ONLY=ON; } && \
  cmake --build build -j 1 --target sim_test save_test placement_test && \
  ./build/sim_test > sim_test.log 2>&1; tail -1 sim_test.log; \
  ./build/save_test | tail -1; ./build/placement_test | tail -1"
