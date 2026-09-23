# Sprint 7: rules II and the windows

**Proved by:** kill, pick up, equip, sell, in windows that redraw on change only; HUD account
kept. (PLAN.md's sentence, unchanged.)

**Opened** 2026-09-21, from MU2's `client/core/{Hud,Panel,Bag,Card,Shelf,Quick}.cs` and
`shared/{Satchel,Market,Rows}.cs`, read one file at a time. The user asked for the HUD, the
inventory, the character window and the vendor window "migrated from MU2", which is this
sprint's windows half; the rules half is what those windows mirror, and a window over nothing
is not a migration.

## Decided on 2026-09-21

| question | answer | what follows |
|---|---|---|
| where potions go | **1 to 4**, and Q W E R stay the skills' (PLAN.md) | MuDream's plate paints `1 2 3 4 5` under the big boxes and `Q W E R T` under the potion boxes. Both rows of painted labels are covered and redrawn in code, so the key printed under a box is the key that fires it. The fifth big box and the T box print nothing |
| the art | **MU2's `interface/` PNGs through `index.json`'s `effects` keys**, as `Panel.Art` reads them | nothing re-cut; `bag_back` is `interface/win_back.png` in both copies |
| the item rows | **`index.json`'s `objects[].stats`**, 118 rows with footprint, requirements, classes, defence | `mu.db` is not needed for any of it, which matters because the root `mu.db` is deleted in the working tree |
| pictures in cells | **the real models, rendered**, as MU2 does and as MU's `RenderObjectScreen` does | an offscreen stage with an orthographic camera; redrawn when the bag changes or an item is hovered (it turns), never otherwise |

## The shape: one layer, a table per window, and a mirror

- **`gfx/interface`**: textured quads and polygons in pixels, in submission order, one draw
  per run of one texture, and the overlay's baked face for text. Retained: a window rebuilds
  its list only when something it mirrors moved, and the frame resubmits the list it kept.
  That is what "redraw on change" means in an immediate-mode renderer, where the costly half
  of a redraw is the layout and the strings, not the upload.
- **The tables are MU2's, number for number.** The HUD is placed in MuDream's plate pixels
  with one `Unit` (0.4) into MU's 640x480 and one screen scale (`height / 480`); the panels
  are MU's 190x429 at MU2's `Scale` of 2 at 1080 lines, scaled with the screen's height (see
  the findings). A number that differs from MU2's is a finding and says so where it is.
- **Windows raise requests and redraw.** Nothing in `game/` moves an item or spends a point;
  it asks the realm, and draws what the realm then holds. The same gates the realm refuses
  by (`fits`, `movable`, `short`) are the ones a drop target is coloured by, so the red cell
  and the refusal cannot drift apart. MU2's `interface-is-a-mirror` rule, kept without a wire.

## In this order

1. **The layer.** `gfx/interface`, `fs_interface.sc`, an `Interface` texture role (the bytes
   as stored; this is drawn after the tonemap), the window's release and key edges.
2. **The HUD.** The plate, the two gems cropped at the waterline (`RenderLifeMana`), the
   level hairline sliding as MU2's does, the side buttons, the tips, the relabelled keys.
   Mana comes into the sim for it: 0.75's per-class maximum and nothing that spends it yet.
   The shield bar is not drawn, because 0.75 has no shield; the ability bar is not drawn, as
   in MU2 (`AbilityShown`).
3. **The character window** and `Panel`'s shared frame: head, crest, plate, fields, the X,
   the tooltip. The plus spends one point through the realm.
4. **The items.** The cooked catalogue, the satchel (MU's twelve worn slots and eight by
   eight, footprints), `Held`, requirements through `Needs.Asking`'s formula, armour defence,
   the refinement tables, the starting kit, equipping through a move, potions drunk.
5. **The bag window**, with the item stage.
6. **The merchants and the shelf.** Lorencia's townsfolk in the realm and drawn, talking
   opens the shop, buying and selling through `Coin`, Zen on the character.
7. **The drop and the pickup**, which closes the sentence: kill, pick up, equip, sell.

## What is deliberately not here

- **Skills.** The skill boxes are drawn empty. PLAN.md's Diablo 3 shape is its own sprint.
- **The chat, the menu, the chaos machine, the vault.** Their buttons are drawn dim, as MU2
  draws the chat's.
- **Refining with jewels.** The tables are transcribed for the tooltip's numbers; the drag
  that applies a jewel is later.
- **Luck and options on drops.** A drop is `+0` with no luck until step 7 says otherwise.

## Measured

### Steps 1 to 3: the layer, the HUD and the character window (2026-09-21)

- **The HUD costs 0.05 ms of wall frame.** Median frame over 900 frames of played Lorencia,
  two runs each: 10.523 and 10.544 ms with `--windows off` (no desk at all), 10.590 and 10.586
  with the HUD up. The character window open on top: 10.645. Inside the present account's 0.5.
  **Taken at 3456x1894**, this Mac's Retina backbuffer, which `--width 1920 --height 1080`
  does not change (the window is asked for in points and the framebuffer comes back doubled).
  The whole frame is overdrawn at that size with or without the HUD; the 5.5 ms budget is a
  1080p number and needs a 1080p backbuffer to be read against. Not this sprint's to fix, and
  said here so nobody reads 10.6 ms as the HUD's doing.
- **Redraw on change holds.** The HUD rebuilds 24 times a second because the gems turn
  (MU2's `Turn`), and otherwise only when a number, the pointer's box or a window's state
  moves. The character window rebuilt once in a 900-frame run, and exactly once per point
  spent when `--ui-click` pressed its plus twice (log: `rebuilt ... card 7` over a run where
  the only changes were the two presses, their two releases and the two spends).
- **15 draws for the HUD, 22 with the character window**: one per run of one texture, in
  painter's order.
- **A plus spends through the realm.** `--ui-click 100:0.9707:0.3426` twice on a level-5
  knight: `window: a point into strength spent` twice, points 20 to 18, strength 28 to 30,
  damage 4~7 to 5~7. The shot shows it.

### Steps 4 to 6: the items, the bag, the merchants (2026-09-21)

- **The rows.** `.mur` version 5 carries all 118 of `index.json`'s item rows and the
  townsfolk (14 in Lorencia, 6 in Noria), the latter transcribed in `tools/cook.py` from MU2's
  `Folk.cs` because neither the index nor `mu.db` carries them in a readable form.
- **The rules** (`sim/items`, `sim/market`) and 36 new checks in `sim_test`, 117 in all, none
  failed: the requirement formula against MU2's own worked numbers (a Small Shield asks 26
  strength, a +2 asks 38, a Small Axe 21), the refinement tables, the footprint walk, equipping
  and unequipping as moves, the class gate refusing a knight a Skull Staff through the same
  `movable` the window colours by, and three potions drunk one per half second.
- **The satchel is the truth and the hands are read off it.** `Realm::equip` now puts what it
  is given into the satchel's hands and re-reads them (`rearm`); `armsOf` takes the player's
  defence from his worn pieces. Two seeded headless runs of 6000 ticks are the same bytes.
- **A drag through the window.** `--ui-click 60:<shield>:<left hand>` on a level-10 knight:
  `window: move 13 -> 1 taken`, the Small Shield in his left hand. A right-click on the
  potions: `use 12 taken`, three to two.
- **A merchant, end to end.** `--talk Lumen --zen 5000`: the hero walks 19 steps to the bar,
  `hero is served by Lumen the Barmaid`, her shelf opens in column two with the bag beside it.
  Two clicks on the Ale: `buy shelf 0 taken`, 5,000 to 4,250 to 3,500 (750 each, `Coin`'s
  flat price for the Ale). A drag of one back onto her shelf: `sell slot 12 taken (250 paid)`,
  a third. The shot shows both windows.
- **At 1080p** (the window opened on a 1080p display for these runs): 4.33 and 4.35 ms median
  frame with no windows, 4.35 and 4.37 with the HUD, 4.39 with the bag and the character
  window open. Inside 5.5.

### Step 7: the drop and the pickup (2026-09-21)

- **What a death leaves** is MU2's `Realm.Leave` walk, rarest first: a jewel at 0.001, an item
  at 0.3 from what the monster's level affords (drop level at most its level and within twelve
  below it), Zen at 0.5 worth the kill's experience plus seven, otherwise nothing -- 80% of
  kills leave something. The plus is `Loot.Refinement`. **No luck roll and no skill roll**,
  which is smaller than MU2. It lies for `Loot.Lingers`' sixty seconds.
- **Picked up on arrival**, within a tile of it: a click on a drop is a Pick order, and the bag
  refuses what it has no room for at its footprint.
- **Labelled** on MU's opaque black plate in `BuildGroundItemLabelDescriptor`'s colours, Zen
  in gold. **No model on the ground yet**; it waits on the stage, as the bag's pictures do.
- **The sentence, headless** (`sim_test`, `testLoot`): a level-8 knight hunts the field at
  200,160 for 12,000 ticks with a plain hand -- 57 kills, 41 drops, 11 items and 1,208 Zen
  picked up; five axes put on (each a swap through `moveItem`), three Short Bows refused as the
  elf's; then a Talk to Lumen, served at the bar, eleven things sold for 304, and the right
  hand refused as worn. 127 checks, none failed.
- **The sentence, in the window**: `--at 200,160 --weapon Axe01 --click-every 20 --loot
  --windows inventory` picks up 165 Zen in 5,000 frames; the shot shows the `55 Zen` label.
- Two seeded headless runs of the spider field are the same bytes.

### The quick bar on 1 to 4 (2026-09-21)

- MU's gesture binds: a potion hovered in the bag with its key pressed
  (`CNewUIMyInventory::UpdateKeyEvent`), and MU2's drag of one onto a potion box. The key then
  drinks the strongest carried thing that may stand in for the bound one -- the healing family
  falls to the apple, the mana family to the small mana potion, the Town Portal only to itself
  (`Quick.Substitutes`) -- and the box counts all of them.
- Proved with `--ui-click 50:<bag slot 12>:<first potion box>` and `--ui-key 100:1
  --ui-key 140:1`: `slot 12 bound to key 1`, `use 12 taken`, and the second press refused
  inside the half-second cooldown. The box reads `Mediu 4`: two Medium Healing Potions and
  the two Apples that stand in for them.
- **The bindings are the interface's** until sprint 9's save writes them.

### Step 5's stage, and the models on the ground (2026-09-21)

- **The pictures are the real models.** `game/items_stage` stands what a window holds on a
  stage its own size and `Renderer::drawStage` (`gfx/stage_pass.cpp`, `shaders/fs_stage.sc`)
  photographs it into an RGBA8 target at 4x MSAA, which the window draws like any other
  image. Two stages, a view each: `ViewStageBag` 53 and `ViewStageShelf` 60, both after the
  HUD, so a restocked window shows its new picture on the next frame and the target keeps the
  old one meanwhile.
- **Its own room, and none of the frame's.** MU2's `Panel.Stage` number for number: a key
  light at 0.7 from up, left and front (Godot rotation -0.7, -0.6, 0), a diffuse fill of
  (0.65, 0.65, 0.7) at 1.7, and `Panel.Studio`'s softbox -- brightest overhead, dark
  underneath, near-neutral and a little warm -- for the metals to reflect, since a metal has
  no diffuse term and on one directional light the Plate set is a black silhouette. No sun,
  no split, no screen-space AO, no probe: the item is not where the frame's shadow map is.
  `fs_stage` writes sRGB, because the interface draws after the tonemap.
- **One store of models for both the windows and the ground** (`game/item_models`): the
  cooked `.mum` where the wardrobe has one, the row's own glb where it does not -- the
  potions, jewels, scrolls and Zen, which `tools/cook.py --only wardrobe` does not take. Read
  on first sight of a kind and kept.
- **What a death leaves is drawn** (`game/litter`), MU2's `Drops`: tossed 90 units up at 8 a
  reference frame against a gravity of 6, one bounce at a third of the arrival speed, then
  laid down by measurement -- longest extent along the ground, shortest up -- at a yaw hashed
  from the drop's own id. The weapons, shields, armour, trousers and gloves sleep; a helm
  and a pair of boots stand, as MU's own range leaves them. Zen is a heap of Gold01, coin by
  coin: `clamp(sqrt(amount) / 2, 3, 80)` of them, each with its own turn, fall and toss.
  They cast into the sun's list, and they are out of the reflection probe.
- **Measured**: 4.015 ms median frame over 2,000 frames of the spider field with no windows,
  4.062 with the bag open and its pictures drawn -- 0.05 ms, inside the present account, and
  the drops are in both. A stage redraws only when what stands on it changes or something on
  it turns, as MU2's `Panel.Repaint` does.
- **Seen**: a Hand Axe and a Crossbow lying labelled on the grass with two Zen heaps beside
  them, the bag drawing the worn axe and the picked-up pieces, and Lumen's shelf drawing the
  Ale and the Town Portal Scroll under their prices.

**Not done yet:** the potion boxes on the quick bar still draw a name -- the HUD is a third
window and wants a stage of its own -- and the sparkle (`CreateShiny`) a drop throws off
every couple of seconds, which waits on the effects it would use. `Shield01` has no cooked
mesh in the figures' wardrobe, so a hero told to hold one holds nothing; that is the cook's,
not the window's.

### Findings

- **The panels scale with the screen.** MU2 draws them at a fixed twice in viewport pixels.
  On this backbuffer a fixed two is a window 45% of the screen's height where MU2's 1080 gave
  79%, so the factor is `2 x height / 1080`: MU2's number exactly at 1080, and the same share
  of the screen at any other size. A departure from MU2's "integer factor lands the art on
  whole pixels", taken because the art is 2x-cut and is minified here anyway.
- **The disc does not brighten under the pointer.** MU2 modulates it by 1.35; a vertex colour
  cannot pass white. The icon's own lit state carries the hover. A departure.
- **The shield and ability bars are not drawn**, and neither is the buff strip or the fan:
  0.75 has no shield, nothing spends ability, and there are no skills yet.
- **No attack speed line** under agility. MU2 prints one; this sim has no attack speed stat,
  because MU paces a swing by its clip (sprint 5).
- **The start-up courtesy spend is halved.** `Play::open` used to put a high-level
  character's spare points into strength "for want of a stat window". It still pays for what
  he is asked to hold, and the rest now stays in hand for the window. The remainder of that
  courtesy goes in step 4, when requirements are MU's formula.
- **Worn slots check the way back.** Putting a piece on sends what was worn to where the new
  one came from, and MU2 never asked whether it fits there: a Small Shield put on over a Kite
  Shield would lay the Kite's 2x3 across whatever sat under the Small Shield's 2x2. Asked
  here, refusing the move; a departure from MU2 in the direction of not corrupting the bag.
- **A drag let go outside the bag throws the item on the ground** (2026-09-23, once there was
  a ground to throw it on). `Realm::discard`, off MU's `SendRequestDropItem` and MU2's
  `Realm.Discard`: the thing leaves the slot, lands on his tile or the nearest clear one --
  the same `clearing` a kill's drop takes -- and lingers the same minute. Worn slots are
  thrown too, and the hands are re-reckoned. The shelf and a quick box are asked first, as
  MU2's `Bag.Caught` is; money is not thrown, because money is not in a slot.
- **No repair button.** Nothing in this sim wears, so it would be a button that does nothing.
- **Talking walks to the townsperson and serves within three tiles** (MU2's `Counter`, marked
  there as MU2's: neither MU nor OpenMU checks a distance). Any new order closes the counter.
- **Mana arrives in the sim** as MU2's per-class rates (`Beast.cs` `Rates.For`), not yet
  traced to OpenMU's lines independently. Nothing spends it.

## The skin, 2026-09-23

MU's leather came off. The three windows keep every rectangle sprint 7 gave them -- the 5x3
worn block, the 8x8 satchel at (15, 200), the Zen strip at 380, the character window's four
wells, the shop's 8x15 -- and are painted in the item card's material instead.

**Chosen from a page of four**, drawn on MU's own window at the size the engine draws it and
compared side by side: **B, "obsidian, deeper wells"**. The other three were A (the same panel
with shallower cells and a bone title), C (a cooler, lighter, more see-through panel with a
hairline grid and no cell fill at all) and D (ink and brass, square, twice the edge). The page
also carried a reference board -- MU 0.75, Diablo II, Diablo III/IV, Path of Exile at one scale,
and the decisions each makes -- because the first attempt at this work went straight into the
engine off three ASCII sketches and produced a layout nobody had looked at.

The kit is `src/game/ui/sheet.{h,cpp}`, in screen pixels, and `panel::frame` is the one place
all three windows come through:

| | what it is |
|---|---|
| body | near-black, two steps: 0.95 to 0.92, flat on purpose |
| edge | a **gradient stroke**, 0.60 along the head and 0.07 by the foot, drawn inside the corners |
| shadow | the card's own three falloffs (`tip::shadowUnder`) |
| head | a band of light, a gold diamond, the name in tracked capitals, a rule that fades at both ends |
| cell | a well: a seat of shadow inside the top edge, a hairline of light at the foot, a quiet border over both |
| close | a drawn cross, three lights, lit under the pointer |
| plus | a gold diamond with a plus cut out of it |

**Six pieces of art are no longer drawn**: `bag_back`, `bag_plate`, `bag_crest`, `bag_cell`,
`bag_field`, `bag_close`. What stays MU's, on the user's word, is the worn slots' own
silhouettes (`bag_slot_*`) -- tinted dark rather than only faded, because each one is a pale
plate with the shape painted on it and the canvas multiplies the vertex colour through the
texture.

Departures from MU that the skin brought with it, each one small and each one deliberate:

- **Six units of air between two open windows.** MU butts its columns flush, which worked while
  every window was a slab with its own carved border.
- **The hovered thing is lit over its whole footprint**, not over the one cell it is recorded in.
- **The vendor prints every price** along the foot of the thing it prices, red where he cannot
  pay; the card carries the figure in full.
- **The character window has a foot**: the level's own progress as a bar. MU stops at Energy and
  leaves its bottom fifth empty, which on leather was a texture and on this skin was a hole.
- **A merchant's name is fitted** to the room between the mark and the cross -- shrunk by up to a
  quarter, then trimmed with two dots.

### Paid for once

The trim above was first written as a loop that popped a letter and put the two dots back inside
its own condition. For a name of exactly the wrong length -- `Lumen the Barmaid` -- it alternates
between too long and short enough for ever: the frame never returned and **the game froze the
moment a merchant's counter opened**. A trim measures the string it is going to draw, and never
grows.

### Open

`Shield02` (Horn Shield) photographs as a black silhouette on every stage -- in the bag, on the
shelf and in the tooltip's plate. It is the model or its material, not the skin.
