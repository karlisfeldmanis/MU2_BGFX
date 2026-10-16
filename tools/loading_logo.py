"""The flat MU Classic logo for the loading screen (src/app/preloader.cpp), from the site's SVG
(web/site/logo.svg): its one cream fill as a white mask, the preloader tints it.

  src/app/art/loading_logo.png   the mask, 512 wide, drawn at 4x and brought down
  src/app/loading_logo.h         its bytes, built into the game: the loading screen comes up
                                 before anything of the cook is in hand

The SVG is ten single-subpath outlines of M, L, V, C and Z, so each is flattened and filled as
one polygon; nothing more of SVG is read.

    python3 tools/loading_logo.py
"""
import os
import re

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "web", "site", "logo.svg")
PNG = os.path.join(ROOT, "src", "app", "art", "loading_logo.png")
HEADER = os.path.join(ROOT, "src", "app", "loading_logo.h")
WIDE, K = 512, 4


def outline(d):
    tokens = re.findall(r"[MLHVCZmlhvcz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    points, at, cmd, i = [], (0.0, 0.0), None, 0
    num = lambda: float(tokens[i])
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                continue
        if cmd == "M" or cmd == "L":
            at = (num(), float(tokens[i + 1]))
            i += 2
            points.append(at)
        elif cmd == "H":
            at = (num(), at[1])
            i += 1
            points.append(at)
        elif cmd == "V":
            at = (at[0], num())
            i += 1
            points.append(at)
        elif cmd == "C":
            c = [float(t) for t in tokens[i:i + 6]]
            i += 6
            p0 = at
            for s in range(1, 13):
                t = s / 12.0
                u = 1.0 - t
                x = u * u * u * p0[0] + 3 * u * u * t * c[0] + 3 * u * t * t * c[2] + t * t * t * c[4]
                y = u * u * u * p0[1] + 3 * u * u * t * c[1] + 3 * u * t * t * c[3] + t * t * t * c[5]
                points.append((x, y))
            at = (c[4], c[5])
        else:
            raise SystemExit(f"loading_logo: path command {cmd} is not read")
    return points


def main():
    text = open(SVG).read()
    vw, vh = [float(v) for v in re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', text).groups()]
    high = WIDE / vw * K
    tall = round(vh * WIDE / vw)
    mask = Image.new("L", (WIDE * K, tall * K), 0)
    draw = ImageDraw.Draw(mask)
    for d in re.findall(r' d="([^"]*)"', text):
        draw.polygon([(x * high, y * high) for x, y in outline(d)], fill=255)
    mask = mask.resize((WIDE, tall), Image.LANCZOS)
    art = Image.new("RGBA", (WIDE, tall), (255, 255, 255, 0))
    art.putalpha(mask)
    os.makedirs(os.path.dirname(PNG), exist_ok=True)
    art.save(PNG, optimize=True)
    data = open(PNG, "rb").read()
    lines = ["// Written by tools/loading_logo.py from web/site/logo.svg; not to be edited.",
             "#pragma once", "", "namespace mu::app {", "",
             f"// src/app/art/loading_logo.png, {WIDE} x {tall}, {len(data)} bytes.",
             "alignas(16) inline const unsigned char kLoadingLogo[] = {"]
    for at in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[at:at + 24]) + ",")
    lines += ["};", "", "}  // namespace mu::app", ""]
    open(HEADER, "w").write("\n".join(lines))
    print(f"loading_logo: {WIDE} x {tall}, {len(data)} bytes")


if __name__ == "__main__":
    main()
