"""A picture for every page that has none of its own, taken from what it links to.

    python3 tools/armory/pictures.py out/armory/<commit>     (build.py runs it after publishing)

Each entity without a thumbnail of its own gets `pic` = {page, card} (the 512 and 96 px
thumbnails), on its file and its index row, chosen from the built release alone:

  box     its own item's thumbnail
  mix     its first result item's, else the townsperson who mixes it
  rune    the item a rune comes on (the item the quests pay with a rune's power on it), with
          `tone` its rarity, for the site to tint
  elite   the Elite look of that map's strongest Elite breed
  event   its strongest monster's
  quest   its giver's, else its first target monster's

Nothing is chosen by name; a page whose links give nothing keeps none.
"""
import json
import sys
from collections import Counter
from pathlib import Path


def _load(p):
    try:
        return json.loads(Path(p).read_text())
    except (OSError, ValueError):
        return None


def _save(p, v):
    Path(p).write_text(json.dumps(v, separators=(",", ":"), ensure_ascii=False))


def _pic(card):
    """{page, card} from a list row's 96 px thumbnail."""
    return {"page": card.replace(".96.webp", ".webp"), "card": card} if card else None


def run(dest, say=print):
    dest = Path(dest)
    rows = lambda kind: (_load(dest / "index" / f"{kind}.json") or {}).get("rows") or []  # noqa: E731
    items = {r["k"]: r for r in rows("items")}
    mons = rows("monsters")
    mon_by_slug = {r["slug"]: r for r in mons}
    mon_by_num = {r.get("num"): r for r in mons}
    npc_by_num = {}
    for r in rows("npcs"):
        if r.get("t"):
            npc_by_num.setdefault(r.get("npc"), r)
    item_pic = lambda k: _pic((items.get(k) or {}).get("t"))  # noqa: E731

    # The item a rune is carried on: what the quests pay with a rune's power on it.
    carried = Counter(p.get("item") for q in rows("quests") for p in (_load(dest / "quests" / f"{q['slug']}.json") or {}).get("paid") or []
                      if p.get("power"))
    rune_item = carried.most_common(1)[0][0] if carried else None

    # Each map's strongest Elite look: the monster files' Elite places and their spawned levels.
    elite_of = {}
    for r in mons:
        e = _load(dest / "monsters" / f"{r['slug']}.json") or {}
        if not (e.get("elite_thumb") or {}).get("card"):
            continue
        for s in e.get("spawned") or []:
            if s.get("elite") and s.get("world"):
                best = elite_of.get(s["world"])
                if best is None or (s.get("level") or 0) > best[0]:
                    elite_of[s["world"]] = (s.get("level") or 0, e["elite_thumb"])

    def choose(kind, e):
        if kind == "boxes":
            return item_pic(e.get("item"))
        if kind == "mixes":
            for r in e.get("result") or []:
                p = item_pic(r.get("item"))
                if p:
                    return p
            return _pic((npc_by_num.get(e.get("npc")) or {}).get("t"))
        if kind == "runes":
            p = item_pic(rune_item)
            return dict(p, tone=e.get("rarity")) if p else None
        if kind == "elites":
            got = elite_of.get(e.get("world"))
            return {"page": got[1]["page"], "card": got[1]["card"]} if got else None
        if kind == "events":
            top = sorted((m for m in e.get("monsters") or [] if (mon_by_slug.get(m.get("slug")) or {}).get("t")),
                         key=lambda m: -(m.get("level") or 0))
            return _pic(mon_by_slug[top[0]["slug"]]["t"]) if top else None
        if kind == "quests":
            p = _pic((npc_by_num.get(e.get("giver")) or {}).get("t"))
            if p:
                return p
            for s in e.get("steps") or []:
                p = _pic((mon_by_num.get(s.get("target")) or {}).get("t"))
                if p:
                    return p
        return None

    done = Counter()
    for kind in ("boxes", "mixes", "runes", "elites", "events", "quests"):
        idx = _load(dest / "index" / f"{kind}.json")
        if not idx:
            continue
        for r in idx.get("rows") or []:
            f = dest / kind / f"{r['slug']}.json"
            e = _load(f)
            if not e or e.get("thumb"):
                continue
            p = choose(kind, e)
            if p:
                e["pic"] = r["pic"] = p
                _save(f, e)
                done[kind] += 1
        _save(dest / "index" / f"{kind}.json", idx)
    say("pictures: " + ", ".join(f"{n} {k}" for k, n in sorted(done.items())))
    return done


if __name__ == "__main__":
    run(sys.argv[1])
