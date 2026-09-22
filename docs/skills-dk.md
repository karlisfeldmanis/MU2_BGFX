# The Dark Knight's skills: 0.75's formulas, and the cooldowns that replace them

Written 2026-09-22 before any code, and **the first skill is now built** — see §6, written the same
day, for what is in the tree and what is still owed. This is the design for the skill sprint
`PLAN.md` puts after sprint 9, and it answers four questions in the order they have to be answered:

1. **What does 0.75 actually say** about the knight's six skills — every number, cited.
2. **What do WoW and LoL do with cooldowns**, since MU has nothing to copy here.
3. **What is ours**, marked `invention`: the orb route, the QWER bar, and the two formulas — skill
   damage off strength, cooldown off agility.
4. **What is already in this tree**, so the sprint does not rebuild it.

Sources: OpenMU at `LEGACY/reference/openmu` (paths below are relative to `src/`), MuMain at
`LEGACY/reference/MuMain/src/source`, and `MU2/docs/skills.md`, which is the same research done for
the Godot client and carried here — with one correction, §1.3.

---

## 1. MU 0.75, traced

### 1.1 The six skills

`Version075/SkillsInitializer.cs:58-63`, argument order from
`Skills/SkillsInitializerBase.cs:51-71`.

| № | skill | damage | range | mana | AG | moves to target | moves target | clip | play speed |
|---|---|---|---|---|---|---|---|---|---|
| 18 | Defense | — | 0 (self) | 30 | 0 | — | — | `PLAYER_DEFENSE1` (187) | 0.32 |
| 19 | Falling Slash | 0 | 3 | 9 | 0 | yes | yes | `…SKILL_SWORD1` (60) | 0.30 |
| 20 | Lunge | 0 | 2 | 9 | 0 | yes | yes | `…SKILL_SWORD2` (61) | 0.30 |
| 21 | Uppercut | 0 | 2 | 8 | 0 | yes | yes | `…SKILL_SWORD3` (62) | 0.27 |
| 22 | Cyclone | 0 | 2 | 9 | 0 | yes | yes | `…SKILL_SWORD4` (63) | 0.30 |
| 23 | Slash | 0 | 2 | 10 | 0 | yes | yes | `…SKILL_SWORD5` (64) | 0.24 |

- **No level requirement, no energy requirement, no ability cost, no cooldown, no skill level.**
  `CreateSkill` is called with mana and nothing else to qualify for — the wizard's twelve all carry
  an energy requirement and the knight's carry none.
- **`movesToTarget` is a gap-closer**: the knight is placed *on the target's tile* before the blow.
  **`movesTarget` is a knock**: the monster is shoved one tile at random. **We keep the knock and
  throw the gap-closer away** — see §3.1a, which is a decision of the user's and the reason the
  `range` column above is only history.
- **The range check is `Range + 2`**, not `Range` (`TargetedSkillDefaultPlugin`), and mana is taken
  **after** the range test and **before** the moves. That order is behaviour, not presentation.
- **Defense** is `SkillType.Buff`, `targetRestriction: Self`, and its effect is
  `DamageReceiveDecrement × 0.50 for 4 seconds` (`Skills/DefenseEffectInitializer.cs`). Half damage
  taken, flat, four seconds, thirty mana. `sim/rules.h:43` already carries `damageTaken` for it.
- **Sounds** (`ZzzOpenData.cpp:4784-4788`): `sKnightDefense`, `sKnightSkill1..4` — Cyclone and Slash
  share `SOUND_SKILL_SWORD4`, which is MU's own reuse.
- **Slash alternates** its clip on an odd swing counter (`WSclient.cpp:4308`).
- **Every skill swing lays a weapon streak**, whatever the weapon: `CreateWeaponBlur` tests
  `PLAYER_ATTACK_SKILL_SWORD1..5` before it asks what is in the hand, drawn white off
  `motion_blur_r.jpg` (mapping 2). An axe that streaks nothing on an ordinary swing streaks on a
  skill.

### 1.2 How a knight got a skill in 0.75 — and why we are leaving it

**He did not learn one. He picked one up, and had it only while it was in his hand.** The item *is*
the grant (`Version075/Items/Weapons.cs:278`, `skillNumber` as the fourth argument), there is no
knight scroll (`Version075/Items/Scrolls.cs` is twelve wizard rows), and there is no knight orb —
0.75's four orbs are all Fairy Elf (`docs/mu-scrolls-and-orbs.md` §2).

| skill | carried by (lowest drop level first) | first carrier | its drop level |
|---|---|---|---|
| Defense 18 | Buckler, Skull, Spiked, Tower, Big Round, Serpent, Bronze, Dragon Slayer, Plate | Buckler | 6 |
| Uppercut 21 | Sword of Assassin, Falchion, Serpent Sword | Sword of Assassin | 12 |
| Falling Slash 19 | Morning Star, Double Axe, Tomahawk, Battle Axe, Nikkea Axe | Morning Star | 13 |
| Lunge 20 | Gladius | Gladius | 20 |
| Cyclone 22 | Blade, Berdysh, Great Scythe | Blade | 36 |
| Slash 23 | Giant Sword, Crystal Sword, Chaos Dragon Axe | Giant Sword | 52 |

`Version075/Items/Weapons.cs:93-132`, `Armors.cs:40`. That ladder — 6, 12, 13, 20, 36, 52 — is worth
keeping even though the route is being replaced: it is MU's own answer to *when* a knight should meet
each skill, and §3.3 uses it as the orbs' drop levels.

### 1.3 A knight's skill does about **twice** his swing, and `MU2/docs/skills.md` says otherwise

`MU2/docs/skills.md` states "a knight's skill does exactly the damage his swing does. Not more."
That reading is **wrong** and this document corrects it. `skill.AttackDamage` is indeed 0 on all
five, so nothing is added to the band — but the multiplier at the end of the calculation is not 1:

```
// AttackableExtensions.cs:226-247, and it runs for every hit that has a skill
if (skill != null) {
    var multiplier = attacker.Attributes[Stats.SkillMultiplier];
    ...
    dmg = (int)(dmg * multiplier * damageFactor);
}
```

and the Dark Knight's class row sets that attribute outright:

```
// ClassDarkKnight.cs:74  and  :112
AttributeRelationship(Stats.SkillMultiplier, 0.001f, Stats.TotalEnergy);
ConstValueAttribute(2, Stats.SkillMultiplier);
```

So **`SkillMultiplier = 2 + energy/1000`** for a knight, applied after defence, the level floor and
`AttackDamageIncrease`. The Dark Wizard's own base is `1` (`ClassDarkWizard.cs:112`) — his spells get
their force from `MinimumWizBaseDmg = energy/9`, `MaximumWizBaseDmg = energy/4` (`:72-73`) plus the
skill's own `AttackDamage`, which is where the wizard's "spells scale with energy" comes from.

Checked against the version gate: the per-skill multipliers in
`Updates/FixSkillMultipliersPlugIn.cs` are `DataInitializationKey => VersionSeasonSix`, so they are
**not** 0.75. 0.75's knight has one flat multiplier for all five skills.

**This matters for §3, because it means the thing the user asked for already exists in 0.75's own
shape** — a skill multiplier that grows with a stat. What we change is which stat, and how fast.

---

## 2. How WoW and LoL build cooldowns

MU has no cooldown to transcribe — `cooldownMinutes` is 0 on every 0.75 skill, and the client's
`SkillAttribute[].Delay` (`GameLogic/Skills/SkillManager.cpp:140-170`) is a Season 6 field that is
zero for all six of these. So the shape has to be borrowed, and these are the two systems worth
borrowing from.

**LoL — Ability Haste.** Since the 2021 preseason, a cooldown is
`base × 100 / (100 + AH)`, equivalently `base / (1 + AH/100)`, and the effective reduction is
`AH / (AH + 100)`. It replaced a flat "cooldown reduction" percentage that had to be **capped at
40%**, because a percentage subtracted from 1 reaches zero and has to be fenced. Three properties
came out of the change and all three are why it is the right base here:

- **No cap is needed.** The function approaches zero and never touches it, so stacking can be left
  open-ended without a special case at the end.
- **Every point is worth the same.** 100 haste is +100% casts, 200 is +200%: the stat is linear in
  *casts per minute*, which is the thing the player feels, rather than in seconds saved. A flat
  percentage is worth more the closer you are to the cap, which is a balance trap.
- **It is the same maths as armour**, so it composes with everything else additively in the stat and
  multiplicatively in the outcome.

**WoW — haste.** Same divisor: `base / (1 + haste)`. Two extra ideas worth taking:

- **A floor, not a zero.** The global cooldown is 1.5 s and haste takes it to **0.75 s and no
  further**. Blizzard did not let the number reach zero; it hits a wall and the animation becomes
  the limiter. That is how "no cooldown" is delivered in a game that never divides by zero.
- **Charges.** An ability can hold 2–3 uses with a recharge timer that haste also shortens. This is
  the tool for a skill that should come in a burst and then be gone — two Uppercuts back to back and
  then a wait — without making it spammable. Not in the first pass; the shape is here so the
  cooldown state is built as a deadline plus a count rather than a deadline alone.

Diablo 3, which `PLAN.md` names as the shape of our bar, stacks cooldown reduction
**multiplicatively** — `(1−a)(1−b)…` — which also never reaches zero but makes each source worth
less than the last and is hard to read off a character sheet. **Not copied**: the divisor is the same
curve with an explanation the tooltip can print.

**The conclusion for us**: one haste number per character, `cd = base / (1 + haste)`, floored
per-skill. Not a percentage, not a cap, not multiplicative stacking.

---

## 3. Ours, and marked as ours

Everything in §3 is `invention` on the scale `PLAN.md` reserves for the skill system. It is the one
place the rules leave 0.75 on purpose.

### 3.1 The spine: strength is force, agility is speed

The user's rule, 2026-09-22: *"like it is for DW spells — they get stronger when the main stat is
bigger, energy for DW; for DK it is strength. And agility probably reduces the cooldown."* That maps
onto 0.75's own arithmetic cleanly, because both hooks already exist:

| what | 0.75's own version | ours | why it is the same shape |
|---|---|---|---|
| skill damage | `SkillMultiplier = 2 + energy/1000` (`ClassDarkKnight.cs:74`, `:112`) | `M = M₀ + strength/K_dmg` (+ the energy term kept) | the attribute and its place in the calculation are unchanged; the input stat and the slope are ours |
| cooldown | nothing | `H = agility/K_cd`, `cd = base/(1 + H)` | new, §2's divisor |
| attack speed | `AttackSpeed += agility/15` (`ClassDarkKnight.cs:58`) | unchanged | agility is *already* the knight's speed stat; the cooldown follows the stat that was always about rate |

Strength and agility are both already load-bearing (`sim/rules.cpp` reads strength for the damage
band and agility for attack rate, defence and swing speed), so neither formula introduces a stat
the character sheet does not already explain.

### 3.1a Close combat only: no leaps, and the skill is punctuation in an auto-attack fight

The user's rule, 2026-09-22: *"don't use tile gaps for DK skills, they have to be close combat only —
only the AOE is different"*, and *"most of the time the DK starts combat with auto attack and then he
can use skills."* Both are departures from 0.75 and both narrow the design, so they are written out
before the numbers that depend on them.

**Every skill is cast at the knight's own reach.** `kHeroAttackRange = 1` in `realm_tuning.h:77`,
measured MU's way (the larger of the two axis distances) — the same test a swing already passes, and
the same number a weapon's own reach will replace in sprint 7. **`movesToTarget` is not
implemented at all**: no skill moves the knight, ever. He walks to the monster on his own feet, as he
already does, and then presses a key. There is no `Close`, no leap, no dash, and nothing to
desynchronise between the sim and the drawn body.

**One consequence to accept knowingly.** In 0.75 a knight's skills bought him *reach and movement*
and not force (Falling Slash at three tiles was the pull). Taking the movement away leaves the skills
with nothing but damage, area and the knock — so **the multipliers in §3.2 are now the whole reason to
press a key**, rather than a bonus on top of a gap-closer. If the skills feel pointless in play, that
table is the knob, not the range.

**What each skill is, then:**

| skill | reach | who it hits | knock | why |
|---|---|---|---|---|
| Falling Slash 19 | 1 | one | off | the overhead: the heaviest single blow |
| Lunge 20 | 1 | one | off | the cheap jab — shortest cooldown, smallest multiplier |
| Uppercut 21 | 1 | one | off | the rising blow, between the two |
| Cyclone 22 | 1 | **everything within 1 tile** — all eight neighbours and his own | no | he spins; it is the crowd answer |
| Slash 23 | 1 | **the three tiles in the facing arc** (ahead and the two diagonals beside it) | no | a wide two-handed sweep, heavy and slow |
| Defense 18 | self | himself | — | the guard |

- **The two area skills are the only thing that is not one target**, which is what "only the AOE is
  different" means: the *area* is different, the *range* never is. Both are centred on the knight, not
  thrown at a tile, so neither needs a ground target, a cursor mode or a frustum — Cyclone is a
  radius-1 ring test and Slash is three named tiles off his facing. That is two lines of geometry and
  no new aiming.
- **The knock is dropped on the two area skills** and kept on the three single ones. Shoving a crowd
  apart is the opposite of what a crowd skill is for: the knight who just gathered four monsters
  around him would scatter them out of his own reach. This is ours, not MU's — MU had `movesTarget`
  on all five.
- **Area damage is per target, rolled per target**, so one Cyclone into four monsters is four
  `strike()` calls with four hit rolls. Seeded-log order is therefore fixed: nearest first, then
  clockwise from north, so the same seed gives the same bytes.

**A skill needs the hand it is thrown with.** The user's rule, 2026-09-22, and it is 0.75's own
arrangement restored rather than a new gate: in the original the weapon *was* the skill, so "no
weapon, no Falling Slash" was true by construction, and learning a skill permanently is exactly
what took that away. So:

- **The five attacks need a blade in the right hand** -- not empty, not a shield, not a bow or a
  crossbow, because all five clips are `PLAYER_ATTACK_SKILL_SWORD1..5` and the streak MU lays on
  them is a blade's.
- **Defense needs a shield on the arm**, which is where skill 18 lived: the Buckler and the nine
  shields after it (`Version075/Items/Armors.cs:40`).
- The box is drawn **disabled** when the hand is wrong or the mana is short -- cold-tinted and
  washed, a different state from the cooldown's dark, because those two must not read as each
  other.

**The fight's rhythm: auto-attack is the floor, a skill is the beat.** The knight opens by clicking a
monster and keeps swinging — that standing order is already in the tree. A skill press does **not**
cancel it:

- Pressing Q..R while an attack order stands **spends this swing on the skill and then stops the
  auto-attack.** (Revised 2026-09-22 on the user's rule; the first pass kept the order standing and
  went on swinging.) A skill ends the exchange and going back to hitting the monster is another
  click. A skill on cooldown changes nothing and the ordinary blow lands.
- **He does not turn or move while the clip runs.** The blow is thrown where he was standing and
  facing when he threw it: no re-aim at a quarry that shuffles round him, no re-path, no step. A
  body that swivels or slides under its own animation reads as a teleport, which is the same
  objection that took the gap-closers out.
- **And the knock is off for the same reason.** 0.75's `movesTarget` puts the monster on a
  neighbouring tile at once, which is a one-tile teleport of somebody else. The column and
  `Realm::shove` are kept and unreached; they come back the day a body can be pushed over a few
  ticks rather than moved.
- **A cast pays the swing timer** (`Fighter.swingsAt`), so a skill cannot be squeezed between two
  swings for free. This is the real global floor under §3.2's cooldowns, and it is MU's own number
  rather than an invented GCD.
- **A skill with no target under the order does nothing** if it needs one, and Defense casts on
  himself regardless — MuMain's split holds: left walks and swings, right and the keys cast.

### 3.2 The two formulas

**Damage.** For a skill hit, everything runs exactly as `sim::strike` runs it today — the band, the
hit roll, defence, the level floor — and then:

```
M = M₀ + strength/K_dmg + energy/1000        // energy term is 0.75's own, kept
damage = strike(...) × M
```

| skill | hits | M₀ | K_dmg | at 28 str (level 1) | at 500 str | at 1000 str | at 2000 str |
|---|---|---|---|---|---|---|---|
| Lunge 20 | one | 1.4 | 1400 | 1.42 | 1.76 | 2.11 | 2.83 |
| Uppercut 21 | one | 1.7 | 1200 | 1.72 | 2.12 | 2.53 | 3.37 |
| Falling Slash 19 | one | 2.0 | 1000 | 2.03 | 2.50 | 3.00 | 4.00 |
| Cyclone 22 | up to 9 | 1.3 | 1400 | 1.32 | 1.66 | 2.01 | 2.73 |
| Slash 23 | up to 3 | 1.8 | 1000 | 1.83 | 2.30 | 2.80 | 3.80 |

`M₀ = 2.0` on Falling Slash is 0.75's own number, and it is the anchor the other four are spread
around — **Falling Slash is the heaviest single blow and the two area skills are paid in coverage
rather than in force.** Cyclone's 1.3 is per target, so it is the weakest key against one monster and
by far the strongest against four; that trade is the only thing making the two kinds of skill
different now that none of them closes a gap. **The damage grows twice over** — the band itself is `strength/6` to `strength/4`
(`ClassDarkKnight.cs:71-72`) — so a level-400 strength knight is not 4× a level-1 one, he is roughly
4× the multiplier on top of ~70× the band. That compounding is intended and is the reason the slopes
are per-mille rather than per-hundred.

**Cooldown.**

```
H  = agility / K_cd                    K_cd = 300
cd = max(floor, base / (1 + H))
```

| skill | base cd | floor | at 20 agi | at 300 agi | at 600 agi | at 1000 agi | at 1500 agi |
|---|---|---|---|---|---|---|---|
| Lunge 20 | 3.0 s | clip (~0.9 s) | 2.8 | 1.5 | 1.0 | **0.9** | **0.9** |
| Uppercut 21 | 3.0 s | clip (~0.9 s) | 2.8 | 1.5 | 1.0 | **0.9** | **0.9** |
| Falling Slash 19 | 4.0 s | clip (~1.0 s) | 3.8 | 2.0 | 1.3 | 1.0 | **1.0** |
| Cyclone 22 | 5.0 s | clip (~1.0 s) | 4.7 | 2.5 | 1.7 | 1.2 | **1.0** |
| Slash 23 | 6.0 s | clip (~1.1 s) | 5.6 | 3.0 | 2.0 | 1.4 | 1.1 |
| Defense 18 | 12.0 s | duration + 2 s | 11.3 | 6.0 | **6.0** | **6.0** | **6.0** |

Defense is floor-bound from about 300 agility onward, and its floor is the only one that moves: it is
`duration + 2`, so a knight who spends on vitality buys both a longer guard and a longer wait for it
(6 s at vitality 0, 12 s at the 10 s cap).

A knight gets 5 points a level (`PointsPerLevelUp 5`) on top of 28/20/25/10, so agility 300 is about
level 60 if he spends half on agility, 1000 is about level 200, and 1500 about level 300 for an
agility-first build. **The short skills stop having a cooldown somewhere around level 170–200 and the
heavy ones never quite do** — which is the pacing worth having: the jab becomes free, the big sweep
always has a beat.

**Where "no cooldown" comes from, and why it is a floor rather than a zero.** Three walls, in the
order they bite:

1. **The clip.** A skill cannot be fired faster than its own animation
   (`length = keys / ((speed + attackSpeed × 0.004) × 25)`, `sim/swings.cpp`), and that length itself
   shrinks with attack speed, which agility also buys. When the cooldown drops under the clip there
   *is* no cooldown: the button is ready before the knight has finished swinging. This is WoW's GCD
   floor and it is what the user's "at some point there is no cooldown at all" should mean.
2. **The swing timer.** `Fighter.swingsAt` already paces blows; a cast is a blow and pays it. So even
   a floorless skill cannot outrun the weapon.
3. **A buff's own duration.** Defense must not become permanent 50% damage reduction, so its floor is
   **its duration + 2 s**, computed rather than constant — and its duration is where vitality goes:
   `seconds = 4 + vitality/100`, capped at 10 s (`invention`; 0.75's four seconds is the value at
   vitality 0). At vitality 800 that is 12 s of cooldown for 10 s of half damage, which is a strong
   button and not an always-on one.

**Mana stays 0.75's** — 8, 9, 9, 9, 10, 30. It stops being the limiter by about level 30, and that is
fine: the cooldown is the new cost, and this is one fewer number invented. If mana should keep
mattering, the knob is `mana = base × M` (the skill costs what it hits for) — **not** recommended for
the first pass, because it re-introduces the potion spam the cooldown exists to replace.

**One tuning knob each.** `K_cd = 300` decides *when* a build runs out of cooldown; `K_dmg` per skill
decides how much strength is worth. Both belong in a live-reloaded sheet
(`sheets/skills.json`), like `lighting.json`, so a session can judge the pacing without a rebuild.

### 3.3 The orbs

The user's route: *bought or dropped, right-click to learn, permanent.* 0.75 has no knight orb, so
six are invented — modelled on 0.95d's `Orb of Twisting Slash` (group 12, number 7), which is MU's
own precedent for a knight orb. Group 12 in 0.75 is wings (0, 1, 2), the elf's four orbs (8–11) and
the Jewel of Chaos (15), so 3–7 are free; 20 is free in this tree's Season 6 list too.

| item | № | teaches | req. level | drop level | price | sold by |
|---|---|---|---|---|---|---|
| Orb of Defense | 3 | Defense 18 | 6 | 6 | 500 | Hanzo the Blacksmith |
| Orb of Uppercut | 4 | Uppercut 21 | 12 | 12 | 2,500 | Hanzo |
| Orb of Falling Slash | 5 | Falling Slash 19 | 13 | 13 | 3,000 | Hanzo |
| Orb of Lunge | 6 | Lunge 20 | 20 | 20 | 8,000 | — (drop only) |
| Orb of Cyclone | 7 | Cyclone 22 | 36 | 36 | 25,000 | Hanzo |
| Orb of Slash | 20 | Slash 23 | 52 | 52 | 60,000 | — (drop only) |

- **The drop levels are §1.2's ladder**, so a knight meets his skills in the order 0.75 gave him
  them. One line of provenance for six numbers.
- **The requirements are raw**, not run through `(3 × drop level × raw/100) + 20`:
  `docs/mu-scrolls-and-orbs.md` §4 establishes that a non-wearable row's requirement is used
  verbatim, and `sim/items.cpp asks()` currently applies the formula unconditionally — so this is a
  real branch the sprint must add, not a value to pick.
- **Level only, no strength or energy gate.** The stats are what make a skill *good* (§3.2); gating
  them behind the same stats would charge twice.
- One cell wide, one tall, `dropsFromMonsters: true`, durability 1 — the elf orbs' own shape
  (`Version075/Items/Orbs.cs:63-67`).
- Lunge and Slash are drop-only on purpose: 0.75 had exactly this texture (the Gladius was the one
  skill carrier Hanzo's store dropped), and two of six unbuyable keeps the hunt in it.

**Learning is permanent and saved.** A right-click consumes the orb and sets a bit; `save.cpp` gets a
version bump and a `learned` mask (six bits now, one word is plenty). 0.75's hotkey bindings are
**not** saved (`MU2/docs/skills.md` §10, checked against MuMain) — the *learned skills* are ours and
are, the *bar* follows MuMain and is not.

### 3.4 The bar and the frame

- **Q W E R.** `src/game/ui/hud.h:17` already records the decision of 2026-09-21: potions on 1–4,
  skills on Q W E R. Four slots, four skills at once, six learned — that is the Diablo 3 shape
  `PLAN.md` asked for and it needs no fan: a learned-skills list with drag-to-slot is enough, and
  the right-click cast (`MU2/docs/skills.md` §9: *left walks and swings, right casts*) stays.
- **The cooldown is drawn on the box**: a dark sweep over the icon and the seconds printed when more
  than one remains — LoL's and WoW's shared convention, and the reason both read at a glance. Grey
  the icon when the mana is not there (MuMain dims it too).
- **A skill's clip blends in longer than a swing's** -- 0.28 s against the 0.18 s everything else
  uses -- and a step does not cancel it. A swing is a jab out of a stance and the short blend hides
  the join; a skill is a wind-up, and at the swing's blend the body stands in the pose before the
  arm has begun to move, which is what "the animation looks instant" means. The step rule is the
  drawing agreeing with the realm: `throwSkill` holds him still for the whole clip, so a walk
  arriving over a skill is interpolation catching up rather than a step he is taking.
- **The box's card is the item tooltip's card** (`tip::Sheet`), not the gauges' two-line strip:
  what the skill does in a line, the blow's multiplier with its breakdown (`x2.06 of a swing`,
  `x2.00 base`, `+0.05 from 50 strength`), the reach, the cooldown with the haste that shortened it
  (`3.5 s`, `4.0 s base, -11% from 40 agility`) and what is left of it while it runs, the mana
  against what he has, and -- when the key is dark -- the reason in words.
- **The tooltip must print the formula's result**, not just the number: `Cooldown 1.4 s (base 6.0,
  −77% from 1000 agility)` and `Damage ×3.85 (base 2.6, +1.25 from 1000 strength)`. This is the
  whole point of stat-driven cooldowns — a player who cannot see agility working will not spend on
  it. `src/game/ui/describe.cpp` already builds exactly this kind of sheet for items.
- **The icons are already in the tree**: `assets/interface/skills/skill_18.png` … `skill_23.png`, cut
  by `pipeline/skill_icons.py`. Nothing reads them yet.

### 3.4a The blow is begun and then landed

Found by the player on 2026-09-23 and fixed the same day, and it is not a skill bug at all: the
sim settled a blow on the tick the swing was *decided*, while the drawing has always shown it
halfway through the clip (`Showing::kLandingPoint`). So a click that cancelled an attack cancelled
the animation and not the damage — "cancel the attack with a click to move and the damage is still
done."

A swing is now two halves. `Realm::begin` says `Swung` and puts the blow in the air, half the clip
ahead; `Realm::land` resolves it when the arm comes down, and an order that is not the same attack
drops it whole. A cast is the same, over its own clip. **A monster is unchanged** — nothing can
cancel a monster's swing, and giving it a wind-up would move every seeded log for no gain.

### 3.5 The sim's shape

- **Per-figure cooldowns**: six deadlines in ticks on the fighter, not a map — `realm.h:194-198`
  already says the tick is per-body and separate from the thinking clock, which is the hook.
- **Requests in, events out**, per `PLAN.md` point 8: a `Cast` request (skill, target) and a `Cast`
  event (who, skill, at whom); the clip, sound, streak and sparks are the game's to pick from the
  number. `Shove` is one `Place`, for the knock on the three single-target skills; there is no
  `Close`, because nothing moves the knight (§3.1a).
- **Refusal order is 0.75's, with its range test replaced**: safe zone, learned, cooldown, reach
  (`kHeroAttackRange`, not `Range + 2` — the generosity in the original was for a target that might
  have drifted out of sync at three tiles, and at one tile a swing's own test is the right one), mana,
  then the blow, then the knock. Mana is taken before the blow and a refusal is silent.
- **The boon list** — a general timed-effect list on a figure, not a special case for Defense, since
  ale and the elf's two Greaters are the same shape. It feeds `Fighter.damageTaken`, which is already
  there and already 1.0.
- **Headless tests**: a seeded cast run that logs the same bytes twice; invariants that nothing casts
  while cooling, nothing casts what it has not learned, and no cooldown is ever below its floor.

---

## 4. The one decision left to the user

**A floor, or a true zero?** §3.2 floors every cooldown at the animation, so "no cooldown" means *the
button is ready before the swing ends* — WoW's answer, and the reason nothing divides by zero, no
buff becomes permanent, and no skill can be fired twice inside one clip. A literal `0.0 s` is
available instead by using `cd = base × (1 − agility/K)` clamped at zero, which reaches zero at a
stated agility and is what a player counting seconds would call "no cooldown" — at the price of a
cap, a permanent Defense unless it is special-cased, and a spam rate limited only by the swing timer.
**Recommended: the floor.** It delivers the same feel and costs nothing later.

---

## 5. Order of work, when the sprint opens

1. `sheets/skills.json` and the skill table in `content` — the six rows, their mana, range, clips,
   `M₀`, `K_dmg`, base cooldown and floor. Live-reloaded.
2. The six orbs in the cook, with the raw-requirement branch in `items.cpp asks()`, and the learned
   mask in the save.
3. The boon list and Defense, first, while exactly one thing uses it.
4. `Realm::cast` with the refusals in order, the multiplier on the blow, the knock's one `Place`, and
   the two area shapes — Cyclone's radius-1 ring and Slash's three-tile arc, both rolled per target in
   a fixed order so the seeded log stays byte-identical. A skill press keeps the standing attack
   order (§3.1a) and pays the swing timer.
5. The bar: icons, QWER, the sweep, the grey, the tooltip with the arithmetic printed.
6. The showing: clip, sound, white streak, sparks at the far end — all four already exist in this
   tree for swings.
7. The headless run and its invariants; then a tuning pass on `K_cd` with the numbers written down.


---

## 6. What is built, 2026-09-22

**Falling Slash, and the whole cooldown machinery under it.** The other five are rows in the table
with `built = false`: adding one is a row's behaviour, not a row.

- `src/sim/skills.h/.cpp` — the six rows, `force()`, `castTicks()`, `cooldownTicks()` and
  `floorTicksFor()`. The two formulas live here and nowhere else.
- `src/sim/realm_skills.cpp` — `invoke` (the press, held for 30 ticks so a key pressed mid-swing is
  not lost), `learn`/`knows`/`cooling`/`coolsFor`, `throwSkill` with the refusals in order, and the
  knock. A cast pays its cooldown **and** the longer of the swing clock and its own clip.
- `Realm::press` throws the wish before the standing order acts, so a skill spends the next swing
  and the knight goes on fighting; `Realm::strikeAt` takes the multiplier; `What::Cast`,
  `What::Shoved` and `What::Learned` are in the log and the save carries `learned`.
- The bar: `Q W E R` on the window, the icon from `skill_19`, a top-down wipe for the cooldown with
  the seconds printed over it, and the cold disabled tint. `--ui-skill F:K` presses a key in a
  scripted run.
- The clip: action 60 with a 0.28 s blend, the `sKnightSkill1` wave, and a step that no longer cuts
  it short. The cook now carries actions 60–64 and 187 so the sim can read their lengths.

**Measured on a headless hunt** (`--headless --at 190,110 --level 200`): at 20 agility the cooldown
is 75 ticks, at 1015 agility it is 18 — the clip's own floor — and a strength build's blow comes out
at ×3.02 of the swing, which is `2 + 1023/1000` to the digit.

**Three things are owed, and two of them are the user's to decide:**

1. **Mana is now the real limiter, not the cooldown.** A level-30 knight has about 35 mana and the
   skill costs 9, so he throws three and then swings for the rest of the fight — the cooldown never
   gets a chance to matter. 0.75's answer is potions, and they exist; the alternatives are a
   regeneration, a smaller cost, or leaving it as the reason to carry blue potions.
2. **It one-shots Lorencia's trash.** ×2 of a swing kills a Spider outright at level 20. That may be
   exactly right for a skill on a four-second cooldown, and it may be too much; the knob is `force`
   in `sim/skills.cpp` and nothing else reads it.
3. **The orbs are not cooked**, so `Realm::raise` hands a knight Falling Slash and says so in a
   marked block. §3.3 is what replaces that line.
