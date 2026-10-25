"""The Armory's C++ half: builds tools/armory/dump (target armory_dump) against a snapshot of the
release's tree and runs it on the release's packs. docs/plans/armory.md.

    from dump import run
    run(snapshot_dir, build_dir, data_dir, out_dir, commit, protocol)

The dump's sources are this checkout's, copied into the snapshot, so an older release is read by
today's dump; a snapshot whose CMakeLists.txt does not include tools/armory/dump/armory.cmake yet
is handed it as CMAKE_PROJECT_mu2_INCLUDE. Built niced, at most six jobs, in build_dir/<snapshot>/:
one build folder a snapshot, its CMakeCache checked to be that snapshot's, since a CMake build folder
keeps the source tree it was first configured with (4b0ad87e's Armory was built by 5d7ddebc's dump
that way). After the run, every rune's line the dump wrote must be a literal of the snapshot's
src/sim/items.cpp, or the run fails: the dump's rules are the snapshot's own.
"""
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCES = HERE / "dump"
JOBS = 6


def _run(cmd, cwd=None):
    done = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"{' '.join(map(str, cmd))} failed:\n{done.stdout[-4000:]}\n{done.stderr[-4000:]}")
    return done.stdout


def home_of(build_dir):
    """The source tree a configured build folder builds (CMakeCache's CMAKE_HOME_DIRECTORY), or None."""
    cache = Path(build_dir) / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:"):
            return Path(line.split("=", 1)[1].strip()).resolve()
    return None


def build(snapshot_dir, build_dir):
    """Configures (once a snapshot) and builds armory_dump in build_dir/<snapshot>/; returns the
    binary's path. Stops when that folder builds another tree."""
    snapshot_dir = Path(snapshot_dir).resolve()
    build_dir = Path(build_dir).resolve() / snapshot_dir.name
    dest = snapshot_dir / "tools" / "armory" / "dump"
    if dest.resolve() != SOURCES.resolve():
        dest.mkdir(parents=True, exist_ok=True)
        for f in SOURCES.iterdir():
            if f.is_file() and (not (dest / f.name).exists() or (dest / f.name).read_bytes() != f.read_bytes()):
                shutil.copy2(f, dest / f.name)
    cmake = ["cmake", "-S", str(snapshot_dir), "-B", str(build_dir), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    if "tools/armory/dump/armory.cmake" not in (snapshot_dir / "CMakeLists.txt").read_text():
        cmake.append(f"-DCMAKE_PROJECT_mu2_INCLUDE={dest / 'armory.cmake'}")
    home = home_of(build_dir)
    if home is not None and home != snapshot_dir:
        raise RuntimeError(f"{build_dir} builds {home}, not the snapshot {snapshot_dir}: error, its dump would "
                           f"be another release's (remove the folder)")
    if not (build_dir / "build.ninja").exists():
        build_dir.mkdir(parents=True, exist_ok=True)
        _run(cmake)
    if home_of(build_dir) != snapshot_dir:
        raise RuntimeError(f"error: {build_dir} is configured for {home_of(build_dir)}, not {snapshot_dir}")
    _run(["nice", "cmake", "--build", str(build_dir), "--target", "armory_dump", "-j", str(JOBS)])
    return build_dir / "armory_dump"


def check_snapshot(snapshot_dir, out_dir):
    """Every rune's line in rules.json is a literal of the snapshot's src/sim/items.cpp (PowerRow's
    `tells`): a dump compiled from another tree says another release's words. Raises when not."""
    import json
    rules = Path(out_dir) / "rules.json"
    if not rules.is_file():
        return
    text = "".join((Path(snapshot_dir) / "src/sim" / f).read_text(errors="replace")
                   for f in ("items.cpp", "items.h") if (Path(snapshot_dir) / "src/sim" / f).is_file())
    runes = [r for r in json.loads(rules.read_text()).get("runes") or [] if isinstance(r, dict) and r.get("tells")]
    foreign = [r["tells"] for r in runes if json.dumps(r["tells"])[1:-1] not in text and r["tells"] not in text]
    if foreign:
        raise RuntimeError(f"error: {len(foreign)} of {len(runes)} rune lines are not the snapshot's "
                           f"({foreign[0]!r}...): the dump was built from another tree")


def run(snapshot_dir, build_dir, data_dir, out_dir, commit, protocol, check_odds=False, assets=False):
    """Builds the dump and writes cards, skills, talents, gear_powers, drops and rules .json into
    out_dir. data_dir is the release's .mupak folder (or, assets=True, an out/assets folder).
    Returns the dump's printout (its spot checks, and the odds check's when asked)."""
    binary = build(snapshot_dir, build_dir)
    os.makedirs(out_dir, exist_ok=True)
    cmd = ["nice", str(binary), "--assets" if assets else "--data", str(data_dir), "--out", str(out_dir),
           "--commit", str(commit), "--protocol", str(protocol)]
    if check_odds:
        cmd.append("--check-odds")
    said = _run(cmd)
    check_snapshot(snapshot_dir, out_dir)
    return said


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("snapshot")
    p.add_argument("build")
    p.add_argument("data")
    p.add_argument("out")
    p.add_argument("--commit", default="")
    p.add_argument("--protocol", default="")
    p.add_argument("--check-odds", action="store_true")
    p.add_argument("--assets", action="store_true")
    a = p.parse_args()
    print(run(a.snapshot, a.build, a.data, a.out, a.commit, a.protocol, a.check_odds, a.assets), end="")
