"""`mu ship`: what HEAD holds that players do not have yet, what it takes to give it to them, and
the front page's Latest updates for what they were given.

    ./mu ship                  what is live where, each commit not yet shipped and what it needs,
                               and the commands to ship it, in their order
    ./mu ship --go             the same, then each command run in turn, asked before each one
    ./mu ship prepare          everything built and staged where it will run, at once where it can:
                               every box's server, each client's files, the launcher; players see
                               nothing (out/ship/prepared.json says what)
    ./mu ship switch [--now]   what prepare staged made live together: every server told to its
                               players and restarted 60 s on, the clients' manifests at that moment,
                               the launcher and the site; then each read back
    ./mu ship notes [--dry]    a draft on the site's /updates page for each change that is now live
                               and not yet told; an admin words them there and publishes them
    ./mu ship notes --import-site   the lines written into web/site/index.html by hand, put in as
                               published (once, the first time the site keeps them itself)
    ./mu ship translate [--dry]   whatever on the site has no translation in a language (a published
                               update line, a page's new words) translated by claude -p and put in
                               there; what an admin wrote is never touched (sitetranslate.py)
    ./mu ship translate --install   that, run by launchd every half hour from a copy outside
                               ~/Documents (tools/mu/translate.plist)

What is live is read where it lives, never remembered here:
  - each client: https://mu-classic.com/game/<platform>/manifest.json, its "version" and "protocol"
  - the launcher: .../game/launcher-<platform>/manifest.json
  - each region's mu2_server: /opt/mu2/server/DEPLOYED on its box, and whether the service was started
    after its binary was built (server/deploy.sh --stage builds one that waits for --switch)
  - the site: /opt/mu2-web/DEPLOYED on the Helsinki box

What a commit needs is read off the files it touches (WHAT_SHIPS): the server's half is what
server/deploy.sh builds into mu2_server; the client is everything `mu dist` builds; the launcher is
src/launcher/; the site is web/. docs/, tools/, tests/ and pipeline/ ship nothing by themselves. A
commit that moves net::kVersion is a protocol bump: the servers and every client go together,
staged first and switched back to back (release-checks-the-server-protocol).

**Downtime** (the user, 2026-10-21: "our deploy app, which has to be designed for minimal downtime"):
the slow half -- four one-core boxes building, three clients built and sent -- is `prepare`, which
runs while everyone plays, the boxes at the same time; the live half is `switch`, one command and
not a list of them, so a protocol bump's refused logins last the restart and a manifest's rename,
not minutes of steps. A server that knows SIGUSR2 tells its players for 60 s first; the clients
knock again by themselves and are back in seconds.
"""
import json
import os
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from paths import ROOT  # noqa: E402

import release  # noqa: E402

SITE = release.SITE
WEB_HOST = release.HOST          # the site and its database are on the Helsinki box
# Each region's game server: its name, its ssh host, and how server/deploy.sh is pointed at it.
BOXES = [
    ("North Europe", "hetzner", ""),
    ("Central Europe", "germany", "MU2_HOST=germany "),
    ("South America", "root@216.238.127.33", "MU2_HOST=root@216.238.127.33 "),
    ("East Asia", "root@45.76.195.120", "MU2_HOST=root@45.76.195.120 "),
]
CLIENTS = ("windows", "macos", "linux")
LAUNCHERS = ("windows", "macos", "linux")

# The paths (from MU2_BGFX) whose change each part ships. A file may be in more than one: src/sim
# is the server's rules and the client's mirror of them.
WHAT_SHIPS = {
    "server": ("src/net/", "src/core/", "src/content/", "src/sim/", "src/game/save.cpp", "src/game/save.h",
               "server/src/", "server/mu2-server.service", "content/raid/"),
    "client": ("src/", "shaders/", "content/", "CMakeLists.txt", "cmake/", "third_party/", "VERSION"),
    "launcher": ("src/launcher/",),
    "web": ("web/",),
}
# Shipped by neither: said when a commit touches only these, so its silence is not a surprise.
SHIPS_NOTHING = ("docs/", "tools/", "tests/", "pipeline/", "shots/", "dist/", "CLAUDE.md", "README.md", ".mcp.json")
# The client's own exceptions: the launcher's files the game does not build, and the server's.
CLIENT_SKIPS = ("server/",)


def git(*args, check=True):
    done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if check and done.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {done.stderr.strip()}")
    return done.stdout.strip() if done.returncode == 0 else None


def short(ref):
    return git("rev-parse", "--short", ref, check=False)


_LACKS = {}


def ancestor(a, b):
    """Whether commit a is in b's history (a == b counts). For b on HEAD's line, from one rev-list of
    what b lacks, kept: a report asks it of every commit against every part, and every commit it
    asks of is on HEAD's line."""
    if b not in _LACKS:
        on_line = ancestor_slow(b, "HEAD")
        _LACKS[b] = set(git("rev-list", f"{b}..HEAD").split()) if on_line else None
    if _LACKS[b] is None:
        return ancestor_slow(a, b)
    return not any(full.startswith(a) for full in _LACKS[b]) if len(a) < 40 else a not in _LACKS[b]


def ancestor_slow(a, b):
    return subprocess.run(["git", "merge-base", "--is-ancestor", a, b], cwd=ROOT).returncode == 0


_KNOWN = {}


def known(ref):
    """Whether this checkout has the commit; asked once a ref."""
    if ref not in _KNOWN:
        _KNOWN[ref] = bool(ref) and git("cat-file", "-e", ref + "^{commit}", check=False) is not None
    return _KNOWN[ref]


def ssh(host, command, timeout=20):
    done = subprocess.run(["ssh", *SSH_SHARED, "-o", f"ConnectTimeout={min(timeout, 10)}", host, command],
                          capture_output=True, text=True, timeout=timeout + 5)
    if done.returncode != 0:
        raise RuntimeError(f"ssh {host}: {done.stderr.strip() or 'exit ' + str(done.returncode)}")
    return done.stdout


# One connection a box, kept a minute: the app asks the same boxes again and again.
SSH_SHARED = ["-o", "BatchMode=yes", "-o", "ControlMaster=auto", "-o", "ControlPath=/tmp/mu-ship-%C",
              "-o", "ControlPersist=60"]


def fetch(url):
    done = subprocess.run(["curl", "-sf", "--max-time", "15", url], capture_output=True, text=True)
    return json.loads(done.stdout) if done.returncode == 0 and done.stdout else None


def parts_of(files):
    """The parts a commit's files ship: a set of WHAT_SHIPS' keys."""
    parts = set()
    for f in files:
        for part, prefixes in WHAT_SHIPS.items():
            if part == "client" and f.startswith(CLIENT_SKIPS):
                continue
            if f.startswith(prefixes) or f in prefixes:
                parts.add(part)
    return parts


def commits(since, until="HEAD"):
    """Each commit after `since` up to `until` that touches MU2_BGFX, oldest first: {sha, subject, day, files,
    parts, protocol}. `since` None takes every commit of the last two days."""
    span = f"{since}..{until}" if since else until
    extra = [] if since else ["--since=2.days"]
    out = git("log", "--reverse", "--format=%x00%h%x01%cs%x01%s", "--name-only", "--relative", *extra, span, "--", ".")
    found = []
    for block in out.split("\x00")[1:]:
        head, _, names = block.partition("\n")
        sha, day, subject = head.split("\x01", 2)
        files = [n for n in names.splitlines() if n.strip()]
        found.append({"sha": sha, "day": day, "subject": subject, "files": files, "parts": parts_of(files),
                      "protocol": "src/net/wire.h" in files and release.protocol_of(sha) != release.protocol_of(sha + "^")})
    return found


# ---- what is live --------------------------------------------------------------------------------

def live(say):
    """Every part's live commit, read from where it lives."""
    state = {"clients": {}, "launchers": {}, "boxes": [], "web": None, "armory": None, "errors": []}
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=6) as pool:
        clients = {p: pool.submit(fetch, f"{SITE}/game/{p}/manifest.json") for p in CLIENTS}
        launchers = {p: pool.submit(fetch, f"{SITE}/game/launcher-{p}/manifest.json") for p in LAUNCHERS}
    for p, m in ((p, f.result()) for p, f in clients.items()):
        state["clients"][p] = {"commit": m.get("version"), "protocol": m.get("protocol"), "built": m.get("built")} if m else None
    for p, m in ((p, f.result()) for p, f in launchers.items()):
        state["launchers"][p] = {"commit": m.get("version"), "built": m.get("built")} if m else None
    def read_box(region, host, env):
        box = {"region": region, "host": host, "env": env}
        try:
            # DEPLOYED is what was last copied; the service is running it only if it was started after
            # the binary was built (a --stage builds and leaves the old one playing). Its protocol is
            # the first line it logged since it started.
            said = ssh(host, "cat /opt/mu2/server/DEPLOYED; stat -c %Y /opt/mu2/build/mu2_server; "
                             "s=$(date -d \"$(systemctl show mu2-server -p ExecMainStartTimestamp --value)\" +%s); echo $s; "
                             "systemctl is-active mu2-server; "
                             "journalctl -u mu2-server -o cat --no-pager --since @$s | grep -m1 -o 'mu2_server: protocol [0-9]*'; "
                             "journalctl -u mu2-server -o cat --no-pager --since @$s | grep -q 'SIGUSR2 restarts' && echo WARNS; true")
            lines = said.splitlines() + [""] * 5
            box["deployed"] = lines[0].strip()
            built, started = int(lines[1] or 0), int(lines[2] or 0)
            box["staged"] = built > started + 5
            box["active"] = lines[3].strip() == "active"
            found = re.search(r"protocol (\d+)", lines[4])
            box["protocol"] = int(found.group(1)) if found else None
            # Whether the running server tells its players before a restart (server/deploy.sh --switch).
            box["warns"] = "WARNS" in said
        except (RuntimeError, ValueError, subprocess.TimeoutExpired) as e:
            box["error"] = str(e)[:200]
        return box

    def read_web():
        try:
            # And the Armory's release: what /srv/mu2-web/armory/current points at (web/deploy.sh).
            said = ssh(WEB_HOST, "cat /opt/mu2-web/DEPLOYED; readlink /srv/mu2-web/armory/current || true").splitlines()
            state["armory"] = said[1].strip() if len(said) > 1 and said[1].strip() else None
            return said[0].strip() if said else None
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            state["errors"].append(f"site: {e}")
            return None

    # The boxes are far apart and each answers in a second or two: asked at once.
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=len(BOXES) + 1) as pool:
        boxes = [pool.submit(read_box, *b) for b in BOXES]
        web = pool.submit(read_web)
        state["boxes"] = [b.result() for b in boxes]
        state["web"] = web.result()
    state["errors"] += [f"{b['region']}: {b['error']}" for b in state["boxes"] if "error" in b]
    return state


def oldest(shas):
    """The commit every one of `shas` holds: the oldest, when they are on one line."""
    shas = [s for s in shas if known(s)]
    if not shas:
        return None
    base = shas[0]
    for s in shas[1:]:
        base = git("merge-base", base, s)
    return short(base)


# ---- the report ----------------------------------------------------------------------------------

def pending(since, part, until="HEAD"):
    """The commits after `since` up to `until` that ship `part`; None when `since` is not in HEAD's
    history."""
    if not known(since) or not ancestor(since, "HEAD"):
        return None
    return [c for c in commits(since, until) if part in c["parts"]]


def report(say, until="HEAD"):
    head = short(until)
    head_protocol = release.protocol_of(head)
    state = live(say)
    for e in state["errors"]:
        say(f"! could not read {e}")
    plan = {"head": head, "protocol": head_protocol, "parts": {}, "steps": [], "rows": [], "unshipped": [],
            "errors": state["errors"], "subject": git("log", "-1", "--format=%s", until)}

    def row(kind, name, live_commit, todo, **more):
        plan["rows"].append({"kind": kind, "name": name, "live": live_commit,
                             "behind": None if todo is None else len(todo), **more})

    say(f"HEAD {head}, protocol {head_protocol}\n")

    # The clients.
    client_todo = []
    for p, c in state["clients"].items():
        if not c:
            say(f"client {p:8} unreadable")
            continue
        todo = pending(c["commit"], "client", until)
        mark = "?" if todo is None else (f"{len(todo)} behind" if todo else "current")
        say(f"client   {p:8} {c['commit']}  protocol {c['protocol']}  {c['built']}  {mark}")
        row("client", p, c["commit"], todo, protocol=c["protocol"], built=c["built"])
        if todo:
            client_todo.append(p)
        plan["parts"][f"client-{p}"] = {"live": c["commit"], "behind": [x["sha"] for x in todo or []]}
    # The launcher.
    launcher_todo = []
    for p, c in state["launchers"].items():
        if not c:
            continue
        todo = pending(c["commit"], "launcher", until)
        say(f"launcher {p:8} {c['commit']}  {c['built']}  "
            f"{'?' if todo is None else (str(len(todo)) + ' behind' if todo else 'current')}")
        row("launcher", p, c["commit"], todo, built=c["built"])
        if todo:
            launcher_todo.append(p)
        plan["parts"][f"launcher-{p}"] = {"live": c["commit"], "behind": [x["sha"] for x in todo or []]}
    # The servers.
    server_todo = []
    for box in state["boxes"]:
        if "error" in box:
            say(f"server   {box['region']:15} unreadable")
            continue
        todo = pending(box["deployed"], "server", until)
        how = ("STAGED, not switched" if box["staged"] else "running") if box["active"] else "STOPPED"
        say(f"server   {box['region']:15} {box['deployed']}  protocol {box['protocol']}  {how}  "
            f"{'?' if todo is None else (str(len(todo)) + ' behind' if todo else 'current')}")
        row("server", box["region"], box["deployed"], todo, protocol=box["protocol"], how=how,
            warns=bool(box.get("warns")), staged=box["staged"])
        if todo or box["staged"] or not box["active"]:
            server_todo.append(box)
        plan["parts"][f"server-{box['host']}"] = {"live": box["deployed"], "staged": box["staged"],
                                                  "behind": [x["sha"] for x in todo or []]}
    # The site.
    web_todo = pending(state["web"], "web", until) if state["web"] else None
    if state["web"]:
        row("site", "mu-classic.com", state["web"], web_todo)
        say(f"site     {'':8} {state['web']}  {'?' if web_todo is None else (str(len(web_todo)) + ' behind' if web_todo else 'current')}")

    # The Armory: the release players will have once this ships -- this commit when its macOS client
    # goes, else the live one -- built and tested, and `current` on the box. A release that changes
    # only the game still uploads its Armory, its own lane (web/deploy.sh --armory, nothing restarted).
    live_mac = (state["clients"].get("macos") or {}).get("commit")
    armory_target = head if "macos" in client_todo else live_mac
    armory_todo = bool(armory_target) and state["armory"] != armory_target
    if armory_target:
        tested = os.path.isfile(os.path.join(ROOT, "out", "armory", armory_target, "TESTED"))
        row("armory", "mu-classic.com/armory", state["armory"], [armory_target] if armory_todo else [],
            target=armory_target, tested=tested)
        say(f"armory   {'':8} {state['armory'] or 'none'}  "
            + (f"-> {armory_target} ({'built and tested' if tested else 'to build and test'})" if armory_todo else "current"))

    # Every commit not yet everywhere, with what it needs.
    floor = oldest([c["commit"] for c in state["clients"].values() if c]
                   + [b["deployed"] for b in state["boxes"] if b.get("deployed")]
                   + ([state["web"]] if state["web"] else [])
                   + [c["commit"] for c in state["launchers"].values() if c])
    unshipped = commits(floor, until) if floor and ancestor(floor, "HEAD") else []
    if unshipped:
        say(f"\nnot yet everywhere ({len(unshipped)} since {floor}):")
        for c in unshipped:
            where = []
            if "server" in c["parts"] and any(b.get("deployed") and known(b["deployed"]) and not ancestor(c["sha"], b["deployed"])
                                              for b in state["boxes"]):
                where.append("server")
            if "client" in c["parts"] and any(v and known(v["commit"]) and not ancestor(c["sha"], v["commit"])
                                              for v in state["clients"].values()):
                where.append("client")
            if "launcher" in c["parts"] and any(v and known(v["commit"]) and not ancestor(c["sha"], v["commit"])
                                                for v in state["launchers"].values()):
                where.append("launcher")
            if "web" in c["parts"] and state["web"] and known(state["web"]) and not ancestor(c["sha"], state["web"]):
                where.append("site")
            if not where:
                continue
            plan["unshipped"].append({"sha": c["sha"], "day": c["day"], "text": draft_text(c["subject"]),
                                      "subject": c["subject"], "where": where, "protocol": c["protocol"]})
            tag = "PROTOCOL " if c["protocol"] else ""
            say(f"  {c['sha']} {tag}[{', '.join(where)}] {draft_text(c['subject'])[:100]}")

    # Uncommitted work never ships: dist and both deploy scripts take the commit.
    dirty = git("status", "--porcelain", "--", "src", "shaders", "content", "server", "web", "CMakeLists.txt")
    plan["dirty"] = [line.split(maxsplit=1)[-1] for line in (dirty or "").splitlines()]
    if dirty:
        say(f"\nnote: {len(dirty.splitlines())} uncommitted files in shipped paths (another session's?); "
            f"only {head} ships, not them")

    # The verdict and the steps.
    bump = any(c and c["protocol"] != head_protocol for c in state["clients"].values()) or \
        any(b.get("protocol") not in (None, head_protocol) for b in state["boxes"])
    # What is to ship, for prepare and switch.
    plan["todo"] = {"boxes": [b["host"] for b in server_todo], "clients": client_todo,
                    "launcher": bool(launcher_todo), "web": bool(web_todo),
                    "armory": armory_target if armory_todo else None}
    verdicts = []
    if bump:
        verdicts.append(f"PROTOCOL {min([c['protocol'] for c in state['clients'].values() if c] or [0])} -> {head_protocol}: "
                        f"every server and every client switch together; logins are refused from the servers' "
                        f"restart until the manifests turn, seconds")
    elif server_todo:
        verdicts.append(f"server deploy on {', '.join(b['region'] for b in server_todo)} "
                        f"(told 60 s first where the server knows how; the clients rejoin by themselves)")
    if client_todo and not bump:
        verdicts.append(f"client patch for {', '.join(client_todo)} (the launcher fetches it; no restart)")
    if launcher_todo:
        verdicts.append("launcher update (every launcher replaces itself)")
    if web_todo:
        verdicts.append("site deploy")
    if armory_todo:
        verdicts.append(f"the Armory of {armory_target} (its data and `current`; nothing restarted)")
    verdict = "; ".join(verdicts) if verdicts else "nothing to ship: every part is current"
    # A prepare of this commit with nothing failed: only the switch is left.
    try:
        with open(PREPARED) as f:
            ready = json.load(f)
        plan["prepared"] = ready if ready.get("commit") == head and not ready.get("failed") else None
    except (OSError, ValueError):
        plan["prepared"] = None
    steps = []
    if verdicts and plan["prepared"]:
        say(f"\nprepared {plan['prepared']['at']}: only the switch is left")
    elif verdicts:
        steps.append((f"./mu ship prepare --commit {head}", "build and stage everywhere; players see nothing"))
    if verdicts:
        steps.append((f"./mu ship switch --commit {head}", "LIVE: servers told and restarted, clients released, together"))
    if steps:
        steps.append(("./mu ship notes", "drafts on /updates"))
    say(f"\n=> {verdict}")
    if steps:
        say("\nsteps:")
        for n, (cmd, what) in enumerate(steps, 1):
            say(f"  {n:2}. {cmd:46}  # {what}")
    plan["verdict"] = verdict
    plan["steps"] = [s[0] for s in steps]
    plan["step_notes"] = [s[1] for s in steps]
    plan["bump"] = bump
    return plan, steps


def go(steps, say):
    """Each step run in turn, asked first; any failure stops the rest."""
    for n, (cmd, what) in enumerate(steps, 1):
        try:
            answer = input(f"\n[{n}/{len(steps)}] {cmd}   # {what}\nrun it? [y]es / [s]kip / [q]uit: ").strip().lower()
        except EOFError:
            answer = "q"
        if answer.startswith("q"):
            say("stopped")
            return False
        if not answer.startswith("y"):
            continue
        done = subprocess.run(["zsh", "-c", cmd], cwd=ROOT)
        if done.returncode != 0:
            say(f"step {n} failed (exit {done.returncode}); the rest were not run")
            return False
    return True


def ship(say, run=False):
    plan, steps = report(say)
    if run and steps:
        plan["ran"] = go(steps, say)
    return True, plan


# ---- prepare and switch -------------------------------------------------------------------------

PREPARED = os.path.join(ROOT, "out", "ship", "prepared.json")
# What a server that knows SIGUSR2 waits before it restarts (server/src/host.h kRestartWarn), and
# the breath after: systemd's RestartSec and the new process opening its worlds. A protocol bump's
# manifests turn then, not before, so nobody is told to update while his server still speaks the old.
RESTART_WARN = 60
RESTART_UP = 5


class Lanes:
    """Commands run side by side, each line of their output said with its lane's name as it comes."""

    def __init__(self, say):
        self.say = say
        self.lock = threading.Lock()
        self.results = {}

    def tell(self, label, text):
        with self.lock:
            self.say(f"[{label}] {text}")

    def run(self, label, commands):
        """Each command under zsh in turn; the first that fails ends the lane. Its result kept."""
        started = time.time()
        for cmd in commands:
            self.tell(label, f"$ {cmd}")
            proc = subprocess.Popen(["zsh", "-c", cmd], cwd=ROOT, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, bufsize=1)
            for line in proc.stdout:
                self.tell(label, line.rstrip())
            if proc.wait() != 0:
                self.tell(label, f"FAILED (exit {proc.returncode}): {cmd}")
                self.results[label] = False
                return False
        self.tell(label, f"done in {time.time() - started:.0f} s")
        self.results[label] = True
        return True


def prepare(say, commit=None):
    """Everything to ship built and put where it will run, while everyone plays: each box's server
    staged at once (they are far apart and build on their own core), the clients built here one at a
    time (one heavy job on this Mac) and each sent as soon as it is built. What is ready is written
    to out/ship/prepared.json for switch."""
    wanted = commit or "HEAD"
    commit = short(wanted)
    if not commit:
        say(f"no commit {wanted}")
        return False, {"error": "no such commit"}
    plan, _ = report(lambda _: None, until=commit)
    todo, protocol = plan["todo"], plan["protocol"]
    boxes = {b[1]: b for b in BOXES}
    say(f"preparing {commit} (protocol {protocol}): {plan['verdict']}")
    if not (todo["boxes"] or todo["clients"] or todo["launcher"] or todo["web"] or todo.get("armory")):
        return True, {"commit": commit, "nothing": True}
    lanes = Lanes(say)
    staged_already = {h for h, part in ((k[len("server-"):], v) for k, v in plan["parts"].items() if k.startswith("server-"))
                      if part["staged"] and part["live"] == commit}

    def here():
        """The clients and the launcher: built in turn, each sent while the next builds."""
        with ThreadPoolExecutor(max_workers=4) as sends:
            sent = []
            for p in todo["clients"]:
                if lanes.run(p, [f"./mu dist {p} --commit {commit}", f"./mu release {p} --dry"]):
                    sent.append(sends.submit(lanes.run, p, [f"./mu release {p} --stage"]))
            if todo["launcher"]:
                lanes.run("launcher", [f"./mu dist launcher --commit {commit}", "./mu release launcher --dry"])
            # The Armory's data for the release players will have (tools/armory/build.py), from its
            # packs (dist/macos's, kept in out/armory/packs/, so after its dist): out/armory/<commit>/,
            # built and tested unless it already is, nothing sent. Its result is said, never a reason
            # the switch waits.
            target = todo.get("armory")
            if target and ("macos" not in todo["clients"] or lanes.results.get("macos")):
                if os.path.isfile(os.path.join(ROOT, "out", "armory", target, "TESTED")):
                    lanes.tell("armory", f"out/armory/{target}/ built and tested already")
                    lanes.results["armory"] = True
                else:
                    lanes.run("armory", [f"./mu armory build --commit {target}", f"./mu armory test out/armory/{target}"])
            for f in sent:
                f.result()

    with ThreadPoolExecutor(max_workers=len(BOXES) + 1) as pool:
        jobs = [pool.submit(here)]
        for host in todo["boxes"]:
            region, _, env = boxes[host]
            if host in staged_already:
                lanes.tell(region, f"already staged at {commit}")
                lanes.results[region] = True
                continue
            jobs.append(pool.submit(lanes.run, region, [f"MU2_COMMIT={commit} {env}server/deploy.sh --stage"]))
        for j in jobs:
            j.result()

    failed = [k for k, ok in lanes.results.items() if not ok and k != "armory"]
    ready = {"commit": commit, "protocol": protocol, "bump": plan["bump"], "at": time.strftime("%Y-%m-%d %H:%M:%S"),
             "boxes": [h for h in todo["boxes"] if lanes.results.get(boxes[h][0])],
             "clients": [p for p in todo["clients"] if lanes.results.get(p)],
             "launcher": bool(lanes.results.get("launcher")), "web": todo["web"], "failed": failed,
             "armory": todo.get("armory") if lanes.results.get("armory") else None}
    os.makedirs(os.path.dirname(PREPARED), exist_ok=True)
    with open(PREPARED, "w") as out:
        json.dump(ready, out, indent=1)
    say("")
    for label, ok in lanes.results.items():
        say(f"  {'ready ' if ok else 'FAILED'}  {label}")
    if failed:
        say(f"\nnot ready: {', '.join(failed)}; switch waits until a prepare has them all")
        return False, ready
    say(f"\nready: `./mu ship switch --commit {commit}` makes it live")
    return True, ready


def switch(say, commit=None, now=False):
    """What prepare staged, made live at one moment: every box's --switch at once (its players told
    for RESTART_WARN first, where its server knows how), the clients' manifests turned as the servers
    come back, the launcher and the site; then each read back from where it lives."""
    try:
        with open(PREPARED) as f:
            ready = json.load(f)
    except (OSError, ValueError):
        say("nothing prepared: `./mu ship prepare` first")
        return False, {"error": "nothing prepared"}
    commit = short(commit) if commit else ready["commit"]
    if ready["commit"] != commit:
        say(f"prepared is {ready['commit']}, not {commit}: prepare {commit} first")
        return False, {"error": "another commit prepared"}
    if ready.get("failed"):
        say(f"prepare left {', '.join(ready['failed'])} not ready: prepare again first")
        return False, {"error": "not all prepared"}

    # The boxes as they are now: each still holding what prepare built and not started on it.
    state = live(say)
    boxes = {b["host"]: b for b in state["boxes"]}
    switching = []
    for host in ready["boxes"]:
        b = boxes.get(host, {})
        if "error" in b:
            say(f"{b.get('region', host)}: unreadable ({b['error']}); nothing switched")
            return False, {"error": f"{host} unreadable"}
        if b["deployed"] != commit:
            say(f"{b['region']}: holds {b['deployed']} now, not {commit} (another deploy?); prepare again")
            return False, {"error": f"{host} holds {b['deployed']}"}
        if not b["staged"] and b["active"]:
            say(f"{b['region']}: already running {commit}")
            continue
        switching.append(b)
    warns = [b for b in switching if b.get("warns")] if not now else []
    # A bump's manifests turn when the told servers come back; anything else needs no waiting.
    wait = RESTART_WARN + RESTART_UP if ready["bump"] and warns else 0
    say(f"switching {commit}: {len(switching)} servers"
        + (f" ({', '.join(b['region'] for b in warns)} told {RESTART_WARN} s first)" if warns else "")
        + f", clients {', '.join(ready['clients']) or 'none'}"
        + (f" in {wait} s, as the servers come back" if wait and ready["clients"] else "")
        + (", the launcher" if ready["launcher"] else "") + (", the site" if ready["web"] else "")
        + (f", the Armory of {ready['armory']}" if ready.get("armory") else ""))

    lanes = Lanes(say)
    started = time.time()

    def client(p):
        left = wait - (time.time() - started)
        if left > 0:
            lanes.tell(p, f"the manifest turns in {left:.0f} s")
            time.sleep(left)
        return lanes.run(p, [f"./mu release {p}"])

    with ThreadPoolExecutor(max_workers=len(switching) + len(ready["clients"]) + 2) as pool:
        jobs = [pool.submit(lanes.run, b["region"],
                            [f"MU2_COMMIT={commit} {'MU2_NOW=1 ' if now else ''}{b['env']}server/deploy.sh --switch"])
                for b in switching]
        jobs += [pool.submit(client, p) for p in ready["clients"]]
        if ready["launcher"]:
            jobs.append(pool.submit(lanes.run, "launcher", ["./mu release launcher"]))
        armory = ready.get("armory")
        if ready["web"]:
            jobs.append(pool.submit(lanes.run, "site", [f"MU2_COMMIT={commit} {f'ARMORY={armory} ' if armory else ''}web/deploy.sh"]))
        elif armory:
            jobs.append(pool.submit(lanes.run, "armory", [f"ARMORY={armory} web/deploy.sh --armory"]))
        for j in jobs:
            j.result()

    # Read back where it lives.
    say("")
    after = live(lambda _: None)
    good = all(lanes.results.values())
    for b in after["boxes"]:
        if b["host"] not in ready["boxes"]:
            continue
        ok = "error" not in b and b["deployed"] == commit and b["active"] and not b["staged"] and \
            b["protocol"] == ready["protocol"]
        good &= ok
        say(f"  {'live  ' if ok else 'WRONG '}  server {b['region']:15} "
            + (b.get("error", "") or f"{b['deployed']} protocol {b['protocol']} {'running' if b['active'] else 'STOPPED'}"
                                     + (' STAGED' if b['staged'] else '')))
    for p in ready["clients"]:
        c = after["clients"].get(p)
        ok = bool(c) and c["commit"] == commit
        good &= ok
        say(f"  {'live  ' if ok else 'WRONG '}  client {p:15} {c['commit'] if c else 'unreadable'}")
    if ready["web"]:
        ok = after["web"] == commit
        good &= ok
        say(f"  {'live  ' if ok else 'WRONG '}  site   {after['web']}")
    if ready.get("armory"):
        ok = after.get("armory") == ready["armory"]
        good &= ok
        say(f"  {'live  ' if ok else 'WRONG '}  armory {after.get('armory')}")
    say(f"\n{'live' if good else 'NOT all live'}: {commit} in {time.time() - started:.0f} s")
    # The release order the Armory's history follows (tools/armory/releases.py): this one, once its
    # macOS client is what players have.
    if "macos" in ready["clients"] and (after["clients"].get("macos") or {}).get("commit") == commit:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "armory", "releases.py"), "record", commit], cwd=ROOT)
    if good:
        os.remove(PREPARED)
    return good, {"commit": commit, "results": lanes.results, "live": good}


# ---- the Latest updates --------------------------------------------------------------------------

class Site:
    """The site's /internal/updates: on the box over ssh, or a local web/server.py (--web URL)."""

    def __init__(self, web=None):
        self.web = web

    def get(self, path="/internal/updates"):
        if self.web:
            return fetch(f"{self.web}{path}")
        return json.loads(ssh(WEB_HOST, f"curl -sf http://127.0.0.1:8080{path}", timeout=60))

    def post(self, body, path="/internal/updates"):
        data = json.dumps(body)
        if self.web:
            done = subprocess.run(["curl", "-sf", "-H", "Content-Type: application/json", "--data-binary", "@-",
                                   f"{self.web}{path}"], input=data, capture_output=True, text=True)
        else:
            done = subprocess.run(["ssh", "-o", "BatchMode=yes", WEB_HOST,
                                   "curl -sf -H 'Content-Type: application/json' --data-binary @- "
                                   f"http://127.0.0.1:8080{path}"], input=data, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"the site took no lines: {done.stderr.strip() or done.stdout.strip()}")
        return json.loads(done.stdout)


# Said of a fix rather than of something new.
FIXED_WORDS = re.compile(r"\b(no longer|again|instead of|fix(es|ed)?|crash|stuck|wrong|broken|missing|"
                         r"does not|doesn't|didn't|did not|never|stops?|bug)\b", re.I)


def draft_text(subject):
    """A commit's subject as a first draft of a player's line: its first clause, without the
    MU2_BGFX: and the quote of who asked."""
    text = re.sub(r"^MU2_BGFX:\s*", "", subject)
    text = re.split(r"\s+--\s+|\s+\(the user\b|;\s", text)[0]
    text = re.split(r"(?<=[a-z0-9)])\.\s", text)[0].rstrip(". ")
    return text[:1].upper() + text[1:]


def player_facing(c):
    """A commit a player could notice: it ships the client or the server's rules."""
    return bool(c["parts"] & {"client", "server", "launcher"})


def reverted(found):
    """The commits a later one in `found` reverts, and those reverts."""
    gone = set()
    for c in found:
        if c["subject"].startswith("Revert"):
            body = git("log", "-1", "--format=%b", c["sha"])
            m = re.search(r"reverts commit ([0-9a-f]{7,40})", body)
            if m:
                gone |= {c["sha"], short(m.group(1))}
    return gone


WORDING = """You write the "Latest updates" list on the website of MU Classic, an online remake of
the MMORPG MU Online. Below are the game's commits that just went live, each with its hash, its
message and the files it touched. Turn them into the lines players read.

- One line for each change a player can notice. Fold several commits about the same thing into one
  line (tuning passes, follow-ups, a fix to a fix) and list all their hashes on it.
- Leave out what no player notices: protocol bumps, tooling, logs and audits, build or deploy
  fixes, a change and its revert, another session's leftovers.
- kind "new" for something added or changed on purpose; "fixed" for something that was wrong.
- Plain words a player knows (bag, vault, summon, Auto-play, Fairy Elf), present tense, no code
  names, no file names, no numbers players cannot see. One sentence, no full stop at the end.
- Match the voice of these published lines:
{examples}

The commits:
{commits}"""
WORDING_SCHEMA = {"type": "object", "required": ["lines"], "properties": {"lines": {"type": "array", "items": {
    "type": "object", "required": ["kind", "text", "commits"], "properties": {
        "kind": {"enum": ["new", "fixed"]}, "text": {"type": "string"},
        "commits": {"type": "array", "items": {"type": "string"}}}}}}}


def worded(found, examples, say):
    """The commits as players' lines, worded by `claude -p` (Claude Code's own command); None when
    it is not here or does not answer, and the plain drafts are used instead."""
    import shutil
    import tempfile
    if not shutil.which("claude") or not found:
        return None
    listed = []
    for c in found:
        body = git("log", "-1", "--format=%B", c["sha"])
        listed.append(f"[{c['sha']}] {body.strip()[:1500]}\n  files: {', '.join(c['files'][:12])}")
    prompt = WORDING.format(examples="\n".join(f"  {e['kind']}: {e['text']}" for e in examples[:24]) or "  (none yet)",
                            commits="\n\n".join(listed))
    say(f"wording {len(found)} commits with claude -p ...")
    # Run from an empty folder: the project's CLAUDE.md and memory are not this job's.
    with tempfile.TemporaryDirectory() as here:
        done = subprocess.run(["claude", "-p", "--model", "sonnet", "--output-format", "json", "--tools", "",
                               "--json-schema", json.dumps(WORDING_SCHEMA)],
                              input=prompt, capture_output=True, text=True, cwd=here, timeout=600)
    try:
        out = json.loads(done.stdout)
        lines = out["structured_output"]["lines"]
    except (ValueError, KeyError, TypeError):
        say(f"claude -p gave no lines ({(done.stderr or done.stdout).strip()[:160]}); plain drafts instead")
        return None
    shas = {c["sha"] for c in found}
    kept = []
    for line in lines:
        commits = [sha for sha in line.get("commits", []) if sha in shas]
        text = " ".join(str(line.get("text", "")).split()).rstrip(".")
        if commits and text and line.get("kind") in ("new", "fixed"):
            kept.append({"kind": line["kind"], "text": text[:400], "commits": commits})
    say(f"  {len(kept)} lines from {len(found)} commits (${out.get('total_cost_usd', 0):.2f})")
    return kept


def notes(say, dry=False, day=None, web=None, since=None, plain=False):
    """A draft for each player-facing commit live everywhere it ships, since the last run."""
    site = Site(web)
    told = site.get()
    noted = since or told.get("noted_through") or ""
    state = live(say)
    if state["errors"]:
        raise RuntimeError("could not read what is live: " + "; ".join(state["errors"]))
    if not noted:
        raise RuntimeError("the site has no noted_through yet: run `mu ship notes --import-site` once, "
                           "or give --since <commit> (the last commit already told)")
    if not known(noted) or not ancestor(noted, "HEAD"):
        raise RuntimeError(f"noted_through {noted} is not in HEAD's history; give --since")
    already = {sha for line in told.get("entries", []) for sha in line.get("commits", [])}
    lives = {
        "client": [c["commit"] for c in state["clients"].values() if c],
        "server": [b["deployed"] for b in state["boxes"] if b.get("deployed") and not b["staged"]],
        "launcher": [c["commit"] for c in state["launchers"].values() if c],
        "web": [state["web"]] if state["web"] else [],
    }

    def is_live(c):
        return all(lives[p] and all(known(v) and ancestor(c["sha"], v) for v in lives[p]) for p in c["parts"])

    found = commits(noted)
    gone = reverted(found)
    today = day or time.strftime("%Y-%m-%d")
    chosen, through = [], noted
    for c in found:
        if not is_live(c):
            break  # in order: the run stops at the first commit not yet out, and starts there next time
        through = c["sha"]
        if c["sha"] in gone or c["sha"] in already or not player_facing(c):
            continue
        chosen.append(c)
    examples = [e for e in told.get("entries", []) if e.get("state") == "published"]
    lines = None if plain else worded(chosen, examples, say)
    if lines is None:
        lines = [{"kind": "fixed" if FIXED_WORDS.search(c["subject"]) else "new", "text": draft_text(c["subject"]),
                  "commits": [c["sha"]]} for c in chosen]
    drafts = [dict(line, day=today, state="draft") for line in lines]
    waiting = len(found) - len([c for c in found if is_live(c)])
    for d in drafts:
        say(f"  {d['kind']:5} {' '.join(d['commits'])[:26]:26}  {d['text'][:110]}")
    say(f"{len(drafts)} drafts for {today}; told through {through}"
        + (f"; {waiting} commits not live yet wait for the next release" if waiting else ""))
    if dry:
        say("dry run: nothing sent")
        return True, {"drafts": drafts, "noted_through": through}
    if drafts or through != noted:
        site.post({"entries": drafts, "noted_through": through})
        say(f"on {web or SITE}/updates as drafts: word them there and publish")
    return True, {"drafts": drafts, "noted_through": through}


# ---- the site in its other languages: tools/mu/sitetranslate.py ----------------------------------

TRANSLATE_HOME = os.path.expanduser("~/Library/Application Support/mu-classic")
TRANSLATE_AGENT = os.path.expanduser("~/Library/LaunchAgents/com.mu-classic.translate.plist")


def translate(say, web=None, dry=False, langs=None, install=False):
    """The site's untranslated lines translated by claude -p (sitetranslate.py); --install puts it
    where launchd runs it every half hour, outside ~/Documents, and (re)starts the job."""
    import shutil
    import sitetranslate
    if not install:
        return sitetranslate.translate(say, web=web, dry=dry, langs=langs)
    os.makedirs(TRANSLATE_HOME, exist_ok=True)
    shutil.copy(os.path.join(os.path.dirname(__file__), "sitetranslate.py"), os.path.join(TRANSLATE_HOME, "sitetranslate.py"))
    shutil.copy(os.path.join(os.path.dirname(__file__), "translate.plist"), TRANSLATE_AGENT)
    me = f"gui/{os.getuid()}"
    subprocess.run(["launchctl", "bootout", f"{me}/com.mu-classic.translate"], capture_output=True)
    done = subprocess.run(["launchctl", "bootstrap", me, TRANSLATE_AGENT], capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"launchctl: {done.stderr.strip()}")
    say(f"installed in {TRANSLATE_HOME}; launchd runs it every half hour, its log in /tmp/mu-translate.log")
    return True, {}


class PanelParser(HTMLParser):
    """The hand-written lines of index.html's Latest updates: [(day, kind, text)]."""
    MONTHS = {m: i for i, m in enumerate(("january", "february", "march", "april", "may", "june", "july", "august",
                                          "september", "october", "november", "december"), 1)}

    def __init__(self):
        super().__init__()
        self.lines, self.day, self.kind, self.grab, self.buf = [], None, None, None, ""

    def handle_starttag(self, tag, attrs):
        cls = dict(attrs).get("class", "")
        if tag == "span" and cls == "fix-date":
            self.grab, self.buf = "date", ""
        elif tag == "h4":
            self.grab, self.buf = "kind", ""
        elif tag == "li" and self.day:
            self.grab, self.buf = "li", ""

    def handle_endtag(self, tag):
        if self.grab == "date" and tag == "span":
            d, m, y = self.buf.split()
            self.day = f"{int(y):04d}-{self.MONTHS[m.lower()]:02d}-{int(d):02d}"
        elif self.grab == "kind" and tag == "h4":
            self.kind = "fixed" if self.buf.strip().lower().startswith("fix") else "new"
        elif self.grab == "li" and tag == "li":
            self.lines.append((self.day, self.kind, " ".join(self.buf.split())))
        else:
            return
        self.grab = None

    def handle_data(self, data):
        if self.grab:
            self.buf += data


def import_site(say, web=None, since=None, dry=False):
    """index.html's hand-written lines, as published lines, once; noted_through set to what the
    clients run now (or --since), so `mu ship notes` starts after them."""
    site = Site(web)
    told = site.get()
    if told.get("entries"):
        raise RuntimeError(f"the site already keeps {len(told['entries'])} lines; nothing imported")
    page = open(os.path.join(ROOT, "web", "site", "index.html")).read()
    start, end = page.find("<!--updates-->"), page.find("<!--/updates-->")
    parser = PanelParser()
    parser.feed(page[start:end] if 0 <= start < end else page)
    lines = [{"day": d, "kind": k, "text": t, "commits": [], "state": "published", "position": n}
             for n, (d, k, t) in enumerate(parser.lines, 1)]
    if not since:
        state = live(say)
        since = oldest([c["commit"] for c in state["clients"].values() if c])
    say(f"{len(lines)} lines over {len({x['day'] for x in lines})} days; told through {since}")
    if dry:
        say("dry run: nothing sent")
        return True, {"lines": len(lines), "noted_through": since}
    site.post({"entries": lines, "noted_through": since})
    say(f"kept by the site now: {web or SITE}/updates")
    return True, {"lines": len(lines), "noted_through": since}


# ---- for the Ship app (tools/ship/ship.cpp) ------------------------------------------------------

def updates(say, web=None):
    """The site's lines and where `notes` got to, as the app lists them."""
    told = Site(web).get()
    say(f"{len(told.get('entries', []))} lines; told through {told.get('noted_through') or '(nothing yet)'}")
    return True, told


def send(say, path, web=None):
    """A file of {entries: [...], delete?: [id], noted_through?} to the site: drafts the app worded,
    lines it changed (with their id), lines it deleted."""
    with open(path) as f:
        body = json.load(f)
    done = Site(web).post(body)
    say(f"{done.get('added', 0)} lines saved, {len(body.get('delete', []))} deleted")
    return True, done


def app(say):
    """The Ship window: built into out/build (only it, and what it links), then opened."""
    build = os.path.join(ROOT, "out", "build")
    say("building mu_ship ...")
    done = subprocess.run(["nice", "cmake", "--build", build, "--target", "mu_ship"], cwd=ROOT,
                          capture_output=True, text=True)
    if done.returncode != 0:
        say(done.stdout[-3000:] + done.stderr[-2000:])
        return False, {"error": "mu_ship did not build"}
    os.execv(os.path.join(build, "mu_ship"), [os.path.join(build, "mu_ship")])
