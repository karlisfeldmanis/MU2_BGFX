"""What a world is still missing, in the order that filling it in pays off most.

    python3 pipeline/worklist.py source/world/lorencia workshop
    python3 pipeline/worklist.py source/world/lorencia workshop --all

Nothing has to be wired up to put a new object into the town. The map's placement list already
says where every one of them stands, at what angle and what size, and the viewer matches a
placement to a model by name — so the first time an asset called Tree01 finishes building, all
eighty of Lorencia's Tree01s stand up by themselves.

What that leaves is a question of order, and it is worth answering with the numbers rather
than by eye. The town is 2870 placements over 109 kinds and they are not evenly distributed:
the top ten kinds are more than a third of everything standing in it, and there is a long tail
of things placed once. Building in placement order means the town fills visibly from the first
asset; building in the order the file listing happens to be in means doing forty models before
it stops looking empty.

So this prints what is placed, how much of it is built, and what the next asset is worth — in
placements, and as a share of the whole town. The percentage is the honest measure of
progress: kinds built is a count of work done, and placements standing is a count of what
somebody walking through it would see.
"""

import json
import sys
from pathlib import Path


def built(build: Path, world: str) -> set[str]:
    """Every model name that has a .glb, by the name a placement would call it.

    Narrowed to the map asking, because a name is not unique across maps. Only Lorencia gives
    its objects names; every other world runs them as Object01 upward out of its own folder,
    so a bare sweep of the build reported four of Noria's kinds as already done — they were
    the character scene's Object06, 15, 35 and 39, which are a ruined arch, a step, a tree and
    a wall against Noria's bush, fence, lamp and sign. Four kinds and 1401 placements of
    progress that did not exist.

    So the world's own build directory first, and the rest of the build after it for a map
    whose objects are named rather than numbered. See Index.Built and World.Local, which is
    the same rule at runtime.
    """
    mine = {path.stem for path in (build / "world" / world).rglob("*.glb")}

    # Everything else, but never a name another world owns. A placement in Noria that names
    # Object06 means Noria's, and if that is not built the answer is "not built" rather than
    # somebody else's model of the same name.
    owned = {path.stem for path in (build / "world").rglob("*.glb")}

    return mine | {
        path.stem for path in build.rglob("*.glb") if path.stem not in owned
    }


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 worklist.py <world asset dir> <build dir> [--all]",
              file=sys.stderr)
        raise SystemExit(2)

    source, build = Path(sys.argv[1]), Path(sys.argv[2])
    everything = "--all" in sys.argv

    world = json.loads(next(
        path for path in source.glob("*.json")
        if "objects" in json.loads(path.read_text())).read_text())

    have = built(build, world["world"])

    counts: dict[str, int] = {}
    hidden = 0

    for one in world["objects"]:
        # Not work. The client marks these HiddenMesh = -2 and never draws them, so building
        # one would put a thing in the town the game does not have — and PoseBox01 sat at 53
        # placements, thirteenth on this list, looking exactly like the next job.
        if one.get("hidden"):
            hidden += 1
            continue

        name = one["model"] or f"type {one['type']}"
        counts[name] = counts.get(name, 0) + 1

    total = sum(counts.values())
    standing = sum(count for name, count in counts.items() if name in have)

    print(f"\n=== {world['world']} ===")

    if hidden:
        print(f"  {hidden} placements the client never draws, not counted")
    print(f"  {standing} of {total} placements standing "
          f"({standing * 100.0 / total:.1f}%), "
          f"{sum(1 for n in counts if n in have)} of {len(counts)} kinds built")
    print()

    ordered = sorted(counts.items(), key=lambda pair: (-pair[1], pair[0]))
    shown = ordered if everything else ordered[:25]

    # Cumulative over the whole ordering rather than over what is shown, so the figure in the
    # last column keeps meaning "this much of the town, once everything down to here exists".
    running = 0
    print("     placements  share   cumulative  kind")

    for name, count in ordered:
        running += count

        if (name, count) not in [(n, c) for n, c in shown]:
            continue

        mark = "ok  " if name in have else "    "
        print(f"  {mark} {count:6d}   {count * 100.0 / total:5.2f}%   "
              f"{running * 100.0 / total:6.2f}%    {name}")

    if not everything and len(ordered) > len(shown):
        rest = sum(count for _, count in ordered[len(shown):])
        print(f"       ... and {len(ordered) - len(shown)} more kinds, "
              f"{rest} placements between them ({rest * 100.0 / total:.1f}%)")

    missing = [(name, count) for name, count in ordered if name not in have]

    if missing:
        name, count = missing[0]
        print()
        print(f"  next       {name} is worth {count} placements "
              f"({count * 100.0 / total:.1f}% of the town) and nothing else is worth more.")

        # What a small run would buy, because the answer is usually surprising: on Lorencia
        # the first five unbuilt kinds are a third of everything left.
        five = sum(count for _, count in missing[:5])
        print(f"             the next five together are {five} "
              f"({five * 100.0 / total:.1f}%).")
    else:
        print()
        print("  nothing left: every kind this map places has been built.")


main()
