// The Chaos Machine: the Chaos Goblin's box, and what it makes of what is put in it.
//
// MU's `CNewUIMixInventory` holds a `CNewUIInventoryCtrl` of 8 x 4 (`Create(STORAGE_TYPE::
// CHAOS_MIX, ..., 8, 4)`), opened by talking to the Chaos Goblin, MU's NPC 238 in Noria
// (OpenMU's NpcWindow.ChaosMachine). A cell here is `row * 8 + column`, from 0 to 31, with an
// item recorded once at the top-left of the rectangle it covers, as in the bag and the vault.
//
// **What it makes** (docs/chaos-machine.md), in the order a box is matched against them:
//
//   * **+10 and +11**, OpenMU's Version095d `ItemLevelUpgradeCrafting`: one thing at +9 (+10),
//     one Chaos, one Bless and one Soul (two of each for +11), and nothing else. 50% (45%), a
//     quarter more for a lucky thing; 2,000,000 Zen (4,000,000). Success puts the thing up one;
//     failure takes it, and the jewels go either way. Not 0.75 -- Version075 has no +10 -- and
//     taken because kRefineCap is eleven only once the machine makes the last two rungs (the
//     user, 2026-09-11: "max lvl currently will be +9, because we need chaos machine").
//   * **Chaos Weapon**, Version075's one combination (ChaosMixes.cs, fixed by
//     FixChaosMixesPlugInBase): one or more things at +4 or better carrying the additional
//     option, one or more Chaos, any Bless and Soul, and nothing else. The rate is the box's
//     old buying price over 20,000 to at most 100 (MixMgr's ItemValue sum and OpenMU's
//     NpcPriceDivisor), the price 10,000 Zen a percent. Success makes a Chaos Dragon Axe,
//     Nature Bow or Lightning Staff at +0..+4, with its own luck and option rolls
//     (ChaosWeaponAndFirstWingsCrafting); failure takes the jewels and drops each thing to a
//     lower plus at random, its option a level down half the time.
//
// Which of the two a box is when it is both -- a +9 thing with an option and one of each
// jewel -- MuMain settles by mix.bmd's order, which this tree does not have. Here the narrower
// recipe wins: a box laid out exactly for +10 is meant for +10. Ours.
//
// The Chaos Weapon's +S skill roll is not taken: skills are orbs here (the user, 2026-09-27).
#pragma once

#include <cstdint>
#include <string>

#include "content/tables.h"
#include "sim/items.h"

namespace mu::sim {

enum : int {
    kMachineColumns = 8,
    kMachineRows = 4,
    kMachineCells = kMachineColumns * kMachineRows,  // 32
};

// The Chaos Goblin, MU's NPC 238.
constexpr int kChaosGoblin = 238;

// The highest plus the machine raises a thing to, Version075's MaximumItemLevel. The jewels and
// the drops stop at nine on their own (kRefineCap); only the machine makes ten and eleven.
constexpr int kMachineCap = 11;

class Machine {
public:
    const Held& operator[](int cell) const;
    uint32_t version() const { return version_; }

    void put(int cell, const Held& what);
    Held lift(int cell);
    void clear();
    bool empty() const;

    // The vault's walk, on the machine's eight by four.
    int holder(const content::Tables& tables, int cell) const;
    bool room(const content::Tables& tables, int cell, int width, int height,
              int ignoring = -1) const;
    int free(const content::Tables& tables, int width, int height) const;

private:
    Held cells_[kMachineCells];
    uint32_t version_ = 0;
};

// MuMain's ChaosMachineMixType numbers, which are OpenMU's ItemCrafting.Number.
enum class Recipe : int8_t { None = -1, ChaosWeapon = 1, PlusTen = 3, PlusEleven = 4 };

// How far one of a recipe's source lines is met, as MixMgr's GetSourceName colours it: red
// for missing, light blue for the optional or the short, yellow for done.
enum class Met : uint8_t { No, Partly, Yes };

constexpr int kMostSources = 4;

struct Judged {
    Recipe recipe = Recipe::None;   // what the box makes as it stands
    Recipe nearest = Recipe::None;  // what it looks most like, for the window's prediction
    int rate = 0;                   // percent, 0..100
    int64_t zen = 0;                // what the Goblin charges
    int target = -1;                // the +10/+11's thing, as a cell
    int sources = 0;                // `nearest`'s lines
    Met met[kMostSources] = {};
    bool empty = true;
};

Judged judge(const content::Tables& tables, const Machine& box);

// "Chaos Weapon", "+10 Item", "+11 Item": MuMain's recipe names.
const char* recipeName(Recipe recipe);
// A recipe's source lines as MixMgr prints them: "Jewel of Chaos 1", "Item +9 1" and on.
int sourceCount(Recipe recipe);
std::string sourceLine(Recipe recipe, int line);

// What a thing is worth to the machine: MixMgr's EvaluateMixItemValue, OpenMU's
// CalculateFinalOldBuyingPrice -- the three jewels at their old prices, the rest at what a
// merchant would charge.
int64_t mixValue(const content::Tables& tables, const Held& what);

// The jewels, by row.
bool jewelOfChaos(const content::ItemRow& row);
bool jewelOfBless(const content::ItemRow& row);
bool jewelOfSoul(const content::ItemRow& row);

}  // namespace mu::sim
