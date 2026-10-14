# web/: MU2's website and accounts

One page where players make an account and download the client, and the account service the
launcher signs in with. Python's standard library only, SQLite, behind Caddy on the Hetzner box.
Design: the "MU2 Launcher" canvas, Website page.

| File | What |
| --- | --- |
| `server.py` | the page, `/api/register`, `/api/login`, `/api/logout`, `/api/me`, `/api/downloads`, and `/internal/session` for mu2_server |
| `site/` | `index.html` and the logo |
| `Caddyfile` | HTTPS on mu-classic.com (and still on 37.27.158.226 for launchers already out), installers from disk, the rest to server.py |
| `mu2-web.service` | the systemd unit; accounts in `/var/lib/mu2-web/accounts.db` |
| `deploy.sh` | HEAD's `web/` to `/opt/mu2-web`, Caddy installed if missing, both restarted |

## Run it here

    python3 -I web/server.py --db out/web/accounts.db --port 8093
    open http://127.0.0.1:8093/

## The accounts

    accounts(id, email UNIQUE NOCASE, password 'scrypt$n$r$p$salt$key', created)
    sessions(token_hash, account_id, created, expires, last_used)   -- 90 days

The launcher posts `{email, password}` to `/api/login` and keeps the 64-hex-digit token it gets
back. Only the token's SHA-256 is stored. The token has the shape of the game's account key
(`net::Account`, `keyShape` in server/src/accounts.cpp); the next step is for mu2_server to ask
`GET http://127.0.0.1:8080/internal/session?token=T` and put the player's characters on account
`T`'s id instead of the machine key. Caddy answers 404 for `/internal/` from outside.

On the box: `sqlite3 /var/lib/mu2-web/accounts.db 'select id, email, created from accounts'`.
To delete one by hand, start with `PRAGMA foreign_keys=ON;` or its sessions stay behind: the
sqlite3 shell does not cascade unless told.

## Game releases (the launcher's)

`./mu release windows` puts `dist/windows/MU2/` under `/srv/mu2-web/game/windows/`: each file
at `files/<sha256>`, and `manifest.json` (version, protocol, every file's path, size and hash),
written last. https://mu-classic.com/game/windows/manifest.json is what the launcher reads (the
launchers built before the domain still read it off 37.27.158.226, which Caddy answers too).

## Installers

Copy them to `/srv/mu2-web/downloads/`: `MU2Launcher.exe` (the launcher, stripped with
`x86_64-w64-mingw32-strip`, which installs the game from `/game/windows/`) and `MU2.dmg`. The page asks
`/api/downloads` and shows each one's size, or "coming soon" while it is missing.

## Machine reports

The launcher's report of the machine it opened on -- card, driver, memory, OS, no account in it
(docs/plans/launcher.md, L7): one JSON line per run in `/var/lib/mu2-web/machines.jsonl`, beside
the accounts and never touched by web/deploy.sh. Read it on the box:

    curl -s http://127.0.0.1:8080/internal/machines   # counted: launches, machines, cards, drivers
    tail -5 /var/lib/mu2-web/machines.jsonl           # the raw reports

## Crash reports

A crash or a freeze of the game or the launcher leaves a report in the player's `logs/`
(src/core/watch.h); the launcher sends what is waiting when it opens and 20 s after Play, the game
when it starts (src/launcher/crash_upload.h), to `POST /api/crash`. Each lands in
`/var/lib/mu2-web/crashes/<day>/` -- the .txt, and on Windows its minidump -- with a line in
`crashes.jsonl`: which program, the platform, the card, the build, the module it died in, and the
account's id when the launcher signed in. Read them on the box:

    curl -s http://127.0.0.1:8080/internal/crashes    # counted by module, and the latest thirty
    ls /var/lib/mu2-web/crashes/$(date -u +%F)/

A report's first lines say where: `in nvwgf2umx.dll+0x1a2b3c` is NVIDIA's driver, `in MU2.exe+0x…`
is ours -- `x86_64-w64-mingw32-addr2line -f -C -e MU2.exe 0x140000000+offset` against that
release's exe (its sha256 is in the manifest; the box keeps every file under /srv/mu2-web/game/).
On the Mac, `atos -o MU2.app/Contents/MacOS/MU2 -l <loaded at> <address>`.
