"""`mu gate`: a gate added with its pair, or taken out with it -- across two worlds' manifests.

A gate is two rows in two files (docs/plans/order-plan.md §9c, sprint G): a way in, `gates.enters`
in the world it stands in (its box, the level it asks, the `target` it sends to), and a way out,
`gates.exits` in the world it lets out on (its box and the way one comes out facing), joined by
the exit's number. So adding one is two inserts and taking one out is two removes or more,
each one of tools/mu/edit.py's list operations: only the touched text changes, and every one is
a line in the journal.

    ./mu gate add lorencia noria 183 136     a way in at column 183, row 136 of Lorencia, its
                                            way out on Noria's arrival tile (rules.arrive)
    ./mu gate remove lorencia 200            gate 200 and its pair

A new pair takes the first two free numbers from 200: MU's own run to 122, and from 1058 are the
events' talk gates. The way in asks level 0 and is a 2x2 box with the given tile its top-left;
the way out is a 2x2 box on the target's arrival tile, facing nowhere, for the user to move
there. Removing a way in takes its way out too, unless another way in still leads to it;
removing a way out takes every way in that leads to it. A list a remove empties goes with it. A gate the sim's code names by number
(sim's kCastleGate and the like) is refused.

Neither reaches the game until `mu make realm` and a rebuild of the game and the server.
"""

import glob
import json
import os
import re
import time

import edit
from things import ROOT, SOURCE, NotFound

FIRST = 200


def manifests():
    """{world: (path, doc)} for every world's manifest."""
    out = {}
    for path in sorted(glob.glob(os.path.join(SOURCE, "world", "*", "world.json"))):
        with open(path) as handle:
            out[os.path.basename(os.path.dirname(path))] = (path, json.load(handle))
    return out


def named_in_code():
    """The gate numbers the sim and the game write as constants (kCastleGate = 66): taking one
    out would leave the code pointing at nothing."""
    numbers = set()
    for path in glob.glob(os.path.join(ROOT, "src", "**", "*.h"), recursive=True):
        with open(path, errors="replace") as handle:
            for found in re.finditer(r"k\w*Gate\w*\s*=\s*(\d+)", handle.read()):
                numbers.add(int(found.group(1)))
    return numbers


def rows(docs):
    """Every gate row: (world, list, index, row)."""
    for world, (_path, doc) in docs.items():
        for kind in ("enters", "exits"):
            for index, row in enumerate(doc.get("gates", {}).get(kind, [])):
                yield world, kind, index, row


def put(docs, world, kind, row, who):
    """Appends `row` to `world`'s gates.<kind>, making the list (or the block) if it has none."""
    path, doc = docs[world]
    gates = doc.get("gates")
    if gates is None:
        return edit.insert(path, ["gates"], {kind: [row]}, who)
    if kind not in gates:
        return edit.insert(path, ["gates", kind], [row], who)
    return edit.insert(path, ["gates", kind, len(gates[kind])], row, who)


def add(world, to, column, row, who="mu"):
    docs = manifests()
    for name in (world, to):
        if name not in docs:
            raise NotFound(f"no world {name} (content/world/<w>/world.json)")
    arrive = docs[to][1].get("rules", {}).get("arrive")
    if not arrive:
        raise NotFound(f"{to} has no rules.arrive to put the way out on")
    taken = {r["number"] for _w, _k, _i, r in rows(docs)} | named_in_code()
    free = [n for n in range(FIRST, 1000) if n not in taken][:2]
    enter, exit_ = free
    when = time.strftime("%Y-%m-%d")
    lines = [
        put(docs, to, "exits", {"number": exit_, "box": [arrive[0], arrive[1], arrive[0] + 1, arrive[1] + 1],
                                "faces": [0, 0], "note": f"Added in the studio {when}. {to.capitalize()}, from "
                                f"{world.capitalize()}: on the arrival tile until it is moved"}, who),
        put(docs, world, "enters", {"number": enter, "box": [column, row, column + 1, row + 1], "level": 0,
                                    "target": exit_, "note": f"Added in the studio {when}. "
                                    f"{world.capitalize()} to {to.capitalize()}"}, who),
    ]
    return {"enter": enter, "exit": exit_, "lines": lines}


def remove(world, number, who="mu"):
    docs = manifests()
    if world not in docs:
        raise NotFound(f"no world {world}")
    if number in named_in_code():
        raise NotFound(f"gate {number} is named in the code (a k...Gate constant): not taken out here")
    everything = list(rows(docs))
    mine = [(w, k, i, r) for w, k, i, r in everything if w == world and r["number"] == number]
    if not mine:
        raise NotFound(f"no gate {number} in {world}")
    _w, kind, _i, gate = mine[0]
    going = list(mine)
    if kind == "enters":
        others = [r for w, k, i, r in everything if k == "enters" and r["target"] == gate["target"]
                  and r is not gate]
        if not others:
            going += [(w, k, i, r) for w, k, i, r in everything if k == "exits" and r["number"] == gate["target"]]
    else:
        going += [(w, k, i, r) for w, k, i, r in everything if k == "enters" and r["target"] == number]
    for _w, _k, _i, r in going:
        if r["number"] in named_in_code():
            raise NotFound(f"its pair, gate {r['number']}, is named in the code: not taken out here")
    # The highest index of a list first, so the ones before it keep theirs; a list emptied goes
    # whole, key and all, as `put` made it.
    going.sort(key=lambda g: (g[0], g[1], -g[2]))
    lines, emptied = [], set()
    for w, k, i, _r in going:
        if (w, k) in emptied:
            continue
        if sum(1 for g in going if g[:2] == (w, k)) == len(docs[w][1]["gates"][k]):
            emptied.add((w, k))
            lines.append(edit.remove(docs[w][0], ["gates", k], who))
        else:
            lines.append(edit.remove(docs[w][0], ["gates", k, i], who))
    return {"removed": [f"{w} {k[:-1]} {r['number']}" for w, k, _i, r in going], "lines": lines}
