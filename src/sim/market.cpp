#include "sim/market.h"

#include <algorithm>
#include <cmath>

namespace mu::sim {
namespace {

constexpr int kHelms = 7, kArmours = 8, kPants = 9, kGloves = 10, kBoots = 11, kShields = 6;
constexpr int kSwords = 0, kAxes = 1, kMaces = 2, kSpears = 3, kBows = 4, kStaves = 5;
constexpr int kOrbs = 12, kPets = 13, kPotions = 14, kScrolls = 15;

constexpr Offer gear(int slot, int group, int number, int refinement, bool skill = false) {
    return Offer{slot, group, number, refinement, 0, skill};
}
constexpr Offer sip(int slot, int number, int pieces) {
    return Offer{slot, kPotions, number, 0, pieces, false};
}
constexpr Offer scroll(int slot, int number) { return Offer{slot, kScrolls, number, 0, 0, false}; }

// WebZen's own shelves (the user, 2026-10-01: "do it"), off shop0..shop10 in WebZen's
// 0.99.60T server package (github makaytrue/0.99.60T, Files.rar), whose headers name each
// NPC and its tile and are dated 2004-08 to 2005-06; which NPC keeps which file is
// user.cpp:4645-4659 of GameServer 1.00.93. Everything at +0, no luck, no option; a weapon
// listed twice is once plain and once with its skill. Laid out as WebZen's CShop fills its
// eight-wide shelf: each in list order on the first free cells. Left out as later than 0.97d:
// the Armor of Guardman (Chaos Castle), the siege Contracts, the +1 potions, and the Devil's
// Eye and Key (no Devil Square here). OpenMU's Version075 shelves, which these replace, were
// Season 6's with its later rows taken out, at +3 with luck and options -- far fuller.

// Potion Girl Amy (shop5): the eight potions, single and three, arrows and bolts to +2, the
// Town Portal Scroll.
constexpr Offer kPotionGirl[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 3), sip(9, 1, 3), sip(10, 2, 3), sip(11, 3, 3),
    sip(12, 4, 3), sip(13, 5, 3), sip(14, 6, 3), sip(15, 8, 3), gear(16, kBows, 7, 0),
    gear(17, kBows, 7, 1), gear(18, kBows, 7, 2), gear(19, kBows, 15, 0), gear(20, kBows, 15, 1),
    gear(21, kBows, 15, 2), sip(22, 10, 1),
};

// Harold (shop3), the wandering merchant east of town: the potions and the Leather set.
constexpr Offer kHarold[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 3), sip(9, 1, 3), sip(10, 2, 3), sip(11, 3, 3),
    sip(12, 4, 3), sip(13, 5, 3), sip(14, 6, 3), sip(15, 8, 3), gear(16, kHelms, 5, 0),
    gear(18, kPants, 5, 0), gear(20, kBoots, 5, 0), gear(22, kGloves, 5, 0),
    gear(32, kArmours, 5, 0),
};

// Martin (shop4), the wandering merchant in the west: the potions, the Bone and Scale pieces
// Hanzo does not sell, Brass gloves and boots, two shields and a rack of weapons. Without the
// source's +S copies of the Gladius, the Falchion and the Arquebus, as Hanzo is without his
// (the user, 2026-10-01): with the skills on the orbs a copy was the same weapon dearer.
constexpr Offer kMartin[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 3), sip(9, 1, 3), sip(10, 2, 3), sip(11, 3, 3),
    sip(12, 4, 3), sip(13, 5, 3), sip(14, 6, 3), sip(15, 8, 3), gear(16, kHelms, 4, 0),
    gear(18, kPants, 4, 0), gear(20, kHelms, 6, 0), gear(22, kArmours, 6, 0),
    gear(32, kPants, 6, 0), gear(34, kGloves, 8, 0), gear(36, kBoots, 8, 0),
    gear(38, kShields, 2, 0), gear(48, kShields, 6, 0), gear(50, kSpears, 2, 0),
    gear(52, kSwords, 3, 0), gear(53, kAxes, 3, 0), gear(54, kSpears, 6, 0),
    gear(64, kMaces, 2, 0), gear(65, kSwords, 6, 0),
    gear(82, kBows, 10, 0), gear(88, kSpears, 1, 0),
    gear(87, kSwords, 7, 0),
};

// Hanzo (shop0): the knight's first weapons, the Bronze set, Scale gloves and boots and three
// shields. **And the knight's nine orbs after them, which are ours**: in 0.75 the weapon WAS
// the skill (docs/skills-dk.md §1.2), and this shelf is the route that replaces it -- in the
// ladder's own order, the level each asks being the drop level of the first weapon that carried
// that skill.
//
// **Without the source's three +S copies** (the user, 2026-10-01: "dublivated item"): the
// Sword of Assassin, the Morning Star and the Double Axe stood twice, plain and with their skill. With the
// skill on the orbs the copy taught nothing and only cost more, so it was the same sword twice.
constexpr Offer kBlacksmith[] = {
    gear(0, kSwords, 1, 0), gear(1, kSwords, 2, 0), gear(2, kSwords, 4, 0),
    gear(4, kHelms, 0, 0), gear(6, kArmours, 0, 0),
    gear(20, kPants, 0, 0), gear(22, kGloves, 0, 0), gear(24, kBoots, 0, 0),
    gear(26, kGloves, 6, 0), gear(36, kBoots, 6, 0), gear(38, kAxes, 1, 0),
    gear(39, kMaces, 0, 0), gear(40, kMaces, 1, 0),
    gear(42, kAxes, 2, 0), gear(52, kSwords, 0, 0),
    gear(61, kSpears, 5, 0), gear(64, kShields, 0, 0), gear(66, kShields, 4, 0),
    gear(80, kShields, 1, 0), gear(53, kOrbs, 3, 0), gear(63, kOrbs, 4, 0), gear(68, kOrbs, 5, 0),
    gear(71, kOrbs, 6, 0), gear(76, kOrbs, 7, 0), gear(79, kOrbs, 25, 0), gear(82, kOrbs, 12, 0),
    gear(83, kOrbs, 20, 0), gear(84, kOrbs, 19, 0),
};

// Pasi (shop2): Fire Ball and Power Wave, the Pad set, Bone gloves and boots, two staves. **And
// the Scroll of Soul Barrier after the two, which is ours** (the user, 2026-09-28): the wizard's
// guard, priced as the knight's Orb of Defense. The other scrolls drop, or Izabel sells them.
constexpr Offer kMage[] = {
    scroll(0, 3), scroll(1, 10), scroll(2, 15), gear(3, kHelms, 2, 0), gear(5, kArmours, 2, 0),
    gear(16, kPants, 2, 0), gear(18, kGloves, 2, 0), gear(20, kBoots, 2, 0),
    gear(22, kGloves, 4, 0), gear(32, kBoots, 4, 0), gear(34, kStaves, 0, 0),
    gear(35, kStaves, 1, 0),
};

// Caren the Barmaid (shop6): the Ale. **And the two pets, which are ours** (the user,
// 2026-09-29): WebZen sells neither anywhere, so the tavern is where they are bought for now.
constexpr Offer kBarmaid[] = {
    sip(0, 9, 1), gear(1, kPets, 0, 0), gear(2, kPets, 1, 0),
};

// **And Lumen's own, which is ours** (the user, 2026-09-30): the same bar with Amy's potions on
// the two rows below it, laid out as Amy lays them, a single over its three. Lorencia only --
// Caren keeps the plain bar, since Izabel sells the potions in Devias.
constexpr Offer kLumen[] = {
    sip(0, 9, 1),   sip(1, 10, 1),  gear(2, kPets, 0, 0), gear(3, kPets, 1, 0),
    sip(16, 0, 1),  sip(24, 0, 3),  sip(17, 1, 1),  sip(25, 1, 3),  sip(18, 2, 1),  sip(26, 2, 3),
    sip(19, 3, 1),  sip(27, 3, 3),  sip(20, 4, 1),  sip(28, 4, 3),  sip(21, 5, 1),  sip(29, 5, 3),
    sip(22, 6, 1),  sip(30, 6, 3),  sip(23, 8, 1),  sip(31, 8, 3),
};

// Elf Lala (shop10): the potions, the Vine and Silk sets, the Wind helm, armour and pants, the
// Town Portal Scroll and the Orb of Healing. **And the Orb of Skillshot after it, which is ours**
// (sprint 15). Her other orbs drop.
constexpr Offer kElfLala[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 3), sip(9, 1, 3), sip(10, 2, 3), sip(11, 3, 3),
    sip(12, 4, 3), sip(13, 5, 3), sip(14, 6, 3), sip(15, 8, 3), gear(16, kHelms, 10, 0),
    gear(18, kHelms, 12, 0), gear(20, kArmours, 10, 0), gear(22, kArmours, 12, 0),
    gear(32, kPants, 10, 0), gear(34, kPants, 12, 0), gear(36, kGloves, 10, 0),
    gear(38, kBoots, 10, 0), gear(48, kHelms, 11, 0), gear(50, kArmours, 11, 0),
    gear(52, kPants, 11, 0), gear(54, kGloves, 11, 0), gear(64, kBoots, 11, 0), sip(66, 10, 1),
    gear(67, kOrbs, 8, 0), gear(68, kOrbs, 21, 0),
};

// Eo the Craftsman (shop9): the bows and crossbows and the arrows and bolts to +2. The source
// sells most bows twice, plain and with the skill; the +S copy of one sold plain is left out
// (the user, 2026-10-01), and the two sold only +S stay.
constexpr Offer kCraftsman[] = {
    gear(0, kBows, 0, 0), gear(4, kBows, 8, 0), gear(20, kBows, 1, 0),
    gear(24, kBows, 9, 0), gear(40, kBows, 2, 0), gear(44, kBows, 10, 0),
    gear(60, kBows, 11, 0, true), gear(62, kBows, 4, 0, true), gear(64, kBows, 7, 0),
    gear(65, kBows, 7, 1), gear(66, kBows, 7, 2), gear(67, kBows, 15, 0), gear(80, kBows, 15, 1),
    gear(81, kBows, 15, 2),
};

// Izabel the Wizard (shop7): the potions, Meteorite, Lightning and Teleport, the Town Portal
// Scroll, the Sphinx pieces with a Bone Armor, and the Serpent Staff.
constexpr Offer kIzabel[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 3), sip(9, 1, 3), sip(10, 2, 3), sip(11, 3, 3),
    sip(12, 4, 3), sip(13, 5, 3), sip(14, 6, 3), sip(15, 8, 3), scroll(16, 1), scroll(17, 2),
    scroll(18, 5), sip(19, 10, 1), gear(20, kHelms, 7, 0), gear(22, kArmours, 4, 0),
    gear(32, kPants, 7, 0), gear(34, kGloves, 7, 0), gear(36, kBoots, 7, 0),
    gear(38, kStaves, 2, 0),
};

// Zienna (shop8): arrows and bolts, Brass helm and pants, Wind gloves and boots, three shields
// and her weapons -- without the +S copies of the two she also sells plain (the user,
// 2026-10-01).
constexpr Offer kZienna[] = {
    gear(0, kBows, 7, 0), gear(1, kBows, 7, 1), gear(2, kBows, 7, 2), gear(3, kBows, 15, 0),
    gear(4, kBows, 15, 1), gear(5, kBows, 15, 2), gear(6, kHelms, 8, 0), gear(16, kPants, 8, 0),
    gear(18, kGloves, 12, 0), gear(20, kBoots, 12, 0), gear(22, kShields, 10, 0),
    gear(32, kShields, 3, 0), gear(34, kShields, 9, 0), gear(36, kBows, 3, 0),
    gear(48, kAxes, 4, 0), gear(49, kSpears, 3, 0),
    gear(59, kAxes, 5, 0), gear(61, kSwords, 8, 0), gear(62, kBows, 11, 0, true),
    gear(80, kSwords, 9, 0), gear(84, kSwords, 5, 0, true),
    gear(85, kSpears, 7, 0, true),
};

template <size_t N>
const Offer* table(const Offer (&t)[N], int* count) {
    *count = int(N);
    return t;
}

// Coin's tables: a plus past four steepens the drop level the price reads, and the rows with
// a written value -- potions, scrolls, orbs, quivers -- are priced off that instead.
int steeper(int r) {
    static const int k[] = {0, 0, 0, 0, 0, 4, 10, 25, 45, 65, 95, 135, 185, 245, 305, 365};
    return r >= 0 && r < 16 ? k[r] : 0;
}
int worth(int number) {
    switch (number) {
        case 0: return 5;
        case 1: case 4: case 8: return 10;
        case 2: case 5: return 20;
        case 3: case 6: case 9: case 10: return 30;
        default: return 0;
    }
}
int spell(int number) {
    static const int k[] = {17000, 11000, 3000, 300, 21000, 5000, 14000, 25000, 35000, 60000,
                            1100, 100000};
    return number >= 0 && number < 12 ? k[number] : 0;
}
int orb(int number) {
    switch (number) {
        case 8: return 800;
        // 9, the Orb of Greater Defense, is not 0.75's 3000 any more: it is the elf's guard and
        // costs what the Orb of Defense and the Scroll of Soul Barrier cost -- the curve at drop
        // level 6, which its row now carries (the user, 2026-09-28; source/items/misc/Gem03.json).
        case 10: return 7000;
        case 11: return 150;
        default: return 0;
    }
}
// The jewels are priced by name, not by the curve: 9M the Bless, 6M the Soul, 810k the Chaos,
// 45M the Life and 36M the Creation -- our Rune -- as WebZen's CItem::Value sets them
// (zzzitem.cpp:1787-1806, 1.00.93; the 1000s above them are a Season 4 trial server's). OpenMU's
// table left them on the curve, where a Bless at drop level 0 sold for ~1,700.
int64_t jewel(int group, int number) {
    if (group == kOrbs) return number == 15 ? 810000 : 0;
    if (group != kPotions) return 0;
    switch (number) {
        case 13: return 9000000;
        case 14: return 6000000;
        case 16: return 45000000;
        case 22: return 36000000;
        default: return 0;
    }
}
// A full quiver by its plus: the arrows 70, 1,200, 2,000, 2,800 and the bolt 100, 1,400,
// 2,200, 3,000 (WebZen 1.00.93 zzzitem.cpp:1702-1760; the +3 is its 2008 flag). Was the +0's
// price at every plus, so a +2 sold for what a +0 did.
int quiver(int number, int refinement) {
    static const int kArrows[4] = {70, 1200, 2000, 2800};
    static const int kBolt[4] = {100, 1400, 2200, 3000};
    const int plus = std::clamp(refinement, 0, 3);
    return number == 15 ? kArrows[plus] : (number == 7 ? kBolt[plus] : 0);
}

// RoundPrice: hundreds above a thousand, tens above a hundred, nothing below.
int64_t round(int64_t price) {
    return price >= 1000 ? price / 100 * 100 : (price >= 100 ? price / 10 * 10 : price);
}

}  // namespace

const Offer* stockOf(int npc, int* count) {
    switch (npc) {
        case 248: return table(kMartin, count);
        case 250: return table(kHarold, count);
        case 251: return table(kBlacksmith, count);
        case 253: return table(kPotionGirl, count);
        case 254: return table(kMage, count);
        case 255: return table(kLumen, count);
        case 244: return table(kBarmaid, count);
        case 245: return table(kIzabel, count);
        case 246: return table(kZienna, count);
        case 242: return table(kElfLala, count);
        case 243: return table(kCraftsman, count);
        default: *count = 0; return nullptr;
    }
}

int64_t buyingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                    int shots, int full, bool luck, int option, int excellent) {
    if (row.group == kBows && quiver(row.number, refinement) > 0) {
        return round(full <= 0 ? 0 : int64_t(quiver(row.number, refinement)) * shots / full);
    }
    if (jewel(row.group, row.number) > 0) return jewel(row.group, row.number);
    if (row.group == kScrolls && spell(row.number) > 0) return round(spell(row.number));
    if (row.group == kOrbs && orb(row.number) > 0) return round(orb(row.number));
    // The pets: `dropLevel^3 + 100`, the branch OpenMU gives group 13 with the capes and the
    // scrolls (ItemPriceCalculator.cs:486-498) -- 12,200 for the Angel, 22,000 for the Imp.
    if (row.group == kPets) {
        const int64_t dropLevel = row.dropLevel + refinement * 3;
        return round(dropLevel * dropLevel * dropLevel + 100);
    }
    if (row.group == kPotions && worth(row.number) > 0) {
        const int64_t value = worth(row.number);
        int64_t price = value * value * 10 / 12;
        // The Ale and the Town Portal Scroll are past the eight OpenMU's branch tests.
        if (row.number > 8) return round(price);
        if (refinement > 0) price *= int64_t(std::pow(2.0, refinement));
        return round(price / 10 * 10 * (pieces > 1 ? pieces : 1));
    }
    // An excellent thing is priced as if it dropped 25 levels deeper (ItemPriceCalculator).
    const int64_t dropLevel =
        row.dropLevel + refinement * 3 + steeper(refinement) + (excellent > 0 ? 25 : 0);
    int64_t reckoned = (dropLevel + 40) * dropLevel * dropLevel / 8 + 100;
    // A fifth off a one-handed weapon or a shield: width is where OpenMU keeps two-handedness.
    if ((row.group < kShields && row.width < 2) || row.group == kShields) {
        reckoned = reckoned * 80 / 100;
    }
    if (skill) reckoned += int64_t(double(reckoned) * 1.5);
    if (luck) reckoned += reckoned * 25 / 100;
    if (option == 1) reckoned += int64_t(double(reckoned) * 0.6);
    if (option > 1) reckoned += int64_t(double(reckoned) * 0.7 * std::pow(2.0, option - 1));
    for (int i = 0; i < excellent; ++i) reckoned += reckoned;
    return round(reckoned);
}

int64_t sellingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                     int shots, int full, bool luck, int option, int excellent) {
    const int64_t price =
        buyingPrice(row, refinement, pieces, skill, shots, full, luck, option, excellent) / 3;
    return row.group == kPotions && row.number <= 8 ? price / 10 * 10 : round(price);
}

}  // namespace mu::sim
