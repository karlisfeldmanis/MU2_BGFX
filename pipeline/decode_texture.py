"""Reads MU's own texture files out into PNG, at their own size and nothing else.

    python3 pipeline/decode_texture.py \
        LEGACY/reference/remaster_one/Data/Item/sword02.OZJ \
        source/textures/sword02.png

The first link in the texture chain, and the one that has to be beyond question. Every map
this pipeline ever produces is derived from what comes out of here, so this step decodes
and does nothing else: no resize, no sharpen, no colour correction, no cleanup of the
JPEG's own ringing. What MU shipped is what lands.

That sounds obvious and it is the thing that had already gone wrong. The albedo the
pipeline was baking from was a 256x64 file, and sword02.OZJ is 64x16 — the art had been
through a four-times neural upscale before it ever reached the mesh, so three quarters of
every edge on that blade was a model's invention rather than MU's painting. It looked
like better source art. It was a guess about what better source art might have been.

*The formats.* MU wraps ordinary images in a small header and renames them:

- ``.OZJ`` is a JPEG behind 24 bytes of header.
- ``.OZT`` is a TGA behind 4 bytes of header, and is the one that carries alpha.

Both are stripped and handed to a real decoder rather than reimplemented. A JPEG decoder
written here would be a second implementation to disagree with the first.

*Why not read the .OZJ at run time.* Because the point of a source directory is that the
thing downstream of it is reproducible from files you can open. A PNG at MU's own
resolution is that, and it costs a few kilobytes.
"""

import struct
import sys
from pathlib import Path

from PIL import Image

#: What MU prepends. Stripping these leaves a file a decoder recognises.
HEADERS = {".ozj": 24, ".ozt": 4}

#: MuDream's own wrappers, which are not headers but a three-byte XOR laid over the
#: whole file: .pdream is a PNG, .tdream a TGA, .jdream a JPEG. The keys were read off the
#: files themselves — the PNG signature, a TGA's zeroed header and a JPEG's FF D8 FF E0
#: each give the key away in the first three bytes, and it repeats from there.
KEYS = {
    ".pdream": bytes((0x3A, 0xEC, 0x29)),
    ".tdream": bytes((0x58, 0xB6, 0x85)),
    ".jdream": bytes((0x8D, 0x39, 0x1C)),
}


def decode(source: Path) -> Image.Image:
    """The image inside one of MU's wrappers, at its own size and colour."""
    from io import BytesIO

    suffix = source.suffix.lower()

    if suffix in KEYS:
        key = KEYS[suffix]
        raw = source.read_bytes()
        payload = bytes(byte ^ key[i % 3] for i, byte in enumerate(raw))
        image = Image.open(BytesIO(payload))
        image.load()
        return image

    skip = HEADERS.get(suffix)
    if skip is None:
        # A plain image, which the later worlds' art sometimes is.
        return Image.open(source)

    payload = source.read_bytes()[skip:]

    image = Image.open(BytesIO(payload))
    image.load()
    return image


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: python3 decode_texture.py <in.OZJ|in.OZT|in.png> [out.png]")
        return

    source = Path(sys.argv[1])
    destination = (
        Path(sys.argv[2]) if len(sys.argv) > 2
        else source.with_suffix(".png"))

    if not source.exists():
        raise SystemExit(f"error: no such file: {source}")

    image = decode(source)

    # RGBA throughout, even where the source has no alpha. One channel layout downstream is
    # worth more than the few bytes an opaque alpha channel costs, and the OZT files that
    # do carry alpha are the same kind of file as the ones that do not.
    if image.mode != "RGBA":
        image = image.convert("RGBA")

    # An alpha channel that is zero everywhere is not a mask, it is an absent one.
    #
    # Data/Player/guard_hair.ozt — the Plate helm's plume — stores 0 in the alpha byte of
    # every one of its 1024 texels while holding perfectly good colour. Read literally that
    # says "draw none of this", and the plume MU quite visibly draws would vanish. Whatever
    # the client does with that file, it is not reading it as opacity.
    #
    # So a uniformly transparent channel is filled in as opaque and said so out loud. The
    # ones that carry a real mask are unaffected: hound_shield1 runs 0 to 255 and cuts away
    # 15% of itself, which is the shield's fretwork and is exactly what this must not lose.
    empty = image.getchannel("A").getextrema() == (0, 0)
    if empty:
        image.putalpha(255)

    opaque = image.getchannel("A").getextrema() == (255, 255)

    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination, "PNG", optimize=True)

    if empty:
        state = "was zero throughout — filled in as opaque, see the note"
    elif opaque:
        state = "none (fully opaque)"
    else:
        cut = sum(1 for value in image.getchannel("A").getdata() if value < 128)
        state = f"present, cutting away {cut * 100 // (image.width * image.height)}%"

    print(
        f"\n=== {source.name} -> {destination.name} ===\n"
        f"  size       {image.width} x {image.height}\n"
        f"  alpha      {state}\n"
        f"  wrote      {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
