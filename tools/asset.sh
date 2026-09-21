#!/usr/bin/env bash
#
# Builds one asset from its recipe under source/ into workshop/, the way MU2's mu2.sh did,
# without Godot and without a viewer. workshop/ is to this tree what MU2/build was to MU2:
# the pipeline's output, which tools/sync.sh copies into assets/ and tools/cook.py cooks.
#
#   ./tools/asset.sh Sword01              build what is stale
#   ./tools/asset.sh Sword01 --rebuild    rebuild the lot, whatever the dates say
#   ./tools/asset.sh lorencia/Object02    a world object, named with its world
#
# Ported from MU2/mu2.sh on 2026-09-21, which carries the history of every rule below. Two
# things differ: the clips are left as the .actions.glb export_actions writes (the .res Godot
# packed from it is never read by the cook), and nothing opens once the build is done.
#
# Needs Blender (BLENDER, or /Applications/Blender.app) and python3 with numpy, Pillow and
# torch. The upscaler weights are fetched into weights/ on first use.

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
assets="$root/source"
workshop="$root/workshop"
pipeline="$root/pipeline"
blender="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"

# Where an asset lives, found by name rather than by being told its kind.
#
# assets/<kind>/<category>/<Name>.json is the shape — items/weapons/Sword01.json,
# items/armor/HelmMale05.json — and neither the kind nor the category is something the
# caller should have to repeat on a command line. The names are MU's own, deliberately: a
# file called HelmMale05 is unambiguous, where a friendlier name is a chance to be wrong.
# That set was very nearly called plate. It is Bone; Plate is file 10.
find_asset() {
  local name="$1" candidate
  shopt -s nullglob

  # A path, or a name with its folder in front of it — "noria/Object06".
  #
  # Because a bare name stopped being unique the moment a second numbered world arrived.
  # Only Lorencia names its objects; Noria and the character scene both run Object01 upward,
  # and they share seven names. The loop below takes the first glob match, which is
  # alphabetical, so `mu2.sh Object06` meant charscene's and would quietly have rebuilt the
  # wrong model while reporting the right name. Naming the folder is how you say which, and
  # an outright path works too so that a shell's tab-completion is a usable way to ask.
  if [[ $name == *.json && -f $name ]]; then
    echo "$name"
    return 0
  fi

  if [[ $name == */* ]]; then
    for candidate in "$assets"/*/"$name".json "$assets"/"$name".json; do
      if [[ -f $candidate ]]; then
        echo "$candidate"
        return 0
      fi
    done
    return 1
  fi

  for candidate in "$assets"/*/"$name".json "$assets"/*/*/"$name".json; do
    [[ $candidate == *.rig.json ]] && continue
    if [[ -f $candidate ]]; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

item="${1:-}"
if [[ -z $item ]]; then
  echo "usage: tools/asset.sh <Name> [--rebuild]" >&2
  exit 1
fi
shift

if ! asset="$(find_asset "$item")"; then
  echo "error: no asset called '$item' under $assets" >&2
  echo "       the recipes are source/<kind>/<category>/<Name>.json" >&2
  exit 1
fi

source_dir="$(dirname "$asset")"
category="$(basename "$source_dir")"
kind="$(basename "$(dirname "$source_dir")")"

# The asset's own name, which is not always what was typed to find it.
#
# A world's objects are asked for as "noria/Object02" now, because a bare Object02 is
# ambiguous across two numbered worlds — and that folder belongs in the *question*, not in
# the answer. Left as typed it became part of every path built from here and the export
# went looking for build/world/noria/noria/Object02. The name is the file's stem.
item="$(basename "$asset" .json)"

# An asset that cannot be built yet says so here, plainly, rather than failing four steps
# later with something about a missing sheet. The field was written when the chain took
# one sheet per asset and every armour part wore two to four; the chain has taken every
# sheet an asset names since the Plate Helm's three, and the Pad, Leather and Vine parts
# built through it with two and three. What still carries the field is the Bone set, whose
# helm names the player's own skin sheet - and whether that is a real block or a stale one
# has not been re-tried. A `blocked` is a claim, and this only repeats it.
blocked="$(python3 -c "
import json,sys
print(json.load(open(sys.argv[1])).get('blocked',''))" "$asset" 2>/dev/null || true)"

if [[ -n $blocked ]]; then
  echo "error: $item cannot be built yet." >&2
  echo "       $blocked" >&2
  exit 1
fi

out="$workshop/$kind/$category/$item"
mkdir -p "$out"

# What this asset is built with: its profile's answers, then its own where they differ.
eval "$(python3 "$pipeline/settings.py" "$asset" "$assets/profiles" "$assets/textures")"

forced=""
build_only=""
passthrough=()
for argument in "$@"; do
  case "$argument" in
    --rebuild) forced=yes ;;
    --build-only) build_only=yes ;;
    *) passthrough+=("$argument") ;;
  esac
done

# A character is an assembly. It has no mesh of its own — MU has none either, player.bmd
# holds meshes=0 — so it builds each part it names.
parts="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(' '.join(d.get('parts',[])))" "$asset" 2>/dev/null || true)"

if [[ -n $parts ]]; then
  for part in $parts; do
    echo "--- $part"
    "$0" "$part" --build-only "$@" || exit 1
  done
  exit 0
fi

# An asset that shares another's model. It has no .obj of its own and nothing to bake.
#
# MU's twelve spell scrolls are the case: Book01.bmd through Book12.bmd are byte-identical
# — one md5 across the lot, 28 vertices and 16 triangles each, all naming book.jpg — so
# group 15 is twelve rows on one model. Building eleven more copies would put eleven
# identical atlases in build/ pretending to be different art, and every one of them would
# have to be rebuilt when the one real material assignment changed.
#
# So this builds what it names and opens on that, under its own name. pipeline/index.py
# reads the same field and points this entry's glb at the other's; from there down nothing
# else in the pipeline or the client knows the difference.
mesh="$(python3 -c "
import json,sys
print(json.load(open(sys.argv[1])).get('mesh',''))" "$asset" 2>/dev/null || true)"

if [[ -n $mesh ]]; then
  echo "--- $mesh (shared model)"
  "$0" "$mesh" --build-only "$@" || exit 1
  exit 0
fi

obj="$source_dir/$item.obj"
blend="$out/${item}_low.blend"
albedo="$out/${item}_albedo.png"
orm="$out/${item}_orm.png"
glb="$out/$item.glb"

# Builds a rig's clips into the one file every part skinned to it shares.
#
#   build_actions <rig.json> [--speeds=<actions.json>]
#
# export_actions knows what MU means — the closing key, the locked walks, the per-action play
# speed — and writes glTF. MU2 then had Godot pack that into an .actions.res; the cook reads
# the .glb, so that second step is gone. The part still records the .res name, as MU2's did,
# and tools/sync.py takes the .glb beside it.
build_actions() {
  local rig="$1"
  local speeds="${2:-}"
  local intermediate

  # Left in a global, because the caller needs it too — the part is exported with a note
  # saying where its clips went, and that note is this path. See export_gltf's --actions.
  actions_library="$(python3 -c "
import sys
sys.path.insert(0, '$pipeline')
from index import actions_library
from pathlib import Path
print(actions_library(Path(sys.argv[1]), Path('$assets'), Path('$workshop')))" "$rig")"

  local library="$actions_library"
  intermediate="${library%.res}.glb"

  # Stale against its own inputs, and against the exporter, for the reason newest_tool
  # counts for a model.
  if [[ -z $forced && -f $intermediate && ! $rig -nt $intermediate \
        && ! $newest_tool -nt $intermediate ]]; then
    if [[ -z $speeds || ! ${speeds#--speeds=} -nt $intermediate ]]; then
      return 0
    fi
  fi

  echo "building the actions for $(basename "$rig")..."

  "$blender" --background --python "$pipeline/export_actions.py" -- \
    "$rig" "$intermediate" ${speeds:+"$speeds"} | grep -v '^Blender' || true
}

build_asset() {
  if [[ ! -x $blender ]]; then
    echo "error: Blender not found at $blender" >&2
    exit 1
  fi

  # MU's triangle soup into a mesh that can be unwrapped and baked onto. The .blend is
  # derived like everything else in build/ — the OBJ under assets/ is the source.
  #
  # And the pipeline and the rig are sources too, for the reason the .glb's own check
  # records further down — a rule that was written there and never applied here. The .blend
  # is where the bones go on, so a fix to clean_lowpoly or a re-exported rig changes it and
  # nothing about the .obj moves; the stale .blend is then rebaked and re-exported by
  # everything downstream, which reports success over a mesh built before the fix. That is
  # exactly what the Budge Dragon's missing skin did: the rig learned its group names, the
  # build was rerun, and the .glb came back with no JOINTS_0 and no complaint, because the
  # only stage that reads the rig's groups was the one stage that thought it was current.
  if [[ -n $forced || ! -f $blend || $obj -nt $blend \
        || $newest_tool -nt $blend \
        || ( -f $rig_file && $rig_file -nt $blend ) ]]; then
    echo "cleaning $item..."
    # The asset goes with the mesh, so a part can say what MU got wrong about its own
    # binding. See clean_lowpoly.rebind: MU is rigid, one bone per vertex, and a seam
    # between two bones tears by however far they turn apart.
    "$blender" --background --python "$pipeline/clean_lowpoly.py" -- \
      "$obj" "$blend" "$asset"
  fi

  # The islands, found from the mesh and merged into the asset's own file.
  #
  # It runs when the file's island list does not match the mesh, which covers both a new
  # asset and a mesh that has been re-unwrapped under an old assignment. Merging is what
  # makes that safe: an island that still measures the same keeps whatever material was
  # chosen for it, and only genuinely new ones take the profile's default.
  #
  # It was not in the build at all until the Plate gloves arrived with twenty-two islands
  # and no file to describe them. The sword's had been made by hand, once, which is exactly
  # the kind of step that works until somebody adds the second asset.
  local described mesh_islands
  described="$(python3 -c "
import json,sys
print(len(json.load(open(sys.argv[1])).get('islands',[])))" "$asset" 2>/dev/null || echo 0)"

  if [[ -n $forced || $described == 0 || $blend -nt $asset ]]; then
    echo "finding the islands..."
    "$blender" --background --python "$pipeline/islands.py" -- \
      "$blend" "$asset" "--default=${default_material:-unassigned}" | tail -n +2
  fi

  # Every sheet this asset wears, each taken to HD on its own, then one bake through all
  # of them. The slots come from the mesh — clean_lowpoly kept MU's groups as material
  # slots — so the pipeline asks the mesh what it needs rather than being told.
  local slots pairs=()
  # Each entry is "group=file". The group is the mesh's material slot, which came from
  # the OBJ group, which came from the texture MU's own .bmd names — and that is not
  # always what the file on disk is called. Sword01's mesh says sword02 and its art is
  # sword02_npc.png, because the higher-resolution copy lives under a different name; the
  # pair matched on file name and silently found nothing, so the blade came out with no
  # colour on it at all.
  slots="$(python3 -c "
import json,sys
from pathlib import Path
d=json.load(open(sys.argv[1]))
s=d.get('sheets')
if isinstance(s, dict):
    print(' '.join(f'{g}={f}' for g, f in s.items()))
elif isinstance(s, list):
    print(' '.join(f'{Path(f).stem}={f}' for f in s))
elif d.get('sheet'):
    print(f'{Path(d[\"sheet\"]).stem}={d[\"sheet\"]}')" "$asset" 2>/dev/null || true)"

  # Which of those sheets is a cut-out, if the asset says any is. See transfer_albedo.
  local cutout
  cutout="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(' '.join(f'--cutout={g}' for g in d.get('cutout', [])))" "$asset" 2>/dev/null || true)"

  # And which of those cut-outs take their mask from the sheet's own alpha rather than from
  # its brightness. The subset, not a separate list: a slot has to be a cutout first. See
  # carry_alpha, which explains why this is a second declaration and not a look at the art.
  local keepalpha
  keepalpha="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(' '.join(f'--keepalpha={g}' for g in d.get('cutout_alpha', [])))" "$asset" 2>/dev/null || true)"

  # And which of them MU wraps round the model rather than laying out flat. See
  # transfer_albedo: the default is to clip outside the sheet, which is right for an atlas
  # and puts a black gash down anything whose UVs leave [0,1] — the Apple's do.
  local wrapped
  wrapped="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(' '.join(f'--wrap={g}' for g in d.get('wrap', [])))" "$asset" 2>/dev/null || true)"

  local tiled_flag=""
  local newest_sheet=""
  for one in $slots; do
    local group="${one%%=*}"
    local file="${one#*=}"
    local raw="$assets/textures/$file"
    [[ -f $raw ]] || { echo "error: no texture $raw for slot '$group'" >&2; exit 1; }

    local stem="${file%.png}"
    local hd="$out/${stem}_hd.png"

    # How this sheet's border is made up, which is a property of the sheet and not of the
    # thing wearing it.
    #
    # The profile answers for most of them and buildings are where that stops being enough.
    # HouseWall02 wears five sheets at once: tile_wood02 is a plank wall that tiles and wants
    # wrap, and tile_ston06 is an atlas with a timber post down one side and a gold ornament
    # in the corner - wrapped, the ornament is fed into the post as context and the model
    # invents gold along a beam the artist drew plain. One asset, two right answers, so the
    # asset is allowed to name the exception and the profile keeps the rule.
    local sheet_pad="$upscale_pad"
    local named
    named="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(d.get('sheet_pad', {}).get(sys.argv[2], ''))" "$asset" "$group" 2>/dev/null || true)"
    [[ -n $named ]] && sheet_pad="$named"

    # A sheet that arrived with its own maps is not upscaled and not seam-healed.
    #
    # Both of those exist to make the most of a 128-pixel hand-painted square. They were
    # skipped for a photoscanned surface, which is 2048 already and tiles already - measured,
    # not assumed - so upscaling it would be reconstructing detail from detail and healing a
    # seam it does not have would blur a border that is correct. Nothing in the tree is one
    # of those any more: the project stands on MU's own art, and this path is what it is for.
    local supplied=""
    supplied="$(python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print('yes' if sys.argv[2] in (d.get('sheet_normal') or {}) or sys.argv[2] in (d.get('sheet_orm') or {}) else '')" "$asset" "$group" 2>/dev/null || true)"

    if [[ -n $supplied ]]; then
      if [[ -n $forced || ! -f $hd || $raw -nt $hd || $asset -nt $hd ]]; then
        echo "$file arrived with its own maps; using it as it is..."
        python3 "$pipeline/enlarge.py" "$raw" "$asset" "$hd" "$enlarge"
      fi

      pairs+=("$group=$hd")
      [[ -z $newest_sheet || $hd -nt $newest_sheet ]] && newest_sheet="$hd"
      continue
    fi

    if [[ ${upscale_factor:-0} != 0 && $(echo "${upscale_detail:-0} > 0" | bc) == 1 ]]; then
      local up="$out/${stem}_x${upscale_factor}.png"
      if [[ -n $forced || ! -f $up || $raw -nt $up || $asset -nt $up ]]; then
        echo "upscaling $file ${upscale_factor}x at detail $upscale_detail, $sheet_pad context..."
        python3 "$pipeline/upscale.py" "$raw" "$up" "$upscale_detail" "$upscale_factor" \
          "$sheet_pad" "$upscale_model" "$upscale_passes"
      fi
      raw="$up"
      hd="$out/${stem}_x${upscale_factor}_hd.png"
    fi

    # Nothing to enlarge past: hand the sheet on as it is.
    #
    # settings.py clamps enlarge to 1 once the upscale already fills the map - a 64-square
    # at 3x is 192 and its map is 192 - and enlarge.py at 1x is an identity: every
    # gloves_07_x3_hd.png in the tree was byte-for-byte its gloves_07_x3.png. A second copy
    # of a file is a second thing to be stale, and a reader diffing the two to judge the
    # model was looking at the wrong pair. So the bake reads the 3x directly and no _hd is
    # written; the pair to judge is the _x3 against the sheet in assets/textures.
    if [[ ${enlarge:-1} -le 1 ]]; then
      hd="$raw"
    elif [[ -n $forced || ! -f $hd || $raw -nt $hd || $asset -nt $hd ]]; then
      echo "enlarging $(basename "$raw")..."
      python3 "$pipeline/enlarge.py" "$raw" "$asset" "$hd" "$enlarge"
    fi

    pairs+=("$group=$hd")
    [[ -z $newest_sheet || $hd -nt $newest_sheet ]] && newest_sheet="$hd"
  done

  # Scenery takes a different road from here, and it is not an optimisation.
  #
  # Everything below this line bakes: it moves MU's painting onto the layout clean_lowpoly
  # unwrapped, generates the surface maps into the same layout, and ships one atlas. That
  # depends on each triangle sampling its sheet exactly once, which every worn and carried
  # thing does and no piece of a map does. Stone01's boulder wraps a 128-square rock face
  # round itself 2.37 times over; baked, its weeds took 59% of the map and the rock 18%.
  #
  # So a tiled profile stops here, hands export_gltf the sheets and what each one is made
  # of, and lets MU's own coordinates do the work. See tiled_materials in export_gltf.
  if [[ ${tiled:-0} == 1 ]]; then
    python3 - "$asset" "$assets/materials" "$out/slots.json" "${pairs[@]}" <<'SLOTS'
import json, sys
from pathlib import Path

asset = json.load(open(sys.argv[1]))
library, out = Path(sys.argv[2]), Path(sys.argv[3])
cutout = set(asset.get("cutout", []))
additive = set(asset.get("additive", []))

# Where a cut-out wants a threshold other than MU's own quarter. See export_gltf's
# ALPHA_TEST_REF: BeginOpengl sets glAlphaFunc(GL_GREATER, 0.25f) and the client tests every
# sheet with an alpha channel against it. A handful want looser still - guard_hair is soft all
# the way across and even a quarter leaves a speckle.
cutout_at = asset.get("cutout_at", {})

# What each sheet is made of. Declared per sheet, because on scenery that is the level the
# question has an answer at: ston01 is rock wherever it appears on the model and ston02 is
# grass. The island list stays for the record and for anything that wants to read it, but
# nothing here consults it.
named = asset.get("sheet_materials", {})

slots = []
for pair in sys.argv[4:]:
    group, sheet = pair.split("=", 1)
    name = named.get(group, asset.get("default_material", "unassigned"))
    definition = library / f"{name}.json"
    values = json.loads(definition.read_text()) if definition.exists() else {}

    if not definition.exists():
        print(f"  slot {group}: no material '{name}' in the library", file=sys.stderr)

    # Maps the sheet brought with it, if any. A Fab surface ships a measured normal and a
    # measured ORM; tiled_maps derives both from luminance and must not, when the real thing
    # is on disk. Named per slot for the same reason materials are: one asset can wear a
    # photoscanned wall and a hand-painted beam at once.
    textures = Path(sys.argv[1]).parents[2] / "textures"
    entry = {}

    if (asset.get("sheet_scale") or {}).get(group):
        entry["scale"] = float(asset["sheet_scale"][group])

    # Roughness and relief per slot, which the material library cannot answer.
    #
    # A material says what a thing is made of and that is the right level for most of it —
    # timber is timber wherever it appears. These two are about a *sheet* rather than a
    # substance: how rough a particular photoscan reads once it is on a particular wall, and
    # how far its measured normal should be pushed. Both are judged by eye on the object, in
    # the Objects tab, and this is where that judgement is written down.
    for declared, key in (("sheet_roughness", "roughness_scale"),
                          ("sheet_relief", "relief"),
                          ("sheet_saturation", "saturation"),
                          ("sheet_metallic", "metal_value")):
        if (asset.get(declared) or {}).get(group) is not None:
            entry[key] = float(asset[declared][group])

    # The name the asset wrote, kept beside the path the build made from it.
    #
    # By the time a slot reaches the viewer its sheet is three transformations downstream —
    # facade_fab.png arrives as facade_fab_hd_sat1.25.png — and a panel reporting that back
    # is naming a file no asset has ever contained. The source name costs one line here and
    # is the only form of it anybody can act on.
    if (asset.get("sheets") or {}).get(group):
        entry["asset_sheet"] = str(asset["sheets"][group])

    # A colour rather than a number, so it travels as the text it was written as.
    if (asset.get("sheet_tint") or {}).get(group):
        entry["tint"] = str(asset["sheet_tint"][group])

    # Parts of one sheet that are made of something else.
    #
    # Per sheet is the right level for most scenery and the wrong one for anything MU drew
    # as an atlas. Well02 is one 128-square holding a plank roof above a stone shaft; the
    # cannons are one 256-square of crates and wheels with the iron barrel, its bands and a
    # tray of shot scattered through it. Declared rock, the well's roof gets stone's relief;
    # declared timber, the cannon's barrel gets a plank's roughness. There is no per-sheet
    # answer to give because the sheet is not made of one thing.
    #
    # A box and not a colour test. Dark-and-neutral finds the cannon's iron and also finds
    # the unlit inside of its wooden crates, which is 19% of that sheet either way — the
    # picture does not carry the distinction reliably enough to be measured out of it. Where
    # it splits is by *place*, and a rectangle is a thing somebody can look at the sheet and
    # write down. The material each one names is resolved here, so the slot arrives carrying
    # the numbers rather than the names.
    regions = []

    for said in (asset.get("sheet_regions") or {}).get(group, []):
        other = library / f"{said['material']}.json"

        if not other.exists():
            print(f"  slot {group}: no material '{said['material']}' in the library",
                  file=sys.stderr)
            continue

        second = json.loads(other.read_text())
        regions.append({
            "material": said["material"],
            "box": [float(v) for v in said["box"]],
            "roughness": float(second.get("roughness", 0.6)),
            "metallic": float(second.get("metallic", 0.0)),
            "depth": float((second.get("grain") or {}).get("depth", 0.0)),
            "roughness_range": float(
                (second.get("grain") or {}).get("roughness_range", 0.0)),
        })

    if regions:
        entry["regions"] = regions

    # A region the asset addressed directly, rather than one generated below.
    #
    # Once a region is a part with a name, the bench can dress it — and then it wants its own
    # sheet, its own scale, its own tint, which are things only the asset can say. So
    # sheets["well__timber"] is allowed, and when it is there this loop has already built it
    # like any other sheet; all it is missing is which faces are its, which is the box the
    # parent's regions declared.
    if "__" in group:
        parent, _, wanted = group.partition("__")
        theirs = [one["box"] for one
                  in (asset.get("sheet_regions") or {}).get(parent, [])
                  if one["material"] == wanted]

        if theirs:
            entry["boxes"] = theirs
            entry["of"] = parent
            entry.pop("regions", None)

    for kind in ("normal", "orm"):
        given = (asset.get(f"sheet_{kind}") or {}).get(group)

        if given and (textures / given).exists():
            entry[f"given_{kind}"] = str(textures / given)
        elif given:
            print(f"  slot {group}: no {kind} map at {textures / given}", file=sys.stderr)

    slots.append({
        "name": group,
        "sheet": sheet,
        "material": name,
        **entry,
        "metallic": values.get("metallic", 0.0),
        "roughness": values.get("roughness", 0.6),

        "cutout": group in cutout,
        # Absent rather than defaulted, so export_gltf's own ALPHA_TEST_REF is what answers.
        # Writing a number here made this the last word: the default moved from a half to
        # MU's quarter and every tiled asset kept the half, because the slot file was still
        # saying so.
        **({"cutout_at": float(cutout_at[group])} if group in cutout_at else {}),
        "additive": group in additive,
        "skip": group in set(asset.get("skip_sheets", [])),
    })

    # A region becomes a slot of its own, standing beside the one it was cut out of.
    #
    # It could have stayed a patch on the maps — that is how it started, and the maps were
    # right. What it could not do was be *spoken to*: the Objects tab lists a model's parts
    # by their materials, so a cannon whose barrel is iron only inside the ORM still had one
    # part called horse_drawn_01 and no way to say the barrel should be darker. A slot has a
    # name, a scale, a tint and a row on the bench, and those are the things a decision about
    # the barrel needs somewhere to live.
    #
    # Same sheet and same maps as its parent — a region is not a different picture, it is a
    # different part of one — so this costs an entry in a list and no texture memory. What
    # divides them is the box, which export_gltf reads to decide which faces go where.
    # One slot per material, however many boxes it took to say where it is. The cannon's
    # iron is four separate patches of its atlas — a muzzle, a plate, a tray of shot and the
    # barrel — and they are one substance and want one row on the bench, not four identical
    # ones.
    by_material = {}

    for one in regions:
        by_material.setdefault(one["material"], []).append(one["box"])

    for material, boxes in by_material.items():
        if f"{group}__{material}" in (asset.get("sheets") or {}):
            continue                      # the asset said it itself, above

        second = library / f"{material}.json"
        their = json.loads(second.read_text()) if second.exists() else {}

        slots.append({
            "name": f"{group}__{material}",
            "sheet": sheet,
            "material": material,
            **{k: v for k, v in entry.items() if k != "regions"},
            "boxes": boxes,
            "of": group,
            "metallic": their.get("metallic", 0.0),
            "roughness": their.get("roughness", 0.6),
            "cutout": group in cutout,
            "additive": group in additive,
            "skip": group in set(asset.get("skip_sheets", [])),
        })

out.write_text(json.dumps(slots, indent=2))
SLOTS
    # And the surface maps, in the sheet's own space rather than a bake's. Reads the slot
    # file it was just handed and writes the normal path back into it.
    python3 "$pipeline/tiled_maps.py" "$out/slots.json" "$assets/materials"

    tiled_flag="--slots=$out/slots.json"
  elif [[ ${#pairs[@]} -gt 0 ]]; then
    # A declaration the baked path cannot carry, said out loud rather than dropped.
    #
    # `additive` is read where the slot file is written, which is the branch above: a sheet
    # becomes emissive there and gets its own material. Baking puts every sheet on one atlas
    # under one material, so there is nowhere for "this one is added rather than drawn" to
    # live, and the key was being accepted and ignored.
    #
    # It cost Elf Lala her wings. lpwing is pale veins on black and MU adds it, so black
    # contributes nothing and you see the veins and the wood through them; baked opaque it is
    # a black card, which is a fairy with two rectangles behind her. Nothing in the build said
    # anything, because nothing was looking.
    #
    # The fix on an asset is `"tiled": true`, which keeps its profile and takes the atlas
    # away. This only has to shout.
    if python3 -c "
import json, sys
print('yes' if json.load(open(sys.argv[1])).get('additive') else '')" "$asset" 2>/dev/null | grep -q yes; then
      echo "error: $item declares 'additive' and is built on the baked path, where it means" >&2
      echo "       nothing - one atlas, one material, nowhere to say a sheet is added." >&2
      echo "       Add \"tiled\": true to the asset. See ElfWizard01.json." >&2
    fi

    if [[ -n $forced || ! -f $albedo || $newest_sheet -nt $albedo || $blend -nt $albedo ]]; then
      echo "baking the albedo from ${#pairs[@]} sheet(s)..."
      # Linear: every sheet has already been taken to HD with its boundaries respected.
      "$blender" --background --python "$pipeline/transfer_albedo.py" -- \
        "$blend" "$albedo" "$maps" Linear ${cutout:-} ${keepalpha:-} ${wrapped:-} "${pairs[@]}"
    fi
  fi

  local newest_material
  newest_material="$(ls -t "$assets"/materials/*.json 2>/dev/null | head -1)"

  if [[ ${tiled:-0} != 1 ]] && [[ -n $forced || ! -f $orm || $asset -nt $orm || $blend -nt $orm \
        || ( -n $newest_material && $newest_material -nt $orm ) ]]; then
    echo "building the material maps..."
    "$blender" --background --python "$pipeline/build_maps.py" -- \
      "$blend" "$asset" "$out" "$material_maps" "$assets/materials"
  fi

  # Decided here, after the bake, and not before it. Read once at the top of the script,
  # the first clean rebuild exported an item with no colour on it at all: the albedo did
  # not exist when the question was asked, and the bake created it a few lines later.
  local source_albedo="$albedo"
  [[ -f $albedo ]] || source_albedo="-"

  # The rig travels with the mesh when there is one. The part's own file supplies the
  # skeleton it was bound in; something else supplies the motion.
  #
  # Which "something else" is the question, and there are two answers because MU keeps
  # animation in two different places. A worn part carries a bind pose and nothing else —
  # the clips live in player.bmd and are written against the player's skeleton, which is
  # why an armour piece has to borrow them. A piece of scenery is the opposite: a candle,
  # a tree, a windmill carries its own bones *and* its own action, because nothing else in
  # the world shares them. Candle01.bmd is nine bones, seven keys and one action, and three
  # of those bones are the flames leaning.
  #
  # So a rig that carries motion of its own is its own motion, and one that does not falls
  # back to the player's. Asked of the file rather than of where the asset sits: the question
  # is what this rig actually holds, and a rule about directories would have to be rewritten
  # the first time a monster or an item turned out to animate itself.
  #
  # Motion, not actions. Every rig has an action; a worn part's is a bind pose that holds one
  # value in every key. Counting actions called that motion and left the bare Dark Knight
  # standing frozen.
  local rig_args=()

  if [[ -f $source_dir/$item.rig.json ]]; then
    rig_args+=("--rig=$source_dir/$item.rig.json")

    if python3 -c "
import json, sys

# Whether this rig carries motion of its own, which is not the same as carrying an action.
#
# Every rig has an action. A worn part's is a bind pose — one entry whose tracks hold the
# same value in every key, there so the part has somewhere to be — and counting actions
# treats that as motion, which is the mistake this replaces: the bare Dark Knight stopped
# borrowing player.bmd's 283 clips and stood frozen, because his armour piece \"had its own\".
#
# So the question is whether anything moves. A candle's flames rotate, a windmill's wheel
# turns, a bind pose does not.
rig = json.load(open(sys.argv[1]))
moves = False

for action in rig.get('animations', []):
    for track in action.get('tracks', []):
        for channel in ('t', 'r'):
            keys = track.get(channel) or []
            if any(any(abs(v[i] - keys[0][i]) > 1e-4 for i in range(len(v))) for v in keys):
                moves = True

print(moves)
" "$source_dir/$item.rig.json" | grep -q True; then
      rig_args+=("--animation=$source_dir/$item.rig.json")

      # And the rate a monster plays its own clips at, which the .bmd does not carry.
      #
      # A monster is the third answer to "where does the play speed come from", and it needed
      # one because the other two are both wrong for it. It is not the townsperson's flat
      # 0.25 — that is what this built first, and it gave the Spider a walk at a quarter of
      # its real rate. It is not the player's table either, because a monster's speed is not
      # a property of the action alone: MU sets a default, scales a few models, then
      # overrides a few walks outright, and the answer depends on which model is asking.
      #
      # So the asset says which model it is and the shared table says the rest. See
      # assets/monsters/actions.json for MU's own three layers and monster_speeds.py for the
      # order they have to be applied in.
      local model_index
      model_index="$(python3 -c "
import json, sys
print(json.load(open(sys.argv[1])).get('monster', {}).get('model_index', ''))" \
        "$asset" 2>/dev/null || true)"

      if [[ -n $model_index && -f $assets/monsters/actions.json ]]; then
        python3 "$pipeline/monster_speeds.py" \
          "$assets/monsters/actions.json" "$model_index" "$out/${item}_speeds.json"
        rig_args+=("--speeds=$out/${item}_speeds.json")
      fi
    else
      # Whose motion it borrows, where it borrows any.
      #
      # The player's, unless the asset names another. MU builds a townsperson exactly the way
      # it builds a player — Girl01, Man01 and Female01 are skeleton and actions with no mesh
      # on them at all, and the head, upper, lower and boots are meshes in a bind pose — so
      # the only thing that differs is which file the clips come out of. A merchant skinned to
      # her own 37-bone rig cannot borrow player.bmd's 60.
      local motion="$assets/players/rig/player.rig.json"
      local named_rig
      named_rig="$(python3 -c "
import json, sys
print(json.load(open(sys.argv[1])).get('animation_rig', ''))" "$asset" 2>/dev/null || true)"

      [[ -n $named_rig ]] && motion="$assets/$named_rig"
      [[ -f $motion ]] && rig_args+=("--animation=$motion")

      # And the rate those clips play at, which the .bmd does not carry either.
      #
      # The player keeps a speed per action and the table is beside its rig; a townsperson
      # gets one flat rate for every action, which is the exporter's default, so nothing is
      # passed for one. See actions.json and export_gltf's DEFAULT_PLAY_SPEED.
      local speeds_arg=""
      if [[ -z $named_rig && -f $assets/players/rig/actions.json ]]; then
        speeds_arg="--speeds=$assets/players/rig/actions.json"
        rig_args+=("$speeds_arg")
      fi

      # Borrowed motion is written where it is borrowed from, and not into the borrower.
      #
      # This is the one place that can tell the difference. A monster's clips are its own —
      # one model, one file, nothing duplicated — and a worn part's are not: five parts make
      # a character and all five are skinned to the same rig, so a part carrying the rig's
      # actions is a part carrying four other parts' copies of them. The fifteen files under
      # build/players/body held fifteen identical copies of player.bmd's 283, 13.4 MB each,
      # and the viewer read five of them per character and used one.
      #
      # So the clips are built once against the rig and the parts are exported without them.
      # See build_actions, and Model.Animate for the side that puts the two back together.
      if [[ -f $motion ]]; then
        build_actions "$motion" "$speeds_arg"
        rig_args+=("--no-clips" "--actions=$actions_library")
      fi
    fi
  fi

  # A model the client plays at its own rate rather than at the one its kind gets.
  #
  # World objects are the case this exists for: MoveObject hands PlayAnimation the object's
  # o->Velocity where a character's play speed goes, so a merchant's animal at 0.16 and a
  # street lamp at 0.3 are the same field with a different number in it.
  # Off the resolved settings and not off the asset, which is what this read before.
  #
  # The asset is the last word and the profile is the default, and reading only the first of
  # those meant the default never arrived: world.json says 0.16 because that is
  # CreateObject's o->Velocity, and every animated object in the project was still taking
  # export_gltf's own fallback of 0.25 — the townsperson's rate, out of OpenNpc. Lorencia's
  # trees, lamps and curtains and all 22 of Noria's ran 56% fast, which on a swaying tree is
  # invisible unless the client is open beside it.
  [[ -n ${play_speed:-} ]] && rig_args+=("--play-speed=$play_speed")

  # And a model that asks not to stall on the wrap. One asset asks: see Carriage01.json,
  # and write_animations for what it costs. It is per model because it takes a bone off the
  # action's clock, which is fine on a swinging lamp and is a limp on anything with a
  # skeleton whose bones are watched against each other.
  local declared_unstall
  declared_unstall="$(python3 -c "
import json, sys
print('yes' if json.load(open(sys.argv[1])).get('unstall') else '')" "$asset" 2>/dev/null || true)"

  [[ -n $declared_unstall ]] && rig_args+=("--unstall")

  # And bones geared down against the rest of the skeleton. One asset asks: Object40, whose
  # drum turns a whole revolution in the goblins' one push. See write_animations.
  local declared_geared
  declared_geared="$(python3 -c "
import json, sys
g = json.load(open(sys.argv[1])).get('geared')
print(','.join(str(b) for b in g['bones']) + ':' + str(int(g['loops'])) if g else '')" "$asset" 2>/dev/null || true)"

  [[ -n $declared_geared ]] && rig_args+=("--geared=$declared_geared")

  # Whether anything about this item is a cut-out: the asset saying so, or a sheet that
  # carries a real alpha channel of its own. Decided here, where both are visible, rather
  # than guessed from a baked map — see the note in export_gltf.
  local cut_flag=""
  if python3 -c "
import json,sys
from pathlib import Path
d=json.load(open(sys.argv[1]))
if d.get('cutout'): raise SystemExit(0)
# Declared false means false. The sniff below is a fallback and it is wrong on any sheet
# whose alpha is a mask for something other than transparency; see Shield10, whose alpha is
# an emblem and which shipped for months as a shield full of holes.
if d.get('cutout') is False: raise SystemExit(1)
try:
    from PIL import Image
except ImportError:
    raise SystemExit(1)
s=d.get('sheets'); names=list(s.values()) if isinstance(s,dict) else list(s or [])
for f in names:
    p=Path(sys.argv[2])/f
    if not p.exists(): continue
    im=Image.open(p)
    if 'A' in im.mode:
        lo,hi=im.getchannel('A').getextrema()
        if lo<250 and hi>0: raise SystemExit(0)
raise SystemExit(1)" "$asset" "$assets/textures" 2>/dev/null; then
    cut_flag="--cutout"
  fi

  # Whether it is drawn one-sided, which is a fact about the kind of thing rather than
  # about this one - so it comes off the resolved profile with the asset able to override.
  local cull_flag=""
  if [[ ${cull:-0} == 1 ]]; then
    cull_flag="--cull"
  fi

  echo "exporting $item..."
  "$blender" --background --python "$pipeline/export_gltf.py" -- \
    "$blend" "$source_albedo" "$glb" ${cut_flag:-} ${tiled_flag:-} ${cull_flag:-} \
    "--islands=$asset" "${rig_args[@]+"${rig_args[@]}"}"
}

# The pipeline is an input too.
#
# This asked whether the *asset* had moved and never whether the thing that reads it had.
# Change the exporter and every .glb in the tree stays exactly as stale as it was, silently,
# because each one is still newer than the .obj and the .json it was made from. Found by
# fixing the animation loop, rebuilding all ten body parts, and getting ten files back
# byte-identical to the ones that had the bug in them.
#
# Any pipeline file counts, not just the one that changed. build_asset is one step from the
# .obj to the .glb with no way in partway down, so there is nothing finer to compare against
# and pretending otherwise would be the same silence in a smaller place.
newest_tool="$(ls -t "$pipeline"/*.py | head -1)"

# And the rig is an input as well, for the same reason and with a sharper edge.
#
# A rig arriving beside an asset that did not have one changes the .glb completely - the part
# stops being one rigid mesh and becomes a skinned one with a skeleton and a clip - and
# nothing else about the asset moves when it happens. Left out, the first crossbow to be given
# its animation rebuilt into a file byte-identical to the rigid one, reported "rig none", and
# looked exactly like a pipeline that did not support the thing it had just been handed.
rig_file="$source_dir/$item.rig.json"

# The material library is an input too, and leaving it out is why "material json edits need
# --rebuild" became folklore.
#
# build_asset has always checked it — see newest_material there — but this gate decides
# whether build_asset is called at all, and the library was not on its list. So editing
# assets/materials/steel.json and rebuilding the item that wears it did nothing, silently,
# and the workaround everyone learned was to force the whole thing. Halving bone's grain
# depth reproduced it exactly: the file was 57 seconds newer than the ORM and the material
# step never ran.
newest_material="$(ls -t "$assets"/materials/*.json 2>/dev/null | head -1)"

if [[ -n $forced || ! -f $glb || $obj -nt $glb || $asset -nt $glb \
      || $newest_tool -nt $glb \
      || ( -n $newest_material && $newest_material -nt $glb ) \
      || ( -f $rig_file && $rig_file -nt $glb ) \
      || ( -f $albedo && $albedo -nt $glb ) || ( -f $orm && $orm -nt $glb ) ]]; then
  build_asset
fi

[[ -n $build_only ]] && exit 0
