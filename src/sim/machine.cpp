#include "sim/machine.h"

#include <algorithm>

#include "sim/market.h"

namespace mu::sim {
namespace {

const Held kNothing{};

const content::ItemRow* rowOf(const content::Tables& tables, const Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return nullptr;
    return &tables.items[size_t(what.item)];
}

// How many a held thing counts for: a stack's pieces, else one.
int piecesOf(const content::ItemRow& row, const Held& what) {
    return stacks(row) ? std::max<int>(1, what.durability) : 1;
}

// What OpenMU's ItemLevelUpgradeCrafting takes as its one thing, which names no item: here the
// groups a plus goes on at all, the weapons and the armour to the boots, less the ammunition.
bool raisable(const content::ItemRow& row) {
    return row.group <= kGroupBoots && !ammunition(row);
}

// The box, sorted into what the recipes ask about.
struct Sorted {
    int chaos = 0, bless = 0, soul = 0;
    int optioned = 0;   // things at +4 or better with the additional option: the Chaos Weapon's
    int at[2] = {};     // raisable things at +9 and at +10
    int atCell[2] = {-1, -1};
    int things = 0;     // everything that is not one of the three jewels
    bool any = false;
};

Sorted sort(const content::Tables& tables, const Machine& box) {
    Sorted s;
    for (int cell = 0; cell < kMachineCells; ++cell) {
        const content::ItemRow* row = rowOf(tables, box[cell]);
        if (!row) continue;
        const Held& what = box[cell];
        s.any = true;
        const int n = piecesOf(*row, what);
        if (jewelOfChaos(*row)) {
            s.chaos += n;
        } else if (jewelOfBless(*row)) {
            s.bless += n;
        } else if (jewelOfSoul(*row)) {
            s.soul += n;
        } else {
            ++s.things;
            if (what.refinement >= 4 && what.option > 0) ++s.optioned;
            for (int k = 0; k < 2; ++k) {
                if (raisable(*row) && what.refinement == 9 + k) {
                    ++s.at[k];
                    s.atCell[k] = cell;
                }
            }
        }
    }
    return s;
}

// Whether the box is exactly this recipe, MixMgr's CheckRecipeSub: every line met and nothing
// left over.
bool exactly(Recipe recipe, const Sorted& s) {
    switch (recipe) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = recipe == Recipe::PlusTen ? 0 : 1;
            const int jewels = k + 1;
            return s.things == 1 && s.at[k] == 1 && s.chaos == 1 && s.bless == jewels &&
                   s.soul == jewels;
        }
        case Recipe::ChaosWeapon:
            return s.optioned >= 1 && s.things == s.optioned && s.chaos >= 1;
        case Recipe::None:
            break;
    }
    return false;
}

// How each of a recipe's lines is met by the box. The lines are sourceLine's, in its order.
int meet(Recipe recipe, const Sorted& s, Met* met) {
    const auto count = [](int have, int least, int most) {
        if (least == 0) return have > 0 ? Met::Yes : Met::Partly;
        if (have >= least && have <= most) return Met::Yes;
        return have > 0 ? Met::Partly : Met::No;
    };
    switch (recipe) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = recipe == Recipe::PlusTen ? 0 : 1;
            met[0] = count(s.at[k], 1, 1);
            met[1] = count(s.chaos, 1, 1);
            met[2] = count(s.bless, k + 1, k + 1);
            met[3] = count(s.soul, k + 1, k + 1);
            return 4;
        }
        case Recipe::ChaosWeapon:
            met[0] = count(s.optioned, 1, 99);
            met[1] = count(s.chaos, 1, 99);
            met[2] = count(s.bless, 0, 99);
            met[3] = count(s.soul, 0, 99);
            return 4;
        case Recipe::None:
            break;
    }
    return 0;
}

// MixMgr's CheckRecipeSimilaritySub, in its weights: ten for a thing on the first line, five
// on the second, three after, and one for a Chaos, which every recipe takes. A box holding
// something the recipe has no line for is not like it at all.
int likeness(Recipe recipe, const Sorted& s) {
    int points = 0;
    switch (recipe) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = recipe == Recipe::PlusTen ? 0 : 1;
            if (s.things != s.at[k]) return 0;
            if (s.at[k] > 0) points += 10;
            if (s.bless > 0) points += 3;
            if (s.soul > 0) points += 3;
            break;
        }
        case Recipe::ChaosWeapon:
            if (s.things != s.optioned) return 0;
            if (s.optioned > 0) points += 10;
            if (s.bless > 0) points += 3;
            if (s.soul > 0) points += 3;
            break;
        case Recipe::None:
            return 0;
    }
    if (s.chaos > 0) points += 1;
    return points;
}

constexpr Recipe kOrder[] = {Recipe::PlusTen, Recipe::PlusEleven, Recipe::ChaosWeapon};

}  // namespace

const Held& Machine::operator[](int cell) const {
    return cell >= 0 && cell < kMachineCells ? cells_[cell] : kNothing;
}

void Machine::put(int cell, const Held& what) {
    if (cell < 0 || cell >= kMachineCells) return;
    cells_[cell] = what;
    ++version_;
}

Held Machine::lift(int cell) {
    if (cell < 0 || cell >= kMachineCells || cells_[cell].empty()) return Held{};
    const Held was = cells_[cell];
    cells_[cell] = Held{};
    ++version_;
    return was;
}

void Machine::clear() {
    for (Held& one : cells_) one = Held{};
    ++version_;
}

bool Machine::empty() const {
    for (const Held& one : cells_) {
        if (!one.empty()) return false;
    }
    return true;
}

int Machine::holder(const content::Tables& tables, int cell) const {
    if (cell < 0 || cell >= kMachineCells) return -1;
    const int cc = cell % kMachineColumns, cr = cell / kMachineColumns;
    for (int at = 0; at < kMachineCells; ++at) {
        const content::ItemRow* row = rowOf(tables, cells_[at]);
        if (!row) continue;
        const int c = at % kMachineColumns, r = at / kMachineColumns;
        if (cc >= c && cc < c + row->width && cr >= r && cr < r + row->height) return at;
    }
    return -1;
}

bool Machine::room(const content::Tables& tables, int cell, int width, int height,
                   int ignoring) const {
    if (cell < 0 || cell >= kMachineCells) return false;
    const int column = cell % kMachineColumns, row = cell / kMachineColumns;
    width = std::max(1, width);
    height = std::max(1, height);
    if (column + width > kMachineColumns || row + height > kMachineRows) return false;
    for (int down = 0; down < height; ++down) {
        for (int across = 0; across < width; ++across) {
            const int held = holder(tables, (row + down) * kMachineColumns + column + across);
            if (held >= 0 && held != ignoring) return false;
        }
    }
    return true;
}

int Machine::free(const content::Tables& tables, int width, int height) const {
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (room(tables, cell, width, height)) return cell;
    }
    return -1;
}

bool jewelOfChaos(const content::ItemRow& row) { return row.group == 12 && row.number == 15; }
bool jewelOfBless(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 13;
}
bool jewelOfSoul(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 14;
}

int64_t mixValue(const content::Tables& tables, const Held& what) {
    const content::ItemRow* row = rowOf(tables, what);
    if (!row) return 0;
    const int n = piecesOf(*row, what);
    // SpecialItemOldValueDictionary, and MixMgr's own three.
    if (jewelOfBless(*row)) return 100000LL * n;
    if (jewelOfSoul(*row)) return 70000LL * n;
    if (jewelOfChaos(*row)) return 40000LL * n;
    return buyingPrice(*row, what.refinement, n, what.skill, what.durability, row->durability,
                       what.luck, what.option, excellentCount(what.excellent));
}

Judged judge(const content::Tables& tables, const Machine& box) {
    Judged j;
    const Sorted s = sort(tables, box);
    j.empty = !s.any;
    for (Recipe one : kOrder) {
        if (exactly(one, s)) {
            j.recipe = one;
            break;
        }
    }
    // The nearest: the recipe matched, else the likest, the first of a tie.
    if (j.recipe != Recipe::None) {
        j.nearest = j.recipe;
    } else {
        int best = 0;
        for (Recipe one : kOrder) {
            const int points = likeness(one, s);
            if (points > best) {
                best = points;
                j.nearest = one;
            }
        }
    }
    if (j.nearest != Recipe::None) j.sources = meet(j.nearest, s, j.met);

    switch (j.recipe) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = j.recipe == Recipe::PlusTen ? 0 : 1;
            j.target = s.atCell[k];
            // SuccessPercent, and SuccessPercentageAdditionForLuck for each lucky thing.
            j.rate = k == 0 ? 50 : 45;
            if (box[j.target].luck) j.rate += 25;
            j.rate = std::min(j.rate, 100);
            j.zen = 2000000LL * (k + 1);
            break;
        }
        case Recipe::ChaosWeapon: {
            int64_t worth = 0;
            for (int cell = 0; cell < kMachineCells; ++cell) worth += mixValue(tables, box[cell]);
            j.rate = int(std::min<int64_t>(100, worth / 20000));
            j.zen = 10000LL * j.rate;
            break;
        }
        case Recipe::None:
            break;
    }
    return j;
}

const char* recipeName(Recipe recipe) {
    switch (recipe) {
        case Recipe::ChaosWeapon: return "Chaos Weapon";
        case Recipe::PlusTen: return "+10 Item";
        case Recipe::PlusEleven: return "+11 Item";
        case Recipe::None: break;
    }
    return "";
}

int sourceCount(Recipe recipe) { return recipe == Recipe::None ? 0 : 4; }

std::string sourceLine(Recipe recipe, int line) {
    // MixMgr's GetSourceName: the name, the plus, the count, and "(rate increase)" for a line
    // any number of which may go in.
    switch (recipe) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = recipe == Recipe::PlusTen ? 0 : 1;
            switch (line) {
                case 0: return "Equipment item +" + std::to_string(9 + k) + "  x1";
                case 1: return "Jewel of Chaos  x1";
                case 2: return "Jewel of Bless  x" + std::to_string(k + 1);
                case 3: return "Jewel of Soul  x" + std::to_string(k + 1);
            }
            break;
        }
        case Recipe::ChaosWeapon:
            switch (line) {
                case 0: return "Item +4 or more with an option  x1+";
                case 1: return "Jewel of Chaos  x1+";
                case 2: return "Jewel of Bless (rate increase)";
                case 3: return "Jewel of Soul (rate increase)";
            }
            break;
        case Recipe::None:
            break;
    }
    return "";
}

}  // namespace mu::sim
