#!/usr/bin/env python3
"""NPC and quest pages for the website: npcs.json and quests.json beside this script.

Run from the MU2_BGFX directory: python3 <this>. Reuses gen_sources.py's parsing (item rows,
shelves, prices, worlds) by running it with its output sent to /dev/null. Read-only on the repo.
"""
import json
import os
import re
import runpy
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_argv = sys.argv
sys.argv = [os.path.join(HERE, "gen_sources.py"), os.devnull]
G = runpy.run_path(os.path.join(HERE, "gen_sources.py"), run_name="gen_sources")
sys.argv = _argv
src, strip_comments, split_args = G["src"], G["strip_comments"], G["split_args"]
ITEMS, BY_GN, WORLDS, LIVE, MAPNAME, BREEDS = G["ITEMS"], G["BY_GN"], G["WORLDS"], G["LIVE"], G["MAPNAME"], G["BREEDS"]
SHELVES, STOCK, buying_price, key_of = G["SHELVES"], G["STOCK"], G["buying_price"], G["key_of"]
ROOT = os.getcwd()


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


STR = r'"(?:[^"\\]|\\.)*"'


def strings(expr):
    return "".join(re.sub(r"\\(.)", lambda e: {"n": "\n", "t": "\t"}.get(e.group(1), e.group(1)), m[1:-1])
                   for m in re.findall(STR, expr))


# ---- constants the quest table names -------------------------------------------------------------
qh = strip_comments(src("src/sim/quests.h"))
CONST = {n: int(v) for n, v in re.findall(r"constexpr int(?:32_t)? (k\w+) = (\d+);", qh)}
KIN_VAR = {"knight": "Dark Knight", "wizard": "Dark Wizard", "elf": "Fairy Elf", "gladiator": "Magic Gladiator"}
KIN_ENUM = {"Kin::DarkKnight": "knight", "Kin::DarkWizard": "wizard", "Kin::FairyElf": "elf",
            "Kin::MagicGladiator": "gladiator"}

qsrc = src("src/sim/quests.cpp")
qtext = strip_comments(qsrc)
FUNCS = {}
for m in re.finditer(r"^QuestRow (\w+)\(([^)]*)\) \{\n(.*?)^\}", qtext, re.S | re.M):
    FUNCS[m.group(1)] = (m.group(2), m.group(3))


def value(tok, env):
    tok = tok.strip()
    if tok.startswith('"'):
        return strings(tok)
    if tok in env:
        return env[tok]
    if tok in CONST:
        return CONST[tok]
    try:
        return int(eval(tok, {}, dict(CONST)))
    except Exception:
        return tok


def build(fn, args=None):
    params, body = FUNCS[fn]
    env = {}
    if args is not None:
        ps = split_args(params)
        for p in ps:
            if "=" in p:
                env[p.split("=")[0].split()[-1]] = value(p.split("=")[1], {})
        for p, a in zip(ps, args):
            env[p.split("=")[0].split()[-1]] = value(a, {}) if not a.startswith("Kin::") else a
    base = re.search(r"QuestRow row = (\w+)\((.*?)\);", body, re.S)
    if base:
        row = json.loads(json.dumps(build(base.group(1), split_args(base.group(2)))))
    else:
        row = {"giver": 0, "giverName": "", "place": "", "receiver": 0, "receiverName": "", "receiverPlace": "",
               "title": "", "offer": {}, "handIn": {}, "underway": "", "resting": "", "next": "", "voice": "",
               "steps": {}, "repeat": 0, "afterAny": [], "minLevel": 0, "gateLevel": False, "promotes": False,
               "second": False, "natives": [], "strangers": False, "experience": 0, "firstExperience": 0,
               "zen": 0, "paid": [], "boon": ""}
    for field in ("giverName", "place", "receiverName", "receiverPlace", "title", "underway", "resting", "next",
                  "voice", "boon"):
        m = re.search(r"row\.%s = ((?:\s*%s)+|\w+);" % (field, STR), body)
        if m:
            row[field] = value(m.group(1), env) if not m.group(1).strip().startswith('"') else strings(m.group(1))
    for field in ("offer", "handIn"):
        for i, expr in re.findall(r"row\.%s\[(\d)\] =((?:\s*%s)+);" % (field, STR), body):
            row[field][i] = strings(expr)
    for field, name in (("giver", "giver"), ("receiver", "receiver"), ("minLevel", "minLevel"),
                        ("experience", "experience"), ("firstExperience", "firstExperience"), ("zen", "zen"),
                        ("repeat", "repeatSeconds")):
        m = re.search(r"row\.%s = ([^;]+);" % name, body)
        if m:
            row[field] = value(m.group(1), env)
    for flag in ("gateLevel", "promotes", "second", "strangers"):
        m = re.search(r"row\.%s = (true|false);" % flag, body)
        if m:
            row[flag] = m.group(1) == "true"
    m = re.search(r"row\.natives = ([^;]+);", body)
    if m:
        expr = m.group(1)
        kins = re.findall(r"\b(knight|wizard|elf|gladiator)\b", expr)
        if "int(kin)" in expr:
            kins.append(KIN_ENUM[env["kin"]])
        if "0x7" in expr:
            kins = ["knight", "wizard", "elf"]
        row["natives"] = [KIN_VAR[k] for k in kins]
    m = re.search(r"row\.afterAny = ([^;]+);", body)
    if m:
        row["afterAny"] = [value(t, env) for t in re.findall(r"<<\s*(\w+)", m.group(1))]
    # Steps: indexed or s++, in the order written.
    steps = {}
    s_counter = 0
    for idx, kind, target, count, line, item in re.findall(
            r"row\.steps\[([^\]]+)\] = \{QuestStepKind::(\w+),\s*([^,]+),\s*([^,]+),\s*(%s|\w+)(?:,\s*(%s|\w+))?\}"
            % (STR, STR), body):
        if idx.strip() == "s++":
            at = s_counter
            s_counter += 1
        else:
            at = int(idx)
        steps[str(at)] = {"kind": kind, "target": value(target, env), "count": value(count, env),
                          "line": value(line, env), "item": value(item, env) if item else None}
    if steps:
        row["steps"] = steps
    for pm in re.finditer(r"row\.paid\[[^\]]+\] = \{(.*?)\};", body, re.S):
        fields = dict(re.findall(r"\.(\w+) = ([^,}]+(?:\([^)]*\))?)", pm.group(1)))
        item = value(fields.get("item", "nullptr"), env)
        if not isinstance(item, str) or item == "nullptr":
            continue
        power = fields.get("power", "")
        pw = re.search(r"Power::(\w+)", power)
        row["paid"].append({"item": item, "count": value(fields.get("count", "1"), env),
                            "plus": value(fields.get("plus", "0"), env),
                            "kin": KIN_VAR.get(fields.get("kin", "").strip()),
                            "first": fields.get("firstOnly", "").strip() == "true",
                            "rank": fields.get("rank", "").split("::")[-1].strip() or None,
                            "sockets": value(fields.get("sockets", "0"), env),
                            "power": pw.group(1) if pw else None})
    return row


table = re.search(r"const QuestRow kRawTable\[kQuests\] = \{(.*?)\};", qtext, re.S).group(1)
QUESTS = []
for m in re.finditer(r"keyed\((\w+)\((.*?)\),\s*\"(\w+)\"\)", table):
    fn, args, key = m.group(1), m.group(2), m.group(3)
    row = build(fn, split_args(args) if args.strip() else None)
    row["key"] = key
    QUESTS.append(row)

# ---- who stands where ------------------------------------------------------------------------------
NPC_DOCS = {}
for path in sorted(os.listdir(os.path.join(ROOT, "content/npc"))):
    if path.endswith(".json") and not path.endswith(".rig.json"):
        doc = json.load(open(os.path.join(ROOT, "content/npc", path)))
        NPC_DOCS[doc.get("mu_name", "").lower()] = doc
        NPC_DOCS[doc.get("item", "").lower()] = doc

PLACED = {}  # name -> entry
for w in LIVE:
    for one in WORLDS[w].get("npcs") or []:
        e = PLACED.setdefault(one["name"], {"name": one["name"], "npc": int(one["npc"]),
                                            "figure": one.get("figure") or "", "where": []})
        if not e["figure"] and one.get("figure"):
            e["figure"] = one["figure"]
        e["where"].append({"map": MAPNAME[w], "map_key": w, "x": one["at"][0], "y": one["at"][1]})

# Figures the world's own placements stand: the npc json whose mu_name is his.
for e in PLACED.values():
    if not e["figure"]:
        doc = NPC_DOCS.get(e["name"].lower())
        if doc:
            e["figure"] = doc["item"]


def about_of(e):
    doc = NPC_DOCS.get(e["figure"].lower()) if e["figure"] else None
    doc = doc or NPC_DOCS.get(e["name"].lower())
    if not doc:
        return None
    for field in ("note", "why_this_one"):
        text = doc.get(field) or ""
        first = re.split(r"(?<=[.!?])\s+", text.strip())[0] if text else ""
        first = re.sub(r"\s*\((?:[^()]*user[^()]*)\)", "", first)
        # asset notes (triangles, sheets, rigs) are not about the person
        if first and not re.search(r"triangle|sheet|mesh|rig\b|bone|\.bmd|OZJ|pieces on|figure|user|OURS|docs/|0\.75|"
                                   r"MuMain|Version0|placement|of him|of them|\d+,\d+", first, re.I):
            return first
    return None


# ---- what talking to them opens (src/sim/realm_folk.cpp Realm::talk) --------------------------------
mh = strip_comments(src("src/sim/machine.h"))
mc = strip_comments(src("src/sim/machine.cpp"))
row_services = re.findall(r"Service::(\w+)", re.search(r"kRowServices\[\] = \{(.*?)\};", mh, re.S).group(1))
service_names = dict(re.findall(r"case Service::(\w+): return \"([^\"]+)\";",
                                re.search(r"const char\* serviceName\(Service service\) \{(.*?)\n\}", mc, re.S).group(1)))
REPAIRS = {int(n) for n in re.findall(r"npc == (\d+)", re.search(r"bool repairsAt\(int npc\) \{([^}]*)\}",
                                                                   strip_comments(src("src/sim/wear.cpp"))).group(1))}
K = {"vault": 240, "broker": 900, "goblin": 238, "jeweler": 492, "lugard": 540, "archangel": 232,
     "messenger": 233, "charon": 237, "guildmaster": 241, "thompson": 231, "guard": 247, "berdysh": 249}

# Shop roles, from the comment over each shelf in market.cpp.
SHOP_ROLE = {"kPotionGirl": "Potion seller", "kHarold": "Wandering merchant", "kMartin": "Wandering merchant",
             "kBlacksmith": "Blacksmith (weapons, armour, orbs)", "kMage": "Mage (scrolls, robes, staves)",
             "kBarmaid": "Barmaid (Ale)", "kLumen": "Barmaid (Ale, potions, arrows)",
             "kElfLala": "Elf merchant (orbs, elf armour, potions)", "kCraftsman": "Bowyer (bows, arrows)",
             "kIzabel": "Wizard merchant (scrolls, armour, potions)", "kZienna": "Weapons merchant"}
market_raw = src("src/sim/market.cpp")


def shelf_comment(table_name):
    m = re.search(r"((?://[^\n]*\n)+)constexpr Offer %s\[\]" % table_name, market_raw)
    if not m:
        return None
    text = " ".join(l.strip()[2:].strip() for l in m.group(1).splitlines())
    first = re.split(r"(?<=[.])\s+(?=[A-Z*])", text)[0]
    first = re.sub(r"\s*\((?:[^()]*user[^()]*)\)", "", first).replace("**", "")
    first = re.sub(r"\s*\(shop\d+\)", "", first)
    first = re.sub(r"^And (\w+)'s own, which is ours: ", r"\1's own: ", first)
    return first.strip()


QUEST_ID = {q["key"]: slug(q["key"]) for q in QUESTS}
LIVE_QUESTS = [q for q in QUESTS if q["giver"] != -1]

npcs = []
for name, e in sorted(PLACED.items(), key=lambda kv: (kv[1]["npc"], kv[0])):
    n = e["npc"]
    out = {"id": slug(name), "name": name, "npc": n, "figure": e["figure"] or None}
    services, role = [], None
    table = STOCK.get(n)
    if table:
        role = SHOP_ROLE.get(table, "Merchant")
        services.append("Shop")
        sells = []
        for o in SHELVES[table]:
            k = key_of(o["group"], o["number"])
            if k is None:
                continue
            one = {"k": k, "plus": o["plus"], "price": buying_price(ITEMS[k], o["plus"], o["pieces"] or 1, o["skill"])}
            if o["pieces"] > 1:
                one["pieces"] = o["pieces"]
            if o["skill"]:
                one["skill"] = True
            sells.append(one)
        out["sells"] = sells
        services.append("Buys items (a third of the plain price)")
        if n in REPAIRS:
            services.append("Repair")
        about = shelf_comment(table)
    else:
        about = None
    gives = [q for q in LIVE_QUESTS if q["giver"] == n]
    takes = [q for q in LIVE_QUESTS if q["receiver"] == n and q["receiver"] != q["giver"]]
    if n == K["vault"]:
        role, services = "Vault keeper", ["Vault"]
    elif n == K["broker"]:
        role, services = "Broker", ["Market (the players' auction house, one book for Lorencia, Devias and Noria)"]
    elif n == K["goblin"]:
        role = "Chaos Machine"
        services = ["Chaos Machine: " + ", ".join(service_names[s] for s in row_services)]
    elif n == K["jeweler"]:
        role, services = "Jeweler", ["Jeweler: Recut, Salvage, Combine (powered rings and pendants)"]
    elif n == K["lugard"]:
        role, services = "Reset keeper", ["Rebirth (level 400; 10,000,000 Zen times the rebirth's number)"]
    elif not table and (gives or takes):
        role = "Quest giver" if gives else "Quest hand-in"
    elif n == K["archangel"]:
        role, services = "Blood Castle", ["Takes the Archangel weapon from the broken statue and pays the castle's win"]
    elif n == K["messenger"]:
        role, services = "Event gatekeeper", ["Blood Castle entry (spends an Invisibility Cloak; the cloak's plus is the castle)"]
    elif n == K["charon"]:
        role, services = "Event gatekeeper", ["Devil Square entry (spends a Devil's Invitation)"]
    elif n in (K["guard"], K["berdysh"]):
        role = "Guard"
    elif n in (K["guildmaster"], K["thompson"]):
        role = "Townsperson"
    if gives:
        services.append("Quests")
        services.append("Unlocks this town on the travel list")
    if takes:
        services.append("Quest hand-ins")
    out["role"] = role or "Townsperson"
    out["about"] = about or about_of(e)
    if not out["about"]:
        out.pop("about")
    out["where"] = e["where"]
    if services:
        out["services"] = services
    if gives or takes:
        out["quests"] = [QUEST_ID[q["key"]] for q in gives + takes]
    if out["figure"] is None:
        out.pop("figure")
    npcs.append(out)

NPC_BY_NUMBER = {}
for o in npcs:
    NPC_BY_NUMBER.setdefault(o["npc"], o["id"])

# ---- quests ------------------------------------------------------------------------------------------
HOME = {}  # breed -> map display name, from the live nests
for w in LIVE:
    p = os.path.join(ROOT, "content/world", w, "nests.json")
    if os.path.exists(p) and w not in ("bloodcastle", "devilsquare"):
        for nest in json.load(open(p))["nests"]:
            HOME.setdefault(nest["breed"], []).append(MAPNAME[w]) if MAPNAME[w] not in HOME.get(nest["breed"], []) else None
POP = {}
for w in LIVE:
    p = os.path.join(ROOT, "content/world", w, "nests.json")
    if os.path.exists(p):
        for nest in json.load(open(p))["nests"]:
            POP[(w, nest["breed"])] = POP.get((w, nest["breed"]), 0) + nest["count"]
INDEX = {i: q["key"] for i, q in enumerate(QUESTS)}
PLACE_MAP = {"Lorencia": "lorencia", "Noria": "noria", "Devias": "devias", "the Lost Tower": "losttower",
             "Atlans": "atlans", "Tarkan": "tarkan", "Aida": "aida"}

quests = []
for i, q in enumerate(QUESTS):
    if q["giver"] == -1:
        continue  # the demo, given by nobody
    change = q["key"] == "sevina_trial" or q["promotes"]
    o = {"id": QUEST_ID[q["key"]], "name": q["title"], "giver": NPC_BY_NUMBER.get(q["giver"]),
         "map": re.sub(r"^the ", "", q["place"])}
    if q["receiver"] and q["receiver"] != q["giver"]:
        o["hand_in"] = NPC_BY_NUMBER.get(q["receiver"])
        o["hand_in_map"] = re.sub(r"^the ", "", q["receiverPlace"])
    kills, items, objectives, levels = [], [], [], []
    for at in sorted(q["steps"], key=int):
        st = q["steps"][at]
        if st["kind"] == "Clear":
            b = BREEDS.get(st["target"], {"name": "breed %s" % st["target"], "level": None})
            where = HOME.get(st["target"], ["?"])
            home = MAPNAME.get(PLACE_MAP.get(q["place"], ""))
            if home in where:
                where = [home]
            count = st["count"]
            if count == 0:
                count = POP.get((PLACE_MAP.get(q["place"], ""), st["target"]), 0)
            kills.append({"monster": b["name"], "count": count, "map": where[0] if len(where) == 1 else " / ".join(where),
                          "level": b["level"]})
            objectives.append("Kill %d %s in %s" % (count, st["line"], kills[-1]["map"]))
            if b["level"]:
                levels.append(b["level"])
        elif st["kind"] == "Find":
            items.append({"k": st["item"], "count": max(1, st["count"])})
            objectives.append("Find the %s (it falls only while this quest is active) and bring it back" %
                              (ITEMS[st["item"]]["name"] if st["item"] in ITEMS else st["item"]))
        else:
            objectives.append(st["line"])
    lvl = {}
    if q["minLevel"]:
        lvl["min"] = q["minLevel"]
        if q["gateLevel"]:
            lvl["note"] = "a gate's level: the Magic Gladiator may take it at two thirds"
    if levels:
        lvl["monsters"] = [min(levels), max(levels)]
    if lvl:
        o["level"] = lvl
    if q["strangers"]:
        o["classes"] = "any"
        o["natives"] = q["natives"]
        o["natives_note"] = "only these classes (born in the giver's town) get the first clear's items and experience"
    else:
        o["classes"] = q["natives"]
    if q["second"]:
        o["rank"] = "second class (or Magic Gladiator) only"
    if q["promotes"]:
        o["promotes"] = q["boon"] or True
    if q["afterAny"]:
        o["prereq"] = [QUEST_ID[INDEX[b]] for b in q["afterAny"]]
        if len(o["prereq"]) > 1:
            o["prereq_any"] = True
    o["repeat"] = ("every %dh" % (q["repeat"] // 3600)) if q["repeat"] else "once"
    o["objectives"] = objectives
    if kills:
        o["kills"] = kills
    if items:
        o["items"] = items
    first, every = [], []
    for p in q["paid"]:
        k = p["item"]
        e = {"k": k if k in ITEMS else None, "name": ITEMS[k]["name"] if k in ITEMS else k}
        if isinstance(p["count"], int) and p["count"] > 1:
            e["count"] = p["count"]
        if isinstance(p["plus"], int) and p["plus"] > 0:
            e["plus"] = p["plus"]
        if p["kin"]:
            e["class"] = p["kin"]
        if p["rank"] in ("First", "Second"):
            e["rank"] = p["rank"].lower() + " class"
        notes = []
        if p["sockets"]:
            notes.append("%d socket%s" % (p["sockets"], "s" if p["sockets"] > 1 else ""))
        if p["power"]:
            notes.append("power: " + re.sub(r"(?<!^)(?=[A-Z])", " ", p["power"]))
        if ITEMS.get(k) and G["takes_options"](ITEMS[k]):
            notes.append("luck and +4 option")
        elif ITEMS.get(k) and G["jewellery"](ITEMS[k]):
            notes.append("luck and +1% life regeneration option")
        if notes:
            e["note"] = ", ".join(notes)
        (first if p["first"] else every).append(e)
    rewards = {}
    if q["firstExperience"]:
        rewards["first_exp"] = q["firstExperience"]
    if q["experience"]:
        rewards["exp"] = q["experience"]
    if q["zen"]:
        rewards["zen"] = q["zen"]
    if first:
        rewards["first"] = first
    if every:
        rewards["every"] = every
    rewards["firecrackers"] = 10 if change else 3
    o["rewards"] = rewards
    text = [q["offer"][k] for k in sorted(q["offer"])]
    if text:
        o["text"] = "\n\n".join(text)
    if q["next"]:
        o["next"] = q["next"]
    if q["voice"] and os.path.isdir(os.path.join(ROOT, "content/voice", q["voice"])):
        o["voice"] = True
    quests.append(o)

NOTE = ("Built from MU2_BGFX HEAD by parsing src/sim/quests.cpp, market.cpp, realm_folk.cpp (Realm::talk), "
        "machine.h/.cpp, wear.cpp (repairsAt) and content/world/*/world.json npcs; Kanturu and charscene left out, "
        "and the demo quest. NPCs are merged by name (Potion Girl Amy, Baz: one entry, many 'where'); the three "
        "Brokers and the guards keep their own names. Shop roles are named from market.cpp's shelf comments; "
        "'about' is the shelf comment's first sentence or the npc json's note when it is about the person, else "
        "left out. Quest kills' 'map' is where that breed nests on the live maps; a count of 0 in the code ('the "
        "whole population') is resolved from the giver's map's nests. 'level.monsters' is the min/max level of the "
        "breeds asked, as a recommendation; 'level.min' is the code's minLevel. 'prereq' is QuestRow::afterAny "
        "(any one of them handed in). 'classes': 'any' when strangers may take it, with 'natives' the classes "
        "paid the first clear. Rewards: 'first' paid once on a native's first clear (first_exp in place of exp), "
        "'every' on each completion; gear and rings come with luck and the +4 option (kQuestOption). "
        "'text' is the offer dialog in full.")
with open(os.path.join(HERE, "npcs.json"), "w") as f:
    json.dump({"_note": NOTE, "npcs": npcs}, f, separators=(",", ":"), ensure_ascii=False)
with open(os.path.join(HERE, "quests.json"), "w") as f:
    json.dump({"_note": NOTE, "quests": quests}, f, separators=(",", ":"), ensure_ascii=False)
print("npcs", len(npcs), os.path.getsize(os.path.join(HERE, "npcs.json")),
      "quests", len(quests), os.path.getsize(os.path.join(HERE, "quests.json")))
