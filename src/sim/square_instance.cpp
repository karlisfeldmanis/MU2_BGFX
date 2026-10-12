// Devil Square's run (sim/square_instance.h): the clock, the waves, the points, and the ranks and
// pay at the end (sim/square.h, docs/devil-square-port.md "The goal and how it is shown"). Moved
// out of realm_square.cpp unchanged but for how it reaches the realm (the Instance's I2).

#include "sim/square_instance.h"

#include "core/log.h"
#include "sim/realm.h"

#include <algorithm>
#include <limits>

namespace mu::sim {

void SquareInstance::tick(Realm& r) {
    if (phase == SquarePhase::None) return;
    if (phase == SquarePhase::Waiting && r.tick_ >= startsAt) {
        // "The [Devil Square] event has begun" (lMsg 707): the fight starts.
        phase = SquarePhase::Running;
        endsAt = r.tick_ + int64_t(kSquarePlay) * kSquareTicksPerSecond;
    } else if (phase == SquarePhase::Running && r.tick_ >= endsAt) {
        // The time is up: the ranks and the result, then a minute's rest and home to Noria. The
        // square's monsters go with it, without a fall, and none rises again (WebZen clears them
        // as it closes, DevilSquare.cpp:1160-1164).
        phase = SquarePhase::Ended;
        leavesAt = r.tick_ + int64_t(kSquareRest) * kSquareTicksPerSecond;
        rank(r);
        core::logf("square %d: time is up, %lld points, rank %d, %d bosses down, %lld experience "
                   "and %lld Zen owed",
                   square, (long long)r.me().squarePoints, r.me().squareRanked,
                   bossesKilled, (long long)r.me().squareExperience, (long long)r.me().squareZen);
        for (Body& one : r.bodies_) {
            if (!one.monster()) continue;
            one.risesAt = std::numeric_limits<int64_t>::max();
            if (!one.alive()) continue;
            one.health = 0;
            one.quarry = 0;
            r.halt(one);
            r.dropBlow(one);
            r.say(What::Dismissed, one);
        }
    }
    // The waves, each called up once as its minute comes, while it has time left -- its breeds
    // rising on tiles drawn from their nests as any respawn -- and the bosses at minute 15.
    if (phase == SquarePhase::Running) {
        const int64_t start = endsAt - int64_t(kSquarePlay) * kSquareTicksPerSecond;
        const int elapsed = int((r.tick_ - start) / kSquareTicksPerSecond);
        for (int w = 0; w <= kSquareWaves; ++w) {
            const uint8_t bit = uint8_t(1u << w);
            const bool boss = w == kSquareWaves;
            const int from = boss ? kSquareBossMinute * 60 : kSquareWaveMinutes[w][0] * 60;
            const int to = boss ? kSquarePlay : kSquareWaveMinutes[w][1] * 60;
            if ((called & bit) != 0 || elapsed < from) continue;
            called |= bit;
            if (elapsed >= to) continue;
            int rising = 0;
            for (Body& one : r.bodies_) {
                if (!one.monster() || one.alive() || !owns(r, one)) continue;
                if (squareWaveOf(square, r.tables_->kinds[size_t(one.kind)].number) == w + 1) {
                    one.risesAt = r.tick_;
                    ++rising;
                }
            }
            core::logf("square %d: %s at %d:%02d, %d rise", square,
                       boss ? "the bosses" : w == 0 ? "the first wave" : w == 1 ? "the second wave"
                                                                       : "the third wave",
                       elapsed / 60, elapsed % 60, rising);
        }
    }
    // Claim on the result: his row paid and home to Noria now, as Blood Castle's Complete is.
    if (phase == SquarePhase::Ended) {
        for (size_t p = 0; p < r.heroes_.size(); ++p) {
            if (!r.heroes_[p].squareClaimOwed) continue;
            r.heroes_[p].squareClaimOwed = false;
            Realm::For him(r, p);
            if (r.mine().gone || r.me().squarePaid) continue;
            pay(r);
            r.warpHome(r.mine());
            core::logf("square %d: rank %d claimed, home to Noria", square, r.me().squareRanked);
        }
    }
    // The rest over: whoever has not claimed is paid on the way out (WebZen pays everyone at the
    // close, DevilSquareGround.cpp:551-638), and everyone still here goes home to Noria (gate 27,
    // DevilSquare.cpp:960-979) -- each by a warp, which the mode and the server carry out.
    if (leavesAt >= 0 && !sentOut && r.tick_ >= leavesAt) {
        sentOut = true;
        for (size_t p = 0; p < r.heroes_.size(); ++p) {
            Realm::For him(r, p);
            if (r.mine().gone || r.me().squarePaid) continue;
            pay(r);
            r.warpHome(r.mine());
        }
        core::logf("square %d: the rest is over, home to Noria", square);
    }
}

void SquareInstance::rank(Realm& r) {
    const int s = std::clamp(square, 1, 4) - 1;
    for (size_t p = 0; p < r.heroes_.size(); ++p) {
        Realm::Player& one = r.heroes_[p];
        if (r.bodies_[one.body].gone) continue;
        int of = 0;
        one.squareRanked = rankOf(r, p, &of);
        const int row = squarePayRow(one.squareRanked, of);
        // One level at the square's middle for the first, the rest OpenMU's share of it.
        const int mid = kSquareMidLevel[s];
        const int64_t level = int64_t(neededExperience(mid + 1) - neededExperience(mid));
        // And never under OpenMU's own at the game's rate, which square 1's middle level is.
        one.squareExperience = std::max(level * kSquarePay[s][row][0] / kSquarePay[s][0][0],
                                        int64_t(double(kSquarePay[s][row][0]) * kExperienceRate));
        one.squareZen = kSquarePay[s][row][1];
    }
}

void SquareInstance::pay(Realm& r) {
    Realm::Player& one = r.me();
    if (one.squareRanked <= 0 || one.squarePaid) return;
    one.squarePaid = true;
    r.gain(r.mine(), int32_t(std::min<int64_t>(one.squareExperience, INT32_MAX)));
    one.money += one.squareZen;
}

bool SquareInstance::owns(const Realm& r, const Body& one) const {
    if (one.nest < 0 || size_t(one.nest) >= r.tables_->nests.size()) return false;
    const content::MonsterNest& nest = r.tables_->nests[size_t(one.nest)];
    return inSquareArena(square, (nest.x1 + nest.x2) / 2, (nest.y1 + nest.y2) / 2);
}

void SquareInstance::kill(Realm& r, const Body& dead, const Body& scorer) {
    if (phase != SquarePhase::Running || !dead.monster() || !scorer.player) return;
    const int at = r.playerOf(scorer);
    if (at < 0) return;
    // WebZen's: his share of the monster's health x its level x the square's number, to the last
    // hitter (DevilSquare.cpp:2219-2236). Ours until damage is kept per player: the whole share,
    // the level x the square.
    r.heroes_[size_t(at)].squarePoints += int64_t(std::max(1, dead.level)) * square;
    if (squareWaveOf(square, r.tables_->kinds[size_t(dead.kind)].number) == kSquareWaves + 1) {
        ++bossesKilled;
    }
}

int SquareInstance::rankOf(const Realm& r, size_t at, int* of) const {
    // By points, ties to the lower level as WebZen breaks them (DevilSquareGround.cpp:32-49), then
    // to the body's id, so every mirror agrees.
    const Realm::Player& mine = r.heroes_[at];
    const Body& self = r.bodies_[mine.body];
    int rank = 1, count = 0;
    for (const Realm::Player& one : r.heroes_) {
        const Body& body = r.bodies_[one.body];
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
