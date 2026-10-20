"""`mu ship`: what HEAD holds that players do not have yet, what it takes to give it to them, and
the front page's Latest updates for what they were given.

    ./mu ship                  what is live where, each commit not yet shipped and what it needs,
                               and the commands to ship it, in their order
    ./mu ship --go             the same, then each command run in turn, asked before each one
    ./mu ship notes [--dry]    a draft on the site's /updates page for each change that is now live
                               and not yet told; an admin words them there and publishes them
    ./mu ship notes --import-site   the lines written into web/site/index.html by hand, put in as
                               published (once, the first time the site keeps them itself)

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
"""
import json
import os
import re
import subprocess
import sys
import time
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
    ("Southeast Asia", "singapore", "MU2_HOST=singapore "),
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
    state = {"clients": {}, "launchers": {}, "boxes": [], "web": None, "errors": []}
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
                             "journalctl -u mu2-server -o cat --no-pager --since @$s | grep -m1 -o 'mu2_server: protocol [0-9]*'")
            lines = said.splitlines() + [""] * 5
            box["deployed"] = lines[0].strip()
            built, started = int(lines[1] or 0), int(lines[2] or 0)
            box["staged"] = built > started + 5
            box["active"] = lines[3].strip() == "active"
            found = re.search(r"protocol (\d+)", lines[4])
            box["protocol"] = int(found.group(1)) if found else None
        except (RuntimeError, ValueError, subprocess.TimeoutExpired) as e:
            box["error"] = str(e)[:200]
        return box

    def read_web():
        try:
            return ssh(WEB_HOST, "cat /opt/mu2-web/DEPLOYED").strip()
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

def pending(since, part):
    """The commits after `since` that ship `part`; None when `since` is not in HEAD's history."""
    if not known(since) or not ancestor(since, "HEAD"):
        return None
    return [c for c in commits(since) if part in c["parts"]]


def report(say):
    head = short("HEAD")
    head_protocol = release.protocol_of(head)
    state = live(say)
    for e in state["errors"]:
        say(f"! could not read {e}")
    plan = {"head": head, "protocol": head_protocol, "parts": {}, "steps": [], "rows": [], "unshipped": [],
            "errors": state["errors"], "subject": git("log", "-1", "--format=%s", "HEAD")}

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
        todo = pending(c["commit"], "client")
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
        todo = pending(c["commit"], "launcher")
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
        todo = pending(box["deployed"], "server")
        how = ("STAGED, not switched" if box["staged"] else "running") if box["active"] else "STOPPED"
        say(f"server   {box['region']:15} {box['deployed']}  protocol {box['protocol']}  {how}  "
            f"{'?' if todo is None else (str(len(todo)) + ' behind' if todo else 'current')}")
        row("server", box["region"], box["deployed"], todo, protocol=box["protocol"], how=how)
        if todo or box["staged"] or not box["active"]:
            server_todo.append(box)
        plan["parts"][f"server-{box['host']}"] = {"live": box["deployed"], "staged": box["staged"],
                                                  "behind": [x["sha"] for x in todo or []]}
    # The site.
    web_todo = pending(state["web"], "web") if state["web"] else None
    if state["web"]:
        row("site", "mu-classic.com", state["web"], web_todo)
        say(f"site     {'':8} {state['web']}  {'?' if web_todo is None else (str(len(web_todo)) + ' behind' if web_todo else 'current')}")

    # Every commit not yet everywhere, with what it needs.
    floor = oldest([c["commit"] for c in state["clients"].values() if c]
                   + [b["deployed"] for b in state["boxes"] if b.get("deployed")]
                   + ([state["web"]] if state["web"] else [])
                   + [c["commit"] for c in state["launchers"].values() if c])
    unshipped = commits(floor) if floor and ancestor(floor, "HEAD") else []
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
    steps = []
    if bump:
        verdict = (f"PROTOCOL {min([c['protocol'] for c in state['clients'].values() if c] or [0])} -> {head_protocol}: "
                   f"every server and every client together; logins are refused between the switch of one and the other")
        for p in CLIENTS:
            steps.append((f"./mu dist {p} --commit {head}", "build"))
        for p in CLIENTS:
            steps.append((f"./mu release {p} --dry", "check"))
        for b in state["boxes"]:
            steps.append((f"{b['env']}server/deploy.sh --stage", "stage"))
        for p in CLIENTS:
            steps.append((f"./mu release {p} --stage", "stage"))
        for b in state["boxes"]:
            steps.append((f"{b['env']}server/deploy.sh --switch", "LIVE: restarts the server"))
        for p in CLIENTS:
            steps.append((f"./mu release {p}", "LIVE: players update"))
    else:
        verdicts = []
        if server_todo:
            verdicts.append(f"server deploy on {', '.join(b['region'] for b in server_todo)} "
                            f"(a restart: players on it are dropped for a few seconds)")
            for b in server_todo:
                if b["staged"] and not pending(b["deployed"], "server"):
                    steps.append((f"{b['env']}server/deploy.sh --switch", "LIVE: restarts the server"))
                else:
                    steps.append((f"{b['env']}server/deploy.sh", "LIVE: builds, tests, restarts the server"))
        if client_todo:
            verdicts.append(f"client patch for {', '.join(client_todo)} (the launcher fetches it; no restart)")
            for p in client_todo:
                steps.append((f"./mu dist {p} --commit {head}", "build"))
                steps.append((f"./mu release {p} --dry", "check"))
                steps.append((f"./mu release {p}", "LIVE: players update"))
        if launcher_todo:
            verdicts.append("launcher update (every launcher replaces itself)")
            steps.append((f"./mu dist launcher --commit {head}", "build"))
            steps.append(("./mu release launcher --dry", "check"))
            steps.append(("./mu release launcher", "LIVE: every launcher updates"))
        if web_todo:
            verdicts.append("site deploy")
            steps.append(("web/deploy.sh", "LIVE: the site restarts"))
        verdict = "; ".join(verdicts) if verdicts else "nothing to ship: every part is current"
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


# ---- the Latest updates --------------------------------------------------------------------------

class Site:
    """The site's /internal/updates: on the box over ssh, or a local web/server.py (--web URL)."""

    def __init__(self, web=None):
        self.web = web

    def get(self):
        if self.web:
            return fetch(f"{self.web}/internal/updates")
        return json.loads(ssh(WEB_HOST, "curl -sf http://127.0.0.1:8080/internal/updates"))

    def post(self, body):
        data = json.dumps(body)
        if self.web:
            done = subprocess.run(["curl", "-sf", "-H", "Content-Type: application/json", "--data-binary", "@-",
                                   f"{self.web}/internal/updates"], input=data, capture_output=True, text=True)
        else:
            done = subprocess.run(["ssh", "-o", "BatchMode=yes", WEB_HOST,
                                   "curl -sf -H 'Content-Type: application/json' --data-binary @- "
                                   "http://127.0.0.1:8080/internal/updates"], input=data, capture_output=True, text=True)
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
