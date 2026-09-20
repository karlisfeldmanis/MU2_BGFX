#include "sim/audit.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace mu::sim {
namespace {

// The grudge, which is the widest leash anything is allowed. A monster dragged further than
// this has lost its quarry and is walking home, and it may be a step or two outside while it
// does, so the check allows the route's own slack rather than the number exactly.
constexpr int kFurthest = 20 + 2;

void note(Findings& findings, const char* format, ...) {
    if (findings.first.size() >= 16) return;
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    findings.first.push_back(line);
}

}  // namespace

void audit(const Realm& realm, Findings& findings) {
    const content::Tables* tables = realm.tables();
    if (!tables) return;

    for (const Body& one : realm.bodies()) {
        // A body standing where the grid refuses it. The dead are exempt: a corpse lies where
        // it fell and is not standing anywhere.
        if (one.alive() && !tables->grid.open(one.column(), one.row(), content::kWallCharacter)) {
            ++findings.onBlocked;
            note(findings, "tick %lld: body %u stands on blocked tile (%d, %d), word 0x%02x",
                 (long long)realm.tick(), one.id, one.column(), one.row(),
                 tables->grid.at(one.column(), one.row()));
        }
        if (one.health < 0) {
            ++findings.belowZero;
            note(findings, "tick %lld: body %u has %d health", (long long)realm.tick(), one.id,
                 one.health);
        }
        if (!one.player && one.alive()) {
            const int away = std::max(std::abs(one.column() - one.homeColumn),
                                      std::abs(one.row() - one.homeRow));
            if (away > kFurthest) {
                ++findings.pastTheLeash;
                note(findings, "tick %lld: body %u is %d tiles from its nest",
                     (long long)realm.tick(), one.id, away);
            }
        }
    }

    // And what this tick's happenings say about each other. A blow on the dead is the one that
    // cannot be seen from the state afterwards -- the victim is dead either way -- so it is
    // caught here, by reading the order the happenings came out in.
    for (size_t i = 0; i < realm.happenings().size(); ++i) {
        const Happening& happening = realm.happenings()[i];
        if (happening.what == What::Hit || happening.what == What::Missed) {
            for (size_t j = 0; j < i; ++j) {
                const Happening& earlier = realm.happenings()[j];
                if (earlier.what == What::Died && earlier.who == happening.whom) {
                    ++findings.hitTheDead;
                    note(findings, "tick %lld: body %u swung at %u, which died this tick",
                         (long long)realm.tick(), happening.who, happening.whom);
                    break;
                }
                if (earlier.what == What::Rose && earlier.who == happening.whom) break;
            }
        }
        if (happening.what == What::Levelled) {
            const Body* hero = realm.find(happening.who);
            // Every level-up is preceded by the experience that pays for it: the total standing
            // after it must be at least what the level costs.
            if (hero && hero->experience < neededExperience(happening.a)) {
                ++findings.unpaidLevel;
                note(findings, "tick %lld: body %u reached level %d with %llu of %llu experience",
                     (long long)realm.tick(), happening.who, happening.a,
                     (unsigned long long)hero->experience,
                     (unsigned long long)neededExperience(happening.a));
            }
        }
    }
}

}  // namespace mu::sim
