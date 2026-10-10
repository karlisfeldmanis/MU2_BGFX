// Devil Square's run: the clock, the waves, the points, and the ranks and pay at the end
// (sim/square.h, docs/devil-square-port.md "The goal and how it is shown").

#include "sim/realm.h"

#include "core/log.h"

#include <algorithm>
#include <ctime>
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
        rankSquare();
        core::logf("square %d: time is up, %lld points, rank %d, %d bosses down, %lld experience "
                   "and %lld Zen owed",
                   square_.square, (long long)me().squarePoints, me().squareRanked,
                   square_.bossesKilled, (long long)me().squareExperience, (long long)me().squareZen);
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
                if (!one.monster() || one.alive() || !ownSquare(one)) continue;
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
    // Claim on the result: his row paid and home to Noria now, as Blood Castle's Complete is.
    if (square_.phase == SquarePhase::Ended) {
        for (size_t p = 0; p < heroes_.size(); ++p) {
            if (!heroes_[p].squareClaimOwed) continue;
            heroes_[p].squareClaimOwed = false;
            For him(*this, p);
            if (mine().gone || me().squarePaid) continue;
            paySquare();
            warpHome(mine());
            core::logf("square %d: rank %d claimed, home to Noria", square_.square, me().squareRanked);
        }
    }
    // The rest over: whoever has not claimed is paid on the way out (WebZen pays everyone at the
    // close, DevilSquareGround.cpp:551-638), and everyone still here goes home to Noria (gate 27,
    // DevilSquare.cpp:960-979) -- each by a warp, which the mode and the server carry out.
    if (square_.leavesAt >= 0 && !square_.sentOut && tick_ >= square_.leavesAt) {
        square_.sentOut = true;
        for (size_t p = 0; p < heroes_.size(); ++p) {
            For him(*this, p);
            if (mine().gone || me().squarePaid) continue;
            paySquare();
            warpHome(mine());
        }
        core::logf("square %d: the rest is over, home to Noria", square_.square);
    }
}

void Realm::rankSquare() {
    const int s = std::clamp(square_.square, 1, 4) - 1;
    for (size_t p = 0; p < heroes_.size(); ++p) {
        Player& one = heroes_[p];
        if (bodies_[one.body].gone) continue;
        int of = 0;
        one.squareRanked = squareRankOf(p, &of);
        const int row = squarePayRow(one.squareRanked, of);
        one.squareExperience = int64_t(double(kSquarePay[s][row][0]) * kExperienceRate);
        one.squareZen = kSquarePay[s][row][1];
    }
}

void Realm::paySquare() {
    Player& one = me();
    if (one.squareRanked <= 0 || one.squarePaid) return;
    one.squarePaid = true;
    gain(mine(), int32_t(std::min<int64_t>(one.squareExperience, INT32_MAX)));
    one.money += one.squareZen;
}

bool Realm::claimSquare() {
    if (square_.phase != SquarePhase::Ended || me().squareRanked <= 0 || me().squarePaid) return false;
    me().squareClaimOwed = true;
    return true;
}

void Realm::squareResults(std::vector<SquareResult>* out) const {
    out->clear();
    for (size_t p = 0; p < heroes_.size(); ++p) {
        const Player& one = heroes_[p];
        if (one.squareRanked <= 0) continue;
        out->push_back({bodies_[one.body].id, one.squareRanked, one.squarePoints,
                        one.squareExperience, one.squareZen, p == me_});
    }
    std::sort(out->begin(), out->end(),
              [](const SquareResult& a, const SquareResult& b) { return a.rank < b.rank; });
}

// ---- Charon's door ------------------------------------------------------------------------

// WebZen NpcRusipher (NpcTalk.cpp:1005-1048) and GCReqmoveDevilSquare (protocol.cpp:18587-18923),
// in their order: an invitation, the gate open, the invitation's square, his level in its band.
// MU's talk opens its square window (NewUIEnterDevilSquare) and its button asks the server again;
// here the window is the quest window's page and Enter is enterSquare, as Blood Castle's.
int Realm::invitationSlot(int square) const {
    for (int i = kWorn; i < kSlots; ++i) {
        if (me().bag[i].empty() || !devilsInvitation(tables_->items[size_t(me().bag[i].item)])) continue;
        // A +0 invitation fits any square (protocol.cpp:18725-18749).
        const int level = me().bag[i].refinement;
        if (square == 0 || level == square || level == 0) return i;
    }
    return -1;
}

int Realm::squareDoorLeft() const {
    if (squareOpen_) return -1;
    if (wall_ <= 0) return 0;
    const time_t at = time_t(wall_);
    struct tm local {};
    localtime_r(&at, &local);
    return config_.square.entryLeft(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
}

SquareRefusal Realm::squareRefusal(int square) const {
    if (square < 1 || square > kSquares || square > kSquaresBuilt) return SquareRefusal::NotBuilt;
    if (invitationSlot(square) < 0) return SquareRefusal::NoInvitation;
    if (squareDoorLeft() == 0) return SquareRefusal::NotYet;
    const Body& hero = mine();
    const int level = squareLevel(hero.level, hero.kin == Kin::MagicGladiator);
    if (level < kSquareBands[square - 1][0]) return SquareRefusal::TooLow;
    if (kSquareBands[square - 1][1] != 0 && level > kSquareBands[square - 1][1]) return SquareRefusal::TooHigh;
    return SquareRefusal::None;
}

// Asked from the window, between ticks, and passed at the next tick's start, as enterCastle.
bool Realm::enterSquare(int square) {
    if (me().gating < 0 || !serving(me().gating) ||
        tables_->folk[size_t(me().gating)].number != kCharon) {
        return false;
    }
    if (squareRefusal(square) != SquareRefusal::None) return false;
    me().squareOwed = square;
    me().gating = -1;
    return true;
}

void Realm::passSquare(int square) {
    Body& hero = mine();
    if (!hero.alive() || squareRefusal(square) != SquareRefusal::None) return;
    const EnterGate* gate = enterGateNumbered(squareEnterGate(square));
    if (gate == nullptr) return;
    // The invitation is spent as he goes (protocol.cpp:18894-18896), and the fight waits for
    // what is left of the gate (sim/square.h), the whole of a held one's kSquareWait.
    me().bag.lift(invitationSlot(square));
    me().squarePassed = square;
    me().squarePassedWait = std::max(0, squareDoorLeft());
    core::logf("square: Charon lets him into Devil Square %d, the fight in %d s", square,
               me().squarePassedWait > 0 ? me().squarePassedWait : kSquareWait);
    passGate(hero, *gate);
}

void Realm::setSquare(int square, int wait) {
    if (tables_ == nullptr || tables_->map != kDevilSquareMap || square_.phase != SquarePhase::Waiting) return;
    square_.square = std::clamp(square, 1, kSquares);
    if (wait > 0) square_.startsAt = tick_ + int64_t(wait) * kSquareTicksPerSecond;
}

bool Realm::ownSquare(const Body& one) const {
    if (one.nest < 0 || size_t(one.nest) >= tables_->nests.size()) return false;
    const content::MonsterNest& nest = tables_->nests[size_t(one.nest)];
    return inSquareArena(square_.square, (nest.x1 + nest.x2) / 2, (nest.y1 + nest.y2) / 2);
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

int Realm::squareRank(int* of) const { return squareRankOf(me_, of); }

int Realm::squareRankOf(size_t at, int* of) const {
    // By points, ties to the lower level as WebZen breaks them (DevilSquareGround.cpp:32-49), then
    // to the body's id, so every mirror agrees.
    const Player& mine = heroes_[at];
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
