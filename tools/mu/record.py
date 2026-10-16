"""mu record: a scripted scene of the game, recorded with its sound, as a 1920x1080 MP4.

    ./mu record <scene>                 tools/record/<scene>.cue -> out/recordings/<scene>.mp4
    ./mu record <scene> --fps 30        30 frames a second (60 by default)
    ./mu record <scene> --seconds 5     only the first 5 s, to try a cue file
    ./mu record <scene> --window        drawn in a window on the screen, through PNGs (slow)
    ./mu record                         the scenes there are

A scene is a cue file (src/app/modes/director.h has the verbs): its `args` lines are the game's
switches, `end N` is its length, and every other line plays the pointer and the keys at a second
of game time. The mouse is never read: the pointer in the picture is the script's.

Headless by default: the game runs --offscreen, with no window on the screen, drawing into a
frame of exactly 1920x1080 and piping every frame raw into ffmpeg (--raw-frames, a FIFO), so
nothing is compressed to PNG and nothing lands on disk but the MP4. It still needs this Mac's
GPU. Every frame advances the world by exactly 1/fps (--fixed-dt) and the sound is opened with
no device and pulled a frame's length at a time into a WAV (--tape), so picture and sound stay
frame-exact however fast or slowly it draws. The WAV is muxed in at the end.
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
WIDTH, HEIGHT = 1920, 1080
X264 = ["-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "film", "-pix_fmt", "yuv420p"]
# The game's mix peaks at full scale in a crowd; a limiter keeps the AAC from clipping.
AUDIO = ["-af", "alimiter=limit=0.9:level=disabled", "-c:a", "aac", "-b:a", "256k"]


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


def errors_in(log):
    try:
        with open(log) as handle:
            return [l.strip() for l in handle if "ERROR" in l][:10]
    except OSError:
        return []


def record(scene, fps, seconds, keep, say, window=False):
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
    wav = os.path.join(work, "sound.wav")
    log = os.path.join(work, "run.log")
    frames = int(round(length * fps))
    game = ["nice", os.path.join(BUILD, "mu2"),
            "--width", str(WIDTH), "--height", str(HEIGHT), "--windowed",
            "--fixed-dt", f"{1000.0 / fps:.6f}", "--frames", str(frames), "--shot", "1",
            "--tape", wav, "--script", os.path.abspath(cue), "--no-fps",
            # A scratch save, so a scene never touches the player's (and starts the same).
            "--save", os.path.join(work, "save.mus"), "--log", log]
    mp4 = os.path.join(OUT, f"{name}.mp4")
    started = time.time()

    if window:
        frames_dir = os.path.join(work, "frames")
        os.makedirs(frames_dir)
        game += ["--shot-path", frames_dir] + switches
        say(" ".join(shlex.quote(c) for c in game))
        say(f"{frames} frames at {fps} fps, {length:g} s, in a window -- leave it be")
        done = subprocess.run(game, cwd=ROOT)
        pngs = [f for f in os.listdir(frames_dir) if f.endswith(".png")]
        if done.returncode != 0 or not pngs:
            say(f"FAILED: the game left with {done.returncode}; the run is kept in {work}")
            return False, {"work": work, "errors": errors_in(log), "code": done.returncode}
        say(f"drawn {len(pngs)} frames in {time.time() - started:.0f} s; encoding")
        encode = ["nice", "ffmpeg", "-y", "-loglevel", "error",
                  "-framerate", str(fps), "-i", os.path.join(frames_dir, "%05d.png"), "-i", wav,
                  # Exactly 1920x1080 whatever the display's scale made the backbuffer.
                  "-vf", f"scale={WIDTH}:{HEIGHT}:flags=lanczos"] + X264 + AUDIO + \
                 ["-movflags", "+faststart", "-shortest", mp4]
        if subprocess.run(encode).returncode != 0:
            say(f"FAILED: ffmpeg; the frames are kept in {work}")
            return False, {"work": work}
        if not keep:
            shutil.rmtree(frames_dir, ignore_errors=True)
    else:
        # ffmpeg reads the FIFO the game writes: opened first, or the game's open would wait.
        fifo = os.path.join(work, "frames.fifo")
        os.mkfifo(fifo)
        video = os.path.join(work, "video.mp4")
        encoder = subprocess.Popen(
            ["nice", "ffmpeg", "-y", "-loglevel", "error",
             "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{WIDTH}x{HEIGHT}", "-r", str(fps),
             "-i", fifo] + X264 + [video])
        game += ["--offscreen", "--raw-frames", fifo] + switches
        say(" ".join(shlex.quote(c) for c in game))
        say(f"{frames} frames at {fps} fps, {length:g} s, offscreen")
        done = subprocess.run(game, cwd=ROOT)
        if done.returncode != 0:
            encoder.kill()
            say(f"FAILED: the game left with {done.returncode}; the run is kept in {work}")
            return False, {"work": work, "errors": errors_in(log), "code": done.returncode}
        if encoder.wait() != 0 or not os.path.isfile(video):
            say(f"FAILED: ffmpeg; the run is kept in {work}")
            return False, {"work": work}
        say(f"drawn and encoded {frames} frames in {time.time() - started:.0f} s; adding the sound")
        mux = ["nice", "ffmpeg", "-y", "-loglevel", "error", "-i", video, "-i", wav,
               "-c:v", "copy"] + AUDIO + ["-movflags", "+faststart", "-shortest", mp4]
        if subprocess.run(mux).returncode != 0:
            say(f"FAILED: ffmpeg's mux; the run is kept in {work}")
            return False, {"work": work}
        os.remove(fifo)
        if not keep:
            os.remove(video)

    errors = errors_in(log)
    for line in errors:
        say(f"  {line}")
    say(f"recorded {mp4}  ({os.path.getsize(mp4) / 1e6:.1f} MB, {time.time() - started:.0f} s;"
        f" the log and sound in {work})")
    return True, {"mp4": mp4, "work": work, "frames": frames, "errors": errors}
