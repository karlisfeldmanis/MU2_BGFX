#!/bin/zsh
# Copies content out of workshop/ -- the pipeline's output, built by tools/content.sh and
# tools/asset.sh -- into assets/ here. MU2_BUILD=../MU2/build copies from MU2's build instead,
# which is where this came from until 2026-09-21. Nothing is symlinked: a run measured here
# does not change because somebody rebuilt something else.
#
#   ./tools/sync.sh              everything index.json reaches
#   ./tools/sync.sh --world lorencia --only-world   just that map's own files
#
# The rules are MU2's own three (docs/environments.md): every string in index.json that is a
# file under build/; every bare file name a copied .json names beside itself; every image a
# copied .glb reaches for by URI.
set -e
cd "$(dirname "$0")/.."
SRC=${MU2_BUILD:-workshop}
[ -f "$SRC/index.json" ] || { echo "no index.json in $SRC: run tools/content.sh"; exit 1; }
mkdir -p assets
python3 tools/sync.py --src "$SRC" --dst assets "$@"
