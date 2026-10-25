"""The Armory's figure thumbnails, measured: a monster's, an Elite's or an NPC's picture must show
the whole figure, framed (thumbs/page.html leaves a margin all round), and at a size that reads.

    python3 tools/armory/tests/thumb_check.py [out/armory/<commit>]    (the newest build by default)

FAIL when a picture's opaque pixels run along its edge (the figure is cropped: the user's "not
visible monster in image"), or when what is visible fills under MIN_AREA of the frame or spans
under MIN_SPAN of it (it is small and off to one side, as when a spell's beams were framed with
it: "some monsters with spells"). WARN when a
large share of the figure is flat near-white (a mirror of the lights or an untextured slot, the
Death Rider's patches). Items are not held to this: a sword may run corner to corner. Exit 1 on any
failure.
"""
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
KINDS = ("monsters", "npcs", "elites")
ALPHA = 128          # opaque enough to touch an edge or count as white
SEEN = 24            # visible enough to count in the area: a glow body (the Valkyrie, Queen
                     # Rainer) is laid on by its brightness and is mostly translucent
EDGE_PIXELS = 4      # opaque pixels allowed on one edge (antialiasing, a stray spark)
EDGE_BAND = 2        # the outermost rows/columns looked at
MIN_AREA = 0.05      # of the frame: the Dark Phoenix, a rider on a bird of thin fire with its
                     # wings spread nine metres, fills 5%
MIN_SPAN = 0.7       # the visible figure's longer side, of the frame's (the framing gives 86%)
WHITE = 236          # every channel at least this ...
WHITE_SHARE = 0.12   # ... over this share of the opaque pixels is reported


def measure(path):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()
    opaque = seen = white = 0
    x0, y0, x1, y1 = w, h, -1, -1
    edges = {"top": 0, "bottom": 0, "left": 0, "right": 0}
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= SEEN:
                seen += 1
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
            if a < ALPHA:
                continue
            opaque += 1
            if r >= WHITE and g >= WHITE and b >= WHITE:
                white += 1
            if y < EDGE_BAND:
                edges["top"] += 1
            if y >= h - EDGE_BAND:
                edges["bottom"] += 1
            if x < EDGE_BAND:
                edges["left"] += 1
            if x >= w - EDGE_BAND:
                edges["right"] += 1
    span = max(x1 - x0 + 1, y1 - y0 + 1, 0) / float(max(w, h))
    return {"area": seen / float(w * h), "span": span, "white": white / float(max(opaque, 1)), "edges": edges}


def judge(label, m):
    fails, warns = [], []
    touched = {k: v for k, v in m["edges"].items() if v > EDGE_PIXELS}
    if touched:
        fails.append(f"{label}: cropped, opaque along the edge {touched}")
    if m["area"] < MIN_AREA:
        fails.append(f"{label}: fills {m['area']:.1%} of the frame (under {MIN_AREA:.0%})")
    if m["span"] < MIN_SPAN:
        fails.append(f"{label}: spans {m['span']:.0%} of the frame (under {MIN_SPAN:.0%}): framed on more than is drawn")
    if m["white"] > WHITE_SHARE:
        warns.append(f"{label}: {m['white']:.0%} of the figure is flat near-white")
    return fails, warns


def newest():
    base = ROOT / "out" / "armory"
    built = [d for d in base.iterdir() if (d / "meta.json").exists()] if base.exists() else []
    return max(built, key=lambda d: (d / "meta.json").stat().st_mtime) if built else None


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else newest()
    if out is None or not (out / "meta.json").exists():
        print("FAIL no built Armory (./mu armory build first)")
        return 1
    seen, fails, warns, checked = set(), [], [], 0
    for kind in KINDS:
        index = out / "index" / f"{kind}.json"
        if not index.exists():
            continue
        for row in json.loads(index.read_text()).get("rows", []):
            t = row.get("t") or ""
            if not t:
                continue
            big = out / t.replace(".96.webp", ".webp")
            if big in seen:
                continue
            seen.add(big)
            label = f"{kind[:-1]} {row.get('slug', '?')} ({big.name})"
            if not big.exists():
                fails.append(f"{label}: missing")
                continue
            f, w = judge(label, measure(big))
            fails += f
            warns += w
            checked += 1
    for line in warns:
        print("WARN", line)
    for line in fails:
        print("FAIL", line)
    print(f"thumb check: {checked} figure pictures, {len(fails)} failed, {len(warns)} warned")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
