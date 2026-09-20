# Sprint 5: rules I

**Proved by:** a seeded 10 000-tick hunt that logs the same bytes twice; click to walk and
click to attack in the window.

**Planned** 2026-09-20, before any code, from `mu.db`'s own rows, our own copy of Lorencia's
attribute grid and MU2's `shared/` read one file at a time — not from a guess and not from a
survey. Sprint 3 is mid-flight (the cook's mesh and texture halves are written, chunking and
instancing are not) and sprint 4 has not started. Nothing here touches either one's files.

## Why this can be planned and built out of order

**The sim has never heard of the renderer** (foundation 8), so the first and larger half of
this sprint needs nothing sprints 3 and 4 produce: a tick, a grid, a route, a monster and a
fight are `src/sim` and `src/content`, and `--headless --seed S --ticks N` runs them with no
window, no glb and no bone texture. That half is also the half that carries the proving
sentence's first clause.

The second clause — click to walk, click to attack *in the window* — needs a pick against the
ground and something to draw where a figure is. It does **not** need sprint 4: a walker drawn
as the sprint-1 bench model, sliding without a step cycle, proves the click, the route and the
landing cue exactly as well, and is thrown away the day the rig lands. What must not happen is
this sprint growing a figure path of its own that sprint 4 then has to undo.

**The order inside the sprint follows from that**: headless, tested and byte-stable first; the
window last, and only as a viewer onto a sim that is already right.

## What the data turned out to be

- **`mu.db` carries the monsters whole, and carries neither of the two formulas.** Sixteen
  rows in `monster_kinds`, each with level, health, damage band, defense, attack and defense
  rates, move/attack/view range, move and attack delay, and respawn seconds. There is **no
  experience table and no drop table** — `PLAN.md`'s point 11 lists both as things the cook
  writes, and that is wrong in the way that matters: MU has an *expression* per level, not a
  list. Both formulas are code, and they are transcribed in "The numbers this sprint owes"
  below. What the cook writes is `monster_kinds`, `monster_spawns` and `npc_spawns`; `items`
  waits for sprint 7.
- **Lorencia holds 250 monsters, and the map after it holds a thousand.** Nine spawn groups on
  map 0: 45 Bull Fighters, 45 Hounds, 60 Budge Dragons over two rectangles, 45 Spiders, 45
  Elite Bull Fighters, 20 Liches, 15 Giants, 15 Skeleton Warriors. Noria (map 3) has eight
  groups and **1000**. So the tick's cost is not a rounding error and is designed for now:
  0.5 ms of sim over 250 monsters is **2.0 µs a monster a tick**, and over Noria's thousand it
  is 500 ns. Nothing per-monster in this sprint may allocate, take a lock, or look anything up
  in a map.
- **Every delay in the rows is in milliseconds and none of them is a multiple of the tick.**
  `move_delay` is 400 on all sixteen; `attack_delay` runs 1400, 1600, 1800, 2000, 2200. At
  20 Hz those are 8 ticks and 28/32/36/40/44 ticks exactly — they *do* divide, which is luck
  worth checking rather than assuming, and the conversion happens **once at cook time** with
  the remainder logged if a future row is not so tidy. A delay re-derived per tick in floating
  point is how a seeded run stops reproducing.
- **Our attribute grid is read as one byte and MU's word is two — measured, and today it
  costs nothing.** `content/ground.cpp:25` takes the red channel only; MU2's `Terrain.cs` says
  low byte in red, high byte in green. Checked on our own copies: on Lorencia and Noria the
  green channel is **zero on all 65 536 tiles of both maps**, so no bit is being lost today.
  It is still read as a word this sprint, because the cost is one line now and a silent
  wrong-map bug later.
- **Passability is a threshold, not a bit test, and this one is a real difference.**
  `ground.cpp:356` asks `(a & 0x04) == 0 && (a & 0x08) == 0`. MU asks
  `(flags & ~(Action|Height|CameraUp)) < wall` — a numeric comparison, so any attribute at or
  above the wall level blocks, including flags with nothing to do with movement. Measured on
  our grids the two agree on every tile of both maps (the words present are only 0, 1, 2, 4
  and 5), so this is a **latent** difference and not a live bug — but the wall level is an
  argument in MU (`Character` 0x02 for a person, `NoMove` 0x04 for the relaxed pass) and the
  bit test has nowhere to put it. The threshold goes in, with the measurement above quoted in
  the comment so the next reader knows it was checked and not merely copied.
- **Lorencia is 71% standable and has a 2 560-tile safe zone.** Of 65 536 tiles: 45 219 plain,
  1 492 safe zone, 1 068 safe zone with a flag, 17 756 `NoMove`, and exactly one tile carrying
  `Character` baked into the file. The safe zone is not decoration — it is where nothing
  attacks and where health recovers, and it is 4% of the map in one piece around the spawn.
- **MU's own walk speed is a client constant, not a row.** `move_range` is how far a monster
  wanders, not how fast; the speed MU2 traced is `MoveSpeed = 10` at 25 fps, which is
  `Realm.cs`'s note beside `Leash = 10`. Speed is one of the few numbers here that has to come
  out of MuMain rather than out of `mu.db`, and it gets its own line in the sheet.

## The gate, stated before the work

**Two, and both are refusals rather than targets.**

1. **The same seed writes the same bytes, twice, on two different days' builds.** Not "the
   same summary" — the same file, compared with `cmp`. A sim that reproduces only until
   something is reordered is not reproducible; it is merely undisturbed. If this cannot be
   made to hold at 10 000 ticks with 250 monsters, the sprint stops and says which source of
   drift beat it, rather than lowering the tick count until it passes.
2. **250 monsters step in under 0.5 ms of CPU, measured in the headless run, before the
   window sees any of it.** That is the account foundation 8 opened and `docs/budget.md`
   records. If the tick does not fit, the AI is made cheaper here — fewer monsters awake, a
   coarser re-path — and it is *not* paid for out of the frame's spare, and not discovered in
   sprint 6 when effects are already standing on it.

## What this sprint builds, in this order

1. **The tick, and the two lists.** Fixed 20 Hz on its own accumulator (`conventions.md`'s
   Time), one seeded random source, no clock but its own, **no allocation in a step**. The sim
   speaks in requests in (walk here, attack that) and events out (spawned, stepped, hit, died,
   dropped, gained, levelled). Everything below produces events; nothing below draws.
2. **The grid, read as MU reads it.** The attribute word out of both channels, the threshold
   with its wall level, the three non-blocking flags cleared, and the off-map answer that is
   *not* "open ground" — `ground.cpp`'s own comment already names that trap and this must keep
   its bounds test ahead of its bit read.
3. **The route.** A* on the tiles, costs **5 straight and 7 diagonal** (MU truncates 5 × 1.414
   to 7, and that truncation is the behaviour), the client's neighbour order so ties break
   where MU's break, and the goal resolved to the nearest standable tile *before* the search,
   so the plan, the marker and the walk all agree. State is generation-stamped, not cleared:
   MU2 measured a quarter of a millisecond a tick spent wiping 64 KB up to four times in one
   plan. The open set and the scratch lists live on the router and are never reallocated.
4. **The cooked tables.** `cook.py` grows a second half: `monster_kinds`, `monster_spawns` and
   `npc_spawns` out of `mu.db` into one flat versioned file, delays converted to ticks once,
   the version checked at load. No sqlite in the game. (`mu.db`'s writes sit in the WAL —
   the cook reads it checkpointed, or it reads yesterday's numbers.)
5. **Spawns and nests.** A group is a rectangle, a count and a kind; each monster keeps its
   nest so it can be led away from it and come back. Placement rejects a tile the threshold
   refuses, and a group whose rectangle is mostly wall is logged rather than silently short.
6. **The mind, as a switch and not an object.** Idle wander, notice, chase, attack, lose
   interest, return. Sight 12 tiles, leash 10, grudge twice the leash, all traced. One
   `Temper` field and a switch — MU2 wrote down that the per-monster intelligence object cost
   an allocation and a virtual call per monster per tick for a state machine with one
   implementation, and at 250 monsters that is the whole budget.
7. **The fight, in MU's order.** Hit chance, the roll, and the five steps after it in the
   sequence transcribed below. The workings come out with the blow, not just the number: a log
   line that says `14` cannot be checked against OpenMU, and one that says
   `roll 12 in [9,17] - def 3 = 9, floored to 14` can.
8. **Death, respawn, experience and levels.** Respawn at `respawn_seconds × 20` ticks.
   Experience by the two formulas. Five points a level, cap 400, experience past the cap
   discarded rather than banked.
9. **`tests/`, which is the point of all of it.** The seeded log, and invariants that run over
   it: nothing stands on a tile the threshold refuses, no health below zero, no blow landed on
   something already dead, no monster outside its leash of its nest, every level-up preceded by
   the experience that pays for it. This is what replaces Instrument, Audit and the bot.
10. **The window, last.** Pick a tile under the pointer, raise a walk request, draw the walker
    as sprint 1's bench model, and click a monster to raise an attack request. Presentation
    smooths between ticks; aim, reach and hits use the sim's own values and never the smoothed
    ones.

## The numbers this sprint owes, traced

Written here so the code is transcription and not invention, and so a review can read the two
side by side.

**Hit chance** — OpenMU `GetHitChanceTo`. `defenseRate < attackRate && attackRate > 0` gives
`1 - defenseRate/attackRate`, and otherwise **0.03**. Only ever a miss or a hit; there is no
glancing blow.

**A blow, in order — and the order is the behaviour, not the presentation:**

1. critical if `criticalChance > 0` and the roll says so; a critical is the maximum exactly
   (every bonus term that widens it is a later season's).
2. otherwise `rand(min, max)`, **upper bound exclusive**, as OpenMU's `Rand.NextInt` is.
3. minus `max(0, defense)`.
4. `× 0.3` if the defender's defense rate beats the attacker's attack rate.
5. floored at `max(1, attackerLevel / 10)` — **after** the subtraction, which is what stops a
   high-level monster being harmless to a tank, and would be invisible if it came first.
6. `× damageTaken`, **after** the floor and **only if the damage is already above 1**.

**A level's cost** — `CalculateNeededExperience`, cumulative from level 1:
`10 × (level + 8) × (level − 1)²`, plus `1000 × (level − 247) × (level − 256)²` past 255. The
second branch is kept even though nothing will reach it this year, because omitting it makes
the curve quietly wrong exactly where nobody is still checking.

**A kill's worth** — `CalculateBaseExperience`:
`award = (killedLevel + 25) × killedLevel / 3`; `× (killedLevel + 10) / killerLevel` when the
killer is more than ten levels above; `+ (killedLevel − 64) × (killedLevel / 4)` at 65 and
over; and **× 1.25 at the end**, which is in the formula and is not a server rate.

**Cap 400. Five points a level, all three classes.** Everything else about classes — the
per-stat rates, shield, mana — is sprint 7's, and is not half-built here.

## What is deliberately deferred

- **Items, drops, the bag and money.** Sprint 7, whole. A kill this sprint gives experience and
  nothing else, and the event says `dropped` to nobody. `items` stays uncooked.
- **Skills of any kind.** Not one, and not even a placeholder. `PLAN.md` decided the skill
  system is Diablo 3's shape — learned permanently, four keys, real cooldowns — and the only
  thing this sprint must do about it is **not bake in MU's assumptions**: no swing that reads a
  skill off an item, no attack path that cannot have a cooldown added to it, no per-figure
  attack state that has room for exactly one timer.
- **The showing.** Blood, damage numbers, the fall, the health bar and the landing cue are
  sprint 6's. The sim emits the events they hang off, and this sprint draws none of them.
- **Real figures and animation** — sprint 4. The walker is a stand-in and is named as one in
  the code.
- **NPCs, the shop, maps and gates.** `npc_spawns` is cooked because it is in the same table
  sweep; nothing stands on it. `gates` holds two rows and is sprint 9's.
- **Mana, ability and shield.** No consumer this sprint; a stat with nothing reading it is a
  field to get wrong twice.

## The two things most likely to go wrong

Written before the code, as sprints 2 and 3 did:

- **The byte-identical run fails for a reason that is not the rules.** The drift will not be in
  the damage formula — it will be an iteration order over a hash container, a float
  accumulated from the frame's delta rather than from the tick, a `respawn_seconds × 1000 / 50`
  done in floating point, or the window's own random source being drawn from the sim's. Each of
  those reads as "the sim is nondeterministic" and none is. The defence is that the headless
  run comes first and the window is added to a sim already proved stable, so the day the
  comparison breaks there is exactly one new thing in it.
- **The tick fits at 30 monsters and does not fit at 250.** Everything about a mind is cheap
  per monster and the cost is entirely in how many are awake and how often they re-path. The
  temptation is to measure with a handful standing around the camera, which is also what the
  window will show; the gate is stated on the full Lorencia population in the headless run for
  that reason. If it does not fit, the answer is fewer awake and a coarser re-path, and it is
  said out loud rather than borrowed from the frame.

## Measured

Built 2026-09-20 in one session, headless half first as planned. Apple clang 21.0.0, Release,
on this Mac. Every number below is from `build/mu2 --headless`, which touches no window, no
device and no .glb.

**The gate's first half, kept.** The hunt is a level-4 Dark Knight put down in the spider and
Budge Dragon field, ten thousand ticks, seed 1:

    ./build/mu2 --headless --seed 1 --ticks 10000 --level 4 --at 200,160 --sim-log /tmp/a.log
    ./build/mu2 --headless --seed 1 --ticks 10000 --level 4 --at 200,160 --sim-log /tmp/b.log
    cmp /tmp/a.log /tmp/b.log        # identical, 117 307 bytes, fingerprint ee321b11beaaf37f

and with `--sim-steps`, which puts every tile crossing in as well, identical again at 201 364
bytes. 372 blows landed, 148 missed, 42 deaths, one level won; 2 880 draws from the realm's
own dice. The scripted hand draws from its own generator and never from the realm's.

**The gate's second half, kept with room to spare.** The account was 0.5 ms a step over
Lorencia's 290 monsters. Measured:

| map | monsters | median step | 99th | worst | in all |
|---|---|---|---|---|---|
| Lorencia | 290 | **0.0010 ms** | 0.0026 | 0.027 | 10.3 ms over 10 000 ticks |
| Noria | 1005 | **0.0033 ms** | 0.0075 | 0.049 | 34.7 ms over 10 000 ticks |

That is 0.2% of the account on Lorencia and 0.7% on Noria, and it is only that because of one
thing found by measuring Noria rather than reasoning about Lorencia: **rousing was O(n²)**.
Every beast scanned every body to find the players, which at 1005 bodies is a million
comparisons a tick and measured 0.33 ms — the whole account, on a map with nothing happening
on it. A list of who is a player and a lookup from id to index cured it: Noria went 0.33 → 0.0033
and Lorencia 0.033 → 0.0010. The seeded log was **byte-identical before and after**, which is
what says the change was a cost and not a behaviour.

**The router.** 677 searches over the hunt, none failing, 6 828 tiles expanded in all, worst
single plan 344 tiles. 49 walks were *refused* — no route to the tile asked for — and that is
in the log now, because it was not: a refusal was a silence for one evening, the scripted hand
asks again whenever it is not walking, and the run spent nine thousand ticks asking for the
same impossible tile with nothing written to the log at all, because nothing was happening.
`Refused` is an event like any other.

**What the invariants caught.** Nothing, on either map, over 10 000 ticks: nothing stood on a
tile the threshold refuses, no health went below zero, no blow landed on the dead, nothing
strayed past its grudge, no level was won unpaid. `build/sim_test` is 57 checks over the rules,
the dice, the router, determinism and those invariants, and it passes.

**Three things this sprint decided that the plan above did not.**

1. **Three classes, not one.** The plan deferred the Dark Wizard and the Fairy Elf to sprint 7
   on the grounds that the census had only MU2's word for their rates. They are traced now —
   `ClassDarkWizard.cs` and `ClassFairyElf.cs`, the files the census named — and the trace
   corrected something MU2's table did not say: **the Fairy Elf's physical damage is not zero.**
   It is conditional on her attack mode, and her melee rate runs over strength *and agility*
   together (1/7 and 1/4 of the sum). Archery is a real second branch and waits for a bow.
2. **A dead character stands up in town.** The plan said nothing about it and the hunt made it
   necessary: a level-1 knight with no weapon dies to a Budge Dragon, and a hunt whose first
   1348 ticks are a fight and whose remaining 8652 are a corpse proves very little. It is MU's
   own rule rather than an addition — three seconds, the map's own spawn box, health restored
   (`Realm.cs:2013`, `Player.cs:1687`) — and the safe gate is cooked into the tables for it.
   The gate is on `index.json`'s world entry and **not** on the world json beside the grids,
   which is where this looked first; a missing gate is not an error there, so he quietly stood
   up where he fell.
3. **Spending points is a sim action.** `--level 20` makes a character with 95 points in hand,
   and unspent he is a level-1 character with more health: his fists do 4 to 7, a Budge Dragon
   has three of defence, and he loses to it. `Realm::spend` is what sprint 7's stat window will
   raise; the headless hand puts them all into strength and says so on the line that does it.

**And two the cook decided.** `respawn_seconds` is corrected for the two breeds `mu.db`
flattened (Bull Fighter and Budge Dragon, 10 → 3, `Lorencia.cs:89` and `:152`), printed on
every run. And the attribute grid is cooked into the `.mur` as MU's whole 16-bit word, because
the sim is the first thing that reads it for a decision and has no PNG decoder; the ground
reads the same word out of the picture now instead of the red channel alone.

**The one definition of blocked.** `content/grid.h` holds it — `(word & ~NonBlocking) < wall`,
MU's threshold with MU's wall argument — and `Ground::walkable` is now a call to it. The two
definitions the census found agreed on every tile of both maps but one, which is exactly the
kind of agreement that ships the wrong one.

**What is not done.** The window half — click to walk, click to attack, the walker drawn — is
not in this commit. The sim it would be a view onto is finished and proved; what stopped it is
that `game/crowd.cpp`, `crowd.h`, `figures.cpp` and `figures.h` were being edited by another
session while this one ran, and a walker is drawn through exactly those four files.
