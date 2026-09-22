// Getting there, and deciding to: a body sent, halted, turned and advanced along its route,
// and a monster roused, wandering, retreating, engaging and thinking.
//
// Every number these turn on is in sim/realm_tuning.h with its source named. The one worth
// repeating here is that a walk is a line whose length takes the time -- A*'s 5-straight
// 7-diagonal is a SEARCH cost and counting it twice makes diagonals 41%% slow
// (docs/conventions.md, "Time").
#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

bool Realm::send(Body& one, int column, int row) {
    // The tile he is standing on, asked for while he is between two tiles: a stop THERE. The
    // router has no route from a tile to itself, and this used to be a refusal, which left the
    // old walk running -- a click on his own feet mid-stride carried him on to wherever he was
    // going before. Half a step back to the centre of the tile is the answer the click meant.
    if (column == one.column() && row == one.row() &&
        (std::fabs(one.x - float(column)) > 1e-3f || std::fabs(one.y - float(row)) > 1e-3f)) {
        one.route.clear();
        one.route.push_back(Step{int16_t(column), int16_t(row)});
        one.onStep = 0;
        one.walking = true;
        say(What::Walked, one, column, row, 1);
        return true;
    }
    if (!router_.plan(one.column(), one.row(), column, row, content::kWallCharacter, scratch_)) {
        // A refusal is an event and not a silence. It was a silence for one evening, and the
        // scripted hand -- which asks again whenever it is not walking -- asked for the same
        // impossible tile every tick for the rest of the run: nine thousand ticks in which
        // nothing whatever was written to the log, because nothing whatever happened.
        say(What::Refused, one, column, row);
        return false;
    }
    const int32_t tiles = int32_t(scratch_.size());
    // Pulled tight from where the body really stands, MU2's Route.Along: an eight-way search
    // on tiles answers any heading that is not straight or diagonal with a staircase, and
    // walked tile to tile that staircase is a zig-zag. The legs are exactly as clear as the
    // tiles were -- Router::sees tests every tile a leg touches -- so nothing walks through
    // anything the plan went round. Invention against MU, which walks the staircase.
    router_.pull(one.x, one.y, content::kWallCharacter, scratch_);
    one.route.assign(scratch_.begin(), scratch_.end());
    one.onStep = 0;
    one.walking = true;
    // The goal the route actually ends on, not the one asked for: the router moves a goal in a
    // wall to the nearest open tile, and the marker is put down from this event.
    say(What::Walked, one, one.route.back().column, one.route.back().row, tiles);
    return true;
}

void Realm::halt(Body& one) {
    if (!one.walking) return;
    one.walking = false;
    one.route.clear();
    one.onStep = 0;
    say(What::Halted, one);
}

// One tick of movement along the line the route describes. The diagonal is NOT penalised here
// and that is the whole of the diagonal question: MU's A* costs a diagonal 7 against a
// straight 5 because that is a SEARCH cost, and OpenMU's GetStepDelay scales the step delay by
// 1.414 because that is a SPEED. Taking both makes a diagonal walk 41% slow and taking neither
// makes it fast. Here a walk is a line and a line has a length, which is MU2's answer
// (Walker.cs:63-67) and is the one that needs no penalty of either kind.
// Turns a body toward its aim and says whether it is still swinging round on the spot -- in
// which case it may not cover ground this tick. Walker.cs:275-286.
bool Realm::turn(Body& one) {
    constexpr float kToRadians = 3.14159265359f / 180.0f;
    const float apart = wrapped(one.aim - one.facing);
    // One tick's worth, and the tick is the only clock: 900 degrees a second at 20 Hz is 45
    // degrees a tick, so a full reversal takes four ticks.
    const float most = kTurnDegrees * kToRadians / 20.0f;
    // Kept within a half turn either side of nothing: a hand that keeps clicking behind him
    // wound it past 400 degrees, harmless to every reader but a trap for the next one.
    one.facing = wrapped(std::fabs(apart) <= most ? one.aim
                                                  : one.facing + (apart < 0.0f ? -most : most));
    one.turning = std::fabs(wrapped(one.aim - one.facing)) > kPivotDegrees * kToRadians;
    return one.turning;
}

void Realm::advance(Body& one) {
    // A body that is standing still still comes round: a fighter between two blows turns onto
    // what it is hitting, and a walk that has just been given spends its first tick or two
    // turning before any ground is covered.
    if (!one.walking) {
        turn(one);
        return;
    }
    // Aimed at the tile it is walking to, and turned toward it BEFORE any ground is covered.
    if (one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        if (dx * dx + dy * dy > 1e-6f) one.aim = std::atan2(dy, dx);
    }
    if (turn(one)) return;  // still coming round: no ground this tick
    // A tile crossed is said when the body's own tile changes, not when it reaches a point of
    // its route: a pulled route's points are the ends of long legs, and the log's Stepped
    // still means one tile.
    const int wasColumn = one.column(), wasRow = one.row();
    float left = one.speed;
    while (left > 0.0f && one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1e-6f) {
            ++one.onStep;
            continue;
        }
        one.aim = std::atan2(dy, dx);
        if (distance <= left) {
            one.x = float(target.column);
            one.y = float(target.row);
            left -= distance;
            ++one.onStep;
        } else {
            one.x += dx / distance * left;
            one.y += dy / distance * left;
            left = 0.0f;
        }
    }
    if (one.column() != wasColumn || one.row() != wasRow) {
        say(What::Stepped, one, one.column(), one.row());
    }
    if (one.onStep >= one.route.size()) {
        one.walking = false;
        one.route.clear();
        one.onStep = 0;
        say(What::Halted, one);
    }
}

// ---- the mind --------------------------------------------------------------------------

bool Realm::worth(const Body& beast, const Body& target, int range) const {
    return target.alive() && within(beast, target, float(range)) &&
           !tables_->grid.safe(target.column(), target.row());
}

void Realm::rouse(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    const float far = float(kind.viewRange + kMargin);
    float nearest = 1e30f;
    for (uint32_t who : players_) {
        // Alive or not, and the "or not" is the point: being roused is about whether anybody
        // can SEE the monster, and a dead character is still standing there. Whether there is
        // anything worth attacking is a separate question and worth() does check.
        nearest = std::min(nearest, reach(beast, bodies_[who]));
    }
    const bool roused = nearest <= far || (beast.provoked && beast.quarry != 0);
    const bool was = beast.temper != Temper::Asleep;
    if (roused == was) return;

    if (roused) {
        beast.temper = Temper::Wandering;
        // A monster that has just woken thinks on the NEXT tick and with a jitter, which is
        // OpenMU's start delay: a whole nest that woke on one tick would swing in unison
        // forever after. No jitter for one woken by a blow -- there is one animal, it has
        // already been told what hit it, and the stagger would only be slowness where somebody
        // is watching for a reaction.
        beast.thinksAt = tick_ + (beast.provoked ? 1 : dice_.nextInt(1, std::max(2, 20 / 4)));
        say(What::Roused, beast, 1, nearest > 1e29f ? -1 : int32_t(nearest));
    } else {
        beast.temper = Temper::Asleep;
        beast.quarry = 0;
        beast.provoked = false;
        halt(beast);
        say(What::Roused, beast, 0, -1);
    }
}

void Realm::wander(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    if (kind.moveRange <= 0) return;
    // Two draws, always in this order, and the tile is tested rather than corrected: a
    // wander that retried until it found somewhere would consume a variable number of draws.
    const int column = beast.column() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    const int row = beast.row() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    if (tables_->grid.open(column, row, content::kWallCharacter)) send(beast, column, row);
}

void Realm::retreat(Body& beast) {
    if (beast.walking || tick_ < beast.repathsAt) return;
    beast.repathsAt = tick_ + kRepath;
    if (!send(beast, beast.homeColumn, beast.homeRow)) wander(beast);
}

void Realm::engage(Body& one, const Body& target) {
    halt(one);
    // Aimed at what it is hitting. A halt keeps whatever aim the walk it ended had, and a blow
    // is the only other thing that aims, so a fighter stopped in reach between two blows faces
    // wherever it was last walking -- MU2 measured a Skeleton Warrior 105 degrees off the
    // knight it was fighting.
    one.aim = std::atan2(target.y - one.y, target.x - one.x);
}

// Whether a chase needs planning again: the chaser has stopped, or its quarry has moved a whole
// tile from where it was when the line was drawn. Realm.cs:1985-1996, and the tile is MU2's
// `Drift`: under it the approach tile would not change anyway and re-planning only jitters the
// line; over it the walker is heading somewhere its quarry has left.
//
// This was `quarry.column() != chaseColumn`, which is a WHOLE-tile comparison and fires the
// moment a monster's rounded position changes -- so a fighter standing next to something that
// shuffled between two tiles re-planned three times a second and took a step each time. It read
// as a character walking while he was hitting something, which is what it was: measured over
// 4 000 ticks, 205 walk orders for 140 blows. With the drift it is 24.
bool Realm::drifted(const Body& chaser, const Body& target) const {
    constexpr float kDrift = 1.0f;
    return !chaser.walking ||
           std::fabs(target.x - chaser.chaseX) + std::fabs(target.y - chaser.chaseY) > kDrift;
}

// The nearest tile beside a target that a walker can both stand on and finish its approach
// from. Nearest rather than OpenMU's random one: the randomness is there to stop a pack
// converging on one tile, and nearest-to-the-walker keeps that spread by construction while
// being STABLE -- asked again a tick later from almost the same place it gives the same
// answer, so a chase that re-plans three times a second does not zig-zag.
bool Realm::beside(const Body& target, int radius, const Body& walker, int* column, int* row) {
    bool found = false;
    float closest = 1e30f;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            if (across == 0 && down == 0) continue;
            const int c = target.column() + across, r = target.row() + down;
            if (!tables_->grid.open(c, r, content::kWallCharacter)) continue;
            if (c == walker.column() && r == walker.row()) continue;
            // Standing here has to be close enough on the same measure the arrival will be
            // judged by -- the centre of this tile against where the target actually is. Without
            // this the approach and the arrival are in different spaces and can disagree
            // forever, which is a deadlock rather than a wobble.
            if (std::max(std::fabs(float(c) - target.x), std::fabs(float(r) - target.y)) >
                float(radius)) {
                continue;
            }
            const float toX = float(c) - walker.x, toY = float(r) - walker.y;
            const float far = toX * toX + toY * toY;
            if (!found || far < closest) {
                found = true;
                closest = far;
                *column = c;
                *row = r;
            }
        }
    }
    return found;
}

void Realm::think(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];

    // The quarry it has, while it is worth having; else the nearest that is. A target learned
    // by being hit is not measured against eyesight at all -- what bounds that chase is the
    // grudge, which is asked first.
    //
    // Held before the choosing, so a beast that has just lost a quarry TO DEATH can be told
    // apart from one that never had a quarry at all. That difference is the whole of the stand
    // below.
    const uint32_t had = beast.quarry;
    uint32_t chosen = 0;
    if (strayed(beast) <= (beast.provoked ? kGrudge : kLeash)) {
        if (const Body* held = find(beast.quarry)) {
            const bool keep = beast.provoked
                                  ? held->alive() && !tables_->grid.safe(held->column(), held->row())
                                  : worth(beast, *held, kind.viewRange);
            if (keep) chosen = held->id;
        }
        if (chosen == 0) {
            float closest = 1e30f;
            for (uint32_t who : players_) {
                const Body& one = bodies_[who];
                if (!worth(beast, one, kind.viewRange)) continue;
                const float distance = reach(beast, one);
                if (distance < closest) {
                    closest = distance;
                    chosen = one.id;
                }
            }
        }
    }
    beast.quarry = chosen;

    if (chosen == 0) {
        // Lost, so the grudge goes with it: provoked qualifies a quarry and means nothing
        // without one.
        beast.provoked = false;

        // **And if it lost that quarry by killing it, it stands over the body for a beat.**
        // invention, and the reason is on kStandOverTicks: the drawing does not put a body down
        // until the blow that killed it has been SEEN landing, so a killer that turns away on
        // the tick walks off while its victim is still standing.
        //
        // Armed once, on the tick the quarry is first found dead -- `had` is 0 on every tick
        // after that, because the line above has already cleared it -- and asked before the
        // leash, so a beast lured far from its nest still looks at what it did before it starts
        // the walk home.
        if (had != 0) {
            const Body* was = find(had);
            if (was != nullptr && !was->alive()) {
                beast.standsUntil = tick_ + kStandOverTicks;
                halt(beast);
            }
        }
        if (tick_ < beast.standsUntil) {
            beast.temper = Temper::Wandering;
            return;
        }

        if (strayed(beast) > kLeash) {
            beast.temper = Temper::Homing;
            retreat(beast);
            return;
        }
        beast.temper = Temper::Wandering;
        if (!beast.walking && tick_ >= beast.thinksAt) {
            beast.thinksAt = tick_ + kind.attackTicks;
            wander(beast);
        }
        return;
    }

    const Body& quarry = *find(chosen);
    // In reach: stop, face it, and swing when the swing comes off its own clock. A monster
    // standing in a safe zone cannot attack out of it, which is the same tile bit that stops
    // it being attacked there.
    if (within(beast, quarry, float(kind.attackRange)) &&
        !tables_->grid.safe(beast.column(), beast.row())) {
        beast.temper = Temper::Fighting;
        engage(beast, quarry);
        if (tick_ >= beast.swingsAt) {
            // The swing clock is the beast's own and is NOT the thinking clock. They were one
            // field in MU2 once, so making a monster think oftener made it hit oftener, which
            // is a fight the player loses for reasons nothing on screen explains. It is also
            // where a per-skill cooldown will go.
            beast.swingsAt = tick_ + kind.attackTicks;
            strikeAt(beast, *body(chosen));
        }
        return;
    }

    beast.temper = Temper::Chasing;
    const bool chases = beast.provoked || within(beast, quarry, float(kind.viewRange + 1));
    if (chases && tick_ >= beast.repathsAt) {
        beast.repathsAt = tick_ + kRepath;
        // Only when it has stopped or its quarry has moved a tile. Re-planning on the clock
        // alone is not harmless: the line is pulled tight from wherever the walker is standing,
        // so the same order given three times a second yields a slightly different line each
        // time and the animal picks its way over in a zig-zag.
        if (drifted(beast, quarry)) {
            beast.chaseX = quarry.x;
            beast.chaseY = quarry.y;
            int column = 0, row = 0;
            if (beside(quarry, std::max(1, kind.attackRange), beast, &column, &row) &&
                send(beast, column, row)) {
                return;
            }
        } else {
            return;
        }
    }

    if (!beast.walking && tick_ >= beast.thinksAt) {
        beast.thinksAt = tick_ + kind.attackTicks;
        wander(beast);
    }
}

// ---- the fight -------------------------------------------------------------------------

}  // namespace mu::sim
