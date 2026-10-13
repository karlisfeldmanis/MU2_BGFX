"""Where the repository's folders are: one root, found once, for every Python script.

docs/layout.md step 1. Every script in tools/, tools/mu/ and pipeline/ takes its folders from
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

# What we author: the recipes, the Cooker's truth (content/ in docs/layout.md; source/ today).
CONTENT = os.path.join(ROOT, "source")
# The hand-tuned sheets: lighting, time, lobby, worlds, materials.
SHEETS = os.path.join(ROOT, "sheets")
# The pipeline's own code and mu's.
PIPELINE = os.path.join(ROOT, "pipeline")
TOOLS = os.path.join(ROOT, "tools")

# Generated: deleting any of these costs a rebuild or a cook, nothing else.
WORKSHOP = os.path.join(ROOT, "workshop")      # the pipeline's output, built from CONTENT
ASSETS = os.path.join(ROOT, "assets")          # what the game reads, synced from WORKSHOP
COOKED = os.path.join(ASSETS, "cooked")        # the cook's binaries
BUILD = os.path.join(ROOT, "build")            # the release build: mu2, texcook
BUILD_DEBUG = os.path.join(ROOT, "build-debug")
SHOTS = os.path.join(ROOT, "shots")
SAVES = os.path.join(ROOT, "saves")
CRASHES = os.path.join(ROOT, "crashes")

# Fetched, never ours.
EXTERN = os.path.join(ROOT, "extern")
PATCHES = os.path.join(ROOT, "patches")
WEIGHTS = os.path.join(ROOT, "weights")        # the upscaler's models
DOWNLOADS = os.path.join(ROOT, "downloads")


class P:
    """The same folders as pathlib.Path."""
    ROOT = Path(ROOT)
    CONTENT = Path(CONTENT)
    SHEETS = Path(SHEETS)
    PIPELINE = Path(PIPELINE)
    TOOLS = Path(TOOLS)
    WORKSHOP = Path(WORKSHOP)
    ASSETS = Path(ASSETS)
    COOKED = Path(COOKED)
    BUILD = Path(BUILD)
    BUILD_DEBUG = Path(BUILD_DEBUG)
    SHOTS = Path(SHOTS)
    SAVES = Path(SAVES)
    CRASHES = Path(CRASHES)
    EXTERN = Path(EXTERN)
    PATCHES = Path(PATCHES)
    WEIGHTS = Path(WEIGHTS)
    DOWNLOADS = Path(DOWNLOADS)
