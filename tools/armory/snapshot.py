"""The release's tree and data: what the Armory is built from, never the working tree.

The commit is the live release's (https://mu-classic.com/game/<platform>/manifest.json `version`,
all three platforms agreeing) unless --commit names one. Its tree is `git archive`d into
out/armory/src/<commit>/ as dist.py snapshot() does, with third_party/extern linked (git has
none of it). The tables are the release's own packs, dist/macos/MU2.app/Contents/Resources/data,
and dist/macos/BUILT_FROM must name the commit, or the build stops.
"""
import json
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

from common import ARMORY, ROOT

sys.path.append(str(ROOT / "tools" / "mu"))
import release  # noqa: E402

SITE = "https://mu-classic.com/game/{}/manifest.json"
PLATFORMS = ("macos", "windows", "linux")
DATA = ROOT / "dist/macos/MU2.app/Contents/Resources/data"
BUILT_FROM = ROOT / "dist/macos/BUILT_FROM"
EXTERN = ROOT / "third_party/extern"


class Stop(RuntimeError):
    """The build cannot go on; the message says why and what to do."""


def git(*args):
    done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        raise Stop(f"git {' '.join(args)}: {done.stderr.strip()}")
    return done.stdout.strip()


def live_commit():
    seen = {}
    for p in PLATFORMS:
        try:
            with urllib.request.urlopen(SITE.format(p), timeout=20) as r:
                seen[p] = json.load(r)["version"]
        except Exception as e:  # noqa: BLE001 - any failure stops with its reason
            raise Stop(f"cannot read the {p} manifest ({e}); pass --commit")
    if len(set(seen.values())) != 1:
        raise Stop("the platforms are on different releases: " +
                   ", ".join(f"{p} {c}" for p, c in seen.items()) + "; pass --commit")
    return next(iter(seen.values()))


def short(ref):
    return git("rev-parse", "--short=8", "--verify", ref + "^{commit}")


def data_dir(commit):
    """The release's packs, checked to be this commit's."""
    if not BUILT_FROM.exists() or not DATA.is_dir():
        raise Stop(f"no {DATA.relative_to(ROOT)}: `./mu dist macos --commit {commit}` first")
    built = BUILT_FROM.read_text().split()[0] if BUILT_FROM.read_text().split() else ""
    if not built or not (commit.startswith(built) or built.startswith(commit)):
        raise Stop(f"dist/macos is built from {built or 'nothing'}, not {commit}: its packs are another "
                   f"release's. `./mu dist macos --commit {commit}` first, or --commit {built}")
    return DATA


def snapshot(commit, say):
    """out/armory/src/<commit>/: the commit's tree, made once and kept."""
    dest = ARMORY / "src" / commit
    stamp = dest / "COMMIT"
    if stamp.exists():
        say(f"snapshot: out/armory/src/{commit}/ (kept)")
    else:
        staged = Path(str(dest) + ".new")
        shutil.rmtree(staged, ignore_errors=True)
        staged.mkdir(parents=True)
        archive = subprocess.run(["git", "archive", "--format=tar", commit, "--", ".", ":(exclude)source",
                                  ":(exclude)docs"], cwd=ROOT, capture_output=True)
        if archive.returncode != 0:
            raise Stop(archive.stderr.decode().strip())
        subprocess.run(["tar", "-x", "-C", str(staged)], input=archive.stdout, check=True)
        line = git("log", "-1", "--format=%h %cs", commit)
        (staged / "COMMIT").write_text(line + "\n")
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(staged, dest)
        say(f"snapshot: {line} into out/armory/src/{commit}/")
    link = dest / "third_party" / "extern"
    if not link.is_symlink() and not link.exists():
        link.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(os.path.relpath(EXTERN, link.parent), link)
    return dest


def protocol(commit):
    return release.protocol_of(commit)


def released_before(commit):
    """The newest Armory build under out/armory/ whose commit is an ancestor of this one."""
    best = None
    for meta in ARMORY.glob("*/meta.json"):
        other = meta.parent.name
        if other == commit:
            continue
        ok = subprocess.run(["git", "merge-base", "--is-ancestor", other, commit], cwd=ROOT).returncode == 0
        if not ok:
            continue
        if best is None or subprocess.run(["git", "merge-base", "--is-ancestor", best, other],
                                          cwd=ROOT).returncode == 0:
            best = other
    return best
