# The server

MU2_BGFX's server runs on a Hetzner box; players run only the client (the user's decision of
2026-10-07). How it was built, in phases and sprints 17-25, is "How it got here" below; what
comes next is `docs/order-plan.md`.

Everything that is the server's alone is in this folder (the user, 2026-10-07: *"we need that
server source is also in /server folder"*):

- **`server/src/`** -- the server program: the world host, the wire, the character store, from
  phase 3. It may include core, content and sim, and never gfx or game; the layer
  check reads it as the `server` layer.
- **`deploy.sh`** -- copy, build and test on the box; later the service file and the box's
  settings beside it.

What the client and the server both run stays shared under `src/`: the rules (`src/sim`) and
what they read (`src/core`, `src/content`). Both programs compile that one copy, which is what
keeps a rule from meaning one thing on the server and another in the client.

## The box

| | |
|---|---|
| name | `ubuntu-1gb-hel1-1`, Helsinki |
| address | 37.27.158.226 (IPv6 2a01:4f9:c010:b6ca::1) |
| login | `ssh hetzner` (root, `~/.ssh/id_ed25519`); root's password in the macOS Keychain, service "hetzner 37.27.158.226", for the web console only |
| system | Ubuntu 26.04, x86_64, 1 vCPU, 927 MB, 2 GB swap (`/swapfile`), 18 GB disk |
| tools | g++ 15.2, CMake 4.2, Ninja, rsync |
| firewall | `ufw`: SSH, and 44406 for the game: tcp for the stream, udp for the tick datagrams (sprint 24) |
| service | `mu2-server` (systemd, `server/mu2-server.service`): `/opt/mu2/build/mu2_server --port 44406 --store /var/lib/mu2/characters.db`, restarted if it stops, its own throwaway user whose one writable folder is `/var/lib/mu2` (systemd keeps it at `/var/lib/private/mu2`). `systemctl status mu2-server`, `journalctl -u mu2-server -f` |
| packages | `libsqlite3-dev` for the character store and `sqlite3` to read it; `deploy.sh` installs them if missing |
| code | `/opt/mu2`, put there by `deploy.sh` |

## Playing on it

    ./main.sh                                  the character screen, asking the box for your characters
    build/mu2 --play --server 37.27.158.226    straight in

The client joins on port 44406, raises its mirror from the server's answer and plays every tick
the server steps (sprint 18). Protocol 24 (`src/net/wire.h`, `kVersion`).
**Connections to the same world share it** (sprint 19):
- every world is raised once and **lives for ever** (55681604), empty or not;
- a newcomer comes in by a Join at the next tick and is sent a **snapshot** of the realm, not
  its past (sprint 21);
- one that goes leaves by a Leave.

**Characters belong to an account** (sprint 23): for now a machine key the
client makes once (`saves/account.key`), with a name and password to be laid over it later. They
live on the box's disk in `/var/lib/mu2/characters.db` (SQLite, `server/src/store.h`), written as
a player leaves a world, every minute and when the service stops, so a restart or a redeploy keeps
them, and a character comes back **in the world he left**.
`sqlite3 /var/lib/private/mu2/characters.db .tables` shows the store. `server/deploy.sh` puts a
new build there and restarts the service.

What is in `server/src/` (split out of one 1900-line `main.cpp` in `docs/order-plan.md` sprint C):

| file | what |
|---|---|
| `host.h` | the clock, the constants, the globals, `World`, `Session`, `Party`, and every job's functions |
| `main.cpp` | the flags, the frame dispatch (`hear`) and the loop |
| `world.cpp` | raising a world (from its own Welcome, `sim::raiseFrom`), Hello and Welcome, the tick, landing, keeping |
| `chat.cpp`, `party.cpp`, `accounts.cpp` | the chat, the parties, the character screen |
| `store.*` | the characters on disk |

`tests/server_test.cpp` checks them without a socket, in `checks`.

## Building there

`cmake -DMU2_SERVER_ONLY=ON` builds `mu_core`, `mu_content_data`, `mu_sim`, `sim_test`,
`save_test`, `placement_test`, `bot` and `raid`, with no bgfx, glfw, shaders or Metal. A first
build takes a few minutes on the one core; `sim_test` runs in about 10 s.

## What the first build found (2026-10-07)

- Two includes Apple's library supplied by accident and Linux's does not: `<cstdint>` in
  `core/args.h`, `<ctime>` in `tests/sim_test.cpp`. Fixed.
- **The rules were not bit-identical between this Mac and the box** -- a seeded fight drifted
  by a tick, and one more check failed there. Apple's clang fuses `a*b+c` on ARM where GCC on x86
  does not, and the two libms round `atan2`, `sin`, `cos`, `hypot` and `pow` differently.
  **Fixed the same day**, because lockstep needs it (the user chose the network first): the build
  has `-ffp-contract=off`, the rules use `sim/fmath.h` (fdlibm's kernels in double, fixed
  order), and the one `pow` is a literal. sim_test's whole output, ~300,000 lines with every
  logged tick of every fight, is now identical on the two machines (0 lines differ).

## How it got here

The plan was `docs/server-plan.md` (2026-10-05, from an audit of `src/sim` and of MU's netcode),
and each sprint had its file under `docs/sprints/`. Both were deleted on 2026-10-13 once built;
git keeps them. Code comments cite them as "phase N" and "sprint N".

| phase | what | sprint |
|---|---|---|
| 1, the seam | `game/` never writes a `Realm`: every ask is a `Command` applied at the next tick's start, answered by a happening; the client reads a const mirror realm through a `Link` (`src/client`), never a separate View; figures by id; debug switches are GM commands | 17, the seam |
| 2, many heroes | `Player` holds what is one-per-hero; happenings carry an audience; credit, drops and traps work from "which player"; players step in id order; `Join` and `Leave` are the server's commands | 19, many heroes |
| 3, the world host | one 20 Hz deadline steps every world; the server moves a player between worlds and owns time, the seed and persistence (`characters.db`, saved on leaving, every minute and on stop) | 20, the world host |
| 4, the wire | TCP, length-framed, little-endian; a frame that does not parse drops that connection, never the server; lockstep: the mirror steps the server's commands and the hashes are compared | 18, the wire (brought forward); 21, the snapshot (a newcomer gets the realm as bytes, not its past); 22, nothing the client says (the client decides nothing the server does not) |
| 5, feel | `--lag` to test a slow line; the ticks a second way by UDP beside the stream; a cushion of held ticks; the hero's own body predicted ahead of the mirror | 24, the quiet line; 25, the clear thread (the server's own timing) |
| 6, accounts | the character screen asks the server; a machine key now, name and password later | 23, the account |
| 7, playing together | other players drawn, chat, party (`docs/party.md`), trade (`sim/realm_deal.cpp`), guilds (`docs/guild.md`), the events' doors on one wall clock (`sim/config.h`). PvP and the PK system are still open (`docs/order-plan.md` §7) | after 25 |

