#!/bin/zsh
# The game. A new Dark Knight, made the way MU makes one, standing in Lorencia with the axe
# his class is given and nothing else on him.
#
#   ./main.sh                     play, esc to quit
#   ./main.sh --at 185,120        start out in the spider field instead of in town
#   ./main.sh --class 1           a Fairy Elf, who is given a Short Bow
#                                 (still in Lorencia: an elf's home is Noria and the map a
#                                 class is made on is sprint 9's, with the gate between them)
#   ./main.sh --level 10          a character further along, his points already spent
#
#   left click on the ground ..... walk there
#   left click on a monster ...... go and fight it until one of you is dead
#   right click .................. stop
#   esc .......................... quit
#
# What the character is, and why each of these is what it is:
#
# * **Naked.** He wears `HelmClass02` and its four fellows -- the class body itself, which is
#   what a Dark Knight is under armour and what MU draws a new one in. The cooked `DarkKnight`
#   row is a plate set with a Kris and a Plate Shield, which is a character forty levels along,
#   and it is what every run before this one drew. See `Figures::dress`.
# * **Holding a Small Axe.** OpenMU's Version075 `AddSmallAxeForDarkKnight`, by way of MU2's
#   `Cradle.cs`: the knight is given one, the elf a Short Bow, and the wizard nothing but
#   Energy Ball. He is 22 strength short of being able to lift it and is given it anyway,
#   because the requirement belongs to picking an item up and not to being made holding one.
#   `Realm::equip`'s `given` says the same thing in the sim, and the log prints the shortfall.
# * **Level one, in Lorencia.** Where everybody who is not an elf starts. He is put down in the
#   town square, which is a safe zone, so the axe rides on his back and he stands unarmed until
#   he walks out of it -- MU's own rule and not a pose this script chose. The spiders are the
#   nest east of the town, tiles 180-226 by 90-244 in `mu.db`'s own spawn row: 45 of them at
#   level 2 with 30 health, which is what a level-one knight is meant to start on.
#
# It is the same binary `run.sh` and `viewer.sh` launch -- there is no server anywhere in this
# tree and nothing to start beside it -- with the switches that make a run a game rather than a
# measurement: vsync ON, so the frame is paced to the display and the machine is not spun to
# 470 fps for nothing, and no `--frames`, no `--shot` and no `--stats`, because a number taken
# while somebody is playing is not a number. Take those with `run.sh`, which is what it is for.
#
# Anything given here is passed through and a later switch wins, so `./main.sh --weapon Sword01`
# arms him with a Kris instead. The one thing the script decides for itself is which weapon the
# cradle gives, because that follows from the class: `--class 1` is an elf and an elf is given a
# bow, not an axe.
set -e
cd "$(dirname "$0")"
[ -x build/mu2 ] || ./build.sh

# The switch is `--class`, spelled as the character screen would ask it; the field behind it is
# `kin`, because `class` is a keyword in the language the engine is written in.
kin=2
asked=no      # the caller named a weapon of his own
prev=
for arg in "$@"; do
  case "$prev" in
    --class) kin=$arg ;;
    --weapon) asked=yes ;;
  esac
  prev=$arg
done
# Cradle.cs's three rows: the knight an axe, the elf a bow, the wizard empty hands.
cradle=()
if [ "$asked" = "no" ]; then
  case "$kin" in
    0) ;;
    1) cradle=(--weapon Bow01) ;;
    *) cradle=(--weapon Axe01) ;;
  esac
fi

# Not `exec`, and the log's last lines on the way out. The engine says everything it has to say
# in mu2.log and nothing at all on the terminal, so a refused switch or a missing cooked file
# closed the window before it opened and looked, from where the player was sitting, exactly like
# nothing happening. It did that the first time this script was run, over a switch spelled
# `--kin` that the engine calls `--class`.
#
# `code` and not `status`: this is zsh, where `status` is a read-only special parameter -- zsh's
# own name for `$?` -- so `status=0` is an error, and under `set -e` it ended the script on this
# very line, before mu2 was ever launched. The script could not have run as first written.
code=0
# `|| code=$?` rather than reading `$?` after it, because `set -e` at the top of this file
# would otherwise end the script on the failure before the lines that explain it are printed.
# `--cap 60` as well as vsync, and the two are not the same thing. Vsync only refuses to
# present between refreshes; on a 180 Hz display, where a refresh is 5.56 ms and this frame
# costs 7.6 ms in the middle and 12.3 at the 99th percentile, that means presenting on the
# second refresh or the third as the cost wanders across 11.1 ms, and the picture steps
# between 90 and 60 fps several times a second. A period the frame fits inside every time is
# the same refresh every time. `./main.sh --cap 0` lets it run free again, and `--cap 90` asks
# for the faster pace on a frame that cannot quite hold it yet.
build/mu2 --world lorencia --play --vsync --cap 60 --level 1 --class "$kin" "${cradle[@]}" "$@" ||
  code=$?
if [ $code -ne 0 ]; then
  echo "mu2 stopped with $code. The last of mu2.log:"
  tail -8 mu2.log
fi
exit $code
