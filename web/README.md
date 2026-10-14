# web/: MU2's website and accounts

One page where players make an account and download the client, and the account service the
launcher signs in with. Python's standard library only, SQLite, behind Caddy on the Hetzner box.
Design: the "MU2 Launcher" canvas, Website page.

| File | What |
| --- | --- |
| `server.py` | the page, `/api/register`, `/api/login`, `/api/logout`, `/api/me`, `/api/downloads`, and `/internal/session` for mu2_server |
| `site/` | `index.html`, the wallpaper and the logo |
| `Caddyfile` | HTTPS on 37.27.158.226 (a Let's Encrypt IP certificate), installers from disk, the rest to server.py |
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

## Installers

Copy them to `/srv/mu2-web/downloads/` as `MU2Setup.exe` and `MU2.dmg`. The page asks
`/api/downloads` and shows each one's size, or "coming soon" while it is missing.
