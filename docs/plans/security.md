# Security: account theft and reverse engineering

The game's line (TCP 44406) is not encrypted. Until protocol 32 the launcher handed the game the
player's 90-day website token (`MU2_SESSION`), and the game said it on that line in every Account
and in the Hello of every map change. Anyone on the path could copy it, play as him, use the
website as him, and push him off his character (a second Hello for a character takes it over).

## What protects the game already (keep it)

- The server is authoritative (`server/src/world.cpp`, `sim::Realm`); clients send commands, and
  their mirror is checked by hash (`src/client/remote_link.cpp`).
- Flood limits: `kMostCommands` a tick, a 64 KB frame cap before sign-in (`net::kMostAsked`), the
  chat's allowance.
- The website: HTTPS behind Caddy, scrypt, tokens stored as SHA-256, rate limits (`web/server.py`).
- `--launcher-only` (`server/src/signin.cpp`, `door()`): from outside, only a launcher's pass;
  account-less lines only from loopback, which is how the bots (`tools/netbot`) play.
- Packs are scrambled, deliberately not encrypted (`src/core/pack.h`).

## 1. The game's pass (built, protocol 32)

Picked from three (2026-10-15): (a) a one-use ticket, then a session secret the server sends back
in the clear; (a+) the same, but the secret never crosses the line; (b) TLS on the game's line.
(a+) was built. TLS would also hide the traffic, at the cost of a TLS library cross-compiled for
mingw and bundled on both platforms, and a certificate; it can still go on top later.

How it goes (`src/net/pass.h`):

1. Play: the launcher posts its token to `POST /api/ticket` over HTTPS and gets
   `{ticket, secret}` -- 32 and 64 hex digits, good for two minutes, kept in the website's memory
   only (`engine.cpp gameTicket`).
2. The game is started with `MU2_TICKET=<ticket>:<secret>` in its environment; the token stays
   with the launcher (`engine.cpp play`).
3. Each line the game opens -- the character screen's Account, every world's Hello -- says
   `<ticket>.<count>.<proof>` where the key went: the count one higher than the line before, the
   proof HMAC-SHA256 of `"mu2 pass <ticket> <count>"` under the secret (`game::sealedKey`).
4. The first time the server sees a ticket, it asks `GET /internal/ticket?ticket=X` on the box
   (loopback, never proxied). The website tells `{account, secret}` once and forgets the ticket.
   The server checks the proof and keeps the secret for 24 hours after its last proof.
5. From then on the server checks each line itself: the proof must be the secret's and the count
   higher than the last proven. A line someone read and says again is turned away; so is the
   website's raw token, and a ticket the website no longer keeps.

What it does not do: hide the game's traffic, or stop someone who can rewrite packets on the path
from taking over a line already open. A server restart forgets the secrets: the player presses
Play again. The game's own crash reports go without the account (it never has the token); the
launcher's still carry it.

Tested on a private port (2026-10-15): a real client through the character screen, a world, a
map change and back, five lines on one ticket and one website ask; replayed, lower and
wrong-secret passes refused; the raw token and a bare machine key from outside refused; netbots
on loopback play as before on `--launcher-only`.

### Releasing it

Protocol 32: the server, both clients and the launcher go out together. Old games are refused by
protocol, and old launchers (`MU2Launcher/3`) by the website (`LAUNCHER_NEEDED = 4`), which update
themselves at their next start. Order:

1. `web/deploy.sh` -- `/api/ticket` and `/internal/ticket` (the old `/internal/session` is gone).
2. `server/deploy.sh` -- the server on protocol 32.
3. `./mu dist windows|macos`, `./mu release windows|macos` -- the games.
4. `./mu dist launcher`, `./mu release launcher` -- the launcher that asks for tickets.

A v3 launcher left open across the release starts the new game with no ticket, which says to open
the launcher; closing it and opening it again updates it.

## 2. Symbols (built)

`mu dist` strips the players' binaries and keeps each build's unstripped one privately in
`out/symbols/<commit>/<platform>/` (and a dSYM on the Mac). Crash reports are read against it
(web/README.md, Crash reports). Deleting `out/` loses them. The launcher exe was stripped already.

## 3. Next, to propose before building

- A per-address connection rate on the game's port: scanners already reach it ("not our
  protocol" in `journalctl -u mu2-server`).
- Server-side bot signs: inhuman command timing, sessions around the clock.
- Fewer secrets in environment variables where it is practical (the ticket's included).
