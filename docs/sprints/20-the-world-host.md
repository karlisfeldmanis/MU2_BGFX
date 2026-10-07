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
    (the token lives only as long as the client's process) and he is new again. Step 2.
  - The client still chooses the tile it lands on.
  - A connection lost while its Join waits loses what it carried. Step 2 as well: the store
    still has him.

## 2. The character on the server's disk — done 2026-10-07

- **`server::Store`** (`server/src/store.*`): `characters.db`, one row per token. The row holds
  the `sim::Kept` in the wire's own bytes (`net::putKept`, now public with `net::keptFrom`) and
  the protocol it was written at. His class, level, Zen and the time are beside it for whoever
  reads the file with `sqlite3`.
  - Uses the system's SQLite: the macOS SDK has it, and the box has it from `libsqlite3-dev`,
    which `deploy.sh` installs. `find_package(SQLite3)`, `mu2_server` only: the client carries
    no SQLite (foundation 11).
  - WAL, `synchronous=NORMAL`: a server crash loses nothing written; the box losing power loses
    at most the last write. A row written at another protocol is not read: he comes in new, and
    the log says so. A migration comes when `Kept` first changes shape.
  - **Why SQLite and not a file per token:** one transaction for the minute's writes, a file that
    `sqlite3` can be asked about, and phase 6's accounts (a login's characters, names taken)
    are queries it already answers.
- **The server** (`main.cpp`): the in-memory hour is gone; the store is the only keeper.
  - **Written:** as he leaves a world (`part`), every minute for everyone in one (`kKeepEvery`,
    OpenMU's rate), and as the server stops on SIGTERM or SIGINT. The minute's and the stop's
    writes are one transaction.
  - **Read:** a Hello's token the store has brings him back; any other is a new character and a
    new token. The token isn't taken off the store, so it brings him back every time.
  - **One place at a time:** a Hello for a character whose connection is still open is refused.
    One whose connection went in the same poll is parted first, so he comes back as it left him.
  - `--store FILE`, `characters.db` in the working folder by default (git ignores it). The
    service runs `--store /var/lib/mu2/characters.db` with `StateDirectory=mu2`, the one folder
    its throwaway user may write.
  - A store that will not open stops the server: what was played on it would be lost.
  - Tokens are seeded from `std::random_device`, not the clock: until accounts, a token is
    the whole of a login.
  - A connection lost while its Join waits now loses nothing; the store still has him.
- **The client** (`play_mode.cpp`, `game/save.*`): the token is kept beside the character's save,
  `hero.server` beside `hero.json`, one `host:port token` line per server.
  - The first world of a run reads it, unless the run is `--fresh`, and every Welcome writes
    back what the server answered.
  - A run with no save (a `--frames` review with no `--save`) keeps none, and a character
    deleted on the roster takes his token aside with him.
  - `sim_test` checks a character's stored bytes read back and say the same again, and that a
    row cut short is refused (6661 checks, the standing 10 failing).
- **Verified on loopback** (private `mu2_server --port 44599 --store` in a scratch folder, client
  `--save` in it):
  - **Across a restart.** A level-40 knight played and left, and the server wrote him and
    stopped. A new server process said "1 characters", and a client asking for level 1 came back
    as level 40 from the token beside its save. 6 of 6 hashes agreed. A `--fresh` run then came
    in new, level 1, under a new token, and the file beside the save took it.
  - A second copy of a playing character was refused ("already playing") while the first played
    on.
  - **The minute and the stop:** a client connected for 75 s was written at 60 s and again as the
    server took SIGTERM ("characters: 1 kept" both times).
  - `layercheck` and `save_test` pass.
- **On the box:** `deploy.sh` installed `libsqlite3-dev` and built there (sim_test 6661, the
  standing 10 failing; save_test and placement_test pass). The service opened
  `/var/lib/mu2/characters.db` empty. Then, from this Mac over the internet:
  - a level-40 knight played and left, and `systemctl restart mu2-server` stopped the service;
  - the new process said "1 characters", and a client asking for level 1 came back level 40;
  - 6 of 6 hashes agreed both times.

## 3. Where he lands is the server's — done 2026-10-07

- **The map table is the rules'** (`sim/maps.*`): the rows a world is, with its number, its spawn
  gate's tile and its home, and `mapOf`/`mapNumbered`/`mapAfter`, moved from `game/world/maps.*`.
  The game's header `using`s them, so not one call site changed. `zoneLevels`, `placeName` and
  `mapSheet` stay the game's.
- **The server reads where the realm sent him** (`landingOf` in `main.cpp`), after each step, from
  the same tables the client changes its map by:
  - `Gated`: the exit gate's map, at the tile the realm chose;
  - an answered `Travel` (matched to its command by ticket and player): the row's map and tile,
    unless it is a floor of this same map;
  - `Warped` or `Rose` with c 1 (a Town Portal read, a fall from flight, a death on a map with no
    safe zone): the map row's home, at its spawn gate;
  - Blood Castle's `sentOut`: its first player to Devias.

  He is then due there, by token, and the server says so ("sent to dungeon 108,247").
- **The Hello that follows is put down on the server's tile**, whatever the client asked. Both a
  raise (the Welcome's tile) and a Join (the command's c, d) carry it, so every mirror follows.
  A Hello for another world than the one he is due in keeps its own tile and the log says so. That
  happens on the ways the realm never sees: Go Back! to another map and `--travel-at`.
- The game stays identical, since the client already works out the same tile. The protocol is
  unchanged (3).
- **Verified on loopback:**
  - a level-40 knight put down by Lorencia's stair (`--at 122,226 --walk-to 122,233`) went down
    it; the server said "sent to dungeon 109,247" and the client came in there, at its own
    tile and the server's alike. 3 and 9 hashes agreed.
  - **A client that lies about it.** A scripted one (Python, the raw protocol) walked the same
    stair, then said Hello to the Dungeon at 50,50. The server logged "asked for 50,50; put down
    at 108,247", and the Welcome said 108,247.
  - sim_test's output is identical to step 2's (6661 checks, the standing 10 failing);
    `layercheck` and `save_test` pass.
  - **On the box** (`deploy.sh`, sim_test 6661 with the standing 10): the same stair from this
    Mac. The server said "sent to dungeon 110,247", the client came in there, 3 and 9 hashes
    agreed, and the step-2 knight was still in `characters.db` after the redeploy.
  - Not run end to end: the Tab trip, the death and portal home and Blood Castle's send-out.
    They are read by the same code as the gate, from the happenings the client already acts on.

## 4. He comes back in the world he left — done 2026-10-07

- **The store keeps his world** (a `world` column, added to a file from step 2 by `ALTER TABLE`;
  its old rows are empty, "wherever his Hello says"). The tile is the Kept's own column and row.
  - **What is written** (`placeOf`): the world he stands in at his tile; once the realm has sent
    him on (step 3's `landingOf`), the world and tile he is due in. Never inside an event: a
    Blood Castle left mid-run is written in Devias at its spawn gate, as WebZen logs him in and
    as the client's own save always did.
  - `kLayout`, the Kept bytes' shape, is now its own number (3), not the protocol's, so a
    protocol bump that leaves `putKept` alone does not orphan every row.
  - Step 3's in-memory hand-off is gone. A map change's Hello reads the world and tile that the
    store was given as the old connection went.
- **Protocol 4** (`net/wire.*`):
  - **`Hello::arriving`:** 1 when the run has played on this server already and comes by a map
    change, 0 for the run's first world.
  - **`Elsewhere{world, column, row}`:** the server's answer, instead of a Welcome, to a first
    world that is not his. Then the line is shut.
- **The server** (`hello`), for a character the store has:
  - Hello for his world: put down at the stored tile. That is where he stood when he quit, or
    where a gate or a trip sent him.
  - First world another one: answered Elsewhere.
  - Arriving in another one: let in at the Hello's tile, with a line in the log. Those are the
    ways between worlds the realm never sees: Go Back! and `--travel-at`.
- **The client:** `RemoteLink::join` reads an Elsewhere, and reads frames that came before a
  close (the old loop gave up on the close first). `Play::elsewhere()` keeps it, and PlayMode
  travels there as a map change does. The world that loaded first stands one frame with no
  realm, the cost of a first world that was wrong. `Args::serverArriving` remembers, across the
  rebuild, that the run has joined once.
- **Fixed on the way:** going back to the character screen and choosing another character kept
  the last one's token in `Args`. The server would have brought back the first character, and
  the second's file would have taken his token. Entering from the lobby now clears it.
- **Verified on loopback:**
  - **Back where he left.** A level-40 knight went down Lorencia's stair and quit; the store
    said "kept in dungeon at 108,247". The server restarted, and a new run asked for Lorencia at
    level 1. It was told "has him in dungeon at 108,247, not lorencia", opened the Dungeon, and
    played there as #1, level 40, with 7 of 7 hashes agreeing.
  - A run that opens the Dungeon he is in comes in at the stored tile.
  - `--travel-at` from the Dungeon to the Lost Tower was let in at its own tile, and the log
    said it was a way the server did not see; 2 and 7 hashes agreed.
  - The lying client, on protocol 4, was still put down at the server's tile (107,247 for
    50,50).
  - Step 2's file opened with its two rows, now with an empty world.
  - sim_test's output is identical to step 3's; `layercheck` and `save_test` pass.

## Next

- **Go Back! across maps** is still the client's own, and so is the tile it lands on. It becomes
  a realm command whose landing the server reads as it does a gate's, and `arriving` goes. Then
  every way between worlds is the server's, and a Hello names nothing but who he is.
- That is phase 3's last item. Its clock (sprint 18's mirror clock) and its population (every
  nest raised, sprint 19) are already done. After it, the plan's phase 5 (feel) or phase 6
  (accounts) is the user's choice.
