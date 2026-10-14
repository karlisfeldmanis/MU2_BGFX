"""The HUD's ammunition icons, hud_ammo_arrows.png and hud_ammo_bolts.png, out of MU's quivers.

The elf's count of arrows or bolts stands over the potion belt (game/ui/endurance.cpp), and
the user wanted the item itself there (2026-10-10: "use the real Arrows/Bolt item picture from
the bag instead of the thin silhouette", then "lets not use circle just icon and under the
number"). So the quiver in its own colours, rendered in workshop/ as quiver_ghost.py renders
the ghost -- side on and leaned -- with a soft dark halo round it so it reads on bright ground
with no container behind it. Arrows02 is MU's Arrows and Arrows01 the Bolt (their recipes'
naming_trap).

    python3 pipeline/ammo_icons.py
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
# The folders are tools/paths.py's; appended, so nothing there shadows a module here.
sys.path.append(str(Path(__file__).resolve().parents[1] / "tools"))
from model_icons import WORKSHOP, render  # noqa: E402
from paths import P  # noqa: E402

OUT = P.CONTENT / "interface"
# name: the model, and its lean in degrees anticlockwise. The quiver is thrown across the square;
# the Bolt's two crossed bolts stand as an even X at 75, its crossing on the box's middle -- at
# the quiver's 60 the long shafts reached up and right and it read off centre (the user,
# 2026-10-10: "bolts not perfectly in center", "still not in center middle").
ICONS = {"hud_ammo_arrows": ("Arrows02", 60.0), "hud_ammo_bolts": ("Arrows01", 75.0)}
TURN = 90.0  # its length across the camera, as the ghost's
SIDE = 96    # the longer side in pixels, the halo inside it
HALO = 5     # pixels of shadow round the figure


def one(name: str, stem: str, lean: float) -> int:
    glb = WORKSHOP / "weapons" / stem / f"{stem}.glb"
    if not glb.exists():
        print(f"missing {glb}: build it with pipeline/asset.sh {stem}", file=sys.stderr)
        return 1
    figure = glb.with_name(f"{stem}_ghost_figure.png")
    if not figure.exists() or figure.stat().st_mtime < glb.stat().st_mtime:
        render(glb, figure, TURN)
    im = Image.open(figure).convert("RGBA")
    im = im.rotate(lean, resample=Image.BICUBIC, expand=True)
    im = im.crop(im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())
    fit = (SIDE - 2 * HALO - 2) / max(im.width, im.height)
    small = im.resize((max(1, round(im.width * fit)), max(1, round(im.height * fit))),
                      Image.LANCZOS)
    # A touch brighter and fuller than the render: it stands on the ground at thirty pixels.
    rgba = np.array(small).astype(np.float32)
    rgb = rgba[..., :3]
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    covered = rgba[..., 3] > 128
    hi = np.percentile(lum[covered], 98) if covered.any() else 255.0
    rgb = np.clip(rgb * (235.0 / max(hi, 1.0)), 0.0, 255.0)
    grey = (rgb @ np.array([0.299, 0.587, 0.114], np.float32))[..., None]
    rgb = np.clip(grey + (rgb - grey) * 1.15, 0.0, 255.0)
    w, h = small.width + 2 * HALO, small.height + 2 * HALO
    figure_layer = np.zeros((h, w, 4), np.float32)
    figure_layer[HALO:HALO + small.height, HALO:HALO + small.width, :3] = rgb
    figure_layer[HALO:HALO + small.height, HALO:HALO + small.width, 3] = rgba[..., 3]
    halo = Image.fromarray(figure_layer[..., 3].astype(np.uint8)).filter(
        ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(HALO * 0.6))
    halo = np.array(halo).astype(np.float32) / 255.0 * 0.95
    a = figure_layer[..., 3] / 255.0
    out_a = a + halo * (1.0 - a)
    out_rgb = figure_layer[..., :3] * (a / np.maximum(out_a, 1e-6))[..., None]
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = np.clip(out_rgb, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(out_a * 255.0, 0, 255).astype(np.uint8)
    path = OUT / f"{name}.png"
    Image.fromarray(out, "RGBA").save(path)
    print(f"wrote {path} ({out.shape[1]}x{out.shape[0]})")
    return 0


def main() -> int:
    return max(one(name, stem, lean) for name, (stem, lean) in ICONS.items())


if __name__ == "__main__":
    sys.exit(main())
