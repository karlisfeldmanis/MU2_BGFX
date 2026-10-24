"""The Armory on mu-classic.com: the game's items, monsters, townsfolk, quests, events, runes, skills,
talents, mixes and Elites, from a built release (tools/armory/build.py's out/armory/<commit>/), and
the players' comments on them (docs/plans/armory.md).

server.py asks `get()` and `post()` first; each answers True when the path was the Armory's.

    GET  /armory, /armory/<list>              a list page: items (the default), monsters, npcs, quests, events,
                                              runes, skills, talents, mixes, elites, worlds, boxes
    GET  /armory/<one>/<slug>                 an entity's page: item, monster, npc, quest, event, rune, skill,
                                              mix, elite, world, box; /armory/talents/<class> the calculator
         Each is served whole: its head says the entity (title, description, og:image its thumbnail)
         and its body carries a plain rendering and the entity's own JSON, so a link previews in
         Discord and a search engine reads it without running the page's script. Under /<lang>/ in
         the site's languages (web/i18n.py), names staying English.
    GET  /armory/data/<commit>/<path>         the release's files (index/, <kind>/, cards/, thumbs/, icons/,
                                              maps/, history/, search.json), immutable
    GET  /armory/sitemap.xml                  every page, for search engines
    GET  /api/armory/comments?kind=K&slug=S   -> 200 {comments, me}
    POST /api/armory/comments  Bearer T {kind, slug, about, text}   -> 201 {ok, id} | 400 | 401 | 429
         `about` is "" (the thing itself) or the commit of one of its updates (history/<kind>/<slug>.json).
    POST /api/armory/vote      Bearer T {id}            -> 200 {ok, votes, mine}  (again takes it back)
    POST /api/armory/report    Bearer T {id, why?}      -> 200 {ok}               (once an account)
    POST /api/armory/delete    Bearer T {id}            -> 200 {ok} | 404          (its writer, or an admin)
    GET  /api/armory/reports   Bearer T of an admin     -> 200 {reports} | 404 for anyone else

The release served is --armory DIR: a built release itself (has meta.json), or a folder of them, of
which `current` (a link deploy.sh moves) or else the newest built is served. Read again when its
meta.json changes, so a new release needs no restart.

Comments live in the accounts' database (armory_comments, armory_votes, armory_reports), each under
its account and gone with it (ON DELETE CASCADE).
"""

import html
import json
import mimetypes
import re
import threading
import time
from pathlib import Path
from urllib.parse import parse_qs

import i18n

SITE = i18n.SITE
# A list page's path and what it shows, in the rail's order.
LISTS = ("items", "monsters", "npcs", "quests", "events", "runes", "skills", "talents", "mixes", "elites", "worlds", "boxes")
LIST_NAMES = {"items": "Items", "monsters": "Monsters", "npcs": "NPCs", "quests": "Quests", "events": "Events",
              "runes": "Runes", "skills": "Skills", "talents": "Talent calculator", "mixes": "Chaos Machine",
              "elites": "Elite monsters", "worlds": "Maps", "boxes": "Boxes"}
# An entity's path's one: /armory/item/<slug> is items/<slug>.json (build.py singular()).
ONE = {"item": "items", "monster": "monsters", "npc": "npcs", "quest": "quests", "event": "events", "rune": "runes",
       "skill": "skills", "talents": "talents", "mix": "mixes", "elite": "elites", "world": "worlds", "box": "boxes"}
KIND_ONE = {v: k for k, v in ONE.items()}
# What the items list's address carries (/armory?cat=... before /armory/items).
LIST_QUERY = {"cat", "sub", "sort", "dir", "page", "cls", "src", "q"}
STATIC = {"armory.js", "armory.css", "card.js", "card_grain.png"}
SLUG = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
# A release's files named by their content (build.py's thumbs and icons): the only ones kept forever.
HASHED = re.compile(r"^(thumbs|icons)/[0-9a-f]{8,}(\.\d+)?\.(webp|png)$")
COMMIT = re.compile(r"^[0-9a-f]{7,40}$")
MOST_TEXT = 1000
# The words the pages say themselves, beside what armory.js says through t(): units of /translate.
PAGE_WORDS = ("MU Classic home", "Sign in", "Privacy", "Contact", "The Armory has not been built yet.",
              "To WebZen, and to MU Online fans across the years: This is a love letter, not a business. Every character, "
              "item, monster, map, sound and name in it belongs to WebZen Inc., who created MU. This is not our game; "
              "it's a tribute to theirs.",
              "© 2026 MU Classic · MU Online is a trademark of WebZen Inc. · Unofficial and non-commercial")

SCHEMA = """
CREATE TABLE IF NOT EXISTS armory_comments (
    id         INTEGER PRIMARY KEY,
    kind       TEXT NOT NULL,       -- items, monsters, ... (the release's folder)
    slug       TEXT NOT NULL,
    about      TEXT NOT NULL DEFAULT '',   -- '' the thing, else the commit of one of its updates
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    text       TEXT NOT NULL,
    at         INTEGER NOT NULL,
    gone       INTEGER NOT NULL DEFAULT 0, -- taken down: by its writer (1) or an admin (2)
    gone_by    INTEGER
);
CREATE INDEX IF NOT EXISTS armory_comments_on ON armory_comments(kind, slug, at);
CREATE TABLE IF NOT EXISTS armory_votes (
    comment_id INTEGER NOT NULL REFERENCES armory_comments(id) ON DELETE CASCADE,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (comment_id, account_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS armory_reports (
    comment_id INTEGER NOT NULL REFERENCES armory_comments(id) ON DELETE CASCADE,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    why        TEXT NOT NULL DEFAULT '',
    at         INTEGER NOT NULL,
    PRIMARY KEY (comment_id, account_id)
) WITHOUT ROWID;
"""


class Limit:
    """At most `most` tries per `seconds` from one key (as server.py's)."""

    def __init__(self, most, seconds):
        self.most, self.seconds, self.tries, self.lock = most, seconds, {}, threading.Lock()

    def allow(self, who):
        now = time.monotonic()
        with self.lock:
            recent = [t for t in self.tries.get(who, []) if now - t < self.seconds]
            if len(recent) >= self.most:
                self.tries[who] = recent
                return False
            self.tries[who] = recent + [now]
            if len(self.tries) > 10000:
                self.tries = {k: v for k, v in self.tries.items() if v and now - v[-1] < self.seconds}
            return True


# A conversation, not a flood: per account and per address.
COMMENT_LIMIT = Limit(6, 600)
COMMENT_IP_LIMIT = Limit(20, 3600)
VOTE_LIMIT = Limit(120, 3600)
REPORT_LIMIT = Limit(20, 3600)


def esc(s) -> str:
    return html.escape(str(s if s is not None else ""), quote=True)


class Release:
    """One built release, read lazily and cached until its meta.json changes."""

    def __init__(self, folder: Path):
        self.dir = folder
        self.meta = json.loads((folder / "meta.json").read_text())
        self.commit = self.meta["commit"]
        self.mark = (folder / "meta.json").stat().st_mtime_ns
        self.cache = {}
        self.lock = threading.Lock()

    def load(self, rel: str):
        with self.lock:
            if rel in self.cache:
                return self.cache[rel]
        f = self.dir / rel
        v = json.loads(f.read_text()) if f.is_file() else None
        with self.lock:
            if len(self.cache) > 4000:
                self.cache.clear()
            self.cache[rel] = v
        return v

    def rows(self, kind: str) -> list:
        v = self.load(f"index/{kind}.json")
        return (v or {}).get("rows") or []

    def kinds(self) -> list:
        return [k for k in LISTS if (self.dir / "index" / f"{k}.json").is_file()]


class Armory:
    def __init__(self, root, site: Path):
        self.root = Path(root) if root else None
        self.site = site
        self.release_ = None
        self.lock = threading.Lock()
        self.made = False

    # ---- the release

    def folder(self):
        r = self.root
        if r is None or not r.is_dir():
            return None
        if (r / "meta.json").is_file():
            return r
        if (r / "current" / "meta.json").is_file():
            return (r / "current").resolve()
        best = None
        for d in r.iterdir():
            m = d / "meta.json"
            if d.is_dir() and m.is_file() and not d.name.endswith(".new"):
                try:
                    built = json.loads(m.read_text()).get("built", "")
                except ValueError:
                    continue
                if best is None or built > best[0]:
                    best = (built, d)
        return best[1] if best else None

    def release(self):
        f = self.folder()
        if f is None:
            return None
        with self.lock:
            r = self.release_
            try:
                mark = (f / "meta.json").stat().st_mtime_ns
            except OSError:
                return r
            if r is None or r.dir != f or r.mark != mark:
                r = self.release_ = Release(f)
            return r

    # ---- GET

    def get(self, h, path: str, query: str) -> bool:
        lang, rest = i18n.lang_of(path)
        if rest == "/api/armory/comments" and lang is None:
            return self.comments_get(h, query)
        if rest == "/api/armory/reports" and lang is None:
            return self.reports(h)
        if not (rest == "/armory" or rest.startswith("/armory/")):
            return False
        parts = [p for p in rest.split("/") if p][1:]
        if lang is None and len(parts) == 1 and parts[0] in STATIC:
            return self.static(h, parts[0])
        if lang is None and len(parts) >= 3 and parts[0] == "data":
            return self.data(h, parts[1], "/".join(parts[2:]))
        if lang is None and parts == ["sitemap.xml"]:
            return self.sitemap(h)
        r = self.release()
        # The items list was /armory itself before the home page: a list's old address goes on.
        if not parts and LIST_QUERY & set(parse_qs(query)):
            return self.redirect(h, ("" if lang is None else f"/{lang}") + f"/armory/items?{query}")
        route = self.route(r, parts)
        if route is None:
            h.send_error(404)
            return True
        # As every page of the site: an English page asked by a browser in one of the site's
        # languages is sent there (i18n.pick), the pick in the footer outranking the browser's.
        if lang is None:
            want = i18n.pick(h.headers.get("Accept-Language", ""), h.headers.get("Cookie", ""))
            picked = parse_qs(query).get("lang", [""])[0]
            if picked == "en" or picked in i18n.LANGS:
                return self.keep_pick(h, picked, rest)
            if want != "en":
                return self.redirect(h, f"/{want}{rest}" + (f"?{query}" if query else ""), vary=True)
        elif parse_qs(query).get("lang", [""])[0] in (["en"] + list(i18n.LANGS)):
            return self.keep_pick(h, parse_qs(query)["lang"][0], rest)
        page = self.page(h, r, route, lang or "en", rest).encode()
        h.send_response(200)
        h.send_header("Content-Type", "text/html; charset=utf-8")
        h.send_header("Content-Length", str(len(page)))
        h.send_header("Vary", "Accept-Language, Cookie")
        h.send_header("Cache-Control", "no-cache")
        h.end_headers()
        h.wfile.write(page)
        return True

    def keep_pick(self, h, picked, rest):
        secure = "; Secure" if h.headers.get("X-Forwarded-Proto", "") == "https" else ""
        h.send_response(302)
        h.send_header("Location", ("" if picked == "en" else f"/{picked}") + rest)
        h.send_header("Set-Cookie", f"{i18n.CHOICE}={picked}; Path=/; Max-Age=31536000; SameSite=Lax{secure}")
        h.send_header("Cache-Control", "no-store")
        h.send_header("Content-Length", "0")
        h.end_headers()
        return True

    def redirect(self, h, where, vary=False):
        h.send_response(302)
        h.send_header("Location", where)
        if vary:
            h.send_header("Vary", "Accept-Language, Cookie")
        h.send_header("Cache-Control", "no-store")
        h.send_header("Content-Length", "0")
        h.end_headers()
        return True

    def route(self, r, parts):
        """{view: list|entity, kind, slug?} or None for a page that is not there."""
        if not parts:
            return {"view": "home", "kind": "home"}
        if len(parts) == 1:
            kind = "mixes" if parts[0] == "chaos-machine" else parts[0]
            if kind in LISTS and (r is None or kind in r.kinds() or kind in ("items",)):
                return {"view": "list", "kind": kind}
            return None
        if len(parts) == 2 and parts[0] in ONE and SLUG.match(parts[1]):
            kind = ONE[parts[0]]
            if r is None or not (r.dir / kind / f"{parts[1]}.json").is_file():
                return None
            return {"view": "entity", "kind": kind, "slug": parts[1]}
        return None

    def static(self, h, name):
        f = self.site / "armory" / name
        if not f.is_file():
            h.send_error(404)
            return True
        data = f.read_bytes()
        h.send_response(200)
        h.send_header("Content-Type", (mimetypes.guess_type(name)[0] or "application/octet-stream")
                      + ("; charset=utf-8" if name.endswith((".js", ".css")) else ""))
        h.send_header("Content-Length", str(len(data)))
        # Asked with ?v=<its mtime> by the page, so a new file is a new URL.
        h.send_header("Cache-Control", "public, max-age=86400")
        h.end_headers()
        h.wfile.write(data)
        return True

    def data(self, h, commit, rel):
        """A release's file, by its commit. Only the content-addressed ones (thumbs/<hash>, icons/<hash>)
        are kept forever: a rebuild of the same commit rewrites the JSON and the charts at the same
        URLs, so those are revalidated every time (no-cache, an ETag, 304 when unchanged). Caddy
        serves the same from disk on the box (web/Caddyfile); this answers for local runs."""
        r = self.release()
        base = None
        if r is not None and commit == r.commit:
            base = r.dir
        elif self.root is not None and COMMIT.match(commit) and (self.root / commit / "meta.json").is_file():
            base = self.root / commit
        if base is None or ".." in rel.split("/"):
            h.send_error(404)
            return True
        target = (base / rel).resolve()
        if base.resolve() not in target.parents or not target.is_file():
            h.send_error(404)
            return True
        st = target.stat()
        tag = f'"{st.st_mtime_ns:x}-{st.st_size:x}"'
        hashed = HASHED.match(rel) is not None
        if not hashed and tag in [t.strip() for t in (h.headers.get("If-None-Match") or "").split(",")]:
            h.send_response(304)
            h.send_header("ETag", tag)
            h.send_header("Cache-Control", "no-cache")
            h.end_headers()
            return True
        data = target.read_bytes()
        h.send_response(200)
        h.send_header("Content-Type", {".json": "application/json", ".webp": "image/webp", ".png": "image/png"}.get(
            target.suffix, "application/octet-stream"))
        h.send_header("Content-Length", str(len(data)))
        if hashed:
            h.send_header("Cache-Control", "public, max-age=31536000, immutable")
        else:
            h.send_header("Cache-Control", "no-cache")
            h.send_header("ETag", tag)
            h.send_header("Last-Modified", time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(st.st_mtime)))
        h.send_header("Access-Control-Allow-Origin", "*")
        h.end_headers()
        h.wfile.write(data)
        return True

    def sitemap(self, h):
        r = self.release()
        urls = [f"{SITE}/armory"] + ([f"{SITE}/armory/{k}" for k in r.kinds()] if r else [])
        if r:
            for kind in r.kinds():
                one = KIND_ONE.get(kind)
                urls += [f"{SITE}/armory/{one}/{row['slug']}" for row in r.rows(kind) if row.get("slug")]
        body = ('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
                + "".join(f"<url><loc>{esc(u)}</loc></url>\n" for u in urls) + "</urlset>\n").encode()
        h.send_response(200)
        h.send_header("Content-Type", "application/xml")
        h.send_header("Content-Length", str(len(body)))
        h.end_headers()
        h.wfile.write(body)
        return True

    # ---- the page

    def entity_head(self, r, kind, e, row):
        """(title, description, image) of an entity, said from its own fields."""
        name = e.get("name") or e.get("title") or e.get("class") or ""
        img = (e.get("thumb") or {}).get("page") if isinstance(e.get("thumb"), dict) else None
        bits = []
        if kind == "items":
            st = e.get("stats") or {}
            what = (e.get("sub") or e.get("category") or "item")
            bits.append(f"{name}: {what.rstrip('s').lower() if what.endswith('s') else what.lower()} in MU Classic")
            if st.get("minimum_damage"):
                bits.append(f"damage {st['minimum_damage']} ~ {st['maximum_damage']}")
            if st.get("defense"):
                bits.append(f"defense {st['defense']}")
            if st.get("drop_level"):
                bits.append(f"drop level {st['drop_level']}")
            if e.get("classes"):
                bits.append("for " + ", ".join(e["classes"]))
            drops = e.get("drops") or []
            if drops:
                best = max(drops, key=lambda d: d.get("chance") or 0)
                c = best.get("chance") or 0
                bits.append(f"dropped by {len({d.get('monster_slug') for d in drops})} monsters"
                            + (f", best 1 in {round(1 / c):,} from {best.get('monster_name')}" if 0 < c < 0.2 else ""))
            if e.get("live") is False:
                bits.append("coming soon")
        elif kind == "monsters":
            # As the realm spawns it (the dump's `spawned`), never the content row.
            sp = next((s for s in e.get("spawned") or [] if not s.get("elite")), None) or {}
            bits.append(f"{name}: level {sp.get('level')} monster in MU Classic" if sp.get("level") else f"{name}, a monster of MU Classic")
            if sp.get("life"):
                bits.append(f"life {sp['life']:,}")
            homes = sorted({n.get("map") for n in e.get("nests") or [] if n.get("map")}) or [w.title() for w in e.get("met_in") or []]
            if homes:
                bits.append("lives in " + ", ".join(homes))
            n = sum(len(p.get("items") or []) for p in e.get("drops") or [])
            if n:
                bits.append(f"drops {n} items")
        elif kind == "npcs":
            bits.append(f"{name}, a townsperson of MU Classic")
            if e.get("services"):
                bits.append(", ".join(str(s).replace("_", " ") for s in e["services"]))
        elif kind == "quests":
            bits.append(f"{name}: a quest" + (f" given by {e['giverName']}" if e.get("giverName") else "") + " in MU Classic")
            if e.get("experience"):
                bits.append(f"{e['experience']:,} experience")
        elif kind == "events":
            bits.append(f"{name} in MU Classic: its monsters and what they drop")
        elif kind == "runes":
            bits.append(f"{name}: a {str(e.get('rarity_name') or '').lower()} rune in MU Classic")
            if e.get("tells"):
                bits.append(e["tells"])
        elif kind == "skills":
            bits.append(f"{name}: a skill in MU Classic")
            if e.get("tells"):
                bits.append(e["tells"])
        elif kind == "talents":
            bits.append(f"The {name} talent calculator for MU Classic")
        else:
            bits.append(f"{name} in MU Classic's Armory")
        if not img and row and row.get("t"):
            img = row["t"].replace(".96.webp", ".webp")
        return name, "; ".join(str(b) for b in bits if b) + ".", img

    def page(self, h, r, route, lang, rest):
        texts = h.store.texts(lang) if lang != "en" else {}
        tr = lambda s: texts.get(i18n.key(s)) or s  # noqa: E731
        kind = route["kind"]
        e, card, row = None, None, None
        title = "Armory" if kind == "home" else LIST_NAMES.get(kind, kind.title())
        desc = ("Every item, monster, townsperson, quest, event, rune, skill and talent of MU Classic, built from "
                "the game itself on every release: drop chances, shops, mixes, updates and players' comments.")
        image = None
        if r is not None and route["view"] == "entity":
            e = r.load(f"{kind}/{route['slug']}.json")
            row = next((x for x in r.rows(kind) if x.get("slug") == route["slug"]), None)
            if kind == "items" and isinstance(e.get("card"), str):
                card = r.load(e["card"])
            title, desc, image = self.entity_head(r, kind, e, row)
            if kind == "talents":
                title = f"{title} talents"
        full = "MU Classic Armory" if kind == "home" else f"{title} — MU Classic Armory"
        img_url = f"{SITE}/armory/data/{r.commit}/{image}" if (r and image) else f"{SITE}/og.jpg"
        canonical = SITE + ("" if lang == "en" else f"/{lang}") + rest
        boot = {"commit": r.commit if r else None, "protocol": r.meta.get("protocol") if r else None,
                "built": r.meta.get("built") if r else None, "data": f"/armory/data/{r.commit}/" if r else None,
                "lang": lang, "prefix": "" if lang == "en" else f"/{lang}", "kinds": r.kinds() if r else [],
                "counts": (r.meta.get("counts") or {}) if r else {}, "missing": ((r.meta.get("dump") or {}).get("missing") or []) if r else [],
                "route": route, "entity": e, "card": card}
        blob = json.dumps(boot, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
        said = {}
        if lang != "en":
            for source in i18n.script_units(self.script_text()):
                text = texts.get(i18n.key(source))
                if text and not i18n.check(source, text):
                    said[source] = text
            for source in i18n.script_units((self.site / "site.js").read_text()):
                text = texts.get(i18n.key(source))
                if text and not i18n.check(source, text):
                    said[source] = text
        said_blob = json.dumps(said, ensure_ascii=False).replace("</", "<\\/")
        v = lambda n: int((self.site / "armory" / n).stat().st_mtime) if (self.site / "armory" / n).is_file() else 0  # noqa: E731
        tag = "en" if lang == "en" else i18n.LANGS[lang][1]
        heading_font = i18n.HEADING_FONTS.get(lang, "")
        body = self.ssr(r, route, e, tr, lang)
        home = "/" if lang == "en" else f"/{lang}/"
        return f"""<!doctype html>
<html lang="{tag}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(full)}</title>
<meta name="description" content="{esc(desc)}">
<link rel="canonical" href="{esc(canonical)}">
{i18n.alternates(rest)}
<meta property="og:site_name" content="MU Classic">
<meta property="og:title" content="{esc(full)}">
<meta property="og:description" content="{esc(desc)}">
<meta property="og:url" content="{esc(canonical)}">
<meta property="og:image" content="{esc(img_url)}">
{i18n.locales(lang)}
<meta property="og:type" content="website">
<meta name="twitter:card" content="{'summary' if image else 'summary_large_image'}">
<meta name="theme-color" content="#0D0B0A">
<link rel="icon" href="/favicon.ico" sizes="48x48">
<link rel="icon" href="/favicon-32.png" type="image/png" sizes="32x32">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Cinzel:wght@500;700&family=Alegreya+Sans+SC:wght@700&family=Alegreya+Sans:wght@400;500;700&display=swap" rel="stylesheet">
{heading_font}
<link rel="stylesheet" href="/site.css">
<link rel="stylesheet" href="/armory/armory.css?v={v('armory.css')}">
</head>
<body>

<div class="bar">
<a href="{home}"><img src="/logo.svg" alt="{esc(tr('MU Classic home'))}" width="435" height="261"></a>
<span class="bar-links">{i18n.picker(rest, lang)}<a class="bar-discord" href="https://discord.gg/frqaT5k3EF" rel="noopener" target="_blank" translate="no"><svg aria-hidden="true" width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M20.3 4.4A19.7 19.7 0 0 0 15.4 3l-.6 1.3a18.3 18.3 0 0 0-5.5 0L8.6 3a19.6 19.6 0 0 0-4.9 1.5C.6 9.1-.3 13.7.1 18.2a19.8 19.8 0 0 0 6 3l1.3-2.1a12.8 12.8 0 0 1-2-1l.5-.4a14.1 14.1 0 0 0 12.2 0l.5.4c-.6.4-1.3.7-2 1l1.3 2.1a19.7 19.7 0 0 0 6-3c.5-5.2-.8-9.8-3.6-13.8zM8 15.4c-1.2 0-2.2-1.1-2.2-2.4S6.8 10.6 8 10.6s2.2 1.1 2.2 2.4-1 2.4-2.2 2.4zm8 0c-1.2 0-2.2-1.1-2.2-2.4s1-2.4 2.2-2.4 2.2 1.1 2.2 2.4-1 2.4-2.2 2.4z"/></svg><span>Discord</span></a><a id="bar-account" href="{'/account' if lang == 'en' else f'/{lang}/account'}" style="font-weight: 500">{esc(tr('Sign in'))}</a></span>
</div>

<main class="ar" id="armory">
{body}
</main>

<footer>
<div class="wrap">
<img src="/logo.svg" alt="MU Classic" width="435" height="261" translate="no">
<p>{esc(tr("To WebZen, and to MU Online fans across the years: This is a love letter, not a business. Every character, item, monster, map, sound and name in it belongs to WebZen Inc., who created MU. This is not our game; it's a tribute to theirs."))}</p>
<nav class="foot-links" aria-label="Site"><a href="{home}armory" translate="no">Armory</a><a href="/mac">Mac</a><a href="/linux">Linux</a><a href="https://discord.gg/frqaT5k3EF" rel="noopener" target="_blank" translate="no">Discord</a><a href="{'/privacy' if lang == 'en' else f'/{lang}/privacy'}">{esc(tr('Privacy'))}</a><a href="{'/contact' if lang == 'en' else f'/{lang}/contact'}">{esc(tr('Contact'))}</a></nav>
<a class="vote" href="https://nostalgic.gg/en/mu-online/mu-classic-com#vote" target="_blank" rel="noopener" translate="no"><img src="https://nostalgic.gg/api/widgets/banner/2241.svg" alt="Vote on Nostalgic.gg" width="200" height="80"></a>
{i18n.switcher(rest, lang)}
<p class="foot-legal">{esc(tr('© 2026 MU Classic · MU Online is a trademark of WebZen Inc. · Unofficial and non-commercial'))}</p>
</div>
</footer>

<script id="armory-boot" type="application/json">{blob}</script>
<script>window.MU_T = {said_blob};</script>
<script src="/site.js"></script>
<script src="/armory/card.js?v={v('card.js')}"></script>
<script src="/armory/armory.js?v={v('armory.js')}"></script>
</body>
</html>
"""

    def script_text(self) -> str:
        f = self.site / "armory" / "armory.js"
        return f.read_text() if f.is_file() else ""

    def ssr(self, r, route, e, tr, lang):
        """The page as plain HTML, for a reader without the script and for search engines: the
        trail, the name, the entity's facts and its links. armory.js draws over it."""
        pre = "" if lang == "en" else f"/{lang}"
        if r is None:
            return f'<div class="awrap"><p class="loading">{esc(tr("The Armory has not been built yet."))}</p></div>'
        kind = route["kind"]
        trail = [f'<li><a class="crumb" href="{pre}/armory">Armory</a></li>']
        out = []
        if route["view"] == "home":
            trail = [f'<li><span class="here">Armory</span></li>']
            out.append('<h1 class="h1 big">Armory</h1><ul>')
            for k in r.kinds():
                out.append(f'<li><a href="{pre}/armory/{k}">{esc(LIST_NAMES.get(k, k))}</a> {len(r.rows(k))}</li>')
            out.append("</ul>")
        elif route["view"] == "list":
            trail.append(f'<li><span class="here">{esc(LIST_NAMES.get(kind, kind))}</span></li>')
            one = KIND_ONE.get(kind)
            out.append(f'<h1 class="h1">{esc(LIST_NAMES.get(kind, kind))}</h1><ul>')
            for row in r.rows(kind):
                out.append(f'<li><a href="{pre}/armory/{one}/{esc(row.get("slug"))}">{esc(row.get("n"))}</a></li>')
            out.append("</ul>")
        else:
            trail.append(f'<li><a class="crumb" href="{pre}/armory/{kind}">{esc(LIST_NAMES.get(kind, kind))}</a></li>')
            name = e.get("name") or e.get("title") or e.get("class") or route["slug"]
            trail.append(f'<li><span class="here">{esc(name)}</span></li>')
            out.append(f'<h1 class="h1 big">{esc(name)}</h1>')
            facts = []
            # A monster's numbers as the realm spawns it, never its content row.
            sp = next((x for x in e.get("spawned") or [] if not x.get("elite")), None)
            shown = ({"level": sp.get("level"), "life": sp.get("life"), "defense": sp.get("defense")} if sp
                     else {} if kind == "monsters" else (e.get("stats") or e.get("guard") or {}))
            for k, v in shown.items():
                if isinstance(v, (int, float, str)) and not isinstance(v, bool) and v not in ("", None):
                    facts.append(f"<li>{esc(k.replace('_', ' '))}: {esc(v)}</li>")
            for k in ("tells", "giverName", "rarity_name", "role"):
                if isinstance(e.get(k), str) and e[k] and e[k].lower() not in ("none", "null"):
                    facts.append(f"<li>{esc(e[k])}</li>")
            if facts:
                out.append("<ul>" + "".join(facts[:24]) + "</ul>")
            links = []
            for d in (e.get("drops") or [])[:60]:
                if d.get("monster_slug"):
                    links.append(f'<li><a href="{pre}/armory/monster/{esc(d["monster_slug"])}">{esc(d.get("monster_name"))}</a></li>')
                for it in d.get("items") or []:
                    if it.get("slug"):
                        links.append(f'<li><a href="{pre}/armory/item/{esc(it["slug"])}">{esc(it.get("n"))}</a></li>')
            if links:
                out.append("<ul>" + "".join(dict.fromkeys(links)) + "</ul>")
        return (f'<div class="awrap" id="ssr"><nav aria-label="Breadcrumb"><ol class="crumbs">{"".join(trail)}</ol></nav>'
                + "".join(out) + "</div>")

    # ---- comments

    def made_tables(self, store):
        if not self.made:
            store.db().executescript(SCHEMA)
            self.made = True

    def thing(self, kind, slug):
        r = self.release()
        return (r is not None and kind in LISTS and isinstance(slug, str) and SLUG.match(slug or "")
                and (r.dir / kind / f"{slug}.json").is_file()), r

    def comments_get(self, h, query):
        q = parse_qs(query)
        kind, slug = q.get("kind", [""])[0], q.get("slug", [""])[0]
        there, _ = self.thing(kind, slug)
        if not there:
            h.reply(404, {"error": "no such page"})
            return True
        self.made_tables(h.store)
        me = h.who()
        mine = me[0] if me else -1
        admin = bool(me and me[3] == "admin")
        rows = h.store.db().execute(
            "SELECT c.id, a.name, c.text, c.at, c.about, c.account_id, "
            "(SELECT count(*) FROM armory_votes v WHERE v.comment_id = c.id), "
            "(SELECT count(*) FROM armory_votes v WHERE v.comment_id = c.id AND v.account_id = ?), "
            "(SELECT count(*) FROM armory_reports p WHERE p.comment_id = c.id), "
            "(SELECT count(*) FROM armory_reports p WHERE p.comment_id = c.id AND p.account_id = ?) "
            "FROM armory_comments c JOIN accounts a ON a.id = c.account_id "
            "WHERE c.kind = ? AND c.slug = ? AND c.gone = 0 ORDER BY c.at DESC LIMIT 500",
            (mine, mine, kind, slug)).fetchall()
        out = [{"id": i, "name": n or "?", "text": t, "at": at, "about": ab, "votes": v, "mine": bool(m),
                "own": acc == mine, "reported": bool(rp)} | ({"reports": rc} if admin else {})
               for i, n, t, at, ab, acc, v, m, rc, rp in rows]
        h.reply(200, {"comments": out, "me": {"name": me[2] or "", "admin": admin} if me else None})
        return True

    def reports(self, h):
        row = h.who()
        if row is None or row[3] != "admin":
            h.reply(404, {"error": "not found"})
            return True
        self.made_tables(h.store)
        rows = h.store.db().execute(
            "SELECT c.id, c.kind, c.slug, c.text, a.name, count(p.account_id), max(p.at), group_concat(p.why, ' | ') "
            "FROM armory_reports p JOIN armory_comments c ON c.id = p.comment_id JOIN accounts a ON a.id = c.account_id "
            "WHERE c.gone = 0 GROUP BY c.id ORDER BY max(p.at) DESC LIMIT 200").fetchall()
        h.reply(200, {"reports": [{"id": i, "kind": k, "slug": s, "text": t, "name": n, "count": c, "at": at, "why": w or ""}
                                  for i, k, s, t, n, c, at, w in rows]})
        return True

    def post(self, h, path) -> bool:
        if path == "/api/armory/comments":
            return self.comment(h)
        if path == "/api/armory/vote":
            return self.vote(h)
        if path == "/api/armory/report":
            return self.report(h)
        if path == "/api/armory/delete":
            return self.delete(h)
        return False

    def comment(self, h):
        row = h.signed()
        if row is None:
            return True
        said = h.body(8192) or {}
        kind, slug = str(said.get("kind", "")), str(said.get("slug", ""))
        text, about = str(said.get("text", "")).strip(), str(said.get("about", "") or "")
        there, r = self.thing(kind, slug)
        if not there:
            h.reply(400, {"error": "no such page"})
            return True
        if about:
            hist = r.load(f"history/{kind}/{slug}.json") or {}
            if not COMMIT.match(about) or about not in {x.get("commit") for x in hist.get("releases") or []}:
                h.reply(400, {"error": "no such update"})
                return True
        if not text or len(text) > MOST_TEXT:
            h.reply(400, {"error": f"Write 1 to {MOST_TEXT} characters."})
            return True
        if not row[2]:
            h.reply(400, {"error": "Pick a name first."})
            return True
        if not COMMENT_LIMIT.allow(str(row[0])) or not COMMENT_IP_LIMIT.allow(h.client()):
            h.reply(429, {"error": "Slow down a little."})
            return True
        self.made_tables(h.store)
        cur = h.store.db().execute(
            "INSERT INTO armory_comments (kind, slug, about, account_id, text, at) VALUES (?, ?, ?, ?, ?, ?)",
            (kind, slug, about, row[0], text, int(time.time())))
        h.reply(201, {"ok": True, "id": cur.lastrowid})
        return True

    def one(self, h):
        said = h.body() or {}
        try:
            cid = int(said.get("id"))
        except (TypeError, ValueError):
            return None, said
        self.made_tables(h.store)
        got = h.store.db().execute("SELECT id, account_id, gone FROM armory_comments WHERE id = ?", (cid,)).fetchone()
        return (got if got and not got[2] else None), said

    def vote(self, h):
        row = h.signed()
        if row is None:
            return True
        c, _ = self.one(h)
        if c is None:
            h.reply(404, {"error": "no such comment"})
            return True
        if not VOTE_LIMIT.allow(str(row[0])):
            h.reply(429, {"error": "Slow down a little."})
            return True
        db = h.store.db()
        if c[1] == row[0]:
            h.reply(400, {"error": "Not your own."})
            return True
        had = db.execute("SELECT 1 FROM armory_votes WHERE comment_id = ? AND account_id = ?", (c[0], row[0])).fetchone()
        if had:
            db.execute("DELETE FROM armory_votes WHERE comment_id = ? AND account_id = ?", (c[0], row[0]))
        else:
            db.execute("INSERT INTO armory_votes VALUES (?, ?)", (c[0], row[0]))
        n = db.execute("SELECT count(*) FROM armory_votes WHERE comment_id = ?", (c[0],)).fetchone()[0]
        h.reply(200, {"ok": True, "votes": n, "mine": not had})
        return True

    def report(self, h):
        row = h.signed()
        if row is None:
            return True
        c, said = self.one(h)
        if c is None:
            h.reply(404, {"error": "no such comment"})
            return True
        if not REPORT_LIMIT.allow(str(row[0])):
            h.reply(429, {"error": "Slow down a little."})
            return True
        h.store.db().execute("INSERT OR IGNORE INTO armory_reports VALUES (?, ?, ?, ?)",
                             (c[0], row[0], str(said.get("why", ""))[:200], int(time.time())))
        h.reply(200, {"ok": True})
        return True

    def delete(self, h):
        row = h.signed()
        if row is None:
            return True
        c, _ = self.one(h)
        if c is None or (c[1] != row[0] and row[3] != "admin"):
            h.reply(404, {"error": "no such comment"})
            return True
        h.store.db().execute("UPDATE armory_comments SET gone = ?, gone_by = ? WHERE id = ?",
                             (1 if c[1] == row[0] else 2, row[0], c[0]))
        h.reply(200, {"ok": True})
        return True


def default_root(here: Path):
    """Locally, the repository's out/armory/ (its newest build)."""
    return here.parent / "out" / "armory"
