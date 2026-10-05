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
#include <ctime>
#include <limits>

#include "core/log.h"
#include "sim/event.h"
#include "sim/gates.h"
#include "sim/items.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

float strideFactor(const Body& one) {
    return one.riding    ? kRideFactor
           : one.flying  ? (one.flyFast ? kFastFlyFactor : kFlyFactor)
           : one.running ? kRunFactor
                         : 1.0f;
}

bool Realm::send(Body& one, int column, int row, bool byRoad) {
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
        rise(one);
        say(What::Walked, one, column, row, 1);
        return true;
    }
    if (!router_.plan(one.column(), one.row(), column, row, wallOf(one), scratch_, byRoad)) {
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
    router_.pull(one.x, one.y, wallOf(one), scratch_, byRoad);
    one.route.assign(scratch_.begin(), scratch_.end());
    one.onStep = 0;
    one.walking = true;
    // On his feet before the first step, and said in that order: a drawing told of the walk
    // first would start the walk clip and then be told to stand up out of it. MU2's Realm.Move.
    rise(one);
    // The goal the route actually ends on, not the one asked for: the router moves a goal in a
    // wall to the nearest open tile, and the marker is put down from this event.
    say(What::Walked, one, one.route.back().column, one.route.back().row, tiles);
    return true;
}

// A monster has come to a stand, however it got there: the rest before it may wander again is
// counted from HERE and not from where the walk began.
//
// The clock was started by the wander that set the walk going -- `thinksAt = tick + attackTicks`
// at the moment of the order -- and a leg of three tiles takes 24 ticks where a Bull Fighter's
// attackTicks is 32, so the rest had all but run out before the animal arrived. It stood for one
// tick and set off again, for as long as it was awake. Two things came of that: a monster never
// stands still, which is not what MU looks like, and the drawing crossfades to the idle and back
// inside 100 ms at every leg -- 143 such blips in 300 seconds, measured by the headless run's
// clip tally, and what they look like is a walk that catches and stutters.
//
// OpenMU has the rest this restores: `BasicMonsterIntelligence` walks ONE tile and waits its
// move delay before the next, so the wait always falls between two steps and never inside one.
// This keeps the multi-tile leg (a route is what this engine's walker takes) and puts the whole
// wait after it.
void Realm::settle(Body& one) {
    if (one.player || one.kind < 0) return;
    const content::MonsterKind& kind = tables_->kinds[size_t(one.kind)];
    one.thinksAt = std::max(one.thinksAt, tick_ + std::max(1, kind.attackTicks));
}

void Realm::halt(Body& one) {
    if (!one.walking) return;
    one.walking = false;
    one.route.clear();
    one.onStep = 0;
    settle(one);
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
    if (one.frozenUntil > tick_) return;  // frozen: not a step, not a turn
    // A body that is standing still still comes round: a fighter between two blows turns onto
    // what it is hitting, and a walk that has just been given spends its first tick or two
    // turning before any ground is covered.
    // Whether he is in a fight, standing or walking (kCombatTicks), and so whether a walk is a
    // run: out of one and off a safe tile, every class alike.
    // A chase counts only when it is close (kCombatReach): the field is full of things that
    // have seen him, and counting every one of them held him in the walk from the town gate on.
    if (one.player) {
        const Body* foe = order_.kind == Request::Kind::Attack ? find(order_.target) : nullptr;
        const char* why = foe != nullptr && foe->alive() ? "his attack"
                          : one.blowAt != 0              ? "a blow in the air"
                          : one.castUntil > tick_ || one.channelSkill != 0 ? "a cast"
                                                                           : nullptr;
        for (size_t i = 1; why == nullptr && i < bodies_.size(); ++i) {
            const Body& other = bodies_[i];
            if (!other.monster() || !other.alive() || other.quarry != one.id) continue;
            if (other.temper == Temper::Fighting ||
                (other.temper == Temper::Chasing && within(one, other, kCombatReach))) {
                why = "a monster on him";
            }
        }
        const bool safe = tables_->grid.safe(one.column(), one.row());
        one.riding = one.pet.mount && !safe && rideMap(tables_->map);
        one.flying = !one.riding && !safe && !bag_[kWings].empty() && bag_[kWings].durability > 0;
        one.flyFast = one.flying && size_t(bag_[kWings].item) < tables_->items.size() &&
                      tables_->items[size_t(bag_[kWings].item)].group == 12 &&
                      tables_->items[size_t(bag_[kWings].item)].number == kDragonNumber;
        if (why != nullptr) {
            if (one.combatUntil <= tick_) core::logf("combat: tick %lld, %s", (long long)tick_, why);
            one.combatUntil = tick_ + (one.riding ? kRideCombatTicks : kCombatTicks);
        } else if (one.riding) {
            one.combatUntil = std::min(one.combatUntil, tick_ + kRideCombatTicks);
        }
        one.running = one.walking && one.combatUntil <= tick_ && !safe;
    }
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
    // Iced, it covers half the ground a tick (`kChillFactor`); running or riding, more.
    float left = one.speed * (one.chilledUntil > tick_ ? kChillFactor : 1.0f) * strideFactor(one);
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
        if (one.player && throughGate(one)) return;
    }
    if (one.onStep >= one.route.size()) {
        one.walking = false;
        one.route.clear();
        one.onStep = 0;
        settle(one);
        say(What::Halted, one);
    }
}

// ---- the gates -------------------------------------------------------------------------
//
// MuMain's CheckGate runs on the hero's tile every frame and fires the moment it lies inside an
// enter gate of the map he is on; the realm asks the same of the tile a step has just put him
// on, which is the only time the answer can change. The level test is the client's (MU shows
// the refusal itself and sends nothing) and the server's again in WarpGateAction; the landing
// is Player.WarpToAsync's, a random tile in the target gate's box. The next map is found
// standable by the realm that raises him there, which moves him to the nearest free tile.
//
// Invention: MuMain repeats the refusal every fifty frames he stands in the box (LoadingWorld =
// 50); here it is said once for each step into it.

bool Realm::throughGate(Body& hero) {
    const EnterGate* gate = enterGateAt(tables_->map, hero.column(), hero.row());
    if (gate == nullptr) return false;
    // A sealed gate refuses everyone: Barred with level 0 is the drawing's "sealed".
    if (gate->target < 0) {
        say(What::Barred, hero, gate->number, 0);
        return false;
    }
    if (hero.level < gate->level) {
        say(What::Barred, hero, gate->number, gate->level);
        return false;
    }
    return passGate(hero, *gate);
}

bool Realm::passGate(Body& hero, const EnterGate& through) {
    const EnterGate* gate = &through;
    const ExitGate* out = exitGate(gate->target);
    if (out == nullptr) return false;
    int column = dice_.nextInt(out->box.x1, out->box.x2 + 1);
    int row = dice_.nextInt(out->box.y1, out->box.y2 + 1);
    // Ours: never onto a tile of an enter gate. The Dungeon's exit 6 shares two tiles with enter
    // 7 beside it (Gates.cs:120, 188), and a landing there stood in the stair back up.
    for (int tries = 0; tries < 16 && enterGateAt(out->map, column, row) != nullptr; ++tries) {
        column = dice_.nextInt(out->box.x1, out->box.x2 + 1);
        row = dice_.nextInt(out->box.y1, out->box.y2 + 1);
    }
    // What a map change takes on MuMain's side (CheckGate's success branch): nothing selected,
    // no attack standing, and he stops where he is. Nothing that follows in this world is his.
    rise(hero);
    dropBlow(hero);
    order_ = Request{};
    pending_ = Request{};
    wants_ = skill::kNone;
    trading_ = -1;
    banking_ = -1;
    closeMachine();
    gating_ = -1;
    angeling_ = -1;
    // A gate to a floor of this same map -- the Dungeon's stairs between its three floors, one
    // grid with three regions the router cannot cross -- is not a map change: he is put down
    // there now, as a Town Portal puts him down (realm_items.cpp), the monsters on him lose
    // him, and her summon is dismissed.
    if (out->map == tables_->map) {
        setHeroDown(column, row, out->dx, out->dy, gate->number);
        return true;
    }
    setDown(hero, hero.column(), hero.row());
    say(What::Gated, hero, gate->number, column, row);
    return true;
}

void Realm::setHeroDown(int column, int row, int dx, int dy, int gate) {
    Body& hero = bodies_[0];
    rise(hero);
    dropBlow(hero);
    order_ = Request{};
    pending_ = Request{};
    wants_ = skill::kNone;
    trading_ = -1;
    banking_ = -1;
    closeMachine();
    gating_ = -1;
    angeling_ = -1;
    int open = column, openRow = row;
    if (router_.nearestOpen(column, row, content::kWallCharacter, 8, &open, &openRow)) {
        column = open;
        row = openRow;
    }
    setDown(hero, column, row);
    hero.facing = std::atan2(float(dy), float(dx));
    hero.turning = false;
    for (Body& one : bodies_) {
        if (one.player || one.quarry != hero.id) continue;
        one.quarry = 0;
        one.provoked = false;
    }
    if (summonSlot_ >= 0) dismiss(bodies_[size_t(summonSlot_)]);
    say(What::Climbed, hero, gate, column, row);
}

// ---- the mind --------------------------------------------------------------------------

bool Realm::worth(const Body& beast, const Body& target, int range) const {
    return target.alive() && within(beast, target, float(range)) &&
           !tables_->grid.safe(target.column(), target.row());
}

void Realm::rouse(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    const float far = float(tables_->map == kBloodCastleMap
                                ? std::max(kind.viewRange + kMargin, kWakeAtLeast)
                                : kind.viewRange + kMargin);
    float nearest = 1e30f;
    for (uint32_t who : players_) {
        // Alive or not, and the "or not" is the point: being roused is about whether anybody
        // can SEE the monster, and a dead character is still standing there. Whether there is
        // anything worth attacking is a separate question and worth() does check.
        nearest = std::min(nearest, reach(beast, bodies_[who]));
    }
    // Hysteresis, and not one distance: waking at `far` and sleeping at `far + slack` means the
    // two edges are two tiles apart, so a character standing on the boundary cannot toggle a
    // monster between the two states as he shuffles. Without it, measured over 6 000 ticks, 19
    // of the 155 wakings undid a sleep less than half a second old -- and a sleep halts the
    // animal mid-stride, so each one was a visible stop and start.
    const bool was = beast.temper != Temper::Asleep;
    const float edge = far + (was ? float(kSleepSlack) : 0.0f);
    const bool roused = nearest <= edge || (beast.provoked && beast.quarry != 0);
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
    // Only to a tile within its nest's reach: gObjMonsterMoveCheck refuses the rest
    // (gObjMonster.cpp:546-555), so one led far off stands where it was left.
    const bool near = std::max(std::abs(column - beast.homeColumn),
                               std::abs(row - beast.homeRow)) <= kWanderReach;
    if (near && tables_->grid.open(column, row, wallOf(beast))) send(beast, column, row);
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

bool Realm::seen(const Body& from, const Body& to) const { return seen(from.x, from.y, to); }

bool Realm::seen(float fromX, float fromY, const Body& to) const {
    if (std::fabs(to.x - fromX) <= kArmsLength && std::fabs(to.y - fromY) <= kArmsLength) {
        return true;
    }
    return router_.sees(fromX, fromY, to.x, to.y, content::kWallNoMove);
}

// The nearest tile beside a target that a walker can both stand on and finish its approach
// from. Nearest rather than OpenMU's random one: the randomness is there to stop a pack
// converging on one tile, and nearest-to-the-walker keeps that spread by construction while
// being STABLE -- asked again a tick later from almost the same place it gives the same
// answer, so a chase that re-plans three times a second does not zig-zag.
bool Realm::beside(const Body& target, int radius, const Body& walker, int* column, int* row,
                   bool sight, bool disc) {
    bool found = false;
    float closest = 1e30f;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            if (across == 0 && down == 0) continue;
            const int c = target.column() + across, r = target.row() + down;
            if (!tables_->grid.open(c, r, wallOf(walker))) continue;
            if (c == walker.column() && r == walker.row()) continue;
            // A shot from here must not cross a wall: the hero's ranged approach (`seen`).
            if (sight && !seen(float(c), float(r), target)) continue;
            // Standing here has to be close enough on the same measure the arrival will be
            // judged by -- the centre of this tile against where the target actually is. Without
            // this the approach and the arrival are in different spaces and can disagree
            // forever, which is a deadlock rather than a wobble.
            if (std::max(std::fabs(float(c) - target.x), std::fabs(float(r) - target.y)) >
                float(radius)) {
                continue;
            }
            // And on the monster's second measure too, WebZen's disc on the tiles (`apart`), which
            // think() asks before it will swing. Without it a ranged beast walked to a corner of
            // the square -- (4, 4) or (4, 3) for the Ice Queen's reach of 4, a disc distance of 5
            // -- found itself out of reach, and planned again to the nearest OTHER tile: it hopped
            // from tile to tile round him and never shot (the user, 2026-10-04: 'ice quenn ... she
            // constanly changes positions').
            if (disc && int(std::sqrt(double(across * across + down * down))) > radius) continue;
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
    if (beast.frozenUntil > tick_) return;  // frozen: no thought and no swing until it thaws
    if (beast.wakesAt > tick_) return;      // risen, and standing its five seconds
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
    // WebZen's chase (the user, 2026-09-30, off gObjMonsterProcess). What ends a chase it was provoked into is the quarry leaving its view
    // box of fifteen tiles (gObjMonster.cpp:620-621, user.cpp:20572); an unprovoked one still
    // lets go at its eyesight. And the leash over all of it (kLeash, the user's of 2026-10-01).
    const bool homing = beast.temper == Temper::Homing && !beast.provoked &&
                        strayed(beast) > kHomeAgain;
    if (!homing && strayed(beast) <= (beast.provoked ? kGrudge : kLeash)) {
        if (const Body* held = find(beast.quarry)) {
            const bool keep = beast.provoked
                                  ? held->alive() &&
                                        !tables_->grid.safe(held->column(), held->row()) &&
                                        within(beast, *held, float(kLoseSight)) &&
                                        (tick_ < beast.provokedUntil ||
                                         worth(beast, *held, kind.viewRange))
                                  : worth(beast, *held, kind.viewRange);
            if (keep) chosen = held->id;
        }
        if (chosen == 0) {
            float closest = 1e30f;
            for (uint32_t who : players_) {
                const Body& one = bodies_[who];
                if (!worth(beast, one, kind.viewRange) || apart(beast, one) >= kind.viewRange) {
                    continue;
                }
                // And it must see him: a wall between them hides him (the user, 2026-10-01: "dont
                // take agro from monsters which are behind the walls"). Only the noticing asks
                // it; a quarry it already has, or one that struck it, is chased round the wall.
                // ours.
                if (!router_.sees(beast.x, beast.y, one.x, one.y, content::kWallNoMove)) continue;
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

        // Past the leash, or still on the way back from it: home.
        if (strayed(beast) > (beast.temper == Temper::Homing ? kHomeAgain : kLeash)) {
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
    // Both reaches: the larger axis on the bodies themselves, so a walker halts only where its
    // arm meets him and not half a tile short, and WebZen's disc on their tiles, which takes a
    // ranged beast's corners off (a reach of 4 no longer shoots from (4, 4)).
    // And nothing shot through a wall (`seen`; the user, 2026-10-04: 'dont allow monsters to shoot
    // arrows throught walls'): walled off, it chases to a tile it can see him from. A blow beside
    // him is always seen. Ours, as the hero's own rule is; 0.75 asks no wall of a monster's reach.
    if (within(beast, quarry, float(kind.attackRange)) && apart(beast, quarry) <= kind.attackRange &&
        !tables_->grid.safe(beast.column(), beast.row()) && seen(beast, quarry)) {
        beast.temper = Temper::Fighting;
        engage(beast, quarry);
        if (tick_ >= beast.swingsAt) {
            // The swing clock is the beast's own and is NOT the thinking clock. They were one
            // field in MU2 once, so making a monster think oftener made it hit oftener, which
            // is a fight the player loses for reasons nothing on screen explains. It is also
            // where a per-skill cooldown will go.
            beast.swingsAt =
                tick_ + kind.attackTicks + (beast.chilledUntil > tick_ ? kChillSwingTicks : 0);
            if (const SplitBlow* split = splitOf(beast)) {
                // The first part is the swing's blow; the rest follow (beamOn).
                strikeAt(beast, *body(chosen), 1.0f / float(split->parts));
                beast.beamsLeft = split->parts - 1;
                beast.beamAt = tick_ + split->after;
                beast.beamOn = chosen;
            } else {
                strikeAt(beast, *body(chosen));
            }
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
            if (beside(quarry, std::max(1, kind.attackRange), beast, &column, &row, true, true) &&
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

// WebZen NpcTalk.cpp:1655-1753 and CGRequestEnterBloodCastle (protocol.cpp:19629-~20030), in
// their order: a cloak, the entry open, the cloak's castle, his level in its band. MU's talk opens
// its castle window (NewUIBloodCastleEnter) and its button asks the server again; here the window
// is the quest window's page (QuestDialog::kGate) and Enter is enterCastle. The entry is the local
// wall clock's (sim/event.h); a realm never handed one -- headless -- keeps the door shut.
int Realm::cloakSlot(int castle) const {
    for (int i = kWorn; i < kSlots; ++i) {
        if (bag_[i].empty() || !invisibilityCloak(tables_->items[size_t(bag_[i].item)])) continue;
        if (castle == 0 || bag_[i].refinement == castle) return i;
    }
    return -1;
}

CastleRefusal Realm::castleRefusal(int castle) const {
    if (castle < 1 || castle > kCastles) return CastleRefusal::NotBuilt;
    // Ours, first: a castle not built yet says so whatever he holds.
    if (castle > kCastlesBuilt) return CastleRefusal::NotBuilt;
    if (cloakSlot(castle) < 0) return CastleRefusal::NoCloak;
    int day = -1;
    if (wall_ > 0) {
        const time_t at = time_t(wall_);
        struct tm local {};
        localtime_r(&at, &local);
        day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    }
    if (!castleOpen_ && (day < 0 || castleEntryLeft(day) == 0)) return CastleRefusal::NotYet;
    const Body& hero = bodies_[0];
    // The band's floor only, ours (the user, 2026-10-04: 'allow to go to lower level BC for all
    // chars, basically we need min lvl requirments'): WebZen's ceiling, TooHigh, is never said.
    if (hero.level < kCastleBands[castle - 1][0]) return CastleRefusal::TooLow;
    return CastleRefusal::None;
}

// Asked from the window, between ticks: checked now and passed at the next tick's start, inside
// it, so its Gated is among that tick's happenings for the mode to read -- said out here, it was
// cleared with the rest before anyone saw it, and he stood in Devias with the window shut.
bool Realm::enterCastle(int castle) {
    if (gating_ < 0 || !serving(gating_)) return false;
    if (castleRefusal(castle) != CastleRefusal::None) return false;
    castleOwed_ = castle;
    gating_ = -1;
    angeling_ = -1;
    return true;
}

void Realm::passCastle(int castle) {
    Body& hero = bodies_[0];
    if (!hero.alive() || castleRefusal(castle) != CastleRefusal::None) return;
    const EnterGate* gate = enterGateNumbered(kCastleEnterGate);
    if (gate == nullptr) return;
    // "You have come to Blood Castle %d" (lMsg 1171): the cloak is spent as he goes.
    bag_.lift(cloakSlot(castle));
    castlePassed_ = castle;
    passGate(hero, *gate);
}

void Realm::setCastle(int castle) {
    if (tables_ == nullptr || tables_->map != kBloodCastleMap) return;
    castle = std::clamp(castle, 1, kCastles);
    run_.castle = castle;
    const auto kindNumbered = [&](int32_t number) {
        for (size_t i = 0; i < tables_->kinds.size(); ++i) {
            if (tables_->kinds[i].number == number) return int32_t(i);
        }
        return int32_t(-1);
    };
    // One of the three statues, as WebZen raises one at random (BloodCastle.cpp:2216) -- of
    // those with a figure cooked, so none stands invisible.
    int32_t statues[3];
    int drawn = 0;
    for (int32_t number : kCastleStatues) {
        const int32_t kind = kindNumbered(number);
        if (kind >= 0 && !tables_->kinds[size_t(kind)].figure.empty()) statues[drawn++] = number;
    }
    const int32_t statue = drawn > 0 ? statues[dice_.nextInt(0, drawn)] : kCastleStatue;
    core::logf("castle: Blood Castle %d, the statue %d of %d standing", castle, statue, drawn);
    for (Body& one : bodies_) {
        if (!one.monster()) continue;
        const int32_t number = tables_->kinds[size_t(one.kind)].number;
        int32_t want = number;
        if (castleStatue(number)) {
            want = statue;
        } else {
            for (int slot = 0; slot < 6; ++slot) {
                if (kCastleBreeds[0][slot] == number) want = kCastleBreeds[castle - 1][slot];
            }
        }
        const int32_t kind = kindNumbered(want);
        if (kind < 0) continue;
        // As Realm::raise dresses a body in its kind.
        const content::MonsterKind& row = tables_->kinds[size_t(kind)];
        one.kind = kind;
        one.level = row.level;
        one.maxHealth = castleStatue(want) ? kCastleStatueHealth[castle - 1] : row.health;
        one.stats.level = row.level;
        one.stats.attackRate = row.attackRate;
        one.stats.defenseRate = row.defenseRate;
        one.stats.defense = row.defense;
        one.stats.minimumDamage = row.minimumDamage;
        one.stats.maximumDamage = row.maximumDamage;
        one.swingTicks = row.attackTicks;
        one.speed = 1.0f / float(std::max(1, row.moveTicks));
    }
}

// ---- Blood Castle's run (sim/event.h) ------------------------------------------------------

bool Realm::fixed(const Body& body) const {
    return body.monster() && body.kind >= 0 && size_t(body.kind) < tables_->kinds.size() &&
           castleStatue(tables_->kinds[size_t(body.kind)].number);
}

void Realm::freeCastle() {
    if (tables_ == nullptr || tables_->map != kBloodCastleMap) return;
    run_ = CastleRun{};
    for (const GridBox& box : {kCastleEntrance, kCastleBridge, kCastleDoor[0], kCastleDoor[1],
                               kCastleDoor[2]}) {
        changeGrid(box.x1, box.y1, box.x2, box.y2, box.bits, false);
    }
    // The garrison kept down for good, and the Statue of Saint stood in its hall at once, to be
    // looked at (the user, 2026-10-03: 'did not see the archachel statue').
    for (Body& one : bodies_) {
        if (!one.monster()) continue;
        if (castleStatue(tables_->kinds[size_t(one.kind)].number)) {
            if (!one.alive()) one.risesAt = tick_;
        } else {
            one.health = 0;
            one.risesAt = std::numeric_limits<int64_t>::max();
        }
    }
}

void Realm::castleTick() {
    // The staff given back, inside the tick so what it says is this tick's (as enterCastle).
    // "Ah! Great warrior..." (ServerCmd 1,23, NpcTalk.cpp:1461-1588), and GiveReward_Win.
    if (staffOwed_) {
        staffOwed_ = false;
        const int slot = staffSlot();
        if (slot >= 0 && run_.phase == CastlePhase::Running) {
            bag_.lift(slot);
            Body& hero = bodies_[0];
            const int64_t seconds = castleSecondsLeft();
            // This castle's pay (sim/event.h): castle 1's 20,000 / 5,000 / 160 a second / 20,000
            // Zen up to castle 6's 110,000 / 30,000 / 260 / 250,000.
            const int c = std::clamp(run_.castle, 1, kCastles) - 1;
            const int64_t experience =
                (run_.statueBroken ? kCastleStatueExps[c] : 0) + kCastleHandInExps[c] +
                seconds * kCastleExpPerSeconds[c];
            // At the game's experience rate, as every kill's (rules.h kExperienceRate), and the
            // better win's (kCastleExpTimes, kCastleZenTimes).
            run_.paidExperience = int64_t(double(experience * kCastleExpTimes) * kExperienceRate);
            run_.paidZen = kCastleWinZens[c] * kCastleZenTimes;
            run_.phase = CastlePhase::Won;
            // The runes' powers drawn now, for his page to show; the pay waits for Complete.
            for (int i = 0; i < kCastleRunes[c]; ++i) {
                run_.paidRunes[i] = drawRunePower(dice_, hero.kin, hero.second, kCastleRuneLevel[c]);
            }
            // And the castle is theirs again: every monster in it gone on the tick, out of the
            // picture without a fall, and none rises for the rest of the run (the user,
            // 2026-10-05: 'when quest is given all monsters have to desepear'). Ours.
            for (Body& one : bodies_) {
                if (!one.monster()) continue;
                one.risesAt = std::numeric_limits<int64_t>::max();
                if (!one.alive()) continue;
                one.health = 0;
                one.quarry = 0;
                halt(one);
                dropBlow(one);
                say(What::Dismissed, one);
            }
            hero.quarry = 0;
        }
    }
    // Complete on his thanks: the win paid, and out to Devias now rather than after the rest.
    if (claimOwed_) {
        claimOwed_ = false;
        if (run_.phase == CastlePhase::Won && !run_.claimed) {
            payCastle();
            angeling_ = -1;
            run_.leavesAt = tick_;
            run_.sentOut = true;
        }
    }
    if (run_.phase == CastlePhase::Waiting && tick_ >= run_.startsAt) {
        // "Blood Castle 1 quest has begun" (lMsg 1161): the barrier lifted, the clock started.
        run_.phase = CastlePhase::Running;
        run_.endsAt = tick_ + int64_t(kCastleRun) * kCastleTicksPerSecond;
        changeGrid(kCastleEntrance.x1, kCastleEntrance.y1, kCastleEntrance.x2, kCastleEntrance.y2,
                   kCastleEntrance.bits, false);
    } else if (run_.phase == CastlePhase::Running && tick_ >= run_.endsAt) {
        run_.phase = CastlePhase::Ended;
    }
    // The drawbridge down: its gap is ground as the door lands (kCastleBridgeTicks), and the
    // castle's door open behind it, the courtyard with it -- ours (the user, 2026-10-03: 'when
    // gates drop allopw to go inside'); WebZen opens the door when the Castle Gate dies.
    if (run_.bridgeAt >= 0 && !run_.bridgeDown && tick_ >= run_.bridgeAt + kCastleBridgeTicks) {
        run_.bridgeDown = true;
        for (const GridBox& box : {kCastleBridge, kCastleDoor[0], kCastleDoor[1], kCastleDoor[2]}) {
            changeGrid(box.x1, box.y1, box.x2, box.y2, box.bits, false);
        }
        // And quota 2's Spirit Sorcerers rise in the courtyard: WebZen's 2 for one player
        // (gObjMonster.cpp:1302-1308), raised there when the gate falls (SetBossMonster).
        int raised = 0;
        for (Body& one : bodies_) {
            if (raised >= kCastleSorcerers) break;
            if (!one.monster() || one.alive()) continue;
            if (!castleSorcerer(tables_->kinds[size_t(one.kind)].number)) continue;
            one.risesAt = tick_;
            ++raised;
        }
    }
    // The run over: a minute's rest, then out to Devias.
    if ((run_.phase == CastlePhase::Won || run_.phase == CastlePhase::Ended) && run_.leavesAt < 0) {
        run_.leavesAt = tick_ + int64_t(kCastleRest) * kCastleTicksPerSecond;
    }
    if (run_.leavesAt >= 0 && !run_.sentOut && tick_ >= run_.leavesAt) {
        // Won and never claimed: paid on the way out, so the win is not lost.
        if (run_.phase == CastlePhase::Won) payCastle();
        run_.sentOut = true;
    }
}

void Realm::payCastle() {
    if (run_.claimed) return;
    run_.claimed = true;
    Body& hero = bodies_[0];
    const int c = std::clamp(run_.castle, 1, kCastles) - 1;
    gain(hero, int32_t(std::min<int64_t>(run_.paidExperience, INT32_MAX)));
    money_ += run_.paidZen;
    // And the castle's jewels, one each (BloodCastle.dat "Reward Items"), into the bag as a
    // quest's pay goes (the user, 2026-10-04: 'when finish quest on BC put items on bag similiar
    // like receving quests'); WebZen lays them at his feet, and one the bag cannot hold still
    // falls there -- the win is not refused, as a quest's hand-in is.
    for (const auto& jewel : kCastleRewardJewels[c]) {
        if (jewel[0] < 0) break;
        const int32_t item = tables_->itemAt(jewel[0], jewel[1]);
        for (int i = 0; item >= 0 && i < jewel[2]; ++i) {
            if (give(item) < 0) lay(item);
        }
    }
    // And the castle's runes, each with the power drawn at the hand-in (kCastleRunes).
    const int32_t rune = tables_->itemAt(14, 22);
    for (int i = 0; rune >= 0 && i < kCastleRunes[c]; ++i) {
        const uint8_t powers[kMostSockets] = {run_.paidRunes[i]};
        if (give(rune, -1, 0, -1, false, 0, 0, 0, powers) >= 0) continue;
        const uint32_t id = lay(rune);
        for (Lying& one : lying_) {
            if (one.id == id) one.what.powers[0] = powers[0];
        }
    }
}

void Realm::castleKill(const Body& dead) {
    if (run_.phase != CastlePhase::Running) return;
    const int32_t number = tables_->kinds[size_t(dead.kind)].number;
    // The statue is the run's target, not one of its garrison: broken, it pays its bonus
    // (kCastleStatueExp) and counts toward neither quota.
    if (castleStatue(number)) {
        // Broken, it lets fall the weapon it held -- the staff, the sword or the crossbow, by
        // which of the three it was -- on the stone where it lay, to be carried back to the
        // Archangel (the user, 2026-10-03; docs/blood-castle-port.md 5). Ours: WebZen's statue
        // drops nothing by the drop table (gObjMonster.cpp:3985) and its weapon comes by another
        // way. It lies there until the run's time is up.
        run_.statueBroken = true;
        const int* weapon = kArchangelWeapons[std::clamp(number - kCastleStatues[0], 0, 2)];
        const int32_t staff = tables_->itemAt(weapon[0], weapon[1]);
        if (staff >= 0) {
            const uint32_t id = lay(staff, 0, false, 0, 0, 0);
            if (!lying_.empty() && lying_.back().id == id) {
                std::tie(lying_.back().column, lying_.back().row) =
                    clearing(dead.column(), dead.row());
                lying_.back().vanishesAt = run_.endsAt;
            }
        }
    } else if (castleSorcerer(number)) {
        // Quota 2 met: the Statue of Saint rises in its hall.
        if (++run_.sorcerers == kCastleSorcerers) {
            for (Body& one : bodies_) {
                if (one.monster() && !one.alive() &&
                    castleStatue(tables_->kinds[size_t(one.kind)].number)) {
                    one.risesAt = tick_;
                }
            }
        }
    } else {
        ++run_.kills;
    }
    // Quota 1: "monsters cleared! attack the castle gate" (lMsg 1168), and the drawbridge falls.
    if (run_.kills >= kCastleKills && run_.bridgeAt < 0) run_.bridgeAt = tick_;
}

void Realm::dropCastleBridge(int seconds) {
    if (tables_ == nullptr || tables_->map != kBloodCastleMap) return;
    if (run_.phase == CastlePhase::Waiting) run_.startsAt = tick_;
    run_.kills = std::max(run_.kills, kCastleKills);
    run_.bridgeAt = tick_ + int64_t(seconds) * kCastleTicksPerSecond;
}

int32_t Realm::castleWeaponItem() const {
    if (tables_ == nullptr) return -1;
    int which = 0;
    for (const Body& one : bodies_) {
        if (!one.monster()) continue;
        const int32_t number = tables_->kinds[size_t(one.kind)].number;
        if (castleStatue(number)) which = std::clamp(number - kCastleStatues[0], 0, 2);
    }
    return tables_->itemAt(kArchangelWeapons[which][0], kArchangelWeapons[which][1]);
}

int Realm::staffSlot() const {
    for (int i = kWorn; i < kSlots; ++i) {
        if (bag_[i].empty()) continue;
        const content::ItemRow& row = tables_->items[size_t(bag_[i].item)];
        if (archangelWeapon(row)) return i;
    }
    return -1;
}

AngelState Realm::angelState() const {
    if (run_.phase == CastlePhase::Won) return AngelState::Done;
    if (run_.phase != CastlePhase::Running) return AngelState::NotYet;
    return staffSlot() >= 0 ? AngelState::Ready : AngelState::NoStaff;
}

bool Realm::handInStaff() {
    if (angeling_ < 0 || !serving(angeling_) || angelState() != AngelState::Ready) return false;
    staffOwed_ = true;
    return true;
}

bool Realm::claimCastle() {
    if (angeling_ < 0 || !serving(angeling_) || run_.phase != CastlePhase::Won || run_.claimed) {
        return false;
    }
    claimOwed_ = true;
    return true;
}

int Realm::castleSecondsLeft() const {
    const int64_t until = run_.phase == CastlePhase::Waiting   ? run_.startsAt
                          : run_.phase == CastlePhase::Running ? run_.endsAt
                          : run_.leavesAt >= 0                 ? run_.leavesAt
                                                               : tick_;
    return int(std::max<int64_t>(0, until - tick_ + kCastleTicksPerSecond - 1) / kCastleTicksPerSecond);
}

}  // namespace mu::sim
