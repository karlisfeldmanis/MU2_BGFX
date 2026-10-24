# The Armory

An item, monster, NPC, quest, event, rune, skill, talent, mix and Elite database on mu-classic.com,
built from the game on every release. The brief is
`tools/armory/prototype/ARMORY_HANDOFF_PROMPT.md`; the design is
`tools/armory/prototype/design/Main.dc.html` (claude.ai/artifact/Gre39R3PX3sVpDv6R5JZAE).

**The game is the source of truth.** Nothing on the Armory is typed by hand. Where a rule is C++,
the build links the game's code and calls it; a Python mirror is allowed only where that is
impractical, and is then checked against the code's output.

## What the build reads

The release players have, never HEAD:

- **The commit.** `https://mu-classic.com/game/<platform>/manifest.json` `version` (all three
  platforms must agree; `--commit` overrides for a local look). Its tree comes from `git archive`
  into `out/armory/src/<commit>/`, as `dist.py snapshot()` does, with `third_party/extern` linked.
- **The tables.** The release's own packs, `dist/macos/MU2.app/Contents/Resources/data/*.mupak`,
  mounted the way the game mounts them (`core/pack.h`, as `mupack --check`). `dist/macos/BUILT_FROM`
  must name the released commit, or the build stops and says so.
- **`content/`** from the same snapshot: items, monsters, nests, npcs, materials, textures, quests.

## The parts

| Part | Where | Makes |
|---|---|---|
| Dump | `tools/armory/dump/*.cpp`, CMake target `armory_dump` (EXCLUDE_FROM_ALL), built in the snapshot | Everything a C++ rule decides: cards (`describe()`), prices (`npcValue`), skills, talents, gear powers, rune odds, quest rewards, NPC services, per-kill drop odds |
| Odds check | `armory_dump --check-odds` | Runs the real `Realm::leave` many times for sample monsters and fails if the tallies leave the analytic odds' confidence band |
| Extractors | `tools/armory/*.py` | What is data, not rule: item rows, monster nests, maps (`Minimap::bakeChart` geometry), thumbnails, icons |
| Thumbnails | `tools/armory/models.py`, `thumbs.py`, `thumbs/` | Models converted to GLB in `out/armory/cache/glb/` only (never published), drawn offscreen by three.js in headless Chrome as transparent WebP thumbnails; Elites in the game's gold (`kEliteLight`, `kEliteChrome`). A figure stands as the game stands it (figures.cpp's idle, first key; held things on their grip bone), without what the game never draws (show.beam_material, hidden_part), framed on what is drawn with a 7% margin, figures on one ground line; `tests/thumb_check.py` fails a cropped or tiny one |
| Build | `tools/armory/build.py`, `./mu armory build` | `out/armory/<commit>/`: per-kind indexes and per-entity files, each stamped with commit and protocol. Also a lane of `./mu ship prepare`, after the macOS dist |
| History | `tools/armory/history.py` (run by the build) | The diff of one release's output against the previous release's, per entity: was/now. Optional per-release note in `tools/armory/notes/<commit>.md` |
| Test | `tools/armory/tests/armory_test.js`, `./mu armory test` | The output's contract, the design's page logic on the built rows, the spot checks below |
| Site | `web/armory.py` (server routes) and `web/site/armory/` (client: `armory.js`, `card.js`, `armory.css`) | Stable URLs per entity, server-rendered head for links and search, thumbnails, comments; `tests/card_check.js` holds the card to the game's renders, `tests/site_test.js` the pages |

The drop odds: a kill's outcome is several independent dice and pools drawn inside
`Realm::leave`. The dump computes each item's per-kill chance analytically in C++, from the
game's own predicates and constants (`dropRateOf`, `rulesOf`, `dropsFromMonsters`,
`excellentable`, `secondClassOnly`, the `k…Odds`). The odds check is what keeps that honest when
`leave()` changes.

## Output layout (`out/armory/<commit>/`)

```
meta.json                      commit, protocol, built, the release before it, what the dump left out
index/<kind>.json              the list page's rows only (items, monsters, npcs, worlds, quests, ...)
<kind>/<slug>.json             one entity: everything its page shows
cards/<slug>.json              its card rows (+0, +11 exc, ...) for tooltips
thumbs/<hash>.webp             512 px, transparent, content-addressed (redrawn only when its model,
thumbs/<hash>.96.webp            or the renderer, changes); the 96 px one is the card's top-right picture
icons/<hash>.png, maps/<world>.png
history/<kind>/<slug>.json     the releases that changed it, was/now per field (per kind: two kinds
history/releases.json            may share a slug); the releases with their hand notes
search.json                    name, kind, slug for everything
```

No models are published: the Armory shows thumbnails, not a 3D viewer (the user, 2026-10-24).
Slugs come from English names (names are not translated) and keep what the release before gave
them; a name two things share gets the content key after it (`red-skeleton-knight-redskeleton01c2`).

## Live, or not dropping yet

Read from the game's flags, never a kept list: `kAncientLevel`, the map rules
(`rulesOf(map)`), whether a world is in the release's packs. Built-but-dormant things are marked
`"live": false` and shown as "not dropping yet".

## Decided

- Thumbnails only, no 3D viewer and no GLBs served (2026-10-24).
- Names stay English (2026-10-24).

## Undecided (ask the user)

- Deploying: only when asked, `web/deploy.sh`.

## Spot checks (done means these hold)

| Check | Expected |
|---|---|
| Sword of Destruction +11 excellent | 148 ~ 156 damage, Strength 692, Agility 272, sells for 604,100 |
| It, from a Blood Castle 4 Red Skeleton Knight | about 1 in 263 per kill |
| Loch's Feather in Tarkan | about 1 in 520 |
| Talents at level 400 | 79, +80 per rebirth (MG 111/+112) |
| Lorencia Elite | ×16 life, ×2.5 damage |
