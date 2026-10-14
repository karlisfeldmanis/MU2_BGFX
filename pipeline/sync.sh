#!/bin/zsh
# Copies content out of workshop/ -- the pipeline's output, built by pipeline/content.sh and
# pipeline/asset.sh -- into assets/ here. MU2_BUILD=../MU2/build copies from MU2's build instead,
# which is where this came from until 2026-09-21. Nothing is symlinked: a run measured here
# does not change because somebody rebuilt something else.
#
#   ./pipeline/sync.sh              everything index.json reaches
#   ./pipeline/sync.sh --world lorencia --only-world   just that map's own files
#
# The rules are MU2's own three (docs/environments.md): every string in index.json that is a
# file under build/; every bare file name a copied .json names beside itself; every image a
# copied .glb reaches for by URI.
set -e
. "$(dirname "$0")/../tools/paths.sh"
cd "$ROOT"
SRC=${MU2_BUILD:-$WORKSHOP}
[ -f "$SRC/index.json" ] || { echo "no index.json in $SRC: run pipeline/content.sh"; exit 1; }
mkdir -p "$ASSETS"
python3 "$PIPELINE/sync.py" --src "$SRC" --dst "$ASSETS" "$@"
# The quest givers' voices, finished WAVs out of pipeline/voice.py: copied as they are.
[ -d "$CONTENT/voice" ] && rsync -a --exclude ref "$CONTENT/voice/" "$ASSETS/voice/"
# Each world's manifest (content/world/<w>/world.json, docs/plans/order-plan.md sprint D): copied as it is,
# beside the world's ground, for the client's game/world/world_show.cpp.
for manifest in $(ls "$CONTENT"/world/*/world.json 2>/dev/null); do
    world=$(basename "$(dirname "$manifest")")
    mkdir -p "$ASSETS/world/$world" && cp "$manifest" "$ASSETS/world/$world/world.json"
done
# The stingers, the user's own music (Sound::stinger): copied as they are, beside MU's tracks.
[ -d "$CONTENT/music" ] && rsync -a "$CONTENT/music/" "$ASSETS/music/"
