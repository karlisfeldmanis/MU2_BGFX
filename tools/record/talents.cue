# Talents, as a player meets them (docs/plans/talents.md): a level-200 Dark Knight with 99 points
# opens the character window, presses its Talents button, puts ten into Heft (the first point and
# the tenth unlock: the sword cut, the line filling), takes Frost Edge (the ring, the embers, the
# next tier waking), starts Pierce, reads Executioner's card, spends in Fury, gives a point back,
# resets twice and closes. Needs a level-200 knight with no talents: give the run his save,
# e.g. args --save /abs/path/knight.json (class 2, level 200), copied first.
# ./mu record talents
args --play --world lorencia
end 25

0.0   move 0.50 0.55
0.6   type character
1.4   move 0.9372 0.1704 0.9
3.0   click
4.0   move 0.2141 0.2250 0.7
5.0   click
6.0   click
6.35  click
6.7   click
7.05  click
7.4   click
7.75  click
8.1   click
8.45  click
9.2   click
10.2  move 0.2370 0.3287 0.5
10.9  click
12.6  move 0.2141 0.4231 0.6
13.3  click
14.2  move 0.2307 0.4861 0.6
15.8  move 0.6453 0.2250 0.7
16.6  click
17.0  click
17.4  click
18.2  move 0.2036 0.4231 0.7
19.0  click
19.9  move 0.7917 0.7981 0.8
20.9  click
21.8  click
23.2  move 0.8120 0.0463 0.7
24.1  click
