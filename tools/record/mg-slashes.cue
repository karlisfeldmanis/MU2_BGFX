# The Magic Gladiator's two Season skills (docs/plans/mg-skills/): a level-150 MG with a Dark Reign
# Blade +11 (Power Slash comes with the blade) and Fire Slash taught, at a pack of six Bull
# Fighters that do not fall. Three Fire Slashes on Q -- the ribbon, and the skulls on the bodies
# whose defence it cuts -- then three Power Slashes on W -- the lightning trail and the five waves.
# ./mu record mg-slashes --fps 60
args --arena bull_fighter --arena-count 6 --arena-undying --no-hand --class 3 --level 150
args --teach 55 --give Sword22:1:+11W
end 20

0.0   move 0.55 0.62
0.6   aim monster 0.7
1.5   skill q
4.0   skill q
6.5   skill q
9.5   skill w
12.0  skill w
14.5  skill w
17.5  move 0.62 0.55 0.9
