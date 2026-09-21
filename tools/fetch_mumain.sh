#!/bin/zsh
# Fetches MU's own data -- the files everything in source/ was imported from -- into
# reference/MuMain. Only needed to import a new asset (docs/content.md); building workshop/
# from source/ does not read it. About 1.1 GB.
#
# Pinned to the commit the import was made from, for the reason bootstrap.sh pins bgfx.
set -e
cd "$(dirname "$0")/.."
MUMAIN_REV=c0d74c4ed5edfad527b71899337f72d39bfc49f3
if [ ! -d reference/MuMain/.git ]; then
  mkdir -p reference
  git clone https://github.com/sven-n/MuMain.git reference/MuMain
fi
git -C reference/MuMain checkout --quiet $MUMAIN_REV
echo "MuMain at $MUMAIN_REV: reference/MuMain/src/bin/Data"
