# Build the MU Classic Armory (item database) for mu-classic.com

You are implementing the Armory: a WoW-Armory / Wowhead-style database on the MU Classic website. It covers every item, monster, NPC, quest, event, rune, skill, talent, Chaos Machine mix and Elite, with drop rates, 3D models, map positions, item-update history and player comments. A finished, clickable design exists, and so do working prototypes of every data extractor. Your job is to make it real, in the repo, built from the game itself on every release.

Read `MU2_BGFX/CLAUDE.md` first and follow it.

## The one rule: the game is the source of truth

Every number, name, rule and line of text on the Armory comes from the game's own content and code, at build time. Nothing is typed by hand. Nothing is copied from docs, wikis or memory notes, which go stale; the agents that built the prototypes found several notes already wrong.

- Read `content/` (items, monsters, world nests and npcs, materials, textures, quests' voice folders) and the cooked tables.
- Where the rule lives in C++ (`src/sim`, `src/game/ui`), compile and call the game's own code from a small dump program rather than re-implementing it in Python. Drop chances, card text, prices, skills and talents all have real functions: `Realm::leave`, `describe()`, `sim::npcValue`, `affixValue`, the talent table and more. A Python mirror is allowed only where calling the code is impractical, and must then be checked against the code's output.
- When the code and a doc disagree, the code wins. Write the disagreement in the generator's notes.
- When the game changes, a rebuild changes the site. No step of the Armory build may be "remember to update X".

## What already exists (start here, don't redo it)

**Design** (the target look and behaviour, click through it): https://claude.ai/artifact/Gre39R3PX3sVpDv6R5JZAE (canvas file `project/Main.dc.html`). It is a single-page prototype. Its page logic is in the `<script type="text/x-dc">` block and can be reused for the real site's client code.

**Prototype generators.** They are in a session scratchpad today, so copy them before that folder is cleaned: `/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/d6e1fce9-2bd5-49b8-90d8-f890ee0f6e84/scratchpad/`

| Script | Output | Notes |
|---|---|---|
| `gen_db.py` | `db.json`: 401 items, stats from `content/items/*/*.json` `stats` | Records which release added each item (git commit vs release commits) |
| `gen_sources.py` | `sources.json`: per item, the monsters with per-kill `chance`, shops, crafts, used_in, quests, events, boxes | Mirrors `Realm::leave` (`realm_items.cpp`), elite spoils, the raid hoard, box tables; `_rates` and `_notes` record what is simplified |
| `gen_monsters.py` | `monsters.json`: combat stats plus where each monster nests | |
| `gen_models.py`, `pack_models.py` | 541 GLBs: items, monsters, NPC figures | Texture lookup as `pipeline/asset.sh`; Y-up, metres, centred; cut-out = MASK, glow = additive |
| `gen_maps.py` | world charts drawn like `Minimap::bakeChart` | Tile to pixel: `px = (x+0.5)*2`, `py = (y+0.5)*2`, x = column, no flip |
| `gen_npcs.py` | `npcs.json`, `quests.json` | Roles and services follow `Realm::talk`; shelves from `market.cpp`; quests from `quests.cpp` |
| `gen_systems_a.py` | `runes.json`, `consumables.json`, `mixes.json`, `elites.json` | |
| `gen_systems_b.py` + `sysb/dump.cpp` | `skills.json`, `talents.json`, `gear_powers.json`, icons | **Compiles the sim and calls it.** This is the pattern to follow everywhere |
| `gen_cards.py` + `cardx/cards.cpp`, `cardx/build.sh` | `cards.json` (item cards from the game's own `describe()`), `card_style.json`/`.md`, `card_grain.png` | The site's tooltips must look exactly like the game's; this is how |
| `harness.js` | runs the page logic headlessly against the real JSON | Use it as the start of the site's tests |
| `card_compare.png` | the web card next to an offscreen game render | |

## What to build

1. **Move the extractors into the repo** as one tool, e.g. `tools/armory/`, with a single entry point (`./mu armory build`), and fold it into `./mu ship`.
   - Where possible, replace Python mirrors with calls into the game's code, the way `sysb/dump.cpp` and `cardx/cards.cpp` already do: drop chances, prices, wing and pet powers, rune odds, quest rewards, NPC services. Build the dump program through the project's CMake, so it always compiles against the current sources and fails loudly when they change shape. Don't parse C++ text with regexes when you can link it.
   - Each output carries the git commit and the protocol it was built from.
   - The build is incremental: only re-convert models or icons whose source files changed.
2. **Build only what is live.** The Armory shows the release that players have, not HEAD.
   - Build from the commit `./mu release` shipped.
   - Things built but not dropping, like Kanturu's 380 gear and Ancient runes, are shown as "not dropping yet" or hidden. Read that from the game's own flags (`kAncientLevel`, the map rules), not from a list you keep.
3. **Update history, automatically.** For each release, diff the generated data against the previous release's data: stats, card rows, drop rules, prices, rates. An item page lists only the updates that changed it, with a was/now for each change. Add a small optional hand-written note per release for the "why". The diff itself is never hand-written.
4. **The website** (`MU2_BGFX/web/`: Python stdlib `server.py`, static `site/`, Caddy, `web/deploy.sh`).
   - Pages: Items (list with filters and sort, item page), Monsters, NPCs, Quests, Events (Blood Castle 1–6, Devil Square 1–4, Golden Dragon), Runes, Skills, a Talent calculator, Chaos Machine, Elites. Match the prototype's structure.
   - Each entity gets a stable URL (`/armory/item/sword-of-destruction`, `/armory/monster/...`), server-rendered or statically generated, so links work in Discord and in search engines.
   - Split the data per entity or per kind. A page must not download 1.5 MB of sources or 14 MB of models to show one sword.
   - Keep the live site's header and footer. The header is clean, with no hero image. Use the site's palette and fonts (`web/site/site.css`).
   - Centre text in pills and buttons with `text-box: trim-both cap alphabetic`, as `site.css` `.live` does.
   - Content is up to 1600px wide, centred, with 20–64px side margins.
5. **Tooltips identical to the game's.** Render `cards.json` rows with `card_style.json`: 346px, its fonts, inks, marks, foot and sell strip.
   - The card's 96px top-right picture should be a small render of the item's own model. That is still to do.
   - Keep a visual regression check: render a few cards in headless Chrome next to offscreen game renders, as `card_compare.png` does, and fail the build on drift.
6. **3D viewer.** Use three.js with the GLBs: one file per model, loaded on demand and cached. Handle additive materials with AdditiveBlending, and add a RoomEnvironment for reflections. Elites are tinted gold, as in game.
7. **Comments.** Comments are stored against site accounts (`/var/lib/mu2-web/accounts.db`, the existing auth). Each comment can be tied to the item or to one of its updates. Add upvotes, a rate limit, reporting, and admin delete; `role=admin` exists already.
8. **Search across everything:** items, monsters, NPCs, quests, runes and skills.

## Ask the user before

- Anything that touches the live site or servers. Deploy only when asked (`web/deploy.sh`), and push only when asked: this subtree to its own remote, never origin.
- Serving WebZen's models and textures publicly. This is undecided. Options are thumbnails only, lower-resolution textures, or the full GLBs. Raise it before the 3D viewer goes live.
- Whether names are translated. The site has 8 languages; item names may stay English.

## Working rules (from the repo; they bite)

- Commit straight to main with explicit paths (`git commit -F - -- <paths>`), after `git diff <file>`.
- Never `git add -A` and never `git stash`. Other sessions keep uncommitted edits in shared files.
- Don't use all cores: use `nice` and about 6 of 10 threads. One heavy job at a time.
- Don't open the game client to look at your work. Offscreen and muted runs with a scratch save are fine for reference renders.
- `content/` is the truth. The Armory only reads it.

## Done means

- `./mu armory build` rebuilds everything from a clean checkout of the released commit, with no hand-kept data. Its output passes the headless page tests and the card visual check.
- Spot checks match the game, against the prototype's verified numbers:

  | Check | Expected |
  |---|---|
  | Sword of Destruction +11 excellent | 148 ~ 156 damage, Strength 692, Agility 272, sells for 604,100 |
  | It, from a Blood Castle 4 Red Skeleton Knight | about 1 in 263 per kill |
  | Loch's Feather in Tarkan | about 1 in 520 |
  | Talents at level 400 | 79, +80 per rebirth (MG 111/+112) |
  | Lorencia Elite | ×16 life, ×2.5 damage |

- Changing a number in the game and rebuilding changes that number on the site, and adds an update entry to the affected items, without touching any Armory file.
