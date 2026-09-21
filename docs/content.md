# Content: from MU's files to `assets/`

Carried over from MU2 on 2026-09-21 so this tree can make its own content on any machine,
without MU2's Godot projects. Four stages; the first is done once per asset and kept in git.

| stage | from | to | tool | in git |
|---|---|---|---|---|
| import | MU's `Data/` (`tools/fetch_mumain.sh`) | `source/` | MuExtract, `decode_texture.py`, `terrain.py` | `source/` yes |
| build | `source/` | `workshop/` | `tools/content.sh`, `tools/asset.sh` | no |
| sync | `workshop/` | `assets/` | `tools/sync.sh` | no |
| cook | `assets/` | `assets/cooked/` | `tools/cook.py` | no |

## A new machine

    ./bootstrap.sh                 bgfx and the headers
    ./tools/content.sh             hours: every sheet through the upscaler, every model through Blender
    ./tools/sync.sh
    ./tools/cook.py --world lorencia

Needs Blender at `/Applications/Blender.app` (or `BLENDER=`), and python3 with `numpy`,
`Pillow` and `torch`. The two upscaler checkpoints `upscale.py` uses (RealESRGAN_x4plus,
4x_NMKD-Siax_200k, 67 MB each) are fetched into `weights/` on first use and checked by sha256.

A build is not byte-reproducible: `build_maps.py` bakes a random grain, and two runs of
MU2's own copy differ by as much as this copy differs from it (Sword01's ORM, mean 2.8 of 255
in roughness either way). Compare builds by eye or by statistics, not by hash.

## Where things came from

- `pipeline/` is `MU2/pipeline/*.py`. Paths changed: `mu.db` is read from `source/` (it sat
  at the repository root), and the scripts that wrote into `MU2/assets` write into `source/`.
- `tools/asset.sh` is `MU2/mu2.sh`'s per-asset build, lines 560-1422, minus the viewer and
  minus Godot: the clips stay the `.actions.glb` `export_actions.py` writes, and the `.res`
  MU2 packed from it with `pack_actions.gd` is never made. Parts still name the `.res`;
  `sync.py` takes the `.glb` beside it.
- `tools/content.sh` is new. MU2 ran these steps one command at a time.
- `source/` is `MU2/assets` as of 2026-09-21, plus `mu.db` from git (made by hand from OpenMU;
  no script writes it).
- `tools/MuExtract/` is MU2's C# importer (`dotnet`, net10.0).

## Importing a new asset

    ./tools/fetch_mumain.sh
    dotnet run --project tools/MuExtract -- export-obj reference/MuMain/src/bin/Data/Item/Sword01.bmd source/items/weapons/Sword01.obj
    dotnet run --project tools/MuExtract -- export-rig <x.bmd> source/<...>/<Name>.rig.json [--actions=all]
    python3 pipeline/decode_texture.py <X.OZJ> source/textures/x.png
    python3 pipeline/terrain.py reference/MuMain/src/bin/Data/World1 lorencia source/world 1

Then write the recipe json beside the `.obj`; `islands.py` fills in its island list on the
first build.

The interface sheets (`hud_cut.py`, `lobby_cut.py`, `skill_icons.py`, `buff_icons.py`) were
cut from MuDream's Season 6 data, which is not public and not on this machine; their output
is in `source/interface` and is kept.
