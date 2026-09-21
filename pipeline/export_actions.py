"""Writes a rig's clips out once, as a file with a skeleton in it and no model.

    Blender --background --python pipeline/export_actions.py -- \
        source/players/rig/player.rig.json \
        workshop/players/rig/actions/player.glb \
        --speeds=source/players/rig/actions.json

The counterpart to export_gltf's --no-clips. A character is five worn parts skinned to one
sixty-bone rig, and until this existed each part shipped its own copy of all 283 of that
rig's actions: fifteen files under build/players/body holding byte-identical animation
data, 13.4 MB of the 13.7 MB each. Measured, not estimated — the fifteen hash the same.

The cost was not the disk. Loading the five parts of a character took 2.05 seconds, almost
all of it glTF parsing, and four fifths of what was parsed was freed on the next line:
Model.Load collapses the parts onto one skeleton and keeps the first AnimationPlayer. The
same five parts without the clips load in 81 ms and this file's library in 70.

## Why it is a .glb and not the end of the chain

Because the thing the viewer wants is a Godot AnimationLibrary, and the only correct way to
get one is to let Godot's own glTF importer build it. That importer decides how a channel
aimed at a joint node becomes a track named `Skeleton3D:Bip01 R Thigh`, and a track named
anything else binds to nothing. So this writes glTF and pack_actions.gd converts it, and
the conversion is a load and a save with no interpretation in between.

## Why there is no mesh in it

There does not need to be one. A glTF skin with no mesh on it still gives Godot a
Skeleton3D and the same track paths — checked, because the alternative was a degenerate
triangle carried for the rest of the project's life to satisfy a requirement that turned
out not to exist.

The node the skin hangs off remains, because glTF binds a skin to a node rather than to a
document, and its name is the rig's. It arrives in Godot as an empty Node3D beside the
skeleton and is dropped with the rest of the scene once the library is out.
"""

import json
import sys
from pathlib import Path

# Beside this file, which is not on Blender's path — a script run by --python is executed,
# not imported, so its directory never joins sys.path the way a module's would.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from export_gltf import (                                            # noqa: E402
    Binary, DEFAULT_PLAY_SPEED, read_closes, read_speeds, skeleton_of, write_animations,
    write_glb, write_skin)


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    if len(argv) < 2:
        print("usage: ... --python export_actions.py -- <rig.json> <out.glb> "
              "[--speeds=<actions.json>] [--play-speed=<n>]")
        return

    rig_path = Path(argv[0])
    destination = Path(argv[1])

    speeds_path = None
    play_speed = DEFAULT_PLAY_SPEED

    for argument in argv[2:]:
        if argument.startswith("--speeds="):
            speeds_path = Path(argument.split("=", 1)[1])
        elif argument.startswith("--play-speed="):
            play_speed = float(argument.split("=", 1)[1])

    rig = json.loads(rig_path.read_text())
    speeds, holds = read_speeds(speeds_path)

    bones = skeleton_of(rig)
    binary = Binary()

    document: dict = {
        "asset": {"version": "2.0", "generator": "MU2 pipeline/export_actions.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": rig_path.stem}],
    }

    # No binds passed, so the inverse binds are the rig's own rest. Nothing is skinned to
    # them here — this file carries no vertices — but the skin is what makes Godot build a
    # Skeleton3D, and a Skeleton3D is what the track paths are written against.
    first_joint = write_skin(document, binary, bones, 0)

    clips = write_animations(document, binary, rig, first_joint, len(bones),
                             speeds, play_speed, holds, closes=read_closes(speeds_path))

    if not clips:
        print(f"error: {rig_path} has no actions in it; nothing to write", file=sys.stderr)
        return

    document["bufferViews"] = binary.views
    document["accessors"] = binary.accessors

    write_glb(destination, document, binary.blob)

    print(
        f"\n=== {rig_path.name} -> {destination.name} ===\n"
        f"  bones      {len(bones)}\n"
        f"  clips      {len(clips)}\n"
        f"  wrote      {destination} ({destination.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
