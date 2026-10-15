"""The game's and the launcher's icon, from the MU Classic logo (src/launcher/art/logo.png):

  src/app/mu2.ico    Windows (src/app/mu2.rc, src/launcher/launcher.rc): the tile fills the square
  src/app/mu2.icns   the Mac (MU2.app by CMakeLists.txt, MU2Launcher.app by tools/mu/dist.py):
                     the tile inside Apple's margin (824 of 1024) with a soft shadow, as macOS
                     draws its own

The logo in silver on a rounded near-black tile, so it holds on a light desktop as on a dark one;
MU alone up to 32 px, where CLASSIC cannot be read, MU CLASSIC above. Each size is drawn at 4x and
brought down, not one image scaled.

    python3 tools/make_icons.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(ROOT, "src", "launcher", "art", "logo.png")
ICO = os.path.join(ROOT, "src", "app", "mu2.ico")
ICNS = os.path.join(ROOT, "src", "app", "mu2.icns")
TOP, BOTTOM = (38, 32, 30), (12, 10, 10)


def tile(n, art, fill, mac):
    k = 4
    N = n * k
    side = round(N * 824 / 1024) if mac else N
    at = (N - side) // 2
    back = Image.new("RGBA", (side, side))
    draw = ImageDraw.Draw(back)
    for y in range(side):
        t = y / max(side - 1, 1)
        draw.line([(0, y), (side, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)) + (255,))
    mask = Image.new("L", (side, side), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, side - 1, side - 1], radius=int(side * (0.225 if mac else 0.18)), fill=255)
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    if mac and n >= 32:
        shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
        shadow.paste(Image.new("RGBA", (side, side), (0, 0, 0, 110)), (at, at + N // 90), mask)
        im = shadow.filter(ImageFilter.GaussianBlur(N / 100))
    im.paste(back, (at, at), mask)
    w = int(side * fill)
    h = int(art.height * w / art.width)
    im.alpha_composite(art.resize((w, h), Image.LANCZOS), ((N - w) // 2, (N - h) // 2))
    return im.resize((n, n), Image.LANCZOS)


def main():
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.crop(logo.getbbox())
    mu = logo.crop((0, 0, logo.width, 168))
    mu = mu.crop(mu.getbbox())

    def art(n, mac):
        small = n <= 32
        return tile(n, mu if small else logo, 0.86 if small else 0.80, mac)

    sizes = [16, 24, 32, 48, 64, 128, 256]
    drawn = [art(n, False) for n in sizes]
    drawn[-1].save(ICO, sizes=[(n, n) for n in sizes], append_images=drawn[:-1])
    print(f"{ICO}: {', '.join(map(str, sizes))} px")

    if shutil.which("iconutil") is None:
        sys.exit("no iconutil (macOS only): the .icns was not made")
    with tempfile.TemporaryDirectory() as folder:
        iconset = os.path.join(folder, "mu2.iconset")
        os.makedirs(iconset)
        for n in [16, 32, 128, 256, 512]:
            art(n, True).save(os.path.join(iconset, f"icon_{n}x{n}.png"))
            art(2 * n, True).save(os.path.join(iconset, f"icon_{n}x{n}@2x.png"))
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", ICNS], check=True)
    print(f"{ICNS}: 16-1024 px")


if __name__ == "__main__":
    main()
