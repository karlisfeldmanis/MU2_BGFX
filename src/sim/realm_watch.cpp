// The town's guards: a body at each post the folk table gives a Berdysh or Crossbow Guard, who
// goes for any monster that comes within his sight, and walks back to his post when it is dead.
//
// OpenMU's `GuardIntelligence` is the source of the idea and of every number (realm_tuning.h's
// kWardens); what it does with them is ours and smaller. It picks the nearest monster in sight
// and fights it where it stands. This one may also take a few steps toward it, on a leash from
// his post (kWardenLeash, invention), because a guard who cannot move never reaches a monster
// that stops two tiles off.
//
// And he speaks, which is the user's (2026-09-28) and not MU's: a challenge when he goes for a
// monster, and when one he fought dies with the hero's help, where the rest of them are -- turning
// to look at the nearest of its kind still standing. The realm says only THAT he spoke and why (What::Shouted);
// the words are the drawing's.
//
// A guard takes no draw from the realm's dice: his fights, either way round, roll off a stream of
// their own (Realm::wardenDice_), so a seeded run is moved by a guard only where he kills.
#include "sim/realm.h"
#include "sim/fmath.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

namespace {

// How far a body stands from a guard's post, in whole tiles on MU's measure.
int fromPost(const Body& guard, const Body& one) {
    return std::max(std::abs(one.column() - guard.homeColumn), std::abs(one.row() - guard.homeRow));
}

// The facing a folk row's Look gives, in the sim's radians. The same arithmetic the drawing
// stood the townsfolk with (play_open.cpp): the Look is three eighths off its name, and
// `(look - 3) x 45 degrees` is the bearing from north toward east.
float lookAngle(int32_t look) {
    const int facing = look >= 1 && look <= 8 ? look : 3;
    const float bearing = float(((facing - 3) % 8 + 8) % 8) * (3.14159265359f / 4.0f);
    return fm::atan2(-fm::cos(bearing), fm::sin(bearing));
}

}  // namespace

void Realm::raiseWardens() {
    size_t raised = 0;
    for (size_t i = 0; i < tables_->folk.size(); ++i) {
        const content::Townsperson& person = tables_->folk[i];
        const WardenRow* row = wardenRow(person.number);
        if (row == nullptr) continue;
        // His own tile when he can stand on it, else the nearest he can: a post on a tile the
        // grid refuses would be a guard no route can start from.
        int column = person.x, rowAt = person.y;
        if (!router_.nearestOpen(person.x, person.y, content::kWallCharacter, 3, &column, &rowAt)) {
            core::logError("guard %s has nowhere to stand near (%d, %d)", person.name.c_str(),
                           person.x, person.y);
            continue;
        }
        Body guard;
        guard.id = nextId_++;
        guard.warden = int32_t(i);
        guard.level = row->level;
        guard.maxHealth = guard.health = row->health;
        guard.stats.level = row->level;
        guard.stats.attackRate = float(row->attackRate);
        guard.stats.defenseRate = float(row->defenseRate);
        guard.stats.defense = row->defense;
        guard.stats.minimumDamage = row->minimumDamage;
        guard.stats.maximumDamage = row->maximumDamage;
        guard.swingTicks = row->attackTicks;
        guard.speed = 1.0f / float(std::max(1, row->moveTicks));
        guard.x = float(column);
        guard.y = float(rowAt);
        guard.homeColumn = column;
        guard.homeRow = rowAt;
        guard.post = guard.facing = guard.aim = lookAngle(person.look);
        guard.temper = Temper::Wandering;
        guard.route.reserve(32);
        bodies_.push_back(std::move(guard));
        ++raised;
    }
    if (raised > 0) core::logf("realm: %zu guards at their posts", raised);
    // And after them whoever walks rounds, so no guard's id moves for him.
    raiseStrollers();
}

void Realm::watch(Body& guard) {
    const WardenRow* row = wardenRow(tables_->folk[size_t(guard.warden)].number);
    // A warden body with no guard's row is a townsperson on his rounds (realm_folk.cpp).
    if (row == nullptr) {
        stroll(guard);
        return;
    }
    advance(guard);
    // Whom he takes on, by distance from his post, and how far he may step: the town guards'
    // leash for both, or a guard of his own rules' reach plus his own leash (WardenRow::leash).
    const bool tethered = row->leash >= 0;
    const int watches = tethered ? row->attackRange + row->leash : kWardenLeash;
    const float share = row->share > 0.0f ? row->share : kWardenShare;

    // The monster he is on, while it lives and has not led him past his leash; else the nearest
    // awake one in his sight within the leash of his post -- one on the hero before any other --
    // the lower id on a tie.
    const uint32_t had = guard.quarry;
    const Body* held = find(guard.quarry);
    const uint32_t heroId = bodies_[0].id;
    const bool keep = held != nullptr && held->alive() && held->monster() && !isBoss(*held) &&
                      fromPost(guard, *held) <= watches;
    // Looked for again whenever what he holds is not on the hero, so a monster that turns on the
    // hero takes him off one that is only at the gate.
    if (!keep || held->quarry != heroId) {
        guard.quarry = keep ? had : 0;
        float closest = 1e30f;
        for (const Body& one : bodies_) {
            // Never the raid's dragon: a guard's blow is a share of what it strikes, and took
            // 91,200 off it a blow (the user, 2026-10-06: 'guards cant attack dragon').
            if (!one.monster() || !one.alive() || isBoss(one)) continue;
            // Awake only: a monster asleep is one nobody is near enough to see, and a guard
            // fighting it would be a fight in an empty street -- and would clear what spawns at
            // his gate before the hero ever walked out to it.
            if (one.temper == Temper::Asleep) continue;
            if (!within(guard, one, float(row->viewRange))) continue;
            if (fromPost(guard, one) > watches) continue;
            // One that is on the hero first, whatever else is nearer: he is there to keep the
            // hero as much as the gate, and a guard hacking at a spider while a dragon eats the
            // hero beside him is no guard.
            const bool onHero = one.quarry == heroId;
            // Held on to over anything but one on the hero.
            if (keep && !onHero) continue;
            const float distance = reach(guard, one) - (onHero ? 1000.0f : 0.0f);
            if (distance < closest) {
                closest = distance;
                guard.quarry = one.id;
            }
        }
        // Said once a monster, when he takes it on -- not on every tick of the fight, not again
        // when it steps out of his leash and back in, and not when a blow from the one he is
        // already on is what woke him.
        if (guard.quarry != 0 && guard.quarry != had && guard.quarry != guard.challenged) {
            guard.challenged = guard.quarry;
            say(What::Shouted, guard, int32_t(Shout::Challenge), -1, -1, guard.quarry);
        }
    }
    guard.provoked = false;

    if (guard.quarry == 0) {
        guard.temper = Temper::Wandering;
        // Looking where he pointed the hero, for as long as the line takes to read -- or where
        // he shot, until he could have shot again, still in his fighting stance: a guard who
        // kills with the blow would otherwise turn his back on it the tick after, and the
        // Golden Archer sling his crossbow while its shot still plays.
        if (tick_ < guard.standsUntil) {
            if (tick_ < guard.swingsAt) guard.temper = Temper::Fighting;
            return;
        }
        const bool home = guard.column() == guard.homeColumn && guard.row() == guard.homeRow;
        if (!home) {
            guard.temper = Temper::Homing;
            if (!guard.walking && tick_ >= guard.repathsAt) {
                guard.repathsAt = tick_ + kRepath;
                send(guard, guard.homeColumn, guard.homeRow);
            }
            return;
        }
        // At his post: facing the way he was put, whole again, and ready to challenge anything.
        if (!guard.walking) guard.aim = guard.post;
        guard.health = guard.maxHealth;
        guard.challenged = 0;
        return;
    }

    Body& quarry = *body(guard.quarry);
    // Not through a wall, as a monster's shot (`seen`).
    if (within(guard, quarry, float(row->attackRange)) && seen(guard, quarry)) {
        guard.temper = Temper::Fighting;
        engage(guard, quarry);
        if (tick_ >= guard.swingsAt) {
            guard.swingsAt = tick_ + row->attackTicks;
            quarry.guardedBy = guard.id;
            // His band, scaled so a blow is about kWardenShare of what the monster can take.
            const float middle = 0.5f * float(row->minimumDamage + row->maximumDamage);
            const float force = std::max(0.01f, float(quarry.maxHealth) * share / middle);
            strikeAt(guard, quarry, force);
            // Facing what he struck until his next blow is due, if it fell (above); a pointing
            // the kill started (pointOn) is held its own longer while.
            guard.standsUntil = std::max(guard.standsUntil, guard.swingsAt);
        }
        return;
    }
    // Out of reach: close, on the same re-plan clock and the same drift a monster's chase uses.
    guard.temper = Temper::Chasing;
    if (tick_ >= guard.repathsAt) {
        guard.repathsAt = tick_ + kRepath;
        if (drifted(guard, quarry)) {
            guard.chaseX = quarry.x;
            guard.chaseY = quarry.y;
            int column = 0, rowAt = 0;
            // A tethered guard steps only to a tile within his leash of the post, and otherwise
            // waits where he is for it to come into his reach.
            if (beside(quarry, std::max(1, row->attackRange), guard, &column, &rowAt, true) &&
                (!tethered || std::max(std::abs(column - guard.homeColumn),
                                       std::abs(rowAt - guard.homeRow)) <= row->leash)) {
                send(guard, column, rowAt);
            }
        }
    }
}

void Realm::pointOn(Body& guard, const Body& dead) {
    // The nearest of its kind still standing, from where he stands; the nearest monster of any
    // kind when none of its own is left. Nearest by MU's measure, the lower id on a tie.
    const Body* more = nullptr;
    float closest = 1e30f;
    for (int pass = 0; pass < 2 && more == nullptr; ++pass) {
        for (const Body& one : bodies_) {
            if (!one.monster() || !one.alive() || one.id == dead.id) continue;
            if (pass == 0 && one.kind != dead.kind) continue;
            const float distance = reach(guard, one);
            if (distance < closest) {
                closest = distance;
                more = &one;
            }
        }
    }
    const int32_t column = more ? more->column() : -1;
    const int32_t rowAt = more ? more->row() : -1;
    say(What::Shouted, guard, int32_t(Shout::Pointing), column, rowAt, more ? more->id : 0);
    // And he turns to look that way, and holds it while the line is read.
    if (more) {
        halt(guard);
        guard.aim = fm::atan2(more->y - guard.y, more->x - guard.x);
        guard.standsUntil = tick_ + kPointTicks;
    }
}

}  // namespace mu::sim
