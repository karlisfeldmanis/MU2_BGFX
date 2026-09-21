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
- **Mana arrives in the sim** as MU2's per-class rates (`Beast.cs` `Rates.For`), not yet
  traced to OpenMU's lines independently. Nothing spends it.
