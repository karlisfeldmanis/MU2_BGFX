#include "sim/harmony.h"

#include <algorithm>
#include <iterator>

namespace mu::sim {
namespace {

using E = HarmonyEffect;

// MuMain's JewelOfHarmonyOption_eng.bmd, levels 0 to 11 (docs/plans/jewel-of-harmony.md §2): the
// weapon's options 1-7 (8 is PvP, 9 and 10 act on a player's SD), the staff's 1-5 (6-8 the same),
// and the defence's 1 and 3-5 and 7 (2 is Max AG, 6 PvP, 8 SD). Weights OpenMU's.
constexpr HarmonyOption kWeapon[] = {
    {1, E::MinimumDamage, "Minimum damage", 0, 40, {2, 3, 4, 5, 6, 7, 9, 11, 12, 14, 15, 16}},
    {2, E::MaximumDamage, "Maximum damage", 0, 40, {3, 4, 5, 6, 7, 8, 10, 12, 14, 17, 20, 23}},
    {3, E::StrengthAsked, "Strength requirement", 0, 40, {6, 8, 10, 12, 14, 16, 20, 23, 26, 29, 32, 35}},
    {4, E::AgilityAsked, "Agility requirement", 0, 40, {6, 8, 10, 12, 14, 16, 20, 23, 26, 29, 32, 35}},
    {5, E::Damage, "Damage", 6, 30, {0, 0, 0, 0, 0, 0, 7, 8, 9, 11, 12, 14}},
    {6, E::CriticalDamage, "Critical damage", 6, 30, {0, 0, 0, 0, 0, 0, 12, 14, 16, 18, 20, 22}},
    {7, E::SkillDamage, "Skill damage", 9, 20, {0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 14, 16}},
};
constexpr HarmonyOption kStaff[] = {
    {1, E::Wizardry, "Wizardry damage", 0, 40, {6, 8, 10, 12, 14, 16, 17, 18, 19, 21, 23, 25}},
    {2, E::StrengthAsked, "Strength requirement", 0, 40, {6, 8, 10, 12, 14, 16, 20, 23, 26, 29, 32, 35}},
    {3, E::AgilityAsked, "Agility requirement", 0, 40, {6, 8, 10, 12, 14, 16, 20, 23, 26, 29, 32, 35}},
    {4, E::SkillDamage, "Skill damage", 6, 30, {0, 0, 0, 0, 0, 0, 7, 10, 13, 16, 19, 22}},
    {5, E::CriticalDamage, "Critical damage", 6, 30, {0, 0, 0, 0, 0, 0, 10, 12, 14, 16, 18, 20}},
};
constexpr HarmonyOption kDefence[] = {
    {1, E::Defense, "Defense", 0, 50, {3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20}},
    {3, E::MaximumLife, "Maximum life", 3, 40, {0, 0, 0, 7, 9, 11, 13, 15, 17, 19, 21, 23}},
    {4, E::LifeRecovery, "Life recovery", 6, 30, {0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6}},
    {5, E::ManaRecovery, "Mana recovery", 9, 20, {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3}},
    {7, E::DamageTaken, "Damage taken", 9, 20, {0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5}, true},
};

struct Table {
    const HarmonyOption* first;
    int count;
};

Table tableOf(HarmonyFamily family) {
    switch (family) {
        case HarmonyFamily::Weapon: return {kWeapon, int(std::size(kWeapon))};
        case HarmonyFamily::Staff: return {kStaff, int(std::size(kStaff))};
        case HarmonyFamily::Defence: return {kDefence, int(std::size(kDefence))};
        case HarmonyFamily::None: break;
    }
    return {nullptr, 0};
}

// An option that would do nothing on this item: a requirement cut where it asks none.
bool idle(const HarmonyOption& option, const content::ItemRow& row) {
    return (option.effect == E::StrengthAsked && row.needStrength == 0) ||
           (option.effect == E::AgilityAsked && row.needAgility == 0);
}

}  // namespace

HarmonyFamily harmonyFamily(const content::ItemRow& row) {
    // Swords, axes, maces, spears and bows (not their arrows), MU's ITEM_SWORD..ITEM_BOW; the
    // staffs, 5; shields and the five armour groups, 6 to 11 (JoH.cpp:609-640).
    if (row.group >= 0 && row.group <= kGroupBows) {
        return ammunition(row) ? HarmonyFamily::None : HarmonyFamily::Weapon;
    }
    if (row.group == 5) return HarmonyFamily::Staff;
    if (row.group >= kGroupShields && row.group <= kGroupBoots) return HarmonyFamily::Defence;
    return HarmonyFamily::None;
}

const HarmonyOption* harmonyOption(HarmonyFamily family, int number) {
    const Table table = tableOf(family);
    for (int i = 0; i < table.count; ++i) {
        if (table.first[i].number == number) return &table.first[i];
    }
    return nullptr;
}

const HarmonyOption* harmonyOf(const content::ItemRow& row, const Held& what) {
    return what.harmony == 0 ? nullptr : harmonyOption(harmonyFamily(row), what.harmony);
}

bool harmonyActive(const Held& what) {
    return what.harmony != 0 && what.refinement >= what.harmonyLevel;
}

int harmonyValue(const content::ItemRow& row, const Held& what) {
    const HarmonyOption* option = harmonyOf(row, what);
    if (option == nullptr || !harmonyActive(what) || what.harmonyLevel > kHarmonyTop) return 0;
    return harmonyAt(*option, what.harmonyLevel);
}

int harmonyAt(const HarmonyOption& option, int level) {
    const int value = option.value[std::clamp(level, 0, kHarmonyTop)];
    if (option.percent) return value;
    return int(double(value) * (option.effect == HarmonyEffect::Wizardry ? kHarmonyWizardryStrength
                                                                         : kHarmonyStrength));
}

Needs harmonyEased(const content::ItemRow& row, const Held& what, Needs asked) {
    const HarmonyOption* option = harmonyOf(row, what);
    if (option == nullptr) return asked;
    const int cut = harmonyValue(row, what);
    if (option->effect == E::StrengthAsked) asked.strength = std::max(0, asked.strength - cut);
    if (option->effect == E::AgilityAsked) asked.agility = std::max(0, asked.agility - cut);
    return asked;
}

int drawHarmony(const content::ItemRow& row, const Held& what, Random& dice) {
    const Table table = tableOf(harmonyFamily(row));
    int total = 0;
    for (int i = 0; i < table.count; ++i) {
        const HarmonyOption& one = table.first[i];
        if (one.least <= what.refinement && !idle(one, row)) total += one.weight;
    }
    if (total == 0) return 0;
    int at = dice.nextInt(0, total);
    for (int i = 0; i < table.count; ++i) {
        const HarmonyOption& one = table.first[i];
        if (one.least > what.refinement || idle(one, row)) continue;
        if (at < one.weight) return one.number;
        at -= one.weight;
    }
    return 0;
}

bool harmonizes(const content::Tables& tables, const Held& jewel, const Held& target) {
    if (jewel.empty() || target.empty() || size_t(jewel.item) >= tables.items.size() ||
        size_t(target.item) >= tables.items.size()) {
        return false;
    }
    const content::ItemRow& jewelRow = tables.items[size_t(jewel.item)];
    const content::ItemRow& row = tables.items[size_t(target.item)];
    if (jewelOfHarmony(jewelRow)) {
        if (target.harmony != 0) return false;
        const Table table = tableOf(harmonyFamily(row));
        for (int i = 0; i < table.count; ++i) {
            if (table.first[i].least <= target.refinement && !idle(table.first[i], row)) return true;
        }
        return false;
    }
    if (!refiningStone(jewelRow) || !harmonyOf(row, target)) return false;
    return target.harmonyLevel < kHarmonyTop && target.harmonyLevel < target.refinement;
}

bool smeltsCheap(const content::ItemRow& row) {
    static constexpr struct {
        uint8_t group, number;
    } kCheap[] = {
        {0, 0},  // Kris
        {0, 1},  // Short Sword
        {0, 2},  // Rapier
        {0, 4},  // Sword of Assassin
        {1, 0},  // Small Axe
        {1, 1},  // Hand Axe
        {1, 2},  // Double Axe
        {2, 0},  // Mace
        {2, 1},  // Morning Star
        {2, 2},  // Flail
        {3, 1},  // Spear
        {3, 2},  // Dragon Lance
        {3, 3},  // Giant Trident
        {3, 5},  // Double Poleaxe
        {4, 0},  // Short Bow
        {4, 1},  // Bow
        {4, 2},  // Elven Bow
        {4, 3},  // Battle Bow
        {4, 8},  // Crossbow
        {4, 9},  // Golden Crossbow
        {4, 10},  // Arquebus
        {4, 11},  // Light Crossbow
        {5, 0},  // Skull Staff
        {5, 1},  // Angelic Staff
        {5, 2},  // Serpent Staff
        {6, 0},  // Small Shield
        {6, 1},  // Horn Shield
        {6, 2},  // Kite Shield
        {6, 3},  // Elven Shield
        {6, 4},  // Buckler
        {6, 6},  // Skull Shield
        {6, 7},  // Spiked Shield
        {6, 9},  // Plate Shield
        {6, 10},  // Big Round Shield
        {7, 0},  // Bronze Helm
        {7, 2},  // Pad Helm
        {7, 4},  // Bone Helm
        {7, 5},  // Leather Helm
        {7, 6},  // Scale Helm
        {7, 7},  // Sphinx Mask
        {7, 8},  // Brass Helm
        {7, 10},  // Vine Helm
        {7, 11},  // Silk Helm
        {7, 12},  // Wind Helm
        {8, 0},  // Bronze Armor
        {8, 2},  // Pad Armor
        {8, 4},  // Bone Armor
        {8, 5},  // Leather Armor
        {8, 6},  // Scale Armor
        {8, 7},  // Sphinx Armor
        {8, 8},  // Brass Armor
        {8, 10},  // Vine Armor
        {8, 11},  // Silk Armor
        {8, 12},  // Wind Armor
        {9, 0},  // Bronze Pants
        {9, 2},  // Pad Pants
        {9, 4},  // Bone Pants
        {9, 5},  // Leather Pants
        {9, 6},  // Scale Pants
        {9, 7},  // Sphinx Pants
        {9, 8},  // Brass Pants
        {9, 10},  // Vine Pants
        {9, 11},  // Silk Pants
        {9, 12},  // Wind Pants
        {10, 0},  // Bronze Gloves
        {10, 2},  // Pad Gloves
        {10, 4},  // Bone Gloves
        {10, 5},  // Leather Gloves
        {10, 6},  // Scale Gloves
        {10, 7},  // Sphinx Gloves
        {10, 8},  // Brass Gloves
        {10, 10},  // Vine Gloves
        {10, 11},  // Silk Gloves
        {10, 12},  // Wind Gloves
        {11, 0},  // Bronze Boots
        {11, 2},  // Pad Boots
        {11, 4},  // Bone Boots
        {11, 5},  // Leather Boots
        {11, 6},  // Scale Boots
        {11, 7},  // Sphinx Boots
        {11, 8},  // Brass Boots
        {11, 10},  // Vine Boots
        {11, 11},  // Silk Boots
        {11, 12},  // Wind Boots
    };
    for (const auto& one : kCheap) {
        if (row.group == one.group && row.number == one.number) return true;
    }
    return false;
}

bool smeltable(const content::ItemRow& row, const Held& what) {
    return harmonyFamily(row) != HarmonyFamily::None && what.harmony == 0 &&
           (what.refinement >= kSmeltCheapFrom || !smeltsCheap(row));
}

Held harmonyMissed(const content::Tables& tables, const Held& jewel, Held target) {
    if (jewel.empty() || target.empty() || size_t(jewel.item) >= tables.items.size() ||
        size_t(target.item) >= tables.items.size()) {
        return target;
    }
    if (!refiningStone(tables.items[size_t(jewel.item)])) return target;
    if (const HarmonyOption* option = harmonyOf(tables.items[size_t(target.item)], target)) {
        target.harmonyLevel = option->least;
    }
    return target;
}

}  // namespace mu::sim
