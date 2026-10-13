"""A netbot run's story up to a game hour: levels, deaths, shops, quests, parties, loops.

    python3 tools/botwatch/analyze.py HOURS saves/botwatch/RUN [saves/botwatch/OTHER ...]
"""
import collections
import glob
import os
import re
import statistics
import sys

LINE = re.compile(r"^\s+\[(\d+):(\d\d):(\d\d)\] (\w+) (.*)$")


def analyze(run, hours):
    limit = hours * 3600
    level, clock, deaths, killers = {}, {}, collections.Counter(), collections.Counter()
    unreach = nopot = 0
    taken, handed, parties, loops = collections.Counter(), collections.Counter(), 0, collections.Counter()
    buys = 0
    files = [f for f in sorted(glob.glob(run + "/netbot-*.log")) or [run + "/netbot.log"] if os.path.exists(f)]
    if not files:
        print(f"{run.rstrip('/').split('/')[-1]}: no netbot logs")
        return
    for path in files:
        for raw in open(path, errors="replace"):
            if "[board] a party for" in raw:
                parties += 1
            if raw.startswith("  [broker]") and " buys " in raw:
                buys += 1
            m = LINE.match(raw)
            if not m:
                continue
            h, mi, s, who, rest = m.groups()
            t = int(h) * 3600 + int(mi) * 60 + int(s)
            if t > limit:
                continue
            clock[who] = max(clock.get(who, 0), t)
            for pat in (r"in \w+ as #\d+, .*? level (\d+)", r"^level (\d+)[ ,(]"):
                if lv := re.match(pat, rest):
                    level[who] = int(lv.group(1))
            if rest.startswith("killed by"):
                deaths[who] += 1
                killers[rest[10:].split(";")[0]] += 1
            nopot += "no potions" in rest
            unreach += rest.startswith("could not reach")
            if q := re.match(r"takes (.+?) from", rest):
                taken[q.group(1)] += 1
            if q := re.match(r"\*\* hands in (.+?) to", rest):
                handed[q.group(1)] += 1
            if not rest.startswith(("party:", "~ ")):
                loops[(who, re.sub(r"\d+", "N", rest)[:60])] += 1
    levels = sorted(level.values())
    print(f"{run.split('/')[-1]} up to game hour {hours:g}: {len(levels)} bots")
    print(f"  level median {statistics.median(levels):.0f}, low {levels[0]}, high {levels[-1]}")
    print(f"  deaths {sum(deaths.values())} ({sum(deaths.values()) / max(1, len(levels)):.1f} a bot), "
          f"no-potion lines {nopot}, could not reach {unreach}")
    print(f"  killers {killers.most_common(5)}")
    print(f"  quests taken {dict(taken)}")
    print(f"  handed in {dict(handed)}")
    print(f"  quest parties {parties}, broker buys {buys} (whole run)")
    print(f"  worst loops {[(n, w, t) for (w, t), n in loops.most_common(3)]}")


for run in sys.argv[2:]:
    analyze(run, float(sys.argv[1]))
