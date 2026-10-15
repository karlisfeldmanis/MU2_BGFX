"""`mu release windows|macos`: the client `mu dist` made, put on the box for the launcher
(docs/plans/launcher.md, L2 and L5).

  1. What dist made: dist/windows/MU2/ or dist/macos/MU2.app, its BUILT_FROM commit, and the
     protocol that commit speaks.
  2. Every file hashed (SHA-256). On the box a file is kept under its hash,
     /srv/mu2-web/game/<platform>/files/<sha256>, so one that did not change between releases is
     never sent again and never kept twice.
  3. Only what the box lacks travels (rsync, resumable), hard-linked under its hash in
     out/release/<platform>/ first so nothing is copied here.
  4. The manifest last: manifests/<commit>.json, then moved over manifest.json in one rename, so a
     launcher reading it mid-release sees the old release whole or the new one whole.
  5. Files no longer named by the newest KEEP manifests are deleted.
  6. Checked over HTTPS as a launcher will read it: the manifest, and one file's bytes.

And first, the release's symbols (dist.py strip_keeping, out/symbols/<commit>/<platform>/) copied
to SYMBOLS on the box, which no web server serves: a crash report is read against them from any
machine, and deleting out/ here loses nothing.

The manifest:
  {"platform": "windows"|"macos", "version": "<commit>", "built": "<date>", "protocol": 31,
   "exe": "MU2.exe" | "MU2.app/Contents/MacOS/MU2", "bytes": N,
   "files": [{"path": "MU2.exe", "size": N, "sha256": "..."}, ...]}
served at https://mu-classic.com/game/<platform>/manifest.json, each file at .../files/<sha256>.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from paths import OUT, ROOT  # noqa: E402

HOST = "hetzner"                 # ~/.ssh/config: root@37.27.158.226
SITE = "https://mu-classic.com"
REMOTE = "/srv/mu2-web/game"
KEEP = 2                         # releases whose files stay on the box
# Where the game's files are kept instead of the box (the user, 2026-10-15: "i dont want to hold
# them on box server"): an FTP account on aurorarave.lv whose home is its httpdocs/muclassic, so
# a file put at /game/<platform>/files/<sha256> is served at FILES_URL/game/<platform>/files/<sha256>.
# FTPS to the name its certificate carries. The login is the user's, in FILES_NETRC (0600), never
# here. Without it, the files go to the box as before.
FILES_HOST = "webhost.dualnode.com"
FILES_URL = "https://aurorarave.lv/muclassic"
FILES_NETRC = os.path.expanduser("~/.config/mu2/ftp.netrc")
SYMBOLS = "/var/lib/mu2-symbols"  # on the box, root's alone; never under /srv
LEFT_OUT = {"BUILT_FROM"}        # dist's own note; logs/ is the player's


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def protocol_of(commit):
    """The protocol the commit's client speaks (net::kVersion)."""
    prefix = subprocess.run(["git", "rev-parse", "--show-prefix"], cwd=ROOT, capture_output=True,
                            text=True, check=True).stdout.strip()
    wire = subprocess.run(["git", "show", f"{commit}:{prefix}src/net/wire.h"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout
    found = re.search(r"constexpr uint32_t kVersion = (\d+);", wire)
    return int(found.group(1)) if found else None


def ssh(command, say=None):
    done = subprocess.run(["ssh", HOST, command], capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"ssh {HOST}: {command[:60]}...: {done.stderr.strip()}")
    return done.stdout


def keep_symbols(target, commit, say):
    """out/symbols/<commit>/<target>/ to the box's SYMBOLS, beside every release's before it."""
    kept = os.path.join(OUT, "symbols", commit, target)
    if not os.path.isdir(kept):
        say(f"no {kept}: this build's symbols were not kept (a dist from before 26bdd9cd)")
        return
    ssh(f"mkdir -p {SYMBOLS}/{commit} && chmod 700 {SYMBOLS}")
    done = subprocess.run(["rsync", "-a", kept, f"{HOST}:{SYMBOLS}/{commit}/"], capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"the symbols did not go to {HOST}: {done.stderr.strip()}")
    say(f"symbols kept on {HOST}:{SYMBOLS}/{commit}/{target}/")


def files_elsewhere():
    """Whether the files go to FILES_HOST: its login is on this machine."""
    try:
        with open(FILES_NETRC) as f:
            return f"machine {FILES_HOST}" in f.read()
    except OSError:
        return False


def ftp(path, *extra, check=True, timeout=None):
    """curl over FTPS to FILES_HOST, the login from FILES_NETRC."""
    cmd = ["curl", "-s", "-S", "--netrc-file", FILES_NETRC, "--ssl-reqd", "--ftp-create-dirs",
           f"ftp://{FILES_HOST}/{path}", *extra]
    return subprocess.run(cmd, capture_output=True, text=True, check=check, timeout=timeout)


def ftp_sizes(folder):
    """{name: size} of a folder on FILES_HOST, from its LIST; {} when there is none."""
    done = ftp(folder.rstrip("/") + "/", check=False, timeout=120)
    sizes = {}
    for line in done.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 9 and parts[4].isdigit():
            sizes[parts[-1]] = int(parts[4])
    return sizes


def ftp_rename(folder, old, new):
    """Within `folder`: curl has gone into it before a quote runs, so the names are bare."""
    moved = ftp(f"{folder}/", "-Q", f"-RNFR {old}", "-Q", f"-RNTO {new}", "-o", os.devnull,
                check=False, timeout=120)
    return moved.returncode == 0


def ftp_put(local, folder, name, say, have_part=False):
    """One file in as <name>.part, then renamed: a name on the host is only ever a whole file. A
    .part already there whole (an earlier run that died before its rename) is only renamed."""
    if have_part and ftp_rename(folder, f"{name}.part", name):
        return True
    for attempt in range(3):
        sent = ftp(f"{folder}/{name}.part", "-T", local, check=False, timeout=3600)
        if sent.returncode == 0:
            if ftp_rename(folder, f"{name}.part", name):
                return True
        say(f"  {name[:12]}: try {attempt + 1} failed: {(sent.stderr or '').strip()[:120]}")
    return False


def release(target, say, dry=False):
    if target not in ("windows", "macos"):
        raise RuntimeError("release: windows or macos (docs/plans/launcher.md, L2 and L5)")
    started = time.time()
    # What is walked: the Windows client is a folder of MU2.exe and its packs; the Mac's is the app
    # bundle, with BUILT_FROM beside it in dist/macos/.
    client = os.path.join(ROOT, "dist", target, "MU2" if target == "windows" else "MU2.app")
    # dist writes Windows' note inside the client folder (dist.py), the Mac's beside the bundle.
    built_from = os.path.join(client, "BUILT_FROM")
    if target != "windows" or not os.path.isfile(built_from):
        built_from = os.path.join(ROOT, "dist", target, "BUILT_FROM")
    if not os.path.isfile(built_from):
        raise RuntimeError(f"no {built_from}: run `./mu dist {target}` first")
    commit, _, built = open(built_from).read().strip().partition(" ")
    protocol = protocol_of(commit)
    say(f"dist/{target} from {commit} ({built or 'undated'}), protocol {protocol}")
    if not dry:
        keep_symbols(target, commit, say)
    exe_path = "MU2.exe" if target == "windows" else "MU2.app/Contents/MacOS/MU2"

    files = []
    for folder, dirs, names in os.walk(client):
        rel_folder = os.path.relpath(folder, client)
        if rel_folder == "logs" or rel_folder.startswith("logs" + os.sep):
            continue
        dirs[:] = [d for d in dirs if not (rel_folder == "." and d == "logs")]
        for name in sorted(names):
            rel = os.path.normpath(os.path.join(rel_folder, name)).replace(os.sep, "/")
            # _CodeSignature is macOS's own and cannot be moved about (its files refuse a hard
            # link); the copy a launcher fetches needs no signature -- nothing quarantines what
            # the launcher writes -- and an update could never replace it anyway.
            if rel in LEFT_OUT or name.startswith(".") or "_CodeSignature" in rel:
                continue
            path = os.path.join(folder, name)
            files.append({"path": rel, "size": os.path.getsize(path), "sha256": sha256(path), "local": path})
    files.sort(key=lambda f: f["path"])
    # The manifest speaks paths from the player's install root: MU2.exe sits at the Windows'
    # root; the Mac's install root takes the bundle, so its files carry MU2.app/ before them.
    if target == "macos":
        for f in files:
            f["path"] = "MU2.app/" + f["path"]
    total = sum(f["size"] for f in files)
    say(f"{len(files)} files, {total / 1e9:.2f} GB, hashed")

    root = f"{REMOTE}/{target}"
    ssh(f"mkdir -p {root}/files {root}/manifests")
    elsewhere = files_elsewhere()
    folder = f"game/{target}/files"
    # What the host has, by name AND size: a name alone is no proof -- the first macos upload died
    # mid-rsync and left a truncated file that the next run took for already sent.
    if elsewhere:
        sizes = ftp_sizes(folder)
        host = FILES_URL
    else:
        listed = ssh(f"cd {root}/files && stat -c '%n %s' * 2>/dev/null || true")
        sizes = {}
        for line in listed.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1].isdigit():
                sizes[parts[0]] = int(parts[1])
        host = "the box"
    missing = [f for f in files if sizes.get(f["sha256"]) != f["size"]]
    sending = sum(f["size"] for f in missing)
    say(f"{host} has {len(files) - len(missing)} of them; {len(missing)} to send, {sending / 1e9:.2f} GB")

    manifest = {
        "platform": target, "version": commit, "built": built, "protocol": protocol, "exe": exe_path,
        "bytes": total, "files": [{k: f[k] for k in ("path", "size", "sha256")} for f in files],
    }
    if elsewhere:
        # The launcher fetches each file from here, falling back to the box (engine.cpp update).
        manifest["files_url"] = f"{FILES_URL}/{folder}/"
    if dry:
        say("dry run: nothing sent")
        return True, {"commit": commit, "protocol": protocol, "files": len(files), "send": len(missing),
                      "send_bytes": sending}

    stage = os.path.join(OUT, "release", target)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "files"))
    if elsewhere:
        return release_elsewhere(target, say, commit, protocol, files, missing, sending, manifest,
                                 stage, root, folder, total, started)
    for f in missing:
        staged = os.path.join(stage, "files", f["sha256"])
        try:
            os.link(f["local"], staged)
        except OSError:
            # macOS refuses to hard-link a code signature's files; a copy does the same job, and
            # only the files it refuses ever take this path.
            shutil.copy2(f["local"], staged)
    if missing:
        say(f"sending {len(missing)} files (rsync, resumable)")
        subprocess.run(["rsync", "-a", "--partial", "--progress",
                        os.path.join(stage, "files") + "/", f"{HOST}:{root}/files/"], check=True)

    named = os.path.join(stage, f"{commit}.json")
    with open(named, "w") as out:
        json.dump(manifest, out, indent=1)
    subprocess.run(["rsync", "-a", named, f"{HOST}:{root}/manifests/"], check=True)
    # Caddy's own user reads them; the Mac's openrsync takes no --chmod=F644.
    ssh(f"chmod -R a+rX {root}")
    ssh(f"cp {root}/manifests/{commit}.json {root}/manifest.json.new && mv {root}/manifest.json.new {root}/manifest.json")
    say(f"manifest.json is {commit}")

    # The newest KEEP releases' files stay; any other goes.
    pruned = ssh(
        "python3 - <<'EOF'\n"
        "import json, os\n"
        f"root = '{root}'\n"
        "ms = sorted((os.path.join(root, 'manifests', n) for n in os.listdir(os.path.join(root, 'manifests'))),"
        " key=os.path.getmtime, reverse=True)\n"
        f"keep = set()\n"
        f"for m in ms[:{KEEP}]:\n"
        "    keep |= {f['sha256'] for f in json.load(open(m))['files']}\n"
        "gone = 0\n"
        "for n in os.listdir(os.path.join(root, 'files')):\n"
        "    if n not in keep:\n"
        "        os.remove(os.path.join(root, 'files', n)); gone += 1\n"
        f"for m in ms[{KEEP}:]:\n"
        "    os.remove(m)\n"
        "print(gone)\n"
        "EOF").strip()
    say(f"{pruned} files of older releases removed; the newest {KEEP} kept")

    # Every file the box holds, size against the manifest, before anything is declared done.
    listed = ssh(f"cd {root}/files && stat -c '%n %s' * 2>/dev/null || true")
    on_box = {}
    for line in listed.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].isdigit():
            on_box[parts[0]] = int(parts[1])
    short = [f for f in files if on_box.get(f["sha256"]) != f["size"]]
    if short:
        raise RuntimeError(f"{len(short)} files on the box are missing or truncated, e.g. {short[0]['path']}")

    # As the launcher will read it.
    got = subprocess.run(["curl", "-sf", f"{SITE}/game/{target}/manifest.json"], capture_output=True, text=True)
    if got.returncode != 0 or json.loads(got.stdout).get("version") != commit:
        raise RuntimeError(f"{SITE}/game/{target}/manifest.json does not say {commit}")
    exe = next(f for f in files if f["path"] == manifest["exe"])
    head = subprocess.run(["curl", "-sfI", f"{SITE}/game/{target}/files/{exe['sha256']}"], capture_output=True, text=True)
    length = re.search(r"(?im)^content-length:\s*(\d+)", head.stdout)
    if not length or int(length.group(1)) != exe["size"]:
        raise RuntimeError(f"{SITE}/game/{target}/files/{exe['sha256']} is not MU2.exe's {exe['size']} bytes")
    shutil.rmtree(stage, ignore_errors=True)
    say(f"{SITE}/game/{target}/manifest.json: {commit}, protocol {protocol}, {total / 1e9:.2f} GB, "
        f"in {time.time() - started:.0f} s")
    return True, {"commit": commit, "protocol": protocol, "files": len(files), "sent": len(missing),
                  "sent_bytes": sending, "manifest": f"{SITE}/game/{target}/manifest.json"}


def release_elsewhere(target, say, commit, protocol, files, missing, sending, manifest, stage, root,
                      folder, total, started):
    """The files to FILES_HOST over FTPS, three at a time, each whole before it has its name; every
    one checked there by size, the exe read over HTTPS; then the manifest on the box, last, in one
    rename; then what no kept release names is pruned there. The box's own copies are left alone:
    they are the launcher's fallback until they are removed on purpose."""
    from concurrent.futures import ThreadPoolExecutor
    if missing:
        say(f"sending {len(missing)} files to {FILES_HOST} (FTPS, 3 at a time)")
        done = [0]
        parts = ftp_sizes(folder)
        def put(f):
            ok = ftp_put(f["local"], folder, f["sha256"], say,
                         have_part=parts.get(f["sha256"] + ".part") == f["size"])
            done[0] += 1
            if done[0] % 5 == 0 or done[0] == len(missing):
                say(f"  {done[0]} of {len(missing)}")
            return ok
        with ThreadPoolExecutor(max_workers=3) as pool:
            failed = [f for f, ok in zip(missing, pool.map(put, missing)) if not ok]
        if failed:
            raise RuntimeError(f"{len(failed)} files did not reach {FILES_HOST}, e.g. {failed[0]['path']}")
    sizes = ftp_sizes(folder)
    short = [f for f in files if sizes.get(f["sha256"]) != f["size"]]
    if short:
        raise RuntimeError(f"{len(short)} files on {FILES_HOST} are missing or truncated, e.g. {short[0]['path']}")
    exe = next(f for f in files if f["path"] == manifest["exe"])
    head = subprocess.run(["curl", "-sfI", f"{manifest['files_url']}{exe['sha256']}"], capture_output=True, text=True)
    length = re.search(r"(?im)^content-length:\s*(\d+)", head.stdout)
    if not length or int(length.group(1)) != exe["size"]:
        raise RuntimeError(f"{manifest['files_url']}{exe['sha256']} is not {manifest['exe']}'s {exe['size']} bytes")

    named = os.path.join(stage, f"{commit}.json")
    with open(named, "w") as out:
        json.dump(manifest, out, indent=1)
    subprocess.run(["rsync", "-a", named, f"{HOST}:{root}/manifests/"], check=True)
    ssh(f"chmod -R a+rX {root}")
    ssh(f"cp {root}/manifests/{commit}.json {root}/manifest.json.new && mv {root}/manifest.json.new {root}/manifest.json")
    say(f"manifest.json is {commit}, its files at {manifest['files_url']}")

    # What the newest KEEP manifests name stays there; anything else goes.
    keep = set(json.loads(ssh(
        "python3 - <<'EOF'\n"
        "import json, os\n"
        f"root = '{root}'\n"
        "ms = sorted((os.path.join(root, 'manifests', n) for n in os.listdir(os.path.join(root, 'manifests'))),"
        " key=os.path.getmtime, reverse=True)\n"
        "keep = set()\n"
        f"for m in ms[:{KEEP}]:\n"
        "    keep |= {f['sha256'] for f in json.load(open(m))['files']}\n"
        "print(json.dumps(sorted(keep)))\n"
        "EOF")))
    gone = [n for n in ftp_sizes(folder) if n not in keep]
    for n in gone:
        ftp(f"{folder}/", "-Q", f"-DELE {n}", "-o", os.devnull, check=False, timeout=120)
    say(f"{len(gone)} files of older releases removed from {FILES_HOST}; the newest {KEEP} kept")

    got = subprocess.run(["curl", "-sf", f"{SITE}/game/{target}/manifest.json"], capture_output=True, text=True)
    if got.returncode != 0 or json.loads(got.stdout).get("version") != commit:
        raise RuntimeError(f"{SITE}/game/{target}/manifest.json does not say {commit}")
    shutil.rmtree(stage, ignore_errors=True)
    say(f"{SITE}/game/{target}/manifest.json: {commit}, protocol {protocol}, {total / 1e9:.2f} GB at "
        f"{FILES_URL}, in {time.time() - started:.0f} s")
    return True, {"commit": commit, "protocol": protocol, "files": len(files), "sent": len(missing),
                  "sent_bytes": sending, "manifest": f"{SITE}/game/{target}/manifest.json",
                  "files_url": manifest["files_url"]}


# ---- the launcher ---------------------------------------------------------------------------------

DOWNLOADS = "/srv/mu2-web/downloads"
LAUNCHER_FILES = ["MU2Launcher.exe", "MU2.dmg", "MU2Launcher.tar.gz", "install-macos.sh", "MU2Install.command"]


def release_launcher(say, dry=False):
    """`mu release launcher`: what `mu dist launcher` made in dist/launcher, published for the
    launchers already on players' machines to replace themselves with (src/launcher/self_update.h),
    and for the site's buttons.

    For each platform, /srv/mu2-web/game/launcher-<platform>/:
      files/<sha256>   the package: MU2Launcher.exe itself; the Mac's MU2Launcher.tar.gz
      manifest.json    {"platform", "version", "built", "exe_sha256", "package": {"sha256", "size"}}
    exe_sha256 is the program's own hash, which is how a launcher knows it is current. As for the
    game: the package first, the manifest last in one rename, the previous package kept. Then the
    download page's copies, each in by a rename, and all of it checked over HTTPS."""
    started = time.time()
    stage = os.path.join(ROOT, "dist", "launcher")
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True,
                            text=True, check=True).stdout.strip()
    built = time.strftime("%Y-%m-%d")
    plans = [
        ("windows", os.path.join(stage, "MU2Launcher.exe"), os.path.join(stage, "MU2Launcher.exe")),
        ("macos", os.path.join(stage, "MU2Launcher.tar.gz"),
         os.path.join(stage, "MU2Launcher.app", "Contents", "MacOS", "MU2Launcher")),
    ]
    for name in LAUNCHER_FILES:
        if not os.path.isfile(os.path.join(stage, name)):
            raise RuntimeError(f"no dist/launcher/{name}: run `./mu dist launcher` first")
    made = {}
    for platform, package, exe in plans:
        manifest = {"platform": platform, "version": commit, "built": built,
                    "exe_sha256": sha256(exe),
                    "package": {"sha256": sha256(package), "size": os.path.getsize(package)}}
        made[platform] = manifest
        say(f"launcher {platform}: {commit}, program {manifest['exe_sha256'][:12]}, "
            f"package {manifest['package']['size'] / 1e6:.1f} MB")
    if dry:
        say("dry run: nothing sent")
        return True, {"commit": commit, "manifests": made}

    for platform, package, _ in plans:
        manifest = made[platform]
        root = f"{REMOTE}/launcher-{platform}"
        sha = manifest["package"]["sha256"]
        ssh(f"mkdir -p {root}/files")
        subprocess.run(["rsync", "-a", "--partial", package, f"{HOST}:{root}/files/{sha}.part"], check=True)
        ssh(f"mv {root}/files/{sha}.part {root}/files/{sha}")
        local = os.path.join(OUT, "release", f"launcher-{platform}.json")
        os.makedirs(os.path.dirname(local), exist_ok=True)
        with open(local, "w") as out:
            json.dump(manifest, out, indent=1)
        subprocess.run(["rsync", "-a", local, f"{HOST}:{root}/manifest.json.new"], check=True)
        ssh(f"chmod -R a+rX {root} && mv {root}/manifest.json.new {root}/manifest.json")
        # This package and the one before it stay: a launcher mid-download finishes its file.
        ssh(f"cd {root}/files && ls -t | grep -v '\\.part$' | tail -n +3 | xargs -r rm -f")
        say(f"launcher-{platform}/manifest.json is {commit}")

    # The site's buttons serve the same launcher.
    ssh(f"mkdir -p {DOWNLOADS}/.incoming")
    subprocess.run(["rsync", "-a"] + [os.path.join(stage, n) for n in LAUNCHER_FILES]
                   + [f"{HOST}:{DOWNLOADS}/.incoming/"], check=True)
    ssh(f"cd {DOWNLOADS} && for f in {' '.join(LAUNCHER_FILES)}; do mv -f .incoming/$f $f; done && "
        f"rmdir .incoming && chmod a+r {' '.join(LAUNCHER_FILES)}")
    sums = ssh(f"cd {DOWNLOADS} && sha256sum {' '.join(LAUNCHER_FILES)}")
    for line in sums.splitlines():
        digest, name = line.split()
        if digest != sha256(os.path.join(stage, name)):
            raise RuntimeError(f"downloads/{name} on the box is not dist/launcher's")
    say(f"downloads: {', '.join(LAUNCHER_FILES)}")

    # As a launcher will read it.
    for platform, _, _ in plans:
        manifest = made[platform]
        got = subprocess.run(["curl", "-sf", f"{SITE}/game/launcher-{platform}/manifest.json"],
                             capture_output=True, text=True)
        if got.returncode != 0 or json.loads(got.stdout).get("exe_sha256") != manifest["exe_sha256"]:
            raise RuntimeError(f"{SITE}/game/launcher-{platform}/manifest.json is not this release")
        url = f"{SITE}/game/launcher-{platform}/files/{manifest['package']['sha256']}"
        head = subprocess.run(["curl", "-sfI", url], capture_output=True, text=True)
        length = re.search(r"(?im)^content-length:\s*(\d+)", head.stdout)
        if not length or int(length.group(1)) != manifest["package"]["size"]:
            raise RuntimeError(f"{url} is not the package's {manifest['package']['size']} bytes")
    say(f"launcher {commit} published for windows and macos, in {time.time() - started:.0f} s")
    return True, {"commit": commit, "manifests": made}
