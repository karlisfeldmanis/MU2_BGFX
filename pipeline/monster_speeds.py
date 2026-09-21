"""Resolves MU's three layers of monster play speed into the one table the exporter takes.

    python3 pipeline/monster_speeds.py \
        source/monsters/actions.json 9 \
        workshop/assets/monsters/Spider01/Spider01_speeds.json

The exporter wants what the player's rig already hands it: a flat `{"actions": {index:
{"play_speed": n}}}` with one number per action. The monster table cannot be written that
way, because a monster's speed is not a property of the action alone — it is the action's
default, then a blanket multiplier for a few models, then a walk set outright for a few
more, and the answer depends on which model is asking.

*Why the layers are kept rather than flattened by hand.* Flattening is a dozen numbers per
monster and there are forty-odd monsters, so it is four hundred numbers copied out of a
function that computes them from twenty. Every one of those is a chance to transcribe the
Giant's walk into the Yeti's row. Keeping MU's own shape means the table is checkable
against the client line by line, and this file is the only thing that has to be right about
the order.

*And the order is the whole of it.* OpenMonsterModel does the three passes in this
sequence, and two of them are not commutative:

1. Every action gets its default. Slots the client never names get the 0.25 the function
   opens with.
2. A multiplier scales models 3, 5, 25, 37 and 42 — across every action *but the death*.
   The client's loop stops before MONSTER01_DIE, so a slowed monster still dies at the
   normal rate. Applying it to the death too is the easy mistake and it makes the Giant
   take four seconds to fall over.
3. A handful of walks are set outright. This is an assignment and not a scale, so it
   *discards* whatever step 2 did rather than compounding with it. Multiplying instead
   would give any model in both lists a walk that is wrong by exactly the multiplier —
   which is nobody today, and would be the Cyclops the moment somebody adds one to the
   multiplier list. Order-dependent code that is right by luck is code that breaks when
   the data grows.

*The hold-at-end rule rides along, and it is not a speed.* It is copied through into the
resolved table because the export does depend on it, which is not obvious: a looping clip is
written with one extra key at the end holding the first key's pose, so that the wrap has an
interval to happen over. A death that holds never wraps, and that key makes its last moment
a blend back toward the pose the thing was standing in — a corpse that half gets up as it
falls. See the closing-key note in export_gltf.write_animations. The viewer needs the same
fact for a different reason, and reads it from actions.json itself; see viewer/Monsters.cs.
"""

import json
import sys
from pathlib import Path

#: The action that dies, and the one the multiplier pass stops before.
DIE = 6

#: What an action falls to when the client names no speed for it.
DEFAULT_PLAY_SPEED = 0.25

#: The walk, which is the only slot the third pass touches.
WALK = 2


def resolve(table: dict, model_index: int) -> dict[int, float]:
    """MU's speed for every action of one model, by the three passes in order."""
    actions = table.get("actions", {})
    multiplier = float(table.get("multipliers", {}).get(str(model_index), 1.0))
    speeds = {}

    for key, action in actions.items():
        index = int(key)
        speed = float(action.get("play_speed", DEFAULT_PLAY_SPEED))

        # Pass two. Everything below the death, which is left at its own rate.
        if index < DIE:
            speed *= multiplier

        speeds[index] = speed

    # Pass three, and an assignment rather than a scale — see the note above.
    if (walk := table.get("walk_overrides", {}).get(str(model_index))) is not None:
        speeds[WALK] = float(walk)

    return speeds


def main() -> None:
    if len(sys.argv) < 4:
        print(
            "usage: python3 monster_speeds.py <actions.json> <model index> <out.json>",
            file=sys.stderr,
        )
        raise SystemExit(2)

    table = json.loads(Path(sys.argv[1]).read_text())
    model_index = int(sys.argv[2])
    out = Path(sys.argv[3])

    speeds = resolve(table, model_index)
    named = table.get("actions", {})
    holds = {int(one) for one in table.get("hold_at_end", [])}

    # Written in the exporter's own shape, which is the player rig's shape, so that
    # export_gltf needs no idea that monsters exist. It reads a play_speed per action index
    # and that is all this has to be.
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({
        "note": (
            f"Resolved for model index {model_index} by pipeline/monster_speeds.py. "
            f"Derived — edit assets/monsters/actions.json, not this."
        ),
        "actions": {
            str(index): {
                "name": named.get(str(index), {}).get("name", f"Action {index}"),
                "play_speed": round(speed, 6),
                **({"hold_at_end": True} if index in holds else {}),
            }
            for index, speed in sorted(speeds.items())
        },
    }, indent=2) + "\n")

    print(f"=== monster speeds for model {model_index} ===")

    for index, speed in sorted(speeds.items()):
        name = named.get(str(index), {}).get("name", f"Action {index}")
        base = float(named.get(str(index), {}).get("play_speed", DEFAULT_PLAY_SPEED))
        note = "" if abs(speed - base) < 1e-9 else f"  (was {base:g})"
        note += "  holds on its last key" if index in holds else ""
        print(f"  {index:2d}  {name:<10} {speed:.3g} -> {speed * 25:.4g} keys/s{note}")

    print(f"  wrote      {out}")


if __name__ == "__main__":
    main()
