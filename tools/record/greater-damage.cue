# Greater Damage standing on the elf (bug report #69, Aura::might): a level-100 Fairy Elf casts it on
# herself on Q -- the cast's burst, then while it stands MU's orange glow on both arms and the
# guard's green ribbons -- and fights a Bull Fighter, neither falling, then runs west a few steps with it on her.
# ./mu record greater-damage --fps 30
args --arena bull_fighter --arena-count 1 --arena-left --arena-undying --no-hand --class 1 --level 100
args --teach 28
end 16

0.0   move 0.5 0.5
0.4   aim hero 0.3
1.0   skill q
9.0   run -1 0
11.5  stop
13.5  move 0.55 0.52 0.9
