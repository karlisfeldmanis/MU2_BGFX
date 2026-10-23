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
    GET  /api/downloads                             -> 200 {windows: {file, bytes, sha256} | null, macos: ...}
    POST /api/bug        {text, platform?, build?, renderer?, gpu?, world?, x?, y?, character?, class?,
                          level?, server?}  [Bearer T]  -> 200 {ok, id} | 400 {error} | 413 | 429 {error: "later"}
         a player's bug report from the game: their words (1 to MOST_BUG_TEXT characters) and where
         they stood. A line in bugs.jsonl beside the database; a token, or the game's `ticket:secret`,
         files it under its account, as for /api/crash. BUG_LIMIT an address, MOST_BUGS_A_DAY in all.
    GET  /api/bugs       Bearer T of an admin       -> 200 {bugs: [...]} | 404 for anyone else
         every report, newest first, each with its state and its account's email and name (the /bugs page).
    POST /api/bugs/state Bearer T of an admin {id, state}  -> 200 {ok} | 400 | 404
         a report marked one of BUG_STATES; kept as a line of its own in bugs.jsonl.
    GET  /api/status                                -> 200 {regions: [{name, city, up}]}
         the front page's server status: each region (REGIONS) up while its mu2_server tells.
    GET  /api/online     Bearer T of an admin       -> 200 {at, fresh, players: [...]} | 404 for anyone else
         who is on the game server now, as mu2_server last told it (the admins' /online page), and
         `days`: a row a UTC day {day, registered, accounts, players, peak, peak_at} (days_report);
         `recent`: the last RECENT_SHOWN plays of the last RECENT_DAYS days, bots left out; and
         `machines`: by account id, the machine its launcher last reported (devices); and `everyone`:
         every account with a character, its characters as mu2_server last listed them (a minute
         apart), and its Plays (launcher starts) of the last PLAYS_DAYS days.
    GET  /api/character?account=N&name=X   Bearer T of an admin -> 200 {name, class, level, world, saved,
         second, experience, zen, points, worn, bag, played} | 404: one character's stats and gear, as mu2_server
         last listed it (a minute apart), and `played`, his seconds on (Store.played).
    POST /internal/online   (127.0.0.1 only) {region?, players: [{account, name, class, level, world, copy, x, y, bot, ms}],
                             characters?: [{account, name, class, level, world, saved, fresh}]}
         a region's mu2_server's list of who is on, every 5 s (server/src/signin.cpp tellOnline); kept in
         memory, one list a region (DEFAULT_REGION when it names none). `ms`: his line's round trip.
    POST /api/transfer   Bearer T {name, to, new_name?} | {id, new_name?} | {id, cancel: true}
                                                    -> 200 {ok} | 409 {field?, error}
         a character moved to another region's server (TRANSFER_STATES, docs/game/transfer.md): asked,
         a held one sent on with a new name or once a slot is free, or one still only asked called off.
         GET /api/account carries the account's `characters` on every region, its `transfers`, the `regions`
         and their `cities`.
    GET  /internal/transfers?region=R  (127.0.0.1 only) -> 200 text/plain, a line a move (transfers_for)
    POST /internal/transfers           (127.0.0.1 only) {region, id, state, why?, kept?}  -> 200 {ok} | 409
         a region's mu2_server's step of a move (server/src/transfer.cpp), every 15 s.
    POST /internal/role     (127.0.0.1 only) {email, role}  -> 200 {ok} | 404
         an account made "admin" or back to "player": on the box,
         curl -s -d '{"email":"X","role":"admin"}' http://127.0.0.1:8080/internal/role
    GET  /internal/ticket?ticket=X  (127.0.0.1 only, never proxied) -> 200 {account, secret} | 404
         for mu2_server to learn whose a game's ticket is and the secret its proofs are made with.
         Each ticket is told once, and forgotten here as it is told.
    GET  /internal/crashes          (127.0.0.1 only) -> 200 {reports, where, last}
         the crash reports counted by the module they died in, and the latest thirty.
    GET  /api/updates    Bearer T of an admin       -> 200 {entries, noted_through} | 404 for anyone else
    POST /api/updates    Bearer T of an admin {id?, day, kind, text, state} | {id, delete: true}
                                                    -> 200 {ok, id} | 400 {error} | 404
         the front page's Latest updates, kept here (the admins' /updates page): one line each, a day
         (the release that first carried it), New or Fixed, and draft until an admin publishes it.
         The published lines are drawn into index.html between <!--updates--> and <!--/updates-->
         as it is served; with none published, the page's own lines show.
    GET  /internal/updates          (127.0.0.1 only) -> 200 {entries, noted_through}
    POST /internal/updates          (127.0.0.1 only) {entries: [{id?, day, kind, text, commits, state?}],
                                     delete?: [id], noted_through?}  -> 200 {ok, added}
         `mu ship notes` (tools/mu/ship.py) and the Ship app (tools/ship/ship.cpp): drafts for the
         commits a release shipped, the last commit looked at, so the next run starts after it; and
         a line changed (one with an id) or deleted.
         Every day whose lines change is told in the Discord's #updates DISCORD_QUIET seconds after
         (class Discord), by the webhook URL in discord_webhook beside the database.
    POST /internal/discord          (127.0.0.1 only) {day}  -> 200 {ok} | 400 | 409 with no webhook
         that day's #updates message sent, edited or taken away now.
    GET  /internal/untranslated     (127.0.0.1 only)  -> 200 {langs, missing, examples}
    POST /internal/translations     (127.0.0.1 only) {texts: {lang: {source: text}}}  -> 200 {put, refused}
         the units of the site with no translation in a language, and translations of them put in
         where there is still none: `mu ship translate` (tools/mu/ship.py) translates them with
         claude -p on the Mac, and keeps doing so on a schedule.
    GET  /api/texts      Bearer T of an admin       -> 200 {langs, groups, texts} | 404 for anyone else
    POST /api/texts      Bearer T of an admin {lang, source, text}  -> 200 {ok} | 400 {error} | 404
         the site in other languages (the admins' /translate page, web/i18n.py): every English unit
         of the pages, the Latest updates and the scripts, and each language's translation of it.
         An empty text takes a translation away. The pages are served in each language under
         /<lang>/ (i18n.LANGS), a unit with no translation in English; web/i18n/<lang>.json are the
         first words of each, put in at start where a unit has none.

A token is 64 hex digits, and goes only over HTTPS. Only its SHA-256 is stored, so a copy of the
database signs nobody in. A ticket's secret is kept until the server is told it, then only its
SHA-256, for a day, which files the game's crash reports under the account. Passwords are scrypt
(n=2^14, r=8, p=1), each with its own salt.
"""

import argparse
import base64
import calendar
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

# The site's other languages (web/i18n.py), beside this file; the service runs with -I, which
# leaves this folder off the path.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import i18n  # noqa: E402

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
# Each game server is a region of the launcher's (src/launcher/engine.cpp regions()) and tells its own
# list, named by "region"; a server from before regions names none and is Helsinki's. A region silent
# this long is a server taken away, and its list is left off the page.
DEFAULT_REGION = "North Europe"
REGION_GONE_SECONDS = 600
# The regions the front page shows, up or not (/api/status): the launcher's list, by name, with the
# city of each one's server.
REGIONS = (("North Europe", "Helsinki"), ("Central Europe", "Nuremberg"), ("South America", "São Paulo"), ("East Asia", "Tokyo"))  # Southeast Asia (Singapore) closed 2026-10-22
# A character's move to another region (transfers, docs/game/transfer.md), one way through:
#   asked  the website took it; the server he leaves checks him and hands over his bytes -> out, or failed
#   out    the website holds his bytes; that server marks him gone from its disk -> gone
#   gone   the server he goes to takes him in -> done, or held (his name taken there, or no slot free)
#   held   waits for the player: a new name, or a slot freed -> gone again
# Once a character in TRANSFER_DAYS, one move an account at a time, free.
TRANSFER_STATES = ("asked", "out", "gone", "held", "done", "failed")
TRANSFER_MOVING = ("asked", "out", "gone", "held")
TRANSFER_DAYS = 7
ROSTER_SLOTS = 5           # sim::kRosterSlots
CHARACTER_NAME = re.compile(r"^[A-Za-z0-9]{4,10}$")  # sim::goodName
# A character's bytes as his server hands them over: the layout, the item list's print, his world and
# his bytes, the last two in hex (server/src/transfer.cpp give).
KEPT = re.compile(r"^\d{1,4} [0-9a-f]{16} (?:-|(?:[0-9a-f]{2}){1,64}) (?:[0-9a-f]{2}){1,65536}$")
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
# The Windows button gives MU2Setup.exe (src/launcher/mu2setup.iss).
DOWNLOADS = {"windows": "MU2Setup.exe", "macos": "MU2.dmg"}
# Each download's SHA-256 by (path, size, mtime): read once a release, not once an ask.
DOWNLOAD_SUMS = {}


def download_sum(f: Path) -> str:
    st = f.stat()
    key = (str(f), st.st_size, st.st_mtime_ns)
    if key not in DOWNLOAD_SUMS:
        digest = hashlib.sha256()
        with open(f, "rb") as data:
            for block in iter(lambda: data.read(1 << 20), b""):
                digest.update(block)
        DOWNLOAD_SUMS[key] = digest.hexdigest()
    return DOWNLOAD_SUMS[key]

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

# A bug report from the game: the player's words, and where and with what they played. Each field
# is cut to its length here and any other key is dropped; the numbers are kept only as whole numbers.
MOST_BUG_BODY = 16 * 1024
MOST_BUG_TEXT = 2000
MOST_BUGS_A_DAY = 1000
BUG_FIELDS = {"build": 64, "renderer": 120, "gpu": 160, "world": 32, "character": 32, "class": 32, "server": 64}
BUG_NUMBERS = ("x", "y", "level")
BUG_PLATFORMS = ("windows", "macos", "linux")
# What an admin marks a report: every one comes in "new".
BUG_STATES = ("new", "seen", "fixed", "wontfix")

# The Latest updates (the /updates page): a line is New or Fixed, and shows once published.
UPDATE_KINDS = ("new", "fixed")
UPDATE_STATES = ("draft", "published")
MOST_UPDATE_TEXT = 400
# The days the front page lists, newest first, all closed until the reader opens one.
UPDATE_DAYS_SHOWN = 10
UPDATE_DAY = re.compile(r"^\d{4}-\d{2}-\d{2}$")
MOST_UPDATES_BODY = 256 * 1024

# The Latest updates told in the Discord's #updates (class Discord): one message a release day,
# sent once its lines have been left alone DISCORD_QUIET seconds (an admin publishes them one by
# one), and edited, or taken away, as they change after. The channel's webhook URL is the file
# discord_webhook beside the database, read at each message: set or changed without a restart, and
# with none, nothing is told.
DISCORD_QUIET = 300
DISCORD_WEBHOOK = re.compile(r"^https://(?:discord|discordapp)\.com/api/webhooks/\d+/[\w-]+$"
                             r"|^http://127\.0\.0\.1:\d+/[\w/-]+$")
DISCORD_COLOUR = 0xC9A45C
DISCORD_MOST_TEXT = 4000
SITE_URL = "https://mu-classic.com/"

# The site's units with no translation in a language are translated on the Mac by `mu ship translate`
# (claude -p), through /internal/untranslated and /internal/translations: the Latest updates as they
# are published, and any new words of a page. What an admin wrote on /translate is never touched; a
# line the translator wrote (texts.by = TRANSLATOR) an admin can reword there like any other.
TRANSLATOR = 0             # texts.by of a line the translator wrote (accounts start at 1)
LANG_NAMES = {"pt": "Brazilian Portuguese", "es": "Spanish", "ru": "Russian", "vi": "Vietnamese", "pl": "Polish",
              "zh": "Simplified Chinese", "ms": "Malay (Malaysia)", "lv": "Latvian"}
EXAMPLES = 40              # a language's own lines sent with its missing ones, to keep its words

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
-- Each character's play before the oldest kept in `games`: its plays' seconds, added in as they are
-- swept (told_online), so a character's total (Store.played) outlives RECENT_DAYS.
CREATE TABLE IF NOT EXISTS played (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    seconds    INTEGER NOT NULL,
    PRIMARY KEY (account_id, name)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS tickets (
    ticket      TEXT PRIMARY KEY,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    secret      TEXT NOT NULL,      -- '' once the server has been told it
    secret_hash TEXT NOT NULL,
    created     INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS updates (
    id       INTEGER PRIMARY KEY,
    day      TEXT NOT NULL,         -- YYYY-MM-DD: the release that first carried it
    kind     TEXT NOT NULL,         -- UPDATE_KINDS
    text     TEXT NOT NULL,
    state    TEXT NOT NULL,         -- UPDATE_STATES
    commits  TEXT NOT NULL DEFAULT '',  -- the short hashes it tells of, space between
    position INTEGER NOT NULL DEFAULT 0, -- its place among its day's lines of its kind
    created  INTEGER NOT NULL,
    changed  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS updates_day ON updates(day);
CREATE TABLE IF NOT EXISTS texts (
    lang     TEXT NOT NULL,         -- i18n.LANGS
    source   TEXT NOT NULL,         -- the English unit, i18n.key()
    text     TEXT NOT NULL,
    changed  INTEGER NOT NULL,
    by       INTEGER,               -- the admin's account; NULL from web/i18n/<lang>.json
    PRIMARY KEY (lang, source)
);
-- A character moved from one region's game server to another's (docs/game/transfer.md): asked
-- here, his bytes handed over by the server he leaves, taken in by the one he goes to.
CREATE TABLE IF NOT EXISTS transfers (
    id          INTEGER PRIMARY KEY,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,      -- as he is on the server he leaves
    new_name    TEXT NOT NULL,      -- as he will be on the one he goes to
    from_region TEXT NOT NULL,
    to_region   TEXT NOT NULL,
    state       TEXT NOT NULL,      -- TRANSFER_STATES
    why         TEXT NOT NULL DEFAULT '',  -- what holds or ended it, said to the player
    kept        TEXT NOT NULL DEFAULT '',  -- "layout items hex" while he is between servers
    asked       INTEGER NOT NULL,
    changed     INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS transfers_account ON transfers(account_id, changed);
-- Each UTC day's players, kept for good for the /online page's graphs (told_online): the accounts
-- that played that day, and the most people on at once across every region. No foreign key: a
-- deleted account still played that day.
CREATE TABLE IF NOT EXISTS days (
    day     TEXT PRIMARY KEY,       -- YYYY-MM-DD, UTC
    peak    INTEGER NOT NULL,
    peak_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS day_players (
    day        TEXT NOT NULL,
    account_id INTEGER NOT NULL,
    PRIMARY KEY (day, account_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
"""


# ---- passwords and tokens ------------------------------------------------------------------------

def utc_day(at: float) -> str:
    return time.strftime("%Y-%m-%d", time.gmtime(at))


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
        # Who is on each region's game server, as its mu2_server last told it (told_online): region ->
        # {at, players}, each player carrying his region; never on disk.
        self.online_lock = threading.Lock()
        self.online = {}
        self.games_swept = 0
        # Every character of an account, as each region's mu2_server last listed it (told_characters):
        # region -> [character], each carrying its region. Kept in settings too (roster:<region>, the
        # fields an account page shows), so a restart does not leave every account page empty until
        # each region's next list, a minute on (the user, 2026-10-23: "dont see characters").
        self.characters = {}
        # The bug reports (bug()), in bugs.jsonl beside the database: a line for each report, and a
        # line {id, state, at} for each time an admin marks one, folded onto it as the file is read.
        # Kept in memory too, by id: a few thousand short lines at most.
        self.bugs_path = path.parent / "bugs.jsonl"
        self.bug_lock = threading.Lock()
        self.bugs = {}
        self.bug_days = {}
        if self.bugs_path.is_file():
            with self.bugs_path.open() as f:
                for line in f:
                    try:
                        row = json.loads(line)
                    except ValueError:
                        continue
                    if not isinstance(row, dict) or not isinstance(row.get("id"), int):
                        continue
                    if "text" in row:
                        self.bugs[row["id"]] = row
                        day = time.strftime("%Y-%m-%d", time.gmtime(row.get("at", 0)))
                        self.bug_days[day] = self.bug_days.get(day, 0) + 1
                    elif row["id"] in self.bugs and row.get("state") in BUG_STATES:
                        self.bugs[row["id"]] |= {"state": row["state"], "state_at": row.get("at", 0)}
        self.crash_days = {}
        for row in self.crash_rows():
            self.crash_seen.add(row.get("sha"))
            day = str(row.get("day", ""))
            self.crash_days[day] = self.crash_days.get(day, 0) + 1
        # Counted up at each translation saved: the pages served in other languages are kept until it moves.
        self.texts_version = 0
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
            if db.execute("SELECT count(*) FROM days").fetchone()[0] == 0:
                self.backfill_days(db)

    @staticmethod
    def backfill_days(db: sqlite3.Connection):
        """The days before `days` was kept, from the plays `games` still holds (RECENT_DAYS): each
        day's accounts, and its peak as the most plays that overlapped."""
        rows = db.execute("SELECT account_id, since, until FROM games").fetchall()
        steps = []
        for account, since, until in rows:
            at = since - since % 86400
            while at <= until:
                db.execute("INSERT OR IGNORE INTO day_players VALUES (?, ?)", (utc_day(at), account))
                at += 86400
            steps += [(since, 1), (until + 1, -1)]
        on, peaks = 0, {}
        for at, step in sorted(steps):
            on += step
            day = utc_day(at)
            if step > 0 and on > peaks.get(day, (0, 0))[0]:
                peaks[day] = (on, at)
        for day, (peak, at) in peaks.items():
            db.execute("INSERT INTO days VALUES (?, ?, ?)", (day, peak, at))

    def close(self):
        """This thread's connection closed: a request's thread ends when its connection does, and an
        unclosed one holds the database, its -wal and -shm until the collector comes, which ran
        the service out of files (1024) in twelve busy minutes on 2026-10-23."""
        db = getattr(self.local, "db", None)
        if db is not None:
            self.local.db = None
            db.close()

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

    def machine_at(self, account: int, ip: str):
        """The account's machine last seen from this address, when it is the only one there: a Play
        after a restart of this service, or more than RECENT_MACHINE_SECONDS after the launcher opened."""
        rows = self.db().execute("SELECT report FROM devices WHERE account_id = ? AND ip = ? LIMIT 2",
                                 (account, ip)).fetchall()
        return json.loads(rows[0][0]) if len(rows) == 1 else None

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
            row = json.loads(report) if report else self.machine_at(account, ip) or {}
            plays.append({"at": at, "ip": short_ip(ip)} |
                         {key: row.get(key) for key in ("gpu", "os", "graphics") if row.get(key)})
        return {"email": email, "name": name or "", "created": created, "role": role, "sessions": sessions,
                "devices": devices, "plays": plays, "characters": self.mine(account),
                "transfers": self.transfers(account), "regions": [r for r, _ in REGIONS],
                "cities": dict(REGIONS)}

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

    # -- the Latest updates

    def updates(self, published_only: bool = False) -> list:
        """Every line, newest day first, then New before Fixed, then their order."""
        rows = self.db().execute(
            "SELECT id, day, kind, text, state, commits, position, created, changed FROM updates"
            + (" WHERE state = 'published'" if published_only else "")
            + " ORDER BY day DESC, kind = 'fixed', position, id").fetchall()
        keys = ("id", "day", "kind", "text", "state", "commits", "position", "created", "changed")
        return [dict(zip(keys, row)) | {"commits": row[5].split()} for row in rows]

    def noted_through(self) -> str:
        row = self.db().execute("SELECT value FROM settings WHERE key = 'noted_through'").fetchone()
        return row[0] if row else ""

    def save_update(self, line: dict):
        """A line put in, or changed when it carries an id; its id, or None when there is no such id."""
        db, now = self.db(), int(time.time())
        if line.get("id") is None:
            position = line.get("position")
            if position is None:
                last = db.execute("SELECT MAX(position) FROM updates WHERE day = ? AND kind = ?",
                                  (line["day"], line["kind"])).fetchone()[0]
                position = (last or 0) + 1
            return db.execute(
                "INSERT INTO updates (day, kind, text, state, commits, position, created, changed) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (line["day"], line["kind"], line["text"], line["state"], " ".join(line.get("commits", [])),
                 position, now, now)).lastrowid
        done = db.execute("UPDATE updates SET day = ?, kind = ?, text = ?, state = ?, changed = ?"
                          + (", position = ?" if line.get("position") is not None else "") + " WHERE id = ?",
                          (line["day"], line["kind"], line["text"], line["state"], now)
                          + ((line["position"],) if line.get("position") is not None else ()) + (line["id"],))
        return line["id"] if done.rowcount else None

    # -- the site's other languages

    def texts(self, lang: str = None) -> dict:
        """{lang: {source: text}}, or one language's {source: text}."""
        rows = self.db().execute("SELECT lang, source, text FROM texts" + (" WHERE lang = ?" if lang else ""),
                                 (lang,) if lang else ()).fetchall()
        if lang:
            return {row[1]: row[2] for row in rows}
        out = {code: {} for code in i18n.LANGS}
        for code, source, text in rows:
            out.setdefault(code, {})[source] = text
        return out

    def save_text(self, lang: str, source: str, text: str, account: int):
        db = self.db()
        if text:
            db.execute("INSERT INTO texts (lang, source, text, changed, by) VALUES (?, ?, ?, ?, ?) "
                       "ON CONFLICT(lang, source) DO UPDATE SET text = excluded.text, changed = excluded.changed, "
                       "by = excluded.by", (lang, source, text, int(time.time()), account))
        else:
            db.execute("DELETE FROM texts WHERE lang = ? AND source = ?", (lang, source))
        self.texts_version += 1

    def fill_text(self, lang: str, source: str, text: str) -> bool:
        """A translation where there is none yet (the translator's): an admin's, or one put in
        meanwhile, stands."""
        put = self.db().execute("INSERT OR IGNORE INTO texts (lang, source, text, changed, by) VALUES (?, ?, ?, ?, ?)",
                                (lang, source, text, int(time.time()), TRANSLATOR)).rowcount > 0
        if put:
            self.texts_version += 1
        return put

    def seed_texts(self, seeds: dict) -> int:
        """web/i18n/<lang>.json put in wherever a unit has no translation yet; an admin's word stands."""
        db, now, added = self.db(), int(time.time()), 0
        for lang, table in seeds.items():
            for source, text in table.items():
                added += db.execute("INSERT OR IGNORE INTO texts (lang, source, text, changed) VALUES (?, ?, ?, ?)",
                                    (lang, source, text, now)).rowcount
        self.texts_version += 1
        return added

    def delete_update(self, line: int) -> bool:
        return self.db().execute("DELETE FROM updates WHERE id = ?", (line,)).rowcount > 0

    def update_day(self, line) -> str:
        """The day a line is under now, '' when there is no such line."""
        if line is None:
            return ""
        row = self.db().execute("SELECT day FROM updates WHERE id = ?", (line,)).fetchone()
        return row[0] if row else ""

    def setting(self, key: str) -> str:
        row = self.db().execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
        return row[0] if row else ""

    def set_setting(self, key: str, value: str):
        if value:
            self.db().execute("INSERT INTO settings (key, value) VALUES (?, ?) "
                              "ON CONFLICT(key) DO UPDATE SET value = excluded.value", (key, value))
        else:
            self.db().execute("DELETE FROM settings WHERE key = ?", (key,))

    def add_updates(self, lines: list, noted_through: str) -> int:
        """`mu ship notes`' lines, all at once with where it got to."""
        db = self.db()
        db.execute("BEGIN IMMEDIATE")
        try:
            ids = [self.save_update(line) for line in lines]
            if noted_through:
                db.execute("INSERT INTO settings (key, value) VALUES ('noted_through', ?) "
                           "ON CONFLICT(key) DO UPDATE SET value = excluded.value", (noted_through,))
            db.execute("COMMIT")
        except Exception:
            db.execute("ROLLBACK")
            raise
        return len(ids)

    def told_online(self, players: list, region: str = DEFAULT_REGION):
        """A region's mu2_server's list, each player's since carried over from the last list he was
        on (in any region), and each one with an account written down as his play (`games`)."""
        now = int(time.time())
        db = self.db()
        for p in players:
            p["region"] = region
        with self.online_lock:
            before = {(p.get("account"), p.get("name")): p.get("since", now)
                      for told in self.online.values() for p in told["players"]}
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
                # The day's players, and its peak: the people on every region still telling, this list in.
                day = utc_day(now)
                for p in players:
                    if isinstance(p.get("account"), int) and p["account"] > 0 and not p.get("bot"):
                        db.execute("INSERT OR IGNORE INTO day_players VALUES (?, ?)", (day, p["account"]))
                people = sum(1 for p in players if not p.get("bot")) + sum(
                    1 for other, told in self.online.items() if other != region
                    and now - told["at"] < ONLINE_STALE_SECONDS for p in told["players"] if not p.get("bot"))
                db.execute("INSERT INTO days VALUES (?, ?, ?) ON CONFLICT DO UPDATE SET peak = excluded.peak, "
                           "peak_at = excluded.peak_at WHERE excluded.peak > days.peak", (day, people, now))
                if now - self.games_swept > 3600:
                    old = now - RECENT_DAYS * 86400
                    db.execute("INSERT INTO played SELECT account_id, name, sum(until - since) FROM games WHERE until < ? "
                               "GROUP BY account_id, name ON CONFLICT DO UPDATE SET seconds = seconds + excluded.seconds", (old,))
                    db.execute("DELETE FROM games WHERE until < ?", (old,))
                    self.games_swept = now
                db.execute("COMMIT")
            except (sqlite3.Error, ValueError, TypeError):
                db.execute("ROLLBACK")
                raise
            self.online[region] = {"at": now, "players": players}

    ROSTER_KEEPS = ("account", "name", "class", "level", "world", "fresh", "region", "saved", "zen")  # not the gear

    def told_characters(self, characters: list, region: str = DEFAULT_REGION):
        for c in characters:
            c["region"] = region
        with self.online_lock:
            self.characters[region] = characters
        self.set_setting(f"roster:{region}", json.dumps([{k: c.get(k) for k in self.ROSTER_KEEPS} for c in characters]))

    def played(self, account: int, name: str) -> int:
        """A character's seconds on any server, every play of his the website has seen (since 2026-10-16)."""
        db = self.db()
        swept = db.execute("SELECT seconds FROM played WHERE account_id = ? AND name = ?", (account, name)).fetchone()
        kept = db.execute("SELECT sum(until - since) FROM games WHERE account_id = ? AND name = ?", (account, name)).fetchone()
        return (swept[0] if swept else 0) + (kept[0] or 0)

    def load_rosters(self):
        """The regions' last lists, as kept, until each region lists them again."""
        rows = self.db().execute("SELECT key, value FROM settings WHERE key LIKE 'roster:%'").fetchall()
        with self.online_lock:
            for key, value in rows:
                self.characters.setdefault(key[len("roster:"):], json.loads(value))
        return sum(len(json.loads(v)) for _, v in rows)

    def regions_told(self) -> dict:
        """The regions whose servers still tell (REGION_GONE_SECONDS): region -> {at, players}."""
        now = time.time()
        with self.online_lock:
            return {region: told for region, told in self.online.items() if now - told["at"] < REGION_GONE_SECONDS}

    def status(self) -> dict:
        """The front page's line for each region: up while its server tells (ONLINE_STALE_SECONDS).
        Nothing about who, nor how many (the user, 2026-10-17: "just status")."""
        now = time.time()
        with self.online_lock:
            told = dict(self.online)
        out = []
        for name, city in REGIONS:
            r = told.get(name)
            up = r is not None and now - r["at"] < ONLINE_STALE_SECONDS
            out.append({"name": name, "city": city, "up": up})
        return {"regions": out}

    def all_characters(self) -> list:
        with self.online_lock:
            return [c for region in self.characters.values() for c in region]

    def everyone(self) -> list:
        """Each account with a character: its characters, newest played first, and its Plays."""
        characters = self.all_characters()
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
                        "characters": [{key: c.get(key) for key in ("name", "class", "level", "world", "saved", "fresh", "zen", "region")}
                                       for c in chars]})
        out.sort(key=lambda a: -a["last"])
        return out

    def character(self, account: int, name: str):
        for c in self.all_characters():
            if c.get("account") == account and str(c.get("name", "")).lower() == name.lower():
                return c
        return None

    # -- moving a character to another region (transfers)

    def transfers(self, account: int) -> list:
        return [dict(zip(("id", "name", "new_name", "from", "to", "state", "why", "asked", "changed"), row))
                for row in self.db().execute(
                    "SELECT id, name, new_name, from_region, to_region, state, why, asked, changed FROM transfers "
                    "WHERE account_id = ? ORDER BY id DESC LIMIT 10", (account,))]

    def mine(self, account: int) -> list:
        """The account's characters on each region's server, as each last listed them."""
        with self.online_lock:
            listed = [c for told in self.characters.values() for c in told if c.get("account") == account]
            playing = {(p.get("region"), p.get("name")) for told in self.online.values() for p in told["players"]
                       if p.get("account") == account}
        return [{key: c.get(key) for key in ("name", "class", "level", "world", "region", "fresh")} |
                {"online": (c.get("region"), c.get("name")) in playing} for c in listed]

    def taken_in(self, region: str, name: str) -> bool:
        with self.online_lock:
            return any(str(c.get("name", "")).lower() == name.lower() for c in self.characters.get(region, []))

    def ask_transfer(self, account: int, name: str, to: str, new_name: str):
        """A move asked: None, or (field, error) when it cannot be."""
        now = int(time.time())
        his = next((c for c in self.mine(account) if c["name"] == name), None)
        if his is None:
            return "name", "No such character."
        if his["fresh"]:
            return "name", "Play him once before he moves."
        if his["online"]:
            return "name", "Log out of the game first."
        if to not in [r for r, _ in REGIONS] or to == his["region"]:
            return "to", "Pick another region."
        if to not in self.regions_told():
            return "to", "That region's server is down. Try again later."
        db = self.db()
        if db.execute("SELECT 1 FROM transfers WHERE account_id = ? AND state IN (?, ?, ?, ?)",
                      (account, *TRANSFER_MOVING)).fetchone():
            return "name", "Another character of yours is moving. Wait for it to finish."
        if db.execute("SELECT 1 FROM transfers WHERE account_id = ? AND state = 'done' AND changed > ? "
                      "AND lower(new_name) = lower(?)", (account, now - TRANSFER_DAYS * 86400, name)).fetchone():
            return "name", f"He moved in the last {TRANSFER_DAYS} days."
        if sum(1 for c in self.mine(account) if c["region"] == to) >= ROSTER_SLOTS:
            return "to", "Your character slots there are full."
        if not CHARACTER_NAME.match(new_name):
            return "new_name", "4 to 10 letters or digits."
        if self.taken_in(to, new_name):
            return "new_name", "That name is taken there. Pick another."
        db.execute("INSERT INTO transfers (account_id, name, new_name, from_region, to_region, state, asked, changed) "
                   "VALUES (?, ?, ?, ?, ?, 'asked', ?, ?)", (account, name, new_name, his["region"], to, now, now))
        return None

    def rename_transfer(self, account: int, move: int, new_name: str):
        """A held move given a new name, or sent on again once a slot is free: None or an error."""
        row = self.db().execute("SELECT to_region, why FROM transfers WHERE id = ? AND account_id = ? AND state = 'held'",
                                (move, account)).fetchone()
        if row is None:
            return "No such move waiting."
        if row[1] == "name":
            if not CHARACTER_NAME.match(new_name):
                return "4 to 10 letters or digits."
            if self.taken_in(row[0], new_name):
                return "That name is taken there. Pick another."
        else:
            new_name = None
        self.db().execute("UPDATE transfers SET state = 'gone', why = '', new_name = coalesce(?, new_name), changed = ? "
                          "WHERE id = ?", (new_name, int(time.time()), move))
        return None

    def cancel_transfer(self, account: int, move: int) -> bool:
        """Called off while it is still only asked."""
        return self.db().execute("UPDATE transfers SET state = 'failed', why = 'cancelled', changed = ? "
                                 "WHERE id = ? AND account_id = ? AND state = 'asked'",
                                 (int(time.time()), move, account)).rowcount == 1

    def transfers_for(self, region: str) -> str:
        """A region's mu2_server's work, one line each: `give ID STATE ACCOUNT NAME` for a character
        leaving it, `take ID ACCOUNT NAME KEPT` for one coming."""
        lines = []
        db = self.db()
        for move, state, account, name in db.execute(
                "SELECT id, state, account_id, name FROM transfers WHERE from_region = ? AND state IN ('asked', 'out')",
                (region,)):
            lines.append(f"give {move} {state} {account} {name}")
        for move, account, name, kept in db.execute(
                "SELECT id, account_id, new_name, kept FROM transfers WHERE to_region = ? AND state = 'gone'", (region,)):
            lines.append(f"take {move} {account} {name} {kept}")
        return "".join(line + "\n" for line in lines)

    def told_transfer(self, region: str, move: int, state: str, why: str, kept: str) -> bool:
        """A server's step of a move: only the one its state allows, and only by the server it is at."""
        steps = {("asked", "out"): "from_region", ("asked", "failed"): "from_region", ("asked", "asked"): "from_region",
                 ("out", "gone"): "from_region", ("gone", "done"): "to_region", ("gone", "held"): "to_region"}
        db = self.db()
        row = db.execute("SELECT state, from_region, to_region FROM transfers WHERE id = ?", (move,)).fetchone()
        if row is None or (row[0], state) not in steps:
            return False
        if region != (row[1] if steps[(row[0], state)] == "from_region" else row[2]):
            return False
        if state == "out" and not KEPT.match(kept):
            return False
        # His bytes are kept only while he is between servers.
        keep = kept if state == "out" else ("" if state in ("done", "failed") else None)
        db.execute("UPDATE transfers SET state = ?, why = ?, kept = coalesce(?, kept), changed = ? WHERE id = ?",
                   (state, why[:64], keep, int(time.time()), move))
        if row[0] != state:
            print(f"transfer {move}: {row[0]} -> {state}{' (' + why + ')' if why else ''} by {region}", flush=True)
        return True

    def days_report(self) -> list:
        """One row a UTC day from the first account to today (the /online page's graphs): the
        accounts made that day and all made by its end, the accounts that played, and the peak."""
        db = self.db()
        made = dict(db.execute("SELECT strftime('%Y-%m-%d', created, 'unixepoch'), count(*) FROM accounts GROUP BY 1"))
        played = dict(db.execute("SELECT day, count(*) FROM day_players GROUP BY day"))
        peaks = {day: (peak, at) for day, peak, at in db.execute("SELECT day, peak, peak_at FROM days")}
        known = list(made) + list(played) + list(peaks)
        if not known:
            return []
        at = calendar.timegm(time.strptime(min(known), "%Y-%m-%d"))
        today = utc_day(time.time())
        out, total = [], 0
        while True:
            day = utc_day(at)
            total += made.get(day, 0)
            peak, peak_at = peaks.get(day, (0, 0))
            out.append({"day": day, "registered": made.get(day, 0), "accounts": total,
                        "players": played.get(day, 0), "peak": peak, "peak_at": peak_at})
            if day >= today:
                return out
            at += 86400

    def online_report(self) -> dict:
        # Every region's list as one: newest told, fresh only while every region still tells.
        regions = self.regions_told()
        now = time.time()
        told = {"at": max([r["at"] for r in regions.values()] + [0]),
                "players": [p for r in regions.values() for p in r["players"]]}
        stale = sorted(region for region, r in regions.items() if now - r["at"] >= ONLINE_STALE_SECONDS)
        ids = sorted({p["account"] for p in told["players"] if p.get("account")})
        accounts = {}
        if ids:
            marks = ",".join("?" * len(ids))
            accounts = {row[0]: (row[1], row[2] or "") for row in self.db().execute(
                f"SELECT id, email, name FROM accounts WHERE id IN ({marks})", ids)}
        players = []
        for p in told["players"]:
            email, name = accounts.get(p.get("account"), ("", ""))
            players.append(p | {"email": email, "account_name": name,
                                "played": self.played(p["account"], str(p.get("name"))) if p.get("account") else 0})
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
        return {"at": told["at"], "fresh": bool(regions) and not stale, "stale": stale, "players": players,
                # Every region of REGIONS, told or not, with its people and bots apart (the page's realm row).
                "regions": [{"name": region, "city": city, "at": regions.get(region, {}).get("at", 0),
                             "up": region in regions and region not in stale,
                             "players": sum(1 for p in regions.get(region, {}).get("players", []) if not p.get("bot")),
                             "bots": sum(1 for p in regions.get(region, {}).get("players", []) if p.get("bot"))}
                            for region, city in REGIONS],
                "recent": recent, "machines": machines, "everyone": everyone, "days": self.days_report()}

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

    def bug(self, row: dict):
        """Keeps one bug report under the next id, and returns the id; None when the day is full."""
        with self.bug_lock:
            day = time.strftime("%Y-%m-%d", time.gmtime(row["at"]))
            if self.bug_days.get(day, 0) >= MOST_BUGS_A_DAY:
                return None
            row = {"id": max(self.bugs, default=0) + 1} | row | {"state": "new"}
            with self.bugs_path.open("a") as f:
                f.write(json.dumps(row, separators=(",", ":")) + "\n")
            self.bugs[row["id"]] = row
            self.bug_days[day] = self.bug_days.get(day, 0) + 1
            return row["id"]

    def set_bug_state(self, bug: int, state: str) -> bool:
        with self.bug_lock:
            if bug not in self.bugs:
                return False
            now = int(time.time())
            with self.bugs_path.open("a") as f:
                f.write(json.dumps({"id": bug, "state": state, "at": now}, separators=(",", ":")) + "\n")
            self.bugs[bug] |= {"state": state, "state_at": now}
            return True

    def bugs_report(self) -> list:
        """Every report, newest first, with its account's email and name."""
        with self.bug_lock:
            rows = [dict(row) for row in self.bugs.values()]
        rows.sort(key=lambda row: -row["id"])
        ids = sorted({row["account"] for row in rows if isinstance(row.get("account"), int)})
        accounts = {}
        for start in range(0, len(ids), 500):
            chunk = ids[start:start + 500]
            accounts |= {row[0]: (row[1], row[2] or "") for row in self.db().execute(
                f"SELECT id, email, name FROM accounts WHERE id IN ({','.join('?' * len(chunk))})", chunk)}
        for row in rows:
            if "account" in row:
                email, name = accounts.get(row["account"], ("", ""))
                row |= {"email": email, "account_name": name}
        return rows

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
# Bug reports: a player who finds ten in an hour has said what there is to say.
BUG_LIMIT = Limit(10, 3600)
# A ticket a Play: sixty an hour is a player who starts the game every minute.
TICKET_LIMIT = Limit(60, 3600)
# Comments, per account: a conversation, not a flood.
COMMENT_LIMIT = Limit(10, 600)


# ---- the site's words --------------------------------------------------------------------------------

def site_page(site: Path, store: "Store", file: str, lang: str = "en") -> str:
    """A page as it is served in English (with the published Latest updates), before any translation."""
    data = (site / file).read_bytes()
    if file == "index.html":
        data = with_updates(data, store.updates(published_only=True), lang)
    return data.decode()


def site_units(site: Path, store: "Store") -> list:
    """Every unit of the site, each once where it first stands: {group, source, path}; the group is
    the page's file, "updates" for the Latest updates, or "js" for what a script says."""
    out, seen = [], set()
    for file, (path, _) in i18n.PAGES.items():
        page = site_page(site, store, file)
        for start, end, kind, source in i18n.units(page):
            group = "js" if kind == "js" else "updates" if file == "index.html" and i18n.in_updates(page, start) else file
            k = i18n.key(source)
            if k not in seen:
                seen.add(k)
                out.append({"group": group, "source": k, "path": path})
    for source in i18n.script_units((site / "site.js").read_text()):
        k = i18n.key(source)
        if k not in seen:
            seen.add(k)
            out.append({"group": "js", "source": k, "path": "/"})
    return out


def examples(have: dict) -> list:
    """Up to EXAMPLES of a language's translations, [[source, text]]: the patch notes' voice, lines
    with tags, and the short labels."""
    picked = [(k, t) for k, t in have.items() if 20 < len(k) < 220][:28]
    picked += [(k, t) for k, t in have.items() if i18n.has_tags(k) and len(k) < 300][:6]
    picked += [(k, t) for k, t in have.items() if len(k) <= 20][:EXAMPLES - len(picked)]
    return [list(pair) for pair in picked]


# ---- the Latest updates in Discord ---------------------------------------------------------------

class Discord:
    """Each release day's published lines as one message in #updates, by the channel's webhook.
    A day changed (touch) is told DISCORD_QUIET seconds after its last change; the message's id and
    a hash of what it said and its count of lines are kept in settings as `discord:<day>`, so a day
    is posted once, posted again at the channel's foot when it gains lines (the old message taken
    away), edited when they are only reworded, and deleted when none is published any more."""

    def __init__(self, store: Store, webhook: Path, quiet: int = DISCORD_QUIET):
        self.store, self.webhook_path, self.quiet = store, webhook, quiet
        self.lock = threading.Lock()
        self.dirty = {}  # day -> when it last changed
        threading.Thread(target=self.run, daemon=True).start()

    def touch(self, *days: str, at: float = None):
        with self.lock:
            for day in days:
                if day:
                    self.dirty[day] = time.time() if at is None else at

    def webhook(self) -> str:
        try:
            url = self.webhook_path.read_text().strip()
        except OSError:
            return ""
        return url if DISCORD_WEBHOOK.match(url) else ""

    def run(self):
        while True:
            time.sleep(5)
            now = time.time()
            with self.lock:
                ready = {day: at for day, at in self.dirty.items() if now - at >= self.quiet}
            for day, at in sorted(ready.items()):
                try:
                    self.tell(day)
                    told = True
                except Exception as e:  # the network, or Discord: tried again a quiet spell later
                    print(f"discord: {day} not told: {e}", flush=True)
                    told = False
                with self.lock:
                    if self.dirty.get(day) == at:
                        if told:
                            del self.dirty[day]
                        else:
                            self.dirty[day] = time.time()

    @staticmethod
    def message(day: str, lines: list) -> dict:
        def plain(text):  # Discord's markdown taken as it is written
            return re.sub(r"([\\*_~`|])", r"\\\1", text)
        year, month, date = (int(x) for x in day.split("-"))
        new = [line["text"] for line in lines if line["kind"] == "new"]
        fixed = [line["text"] for line in lines if line["kind"] == "fixed"]
        rows = []
        for heading, texts in (("New", new), ("Fixed", fixed)):
            if texts:
                rows += [f"**{heading}**"] + [f"• {plain(text)}" for text in texts] + [""]
        # Past Discord's 4096 an embed, the rest is on the site.
        shown, size = [], 0
        for n, row in enumerate(rows):
            if size + len(row) + 80 > DISCORD_MOST_TEXT:
                shown.append(f"…and {sum(r.startswith('•') for r in rows[n:])} more at {SITE_URL}")
                break
            shown.append(row)
            size += len(row) + 1
        return {"embeds": [{"title": f"{i18n.day_label('en', year, month, date)} · "
                                     f"{i18n.count_label('en', len(new), len(fixed))}",
                            "url": SITE_URL, "description": "\n".join(shown).strip(),
                            "color": DISCORD_COLOUR}],
                "allowed_mentions": {"parse": []}}

    def call(self, method: str, url: str, body: dict = None) -> dict:
        import urllib.request
        import urllib.error
        request = urllib.request.Request(
            url, data=json.dumps(body).encode() if body is not None else None, method=method,
            # Discord's Cloudflare turns away Python's own agent.
            headers={"Content-Type": "application/json", "User-Agent": f"MU2-web ({SITE_URL}, 1)"})
        try:
            with urllib.request.urlopen(request, timeout=15) as reply:
                data = reply.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            raise RuntimeError(f"{method} {e.code} {e.read()[:200]!r}")
        return json.loads(data) if data else {}

    def tell(self, day: str):
        url = self.webhook()
        if not url:
            return
        lines = [line for line in self.store.updates(published_only=True) if line["day"] == day]
        key = f"discord:{day}"
        kept = self.store.setting(key).split()
        sent, said = (kept + ["", ""])[:2]
        if not lines:
            if sent:
                self.call("DELETE", f"{url}/messages/{sent}")
                self.store.set_setting(key, "")
                print(f"discord: {day} taken away", flush=True)
            return
        body = self.message(day, lines)
        digest = hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest()[:16]
        if sent and digest == said:
            return
        # A day that gained lines is posted again at the channel's foot, the old message taken away:
        # an edit is silent and stays where it was, so a later release of the same day went unseen
        # (the user, 2026-10-20: "i published updates but was not pushed on discord"). A day whose
        # lines were only reworded or taken away is edited in place.
        before = (kept + ["", "", "0"])[2]
        grew = not sent or len(lines) > (int(before) if before.isdigit() else 0)
        if sent and not grew and self.call("PATCH", f"{url}/messages/{sent}", body) is not None:
            print(f"discord: {day} edited, {len(lines)} lines", flush=True)
        else:
            if sent:
                self.call("DELETE", f"{url}/messages/{sent}")
            sent = self.call("POST", f"{url}?wait=true", body)["id"]
            print(f"discord: {day} posted, {len(lines)} lines", flush=True)
        self.store.set_setting(key, f"{sent} {digest} {len(lines)}")


# ---- the handler ---------------------------------------------------------------------------------

def update_fields(said: dict, default_state: str = None):
    """An update line's fields from what was sent, checked; or the words saying what is wrong."""
    day, kind = str(said.get("day", "")), said.get("kind")
    text = " ".join(str(said.get("text", "")).split())
    state = said.get("state", default_state)
    if not UPDATE_DAY.match(day):
        return "day: YYYY-MM-DD"
    if kind not in UPDATE_KINDS:
        return f"kind is one of {', '.join(UPDATE_KINDS)}"
    if state not in UPDATE_STATES:
        return f"state is one of {', '.join(UPDATE_STATES)}"
    if not text or len(text) > MOST_UPDATE_TEXT:
        return f"text: 1 to {MOST_UPDATE_TEXT} characters"
    commits = said.get("commits", [])
    if not isinstance(commits, list) or not all(isinstance(c, str) and re.match(r"^[0-9a-f]{7,40}$", c) for c in commits):
        return "commits: a list of hashes"
    position = said.get("position")
    if position is not None and (not isinstance(position, int) or isinstance(position, bool)):
        return "position: a number"
    return {"day": day, "kind": kind, "text": text, "state": state, "commits": commits, "position": position}


def updates_html(lines: list, lang: str = "en") -> str:
    """The front page's Latest updates: a <details> for each of the newest UPDATE_DAYS_SHOWN days,
    each closed until clicked, its New and its Fixed lines (site.css .fix-day). The date and the count in
    `lang`; the lines and headings are units of the page, put in its language with the rest."""
    from html import escape
    days = {}
    for line in lines:
        days.setdefault(line["day"], {"new": [], "fixed": []})[line["kind"]].append(line["text"])
    out = []
    for day in sorted(days, reverse=True)[:UPDATE_DAYS_SHOWN]:
        year, month, date = (int(x) for x in day.split("-"))
        new, fixed = days[day]["new"], days[day]["fixed"]
        out.append(f'<details class="fix-day">\n'
                   f'<summary><span class="fix-date" translate="no">{i18n.day_label(lang, year, month, date)}</span>'
                   f'<span class="fix-count" translate="no">{i18n.count_label(lang, len(new), len(fixed))}</span></summary>')
        for heading, texts in (("New", new), ("Fixed", fixed)):
            if texts:
                out.append(f'<div class="fix-group">\n<h4>{heading}</h4>\n<ul>\n'
                           # The lines stay English in every language (the user, 2026-10-23: "lets use
                           # english for update notes"): translate="no" keeps them out of i18n's units,
                           # so neither /translate nor mu ship translate sees them.
                           + "".join(f'<li translate="no">{escape(t)}</li>\n' for t in texts) + "</ul>\n</div>")
        out.append("</details>")
    return "\n".join(out)


def with_updates(page: bytes, lines: list, lang: str = "en") -> bytes:
    """index.html with its lines between the markers drawn from the published ones; as it is when none is."""
    if not lines:
        return page
    text = page.decode()
    start, end = text.find("<!--updates-->"), text.find("<!--/updates-->")
    if start < 0 or end < start:
        return page
    return (text[:start + len("<!--updates-->")] + "\n" + updates_html(lines, lang) + "\n" + text[end:]).encode()


class Handler(BaseHTTPRequestHandler):
    server_version = "mu2-web"
    sys_version = ""
    store: Store = None
    site: Path = None
    downloads: Path = None

    def finish(self):
        try:
            super().finish()
        finally:
            self.store.close()

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
        if path == "/api/bug":
            return self.bug()
        if path == "/api/bugs/state":
            return self.bug_state()
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
        if path == "/api/updates":
            return self.update_line()
        if path == "/internal/updates":
            return self.told_updates()
        if path == "/internal/discord":
            return self.tell_discord()
        if path == "/internal/translations":
            return self.take_translations()
        if path == "/api/texts":
            return self.save_text()
        if path == "/api/transfer":
            return self.transfer()
        if path == "/internal/transfers":
            return self.told_transfer()
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

    def bug(self):
        """A player's bug report from the game, with where they stood when they wrote it."""
        if int(self.headers.get("Content-Length") or 0) > MOST_BUG_BODY:
            return self.reply(413, {"error": "too big"})
        if not BUG_LIMIT.allow(self.client()):
            return self.reply(429, {"error": "later"})
        said = self.body(MOST_BUG_BODY)
        if said is None:
            return self.reply(400, {"error": "bad request"})
        text = said.get("text")
        text = text.strip() if isinstance(text, str) else ""
        if not 1 <= len(text) <= MOST_BUG_TEXT:
            return self.reply(400, {"error": f"Write 1 to {MOST_BUG_TEXT} characters."})
        row = {"at": int(time.time())}
        signed = self.who() if self.bearer() else None
        account = signed[0] if signed else self.store.ticket_account(self.bearer())
        if account:
            row["account"] = account
        row["text"] = text
        if said.get("platform") in BUG_PLATFORMS:
            row["platform"] = said["platform"]
        for key, room in BUG_FIELDS.items():
            value = said.get(key)
            if isinstance(value, str) and value.strip():
                row[key] = value.strip()[:room]
        for key in BUG_NUMBERS:
            value = said.get(key)
            if isinstance(value, int) and not isinstance(value, bool) and abs(value) < 1_000_000:
                row[key] = value
        bug = self.store.bug(row)
        if bug is None:
            return self.reply(429, {"error": "later"})
        print(f"{self.client()} bug {bug}: {row.get('character', '?')} in {row.get('world', '?')} "
              f"{row.get('platform', '?')} | {text[:80]!r}", flush=True)
        self.reply(200, {"ok": True, "id": bug})

    def admin(self):
        """The admin's account, or None with 404 already answered (as for a page that does not exist)."""
        row = self.who()
        if row is None or row[3] != "admin":
            self.reply(404, {"error": "not found"})
            return None
        return row

    def bug_state(self):
        row = self.admin()
        if row is None:
            return
        said = self.body() or {}
        bug, state = said.get("id"), said.get("state")
        if state not in BUG_STATES or not isinstance(bug, int) or isinstance(bug, bool):
            return self.reply(400, {"error": f"state is one of {', '.join(BUG_STATES)}"})
        if not self.store.set_bug_state(bug, state):
            return self.reply(404, {"error": "no such report"})
        print(f"{self.client()} account {row[0]} marked bug {bug} {state}", flush=True)
        self.reply(200, {"ok": True})

    def update_line(self):
        row = self.admin()
        if row is None:
            return
        said = self.body() or {}
        line = said.get("id")
        if line is not None and (not isinstance(line, int) or isinstance(line, bool)):
            return self.reply(400, {"error": "bad id"})
        was = self.store.update_day(line)
        if said.get("delete") is True:
            if line is None or not self.store.delete_update(line):
                return self.reply(404, {"error": "no such line"})
            self.discord.touch(was)
            print(f"{self.client()} account {row[0]} deleted update {line}", flush=True)
            return self.reply(200, {"ok": True, "id": line})
        checked = update_fields(said)
        if isinstance(checked, str):
            return self.reply(400, {"error": checked})
        checked["id"] = line
        saved = self.store.save_update(checked)
        if saved is None:
            return self.reply(404, {"error": "no such line"})
        self.discord.touch(was, checked["day"])
        print(f"{self.client()} account {row[0]} saved update {saved} ({checked['state']})", flush=True)
        self.reply(200, {"ok": True, "id": saved})

    def save_text(self):
        row = self.admin()
        if row is None:
            return
        said = self.body(MOST_UPDATES_BODY) or {}
        lang, source, text = said.get("lang"), said.get("source"), said.get("text")
        if lang not in i18n.LANGS or not isinstance(source, str) or not isinstance(text, str):
            return self.reply(400, {"error": "lang, source and text"})
        source, text = i18n.key(source), text.strip()
        if len(text) > 8000:
            return self.reply(400, {"error": "too long"})
        known = {u["source"] for g in self.texts_report()["groups"] for u in g["units"]}
        if source not in known and text:
            return self.reply(400, {"error": "no such text on the site"})
        why = i18n.check(source, text) if text else None
        if why:
            return self.reply(400, {"error": why})
        self.store.save_text(lang, source, text, row[0])
        print(f"{self.client()} account {row[0]} translated to {lang}: {source[:60]!r}", flush=True)
        self.reply(200, {"ok": True})

    def site_page(self, file: str, lang: str = "en") -> str:
        return site_page(self.site, self.store, file, lang)

    def texts_report(self) -> dict:
        """Every unit of the site, grouped as the /translate page shows them, each once (where it
        first stands), and each language's translations."""
        groups = {}
        names = {"index.html": "Front page", "updates": "Latest updates", "js": "Messages"}
        for file, (path, name) in i18n.PAGES.items():
            names.setdefault(file, name)
        for unit in site_units(self.site, self.store):
            groups.setdefault(unit["group"], []).append({"source": unit["source"], "tags": i18n.has_tags(unit["source"]),
                                                        "path": unit["path"]})
        order = ["index.html", "updates", "account.html", "privacy.html", "contact.html", "mac.html", "linux.html", "js"]
        return {"langs": {code: v[0] for code, v in i18n.LANGS.items()},
                "groups": [{"id": g, "name": names[g], "units": groups[g]} for g in order if g in groups],
                "texts": self.store.texts()}

    def untranslated(self):
        """Each language's units with no translation, and some of its own lines to keep its words."""
        units = [u["source"] for u in site_units(self.site, self.store)]
        texts = self.store.texts()
        return self.reply(200, {"langs": LANG_NAMES,
                                "missing": {lang: [k for k in units if k not in texts.get(lang, {})] for lang in i18n.LANGS},
                                "examples": {lang: examples(texts.get(lang, {})) for lang in i18n.LANGS}})

    def take_translations(self):
        """Translations put in where a unit still has none; one that is not a unit of the site now,
        or loses its English's tags, is refused."""
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = (self.body(most=2_000_000) or {}).get("texts")  # this machine only: a whole language at once
        if not isinstance(said, dict):
            return self.reply(400, {"error": "texts: {lang: {source: text}}"})
        units = {u["source"] for u in site_units(self.site, self.store)}
        put, refused = 0, 0
        for lang, table in said.items():
            if lang not in i18n.LANGS or not isinstance(table, dict):
                return self.reply(400, {"error": f"no language {lang!r}"})
            for source, text in table.items():
                text = " ".join(str(text).split())
                if source in units and text and not i18n.check(source, text) and self.store.fill_text(lang, source, text):
                    put += 1
                else:
                    refused += 1
        print(f"translations: {put} put in, {refused} refused", flush=True)
        self.reply(200, {"put": put, "refused": refused})

    def tell_discord(self):
        """A day's message sent (or put right) now, not after a quiet spell: on the box,
        curl -s -d '{"day":"2026-10-19"}' http://127.0.0.1:8080/internal/discord"""
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        day = str((self.body() or {}).get("day", ""))
        if not UPDATE_DAY.match(day):
            return self.reply(400, {"error": "day: YYYY-MM-DD"})
        if not self.discord.webhook():
            return self.reply(409, {"error": "no webhook in discord_webhook beside the database"})
        self.discord.touch(day, at=0)
        self.reply(200, {"ok": True})

    def told_updates(self):
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = self.body(MOST_UPDATES_BODY)
        lines = said.get("entries") if said else None
        if not isinstance(lines, list):
            return self.reply(400, {"error": "entries: a list"})
        checked = []
        for one in lines:
            fields = update_fields(one if isinstance(one, dict) else {}, default_state="draft")
            if isinstance(fields, str):
                return self.reply(400, {"error": fields})
            line = one.get("id")
            if line is not None and (not isinstance(line, int) or isinstance(line, bool)):
                return self.reply(400, {"error": "bad id"})
            checked.append(fields | {"id": line})
        gone = said.get("delete", [])
        if not isinstance(gone, list) or not all(isinstance(g, int) and not isinstance(g, bool) for g in gone):
            return self.reply(400, {"error": "delete: a list of ids"})
        noted = said.get("noted_through")
        noted = noted if isinstance(noted, str) and re.match(r"^[0-9a-f]{7,40}$", noted) else ""
        days = {self.store.update_day(line) for line in gone} | {self.store.update_day(line["id"]) for line in checked}
        added = self.store.add_updates(checked, noted)
        for line in gone:
            self.store.delete_update(line)
        self.discord.touch(*days, *(line["day"] for line in checked))
        print(f"mu ship: {added} update lines, {len(gone)} deleted, noted through {noted or '(unchanged)'}", flush=True)
        self.reply(200, {"ok": True, "added": added})

    def told_online(self):
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = self.body(MOST_ONLINE_BODY)
        players = said.get("players") if said else None
        if not isinstance(players, list):
            return self.reply(400, {"error": "bad request"})
        region = said.get("region") if isinstance(said.get("region"), str) and said.get("region") else DEFAULT_REGION
        region = region[:32]
        self.store.told_online([p for p in players if isinstance(p, dict)], region)
        if isinstance(said.get("characters"), list):
            self.store.told_characters([c for c in said["characters"] if isinstance(c, dict)], region)
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
        machine = self.store.recent(self.client()) or self.store.machine_at(row[0], self.client())
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

    def transfer(self):
        """{name, to, new_name?} a move asked; {id, new_name?} a held one sent on; {id, cancel} one called off."""
        row = self.signed()
        if row is None:
            return
        said = self.body() or {}
        if isinstance(said.get("id"), int):
            if said.get("cancel"):
                ok = self.store.cancel_transfer(row[0], said["id"])
                return self.reply(200, {"ok": True}) if ok else self.reply(409, {"error": "It can't be called off now."})
            error = self.store.rename_transfer(row[0], said["id"], str(said.get("new_name", "")).strip())
            return self.reply(409, {"field": "new_name", "error": error}) if error else self.reply(200, {"ok": True})
        name = str(said.get("name", ""))
        refused = self.store.ask_transfer(row[0], name, str(said.get("to", "")),
                                          str(said.get("new_name") or name).strip())
        if refused:
            return self.reply(409, {"field": refused[0], "error": refused[1]})
        print(f"{self.client()} account {row[0]} asked {name} moved to {said.get('to')}", flush=True)
        self.reply(200, {"ok": True})

    def told_transfer(self):
        if not self.direct():
            return self.reply(404, {"error": "not found"})
        said = self.body(MOST_ONLINE_BODY) or {}
        ok = isinstance(said.get("id"), int) and self.store.told_transfer(
            str(said.get("region", ""))[:32], said["id"], str(said.get("state", "")), str(said.get("why", "")),
            str(said.get("kept", "")))
        self.reply(200 if ok else 409, {"ok": ok})

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

    def do_HEAD(self):
        # Crawlers and link checkers ask HEAD: a GET's status and headers, its body dropped.
        self.wfile = HeadersOnly(self.wfile)
        self.do_GET()

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
        if url.path == "/api/status":
            return self.reply(200, self.store.status())
        if url.path == "/api/online":
            # Anyone not an admin is told there is nothing here, as for a page that does not exist.
            row = self.who()
            if row is None or row[3] != "admin":
                return self.reply(404, {"error": "not found"})
            return self.reply(200, self.store.online_report())
        if url.path == "/api/bugs":
            return self.reply(200, {"bugs": self.store.bugs_report()}) if self.admin() else None
        if url.path == "/api/updates":
            return (self.reply(200, {"entries": self.store.updates(), "noted_through": self.store.noted_through()})
                    if self.admin() else None)
        if url.path == "/api/texts":
            return self.reply(200, self.texts_report()) if self.admin() else None
        if url.path == "/internal/untranslated":
            return self.untranslated() if self.direct() else self.reply(404, {"error": "not found"})
        if url.path == "/internal/updates":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            return self.reply(200, {"entries": self.store.updates(), "noted_through": self.store.noted_through()})
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
            if found:
                found = found | {"played": self.store.played(account, str(found.get("name")))}
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
        if url.path == "/internal/transfers":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            data = self.store.transfers_for(parse_qs(url.query).get("region", [""])[0]).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if url.path == "/internal/ticket":
            if not self.direct():
                return self.reply(404, {"error": "not found"})
            row = self.store.redeem(parse_qs(url.query).get("ticket", [""])[0])
            return self.reply(200, {"account": row[0], "secret": row[1]}) if row else self.reply(404, {"error": "no ticket"})
        if url.path.startswith("/api/"):
            return self.reply(404, {"error": "not found"})
        self.file(url.path)

    # A page in a language, kept until its file, the published updates or a translation changes.
    pages_made = {}

    def page_in(self, file: str, lang: str) -> bytes:
        lines = self.store.updates(published_only=True) if file == "index.html" else []
        mark = ((self.site / file).stat().st_mtime_ns, (self.site / "site.js").stat().st_mtime_ns,
                json.dumps(lines, sort_keys=True), self.store.texts_version)
        made = self.pages_made.get((file, lang))
        if made and made[0] == mark:
            return made[1]
        page = self.site_page(file, lang)
        texts = self.store.texts(lang) if lang != "en" else {}
        data = i18n.localise(page, file, lang, texts, i18n.script_units((self.site / "site.js").read_text())).encode()
        self.pages_made[(file, lang)] = (mark, data)
        return data

    def listing(self) -> dict:
        out = {}
        for system, name in DOWNLOADS.items():
            f = self.downloads / name
            out[system] = ({"file": f"/downloads/{name}", "bytes": f.stat().st_size, "sha256": download_sum(f)}
                           if f.is_file() else None)
        return out

    def file(self, path: str):
        # The page and its pictures. Caddy serves /downloads/ itself; this answers for local runs.
        # A page under /<lang>/ is the English one in that language (web/i18n.py).
        lang, path = i18n.lang_of(path)
        asked = path
        if path in ("", "/"):
            path = "/index.html"
        elif path in ("/account", "/account/"):
            path = "/account.html"
        elif path in ("/online", "/online/"):
            path = "/online.html"
        elif path in ("/bugs", "/bugs/"):
            path = "/bugs.html"
        elif path in ("/updates", "/updates/"):
            path = "/updates.html"
        elif path in ("/privacy", "/privacy/"):
            path = "/privacy.html"
        elif path in ("/contact", "/contact/"):
            path = "/contact.html"
        elif path in ("/mac", "/mac/"):
            path = "/mac.html"
        elif path in ("/linux", "/linux/"):
            path = "/linux.html"
        elif path in ("/translate", "/translate/"):
            path = "/translate.html"
        if lang and path.lstrip("/") not in i18n.PAGES:
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        # A language picked in the footer (?lang=, i18n.switcher) is kept in the mu-lang cookie and the
        # page asked again without it, so the pick holds on every page, script or none.
        picked = parse_qs(urlparse(self.path).query).get("lang", [""])[0]
        if path.lstrip("/") in i18n.PAGES and (picked == "en" or picked in i18n.LANGS):
            secure = "; Secure" if self.headers.get("X-Forwarded-Proto", "") == "https" else ""
            self.send_response(302)
            self.send_header("Location", ("" if picked == "en" else f"/{picked}") + (asked if asked.startswith("/") else "/" + asked))
            self.send_header("Set-Cookie", f"{i18n.CHOICE}={picked}; Path=/; Max-Age=31536000; SameSite=Lax{secure}")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        # An English page, asked by a browser in one of the site's languages (or by someone who
        # picked one in the footer), is that page in it: sent there before anything is drawn.
        if lang is None and path.lstrip("/") in i18n.PAGES:
            want = i18n.pick(self.headers.get("Accept-Language", ""), self.headers.get("Cookie", ""))
            if want != "en":
                query = urlparse(self.path).query
                self.send_response(302)
                self.send_header("Location", f"/{want}{asked if asked.startswith('/') else '/' + asked}"
                                 + (f"?{query}" if query else ""))
                self.send_header("Vary", "Accept-Language, Cookie")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
        root = self.downloads if path.startswith("/downloads/") else self.site
        rel = path[len("/downloads/"):] if root is self.downloads else path.lstrip("/")
        target = (root / rel).resolve()
        if root.resolve() not in target.parents or not target.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        data = target.read_bytes()
        if path.lstrip("/") in i18n.PAGES:
            data = self.page_in(path.lstrip("/"), lang or "en")
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(target.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        if path.lstrip("/") in i18n.PAGES:
            self.send_header("Vary", "Accept-Language, Cookie")
        if path.startswith("/shots/") and path.endswith(".webp"):  # kept a day: a shot taken again keeps its name
            self.send_header("Cache-Control", "public, max-age=86400")
        self.end_headers()
        self.wfile.write(data)


class HeadersOnly:
    """A HEAD's wfile: the headers go out in one write (end_headers), anything after is dropped."""
    def __init__(self, out):
        self.out, self.sent = out, False

    def write(self, data):
        if not self.sent:
            self.sent = True
            return self.out.write(data)
        return len(data)

    def flush(self):
        self.out.flush()


def main():
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default="out/web/accounts.db")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--site", default=str(here / "site"))
    ap.add_argument("--downloads", default=str(here / "downloads"))
    ap.add_argument("--discord-quiet", type=int, default=DISCORD_QUIET,
                    help="seconds a day's lines are left alone before #updates is told")
    args = ap.parse_args()
    Handler.store = Store(Path(args.db))
    print(f"{Handler.store.load_rosters()} characters as their regions last listed them", flush=True)
    Handler.discord = Discord(Handler.store, Path(args.db).parent / "discord_webhook", args.discord_quiet)
    seeded = Handler.store.seed_texts(i18n.seeds(here / "i18n"))
    if seeded:
        print(f"{seeded} translations put in from web/i18n/", flush=True)
    Handler.site = Path(args.site)
    Handler.downloads = Path(args.downloads)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"mu2-web on http://{args.host}:{args.port}, accounts in {args.db}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
