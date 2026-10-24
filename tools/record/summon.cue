# The Dark Lord's Summon (docs/plans/dark-lord-spells/summon.md): a level-100 Dark Lord alone with two
# heroes of his party nine tiles off (--bench-party: the first stands, the second walks every four
# seconds), nothing fighting, nothing else taught. Off Q: the blue star and the wall at his feet, the
# pillars, the arm held up and glittering; seven seconds on the one who stood still is set down
# beside him, the walker's summons cancelled.
# ./mu record summon --fps 30
args --arena "Bull Fighter" --arena-count 1 --no-hand --unseen --bench-players 2 --bench-party --class 4 --level 100 --teach 63 --weapon Sword02
end 16

0.0  move 0.5 0.5
2.0  skill q
