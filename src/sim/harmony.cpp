#include "sim/harmony.h"

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
    return option->value[what.harmonyLevel];
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

}  // namespace mu::sim
