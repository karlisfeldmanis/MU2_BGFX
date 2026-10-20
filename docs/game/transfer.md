# Moving a character to another region

Each region's game server keeps its own world and its own `characters.db`; the accounts are the
website's (server/README.md). A player moves a character from one region to another on the
website's Account page (the user, 2026-10-20: "we also need option to tranfer account to other
realm in website for our players").

## The rules (the user's picks, 2026-10-20)

- **What goes:** the character -- level, points, quests, what he wears, his bag, his Zen, his Chaos
  Machine. **The vault stays**: it is the account's on each server, and he finds that server's
  vault of his account where he arrives. He leaves his guild.
- **Free, once a week** (`TRANSFER_DAYS`), and one move an account at a time.
- **A name taken there** is asked for again: the site checks before asking, and if it is taken
  while he travels the move waits (held) for a new name on the Account page.
- Refused while he is in the game, while he has a Broker listing up or a Collect waiting, while he
  leads a guild, never played, or when the account's five slots there are full (held, if they
  filled while he travelled).

## How it goes

The website cannot reach the game servers; they reach it, through the accounts' line (the
`mu2-accounts-tunnel` on every box but Helsinki's). So the move is a row on the website
(`transfers` in web/server.py) that the two servers step along, each asking
`/internal/transfers?region=` every 15 s (server/src/transfer.cpp):

| state | who moves it on | what happens |
|---|---|---|
| asked | the server he leaves | checks him, sends his bytes (vault taken out), refuses him from then on → out; or failed (`broker`, `master`, `missing`); or stays asked with `online` |
| out | the server he leaves | leaves his guild, marks his row deleted (kept on disk, never read) → gone |
| gone | the server he goes to | makes him under the account with a new token in a free slot, his world kept → done; or held (`name`, `full`) |
| held | the player | a new name, or a slot freed, on the Account page → gone |

He is on one server or the other, never both: the bytes go first, the old row is marked only when
the website has them, and the new server takes only a gone move. A restart of the server he leaves
between out and gone lets him play there again until it marks him (he loses those minutes, nothing
is doubled). A move whose two servers are on different builds (item list or character layout)
waits on the new one until it is deployed.

Seen 2026-10-20 in a local run (two servers, the website, a botwatch save): a level-56 knight moved
in ~45 s with his level and Zen; one with Broker listings refused; a second move before a week
refused; a name taken while travelling held, a taken rename refused, a free one taken in.
Not yet seen: a player in the game while asked (`online`), a guild member's leaving.
