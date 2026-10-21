#!/bin/zsh
# MU2_BGFX's release, from its root folder (the user, 2026-10-21: "we need option to run it from
# project root folder", then "in MU2_BGFX root folder"). tools/mu/ship.py has the rules.
#
#   ./deploy.sh                 the Ship window (built first if it changed)
#   ./deploy.sh status          what is live where and what HEAD would ship
#   ./deploy.sh prepare         build and stage everything; players see nothing
#   ./deploy.sh switch [--now]  make what prepare staged live, the servers' players told 60 s first
#   ./deploy.sh notes ...       and any other `mu ship` action, passed on
mu=${0:A:h}/mu
case $1 in
  "")     exec $mu ship app ;;
  status) shift; exec $mu ship "$@" ;;
  *)      exec $mu ship "$@" ;;
esac
