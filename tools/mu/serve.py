"""`mu serve`: the cook's job runner, the Cooker's first slice (docs/order-plan.md §9c, F1).

One process holds the queue and runs make.py's plans one job at a time -- one heavy job, never
every core (settings.py) -- and tells whoever listens what it is doing. The studio, `./mu` and
the agent are all its clients. Nothing about a cook changes: the same plan, the same steps, the
same per-world locks, the same logs in workshop/logs/mu/.

    ./mu serve              run it here, in the foreground (Ctrl-C stops it after the job)
    ./mu serve --stop       ask a running one to stop when its job is done

It listens on a Unix socket, workshop/.mu/serve.sock. A client sends one JSON line and reads
JSON lines back:

    {"op": "make", "target": ["Sword01"], "cores": null}   -> {"job": 3}
    {"op": "jobs"}                                          -> {"jobs": [...]}
    {"op": "watch", "job": 3, "since": 0}                   -> every event since, then live
    {"op": "cancel", "job": 3, "now": false}                -> after the step, or kill it now
    {"op": "plan", "target": ["Sword01"]}                   -> {"steps": [{name, needed, reason}]}
    {"op": "recipe", "target": "Sword01"}                   -> {"files": [{file, doc}], "materials"}
    {"op": "edit", "file": f, "path": [...], "value": v}    -> {"edit": the journal's line}
        "how": "insert" puts value in at path, "remove" takes path out (value unused)
    {"op": "stop"}

An event is {"seq", "at", "job", "kind", ...}. The kinds: queued, plan (the steps that will run,
with their estimates), step, progress {n, of, label}, line {text}, say {text}, skip, warning,
done (a step's), stopped, finished {ok, error?}. They are kept in workshop/.mu/events.jsonl
too, so a studio opened later reads the last jobs back.
"""

import json
import os
import signal
import socket
import threading
import time
from collections import deque

import edit
import make
import settings
from things import WORKSHOP, NotFound

HOME = os.path.join(WORKSHOP, ".mu")
SOCKET = os.path.join(HOME, "serve.sock")
EVENTS = os.path.join(HOME, "events.jsonl")
LOGS = os.path.join(WORKSHOP, "logs", "mu")
KEEP = 20000


class Stop:
    """A job's brake: `after` ends it between steps, `now` kills the running step's group."""

    def __init__(self):
        self.after = self.now = False
        self.proc = None

    def kill(self):
        self.now = True
        proc = self.proc
        if proc is not None and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass


class Job:
    def __init__(self, number, target, cores, folder, reimport):
        self.number, self.target, self.cores = number, target, cores
        self.folder, self.reimport = folder, reimport
        self.state = "queued"          # queued, running, done, failed, cancelled
        self.steps = []                # [{name, estimate, state, seconds}] of the steps that run
        self.at = None                 # the index into steps running now
        self.progress = None           # {n, of, label} inside it
        self.line = ""
        self.queued, self.started, self.ended = time.time(), None, None
        self.error = None
        self.stop = Stop()
        self.plan_index = {}           # the plan's step index -> the index into steps

    def view(self):
        return {"job": self.number, "target": " ".join(self.target), "state": self.state,
                "steps": self.steps, "at": self.at, "progress": self.progress, "line": self.line,
                "queued": self.queued, "started": self.started, "ended": self.ended,
                "error": self.error, "eta": self.eta()}

    def eta(self):
        """Seconds left, from the timings of earlier runs; None when a step was never timed."""
        if self.state not in ("queued", "running"):
            return 0
        left = 0.0
        for i, step in enumerate(self.steps):
            if step["state"] in ("done", "skipped"):
                continue
            guess = step["estimate"]
            if guess is None:
                return None
            if i == self.at and step.get("began"):
                ran = time.time() - step["began"]
                p = self.progress
                if p and p["of"] and p["n"]:
                    guess = ran / (p["n"] / p["of"])   # the bar beats the history once it moves
                left += max(0.0, guess - ran)
            else:
                left += guess
        return round(left)


class Server:
    def __init__(self):
        self.jobs, self.queue = [], deque()
        self.events, self.seq = deque(maxlen=KEEP), 0
        self.lock = threading.Condition()
        self.closing = False
        self.journal = None

    # ------------------------------------------------------------------ events

    def emit(self, job, kind, **fields):
        with self.lock:
            self.seq += 1
            event = dict(seq=self.seq, at=round(time.time(), 2), job=job.number, kind=kind, **fields)
            self.events.append(event)
            self.track(job, event)
            self.journal.write(json.dumps(event) + "\n")
            self.journal.flush()
            self.lock.notify_all()

    def track(self, job, event):
        """The job's view follows its events, so `jobs` answers without replaying them."""
        kind = event["kind"]
        i = job.plan_index.get(event.get("step"))
        if kind == "step" and i is not None:
            job.at, job.progress, job.line = i, None, ""
            job.steps[i].update(state="running", began=event["at"])
        elif kind == "progress":
            job.progress = {"n": event["n"], "of": event["of"], "label": event["label"]}
        elif kind in ("line", "say"):
            job.line = event["text"]
        elif kind == "done" and i is not None:
            job.steps[i].update(state="done" if event["code"] == 0 else "failed",
                                seconds=event["seconds"])
            job.progress = None

    # ------------------------------------------------------------------ the worker

    def work(self):
        while True:
            with self.lock:
                while not self.queue and not self.closing:
                    self.lock.wait()
                if self.closing:
                    return
                job = self.queue.popleft()
                job.state, job.started = "running", time.time()
            self.cook(job)

    def cook(self, job):
        cores, _ = settings.cores(job.cores)
        say = lambda text: self.emit(job, "say", text=text)
        try:
            steps = make.plan(job.target, cores, say, job.folder, job.reimport)
            # A dry walk first: the steps that will run are the job's bar, known up front.
            # The tests only read dates, so asking them twice costs nothing and changes nothing.
            dry, changed = [], set()
            timings = make.read_timings()
            for index, step in enumerate(steps):
                needed, _reason = step.test(changed)
                if needed:
                    changed.add(step.makes)
                    job.plan_index[index] = len(dry)
                    dry.append({"name": step.name, "index": index, "state": "waiting",
                                "estimate": make.estimate(step.name, timings), "seconds": None})
            job.steps = dry
            self.emit(job, "plan", steps=dry)
            ok, results = make.run(steps, False, say, LOGS, settings.environment(cores),
                                   on=lambda kind, **f: self.emit(job, kind, **f), stop=job.stop)
            for result in results:
                if result.get("code") not in (None, 0):
                    job.error = (f"{result['step']} killed" if job.stop.now else
                                 f"{result['step']} failed with {result['code']}; see {result.get('log')}")
        except NotFound as missing:
            ok, job.error = False, str(missing)
            say(f"mu: {missing}")
        except Exception as crash:   # a job's crash is the job's, never the server's
            ok, job.error = False, f"{type(crash).__name__}: {crash}"
            say(f"mu: {job.error}")
        for step in job.steps:
            if step["state"] in ("waiting", "running"):
                step["state"] = "skipped" if ok else "not run"
        job.ended = time.time()
        stopped = job.stop.after or job.stop.now
        job.state = "done" if ok else ("cancelled" if stopped else "failed")
        self.emit(job, "finished", ok=ok, state=job.state, error=job.error)

    # ------------------------------------------------------------------ the socket

    def answer(self, conn):
        with conn, conn.makefile("rw") as stream:
            for line in stream:
                try:
                    ask = json.loads(line)
                except ValueError:
                    continue
                op = ask.get("op")
                if op == "watch":
                    self.watch(stream, ask.get("job"), ask.get("since", 0))
                    return
                # A plan reads dates and asks git, and an edit writes a file: answered here,
                # outside the lock, so neither waits on the queue.
                if op == "plan":
                    reply = self.plan(ask)
                elif op in ("recipe", "edit"):
                    reply = self.write(op, ask)
                else:
                    reply = self.handle(op, ask)
                stream.write(json.dumps(reply) + "\n")
                stream.flush()

    def handle(self, op, ask):
        with self.lock:
            if op == "make":
                job = Job(len(self.jobs) + 1, list(ask["target"]), ask.get("cores"),
                          ask.get("folder"), bool(ask.get("import")))
                self.jobs.append(job)
                self.queue.append(job)
                self.lock.notify_all()
            elif op == "jobs":
                return {"jobs": [j.view() for j in self.jobs[-50:]], "seq": self.seq}
            elif op == "cancel":
                job = self.find(ask.get("job"))
                if job is None:
                    return {"error": f"no job {ask.get('job')}"}
                if job.state == "queued":
                    self.queue.remove(job)
                    job.state, job.ended = "cancelled", time.time()
                elif job.state == "running":
                    job.stop.after = True
                    if ask.get("now"):
                        job.stop.kill()
                return {"job": job.number, "state": job.state}
            elif op == "stop":
                self.closing = True
                self.lock.notify_all()
                return {"stopping": True}
            else:
                return {"error": f"no op {op}"}
        self.emit(job, "queued", target=job.target)
        return {"job": job.number}

    def plan(self, ask):
        """What a make of the target would run: each step, whether it is needed, and why.
        The studio's stale marker; the same walk `mu make X --plan` prints."""
        try:
            steps = make.plan(list(ask["target"]), settings.cores()[0], lambda text: None)
        except NotFound as missing:
            return {"error": str(missing), "steps": []}
        except Exception as crash:
            return {"error": f"{type(crash).__name__}: {crash}", "steps": []}
        out, changed = [], set()
        for step in steps:
            needed, reason = step.test(changed)
            if needed:
                changed.add(step.makes)
            out.append({"name": step.name, "needed": needed, "reason": reason})
        return {"steps": out}

    def write(self, op, ask):
        """The inspector's two (tools/mu/edit.py): a recipe read, and one value of it set."""
        try:
            if op == "recipe":
                return edit.recipe(ask["target"])
            how, who = ask.get("how", "set"), ask.get("who", "studio")
            if how == "remove":
                return {"edit": edit.remove(ask["file"], ask["path"], who)}
            if how == "insert":
                return {"edit": edit.insert(ask["file"], ask["path"], ask["value"], who)}
            return {"edit": edit.edit(ask["file"], ask["path"], ask["value"], who)}
        except NotFound as refused:
            return {"error": str(refused)}
        except Exception as crash:
            return {"error": f"{type(crash).__name__}: {crash}"}

    def find(self, number):
        return next((j for j in self.jobs if j.number == number), None)

    def watch(self, stream, number, since):
        """Every kept event after `since` (of one job, or all), then live until the client goes."""
        sent = since
        while True:
            with self.lock:
                while not self.closing and (not self.events or self.events[-1]["seq"] <= sent):
                    self.lock.wait(1.0)
                    if self.closing:
                        break
                batch = [e for e in self.events if e["seq"] > sent
                         and (number is None or e["job"] == number)]
                if self.events:
                    sent = self.events[-1]["seq"]
                closing = self.closing
            try:
                for event in batch:
                    stream.write(json.dumps(event) + "\n")
                stream.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
            if closing:
                return


def alive():
    """Whether a server answers on the socket; a stale socket file is removed."""
    if not os.path.exists(SOCKET):
        return False
    try:
        with socket.socket(socket.AF_UNIX) as probe:
            probe.connect(SOCKET)
        return True
    except OSError:
        try:
            os.unlink(SOCKET)
        except OSError:
            pass
        return False


def serve(say):
    if alive():
        say(f"mu serve is already running on {os.path.relpath(SOCKET, os.path.dirname(WORKSHOP))}")
        return False, {"error": "already running"}
    os.makedirs(HOME, exist_ok=True)
    server = Server()
    if os.path.exists(EVENTS) and os.path.getsize(EVENTS) > 8_000_000:
        os.replace(EVENTS, EVENTS + ".previous")   # as mu2.log keeps one before it
    server.journal = open(EVENTS, "a")
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(SOCKET)
    listener.listen(16)
    listener.settimeout(0.5)
    worker = threading.Thread(target=server.work, daemon=True)
    worker.start()
    say(f"mu serve: listening on {SOCKET}; one job at a time, {settings.cores()[0]} cores each")
    # Its own code, as it was when it started: a server that outlives an edit to make.py or
    # edit.py answers with the old code, so it stops when idle and the studio starts a new one.
    code = os.path.dirname(os.path.abspath(__file__))
    stamp = lambda: max(os.path.getmtime(os.path.join(code, f)) for f in os.listdir(code) if f.endswith(".py"))
    started = stamp()
    try:
        while not server.closing:
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                with server.lock:
                    idle = not server.queue and all(j.state != "running" for j in server.jobs)
                if idle and stamp() > started:
                    say("mu serve: tools/mu changed since it started; stopping so a new one runs it")
                    with server.lock:
                        server.closing = True
                        server.lock.notify_all()
                continue
            threading.Thread(target=server.answer, args=(conn,), daemon=True).start()
    except KeyboardInterrupt:
        say("mu serve: stopping after the job that is running")
        with server.lock:
            server.closing = True
            server.lock.notify_all()
    finally:
        listener.close()
        os.unlink(SOCKET)
    worker.join()
    server.journal.close()
    say("mu serve: stopped")
    return True, {}
