#!/bin/zsh
# Puts the server's half of MU2_BGFX on the Hetzner box, builds it there and runs its checks.
#
#   server/deploy.sh            copy, build, test, and (re)start the service
#   server/deploy.sh --copy     copy only
#   server/deploy.sh --stage    copy, build and test; the running server keeps playing on the old binary
#   server/deploy.sh --switch   only (re)start the service on what --stage built -- a protocol bump's
#                               seconds, run on every box back to back with `mu release`
#
# What goes over is what MU2_SERVER_ONLY builds (CMakeLists.txt): src/core, src/content, src/sim,
# the save, the tests and tools that link only those, and every map's cooked tables (*.mur, ~2 MB
# of the 3.9 GB in assets/cooked). No textures, meshes, shaders or sound. server/README.md has
# the box itself.
#
# **What goes is the commit, never the working tree** (the user, 2026-10-11: "we need ... actual
# master not private stuff so we can do normal deploy"): the code is HEAD's, exported with
# `git archive`, so another session's edit in progress never reaches the box and never splits
# the server's rules from a client built from HEAD. The cooked tables are not in git; they come
# from the working tree as the cook last wrote them.
#
# MU2_HOST picks the box: hetzner by default (North Europe), root@216.238.127.33 for South America
# (Vultr, São Paulo), singapore (root@5.223.83.101) for Southeast Asia, germany (root@178.104.232.133,
# Nuremberg) for Central Europe; their accounts tunnels in server/README.md.
set -e
cd "$(dirname "$0")/.."
host=${MU2_HOST:-hetzner}   # ~/.ssh/config: root@37.27.158.226
dest=/opt/mu2

commit=$(git rev-parse --short HEAD)
here=$(git rev-parse --show-prefix)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
git -C "$(git rev-parse --show-toplevel)" archive "HEAD:${here%/}" | tar -x -C "$stage"
rsync -a --prune-empty-dirs --include='/out/' --include='/out/assets/' --include='/out/assets/cooked/' --include='/out/assets/cooked/*/' \
  --include='/out/assets/cooked/*/*.mur' --exclude='*' ./ "$stage/"
echo "$commit" > "$stage/server/DEPLOYED"
# Said, not refused: what the working tree holds beyond the commit stays here.
if ! git diff --quiet HEAD -- src server tests tools/bot tools/raid content/raid CMakeLists.txt; then
  echo "note: the working tree differs from HEAD in what deploys; HEAD $commit goes, not those edits"
fi

if [ "$1" != "--switch" ]; then
ssh $host "mkdir -p $dest"
rsync -az --delete --prune-empty-dirs \
  --include='/CMakeLists.txt' \
  --include='/src/' --include='/src/net/***' --include='/src/core/***' --include='/src/content/***' --include='/src/sim/***' \
  --include='/src/game/' --include='/src/game/save.cpp' --include='/src/game/save.h' \
  --include='/src/launcher/' --include='/src/launcher/sha256.cpp' --include='/src/launcher/sha256.h' \
  --include='/tests/' --include='/tests/*.cpp' --include='/tests/fixtures/***' \
  --include='/tools/' --include='/tools/bot/***' --include='/tools/raid/***' \
  --include='/content/' --include='/content/raid/***' \
  --include='/out/' --include='/out/assets/' --include='/out/assets/cooked/' --include='/out/assets/cooked/*/' \
  --include='/out/assets/cooked/*/*.mur' \
  --include='/server/***' \
  --exclude='*' "$stage/" $host:$dest/
echo "copied $commit to $host:$dest"
[ "$1" = "--copy" ] && exit 0

# The character store's SQLite headers (server/src/store.h), and sqlite3 to read characters.db
# with (server/README.md); the library itself is the system's.
ssh $host "dpkg -s libsqlite3-dev sqlite3 >/dev/null 2>&1 || DEBIAN_FRONTEND=noninteractive apt-get install -y -q libsqlite3-dev sqlite3 >/dev/null"
# zlib's headers, for the Welcome's packed snapshot (net::pack).
ssh $host "dpkg -s zlib1g-dev >/dev/null 2>&1 || DEBIAN_FRONTEND=noninteractive apt-get install -y -q zlib1g-dev >/dev/null"

# One job: the box has one core and 1 GB (2 GB swap). sim_test.cpp is the slow one, a few minutes.
# A build that fails stops here (set -e): before 2026-10-20 it went on and restarted the old binary.
ssh $host "cd $dest && \
  { [ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMU2_SERVER_ONLY=ON; } && \
  cmake --build build -j 1 --target mu2_server sim_test save_test placement_test || { echo 'build failed: nothing restarted'; exit 1; }; \
  ./build/sim_test > sim_test.log 2>&1; tail -1 sim_test.log; \
  ./build/save_test | tail -1; ./build/placement_test | tail -1"
if [ "$1" = "--stage" ]; then
  # A staged build waits for --switch only if its checks passed.
  ssh $host "tail -1 $dest/sim_test.log | grep -q ' 0 failed'" || { echo "sim_test failed on $host: not staged"; exit 1; }
  echo "staged $commit on $host; server/deploy.sh --switch starts it"
  exit 0
fi
fi

# The service: installed, the game's port open (the stream, and the tick datagrams since sprint 24),
# restarted on the new binary.
ssh $host "install -m 644 $dest/server/mu2-server.service /etc/systemd/system/mu2-server.service && \
  systemctl daemon-reload && systemctl enable --now mu2-server >/dev/null 2>&1; \
  systemctl restart mu2-server && ufw allow 44406/tcp >/dev/null && ufw allow 44406/udp >/dev/null && sleep 1 && \
  systemctl is-active mu2-server && journalctl -u mu2-server -n 2 --no-pager -o cat"
