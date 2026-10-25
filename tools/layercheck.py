#!/usr/bin/env python3
"""The layer rule, enforced instead of reviewed.

CMakeLists.txt has said since sprint 1 that "gfx never includes game or sim, and sim never
includes gfx -- nothing enforces that but review". This is the enforcement. It reads every
`#include "layer/file.h"` in src/ and refuses an arrow the architecture does not allow.

The arrows, and why each one points the way it does -- the long form is docs/reference/architecture.md:

    core      depends on nothing. Logging, argument parsing, json, files, maths. It is the
              only layer a test may link on its own without dragging a renderer in.
    content   depends on core. What the cook wrote, read back: meshes, textures, the ground,
              the tables, the cooked .mur files. It knows file formats and no rules.
    sim       depends on core and content. The realm, the tick, the rules, items, routing,
              the market. It must never learn that a screen exists -- foundation 9, "the sim
              runs without a window", is what `sim_test` keeps true, and it can only stay
              true while this arrow does not exist: sim -> gfx.
    gfx       depends on core and content. bgfx, the views, the renderer, the passes, the
              overlay. It draws what it is handed and must never reach back into the game
              that handed it over, nor into the sim -- a renderer that reads the realm is a
              renderer that cannot be run on a bench.
    game      depends on all of the above. The play loop, the world, the figures, the
              windows, the effects. This is where the three meet.
    studio    the editor's panels and the cook's client (docs/plans/order-plan.md §9c): core,
              content and gfx, never the game -- the mode hands the panels what they show, so
              they are drawn and tested with no world behind them.
    app       depends on all of the above. The application object and its modes. Nothing
              depends on app: it is the top.
    net       the wire, over sim: a message carries a sim::Command.
    client    the client's line to the realm (Link, LocalLink, RemoteLink), over net and below
              game, so a bot plays the server without a renderer.
    server    server/src: core, content, sim and net, never a screen or the client.
    tools     the bots and the cook's C++: up to client, never gfx, game or app.

A violation is an error and the exit code is 1, so `cmake --build build --target checks`
fails on it the way it fails on a test. Run it alone with `tools/layercheck.py`.
"""

import os
import re
import sys

from paths import ROOT
SRC = os.path.join(ROOT, "src")
SERVER = os.path.join(ROOT, "server", "src")
TOOLS = os.path.join(ROOT, "tools")

# What each layer may include. A layer may always include itself.
ALLOWED = {
    "core": {"core"},
    "content": {"core", "content"},
    "sim": {"core", "content", "sim"},
    "gfx": {"core", "content", "gfx"},
    # The wire: TCP and the protocol (sprint 18). A message carries a Command.
    "net": {"core", "content", "sim", "net"},
    # The client's line to the realm: the Link, LocalLink and RemoteLink (docs/plans/order-plan.md
    # sprint C). Below game, so a bot links it without a renderer.
    "client": {"core", "content", "sim", "net", "client"},
    "game": {"core", "content", "sim", "net", "client", "gfx", "game"},
    # The studio's panels and the cook's client (docs/plans/order-plan.md §9c): below app, beside game.
    "studio": {"core", "content", "gfx", "studio"},
    # The launcher (docs/plans/launcher.md): its own program, and the crash, bug and graphics
    # reports the game sends through it. Its window is gfx; its downloads and sign-in the wire.
    "launcher": {"core", "gfx", "net", "launcher"},
    "app": {"core", "content", "sim", "net", "client", "gfx", "game", "studio", "launcher", "app"},
    # The server program, in server/src/ and not src/ (the user, 2026-10-07: "we need that server
    # source is also in /server folder"): the rules and what they read, never a screen or the
    # client. Nothing under src/ may include it -- no layer above lists it.
    "server": {"core", "content", "sim", "net", "server"},
    # tools/: the bots, the raid stats, the cook's C++ -- programs over the rules and the wire,
    # never the renderer or the game (sprint C: netbot reached into game/ for RemoteLink).
    "tools": {"core", "content", "sim", "net", "client"},
}

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

# The client sees the realm and never changes it (server/README.md phase 1): a writable
# sim::Realm -- a reference, a pointer or one of its own -- in game/ or app/ is refused, except at
# Play's realm and its `local_` (what will be the server's, each use a line to move) and the
# scripted hand (game/headless.cpp), which plays the server's part in a run with no window. The
# links themselves are the client layer's now (src/client), where a mirror is writable by design. Everything else reads `const sim::Realm&`, which is what a mirror will be.
WRITABLE_REALM = re.compile(r'(?<![\w:])(?:\w+::)*Realm\s*(?:[&*]|\s+[a-z_]\w*\s*[;{=(])')


def writable_realm(code):
    """Whether a line of code names a sim::Realm it could change: one not preceded by `const`."""
    return any(not code[:m.start()].rstrip().endswith("const") for m in WRITABLE_REALM.finditer(code))
WRITABLE_ALLOWED = {os.path.join("src", "game", "headless.cpp")}
# And on a line of its own: Play's realm, which the link works on (Play::realmHeld_).
WRITABLE_MARKS = ("local_", "realmHeld_")


def layer_of(path):
    """The layer a file under src/ belongs to, or None for src/main.cpp itself.

    A file in a subfolder belongs to its top folder: src/game/ui/hud.cpp is `game`. The
    subfolders inside a layer are there to keep 67 files legible, not to add arrows.
    """
    if os.path.commonpath([path, SERVER]) == SERVER:
        return "server"  # server/src/, every file of it, main.cpp included
    if os.path.commonpath([path, TOOLS]) == TOOLS:
        return "tools"
    rel = os.path.relpath(path, SRC)
    parts = rel.split(os.sep)
    return parts[0] if len(parts) > 1 else None


def main():
    violations = []
    counted = 0
    walked = [w for root in (SRC, SERVER, TOOLS) if os.path.isdir(root) for w in os.walk(root)]
    for dirpath, dirnames, filenames in walked:
        for name in sorted(filenames):
            if not name.endswith((".h", ".hpp", ".cpp")):
                continue
            path = os.path.join(dirpath, name)
            here = layer_of(path)
            if here is None:
                continue  # src/main.cpp is the entry point and sits above every layer
            if here not in ALLOWED:
                violations.append(f"{os.path.relpath(path, ROOT)}: unknown layer '{here}'")
                continue
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            rel = os.path.relpath(path, ROOT)
            if here in ("game", "app") and rel not in WRITABLE_ALLOWED:
                for number, line in enumerate(text.splitlines(), 1):
                    code = line.split("//")[0]
                    if writable_realm(code) and not any(mark in code for mark in WRITABLE_MARKS):
                        violations.append(f"{rel}:{number}: a writable sim::Realm in the client "
                                          f"(only the links, Play's realmHeld_ and local_, and headless.cpp may hold one)")
            for target in INCLUDE.findall(text):
                head = target.split("/")[0]
                if head not in ALLOWED:
                    continue  # a vendored header or a sibling include, not one of ours
                counted += 1
                if head not in ALLOWED[here]:
                    violations.append(
                        f"{os.path.relpath(path, ROOT)}: {here} -> {head} "
                        f'(#include "{target}") is not an arrow the architecture allows'
                    )

    if violations:
        print(f"layercheck: {len(violations)} violation(s) of docs/reference/architecture.md")
        for line in violations:
            print(f"  {line}")
        return 1
    print(f"layercheck: {counted} includes, every one of them an allowed arrow")
    return 0


if __name__ == "__main__":
    sys.exit(main())
