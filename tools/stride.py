#!/usr/bin/env python3
"""What a walk clip does with the feet, and what playback rate slides them least.

    python3 tools/stride.py                        the player's walk, action17
    python3 tools/stride.py --clip action15        bare-handed
    python3 tools/stride.py --glb assets/monsters/BudgeDragon01/BudgeDragon01.glb --clip action2
    python3 tools/stride.py --gait 2.5             at a different ground speed

Why this exists. A walk slides when the ground moves under a foot that is supposed to be
holding still, and the only cure available to an engine is the RATE it plays the clip at:
too slow and the man moonwalks, too fast and he skates forward. Picking that rate from
`action_travel` -- metres per cycle, which MU2's pipeline measured and this cook carries
into every `.muc` -- is what game/play.cpp does. This says whether that was the right number
and what it leaves behind, by doing the arithmetic on the source animation rather than on a
screenshot of it.

It works on the .glb because that is where the truth is. The cook is trusted everywhere else
in this project; here it is deliberately gone round, so that a mistake in the cook's own
`travel` shows up as a disagreement rather than as two files agreeing with each other.

What it prints, and how to read it:

* **The root's travel.** MU's locomotion is authored IN PLACE -- `Bip01` does not move -- and
  the engine translates the body itself. A clip whose root DOES travel would be moved twice,
  and this is the line that would say so.
* **Each foot's stride**, in metres, and the per-key steps it is made of. MU's walk is seven
  keys, and NO key has a foot standing still: the smallest step is about a fifth of a metre.
  That is the finding that decides how good a walk can get -- see below.
* **The slide against the playback rate.** The engine's frame loop, run in arithmetic: the
  clip sampled as the engine samples it, the body advanced at the gait, and the slower foot's
  speed over the ground taken every frame, which is the one bearing weight.

The answer for MU's own walk, measured 2026-09-20: the curve bottoms out flat between about
0.8 and 1.0, the rate the cook's travel asks for (0.961 for a Dark Knight) sits in that
bottom, and the slide it leaves is about 0.85 m/s mean. That residue is not the engine's and
no rate removes it -- it is in the animation, whose feet never stop moving. Fixing it would
mean re-authoring MU's seven keys, which is a different decision from playing them properly.
"""
import argparse
import json
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_glb(path):
    with open(path, "rb") as handle:
        magic, _version, _length = struct.unpack("<III", handle.read(12))
        if magic != 0x46546C67:
            raise ValueError(f"{path} is not a .glb")
        chunk, _kind = struct.unpack("<II", handle.read(8))
        document = json.loads(handle.read(chunk))
        length, _kind = struct.unpack("<II", handle.read(8))
        return document, handle.read(length)


def reader(document, binary):
    def accessor(index):
        acc = document["accessors"][index]
        view = document["bufferViews"][acc["bufferView"]]
        start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        code, width = {5126: ("f", 4), 5123: ("H", 2), 5121: ("B", 1),
                       5125: ("I", 4)}[acc["componentType"]]
        wide = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]
        out = []
        for element in range(acc["count"]):
            values = struct.unpack_from("<" + code * wide, binary,
                                        start + element * width * wide)
            out.append(values if wide > 1 else values[0])
        return out
    return accessor


def matrix(q):
    """A quaternion as a 3x3, rows first -- the same layout core/maths.h composes."""
    x, y, z, w = q
    return [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]


class Pose:
    """One clip, sampled by forward kinematics, exactly as the engine walks the hierarchy."""

    def __init__(self, document, binary, clip):
        self.nodes = document["nodes"]
        self.parent = {}
        for index, node in enumerate(self.nodes):
            for child in node.get("children", []):
                self.parent[child] = index
        self.named = {node.get("name", ""): i for i, node in enumerate(self.nodes)}

        animations = {a.get("name", str(i)): a for i, a in enumerate(document["animations"])}
        if clip not in animations:
            raise SystemExit(f"no clip called {clip}; this file has "
                             f"{len(animations)}: {', '.join(sorted(animations)[:8])}...")
        animation = animations[clip]
        accessor = reader(document, binary)
        self.rotation, self.translation, self.times = {}, {}, None
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            node = channel["target"]["node"]
            if channel["target"]["path"] == "rotation":
                self.rotation[node] = accessor(sampler["output"])
            elif channel["target"]["path"] == "translation":
                self.translation[node] = accessor(sampler["output"])
            if self.times is None:
                self.times = accessor(sampler["input"])

    def local(self, node, key):
        rest = self.nodes[node]
        q = (self.rotation[node][key] if node in self.rotation
             else tuple(rest.get("rotation", (0.0, 0.0, 0.0, 1.0))))
        t = (self.translation[node][key] if node in self.translation
             else tuple(rest.get("translation", (0.0, 0.0, 0.0))))
        return matrix(q), list(t)

    def world(self, node, key):
        m, t = self.local(node, key)
        parent = self.parent.get(node)
        if parent is None:
            return m, t
        pm, pt = self.world(parent, key)
        out = [sum(m[r * 3 + k] * pm[k * 3 + c] for k in range(3))
               for r in range(3) for c in range(3)]
        place = [pt[i] + sum(t[k] * pm[k * 3 + i] for k in range(3)) for i in range(3)]
        return out, place

    def track(self, bone):
        """Where a bone is at every key, plus the closing key the cook appends."""
        if bone not in self.named:
            return None
        node = self.named[bone]
        points = [self.world(node, key)[1] for key in range(len(self.times))]
        points.append(points[0])
        return points


def stance(track, cycle):
    """(metres, seconds) of the phase a foot spends low and travelling backwards.

    The stance is what the eye judges: the foot is on the ground and the world is supposed to
    be passing it at exactly the speed the body moves. The swing -- the same foot in the air,
    going the other way at twice the speed -- can be a little wrong without anybody seeing it.
    So the honest rate is the one that makes THIS phase still, and it is not quite the one the
    whole cycle's travel asks for.
    """
    keys = len(track) - 1
    interval = cycle / keys
    low = min(p[1] for p in track) + 0.05     # within 5 cm of the foot's lowest point
    far = 0.0
    took = 0.0
    for i in range(keys):
        a, b = track[i], track[i + 1]
        if b[2] - a[2] >= 0.0:                # forward: that is the swing
            continue
        if a[1] > low or b[1] > low:          # in the air at either end
            continue
        far += a[2] - b[2]
        took += interval
    return far, took


def slide(points, cycle, gait, rate, frames=2000, hz=180.0):
    """The weight-bearing foot's speed over the ground, sampled as the engine samples."""
    # Per KEY, off one of the tracks. `points` is keyed by bone, so its own length is the
    # number of feet -- which is two, and a cycle divided by one is a clip sampled at a single
    # key held for its whole length. That mistake made every rate look equally bad, which is
    # exactly what a measurement that is silently not measuring looks like.
    interval = cycle / (len(next(iter(points.values()))) - 1)

    def at(track, when):
        when %= cycle
        first = min(int(when / interval), len(track) - 2)
        through = (when - first * interval) / interval
        a, b = track[first], track[first + 1]
        return [a[i] + (b[i] - a[i]) * through for i in range(3)]

    step = 1.0 / hz
    clock = 0.0
    was = {name: at(track, 0.0) for name, track in points.items()}
    speeds = []
    for _ in range(frames):
        clock += step * rate
        now = {name: at(track, clock) for name, track in points.items()}
        # The model looks down +z (docs/conventions.md), so the ground goes past a foot in -z.
        over = []
        for name in points:
            dz = (now[name][2] - was[name][2]) + gait * step
            dx = now[name][0] - was[name][0]
            over.append(math.hypot(dx, dz) / step)
        speeds.append(min(over))  # the slower foot is the one taking the weight
        was = now
    speeds.sort()
    return (sum(speeds) / len(speeds), speeds[len(speeds) // 2], speeds[len(speeds) // 10])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glb", default="assets/players/rig/player.actions.glb")
    parser.add_argument("--clip", default="action17", help="MU's own action name")
    parser.add_argument("--gait", type=float, default=2.5,
                        help="metres a second over the ground; a Dark Knight's own is 2.5")
    parser.add_argument("--feet", default="Bip01 L Foot,Bip01 R Foot")
    args = parser.parse_args()

    path = args.glb if os.path.isabs(args.glb) else os.path.join(ROOT, args.glb)
    document, binary = read_glb(path)
    pose = Pose(document, binary, args.clip)
    keys = len(pose.times)
    interval = pose.times[1] - pose.times[0]
    # The cook closes a looping clip -- it appends the first key again and extends the duration
    # by one interval -- so the cycle is one interval longer than the key times say. A cycle
    # taken as `times[-1]` is a sixth short on a seven-key walk, and every rate derived from it
    # is a sixth wrong.
    cycle = interval * keys
    print(f"{args.clip}: {keys} keys, {interval * 1000:.1f} ms apart, "
          f"cycle {cycle:.4f} s with the cook's closing key")

    for bone in ("Bip01", "Bip01 Root", "Bip01 Pelvis"):
        track = pose.track(bone)
        if not track:
            continue
        xs = [p[0] for p in track]
        zs = [p[2] for p in track]
        moved = max(max(xs) - min(xs), max(zs) - min(zs))
        print(f"  {bone}: travels {moved:.4f} m over the cycle"
              f"{'  -- IN PLACE, as MU authors locomotion' if moved < 0.01 else ''}")
        break

    tracks = {}
    for bone in args.feet.split(","):
        track = pose.track(bone)
        if not track:
            print(f"  no bone called {bone}")
            continue
        tracks[bone] = track
        steps = [math.dist(track[i], track[i + 1]) for i in range(len(track) - 1)]
        zs = [p[2] for p in track]
        print(f"  {bone}: stride {max(zs) - min(zs):.4f} m, travels {sum(steps):.4f} m a "
              f"cycle, smallest key step {min(steps):.3f} m")
    if not tracks:
        return 1

    # What the clip's own stance says the rate should be, against what its whole-cycle travel
    # says. The two disagree because the swing is not the mirror of the stance.
    for bone, track in tracks.items():
        far, took = stance(track, cycle)
        if took > 0.0:
            print(f"  {bone}: stance {far:.4f} m over {took:.4f} s = {far / took:.3f} m/s, "
                  f"so rate {args.gait / (far / took):.3f} plants it")
    travelled = sum(math.dist(t[i], t[i + 1]) for t in tracks.values()
                    for i in range(len(t) - 1)) / len(tracks)
    print(f"  the whole cycle's travel asks for rate "
          f"{args.gait / (travelled / cycle):.3f}")

    smallest = min(min(math.dist(t[i], t[i + 1]) for i in range(len(t) - 1))
                   for t in tracks.values())
    if smallest > 0.05:
        print(f"  NOTE no key has a foot still (the smallest step is {smallest:.3f} m), so "
              f"some slide is in the animation and no rate removes it")

    print()
    print(f"  at {args.gait:.2f} m/s over the ground, the weight-bearing foot slides:")
    print("   rate   a cycle covers   mean    median   best tenth")
    best = None
    rates = [0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.961, 0.98, 1.0, 1.02, 1.05, 1.1, 1.2, 1.4]
    for rate in rates:
        mean, median, tenth = slide(tracks, cycle, args.gait, rate)
        print(f"  {rate:5.3f}   {args.gait * cycle / rate:6.3f} m      "
              f"{mean:5.2f}   {median:5.2f}    {tenth:5.2f}")
        if best is None or tenth < best[1]:
            best = (rate, tenth)
    # Ranked on the best tenth and not on the mean, because the mean is mostly the swinging
    # foot -- which is in the air, is going the other way, and is nobody's idea of a slide.
    # The tenth is the frames where a foot is genuinely bearing weight.
    print(f"  the planted frames are stillest at rate {best[0]:.3f}, "
          f"leaving {best[1]:.2f} m/s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
