#!/usr/bin/env python3
"""The releases the Armory follows, in the order players had them, and each one's packs.

    python3 tools/armory/releases.py list
    python3 tools/armory/releases.py seed --since 2b237b5c     the box's own order, from that release on
    python3 tools/armory/releases.py record <commit>           one more, as `mu ship switch` does
    python3 tools/armory/releases.py packs <commit>            its packs kept (dist) or fetched (manifest)

**The order** is out/armory/releases.json, never git ancestry: a release is often a hotfix branch off
an older live commit (5d7ddebc and 2b237b5c are on no ancestor of main), so the release before a
commit is the one listed before it. It is seeded from the box itself -- each release's manifest,
`/srv/mu2-web/game/macos/manifests/<commit>.json`, by when it was put there (tools/mu/release.py
writes it as the manifest turns) -- and `mu ship switch` records each new one.

**The packs** a release's Armory is built from are kept per commit in out/armory/packs/<commit>/,
so a release can be built again after dist/macos has moved on: copied (APFS clones) from dist/macos
when its BUILT_FROM is that commit, else fetched from the release's own manifest
(SITE/game/macos/manifests/<commit>.json, each file at its files_url by sha256, checked). The dump
reads the worlds' tables only, so the world packs and common.mupak are what is kept. The box keeps
the files of its newest few releases only (release.py KEEP); an older one's are gone, and its
Armory cannot be built.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import ARMORY, ROOT  # noqa: E402

RELEASES = ARMORY / "releases.json"
PACKS = ARMORY / "packs"
SITE = "https://mu-classic.com"
HOST = "hetzner"
MANIFESTS = "/srv/mu2-web/game/macos/manifests"
DIST = ROOT / "dist/macos"
DIST_DATA = DIST / "MU2.app/Contents/Resources/data"


def wanted(name):
    """A pack the dump reads: a world's, and the common one."""
    return name.endswith(".mupak") and (name.startswith("world-") or name == "common.mupak")


# ---------------------------------------------------------------- the order


def load():
    try:
        return json.loads(RELEASES.read_text()).get("releases") or []
    except (OSError, ValueError):
        return []


def save(rows):
    RELEASES.parent.mkdir(parents=True, exist_ok=True)
    tmp = RELEASES.with_suffix(".json.tmp")
    tmp.write_text(json.dumps({"note": "the releases in the order players had them (tools/armory/releases.py)",
                               "releases": rows}, indent=1) + "\n")
    os.replace(tmp, RELEASES)


def commits():
    return [r["commit"] for r in load()]


def previous(commit):
    """The release players had before `commit`: the one listed before it, or, for a commit not
    listed yet (built at prepare, before its switch), the last listed that is not it."""
    order = commits()
    if commit in order:
        at = order.index(commit)
        return order[at - 1] if at > 0 else None
    rest = [c for c in order if c != commit]
    return rest[-1] if rest else None


def manifest(commit, platform="macos"):
    url = f"{SITE}/game/{platform}/manifests/{commit}.json"
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return json.load(r)
    except (urllib.error.URLError, ValueError, TimeoutError):
        return None


def from_box():
    """[(commit, when it went live)] oldest first: the box's manifests by their time."""
    said = subprocess.run(["ssh", "-o", "BatchMode=yes", HOST,
                           f"cd {MANIFESTS} && stat -c '%Y %n' *.json"], capture_output=True, text=True, timeout=30)
    if said.returncode != 0:
        raise RuntimeError(f"ssh {HOST}: {said.stderr.strip()}")
    rows = []
    for line in said.stdout.splitlines():
        stamp, name = line.split(maxsplit=1)
        rows.append((int(stamp), name[:-len(".json")]))
    rows.sort()
    return [(c, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t))) for t, c in rows]


def row(commit, live_at, how):
    m = manifest(commit) or {}
    return {"commit": commit, "protocol": m.get("protocol"), "built": m.get("built"), "live_at": live_at, "from": how}


def seed(since, say=print):
    """releases.json from the box's order, `since` and every release after it."""
    order = from_box()
    names = [c for c, _ in order]
    if since not in names:
        raise SystemExit(f"{since} is not among the box's manifests: {' '.join(names)}")
    rows = [row(c, t, "the box's manifests") for c, t in order[names.index(since):]]
    save(rows)
    say("releases: " + " -> ".join(r["commit"] for r in rows))
    return rows


def record(commit, say=print):
    """`commit` as the newest release, unless it already is."""
    rows = load()
    if rows and rows[-1]["commit"] == commit:
        return rows
    rows = [r for r in rows if r["commit"] != commit]
    rows.append(row(commit, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "mu ship switch"))
    save(rows)
    say(f"releases: {commit} recorded after {rows[-2]['commit'] if len(rows) > 1 else '(none)'}")
    return rows


# ---------------------------------------------------------------- the packs


def built_from():
    try:
        words = (DIST / "BUILT_FROM").read_text().split()
    except OSError:
        return ""
    return words[0] if words else ""


def same(a, b):
    return bool(a) and bool(b) and (a.startswith(b) or b.startswith(a))


def kept(commit):
    """out/armory/packs/<commit>/ when it holds a world pack."""
    d = PACKS / commit
    return d if d.is_dir() and any(p.name.startswith("world-") for p in d.glob("*.mupak")) else None


def keep(commit, say=print):
    """dist/macos's packs into out/armory/packs/<commit>/ when dist is that commit's (clones, so
    no space is taken on APFS). Returns the folder, or None."""
    if kept(commit):
        return kept(commit)
    if not same(built_from(), commit) or not DIST_DATA.is_dir():
        return None
    staged = PACKS / f"{commit}.new"
    shutil.rmtree(staged, ignore_errors=True)
    staged.mkdir(parents=True)
    for p in sorted(DIST_DATA.iterdir()):
        if wanted(p.name):
            if subprocess.run(["cp", "-c", str(p), str(staged / p.name)], capture_output=True).returncode != 0:
                shutil.copy2(p, staged / p.name)
    (staged / "FROM").write_text(f"dist/macos, BUILT_FROM {built_from()}\n")
    shutil.rmtree(PACKS / commit, ignore_errors=True)
    os.replace(staged, PACKS / commit)
    say(f"packs: dist/macos's kept in out/armory/packs/{commit}/")
    return PACKS / commit


def fetch(commit, say=print):
    """The release's packs from its manifest's files, each checked by its sha256. Returns the folder,
    or None when the manifest or any world pack is gone from the box."""
    if kept(commit):
        return kept(commit)
    m = manifest(commit)
    if not m:
        say(f"packs: no manifest for {commit} at {SITE}/game/macos/manifests/")
        return None
    url = m.get("files_url") or f"{SITE}/files/game/macos/files/"
    staged = PACKS / f"{commit}.new"
    staged.mkdir(parents=True, exist_ok=True)
    gone = []
    for f in m["files"]:
        name = f["path"].rsplit("/", 1)[-1]
        if not ("/data/" in f["path"] and wanted(name)):
            continue
        dest = staged / name
        if dest.is_file() and dest.stat().st_size == f["size"]:
            continue
        part = staged / (name + ".part")
        try:
            with urllib.request.urlopen(url + f["sha256"], timeout=120) as r, open(part, "wb") as out:
                h = hashlib.sha256()
                while chunk := r.read(1 << 20):
                    h.update(chunk)
                    out.write(chunk)
        except urllib.error.HTTPError as e:
            part.unlink(missing_ok=True)
            gone.append(f"{name} ({e.code})")
            continue
        if h.hexdigest() != f["sha256"]:
            part.unlink(missing_ok=True)
            raise RuntimeError(f"packs: {name} of {commit} is not its manifest's sha256")
        os.replace(part, dest)
        say(f"packs: {name} of {commit} fetched")
    worlds_gone = [g for g in gone if g.startswith("world-")]
    if worlds_gone:
        say(f"packs: {commit}'s {', '.join(worlds_gone)} are gone from the box; its Armory cannot be built")
        return None
    (staged / "FROM").write_text(f"{url} (manifests/{commit}.json)" + (f"; not there: {', '.join(gone)}" if gone else "") + "\n")
    shutil.rmtree(PACKS / commit, ignore_errors=True)
    os.replace(staged, PACKS / commit)
    return PACKS / commit


def data_for(commit, say=print):
    """The release's packs: kept, else dist/macos's (kept as they are read), else fetched."""
    return kept(commit) or keep(commit, say) or fetch(commit, say)


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("action", choices=["list", "seed", "record", "packs"])
    ap.add_argument("commit", nargs="?")
    ap.add_argument("--since", help="seed: the first release to list")
    a = ap.parse_args()
    if a.action == "list":
        for r in load():
            print(f"{r['commit']}  protocol {r.get('protocol')}  live {r.get('live_at')}  {'built' if (ARMORY / r['commit'] / 'meta.json').is_file() else '-'}"
                  f"  {'packs' if kept(r['commit']) else '-'}")
    elif a.action == "seed":
        seed(a.since or (a.commit or ""))
    elif a.action == "record":
        record(a.commit)
    elif a.action == "packs":
        print(data_for(a.commit) or "none")


if __name__ == "__main__":
    main()
