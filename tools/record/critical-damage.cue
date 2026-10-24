# The Dark Lord's Critical Damage (docs/plans/dark-lord-spells/critical-damage.md): a level-100 Dark
# Lord casts it off Q beside one Bull Fighter and fights it with his sword alone (--arena-left: no Force,
# nothing else taught) -- the flash at his weapon hand, then the coils at his grip every 1.2 s while the
# sword is drawn -- and casts it again.
# ./mu record critical-damage --fps 30
args --arena "Bull Fighter" --arena-count 1 --arena-left --arena-undying --class 4 --level 100 --teach 64 --weapon Sword02
end 24

0.0  move 0.5 0.5
1.0  skill q
12.0 skill q
