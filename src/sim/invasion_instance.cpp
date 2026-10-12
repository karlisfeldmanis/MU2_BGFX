// The Golden Invasion (sim/invasion.h, sim/invasion_instance.h): one dragon a map, raised down
// with the realm and risen where it lands. Out of the Realm as an instance (order-plan §9e, J1).
//
// The realm's part is small, as OpenMU's is: a clock that begins it, a tile it comes down on, the
// tick it rises there, and its end -- killed, never to rise again, or its thirty minutes up and
// gone without a fall. The rain, the dragons in the sky and the dive are the drawing's, paced off
// kInvasionRainTicks and kInvasionLandTicks from the `Invasion` happening (game/invasion_sky.h).
#include "sim/invasion_instance.h"

#include "sim/realm.h"
#include "sim/fmath.h"

#include <cmath>
#include <limits>

#include "core/log.h"

namespace mu::sim {

void InvasionInstance::raise(Realm& realm) {
    slot = -1;
    phase = InvasionPhase::Quiet;
    owed = false;
    if (!invasionMap(realm.tables_->map)) return;
    int32_t kindAt = -1;
    for (size_t i = 0; i < realm.tables_->kinds.size(); ++i) {
        if (realm.tables_->kinds[i].number == kGoldenDragonNumber) {
            kindAt = int32_t(i);
            break;
        }
    }
    // Not cooked into this world's tables: no invasion, and the log says why.
    if (kindAt < 0) {
        core::logf("invasion: breed %d is not in map %u's tables; no invasion", kGoldenDragonNumber,
                   realm.tables_->map);
        return;
    }
    const content::MonsterKind& kind = realm.tables_->kinds[size_t(kindAt)];
    Body beast;
    beast.id = realm.nextId_++;
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
    slot = int(realm.bodies_.size());
    realm.bodies_.push_back(std::move(beast));
    realm.route(realm.bodies_.back()).reserve(64);
}

void InvasionInstance::rain(Realm& realm, bool raining, bool elsewhere) {
    const bool began = raining && !this->raining;
    this->raining = raining;
    if (!began || elsewhere || slot < 0 || phase != InvasionPhase::Quiet ||
        owed) {
        return;
    }
    if (realm.invasionDice_.nextInt(0, 100) < kInvasionChance) invade(realm, false);
}

bool InvasionInstance::invade(Realm& realm, bool now) {
    if (slot < 0 || phase != InvasionPhase::Quiet) return false;
    landNow = now;
    // Begun on the next tick, inside it, so its happening is that tick's (as a castle's Enter).
    owed = true;
    return true;
}

void InvasionInstance::tick(Realm& realm) {
    if (slot < 0) return;
    Body& dragon = realm.bodies_[size_t(slot)];
    if (owed) {
        owed = false;
        const Body& hero = realm.mine();
        int column = -1, row = -1;
        const auto standable = [&](int c, int r) {
            return realm.tables_->grid.open(c, r, content::kWallCharacter) &&
                   !realm.tables_->grid.safe(c, r);
        };
        // The nearest standable tile to a field, within `reach` of it.
        const auto near = [&](int fieldColumn, int fieldRow, int reach) {
            for (int out = 0; out <= reach && column < 0; ++out) {
                for (int dr = -out; dr <= out && column < 0; ++dr) {
                    for (int dc = -out; dc <= out && column < 0; ++dc) {
                        if (standable(fieldColumn + dc, fieldRow + dr)) {
                            column = fieldColumn + dc;
                            row = fieldRow + dr;
                        }
                    }
                }
            }
        };
        // The raid's dragon on one of its fields outside the town (sim::kRaidLandings), when one
        // was asked for in Lorencia (--raid).
        if (realm.raidAsked_ && realm.tables_->map == 0) {
            const int at = realm.raidLanding_ >= 0 && realm.raidLanding_ < kRaidLandingCount
                               ? realm.raidLanding_
                               : realm.invasionDice_.nextInt(0, kRaidLandingCount);
            const RaidLanding& landing = kRaidLandings[at];
            near(landing.column, landing.row, 3);
            core::logf("raid: it lands to the %s of the town (%c)", landing.where, landing.name);
        }
        // The invasion's on one of its map's three fields, at random (sim::kInvasionFields).
        if (column < 0) {
            int fields[kInvasionFieldCount];
            int count = 0;
            for (int i = 0; i < kInvasionFieldCount; ++i) {
                if (kInvasionFields[i].map == realm.tables_->map) fields[count++] = i;
            }
            if (count > 0) {
                const int pick = realm.invasionDice_.nextInt(0, count);
                const InvasionField& field = kInvasionFields[fields[pick]];
                near(field.column, field.row, kInvasionFieldReach);
            }
        }
        const int side = int(realm.tables_->grid.size());
        for (int attempt = 0; attempt < 400 && column < 0; ++attempt) {
            const int c = realm.invasionDice_.nextInt(0, side);
            const int r = realm.invasionDice_.nextInt(0, side);
            if (standable(c, r)) {
                column = c;
                row = r;
            }
        }
        if (column < 0) {
            core::logError("invasion: no standable tile on map %u", realm.tables_->map);
            return;
        }
        // Tough for the players on the map as it comes, when no raid named how many (setRaid):
        // a lone hero meets the one-player raid, ten meet the ten's (sim::raidHealth).
        if (!realm.raidAsked_) {
            realm.raid_.players = std::max(1, realm.playerCount());
            dragon.maxHealth = raidHealth(realm.raid_.players);
            core::logf("invasion: the dragon is tough for %d, %d health", realm.raid_.players,
                       dragon.maxHealth);
        }
        dragon.homeColumn = column;
        dragon.homeRow = row;
        dragon.x = float(column);
        dragon.y = float(row);
        // Facing him as it comes down, so it lands looking at him.
        dragon.facing = dragon.aim =
            fm::atan2(hero.y - dragon.y, hero.x - dragon.x);
        phase = InvasionPhase::Entering;
        struck = false;
        landsAt = realm.tick_ + (landNow ? 1 : kInvasionLandTicks);
        landNow = false;
        // Risen by the beasts' own loop on that tick (Realm::step, raiseBeast), its five idle
        // seconds the roar it lands with.
        dragon.risesAt = landsAt;
        realm.say(What::Invasion, dragon, 1, column, row);
        core::logf("invasion: begun on map %u, landing at (%d, %d) on tick %lld",
                   realm.tables_->map, column, row, (long long)landsAt);
        return;
    }
    if (phase == InvasionPhase::Entering && realm.tick_ >= landsAt) {
        phase = InvasionPhase::Standing;
        endsAt = landsAt + kInvasionStandTicks;
    }
    // Struck: a player's blow, rune, poison or fire has taken its health (hurtBy, and a summon's
    // under its owner). Its turning on a bot is not a fight begun (the user, 2026-10-11); only
    // a wound is. Read by the raid's clock (Realm::raidTick, kInvasionUnstruckTicks).
    if (phase == InvasionPhase::Standing && dragon.alive() && !struck) {
        for (uint32_t who : dragon.hurtBy) struck |= who != 0;
        struck |= dragon.health < dragon.maxHealth;
    }
    if (phase == InvasionPhase::Standing && realm.tick_ >= endsAt) {
        // Its time up: gone where it stands, without a fall (CleanUpMonstersAsync), as a summon
        // is dismissed.
        if (dragon.alive()) {
            dragon.health = 0;
            dragon.quarry = 0;
            realm.halt(dragon);
            realm.dropBlow(dragon);
            for (Body& one : realm.bodies_) {
                if (one.quarry == dragon.id) {
                    one.quarry = 0;
                    one.provoked = false;
                }
            }
            realm.say(What::Dismissed, dragon);
        }
        end(realm);
    }
}

void InvasionInstance::end(Realm& realm) {
    if (slot < 0 || phase == InvasionPhase::Quiet) return;
    Body& dragon = realm.bodies_[size_t(slot)];
    dragon.risesAt = std::numeric_limits<int64_t>::max();
    phase = InvasionPhase::Quiet;
    realm.say(What::Invasion, dragon, 0, dragon.homeColumn, dragon.homeRow);
}

// The Realm's door to it: the weather each frame, and --invasion's begin-now.
void Realm::invasionRain(bool raining, bool elsewhere) {
    invasion_.rain(*this, raining, elsewhere);
}
bool Realm::invade(bool now) { return invasion_.invade(*this, now); }

}  // namespace mu::sim
