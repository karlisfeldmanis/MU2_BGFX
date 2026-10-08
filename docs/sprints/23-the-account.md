# The account

Begun 2026-10-08, server-plan phase 6's first half. The user: "we are going away from local
saves, we have to use server/client envorment", after a character geared on the server stood
naked on the character screen. That screen read `saves/characters`, which a server's character
never writes. Protocol 10.

## The decisions — the user, 2026-10-08

- **A machine key, not a login screen, for now.** The client makes a random key once
  (`saves/account.key`, 32 hex digits), and the server ties characters to it. A name and password
  can be laid over the same accounts later.
- **The server always.** The character screen talks to the Hetzner box (`net::kDefaultHost`)
  unless `--server` names another. Tests, the bench and `--new` keep their own ways.

## What was done — 2026-10-08

- **The wire** (`net/wire.h`):
  - `Account` carries the key and any claims, and is answered with a `Roster`.
  - `Create` (a name and a class) and `Delete` (a token) are each answered with the `Roster`
    again, with how the ask went (`net::Refused`).
  - A `Roster` seat holds his token, slot, name, class, level, promotion, the world he comes in
    and what he wears.
  - The `Hello` gained `account`.
- **The store** (`server/src/store.*`):
  - `characters` gained `account`, `name`, `slot` and `deleted`, added to an older file at their
    defaults.
  - A character made on the screen is a row at layout 0 with no bytes. His first Hello makes him
    as a new character is made, under that token and in that class.
  - A deleted character is stamped and kept, never erased.
  - Saving is an upsert, so a save never overwrites whose a character is.
- **The server's rules** (`server/src/main.cpp`, with `sim/cradle.h`'s `goodName`,
  `kRosterSlots`, `kGladiatorLevel`):
  - five characters an account;
  - a name of 4 to 10 letters and digits, unique on the server in any case;
  - the Magic Gladiator only once a character of the account has reached level 220;
  - no deleting a character who is in a world.
- **Who may play whom:**
  - A character of an account is played only with its key.
  - An account's characters are made only by `Create`; a Hello with a key and no token is
    refused.
  - A character of no account is still played by its token alone. That door is the bench's:
    `--new`, `bot` and the net bot.
- **Claims:** the screen sends the tokens this machine kept for that server
  (`saves/characters/Name.server`) with their names. A character of no account is put on this
  one, so `karlis1` came across with his level 37 and his gear. This is the one thing still read
  from `saves/characters`.
- **The client:**
  - `LobbyMode` opens an `AccountLink` (`game/remote_link.h`) and stands the server's Roster
    (`game::seatsOf`). Create and Delete ask over it.
  - Enter hands Play the token, the key and the server.
  - The window layout, the client's own preference, is kept in `saves/layouts/Name.ui`.
  - `readRoster`, `makeCharacter`, `dropCharacter` and `--roster` are gone.

## Checked — 2026-10-08

On a local server raised on a copy of the Hetzner `characters.db`:

- The new columns were added to its 11 rows.
- Over the wire, from a script:
  - `karlis1` was claimed at level 37 with 9 worn things, and claiming again changed nothing.
  - Two characters were made.
  - Refused: a name taken in another case, a short name, and a Gladiator below 220.
  - A character was deleted; someone else's was refused.
  - A Hello for `karlis1` was refused with another key and with none, and refused with a key and
    no token. With his key it was welcomed.
- With the client:
  - The screen stood `karlis1` in his armour and a new elf with her bow.
  - Entering the elf, never played, made her in Noria. Entering `karlis1` laid him from the
    snapshot, 6 of 6 hashes agreeing.
- sim_test: 6683 of 6683. save_test: 0 failures.

## Next

- A login screen: a name and a password hashed on the server, mapping onto these accounts.
- The build stamp checked on connect (phase 6).
- GM commands by account.
