"""The showing cook's sounds: read any of MU's WAV formats, make it mono 16-bit at one rate.

Moved out of tools/cook.py on 2026-10-11 to pay for the showing cook's skip rules under the size
ratchet (docs/plans/order-plan.md sprint B). cook.py imports these; cook_showing's note says why the
format is what it is.
"""

import array
import sys
import wave


# What every cooked sound is resampled and downmixed to. See cook_showing for why each of
# the two numbers is what it is; the short version is that MONO IS NOT AN OPTIMISATION -- a
# stereo file cannot be panned, and sprint 6 places every emitter by the vector from the hero
# rotated by the camera's yaw.
SOUND_RATE = 22050
SOUND_BITS = 16


def read_wav(path):
    """One wav as mono 16-bit samples at its own rate. Returns (samples, rate).

    MU's sounds are not one format and this is the whole reason this function exists rather
    than a copy: measured over the 104 files, there are NINE -- 47 of them stereo 44.1 kHz
    16-bit, 18 mono 22.05 kHz 16-bit, 10 mono 44.1 kHz 8-BIT, and one lone file at 11127 Hz,
    which is not a rate anybody chooses on purpose. `wave` hands back raw frames and leaves
    all of that to the caller.

    8-bit wav is UNSIGNED with 128 for silence, and 16-bit is signed. Reading the first as
    the second is the classic way to turn a footstep into a scream of white noise.
    """
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        raw = handle.readframes(handle.getnframes())

    if width == 1:
        wide = array.array("h", (int(b - 128) * 256 for b in raw))
    elif width == 2:
        wide = array.array("h")
        wide.frombytes(raw)
        if sys.byteorder == "big":
            wide.byteswap()
    else:
        raise ValueError(f"{path}: {width * 8}-bit wav, and this reads 8 and 16")

    if channels > 1:
        # Averaged rather than taking the left, because several of these are a true stereo
        # pair and one side of a pair is half the sound. The sum cannot overflow the sample
        # type because it is done in Python's own integers and divided before it is stored.
        mixed = array.array("h", (sum(wide[i:i + channels]) // channels
                                  for i in range(0, len(wide) - channels + 1, channels)))
        wide = mixed
    return wide, rate


def resample(samples, rate, target):
    """Linear interpolation to `target` Hz. Returns the samples unchanged when it is a no-op.

    Linear and not a windowed filter on purpose: the material is 1990s game audio, half of
    it already 8-bit, and the aliasing a proper low-pass would save is below what this
    content carries. If a sound ever comes out harsh this is the line to revisit, and the
    cook says which files it touched so there is somewhere to look.
    """
    if rate == target or not samples:
        return samples
    count = max(1, int(len(samples) * target / rate))
    step = len(samples) / count
    out = array.array("h", bytes(2 * count))
    last = len(samples) - 1
    for i in range(count):
        where = i * step
        low = int(where)
        if low >= last:
            out[i] = samples[last]
            continue
        frac = where - low
        out[i] = int(samples[low] + (samples[low + 1] - samples[low]) * frac)
    return out


#: How long the tail of a one-shot is faded to silence. MU's pWalk(Soil).wav stops at a fifth
#: of its peak, mid-wave, and a sample that stops mid-wave is a click. The reverb hid it in
#: the open; the Lost Tower's dry steps did not (the user, 2026-10-03: 'when i step on LT the
#: sound is glitchy'). Four milliseconds is under anything heard as a fade. Ours.
#:
#: Thirty since 2026-10-05, a fifth of the sound at most: four was not enough for a tail cut at
#: three fifths of its peak -- the soil step still chopped (the user: 'when i step on lost tower
#: bricks sound feels buggy/shuttering'), and the melee hit, cut at a quarter, ticked after every
#: Evil Spirit strike ('i can see little ticks when evil spirit sound ended'). A thirty-millisecond
#: ramp on a tail is heard as the sound ending, not as a fade.
TAIL_FADE_SECONDS = 0.030


def fade_tail(samples, rate):
    """The last TAIL_FADE_SECONDS, or a fifth of a short sound, ramped to zero. Not for a loop,
    whose tail meets its head."""
    count = min(len(samples) // 5, int(rate * TAIL_FADE_SECONDS))
    count = max(1, min(len(samples), count))
    start = len(samples) - count
    for i in range(count):
        samples[start + i] = int(samples[start + i] * (count - 1 - i) / count)
    return samples


def write_wav(path, samples, rate):
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        if sys.byteorder == "big":
            samples = array.array("h", samples)
            samples.byteswap()
        handle.writeframes(samples.tobytes())
