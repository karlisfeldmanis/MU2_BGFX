# The name and bar over a figure hold still (24c6b13a): a Dark Knight on a local server (so he
# has a name) stands, runs, stops, runs back and points at a monster -- the label stays put
# through the idle and the run. Needs a private server first:
#   out/build/mu2_server --port 44577 --store <scratch>/world.db
# ./mu record label-hold --fps 30
args --play --world lorencia --class 0 --server 127.0.0.1:44577
end 16

0.0  move 0.5 0.5
3.0  run 0 1
7.0  stop
9.0  run 1 0
12.0 stop
13.0 run 0 -1
