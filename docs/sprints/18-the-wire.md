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

## 2-4. The protocol, the server and RemoteLink — done 2026-10-07

- **`src/net`**, a new layer (core, content and sim below it; game, app and the server above):
  `socket.*` (non-blocking POSIX TCP, no Nagle, IPv4 and IPv6) and `wire.*` (frames of u32
  length, u8 kind, little-endian body; `Hello`, `Welcome`, `Command`, `Tick`, `Hash`; a frame that
  does not parse drops that connection). Protocol version 1, port 44406.
- **`server/src/main.cpp`, `mu2_server`**: a listener and one realm per connection on one 20 Hz
  deadline loop (poll, step every realm, flush), owing ticks up to a second rather than dropping
  them. Raises his world on his Hello, outfits him with the rules' own cradle, sends each tick's
  inputs and a hash a second. A world name is letters only, so a Hello cannot name a path.
- **`sim/cradle.*`**: a new character's points for his weapon and the weapon in his hands, moved
  out of Play::open so the mirror and the server do the same.
- **`game/remote_link.*`**: joins (Hello, then waits up to 10 s for the Welcome), sends commands,
  steps the mirror per tick with the same inputs in the same order, and checks the server's
  hashes; logs a divergence once and a summary on close.
- **Play** owns its realm (`realmHeld_`) and holds the link by pointer: `useServer(host, port)`
  before open makes it remote. Remote, open raises from the Welcome, keeps every nest (no
  uncooked-figure filter), sets up no arena, raid or roads; the server's doors -- save, vault,
  machine, clock, rain, the GM switches -- refuse and say so once each. The frame loop pumps the
  link, steps only ticks that have arrived, and repays when more than two behind.
- `--server HOST[:PORT]` on the client. layercheck: `net` is a layer; the links and Play's
  realm may hold a writable realm.
- **Loopback on the Mac:** `mu2_server --port 44599` and `mu2 --play --server 127.0.0.1:44599
  --talk Lumen`: joined, walked to Lumen by Order commands over the wire and was served; 12 of
  12 of the server's hashes agreed with the mirror.

## 5. On the box — done 2026-10-07

- `server/mu2-server.service`: systemd runs `/opt/mu2/build/mu2_server --port 44406`, restarts it
  if it stops, as its own throwaway user with nothing writable. `server/deploy.sh` copies, builds
  (mu2_server and the tests), installs the service, opens 44406/tcp in ufw and restarts it.
  The server line-buffers stdout so the journal has each join as it happens.
- **Played from this Mac:** `mu2 --play --server 37.27.158.226 --talk Lumen` joined the box,
  walked to Lumen by commands over the internet and was served; 14 of 14 of the server's hashes
  agreed with the mirror. 26 ms round trip to Helsinki.

## Next

- The lobby (Sanctuary) choosing local or the server, so a plain launch can play on it.
- A dropped connection said on screen, not only in the log; a reconnect.
- Phase 2: more than one hero in a realm -- then two clients see each other.

