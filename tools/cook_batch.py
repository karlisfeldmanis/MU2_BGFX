#!/usr/bin/env python3
"""Builds, cooks and shoots a list of things -- cook_one.py for many, in the time of a few.

    tools/cook_batch.py Sword01 Axe02 Shield05          build stale, cook, shoot
    tools/cook_batch.py --rebuild Sword01               rebuild from the sheets up
    tools/cook_batch.py --no-build Sword01              workshop/ as it is
    tools/cook_batch.py -j 6 ...                        builds at once (default: cores / 2)

Three phases, each as parallel as it is safe to be:

  1. build   tools/asset.sh per name, several at once. Each is its own Blender and its own
             folder under workshop/, and nothing two of them write is shared -- except a rig's
             actions, which only worn parts borrow, so a set or a figure is not for this.
  2. cook    tools/sync_one.py and cook_one's cook, ONE at a time: every item's entry goes
             into the same textures.json and wardrobe.json, and two writers would lose one.
  3. shoot   noon, dusk and night per item, three viewers at once; the viewer reads the
             tables phase 2 finished writing.

Every item lands in docs/cook-log.md awaiting a look, as cook_one leaves it. Logs of the
builds are in workshop/logs/.
"""

import argparse
import glob
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cook_one  # noqa: E402
from cook import ROOT  # noqa: E402

WORKSHOP = os.path.join(ROOT, "workshop")
LOGS = os.path.join(WORKSHOP, "logs")


def build(name, rebuild):
    os.makedirs(LOGS, exist_ok=True)
    started = time.time()
    with open(os.path.join(LOGS, f"{name}.log"), "w") as out:
        code = subprocess.run([os.path.join(ROOT, "tools", "asset.sh"), name]
                              + (["--rebuild"] if rebuild else []),
                              stdout=out, stderr=subprocess.STDOUT).returncode
    return name, code, time.time() - started


def built_glb(name):
    """workshop/<kind>/<category>/<Name>/<Name>.glb, relative to workshop/."""
    found = glob.glob(os.path.join(WORKSHOP, "*", "*", name, f"{name}.glb"))
    return os.path.relpath(found[0], WORKSHOP) if found else None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="+")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--no-shots", action="store_true")
    parser.add_argument("--world", default="lorencia")
    parser.add_argument("--texcook", default=os.path.join(ROOT, "build", "texcook"))
    args = parser.parse_args()
    failed = []
    began = time.time()

    names = list(args.names)
    if not args.no_build:
        print(f"== build: {len(names)} at {args.jobs} at once")
        with ThreadPoolExecutor(args.jobs) as pool:
            for name, code, seconds in pool.map(lambda n: build(n, args.rebuild), names):
                print(f"  {name:<14} {'ok' if code == 0 else 'FAILED'}  {seconds:5.1f} s")
                if code:
                    failed.append((name, f"build, see workshop/logs/{name}.log"))
        names = [n for n in names if n not in {f for f, _ in failed}]

    print("== cook")
    shots = []
    for name in names:
        rel = built_glb(name)
        if not rel:
            failed.append((name, "no .glb in workshop/"))
            continue
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "sync_one.py"), rel],
                       stdout=subprocess.DEVNULL)
        kind, area, meshes, extra = cook_one.resolve(name, args.world)
        if kind is None:
            failed.append((name, "not in assets/index.json -- run pipeline/index.py and sync"))
            continue
        if cook_one.cook_item(kind, area, meshes, extra, args.texcook) is None:
            failed.append((name, "cook"))
            continue
        cook_one.report(meshes)
        cook_one.log(name, kind=kind)
        shots.append((kind, name, extra if kind in ("set", "arm") else None))

    if shots and not args.no_shots:
        print(f"== shoot: {len(shots)}")
        with ThreadPoolExecutor(3) as pool:
            list(pool.map(lambda s: cook_one.shoot(s[0], s[1], s[2], args.world), shots))

    print(f"== {len(shots)} cooked in {time.time() - began:.0f} s, awaiting a look in "
          f"docs/cook-log.md")
    for name, why in failed:
        print(f"  FAILED {name}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
