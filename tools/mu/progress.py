"""What a step says while it runs, and how long steps take: make.run's half for mu serve.

Out of make.py (the size ratchet, docs/order-plan.md §9c F6), unchanged.
"""

import json
import os
import re
import subprocess
import time

from things import ROOT, WORKSHOP

LOCKS = os.path.join(WORKSHOP, ".mu")

# ----------------------------------------------------------------------------- what a step says while it runs
#
# A step's output goes line by line to its log and, read on the way, to `on` (mu serve's
# events, docs/order-plan.md §9c). A line `@progress n/m label` is a bar inside the step:
# asset.sh prints one a sheet and one for the export, texcook one an image. Any other line is
# the step's latest word. Python steps run unbuffered, or their lines would arrive at the end.

PROGRESS = re.compile(r"^@progress (\d+)/(\d+)\s*(.*)$")
TIMINGS = os.path.join(LOCKS, "timings.json")


def kind_of(name):
    """'build Sword01' -> 'build', 'tables lorencia' -> 'tables': what an estimate falls back on."""
    return name.split(" ")[0]


def read_timings():
    try:
        with open(TIMINGS) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def estimate(name, timings=None):
    """Seconds this step took last time, else the median of its kind's, else None."""
    timings = read_timings() if timings is None else timings
    if name in timings.get("steps", {}):
        return timings["steps"][name]
    seen = sorted(timings.get("kinds", {}).get(kind_of(name), []))
    return seen[len(seen) // 2] if seen else None


def remember(name, seconds):
    timings = read_timings()
    timings.setdefault("steps", {})[name] = seconds
    kinds = timings.setdefault("kinds", {}).setdefault(kind_of(name), [])
    kinds.append(seconds)
    del kinds[:-20]
    os.makedirs(LOCKS, exist_ok=True)
    with open(TIMINGS + ".tmp", "w") as handle:
        json.dump(timings, handle, indent=1)
    os.replace(TIMINGS + ".tmp", TIMINGS)


def stream(cmd, log, env, on, step, stop):
    """Runs one command niced, its output to `log` and to `on`. Returns its exit code.

    `stop` is the job's: stop.now set kills the step's whole process group (Blender, texcook
    and all); stop.after alone lets it finish, which is the safe way to end a cook.
    """
    env = dict(env or os.environ, PYTHONUNBUFFERED="1")
    with open(log, "w") as out:
        proc = subprocess.Popen(["nice", "-n", "10"] + cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, cwd=ROOT, env=env,
                                start_new_session=True, bufsize=1, text=True, errors="replace")
        if stop is not None:
            stop.proc = proc
        said = 0.0
        for line in proc.stdout:
            out.write(line)
            line = line.rstrip()
            mark = PROGRESS.match(line)
            if mark:
                on("progress", step=step, n=int(mark.group(1)), of=int(mark.group(2)),
                   label=mark.group(3))
            elif line and time.time() - said > 0.2:
                # The latest word, ten a second at most: Blender alone prints thousands.
                said = time.time()
                on("line", step=step, text=line[:300])
        code = proc.wait()
        if stop is not None:
            stop.proc = None
        return code


def quiet(*_, **__):
    pass
