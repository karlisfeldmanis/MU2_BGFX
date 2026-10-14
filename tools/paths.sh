# Where the repository's folders are, for the shell scripts: tools/paths.py's names, said once.
# Sourced, never run, by bash and zsh alike:
#
#   . "$(dirname -- "${BASH_SOURCE[0]}")/paths.sh"     from a bash script in tools/
#   . "$(dirname "$0")/paths.sh"                       from a zsh one
#
# Plain variables, not exported: what a script runs sees the environment it always did.

# This file is tools/paths.sh; the root is the folder above tools/. bash names a sourced file
# in BASH_SOURCE, zsh in $0.
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")/.." && pwd)"

CONTENT="$ROOT/content"
SHEETS="$CONTENT/sheets"
PIPELINE="$ROOT/pipeline"
TOOLS="$ROOT/tools"

OUT="$ROOT/out"
WORKSHOP="$OUT/workshop"
ASSETS="$OUT/assets"
COOKED="$ASSETS/cooked"
BUILD="$OUT/build"
BUILD_DEBUG="$OUT/build-debug"
SHOTS="$OUT/shots"
SAVES="$OUT/saves"
CRASHES="$OUT/logs"
LOGS="$CRASHES"

THIRD_PARTY="$ROOT/third_party"
EXTERN="$THIRD_PARTY/extern"
PATCHES="$THIRD_PARTY/patches"
WEIGHTS="$THIRD_PARTY/weights"
DOWNLOADS="$THIRD_PARTY/downloads"
