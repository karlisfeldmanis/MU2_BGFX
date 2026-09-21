"""What a lit frame is scored on, so a look can be tuned by measurement rather than by argument.

    python3 pipeline/look.py shots/*.png
    python3 pipeline/look.py --against shots_noshadow lit shots_shadow

Every number here belongs to one of two questions, and keeping them apart is the whole point.

**Is it broken?** `acne` and `teal` are defects: nobody wants more of either and any amount
is a bug. They exist because both have been argued from screenshots in this project and the
eye is bad at both — acne on dark leather at a graze looks like the leather, and a teal
fringe two pixels wide is invisible until somebody zooms in and then it is all they can see.

**Does it look right?** `mean`, `p10`, `clip`, `warmth`, `chroma` and `split` describe a
picture rather than judge it. There is no target in this file for any of them: a town at a
mean of 80 is not worse than one at 120, it is darker, and which is wanted is a decision.
What they are for is holding the rest still while one thing moves — the failure this project
keeps having is a change that fixes a shoulder and quietly costs a stop everywhere.

The one thing this cannot see is whether a frame is beautiful. It can only say what changed.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image

#: Rec. 709 luma: what the eye reads as brightness, not a flat channel average.
LUMA = np.array([0.2126, 0.7152, 0.0722], np.float32)

#: How much darker than its neighbourhood a pixel must be to count as acne, in 0..1.
#:
#: Shadow acne is a *thin* error: a shadow map disagreeing with itself across one texel, so
#: the pixel flips fully dark while its neighbours stay lit. A real shading gradient over the
#: same distance is a few levels. 0.06 of 1.0 is about 15 of 255 — far above any gradient a
#: smooth surface produces across three pixels, far below the step acne makes.
ACNE_STEP = 0.06

#: How bright the surroundings must be for a dark speck to be acne rather than shading.
#:
#: A speck inside a shadow is not acne, it is the shadow. Acne is the artefact of a surface
#: the sun *reaches* shadowing itself, so the test only looks where the neighbourhood is lit.
ACNE_LIT = 0.18

#: How far a pixel's red must fall below its green and blue to count as teal, in 0..1.
#:
#: Named for the fault it was written to catch: with a warm key and a soft shadow filter, a
#: penumbra came out at (28, 149, 140) of 255 where the honest shade is (110, 126, 131) —
#: red subtracted rather than compressed. No light in any of this project's rigs can make
#: that, so any pixel this far from the greys is arriving by a route nobody chose.
#:
#: 0.12 rather than the 0.08 this started at, and the first thing the instrument did was
#: catch itself. At 0.08 it reported 2232 pixels on a frame with no defect on it, and the
#: pixels it named were (126, 147, 166) and its neighbours — the bare Knight's blue-grey
#: steel, honestly lit. A test that fires on the material it is supposed to exonerate is
#: worse than no test: it is the measurement that would have "confirmed" every wrong theory
#: this fault has already been given.
TEAL_LEAD = 0.12

#: How near a teal pixel's green and blue must be to each other, as a share of the lead.
#:
#: This is what separates the defect from honest shade, and it is a statement about which
#: channel went wrong. Sky-lit steel is *blue*: blue leads green leads red, in that order and
#: by steps — (126, 147, 166) has green and blue 19 apart against a lead of 21. The defect is
#: *cyan*: red alone has been subtracted and the other two are left where they were, so they
#: sit together — (28, 149, 140) has them 9 apart against a lead of 121.
#:
#: So a pixel is only teal if its green and blue agree with each other far better than either
#: agrees with red. Half is a wide margin and both cases clear it by an order of magnitude.
TEAL_FLATNESS = 0.5


#: The part of the frame to score, as x0,y0,x1,y1, or None for all of it. See --crop.
#:
#: Not a convenience. Grass and a cobbled street are high-frequency by design, and a speckle
#: test run over them reports one per cent of the frame as acne on a frame with none — the
#: measurement drowns in the thing it is supposed to be sensitive to. A defect is reported
#: on a character, so the character is what gets scored.
WINDOW: "tuple[int, int, int, int] | None" = None


def read(path: Path) -> np.ndarray:
    picture = Image.open(path).convert("RGB")

    if WINDOW is not None:
        picture = picture.crop(WINDOW)

    return np.asarray(picture, np.float32) / 255.0


def blur(plane: np.ndarray, radius: int) -> np.ndarray:
    """A box blur by repeated rolls. Separable, so two passes rather than a kernel."""
    out = plane.copy()

    for axis in (0, 1):
        run = out.copy()
        for step in range(1, radius + 1):
            run = run + np.roll(out, step, axis=axis) + np.roll(out, -step, axis=axis)
        out = run / (2 * radius + 1)

    return out


def acne(rgb: np.ndarray) -> tuple[float, float]:
    """Isolated dark specks on surfaces the light reaches, as a fraction of the frame.

    The shape of the test is the shape of the defect. Acne is high-frequency — one or two
    pixels dark against lit neighbours — so it survives a high pass, and it sits *on* lit
    geometry, so the neighbourhood is bright. A real shadow fails both: it is a connected
    region, so its interior looks like its neighbourhood, and only its edge is a step.

    Returns the fraction of pixels that qualify and the mean depth of the ones that do.
    """
    lit = rgb @ LUMA
    around = blur(lit, 2)
    below = around - lit

    speckled = (below > ACNE_STEP) & (around > ACNE_LIT)
    share = float(speckled.mean())
    depth = float(below[speckled].mean()) if speckled.any() else 0.0

    return share * 100.0, depth


def teal(rgb: np.ndarray) -> float:
    """Pixels whose red has been subtracted rather than compressed, per million.

    Two conditions, because one of them is not enough and finding that out is most of what
    this function is worth. See TEAL_LEAD and TEAL_FLATNESS.

    Neither condition can tell the defect from paint that is genuinely this colour, and on a
    town frame that is most of what it counts: the 244 pixels flagged on Lorencia's square
    turned out to be moss on the stonework, scattered over a wall, where the defect is a
    coherent fringe along a shadow edge. So a rising count is a prompt to look at *where*
    they are and not a verdict — mark them and crop, which is what settled that one.
    """
    lead = np.minimum(rgb[..., 1], rgb[..., 2]) - rgb[..., 0]
    flat = np.abs(rgb[..., 1] - rgb[..., 2])

    # Not in the near-black, where every channel is in the tone curve's toe and a lead of a
    # few levels is quantisation rather than colour.
    lit = (rgb @ LUMA) > 0.05

    caught = (lead > TEAL_LEAD) & (flat < TEAL_FLATNESS * lead) & lit

    return float(caught.mean()) * 1e6


def tonality(rgb: np.ndarray) -> dict[str, float]:
    """What the picture is doing, as against whether it is broken."""
    lit = rgb @ LUMA
    high = rgb.max(axis=2)
    low = rgb.min(axis=2)

    # Saturation as the eye meets it, over the pixels bright enough to have a colour.
    seen = lit > 0.05
    chroma = np.zeros_like(lit)
    np.divide(high - low, np.maximum(high, 1e-6), out=chroma, where=seen)

    # The warm/cool split: how much warmer the lit half of the frame is than the shaded
    # half. This is the number that says "sunlit" — a picture whose light and shade agree
    # about colour reads as flat however much contrast it has, and one where they disagree
    # reads as a time of day. See the rig's note on why the colour lives in the difference.
    bright = lit > np.percentile(lit, 75)
    dark = lit < np.percentile(lit, 25)
    warm = lambda m: float((rgb[..., 0][m] - rgb[..., 2][m]).mean()) if m.any() else 0.0

    return {
        "mean": float(lit.mean()) * 255.0,
        "p10": float(np.percentile(lit, 10)) * 255.0,
        "p90": float(np.percentile(lit, 90)) * 255.0,
        "clip": float((high > 0.996).mean()) * 100.0,
        "chroma": float(chroma[seen].mean()) if seen.any() else 0.0,
        "warmth": warm(bright) * 255.0,
        "split": (warm(bright) - warm(dark)) * 255.0,
    }


def score(path: Path) -> dict[str, float]:
    rgb = read(path)
    share, depth = acne(rgb)
    out = {"acne": share, "acne_depth": depth * 255.0, "teal": teal(rgb)}
    out.update(tonality(rgb))
    return out


def shadow_acne(lit: Path, unlit: Path) -> float:
    """The part of the speckle the sun's shadow is responsible for, per million.

    The honest instrument, and the one this project already reaches for by hand: two frames
    that differ only in whether the sun casts differ only where a shadow falls. So the
    specks that are *in* the shadowed frame and *not* in the unshadowed one are the shadow
    map's, and everything else — a noisy normal map, a dithered sheet, debanding — is
    subtracted out rather than argued about.

    Read as a trend and not as a count. It cannot tell acne from the *edge* of an honest
    shadow, because both are a step that appears when the sun is switched on, so a rig that
    casts more shadow scores higher without being more broken: a softer, longer-shadowed
    town measured 12,365 against 8,516 for a crisper one with no acne visible in either. Use
    it to compare two rigs that shadow the same amount, and use `acne` — which requires a
    *lit* neighbourhood, so a shadow's interior and its edge both fail it — to ask whether a
    single frame is speckled at all.
    """
    a, b = read(lit), read(unlit)

    if a.shape != b.shape:
        return float("nan")

    dark = (b @ LUMA) - (a @ LUMA)
    high = dark - blur(dark, 2)

    return float((high > ACNE_STEP).mean()) * 1e6


def main() -> None:
    global WINDOW
    argv = sys.argv[1:]

    if not argv:
        print(__doc__)
        return

    # --against <dir>: score each frame and, where a same-named frame exists in that
    # directory, attribute its speckle to the sun.
    against = None

    while argv and argv[0].startswith("--"):
        if argv[0] == "--against":
            against = Path(argv[1])
            argv = argv[2:]
        elif argv[0].startswith("--crop="):
            WINDOW = tuple(int(v) for v in argv[0].split("=", 1)[1].split(","))
            argv = argv[1:]
        else:
            print(f"unknown option {argv[0]}")
            return

    paths = [Path(p) for p in argv if Path(p).is_file()]

    if not paths:
        print("no frames")
        return

    head = f"{'frame':<26} {'acne%':>7} {'deep':>5} {'teal':>7}"
    head += f" {'mean':>6} {'p10':>6} {'p90':>6} {'clip%':>6} {'chr':>5} {'warm':>6} {'split':>6}"
    if against:
        head += f" {'sun-acne':>9}"

    print(head)
    print("-" * len(head))

    totals: dict[str, list[float]] = {}

    for path in sorted(paths):
        s = score(path)
        line = (f"{path.name:<26} {s['acne']:7.3f} {s['acne_depth']:5.1f} {s['teal']:7.0f}"
                f" {s['mean']:6.1f} {s['p10']:6.1f} {s['p90']:6.1f} {s['clip']:6.2f}"
                f" {s['chroma']:5.3f} {s['warmth']:6.2f} {s['split']:6.2f}")

        if against:
            twin = against / path.name
            sun = shadow_acne(path, twin) if twin.exists() else float("nan")
            line += f" {sun:9.0f}"
            s["sun"] = sun

        print(line)

        for key, value in s.items():
            totals.setdefault(key, []).append(value)

    if len(paths) > 1:
        mean = {k: float(np.nanmean(v)) for k, v in totals.items()}
        print("-" * len(head))
        line = (f"{'mean of ' + str(len(paths)):<26} {mean['acne']:7.3f} {mean['acne_depth']:5.1f}"
                f" {mean['teal']:7.0f} {mean['mean']:6.1f} {mean['p10']:6.1f} {mean['p90']:6.1f}"
                f" {mean['clip']:6.2f} {mean['chroma']:5.3f} {mean['warmth']:6.2f}"
                f" {mean['split']:6.2f}")
        if against:
            line += f" {mean['sun']:9.0f}"
        print(line)


if __name__ == "__main__":
    main()
