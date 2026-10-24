"""The Armory's C++ half: builds tools/armory/dump (target armory_dump) against a snapshot of the
release's tree and runs it on the release's packs. docs/plans/armory.md.

    from dump import run
    run(snapshot_dir, build_dir, data_dir, out_dir, commit, protocol)

The dump's sources are this checkout's, copied into the snapshot, so an older release is read by
today's dump; a snapshot whose CMakeLists.txt does not include tools/armory/dump/armory.cmake yet
is handed it as CMAKE_PROJECT_mu2_INCLUDE. Built niced, at most six jobs, in build_dir alone.
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


def build(snapshot_dir, build_dir):
    """Configures (once) and builds armory_dump; returns the binary's path."""
    snapshot_dir, build_dir = Path(snapshot_dir).resolve(), Path(build_dir).resolve()
    dest = snapshot_dir / "tools" / "armory" / "dump"
    if dest.resolve() != SOURCES.resolve():
        dest.mkdir(parents=True, exist_ok=True)
        for f in SOURCES.iterdir():
            if f.is_file() and (not (dest / f.name).exists() or (dest / f.name).read_bytes() != f.read_bytes()):
                shutil.copy2(f, dest / f.name)
    cmake = ["cmake", "-S", str(snapshot_dir), "-B", str(build_dir), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    if "tools/armory/dump/armory.cmake" not in (snapshot_dir / "CMakeLists.txt").read_text():
        cmake.append(f"-DCMAKE_PROJECT_mu2_INCLUDE={dest / 'armory.cmake'}")
    if not (build_dir / "build.ninja").exists():
        build_dir.mkdir(parents=True, exist_ok=True)
        _run(cmake)
    _run(["nice", "cmake", "--build", str(build_dir), "--target", "armory_dump", "-j", str(JOBS)])
    return build_dir / "armory_dump"


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
    return _run(cmd)


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
