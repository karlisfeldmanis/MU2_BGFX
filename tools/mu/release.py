"""`mu release windows`: the client `mu dist windows` made, put on the box for the launcher
(docs/plans/launcher.md, L2).

  1. What dist made: dist/windows/MU2/, its BUILT_FROM commit, and the protocol that commit speaks.
  2. Every file hashed (SHA-256). On the box a file is kept under its hash,
     /srv/mu2-web/game/windows/files/<sha256>, so one that did not change between releases is never
     sent again and never kept twice.
  3. Only what the box lacks travels (rsync, resumable), hard-linked under its hash in
     out/release/windows/ first so nothing is copied here.
  4. The manifest last: manifests/<commit>.json, then moved over manifest.json in one rename, so a
     launcher reading it mid-release sees the old release whole or the new one whole.
  5. Files no longer named by the newest KEEP manifests are deleted.
  6. Checked over HTTPS as a launcher will read it: the manifest, and one file's bytes.

The manifest:
  {"platform": "windows", "version": "<commit>", "built": "<date>", "protocol": 31, "exe": "MU2.exe",
   "bytes": N, "files": [{"path": "MU2.exe", "size": N, "sha256": "..."}, ...]}
served at https://37.27.158.226/game/windows/manifest.json, each file at .../files/<sha256>.
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
SITE = "https://37.27.158.226"
REMOTE = "/srv/mu2-web/game"
KEEP = 2                         # releases whose files stay on the box
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


def release(target, say, dry=False):
    if target != "windows":
        raise RuntimeError("release: windows only for now (docs/plans/launcher.md; the Mac is L5)")
    started = time.time()
    client = os.path.join(ROOT, "dist", "windows", "MU2")
    built_from = os.path.join(client, "BUILT_FROM")
    if not os.path.isfile(built_from):
        raise RuntimeError(f"no {built_from}: run `./mu dist windows` first")
    commit, _, built = open(built_from).read().strip().partition(" ")
    protocol = protocol_of(commit)
    say(f"dist/windows/MU2 from {commit} ({built or 'undated'}), protocol {protocol}")

    files = []
    for folder, dirs, names in os.walk(client):
        rel_folder = os.path.relpath(folder, client)
        if rel_folder == "logs" or rel_folder.startswith("logs" + os.sep):
            continue
        dirs[:] = [d for d in dirs if not (rel_folder == "." and d == "logs")]
        for name in sorted(names):
            rel = os.path.normpath(os.path.join(rel_folder, name)).replace(os.sep, "/")
            if rel in LEFT_OUT or name.startswith("."):
                continue
            path = os.path.join(folder, name)
            files.append({"path": rel, "size": os.path.getsize(path), "sha256": sha256(path), "local": path})
    files.sort(key=lambda f: f["path"])
    total = sum(f["size"] for f in files)
    say(f"{len(files)} files, {total / 1e9:.2f} GB, hashed")

    root = f"{REMOTE}/{target}"
    ssh(f"mkdir -p {root}/files {root}/manifests")
    have = set(ssh(f"ls {root}/files").split())
    missing = [f for f in files if f["sha256"] not in have]
    sending = sum(f["size"] for f in missing)
    say(f"the box has {len(files) - len(missing)} of them; {len(missing)} to send, {sending / 1e9:.2f} GB")

    manifest = {
        "platform": target, "version": commit, "built": built, "protocol": protocol, "exe": "MU2.exe",
        "bytes": total, "files": [{k: f[k] for k in ("path", "size", "sha256")} for f in files],
    }
    if dry:
        say("dry run: nothing sent")
        return True, {"commit": commit, "protocol": protocol, "files": len(files), "send": len(missing),
                      "send_bytes": sending}

    stage = os.path.join(OUT, "release", target)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "files"))
    for f in missing:
        os.link(f["local"], os.path.join(stage, "files", f["sha256"]))
    if missing:
        say(f"sending {len(missing)} files (rsync, resumable)")
        subprocess.run(["rsync", "-a", "--partial", "--chmod=F644", "--progress",
                        os.path.join(stage, "files") + "/", f"{HOST}:{root}/files/"], check=True)

    named = os.path.join(stage, f"{commit}.json")
    with open(named, "w") as out:
        json.dump(manifest, out, indent=1)
    subprocess.run(["rsync", "-a", "--chmod=F644", named, f"{HOST}:{root}/manifests/"], check=True)
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
