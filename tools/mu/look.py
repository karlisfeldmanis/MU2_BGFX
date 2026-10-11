"""`mu where`, `mu shot` and `mu compare`: finding a thing, and looking at it.

    mu where Balrog            every file that names it, by area, with a count each
    mu shot aida 118,87        a muted shot at a tile: noon by default, the hero's save untouched
    mu compare A.png B.png     the two side by side, labelled, with how much changed

`where` is the measure docs/recipes.md asks for: a thing named in one folder is a thing with
a home. `shot` is the memory's run line, which every session retyped: --mute, a scratch
--save, an absolute --shot-path. `compare` is for me as much as anyone: one labelled sheet
reads in a single look where two loose PNGs take two and a guess.
"""

import os
import subprocess
import tempfile
import time

from things import ROOT

AREAS = ["src", "server/src", "tests", "tools", "pipeline", "source", "sheets", "docs"]
SKIP = (".obj", ".png", ".wav", ".blend", ".glb", ".ktx", ".db")


def where(name, say, docs=False):
    areas = AREAS if docs else AREAS[:-1]
    done = subprocess.run(["git", "grep", "-c", "-I", "-i", "-w", "--untracked", "-e", name, "--"]
                          + areas, capture_output=True, text=True, cwd=ROOT)
    hits = []
    for line in done.stdout.splitlines():
        path, _, count = line.rpartition(":")
        if not path.endswith(SKIP):
            hits.append({"file": path, "count": int(count)})
    by_area = {}
    for hit in hits:
        area = hit["file"].split("/")[0]
        if area in ("src", "server"):
            area = "/".join(hit["file"].split("/")[:2])
        by_area.setdefault(area, []).append(hit)
    for area in sorted(by_area):
        say(f"{area}: {len(by_area[area])} file(s)")
        for hit in sorted(by_area[area], key=lambda h: -h["count"]):
            say(f"  {hit['file']}  {hit['count']}")
    say(f"{name}: {len(hits)} files in {len(by_area)} areas")
    return True, {"name": name, "files": hits}


def shot(world, at, when, frames, out, say):
    out = os.path.abspath(out or os.path.join(ROOT, "shots", "mu", f"{world}_{at.replace(',', '_')}_{when}"))
    os.makedirs(out, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="mu-save-")
    at_frame = max(1, frames // 2)
    cmd = [os.path.join(ROOT, "build", "mu2"), "--world", world, "--at", at, "--time", when,
           "--frames", str(frames), "--shot", str(at_frame), "--shot-path", out, "--mute",
           "--save", os.path.join(scratch, "demo.json")]
    say(" ".join(cmd))
    started = time.time()
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    pngs = sorted(f for f in os.listdir(out) if f.endswith(".png"))
    path = os.path.join(out, pngs[-1]) if pngs else None
    ok = done.returncode == 0 and path is not None
    say(f"{'shot' if ok else 'FAILED'} {path or '(none)'} in {time.time() - started:.0f} s")
    return ok, {"png": path, "code": done.returncode, "cmd": " ".join(cmd)}


def compare(a, b, out, say):
    from PIL import Image, ImageChops, ImageDraw, ImageStat

    left, right = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if left.size != right.size:
        right = right.resize(left.size)
    diff = ImageChops.difference(left, right)
    mean = sum(ImageStat.Stat(diff).mean) / 3
    grey = diff.convert("L")
    changed = sum(grey.histogram()[9:]) / (grey.width * grey.height) * 100
    heat = grey.point(lambda v: min(255, v * 4)).convert("RGB")

    w, h = left.size
    scale = min(1.0, 900 / w)
    tw, th = int(w * scale), int(h * scale)
    sheet = Image.new("RGB", (tw * 3, th + 28), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, (label, image) in enumerate([(os.path.basename(a), left), (os.path.basename(b), right),
                                        (f"diff x4: mean {mean:.2f}, {changed:.1f}% moved", heat)]):
        sheet.paste(image.resize((tw, th)), (i * tw, 28))
        draw.text((i * tw + 8, 8), label, fill=(230, 230, 230))
    out = os.path.abspath(out or os.path.splitext(b)[0] + ".compare.png")
    sheet.save(out)
    say(f"{out}: mean difference {mean:.2f} of 255, {changed:.1f}% of pixels moved by more than 8")
    return True, {"sheet": out, "mean": round(mean, 3), "moved_percent": round(changed, 2)}
