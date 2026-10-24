// drops.json: what one kill leaves, per monster and per place, worked out analytically.
//
// Realm::leave (src/sim/realm_items.cpp) rolls several independent dice and one main roll; this
// file walks the same branches in the same order and asks the game's own predicates and
// constants at each (dropRateOf, rulesOf, excellentable, secondClassOnly, kFeatherOdds, ...).
// What leave() keeps in a local lambda is mirrored here under the lambda's own name (reaches,
// jewelGroup, plusOf, the item pool's admits), and so are eliteSpoils, minionSpoils,
// dragonHoard, openBox and crack: --check-odds holds this against the real roll.
//
// The baseline only: one killer alone, no item find (Ring of Fortune), no Fortune talents, no
// party or raider credit.

#include <algorithm>
#include <cmath>
#include <map>

#include "dump.h"
#include "sim/elite.h"
#include "sim/event.h"
#include "sim/invasion.h"
#include "sim/items.h"
#include "sim/machine.h"
#include "sim/maps.h"
#include "sim/quests.h"
#include "sim/raid.h"
#include "sim/realm_tuning.h"
#include "sim/square.h"
#include "sim/wear.h"

namespace armory {
namespace {

using content::ItemRow;
using content::Tables;

// One way an item comes off a kill.
struct Roll {
    enum class Kind : uint8_t { Ticket, Main, Own } kind = Kind::Own;
    std::string how;
    double chance = 0.0;  // per kill, unconditional
    int plusLow = 0, plusHigh = 0;
    double luck = -1.0;            // the chance it is lucky, -1 when it takes none
    double option[4] = {-1, 0, 0, 0};  // option level 0..3, -1 in [0] when it takes none
    double sockets = -1.0;         // at least one socket
    double twoOptions = -1.0;      // an excellent's second option
    bool excellent = false;
    int kin = -1;                  // only for a killer of this class
    int count = 1;                 // how many of it the roll lays
    double powers[4] = {-1, 0, 0, 0};  // a powered piece's count of powers, 1..4
};

struct Model {
    double pass = 1.0;  // no ticket took the kill
    double zen = 0.0;
    std::map<int32_t, std::vector<Roll>> items;
    // An item whose rolls are not independent (an Elite's pieces share their count): its exact
    // at-least-one, which the total takes in place of the rolls'.
    std::map<int32_t, double> exact;
    J notes = J::arr();
    J whileQuest = J::arr();
    void add(int32_t item, Roll roll) {
        if (item < 0 || roll.chance <= 0.0) return;
        items[item].push_back(std::move(roll));
    }
};

// The chance one kill leaves at least one, for a killer of `kin` (-1: any roll counts).
double total(const Model& m, int32_t item, const std::vector<Roll>& rolls, int kin) {
    if (const auto it = m.exact.find(item); it != m.exact.end()) return it->second;
    double ticket = 0.0, main = 0.0, none = 1.0;
    for (const Roll& r : rolls) {
        if (kin >= 0 && r.kin >= 0 && r.kin != kin) continue;
        if (r.kind == Roll::Kind::Ticket) ticket += r.chance;
        else if (r.kind == Roll::Kind::Main) main += r.chance / m.pass;
        else none *= 1.0 - r.chance / m.pass;
    }
    return ticket + m.pass * (1.0 - (1.0 - main) * none);
}

// rollOptions (leave's lambda): luck under `luckIn100`, then one of three draws for +12, +8, +4.
void dropOptions(Roll& r, int luckIn100) {
    r.luck = luckIn100 / 100.0;
    r.option[0] = 1.0;
    for (int which = 0; which < 3; ++which) {
        const double p = (1.0 / 3.0) * sim::kOptionUnder[which] / 100.0;
        r.option[sim::kMostOptionDropped - which] += p;
        r.option[0] -= p;
    }
}

// The Elite's `option`: the same draw twice, the better kept.
void eliteOptions(Roll& r) {
    Roll one;
    dropOptions(one, 0);
    r.luck = sim::kEliteLuckIn100 / 100.0;
    double below[5] = {0, one.option[0], 0, 0, 1.0};  // P(option <= level)
    below[2] = below[1] + one.option[1];
    below[3] = below[2] + one.option[2];
    r.option[0] = below[1] * below[1];
    for (int l = 1; l <= 3; ++l) r.option[l] = (l == 3 ? 1.0 : below[l + 1] * below[l + 1]) - below[l] * below[l];
}

// A box's (crack, openBox): the luck at `luck`, the skill at a half, and the option where either
// missed: one in five +12, else +0, +4 or +8. `skill` false: excellentOf's, the option where the
// luck missed.
void boxOptions(Roll& r, double luck, bool skill) {
    r.luck = luck;
    const double drawn = skill ? 1.0 - luck * 0.5 : 1.0 - luck;
    r.option[0] = 1.0 - drawn + drawn * (4.0 / 5.0) / 3.0;
    r.option[1] = drawn * (4.0 / 5.0) / 3.0;
    r.option[2] = drawn * (4.0 / 5.0) / 3.0;
    r.option[3] = drawn * (1.0 / 5.0);
}

// NewOptionRand's second option, as leave, eliteSpoils and excellentOf roll it.
double secondOption() {
    return (1.0 / 4.0) * (double(sim::kExcellentOptions - 1) / sim::kExcellentOptions);
}

void socketsOf(Roll& r, const ItemRow& row) {
    if (sim::takesSockets(row)) r.sockets = sim::kSocketChance;
}

// rollAffixes (leave's lambda): how many powers a powered piece takes at this level.
void affixCount(Roll& r, const ItemRow& row, int level) {
    if (!sim::powered(row)) return;
    double held = 0.0;
    for (int c = 0; c < 4; ++c) held += level >= sim::kAffixCountLevel[c] ? sim::kAffixCountShare[c] : 0.0;
    r.powers[0] = 0.0;
    for (int c = 0; c < 4; ++c) {
        r.powers[c] = level >= sim::kAffixCountLevel[c] ? sim::kAffixCountShare[c] / held : 0.0;
    }
}

// The first row the predicate takes, as leave's `for ... break` loops find it.
template <class F>
int32_t first(const Tables& t, F&& admits) {
    for (size_t i = 0; i < t.items.size(); ++i) {
        if (admits(t.items[i])) return int32_t(i);
    }
    return -1;
}
template <class F>
std::vector<int32_t> pool(const Tables& t, F&& admits) {
    std::vector<int32_t> out;
    for (size_t i = 0; i < t.items.size(); ++i) {
        if (admits(t.items[i])) out.push_back(int32_t(i));
    }
    return out;
}

// leave's `jewelGroup`.
bool jewelGroup(const ItemRow& r) {
    return (r.jewel() || (r.group == sim::kGroupPotions && (r.number == 9 || r.number == 10))) &&
           !sim::refiningJewel(r);
}

// What the context of a kill is: where, and which monster.
struct Kill {
    const Tables* t = nullptr;
    uint32_t map = 0;
    sim::EventKind event = sim::EventKind::None;
    int castle = 0;
    int level = 0;
    int32_t number = 0;
    sim::DropRate rate{};
};

// Realm::treasure: a Find's thing, while its quest is active, beside all else.
void treasure(const Kill& k, Model& m) {
    const bool event = k.map == sim::kBloodCastleMap || k.map == sim::kDevilSquareMap;
    const bool strong = event && k.level >= sim::kTreasureLevel;
    const sim::Treasure ground = sim::rulesOf(k.map).treasure;
    if (!strong && ground == sim::Treasure::None) return;
    for (int index = 0; index < sim::kQuests; ++index) {
        const sim::QuestRow& row = sim::questAt(index);
        for (int step = 0; step < row.stepCount; ++step) {
            const sim::QuestStepRow& want = row.steps[step];
            if (want.kind != sim::QuestStepKind::Find || !want.item) continue;
            const int32_t item = k.t->itemNamed(want.item);
            if (item < 0) continue;
            J classes = J::arr();
            for (sim::Kin kin : kKins) {
                if (sim::questNative(row, int(kin))) classes.push(kinName(kin));
            }
            J one = J::obj();
            one.set("item", k.t->items[size_t(item)].name)
                .set("quest", row.key)
                .set("chance", J::p(sim::kTreasureIn10000 / 10000.0))
                .set("classes", classes)
                .set("where", strong ? "any kill of this level" : ground == sim::Treasure::Map ? "the whole map" : "the last floor");
            m.whileQuest.push(one);
        }
    }
}

// Realm::leave, for a monster that is not an Elite.
void leave(const Kill& k, Model& m) {
    const Tables& t = *k.t;
    const int level = k.level;
    treasure(k, m);
    // Blood Castle's scroll, then its bone, anywhere but a castle; one that lands is the drop.
    if (!sim::bloodCastleMap(k.map)) {
        const double scroll = sim::kScrollOfArchangelIn10000 / 10000.0;
        const double bone = (1.0 - scroll) * sim::kBloodBoneIn10000 / 10000.0;
        const int32_t scrollItem = t.itemAt(sim::kGroupPets, 16), boneItem = t.itemAt(sim::kGroupPets, 17);
        const int plus = sim::castleMaterialLevel(level);
        if (scrollItem >= 0) {
            Roll r{Roll::Kind::Ticket, "scroll", scroll * m.pass, plus, plus};
            m.add(scrollItem, r);
        }
        if (boneItem >= 0) {
            Roll r{Roll::Kind::Ticket, "bone", bone * m.pass, plus, plus};
            m.add(boneItem, r);
        }
        if (scrollItem >= 0) m.pass *= 1.0 - scroll;
        if (boneItem >= 0) m.pass *= 1.0 - sim::kBloodBoneIn10000 / 10000.0;
    }
    // Devil Square's eye, then its key, anywhere but the square, from a monster over 15.
    if (k.event != sim::EventKind::Square && sim::devilTicketLevel(level) > 0) {
        const double eye = sim::kDevilsEyeIn10000 / 10000.0;
        const double key = (1.0 - eye) * sim::kDevilsKeyIn10000 / 10000.0;
        const int32_t eyeItem = t.itemAt(sim::kGroupPotions, 17), keyItem = t.itemAt(sim::kGroupPotions, 18);
        const int plus = sim::devilTicketLevel(level);
        if (eyeItem >= 0) m.add(eyeItem, Roll{Roll::Kind::Ticket, "eye", eye * m.pass, plus, plus});
        if (keyItem >= 0) m.add(keyItem, Roll{Roll::Kind::Ticket, "key", key * m.pass, plus, plus});
        if (eyeItem >= 0) m.pass *= 1.0 - eye;
        if (keyItem >= 0) m.pass *= 1.0 - sim::kDevilsKeyIn10000 / 10000.0;
    }
    const double pass = m.pass;
    // The separate rolls, each its own dice, beside the main roll.
    if (level >= sim::kFirecrackerFromLevel && sim::firecrackerMap(k.map)) {
        m.add(first(t, [](const ItemRow& r) { return sim::firecracker(r); }),
              Roll{Roll::Kind::Own, "firecracker", pass / sim::kFirecrackerOdds});
    }
    const bool featherHere = sim::featherMap(k.map) || (k.event == sim::EventKind::Castle && k.castle == sim::kFeatherCastle);
    if (level >= sim::kFeatherFromLevel && featherHere) {
        m.add(first(t, [](const ItemRow& r) { return sim::lochsFeather(r); }),
              Roll{Roll::Kind::Own, "feather", pass / sim::kFeatherOdds});
    }
    if (level >= sim::kNovaScrollFromLevel && sim::novaScrollMap(k.map)) {
        m.add(first(t, [](const ItemRow& r) { return sim::scrollOfNova(r); }),
              Roll{Roll::Kind::Own, "nova_scroll", pass / sim::kNovaScrollOdds});
    }
    if (sim::rulesOf(k.map).firstWings) {
        for (sim::Kin kin : kKins) {
            const int32_t item = t.itemAt(12, sim::firstWingOf(kin));
            if (item < 0) continue;
            Roll r{Roll::Kind::Own, "first_wings", pass / sim::kIcarusWingOdds};
            r.kin = int(kin);
            r.luck = sim::kLuckIn100 / 100.0;
            m.add(item, r);
        }
    }
    if (sim::rulesOf(k.map).secondGear && level >= sim::kAtlansGearFromLevel) {
        const std::vector<int32_t> gear =
            pool(t, [](const ItemRow& r) { return r.dropsFromMonsters() && sim::secondClassOnly(r); });
        for (const int32_t item : gear) {
            Roll r{Roll::Kind::Own, "second_gear", pass / sim::kAtlansGearOdds / double(gear.size())};
            dropOptions(r, sim::kLuckIn100);
            m.add(item, r);
        }
    }
    {
        const int32_t orb = first(t, [&](const ItemRow& r) { return sim::summoningOrb(r) && r.dropLevel <= level; });
        if (orb >= 0) {
            const int plus = std::min((level - t.items[size_t(orb)].dropLevel) / sim::kSummonOrbLevelsAPlus,
                                      sim::kSummonOrbMostPlus);
            m.add(orb, Roll{Roll::Kind::Own, "summoning_orb", pass / sim::kSummonOrbOdds, plus, plus});
        }
    }
    if (level >= sim::kLifeFromLevel) {
        m.add(first(t, [](const ItemRow& r) { return sim::refiningJewel(r) && sim::jewelOf(r) == sim::Jewel::Life; }),
              Roll{Roll::Kind::Own, "life", pass / sim::kLifeOdds});
    }

    // The main roll: the rune, the refining jewel, the jewels' group, an excellent, an item; else
    // Zen at the breed's rate, or nothing.
    const bool secondGearHere =
        sim::rulesOf(k.map).secondClass || (k.event == sim::EventKind::Castle && k.castle >= sim::kCastles);
    // leave's `reaches`.
    const auto reaches = [&](const ItemRow& row) {
        return row.dropLevel <= level && (row.maximumDropLevel == 0 || level <= row.maximumDropLevel) &&
               (secondGearHere || !sim::secondClassOnly(row));
    };
    const double creationChance = level >= sim::kRuneRarityLevel[int(sim::Rarity::Common)] ? sim::kCreationChance : 0.0;
    const double itemChance = 0.1;
    const double excellentItemChance = 0.1 * sim::kExcellentShareOfItem;
    double spent = 0.0;
    {
        const auto runes = pool(t, [](const ItemRow& r) { return sim::creation(r); });
        for (const int32_t item : runes) {
            m.add(item, Roll{Roll::Kind::Main, "rune", pass * creationChance / double(runes.size())});
        }
        spent += creationChance;
    }
    {
        const auto jewels = pool(t, [&](const ItemRow& r) {
            return sim::refiningJewel(r) && sim::jewelOf(r) != sim::Jewel::Life && reaches(r);
        });
        for (const int32_t item : jewels) {
            m.add(item, Roll{Roll::Kind::Main, "jewel", pass * sim::kJewelChance / double(jewels.size())});
        }
        spent += sim::kJewelChance;
    }
    {
        const auto group = pool(t, [&](const ItemRow& r) { return jewelGroup(r) && reaches(r); });
        for (const int32_t item : group) {
            m.add(item, Roll{Roll::Kind::Main, "jewel_group", pass * sim::kJewelGroupChance / double(group.size())});
        }
        spent += sim::kJewelGroupChance;
    }
    {
        const int lower = level - sim::kExcellentLevelDelta;
        constexpr int kGap = 15;  // leave's own kGap
        if (lower >= 0) {
            const auto fine = pool(t, [&](const ItemRow& r) {
                return r.dropsFromMonsters() && sim::excellentable(r) && r.dropLevel <= lower &&
                       (r.maximumDropLevel == 0 || lower <= r.maximumDropLevel) && r.dropLevel >= lower - kGap &&
                       (secondGearHere || !sim::secondClassOnly(r));
            });
            for (const int32_t item : fine) {
                const ItemRow& row = t.items[size_t(item)];
                Roll r{Roll::Kind::Main, "excellent", pass * excellentItemChance / double(fine.size())};
                r.excellent = true;
                dropOptions(r, sim::kExcellentLuckIn100);
                affixCount(r, row, level);
                r.twoOptions = secondOption();
                m.add(item, r);
            }
        }
        spent += excellentItemChance;
    }
    {
        constexpr int kGap = 15, kPotionGap = 8;  // leave's kGap and kPotionGap
        // leave's `plusOf`.
        const auto plusOf = [&](const ItemRow& r) {
            const bool refinable = r.group <= sim::kGroupBoots && !sim::ammunition(r);
            if (sim::jewellery(r)) return std::clamp((level - r.dropLevel) / 3, 0, sim::kJewelleryMostPlus);
            return refinable ? std::clamp((level - r.dropLevel) / 3, 0, sim::kRefineCap) : 0;
        };
        // The item pool's `admits`, with the deep share and without.
        const auto admits = [&](const ItemRow& r, bool deep) {
            const int gap = r.group == sim::kGroupPotions ? kPotionGap : kGap;
            return r.dropsFromMonsters() && !sim::summoningOrb(r) && reaches(r) &&
                   (r.dropLevel >= level - gap || (deep && sim::powered(r))) && plusOf(r) <= k.rate.maxPlus;
        };
        const bool deepHere = level - kGap > sim::kDeepJewelleryFrom;
        const double shares[2] = {deepHere ? 1.0 - sim::kDeepJewellery : 1.0, deepHere ? sim::kDeepJewellery : 0.0};
        std::map<int32_t, double> chance;
        for (int deep = 0; deep < 2; ++deep) {
            if (shares[deep] <= 0.0) continue;
            const auto items = pool(t, [&](const ItemRow& r) { return admits(r, deep == 1); });
            for (const int32_t item : items) chance[item] += pass * itemChance * shares[deep] / double(items.size());
        }
        for (const auto& [item, p] : chance) {
            const ItemRow& row = t.items[size_t(item)];
            const int plus = plusOf(row);
            Roll r{Roll::Kind::Main, "item", p, plus, plus};
            if (sim::jewellery(row) || sim::takesOptions(row)) dropOptions(r, sim::kLuckIn100);
            affixCount(r, row, level);
            socketsOf(r, row);
            m.add(item, r);
        }
        spent += itemChance;
    }
    m.zen = pass * (1.0 - spent) * std::min(1.0, 10.0 / double(std::max(1, k.rate.moneyRate)));
}

// Realm::eliteSpoils: four to seven pieces of gear, one or two of them excellent, and a jewel or
// two; nothing of leave's roll.
void eliteSpoils(const Kill& k, Model& m) {
    const Tables& t = *k.t;
    const int level = k.level + sim::kEliteLevelsUp;
    constexpr int kGap = 15;  // eliteSpoils' own
    const bool secondGearHere = sim::rulesOf(k.map).secondClass;
    // eliteSpoils' `gear`.
    const auto gear = [&](const ItemRow& r) {
        return r.dropsFromMonsters() && r.group <= sim::kGroupBoots && !sim::ammunition(r) && !sim::jewellery(r) &&
               !sim::summoningOrb(r) && r.dropLevel <= level && r.dropLevel >= level - kGap &&
               (r.maximumDropLevel == 0 || level <= r.maximumDropLevel) && (secondGearHere || !sim::secondClassOnly(r));
    };
    const auto plain = pool(t, gear);
    const auto fine = pool(t, [&](const ItemRow& r) { return gear(r) && sim::excellentable(r); });
    const int counts = sim::kEliteMostPieces - sim::kEliteLeastPieces + 1;
    const double second = sim::kEliteSecondExcellentIn100 / 100.0;
    for (const int32_t item : plain) {
        const ItemRow& row = t.items[size_t(item)];
        const double a = 1.0 / double(plain.size());
        const bool inFine = std::find(fine.begin(), fine.end(), item) != fine.end();
        const double b = inFine ? 1.0 / double(fine.size()) : 0.0;
        double nonePlain = 0.0, noneFine = 0.0, none = 0.0;
        for (int n = sim::kEliteLeastPieces; n <= sim::kEliteMostPieces; ++n) {
            for (int e = 1; e <= 2; ++e) {
                const double w = (e == 1 ? 1.0 - second : second) / counts;
                nonePlain += w * std::pow(1.0 - a, n - e);
                noneFine += w * std::pow(1.0 - b, e);
                none += w * std::pow(1.0 - a, n - e) * std::pow(1.0 - b, e);
            }
        }
        const int plus = std::clamp((level - row.dropLevel) / 3, 0, std::min(sim::kRefineCap, k.rate.maxPlus));
        Roll r{Roll::Kind::Own, "elite_piece", 1.0 - nonePlain, plus, plus};
        if (sim::takesOptions(row)) eliteOptions(r);
        socketsOf(r, row);
        m.add(item, r);
        if (inFine) {
            Roll x{Roll::Kind::Own, "elite_excellent", 1.0 - noneFine};
            x.excellent = true;
            if (sim::takesOptions(row)) eliteOptions(x);
            socketsOf(x, row);
            x.twoOptions = secondOption();
            m.add(item, x);
        }
        // The two share the piece count, so the item's total is not their product.
        m.exact[item] = 1.0 - none;
    }
    const auto jewels = pool(t, [](const ItemRow& r) { return sim::refiningJewel(r) && sim::jewelOf(r) != sim::Jewel::Life; });
    const double one = sim::kEliteJewelIn100 / 100.0, two = one * sim::kEliteSecondJewelIn100 / 100.0;
    for (const int32_t item : jewels) {
        const double miss = 1.0 - 1.0 / double(jewels.size());
        const double none = (1.0 - one) + (one - two) * miss + two * miss * miss;
        m.add(item, Roll{Roll::Kind::Own, "elite_jewel", 1.0 - none});
    }
    m.notes.push("an Elite leaves its spoils at its level + kEliteLevelsUp and nothing of the ordinary roll");
}

// Realm::minionSpoils, for a Golden Budge Dragon of the raid, beside its leave().
void minionSpoils(const Kill& k, Model& m) {
    const Tables& t = *k.t;
    m.add(first(t, [](const ItemRow& r) { return sim::boxOfLuck(r); }), Roll{Roll::Kind::Own, "raid_minion", 1.0});
    const auto jewels = pool(t, [](const ItemRow& r) { return sim::refiningJewel(r); });
    for (const int32_t item : jewels) {
        m.add(item, Roll{Roll::Kind::Own, "raid_minion", 1.0 / sim::kMinionJewelOdds / double(jewels.size())});
    }
    const auto runes = pool(t, [](const ItemRow& r) { return sim::creation(r); });
    for (const int32_t item : runes) {
        m.add(item, Roll{Roll::Kind::Own, "raid_minion", 1.0 / sim::kMinionRuneOdds / double(runes.size())});
    }
}

// Rows of a box's bag this tree has, as crack/openBox's rowOf counts them (duplicates twice).
std::vector<int32_t> bagRows(const Tables& t, const sim::BagRow* rows, size_t count, bool onlyFine) {
    std::vector<int32_t> out;
    for (size_t i = 0; i < count; ++i) {
        for (size_t k = 0; k < t.items.size(); ++k) {
            const ItemRow& row = t.items[k];
            if (row.group != rows[i].group || row.number != rows[i].number) continue;
            if (onlyFine && !(sim::excellentable(row) && sim::takesOptions(row))) continue;
            out.push_back(int32_t(k));
            break;
        }
    }
    return out;
}

// Realm::dragonHoard, for the raid's Golden Dragon, beside its leave().
void dragonHoard(const Kill& k, Model& m) {
    const Tables& t = *k.t;
    const int32_t box = first(t, [](const ItemRow& r) { return sim::boxOfKundun(r); });
    if (box >= 0) {
        Roll r{Roll::Kind::Own, "raid_hoard", 1.0, 1, sim::kKundunTiers};
        r.count = sim::kHoardBoxes;
        m.add(box, r);
    }
    const int32_t rune = first(t, [](const ItemRow& r) { return sim::creation(r); });
    if (rune >= 0) {
        Roll r{Roll::Kind::Own, "raid_hoard", 1.0};
        r.count = sim::kHoardRunes;
        m.add(rune, r);
    }
    const auto jewels = pool(t, [](const ItemRow& r) { return sim::refiningJewel(r); });
    for (const int32_t item : jewels) {
        m.add(item, Roll{Roll::Kind::Own, "raid_hoard",
                         1.0 - std::pow(1.0 - 1.0 / double(jewels.size()), sim::kHoardJewels)});
    }
    const auto fine = bagRows(t, sim::kKundunExcellent3, std::size(sim::kKundunExcellent3), true);
    const double more = 1.0 / sim::kHoardExcellentOdds;
    for (const int32_t item : fine) {
        const double miss = 1.0 - double(std::count(fine.begin(), fine.end(), item)) / double(fine.size());
        Roll r{Roll::Kind::Own, "raid_hoard", 1.0 - ((1.0 - more) * std::pow(miss, sim::kHoardExcellents) +
                                                      more * std::pow(miss, sim::kHoardExcellents + 1))};
        r.excellent = true;
        boxOptions(r, sim::kBoxLuckIn100 / 100.0, false);
        r.twoOptions = secondOption();
        m.add(item, r);
    }
    m.add(first(t, [](const ItemRow& r) { return sim::lochsFeather(r); }),
          Roll{Roll::Kind::Own, "raid_hoard", 1.0 / sim::kHoardFeatherOdds});
}

J rollJson(const Roll& r) {
    J o = J::obj();
    o.set("roll", r.how).set("chance", J::p(r.chance));
    if (r.plusHigh > 0 || r.plusLow > 0) {
        if (r.plusLow == r.plusHigh) o.set("plus", r.plusLow);
        else {
            J range = J::arr();
            range.push(r.plusLow);
            range.push(r.plusHigh);
            o.set("plus", range);
        }
    }
    if (r.luck >= 0.0) o.set("luck", J::p(r.luck));
    if (r.option[0] >= 0.0) {
        J opt = J::obj();
        for (int l = 1; l <= 3; ++l) opt.set(std::to_string(l), J::p(r.option[l]));
        o.set("option", opt);
    }
    if (r.sockets >= 0.0) o.set("sockets", J::p(r.sockets));
    if (r.excellent) o.set("excellent", true);
    if (r.twoOptions >= 0.0) o.set("two_options", J::p(r.twoOptions));
    if (r.kin >= 0) o.set("class", kinName(sim::Kin(r.kin)));
    if (r.count > 1) o.set("count", r.count);
    if (r.powers[0] >= 0.0) {
        J p = J::obj();
        for (int c = 0; c < 4; ++c) {
            if (r.powers[c] > 0.0) p.set(std::to_string(c + 1), J::p(r.powers[c]));
        }
        o.set("powers", p);
    }
    return o;
}

J modelJson(const Tables& t, const Model& m) {
    J items = J::arr();
    for (const auto& [item, rolls] : m.items) {
        J one = J::obj();
        bool classed = false;
        for (const Roll& r : rolls) classed |= r.kin >= 0;
        one.set("chance", J::p(total(m, item, rolls, -1)));
        if (classed) one.set("chance_note", "for a killer of the roll's class");
        J list = J::arr();
        for (const Roll& r : rolls) list.push(rollJson(r));
        one.set("rolls", list).set("item", t.items[size_t(item)].name);
        items.push(one);
    }
    return items;
}

Kill killOf(const World& w, uint32_t kind, int castle) {
    Kill k;
    k.t = w.tables.get();
    k.map = w.tables->map;
    k.event = sim::eventOf(k.map);
    k.castle = castle;
    const content::MonsterKind& mk = w.tables->kinds[kind];
    k.level = mk.level;
    k.number = mk.number;
    k.rate = sim::dropRateOf(mk.number);
    return k;
}

Model modelOf(const Ground& g) {
    Model m;
    const Kill k = killOf(*g.world, g.kind, g.castle);
    if (g.elite) {
        eliteSpoils(k, m);
        return m;
    }
    leave(k, m);
    if (g.raid && k.number == sim::kGoldenBudgeDragonNumber) minionSpoils(k, m);
    if (g.raid && k.number == sim::kGoldenDragonNumber) dragonHoard(k, m);
    return m;
}

J monsterJson(const Ground& g) {
    const content::MonsterKind& mk = g.world->tables->kinds[g.kind];
    const Model m = modelOf(g);
    const sim::DropRate rate = sim::dropRateOf(mk.number);
    J o = J::obj();
    o.set("number", mk.number).set("name", mk.label).set("level", mk.level).set("elite", g.elite);
    o.set("max_plus", rate.maxPlus).set("money_rate", rate.moneyRate);
    if (g.elite) o.set("spoils_level", mk.level + sim::kEliteLevelsUp);
    if (!g.elite) o.set("zen", J::p(m.zen)).set("no_ticket", J::p(m.pass));
    o.set("items", modelJson(*g.world->tables, m));
    if (m.whileQuest.size()) o.set("while_quest", m.whileQuest);
    if (m.notes.size()) o.set("notes", m.notes);
    return o;
}

int32_t kindNumbered(const Tables& t, int32_t number) {
    for (size_t i = 0; i < t.kinds.size(); ++i) {
        if (t.kinds[i].number == number) return int32_t(i);
    }
    return -1;
}

// riseElite's breeds: the map's kEliteStrongest strongest that may be an Elite.
std::vector<uint32_t> eliteBreeds(const Tables& t) {
    std::vector<uint32_t> breeds;
    for (const content::MonsterNest& nest : t.nests) {
        if (nest.kind >= t.kinds.size() || !sim::mayBeElite(t.kinds[nest.kind].number)) continue;
        if (std::find(breeds.begin(), breeds.end(), nest.kind) == breeds.end()) breeds.push_back(nest.kind);
    }
    std::sort(breeds.begin(), breeds.end(), [&](uint32_t a, uint32_t b) {
        const content::MonsterKind &ka = t.kinds[a], &kb = t.kinds[b];
        return ka.level != kb.level ? ka.level > kb.level : ka.number < kb.number;
    });
    if (breeds.size() > size_t(sim::kEliteStrongest)) breeds.resize(size_t(sim::kEliteStrongest));
    return breeds;
}

J monsters(std::vector<Ground> grounds) {
    std::sort(grounds.begin(), grounds.end(), [](const Ground& a, const Ground& b) {
        const auto& ka = a.world->tables->kinds[a.kind];
        const auto& kb = b.world->tables->kinds[b.kind];
        if (ka.level != kb.level) return ka.level < kb.level;
        if (ka.number != kb.number) return ka.number < kb.number;
        return a.elite < b.elite;
    });
    J list = J::arr();
    for (const Ground& g : grounds) {
        // A Statue of Saint, or anything of level 0, leaves nothing (gen_sources' rule; leave()
        // would roll its tickets at level 0's plus).
        if (g.world->tables->kinds[g.kind].level <= 0 || sim::castleStatue(g.world->tables->kinds[g.kind].number)) continue;
        list.push(monsterJson(g));
    }
    return list;
}

// What one box opens into (crack, openBox).
}  // namespace

J dumpBoxes(const Dump& d) {
    const Tables& t = d.items();
    J out = J::obj();
    const auto entry = [&](J& items, int32_t item, double chance, const Roll& like) {
        J& one = items[t.items[size_t(item)].name];
        const double before = one.null() ? 0.0 : one.find("chance")->real();
        J r = rollJson(like);
        r.set("chance", J::p(before + chance));
        one = r;
    };
    // The Firecracker: kFirecrackerItemIn10 an item of its bag, else Zen.
    if (const int32_t fc = first(t, [](const ItemRow& r) { return sim::firecracker(r); }); fc >= 0) {
        const auto rows = bagRows(t, sim::kFirecrackerBag, std::size(sim::kFirecrackerBag), false);
        J box = J::obj(), items = J::obj();
        const double p = rows.empty() ? 0.0 : sim::kFirecrackerItemIn10 / 10.0;
        for (const int32_t item : rows) {
            Roll r;
            r.how = "item";
            if (sim::takesOptions(t.items[size_t(item)])) {
                r.plusLow = std::min(sim::kFirecrackerPlus, sim::kRefineCap);
                r.plusHigh = std::min(sim::kFirecrackerPlus + sim::kFirecrackerPluses - 1, sim::kRefineCap);
                boxOptions(r, 0.5, true);
            }
            entry(items, item, p / double(rows.size()), r);
        }
        J range = J::arr();
        range.push(J(sim::kFirecrackerZenLeast));
        range.push(J(sim::kFirecrackerZenMost));
        box.set("items", items).set("zen", J::p(1.0 - p)).set("zen_range", range);
        out[t.items[size_t(fc)].name] = box;
    }
    // The Box of Luck.
    if (const int32_t luck = first(t, [](const ItemRow& r) { return sim::boxOfLuck(r); }); luck >= 0) {
        const auto rows = bagRows(t, sim::kLuckBag, std::size(sim::kLuckBag), false);
        J box = J::obj(), items = J::obj();
        const double p = rows.empty() ? 0.0 : sim::kLuckItemIn100 / 100.0;
        for (const int32_t item : rows) {
            Roll r;
            r.how = "item";
            if (sim::takesOptions(t.items[size_t(item)])) {
                r.plusLow = r.plusHigh = std::min(sim::kLuckPlus, sim::kRefineCap);
                boxOptions(r, sim::kBoxLuckIn100 / 100.0, true);
            }
            entry(items, item, p / double(rows.size()), r);
        }
        box.set("items", items).set("zen", J::p(1.0 - p)).set("zen_amount", J(sim::kLuckZen));
        out[t.items[size_t(luck)].name] = box;
    }
    // The Box of Kundun +1..+3: an item at kKundunItemIn100, excellent at kKundunExcellentIn100
    // of those from its excellent bag (the plain bag when that has none here).
    if (const int32_t kundun = first(t, [](const ItemRow& r) { return sim::boxOfKundun(r); }); kundun >= 0) {
        const sim::BagRow* plains[3] = {sim::kKundunPlain1, sim::kKundunPlain2, sim::kKundunPlain3};
        const size_t plainCounts[3] = {std::size(sim::kKundunPlain1), std::size(sim::kKundunPlain2), std::size(sim::kKundunPlain3)};
        const sim::BagRow* fines[3] = {sim::kKundunExcellent1, sim::kKundunExcellent2, sim::kKundunExcellent3};
        const size_t fineCounts[3] = {std::size(sim::kKundunExcellent1), std::size(sim::kKundunExcellent2),
                                      std::size(sim::kKundunExcellent3)};
        for (int tier = 0; tier < sim::kKundunTiers; ++tier) {
            const auto plain = bagRows(t, plains[tier], plainCounts[tier], false);
            const auto fine = bagRows(t, fines[tier], fineCounts[tier], true);
            const double item = sim::kKundunItemIn100[tier] / 100.0;
            const double exc = fine.empty() ? 0.0 : sim::kKundunExcellentIn100[tier] / 100.0;
            J box = J::obj(), items = J::obj();
            for (const int32_t one : fine) {
                Roll r;
                r.how = "excellent";
                r.excellent = true;
                boxOptions(r, sim::kBoxLuckIn100 / 100.0, false);
                r.twoOptions = secondOption();
                entry(items, one, item * exc / double(fine.size()), r);
            }
            for (const int32_t one : plain) {
                Roll r;
                r.how = "item";
                if (sim::takesOptions(t.items[size_t(one)])) {
                    r.plusLow = std::min(sim::kKundunPlainLevel[tier], sim::kRefineCap);
                    r.plusHigh = std::min(sim::kKundunPlainLevel[tier] + sim::kKundunAddLevel - 1, sim::kRefineCap);
                    boxOptions(r, sim::kBoxLuckIn100 / 100.0, true);
                }
                // The excellent's row in `items` is keyed the same: kept apart under its own name.
                J& slot = items[t.items[size_t(one)].name + (std::count(fine.begin(), fine.end(), one) ? "#plain" : "")];
                const double before = slot.null() ? 0.0 : slot.find("chance")->real();
                J rj = rollJson(r);
                rj.set("chance", J::p(before + item * (1.0 - exc) / double(plain.size())));
                slot = rj;
            }
            box.set("items", items).set("zen", J::p(1.0 - item)).set("zen_amount", J(sim::kKundunZen[tier]));
            out[t.items[size_t(kundun)].name + "+" + std::to_string(tier + 1)] = box;
        }
    }
    // As rules.json's list of pages: each box, and what it opens into under `result`.
    J list = J::arr();
    for (auto& [key, box] : out.members()) {
        const size_t plus = key.find('+');
        const std::string itemName = key.substr(0, plus);
        const int32_t item = t.itemNamed(itemName);
        J result = J::arr();
        for (auto& [name, one] : box["items"].members()) {
            const size_t hash = name.find('#');
            one.set("item", name.substr(0, hash));
            result.push(std::move(one));
        }
        J entry = J::obj();
        entry.set("key", key).set("item", itemName)
            .set("name", (item >= 0 ? t.items[size_t(item)].label : itemName) + (plus == std::string::npos ? "" : " " + key.substr(plus)))
            .set("result", result);
        for (auto& [field, v] : box.members()) {
            if (field != "items") entry.set(field, v);
        }
        list.push(entry);
    }
    return list;
}

namespace {
}  // namespace

std::vector<double> perKill(const Dump&, const Ground& g, sim::Kin kin) {
    const Model m = modelOf(g);
    std::vector<double> out(g.world->tables->items.size(), 0.0);
    for (const auto& [item, rolls] : m.items) out[size_t(item)] = total(m, item, rolls, int(kin));
    return out;
}

J dumpDrops(const Dump& d) {
    J out = d.stamped();
    out.set("baseline", "one kill, the killer alone: no item find, no Fortune talents, no party or raider credit");
    J places = J::obj();
    for (const World& w : d.worlds) {
        const Tables& t = *w.tables;
        const sim::EventKind event = sim::eventOf(t.map);
        J rules = J::obj();
        const sim::MapRow& r = sim::rulesOf(t.map);
        rules.set("feather", r.feather).set("nova_scroll", r.novaScroll).set("firecracker", r.firecracker)
            .set("first_wings", r.firstWings).set("second_gear", r.secondGear).set("second_class", r.secondClass)
            .set("invasion", r.invasion)
            .set("treasure", r.treasure == sim::Treasure::Map ? "map" : r.treasure == sim::Treasure::LastFloor ? "last_floor" : "none");
        if (event == sim::EventKind::Castle) {
            for (int c = 1; c <= sim::kCastles; ++c) {
                std::vector<Ground> grounds;
                for (const int32_t number : sim::kCastleBreeds[c - 1]) {
                    const int32_t kind = kindNumbered(t, number);
                    if (kind >= 0) grounds.push_back(Ground{&w, uint32_t(kind), c});
                }
                J place = J::obj();
                place.set("world", w.name).set("map", t.map).set("kind", "castle").set("castle", c)
                    .set("built", c <= sim::kCastlesBuilt).set("rules", rules).set("monsters", monsters(grounds));
                places["bloodcastle-" + std::to_string(c)] = place;
            }
            continue;
        }
        if (event == sim::EventKind::Square) {
            for (int s = 1; s <= sim::kSquares; ++s) {
                std::vector<int32_t> numbers;
                for (int wave = 0; wave < sim::kSquareWaves; ++wave) {
                    for (int i = 0; i < 2; ++i) numbers.push_back(sim::kSquareBreeds[s - 1][wave][i]);
                }
                numbers.push_back(sim::kSquareBoss[s - 1]);
                std::sort(numbers.begin(), numbers.end());
                numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
                std::vector<Ground> grounds;
                for (const int32_t number : numbers) {
                    const int32_t kind = kindNumbered(t, number);
                    if (kind >= 0) grounds.push_back(Ground{&w, uint32_t(kind), 0, s});
                }
                J place = J::obj();
                place.set("world", w.name).set("map", t.map).set("kind", "square").set("square", s)
                    .set("built", s <= sim::kSquaresBuilt).set("rules", rules).set("monsters", monsters(grounds));
                places["devilsquare-" + std::to_string(s)] = place;
            }
            continue;
        }
        if (t.nests.empty()) continue;
        std::vector<Ground> grounds;
        std::vector<uint32_t> seen;
        for (const content::MonsterNest& nest : t.nests) {
            if (nest.kind >= t.kinds.size() || std::count(seen.begin(), seen.end(), nest.kind)) continue;
            seen.push_back(nest.kind);
            grounds.push_back(Ground{&w, nest.kind});
        }
        // Its Elites (Realm::raiseElites): none in an event's copy, none where nothing nests.
        J elites = J::arr();
        if (sim::elitesOn(t.map) > 0) {
            for (const uint32_t kind : eliteBreeds(t)) {
                Ground g{&w, kind};
                g.elite = true;
                grounds.push_back(g);
                elites.push(t.kinds[kind].number);
            }
        }
        J place = J::obj();
        place.set("world", w.name).set("map", t.map).set("kind", "map").set("rules", rules).set("monsters", monsters(grounds));
        if (sim::elitesOn(t.map) > 0) {
            place.set("elites", J::obj().set("count", sim::elitesOn(t.map)).set("breeds", elites)
                                    .set("life", J(double(sim::eliteLifeOn(t.map)))));
        }
        places[w.name] = place;
        // The Golden Dragon raid on a map it invades: the dragon and its minions, each its own
        // leave() here and its own spoils.
        if (sim::invasionMap(t.map)) {
            std::vector<Ground> raid;
            for (const int32_t number : {sim::kGoldenDragonNumber, sim::kGoldenBudgeDragonNumber}) {
                const int32_t kind = kindNumbered(t, number);
                if (kind < 0) continue;
                Ground g{&w, uint32_t(kind)};
                g.raid = true;
                raid.push_back(g);
            }
            J rp = J::obj();
            rp.set("world", w.name).set("map", t.map).set("kind", "raid").set("rules", rules).set("monsters", monsters(raid));
            places[w.name + "-raid"] = rp;
        }
    }
    // By monster, each with the places it is met in (build.py's reader): the place's own fields
    // beside each, and the place keeps the list of who is met there.
    std::map<int, J> byNumber;
    for (auto& [id, place] : places.members()) {
        J met = J::arr();
        for (J& m : place["monsters"].items()) {
            const int number = int(m.find("number")->real());
            J& entry = byNumber[number];
            if (entry.null()) entry = J::obj().set("number", number).set("name", *m.find("name")).set("places", J::arr());
            m.set("place", id);
            for (const char* f : {"world", "map", "kind", "castle", "square"}) {
                if (const J* v = place.find(f)) m.set(f, *v);
            }
            met.push(J::obj().set("number", number).set("elite", *m.find("elite")));
            entry["places"].push(std::move(m));
        }
        place.set("monsters", met);
    }
    J list = J::arr();
    for (auto& [number, entry] : byNumber) list.push(std::move(entry));
    out.set("places", places).set("monsters", list);
    J notes = J::arr();
    notes.push("chance: the odds one kill leaves at least one; rolls: each way it comes, per kill");
    notes.push("roll kinds: scroll/bone/eye/key are a kill's whole drop (Ticket); rune, jewel, jewel_group, excellent, item share the one main roll; the rest are their own dice");
    notes.push("first_wings: only the killer's own class's wing; its chance is for a killer of that class");
    notes.push("option: the option level's odds (1, 2, 3 = +4, +8, +12 on gear); luck: lucky; sockets: at least one socket; powers: a powered piece's count of powers");
    notes.push("while_quest: a Find quest's treasure, only while the quest is active for a native class, not in chance");
    notes.push("zen: the chance the kill pays Zen into the purse, its amount the kill's experience + 7");
    out.set("notes", notes);
    return out;
}

}  // namespace armory
