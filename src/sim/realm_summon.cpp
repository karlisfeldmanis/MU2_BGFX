// The elf's summon (sprint 15, step 5): one monster of her own, raised beside her by a summon
// skill, that guards her, fights what she fights, and holds it.
//
// OpenMU's PlayerSummon and SummonedMonsterIntelligence are the source of the shape and of every
// distance: one at a time, raised within three tiles of her, hunting the nearest monster within
// eight tiles of HER, walking back when it strays past two (five while it fights), never turning
// on her, and a second cast dismisses it. What is ours, and the user's of 2026-09-28:
//   * **it scales with her level and her points**, energy most (skills.h, `summonLevel`,
//     `summonClimb`, `summonHealthRate` and `summonForceRate`; refit every tick it stands);
//   * **it holds aggro**: what it has struck stays on it though she shoots it too
//     (Realm::strikeAt), and it takes a monster that is on her before any other, so it peels
//     them off her -- the guard's "one on the hero first" (realm_watch.cpp);
//   * its kills are hers, drop and experience, as a guard's are when she helped (Realm::kill);
//   * it goes with her death rather than standing over her body, and with any warp of hers --
//     the Town Portal, and a map change, which raises a realm with its slot dormant (the user,
//     2026-09-29; OpenMU carries it to her landing gate instead).
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
    Body& summon = bodies_[size_t(summonSlot_)];
    const uint32_t id = summon.id;
    summon = Body{};
    summon.id = id;
    summon.summoner = hero.id;
    summon.summonedBy = row.number;
    summon.kind = kindAt;
    fitSummon(summon, hero);
    summon.health = summon.maxHealth;
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

void Realm::fitSummon(Body& summon, const Body& hero) {
    const content::MonsterKind& kind = tables_->kinds[size_t(summon.kind)];
    const int level = summonLevel(kind.level, hero.level, summon.summonedBy);
    const float lasts = summonHealthRate(hero.totalPoints());
    const float bites = summonForceRate(hero.totalPoints());
    const auto climb = [&](Ladder column) { return summonClimb(column, kind.level, level); };
    const int maxHealth =
        std::max(1, int(float(kind.health) * climb(Ladder::Health) * lasts));
    // A refit while it stands (she levelled, or spent a point) keeps its share of health.
    if (summon.maxHealth > 0 && summon.maxHealth != maxHealth) {
        summon.health = std::max(1, int(int64_t(summon.health) * maxHealth / summon.maxHealth));
    }
    summon.maxHealth = maxHealth;
    summon.level = level;
    summon.stats.level = level;
    summon.stats.attackRate = float(kind.attackRate) * climb(Ladder::AttackRate) * bites;
    summon.stats.defenseRate = float(kind.defenseRate) * climb(Ladder::DefenseRate) * bites;
    summon.stats.defense = int(float(kind.defense) * climb(Ladder::Defense) * bites);
    const float damage = climb(Ladder::Damage) * bites * kSummonDamageShare;
    summon.stats.minimumDamage = int(float(kind.minimumDamage) * damage);
    summon.stats.maximumDamage = int(float(kind.maximumDamage) * damage);
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
    // Her level and points as they are now: a level gained or a point spent mid-fight shows.
    fitSummon(summon, *owner);
    const content::MonsterKind& kind = tables_->kinds[size_t(summon.kind)];
    // Its breed's own pace, but walking back to her at hers -- on her mount it fell behind
    // and never caught up -- and a little over it so the gap closes (kSummonCatchUp, ours).
    const float pace = 1.0f / float(std::max(1, kind.moveTicks));
    summon.speed = summon.temper == Temper::Homing
                       ? std::max(pace, owner->speed * strideFactor(*owner) * kSummonCatchUp)
                       : pace;
    advance(summon);
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

    // Left far behind -- she rode off, or went through a door it is on the wrong side of -- it
    // is put down behind her rather than trailing across the map (kSummonBlink, ours).
    if (reach(summon, *owner) > float(kSummonBlink) && blinkSummon(summon, *owner)) return;

    // Tethered: past its leash from her it walks back beside her and does nothing else.
    const int tether = summon.quarry != 0 ? kSummonTetherFighting : kSummonTether;
    const float gap = reach(summon, *owner);
    if (gap > float(tether)) {
        // Walking back and not half a tile from where it stood a second and a half ago: caught
        // on something.
        if (summon.temper != Temper::Homing ||
            std::max(std::fabs(summon.x - summon.stuckX), std::fabs(summon.y - summon.stuckY)) >
                kSummonStuckGain) {
            summon.stuckX = summon.x;
            summon.stuckY = summon.y;
            summon.stuckAt = tick_;
        } else if (tick_ - summon.stuckAt >= kSummonStuckTicks &&
                   blinkSummon(summon, *owner)) {
            return;
        }
        summon.temper = Temper::Homing;
        if (tick_ >= summon.repathsAt) {
            summon.repathsAt = tick_ + kRepath;
            int column = 0, rowAt = 0;
            if (beside(*owner, 1, summon, &column, &rowAt)) {
                // No road back to her, or one round a wall that is longer than the blink: the
                // walk would be a lap of the building, so it steps through instead.
                if (!send(summon, column, rowAt)) {
                    blinkSummon(summon, *owner);
                } else {
                    float x = summon.x, y = summon.y, walk = 0.0f;
                    for (const Step& step : summon.route) {
                        walk += std::hypot(float(step.column) - x, float(step.row) - y);
                        x = float(step.column);
                        y = float(step.row);
                    }
                    if (walk > float(kSummonBlink * 2)) blinkSummon(summon, *owner);
                }
            }
        }
        return;
    }

    if (summon.quarry == 0) {
        summon.temper = Temper::Wandering;
        return;
    }
    Body& quarry = *body(summon.quarry);
    // Not through a wall, as a monster's shot (`seen`).
    if (within(summon, quarry, float(attackRange)) && seen(summon, quarry)) {
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
            if (beside(quarry, attackRange, summon, &column, &rowAt, true)) {
                send(summon, column, rowAt);
            }
        }
    }
}

bool Realm::blinkSummon(Body& summon, const Body& owner) {
    // Two tiles behind her, against the way she faces, so it falls in at her back and not in
    // her path; else any open tile within three of that; and it must see her, or it would land
    // on the far side of the wall it was stuck behind.
    const float backX = owner.x - std::cos(owner.facing) * 2.0f;
    const float backY = owner.y - std::sin(owner.facing) * 2.0f;
    int column = 0, rowAt = 0;
    const bool behind =
        router_.nearestOpen(int(std::lround(backX)), int(std::lround(backY)), wallOf(summon), 3,
                            &column, &rowAt) &&
        (column != owner.column() || rowAt != owner.row()) &&
        router_.sees(float(column), float(rowAt), owner.x, owner.y, wallOf(summon));
    if (!behind && !beside(owner, 2, summon, &column, &rowAt, true)) return false;
    halt(summon);
    summon.x = float(column);
    summon.y = float(rowAt);
    summon.aim = summon.facing = owner.facing;
    summon.temper = Temper::Wandering;
    summon.repathsAt = tick_ + kRepath;
    say(What::Blinked, summon, column, rowAt);
    return true;
}

}  // namespace mu::sim
