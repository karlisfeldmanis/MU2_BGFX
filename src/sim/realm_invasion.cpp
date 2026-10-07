// The Golden Invasion (sim/invasion.h): one dragon a map, raised down with the realm and risen
// where it lands.
//
// The realm's part is small, as OpenMU's is: a clock that begins it, a tile it comes down on, the
// tick it rises there, and its end -- killed, never to rise again, or its thirty minutes up and
// gone without a fall. The rain, the dragons in the sky and the dive are the drawing's, paced off
// kInvasionRainTicks and kInvasionLandTicks from the `Invasion` happening (game/invasion_sky.h).
#include "sim/realm.h"
#include "sim/fmath.h"

#include <cmath>
#include <limits>

#include "core/log.h"

namespace mu::sim {

void Realm::raiseInvader() {
    invaderSlot_ = -1;
    invasion_.phase = InvasionPhase::Quiet;
    invasionOwed_ = false;
    if (tables_->map != kInvasionMap) return;
    int32_t kindAt = -1;
    for (size_t i = 0; i < tables_->kinds.size(); ++i) {
        if (tables_->kinds[i].number == kGoldenDragonNumber) {
            kindAt = int32_t(i);
            break;
        }
    }
    // Not cooked into this world's tables: no invasion, and the log says why.
    if (kindAt < 0) {
        core::logf("invasion: breed %d is not in map %u's tables; no invasion", kGoldenDragonNumber,
                   tables_->map);
        return;
    }
    const content::MonsterKind& kind = tables_->kinds[size_t(kindAt)];
    Body beast;
    beast.id = nextId_++;
    beast.kind = kindAt;
    beast.level = kind.level;
    beast.maxHealth = kind.health;
    beast.health = 0;
    beast.stats.level = kind.level;
    beast.stats.attackRate = kind.attackRate;
    beast.stats.defenseRate = kind.defenseRate;
    beast.stats.defense = kind.defense;
    beast.stats.minimumDamage = kind.minimumDamage;
    beast.stats.maximumDamage = kind.maximumDamage;
    beast.swingTicks = kind.attackTicks;
    beast.speed = 1.0f / float(std::max(1, kind.moveTicks));
    beast.x = beast.y = 0.0f;
    beast.nest = -1;
    beast.temper = Temper::Dead;
    beast.risesAt = std::numeric_limits<int64_t>::max();
    beast.route.reserve(64);
    invaderSlot_ = int(bodies_.size());
    bodies_.push_back(std::move(beast));
}

void Realm::invasionRain(bool raining) {
    const bool began = raining && !invasion_.raining;
    invasion_.raining = raining;
    if (!began || invaderSlot_ < 0 || invasion_.phase != InvasionPhase::Quiet || invasionOwed_) {
        return;
    }
    if (invasionDice_.nextInt(0, 100) < kInvasionChance) invade();
}

bool Realm::invade(bool now) {
    if (invaderSlot_ < 0 || invasion_.phase != InvasionPhase::Quiet) return false;
    invasionNow_ = now;
    // Begun on the next tick, inside it, so its happening is that tick's (as me().castleOwed).
    invasionOwed_ = true;
    return true;
}

void Realm::invasionTick() {
    if (invaderSlot_ < 0) return;
    Body& dragon = bodies_[size_t(invaderSlot_)];
    if (invasionOwed_) {
        invasionOwed_ = false;
        const Body& hero = mine();
        // A tile it can stand on, out of the safe zone, within sight of him: kInvasionNear to
        // kInvasionFar tiles off in any direction (ours). In town the ring is all safe, so it
        // widens until it reaches the fields -- the dragon comes down outside his walls. Failing
        // every ring, anywhere walkable, as OpenMU's RandomWalkableCoordinate.
        int column = -1, row = -1;
        const auto standable = [&](int c, int r) {
            return tables_->grid.open(c, r, content::kWallCharacter) && !tables_->grid.safe(c, r);
        };
        // The raid's dragon on one of its fields outside the town (sim::kRaidLandings), or the
        // nearest standable tile to it.
        if (raidAsked_) {
            const int at = raidLanding_ >= 0 && raidLanding_ < kRaidLandingCount
                               ? raidLanding_
                               : invasionDice_.nextInt(0, kRaidLandingCount);
            const RaidLanding& landing = kRaidLandings[at];
            for (int out = 0; out <= 3 && column < 0; ++out) {
                for (int dr = -out; dr <= out && column < 0; ++dr) {
                    for (int dc = -out; dc <= out && column < 0; ++dc) {
                        if (standable(landing.column + dc, landing.row + dr)) {
                            column = landing.column + dc;
                            row = landing.row + dr;
                        }
                    }
                }
            }
            core::logf("raid: it lands to the %s of the town (%c)", landing.where, landing.name);
        }
        for (int far = kInvasionFar; far <= kInvasionRings * kInvasionFar && column < 0;
             far += kInvasionFar) {
            const int near = far == kInvasionFar ? kInvasionNear : far - kInvasionFar;
            for (int attempt = 0; attempt < 160 && column < 0; ++attempt) {
                const double angle = invasionDice_.nextDouble() * 6.283185307179586;
                const double reach = near + invasionDice_.nextDouble() * (far - near);
                const int c = hero.column() + int(std::lround(fm::cos(angle) * reach));
                const int r = hero.row() + int(std::lround(fm::sin(angle) * reach));
                if (standable(c, r)) {
                    column = c;
                    row = r;
                }
            }
        }
        const int side = int(tables_->grid.size());
        for (int attempt = 0; attempt < 400 && column < 0; ++attempt) {
            const int c = invasionDice_.nextInt(0, side);
            const int r = invasionDice_.nextInt(0, side);
            if (standable(c, r)) {
                column = c;
                row = r;
            }
        }
        if (column < 0) {
            core::logError("invasion: no standable tile on map %u", tables_->map);
            return;
        }
        dragon.homeColumn = column;
        dragon.homeRow = row;
        dragon.x = float(column);
        dragon.y = float(row);
        // Facing him as it comes down, so it lands looking at him.
        dragon.facing = dragon.aim =
            fm::atan2(hero.y - dragon.y, hero.x - dragon.x);
        invasion_.phase = InvasionPhase::Entering;
        invasion_.landsAt = tick_ + (invasionNow_ ? 1 : kInvasionLandTicks);
        invasionNow_ = false;
        // Risen by the beasts' own loop on that tick (Realm::step, raiseBeast), its five idle
        // seconds the roar it lands with.
        dragon.risesAt = invasion_.landsAt;
        say(What::Invasion, dragon, 1, column, row);
        core::logf("invasion: begun on map %u, landing at (%d, %d) on tick %lld", tables_->map,
                   column, row, (long long)invasion_.landsAt);
        return;
    }
    if (invasion_.phase == InvasionPhase::Entering && tick_ >= invasion_.landsAt) {
        invasion_.phase = InvasionPhase::Standing;
        invasion_.endsAt = invasion_.landsAt + kInvasionStandTicks;
    }
    if (invasion_.phase == InvasionPhase::Standing && tick_ >= invasion_.endsAt) {
        // Its time up: gone where it stands, without a fall (CleanUpMonstersAsync), as a summon
        // is dismissed.
        if (dragon.alive()) {
            dragon.health = 0;
            dragon.quarry = 0;
            halt(dragon);
            dropBlow(dragon);
            for (Body& one : bodies_) {
                if (one.quarry == dragon.id) {
                    one.quarry = 0;
                    one.provoked = false;
                }
            }
            say(What::Dismissed, dragon);
        }
        endInvasion();
    }
}

void Realm::endInvasion() {
    if (invaderSlot_ < 0 || invasion_.phase == InvasionPhase::Quiet) return;
    Body& dragon = bodies_[size_t(invaderSlot_)];
    dragon.risesAt = std::numeric_limits<int64_t>::max();
    invasion_.phase = InvasionPhase::Quiet;
    say(What::Invasion, dragon, 0, dragon.homeColumn, dragon.homeRow);
}

}  // namespace mu::sim
