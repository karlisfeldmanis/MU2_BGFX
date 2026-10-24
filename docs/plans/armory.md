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
| Extractors | `tools/armory/*.py` | What is data, not rule: item rows, monster nests, maps (`Minimap::bakeChart` geometry), models (GLB), icons |
| Build | `tools/armory/build.py`, `./mu armory build` | `out/armory/<commit>/`: per-kind indexes and per-entity files, each stamped with commit and protocol |
| History | `tools/armory/history.py` | The diff of one release's output against the previous release's, per entity: was/now. Optional per-release note in `tools/armory/notes/<commit>.md` |
| Site | `web/armory/` (server routes) and `web/site/armory/` (client) | Stable URLs per entity, server-rendered head for links and search, three.js viewer, comments |

The drop odds: a kill's outcome is several independent dice and pools drawn inside
`Realm::leave`. The dump computes each item's per-kill chance analytically in C++, from the
game's own predicates and constants (`dropRateOf`, `rulesOf`, `dropsFromMonsters`,
`excellentable`, `secondClassOnly`, the `k…Odds`). The odds check is what keeps that honest when
`leave()` changes.

## Output layout (`out/armory/<commit>/`)

```
meta.json                      commit, protocol, built, the release before it
index/<kind>.json              the list page's rows only (items, monsters, npcs, quests, ...)
<kind>/<slug>.json             one entity: everything its page shows
cards/<slug>.json              its card rows (+0, +11 exc, ...) for tooltips
models/<hash>.glb              content-addressed, rebuilt only when its sources change
icons/<hash>.png, maps/<world>.png
history/<slug>.json            the releases that changed it, was/now per field
search.json                    name, kind, slug for everything
```

## Live, or not dropping yet

Read from the game's flags, never a kept list: `kAncientLevel`, the map rules
(`rulesOf(map)`), whether a world is in the release's packs. Built-but-dormant things are marked
`"live": false` and shown as "not dropping yet".

## Undecided (ask the user)

- Serving WebZen's models and textures publicly: thumbnails only, lower resolution, or full GLBs.
  Before the 3D viewer goes live.
- Translating names (the site has eight languages).
- Deploying: only when asked, `web/deploy.sh`.

## Spot checks (done means these hold)

| Check | Expected |
|---|---|
| Sword of Destruction +11 excellent | 148 ~ 156 damage, Strength 692, Agility 272, sells for 604,100 |
| It, from a Blood Castle 4 Red Skeleton Knight | about 1 in 263 per kill |
| Loch's Feather in Tarkan | about 1 in 520 |
| Talents at level 400 | 79, +80 per rebirth (MG 111/+112) |
| Lorencia Elite | ×16 life, ×2.5 damage |
