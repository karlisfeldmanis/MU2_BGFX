# Greater Damage standing on the elf (bug report #69, Aura::might) beside Greater Defense: a level-100
# Fairy Elf casts Greater Defense on herself on Q -- the guard's green ribbons -- then Greater
# Damage on W: its red burst, the red glow on both arms, and the swirl thrown again with the two
# auras' ribbons alternating, green and red (Play::guardStep's auraKinds). She fights a Bull
# Fighter, neither falling, then runs west a few steps with it all on her.
# ./mu record greater-damage --fps 30
args --arena bull_fighter --arena-count 1 --arena-left --arena-undying --no-hand --class 1 --level 100
args --teach 27,28
end 18

0.0   move 0.5 0.5
0.4   aim hero 0.3
1.0   skill q
4.0   skill w
11.0  run -1 0
13.5  stop
15.5  move 0.55 0.52 0.9
