#!/usr/bin/env python3
"""The performance sweep: walk a world for its bad places, flag regressions, name hitches.

    python3 tools/perfsweep.py --world losttower --preset light
    python3 tools/perfsweep.py --world losttower,dungeon --preset light --update-baseline
    python3 tools/perfsweep.py --world lorencia --preset full
    python3 tools/perfsweep.py --report build/perfsweep/<run>      # re-read a run, no launch

Each world is two launches of build/mu2 with the world loaded once each: a fight pass (the
map's monsters, the hero at --level 400 hitting back at what is within six tiles) and a
--peaceful pass (no monsters), so "room cost" and "monster cost" come apart. Inside a launch
the game's --sweep mode (src/app/sweep.cpp) puts the hero down on every tile in turn, settles,
measures, and writes a JSON row a tile; it repeats the whole list --passes times, and the
spread of a tile's pass means is the noise that tile is judged against.

Presets: light is tools/perfsweep_spots.json's hand list alone (about a minute a launch);
full adds every walkable tile on a 16-tile lattice, read off the cooked grid by the game.

Baselines are tools/perf/baseline_<world>.json, merged per tile by --update-baseline. A run
flags a tile over 5.5 ms mean, a tile slower than its baseline by more than the spread (never
under 0.1 ms, and 0.2 when a single pass gives no spread), and hitch causes the baseline never
saw. docs/budget.md, "The sweep", says how to read the report.
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "build", "mu2")
SPOTS = os.path.join(ROOT, "tools", "perfsweep_spots.json")
BASELINES = os.path.join(ROOT, "tools", "perf")
BUDGET_MS = 5.5
# A frame this long is the machine, not the game (docs/budget.md: one frame in 570 at about a
# second, a hitch that lands in some segments and not others). A pass holding one is left out
# of the tile's mean when another pass is there to stand for it, and the report says so.
STALL_MS = 250.0
# A hitch with nothing logged or happening on it, under this many times the mean, is the
# drawable wait's second hump and is counted rather than listed.
LIST_BARE_OVER = 4.0
# A hitch is put down to its cause when it is this far over the mean or something was logged
# on it. Below that, a fight's tick lands on one frame in ten anyway, so a "hit hero" beside a
# 2x frame is as likely the hump as the blow.
ATTRIBUTE_OVER = 3.0
# What a tile must move, beyond its launch's common drift, to be called slower: the noise floor
# a quiet machine gives between launches (docs/budget.md, 0.2 to 2 ms), never less.
CHANGE_FLOOR = 0.2
MODES = ("fight", "peaceful")

PRESETS = {
    # passes, settle frames, measured frames, grid step (0 none)
    "light": dict(passes=3, settle=90, frames=400, grid=0),
    "full": dict(passes=1, settle=90, frames=300, grid=16),
}


def say(*a):
    print(*a, flush=True)


# ---- launching -------------------------------------------------------------------------

def busy():
    """What else holds the machine: the load average, any other mu2 window, and anything over
    a third of a core. A measurement's conditions, said beside it (docs/budget.md)."""
    others = []
    try:
        out = subprocess.run(["ps", "-Ao", "pid=,pcpu=,comm="], capture_output=True,
                             text=True).stdout
    except OSError:
        out = ""
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3 or int(parts[0]) == os.getpid():
            continue
        cpu, name = float(parts[1]), os.path.basename(parts[2])
        # The window server and this agent are always there; their share is the baseline's too.
        if name in ("WindowServer", "kernel_task", "claude", "ps"):
            continue
        if name == "mu2" or cpu > 33.0:
            others.append(f"{name} {cpu:.0f}%")
    return os.getloadavg()[0], others


def launch(world, mode, spots, opts, run_dir):
    """One process: the world loaded once, every tile measured `passes` times."""
    tag = f"{world}_{mode}"
    spots_path = os.path.join(run_dir, f"spots_{tag}.json")
    out_path = os.path.join(run_dir, f"rows_{tag}.jsonl")
    log_path = os.path.join(run_dir, f"mu2_{tag}.log")
    save_dir = os.path.join(run_dir, f"save_{tag}")
    os.makedirs(save_dir, exist_ok=True)
    with open(spots_path, "w") as f:
        json.dump({"settle": opts["settle"], "frames": opts["frames"],
                   "passes": opts["passes"], "fight": mode == "fight",
                   "grid": opts["grid"],
                   "spots": [{"name": s["name"], "at": s["at"]} for s in spots]}, f, indent=1)
    cmd = ["nice", BINARY, "--world", world, "--play", "--fresh",
           "--save", os.path.join(save_dir, "hero.json"), "--level", str(opts["level"]),
           "--no-vsync", "--mute", "--frames", "100000000",
           "--width", str(opts["width"]), "--height", str(opts["height"]),
           "--sweep", spots_path, "--sweep-out", out_path, "--log", log_path]
    if mode == "peaceful":
        cmd.append("--peaceful")
    say(f"  {world} {mode}: launching ({len(spots)} hand tiles"
        f"{', grid every %d' % opts['grid'] if opts['grid'] else ''}, {opts['passes']} passes)")
    before = busy()
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    after = busy()
    with open(os.path.join(run_dir, f"busy_{tag}.json"), "w") as f:
        json.dump({"before": before, "after": after}, f)
    if proc.returncode not in (0, 1):
        say(f"  {world} {mode}: mu2 exited {proc.returncode}; see {log_path}")
    return out_path, log_path


# ---- reading ---------------------------------------------------------------------------

def read_log(log_path):
    info = {"resolution": None, "errors": []}
    if not os.path.exists(log_path):
        return info
    with open(log_path, errors="replace") as f:
        for line in f:
            m = re.search(r"Metal, (\d+x\d+)", line)
            if m and not info["resolution"]:
                info["resolution"] = m.group(1)
            if "ERROR" in line and "save:" not in line:
                info["errors"].append(line.strip())
    return info


def read_rows(path):
    header, rows = None, []
    if not os.path.exists(path):
        return header, rows
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("header"):
                header = row
            else:
                rows.append(row)
    return header, rows


def key_of(row):
    return row["spot"] if row["spot"] else f"{row['asked'][0]},{row['asked'][1]}"


def cause_kinds(cause):
    """A hitch cause reduced to what it was about, numbers out, so runs can be compared."""
    if not cause:
        return set()
    kinds = set()
    for part in cause.split("; "):
        part = part.replace("(frame before) ", "")
        part = re.sub(r" x\d+$", "", part)
        part = re.sub(r"[-+]?\d+(\.\d+)?", "#", part)
        if part.startswith("log: "):
            part = " ".join(part.split()[:5])
        kinds.add(part)
    return kinds


def aggregate(rows):
    """Per tile across passes: mean of means, spread, p99, worst, hitches."""
    by = {}
    for r in rows:
        by.setdefault(key_of(r), []).append(r)
    out = {}
    for key, rs in by.items():
        clean = [r for r in rs if r["worst"] < STALL_MS]
        stalled = [r for r in rs if r["worst"] >= STALL_MS]
        means = [r["mean"] for r in (clean if clean else rs)]
        hitches = []
        for r in rs:
            for h in r["hitches"]:
                hitches.append(dict(h, pass_=r["pass"], mean=r["mean"]))
        hitches.sort(key=lambda h: -h["ms"])
        for h in hitches:
            h["attributed"] = bool(h.get("cause")) and (
                h["x"] >= ATTRIBUTE_OVER or "log: " in h["cause"])
        out[key] = {
            "name": rs[0]["spot"], "hot": rs[0]["hot"], "asked": rs[0]["asked"],
            "tile": rs[0]["tile"], "passes": len(rs),
            "mean": sum(means) / len(means),
            "spread": (max(means) - min(means)) if len(means) > 1 else None,
            "pass_means": means,
            "stalls": [{"pass_": r["pass"], "ms": r["worst"], "mean": r["mean"]} for r in stalled],
            "dropped": len(stalled) if clean else 0,
            "p99": max(r["p99"] for r in rs),
            "worst": max(r["worst"] for r in rs),
            "draws": sum(r["draws"] for r in rs) / len(rs),
            "tris": sum(r["tris"] for r in rs) / len(rs),
            "tick_ms": sum(r["tick_ms"] for r in rs) / len(rs),
            "awake": max(r["awake"] for r in rs),
            "near": sum(r["near"] for r in rs) / len(rs),
            "blows": sum(r["blows"] for r in rs),
            "kills": sum(r["kills"] for r in rs),
            "deaths": sum(r["deaths"] for r in rs),
            "settle_worst": max(r["settle_worst"] for r in rs),
            "settle_cause": next((r["settle_cause"] for r in
                                  sorted(rs, key=lambda r: -r["settle_worst"])), None),
            "hitches": hitches,
            "hitch_kinds": sorted(set().union(
                set(), *[cause_kinds(h["cause"]) for h in hitches if h["attributed"]])),
        }
    return out


# ---- judging ---------------------------------------------------------------------------

def threshold(now, base):
    """What a difference must clear to be called one: the larger spread, never under 0.1,
    and 0.2 (the quiet machine's floor, docs/budget.md) when no spread was measured."""
    spreads = [s for s in (now.get("spread"), base.get("spread")) if s is not None]
    return max(max(spreads) if spreads else 0.0, CHANGE_FLOOR)


def judge(world, tiles, baseline):
    flags = []
    for mode in MODES:
        base_mode = (baseline or {}).get("spots", {}).get(mode, {})
        # A whole launch moved: most tiles the same way by more than their spread. One launch is
        # one stretch of the machine, and a slow stretch (a scanner, an indexer, another
        # session's build) moves every tile in it together; code moves the tiles it touches.
        deltas = [t["mean"] - base_mode[k]["mean"] for k, t in tiles.get(mode, {}).items()
                  if k in base_mode]
        shifted = False
        middle = 0.0
        if len(deltas) >= 3:
            ups = sorted(deltas)
            middle = ups[len(ups) // 2]
            same = sum(1 for d in deltas if (d > 0) == (middle > 0) and abs(d) > 0.2)
            if same >= 0.8 * len(deltas) and abs(middle) > 0.3:
                shifted = True
                flags.append(("launch-shift", world, mode, "all",
                              f"{same} of {len(deltas)} tiles moved together, median "
                              f"{middle:+.2f} ms: the machine changed, or something every "
                              "tile draws did -- re-run; if it holds, it is the code"))
        for key, t in tiles.get(mode, {}).items():
            t["delta"] = None
            if t["mean"] > BUDGET_MS:
                flags.append(("over", world, mode, key,
                              f"{t['mean']:.2f} ms mean, over the {BUDGET_MS} budget"))
            b = base_mode.get(key)
            if not b:
                continue
            t["delta"] = t["mean"] - b["mean"]
            t["threshold"] = threshold(t, b)
            # Judged against the launch's common drift: one launch is one stretch of the
            # machine, and every tile in it moves together with it. A tile is slower when it
            # moved past the others by more than its noise.
            own = t["delta"] - middle
            if own > t["threshold"]:
                flags.append(("slower", world, mode, key,
                              f"{t['mean']:.2f} ms against {b['mean']:.2f} "
                              f"(+{t['delta']:.2f}; +{own:.2f} past the launch's "
                              f"{middle:+.2f} drift, over the {t['threshold']:.2f} noise)"))
            new = sorted(set(t["hitch_kinds"]) - set(b.get("hitch_kinds", [])))
            if new:
                flags.append(("new-hitch", world, mode, key, "; ".join(new[:4])))
    # Monsters cannot make a tile cheaper. Peaceful slower than fight on most tiles is the two
    # launches having run on two different machines, as far as the frame is concerned: found
    # 2026-10-01, the Dungeon's peaceful launch at 6.8 ms against 4.2 by hand, under an indexer.
    fight, calm = tiles.get("fight", {}), tiles.get("peaceful", {})
    both = [k for k in fight if k in calm]
    worse = [k for k in both if calm[k]["mean"] > fight[k]["mean"] + 0.3]
    if len(both) >= 3 and len(worse) >= 0.8 * len(both):
        flags.append(("launch-shift", world, "peaceful", "all",
                      f"peaceful slower than fight on {len(worse)} of {len(both)} tiles; the "
                      "two launches did not see the same machine -- re-run"))
    return flags


# ---- the heat map ----------------------------------------------------------------------

def colour(ms):
    """Diverging about the budget: blue well under it, grey at 5.5, red well over."""
    blue, grey, red = (0x2a, 0x78, 0xd6), (0xf0, 0xef, 0xec), (0xe3, 0x49, 0x48)
    lo, hi = 3.5, 7.5
    if ms <= BUDGET_MS:
        t = max(0.0, (ms - lo) / (BUDGET_MS - lo))
        a, b = blue, grey
    else:
        t = min(1.0, (ms - BUDGET_MS) / (hi - BUDGET_MS))
        a, b = grey, red
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def heat_map(world, header, tiles, mode, path):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        say("  (Pillow missing: no heat map)")
        return None
    size = header["size"]
    step = header["mask_step"]
    scale = 3
    legend_h = 70
    img = Image.new("RGB", (size * scale, size * scale + legend_h), (0x1a, 0x1a, 0x19))
    d = ImageDraw.Draw(img)
    for r, line in enumerate(header["mask"]):
        for c, ch in enumerate(line):
            if ch == "1":
                x, y = c * step * scale, r * step * scale
                d.rectangle([x, y, x + step * scale - 1, y + step * scale - 1],
                            fill=(0x38, 0x38, 0x35))
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:
        font = ImageFont.load_default()
    grid = header.get("grid_step") or 16
    # Grid tiles as blocks, hand tiles as rings on top with their names.
    for t in tiles.values():
        if t["hot"]:
            continue
        c, r = t["asked"]
        x0, y0 = (c - grid // 2) * scale, (r - grid // 2) * scale
        d.rectangle([x0 + 1, y0 + 1, x0 + grid * scale - 2, y0 + grid * scale - 2],
                    fill=colour(t["mean"]))
    for t in tiles.values():
        if not t["hot"]:
            continue
        c, r = t["asked"]
        x, y = c * scale, r * scale
        d.ellipse([x - 9, y - 9, x + 9, y + 9], fill=(0x1a, 0x1a, 0x19))
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=colour(t["mean"]))
        label = f"{t['name']} {t['mean']:.2f}"
        tx = min(max(2, x + 11), size * scale - 8 * len(label))
        box = d.textbbox((tx, y - 8), label, font=font)
        d.rectangle([box[0] - 3, box[1] - 2, box[2] + 3, box[3] + 2], fill=(0x1a, 0x1a, 0x19))
        d.text((tx, y - 8), label, fill=(0xf0, 0xef, 0xec), font=font)
    # The legend: the scale, with the budget marked.
    y0 = size * scale + 12
    x0, x1 = 20, size * scale - 20
    for x in range(x0, x1):
        ms = 3.5 + (x - x0) / (x1 - x0) * 4.0
        d.line([x, y0, x, y0 + 14], fill=colour(ms))
    for ms in (3.5, 4.5, 5.5, 6.5, 7.5):
        x = x0 + (ms - 3.5) / 4.0 * (x1 - x0)
        d.line([x, y0 + 14, x, y0 + 19], fill=(0xc0, 0xbf, 0xbb))
        d.text((x - 12, y0 + 21), f"{ms:.1f}", fill=(0xc0, 0xbf, 0xbb), font=font)
    d.text((x0, y0 + 40), f"{world}, {mode}: mean wall frame ms at {header['width']}x"
           f"{header['height']}; budget {BUDGET_MS}", fill=(0xf0, 0xef, 0xec), font=font)
    img.save(path)
    return path


# ---- the report ------------------------------------------------------------------------

def fmt_delta(t):
    if t.get("delta") is None:
        return "new"
    mark = " !" if t["delta"] > t.get("threshold", 9) else ""
    return f"{t['delta']:+.2f}{mark}"


def report(run_dir, results, flags, conditions):
    lines = [f"# Performance sweep {os.path.basename(run_dir)}", ""]
    lines.append(f"Resolution {conditions['resolution']}, vsync off, Release, `--level "
                 f"{conditions['level']}`; load average {conditions['load']:.1f} at the start"
                 + (f"; **also running: {', '.join(conditions['others'])}**"
                    if conditions["others"] else "") + ".")
    lines.append(f"Preset {conditions['preset']}: {conditions['passes']} passes, "
                 f"{conditions['settle']} settling + {conditions['frames']} measured frames a "
                 "tile. Mean of pass means; spread is max-min of the pass means.")
    lines.append("")
    lines.append("## Flags")
    lines.append("")
    if not flags:
        lines.append("None.")
    order = {"launch-shift": 0, "slower": 1, "over": 2, "new-hitch": 3}
    for kind, world, mode, key, text in sorted(flags, key=lambda f: order[f[0]]):
        lines.append(f"- **{kind}** {world} {key} ({mode}): {text}")
    regressions = [f for f in flags if f[0] == "slower"]
    lines.append("")
    lines.append("No regressions within the spread." if not regressions
                 else f"**{len(regressions)} regression(s).**")
    for world in results:
        for mode in MODES:
            try:
                with open(os.path.join(run_dir, f"busy_{world}_{mode}.json")) as f:
                    b = json.load(f)
                lines.append(f"- {world} {mode} launch: load {b['before'][0]:.1f} before, "
                             f"{b['after'][0]:.1f} after; busy: "
                             f"{', '.join(b['before'][1] + b['after'][1]) or 'nothing else'}")
            except (OSError, ValueError, KeyError):
                pass
    lines.append("")
    lines.append("## Tiles, worst first")
    lines.append("")
    lines.append("| world | tile | at | fight ms | spread | p99 | worst | vs base | peaceful ms "
                 "| monsters | draws | tris | sim ms/tick | awake | near | blows | hitches |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    table = []
    for world, by_mode in results.items():
        fight, calm = by_mode["tiles"].get("fight", {}), by_mode["tiles"].get("peaceful", {})
        for key in sorted(set(fight) | set(calm)):
            f, p = fight.get(key), calm.get(key)
            lead = f or p
            table.append((max(x["mean"] for x in (f, p) if x), world, key, f, p, lead))
    table.sort(key=lambda r: -r[0])
    for _, world, key, f, p, lead in table:
        sp = lambda t: "-" if not t or t["spread"] is None else f"{t['spread']:.2f}"
        row = [world, key, f"{lead['tile'][0]},{lead['tile'][1]}",
               f"{f['mean']:.2f}" if f else "-", sp(f),
               f"{f['p99']:.2f}" if f else "-", f"{f['worst']:.2f}" if f else "-",
               fmt_delta(f) if f else "-",
               (f"{p['mean']:.2f} ({fmt_delta(p)})" if p else "-"),
               (f"{f['mean'] - p['mean']:+.2f}" if f and p else "-"),
               f"{lead['draws']:.0f}", f"{lead['tris'] / 1e6:.2f}M",
               f"{f['tick_ms']:.3f}" if f else "-",
               str(f["awake"]) if f else "-", f"{f['near']:.1f}" if f else "-",
               str(f["blows"]) if f else "-",
               str(len(f["hitches"]) + (len(p["hitches"]) if p else 0)) if f
               else str(len(p["hitches"]))]
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")
    lines.append("`monsters` is fight minus peaceful: what the monsters and the fight cost on "
                 "that tile. `vs base` is against tools/perf/baseline_<world>.json; `!` "
                 "marks a change past the spread.")
    lines.append("")
    lines.append("## Hitches (frames over twice their tile's mean)")
    lines.append("")
    lines.append(f"Listed: hitches put down to a cause (over {ATTRIBUTE_OVER:.0f}x with a "
                 f"happening, or anything logged on the frame), and any over "
                 f"{LIST_BARE_OVER:.0f}x. The rest are counted: on this Mac a 2-3x frame is the "
                 "drawable wait landing twice (docs/budget.md, the two humps), and in a fight a "
                 "tick falls on one frame in ten whether or not it is the cause.")
    lines.append("")
    any_hitch = False
    for world, by_mode in results.items():
        for mode in MODES:
            for key, t in by_mode["tiles"].get(mode, {}).items():
                bare = [h for h in t["hitches"]
                        if not h["attributed"] and h["x"] < LIST_BARE_OVER]
                shown = [h for h in t["hitches"] if h not in bare]
                for h in shown[:6]:
                    any_hitch = any_hitch or h["attributed"]
                    lines.append(f"- {world} {key} ({mode}), pass {h['pass_'] + 1} frame "
                                 f"{h['frame']}: {h['ms']:.2f} ms ({h['x']:.1f}x the "
                                 f"{h['mean']:.2f} mean) -- "
                                 f"{h.get('cause') or 'nothing logged or happened that frame'}")
                if bare:
                    ticked = sum(1 for h in bare if h.get("cause"))
                    lines.append(f"- {world} {key} ({mode}): {len(bare)} unattributed frame(s) "
                                 f"at {min(h['x'] for h in bare):.1f}-"
                                 f"{max(h['x'] for h in bare):.1f}x, worst "
                                 f"{max(h['ms'] for h in bare):.2f} ms; nothing logged"
                                 + (f", {ticked} with an ordinary tick beside them"
                                    if ticked else ""))
                for st in t["stalls"]:
                    lines.append(f"- {world} {key} ({mode}): **machine stall** in pass "
                                 f"{st['pass_'] + 1}, a {st['ms']:.0f} ms frame (that pass's "
                                 f"mean {st['mean']:.2f}); "
                                 + ("left out of the tile's mean" if t["dropped"]
                                    else "no clean pass to stand for it -- re-run"))
    if not any_hitch:
        lines.append("")
        lines.append("**No hitch could be put down to a cause**: nothing was logged on any "
                     f"frame over twice its tile's mean, and none over {ATTRIBUTE_OVER:.0f}x had "
                     "a happening in the realm beside it.")
    lines.append("")
    lines.append("## Arrival (the worst settling frame after the hero is put down)")
    lines.append("")
    for world, by_mode in results.items():
        for key, t in sorted(by_mode["tiles"].get("fight", {}).items(),
                             key=lambda kv: -kv[1]["settle_worst"])[:5]:
            cause = (t["settle_cause"] or "nothing logged")[:200]
            lines.append(f"- {world} {key}: {t['settle_worst']:.1f} ms -- {cause}")
    lines.append("")
    lines.append("## Heat maps")
    lines.append("")
    for world, by_mode in results.items():
        for p in by_mode.get("maps", []):
            lines.append(f"- {p}")
    path = os.path.join(run_dir, "report.md")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return path


def summary(results, flags, conditions):
    say("")
    say(f"== sweep at {conditions['resolution']}, vsync off, load {conditions['load']:.1f}"
        + (f" (ALSO RUNNING: {len(conditions['others'])} other process(es) -- numbers "
           "contended)" if conditions["others"] else ""))
    rows = []
    for world, by_mode in results.items():
        fight, calm = by_mode["tiles"].get("fight", {}), by_mode["tiles"].get("peaceful", {})
        for key in set(fight) | set(calm):
            f, p = fight.get(key), calm.get(key)
            rows.append((max(x["mean"] for x in (f, p) if x), world, key, f, p))
    rows.sort(key=lambda r: -r[0])
    say(f"{'world':10} {'tile':14} {'at':8} {'fight':>6} {'spread':>6} {'p99':>6} "
        f"{'vs base':>8} {'calm':>6} {'mons':>6} {'hitch':>5}")
    for top, world, key, f, p in rows[:25]:
        lead = f or p
        say(f"{world:10} {key[:14]:14} {lead['tile'][0]:>3},{lead['tile'][1]:<4} "
            f"{(f'%.2f' % f['mean']) if f else '-':>6} "
            f"{(f'%.2f' % f['spread']) if f and f['spread'] is not None else '-':>6} "
            f"{(f'%.2f' % f['p99']) if f else '-':>6} {fmt_delta(f) if f else '-':>8} "
            f"{(f'%.2f' % p['mean']) if p else '-':>6} "
            f"{(f'%+.2f' % (f['mean'] - p['mean'])) if f and p else '-':>6} "
            f"{len(f['hitches']) if f else 0:>5}"
            + ("  OVER" if top > BUDGET_MS else ""))
    regressions = [x for x in flags if x[0] == "slower"]
    for kind, world, mode, key, text in flags:
        if kind != "over":
            say(f"  {kind}: {world} {key} ({mode}): {text}")
    say("no regressions within the spread" if not regressions
        else f"{len(regressions)} REGRESSION(S)")


# ---- the baseline ----------------------------------------------------------------------

def baseline_path(world):
    return os.path.join(BASELINES, f"baseline_{world}.json")


def load_baseline(world):
    try:
        with open(baseline_path(world)) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_baseline(world, tiles, conditions):
    base = load_baseline(world) or {"world": world, "spots": {}}
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    base.update({"taken": datetime.date.today().isoformat(), "commit": commit,
                 "resolution": conditions["resolution"], "preset": conditions["preset"],
                 "level": conditions["level"], "load": round(conditions["load"], 1)})
    for mode in MODES:
        dst = base["spots"].setdefault(mode, {})
        for key, t in tiles.get(mode, {}).items():
            dst[key] = {"tile": t["tile"], "mean": round(t["mean"], 3),
                        "spread": None if t["spread"] is None else round(t["spread"], 3),
                        "p99": round(t["p99"], 3), "draws": round(t["draws"]),
                        "hitch_kinds": t["hitch_kinds"]}
    os.makedirs(BASELINES, exist_ok=True)
    with open(baseline_path(world), "w") as f:
        json.dump(base, f, indent=1, sort_keys=True)
        f.write("\n")
    return baseline_path(world)


# ---- main ------------------------------------------------------------------------------

def collect(world, run_dir):
    tiles, header, info = {}, None, {"resolution": None, "errors": []}
    for mode in MODES:
        h, rows = read_rows(os.path.join(run_dir, f"rows_{world}_{mode}.jsonl"))
        if h:
            header = header or h
            tiles[mode] = aggregate(rows)
        li = read_log(os.path.join(run_dir, f"mu2_{world}_{mode}.log"))
        info["resolution"] = info["resolution"] or li["resolution"]
        info["errors"] += li["errors"]
    return tiles, header, info


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--world", help="one or more worlds, comma separated")
    ap.add_argument("--preset", choices=sorted(PRESETS), default="light")
    ap.add_argument("--passes", type=int)
    ap.add_argument("--frames", type=int)
    ap.add_argument("--settle", type=int)
    ap.add_argument("--grid", type=int, help="grid step in tiles (full preset: 16)")
    ap.add_argument("--level", type=int, default=400)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--only", choices=MODES, help="one launch instead of both")
    ap.add_argument("--out", help="run directory (default build/perfsweep/<stamp>)")
    ap.add_argument("--report", metavar="RUN_DIR", help="re-read a finished run; no launch")
    ap.add_argument("--update-baseline", action="store_true",
                    help="merge this run's tiles into tools/perf/baseline_<world>.json")
    args = ap.parse_args()

    opts = dict(PRESETS[args.preset])
    for k in ("passes", "frames", "settle", "grid"):
        if getattr(args, k) is not None:
            opts[k] = getattr(args, k)
    opts.update(level=args.level, width=args.width, height=args.height)

    with open(SPOTS) as f:
        hand = json.load(f)
    load, others = busy()
    if args.report:
        run_dir = os.path.abspath(args.report)
        try:
            with open(os.path.join(run_dir, "run.json")) as f:
                kept = json.load(f)
            load, others = kept["load"], kept["others"]
        except (OSError, ValueError, KeyError):
            load, others = float("nan"), []
        worlds = sorted({m.group(1) for n in os.listdir(run_dir)
                         for m in [re.match(r"rows_(.+)_(fight|peaceful)\.jsonl$", n)] if m})
        if args.world:
            worlds = [w for w in worlds if w in args.world.split(",")]
    else:
        if not args.world:
            ap.error("--world is required (or --report RUN_DIR)")
        worlds = args.world.split(",")
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        run_dir = os.path.abspath(args.out or os.path.join(ROOT, "build", "perfsweep",
                                                           f"{stamp}-{args.preset}"))
        os.makedirs(run_dir, exist_ok=True)
        if not os.path.exists(BINARY):
            sys.exit(f"no {BINARY}; build it first")
        say(f"sweep -> {run_dir}")
        with open(os.path.join(run_dir, "run.json"), "w") as f:
            json.dump({"load": load, "others": others}, f)
        if others:
            say(f"WARNING: load {load:.1f}, also running: " + "; ".join(o[:80] for o in others))
        for world in worlds:
            spots = hand.get(world, [])
            if not spots and not opts["grid"]:
                say(f"  {world}: no hand tiles in {SPOTS} and no grid; skipped")
                continue
            for mode in MODES:
                if args.only and mode != args.only:
                    continue
                launch(world, mode, spots, opts, run_dir)

    results, flags, resolution = {}, [], None
    conditions = dict(preset=args.preset, load=load, others=[o[:60] for o in others], **opts)
    for world in worlds:
        tiles, header, info = collect(world, run_dir)
        if not header:
            say(f"  {world}: no rows written; see the mu2_{world}_*.log in {run_dir}")
            continue
        header["grid_step"] = opts["grid"]
        resolution = resolution or info["resolution"]
        for e in info["errors"][:5]:
            say(f"  {world}: {e}")
        flags += judge(world, tiles, load_baseline(world))
        maps = []
        for mode in MODES:
            if tiles.get(mode):
                p = heat_map(world, header, tiles[mode], mode,
                             os.path.join(run_dir, f"heat_{world}_{mode}.png"))
                if p:
                    maps.append(p)
        results[world] = {"tiles": tiles, "maps": maps}
        conditions.update(passes=header["passes"], settle=header["settle"],
                          frames=header["frames"], level=header["level"])
    conditions["resolution"] = resolution or "unknown"
    if not results:
        sys.exit(1)
    path = report(run_dir, results, flags, conditions)
    summary(results, flags, conditions)
    say(f"report: {path}")
    for world in results:
        for p in results[world]["maps"]:
            say(f"heat map: {p}")
    if args.update_baseline:
        for world, r in results.items():
            say(f"baseline: {write_baseline(world, r['tiles'], conditions)}")
    sys.exit(1 if any(f[0] == "slower" for f in flags) else 0)


if __name__ == "__main__":
    main()
