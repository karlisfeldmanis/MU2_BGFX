# Where the tree stands, and what is left

Written 2026-09-22, three days into the bgfx rewrite. `PLAN.md` is still the map and nothing
in it is withdrawn here; this file is the position on that map — what the 172 commits since
2026-09-20 actually landed, what the last two days did **outside** any sprint, and what the
remaining sprints are.

## In one paragraph

Sprints 0 to 8 of `PLAN.md` are done and each has its file under `docs/sprints/`. Since
sprint 8c closed on 2026-09-21 there have been **72 further commits with no sprint file
between them**: the content pipeline carried into this tree, the whole wardrobe and weapon
rack re-cooked and judged, Lorencia's night look, click-to-move and the camera, the cursor,
the item models, the hover ring, the save file, the level-up, the whole town's animation,
the sound, and the dragon's breath. That stretch is catalogued below as sprint 10 rather
than pretended into the earlier files. What is **not** in the tree is the frame round the
game: no character creation, no character select, no second map and no gate to walk through,
no `.app`, and no skills. That is sprints 9 and 11.

## Done: sprints 0 to 8

| # | sprint | file | state |
|---|---|---|---|
| 0 | Foundations — layers, CMake, bootstrap, log, shots, stats, budget gate | `00-foundations.md` | done 2026-09-20 |
| 1 | The frame — six views, PCSS shadow, prepass, SSAO, GGX shade, ACES present, MSAA | `01-the-frame.md` | done; sent back twice |
| 2 | The ground — height, tile grid, blended surfaces, `light.png`, MU's camera | `02-the-ground.md` | done |
| 3 | The town — placements, instancing, chunk culling, cutouts, the cook | `03-the-town.md` | done |
| 4 | Figures — body parts, rig, baked clips, crossfade, equipment, monsters | `04-figures.md` (+ census, QA) | done; sent back once |
| 5 | Rules I — tick, pathing, spawns, AI, 0.75 hit and damage, death, experience, headless | `05-rules-i.md` (+ census) | done; sent back once |
| 6 | The showing — transparent pass, landing cue, blood, numbers, effect pools, sound | `06-the-showing.md` (+ census) | done |
| 7 | Rules II and the windows — items, drops, bag, equipment, potions, shop, HUD | `07-the-windows.md` | done |
| 8 | Light and look — lamps and fires (8a/8b), the reflection probe and metal (8c) | `08a-the-lamps.md`, `08c-the-metal.md` | done |
| — | The ring — MU2's hover outline migrated off Godot | `09-the-ring.md` | done (misnumbered: it is not sprint 9) |

`09-the-ring.md` carries a sprint number that `PLAN.md` promises to something else. It keeps
its filename and is read as part of sprint 10 below; `PLAN.md`'s sprint 9 is still ahead.

## Sprint 10, written after the fact: the town played

The 72 commits from 2026-09-21 13:17 to now. They were not planned as a sprint and were not
one — they are a single arc all the same, and this is what it was, so nothing in it has to be
re-derived from `git log` again.

**The content pipeline moved in.** `source/`, `pipeline/` and `workshop/` carried over from
MU2 so the tree builds its own art without MU2 or Godot. `tools/asset.sh`, `cook_one.py`,
`content.sh`. `texcook` split across cores, because a six-minute cook is not a review loop.

**The wardrobe and the rack were re-cooked and judged, one at a time.** `build_maps` had been
laying the metal lift and the painted relief upside down on the art; every set built before
2026-09-21 carried it. Rebuilt and passed on studio sheets: Bronze, Scale, Sphinx, Pad, Vine,
Leather, Bone, Brass, Plate; the seven Lorencia swords, Dragon Lance, the axes and maces, the
bows, the crossbow, the quivers, the four staves, all twelve shields. `tools/studio.py` and
the viewer's stage — a bonfire, a lamp and a house round the subject, noon to night — are what
made that judgeable. An open helm is now worn over the head rather than in place of it, and
MU's `HideSkin` is on the item paths so a helm on a shelf has nobody in it.

**Lorencia got its look.** The default is a moonlit night with a little sepia, lit by its own
fires; the fog gone, the dust settled at 0.0055, the moon at 1.5, ambient 0.8, a
contrast-adaptive sharpen at 0.4 and a midtone contrast at the present. World metals
calibrated so the railings read as one iron, and a placement's light taken as the median of
the tiles round it.

**It became a game to hold.** Click to move, with MU2's marker on the land and no moonwalk;
walks pulled tight as `Route.Along` pulled them; spam-clicking answered from a stand; a click
answered on its own frame, the camera on a spring, the wheel zooming 3.5 m to 8. MU's own
cursor migrated. The HUD scaled down to 0.34. A preloader behind a spinner and the character
fading in. Damage numbers coloured by who took them, unequip taking the weapon out of the
hand, a drop no longer stacking on another's tile, the bag and the shelf drawing the real
item models, a tooltip over every window.

**The town moved and made noise.** All twenty rigged Lorencia objects animate — lamps, signs,
curtains, carriages, animals — the trees sway by wind and their own weight, the fountain falls
and sprays where its fall lands, lit windows slide, water flows, the merchant's animal carries
two lit lanterns. The townsfolk face where MU faces them and take turns among their own clips.
Sound: the monsters heard where they stand, the knight's swings and treads, Lorencia's air,
drops, coins, the bag, the windows as MuMain plays them, a miss heard, and only what the
camera holds audible at all.

**And the parts of a character's life.** The save file (v1), worn armour restored from it, the
potion bar, the level-up that rises and sounds, the monster's health bar and the tile over its
head, the hover ring, the Budge Dragon's breath and dust, experience paying double and a
monster leaving fewer things.

## What is not in the tree

The honest gap list, checked against the code rather than remembered:

- **No character creation and no character select.** `main.sh` makes one Dark Knight from
  `Cradle`'s rules at the door. `--class 1` gives an elf, still in Lorencia.
- **No second map and no gate.** `assets/cooked/noria/` holds `noria.mur` and nothing else —
  no land, no town, no figures cooked. `content::Tables` has `safeGate` (the safe zone's own
  rectangle) and no map-to-map gate at all.
- **No `.app` bundle.** The game is `build/mu2` launched by a shell script.
- **No skills.** The HUD's skill boxes are drawn empty on purpose; `PLAN.md` decided the
  Diablo 3 shape and nothing has been built toward it.
- **Drawn dim, as MU2 draws them**: the chat, the menu, the chaos machine, the vault.
- **Not built**: refining with jewels (the tables are transcribed for the tooltips only), luck
  and options on drops, the shield and ability bars, the buff strip, the fan.

## The roadmap

One sprint a session, one file under `docs/sprints/`, one sentence that proves it — the
working rules do not change.

| # | sprint | what it is | proved by |
|---|---|---|---|
| 9 | **The game whole** | character creation and select, the save file finished (bindings, the map a character stands on), the `.app` bundle | a packaged app that makes a character, hunts, quits and resumes |
| 11 | **Noria and the gate** | Noria cooked as Lorencia is, the gate tables, the walk between maps, the elf starting where an elf starts | a character walks out of Lorencia and stands in Noria, both inside budget |
| 12 | **The skills** | the Diablo 3 shape `PLAN.md` decided: learned permanently, dragged onto QWER, a real cooldown each, in the sim and on the HUD | four skills on the bar, one running its cooldown down, in a seeded headless log and in the window |
| 13 | **The debts** | the standing list below, worked as one pass rather than leaked into every sprint | the list is shorter and each line says how |

Sprint 10 is the stretch above and needs no file beyond this one. After 12 the backlog is
`PLAN.md`'s: refining and the chaos machine, summoning, Devias, the single player events worth
keeping, the vault, luck and options on drops.

**The order, and why.** 9 before 11 because a gate needs a character who persists across a map
change, and the save is what makes that mean anything. 11 before 12 because Noria is the only
thing that will show whether the frame holds on a map this one has never drawn, and a skill
built over a frame that turns out not to hold is a skill rebuilt. 13 last, but any line in it
that blocks the sprint in hand is done in that sprint and struck off.

## Standing debts

Carried out of the sprint files so they are in one place. None of these is blocking.

- **The per-view GPU timers cannot price a pass on Metal** (`docs/budget.md`). The wall frame
  is the only enforced number; the ring and the probe accounts stay partly unclaimed.
- **The probe's 0.3 ms has no account that gave it up** (`08c`), and the probe steps on every
  fourteenth frame on a walk — judged still, never walking.
- **Water reflects the probe at roughness 0.08 and has not been looked at** (`08c`).
- **The NaN meshes** (`08c`).
- **The ring is not gated on the pointer being clear of a window**, as MU2's was not, and the
  townsperson path is the one of three never confirmed in a picture (`09-the-ring`).
- **The panels scale with the screen**; MU2 draws them at a fixed twice in viewport pixels
  (`07`). **The disc does not brighten under the pointer** (MU2 modulates by 1.35).
- **`sheets/materials.baseline.json`** holds the material faults known on the day `matcheck`
  was written. The list may only shrink; it has not been worked deliberately since.
- **The shield whites out at `metal_gain` above 2.5 facing the low sun** — the user's eye
  decides whether the amount is wrong or only the kind.
