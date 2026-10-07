# Nothing the client says

Begun 2026-10-08 on the user's "better check if all is synced, and there is no hardcoded client
stuff which is not like server". This page has two parts: an audit of what a client decides or
changes that the server does not, and what was fixed. Protocol 8.

## The audit

- **The client's mirror is written only where the server writes alike.** Every write to the
  writable realm (`local_`) is one of two kinds:
  - refused on a server by `Play::here()`: the save, vault and machine restores, `--give`,
    `--lay`, `--zen`, the castle and invasion switches, the sweep;
  - or the raise's own steps, done the same on both: `configure`, `raise`, `outfit`,
    `restoreKept`, and now `setCastle`.

  The arena, the raid and the roads are never set up on a server.
- **No debug or GM command reaches the server.** `sim::Command` has none, the server forwards
  every command but Join and Leave as the session's own player, and the realm refuses what its
  rules refuse.
- **Time, rain and the population are the server's.** The wall clock and the rain come in each
  tick, and every nest is raised.
- **What was the client's, and is fixed below:**
  1. Blood Castle's number.
  2. A new character's level, weapon, world and tile.
  3. Go Back!: its way back, its clock, its tile, and a `GoBack` command that set him down on any
     tile it named.
  4. The `arriving` door of protocol 4.
  5. The windows' layout on a server, which was never kept.
- **Found on the way:** `describe()` had no line for `Answered`. A seeded log that reached one
  wrote an unset buffer. Fixed.

## What was fixed — done 2026-10-08

- **Blood Castle on a server.** The client set the castle on the next world by `setCastle`,
  which a server refuses, so every castle there was castle 1, and two castles shared one world.
  Now:
  - the castle gate's landing takes the castle his ticket passed (`Realm::castlePassedOf`);
  - the server keeps one world for each castle (`World::castle`);
  - the Welcome's `castle` has the server and every mirror call `setCastle` straight after the
    raise.
- **A new character is the rules'** (`sim/cradle.h`: `kNewLevel`, `cradleWeapon`, `homeWorld`,
  moved from `game/roster.cpp`). Of the Hello, only the class is the client's. He is level 1
  with his class's weapon, at his class's town's spawn gate, and a first world elsewhere is
  answered Elsewhere. Loopback: a client asked for a level-40 elf in Lorencia at a tile of its
  own. It was told "born in noria", and came in at level 1 with the Short Bow at 174,112, with 6
  of 6 hashes agreeing.
- **Go Back! is the realm's** (`sim::WayBack` in `Player` and `HeroRecord`, `Realm::goBack`):
  - **Opened** by a Town Portal read in the field of a dungeon of floors, or a Tab trip out of
    one. A trip from anywhere else, or a gate to another map, gives it up.
  - **Its clock** is the realm's: five minutes of play, then the closed line for three seconds.
    A death clears it, and so does a landing on unsafe ground.
  - **It goes with the character:** in the Kept (`kKeptLayout` 4) between worlds and to
    `characters.db`. The store still reads layout-3 rows, with no way back.
  - **The `GoBack` command carries nothing.** On this map he is set down on the kept tile. To
    another, `What::WentBack` is said, and the server lands him there as it does a gate.
  - **The client** shows `Realm::wayBack()` and asks; `ctx.goBack` is only the landing sound
    now. The local save writes and reads the record's way back.
  - `testGoBack`: a knight reads a Town Portal in the Dungeon's field, is carried to Lorencia as
    the server carries him, and asks with a tile in the ask. The realm says `WentBack` to the
    tile it kept. The Batch-3 check that obeyed a client's tile now checks it is refused.
- **The Hello's `arriving` is gone.** Every way between worlds is the realm's to say, so a
  Hello for a world he is not in is always answered Elsewhere.
- **The windows' layout on a server** (`game::loadLayout`/`writeLayout`, `Name.ui` beside the
  save): the quick slots by item group and number, the skill bar and the quest he follows.
  Nothing else is written into the local save there.
- **Verified:** sim_test 6674 of 6674; on loopback, two players on protocol 8 with 17 and 29
  hashes agreeing; a quick slot kept across two runs on a server.

## Still the first player's, or not yet the server's

- The Golden Invasion, Blood Castle's run and win, and the raid belong to a world's first
  player.
- Drops have no owner.
- Another player's gear changing is not redrawn.
- There is no viewport.
- The lobby lists the local saves, not the server's characters (phase 6).
- A token is the whole login (phase 6).
