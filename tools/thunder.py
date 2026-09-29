#!/usr/bin/env python3
"""Cuts the storm's claps into source/sounds/thunder1..N.wav.

The user's file (downloads/freesound_community-thunderstorm-14708.mp3, given 2026-09-29) is
two minutes of thunder with no rain under it: seven claps, each a crack and a rumble, with near
silence between. Each clap becomes one file of the `world_thunder` event, so the game can play
them one at a time over its own rain loop, at its own intervals, and the flash can be read off
the clap that is playing (game/world/weather.cpp).

A clap is found as a run of the file louder than -44 dB (a 200 ms mean of 20 ms windows) with
no gap of 1.5 s in it. It is cut from 0.2 s before that to 0.3 s after, faded in over 30 ms
and out over the last 1.5 s, and written 22 kHz mono 16-bit as arain.wav is. The whole file is
scaled once so its loudest sample is 0.95: the mp3 decodes to 1.19 and would clip, and one
scale for all seven keeps the near claps louder than the far ones, which is what the flash
reads as distance.

    tools/thunder.py            # cut and write
    tools/thunder.py --dry      # only say what it would cut
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "downloads" / "freesound_community-thunderstorm-14708.mp3"
OUT = ROOT / "source" / "sounds"
RATE = 22050
WINDOW = 0.02


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE),
                          "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()


def claps(x):
    w = int(RATE * WINDOW)
    n = len(x) // w
    rms = np.sqrt((x[:n * w].reshape(n, w) ** 2).mean(1))
    db = 20 * np.log10(rms + 1e-9)
    loud = np.convolve(db, np.ones(10) / 10, "same") > -44.0
    gap = int(1.5 / WINDOW)
    found, i = [], 0
    while i < n:
        if not loud[i]:
            i += 1
            continue
        j = i
        while j < n and loud[j:j + gap].any():
            j += 1
        if (j - i) * WINDOW > 2.0:  # a clap, not a click
            found.append((i * WINDOW, j * WINDOW, float(db[i:j].max())))
        i = j
    return found


def main():
    dry = "--dry" in sys.argv
    x = decode(SOURCE)
    x *= 0.95 / float(np.abs(x).max())
    for k, (start, end, peak) in enumerate(claps(x), 1):
        a = max(0, int((start - 0.2) * RATE))
        b = min(len(x), int((end + 0.3) * RATE))
        cut = x[a:b].copy()
        fade_in = int(0.03 * RATE)
        cut[:fade_in] *= np.linspace(0.0, 1.0, fade_in)
        fade_out = min(len(cut), int(1.5 * RATE))
        cut[-fade_out:] *= np.linspace(1.0, 0.0, fade_out) ** 2
        name = OUT / f"thunder{k}.wav"
        print(f"{name.name}: {start:6.2f}-{end:6.2f} s of the storm, {len(cut) / RATE:5.2f} s, "
              f"peak {peak:5.1f} dB")
        if dry:
            continue
        with wave.open(str(name), "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(RATE)
            out.writeframes((np.clip(cut, -1.0, 1.0) * 32767.0).astype("<i2").tobytes())


if __name__ == "__main__":
    main()
