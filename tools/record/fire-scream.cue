# The Dark Lord's Fire Scream (docs/plans/dark-lord-spells/fire-scream.md): a level-200 Dark Lord among
# six Bull Fighters throws it off Q whenever it has cooled, his sword alone between (--arena-left: no Force,
# nothing else taught) -- the three lanes of fire down his aim, each body struck as a head passes it, now
# and then an Explosion's bombs.
# ./mu record fire-scream --fps 30
args --arena "Bull Fighter" --arena-count 6 --arena-left --arena-undying --class 4 --level 200 --teach 78 --weapon Sword02
end 20

0.0  move 0.5 0.5
0.4  aim monster 0.5
1.0  skill q
4.2  aim monster 0.4
4.8  skill q
8.2  aim monster 0.4
8.8  skill q
12.2 aim monster 0.4
12.8 skill q
16.2 aim monster 0.4
16.8 skill q
