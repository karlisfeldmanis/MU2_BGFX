# The figures over the fight

Built 2026-09-23, off a design page the user chose from: **concept B2, "weighted by the blow,
flat"**. Three concepts were drawn over a real Lorencia night shot — A, MU's own bitmap digits
sharpened; B, the Diablo reading, size for force and hue for kind; C, the Path of Exile reading,
small tabular figures stacked and labelled — and B was picked and then flattened at the user's
word, in three variants of its own. B2 is the condensed one.

## What the fight can say, and what it says

The page began with an inventory of every event the realm already publishes, because a number
that has no event behind it is a number somebody has to invent. Read off `src/sim/realm.h`:

| said by the realm | shown as |
| --- | --- |
| `Hit` on a monster | the damage, bone (a swing) or amber (a skill) |
| `Hit` on the hero | the damage, red — whatever threw it |
| the shield's share of that | the absorbed part, blue and small, in the row above |
| `Missed` | the word, pale on him and grey on anything else |
| `Blow::critical` | gold, one size up. **Unreachable**: 0.75 grants criticalChance from the luck option alone |
| `Gained` | `+45 EXPERIENCE` in the lane over the HUD |
| `Picked` with no bag slot | `+1,340 ZEN`, summed over a second and posted once |
| `Drank` | `+60 LIFE` or `MANA`, in the same lane |
| `Levelled` | **nothing.** The bar fills and the card says it; a banner stops the hunt |
| the mana a swing returns | nothing: it is every swing, and the gem already shows it |

Two of these are decisions and not transcriptions, and both are the user's:

* **A critical is a step on the ramp, not an event.** No ring, no flash, no shake. With the luck
  option rolled it lands several times a second, and anything that announces itself would be the
  loudest thing on screen for a whole hunt.
* **Nothing announces a level.**

OpenMU's *excellent* damage arm is not ported and is not styled. It would add a draw and
desynchronise every seeded log (`src/sim/rules.cpp` says so at the branch).

## The style, in one paragraph

Size is force, hue is kind, and there is nothing else. One face — Barlow Semi Condensed Bold,
`extern/`, pinned in `bootstrap.sh` — at 23 interface units for a swing, 30 for a skill, 35 for
a critical and 17 for a miss or the shield's share. No outline, no bloom, no tilt: one Gaussian
halo baked from the same face, black, and a 1-unit drop, which is what carries a figure over
Lorencia's paving and its night grass. A figure pops to 1.08 and settles inside a quarter of its
0.95 s, rises 62 units, leans a random ±13 and holds full opacity for two thirds of its life.
Every one of those lengths is in screen pixels on a point projected from the world, so a figure
never grows as the camera comes in and never slides when it turns.

The condensed face is the reason B2 was chosen over B1 and is not a taste: Cyclone catches
everything within a tile and puts five figures across it at once, and a normal-width face has
them touching.

## Where it lives, and why it moved

MU drew its damage out of `Data/Interface/FontTest.OZT` — ten 16-pixel cells and the word
`Miss` — as camera-facing quads in the transparent pass. A sheet of ten cells cannot carry a
ramp: one size, one weight, one word. So the figures moved into the interface:

* `src/game/fx/showing.{h,cpp}` still owns them — it anchors a figure over the body a blow
  landed on (MU's own flat 140 units above the feet), stacks it a row higher if another is
  already standing there, and ages it. It knows nothing about how it looks. The **blood did not
  move**: it is still MU's sprites in the transparent pass.
* `src/game/ui/tally.{h,cpp}` draws them, and the gain lane with them, in a real face over the
  world — the same division the monster's health plate has had since sprint 6.

Two things the sim now says that it did not:

* `Happening::critical`, off `Blow::critical`. It takes no draw, so no seeded log moves, and
  nothing can set it true until an item rolls luck.
* Nothing for the skill: `Swung` already carries the skill's number, so the drawing keeps it on
  the swing (`Drawn::swingSkill`) and the blow that settles two ticks later reads it back.

The shield's share is not said by the sim either and does not need to be: the realm puts nine
tenths of a blow onto the pool and overflows the rest into health, so the difference between
what was rolled and what came off health **is** the shield's share. It is worked out in
`Play::update`, where the health before the blow is still known, and never on a killing blow —
there the floor at nought makes that difference overkill instead.

## The stack rule, which the page did not have

Taken from concept C after the first run: two spiders missing him on one tick drew `MISS` over
`MISS` at the same pixel. A figure counts the figures already anchored within 0.6 m and put up
in the last 0.3 s, and is lifted a row for each. Up to four rows; a Cyclone in a nest stops
there.

## Open

* **The skill and critical branches have not been seen in a picture.** The arena harness has no
  way to put a skill on a key (`--ui-skill` presses a key, and a fresh character's bar is
  empty), and a critical cannot happen at all. Both are two lines off the path that was seen.
* **Zen's roll-up has not been seen either** — a pile has to be walked over, and the arena's
  hero does not.
* The lane's own place is the HUD plate's top edge; nothing re-lays it when a window opens over
  it, because nothing needs to yet.
