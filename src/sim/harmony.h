#pragma once

// The Jewel of Harmony's options (docs/plans/jewel-of-harmony.md): MU's three tables, the weapon's,
// the staff's and the defence's, as WebZen's JewelOfHarmonySystem.cpp uses them and MuMain's
// JewelOfHarmonyOption_eng.bmd prints them (OpenMU's HarmonyOptions.cs has the same numbers), cut
// as the user decided on 2026-10-25: levels 0 to 11 only, since our plus stops at kMachineCap, and
// none of the options that act only between players -- the PvP attack and defence rates, the SD
// decrement, bypass and ratio (no player strikes a player here; monsters carry no SD) -- nor Max AG,
// which this game does not have. An option keeps MU's number in its table, so a kept item says
// which it is the way MU's would.

#include <cstdint>

#include "content/tables.h"
#include "sim/items.h"
#include "sim/random.h"

namespace mu::sim {

// Which table an item draws from; None for whatever takes no jewel (wings, jewellery, pets,
// ammunition, everything in groups 12 to 15).
enum class HarmonyFamily : uint8_t { None, Weapon, Staff, Defence };

// What an option does, read by Realm::rearm and the card (steps 3 and 5 of the plan).
enum class HarmonyEffect : uint8_t {
    None,
    MinimumDamage,     // weapon 1: added to the minimum of both hands
    MaximumDamage,     // weapon 2: to the maximum
    StrengthAsked,     // weapon and staff 3 / 2: taken off the item's strength requirement
    AgilityAsked,      // weapon and staff 4 / 3: off its agility requirement
    Damage,            // weapon 5: to both the minimum and the maximum
    CriticalDamage,    // weapon 6, staff 5: a flat add on a critical hit
    SkillDamage,       // weapon 7, staff 4: a flat add to a skill's damage
    Wizardry,          // staff 1: to wizardry's minimum and maximum
    Defense,           // defence 1: MU's server applies half (JoH.cpp:1163); so do we
    MaximumLife,       // defence 3
    LifeRecovery,      // defence 4: a flat amount each recovery tick
    ManaRecovery,      // defence 5: the same, of mana
    DamageTaken,       // defence 7: a percent less of every blow taken
};

// Option levels 0 to kHarmonyTop: ours stop at the plus's own top.
inline constexpr int kHarmonyTop = 11;

struct HarmonyOption {
    uint8_t number = 0;       // MU's, in its table
    HarmonyEffect effect = HarmonyEffect::None;
    const char* name = "";
    uint8_t least = 0;        // the item +level it asks to roll, and the level it starts at
    uint8_t weight = 0;       // OpenMU's (AddHarmonyOptionWeightsUpdateSeason6.cs)
    int16_t value[kHarmonyTop + 1] = {};  // by option level; 0 below `least`
    bool percent = false;     // DamageTaken's value is a percent
};

HarmonyFamily harmonyFamily(const content::ItemRow& row);
// The option `number` of `family`, or nullptr.
const HarmonyOption* harmonyOption(HarmonyFamily family, int number);
// The option `what` carries, or nullptr for none (or an unknown number, which a later table
// may have dropped).
const HarmonyOption* harmonyOf(const content::ItemRow& row, const Held& what);
// Whether its option works: MU's rule, the item's plus at least the option's level
// (JoH.cpp:473-476). A failed Jewel of Soul that drops the plus greys it out until it is raised.
bool harmonyActive(const Held& what);
// Its value now: the option's at its level, or 0 when it is none or inactive.
int harmonyValue(const content::ItemRow& row, const Held& what);
// The option a Jewel of Harmony lays on `what`: a weighted draw among its family's options whose
// least is at most its plus (JoH.cpp:528-579), the requirement cuts left out of an item that asks
// nothing of that stat (WebZen meant to and never did, JoH.cpp:555). 0 when none can roll. The
// level it starts at is the option's least.
int drawHarmony(const content::ItemRow& row, const Held& what, Random& dice);

}  // namespace mu::sim
