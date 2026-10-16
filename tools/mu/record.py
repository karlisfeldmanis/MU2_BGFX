"""mu record: a scripted scene of the game, recorded with its sound, as a 1920x1080 MP4.

    ./mu record <scene>                 tools/record/<scene>.cue -> out/recordings/<scene>.mp4
    ./mu record <scene> --fps 30        30 frames a second (60 by default)
    ./mu record <scene> --seconds 5     only the first 5 s, to try a cue file
    ./mu record <scene> --window        drawn in a window on the screen, through PNGs (slow)
    ./mu record                         the scenes there are
    ./mu record <reel>                  tools/record/<reel>.reel.json: takes cut on the same stride

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

import json
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
    return sorted([f[:-4] for f in os.listdir(SCENES) if f.endswith(".cue")] +
                  [f[:-10] for f in os.listdir(SCENES) if f.endswith(".reel.json")])


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
    reel_file = os.path.join(SCENES, scene + ".reel.json")
    if scene.endswith(".reel.json") or os.path.isfile(reel_file):
        return reel(scene if scene.endswith(".reel.json") else reel_file, seconds, keep, say)
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


def take(cue_text, switches, frames, fps, work, say):
    """One offscreen run of a cue, its frames encoded losslessly: (video, wav, log, stride log)."""
    os.makedirs(work, exist_ok=True)
    cue = os.path.join(work, "take.cue")
    with open(cue, "w") as handle:
        handle.write(cue_text)
    wav, log = os.path.join(work, "sound.wav"), os.path.join(work, "run.log")
    strides, video = os.path.join(work, "strides.txt"), os.path.join(work, "video.mkv")
    fifo = os.path.join(work, "frames.fifo")
    if os.path.exists(fifo):
        os.remove(fifo)
    os.mkfifo(fifo)
    encoder = subprocess.Popen(
        ["nice", "ffmpeg", "-y", "-loglevel", "error",
         "-f", "rawvideo", "-pix_fmt", "bgra", "-s", f"{WIDTH}x{HEIGHT}", "-r", str(fps),
         "-i", fifo, "-c:v", "libx264", "-preset", "ultrafast", "-qp", "0",
         "-pix_fmt", "yuv444p", video])
    game = ["nice", os.path.join(BUILD, "mu2"),
            "--width", str(WIDTH), "--height", str(HEIGHT), "--windowed",
            "--fixed-dt", f"{1000.0 / fps:.6f}", "--frames", str(frames), "--shot", "1",
            "--offscreen", "--raw-frames", fifo, "--tape", wav, "--script", cue,
            "--stride-log", strides, "--no-fps",
            "--save", os.path.join(work, "save.mus"), "--log", log] + switches
    done = subprocess.run(game, cwd=ROOT)
    if done.returncode != 0:
        encoder.kill()
        raise NotFound(f"the take in {work} left with {done.returncode}: {errors_in(log)[:3]}")
    if encoder.wait() != 0:
        raise NotFound(f"ffmpeg failed on the take in {work}")
    os.remove(fifo)
    return video, wav, log, strides


def stride_at(strides, frame):
    with open(strides) as handle:
        for line in handle:
            words = line.split()
            if int(words[0]) == frame:
                return words[1:]
    raise NotFound(f"no stride logged for frame {frame} in {strides}")


def reel(path, seconds, keep, say):
    """Takes, one a world, cut hard on the same stride: each take's own stretch of
    [preroll, preroll + window) seconds, the next one's stride clocks pinned on its first frame to
    those the last one had on the frame after its own (Play::Stride)."""
    spec = json.load(open(path))
    name = os.path.basename(path)[:-len(".reel.json")]
    fps, preroll, window = spec["fps"], spec["preroll"], spec["window"]
    change, (dx, dy) = spec.get("change", 0.0), spec["direction"]
    stages = spec["stages"]
    takes = spec["takes"][:int(seconds // window) + 1] if seconds else spec["takes"]
    first, length = int(round(preroll * fps)), int(round(window * fps))
    os.makedirs(OUT, exist_ok=True)
    work = tempfile.mkdtemp(prefix=f"{name}-", dir=OUT)
    started = time.time()
    pin = None
    parts = []
    for i, one in enumerate(takes):
        gear = ",".join(stages[s] for s in one["wears"])
        switches = ["--world", one["world"], "--play", "--at", f"{one['at'][0]},{one['at'][1]}",
                    "--give", gear] + shlex.split(spec.get("args", ""))
        if one.get("weather"):
            switches += ["--weather", one["weather"]]
        at_change = preroll + change
        lines = ["0.0 move 0.55 0.6", f"0.1 run {dx} {dy}"]
        if pin is not None:
            lines.append(f"{first / fps:.6f} pin " + " ".join(pin))
        for slot in one.get("unwear", []):
            lines.append(f"{at_change:.3f} unwear {slot}")
        if one.get("becomes"):
            pieces = ",".join(stages[s] for s in one["becomes"]).split(",")
            lines.append(f"{at_change:.3f} wear " + " ".join(pieces))
            if one.get("flare"):
                lines.append(f"{at_change:.3f} flare")
        frames = first + length + 2
        say(f"take {i + 1}/{len(takes)}: {one['world']} at {one['at']}, {frames} frames")
        video, wav, log, strides = take("\n".join(lines) + "\n", switches, frames, fps,
                                        os.path.join(work, f"{i:02d}-{one['world']}"), say)
        pin = stride_at(strides, first + length)
        say(f"  carries clip {pin[0]}, stride {float(pin[1]):.3f} s"
            + (f", beat {float(pin[2]):.3f}" if float(pin[2]) >= 0 else "")
            + (f", gallop {float(pin[4]):.3f}" if float(pin[4]) >= 0 else ""))
        errors = errors_in(log)
        for line in errors[:3]:
            say(f"  {line}")
        parts.append((video, wav))

    # Each take's own stretch, frame-exact and sample-exact, joined; 5 ms fades keep the sound's
    # seams from clicking.
    inputs, chains = [], []
    for i, (video, wav) in enumerate(parts):
        inputs += ["-i", video, "-i", wav]
        start, end = first / fps, (first + length) / fps
        chains.append(f"[{2 * i}:v]trim=start_frame={first}:end_frame={first + length},"
                      f"setpts=PTS-STARTPTS[v{i}]")
        chains.append(f"[{2 * i + 1}:a]atrim=start={start:.6f}:end={end:.6f},asetpts=PTS-STARTPTS,"
                      f"afade=t=in:d=0.005,afade=t=out:st={window - 0.005:.3f}:d=0.005[a{i}]")
    joined = "".join(f"[v{i}][a{i}]" for i in range(len(parts)))
    chains.append(f"{joined}concat=n={len(parts)}:v=1:a=1[v][a0]")
    chains.append("[a0]alimiter=limit=0.9:level=disabled[a]")
    mp4 = os.path.join(OUT, f"{name}.mp4")
    encode = ["nice", "ffmpeg", "-y", "-loglevel", "error"] + inputs + \
             ["-filter_complex", ";".join(chains), "-map", "[v]", "-map", "[a]"] + X264 + \
             ["-c:a", "aac", "-b:a", "256k", "-movflags", "+faststart", "-r", str(fps), mp4]
    say("cutting the reel")
    if subprocess.run(encode).returncode != 0:
        say(f"FAILED: ffmpeg's cut; the takes are kept in {work}")
        return False, {"work": work}
    if not keep:
        for video, _ in parts:
            os.remove(video)
    say(f"recorded {mp4}  ({os.path.getsize(mp4) / 1e6:.1f} MB, {len(parts)} takes,"
        f" {time.time() - started:.0f} s; the takes' logs and sound in {work})")
    return True, {"mp4": mp4, "work": work, "takes": len(parts)}
