#include "sim/wear.h"

#include <algorithm>
#include <cmath>

#include "sim/items.h"
#include "sim/market.h"

namespace mu::sim {

namespace {

// What each plus adds, summed: OpenMU's AdditionalDurabilityPerLevel, which is MuMain's
// CalcMaxDurability loop written out -- +1 a level to +4, +2 to +9, then +3 up to +8.
constexpr int kAddedByPlus[16] = {0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 17, 21, 26, 32, 39, 47};

// RoundPrice, as market.cpp keeps it.
int64_t round(int64_t price) {
    return price >= 1000 ? price / 100 * 100 : (price >= 100 ? price / 10 * 10 : price);
}

}  // namespace

bool wears(const content::ItemRow& row) {
    return row.durability > 0 && placeOf(row) >= 0 && !ammunition(row);
}

double weaponWear(const content::ItemRow& row, const Held& held, int defense) {
    constexpr int kStaves = 5;
    const int option = optionValue(row, held.option);
    const int armed = std::max(0, defense);
    if (row.group == kStaves) {
        const int magic = row.magicPower / 2 + held.refinement * 2;
        const int divisor = magic + magic / 3 + option;
        return divisor > 0 ? double(armed / divisor) / 1050.0 : 0.0;
    }
    if (row.group <= kGroupBows) {
        const int least = row.minimumDamage;
        const int divisor = least + least / 2 + option;
        const double past = row.group == kGroupBows ? 780.0 : 564.0;
        return divisor > 0 ? double(armed * 2 / divisor) / past : 0.0;
    }
    return 1.0 / kHitsPerDurability;
}

int maximumDurability(const content::ItemRow& row, int refinement) {
    if (!wears(row)) return 0;
    const int plus = std::clamp(refinement, 0, 15);
    return std::min(255, row.durability + kAddedByPlus[plus]);
}

int maximumDurability(const content::ItemRow& row, const Held& held) {
    const int most = maximumDurability(row, held.refinement);
    if (most <= 0 || held.excellent == 0) return most;
    return std::min(255, most + 15);
}

int fullDurability(const content::ItemRow& row, int refinement) {
    return wears(row) ? maximumDurability(row, refinement) : row.durability;
}

bool repairsAt(int npc) { return npc == 251 || npc == 243 || npc == 246; }

float wearCut(int durability, int maximum) {
    if (maximum <= 0) return 0.0f;
    if (durability <= 0) return 1.0f;
    const float gone = 1.0f - float(durability) / float(maximum);
    if (gone > 0.8f) return 0.5f;
    if (gone > 0.7f) return 0.3f;
    if (gone > 0.5f) return 0.2f;
    return 0.0f;
}

Worn wornBand(int durability, int maximum) {
    if (maximum <= 0) return Worn::Fine;
    if (durability <= 0) return Worn::Broken;
    const float left = float(durability);
    if (left <= float(maximum) * 0.2f) return Worn::Fifth;
    if (left <= float(maximum) * 0.3f) return Worn::Third;
    if (left <= float(maximum) * 0.5f) return Worn::Half;
    return Worn::Fine;
}

int64_t repairPrice(const content::ItemRow& row, int refinement, bool skill, int durability,
                    bool atMerchant) {
    const int maximum = maximumDurability(row, refinement);
    if (maximum <= 0 || durability >= maximum) return 0;
    // CalculateRepairPrice: a third of the final buying price, rounded, never over 400 million.
    const int64_t base =
        round(std::min<int64_t>(buyingPrice(row, refinement, 1, skill) / 3, 400000000));
    const float root = std::sqrt(float(base));
    const float missing = 1.0f - float(std::max(0, durability)) / float(maximum);
    float price = 3.0f * root * std::sqrt(root) * missing + 1.0f;
    if (durability <= 0) price *= float(kBrokenRepairFactor);
    if (!atMerchant) price *= float(kSelfRepairFactor);
    return round(int64_t(price));
}

int64_t wornSellingPrice(int64_t sellingPrice, int durability, int maximum) {
    if (maximum <= 1 || durability >= maximum) return sellingPrice;
    const float gone = 1.0f - float(std::max(0, durability)) / float(maximum);
    return round(sellingPrice - int64_t(double(sellingPrice) * 0.6 * double(gone)));
}

}  // namespace mu::sim
