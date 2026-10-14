#!/usr/bin/env python3
"""MU2's website and account service: the one-page site, and the accounts the launcher signs in with.

Python's standard library only (the user, 2026-10-14: no packages on the box). It listens on
127.0.0.1 behind Caddy (web/Caddyfile), which holds HTTPS and passes /api/* and the page here.

    web/server.py --db out/web/accounts.db            serve on 127.0.0.1:8080
    web/server.py --db X --port 8090 --site web/site  elsewhere

The routes:
    POST /api/register   {email, password}          -> 201 {ok}            | 400 {field, error} | 409 {error: taken}
    POST /api/login      {email, password}          -> 200 {token, email}  | 401 {error}
    POST /api/logout     Authorization: Bearer T    -> 200 {ok}
    GET  /api/me         Authorization: Bearer T    -> 200 {email}         | 401
    GET  /api/downloads                             -> 200 {windows: {file, bytes} | null, macos: ...}
    GET  /internal/session?token=T  (127.0.0.1 only, never proxied) -> 200 {account} | 404
         for mu2_server to turn a launcher's token into the account its characters belong to.

A token is 64 hex digits, the shape the game's Account message already carries (net::kMostKey,
server/src/accounts.cpp keyShape). Only its SHA-256 is stored, so a copy of the database signs
nobody in. Passwords are scrypt (n=2^14, r=8, p=1), each with its own salt.
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
MOST_BODY = 4096
EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s]+\.[^@\s]+$")
SCRYPT = dict(n=2**14, r=8, p=1, maxmem=64 * 1024 * 1024, dklen=32)

# The two installers the page offers, by the names the dist step will give them; a file not
# there yet leaves its button saying so instead of linking nowhere.
DOWNLOADS = {"windows": "MU2Setup.exe", "macos": "MU2.dmg"}

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

    def logout(self, token: str):
        self.db().execute("DELETE FROM sessions WHERE token_hash = ?", (token_hash(token or ""),))


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

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MOST_BODY:
            return None
        try:
            data = json.loads(self.rfile.read(length))
            return data if isinstance(data, dict) else None
        except (ValueError, UnicodeDecodeError):
            return None

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
        email = str(said.get("email", "")).strip().lower()
        password = str(said.get("password", ""))
        signed = self.store.login(email, password) if email and password else None
        if signed is None:
            return self.reply(401, {"error": "Wrong email or password."})
        self.reply(200, {"token": signed[0], "email": signed[1]})

    # -- GET

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/api/me":
            row = self.store.session(self.bearer())
            return self.reply(200, {"email": row[1]}) if row else self.reply(401, {"error": "signed out"})
        if url.path == "/api/downloads":
            return self.reply(200, self.listing())
        if url.path == "/internal/session":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            row = self.store.session(parse_qs(url.query).get("token", [""])[0])
            return self.reply(200, {"account": row[0]}) if row else self.reply(404, {"error": "no session"})
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
