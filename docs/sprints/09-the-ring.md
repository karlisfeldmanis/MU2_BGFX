# The ring: MU2's hover outline, migrated off Godot

Written 2026-09-21. The gold silhouette round whatever the pointer is over — a monster, a
townsperson, or a thing lying on the grass — carried over from `MU2/client/core/Outline.cs`
and its `outline.gdshader`.

## What was kept, and why it is a silhouette

The design is MU2's and none of it was re-litigated: the ring is a **measured shape**, drawn
by rendering the hovered thing a second time into a mask of its own and painting every pixel
that is *near* that mask without being *in* it.

The alternative — the inverted hull, drawn again slightly larger with flipped faces — is one
material and no second render, and it is wrong here twice over. Its thickness is a length in
the world, so the ring is fat on a spider at your feet and invisible on one across the square;
and it grows every vertex along its own normal, so wherever the mesh has a hard crease the
shell tears open, which on this content is everywhere. A count of pixels is what "the same
thickness whatever the distance" actually means.

The tuned numbers came over unchanged, because they were measured in MU2 rather than guessed:
width 2.6 px, feather 0.9, the shadow at 0.5 with a drift of (2, 3) and a spread of 4, and the
gold at (1.0, 0.78, 0.28).

## What is this engine's own

| Godot | here |
|---|---|
| a `SubViewport` resized in 64-pixel steps to the box | one fixed 512-square R8 target, never reallocated; a box past the cap is clipped rather than losing precision |
| `Camera3D.FrustumOffset` for the off-axis crop | `bx::mtxProj`'s asymmetric form, bounds scaled by the near plane |
| a 20-bit visual layer the mask camera alone sees | the SAME `Drawable`s the frame already posed, handed to a second list (`Play::gather`'s `hover`, `Litter::gatherOne`) and submitted again |
| a `CanvasLayer` at index −1 | views 16 and 17, between the present pass and the HUD |

Nothing is re-skinned for the mask: the pose is composed once and the second render reads the
same palette row, which is the point Outline.cs makes about "the same mesh, in the same pose,
because it is the same mesh".

The ring composes **after** the tonemap and **before** the HUD, so its gold is a display
colour rather than linear radiance added into HDR, and a window drawn over a ringed monster
still covers it.

## Which thing is ringed

`Play::leftClick`'s own ladder, so that the ring shows what a click would answer: a
townsperson first, then a thing on the ground where no body is nearer, then a monster. The
shadow under the ring is a dropped item's alone — a monster and a townsperson stand on a cast
shadow of their own, while an item lies flat against the grass and has none worth seeing.

## The four bugs this cost, all of them found by looking at the mask

Written down because three of the four are invisible in code review and obvious in a picture.

1. **The frustum bounds are near-plane coordinates, not bare tangents.** `bx::mtxProj`'s
   asymmetric overload computes its scale as `2*near/(rt-lt)`, which only reduces to the plain
   `tan(fovy/2)` form when the tangent has already been multiplied by the near plane. The
   symmetric `fovy` overload never goes through that path at all. Passed unscaled the frustum
   is wrong by a factor of `1/near`, which is invisible at near 1 — and this project's near is
   0.05. Both hand-checks that "confirmed" the sign convention had used near 1 and a 45-degree
   angle, where `tan`, `cot` and the scaling all coincide; neither could have caught it.
2. **The mask's fade must be 1, not 0.** The instance's `i_data5.y` is the figure's fade;
   `vs_skinned_depth` sends `2 + fade` and `fs_shadow` dithers the fragment away below 1. At 0
   it discards *every* pixel, so the mask came back empty and the ring drew nothing — with
   four valid submits, a valid program and a valid target, all of which checked out.
3. **A fixed target is only partly written.** The box fills its own corner of the 512, so the
   ring's `v_texcoord0` had to be scaled into that corner rather than across the whole texture.
4. **And only partly cleared.** Everything past that corner still holds *the previous frame's
   silhouette*, and the ring's search reaches a couple of texels out — far enough to find it
   and draw a stroke along the box's edge from a shape no longer there. That was the straight
   gold line under a hovered monster. The sampler's own CLAMP is no help: it holds at the edge
   of the 512, not at the edge of what was drawn. Every tap is now clamped to the drawn corner,
   which reads the cleared margin the box's own fit guarantees exists.

## Proved by

Lorencia, 1080p, Release, vsync off, `--click-every 1` driving the pointer onto its target:

- a **Lich** ringed through hood, robe and staff, its ring closing round the figure and the
  weapon in its hand as one shape;
- a **Spider** — the case Outline.cs names, under a hundred pixels across — ringed leg by leg,
  which is the thing an inverted hull cannot do;
- a dropped **Sword of Assassin** ringed on the grass under its own label.

The frame over those runs measured **3.94–4.04 ms median** against the enforced 5.5, the same
as the runs before the ring existed: it draws only while the pointer is over something, into a
target a few hundred pixels on a side.

## Owed

- The ring is **not gated on the pointer being clear of a window**, exactly as MU2's was not —
  only the click a monster answers to is. If that reads wrong in play it is one condition.
- The **townsperson** path is the monster path with an index in place of an id, and it is the
  one of the three not confirmed in a picture: the scripted pointer only aims at monsters and
  at loot, so nothing headless can drive it onto a merchant.
- The per-view GPU timers cannot price this pass on Metal for the reason `budget.md` already
  gives; the ring's own number is the wall frame above, and the account line in `views.cpp`
  stays unclaimed until something can measure it on its own worst case.
