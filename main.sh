#!/bin/zsh
# The game. The character screen -- world 74 with your characters on its five pedestals -- and
# the game after it on the one you enter.
#
#   ./main.sh                     play fullscreen on the whole display
#   ./main.sh --windowed          in a 1920x1080 window instead
#   ./main.sh --no-vsync          torn rather than paced to the refresh; --cap still holds
#   ./main.sh --scale 1           every pixel the display has, rather than 85% magnified
#   ./main.sh --roster DIR        characters from DIR instead of your own folder
#   ./main.sh --server 37.27.158.226
#                                 play on the Hetzner server (server/README.md): the character
#                                 you pick enters as his class and level, fresh, and nothing
#                                 played there is written over your characters here
#
# The game menu's Options -- fullscreen or windowed, the window's size, v-sync, volume and the
# frame counter -- are kept in saves/options.txt in this folder and the next run
# opens on them (`--remember`, after this script's own --fullscreen and --vsync so it overrides
# them). A switch given here still wins for that run; delete the file to start from the defaults.
#
#   click a figure .............. pick him; double click or Enter plays him
#   Create Character ............ the create window: a class, a name, Create
#   Delete ...................... asks, then wants his name typed back
#   Menu / esc .................. the game menu; in the world, its Switch Character comes back here
#
#   left click on the ground ..... walk there
#   left click on a monster ...... go and fight it until one of you is dead
#   right click .................. stop
#
# Your characters live in saves/characters in this folder, one file each. The
# first run takes the old hero.json in as "DarkKnight" in slot 0 and leaves hero.json where it
# is. A deleted character is moved to characters/deleted/, not erased.
#
# `--new` passes the character screen by and puts a fresh one straight into Lorencia, which is
# what a quick try of one thing wants:
#
#   ./main.sh --new               a new Dark Knight, the axe his class is given and nothing else
#   ./main.sh --new --at 185,120  start out in the spider field instead of in town
#   ./main.sh --new --class 1     a Fairy Elf, who is given a Short Bow
#                                 (still in Lorencia: an elf's home is Noria and the map a
#                                 class is made on is sprint 9's, with the gate between them)
#   ./main.sh --new --level 10    a character further along, his points already spent
#   ./main.sh --new --level 13 --quest-ready
#                                 Lorencia already cleared: Marlon wears a ? and the talk is the
#                                 hand-in and its reward; nothing is saved
#
# What the --new character is, and why each of these is what it is:
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
lobby=yes     # the character screen, unless --new makes one here
prev=
for arg in "$@"; do
  case "$prev" in
    --class) kin=$arg ;;
    --weapon) asked=yes ;;
  esac
  [ "$arg" = "--new" ] && lobby=no
  prev=$arg
done
# --new is this script's and not the engine's, so it goes no further.
args=()
for arg in "$@"; do [ "$arg" = "--new" ] || args+=("$arg"); done
# Cradle.cs's rows: the knight an axe, the elf a bow -- and the wizard the Skull Staff, which is
# ours (0.75 makes him empty-handed; game/roster.h has the reason).
cradle=()
if [ "$asked" = "no" ]; then
  case "$kin" in
    0) cradle=(--weapon Staff01) ;;
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
# `--cap 180` as well as vsync, and the two are not the same thing. Vsync only refuses to
# present between refreshes; the cap is a period waited out after the present, which is what
# makes a pace rather than a ceiling.
#
# 180 is this display's own refresh, so the cap is not holding anything back: it is the
# setting that SHOWS the drops, because every frame that misses 5.56 ms falls to the next
# refresh and the readout says 90 where it said 180. That is what it is set to while the
# frame is being worked on.
#
# `--scale 0.99` is here for the same reason and is the play setting, not a measuring one:
# the world is drawn at 85% of the screen and magnified by the present pass, while the ring
# and the HUD are drawn at the screen's own size, so the plate and its text stay sharp. The
# 2K frame is fill-bound and has no hot pass to cut -- pixels are the only knob of the right
# size -- and at 2560x1440 it measured 6.32 ms at 1.0, 6.24 at 0.99, 5.87 at 0.95, 5.41 at
# 0.9, 5.01 at 0.85 and 4.60 at 0.8, against the 5.56 ms a 180 Hz refresh allows.
#
# 0.99 is the user's choice, made on 2026-09-24, and it is a sharpness setting rather than a
# frame one: it buys 0.08 ms, which is inside the spread between two runs of the same thing.
# `--scale 1` is the one that costs nothing at all and resamples nothing, and every number in
# docs/budget.md is taken there. `--scale 0.9` is where the saving starts to be worth having.
#
# `--cap 60` is the other setting, and the smooth one: the frame is 7.6 ms in the middle and
# 12.3 at the 99th, so at 180 it steps between 90 and 60 several times a second, while a
# 16.67 ms period is one it fits inside every time and the same refresh every time. Play on
# 60; measure and hunt on 180. `--cap 0` lets it run free.
if [ "$lobby" = "yes" ]; then
  build/mu2 --lobby --fullscreen --vsync --cap 180 --scale 0.99 --remember "${args[@]}" || code=$?
else
  build/mu2 --world lorencia --play --fullscreen --vsync --cap 180 --scale 0.99 --remember --level 1 --class "$kin" "${cradle[@]}" "${args[@]}" ||
    code=$?
fi
if [ $code -ne 0 ]; then
  echo "mu2 stopped with $code. The last of mu2.log:"
  tail -8 mu2.log
fi
exit $code
