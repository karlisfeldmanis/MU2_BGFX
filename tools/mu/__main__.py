"""mu: one command for making the game's content, for a person, a script or an agent.

    ./mu make <Name|world/Name|path.json>   one recipe, from source to cooked, what is stale only
    ./mu make world <w>                      every object recipe of a world, then its cook
    ./mu make ground <w> [--folder N|--import] the bare land: terrain import, tile sheets, ground, cook
    ./mu new object <w>/<ObjectNN>           a recipe from MU's model: mesh, sheets, placements
    ./mu new monster <number> [--db]         from OpenMU's row and spawns and MuMain's model
    ./mu new item "<name>" [--db]            a weapon, shield or armour piece from OpenMU's call
    ./mu make sounds                         sounds.json and the wavs into showing.mus
    ./mu make tables                         every world's tables (after a new item or monster)
    ./mu make gates                          the realm's gate table from the world manifests
    ./mu make ... --plan                     what would run and why, running nothing
    ./mu check                               the quick gates: code and content
    ./mu where <name> [--docs]               every file that names it
    ./mu shot <world> <col,row> [--time T]   a muted shot at a tile, the save untouched
    ./mu compare <a.png> <b.png>             side by side, labelled, with how much moved
    ./mu cores [N]                           how many cores mu's tools may use, kept per machine

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
import look  # noqa: E402
import make  # noqa: E402
import settings  # noqa: E402
from things import WORKSHOP, NotFound, find  # noqa: E402


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

    n = sub.add_parser("new", parents=[common])
    n.add_argument("kind", choices=("object", "monster", "item"))
    n.add_argument("target", help="<world>/<ObjectNN>, a monster's server number, or an item's name")
    n.add_argument("--world", help="monster: its world, when OpenMU spawns it nowhere this game has")
    n.add_argument("--db", action="store_true", help="monster, item: write the mu.db row when it is absent")
    n.add_argument("--prefix", help="the sheet prefix; read off the world's recipes by default")

    sub.add_parser("check", parents=[common])

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
        target = args.target
        if target[0] == "ground" and len(target) == 2:
            steps = make.plan_ground(target[1], args.folder, args.threads, args.reimport)
        elif target[0] == "world" and len(target) == 2:
            steps = make.plan_world(target[1], args.threads)
        elif target == ["sounds"]:
            steps = make.plan_sounds(args.threads)
        elif target == ["tables"]:
            steps = make.plan_tables(args.threads)
        elif target == ["gates"]:
            steps = make.plan_gates()
        elif len(target) == 1:
            thing = find(target[0])
            say(f"{thing.kind} {thing.build_name}, world {thing.world}: {thing.rel}")
            steps = make.plan_thing(thing, args.threads)
        else:
            raise NotFound(f"make takes one name, 'world <w>', 'ground <w>', 'sounds', 'tables' or 'gates', "
                           f"not {' '.join(target)}")
        ok, results = make.run(steps, args.plan, say, os.path.join(WORKSHOP, "logs", "mu"),
                               settings.environment(cores))
        return ok, {"plan": args.plan, "cores": cores, "steps": results}
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
    if args.command == "where":
        return look.where(args.name, say, args.docs)
    if args.command == "shot":
        return look.shot(args.world, args.at, args.time, args.frames, args.out, say)
    if args.command == "compare":
        return look.compare(args.a, args.b, args.out, say)
    raise NotFound(f"no command {args.command}")


if __name__ == "__main__":
    sys.exit(main())
