# The MU2 item card: how the game draws it

Read from `src/game/ui/tip.cpp` (`tip::draw`, `glass`, `glyphAt`, `colourOf`), `tip.h` (`ink::`),
`controls.cpp` (fonts, the stone grain) and `gfx/face.cpp`. Every number is in pixels at a
1080-line screen (`tip::unit()` = screen height / 1080), so scale by `height / 1080`.
`card_style.json` holds the same figures as data; `cards.json` holds each card's rows.

## Fonts
- **Title:** Cinzel Medium (500), 17 px, UPPERCASE, letter-spacing 0.06em, line 21 px, in the
  item's quality tone. It wraps at 346 − 24 − 0.6 × picture.
- **Everything else:** Alegreya Sans Medium (500). The game has no bold; "heavy" means the same
  line printed twice, 0.5 px apart. On the web use weight 700 or `-webkit-text-stroke: .45px`.
- **Drop shadow on every word:** `text-shadow: 1px 1px 0 rgba(0,0,0,.75)`.
- **Opacity:** every ink the card draws has its alpha multiplied by 0.94. The body gradient is not.

## Container
| | value |
|---|---|
| width | 346 px; a sheet's `wide` can override it |
| padding | 12 px |
| corner radius | 4 px |
| body | vertical gradient from `rgba(13,10,8,.92)` to `rgba(5,3,3,.90)` |
| grain | `card_grain.png`, a port of `controls::grain`: a 128 px tile laid 1:1 over the body, 1 px in |
| edge | a 1 px iron band graded from `#6b5645` at the top to `#2a2019` at the foot, alpha .94, radius 5; outside it a 1 px black seam, radius 6 |
| shadow | three Gaussians summed and clamped at .85, not drawn under the card: (drop 1, σ 1.5, α .70), (8, 7, .55), (30, 30, .55). CSS: `0 1px 3px rgba(0,0,0,.7), 0 8px 14px rgba(0,0,0,.55), 0 30px 60px rgba(0,0,0,.55)` |

- **Head tint:** a band over the head in the name's tone. It starts at alpha .13 × .94 at the
  top and fades to clear at the head's foot; its top corners follow the card's radius.
- **Lit top edge:** a 2 px line starting 1 px above the card, from x 4 to width − 4. It is the
  name's tone at alpha .85 × .94 in the middle and fades to clear at both ends.
- **Placement:**
  - The card is centred over the hovered item's cells, with its foot 10 px above them.
  - If there is no room above, it goes 10 px below.
  - If there is room for neither, it stands beside, on the side with more room.
  - It always stays 4 px inside the screen.
  - It shows only while hovering, never during a drag.

## Layout, top to bottom
1. **Head** (12 px pad above and below).
   - Title lines, 21 px each.
   - Subtitle: the type line, 14 px, line 22, in the title's tone.
   - Bound line: "Cannot be dropped", 14 px, line 21, RedPurple `#cc80cc`.
   - **Picture:** the item's own render, a square of 96 × artScale px. Its right edge is 6 px in
     from the card's right and its top is 6 px down.
2. **Headline.**
   - The value: 24 px, heavy, line 31, in its tone.
   - Then 8.4 px of gap and the word: 15 px, label ink `#c4c1b4`.
3. **Breakdown rail.** Each line is 13.5 px on a 21 px line, starting at x 25.
   - Figure first: heavy, in figureTone, or the label ink when it has none.
   - Then 4.7 px and the text in quiet ink `#96998e`.
   - **Left bar:** 1 px wide at x 15, white at .18 alpha, from 3 px under the first line's top
     to 3 px above the last line's foot.
   - 12 px of air after the rail.
   - The sections start at the larger of (head + headline block) and (6 + picture + 4).
4. **Sections.** Each opens with a full-width 1 px hairline (white .06) followed by 8 px of air,
   and ends with 8 px of air.
   - **option / prose:** 14 px text on a 21 px line, starting at x 37.
     - It reads `keyword · text tail` and wraps as one string at 297 px.
     - keyword: white, or keywordTone. text: tone (white by default).
     - tail: heavy, in the tone moved 35% toward white (`tones[x].tail` in the json).
   - **stat:** the label at x 37 in label ink (or labelTone). The value is right-aligned at
     width − 12 in tailTone; several values stack, one line each.
   - **gap:** 9 px of air above a row that has `gap`.
   - **Marks** sit in a 16 px gutter: centre x 20, vertically centred on the row's first line.
     - Diamond (a rolled option): an 8 px glyph, an upright 4.96 px square.
     - Star (an excellent option): four spikes, SVG `0,-4 1.28,-1.28 4,0 1.28,1.28 0,4 -1.28,1.28 -4,0 -1.28,-1.28`.
     - Ring (an empty socket): 18 px, three lozenges whose width is 0.8 of their height —
       shade `rgba(0,0,0,.7)` (h 9), bronze rim `#9e733d` (h 7.92), hole `#0d0908` (h 5.4).
     - RingSet (a set socket): the Ring plus violet `#8c80ff` (h 5.58) and rose `#ff63b3`
       (h 3.6). In game the rune's 3D picture is drawn over it.
     - Mark colour is markTone at alpha .94; Ring and RingSet keep their own colours.
   - **framed** (a set's block):
     - Fill white .03, 1 px outline white .10, from x 31 to width − 6.
     - Rows are indented 6 px more.
     - A marked row's mark stands at the frame's left, with its text 21 px after it.
     - The set's name (`set`) is not printed on an item card.
5. **Foot.**
   - Background black .40 × .94, bottom corners rounded, a 1 px hairline on top.
   - 13 px text on 20 px lines, with 8 px of air above and below.
   - **Left column, from x 12:**
     - The class line first: Green when he may wear it, Red when not.
     - Then one line per requirement: met in the quiet foot ink `#7d8077`, unmet in Red.
   - **Right column, right-aligned at width − 12:**
     - The wear, in foot ink, with its ring to the left. The ring has radius 8 and thickness 3,
       and its centre is 6 + 8 px left of the words, at baseline − 4.3.
     - The ring's back is white .12. Its arc shows what is left (`worn` 0–1), drawn from
       12 o'clock clockwise in the lettering ink `#e6dbc2`, or in ringTone.
     - Then the note, the keep lines and the price.
6. **Sell strip.**
   - 36 px tall, black .55, with a 1 px iron rule `rgba(107,86,69,.9)` on top.
   - "SELLS FOR": 11.96 px, tracking .12em, in foot ink, at x 12.
   - The figure: 14.56 px, heavy, Yellow `#ffcc1a`, right-aligned at width − 12.
   - MU's Zen coin, 18 px tall, sits 5.4 px left of the figure.

## Tones (`colourOf`)
| tone | hex | tail (heavy value) |
|---|---|---|
| White / Uncommon | #ffffff | #ffffff |
| Blue (options) | #80b2ff | #accdff |
| Red (unmet, warnings) | #ff331a | #ff7a6a |
| Yellow (refined, Zen) | #ffcc1a | #ffde6a |
| Green (excellent, class line) | #1aff80 | #6affac |
| Gray | #96998e | #bbbdb6 |
| Violet | #b266ff | #cd9cff |
| RedPurple (bound) | #cc80cc | #deacde |
| Orange | #e66b0a | #ef9f60 |
| Rare (socketed) | #0070dd | #59a2e9 |
| Epic | #a335ee | #c37cf4 |
| Legendary | #1eff00 | #6dff59 |
| Artifact (+7, jewels) | #ffd100 | #ffe159 |
| Quest | #d685ff | #e4b0ff |
| RuneLegendary | #ff8000 | #ffac59 |
| RuneAncient | #00ccff | #59deff |

Other inks:
- label `#c4c1b4`
- quiet `#96998e`
- foot `#7d8077`
- lettering `#e6dbc2`
- delta better `#7fd48b`, worse `#e2685f`

**Quality ladder** (the title's tone, `describe.cpp` `qualityOf`), from the top:
- the Rune of Creation: RuneLegendary or RuneAncient
- excellent: Green
- socketed: Rare
- +7 and up, and jewels: Artifact
- luck, skill or option: Uncommon (white)
- everything else: White
- quest items: Quest

## Hover
- The bag turns the hovered model; the card's picture holds still.
- A value compared with the worn piece gets a 13 px delta, right-aligned: better or worse ink.
  The `cards.json` cards were built with nothing worn, so they carry no deltas.
- Nothing else on the card changes on hover.
