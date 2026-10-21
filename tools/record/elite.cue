# An Elite met in Lorencia (sim/elite.h): a level-60 Dark Knight runs north through the field;
# the gold dot comes onto the minimap with its bell, ringing on every pulse; he points at it --
# the gold Budge Dragon, its ELITE bar -- fights it down, and picks up what it leaves.
# Seed 7 stands the Elite at (196, 52); he starts 32 tiles south of it, out of the disc's reach.
# ./mu record elite --fps 30
args --play --world lorencia --seed 7 --at 196,84 --class 0 --level 60
args --give Sword05:1:+5W,HelmMale02:1:W,ArmorMale02:1:W,PantMale02:1:W,GloveMale02:1:W,BootMale02:1:W
end 40

0.0  move 0.5 0.5
0.5  run 0 -1
8.5  stop
9.0  aim elite 0.7
10.2 click
13.5 aim elite 0.3
14.0 click
17.5 aim elite 0.3
18.0 click
21.5 aim elite 0.3
22.0 click
25.5 aim loot 0.6
26.3 click
28.3 aim loot 0.6
29.1 click
31.0 aim loot 0.6
31.8 click
33.6 aim loot 0.6
34.4 click
36.2 aim loot 0.6
37.0 click
38.2 move 0.62 0.45 1.0
