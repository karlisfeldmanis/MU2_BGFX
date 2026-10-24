#!/usr/bin/env python3
"""What each release changed, per entity: history/<kind>/<slug>.json, never written by hand.

    python3 tools/armory/history.py <this out dir> [--previous COMMIT]

Compares this release's built output with the previous release's (out/armory/<previous>/, found by
meta.json's `previous`, or rebuilt when named with --previous and not built yet) field by field:
stats, card rows, drop chances, prices, rates, anything an entity's file carries. Each entity that
changed gets one entry for this release, {commit, protocol, built, changes: [{field, was, now}]},
put in front of the entries the previous release's history had for it; a new entity's entry says
`added`. history/releases.json lists the releases with their optional hand note, read from
tools/armory/notes/<commit>.md (the "why"; the diff itself is always computed).

Two kinds may share a slug (an item and a townsperson), so history is kept per kind folder:
history/items/<slug>.json, history/monsters/<slug>.json, ...
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import ARMORY, HERE, load, write  # noqa: E402

# Not content: where the file is, what it is called on disk, the build's own stamp.
IGNORE = {"commit", "protocol", "thumb", "elite_thumb", "card", "chart", "icon_png", "icons", "t", "built"}
# Fields that name an element of a list, so a list of drops or nests is compared element by element
# whatever its order.
IDENT = ("monster_slug", "slug", "key", "id", "item", "world", "map", "place", "castle", "square", "elite", "state", "label")
NOTES = HERE / "notes"


def ident(e):
    parts = [f"{k}={e[k]}" for k in IDENT if isinstance(e, dict) and k in e and not isinstance(e[k], (dict, list))]
    return ",".join(parts) if parts else None


def flatten(v, path="", out=None):
    if out is None:
        out = {}
    if isinstance(v, dict):
        for k, x in v.items():
            if k in IGNORE and path == "":
                continue
            flatten(x, f"{path}.{k}" if path else k, out)
    elif isinstance(v, list):
        if v and all(isinstance(x, dict) for x in v):
            ids = [ident(x) for x in v]
            if all(ids) and len(set(ids)) == len(ids):
                for i, x in zip(ids, v):
                    flatten(x, f"{path}[{i}]", out)
                return out
        if all(not isinstance(x, (dict, list)) for x in v):
            out[path] = v
            return out
        for i, x in enumerate(v):
            flatten(x, f"{path}[{i}]", out)
    else:
        out[path] = v
    return out


def diff(old, new):
    a, b = flatten(old), flatten(new)
    out = []
    for k in sorted(set(a) | set(b)):
        if a.get(k, None) != b.get(k, None) or (k in a) != (k in b):
            ch = {"field": k}
            if k in a:
                ch["was"] = a[k]
            if k in b:
                ch["now"] = b[k]
            out.append(ch)
    return out


def entities(folder):
    """(kind, slug) -> the entity, with an item's card rows folded in as `card_rows`."""
    out = {}
    for idx in sorted((folder / "index").glob("*.json")):
        kind = idx.stem
        for f in sorted((folder / kind).glob("*.json")):
            e = load(f)
            if kind == "items":
                c = folder / "cards" / f.name
                if c.exists():
                    e["card_rows"] = load(c).get("cards")
            out[(kind, f.stem)] = e
    return out


def run(this, previous=None, say=print):
    this = Path(this)
    meta = load(this / "meta.json")
    prev = previous or meta.get("previous")
    if not prev:
        say("history: no release before this one is built; every entity starts its history here")
    prev_dir = (this.parent / prev) if prev else None
    if prev and not (prev_dir / "meta.json").exists():
        import build
        say(f"history: the release before ({prev}) is not built; building it")
        ok, _ = build.main(["--commit", prev, "--out", str(this.parent)], say)
        if not ok:
            raise SystemExit(f"history: cannot build {prev}; its history is left out")
    now = entities(this)
    was = entities(prev_dir) if prev else {}
    stamp = {"commit": meta["commit"], "protocol": meta["protocol"]}
    entry_base = {"commit": meta["commit"], "protocol": meta["protocol"], "built": meta.get("built")}
    changed = added = 0
    things = []  # what this release added or changed, for the Armory's home page
    for (kind, slug), e in now.items():
        before = []
        if prev:
            h = prev_dir / "history" / kind / f"{slug}.json"
            if h.exists():
                before = load(h).get("releases") or []
        entry = None
        if prev and (kind, slug) not in was:
            entry = dict(entry_base, added=True)
            added += 1
            things.append({"kind": kind, "slug": slug, "name": e.get("name"), "added": True})
        elif prev:
            changes = diff(was[(kind, slug)], e)
            if changes:
                entry = dict(entry_base, changes=changes)
                changed += 1
                things.append({"kind": kind, "slug": slug, "name": e.get("name"), "fields": len(changes)})
        releases = ([entry] if entry else []) + before
        write(this / "history" / kind / f"{slug}.json",
              {"kind": kind, "slug": slug, "name": e.get("name"), "releases": releases}, stamp)
    gone = sorted(f"{k}/{s}" for (k, s) in was if (k, s) not in now)
    rel_before = load(prev_dir / "history" / "releases.json").get("releases", []) if prev and (prev_dir / "history" / "releases.json").exists() else []
    note = NOTES / f"{meta['commit']}.md"
    this_rel = dict(entry_base, previous=prev, changed=changed, added=added, removed=gone, things=things,
                    note=note.read_text().strip() if note.exists() else None)
    write(this / "history" / "releases.json", {"releases": [this_rel] + [r for r in rel_before if r.get("commit") != meta["commit"]]}, stamp)
    say(f"history: {changed} changed, {added} added, {len(gone)} gone since {prev or '(none)'}")
    return this_rel


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", nargs="?", help="the built release (out/armory/<commit>)")
    ap.add_argument("--previous", help="the release to compare with (meta.json's by default)")
    a = ap.parse_args()
    folder = Path(a.folder) if a.folder else max((p.parent for p in ARMORY.glob("*/meta.json")), key=lambda p: p.stat().st_mtime)
    run(folder, a.previous)


if __name__ == "__main__":
    main()
