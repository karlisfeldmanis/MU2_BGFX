#!/usr/bin/env python3
"""The Armory's build: out/armory/<commit>/ from the release players have (docs/plans/armory.md).

    ./mu armory build [--commit REF] [--out DIR] [--no-thumbs]
    python3 tools/armory/build.py ...          (the same)

1. The commit: the live release's (all three manifests agreeing) or --commit. Its tree is
   snapshotted into out/armory/src/<commit>/; the tables are dist/macos's packs, whose BUILT_FROM
   must be that commit (snapshot.py).
2. The dump (tools/armory/dump.py, the C++ that links the game): cards, skills, talents, gear
   powers, drops and rules. Every chance, price, service, reward and `live` flag is its.
3. The data (content.py): items, monsters, nests, worlds, townsfolk; charts (maps.py); models
   converted into the cache (models.py) and drawn as thumbnails (thumbs.py); icons (icons.py).
4. Merged per entity, so a page needs its own file and nothing else: an item's stats, card rows,
   where it drops and how often, its shops, crafts, quests and events; a monster's stats, nests
   and drops.

Output (every JSON stamped with commit and protocol):

    meta.json                  commit, protocol, built, previous, counts, what the dump gave
    index/<kind>.json          the list page's rows only
    <kind>/<slug>.json         one entity: everything its page shows
    cards/<slug>.json          an item's card rows (the dump's describe())
    thumbs/<hash>.webp         512 px, transparent; thumbs/<hash>.96.webp the card's picture
    icons/<hash>.png           skills and talents
    maps/<world>.png           the world charts
    search.json                name, kind, slug for everything

Slugs come from names and stay what the release before gave them (common.Slugs).

What the merge reads from the dump (out/armory/cache/dump/<commit>/<name>.json, each stamped):

    cards.json        {item key: <the card states and rows>} (or under "items")
    skills.json       [{id, name, ...}] (or under "skills")
    talents.json      {"classes": {class: {...}}} or any tree; every "icon" number gets its icon
    gear_powers.json  {item key: <powers>} (or under "items"), shown on the item's page
    drops.json        [{monster: key | number: breed, <place fields: world, map, castle, square,
                      elite, ...>, items: [{item: key, chance, ...}]}] (or under "monsters", or a
                      monster with "places": [...]); inverted onto every item's page
    rules.json        {maps|worlds: {world: {live, ...}}, npcs: [{npc: number, role, services,
                      shelf: [{item, price, ...}]}], and any other list (quests, mixes, runes,
                      elites, events, ...): each entry a page of its kind, and every item key it
                      names (item/k/key/result/reward fields) linked from that item's page,
                      "gives" where the field path says reward/paid/result/sell/shelf, else "needs"}

The dump's files are read loosely (see `Dump` below): each may be a list or a dict, keyed or under
its kind's name, so the merge does not break when a field is added. Where the dump has not run, its
outputs are missing, `meta.dump.missing` says which, and nothing is guessed in their place.
"""
import argparse
import json
import os
import shutil
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402
import content  # noqa: E402
import icons as icons_  # noqa: E402
import maps as maps_  # noqa: E402
import models  # noqa: E402
import snapshot  # noqa: E402
import thumbs as thumbs_  # noqa: E402
from common import ARMORY, Slugs, write  # noqa: E402

sys.path.append(str(common.ROOT / "tools" / "mu"))
import settings  # noqa: E402

DUMP_FILES = ("cards", "skills", "talents", "gear_powers", "drops", "rules")
# What an item's page keeps of each drop: of the dump's item entry, and of the place it drops in.
DROP_ROW = ("chance", "rolls", "excellent")
DROP_PLACE = ("place", "kind", "world", "level", "elite", "castle", "square")


# ---------------------------------------------------------------- the dump


def run_dump(src, data, commit, protocol, say):
    """tools/armory/dump.py run(); its outputs into out/armory/cache/dump/<commit>/.
    Returns (dir, missing names). Until dump.py exists the outputs are missing, said loudly."""
    out = common.CACHE / "dump" / commit
    try:
        import dump  # tools/armory/dump.py, the other half of the build
    except ImportError:
        dump = None
    if dump is not None and not hasattr(dump, "run"):
        dump = None
    if dump is None:
        say("DUMP MISSING: tools/armory/dump.py is not there yet; cards, skills, talents, gear powers, "
            "drops and rules are left out (no fallback guesses them)")
    else:
        # A dump that fails leaves nothing behind: no earlier run's files stand in for this one's.
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True, exist_ok=True)
        try:
            dump.run(snapshot_dir=src, build_dir=ARMORY / "build-dump", data_dir=data, out_dir=out,
                     commit=commit, protocol=protocol)
        except Exception as failed:  # noqa: BLE001 - said, and the dump's files left out
            tail = [line for line in str(failed).splitlines() if "error" in line.lower()][:8]
            say("DUMP FAILED: " + (str(failed).splitlines() or ["?"])[0][:200])
            for line in tail:
                say("  " + line[:300])
            shutil.rmtree(out, ignore_errors=True)
            out.mkdir(parents=True, exist_ok=True)
    missing = [n for n in DUMP_FILES if not (out / f"{n}.json").exists()]
    if missing and dump is not None:
        say(f"the dump left out {', '.join(missing)}")
    return out, missing


class Dump:
    """The dump's files, read loosely."""

    def __init__(self, folder, missing):
        self.d = {}
        for n in DUMP_FILES:
            if n not in missing:
                self.d[n] = common.load(Path(folder) / f"{n}.json")

    def has(self, name):
        return name in self.d

    def body(self, name):
        """The file without its stamp."""
        v = self.d.get(name)
        if isinstance(v, dict):
            return {k: x for k, x in v.items() if k not in ("commit", "protocol", "_note", "notes")}
        return v

    def listed(self, name, inner=None):
        """A list of entries: the file itself, its `inner` (or own-named) list, or a dict's values
        (each given its key as `key` when it has none)."""
        v = self.body(name)
        if v is None:
            return []
        if isinstance(v, dict):
            for k in (inner, name):
                if k and isinstance(v.get(k), (list, dict)):
                    v = v[k]
                    break
        if isinstance(v, list):
            return v
        if isinstance(v, dict):
            out = []
            for k, x in v.items():
                if isinstance(x, dict):
                    out.append({"key": k, **x} if "key" not in x else x)
            return out
        return []


def name_of(e):
    for k in ("name", "title", "n", "label", "mu_name"):
        if isinstance(e.get(k), str) and e[k]:
            return e[k]
    return str(e.get("key") or e.get("id") or "")


def key_of(e):
    for k in ("key", "id", "item", "slug"):
        if e.get(k) is not None:
            return str(e[k])
    return name_of(e)


def item_refs(tree, items, path=""):
    """Every item key named anywhere in a dump entry: [(key, field path)]."""
    found = []
    if isinstance(tree, dict):
        for k, v in tree.items():
            p = f"{path}.{k}" if path else k
            if isinstance(v, str) and v in items and k in ("item", "k", "key", "result", "reward", "gives", "needs"):
                found.append((v, p))
            else:
                found.extend(item_refs(v, items, p))
    elif isinstance(tree, list):
        for i, v in enumerate(tree):
            found.extend(item_refs(v, items, path))
    return found


def made_by_words(success, items):
    """The items a mix makes, from the words it says it makes when it names no item key (the Chaos
    Machine's Dinorant, Invisibility Cloak, Devil's Invitation and Chaos Weapon recipes): each
    part of "X, Y or Z" without its "A " and its " +0 to +4" is an item's own name, or "<Word>
    weapon", every weapon whose name starts with that word. A stopgap until the dump says the keys."""
    import re
    out = []
    for part in re.split(r",\s*|\s+or\s+", str(success or "")):
        part = re.sub(r"^(an?|the)\s+", "", part.strip(), flags=re.I)
        part = re.sub(r"\s*\+\d+(\s+to\s+\+\d+)?$", "", part).strip()
        if not part:
            continue
        named = [k for k, v in items.items() if v["name"].lower() == part.lower()]
        if not named:
            m = re.fullmatch(r"(\w+) weapon", part, re.I)
            if m:
                named = [k for k, v in items.items() if v.get("cat") == "Weapons"
                         and v["name"].lower().startswith(m.group(1).lower() + " ")]
        out += named
    return out


GIVES = ("reward", "paid", "result", "sell", "shelf", "stock", "gives", "win", "hoard", "out")


def role(path):
    p = path.lower()
    return "gives" if any(w in p for w in GIVES) else "needs"


# ---------------------------------------------------------------- the build


def previous_slugs(prev_dir):
    out = {}
    if not prev_dir:
        return out
    for f in (prev_dir / "index").glob("*.json"):
        rows = common.load(f).get("rows") or []
        out[f.stem] = {str(r["k"]): r["slug"] for r in rows if "k" in r and "slug" in r}
    return out


def build(commit=None, out=None, thumbs=True, say=print):
    started = time.time()
    commit = snapshot.short(commit) if commit else snapshot.live_commit()
    protocol = snapshot.protocol(commit)
    data = snapshot.data_dir(commit)
    src = snapshot.snapshot(commit, say)
    stamp = {"commit": commit, "protocol": protocol}
    out_root = Path(out) if out else ARMORY
    final = out_root / commit
    dest = out_root / f"{commit}.new"
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    prev = snapshot.released_before(commit) if out_root == ARMORY else None
    prev_dir = (out_root / prev) if prev else None
    say(f"the Armory of {commit} (protocol {protocol})" + (f", after {prev}" if prev else ", the first"))
    slugs = Slugs(previous_slugs(prev_dir))
    cores = min(settings.cores()[0], 6)
    notes = []

    dump_dir, missing = run_dump(src, data, commit, protocol, say)
    D = Dump(dump_dir, missing)

    # ---- data
    packs = content.packs_of(data)
    I = content.items(src)
    M = content.monsters(src)
    W = content.worlds(src, packs)
    F = content.townsfolk(src, W)
    V = content.voices(src)
    nests = content.nests_by_breed(W)
    by_number = {m["number"]: k for k, m in M.items()}
    say(f"content: {len(I)} items, {len(M)} monsters, {len(W)} worlds, {len(F)} townsfolk")

    item_slug = slugs.assign("items", [(k, v["name"]) for k, v in I.items()])
    mon_slug = slugs.assign("monsters", [(k, v["name"]) for k, v in M.items()])
    npc_slug = slugs.assign("npcs", [(k, k) for k in F])
    world_slug = slugs.assign("worlds", [(k, v["name"]) for k, v in W.items()])

    # ---- the dump's rules: which worlds are live, what each thing feeds
    rules = D.body("rules") if D.has("rules") else {}
    live_worlds = None
    for k in ("maps", "worlds"):
        v = (rules or {}).get(k)
        if isinstance(v, dict):
            live_worlds = {w: bool(x.get("live")) if isinstance(x, dict) else bool(x) for w, x in v.items()}
        elif isinstance(v, list):
            live_worlds = {str(x.get("world") or x.get("name")): bool(x.get("live")) for x in v if isinstance(x, dict)}
        if live_worlds is not None:
            break

    # ---- drops: per monster per place, inverted per item
    drops_by_mon = defaultdict(list)
    drops_by_item = defaultdict(list)
    elite_breeds = set()
    for e in D.listed("drops", "monsters"):
        mk = e.get("monster") if e.get("monster") in M else by_number.get(e.get("number", e.get("monster")))
        if mk is None and e.get("key") in M:
            mk = e["key"]
        if mk is None:
            notes.append(f"drops: an entry names no monster this release has ({str(e)[:80]})")
            continue
        places = e.get("places") if isinstance(e.get("places"), list) else [e]
        for pl in places:
            place = {k: v for k, v in pl.items() if k not in ("items", "monster", "number", "key", "places")}
            if pl.get("elite"):
                elite_breeds.add(mk)
            rows = []
            for it in pl.get("items") or []:
                ik = it.get("item") or it.get("k") or it.get("key")
                if ik not in I:
                    continue
                row = {**it, "item": ik, "slug": item_slug[ik], "n": I[ik]["name"]}
                rows.append(row)
                # An item's page shows who drops it, where and how often: the place's own details
                # (its rules, notes, money, quest treasure) are the monster's and the event's page's.
                drops_by_item[ik].append({**{k: it[k] for k in DROP_ROW if k in it},
                                          "monster_slug": mon_slug[mk], "monster_name": M[mk]["name"],
                                          **{k: place[k] for k in DROP_PLACE if k in place}})
            drops_by_mon[mk].append({**place, "items": sorted(rows, key=lambda r: -(r.get("chance") or 0))})

    # ---- the rules' kinds published as their own pages, and their item references inverted
    refs = defaultdict(lambda: defaultdict(list))   # item -> kind -> [{slug, name, role}]
    kinds = {}                                       # kind -> {key: (slug, entry)}
    npc_rules = {}
    if rules:
        for kind, v in rules.items():
            if kind in ("maps", "worlds") or not isinstance(v, (list, dict)):
                continue
            entries = D.listed("rules", kind) if isinstance(v, (list, dict)) else []
            entries = [x for x in entries if isinstance(x, dict) and name_of(x)]
            if not entries:
                continue
            if kind == "npcs":
                for x in entries:
                    num = x.get("npc", x.get("number"))
                    if num is not None:
                        npc_rules[int(num)] = x
                continue
            sl = slugs.assign(kind, [(key_of(x), name_of(x)) for x in entries])
            kinds[kind] = {key_of(x): (sl[key_of(x)], x) for x in entries}
            for k, (s, x) in kinds[kind].items():
                found = item_refs(x, I)
                if kind == "mixes" and not any(role(p) == "gives" for _, p in found):
                    found += [(ik, "result") for ik in made_by_words(x.get("success"), I)]
                for ik, path in found:
                    r = {"kind": kind, "slug": s, "name": name_of(x), "role": role(path)}
                    if r not in refs[ik][kind]:
                        refs[ik][kind].append(r)
    for num, x in npc_rules.items():
        for name, e in F.items():
            if e["npc"] == num:
                for ik, path in item_refs(x, I):
                    r = {"kind": "npcs", "slug": npc_slug[name], "name": name, "role": role(path)}
                    if r not in refs[ik]["shops" if role(path) == "gives" else "npcs"]:
                        refs[ik]["shops" if role(path) == "gives" else "npcs"].append(r)

    # ---- models and thumbnails
    figures = sorted({e["figure"] for e in F.values() if e["figure"]})
    thumb = {}
    if thumbs:
        jobs, mnotes = models.plan(src, I, M, figures)
        notes.extend(mnotes)
        glbs, cnotes = models.convert(jobs, cores, say)
        notes.extend(sorted(set(cnotes)))
        wanted = [(jid, jid, "item" if jid.startswith("item:") else "figure", False) for jid in glbs]
        wanted += [(f"elite:monster:{k}", f"monster:{k}", "figure", True) for k in sorted(elite_breeds)]
        thumb = thumbs_.build(wanted, glbs, src, dest, say)
    else:
        say("thumbs: skipped (--no-thumbs)")

    def pics(tid):
        h = thumb.get(tid)
        return {"page": f"thumbs/{h}.webp", "card": f"thumbs/{h}.96.webp"} if h else None

    charts = maps_.build(src, W, dest, say)
    icon = icons_.build(src, D.body("talents") if D.has("talents") else None, dest, say)

    counts, search = {}, []

    def publish(kind, rows, entities):
        """index/<kind>.json and <kind>/<slug>.json."""
        write(dest / "index" / f"{kind}.json", {"kind": kind, "rows": rows}, stamp)
        for s, e in entities:
            write(dest / kind / f"{s}.json", e, stamp)
        counts[kind] = len(rows)

    def is_live(world):
        if live_worlds is None:
            return None
        return live_worlds.get(world)

    # ---- items
    cards = D.body("cards") if D.has("cards") else None
    if isinstance(cards, dict) and isinstance(cards.get("items"), dict):
        cards = cards["items"]
    powers = D.body("gear_powers") if D.has("gear_powers") else None
    if isinstance(powers, dict) and isinstance(powers.get("items"), dict):
        powers = powers["items"]
    rows, ents = [], []
    for k, it in sorted(I.items(), key=lambda kv: item_slug[kv[0]]):
        s = item_slug[k]
        row = content.item_row(it)
        row["slug"] = s
        p = pics(f"item:{k}")
        if p:
            row["t"] = p["card"]
        src_flags = ""
        gives = {kind: [r for r in v if r["role"] == "gives"] for kind, v in refs[k].items()}
        for flag, have in (("d", [d for d in drops_by_item.get(k, []) if (d.get("chance") or 0) > 0]),
                           ("s", gives.get("shops")), ("c", gives.get("mixes")), ("q", gives.get("quests")),
                           ("e", gives.get("events")), ("b", gives.get("boxes"))):
            if have:
                src_flags += flag
        row["src"] = src_flags
        live = None
        if not missing or ("drops" not in missing and "rules" not in missing):
            # Live when anything in the release makes it: a drop that can happen, a shelf, a mix's
            # result, a box, a quest's or an event's reward. Being asked for makes nothing.
            live = (any((d.get("chance") or 0) > 0 for d in drops_by_item.get(k, []))
                    or any(r["role"] == "gives" for v in refs[k].values() for r in v))
        row["live"] = live
        rows.append(row)
        e = {"kind": "item", "key": k, "slug": s, "name": it["name"], "category": it["cat"], "sub": it["sub"],
             "classes": it["classes"], "two_handed": it["two_handed"], "stats": it["stats"], "live": live,
             "thumb": p, "card": f"cards/{s}.json" if isinstance(cards, dict) and k in cards else None,
             "drops": sorted(drops_by_item.get(k, []), key=lambda d: -(d.get("chance") or 0)),
             "from": {kind: v for kind, v in refs[k].items()}}
        if isinstance(powers, dict) and k in powers:
            e["powers"] = powers[k]
        ents.append((s, e))
        if isinstance(cards, dict) and k in cards:
            write(dest / "cards" / f"{s}.json", {"item": k, "slug": s, "name": it["name"], "cards": cards[k]}, stamp)
        search.append({"n": it["name"], "k": "item", "s": s})
    publish("items", rows, ents)

    # ---- monsters
    rows, ents = [], []
    for k, m in sorted(M.items(), key=lambda kv: mon_slug[kv[0]]):
        s = mon_slug[k]
        c = m["combat"]
        where = [dict(n, world_slug=world_slug[n["world"]], live=is_live(n["world"])) for n in nests.get(m["number"], [])]
        live = None if live_worlds is None and "drops" in missing else (
            any(w["live"] for w in where) or bool(drops_by_mon.get(k)))
        p = pics(f"monster:{k}")
        row = {"k": k, "slug": s, "n": m["name"], "num": m["number"], "lv": c.get("level"), "hp": c.get("health"),
               "dmg": [c.get("minimum_damage"), c.get("maximum_damage")], "def": c.get("defense"),
               "home": W[m["world"]]["name"] if m["world"] in W else m["world"],
               "maps": [w["map"] for w in where], "live": live}
        if p:
            row["t"] = p["card"]
        rows.append(row)
        e = {"kind": "monster", "key": k, "slug": s, "name": m["name"], "number": m["number"], "combat": c,
             "resist": m["resist"], "drop_rules": m["drops_rule"], "scale": m["scale"],
             "home": m["world"], "nests": where, "live": live, "thumb": p,
             "elite_thumb": pics(f"elite:monster:{k}"), "drops": drops_by_mon.get(k, [])}
        ents.append((s, e))
        search.append({"n": m["name"], "k": "monster", "s": s})
    publish("monsters", rows, ents)

    # ---- townsfolk
    rows, ents = [], []
    for name, f in sorted(F.items(), key=lambda kv: npc_slug[kv[0]]):
        s = npc_slug[name]
        where = [dict(w, world_slug=world_slug[w["world"]], live=is_live(w["world"])) for w in f["where"]]
        p = pics(f"npc:{f['figure']}") if f["figure"] else None
        r = npc_rules.get(f["npc"]) or {}
        row = {"k": name, "slug": s, "n": name, "npc": f["npc"], "maps": sorted({w["map"] for w in where}),
               "role": r.get("role")}
        if p:
            row["t"] = p["card"]
        rows.append(row)
        rr = {k: v for k, v in r.items() if k not in ("npc", "number", "name")}
        ents.append((s, {"kind": "npc", "key": name, "slug": s, "name": name, "npc": f["npc"], "figure": f["figure"] or None,
                         "where": where, "thumb": p, **rr}))
        search.append({"n": name, "k": "npc", "s": s})
    publish("npcs", rows, ents)

    # ---- worlds
    rows, ents = [], []
    for w, wd in sorted(W.items(), key=lambda kv: world_slug[kv[0]]):
        s = world_slug[w]
        breeds = sorted({n["breed"] for n in wd["nests"]})
        mons = [{"slug": mon_slug[by_number[b]], "name": M[by_number[b]]["name"], "level": M[by_number[b]]["combat"].get("level"),
                 "count": sum(n.get("count", 1) for n in wd["nests"] if n["breed"] == b),
                 "boxes": [n["box"] for n in wd["nests"] if n["breed"] == b and n.get("box")]}
                for b in breeds if b in by_number]
        folk = [{"slug": npc_slug[n["name"]], "name": n["name"], "x": (n.get("at") or [None])[0],
                 "y": (n.get("at") or [None, None])[1]} for n in wd["npcs"]]
        rows.append({"k": w, "slug": s, "n": wd["name"], "map": wd["map"], "in_release": wd["in_release"],
                     "live": is_live(w), "chart": (charts.get(w) or {}).get("png"), "monsters": len(mons)})
        ents.append((s, {"kind": "world", "key": w, "slug": s, "name": wd["name"], "map": wd["map"],
                         "in_release": wd["in_release"], "live": is_live(w), "rules": wd["rules"],
                         "chart": charts.get(w), "monsters": mons, "npcs": folk}))
        search.append({"n": wd["name"], "k": "world", "s": s})
    publish("worlds", rows, ents)

    # ---- the dump's own kinds: skills, talents, and every list in rules (quests, mixes, runes ...)
    if D.has("skills"):
        sk = [x for x in D.listed("skills") if isinstance(x, dict)]
        sl = slugs.assign("skills", [(key_of(x), name_of(x)) for x in sk])
        rows, ents = [], []
        for x in sk:
            s = sl[key_of(x)]
            ic = icon["skills"].get(int(x["id"])) if str(x.get("id", "")).isdigit() else None
            rows.append({"k": key_of(x), "slug": s, "n": name_of(x), "icon": f"icons/{ic}.png" if ic else None})
            ents.append((s, {"kind": "skill", "slug": s, **x, "icon_png": f"icons/{ic}.png" if ic else None}))
            search.append({"n": name_of(x), "k": "skill", "s": s})
        publish("skills", rows, ents)
    if D.has("talents"):
        t = D.body("talents")
        t_icons = {str(n): f"icons/{h}.png" for n, h in icon["talents"].items()}
        classes = t.get("classes") if isinstance(t, dict) else None
        if isinstance(classes, dict):
            rows, ents = [], []
            for cls, body in classes.items():
                s = common.slug(cls)
                rows.append({"k": cls, "slug": s, "n": cls})
                ents.append((s, {"kind": "talents", "class": cls, "icons": t_icons, **(body if isinstance(body, dict) else {"tree": body})}))
            publish("talents", rows, ents)
        else:
            publish("talents", [{"k": "all", "slug": "all", "n": "Talents"}],
                    [("all", {"kind": "talents", "icons": t_icons, "tree": t})])
    for kind, entries in kinds.items():
        rows, ents = [], []
        for k, (s, x) in sorted(entries.items(), key=lambda kv: kv[1][0]):
            extra = {}
            if kind == "quests" and isinstance(x.get("voice"), str) and x["voice"] in V:
                extra["voice_pages"] = V[x["voice"]]
            row = {"k": k, "slug": s, "n": name_of(x)}
            for f in ("level", "map", "giver", "live", "rate", "category"):
                if f in x and not isinstance(x[f], (dict, list)):
                    row[f] = x[f]
            rows.append(row)
            ents.append((s, {"kind": kind, "slug": s, **x, **extra}))
            search.append({"n": name_of(x), "k": kind.rstrip("s"), "s": s})
        publish(kind, rows, ents)

    write(dest / "search.json", {"rows": sorted(search, key=lambda r: (r["n"].lower(), r["k"]))}, stamp)
    meta = {"built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "previous": prev,
            "counts": counts, "dump": {"dir": str(dump_dir.relative_to(common.ROOT)), "missing": missing},
            "names": "English (the game's own); not translated", "notes": notes[:500]}
    write(dest / "meta.json", meta, stamp)
    import history
    history.run(dest, say=say)

    shutil.rmtree(final, ignore_errors=True)
    os.replace(dest, final)
    sizes = summary(final)
    say(f"out/armory/{commit}/ in {time.time() - started:.0f} s: " +
        ", ".join(f"{k} {v['files']} files {v['bytes'] / 1e6:.1f} MB" for k, v in sizes.items()))
    if missing:
        say(f"waiting on the dump: {', '.join(missing)}")
    return final, {"commit": commit, "protocol": protocol, "previous": prev, "counts": counts, "sizes": sizes,
                   "missing": missing}


def summary(folder):
    out = {}
    for p in sorted(Path(folder).rglob("*")):
        if p.is_file():
            rel = p.relative_to(folder).parts
            k = rel[0] if len(rel) > 1 else "(top)"
            e = out.setdefault(k, {"files": 0, "bytes": 0})
            e["files"] += 1
            e["bytes"] += p.stat().st_size
    return out


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(prog="mu armory build", description=__doc__.split("\n\n")[0])
    ap.add_argument("--commit", help="the release to build (the live one by default)")
    ap.add_argument("--out", help="the folder the <commit>/ goes in (out/armory)")
    ap.add_argument("--no-thumbs", action="store_true", help="skip the models and thumbnails")
    a = ap.parse_args(argv)
    try:
        os.nice(10)
    except OSError:
        pass
    try:
        _, data = build(a.commit, a.out, not a.no_thumbs, say)
    except snapshot.Stop as stop:
        say(f"armory: {stop}")
        return False, {"error": str(stop)}
    return True, data


if __name__ == "__main__":
    sys.exit(0 if main()[0] else 1)
