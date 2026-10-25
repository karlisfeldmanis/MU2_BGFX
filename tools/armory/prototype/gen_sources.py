#!/usr/bin/env python3
"""Where every MU2_BGFX item comes from, for the website's item database.

Run from the MU2_BGFX directory:  python3 <this> [out.json]
Reads content/ (items, monsters, worlds) and parses the sim's C++ tables (market.cpp,
quests.cpp, event.h, square.h, items.h, realm_machine.cpp). Read-only on the repo.
"""
import glob
import json
import os
import re
import sys

ROOT = os.getcwd()
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "sources.json")


def src(path):
    with open(os.path.join(ROOT, path)) as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


# ---- items -------------------------------------------------------------------------------------
ITEMS = {}      # key -> row dict
BY_GN = {}      # (group, number) -> key
for path in sorted(glob.glob(os.path.join(ROOT, "content/items/*/*.json"))):
    if path.endswith(".rig.json"):
        continue
    doc = json.load(open(path))
    s = doc.get("stats") or {}
    if "item" not in doc or "group" not in s:
        continue
    classes = s.get("classes") or []
    row = {
        "key": doc["item"], "name": doc.get("mu_name") or doc["item"],
        "group": int(s["group"]), "number": int(s["number"]),
        "drop": int(s.get("drop_level") or 0),
        "drops": s.get("drops_from_monsters", True) is not False,
        "jewel": bool(s.get("jewel")),
        "maxdrop": int(s.get("maximum_drop_level") or 0),
        "second": int(s.get("class_level") or 1) >= 2 and bool(classes or s.get("second_classes")),
        "width": int(s.get("width") or 1),
    }
    # cook.py: classes |= asked << 8 for class_level >= 2; an item with no classes has asked 0.
    if int(s.get("class_level") or 1) >= 2:
        seconds = s.get("second_classes")
        row["second"] = bool(seconds if seconds is not None else classes)
    ITEMS[row["key"]] = row
    BY_GN[(row["group"], row["number"])] = row["key"]

G_BOOTS, G_PETS, G_POTIONS, G_BOWS = 11, 13, 14, 4


def ammunition(r): return r["group"] == G_BOWS and r["number"] in (7, 15)
def ring(r): return r["group"] == G_PETS and (r["number"] in (8, 9) or 21 <= r["number"] <= 24 or 26 <= r["number"] <= 28)
def pendant(r): return r["group"] == G_PETS and r["number"] in (12, 13, 25, 29)
def jewellery(r): return ring(r) or pendant(r)
def powered(r): return r["group"] == G_PETS and r["number"] in (21, 22, 23, 24, 25, 29)
def takes_options(r): return r["group"] <= G_BOOTS and not ammunition(r)
def excellentable(r): return takes_options(r) or jewellery(r)
def summoning_orb(r): return r["group"] == 12 and r["number"] == 11
def refining_jewel(r): return (r["group"] == G_POTIONS and r["number"] in (13, 14, 16)) or (r["group"] == 12 and r["number"] == 15)
def is_life(r): return r["group"] == G_POTIONS and r["number"] == 16
def creation(r): return r["group"] == G_POTIONS and r["number"] == 22
def jewel_group(r): return (r["jewel"] or (r["group"] == G_POTIONS and r["number"] in (9, 10))) and not refining_jewel(r)
def first_wing(r): return r["group"] == 12 and r["number"] <= 2
def second_wing(r): return r["group"] == 12 and r["number"] in (13, 14, 16, 22)


def key_of(group, number):
    return BY_GN.get((group, number))


# ---- maps --------------------------------------------------------------------------------------
DORMANT = {"kanturu", "kanturu_relics", "kanturu_tower", "charscene"}
WORLDS = {}
for path in sorted(glob.glob(os.path.join(ROOT, "content/world/*/world.json"))):
    folder = os.path.basename(os.path.dirname(path))
    doc = json.load(open(path))
    WORLDS[folder] = doc
LIVE = [w for w in WORLDS if w not in DORMANT]
MAPNAME = {w: WORLDS[w].get("name", w) for w in WORLDS}


def rules(world):
    return WORLDS[world].get("rules") or {}


def drops_rule(world, what):
    return what in (rules(world).get("drops") or [])


# ---- monsters ----------------------------------------------------------------------------------
BREEDS = {}  # number -> {name, level, maxplus}
for path in sorted(glob.glob(os.path.join(ROOT, "content/monsters/*.json"))):
    if path.endswith(".rig.json"):
        continue
    doc = json.load(open(path))
    n = doc.get("object_type")
    if n is None or not doc.get("combat"):
        continue
    d = (doc.get("rules") or {}).get("drops") or {}
    BREEDS[int(n)] = {"name": doc.get("mu_name") or doc["item"], "level": int(doc["combat"].get("level") or 0),
                      "maxplus": int(d.get("max_plus", 6))}  # dropRateOf's default {10, 6}

event_h = strip_comments(src("src/sim/event.h"))
square_h = strip_comments(src("src/sim/square.h"))


def int_rows(text, name):
    m = re.search(name + r"[^=]*=\s*\{(.*?)\};", text, re.S)
    return [int(x) for x in re.findall(r"-?\d+", m.group(1))]


castle_flat = int_rows(event_h, r"kCastleBreeds\[6\]\[6\]")
CASTLE_BREEDS = [castle_flat[i * 6:(i + 1) * 6] for i in range(6)]
sq = int_rows(square_h, r"kSquareBreeds\[4\]\[kSquareWaves\]\[2\]")
SQUARE_BREEDS = [sorted(set(sq[i * 6:(i + 1) * 6])) for i in range(4)]
SQUARE_BOSS = int_rows(square_h, r"kSquareBoss\[4\]")
for i in range(4):
    SQUARE_BREEDS[i].append(SQUARE_BOSS[i])
STATUES = {132, 133, 134}
NEVER_ELITE = set(int_rows(strip_comments(src("src/sim/elite.h")), r"kNeverElite\[\]"))
ELITE_MAPS = {}
for m, count in re.findall(r"\{(\d+),\s*(\d+),\s*[\d.]+f\}", strip_comments(src("src/sim/elite.h"))):
    ELITE_MAPS[int(m)] = int(count)

# Every hunting ground: (label, world folder, breed, flags)
GROUNDS = []
for w in LIVE:
    if w in ("bloodcastle", "devilsquare"):
        continue
    path = os.path.join(ROOT, "content/world", w, "nests.json")
    if not os.path.exists(path):
        continue
    breeds = sorted({n["breed"] for n in json.load(open(path))["nests"]})
    for b in breeds:
        if b in BREEDS:
            GROUNDS.append({"map": MAPNAME[w], "world": w, "breed": b, "level": BREEDS[b]["level"]})
    # Elites: the map's three strongest ordinary breeds, two levels up (sim/elite.h, realm_elite.cpp).
    if ELITE_MAPS.get(WORLDS[w].get("map"), 0) > 0:
        ok = sorted([b for b in breeds if b in BREEDS and b not in NEVER_ELITE],
                    key=lambda b: (-BREEDS[b]["level"], b))[:3]
        for b in ok:
            GROUNDS.append({"map": MAPNAME[w], "world": w, "breed": b, "level": BREEDS[b]["level"] + 2, "elite": True})
for c in range(6):
    for b in CASTLE_BREEDS[c]:
        if b in BREEDS:
            GROUNDS.append({"map": "Blood Castle %d" % (c + 1), "world": "bloodcastle", "breed": b,
                            "level": BREEDS[b]["level"], "castle": c + 1})
for s in range(4):
    for b in SQUARE_BREEDS[s]:
        if b in BREEDS:
            GROUNDS.append({"map": "Devil Square %d" % (s + 1), "world": "devilsquare", "breed": b,
                            "level": BREEDS[b]["level"], "square": s + 1})
# The Golden Invasion: the Golden Dragon lands on maps with rules.invasion; its raid raises Golden
# Budge Dragons (43). Both leave the ordinary drop too (realm_fight.cpp kill -> leave).
for w in LIVE:
    if rules(w).get("invasion"):
        for b in (79, 43):
            if b in BREEDS:
                GROUNDS.append({"map": MAPNAME[w] + " (Golden Invasion)", "world": w, "breed": b,
                                "level": BREEDS[b]["level"], "invasion": True})

OUTPUT = {k: {} for k in ITEMS}


def add(key, field, entry):
    if key is None or key not in OUTPUT:
        return
    lst = OUTPUT[key].setdefault(field, [])
    if entry not in lst:
        lst.append(entry)


# ---- monster drops (src/sim/realm_items.cpp Realm::leave, realm_elite.cpp eliteSpoils) -----------
# Every number below is the sim's (items.h, elite.h, raid.h); chances are for ONE kill, solo, no Item
# Find, no talents, no party or raider credit.
K_GAP, K_POTION_GAP, K_REFINE_CAP, K_JEWELLERY_PLUS = 15, 8, 9, 4
K_EXC_DELTA, K_DEEP_FROM, K_DEEP_SHARE = 25, 18, 0.15
K_ELITE_UP = 2
C_CREATION, C_JEWEL, C_JEWEL_GROUP = 0.0005, 0.005, 0.001       # kCreationChance, kJewelChance, kJewelGroupChance
C_ITEM = 0.1                                                     # itemChance at find 1
C_EXCELLENT = 0.1 * (1.0 / 100.0)                                # excellentItemChance
P_SCROLL = P_EYE = 0.01                                          # 100 in 10000 each, then the bone/key
ELITE_PIECES = range(4, 8)                                       # kEliteLeastPieces..kEliteMostPieces
ELITE_SECOND_EXC = 0.35
ELITE_JEWEL_FIRST, ELITE_JEWEL_SECOND = 1.0, 0.5
MINION_JEWEL, MINION_RUNE = 1 / 4, 1 / 10                        # kMinionJewelOdds, kMinionRuneOdds
HOARD_JEWELS, HOARD_FEATHER, HOARD_EXTRA_EXC = 3, 1 / 4, 1 / 3   # kHoardJewels, kHoardFeatherOdds, kHoardExcellentOdds


def sig(x):
    return float("%.4g" % x)


MON = {}  # key -> {(name, map): entry}


def mon_add(key, g, plain=0.0, exc=0.0, own=0.0, how=None):
    """Adds one source of `key` from ground `g`: `plain` the plain main-roll share, `exc` the
    excellent share (both mutually exclusive branches of one roll, or for elites independent
    pieces' at-least-once odds), `own` an independent separate roll."""
    if plain <= 0 and exc <= 0 and own <= 0:
        return
    b = BREEDS[g["breed"]]
    name = ("Elite " + b["name"]) if g.get("elite") else b["name"]
    slot = MON.setdefault(key, {})
    e = slot.get((name, g["map"]))
    if e is None:
        e = {"name": name, "level": g["level"], "map": g["map"], "_p": 0.0, "_x": 0.0, "_o": 0.0, "_hows": []}
        if g.get("elite"):
            e["elite"] = True
        slot[(name, g["map"])] = e
    e["_p"] += plain
    e["_x"] += exc
    e["_o"] = 1 - (1 - e["_o"]) * (1 - own)
    if how and how not in e["_hows"]:
        e["_hows"].append(how)
    if plain > 0 or exc > 0:
        e["_main"] = True


def second_here(g):
    return bool(rules(g["world"]).get("second_class")) or g.get("castle") == 6


def elite_spoils(g):
    L, sec = g["level"], second_here(g)
    gear = [k for k, r in ITEMS.items()
            if r["drops"] and r["group"] <= G_BOOTS and not ammunition(r) and not jewellery(r)
            and not summoning_orb(r) and L - K_GAP <= r["drop"] <= L
            and (r["maxdrop"] == 0 or L <= r["maxdrop"]) and (sec or not r["second"])]
    exc_pool = [k for k in gear if excellentable(ITEMS[k])]
    for k in gear:
        pp = px = 0.0
        for n in ELITE_PIECES:
            for e_count, pe in ((1, 1 - ELITE_SECOND_EXC), (2, ELITE_SECOND_EXC)):
                w = pe / len(ELITE_PIECES)
                pp += w * (1 - (1 - 1 / len(gear)) ** (n - e_count))
                if k in exc_pool:
                    px += w * (1 - (1 - 1 / len(exc_pool)) ** e_count)
        # plain and excellent pieces are drawn independently; report each, total as at-least-one
        mon_add(k, g, plain=pp, exc=px)
    jewels = [k for k, r in ITEMS.items() if refining_jewel(r) and not is_life(r)]
    miss = 1 - 1 / len(jewels)
    pj = ELITE_JEWEL_FIRST * ((1 - ELITE_JEWEL_SECOND) * (1 - miss) + ELITE_JEWEL_SECOND * (1 - miss ** 2))
    for k in jewels:
        mon_add(k, g, own=pj, how="elite spoils: 1-2 jewels")


def leave(g):
    L, sec, world = g["level"], second_here(g), g["world"]
    maxplus = BREEDS[g["breed"]]["maxplus"]

    def reaches(r):
        return r["drop"] <= L and (r["maxdrop"] == 0 or L <= r["maxdrop"]) and (sec or not r["second"])

    # The early returns: Blood Castle's scroll then bone (not in a castle), Devil Square's eye then
    # key (not in the square, monster above 15). One that lands is the kill's whole drop.
    pre = 1.0
    if world != "bloodcastle":
        for n, p in ((16, P_SCROLL), (17, (1 - P_SCROLL) * P_SCROLL)):
            lvl = 1 if L < 32 else 2 if L < 45 else 3 if L < 57 else 4 if L < 68 else 5 if L < 76 else 6
            mon_add(key_of(13, n), g, own=p, how="own roll, 1 in 100, +%d" % lvl)
        pre *= (1 - P_SCROLL) ** 2
    if world != "devilsquare" and L > 15:
        lvl = 1 if L <= 40 else 2 if L <= 54 else 3 if L <= 66 else 4
        for n, p in ((17, P_EYE), (18, (1 - P_EYE) * P_EYE)):
            mon_add(key_of(14, n), g, own=pre * p, how="own roll, 1 in 100, +%d" % lvl)
        pre *= (1 - P_EYE) ** 2
    # The separate rolls beside the main one (each reached only past the early returns).
    if L >= 17 and drops_rule(world, "firecracker"):
        mon_add(key_of(14, 11), g, own=pre / 300, how="own roll, 1 in 300")
    if L >= 60 and (drops_rule(world, "feather") or g.get("castle") == 6):
        mon_add(key_of(13, 14), g, own=pre / 500, how="own roll, 1 in 500")
    if L >= 60 and drops_rule(world, "nova_scroll"):
        mon_add(key_of(15, 18), g, own=pre / 1500, how="own roll, 1 in 1500")
    if drops_rule(world, "first_wings"):
        for n in range(3):
            mon_add(key_of(12, n), g, own=pre / 1000, how="own roll, 1 in 1000, only the killer's class's wing")
    if L >= 43 and drops_rule(world, "second_gear"):
        pool = [k for k, r in ITEMS.items() if r["drops"] and r["second"]]
        for k in pool:
            mon_add(k, g, own=pre / 400 / len(pool), how="own roll, 1 in 400, any second-class piece")
    for k, r in ITEMS.items():
        if summoning_orb(r) and r["drop"] <= L:
            mon_add(k, g, own=pre / 150, how="own roll, 1 in 150")
            break
    if L >= 50:
        mon_add(key_of(14, 16), g, own=pre / 2500, how="own roll, 1 in 2500, level 50+")
    # The main roll: rune, refining jewel, jewel group, excellent, item; else Zen or nothing.
    if L >= 1:
        runes = [k for k, r in ITEMS.items() if creation(r)]
        for k in runes:
            mon_add(k, g, plain=pre * C_CREATION / len(runes))
    pool = [k for k, r in ITEMS.items() if refining_jewel(r) and not is_life(r) and reaches(r)]
    for k in pool:
        mon_add(k, g, plain=pre * C_JEWEL / len(pool))
    pool = [k for k, r in ITEMS.items() if jewel_group(r) and reaches(r)]
    for k in pool:
        mon_add(k, g, plain=pre * C_JEWEL_GROUP / len(pool))
    lower = L - K_EXC_DELTA
    if lower >= 0:
        pool = [k for k, r in ITEMS.items()
                if r["drops"] and excellentable(r) and r["drop"] <= lower
                and (r["maxdrop"] == 0 or lower <= r["maxdrop"]) and r["drop"] >= lower - K_GAP
                and (sec or not r["second"])]
        for k in pool:
            mon_add(k, g, exc=pre * C_EXCELLENT / len(pool))

    def admits(r, deep):
        if not (r["drops"] and not summoning_orb(r) and reaches(r)):
            return False
        gap = K_POTION_GAP if r["group"] == G_POTIONS else K_GAP
        if not (r["drop"] >= L - gap or (deep and powered(r))):
            return False
        refinable = r["group"] <= G_BOOTS and not ammunition(r)
        if jewellery(r):
            plus = max(0, min((L - r["drop"]) // 3, K_JEWELLERY_PLUS))
        else:
            plus = max(0, min((L - r["drop"]) // 3, K_REFINE_CAP)) if refinable else 0
        return plus <= maxplus

    shares = [(False, 1.0)]
    if L - K_GAP > K_DEEP_FROM:
        shares = [(False, 1 - K_DEEP_SHARE), (True, K_DEEP_SHARE)]
    for deep, share in shares:
        pool = [k for k, r in ITEMS.items() if admits(r, deep)]
        for k in pool:
            mon_add(k, g, plain=pre * C_ITEM * share / len(pool))
    # The Golden Invasion's raid: every minion's spoils, and the dragon's hoard (realm_raid.cpp).
    if g.get("invasion") and g["breed"] == 43:
        for k, r in ITEMS.items():
            if r["group"] == G_POTIONS and r["number"] == 51:
                mon_add(k, g, own=1.0, how="raid minion spoils: always")
        rj = [k for k, r in ITEMS.items() if refining_jewel(r)]
        for k in rj:
            mon_add(k, g, own=MINION_JEWEL / len(rj), how="raid minion spoils")
        for k, r in ITEMS.items():
            if creation(r):
                mon_add(k, g, own=MINION_RUNE, how="raid minion spoils")
    if g.get("invasion") and g["breed"] == 79:
        for k, r in ITEMS.items():
            if r["group"] == G_POTIONS and r["number"] in (52, 22):
                mon_add(k, g, own=1.0, how="dragon's hoard: always")
        rj = [k for k, r in ITEMS.items() if refining_jewel(r)]
        for k in rj:
            mon_add(k, g, own=1 - (1 - 1 / len(rj)) ** HOARD_JEWELS, how="dragon's hoard")
        mon_add(key_of(13, 14), g, own=HOARD_FEATHER, how="dragon's hoard")
        fine = [key_of(*gn) for gn in KUNDUN_EXC3 if key_of(*gn) and takes_options(ITEMS[key_of(*gn)])]
        for k in fine:
            p1 = 1 - (1 - 1 / len(fine))
            p2 = 1 - (1 - 1 / len(fine)) ** 2
            mon_add(k, g, own=(1 - HOARD_EXTRA_EXC) * p1 + HOARD_EXTRA_EXC * p2, how="dragon's hoard: excellent")


items_h_src = strip_comments(src("src/sim/items.h"))
KUNDUN_EXC3 = [tuple(map(int, p)) for p in re.findall(
    r"\{(\d+),\s*(\d+)\}", re.search(r"inline constexpr BagRow kKundunExcellent3\[\] = \{(.*?)\};", items_h_src, re.S).group(1))]

for g in GROUNDS:
    if g["level"] <= 0 or g["breed"] in STATUES:
        continue
    if g.get("elite"):
        elite_spoils(g)
    else:
        leave(g)

for k, slot in MON.items():
    lst = []
    for e in slot.values():
        p, x, o = e.pop("_p"), e.pop("_x"), e.pop("_o")
        hows = e.pop("_hows")
        main = e.pop("_main", False)
        if e.get("elite"):
            total = 1 - (1 - p) * (1 - x) * (1 - o)  # independent pieces
        else:
            total = 1 - (1 - (p + x)) * (1 - o)       # one main roll and independent own rolls
        e["excellent"] = x > 0
        if x > 0 and p <= 0:
            e["plain"] = False
        if hows and not main:
            e["how"] = hows[0] if len(hows) == 1 else "; ".join(hows)
        elif hows:
            e["also"] = "; ".join(hows)
        e["chance"] = sig(total)
        if x > 0 and p > 0:
            e["chance_plain"] = sig(p)
            e["chance_excellent"] = sig(x)
        lst.append(e)
    lst.sort(key=lambda e: (e["level"], e["map"], e["name"]))
    OUTPUT[k]["monsters"] = lst

# ---- specials (world.json rules.drops and the other map rules) ----------------------------------
def worlds_with(what):
    return [MAPNAME[w] for w in LIVE if drops_rule(w, what)]


add(key_of(14, 11), "special", {"map": ", ".join(worlds_with("firecracker")),
                                "note": "Firecracker: any kill of level 17+, 1 in 300, beside the normal drop"})
add(key_of(13, 14), "special", {"map": ", ".join(worlds_with("feather") + ["Blood Castle 6"]),
                                "note": "Loch's Feather: any kill of level 60+, 1 in 500"})
add(key_of(15, 18), "special", {"map": ", ".join(worlds_with("nova_scroll")),
                                "note": "Scroll of Nova: any kill of level 60+, 1 in 1500"})
for n in range(3):
    add(key_of(12, n), "special", {"map": ", ".join(worlds_with("first_wings")),
                                   "note": "Any kill, 1 in 1000: drops the killer's own class's 1st wing (Elf: Wings of Elf, DW: Heaven, DK/MG: Satan)"})
for k, r in ITEMS.items():
    if r["drops"] and r["second"]:
        add(k, "special", {"map": ", ".join(worlds_with("second_gear")),
                           "note": "Second-class gear: any kill of level 43+ leaves a random second-class piece, 1 in 400"})
        add(k, "special", {"map": "Tarkan, Icarus, Aida, Blood Castle 6",
                           "note": "Second-class gear drops normally only on maps with rules.second_class and in Blood Castle 6"})
add(key_of(14, 16), "special", {"map": "any", "note": "Jewel of Life: any kill of level 50+, 1 in 2500"})
for n in (16, 17):
    add(key_of(13, n), "special", {"map": "any but Blood Castle",
                                   "note": "Any kill, 1 in 100, at +1..+6 by the monster's level (+1 <32, +2 <45, +3 <57, +4 <68, +5 <76, +6 76+)"})
for n in (17, 18):
    add(key_of(14, n), "special", {"map": "any but Devil Square",
                                   "note": "Any kill above level 15, 1 in 100, at +1..+4 by the monster's level (+1 <=40, +2 <=54, +3 <=66, +4 67+)"})
add(key_of(12, 11), "special", {"map": "any", "note": "Orb of Summoning: own roll 1 in 150 from any monster of level 3+, +1 per 12 levels over 3, up to +4"})
add(key_of(14, 22), "special", {"map": "any", "note": "Rune of Creation: 0.05% of kills of any level; Ancient runes only in Kanturu (dormant)"})

# Sevina's treasures (realm_quests.cpp Realm::treasure)
for qk in ("Quest01", "Quest02", "Quest03"):
    add(qk, "special", {"map": "Lost Tower 7, Atlans, Blood Castle, Devil Square",
                        "note": "Falls (2% of kills) only while Sevina's class-change treasure quest is active: on the trial's ground, or in BC/DS from monsters of level 62+"})

# ---- shops (src/sim/market.cpp) ------------------------------------------------------------------
market = strip_comments(src("src/sim/market.cpp"))
consts = {"kHelms": 7, "kArmours": 8, "kPants": 9, "kGloves": 10, "kBoots": 11, "kShields": 6,
          "kSwords": 0, "kAxes": 1, "kMaces": 2, "kSpears": 3, "kBows": 4, "kStaves": 5,
          "kOrbs": 12, "kPets": 13, "kPotions": 14, "kScrolls": 15}
for name, val in re.findall(r"constexpr int (k\w+) = (\d+)", market):
    consts[name] = int(val)
for line in re.findall(r"constexpr int ([^;]+);", market):
    for name, val in re.findall(r"(k\w+) = (\d+)", line):
        consts[name] = int(val)


def num(tok):
    tok = tok.strip()
    return consts[tok] if tok in consts else int(tok)


SHELVES = {}
for name, body in re.findall(r"constexpr Offer (k\w+)\[\] = \{(.*?)\};", market, re.S):
    offers = []
    for fn, args in re.findall(r"\b(sip|gear|scroll)\(([^)]*)\)", body):
        a = [x.strip() for x in args.split(",")]
        if fn == "sip":
            offers.append({"group": 14, "number": num(a[1]), "plus": 0, "pieces": num(a[2]), "skill": False})
        elif fn == "gear":
            offers.append({"group": num(a[1]), "number": num(a[2]), "plus": num(a[3]), "pieces": 0,
                           "skill": len(a) > 4 and a[4] == "true"})
        else:
            offers.append({"group": 15, "number": num(a[1]), "plus": 0, "pieces": 0, "skill": False})
    SHELVES[name] = offers
STOCK = {int(n): t for n, t in re.findall(r"case (\d+): return table\((k\w+), count\)", market)}

NPCS = {}  # npc number -> [(name, map)]
for w in LIVE:
    for one in WORLDS[w].get("npcs") or []:
        lst = NPCS.setdefault(int(one["npc"]), [])
        if (one["name"], MAPNAME[w]) not in lst:
            lst.append((one["name"], MAPNAME[w]))


def mround(p):
    return p // 100 * 100 if p >= 1000 else (p // 10 * 10 if p >= 100 else p)


STEEPER = [0, 0, 0, 0, 0, 4, 10, 25, 45, 65, 95, 135, 185, 245, 305, 365]


def worth(n):
    return {0: 5, 1: 10, 4: 10, 8: 10, 2: 20, 5: 20, 3: 30, 6: 30, 9: 30, 10: 30}.get(n, 0)


SPELL = [17000, 11000, 3000, 300, 21000, 5000, 14000, 25000, 35000, 60000, 1100, 100000]


def orb_price(n): return {8: 800, 10: 7000, 11: 150}.get(n, 0)


def jewel_price(g, n):
    if g == 12: return 40000 if n == 15 else 0
    if g != 14: return 0
    return {13: 100000, 14: 70000, 16: 250000, 22: 400000}.get(n, 0)


def quiver(n, plus):
    plus = max(0, min(plus, 3))
    return [70, 1200, 2000, 2800][plus] if n == 15 else ([100, 1400, 2200, 3000][plus] if n == 7 else 0)


def buying_price(r, refinement, pieces, skill):
    """sim::buyingPrice with no luck, option or excellent (nothing on a shelf has them)."""
    g, n, d = r["group"], r["number"], r["drop"]
    if g == 4 and quiver(n, refinement) > 0:
        return mround(quiver(n, refinement))
    if jewel_price(g, n) > 0:
        return jewel_price(g, n) * max(1, pieces)
    if g == 15 and 0 <= n < 12 and SPELL[n] > 0:
        return mround(SPELL[n])
    if g == 12 and orb_price(n) > 0:
        return mround(orb_price(n))
    if g == 13:
        dl = d + refinement * 3
        base = dl * dl * dl + 100
        return mround(base)
    if g == 14 and worth(n) > 0:
        v = worth(n)
        price = v * v * 10 // 12
        if n > 8:
            return mround(price)
        if refinement > 0:
            price *= 2 ** refinement
        return mround(price // 10 * 10 * (pieces if pieces > 1 else 1))
    if first_wing(r) or second_wing(r):
        lv = d + refinement * 3 + STEEPER[refinement]
        return mround(40000000 + (40 + lv) * lv * lv * 11)
    dl = d + refinement * 3 + STEEPER[refinement]
    reckoned = (dl + 40) * dl * dl // 8 + 100
    if (g < 6 and r["width"] < 2) or g == 6:
        reckoned = reckoned * 80 // 100
    if skill:
        reckoned += int(reckoned * 1.5)
    return mround(reckoned)


for npc, table in sorted(STOCK.items()):
    for offer in SHELVES.get(table, []):
        k = key_of(offer["group"], offer["number"])
        if k is None:
            continue
        for name, where in NPCS.get(npc, [("NPC %d" % npc, "?")]):
            e = {"npc": name, "map": where, "plus": offer["plus"],
                 "price": buying_price(ITEMS[k], offer["plus"], offer["pieces"] or 1, offer["skill"])}
            if offer["pieces"] > 1:
                e["pieces"] = offer["pieces"]
            add(k, "shops", e)

# ---- the Chaos Machine (machine.h, machine.cpp, realm_machine.cpp) and the Jeweler (jeweler.h) ---
CHAOS = "Jewel15"
BLESS, SOUL, LIFE, RUNE, FEATHER = key_of(14, 13), key_of(14, 14), key_of(14, 16), key_of(14, 22), key_of(13, 14)
rm = strip_comments(src("src/sim/realm_machine.cpp"))
chaos_weapons = [tuple(map(int, p)) for p in re.findall(r"\{(\d+),\s*(\d+)\}", re.search(r"kChaosWeapons\[3\]\[2\] = \{(.*?)\};", rm, re.S).group(1))]
CHAOS_WEAPON_KEYS = [key_of(*gn) for gn in chaos_weapons]


def nm(k):
    return ITEMS[k]["name"] if k in ITEMS else k


RECIPES = [
    {"recipe": "Chaos Weapon (Chaos Machine, Chaos Goblin in Noria)",
     "makes": CHAOS_WEAPON_KEYS,
     "needs": ["1+ items at +4 or better with an option (or excellent +4)", "1+ Jewel of Chaos",
               "any Jewels of Bless and Soul (raise the rate)", "Zen 5,000 per % of rate"],
     "uses": [CHAOS, BLESS, SOUL],
     "note": "one of the three at random, +0..+4"},
    {"recipe": "1st Level Wings (Chaos Machine)",
     "makes": [key_of(12, 0), key_of(12, 1), key_of(12, 2)],
     "needs": ["a Chaos weapon (%s) at +4 or better with an option" % " / ".join(nm(k) for k in CHAOS_WEAPON_KEYS if k),
               "1+ Jewel of Chaos", "any Jewels of Bless and Soul", "any excellent +4 items (raise the rate)",
               "Zen 5,000 per % of rate"],
     "uses": [CHAOS, BLESS, SOUL] + CHAOS_WEAPON_KEYS,
     "note": "Wings of Elf, Heaven or Satan at random, +0"},
    {"recipe": "2nd Level Wings (Chaos Machine)",
     "makes": [key_of(12, 13), key_of(12, 14), key_of(12, 16), key_of(12, 22)],
     "needs": ["a 1st level wing", "Loch's Feather", "Jewel of Chaos", "any excellent +4 items (raise the rate)",
               "Zen 1,500,000"],
     "uses": [CHAOS, FEATHER, key_of(12, 0), key_of(12, 1), key_of(12, 2)],
     "note": "the mixer's class's 2nd wing: DK Dragon, DW Soul, Elf Spirits, MG Darkness"},
    {"recipe": "Dinorant (Chaos Machine)", "makes": [key_of(13, 3)],
     "needs": ["10 Horns of Uniria at full life", "Jewel of Chaos", "Zen 500,000", "70%"],
     "uses": [CHAOS, key_of(13, 2)]},
    {"recipe": "Invisibility Cloak (Chaos Machine)", "makes": [key_of(13, 18)],
     "needs": ["Scroll of Archangel", "Blood Bone (same level, +1..+6)", "Jewel of Chaos",
               "Zen 50,000..600,000 by level", "80%", "level 15+"],
     "uses": [CHAOS, key_of(13, 16), key_of(13, 17)]},
    {"recipe": "Devil's Invitation (Chaos Machine)", "makes": [key_of(14, 19)],
     "needs": ["Devil's Eye", "Devil's Key (same level, +0..+4)", "Jewel of Chaos",
               "Zen 40,000..300,000 by level", "60-80%", "level 10+"],
     "uses": [CHAOS, key_of(14, 17), key_of(14, 18)]},
    {"recipe": "Upgrade +10 / +11 (Chaos Machine)", "makes": [],
     "needs": ["an item at +9 (+10)", "Jewel of Chaos", "1 (2) Jewel of Bless", "1 (2) Jewel of Soul"],
     "uses": [CHAOS, BLESS, SOUL]},
    {"recipe": "Add Socket (Chaos Machine)", "makes": [],
     "needs": ["an item with room for a socket", "Jewel of Life", "Jewel of Chaos", "2 Jewels of Soul",
               "2 Jewels of Bless", "Zen 300,000"],
     "uses": [LIFE, CHAOS, SOUL, BLESS]},
    {"recipe": "Remove Rune (Chaos Machine)", "makes": [RUNE],
     "needs": ["an item with a rune set", "Jewel of Chaos"], "uses": [CHAOS],
     "note": "the socket's rune comes back as a Rune of Creation"},
    {"recipe": "Fuse Runes (Chaos Machine)", "makes": [RUNE],
     "needs": ["3 runes of one rarity (below Legendary)", "Jewel of Chaos", "Jewel of Life", "Zen 100,000"],
     "uses": [RUNE, CHAOS, LIFE], "note": "a random rune of the next rarity"},
    {"recipe": "Jeweler Combine (Moss the Gambler, Noria)",
     "makes": [k for k, r in ITEMS.items() if powered(r)],
     "needs": ["3 powered rings/pendants of one colour (white or blue)", "Jewel of Chaos", "Zen 500,000 / 2,000,000"],
     "uses": [CHAOS] + [k for k, r in ITEMS.items() if powered(r)],
     "note": "a random powered piece of the next colour"},
    {"recipe": "Jeweler Recut (Moss the Gambler, Noria)", "makes": [],
     "needs": ["a powered ring/pendant", "Jewel of Chaos", "Zen 50,000..500,000 by plus"],
     "uses": [CHAOS] + [k for k, r in ITEMS.items() if powered(r)]},
    {"recipe": "Jeweler Salvage (Moss the Gambler, Noria)",
     "makes": [key_of(14, 1), key_of(12, 15), key_of(14, 13), key_of(14, 14), key_of(14, 16)],
     "needs": ["a powered ring/pendant (broken for one prize; rarer piece, better odds)"],
     "uses": [k for k, r in ITEMS.items() if powered(r)],
     "note": "3 Small Healing Potions, or a Jewel of Chaos, Bless, Soul or Life"},
]
for rec in RECIPES:
    for k in rec["makes"]:
        e = {"recipe": rec["recipe"], "needs": rec["needs"]}
        if rec.get("note"):
            e["note"] = rec["note"]
        add(k, "crafts", e)
    for k in rec["uses"]:
        add(k, "used_in", {"recipe": rec["recipe"]})

# ---- quests (src/sim/quests.cpp) -------------------------------------------------------------------
qsrc = src("src/sim/quests.cpp")
qtext = strip_comments(qsrc)
KIN = {"knight": "Dark Knight", "wizard": "Dark Wizard", "elf": "Fairy Elf", "gladiator": "Magic Gladiator"}
FUNCS = {}
for m in re.finditer(r"^QuestRow (\w+)\(([^)]*)\) \{\n(.*?)^\}", qtext, re.S | re.M):
    FUNCS[m.group(1)] = (m.group(2), m.group(3))


def split_args(s):
    out, depth, cur, instr = [], 0, "", False
    for ch in s:
        if ch == '"':
            instr = not instr
        if not instr and ch in "([{":
            depth += 1
        if not instr and ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0 and not instr:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def literal(tok, env):
    tok = tok.strip()
    if tok.startswith('"'):
        return tok.strip('"')
    if tok in env:
        return env[tok]
    try:
        return int(tok)
    except ValueError:
        return tok


def build(fn, args=None):
    params, body = FUNCS[fn]
    env = {}
    if args is not None:
        names = []
        for p in split_args(params):
            p = p.split("=")[0].strip()
            names.append(p.split()[-1])
        defaults = {}
        for p in split_args(params):
            if "=" in p:
                defaults[p.split("=")[0].split()[-1]] = literal(p.split("=")[1], {})
        env.update(defaults)
        for n, a in zip(names, args):
            env[n] = literal(a, {})
    base = re.search(r"QuestRow row = (\w+)\((.*?)\);", body, re.S)
    row = {"paid": [], "repeat": 0, "place": "", "giverName": "", "title": "", "receiverName": "",
           "receiverPlace": "", "promotes": False, "giver": 0}
    if base:
        row = build(base.group(1), split_args(base.group(2)))
        row = json.loads(json.dumps(row))
    for field in ("giverName", "place", "title", "receiverName", "receiverPlace"):
        m = re.search(r"row\.%s = ([^;]+);" % field, body)
        if m:
            row[field] = literal(m.group(1), env)
    m = re.search(r"row\.giver = ([^;]+);", body)
    if m:
        row["giver"] = literal(m.group(1), env)
    m = re.search(r"row\.repeatSeconds = ([^;]+);", body)
    if m:
        row["repeat"] = eval(m.group(1), {})
    if re.search(r"row\.promotes = true", body):
        row["promotes"] = True
    for pm in re.finditer(r"row\.paid\[[^\]]+\] = \{(.*?)\};", body, re.S):
        fields = dict(re.findall(r"\.(\w+) = ([^,}]+(?:\([^)]*\))?)", pm.group(1)))
        item = literal(fields.get("item", "nullptr"), env)
        if not isinstance(item, str) or item == "nullptr":
            continue
        row["paid"].append({
            "item": item,
            "count": literal(fields.get("count", "1"), env),
            "plus": literal(fields.get("plus", "0"), env),
            "kin": KIN.get(fields.get("kin", "").strip()),
            "first": fields.get("firstOnly", "").strip() == "true",
            "rank": fields.get("rank", "").split("::")[-1].strip() or None,
        })
    return row


table = re.search(r"const QuestRow kRawTable\[kQuests\] = \{(.*?)\};", qtext, re.S).group(1)
QUEST_ROWS = []
for call, key in re.findall(r"keyed\((\w+\([^)]*\)),\s*\"(\w+)\"\)", table):
    fn = call.split("(")[0]
    args = split_args(call[len(fn) + 1:-1])
    row = build(fn, args if args else None)
    row["key"] = key
    QUEST_ROWS.append(row)

for i, row in enumerate(QUEST_ROWS):
    if row["giver"] == -1:
        continue  # the demo quest: given by nobody
    change = row["key"] == "sevina_trial" or row["promotes"]
    row["paid"].append({"item": "MagicBox03", "count": 10 if change else 3, "plus": 0, "kin": None,
                        "first": False, "rank": None})
    npc = row["giverName"]
    if row["receiverName"] and row["receiverName"] != row["giverName"]:
        npc += " (hand in to %s)" % row["receiverName"]
    for p in row["paid"]:
        when = "first clear" if p["first"] else ("every clear" if row["repeat"] else "once")
        e = {"quest": row["title"], "npc": npc, "map": re.sub(r"^the ", "", str(row["place"])).strip() or "?",
             "when": when}
        if row["repeat"] and not p["first"]:
            e["repeat"] = "%dh" % (row["repeat"] // 3600)
        if isinstance(p["count"], int) and p["count"] > 1:
            e["count"] = p["count"]
        if isinstance(p["plus"], int) and p["plus"] > 0:
            e["plus"] = p["plus"]
        if p["kin"]:
            e["class"] = p["kin"]
        if p["rank"] == "Second":
            e["rank"] = "second class"
        elif p["rank"] == "First":
            e["rank"] = "first class"
        if row["promotes"]:
            e["note"] = "class-change hand-in"
        add(p["item"], "quests", e)

# Quest treasures handed in (Find steps): the item is used for the quest.
for m in re.finditer(r"sevinaTreasure\(Kin::\w+, \"([^\"]+)\", \"(\w+)\"", qtext):
    add(m.group(2), "used_in", {"recipe": "Quest: %s (Sevina, Devias) - hand-in item" % m.group(1)})

# ---- events ----------------------------------------------------------------------------------------
rew = int_rows(event_h, r"kCastleRewardJewels\[6\]\[4\]\[3\]")
for c in range(6):
    for j in range(4):
        g, n, cnt = rew[(c * 4 + j) * 3: (c * 4 + j) * 3 + 3]
        if g < 0:
            continue
        add(key_of(g, n), "events", {"event": "Blood Castle %d" % (c + 1), "note": "win reward, x%d" % cnt,
                                     "count": cnt, "chance": 1.0})
aw = int_rows(event_h, r"kArchangelWeapons\[3\]\[2\]")
for i in range(3):
    add(key_of(aw[i * 2], aw[i * 2 + 1]), "events",
        {"event": "Blood Castle (any)", "note": "the Statue of Saint drops it when broken; carried to the Archangel to win"})
add(key_of(13, 18), "used_in", {"recipe": "Blood Castle entry ticket (Messenger of Archangel, Devias)"})
add(key_of(14, 19), "used_in", {"recipe": "Devil Square entry ticket (Charon, Noria)"})
# Golden Dragon raid (realm_raid.cpp)
raid = "Golden Dragon raid (invasion of Lorencia/Devias/Noria)"
add(key_of(14, 51), "events", {"event": raid, "note": "every Golden Budge Dragon minion leaves one", "chance_minion": 1.0})
_rj = [k for k, r in ITEMS.items() if refining_jewel(r)]
for k in _rj:
    add(k, "events", {"event": raid, "note": "minion 1 in 4 (any of 4 refining jewels); dragon's hoard 3 random refining jewels",
                      "chance_minion": sig(MINION_JEWEL / len(_rj)), "chance_hoard": sig(1 - (1 - 1 / len(_rj)) ** HOARD_JEWELS)})
add(key_of(14, 22), "events", {"event": raid, "note": "minion 1 in 10; dragon's hoard 2 runes",
                               "chance_minion": MINION_RUNE, "chance_hoard": 1.0, "count_hoard": 2})
add(key_of(14, 52), "events", {"event": raid, "note": "dragon's hoard: 5 Boxes of Kundun +1..+3", "chance_hoard": 1.0, "count_hoard": 5})
add(key_of(13, 14), "events", {"event": raid, "note": "dragon's hoard, 1 in 4", "chance_hoard": HOARD_FEATHER})

# ---- boxes (items.h bags; realm_items.cpp openBox / crack) -------------------------------------------
items_h = strip_comments(src("src/sim/items.h"))


def bag(name):
    m = re.search(r"inline constexpr BagRow %s\[\] = \{(.*?)\};" % name, items_h, re.S)
    return [tuple(map(int, p)) for p in re.findall(r"\{(\d+),\s*(\d+)\}", m.group(1))]


def present(rows, exc_only=False):
    out = []
    for gn in rows:
        k = key_of(*gn)
        if k is None:
            continue
        if exc_only and not (excellentable(ITEMS[k]) and takes_options(ITEMS[k])):
            continue  # Realm::excellentOf takes only rows that carry the options
        out.append(k)
    return out


def box_add(k, label, plain, exc):
    lst = OUTPUT[k].setdefault("boxes", [])
    hit = next((b for b in lst if b["box"] == label), None)
    if hit is None:
        hit = {"box": label, "excellent": False, "_p": 0.0, "_x": 0.0}
        lst.append(hit)
    hit["_p"] += plain
    hit["_x"] += exc
    hit["excellent"] = hit["excellent"] or exc > 0


# A draw is uniform over the bag's rows the tables hold (duplicates count twice).
K_FIRECRACKER_ITEM, K_LUCK_ITEM = 4 / 10, 50 / 100       # kFirecrackerItemIn10, kLuckItemIn100
K_KUNDUN_ITEM, K_KUNDUN_EXC = [0.50, 0.55, 0.60], [0.40, 0.50, 0.60]
fc = present(bag("kFirecrackerBag"))
for k in fc:
    box_add(k, "Firecracker", K_FIRECRACKER_ITEM / len(fc), 0.0)
lb = present(bag("kLuckBag"))
for k in lb:
    box_add(k, "Box of Luck", K_LUCK_ITEM / len(lb), 0.0)
for t_ in range(3):
    plain = present(bag("kKundunPlain%d" % (t_ + 1)))
    fine = present(bag("kKundunExcellent%d" % (t_ + 1)), exc_only=True)
    for k in plain:
        box_add(k, "Box of Kundun +%d" % (t_ + 1), K_KUNDUN_ITEM[t_] * (1 - K_KUNDUN_EXC[t_]) / len(plain), 0.0)
    for k in fine:
        box_add(k, "Box of Kundun +%d" % (t_ + 1), 0.0, K_KUNDUN_ITEM[t_] * K_KUNDUN_EXC[t_] / len(fine))
for v in OUTPUT.values():
    for b_ in v.get("boxes", []):
        p_, x_ = b_.pop("_p"), b_.pop("_x")
        b_["chance"] = sig(p_ + x_)
        if p_ > 0 and x_ > 0:
            b_["chance_plain"], b_["chance_excellent"] = sig(p_), sig(x_)
_fine3 = present(bag("kKundunExcellent3"), exc_only=True)
for k in _fine3:
    add(k, "events", {"event": raid, "note": "dragon's hoard: 1 excellent item (2 one time in 3) from the Box of Kundun +3 pool",
                      "chance_hoard": sig((2 / 3) * (1 / len(_fine3)) + (1 / 3) * (1 - (1 - 1 / len(_fine3)) ** 2))})

# ---- write ------------------------------------------------------------------------------------------
result = {}
for k in sorted(OUTPUT):
    v = {f: x for f, x in OUTPUT[k].items() if x}
    v = {"name": ITEMS[k]["name"], **v}
    result[k] = v
result["_maps"] = {w: MAPNAME[w] for w in LIVE}
result["_maps"].update({"bloodcastle_%d" % c: "Blood Castle %d" % c for c in range(1, 7)})
result["_maps"].update({"devilsquare_%d" % s: "Devil Square %d" % s for s in range(1, 5)})
result["_notes"] = [
    "Built from MU2_BGFX HEAD (748c6fef), 72 commits past release 2b237b5c; the only source-relevant diffs since are market.cpp potion stacks of 20 (were 10) and machine.cpp letting excellent +4 items boost the Chaos Weapon/wings mixes.",
    "Kanturu (kanturu, kanturu_relics, kanturu_tower) is dormant and left out; so Ancient runes and Splinter Wolf drops do not appear.",
    "monsters: the normal drop pool (dropLevel in [L-15, L], potions [L-8, L], plus (L-drop)/3 must not pass the breed's rules.drops.max_plus, default 6), the jewel rolls (Bless/Soul/Chaos by dropLevel <= L; pets, Ale, Town Portal by dropLevel <= L), the rune (any L >= 1), and excellent (rows a level-(L-25) monster would draw). 'excellent': true means it can fall excellent from that monster; 'plain': false means only as excellent. 'how' marks entries that come only from a separate small roll (feather, nova, firecracker, wings, eye/key, scroll/bone, Life, summoning orb).",
    "Powered rings/pendants' 'deep jewellery' rule (15% of item drops past level 33 also draw powered pieces below the gap) is included, still filtered by max_plus as in the code.",
    "Elite entries ('elite': true, name 'Elite X'): the map's 3 strongest non-boss breeds at level +2; elites drop gear only ([L-15, L], one or two pieces excellent) plus Bless/Soul/Chaos; any of the three breeds may be the elite.",
    "Blood Castle monsters are listed per castle (kCastleBreeds); castle 6 allows second-class gear and Loch's Feather. Devil Square monsters per square (kSquareBreeds + boss). The Golden Dragon and Golden Budge Dragons appear as '<town> (Golden Invasion)' and also leave the normal drop.",
    "Odds are not modelled except where noted; Item Find, talents and party/raider credit are ignored. Statues of Saint (level 0) are not drop sources.",
    "shops: market.cpp shelves by NPC number, placed by each world.json npcs list (Potion Girl Amy stands in several maps). Prices are sim::buyingPrice for the shelf's plus/pieces; the 'shelf_note' fields in item json are stale (old MU2 Market.cs) and ignored. The broker (player market) is not a source here.",
    "crafts / used_in: the Chaos Machine services (Chaos Goblin, Noria) and the Jeweler (Moss the Gambler, Noria). Needs are short summaries of machine.cpp's judge(); rates/Zen as in machine.h.",
    "quests: parsed from quests.cpp's kRawTable (the demo quest is skipped); 'first clear' = paid once, to a native of the giver's town; 'every clear' = each completion (repeat 12h); 'once' = a one-time quest. Every quest also pays 3 Firecrackers (10 for Sevina's class change). 'class' limits who is paid it; 'rank' when only first/second class heroes get it.",
    "boxes (extra key): what Firecrackers, the Box of Luck and the Box of Kundun +1..+3 can open into; 'excellent': true if the box's excellent pool has it.",
]
result["_rates"] = [
    "Every monster entry's 'chance' is the odds that ONE kill of it leaves this item: solo killer, no Item Find (rings), no Fortune talents, no party/raider/summon credit. Rounded to 4 significant digits.",
    "Ordinary kill (Realm::leave), one main roll: Rune of Creation 0.05%, refining jewel (Bless/Soul/Chaos whose drop level the monster reaches) 0.5%, jewel group (Guardian Angel, Imp, Horn of Uniria, Ale, Town Portal Scroll) 0.1%, excellent item 0.1% (none under level 25), plain item 10%; the rest is Zen (money_rate) or nothing. Each branch draws uniformly from the rows it admits for that monster's level and map, so chance = share / pool size.",
    "Above level 33, 15% of the plain item draws use the 'deep' pool that adds every powered ring/pendant the level reaches (still limited by the breed's max_plus).",
    "Before the main roll: outside Blood Castle a Scroll of Archangel 1% then a Blood Bone 1%; outside Devil Square (monster above 15) a Devil's Eye 1% then a Devil's Key 1%. Any of these is the kill's whole drop, so every other chance is multiplied by 0.9801 (and again by 0.9801 where both apply: 0.9606).",
    "Separate rolls beside the main one: Orb of Summoning 1/150 (level 3+), Jewel of Life 1/2500 (level 50+), Firecracker 1/300 (level 17+, Dungeon/Lost Tower/BC/DS), Loch's Feather 1/500 (level 60+, Tarkan/Icarus/Aida/BC6), Scroll of Nova 1/1500 (level 60+, Lost Tower/Atlans/Tarkan/Icarus), Icarus 1st wing 1/1000 (only the killer's class's wing, so the listed chance applies to that class only), Atlans second-class gear 1/400 (level 43+) spread over every second-class row.",
    "Elites (realm_elite.cpp): 4-7 gear pieces (uniform), 1 excellent (2 at 35%), from the [L-15, L] gear pool at level+2; 1 jewel always and a 2nd at 50%, of Chaos/Bless/Soul. 'chance' is at least one of this item per elite kill; chance_plain/chance_excellent per piece kind. Elites skip every other roll.",
    "Golden Invasion: the Golden Dragon and Golden Budge Dragons also take the ordinary roll; minions add a Box of Luck always, a refining jewel 1/4 (of 4, Life included) and a rune 1/10; the dragon's hoard adds 5 Boxes of Kundun, 2 runes, 3 random refining jewels, 1 excellent (a 2nd 1 time in 3) from the Kundun +3 excellent pool, and a feather 1/4.",
    "Boxes: 'chance' is the odds that one box opens into this item (Firecracker 40% item, Box of Luck 50%, Kundun +1/+2/+3 item 50/55/60% of which excellent 40/50/60%); Blood Castle win rewards are certain (chance 1, with count).",
    "Not modelled: Sevina's treasures (2% of kills on the trial ground while her quest is active), the Icarus wing's class choice, Item Find/Fortune bonuses, and which of a party gets the drop.",
]
with open(OUT, "w") as f:
    json.dump(result, f, separators=(",", ":"), ensure_ascii=False)
print("wrote", OUT, os.path.getsize(OUT), "bytes,", len(ITEMS), "items")
