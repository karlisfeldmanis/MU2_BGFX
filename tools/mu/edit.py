"""`mu edit` and `mu recipe`: one value of a recipe or a sheet changed in place, and journalled.

The studio's inspector (docs/order-plan.md §9c, F4) and the agent both write through here, so
a recipe edited from the window and one edited by hand land the same way:

    ./mu recipe Sword01                          the recipe's file(s), parsed, and the library
    ./mu edit Sword01 islands.0.material brass   one value; the path's numbers are list indices
    ./mu edit sheets/lighting.json sun_strength 1.6

Only a value already there, and only a scalar -- a number, a string, a bool, null -- is
changed, and only its own span of text is replaced: every other byte of the file stays as it
was. A re-dump would not do: 39 of the recipes mix indents within one file (blocks appended by
scripts at their own depth), and re-dumping them reformats the whole file in git. A list of
numbers (a colour) is edited one element at a time, by its index.

Every edit appends a line to workshop/.mu/journal.log: when, who, the file, the path, the old
value and the new. That is how the agent reads what the user changed in the window.
"""

import json
import os
import time

from things import ROOT, SOURCE, WORKSHOP, NotFound

JOURNAL = os.path.join(WORKSHOP, ".mu", "journal.log")
SHEETS = os.path.join(ROOT, "sheets")
MATERIALS = os.path.join(SOURCE, "materials")


# ----------------------------------------------------------------------------- spans

class Node:
    """A JSON value and where it lies in the text: [start, end)."""

    __slots__ = ("value", "start", "end", "children")

    def __init__(self, value, start, end, children=None):
        self.value, self.start, self.end, self.children = value, start, end, children


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
            out = {}
            space()
            if text[i] == "}":
                i += 1
                return Node(out, start, i, children)
            while True:
                space()
                key = json.loads(text[i:scan_string(i)])
                i = scan_string(i)
                space()
                if text[i] != ":":
                    raise ValueError(f"expected : at {i}")
                i += 1
                child = value()
                children[key] = child
                out[key] = child.value
                space()
                if text[i] == ",":
                    i += 1
                    continue
                if text[i] == "}":
                    i += 1
                    return Node(out, start, i, children)
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
    """A recipe's name (or Set<suffix>, or Arm<recipe>) or a path under source/ or sheets/ ->
    the absolute files it is, refusing anything outside those two folders."""
    if target.endswith(".json"):
        path = os.path.realpath(target if os.path.isabs(target) else os.path.join(ROOT, target))
        if not (path.startswith(SOURCE + os.sep) or path.startswith(SHEETS + os.sep)):
            raise NotFound(f"{target}: only files under source/ and sheets/ are edited here")
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
            raise NotFound(f"no piece of the set {target[3:]} under source/")
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
    files = resolve(target)
    if len(files) != 1:
        raise NotFound(f"{target} is {len(files)} files: name one of them "
                       f"({', '.join(os.path.relpath(f, ROOT) for f in files)})")
    file = files[0]
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
    expect = json.loads(text)
    holder = expect
    for step in path[:-1]:
        holder = holder[step]
    holder[path[-1]] = value
    if json.loads(new_text) != expect:
        raise NotFound("the edit would change more than its one value; not written")

    if new_text != text:
        temporary = file + ".mu-edit"
        with open(temporary, "w") as handle:
            handle.write(new_text)
        os.replace(temporary, file)
    line = {"at": time.strftime("%Y-%m-%d %H:%M:%S"), "who": who,
            "file": os.path.relpath(file, ROOT), "path": ".".join(map(str, path)),
            "old": old, "new": value}
    if new_text != text:   # a value set to itself is not news
        os.makedirs(os.path.dirname(JOURNAL), exist_ok=True)
        with open(JOURNAL, "a") as handle:
            handle.write(json.dumps(line, ensure_ascii=False) + "\n")
    return line
