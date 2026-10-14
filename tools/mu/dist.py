"""`mu dist macos`: the app a player is given, dist/macos/MU2.app (docs/plans/layout.md).

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
from paths import ASSETS, EXTERN, OUT, ROOT, SHEETS  # noqa: E402

import manifest  # noqa: E402
import settings  # noqa: E402

SHIP = os.path.join(OUT, "build-ship")
WIN = os.path.join(OUT, "build-windows")
SNAPSHOT = os.path.join(OUT, "build-ship-src")
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
    # third_party/extern/ and out/assets/ are links to this checkout's, made here and kept: git
    # has neither.
    run(["rsync", "-rlc", "--delete", "--exclude", "/third_party/extern", "--exclude", "/out",
         staged + "/", SNAPSHOT + "/"], say)
    shutil.rmtree(staged)
    for name, target in (("third_party/extern", EXTERN), ("out/assets", ASSETS)):
        link = os.path.join(SNAPSHOT, name)
        os.makedirs(os.path.dirname(link), exist_ok=True)
        if not os.path.islink(link):
            os.symlink(os.path.relpath(target, os.path.dirname(link)), link)
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


def build_windows(cores, say):
    """MU2.exe, cross-compiled from the same snapshot by mingw (docs/plans/windows-client.md). Its
    shaders are SPIR-V, made by build-ship's own shaderc, the one this Mac can run."""
    cache = os.path.join(WIN, "CMakeCache.txt")
    if os.path.exists(cache) and f"CMAKE_HOME_DIRECTORY:INTERNAL={SNAPSHOT}\n" not in open(cache).read():
        shutil.rmtree(WIN)
    if not os.path.exists(os.path.join(WIN, "build.ninja")):
        say("configuring build-windows/ (mingw); the first build compiles bgfx and glfw")
        shaderc = os.path.join(SHIP, "third_party", "extern", "bgfx.cmake", "cmake", "bgfx", "shaderc")
        run(["cmake", "-S", SNAPSHOT, "-B", WIN, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(SNAPSHOT, "tools", "cmake", "mingw.cmake"),
             "-DMU2_HOST_SHADERC=" + shaderc], say)
    say("building MU2.exe")
    run(["cmake", "--build", WIN, "--target", "mu2", "--", f"-j{cores}"], say)


def pack(data, shaders, profile, lists, cores, level, say):
    """The manifest's packs into `data`, then every file read back through the game's reader."""
    sheets = os.path.join(SNAPSHOT, "content", "sheets")  # the commit's, since layout step 3
    found = manifest.runtime(shaders_dir=shaders, profile=profile)
    if found["missing"]:
        say(f"{len(found['missing'])} files are named but not on disk; the packs go without them")
    files = sum(len(p) for p in found["packs"].values())
    size = sum(sum(p.values()) for p in found["packs"].values())
    say(f"the manifest: {files} files, {size / 1e9:.2f} GB in {len(found['packs'])} packs")
    os.makedirs(data, exist_ok=True)
    os.makedirs(lists, exist_ok=True)
    mupack = os.path.join(SHIP, "mupack")
    every = os.path.join(lists, "all.txt")
    with open(every, "w") as everything:
        for name, entries in sorted(found["packs"].items()):
            listed = os.path.join(lists, name + ".txt")
            with open(listed, "w") as out:
                for logical in sorted(entries):
                    line = f"{logical}\t{source_of(logical, shaders, sheets)}\n"
                    out.write(line)
                    everything.write(line)
            say(run([mupack, os.path.join(data, name + ".mupak"), listed, "--level", str(level),
                     "--threads", str(cores)], say).strip())
    say("reading every file back through the game's reader")
    say(run([mupack, "--check", data, every], say).strip().splitlines()[-1])
    return files, size


def dist_windows(say, ref, level):
    """dist/windows/MU2/: MU2.exe, data/*.mupak and logs/, and MU2-windows.zip of it. The packs
    are the Mac's but for the shaders, which are SPIR-V for Vulkan."""
    started = time.time()
    cores, _ = settings.cores()
    made_from = snapshot(ref, say)
    build(cores, say)  # the Mac's mupack and shaderc
    build_windows(cores, say)
    client = os.path.join(DIST, "windows", "MU2")
    shutil.rmtree(client, ignore_errors=True)
    os.makedirs(os.path.join(client, "logs"))
    shutil.copy2(os.path.join(WIN, "MU2.exe"), client)
    files, size = pack(os.path.join(client, "data"), os.path.join(WIN, "shaders", "spirv"), "spirv",
                       os.path.join(DIST, "windows", "lists"), cores, level, say)
    with open(os.path.join(client, "BUILT_FROM"), "w") as out:
        out.write(made_from + "\n")
    zipped = os.path.join(DIST, "windows", "MU2-windows.zip")
    if os.path.exists(zipped):
        os.remove(zipped)
    say("zipping it (the packs are compressed already, so stored)")
    run(["zip", "-r", "-0", "-q", zipped, "MU2"], say, cwd=os.path.join(DIST, "windows"))
    total = os.path.getsize(zipped)
    say(f"{zipped}: {total / 1e9:.2f} GB from {made_from}, in {time.time() - started:.0f} s; "
        f"unzip anywhere and run MU2.exe")
    return True, {"client": client, "zip": zipped, "bytes": total, "files": files, "commit": made_from}


# The launcher itself, as the site gives it out: the same bundle shape the game's has -- glfw
# inside, signed ad hoc -- and a DMG holding it next to the link to Applications to drag it onto.
LAUNCHER_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
\t<key>CFBundleDevelopmentRegion</key><string>en</string>
\t<key>CFBundleDisplayName</key><string>MU2 Launcher</string>
\t<key>CFBundleExecutable</key><string>MU2Launcher</string>
\t<key>CFBundleIdentifier</key><string>com.mu2.launcher</string>
\t<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
\t<key>CFBundleName</key><string>MU2Launcher</string>
\t<key>CFBundlePackageType</key><string>APPL</string>
\t<key>CFBundleShortVersionString</key><string>1.0</string>
\t<key>CFBundleVersion</key><string>1</string>
\t<key>LSApplicationCategoryType</key><string>public.app-category.games</string>
\t<key>LSMinimumSystemVersion</key><string>11.0</string>
\t<key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
"""


# The one line the site offers beside the Mac button. A browser download carries the quarantine
# that Gatekeeper then stands behind ("Apple could not verify", macOS 15+ with no right-click
# door); a fetch by curl carries none, so this route opens the app straight away.
INSTALL_SH = """#!/bin/sh
# MU Classic's launcher for macOS, installed from Terminal: curl carries no quarantine, so
# macOS opens the app with no Gatekeeper step. The game it installs comes the same way.
set -e
where="$HOME/Applications"
if pgrep -x MU2Launcher >/dev/null 2>&1; then
    echo "The launcher is running -- close it first."
    exit 1
fi
mkdir -p "$where"
echo "Fetching the launcher..."
curl -fsSL https://mu-classic.com/downloads/MU2Launcher.tar.gz -o "$where/.MU2Launcher.tgz"
rm -rf "$where/MU2Launcher.app"
tar -xzf "$where/.MU2Launcher.tgz" -C "$where"
rm -f "$where/.MU2Launcher.tgz"
echo "Installed in $where. Opening..."
open "$where/MU2Launcher.app"
"""


def dist_launcher(say):
    """dist/launcher/MU2Launcher.app and MU2.dmg: the launcher for the Mac (docs/plans/launcher.md,
    L5). Built from the working tree -- it is one small program and no game content -- and the DMG
    holds the app with the Applications link beside it, to drag one onto the other."""
    started = time.time()
    say("building the launcher")
    build_dir = os.path.join(OUT, "build")
    run(["cmake", "--build", build_dir, "--target", "mu2_launcher"], say)
    stage = os.path.join(DIST, "launcher")
    app = os.path.join(stage, "MU2Launcher.app")
    shutil.rmtree(app, ignore_errors=True)
    mac = os.path.join(app, "Contents", "MacOS")
    os.makedirs(mac)
    os.makedirs(os.path.join(app, "Contents", "Frameworks"))
    shutil.copy2(os.path.join(build_dir, "MU2Launcher"), mac)
    # glfw, as the game's bundle carries it (bundle() above): inside, and pointed at.
    homebrew = run(["brew", "--prefix", "glfw"], say).strip()
    glfw = os.path.join(homebrew, "lib", GLFW)
    shutil.copy2(glfw, os.path.join(app, "Contents", "Frameworks", GLFW))
    # Homebrew's dylib is read-only, and a drag out of the DMG keeps the mode -- a 444 file then
    # refuses the quarantine strip that a player's first right-click needs.
    os.chmod(os.path.join(app, "Contents", "Frameworks", GLFW), 0o644)
    run(["install_name_tool", "-change", glfw, f"@executable_path/../Frameworks/{GLFW}",
         os.path.join(mac, "MU2Launcher")], say)
    with open(os.path.join(app, "Contents", "Info.plist"), "w") as out:
        out.write(LAUNCHER_PLIST)
    run(["codesign", "--force", "--deep", "--sign", "-", app], say)

    dmg = os.path.join(stage, "MU2.dmg")
    if os.path.exists(dmg):
        os.remove(dmg)
    into = os.path.join(stage, "image")
    shutil.rmtree(into, ignore_errors=True)
    os.makedirs(into)
    shutil.copytree(app, os.path.join(into, "MU2Launcher.app"))
    os.symlink("/Applications", os.path.join(into, "Applications"))
    run(["hdiutil", "create", "-volname", "MU2 Launcher", "-srcfolder", into, "-ov", "-format", "UDZO",
         dmg], say)
    shutil.rmtree(into)
    total = os.path.getsize(dmg)

    # The same app as a tarball, for the Terminal install: a download by curl carries no
    # quarantine, so the app opens with no Gatekeeper step at all (docs/plans/launcher.md, L5).
    # The script the site offers next to the DMG's button is written here too, saying this URL.
    tared = os.path.join(stage, "MU2Launcher.tar.gz")
    if os.path.exists(tared):
        os.remove(tared)
    run(["tar", "-czf", tared, "-C", stage, "MU2Launcher.app"], say)
    with open(os.path.join(stage, "install-macos.sh"), "w") as out:
        out.write(INSTALL_SH)
    say(f"{dmg}: {total / 1e6:.0f} MB, app signed ad hoc, in {time.time() - started:.0f} s")
    return True, {"dmg": dmg, "app": app, "bytes": total}


def dist(target, say, ref="HEAD", level=12):
    if target == "launcher":
        return dist_launcher(say)
    if target == "windows":
        return dist_windows(say, ref, level)
    if target != "macos":
        raise RuntimeError(f"dist {target}: macos or windows")
    started = time.time()
    cores, _ = settings.cores()
    made_from = snapshot(ref, say)
    build(cores, say)

    app = os.path.join(DIST, "macos", "MU2.app")
    bundle(app, say)
    data = os.path.join(app, "Contents", "Resources", "data")
    files, size = pack(data, os.path.join(SHIP, "shaders", "metal"), "metal",
                       os.path.join(DIST, "macos", "lists"), cores, level, say)

    run(["codesign", "--force", "--deep", "--sign", "-", app], say)
    packed = sum(os.path.getsize(os.path.join(data, f)) for f in os.listdir(data))
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(app) for f in fs)
    say(f"{app}: {total / 1e9:.2f} GB ({packed / 1e9:.2f} GB of packs from {size / 1e9:.2f} GB), "
        f"signed ad hoc, from {made_from}, in {time.time() - started:.0f} s")
    with open(os.path.join(DIST, "macos", "BUILT_FROM"), "w") as out:
        out.write(made_from + "\n")
    return True, {"app": app, "bytes": total, "packs": packed, "files": files, "commit": made_from}
