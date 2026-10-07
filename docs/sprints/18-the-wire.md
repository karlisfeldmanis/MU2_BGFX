# The wire

Server-plan phase 4, brought forward on 2026-10-07 (the user chose "network first"): the client
on this Mac plays on a server on the Hetzner box (server/README.md). **One player per world** at
first -- each connection gets its own realm -- so the user can play on the real server before
phases 2 and 3 make the world shared.

## How: lockstep

The server raises the realm and steps it at 20 Hz; the client's mirror (sprint 17, batch 5)
raises the same realm from the same seed and steps it with the same commands at the same ticks.
Per tick the server sends only what went in -- the commands it applied and its wall clock --
never the state. It sends a hash of its realm now and then; a client that disagrees has
diverged and says so. The server stays the authority: its realm decides, the mirror shows.

This needs the rules to give the same bits on the Mac and on Linux.

## 1. The same bits everywhere — done 2026-10-07

- `-ffp-contract=off` for the whole build (CMakeLists.txt).
- `sim/fmath.h`: the rules' `sin`, `cos`, `atan2` and `hypot`, fdlibm's kernels in double; the
  raid's one `pow` a literal. No `std::sort`, no library distributions and no hash-map walk in
  the rules, checked.
- sim_test's full output identical on the Mac and the box (0 of ~300,000 lines differ).
- The Twister's third-beat check was one fight's luck (it scraped by at 1); it now pools three
  seeds (5 third-beat strikes in 86 storms). sim_test 6610 checks, the standing 10 failing.

## Next

2. The protocol: framing, `Hello`/`Welcome` (map, seed, class, level, config), the per-tick
   message (tick, wall clock, commands), the hash.
3. `server/src`: `mu2_server`, a TCP listener and one realm per connection on a 20 Hz loop.
4. `RemoteLink` in the client (`--server host:port`): raises the mirror from `Welcome`, steps on
   each tick message, owes and repays ticks rather than clamping.
5. On the box: a systemd service, the port opened in ufw, and a game played on it from here.
