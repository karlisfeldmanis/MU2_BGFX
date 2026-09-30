# Jewels: how they drop and how they refine

Written 2026-09-25. MU2's Godot build worked the whole of this out once, in
`MU2/docs/refining.md`, and that page is still the fuller account: the luck, the wire, the shine
ladder and the bench. This page is what this tree takes from it, re-read against OpenMU and
MuMain, and what it leaves for later.

## How a jewel drops

MuMain has no say in it. The client is handed an item on the ground and plays its sound; what
the item is was the server's decision, and the server's rule is OpenMU's `DefaultDropGenerator`
over the groups `GameConfigurationInitializerBase` registers on every map:

| group | chance a drop slot | what it holds |
|---|---|---|
| jewels | 0.001 | the Bless (drop level 25), the Soul (30), the Chaos (12 to 66), the Ale (15), the Town Portal Scroll (30), the three pets |
| random item | 0.3 | anything `DropsFromMonsters` from the monster's level down to twelve below it |
| money | 0.5 | the kill's experience plus seven |

The jewels themselves are `DropsFromMonsters = false`, so the random-item group never gives one;
the 0.001 group is their only way to the ground. Inside it the draw is uniform over what the
monster's level reaches (`CanDropAtMonsterLevel`: at or above the drop level, at or under the
maximum), with no twelve-level gap — `GenerateItemFromGroup` waives it for `isJewel`. A group
whose pool comes out empty leaves nothing for that slot.

**In Lorencia that means the Chaos from level 12 and the Ale from 15, and nothing else.** No
monster in the town is level 25, so the Bless and the Soul are never left there. That is 0.75 as
OpenMU has it and it is kept: a Bless or a Soul in this tree comes from `--give` until a map
that drops them exists.

`Realm::leave` (`src/sim/realm_items.cpp`) rolls it this way already. The jewels group was the
three jewel rows alone until today; the Ale and the Town Portal Scroll are in it now, as
`AddItemToJewelItemDrop` puts them. The pets have no rows here. The item chance is 0.1 here, not
0.3, which is ours (2026-09-22) and said where it is set.

When one lands, MuMain's `CreateItemDrop` plays `SOUND_JEWEL01` (eGem.wav) for the jewels and
`SOUND_DROP_ITEM01` for the rest. That is `Play::landed`'s branch on `ItemRow::jewel()`.

**Its label is gold and bold.** `BuildGroundItemLabelDescriptor` puts every jewel in
`boldTextItems` and `yellowTextItems` (`ZzzInventory.cpp:6077, 6101`), beside Zen, and the level
ladder after it skips anything already coloured. So a jewel reads `(1.0, 0.8, 0.1)` in
`g_hFontBold` on the black plate, where an Ale or a scroll at +0 is the ladder's gray. Here that
is `Desk::labelGround`: the yellow rung, and a point up for the bold, as a tip's bold line is.
It lies as modelled, standing: `ItemAngle` gives a jewel only the default −45°.

**And it cannot be thrown away.** Both of MuMain's drop paths refuse `IsHighValueItem`
(`ZzzInventory.cpp:7407`) with "You are not allowed to drop this expensive item": the jewels,
anything below the wings at +7 or more, wings, excellent and ancient. `sim::expensive` is the
rows of that list this tree has, and `Realm::discard` refuses it. OpenMU's server would take the
drop; the client never sends it. There is no system-message lane here yet, so the refusal is
heard (`iButtonError`) and not read.

To look at one: `--lay 200:Jewel01,Jewel02,Jewel15` lays them beside the hero on frame 200 as a
kill's drop lies — the fall, the ring and the label. It is the bench's, because nothing in
Lorencia leaves a Bless or a Soul.

## How a jewel refines

OpenMU's `UpgradeItemLevelJewelConsumeHandlerPlugIn.ModifyItem`, configured by its Bless and
Soul handlers:

| | Jewel of Bless (14, 13) | Jewel of Soul (14, 14) |
|---|---|---|
| goes on a thing at | +0 … +5 | +0 … +8 |
| chance | 100% | 50%, +25% on a lucky thing |
| on success | +1, made whole at the new plus | +1, made whole at the new plus |
| on failure | — | −1; from +7, back to +0 |
| the jewel | spent | spent, success or not |

What takes one is `CanLevelBeUpgraded`, which MuMain's `CanUpgradeItem` paints by: the weapon
and armour groups up to the boots, less the arrows and the bolts. The Chaos goes on nothing; it
is the machine's. And nothing is carried past `kRefineCap`, +9, which the Soul stops at on its
own; it becomes eleven when the Chaos Machine's +10 and +11 are built.

The target may be worn. OpenMU refuses that and says in its own comment that the original
server allowed it, and the original is what is being built. The jewel must be in the bag.

## Where it lives

- `sim::refinable` and `sim::jewelOf` (`src/sim/items.h`): the one gate, asked by the realm's
  refusal and by the bag's colour, so the two cannot disagree.
- `Realm::refine` (`src/sim/realm_items.cpp`): the roll, the jewel spent first and the thing put
  back second, `rearm` for a worn one, and `What::Refined` (slot, the plus before, the plus
  after). A Bless draws nothing from the dice, so a seeded run without a Soul in it is the run
  it was.
- The bag (`src/game/ui/bag.cpp`): a jewel let go over a thing it goes on is a refine request and
  never a move — MuMain's `HandlePickedItemPlacement` tries `ApplyJewels` before the move — and
  while it is dragged there the whole thing under it lights blue, not the jewel's own cell.
- `Play::refine` (`src/game/play_requests.cpp`): `SOUND_GET_ITEM01` on the asking, which is how
  `ApplyJewels` ends, then eGem where the hero stands for the answer, up or down alike, as
  `ReceiveModifyItemExtended` rings it. A worn thing re-dresses the figure at its new rung.

To try it: `./main.sh --new --give Sword01,Jewel01:3,Jewel02:3`, open the bag, and drag a jewel onto
the Kris in the bag or the Small Axe in his hand (a level 1 knight is 7 agility short of
wearing the Kris). A played run writes the real save on the way out,
jewels and all; add `--save /tmp/jewels.json` to keep them out of it.

## Luck and the additional option

`Held::luck` and `Held::option`, rolled on every dropped weapon, piece of armour and shield
(`sim::takesOptions`: the refinable set, arrows and bolts out). OpenMU's `ApplyRandomOptions`
over 0.75's two option definitions, in the order the initialisers add them:

| | chance | what it does worn | tooltip (MuMain, blue) |
|---|---|---|---|
| luck | 4% (WebZen) | +5% critical chance each (`Arms::criticalChance`); +25% on a Soul | `Luck (success rate of Jewel of Soul +25%)`, `Luck (critical damage rate +5%)` |
| option | 25%, level 1 to 3 evenly | weapon: both ends of the band +4 a level, worn down with it; armour: defence +4 a level; shield: defence rate +5 a level; staff: wizardry, not reckoned yet | `Additional Dmg / Wizardry Dmg / defense / defense rate +N` |

A critical is the top of the roll, and `strike` only draws for one when the chance is above
nought, so a run with nothing lucky worn rolls what it rolled before. No skill is rolled: skills
are orbs here (the user, 2026-09-27). The name goes blue, the ground label goes blue and says
`+Option` and `+Luck` after the plus as `BuildGroundItemLabelDescriptor` appends them, and the
price takes a quarter for luck and 60%, then 0.7 x 2^(level-1), for the option
(`ItemPriceCalculator`). The save carries both.

## Excellent

**Not 0.75** — OpenMU adds it in 0.95d — and taken on the user's word (2026-09-27).
`Held::excellent` is MuMain's `ExcellentFlags & 63`, a bit per option, OpenMU's option number
less one:

| bit | weapon (a staff's reads Wizardry Dmg) | armour and shield |
|---|---|---|
| 0 | mana after a kill +mana/8 | Zen after hunting +40% |
| 1 | life after a kill +life/8 | defence success rate +10% |
| 2 | attack speed +7 | reflect damage +5% |
| 3 | damage +2% | damage decrease +4% |
| 4 | damage +level/20 | max mana +4% |
| 5 | excellent damage rate +10% | max HP +4% |

The Zen line is written +40%, OpenMU's `MoneyAmountRate` 1.4, where MuMain's GT 627 says
+30%: the line says what the rule does.

**The drop** is its own group at 0.0001 a slot, rolled after the jewels: nothing from a monster
under level 25, otherwise what a monster 25 levels lower would drop, at +0, with luck and the
option rolled as on any drop, one excellent option always and a second at 0.001. So nothing in
Lorencia leaves one either; `--lay` and `--give` make them (`E` and the option numbers:
`Sword01:E36`). OpenMU's list admits rows with no excellent options, which would make an
"excellent" potion; only rows that can carry them are drawn here.

**The look** is `RenderPartObjectEffect`'s excellent pass (`ZzzObject.cpp:10492`): the item
drawn again, added, with Chrome02 at `u = N·L`, `v = 1 - N·L` off ZzzBMD's fixed light and tinted
`(L, 0.3L, 1 - L)`, `L = sin(WorldTime × 0.002) × 0.5 + 0.5` — the whole piece breathing from
red-orange through violet to blue in about three seconds, on the body, in the hand, on the ground
and in the bag. `shaders/shine.sh`'s `shineExcellentAdded`, flagged to the shader as the plus
+ 20; Chrome02 is `chrome2` in the showing, on stage 11.

**The rest:** the name reads `Excellent Kris` in green, the damage and defence lines go blue,
each option is a blue line after luck's and the option's, the ground label is green
`(0.1, 1.0, 0.5)`, the price is 25 drop levels deeper and doubles per option, and an excellent
thing cannot be thrown away (`IsHighValueItem`). The save carries the mask.

**What being excellent does**, whatever its options (`ItemPowerUpFactory`, which MuMain's
`CalcDamageMin`, `CalcDefense` and `CalcSuccessfulBlocking` agree with), all off the row's drop
level: a weapon's band + min x 25 / drop level + 5 at both ends (`sim::excellentDamage`);
armour + defence x 12 / drop level + drop level / 5 + 4 (`excellentDefense`, not a shield); a
shield's block rate + rate x 25 / drop level + 5 (`excellentBlock`); the requirements reckoned
25 drop levels deeper (`asks`); fifteen more durability (`maximumDurability(row, held)`). The
tooltip prints the band, the defence and the block with it, in blue.

**What the options do** (`sim::Excellence`, summed off the worn slots in `Realm::rearm`, the
multiplying ones per piece): the weapon's 1 and 2 give an eighth of the pool back after a kill
(`Realm::kill`); 3 adds 7 to the attack speed (`swingMilliseconds`); 4 and 5 are x1.02 and
+level/20 on the damage (in `reckon`, the level first -- the order is ours); 6 is a tenth of
excellent hits, 1.2 x the top of the band, drawn after the critical and winning over it, as
`AttackableExtensions` has them (`strike`). A staff's 4 and 5 are wizardry, not reckoned. The
armour's 1 is x1.4 on Zen at the pick-up (`Realm::take`); 2 x1.1 on the defence rate; 3 sends a
twentieth of what reached him back at the attacker (`strikeAt`, truncated, so a blow under 20 on
one piece reflects nothing); 4 takes 4% off after the overrate and before the floor; 5 and 6 are
x1.04 on max mana and max life.

**The numbers** take MuMain's two colours (`WSclient.cpp:3317-3347`), asked for by the user:
an excellent hit is `DT_EXCELLENT` green `(0, 1, 0.6)` at the critical's size, and a reflect
`DT_MIRROR` magenta `(1, 0, 1)` -- `Mark::Excellent`, `Mark::Reflected` in game/ui/tally.cpp. A
reflect is shown as a cue of its own and never starts a swing. The critical keeps the gold the
user chose on 2026-09-23 over MuMain's blue.

## Not built

- **Stacks of jewels.** A jewel is one cell. `Realm::refine` spends one from a stack if it is
  ever given one, but nothing merges them.
- **The Chaos Machine**, and with it +10, +11 and the Chaos's only use.
- **The Jewel of Life.** 0.95d, and it wants item options first.

## Sources

- `LEGACY/reference/openmu/src/GameLogic/DefaultDropGenerator.cs` — `GenerateItemFromGroup`,
  `CanDropAtMonsterLevel`, `SelectRandomGroup`.
- `.../Persistence/Initialization/GameConfigurationInitializerBase.cs:176-211` — the groups.
- `.../Persistence/Initialization/Version075/Items/Jewels.cs`, `Potions.cs:42-58, 225-236`,
  `Pets.cs:30-37`, `InitializerBase.cs:204-209` — what is in the jewels group.
- `.../GameLogic/PlayerActions/ItemConsumeActions/UpgradeItemLevelJewelConsumeHandlerPlugIn.cs`,
  `BlessJewelConsumeHandlerPlugIn.cs`, `SoulJewelConsumeHandlerPlugIn.cs`.
- `LEGACY/reference/MuMain/src/source/UI/NewUI/Inventory/NewUIInventoryActionController.cpp:480-604`
  — `ApplyJewels`; `NewUIInventoryCtrl.cpp:1801-1826` — `CanUpgradeItem`.
