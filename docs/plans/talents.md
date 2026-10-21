# Talents: the build plan

**Status, 2026-10-21: building.** The user: "lets starst to implement". Resets are still open, so the
small caps stay at 10 for now.
- Done in the sim: phase 1 (2fa16e44), phase 2 (93d8c7e0, protocol 45, layout 10), phase 3 (5cddd7d2),
  phase 4 (e689b1ce), and the bots' builds (b435df62, a live lockstep run on a private port agreed).
  A hero with no talents plays line for line as before.
- Not yet: the window, button, key, sound and animation (phase 5, in progress); the buff strip's
  cells for the timed states; the client's skill cells reading `sim::manaCostOf`; the jewel and item
  counts in `economy.log`; the measured A/B; reading a copy of the live `characters.db` (the read of
  the box was refused by the session's permissions, so it waits for the user).
- Small things left out on purpose: Patron does not lift an elf's ward on a party member (only
  Blessed Hands does), and Frost Edge and Icy Shot chill the next body the swing lands on, once. Until 2026-10-20 the patch was held ("this will be a big patch, so we are
still holding for talents update ... you can do some preparations without real commits"). The prepared
files were:

**Keep the drafts out of `src/sim/` until the patch starts.** `net::rulesPrint` hashes every
`src/sim/*.h`, `*.cpp` and `*.inc` (`src/net/rules_print.cmake`, CMakeLists.txt's
`MU_RULES_SOURCES` glob). A loose file there changes the rules print of every build from this tree,
and the server then refuses those clients. That happened for an hour on 2026-10-20, until the drafts
moved here.

| File | What it is |
|---|---|
| `docs/plans/talents.md` | this plan |
| `docs/plans/talents/src/sim/talents.h`, `talents.cpp` | the rules: points, the gate, take-back, validation, sums. They move to `src/sim/` when the patch starts |
| `docs/plans/talents/src/sim/talents_table.inc` | the 225 talents, generated from the canvas |
| `pipeline/talents_table.js` | the generator: `node pipeline/talents_table.js <Passive.dc.html> <talents_table.inc>` |
| `tests/talents_check.cpp` | the standalone check |
| `tests/talents_bench.cpp` | the damage bench |

The design is the Talents canvas, https://claude.ai/artifact/RTLpPhd7Prn4G6eX86FCaL. The tree data is
in its `project/Passive.dc.html`, and the button boards are on the row beneath. Change the canvas
first, then generate the table again. The generator stops on any talent it cannot map to a sim stat.

A standalone check compiled and passed on 2026-10-20 (`clang++ -std=c++20 -I docs/plans/talents/src -I src
tests/talents_check.cpp docs/plans/talents/src/sim/talents.cpp`). It covers points by class and level, the gate, take-back, invalid ranks, and
the sums by hold, skill and element. When the patch starts, move it into `tests/sim_test`.

## What is decided

- **Shape:** six columns a class, seven for the Magic Gladiator. Each column has three tiers of two
  small talents (0-10 points) leading to one major (1 point). Fortune is the last column in every class.
- **Gate:** a major opens at 10 points in either small above it. The next tier opens when the major
  is taken. A point comes back only if nothing below it closes.
- **Points:** a talent point for every 10 stat points earned. That is 199 at level 400, and 279 for
  the Magic Gladiator.
- **Never:** keystones, drawbacks, movement speed, cooldown reduction, double damage, stun, a dodge
  roll, skill area as a stat, or AG.
- **Runes:**
  - Element and plain damage talents raise rune blows, as element runes already do.
  - Skill-named talents raise only their skill.
  - Rune blows and Arcane Echo never roll talent procs.
  - Poison and chill talents act on the spells only, never on the percent-of-life pulses of the
    plague and burn runes.
  - Talent block counts under Steadfast's 25% cap (`rules.cpp:322`).
- **Procs** roll once per swing or cast, never once per body struck.
- **Buff icons** for the nine on/off talents are picked by cell; see the memory note
  `talent-buff-icons`.
- **Opening Talents:**
  - **Button C2:** a framed master icon on the character window's top-left edge, with the
    unspent-points count and "Talents" beside it. It goes with the character window.
  - Opening Talents hides the other column windows, and closing it brings back what was open.
  - **Key: A**, MU's own master-tree key since Season 4. Append it to `gfx::Window::Key`.
- **Resetting the tree is free**, as Reset points is.
- **Damage decrease is capped at 50%** for gear and talents together. This is a new cap, added with
  this patch in `reckon` (`rules.cpp:321`), where it is filled today without one.
- **Bastion and Mana Ward lift Defense and Magic Shield past the 60% cap** (about 59% → 65% late in
  the game), as designed.

## Rule 1, combining. Decided by the user, 2026-10-20: one sum

Every talent adds into one sum for its stat. The sum multiplies once, as the element runes do
(`realm_tuning.h:525-528`).

For example, Twisting Slash's damage is ×(1 + (Damage + SkillDamage + ElementDamage) / 100) from
talents, beside the runes' own factors. The bench below assumes it. Multiplying each talent on its
own would climb well past those totals.

## The build, in order

1. **Sim.**
   - Move the three drafts into `src/sim/` (this changes the rules print) and add `talents.cpp` to
     CMakeLists.txt.
   - Give the hero `TalentRanks` beside his points (`Body`). Read them in `Realm::rearm` and
     `reckon` through `talentSum`, for each stat in `TalentStat`.
   - The flat stats come first; most land next to an existing field (`Excellence`, `Fighter`,
     `recovery.h`):
     - Damage, AttackSpeed, CastSpeed, AttackRate, DefenseRate, Defense, MaxLife, MaxMana and MaxShield.
     - CriticalChance, ExcellentChance, IgnoreDefense, DamageDecrease, Reflect and BlockChance.
     - LifeOnKill, LifeSteal, IcePoisonResistance, ManaRegen and SkillManaCost.
     - MoreZen, MoreJewels, MoreItems, MoreExcellentItems and PotionHealing.
   - Then each `Special`, one at a time, each with a sim_test check:
     - The procs: DamageProc, KillSpeed, Mercy and IronWill. They need a timed state on the body,
       like Frenzy's (`kFrenzyTicks`).
     - The conditions: Executioner, Riposte, LastStand, Crowd, EyeOfTheStorm, FarShot and VsChilled.
     - The skill changes: Reach, ChainMore and ManaBack. Mind the user's 2026-10-04 cut to Lightning
       (`kLightningBodies` 11), which ChainMore +1 lifts to 12.
     - The rest: ChillOnHit, OffHand, WizardryIntoSwings, Attunement, SharedLight and Patron.
   - Summons: SummonDamage, SummonLife, SummonDefense, SummonAttackRate, SummonDamageTaken and
     SummonDamageAndLife go on the summoned body as it is raised.
2. **Commands and protocol.**
   - A new command takes a point (column, node), a second gives one back, and a third resets the
     tree. The realm checks every one with `talentsValid`.
   - This is a protocol bump, and the release check needs the server's protocol to match
     (memory: release checks the server protocol).
3. **Save.**
   - Save the ranks with the character, and refuse invalid ones on load with `talentsValid`, which
     returns the points.
   - Bump `kKeptLayout` if the kept layout grows (memory: kept layout follows every count).
4. **Interface.**
   - The Talents window ports `Passive.dc.html`: the columns, the − and + inside each icon, the
     strip naming the selected talent, and Reset with a second click. Sanctuary style
     (`game/ui/sheet.h`).
   - Add button C2 to the character window (`game/ui/card.cpp`), and the hide-others rule to
     `game/ui/desk.cpp`.
   - The buff strip holds 10 cells (`kBoons`, `game/ui/hud.h:192`). Raise it to about 16 or add a
     second row.
   - The character window's Combat and Bonuses pages show the talent shares.
5. **Content.**
   - Cut the 20×28 master icons (new_Master_Icon.OZJ) for the tree. Teach `pipeline/buff_icons.py`
     status sheets 2 and 3 and the master sheet for the nine buff cells.
   - Take MuDream's 4× copies where they can be read; otherwise use `pipeline/upscale.py`, 3× at
     most.
6. **Tests and bots.**
   - sim_test for every stat and special.
   - A balance bench with a geared hero (memory: balance bench needs a geared hero). Compare
     Twisting Slash, Hellfire, Penetration and Multi-Shot with and without a full tree.
   - The bots spend talents as they spend stat points.

## How it is built and tested

It is built in eight phases. Each ends in a commit straight to main, with explicit paths after
`git diff`, and nothing goes to players until phase 8. These rules hold throughout:
- **Build privately.** Use a scratch build directory, never the shared `build/`, which another
  session's half-edited headers can break (memory: shared build races).
- **No game windows.** Use `sim_test`, headless runs and offscreen shots. One QA pass at the end, if
  the user wants it.
- **Talents cost nothing when unused.** A character with no talents must play byte for byte as
  today. Every new dice roll happens only when the talent that needs it is taken, so the seeded sim
  log of a talentless run stays identical. That is the main regression test of every phase.

### Phase 0: start clean
- **Wait for the Jeweler session to land.** On 2026-10-20 it holds uncommitted edits in exactly our
  files: `command.h`, `realm.h`, `realm.cpp` and `realm_commands.cpp`, plus its own new files in
  src/sim. It will likely bump the protocol too.
- Start from a HEAD with both in, and take the next protocol number after theirs.
- Read every file:line in "Where it plugs in" again; they will have moved.

### Phase 1: data and rules, no behaviour
- Move the three drafts into `src/sim/` and add `talents.cpp` to CMakeLists.txt. Fold
  `tests/talents_check.cpp` into `sim_test` as `testTalents`.
- **Test:**
  - sim_test passes.
  - The seeded sim log is byte-identical to HEAD's, from a `git worktree` at HEAD (memory: verify
    lockstep on a private port, step 2).
  - Nothing reads talents yet, so any difference is a bug.

### Phase 2: ranks on the hero, commands, save
- `TalentRanks` on `Body` and `HeroRecord`.
- `TalentTake`, `TalentGive` and `TalentReset` (free), with `kLastKind` moved.
- `net::kVersion` +1.
- `kKeptLayout` 9 → 10, with ranks appended behind `layout >= 10`.
- Keys in the local save.
- Validation on restore through `talentsValid`.
- **Test:**
  - sim_test:
    - Take, give back and reset.
    - Refusals: closed node, over 10, no points, a seventh column on a non-MG, a give-back that
      would close a node.
    - Reset returns every point.
    - A tampered save loses its ranks and keeps its points.
  - **Read a copy of the live `characters.db`** with the new `keptFrom`. Every layout-9 row must
    still read, with empty ranks (memory: kept layout follows every count).
  - Private server on its own port, four netbots and one client. Every link logs "none disagreed".

### Phase 3: the flat stats
- Talent sums in `rearm` / `armsOf` and `reckon`, by the path that survives `Realm::spend` (the
  trap in "Where it plugs in").
- The talent factor in `strikeAt` and `strikeAround`, as one sum (rule 1).
- Loot, potions and life on kill.
- The 50% damage decrease cap.
- Three commits: offence, defence, loot.
- **Test:**
  - For each stat, a sim_test with a fixed hero gives the exact expected number. For example, Heft
    10 with a two-handed sword gives +10% on the band, and nothing with one hand.
  - The talentless seeded log is still identical.
  - The 50% cap is reached with eight excellent pieces and Hold the Line.

### Phase 4: the special majors, three to five a batch
1. **Procs:** Battle Fury, Battle Mage, Bloodrush, Iron Will and Mercy. These are timed states like
   Frenzy, expiring in `press` and cleared on death.
2. **Conditions:** Executioner, Deadeye, Frostbite, Thunderclap, Bloodlust, Eye of the Storm,
   Riposte and the Attunements.
3. **Skill changes:** Wider Rings, Frost Burst, Legion, Chain Lightning, Mana Surge, Frost Edge and
   Icy Shot, Off-Hand Mastery and Gladiator's Edge.
4. **Support and summons:** Shared Light, Patron, and the summon talents in `fitSummon`.

- **Test, each batch:**
  - A deterministic sim_test for each major:
    - The proc's rate over many seeded swings.
    - Once per swing, never once per body.
    - **No chains:** a Stormcall blow or an Arcane Echo never rolls a talent proc.
    - Poison and chill talents never stretch the plague and burn runes' pulses.
    - Block still stops at 25%.
  - A lockstep run after every batch, because procs add dice draws.

### Phase 5: the interface, shown early
- The Talents window, ported from `Passive.dc.html` in Sanctuary style.
- Button C2 on the character window, key A, and Talents hiding the other windows.
- The buff strip raised from 10 cells, with the nine new cells.
- Talent shares on the Combat and Bonuses pages.
- **The unlock: a sound and an animation** (the user, 2026-10-21):
  - **Sound:** "use cyberwave-orchestra-fantasy-game-sword-cut-sound-effect-get-more-on-my-patreon-339824
    (1).mp3 when talent is unlocked". It is in `third_party/downloads/` and is 1.96 s, 44 kHz stereo; the
    copy without "(1)" is the same bytes. It becomes `window_talent` (`italent.wav`, 22 kHz mono) in
    `content/sounds/sounds.json`, cooked with `./mu make sounds`, the way `window_invite` was (7e986308).
  - **"Unlocked"** is a point that opens something new: a talent's first point, or a point that opens a
    closed talent (the 10th in a small opens its major; a taken major opens the next tier). It plays
    once a click, however many talents open. Any other + is the plain button click.
  - **The animation**, for what opens, all in the window's own draw with no new art:
    1. **The line fills.** From the talent that opened it, blood runs down the joining line to the
       new talent, about 0.25 s, with a bright head.
    2. **The icon wakes.** MU's grey twin (`new_Master_non_Icon`) cross-fades to the colour icon,
       about 0.2 s, while the frame pops 1.0 → 1.12 → 1.0 and the rim flashes white-gold to blood.
    3. **A major** gets more: a blood ring grows from it and fades, about 0.45 s, with a few embers
       rising, and its two lines to the next tier fill one after the other.
  - **A plain point:** the rank number flashes and the icon gives a small 1.0 → 1.05 pop.
  - **Reset:** the talents fade back to grey in a ripple from the bottom tier up, about 0.3 s. No sound
    beyond the button's.
  - The realm's answer starts it, not the click, so a refused point never plays.
- **Test:**
  - Offscreen shots (`--windows character,inventory` and the talents window), at 1920×1080 and at
    one smaller size.
  - Shown to the user after each piece, as the character window was this week: window, then button,
    then strip.
  - Then a scripted run with `--ui-click`: take a point, give it back, reset, close and reopen. Check
    the realm's answers in the log.

### Phase 6: icons into content
- Teach `pipeline/buff_icons.py` status sheets 2 and 3 and the master sheet. Cut the tree icons.
  Take MuDream's 4× where it can be read, else the upscale route at 3× at most.
- Follow the Cooker rules (CLAUDE.md): `plan` before `cook`, and ask before a cook that changes what
  the game shows.
- **Test:** an offscreen shot of the strip and tree at real size, judged by the user.

### Phase 7: bots and balance
- `Brain::spend` learns the bot builds above.
- Count jewels and excellent items in `economy.log`.
- **Test, the measured A/B** (memory: bot comparisons need a frozen sim; the balance bench needs a
  geared hero):
  - Both arms are built from one frozen worktree.
  - Geared heroes with their points spent, at level 400 and at 150.
  - Two hunts: an easy one for damage, a hurting one for survival. 12-16 seeds of 5 minutes, each
    with the tree and without.
  - Measured: kills an hour, deaths, potions used and Zen an hour.
- **Pass:**
  - Each build's kill gain lands near its bench figure (+50% to +125%, without the gear's share).
  - No build dies more with talents.
  - Zen rises by about the 14% expected.
- If a build is off, change it on the canvas, generate the table again and run again. Never tune the
  .inc by hand.

### Phase 8: a real player, then ship
- **Test as a real player** on a local scratch server, with an account character rather than a
  scripted bot (memory: test as a real player):
  - Spend points, log out and back in, and reset.
  - Party with a netbot. Mercy and Patron need a party.
  - Change maps.
- **Ship only on the user's word,** and ask for "deploy" and "release" together, since this is a
  protocol bump:
  1. `./mu ship`.
  2. Back up `characters.db` on every box.
  3. Deploy every region's server.
  4. Release every platform.
  5. Watch `journalctl` for "this server speaks" refusals (memory: release checks the server
     protocol).
- Then `./mu ship notes` for the update panel.

### If it goes wrong live
- The servers keep the backed-up `characters.db`.
- A layout-10 row can't be read by an older server, so rolling back means the previous build *and*
  that backup, losing the play since.
- That is why phase 2 reads a copy of the live store before anything ships.

## Where it plugs in (mapped 2026-10-20; check the lines again when the patch starts)

The template is commit 530534c3 (Spend counts, ResetPoints). It touched 16 files, and talents
follow the same road.

- **Commands.**
  - Add `TalentTake`, `TalentGive` and `TalentReset` to the `sim::Kind` enum (`src/sim/command.h`),
    and **move `kLastKind` (:114)**, or `net::takeCommand` reads them as None.
  - Handle them in `Realm::applyCommands` (`realm_commands.cpp:177-186`, next to Spend and
    ResetPoints).
  - The wire carries a, b, c and d generically (`wire.cpp:79/:92`), so only `net::kVersion`
    (`wire.h:157`, now 43) bumps.
  - The server gate (`server/src/main.cpp:105-147`) passes player commands through.
- **Body and save.**
  - Put `TalentRanks` next to `points` / `pointsInHand` (`realm.h:371/:506`) and `HeroRecord`
    (`realm.h:736`).
  - Copy them in `Realm::record` / `restore` (`realm.cpp:538/:584`), and validate on restore.
  - Append them to `putKept` / `takeKept` (`wire.cpp:155/:204`) behind `layout >= 10`, and bump
    `kKeptLayout` 9 → 10 (`wire.h:166`). The server store (`server/src/store.cpp:22`) and
    `transfer.cpp:199/216` follow it.
  - Local save: new JSON keys in `src/game/save.cpp` `loadSave` :144 and `writeSave` :373, with
    defaults through `numberOr`.
- **Stats.**
  - Flat sums go in `Realm::rearm` (`realm_items.cpp:274`) just before the Zen rate (:532) and
    `reckon` (:651).
  - **`Realm::spend` (`realm.cpp:674`) calls `reckon` directly, not `rearm`**, so the talent sums
    must ride on `Excellence` / `Arms` (through `armsOf`, `realm_items.cpp:28`), or a stat spend
    drops them.
  - Fighter fields are filled in `reckon` (`rules.cpp:197`, chances and decrease :320-326, life
    :332-341). Mana is in `restoreMana` (`realm_tuning.h:30`), and speed in `Realm::reswing`
    (`realm_items.cpp:82`).
- **The blow.**
  - `Realm::strikeAt` (`realm_fight.cpp:20`) multiplies the force at `elementForce` :156 and
    `wrathForce` :158, and applies it at :189.
  - The talent factor (rule 1) and the conditional majors go there: Executioner, FarShot,
    VsChilled, Crowd, EyeOfTheStorm, Riposte and Attunement.
  - Area skills also pass `Realm::strikeAround` (`realm_skills.cpp:670`, its own elementForce :687).
- **Procs and timed states.**
  - Copy Frenzy: `frenzyUntil` / `frenzyStacks` (`realm.h:558-565`), set on a wound
    (`realm_fight.cpp:254-265`).
  - Expiry goes in `Realm::press` (`realm.cpp:1021`); rearm on expiry, as might does at :1027.
    Clear them on death (`realm_fight.cpp:1867`).
  - Not on the wire; the mirrors compute them in lockstep.
- **Kills and loot.**
  - `Realm::kill` (`realm_fight.cpp:1765`) handles LifeOnKill next to `killLife` :2009 and
    KillSpeed.
  - `Realm::leave` (`realm_items.cpp:1161`) scales item find :1179-1181, the jewel branch :1445,
    the excellent item :1454 and Zen :1522.
  - Potions: `potionPours` (`items.cpp:863`, called at `realm_items.cpp:894`). Heal: `healOf`
    (`skills.cpp:1180`).
- **Summons.**
  - `Realm::fitSummon` (`realm_summon.cpp:86-105`) is refit every tick by `tend` :179, so the
    talents show live.
  - SummonDamageTaken needs a summoner check in strikeAt; there is none today.
- **Interface.**
  - The character window's Combat page (`card.cpp:226-246`) and Bonuses page (:247-277) list the
    talent shares, and the Fortune group (:270) takes the loot ones.
  - Button C2 goes on `Card`, with its press out through `Card::update` as spend and reset are
    (`card.cpp:359-370`), and `Desk` opens Talents (`desk.cpp:962-990`).
  - Buff strip: `Desk::skillKeys` builds the cells (`desk.cpp:1837-1944`), after Frenzy's at
    :1893. `kBoons` is at `hud.h:192`.
  - Key: append to `gfx::Window::Key` (`window.h:145`) and to the positional table
    (`window.cpp:428-437`) in the same order. A, N and K are free.
- **Bots.** `Brain::spend` (`tools/bot/brain.h:1659`, shared by netbot) learns the builds below.
  Other point spenders that may want a default tree: `cradle.cpp:41`, `headless.cpp:200` and
  `play_open.cpp:277`.
- **Tests.** Add `testTalents` to `tests/sim_test.cpp` beside `testResetPoints` (:5215), and fold
  in `tests/talents_check.cpp`.

## The bench, 2026-10-20

This measures what the talents add to each build's main skill under rule 1, in a pack fight. These
are talent shares only; gear, runes and stats are not included. The bench source is
`tests/talents_bench.cpp`; build it as the check is built.

It assumes:
- Procs are up 65% of the time (10% a swing at about 2 a second, 5 s).
- Five monsters are in reach.
- Three element runes of the skill's element are worn.
- A quarter of blows land on monsters under 30% life, and a quarter are armed by a miss.
- 70% of arrows fly 5 tiles or more.
- 80% of spells hit chilled monsters when Ice is in the rotation.

| Build (the bots' builds, below) | Skill | At 199 (MG 279) | Full tree |
|---|---|---|---|
| Knight, two-handed whirlwind | Twisting Slash | +123% | +130% |
| Knight, dual wield | Cyclone | +125% | +125% |
| Knight, two-handed tank | Death Stab | +83% | +100% |
| Wizard, fire | Hellfire | +97% | +97% |
| Wizard, lightning and ice | Lightning | +89% | +89% |
| Wizard, evil spirit | Evil Spirit | +52% | +52% |
| Elf, multi-shot | Multi-Shot | +47% | +47% |
| Elf, penetration | Penetration | +102% | +102% |
| Elf, support | Multi-Shot | +37% | +47% |
| MG, spellblade | Twisting Slash | +60% | +75% |
| MG, fire caster | Flame | +98% | +105% |

What it shows:
- **Specialisation caps the gain.** A full tree adds little over a level-400 build, because the other
  columns don't feed that skill. This is the property we wanted.
- **Two trims, taken by the user on 2026-10-20 and on the canvas.** The MG's Flame was counted
  twice, so Flame Edge → "Flame mana cost −2%" and Flame Strike → "Hellfire damage" (it was +138%).
  Eye of the Storm drops to +2% a monster, to 10%, for the Knight and Elf (knight pack melee was
  about +130%). The table shows the numbers after both.
- **Knight pack melee is still the highest**, at about +125%. Melee stands in the crowd.
- **Multi-Shot and Evil Spirit are low (about +50%) on purpose.** Multi-Shot has its own Barrage runes
  (+15/30/50% each) and the user called it "very powerful". Evil Spirit is the cheap pack opener
  with Spirit Plague runes. Leave both.

## Measured, 2026-10-21

`tests/talents_fight.cpp`: a raid kit's level-400 hero (content/raid/party.json: +9 to +11 sets,
runes, 2nd wings, a pet), potions in the bag, attacks the nearest monster with one skill. 12 seeds of
5 minutes each, with the bot's build (all 199 points) and without. Icarus is the easy ground, Aida
(map 33) the one that hurts. Per hour:

| Build, kit, skill | Ground | Damage | Kills | Potions | Deaths |
|---|---|---|---|---|---|
| Knight whirlwind, Bran, Twisting Slash | Icarus | 6.97M → 12.8M (×1.84) | 539 → 796 (+48%) | 4 → 0 | 0 → 0 |
| | Aida | 4.17M → 9.0M (×2.16) | 67 → 148 (+121%) | 881 → 413 | 0 → 0 |
| Knight tank, Aldric, Death Stab | Icarus | 1.82M → 2.5M (×1.37) | 146 → 199 (+36%) | 0 → 0 | 0 → 0 |
| | Aida | 0.47M → 0.72M (×1.53) | 0 → 5 | 389 → 86 | 0 → 0 |
| Wizard fire, Morwen, Inferno | Icarus | 8.74M → 14.7M (×1.68) | 683 → 997 (+46%) | 0 → 0 | 0 → 0 |
| | Aida | 5.11M → 10.7M (×2.09) | 85 → 183 (+115%) | 1234 → 626 | 3 → 1 |
| Elf multi-shot, Faela, Multi-Shot | Icarus | 8.67M → 11.8M (×1.36) | 715 → 952 (+33%) | 0 → 0 | 0 → 0 |
| | Aida | 4.28M → 6.64M (×1.55) | 71 → 115 (+62%) | 95 → 0 | 0 → 0 |
| Elf penetration, Faela, Penetration | Icarus | 13.0M → 19.9M (×1.53) | 1054 → 1381 (+31%) | 24 → 13 | 0 → 0 |
| | Aida | 7.04M → 13.7M (×1.95) | 119 → 240 (+102%) | 68 → 57 | 0 → 0 |

Against the plan's pass:
- **Damage lands on the bench's figures.** On Aida, where the conditions and procs all count, it is
  about the bench: whirlwind ×2.16 (bench +123%), fire ×2.09 (+97%), penetration ×1.95 (+102%), the
  tank ×1.53 (+83%) and multi-shot ×1.55 (+47%, low on purpose). On Icarus, where monsters fall in one
  or two blows, it is less, as it should be.
- **Kills follow damage only where monsters take several blows.** On Icarus +31% to +48%, since a
  one-blow kill gains nothing; on Aida +62% to +121%.
- **No build dies more.** The wizard's Aida deaths fall from 3 to 1 an hour. Potions roughly halve on
  Aida, because monsters fall sooner.
- **Zen a kill rises 18-24%,** about the 10-20% of the builds' Fortune plus a richer mix of kills.
  Zen an hour rises far more than the 14% the economy note expected, because kills rise; that note
  counted Fortune alone.
- Not measured: the Magic Gladiator (the raid party has none) and heroes at level 150.

The offline bot can be raised in a kit (`bot --kit NAME --map N --no-talents`), but its brain wanders
between maps and barely fights in a kit, so these numbers are from the bench.

## Economy, from the live log (2026-10-20)

Source: the server's `economy.log`, 2026-10-11 to 2026-10-20, 153 player-hours. It counts Zen
only. Jewels and items are not logged, so those are estimates from the drop formulas.

Today, per player-hour:
- Zen in, 556k: kills 218k (the largest), quests 148k, Firecrackers 91k, sold 57k, events 33k.
- Zen out, 237k: the machine 155k, shops 46k, travel and repair 14k each.
- Net +319k.

What Fortune adds once a character fills it:

| Talent | Today | With Fortune | Effect |
|---|---|---|---|
| Zen from kills, +20% (Prospector 10, Treasure Hunter 10; was +40%) | 218k/h | +44k/h | Net Zen +14% (+27% before it was halved) |
| Jewels, +10% (Scavenger 5, Jeweller's Eye 5) | about 1-3 a player-hour (0.5% of kills, at an estimated 150-600 kills an hour) | +0.1-0.3/h | One more jewel every 3-10 hours. Small |
| Excellent items, +20% (Lucky Find) | about 0.1-0.3 a player-hour (1/200 of item drops) | +0.02-0.06/h | One more every 15-50 hours. Small |

Kills an hour are inferred from kill Zen (218k/h). Zen falls on a share of kills, at about 1,500 for
a level-50 monster (`killExperience` + 7, realm_items.cpp:1514), so the range is wide.

**Reading.** The jewel and excellent talents are sober. Fortune's Zen was not: +40% on the largest
faucet raised net Zen by about a quarter. **The user halved it, 2026-10-20**: Prospector 1% a point
and Treasure Hunter +10%, so +20% in all, +44k a player-hour, and net Zen rises about 14%. It is on
the canvas.

**Build note.** Count jewels and excellent items in `economy.log` with this patch
(`server/src/economy.cpp` labels each change by its happening). Then the jewel estimate becomes a
measurement.

## Bot builds

These are the bench's builds, as column ranks in the class's column order. F is a full column (63
points). S is a spine: the left small at 10 and the major, each tier (33). Z is empty. A bot takes
its build's ranks in that order, a point at a time, as soon as it is open.

| Build | Columns |
|---|---|
| Knight, two-handed whirlwind | F Two-Handed, F Whirlwind, F Fury, Fortune 10 |
| Knight, dual wield | F Dual Wield, S Whirlwind, F Fury, Fortune 10/10/1/10/9 |
| Knight, two-handed tank | F Two-Handed, F Guardian, F Fury, Fortune 10 |
| Wizard, fire | F Inferno, S Ice & Poison, F Evil Spirit, Fortune 10/10/1/10/9 |
| Wizard, lightning and ice | F Lightning, F Ice & Poison, F Evil Spirit, Fortune 10 |
| Wizard, evil spirit | S Ice & Poison, F Evil Spirit, F Ward, Fortune 10/10/1/10/9 |
| Elf, multi-shot | F Marksman, S Wind Arrow, F Grace, Fortune 10/10/1/10/9 |
| Elf, penetration | F Marksman, F Wind Arrow, F Grace, Fortune 10 |
| Elf, support | S Marksman, F Support, S Summoner, F Grace, Fortune 7 |
| MG, spellblade | F Spellblade, F Blade, F Evil Spirit, F Guard, Fortune 10/10/1/6 |
| MG, fire caster | F Spellblade, F Inferno, S Lightning & Ice, F Evil Spirit, S Guard, Fortune 10/10/1/3 |

## Cards (tooltips)

A talent's card is built from the table, so it needs no new text. It mirrors the canvas strip:
- The name, its rank (3/10), and what it gives now and at the next point, e.g. "+3% two-handed
  damage, next +4%".
- For a major, its whole line.
- On a closed node, in muted ink, why it is closed: "Opens at 10 points in a talent above" or
  "Opens when the major above is taken".
- On a taken node that can't give a point back: "Take back the talents below first".

The words are the canvas's `text` fields, so rewording a talent means editing the canvas and
generating the table again.

## Icons, staged

`out/staging/talents/` (ignored by git) holds the 133 distinct tree icons (`tree/master_N.png`) and
the 10 buff cells (`buffs/`), cut at MuMain's raw 20×28 from third_party's S17 client. They are for
looking at only. The real cut goes into content/ through `pipeline/buff_icons.py` with the sharper
source (step 5).

## Open

- **Resets:** the tree fills at the first reset (399 points against 378). When resets are designed,
  raise small caps from 10 to 20 so the tree lasts about three resets. The user, 2026-10-20: wait,
  since resets are a game-wide decision and talents follow it.
- Nothing else. The seven questions of 2026-10-20 were answered: one sum, both trims, a free reset,
  key A, a 50% damage decrease cap, and lifting the guard cap. They are in "What is decided".
