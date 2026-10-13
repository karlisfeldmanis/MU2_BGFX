// Devil Square from the Realm's side: Charon's door in Noria, the test commands and the queries
// the drawing asks. The run itself is sim/square_instance.{h,cpp} (the Instance's I2).

#include "sim/realm.h"

#include "core/log.h"

#include <algorithm>
#include <ctime>
#include <limits>

namespace mu::sim {

bool Realm::claimSquare() {
    if (square_.phase != SquarePhase::Ended || me().event.squareRanked <= 0 || me().event.squarePaid) return false;
    me().event.squareClaimOwed = true;
    return true;
}

void Realm::squareResults(std::vector<SquareResult>* out) const {
    out->clear();
    for (size_t p = 0; p < heroes_.size(); ++p) {
        const Player& one = heroes_[p];
        if (one.event.squareRanked <= 0) continue;
        out->push_back({bodies_[one.body].id, one.event.squareRanked, one.event.squarePoints,
                        one.event.squareExperience, one.event.squareZen, p == me_});
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
    // The band's floor only, as Blood Castle's since 2026-10-04 (the user, 2026-10-13: "bots can use
    // lower tire BC,DS they dont have to use higher tiers always"): WebZen's ceiling, TooHigh, is
    // never said.
    if (level < kSquareBands[square - 1][0]) return SquareRefusal::TooLow;
    return SquareRefusal::None;
}

// Asked from the window, between ticks, and passed at the next tick's start, as enterCastle.
bool Realm::enterSquare(int square) {
    if (me().gating < 0 || !serving(me().gating) ||
        tables_->folk[size_t(me().gating)].number != kCharon) {
        return false;
    }
    if (squareRefusal(square) != SquareRefusal::None) return false;
    me().event.squareOwed = square;
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
    me().event.squarePassed = square;
    me().event.squarePassedWait = std::max(0, squareDoorLeft());
    core::logf("square: Charon lets him into Devil Square %d, the fight in %d s", square,
               me().event.squarePassedWait > 0 ? me().event.squarePassedWait : kSquareWait);
    passGate(hero, *gate);
}

void Realm::setSquare(int square, int wait) {
    if (event() != EventKind::Square || square_.phase != SquarePhase::Waiting) return;
    square_.square = std::clamp(square, 1, kSquares);
    if (wait > 0) square_.startsAt = tick_ + int64_t(wait) * kSquareTicksPerSecond;
}

void Realm::jumpSquare(int seconds) {
    if (square_.phase == SquarePhase::None) return;
    const int at = std::clamp(seconds, 0, kSquarePlay);
    square_.phase = SquarePhase::Running;
    square_.startsAt = tick_;
    square_.endsAt = tick_ + int64_t(kSquarePlay - at) * kSquareTicksPerSecond;
}

void Realm::freeSquare() {
    if (square_.phase == SquarePhase::None) return;
    square_ = SquareInstance{};
    for (Body& one : bodies_) {
        if (one.monster() && !one.alive()) one.risesAt = tick_;
    }
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

int Realm::squareRank(int* of) const { return square_.rankOf(*this, me_, of); }

// Any player's, by his body's id, 0 for an id that is no player's: which square and castle the
// server raises for him. Out of realm.h to pay for the Broker's lines there.
int Realm::squarePassedOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at >= 0 ? heroes_[size_t(at)].event.squarePassed : 0;
}

int Realm::squarePassedWaitOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at >= 0 ? heroes_[size_t(at)].event.squarePassedWait : 0;
}

int Realm::castlePassedOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at >= 0 ? heroes_[size_t(at)].event.castlePassed : 0;
}

}  // namespace mu::sim
