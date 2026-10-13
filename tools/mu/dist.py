"""`mu dist macos`: the app a player is given, dist/macos/MU2.app (docs/layout.md).

Each step is said as it runs:

  1. The player's build, from a COMMIT and never from the working tree, where other sessions keep
     half-done edits (the first app went out at protocol 30, HEAD's 29, the server's 26).
     build-ship-src/ is that commit's tree, recipes and docs left out, rsynced by content so
     ninja rebuilds only what changed; build-ship/ is configured from it once with MU2_SHIP=ON,
     and mu2 and mupack built. The game and nothing else (no studio, bench or developer switch).
  2. The manifest (manifest.py): every file the game reads, grouped into packs. Nothing it does
     not reach goes in: no recipes, no glb sources, no cook intermediates, no stale textures.
  3. The bundle: the player's MU2.app copied into dist/macos/, glfw copied into its Frameworks
     and the binary pointed at that copy, so a Mac without Homebrew opens it.
  4. The packs: a list a pack, and mupack writes each into Contents/Resources/data/.
  5. The proof: `mupack --check` mounts the packs as the game does and reads every file back
     through the game's own reader, byte for byte against its source.
  6. Signed ad hoc, so it opens here. A Developer ID and notarisation are what let another Mac
     open it without a warning; that is a step this does not take.

Every step runs niced, the packer held to `mu cores`.
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from paths import ASSETS, EXTERN, ROOT, SHEETS  # noqa: E402

import manifest  # noqa: E402
import settings  # noqa: E402

SHIP = os.path.join(ROOT, "build-ship")
SNAPSHOT = os.path.join(ROOT, "build-ship-src")
DIST = os.path.join(ROOT, "dist")
GLFW = "libglfw.3.dylib"


def source_of(logical, shaders, sheets=SHEETS):
    """The file on disk a pack's logical path is made from."""
    top, _, rest = logical.partition("/")
    if top == "assets":
        return os.path.join(ASSETS, rest)
    if top == "sheets":
        return os.path.join(sheets, rest)
    if top == "fonts":
        return os.path.join(EXTERN, rest)
    if top == "shaders":
        return os.path.join(shaders, rest.partition("/")[2])
    raise ValueError(f"no source for {logical}")


def run(command, say, **kw):
    done = subprocess.run(["nice", "-n", "10"] + command, capture_output=True, text=True, **kw)
    if done.returncode != 0:
        say((done.stdout + done.stderr).strip()[-2000:])
        raise RuntimeError(f"{os.path.basename(command[0])} exited {done.returncode}")
    return done.stdout


def snapshot(ref, say):
    """The commit's tree in build-ship-src/: what is built, and the sheets the packs carry."""
    commit = run(["git", "rev-parse", "--verify", ref + "^{commit}"], say, cwd=ROOT).strip()
    line = run(["git", "log", "-1", "--format=%h %cs", commit], say, cwd=ROOT).strip()
    staged = SNAPSHOT + ".new"
    shutil.rmtree(staged, ignore_errors=True)
    os.makedirs(staged)
    archive = subprocess.run(["git", "archive", "--format=tar", commit, "--", ".",
                              ":(exclude)source", ":(exclude)docs"], cwd=ROOT, capture_output=True)
    if archive.returncode != 0:
        raise RuntimeError(archive.stderr.decode().strip())
    subprocess.run(["tar", "-x", "-C", staged], input=archive.stdout, check=True)
    with open(os.path.join(staged, "COMMIT"), "w") as out:
        out.write(line + "\n")
    # By content, without times: a file that changed gets today's time and is rebuilt; one
    # that did not keeps its old time and is not. A commit's own times would undo that.
    os.makedirs(SNAPSHOT, exist_ok=True)
    # extern/ and assets/ are links to this checkout's, made here and kept: git has neither.
    run(["rsync", "-rlc", "--delete", "--exclude", "/extern", "--exclude", "/assets",
         staged + "/", SNAPSHOT + "/"], say)
    shutil.rmtree(staged)
    for name, target in (("extern", EXTERN), ("assets", ASSETS)):
        link = os.path.join(SNAPSHOT, name)
        if not os.path.islink(link):
            os.symlink(os.path.relpath(target, SNAPSHOT), link)
    say(f"the player's build is made from {line} (build-ship-src/)")
    return line


def build(cores, say):
    cache = os.path.join(SHIP, "CMakeCache.txt")
    if os.path.exists(cache) and f"CMAKE_HOME_DIRECTORY:INTERNAL={SNAPSHOT}\n" not in open(cache).read():
        say("build-ship/ was configured from another tree; configuring it again from the snapshot")
        shutil.rmtree(SHIP)
    if not os.path.exists(os.path.join(SHIP, "build.ninja")):
        say("configuring build-ship/ (MU2_SHIP=ON); the first build compiles bgfx")
        run(["cmake", "-S", SNAPSHOT, "-B", SHIP, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             "-DMU2_SHIP=ON"], say)
    say("building the player's mu2 and mupack")
    run(["cmake", "--build", SHIP, "--target", "mu2", "mupack", "--", f"-j{cores}"], say)


def bundle(app, say):
    """The player's MU2.app into dist/, carrying its own glfw."""
    if os.path.exists(app):
        shutil.rmtree(app)
    shutil.copytree(os.path.join(SHIP, "MU2.app"), app, symlinks=True)
    binary = os.path.join(app, "Contents", "MacOS", "MU2")
    linked = [line.split()[0] for line in run(["otool", "-L", binary], say).splitlines()[1:]]
    frameworks = os.path.join(app, "Contents", "Frameworks")
    os.makedirs(frameworks, exist_ok=True)
    for lib in linked:
        if lib.startswith(("/System/", "/usr/lib/", "@")):
            continue
        name = os.path.basename(lib)
        say(f"carrying {name} inside the app (it was {lib})")
        copy = os.path.join(frameworks, name)
        shutil.copy2(os.path.realpath(lib), copy)
        os.chmod(copy, 0o755)
        run(["install_name_tool", "-id", f"@rpath/{name}", copy], say)
        run(["install_name_tool", "-change", lib, f"@executable_path/../Frameworks/{name}",
             binary], say)
    # Nothing but the system and our own copy may be linked now.
    for lib in run(["otool", "-L", binary], say).splitlines()[1:]:
        lib = lib.split()[0]
        if not lib.startswith(("/System/", "/usr/lib/", "@executable_path/")):
            raise RuntimeError(f"the app still links {lib}, which a player's Mac may not have")


def dist(target, say, ref="HEAD", level=12):
    if target != "macos":
        raise RuntimeError(f"dist {target}: only macos so far (docs/layout.md, Windows and Linux)")
    started = time.time()
    cores, _ = settings.cores()
    made_from = snapshot(ref, say)
    build(cores, say)

    shaders = os.path.join(SHIP, "shaders", "metal")
    sheets = os.path.join(SNAPSHOT, "sheets")
    found = manifest.runtime(shaders_dir=shaders)
    if found["missing"]:
        say(f"{len(found['missing'])} files are named but not on disk; the packs go without them")
    files = sum(len(p) for p in found["packs"].values())
    size = sum(sum(p.values()) for p in found["packs"].values())
    say(f"the manifest: {files} files, {size / 1e9:.2f} GB in {len(found['packs'])} packs")

    app = os.path.join(DIST, "macos", "MU2.app")
    bundle(app, say)
    data = os.path.join(app, "Contents", "Resources", "data")
    os.makedirs(data, exist_ok=True)
    lists = os.path.join(DIST, "macos", "lists")
    os.makedirs(lists, exist_ok=True)
    mupack = os.path.join(SHIP, "mupack")
    every = os.path.join(lists, "all.txt")
    with open(every, "w") as everything:
        for pack, entries in sorted(found["packs"].items()):
            listed = os.path.join(lists, pack + ".txt")
            with open(listed, "w") as out:
                for logical in sorted(entries):
                    line = f"{logical}\t{source_of(logical, shaders, sheets)}\n"
                    out.write(line)
                    everything.write(line)
            say(run([mupack, os.path.join(data, pack + ".mupak"), listed, "--level", str(level),
                     "--threads", str(cores)], say).strip())

    say("reading every file back through the game's reader")
    say(run([mupack, "--check", data, every], say).strip().splitlines()[-1])

    run(["codesign", "--force", "--deep", "--sign", "-", app], say)
    packed = sum(os.path.getsize(os.path.join(data, f)) for f in os.listdir(data))
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(app) for f in fs)
    say(f"{app}: {total / 1e9:.2f} GB ({packed / 1e9:.2f} GB of packs from {size / 1e9:.2f} GB), "
        f"signed ad hoc, from {made_from}, in {time.time() - started:.0f} s")
    with open(os.path.join(DIST, "macos", "BUILT_FROM"), "w") as out:
        out.write(made_from + "\n")
    return True, {"app": app, "bytes": total, "packs": packed, "files": files, "commit": made_from}
