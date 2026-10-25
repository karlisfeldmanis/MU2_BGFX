// Getting there, and deciding to: a body sent, halted, turned and advanced along its route,
// and a monster roused, wandering, retreating, engaging and thinking.
//
// Every number these turn on is in sim/realm_tuning.h with its source named. The one worth
// repeating here is that a walk is a line whose length takes the time -- A*'s 5-straight
// 7-diagonal is a SEARCH cost and counting it twice makes diagonals 41%% slow
// (docs/reference/conventions.md, "Time").
#include "sim/realm.h"
#include "sim/fmath.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

#include "core/log.h"
#include "sim/event.h"
#include "sim/quests.h"
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
        route(one).clear();
        route(one).push_back(Step{int16_t(column), int16_t(row)});
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
    route(one).assign(scratch_.begin(), scratch_.end());
    one.onStep = 0;
    one.walking = true;
    // On his feet before the first step, and said in that order: a drawing told of the walk
    // first would start the walk clip and then be told to stand up out of it. MU2's Realm.Move.
    rise(one);
    // The goal the route actually ends on, not the one asked for: the router moves a goal in a
    // wall to the nearest open tile, and the marker is put down from this event.
    say(What::Walked, one, route(one).back().column, route(one).back().row, tiles);
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
    route(one).clear();
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
    if (one.sleepUntil > tick_) return;   // a Sleep rune's: held as frozen is (kSleep...)
    // A body that is standing still still comes round: a fighter between two blows turns onto
    // what it is hitting, and a walk that has just been given spends its first tick or two
    // turning before any ground is covered.
    // Whether he is in a fight, standing or walking (kCombatTicks), and so whether a walk is a
    // run: out of one and off a safe tile, every class alike.
    // A chase counts only when it is close (kCombatReach): the field is full of things that
    // have seen him, and counting every one of them held him in the walk from the town gate on.
    if (one.player) {
        For his(*this, one);
        const Body* foe = me().order.kind == Request::Kind::Attack ? find(me().order.target) : nullptr;
        const char* why = foe != nullptr && foe->alive() ? "his attack"
                          : one.blowAt != 0              ? "a blow in the air"
                          : one.castUntil > tick_ || one.channelSkill != 0 ? "a cast"
                                                                           : nullptr;
        for (size_t i = 1; why == nullptr && i < bodies_.size(); ++i) {
            const Body& other = bodies_[i];
            // `quarry` first: one cache line of the body's thirty, and nearly every one is out
            // there. monster() alone reads three (2026-10-13, netbot's profile).
            if (other.quarry != one.id || !other.monster() || !other.alive()) continue;
            if (other.temper == Temper::Fighting ||
                (other.temper == Temper::Chasing && within(one, other, kCombatReach))) {
                why = "a monster on him";
            }
        }
        const bool safe = tables_->grid.safe(one.column(), one.row());
        one.riding = one.pet.mount && !safe && rideMap(tables_->map);
        one.flying = !one.riding && !safe && !me().bag[kWings].empty() && me().bag[kWings].durability > 0;
        one.flyFast = one.flying && size_t(me().bag[kWings].item) < tables_->items.size() &&
                      tables_->items[size_t(me().bag[kWings].item)].group == 12 &&
                      tables_->items[size_t(me().bag[kWings].item)].number == kDragonNumber;
        if (why != nullptr) {
            // Only on coming into a fight: held, the hold reaches this tick (kCombatTicks is one),
            // and `<=` logged every fighter every tick, 300 lines a second on the server's box.
            if (one.combatUntil < tick_) core::logf("combat: tick %lld, %s", (long long)tick_, why);
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
    if (one.onStep < route(one).size()) {
        const Step& target = route(one)[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        if (dx * dx + dy * dy > 1e-6f) one.aim = fm::atan2(dy, dx);
    }
    if (turn(one)) return;  // still coming round: no ground this tick
    // A tile crossed is said when the body's own tile changes, not when it reaches a point of
    // its route: a pulled route's points are the ends of long legs, and the log's Stepped
    // still means one tile.
    const int wasColumn = one.column(), wasRow = one.row();
    // Iced, it covers half the ground a tick (`kChillFactor`); running or riding, more.
    float left = one.speed * (one.chilledUntil > tick_ ? kChillFactor : 1.0f) * strideFactor(one);
    while (left > 0.0f && one.onStep < route(one).size()) {
        const Step& target = route(one)[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1e-6f) {
            ++one.onStep;
            continue;
        }
        one.aim = fm::atan2(dy, dx);
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
    if (one.onStep >= route(one).size()) {
        one.walking = false;
        route(one).clear();
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
    // **Only the gate his walk is for** (2026-10-13, the user: "Sylwen for some reason is not
    // making dyno or wings"): Atlans's gate to Tarkan stands beside the box Tarkan's sends him
    // back into, and Tarkan's the same, so a walk from either landing toward the Death Beam
    // Knights crossed the other gate -- the strongest bots went Atlans, Tarkan, Atlans every two
    // seconds, 9,424 crossings in an evening. A route that ends in this gate, or none, goes
    // through; one that only passes over it walks on. Invention: MU's CheckGate takes any tile.
    // The goal asked counts too: the router moves a goal in a wall to the nearest open tile,
    // which may lie just outside the box.
    if (hero.walking && !route(hero).empty()) {
        const Step& end = route(hero).back();
        const int at = playerOf(hero);
        const Request& order = at >= 0 ? heroes_[size_t(at)].order : me().order;
        const bool asked = order.kind == Request::Kind::WalkTo &&
                           enterGateAt(tables_->map, order.column, order.row) == gate;
        // And a walk that ends just past it, every step left within a tile of its box (2026-10-16,
        // the user: "i was not able to use dungeon -> lorencia gates after i used fast travel"):
        // the Dungeon's way out, gate 3, is the top two of a stair whose last row, 249, is open
        // under it, and a click into the dark doorway asked that row -- or the wall below, which
        // the router moves to it -- so he crossed the gate and stood in the pocket. A walk over
        // a gate to somewhere else leaves its side and walks on, as above.
        bool past = true;
        for (size_t k = hero.onStep; k < route(hero).size() && past; ++k) {
            const Step& next = route(hero)[k];
            past = next.column >= gate->box.x1 - 1 && next.column <= gate->box.x2 + 1 &&
                   next.row >= gate->box.y1 - 1 && next.row <= gate->box.y2 + 1;
        }
        if (!asked && !past && enterGateAt(tables_->map, end.column, end.row) != gate) return false;
    }
    // A sealed gate refuses everyone: Barred with level 0 is the drawing's "sealed".
    if (gate->target < 0) {
        say(What::Barred, hero, gate->number, 0);
        return false;
    }
    if (hero.level < moveLevel(gate->level, hero.kin)) {
        say(What::Barred, hero, gate->number, moveLevel(gate->level, hero.kin));
        return false;
    }
    // And Icarus's: Barred with level -1 is the drawing's "wings or a Dinorant", -2 its "a
    // Uniria cannot fly" (2026-10-25, dong on Discord, on a Uniria without wings: "Why I can't
    // get in").
    if (gate->fly && !canFly(*tables_, me().bag)) {
        say(What::Barred, hero, gate->number, ridesUniria(*tables_, me().bag) ? -2 : -1);
        return false;
    }
    // And -3 a quest not yet handed in: the Relics' door asks The Road to the Relics (the user,
    // 2026-10-25: "After the far road").
    if (gate->quest) {
        const int quest = questIndexOf(gate->quest);
        if (quest >= 0 && me().quests[size_t(quest)].completions == 0) {
            say(What::Barred, hero, gate->number, -3);
            return false;
        }
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
    me().order = Request{};
    me().pending = Request{};
    me().wants = skill::kNone;
    me().trading = -1;
    me().banking = -1;
    closeMachine();
    me().gating = -1;
    me().event.angeling = -1;
    // A gate to a floor of this same map -- the Dungeon's stairs between its three floors, one
    // grid with three regions the router cannot cross -- is not a map change: he is put down
    // there now, as a Town Portal puts him down (realm_items.cpp), the monsters on him lose
    // him, and her summon is dismissed.
    if (out->map == tables_->map) {
        setHeroDown(column, row, out->dx, out->dy, gate->number);
        return true;
    }
    setDown(hero, hero.column(), hero.row());
    // Walked out through a gate: the way back by magic is given up. And her summon with it: a
    // map change dismisses it here as it does in the save (PlayMode::keep), where the server
    // carried it and raised it on the far side as a health bar with no body (the user,
    // 2026-10-08).
    me().wayBack = WayBack{};
    if (me().summonSlot >= 0) dismiss(bodies_[size_t(me().summonSlot)]);
    say(What::Gated, hero, gate->number, column, row);
    return true;
}

void Realm::setHeroDown(int column, int row, int dx, int dy, int gate) {
    Body& hero = mine();
    rise(hero);
    dropBlow(hero);
    me().order = Request{};
    me().pending = Request{};
    me().wants = skill::kNone;
    me().trading = -1;
    me().banking = -1;
    closeMachine();
    me().gating = -1;
    me().event.angeling = -1;
    int open = column, openRow = row;
    if (router_.nearestOpen(column, row, content::kWallCharacter, 8, &open, &openRow)) {
        column = open;
        row = openRow;
    }
    setDown(hero, column, row);
    hero.facing = fm::atan2(float(dy), float(dx));
    hero.turning = false;
    for (Body& one : bodies_) {
        if (one.player || one.quarry != hero.id) continue;
        one.quarry = 0;
        one.provoked = false;
    }
    if (me().summonSlot >= 0) dismiss(bodies_[size_t(me().summonSlot)]);
    say(What::Climbed, hero, gate, column, row);
}

// ---- the mind --------------------------------------------------------------------------

bool Realm::worth(const Body& beast, const Body& target, int range) const {
    return target.alive() && within(beast, target, float(range)) &&
           !tables_->grid.safe(target.column(), target.row());
}

void Realm::rouse(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    const float far = float(event() == EventKind::Castle
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
    one.aim = fm::atan2(target.y - one.y, target.x - one.x);
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
    if (beast.sleepUntil > tick_) return;   // asleep (a Sleep rune's): nor while it sleeps
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
                if (unseen_ && one.id == hero().id) continue;
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
            // Numbing's share more on a chill a hero with it laid (sim/talents.h).
            const int64_t chill = beast.chillSlow == 0.0f
                                      ? kChillSwingTicks
                                      : int64_t(float(kChillSwingTicks) * (1.0f + beast.chillSlow / 100.0f));
            beast.swingsAt = tick_ + kind.attackTicks + (beast.chilledUntil > tick_ ? chill : 0);
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
        if (me().bag[i].empty() || !invisibilityCloak(tables_->items[size_t(me().bag[i].item)])) continue;
        if (castle == 0 || me().bag[i].refinement == castle) return i;
    }
    return -1;
}

CastleRefusal Realm::castleRefusal(int castle) const {
    if (castle < 1 || castle > kCastles) return CastleRefusal::NotBuilt;
    // Ours, first: a castle not built yet says so whatever he holds.
    if (castle > kCastlesBuilt) return CastleRefusal::NotBuilt;
    if (cloakSlot(castle) < 0) return CastleRefusal::NoCloak;
    const int day = daySecond(wall_);
    if (!castleOpen_ && (day < 0 || config_.castle.entryLeft(day) == 0)) return CastleRefusal::NotYet;
    const Body& hero = mine();
    // The band's floor only, ours (the user, 2026-10-04: 'allow to go to lower level BC for all
    // chars, basically we need min lvl requirments'): WebZen's ceiling, TooHigh, is never said.
    if (hero.level < kCastleBands[castle - 1][0]) return CastleRefusal::TooLow;
    return CastleRefusal::None;
}

// Asked from the window, between ticks: checked now and passed at the next tick's start, inside
// it, so its Gated is among that tick's happenings for the mode to read -- said out here, it was
// cleared with the rest before anyone saw it, and he stood in Devias with the window shut.
bool Realm::enterCastle(int castle) {
    if (me().gating < 0 || !serving(me().gating) ||
        tables_->folk[size_t(me().gating)].number != kMessenger) {
        return false;
    }
    if (castleRefusal(castle) != CastleRefusal::None) return false;
    me().event.castleOwed = castle;
    me().gating = -1;
    me().event.angeling = -1;
    return true;
}

void Realm::passCastle(int castle) {
    Body& hero = mine();
    if (!hero.alive() || castleRefusal(castle) != CastleRefusal::None) return;
    const EnterGate* gate = enterGateNumbered(kCastleEnterGate);
    if (gate == nullptr) return;
    // "You have come to Blood Castle %d" (lMsg 1171): the cloak is spent as he goes.
    me().bag.lift(cloakSlot(castle));
    me().event.castlePassed = castle;
    passGate(hero, *gate);
}

void Realm::setCastle(int castle) { run_.set(*this, castle); }

// ---- Blood Castle's run (sim/event.h; the run itself is sim/castle_instance.{h,cpp}) -------

bool Realm::fixed(const Body& body) const {
    return body.monster() && body.kind >= 0 && size_t(body.kind) < tables_->kinds.size() &&
           castleStatue(tables_->kinds[size_t(body.kind)].number);
}

void Realm::freeCastle() { run_.freeRun(*this); }

void Realm::dropCastleBridge(int seconds) { run_.dropBridge(*this, seconds); }

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
    // The one this castle's statue held, and a plain one -- unrefined and not excellent, as the
    // statue gives it -- since the three drop as weapons (2026-10-10): a hero's own +9 Divine
    // Sword carried in is not what the Archangel takes back.
    const int32_t wanted = castleWeaponItem();
    for (int i = kWorn; i < kSlots; ++i) {
        const Held& held = me().bag[i];
        if (held.empty()) continue;
        const content::ItemRow& row = tables_->items[size_t(held.item)];
        if (!archangelWeapon(row)) continue;
        if (wanted >= 0 && held.item != wanted) continue;
        if (held.refinement != 0 || held.excellent != 0) continue;
        return i;
    }
    return -1;
}

AngelState Realm::angelState() const {
    if (run_.phase == CastlePhase::Won) return AngelState::Done;
    if (run_.phase != CastlePhase::Running) return AngelState::NotYet;
    return staffSlot() >= 0 ? AngelState::Ready : AngelState::NoStaff;
}

bool Realm::handInStaff() {
    if (me().event.angeling < 0 || !serving(me().event.angeling) || angelState() != AngelState::Ready) return false;
    me().event.staffOwed = true;
    return true;
}

bool Realm::claimCastle() {
    if (me().event.angeling < 0 || !serving(me().event.angeling) || run_.phase != CastlePhase::Won || run_.claimed ||
        mine().id != run_.winner) {
        return false;
    }
    me().event.claimOwed = true;
    return true;
}

int Realm::castleLiving() const {
    int living = 0;
    for (const Player& one : heroes_) {
        const Body& body = bodies_[one.body];
        living += !body.gone && body.alive() ? 1 : 0;
    }
    return living;
}

int Realm::castleSecondsLeft() const {
    const int64_t until = run_.phase == CastlePhase::Waiting   ? run_.startsAt
                          : run_.phase == CastlePhase::Running ? run_.endsAt
                          : run_.leavesAt >= 0                 ? run_.leavesAt
                                                               : tick_;
    return int(std::max<int64_t>(0, until - tick_ + kCastleTicksPerSecond - 1) / kCastleTicksPerSecond);
}

}  // namespace mu::sim
