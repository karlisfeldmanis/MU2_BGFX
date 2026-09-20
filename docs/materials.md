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
| an ORM map at all | the engine's fallback is occlusion 1, roughness 1, metal 0 — a deliberate matte, not a right answer. A surface reaching it is unpainted |
| relief asked vs the normal map's measured lean | **the one the wings were found by.** See below |
| `MASK` with no holes in its alpha | a discard paid for in four passes that cuts nothing |
| `OPAQUE` with holes in its alpha | whatever was meant to be cut out is drawn solid; this is how MU's foliage breaks |
| cooked `.ktx` format per role | BC7-sRGB albedo, BC5 normal, BC7-linear ORM. An ORM read as sRGB turns roughness 0.62 into about 0.35 and makes the whole town glossy at once |
| mips in every cooked texture | `PLAN.md`'s mip rule, which is not optional |

The land is excluded, with a reason: it has its own shader, it blends two full material sets by
a per-vertex weight, and its surfaces come from a world's `ground_surfaces.json` rather than
from a glTF material slot. Auditing its slots against the library would measure the wrong
thing. `docs/conventions.md`, Materials.

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

- **147 slots (23%) name a library material.** The other 424 are named after MU's texture
  sheets or left at the exporter's `mu2` default. By area it is worse: 2% of the surface in
  `assets/` is wearing a material anything recorded a decision about. Most of those sheets do
  carry an ORM whose roughness sits on a library value, so a recipe did run — but the record of
  which one went with the name, and the numbers cannot give it back: six or seven library
  entries fit inside one tolerance between 0.7 and 0.9. Only 5 of the 424 can be identified
  from their ORM alone.
- **190 failures**, recorded as the baseline:

  | count | failure |
  |---:|---|
  | 102 | no ORM map at all, so the surface falls back to roughness 1 |
  | 46 | a baked roughness that disagrees with the entry it claims |
  | 26 | a flat normal map against a relief the library asked for |
  | 13 | a `MASK` cutout with no holes to cut |
  | 2 | no normal map at all against a relief of 1.00 |
  | 1 | an `OPAQUE` material with 12% of its alpha below the threshold |

  The 102 include the two largest surfaces in the content, `Tree02/Tree_a` and
  `Tree01/tree_a`. Of the 46, `Potion01`, `Potion03`, `Antidote01` and `Axe02` are the
  clearest: every material on them measures the same 0.722, so one recipe was baked over a
  model that declares three — `glass` at 0.722 where the library asks 0.12 is a polished orb
  rendered as dry plaster.
- **65 notes**, all of them a material slot no primitive draws with. Nothing renders wrong, so
  they do not fail the run, but every one is a material MU2's exporter wrote and the mesh
  dropped, and it is where the `mu2` default hides.
- **Nothing is metal that should not be**, and every cooked texture carries the format and the
  mip chain its role demands. Those two were the cheapest things to get wrong and they are
  right.

## The baseline, and why there is one

Every one of those 190 failures is real and none can be fixed today: most are repairs in MU2's
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
