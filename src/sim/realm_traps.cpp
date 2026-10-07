// The Dungeon's traps in the realm: raised off sim/traps.h's table, fired on their own clock at
// the hero. See sim/traps.h for the record and the two decisions in it.
#include <algorithm>
#include <cstdlib>

#include "sim/realm.h"
#include "sim/recovery.h"
#include "sim/traps.h"

namespace mu::sim {
namespace {

// AttackDelay, 1000 ms, at the realm's 20 Hz.
constexpr int64_t kTrapEvery = 20;

}  // namespace

void Realm::raiseTraps() {
    traps_.clear();
    if (!tables_) return;
    size_t count = 0;
    const TrapSpot* spots = trapSpots(&count);
    for (size_t i = 0; i < count; ++i) {
        const TrapSpot& spot = spots[i];
        if (spot.map != tables_->map || trapKind(spot.number) == nullptr) continue;
        Trap trap;
        trap.number = spot.number;
        trap.column = spot.column;
        trap.row = spot.row;
        trap.dx = spot.dx;
        trap.dy = spot.dy;
        // TrapIntelligenceBase.Start: the first after AttackDelay and up to a second more, so a
        // corridor of them does not fire as one.
        trap.firesAt = tick_ + kTrapEvery + trapDice_.nextInt(0, int(kTrapEvery));
        traps_.push_back(trap);
    }
}

void Realm::fireTraps() {
    if (traps_.empty()) return;
    for (size_t i = 0; i < traps_.size(); ++i) {
        Trap& trap = traps_[i];
        if (tick_ < trap.firesAt) continue;
        trap.firesAt = tick_ + kTrapEvery;
        // At every player in its way, in the order they joined.
        for (size_t p = 0; p < heroes_.size(); ++p) {
            For him(*this, p);
            fireTrap(i, mine());
        }
    }
}

void Realm::fireTrap(size_t i, Body& hero) {
    const Trap& trap = traps_[i];
    if (!hero.alive()) return;
    const TrapKind& kind = *trapKind(trap.number);
    const int dx = hero.column() - trap.column, dy = hero.row() - trap.row;
    bool caught = false;
    if (kind.pressed) {
        // AttackSingleWhenPressedTrapIntelligence: on its own tile.
        caught = dx == 0 && dy == 0;
    } else {
        // AttackAreaTargetInDirectionTrapIntelligence: the way it faces, within its range
        // (IsInRange, a square), and not on a safe tile.
        caught = octantOf(dx, dy) == octantOf(trap.dx, trap.dy) &&
                 std::abs(dx) <= kind.attackRange && std::abs(dy) <= kind.attackRange &&
                 !tables_->grid.safe(hero.column(), hero.row());
    }
    if (!caught) return;

    // Trap.AttackAsync: the hero's AttackByAsync, the same roll any monster's blow takes.
    Fighter fighter;
    fighter.level = kind.level;
    fighter.attackRate = kind.attackRate;
    fighter.defenseRate = kind.defenseRate;
    fighter.minimumDamage = kind.minimumDamage;
    fighter.maximumDamage = kind.maximumDamage;
    const Blow blow = strike(fighter, hero.stats, trapDice_);
    int damage = 0;
    if (blow.hit) {
        damage = blow.damage;
        // The shield's nine tenths first, as every blow on him (Realm::strikeAt).
        int wound = damage;
        if (hero.sd > 0) {
            const int onto = int(float(damage) * kShieldShare);
            const int over = onto - hero.sd;
            hero.sd = std::max(0, hero.sd - onto);
            wound = damage - onto + std::max(0, over);
        }
        hero.health = std::max(0, hero.health - wound);
        if (wound > 0) wearOnTaken(wound);
    }
    say(What::Trapped, hero, damage, int32_t(i), hero.health);
    // Nobody to credit: a trap is no body, so he is his own killer, which is what the log
    // and the fall read.
    if (hero.health <= 0) kill(hero, hero);
}

}  // namespace mu::sim
