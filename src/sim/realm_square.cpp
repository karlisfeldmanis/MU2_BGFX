// Devil Square's run: the clock, the waves and the points (sim/square.h, docs/devil-square-port.md
// "The goal and how it is shown"). The monsters and their waves are the doc's step 3; until then
// the clock runs and the points stand at nought.

#include "sim/realm.h"

#include "core/log.h"

#include <algorithm>
#include <limits>

namespace mu::sim {

void Realm::squareTick() {
    if (square_.phase == SquarePhase::None) return;
    if (square_.phase == SquarePhase::Waiting && tick_ >= square_.startsAt) {
        // "The [Devil Square] event has begun" (lMsg 707): the fight starts.
        square_.phase = SquarePhase::Running;
        square_.endsAt = tick_ + int64_t(kSquarePlay) * kSquareTicksPerSecond;
    } else if (square_.phase == SquarePhase::Running && tick_ >= square_.endsAt) {
        // The time is up: the ranks and the result, then a minute's rest and home to Noria. The
        // square's monsters go with it, without a fall, and none rises again (WebZen clears them
        // as it closes, DevilSquare.cpp:1160-1164).
        square_.phase = SquarePhase::Ended;
        square_.leavesAt = tick_ + int64_t(kSquareRest) * kSquareTicksPerSecond;
        int of = 0;
        const int rank = squareRank(&of);
        core::logf("square %d: time is up, %lld points, rank %d of %d, %d bosses down",
                   square_.square, (long long)me().squarePoints, rank, of, square_.bossesKilled);
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
    }
    // The waves, each called up once as its minute comes, while it has time left -- its breeds
    // rising on tiles drawn from their nests as any respawn -- and the bosses at minute 15.
    if (square_.phase == SquarePhase::Running) {
        const int64_t start = square_.endsAt - int64_t(kSquarePlay) * kSquareTicksPerSecond;
        const int elapsed = int((tick_ - start) / kSquareTicksPerSecond);
        for (int w = 0; w <= kSquareWaves; ++w) {
            const uint8_t bit = uint8_t(1u << w);
            const bool boss = w == kSquareWaves;
            const int from = boss ? kSquareBossMinute * 60 : kSquareWaveMinutes[w][0] * 60;
            const int to = boss ? kSquarePlay : kSquareWaveMinutes[w][1] * 60;
            if ((square_.called & bit) != 0 || elapsed < from) continue;
            square_.called |= bit;
            if (elapsed >= to) continue;
            int rising = 0;
            for (Body& one : bodies_) {
                if (!one.monster() || one.alive()) continue;
                if (squareWaveOf(square_.square, tables_->kinds[size_t(one.kind)].number) == w + 1) {
                    one.risesAt = tick_;
                    ++rising;
                }
            }
            core::logf("square %d: %s at %d:%02d, %d rise", square_.square,
                       boss ? "the bosses" : w == 0 ? "the first wave" : w == 1 ? "the second wave"
                                                                       : "the third wave",
                       elapsed / 60, elapsed % 60, rising);
        }
    }
    if (square_.leavesAt >= 0 && !square_.sentOut && tick_ >= square_.leavesAt) {
        square_.sentOut = true;
        core::logf("square %d: the rest is over, home to Noria", square_.square);
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
    if (squareWaveOf(square_.square, tables_->kinds[size_t(dead.kind)].number) == kSquareWaves + 1) {
        ++square_.bossesKilled;
    }
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
