"""WORLD resynthesis: pitch and formants moved apart, for a voice that is not a man's.

    ~/.cache/mu2-voice-alt/bin/python pipeline/world_shift.py in.wav out.wav --pitch 0.95 --formant 1.25

Made 2026-10-16 for the Seed Master, Aida's goblin (docs/game/aida-quest.md, "His voice"): asetrate
raises pitch and throat together, a chipmunk; WORLD (pyworld, MIT) moves them apart, so a goblin
can be small-throated and old. Needs pyworld, which lives in ~/.cache/mu2-voice-alt with
"setuptools<81" (pyworld imports pkg_resources).

--formant > 1 squeezes the spectral envelope up: a smaller throat and head, the goblin part.
--pitch scales f0 on its own. --rasp blends aperiodicity toward noise for a croak.
"""
import argparse, warnings
import numpy as np, soundfile as sf, pyworld as pw, librosa
warnings.filterwarnings("ignore")

ap = argparse.ArgumentParser()
ap.add_argument("src"); ap.add_argument("dst")
ap.add_argument("--pitch", type=float, default=1.0)
ap.add_argument("--formant", type=float, default=1.0)
ap.add_argument("--rasp", type=float, default=0.0)
ap.add_argument("--jitter", type=float, default=0.0, help="random f0 wobble, fraction")
a = ap.parse_args()

x, sr = sf.read(a.src)
if x.ndim > 1: x = x.mean(axis=1)
x = x.astype(np.float64)
f0, t = pw.harvest(x, sr, f0_floor=50, f0_ceil=500)
sp = pw.cheaptrick(x, f0, t, sr)
apd = pw.d4c(x, f0, t, sr)

bins = sp.shape[1]
src_idx = np.arange(bins) / a.formant  # envelope value at bin k comes from k/formant
sp2 = np.empty_like(sp)
for i in range(sp.shape[0]):
    sp2[i] = np.interp(src_idx, np.arange(bins), sp[i], right=sp[i, -1])
f02 = f0 * a.pitch
if a.jitter:
    rng = np.random.default_rng(3)
    wob = np.convolve(rng.normal(0, 1, len(f02)), np.ones(9) / 9, mode="same")
    f02 = f02 * (1 + a.jitter * wob)
apd2 = np.clip(apd + a.rasp * (1 - apd), 0, 1)
y = pw.synthesize(f02, sp2, apd2, sr)
y = y / (np.abs(y).max() + 1e-9) * 0.9
sf.write(a.dst, y, sr)
