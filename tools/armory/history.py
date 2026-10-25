#!/usr/bin/env python3
"""What each release changed, per entity and for the release: history/, never written by hand.

    python3 tools/armory/history.py <this out dir> [--previous COMMIT]

Compares this release's built output with the previous release's. The previous release is
out/armory/releases.json's (releases.py), never git ancestry. When it is not built yet it is built
here into its own folder, without thumbnails and without a history of its own
(ARMORY_NO_PREVIOUS), and marked SEED: web/deploy.sh never uploads it and nothing serves it. If
that build fails, this release's history starts here and this build goes on.

**What counts.** Only what a player reads, field by field per kind (WATCH): an item's numbers,
requirements, sell price, where it drops and what makes it; a monster's numbers where it is met
and what it drops there; a shop's shelf and prices; a quest's pay; a rune's or a skill's words and
numbers; a mix's rate and Zen; a box's contents. Layout, pictures and the dump's own bookkeeping
are not watched. A field the previous release did not have at all is the schema growing, not an
update, and is left out. A drop chance that moved by less than POOL_SHIFT of itself is its pool
changing size (an item joined or left the place's pool), not that drop changing: all such moves of
an entity are one "Drop chances shifted slightly" line, naming the items that joined the pool.

**What it writes.**

    history/<kind>/<slug>.json   {kind, slug, name, releases: [entry, ...newest first]}
        entry = {commit, protocol, date, added?: true,
                 changes: [{field, label, was, now, change}]}
        `was`/`now` are said for people ("1 in 263", "12,000", "3 s", the text); `change` is
        new | removed | changed | buffed | nerfed (buffed/nerfed only where the number says which
        is better for a player); `field` is the stable key a comment can name.
    history/releases.json        {releases: [{commit, protocol, date, previous, note,
                                   counts: {new, changed, removed, pool_shifts},
                                   things: {new: [...], changed: [...], removed: [...]}}]}
        a thing = {kind, slug, name, summary: ["Label: was → now", ...]}; the Armory's first page
        shows these as the release's notes. `note` is tools/armory/notes/<commit>.md, the "why".

Two kinds may share a slug (an item and a townsperson), so history is kept per kind folder.
"""
import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from common import ARMORY, HERE, load, write  # noqa: E402

NOTES = HERE / "notes"
# A chance that moved by less than this share of itself: its pool changed size.
POOL_SHIFT = 0.15
TICKS = 20  # the realm's ticks a second (sim: kCastleTicksPerSecond, Realm's Hz)

# What is watched, per kind: a dotted path into the entity -> (label, how it is said, which way is
# better for a player: +1 more is better, -1 less is better, 0 neither).
WATCH = {
    "items": {
        "stats.minimum_damage": ("Minimum damage", "n", 1), "stats.maximum_damage": ("Maximum damage", "n", 1),
        "stats.magic_power": ("Wizardry", "n", 1), "stats.defense": ("Defense", "n", 1),
        "stats.defense_rate": ("Defense rate", "n", 1), "stats.attack_speed": ("Attack speed", "n", 1),
        "stats.walk_speed": ("Walk speed", "n", 1), "stats.durability": ("Durability", "n", 1),
        "stats.drop_level": ("Drop level", "n", 0), "two_handed": ("Two-handed", "yes", 0),
        "classes": ("Classes", "list", 0), "live": ("In the game", "live", 0),
        "powers.needs.level": ("Level required", "n", -1), "powers.needs.strength": ("Strength required", "n", -1),
        "powers.needs.agility": ("Agility required", "n", -1), "powers.needs.energy": ("Energy required", "n", -1),
        "powers.needs.vitality": ("Vitality required", "n", -1), "powers.tells": ("Text", "text", 0),
        "sells": ("Sell price", "zen", 0),
    },
    "monsters": {"live": ("In the game", "live", 0)},
    "npcs": {"role": ("Role", "text", 0), "services": ("Services", "list", 0), "repairs": ("Repairs", "yes", 0)},
    "quests": {
        "title": ("Title", "text", 0), "minLevel": ("Level", "n", 0), "experience": ("Experience", "n", 1),
        "firstExperience": ("Experience the first time", "n", 1), "zen": ("Zen", "zen", 1),
        "repeatSeconds": ("Repeats every", "secs", -1), "natives": ("Classes", "list", 0),
        "strangers": ("Open to every class", "yes", 0), "boon": ("Boon", "text", 0),
    },
    "runes": {
        "tells": ("Rune text", "text", 0), "rarity_name": ("Rarity", "text", 0), "hero_level": ("Level to use", "n", -1),
        "from_level": ("Drops from monster level", "n", -1), "classes": ("Classes", "list", 0), "slots": ("Goes in", "list", 0),
        "once": ("One per item", "yes", 0), "stacking.stacks": ("Stacks", "n", 0), "stacking.says": ("Stacking", "text", 0),
        "sale": ("Sale share", "pct", 0), "live": ("In the game", "live", 0),
    },
    "skills": {
        "tells": ("Skill text", "text", 0), "mana": ("Mana", "n", -1), "coolTicks": ("Cooldown", "ticks", -1),
        "boonTicks": ("Lasts", "ticks", 1), "reach": ("Reach", "tiles", 1), "damage": ("Damage", "n", 1),
        "needLevel": ("Level", "n", -1), "splash": ("Splash", "n", 1), "classes": ("Classes", "list", 0),
        "spread": ("Shape", "text", 0), "built": ("In the game", "yes", 0),
    },
    "mixes": {
        "rate": ("Success rate", "pctn", 1), "zen": ("Zen", "zen", -1), "success": ("Makes", "text", 0),
        "failure": ("On failure", "text", 0), "title": ("Title", "text", 0), "listed": ("Listed", "yes", 0),
    },
    "boxes": {"zen": ("Zen instead", "pct", 0), "zen_amount": ("Zen", "zen", 1)},
    "elites": {
        "count": ("At once", "n", 0), "life": ("Life ×", "x", 0), "damage": ("Damage ×", "x", 0),
        "defense": ("Defense ×", "x", 0), "experience": ("Experience ×", "x", 1), "zen_times": ("Zen ×", "x", 1),
        "levels_up": ("Spoils levels higher", "n", 1), "pieces": ("Pieces", "range", 1),
        "jewel_in_100": ("Jewel chance", "pctn", 1), "live": ("In the game", "live", 0),
    },
    "worlds": {"live": ("In the game", "live", 0)},
    "events": {"live": ("In the game", "live", 0)},
}
SPAWNED = {"level": ("Level", "n", 0), "life": ("Life", "n", 0), "damage": ("Damage", "range", 0),
           "defense": ("Defense", "n", 0), "attack_rate": ("Attack rate", "n", 0),
           "defense_rate": ("Defense rate", "n", 0), "swing_ticks": ("Attacks every", "ticks", 0)}


# ---------------------------------------------------------------- saying things


def num(v):
    if isinstance(v, bool):
        return "yes" if v else "no"
    if isinstance(v, float) and not v.is_integer():
        return f"{v:,.2f}".rstrip("0").rstrip(".")
    return f"{int(v):,}" if isinstance(v, (int, float)) else str(v)


def chance(c):
    if not c:
        return "never"
    if c >= 0.999:
        return "always"
    if c >= 0.2:
        return f"{c * 100:.0f}%"
    return f"1 in {round(1 / c):,}"


def say_as(how, v):
    if v is None:
        return "—"
    if how == "n":
        return num(v)
    if how == "zen":
        return f"{num(v)} Zen"
    if how == "ticks":
        return f"{num(round(v / TICKS, 2))} s" if v else "none"
    if how == "secs":
        return f"{num(round(v / 3600, 1))} h" if v >= 3600 else f"{num(v)} s"
    if how == "tiles":
        return f"{num(v)} tiles"
    if how == "pct":
        return f"{num(round(v * 100, 1))}%"
    if how == "pctn":
        return f"{num(v)}%"
    if how == "x":
        return f"×{num(round(v, 2))}"
    if how == "chance":
        return chance(v)
    if how == "yes":
        return "yes" if v else "no"
    if how == "live":
        return "in the game" if v else "not yet"
    if how in ("list", "range"):
        if isinstance(v, list):
            return (" – " if how == "range" else ", ").join(num(x) for x in v) or "none"
    if isinstance(v, (dict, list)):
        return json.dumps(v, ensure_ascii=False)
    return num(v) if isinstance(v, (int, float)) else str(v)


def same(x, y):
    """Equal, a float's last digits aside (the dump writes float32s through doubles)."""
    if isinstance(x, (int, float)) and isinstance(y, (int, float)) and not isinstance(x, bool) and not isinstance(y, bool):
        return abs(x - y) <= 1e-6 * max(1.0, abs(x), abs(y))
    if isinstance(x, list) and isinstance(y, list):
        return len(x) == len(y) and all(same(a, b) for a, b in zip(x, y))
    if isinstance(x, dict) and isinstance(y, dict):
        return x.keys() == y.keys() and all(same(x[k], y[k]) for k in x)
    return x == y


CLASS_NAMES = {"DK": "Dark Knight", "DW": "Dark Wizard", "Elf": "Fairy Elf", "FE": "Fairy Elf", "MG": "Magic Gladiator"}


def direction(was, now, better):
    if better and isinstance(was, (int, float)) and isinstance(now, (int, float)) and not isinstance(was, bool):
        if now == was:
            return "changed"
        return "buffed" if (now > was) == (better > 0) else "nerfed"
    return "changed"


def change(field, label, how, better, was, now):
    if was is None:
        kind = "new"
    elif now is None:
        kind = "removed"
    else:
        kind = direction(was, now, better)
    return {"field": field, "label": label, "was": say_as(how, was) if was is not None else "",
            "now": say_as(how, now) if now is not None else "", "change": kind}


def line(c):
    if c["change"] == "new" and not c["was"]:
        return f"{c['label']}: {c['now']}" if c["now"] else c["label"]
    if c["change"] == "removed" and not c["now"]:
        return f"{c['label']}: no longer"
    return f"{c['label']}: {c['was']} → {c['now']}"


# ---------------------------------------------------------------- reading a release


def get(e, path):
    for k in path.split("."):
        if not isinstance(e, dict) or k not in e:
            return None
        e = e[k]
    return e


def has(e, path):
    for k in path.split("."):
        if not isinstance(e, dict) or k not in e:
            return False
        e = e[k]
    return True


class Release:
    """One built release's entities, with the names its indexes give."""

    def __init__(self, folder):
        self.dir = Path(folder)
        self.meta = load(self.dir / "meta.json")
        self.ents = {}
        for idx in sorted((self.dir / "index").glob("*.json")):
            kind = idx.stem
            for f in sorted((self.dir / kind).glob("*.json")):
                e = load(f)
                if kind == "items":
                    c = self.dir / "cards" / f.name
                    if c.exists():
                        plain = (load(c).get("cards") or {}).get("plain") or {}
                        e["sells"] = plain.get("sells")
                self.ents[(kind, f.stem)] = e
        self.item_name = {}
        rows = self.dir / "index" / "items.json"
        if rows.exists():
            self.item_name = {r["k"]: r["n"] for r in load(rows).get("rows") or []}
        self.world_name = {}
        rows = self.dir / "index" / "worlds.json"
        if rows.exists():
            self.world_name = {r["k"]: r["n"] for r in load(rows).get("rows") or []}
        # Every watched field some entity of a kind has: what the release's schema holds.
        self.schema = {}
        for (kind, _), e in self.ents.items():
            s = self.schema.setdefault(kind, set())
            for path in WATCH.get(kind, {}):
                if has(e, path):
                    s.add(path)

    def place(self, p):
        """A drop place said for people: Lorencia, Blood Castle 4, Devil Square 2, Golden Dragon raid (Noria)."""
        p = str(p or "")
        for pre, name in (("bloodcastle-", "Blood Castle "), ("devilsquare-", "Devil Square ")):
            if p.startswith(pre):
                return name + p[len(pre):]
        if p.endswith("-raid"):
            w = p[:-len("-raid")]
            return f"the raid on {self.world_name.get(w, w.title())}"
        return self.world_name.get(p, p.title())


# ---------------------------------------------------------------- what changed


def pool_shift(was, now):
    return was and now and abs(now - was) / was < POOL_SHIFT


def places_said(places):
    """Lorencia, Noria; the raid on Devias, Lorencia, Noria."""
    pre = "the raid on "
    if len(places) > 1 and all(p.startswith(pre) for p in places):
        return pre + ", ".join(p[len(pre):] for p in places)
    return ", ".join(places)


def drops_of_item(e):
    """(monster_slug, place, elite) -> (chance, monster name), quest treasures aside."""
    out = {}
    for d in e.get("drops") or []:
        if any(r.get("roll") == "while_quest" for r in d.get("rolls") or []):
            continue
        key = (d.get("monster_slug"), d.get("place"), bool(d.get("elite")))
        out[key] = (d.get("chance") or 0, d.get("monster_name") or d.get("monster_slug"))
    return out


def sources_of(e):
    """(kind, slug) -> (name, role) of what makes it or asks for it."""
    out = {}
    for kind, refs in (e.get("from") or {}).items():
        for r in refs or []:
            out[(kind, r.get("slug"))] = (r.get("name"), r.get("role"))
    return out


SOURCE_WORDS = {("shops", "gives"): "Sold by", ("npcs", "gives"): "Sold by", ("mixes", "gives"): "Made by",
                ("quests", "gives"): "Quest reward from", ("boxes", "gives"): "Found in",
                ("events", "gives"): "Reward from"}


def item_changes(a, b, R0, R1):
    out, shifted, most = [], 0, 0.0
    da, db = drops_of_item(a), drops_of_item(b)
    # One line for a monster whose drop changed the same way in several places (the raid's three towns).
    merged = {}
    for key in sorted(set(da) | set(db), key=str):
        mon, place, elite = key
        x, y = (da[key][0] if key in da else None), (db[key][0] if key in db else None)
        if x is not None and y is not None and same(x, y):
            continue
        if x is not None and y is not None and pool_shift(x, y):
            shifted += 1
            most = max(most, abs(y - x) / x)
            continue
        who = (db.get(key) or da.get(key))[1] + (" (Elite)" if elite else "")
        sx, sy = say_as("chance", x) if x is not None else None, say_as("chance", y) if y is not None else None
        group = merged.setdefault((mon, elite, sx, sy), {"who": who, "x": x, "y": y, "places": []})
        group["places"].append((R1 if key in db else R0).place(place))
    for (mon, elite, _, _), g in merged.items():
        field = f"drops[{mon}{'@elite' if elite else ''}@{'+'.join(g['places'])}]"
        c = change(field, f"Drops from {g['who']}, {places_said(g['places'])}", "chance", 1, g["x"], g["y"])
        c["cause"] = f"drops:{g['who']}@{places_said(g['places'])}"
        out.append(c)
    if shifted:
        out.append({"field": "drops.pool", "label": "Drop chances shifted slightly", "was": "",
                    "now": f"{shifted} places, by at most {most * 100:.0f}%: the pools it drops from changed size",
                    "change": "changed", "pool": True})
    sa, sb = sources_of(a), sources_of(b)
    for key in sorted(set(sa) | set(sb), key=str):
        name, role = sb.get(key) or sa.get(key)
        words = SOURCE_WORDS.get((key[0], role))
        if not words or (key in sa) == (key in sb):
            continue
        out.append({"field": f"from[{key[0]}:{key[1]}]", "label": f"{words} {name}",
                    "was": "" if key in sb else "yes", "now": "yes" if key in sb else "",
                    "change": "new" if key in sb else "removed", "cause": f"source:{words} {name}"})
    return out


def monster_changes(a, b, R0, R1):
    out = []
    pa = {(p.get("place"), bool(p.get("elite"))): p for p in a.get("drops") or []}
    pb = {(p.get("place"), bool(p.get("elite"))): p for p in b.get("drops") or []}
    for key in sorted(set(pa) | set(pb), key=str):
        place, elite = key
        where = (R1 if key in pb else R0).place(place) + (" (Elite)" if elite else "")
        if key not in pa:
            out.append({"field": f"places[{place}{'@elite' if elite else ''}]", "label": f"Met in {where}",
                        "was": "", "now": "yes", "change": "new"})
            continue
        if key not in pb:
            out.append({"field": f"places[{place}{'@elite' if elite else ''}]", "label": f"Met in {where}",
                        "was": "yes", "now": "", "change": "removed"})
            continue
        x, y = pa[key], pb[key]
        sx, sy = x.get("spawned") or {}, y.get("spawned") or {}
        for f, (label, how, better) in SPAWNED.items():
            if f in sx and f in sy and not same(sx[f], sy[f]):
                c = change(f"spawned[{place}].{f}", f"{label}, {where}", how, better, sx[f], sy[f])
                c["cause"] = f"spawned:{label}"
                out.append(c)
        ia = {i.get("item"): i.get("chance") or 0 for i in x.get("items") or []}
        ib = {i.get("item"): i.get("chance") or 0 for i in y.get("items") or []}
        joined = [k for k in ib if k not in ia]
        shifted, most = 0, 0.0
        for k in sorted(set(ia) | set(ib)):
            name = R1.item_name.get(k) or R0.item_name.get(k) or k
            field = f"drops[{place}].{k}"
            if k not in ia:
                out.append(dict(change(field, f"Drops {name}, {where}", "chance", 1, None, ib[k]), cause=f"mdrop:{name}:new"))
            elif k not in ib:
                out.append(dict(change(field, f"Drops {name}, {where}", "chance", 1, ia[k], None), cause=f"mdrop:{name}:removed"))
            elif not same(ia[k], ib[k]):
                if pool_shift(ia[k], ib[k]):
                    shifted += 1
                    most = max(most, abs(ib[k] - ia[k]) / ia[k])
                else:
                    out.append(change(field, f"Drops {name}, {where}", "chance", 1, ia[k], ib[k]))
        if shifted:
            new = ", ".join(R1.item_name.get(k, k) for k in joined[:6]) + (" and more" if len(joined) > 6 else "")
            out.append({"field": f"drops[{place}].pool", "label": f"Drop chances shifted slightly, {where}", "was": "",
                        "now": f"{shifted} items, by at most {most * 100:.0f}%"
                               + (f" (new in the pool: {new})" if joined else ""),
                        "change": "changed", "pool": True})
    return out


def shelf_changes(a, b, R0, R1):
    out = []
    def shelf(e):
        return {(s.get("item"), s.get("plus")): (s.get("pieces") or 0, s.get("price")) for s in e.get("shelf") or []}
    def said(v):
        pieces, price = v
        return (f"×{pieces} for " if pieces and pieces > 1 else "") + f"{num(price)} Zen"
    sa, sb = shelf(a), shelf(b)
    for key in sorted(set(sa) | set(sb), key=str):
        item, plus = key
        name = (R1.item_name.get(item) or R0.item_name.get(item) or item) + (f" +{plus}" if plus else "")
        x, y = sa.get(key), sb.get(key)
        if x == y:
            continue
        kind = "new" if x is None else "removed" if y is None else \
            ("buffed" if y[0] == x[0] and y[1] < x[1] else "nerfed" if y[0] == x[0] and y[1] > x[1] else "changed")
        out.append({"field": f"shelf[{item}+{plus}]", "label": f"Sells {name}", "was": said(x) if x else "",
                    "now": said(y) if y else "", "change": kind, "cause": "shelf"})
    return out


def paid_changes(a, b, R0, R1):
    out = []
    def paid(e):
        return {(p.get("item"), p.get("class"), p.get("power"), p.get("rank")): p for p in e.get("paid") or []}
    pa, pb = paid(a), paid(b)
    for key in sorted(set(pa) | set(pb), key=str):
        item, cls, power, rank = key
        p = pb.get(key) or pa.get(key)
        name = (R1.item_name.get(item) or R0.item_name.get(item) or item) + (f" +{p.get('plus')}" if p.get("plus") else "") \
            + (f" ×{p.get('count')}" if (p.get("count") or 1) > 1 else "") + (f" (rune power {power})" if power else "") \
            + (f", for the {CLASS_NAMES.get(cls, cls)}" if cls else "")
        if key not in pa or key not in pb:
            out.append({"field": f"paid[{item}:{cls}:{power}]", "label": f"Reward: {name}",
                        "was": "" if key in pb else "yes", "now": "yes" if key in pb else "",
                        "change": "new" if key in pb else "removed"})
    return out


def box_changes(a, b, R0, R1):
    out = []
    ra = {r.get("item"): r.get("chance") or 0 for r in a.get("result") or []}
    rb = {r.get("item"): r.get("chance") or 0 for r in b.get("result") or []}
    for k in sorted(set(ra) | set(rb)):
        name = R1.item_name.get(k) or R0.item_name.get(k) or k
        if not same(ra.get(k), rb.get(k)) and not (k in ra and k in rb and pool_shift(ra[k], rb[k])):
            out.append(change(f"result[{k}]", f"Holds {name}", "chance", 1, ra.get(k), rb.get(k)))
    return out


def talent_changes(a, b, R0, R1):
    out = []
    def nodes(e):
        return {(c.get("name"), n.get("name")): n for c in e.get("columns") or [] for n in c.get("nodes") or []}
    na, nb = nodes(a), nodes(b)
    for key in sorted(set(na) | set(nb), key=str):
        x, y = na.get(key) or {}, nb.get(key) or {}
        for f, label in (("text", "Talent text"), ("amount", "Talent amount")):
            if not same(x.get(f), y.get(f)):
                out.append(change(f"talents[{key[0]}:{key[1]}].{f}", f"{key[1]} ({key[0]}): {label.split()[1]}",
                                  "text" if f == "text" else "n", 0, x.get(f), y.get(f)))
    return out


SPECIAL = {"items": item_changes, "monsters": monster_changes, "npcs": shelf_changes, "quests": paid_changes,
           "boxes": box_changes, "talents": talent_changes}


def changes_of(kind, a, b, R0, R1):
    out = []
    schema = R0.schema.get(kind, set())
    for path, (label, how, better) in WATCH.get(kind, {}).items():
        if path not in schema:
            continue  # new to the schema: not an update
        x, y = get(a, path), get(b, path)
        if not same(x, y):
            out.append(dict(change(path, label, how, better, x, y), cause=f"field:{kind}:{label}"))
    if kind in SPECIAL:
        out += SPECIAL[kind](a, b, R0, R1)
    return out


# ---------------------------------------------------------------- the previous release


def build_previous(prev, root, say):
    """The release before, built into root/<prev>/ without thumbnails or its own previous, marked
    SEED. True when it is there; any failure is said and taken, never raised."""
    if (root / prev / "meta.json").exists():
        return True
    say(f"history: the release before ({prev}) is not built; building it (no thumbnails)")
    was = os.environ.get("ARMORY_NO_PREVIOUS")
    os.environ["ARMORY_NO_PREVIOUS"] = "1"
    try:
        import build
        ok, _ = build.main(["--commit", prev, "--out", str(root), "--no-thumbs"], say)
    except BaseException as e:  # noqa: BLE001 - a SystemExit or anything else: this build goes on
        say(f"history: building {prev} failed ({e}); this release's history starts here")
        ok = False
    finally:
        if was is None:
            os.environ.pop("ARMORY_NO_PREVIOUS", None)
        else:
            os.environ["ARMORY_NO_PREVIOUS"] = was
    if ok and (root / prev / "meta.json").exists():
        (root / prev / "SEED").write_text("built for history only: never uploaded, never served (history.py)\n")
        return True
    if ok:
        say(f"history: {prev} built nowhere it can be read; this release's history starts here")
    return False


# A field changed across a kind, said as one line of the notes.
HEADLINES = {("runes", "Rune text"): "Runes reworded", ("runes", "One per item"): "Runes now limited to one per item",
             ("runes", "In the game"): "Runes now in the game", ("runes", "Level to use"): "Rune level requirements changed",
             ("skills", "Skill text"): "Skills reworded", ("skills", "Reach"): "Skill reach changed",
             ("items", "Sell price"): "Sell prices changed", ("items", "In the game"): "Items now in the game",
             ("items", "Level required"): "Item level requirements changed"}
KIND_WORDS = {"items": "items", "runes": "runes", "skills": "skills", "monsters": "monsters", "npcs": "townsfolk",
              "quests": "quests", "mixes": "mixes", "boxes": "boxes", "talents": "talent trees", "elites": "Elite maps",
              "events": "events", "worlds": "maps"}
# How much a line of the notes matters, first first: new things, then the game's rules and pay,
# then numbers and new drops, then hoards, shops and the like.
IMPORTANCE = {"runes": 1, "skills": 1, "quests": 1, "talents": 1, "mixes": 1, "items": 2, "monsters": 2,
              "elites": 2, "boxes": 4, "npcs": 5, "events": 5, "worlds": 5}
GROUP_AT = 3  # a cause shared by this many entities is one line of the notes


def headline(cause, members):
    """What a group of changes sharing one cause says, and how much it matters."""
    n = len(members)
    changes = [c for m in members for c in m["changes"]]
    new = sum(1 for c in changes if c["change"] == "new")
    gone = sum(1 for c in changes if c["change"] == "removed")
    other = len(changes) - new - gone
    def counts(word):
        bits = [f"{new} {word} added" if new else "", f"{gone} removed" if gone else "", f"{other} changed" if other else ""]
        return ", ".join(b for b in bits if b)
    kind, _, rest = cause.partition(":")
    if kind == "drops":
        who, _, where = rest.partition("@")
        return f"What {who} drops ({where}): {counts('items')}", 4 if "raid" in where else 3
    if kind == "source":
        return f"{rest} ({n} items)", 1
    if kind == "mdrop":
        item, _, how = rest.rpartition(":")
        return (f"{item} now drops from {n} monsters" if how == "new" else f"{item} no longer drops from {n} monsters"), 3
    if kind == "spawned":
        return f"Monsters' {rest.lower()} changed ({n} monsters)", 2
    if kind == "shelf":
        sample = changes[0]
        return f"Shop shelves changed ({n} shops, {len(changes)} shelves; e.g. {line(sample)})", 5
    if kind == "field":
        k, _, label = rest.partition(":")
        words = HEADLINES.get((k, label)) or f"{label} changed"
        return f"{words} ({n} {KIND_WORDS.get(k, k)})", IMPORTANCE.get(k, 5)
    return f"{cause} ({n})", 5


def notes_of(changed, new):
    """The release's notes: [{type: new|group|single, ...}], what matters first. `changed` is
    [{kind, slug, name, changes}] of the real (not pool) changes."""
    by_cause = {}
    for e in changed:
        for c in e["changes"]:
            if c.get("cause"):
                m = by_cause.setdefault(c["cause"], {})
                m.setdefault((e["kind"], e["slug"]), {"kind": e["kind"], "slug": e["slug"], "name": e["name"], "changes": []})
                m[(e["kind"], e["slug"])]["changes"].append(c)
    grouped = {cause for cause, m in by_cause.items() if len(m) >= GROUP_AT}
    notes = [dict(type="new", importance=0, **t) for t in new]
    for cause in grouped:
        members = sorted(by_cause[cause].values(), key=lambda m: m["name"])
        words, importance = headline(cause, members)
        notes.append({"type": "group", "importance": importance, "headline": words, "cause": cause, "count": len(members),
                      "kind": members[0]["kind"],
                      "entities": [{"kind": m["kind"], "slug": m["slug"], "name": m["name"],
                                    "lines": [line(c) for c in m["changes"]]} for m in members]})
    for e in changed:
        rest = [c for c in e["changes"] if c.get("cause") not in grouped]
        if not rest:
            continue
        lines = [line(c) for c in rest]
        if len(lines) > 8:
            lines = lines[:7] + [f"and {len(lines) - 7} more changes"]
        notes.append({"type": "single", "importance": IMPORTANCE.get(e["kind"], 5), "kind": e["kind"], "slug": e["slug"],
                      "name": e["name"], "summary": lines})
    notes.sort(key=lambda x: (x["importance"], -(x.get("count") or 1), x.get("headline") or x.get("name") or ""))
    return notes


def release_label(commit, date):
    """The day it went live, said, with its time when another release went live that day."""
    import datetime
    when = None
    same_day = 0
    try:
        import releases
        rows = releases.load()
        for r in rows:
            if r["commit"] == commit:
                when = r.get("live_at")
        same_day = sum(1 for r in rows if (r.get("live_at") or "")[:10] == date)
    except Exception:  # noqa: BLE001 - the day alone
        pass
    try:
        d = datetime.date.fromisoformat(date)
        said = f"{d.day} {d.strftime('%B %Y')}"
    except ValueError:
        return date
    if same_day > 1 and when and len(when) >= 16:
        said += f", {when[11:16]} UTC"
    return said


def release_number(commit):
    try:
        import releases
        order = releases.commits()
        return order.index(commit) + 1 if commit in order else None
    except Exception:  # noqa: BLE001
        return None


def release_date(commit, meta):
    """The day players had it: releases.json's live_at, else the build's own day."""
    try:
        import releases
        for r in releases.load():
            if r["commit"] == commit and r.get("live_at"):
                return r["live_at"][:10]
    except Exception:  # noqa: BLE001 - the build's day stands in
        pass
    return (meta.get("built") or "")[:10]


def run(this, previous=None, say=print):
    this = Path(this)
    meta = load(this / "meta.json")
    commit = meta["commit"]
    prev = previous or meta.get("previous")
    if prev and os.environ.get("ARMORY_NO_PREVIOUS") and not (this.parent / prev / "meta.json").exists():
        say(f"history: {prev} is not built and this is a build for history; its history starts here")
        prev = None
    if not prev:
        say("history: no release before this one; every entity starts its history here")
    prev_dir = (this.parent / prev) if prev else None
    if prev and not build_previous(prev, this.parent, say):
        prev, prev_dir = None, None
    R1 = Release(this)
    R0 = Release(prev_dir) if prev else None
    stamp = {"commit": commit, "protocol": meta["protocol"]}
    base = {"commit": commit, "protocol": meta["protocol"], "date": release_date(commit, meta)}
    # Built but not in the game (live false: a map nobody can get to, a rune nothing drops): not
    # news while it stays so; it is listed apart, as coming.
    things = {"new": [], "changed": [], "removed": [], "coming": []}
    real_changes = []
    shifts = 0
    for (kind, slug), e in R1.ents.items():
        before = []
        if prev:
            h = prev_dir / "history" / kind / f"{slug}.json"
            if h.exists():
                before = [r for r in load(h).get("releases") or [] if r.get("commit") != commit]
        entry = None
        name = e.get("name") or e.get("title") or e.get("class") or slug
        if prev and (kind, slug) not in R0.ents and e.get("live") is False:
            things["coming"].append({"kind": kind, "slug": slug, "name": name, "summary": []})
        elif prev and (kind, slug) not in R0.ents:
            entry = dict(base, added=True, changes=[])
            things["new"].append({"kind": kind, "slug": slug, "name": name, "summary": []})
        elif prev and e.get("live") is False and R0.ents[(kind, slug)].get("live") is False:
            pass  # still not in the game: what changed in it is not news yet
        elif prev:
            ch = changes_of(kind, R0.ents[(kind, slug)], e, R0, R1)
            if ch:
                entry = dict(base, changes=ch)
                real = [c for c in ch if not c.get("pool")]
                if real:
                    real_changes.append({"kind": kind, "slug": slug, "name": name, "changes": real})
                    lines = [line(c) for c in real]
                    if len(lines) > 8:
                        lines = lines[:7] + [f"and {len(lines) - 7} more changes"]
                    things["changed"].append({"kind": kind, "slug": slug, "name": name, "summary": lines})
                else:
                    shifts += 1
        write(this / "history" / kind / f"{slug}.json",
              {"kind": kind, "slug": slug, "name": name, "releases": ([entry] if entry else []) + before}, stamp)
    if prev:
        for (kind, slug), e in R0.ents.items():
            if (kind, slug) not in R1.ents:
                things["removed"].append({"kind": kind, "slug": slug, "name": e.get("name") or slug, "summary": []})
    order = ["items", "runes", "skills", "talents", "quests", "monsters", "npcs", "mixes", "boxes", "elites", "events", "worlds"]
    for group in things.values():
        group.sort(key=lambda t: (order.index(t["kind"]) if t["kind"] in order else 99, t["name"]))
    rel_before = []
    if prev and (prev_dir / "history" / "releases.json").exists():
        rel_before = [r for r in load(prev_dir / "history" / "releases.json").get("releases", [])
                      if r.get("commit") != commit]
    note = NOTES / f"{commit}.md"
    this_rel = dict(base, previous=prev, note=note.read_text().strip() if note.exists() else None,
                    label=release_label(commit, base["date"]), number=release_number(commit),
                    notes=notes_of(real_changes, things["new"]) if prev else [],
                    counts={"new": len(things["new"]), "changed": len(things["changed"]),
                            "removed": len(things["removed"]), "pool_shifts": shifts},
                    things=things)
    write(this / "history" / "releases.json", {"releases": [this_rel] + rel_before}, stamp)
    say(f"history: {len(things['changed'])} changed, {len(things['new'])} new, {len(things['removed'])} removed, "
        f"{shifts} with only their drop pools shifted, since {prev or '(none)'}")
    return this_rel


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", nargs="?", help="the built release (out/armory/<commit>)")
    ap.add_argument("--previous", help="the release to compare with (meta.json's by default)")
    a = ap.parse_args()
    folder = Path(a.folder) if a.folder else ARMORY / (__import__("releases").commits() or [""])[-1]
    run(folder, a.previous)


if __name__ == "__main__":
    main()
