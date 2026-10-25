"""The release's tree and data: what the Armory is built from, never the working tree.

The commit is the live release's (https://mu-classic.com/game/<platform>/manifest.json `version`,
all three platforms agreeing) unless --commit names one. Its tree is `git archive`d into
out/armory/src/<commit>/ as dist.py snapshot() does, with third_party/extern linked (git has
none of it). The tables are the release's own packs (releases.py): out/armory/packs/<commit>/,
kept from dist/macos when its BUILT_FROM is the commit, else fetched from the release's manifest;
with neither, the build stops. The release before a commit is releases.json's, never git's.
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


def data_dir(commit, say=print):
    """The release's packs, checked to be this commit's: out/armory/packs/<commit>/ (releases.py)."""
    import releases
    found = releases.data_for(commit, say)
    if found is None:
        built = releases.built_from()
        raise Stop(f"no packs of {commit}: out/armory/packs/{commit}/ is empty, dist/macos is built from "
                   f"{built or 'nothing'} and the release's manifest has no world packs left. "
                   f"`./mu dist macos --commit {commit}` first")
    return found


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
    """The release players had before this one, by out/armory/releases.json (releases.py): a
    release is often a hotfix branch, so git ancestry does not say it. None for the first."""
    import releases
    return releases.previous(commit)
