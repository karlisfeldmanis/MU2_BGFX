"""`mu jobs`: what `mu serve` is cooking, and `mu make` handed to it (docs/order-plan.md §9c, F1).

    ./mu jobs                   the last jobs: state, the step and its bar, time left
    ./mu jobs --watch [N]       live, until job N (or every job) is finished; Ctrl-C only stops watching
    ./mu jobs cancel N [--now]  after the step it is in, or kill that step now

When `mu serve` is running, `./mu make X` queues X there and watches it, printing what a make
run here prints; `--here` runs it in this process as before. Ctrl-C while watching leaves the
job running: a cook stopped between its textures and its meshes is the white-mesh trap.
"""

import json
import socket
import sys
import time

from serve import SOCKET, alive


def ask(request):
    with socket.socket(socket.AF_UNIX) as conn:
        conn.connect(SOCKET)
        conn.sendall((json.dumps(request) + "\n").encode())
        with conn.makefile("r") as stream:
            return json.loads(stream.readline())


def events(job=None, since=0):
    """Every event (of job, or of all) after `since`, as they come; ends when the server does."""
    with socket.socket(socket.AF_UNIX) as conn:
        conn.connect(SOCKET)
        conn.sendall((json.dumps({"op": "watch", "job": job, "since": since}) + "\n").encode())
        with conn.makefile("r") as stream:
            for line in stream:
                yield json.loads(line)


def clock(seconds):
    if seconds is None:
        return "?"
    seconds = int(seconds)
    return f"{seconds // 60}:{seconds % 60:02d}" if seconds >= 60 else f"{seconds} s"


def bar(fraction, width=20):
    filled = int(round(max(0.0, min(1.0, fraction)) * width))
    return "#" * filled + "-" * (width - filled)


def describe(view):
    """One line a job: '3 Sword01  running  build Sword01 2/5 [######----] sheet x.png  ~1:10 left'."""
    head = f"{view['job']:>3} {view['target']:<18} {view['state']:<9}"
    steps = view["steps"]
    if view["state"] == "running" and view["at"] is not None and steps:
        step = steps[view["at"]]
        p = view["progress"]
        inner = f" [{bar(p['n'] / p['of'])}] {p['label']}" if p and p["of"] else f" {view['line'][:50]}"
        return (f"{head} {view['at'] + 1}/{len(steps)} {step['name']}{inner}"
                f"  ~{clock(view['eta'])} left")
    if view["state"] in ("done", "failed", "cancelled") and view["started"]:
        took = clock((view["ended"] or time.time()) - view["started"])
        tail = f"  {view['error']}" if view["error"] else ""
        return f"{head} {len(steps)} steps in {took}{tail}"
    if view["state"] == "queued":
        return f"{head} waiting its turn; planned when it starts"
    return head


def listing(say):
    if not alive():
        say("mu serve is not running: ./mu serve")
        return False, {"error": "not running"}
    jobs = ask({"op": "jobs"})["jobs"]
    for view in jobs[-15:]:
        say(describe(view))
    if not jobs:
        say("no jobs yet")
    return True, {"jobs": jobs}


def watch(number, say, live=True):
    """Follows job `number` (or all) and prints its prose as make would; returns its last view."""
    if not alive():
        say("mu serve is not running: ./mu serve")
        return False, {"error": "not running"}
    tty = live and sys.stdout.isatty()
    shown, finished = False, None
    # One job is replayed from its start; all of them from now, not the kept history.
    since = 0 if number is not None else ask({"op": "jobs"})["seq"]
    try:
        for event in events(number, since):
            kind = event["kind"]
            if kind == "say":
                if shown:
                    sys.stdout.write("\r\033[K")
                    shown = False
                say(event["text"] if number is not None else f"[{event['job']}] {event['text']}")
            elif kind == "progress" and tty:
                sys.stdout.write(f"\r\033[K     [{bar(event['n'] / max(1, event['of']))}] "
                                 f"{event['n']}/{event['of']} {event['label'][:50]}")
                sys.stdout.flush()
                shown = True
            elif kind == "finished" and (number is None or event["job"] == number):
                if shown:
                    sys.stdout.write("\r\033[K")
                finished = event
                if number is not None:
                    break
    except KeyboardInterrupt:
        say(f"\nstopped watching; the job runs on (./mu jobs, ./mu jobs cancel {number})")
        return True, {"job": number, "watching": False}
    except (ConnectionError, OSError):
        say("mu serve went away")
        return False, {"error": "server gone"}
    if finished is None:
        return False, {"job": number, "error": "the server stopped before the job finished"}
    views = {v["job"]: v for v in ask({"op": "jobs"})["jobs"]} if alive() else {}
    view = views.get(finished["job"], {})
    return finished["ok"], {"job": finished["job"], "state": finished["state"],
                            "error": finished.get("error"), "steps": view.get("steps")}


def hand_off(target, cores, folder, reimport, say, quiet):
    """`mu make` through the server: queue it and watch it to the end."""
    reply = ask({"op": "make", "target": target, "cores": cores, "folder": folder, "import": reimport})
    say(f"queued as job {reply['job']} in mu serve (./mu jobs; --here runs it in this process)")
    return watch(reply["job"], say, live=not quiet)


def cancel(number, now, say):
    if not alive():
        say("mu serve is not running")
        return False, {"error": "not running"}
    reply = ask({"op": "cancel", "job": number, "now": now})
    if "error" in reply:
        say(reply["error"])
        return False, reply
    say(f"job {number}: {'killing its step' if now else 'stopping after its step'} ({reply['state']})")
    return True, reply
