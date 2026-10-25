# The Whirlwind rune's pull (sim Realm::whirl) and its crash (whirl_pull): a level-260 Blade Knight
# (Sevina's quests done; a Legendary rune asks 250) with a socketed Whirlwind sword, at a file of
# Giants five tiles east, rooted and unseeing, out of Twisting Slash's reach and within the
# pull's. He slashes in place on the right button; seed 3 pulls on the second: the file slides
# in under the scrape, and the crash is the last of them landing beside him.
# ./mu record whirl-pull --fps 60
args --arena giant --arena-count 7 --arena-gap 5 --arena-undying --unseen --no-hand --class 2 --level 260 --quests-done 13,14 --seed 3
args --arena-learn 41 --give Sword05:1:+9S1P27W,HelmMale02:1:W,ArmorMale02:1:W,PantMale02:1:W,GloveMale02:1:W,BootMale02:1:W
end 8

0.0   move 0.55 0.62
0.5   aim tile 170 66 0.6
1.5   rclick
3.0   rclick
4.5   rclick
6.8   move 0.62 0.55 0.9
