"""Reading the facts `mu new` copies, out of the two sources every recipe cites.

  OpenMU   LEGACY/reference/openmu/src/Persistence/Initialization: a monster's row (Maps/*.cs,
           `<var>.Number = N;` to its SetGuid), its spawns (`CreateMonsterSpawn(...)` in the same
           version's map files) and an item's row (`this.Create<Kind>(...)` in Items/*.cs, read
           against the helper's own declared parameters -- every overload, by argument count).
           Version075 first, the game's version; then 0.95d, 0.97d and Season 6 for the worlds
           0.75 never had (Aida, Icarus ...), and the recipe says which it came from.
  MuMain   LEGACY/reference/MuMain/src/source: CreateMonster's case for the monster (its model,
           scale, name and weapons) and the enums that turn names into numbers.

Each answer carries `file:line`, because a recipe quotes where its numbers came from
(memory: absolute 0.75 replica -- every rule traced).
"""

import glob
import os
import re

from things import ROOT

LEGACY = os.path.normpath(os.path.join(ROOT, "..", "LEGACY", "reference"))
OPENMU = os.path.join(LEGACY, "openmu", "src", "Persistence", "Initialization")
MUMAIN = os.path.join(LEGACY, "MuMain", "src", "source")
VERSIONS = ["Version075", "Version095d", "Version097d", "VersionSeasonSix"]


def rel(path, line):
    return f"{os.path.relpath(path, LEGACY)}:{line}"


def read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def line_of(text, index):
    return text.count("\n", 0, index) + 1


# ----------------------------------------------------------------------------- OpenMU monsters

STATS = {"Level": "level", "MaximumHealth": "health", "MinimumPhysBaseDmg": "minimum_damage",
         "MaximumPhysBaseDmg": "maximum_damage", "DefenseBase": "defense",
         "AttackRatePvm": "attack_rate", "DefenseRatePvm": "defense_rate"}


def skill_numbers():
    text = read(os.path.join(OPENMU, "Skills", "SkillNumber.cs"))
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"^\s*(\w+)\s*=\s*(\d+)", text, re.M)}


def timespan(text, field):
    m = re.search(rf"\.{field} = new TimeSpan\((\d+) \* TimeSpan\.TicksPer(Millisecond|Second)\)", text)
    if not m:
        return None
    return int(m.group(1)) * (1000 if m.group(2) == "Second" else 1)


def monster(number):
    """OpenMU's row for monster `number`, from the first version that defines it, or None."""
    for version in VERSIONS:
        for path in sorted(glob.glob(os.path.join(OPENMU, version, "**", "*.cs"), recursive=True)):
            text = read(path)
            for m in re.finditer(rf"\b(\w+)\.Number = {number};", text):
                var = m.group(1)
                # From the monster's own CreateNew, not the nearest brace: Lorencia's are written
                # one after another, and a brace back reached the last one's attack skill.
                made = [c.start() for c in re.finditer(rf"\b{var} = this\.Context\.CreateNew", text[:m.start()])]
                start = made[-1] if made else text.rfind("{", 0, m.start())
                end = text.find("SetGuid", m.end())
                block = text[start:end]
                # A skill or a gate is numbered too: only a block that names a monster and
                # gives it a level is one.
                if re.search(rf"{var}\.Designation", block) and "Stats.Level" in block:
                    return monster_row(number, version, path, text, m, var, block)
    return None


def monster_row(number, version, path, text, m, var, block):
    name = re.search(rf"{var}\.Designation = \"([^\"]+)\"", block)
    row = {"number": number, "name": name.group(1) if name else None, "version": version,
           "from": rel(path, line_of(text, m.start()))}
    for field, key in (("MoveRange", "move_range"), ("AttackRange", "attack_range"),
                       ("ViewRange", "view_range")):
        v = re.search(rf"{var}\.{field} = (\d+);", block)
        row[key] = int(v.group(1)) if v else None
    row["move_delay"] = timespan(block, "MoveDelay")
    row["attack_delay"] = timespan(block, "AttackDelay")
    respawn = timespan(block, "RespawnDelay")
    row["respawn_seconds"] = respawn // 1000 if respawn is not None else None
    for stat, key in STATS.items():
        v = re.search(rf"\{{ Stats\.{stat}, (\d+) \}}", block)
        row[key] = int(v.group(1)) if v else None
    skill = re.search(rf"{var}\.AttackSkill = .*?SkillNumber\.(\w+)", block)
    row["attack_skill"] = skill_numbers().get(skill.group(1)) if skill else None
    return row
    return None


def spawns(number, version):
    """Every CreateMonsterSpawn of `number` in that version's maps: one row per area."""
    out = []
    for path in sorted(glob.glob(os.path.join(OPENMU, version, "Maps", "*.cs"))):
        text = read(path)
        map_number = re.search(r"const byte Number = (\d+);", text)
        if not map_number:
            continue
        pattern = rf"CreateMonsterSpawn\((\d+), this\.NpcDictionary\[{number}\], ([^)]*)\)"
        for m in re.finditer(pattern, text):
            args = [a.strip() for a in m.group(2).split(",")]
            nums = [int(a) for a in args if a.isdigit()]
            if len(nums) >= 4 and not args[2].startswith("Direction"):
                x1, x2, y1, y2 = nums[:4]
                count = nums[4] if len(nums) > 4 else 1
            else:
                x1 = x2 = nums[0]
                y1 = y2 = nums[1]
                count = 1
            out.append({"map": int(map_number.group(1)), "x1": x1, "x2": x2, "y1": y1, "y2": y2,
                        "count": count, "from": rel(path, line_of(text, m.start()))})
    return out


# ----------------------------------------------------------------------------- MuMain monsters

def enum_value(name):
    text = read(os.path.join(MUMAIN, "Core", "Globals", "_enum.h"))
    m = re.search(rf"^\s*{name}\s*=\s*(\d+)", text, re.M)
    return int(m.group(1)) if m else None


def monster_case(number):
    """CreateMonster's case for `number`: its enum, model index, scale, name and the block itself."""
    enums = read(os.path.join(MUMAIN, "Core", "Globals", "_enum.h"))
    names = [m.group(1) for m in re.finditer(rf"^\s*(MONSTER_\w+)\s*=\s*{number}\s*,", enums, re.M)
             if not m.group(1).startswith("MONSTER_MODEL_")]
    # CreateMonster for the early monsters; the later ones are created by their map's own file
    # (GM_Kanturu_1st.cpp's Berserker), in the same shape, cases falling through to one body.
    paths = [os.path.join(MUMAIN, "Engine", "Object", "ZzzCharacter.cpp")] + \
        sorted(glob.glob(os.path.join(MUMAIN, "World", "GameMaps", "*.cpp")))
    for name, path in ((n, p) for n in names for p in paths):
        text = read(path)
        m = re.search(rf"case {name}:\s*\n", text)
        if not m:
            continue
        end = text.find("break;", m.end())
        block = text[m.start():end + len("break;")]
        model = re.search(r"OpenMonsterModel\((MONSTER_MODEL_\w+)\)", block)
        if not model:
            continue
        scale = re.search(r"Object\.Scale = ([\d.]+)f", block)
        weapons = re.findall(r"Weapon\[(\d)\]\.Type = (MODEL_\w+)", block)
        return {"enum": name, "from": rel(path, line_of(text, m.start())), "block": block.strip(),
                "model": model.group(1) if model else None,
                "model_index": enum_value(model.group(1)) if model else None,
                "scale": float(scale.group(1)) if scale else 1.0,
                "weapons": weapons}
    return None


# ----------------------------------------------------------------------------- OpenMU items

KEYS = {"group": "group", "number": "number", "slot": "slot", "skillNumber": "skill", "skill": "skill",
        "width": "width", "height": "height", "dropsFromMonsters": "drops_from_monsters",
        "dropLevel": "drop_level", "minimumDamage": "minimum_damage", "maximumDamage": "maximum_damage",
        "attackSpeed": "attack_speed", "durability": "durability", "magicPower": "magic_power",
        "defense": "defense", "defenseRate": "defense_rate", "walkSpeed": "walk_speed",
        "isAmmunition": "is_ammunition"}
REQUIRES = {"levelRequirement": "level", "strengthRequirement": "strength",
            "agilityRequirement": "agility", "energyRequirement": "energy",
            "vitalityRequirement": "vitality", "leadershipRequirement": "leadership"}
CLASSES = {"wizardClass": "wizard", "darkWizardClassLevel": "wizard", "knightClass": "knight",
           "darkKnightClassLevel": "knight", "elfClass": "elf", "elfClassLevel": "elf",
           "magicGladiatorClassLevel": "gladiator", "magicGladiatorClass": "gladiator"}
# The groups the armour helpers do not take as an argument, and the footprint the 0.75
# overloads without one give a piece (ArmorInitializerBase.cs).
HELPER_GROUP = {"CreateShield": 6, "CreateGloves": 10, "CreateBoots": 11}
ARMOUR_SLOT_GROUP = {2: 7, 3: 8, 4: 9, 5: 10, 6: 11}


def split_args(text):
    out, depth, quote, cur = [], 0, False, ""
    for ch in text:
        if ch == '"':
            quote = not quote
        if not quote and ch in "([":
            depth += 1
        if not quote and ch in ")]":
            depth -= 1
        if ch == "," and depth == 0 and not quote:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def helpers(version):
    """Every Create<Kind> declaration visible to `version`'s items: name -> [(params, file:line)]."""
    found = {}
    paths = glob.glob(os.path.join(OPENMU, version, "Items", "*.cs")) + glob.glob(os.path.join(OPENMU, "Items", "*.cs"))
    for path in paths:
        text = read(path)
        for m in re.finditer(r"(?:protected|private|public)[\w\s<>?]*\s(Create\w+)\(([^)]*)\)", text):
            params = [p.strip().split("=")[0].split()[-1].lstrip("@") for p in split_args(m.group(2)) if p.strip()]
            found.setdefault(m.group(1), []).append((params, rel(path, line_of(text, m.start()))))
    return found


def item(name):
    """OpenMU's row for the item called `name` (case-insensitive), with the call and its signature."""
    for version in VERSIONS:
        signatures = helpers(version)
        for path in sorted(glob.glob(os.path.join(OPENMU, version, "Items", "*.cs"))):
            text = read(path)
            for m in re.finditer(r"this\.(Create\w+)\((.*?)\);", text):
                args = split_args(m.group(2))
                names = [a.strip('"') for a in args if a.startswith('"')]
                if not names or names[0].lower() != name.lower():
                    continue
                options = [s for s in signatures.get(m.group(1), [])
                           if len(s[0]) >= len(args) and len([p for p in s[0]]) - len(args) <= 1]
                if not options:
                    return {"error": f"{m.group(1)} has no declaration with {len(args)} arguments"}
                params, declared = min(options, key=lambda s: len(s[0]) - len(args))
                values = dict(zip(params, args))
                return {"helper": m.group(1), "version": version, "values": values, "params": params,
                        "call": f"{m.group(1)}({m.group(2)})", "from": rel(path, line_of(text, m.start())),
                        "declared": declared}
    return None


def item_stats(found):
    """The recipe's `stats` from an item() answer, in the keys index.py reads."""
    values, helper = found["values"], found["helper"]

    def value(text):
        if text in ("true", "false"):
            return text == "true"
        try:
            return int(text)
        except ValueError:
            return text.strip('"')

    stats = {"call": found["call"], "from": found["from"],
             "call_note": f"{helper}({', '.join(found['params'])}), {found['declared']}."}
    requires, classes, levels = {}, [], []
    for param, text in values.items():
        v = value(text)
        if param in KEYS:
            stats[KEYS[param]] = v
        elif param in REQUIRES:
            requires[REQUIRES[param]] = v
        elif param in CLASSES and isinstance(v, int) and v > 0:
            classes.append(CLASSES[param])
            levels.append(v)
        elif param == "name":
            stats["name"] = v
    stats.setdefault("drops_from_monsters", True)
    if "group" not in stats:
        stats["group"] = HELPER_GROUP.get(helper) or ARMOUR_SLOT_GROUP.get(stats.get("slot"))
    if helper in ("CreateGloves", "CreateBoots"):
        stats.setdefault("slot", 5 if helper == "CreateGloves" else 6)
    if stats.get("group") in (7, 8, 9, 10, 11):
        stats.setdefault("width", 2)
        stats.setdefault("height", 2)
    for key in ("skill", "magic_power"):
        if stats.get(key) == 0:
            del stats[key]
    if stats.get("is_ammunition") is False:
        del stats["is_ammunition"]
    stats["requires"] = {k: requires.get(k, 0) for k in ("level", "strength", "agility", "energy", "vitality")}
    # The Magic Gladiator is this game's, not 0.75's: every piece a knight or a wizard may wear
    # but a helm, and every shield (the hand-made recipes' classes_gladiator notes).
    explicit = bool({"magicGladiatorClassLevel", "magicGladiatorClass"} & set(found["params"]))
    if not explicit and (stats.get("group") == 6 or (stats.get("group") != 7 and {"knight", "wizard"} & set(classes))):
        classes.append("gladiator")
        stats["classes_gladiator"] = "Added by mu new on the project's rule: every shield, and every knight's or wizard's piece but a helm."
    stats["classes"] = classes
    if levels and max(levels) > 1:
        stats["class_level"] = max(levels)
    return stats


# ----------------------------------------------------------------------------- MuMain item models

GROUP_BASES = ["MODEL_SWORD", "MODEL_AXE", "MODEL_MACE", "MODEL_SPEAR", "MODEL_BOW", "MODEL_STAFF",
               "MODEL_SHIELD", "MODEL_HELM", "MODEL_ARMOR", "MODEL_PANTS", "MODEL_GLOVES", "MODEL_BOOTS"]


def item_model(group, number):
    """MuMain's file for (group, number), as OpenItems/OpenPlayers load it: "Item/Sword21.bmd".

    Three shapes in ZzzOpenData.cpp: a loop `AccessModel(MODEL_SWORD + i, ..., L"Sword", i + 1)`
    for the early numbers, a named model `AccessModel(MODEL_KNIGHT_BLADE, ..., L"Sword", 21)`
    whose enum is `MODEL_SWORD + 20`, and a named file with no number, `L"HDK_Sword"`. MU's
    numbering (number + 1) holds only for the first: the Knight Blade is sword 20 in Sword21.
    """
    base = GROUP_BASES[group]
    enums = read(os.path.join(MUMAIN, "Core", "Globals", "_enum.h"))
    names = {m.group(1) for m in re.finditer(rf"^\s*(MODEL_\w+)\s*=\s*{base}\s*\+\s*{number}\s*,", enums, re.M)}
    path = os.path.join(MUMAIN, "Engine", "Object", "ZzzOpenData.cpp")
    text = read(path)
    call = r'AccessModel\(([^,]+), L"Data\\\\(\w+)\\\\", L"(\w+)"(?:, ([^)]+))?\)'
    for m in re.finditer(rf"for \(int i = (\d+); i < (\d+); [^)]*\)", text):
        rest = text[m.end():].lstrip()
        body = rest[:rest.find("}")] if rest.startswith("{") else rest[:rest.find(";") + 1]
        for c in re.finditer(call, body):
            # `MODEL_HELM + i + 10` with `i + 1`: the elf helms are helm 10 on in HelmElf01.
            shape = re.fullmatch(rf"{base}\+i(?:\+(\d+))?", c.group(1).replace(" ", ""))
            if not shape or not c.group(4):
                continue
            i = number - int(shape.group(1) or 0)
            if int(m.group(1)) <= i < int(m.group(2)):
                offset = int(re.sub(r"[^\d]", "", c.group(4)) or 0)
                return f"{c.group(2)}/{c.group(3)}{i + offset:02d}.bmd", rel(path, line_of(text, m.start()))
    for c in re.finditer(call, text):
        target = c.group(1).replace(" ", "")
        if target in names or target == f"{base}+{number}":
            file = f"{c.group(3)}{int(c.group(4)):02d}" if c.group(4) and c.group(4).strip().isdigit() else c.group(3)
            return f"{c.group(2)}/{file}.bmd", rel(path, line_of(text, c.start()))
    return None, None


def find_file(root, model):
    """`model` under `root`, ignoring case: MU's Data spells sword21.bmd and Sword20.bmd alike."""
    folder, name = os.path.split(model)
    here = os.path.join(root, folder)
    for file in os.listdir(here) if os.path.isdir(here) else []:
        if file.lower() == name.lower():
            return os.path.join(here, file)
    return None
