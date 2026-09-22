#!/usr/bin/env python3
"""The picture, checked against a picture that was right.

The sim has had a regression check since sprint 5: `sim_test` runs a seeded hunt and compares
a fingerprint, so nobody can move a rule by accident. The FRAME had nothing. Every refactor,
every shader edit and every pass reordering was verified by a person opening a PNG and
looking at it, which catches a black screen and misses a wrong roughness for weeks.

This is the other half. It draws three pinned scenes and demands they come out EXACTLY as the
committed references -- not approximately, exactly, byte for byte in pixels. That is a real
bar and not an aspiration: measured over ten runs each, these scenes come out identical to the
last bit. A tolerance would be a place for a regression to hide, so there isn't one.

There is ONE known flake and it is not a tolerance. `town` draws a dropped item the reference
does not have in roughly one run in fifteen; the cause is not found. A mismatch is therefore
drawn a second time before it is believed, and only a scene that differs twice fails. A real
regression is deterministic and fails both draws.

What the three cover between them:

  town      PlayMode: the world, the town's chunks, a posed figure, cast shadows, the HUD,
            the windows, the tile label, and the fountain's scrolling water
  browser   BenchMode/Browser: the bench's own camera and ground, and the overlay's list
  studio    BenchMode/Studio: the effects pass -- the bonfire's flames and smoke -- over a
            world opened behind a bench, with the lamps' light grid laid

Why it is exact, and what had to be fixed before it could be. `--fixed-dt` did not reach the
picture: `Renderer::draw` took its clock off `bx::getHPCounter()`, so the water scrolled by
how long the PROCESS had been alive -- which sweeps up the preloader's duration and varies
with the disk cache. Two runs of one pinned command differed in 3.5% of the pixels. The clock
is seconds of play now (`Renderer::setClock`), and the difference is zero.

NOT in the `checks` target, on purpose: `checks` is "everything that can say no WITHOUT a
window", and this needs a device, a display and about twenty seconds. Run it before a commit
that touched the renderer, a shader, a pass or the frame loop:

    cmake --build build --target shotcheck      # or tools/shotcheck.py

A reference is only allowed to move when the change was MEANT to move it. To move one, look
at what it does to the picture first and then:

    tools/shotcheck.py --bless [scene ...]

and say in the commit message what changed and why, with the old and new both looked at.
Blessing a reference to make a red check green is how this whole idea stops being worth
anything.

The references are this Mac's Metal. Another GPU or driver will differ and that is expected;
this is a check against yesterday's build on one machine, not a conformance suite.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "build", "mu2")
REFERENCE = os.path.join(ROOT, "tests", "reference")

# Every scene is pinned on all four axes that can move a picture: the seed, the tile, the
# frame count and the step. `--fixed-dt` makes the step a constant, so frame 90 is always the
# same 1.5 seconds of play in; `--no-fps` takes the frame rate out of the corner, which is the
# one thing on screen that is SUPPOSED to differ run to run; `--mute` keeps a review run
# quiet; and 640x360 is small enough that three references cost under a megabyte of git and
# large enough that a wrong material is visible in the diff.
#
# None of them writes a save: PlayMode only opens one when `--frames` is 0, and `--fresh` says
# so a second time.
COMMON = ["--fixed-dt", "16.667", "--frames", "91", "--shot", "90", "--no-fps", "--mute",
          "--width", "640", "--height", "360", "--seed", "1"]

SCENES = {
    "town": ["--world", "lorencia", "--play", "--fresh", "--at", "138,124"],
    "browser": ["--browse", "--pick", "Beer01", "--still"],
    "studio": ["--browse", "--studio", "--pick", "Bonfire01", "--still"],
}


def load(path):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def draw(name, into):
    """Runs one scene and returns the path of the last shot it took."""
    out = os.path.join(into, name)
    os.makedirs(out, exist_ok=True)
    command = [BINARY] + SCENES[name] + COMMON + ["--shot-path", out,
                                                  "--log", os.path.join(out, "mu2.log")]
    done = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if done.returncode != 0:
        print(f"  {name}: the run exited {done.returncode}; its log is {out}/mu2.log")
        return None
    shots = sorted(f for f in os.listdir(out) if f.endswith(".png"))
    if not shots:
        print(f"  {name}: the run took no screenshot at all")
        return None
    return os.path.join(out, shots[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenes", nargs="*", default=[], help="which scenes; default all")
    parser.add_argument("--bless", action="store_true",
                        help="write what was drawn as the new reference")
    args = parser.parse_args()

    if not os.path.exists(BINARY):
        print(f"shotcheck: no binary at {BINARY}; run ./build.sh")
        return 1
    names = args.scenes or list(SCENES)
    for name in names:
        if name not in SCENES:
            print(f"shotcheck: no scene called '{name}'; there are {', '.join(SCENES)}")
            return 1

    import numpy as np
    from PIL import Image

    os.makedirs(REFERENCE, exist_ok=True)
    kept = tempfile.mkdtemp(prefix="shotcheck-")
    failures = []
    for name in names:
        drawn = draw(name, kept)
        if drawn is None:
            failures.append(name)
            continue
        want = os.path.join(REFERENCE, f"{name}.png")
        if args.bless:
            # Re-encoded rather than copied: bgfx writes a PNG at low compression, and these
            # live in git.
            Image.open(drawn).convert("RGB").save(want, optimize=True)
            print(f"  {name}: blessed -> tests/reference/{name}.png "
                  f"({os.path.getsize(want) // 1024} KB)")
            continue
        if not os.path.exists(want):
            print(f"  {name}: NO REFERENCE. Look at {drawn}, then --bless it.")
            failures.append(name)
            continue
        got, expected = load(drawn), load(want)
        if got.shape != expected.shape:
            print(f"  {name}: FAILED -- drawn {got.shape} against reference {expected.shape}")
            failures.append(name)
            continue
        difference = np.abs(got - expected)
        moved = int((difference.max(axis=2) > 0).sum())
        if moved == 0:
            print(f"  {name}: identical")
            continue

        # It did not match. Draw it once more before believing that, because ONE of these
        # scenes is known to be rarely nondeterministic and a gate that cries wolf is a gate
        # people learn to ignore.
        #
        # The flake, measured 2026-09-22: `town` draws a dropped item on the ground that the
        # reference does not have, in roughly one run in fifteen -- ten consecutive runs came
        # out identical either side of the one that did not. The cause is not found and it is
        # in `docs/architecture.md`'s owed list; it is NOT the renderer's wall clock, which was
        # a different bug fixed the same day.
        #
        # A real regression is deterministic and fails both draws. A one-in-fifteen flake
        # survives a second draw about once in two hundred and twenty. So: two strikes.
        second = draw(name, kept)
        if second is not None and np.array_equal(load(second), expected):
            print(f"  {name}: identical on a second draw -- the first differed in {moved} "
                  f"pixels. Treated as the known flake, NOT as a pass to rely on; if you see "
                  f"this often, the flake has got worse and is worth chasing.")
            continue
        # A mask of what moved, beside the frame that moved it, because "1.2% of pixels
        # differ" does not say whether the shadows went or the sky changed shade.
        mask = os.path.join(os.path.dirname(drawn), f"{name}.diff.png")
        Image.fromarray(((difference.max(axis=2) > 0) * 255).astype("uint8")).save(mask)
        total = expected.shape[0] * expected.shape[1]
        print(f"  {name}: FAILED -- {moved} of {total} pixels ({100.0 * moved / total:.2f}%) "
              f"moved, worst channel {difference.max()}, mean {difference.mean():.3f}")
        print(f"      drawn {drawn}")
        print(f"      what moved {mask}")
        failures.append(name)

    if args.bless:
        shutil.rmtree(kept, ignore_errors=True)
        return 0
    if failures:
        print(f"shotcheck: {len(failures)} of {len(names)} scenes moved: {', '.join(failures)}")
        print("  If the change was meant to move them, look at both and then --bless.")
        return 1
    shutil.rmtree(kept, ignore_errors=True)
    print(f"shotcheck: {len(names)} scenes, every pixel of every one of them identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
