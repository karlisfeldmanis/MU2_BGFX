#!/usr/bin/env python3
"""The guild run's sampler and report (netbot --guilds, tools/netbot/guilds.h).

  guild_report.py sample RUN [SECONDS]   every SECONDS (60), each bot's line from RUN/bots/guild/*.who
                                         appended to RUN/guild_samples.tsv, until killed
  guild_report.py report RUN [SPEED]     RUN/guild_report.md: what the guilds did (the netbots'
                                         [guild] and [board] lines) and how their members grew
                                         beside the guildless, at the same levels

Game hours are the wall's times SPEED (mu2_server --speed, 100): a bot's clock is his map's.
"""
import collections
import glob
import os
import re
import sys
import time


def sample(run, every):
    out = os.path.join(run, "guild_samples.tsv")
    if not os.path.exists(out):
        with open(out, "w") as f:
            f.write("wall\tname\tkin\tlevel\tstrength\tguild\tguildName\tmaster\tzen\ttrader\n")
    while True:
        now = int(time.time())
        rows = []
        for path in glob.glob(os.path.join(run, "bots", "guild", "*.who")):
            try:
                f = open(path).read().rstrip("\n").split("\t")
            except OSError:
                continue
            if len(f) < 17 or now - int(f[15]) > 30:
                continue
            rows.append("\t".join([str(now), f[0], f[1], f[5], f[6], f[7], f[8], f[9], f[12], f[4]]))
        with open(out, "a") as f:
            for r in rows:
                f.write(r + "\n")
        time.sleep(every)


def band(level):
    for top in (50, 100, 150, 200, 250):
        if level < top:
            return "%d-%d" % (top - 50, top - 1)
    return "250+"


def report(run, speed):
    lines = []
    for path in sorted(glob.glob(os.path.join(run, "netbot-*.log"))):
        lines += open(path, errors="replace").read().splitlines()
    g = [l for l in lines if "[guild]" in l]
    board = [l for l in lines if "guild party" in l]
    count = collections.Counter()
    pats = {
        "founded": r"\[guild\] \S+ founded ",
        "set out to found": r"sets out to found a guild",
        "gave up founding": r"gave up founding|cannot reach Devias|found no Guild Master|leaves founding",
        "invites sent": r"\] \S+ invites \S+ \(",
        "asks to join": r" asks \S+ to join ",
        "asks taken": r" takes \S+'s ask to join",
        "asks declined": r" declines \S+'s ask to join",
        "invites taken": r"\[guild\] joins ",
        "invites declined": r"\[guild\] declines ",
        "removed": r" removes \S+ from ",
        "left": r"\] \S+ leaves \S+: ",
        "disbanded": r" disbands ",
        "notices set": r" sets \S+'s notice",
        "party plans": r" plans \S+'s parties",
        "gifts set out": r"goes to give",
        "gifts given": r"\[guild\] gives ",
        "gifts failed": r"could not give",
        "gifts taken": r"\[guild\] takes \d+ thing",
    }
    for l in g:
        for k, p in pats.items():
            if re.search(p, l):
                count[k] += 1
    fails = collections.Counter()
    for l in g:
        m = re.search(r"could not give .*: (.*)$", l)
        if m:
            fails[m.group(1)] += 1
    heard = collections.Counter()
    for l in g:
        m = re.search(r"\[guild\] hears: (.*)$", l)
        if m:
            heard[re.sub(r"[A-Z][a-z]+\d*", "X", m.group(1))] += 1

    # Samples: each bot's level and strength over the wall's time.
    samples = collections.defaultdict(list)
    path = os.path.join(run, "guild_samples.tsv")
    if os.path.exists(path):
        for row in open(path).read().splitlines()[1:]:
            f = row.split("\t")
            if len(f) < 10:
                continue
            samples[f[1]].append((int(f[0]), int(f[3]), float(f[4]), f[6], f[7] == "1", int(f[8]), f[9] == "1"))
    # Growth per game hour in each band, in a guild or not over the interval.
    grow = collections.defaultdict(lambda: [0.0, 0.0, 0.0])  # (band, inGuild) -> [levels, strength, hours]
    for name, rows in samples.items():
        rows.sort()
        for a, b in zip(rows, rows[1:]):
            if a[6]:  # traders: the market's, not the fight's
                continue
            hours = (b[0] - a[0]) * speed / 3600.0
            if hours <= 0 or hours > 3:
                continue
            key = (band(a[1]), a[3] != "-" and b[3] == a[3])
            grow[key][0] += b[1] - a[1]
            grow[key][1] += b[2] - a[2]
            grow[key][2] += hours
    # Each guild at the last sample: members, mean level, power.
    last = {}
    for name, rows in samples.items():
        last[name] = max(rows)
    guilds = collections.defaultdict(list)
    for name, r in last.items():
        guilds[r[3]].append((name, r))
    first_wall = min((r[0] for rows in samples.values() for r in rows), default=0)
    last_wall = max((r[0] for rows in samples.values() for r in rows), default=0)

    out = ["# Guild bot run: %s" % os.path.basename(run.rstrip("/")), ""]
    out.append("Sampled over %.1f real hours, about %.0f game hours at --speed %d." % (
        (last_wall - first_wall) / 3600.0, (last_wall - first_wall) * speed / 3600.0, speed))
    out += ["", "## What the guilds did", "", "| event | count |", "|---|---|"]
    for k in pats:
        out.append("| %s | %d |" % (k, count[k]))
    if fails:
        out += ["", "Gifts that failed, by why:", ""]
        for why, n in fails.most_common():
            out.append("- %s: %d" % (why, n))
    if heard:
        out += ["", "What the server told the bots on the guild channel (names as X):", ""]
        for t, n in heard.most_common(15):
            out.append("- %d x %s" % (n, t))
    out += ["", "## The guilds at the end", "", "| guild | members | mean level | mean strength | power (sum) |", "|---|---|---|---|---|"]
    for gname, members in sorted(guilds.items(), key=lambda kv: -sum(r[2] for _, r in kv[1])):
        lv = sum(r[1] for _, r in members) / len(members)
        st = sum(r[2] for _, r in members) / len(members)
        label = "(guildless)" if gname == "-" else gname
        out.append("| %s | %d | %.0f | %.0f | %.0f |" % (label, len(members), lv, st, sum(r[2] for _, r in members)))
    out += ["", "## Growth: guild members against the guildless, at the same levels", "",
            "Levels and strength gained per game hour, over each pair of samples, by the level the bot had at the first. "
            "Traders left out.", "", "| levels | in a guild: lv/h | str/h | game h | guildless: lv/h | str/h | game h |",
            "|---|---|---|---|---|---|---|"]
    for b in sorted({k[0] for k in grow}, key=lambda s: int(s.split("-")[0].rstrip("+"))):
        cells = []
        for inside in (True, False):
            lv, st, h = grow[(b, inside)]
            cells += ["%.2f" % (lv / h) if h else "-", "%.1f" % (st / h) if h else "-", "%.0f" % h]
        out.append("| %s | %s |" % (b, " | ".join(cells)))
    out += ["", "## Guild parties formed", "", "%d party lines in the logs; the last few:" % len(board), ""]
    for l in board[-10:]:
        out.append("    " + l.strip())
    text = "\n".join(out) + "\n"
    with open(os.path.join(run, "guild_report.md"), "w") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "sample":
        sample(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 60)
    elif len(sys.argv) >= 3 and sys.argv[1] == "report":
        report(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 100)
    else:
        print(__doc__)
        sys.exit(2)
