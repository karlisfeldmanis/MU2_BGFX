#!/usr/bin/env python3
"""Cuts Devias's blizzard bed into source/sounds/ablizzard.wav.

The user's file (downloads/nickpanek-whiteout-valley-blizzard-ambient-loop-with-howling-
hillside-winds-563822.mp3, given 2026-09-29) is 39 s of howling wind, 44.1 kHz stereo. It
becomes the `world_blizzard` loop the storm plays (game/world/weather.cpp), made as arain.wav
and ajungle.wav were: 22 kHz mono 16-bit, its last two seconds folded over its first two at
equal power so the loop has no seam, and the whole brought to a 0.95 peak.

    tools/blizzard.py           # cut and write
    tools/blizzard.py --dry     # only say what it would write
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "downloads" / (
    "nickpanek-whiteout-valley-blizzard-ambient-loop-with-howling-hillside-winds-563822.mp3")
OUT = ROOT / "source" / "sounds" / "ablizzard.wav"
RATE = 22050
FOLD = 2.0


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE),
                          "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()


def fold(x):
    """The last FOLD seconds laid over the first at equal power, and cut off the end: the
    sample after the loop's last is then the one the tail was fading into."""
    n = int(FOLD * RATE)
    t = np.linspace(0.0, 1.0, n, endpoint=False)
    head, tail = x[:n], x[-n:]
    out = x[:-n].copy()
    out[:n] = head * np.sin(t * np.pi / 2) + tail * np.cos(t * np.pi / 2)
    return out


def main():
    x = fold(decode(SOURCE))
    x *= 0.95 / float(np.abs(x).max())
    rms = 20 * np.log10(float(np.sqrt((x ** 2).mean())) + 1e-12)
    print(f"{OUT.name}: {len(x) / RATE:.1f} s, rms {rms:.1f} dB, peak 0.95")
    if "--dry" in sys.argv:
        return
    with wave.open(str(OUT), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((np.clip(x, -1.0, 1.0) * 32767.0).astype("<i2").tobytes())


if __name__ == "__main__":
    main()
