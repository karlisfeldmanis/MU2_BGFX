// The elf's summon (sprint 15, step 5): one monster of her own, raised beside her by a summon
// skill, that guards her, fights what she fights, and holds it.
//
// OpenMU's PlayerSummon and SummonedMonsterIntelligence are the source of the shape and of every
// distance: one at a time, raised within three tiles of her, hunting the nearest monster within
// eight tiles of HER, walking back when it strays past two (five while it fights), never turning
// on her, and a second cast dismisses it. What is ours, and the user's of 2026-09-28:
//   * **it scales with her energy** (skills.h, `summonHealthRate` and `summonForceRate`);
//   * **it holds aggro**: what it has struck stays on it though she shoots it too
//     (Realm::strikeAt), and it takes a monster that is on her before any other, so it peels
//     them off her -- the guard's "one on the hero first" (realm_watch.cpp);
//   * its kills are hers, drop and experience, as a guard's are when she helped (Realm::kill);
//   * it goes with her death rather than standing over her body.
//
// The body is raised once, dormant, at the end of `bodies_` (Realm::raise) and reused, so casting
// never moves a pointer into `bodies_`. Its fights roll off `summonDice_`.
#include "sim/realm.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

bool Realm::conjure(Body& hero, const SkillRow& row) {
    if (summonSlot_ < 0 || row.summons <= 0) return false;
    int32_t kindAt = -1;
    for (size_t i = 0; i < tables_->kinds.size(); ++i) {
        if (tables_->kinds[i].number == row.summons) {
            kindAt = int32_t(i);
            break;
        }
    }
    // A breed this game has not cooked cannot be raised, and the cast is refused before it costs
    // anything.
    if (kindAt < 0) return false;
    int column = hero.column(), rowAt = hero.row();
    if (!router_.nearestOpen(hero.column() + 1, hero.row(), content::kWallCharacter, 3, &column,
                             &rowAt)) {
        return false;
    }
    const content::MonsterKind& kind = tables_->kinds[size_t(kindAt)];
    const float lasts = summonHealthRate(hero.points.energy);
    const float bites = summonForceRate(hero.points.energy);
    Body& summon = bodies_[size_t(summonSlot_)];
    const uint32_t id = summon.id;
    summon = Body{};
    summon.id = id;
    summon.summoner = hero.id;
    summon.summonedBy = row.number;
    summon.kind = kindAt;
    summon.level = kind.level;
    summon.maxHealth = summon.health = std::max(1, int(float(kind.health) * lasts));
    summon.stats.level = kind.level;
    summon.stats.attackRate = float(kind.attackRate) * bites;
    summon.stats.defenseRate = float(kind.defenseRate) * bites;
    summon.stats.defense = int(float(kind.defense) * bites);
    summon.stats.minimumDamage = int(float(kind.minimumDamage) * bites);
    summon.stats.maximumDamage = int(float(kind.maximumDamage) * bites);
    summon.swingTicks = kind.attackTicks;
    summon.speed = 1.0f / float(std::max(1, kind.moveTicks));
    summon.x = float(column);
    summon.y = float(rowAt);
    summon.homeColumn = column;
    summon.homeRow = rowAt;
    summon.aim = summon.facing = hero.facing;
    summon.temper = Temper::Wandering;
    summon.swingsAt = tick_ + kind.attackTicks;
    summon.route.reserve(32);
    say(What::Spawned, summon, summon.level, summon.health);
    return true;
}

void Realm::dismiss(Body& summon) {
    if (summon.summoner == 0 || !summon.alive()) return;
    summon.health = 0;
    summon.quarry = 0;
    halt(summon);
    dropBlow(summon);
    // Whatever was fighting it forgets it, as they forget a corpse.
    for (Body& one : bodies_) {
        if (one.quarry == summon.id) {
            one.quarry = 0;
            one.provoked = false;
        }
    }
    say(What::Dismissed, summon);
}

void Realm::tend(Body& summon) {
    if (!summon.alive()) return;
    const Body* owner = body(summon.summoner);
    if (owner == nullptr || !owner->alive()) {
        dismiss(summon);
        return;
    }
    advance(summon);
    const content::MonsterKind& kind = tables_->kinds[size_t(summon.kind)];
    const int attackRange = std::max(1, kind.attackRange);

    // The one it is on, while it lives and stays within the hunt round her; else a monster on
    // her, nearest first; else the nearest awake one within the hunt. The lower id on a tie.
    const Body* held = find(summon.quarry);
    const bool keep = held != nullptr && held->alive() && held->monster() &&
                      within(*owner, *held, float(kSummonHunt)) &&
                      !tables_->grid.safe(held->column(), held->row());
    if (!keep || held->quarry != owner->id) {
        uint32_t chosen = keep ? summon.quarry : 0;
        float closest = 1e30f;
        for (const Body& one : bodies_) {
            if (!one.monster() || !one.alive()) continue;
            if (tables_->grid.safe(one.column(), one.row())) continue;
            if (!within(*owner, one, float(kSummonHunt))) continue;
            // Asleep or not: a sleeping monster is only one no player is near enough to wake,
            // and OpenMU's summon hunts whatever stands within eight tiles of her. Its blow
            // wakes what it strikes.
            const bool onHer = one.quarry == owner->id;
            if (!onHer && keep) continue;
            const float distance = reach(summon, one) - (onHer ? 1000.0f : 0.0f);
            if (distance < closest) {
                closest = distance;
                chosen = one.id;
            }
        }
        summon.quarry = chosen;
    }

    // Tethered: past its leash from her it walks back beside her and does nothing else.
    const int tether = summon.quarry != 0 ? kSummonTetherFighting : kSummonTether;
    if (reach(summon, *owner) > float(tether)) {
        summon.temper = Temper::Homing;
        if (tick_ >= summon.repathsAt) {
            summon.repathsAt = tick_ + kRepath;
            int column = 0, rowAt = 0;
            if (beside(*owner, 1, summon, &column, &rowAt)) send(summon, column, rowAt);
        }
        return;
    }

    if (summon.quarry == 0) {
        summon.temper = Temper::Wandering;
        return;
    }
    Body& quarry = *body(summon.quarry);
    if (within(summon, quarry, float(attackRange))) {
        summon.temper = Temper::Fighting;
        engage(summon, quarry);
        if (tick_ >= summon.swingsAt) {
            summon.swingsAt = tick_ + summon.swingTicks;
            strikeAt(summon, quarry);
        }
        return;
    }
    summon.temper = Temper::Chasing;
    if (tick_ >= summon.repathsAt) {
        summon.repathsAt = tick_ + kRepath;
        if (drifted(summon, quarry)) {
            summon.chaseX = quarry.x;
            summon.chaseY = quarry.y;
            int column = 0, rowAt = 0;
            if (beside(quarry, attackRange, summon, &column, &rowAt)) send(summon, column, rowAt);
        }
    }
}

}  // namespace mu::sim
