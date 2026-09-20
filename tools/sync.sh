#!/bin/zsh
# Copies content out of MU2's build into assets/ here. Nothing is read from MU2 at run time
# and nothing is symlinked: this tree stands on its own, and a run measured here does not
# change because somebody rebuilt MU2.
#
#   ./tools/sync.sh              everything index.json reaches
#   ./tools/sync.sh --world lorencia --only-world   just that map's own files
#
# The rules are MU2's own three (docs/environments.md): every string in index.json that is a
# file under build/; every bare file name a copied .json names beside itself; every image a
# copied .glb reaches for by URI.
set -e
cd "$(dirname "$0")/.."
SRC=${MU2_BUILD:-../MU2/build}
[ -d "$SRC" ] || { echo "no MU2 build at $SRC"; exit 1; }
mkdir -p assets
python3 tools/sync.py --src "$SRC" --dst assets "$@"
