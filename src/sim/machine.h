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
//   * **Dinorant**, below (WebZen's PegasiaChaosMix).
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
enum class Recipe : int8_t { None = -1, ChaosWeapon = 1, PlusTen = 3, PlusEleven = 4, Dinorant = 5 };

// **The services** (the user, 2026-10-02, docs/chaos-machine.md "Phase two"): the Goblin's box
// read four ways, picked on the window's service row. Combine is MU's machine; the other three
// work the Runes of Creation and their sockets and are invention, all of them.
//
//   * **Remove Rune**: one thing with a rune set, one Chaos. The picked socket's rune comes back
//     as a Rune of Creation and the socket is empty again; 100%, Zen by the rune's rarity.
//   * **Add Socket**: one thing that takes sockets and has room for another, one Chaos, one
//     Soul. 50%, 35%, 20% for the first, second and third; failure takes the jewels only.
//   * **Fuse Runes**: three runes of one rarity below Legendary and one Chaos make one random
//     rune of the next rarity, one his class may set; 100%.
enum class Service : uint8_t { Combine = 0, RemoveRune = 1, AddSocket = 2, FuseRunes = 3 };
constexpr int kServices = 4;
const char* serviceName(Service service);  // the row: "Combine", "Remove Rune", ...
const char* serviceVerb(Service service);  // the button: "Combine", "Remove", "Add Socket", "Fuse"

constexpr int64_t kRemoveRuneZen[3] = {500000, 1000000, 1500000};  // Rare, Epic, Legendary
constexpr int kAddSocketRate[3] = {50, 35, 20};                     // for the 1st, 2nd, 3rd
constexpr int64_t kAddSocketZen = 1000000;
constexpr int64_t kFuseZen = 500000;
constexpr int kFuseCount = 3;

// One line of what a service takes: how many are in the box and how many it wants. A `need` of
// 0 is any number, each one raising the rate (the Chaos Weapon's Bless and Soul).
struct Need {
    std::string name;
    int have = 0, need = 0;
    bool met() const { return need == 0 ? true : have == need; }
};
constexpr int kMostNeeds = 4;

struct Judged {
    Service service = Service::Combine;
    Recipe recipe = Recipe::None;   // Combine: what the box makes as it stands
    Recipe nearest = Recipe::None;  // Combine: what it looks most like
    bool ready = false;             // the button would run it
    bool empty = true;
    int rate = 0;                   // percent, 0..100, luck counted
    int luck = 0;                   // the share of `rate` luck gives, or would give: the meter's
    bool lucky = false;             // whether the thing is lucky, so `luck` is in `rate`
    int64_t zen = 0;
    int target = -1;                // the thing worked on, as a cell
    int socket = -1;                // Remove Rune: the socket it empties
    std::string title;              // "+10 Item", "Remove Stormcall", "Improper items ..."
    Need needs[kMostNeeds];
    int needCount = 0;
    std::string success, failure;   // what happens, in words; failure empty when it cannot fail
};

// `socket` is Remove Rune's pick, -1 for the first with a rune; `kin` is whose class a fused rune
// is drawn for.
Judged judge(const content::Tables& tables, const Machine& box,
             Service service = Service::Combine, int socket = -1, Kin kin = Kin::DarkKnight);

// The Dinorant (WebZen 1.00.93 MixSystem.cpp:2141-2290, PegasiaChaosMix under NEW_FORSKYLAND2,
// as docs/mount.md takes it): ten Horns of Uniria at their full 255 life and one Chaos, 70%,
// 500,000 Zen; a failure takes the box. Its three options (30%, then one in five a second) are
// not rolled: the Dinorant's options are not built (docs/mount.md).
constexpr int kDinorantHorns = 10;
constexpr int kDinorantRate = 70;
constexpr int64_t kDinorantZen = 500000;
// +10 and +11 as WebZen's base 0.97d branch has them (MixSystem.cpp:1380-1890): 50% and 45%,
// twenty more for a lucky thing (`m_Option2 != 0`, `+= 20`), and never above 75
// (m_iMaxCombinationRate). OpenMU's 0.95d gives luck 25 and no cap; WebZen outranks it here.
constexpr int kPlusLuck = 20;
constexpr int kPlusCap = 75;

// "Chaos Weapon", "+10 Item", "+11 Item", "Dinorant": MuMain's recipe names.
const char* recipeName(Recipe recipe);

// What a thing is worth to the machine: MixMgr's EvaluateMixItemValue, OpenMU's
// CalculateFinalOldBuyingPrice -- the three jewels at their old prices, the rest at what a
// merchant would charge.
int64_t mixValue(const content::Tables& tables, const Held& what);

// The jewels, by row.
bool jewelOfChaos(const content::ItemRow& row);
bool jewelOfBless(const content::ItemRow& row);
bool jewelOfSoul(const content::ItemRow& row);

}  // namespace mu::sim
