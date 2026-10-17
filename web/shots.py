"""The screenshots as the page loads them, made from site/shots/<name>.jpg: python3 web/shots.py

<name>.webp at full size for the middle slide, <name>-960.webp for the small ones at the sides and
on phones, and shots/previews.js: each shot 32 pixels wide, inline, shown blurred the moment the
page opens while the real one comes."""
import base64, io, json, subprocess
from pathlib import Path
from PIL import Image
shots = Path(__file__).resolve().parent / "site" / "shots"
previews = {}
for jpg in sorted(shots.glob("*.jpg")):
    name = jpg.stem
    for out, width, q in ((f"{name}.webp", 1920, 74), (f"{name}-960.webp", 960, 78)):
        subprocess.run(["cwebp", "-quiet", "-q", str(q), "-m", "6", "-sharp_yuv", "-resize", str(width), "0",
                        str(jpg), "-o", str(shots / out)], check=True)
    im = Image.open(jpg).convert("RGB")
    im = im.resize((32, round(32 * im.height / im.width)), Image.LANCZOS)
    b = io.BytesIO(); im.save(b, "JPEG", quality=60, optimize=True)
    previews[name] = "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()
    print(f"{name}: {jpg.stat().st_size // 1024} KB jpg -> {(shots / f'{name}.webp').stat().st_size // 1024} KB, "
          f"{(shots / f'{name}-960.webp').stat().st_size // 1024} KB")
(shots / "previews.js").write_text("// Made by web/shots.py.\nwindow.SHOT_PREVIEWS = "
                                   + json.dumps(previews, indent=0) + ";\n")
