"""The mount slot's ghost, win_ghost_mount.png, out of the horse the Horn of Uniria calls.

MU has no mount slot -- Uniria and Dinorant are helpers and take slot 8 with the pets (OpenMU
Version075/Items/Pets.cs) -- so it has no empty-slot plate for slot_ghosts.py to cut a shape
from. The slot is ours (the user, 2026-10-02: "create new equipment slot, for mounts, find
proper ghost icon"), and so is its ghost: Rider01, the built horse in workshop/, rendered side
on in its rest pose, which rears -- the plainest horse shape there is -- and written the way
slot_ghosts.py writes MU's: the model's own shading as grey in RGB, the coverage in alpha, so
the bag tints it exactly as it tints the helm and the ring.

    python3 pipeline/mount_ghost.py
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model_icons import WORKSHOP, render  # noqa: E402

PROJECT = Path(__file__).resolve().parents[1]
OUT = PROJECT / "source" / "interface" / "win_ghost_mount.png"
TURN = 270.0  # side on, head to the right, into the window
# The longer side, in pixels: the ring's ghost is 41 for a 25-unit cell, a slot_ghosts.py
# ghost being written at about one and a half times what it is drawn at.
SIDE = 48
# MU's ghosts run from about 70 to white inside the shape (win_ghost_ring: 70..255, mean 192);
# the horse's piebald coat is patchier than any of MU's painted shapes, so its floor is higher.
LOW, HIGH = 130, 255


def main() -> int:
    glb = WORKSHOP / "pets" / "Rider01" / "Rider01.glb"
    if not glb.exists():
        print(f"missing {glb}: build it with tools/asset.sh Rider01", file=sys.stderr)
        return 1
    figure = glb.with_name("Rider01_ghost_figure.png")
    if not figure.exists() or figure.stat().st_mtime < glb.stat().st_mtime:
        render(glb, figure, TURN)
    im = Image.open(figure).convert("RGBA")
    im = im.crop(im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())
    # Fitted with a pixel of air, at the shape's own aspect.
    fit = (SIDE - 2) / max(im.width, im.height)
    small = im.resize((max(1, round(im.width * fit)), max(1, round(im.height * fit))),
                      Image.LANCZOS)
    rgba = np.array(small).astype(np.float32)
    # The shading: the render's luminance stretched onto MU's ghost range, so the mane and the
    # belly keep their light and dark.
    lum = rgba[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    covered = rgba[..., 3] > 128
    lo, hi = np.percentile(lum[covered], [2, 98]) if covered.any() else (0.0, 255.0)
    tone = np.clip((lum - lo) / max(hi - lo, 1.0), 0.0, 1.0) * (HIGH - LOW) + LOW
    # The coverage, its edge cut again at a half after a light blur, as slot_ghosts.py does, so
    # the hairs of the tail that are thinner than a pixel come off clean.
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
