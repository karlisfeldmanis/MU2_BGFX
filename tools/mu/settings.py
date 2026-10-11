"""How many cores mu may use, one number for every tool it runs.

The user: "we need probably also some setting how many cores to use" (2026-10-11), and before
that, more than once: never every core -- the game, another session's cook and Blender share
this machine. So one number, kept per machine (machines differ, so not in git):

    ./mu cores          what it is and where it came from
    ./mu cores 4        keep 4 in workshop/.mu/settings.json
    MU_CORES=2 ./mu ... for one run
    ./mu make X --cores 3

The first of those that is set wins: --cores, then MU_CORES, then the file, then 60% of the
machine's cores (6 of 10 on this Mac). It reaches every tool a step runs:

  - cook.py's texture compressor, by --threads;
  - Blender, which asset.sh starts as $BLENDER, by a wrapper that adds --threads;
  - torch and numpy in the upscaler and the pipeline, by OMP/MKL/VECLIB thread variables;
  - and every step is started under `nice`, so the game in front of you keeps its frame.
"""

import json
import os
import stat

from things import WORKSHOP

FILE = os.path.join(WORKSHOP, ".mu", "settings.json")
BLENDER = os.environ.get("BLENDER", "/Applications/Blender.app/Contents/MacOS/Blender")


def machine():
    return os.cpu_count() or 4


def default():
    return max(1, round(machine() * 0.6))


def read():
    try:
        with open(FILE) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def cores(flag=None):
    """(count, where it came from)."""
    if flag:
        return clamp(flag), "--cores"
    if os.environ.get("MU_CORES", "").isdigit():
        return clamp(int(os.environ["MU_CORES"])), "MU_CORES"
    kept = read().get("cores")
    if isinstance(kept, int):
        return clamp(kept), os.path.relpath(FILE, os.path.dirname(WORKSHOP))
    return default(), f"60% of this machine's {machine()}"


def clamp(n):
    return max(1, min(int(n), machine()))


def keep(n):
    os.makedirs(os.path.dirname(FILE), exist_ok=True)
    doc = read()
    doc["cores"] = clamp(n)
    with open(FILE, "w") as handle:
        json.dump(doc, handle, indent=1)
    return doc["cores"]


def environment(n):
    """The environment a step runs in: every thread pool held to n."""
    env = dict(os.environ)
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS",
                 "OPENBLAS_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
        env[name] = str(n)
    env["BLENDER"] = blender_wrapper(n)
    env["MU_CORES"] = str(n)
    return env


def blender_wrapper(n):
    """asset.sh runs $BLENDER; this one is Blender held to n threads."""
    path = os.path.join(WORKSHOP, ".mu", f"blender-{n}")
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(f'#!/bin/sh\nexec "{BLENDER}" --threads {n} "$@"\n')
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path
