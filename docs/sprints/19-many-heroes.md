# Many heroes

Server-plan phase 2 (docs/server-plan.md §4), started 2026-10-07 after sprint 18 put the client on
the Hetzner server with one player per world. **The goal: several heroes in one realm, and clients
on the same world sharing it,** so two players see each other.

The user, the same day: *"this will not be a single player game anymore but standart mmorg
experience"*. So single player is not a mode to keep (server-plan §5, decision 2): `LocalLink`
stays as the harness the tests and the headless runs drive, not as a product.

**The rule for each step:** the game identical after it. `sim_test`'s whole output (timestamps
stripped) the same line for line, `layercheck` passing, and a loopback run served by Lumen with
every hash agreeing.

## The steps, in order

1. `Realm::Player`: every per-hero field out of `Realm`. Done 2026-10-07, below.
2. Every player steps, in id order; commands to `Command::player`; kill credit, experience, drops,
   guards, traps and townsfolk per player; happenings with an audience; a two-hero `sim_test` case.
   Done 2026-10-07, below.
3. Join and leave: a second hero by `Realm::spawn`; leaving marks the body gone, never erases it.
   Done 2026-10-07, below.
4. The server: connections to one world share its realm; the client draws other heroes; a client
   joining mid-session replays the world's input log from the Welcome; `net::kVersion` bumped.
   Done 2026-10-07, below.

## 1. The Player — done

- `Realm::Player` (`sim/realm.h`, at the end of the class) holds what is one per hero: the
  pending order and the order, the skill wish, the charge and its damage, the Arcane Echo
  waiting, `undying`, the equip refusal, the bag and the money, every open window (`trading`,
  `banking`, `mixing`, `gating`, `angeling`, `questing`), the buy-back list, the vault, the
  machine and its `mixed`, the castle's owed Enter, hand-in and claim and the castle passed,
  the quests and `found`, the potion cooldown and its sips, the summon's slot and the summon a
  save owes, the wear's fractions and the weapon's last wear, and `grounded`.
- `body` is his index in `bodies_` (bodies never leave, so it holds). The realm holds
  `std::vector<Player> heroes_`, made with one, and `me_`, the one it is working for:
  `me()` is that Player and `mine()` his body. Every `bodies_[0]` in the rules is `mine()`.
- **Kept with the world, decided case by case:**
  - `run_`, Blood Castle's run: its phase, clock, quotas and drawbridge are the castle's, and
    a castle is fought by a party. Its pay (`paidExperience`, `paidZen`, `paidRunes`,
    `claimed`) is a winner's; it moves to the Player in step 2 if a castle holds two.
  - `flights_`, `spiritBlows_`, `fires_`: fixed pools of the world's, which step 2 gives an
    owner per entry rather than a pool per player.
  - `loosingPlague_`: a flag set and cleared inside one call.
  - `wingDemo_`, a GM switch for the map.
- The Player is not rebuilt by `raise()`. It resets the same fields it always reset, and the
  bag and money carry across a raise as before.
- sim_test's whole output identical to before, 371,750 lines (6610 checks, the standing 10
  failing); `layercheck` and `save_test` pass; a loopback run was served by Lumen with 19 of 19
  of the server's hashes agreeing.

## 2. Every player steps — done

- **`step()`** (`realm.cpp`): the world's part -- the commands, the castle, the invasion, the raid
  -- then each player's first half (`heroBefore`: the castle gate owed, the summon a save owed,
  potions, the charge, recovery, Icarus, his floor), the ground's vanishing once, each player's
  second half (`heroAfter`: his blow landing, his flights and fires and spirits, his order, or his
  rising), the traps once, then the monsters. Players go in the order they joined, which is id
  order. The monster loop skips players, and the world's own work runs for the first player, as
  it did for the one. One player's order of happenings is exactly the old one.
- **`Realm::For`**, a scope that points the realm at a body's player and puts it back after.
  It sits at:
  - every command (`Command::player`: 0 is the first player; an id that is no player's is
    dropped unanswered);
  - `strikeAt`, for whichever side is a player (his charge, his wear, his potions);
  - `kill`, the fallen player's own and each payment;
  - `advance` for a player's run, ride or flight;
  - each player's castle hand-in and claim.
  The scope keeps an index, not a pointer, so `heroes_` may grow under it.
- **Kill credit:** `Body::heroStruck` is the id of the player whose blow last landed, not a bool.
  A guard's kill a player helped pays that player; a summon's kill pays its owner; a raider's
  kill pays the first player (the raid is still one hero's, below); a player's own kill pays him.
  Quests count it for the same player.
- **The world's things, per player:**
  - The guards put a monster on *any* player before one at the gate.
  - A stroller stops for the first player talking to him, and his chat pauses while any player
    is at his partner's counter.
  - Each trap that is due fires once and checks every live player in turn (`fireTrap`). It
    fires only while some player is alive, as it did while the one hero lay dead.
  - The ground's `Vanished` is said on the first player.
- **The pools have an owner per entry** (`Flight`, `SpiritBlow`, `Fire`: `owner`, set after the
  positional fields). Each player lands his own, and his death clears his own. A raider's rune
  chains (`hop` off a raider's Fireburst) belong to no player and land for the first player, as
  they did. `spiritsGoing` asks one owner.
- **Happenings have an audience** (`Happening::audience`, the player's body id, or 0 for everyone).
  `say()` gives a player's own business to him alone: Gained, Drank, Served, Bought, Sold,
  Learned, Worn, Repaired, Refined, Enlivened, Set, Offered, the Quest ones, Arrowless, Mixed,
  Answered, PetLost and Barred. His blows, his fall, his level's flash and a thing leaving the
  ground are everyone's. Every mirror still holds every happening; the client's filter is step 4.
- **`Realm::join(kin, level, column, row)`** came forward from step 3 because the test needs it.
  It dresses a hero as `raise()` does (`dressNew`, shared with raise), with a Player of his own
  and his own dormant summon body, both through `spawn()`, and says `Spawned`. Also new:
  `playerCount()`, `playerAt(i)`, and `lookAs(id)` to choose whom the queries answer for (step()
  comes back to it). `raise()` drops whoever joined the last raise.
- **Still one hero's, on purpose:**
  - the Golden Dragon raid's party (the first player is its index 0);
  - where an invasion lands (near the first player);
  - Blood Castle's win (whoever hands the staff in; an unclaimed win is paid to the first player
    as the run sends them out).
  Each is its own decision in server-plan phase 7.
- **`testTwoHeroes`** in sim_test: a wizard joins a knight's world. Each has his own bag and
  purse, and `lookAs` refuses a stranger. His Order walks him and not the first. His potion is
  answered to him alone, and an ask from no player is dropped. `smite` with the queries on him
  pays him and not the first. Then two level-60 knights hunt side by side for 1200 ticks, twice:
  the two runs hash the same, every one of the 6 Gained is said to the one it paid, and monsters
  struck the second 23 times.
- **Verified:** sim_test 6631 checks, the standing 10 failing. Outside the new test, the output
  is identical line for line to before. `layercheck` and `save_test` pass. A loopback run was
  served by Lumen with 19 of 19 hashes agreeing.

## 3. Join and leave — done

- **Two commands, the server's and never a client's** (`sim/command.h`):
  - `Join` (a: the Kin, b: the level, c and d: the tile) runs `Realm::join` and answers with
    the new body's id.
  - `Leave` (`player`: who leaves) runs `Realm::depart`.

  Applied at a tick's start like every command, so lockstep carries them for free: the server
  puts them in a tick's inputs, and every mirror opens and shuts the same door on the same tick.
- **`Realm::depart(id)`** marks the body `gone` and leaves it where it stands, never erased, so
  every id and every index after it holds for the mirrors. What goes with him:
  - his summon is dismissed;
  - his windows shut, and the machine's box goes back into his bag;
  - his order, his wish, his charge, his echo, his blow and his flights, fires and spirits are
    spent;
  - whatever held him forgets him;
  - he leaves `players_`, so no monster wakes for him (rouse counts the dead, who still stand
    there, but not the gone).

  It says `Left`. A gone player is not stepped, and a command from him is dropped unanswered.
  `playersHere()` counts those not gone.
- **Not done yet:** drops have no owner, so anyone may pick anything up, as before. MU's loot
  ownership belongs with party loot rules (server-plan phase 7).
- `testTwoHeroes` grew:
  - the second leaves by command: Left is said, his body keeps its place and nothing after it
    moves, he is gone and not alive, his summon is down, nothing holds him, and his orders move
    nothing;
  - a Join command lets an elf in with the next id;
  - the hunt now has the second leave at tick 600 and an elf join at 700, and both runs still
    hash and count the same.
- **Verified:** sim_test 6643 checks, the standing 10 failing; outside the two-hero test, the
  output is identical line for line to before. `layercheck` and `save_test` pass; the client
  builds with no new warning; a loopback run was served by Lumen with 19 of 19 hashes agreeing.

## 4. One world, many connections — done

- **The server** (`server/src/main.cpp`) keeps a `World` per map name: its tables, its realm, its
  start (the Welcome every mirror raises from), the commands queued for its next tick, and its
  log, every tick since it was raised.
  - **The first Hello** for a world raises it round him, as sprint 18 raised one per connection,
    and welcomes him with no past.
  - **A later Hello** queues a `Join` at the next tick's start. It carries his class, level and
    tile, and his hands as arm indices (`target`, `zen`: index + 1), which the realm puts in them
    as `sim::outfit` does (`sim::outfitArms`, split out of `outfit`). The ticket is the server's
    own, from 0x80000000, so it is never one a client numbered.
  - **When that tick has answered the Join**, which is said on the newcomer with his id, he gets
    the world's start, his own id (`Welcome::you`) and the log up to and including that tick as
    plain Tick frames (`Welcome::backlog` says how many).
  - **A connection that goes queues a `Leave`.** One that goes before its Join is answered is
    sent out by a Leave when the answer comes.
  - **A world nobody is in is let go,** and the next Hello raises it fresh.
  - Every tick goes to everyone in the world, every command carrying its player. A client's
    `Join` or `Leave` is dropped, and a client's every command is stamped with his own player.
- **The protocol is version 2** (`net/wire.*`): `Welcome::you` and `backlog`, the two new command
  kinds. `stateHash` folds every player in join order instead of "the hero", because a mirror
  looks at its own player and the server at the first. For one player it is the same number as
  before.
- **The client:**
  - `RemoteLink::join` reads the Welcome and then its backlog of ticks. `Link::catchUp()`, which
    Play calls right after the cradle's outfit, steps the whole past on the mirror before
    anything is drawn, then `lookAs(you)`. From there `realm_.hero()` is his own player.
  - **Play:** `heroAt()` replaces every `drawn_[0]` (the figures share the bodies' order, and on
    a shared world he need not be first). `drawnFor()` is open's figure for a body, factored out
    so that a body joining after open, another player and his summon's slot, gets one after the
    step that added it.
  - **Another player is dressed in his own class and gear** (`otherLook`: `redress` split into
    `dressOf`, under the name `Player<id>`; `Realm::satchelOf(id)` reads his bag).
  - **What the client shows:** another player's private happenings are skipped (the audience).
    `Left` takes his figure out on the tick, and a gone body is never drawn.
- **Two clients on one world** (loopback, a private `mu2_server --port 44599`). A knight played
  4200 frames with `--talk Lumen`, and an elf joined 10 s later for 1800 frames:
  - the server raised Lorencia for #1, then welcomed the elf as #302 with 219 ticks of the past;
  - the elf's mirror replayed them at once and played as #302, an elf; the knight's mirror drew
    #302 coming in, dressed;
  - 31 of 31 and 21 of 21 of the server's hashes agreed with each mirror;
  - when both had gone, the world was let go after 627 ticks.

  The single-client loopback was served by Lumen with 19 of 19 hashes agreeing. sim_test's output
  is identical to step 3's (6643 checks, the standing 10 failing). `layercheck` and `save_test`
  pass.
- **Known limits of this first version**, each for its own step later:
  - A newcomer replays the world's whole past, about 20 bytes a tick, so an hour is 72,000 ticks
    and a few seconds of stepping. A state snapshot replaces it when that starts to show
    (server-plan phase 4).
  - Every connection gets every tick of its world. Lockstep needs that, and there is no
    viewport.
  - Another player is dressed as he stands when he comes in. His gear changing later is not
    redrawn yet.
  - The world's start is its first player's raise. If he leaves, his body stays, gone, and the
    world goes on.
  - Saves stay local, and a character on the server is fresh each time (the user: existing saves
    do not matter; the server starts with a fresh character database).

## On the box — done 2026-10-07

- `server/deploy.sh` put `dd95ffb8` on the Hetzner box, and the service runs protocol 2.
  `sim_test`'s whole output there (376,118 lines) matches the Mac's, 0 lines differ.
- **The two clients on the real server,** from this Mac over the internet, the same run as the
  loopback's:
  - the knight raised Lorencia as #1, and the elf was welcomed as #302 with 220 ticks of its past;
  - each played as itself, and the knight's picture drew #302 coming in, dressed;
  - 31 of 31 and 20 of 20 hashes agreed;
  - the server said #302 left as his connection went, and the world was let go after 640 ticks.
- After it, the seeded log's `describe` names the players after the first `hero#<id>`; it named
  every player "hero", so the elf's log read the knight sitting down as his own. One player's log
  is unchanged, and so is sim_test's output.

## Next

- Phase 3, the world host: one process for every map, and the map change moving a player between
  worlds on the server instead of reconnecting. Begun: docs/sprints/20-the-world-host.md.
- Another player's gear redrawn when it changes; his name over his head; chat.
