// What Lorencia's and Noria's merchants sell, and what a thing is worth over the counter.
//
// MU2's shared/Market.cs, transcribed from OpenMU's Version075/MerchantStores.cs and
// ItemPriceCalculator: the shelves line for line, with the source's own oddities kept (Hanzo's
// slot 73 given twice, so the Falchion overwrites the Gladius; the Mace and the Morning Star
// both scepter 1), and the price's cubic curve over the drop level.
//
// What the original prices and this does not: the +4 option and luck every piece of shop gear
// is created with in OpenMU. The shelves do not hand either over, so neither is charged --
// MU2's own decision, and its prices are lower than the original's by that stated factor.
#pragma once

#include <cstdint>

#include "content/tables.h"

namespace mu::sim {

// One line of a shelf: where it sits (the shop's grid is a bag eight wide), what, at what
// plus, how many in one purchase for a stacking row (0 otherwise), and whether it comes with
// its skill -- the `+S` in MerchantStores' comments, which the price charges for.
struct Offer {
    int slot = 0;
    int group = 0, number = 0;
    int refinement = 0;
    int pieces = 0;
    bool skill = false;
};

// A merchant's shelf by MU's NPC number, or none (count 0) for anybody who is not one.
const Offer* stockOf(int npc, int* count);
inline bool sells(int npc) {
    int count = 0;
    stockOf(npc, &count);
    return count > 0;
}

// What a merchant charges for one, and pays for one (a third, with its own rounding).
// `shots` / `full` are a quiver's; ignored for anything else.
int64_t buyingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                    int shots = 1, int full = 1);
int64_t sellingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                     int shots = 1, int full = 1);

// How close a character has to stand to be served, in tiles. MU2's `Counter`: neither MU nor
// OpenMU's TalkNpcAction checks any distance, and three is the smallest figure that lets a
// character trade with Lumen, whose whole bar is closed to walking. Marked there as MU2's.
constexpr float kCounter = 3.0f;

}  // namespace mu::sim
