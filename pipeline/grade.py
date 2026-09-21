"""Bakes a colour grade into a lookup table the engine can apply in one instruction.

    python3 pipeline/grade.py source/grades/lorencia.json workshop/grades

Three sliders — brightness, contrast, saturation — are what a renderer gives you for free and
they cannot express a look. They move every colour the same way. What separates a graded
frame from an ungraded one is that it moves colours *differently depending on what they are*:
shadows go one way and highlights another, some hues are held while others are let go, and
the curve through the middle is not a straight line.

That is a lookup table. Every colour the renderer produces is a coordinate in a cube, the
cube says what colour to put there instead, and the whole grade costs one texture read
however complicated it was to decide.

The cube is written as a strip: 33 slices of 33 by 33, laid left to right, which is 1089 by
33 pixels. Godot wants a Texture3D and the viewer assembles one from the slices; a strip is
used rather than 33 files because a grade is one thing and should arrive as one file, and
because it can be looked at, which a 3D texture cannot.

33 rather than 32 on purpose: with an odd size there is a sample exactly at the middle of
each axis, so a grade that does nothing is exactly identity at grey rather than nearly it.

What the stages are and the order they run in — the order matters more than any single
figure, and this is the order a colourist works in:

    exposure -> white balance -> contrast about a pivot -> split tone -> saturation -> lift

Each is argued for where it is applied.
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

#: Samples along each axis of the cube. Odd, so grey is a sample rather than an interpolation.
SIZE = 33

#: The tone curve's pivot, in linear light: the value that does not move when contrast is
#: applied. 0.18 is middle grey and is where every photographic curve is hinged, because it
#: is the point the eye reads as "neither light nor dark".
PIVOT = 0.18


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    """The renderer's output is sRGB-encoded; grading has to happen in light."""
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.clip(c, 0, None) ** (1 / 2.4) - 0.055)


def luma(rgb: np.ndarray) -> np.ndarray:
    """Rec. 709, which is what the eye reads as brightness rather than a flat average."""
    return (0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2])[..., None]


def build(grade: dict) -> np.ndarray:
    """The cube, as SIZE^3 sRGB triples in 0..1."""
    axis = np.linspace(0.0, 1.0, SIZE, dtype=np.float32)

    # Blue is the slowest axis because that is how the strip is laid out: slice n of the
    # strip is the plane at blue = n.
    b, g, r = np.meshgrid(axis, axis, axis, indexing="ij")
    rgb = srgb_to_linear(np.stack([r, g, b], axis=-1))

    # --- exposure -------------------------------------------------------------------------
    # A stop is a doubling, so this is expressed in stops rather than in multiples: "half a
    # stop down" is a thing a person can picture and 0.707 is not.
    rgb = rgb * (2.0 ** grade.get("exposure_stops", 0.0))

    # --- white balance --------------------------------------------------------------------
    # A flat multiply per channel, before anything shapes the image. It is the one correction
    # that belongs at the front: everything after it is deciding what to do with a colour, and
    # doing that to a colour cast is deciding it wrong.
    balance = np.asarray(grade.get("balance", [1.0, 1.0, 1.0]), dtype=np.float32)
    rgb = rgb * balance

    # --- contrast about the pivot -----------------------------------------------------------
    # A power curve hinged at middle grey rather than a linear stretch about 0.5. Stretching
    # about 0.5 in sRGB is what a "contrast" slider does and it crushes the shadows first,
    # because in linear light 0.5 sRGB is 0.21 and everything below it is already compressed.
    contrast = float(grade.get("contrast", 1.0))
    if contrast != 1.0:
        rgb = PIVOT * np.power(np.maximum(rgb, 1e-6) / PIVOT, contrast)

    # Out of light and into display space for everything that follows, and this is not a
    # detail. Adding a tint to a linear value near zero is enormous once it is encoded: a
    # shadow tint of 0.011 in linear light puts 28 of 255 into black, which came out as a
    # frame whose darkest pixel was navy. Tinting, saturation and lift are all decisions about
    # what a picture *looks* like, so they belong where looking happens.
    rgb = linear_to_srgb(np.clip(rgb, 0.0, None))

    # --- split tone --------------------------------------------------------------------------
    # The single most recognisable thing in a modern game's look, and the one three sliders
    # cannot do at all: the shadows are pushed one way in hue and the highlights the other.
    #
    # Cool shadows and warm highlights is the near-universal choice and it is not arbitrary -
    # it is what daylight does. Sunlight is warm and the light in shadow is the sky, which is
    # blue, so an image graded that way reads as lit by the sun even when nothing in the
    # render says so. Push it far and it is a look; push it a little and it is just daylight.
    #
    # Weighted by luminance so it is a tint rather than a wash: shadows get theirs where the
    # image is dark and highlights get theirs where it is bright, and the midtones - which is
    # where skin and stone and grass live - are left nearly alone.
    lum = np.clip(luma(rgb), 0.0, 1.0)
    shadow = np.asarray(grade.get("shadow_tint", [0, 0, 0]), dtype=np.float32)
    highlight = np.asarray(grade.get("highlight_tint", [0, 0, 0]), dtype=np.float32)

    # Squared, so each falls away from its own end quickly and neither reaches the middle.
    rgb = rgb + shadow * ((1.0 - lum) ** 2) + highlight * (lum ** 2)

    # --- saturation, and how much of it survives into the shadows ---------------------------
    # Two figures rather than one. Saturation alone applied flat is what makes a graded frame
    # look like a filter: the bright parts get lurid and the dark parts get muddy colour
    # nobody can see anyway, which then shows up as noise.
    #
    # Real film loses chroma as it goes down. shadow_saturation is how much is left at black -
    # under one is the filmic direction and it is most of why a dark frame reads as expensive
    # rather than as underexposed.
    saturation = float(grade.get("saturation", 1.0))
    floor = float(grade.get("shadow_saturation", 1.0))

    reach = floor + (1.0 - floor) * np.clip(lum * 2.0, 0.0, 1.0)
    grey = luma(rgb)
    rgb = grey + (rgb - grey) * saturation * reach

    # --- the toe goes grey --------------------------------------------------------------------
    # Chroma to zero at true black, ramping to untouched by `toe_desaturation` of luma.
    # Separate from shadow_saturation, which shapes how the visible shade holds colour;
    # this one exists for pixels the renderer gets wrong. The Mobile renderer was measured
    # emitting pure blue - (0, 0, 63) of 255, no grade - at a grazing crease on the bare
    # Knight's boot cuff, a hue no light in the scene carried after every source was
    # neutralised in turn. A pure hue at near-zero luma is exactly what this ramp erases
    # and exactly what no honest pixel is: film has no chroma at black, so nothing real
    # lives below the ramp. Draining shadow_saturation instead was tried and flattened
    # the whole shade for one sliver.
    toe = float(grade.get("toe_desaturation", 0.0))
    if toe > 0.0:
        keep = np.clip(lum / toe, 0.0, 1.0)
        keep = keep * keep * (3.0 - 2.0 * keep)
        rgb = grey + (rgb - grey) * keep

    # --- shoulder -----------------------------------------------------------------------------
    # A soft rolloff above a knee, and it exists because of what runs before it. The contrast
    # curve and the highlight tint both push the top end up — that is their job — and the cube
    # ends in a hard clip at 1.0. Applied in the engine *after* ACES has already rolled the
    # highlights off, that clip re-steepens them into the ceiling: measured on a World-tab
    # frame, 1.5% of pixels clipped with the grade against 0.0% without it, all of it sunlit
    # roof and paving with the detail flattened out. "The game is over-exposed" was the cube's
    # top rows, not the exposure.
    #
    # Exponential past the knee: continuous in value and slope where it joins, approaching 1.0
    # and never arriving. Below the knee not a single value moves, which is the point — the
    # shadows and mids are the look and the look is right.
    knee = float(grade.get("shoulder", 1.0))
    if knee < 1.0:
        head = 1.0 - knee
        rgb = np.where(rgb > knee, 1.0 - head * np.exp(-(rgb - knee) / head), rgb)

    # --- lift ---------------------------------------------------------------------------------
    # Black is not black. Every photographic process has a floor it cannot go below - fog on
    # the film, flare in the lens - and a frame whose darkest pixel is 0,0,0 is the single
    # loudest tell that nothing happened to it. This is small on purpose; at anything much
    # larger it stops reading as air and starts reading as a washed-out screen.
    lift = np.asarray(grade.get("lift", [0.0, 0.0, 0.0]), dtype=np.float32)
    rgb = rgb + lift * (1.0 - np.clip(lum, 0.0, 1.0))

    return np.clip(rgb, 0.0, 1.0)


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 grade.py <grade.json> <out dir>", file=sys.stderr)
        raise SystemExit(2)

    source, out = Path(sys.argv[1]), Path(sys.argv[2])
    grade = json.loads(source.read_text())
    out.mkdir(parents=True, exist_ok=True)

    cube = build(grade)

    # Laid out as slices side by side: x within a slice is red, y is green, and which slice
    # it is, is blue.
    strip = np.concatenate([cube[n] for n in range(SIZE)], axis=1)
    image = Image.fromarray((strip * 255.0).round().astype(np.uint8), "RGB")

    target = out / f"{source.stem}.png"
    image.save(target, "PNG", optimize=True)

    # What it actually does to a few colours worth knowing about, because a cube is not
    # something anybody can read.
    print(f"\n=== {grade.get('name', source.stem)} ===")
    print(f"  {grade.get('summary', '')}")
    print(f"  cube       {SIZE}^3 as a {image.width}x{image.height} strip  -> {target.name}")

    def show(label: str, colour: tuple[float, float, float]) -> None:
        at = [min(SIZE - 1, max(0, int(round(c * (SIZE - 1))))) for c in colour]
        was = tuple(round(c * 255) for c in colour)
        now = tuple(int(round(v * 255)) for v in cube[at[2], at[1], at[0]])
        print(f"  {label:11s} {str(was):>16s}  ->  {str(now)}")

    show("black", (0.0, 0.0, 0.0))
    show("shadow", (0.12, 0.12, 0.13))
    show("mid grey", (0.5, 0.5, 0.5))
    show("highlight", (0.85, 0.85, 0.82))
    show("white", (1.0, 1.0, 1.0))
    show("grass", (0.30, 0.34, 0.14))
    show("skin", (0.76, 0.57, 0.44))


main()
