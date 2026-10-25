# Jewel of Harmony, and Kanturu's drops

Plan, 2026-10-25. Built: step 1 (21a4568a, the four items, dormant), step 2 (8409b267, the option
on the item, protocol 55), step 3 (bb869b7e, ad138709, it counts worn and shows on the card), step 4 (f7c5c19c, the Gemstone drop, one kill in 150 in the Ruins and the Relics on its own dice, snapshot shape 27), step 5 (8d99fe4c, the jewel and the stones dragged on, protocol 58; seen in game, shots/harmony/harmony_tooltip_level4.png), step 6 (2c54515d, the Chaos Machine's Refine Gemstone, Smelt and Remove Harmony, protocol 59; docs/game/chaos-machine.md), step 7 (the services' odds over a seeded run, the Armory's Gemstone roll, docs/game/harmony.md). **All built; not deployed.** The skill_sweep was not run: the strength was the user's call at step 3. The user: "we need drops which amke sense for kanturu
monsters", picking Feather + Nova, Gemstone -> Harmony and the Golden Satyros box; then "what is
that jewel of harmonu? how we can use it?", "lets make a plan how we implement this system", and
"what about end game orbs and scrolls for all classes?".

Sources: WebZen GameServer 1.00.93 (`WZ`, `JewelOfHarmonySystem.cpp`, cloned to a scratchpad,
cp949), MuMain (`MM`, `UI/Legacy/UIJewelHarmony.cpp`, `Data/Local/Eng/JewelOfHarmonyOption_eng.bmd`,
`mix.bmd`), OpenMU (`OM`, `VersionSeasonSix/Items/HarmonyOptions.cs`,
`HarmonyJewelConsumeHandlerPlugIn.cs`). **WebZen's rates are not on disk**: every success rate, zen
price and the Gemstone's drop rate are `commonserver.cfg` keys that default to 0, and no cfg we have
carries them. The rates below are the client's `mix.bmd` where it has them, else OpenMU's.

**Decided (the user, 2026-10-25, answering §3's questions):**
- Socketed items take Harmony; wings, jewellery and pets do not.
- The item **binds** to the character when the jewel takes; removing the option does not unbind it.
- **Stronger than MU's** (the user, building step 1: "our harmony stones has to be stronger that
  original i think"): MU's tables are the shape. **Decided at step 3 ("you decide"): every flat
  option three times MU's (sim::kHarmonyStrength); Damage taken keeps MU's 3-5% a piece**, since
  five pieces at MU's already take a quarter off a blow (ad138709).
- **Gemstone 1 in 150 kills** in the Ruins and Relics, beside the kill's own drop.
- The +13-only options and the two PvP rate options are **dropped**; option levels run 0-11.
- **Everything at the Chaos Machine** (the user: "chaos machine", then "Everything at the Machine"):
  Gemstone -> Jewel of Harmony, item -> Refining Stone, and Remove option are Machine recipes. Elpis
  stays a quest giver. The jewel and the stones are dragged onto the item, as Bless and Soul are.
- **MU's rates**, the harsh fail included: refine 80%, apply 60%, smelt 20% normal / 50% excellent,
  stones 20% lower / 80% higher, a failed stone back to the option's minimum.
- The tooltip line in **MU's gold** (1.0, 0.8, 0.1), grey when inactive.
- **Feather and Scroll of Nova** drop in the Ruins now, before Harmony.

## 1. MU's system in short

| Step | Who | MU's rule | Rate |
|---|---|---|---|
| Gemstone (14,41) drops | any monster in Kanturu 1, 2 and the boss map | instead of the kill's other drop | cfg (S17: 500/10000 on 37 and 38) |
| Gemstone -> Jewel of Harmony (14,42) | Elpis | jewel lost on a fail | 80% (mix.bmd, OM), no zen |
| Jewel onto an item | the player, dragged | weapons, bows, staffs, shields, armour; **not** ancient, socket, wings, jewellery, or an item that has an option | 60% (OM); a fail eats the jewel, the item untouched |
| The option | weighted draw among the options whose minimum is at or under the item's +level | starts at its minimum, not at the item's +level | weights OM's (§2) |
| Refining Stone, lower (14,43) / higher (14,44) | Osbourne smelts one item with no option (the 85 cheapest at +4 or more) | normal item -> lower, excellent -> higher; the item is eaten either way | 20% / 50% (mix.bmd, OM) |
| Stone onto the item | the player | +1 option level, never past the item's +level; a fail drops the option back to its minimum | lower 20%, higher 80% (OM) |
| Remove the option | Jerridon | always | 100k-500k zen by option level |

The option is **active only while the item's +level is at least the option level** (a failed Soul
that drops the plus greys it out; raising the plus brings it back). The same option on several pieces
**stacks**. A harmony item **cannot be traded, put in a personal shop, sold, or dropped**, and is never
the item a PK death loses; the warehouse takes it. The tooltip line is bold yellow
(1.0, 0.8, 0.1), grey (0.4) when inactive. The server applies only **half** of "Defense increase"
(`JoH.cpp:1163`), though the client shows all of it.

## 2. MU's option tables

Values per option level 0-13 (MM's `JewelOfHarmonyOption_eng.bmd`, identical in OM). "Min" is the
item +level needed to roll it, and the level it starts at. **Weight** is OM's.

**Weapons** (swords, axes, maces, spears, bows):

| # | Option | Min | Weight | 0 | 3 | 6 | 9 | 11 | 13 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Min attack + | 0 | 40 | 2 | 5 | 9 | 14 | 16 | 20 |
| 2 | Max attack + | 0 | 40 | 3 | 6 | 10 | 17 | 23 | 29 |
| 3 | Required strength - | 0 | 40 | 6 | 12 | 20 | 29 | 35 | 40 |
| 4 | Required agility - | 0 | 40 | 6 | 12 | 20 | 29 | 35 | 40 |
| 5 | Attack + (min and max) | 6 | 30 | | | 7 | 11 | 14 | 19 |
| 6 | Critical damage + | 6 | 30 | | | 12 | 18 | 22 | 30 |
| 7 | Skill damage + | 9 | 20 | | | | 12 | 16 | 22 |
| 8 | Attack rate + (PvP) | 9 | 20 | | | | 5 | 9 | 14 |
| 9 | SD decrement % | 9 | 20 | | | | 3 | 7 | 10 |
| 10 | SD bypass % | 13 | 10 | | | | | | 10 |

**Staffs**: 1 Wizardry + (min 0, 6 -> 31), 2/3 Required strength/agility - (0), 4 Skill damage +
(6, 7 -> 30), 5 Critical damage + (6, 10 -> 28), 6 SD decrement % (9), 7 Attack rate PvP (9),
8 SD bypass % (13). Weights 40, 40, 40, 30, 30, 20, 20, 10.

**Armour and shields**: 1 Defense + (0, 3 -> 25), 2 Max AG + (3), 3 Max life + (3, 7 -> 30),
4 Life regen + (6, 1 -> 8 a tick), 5 Mana regen + (9, 1 -> 5), 6 Defense rate PvP (9),
7 Damage taken - % (9, 3 -> 7), 8 SD ratio % (13). Weights 50, 40, 40, 30, 20, 20, 20, 10.

The full 14-level tables are in the research write-up (scratchpad `harmony/mu_rules.md` §5,
2026-10-25); they go into `src/sim/harmony.h` whole when built.

## 3. Where ours must differ, and the picks to make

Recommendations first; each is the user's call.

1. **Our plus stops at +11** (`kMachineCap`, sim/machine.h). Options whose minimum is 13 (SD bypass,
   staff SD bypass, SD ratio) could never roll. *Recommend:* drop them; option levels run 0-11.
2. **PvP options.** The sim has no player-vs-player hit roll that reads an attack or defence *rate*
   between players. *Recommend:* drop "Attack rate (PvP)" and "Defense rate (PvP)" and their
   weights. The SD options stay only if SD soaks blows as ours does (it does, monsters too).
3. **Socket items.** MU refuses a socketed item. Ours put rune sockets on most endgame pieces, so
   MU's rule would shut Harmony out of the gear it is for. *Recommend:* allow sockets; refuse wings,
   jewellery and pets, as MU.
4. **Binding.** MU forbids trade, shop, sale and ground for a harmony item. Ours has a bind
   (`sim::Held::bound`). *Recommend:* the item **binds to the character
   when the jewel takes**, and Elpis's removal does not unbind it. The market stays clean of
   harmonised gear, as MU's was.
5. **The numbers.** MU's flat values (max attack +23 at 11) are small next to ours (a Titan knight
   hits 549-703 before skills). *Recommend:* MU's values as they are for a first build, measured with
   `tests/skill_sweep` on the Kanturu kits (tests/kits/kanturu.json); scale only if the sweep says
   they do nothing.
6. **Where it happens.** MU splits it over three NPCs (Elpis, Osbourne, Jerridon). We have Elpis
   only. *Recommend:* **one Refinery page in Elpis's dialog** with four services -- Refine Gemstone,
   Smelt (item -> stone), Remove option -- and the jewel and stones used by dragging onto the item,
   as Bless and Soul are. A page, not a separate window opened on talk: `Realm::talk` checks services
   before quests (`realm_folk.cpp:327`), and a plain service branch would hide her quests.
7. **Rates.** *Recommend:* MU's from mix.bmd/OM: refine 80%, apply 60%, smelt 20/50%, stones 20/80%,
   a stone's fail back to the option's minimum. Removal 100k-500k zen as MU's table.
8. **Gemstone drop rate.** S17's 5% a kill would flood it (a geared hero kills ~200-500 an hour in the
   Ruins). *Recommend:* **1 in 150 kills** in the Ruins and Relics, owned as other drops are, the
   kill's other drop kept (MU's "instead" is not ours to copy: it would starve the 380 gear).
9. **Colour.** Yellow is our +7 name colour (`describe.cpp:27`). *Recommend:* the harmony line in
   MU's gold (1.0, 0.8, 0.1), its own tone, grey when inactive. Never red: a positive value never is.

## 4. How it is built (our code)

Survey of 2026-10-25 (scratchpad `harmony/our_code.md`). Versions today: protocol 54, kept layout 19,
snapshot shape 24. Harmony takes **protocol 55, kept layout 20, shape 25**: client and servers
release together.

**Step 1 -- the four items.** Gemstone 14/41, Jewel of Harmony 14/42, Lower 14/43 and Higher Refining
Stone 14/44 (all free numbers). Models from MuMain's `Data/Item`: `rs.bmd`, `jos.bmd`,
`LowRefineStone.bmd`, `HighRefineStone.bmd`. Recipes like `content/items/misc/Jewel22.json` with
`"jewel": false` (out of the generic jewel pool), `mu.db` rows, `./mu make`, every world's tables.
`stackMost`, `brokerKind`, `expensive` and predicates in `sim/items.h`. Shown in the studio first.

**Step 2 -- the option on the item.** `sim::Held` (items.h:70-108) gains `uint8_t harmony` (kind) and
`uint8_t harmonyLevel` after `bound`: the struct stays 24 bytes and every brace initialiser holds.
- Saves: `putHeld`/`takeHeld` (net/wire.cpp:125-155) grow two bytes under kept layout 20 (old rows
  read as none). Covers the character blob, the vault, the machine box and the lobby roster;
  `server/src/store.cpp` needs nothing else.
- Market: `packHeld`/`unpackHeld` (sim/broker.cpp:94-123) have free bits, so no market format moves --
  and a bound item never reaches it anyway.
- Also: Held equality (realm_broker.cpp:18-25), `brokerAlike`, chat item links (item_link.h), the
  JSON save (save.h/.cpp), roster.cpp, `--give` extras, the snapshot.
- `src/sim/harmony.h/.cpp`: the tables of §2 as §3 cuts them, the weighted draw, `active(held)`.

**Step 3 -- what it does.** `Realm::rearm` sums active options after the excellent loop
(realm_items.cpp:374-400) into `Excellence` (rules.h:55-110): attack, wizardry, critical and skill
damage into `Arms`, defence (half, as MU) into the armour loop (:620-640), max life, max AG, regen,
damage taken. The requirement decreases go into `fits`. Tooltip: `describe.cpp`, the line after the
excellent lines (:1040-1045), gold, grey when inactive; the damage and defence lines include it.

**Step 4 -- the drop.** `bool gemstone` in `MapRow` (maps.h), `"gemstone"` in `DROPS`
(pipeline/realm_tables.py:87), in `rules.drops` of `kanturu` and `kanturu_relics`. The roll copies the
Scroll of Nova block in `Realm::leave` (realm_items.cpp:1395-1410) with its own dice (realm.h, realm.cpp,
the snapshot's dice list), so no other map's log moves.

**Step 5 -- the jewel and stones on an item.** A branch in `Realm::refine` (realm_items.cpp:1037)
after the Rune of Creation's, shaped like the Jewel of Life's; `worksOn` (:2177) and `refineAcross`
(:2183) learn the three items; the bag's drag (`bag.cpp:632-659`, chest.cpp) already sends
`Command::Refine`. No new command kind.

**Step 6 -- the Chaos Machine's three recipes** (decided: everything at the Machine, not Elpis).
Recipes beside the Machine's others (`sim/machine.h:142`, docs/game/chaos-machine.md): Refine Gemstone
(Gemstones in, a Jewel of Harmony each at 80%), Smelt (one weapon or armour piece with no option, the
cheapest at +4 or more -> a Lower stone, an excellent piece -> a Higher one, at 20% / 50%, the piece
eaten either way), Remove option (a harmonised piece and zen, MU's 100k-500k by option level, always).
The Machine's window, rates line and Armory mixes pick them up as its other recipes do.

**Step 7 -- tests and the rest.** sim_test: the draw's weights, activity by plus, stacking, the bind,
each service's odds over a seeded run, kept layout 19 rows reading as no harmony. skill_sweep before
and after on the Kanturu kits. The Armory (tools/armory dump: rules.cpp services and mixes,
drops.cpp), docs/game/harmony.md, refining.md, kanturu-quest.md, broker.md.

Rough size: steps 1 and 4 a session; 2, 3 and 5 a session; 6 a session; 7 and the review a session.

## 5. The rest of Kanturu's drops

- **Feather + Scroll of Nova** (picked): `"feather"` and `"nova_scroll"` into Kanturu's
  `rules.drops`. Two words and `mu make realm`; no new code.
- **Golden Satyros box** (picked): MU's Golden Satyros (497) appears on a random Ruins tile and always
  drops a Box of Kundun +4 (`WZ EledoradoEvent.cpp:887-930`, `gObjMonster.cpp:4253-4267`). Needs the
  +4 box tier (docs/game/kundun-box.md: only +1 to +3 are built) and a golden spawn on a timer. After Harmony.

## 6. Endgame orbs and scrolls (the user's question, research only)

Survey of 2026-10-25 (scratchpad `harmony/skill_items.md`).

- **Nothing that teaches a skill can drop in Kanturu today.** A kill drops items of drop level
  [L-15, L] (`kGap`), so the Ruins' window starts at 127; our highest skill item is the Orb of
  Rageful Blow at 120. **No class has a skill item asked above level 176.**
- **The Dark Lord has none at all**: Fire Burst, Electric Spike, Critical Damage, Summon and Fire
  Scream are built, but no scroll teaches them -- only test kits. docs/plans/dark-lord.md already
  plans them as quest pay and Hanzo's shelf. This is a live gap, apart from Kanturu.

| Class | Top skill item now (asked level, source) | MU's Kanturu-era item | Gap |
|---|---|---|---|
| Knight | Rageful Blow, Death Stab, Greater Fortitude (120; Lost Tower quests, Aida drops) | none new | no endgame skill item; no knight Ancient designed |
| Wizard / Soul Master | Inferno (176, Tersia), Cometfall (160, Sky Door), Nova (Kanturu's own in MU, not on our Kanturu) | Decay, Ice Storm, Nova, Teleport Ally, Wizardry Enhance | Decay, Teleport Ally, Wizardry Enhance missing; Ice Storm and Weakness are SM Ancients |
| Elf | Penetration (130, Tersia 6), Alquamos orb (Sky Door) | Ice Arrow | Ice Arrow missing (its number is our Impale's) |
| Magic Gladiator | Power Slash (150, drops only) | Gigantic Storm | Gigantic Storm missing; no quest pays his orbs |
| Dark Lord | none | base scrolls; Chaotic Diseier | all five base scrolls; Chaotic Diseier |

Endgame power is planned to come from **Ancient runes** (Relics and boss only, hero 380; six built,
all the Soul Master's; the other classes' "still to design"), not from new skills. Kanturu scrolls
would sit beside that: a spell is a rune or a scroll, never both.

*Recommend:* Nova on Kanturu now (§5); the Dark Lord's five scrolls next, as his plan says; then one
Kanturu-tier skill per class, researched one at a time with code and footage before it is built,
as the Dark Lord's spells were, dropped through a Kanturu map flag as Nova is.
