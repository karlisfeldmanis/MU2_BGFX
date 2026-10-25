// Burning bodies: a monster an Immolate rune lit (Realm::ignite, realm_fight.cpp) and a character
// a burner's fire lay lit (Hell Maine's, realm_tuning.h kBurners), each pulsing while it burns.
// And a character a bleeder's claws cut (Aida's orcs, kBleeders), stacking, and a monster a
// bleed rune cut (Realm::rend).
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

// ---- bleeding: a bleeder's claws on him (realm_tuning.h kBleeders, kBleedMost) ----------------

bool Realm::bleeds(const Body& monster) const {
    if (monster.player || monster.kind < 0 || size_t(monster.kind) >= tables_->kinds.size()) {
        return false;
    }
    const int32_t number = tables_->kinds[size_t(monster.kind)].number;
    for (const int32_t one : kBleeders) {
        if (one == number) return true;
    }
    return false;
}

// A bleeder's blow that landed for `damage`: a stack more, to kBleedMost, and the bleed
// kBleedTicks from now, each stack kBleedShare of this blow. A cut while it bleeds keeps its
// pulse's beat; a first cut's first pulse is kBleedEvery away.
void Realm::cut(Body& by, Body& target, int damage) {
    if (!target.player || !target.alive() || damage <= 0) return;
    const bool bleeding = target.bleedUntil > tick_ && target.bleed > 0;
    target.bleed = bleeding ? std::min(kBleedMost, target.bleed + 1) : 1;
    target.bleedDamage = std::max(1, int(float(damage) * kBleedShare));
    target.bleedUntil = tick_ + kBleedTicks;
    if (!bleeding) target.bleedNext = tick_ + kBleedEvery;
    target.bleedBy = by.id;
    core::logf("cut: tick %lld, #%u cuts #%u, %d stack(s) of %d a pulse", (long long)tick_, by.id,
               target.id, target.bleed, target.bleedDamage);
}

// A bleed rune's roll off his swing that landed for `wound` (sim/items.h kGrazeBleed...): a
// stack more on the monster, as a bleeder's claws lay one on him, each stack the tier's share
// of this swing. Rolled first, so the dice do not hang on what it struck.
void Realm::rend(Body& hero, Body& struck, const BleedRune& rune, int wound) {
    if (!runeDice_.nextBool(rune.chance)) return;
    if (!struck.alive() || !struck.monster() || wound <= 0) return;
    const bool bleeding = struck.bleedUntil > tick_ && struck.bleed > 0;
    struck.bleed = bleeding ? std::min(kBleedMost, struck.bleed + 1) : 1;
    struck.bleedDamage = std::max(1, int(float(wound) * rune.share));
    struck.bleedUntil = tick_ + kBleedTicks;
    if (!bleeding) struck.bleedNext = tick_ + kBleedEvery;
    struck.bleedBy = hero.id;
    struck.bleedRequiem = false;
    core::logf("rend: tick %lld, #%u cuts #%u, %d stack(s) of %d a pulse", (long long)tick_,
               hero.id, struck.id, struck.bleed, struck.bleedDamage);
}

void Realm::bleedPulse(Body& one) {
    if (one.bleedUntil == 0 || tick_ < one.bleedNext) return;
    if (one.bleedNext > one.bleedUntil || !one.alive() || one.bleed <= 0) {
        one.bleed = 0;
        one.bleedUntil = 0;
        one.bleedRequiem = false;
        return;
    }
    one.bleedNext += kBleedEvery;
    Body* by = body(one.bleedBy);
    // On a monster, a bleed rune's cut: the whole pulse, which may kill, his while he stands.
    if (!one.player) {
        if (by == nullptr || !by->alive()) {
            one.bleed = 0;
            one.bleedUntil = 0;
            return;
        }
        const int bite = std::min(one.bleedDamage * one.bleed, one.health);
        one.health -= bite;
        noteHurt(one, *by, bite);
        say(What::Hit, *by, bite, bite, one.health, one.id);
        happenings_.back().thrown = true;
        happenings_.back().bled = true;
        if (one.health <= 0) kill(one, *by);
        return;
    }
    if (by == nullptr) by = &one;
    // As a poison's pulse: his shield first, never his last point.
    const int due = one.bleedDamage * one.bleed;
    int wound = due;
    if (one.sd > 0) {
        const int onto = int(float(due) * kShieldShare);
        const int over = onto - one.sd;
        one.sd = std::max(0, one.sd - onto);
        wound = due - onto + std::max(0, over);
    }
    const int bite = std::min(wound, one.health - 1);
    const int said = due - wound + std::max(0, bite);
    if (said <= 0) return;
    if (bite > 0) one.health -= bite;
    if (bite > 0) noteHurt(one, *by, bite);
    say(What::Hit, *by, said, said, one.health, one.id);
    happenings_.back().thrown = true;
    happenings_.back().bled = true;
}

}  // namespace mu::sim
