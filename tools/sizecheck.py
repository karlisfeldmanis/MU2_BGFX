#!/usr/bin/env python3
"""The size ratchet, enforced instead of reviewed.

docs/order-plan.md, sprint A (M7). The hub files -- play.cpp, realm.h, play.h,
desk.cpp, sim_test.cpp -- grow with every feature, because every feature has to register in
them. Between 2026-10-06 and 2026-10-11 the files over 1500 lines went from 8 to 17. The cost
lands on every reader: an agent pages a 3000-line file in chunks, needs a unique anchor in it
for an edit, and collides with the other session editing the same hub.

The rule:

    a .cpp, .py or .sh over 800 lines, or a header over 1000, fails --
    unless it is listed in tools/sizecheck.allow with its size on the day it was recorded.
    A listed file may shrink. It may never grow past its recorded size.

So a feature that must grow a hub moves something out of that hub first. That is the point:
the cure is a feature in its own file, registered into a list (order-plan §2), not a bigger hub.

Comments count. The house style runs them at 40-50% of the big headers and that is kept.

    tools/sizecheck.py            the check; exit 1 on a violation
    tools/sizecheck.py --record   rewrite the allow list from today's sizes, ONLY after a hub
                                  shrank (the check says when) -- never to let one grow
"""

import os
import sys

from paths import ROOT
ALLOW = os.path.join(ROOT, "tools", "sizecheck.allow")

# Where authored code lives. Vendored code (extern/) and the cook's output are not ours.
SCAN = ["src", "server/src", "tests", "tools", "pipeline"]
SKIP_DIRS = {"__pycache__", "MuExtract", "bin", "obj", "build", "scratch"}

LIMITS = {".cpp": 800, ".py": 800, ".sh": 800, ".h": 1000}

# Not held, by name, and each with the reason and the day it ends. tests/sim_test.cpp is where
# every feature's test lands, so holding it would refuse every feature until it is split into
# tests/sim/ (S7, order-plan §7); it is held again the day that lands.
EXEMPT = {"tests/sim_test.cpp"}


def lines_of(path):
    with open(path, "rb") as handle:
        return handle.read().count(b"\n")


def measure():
    sizes = {}
    for top in SCAN:
        base = os.path.join(ROOT, top)
        for folder, dirs, files in os.walk(base):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
            for name in sorted(files):
                ext = os.path.splitext(name)[1]
                if ext not in LIMITS:
                    continue
                path = os.path.join(folder, name)
                sizes[os.path.relpath(path, ROOT)] = lines_of(path)
    return sizes


def read_allow():
    allowed = {}
    if not os.path.exists(ALLOW):
        return allowed
    with open(ALLOW) as handle:
        for line in handle:
            line = line.split("#")[0].strip()
            if line:
                path, size = line.rsplit(None, 1)
                allowed[path] = int(size)
    return allowed


def over(path, size):
    return size > LIMITS[os.path.splitext(path)[1]]


def record(sizes, allowed):
    """Writes today's offenders. Refuses to raise a recorded size: the ratchet only turns one way."""
    raised = [p for p, s in sizes.items() if p in allowed and s > allowed[p] and p not in EXEMPT]
    if raised and allowed:
        print("sizecheck: --record refused, these grew past their recorded size:")
        for path in raised:
            print(f"  {path}: {sizes[path]} > {allowed[path]}")
        return 1
    offenders = {p: s for p, s in sizes.items() if over(p, s) and p not in EXEMPT}
    if allowed:
        fresh = [p for p in offenders if p not in allowed]
        if fresh:
            print("sizecheck: --record refused, these are new offenders and must be split instead:")
            for path in fresh:
                print(f"  {path}: {offenders[path]}")
            return 1
    with open(ALLOW, "w") as handle:
        handle.write("# The files allowed over sizecheck's limit, each at most at its size here.\n")
        handle.write("# Written by `tools/sizecheck.py --record`; may only shrink. See tools/sizecheck.py.\n")
        for path in sorted(offenders):
            handle.write(f"{path} {offenders[path]}\n")
    print(f"sizecheck: recorded {len(offenders)} files over the limit")
    return 0


def main():
    sizes = measure()
    allowed = read_allow()
    if "--record" in sys.argv[1:]:
        return record(sizes, allowed)

    violations, shrunk = [], []
    for path, size in sorted(sizes.items()):
        if path in EXEMPT:
            continue
        if path in allowed:
            if size > allowed[path]:
                violations.append(f"{path}: {size} lines, grew past its recorded {allowed[path]} "
                                  f"-- move something out of it first")
            elif size < allowed[path]:
                shrunk.append(path)
        elif over(path, size):
            limit = LIMITS[os.path.splitext(path)[1]]
            violations.append(f"{path}: {size} lines, over the {limit} limit -- split it")

    if violations:
        print(f"sizecheck: {len(violations)} violation(s) of the size ratchet (docs/order-plan.md)")
        for line in violations:
            print(f"  {line}")
        return 1
    note = f"; {len(shrunk)} shrank, tighten with --record" if shrunk else ""
    print(f"sizecheck: {len(sizes)} files, none grew{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
