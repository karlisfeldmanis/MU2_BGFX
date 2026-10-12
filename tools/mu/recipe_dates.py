"""When a monster recipe last changed, as each cook step reads it (tools/mu/make.py's mtime).

Out of make.py, which is at its size.
"""

import hashlib
import json
import os

from things import ROOT, WORKSHOP

# A monster recipe's MU checklist (tools/mu/research.py) is a record kept beside the recipe, not
# an input of any cook: ticking an item must not make the monster's build, the realm, every
# world's tables and the figure stale. So such a recipe is dated by when the rest of it last
# changed -- a digest of it without `mu_checklist`, and the time that digest was first seen,
# kept in workshop/.mu/recipe_times.json. A step names what else of a recipe it does not read
# (`without`): the Blender build reads neither `show` nor `rules`, and the realm not `show`, so
# a show's tweak cooks shows.json alone (2026-10-12, the Witch Queen: one number of the golem's
# light planned its build, every world's tables, the realm and a compile).
RECIPE_TIMES = os.path.join(WORKSHOP, ".mu", "recipe_times.json")
BUILD_IGNORES = ("mu_checklist", "show", "rules")
REALM_IGNORES = ("mu_checklist", "show")


def recipe_time(path, when, without=("mu_checklist",)):
    try:
        with open(path) as handle:
            text = handle.read()
        if not any(f'"{key}"' in text for key in without):
            return when
        doc = json.loads(text)
    except (OSError, ValueError):
        return when
    for key in without:
        doc.pop(key, None)
    digest = hashlib.sha1(json.dumps(doc, sort_keys=True).encode()).hexdigest()
    try:
        with open(RECIPE_TIMES) as handle:
            times = json.load(handle)
    except (OSError, ValueError):
        times = {}
    key = os.path.relpath(path, ROOT)
    if tuple(without) != ("mu_checklist",):
        key += " without " + ",".join(without)
    seen = times.get(key)
    if seen and seen[0] == digest:
        return min(when, seen[1])
    times[key] = [digest, when]
    os.makedirs(os.path.dirname(RECIPE_TIMES), exist_ok=True)
    scratch = RECIPE_TIMES + f".{os.getpid()}"
    with open(scratch, "w") as handle:
        json.dump(times, handle, indent=1, sort_keys=True)
    os.replace(scratch, RECIPE_TIMES)
    return when
