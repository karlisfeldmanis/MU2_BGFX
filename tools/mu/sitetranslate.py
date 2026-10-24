"""The site's untranslated lines translated by claude -p and put in (web/server.py's
/internal/untranslated and /internal/translations, over ssh to the Helsinki box).

    python3 sitetranslate.py [--dry] [--lang pt,lv] [--web http://127.0.0.1:8090]

Standard library only and nothing else of the project, so launchd can run a copy kept outside
~/Documents (macOS keeps background jobs out of it): `./mu ship translate --install` puts this file
and tools/mu/translate.plist in place; `./mu ship translate` runs it from here.
"""

import json
import subprocess
import sys

WEB_HOST = "hetzner"     # ~/.ssh/config: the Helsinki box, where the site and its database are


class Site:
    """The site's /internal/ routes: on the box over ssh, or a local web/server.py (web=URL)."""

    def __init__(self, web=None):
        self.web = web

    def get(self, path):
        if self.web:
            done = subprocess.run(["curl", "-sf", "--max-time", "60", f"{self.web}{path}"], capture_output=True, text=True)
        else:
            done = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", WEB_HOST,
                                   f"curl -sf http://127.0.0.1:8080{path}"], capture_output=True, text=True, timeout=90)
        if done.returncode != 0 or not done.stdout:
            raise RuntimeError(f"the site said nothing: {done.stderr.strip()[:200]}")
        return json.loads(done.stdout)

    def post(self, body, path):
        data = json.dumps(body)
        if self.web:
            done = subprocess.run(["curl", "-sf", "-H", "Content-Type: application/json", "--data-binary", "@-",
                                   f"{self.web}{path}"], input=data, capture_output=True, text=True)
        else:
            done = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", WEB_HOST,
                                   "curl -sf -H 'Content-Type: application/json' --data-binary @- "
                                   f"http://127.0.0.1:8080{path}"], input=data, capture_output=True, text=True, timeout=90)
        if done.returncode != 0:
            raise RuntimeError(f"the site took no lines: {done.stderr.strip() or done.stdout.strip()}")
        return json.loads(done.stdout)


TRANSLATING = """You translate the website of MU Classic, a free remaster of the classic MMORPG MU Online (a new
engine, client and server, not a private server), from English into {language}.

Each item is one piece of the site: a heading, a button, a sentence, or a line of the game's patch
notes. Translate each so a native {language} speaker who plays MMORPGs reads it as natural,
idiomatic text written in {language}, never word for word from English: say what the English
means the way {language} says it.

Rules:
- Keep every HTML tag exactly as it is in the English, with the same attributes, and put the
  words inside and around them where {language} needs them.
- Keep every {{placeholder}} in braces exactly as written.
- The game's own names stay in English, exactly as written: maps and towns (Lorencia, Noria,
  Devias, Lost Tower, Icarus, Aida, Kanturu, Blood Castle, Devil Square), classes, skills, items,
  jewels, runes, monsters, NPCs, pets, buttons and messages the game shows in English (Play, Helper,
  Broker, Jeweler, Chaos Machine, Zen, MISS), and anything in quotes. Only names: ordinary words
  (ring, pendant, sword, arrow, jewel, party, guild, map, monster) are translated as usual.
- Patch notes are short and plain, in the voice of the examples. Do not add or leave out facts.
- Use the same words for the same things as the examples do.

Examples of this site's {language}:
{examples}

Translate each item's text; answer with every id.

{items}
"""

TRANSLATING_SCHEMA = {"type": "object", "required": ["translations"], "properties": {"translations": {
    "type": "array", "items": {"type": "object", "required": ["id", "text"],
                               "properties": {"id": {"type": "integer"}, "text": {"type": "string"}}}}}}
TRANSLATE_BATCH = 60


def translated(language, sources, examples, say):
    """{source: text} for each of `sources` claude -p translated; {} when it gave nothing."""
    import html
    import tempfile
    items = [{"id": n, "text": src if "<" in src else html.unescape(src)} for n, src in enumerate(sources)]
    prompt = TRANSLATING.format(language=language, items=json.dumps(items, ensure_ascii=False),
                                examples="\n".join(f"EN: {html.unescape(s)}\n-> {t}" for s, t in examples) or "(none yet)")
    # Run from an empty folder: the project's CLAUDE.md and memory are not this job's.
    with tempfile.TemporaryDirectory() as here:
        done = subprocess.run(["claude", "-p", "--model", "opus", "--output-format", "json", "--tools", "",
                               "--json-schema", json.dumps(TRANSLATING_SCHEMA)],
                              input=prompt, capture_output=True, text=True, cwd=here, timeout=1200)
    try:
        out = json.loads(done.stdout)["structured_output"]["translations"]
    except (ValueError, KeyError, TypeError):
        say(f"  claude -p gave nothing for {language} ({(done.stderr or done.stdout).strip()[:160]})")
        return {}
    return {sources[t["id"]]: " ".join(str(t.get("text", "")).split())
            for t in out if isinstance(t.get("id"), int) and 0 <= t["id"] < len(sources) and t.get("text")}


def translate(say, web=None, dry=False, langs=None):
    """Each language's units with no translation, translated and put in (the site keeps only those
    that keep their tags, and only where there is still none)."""
    import shutil
    site = Site(web)
    told = site.get("/internal/untranslated")
    missing = {lang: units for lang, units in told["missing"].items() if units and (not langs or lang in langs)}
    if not missing:
        say("every language has every line")
        return True, {"put": 0}
    say("missing: " + ", ".join(f"{lang} {len(units)}" for lang, units in missing.items()))
    if dry:
        return True, {"missing": missing}
    if not shutil.which("claude"):
        raise RuntimeError("claude is not on the PATH: Claude Code translates the site")
    put, failed = 0, []
    for lang, units in missing.items():
        for n in range(0, len(units), TRANSLATE_BATCH):
            # Each batch sent as it is done: a failure later loses only its own batch.
            try:
                texts = translated(told["langs"][lang], units[n:n + TRANSLATE_BATCH], told["examples"][lang], say)
                got = site.post({"texts": {lang: texts}}, "/internal/translations") if texts else {"put": 0, "refused": 0}
            except (RuntimeError, subprocess.TimeoutExpired) as e:
                failed.append(lang)
                say(f"  {lang}: {e}")
                continue
            put += got["put"]
            say(f"  {lang}: {got['put']} put in" + (f", {got['refused']} refused" if got["refused"] else ""))
    if failed:
        say(f"not done: {', '.join(sorted(set(failed)))}; the next run tries them again")
    return not failed, {"put": put}




def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dry", action="store_true", help="say what is missing, translate nothing")
    ap.add_argument("--lang", help="only these languages, comma between (pt,lv)")
    ap.add_argument("--web", help="a local web/server.py instead of the box")
    args = ap.parse_args()
    ok, _ = translate(print, web=args.web, dry=args.dry, langs=args.lang.split(",") if args.lang else None)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
