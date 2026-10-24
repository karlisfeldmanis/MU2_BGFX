#!/usr/bin/env python3
"""Skills, talents, icons and gear powers for the website item database, out of the game's own code.

    cd MU2_BGFX && nice python3 <scratch>/gen_systems_b.py [--out DIR]

How it mirrors the game rather than restating it:
  * sysb/dump.cpp is compiled against the repo's sources (read-only: src/sim/skills.cpp,
    talents.cpp + talents_table.inc, items.cpp, rules.cpp, maps.cpp, swings.cpp,
    content/tables.cpp ...) into the scratchpad, and run on the cooked tables
    (out/assets/cooked/lorencia/lorencia.mur). It prints every SkillRow, the talent tables,
    talentPoints(), petPower / wingPower / wingDefense / asks, affixValue, furyChance and
    ringStingChance as JSON. So every number below is what the sim computes.
  * The words follow the cards: game/ui/desk.cpp Desk::skillSheet (skill key card),
    game/ui/describe.cpp spellLines / selfLines, game/ui/talent_window.cpp effect()/figure().
    Where a card shows a number off the viewer's own stats, the formula is given instead.
  * learned_from comes from content/items/*/*.json `skill` blocks (fresher than the cook).
  * Icons: the game's own decodes of MU's sheets (content/interface/skills/skill_<n>.png, cut from
    MuDream's newui_skill by pipeline/skill_icons.py; content/interface/talents/talent_icons.png,
    MuMain's new_Master_Icon.OZJ by pipeline/talent_icons.py), cut at the talent window's
    arithmetic (cell = icon - 300, 25 a row, 20x28) and scaled to 64 px tall.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path.cwd()
HERE = Path(__file__).resolve().parent
SYSB = HERE / "sysb"
TABLES = ROOT / "out/assets/cooked/lorencia/lorencia.mur"

KIN_NAMES = {"DK": "Dark Knight", "DW": "Dark Wizard", "Elf": "Fairy Elf", "MG": "Magic Gladiator"}
SECOND = {"DK": "Blade Knight", "DW": "Soul Master", "Elf": "Muse Elf", "MG": None}
ELEMENTS = ["none", "ice", "poison", "lightning", "fire", "wind"]
TICK = 0.05


def dump():
    exe = SYSB / "dump"
    srcs = ["src/content/tables.cpp", "src/sim/skills.cpp", "src/sim/talents.cpp", "src/sim/items.cpp",
            "src/sim/rules.cpp", "src/sim/maps.cpp", "src/sim/swings.cpp", "src/core/files.cpp",
            "src/content/grid.cpp"]
    newest = max(os.path.getmtime(ROOT / s) for s in srcs + ["src/sim/talents_table.inc", "src/sim/skills.h",
                                                             "src/sim/items.h"])
    if not exe.exists() or os.path.getmtime(exe) < max(newest, os.path.getmtime(SYSB / "dump.cpp")):
        subprocess.run(["nice", "c++", "-std=c++20", "-O1", "-Isrc", "-Ithird_party", "-o", str(exe),
                        str(SYSB / "dump.cpp")] + srcs, check=True, cwd=ROOT)
    out = subprocess.run([str(exe), str(TABLES)], check=True, capture_output=True, text=True, cwd=ROOT)
    return json.loads(out.stdout)


def enum_names(header, enum):
    text = (ROOT / header).read_text()
    body = re.search(r"enum class " + enum + r"\b[^{]*\{(.*?)\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [n.strip().split("=")[0].strip() for n in body.split(",") if n.strip()]


def fig(v):
    s = f"{v:.1f}"
    if s.endswith(".0"):
        s = s[:-2]
    return "0" if s == "-0" else s


def secs(ticks):
    return round(ticks * TICK, 2)


def spoken(seconds):
    whole = int(-(-seconds // 1))
    return f"{whole // 60}:{whole % 60:02d}" if whole >= 60 else f"{whole} s"


def wait(seconds):
    return spoken(seconds) if seconds >= 60 else f"{seconds:.1f} s"


# ------------------------------------------------------------------------------------- skills

def teaching_items(d):
    cooked = {i["key"]: i for i in d["items"]}
    out = {}
    for f in sorted(glob.glob(str(ROOT / "content/items/*/*.json"))):
        if f.endswith(".rig.json"):
            continue
        doc = json.load(open(f))
        s = doc.get("skill")
        if not (isinstance(s, dict) and "number" in s):
            continue
        st = doc.get("stats") or {}
        req = st.get("requires") or {}
        c = cooked.get(doc["item"])
        strength = c["needs"][1] if c else (
            20 + req.get("strength", 0) * st.get("drop_level", 0) * 3 // 100 if req.get("strength") else 0)
        entry = {"item": doc["item"], "name": doc.get("mu_name"), "level": s.get("level_requirement"),
                 "energy": s.get("energy_requirement") or 0, "strength": strength,
                 "second_class": (st.get("class_level") or 0) >= 2}
        if c and c.get("by_plus"):
            for p in c["by_plus"]:
                out.setdefault(p["teaches"], []).append(dict(entry, plus=p["plus"], name=p["label"],
                                                             level=p["level"], energy=p["energy"]))
        else:
            out.setdefault(s["number"], []).append(entry)
    return out


GRANTS = {}


def weapon_grants():
    for f in sorted(glob.glob(str(ROOT / "content/items/weapons/*.json"))):
        if f.endswith(".rig.json"):
            continue
        doc = json.load(open(f))
        sk = (doc.get("stats") or {}).get("skill")
        if isinstance(sk, int) and sk:
            GRANTS.setdefault(sk, []).append(doc["item"])


def area_of(r, k):
    reach = int(r["reach"])
    if r["blinks"]:
        return f"a blink to the ground under the pointer, up to {reach} tiles"
    if r["channelled"]:
        return f"a chain leaping body to body, {reach} tiles a leap, up to {k['kLightningBodies']} bodies"
    if r["spread"] == "line":
        return f"a line of {int(k['kLineTiles'])} tiles; strikes everything it passes through"
    if r["spread"] == "beam":
        return f"a beam {k['kBeamTiles']:g} tiles long ahead of him"
    if r["burns"]:
        return f"fire on the ground, {r['burnTiles']:g} tiles round its target"
    if r["splash"] and r["fallTicks"]:
        return f"a shower of {k['kShowerRocks']} rocks over {int(r['splash'])} tiles round the spot"
    if r["splash"]:
        return f"{int(r['splash'])} tiles round its target"
    if r["walks"]:
        return "a whirlwind walking out ahead of him, striking what it passes three times"
    if r["spread"] == "ring":
        return f"everything within {reach} tiles round him"
    if r["spread"] == "arc":
        return f"an arc of {round(2 * r['arcHalfDeg'])} degrees in front, {r['reach']:g} tiles deep"
    if r["spread"] == "fan":
        if r["number"] == 52:
            return f"one arrow flying on through everything in its line, {k['kPierceTiles']:g} tiles"
        return f"{r['arrows']} arrows in a fan, each flying on through what it strikes"
    if r["onSelf"]:
        return "himself" + (" or a party member he points at" if r["aids"] else "")
    return "one body"


def effect_of(r, k):
    e = []
    if r["pushes"]:
        e.append("pushes what it strikes a tile away")
    if r["chillTicks"]:
        e.append(f"chills: half walking speed and slower swings for {r['chillTicks'] // 20} s")
    if r["poisonTicks"]:
        e.append(f"poisons for {r['poisonTicks'] // 20} s, a quarter of the blow every {k['kPoisonEvery'] // 20} s")
    if r["burns"]:
        e.append(f"burns {r['burns']} times, {k['kBurnEvery'] * TICK:.1f} s apart")
    if r["cutTicks"]:
        e.append(f"{round(r['cutChance'] * 100)}% chance to cut its defence by {r['cutShare']}% "
                 f"for {r['cutTicks'] // 20} s")
    if r["knock"]:
        e.append("knocks back a tile")
    if r["boonTicks"]:
        e.append(f"absorbs a share of every blow for {spoken(r['boonTicks'] * TICK)}")
    if r["mightTicks"]:
        e.append(f"adds damage to every blow for {spoken(r['mightTicks'] * TICK)}")
    if r["mends"]:
        e.append("heals at once")
    if r["summons"]:
        e.append("raises a summon that fights beside her")
    if r["blinks"]:
        e.append("teleports")
    return e or None


def damage_of(r, k, d):
    n = r["number"]
    if r["blinks"]:
        return None
    if r["boonTicks"]:
        cap = round(k["kGuardCap"] * 100)
        if n == 27:
            pts = "15 + 1.1 x Agility + 0.5 x Energy (no shield)"
        elif n == 16:
            pts = "5 x shield defence + 1.1 x Energy + 0.5 x Agility"
        else:
            pts = "5 x shield defence + 1.1 x Strength + 0.5 x Agility"
        return {"formula": f"absorbs {cap}% x P / (P + 150) of every blow, P = {pts}",
                "max_percent": cap, "lasts_s": secs(r["boonTicks"])}
    if r["mightTicks"]:
        return {"formula": "+(3 + Energy / 7) damage on every blow, arrows and spells too",
                "lasts_s": secs(r["mightTicks"])}
    if r["mends"]:
        return {"formula": "heals 5 + Energy / 5 at once"}
    if r["summons"]:
        return {"formula": "the summon stands at the breed's level plus a share of hers "
                           f"({10 * (1 + (n - 30 if n <= 35 else 5))}% of her level); its life x(1 + Energy/250 + "
                           "Vitality/500), its damage and attack rate x(1 + Energy/200 + Agility/800), "
                           "damage x0.6", "summons_monster": r["summons"],
                "fights_as": r["summonsAs"] or None}
    if n == 40:
        return {"formula": "his wizardry band plus the full charge (1320 at 1000 Energy, scaled by "
                           "Energy/1000) plus half his Strength, on every monster within 6 tiles",
                "stage_damage_at_1000_energy": 1320}
    if r["wizardry"]:
        D = r["damage"]
        return {"formula": f"(Energy/9 + {D}) to (Energy/4 + {D + D // 2}), x(1 + staff rise %) x {r['force']:g}",
                "spell_damage": D, "multiplier": r["force"], "stat": "Energy",
                "per": "each strike" if r["channelled"] or r["burns"] else
                       "each rock" if r["splash"] and r["fallTicks"] else
                       "each body" if r["splash"] else "the cast"}
    boost = 1.0
    if "DK" in r["classes"] and r["kin"] in ("DK", "MG"):
        boost = k["kKnightSkillBoost"]
    if r["kin"] == "Elf":
        return {"formula": f"x({r['force']:g} + Energy x 0.001) of a shot, on each body each arrow strikes",
                "multiplier": r["force"], "stat": "Agility (the shot itself)"}
    per = round(1 / r["forcePerStrength"]) if r["forcePerStrength"] else 0
    f = {"formula": f"x({r['force']:g} + Strength / {per} + Energy x 0.001) of a swing"
                    + (f", x{boost:g} for a Dark Knight or Magic Gladiator" if boost != 1 else "")
                    + (f", and x{k['kTwoHandBoost']:g} more for a Dark Knight with a two-handed weapon"
                       if r["kin"] == "DK" else ""),
         "multiplier": r["force"], "strength_per_point": r["forcePerStrength"], "stat": "Strength"}
    if r["mounted"]:
        f["formula"] += " (the dragon's breath; ridden only)"
    return f


def build_skills(d, k):
    teach = teaching_items(d)
    skills = []
    for r in d["skills"]:
        n = r["number"]
        base_cd = secs(r["coolTicks"])
        floor = r["floorTicks"]
        if r["coolTicks"] <= 0:
            cd = None
        elif r["summons"]:
            cd = secs(max(floor, r["coolTicks"]))
        else:
            cd = secs(max(floor, r["coolTicks"]))
        learned = teach.get(n)
        if n == 17:
            learned = [{"how": "known from creation (every Dark Wizard is born with it)"}]
        elif n == 49:
            learned = [{"item": "Helper04", "level": 160,
                        "how": "known while a Horn of Dinorant is worn; thrown only while riding it"}]
        elif n == 37:
            learned = [dict(l, how="the Orb of Summoning read at +6; given for the first Icarus clear")
                       for l in (learned or [])]
        elif not learned and GRANTS.get(n):
            learned = [{"item": key, "how": "known while this weapon is in his right hand"} for key in GRANTS[n]]
        classes = r["classes"]
        cls_words = [KIN_NAMES[c] for c in classes]
        if r["second"]:
            cls_words = [SECOND[r["kin"]] + " (second class)"]
        lvl = next((l.get("level") for l in (learned or []) if l.get("level")), r["needLevel"] or None)
        energy = next((l.get("energy") for l in (learned or []) if l.get("energy")), 0)
        strength = next((l.get("strength") for l in (learned or []) if l.get("strength")), 0)
        dmg = damage_of(r, k, d)
        weapon = None
        if not r["wizardry"] and not r["mounted"] and not (r["anyHand"] and r["onSelf"]) and \
                r["familiesListed"] and r["families"] & 0x7F != 0x7F:
            weapon = r["familiesListed"]
        mana = r["mana"] * (r["chargeStages"] or 1)
        # The card, as Desk::skillSheet lays it out.
        card = []
        if r["tells"]:
            card.append(r["tells"])
        if weapon:
            card.append(f"Weapon: {weapon}")
        if r["mounted"]:
            card.append("Mount: Horn of Dinorant")
        if r["wizardry"] and not r["blinks"]:
            lab = {"each strike": "Each strike", "each rock": "Each rock", "each body": "Each body"}.get(
                dmg.get("per"), "Damage")
            card.append(f"{lab}: (Energy/9 + {r['damage']}) - (Energy/4 + {r['damage'] + r['damage'] // 2})"
                        f" x staff x {r['force']:g}")
            card.append(f"Range: {int(r['reach'])} tiles")
            if r["channelled"]:
                card.append(f"Channel: {r['channelTicks'] * TICK:.1f} s")
            card.append("Area: " + area_of(r, k))
        elif r["blinks"]:
            card += [f"Range: {int(r['reach'])} tiles", "to the ground under the pointer"]
        elif r["onSelf"]:
            card.append(dmg["formula"])
        else:
            per = round(1 / r["forcePerStrength"]) if r["forcePerStrength"] else 0
            card.append(f"Damage: x{r['force']:.2f} + Strength / {per} of a swing" if per else
                        f"Damage: x{r['force']:.2f} of a shot")
        for e in effect_of(r, k) or []:
            if not r["onSelf"]:
                card.append(e[0].upper() + e[1:])
        if cd is not None:
            card.append(f"Cooldown: {wait(cd)}")
        card.append(f"Mana: {mana}")
        if lvl:
            card.append(f"Learned at: level {lvl}")
        skills.append({
            "id": n, "name": r["name"], "class": KIN_NAMES[r["kin"]], "classes": cls_words,
            "class_keys": classes, "second_class_only": r["second"],
            "learned_from": learned, "level": lvl, "energy": energy or None, "strength": strength or None,
            "mana": mana, "mana_note": (f"{r['mana']} a stage x {r['chargeStages']} stages, paid at once"
                                        if r["chargeStages"] else None),
            "cooldown_s": cd, "cooldown_base_s": base_cd if r["coolTicks"] > 0 else None,
            "cooldown_rule": (None if r["coolTicks"] <= 0 else
                              "fixed: one summon key cools a minute" if r["summons"] else
                              f"{base_cd:g} s / (1 + Agility / {k['kAgilityPerDoubling']:g}), never shorter than "
                              + ("the buff's own length + 2 s" if (r["boonTicks"] or r["mightTicks"])
                                 and r["number"] not in (16, 27, 28) else "its own animation")),
            "range_tiles": r["reach"] if r["reach"] else None, "area": area_of(r, k),
            "damage": dmg, "element": ELEMENTS[ELEMENT_OF.get(n, 0)],
            "effect": effect_of(r, k), "weapon": weapon,
            "slot": ("primary (right-click / auto-attack: no cooldown, cast over and over)" if r["primary"]
                     else "QWER (a key on the skill bar)"),
            "cast_with_no_target": r["castsBare"], "aims_at_pointer": r["aimsAtPointer"],
            "card": card,
        })
    order = {"DK": 0, "MG": 1, "DW": 2, "Elf": 3}
    skills.sort(key=lambda s: (order[s["class_keys"][0]] if s["class_keys"] else 9, s["level"] or 0, s["id"]))
    return skills


ELEMENT_OF = {}


def read_elements():
    text = (ROOT / "src/sim/realm_tuning.h").read_text()
    body = re.search(r"inline Element skillElement\(int32_t number\) \{(.*?)\n\}", text, re.S).group(1)
    names = {"kFireBall": 4, "kFlame": 4, "kHellfire": 4, "kInferno": 4, "kFireSlash": 4, "kMeteorite": 4}
    sk = (ROOT / "src/sim/skills.h").read_text()
    num = {m.group(1): int(m.group(2)) for m in re.finditer(r"constexpr int32_t (k\w+) = (\d+);", sk)}
    pending = []
    for line in body.splitlines():
        for m in re.finditer(r"case skill::(k\w+)", line):
            pending.append(m.group(1))
        r = re.search(r"return Element::(\w+)", line)
        if r:
            el = ["None", "Ice", "Poison", "Lightning", "Fire", "Wind"].index(r.group(1))
            for p in pending:
                ELEMENT_OF[num[p]] = el
            pending = []


# ------------------------------------------------------------------------------------- talents

def build_talents(d, k):
    stats = enum_names("src/sim/talents.h", "TalentStat")
    specials = enum_names("src/sim/talents.h", "Special")
    holds = enum_names("src/sim/talents.h", "Hold")
    counted = {"AttackSpeed", "CastSpeed", "IcePoisonResistance", "AidReach"}
    skill_names = {s["number"]: s["name"] for s in d["skills"]}
    rules = {
        "points": "one talent point for every 25 stat points the levels pay: (level - 1) x points per level, "
                  "plus 400 levels' worth for each rebirth, divided by 25",
        "points_per_level": k["pointsPerLevel"], "stat_points_per_talent": k["kStatPointsPerTalent"],
        "in_words": {"DK": "1 every 5 levels, 79 at level 400, +80 per rebirth",
                     "DW": "1 every 5 levels, 79 at level 400, +80 per rebirth",
                     "Elf": "1 every 5 levels, 79 at level 400, +80 per rebirth",
                     "MG": "7 every 25 levels (he gets 7 stat points a level), 111 at level 400, +112 per rebirth"},
        "points_at": d["talentPoints"],
        "points_at_note": "level -> [no rebirth, 1 rebirth, 2 rebirths]",
        "tiers": 3, "nodes_per_column": 9, "layout": "each tier is small, small, major",
        "small_max": k["kTalentSmallMost"], "major_max": 1,
        "unlock": [
            "the first tier's two small nodes are open from the start",
            f"a major opens when either small node of its tier has {k['kTalentMajorOpensAt']} points",
            "the next tier's small nodes open once the tier above has its major",
            "a point can be taken back only if everything above it stays open",
        ],
        "reset": "the talents window's Reset gives every point back",
    }
    classes = {}
    for kin, cols in d["talents"].items():
        out = []
        for ci, c in enumerate(cols):
            nodes = []
            for ni, n in enumerate(c["nodes"]):
                major = ni % 3 == 2
                stat = stats[n["stat"]]
                unit = "" if stat in counted else "%"
                if major:
                    per, at_max, needs = None, n["text"], (
                        f"{k['kTalentMajorOpensAt']} points in {c['nodes'][ni - 2]['name']} or {c['nodes'][ni - 1]['name']}")
                else:
                    per = ("+" if n["amount"] > 0 else "") + fig(n["amount"]) + unit + " " + n["text"]
                    v = n["amount"] * k["kTalentSmallMost"]
                    at_max = ("+" if v > 0 else "") + fig(v) + unit + " " + n["text"]
                    needs = "open from the start" if ni < 3 else f"{c['nodes'][(ni // 3) * 3 - 1]['name']} taken"
                nodes.append({
                    "id": f"{kin}_{ci}_{ni}", "name": n["name"], "kind": "major" if major else "small",
                    "tier": ni // 3 + 1, "max": 1 if major else k["kTalentSmallMost"],
                    "per_point": per, "at_max": at_max, "amount": n["amount"], "unit": unit or "count",
                    "stat": stat, "special": specials[n["special"]] if n["special"] else None,
                    "hold": holds[n["hold"]] if n["hold"] else None,
                    "skills": [skill_names.get(s, s) for s in n["skills"] if s] or None,
                    "chance_percent": n["chance"] or None, "seconds": n["seconds"] or None,
                    "needs": needs, "icon": n["icon"], "icon_png": f"icons/talent_{kin}_{ci}_{ni}.png",
                })
            out.append({"name": c["name"], "sub": c["sub"],
                        "feeds_skills": [skill_names.get(s, s) for s in c["feeds"] if s],
                        "nodes": nodes})
        classes[kin] = {"class": KIN_NAMES[kin], "columns": out}
    return {"rules": rules, "classes": classes}


# ------------------------------------------------------------------------------------- icons

def build_icons(skills, talents, out):
    icons = out / "icons"
    icons.mkdir(parents=True, exist_ok=True)
    index = {"skills": {}, "talents": {}, "notes": []}
    for s in skills:
        src = ROOT / f"content/interface/skills/skill_{s['id']}.png"
        if not src.exists():
            index["skills"][str(s["id"])] = None
            index["notes"].append(f"skill {s['id']} {s['name']}: no icon in the game (content/interface/skills "
                                  f"has no skill_{s['id']}.png; the bar draws none)")
            s["icon"] = None
            continue
        im = Image.open(src).convert("RGB").resize((46, 64), Image.LANCZOS)
        p = icons / f"skill_{s['id']}.png"
        im.save(p, optimize=True)
        index["skills"][str(s["id"])] = f"icons/skill_{s['id']}.png"
        s["icon"] = f"icons/skill_{s['id']}.png"
    sheet = Image.open(ROOT / "content/interface/talents/talent_icons.png").convert("RGB")
    for kin, c in talents["classes"].items():
        for col in c["columns"]:
            for n in col["nodes"]:
                cell = n["icon"] - 300
                x, y = (cell % 25) * 20, (cell // 25) * 28
                im = sheet.crop((x, y, x + 20, y + 28)).resize((46, 64), Image.LANCZOS)
                p = icons / f"talent_{n['id']}.png"
                im.save(p, optimize=True)
                index["talents"][n["id"]] = f"icons/talent_{n['id']}.png"
    index["format"] = ("46x64 PNG (MU's 20x28 portrait cells, 64 px tall). Skills: the game's own cut of "
                       "MuDream's newui_skill sheet (80x112), downscaled. Talents: MuMain's new_Master_Icon.OZJ "
                       "(decoded by pipeline/talent_icons.py), cell = icon - 300, 25 a row, enlarged 2.3x.")
    (icons / "index.json").write_text(json.dumps(index, indent=1))
    return index


# ------------------------------------------------------------------------------------- gear powers

def build_gear(d, k):
    items = {i["key"]: i for i in d["items"]}
    gear = {}
    hours = lambda s: s / 3600
    # pets
    words = {
        "Helper01": ("Guardian Angel", "A guardian angel flies at your side: every blow that reaches you is cut "
                     "by 30% and your max life is +50, but your own blows and spells hit 20% softer."),
        "Helper02": ("Imp", "An imp rides your shoulder: every blow and spell you land hits 30% harder, "
                     "and costs you 3 life each time it lands (not below 3 life)."),
        "Helper03": ("Horn of Uniria", "A mount: you ride Uniria off safe ground, faster than you run. "
                     "No combat bonus."),
        "Helper04": ("Horn of Dinorant", "A flying mount: your blows hit 15% harder (1 life each blow that lands), "
                     "what reaches you is cut by 10%, and you learn Fire Breath while you ride it."),
    }
    for key, (name, line) in words.items():
        i = items[key]
        p = i["pet"]
        e = {"name": name, "kind": "mount" if p["mount"] else "pet", "slot": "pet" if not p["mount"] else "mount",
             "summary": line, "drop_level": i["dropLevel"], "level_needed": i["needs"][0],
             "damage_dealt_x": p["dealt"], "damage_taken_x": p["taken"], "max_life": p["health"] or None,
             "life_per_blow": p["lifeCost"] or None}
        if p["timed"]:
            e["lasts"] = (f"{hours(p['seconds']):g} hours from the moment it is first worn, worn or not, "
                          "online or not; then it is gone. Not worn down by blows and never repaired. "
                          "Unworn ones stack 20 to a cell.")
            e["lasts_hours"] = hours(p["seconds"])
        else:
            e["lasts"] = (f"{i['durability']} life, worn down by the damage you take (1 point every "
                          f"{round(1 / p['wear'])} damage); repairable.")
            e["life"] = i["durability"]
        gear[key] = e
    # wings
    for key, i in items.items():
        w = i.get("wing")
        if not w:
            continue
        lvl2 = w["level"] == 2
        by = [{"plus": b["plus"], "damage_increase_percent": round((b["dealt"] - 1) * 100),
               "absorb_percent": round((1 - b["taken"]) * 100), "defense": b["defense"],
               "level_needed": b["needLevel"]} for b in w["by_plus"] if b["plus"] <= 15]
        life_cost = w["by_plus"][0]["lifeCost"]
        opts = sorted(set(w["options"]))
        optwords = {"regeneration": "life regeneration +1% a level", "damage": "damage +4 a level",
                    "wizardry": "wizardry +4 a level"}
        e = {"name": i["label"], "kind": f"wings, {'2nd' if lvl2 else '1st'} level",
             "summary": (f"Your blows hit {by[0]['damage_increase_percent']}% harder "
                         f"(+{1 if lvl2 else 2}% a plus) and what reaches you is cut by "
                         f"{by[0]['absorb_percent']}% (+2% a plus); each blow that lands costs "
                         f"{life_cost} life."),
             "formula": {"damage": "x(132 + plus)%" if lvl2 else "x(112 + 2 x plus)%",
                         "absorb": "x(75 - 2 x plus)% taken" if lvl2 else "x(88 - 2 x plus)% taken",
                         "defense": f"{i['defense']} + {2 if lvl2 else 3} a plus, and from +10 a triangle on top",
                         "level": f"{i['needs'][0]} + {5 if lvl2 else 4} a plus"},
             "by_plus": by, "life_per_blow": life_cost,
             "additional_option": [optwords[o] for o in opts],
             "additional_option_note": ("the option's kind is rolled when the wing is made: "
                                        + " or ".join(optwords[o] for o in w["options"]) if lvl2 and
                                        len(set(w["options"])) > 1 else "its one kind")
                                       + "; up to +4 levels (16), raised with a Jewel of Life",
             "durability": i["durability"],
             "wear": f"loses 1 durability for about every {round(k['kWingWearTicks'] * TICK * k['kWingWearSteps'] / 60)} "
                     "minutes worn",
             "level_needed": i["needs"][0], "drop_level": i["dropLevel"]}
        if lvl2:
            e["extras"] = {"note": "a 2nd wing may carry some of these, rolled when it is made",
                           "max_life": f"+{k['kWingExtraBase']} and +{k['kWingExtraPerPlus']} a plus",
                           "max_mana": f"+{k['kWingExtraBase']} and +{k['kWingExtraPerPlus']} a plus",
                           "ignore_defense": f"{round(k['kWingIgnoreChance'] * 100)}% chance a blow ignores the defence it meets"}
            e["class"] = {"Wing04": "Muse Elf", "Wing05": "Soul Master", "Wing06": "Blade Knight",
                          "Wing07": "Magic Gladiator"}.get(key)
            e["made"] = "in the Chaos Machine from a 1st wing, Loch's Feather and an excellent item (docs: wings)"
        else:
            e["class"] = {"Wing01": "Fairy Elf", "Wing02": "Dark Wizard", "Wing03": "Dark Knight"}.get(key)
            e["made"] = (f"in the Chaos Machine; and one in {k['kIcarusWingOdds']} kills in Icarus leaves your "
                         "class's wing")
        gear[key] = e
    # jewellery
    affixes = {a["id"]: a for a in d["affixes"]}
    plus_range = list(range(0, k["kRefineCap"] + 1))
    colours = ["green (1 power)", "blue (2 powers)", "purple (3 powers)", "legendary orange (4 powers)"]
    count_rule = [{"powers": n + 1, "colour": colours[n], "share_percent": round(k["kAffixCountShare"][n] * 100),
                   "from_monster_level": k["kAffixCountLevel"][n]} for n in range(4)]
    resist = [affixes[i] for i in (6, 7, 8, 9)]
    sig_words = {
        1: "more experience from every kill", 2: "more Zen from every kill",
        3: "better odds of item drops (both the excellent and the plain roll)",
        4: "life back on every blow that lands, per body", 5: "critical blows hit harder, and a critical chance",
        10: "attack and casting speed",
    }
    for key, i in items.items():
        j = i.get("jewellery")
        if not j:
            continue
        kind = "ring" if j["ring"] else "pendant"
        e = {"name": i["label"], "kind": kind, "drop_level": i["dropLevel"], "level_needed": i["needs"][0]}
        if j["signature"]:
            a = affixes[j["signature"]]
            unit = "" if j["signature"] in (4, 10) else "%"
            e["power"] = {"name": a["name"], "what": sig_words[j["signature"]],
                          "by_plus": {f"+{p}": a["by_plus"][p] for p in plus_range}, "unit": unit or
                          ("life a blow" if j["signature"] == 4 else "speed")}
            if j["signature"] == 5:
                e["power"]["critical_chance_by_plus"] = {f"+{p}": d["furyChance"][p] for p in plus_range}
            e["summary"] = (f"{a['name']}: {a['by_plus'][0]}{unit} at +0 to {a['by_plus'][9]}{unit} at +9"
                            + (f", and {d['furyChance'][0]}% to {d['furyChance'][9]}% critical chance"
                               if j["signature"] == 5 else "") + ". Every worn piece adds its own.")
            e["value_formula"] = ("lo + (hi - lo) x 0.5 x (plus / 9)^2, rounded: weak at +0, most of it in the last "
                                  "pluses")
            e["further_powers"] = {"how_many": count_rule,
                                   "drawn_from": [affixes[x]["name"] for x in (1, 2, 3, 4, 5) if x != j["signature"]]
                                                 + [r["name"] for r in resist],
                                   "note": "the extra powers are drawn from the other four signature powers and the "
                                           "four resistances, none twice; Attack and casting speed is never an extra"}
            e["drops"] = (f"as any item in its 15-level band; deeper kills still draw powered pieces on "
                          f"{round(k['kDeepJewellery'] * 100)}% of their item drops. Colour = power count.")
        else:
            el = ELEMENTS[j["element"]]
            e["resistance"] = {"element": el, "by_plus": {f"+{p}": p for p in range(0, k["kJewelleryMostPlus"] + 1)},
                               "note": "1 a plus; the largest worn piece of each element counts"}
            e["resistance_does"] = {"ice": "turns the chill aside r times in r + 1",
                                    "poison": "turns the poison aside r times in r + 1",
                                    "lightning": "turns the push aside r times in r + 1",
                                    "fire": f"{round(k['kResistanceCut'] * 100)}% less fire damage a point, to "
                                            f"{round(k['kResistanceCutMost'] * 100)}%",
                                    "wind": f"{round(k['kResistanceCut'] * 100)}% less wind damage a point, to "
                                            f"{round(k['kResistanceCutMost'] * 100)}%"}[el]
            e["option"] = "life regeneration +1% to +3% of max life every second"
            e["dropped_plus"] = f"+0 to +{k['kJewelleryMostPlus']}"
            if j["ring"]:
                e["sting"] = {"chance_by_plus": {f"+{p}": round(d["ringSting"][p] * 100, 2) for p in plus_range},
                              "what": {"ice": "chills it as Ice does and bites for half a rune blow",
                                       "poison": f"poisons it; each pulse {round(k['kRingPulseShare'] * 100)}% of the blow",
                                       "lightning": "a bolt at half a rune blow that pushes it a tile",
                                       "fire": f"sets it burning; each pulse {round(k['kRingPulseShare'] * 100)}% of the blow",
                                       "wind": "a gust at half a rune blow that knocks it back a tile"}[el],
                              "formula": "5% + 1.25% a plus, to 15%, on each blow you land on a monster"}
            e["summary"] = (f"{el.capitalize()} resistance 1 a plus"
                            + (f"; {round(d['ringSting'][0] * 100)}% chance a blow you land also strikes with "
                               f"{el} (up to 15%)" if j["ring"] else "") + ".")
        e["refining"] = ("Jewels of Bless (to +6, never fails) and Soul (to +9, 50%, 75% when lucky; a failure "
                         "drops a plus, from +7 back to +0) take every ring and pendant, at armour's odds")
        e["luck"] = "a ring or pendant drop is lucky 4 times in 100; luck adds critical chance and +25% to Soul's odds"
        gear[key] = e
    gear["_powers"] = {
        "affixes": [{"id": a["id"], "name": a["name"], "by_plus": {f"+{p}": a["by_plus"][p] for p in plus_range}}
                    for a in d["affixes"]],
        "power_count": count_rule,
        "resistances": "Ice, Poison, Lightning and Fire resistance are extra powers only: "
                       + ", ".join(f"{r['by_plus'][0]} at +0, {r['by_plus'][9]} at +9" for r in resist[:1])
                       + "; added on top of the largest of MU's own elemental pieces worn",
    }
    return gear


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    try:
        os.nice(10)
    except OSError:
        pass
    d = dump()
    k = d["constants"]
    read_elements()
    weapon_grants()
    skills = build_skills(d, k)
    talents = build_talents(d, k)
    icons = build_icons(skills, talents, out)
    gear = build_gear(d, k)
    meta = {"source": "the game's own tables (src/sim, compiled) and cards (game/ui); numbers at 0 extra stats",
            "ticks": "20 a second"}
    (out / "skills.json").write_text(json.dumps({"meta": meta, "skills": skills}, indent=1, ensure_ascii=False))
    (out / "talents.json").write_text(json.dumps(talents, indent=1, ensure_ascii=False))
    (out / "gear_powers.json").write_text(json.dumps(gear, indent=1, ensure_ascii=False))
    print(len(skills), "skills;", sum(len(c["columns"]) for c in talents["classes"].values()), "talent columns;",
          len(icons["skills"]), "skill icons,", len(icons["talents"]), "talent icons;", len(gear) - 1, "gear entries")


if __name__ == "__main__":
    main()
