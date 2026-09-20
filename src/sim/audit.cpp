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

    // And what this tick's happenings say about each other, read in the order they came out
    // in and against who was already dead when the tick began. A blow on the dead cannot be
    // seen from the state afterwards -- the victim is dead either way -- and it cannot be seen
    // from one tick's happenings either, because a body that died an hour ago has no Died line
    // in this tick's list. So the deaths are carried.
    if (findings.dead.size() < realm.bodies().size() + 2) {
        findings.dead.assign(realm.bodies().size() + 2, 0);
        for (const Body& one : realm.bodies()) {
            if (one.id < findings.dead.size()) findings.dead[one.id] = one.alive() ? 0 : 1;
        }
    }
    for (size_t i = 0; i < realm.happenings().size(); ++i) {
        const Happening& happening = realm.happenings()[i];
        if (happening.what == What::Hit || happening.what == What::Missed) {
            const uint32_t whom = happening.whom;
            if (whom < findings.dead.size() && findings.dead[whom]) {
                ++findings.hitTheDead;
                note(findings, "tick %lld: body %u swung at %u, which was already dead",
                     (long long)realm.tick(), happening.who, whom);
            }
        }
        if (happening.what == What::Died && happening.who < findings.dead.size()) {
            findings.dead[happening.who] = 1;
        }
        if (happening.what == What::Rose && happening.who < findings.dead.size()) {
            findings.dead[happening.who] = 0;
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
