#!/bin/zsh
# The Golden Dragon, called on a running server now: rain, the dragons across the sky and the
# landing about thirty seconds later, as a wet spell's roll brings it (sim/invasion.h). For a demo
# server, so nobody waits for the weather.
#
#   ./mu dragon                    the one mu2_server on this machine; the first map with anyone
#                                  on it of Lorencia, Devias and Noria
#   ./mu dragon noria              that map (lorencia, devias or noria)
#   ./mu dragon --port 47231       the server listening on that port, when several run
#   ./mu dragon --host hetzner     a server over ssh, the same way
#
# It writes the map into dragon.call beside the server's --store and sends it SIGUSR1
# (server/src/main.cpp, callDragon in world.cpp), which reads and removes the file. The server's
# log says "dragon: called on <map>", or why not: one is out already, or nobody is on that map.
# Its clients must be built from 2026-10-15 on: the call goes out in the tick's rain byte
# (net::Tick::invade), and an older client would not take it and fall out of step.
set -e
map=""
port=""
host=""
while [ $# -gt 0 ]; do
  case "$1" in
    --port) port=$2; shift 2 ;;
    --host) host=$2; shift 2 ;;
    lorencia|devias|noria) map=$1; shift ;;
    *) echo "mu dragon: lorencia, devias or noria, --port N, --host H; not '$1'" >&2; exit 2 ;;
  esac
done

# One script for here and over ssh: plain sh, and lsof or /proc for the server's folder.
read -r -d '' call <<EOF || true
set -e
port='$port'; map='$map'
if [ -n "\$port" ]; then
  pid=\$(lsof -nP -iTCP:\$port -sTCP:LISTEN -t 2>/dev/null | head -1)
else
  pids=\$(pgrep -x mu2_server || true)
  [ \$(printf '%s\n' \$pids | grep -c .) -gt 1 ] && { echo "mu dragon: several servers run; say --port" >&2; exit 1; }
  pid=\$pids
fi
[ -n "\$pid" ] || { echo "mu dragon: no mu2_server running\${port:+ on \$port}" >&2; exit 1; }
args=\$(ps -o args= -p \$pid)
store=\$(printf '%s\n' "\$args" | sed -n 's/.*--store \([^ ]*\).*/\1/p')
[ -n "\$store" ] || store=characters.db
case "\$store" in
  /*) ;;
  *) if [ -e /proc/\$pid/cwd ]; then cwd=\$(readlink /proc/\$pid/cwd);
     else cwd=\$(lsof -a -p \$pid -d cwd -Fn | sed -n 's/^n//p'); fi
     store="\$cwd/\$store" ;;
esac
dir=\$(dirname "\$store")
if [ -n "\$map" ]; then printf '%s\n' "\$map" > "\$dir/dragon.call"; else rm -f "\$dir/dragon.call"; fi
kill -USR1 \$pid
echo "mu dragon: called\${map:+ on \$map} (server \$pid)"
EOF

if [ -n "$host" ]; then
  ssh "$host" sh -c "$(printf '%q' "$call")"
else
  sh -c "$call"
fi
