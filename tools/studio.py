#!/usr/bin/env python3
"""A weapon or an armour set in the studio: turned full circle at noon, dusk and night.

    tools/studio.py Kris                       a weapon, by its label or file name
    tools/studio.py Plate --category armour    an armour set on its body
    tools/studio.py Kris --turns 12            more angles

The studio is the viewer standing its subject in the world the game draws -- Lorencia's
town, ground and baked light, its lamps and fires, the reflection probe taken there -- beside
the map's own bonfire nearest the middle of the town. A weapon fills the frame, its bearer
hidden; the fire is 2.3 m off, in its light and its reflection. One run turns the camera
round the subject at each time of day and shoots every angle, and the frames are laid out as
one sheet, a row a time of day: shots/studio/<name>/sheet.png.

What it is for is what a still from one side cannot show: a flat blade that goes black when
it faces the grass, a round grip that whites out when it faces the sun. A fault that depends
on the angle is on this sheet somewhere.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMES = ("noon", "dusk", "night")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name")
    parser.add_argument("--category", default="weapons")
    parser.add_argument("--turns", type=int, default=8)
    parser.add_argument("--settle", type=int, default=20,
                        help="frames an angle is held before its shot")
    args = parser.parse_args()

    safe = "".join(c if c.isalnum() else "_" for c in args.name)
    out = os.path.join(ROOT, "shots", "studio", safe)
    frames_dir = os.path.join(out, "frames")
    os.makedirs(frames_dir, exist_ok=True)
    for old in os.listdir(frames_dir):
        os.remove(os.path.join(frames_dir, old))

    blocks = args.turns * len(TIMES)
    frames = blocks * args.settle + 1
    command = [os.path.join(ROOT, "build", "mu2"), "--studio", "--still", "--no-list",
               "--category", args.category, "--pick", args.name,
               "--turns", str(args.turns), "--shot", str(args.settle),
               "--frames", str(frames), "--shot-path", frames_dir,
               "--log", os.path.join(out, "studio.log")]
    if subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        print(f"studio: the viewer failed; see {os.path.relpath(out, ROOT)}/studio.log",
              file=sys.stderr)
        return 1

    from PIL import Image, ImageDraw
    tile_w, tile_h = 640, 480
    sheet = Image.new("RGB", (tile_w * args.turns, tile_h * len(TIMES)))
    missing = 0
    for block in range(blocks):
        # The shot that closes block b is taken on the frame block b + 1 begins.
        frame = (block + 1) * args.settle
        path = os.path.join(frames_dir, f"{frame:05d}.png")
        if not os.path.exists(path):
            missing += 1
            continue
        image = Image.open(path).convert("RGB")
        w, h = image.size
        # The middle of the frame, where the subject is framed, at 4:3.
        crop_h = int(h * 0.8)
        crop_w = crop_h * 4 // 3
        left, top = (w - crop_w) // 2, (h - crop_h) // 2
        tile = image.crop((left, top, left + crop_w, top + crop_h)).resize((tile_w, tile_h))
        turn, time = block % args.turns, block // args.turns
        ImageDraw.Draw(tile).text(
            (8, 8), f"{args.name}  {TIMES[time]}  {360 * turn // args.turns} deg",
            fill=(255, 255, 0))
        sheet.paste(tile, (turn * tile_w, time * tile_h))
    path = os.path.join(out, "sheet.png")
    sheet.save(path)
    print(f"studio: {args.name}, {args.turns} angles x {len(TIMES)} times -> "
          f"{os.path.relpath(path, ROOT)}" + (f" ({missing} shots missing)" if missing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
