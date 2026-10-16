"""MU2Setup.exe's wizard pictures (src/launcher/mu2setup.iss), from the launcher's own art
(src/launcher/art/wallpaper.jpg and logo.png), so the setup looks like the launcher it installs:

  src/launcher/art/setup/wizard-NNN.png   the tall picture left of the Welcome and Finish pages:
                                          the Dark Knight's head and chest, the logo below him,
                                          fading into the wizard's near-black on the right
  src/launcher/art/setup/small-NNN.png    the square at the top right of the other pages: the logo

NNN is the Windows scale (100 to 250 %); Setup takes the size nearest the player's. Each is drawn
from the full-size art, not one picture scaled.

    python3 tools/make_setup_art.py
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "src", "launcher", "art")
OUT = os.path.join(ART, "setup")
# The launcher's ground (src/launcher/ui.cpp, 0x0D0B0A); mu2setup.iss's WizardBackColor.
GROUND = (13, 11, 10)
SCALES = (100, 125, 150, 175, 200, 225, 250)
WIZARD, SMALL = (164, 314), (55, 55)
# The wallpaper's knight, head to belt, at the wizard picture's shape.
CROP = (800, 40, 1330, 1055)


def fade(size, stops):
    """A mask from `stops`, (where 0..1 across, alpha 0..255) pairs, running left to right."""
    w, h = size
    row = Image.new("L", (w, 1))
    for x in range(w):
        t = x / max(1, w - 1)
        for (t0, a0), (t1, a1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                row.putpixel((x, 0), round(a0 + (a1 - a0) * (t - t0) / max(1e-6, t1 - t0)))
                break
    return row.resize((w, h))


def wizard(scale):
    w, h = round(WIZARD[0] * scale / 100), round(WIZARD[1] * scale / 100)
    wall = Image.open(os.path.join(ART, "wallpaper.jpg")).convert("RGB").crop(CROP)
    # Cover, keeping the knight's face in view.
    k = max(w / wall.width, h / wall.height)
    wall = wall.resize((round(wall.width * k), round(wall.height * k)), Image.LANCZOS)
    left = (wall.width - w) // 2
    picture = wall.crop((left, 0, left + w, h))
    ground = Image.new("RGB", (w, h), GROUND)
    # Into the ground at the right edge, where the page's words start, and under the logo.
    side = fade((w, h), [(0.0, 255), (0.62, 255), (1.0, 40)])
    foot = fade((h, w), [(0.0, 255), (0.42, 255), (0.80, 0), (1.0, 0)]).rotate(-90, expand=True)
    mask = Image.composite(side, Image.new("L", (w, h), 0), foot)
    picture = Image.composite(picture, ground, mask)
    logo = Image.open(os.path.join(ART, "logo.png")).convert("RGBA")
    lw = round(w * 0.78)
    logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
    picture.paste(logo, ((w - lw) // 2, round(h * 0.93) - logo.height), logo)
    return picture


def small(scale):
    w, h = round(SMALL[0] * scale / 100), round(SMALL[1] * scale / 100)
    picture = Image.new("RGB", (w, h), GROUND)
    logo = Image.open(os.path.join(ART, "logo.png")).convert("RGBA")
    lw = w
    logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
    picture.paste(logo, (0, (h - logo.height) // 2), logo)
    return picture


def main():
    os.makedirs(OUT, exist_ok=True)
    for s in SCALES:
        wizard(s).save(os.path.join(OUT, f"wizard-{s}.png"))
        small(s).save(os.path.join(OUT, f"small-{s}.png"))
    print(f"{OUT}: {len(SCALES) * 2} pictures")


if __name__ == "__main__":
    main()
