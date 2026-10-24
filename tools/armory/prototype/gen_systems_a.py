#!/usr/bin/env python3
"""runes.json, consumables.json, mixes.json and elites.json for the website, beside this script.

Run from the MU2_BGFX directory. Reuses gen_npcs.py (and through it gen_sources.py) for the item
rows, prices, worlds and quests; those write only to /dev/null and their own scratchpad files.
Read-only on the repo.
"""
import json
import os
import re
import runpy
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_argv = sys.argv
sys.argv = [os.path.join(HERE, "gen_npcs.py")]
N = runpy.run_path(os.path.join(HERE, "gen_npcs.py"), run_name="gen_npcs")
sys.argv = _argv
G = N["G"]
src, strip_comments, split_args = G["src"], G["strip_comments"], G["split_args"]
ITEMS, key_of, buying_price = G["ITEMS"], G["key_of"], G["buying_price"]
WORLDS, LIVE, MAPNAME, BREEDS = G["WORLDS"], G["LIVE"], G["MAPNAME"], G["BREEDS"]
QUESTS_RAW, QUEST_ID, NPC_BY_NUMBER = N["QUESTS"], N["QUEST_ID"], N["NPC_BY_NUMBER"]
STR = r'"(?:[^"\\]|\\.)*"'
strings = N["strings"]


def sig(x):
    return float("%.4g" % x)


def consts(text):
    """Every `constexpr <int/double/float/int64_t> kName = value;` of a header, evaluated."""
    out = {}
    for name, val in re.findall(r"constexpr (?:int|int32_t|int64_t|double|float|uint8_t) (k\w+) = ([^;{]+);", text):
        try:
            out[name] = eval(val.replace("f", "") if re.fullmatch(r"[\d.]+f", val.strip()) else val, {}, dict(out))
        except Exception:
            pass
    for line in re.findall(r"constexpr (?:int|int64_t) (k\w+ = [^;{]+(?:, k\w+ = [^;{]+)+);", text):
        for name, val in re.findall(r"(k\w+) = ([^,]+)", line):
            try:
                out[name] = eval(val, {}, dict(out))
            except Exception:
                pass
    return out


def array(text, name):
    m = re.search(r"constexpr [\w:]+ %s\[[^\]]*\](?:\[[^\]]*\])* = \{(.*?)\};" % name, text, re.S)
    return [float(x) if "." in x else int(x) for x in re.findall(r"-?\d+(?:\.\d+)?", m.group(1))]


items_h = strip_comments(src("src/sim/items.h"))
IC = consts(items_h)
machine_h = strip_comments(src("src/sim/machine.h"))
MC = consts(machine_h)
elite_h = strip_comments(src("src/sim/elite.h"))
EC = consts(elite_h)
event_h = strip_comments(src("src/sim/event.h"))
square_h = strip_comments(src("src/sim/square.h"))

# ---- 1) runes -----------------------------------------------------------------------------------------
RARITY = ["Rare", "Epic", "Legendary", "Common", "Ancient"]
RARITY_SHARE = array(items_h, "kRuneRarityShare")
RARITY_FROM = [IC["kCreationLevel"], 40, 60, 1, IC["kAncientLevel"]]
RARITY_FROM = array(items_h.replace("kCreationLevel", str(IC["kCreationLevel"])).replace("kAncientLevel", str(IC["kAncientLevel"])),
                    "kRuneRarityLevel")
HERO_LEVEL = array(items_h, "kRuneHeroLevel")
SALE = array(items_h, "kRuneSale")
POWER_NUM = {n: int(v) for n, v in re.findall(r"^\s*(\w+) = (\d+),?$", re.search(
    r"enum class Power : uint8_t \{(.*?)\};", items_h, re.S).group(1), re.M)}
KIN_BIT = {"kWizardOnly": 1 << 1, "kElfOnly": 1 << 2, "kKnightOnly": 1 << 0, "kGladiatorOnly": 1 << 3}
# sim::Kin: DarkKnight 0, DarkWizard 1, FairyElf 2, MagicGladiator 3 (classBit = 1 << kin)
kin_h = strip_comments(src("src/sim/rules.h"))
m = re.search(r"enum class Kin : \w+ \{(.*?)\};", kin_h, re.S)
KIN_ORDER = [x.split("=")[0].strip() for x in m.group(1).split(",") if x.strip()]
KIN_NUM = {}
n = 0
for x in m.group(1).split(","):
    x = x.strip()
    if not x:
        continue
    if "=" in x:
        n = int(x.split("=")[1])
    KIN_NUM[x.split("=")[0].strip()] = n
    n += 1
KIN_BIT = {"kWizardOnly": 1 << KIN_NUM["DarkWizard"], "kElfOnly": 1 << KIN_NUM["FairyElf"],
           "kKnightOnly": 1 << KIN_NUM["DarkKnight"], "kGladiatorOnly": 1 << KIN_NUM["MagicGladiator"]}
KIN_BIT["kEveryClass"] = sum(KIN_BIT.values())
SLOT_BIT = {"kInWeapon": 1, "kInShield": 2, "kInArmour": 4, "kInJewellery": 8}
SLOT_BIT["kWorn"] = 2 | 4 | 8
SLOT_BIT["kHeld"] = 1 | 8
SLOT_BIT["kAny"] = 1 | 2 | 4 | 8
CLASS_SHORT = [("DarkKnight", "DK"), ("DarkWizard", "DW"), ("FairyElf", "FE"), ("MagicGladiator", "MG")]
SLOT_NAMES = [(1, "weapon"), (2, "shield"), (4, "armour"), (8, "jewellery")]
ELEMENT = {"Inferno": "fire", "Glacier": "ice", "Venom": "poison", "Thunder": "lightning", "Tempest": "wind"}
LABEL_EXC = {"FireRing": "Rune of the Fire Ring", "Undying": "Rune of the Undying", "Frost": "Rune of Frost Arrows",
             "Pyroblast": "Rune of Pyroblast", "Spirits": "Rune of Evil Spirits"}

ic = strip_comments(src("src/sim/items.cpp"))
body = re.search(r"static const PowerRow kPowers\[\] = \{(.*?)\n    \};", ic, re.S).group(1)
POWERS = []
depth, cur = 0, ""
for ch in body:
    if ch == "{":
        depth += 1
        if depth == 1:
            cur = ""
            continue
    if ch == "}":
        depth -= 1
        if depth == 0:
            POWERS.append(cur)
            continue
    if depth >= 1:
        cur += ch


def bits(expr, table):
    return sum(table[t] for t in re.findall(r"k\w+", expr))


ROWS = []
for entry in POWERS:
    parts = split_args(entry)
    p = parts[0].split("::")[1]
    tells = strings(parts[2]) if '"' in parts[2] else ""
    ROWS.append({"power": p, "number": POWER_NUM[p], "name": strings(parts[1]), "tells": tells,
                 "classes": bits(parts[3], KIN_BIT), "slots": bits(parts[4], SLOT_BIT),
                 "rarity": RARITY.index(parts[5].split("::")[1]),
                 "second": len(parts) > 6 and parts[6].strip() == "true",
                 "once": len(parts) > 7 and parts[7].strip() == "true"})
ROWS.sort(key=lambda r: r["number"])


def element_serves(element, kin):
    return element is not None and (kin != "FairyElf" or element in ("ice", "wind"))


def drawable(row, kin, second, commons=True):
    if not commons and row["rarity"] == 3:
        return False
    el = ELEMENT.get(row["power"])
    if el:
        return element_serves(el, kin)
    return (row["classes"] >> KIN_NUM[kin]) & 1 and (not row["second"] or second)


def rune_odds(kin, second, level, commons=True):
    """drawRunePower: P(power | a rune drops) for a killer of kin at a monster of level."""
    count = [0] * 5
    for r in ROWS:
        if drawable(r, kin, second, commons) and level >= RARITY_FROM[r["rarity"]]:
            count[r["rarity"]] += 1
    held = sum(RARITY_SHARE[i] for i in range(5) if count[i] > 0)
    out = {}
    for r in ROWS:
        if drawable(r, kin, second, commons) and level >= RARITY_FROM[r["rarity"]]:
            out[r["number"]] = RARITY_SHARE[r["rarity"]] / held / count[r["rarity"]]
    return out


BANDS = [(1, 14), (15, 39), (40, 59), (60, 140)]  # live monsters stop at 140; Ancient (141+) is Kanturu's
HEROES = [("DarkKnight", False, "DK"), ("DarkKnight", True, "BK"), ("DarkWizard", False, "DW"),
          ("DarkWizard", True, "SM"), ("FairyElf", False, "FE"), ("FairyElf", True, "ME"),
          ("MagicGladiator", False, "MG")]
C_CREATION = IC["kCreationChance"]

# Quests that pay each power, from quests.cpp (via gen_npcs' parse).
Q_RUNES = {}
for q in QUESTS_RAW:
    if q["giver"] == -1:
        continue
    for p in q["paid"]:
        if p["item"] == "Jewel22" and p["power"]:
            e = {"quest": QUEST_ID[q["key"]], "when": "first clear" if p["first"] else ("every clear" if q["repeat"] else "once")}
            if p["kin"]:
                e["class"] = p["kin"]
            Q_RUNES.setdefault(p["power"], [])
            if e not in Q_RUNES[p["power"]]:
                Q_RUNES[p["power"]].append(e)

raid_h = strip_comments(src("src/sim/raid.h"))
RC = consts(raid_h)
runes = []
for r in ROWS:
    rarity = RARITY[r["rarity"]]
    classes = [s for k, s in CLASS_SHORT if (r["classes"] >> KIN_NUM[k]) & 1]
    o = {"id": "rune-" + re.sub(r"[^a-z0-9]+", "-", r["name"].lower()).strip("-"), "power": r["number"],
         "name": LABEL_EXC.get(r["power"], "Rune of " + r["name"]), "power_name": r["name"], "rarity": rarity,
         "level": HERO_LEVEL[r["rarity"]],
         "classes": "any" if r["classes"] == KIN_BIT["kEveryClass"] else classes}
    if r["second"]:
        o["second_class_only"] = True
    if r["once"]:
        o["once_per_item"] = True
    o["slots"] = [name for bit, name in SLOT_NAMES if r["slots"] & bit]
    o["effect"] = r["tells"]
    ch = re.match(r"(\d+)% (?:on hit|chance)", r["tells"]) or re.search(r"(\d+)% to ", r["tells"])
    if ch:
        o["chance"] = int(ch.group(1))
    nums = re.findall(r"[+-]?\d+(?:\.\d+)?(?:%| s\b)?", r["tells"])
    if nums:
        o["numbers"] = nums
    if r["power"] in ELEMENT:
        o["element"] = ELEMENT[r["power"]]
    nxt = {3: "Rare", 0: "Epic", 1: "Legendary"}.get(r["rarity"])
    if nxt:
        o["fuses_into"] = nxt
    if r["rarity"] == 4:
        o["dormant"] = True
    o["sells_for"] = int(round(SALE[r["rarity"]] * 12000000))
    sources = []
    if r["rarity"] != 4:
        odds = []
        for lo, hi in BANDS:
            if lo < RARITY_FROM[r["rarity"]] and hi < RARITY_FROM[r["rarity"]]:
                continue
            row = {"monster_level": "%d-%d" % (max(lo, RARITY_FROM[r["rarity"]]), hi)}
            for kin, second, short in HEROES:
                p = rune_odds(kin, second, hi).get(r["number"], 0.0)
                if p > 0:
                    row[short] = sig(p * C_CREATION)
            if len(row) > 1:
                odds.append(row)
        if odds:
            sources.append({"type": "drop", "note": "Rune of Creation drop: 0.05% of ordinary kills, its power drawn for the killer's class; per-kill odds by killer (BK/SM/ME are the second classes)",
                            "odds": odds})
    else:
        sources.append({"type": "drop", "note": "Ancient: only from monsters of level %d and over (Kanturu, not yet open)" % RARITY_FROM[4]})
    if r["rarity"] != 3 and r["rarity"] != 4:
        raid = {}
        for kin, second, short in HEROES:
            p = rune_odds(kin, second, BREEDS[79]["level"], commons=False).get(r["number"], 0.0)
            if p > 0:
                raid[short] = sig(p)
        if raid:
            sources.append({"type": "raid", "note": "Golden Dragon raid: each minion 1 in %d leaves a rune, the dragon's hoard %d; this rune's share of them by killer" % (RC["kMinionRuneOdds"], RC["kHoardRunes"]),
                            "share": raid})
    for e in Q_RUNES.get(r["power"], []):
        sources.append({"type": "quest", **e})
    for k, short in (("Fuse Runes", "fuse"),):
        if r["rarity"] in (0, 1, 2):
            sources.append({"type": "fuse", "note": "Fuse Runes: three %s runes make a random %s one" % (
                {0: "Common", 1: "Rare", 2: "Epic"}[r["rarity"]], rarity)})
    o["sources"] = sources
    runes.append(o)

RZ = array(machine_h, "kRemoveRuneZen")
rules = {
    "sockets": "Weapons, armour, shields, rings and pendants can drop with sockets: %d%% of plain item drops get one, and each further one %d%%, up to %d (a ring holds 1). Wings take sockets only at the Chaos Machine's Add Socket." % (
        round(IC["kSocketChance"] * 100), round(IC["kMoreSocketChance"] * 100), IC["kMostSockets"]),
    "setting": "Drop a Rune of Creation on an item with a free socket, as you would a Jewel of Bless. Its class and slot must fit, and a rune marked once per item cannot be set twice in one item.",
    "level": "Setting a rune needs the hero level of its rarity: Common %d, Rare %d, Epic %d, Legendary %d, Ancient %d. An item needs the highest level of the runes set in it to be worn. A reborn hero meets every rune level." % (
        HERO_LEVEL[3], HERO_LEVEL[0], HERO_LEVEL[1], HERO_LEVEL[2], HERO_LEVEL[4]),
    "removing": "Remove Rune (Chaos Machine): one item with a rune and a Jewel of Chaos; the rune comes back as a Rune of Creation, 100%%. Zen by rarity: Common %s, Rare %s, Epic %s, Legendary %s." % tuple(
        "{:,}".format(z) for z in [RZ[3], RZ[0], RZ[1], RZ[2]]),
    "fusing": "Fuse Runes (Chaos Machine): three runes of one rarity, a Jewel of Chaos and a Jewel of Life make one random rune of the next rarity your class can set, 100%%, %s Zen. Legendary and Ancient do not fuse." % "{:,}".format(MC["kFuseZen"]),
    "drop": "A Rune of Creation drops from 0.05%% of kills. Its rarity is drawn among those the monster's level reaches: Common (any level) %d%%, Rare (level %d+) %d%%, Epic (%d+) %d%%, Legendary (%d+) %d%%, Ancient (%d+) %d%%, each share divided by the total of the reachable ones; then one rune of that rarity your class can set, evenly. Element runes go to any class that throws that element." % (
        round(RARITY_SHARE[3] * 100), RARITY_FROM[0], round(RARITY_SHARE[0] * 100), RARITY_FROM[1], round(RARITY_SHARE[1] * 100),
        RARITY_FROM[2], round(RARITY_SHARE[2] * 100), RARITY_FROM[4], round(RARITY_SHARE[4] * 100)),
    "magic_gladiator": "A Magic Gladiator paid a knight's or wizard's Legendary rune by a quest gets its weaker kin instead.",
}

# ---- 2) consumables --------------------------------------------------------------------------------------
PC = IC
cons = {}


def card(k):
    path = None
    for d in ("misc", "pets"):
        p = os.path.join(os.getcwd(), "content/items", d, k + ".json")
        if os.path.exists(p):
            path = p
    return json.load(open(path)) if path else {}


def stack_of(r):
    g, n = r["group"], r["number"]
    if g == 14 and n in range(0, 9):
        return IC["kPotionStackMost"]
    if g == 14 and n == 11:
        return IC["kFirecrackerStackMost"]
    if G["refining_jewel"](r):
        return IC["kStackMost"]
    if (g == 14 and n in (9, 10, 17, 18, 51, 52)) or (g == 13 and n in (14, 16, 17)):
        return IC["kStackMost"]
    return 1


def price(k):
    return buying_price(ITEMS[k], 0, 1, False)


def bag_rows(name):
    return G["bag"](name)


def present(name, exc_only=False):
    out = []
    for gn in bag_rows(name):
        k = key_of(*gn)
        if k and (not exc_only or (G["excellentable"](ITEMS[k]) and G["takes_options"](ITEMS[k]))):
            out.append(k)
    return out


def contents(plain_name, item_share, plain_share, exc_name=None, exc_share=0.0):
    rows = {}
    plain = present(plain_name)
    for k in plain:
        rows.setdefault(k, [0.0, 0.0])[0] += item_share * plain_share / len(plain)
    if exc_name:
        fine = present(exc_name, True)
        for k in fine:
            rows.setdefault(k, [0.0, 0.0])[1] += item_share * exc_share / len(fine)
    out = []
    for k, (p, x) in sorted(rows.items(), key=lambda kv: -(kv[1][0] + kv[1][1])):
        e = {"k": k, "name": ITEMS[k]["name"], "chance": sig(p + x)}
        if x > 0:
            e["excellent_chance"] = sig(x)
        out.append(e)
    return out


SOUL, LIFE = IC["kSoulChance"], IC["kLifeChance"]
for k, r in sorted(ITEMS.items()):
    g, n = r["group"], r["number"]
    if g not in (12, 13, 14, 15) or k.startswith(("Gem", "Orb", "Wing", "Helper", "Ring", "Necklace")) or k == "Gold01":
        continue
    doc = card(k)
    does, rates = [], {}
    tells = (doc.get("tells") or "").strip()
    if tells:
        does.append(tells)
    if g == 14 and n == 13:
        does.append("Raises a weapon, armour piece or shield at +0 to +5 by one level.")
        rates = {"success": 100, "range": "+0..+5 to +1..+6"}
    elif g == 14 and n == 14:
        does.append("Raises a weapon, armour piece or shield at +0 to +8 by one level.")
        does.append("A failure takes one level off, and from +%d takes it to +0." % IC["kSoulResetFrom"])
        rates = {"success": SOUL, "with_luck": 100, "range": "+0..+8 to +1..+9", "fail": "-1 level, or to +0 from +%d" % IC["kSoulResetFrom"]}
    elif g == 14 and n == 16:
        does.append("Raises the additional option of a weapon, armour piece, shield or wing by one level, up to level %d." % IC["kMostOption"])
        does.append("A failure takes the item's option back to none.")
        rates = {"success": LIFE, "with_luck": 100, "most_option_level": IC["kMostOption"], "fail": "option back to none"}
        does.append("Also used in Add Socket and Fuse Runes at the Chaos Machine.")
    elif g == 12 and n == 15:
        does.append("The Chaos Machine's jewel: every mix and service needs it.")
        rates = {"note": "see mixes.json"}
    elif g == 14 and n == 22:
        does.append("Carries one rune power; set it into a free socket. See runes.json.")
    elif g == 14 and n in range(0, 8):
        rank = (n - 4 + 1) if n >= 4 else n
        mana = n >= 4
        pct = rank * 10
        does.append("Restores %s: %d%% of maximum %s plus %d, less your level (the flat part never below 0)." % (
            "Mana" if mana else "Life", pct, "mana" if mana else "life", (rank + 1) * 50))
        rates = {"percent_of_max": pct, "flat": (rank + 1) * 50, "flat_minus_level": True,
                 "plus_adds_percent": 1}
    elif g == 14 and n == 9:
        rates = {"attack_speed": IC["kAleSpeed"], "seconds": IC["kAleTicks"] // 20}
    elif g == 14 and n == 11:
        does.append("Opens on the ground: an item (%d in 10) from its list at +%d..+%d, else %s to %s Zen." % (
            IC["kFirecrackerItemIn10"], IC["kFirecrackerPlus"], IC["kFirecrackerPlus"] + IC["kFirecrackerPluses"] - 1,
            "{:,}".format(IC["kFirecrackerZenLeast"]), "{:,}".format(IC["kFirecrackerZenMost"])))
        rates = {"item": IC["kFirecrackerItemIn10"] * 10}
        cons_box = contents("kFirecrackerBag", IC["kFirecrackerItemIn10"] / 10, 1.0)
    elif g == 14 and n == 51:
        does.append("Opens on the ground: an item (%d%%) from its list at +%d, else %s Zen." % (
            IC["kLuckItemIn100"], IC["kLuckPlus"], "{:,}".format(IC["kLuckZen"])))
        rates = {"item": IC["kLuckItemIn100"]}
    elif g == 14 and n == 52:
        kz = array(items_h, "kKundunZen")
        ki = array(items_h, "kKundunItemIn100")
        kx = array(items_h, "kKundunExcellentIn100")
        kl = array(items_h, "kKundunPlainLevel")
        does.append("Opens on the ground. Its plus (+1 to +3) is its tier: a better tier holds better items.")
        rates = {"tiers": [{"plus": t + 1, "item": ki[t], "excellent_of_items": kx[t],
                            "plain_plus": "+%d..+%d" % (kl[t], kl[t] + IC["kKundunAddLevel"] - 1), "zen": kz[t]} for t in range(3)]}
    elif g == 13 and n == 14:
        does.append("Needed for the 2nd Level Wings mix at the Chaos Machine.")
    elif g == 13 and n in (16, 17):
        does.append("With its pair of the same level (+1..+6) and a Jewel of Chaos, makes an Invisibility Cloak at the Chaos Machine.")
    elif g == 13 and n == 18:
        does.append("Spent at the Messenger of Archangel in Devias to enter Blood Castle; its plus is the castle (+1..+6).")
    elif g == 14 and n in (17, 18):
        does.append("With its pair of the same level (+0..+4) and a Jewel of Chaos, makes a Devil's Invitation at the Chaos Machine.")
    elif g == 14 and n == 19:
        does.append("Spent at Charon in Noria to enter Devil Square; its plus is the square (+1..+4), a +0 fits any square.")
    elif g == 14 and n in (24, 25, 26):
        does.append("Found only during Sevina's class-change quest; hand it to her in Devias to become your class's second.")
    elif g == 15:
        sk = doc.get("skill") or {}
        if sk.get("tells"):
            does.append("Teaches %s: %s" % (sk.get("name") or "a spell", sk["tells"]))
        if sk.get("level_requirement") or sk.get("energy_requirement"):
            rates = {k2: v for k2, v in (("level", sk.get("level_requirement")), ("energy", sk.get("energy_requirement"))) if v}
    o = {"name": r["name"], "does": does}
    if rates:
        o["rates"] = rates
    o["stack"] = stack_of(r)
    pr = price(k)
    if pr:
        o["price"] = pr
        o["sells_for"] = (pr // 3) // 10 * 10 if (g == 14 and n <= 8) else G["mround"](pr // 3)  # sim::sellingPrice
    if g == 14 and n == 11:
        o["contents"] = cons_box
    if g == 14 and n == 51:
        o["contents"] = contents("kLuckBag", IC["kLuckItemIn100"] / 100, 1.0)
    if g == 14 and n == 52:
        ki = array(items_h, "kKundunItemIn100")
        kx = array(items_h, "kKundunExcellentIn100")
        o["contents"] = {"+%d" % (t + 1): contents("kKundunPlain%d" % (t + 1), ki[t] / 100, 1 - kx[t] / 100,
                                                   "kKundunExcellent%d" % (t + 1), kx[t] / 100) for t in range(3)}
    cons[k] = o

# ---- 3) mixes --------------------------------------------------------------------------------------------
GOBLIN = NPC_BY_NUMBER.get(238)
MOSS = NPC_BY_NUMBER.get(492)
CW = [key_of(*gn) for gn in [(2, 6), (4, 6), (5, 7)]]
cloak_zen = array(machine_h, "kCloakZen")
inv_zen = array(machine_h, "kInvitationZen")
inv_rate = array(machine_h, "kInvitationRate")
castle_bands = array(event_h, "kCastleBands")
square_bands = array(square_h, "kSquareBands")
add_rate = array(machine_h, "kAddSocketRate")
remove_zen = array(machine_h, "kRemoveRuneZen")
jc = strip_comments(src("src/sim/jeweler.h"))
salvage_odds = array(jc, "kSalvageOdds")
mixes = [
    {"id": "upgrade-10-11", "name": "Upgrade (+10 / +11)", "npc": GOBLIN,
     "needs": [{"name": "Item at +9 (for +10) or +10 (for +11)", "count": 1},
               {"k": "Jewel15", "count": 1}, {"k": "Jewel01", "count": "1 (+10) / 2 (+11)"},
               {"k": "Jewel02", "count": "1 (+10) / 2 (+11)"}],
     "zen": {"+10": MC["kPlusZen"], "+11": MC["kPlusZen"] * 2},
     "rate": {"+10": 50, "+11": 45, "luck": "+%d" % MC["kPlusLuck"], "max": MC["kPlusCap"]},
     "result": [{"note": "The item one level up"}],
     "on_fail": "The item and the jewels are lost", "notes": ["Nothing else may be in the box."]},
    {"id": "chaos-weapon", "name": "Chaos Weapon", "npc": GOBLIN,
     "needs": [{"name": "Item at +4 or better with an option or socket", "count": "1+"},
               {"k": "Jewel15", "count": "1+"}, {"k": "Jewel01", "count": "any", "note": "raises the rate"},
               {"k": "Jewel02", "count": "any", "note": "raises the rate"},
               {"name": "Excellent item +4 or better", "count": "any", "note": "raises the rate"}],
     "zen": "%s per %% of rate" % "{:,}".format(MC["kMixZenPerPoint"]),
     "rate": {"base": "the box's items' old buying price / 20,000 (jewels at Bless 100,000, Soul 70,000, Chaos 40,000)", "max": 100},
     "result": [{"k": k, "note": "one of the three at random, +0..+4"} for k in CW],
     "on_fail": "The jewels are lost and each item drops to a lower plus (its option a level down half the time)"},
    {"id": "first-wings", "name": "1st Level Wings", "npc": GOBLIN,
     "needs": [{"name": "Chaos weapon at +4 or better with an option or socket", "count": 1, "note": " / ".join(ITEMS[k]["name"] for k in CW)},
               {"k": "Jewel15", "count": "1+"}, {"k": "Jewel01", "count": "any"}, {"k": "Jewel02", "count": "any"},
               {"name": "Excellent item +4 or better", "count": "any", "note": "raises the rate"}],
     "zen": "%s per %% of rate" % "{:,}".format(MC["kMixZenPerPoint"]),
     "rate": {"base": "the box's old buying price / 20,000", "max": 100},
     "result": [{"k": key_of(12, n), "note": "one of the three at random, +0, whatever your class"} for n in range(3)],
     "on_fail": "The jewels are lost and each item drops to a lower plus"},
    {"id": "second-wings", "name": "2nd Level Wings", "npc": GOBLIN,
     "needs": [{"name": "1st level wing", "count": 1, "note": "Wings of Elf, Heaven or Satan"}, {"k": "Quest04", "count": 1},
               {"k": "Jewel15", "count": 1}, {"name": "Excellent item +4 or better", "count": "any", "note": "raises the rate"}],
     "zen": MC["kSecondWingsZen"],
     "rate": {"base": "the wing's price / 4,000,000 + the excellent items' / 40,000", "max": 100},
     "result": [{"k": key_of(12, 16), "note": "Dark Knight"}, {"k": key_of(12, 14), "note": "Dark Wizard"},
                {"k": key_of(12, 13), "note": "Fairy Elf"}, {"k": key_of(12, 22), "note": "Magic Gladiator"}],
     "on_fail": "Everything in the box is lost, the wing too",
     "notes": ["Your own class's 2nd wing, at +0; luck 1 in 5 and the option rolled."]},
    {"id": "dinorant", "name": "Dinorant", "npc": GOBLIN,
     "needs": [{"k": "Helper03", "count": MC["kDinorantHorns"], "note": "at full life"}, {"k": "Jewel15", "count": 1}],
     "zen": MC["kDinorantZen"], "rate": {"base": MC["kDinorantRate"]},
     "result": [{"k": "Helper04"}], "on_fail": "The horns and the Chaos are lost"},
    {"id": "invisibility-cloak", "name": "Invisibility Cloak", "npc": GOBLIN,
     "needs": [{"k": "EventBloodCastle00", "count": 1, "plus": "+1..+6"}, {"k": "EventBloodCastle01", "count": 1, "note": "same level as the scroll"},
               {"k": "Jewel15", "count": 1}],
     "zen": {"+%d" % lv: cloak_zen[lv] for lv in range(1, 7)}, "rate": {"base": MC["kCloakRate"]},
     "result": [{"k": "EventBloodCastle02", "note": "+%d enters Blood Castle %d (level %d+)" % (lv, lv, castle_bands[(lv - 1) * 2])} for lv in range(1, 7)],
     "on_fail": "The scroll, the bone and the Chaos are lost", "notes": ["You must be level %d or higher." % MC["kCloakFromLevel"]]},
    {"id": "devils-invitation", "name": "Devil's Invitation", "npc": GOBLIN,
     "needs": [{"k": "EventDevilSquare00", "count": 1, "plus": "+0..+4"}, {"k": "EventDevilSquare01", "count": 1, "note": "same level as the eye"},
               {"k": "Jewel15", "count": 1}],
     "zen": {"+%d" % lv: inv_zen[lv] for lv in range(5)}, "rate": {"+%d" % lv: inv_rate[lv] for lv in range(5)},
     "result": [{"k": "EventDevilSquare02", "note": ("+0 fits any square" if lv == 0 else "+%d enters Devil Square %d (level %d+)" % (lv, lv, square_bands[(lv - 1) * 2]))} for lv in range(5)],
     "on_fail": "The eye, the key and the Chaos are lost", "notes": ["You must be level %d or higher." % MC["kInvitationFromLevel"]]},
    {"id": "add-socket", "name": "Add Socket", "npc": GOBLIN,
     "needs": [{"name": "Weapon, armour, shield or wings with room for a socket", "count": 1},
               {"k": "Jewel03", "count": MC["kAddSocketLife"]}, {"k": "Jewel15", "count": MC["kAddSocketChaos"]},
               {"k": "Jewel02", "count": MC["kAddSocketSoul"]}, {"k": "Jewel01", "count": MC["kAddSocketBless"]}],
     "zen": MC["kAddSocketZen"], "rate": {"1st socket": add_rate[0], "2nd": add_rate[1], "3rd": add_rate[2]},
     "result": [{"note": "The item with one more socket (a ring holds one)"}], "on_fail": "The jewels are lost, the item is kept"},
    {"id": "remove-rune", "name": "Remove Rune", "npc": GOBLIN,
     "needs": [{"name": "Item with a rune set", "count": 1}, {"k": "Jewel15", "count": 1}],
     "zen": {"Common": remove_zen[3], "Rare": remove_zen[0], "Epic": remove_zen[1], "Legendary": remove_zen[2]},
     "rate": {"base": 100}, "result": [{"k": "Jewel22", "note": "the rune, as a Rune of Creation; the socket is empty again"}],
     "on_fail": None},
    {"id": "fuse-runes", "name": "Fuse Runes", "npc": GOBLIN,
     "needs": [{"k": "Jewel22", "count": MC["kFuseCount"], "note": "of one rarity, below Legendary"}, {"k": "Jewel15", "count": 1},
               {"k": "Jewel03", "count": MC["kFuseLife"]}],
     "zen": MC["kFuseZen"], "rate": {"base": 100},
     "result": [{"k": "Jewel22", "note": "a random rune of the next rarity your class can set (Common to Rare, Rare to Epic, Epic to Legendary)"}],
     "on_fail": None},
    {"id": "jeweler-combine", "name": "Jeweler: Combine", "npc": MOSS,
     "needs": [{"name": "Powered ring or pendant", "count": 3, "note": "all of one colour (white or blue)"}, {"k": "Jewel15", "count": 1}],
     "zen": {"white to blue": 500000, "blue to purple": 2000000}, "rate": {"base": 100},
     "result": [{"k": k, "note": "one at random, of the next colour, at the lowest plus of the three"} for k, r in ITEMS.items() if G["powered"](r)],
     "on_fail": None, "notes": ["Legendary pieces are only ever found."]},
    {"id": "jeweler-recut", "name": "Jeweler: Recut", "npc": MOSS,
     "needs": [{"name": "Powered ring or pendant", "count": 1}, {"k": "Jewel15", "count": 1}],
     "zen": {"+%d" % p: int(round((50000 + 450000 * (p / 9) ** 2) / 1000)) * 1000 for p in range(10)},
     "rate": {"base": 100},
     "result": [{"note": "One further power rolls again, from those the piece does not carry; the colour never changes"}], "on_fail": None},
    {"id": "jeweler-salvage", "name": "Jeweler: Salvage", "npc": MOSS,
     "needs": [{"name": "Powered ring or pendant", "count": 1, "note": "broken for one prize"}],
     "zen": 0,
     "rate": {"note": "odds by the piece's colour; +%d or lucky pieces roll twice and keep the better" % 7,
              "odds": {colour: {"Small Healing Potion x3": salvage_odds[i * 5], "Jewel of Chaos": salvage_odds[i * 5 + 1],
                                "Jewel of Bless": salvage_odds[i * 5 + 2], "Jewel of Soul": salvage_odds[i * 5 + 3],
                                "Jewel of Life": salvage_odds[i * 5 + 4]}
                       for i, colour in enumerate(["white", "blue", "purple", "legendary"])}},
     "result": [{"k": "Potion02", "note": "x3"}, {"k": "Jewel15"}, {"k": "Jewel01"}, {"k": "Jewel02"}, {"k": "Jewel03"}],
     "on_fail": None, "notes": ["Never a rune."]},
]

# ---- 4) elites -------------------------------------------------------------------------------------------
elite_maps = re.findall(r"\{(\d+),\s*(\d+),\s*([\d.]+)f\}", elite_h)
never = set(array(elite_h, "kNeverElite"))
BY_NUMBER = {WORLDS[w].get("map"): w for w in LIVE}
elites = []
for num, count, life in elite_maps:
    w = BY_NUMBER.get(int(num))
    if not w:
        continue
    p = os.path.join(os.getcwd(), "content/world", w, "nests.json")
    breeds = sorted({x["breed"] for x in json.load(open(p))["nests"]})
    ok = sorted([b for b in breeds if b in BREEDS and b not in never], key=lambda b: (-BREEDS[b]["level"], b))[:EC["kEliteStrongest"]]
    elites.append({
        "map": MAPNAME[w], "map_key": w, "count": int(count), "life_x": float(life),
        "damage_x": EC["kEliteDamage"], "defense_x": EC["kEliteDefense"],
        "attack_speed": "+%d%%" % round((EC["kEliteSwing"] - 1) * 100), "move_speed": "+%d%%" % round((EC["kEliteStride"] - 1) * 100),
        "experience_x": EC["kEliteExperience"], "zen_x": EC["kEliteZen"], "level_plus": EC["kEliteLevelsUp"],
        "breeds": [{"name": BREEDS[b]["name"], "level": BREEDS[b]["level"]} for b in ok],
        "loot": ["%d to %d pieces of gear (weapons, armour, shields) from up to %d levels over its own, at the plus its level gives" % (
                     EC["kEliteLeastPieces"], EC["kEliteMostPieces"], EC["kEliteLevelsUp"]),
                 "One piece always excellent, a second one %d%% of the time" % EC["kEliteSecondExcellentIn100"],
                 "Luck %d%%, the option rolled twice and the better kept; sockets as any drop" % EC["kEliteLuckIn100"],
                 "A Jewel of Bless, Soul or Chaos always, a second %d%% of the time; never the Jewel of Life" % EC["kEliteSecondJewelIn100"],
                 "No runes, no potions, and none of an ordinary kill's other drops",
                 "%d times an ordinary kill's Zen, straight to the purse; its things lie %d s on the ground" % (EC["kEliteZen"], EC["kEliteLingerTicks"] // 20)],
        "immune": ["slow (chill)", "freeze", "knock-back and pull"],
        "alert": "pulsing gold minimap dot + bell",
        "respawn": "%d to %d minutes after it falls, as a new breed in a new place" % (EC["kEliteRiseLeastTicks"] // 1200, EC["kEliteRiseMostTicks"] // 1200),
        "spawns": "never within %d tiles of a gate or on a safe tile" % EC["kEliteGateClear"],
    })

NOTE = "Generated from MU2_BGFX HEAD; Kanturu and charscene left out."
for name, data in (("runes.json", {"_note": NOTE + " Rune per-kill odds: 0.05% creation share x this rune's share for that killer, at the band's top level, before the 1-2% early-return rolls; MG takes no second-class runes.", "_rules": rules, "runes": runes}),
                   ("consumables.json", {"_note": NOTE + " Prices at +0, sells_for a third. Box contents: chance per opened box.", **cons}),
                   ("mixes.json", {"_note": NOTE, "mixes": mixes}),
                   ("elites.json", {"_note": NOTE + " Elites never appear in Blood Castle or Devil Square; bosses are never Elites.", "elites": elites})):
    with open(os.path.join(HERE, name), "w") as f:
        json.dump(data, f, separators=(",", ":"), ensure_ascii=False)
    print(name, os.path.getsize(os.path.join(HERE, name)))
print("runes", len(runes), "consumables", len(cons), "mixes", len(mixes), "elite maps", len(elites))
