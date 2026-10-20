#include "sim/jeweler.h"

#include <algorithm>
#include <cmath>

#include "sim/random.h"

namespace mu::sim {

int64_t recutZen(int refinement) {
    const double t = double(std::clamp(refinement, 0, kRefineCap)) / double(kRefineCap);
    const double zen = 50000.0 + 450000.0 * t * t;
    return int64_t(std::lround(zen / 1000.0)) * 1000;
}

ItemKey recutJewel(int powers) {
    if (powers >= 4) return {kGroupPotions, 16};  // Life
    if (powers == 3) return {kGroupPotions, 14};  // Soul
    return {12, 15};                              // Chaos
}

Affix recutRoll(const content::ItemRow& row, const Held& what, int line, Random& dice) {
    const Affix own = signatureOf(row);
    uint8_t left[kAffixes];
    int n = 0;
    for (int a = 1; a <= kAffixes; ++a) {
        if (Affix(a) == own || Affix(a) == Affix::Haste) continue;
        bool carried = false;
        for (int i = 0; i < 3; ++i) carried = carried || (i != line && what.affixes[i] == a);
        // Not the line's own either: a recut that gave back what was there would be no recut.
        if (carried || what.affixes[line] == a) continue;
        left[n++] = uint8_t(a);
    }
    return n == 0 ? Affix::None : Affix(left[dice.nextInt(0, n)]);
}

ItemKey salvagePrize(int prize) {
    switch (prize) {
        case 0: return {kGroupPotions, 1};   // Small Healing Potion
        case 1: return {12, 15};             // Chaos
        case 2: return {kGroupPotions, 13};  // Bless
        case 3: return {kGroupPotions, 14};  // Soul
        default: return {kGroupPotions, 16}; // Life
    }
}

int salvageCount(int prize) { return prize == 0 ? 3 : 1; }

int salvageRoll(int powers, bool twice, Random& dice) {
    const int* odds = kSalvageOdds[std::clamp(powers, 1, 4) - 1];
    const auto once = [&] {
        int pick = dice.nextInt(0, 100);
        for (int p = 0; p < kSalvagePrizes; ++p) {
            if (pick < odds[p]) return p;
            pick -= odds[p];
        }
        return 0;
    };
    const int first = once();
    return twice ? std::max(first, once()) : first;
}

int64_t combineZen(int powers) {
    if (powers == 1) return 500000;
    if (powers == 2) return 2000000;
    return 0;
}

void combineAffixes(const content::ItemRow& row, int powers, Held& what, Random& dice) {
    for (uint8_t& a : what.affixes) a = 0;
    const Affix own = signatureOf(row);
    uint8_t left[kAffixes];
    int n = 0;
    for (int a = 1; a <= kAffixes; ++a) {
        if (Affix(a) != own && Affix(a) != Affix::Haste) left[n++] = uint8_t(a);
    }
    for (int i = 0; i < std::min(powers - 1, 3) && n > 0; ++i) {
        const int at = dice.nextInt(0, n);
        what.affixes[i] = left[at];
        left[at] = left[--n];
    }
}

}  // namespace mu::sim
