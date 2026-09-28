# Sprint 15: the elf

Opened 2026-09-28, while another session builds Noria. The Fairy Elf can be made and is given
a Short Bow (`game/roster.cpp:205`), and then she punches: the realm reaches one tile for
every hero (`kHeroAttackRange`), reckons her melee band, and has no elf row in the skill
table. This sprint gives her the whole kit: the bow and the crossbow, the quiver she wears,
her skills and her summons. Tested in Lorencia, on its own cooked monsters.

**Proved by:** an elf with a Short Bow and 255 arrows shoots a Spider from six tiles in a seeded
headless log, one arrow spent a shot, the `Hit` a flight after the `Loosed`, and she stops with
"no more arrows" when the quiver and the bag are empty; in the window, the arrow flies from
the bow and the quiver hangs on her back.

## The rules, traced

| rule | source |
|---|---|
| archery band: min Agi/7 + Str/14, max Agi/4 + Str/8, in place of her melee band while a bow or crossbow is held | `ClassFairyElf.cs:78-81`, `ArcheryAttackMode` from `IsBowEquipped`/`IsCrossBowEquipped` (:88-89) |
| ammunition adds no damage in 0.75 | `AmmunitionDamageBonus` is a 0.95d power-up (`Version095d/Items/Weapons.cs:213`), absent from Version075 |
| reach 6 with any bow type, 1.8 bare, measured on the tile grid | MuMain `Action()`, `ZzzInterface.cpp:1245-1261` |
| one shot spends one arrow, hit or miss; bolts for a crossbow, arrows for a bow | `AmmunitionConsumptionRate` 1 on every bow (`Version075/Items/Weapons.cs:329`), OpenMU `ApplyAmmunitionConsumption` |
| an empty hand reloads from the bag, last column first, bottom up; none anywhere refuses | MuMain `CheckArrow`/`SearchArrow` (`FindItemReverseIndex`, `ZzzInterface.cpp:450`) |
| a new elf: Short Bow in the left hand, 255 Arrows in the right | OpenMU `AddShortBowForFairyElf`, `AddArrowsForFairyElf` |
| the arrow flies 17.5 tiles a second and stops a tile short | MU's `Direction[1] = -70` a reference frame at 25; MU2 `Realm.cs:79` |
| attack clip 50 for a bow, 51 for a crossbow | already in `sim/swings.cpp:102-110` |

**Ours:** the damage lands when the arrow arrives, not on the clip (MU2's rule, and this engine's
for spells since sprint 11): the arrow is let go at the bottom of the swing, where `begin`
already puts a blow, and rides the realm's existing `flights_`. An empty quiver refuses the
shot rather than OpenMU's shot that costs nothing.

## The steps

1. **The shot, in the sim.** Archery band, reach 6, ammunition drawn and reloaded, the refusal,
   the arrow let go into `flights_`, the cradle's arrows. A seeded headless log proves it.
2. **The shot, seen.** The `missiles` rows (`Arrow01`, `ArrowSteel01`, `ArrowLaser01`,
   `ArrowSaw01`) drawn with `fx/effect_mesh`, the weapon's own fire per MU2's `fires` field, the
   release off the bow's rail, the ember wake on Arrow01 only, `ebow`/`ecrossbow`.
3. **The quiver worn.** The ammunition hand drawn on her back (`kQuiverOnBack` exists in
   `game/figures.cpp:63`) in the field as well as in town, arrows and bolts both.
4. **Her skills.** Heal, Greater Defense, Greater Damage as rows beside the knight's and the
   wizard's, on QWER with cooldowns (`skills-are-diablo3-shaped`). **The multishot is learned
   from an "Orb of Skillshot"** (the user's, 2026-09-28) and thrown with any bow or crossbow --
   ours, in place of 0.75's Triple Shot, which only a bow carrying the Skill option grants
   (skill 24: 5 mana, reach 6, three arrows at 0 and +/-15 degrees, OpenMU
   `Version075/SkillsInitializer.cs:64`). It spends one arrow a body struck, as MU2 did.
5. **Her summons.** Goblin, Stone Golem, Assassin, Elite Yeti, Dark Knight and Bali: one at a time,
   hers, fighting what she fights. The orbs are named **"Orb of Goblin", "Orb of Golem"** and so on,
   the user's (2026-09-28) -- ours, over 0.75's "Orb of Summoning". **A summon scales with the
   elf** (the user's, 2026-09-28, "so they are useful") -- ours: 0.75 summons the breed's fixed
   row, which a levelled elf outgrows in an hour. **Energy is the build it answers:** an elf who
   puts her points in energy has a summon that does real damage and **holds aggro** -- the
   monsters it fights stay on it rather than turning to her, so she shoots from behind it. The
   curve (energy on the breed's health, damage and defence, with level as a floor) and the
   aggro rule are designed and written here before they are built, and the seeded log shows
   both: a full-energy elf's Stone Golem tanking a pack she kills. Every summoned breed but the
   Goblin lives outside Lorencia and needs its figure cooked; that is priced before it is begun.

## Progress

| step | state | where |
|---|---|---|
| 1. the shot, in the sim | **done** | `Body::archer` set in `rearm`; the band in `rules.cpp` `reckon`; `nock` in `realm_items.cpp` (reload, spend, refuse); `looseArrow` in `realm_fight.cpp`; `kArcherReach` and the flight in `realm_tuning.h`; the cradle's quiver in `equip`. `sim_test` `testArchery`: 255 drawn, 248 let go (7 targets died on the draw, and the arrow is spent at the draw), 94 from past arm's length, every arrow in the air landing a flight later, one "no more arrows" and an empty hand. A level-one elf with the Short Bow reckons 8 to 14 |
| 2. the shot, seen | **done, for the user to judge in game** | `game/fx/arrow.*`: the four models by MuMain's `CreateArrow` switch (every bow the wooden arrow, Crossbow/Golden the steel bolt, Arquebus the saw, Light Crossbow the laser; the unbuilt ones fall back, marked ours), from MU's (-10, -60, 135) muzzle (`Play::shootArrow`), at the realm's 17.5 tiles a second, gone a tile short of the body -- the tick the blow lands. The wooden arrow sheds `BITMAP_FIRE` embers at the meteor's numbers; the others shed none, as `Move_MODEL_ARROW_STEEL` lays none. Ours: aimed at the target's middle and following it, where MU flies level. `ebow`/`ecrossbow` were already on the swing. Bench: `--bolt-skill 0` fires the hero's own arrow. Read in a shot at Lorencia's fountain: shaft head-first, the trail heavy -- MU's own sizes, left to the user |
| 3. the quiver worn | **done** | `Figures::dress` takes the quiver as a third item (`HeldItem::alwaysSlung`, kind `quiver`, `kQuiverOnBack` for both kinds), drawn on `Bone05` in the field as well as in town (`crowd.cpp`), as `RenderCharacterBackItem` sends MODEL_ARROWS and MODEL_BOLT to the back unconditionally (`ZzzCharacter.cpp:15294`). `Play::redress` passes the ammunition hand; a draw that empties or refills it redresses her; `Play::open` redresses once after the cradle arms her. Read in arena shots: Arrows02 behind her shoulder with the Short Bow, Arrows01 with the Crossbow |

**For step 5, from session mu2-bgfx-49's Noria research (2026-09-28):** the Stone Golem is not
cooked; MU bursts it on death into 8 x (MODEL_BIG_STONE1 + BIG_STONE2) with SOUND_BONE2
(`ZzzCharacter.cpp:1489`), and `source/effects/bigstone` exists. Noria's Hunter shoots with
`CreateArrow` too, so `fx/arrow` is what draws its shot when it is built.

**Found on the way, not this sprint's:** `testSwings` fails its two fist checks (1085 ms against
462) on a tree this sprint did not touch there -- the fist clip's cooked speed, left for whoever
owns the cook.
