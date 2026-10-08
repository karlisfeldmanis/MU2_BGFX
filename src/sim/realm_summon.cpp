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
//   * **it is passive**: it fights only what is on her, on itself, or what she shoots;
//   * its kills are hers, drop and experience, as a guard's are when she helped (Realm::kill);
//   * it goes with her death rather than standing over her body, and with any warp of hers --
//     the Town Portal, and a map change, which raises a realm with its slot dormant (the user,
//     2026-09-29; OpenMU carries it to her landing gate instead);
//   * it is hidden while she stands in a safe zone and back beside her on her first step out
//     of one (the user, 2026-10-09; Realm::shelterSummon).
//
// The body is raised once, dormant, at the end of `bodies_` (Realm::raise) and reused, so casting
// never moves a pointer into `bodies_`. Its fights roll off `summonDice_`.
#include "sim/realm.h"
#include "sim/fmath.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

bool Realm::conjure(Body& hero, const SkillRow& row) {
    if (me().summonSlot < 0 || row.summons <= 0) return false;
    // None in Icarus: WebZen refuses the cast there (user.cpp:29567-29571), MuMain's client too
    // (ClassAttack.cpp:120), and one carried through the door is not raised on the other side.
    if (tables_->map == kIcarusMap) return false;
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
    Body& summon = bodies_[size_t(me().summonSlot)];
    const uint32_t id = summon.id;
    summon = Body{};
    route(summon).clear();  // a fresh body walks nothing, as one with its own route did
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
    route(summon).reserve(32);
    say(What::Spawned, summon, summon.level, summon.health);
    return true;
}

void Realm::fitSummon(Body& summon, const Body& hero) {
    const content::MonsterKind& kind = tables_->kinds[size_t(summon.kind)];
    const SummonFit fit = summonFit(kind, hero.level, hero.totalPoints(), summon.summonedBy);
    // A refit while it stands (she levelled, or spent a point) keeps its share of health.
    if (summon.maxHealth > 0 && summon.maxHealth != fit.health) {
        summon.health = std::max(1, int(int64_t(summon.health) * fit.health / summon.maxHealth));
    }
    summon.maxHealth = fit.health;
    summon.level = fit.level;
    summon.stats.level = fit.level;
    summon.stats.attackRate = fit.attackRate;
    summon.stats.defenseRate = fit.defenseRate;
    summon.stats.defense = fit.defense;
    summon.stats.minimumDamage = fit.minimumDamage;
    summon.stats.maximumDamage = fit.maximumDamage;
}

void Realm::dismiss(Body& summon) {
    if (summon.summoner == 0) return;
    // Gone for good, and one hidden in a safe zone (shelterSummon) with it: a warp, a gate, the
    // map travel or her death leaves nothing to come back on her next step out.
    if (const int at = playerOfId(summon.summoner); at >= 0) {
        heroes_[size_t(at)].summonOwed = 0;
        heroes_[size_t(at)].summonOwedHealth = 0;
    }
    vanish(summon);
}

void Realm::vanish(Body& summon) {
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

void Realm::shelterSummon(Body& hero) {
    // **Hidden in a safe zone, shown out of it** (the user, 2026-10-09: "in safezone summons are
    // hidden, but if summon buff is active and its not safezone we show summon"), ours: a summon
    // is a monster, and a monster's wall is the safe zone (content::kWallMonster), so one that
    // followed her to town stood stranded at its edge. Now the moment she stands on a safe tile it
    // goes out of the picture, its breed and health held as `summonOwed`, and the first tick she
    // stands off one it is raised beside her again at that health -- no mana, no cooldown, as a
    // save's summon is raised (Realm::restore). A gate, the map travel, a Town Portal, a Teleport
    // and her death still dismiss it for good (Realm::dismiss), as the user's rules of 2026-09-29
    // and 2026-10-08 have them.
    if (me().summonSlot < 0) return;
    Body& summon = bodies_[size_t(me().summonSlot)];
    const bool sheltered = tables_->grid.safe(hero.column(), hero.row());
    if (summon.alive()) {
        if (!sheltered || !hero.alive()) return;
        me().summonOwed = summon.summonedBy;
        me().summonOwedHealth = summon.health;
        vanish(summon);
        return;
    }
    if (me().summonOwed == 0) return;
    // Not raised for one who lies dead, nor in Icarus (Realm::conjure refuses it there): gone.
    if (!hero.alive() || tables_->map == kIcarusMap) {
        me().summonOwed = 0;
        me().summonOwedHealth = 0;
        return;
    }
    if (sheltered) return;  // held until she steps out
    const SkillRow* row = skillNumbered(me().summonOwed);
    if (row != nullptr && conjure(hero, *row) && me().summonOwedHealth > 0) {
        summon.health = std::min(me().summonOwedHealth, summon.maxHealth);
    }
    me().summonOwed = 0;
    me().summonOwedHealth = 0;
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
    // her, nearest first; else one on itself or the one she is shooting. The lower id on a tie.
    //
    // **Passive** (the user, 2026-10-08: "elf summon has to be passive, and only attack if elf
    // is attacked or elf is attacking"), ours, where OpenMU's hunts whatever stands within
    // eight tiles of her: a monster minding its own business is left to it.
    const auto wanted = [&](const Body& one) {
        return one.quarry == owner->id || one.quarry == summon.id || one.id == owner->blowTarget;
    };
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
            if (!wanted(one)) continue;
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
                    for (const Step& step : route(summon)) {
                        walk += fm::hypot(float(step.column) - x, float(step.row) - y);
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
    const float backX = owner.x - fm::cos(owner.facing) * 2.0f;
    const float backY = owner.y - fm::sin(owner.facing) * 2.0f;
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
