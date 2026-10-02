#include "sim/items.h"

#include "sim/skills.h"

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

Needs asks(const content::ItemRow& row, int refinement, bool excellent) {
    const int scaled = row.dropLevel + 3 * refinement + (excellent ? kExcellentLevelDelta : 0);
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

PetPower petPower(const content::ItemRow& row) {
    PetPower power;
    if (row.group != kGroupPets) return power;
    // gObjSpriteDamage divides both wear rates by fN = 10 (the `happycat@20050201` line); what
    // 0.75 itself divided by is not in any source read, so the documented 1.00.93 rate stands.
    if (row.number == 0) {
        power.taken = 0.7;
        power.health = 50;
        // Ours (the user, 2026-09-30): the Angel's price, his own blows at x0.8. No WebZen
        // server has it -- gObjAngelSprite is asked of the target only -- but it is how the
        // user remembers the Angel, so the two pets trade: guard for the Angel, strike for the Imp.
        power.dealt = 0.8;
        power.wear = 0.3 / 10.0;
    } else if (row.number == 1) {
        power.dealt = 1.3;
        power.lifeCost = 3;
        power.wear = 0.2 / 10.0;
    }
    return power;
}

bool ring(const content::ItemRow& row) {
    return row.group == kGroupPets && (row.number == 8 || row.number == 9);
}

bool pendant(const content::ItemRow& row) {
    return row.group == kGroupPets && (row.number == 12 || row.number == 13);
}

Element elementOf(const content::ItemRow& row) {
    if (row.group != kGroupPets) return Element::None;
    switch (row.number) {
        case 8: return Element::Ice;
        case 9: return Element::Poison;
        case 12: return Element::Lightning;
        case 13: return Element::Fire;
        default: return Element::None;
    }
}

bool ammunition(const content::ItemRow& row) {
    return row.group == kGroupBows && (row.number == 7 || row.number == 15);
}

Jewel jewelOf(const content::ItemRow& row) {
    if (row.group != kGroupPotions) return Jewel::None;
    return row.number == 13 ? Jewel::Bless : row.number == 14 ? Jewel::Soul : Jewel::None;
}

bool expensive(const content::Tables& tables, const Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return false;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    // `(iLevel > 6 && pItem->Type < ITEM_WING)`: every group before the wings' twelve.
    constexpr int kGroupWings = 12;
    // And, ours, the Rune of Creation and anything with a socket: the user, 2026-09-29, "jewel of
    // creation or item with sockets is not dropobale".
    // The pets share the jewels' drop group and not their worth: IsHighValueItem names no helper.
    const bool jewel = row.jewel() && row.group != kGroupPets;
    return jewel || (what.refinement > 6 && row.group < kGroupWings) || what.excellent != 0 ||
           creation(row) || what.sockets > 0;
}

bool takesOptions(const content::ItemRow& row) {
    return row.group <= kGroupBoots && !ammunition(row);
}

bool excellentable(const content::ItemRow& row) { return takesOptions(row) || jewellery(row); }

int excellentDamage(const content::ItemRow& row) {
    if (!excellentable(row) || row.minimumDamage <= 0) return 0;
    return row.minimumDamage * 25 / std::max(1, row.dropLevel) + 5;
}

int excellentDefense(const content::ItemRow& row) {
    if (!row.armour() || row.shield()) return 0;
    const int drop = std::max(1, row.dropLevel);
    return row.defense * 12 / drop + drop / 5 + 4;
}

int excellentBlock(const content::ItemRow& row) {
    if (!row.shield() || row.defenseRate <= 0) return 0;
    return row.defenseRate * 25 / std::max(1, row.dropLevel) + 5;
}

int setOf(const content::ItemRow& row) {
    return row.group >= kGroupHelms && row.group <= kGroupBoots ? row.number : -1;
}

double setDefense(const content::Tables& tables, const Satchel& bag) {
    int set = -1;
    bool excellent = true;
    for (int slot = kHelm; slot <= kBoots; ++slot) {
        const Held& worn = bag[slot];
        if (worn.empty() || size_t(worn.item) >= tables.items.size()) return 0.0;
        const int of = setOf(tables.items[size_t(worn.item)]);
        if (of < 0 || (set >= 0 && of != set)) return 0.0;
        set = of;
        excellent = excellent && worn.excellent != 0;
    }
    return excellent ? kExcellentSetDefense : kSetDefense;
}

std::string excellentLine(const content::ItemRow& row, int bit) {
    if (!excellentable(row) || bit < 0 || bit >= kExcellentOptions) return std::string();
    // ExcellentOptions.CreateDefenseOptions, number 1 to 6. The Zen line is written with
    // OpenMU's 40 (MoneyAmountRate 1.4) where MuMain's GT 627 reads +30%: the line says what
    // the rule does, and the rule is the server's.
    static const char* const kDefense[kExcellentOptions] = {
        "Increases acquisition rate of Zen after hunting monsters +40%",
        "Defense success rate +10%",
        "Reflect Damage +5%",
        "Damage Decrease +4%",
        "Increase Max Mana +4%",
        "Increase Max HP +4%",
    };
    // CreatePhysicalAttackOptions and CreateWizardryAttackOptions, number 1 to 6.
    static const char* const kAttack[kExcellentOptions] = {
        "Increases acquisition rate of Mana after hunting monsters +Mana/8",
        "Increases acquisition rate of Life after hunting monsters +life/8",
        "Increase Attacking(Wizardry)speed +7",
        "Increase Damage +2%",
        "Increase Damage +level/20",
        "Excellent Damage rate +10%",
    };
    static const char* const kWizardry[kExcellentOptions] = {
        "Increases acquisition rate of Mana after hunting monsters +Mana/8",
        "Increases acquisition rate of Life after hunting monsters +life/8",
        "Increase Attacking(Wizardry)speed +7",
        "Increase Wizardry Dmg +2%",
        "Increase Wizardry Dmg +level/20",
        "Excellent Damage rate +10%",
    };
    if (row.armour() || row.shield() || ring(row)) return kDefense[bit];
    return row.magicPower > 0 || elementOf(row) == Element::Lightning ? kWizardry[bit]
                                                                      : kAttack[bit];
}

int optionValue(const content::ItemRow& row, int level) {
    if (level <= 0) return 0;
    // A ring's and a pendant's is life regeneration, a percent a level (AT_LIFE_REGENERATION).
    if (jewellery(row)) return level;
    return level * (row.shield() ? 5 : 4);
}

bool refinable(const content::Tables& tables, const Held& jewel, const Held& target) {
    const auto known = [&](const Held& h) {
        return !h.empty() && size_t(h.item) < tables.items.size();
    };
    if (!known(jewel) || !known(target)) return false;
    const Jewel kind = jewelOf(tables.items[size_t(jewel.item)]);
    if (kind == Jewel::None) return false;
    const content::ItemRow& row = tables.items[size_t(target.item)];
    if (row.group > kGroupBoots || ammunition(row)) return false;
    // BlessJewelConsumeHandlerPlugIn's MaximumLevel 5, SoulJewelConsumeHandlerPlugIn's 8.
    const int highest = kind == Jewel::Bless ? 5 : 8;
    return target.refinement <= highest && target.refinement < kRefineCap;
}

const PowerRow* powerOf(uint8_t power) {
    static const PowerRow kPowers[] = {
        {Power::Stormcall, "Stormcall",
         "A swing that lands has a 20% chance to call lightning down on a monster near him, another "
         "where there is one, its damage raised by his energy",
         true, Kin::DarkKnight},
        {Power::Meteor, "Meteor",
         "A swing that lands has a 15% chance to bring a burning rock down on a monster near him, "
         "another where there is one",
         true, Kin::DarkKnight},
        {Power::Ice, "Ice",
         "A swing that lands has a 15% chance to freeze the monster he struck, slowing it to half "
         "its pace",
         true, Kin::DarkKnight},
        {Power::Poison, "Poison",
         "A swing that lands has a 15% chance to poison the monster he struck, hurting it for "
         "twenty seconds",
         true, Kin::DarkKnight},
        {Power::Frost, "Frost Arrow",
         "An arrow that lands has a 20% chance to freeze the monster it struck for two seconds "
         "and wound it again for half the arrow's damage, raised by her energy",
         true, Kin::FairyElf},
        {Power::Echo, "Arcane Echo",
         "A spell he casts has a 20% chance to be cast a second time, for no mana", true,
         Kin::DarkWizard},
        {Power::Undying, "Undying", "+20% maximum health", false, Kin::DarkKnight, true},
        {Power::KeenEye, "Keen Eye", "+10% critical hit chance", false, Kin::DarkKnight, true},
        {Power::Bloodwell, "Bloodwell",
         "3% of the damage you deal comes back as life, and 5% of your mana after a kill", false,
         Kin::DarkKnight, true},
        {Power::Frenzy, "Frenzy",
         "A blow that lands has a 15% chance to raise attack and casting speed by 20 for three "
         "seconds",
         false, Kin::DarkKnight, true},
        {Power::Renewal, "Renewal", "Restores 3% of maximum health every three seconds, anywhere",
         false, Kin::DarkKnight, true},
        {Power::Spirits, "Evil Spirit",
         "A blow that misses you has a 15% chance to release evil spirits around you, which "
         "strike most of the monsters within ten tiles, raised by your energy",
         false, Kin::DarkKnight, true, true},
    };
    for (const PowerRow& row : kPowers) {
        if (uint8_t(row.power) == power) return &row;
    }
    return nullptr;
}

int freeSocket(const Held& thing) {
    for (int i = 0; i < std::min<int>(thing.sockets, kMostSockets); ++i) {
        if (thing.powers[i] == 0) return i;
    }
    return -1;
}

bool creation(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 22;
}

bool refiningJewel(const content::ItemRow& row) {
    return (row.group == kGroupPotions && (row.number == 13 || row.number == 14)) ||
           (row.group == 12 && row.number == 15);
}

bool settable(const content::Tables& tables, const Held& jewel, const Held& target, Kin kin) {
    const auto known = [&](const Held& h) {
        return !h.empty() && size_t(h.item) < tables.items.size();
    };
    if (!known(jewel) || !known(target)) return false;
    if (!creation(tables.items[size_t(jewel.item)])) return false;
    const PowerRow* power = powerOf(jewel.powers[0]);
    if (power == nullptr || (!power->everyone && power->kin != kin)) return false;
    const content::ItemRow& row = tables.items[size_t(target.item)];
    if (!takesSockets(row) || freeSocket(target) < 0) return false;
    // A ring's sockets (the Pit's, the only ring there is with any) take Evil Spirit alone: the user,
    // 2026-10-01, "give ring with +1 sockets, and give only evil spirits rune".
    if (ring(row)) return power->power == Power::Spirits;
    if (power->shieldOnly) return row.shield();
    return power->weapon == (row.weapon() && !row.shield());
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
    // The Guardian Angel and the Imp: EQUIPMENT_HELPER, OpenMU's slot type holding 8 (CreatePet).
    // A ring in the right ring slot and a pendant as the amulet (ZzzInfomation.cpp:1085-1094);
    // placesIn lets a ring into the left one too.
    if (ring(row)) return kRingRight;
    if (pendant(row)) return kAmulet;
    if (row.group == kGroupPets && row.number <= 1) return kPet;
    return -1;
}

bool offHanded(const content::ItemRow& row, Kin kin) {
    return kin == Kin::DarkKnight && row.weapon() && !row.shield() && row.group < kGroupBows &&
           !row.twoHanded();
}

bool placesIn(const content::ItemRow& row, Kin kin, int slot) {
    const int place = placeOf(row);
    if (place == slot) return true;
    if (slot == kRingLeft && place == kRingRight) return true;
    return slot == kWeaponLeft && place == kWeaponRight && offHanded(row, kin);
}

bool heals(const content::ItemRow& row) { return row.group == kGroupPotions && row.number <= 3; }
bool restores(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number >= 4 && row.number <= 6;
}

bool ale(const content::ItemRow& row) { return row.group == kGroupPotions && row.number == 9; }
bool summoningOrb(const content::ItemRow& row) { return row.group == 12 && row.number == 11; }

content::ItemRow asRead(const content::ItemRow& row, int refinement) {
    content::ItemRow read = row;
    if (!summoningOrb(row)) return read;
    static const char* const kNames[6] = {"Goblin", "Golem", "Assassin",
                                          "Elite Yeti", "Dark Knight", "Bali"};
    static const int kEnergy[6] = {30, 60, 90, 130, 170, 210};
    const int at = std::clamp(refinement, 0, 5);
    read.teaches = skill::kSummonGoblin + at;
    read.teachesEnergy = kEnergy[at];
    read.label = std::string("Orb of ") + kNames[at];
    if (const SkillRow* taught = skillNumbered(read.teaches)) {
        read.teachesName = taught->name;
        read.teachesTells = taught->tells;
    }
    return read;
}

bool antidote(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 8;
}
bool portal(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 10;
}

// And the Antidote, the user's (2026-09-30: "antidotes are not stacking"): the Dungeon's poisons
// stack now, and a player carries a few.
bool stacks(const content::ItemRow& row) { return heals(row) || restores(row) || antidote(row); }

bool tops(const content::Tables& tables, const Held& onto, const Held& what) {
    if (onto.empty() || onto.item != what.item || onto.refinement != what.refinement) return false;
    if (size_t(onto.item) >= tables.items.size()) return false;
    return stacks(tables.items[size_t(onto.item)]) && onto.durability < kStackMost;
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
    return shortOf(asks(*row, what.refinement, what.excellent != 0), who.level, who.points).none();
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
        if (!baggable(from) || !placesIn(*row, who.kin, to) || !fits(tables, who, what) ||
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
        return placesIn(*other, who.kin, from) && fits(tables, who, displaced) &&
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
    // A stack let go on a stack of its kind pours into it, and what does not fit stays behind
    // (kStackMost). A full one swaps, as anything else does.
    if (baggable(from) && baggable(to)) {
        const int onto = bag.holder(tables, to);
        if (onto >= 0 && onto != from) {
            const int went = topUp(tables, bag, onto, bag[from]);
            if (went > 0) {
                Held left = bag[from];
                left.durability = int16_t(left.durability - went);
                if (left.durability <= 0) bag.lift(from);
                else bag.put(from, left);
                return true;
            }
        }
    }
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
