#!/bin/zsh
# The content command: make, check, where, shot, compare. `./mu --help`; tools/mu/__main__.py.
exec python3 "$(dirname "$0")/tools/mu" "$@"
