"""`mu research <Monster>`: everything MuMain does for a monster, as a checklist in its recipe.

The user, 2026-10-06: "when you cook monsters ... check if there is no some special effects for
that monsters, like lights, baked in animations, sprites, etc" -- and 2026-10-12, choosing this
from four: a monster is not done until each thing MuMain does for it is carried or set aside.

MuMain names a monster three ways: MONSTER_X (its server number, CreateMonster's case),
MONSTER_MODEL_X (the model OpenMonsterModel opens) and MODEL_X (the model as a character, the
name every render, effect and sound arm keys on). The case gives the other two (sources.py).
Every line of MuMain's source naming any of them is found, and the block around it is one
item: a `case` to its `break` (and a case that has none falls through, which is said), an `if`
to its closing brace, anything else the line itself with its neighbours. Each item says where it
is (`at`, file:line from LEGACY/reference, as every recipe quotes), the function it is in, what
kind of thing it is, the actions it waits on and the calls it makes. Its `state` is todo, done
or skip, and its `note` says how ours carries it or why not. The studio's inspector draws the
list with the code on hover; a state is one `mu edit` (mu_checklist.N.state done).

    ./mu research 305                     a breed's list before it has a recipe
    ./mu research DeathRider01            the list, nothing written
    ./mu research DeathRider01 --write    into the recipe as `mu_checklist`, journalled; a state
                                          or note already set for the same `at` is kept
"""

import glob
import os
import re

import edit
import sources
from things import ROOT, NotFound

KIND_ORDER = ["body", "effect", "sound", "look", "animation", "bones", "other"]
EFFECT = re.compile(r"\b(Create(?:Effect|Joint|Particle|Sprite|Blur\w*|Bomb|Arrow|Magic\w*|Shiny|Fire\w*)\w*)\s*\(")
CALLS = re.compile(r"\b(Create\w+|PlayBuffer|AddTerrainLight|RenderMesh|RenderBody|RenderLinkObject|"
                   r"RegisterBone|BlendMeshTexCoord\w*|StreamMesh|SetAction|PlayAnimation|"
                   r"RENDER_\w+|BITMAP_\w+|SOUND_\w+)\b")
ACTIONS = re.compile(r"\b(MONSTER01_\w+)\b")
FUNCTION = re.compile(r"^(?!if\b|for\b|while\b|switch\b|return\b|else\b|case\b|#)"
                      r"[A-Za-z_][\w:<>\*&\s,]*?\b([A-Za-z_]\w*(?:::\w+)?)\s*\([^;]*\)\s*(?:const\s*)?\{?\s*$")


def names_for(doc):
    """The three names, and the case they came from, for a recipe's `object_type`."""
    number = doc.get("object_type")
    if not isinstance(number, int):
        raise NotFound("not a monster recipe: no object_type")
    case = sources.monster_case(number)
    if case is None:
        raise NotFound(f"no CreateMonster case for {number} in MuMain")
    model = re.search(r"CreateCharacter\([^,]+,\s*(MODEL_\w+)", case["block"])
    names = [case["enum"], case["model"]] + ([model.group(1)] if model else [])
    return [n for n in names if n], case


def kind_of(code):
    if "OpenMonsterModel" in code or "CreateCharacter" in code:
        return "body"
    if EFFECT.search(code):
        return "effect"
    if re.search(r"PlayBuffer|SOUND_|LoadWaveFile", code):
        return "sound"
    if re.search(r"BodyLight|RenderMesh|BlendMesh|StreamMesh|RENDER_|AddTerrainLight|Luminosity", code):
        return "look"
    if "PlaySpeed" in code or "SetAction" in code:
        return "animation"
    if re.search(r"RegisterBone|BoneTransform|TransformPosition", code):
        return "bones"
    return "other"


def block_end(lines, start, is_case):
    """The last line of the block opened at `start`, and whether a case falls through."""
    depth, opened = 0, False
    for i in range(start, min(len(lines), start + 80)):
        text = re.sub(r"//.*", "", lines[i])
        if is_case:
            if i > start and depth == 0 and re.match(r"\s*(case\b|default\s*:)", text):
                prior = "\n".join(lines[start:i])
                return i - 1, not re.search(r"\b(break|return)\b", prior)
            depth += text.count("{") - text.count("}")
            if depth <= 0 and re.search(r"\b(break|return)\b", text):
                return i, False
            if depth < 0:
                return i, False
        else:
            # An if: to its closing brace, or to its one statement's semicolon.
            opened = opened or "{" in text
            depth += text.count("{") - text.count("}")
            if opened and depth <= 0:
                return i, False
            if not opened and ";" in text:
                return i, False
    return min(len(lines), start + 80) - 1, False


def function_at(lines, at):
    for i in range(at, -1, -1):
        m = FUNCTION.match(lines[i])
        if m and not lines[i].startswith((" ", "\t")):
            return m.group(1)
    return ""


def scan(names):
    """Each block of MuMain's source that names a monster, as a checklist item."""
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\b")
    files = sorted(glob.glob(os.path.join(sources.MUMAIN, "**", "*.cpp"), recursive=True) +
                   glob.glob(os.path.join(sources.MUMAIN, "**", "*.h"), recursive=True))
    items = []
    for path in files:
        if path.endswith("_enum.h"):
            continue
        text = sources.read(path)
        if not pattern.search(text):
            continue
        lines = text.split("\n")
        spans = []
        for i, line in enumerate(lines):
            if not pattern.search(re.sub(r"//.*", "", line)):
                continue
            stripped = line.strip()
            if stripped.startswith("case "):
                start = i
                while start > 0 and lines[start - 1].strip().startswith("case "):
                    start -= 1
                end, falls = block_end(lines, i, True)
            elif re.match(r"(\}\s*)?(else\s+)?if\s*\(", stripped):
                start, (end, falls) = i, block_end(lines, i, False)
            else:
                start, end, falls = i, i, False
            if spans and start <= spans[-1][1] + 1:
                spans[-1] = (spans[-1][0], max(spans[-1][1], end), spans[-1][2] or falls)
            else:
                spans.append((start, end, falls))
        for start, end, falls in spans:
            code = "\n".join(lines[start:end + 1])
            calls = list(dict.fromkeys(m.group(1) for m in CALLS.finditer(code)))
            item = {"at": sources.rel(path, start + 1), "in": function_at(lines, start),
                    "kind": kind_of(code), "lines": end - start + 1,
                    "actions": list(dict.fromkeys(ACTIONS.findall(code))), "calls": calls[:12],
                    "state": "todo", "note": ""}
            if falls:
                item["note"] = "falls through into the next case: no break"
            if item["kind"] == "body" and not re.search(r"\b" + re.escape(names[0]) + r"\b", code):
                item["ours"] = False
            items.append(item)
    items.sort(key=lambda it: (KIND_ORDER.index(it["kind"]), it["at"]))
    return items


def sound_loads(items):
    """Where each SOUND_ the blocks play is loaded, and from which file: a sound item a line."""
    wanted = sorted({c for it in items for c in it["calls"] if c.startswith("SOUND_")})
    if not wanted:
        return []
    pattern = re.compile(r"LoadWaveFile\(\s*(" + "|".join(map(re.escape, wanted)) + r")\b[^\n]*?L?\"([^\"]+)\"")
    out = []
    for path in sorted(glob.glob(os.path.join(sources.MUMAIN, "**", "*.cpp"), recursive=True)):
        text = sources.read(path)
        for m in pattern.finditer(text):
            out.append({"at": sources.rel(path, sources.line_of(text, m.start())), "in": "LoadWaveFile",
                        "kind": "sound", "lines": 1, "actions": [], "calls": [m.group(1)],
                        "file": m.group(2).replace("\\\\", "/"), "state": "todo", "note": ""})
    return out


# Blocks that carry nothing a monster shows: a name for MU's own tools.
NOTHING_TO_CARRY = {"GetMonsterModelName": "MU's name for the model, for its tools; nothing to carry"}


def research(target, write=False, say=print):
    # A breed's number, before `mu new monster` has made its recipe: the list alone.
    if str(target).isdigit():
        if write:
            raise NotFound(f"{target} has no recipe yet: mu new monster {target}, then research it by name")
        file, doc = None, {"object_type": int(target)}
    else:
        file = edit.one_file(target)
        doc = edit.recipe(target)["files"][0]["doc"]
    names, case = names_for(doc)
    items = scan(names)
    items += sound_loads(items)
    items.sort(key=lambda it: (KIND_ORDER.index(it["kind"]), it["at"]))
    # What was already judged stays judged: the same block keeps its state and its note.
    kept = {it.get("at"): it for it in doc.get("mu_checklist") or [] if isinstance(it, dict)}
    for item in items:
        old = kept.get(item["at"])
        if old:
            item["state"] = old.get("state", "todo")
            item["note"] = old.get("note") or item["note"]
        elif item["at"] == case["from"]:
            item["state"], item["note"] = "done", "the recipe's monster block (mu new monster)"
        elif item["kind"] == "body" and not item.get("ours", True):
            item["state"], item["note"] = "skip", "another breed's body on the same model"
        elif item["in"] in NOTHING_TO_CARRY:
            item["state"], item["note"] = "skip", NOTHING_TO_CARRY[item["in"]]
    for item in items:
        item.pop("ours", None)
    say(f"{doc.get('mu_name', target)}: MuMain names it {', '.join(names)}")
    for i, it in enumerate(items):
        actions = f" on {', '.join(a.replace('MONSTER01_', '') for a in it['actions'])}" if it["actions"] else ""
        say(f"  {i:2d} {it['state']:<4} {it['kind']:<9} {it['at']}  {it['in']}{actions}: "
            f"{it.get('file') or ', '.join(it['calls'][:6]) or '-'}{'  -- ' + it['note'] if it['note'] else ''}")
    counts = {s: sum(1 for it in items if it["state"] == s) for s in ("done", "skip", "todo")}
    say(f"  {len(items)} blocks: {counts['done']} done, {counts['skip']} set aside, {counts['todo']} to do")
    if write:
        rel = os.path.relpath(file, ROOT)
        if "mu_checklist" in doc:
            edit.remove(target, ["mu_checklist"], who="mu research")
        edit.insert(target, ["mu_checklist"], items, who="mu research")
        say(f"  written to {rel} as mu_checklist (journalled)")
    return {"names": names, "items": items, "counts": counts}
