#!/bin/zsh
# Puts MU2's website and account service on the Hetzner box and (re)starts it behind Caddy.
#
#   web/deploy.sh            copy, install, restart, check
#   web/deploy.sh --copy     copy only
#   web/deploy.sh --armory   the Armory's data only: upload and move `current`, nothing restarted
#
# MU2_COMMIT pins the commit (HEAD by default), as server/deploy.sh: `mu ship switch` sends the one
# `mu ship prepare` staged. ARMORY names the Armory's release (`mu ship switch` passes its commit),
# else the live release's (the macOS manifest's version) -- never the newest built. DRY=1 says the
# Armory's commands instead of running them.
#
# What goes is the commit, never the working tree (as server/deploy.sh): web/ at HEAD, exported
# with `git archive`. The accounts live in /var/lib/mu2-web/accounts.db and are never touched
# here. Installers go in /srv/mu2-web/downloads/ (web/README.md).
set -e
cd "$(dirname "$0")/.."
host=hetzner          # ~/.ssh/config: root@37.27.158.226
dest=/opt/mu2-web

# The Armory's data (tools/armory/build.py): out/armory/<release>/ to /srv/mu2-web/armory/<release>/,
# then `current` moved to it, which web/server.py (--armory /srv/mu2-web/armory) and Caddy
# (/armory/data/*) serve. The release is ARMORY, else the live one. Refused, and `current` left as
# it is, when that build is missing, is a `.new` (unfinished) or a SEED (history only), or has no
# TESTED (`./mu armory test out/armory/<commit>` writes it on a pass). Sent against the release
# `current` is now (rsync --link-dest), so unchanged files are linked on the box, not sent again.
# Its files are named by commit, so the release before stays served for pages already open. Kept
# on the box: `current`'s, the one before it, this one and the newest four by meta.json `built`.
armory_deploy() {
  local box=/srv/mu2-web/armory run=""
  [ -n "$DRY" ] && run=echo
  local a=${ARMORY:-$(curl -sf -m 20 https://mu-classic.com/game/macos/manifest.json | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])' 2>/dev/null)}
  local d="out/armory/$a"
  if [ -z "$a" ]; then
    echo "the Armory: no ARMORY and the live manifest unread; current left as it is"; return 1
  fi
  if [[ "$a" == *.new ]] || [ ! -f "$d/meta.json" ]; then
    echo "the Armory: no finished build of $a in out/armory/ (./mu armory build --commit $a); current left as it is"; return 1
  fi
  if [ -e "$d/SEED" ]; then
    echo "the Armory: out/armory/$a is a history-only build (SEED); current left as it is"; return 1
  fi
  if [ ! -f "$d/TESTED" ]; then
    echo "the Armory: $a is not tested (./mu armory test $d); current left as it is"; return 1
  fi
  local was
  was=$(ssh $host "readlink $box/current 2>/dev/null" || true)
  local link=()
  [ -n "$was" ] && [ "$was" != "$a" ] && link=(--link-dest="$box/$was")
  $run ssh $host "mkdir -p $box"
  $run rsync -az --chmod=a+rX "${link[@]}" "$d/" "$host:$box/$a/"
  $run ssh $host "cd $box && ln -sfn $a current.new && mv -T current.new current"
  $run ssh $host "cd $box && python3 - $a ${was:--} <<'EOF'
import json, os, shutil, sys
keep = {sys.argv[1], sys.argv[2], os.path.basename(os.path.realpath('current'))}
built = []
for n in os.listdir('.'):
    if n == 'current' or os.path.islink(n) or not os.path.isdir(n):
        continue
    try:
        built.append((json.load(open(os.path.join(n, 'meta.json'))).get('built', ''), n))
    except (OSError, ValueError):
        if n.endswith('.new'):
            shutil.rmtree(n)
keep |= {n for _, n in sorted(built, reverse=True)[:4]}
for _, n in built:
    if n not in keep:
        shutil.rmtree(n)
        print('pruned', n)
EOF"
  echo "the Armory of $a is current on $host${was:+ (was $was)}"
}
if [ "$1" = "--armory" ]; then
  armory_deploy
  exit $?
fi

commit=$(git rev-parse --short "${MU2_COMMIT:-HEAD}")
here=$(git rev-parse --show-prefix)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
git -C "$(git rev-parse --show-toplevel)" archive "$commit:${here}web" | tar -x -C "$stage"
echo "$commit" > "$stage/DEPLOYED"
# mktemp makes the folder 0700 and rsync -a carries that over; the service's own user must read it.
chmod -R a+rX "$stage"
if ! git diff --quiet "$commit" -- web; then
  echo "note: web/ differs from $commit; $commit goes, not those edits"
fi

ssh $host "mkdir -p $dest /srv/mu2-web/downloads"
rsync -az --delete "$stage/" $host:$dest/
echo "copied $commit to $host:$dest"

armory_deploy || echo "(the site goes on without it)"
[ "$1" = "--copy" ] && exit 0

# Caddy 2.10 or later: Ubuntu's own (2.6) cannot ask Let's Encrypt for an IP certificate, so it
# comes from Caddy's official apt repository.
ssh $host 'v=$(caddy version 2>/dev/null | sed -E "s/^v([0-9]+)\.([0-9]+).*/\1\2/"); \
  if [ -z "$v" ] || [ "$v" -lt 210 ]; then \
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q gpg curl >/dev/null && \
    curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | gpg --dearmor --yes -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg && \
    curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt > /etc/apt/sources.list.d/caddy-stable.list && \
    apt-get update -q >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -q caddy >/dev/null; \
  fi; caddy version'

# The service and Caddy's config, the web ports open, both restarted.
ssh $host "install -m 644 $dest/mu2-web.service /etc/systemd/system/mu2-web.service && \
  install -m 644 $dest/Caddyfile /etc/caddy/Caddyfile && \
  caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile >/dev/null && \
  systemctl daemon-reload && systemctl enable --now mu2-web caddy >/dev/null 2>&1; \
  systemctl restart mu2-web && systemctl reload-or-restart caddy && \
  ufw allow 80/tcp >/dev/null && ufw allow 443/tcp >/dev/null && sleep 2 && \
  systemctl is-active mu2-web caddy && curl -s -o /dev/null -w 'local: %{http_code}\n' http://127.0.0.1:8080/"
