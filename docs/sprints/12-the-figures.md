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
| `Died`, on the hero | `You Died` in the middle of the screen, dressed as the map name is |
| the mana a swing returns | nothing: it is every swing, and the gem already shows it |

Two of these are decisions and not transcriptions, and both are the user's:

* **A critical is a step on the ramp, not an event.** No ring, no flash, no shake. With the luck
  option rolled it lands several times a second, and anything that announces itself would be the
  loudest thing on screen for a whole hunt.
* **Nothing announces a level.**

OpenMU's *excellent* damage arm is not ported and is not styled. It would add a draw and
desynchronise every seeded log (`src/sim/rules.cpp` says so at the branch).

## The style, in one paragraph

Size is force, hue is kind, and there is nothing else. **Cinzel** — the map name's own family,
Bold for the fight and the arrival's exact Medium for the lane — at 21 interface units for a
swing, 27 for a skill, 32 for a critical, 15.5 for the shield's share and 12 for a miss. No
outline, no bloom, no tilt: a Gaussian halo baked from the same face, plus a 1.6-unit hard drop
under the figures alone, which is what carries a Roman capital over Lorencia's paving and its
night grass. No tracking on a figure: a number is one object, and tracked, 27 reads as 2 7. A
figure pops to 1.08 and settles inside a quarter of its 0.95 s, rises 62 units, leans a random
±13 and holds full opacity for two thirds of its life. Every one of those lengths is in screen
pixels on a point projected from the world, so a figure never grows as the camera comes in and
never slides when it turns.

The design page drew B2 in a condensed grotesque, chosen for width — Cyclone catches everything
within a tile and puts five figures across it. The user replaced the family on sight of the
first build: the fight has to speak in the same voice as the map name. The width that bought is
paid back by a point off every size and by the stack rule below.

**The gain lane** is Cinzel Medium at nine units, one size and one weight for every row —
experience, Zen and a potion are one kind of thing and are said one way — with the unit word at
6.5 units in tracked capitals at 60% of the row's own ink. Its halo is drawn at half strength:
a blur baked for a 42-unit figure, laid under a 9-unit one, is a dark band behind the words
rather than air round them.

**The death** is not in the lane at all — it is said in the middle of the screen, at 44% of the
height, because it is the one thing the game says that stops the hunt. `You Died` in the
arrival's off-white
under the arrival's own furniture — its soft-and-tight black halo pair and its hairline rule
drawing outward from a diamond, at the arrival's own numbers (its design page is in pixels at
1080 lines and an interface unit is two of them, so they come across unconverted) — over a soft
red scrim behind the **whole block**, which is the arrival's own cloud in a different colour.
The red was a per-glyph glow first and the user corrected it: lighting each letter read as the
word glowing, where a cloud behind the block reads as the screen darkening, which is what it is.
It fades in and out and does nothing else. The map name's own entrance was built on it first —
letters closing from 0.30 em, the rules drawing out, the diamond turning in — and the user cut
all three: the arrival introduces a place and can take a moment over it, where a death is
already true when it is said, and letters arranging themselves while he lies there reads wrong.
Not shouted, either: Cinzel has no lowercase, so `You Died` comes out as a capital and small
capitals, at 24 units — the map name's own size and a third.

**And the world goes grey behind it.** `Renderer::setDrain` multiplies the sheet's own
saturation in the present pass, half a second out while he is down and a second back as he gets
up (`PlayMode::frame`). It is a renderer setting and not a grade in `lighting.json` for one
reason: the present pass is the WORLD's, and the interface is drawn after it — so the HUD he is
reading, and the message over it, keep their colour while everything he was fighting loses it.

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
  it, because nothing needs to yet. The HUD's own experience tooltip lands on the same three
  square inches when the pointer is over the bar, and the two overdraw each other.
* **A bake that will not pack fails quietly and takes its neighbours with it.** Cinzel's halo at
  a 512 atlas overflowed `stbtt_PackFontRanges` -- a halo reserves three sigmas of padding round
  every glyph, which Barlow fitted and Cinzel did not -- and because the four bakes were chained
  with `&&`, the gain lane silently got no face and stopped drawing while the figures drew on
  with no halo. The atlases are 2048 and each bake is judged on its own now.
