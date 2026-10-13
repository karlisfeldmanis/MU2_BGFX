"""The shipped client's file set: what the game reads at runtime, and nothing else, in packs.

docs/layout.md, "What a player is given". The player build reads its data out of .mupak packs;
this says which files go into which pack. It is worked out by REACHABILITY, not by folder:

  1. The roots are what src/ names outright -- the string literals of the player build (the
     studio, the bench and the sweep are left out of it, so their literals are too) -- plus
     the manifests src/ opens by a name it builds: each world's tables and town, the figures'
     and the wardrobe's json, index.json's interface art, every world's world.json and json.
  2. From there every file reached is read for the paths it names, and those are followed:
     a json's string values, a glb's external uris, and the plain path strings inside the
     cooked binaries (.mum .muc .mut .mur .mus .mup, magic MU2M/MU2C/MU2T/MU2R/MU2S/MU2P,
     which carry `cooked/lorencia/textures/x.ktx` as written by tools/cook.py write_string).
  3. What src/ reaches by a name it makes at runtime -- a folder it lists, a file it names
     out of a table -- is taken a folder at a time and marked "dynamic", with the src line.
  4. Whatever is kept without a proof that the game reads it is marked "unproven" and kept:
     a file wrongly left out is a broken game, a file wrongly kept is a few bytes.

Logical paths are relative to the data root the C++ reader is pointed at:

  assets/...          a file under ASSETS. The strings inside the cooked binaries are relative
                      to ASSETS ("cooked/..."), so the reader strips "assets/" to match them.
  sheets/...          lighting.json, lobby.json, time/*.json, worlds/*.json (never materials*)
  fonts/<name>.ttf    the faces src/ names, out of EXTERN
  shaders/metal/...   the .sc.bin, out of `shaders_dir` (the ship build's own, by argument)

    python3 tools/mu/manifest.py                 the packs and their sizes, the unproven list
    python3 tools/mu/manifest.py --json OUT      and the whole manifest written to OUT
    python3 tools/mu/manifest.py --stale         the cooked textures nothing reaches, and why

    import manifest; m = manifest.runtime(shaders_dir=...)   # m["packs"][name] = {logical: bytes}

Read-only: it never writes under assets/, never cooks, never deletes.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import re
import struct
import sys
import time

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from paths import ASSETS, BUILD, COOKED, EXTERN, ROOT, SHEETS, WORKSHOP  # noqa: E402

SRC = os.path.join(ROOT, "src")

# What the player build leaves out (docs/layout.md "The player build"): the studio, the bench
# and its scenes, the sweep and headless. Their literals name files only the tools read --
# bench_scenes.cpp opens Lorencia's tables for a bench, studio_marks.cpp source/world -- and
# counting them would prove files the player never opens.
NOT_SHIPPED = re.compile(r"^(studio/|app/modes/studio_|app/modes/bench|game/bench|app/sweep|"
                         r"game/headless|game/studio_)")

# The cooked binaries, by magic. Each carries its paths as plain length-prefixed strings.
COOKED_MAGIC = {b"MU2M", b"MU2C", b"MU2T", b"MU2R", b"MU2S", b"MU2P"}
COOKED_EXT = {".mum", ".muc", ".mut", ".mur", ".mus", ".mup"}

# A path as the content writes one: under one of ASSETS' top folders, ending in a file type.
# Spaces are allowed because the land's sheets have them ("TileGrass01 1_tiling_hd.png"); the
# match is non-greedy from the folder so a printable length byte before it is not taken in.
TOPS = r"(?:cooked|effects|interface|items|music|voice|world|sounds|grades|npc|players|assets)"
EXTS = r"(?:ktx|mum|muc|mut|mur|mus|mup|walls|json|png|jpg|obj|glb|bin|wav|mp3|MP3|ogg)"
# In a cooked binary a string is tools/cook.py's write_string: a little-endian u16 length and
# the bytes. A path is taken whole by its own length, never by pattern: a stem can hold a
# second extension ("Tree06_tree_02.jpg_x3_albedo_<sha>.ktx") and a pattern stops at the first.
TOP_IN_BYTES = re.compile(TOPS.encode() + rb"/")
EXT_AT_END = re.compile(rb"\." + EXTS.encode() + rb"$")
PATH_IN_TEXT = re.compile(r"/?" + TOPS + r"/[^\"\n]*?\." + EXTS + r"(?![A-Za-z0-9])")

# The cooked worlds that are not a world: shared by every world, or the lobby's own scene.
NOT_A_WORLD = {"figures", "wardrobe", "showing", "charscene"}

# What the cook leaves beside its output, for itself: the BC7 inputs (`raw/*.bin`, docs/
# layout.md's 1.7 GB), its job lists, and its own indexes, which src/ never opens -- grep finds
# "textures.json" only in content/ground.cpp, and that is the WORLD's (cooked/<w>/textures.json).
COOK_ONLY = [
    ("cook intermediate (raw/*.bin)", lambda rel: "/raw/" in rel and rel.startswith("cooked/")),
    ("cook job lists (jobs*.txt)", lambda rel: rel.startswith("cooked/") and
     os.path.basename(rel).startswith("jobs") and rel.endswith(".txt")),
    ("cook's own index (figures/wardrobe textures.json, sounds.json)",
     lambda rel: rel in ("cooked/figures/textures.json", "cooked/wardrobe/textures.json",
                         "cooked/showing/sounds/sounds.json")),
]


# ---- reading ---------------------------------------------------------------------------------

def walk(top):
    """Every file under `top`, relative to it, with its size."""
    out = {}
    for folder, dirs, files in os.walk(top):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in files:
            if name.startswith("."):
                continue
            full = os.path.join(folder, name)
            out[os.path.relpath(full, top)] = os.path.getsize(full)
    return out


def norm(rel):
    """A content path as the files dict keys it: no leading slash, no `..`."""
    return os.path.normpath(rel.lstrip("/")).replace(os.sep, "/")


def strings_in(data):
    """The paths a cooked binary carries, each read by its own u16 length prefix."""
    out = []
    for match in TOP_IN_BYTES.finditer(data):
        at = match.start()
        if at < 2:
            continue
        length = struct.unpack("<H", data[at - 2:at])[0]
        text = data[at:at + length]
        if len(text) == length and all(32 <= c < 127 for c in text) and EXT_AT_END.search(text):
            out.append(text.decode())
    return out


def json_strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        # A "note" is prose for whoever reads the json: it cites source/ files the game never
        # opens (world.json's "source/npc/ElfEnvoy.json").
        for key, value in node.items():
            if not key.startswith("note"):
                yield from json_strings(value)
    elif isinstance(node, list):
        for value in node:
            yield from json_strings(value)


def read_glb_json(path):
    """A glb's JSON chunk, or None."""
    with open(path, "rb") as handle:
        head = handle.read(20)
        if len(head) < 20 or head[:4] != b"glTF":
            return None
        length, kind = struct.unpack("<II", head[12:20])
        if kind != 0x4E4F534A:
            return None
        return json.loads(handle.read(length))


def src_literals():
    """{src file: [string literals]} for the player build's sources."""
    out = {}
    literal = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    for folder, _dirs, files in os.walk(SRC):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue
            full = os.path.join(folder, name)
            rel = os.path.relpath(full, SRC)
            if NOT_SHIPPED.match(rel):
                continue
            with open(full, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            # Comments dropped: they cite paths (source/players/rig/...) the code never opens.
            text = re.sub(r"//[^\n]*", "", text)
            out[rel] = literal.findall(text)
    return out


# ---- the walk -------------------------------------------------------------------------------

class Reach:
    """The files reached, each with how: "proven" (a chain of names from src/), "dynamic"
    (a folder src/ lists or a name it builds), or "unproven" (kept without a proof)."""

    def __init__(self, files):
        self.files = files          # ASSETS-relative path -> bytes
        self.how = {}               # path -> (kind, why)
        self.queue = []
        self.missing = {}           # named but not on disk -> who named it
        self.leaf_only = set()      # read by the game, but what they name is not followed

    def add(self, rel, kind, why):
        rel = norm(rel)
        if rel not in self.files:
            self.missing.setdefault(rel, why)
            return False
        had = self.how.get(rel)
        rank = {"proven": 0, "dynamic": 1, "unproven": 2}
        if had is None or rank[kind] < rank[had[0]]:
            # Walked again on an upgrade, so what it names is upgraded with it.
            self.how[rel] = (kind, why)
            self.queue.append(rel)
        return True

    def named_in(self, rel, base=None):
        """The paths a file names, ASSETS-relative."""
        full = os.path.join(ASSETS, rel)
        ext = os.path.splitext(rel)[1].lower()
        names = []
        if ext in COOKED_EXT:
            with open(full, "rb") as handle:
                data = handle.read()
            if data[:4] in COOKED_MAGIC:
                names = strings_in(data)
        elif ext == ".json":
            try:
                with open(full) as handle:
                    doc = json.load(handle)
            except (OSError, ValueError):
                return []
            for text in json_strings(doc):
                names += PATH_IN_TEXT.findall(text)
                # A name relative to the json's own folder: a world json's height.png.
                if "/" not in text and "." in text:
                    beside = norm(os.path.join(os.path.dirname(rel), text))
                    if beside in self.files and beside != rel:
                        names.append(beside)
        elif ext == ".glb":
            doc = read_glb_json(full)
            for kind in ("images", "buffers"):
                for one in (doc or {}).get(kind, []):
                    if one.get("uri") and not one["uri"].startswith("data:"):
                        names.append(norm(os.path.join(os.path.dirname(rel), one["uri"])))
        return names

    def run(self):
        while self.queue:
            rel = self.queue.pop()
            if rel in self.leaf_only:
                continue
            kind, _why = self.how[rel]
            for name in self.named_in(rel):
                # What a proven file names is proven; what an unproven one names is kept on the
                # same terms, so nothing is promoted by being near something unproven.
                self.add(name, "proven" if kind != "unproven" else "unproven", "named in " + rel)


def printf_glob(text):
    """`Ice01_%02d.obj` -> `Ice01_*.obj`, for a name src/ formats."""
    return re.sub(r"%0?\d*[ds]", "*", text)


def runtime(shaders_dir=None):
    """The runtime file set. Returns a dict:

      packs:     {pack: {logical path: bytes}}
      how:       {logical path: [kind, why]}       kind proven | dynamic | unproven
      excluded:  {category: {"files": n, "bytes": b}}
      stale:     {ASSETS-relative ktx: bytes} (cooked textures nothing reaches)
      missing:   {path named somewhere: who named it} (named, not on disk)
      seconds:   how long it took
    """
    started = time.time()
    shaders_dir = shaders_dir or os.path.join(BUILD, "shaders", "metal")
    files = walk(ASSETS)
    reach = Reach(files)
    literals = src_literals()

    # --- 1. what src/ names outright ------------------------------------------------------
    worlds = sorted(d for d in os.listdir(COOKED) if os.path.isdir(os.path.join(COOKED, d))
                    and d not in NOT_A_WORLD)
    for rel_src, strings in literals.items():
        dirs = [norm(s) + "/" for s in strings if re.fullmatch(r"/?" + TOPS + r"(/[^\"%]*)?/?", s)
                and not os.path.splitext(s)[1]]
        leaves = [s for s in strings if re.fullmatch(r"[\w .%/-]*\.\w+", s.lstrip("./"))]
        for s in strings:
            for found in PATH_IN_TEXT.findall(s):
                reach.add(found, "proven", "src/" + rel_src)
        # A folder and a leaf named in the same file: fx/arrow.cpp's "/effects/missiles/" and
        # "Arrow01.obj"; "%02d" frames matched as a glob over what is there.
        for folder in dirs:
            for leaf in leaves:
                pattern = norm(folder + printf_glob(leaf))
                for rel in fnmatch.filter(files.keys(), pattern) if "*" in pattern else [pattern]:
                    if rel in files:
                        reach.add(rel, "proven", "src/" + rel_src)

    # --- 2. the manifests src/ opens by a name it builds -----------------------------------
    for w in worlds + ["charscene"]:
        base = f"cooked/{w}/"
        # game/play_open.cpp:43 .mur, game/world/town.cpp:57 .mut, game/world/grass.cpp:188
        # .walls, game/world/sway.cpp:65 clips.json (world-folder and file built from the name).
        for leaf in (f"{w}.mur", f"{w}.mut", f"{w}.walls", "clips.json"):
            reach.add(base + leaf, "proven", "src/ builds cooked/<w>/" + leaf.replace(w, "<w>"))
        # content/ground.cpp:630 <w>.json, :888 <w>_ground.glb, :877 ground_surfaces.json;
        # game/world/world_show.cpp:174 world.json.
        wdir = f"world/{w}/"
        for leaf in (f"{w}.json", f"{w}_ground.glb", "ground_surfaces.json", "world.json"):
            reach.add(wdir + leaf, "proven", "src/ builds world/<w>/" + leaf.replace(w, "<w>"))
        # content/ground.cpp:871 reads the cooked textures.json, but only for its "ground/"
        # keys (below): the town's meshes name their own .ktx, and the rest of its entries are
        # the cook's index of every image it ever deduplicated, superseded ones included.
        reach.add(base + "textures.json", "proven", "src/content/ground.cpp:871")
        reach.leaf_only.add(base + "textures.json")
        # The land's sheets: ground_surfaces.json's names, through the cooked textures.json's
        # "ground/<name>:<role>" when the cook made one, else the loose .png beside it
        # (content/ground.cpp readLayer).
        cooked_textures = {}
        try:
            with open(os.path.join(COOKED, w, "textures.json")) as handle:
                cooked_textures = json.load(handle).get("textures", {})
        except (OSError, ValueError):
            pass
        try:
            with open(os.path.join(ASSETS, wdir, "ground_surfaces.json")) as handle:
                surfaces = json.load(handle)
        except (OSError, ValueError):
            surfaces = []
        for surface in surfaces:
            for layer in ("base", "overlay"):
                node = surface.get(layer) or {}
                for role in ("albedo", "normal", "orm"):
                    name = node.get(role)
                    if not name:
                        continue
                    mapped = cooked_textures.get(f"ground/{name}:{role}")
                    if not (mapped and reach.add(mapped, "proven",
                                                 f"cooked/{w}/textures.json ground/{name}:{role}")):
                        reach.add(wdir + name, "proven", "ground fallback, content/ground.cpp:334")
        # The rest of the world's cooked meshes and clips: boids.cpp, scurry.cpp and
        # trap_show.cpp open cooked/<w>/meshes/<model>.mum by a model name out of world.json.
        for rel in files:
            if rel.startswith(base) and rel.split("/")[2:3] in (["meshes"], ["clips"]):
                reach.add(rel, "dynamic", "boids/scurry/trap_show build cooked/<w>/meshes|clips")

    # The figures: game/figures.cpp:1138 figures_<w>.json or figures.json, and :1460 lists the
    # folder for every figures*.json; shows.json game/figure_shows.cpp.
    for rel in files:
        if re.fullmatch(r"cooked/figures/figures[^/]*\.json", rel):
            reach.add(rel, "dynamic", "game/figures.cpp:1460 lists cooked/figures/figures*.json")
    reach.add("cooked/figures/shows.json", "proven", "src/game/figure_shows.cpp")
    reach.add("cooked/wardrobe/wardrobe.json", "proven", "src/game/figures.cpp:623")
    # game/figures.cpp:793 opens cooked/wardrobe/clips/<name>.muc by name, and
    # game/item_models.cpp:86 cooked/wardrobe/meshes/<stem>.mum by an item's glb stem.
    for rel in files:
        if rel.startswith("cooked/wardrobe/clips/"):
            reach.add(rel, "dynamic", "game/figures.cpp:793 opens cooked/wardrobe/clips/<name>.muc")
        elif rel.startswith("cooked/wardrobe/meshes/"):
            reach.add(rel, "dynamic", "game/item_models.cpp:86 opens wardrobe/meshes/<stem>.mum")
        elif rel.startswith(("cooked/figures/meshes/", "cooked/figures/clips/")):
            reach.add(rel, "unproven", "a figure mesh or clip no figures*.json names")
    reach.add("cooked/showing/showing.mus", "proven", "src/game/fx/showing.cpp")
    reach.add("cooked/showing/missiles.mup", "proven", "src/game/fx/bones.cpp")

    # index.json (game/ui/panel.cpp:21): only its interface/ entries are read.
    reach.add("index.json", "proven", "src/game/ui/panel.cpp:21")
    with open(os.path.join(ASSETS, "index.json")) as handle:
        index = json.load(handle)
    for key, path in (index.get("effects") or {}).items():
        if isinstance(path, str) and path.startswith("interface/"):
            reach.add(path, "proven", f"index.json effects.{key}, game/ui/panel.cpp:26")

    # Voice: game/ui/desk.cpp:333 builds voice/<who>/<who>_<page>.wav from sim/quests.cpp.
    for rel in files:
        if rel.startswith("voice/") and rel.endswith(".wav"):
            reach.add(rel, "dynamic", "game/ui/desk.cpp:333 builds voice/<who>/<who>_<page>.wav")

    # --- 3. follow everything named ------------------------------------------------------
    # index.json itself is not followed: of its tables only `effects` is read at runtime, and
    # its other strings name source glbs (items/, assets/monsters/) the cook already turned
    # into .mum. Taken out of the queue before the walk and its interface entries added above.
    reach.leaf_only.add("index.json")
    reach.run()

    # Items: game/item_models.cpp:86-124 reads an item's glb only when the wardrobe has no
    # cooked/wardrobe/meshes/<stem>.mum. The tables (.mur) name every item's glb, so the walk
    # took them all; the ones with a cooked mesh are dropped again (and what only they named).
    def cooked_instead(rel):
        stem = os.path.splitext(os.path.basename(rel))[0]
        return rel.startswith("items/") and rel.endswith(".glb") and \
            f"cooked/wardrobe/meshes/{stem}.mum" in files
    drop = {rel for rel in reach.how if cooked_instead(rel)}
    if drop:
        keep_named = set()
        for rel in reach.how:
            if rel not in drop and rel.endswith(".glb"):
                keep_named.update(norm(n) for n in reach.named_in(rel))
        for rel in drop:
            for name in reach.named_in(rel):
                name = norm(name)
                if name not in keep_named and reach.how.get(name, ("", ""))[1] == "named in " + rel:
                    reach.how.pop(name, None)
            reach.how.pop(rel)

    # Kept without a proof: the interface and the effects. Both are a few MB, and the fx
    # build their paths from folder and leaf in ways a literal scan cannot always pair.
    for rel in files:
        if rel.startswith(("interface/", "effects/")) and rel not in reach.how:
            reach.add(rel, "unproven", "interface/effects art no src literal or table names")
    reach.run()

    # --- 4. packs ------------------------------------------------------------------------
    packs = {}
    how = {}

    def put(pack, logical, size, kind, why):
        packs.setdefault(pack, {})[logical] = size
        how[logical] = [kind, why]

    for rel, (kind, why) in sorted(reach.how.items()):
        parts = rel.split("/")
        if parts[0] == "cooked" and parts[1] in ("figures", "wardrobe", "showing"):
            pack = parts[1]
        elif parts[0] in ("cooked", "world") and len(parts) > 2 and parts[1] in worlds:
            pack = "world-" + parts[1]
        elif parts[0] in ("music", "voice"):
            pack = parts[0]
        else:
            pack = "common"
        put(pack, "assets/" + rel, files[rel], kind, why)

    # Sheets (app/application.cpp:147, app/context.cpp:25, app/modes/lobby_mode.cpp:304,
    # game/world/maps.cpp:33), never materials*.json, which only the pipeline reads.
    sheets = walk(SHEETS)
    for rel, size in sheets.items():
        if rel in ("lighting.json", "lobby.json") or re.fullmatch(r"(time|worlds)/[^/]+\.json", rel):
            put("common", "sheets/" + rel, size, "proven", "src/ sheets path")
    # Fonts: the faces src/ names (gfx/face.cpp, game/ui/controls.cpp, game/ui/desk.cpp:108).
    for rel_src, strings in literals.items():
        for s in strings:
            face = os.path.basename(s)
            if face.endswith(".ttf") and os.path.exists(os.path.join(EXTERN, face)):
                put("common", "fonts/" + face, os.path.getsize(os.path.join(EXTERN, face)),
                    "proven", "src/" + rel_src)
    # Shaders: every .sc.bin the build made (gfx/program.h names them by stem).
    if os.path.isdir(shaders_dir):
        for name in sorted(os.listdir(shaders_dir)):
            if name.endswith(".sc.bin"):
                put("common", "shaders/metal/" + name,
                    os.path.getsize(os.path.join(shaders_dir, name)), "proven", "gfx/program.h")

    # --- 5. what is left out, and why ------------------------------------------------------
    excluded = {}
    stale = {}

    def leave(category, rel):
        one = excluded.setdefault(category, {"files": 0, "bytes": 0})
        one["files"] += 1
        one["bytes"] += files[rel]

    for rel in files:
        if rel in reach.how:
            continue
        for category, test in COOK_ONLY:
            if test(rel):
                leave(category, rel)
                break
        else:
            parts = rel.split("/")
            if parts[0] == "cooked" and rel.endswith(".ktx"):
                stale[rel] = files[rel]
                leave("stale cooked textures (nothing reaches them)", rel)
            elif parts[0] == "world" and len(parts) > 3:
                leave("world/<w>/<object>/ sources (the cook's input)", rel)
            elif parts[0] == "world":
                leave("world/<w>/ top-level files nothing names (Tile*.png the cook mapped, ...)",
                      rel)
            elif parts[0] == "items" and cooked_instead(rel):
                leave("items/ glbs cooked into the wardrobe", rel)
            elif parts[0] == "items":
                leave("items/ files no table names", rel)
            elif parts[0] in ("assets", "npc", "players", "sounds", "grades"):
                leave(f"{parts[0]}/ (cook sources; src/ never opens them)", rel)
            elif parts[0] == "cooked" and parts[1] == "showing":
                leave("cooked/showing files nothing names (unused meshes, sounds, digests.json)",
                      rel)
            elif parts[0] == "music":
                leave("music/ tracks no world.json or src/ names", rel)
            elif parts[0] == "voice":
                leave("voice/ notes (not .wav)", rel)
            else:
                leave("other: " + "/".join(parts[:2]), rel)

    return {"packs": packs, "how": how, "excluded": excluded, "stale": stale,
            "missing": reach.missing, "seconds": round(time.time() - started, 2)}


# ---- the stale audit ------------------------------------------------------------------------

STALE_DIRS = ("cooked/figures/textures/", "cooked/wardrobe/textures/")
KTX_NAME = re.compile(r"_(albedo|normal|orm|emissive)_([0-9a-f]{16})\.ktx$")


def source_digests(roots):
    """{(sha1[:16], role)} for every image in every glb under `roots`, as tools/cook.py
    `collect`/`cook_figures` name a cooked texture: `<model>_<image>_<role>_<sha1 of the source
    bytes>[:16]`. Role is taken as any of the four, since the role comes from the material."""
    digests = set()
    for root in roots:
        for folder, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs if not d.startswith(".") and d not in ("logs", "bot")]
            for name in names:
                path = os.path.join(folder, name)
                if name.endswith(".png") and "/world/" in path:
                    # The land's loose sheets, which ground_jobs cooks by name (no digest).
                    continue
                if not name.endswith(".glb"):
                    continue
                try:
                    with open(path, "rb") as handle:
                        data = handle.read()
                except OSError:
                    continue
                if data[:4] != b"glTF":
                    continue
                at = 12
                doc, binary = None, b""
                while at + 8 <= len(data):
                    length, kind = struct.unpack("<II", data[at:at + 8])
                    chunk = data[at + 8:at + 8 + length]
                    if kind == 0x4E4F534A:
                        doc = json.loads(chunk)
                    elif kind == 0x004E4942:
                        binary = chunk
                    at += 8 + length
                if not doc:
                    continue
                views = doc.get("bufferViews", [])
                for image in doc.get("images", []):
                    if "bufferView" in image:
                        view = views[image["bufferView"]]
                        start = view.get("byteOffset", 0)
                        raw = binary[start:start + view["byteLength"]]
                    elif image.get("uri") and not image["uri"].startswith("data:"):
                        try:
                            with open(os.path.join(folder, image["uri"]), "rb") as handle:
                                raw = handle.read()
                        except OSError:
                            continue
                    else:
                        continue
                    digests.add(hashlib.sha1(raw).hexdigest()[:16])
    return digests


def stale_audit(result):
    """The cooked textures nothing reaches, with the proof. Report only; nothing is deleted."""
    stale = result["stale"]
    listed = {}
    for owner in ("figures", "wardrobe"):
        try:
            with open(os.path.join(COOKED, owner, "textures.json")) as handle:
                listed[owner] = set(json.load(handle)["textures"].values())
        except (OSError, ValueError, KeyError):
            listed[owner] = set()
    for w in os.listdir(COOKED):
        path = os.path.join(COOKED, w, "textures.json")
        if w not in listed and os.path.exists(path):
            with open(path) as handle:
                listed[w] = set(json.load(handle).get("textures", {}).values())
    began = time.time()
    in_assets = source_digests([ASSETS])
    in_workshop = source_digests([WORKSHOP])
    hashed = round(time.time() - began, 1)

    groups = {}
    for rel, size in sorted(stale.items()):
        owner = rel.split("/")[1]
        match = KTX_NAME.search(rel)
        digest = match.group(2) if match else None
        if digest and digest in in_assets:
            source = "a glb in assets/ still has these bytes"
        elif digest and digest in in_workshop:
            source = "only a glb in workshop/ has these bytes (not synced to assets/)"
        elif digest:
            source = "no glb in assets/ or workshop/ has these bytes"
        else:
            source = "no digest in its name"
        in_index = "listed in the cook's textures.json" if rel in listed.get(owner, ()) \
            else "not in the cook's textures.json"
        key = (owner, source, in_index)
        one = groups.setdefault(key, {"files": 0, "bytes": 0, "examples": []})
        one["files"] += 1
        one["bytes"] += size
        if len(one["examples"]) < 3:
            one["examples"].append(os.path.basename(rel))
    return {"groups": groups, "hash_seconds": hashed}


# ---- the command ----------------------------------------------------------------------------

def mb(n):
    return f"{n / 1e6:9.1f} MB"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--json", metavar="OUT", help="write the whole manifest to OUT")
    parser.add_argument("--stale", action="store_true", help="the stale-texture audit")
    parser.add_argument("--shaders-dir", default=None, help="the .sc.bin folder (default "
                        "build/shaders/metal)")
    args = parser.parse_args()

    result = runtime(args.shaders_dir)
    total = 0
    print("pack                     files        bytes   (unproven)")
    for pack, entries in sorted(result["packs"].items()):
        size = sum(entries.values())
        total += size
        unproven = sum(s for p, s in entries.items() if result["how"][p][0] == "unproven")
        print(f"  {pack:20} {len(entries):7} {mb(size)}  ({mb(unproven).strip()})")
    print(f"  {'total':20} {sum(len(e) for e in result['packs'].values()):7} {mb(total)}")
    print("\nleft out")
    for category, one in sorted(result["excluded"].items(), key=lambda kv: -kv[1]["bytes"]):
        print(f"  {mb(one['bytes'])} {one['files']:6} files  {category}")
    unproven = {}
    for path, (kind, why) in result["how"].items():
        if kind == "unproven":
            folder = "/".join(os.path.dirname(path).split("/")[:3])
            one = unproven.setdefault((folder, why), [0, 0])
            one[0] += 1
            one[1] += next(e[path] for e in result["packs"].values() if path in e)
    print("\nincluded, unproven")
    for (folder, why), (n, size) in sorted(unproven.items()):
        print(f"  {mb(size)} {n:5} files  {folder}/  ({why})")
    if result["missing"]:
        print(f"\nnamed but not on disk: {len(result['missing'])}")
        for path, who in sorted(result["missing"].items())[:20]:
            print(f"  {path}  <- {who}")
    print(f"\n{result['seconds']} s")

    if args.stale:
        audit = stale_audit(result)
        print(f"\nstale cooked textures (hashing the glbs took {audit['hash_seconds']} s)")
        for (owner, source, in_index), one in sorted(audit["groups"].items(),
                                                     key=lambda kv: (kv[0][0], -kv[1]["bytes"])):
            print(f"  {owner:12} {one['files']:5} files {mb(one['bytes'])}  {source}; {in_index}"
                  f"  e.g. {one['examples'][0]}")
        result["stale_audit"] = {"|".join(k): v for k, v in audit["groups"].items()}

    if args.json:
        with open(args.json, "w") as handle:
            json.dump(result, handle, indent=1, sort_keys=True)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
