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
