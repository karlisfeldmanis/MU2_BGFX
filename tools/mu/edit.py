"""`mu edit` and `mu recipe`: one value of a recipe or a sheet changed in place, and journalled.

The studio's inspector (docs/plans/order-plan.md §9c, F4) and the agent both write through here, so
a recipe edited from the window and one edited by hand land the same way:

    ./mu recipe Sword01                          the recipe's file(s), parsed, and the library
    ./mu edit Sword01 islands.0.material brass   one value; the path's numbers are list indices
    ./mu edit content/sheets/lighting.json sun_strength 1.6

Only a value already there, and only a scalar -- a number, a string, a bool, null -- is
changed, and only its own span of text is replaced: every other byte of the file stays as it
was. A re-dump would not do: 39 of the recipes mix indents within one file (blocks appended by
scripts at their own depth), and re-dumping them reformats the whole file in git. A list of
numbers (a colour) is edited one element at a time, by its index.

Two more, for what a world's Place adds and takes away (§9c, sprint G), under the same rule:

    ./mu edit lorencia objects.2870 '{"model": ...}' --insert   a value put in, here at the end
    ./mu edit lorencia objects.2234 --remove                   one taken out, its separator too
    ./mu edit lorencia objects.5.scale 1.0 --insert             a key a block did not have

An insert is written in its neighbours' style -- their separator, their indent, the file's
indent step -- so the diff is the new lines alone; a remove cuts the value and the separator
before it. Each is the other's undo, and the journal's line names which it was (`op`).

Every edit appends a line to workshop/.mu/journal.log: when, who, the file, the path, the old
value and the new. That is how the agent reads what the user changed in the window.
"""

import json
import os
import time

from things import ROOT, SHEETS, SOURCE, WORKSHOP, NotFound

JOURNAL = os.path.join(WORKSHOP, ".mu", "journal.log")
MATERIALS = os.path.join(SOURCE, "materials")


# ----------------------------------------------------------------------------- spans

class Node:
    """A JSON value and where it lies in the text: [start, end)."""

    __slots__ = ("value", "start", "end", "children", "keys")

    def __init__(self, value, start, end, children=None, keys=None):
        self.value, self.start, self.end, self.children = value, start, end, children
        self.keys = keys   # a block's: each key's own start, where its member's text begins


def parse(text):
    """The text as a Node tree. Strict JSON; raises ValueError where json.loads would."""
    i = 0
    n = len(text)

    def space():
        nonlocal i
        while i < n and text[i] in " \t\r\n":
            i += 1

    def value():
        nonlocal i
        space()
        start = i
        c = text[i]
        if c == "{":
            i += 1
            children = {}
            keys = {}
            out = {}
            space()
            if text[i] == "}":
                i += 1
                return Node(out, start, i, children, keys)
            while True:
                space()
                keys_at = i
                key = json.loads(text[i:scan_string(i)])
                i = scan_string(i)
                space()
                if text[i] != ":":
                    raise ValueError(f"expected : at {i}")
                i += 1
                child = value()
                children[key] = child
                keys[key] = keys_at
                out[key] = child.value
                space()
                if text[i] == ",":
                    i += 1
                    continue
                if text[i] == "}":
                    i += 1
                    return Node(out, start, i, children, keys)
                raise ValueError(f"expected , or }} at {i}")
        if c == "[":
            i += 1
            children = []
            space()
            if text[i] == "]":
                i += 1
                return Node([], start, i, children)
            while True:
                child = value()
                children.append(child)
                space()
                if text[i] == ",":
                    i += 1
                    continue
                if text[i] == "]":
                    i += 1
                    return Node([ch.value for ch in children], start, i, children)
                raise ValueError(f"expected , or ] at {i}")
        if c == '"':
            end = scan_string(i)
            i = end
            return Node(json.loads(text[start:end]), start, end)
        end = i
        while end < n and text[end] not in ",]} \t\r\n":
            end += 1
        i = end
        return Node(json.loads(text[start:end]), start, end)

    def scan_string(at):
        j = at + 1
        while text[j] != '"':
            j += 2 if text[j] == "\\" else 1
        return j + 1

    root = value()
    space()
    if i != n:
        raise ValueError(f"text after the value at {i}")
    return root


def walk(node, path):
    for step in path:
        # A block keyed by numbers (actions.json's "101"): in a block a number is a key's word.
        if isinstance(node.children, dict) and isinstance(step, int):
            step = str(step)
        if isinstance(node.children, dict) and isinstance(step, str) and step in node.children:
            node = node.children[step]
        elif isinstance(node.children, list) and isinstance(step, int) and 0 <= step < len(node.children):
            node = node.children[step]
        else:
            raise NotFound(f"no {'.'.join(str(s) for s in path)} here (stopped at {step!r})")
    return node


def split_path(dotted):
    return [int(p) if p.lstrip("-").isdigit() else p for p in dotted.split(".") if p != ""]


# ----------------------------------------------------------------------------- files

def resolve(target):
    """A recipe's name (or Set<suffix>, or Arm<recipe>) or a path under content/ or content/sheets/ ->
    the absolute files it is, refusing anything outside those two folders."""
    if target.endswith(".json"):
        path = os.path.realpath(target if os.path.isabs(target) else os.path.join(ROOT, target))
        if not (path.startswith(SOURCE + os.sep) or path.startswith(SHEETS + os.sep)):
            raise NotFound(f"{target}: only files under content/ and content/sheets/ are edited here")
        if not os.path.exists(path):
            raise NotFound(f"no file {target}")
        return [path]
    from things import find
    if target.startswith("Set") and target[3:4].isupper():
        files = []
        for piece in ("Helm", "Armor", "Pant", "Glove", "Boot"):
            try:
                files.append(find(piece + target[3:]).recipe)
            except NotFound:
                continue
        if not files:
            raise NotFound(f"no piece of the set {target[3:]} under content/")
        return files
    return [find(target).recipe]


def library():
    try:
        return sorted(f[:-5] for f in os.listdir(MATERIALS) if f.endswith(".json"))
    except OSError:
        return []


def recipe(target):
    """What the inspector shows: each file, relative to the root, and its parsed contents."""
    files = []
    for path in resolve(target):
        with open(path) as handle:
            files.append({"file": os.path.relpath(path, ROOT), "doc": json.load(handle)})
    return {"files": files, "materials": library()}


def edit(target, path, value, who="mu"):
    """Sets the scalar at `path` (a list, or a dotted string) in `target`'s file to `value`."""
    file = one_file(target)
    if isinstance(path, str):
        path = split_path(path)
    with open(file) as handle:
        text = handle.read()
    node = walk(parse(text), path)
    old = node.value
    if isinstance(old, (dict, list)):
        raise NotFound(f"{'.'.join(map(str, path))} is a {'list' if isinstance(old, list) else 'block'}: "
                       f"edit its values one at a time")
    if isinstance(old, bool) != isinstance(value, bool):
        raise NotFound(f"{'.'.join(map(str, path))} is {json.dumps(old)}: give it a "
                       f"{'true or false' if isinstance(old, bool) else 'value of its own kind'}")
    # A whole number stays written as one, so `40` does not become `40.0` in git.
    if isinstance(old, int) and not isinstance(old, bool) and isinstance(value, float) and value.is_integer():
        value = int(value)
    written = json.dumps(value, ensure_ascii=False)
    new_text = text[:node.start] + written + text[node.end:]

    # Proof before the write: the new text parses, and differs from the old in this one place.
    proved(text, new_text, path, lambda holder, key: holder.__setitem__(key, value))
    return commit(file, text, new_text, {"path": ".".join(map(str, path)), "old": old, "new": value}, who)


def one_file(target):
    files = resolve(target)
    if len(files) != 1:
        raise NotFound(f"{target} is {len(files)} files: name one of them "
                       f"({', '.join(os.path.relpath(f, ROOT) for f in files)})")
    return files[0]


def commit(file, text, new_text, what, who):
    """Writes `new_text` over `text` and journals it; a change to nothing is neither."""
    if new_text != text:
        temporary = file + ".mu-edit"
        with open(temporary, "w") as handle:
            handle.write(new_text)
        os.replace(temporary, file)
    line = {"at": time.strftime("%Y-%m-%d %H:%M:%S"), "who": who,
            "file": os.path.relpath(file, ROOT), **what}
    if new_text != text:   # a value set to itself is not news
        os.makedirs(os.path.dirname(JOURNAL), exist_ok=True)
        with open(JOURNAL, "a") as handle:
            handle.write(json.dumps(line, ensure_ascii=False) + "\n")
    return line


def proved(text, new_text, path, change):
    """Raises unless `new_text` is `text` parsed with `change` done at `path`'s holder, and no
    other difference: the proof before any write."""
    expect = json.loads(text)
    holder = expect
    word = lambda h, step: str(step) if isinstance(h, dict) and isinstance(step, int) else step
    for step in path[:-1]:
        holder = holder[word(holder, step)]
    change(holder, word(holder, path[-1]))
    if json.loads(new_text) != expect:
        raise NotFound("the edit would change more than its one value; not written")


# ----------------------------------------------------------------------------- insert, remove

def indent_of(text, at):
    """The whitespace that starts the line `at` is on."""
    start = text.rfind("\n", 0, at) + 1
    end = start
    while end < len(text) and text[end] in " \t":
        end += 1
    return text[start:end]


def members(node):
    """A list's or a block's children in order, each as (where its text starts, its node)."""
    if isinstance(node.children, dict):
        return [(node.keys[k], child) for k, child in node.children.items()]
    return [(child.start, child) for child in node.children]


def insert(target, path, value, who="mu"):
    """Puts `value` in at `path`: a list's index (its length appends), or a block's new key."""
    file = one_file(target)
    if isinstance(path, str):
        path = split_path(path)
    if not path:
        raise NotFound("insert where? name a list's index or a block's new key")
    with open(file) as handle:
        text = handle.read()
    parent = walk(parse(text), path[:-1])
    last, dotted = path[-1], ".".join(map(str, path))
    if isinstance(parent.children, dict) and isinstance(last, int):
        last = path[-1] = str(last)
    if isinstance(parent.children, list):
        if not isinstance(last, int) or not 0 <= last <= len(parent.children):
            raise NotFound(f"{dotted}: a list of {len(parent.children)} takes an index 0 to "
                           f"{len(parent.children)}")
    elif isinstance(parent.children, dict):
        if not isinstance(last, str) or last in parent.children:
            raise NotFound(f"{dotted}: {'already there: edit it' if last in parent.children else 'a key is a word'}")
    else:
        raise NotFound(f"{'.'.join(map(str, path[:-1])) or 'the file'} is a value, not a list or a block")

    # The neighbours' style: what lies between two of them (or before the first), the indent
    # they stand at, and the step from the holder's indent to theirs.
    have = members(parent)
    if len(have) >= 2:
        sep = text[have[0][1].end:have[1][0]]
    elif have:
        sep = "," + text[parent.start + 1:have[0][0]]
    else:
        sep = ""
    if "\n" in sep:
        own = sep[sep.rfind("\n") + 1:]
        outer = indent_of(text, parent.start)
        step = own[len(outer):] if own.startswith(outer) and len(own) > len(outer) else " "
        written = json.dumps(value, ensure_ascii=False, indent=step).replace("\n", "\n" + own)
    else:
        written = json.dumps(value, ensure_ascii=False)
    if isinstance(parent.children, dict):
        written = json.dumps(last, ensure_ascii=False) + ": " + written

    if not have:
        new_text = text[:parent.start + 1] + written + text[parent.end - 1:]
    elif isinstance(last, int) and last == 0:
        at = have[0][0]
        new_text = text[:at] + written + sep + text[at:]
    else:
        at = have[(last if isinstance(last, int) else len(have)) - 1][1].end
        new_text = text[:at] + sep + written + text[at:]

    def put(holder, key):
        if isinstance(holder, list):
            holder.insert(key, value)
        else:
            holder[key] = value
    proved(text, new_text, path, put)
    return commit(file, text, new_text, {"op": "insert", "path": dotted, "old": None, "new": value}, who)


def remove(target, path, who="mu"):
    """Takes the value at `path` out of its list or block, with the separator before it."""
    file = one_file(target)
    if isinstance(path, str):
        path = split_path(path)
    if not path:
        raise NotFound("remove what? name a list's index or a block's key")
    with open(file) as handle:
        text = handle.read()
    parent = walk(parse(text), path[:-1])
    node = walk(parent, path[-1:])
    have = members(parent)
    which = next(n for n, (_at, child) in enumerate(have) if child is node)
    if which > 0:
        cut = (have[which - 1][1].end, node.end)
    elif len(have) > 1:
        cut = (have[0][0], have[1][0])
    else:
        cut = (parent.start + 1, parent.end - 1)
    new_text = text[:cut[0]] + text[cut[1]:]

    def take(holder, key):
        del holder[key]
    proved(text, new_text, path, take)
    return commit(file, text, new_text,
                  {"op": "remove", "path": ".".join(map(str, path)), "old": node.value, "new": None}, who)
