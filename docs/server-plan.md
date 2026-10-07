# Server audit and plan

Written 2026-10-05. The user asked for an audit of our "server side" and a plan to make
MU2_BGFX a real server and client, as MU Online is. This page has three parts: what we have,
what MU does, and the plan in phases. It is a proposal. PLAN.md's decision table still says
**server: none; single player** (2026-09-20), and nothing here changes that until the
decisions at the bottom are taken.

The sources behind it:

- `docs/mu-netcode.md`: how WebZen's GameServer, MuMain and OpenMU split the work, with a
  table of message IDs.
- MU2's Godot server (`MU2/server`, `MU2/shared/Protocol.cs`, `Gate.cs`, `Mirror.cs`,
  `MU2/docs/environments.md`). That one played on a wire, so its mistakes are listed here and
  are not to be paid for twice.
- A read of `src/sim` and of every caller of it in `src/game` and `src/app`.

## 1. The audit: where the "server" stands today

The server side is `src/sim`. It is 19.6k lines. Its `Realm` is owned by `game::Play`
(`game/play.h:939`), one per map, and is stepped by the frame loop.

### What is already server-shaped (keep it)

| what | where | why it matters |
|---|---|---|
| `sim` never includes `gfx`/`game`, and the rule is enforced | `tools/layercheck.py`, `docs/architecture.md` | the server binary links `core + content + sim` and nothing else, today |
| a fixed 20 Hz tick, its own accumulator, no frame delta inside | `realm.h` header, `Realm::step` | the same rate MU2's server ran and the rate `mu-netcode.md` recommends |
| seeded xoshiro dice, 16 streams from one seed, counted | `sim/random.h`, `realm.cpp:80-95` | replays and lockstep checks are possible |
| determinism tested: two realms, one seed, equal hashes | `tests/sim_test.cpp:367-405`, headless 10k-tick `cmp` | the differential test the wire will need already exists in kind |
| headless realm with a scripted hand | `game/headless.cpp`, `--headless --seed --ticks` | the server's main loop, minus the socket |
| requests in, happenings out (`Request`, `Happening`, 51 `What` kinds) | `realm.h:61-231`, `:592` | the wire's two directions, already named |
| the wall clock is injected, not read | `Realm::setWallClock` | the server owns time by setting it |
| several realms can exist side by side | `sim_test.cpp:392-405` steps three | one process can host every map |
| monsters already pick among `players_` | `realm.cpp:279`, `realm_move.cpp:322, 487` | multi-player targeting is half there |
| showing waits for the landing cue (blood, number, fall, drop) | `play.cpp:600-620, 873-1340`, `fx/showing.h` | exactly what a client receiving late results needs; MU itself shows the number the moment the packet lands |

### What is client-shaped (the work)

Ranked by how much the split costs. Counts are from 2026-10-05.

1. **Most input skips the tick.** Only `ask` (walk, attack, talk, pick, perch) and
   `invoke`/`invokeAt` (skills) wait for the tick, at about 17 call sites. About 45 other
   Realm methods are called synchronously from `game/play_requests.cpp`, `play.h` and
   `play_open.cpp`, and they take effect at once and return a bool: `spend`, `moveItem`,
   `useItem`, `refine`, `crack`, `discard`, `buy`, `sellItem`, `buyBack`, `repair*`,
   `deposit`/`withdraw*`, `putIn`/`takeOut`/`shuffle`/`mix`, `acceptQuest`/`completeQuest`,
   `travel`, `equip`, and so on. Those rolls happen between ticks. Their happenings are read
   off `happenings().back()` straight after the call (`play_requests.cpp:97-115`), because
   the next `step()` clears them.
2. **The client reads the realm directly, everywhere.** `game/` and `app/` make about 670
   calls on it: `hero()` 147, `tables()` 63, `find()` 45, `satchel()` 40, `tick()` 38,
   `bodies()` 28. Twelve UI files hold a `const Realm&`, among them `ui/desk.cpp` (67 reads),
   `ui/quest_dialog.cpp` (56), `tracker`, `bag`, `mixer`, `shelf` and `minimap` (every body on
   the map). Figures are matched to bodies **by index** (`play.cpp:163`), not by id.
3. **One hero is built into the realm.** There are 92 uses of `bodies_[0]`/`hero()` inside
   `sim`, and about 40 single-instance fields:
   - the order and wishes;
   - the bag and the money;
   - every open window (`trading_`, `banking_`, `mixing_`, `questing_`, `gating_`, `angeling_`);
   - the vault, the machine and the quests;
   - the one summon;
   - potions and the Blood Castle run;
   - the projectile pools, which have no owner.

   Kill credit from guards, traps, folk and `gain`/`reviveHero` all assume the hero.
   **Happenings have no recipient.** `Gained`, `Picked`, `Served` and `Quest*` are said "on the
   hero" to whoever reads.
4. **The client owns the map change.** `Play::gated`, `takeTravel`, `takeHome` and the portal
   ask the app to destroy the mode and build a new one. The **save file is how the hero gets
   from one map to the next** (`play_mode.cpp:97-135, 259-268`). Go Back! hands the realm a
   tile the client kept (`play_requests.cpp:409-415`).
5. **The client owns persistence, and the realm trusts it.** `saves/hero.json` and
   `vault.json` are written by `PlayMode::keep` every 15 s and on exit. When the file is read
   back:
   - level is clamped;
   - experience, points, item pluses, options and excellents are taken as written;
   - only sockets are capped (`save.cpp:247-253`).
6. **The client drives time.** The frame accumulator steps the realm and forgives lost time
   at `kMostTicks = 5`. A click from a standstill runs an early tick (`play.cpp:131-159`).
   The desk sets the wall clock from `std::time` every frame (`ui/desk.cpp:187`).
7. **Rendering decides the population.** `Play::open` drops nests whose figure is not cooked
   (`play_open.cpp:~130-150`). Noria's roads are read from terrain texture names (`:154-182`).
   Every raise uses seed 1. A server has no cooked figures and must not need them.
8. **Debug paths write straight into the realm:** `--give`, `--zen`, `--castle*`,
   `--quest-ready`, `--weapon` (with `given=true`, which skips requirements), and
   `enableQuestDemo` (a global that mutates `kQuestTable`). The next save persists all of them.
9. **Small things:**
   - `sim/event.h` holds Blood Castle, not events.
   - The happening hash folds struct padding.
   - The sim uses floats with `sqrt`/`atan2`/`cos`. That is fine for one server binary, but
     the client must not be asked to reach the same numbers on its own.

**The verdict:** the realm is a good server *core*. What it is not yet is a server *process*.
It has one hero, its input is half on the tick and half beside it, and the program that owns
it also owns its time, its save, its map changes and its debug switches. None of that needs
the realm's rules rewritten. It needs the seam between `sim` and `game` finished.

## 2. What "like MU" means here

From `docs/mu-netcode.md`:

- **The server simulates and the client presents.** The server owns every number: damage,
  drops, items, Zen, shops, the Chaos Machine, trades, stats, quests and map moves.
- **The client walks first,** with no correction in the normal case. It sends a start tile and
  up to 15 steps, and the server follows along the path. This is the one prediction MU makes,
  and it is why MU feels responsive on bad links. A forced position (`C1 15`) snaps it back.
- **Results wait one round trip and simply appear.** The swing starts on the click, and the
  number shows when `ObjectHit` lands. Our showing already holds blows until a landing cue.
- **A viewport per player:** about 31×31 tiles, with enter and leave messages (`C2 12/13/20`,
  `C1 14`, `C2 21`). WebZen rebuilt viewports once a second, which is where MU's pop-in comes
  from.
- **TCP,** with C1–C4 framing, an XOR and SimpleModulus scramble, and one GameServer for all
  maps. Separate Connect, Join and Data servers stand in front and behind it.
- **What MU got wrong,** which we should not copy:
  - the client picked area-skill targets (up to 5);
  - range was checked loosely;
  - the client could set its own position;
  - timers were per-subsystem instead of one tick;
  - the viewport was rebuilt once a second.

The target, stated once: **one authoritative server process steps every map at 20 Hz. Clients
send intents and receive an event stream plus their own private state. The client predicts
only its own walk.** That is MU's split with the exploit holes closed. It is also what MU2's
Godot server reached, except for the walk prediction.

## 3. Lessons already paid for in MU2's Godot server

That server ran Lorencia and Noria behind one ENet socket, with 57 opcodes, login and roster,
and a client that played on it. What it taught us:

- **Use one ordered reliable stream.** A second, unreliable channel let `Hurt`/`Slain`
  overtake the `Strike` that caused them, and it ended up unused. That argues for TCP, as MU
  used, or a single ENet channel.
- **Poll, step and flush once per tick,** with one batched packet per client, and nothing
  sent mid-tick.
- **Send what the client cannot derive,** and no more. Breed stats, clips and swing timing
  come from the same tables on both sides. The walk needs its corner bits and sub-tile
  placement, or the client cuts corners the server did not.
- **Despawn must stop the object.** A hidden figure kept walking and chirping. A departed
  copy is kept and reused when it returns, because the drawing holds the object.
- **The client clock owes ticks and does not clamp them.** It owes up to 1 s and repays 4
  ticks a frame. Clamping, as we do now with `kMostTicks`, loses ticks against a server that
  never stops.
- **Rules that live in the drawing get lost in the split.** MU2's revive lived in the client,
  and nobody revived on the server. The same risk sits in item 7 above.
- **Never block the network pump while loading,** and set timeouts to at least 60 s. The
  5 s default dropped clients during model loads.
- **A malformed message must not kill the server.** It took every map down once.
- **Bots live on the server only, behind the same admission check as clients.** Admitting
  them past it produced the "character clones" bug.
- **Run the realm-versus-mirror lockstep check routinely from day one.** It existed, nobody
  ran it, and it would have caught an id that was always 0.

## 4. The plan

Each phase leaves the game playable in single player. The first three use no socket at all.
They finish the seam inside one process, which is where most of the risk is. The socket in
phase 4 then carries a seam that is already proven.

### Phase 1: the seam, in process

The goal: `game/` never sees a `Realm`.

- **Commands.** Every one of the ~45 immediate methods becomes a command in the request
  queue, applied at the start of the next tick, in arrival order, with the player's id on it.
  Its answer is a happening (`Bought`, `Refused{why}`, …) and not a return value. The windows
  already raise requests and redraw (the "interface is a mirror" rule). They lose the bool and
  wait one tick, 50 ms, which is what MU's own shop windows do. Dice rolled outside the tick
  go away with the bools.
- **A `Link` interface.**
  - `send(Command)`;
  - `happenings()` since the last poll;
  - `self()`: the hero's private state, meaning stats, bag, money, cooldowns, quests, open
    window, vault and machine.
  - `LocalLink` wraps a `Realm` in process.
  - ~~Play and every UI file read from a **client-side `View`** that the link fills, and never
    from the realm.~~ **Changed 2026-10-07, in sprint 17: the View is a mirror realm.** The
    client reads 86 Realm methods, and many are rule queries over its state (`questOffered`,
    `judged`, `repairCost`, `travelRefusal`, `knows`, the router, the hazards). A separate View
    would have to rewrite those rules in the client -- §3's "rules that live in the drawing get
    lost in the split". So the client reads a `const sim::Realm` the link keeps: LocalLink's is
    the realm itself, RemoteLink's a mirror the server's stream fills, which runs the same const
    queries. That is MU2's own `Mirror`. The 667 reads stay; what phase 4 decides is which of
    the realm's state the stream carries.
- **Figures by id,** not by index.
- ~~**`layercheck` gets the new arrow:** `game/ui` and `game/fx` may not include `sim/realm.h`.~~
  **The gate instead: no writable `sim::Realm` in `game/` or `app/`** except at the link
  (`game/link.h`), Play's `local_` and the scripted hand (`game/headless.cpp`); layercheck
  refuses any other. That is the gate that says phase 1 is done.
- **Debug switches become commands** (`Give`, `Zen`, `SetCastle`, …) behind a `gm` flag, so
  they keep working and later run on a server only for a GM account.

This is the largest phase, and nearly all of it is in `game/`. Roughly 670 call sites move to
the `View`, most of them mechanical.

### Phase 2: many heroes in one realm

**Done 2026-10-07** (`docs/sprints/19-many-heroes.md`). Phase 4's wire came first (sprint 18),
so the step that made the world shared also put several connections on one world: the server
lets a later player in by a Join command and sends him the world's past to replay.

- A `Player` struct takes everything that is one-per-hero today:
  - the order, wishes, bag and money;
  - the open window, vault, machine, quests, summon and potions;
  - the castle run, Go Back and the projectile owners.
  
  `Realm` holds `players_` as a vector of those. `bodies_[0]` goes, all 92 of them, replaced
  by the player's body id.
- **Happenings carry an audience:** everyone who can see it, or one player. Private state
  goes only to its owner.
- Kill credit, experience, drops' owner, guards, traps and folk work from "which player",
  never "the hero".
- **Determinism:** players step in id order, and a test with two scripted heroes is added to
  `sim_test`.
- **Join and leave are commands** (`Join`, `Leave`), the server's alone, so lockstep carries the
  world's door. A body that leaves is marked gone and never erased.
- **Out of scope here:** party experience and loot rules, PvP and trade. They are their own
  decisions (§5).

### Phase 3: the world host, still in process

- A `World` steps every map's `Realm` on one 20 Hz deadline loop: poll, step every realm,
  flush. MU2's `Serve` is the worked example.
- **The server moves a player between realms.** Map change, gate, Tab travel and portal stop
  going through the save file and the mode's rebuild. The client is *told* `MapChanged{map,
  x, y}`, and only then rebuilds its picture.
- **The server owns time:** the wall clock, the tick and the seed per raise. The client's
  accumulator becomes a mirror clock that owes and repays ticks.
- **The population is no longer chosen by what is cooked.** Nests come from the tables, and a
  missing figure is the client's problem, drawn as a placeholder.
- **The server owns persistence.** Characters and the vault live in a server-side SQLite
  database (`characters.db`, separate from the content `mu.db`). A character is saved on map
  change and logout, every minute (OpenMU's rate), and after every trade-like event. The
  client keeps no save. The `hero.json` reader becomes a one-time import, checked against
  level and the rules, not trusted.

By the end of this phase single player runs as a server and a client inside one process.
That is MU2's "Local".

### Phase 4: the wire

- A `mu2_server` target links `core + content + sim` and a net file. `layercheck` already
  allows it. It reads the cooked tables and the `.mur` grids, and no textures.
- **Transport: TCP,** as MU used, one stream per client. Messages are framed with a length
  and an opcode, and fields are explicit little-endian. The codes follow MU's (`C1 10` walk,
  `C1 15` hit, `C2 13` monsters in, …) wherever the meaning matches, so `mu-netcode.md` and
  OpenMU's packet tables stay our reference. We use none of MU's encryption; TLS can come
  later.
- **The parser is crash-proof.** A bad message drops that client, not the server. A fuzz test
  goes in `tests/`.
- **Viewport:**
  - a 31×31 tile interest square per player;
  - enter and leave are computed every tick from the player's view set, not once a second;
  - despawn stops the copy, and the client keeps it for reuse.
- **`RemoteLink`** implements the phase 1 `Link` over the socket. `--server host:port`
  chooses it, and with no flag the game is `LocalLink` as before.
- **The lockstep test:** one realm, and one mirror fed by its serialized stream, compared
  every tick in `sim_test`. It runs in `ctest`, not "when someone remembers".
- **The network pump runs on its own thread,** so loading a map never starves it.

### Phase 5: feel

- **Own-walk prediction, MU's way.** The client walks the path at once and sends start plus
  steps. The server checks every step against the grid and a speed budget, which is where MU
  was loose, and sends a correction only on a disagreement.
- **Clicks and skills:** the swing starts on the click, as now. The number, the blood and the
  fall wait for the server's `Hit`. The showing already does that.
- **Area skills target on the server.** The client sends where and which way, never who.
- **Measure:** add an artificial latency switch (`--lag 80`) on the link. The landing cue
  should hide 50–100 ms, and if it does not, this phase is where that shows.

### Phase 6: accounts and the lobby

- Login with an account and a password hash on the server, then the character list,
  create, delete and select over the wire. MU's `F1 01`, `F3 00..03`.
  - The Sanctuary lobby already exists. It changes from reading the save folder to asking
    the server.
  - No bench door like MU2's `Hello` without an account.
  - A build stamp is checked on connect, so a client never runs against different tables.
- GM commands are allowed per account.
- Server-side bots (`headless`'s hand) join through the same login.

### Phase 7: playing together

Each of these is a decision more than a task, and each has MU's rules in WebZen's source:

- other players drawn by appearance (`C2 12`), and the first time you see one;
- chat: public, whisper and notices;
- party: shared experience, kill credit and the party bar;
- trade (`C1 36..3D`);
- PvP and the PK system;
- guilds;
- the events (Blood Castle, Devil Square) on a shared schedule instead of each player's own
  clock.

## 5. Decisions for the user

1. **Do we reverse PLAN.md's "server: none"?** If not, phases 1–3 are still worth doing.
   They make the save and the map change server-owned in all but name, and that is the
   cheap part of the insurance.
2. ~~**Is single player still a mode?** The proposal is yes: `LocalLink` with no socket, like
   MU2's Local.~~ **No** (the user, 2026-10-07: *"this will not be a single player game anymore
   but standart mmorg experience"*). `LocalLink` stays as the tests' and the headless runs'
   harness, not a mode to keep.
3. **Transport:** TCP, as MU used (proposed), or ENet as MU2 used.
4. **Do we keep our own wire, or speak MU's real 0.97/S6 protocol** so that a MuMain client
   or OpenMU tools could connect? Ours is far less work. Theirs is a compatibility project of
   its own, and our rules have drifted on purpose (runes, D3 skills, run speed).
5. **How much of phase 7, and in what order?** Party first is the usual answer.
6. **Where the server runs:** this Mac only for now, or a hosted box. That decides how soon
   phase 6's TLS and accounts matter.

## 6. What to do first, whatever is decided

These pay for themselves in single player:

- Turn the immediate methods into tick commands, a window at a time. Start with the shop,
  the vault and the machine: their bool-and-`happenings().back()` dance is the source of the
  between-tick dice.
- Move the realm behind a `View` for the UI files and add the `layercheck` arrow.
- Stop dropping nests for uncooked figures. Give each map raise its own seed.
- Make the debug switches commands.
