#include "sim/machine.h"

#include <algorithm>

#include "sim/market.h"
#include "sim/wear.h"

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

// The three Chaos weapons, Version075's MODEL_MACE+6, MODEL_BOW+6 and MODEL_STAFF+7.
bool chaosWeapon(const content::ItemRow& row) {
    return (row.group == 2 && row.number == 6) || (row.group == kGroupBows && row.number == 6) ||
           (row.group == 5 && row.number == 7);
}

bool anyRune(const Held& what) {
    for (int i = 0; i < what.sockets && i < kMostSockets; ++i) {
        if (what.powers[i] != 0) return true;
    }
    return false;
}

// The box, sorted into what the services ask about.
struct Sorted {
    int chaos = 0, bless = 0, soul = 0;
    int optioned = 0;   // things at +4 or better with the additional option: the Chaos Weapon's
    int chaosWeapons = 0;  // and of them the Chaos weapons, which make it the wings' box
    int chaosWeaponCell = -1;
    int at[2] = {};     // raisable things at +9 and at +10
    int atCell[2] = {-1, -1};
    int runed = 0, runedCell = -1;        // things with a rune set in a socket
    int socketable = 0, socketCell = -1;  // things with room for another socket
    int horns = 0, wornHorns = 0;         // Horns of Uniria at full life, and short of it
    int scrolls = 0, bones = 0;           // the cloak's: Scrolls of Archangel and Blood Bones
    int scrollLevel = 0, boneLevel = 0;   // and the last one's level
    int runes = 0;                        // Runes of Creation carrying a power
    int runeCells[kMostSockets + 1] = {-1, -1, -1, -1};
    int things = 0;     // everything that is not one of the three jewels
    int others = 0;     // everything that is neither a jewel nor a rune
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
            if (creation(*row) && what.powers[0] != 0) {
                if (s.runes < kMostSockets + 1) s.runeCells[s.runes] = cell;
                ++s.runes;
                continue;
            }
            ++s.others;
            if (scrollOfArchangel(*row) || bloodBone(*row)) {
                ++(scrollOfArchangel(*row) ? s.scrolls : s.bones);
                (scrollOfArchangel(*row) ? s.scrollLevel : s.boneLevel) = what.refinement;
                continue;
            }
            if (row->group == kGroupPets && row->number == 2) {
                // `m_Durability == 255`: only a horn at its whole life counts.
                ++(what.durability >= maximumDurability(*row, what) ? s.horns : s.wornHorns);
                continue;
            }
            if (what.refinement >= 4 && what.option > 0) {
                ++s.optioned;
                if (chaosWeapon(*row)) {
                    ++s.chaosWeapons;
                    s.chaosWeaponCell = cell;
                }
            }
            for (int k = 0; k < 2; ++k) {
                if (raisable(*row) && what.refinement == 9 + k) {
                    ++s.at[k];
                    s.atCell[k] = cell;
                }
            }
            if (anyRune(what)) {
                ++s.runed;
                s.runedCell = cell;
            }
            if (takesSockets(*row) && what.sockets < mostSocketsOf(*row)) {
                ++s.socketable;
                s.socketCell = cell;
            }
        }
    }
    return s;
}

// ---- Combine: MU's machine --------------------------------------------------------------------

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
        case Recipe::Wings:
            return s.chaosWeapons >= 1 && s.things == s.optioned && s.chaos >= 1;
        case Recipe::Dinorant:
            return s.horns == kDinorantHorns && s.things == s.horns && s.chaos == 1 &&
                   s.bless == 0 && s.soul == 0;
        case Recipe::Cloak:
            return s.scrolls == 1 && s.bones == 1 && s.things == 2 && s.chaos == 1 &&
                   s.bless == 0 && s.soul == 0 && s.scrollLevel == s.boneLevel &&
                   s.scrollLevel >= 1 && s.scrollLevel <= kCloakMostLevel;
        case Recipe::None:
            break;
    }
    return false;
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
        case Recipe::Wings:
            // Ahead of the Chaos Weapon in kOrder, so a box with a Chaos weapon in it looks
            // like wings at the same points, and one without none at all.
            if (s.things != s.optioned || s.chaosWeapons == 0) return 0;
            points += 10;
            if (s.bless > 0) points += 3;
            if (s.soul > 0) points += 3;
            break;
        case Recipe::Dinorant:
            if (s.things != s.horns + s.wornHorns || s.bless > 0 || s.soul > 0) return 0;
            if (s.horns + s.wornHorns > 0) points += 10;
            break;
        case Recipe::Cloak:
            if (s.things != s.scrolls + s.bones || s.bless > 0 || s.soul > 0) return 0;
            if (s.scrolls > 0) points += 10;
            if (s.bones > 0) points += 5;
            break;
        case Recipe::None:
            return 0;
    }
    if (s.chaos > 0) points += 1;
    return points;
}

// The wings before the Chaos Weapon: a box that is both is WebZen's `MixResult2`, a wing.
constexpr Recipe kOrder[] = {Recipe::PlusTen, Recipe::PlusEleven, Recipe::Dinorant,
                             Recipe::Cloak, Recipe::Wings, Recipe::ChaosWeapon};

void need(Judged& j, std::string name, int have, int want) {
    if (j.needCount >= kMostNeeds) return;
    j.needs[j.needCount].name = std::move(name);
    j.needs[j.needCount].have = have;
    j.needs[j.needCount].need = want;
    ++j.needCount;
}

std::string labelAt(const content::Tables& tables, const Machine& box, int cell) {
    const content::ItemRow* row = rowOf(tables, box[cell]);
    return row ? row->label : std::string();
}

void combine(const content::Tables& tables, const Machine& box, const Sorted& s, Kin kin,
             Judged& j) {
    for (Recipe one : kOrder) {
        if (exactly(one, s)) {
            j.recipe = one;
            break;
        }
    }
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
    j.ready = j.recipe != Recipe::None;
    switch (j.nearest) {
        case Recipe::PlusTen:
        case Recipe::PlusEleven: {
            const int k = j.nearest == Recipe::PlusTen ? 0 : 1;
            need(j, "Item +" + std::to_string(9 + k), s.at[k], 1);
            need(j, "Jewel of Chaos", s.chaos, 1);
            need(j, "Jewel of Bless", s.bless, k + 1);
            need(j, "Jewel of Soul", s.soul, k + 1);
            // WebZen's: the rate, twenty for luck, and the cap.
            j.luck = kPlusLuck;
            j.rate = k == 0 ? 50 : 45;
            if (s.atCell[k] >= 0 && box[s.atCell[k]].luck) {
                j.lucky = true;
                j.rate = std::min(kPlusCap, j.rate + j.luck);
            }
            j.zen = 2000000LL * (k + 1);
            if (s.atCell[k] >= 0) {
                j.target = s.atCell[k];
                const std::string name = labelAt(tables, box, j.target);
                j.success = name + " +" + std::to_string(10 + k);
                j.failure = name + " and the jewels are lost";
            } else {
                j.success = "The item goes up one";
                j.failure = "The item and the jewels are lost";
            }
            break;
        }
        case Recipe::ChaosWeapon:
        case Recipe::Wings: {
            if (j.nearest == Recipe::Wings) {
                need(j, "Chaos weapon +4 with an option", std::min(s.chaosWeapons, 1), 1);
            } else {
                need(j, "Item +4 with an option", s.optioned, 1);
            }
            need(j, "Jewel of Chaos", s.chaos, 1);
            need(j, "Jewel of Bless", s.bless, 0);
            need(j, "Jewel of Soul", s.soul, 0);
            // The old buying price over NpcPriceDivisor, MoneyPerFinalSuccessPercentage a point.
            int64_t worth = 0;
            for (int cell = 0; cell < kMachineCells; ++cell) worth += mixValue(tables, box[cell]);
            j.rate = int(std::min<int64_t>(100, worth / 20000));
            j.zen = 10000LL * j.rate;
            if (j.nearest == Recipe::Wings) {
                j.target = s.chaosWeaponCell;
                j.success = firstWingName(kin);
            } else {
                j.success = "A Chaos weapon, +0 to +4";
            }
            j.failure = "Jewels lost, items a plus lower";
            break;
        }
        case Recipe::Dinorant:
            need(j, "Horn of Uniria, full life", s.horns, kDinorantHorns);
            need(j, "Jewel of Chaos", s.chaos, 1);
            j.rate = kDinorantRate;
            j.zen = kDinorantZen;
            j.success = "Horn of Dinorant";
            j.failure = "The horns and the Chaos are lost";
            break;
        case Recipe::Cloak: {
            need(j, "Scroll of Archangel", s.scrolls, 1);
            need(j, "Blood Bone", s.bones, 1);
            need(j, "Jewel of Chaos", s.chaos, 1);
            j.rate = kCloakRate;
            // The scroll's level sets the price; a box whose two disagree is not ready, and
            // says so where the result would be (WebZen's result 9).
            const int level = s.scrollLevel > 0 ? s.scrollLevel : s.boneLevel;
            j.zen = level >= 1 && level <= kCloakMostLevel ? kCloakZen[level] : 0;
            if (s.scrolls == 1 && s.bones == 1 && s.scrollLevel != s.boneLevel) {
                j.success = "The scroll and the bone must be of one level";
            } else if (level > kCloakMostLevel) {
                j.success = "A +" + std::to_string(level) + " cannot be combined";
            } else {
                j.success = "Invisibility Cloak +" + std::to_string(std::max(1, level));
            }
            j.failure = "The scroll, the bone and the Chaos are lost";
            break;
        }
        case Recipe::None:
            break;
    }
    if (j.ready) j.title = recipeName(j.recipe);
    else if (j.empty) j.title = "Put items in the box";
    else if (j.nearest != Recipe::None) j.title = std::string(recipeName(j.nearest)) + ", not ready";
    else j.title = "Improper items for combination";
}

// ---- the rune services (invention) -----------------------------------------------------------

void removeRune(const content::Tables& tables, const Machine& box, const Sorted& s, int socket,
                Judged& j) {
    need(j, "Item with a rune", s.runed, 1);
    need(j, "Jewel of Chaos", s.chaos, 1);
    j.rate = 100;
    if (s.runed != 1) {
        j.title = s.runed == 0 ? "Put in an item with a rune" : "One item at a time";
        return;
    }
    j.target = s.runedCell;
    const Held& thing = box[j.target];
    if (socket < 0 || socket >= thing.sockets || thing.powers[socket] == 0) {
        socket = -1;
        for (int i = 0; i < thing.sockets && socket < 0; ++i) {
            if (thing.powers[i] != 0) socket = i;
        }
    }
    j.socket = socket;
    const PowerRow* power = socket >= 0 ? powerOf(thing.powers[socket]) : nullptr;
    if (!power) return;
    j.zen = kRemoveRuneZen[int(power->rarity)];
    j.title = std::string("Remove ") + power->name;
    j.success = std::string(power->name) + " back as a rune, the socket empty";
    j.ready = s.things == 1 && s.chaos == 1 && s.bless == 0 && s.soul == 0;
    (void)tables;
}

void addSocket(const content::Tables& tables, const Machine& box, const Sorted& s, Judged& j) {
    need(j, "Item with room for a socket", s.socketable, 1);
    need(j, "Jewel of Chaos", s.chaos, 1);
    need(j, "Jewel of Soul", s.soul, 1);
    j.zen = kAddSocketZen;
    if (s.socketable != 1) {
        j.title = s.socketable == 0 ? "Put in a weapon, armour or shield" : "One item at a time";
        j.rate = kAddSocketRate[0];
        return;
    }
    j.target = s.socketCell;
    const Held& thing = box[j.target];
    const int has = std::clamp<int>(thing.sockets, 0, kMostSockets - 1);
    j.rate = kAddSocketRate[has];
    const std::string name = labelAt(tables, box, j.target);
    j.title = "Socket " + std::to_string(has + 1) + " for the " + name;
    j.success = name + " with " + std::to_string(has + 1) + (has == 0 ? " socket" : " sockets");
    j.failure = "The jewels are lost, the " + name + " is kept";
    j.ready = s.things == 1 && s.chaos == 1 && s.soul == 1 && s.bless == 0;
}

void fuseRunes(const content::Tables& tables, const Machine& box, const Sorted& s, Judged& j) {
    need(j, "Runes of one rarity", s.runes, kFuseCount);
    need(j, "Jewel of Chaos", s.chaos, 1);
    j.rate = 100;
    j.zen = kFuseZen;
    int rarity = -1;
    bool same = true;
    for (int i = 0; i < s.runes && i < kMostSockets + 1; ++i) {
        const PowerRow* power = powerOf(box[s.runeCells[i]].powers[0]);
        const int r = power ? int(power->rarity) : -1;
        if (rarity < 0) rarity = r;
        else if (r != rarity) same = false;
    }
    if (s.runes == 0) {
        j.title = "Put in three runes";
        return;
    }
    if (!same) {
        j.title = "The runes must share a rarity";
        return;
    }
    if (rarity >= int(Rarity::Legendary)) {
        j.title = "Legendary runes fuse no higher";
        return;
    }
    const char* next = rarityName(Rarity(rarity + 1));
    j.title = std::string("Fuse into ") + next;
    j.success = std::string("A random ") + next + " rune";
    j.ready = s.runes == kFuseCount && s.others == 0 && s.chaos == 1 && s.bless == 0 &&
              s.soul == 0;
    (void)tables;
}

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

Judged judge(const content::Tables& tables, const Machine& box, Service service, int socket,
             Kin kin) {
    Judged j;
    j.service = service;
    const Sorted s = sort(tables, box);
    j.empty = !s.any;
    switch (service) {
        case Service::Combine: combine(tables, box, s, kin, j); break;
        case Service::RemoveRune: removeRune(tables, box, s, socket, j); break;
        case Service::AddSocket: addSocket(tables, box, s, j); break;
        case Service::FuseRunes: fuseRunes(tables, box, s, j); break;
    }
    return j;
}

int firstWingOf(Kin kin) {
    switch (kin) {
        case Kin::DarkWizard: return 1;
        case Kin::FairyElf: return 0;
        case Kin::DarkKnight: return 2;
    }
    return 2;
}

const char* firstWingName(Kin kin) {
    switch (kin) {
        case Kin::DarkWizard: return "Wings of Heaven";
        case Kin::FairyElf: return "Wings of Elf";
        case Kin::DarkKnight: return "Wings of Satan";
    }
    return "";
}

const char* recipeName(Recipe recipe) {
    switch (recipe) {
        case Recipe::ChaosWeapon: return "Chaos Weapon";
        case Recipe::PlusTen: return "+10 Item";
        case Recipe::PlusEleven: return "+11 Item";
        case Recipe::Dinorant: return "Dinorant";
        case Recipe::Cloak: return "Invisibility Cloak";
        case Recipe::Wings: return "1st Level Wings";
        case Recipe::None: break;
    }
    return "";
}

const char* serviceName(Service service) {
    switch (service) {
        case Service::Combine: return "Combine";
        case Service::RemoveRune: return "Remove Rune";
        case Service::AddSocket: return "Add Socket";
        case Service::FuseRunes: return "Fuse Runes";
    }
    return "";
}

const char* serviceVerb(Service service) {
    switch (service) {
        case Service::Combine: return "Combine";
        case Service::RemoveRune: return "Remove";
        case Service::AddSocket: return "Add Socket";
        case Service::FuseRunes: return "Fuse";
    }
    return "";
}

}  // namespace mu::sim
