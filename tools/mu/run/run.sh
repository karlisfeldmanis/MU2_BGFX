#!/bin/zsh
# Runs it. Vsync is off so a frame counter is a frame; the log lands in mu2.log and the
# shots in shots/.
#   ./mu run                                   play until esc
#   ./mu run --frames 300 --shot 100           a review run
#   ./mu run --frames 300 --stats /tmp/s.csv --budget
set -e
cd "$(dirname "$0")/../../.."
[ -x out/build/mu2 ] || ./mu build
mkdir -p out/shots
exec out/build/mu2 "$@"
