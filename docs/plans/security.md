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
   `{ticket, secret}` -- 32 and 64 hex digits, good for two minutes (`engine.cpp gameTicket`).
   The website keeps the secret until the server is told it, then its SHA-256 for a day.
2. The game is started with `MU2_TICKET=<ticket>:<secret>` in its environment; the token stays
   with the launcher (`engine.cpp play`).
3. Each line the game opens -- the character screen's Account, every world's Hello -- says
   `<ticket>.<count>.<proof>` where the key went: the count one higher than the line before, the
   proof HMAC-SHA256 of `"mu2 pass <ticket> <count>"` under the secret (`game::sealedKey`).
4. The first time the server sees a ticket, it asks `GET /internal/ticket?ticket=X` on the box
   (loopback, never proxied). The website tells `{account, secret}` once and forgets the ticket.
   The server checks the proof and keeps the secret for 24 hours after its last proof, in memory
   and in characters.db (`passes`), so a restart of the server does not sign anyone out.
5. From then on the server checks each line itself: the proof must be the secret's and the count
   higher than the last proven. A line someone read and says again is turned away; so is the
   website's raw token, and a ticket the website no longer keeps.

What it does not do: hide the game's traffic, or stop someone who can rewrite packets on the path
from taking over a line already open. The game's own crash reports carry its `ticket:secret` as
their Bearer over HTTPS, which the website files under the ticket's account for a day.

Since 2026-10-15 (second pass): a server restart keeps the passes, tested by restarting mid-session
on a private port; and the game's crash reports name the account again.

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
`out/symbols/<commit>/<platform>/` (and a dSYM on the Mac), and `mu release` copies them to
`/var/lib/mu2-symbols/<commit>/` on the box (root's, served by nothing). Crash reports are read
against them (web/README.md, Crash reports). The launcher exe was stripped already.

## 3. The door's limits (built 2026-10-19, server only)

From an audit of what a hand-made client could do. The four that mattered, each fixed in the
server alone (no protocol change, no client release), and the constants in `server/src/host.h`:

- **A trade's partner gone in the tick of the other's Accept duplicated the offer.** His
  character was kept with the offer back in his bag, then her Accept, queued ahead of his
  Leave, swapped it. `part()` now puts his Leave first, so the trade is called off before.
  `server_test` "deal leave" fails without it.
- **A stream of Pings held the loop's one thread.** Each frame erased from the front of the
  buffer moved the rest; `net::take` now reads from an offset and the buffer is trimmed
  once a pass. At most `kMostPings` (10) are answered a second, and a line with more than
  `kMostUnread` (1 MB) unread is let go.
- **Idle sockets took every file the box would open**, and `accept()` failing left the poll
  spinning. A line with no account or world is let go after `kSignInWithin` (30 s), one address
  holds at most `kMostWaitingFromOne` (8) such lines, the server `kMostLines` (4000) in all,
  and the service has `LimitNOFILE=16384`. This machine's own lines (the bots) are not counted.
- **Made-up passes stalled the loop three seconds each**: a gone line's `std::async` waited on
  its destruction. Its answer is now kept till it comes (`letGoOf`), and past 64 asked at once
  a new pass is refused.

Still open from the audit: the game's line is plain TCP (a session can be taken over by
someone on the player's network), the launcher's updates are not signed, the website's
`/internal/*` trusts any process on the box, and chat item links can be forged.

## 4. Next, to propose before building

- Server-side bot signs: inhuman command timing, sessions around the clock.
- Fewer secrets in environment variables where it is practical (the ticket's included).
