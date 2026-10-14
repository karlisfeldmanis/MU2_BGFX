"""Where the repository's folders are: one root, found once, for every Python script.

docs/plans/layout.md step 1. Every script in tools/, tools/mu/ and pipeline/ takes its folders from
here rather than joining "source" or "assets" onto a root of its own, so that moving a folder
(steps 2-4) is a change to this file. tools/paths.sh says the same for the shell scripts.

Two flavours of the same names: the plain ones are str, for os.path code; `P` holds them as
pathlib.Path, for code written that way.

    from paths import CONTENT, WORKSHOP            # str
    from paths import P                            # P.CONTENT / "interface"

A script outside tools/ puts tools/ on sys.path first (appended, so it shadows nothing):

    sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))

That works under Blender's Python too (`blender --python`), which adds no folder of its own.
"""

import os
from pathlib import Path

# This file is tools/paths.py; the root is the folder above tools/.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# What we author: the recipes, the Cooker's truth (content/ in docs/plans/layout.md; content/ today).
CONTENT = os.path.join(ROOT, "content")
# The hand-tuned sheets: lighting, time, lobby, worlds, materials.
SHEETS = os.path.join(CONTENT, "sheets")
# The pipeline's own code and mu's.
PIPELINE = os.path.join(ROOT, "pipeline")
TOOLS = os.path.join(ROOT, "tools")

# Generated: deleting any of these costs a rebuild or a cook, nothing else.
OUT = os.path.join(ROOT, "out")              # generated: delete it and rebuild
WORKSHOP = os.path.join(OUT, "workshop")      # the pipeline's output, built from CONTENT
ASSETS = os.path.join(OUT, "assets")          # what the game reads, synced from WORKSHOP
COOKED = os.path.join(ASSETS, "cooked")        # the cook's binaries
BUILD = os.path.join(OUT, "build")            # the release build: mu2, texcook
BUILD_DEBUG = os.path.join(OUT, "build-debug")
SHOTS = os.path.join(OUT, "shots")
SAVES = os.path.join(OUT, "saves")
CRASHES = os.path.join(OUT, "logs")
LOGS = CRASHES                                 # mu2.log and the crash reports

# Fetched, never ours.
THIRD_PARTY = os.path.join(ROOT, "third_party")
EXTERN = os.path.join(THIRD_PARTY, "extern")
PATCHES = os.path.join(THIRD_PARTY, "patches")
WEIGHTS = os.path.join(THIRD_PARTY, "weights")        # the upscaler's models
DOWNLOADS = os.path.join(THIRD_PARTY, "downloads")


class P:
    """The same folders as pathlib.Path."""
    ROOT = Path(ROOT)
    CONTENT = Path(CONTENT)
    SHEETS = Path(SHEETS)
    PIPELINE = Path(PIPELINE)
    TOOLS = Path(TOOLS)
    OUT = Path(OUT)
    WORKSHOP = Path(WORKSHOP)
    ASSETS = Path(ASSETS)
    COOKED = Path(COOKED)
    BUILD = Path(BUILD)
    BUILD_DEBUG = Path(BUILD_DEBUG)
    SHOTS = Path(SHOTS)
    SAVES = Path(SAVES)
    CRASHES = Path(CRASHES)
    LOGS = Path(LOGS)
    THIRD_PARTY = Path(THIRD_PARTY)
    EXTERN = Path(EXTERN)
    PATCHES = Path(PATCHES)
    WEIGHTS = Path(WEIGHTS)
    DOWNLOADS = Path(DOWNLOADS)
