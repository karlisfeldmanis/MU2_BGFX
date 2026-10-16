#!/usr/bin/env python3
"""MU2's website and account service: the one-page site, and the accounts the launcher signs in with.

Python's standard library only (the user, 2026-10-14: no packages on the box). It listens on
127.0.0.1 behind Caddy (web/Caddyfile), which holds HTTPS and passes /api/* and the page here.

    web/server.py --db out/web/accounts.db            serve on 127.0.0.1:8080
    web/server.py --db X --port 8090 --site web/site  elsewhere

The routes:
    POST /api/register   {email, password, name}    -> 201 {ok}            | 400 {field, error} | 409 {field, error}
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
    GET  /api/account    Bearer T                   -> 200 {email, name, created, sessions, devices, plays} | 401
         the account page: each sign-in (browser or launcher, its address shortened), each machine
         its launcher reported (with the token, or from the address that pressed Play within
         RECENT_MACHINE_SECONDS), and each Play of the last PLAYS_DAYS days with its machine.
    POST /api/name       Bearer T {name}            -> 200 {ok} | 400 {field, error} | 409
    POST /api/password   Bearer T {old, new}        -> 200 {ok} | 400 {field, error} | 401
         every other sign-in of the account is ended; the one that asked stays.
    POST /api/sessions/end  Bearer T {id}           -> 200 {ok} | 404     one sign-in ended, by the id /api/account gave
    POST /api/delete     Bearer T {password}        -> 200 {ok} | 400 {field, error} | 401
         the account and everything under it (sign-ins, tickets, plays, machines, comments) gone at once.
    GET  /api/comments?post=ID                      -> 200 {comments: [{name, text, at}]}
    POST /api/comments   Bearer T {post, text}      -> 201 {ok} | 400 {error} | 401 | 429
         on the posts of site/posts.json, under the account's name.
    GET  /api/downloads                             -> 200 {windows: {file, bytes} | null, macos: ...}
    GET  /api/online     Bearer T of an admin       -> 200 {at, fresh, players: [...]} | 404 for anyone else
         who is on the game server now, as mu2_server last told it (the admins' /online page), and
         `recent`: the last RECENT_SHOWN plays of the last RECENT_DAYS days, bots left out; and
         `machines`: by account id, the machine its launcher last reported (devices); and `everyone`:
         every account with a character, its characters as mu2_server last listed them (a minute
         apart), and its Plays (launcher starts) of the last PLAYS_DAYS days.
    GET  /api/character?account=N&name=X   Bearer T of an admin -> 200 {name, class, level, world, saved,
         second, experience, zen, points, worn, bag} | 404: one character's stats and gear, as mu2_server
         last listed it (a minute apart).
    POST /internal/online   (127.0.0.1 only) {players: [{account, name, class, level, world, copy, x, y, bot}],
                             characters?: [{account, name, class, level, world, saved, fresh}]}
         mu2_server's list of who is on, every 5 s (server/src/signin.cpp tellOnline); kept in memory.
    POST /internal/role     (127.0.0.1 only) {email, role}  -> 200 {ok} | 404
         an account made "admin" or back to "player": on the box,
         curl -s -d '{"email":"X","role":"admin"}' http://127.0.0.1:8080/internal/role
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
# The plays an account page lists, and how long each is kept.
PLAYS_DAYS = 30
# A launcher reports its machine when it opens and asks a ticket when Play is pressed, both from
# the same address; a report this recent from the address that asks is that Play's machine. The
# address is kept only in memory and only this long, never written down beside the report.
RECENT_MACHINE_SECONDS = 6 * 3600
NAME = re.compile(r"^[A-Za-z0-9_]{3,16}$")
MOST_COMMENT = 500
# A ticket waits this long for the server to ask about it: the game starts and opens the character
# screen in seconds, a slow machine's first start in under a minute.
TICKET_SECONDS = 120
# And a ticket's account is known this long, for the crash reports the game sends as it starts.
TICKET_KEPT_SECONDS = 86400
MOST_BODY = 4096
# An account's role: "player", or "admin", who may open the /online page (/internal/role sets it).
ROLES = ("player", "admin")
# mu2_server's list of who is on: a few hundred players at most, each line under 200 bytes.
MOST_ONLINE_BODY = 8 * 1024 * 1024  # and once a minute every character's gear, a few KB each
# The list is told every 5 s; one older than this is a server that stopped telling (down, or restarting).
ONLINE_STALE_SECONDS = 20
# A play: one character on the server from when he came on until he was last on a list, kept this
# long (the `games` table). Off a list no longer than RECENT_GAP -- the website restarting -- he is
# still on the same play.
RECENT_DAYS = 30
RECENT_SHOWN = 100
RECENT_GAP = 30
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
                "vulkan": 4, "old": 4, "software": 4, "tuned": 4, "renderer": 4}
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
CREATE TABLE IF NOT EXISTS plays (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    at         INTEGER NOT NULL,
    ip         TEXT NOT NULL,
    fp         TEXT NOT NULL DEFAULT ''   -- the machine it was played on (devices.fp), '' unknown
);
CREATE INDEX IF NOT EXISTS plays_account ON plays(account_id, at);
CREATE TABLE IF NOT EXISTS devices (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    fp         TEXT NOT NULL,
    report     TEXT NOT NULL,      -- the machine report as /api/machine kept it, JSON
    ip         TEXT NOT NULL,
    seen       INTEGER NOT NULL,
    PRIMARY KEY (account_id, fp)
);
CREATE TABLE IF NOT EXISTS comments (
    id         INTEGER PRIMARY KEY,
    post       TEXT NOT NULL,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    text       TEXT NOT NULL,
    at         INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS comments_post ON comments(post, at);
CREATE TABLE IF NOT EXISTS games (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,       -- the character
    class      TEXT NOT NULL,
    level      INTEGER NOT NULL,    -- as he was last on a list
    world      TEXT NOT NULL,
    since      INTEGER NOT NULL,
    until      INTEGER NOT NULL,    -- the last list he was on
    PRIMARY KEY (account_id, name, since)
);
CREATE INDEX IF NOT EXISTS games_until ON games(until);
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


# ---- what the account page shows of a sign-in ------------------------------------------------------

def short_ip(ip: str) -> str:
    """An address with its last half hidden: enough to tell home from elsewhere, not to find anyone."""
    if ":" in ip:
        return ":".join(ip.split(":")[:2]) + ":…"
    parts = ip.split(".")
    return ".".join(parts[:2] + ["x", "x"]) if len(parts) == 4 else ""


def agent_name(agent: str) -> str:
    """'MU Classic launcher' or the browser and its system, read off the User-Agent."""
    if LAUNCHER_AGENT.match(agent.strip()):
        return "MU Classic launcher"
    system = next((name for key, name in (("iPhone", "iPhone"), ("iPad", "iPad"), ("Android", "Android"),
                                          ("Windows", "Windows"), ("Mac OS X", "macOS"), ("Linux", "Linux"))
                   if key in agent), "")
    browser = next((name for key, name in (("Edg/", "Edge"), ("Firefox/", "Firefox"), ("Chrome/", "Chrome"),
                                           ("Safari/", "Safari")) if key in agent), "Browser")
    return f"{browser} on {system}" if system else browser


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
        # The last report from each address, for RECENT_MACHINE_SECONDS (remember(), recent()).
        self.recent_machines = {}
        # The crash reports, each a file, and a line for each in crashes.jsonl (crash()).
        self.crashes_dir = path.parent / "crashes"
        self.crashes_path = path.parent / "crashes.jsonl"
        self.crash_lock = threading.Lock()
        self.crash_seen = set()
        # Who is on the game server, as mu2_server last told it (told_online); never on disk.
        self.online_lock = threading.Lock()
        self.online = {"at": 0, "players": []}
        self.games_swept = 0
        # Every character of an account, as mu2_server last listed it (told_characters).
        self.characters = []
        self.crash_days = {}
        for row in self.crash_rows():
            self.crash_seen.add(row.get("sha"))
            day = str(row.get("day", ""))
            self.crash_days[day] = self.crash_days.get(day, 0) + 1
        with self.db() as db:
            db.executescript(SCHEMA)
            # Columns added after the first accounts: an account's public name (comments), and
            # where and with what each sign-in was made (the account page).
            have = {row[1] for row in db.execute("PRAGMA table_info(accounts)")}
            if "name" not in have:
                db.execute("ALTER TABLE accounts ADD COLUMN name TEXT")
            db.execute("CREATE UNIQUE INDEX IF NOT EXISTS accounts_name ON accounts(name COLLATE NOCASE)")
            if "fp" not in {row[1] for row in db.execute("PRAGMA table_info(plays)")}:
                db.execute("ALTER TABLE plays ADD COLUMN fp TEXT NOT NULL DEFAULT ''")
            if "role" not in have:
                db.execute("ALTER TABLE accounts ADD COLUMN role TEXT NOT NULL DEFAULT 'player'")
            have = {row[1] for row in db.execute("PRAGMA table_info(sessions)")}
            for column in ("ip", "agent"):
                if column not in have:
                    db.execute(f"ALTER TABLE sessions ADD COLUMN {column} TEXT NOT NULL DEFAULT ''")

    def db(self) -> sqlite3.Connection:
        db = getattr(self.local, "db", None)
        if db is None:
            db = sqlite3.connect(self.path, timeout=5, isolation_level=None)
            db.execute("PRAGMA journal_mode=WAL")
            db.execute("PRAGMA foreign_keys=ON")
            db.execute("PRAGMA busy_timeout=5000")
            self.local.db = db
        return db

    def register(self, email: str, password: str, name: str):
        """None when made, else which of the two is taken: "email" or "name"."""
        db = self.db()
        if db.execute("SELECT 1 FROM accounts WHERE email = ?", (email,)).fetchone():
            return "email"
        if name and db.execute("SELECT 1 FROM accounts WHERE name = ? COLLATE NOCASE", (name,)).fetchone():
            return "name"
        try:
            db.execute("INSERT INTO accounts (email, password, created, name) VALUES (?, ?, ?, ?)",
                       (email, hash_password(password), int(time.time()), name or None))
            return None
        except sqlite3.IntegrityError:
            return "email"

    def login(self, email: str, password: str, ip: str = "", agent: str = ""):
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
        db.execute("INSERT INTO sessions (token_hash, account_id, created, expires, last_used, ip, agent) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?)",
                   (token_hash(token), row[0], now, now + SESSION_DAYS * 86400, now, ip, agent[:200]))
        return token, row[1]

    def check(self, account: int, password: str) -> bool:
        row = self.db().execute("SELECT password FROM accounts WHERE id = ?", (account,)).fetchone()
        return bool(row) and check_password(password, row[0])

    def set_password(self, account: int, password: str, keep: str):
        """The new password, and every sign-in but `keep` (a token) ended."""
        db = self.db()
        db.execute("UPDATE accounts SET password = ? WHERE id = ?", (hash_password(password), account))
        db.execute("DELETE FROM sessions WHERE account_id = ? AND token_hash != ?", (account, token_hash(keep)))

    def set_name(self, account: int, name: str) -> bool:
        try:
            self.db().execute("UPDATE accounts SET name = ? WHERE id = ?", (name, account))
            return True
        except sqlite3.IntegrityError:
            return False

    def delete(self, account: int):
        self.db().execute("DELETE FROM accounts WHERE id = ?", (account,))

    def end_session(self, account: int, sid: str) -> bool:
        """Ends the account's sign-in whose id (its hash's first 16 digits) is `sid`."""
        if not re.fullmatch(r"[0-9a-f]{16}", sid or ""):
            return False
        return self.db().execute("DELETE FROM sessions WHERE account_id = ? AND substr(token_hash, 1, 16) = ?",
                                 (account, sid)).rowcount > 0

    def played(self, account: int, ip: str, fp: str = ""):
        now = int(time.time())
        db = self.db()
        db.execute("DELETE FROM plays WHERE at < ?", (now - PLAYS_DAYS * 86400,))
        db.execute("INSERT INTO plays (account_id, at, ip, fp) VALUES (?, ?, ?, ?)", (account, now, ip, fp))

    def remember(self, ip: str, report: dict):
        with self.machine_lock:
            now = time.time()
            self.recent_machines[ip] = (now, report)
            if len(self.recent_machines) > 5000:
                self.recent_machines = {k: v for k, v in self.recent_machines.items()
                                        if now - v[0] < RECENT_MACHINE_SECONDS}

    def recent(self, ip: str):
        """The machine last reported from this address, if within RECENT_MACHINE_SECONDS."""
        with self.machine_lock:
            seen = self.recent_machines.get(ip)
        return seen[1] if seen and time.time() - seen[0] < RECENT_MACHINE_SECONDS else None

    def device(self, account: int, report: dict, ip: str):
        self.db().execute("INSERT OR REPLACE INTO devices VALUES (?, ?, ?, ?, ?)",
                          (account, report["fp"], json.dumps(report, separators=(",", ":")), ip, report["at"]))

    def account(self, account: int, token: str) -> dict:
        db = self.db()
        email, name, created, role = db.execute("SELECT email, name, created, role FROM accounts WHERE id = ?",
                                                (account,)).fetchone()
        mine = token_hash(token)
        sessions = [{"id": row[0][:16], "current": row[0] == mine, "created": row[1], "last": row[2],
                     "ip": short_ip(row[3]), "agent": agent_name(row[4])}
                    for row in db.execute("SELECT token_hash, created, last_used, ip, agent FROM sessions "
                                          "WHERE account_id = ? AND expires >= ? ORDER BY last_used DESC",
                                          (account, int(time.time())))]
        devices = []
        for report, ip, seen in db.execute("SELECT report, ip, seen FROM devices WHERE account_id = ? "
                                           "ORDER BY seen DESC LIMIT 10", (account,)):
            row = json.loads(report)
            devices.append({key: row.get(key) for key in ("gpu", "driver", "date", "cpu", "ram", "os", "graphics")}
                           | {"ip": short_ip(ip), "seen": seen})
        plays = []
        for at, ip, report in db.execute(
                "SELECT p.at, p.ip, d.report FROM plays p LEFT JOIN devices d "
                "ON d.account_id = p.account_id AND d.fp = p.fp AND p.fp != '' "
                "WHERE p.account_id = ? ORDER BY p.at DESC LIMIT 20", (account,)):
            row = json.loads(report) if report else {}
            plays.append({"at": at, "ip": short_ip(ip)} |
                         {key: row.get(key) for key in ("gpu", "os", "graphics") if row.get(key)})
        return {"email": email, "name": name or "", "created": created, "role": role, "sessions": sessions,
                "devices": devices, "plays": plays}

    def comments(self, post: str) -> list:
        return [{"name": name, "text": text, "at": at} for name, text, at in self.db().execute(
            "SELECT a.name, c.text, c.at FROM comments c JOIN accounts a ON a.id = c.account_id "
            "WHERE c.post = ? ORDER BY c.at LIMIT 200", (post,))]

    def comment(self, post: str, account: int, text: str):
        self.db().execute("INSERT INTO comments (post, account_id, text, at) VALUES (?, ?, ?, ?)",
                          (post, account, text, int(time.time())))

    def session(self, token: str, ip: str = "", agent: str = ""):
        """The account a live token belongs to, as (id, email, name, role), or None. The sign-in learns
        where and with what it was last used: those made before 2026-10-15 kept neither, and a
        launcher says itself on every start (/api/me, /api/ticket)."""
        if not re.fullmatch(r"[0-9a-f]{64}", token or ""):
            return None
        now = int(time.time())
        row = self.db().execute(
            "SELECT a.id, a.email, a.name, a.role FROM sessions s JOIN accounts a ON a.id = s.account_id "
            "WHERE s.token_hash = ? AND s.expires >= ?", (token_hash(token), now)).fetchone()
        if row is not None:
            if agent:
                self.db().execute("UPDATE sessions SET last_used = ?, ip = ?, agent = ? WHERE token_hash = ?",
                                  (now, ip, agent[:200], token_hash(token)))
            else:
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

    def set_role(self, email: str, role: str) -> bool:
        return self.db().execute("UPDATE accounts SET role = ? WHERE email = ?", (role, email)).rowcount > 0

    def told_online(self, players: list):
        """mu2_server's list, each player's since carried over from the last list he was on, and
        each one with an account written down as his play (`games`)."""
        now = int(time.time())
        db = self.db()
        with self.online_lock:
            before = {(p.get("account"), p.get("name")): p.get("since", now) for p in self.online["players"]}
            db.execute("BEGIN")
            try:
                for p in players:
                    key = (p.get("account"), p.get("name"))
                    account = p.get("account") if isinstance(p.get("account"), int) else 0
                    if key not in before and account > 0 and not p.get("bot"):
                        # The website restarted under him: the play he was on goes on.
                        row = db.execute("SELECT max(since) FROM games WHERE account_id = ? AND name = ? AND until >= ?",
                                         (account, str(p.get("name")), now - RECENT_GAP)).fetchone()
                        if row and row[0]:
                            before[key] = row[0]
                    p["since"] = before.get(key, now)
                    if account > 0 and not p.get("bot"):
                        db.execute("INSERT INTO games VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT DO UPDATE SET "
                                   "class = excluded.class, level = excluded.level, world = excluded.world, "
                                   "until = excluded.until",
                                   (account, str(p.get("name"))[:32], str(p.get("class"))[:32],
                                    int(p.get("level") or 0), str(p.get("world"))[:32], p["since"], now))
                if now - self.games_swept > 3600:
                    db.execute("DELETE FROM games WHERE until < ?", (now - RECENT_DAYS * 86400,))
                    self.games_swept = now
                db.execute("COMMIT")
            except (sqlite3.Error, ValueError, TypeError):
                db.execute("ROLLBACK")
                raise
            self.online = {"at": now, "players": players}

    def told_characters(self, characters: list):
        with self.online_lock:
            self.characters = characters

    def everyone(self) -> list:
        """Each account with a character: its characters, newest played first, and its Plays."""
        with self.online_lock:
            characters = list(self.characters)
        by = {}
        for c in characters:
            if isinstance(c.get("account"), int) and c["account"] > 0:
                by.setdefault(c["account"], []).append(c)
        if not by:
            return []
        ids = sorted(by)
        marks = ",".join("?" * len(ids))
        db = self.db()
        accounts = {row[0]: row[1:] for row in db.execute(
            f"SELECT id, email, name, created FROM accounts WHERE id IN ({marks})", ids)}
        plays = {row[0]: row[1:] for row in db.execute(
            f"SELECT account_id, count(*), max(at) FROM plays WHERE account_id IN ({marks}) GROUP BY account_id", ids)}
        out = []
        for account, chars in by.items():
            if account not in accounts:
                continue  # an account deleted since: its characters go with the next list
            email, name, created = accounts[account]
            count, last_play = plays.get(account, (0, 0))
            chars.sort(key=lambda c: -int(c.get("saved") or 0))
            played = [int(c.get("saved") or 0) for c in chars if not c.get("fresh")]
            out.append({"account": account, "email": email, "account_name": name or "", "created": created,
                        "plays": count, "last_play": last_play or 0, "last": max(played + [last_play or 0]),
                        "characters": [{key: c.get(key) for key in ("name", "class", "level", "world", "saved", "fresh", "zen")}
                                       for c in chars]})
        out.sort(key=lambda a: -a["last"])
        return out

    def character(self, account: int, name: str):
        with self.online_lock:
            for c in self.characters:
                if c.get("account") == account and str(c.get("name", "")).lower() == name.lower():
                    return c
        return None

    def online_report(self) -> dict:
        with self.online_lock:
            told = self.online
        ids = sorted({p["account"] for p in told["players"] if p.get("account")})
        accounts = {}
        if ids:
            marks = ",".join("?" * len(ids))
            accounts = {row[0]: (row[1], row[2] or "") for row in self.db().execute(
                f"SELECT id, email, name FROM accounts WHERE id IN ({marks})", ids)}
        players = []
        for p in told["players"]:
            email, name = accounts.get(p.get("account"), ("", ""))
            players.append(p | {"email": email, "account_name": name})
        players.sort(key=lambda p: (p.get("bot", False), str(p.get("world")), -int(p.get("level") or 0)))
        on = {(p.get("account"), p.get("name"), p.get("since")) for p in told["players"]}
        recent = [{"account": account, "email": email, "account_name": name or "", "name": who, "class": kind,
                   "level": level, "world": world, "since": since, "until": until,
                   "online": (account, who, since) in on}
                  for account, email, name, who, kind, level, world, since, until in self.db().execute(
                      "SELECT g.account_id, a.email, a.name, g.name, g.class, g.level, g.world, g.since, g.until "
                      "FROM games g JOIN accounts a ON a.id = g.account_id WHERE g.until >= ? "
                      "ORDER BY g.until DESC LIMIT ?", (int(time.time()) - RECENT_DAYS * 86400, RECENT_SHOWN))]
        # Each account's machine: the one its launcher reported last (the account page's own rows).
        machines = {}
        everyone = self.everyone()
        ids = sorted({p["account"] for p in players + recent + everyone
                      if isinstance(p.get("account"), int) and p["account"] > 0})
        if ids:
            marks = ",".join("?" * len(ids))
            for account, report in self.db().execute(
                    f"SELECT account_id, report FROM devices WHERE account_id IN ({marks}) ORDER BY seen", ids):
                row = json.loads(report)
                machines[str(account)] = {key: row.get(key) for key in
                                          ("gpu", "vendor", "driver", "date", "cpu", "ram", "os", "graphics", "renderer")}
        return {"at": told["at"], "fresh": time.time() - told["at"] < ONLINE_STALE_SECONDS, "players": players,
                "recent": recent, "machines": machines, "everyone": everyone}

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
            if row.get("kind") == "log":
                continue  # a log digest died nowhere
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
# Comments, per account: a conversation, not a flood.
COMMENT_LIMIT = Limit(10, 600)


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

    def who(self):
        """The account the request's token is, the sign-in told where and with what it came."""
        return self.store.session(self.bearer(), self.client(), self.headers.get("User-Agent", ""))

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
        if path == "/api/name":
            return self.name()
        if path == "/api/password":
            return self.password()
        if path == "/api/sessions/end":
            return self.end_session()
        if path == "/api/delete":
            return self.delete()
        if path == "/api/comments":
            return self.comment()
        if path == "/internal/online":
            return self.told_online()
        if path == "/internal/role":
            return self.role()
        self.reply(404, {"error": "not found"})

    def register(self):
        said = self.body()
        if said is None:
            return self.reply(400, {"error": "bad request"})
        email = str(said.get("email", "")).strip().lower()
        password = str(said.get("password", ""))
        # The name is asked by the page since comments came (2026-10-15); an older page sends none.
        name = str(said.get("name", "")).strip()
        if not EMAIL.match(email) or len(email) > 254:
            return self.reply(400, {"field": "email", "error": "Enter a valid email address."})
        if name and not NAME.match(name):
            return self.reply(400, {"field": "name", "error": "3 to 16 letters, digits or _."})
        if len(password) < 8:
            return self.reply(400, {"field": "password", "error": "Use at least 8 characters."})
        if len(password) > 128:
            return self.reply(400, {"field": "password", "error": "Use at most 128 characters."})
        if not REGISTER_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many accounts from here. Try again later."})
        taken = self.store.register(email, password, name)
        if taken == "name":
            return self.reply(409, {"field": "name", "error": "That name is taken."})
        if taken:
            return self.reply(409, {"field": "email", "error": "taken"})
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
        signed = (self.store.login(email, password, self.client(), self.headers.get("User-Agent", ""))
                  if email and password else None)
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
        self.store.remember(self.client(), report)
        # A launcher that says its token files the machine under the account too, for the
        # account page; the fleet's file above still holds nothing about who.
        signed = self.who() if self.bearer() else None
        if signed:
            self.store.device(signed[0], report, self.client())
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
        # log-<when>.txt is the launcher's digest of its own log and the game's, not a crash.
        row["kind"] = "log" if name.startswith("log-") else "hang" if "hang-" in name else "crash"
        lines = text.decode("utf-8", "replace").splitlines()
        row["what"] = (lines[0] if lines else "")[:200]
        # Windows names the module the fault is in on a line of its own (core/watch_win.cpp).
        row["where"] = next((line[3:].strip()[:120] for line in lines[:4] if line.startswith("in ")), "")
        for line in lines[:12]:
            if line.startswith("MU2 "):
                row["build"] = line[4:].strip()[:64]
            elif " gpu " in line and " on " in line:
                row["renderer"] = line.strip()[:120]
        signed = self.who() if self.bearer() else None
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

    def told_online(self):
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = self.body(MOST_ONLINE_BODY)
        players = said.get("players") if said else None
        if not isinstance(players, list):
            return self.reply(400, {"error": "bad request"})
        self.store.told_online([p for p in players if isinstance(p, dict)])
        if isinstance(said.get("characters"), list):
            self.store.told_characters([c for c in said["characters"] if isinstance(c, dict)])
        self.reply(200, {"ok": True})

    def role(self):
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = self.body() or {}
        email, role = str(said.get("email", "")).strip().lower(), str(said.get("role", ""))
        if role not in ROLES:
            return self.reply(400, {"error": f"role is one of {', '.join(ROLES)}"})
        if not self.store.set_role(email, role):
            return self.reply(404, {"error": "no such account"})
        print(f"account {email} is now {role}", flush=True)
        self.reply(200, {"ok": True})

    def ticket(self):
        if self.old_launcher():
            return self.reply(426, {"error": OLD_LAUNCHER})
        if not TICKET_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many starts. Wait a minute."})
        row = self.who()
        if row is None:
            return self.reply(401, {"error": "signed out"})
        ticket, secret = self.store.ticket(row[0])
        # The Play's machine: the one this address reported as its launcher opened.
        machine = self.store.recent(self.client())
        if machine:
            self.store.device(row[0], machine, self.client())
        self.store.played(row[0], self.client(), machine["fp"] if machine else "")
        self.reply(200, {"ticket": ticket, "secret": secret})

    # -- the account page

    def signed(self):
        """The account the request's token is, or None with 401 already answered."""
        row = self.who()
        if row is None:
            self.reply(401, {"error": "signed out"})
        return row

    def name(self):
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        name = str(said.get("name", "")).strip()
        if not NAME.match(name):
            return self.reply(400, {"field": "name", "error": "3 to 16 letters, digits or _."})
        if not self.store.set_name(row[0], name):
            return self.reply(409, {"field": "name", "error": "That name is taken."})
        self.reply(200, {"ok": True})

    def password(self):
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        old, new = str(said.get("old", "")), str(said.get("new", ""))
        if not LOGIN_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many tries. Wait a minute."})
        if not self.store.check(row[0], old):
            return self.reply(400, {"field": "old", "error": "That is not your password."})
        if not 8 <= len(new) <= 128:
            return self.reply(400, {"field": "new", "error": "Use 8 to 128 characters."})
        self.store.set_password(row[0], new, self.bearer())
        print(f"{self.client()} account {row[0]} changed its password", flush=True)
        self.reply(200, {"ok": True})

    def end_session(self):
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        if not self.store.end_session(row[0], str(said.get("id", ""))):
            return self.reply(404, {"error": "no such sign-in"})
        self.reply(200, {"ok": True})

    def delete(self):
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        if not LOGIN_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "Too many tries. Wait a minute."})
        if not self.store.check(row[0], str(said.get("password", ""))):
            return self.reply(400, {"field": "password", "error": "That is not your password."})
        self.store.delete(row[0])
        print(f"{self.client()} account {row[0]} deleted itself", flush=True)
        self.reply(200, {"ok": True})

    def posts(self) -> set:
        try:
            return {str(post.get("id")) for post in json.loads((self.site / "posts.json").read_text())}
        except (OSError, ValueError, AttributeError):
            return set()

    def comment(self):
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        post, text = str(said.get("post", "")), str(said.get("text", "")).strip()
        if post not in self.posts():
            return self.reply(400, {"error": "no such post"})
        if not text or len(text) > MOST_COMMENT:
            return self.reply(400, {"error": f"Write 1 to {MOST_COMMENT} characters."})
        if not row[2]:
            return self.reply(400, {"error": "Pick a name first."})
        if not COMMENT_LIMIT.allow(str(row[0])):
            return self.reply(429, {"error": "Slow down a little."})
        self.store.comment(post, row[0], text)
        self.reply(201, {"ok": True})

    # -- GET

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/api/me":
            if self.old_launcher():
                return self.reply(401, {"error": OLD_LAUNCHER})
            row = self.who()
            return self.reply(200, {"email": row[1], "role": row[3]}) if row else self.reply(401, {"error": "signed out"})
        if url.path == "/api/downloads":
            return self.reply(200, self.listing())
        if url.path == "/api/account":
            row = self.signed()
            return self.reply(200, self.store.account(row[0], self.bearer())) if row else None
        if url.path == "/api/online":
            # Anyone not an admin is told there is nothing here, as for a page that does not exist.
            row = self.who()
            if row is None or row[3] != "admin":
                return self.reply(404, {"error": "not found"})
            return self.reply(200, self.store.online_report())
        if url.path == "/api/character":
            row = self.who()
            if row is None or row[3] != "admin":
                return self.reply(404, {"error": "not found"})
            asked = parse_qs(url.query)
            try:
                account = int(asked.get("account", ["0"])[0])
            except ValueError:
                account = 0
            found = self.store.character(account, asked.get("name", [""])[0])
            return self.reply(200, found) if found else self.reply(404, {"error": "not listed"})
        if url.path == "/api/comments":
            post = parse_qs(url.query).get("post", [""])[0]
            if post not in self.posts():
                return self.reply(404, {"error": "no such post"})
            return self.reply(200, {"comments": self.store.comments(post)})
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
        elif path in ("/account", "/account/"):
            path = "/account.html"
        elif path in ("/online", "/online/"):
            path = "/online.html"
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
