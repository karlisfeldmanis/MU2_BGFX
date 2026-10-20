#pragma once

// **The Jeweler**, ours: Moss the Gambler in Noria (npc 492, MuMain's MONSTER_MOSS_THE_MERCHANT),
// whose window works the powered rings and pendants (sim::Affix) three ways. The user's, in
// conversation on 2026-10-20 (docs/game/jewellery.md "The Jeweler"):
//
//   * **Recut** one further power: that line rolls again, from the powers the piece does not
//     already carry. The signature and the count (the colour) never change. Zen by the plus, and
//     one jewel by the colour ("for recut we need also some jewels").
//   * **Salvage**: the piece is broken for one prize, a lottery ("you can get 1 jewel and there is
//     chance to get some shit and something good"), never a rune ("dont give runes on salvage").
//     The rarer the piece, the better the odds; +7 or luck rolls twice and keeps the better.
//   * **Combine** three of one colour into a random powered piece of the next, at the lowest plus
//     of the three: white to blue, blue to purple. Zen only ("combine does not ask jewels but
//     just zen"); legendary is only ever found.
//
// Bound pieces take all three (the user: "never mind they can"): a recut one stays bound, a
// salvage's prize is free (jewels never bind, sim::bindable), and a combine with a bound piece in
// it makes a bound one.

#include <cstdint>

#include "content/tables.h"
#include "sim/items.h"

namespace mu::sim {

class Random;

constexpr int kJeweler = 492;

// What he works: the powered pieces alone, the six with a signature and a colour.
inline bool jewelerTakes(const content::ItemRow& row) { return powered(row); }

// ---- Recut -----------------------------------------------------------------------------------
// 50,000 at +0 to 500,000 at +9 on the powers' own square curve, to the thousand.
int64_t recutZen(int refinement);
// The jewel one asks, by how many powers the piece carries: white and blue a Chaos, purple a
// Soul, legendary a Life. As the item's (group, number).
struct ItemKey {
    int32_t group = -1, number = -1;
};
ItemKey recutJewel(int powers);
// A further power for line `line` (0 to 2) of `what`: any the row's signature, Haste and the
// piece's other lines leave, off `dice`. None when nothing is left.
Affix recutRoll(const content::ItemRow& row, const Held& what, int line, Random& dice);

// ---- Salvage ---------------------------------------------------------------------------------
// The prizes, worst to best: three Small Healing Potions, then a Jewel of Chaos, Bless, Soul and
// Life.
constexpr int kSalvagePrizes = 5;
ItemKey salvagePrize(int prize);
int salvageCount(int prize);  // how many of it: 3 potions, 1 jewel
// In hundredths, a row a colour (1 to 4 powers), a column a prize. Ours, a first draft.
constexpr int kSalvageOdds[4][kSalvagePrizes] = {
    {45, 35, 15, 5, 0},   // white
    {30, 30, 25, 12, 3},  // blue
    {20, 20, 25, 25, 10}, // purple
    {5, 10, 20, 30, 35},  // legendary
};
// +7 and up, or luck: two rolls and the better kept.
constexpr int kSalvageTwiceFrom = 7;
int salvageRoll(int powers, bool twice, Random& dice);

// ---- Combine ---------------------------------------------------------------------------------
// The Zen for three of `powers` into one of `powers` + 1: 500,000 white to blue, 2,000,000 blue
// to purple, 0 for a colour that does not combine.
int64_t combineZen(int powers);
// The made piece's further powers: `powers` - 1 of them, none its signature, none Haste, none
// twice, off `dice` (the drop's own draw, Realm::leave's rollAffixes).
void combineAffixes(const content::ItemRow& row, int powers, Held& what, Random& dice);

}  // namespace mu::sim
