#!/bin/zsh
# The object viewer: every cooked model, one at a time, turning over a patch of real ground.
#
# This is the browser and nothing else -- no town, no realm, no crowd. It walks what the COOK
# wrote rather than the .glb it was made from, which is the point of it: `--model` reads a glb
# through cgltf and proves the art, and this reads exactly what the game loads, so a fault
# introduced by the cook shows here and nowhere else.
#
# What it shows is cut into four categories, which is what the tabs down the left are:
#
#   World objects ... the cooked meshes of the world, standing on the land
#   Monsters ........ the breeds, stood up as the game stands one: on the land, with their
#                     gear on, playing their own idle
#   People .......... the characters, guards, merchants and townsfolk, the same way, with
#                     the weapon drawn and in the hand rather than slung on the back
#   Armour sets ..... every suit in index.json, WORN: the five pieces on the bare body of a
#                     class that may wear it, so a suit missing a glove shows a bare hand
#                     rather than a hole, which is what MU draws
#   Weapons ......... every weapon and shield, HELD, by a class that may hold it and in the
#                     stance that weapon puts a body in -- a crossbow level in both hands,
#                     a sword at the side. A weapon lying on the grass is not the object
#                     the game draws, and the crossbow proved it
#   Figure parts .... the figures' loose meshes -- one helmet, one plate, one sword -- which
#                     is what this viewer showed before the categories, and is still where a
#                     material fault on a worn part is read
#
# The last two come out of the WARDROBE, which is its own cook and its own manifest:
#
#   python3 tools/cook.py --only wardrobe
#
# It is not part of `--only all` and the game never loads it. Ninety item files is what the
# figures cook deliberately does not take -- the game wants the five the town wears -- and
# what a viewer exists to show.
#
#   ./viewer.sh                       the list, one at a time, esc to quit
#   ./viewer.sh --dist 3              hold one distance instead of re-framing per model
#   ./viewer.sh --spin                turn the camera; it is held still by default, because
#                                     a turntable is motion you did not ask for when you are
#                                     trying to look at one face of a thing
#   ./viewer.sh --category monsters --pick spider                opens on one thing
#   ./viewer.sh --category armour                                and the same for a suit
#   ./viewer.sh --frames 200 --shot 100 --shot-path /abs/dir     a review run
#
#   left / right ..... one entry            tab .............. next category
#   down / up ........ ten                  [ / ] ............ the figure's next clip
#   drag ............. turns the camera     wheel ............ in and out
#   esc .............. quit
#
# The subject STANDS ON the land. It used to hang clear of it, on the argument that a viewer
# is for looking at the underside of a thing; what a model looks like is inseparable from
# where it meets the earth, and the drag and the wheel reach the underside anyway. The ground
# is a synthetic plot wearing a real surface out of the world's own ground_surfaces.json,
# under the game's own lighting sheet -- so a material is judged against the shader and the
# sun it will actually live under, and against the same plot every time. See
# docs/materials.md.
set -e
cd "$(dirname "$0")"
[ -x build/mu2 ] || ./build.sh
mkdir -p shots
# --still first so a --spin on the command line takes it back.
exec build/mu2 --browse --still "$@"
