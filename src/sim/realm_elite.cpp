// The Elites (sim/elite.h): a map's own monster promoted, raised with the realm and drawn again at
// each rise. Ours, the user's of 2026-10-21.
//
// Its body is raised last of all, after the raid's, so no id before it moves; its breed, its
// tile, its rise and its spoils come off eliteDice_, so a seeded hunt beside it rolls as it did.
// Between rises it is an ordinary monster to the realm: it wakes, chases, swings and dies by the
// beasts' own loop (Realm::step), and only its numbers, its spoils and its time down are its own.

#include <algorithm>
#include <cmath>
#include <tuple>

#include "core/log.h"
#include "sim/elite.h"
#include "sim/gates.h"
#include "sim/realm.h"
#include "sim/realm_tuning.h"
#include "sim/wear.h"

namespace mu::sim {

void Realm::raiseElites() {
    eliteSlots_.clear();
    const int count = elitesOn(tables_->map);
    // Not in an event's copy of a map, nor on a map whose tables carry no nests.
    if (count <= 0 || event() != EventKind::None || tables_->nests.empty()) return;
    for (int i = 0; i < count; ++i) {
        Body elite;
        elite.id = nextId_++;
        elite.elite = true;
        elite.kind = int32_t(tables_->nests.front().kind);
        elite.nest = -1;
        elite.temper = Temper::Asleep;
        eliteSlots_.push_back(int32_t(bodies_.size()));
        bodies_.push_back(std::move(elite));
        route(bodies_.back()).reserve(64);
        // Standing from the first tick, as every monster is: it is just there.
        riseElite(bodies_.back());
        if (!bodies_.back().alive()) {
            // No tile found anywhere: it waits a rise and tries again.
            bodies_.back().risesAt = tick_ + kEliteRiseLeastTicks;
        }
    }
}

void Realm::riseElite(Body& elite) {
    // A breed of the map's nests -- one of its kEliteStrongest strongest ordinary breeds, by level,
    // the bosses left out (the user, 2026-10-21: "dont make elite monster weakest monsters on
    // map, only last 3 strongest") -- the breed drawn first, each of the three alike, and none
    // another standing Elite of the map already is ("elite monsters could be last 3 strongest on
    // each map not just 1 type"); then one of its nests, and a tile inside that nest's box away
    // from the town, the guards and the gates.
    const std::vector<content::MonsterNest>& nests = tables_->nests;
    std::vector<uint32_t> breeds;
    for (const content::MonsterNest& nest : nests) {
        if (nest.kind >= tables_->kinds.size() || !mayBeElite(tables_->kinds[nest.kind].number)) continue;
        if (std::find(breeds.begin(), breeds.end(), nest.kind) == breeds.end()) breeds.push_back(nest.kind);
    }
    std::sort(breeds.begin(), breeds.end(), [&](uint32_t a, uint32_t b) {
        const content::MonsterKind &ka = tables_->kinds[a], &kb = tables_->kinds[b];
        return ka.level != kb.level ? ka.level > kb.level : ka.number < kb.number;
    });
    if (breeds.size() > size_t(kEliteStrongest)) breeds.resize(size_t(kEliteStrongest));
    // Not a breed another of the map's Elites stands as now, while one is left to draw.
    std::vector<uint32_t> free = breeds;
    for (const int32_t slot : eliteSlots_) {
        const Body& other = bodies_[size_t(slot)];
        if (&other == &elite || !other.alive() || other.kind < 0) continue;
        std::erase(free, uint32_t(other.kind));
    }
    if (free.empty()) free = breeds;
    const uint32_t breed = free.empty() ? 0 : free[size_t(eliteDice_.nextInt(0, int(free.size())))];
    std::vector<size_t> fit;
    for (size_t i = 0; i < nests.size(); ++i) {
        if (nests[i].kind == breed) fit.push_back(i);
    }
    if (fit.empty()) {
        core::logError("elite: no breed for an Elite on map %u", tables_->map);
        return;
    }
    const auto byGate = [&](int column, int row) {
        for (int dr = -kEliteGateClear; dr <= kEliteGateClear; dr += 3) {
            for (int dc = -kEliteGateClear; dc <= kEliteGateClear; dc += 3) {
                if (enterGateAt(tables_->map, column + dc, row + dr) != nullptr) return true;
            }
        }
        return false;
    };
    int column = -1, row = -1;
    size_t nestAt = 0;
    for (int attempt = 0; attempt < 60 && column < 0; ++attempt) {
        nestAt = fit[size_t(eliteDice_.nextInt(0, int(fit.size())))];
        const content::MonsterNest& nest = nests[nestAt];
        const int c = eliteDice_.nextInt(nest.x1, nest.x2 + 1);
        const int r = eliteDice_.nextInt(nest.y1, nest.y2 + 1);
        if (!tables_->grid.open(c, r, content::kWallCharacter) || tables_->grid.safe(c, r) ||
            nearPost(c, r) || byGate(c, r)) {
            continue;
        }
        column = c;
        row = r;
    }
    if (column < 0) {
        core::logError("elite: no tile for an Elite on map %u", tables_->map);
        return;
    }
    const content::MonsterKind& kind = tables_->kinds[nests[nestAt].kind];
    elite.kind = int32_t(nests[nestAt].kind);
    elite.level = kind.level;
    elite.maxHealth = eliteHealth(kind.health, tables_->map);
    elite.stats = Fighter{};
    elite.stats.level = kind.level;
    elite.stats.attackRate = kind.attackRate;
    elite.stats.defenseRate = kind.defenseRate;
    elite.stats.defense = int32_t(std::lround(float(kind.defense) * kEliteDefense));
    elite.stats.minimumDamage = kind.minimumDamage * kEliteDamage;
    elite.stats.maximumDamage = kind.maximumDamage * kEliteDamage;
    elite.swingTicks = std::max(1, int(std::lround(float(kind.attackTicks) / kEliteSwing)));
    elite.speed = kEliteStride / float(std::max(1, kind.moveTicks));
    elite.homeColumn = column;
    elite.homeRow = row;
    // Stood up as any monster is, on the tile drawn here: raiseBeast draws none for a body of
    // no nest.
    elite.nest = -1;
    raiseBeast(elite);
    core::logf("elite: an Elite %s (level %d, %d health) at (%d, %d) on map %u", kind.label.c_str(),
               elite.level, elite.maxHealth, column, row, tables_->map);
}

void Realm::shrugControl(Body& elite) {
    elite.chilledUntil = 0;
    elite.chillSlow = 0.0f;
    elite.frozenUntil = 0;
    // Neither pushed nor pulled: it stays on its tile, and a Whirlwind's slash that would have
    // landed at the end of the pull lands now, if he is near enough to it.
    if (elite.pushTicks > 0) {
        elite.pushTicks = 0;
        elite.pushX = elite.pushY = 0.0f;
        elite.x = float(elite.column());
        elite.y = float(elite.row());
        if (elite.whirledBy != 0) {
            Body* by = body(elite.whirledBy);
            const SkillRow* slash = skillNumbered(skill::kTwistingSlash);
            if (by && by->alive() && slash && within(*by, elite, slash->reach)) {
                strikeAt(*by, elite, elite.whirlForce, nullptr, false);
            }
        }
        elite.whirledBy = 0;
    }
}

void Realm::eliteSpoils(const Body& dead, const Body& killer) {
    const int level = dead.level + kEliteLevelsUp;
    constexpr int kGap = 15;
    const DropRate rate = dropRateOf(tables_->kinds[size_t(dead.kind)].number);
    const bool secondGearHere = rulesOf(tables_->map).secondClass;
    const auto draw = [&](auto&& admits) -> int32_t {
        int count = 0;
        for (const content::ItemRow& row : tables_->items) count += admits(row) ? 1 : 0;
        if (count == 0) return -1;
        int pick = eliteDice_.nextInt(0, count);
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (admits(tables_->items[i]) && pick-- == 0) return int32_t(i);
        }
        return -1;
    };
    // Gear only: weapons, armour, shields and wings' groups under the boots, ammunition and
    // rings left to the ordinary drops.
    const auto gear = [&](const content::ItemRow& r) {
        return r.dropsFromMonsters() && r.group <= kGroupBoots && !ammunition(r) && !jewellery(r) &&
               !summoningOrb(r) && r.dropLevel <= level && r.dropLevel >= level - kGap &&
               (r.maximumDropLevel == 0 || level <= r.maximumDropLevel) &&
               (secondGearHere || !secondClassOnly(r));
    };
    const auto option = [&](Held& what) {
        what.luck = eliteDice_.nextInt(0, 100) < kEliteLuckIn100;
        // Rolled twice, the better kept.
        for (int roll = 0; roll < 2; ++roll) {
            const int under = eliteDice_.nextInt(0, 100);
            const int which = eliteDice_.nextInt(0, 3);
            if (under < kOptionUnder[which]) {
                what.option = int8_t(std::max<int>(what.option, kMostOptionDropped - which));
            }
        }
    };
    Held spoils[kEliteMostPieces + 2];
    int count = 0;
    const int pieces = eliteDice_.nextInt(kEliteLeastPieces, kEliteMostPieces + 1);
    // Which of them is excellent, -1 for none.
    const int excellentAt = eliteDice_.nextInt(0, 100) < kEliteExcellentIn100 ? eliteDice_.nextInt(0, pieces) : -1;
    for (int i = 0; i < pieces; ++i) {
        const bool excellent = i == excellentAt;
        const int32_t item = draw([&](const content::ItemRow& r) { return gear(r) && (!excellent || excellentable(r)); });
        if (item < 0) continue;
        const content::ItemRow& row = tables_->items[size_t(item)];
        // Its plus as any drop's, `(level - drop level) / 3`, to the breed's own most; an
        // excellent one at +0, as MU's are.
        const int plus = excellent ? 0 : std::clamp((level - row.dropLevel) / 3, 0, std::min(kRefineCap, rate.maxPlus));
        Held what{item, int16_t(plus), int16_t(fullDurability(row, plus))};
        if (takesOptions(row)) option(what);
        if (excellent) {
            int first = eliteDice_.nextInt(0, kExcellentOptions);
            what.excellent = uint8_t(1u << first);
            if (eliteDice_.nextInt(0, 4) == 0) what.excellent |= uint8_t(1u << eliteDice_.nextInt(0, kExcellentOptions));
            what.durability = int16_t(maximumDurability(row, what));
        }
        if (takesSockets(row) && eliteDice_.nextBool(kSocketChance)) {
            what.sockets = 1;
            while (what.sockets < mostSocketsOf(row) && eliteDice_.nextBool(kMoreSocketChance)) ++what.sockets;
        }
        spoils[count++] = what;
    }
    // The jewels: the Bless, the Soul or the Chaos -- never the Life, never a rune.
    const auto jewel = [](const content::ItemRow& r) { return refiningJewel(r) && jewelOf(r) != Jewel::Life; };
    int jewels = eliteDice_.nextInt(0, 100) < kEliteJewelIn100 ? 1 : 0;
    if (jewels > 0 && eliteDice_.nextInt(0, 100) < kEliteSecondJewelIn100) ++jewels;
    for (int i = 0; i < jewels; ++i) {
        const int32_t item = draw(jewel);
        if (item >= 0) spoils[count++] = Held{item, 0, 1};
    }
    for (int i = 0; i < count; ++i) {
        Lying one;
        one.what = spoils[i];
        std::tie(one.column, one.row) = clearing(dead.column(), dead.row());
        one.vanishesAt = tick_ + kEliteLingerTicks;
        one.owner = mine().id;
        one.id = nextId_++;
        lying_.push_back(one);
        say(What::Dropped, dead, int32_t(one.id), one.what.item, one.what.refinement);
        core::logf("elite: leaves %s +%d%s%s", tables_->items[size_t(one.what.item)].name.c_str(),
                   one.what.refinement, one.what.excellent ? ", excellent" : "", one.what.luck ? ", luck" : "");
    }
    // And its Zen, to the purse as every kill's, said off the body it came from.
    const int64_t zen = (int64_t(killExperience(dead.level, killer.level)) + 7) * kEliteZen;
    const TalentShares& fortune = mine().talentShare;
    const double zenRate = fortune.moreZen != 0 ? mine().excel.zenRate * (1.0 + double(fortune.moreZen) / 100.0)
                                                : mine().excel.zenRate;
    me().money += int64_t(double(zen) * zenRate);
    say(What::Picked, mine(), int32_t(dead.id), -1, int32_t(zen));
}

}  // namespace mu::sim
