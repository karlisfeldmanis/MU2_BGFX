#include "sim/market.h"

#include "sim/items.h"

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

// Potion Girl Amy (shop5): the eight potions, single and twenty (the user, 2026-10-23: "sell
// potions on vendor on stacks by 20 and single"; a ten from ByPass's bug #55, 2026-10-22, where
// WebZen sells three: a cell holds a hundred now, sim::kPotionStackMost), arrows and bolts to +2. **Not
// the Town Portal Scroll, on no shelf** (the user, 2026-10-04: "there is no point of town
// portal"): Tab and the warp list take him home.
constexpr Offer kPotionGirl[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 20), sip(9, 1, 20), sip(10, 2, 20), sip(11, 3, 20),
    sip(12, 4, 20), sip(13, 5, 20), sip(14, 6, 20), sip(15, 8, 20), gear(16, kBows, 7, 0),
    gear(17, kBows, 7, 1), gear(18, kBows, 7, 2), gear(19, kBows, 15, 0), gear(20, kBows, 15, 1),
    gear(21, kBows, 15, 2),
};

// Harold (shop3), the wandering merchant east of town: the potions and the Leather set.
constexpr Offer kHarold[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 20), sip(9, 1, 20), sip(10, 2, 20), sip(11, 3, 20),
    sip(12, 4, 20), sip(13, 5, 20), sip(14, 6, 20), sip(15, 8, 20), gear(16, kHelms, 5, 0),
    gear(18, kPants, 5, 0), gear(20, kBoots, 5, 0), gear(22, kGloves, 5, 0),
    gear(32, kArmours, 5, 0),
};

// Martin (shop4), the wandering merchant in the west: the potions, the Bone and Scale pieces
// Hanzo does not sell, Brass gloves and boots, two shields and a rack of weapons. Without the
// source's +S copies of the Gladius, the Falchion and the Arquebus, as Hanzo is without his
// (the user, 2026-10-01): with the skills on the orbs a copy was the same weapon dearer.
constexpr Offer kMartin[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 20), sip(9, 1, 20), sip(10, 2, 20), sip(11, 3, 20),
    sip(12, 4, 20), sip(13, 5, 20), sip(14, 6, 20), sip(15, 8, 20), gear(16, kHelms, 4, 0),
    gear(18, kPants, 4, 0), gear(20, kHelms, 6, 0), gear(22, kArmours, 6, 0),
    gear(32, kPants, 6, 0), gear(34, kGloves, 8, 0), gear(36, kBoots, 8, 0),
    gear(38, kShields, 2, 0), gear(48, kShields, 6, 0), gear(50, kSpears, 2, 0),
    gear(52, kSwords, 3, 0), gear(53, kAxes, 3, 0), gear(54, kSpears, 6, 0),
    gear(64, kMaces, 2, 0), gear(65, kSwords, 6, 0),
    gear(82, kBows, 10, 0), gear(88, kSpears, 1, 0),
    gear(87, kSwords, 7, 0),
};

// Hanzo (shop0): the knight's first weapons, the Bronze set, Scale gloves and boots and three
// shields. **And the knight's four early orbs after them, which are ours**: in 0.75 the weapon
// WAS the skill (docs/game/skills-dk.md §1.2), and this shelf is the route that replaces it -- in the
// ladder's own order, the level each asks being the drop level of the first weapon that carried
// that skill. Only Defense, Uppercut, Falling Slash and Lunge, drop level 20 and under (the user,
// 2026-10-02: "dont sell best orbs but sell only early game orbs and scrolls"); Cyclone, Slash,
// Death Stab and Rageful Blow drop and are not sold. Twisting Slash and Impale Zienna sells in
// Devias (2026-10-07).
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
    gear(71, kOrbs, 6, 0),
};

// Pasi (shop2): Fire Ball and Power Wave, the Pad set, Bone gloves and boots, two staves. **And
// the Scroll of Soul Barrier after the two, which is ours** (the user, 2026-09-28): the wizard's
// guard, priced as the knight's Orb of Defense. **And the Scroll of Meteorite in the column by
// the Pad Armor, ours too** (the user, 2026-10-01: "let Pasi sell Meteorite"): the first spell a
// wizard can buy with a multiplier on it (its force is 2), since the bot's runs showed him
// trailing the knight at a flat one. The other scrolls drop, or Izabel sells them. **Meteorite
// off it again** (the user, 2026-10-11: "lets give meteor for DW when finish lorencia questline",
// "remove twisting slash,multi-shot,meteor from vendors"): Marlon's first clear pays it.
constexpr Offer kMage[] = {
    scroll(0, 3), scroll(1, 10), scroll(2, 15), gear(3, kHelms, 2, 0), gear(5, kArmours, 2, 0),
    gear(16, kPants, 2, 0), gear(18, kGloves, 2, 0), gear(20, kBoots, 2, 0),
    gear(22, kGloves, 4, 0), gear(32, kBoots, 4, 0), gear(34, kStaves, 0, 0),
    gear(35, kStaves, 1, 0),
};

// Caren the Barmaid (shop6): the Ale. The two pets she sold from 2026-09-29 are off every shelf
// (the user, 2026-10-11: "remove also mounts,satan,guardian angle and other very good items from
// vendors so there is actual value for thoses items"): they drop, as WebZen's do, and trade.
constexpr Offer kBarmaid[] = {
    sip(0, 9, 1),
};

// **And Lumen's own, which is ours** (the user, 2026-09-30): the same bar, the Horns of Uniria
// and Dinorant beside the pets (docs/game/mount.md, ours: 0.75 sells neither, and the Dinorant is
// made in a Chaos Machine this game does not have -- the user's pick), with Amy's potions on
// the two rows below it, laid out as Amy lays them, a single over its twenty. Lorencia only --
// Caren keeps the plain bar, since Izabel sells the potions in Devias. **The pets and the mounts
// off it since 2026-10-11**, as off Caren's: the Angel, the Imp and Uniria drop, and the Dinorant
// is made in the Chaos Machine. **And the arrows and
// bolts +0 to +3 under the potions, ours too** (the user, 2026-10-02: "sell arows bolts also in
// lorencia tavern", "+0,+1,+2,+3"): Amy and Eo stop at +2; the +3 is priced off quiver().
constexpr Offer kLumen[] = {
    sip(0, 9, 1),
    sip(16, 0, 1),  sip(24, 0, 20), sip(17, 1, 1),  sip(25, 1, 20), sip(18, 2, 1),  sip(26, 2, 20),
    sip(19, 3, 1),  sip(27, 3, 20), sip(20, 4, 1),  sip(28, 4, 20), sip(21, 5, 1),  sip(29, 5, 20),
    sip(22, 6, 1),  sip(30, 6, 20), sip(23, 8, 1),  sip(31, 8, 20),
    gear(32, kBows, 15, 0), gear(33, kBows, 15, 1), gear(34, kBows, 15, 2), gear(35, kBows, 15, 3),
    gear(36, kBows, 7, 0),  gear(37, kBows, 7, 1),  gear(38, kBows, 7, 2),  gear(39, kBows, 7, 3),
};

// Elf Lala (shop10): the potions, the Vine and Silk sets, the Wind helm, armour and pants, the
// Orb of Healing. The Orb of Skillshot she sold after it (sprint 15) is Peia's first clear's
// since 2026-10-11 (the user: "multi-shot is very poverfull"). **And Greater Defense, Greater Damage and Summoning after those** (the user,
// 2026-10-02: "a lot of orbs for DK but not enough for Elf"): all three early, and all three
// on 0.75's own Lala (OpenMU Version075/MerchantStores.cs:231-243), which WebZen's shop10 lacks.
// **And the Orb of Summoning at +1 to +4 under them** (the user, 2026-10-03: "add some more
// summon orbs in elf shop"): the Stone Golem and the Assassin, of the five stacks 0.75's Lala
// keeps (MerchantStores.cs:239-243). Bali, +5, she never sold, and the Dark Knight, +4, she no
// longer does (the user, 2026-10-03: "remove dark knight orb from shop"), nor the Elite Yeti, +3
// (the user, 2026-10-19: "lets remove elite yeti orb from vendor on noria").
constexpr Offer kElfLala[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 20), sip(9, 1, 20), sip(10, 2, 20), sip(11, 3, 20),
    sip(12, 4, 20), sip(13, 5, 20), sip(14, 6, 20), sip(15, 8, 20), gear(16, kHelms, 10, 0),
    gear(18, kHelms, 12, 0), gear(20, kArmours, 10, 0), gear(22, kArmours, 12, 0),
    gear(32, kPants, 10, 0), gear(34, kPants, 12, 0), gear(36, kGloves, 10, 0),
    gear(38, kBoots, 10, 0), gear(48, kHelms, 11, 0), gear(50, kArmours, 11, 0),
    gear(52, kPants, 11, 0), gear(54, kGloves, 11, 0), gear(64, kBoots, 11, 0),
    gear(67, kOrbs, 8, 0), gear(69, kOrbs, 9, 0), gear(70, kOrbs, 10, 0),
    gear(71, kOrbs, 11, 0), gear(76, kOrbs, 11, 1), gear(77, kOrbs, 11, 2),
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

// Izabel the Wizard (shop7): the potions, Lightning and Teleport, the Sphinx pieces with a Bone
// Armor, and the Serpent Staff. WebZen's shop7 sells Meteorite too, off it since 2026-10-11 as off
// Pasi's (the user: "remove twisting slash,multi-shot,meteor from vendors").
constexpr Offer kIzabel[] = {
    sip(0, 0, 1), sip(1, 1, 1), sip(2, 2, 1), sip(3, 3, 1), sip(4, 4, 1), sip(5, 5, 1),
    sip(6, 6, 1), sip(7, 8, 1), sip(8, 0, 20), sip(9, 1, 20), sip(10, 2, 20), sip(11, 3, 20),
    sip(12, 4, 20), sip(13, 5, 20), sip(14, 6, 20), sip(15, 8, 20), scroll(17, 2),
    scroll(18, 5), gear(20, kHelms, 7, 0), gear(22, kArmours, 4, 0),
    gear(32, kPants, 7, 0), gear(34, kGloves, 7, 0), gear(36, kBoots, 7, 0),
    gear(38, kStaves, 2, 0),
};

// Zienna (shop8): arrows and bolts, Brass helm and pants, Wind gloves and boots, three shields
// and her weapons -- without the +S copies of the two she also sells plain (the user,
// 2026-10-01). **And two knight's orbs in the free cells by the shields, ours** (the user,
// 2026-10-07: 'sell some DK orbs on zienna vendor at devias like impale and twisting slash'):
// Impale and Twisting Slash, past Hanzo's four early ones, which still drop as well. **Twisting
// Slash off it again** (the user, 2026-10-11): the Golden Archer's first clear pays it, and it drops.
constexpr Offer kZienna[] = {
    gear(0, kBows, 7, 0), gear(1, kBows, 7, 1), gear(2, kBows, 7, 2), gear(3, kBows, 15, 0),
    gear(4, kBows, 15, 1), gear(5, kBows, 15, 2), gear(6, kHelms, 8, 0), gear(16, kPants, 8, 0),
    gear(18, kGloves, 12, 0), gear(20, kBoots, 12, 0), gear(22, kShields, 10, 0),
    gear(32, kShields, 3, 0), gear(34, kShields, 9, 0), gear(36, kBows, 3, 0),
    gear(38, kOrbs, 18, 0),
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
        // level 6, which its row now carries (the user, 2026-09-28; content/items/misc/Gem03.json).
        case 10: return 7000;
        case 11: return 150;
        default: return 0;
    }
}
// The jewels are priced by name, not by the curve, at MU's old values since 2026-10-11 (the user:
// "i dont like idea that something simple as jewels cost millions"): 100k the Bless, 70k the Soul,
// 40k the Chaos, OpenMU's SpecialItemOldValueDictionary and the machine's own (sim::mixValue);
// 500k the Life and 400k the Creation -- our Rune -- ours, in proportion to the Bless; the Life
// 250k since 2026-10-13 (the user: "jewel of life sell price is very hight", then "price has to be
// decent hight but not to crazy because you can farm it in BC"). They were
// WebZen's CItem::Value (zzzitem.cpp:1787-1806, 1.00.93): 9M, 6M, 810k, 45M and 36M, so every
// jewel a merchant took paid a third of that and Zen flooded the realm.
int64_t jewel(int group, int number) {
    if (group == kOrbs) return number == 15 ? 40000 : 0;
    if (group != kPotions) return 0;
    switch (number) {
        case 13: return 100000;
        case 14: return 70000;
        case 16: return 250000;
        case 22: return 400000;
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

bool stocked(int group, int number) {
    for (const int npc : {248, 250, 251, 253, 254, 255, 244, 245, 246, 242, 243}) {
        int count = 0;
        const Offer* shelf = stockOf(npc, &count);
        for (int i = 0; i < count; ++i) {
            if (shelf[i].group == group && shelf[i].number == number) return true;
        }
    }
    return false;
}

int64_t buyingPrice(const content::ItemRow& row, int refinement, int pieces, bool skill,
                    int shots, int full, bool luck, int option, int excellent) {
    if (row.group == kBows && quiver(row.number, refinement) > 0) {
        return round(full <= 0 ? 0 : int64_t(quiver(row.number, refinement)) * shots / full);
    }
    // A stack of jewels (they stack since 2026-10-20) is that many jewels' price.
    if (jewel(row.group, row.number) > 0) return jewel(row.group, row.number) * std::max(1, pieces);
    if (row.group == kScrolls && spell(row.number) > 0) return round(spell(row.number));
    if (row.group == kOrbs && orb(row.number) > 0) return round(orb(row.number));
    // The pets: `dropLevel^3 + 100`, the branch OpenMU gives group 13 with the capes and the
    // scrolls (ItemPriceCalculator.cs:486-498) -- 12,200 for the Angel, 22,000 for the Imp.
    // A ring and a pendant on the same branch, times one and its option (WebZen's
    // `Gold += Gold * m_Option3` for AT_LIFE_REGENERATION, zzzitem.cpp:2532).
    // The Dark Horse is priced by its level, 2,000,000 a level, and sold for a third (WebZen
    // CItem::PetValue, zzzitem.cpp; BUGFIX_SELL_PRICE_OF_DARKLOAD_PETS_20050310): 2,000,000 at
    // the level 1 every horse is here (sim::kHorseLevel).
    if (darkHorse(row)) return round(int64_t(kHorseLevel) * 2000000);
    if (row.group == kPets) {
        const int64_t dropLevel = row.dropLevel + refinement * 3;
        const int64_t base = dropLevel * dropLevel * dropLevel + 100;
        return round(jewellery(row) ? base * (1 + std::max(0, option)) : base);
    }
    if (row.group == kPotions && worth(row.number) > 0) {
        const int64_t value = worth(row.number);
        int64_t price = value * value * 10 / 12;
        // The Ale and the Town Portal Scroll are past the eight OpenMU's branch tests.
        if (row.number > 8) return round(price);
        if (refinement > 0) price *= int64_t(std::pow(2.0, refinement));
        return round(price / 10 * 10 * (pieces > 1 ? pieces : 1));
    }
    // A 1st level wing: `40000000 + (40 + Level2) * Level2^2 * 11` (WebZen zzzitem.cpp:2485-2488),
    // its luck and option then as anything's.
    // The 2nd wings on the same line (:2485-2488 takes 12/0-6 alike), from their drop level 150.
    if (anyWing(row)) {
        const int64_t level = row.dropLevel + refinement * 3 + steeper(refinement);
        int64_t wing = 40000000LL + (40 + level) * level * level * 11;
        if (luck) wing += wing * 25 / 100;
        if (option == 1) wing += int64_t(double(wing) * 0.6);
        if (option > 1) wing += int64_t(double(wing) * 0.7 * std::pow(2.0, option - 1));
        return round(wing);
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
                     int shots, int full, bool, int, int) {
    // The plain thing only: no luck, option or excellent options are paid for (the user,
    // 2026-10-11: "Merchants pay only for the plain item: no extra for excellent options, luck or
    // sockets"), so a merchant is no mint for a drop nobody wants. Ours; the plus still counts.
    const int64_t price = buyingPrice(row, refinement, pieces, skill, shots, full) / 3;
    return row.group == kPotions && row.number <= 8 ? price / 10 * 10 : round(price);
}

}  // namespace mu::sim
