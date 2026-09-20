#!/bin/zsh
# The object viewer: every cooked model, one at a time, turning over a patch of real ground.
#
# This is the browser and nothing else -- no town, no realm, no crowd. It walks what the COOK
# wrote rather than the .glb it was made from, which is the point of it: `--model` reads a glb
# through cgltf and proves the art, and this reads exactly what the game loads, so a fault
# introduced by the cook shows here and nowhere else.
#
#   ./viewer.sh                       the list, one model at a time, esc to quit
#   ./viewer.sh --dist 3              hold one distance instead of re-framing per model
#   ./viewer.sh --spin                turn the camera; it is held still by default, because
#                                     a turntable is motion you did not ask for when you are
#                                     trying to look at one face of a thing
#   ./viewer.sh --frames 200 --shot 100 --shot-path /abs/dir     a review run
#
#   left / right ..... one model
#   down / up ........ ten
#   esc .............. quit
#
# The subject hangs clear of the land rather than standing on it, because a viewer is for
# looking at the underside of a thing. The ground is a synthetic plot wearing a real surface
# out of the world's own ground_surfaces.json, under the game's own lighting sheet -- so a
# material is judged against the shader and the sun it will actually live under, and against
# the same plot every time. See docs/materials.md.
set -e
cd "$(dirname "$0")"
[ -x build/mu2 ] || ./build.sh
mkdir -p shots
# --still first so a --spin on the command line takes it back.
exec build/mu2 --browse --still "$@"
