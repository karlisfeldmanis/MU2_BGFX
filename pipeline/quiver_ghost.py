"""The elf's left-hand ghost, win_ghost_quiver.png, out of MU's own quiver.

The Fairy Elf wears her arrows and bolts in the left hand (sim::placeOf), so MU's shield plate
was the wrong shape in her slot (the user, 2026-10-10: "we need to update ghost icons for elf
equipent shield slots, because she is wearing arrrow/bolts there"). MU has no quiver plate for
slot_ghosts.py to cut, so the shape is Arrows02 -- MU's Arrows, the quiver anybody would call
one (its recipe's naming_trap) -- rendered in workshop/ and written as mount_ghost.py writes
the horse: the model's own shading as grey in RGB, the coverage in alpha.

    python3 pipeline/quiver_ghost.py
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

OUT = P.CONTENT / "interface" / "win_ghost_quiver.png"
TURN = 90.0  # its length across the camera, the arrows' heads to the right
# Stood upright and leaned as the shield plate's sword is: degrees, anticlockwise.
LEAN = 75.0
# The longer side, in pixels: win_ghost_weapon_right is 122 tall for the same tall cell.
SIDE = 120
LOW, HIGH = 110, 255


def main() -> int:
    glb = WORKSHOP / "weapons" / "Arrows02" / "Arrows02.glb"
    if not glb.exists():
        print(f"missing {glb}: build it with tools/asset.sh Arrows02", file=sys.stderr)
        return 1
    figure = glb.with_name("Arrows02_ghost_figure.png")
    if not figure.exists() or figure.stat().st_mtime < glb.stat().st_mtime:
        render(glb, figure, TURN)
    im = Image.open(figure).convert("RGBA")
    if LEAN:
        im = im.rotate(LEAN, resample=Image.BICUBIC, expand=True)
    im = im.crop(im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())
    fit = (SIDE - 2) / max(im.width, im.height)
    small = im.resize((max(1, round(im.width * fit)), max(1, round(im.height * fit))),
                      Image.LANCZOS)
    rgba = np.array(small).astype(np.float32)
    lum = rgba[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    covered = rgba[..., 3] > 128
    lo, hi = np.percentile(lum[covered], [2, 98]) if covered.any() else (0.0, 255.0)
    tone = np.clip((lum - lo) / max(hi - lo, 1.0), 0.0, 1.0) * (HIGH - LOW) + LOW
    alpha = Image.fromarray(rgba[..., 3].astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.5))
    alpha = np.array(alpha).astype(np.float32)
    alpha = np.clip((alpha - 96.0) * 2.0, 0.0, 255.0)
    out = np.zeros((small.height + 2, small.width + 2, 4), np.uint8)
    out[1:-1, 1:-1, 0] = out[1:-1, 1:-1, 1] = out[1:-1, 1:-1, 2] = tone.astype(np.uint8)
    out[1:-1, 1:-1, 3] = alpha.astype(np.uint8)
    Image.fromarray(out, "RGBA").save(OUT)
    print(f"wrote {OUT} ({out.shape[1]}x{out.shape[0]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
