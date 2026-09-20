# The material audit

Written 2026-09-20, after a Budge Dragon's wings came out glossy. Not a sprint: a standing
check, like `cookcheck` and the budget gate, that says whether every surface in this content
wears the material it ought to.

`PLAN.md`'s foundation 5 closed the material model — albedo, normal, ORM, emissive, and the
three flags — and said MU2's material library would be mapped onto it at cook time. Nothing
checked that the mapping happened, or that what it produced matched what the library asked
for. `tools/matcheck.py` is that check.

    tools/matcheck.py                       # the gate: exits non-zero on a new failure
    tools/matcheck.py --strict              # every failure, baseline or not
    tools/matcheck.py --model BudgeDragon01 # one model, every slot, verbose
    tools/matcheck.py --census docs/materials.census.md
    tools/matcheck.py --record              # rewrite the baseline after a repair

## Why it takes two halves

Only some of this can be mechanised, and pretending otherwise is how an audit produces
confident nonsense.

**The mechanical half** compares two records that already exist: the glTF material name, and
the ORM texture baked under the triangles that use it. A name either resolves to a library
entry in `index.json` or it does not. A baked roughness either agrees with that entry or it
does not. A `MASK` material's albedo either has holes in its alpha or the cutout is a lie. A
cooked `.ktx` either carries the format its role demands or the colour space is wrong. None of
that needs an eye, and all of it fails the run.

**The judged half** cannot be mechanised at all. No checker knows that a bat's wing membrane
should not wear `leather`; that is a look, judged on a shot, and it is the user's call. So the
judgements are written down once, in `sheets/materials.json`, as **rulings** — and after a
ruling exists the audit is mechanical again, because a ruling is something the ORM can be
measured against. The unruled slots are listed largest-area-first so the list shrinks in the
order that matters instead of being rediscovered every time someone looks at a monster.

## What it measures, and why each one is a defect

| check | the failure it catches |
|---|---|
| name resolves to the library | a slot named `mu2` or `tile_wood02`: MU2's exporter named the glTF material after the texture sheet, so nothing records which library entry chose its reflectance |
| baked roughness vs the entry's | a region painted from the wrong recipe, a channel swapped, or a bake that flattened every material on a model to one value |
| baked metal vs the entry's | the loudest mistake available: a metal has no diffuse at all, so a wrong one turns painted art into a mirror |
| metal between 0.05 and 0.30 | not a material but a blend of two, and the library has no entry in that gap on purpose |
| a metal against the model's `metal:` list | `index.json` records which metals a monster may wear; anything else is a metal nobody asked for |
| an ORM map **or** a roughness factor | glTF says roughness is `factor × map`, and MU2's pipeline uses exactly that split, so a surface with no map and a real factor is correct. One with neither is unpainted and reaches the engine's matte |
| relief asked vs the normal map's measured lean | **the one the wings were found by.** See below |
| `MASK` with no holes in its alpha | a discard paid for in four passes that cuts nothing |
| `OPAQUE` with holes in its alpha | whatever was meant to be cut out is drawn solid; this is how MU's foliage breaks |
| cooked `.ktx` format per role | BC7-sRGB albedo, BC5 normal, BC7-linear ORM. An ORM read as sRGB turns roughness 0.62 into about 0.35 and makes the whole town glossy at once |
| mips in every cooked texture | `PLAN.md`'s mip rule, which is not optional |

**The land is checked through its own record**, not through material slots it does not have.
It has its own shader, it blends two full material sets by a per-vertex weight, and its
surfaces come from a world's `ground_surfaces.json` — which was at first taken as a reason to
skip it, and is only a reason not to audit it the same way. Every surface there does name a
library material, in the suffix of the maps MU2's `tiled_maps` wrote for it
(`TileGrass01 1_tiling_hd_grass_orm.png` is `grass`), so the same question is asked of the
same numbers: 29 surface maps across three worlds, and the tolerance is wider because a
ground ORM's roughness is *meant* to vary across its sheet.

It was worth doing. 27 of the 29 land within 0.011 of what they claim; noria's and
charscene's water carry 0.722 where the library asks 0.08, which is a lake rendered as dry
plaster. Lorencia's water is correct, which is why nothing had noticed.

## The glossy wings, and what they turned out to be

The Budge Dragon's wings are 8 triangles wearing `leather`, and the audit says their data is
faithful: the ORM under them reads roughness 0.612 against the library's 0.62, metal 0.000
against 0.00. Nothing was mis-assigned and nothing drifted.

What is wrong is one row over. `chitin` carries **relief 1.00** and `leather` 0.06, and the
normal map they share leans **0.3° across the body and 2.2° across the wings** — it is flat.
The relief was authored in the library and never reached the map.

That is what the glossiness is. Relief is what breaks a specular highlight into a surface;
without it, roughness 0.62 over a flat two-sided quad produces one wide coherent lobe across
the whole wing, which reads as sheet plastic. Roughness cannot do relief's job — lowering it
makes the lobe tighter and brighter, raising it makes the wing chalky, and neither of those is
a membrane. The check is now measured rather than inferred from the map's presence, because a
normal map full of (0.5, 0.5) is present and does nothing.

It is not one monster. 26 slots fail this check today, and `skin` is fifteen of them — which is
every bare face, hand and shin in the player's armour sets.

## What the run says today

329 models, 636 material slots, 465 cooked textures.

**First fixed, 2026-09-20: the factors.** 102 of the failures below were the engine's fault
rather than the content's. glTF defines roughness as `factor × map`; MU2's pipeline writes a
map where the relief came out of the art and puts the whole answer in the factor where the
material declares no grain, which is MU's foliage, its grass and its water. The engine read
only the map, so all 195 of those slots shaded at the fallback — water at roughness 1 where
the library asks 0.08. The factors now travel from the glb through the `.mum` (versions 3 and
4) into `u_material.zw`, and the shade pass multiplies. 190 failures became 90.

- **147 slots (23%) name a library material.** The other 424 are named after MU's texture
  sheets or left at the exporter's `mu2` default. By area it is worse: 2% of the surface in
  `assets/` is wearing a material anything recorded a decision about. Most of those sheets do
  carry an ORM whose roughness sits on a library value, so a recipe did run — but the record of
  which one went with the name, and the numbers cannot give it back: six or seven library
  entries fit inside one tolerance between 0.7 and 0.9. Only 5 of the 424 can be identified
  from their ORM alone.
- **90 failures**, recorded as the baseline:

  | count | failure | where it is |
  |---:|---|---|
  | 46 | a baked roughness that disagrees with the entry it claims | MU2's build, stale |
  | 26 | a flat normal map against a relief the library asked for | MU2's `build_maps.py` |
  | 13 | a `MASK` cutout with no holes to cut | MU2's export |
  | 2 | noria's and charscene's water at 0.722 where water asks 0.08 | MU2's build, stale |
  | 2 | no normal map at all against a relief of 1.00 | MU2's build |
  | 1 | an `OPAQUE` material with 12% of its alpha below the threshold | MU2's export |

  **The 46 and the 2 have one cause and it is dated.** MU2's `ROUGHNESS_FLOOR` used to be
  high enough to flatten the whole library — its own note records ArmorMale10 and Axe01 both
  running 0.722 to 0.749 across an entire sheet — and it was lowered on 2026-09-05. 103 of
  the 329 models in `assets/` were baked before that day and still carry it. `Potion01`,
  `Potion03`, `Antidote01` and `Axe02` are the clearest: every material on them measures the
  same 0.722, `glass` included, where the library asks 0.12. The repair is a rebuild of those
  103 in MU2 and a re-sync, not a change here.

  **The 26 have a cause too, and MU2's own library already names it.** `build_maps.py`
  multiplies the whole height field by the material's grain depth, and a material that
  declares no grain has depth 0 — so declaring none discards the relief read out of MU's own
  painting along with the grain that was not wanted. `leather.json`'s `grain_why` records
  exactly this, found on the Bone set and fixed for leather alone by giving it a `cast` grain.
  `skin`, `chitin`, `glass` and `fur` still declare none, which is why 15 of the 26 are `skin`.
- **65 notes**, all of them a material slot no primitive draws with. Nothing renders wrong, so
  they do not fail the run, but every one is a material MU2's exporter wrote and the mesh
  dropped, and it is where the `mu2` default hides.
- **Nothing is metal that should not be**, and every cooked texture carries the format and the
  mip chain its role demands. Those two were the cheapest things to get wrong and they are
  right.

## The baseline, and why there is one

Every one of those 90 failures is real and none can be fixed from here: all of them are repairs in MU2's
pipeline recipes followed by a re-export, and some are judgements nobody has made yet. A gate
that failed on all of them would be switched off within the week.

So `sheets/materials.baseline.json` records the known ones by slot and kind — not by measured
number, which moves whenever a sheet is retuned. The gate fails on anything new, **and on
anything recorded that has since been fixed**, so the file can only shrink. After a repair,
`--record`.

## Applying a ruling

Not built, and stated here so the wrong thing is not built later. A ruling in
`sheets/materials.json` is a statement of what ought to be; today it makes the audit fail on
that slot until the content is repaired, which is the honest state, because the wrong material
is still on screen.

Two ways to make a ruling take effect, and the choice is not obvious:

1. **Repair it in MU2's pipeline recipe and re-export the glb.** Correct at the source, and the
   right answer for a recipe that is wrong for a whole family of models. It costs a round trip
   through MU2's build, and this engine's rule is that nothing is read from `MU2/` at run time —
   only the cook's output — so the trip is real.
2. **Carry the override into the cook.** `u_material` already holds cutout in `x` and two-sided
   in `y`; `z` and `w` are free, and a roughness and metal override per material costs nothing
   to bind and nothing to shade. The cook would read `sheets/materials.json`, resolve each
   ruling to a library entry, and write the pair into the `.mum`'s material record. This fixes
   a single model's wrong choice without a round trip, and it is the only way to fix the ones
   whose repair is a look rather than a recipe.

Neither is free and neither is urgent: the audit's first job is to make the list visible and
stop it growing. Option 2 is the cheaper of the two and the one worth building first, but it
bumps the `.mum` version and touches `cook.py`, `cooked.cpp`, `mesh.cpp`, the renderer and
`fs_shade.sc`, so it wants a sprint's slot rather than an afternoon's.

What the override cannot fix is either of the two biggest findings. A flat normal map has no
detail to scale and no uniform puts it back, and a slot with no ORM map has nothing to override
— both are re-bakes in MU2's pipeline. The override's value is the third group: 46 roughnesses
that are simply the wrong number for a material that is correctly named.
