#include "sim/market.h"

#include <cmath>

namespace mu::sim {
namespace {

constexpr int kHelms = 7, kArmours = 8, kPants = 9, kGloves = 10, kBoots = 11, kShields = 6;
constexpr int kSwords = 0, kAxes = 1, kMaces = 2, kBows = 4, kStaves = 5;
constexpr int kOrbs = 12, kPotions = 14, kScrolls = 15;

constexpr Offer gear(int slot, int group, int number, int refinement, bool skill = false) {
    return Offer{slot, group, number, refinement, 0, skill};
}
constexpr Offer sip(int slot, int number, int pieces) {
    return Offer{slot, kPotions, number, 0, pieces, false};
}
constexpr Offer scroll(int slot, int number) { return Offer{slot, kScrolls, number, 0, 0, false}; }

// Potion Girl Amy: each potion twice, a single and a three on the row below. Market.PotionGirl.
constexpr Offer kPotionGirl[] = {
    sip(0, 0, 1),  sip(8, 0, 3),  sip(1, 1, 1),  sip(9, 1, 3),  sip(2, 2, 1),  sip(10, 2, 3),
    sip(3, 3, 1),  sip(11, 3, 3), sip(4, 4, 1),  sip(12, 4, 3), sip(5, 5, 1),  sip(13, 5, 3),
    sip(6, 6, 1),  sip(14, 6, 3), sip(7, 8, 1),  sip(15, 8, 3), gear(16, kBows, 7, 0),
    gear(17, kBows, 15, 0),       sip(18, 10, 1),
};

// Martin and Harold: the same shop in two places. Market.Wandering.
constexpr Offer kWandering[] = {
    gear(0, kHelms, 5, 0),    gear(16, kArmours, 5, 0),  gear(40, kPants, 5, 0),
    gear(56, kBoots, 5, 0),   gear(72, kGloves, 5, 0),   gear(2, kHelms, 0, 2),
    gear(18, kArmours, 0, 2), gear(34, kPants, 0, 2),    gear(50, kBoots, 0, 2),
    gear(66, kGloves, 0, 2),  gear(4, kHelms, 6, 3),     gear(20, kArmours, 6, 3),
    gear(36, kPants, 6, 3),   gear(52, kBoots, 6, 3),    gear(68, kGloves, 6, 3),
    gear(6, kHelms, 8, 3),    gear(22, kArmours, 8, 3),  gear(38, kPants, 8, 3),
    gear(54, kBoots, 8, 3),   gear(70, kGloves, 8, 3),   gear(88, kHelms, 9, 3),
    gear(104, kArmours, 9, 3), gear(82, kPants, 9, 3),   gear(98, kBoots, 9, 3),
    gear(84, kGloves, 9, 3),
};

// Hanzo. Slot 73 twice, as the source has it: the second wins. Market.Blacksmith.
constexpr Offer kBlacksmith[] = {
    gear(0, kShields, 0, 0),          gear(2, kShields, 4, 1, true),  gear(4, kShields, 1, 2),
    gear(6, kShields, 2, 3),          gear(16, kShields, 6, 3, true), gear(18, kShields, 10, 3, true),
    gear(20, kShields, 9, 3, true),   gear(22, kShields, 7, 3, true), gear(32, kShields, 5, 3, true),
    gear(34, kShields, 8, 3, true),   gear(36, kShields, 11, 3, true), gear(38, kShields, 12, 3, true),
    gear(48, kSwords, 1, 0),          gear(49, kAxes, 1, 1),          gear(50, kMaces, 1, 2),
    gear(51, kSwords, 2, 2),          gear(52, kAxes, 2, 2),          gear(53, kSwords, 4, 3, true),
    gear(54, kMaces, 1, 3, true),     gear(55, kAxes, 3, 3, true),    gear(72, kSwords, 0, 2),
    gear(73, kSwords, 6, 3, true),    gear(73, kSwords, 7, 3, true),  gear(74, kSwords, 8, 3),
    gear(75, kSwords, 5, 3, true),
    // **And the knight's nine orbs**, on the two rows below everything else (96 is row 12 of
    // fifteen; the swords above hang down to row 11). Ours, and not MerchantStores': 0.75 has no
    // knight orb to sell because in 0.75 the weapon WAS the skill (docs/skills-dk.md §1.2), and
    // this shelf is the route that replaces it -- bought here, read once, learned for good.
    //
    // In the ladder's own order, which is 0.75's: the level each one asks for is the drop level
    // of the first weapon that carried that skill, so a knight meets his skills in the order MU
    // gave them to him and the row he can afford is usually the row he can read.
    //
    // §3.3 kept Lunge and Slash off the shelf to leave something for the hunt; all nine are sold
    // here for now, because nothing in the tree drops an orb yet and a skill that can only drop
    // is a skill that cannot be tested. The day they drop, those two lines come out.
    gear(96, kOrbs, 3, 0),  gear(97, kOrbs, 4, 0),  gear(98, kOrbs, 5, 0),
    gear(99, kOrbs, 6, 0),  gear(100, kOrbs, 7, 0), gear(101, kOrbs, 25, 0),
    gear(102, kOrbs, 12, 0), gear(103, kOrbs, 20, 0), gear(104, kOrbs, 19, 0),
};

// Pasi: seven scrolls, then the robes and the staves. Market.Mage.
constexpr Offer kMage[] = {
    scroll(0, 3),              scroll(1, 10),             scroll(2, 2),
    scroll(3, 1),              scroll(4, 5),              scroll(5, 6),
    scroll(6, 0),              gear(16, kHelms, 2, 0),    gear(32, kArmours, 2, 0),
    gear(48, kPants, 2, 0),    gear(64, kBoots, 2, 0),    gear(80, kGloves, 2, 0),
    gear(18, kHelms, 4, 2),    gear(34, kArmours, 4, 2),  gear(50, kPants, 4, 2),
    gear(66, kBoots, 4, 2),    gear(82, kGloves, 4, 2),   gear(20, kHelms, 7, 3),
    gear(36, kArmours, 7, 3),  gear(60, kPants, 7, 3),    gear(76, kBoots, 7, 3),
    gear(92, kGloves, 7, 3),   gear(22, kStaves, 0, 0),   gear(46, kStaves, 1, 2),
    gear(70, kStaves, 2, 3),   gear(94, kStaves, 3, 3),
};

// Lumen: the Ale and the Town Portal Scroll. Market.Barmaid.
constexpr Offer kBarmaid[] = {sip(0, 9, 1), sip(1, 10, 1)};

// Elf Lala and Eo the Craftsman, Noria's two. Market.ElfLala and Market.Craftsman.
constexpr Offer kElfLala[] = {
    sip(0, 0, 1),  sip(8, 0, 3),  sip(1, 1, 1),  sip(9, 1, 3),  sip(2, 2, 1),  sip(10, 2, 3),
    sip(3, 3, 1),  sip(11, 3, 3), sip(4, 4, 1),  sip(12, 4, 3), sip(5, 5, 1),  sip(13, 5, 3),
    sip(6, 6, 1),  sip(14, 6, 3), sip(7, 8, 1),  sip(15, 8, 3),
    gear(16, kOrbs, 8, 0), gear(17, kOrbs, 9, 0), gear(18, kOrbs, 10, 0), sip(21, 10, 1),
    gear(22, kBows, 7, 0), gear(23, kBows, 15, 0), gear(24, kOrbs, 11, 0), gear(25, kOrbs, 11, 1),
    gear(26, kOrbs, 11, 2), gear(27, kOrbs, 11, 3), gear(28, kOrbs, 11, 4),
    gear(32, kHelms, 10, 0), gear(34, kArmours, 10, 0), gear(36, kPants, 10, 0),
    gear(38, kGloves, 10, 3), gear(48, kBoots, 10, 3), gear(50, kHelms, 11, 2),
    gear(52, kArmours, 11, 2), gear(54, kPants, 11, 2), gear(64, kGloves, 11, 2),
    gear(66, kBoots, 11, 2), gear(68, kHelms, 12, 3), gear(70, kArmours, 12, 3),
    gear(80, kPants, 12, 3), gear(82, kGloves, 12, 3), gear(84, kBoots, 12, 3),
};
constexpr Offer kCraftsman[] = {
    gear(0, kHelms, 13, 3),   gear(2, kArmours, 13, 3),  gear(4, kPants, 13, 3),
    gear(6, kGloves, 13, 3),  gear(16, kBoots, 13, 3),   gear(18, kHelms, 14, 3),
    gear(20, kArmours, 14, 3), gear(22, kPants, 14, 3),  gear(32, kGloves, 14, 3),
    gear(34, kBoots, 14, 3),  gear(36, kBows, 8, 1, true), gear(38, kBows, 9, 3, true),
    gear(48, kBows, 0, 0, true), gear(50, kBows, 1, 0, true), gear(52, kBows, 2, 2, true),
    gear(54, kBows, 3, 3, true), gear(72, kBows, 11, 3, true), gear(74, kBows, 4, 3, true),
    gear(76, kBows, 10, 3, true), gear(78, kShields, 3, 3),
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
        case 9: return 3000;
        case 10: return 7000;
        case 11: return 150;
        default: return 0;
    }
}
int quiver(int number) { return number == 15 ? 70 : (number == 7 ? 100 : 0); }

// RoundPrice: hundreds above a thousand, tens above a hundred, nothing below.
int64_t round(int64_t price) {
    return price >= 1000 ? price / 100 * 100 : (price >= 100 ? price / 10 * 10 : price);
}

}  // namespace

const Offer* stockOf(int npc, int* count) {
    switch (npc) {
        case 248: case 250: return table(kWandering, count);
        case 251: return table(kBlacksmith, count);
        case 253: return table(kPotionGirl, count);
        case 254: return table(kMage, count);
        case 255: return table(kBarmaid, count);
        case 242: return table(kElfLala, count);
        case 243: return table(kCraftsman, count);
        default: *count = 0; return nullptr;
    }
}

int64_t buyingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                    int shots, int full) {
    if (row.group == kBows && quiver(row.number) > 0) {
        return round(full <= 0 ? 0 : int64_t(quiver(row.number)) * shots / full);
    }
    if (row.group == kScrolls && spell(row.number) > 0) return round(spell(row.number));
    if (row.group == kOrbs && orb(row.number) > 0) return round(orb(row.number));
    if (row.group == kPotions && worth(row.number) > 0) {
        const int64_t value = worth(row.number);
        int64_t price = value * value * 10 / 12;
        // The Ale and the Town Portal Scroll are past the eight OpenMU's branch tests.
        if (row.number > 8) return round(price);
        if (refinement > 0) price *= int64_t(std::pow(2.0, refinement));
        return round(price / 10 * 10 * (pieces > 1 ? pieces : 1));
    }
    const int64_t dropLevel = row.dropLevel + refinement * 3 + steeper(refinement);
    int64_t reckoned = (dropLevel + 40) * dropLevel * dropLevel / 8 + 100;
    // A fifth off a one-handed weapon or a shield: width is where OpenMU keeps two-handedness.
    if ((row.group < kShields && row.width < 2) || row.group == kShields) {
        reckoned = reckoned * 80 / 100;
    }
    if (skill) reckoned += int64_t(double(reckoned) * 1.5);
    return round(reckoned);
}

int64_t sellingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                     int shots, int full) {
    const int64_t price = buyingPrice(row, refinement, pieces, skill, shots, full) / 3;
    return row.group == kPotions && row.number <= 8 ? price / 10 * 10 : round(price);
}

}  // namespace mu::sim
