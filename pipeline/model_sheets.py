"""Which sheet each submesh of one of MU's models wears, read from the model itself.

    python3 pipeline/model_sheets.py reference/MuMain/src/bin/Data/Object1/Well02.bmd
    python3 pipeline/model_sheets.py <Data/Object1> --all

This replaces reading the names out with a byte scan, which is what was being done and which
is wrong in a way that costs real work.

A .bmd stores each submesh's texture as a **fixed 32-byte field, terminated by the first
NUL**. The rest of the field is whatever was in the writer's buffer beforehand, and MU's
tooling did not clear it. Well02's field reads:

    well.jpg\\0tue02.jpg\\0.jpg\\0\\0\\0...

`well.jpg` is the texture. `tue02.jpg` is the tail of a longer name a previous model used,
still sitting in the buffer, and `.jpg` is the tail of the tail. A scan for anything ending
in `.jpg` finds all three and reports a one-sheet model as needing three, two of which do not
exist anywhere in MU's data. That was the state of the reconnaissance for Lorencia's last
27 objects: 6 of the 32 sheets it said were needed were ghosts - `tue02`, `e02`, `ess2`,
`ow01`, `1`, `3` - and each would have been an hour of looking for a file that was never
written.

The lesson is narrow and worth keeping: a name in a fixed-width field ends where the field
says it ends, not where the next plausible-looking string begins. Read the structure.

The layout, from the reference reader in LEGACY/tools/MuAssets:

    header   name[32], meshCount i16, boneCount i16, actionCount i16
    mesh     vertexCount i16, normalCount i16, texCoordCount i16, triangleCount i16,
             textureIndex i16,
             vertex[16] * n, normal[20] * n, texCoord[8] * n, triangle[64] * n,
             textureName[32]

Version 10 is plain and its body starts at offset 4. Version 12 wraps the body in MuCrypt
with a 4-byte length in front of it; Lorencia's objects are all version 10, so 12 is decrypted
using the same routine terrain.py already has and 14 is refused outright rather than guessed
at.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

NAME_LENGTH = 32

#: Bytes per record, in the order a mesh stores them.
VERTEX, NORMAL, TEXCOORD, TRIANGLE = 16, 20, 8, 64


def text(field: bytes) -> str:
    """A fixed-width name field, up to the first NUL and no further."""
    return field.split(b"\x00", 1)[0].decode("latin-1").strip()


def body(blob: bytes) -> bytes:
    """The model's payload, decrypted if this version encrypts it."""
    version = blob[3]

    if version == 10:
        return blob[4:]

    if version == 12:
        from terrain import decrypt

        length = struct.unpack_from("<i", blob, 4)[0]
        return decrypt(blob[8:8 + length])

    raise SystemExit(f"error: BMD version {version} is not one this reads")


def sheets(path: Path) -> list[str]:
    """The texture each submesh wears, in submesh order, with blanks kept.

    Blanks are kept rather than dropped because the position is the meaning: submesh 2
    wearing nothing is a different fact from the model having two submeshes, and the
    exporter's slot list is indexed the same way.
    """
    data = body(path.read_bytes())
    at = NAME_LENGTH
    meshes, _bones, _actions = struct.unpack_from("<3h", data, at)
    at += 6

    found = []

    for _ in range(meshes):
        vertices, normals, coords, triangles, _texture = struct.unpack_from("<5h", data, at)
        at += 10
        at += vertices * VERTEX + normals * NORMAL + coords * TEXCOORD + triangles * TRIANGLE
        found.append(text(data[at:at + NAME_LENGTH]))
        at += NAME_LENGTH

    return found


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: python3 model_sheets.py <model.bmd | folder --all>", file=sys.stderr)
        raise SystemExit(2)

    target = Path(sys.argv[1])

    if target.is_dir():
        models = sorted(target.glob("*.bmd"), key=lambda p: p.name.lower())
    else:
        models = [target]

    print()

    for model in models:
        try:
            worn = sheets(model)
        except (SystemExit, struct.error, IndexError) as bad:
            print(f"  {model.stem:20s} unreadable: {bad}")
            continue

        listed = " ".join(one or "(none)" for one in worn)
        print(f"  {model.stem:20s} {len(worn):2d} submesh(es)  {listed}")

    print()


if __name__ == "__main__":
    main()
