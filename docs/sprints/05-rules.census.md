# Sprint 5 data census: what the rules can be built from

Read-only survey taken 2026-09-20, before sprint 5 is planned. Nothing here is a design. Every
number is traced to `mu.db` `table.column`, to a file:line in OpenMU's Version075 source, or to
MU2's C# — and where it is only MU2's C# and not a 0.75 source, that is said.

**Three source trees are in reach and their standing differs:**

| source | where | standing |
|---|---|---|
| `mu.db` | `/Users/karlisfeldmanis/Documents/muremaster2/mu.db` | cooked 0.75 data, verified against OpenMU below; two rows wrong |
| OpenMU | `LEGACY/reference/openmu/src` — **the full source, 158 MB, including `Persistence/Initialization/Version075`** | the primary trace for every formula. It was not known to be here; everything below is traced to it directly rather than through MU2 |
| MU2 `shared/` | `MU2/shared/*.cs` | a worked C# interpretation. Useful as a reading, never as a trace |

---

## 1. mu.db: the tables sprint 5 touches

Twelve tables, all small. Schema is `sqlite3 mu.db .schema`.

| table | rows | sprint 5 relevance |
|---|---|---|
| `monster_kinds` | 16 | the breed table — every stat a fight needs |
| `monster_spawns` | 17 | the nests, with `map` |
| `npc_spawns` | 14 | Lorencia's townsfolk; no combat stats |
| `gates` | 2 | map 0 and map 3 entry rectangles |
| `items` | 166 | **names and drop levels only — no combat stats** |
| `characters` | 12 | saved test characters; the starting-stat shape |
| `inventory` | 72 | slot/code/level/options/durability/quantity |
| `accounts`, `messages`, `character_skills`, `character_hotkeys`, `character_spellkeys` | 6/20/15/4/4 | not sprint 5 |

**There is no `experience` table, no `levels` table and no `drops` table.** PLAN.md foundation 11
says `cook.py` writes "monsters, spawns, items, drops, experience". Two of those five do not
exist in `mu.db` and cannot be cooked from it. Experience is a formula (§3), drops are a
formula plus a pool filtered from `items` (§3, and sprint 7).

### `monster_kinds`

    number, name, level, health, minimum_damage, maximum_damage, defense,
    move_range, attack_range, view_range, move_delay, attack_delay,
    attack_rate, defense_rate, respawn_seconds, attack_skill

All sixteen rows checked against `Version075/Maps/Lorencia.cs` and `Noria.cs`. Example rows:

| number | name | level | health | min/max dmg | def | move/atk/view | move_delay | atk_delay | atk_rate | def_rate | respawn | skill |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Bull Fighter | 6 | 100 | 16/20 | 6 | 3/1/5 | 400 | 1600 | 28 | 6 | 10 | — |
| 3 | Spider | 2 | 30 | 4/7 | 1 | 2/1/5 | 400 | 1800 | 8 | 1 | 10 | — |
| 6 | Lich | 14 | 255 | 41/46 | 14 | 3/4/7 | 400 | 2000 | 60 | 14 | 10 | 2 |
| 7 | Giant | 17 | 400 | 57/62 | 18 | 2/2/3 | 400 | 2200 | 80 | 18 | 10 | — |
| 14 | Skeleton Warrior | 19 | 525 | 68/74 | 22 | 2/1/4 | 400 | 1400 | 93 | 22 | 10 | — |

Every column maps one-to-one onto `Version075/Maps/Lorencia.cs:77-102` (Bull Fighter's block):
`Stats.Level`, `Stats.MaximumHealth`, `Stats.MinimumPhysBaseDmg`, `Stats.MaximumPhysBaseDmg`,
`Stats.DefenseBase`, `Stats.AttackRatePvm`, `Stats.DefenseRatePvm`, plus `MoveRange`,
`AttackRange`, `ViewRange`, `MoveDelay`, `AttackDelay`, `RespawnDelay`.

**All sixteen columns are 0.75 data. There are no later-season columns in this table.** The
only 0.75 field not carried is `NumberOfMaximumItemDrops` (1 for 50 of the Version075 monsters,
0 for 4) — sprint 7's problem, but it is missing and the cook cannot invent it.

**`respawn_seconds` is wrong for two of Lorencia's eight breeds.** `mu.db` stores 10 for all
sixteen. `Version075/Maps/Lorencia.cs` says:

| breed | mu.db | Version075 |
|---|---|---|
| Bull Fighter | 10 | **3** (`Lorencia.cs:89`) |
| Budge Dragon | 10 | **3** (`Lorencia.cs:152`) |
| Hound, Spider, Elite Bull Fighter, Lich, Giant, Skeleton Warrior | 10 | 10 |
| all eight Noria breeds | 10 | 10 |

The map files across Version075 hold six distinct respawn delays (3, 8, 10, 15, 50, 150 s), so
"10 everywhere" is a flattening in whatever wrote `mu.db`, not a 0.75 fact. Either the cook
corrects those two rows against `Lorencia.cs`, or it is marked `invention` on the line.

`move_delay` is 400 ms for all sixteen. `attack_delay` ranges 1400-2200 ms.

### `monster_spawns`

    id, number, x1, x2, y1, y2, count, map

Seventeen rows, `map` 0 (Lorencia) and 3 (Noria), verified identical to
`Version075/Maps/Lorencia.cs:65-73` and `Noria.cs` — the same rectangles, the same counts, the
same order. The `x1 x2 y1 y2` order in `mu.db` differs from OpenMU's argument order
(`CreateMonsterSpawn(id, npc, x1, x2, y1, y2, count)`) only in naming, not in value.

**The coordinates are MU tile coordinates: `x` is the attribute-grid column, `y` is the
attribute-grid row.** `conventions.md` "Space" makes a tile's centre in metres
`(column + 0.5, y, -(row + 0.5))`, so a spawn rectangle's `y` is negated on the way into the
world. A spawn rectangle read as world z is mirrored about the x axis and still looks like a
nest.

### `items`

    code (PK), item_group, item_index, name, model, drop_level

166 rows in groups 0-11 and 14. Names and `drop_level` check exactly against
`Version075/Items/Weapons.cs:89-104` for group 0 (Kris dl 6, Short Sword dl 3, … Giant Sword
dl 52). **No group 12 (wings) or 13 (pets)**, although `Version075/Items/Wings.cs:44-46` and
`Pets.cs:32-36` define three of each — so `mu.db`'s item set is a 0.75 *subset*, not
contaminated with a later season.

**`items` carries no combat stats at all** — no damage, defence, attack speed, reach,
durability, requirements, two-handedness. In MU2 those live on the *asset* json and travel
through `index.json` (`Rows.cs:1672-1687` says so explicitly: "the rows are on the assets
rather than in a table here"). Sprint 7's problem, but it means `cook.py` cannot produce a
weapon table from `mu.db` alone.

### `characters`

    id, account_id, name, map, x, z, facing, health, max_health, class, level,
    experience, slot, seen, money, strength, agility, vitality, energy, points

`class` is 0 Dark Wizard, 1 Fairy Elf, 2 Dark Knight — **MU2's own enumeration, not MU's packed
class byte** (MU writes a Dark Knight as 0x10 and a Blade Knight as 0x11 —
`Beast.cs:2460-2466`). Twelve saved rows; the starting stat triples in them (28/20/25/10,
18/18/15/30, 22/25/20/15) match `Rates.For` (§3) exactly. `x`/`z` are stored in MU units, not
metres (divide by `units_per_tile`).

---

## 2. What Lorencia needs

Map number 0 (`Version075/Maps/Lorencia.cs:20`, `internal const byte Number = 0`).

**Nine nests, eight breeds, 290 monsters alive at once:**

| nest | breed | level | rect (x1,y1)-(x2,y2) | count | blocked tiles in rect | safe tiles |
|---|---|---|---|---|---|---|
| 1 | Spider | 2 | (180,90)-(226,244) | 45 | 11.0% | 0% |
| 2 | Budge Dragon | 4 | (180,90)-(226,244) | 40 | 11.0% | 0% |
| 3 | Budge Dragon | 4 | (135,20)-(240,88) | 20 | 10.9% | 0% |
| 4 | Bull Fighter | 6 | (135,20)-(240,88) | 45 | 10.9% | 0% |
| 5 | Hound | 9 | (8,11)-(94,244) | 45 | 11.5% | **0.3%** |
| 6 | Elite Bull Fighter | 12 | (8,11)-(94,244) | 45 | 11.5% | **0.3%** |
| 7 | Lich | 14 | (95,168)-(175,244) | 20 | 7.4% | 0% |
| 8 | Skeleton Warrior | 19 | (95,168)-(175,244) | 15 | 7.4% | 0% |
| 9 | Giant | 17 | (8,11)-(60,80) | 15 | 6.2% | 0% |

Blocked and safe fractions measured from `assets/world/lorencia/attributes.png` (§4). **Between
6% and 12% of every nest rectangle is un-standable**, so the spawn placement must reject and
redraw, not place blind. Two nests clip the safe zone by a fraction of a percent, which is
enough to put a Hound inside the town's no-attack ring if placement does not test it.

**Respawn timing** is stored, per breed, in `monster_kinds.respawn_seconds` — 10 s for all of
Lorencia's eight, and wrong by the two rows above. MU2's realm converts it at
`Realm.cs:3696`: `dead.RisesAt = this.Tick + (dead.Breed.RespawnSeconds * Hz)`, i.e. the
conversion to ticks happens where the tick rate is known, not in the table (`Rows.cs:23-26`
says the delays stay in milliseconds/seconds on purpose).

**14 NPCs** (`npc_spawns`), tile positions and a `facing` 0-8, two of them `travels = 1`
(Wandering Merchant Martin at 6,145 and Harold at 183,137). Identical to `Lorencia.cs:46-59`;
`facing` is OpenMU's `Direction` enum. NPCs have no combat row; they are scenery with a name
until sprint 7's shop.

**One gate** out of Lorencia: `gates` id 17, map 0, rect (133,118)-(151,135), `spawn = 1`. The
Noria side is id 27, map 3, (171,108)-(177,117). Sprint 9's business.

---

## 3. The formulas: where each one actually lives

Searched in the order PLAN.md asks. **Found in `mu.db`: none — `mu.db` holds no formula of any
kind.** **Found in OpenMU Version075/GameLogic: all five.** MU2's `shared/` has a reading of
each, checked against the original below.

### Hit chance — traced

`LEGACY/reference/openmu/src/GameLogic/AttackableExtensions.cs:694-716`:

    defenseRate = defender.GetDefenseRatePvm();      // Stats.DefenseRatePvm
    attackRate  = attacker.Attributes[Stats.AttackRatePvm];
    float hitChance = 0.03f;
    if (defenseRate < attackRate) { hitChance = 1.0f - (defenseRate / attackRate); }

Rolled at `AttackableExtensions.cs:690`: `Rand.NextRandomBool(hitChance)`, which is
`Rand.cs:63-71` — `RandomInstance.NextDouble() <= chance`. Note **`<=`, not `<`**.

MU2's `Combat.cs:67-75` is the same expression but adds `&& attackRate > 0` and rolls
`roll >= chance` → miss, i.e. `roll < chance` → hit, which is `<` not `<=`. Immaterial in
floating point, but it is a departure and it is not marked as one in MU2.

### Damage — traced, and the order is the behaviour

`AttackableExtensions.cs:69-225`, physical arm only. The 0.75-reachable path, in order:

1. `AttackableExtensions.cs:82` critical roll: `Rand.NextRandomBool(Stats.CriticalDamageChance)`.
   In 0.75 the only source is the **luck** item option at +0.05 each (`Combat.cs:236-245`
   traces it to `ItemPowerUpFactory.GetPowerUps`).
2. `:83` excellent roll — **unreachable in 0.75**: `ExcellentOptions` is constructed only by
   Version095d's and Season Six's initialisers. Do not port the excellent arm.
3. `:87-98` defence: `(Attributes[defenseAttribute] + GreaterDefenseBonus) * DefenseDecrement`,
   clamped at 0.
4. `:120-129` the roll: `Rand.NextInt(baseMinDamage, baseMaxDamage)`, **upper bound
   exclusive**; if `max <= min` then `dmg = min`. A critical is `baseMaxDamage` exactly (`:111`).
5. `:137` `dmg = (int)((dmg * duelDmgDec) - defense)` — `duelDmgDec` is 1 outside a duel.
6. `:205-208` **`Overrates`**: `if (!isPvp && defender.GetDefenseRatePvm() > attacker.AttackRatePvm) dmg = (int)(dmg * 0.3);`
   (`AttackableExtensions.cs:728-731`).
7. `:212-217` the level floor: `minLevelDmg = Math.Max(1, (int)attackerLevel / 10); if (dmg < minLevelDmg) dmg = minLevelDmg;`
8. `:220-223` `if (dmg > 1) dmg = (int)(dmg * defender.Attributes[Stats.DamageReceiveDecrement]);`

Everything between them (double wield, two-handed increase, berserker, master tree, raven,
`ArmorDamageDecrease`, `AttackDamageIncrease`) has no 0.75 source feeding it and evaluates to
identity. MU2's `Combat.cs:162-250` is this path and its remarks say exactly the same; the
ordering of 6, 7 and 8 is load-bearing and is what makes armour matter against low monsters.

### Defence, attack rate, damage from stats — traced to the class initialisers

`Beast.cs:2057-2148` (`Player.Reckon`) is MU2's transcription:

    attackRate  = (int)(Level * RatePerLevel + Agility * RatePerAgility + Strength * RatePerStrength)
    defenseRate = (int)(Agility * DefenseRatePerAgility)
    defense     = (int)((Agility * DefensePerAgility + ArmourDefense) * 0.5f)
    minimumDamage = (int)(Strength * MinimumDamagePerStrength) + WeaponMinimum
    maximumDamage = (int)(Strength * MaximumDamagePerStrength) + WeaponMaximum

with the per-class rates at `Beast.cs:2391-2393`:

| class | start str/agi/vit/ene | rate/lvl | rate/agi | rate/str | defRate/agi | def/agi | minDmg/str | maxDmg/str | hp base/lvl/vit |
|---|---|---|---|---|---|---|---|---|---|
| Dark Knight | 28/20/25/10 | 5 | 1.5 | 0.25 | 1/3 | 1/3 | 1/6 | 1/4 | 35/2/3 |
| Dark Wizard | 18/18/15/30 | 5 | 1.5 | 0.25 | 1/3 | 0.25 | 1/8 | 1/4 | 30/1/2 |
| Fairy Elf | 22/25/20/15 | 5 | 1.5 | 0.25 | 0.25 | 1/10 | 0 | 0 | 39/1/2 |

**Traced, and the trail is indirect enough to write down.**
`Version075/CharacterClassInitialization.cs` is 34 lines; at `:31-33` it calls
`CreateDarkKnight`, `CreateDarkWizard` and `CreateFairyElf` on the shared base in
`Persistence/Initialization/CharacterClasses/`. So the per-class numbers live in files that are
**not** under `Version075/` but are what Version075 constructs. Spot-checked for the Dark
Knight, `CharacterClasses/ClassDarkKnight.cs`:

| MU2 field | value | OpenMU line |
|---|---|---|
| `DefensePerAgility` | 1/3 | `ClassDarkKnight.cs:51` (`Stats.DefenseBase`, 1/3 × TotalAgility) |
| `DefenseRatePerAgility` | 1/3 | `:52` |
| `RatePerLevel` | 5 | `:54` (`AttackRatePvm`, 5 × TotalLevel) |
| `RatePerAgility` | 1.5 | `:55` |
| `RatePerStrength` | 0.25 | `:56` |
| `HealthPerLevel` | 2 | `:68` |
| `HealthPerVitality` | 3 | `:69` |
| `MinimumDamagePerStrength` | 1/6 | `:70` |
| `MaximumDamagePerStrength` | 1/4 | `:71` |
| `BaseHealth` | 35 | `:107` |
| `PointsPerLevel` | 5 | `:37` (`Stats.PointsPerLevelUp`) |

and the halving — `defense = DefenseBase * 0.5` — is
`CharacterClasses/CharacterClassInitialization.cs:103`
(`Stats.DefenseFinal, 0.5f, Stats.DefenseBase`), shared by all three classes.

The Dark Wizard's and Fairy Elf's tables were **not** checked line by line; `ClassDarkWizard.cs`
and `ClassFairyElf.cs` are the files to read. The starting stat triples in `characters` agree
for all three, which is a weak check on four numbers each.

### Experience — traced, no table exists anywhere

`Persistence/Initialization/GameConfigurationInitializerBase.cs:87-100`:

    if (level == 0) return 0;
    if (level < 256) return 10 * (level + 8) * (level - 1) * (level - 1);
    return 10 * (level + 8) * (level - 1) * (level - 1)
         + 1000 * (level - 247) * (level - 256) * (level - 256);

**Cumulative from level 1**, not per-level. Maximum level 400 —
`GameConfigurationInitializerBase.cs:42`, `this.GameConfiguration.MaximumLevel = 400;`.

`AttackableExtensions.cs:601-623`, what a kill is worth:

    tempExperience = (targetLevel + 25) * targetLevel / 3.0;
    if (killerLevel > targetLevel + 10) tempExperience *= (targetLevel + 10) / killerLevel;
    if (targetLevel >= 65) tempExperience += (targetLevel - 64) * (targetLevel / 4);
    return Math.Max(tempExperience, 0) * 1.25;

The 1.25 is inside the formula, before any configurable rate. `killerLevel` is a `float` and
the division at line 615 is float division — a C++ port that makes it integer is silently
wrong for every over-levelled kill. Nothing in Lorencia reaches level 65, so the third term is
dead here but cheap to keep.

Level-up loop: `Beast.cs:1051-1084` (`Player.Gain`), MU2's reading of
`PlayerExperience.AddExperienceCoreAsync` — a **while** loop that can carry more than one
level from a single kill, `PointsPerLevel = 5` (`Beast.cs:2234`), and health/mana refilled
*after* re-reckoning, not before. The OpenMU original of that loop was **not** opened in this
census; the loop shape is MU2's word.

### Walking speed and the tick

`Realm.cs:49-52`: `public const int Hz = 20; public const int Pace = 1000 / Hz;` — 50 ms a
tick, which is `conventions.md` "Time".

`Things.cs:583`: `public virtual int MoveDelay => 400;` — 400 ms per tile, for players and (via
`monster_kinds.move_delay`) for every Lorencia monster. MU2's remark at `Things.cs:565-575`
says a source stores 12 and the right number is 10, and that MU's walk clip is authored at
0.8 s for two steps = 400 ms a step. **That derivation is MU2's, from MuMain's animation, and
is not traced to a line in this census.** The 400 in `mu.db` *is* traced —
`Lorencia.cs:87`, `MoveDelay = new TimeSpan(400 * TimeSpan.TicksPerMillisecond)`.

The per-tick distance, from `Walker.cs:211`:

    left = 1000f / max(1, MoveDelay) * seconds * max(0, MovesAt)

`seconds` is one tick, 0.05. So **one tick of movement = `1000/400 * 0.05 = 0.125 tiles`**, i.e.
2.5 tiles (2.5 m) a second, 8 ticks to cross a tile. That is one factor of the tick rate, which
is what `conventions.md` "Time" demands of a *speed*; an acceleration would take two and there
is none — `Walker.cs:188-200` says MU walks at one speed and every ramp and brake was tried and
removed.

The one 0.75 modulator is `MovesAt` (`Stats.MovementSpeedFactor`), seeded at 1 and halved by
Ice only (`Walker.cs:201-210`, citing `ZzzCharacter.cpp:6353` `Speed *= 0.5f`). Nothing else
touches it.

**The diagonal**: MU2's walker has no diagonal penalty because a line has a length
(`Walker.cs:63-67`). OpenMU's `GetStepDelay` scales the delay by 1.414. The A* *cost* is still
5 straight / 7 diagonal (`Route.cs:85-88`, "the client truncates 5 * 1.414 to 7"), which is a
search cost and not a speed. Mixing the two makes diagonal walking either 41% fast or 41% slow
and it will read as a pacing bug, not a number bug.

### Drops — formula found, pool cannot be built from mu.db

`Loot.cs:88-210` traces to `GameConfigurationInitializerBase`'s base drop item groups and
`DefaultDropGenerator`: one roll per kill against ascending group chances — excellent at
r ≤ 0.001, an item at r ≤ 0.301, money at r ≤ 0.801, nothing above (0.1% / 30% / 50%);
`LuckChance = 0.25`, `SkillChance = 0.5`, `ItemDropDuration = 60 s`, `MaximumItemLevel = 11`,
`DropLevelMaxGap` windowing the pool. Sprint 7, but noted because the *pool* is
`items` filtered by `drop_level` and by `DropsFromMonsters`, and `mu.db` has no
`DropsFromMonsters` column.

---

## 4. The attribute grid, measured

`assets/world/lorencia/attributes.png`, 256×256, 8-bit RGB, uninterlaced. Decoded here with a
standalone PNG reader; the fractions below are counts over all 65 536 tiles.

**The green channel is entirely zero.** MU's grid is 16 bits a tile and MU2 reads it as
`red | (green << 8)` (`Terrain.cs:87`); `src/content/ground.cpp:126` reads the red channel
alone. For Lorencia the two agree exactly. They will not necessarily agree for another map, and
nothing currently says so.

**Only five distinct words occur in the whole map:**

| word | tiles | meaning |
|---|---|---|
| 0x00 | 45 219 | open ground |
| 0x01 | 1 492 | safe zone, walkable |
| 0x02 | 1 | Character — **one tile, at (221, 11)**, baked into the file |
| 0x04 | 17 756 | NoMove |
| 0x05 | 1 068 | safe zone + NoMove |

| measure | fraction |
|---|---|
| walkable (`!(0x04\|0x08)`) | **71.28%** (46 712 tiles) |
| blocked | **28.72%** (18 824 tiles) |
| safe zone (0x01) | **3.91%** (2 560 tiles), bounding box x 75-172, y 77-166 |
| safe **and** walkable | 1 492 tiles |
| NoGround 0x08, Action 0x20, Height 0x40, CameraUp 0x80 | **0% — never set on this map** |

**What the bits are**, from `Route.cs:65-82` (MU2's naming of MU's own flags):

    0x0001 SafeZone     0x0002 Character    0x0004 NoMove     0x0008 NoGround
    0x0020 Action       0x0040 Height       0x0080 CameraUp

with `NonBlocking = Action | Height | CameraUp` and the passability test
`Route.cs:186`: `Passable(flags, wall) => (flags & ~NonBlocking) < wall`, where `wall` is
`Character` (2) in the strict pass and relaxed to `NoMove` (4) near a safe zone
(`Route.cs:691-693`).

**Two definitions of "blocked" are live in this repo and they are not the same.**
`src/content/ground.cpp:394-398` is `(a & 0x04) == 0 && (a & 0x08) == 0`; `Route.Open` is
`(flags & ~0xE0) < 4`. On Lorencia the only tile they disagree about is (221, 11), which both
happen to call walkable. On a map with Height set on a NoMove tile they differ everywhere:
`Route.cs:471-474` spells out that NoMove-with-Height stops a walk and does not stop a spell.
Sprint 5 must pick one and write it on `conventions.md`, and the ground's test and the sim's
test must be one function.

`Terrain.Safe` (`Terrain.cs:102`) is `(At(column,row) & 0x0001) != 0` and it is what stops a
monster attacking out of the town (`Realm.cs:2466-2469`).

---

## 5. What a seeded headless run would need to be byte-identical

Not a design — the list of everything that can make the same seed print different bytes.

**Random draws.** One source, seeded, is the rule (`Realm.cs:118,145`). Every call site is a
place the *number of draws* must not vary:
- the hit roll, one draw per swing (`AttackableExtensions.cs:690`)
- the critical roll, **drawn only when there is a chance to draw against** (`Combat.cs:184`:
  `attacker.CriticalChance > 0f && dice.NextDouble() < ...`). This short circuit is load-bearing:
  drawing unconditionally consumes a number and shifts every later draw. C++'s `&&` short
  circuits the same way, but a refactor that hoists the draw out breaks every log.
- the damage roll, drawn only when `max > min` (`AttackableExtensions.cs:120-128`)
- the rouse jitter, `dice.Next(1, max(2, Hz/4))` and **not drawn when provoked** (`Realm.cs:2386`)
- the approach offset `dice.Next(-1, 2)` twice (`Realm.cs:1735-1736`)
- the wander target, two draws (`Realm.cs:3099-3100`); the spawn placement, two draws per attempt
  (`Realm.cs:5767-5768`) — **per attempt, so a rejected blocked tile costs draws**
- the monster's first-thought jitter `dice.Next(0, 100)` (`Realm.cs:5709`)
- the drop chain, up to five draws (`Realm.cs:3796-3853`)

`Rand.NextInt(min, max)` is **upper-exclusive**; `NextRandomBool(chance)` is **`<=`**. A C++
distribution that is inclusive, or an engine whose bit stream differs, changes the game and not
just the log. The generator itself must be ours and pinned — .NET's `Random` is not
reproducible across runtimes and cannot be the reference.

**Iteration order.** `Realm.Step` (`Realm.cs:2188-2314`) fixes it and says why: players walk,
then in-flight things land, then monsters think, then the dead are considered for respawn, then
loot fades. Within each, an indexed loop over a vector. **No `unordered_map` may ever be walked
to produce an event**, and no id may be handed out by anything but a monotonic counter
(`Realm.cs:3867`, `this.nextId++`). Spatial queries (`Rouse`'s linear scan over players,
`Realm.cs:2354`) must stay linear scans in a fixed order, not grid-bucket walks.

**Floating point.** The sim is float today (`Walker.cs` positions, `Combat.Chance`). On one
Mac, one compiler, `-ffp-contract=off` and no `-ffast-math`, a float sim replays. It does not
replay across a change of optimisation level if FMA contraction is on. The cheap insurance is
that every *decision* (did it hit, did it cross, is it in reach) is an integer or a comparison
against an integer-derived threshold, and only the drawn position is float.

**Time.** The sim's only clock is its own tick counter (`Chronicle.Now = this.Tick`). Nothing
in a step may read a wall clock, and `Realm.cs` converts every ms/second delay to ticks at one
place. A delay that rounds differently (`Ticks(ms)` — floor vs round) shifts every swing.

**Allocation and addresses.** No allocation in a step (PLAN foundation 8). Nothing may sort or
compare by pointer.

**The two logs.** The proof sentence is "a seeded 10 000-tick hunt that logs the same bytes
twice", so the event log's own formatting is part of the contract: float formatting must be
fixed-precision, and any `%g` of a float is a byte difference waiting for a compiler change.

---

## 6. TRAPS

1. **`monster_kinds.respawn_seconds` is 10 for all sixteen rows and 0.75 says 3 for Bull
   Fighter and Budge Dragon.** Lorencia's two commonest low nests refill three times slower
   than MU's. Nothing looks wrong; the map just feels empty. §1.

2. **`mu.db` has no experience, level or drop table, and PLAN.md foundation 11 says it does.**
   Experience is two expressions (§3) and must be computed, not cooked. A cook that emits an
   "experience table" is inventing one.

3. **Cumulative vs per-level experience.** `CalculateNeededExperience(level)` is the *total* to
   *be* that level. `Player.Gain` subtracts against it (`Beast.cs:1058-1064`). Reading it as
   "experience for this level" makes levelling roughly quadratically too fast and the curve
   still looks like a curve.

4. **`killerLevel` is a float in the experience penalty.** `(targetLevel + 10) / killerLevel`
   at `AttackableExtensions.cs:615`. Integer division there gives 0 for every kill more than
   ten levels down and silently stops all low-level farming.

5. **The excellent damage arm is not 0.75.** `AttackableExtensions.cs:83` and `:107-112`.
   `ExcellentOptions` is built only by Version095d and Season Six. Porting it adds a damage
   band the version does not have, and adds a random draw that desynchronises every seeded log.
   The same goes for double wield, two-handed increase, berserker, master-skill tree and
   `ArmorDamageDecrease`: all present in the file, all identity in 0.75.

6. **Stats 0.75 does not have.** A damage formula wanting `DefenseIgnoreChance`,
   `GreaterDefenseBonus`, `AttackDamageIncrease` or `DamageReceiveDecrement` to be *sourced*
   will find nothing granting them. `DamageReceiveDecrement` is granted by exactly one thing in
   0.75 — the knight's Defense skill at 0.50 for 4 seconds (`Rows.cs:516-528`) — and the
   critical chance by exactly one — the luck item option at 0.05 each.

7. **Two definitions of "blocked" already exist in this repo** (§4) and will not disagree on
   Lorencia, so a wrong one ships and shows up on the first map that sets Height or Action.

8. **The attribute grid must never be mipped or filtered.** `conventions.md` says so; it is
   repeated here because sprint 5 is the first code that *reads* it for a decision rather than
   for a count.

9. **Spawn rectangles are 6-12% unwalkable and two clip the safe zone.** Blind placement puts
   monsters inside walls and inside the town. Rejection sampling costs random draws per attempt,
   which is itself a determinism surface (§5).

10. **`monster_spawns.y` is a grid row, not a world z.** Negated on the way in
    (`conventions.md`, "Space"). A nest placed without the negation mirrors about the x axis and
    still looks like a nest in the right part of the map.

11. **The diagonal is counted twice if both rules are ported.** A* cost 5/7 is a *search* cost
    (`Route.cs:85-88`); OpenMU's `GetStepDelay` × 1.414 is a *speed* penalty MU2 deliberately
    dropped (`Walker.cs:63-67`). Taking both makes diagonals slow; taking neither makes them
    fast. `conventions.md` "Time" calls this class of error out by name.

12. **`characters.class` is MU2's 0/1/2, not MU's packed class byte** (`Beast.cs:2460-2466`).
    A save file that writes the wire value and a loader that reads MU2's makes a Dark Knight a
    Dark Wizard.

13. **Two of MU2's numbers are still MU2's word.** The `Gain` level-up loop's shape
    (`Beast.cs:1051-1084`, claimed from `PlayerExperience.AddExperienceCoreAsync` — not opened
    here), and the Dark Wizard's and Fairy Elf's `Rates` rows (`Beast.cs:2392-2393` — the Dark
    Knight's row is traced in §3, the other two are not). `CharacterClasses/ClassDarkWizard.cs`
    and `ClassFairyElf.cs` are the files. Also: the **400 ms move delay for a *player*** is
    `Things.cs:583`, derived by MU2 from MU's walk clip; the 400 for a *monster* is traced
    (`Lorencia.cs:87`). A player's is untraced — it would be an invention if it were not simply
    borrowed from the monster row.

14. **The skills must not be baked in.** PLAN.md "The skills are not MU's" — sprint 5's AI,
    cooldown and swing scheduling must not assume a skill comes from an item or that a swing
    has no cooldown. `monster_kinds.attack_skill` (Lich = 2, Meteorite) is a monster's
    *animation and projectile choice*, not a second damage table (`Combat.cs:97-101`), and
    reading it as a skill system is the shape that would have to be dug out again.
