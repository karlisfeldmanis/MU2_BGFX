// Burning bodies: a monster an Immolate rune lit (Realm::ignite, realm_fight.cpp) and a character
// a burner's fire lay lit (Hell Maine's, realm_tuning.h kBurners), each pulsing while it burns.
// Out of realm_fight.cpp, which is at its size.
#include "sim/realm.h"

#include <algorithm>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

void Realm::burnPulse(Body& beast) {
    if (beast.burnUntil == 0 || tick_ < beast.burnNext) return;
    if (beast.burnNext > beast.burnUntil || !beast.alive()) {
        beast.burnUntil = 0;
        return;
    }
    beast.burnNext += beast.player ? kHeroBurnEvery : kBurnRuneEvery;
    Body* by = body(beast.burnBy);
    if (by == nullptr || !by->alive()) {
        beast.burnUntil = 0;
        return;
    }
    // On him, a burner's fire (kBurners) as a poison: his shield first, never his last point.
    if (beast.player) {
        int wound = beast.burnDamage;
        if (beast.sd > 0) {
            const int onto = int(float(wound) * kShieldShare);
            const int over = onto - beast.sd;
            beast.sd = std::max(0, beast.sd - onto);
            wound = wound - onto + std::max(0, over);
        }
        const int bite = std::min(wound, beast.health - 1);
        const int said = beast.burnDamage - wound + std::max(0, bite);
        if (said <= 0) return;
        if (bite > 0) beast.health -= bite;
        if (bite > 0) noteHurt(beast, *by, bite);
        say(What::Hit, *by, said, said, beast.health, beast.id);
        happenings_.back().thrown = true;
        happenings_.back().burned = true;
        return;
    }
    const int bite = std::min(beast.burnDamage, beast.health);
    beast.health -= bite;
    noteHurt(beast, *by, bite);
    say(What::Hit, *by, bite, bite, beast.health, beast.id);
    happenings_.back().thrown = true;
    happenings_.back().burned = true;
    if (beast.health <= 0) kill(beast, *by);
}

// A burner's fire lay that landed (realm_tuning.h kBurners): him set burning, one at a time,
// unless his fire resistance turns it aside.
void Realm::layFire(Body& by, Body& target, int damage) {
    if (!target.player || !target.alive() || target.burnUntil > tick_ ||
        heroResists(target.excel.fireResistance)) {
        return;
    }
    target.burnDamage = std::max(1, int(float(damage) * kHeroBurnShare));
    target.burnUntil = tick_ + kHeroBurnTicks;
    target.burnNext = tick_ + kHeroBurnEvery;
    target.burnBy = by.id;
    core::logf("fire lay: tick %lld, #%u burns #%u for %d a pulse", (long long)tick_, by.id, target.id,
               target.burnDamage);
}

}  // namespace mu::sim
