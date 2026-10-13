"""mu: one command for making the game's content, for a person, a script or an agent.

    ./mu make <Name|world/Name|path.json>   one recipe, from source to cooked, what is stale only
    ./mu make world <w>                      every object recipe of a world, then its cook
    ./mu make ground <w> [--folder N|--import] the bare land: terrain import, tile sheets, ground, cook
    ./mu new object <w>/<ObjectNN>           a recipe from MU's model: mesh, sheets, placements
    ./mu new monster <number> [--db]         from OpenMU's row and spawns and MuMain's model
    ./mu new item "<name>" [--db]            a weapon, shield or armour piece from OpenMU's call
    ./mu make sounds                         sounds.json and the wavs into showing.mus
    ./mu make tables                         every world's tables (after a new item or monster)
    ./mu make tables <w>                     one world's (after its townsfolk moved)
    ./mu make realm                          the realm's gate and world tables from the manifests
    ./mu make ... --plan                     what would run and why, running nothing
    ./mu check                               the quick gates: code and content
    ./mu where <name> [--docs]               every file that names it
    ./mu shot <world> <col,row> [--time T]   a muted shot at a tile, the save untouched
    ./mu compare <a.png> <b.png>             side by side, labelled, with how much moved
    ./mu cores [N]                           how many cores mu's tools may use, kept per machine
    ./mu recipe <name>                       a recipe's file(s), parsed, and the material library
    ./mu research <Monster|number> [--write] everything MuMain does for it, a checklist in its recipe
    ./mu edit <name|file> <path> <value>     one value in place (islands.0.material brass), journalled
              ... --insert / --remove          a value put in at path, or taken out (no value)
    ./mu gate add <w> <to> <col> <row>       a way in here and its way out there (tools/mu/gates.py)
    ./mu gate remove <w> <number>            a gate and its pair
    ./mu paint <w> <col> <row> <slot>        the ground's tiles.png (--overlay --alpha --radius, --undo)
    ./mu do "<line>; <line>; ..."            drive an open studio: subject, scene, shot, wait, status...
    ./mu serve                               the cook's job runner; `mu make` hands it its jobs
    ./mu jobs [--watch [N]] | cancel N [--now] what it is cooking, with bars and time left
    ./mu dist macos                          the player's app, dist/macos/MU2.app, from packs

Every command takes --json and then prints one JSON object as its last line, for a script or
an agent to read instead of the prose. Every step runs niced and held to `mu cores` (60% of
the machine unless set; settings.py), Blender and torch included, never every core. docs/order-plan.md, sprint B; the steps and the traps they close are in make.py.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check  # noqa: E402
import dist  # noqa: E402
import jobs  # noqa: E402
import look  # noqa: E402
import make  # noqa: E402
import serve  # noqa: E402
import settings  # noqa: E402
from things import WORKSHOP, NotFound  # noqa: E402


def main():
    parser = argparse.ArgumentParser(prog="mu", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="end with one JSON object")
    # Taken after the command as well as before it: `mu make X --json` is how it gets typed.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    sub = parser.add_subparsers(dest="command", required=True)

    m = sub.add_parser("make", parents=[common])
    m.add_argument("target", nargs="+")
    m.add_argument("--plan", action="store_true", help="say what would run and why; run nothing")
    m.add_argument("--folder", type=int, default=None,
                   help="with `ground`: MU's Data/World<N> to import from (after the first, <w>.json knows)")
    m.add_argument("--import", dest="reimport", action="store_true",
                   help="with `ground`: read the terrain again from MU's Data")
    m.add_argument("--cores", "--threads", dest="cores", type=int, default=None,
                   help="for this run; `mu cores N` keeps one")
    m.add_argument("--here", action="store_true",
                   help="run in this process even when `mu serve` is running")

    r = sub.add_parser("recipe", parents=[common])
    r.add_argument("target")

    q = sub.add_parser("research", parents=[common])
    q.add_argument("target", help="a monster recipe's name, or a breed's number")
    q.add_argument("--write", action="store_true", help="into the recipe as mu_checklist, journalled")

    e = sub.add_parser("edit", parents=[common])
    e.add_argument("target", help="a recipe's name, or a file under source/ or sheets/")
    e.add_argument("path", help="dotted, list indices as numbers: islands.0.material")
    e.add_argument("value", nargs="?", help="JSON (7, 0.35, true, \"brass\") or a bare word for a string")
    how = e.add_mutually_exclusive_group()
    how.add_argument("--insert", action="store_true",
                     help="put the value in at path: a list's index (its length appends) or a new key")
    how.add_argument("--remove", action="store_true", help="take the value at path out")

    g = sub.add_parser("gate", parents=[common])
    g.add_argument("action", choices=["add", "remove"])
    g.add_argument("world", help="the world the way in stands in (remove: the gate's world)")
    g.add_argument("rest", nargs="+", help="add: <to-world> <column> <row>; remove: <number>")

    pa = sub.add_parser("paint", parents=[common])
    pa.add_argument("world")
    pa.add_argument("at", nargs="*", type=int, help="<column> <row> <slot>")
    pa.add_argument("--overlay", action="store_true", help="paint the overlay slot over the base")
    pa.add_argument("--alpha", type=int, default=255, help="with --overlay: how far over, 0..255")
    pa.add_argument("--radius", type=int, default=0, help="a round brush, in tiles")
    pa.add_argument("--undo", action="store_true", help="take back the world's last stroke")

    d = sub.add_parser("do", parents=[common])
    d.add_argument("lines", help="the studio's words, ';' between them (app/modes/studio_control.cpp)")

    v = sub.add_parser("serve", parents=[common])
    v.add_argument("--stop", action="store_true", help="stop a running one after its job")

    j = sub.add_parser("jobs", parents=[common])
    j.add_argument("action", nargs="?", choices=("cancel",))
    j.add_argument("number", nargs="?", type=int)
    j.add_argument("--watch", nargs="?", type=int, const=0, default=None,
                   help="live; a job's number, or every job")
    j.add_argument("--now", action="store_true", help="with cancel: kill the running step")

    n = sub.add_parser("new", parents=[common])
    n.add_argument("kind", choices=("object", "monster", "item"))
    n.add_argument("target", help="<world>/<ObjectNN>, a monster's server number, or an item's name")
    n.add_argument("--world", help="monster: its world, when OpenMU spawns it nowhere this game has")
    n.add_argument("--db", action="store_true", help="monster, item: write the mu.db row when it is absent")
    n.add_argument("--prefix", help="the sheet prefix; read off the world's recipes by default")

    sub.add_parser("check", parents=[common])
    d = sub.add_parser("dist", parents=[common])
    d.add_argument("target", choices=["macos"], help="the platform; macos so far")

    k = sub.add_parser("cores", parents=[common])
    k.add_argument("count", nargs="?", type=int)

    w = sub.add_parser("where", parents=[common])
    w.add_argument("name")
    w.add_argument("--docs", action="store_true", help="search docs/ as well")

    s = sub.add_parser("shot", parents=[common])
    s.add_argument("world")
    s.add_argument("at", help="column,row")
    s.add_argument("--time", default="noon", choices=("noon", "dusk", "night"))
    s.add_argument("--frames", type=int, default=240)
    s.add_argument("--out", help="the folder; shots/mu/<world>_<at>_<time> by default")

    c = sub.add_parser("compare", parents=[common])
    c.add_argument("a")
    c.add_argument("b")
    c.add_argument("--out")

    args = parser.parse_args()
    lines, quiet = [], []

    def say(text):
        lines.append(text)
        if args.json or quiet:
            return
        try:
            print(text, flush=True)
        except BrokenPipeError:
            # `mu make X | head` closed the pipe. That ends the reading, never the cook: a
            # make killed between its textures and its meshes is the white-mesh trap again.
            quiet.append(True)
            sys.stdout = open(os.devnull, "w")

    try:
        ok, data = dispatch(args, say)
    except NotFound as missing:
        ok, data = False, {"error": str(missing)}
        say(f"mu: {missing}")
    if args.json:
        print(json.dumps({"ok": ok, "command": args.command, "data": data}))
    return 0 if ok else 1


def dispatch(args, say):
    if args.command == "cores":
        if args.count:
            settings.keep(args.count)
        count, source = settings.cores()
        say(f"{count} of {settings.machine()} cores ({source})")
        return True, {"cores": count, "machine": settings.machine(), "from": source}
    if args.command == "make":
        cores, source = settings.cores(args.cores)
        args.threads = cores
        say(f"cores: {cores} of {settings.machine()} ({source})")
        if not args.plan and not args.here and serve.alive():
            return jobs.hand_off(args.target, args.cores, args.folder, args.reimport, say, args.json)
        steps = make.plan(args.target, args.threads, say, args.folder, args.reimport)
        ok, results = make.run(steps, args.plan, say, os.path.join(WORKSHOP, "logs", "mu"),
                               settings.environment(cores))
        return ok, {"plan": args.plan, "cores": cores, "steps": results}
    if args.command == "recipe":
        import edit
        data = edit.recipe(args.target)
        for one in data["files"]:
            say(one["file"])
        return True, data
    if args.command == "research":
        import research
        return True, research.research(args.target, args.write, say)
    if args.command == "edit":
        import edit
        if args.remove:
            line = edit.remove(args.target, args.path, who="mu")
            say(f"{line['file']}: {line['path']} removed (was {json.dumps(line['old'])})")
            return True, line
        if args.value is None:
            raise NotFound("a value: mu edit <target> <path> <value>")
        try:
            value = json.loads(args.value)
        except ValueError:
            value = args.value
        line = (edit.insert if args.insert else edit.edit)(args.target, args.path, value, who="mu")
        say(f"{line['file']}: {line['path']} {json.dumps(line['old'])} -> {json.dumps(line['new'])}"
            + (" (inserted)" if args.insert else ""))
        return True, line
    if args.command == "gate":
        import gates
        if args.action == "add":
            if len(args.rest) != 3:
                raise NotFound("mu gate add <world> <to-world> <column> <row>")
            done = gates.add(args.world, args.rest[0], int(args.rest[1]), int(args.rest[2]), who="mu")
            say(f"gate {done['enter']} in {args.world} leads to gate {done['exit']} in {args.rest[0]}")
        else:
            done = gates.remove(args.world, int(args.rest[0]), who="mu")
            say("removed " + ", ".join(done["removed"]))
        return True, done
    if args.command == "paint":
        import paint
        if args.undo:
            done = paint.undo(args.world, who="mu")
        else:
            if len(args.at) != 3:
                raise NotFound("mu paint <world> <column> <row> <slot> [--overlay --alpha A --radius R]")
            done = paint.brush(args.world, *args.at, overlay=args.overlay, alpha=args.alpha,
                               radius=args.radius, who="mu")
        say(f"{done['tiles']} tiles of {done['world']}'s ground {'taken back' if args.undo else 'painted'}")
        return True, {"world": done["world"], "tiles": done["tiles"]}
    if args.command == "do":
        return drive(args.lines, say)
    if args.command == "serve":
        if args.stop:
            if not serve.alive():
                say("mu serve is not running")
                return False, {"error": "not running"}
            jobs.ask({"op": "stop"})
            say("mu serve stops after the job it is running")
            return True, {}
        return serve.serve(say)
    if args.command == "jobs":
        if args.action == "cancel":
            if args.number is None:
                raise NotFound("mu jobs cancel <number>")
            return jobs.cancel(args.number, args.now, say)
        if args.watch is not None:
            return jobs.watch(args.watch or None, say, live=not args.json)
        return jobs.listing(say)
    if args.command == "new":
        if args.kind == "object":
            import new
            return new.new_object(args.target, args.prefix, say)
        import new_figure
        if args.kind == "monster":
            if not args.target.isdigit():
                raise NotFound("a monster is named by its server number: mu new monster 31")
            return new_figure.new_monster(int(args.target), args.world, args.db, say)
        return new_figure.new_item(args.target, args.db, say)
    if args.command == "check":
        return check.run(say)
    if args.command == "dist":
        return dist.dist(args.target, say)
    if args.command == "where":
        return look.where(args.name, say, args.docs)
    if args.command == "shot":
        return look.shot(args.world, args.at, args.time, args.frames, args.out, say)
    if args.command == "compare":
        return look.compare(args.a, args.b, args.out, say)
    raise NotFound(f"no command {args.command}")


def drive(lines, say):
    """`mu do`: each line to the studio's control socket, and its answer back, in order."""
    import socket
    path = os.path.join(WORKSHOP, ".mu", "studio.sock")
    answers = []
    try:
        with socket.socket(socket.AF_UNIX) as conn:
            conn.connect(path)
            conn.settimeout(120)   # a scene that opens a world holds the studio's frame for seconds
            stream = conn.makefile("rw")
            for line in [one.strip() for one in lines.split(";") if one.strip()]:
                stream.write(line + "\n")
                stream.flush()
                answer = stream.readline().strip()
                answers.append({"line": line, "answer": answer})
                say(f"{line} -> {answer}")
                if not answer.startswith("ok"):
                    return False, {"answers": answers}
    except OSError as failed:
        say(f"mu do: no studio is listening on {path} ({failed}); open one with build/mu2 --studio")
        return False, {"error": str(failed), "answers": answers}
    return True, {"answers": answers}


if __name__ == "__main__":
    sys.exit(main())
