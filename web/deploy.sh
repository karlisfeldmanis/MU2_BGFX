#!/bin/zsh
# Puts MU2's website and account service on the Hetzner box and (re)starts it behind Caddy.
#
#   web/deploy.sh            copy, install, restart, check
#   web/deploy.sh --copy     copy only
#
# MU2_COMMIT pins the commit (HEAD by default), as server/deploy.sh: `mu ship switch` sends the one
# `mu ship prepare` staged.
#
# What goes is the commit, never the working tree (as server/deploy.sh): web/ at HEAD, exported
# with `git archive`. The accounts live in /var/lib/mu2-web/accounts.db and are never touched
# here. Installers go in /srv/mu2-web/downloads/ (web/README.md).
set -e
cd "$(dirname "$0")/.."
host=hetzner          # ~/.ssh/config: root@37.27.158.226
dest=/opt/mu2-web

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

# The Armory's data (tools/armory/build.py): out/armory/<release>/ to /srv/mu2-web/armory/<release>/,
# then `current` moved to it, which web/server.py (--armory /srv/mu2-web/armory) and Caddy
# (/armory/data/*) serve; the release is ARMORY (a commit), else the newest built. Its files are
# named by commit, so the release before stays served for pages already open; the four newest kept.
armory=${ARMORY:-$(ls -t out/armory/*/meta.json 2>/dev/null | head -1 | xargs -n1 dirname 2>/dev/null | xargs -n1 basename 2>/dev/null)}
if [ -n "$armory" ] && [ -f "out/armory/$armory/meta.json" ]; then
  ssh $host "mkdir -p /srv/mu2-web/armory"
  rsync -az --chmod=a+rX "out/armory/$armory/" "$host:/srv/mu2-web/armory/$armory/"
  ssh $host "cd /srv/mu2-web/armory && ln -sfn $armory current.new && mv -T current.new current && \
    ls -1dt */ | grep -v '^current' | tail -n +5 | xargs -r rm -rf"
  echo "the Armory of $armory is current on $host"
else
  echo "note: no built Armory in out/armory/ (./mu armory build); the site's /armory says so"
fi
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
