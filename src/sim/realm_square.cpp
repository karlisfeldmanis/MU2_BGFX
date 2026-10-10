// Devil Square's run: the clock, the waves and the points (sim/square.h, docs/devil-square-port.md
// "The goal and how it is shown"). The monsters and their waves are the doc's step 3; until then
// the clock runs and the points stand at nought.

#include "sim/realm.h"

#include <algorithm>

namespace mu::sim {

void Realm::squareTick() {
    if (square_.phase == SquarePhase::None) return;
    if (square_.phase == SquarePhase::Waiting && tick_ >= square_.startsAt) {
        // "The [Devil Square] event has begun" (lMsg 707): the fight starts.
        square_.phase = SquarePhase::Running;
        square_.endsAt = tick_ + int64_t(kSquarePlay) * kSquareTicksPerSecond;
    } else if (square_.phase == SquarePhase::Running && tick_ >= square_.endsAt) {
        // The time is up: the ranks and the result, then a minute's rest and home to Noria.
        square_.phase = SquarePhase::Ended;
        square_.leavesAt = tick_ + int64_t(kSquareRest) * kSquareTicksPerSecond;
    }
    if (square_.leavesAt >= 0 && !square_.sentOut && tick_ >= square_.leavesAt) {
        square_.sentOut = true;
    }
}

void Realm::squareKill(const Body& dead, const Body& scorer) {
    if (square_.phase != SquarePhase::Running || !dead.monster() || !scorer.player) return;
    const int at = playerOf(scorer);
    if (at < 0) return;
    // WebZen's: his share of the monster's health x its level x the square's number, to the last
    // hitter (DevilSquare.cpp:2219-2236). Ours until damage is kept per player: the whole share,
    // the level x the square.
    heroes_[size_t(at)].squarePoints += int64_t(std::max(1, dead.level)) * square_.square;
}

void Realm::jumpSquare(int seconds) {
    if (square_.phase == SquarePhase::None) return;
    const int at = std::clamp(seconds, 0, kSquarePlay);
    square_.phase = SquarePhase::Running;
    square_.startsAt = tick_;
    square_.endsAt = tick_ + int64_t(kSquarePlay - at) * kSquareTicksPerSecond;
}

int Realm::squareSecondsLeft() const {
    const int64_t until = square_.phase == SquarePhase::Waiting   ? square_.startsAt
                          : square_.phase == SquarePhase::Running ? square_.endsAt
                          : square_.leavesAt >= 0                 ? square_.leavesAt
                                                                  : tick_;
    return int(std::max<int64_t>(0, until - tick_ + kSquareTicksPerSecond - 1) / kSquareTicksPerSecond);
}

int Realm::squareWave() const {
    if (square_.phase != SquarePhase::Running) return 0;
    return squareWaveAt(int((tick_ - (square_.endsAt - int64_t(kSquarePlay) * kSquareTicksPerSecond)) /
                            kSquareTicksPerSecond));
}

int Realm::squareRank(int* of) const {
    // By points, ties to the lower level as WebZen breaks them (DevilSquareGround.cpp:32-49), then
    // to the body's id, so every mirror agrees.
    const Player& mine = me();
    const Body& self = bodies_[mine.body];
    int rank = 1, count = 0;
    for (const Player& one : heroes_) {
        const Body& body = bodies_[one.body];
        if (body.gone) continue;
        ++count;
        if (&one == &mine) continue;
        const bool ahead = one.squarePoints != mine.squarePoints ? one.squarePoints > mine.squarePoints
                           : body.level != self.level            ? body.level < self.level
                                                                 : body.id < self.id;
        rank += ahead ? 1 : 0;
    }
    if (of != nullptr) *of = std::max(1, count);
    return rank;
}

}  // namespace mu::sim
