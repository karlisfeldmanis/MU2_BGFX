#!/usr/bin/env bash
#
# Builds workshop/ from source/ -- every recipe, every world's ground, every grade, then the
# index -- which is what MU2 did by hand, one mu2.sh call and one pipeline script at a time.
#
#   ./tools/content.sh                    everything that is stale
#   ./tools/content.sh --world lorencia   one world's ground and objects, plus everything that
#                                         is not a world object (players, items, monsters...)
#   ./tools/content.sh --rebuild          all of it, whatever the dates say
#
# Then ./tools/sync.sh and ./tools/cook.py, as before. A full build from nothing is hours:
# every sheet goes through the upscaler and every model through Blender.
#
# The chain from MU's own files to source/ -- MuExtract for .bmd, decode_texture.py for
# .OZJ/.OZT, terrain.py for a World folder -- is the import step and is not re-run here: its
# results are source/, which is kept. docs/content.md says how to run it for a new asset.

set -uo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$root/source"
workshop="$root/workshop"
pipeline="$root/pipeline"

worlds=()
forced=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --world) worlds+=("$2"); shift 2 ;;
    --rebuild) forced=(--rebuild); shift ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [[ ${#worlds[@]} -eq 0 ]]; then
  for w in "$source_dir"/world/*/ground.json; do worlds+=("$(basename "$(dirname "$w")")"); done
fi

failed=()
mkdir -p "$workshop/grades"

echo "=== grades"
for g in "$source_dir"/grades/*.json; do
  python3 "$pipeline/grade.py" "$g" "$workshop/grades" || failed+=("grade $(basename "$g")")
done

for w in "${worlds[@]}"; do
  echo "=== ground: $w"
  mkdir -p "$workshop/world/$w"
  python3 "$pipeline/tileset.py" "$source_dir/world/$w/ground.json" "$workshop/world/$w" \
    || failed+=("tileset $w")
  python3 "$pipeline/ground.py" "$source_dir/world/$w" "$workshop/world/$w" \
    "$source_dir/textures" || failed+=("ground $w")
done

# Every recipe: a json that says it is an item or an assembly of parts. A world's objects
# are named with their world, because Object01 is a name several worlds use.
echo "=== assets"
while IFS= read -r recipe; do
  rel="${recipe#"$source_dir"/}"
  case "$rel" in
    world/*)
      w="$(cut -d/ -f2 <<<"$rel")"
      printf '%s\n' "${worlds[@]}" | grep -qx "$w" || continue
      name="$w/$(basename "$recipe" .json)"
      ;;
    *) name="$recipe" ;;
  esac
  blocked="$(python3 -c "
import json,sys
print(json.load(open(sys.argv[1])).get('blocked',''))" "$recipe" 2>/dev/null)"
  if [[ -n $blocked ]]; then
    echo "--- skipped $rel: blocked"
    continue
  fi
  echo "--- $rel"
  "$root/tools/asset.sh" "$name" --build-only "${forced[@]+"${forced[@]}"}" \
    || failed+=("$rel")
done < <(python3 - "$source_dir" <<'PY'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
for p in sorted(root.glob("*/*.json")) + sorted(root.glob("*/*/*.json")):
    if p.name.endswith(".rig.json"):
        continue
    try:
        d = json.loads(p.read_text())
    except (OSError, ValueError):
        continue
    if isinstance(d, dict) and ("item" in d or "parts" in d):
        print(p)
PY
)

echo "=== index"
python3 "$pipeline/index.py" "$source_dir" "$workshop" || failed+=("index")

if [[ ${#failed[@]} -gt 0 ]]; then
  echo
  echo "${#failed[@]} step(s) failed:"
  printf '  %s\n' "${failed[@]}"
  exit 1
fi
echo "workshop is built; now ./tools/sync.sh"
