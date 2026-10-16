"""mu record: a scripted scene of the game, recorded with its sound, as a 1920x1080 MP4.

    ./mu record <scene>                 tools/record/<scene>.cue -> out/recordings/<scene>.mp4
    ./mu record <scene> --fps 30        30 frames a second (60 by default)
    ./mu record <scene> --seconds 5     only the first 5 s, to try a cue file
    ./mu record                         the scenes there are

A scene is a cue file (src/app/modes/director.h has the verbs): its `args` lines are the game's
switches, `end N` is its length, and every other line plays the pointer and the keys at a second
of game time. The game is run with a window in front of you -- bgfx needs one -- but it ignores
your mouse: the pointer you see is the script's.

The run is not real time. Every frame advances the world by exactly 1/fps (--fixed-dt) and is
written as a PNG, and the sound is opened with no device and pulled a frame's length at a time
into a WAV (--tape), so picture and sound stay frame-exact however slowly it draws (about three
frames a second at 1080p). ffmpeg then joins them and the PNGs are thrown away.
"""

import os
import shlex
import shutil
import subprocess
import tempfile
import time

from things import BUILD, ROOT, NotFound

SCENES = os.path.join(ROOT, "tools", "record")
OUT = os.path.join(ROOT, "out", "recordings")


def scenes():
    if not os.path.isdir(SCENES):
        return []
    return sorted(f[:-4] for f in os.listdir(SCENES) if f.endswith(".cue"))


def read(path):
    switches, end = [], None
    with open(path) as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if line.startswith("args "):
                switches += shlex.split(line[5:])
            elif line.startswith("end "):
                end = float(line[4:])
    return switches, end


def record(scene, fps, seconds, keep, say):
    if not scene:
        for name in scenes():
            say(name)
        return True, {"scenes": scenes()}
    cue = scene if scene.endswith(".cue") else os.path.join(SCENES, scene + ".cue")
    if not os.path.isfile(cue):
        raise NotFound(f"no scene {scene}; there are: {', '.join(scenes()) or 'none'}")
    name = os.path.splitext(os.path.basename(cue))[0]
    switches, end = read(cue)
    length = seconds or end
    if not length:
        raise NotFound(f"{cue} has no `end SECONDS` line")
    if not shutil.which("ffmpeg"):
        raise NotFound("ffmpeg is not installed (brew install ffmpeg)")

    os.makedirs(OUT, exist_ok=True)
    work = tempfile.mkdtemp(prefix=f"{name}-", dir=OUT)
    frames_dir = os.path.join(work, "frames")
    os.makedirs(frames_dir)
    wav = os.path.join(work, "sound.wav")
    frames = int(round(length * fps))
    cmd = ["nice", os.path.join(BUILD, "mu2"),
           "--width", "1920", "--height", "1080", "--windowed",
           "--fixed-dt", f"{1000.0 / fps:.6f}", "--frames", str(frames),
           "--shot", "1", "--shot-path", frames_dir, "--tape", wav,
           "--script", os.path.abspath(cue), "--no-fps",
           # A scratch save, so a scene never touches the player's (and starts the same).
           "--save", os.path.join(work, "save.mus"),
           "--log", os.path.join(work, "run.log")] + switches
    say(" ".join(shlex.quote(c) for c in cmd))
    say(f"{frames} frames at {fps} fps, {length:g} s; the game's window will open -- leave it be")
    started = time.time()
    done = subprocess.run(cmd, cwd=ROOT)
    pngs = sorted(f for f in os.listdir(frames_dir) if f.endswith(".png"))
    say(f"drawn {len(pngs)} frames in {time.time() - started:.0f} s")
    errors = []
    try:
        with open(os.path.join(work, "run.log")) as handle:
            errors = [l.strip() for l in handle if "ERROR" in l][:10]
    except OSError:
        pass
    for line in errors:
        say(f"  {line}")
    if done.returncode != 0 or not pngs:
        say(f"FAILED: the game left with {done.returncode}; the run is kept in {work}")
        return False, {"work": work, "errors": errors, "code": done.returncode}

    mp4 = os.path.join(OUT, f"{name}.mp4")
    encode = ["nice", "ffmpeg", "-y", "-loglevel", "error", "-stats",
              "-framerate", str(fps), "-i", os.path.join(frames_dir, "%05d.png"),
              "-i", wav,
              # Exactly 1920x1080 whatever the display's scale made the backbuffer.
              "-vf", "scale=1920:1080:flags=lanczos,format=yuv420p",
              "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "film",
              # The game's mix peaks at full scale in a crowd; a limiter keeps the AAC from clipping.
              "-af", "alimiter=limit=0.9:level=disabled",
              "-c:a", "aac", "-b:a", "256k", "-movflags", "+faststart", "-shortest", mp4]
    say("encoding " + mp4)
    if subprocess.run(encode).returncode != 0:
        say(f"FAILED: ffmpeg; the frames are kept in {work}")
        return False, {"work": work}
    if not keep:
        shutil.rmtree(frames_dir, ignore_errors=True)
    say(f"recorded {mp4}  ({os.path.getsize(mp4) / 1e6:.1f} MB; the log and sound in {work})")
    return True, {"mp4": mp4, "work": work, "frames": len(pngs), "errors": errors}
