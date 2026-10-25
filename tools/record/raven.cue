# The Dark Lord's Dark Raven (docs/plans/dark-lord-pets/README.md): a level-250 lord with the raven on
# his left hand among four Spiders, the raven set to Random with Shift+2 -- it flies about him, dives
# on one (the dash) or hovers and lets its ball go, and his sword does nothing (--arena-undying keeps
# them standing for the frames).
# ./mu record raven --fps 30
args --arena Spider --arena-count 4 --arena-undying --class 4 --level 250 --give DarkRaven::W
end 14

0.0  move 0.5 0.45
1.0  shift-key 2
