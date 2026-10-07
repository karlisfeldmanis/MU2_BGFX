# Magic Gladiator port: the record and the plan

Started 2026-10-06. The user: "we need to migrate also MG, we need to decide which runes he can
use". Nothing is built yet. This page holds what is decided, what MU says, and the steps.

Sources: OpenMU `ClassMagicGladiator.cs` and Version095d (the first version with MG; 0.75 has
none, so every MG rule is past 0.75 and is read from 0.95d, then WebZen 1.00.93 for 0.97d);
VersionSeasonSix for the 2nd wing; MuMain for the drawing.

## Decided

**Runes (the user, 2026-10-06: "dont make specific runes for MG, but allow him to use same weaker
runes from DW and DK").** MG gets no rune of his own. He may set:
- every `kEveryClass` rune, as everyone (Evil Spirit and Wisp go only into jewellery, since he
  carries no shield);
- the knight's and the wizard's **Common and Epic** runes: Cinder, Gust, Faint Echo (Common);
  Meteor, Ice, Poison, Immolate, Scorch (Epic);
- **no Legendary** of either class: not Hellfire, Twister, Whirlwind, Bulwark, Fireburst, Ring
  of Fire, Arcane Echo or Pyroblaster.

As code: a `kGladiatorOnly` bit in `kEveryClass`, and that bit added to the eight rows above in
`powerOf` (sim/items.cpp). `drawRunePower` then drops him only these, with no further change.
The `second` flag does not reach him, since none of his runes carry it.

**No 2nd class quest (asked 2026-10-06).** In MU the Magic Gladiator never changes class. Sevina's
quest is not his (docs/class-change-quest.md). He wears the 1st wings of both the wizard and the
knight, the Wings of Heaven and the Wings of Satan, but not the Wings of Elf (OpenMU 0.95d
Wings.cs:108-109 gives class level 1 of either to MG; the user remembers the same, 2026-10-06) and, from 0.97, the
**Wings of Darkness** with no quest (VersionSeasonSix Wings.cs:83, MG's class level 1). Every rule
here that asks "is he second class?" (2nd wings, the second-class runes, second-class gear) must
answer for MG by its own row, never via `isSecond`.

## What MU says

**Creation (ClassMagicGladiator.cs):** unlocked when the account holds a level 220 hero
(`LevelRequirementByCreation = 220`, UnlockMagicGladiatorAtLevel220.cs). Born at level 1 with 26 /
26 / 26 / 26, 7 points a level (the others get 5), home Lorencia. Base health 57 (+1 a level,
+2 a vitality point), base mana 7 (+1 a level, +2 an energy point).

**Damage:** physical from strength (min str/6, max str/4) **and** energy (min ene/12, max
ene/8). Wizardry is the wizard's band (ene/9 to ene/4). Defence is agi/5, and attack speed
agi/15 (swing) and agi/20 (magic).

**Skills (Version095d SkillsInitializer.cs; the user, 2026-10-06: "most of the spells but not
soul master spells, ... only DK defense skill not DW soul barrier"):**
- the wizard's spells: Energy Ball, Fire Ball, Power Wave, Lightning, Meteorite, Ice, Poison,
  Flame, Twister, Evil Spirit, Hellfire, Aqua Beam, Cometfall, Inferno (:41-60);
- **not** Teleport (:47 is `DarkWizard` alone) and **not** Soul Barrier, the wizard's own guard
  (`kSoulBarrier`; Season 6 keeps it the wizard's). Any spell we later lock to the Soul Master
  stays closed to him too;
- the knight's Falling Slash, Lunge, Uppercut, Cyclone, Slash, Twisting Slash's orb and Fire
  Breath, and **Defense**, his only guard (:61-80, Orbs.cs:32). Fire Breath is built here as the
  Horn of Dinorant's, known while it is ridden (`skill::kFireBreath`), but only by the row's own class
  (`Realm::knows`, realm_skills.cpp:79), so that gate has to let MG in.
  **Impale** (0.95d :79, the spear's, level 28) is not built in this game, for any class;
- not Rageful Blow or Death Stab, which are Season skills that stay the knight's here.
Power Slash (56) and Spiral Slash (57) are Season skills and are not in 0.95d.

**Gear:** the weapon rows mark him on both sides (Weapons.cs:301-304): any wizard staff or knight
weapon, plus his own blades. In MU he wears no helm; his Storm Crow set has none. Storm Crow was
skipped for the knight (the user, 2026-10-06), so MG has no set of his own here yet.

**The 2nd wing:** Wings of Darkness, MU's 12/6, 4x2, defence 40, level 215, options wizardry or
damage (`0b00` / `0b10`), the 2nd wings' damage table. The model is MuMain's `MODEL_WING + 6`,
`Data/Item/Wing07.bmd` (our `Wing06` is the Dragon, `MODEL_WING + 5`). Ours needs an unused group
12 number: 15 is taken by the Jewel of Chaos.

## Steps

1. **The class:** done 2026-10-07. `Kin::MagicGladiator` = 3 (sim/rules.h, `kKinCount` 4), his
   ClassRow (energy/12 to /8 added to the physical band, defence agility/5, 57 health), 26 of
   each, `pointsPerLevel` 7, mana 7 + level + 2 energy (110 health and 60 mana at level 1,
   OpenMU's own), the wizard's band, the knight's swing speed, half the wizard's magic speed,
   double wield, every element for his runes, `moveLevel` (two thirds of a gate or travel), and
   Satan / Darkness from the wing mixes. sim_test checks his numbers.
2. **Creation:** done 2026-10-07, open from the start for now, so he is easy to test (the user,
   2026-10-07); MU's 220 gate (the lobby offers MG only when a saved hero has reached 220) comes
   later. The lobby's fourth class button, MU's text 1708, his bust (NewFace04 from Data/Logo,
   CLASS_DARK's -13 / 6.0 / lift 1.8, CharMakeWin.cpp:107) and his bare body on the pedestal and
   in the world (`--class 3`, `--lobby-create 3`). He is born in Lorencia with empty hands.
3. **Skills and gear:** the skill list above (no Teleport, no Soul Barrier); the item class
   masks gain him, Heaven and Satan wings included.
4. **The figure:** done 2026-10-07 for the bare body (the user: 'we need to build MG naked
   skin character default lvl 1'): `source/players/MagicGladiatorBare.json` on HelmClass04,
   ArmorClass04, PantClass04, GloveClass04 and BootClass04 (MODEL_BODY_* + SkinIndex, class 3),
   all on skin_special_01 -- red hair, the navy tunic with the gold sun, the high collar.
   Materials: face skin, hair hair, the outfit cloth, hands and boots leather as the knight's.
   The head's 37 nape vertices stay on Spine1, MU's binding (the hair on his back). On the
   bench's People tab as "Magic Gladiator, no armour".
5. **Runes:** the decided rule above, along with `sim_test` rows that MG can set Meteor but not
   Hellfire.
6. **Wings of Darkness:** cooked 2026-10-07 as ours 12/22, class `gladiator` (docs/second-wings.md's
   last section). Still to do once he exists: the 2nd wing mix's answer for him
   (sim/machine.cpp's class switch) and the `isSecond` gate in `sim::fits`, which he must pass
   without a class change. MU's violet chrome pass (game/shine.h kShineDarkness, Chrome02) and
   rib sparks (game/wings.cpp) are in, and its second action on a safe tile.
7. **His set**, if wanted: Storm Crow (no helm), asked before building.

## Open

- Whether the Chaos Machine's 2nd wing mix stays the only way to get Darkness for him, as it is
  for the others.
