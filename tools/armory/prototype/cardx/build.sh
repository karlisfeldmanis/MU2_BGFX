#!/bin/bash
# Builds the card dumper from the repo's sources (read-only); objects go in the scratchpad.
# Run from MU2_BGFX. Five compilers at most, niced.
B="$(cd "$(dirname "$0")" && pwd)"
E=third_party/extern/bgfx.cmake
FLAGS="-std=c++20 -O1 -Isrc -Ithird_party/extern -I$E/bgfx/include -I$E/bx/include -I$E/bimg/include -I$E/bx/include/compat/osx"
SRCS="src/game/ui/describe.cpp src/content/tables.cpp src/core/files.cpp src/content/grid.cpp $B/cards.cpp"
for f in items rules skills wear maps swings quests talents market broker $EXTRA_SIM; do SRCS="$SRCS src/sim/$f.cpp"; done
mkdir -p "$B/obj"
OBJS=""
n=0
for s in $SRCS; do
  o="$B/obj/$(basename "$(dirname "$s")")_$(basename "$s" .cpp).o"
  OBJS="$OBJS $o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
    nice c++ $FLAGS -c "$s" -o "$o" &
    n=$((n + 1))
    if [ $n -ge 5 ]; then wait -n; n=$((n - 1)); fi
  fi
done
wait
nice c++ -o "$B/cards" $OBJS
