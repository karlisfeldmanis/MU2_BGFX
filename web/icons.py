"""The site's icons and share card, made from logo.png and hero.jpg: python3 web/icons.py"""
import sys
from PIL import Image, ImageDraw, ImageFilter
from pathlib import Path
site = str(Path(__file__).resolve().parent / "site")
GROUND = (13, 11, 10, 255)
logo = Image.open(f"{site}/logo.png").convert("RGBA")
mu = logo.crop((0, 0, logo.width, 172))
mu = mu.crop(mu.getbbox())

def tile(size, fill=0.84, radius=0.0, bg=True):
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    if bg:
        m = Image.new("L", (size * 4, size * 4), 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), int(radius * size * 4), fill=255)
        g = Image.new("RGBA", (size, size), GROUND)
        im.paste(g, (0, 0), m.resize((size, size), Image.LANCZOS))
    w = round(size * fill); h = round(w * mu.height / mu.width)
    m = mu.resize((w, h), Image.LANCZOS)
    if size <= 32:  # small sizes: a touch of sharpening so the letters hold
        m = m.filter(ImageFilter.UnsharpMask(1, 80, 0))
    im.alpha_composite(m, ((size - w) // 2, (size - h) // 2))
    return im

ico = [tile(s, fill=0.96, radius=0.18) for s in (16, 32, 48)]
ico[2].save(f"{site}/favicon.ico", sizes=[(16, 16), (32, 32), (48, 48)], append_images=ico[:2])
tile(32, fill=0.96, radius=0.18).save(f"{site}/favicon-32.png")
tile(180, fill=0.78).convert("RGB").save(f"{site}/apple-touch-icon.png")   # iOS rounds it itself
tile(192, fill=0.80, radius=0.18).save(f"{site}/icon-192.png")
tile(512, fill=0.80, radius=0.18).save(f"{site}/icon-512.png")
tile(512, fill=0.62).convert("RGB").save(f"{site}/icon-maskable-512.png")  # inside the 80% safe circle

# The share card: hero picture, the page's shade, the logo in the middle.
hero = Image.open(f"{site}/hero.jpg").convert("RGBA")
W, H = 1200, 630
s = max(W / hero.width, H / hero.height)
hero = hero.resize((round(hero.width * s), round(hero.height * s)), Image.LANCZOS)
hero = hero.crop(((hero.width - W) // 2, (hero.height - H) // 2, (hero.width - W) // 2 + W, (hero.height - H) // 2 + H))
shade = Image.new("L", (W, H), 0)
d = ImageDraw.Draw(shade)
for i in range(60):  # radial dark centre, as .shade on the page
    t = i / 59
    d.ellipse((W/2 - W*0.6*(1-t), H*0.5 - H*0.7*(1-t), W/2 + W*0.6*(1-t), H*0.5 + H*0.7*(1-t)), fill=int(40 + 120 * t))
shade = shade.filter(ImageFilter.GaussianBlur(30))
dark = Image.new("RGBA", (W, H), GROUND); dark.putalpha(shade)
hero.alpha_composite(dark)
lw = 560; lh = round(lw * logo.height / logo.width)
glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
glow.alpha_composite(logo.resize((lw, lh), Image.LANCZOS), ((W - lw) // 2, (H - lh) // 2))
sh = glow.split()[3].filter(ImageFilter.GaussianBlur(14))
black = Image.new("RGBA", (W, H), (0, 0, 0, 0)); black.putalpha(sh.point(lambda v: v * 0.8))
hero.alpha_composite(black); hero.alpha_composite(glow)
hero.convert("RGB").save(f"{site}/og.jpg", quality=86, optimize=True, progressive=True)
