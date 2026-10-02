// A townsperson's rounds: Marlon walks from his spot to the tavern's bench and sits, looks in on
// two of the guards -- who turn to him and salute -- and goes home, and round again.
//
// All of it is ours (realm_tuning.h, kStrollers): MU's NPCs stand where the map puts them. The
// user's, 2026-09-29: "make Marlon do some walking to the bar and sit, check on guards and go
// back to his spot. when user interact he pauses, then he goes back to his route", and "guards
// has to do salute to Marlon".
//
// He is a body like a guard, `warden` naming his folk row, so the pointer, the talk, the name
// over his head and the figure all find him as they find a guard; only his turn is different
// (Realm::watch hands a body with no warden row here). He takes no dice: his rounds are a fixed
// loop, so a seeded run is moved by him only in which tiles are taken when.
#include "sim/realm.h"

#include <cmath>
#include <cstdlib>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

namespace {

// The realm's ticks a second (game/play_tuning.h's kTickSeconds is its other side).
constexpr int kTicksASecond = 20;

// A folk row's Look as the sim's facing: the arithmetic realm_watch.cpp stands a guard with.
float facingOf(int32_t look) {
    const int facing = look >= 1 && look <= 8 ? look : 3;
    const float bearing = float(((facing - 3) % 8 + 8) % 8) * (3.14159265359f / 4.0f);
    return std::atan2(-std::cos(bearing), std::sin(bearing));
}

}  // namespace

void Realm::raiseStrollers() {
    for (size_t i = 0; i < tables_->folk.size(); ++i) {
        const content::Townsperson& person = tables_->folk[i];
        const StrollRow* row = strollRow(person.number);
        if (row == nullptr) continue;
        int column = person.x, at = person.y;
        if (!router_.nearestOpen(person.x, person.y, content::kWallCharacter, 3, &column, &at)) {
            core::logError("%s has nowhere to stand near (%d, %d)", person.name.c_str(), person.x,
                           person.y);
            continue;
        }
        Body walker;
        walker.id = nextId_++;
        walker.warden = int32_t(i);
        walker.level = 1;
        // Alive, and never struck: nothing in the rules aims at a warden body.
        walker.maxHealth = walker.health = 1;
        walker.speed = 1.0f / float(kStrollTicks);
        walker.x = float(column);
        walker.y = float(at);
        walker.homeColumn = column;
        walker.homeRow = at;
        walker.post = walker.facing = walker.aim = facingOf(person.look);
        walker.temper = Temper::Wandering;
        walker.route.reserve(64);
        Stroller one;
        one.id = walker.id;
        one.row = row;
        strollers_.push_back(one);
        bodies_.push_back(std::move(walker));
        core::logf("realm: %s walks his rounds, %d stops", person.name.c_str(), row->count);
    }
}

bool Realm::folkTile(int folk, int* column, int* row) const {
    if (!tables_ || folk < 0 || size_t(folk) >= tables_->folk.size()) return false;
    for (size_t i = 1; i < bodies_.size(); ++i) {
        if (bodies_[i].warden == folk) {
            *column = bodies_[i].column();
            *row = bodies_[i].row();
            return true;
        }
    }
    *column = tables_->folk[size_t(folk)].x;
    *row = tables_->folk[size_t(folk)].y;
    return true;
}

void Realm::stroll(Body& walker) {
    Stroller* self = nullptr;
    for (Stroller& one : strollers_) {
        if (one.id == walker.id) self = &one;
    }
    if (self == nullptr) return;
    Stroller& s = *self;
    advance(walker);

    // Up from the bench, said so the drawing stands him up.
    const auto standUp = [&] {
        if (walker.pose == Pose::Standing) return;
        walker.pose = Pose::Standing;
        walker.perch = -1;
        s.perch = -1;
        say(What::Posed, walker, int32_t(Pose::Standing), -1);
    };

    // **The hero talking to him holds him.** From the click -- a Talk order on his row, which
    // walks the hero to where he stands NOW -- through the quest window, and a moment after:
    // he stops, gets up if he sat, and faces the hero. Then he takes up the stop he was on.
    const Body& hero = bodies_[0];
    const int folk = walker.warden;
    const bool talking =
        hero.alive() && (questing_ == folk || (order_.kind == Request::Kind::Talk &&
                                               int(order_.target) == folk));
    if (talking) {
        if (!s.held) {
            s.held = true;
            halt(walker);
            standUp();
            s.there = false;
        }
        walker.aim = std::atan2(hero.y - walker.y, hero.x - walker.x);
        s.freeAt = tick_ + kStrollResumeTicks;
        return;
    }
    if (s.held) {
        if (tick_ < s.freeAt) return;
        s.held = false;
    }

    const StrollStop& stop = s.row->stops[s.stop];
    const auto next = [&] {
        standUp();
        s.stop = (s.stop + 1) % s.row->count;
        s.there = false;
        s.chatLine = 0;
    };

    // Where this stop is: his own spot, the bench's tile, or beside the guard.
    int column = stop.column, row = stop.row;
    int perch = -1;
    Body* guard = nullptr;
    if (stop.kind == StopKind::Stand) {
        if (!router_.nearestOpen(stop.column, stop.row, content::kWallCharacter, 3, &column, &row)) {
            next();
            return;
        }
    } else if (stop.kind == StopKind::Sit) {
        for (size_t i = 0; i < tables_->perches.size(); ++i) {
            const content::Perch& one = tables_->perches[i];
            if (one.column == stop.column && one.row == stop.row && content::usable(tables_->grid, one)) {
                perch = int(i);
            }
        }
        if (perch < 0) {
            next();
            return;
        }
    } else {
        for (size_t i = 1; i < bodies_.size(); ++i) {
            Body& one = bodies_[i];
            if (one.warden < 0 || one.id == walker.id) continue;
            if (std::abs(one.homeColumn - stop.column) <= 2 && std::abs(one.homeRow - stop.row) <= 2) {
                guard = &one;
            }
        }
        if (guard == nullptr) {
            next();
            return;
        }
    }

    if (!s.there) {
        const bool byGuard = guard != nullptr &&
                            std::max(std::abs(walker.column() - guard->column()),
                                     std::abs(walker.row() - guard->row())) <= 1;
        const bool arrived =
            !walker.walking &&
            (guard != nullptr ? byGuard : walker.column() == column && walker.row() == row);
        if (!arrived) {
            if (!walker.walking && tick_ >= walker.repathsAt) {
                walker.repathsAt = tick_ + kRepath;
                if (guard != nullptr && !beside(*guard, 1, walker, &column, &row)) {
                    next();
                    return;
                }
                // No way there: on to the next stop rather than standing in the road for ever.
                // By the road, as a townsperson walks the town (the user's, 2026-09-29).
                if (!send(walker, column, row, true)) next();
            }
            return;
        }
        s.there = true;
        s.leaves = tick_ + int64_t(stop.seconds) * kTicksASecond;
        if (stop.kind == StopKind::Stand) {
            walker.aim = walker.post;
            // A stop with someone to talk to: turned to them where the table stands them, and
            // the talk starts a second later, as it does on the bench.
            if (stop.with != 0) {
                for (const content::Townsperson& one : tables_->folk) {
                    if (one.number != stop.with) continue;
                    walker.aim = std::atan2(float(one.y) - walker.y, float(one.x) - walker.x);
                    break;
                }
                s.chatAt = tick_ + kChatTicks / 3;
            }
        } else if (stop.kind == StopKind::Sit) {
            // Sat as the hero sits (Realm::perch): the bench's own facing, and said.
            const content::Perch& one = tables_->perches[size_t(perch)];
            if (one.turns) {
                walker.facing = walker.aim = one.aim;
                walker.turning = false;
            }
            walker.pose = Pose(one.pose);
            walker.perch = perch;
            s.perch = perch;
            say(What::Posed, walker, int32_t(walker.pose), perch);
            // The talk starts a second after he sits -- or goes on, after a pause, from the line
            // it had reached: `chatLine` is only reset when he leaves the stop.
            s.chatAt = tick_ + kChatTicks / 3;
        } else {
            walker.aim = std::atan2(guard->y - walker.y, guard->x - walker.x);
            // The guard, when he is at his post and not in a fight: turned to him, and the salute,
            // held as a pointing is held (Realm::watch returns while `standsUntil` runs).
            if (guard->quarry == 0 && !guard->walking) {
                halt(*guard);
                guard->aim = std::atan2(walker.y - guard->y, walker.x - guard->x);
                guard->standsUntil = tick_ + kSaluteTicks;
                say(What::Shouted, *guard, int32_t(Shout::Salute), -1, -1, walker.id);
            }
        }
        return;
    }
    // **His talk at the stop**, her line first and then his, turn about. The hero at her
    // counter -- or on his way to it -- pauses it: the time at the stop stands still with it, so
    // the talk is finished when he goes and not cut off.
    if (stop.with != 0) {
        int partner = -1;
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == stop.with) partner = int(i);
        }
        const bool busy = partner >= 0 && (trading_ == partner || banking_ == partner || mixing_ == partner ||
                                            gating_ == partner ||
                                           questing_ == partner ||
                                           (order_.kind == Request::Kind::Talk &&
                                            int(order_.target) == partner));
        if (busy) {
            ++s.leaves;
            ++s.chatAt;
            return;
        }
        if (s.chatLine < kChatLines && tick_ >= s.chatAt) {
            const int speaker = s.chatLine % 2 == 0 ? partner : -1;
            say(What::Shouted, walker, int32_t(Shout::Chat), s.chatLine, speaker, 0);
            ++s.chatLine;
            s.chatAt = tick_ + kChatTicks;
        }
    }
    if (tick_ < s.leaves) return;
    next();
}

}  // namespace mu::sim
