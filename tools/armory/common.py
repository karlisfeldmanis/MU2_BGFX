"""What every Armory step shares: the folders, the stamp, slugs, hashes and the JSON writer.

Every extractor reads the release's snapshot (out/armory/src/<commit>/), never the working tree,
and writes only under the build's output folder or out/armory/cache/.
"""
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.append(str(TOOLS))
from paths import OUT, ROOT  # noqa: E402

ARMORY = Path(OUT) / "armory"
CACHE = ARMORY / "cache"
ROOT = Path(ROOT)

# Provenance words in content/ (where a number came from, why): not what a page shows.
_PROVENANCE = re.compile(r"(^|_)(note|from|call|why|notes)$|_note_|^why_")


def slug(text):
    s = re.sub(r"[^a-z0-9]+", "-", str(text).lower().replace("'", "")).strip("-")
    return s or "x"


def plain(d):
    """A content dict without its provenance fields (notes, *_from, call), recursively."""
    if isinstance(d, dict):
        return {k: plain(v) for k, v in d.items() if not _PROVENANCE.search(k)}
    if isinstance(d, list):
        return [plain(v) for v in d]
    return d


def load(path):
    with open(path) as h:
        return json.load(h)


def write(path, data, stamp=None):
    """Compact JSON, stamped with the release's commit and protocol when given."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if stamp is not None and isinstance(data, dict):
        data = {"commit": stamp["commit"], "protocol": stamp["protocol"], **data}
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w") as h:
        json.dump(data, h, separators=(",", ":"), ensure_ascii=False, sort_keys=False)
    os.replace(tmp, path)
    return path.stat().st_size


def file_hash(paths, extra=b""):
    """sha256 over the files' bytes (in the order given) and `extra`; the first 16 hex digits."""
    h = hashlib.sha256()
    for p in paths:
        p = Path(p)
        h.update(p.name.encode() + b"\0")
        if p.exists():
            with open(p, "rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    h.update(block)
        else:
            h.update(b"<missing>")
        h.update(b"\0")
    h.update(extra)
    return h.hexdigest()[:16]


def source_hash(*files):
    """The converter's own source, so a change to it reconverts what it made."""
    return file_hash([HERE / f for f in files])


def place(src, dst):
    """A cached file into the output: hard-linked when the disk allows, else copied."""
    dst = Path(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        return
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


class Slugs:
    """Stable slugs: a key keeps the slug the release before gave it while its name's slug is
    unchanged; a new key whose name is taken gets `<name>-<key>`. Deterministic: keys are taken in
    sorted order, the earlier releases' first."""

    def __init__(self, before=None):
        self.before = before or {}   # kind -> {key: slug}
        self.taken = {}              # kind -> {slug: key}
        self.of = {}                 # kind -> {key: slug}

    def assign(self, kind, pairs):
        """pairs: [(key, name)]. Returns {key: slug}."""
        before = self.before.get(kind, {})
        taken = self.taken.setdefault(kind, {})
        out = self.of.setdefault(kind, {})
        pairs = sorted(pairs, key=lambda kv: str(kv[0]))
        rest = []
        for key, name in pairs:
            old = before.get(str(key))
            base = slug(name)
            if old and (old == base or old.startswith(base + "-")) and old not in taken:
                taken[old] = key
                out[key] = old
            else:
                rest.append((key, name))
        for key, name in rest:
            s = slug(name)
            if s in taken:
                s = f"{s}-{slug(key)}"
            n = 2
            while s in taken:
                s = f"{slug(name)}-{slug(key)}-{n}"
                n += 1
            taken[s] = key
            out[key] = s
        return out
