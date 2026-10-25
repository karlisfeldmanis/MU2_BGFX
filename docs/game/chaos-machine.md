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
| 1st Level Wings | the Chaos Weapon's box with a Chaos weapon at +4 with an option in it | as the Chaos Weapon | as the Chaos Weapon | the hero's class's wing at +0 (ours) | as the Chaos Weapon | WebZen `DefaultChaosMix` `MixResult2`; OpenMU 0.95d number 11. docs/game/wings.md |

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
`machine_break` in `content/sounds/sounds.json`), cooked 2026-10-02.

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
| Add Luck | one weapon, armour, shield or wing without Luck, 3 Chaos, 3 Bless, 3 Souls, 3 Lifes, and a Rare, an Epic and a Legendary rune (the user, 2026-10-25, for Bruno's "Jewel of Luck" -- MU has none; three of each "because its very strong feature") | 50% | 300,000 | the thing gains Luck; failure takes the jewels and runes only |
| Fuse Runes | three runes of one rarity (not Legendary), 1 Chaos | 100% | 500,000 | one random rune of the next rarity his class may set |

All three rune services are invention, and their numbers are the proposal's first guesses.

The Jewel of Harmony's three (docs/plans/jewel-of-harmony.md step 6, protocol 59; the user,
2026-10-25: everything at the Machine, where MU has Elpis, Osbourne and Jerridon):

| service | box | chance | Zen | answer |
|---|---|---|---|---|
| Refine Gemstone | Gemstones, any number (Fill takes them all) | 80% each | none | a Jewel of Harmony for each that takes; the rest are lost |
| Smelt | weapons, staffs, shields or armour with no Harmony option; the 84 cheapest (the Kris, the Bronze set...) at +4 or more | 50% each excellent piece, 20% each other | none | a Higher Refining Stone from an excellent piece, a Lower from any other; every piece eaten |
| Remove Harmony | one piece with a Harmony option, 1 Jewel of Harmony, 1 Chaos (ours; the user, 2026-10-25) | 100% | 100,000 to 360,000 by the option's level | the option off; the piece stays bound; the jewels are spent |

The rates and the cheap list are OpenMU's (mix.bmd agrees on 80, 20 and 50; WebZen's are
commonserver.cfg keys and its list a JewelOfHarmonySmelt.txt we do not have). MU takes one
Gemstone or one piece a mix; ours a box of them, each rolled on its own (sim::byPiece). Fill from
bag never puts gear in to smelt: the pieces are the player's to pick.

## Phase three: every service on the window (2026-10-20)

The user asked for every craft option on screen as buttons ("craft options all visible with
buttons and user press it"), then picked canvas B (claude.ai/artifact/5vvfQYnoxuLNTUuzQZJrmP),
matched to Sanctuary. Phase two's service row and its page turn are gone.

- The window is the Broker's width, 618 units, three columns at the vault's place
  (`Mixer::kWide`, `panel::columnX(width, 4)`), the bag beside it.
- **The list**, on the left: the services under MIXES (Upgrade, Chaos Weapon, 1st Wings,
  2nd Wings, Dinorant), TICKETS (Invisibility Cloak, Devil's Invitation), RUNES (Remove Rune,
  Add Socket, Add Luck, Fuse Runes) and HARMONY (Refine Gemstone, Smelt, Remove Harmony). Each one is a bordered iron row, and the picked one is in the travel
  list's gold. At its right is **Ready** in green when the bag and the box hold everything it
  needs, else **N missing**. A press picks the service and plays `quest_page_turn`.
- **The page**, on the right: the service's name in Cinzel and what the box is to it, then the
  box (eight by four at 26 units a cell, larger than the bag's 21) beside **Needs**. Each need
  shows in-box over wanted and how many more the bag holds: green when met, bright when the bag
  has the rest, red when it has none. Remove Rune adds **Socket** rows in their runes'
  rarities, each with its price. **Chance**, Success and Failure come under the box, and the
  answer takes their place after a run.
- **The foot**: the cost (red when short) and his Zen, then **Take out**, **Fill from bag** and
  the service's button. The confirm is on the same foot.

**Fill from bag** is `sim::plan` (machine.h). It tries the bag's things in a copy of the box,
one at a time, through `judge()` itself, and keeps a thing only when it brings a need nearer
without going past it or blocking the box (`Judged::blocked`: two levels, two rarities, a
Legendary, a bound thing in a mix). So no recipe is written twice. When the bag holds two or
more of the thing worked on (the +9, the item to socket, the 1st wing), that choice is the
player's: Fill puts in the rest and the need reads "N in bag, pick one". The desk sends one
`PutIn` a slot to the first free cell, as many drags would, so it needs no protocol change.
The list's Ready is the same plan, allowed to pick that thing (`anyTarget`).

## Stacked jewels (2026-10-20)

The Bless, Soul, Life and Chaos stack twenty a cell, as the potions do (bug report #41: "It's
getting hard to manage single jewels"; the user: "we need stacking jewels"). The Rune of
Creation does not: each carries its own power. Canvas claude.ai/artifact/DDBMWGBD7VHc3QEHGDGnf1.

- A stack in the box counts as its pieces (`piecesOf`). Dropped in, right-clicked in or put by
  Fill, a stack gives only what the open service wants of it (`sim::piecesWanted`, the count
  `judge()` likes best, the fewest on a tie): one Chaos of twenty for the +10. A need of any
  number (the Chaos Weapon's Bless) takes one; more go in by Shift-click. `Planned::pieces`
  carries Fill's counts.
- **Shift-click** on a stack in the bag, the vault or the box opens the number box ("Split
  stack", `Amount::Purpose::Split`); its OK puts that many on the pointer, the rest stays in the
  cell, and the drop asks the realm for that many: Move, Deposit, Withdraw, Rearrange, PutIn,
  TakeOut, Shuffle, DealPut, Sell and Discard carry the count in the command's `c`, and
  `Realm::applyCommands` splits it off into a free cell of the same grid before the method runs,
  pouring it back when the method refuses. The stack's card says "Shift-click to split".
- A stack sells at the counter for every jewel in it (`buyingPrice`). At the Broker a stack is
  one listing at the stack's price, bought whole; only a single jewel joins the cheapest-first
  row (`brokerAlike`), and the Quote is a piece's price, which the Sell tab and the bots multiply
  by the stack's count. Protocol 40.

## Trying it

    ./mu play --new --windowed --world noria --at 182,106 --level 50 --zen 20000000 \
      --give "Sword01:1:+9,Jewel15,Jewel01,Jewel02" --talk "Chaos Goblin" --save /tmp/mix.json

`tests/sim_test.cpp` `testChaosMachine` runs +10 until it makes one, checks +11's rate and price,
the Chaos Weapon's judge, and that walking off hands the box back.
