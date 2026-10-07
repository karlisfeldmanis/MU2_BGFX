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
4. The server: connections to one world share its realm; the client draws other heroes; a client
   joining mid-session replays the world's input log from the Welcome; `net::kVersion` bumped.

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
