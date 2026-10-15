#!/usr/bin/env python3
"""MU2's website and account service: the one-page site, and the accounts the launcher signs in with.

Python's standard library only (the user, 2026-10-14: no packages on the box). It listens on
127.0.0.1 behind Caddy (web/Caddyfile), which holds HTTPS and passes /api/* and the page here.

    web/server.py --db out/web/accounts.db            serve on 127.0.0.1:8080
    web/server.py --db X --port 8090 --site web/site  elsewhere

The routes:
    POST /api/register   {email, password}          -> 201 {ok}            | 400 {field, error} | 409 {error: taken}
    POST /api/login      {email, password}          -> 200 {token, email}  | 401 {error}
                                                    | 426 {error} from a launcher too old (OLD_LAUNCHER)
    POST /api/logout     Authorization: Bearer T    -> 200 {ok}
    POST /api/machine    {gpu, driver, cpu, ...}    -> 200 {ok} | 400 {error} | 429
         the launcher's report of the machine it runs on (docs/plans/launcher.md, L7): one JSON
         line per report in machines.jsonl beside the database, never anything about the account.
    POST /api/crash      {file, platform, sender, text, dump?, gpu?, driver?}  [Bearer T]
                                                    -> 200 {ok} | 400 | 413 | 429
         a crash or freeze report from the launcher or the game (src/launcher/crash_upload.h),
         text and dump in base64: kept under crashes/<day>/ beside the database, a line for it in
         crashes.jsonl. A token, or the game's `ticket:secret` (Bearer), files it under its
         account's id; neither is kept.
    POST /api/ticket     Authorization: Bearer T    -> 200 {ticket, secret} | 401 | 426 | 429
         the launcher's ask when Play is pressed (docs/plans/security.md): a ticket and its secret
         for the game, good once and for TICKET_SECONDS. The game never sees the token, and says
         only the ticket and proofs made with the secret on its line (src/net/pass.h).
    GET  /api/me         Authorization: Bearer T    -> 200 {email}         | 401
    GET  /api/downloads                             -> 200 {windows: {file, bytes} | null, macos: ...}
    GET  /internal/ticket?ticket=X  (127.0.0.1 only, never proxied) -> 200 {account, secret} | 404
         for mu2_server to learn whose a game's ticket is and the secret its proofs are made with.
         Each ticket is told once, and forgotten here as it is told.
    GET  /internal/crashes          (127.0.0.1 only) -> 200 {reports, where, last}
         the crash reports counted by the module they died in, and the latest thirty.

A token is 64 hex digits, and goes only over HTTPS. Only its SHA-256 is stored, so a copy of the
database signs nobody in. A ticket's secret is kept until the server is told it, then only its
SHA-256, for a day, which files the game's crash reports under the account. Passwords are scrypt
(n=2^14, r=8, p=1), each with its own salt.
"""

import argparse
import base64
import hashlib
import hmac
import json
import mimetypes
import os
import re
import secrets
import sqlite3
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

SESSION_DAYS = 90
# A ticket waits this long for the server to ask about it: the game starts and opens the character
# screen in seconds, a slow machine's first start in under a minute.
TICKET_SECONDS = 120
# And a ticket's account is known this long, for the crash reports the game sends as it starts.
TICKET_KEPT_SECONDS = 86400
MOST_BODY = 4096
EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s]+\.[^@\s]+$")
SCRYPT = dict(n=2**14, r=8, p=1, maxmem=64 * 1024 * 1024, dklen=32)

# What the page offers each system: on Windows the launcher (src/launcher/), which installs and
# updates the game itself (docs/plans/launcher.md, L4); on the Mac the app in a DMG (L5 -- the
# double-click installer file is served too for macOS before 27, which let quarantined scripts
# run; 27 refuses them outright). A file not there leaves its button saying so.
DOWNLOADS = {"windows": "MU2Launcher.exe", "macos": "MU2.dmg"}

# What a machine report may carry, and how long each field may be (the launcher's engine,
# reportMachine). The values are kept as they arrive -- a string cut to size, a number whole --
# and any field not named here is dropped, so the file holds what the fleet says and nothing else.
MACHINE_KEYS = {"gpu": 160, "vendor": 8, "driver": 64, "date": 32, "cpu": 128, "os": 48,
                "memory": 16, "ram": 16, "graphics": 16,
                "vulkan": 4, "old": 4, "software": 4, "tuned": 4}
# What one machine is counted by: the card, the CPU and the memory around it, and the OS.
MACHINE_FINGERPRINT = ("gpu", "cpu", "ram", "os")

# A crash report's request: a megabyte of text and three of minidump at most (crash_upload.cpp),
# in base64, with room for the fields around them.
MOST_CRASH_BODY = 6 * 1024 * 1024
# The most a day keeps, whoever sends them: a crash loop on many machines does not fill the disk.
MOST_CRASHES_A_DAY = 2000
CRASH_FIELDS = {"platform": 16, "sender": 16, "gpu": 160, "driver": 64}
CRASH_FILE = re.compile(r"^[A-Za-z0-9_.-]{1,96}\.txt$")

# The launchers that must be replaced: they cannot update themselves, so the site tells them so.
# A launcher says "MU2Launcher/<build>" (src/launcher/https.h kAgent); those before crash reports
# said "MU2Launcher" alone. Their sign-in is refused with words their screen shows as they are
# (one line, 250 units wide at most), and a kept sign-in is not known to them, which brings them to
# that screen. Browsers and the game are not launchers and are never turned away.
LAUNCHER_AGENT = re.compile(r"^MU2Launcher(?:/(\d+))?$")
LAUNCHER_NEEDED = 4
OLD_LAUNCHER = "Download the new launcher."

SCHEMA = """
CREATE TABLE IF NOT EXISTS accounts (
    id       INTEGER PRIMARY KEY,
    email    TEXT NOT NULL UNIQUE COLLATE NOCASE,
    password TEXT NOT NULL,
    created  INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    created    INTEGER NOT NULL,
    expires    INTEGER NOT NULL,
    last_used  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS sessions_account ON sessions(account_id);
CREATE TABLE IF NOT EXISTS tickets (
    ticket      TEXT PRIMARY KEY,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    secret      TEXT NOT NULL,      -- '' once the server has been told it
    secret_hash TEXT NOT NULL,
    created     INTEGER NOT NULL
);
"""


# ---- passwords and tokens ------------------------------------------------------------------------

def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    key = hashlib.scrypt(password.encode(), salt=salt, **SCRYPT)
    b64 = lambda b: base64.b64encode(b).decode()
    return f"scrypt${SCRYPT['n']}${SCRYPT['r']}${SCRYPT['p']}${b64(salt)}${b64(key)}"


def check_password(password: str, stored: str) -> bool:
    try:
        kind, n, r, p, salt, key = stored.split("$")
        if kind != "scrypt":
            return False
        want = base64.b64decode(key)
        got = hashlib.scrypt(password.encode(), salt=base64.b64decode(salt), n=int(n), r=int(r),
                             p=int(p), maxmem=SCRYPT["maxmem"], dklen=len(want))
        return hmac.compare_digest(got, want)
    except (ValueError, TypeError):
        return False


# Checked against when the email is unknown, so a wrong email costs the same time as a wrong password.
DUMMY_HASH = hash_password(secrets.token_hex(8))


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


# ---- the store -----------------------------------------------------------------------------------

class Store:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = str(path)
        self.local = threading.local()
        # The machine reports, one JSON line each, beside the database: append-only, greppable,
        # and never touched by the accounts (web/README.md).
        self.machines_path = path.parent / "machines.jsonl"
        self.machine_lock = threading.Lock()
        # The crash reports, each a file, and a line for each in crashes.jsonl (crash()).
        self.crashes_dir = path.parent / "crashes"
        self.crashes_path = path.parent / "crashes.jsonl"
        self.crash_lock = threading.Lock()
        self.crash_seen = set()
        self.crash_days = {}
        for row in self.crash_rows():
            self.crash_seen.add(row.get("sha"))
            day = str(row.get("day", ""))
            self.crash_days[day] = self.crash_days.get(day, 0) + 1
        with self.db() as db:
            db.executescript(SCHEMA)

    def db(self) -> sqlite3.Connection:
        db = getattr(self.local, "db", None)
        if db is None:
            db = sqlite3.connect(self.path, timeout=5, isolation_level=None)
            db.execute("PRAGMA journal_mode=WAL")
            db.execute("PRAGMA foreign_keys=ON")
            db.execute("PRAGMA busy_timeout=5000")
            self.local.db = db
        return db

    def register(self, email: str, password: str) -> bool:
        try:
            self.db().execute("INSERT INTO accounts (email, password, created) VALUES (?, ?, ?)",
                              (email, hash_password(password), int(time.time())))
            return True
        except sqlite3.IntegrityError:
            return False

    def login(self, email: str, password: str):
        row = self.db().execute("SELECT id, email, password FROM accounts WHERE email = ?",
                                (email,)).fetchone()
        if row is None:
            check_password(password, DUMMY_HASH)
            return None
        if not check_password(password, row[2]):
            return None
        token = secrets.token_hex(32)
        now = int(time.time())
        db = self.db()
        db.execute("DELETE FROM sessions WHERE expires < ?", (now,))
        db.execute("INSERT INTO sessions VALUES (?, ?, ?, ?, ?)",
                   (token_hash(token), row[0], now, now + SESSION_DAYS * 86400, now))
        return token, row[1]

    def session(self, token: str):
        """The account a live token belongs to, as (id, email), or None."""
        if not re.fullmatch(r"[0-9a-f]{64}", token or ""):
            return None
        now = int(time.time())
        row = self.db().execute(
            "SELECT a.id, a.email FROM sessions s JOIN accounts a ON a.id = s.account_id "
            "WHERE s.token_hash = ? AND s.expires >= ?", (token_hash(token), now)).fetchone()
        if row is not None:
            self.db().execute("UPDATE sessions SET last_used = ? WHERE token_hash = ?",
                              (now, token_hash(token)))
        return row

    def ticket(self, account: int):
        """A new ticket for the account, and its secret."""
        ticket, secret = secrets.token_hex(16), secrets.token_hex(32)
        now = int(time.time())
        db = self.db()
        db.execute("DELETE FROM tickets WHERE created < ?", (now - TICKET_KEPT_SECONDS,))
        db.execute("INSERT INTO tickets VALUES (?, ?, ?, ?, ?)",
                   (ticket, account, secret, token_hash(secret), now))
        return ticket, secret

    def redeem(self, ticket: str):
        """A ticket's (account id, secret), once and within TICKET_SECONDS. None for one not kept."""
        if not re.fullmatch(r"[0-9a-f]{32}", ticket or ""):
            return None
        db = self.db()
        row = db.execute("SELECT account_id, secret FROM tickets WHERE ticket = ? AND secret != '' AND created >= ?",
                         (ticket, int(time.time()) - TICKET_SECONDS)).fetchone()
        if row is None:
            return None
        # Told once: whoever asks second -- a line read and said again -- is told nothing.
        if db.execute("UPDATE tickets SET secret = '' WHERE ticket = ? AND secret != ''", (ticket,)).rowcount != 1:
            return None
        return row[0], row[1]

    def ticket_account(self, said: str):
        """The account of a game's `ticket:secret`, within TICKET_KEPT_SECONDS, or None."""
        found = re.fullmatch(r"([0-9a-f]{32}):([0-9a-f]{64})", said or "")
        if not found:
            return None
        row = self.db().execute("SELECT account_id, secret_hash FROM tickets WHERE ticket = ? AND created >= ?",
                                (found.group(1), int(time.time()) - TICKET_KEPT_SECONDS)).fetchone()
        if row is None or not hmac.compare_digest(row[1], token_hash(found.group(2))):
            return None
        return row[0]

    def logout(self, token: str):
        self.db().execute("DELETE FROM sessions WHERE token_hash = ?", (token_hash(token or ""),))

    def machine(self, row: dict):
        with self.machine_lock:
            with self.machines_path.open("a") as f:
                f.write(json.dumps(row, separators=(",", ":")) + "\n")

    def crash_rows(self) -> list:
        rows = []
        if self.crashes_path.is_file():
            with self.crashes_path.open() as f:
                for line in f:
                    try:
                        row = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(row, dict):
                        rows.append(row)
        return rows

    def crash(self, row: dict, text: bytes, dump: bytes) -> str:
        """Keeps one report: "kept", "seen" (the same text came before) or "full" (the day's done)."""
        with self.crash_lock:
            if row["sha"] in self.crash_seen:
                return "seen"
            day = row["day"]
            if self.crash_days.get(day, 0) >= MOST_CRASHES_A_DAY:
                return "full"
            folder = self.crashes_dir / day
            folder.mkdir(parents=True, exist_ok=True)
            stem = f"{time.strftime('%H%M%S', time.gmtime(row['at']))}-{row['sha'][:8]}-{row['file'][:-4]}"
            (folder / f"{stem}.txt").write_bytes(text)
            if dump:
                (folder / f"{stem}.dmp").write_bytes(dump)
            row["saved"] = f"{day}/{stem}.txt"
            with self.crashes_path.open("a") as f:
                f.write(json.dumps(row, separators=(",", ":")) + "\n")
            self.crash_seen.add(row["sha"])
            self.crash_days[day] = self.crash_days.get(day, 0) + 1
            return "kept"

    def crashes_report(self) -> dict:
        rows = self.crash_rows()
        where = {}
        for row in rows:
            name = str(row.get("where", "")).split("+")[0] or "?"
            where[name] = where.get(name, 0) + 1
        return {"reports": len(rows), "where": dict(sorted(where.items(), key=lambda one: -one[1])),
                "last": rows[-30:][::-1]}

    def machines_report(self) -> dict:
        """What has been reported, counted: a driver-targeted update is read off this."""
        rows = []
        if self.machines_path.is_file():
            with self.machines_path.open() as f:
                for line in f:
                    try:
                        row = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(row, dict):
                        rows.append(row)

        def tally(key: str) -> dict:
            counts = {}
            for row in rows:
                name = str(row.get(key, ""))
                counts[name] = counts.get(name, 0) + 1
            return dict(sorted(counts.items(), key=lambda one: -one[1]))

        return {"launches": len(rows),
                "machines": len({row.get("fp") for row in rows if row.get("fp")}),
                "gpu": tally("gpu"), "driver": tally("driver"), "graphics": tally("graphics"),
                "last": rows[-10:][::-1]}


# ---- a little rate limit, per client address ------------------------------------------------------

class Limit:
    """At most `most` tries per `seconds` from one address."""

    def __init__(self, most: int, seconds: int):
        self.most, self.seconds = most, seconds
        self.tries = {}
        self.lock = threading.Lock()

    def allow(self, who: str) -> bool:
        now = time.monotonic()
        with self.lock:
            recent = [t for t in self.tries.get(who, []) if now - t < self.seconds]
            if len(recent) >= self.most:
                self.tries[who] = recent
                return False
            recent.append(now)
            self.tries[who] = recent
            if len(self.tries) > 10000:
                self.tries = {k: v for k, v in self.tries.items() if v and now - v[-1] < self.seconds}
            return True


REGISTER_LIMIT = Limit(5, 3600)
LOGIN_LIMIT = Limit(10, 60)
# A report a run: a player never opens the launcher sixty times in an hour, and a machine that
# tries to fill the file is refused after the sixty-first.
MACHINE_LIMIT = Limit(60, 3600)
# Eight a start (crash_upload.cpp kMostAStart), a few starts an hour.
CRASH_LIMIT = Limit(24, 3600)
# A ticket a Play: sixty an hour is a player who starts the game every minute.
TICKET_LIMIT = Limit(60, 3600)


# ---- the handler ---------------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "mu2-web"
    sys_version = ""
    store: Store = None
    site: Path = None
    downloads: Path = None

    def log_message(self, fmt, *args):
        print(f"{self.client()} {fmt % args}", flush=True)

    def client(self) -> str:
        # Behind Caddy on the same machine, the caller is the last address Caddy appended.
        if self.client_address[0] in ("127.0.0.1", "::1"):
            forwarded = self.headers.get("X-Forwarded-For", "")
            if forwarded:
                return forwarded.split(",")[-1].strip()
        return self.client_address[0]

    def direct(self) -> bool:
        """Asked on this machine itself, not through Caddy."""
        return self.client_address[0] in ("127.0.0.1", "::1") and "X-Forwarded-For" not in self.headers

    def reply(self, status: int, body: dict):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def body(self, most: int = MOST_BODY):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > most:
            return None
        try:
            data = json.loads(self.rfile.read(length))
            return data if isinstance(data, dict) else None
        except (ValueError, UnicodeDecodeError):
            return None

    def old_launcher(self) -> bool:
        said = LAUNCHER_AGENT.match(self.headers.get("User-Agent", "").strip())
        return bool(said) and int(said.group(1) or 1) < LAUNCHER_NEEDED

    def bearer(self) -> str:
        said = self.headers.get("Authorization", "")
        return said[7:].strip() if said.startswith("Bearer ") else ""

    # -- POST

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/register":
            return self.register()
        if path == "/api/login":
            return self.login()
        if path == "/api/logout":
            self.store.logout(self.bearer())
            return self.reply(200, {"ok": True})
        if path == "/api/machine":
            return self.machine()
        if path == "/api/crash":
            return self.crash()
        if path == "/api/ticket":
            return self.ticket()
        self.reply(404, {"error": "not found"})

    def register(self):
        said = self.body()
        if said is None:
            return self.reply(400, {"error": "bad request"})
        email = str(said.get("email", "")).strip().lower()
        password = str(said.get("password", ""))
        if not EMAIL.match(email) or len(email) > 254:
            return self.reply(400, {"field": "email", "error": "Enter a valid email address."})
        if len(password) < 8:
            return self.reply(400, {"field": "password", "error": "Use at least 8 characters."})
        if len(password) > 128:
            return self.reply(400, {"field": "password", "error": "Use at most 128 characters."})
        if not REGISTER_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many accounts from here. Try again later."})
        if not self.store.register(email, password):
            return self.reply(409, {"error": "taken"})
        print(f"{self.client()} registered account {email}", flush=True)
        self.reply(201, {"ok": True})

    def login(self):
        said = self.body()
        if said is None:
            return self.reply(400, {"error": "bad request"})
        if not LOGIN_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many tries. Wait a minute."})
        if self.old_launcher():
            print(f"{self.client()} an old launcher was told to download the new one", flush=True)
            return self.reply(426, {"error": OLD_LAUNCHER})
        email = str(said.get("email", "")).strip().lower()
        password = str(said.get("password", ""))
        signed = self.store.login(email, password) if email and password else None
        if signed is None:
            return self.reply(401, {"error": "Wrong email or password."})
        self.reply(200, {"token": signed[0], "email": signed[1]})

    def machine(self):
        """One launcher's report of its machine: kept as it came, inside MACHINE_KEYS."""
        said = self.body()
        if said is None:
            return self.reply(400, {"error": "bad request"})
        if not MACHINE_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "later"})
        report = {}
        for key, room in MACHINE_KEYS.items():
            value = said.get(key)
            if isinstance(value, bool):
                report[key] = int(value)
            elif isinstance(value, (int, float)):
                report[key] = value
            elif isinstance(value, str) and value.strip():
                report[key] = value[:room]
        if not report.get("gpu"):
            return self.reply(400, {"error": "no machine"})
        report["at"] = int(time.time())
        joined = "|".join(str(report.get(key, "")) for key in MACHINE_FINGERPRINT)
        report["fp"] = hashlib.sha256(joined.encode()).hexdigest()[:16]
        self.store.machine(report)
        print(f"{self.client()} machine {report['fp']} {report.get('gpu')} driver {report.get('driver', '?')}",
              flush=True)
        self.reply(200, {"ok": True})

    def crash(self):
        """One crash or freeze report (src/launcher/crash_upload.h), kept with its dump."""
        if int(self.headers.get("Content-Length") or 0) > MOST_CRASH_BODY:
            return self.reply(413, {"error": "too big"})
        if not CRASH_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "later"})
        said = self.body(MOST_CRASH_BODY)
        if said is None:
            return self.reply(400, {"error": "bad request"})
        name = str(said.get("file", ""))
        if not CRASH_FILE.match(name):
            return self.reply(400, {"error": "bad file"})
        try:
            text = base64.b64decode(str(said.get("text", "")), validate=True)
            dump = base64.b64decode(str(said.get("dump", "")), validate=True)
        except ValueError:
            return self.reply(400, {"error": "bad base64"})
        if not text:
            return self.reply(400, {"error": "empty"})
        row = {"at": int(time.time()), "day": time.strftime("%Y-%m-%d", time.gmtime()), "file": name,
               "sha": hashlib.sha256(text).hexdigest()}
        for key, room in CRASH_FIELDS.items():
            value = said.get(key)
            if isinstance(value, str) and value.strip():
                row[key] = value[:room]
        row["program"] = "launcher" if name.startswith("launcher-") else "game"
        row["kind"] = "hang" if "hang-" in name else "crash"
        lines = text.decode("utf-8", "replace").splitlines()
        row["what"] = (lines[0] if lines else "")[:200]
        # Windows names the module the fault is in on a line of its own (core/watch_win.cpp).
        row["where"] = next((line[3:].strip()[:120] for line in lines[:4] if line.startswith("in ")), "")
        for line in lines[:12]:
            if line.startswith("MU2 "):
                row["build"] = line[4:].strip()[:64]
            elif " gpu " in line and " on " in line:
                row["renderer"] = line.strip()[:120]
        signed = self.store.session(self.bearer()) if self.bearer() else None
        account = signed[0] if signed else self.store.ticket_account(self.bearer())
        if account:
            row["account"] = account
        if dump:
            row["dump"] = len(dump)
        kept = self.store.crash(row, text, dump)
        if kept == "full":
            return self.reply(429, {"error": "later"})
        print(f"{self.client()} crash {kept}: {row['program']} {row['kind']} {row.get('platform', '?')} "
              f"{row.get('gpu') or row.get('renderer', '')} | {row['what']} {row['where']}", flush=True)
        self.reply(200, {"ok": True})

    def ticket(self):
        if self.old_launcher():
            return self.reply(426, {"error": OLD_LAUNCHER})
        if not TICKET_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many starts. Wait a minute."})
        row = self.store.session(self.bearer())
        if row is None:
            return self.reply(401, {"error": "signed out"})
        ticket, secret = self.store.ticket(row[0])
        self.reply(200, {"ticket": ticket, "secret": secret})

    # -- GET

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/api/me":
            if self.old_launcher():
                return self.reply(401, {"error": OLD_LAUNCHER})
            row = self.store.session(self.bearer())
            return self.reply(200, {"email": row[1]}) if row else self.reply(401, {"error": "signed out"})
        if url.path == "/api/downloads":
            return self.reply(200, self.listing())
        if url.path == "/internal/crashes":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            return self.reply(200, self.store.crashes_report())
        if url.path == "/internal/machines":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            return self.reply(200, self.store.machines_report())
        if url.path == "/internal/ticket":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            row = self.store.redeem(parse_qs(url.query).get("ticket", [""])[0])
            return self.reply(200, {"account": row[0], "secret": row[1]}) if row else self.reply(404, {"error": "no ticket"})
        if url.path.startswith("/api/"):
            return self.reply(404, {"error": "not found"})
        self.file(url.path)

    def listing(self) -> dict:
        out = {}
        for system, name in DOWNLOADS.items():
            f = self.downloads / name
            out[system] = {"file": f"/downloads/{name}", "bytes": f.stat().st_size} if f.is_file() else None
        return out

    def file(self, path: str):
        # The page and its pictures. Caddy serves /downloads/ itself; this answers for local runs.
        if path in ("", "/"):
            path = "/index.html"
        root = self.downloads if path.startswith("/downloads/") else self.site
        rel = path[len("/downloads/"):] if root is self.downloads else path.lstrip("/")
        target = (root / rel).resolve()
        if root.resolve() not in target.parents or not target.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(target.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default="out/web/accounts.db")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--site", default=str(here / "site"))
    ap.add_argument("--downloads", default=str(here / "downloads"))
    args = ap.parse_args()
    Handler.store = Store(Path(args.db))
    Handler.site = Path(args.site)
    Handler.downloads = Path(args.downloads)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"mu2-web on http://{args.host}:{args.port}, accounts in {args.db}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
