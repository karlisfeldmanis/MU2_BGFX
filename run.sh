#!/bin/zsh
# Runs it. Vsync is off so a frame counter is a frame; the log lands in mu2.log and the
# shots in shots/.
#   ./run.sh                                   play until esc
#   ./run.sh --frames 300 --shot 100           a review run
#   ./run.sh --frames 300 --stats /tmp/s.csv --budget
set -e
cd "$(dirname "$0")"
[ -x build/mu2 ] || ./build.sh
mkdir -p shots
exec build/mu2 "$@"
