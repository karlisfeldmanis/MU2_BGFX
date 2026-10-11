# The clear thread

Begun 2026-10-11. The user: "audit our server and find some improvment for better performance and
less lag and hikups possible, propouse some plan", then took steps 0-2 of the plan.

## What the audit measured

On the Mac (M-series, -O2, a private copy of HEAD's sim), and on the box itself:

- **The box is idle.** 12 h up, 9.7 CPU-minutes, 42 MB; no steal, fast fsync, 1.3 ms out.
- **The server's one-offs are small.** Raising a world under 1 ms; a step with 100 idle players
  0.13 ms (Lorencia) to 0.51 ms (Noria); a snapshot 0.1-0.6 ms; the hash nothing. The largest is
  packing a snapshot for a newcomer: 3-5 ms at 100 players, on the thread every world ticks on.
- **The client paid most.** `RemoteLink::predict` laid the whole map from the mirror and stepped
  all of it `lead` ticks, every tick, on the frame thread, to say where one hero stands:

  | 100 idle players | lead 3 | lead 6 | lead 11 |
  |---|---|---|---|
  | Lorencia | 0.52 ms | 0.91 ms | 1.58 ms |
  | Noria | 1.74 ms | 3.24 ms | 5.83 ms |

- **The live world grew** (920 to 989 KB in 25 minutes, one player): the box ran a build from
  before bb9be96a, which reuses a gone player's place. The next deploy ends it.
- **Nothing timed the server's own loop**, so a "lag" could not be laid at its door or not.

## What was done

1. **The server's timing** (server/src/main.cpp, `g_stats`). Each minute:
   `timing: 15 players (15 bots), 2 worlds | 2385 ticks, 0.701 ms mean, 15.04 most (lorencia) |
   pass most 15.1 ms, 1 slow | late most 11.1 ms, 0 a tick behind | out 59.2 KB/s stream,
   111.4 KB/s udp | 0 saved, 0.02 ms most | snapshot 0.40 ms most, 4 packed`. And any pass over
   10 ms (`kSlowPass`): `slow pass 15.1 ms: heard 0.0, ticks 15.0 (1, the longest lorencia 15.0),
   party 0.0, saves 0.0 (0), sent 0.0, gone 0.0`. The poll's wait is not counted.
2. **The hero predicted alone** (`Realm::stepAlone`, src/sim/realm.cpp). His commands and his two
   halves of a tick, the rest of the map held where the mirror has it. `Realm::step` is untouched:
   nothing a mirror or the server steps goes through it. Play reads only his place and facing.
   Noria at 100 and lead 6: 3.24 ms to 0.24 ms, which is now the map's copy and not the lead.
3. **No prediction while he stands** with nothing unanswered: no order, a walk arrived, no push or
   blink coming. The hero is drawn where the mirror has him, which is where the guess would put him.
4. **The client's catch-up, checked, unchanged.** Play::update hurries the clock to at most twice,
   one tick a frame at 60 fps; only a second behind (`kSnapOwed`, 20 ticks) are ticks stepped five
   a frame (`kMostTicks`).
5. **A snapshot is packed on a worker** (`World::packing`, `takePacked`), at a lowered priority on
   Linux. Until its pack is in, the last snapshot, its pack and the past since it stand, so a
   newcomer is welcomed at once with those.
6. **The snapshot is taken after the tick is sent**, not between the step and the send.
7. **SQLite's checkpoint on a thread of its own** (`Store::checkpoints`): `wal_autocheckpoint=0` on
   the server's connection, a PASSIVE checkpoint every 10 s on a second one. The saves stay on the
   loop -- about 20 us a character, and a store read straight after a write (a map change) must
   see it; a write-behind queue would have been a race for that.
8. **Owed ticks a world at a time in turn**: after a stall, a tick of each world, then the next
   round, where one world's twenty went back to back while every other waited.

## Measured after

A local server, 15 bots made at level 40 (`--kit 40`), and a client on a 150 ms line with 30 ms of
jitter clicking every 25 frames: 152 orders, every hash agreed, no long frame logged. The server's
minutes: 0.7-1.3 ms a tick on average, no tick a whole interval late.

## Found by the timing, not yet fixed: a long walk's route

The slow-pass lines (10-25 ms ticks, a few a minute with 15 bots) were all one thing, found with
throwaway timers in a private worktree: **a player's route across a town**. Router::plan expanded
7,000-11,000 tiles for a walk like Lorencia 115,138 to 6,145 and took 5-22 ms on the Mac -- every
one of them found, none failed. The estimate is octile scaled by three quarters (Route.cs's), so
A* explores far more than an exact estimate would, at 2-3 us a tile. On the box that is 20-80 ms,
more than a tick, and every client in the map runs the same plan in its mirror.

Ways, for the user to choose: make each expansion cheaper with the same routes (no rule changes),
an exact octile estimate (other routes of the same cost now and then), or a cap on a plan's
expansions with a long walk taken in legs (MU's own clients walk a click in pieces).
