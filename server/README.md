# The server

MU2_BGFX's server runs on a Hetzner box; players run only the client (PLAN.md, the decision of
2026-10-07). The plan is `docs/server-plan.md`; the sprint in hand is `docs/sprints/17-the-seam.md`.

Everything that is the server's alone is in this folder (the user, 2026-10-07: *"we need that
server source is also in /server folder"*):

- **`server/src/`** -- the server program: the world host, the wire, the character store, from
  server-plan phase 3. It may include core, content and sim, and never gfx or game; the layer
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
| firewall | `ufw`: only SSH in. The game's port is opened at phase 4 |
| code | `/opt/mu2`, put there by `deploy.sh` |

## Building there

`cmake -DMU2_SERVER_ONLY=ON` builds `mu_core`, `mu_content_data`, `mu_sim`, `sim_test`,
`save_test`, `placement_test`, `bot` and `raid`, with no bgfx, glfw, shaders or Metal. A first
build takes a few minutes on the one core; `sim_test` runs in about 10 s.

## What the first build found (2026-10-07)

- Two includes Apple's library supplied by accident and Linux's does not: `<cstdint>` in
  `core/args.h`, `<ctime>` in `tests/sim_test.cpp`. Fixed.
- **The rules are not bit-identical between this Mac and the box.** The same seeded run
  diverges by a tick in a long fight, and one more sim_test check fails there (11 against the
  Mac's 10: "as late as its third strike, four and a half tiles out"). Two causes:
  - Apple's clang fuses `a*b+c` into one rounding on ARM and GCC on x86 does not. Building the
    Mac with `-ffp-contract=off` gives the box's 11 and removes most of the difference.
  - The rest is the maths library: `std::atan2` (24 calls in sim), `sin`/`cos` (20 each),
    `hypot` (14) and `pow` (8) round differently in Apple's libm and glibc.

  **Not fixed, on purpose.** The server is authoritative and the client draws what it is told,
  so phases 4-7 do not need the two machines to agree. It matters only to replay a server's
  log on the Mac. The fix, when wanted: `-ffp-contract=off` for `mu_sim` everywhere, and the
  five functions written once in `sim` (sqrt is exact on both and stays).
