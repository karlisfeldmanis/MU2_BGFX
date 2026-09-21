#!/usr/bin/env python3
"""Copies one built model from workshop/ into assets/: its .glb and the files it names.

    tools/sync_one.py items/weapons/Sword01/Sword01.glb [more.glb ...]

sync.sh's rule 3 for a single model, and nothing else -- not index.json, not what the rest
of the workshop holds. For cooking one thing while something else in assets/ is being worked
on from another build.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sync import glb_json, glb_uris  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC, DST = os.path.join(ROOT, "workshop"), os.path.join(ROOT, "assets")

for rel in sys.argv[1:]:
    here = os.path.dirname(rel)
    wanted = [rel] + [os.path.normpath(os.path.join(here, u))
                      for u in glb_uris(glb_json(os.path.join(SRC, rel)))]
    for one in wanted:
        s, d = os.path.join(SRC, one), os.path.join(DST, one)
        if not os.path.isfile(s):
            print(f"  ! missing {one}")
            continue
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)
    print(f"{rel}: {len(wanted)} file(s)")
