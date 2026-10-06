#include "sim/items.h"

#include "sim/skills.h"

#include <algorithm>
#include <cmath>

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
    // A wing asks 4 levels more a plus: it falls to the rings' branch of WebZen's
    // `m_RequireLevel = RequireLevel + m_Level * 4` (zzzitem.cpp:628-632).
    // A 2nd wing 5 a plus (zzzitem.cpp:594-597).
    n.level = row.needLevel + (firstWing(row)    ? 4 * std::max(0, refinement)
                               : secondWing(row) ? 5 * std::max(0, refinement)
                                                 : 0);
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

bool canFly(const content::Tables& tables, const Satchel& bag) {
    const Held& mount = bag[kMount];
    const content::ItemRow* ridden =
        !mount.empty() && size_t(mount.item) < tables.items.size() ? &tables.items[size_t(mount.item)]
                                                                     : nullptr;
    if (ridden != nullptr && ridden->group == kGroupPets && ridden->number == 2) return false;
    if (!bag[kWings].empty()) return true;
    return ridden != nullptr && ridden->group == kGroupPets && ridden->number == 3 &&
           mount.durability > 0;
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
    } else if (row.number == 2) {
        // The Horn of Uniria: no damage, defence or absorb in OpenMU's 0.75 row or WebZen's base
        // (its one combat rule, no hit stiffness, is against players, ObjAttack.cpp:2734-2739).
        // gObjSpriteDamage wears it by damage x 1/10 / 10 (1.00.93 user.cpp:10615-10620).
        power.mount = true;
        power.wear = 0.1 / 10.0;
    } else if (row.number == 3) {
        // The Horn of Dinorant, **0.95d's and not 0.75's** (OpenMU Version095d/Items/Pets.cs:40):
        // damage x1.15 and taken x0.9. WebZen's 1.00.93 takes 1 of the rider's life for each x1.15
        // blow, as the Imp's 3 (ObjAttack.cpp:1293-1306), absorbs `90 - option` (:1314-1328), and
        // wears it by damage/200 a hit taken (user.cpp:10623-10628, NEW_SKILL_FORSKYLAND).
        power.mount = true;
        power.dealt = 1.15;
        power.taken = 0.9;
        power.lifeCost = 1;
        power.wear = 1.0 / 200.0;
    }
    return power;
}

bool ring(const content::ItemRow& row) {
    // MU's two, and the four powered rings (sim::Affix).
    return row.group == kGroupPets &&
           (row.number == 8 || row.number == 9 || (row.number >= 21 && row.number <= 24));
}

bool pendant(const content::ItemRow& row) {
    // MU's two, and the Pendant of Fury.
    return row.group == kGroupPets && (row.number == 12 || row.number == 13 || row.number == 25);
}

Affix signatureOf(const content::ItemRow& row) {
    if (row.group != kGroupPets) return Affix::None;
    switch (row.number) {
        case 21: return Affix::Wisdom;
        case 22: return Affix::Wealth;
        case 23: return Affix::Fortune;
        case 24: return Affix::Leech;
        case 25: return Affix::Fury;
        default: return Affix::None;
    }
}

int affixCount(const content::ItemRow& row, const Held& what) {
    if (!powered(row)) return 0;
    int count = 1;
    for (uint8_t a : what.affixes) count += a != 0 ? 1 : 0;
    return count;
}

int affixValue(Affix affix, int refinement) {
    // +0 and +9, the user's of 2026-10-03 ("+9 still to OP", then "its better").
    // The resistances 1 to 4 points, MU's own ring at +4 (2026-10-04).
    static const int kLow[kAffixes + 1] = {0, 1, 2, 1, 1, 2, 1, 1, 1, 1};
    static const int kHigh[kAffixes + 1] = {0, 8, 12, 10, 5, 12, 4, 4, 4, 4};
    const int at = int(affix);
    if (at <= 0 || at > kAffixes) return 0;
    const double t = double(std::clamp(refinement, 0, kRefineCap)) / double(kRefineCap);
    return int(std::lround(kLow[at] + (kHigh[at] - kLow[at]) * t * t));
}

const char* affixName(Affix affix) {
    switch (affix) {
        case Affix::Wisdom: return "Experience from kills";
        case Affix::Wealth: return "Zen from kills";
        case Affix::Fortune: return "Item find";
        case Affix::Leech: return "Life per hit";
        case Affix::Fury: return "Critical damage";
        case Affix::IceResistance: return "Ice resistance";
        case Affix::PoisonResistance: return "Poison resistance";
        case Affix::LightningResistance: return "Lightning resistance";
        case Affix::FireResistance: return "Fire resistance";
        default: return "";
    }
}

Element affixElement(Affix affix) {
    switch (affix) {
        case Affix::IceResistance: return Element::Ice;
        case Affix::PoisonResistance: return Element::Poison;
        case Affix::LightningResistance: return Element::Lightning;
        case Affix::FireResistance: return Element::Fire;
        default: return Element::None;
    }
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
    switch (row.number) {
        case 13: return Jewel::Bless;
        case 14: return Jewel::Soul;
        case 16: return Jewel::Life;
        default: return Jewel::None;
    }
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
    // the rule does, and the rule is the server's. Every number twice MU's (2026-10-06).
    static const char* const kDefense[kExcellentOptions] = {
        "Increases acquisition rate of Zen after hunting monsters +80%",
        "Defense success rate +20%",
        "Reflect Damage +10%",
        "Damage Decrease +8%",
        "Increase Max Mana +8%",
        "Increase Max HP +8%",
    };
    // CreatePhysicalAttackOptions and CreateWizardryAttackOptions, number 1 to 6.
    static const char* const kAttack[kExcellentOptions] = {
        "Increases acquisition rate of Mana after hunting monsters +Mana/4",
        "Increases acquisition rate of Life after hunting monsters +life/4",
        "Increase Attacking(Wizardry)speed +14",
        "Increase Damage +4%",
        "Increase Damage +level/10",
        "Excellent Damage rate +20%",
    };
    static const char* const kWizardry[kExcellentOptions] = {
        "Increases acquisition rate of Mana after hunting monsters +Mana/4",
        "Increases acquisition rate of Life after hunting monsters +life/4",
        "Increase Attacking(Wizardry)speed +14",
        "Increase Wizardry Dmg +4%",
        "Increase Wizardry Dmg +level/10",
        "Excellent Damage rate +20%",
    };
    if (row.armour() || row.shield() || ring(row)) return kDefense[bit];
    return row.magicPower > 0 || elementOf(row) == Element::Lightning ? kWizardry[bit]
                                                                      : kAttack[bit];
}

bool firstWing(const content::ItemRow& row) { return row.group == 12 && row.number <= 2; }
bool secondWing(const content::ItemRow& row) {
    return row.group == 12 && (row.number == kSpiritsNumber || row.number == kSoulNumber ||
                               row.number == kDragonNumber);
}
bool lochsFeather(const content::ItemRow& row) { return row.group == 13 && row.number == 14; }
bool anyWing(const content::ItemRow& row) { return firstWing(row) || secondWing(row); }

WingOption wingOption(const content::ItemRow& row, uint8_t bits) {
    const bool kind = (bits & kWingOptionKind) != 0;
    switch (row.group == 12 ? row.number : -1) {
        case 0: return WingOption::Regeneration;
        case 1: return WingOption::Wizardry;
        case kSpiritsNumber: return kind ? WingOption::Regeneration : WingOption::Damage;
        case kSoulNumber: return kind ? WingOption::Wizardry : WingOption::Regeneration;
        case kDragonNumber: return kind ? WingOption::Damage : WingOption::Regeneration;
        default: return WingOption::Damage;
    }
}

int wingOptionValue(const content::ItemRow& row, const Held& held) {
    if (held.option <= 0) return 0;
    return wingOption(row, held.wing) == WingOption::Regeneration ? held.option : held.option * 4;
}

int wingDefense(const content::ItemRow& row, int refinement) {
    if (!anyWing(row)) return 0;
    const int plus = std::max(0, refinement);
    // 3 a plus on a 1st wing, 2 on a 2nd (zzzitem.cpp:878-889), the triangle on both.
    int defense = row.defense + (secondWing(row) ? 2 : 3) * plus;
    if (plus >= 10) defense += (plus - 9) * (plus - 9 + 1) / 2;
    return defense;
}

PetPower wingPower(const content::ItemRow& row, int refinement) {
    PetPower power;
    if (!anyWing(row)) return power;
    const int plus = std::max(0, refinement);
    // A 2nd wing's x(132 + plus)% and x(75 - 2 a plus)% (ObjAttack.cpp, `m_Type >
    // MAKE_ITEMNUM(12,2)` under NEW_FORSKYLAND3, the flag that brought them).
    power.dealt = secondWing(row) ? double(132 + plus) / 100.0 : double(112 + 2 * plus) / 100.0;
    power.taken = secondWing(row) ? double(75 - 2 * plus) / 100.0 : double(88 - 2 * plus) / 100.0;
    // The wizard's wing, Heaven or Soul, 1 Life a blow; the others 3.
    power.lifeCost = row.number == 1 || row.number == kSoulNumber ? 1 : 3;
    return power;
}

int optionValue(const content::ItemRow& row, int level) {
    if (level <= 0) return 0;
    // A ring's and a pendant's is life regeneration, a percent a level (AT_LIFE_REGENERATION).
    if (jewellery(row)) return level;
    // And the Wings of Elf's (zzzitem.cpp:1150-1153); Heaven's and Satan's are 4 a level.
    if (firstWing(row) && row.number == 0) return level;
    // A 2nd wing's option without its kind (the price, the box's value) reads as 4 a level;
    // wingOptionValue says what it gives.
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
    if (kind == Jewel::Life) {
        return (takesOptions(row) || anyWing(row)) && target.option < kMostOption;
    }
    // And the rings and pendants, ours (the user, 2026-10-03: "if user upgrades rings and pendants
    // with jewel of bless or soul"); MU refines nothing past the boots.
    // And the 1st level wings, which MU raises (WebZen's level-up refuses from 12/7 on,
    // user.cpp:28576-28584).
    if ((row.group > kGroupBoots && !jewellery(row) && !anyWing(row)) || ammunition(row)) {
        return false;
    }
    // BlessJewelConsumeHandlerPlugIn's MaximumLevel 5, SoulJewelConsumeHandlerPlugIn's 8.
    const int highest = kind == Jewel::Bless ? 5 : 8;
    return target.refinement <= highest && target.refinement < kRefineCap;
}

const PowerRow* powerOf(uint8_t power) {
    // Grouped by who sets them and where (sim::PowerRow): the knight's weapon, the elf's, the
    // wizard's, every class's armour and shield, and every class's weapon and jewellery.
    constexpr uint8_t kWorn = kInArmour | kInShield | kInJewellery;
    constexpr uint8_t kHeld = kInWeapon | kInJewellery;
    constexpr uint8_t kAny = kInWeapon | kWorn;
    static const PowerRow kPowers[] = {
        {Power::Stormcall, "Stormcall", "20% on hit: lightning strikes a nearby monster",
         kEveryClass, kInWeapon, Rarity::Epic},
        {Power::Meteor, "Meteor", "15% on hit: a meteor falls on a nearby monster",
         kKnightOnly, kInWeapon, Rarity::Epic},
        {Power::Ice, "Ice", "15% on hit: freezes the target to half speed and wounds it",
         kKnightOnly, kInWeapon, Rarity::Epic},
        {Power::Poison, "Poison", "15% on hit: poisons the target for 20 s",
         kKnightOnly, kInWeapon, Rarity::Epic},
        {Power::Fireburst, "Fireburst", "10% on hit: four fireballs chain from the target",
         kKnightOnly, kInWeapon, Rarity::Legendary, true},
        {Power::FireRing, "Ring of Fire", "10% on hit: a ring of fire bursts around you",
         kKnightOnly, kInWeapon, Rarity::Legendary, true},
        {Power::Hellfire, "Hellfire", "10% on hit: hellfire erupts around you",
         kKnightOnly, kInWeapon, Rarity::Epic},
        {Power::Twister, "Twister", "10% on hit: a twister walks out at the target",
         kKnightOnly, kInWeapon, Rarity::Epic},
        {Power::Bulwark, "Bulwark", "Defense works without a shield", kKnightOnly,
         kInWeapon, Rarity::Legendary, true},
        // Every Fairy Elf's, as Arcane Echo is every wizard's (the user, 2026-10-05: Lirien's
        // Atlans clear pays it to the first-class elf).
        {Power::Frost, "Frost Arrow", "20% on hit: freezes the target for 2 s and wounds it",
         kElfOnly, kInWeapon, Rarity::Legendary},
        // Every Dark Wizard's, not the Soul Master's alone (the user, 2026-10-04: "remove 2nd
        // class requirement for that rune, because we give this rune on lorencia quest for DW").
        {Power::Echo, "Arcane Echo", "20% chance a spell casts twice, the second free",
         kWizardOnly, kInWeapon, Rarity::Legendary},
        {Power::Pyroblast, "Pyroblaster", "Fire Ball +50% damage, 20% to burst into four more",
         kWizardOnly, kInWeapon, Rarity::Legendary, true},
        {Power::Undying, "Undying", "+20% max life", kEveryClass, kWorn, Rarity::Epic},
        {Power::KeenEye, "Keen Eye", "+10% critical chance", kEveryClass, kWorn, Rarity::Rare},
        {Power::Bloodwell, "Bloodwell", "Heal 3% of damage dealt, 5% mana per kill",
         kEveryClass, kWorn, Rarity::Epic},
        {Power::Frenzy, "Frenzy", "Each hit: +8 attack speed for 4 s, stacks to +40",
         kEveryClass, kWorn, Rarity::Epic},
        {Power::Renewal, "Renewal", "Restore 3% life every 3 s",
         kEveryClass, kWorn, Rarity::Epic},
        {Power::Spirits, "Evil Spirit", "15% when missed: evil spirits strike monsters around you",
         kEveryClass, kInShield | kInJewellery, Rarity::Legendary},
        {Power::Kinship, "Kinship", "Your pets lose their drawbacks", kEveryClass, kInJewellery, Rarity::Epic},
        {Power::Wrath, "Wrath", "+20% damage", kEveryClass, kInWeapon, Rarity::Legendary},
        {Power::Ironskin, "Ironskin", "+10% defense", kEveryClass, kInArmour | kInShield,
         Rarity::Rare},
        {Power::Steadfast, "Steadfast", "+5% block chance, up to 25%",
         kEveryClass, kInArmour | kInShield, Rarity::Rare},
        {Power::SecondWind, "Second Wind", "Kills restore 5% life and mana",
         kEveryClass, kWorn, Rarity::Epic},
        {Power::Whirlwind, "Whirlwind", "Twisting Slash +50% damage, 25% to pull monsters in",
         kKnightOnly, kInWeapon, Rarity::Legendary, true},
        {Power::Volley, "Piercing Volley", "Penetration fires three piercing arrows in a fan",
         kElfOnly, kInWeapon, Rarity::Legendary, true},
        {Power::Inferno, "Inferno", "+20% fire damage", kEveryClass, kHeld, Rarity::Rare},
        {Power::Glacier, "Glacier", "+20% ice damage", kEveryClass, kHeld, Rarity::Rare},
        {Power::Venom, "Venom", "+20% poison damage", kEveryClass, kHeld, Rarity::Rare},
        {Power::Thunder, "Thunder", "+20% lightning damage", kEveryClass, kHeld, Rarity::Rare},
        {Power::Tempest, "Tempest", "+20% wind damage", kEveryClass, kHeld, Rarity::Rare},
        // The stat runes (sim::statShareOf), every class's and every socket's -- but the all-stats
        // pair, a weapon's socket alone (the user, 2026-10-05).
        {Power::LesserMight, "Lesser Might", "+10% strength", kEveryClass, kAny, Rarity::Rare},
        {Power::GreaterMight, "Greater Might", "+30% strength", kEveryClass, kAny, Rarity::Epic},
        {Power::LesserGrace, "Lesser Grace", "+10% agility", kEveryClass, kAny, Rarity::Rare},
        {Power::GreaterGrace, "Greater Grace", "+30% agility", kEveryClass, kAny, Rarity::Epic},
        {Power::LesserVigor, "Lesser Vigor", "+10% vitality", kEveryClass, kAny, Rarity::Rare},
        {Power::GreaterVigor, "Greater Vigor", "+30% vitality", kEveryClass, kAny, Rarity::Epic},
        {Power::LesserInsight, "Lesser Insight", "+10% energy", kEveryClass, kAny, Rarity::Rare},
        {Power::GreaterInsight, "Greater Insight", "+30% energy", kEveryClass, kAny,
         Rarity::Epic},
        {Power::LesserAscendance, "Lesser Ascendance", "+10% all stats", kEveryClass,
         kInWeapon, Rarity::Epic},
        {Power::GreaterAscendance, "Greater Ascendance", "+30% all stats", kEveryClass,
         kInWeapon, Rarity::Legendary},
    };
    for (const PowerRow& row : kPowers) {
        if (uint8_t(row.power) == power) return &row;
    }
    return nullptr;
}

HeroPoints statShareOf(Power power) {
    const int p = int(power);
    if (p < int(Power::LesserMight) || p > int(Power::GreaterAscendance)) return {};
    // Two tiers a stat, in the enum's order: Might, Grace, Vigor, Insight, Ascendance.
    const int share = (p - int(Power::LesserMight)) % 2 == 0 ? 10 : 30;
    switch ((p - int(Power::LesserMight)) / 2) {
        case 0: return {share, 0, 0, 0};
        case 1: return {0, share, 0, 0};
        case 2: return {0, 0, share, 0};
        case 3: return {0, 0, 0, share};
        default: return {share, share, share, share};
    }
}

const char* rarityName(Rarity rarity) {
    switch (rarity) {
        case Rarity::Rare: return "Rare";
        case Rarity::Epic: return "Epic";
        case Rarity::Legendary: return "Legendary";
    }
    return "";
}

Element elementOf(Power power) {
    switch (power) {
        case Power::Inferno: return Element::Fire;
        case Power::Glacier: return Element::Ice;
        case Power::Venom: return Element::Poison;
        case Power::Thunder: return Element::Lightning;
        case Power::Tempest: return Element::Wind;
        default: return Element::None;
    }
}

bool elementServes(Element element, Kin kin) {
    switch (kin) {
        // His runes' rock (fire), frost (ice, since 2026-10-02 the Ice rune wounds), sickness
        // (poison) and lightning; Cyclone and Twisting Slash (wind).
        case Kin::DarkKnight: return element != Element::None;
        // Fire Ball, Flame, Meteorite, Hellfire; Ice; Poison; Lightning; Twister (wind).
        case Kin::DarkWizard: return element != Element::None;
        // Frost Arrow's wound; Penetration (wind).
        case Kin::FairyElf: return element == Element::Ice || element == Element::Wind;
        default: return false;
    }
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

bool firecracker(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number == 11;
}

bool scrollOfArchangel(const content::ItemRow& row) {
    return row.group == kGroupPets && row.number == 16;
}

bool bloodBone(const content::ItemRow& row) { return row.group == kGroupPets && row.number == 17; }

bool archangelWeapon(const content::ItemRow& row) {
    return (row.group == 5 && row.number == 10) || (row.group == 0 && row.number == 19) ||
           (row.group == 4 && row.number == 18);
}

bool classTreasure(const content::ItemRow& row) {
    return row.group == kGroupPotions && row.number >= 24 && row.number <= 26;
}

bool divineStaff(const content::ItemRow& row) {
    return row.group == 5 && row.number == 10;  // group 5, the staves
}

bool invisibilityCloak(const content::ItemRow& row) {
    return row.group == kGroupPets && row.number == 18;
}

bool refiningJewel(const content::ItemRow& row) {
    return (row.group == kGroupPotions &&
            (row.number == 13 || row.number == 14 || row.number == 16)) ||
           (row.group == 12 && row.number == 15);
}

bool settable(const content::Tables& tables, const Held& jewel, const Held& target, Kin kin,
              bool second) {
    const auto known = [&](const Held& h) {
        return !h.empty() && size_t(h.item) < tables.items.size();
    };
    if (!known(jewel) || !known(target)) return false;
    if (!creation(tables.items[size_t(jewel.item)])) return false;
    const PowerRow* power = powerOf(jewel.powers[0]);
    if (power == nullptr || !power->takenBy(kin, second)) return false;
    const content::ItemRow& row = tables.items[size_t(target.item)];
    if (!socketsFit(row) || freeSocket(target) < 0) return false;
    // Its group's sockets (sim::PowerRow::slots): a ring's and a pendant's take the armour runes
    // and the element runes (the user, 2026-10-02: "allow to put runes on jewels and pendants"),
    // and a weapon's power is read off the hands only, so it stays out of them.
    const uint8_t kind = jewellery(row) ? kInJewellery
                         : row.shield() ? kInShield
                         : row.weapon() ? kInWeapon
                                        : kInArmour;
    return (power->slots & kind) != 0;
}

int placeOf(const content::ItemRow& row) {
    if (row.group < kGroupShields) {
        // Every bow and crossbow in the weapon slot and every quiver in the left hand -- ours
        // (the user, 2026-10-02: "bow/crosbow has to go to the same slot"). MU puts a bow in
        // Weapon[1] and its arrows in Weapon[0] (GetEquipedBowType, OpenMU's CreateAmmunition),
        // so a bow and a crossbow sat in different hands. The figure draws a bow by its name in
        // the hand its clip holds it in, whichever slot it came out of. Satchel.Hand.
        if (ammunition(row)) return kWeaponLeft;
        return kWeaponRight;
    }
    if (row.group >= kGroupShields && row.group <= kGroupBoots) return row.group - 5;
    // The Guardian Angel and the Imp: EQUIPMENT_HELPER, OpenMU's slot type holding 8 (CreatePet).
    // The Horns of Uniria and Dinorant are helpers there too; here they take the mount's slot.
    // A ring in the right ring slot and a pendant as the amulet (ZzzInfomation.cpp:1085-1094);
    // placesIn lets a ring into the left one too.
    if (ring(row)) return kRingRight;
    if (pendant(row)) return kAmulet;
    if (row.group == kGroupPets && (row.number == 2 || row.number == 3)) return kMount;
    if (row.group == kGroupPets && row.number <= 1) return kPet;
    // The 1st level wings, EQUIPMENT_WING (OpenMU Version075 Wings.cs, ItemSlot 7).
    if (anyWing(row)) return kWings;
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
int stackMost(const content::ItemRow& row) {
    if (heals(row) || restores(row) || antidote(row)) return kStackMost;
    if (firecracker(row)) return kFirecrackerStackMost;
    return 0;
}

bool stacks(const content::ItemRow& row) { return stackMost(row) > 0; }

bool tops(const content::Tables& tables, const Held& onto, const Held& what) {
    if (onto.empty() || onto.item != what.item || onto.refinement != what.refinement) return false;
    if (size_t(onto.item) >= tables.items.size()) return false;
    return onto.durability < stackMost(tables.items[size_t(onto.item)]);
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
    if (!row || placeOf(*row) < 0 || archangelWeapon(*row)) return false;
    // mu.db's class enumeration, bit 0 wizard, bit 1 elf, bit 2 knight; none named is anybody.
    if (row->classes != 0 && (row->classes & (1 << int(who.kin))) == 0) return false;
    // A 2nd level wing and the second class's gear are its class's second's: OpenMU's class
    // level 2 (docs/second-class-gear.md).
    if (secondClassOnly(*row) && !who.second) return false;
    return shortOf(asks(*row, what.refinement, what.excellent != 0), who.level, who.points).none();
}

namespace {

// A bow or a crossbow: the bow group less its two quivers.
bool shooter(const content::ItemRow& row) {
    return row.group == kGroupBows && !ammunition(row);
}

// Whether two things may be held at once, one in each hand: a two-handed weapon beside nothing
// but a quiver, and a bow or crossbow beside nothing but its own -- the arrows a bow's, the bolt
// a crossbow's (MuMain's CheckArrow pairs them the same way).
bool together(const content::ItemRow& a, const content::ItemRow& b) {
    if (shooter(a) || shooter(b)) {
        const content::ItemRow& bow = shooter(a) ? a : b;
        const content::ItemRow& other = shooter(a) ? b : a;
        return ammunition(other) && other.number == (bow.number <= 6 ? 15 : 7);
    }
    if (ammunition(a) || ammunition(b)) return true;
    return !a.twoHanded() && !b.twoHanded();
}

// Puts the bag's thing at `from` on at worn slot `to`, and takes down whatever it cannot be
// worn with: what was in that slot, and the other hand when the two cannot be held together --
// a two-handed sword or a bow put on over a sword and shield takes both off. What comes down
// goes into the bag, the first where this came from if it fits there and the rest wherever
// there is room (the user, 2026-10-02: "it has to drop old one in inventory and get space for
// anything what it needs"). False, with the bag half-changed, when something has nowhere to go;
// the callers run it on a copy.
bool wearOn(const content::Tables& tables, const Wearer& who, Satchel& bag, int from, int to) {
    const content::ItemRow* row = rowOf(tables, bag[from]);
    if (!row || !baggable(from) || !placesIn(*row, who.kin, to) || !fits(tables, who, bag[from])) {
        return false;
    }
    const Held what = bag.lift(from);
    Held down[2];
    int count = 0;
    if (!bag[to].empty()) down[count++] = bag.lift(to);
    if (to == kWeaponRight || to == kWeaponLeft) {
        const int other = to == kWeaponRight ? kWeaponLeft : kWeaponRight;
        const content::ItemRow* held = rowOf(tables, bag[other]);
        if (held && !together(*row, *held)) down[count++] = bag.lift(other);
    }
    bag.put(to, what);
    for (int i = 0; i < count; ++i) {
        const content::ItemRow* r = rowOf(tables, down[i]);
        if (!r) return false;
        const int at = i == 0 && bag.room(tables, from, r->width, r->height)
                           ? from
                           : bag.free(tables, r->width, r->height);
        if (at < 0) return false;
        bag.put(at, down[i]);
    }
    return true;
}

}  // namespace

bool handful(const content::Tables& tables, const Satchel& bag, const Held& what, int hand) {
    if (hand != kWeaponRight && hand != kWeaponLeft) return false;
    const Held& other = bag[hand == kWeaponRight ? kWeaponLeft : kWeaponRight];
    const content::ItemRow* mine = rowOf(tables, what);
    const content::ItemRow* theirs = rowOf(tables, other);
    if (!mine || !theirs) return false;
    return !together(*mine, *theirs);
}

bool movable(const content::Tables& tables, const Wearer& who, const Satchel& bag, int from,
             int to) {
    if (from == to || from < 0 || from >= kSlots || to < 0 || to >= kSlots) return false;
    const Held& what = bag[from];
    const content::ItemRow* row = rowOf(tables, what);
    if (!row) return false;

    if (wearable(to)) {
        // Tried on a copy: whatever comes down has to find room in the bag, or nothing moves.
        Satchel trial = bag;
        return wearOn(tables, who, trial, from, to);
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
    if (wearable(to)) {
        Satchel trial = bag;
        if (!wearOn(tables, who, trial, from, to)) return false;
        bag = trial;
        return true;
    }
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
    const int block = blocking(tables, bag, from, to);
    const Held what = bag.lift(from);
    const Held displaced = block >= 0 ? bag.lift(block) : Held{};
    bag.put(to, what);
    if (!displaced.empty()) bag.put(from, displaced);
    return true;
}

}  // namespace mu::sim
