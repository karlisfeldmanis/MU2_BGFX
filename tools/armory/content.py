"""What is data, not rule: the release's own content/ read as it stands in the snapshot.

Items (content/items/*/*.json `stats`), monsters (content/monsters/*.json `combat`, `rules`,
`monster`), worlds (content/world/<w>/world.json, nests.json, <w>.json) and townsfolk (world.json
`npcs`, content/npc/<figure>.json). Nothing here decides a chance, a price or a service: those
are C++ rules and come from the dump (tools/armory/dump.py).

Ported from the prototype's gen_db.py, gen_monsters.py and gen_npcs.py, without their hand lists:
no release commits typed in (history.py dates every change), no DORMANT/kanturu list (a world is
in the release when its pack is; `live` is the dump's), no shop roles typed per shelf.
"""
import re
from collections import defaultdict
from pathlib import Path

from common import load, plain

CLASS = {"knight": "DK", "wizard": "DW", "elf": "FE", "gladiator": "MG", "lord": "DL"}
SLOT = {2: "Helms", 3: "Armour", 4: "Pants", 5: "Gloves", 6: "Boots"}
WEAPON = {"Sword": "Swords", "Axe": "Axes", "Mace": "Maces", "Spear": "Spears", "Bow": "Bows",
          "Staff": "Staffs", "Arrows": "Ammo"}
# content/items/misc: the key's family (Book01, Potion03, ...) names its shelf on the list page.
MISC = {"Book": "Scrolls", "Scroll": "Scrolls", "Potion": "Potions", "Antidote": "Potions",
        "Ale": "Potions", "Jewel": "Jewels", "Gem": "Orbs", "Orb": "Orbs", "MagicBox": "Boxes",
        "Quest": "Quest items", "EventDevilSquare": "Event items", "EventBloodCastle": "Event items"}


def _family(key):
    m = re.match(r"[A-Za-z]+?(?=\d|$)", key)
    return m.group(0) if m else key


def category(folder, key, doc, st):
    name = doc.get("mu_name", "")
    if folder == "weapons":
        fam = re.match(r"[A-Za-z]+", key).group(0)
        sub = WEAPON.get(fam, fam)
        if fam == "Bow" and "crossbow" in name.lower():
            sub = "Crossbows"
        return "Weapons", sub
    if folder == "armor":
        return "Armour", SLOT.get(st.get("slot"), "Armour")
    if folder == "shields":
        return "Shields", "Shields"
    if folder == "wings":
        return "Wings", "Wings"
    if folder == "pets":
        return "Pets", "Pets"
    if folder == "jewellery":
        return "Jewellery", "Rings" if "Ring" in key else "Pendants"
    fam = _family(key)
    for prefix, sub in MISC.items():
        if fam.startswith(prefix):
            return "Other", sub
    return "Other", "Other"


def items(src):
    """key -> {key, name, folder, file, doc, stats, cat, sub, ...}: every item with stats."""
    out = {}
    for p in sorted((src / "content/items").glob("*/*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load(p)
        st = d.get("stats")
        if "item" not in d or not st:
            continue
        key = d["item"]
        folder = p.parent.name
        cat, sub = category(folder, key, d, st)
        rq = st.get("requires") or {}
        stats = plain(st)
        classes = [CLASS[c] for c in st.get("classes", []) if c in CLASS]
        out[key] = {
            "key": key, "name": d.get("mu_name") or key, "folder": folder, "file": p, "doc": d,
            "cat": cat, "sub": sub, "group": st.get("group"), "number": st.get("number"),
            "classes": classes, "two_handed": bool(d.get("two_handed")), "requires": plain(rq),
            "stats": stats,
        }
    return out


def item_row(it):
    """The list page's row (prototype db.json's short names, so the design's logic reads it)."""
    st, rq = it["stats"], it["requires"]
    r = {"k": it["key"], "n": it["name"], "c": it["cat"], "s": it["sub"],
         "lv": rq.get("level", 0), "dl": st.get("drop_level", 0),
         "str": rq.get("strength", 0), "agi": rq.get("agility", 0), "ene": rq.get("energy", 0),
         "cls": it["classes"], "g": it["group"], "num": it["number"],
         "mob": 1 if st.get("drops_from_monsters") else 0}
    if "minimum_damage" in st:
        r["dmg"] = [st["minimum_damage"], st["maximum_damage"]]
    if "magic_power" in st:
        r["mp"] = st["magic_power"]
    if "attack_speed" in st and it["cat"] == "Weapons":
        r["sp"] = st["attack_speed"]
    if "defense" in st:
        r["def"] = st["defense"]
    if it["two_handed"]:
        r["two"] = 1
    if st.get("durability"):
        r["dur"] = st["durability"]
    if st.get("width"):
        r["sz"] = [st["width"], st.get("height", 1)]
    if st.get("defense_rate"):
        r["dr"] = st["defense_rate"]
    if (st.get("class_level") or 1) >= 2:
        r["cl2"] = 1
    if "resistance" in st:
        r["res"] = st["resistance"]
    return r


def worlds(src, packs):
    """folder -> {world, name, map, rules, npcs, nests, doc, chart_doc, in_release}."""
    out = {}
    for w in sorted((src / "content/world").glob("*/world.json")):
        folder = w.parent.name
        doc = load(w)
        nests = []
        n = w.parent / "nests.json"
        if n.exists():
            nests = load(n).get("nests") or []
        out[folder] = {"world": folder, "name": doc.get("name", folder.title()), "map": doc.get("map"),
                       "rules": plain(doc.get("rules") or {}), "npcs": doc.get("npcs") or [],
                       "nests": nests, "dir": w.parent,
                       "in_release": f"world-{folder}.mupak" in packs}
    return out


def monsters(src):
    """key -> monster: every breed with a combat level."""
    out = {}
    for p in sorted((src / "content/monsters").glob("*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load(p)
        c = d.get("combat") or {}
        if "level" not in c or "item" not in d:
            continue
        rules = d.get("rules") or {}
        out[d["item"]] = {"key": d["item"], "name": d.get("mu_name") or d["item"], "number": d.get("object_type"),
                          "world": d.get("world"), "file": p, "doc": d, "combat": plain(c),
                          "drops_rule": plain(rules.get("drops") or {}), "resist": plain(rules.get("resist") or {}),
                          "scale": float((d.get("monster") or {}).get("scale", 1.0) or 1.0)}
    return out


def nests_by_breed(worlds_):
    """breed number -> [{world, map, count, boxes}], from every world's nests.json."""
    where = defaultdict(dict)
    for w, wd in worlds_.items():
        for n in wd["nests"]:
            e = where[n["breed"]].setdefault(w, {"world": w, "map": wd["name"], "count": 0, "boxes": []})
            e["count"] += n.get("count", 1)
            if n.get("box"):
                e["boxes"].append(n["box"])
    return {b: sorted(v.values(), key=lambda e: -e["count"]) for b, v in where.items()}


def townsfolk(src, worlds_):
    """name -> {name, npc, figure, where:[{world, map, x, y, look}]}: every placed townsperson.
    Merged by name (Potion Girl Amy stands in several towns: one page, many places)."""
    docs = {}
    for p in sorted((src / "content/npc").glob("*.json")):
        if p.name.endswith(".rig.json"):
            continue
        d = load(p)
        for k in (d.get("mu_name", ""), d.get("item", "")):
            if k:
                docs.setdefault(k.lower(), d)
    out = {}
    for w, wd in worlds_.items():
        # A table entry with no figure is stood by the world's own figure placement on its tile
        # (src/game/play_open.cpp: "Hanzo is Smith01, Pasi Wizard01 and Baz Storage01 in the
        # town's own placements", matched by tile to the table's blank entries).
        placed = {}
        pf = wd["dir"] / "placements.json"
        if pf.exists():
            for spot in load(pf).get("spawns") or []:
                tile = spot.get("tile")
                if spot.get("model") and isinstance(tile, list) and len(tile) == 2:
                    placed.setdefault((int(tile[0]), int(tile[1])), spot["model"])
        for one in wd["npcs"]:
            figure = one.get("figure") or ""
            at = one.get("at") or [None, None]
            if not figure and at[0] is not None:
                figure = placed.get((int(at[0]), int(at[1])), "")
            e = out.setdefault(one["name"], {"name": one["name"], "npc": int(one["npc"]),
                                             "figure": figure, "where": []})
            if not e["figure"] and figure:
                e["figure"] = figure
            e["where"].append({"world": w, "map": wd["name"], "x": at[0], "y": at[1], "look": one.get("look")})
    for e in out.values():
        if not e["figure"]:
            d = docs.get(e["name"].lower())
            if d:
                e["figure"] = d["item"]
    return out


def voices(src):
    """voice folder -> the dialog pages it has read aloud (offer, underway, handin, resting)."""
    out = {}
    base = src / "content/voice"
    if not base.is_dir():
        return out
    for d in sorted(base.iterdir()):
        if not d.is_dir() or d.name == "ref":
            continue
        pages = sorted(p.stem[len(d.name) + 1:] for p in d.glob(f"{d.name}_*.wav"))
        if pages:
            out[d.name] = pages
    return out


def packs_of(data_dir):
    return {p.name for p in Path(data_dir).glob("*.mupak")}
