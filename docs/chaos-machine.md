# The Chaos Machine

Started 2026-10-02. The user: "first we need to migrate basic things how it was in MuMain, and
then we build our own UI and options which machine". This page is phase one, MuMain's machine
as it was; phase two is our own window and our own combinations.

## Phase one: MuMain's machine

**Where.** The Chaos Goblin, NPC 238, in Noria at 180,103 (Version075/Maps/Noria.cs). A Talk
order within `kCounter` opens it, as Baz opens the vault; any other order closes it.

**The box.** `sim::Machine`, 8 x 4 (`CNewUIMixInventory`'s `CNewUIInventoryCtrl`), cells
`row * 8 + column`, the vault's moves: bag to box (drag, or right-click a bag thing), box to bag
(drag out over the bag, or right-click), and inside the box. Closing puts everything back in
the bag; what does not fit stays in the box and is saved with the character (`"machine"` in the
save, absent when empty). After a mix the box is locked to new things until it is emptied (MU
locks it at MIX_FINISHED until reopened).

**What it makes** (`sim/machine.h`, matched in this order):

| combination | box | rate | Zen | success | failure | source |
|---|---|---|---|---|---|---|
| +10 Item | one thing at +9, 1 Chaos, 1 Bless, 1 Soul | 50%, +20 lucky, at most 75% | 2,000,000 | +10 | thing and jewels gone | WebZen `PlusItemLevelChaosMix` (base 0.97d branch); OpenMU 0.95d has +25 and no cap |
| +11 Item | one thing at +10, 1 Chaos, 2 Bless, 2 Soul | 45%, +20 lucky, at most 75% | 4,000,000 | +11 | thing and jewels gone | the same |
| Dinorant | ten Horns of Uniria at full life (255), 1 Chaos | 70% | 500,000 | a Horn of Dinorant, its options not rolled | the box gone | WebZen `PegasiaChaosMix` under NEW_FORSKYLAND2 (OpenMU 0.95d: three horns, 250,000) |
| Chaos Weapon | 1+ things at +4 or more with an option, 1+ Chaos, any Bless and Soul | box's old price / 20,000, at most 100 | 10,000 a percent | Chaos Dragon Axe, Nature Bow or Lightning Staff at +0..+4, own luck and option rolls | jewels gone, each thing to a lower plus, option down half the time | Version075 `ChaosMixes`, `ChaosWeaponAndFirstWingsCrafting` |
| 1st Level Wings | the Chaos Weapon's box with a Chaos weapon at +4 with an option in it | as the Chaos Weapon | as the Chaos Weapon | the hero's class's wing at +0 (ours) | as the Chaos Weapon | WebZen `DefaultChaosMix` `MixResult2`; OpenMU 0.95d number 11. docs/wings.md |

- +10/+11 are **not 0.75**; they are why `kMachineCap` is 11 while `kRefineCap` (jewels, drops)
  stays 9.
- A box that is both +10 and Chaos Weapon is +10. MuMain decides by mix.bmd's order, which this
  tree does not have. Ours.
- Old prices are MixMgr's and OpenMU's: Bless 100,000, Soul 70,000, Chaos 40,000, the rest what
  a merchant charges.
- No +S skill roll on a Chaos weapon: skills are orbs here.
- The mix draws from its own dice (`mixDice_`), so no seeded run moves.

**The window** (`game/ui/mixer.cpp`), MuMain's RenderFrame top to bottom: the recipe (yellow
ready, red "Improper items for combination"), success rate and Zen (pale blue; Zen red when he
is short, ours), the grid, "Assembly prediction:" and the likest recipe's lines coloured as
GetSourceName colours them, and Combine, which asks "Do you want to combine your items?" on the
same foot. The answer ("Chaos combination has succeeded / has failed") stands in the recipe's
place, since there is no system log. Sparks over the box for two seconds, drawn in the window's
own ink (MU's BITMAP_SHINY is not in the interface art).

**Sounds.** eMix with eGem on success, eMix with eBreak on failure (`machine_mix`,
`machine_break` in `source/sounds/sounds.json`), cooked 2026-10-02.

## Open

- **The Chaos weapons are not built.** Mace07, Bow07 and Staff08 are not in `index.json`, so a
  Chaos Weapon box is judged and priced but the Goblin refuses to run it ("no Chaos weapon is in
  this world's tables"). Importing the three is the next content step, and every world's tables
  after it.
- The shine ladder has no +10/+11 rung of its own yet.

## Phase two: services (2026-10-02)

The user picked the revised proposal (claude.ai/artifact/FaaQXq4irQCdk5oed6SksA, "proposed
version is perfect"): the same window, improved with controls the game already draws, and no
paginator dots.

- **Service row** under the title: Options' row and chevrons, the service in gold. A step plays
  `quest_page_turn` (the journal's and the map's page sound) and turns the page as the quest
  journal does: out in 0.14 s, in in 0.20 s, sliding 22 units (`Canvas::fadeSince`).
- **Box** (unchanged), then **Recipe** (or **Sockets**), **Needs** with in-box/wanted counts,
  **Chance** on the character card's meter (luck's share faint when the thing is not lucky),
  Success and Failure in words. The answer stands in the Chance block's place, gold or red, and
  the button becomes **Take out**.
- **Foot**: the vault's coin and the cost (red when short) at the left, the button at the right,
  the confirm on the same foot.

The services (`sim::Service`, `sim::judge(tables, box, service, socket, kin)`):

| service | box | chance | Zen | answer |
|---|---|---|---|---|
| Combine | phase one's recipes | as above | as above | as above |
| Remove Rune | one thing with a rune, 1 Chaos | 100% | 500k / 1M / 1.5M by rarity | the picked socket's rune back as a Rune of Creation, the socket empty |
| Add Socket | one thing with room for a socket, 1 Life, 1 Chaos, 2 Souls, 2 Blesses (the user, 2026-10-04) | 50 / 35 / 20% for the 1st / 2nd / 3rd | 1,000,000 | one more socket; failure takes the jewels only |
| Fuse Runes | three runes of one rarity (not Legendary), 1 Chaos | 100% | 500,000 | one random rune of the next rarity his class may set |

All three rune services are invention, and their numbers are the proposal's first guesses.

## Trying it

    ./main.sh --new --windowed --world noria --at 182,106 --level 50 --zen 20000000 \
      --give "Sword01:1:+9,Jewel15,Jewel01,Jewel02" --talk "Chaos Goblin" --save /tmp/mix.json

`tests/sim_test.cpp` `testChaosMachine` runs +10 until it makes one, checks +11's rate and price,
the Chaos Weapon's judge, and that walking off hands the box back.
