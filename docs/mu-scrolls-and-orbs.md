# Scrolls (group 15) and Orbs (group 12) — the skill items

Research note, read-only. Sources, in the order they were trusted:

- **OpenMU** — `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/openmu`
  (paths below are relative to `src/`).
- **MuMain** (a Season 6 source tree) — `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/source`.
  Abbreviated `ZI:n` = `Engine/Object/ZzzInventory.cpp:n`, matching the convention already used in
  `MU2_BGFX/docs/mu-tooltip-lines.md`.
- `item_eng.bmd` **was** decoded (see §7). Every name below is OpenMU's, corroborated against both
  the client's enum identifier (`ITEM_SCROLL_OF_POISON`, …) and the client's own data file.

---

## 0. The two groups, and do orbs exist in 0.75?

**Yes. Orbs exist in 0.75, and there are exactly four of them.**

Evidence:

| Fact | Where |
|---|---|
| Group 12 is `Orbs`, group 15 is `Scrolls` | `Persistence/Initialization/Items/ItemGroups.cs:77` and `:92` |
| `Version075/Items/Orbs.cs` exists and is registered in the 0.75 config | `Version075/GameConfigurationInitializer.cs:45` (`new Orbs(...)`), `:46` (`new Scrolls(...)`) |
| It creates four orbs: numbers 8, 9, 10, 11 | `Version075/Items/Orbs.cs:33-37` |
| All four are on sale at **Elf Lala** in the 0.75 merchant tables | `Version075/MerchantStores.cs:231-233` (orbs 8/9/10) and `:239-243` (five Orb-of-Summoning stacks, +0…+4) |
| Group 12 is **shared** with wings in 0.75 — numbers 0/1/2 are Wings of Elf/Heaven/Satan | `Version075/Items/Wings.cs:44-46`, `:87` (`wing.Group = 12`) |
| The client agrees on both the group and the indices | `Core/Globals/_enum.h:2615-2618` (`ITEM_ORB_OF_HEALING = ITEM_WING + 8`, …) with `ITEM_GROUP_WING = 12`, `MAX_ITEM_INDEX = 512` — `Core/Globals/_define.h:382`, `:354`, `:400` |

So a group-12 row is "wing **or** orb **or** Jewel of Chaos (12:15)", decided by the number. The
client's group-12 constant is literally named `ITEM_WING`; there is no separate orb group.
Group 15 in the client is `ITEM_ETC` (`_define.h:385`, `:403`) and holds nothing but scrolls and,
later, parchments — see `Core/Globals/_enum.h:2826-2862`.

Both are 1 cell wide; scrolls are 2 cells tall (`Version075/Items/Scrolls.cs:64-65`), orbs 1 cell
tall (`Orbs.cs:63-64`, the `height` argument is 1 on all four).

---

## 1. Scrolls — group 15, MU 0.75

All twelve are **Dark Wizard only** (`Version075/Items/Scrolls.cs:70`), `DropsFromMonsters = true`
(`:68`), durability 1 (`:69`), level requirement 0 on every one.

Source row: `CreateScroll(number, skillNumber, name, dropLevel, levelRequirement, energyRequirement, money)`
— `Version075/Items/Scrolls.cs:57`.

| # | Name | Teaches (skill №) | Req. level | Req. energy | Drop level | Price (zen) | Sold by |
|---|---|---|---|---|---|---|---|
| 0 | Scroll of Poison | Poison (1) | 0 | 140 | 30 | 17,000 | Pasi the Mage |
| 1 | Scroll of Meteorite | Meteorite (2) | 0 | 104 | 21 | 11,000 | Pasi the Mage |
| 2 | Scroll of Lighting *(sic — one "n")* | Lightning (3) | 0 | 72 | 13 | 3,000 | Pasi the Mage |
| 3 | Scroll of Fire Ball | Fire Ball (4) | 0 | 40 | 5 | 300 | Pasi the Mage |
| 4 | Scroll of Flame | Flame (5) | 0 | 160 | 35 | 21,000 | Izabel the Wizard |
| 5 | Scroll of Teleport | Teleport (6) | 0 | 88 | 17 | 5,000 | Pasi the Mage |
| 6 | Scroll of Ice | Ice (7) | 0 | 120 | 25 | 14,000 | Pasi the Mage |
| 7 | Scroll of Twister | Twister (8) | 0 | 180 | 40 | 25,000 | Izabel the Wizard |
| 8 | Scroll of Evil Spirit | Evil Spirit (9) | 0 | 220 | 50 | 35,000 | — (drop only) |
| 9 | Scroll of Hellfire | Hellfire (10) | 0 | 260 | 60 | 60,000 | — (drop only) |
| 10 | Scroll of Power Wave | Power Wave (11) | 0 | 56 | 9 | 1,100 | Pasi the Mage |
| 11 | Scroll of Aqua Beam | Aqua Beam (12) | 0 | 345 | 74 | 100,000 | — (drop only) |

`Version075/Items/Scrolls.cs:33-44`. Merchants: `Version075/MerchantStores.cs:161-167` (Pasi, named
at `:153`) and `:310-311` (Izabel, named at `:270`).

### Scrolls that are NOT 0.75

| # | Name | Teaches | Arrived in | Where |
|---|---|---|---|---|
| 12 | Scroll of Cometfall | Cometfall (13) | **0.95d** | `Version095d/Items/Scrolls.cs:31` |
| 13 | Scroll of Inferno | Inferno (14) | **0.95d** | `Version095d/Items/Scrolls.cs:32` |
| 14 | Scroll of Teleport Ally | Teleport Ally (15) | Season 6 in this tree | `VersionSeasonSix/Items/Scrolls.cs:51` |
| 15 | Scroll of Soul Barrier | Soul Barrier (16) | Season 6 in this tree | `…/Scrolls.cs:52` |
| 16 | Scroll of Decay | Decay (38) | Season 6 in this tree | `…/Scrolls.cs:53` |
| 17 | Scroll of Ice Storm | Ice Storm (39) | Season 6 in this tree | `…/Scrolls.cs:54` |
| 18 | Scroll of Nova | Nova (40) | Season 6 in this tree | `…/Scrolls.cs:55` |
| 19-27 | …Parchments (Summoner) | Chain Lightning, Drain Life, … | Season 6 (Summoner class) | `…/Scrolls.cs:56-63` |
| 28-29 | Wizardry Enhance, Gigantic Storm | 233, 237 | Season 6 | `…/Scrolls.cs:64-65` |
| 30-36 | …Parchments (Rage Fighter) | 262-268 | Season 6 | `…/Scrolls.cs:66-72` |

`Version095d/Items/Scrolls.cs:13` — the 0.95d class **inherits** the 0.75 one and calls
`base.Initialize()` (`:30`), so 0.95d = 0.75's twelve **plus** Cometfall and Inferno.

**0.97d: unsure.** `Version097d/` contains only `Items/Jewels.cs` — there is no 0.97d scroll or orb
initializer in this tree, so OpenMU has no 0.97d-specific answer. Numbers 14-18 (Teleport Ally,
Soul Barrier, Decay, Ice Storm, Nova) are widely held to be 0.97d/Season-1 era in the live game, but
this repository only ever states "Season Six" for them. Do not claim 0.97d from this source.

---

## 2. Orbs — group 12, MU 0.75

All four are **Fairy Elf only** (`Version075/Items/Orbs.cs:33-36`, `CharacterClasses.FairyElf`),
`DropsFromMonsters = true` (`:66`), durability 1 (`:67`), level requirement 0 on every one.

Source row: `CreateOrb(number, skillNumber, height, name, dropLevel, levelRequirement, energyRequirement, strengthRequirement, agilityRequirement, money, characterClasses)`
— `Version075/Items/Orbs.cs:55`.

| # | Name | Teaches (skill №) | Req. level | Req. energy | Req. str/agi | Drop level | Price (zen) |
|---|---|---|---|---|---|---|---|
| 8 | Orb of Healing | Heal (26) | 0 | 100 | 0/0 | 8 | 800 |
| 9 | Orb of Greater Defense | Greater Defense (27) | 0 | 100 | 0/0 | 13 | 3,000 |
| 10 | Orb of Greater Damage | Greater Damage (28) | 0 | 100 | 0/0 | 18 | 7,000 |
| 11 | Orb of Summoning | Summon Goblin (30) **+ its level** | 0 | 0 | 0/0 | 3 | 150 |

`Version075/Items/Orbs.cs:33-37`. All four sold by Elf Lala (`Version075/MerchantStores.cs:231-233,
239-243`; store named at `:199`).

**Orb of Summoning is the odd one.** `summonOrb.MaximumItemLevel = 5` (`Orbs.cs:37`), and the
taught skill is `base skill number + item level`
(`GameLogic/PlayerActions/ItemConsumeActions/SummoningOrbConsumeHandlerPlugIn.cs:37-39`):

| Item level | Skill taught | Skill № | Mana | Req. energy |
|---|---|---|---|---|
| +0 | Summon Goblin | 30 | 40 | 30 |
| +1 | Summon Stone Golem | 31 | 70 | 60 |
| +2 | Summon Assassin | 32 | 110 | 90 |
| +3 | Summon Elite Yeti | 33 | 160 | 130 |
| +4 | Summon Dark Knight | 34 | 200 | 170 |
| +5 | Summon Bali | 35 | 250 | 210 |

Skill numbers `Persistence/Initialization/Skills/SkillNumber.cs:43-48`; skill rows
`Version075/SkillsInitializer.cs:68-73`. Elf Lala stocks +0…+4 (`MerchantStores.cs:239-243`); +5
(Bali) exists as a skill and is within `MaximumItemLevel`, but nothing in the 0.75 tables sells it —
it would have to drop or be upgraded. `Bali` even gets a bespoke summoned-monster definition at
`Version075/SkillsInitializer.cs:96-123` (level 52, 5000 HP, 165-170 damage).

Note the orb's **drop level of 3** on a +0 orb: the item is meant to be in a level-3 elf's hands.

### Orbs that are NOT 0.75

| # | Name | Teaches | Arrived in | Where |
|---|---|---|---|---|
| 7 | Orb of Twisting Slash | Twisting Slash | **0.95d** (DK + MG) | `Version095d/Items/Orbs.cs:32` (class inherits 0.75's, `:14`) |
| 12 | Orb of Rageful Blow | Rageful Blow | Season 6 in this tree | `VersionSeasonSix/Items/Orbs.cs:43` |
| 13 | Orb of Impale | Impale | Season 6 | `…/Orbs.cs:44` |
| 14 | Orb of Greater Fortitude | Swell Life | Season 6 | `…/Orbs.cs:45` |
| 16 | Orb of Fire Slash | Fire Slash | Season 6 | `…/Orbs.cs:46` |
| 17 | Orb of Penetration | Penetration | Season 6 | `…/Orbs.cs:47` |
| 18 | Orb of Ice Arrow | Ice Arrow | Season 6 | `…/Orbs.cs:48` |
| 19 | Orb of Death Stab | Death Stab | Season 6 | `…/Orbs.cs:49` |
| 21-24, 35, 48 | "Scroll of …" (Dark Lord) — FireBurst, Summon, Critical Damage, Electric Spark, Fire Scream, Chaotic Diseier | — | Season 6 | `…/Orbs.cs:57-62` |
| 44-47 | "Crystal of …" — Destruction, Multi-Shot, Recovery, Flame Strike | — | Season 6 | `…/Orbs.cs:50-54` |

Careful: numbers 21-24/35/48 are **named "Scroll of …" but live in group 12**, not 15. Number 15 in
group 12 is the Jewel of Chaos (`Core/Globals/_enum.h:2622`), which is why the orb list skips it.

---

## 3. The skills these items teach — 0.75 numbers

From `Version075/SkillsInitializer.cs`. Signature for the argument order is
`Persistence/Initialization/Skills/SkillsInitializerBase.cs:51-71`
(`number, name, characterClasses, damageType, damage, distance, abilityConsumption, manaConsumption,
levelRequirement, energyRequirement, …`). **Ability/AG cost is 0 on every 0.75 wizard and elf skill
below** — `abilityConsumption` is never passed.

### Dark Wizard (scrolls)

| № | Skill | Dmg | Range | Mana | AG | Element | Type | Source |
|---|---|---|---|---|---|---|---|---|
| 1 | Poison | 12 | 6 | 42 | 0 | Poison | direct hit | `:41` |
| 2 | Meteorite | 21 | 6 | 12 | 0 | Earth | direct hit | `:42` |
| 3 | Lightning | 17 | 6 | 15 | 0 | Lightning | direct hit | `:43` |
| 4 | Fire Ball | 8 | 6 | 3 | 0 | Fire | direct hit | `:44` |
| 5 | Flame | 25 | 6 | 50 | 0 | Fire | area, automatic hits, 2-tile target area, 0.5 s between hits | `:45-46` |
| 6 | Teleport | — | 6 | 30 | 0 | — | `SkillType.Other` | `:47` |
| 7 | Ice | 10 | 6 | 38 | 0 | Ice | direct hit | `:48` |
| 8 | Twister | 35 | 6 | 60 | 0 | Wind | area, frustum 1.5→1.5 over 4, deferred hits | `:49-50` |
| 9 | Evil Spirit | 45 | **7** | 90 | 0 | *none* | area, automatic hits, deferred | `:51-52` |
| 10 | Hellfire | 120 | **0** | 160 | 0 | Fire | area, automatic hits, **no area settings given** | `:53` |
| 11 | Power Wave | 14 | 6 | 5 | 0 | *none* | direct hit | `:54` |
| 12 | Aqua Beam | 80 | 6 | 140 | 0 | Water | area, frustum 1.5→1.5 over 8 | `:55-56` |

*(Energy Ball, skill 17, damage 3 / range 6 / mana 1 — `:57` — is the wizard's starting skill and has
no scroll. It is not in group 15.)*

The skills' own **energy requirements** match the scrolls' exactly (Poison 140, Meteorite 104,
Lightning 72, Fire Ball 40, Flame 160, Teleport 88, Ice 120, Twister 180, Evil Spirit 220, Hellfire
260, Power Wave 56, Aqua Beam 345) — compare `SkillsInitializer.cs:41-55` against `Scrolls.cs:33-44`.
**So the scroll's requirement is the skill's requirement.** One number, two places.

Elemental modifiers turn into a resistance target and, for Ice/Poison, a status effect:
`SkillsInitializerBase.ApplyElementalModifier` — Ice → `IsIced` for 10 s, Poison → `IsPoisoned` for
20 s on skill 1 specifically ("Poison applies damage 7 times… every 3 seconds"), Lightning/Fire/Water/
Wind/Earth are resistance targets only.

### In plain English

- **Poison** — a green bolt that sticks; damage now, then a poison tick for ~20 s.
- **Meteorite** — a rock drops on one target. Cheap for its damage (12 mana for 21); the wizard's
  workhorse in the twenties.
- **Lightning** — a bolt down one line. Middle cost, middle damage.
- **Fire Ball** — the first real attack: 3 mana, 8 damage, range 6.
- **Flame** — a burst on the ground that hits everything in a 2-tile circle repeatedly, every 0.5 s.
- **Teleport** — no damage; the wizard steps to a spot within 6 tiles.
- **Ice** — one target, damage and frozen for 10 s.
- **Twister** — a cone (a frustum 1.5 wide, 4 long) that hits everything in it, hits landing after
  a 0.3 s per-tile delay.
- **Evil Spirit** — the long one, range 7, no element, hits a group; the level-50 workhorse.
- **Hellfire** — 120 damage, range 0: it goes off around the caster. 160 mana.
- **Power Wave** — a wide cheap push, 5 mana, no element; the early-level filler.
- **Aqua Beam** — a long beam (frustum 8 deep), 80 damage, 140 mana; the level-74 endgame nuke.

### Fairy Elf (orbs)

| № | Skill | Range | Mana | Req. energy | What it does | Source |
|---|---|---|---|---|---|---|
| 26 | Heal | 6 | 20 | 52 | Restores `5 + energy/5` health to a player. No duration. | `:65`; `Skills/HealEffectInitializer.cs:42`, `:47` |
| 27 | Greater Defense | 6 | 30 | 72 | **60 s** buff, `+2 + energy/8` defense on a player. | `:66`; `GreaterDefenseEffectInitializer.cs:38`, `:45`, `:51` |
| 28 | Greater Damage | 6 | 40 | 92 | **60 s** buff, `+3 + energy/7` damage on a player. | `:67`; `GreaterDamageEffectInitializer.cs:38`, `:45`, `:51` |
| 30-35 | Summon … | — | 40/70/110/160/200/250 | 30/60/90/130/170/210 | Calls a monster that fights for you. | `:68-73` |

Note the mismatch worth copying carefully: **the orbs ask 100 energy, the skills ask 52/72/92.** The
orb's requirement is not the skill's here (unlike scrolls). `Orbs.cs:33-35` vs
`SkillsInitializer.cs:65-67`.

---

## 4. Requirements are **raw** on these items — a trap

`MU2_BGFX/src/content/tables.h:91-93` says "The requirement is carried RAW. What the game asks is
MU's formula over it — `(3 × drop level × raw / 100) + 20`, four for energy", and
`MU2_BGFX/src/sim/items.cpp:53-65` (`asks()`) applies that formula to every row unconditionally.

**For scrolls and orbs OpenMU does not apply it.** `GameLogic/ItemExtensions.cs:313-318`:

```
if (RequirementAttributeMapping.TryGetValue(requirement.Attribute, out var totalAttribute))
{
    if (!item.IsWearable())
    {
        return (totalAttribute, requirement.MinimumValue);   // <- raw, no formula
    }
    ...
```

and `IsWearable` is `item.Definition?.ItemSlot != null` (`DataModel/ItemExtensions.cs:101`). Neither
`Scrolls.cs` nor `Orbs.cs` ever sets `ItemSlot`, so scrolls and orbs are **not wearable** and their
requirement is used verbatim.

Sanity check: Scroll of Poison, raw 140, drop level 30 → the formula would give
`4 × 30 × 140 / 100 + 20 = 188`; the game asks **140**. The table figure is the answer.

There is a second, quieter difference. Scrolls register their requirement against
`Stats.TotalEnergyRequirementValue` (`Scrolls.cs:73`) — the *scalable* attribute — while orbs
register against `Stats.TotalEnergy` directly (`Orbs.cs:70`). Because neither is wearable the two
paths land on the same number, but only the scroll's would ever be scaled if the item became
wearable. Treat both as flat.

**Action for the cook:** `teachesEnergy` / `needEnergy` on a group-12 or group-15 row must reach the
tooltip and the gate **unscaled**. `sim::asks()` currently scales it.

---

## 5. How a scroll or orb is used, and by whom

- Double-click in the inventory → `ItemConsumeAction`. When an item has a `Skill` and is not
  wearable, the handler picked is the `AllScrolls` one — `GameLogic/PlayerActions/ItemConsumeActions/ItemConsumeAction.cs:37-40`
  — and `ItemConstants.AllScrolls` is `new(null, 15)`, i.e. *any number in group 15*
  (`GameLogic/ItemConstants.cs:165`). Group-12 orbs reach the same handler through that
  `Definition.Skill is { } && !item.IsWearable()` fallback; only the Orb of Summoning has its own
  key, `ItemConstants.SummonOrb => new(11, 12)` (`GameLogic/ItemConstants.cs:15`).
- `LearnablesConsumeHandlerPlugIn.ConsumeItemAsync` (`…/LearnablesConsumeHandlerPlugIn.cs:22-38`):
  fails if the skill is **already known**, checks `player.CompliesRequirements(item)`
  (`:41-45`), then consumes the item and adds a **permanent learned skill**.
- Separately, an item with a skill that *is* wearable grants its skill only while equipped and
  loses it when unequipped — `GameLogic/SkillList.cs:215-231`. That is the weapon path
  (`ItemRow::skill`), not this one.

Which classes: scrolls, all twelve, Dark Wizard only (`Scrolls.cs:70`); orbs, all four, Fairy Elf
only (`Orbs.cs:33-36`). In 0.75 those are two of the three classes; the Dark Knight has no skill item
at all until Orb of Twisting Slash in 0.95d.

---

## 6. What MU's own tooltip shows — the group 12/15 sub-case

The general machine is already written up in `MU2_BGFX/docs/mu-tooltip-lines.md`; this section only
says which of its lines a scroll or an orb actually takes, in order. All of it is `RenderItemInfo`,
`Engine/Object/ZzzInventory.cpp`, with the shared frame at ZI:2132-2164 and the renderer at ZI:312.

A **scroll**, top to bottom:

| # | Line | Colour | Where |
|---|---|---|---|
| 1 | blank (every tooltip opens with one) | – | ZI:2164 |
| 2 | `Purchase Price: 350,000` GT 62 / `Selling Price: …` GT 63 — only with an NPC shop open | the item-name colour, then a blank | ZI:2250-2266 |
| 3 | **`Scroll of Meteorite`**, bold. `+N` is appended only when level > 0, which a scroll never has. | WHITE (rule 8 of the name table — a scroll has no option, no excellent, no level) | ZI:2322-2673, name assembly ZI:2645-2671 |
| 4 | blank | – | ZI:2674 |
| 5 | **`Wizardry Damage: 21 ~ 31`** — the scroll-specific line. Looks the taught skill up with `GetSkillByBook(Type)` and prints `skill.Damage ~ skill.Damage + skill.Damage/2`. | WHITE (no harmony, no excellent on a scroll) | ZI:3974-3989; `GetSkillByBook` = `Engine/Object/ZzzInfomation.cpp:418-455` |
| 6 | `Durability: [1/1]` GT 71 — **unsure**. The `bDurExist` test at ZI:4572-4578 fails for both a scroll (`ITEM_ETC` is above `ITEM_POTION`) and an orb (`ITEM_WING+8..11` is past `ITEM_WINGS_OF_DARKNESS` = `ITEM_WING+6` and below `ITEM_HELPER`), but the outer gate at ZI:4580 is `(bDurExist \|\| ip->Durability)`, and both items carry durability 1. Whether a line is actually emitted depends on the inner branch; not traced. Treat as "probably not shown, verify before copying." | WHITE (ZI:4772) | ZI:4572-4580, line at ZI:4754 |
| 7 | `Energy Requirement: 104` GT 77. **RED plus a second RED line `(lacking 32)` GT 74 when unmet**, WHITE when met. | WHITE / RED ×2 | ZI:4959-4981 |
| 8 | blank, then `Can be equipped by Dark Wizard` GT 61. Scrolls are **not** in `IsRequireClassRenderItem`'s skip list (ZI:557-625 lists only `ITEM_HELPER+…`, i.e. group **13**, and specific potions), so the line is printed. | WHITE when your class qualifies; otherwise `TEXT_COLOR_DARKRED` — white text on a dark-red band | `RequireClass` ZI:627-802, called ZI:5008-5011; colour ZI:658-665 |

Three scrolls skip line 5 entirely — Teleport, Teleport Ally and Soul Barrier are excluded by name
at ZI:3974. In 0.75 that means **Scroll of Teleport prints no damage line**.

(Do not be misled by ZI:3970, `if (ip->Type >> 4 == 15)`. With `MAX_ITEM_INDEX = 512` a group test
would be `>> 9`; `>> 4 == 15` matches raw types 240-255, i.e. sword indices, so that branch is a
porting bug and scrolls never take it. The live scroll path is the `ITEM_ETC` test at ZI:3976.)

There is no `Minimum Level Requirement` line on a 0.75 scroll, because every one of them has level
requirement 0 (`Scrolls.cs:33-44`, fifth argument) and the line is gated on `ip->RequireLevel`
(ZI:4820-4836). No strength, agility or stamina line either. No skill/luck/option block: those come
off `Special[]`, which a scroll has nothing in.

An **orb** is the same stack minus line 5: `GetSkillByBook` only knows group-15 types
(`ZzzInfomation.cpp:422-451` — every case is an `ITEM_ETC + n`), and an orb's own `DamageMin` is 0,
so the damage branch falls to `TextNum--` and prints nothing (ZI:3998-4000). An orb therefore shows:
blank, price, **name**, blank, `Energy Requirement: 100`, blank, `Can be equipped by Elf`. The Orb of
Summoning has energy requirement 0, so it shows name and class line only.

Two orb-only details:

- **The Orb of Summoning's name is built, not looked up.** ZI:2486 prints `"%ls %ls"` =
  `SkillAttribute[30 + Level].Name` + `I18N::Game::Jewel`, so a +1 orb reads *"Summon Stone Golem
  Jewel"* rather than "Orb of Summoning +1". That is the client telling the player which monster the
  orb will teach — worth copying, because our `teaches = 30 + level` rule has to show up somewhere.
- **An orb carries its skill in `ip->Special[]`.** When `SpecialNum > 0` the tooltip adds a blank
  (ZI:5056-5069) and then one line per entry (ZI:5157-5199): `gSkillManager.GetSkillInformation`
  fetches the mana (ZI:5163), `GetSpecialOptionText` (ZI:1889-2112) formats it, e.g.
  `Twisting Slash Skill (Mana:22)` — **`TEXT_COLOR_BLUE` (128,179,255), not bold** (ZI:5166).
  Class notes `Knight specific skill` (ZI:5177) and `Dark Lord exclusive skill` (ZI:5185) are
  `TEXT_COLOR_DARKRED`, i.e. white on a red band. Note this is the *equipped-weapon* skill mechanism
  reused; whether a 0.75 orb actually populates `Special[]` is **unsure** — OpenMU's orbs teach on
  consume and have no `ItemOption`, so in a faithful 0.75 build this block is probably empty.

Colour values (the tooltip resolves them in `RenderTipTextList`, ZI:383-467): WHITE 255,255,255;
BLUE 128,179,255 (ZI:413); RED 255,51,26 (ZI:422); YELLOW 255,204,26 (ZI:425); DARKRED = white glyph
on a 160,0,0 background (ZI:445). Box: 1 px black frame (ZI:375-379) over black at 0.8 alpha
(ZI:381). Bold uses `g_hFontBold` (ZI:387-389) and **only the name line sets it** (ZI:2673).

Exact English strings, from `LEGACY/reference/MuMain/src/Localization/Game.en.resx`:

| GT | String | resx line |
|---|---|---|
| 42 | `Wizardry Damage` | 167-169 |
| 61 | `Can be equipped by %s` | 243-245 |
| 62 | `Purchase Price: %s` | 247 |
| 74 | `(lacking %d)` | 295-297 |
| 76 | `Minimum Level Requirement: %d` | 303-305 |
| 77 | `Energy Requirement: %d` | 307-309 |

**MU says nothing about what the skill does.** No skill name line, no description, no mana cost,
no range. The whole tooltip is name + wizardry damage + energy + class. The player is expected to
know. `MU2_BGFX/src/game/ui/describe.cpp:166-184` already notes this and adds a "Teaches" section of
its own (`teachesName`, `teachesTells` — `src/content/tables.h:112-117`); that section is MU2's
invention and should stay marked as such, per the absolute-replica rule.

---

## 7. `item_eng.bmd` — decoded, and it disagrees with OpenMU on two orb names

`LEGACY/reference/MuMain/src/bin/Data/Local/Eng/item_eng.bmd` decodes with the plain three-byte MU
XOR key `0xFC, 0xCF, 0xAB` — `byte ^ key[i % 3]`, no offset subtraction, no 16-byte table.
688,132 bytes = 8192 records × 84 bytes (+4 trailing); record index is `group * 512 + number`, the
name is a NUL-terminated ASCII string at record offset 0. `skill_eng.bmd` is the same key, 108-byte
stride, name at offset 0 (1 = `Poison`, 41 = `Twisting Slash`, …). `strings -a` alone yields nothing.

**Group 15, the client's own names** — 1:1 with OpenMU, including the `Lighting` typo:

```
 0 Scroll of Poison      1 Scroll of Meteorite   2 Scroll of Lighting
 3 Scroll of Fire Ball   4 Scroll of Flame       5 Scroll of Teleport
 6 Scroll of Ice         7 Scroll of Twister     8 Scroll of Evil Spirit
 9 Scroll of Hellfire   10 Scroll of Power Wave 11 Scroll of Aqua Beam
```
(12-36 are the later scrolls and parchments listed in §1; they match OpenMU's Season-6 names exactly.)

**Group 12, the client's own names** — note the two disagreements:

```
 0 Wings of Elf          1 Wings of Heaven       2 Wings of Satan
 3 Wings of Spirits      4 Wings of Soul         5 Wings of Dragon
 6 Wings of Darkness     7 Orb of Twisting Slash
 8 Healing Orb           9 Orb of Greater Fortitude  10 Orb of Greater Damage
11 Orb of Summoning     12 Orb of Rageful Blow  13 Orb of Impale
14 Orb of Greater Fortitude  15 Jewel of Chaos  16 Orb of Fire Slash
17 Orb of Penetration   18 Orb of Ice Arrow     19 Orb of Death Stab
```

1. **12:8 is `Healing Orb` in the data file, `Orb of Healing` in OpenMU.**
2. **12:9 decodes as `Orb of Greater Fortitude`, the same string as 12:14** — but the enum calls it
   `ITEM_ORB_OF_GREATER_DEFENSE` (`Core/Globals/_enum.h:2616`) and OpenMU calls it
   `Orb of Greater Defense` (`Version075/Items/Orbs.cs:34`), and the skill it teaches really is
   Greater Defense. This looks like a genuine collision in this Season-6 data file, not a decode
   error. **Do not copy 12:9's name from the .bmd.**

Recommendation: take 12:9 and 12:10 from OpenMU (`Orb of Greater Defense`, `Orb of Greater Damage`)
and decide 12:8 by taste — `Healing Orb` is what the shipped client showed, `Orb of Healing` is what
every wiki and OpenMU say. Everything in group 15 can be taken from either source.

`docs/mu-tooltip-lines.md:259` already records that the tooltip is built in code from `item_eng.bmd`
names plus the string table, and that `ItemTooltip_eng.bmd` / `ItemTooltipText_eng.bmd` /
`ItemLevelTooltip_eng.bmd` are **not read** by this source tree (they decode with the same key if
ever needed). `NpcName_Eng.txt` beside them is plain text.

---

## 8. Summary of what to cook

For each row, group and number as in §1 and §2, then:
`dropLevel`, `needLevel` (0 on all 0.75 scrolls and orbs), `needEnergy` **raw, unscaled**,
`classes` (DW for scrolls, Elf for orbs), `durability = 1`, `width = 1`,
`height = 2` for a scroll and `1` for an orb, `value` = the price column,
`teaches` = the skill number, `teachesEnergy` = the *skill's* energy requirement (equal to the
scroll's, different from the orb's), and — MU2's own additions — `teachesName` and `teachesTells`
from §3.

Three things to get right that the current code would get wrong:

1. `sim::asks()` (`src/sim/items.cpp:53-65`) scales `needEnergy` by drop level. It must not, for
   group 12 and group 15 — see §4.
2. The Orb of Summoning's taught skill is `30 + item level`, not a constant, and MU's own tooltip
   names the *summoned monster* in the item's name line rather than showing "+1".
3. Group 12 is shared with wings (0-6) and the Jewel of Chaos (15). A cook that treats "group 12"
   as "orb" will mis-file all four 0.75 wings and the jewel.

And one name to decide by hand: 12:8, `Healing Orb` (client data) vs `Orb of Healing` (OpenMU) — §7.
