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

CONTENT="$ROOT/source"
SHEETS="$ROOT/sheets"
PIPELINE="$ROOT/pipeline"
TOOLS="$ROOT/tools"

WORKSHOP="$ROOT/workshop"
ASSETS="$ROOT/assets"
COOKED="$ASSETS/cooked"
BUILD="$ROOT/build"
BUILD_DEBUG="$ROOT/build-debug"
SHOTS="$ROOT/shots"
SAVES="$ROOT/saves"
CRASHES="$ROOT/crashes"

EXTERN="$ROOT/extern"
PATCHES="$ROOT/patches"
WEIGHTS="$ROOT/weights"
DOWNLOADS="$ROOT/downloads"
