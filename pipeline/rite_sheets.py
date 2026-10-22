"""The Rebirth rite's own sheets (game/fx/rite.h), drawn here because MU has none of them.

    python3 pipeline/rite_sheets.py

Writes content/effects/rite/:

  beam.png   128x512  the light from heaven: across, a flat core with a soft falloff to black at
                      both edges; along, full from the ground up, fading out smoothly over its top half.
                      White: the rite tints it gold.
  rays.png   128x512  the shafts inside it: a few soft vertical streaks of different widths and
                      strengths, under the same edges, for the beam's slow shimmer.
  pool.png   128x128  where it lands: a disc, full to 0.7 of its radius, soft to black at its rim.
  ring.png   128x128  the ground explosion's wave: a bright band near the rim, a faint glow trailing
                      inside it.
  runes.png  128x128  MU's level-up magic_ground (content/effects/levelup) made grey by its
                      brightest channel, so the rite's gold tint gives gold: the sheet's own
                      colours under a gold tint read purple (the user, 2026-10-22: "use gold also
                      under the character").

**Ours, all of it**, the user's ask of 2026-10-22: "beam has to be more like in opera wider beam",
"its like light coming from heaven". Fixed seed: the same sheets every run.
"""
import pathlib
import sys

import numpy as np
from PIL import Image

sys.path.append(str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from paths import P  # noqa: E402

OUT = P.CONTENT / "effects" / "rite"


def edges(u):
    # A flat core with a soft falloff: a super-gaussian across the beam.
    return np.exp(-((u - 0.5) / 0.36) ** 6) * 0.85 + np.exp(-((u - 0.5) / 0.22) ** 2) * 0.15


def along(v):
    # v 0 at the top: faded in smoothly over its top half -- lost in the sky, not cut off --
    # and a breath of fade at the very foot.
    k = np.clip(v / 0.5, 0.0, 1.0)
    top = k * k * (3.0 - 2.0 * k)
    foot = np.clip((1.0 - v) / 0.04, 0.0, 1.0)
    return top * foot


def save(name, values):
    grey = (np.clip(values, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(np.stack([grey, grey, grey, np.full_like(grey, 255)], axis=-1), "RGBA").save(OUT / name)
    print(f"rite_sheets: {name} {grey.shape[1]}x{grey.shape[0]}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    w, h = 128, 512
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    save("beam.png", np.outer(along(v), edges(u)))

    rng = np.random.default_rng(22)
    shafts = np.zeros(w)
    for _ in range(9):
        centre, width, strength = rng.uniform(0.1, 0.9), rng.uniform(0.03, 0.09), rng.uniform(0.35, 1.0)
        shafts += strength * np.exp(-((u - centre) / width) ** 2)
    shafts /= shafts.max()
    save("rays.png", np.outer(along(v), shafts * edges(u)))

    n = 128
    x = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    r = np.sqrt(x[None, :] ** 2 + x[:, None] ** 2)
    k = np.clip((r - 0.7) / 0.3, 0.0, 1.0)
    save("pool.png", 1.0 - k * k * (3.0 - 2.0 * k))

    # The ground explosion's wave: a bright band at 0.85 of the radius, sharp outside, a faint glow
    # trailing inside it, nothing at the middle -- a ring that reads as a ring at any size.
    band = np.exp(-((r - 0.85) / 0.05) ** 2) * (r < 0.85) + np.exp(-((r - 0.85) / 0.025) ** 2) * (r >= 0.85)
    trail = np.clip(r / 0.85, 0.0, 1.0) ** 4 * 0.22 * (r < 0.85)
    save("ring.png", band + trail)

    ground = np.asarray(Image.open(P.CONTENT / "effects" / "levelup" / "magic_ground.png").convert("RGB"), float) / 255.0
    save("runes.png", ground.max(axis=-1))


if __name__ == "__main__":
    main()
