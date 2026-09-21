#include "sim/items.h"

#include <algorithm>

namespace mu::sim {
namespace {

// Version075's bonus tables, digit for digit (Rows.cs Refine carries the provenance):
// Weapons.DamageIncreaseByLevel, ArmorInitializerBase.DefenseIncreaseByLevel and its
// ShieldDefenseIncreaseByLevel, "always 1 per item level".
constexpr int kWeaponDamage[] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 31, 36};
constexpr int kArmourDefense[] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 31, 36, 42, 49, 57, 66};
constexpr int kShieldDefense[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

template <size_t N>
int at(const int (&table)[N], int refinement) {
    return table[std::clamp(refinement, 0, int(N) - 1)];
}

// Out of range reads as empty rather than throwing: a slot number is the one field here that
// will one day come off a save file.
const Held kNothing{};

// What stands in the way of a move: -1 nothing, a slot, or kBlocked where two different
// things or the edge of the grid are in the way. Beast.Blocking.
constexpr int kBlocked = -2;

int blocking(const content::Tables& tables, const Satchel& bag, int from, int to) {
    const Held& what = bag[from];
    if (what.empty()) return kBlocked;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    int cells[kSlots];
    const int count = bag.covered(to, row.width, row.height, cells);
    if (count == 0) return kBlocked;
    int found = -1;
    for (int i = 0; i < count; ++i) {
        const int holder = bag.holder(tables, cells[i]);
        if (holder < 0 || holder == from) continue;
        if (found >= 0 && found != holder) return kBlocked;
        found = holder;
    }
    return found;
}

const content::ItemRow* rowOf(const content::Tables& tables, const Held& what) {
    return what.empty() || size_t(what.item) >= tables.items.size()
               ? nullptr
               : &tables.items[size_t(what.item)];
}

}  // namespace

Needs asks(const content::ItemRow& row, int refinement) {
    const int scaled = row.dropLevel + 3 * refinement;
    const auto ask = [scaled](int raw, int multiplier) {
        return raw <= 0 ? 0 : multiplier * scaled * raw / 100 + 20;
    };
    Needs n;
    n.level = row.needLevel;
    n.strength = ask(row.needStrength, 3);
    n.agility = ask(row.needAgility, 3);
    n.energy = ask(row.needEnergy, 4);
    n.vitality = ask(row.needVitality, 3);
    return n;
}

Needs shortOf(const Needs& asked, int level, const HeroPoints& points) {
    Needs n;
    n.level = std::max(0, asked.level - level);
    n.strength = std::max(0, asked.strength - points.strength);
    n.agility = std::max(0, asked.agility - points.agility);
    n.energy = std::max(0, asked.energy - points.energy);
    n.vitality = std::max(0, asked.vitality - points.vitality);
    return n;
}

int damageBonus(int refinement) { return at(kWeaponDamage, refinement); }
int defenseBonus(bool shield, int refinement) {
    return shield ? at(kShieldDefense, refinement) : at(kArmourDefense, refinement);
}

bool ammunition(const content::ItemRow& row) {
    return row.group == kGroupBows && (row.number == 7 || row.number == 15);
}

int placeOf(const content::ItemRow& row) {
    if (row.group < kGroupShields) {
        // A bow is Weapon[1] and a crossbow Weapon[0] (GetEquipedBowType); the bolt goes in
        // the left hand beside a crossbow, the arrows in the right beside a bow -- OpenMU's
        // CreateAmmunition slot column. Everything swung goes in the right hand. Satchel.Hand.
        if (row.group == kGroupBows && (row.number <= 6 || row.number == 7)) return kWeaponLeft;
        return kWeaponRight;
    }
    if (row.group >= kGroupShields && row.group <= kGroupBoots) return row.group - 5;
    return -1;
}

bool heals(const content::ItemRow& row) { return row.group == kGroupPotions && row.number <= 3; }
bool restores(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number >= 4 && row.number <= 6;
}

// ---- the satchel ------------------------------------------------------------------------------

const Held& Satchel::operator[](int slot) const {
    return slot >= 0 && slot < kSlots ? slots_[slot] : kNothing;
}

void Satchel::put(int slot, const Held& what) {
    if (slot < 0 || slot >= kSlots) return;
    slots_[slot] = what;
    ++version_;
}

Held Satchel::lift(int slot) {
    if (slot < 0 || slot >= kSlots || slots_[slot].empty()) return Held{};
    const Held was = slots_[slot];
    slots_[slot] = Held{};
    ++version_;
    return was;
}

void Satchel::clear() {
    for (Held& one : slots_) one = Held{};
    ++version_;
}

int Satchel::covered(int slot, int width, int height, int* out) const {
    if (wearable(slot)) {
        out[0] = slot;
        return 1;
    }
    if (!baggable(slot)) return 0;
    const int column = (slot - kWorn) % kBagColumns, row = (slot - kWorn) / kBagColumns;
    width = std::max(1, width);
    height = std::max(1, height);
    // Off the grid entirely is a refusal, not a clipped item: a three-tall sword cannot start
    // in the last row, and InsertItem makes the same test before it writes anything.
    if (column + width > kBagColumns || row + height > kBagRows) return 0;
    int n = 0;
    for (int down = 0; down < height; ++down) {
        for (int across = 0; across < width; ++across) {
            out[n++] = kWorn + (row + down) * kBagColumns + column + across;
        }
    }
    return n;
}

int Satchel::holder(const content::Tables& tables, int cell) const {
    if (wearable(cell)) return slots_[cell].empty() ? -1 : cell;
    if (!baggable(cell)) return -1;
    const int cc = (cell - kWorn) % kBagColumns, cr = (cell - kWorn) / kBagColumns;
    for (int slot = kWorn; slot < kSlots; ++slot) {
        const content::ItemRow* row = rowOf(tables, slots_[slot]);
        if (!row) continue;
        const int c = (slot - kWorn) % kBagColumns, r = (slot - kWorn) / kBagColumns;
        if (cc >= c && cc < c + row->width && cr >= r && cr < r + row->height) return slot;
    }
    return -1;
}

bool Satchel::room(const content::Tables& tables, int slot, int width, int height,
                   int ignoring) const {
    int cells[kSlots];
    const int count = covered(slot, width, height, cells);
    if (count == 0) return false;
    for (int i = 0; i < count; ++i) {
        const int held = holder(tables, cells[i]);
        if (held >= 0 && held != ignoring) return false;
    }
    return true;
}

int Satchel::free(const content::Tables& tables, int width, int height) const {
    for (int slot = kWorn; slot < kSlots; ++slot) {
        if (room(tables, slot, width, height)) return slot;
    }
    return -1;
}

// ---- the gates --------------------------------------------------------------------------------

bool fits(const content::Tables& tables, const Wearer& who, const Held& what) {
    const content::ItemRow* row = rowOf(tables, what);
    if (!row || placeOf(*row) < 0) return false;
    // mu.db's class enumeration, bit 0 wizard, bit 1 elf, bit 2 knight; none named is anybody.
    if (row->classes != 0 && (row->classes & (1 << int(who.kin))) == 0) return false;
    return shortOf(asks(*row, what.refinement), who.level, who.points).none();
}

bool handful(const content::Tables& tables, const Satchel& bag, const Held& what, int hand) {
    if (hand != kWeaponRight && hand != kWeaponLeft) return false;
    const Held& other = bag[hand == kWeaponRight ? kWeaponLeft : kWeaponRight];
    const content::ItemRow* mine = rowOf(tables, what);
    const content::ItemRow* theirs = rowOf(tables, other);
    if (!mine || !theirs || ammunition(*mine) || ammunition(*theirs)) return false;
    return mine->twoHanded() || theirs->twoHanded();
}

bool movable(const content::Tables& tables, const Wearer& who, const Satchel& bag, int from,
             int to) {
    if (from == to || from < 0 || from >= kSlots || to < 0 || to >= kSlots) return false;
    const Held& what = bag[from];
    const content::ItemRow* row = rowOf(tables, what);
    if (!row) return false;

    if (wearable(to)) {
        if (!baggable(from) || placeOf(*row) != to || !fits(tables, who, what) ||
            handful(tables, bag, what, to)) {
            return false;
        }
        // What was worn comes down to where this came from, so it has to fit there. MU2's
        // Beast.Movable does not ask, and a Kite Shield (2x3) put on over a Small Shield (2x2)
        // would be fine while a Small Shield put on over a Kite would lay the Kite across
        // whatever sat under the Small Shield's footprint. Asked here; a departure from MU2
        // in the direction of refusing what would corrupt the bag.
        const content::ItemRow* worn = rowOf(tables, bag[to]);
        return !worn || bag.room(tables, from, worn->width, worn->height, from);
    }

    const int block = blocking(tables, bag, from, to);
    if (block == kBlocked) return false;
    if (block < 0) return true;
    const Held& displaced = bag[block];
    const content::ItemRow* other = rowOf(tables, displaced);
    if (!other) return false;
    if (wearable(from)) {
        // A swap run the other way: what comes up to take its place has to pass the gates the
        // thing coming down just left.
        return placeOf(*other) == from && fits(tables, who, displaced) &&
               !handful(tables, bag, displaced, from);
    }
    // And the displaced thing has to fit into the space this one is leaving, or the window
    // promises a swap the move then refuses.
    int cells[kSlots];
    const int count = bag.covered(from, other->width, other->height, cells);
    if (count == 0) return false;
    for (int i = 0; i < count; ++i) {
        const int held = bag.holder(tables, cells[i]);
        if (held >= 0 && held != from && held != block) return false;
    }
    return true;
}

bool move(const content::Tables& tables, const Wearer& who, Satchel& bag, int from, int to) {
    if (!movable(tables, who, bag, from, to)) return false;
    // Whatever is in the way, which with a footprint is not always what is recorded at the
    // destination cell: a sword dropped on a breastplate's lower half displaces the
    // breastplate, whose own slot is two cells up.
    const int block = wearable(to) ? (bag[to].empty() ? -1 : to) : blocking(tables, bag, from, to);
    const Held what = bag.lift(from);
    const Held displaced = block >= 0 ? bag.lift(block) : Held{};
    bag.put(to, what);
    if (!displaced.empty()) bag.put(from, displaced);
    return true;
}

}  // namespace mu::sim
