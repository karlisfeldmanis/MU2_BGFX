# The world host

Server-plan phase 3 (docs/server-plan.md §4), begun 2026-10-07 straight after phase 2 (sprint 19),
on the user's "do next". The game is an MMORPG now, not single player (the user, the same day).

## Where it starts

Sprint 19 left the server already a world host in part:
- one process, every map on one 20 Hz loop, one `World` each;
- the server owns the tick and the wall clock;
- every nest is raised, whether or not this client has cooked a figure for it.

What it did not do was keep the character. A map change rebuilds the client's mode and opens a
new connection, and the new Hello said the lobby's class and level. **Everything a player earned
on the server was lost at every gate, trip and death sent home.**

## 1. The character goes with him — done 2026-10-07

- **`sim::Kept`** (`sim/realm.h`): all of a character between worlds, which is the `HeroRecord` a
  save keeps (level, experience, points, Zen, every slot, quests, skills, cooldowns, buffs),
  his vault and the Chaos Machine's box.
  - `Realm::keptOf(id)` reads one off a player.
  - `restoreKept` lays one on whomever the realm looks at (`restore` plus vault and box).
  - `carry(ticket, kept)` queues one for the `Join` with that ticket at the next tick's start:
    laid on the newcomer after his cradle, and dropped if no Join that tick claims it.
- **Protocol 3** (`net/wire.*`):
  - Hello and Welcome carry a **token**, the server's word for the character.
  - A Tick carries its **arrivals** (`{ticket, Kept}`, laid with `carry` before the commands).
  - A Welcome carries the world's **kept first player** when the world was raised round one.
  - All of it is explicit little-endian fields, Held by Held, and floats by their bits.
- **The server** (`server/src/main.cpp`): a new connection gets a fresh token.
  - When it goes, his character is kept under the token (`keptOf`) for an hour.
  - A Hello with a kept token brings him back once, into whichever world it names:
    - by `Join` plus arrival into a running world, with his class and level from the record
      and no cradle;
    - or as the first player of a world raised round him (`restoreKept` after the raise, and
      the Welcome's `kept` so every mirror does the same).
  - The tile is still the Hello's: where the client's gate, trip or warp put him.
- **The client:** `core::Args::serverToken` lives across the mode's rebuild. `Play::useServer`
  sends it in the Hello and keeps the Welcome's answer, and the mode writes that answer back
  after each open. `RemoteLink::step` lays the tick's arrivals on the mirror. `Play::open` lays
  a kept first player on the mirror after the cradle, and once the figures are made it redresses
  the hero in what he really wears.
- **`testKept`** in sim_test: a level-50 knight takes his kept self through the wire as a Tick's
  arrival into another world's Join. That means his Zen, spent and unspent points, an axe +3
  with luck and its option, three potions, a jewel and 777 Zen in the vault, and a jewel in the
  box, every one of them there on the other side. The first player is untouched, and a carried
  character no Join claims in its tick is dropped (sim_test links `mu_net` for the wire now).
- **Verified on loopback** (private `mu2_server --port 44599`):
  - **A knight travels to a world another player is in.** A level-40 knight went Lorencia to
    Noria by `--travel-at 600` while an elf played there. The server kept him as he left and
    brought him back into Noria as #1015 by Join and arrival. The elf's picture drew him coming
    in, dressed, level 40, and every hash agreed: 8 in Lorencia, 17 in Noria, and 31 for the elf.
  - **He travels alone.** He raised an empty Noria as its kept first player, and 4 and 9 hashes
    agreed.
  - **No regressions:** the two-client run and the single client served by Lumen agree as
    before. sim_test 6658 checks, the standing 10 failing; outside the new test, its output is
    identical to before. `layercheck` and `save_test` pass.
- **Not yet:**
  - The characters live in the server's memory: a restart, an hour away, or quitting the game
    (the token lives only as long as the client's process) and he is new again. The server-side
    character store, `characters.db`, is next, and with accounts (phase 6) a character is his
    login's, not a token's.
  - The client still chooses the tile it lands on.
  - A connection lost while its Join waits loses what it carried.

## Next

- The character store on the server's disk: `characters.db` (SQLite), written on leaving a world,
  every minute and on quitting; the token kept by the client between runs until accounts come.
- The landing tile decided by the server from the gate, trip or warp the realm said, not by the
  client's Hello.
