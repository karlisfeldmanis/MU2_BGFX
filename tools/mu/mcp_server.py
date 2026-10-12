"""The Cooker as an MCP server: an agent works in the studio the user has open, and the user
watches it happen (docs/cooker.md).

The user, 2026-10-12: "cooker has to work similar like MCP, that it's opened and we see
realtime how objects are made and improved and bug fixed ... if we cook a new monster on Aida,
we see it on the Aida map where it will be placed and a character fighting the monster."

So every tool here acts through what the studio already is, and nothing beside it:

- the studio's control socket (workshop/.mu/studio.sock, StudioDesk::act's words) for what is
  shown: the subject, the scene, Place, the arena, a shot;
- `mu serve` (workshop/.mu/serve.sock) for cooks, whose bars the open studio draws;
- `./mu ... --json` for edits, plans and checks, so the code that runs is always the current one.

A cook of what is on the bench swaps itself in when it finishes, with before and after shots,
and a monster's arena restarts on the new tables: the studio does that on its own (sprint F).
An edit an agent makes shows in the studio's inspector and Place as soon as it is journalled.

Speaks MCP over stdio: JSON-RPC 2.0, a message a line. No SDK: the protocol's four requests
(initialize, tools/list, tools/call, ping) are all a tool server needs.

    claude mcp add cooker -- python3 MU2_BGFX/tools/mu/mcp_server.py      (or .mcp.json)
"""

import base64
import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MU = os.path.join(ROOT, "mu")
DOT_MU = os.path.join(ROOT, "workshop", ".mu")
STUDIO_SOCK = os.path.join(DOT_MU, "studio.sock")
SERVE_SOCK = os.path.join(DOT_MU, "serve.sock")
JOURNAL = os.path.join(DOT_MU, "journal.log")
SHOTS = os.path.join(DOT_MU, "mcp_shots")
ARENA_LOG = os.path.join(ROOT, "workshop", "logs", "mu", "arena.log")
VERSION = "2025-06-18"


class Refused(Exception):
    pass


# ----------------------------------------------------------------------------- the two sockets

def studio(lines, timeout=120):
    """Each line to the open studio, its answer back; stops at the first error."""
    if not os.path.exists(STUDIO_SOCK):
        raise Refused("no studio is open (workshop/.mu/studio.sock). Ask the user to open it -- "
                      "`build/mu2 --studio` -- or call studio_open if they asked you to.")
    answers = []
    with socket.socket(socket.AF_UNIX) as conn:
        try:
            conn.connect(STUDIO_SOCK)
        except OSError as failed:
            raise Refused(f"the studio's socket does not answer ({failed}): is it still open?")
        conn.settimeout(timeout)
        stream = conn.makefile("rw")
        for line in [one.strip() for one in lines if one.strip()]:
            stream.write(line + "\n")
            stream.flush()
            answer = stream.readline().strip()
            answers.append(f"{line} -> {answer}")
            if not answer.startswith("ok"):
                break
    return answers


def serve(ask):
    """One request to mu serve, started first when it is not running."""
    if not os.path.exists(SERVE_SOCK) or not _answers(SERVE_SOCK):
        subprocess.Popen([MU, "serve"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True)
        for _ in range(40):
            time.sleep(0.25)
            if os.path.exists(SERVE_SOCK) and _answers(SERVE_SOCK):
                break
        else:
            raise Refused("mu serve did not start; see workshop/logs/mu/serve.log")
    with socket.socket(socket.AF_UNIX) as conn:
        conn.connect(SERVE_SOCK)
        conn.settimeout(30)
        stream = conn.makefile("rw")
        stream.write(json.dumps(ask) + "\n")
        stream.flush()
        return json.loads(stream.readline())


def _answers(path):
    try:
        with socket.socket(socket.AF_UNIX) as conn:
            conn.settimeout(1)
            conn.connect(path)
        return True
    except OSError:
        return False


def mu(*args, data=False):
    """`./mu <args>`: its prose; with `data`, run with --json and its last-line JSON too (which
    then stands in for the prose: --json keeps mu quiet)."""
    done = subprocess.run([MU, *args] + (["--json"] if data else []), cwd=ROOT, capture_output=True, text=True,
                          timeout=600)
    lines = done.stdout.strip().splitlines()
    data = None
    if lines:
        try:
            data = json.loads(lines[-1])
            lines = lines[:-1]
        except ValueError:
            pass
    prose = "\n".join(lines + ([done.stderr.strip()] if done.stderr.strip() else []))
    ok = done.returncode == 0 and (data is None or data.get("ok", True))
    return ok, prose, data


# ----------------------------------------------------------------------------- the tools

def t_status(_):
    out = []
    if os.path.exists(STUDIO_SOCK) and _answers(STUDIO_SOCK):
        out += studio(["status"])
    else:
        out.append("no studio open")
    if os.path.exists(SERVE_SOCK) and _answers(SERVE_SOCK):
        jobs = serve({"op": "jobs"}).get("jobs", [])[-5:]
        out += [f"job {j.get('number')} {' '.join(j.get('target', []) if isinstance(j.get('target'), list) else [str(j.get('target'))])}: "
                f"{j.get('state')}" for j in jobs] or ["mu serve: no jobs"]
    else:
        out.append("mu serve not running (cook starts it)")
    return "\n".join(out)


def t_studio_open(_):
    if os.path.exists(STUDIO_SOCK) and _answers(STUDIO_SOCK):
        return "the studio is already open"
    subprocess.Popen([os.path.join(ROOT, "build", "mu2"), "--studio", "--mute"], cwd=ROOT,
                     stdout=open(os.path.join(DOT_MU, "studio_mcp.log"), "w"), stderr=subprocess.STDOUT,
                     start_new_session=True)
    for _ in range(120):
        time.sleep(0.5)
        if os.path.exists(STUDIO_SOCK) and _answers(STUDIO_SOCK):
            return "the studio is open; its log is workshop/.mu/studio_mcp.log"
    raise Refused("the studio did not open its socket in 60 s; see workshop/.mu/studio_mcp.log")


def t_do(a):
    return "\n".join(studio([w for w in a["words"].split(";")]))


def t_show(a):
    """A thing put where the user sees it: on the bench, in its place, a monster fighting."""
    tab = {"monster": "Monsters", "object": "World objects", "weapon": "Weapons", "armour": "Armour sets",
           "wings": "Wings", "person": "People"}[a["kind"]]
    label, home = _label(a["kind"], a["name"])
    # A monster's tab lists the breeds of the world open, so its own world opens first.
    words = ([f"scene {home}", "wait 30"] if home else []) + \
        [f"subject {tab}|{label}", f"scene {a.get('scene') or 'Home'}", "wait 60"]
    if a["kind"] == "monster" and a.get("fight", True):
        words.append("arena")
    answers = studio(words)
    if a["kind"] == "monster" and a.get("fight", True):
        answers.append("the arena opens in a window of its own: a character fights it in its world; "
                       "arena_log reads how the fight goes")
    return "\n".join(answers)


def _label(kind, name):
    """What the studio's browser calls a recipe, and the world it lives in: a figure by its label
    (the recipe's mu_name, "Death Tree"), so that `DeathTree01` finds the monster and not its
    Figure parts entry; a monster's `world`, whose breeds its tab lists."""
    folders = {"monster": ["monsters"], "person": ["npc"]}.get(kind, [])
    for folder in folders:
        path = os.path.join(ROOT, "source", folder, name + ".json")
        if os.path.exists(path):
            with open(path) as handle:
                doc = json.load(handle)
            return doc.get("mu_name") or name, doc.get("world") if kind == "monster" else None
    return name, None


def t_shot(a):
    os.makedirs(SHOTS, exist_ok=True)
    # Unique to the millisecond: a name seen before would hand back the last picture at once.
    path = os.path.join(SHOTS, time.strftime("%H%M%S") + f"_{int(time.time() * 1000) % 1000:03d}.png")
    words = (["hide"] if a.get("clean") else []) + [f"shot {path}", "wait 3"] + (["show"] if a.get("clean") else [])
    studio(words)
    for _ in range(40):
        if os.path.exists(path) and os.path.getsize(path) > 0:
            break
        time.sleep(0.1)
    else:
        raise Refused("the shot was not written")
    time.sleep(0.2)
    data, mime = _small(path)
    return [{"type": "text", "text": f"the studio as the user sees it: {path}"},
            {"type": "image", "data": data, "mimeType": mime}]


def _small(path):
    """The shot at half size as a JPEG, so a look costs a fraction of the full PNG."""
    try:
        from PIL import Image
        import io
        image = Image.open(path).convert("RGB")
        image.thumbnail((1280, 1280))
        buffer = io.BytesIO()
        image.save(buffer, "JPEG", quality=85)
        return base64.b64encode(buffer.getvalue()).decode(), "image/jpeg"
    except ImportError:
        with open(path, "rb") as handle:
            return base64.b64encode(handle.read()).decode(), "image/png"


def t_plan(a):
    ok, prose, _ = mu("make", *a["target"].split(), "--plan")
    if not ok:
        raise Refused(prose)
    return prose


def t_cook(a):
    reply = serve({"op": "make", "target": a["target"].split(), "cores": None})
    if "error" in reply:
        raise Refused(reply["error"])
    return (f"job {reply['job']} queued in mu serve: the open studio shows its bars, and swaps in what it "
            f"cooked when it is on the bench. `jobs` with wait=true for the end.")


def t_jobs(a):
    deadline = time.time() + min(float(a.get("seconds", 300)), 900)
    while True:
        jobs = serve({"op": "jobs"}).get("jobs", [])
        if a.get("job") is not None:
            jobs = [j for j in jobs if j.get("number") == a["job"]]
        busy = [j for j in jobs if j.get("state") in ("queued", "running")]
        if not a.get("wait") or not busy or time.time() > deadline:
            return json.dumps(jobs[-8:], indent=1)
        time.sleep(2)


def t_recipe(a):
    ok, prose, data = mu("recipe", a["name"], data=True)
    if not ok:
        raise Refused(prose)
    return json.dumps(data.get("data", data), indent=1)[:60000]


def t_edit(a):
    args = ["edit", a["target"], a["path"]]
    how = a.get("how", "set")
    if how != "remove":
        args.append(json.dumps(a["value"]) if not isinstance(a.get("value"), str) else json.dumps(a["value"]))
    if how in ("insert", "remove"):
        args.append("--" + how)
    ok, prose, _ = mu(*args)
    if not ok:
        raise Refused(prose)
    return prose


def t_gate(a):
    if a["how"] == "add":
        ok, prose, _ = mu("gate", "add", a["world"], a["to"], str(a["column"]), str(a["row"]))
    else:
        ok, prose, _ = mu("gate", "remove", a["world"], str(a["number"]))
    if not ok:
        raise Refused(prose)
    return prose


def t_light(a):
    """A knob of a map's light: its sheet's value set, or written in when the map inherits it."""
    layer = a.get("layer") or "map"
    world = a.get("world", "")
    files = {"map": f"sheets/worlds/{world}.json", "base": "sheets/lighting.json",
             "dusk": "sheets/time/dusk.json", "night": "sheets/time/night.json",
             "rain": f"sheets/worlds/{world}_rain.json"}
    if layer not in files or (layer in ("map", "rain") and not world):
        raise Refused("layer is map, rain (both with world), base, dusk or night")
    path = os.path.join(ROOT, files[layer])
    if not os.path.exists(path):
        raise Refused(f"no {files[layer]}")
    with open(path) as handle:
        sheet = json.load(handle)
    if a.get("value") is None:
        with open(os.path.join(ROOT, "sheets", "lighting.json")) as handle:
            base = json.load(handle)
        key = a.get("key")
        if key:
            return f"{files[layer]} {key}: {json.dumps(sheet.get(key))} (base {json.dumps(base.get(key))})"
        return json.dumps({k: v for k, v in sheet.items() if "note" not in k}, indent=1)
    args = ["edit", files[layer], a["key"], json.dumps(a["value"])]
    if a["key"] not in sheet:
        args.append("--insert")
    ok, prose, _ = mu(*args)
    if not ok:
        raise Refused(prose)
    return prose + "\nthe open studio re-applies it within a quarter of a second"


def t_journal(a):
    if not os.path.exists(JOURNAL):
        return "the journal is empty"
    with open(JOURNAL) as handle:
        lines = handle.readlines()[-int(a.get("last", 20)):]
    who = a.get("who")
    out = []
    for line in lines:
        try:
            one = json.loads(line)
        except ValueError:
            continue
        if who and one.get("who") != who:
            continue
        if one.get("op", "").startswith("paint"):
            out.append(f"{one['at']} {one['who']} {one['op']} {one['file']}: {len(one.get('tiles', []))} tiles")
        else:
            out.append(f"{one['at']} {one['who']} {one.get('op', 'set')} {one['file']} {one['path']}: "
                       f"{json.dumps(one.get('old'))[:80]} -> {json.dumps(one.get('new'))[:80]}")
    return "\n".join(out) or "nothing"


def t_arena_log(a):
    if not os.path.exists(ARENA_LOG):
        return "no arena has run (show a monster with fight=true)"
    with open(ARENA_LOG, errors="replace") as handle:
        return "".join(handle.readlines()[-int(a.get("last", 40)):])


def t_check(_):
    ok, prose, _ = mu("check")
    return ("ok\n" if ok else "failing\n") + prose


TOOLS = [
    ("status", t_status, "What the open studio has on its bench and in which scene, and mu serve's last jobs.",
     {}),
    ("studio_open", t_studio_open,
     "Open the studio window (build/mu2 --studio) for the user to watch. Only when the user asked for it: a "
     "window takes their focus. Usually they have it open already.", {}),
    ("show", t_show,
     "Put a thing where the user sees it in the open studio: on the bench, in its own place (scene Home: a "
     "monster at its spawn in its world, an armour set on the character screen, the rest at Lorencia's bonfire), "
     "and a monster fighting a character in the arena window. Do this first when you start on a thing, so the "
     "user watches it change.",
     {"kind": {"type": "string", "enum": ["monster", "object", "weapon", "armour", "wings", "person"]},
      "name": {"type": "string", "description": "the recipe's name, as the studio's browser lists it"},
      "scene": {"type": "string", "description": "Home (default), Stage, or a world's name"},
      "fight": {"type": "boolean", "description": "a monster: open the arena (default true)"}},
     ["kind", "name"]),
    ("shot", t_shot, "See the studio as the user sees it now: a picture of its window.",
     {"clean": {"type": "boolean", "description": "hide the panels for the picture"}}),
    ("do", t_do,
     "Any of the studio's words, ';' between them (docs/cooker.md, 'The studio's words'): subject, scene, plus, "
     "swap, arena, place, mode, pick, nudge, add, duplicate, delete, mark, budge, look, way, brush, dab, undo, "
     "tab light, hide, show, wait N, status. Place's edits made this way show at once and are written to "
     "source/.",
     {"words": {"type": "string"}}, ["words"]),
    ("plan", t_plan, "What `mu make <target>` would run and why, running nothing. Always before cook.",
     {"target": {"type": "string", "description": "Sword01, world/Bonfire01, BudgeDragon01, placements aida, "
                                                  "tables aida, realm, ground aida, world aida ..."}},
     ["target"]),
    ("cook", t_cook,
     "Queue `mu make <target>` in mu serve; returns the job. The studio shows its progress and swaps in what it "
     "cooked if it is on the bench (a monster's arena restarts on it). Ask the user before a cook that changes "
     "what the game shows: a Blender build, a ground cook, every world's tables, realm and a rebuild.",
     {"target": {"type": "string"}}, ["target"]),
    ("jobs", t_jobs, "mu serve's jobs; with wait, until the job (or all) is done or `seconds` pass.",
     {"job": {"type": "integer"}, "wait": {"type": "boolean"}, "seconds": {"type": "number"}}),
    ("recipe", t_recipe, "A recipe's file(s), parsed, and the material library.",
     {"name": {"type": "string"}}, ["name"]),
    ("edit", t_edit,
     "Change source/ in place, journalled, only the touched text: how=set (one value), insert (a value put in "
     "at path: a list's index, or a new key), remove (path taken out). The studio's inspector shows it at once.",
     {"target": {"type": "string", "description": "a recipe's name or a file under source/ or sheets/"},
      "path": {"type": "string", "description": "dotted, list indices as numbers: islands.0.material"},
      "value": {"description": "JSON value (not for remove)"},
      "how": {"type": "string", "enum": ["set", "insert", "remove"]}},
     ["target", "path"]),
    ("gate", t_gate, "Add a gate with its pair (a way in here at column,row; its way out on the target's arrival "
                     "tile) or remove one with its pair. Then plan realm.",
     {"how": {"type": "string", "enum": ["add", "remove"]}, "world": {"type": "string"},
      "to": {"type": "string"}, "column": {"type": "integer"}, "row": {"type": "integer"},
      "number": {"type": "integer"}}, ["how", "world"]),
    ("light", t_light,
     "A map's lighting, seen live in the open studio: the sun (azimuth, elevation, sun_colour, sun_strength), "
     "sky and daylight (sky_colour, ambient_strength, dust_*), shadows (shadow_range, shadow_bias_metres, "
     "ssao_*), tonemap (exposure, contrast, saturation, split, tint_low, tint_high), bloom. Without value: read "
     "the layer (or one key, with the base's value beside it). With value: set it; a key the map inherits is "
     "written in. layer: map (default; sheets/worlds/<world>.json), rain, base (every map), dusk, night. Put "
     "the world on screen first (do 'scene <world>'), and 'time dusk' to see dusk.",
     {"world": {"type": "string"}, "key": {"type": "string"}, "value": {"description": "a number or [r, g, b]"},
      "layer": {"type": "string", "enum": ["map", "rain", "base", "dusk", "night"]}}),
    ("journal", t_journal, "The last edits to source/, the user's in the studio (who=studio) and the agents'.",
     {"last": {"type": "integer"}, "who": {"type": "string"}}),
    ("arena_log", t_arena_log, "The arena's log: how the fight on screen is going (hits, misses, deaths).",
     {"last": {"type": "integer"}}),
    ("check", t_check, "./mu check: layers, sizes, WAL, index, sync, white meshes, tables.", {}),
]


def listed():
    out = []
    for one in TOOLS:
        name, _fn, text, props = one[:4]
        required = one[4] if len(one) > 4 else []
        out.append({"name": name, "description": text,
                    "inputSchema": {"type": "object", "properties": props, "required": required}})
    return out


def call(name, arguments):
    for one in TOOLS:
        if one[0] == name:
            try:
                result = one[1](arguments or {})
            except Refused as refused:
                return {"content": [{"type": "text", "text": str(refused)}], "isError": True}
            except Exception as crash:   # a tool's crash is the agent's to read, not the server's end
                return {"content": [{"type": "text", "text": f"{type(crash).__name__}: {crash}"}], "isError": True}
            content = result if isinstance(result, list) else [{"type": "text", "text": str(result)}]
            return {"content": content, "isError": False}
    return {"content": [{"type": "text", "text": f"no tool {name}"}], "isError": True}


def main():
    for line in sys.stdin:
        try:
            message = json.loads(line)
        except ValueError:
            continue
        method, ident = message.get("method"), message.get("id")
        if ident is None:
            continue   # a notification: initialized, cancelled
        if method == "initialize":
            result = {"protocolVersion": message.get("params", {}).get("protocolVersion", VERSION),
                      "capabilities": {"tools": {}},
                      "serverInfo": {"name": "cooker", "version": "1"},
                      "instructions": "The Cooker: work in the studio the user has open, so they watch. "
                                      "Start with status, then show the thing you work on. docs/cooker.md."}
        elif method == "tools/list":
            result = {"tools": listed()}
        elif method == "tools/call":
            params = message.get("params", {})
            result = call(params.get("name"), params.get("arguments"))
        elif method == "ping":
            result = {}
        else:
            reply = {"jsonrpc": "2.0", "id": ident, "error": {"code": -32601, "message": f"no method {method}"}}
            sys.stdout.write(json.dumps(reply) + "\n")
            sys.stdout.flush()
            continue
        sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": ident, "result": result}) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
