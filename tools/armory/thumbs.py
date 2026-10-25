"""Thumbnails of every item, monster and townsfolk figure, rendered offscreen from the cached GLBs.

No game client: headless Chrome draws each GLB with three.js (thumbs/page.html), driven by
thumbs/render.js over the DevTools protocol. Each picture is content-addressed: its hash is the
GLB's hash, the view, the Elite's gold and the renderer's own source and three.js version, so a
model that did not change is not drawn again. Kept in out/armory/cache/thumbs/ and placed in the
output as thumbs/<hash>.webp (512 px, transparent) and thumbs/<hash>.96.webp (the item card's
top-right picture).

An Elite is drawn in the game's gold: src/game/play_tuning.h kEliteLight (its body's light) and
kEliteChrome (the +9 pass), read from the snapshot so the site follows the game.
"""
import hashlib
import json
import re
import subprocess
import urllib.request
from pathlib import Path

from common import CACHE, HERE, file_hash, place

THREE = "0.160.0"
# Pinned by version and by content: a CDN that changed a file under the same version stops the build.
VENDOR = {
    "build/three.module.js": "76dea8151bc9352aef3528b4262e249b2604f62543828328db978d060d61a495",
    "examples/jsm/loaders/GLTFLoader.js": "d073b438e6a07e1359741dd5d6c76c953420cc0d4fd84eb1bdde94315540e6a3",
    "examples/jsm/utils/BufferGeometryUtils.js": "9be041e96308775d00e2695cc607645b9a9b64fd7c0e759dd8f7c00a8d92becb",
    "examples/jsm/environments/RoomEnvironment.js": "e21f41b7ef2016f2984a5d27850b42dffa797a6a194a138e288b06576d765a7b",
}
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
BATCH = 60


def vendor():
    root = CACHE / "vendor" / f"three-{THREE}"
    for rel, want in VENDOR.items():
        p = root / rel
        if not p.exists():
            p.parent.mkdir(parents=True, exist_ok=True)
            url = f"https://cdn.jsdelivr.net/npm/three@{THREE}/{rel}"
            with urllib.request.urlopen(url, timeout=60) as r:
                p.write_bytes(r.read())
        got = hashlib.sha256(p.read_bytes()).hexdigest()
        if got != want:
            p.unlink()
            raise RuntimeError(f"three.js {rel} is not the pinned file (sha256 {got[:12]}, want {want[:12]})")
    return root


def elite_gold(src):
    text = (src / "src/game/play_tuning.h").read_text()
    out = {}
    for name in ("kEliteLight", "kEliteChrome"):
        m = re.search(r"constexpr float %s\[3\] = \{([^}]*)\};" % name, text)
        if not m:
            raise RuntimeError(f"play_tuning.h no longer says {name}: thumbs.py draws the Elite's gold from it")
        out[name] = [float(x.strip().rstrip("f")) for x in m.group(1).split(",")]
    return {"light": out["kEliteLight"], "chrome": out["kEliteChrome"]}


def build(wanted, glbs, src, out_dir, say):
    """wanted: [(thumb id, glb job id, view, elite)]; glbs: {glb job id: (hash, path)}.
    Returns {thumb id: hash} for every picture that exists."""
    me = file_hash([HERE / "thumbs.py", HERE / "thumbs" / "page.html", HERE / "thumbs" / "render.js"], THREE.encode())
    gold = elite_gold(src) if any(w[3] for w in wanted) else None
    cache = CACHE / "thumbs"
    cache.mkdir(parents=True, exist_ok=True)
    result, todo = {}, []
    for tid, gid, view, elite in wanted:
        if gid not in glbs:
            continue
        gh, gpath = glbs[gid]
        h = hashlib.sha256(f"{gh}|{view}|{json.dumps(gold) if elite else ''}|{me}".encode()).hexdigest()[:16]
        result[tid] = h
        if not (cache / f"{h}.webp").exists() or not (cache / f"{h}.96.webp").exists():
            todo.append({"id": h, "glb": gpath.name, "view": view, "elite": gold if elite else None})
    # One picture per hash even when two things share a model.
    seen, unique = set(), []
    for j in todo:
        if j["id"] not in seen:
            seen.add(j["id"])
            unique.append(j)
    if unique:
        if not Path(CHROME).exists():
            raise RuntimeError(f"no Chrome at {CHROME}: the thumbnails are drawn in headless Chrome")
        root = vendor()
        say(f"thumbs: drawing {len(unique)} ({len(set(result.values())) - len(unique)} cached)")
        failed = []
        for i in range(0, len(unique), BATCH):
            batch = unique[i:i + BATCH]
            spec = CACHE / "thumbs-jobs.json"
            spec.write_text(json.dumps({"chrome": CHROME, "vendor": str(root), "glb": str(CACHE / "glb"),
                                        "out": str(cache), "jobs": batch}))
            done = subprocess.run(["node", str(HERE / "thumbs" / "render.js"), str(spec)],
                                  capture_output=True, text=True)
            for line in done.stdout.splitlines():
                if line.startswith("FAIL"):
                    failed.append(line)
            say(f"thumbs: {min(i + BATCH, len(unique))}/{len(unique)}")
        if failed:
            say(f"thumbs: {len(failed)} failed: " + "; ".join(failed[:5]))
    else:
        say(f"thumbs: all {len(set(result.values()))} cached")
    for tid, h in list(result.items()):
        big, small = cache / f"{h}.webp", cache / f"{h}.96.webp"
        if not big.exists() or not small.exists():
            result.pop(tid)
            continue
        place(big, Path(out_dir) / "thumbs" / big.name)
        place(small, Path(out_dir) / "thumbs" / small.name)
    return result
