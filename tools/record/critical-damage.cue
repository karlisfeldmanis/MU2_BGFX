# The Dark Lord's Critical Damage (docs/plans/dark-lord-spells/critical-damage.md): a level-100 Dark
# Lord alone on a clear patch casts it off Q and stands under it -- the flash at his scepter hand, then
# the coils up his weapon every 1.2 s -- and casts it again. Nothing else is taught, nothing fights.
# ./mu record critical-damage --fps 30
args --arena "Bull Fighter" --arena-count 0 --class 4 --level 100 --teach 64 --weapon Sword02
end 24

0.0  move 0.5 0.5
1.0  skill q
12.0 skill q
