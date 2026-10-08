# The quiet line

Begun 2026-10-08. The user asked how many players the server holds, then: "we need some smarter
server tricks so players dont experience lags". Protocol 13.

## What the box holds — measured 2026-10-08

A scratch bench put N players in one map (two thirds attacking the nearest monster, one third
walking, a click a second each, plain attacks) and timed the server's tick: `realm.step`, the
Tick's frame and the hash every 20th.

| players in the map | Lorencia mean / p99 | Noria mean / p99 | bytes per tick |
|---|---|---|---|
| 1 | 0.008 / 0.02 ms | 0.025 / 0.05 ms | 30 |
| 100 | 1.0 / 3.4 ms | 1.4 / 4.1 ms | 218 |
| 400 | 4.3 / 8.9 ms | 5.7 / 10.7 ms | 788 |

About 10-14 us a player a tick on the box, straight-line to 400; monsters nobody fights cost
almost nothing. With skills, area attacks and drops allowed at 2-3x: **about 200 players
comfortable, 300-400 before ticks slip.** Memory and bandwidth are not the limit. A map's own
limit is the client's: every client steps its whole map (lockstep) and replays its past to come in.

## The tricks, and which are done

1. **The hero ahead — done.** `Link::ahead` (game/remote_link.cpp): each tick the client lays a
   scratch realm from the mirror's snapshot, puts in his orders the server has not yet answered,
   each at the step the server most likely takes it, and steps it `lead` ticks (the round trip in
   ticks, held still within 3/4 of a tick, at most 8). `Play::remember` draws the hero there;
   everyone else is drawn where the mirror has them. It is laid again from the mirror every
   tick, so a wrong guess lasts one tick and is drawn as a slide of a step at most. Not on an
   event map (its grid changes under the run), nor while more than two ticks are owed.
2. **The cushion — done.** `Play::update` counts the stalls a player sees: a frame with no tick to
   step, longer than 8 ms. Three in five seconds and one more tick is held in hand (at most
   three); twenty seconds with none and one fewer. Short of the cushion the clock runs at 0.9,
   past it hurries as before. The link leads the hero by the cushion too (`Link::setCushion`), so
   his clicks pay nothing for it. A steady line holds none. Constants in play_tuning.h.
3. **Faster joins — server half done.** The Welcome's snapshot is packed (`net::pack`, zlib's
   fastest level): Lorencia's 548 KB went as 25 KB, Noria's 1.7 MB as 69 KB. A world is snapshot
   every 20 s rather than every minute, so a newcomer replays at most 400 ticks. The pack is made
   at the first welcome after a snapshot and kept until the next.
4. **The server's loop — done** (server/src/main.cpp):
   - It sleeps in `poll` (`ppoll` on Linux) until a line speaks or the next world's tick is due,
     and reads only the lines that spoke. It was a 2 ms nap and a `recv` on every line, 500
     times a second: ~0.5 us a line a pass, 13% of the box at 500 players, and up to 2 ms on an
     order before it was read.
   - Each player is written on his own minute (`Session::keepAt`), not everyone at once: about
     20 us a character, so 500 at once had been a 10 ms tick.
   - A line that takes nothing for 20 s, or owes more than 64 MB, is let go
     (`Socket::stalledSeconds`, `waiting`). A client that stopped reading held the server's bytes
     for him forever before.
5. **The tick datagrams — done.** Beside the stream, not instead of it: the server sends each
   tick down the stream and in a UDP datagram (`net::putTicks`) carrying the last four ticks, as
   many as fit in 1200 bytes. The client takes each tick from whichever brings it first
   (`RemoteLink::offer`); a datagram's tick past a gap waits for it. The client says its
   Welcome's `udpKey` from its UDP socket once a second (`net::putBind`), which tells the server
   where to send and keeps a home router's way open. Commands, Welcome, Who and the hashes stay on
   the stream; a line where UDP is shut plays as before. The box opens 44406/udp (deploy.sh).
6. **Copies of a crowded map — done.** A map past `--map-cap` players (100 by default) raises
   another copy; a newcomer goes to the fullest copy with room, so players are together while
   they fit. The Welcome says which (`copy`), and the minimap says "Lorencia 2". Blood Castle is
   never copied.

## The test line

`build/mu2 --play --server HOST --lag 150 --jitter 20` holds every byte each way for half the
round trip and up to the jitter more, in order (`RemoteLink::setLag`). Measured with a probe
through the real RemoteLink against a local `mu2_server`, ten clicks from a stand and then
15 s of clicks every 100 ms in random directions:

| line | click to move, mirror | click to move, hero ahead | ahead vs server, spam (tiles) |
|---|---|---|---|
| 41 ms, lead 1 | 85 ms | 39 ms | 0.02 mean, 0.21 max |
| 136 ms, lead 2 | 224 ms | 62 ms | 0.03 mean, 0.37 max |
| 248 ms, lead 5 | 368 ms | 77 ms | 0.10 mean, 0.46 max |

A joiner after a snapshot, at 150 ms with spam: 46 of the server's hashes agreed, none disagreed.

## Measured — the cushion and the datagrams

A probe running Play::update's pacing at 60 fps over the real RemoteLink, 30 s a run, counting
the stalls a player would see:

| line | stalls before | with the cushion |
|---|---|---|
| 40 ms, no jitter | 0 | 0 |
| 40 ms, 40 ms jitter | 0 | 0 |
| 100 ms, 80 ms jitter | 43 (529 ms frozen) | 3 (28 ms), one tick held |

And with packets lost (`--loss`), 60 ms and 10 ms of jitter, the cushion on in both:

| loss | the stream alone | with the datagrams |
|---|---|---|
| 0% | 0 | 0 |
| 2% | 14 (1113 ms frozen), cushion 3 | 0, cushion 0 |
| 5% | 25 (1289 ms frozen), cushion 3 | 2 (42 ms), cushion 1 |

Every run's mirror agreed with every one of the server's hashes. With `--map-cap 2`, three bots
into Lorencia: the third raised its copy 2, and all three agreed with all 66 hashes.

The test line's flags, for the client: `--lag MS --jitter MS --loss P --no-udp`.
