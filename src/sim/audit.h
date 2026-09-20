// The invariants, run over a live realm rather than over a log afterwards.
//
// This is what replaces Instrument, Audit and the bot: the same checks run in the headless
// hunt, in the tests, and -- cheaply -- behind the window, so an impossible state is caught on
// the tick it happens rather than found in a picture a week later. Each finding names the tick
// and the body, because "something walked on a wall at some point" is not a bug report.
//
// Nothing in here may change the realm. It reads.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/realm.h"

namespace mu::sim {

struct Findings {
    uint64_t onBlocked = 0;    // something standing where the grid refuses it
    uint64_t belowZero = 0;    // health under zero rather than clamped at it
    uint64_t hitTheDead = 0;   // a blow landed on something already dead
    uint64_t pastTheLeash = 0; // a monster further from its nest than a grudge allows
    uint64_t unpaidLevel = 0;  // a level-up not preceded by the experience that buys it
    uint64_t total() const {
        return onBlocked + belowZero + hitTheDead + pastTheLeash + unpaidLevel;
    }
    // The first line of each kind, kept whole: the count says how bad and the line says what.
    std::vector<std::string> first;
};

// Every check that can be made from one tick's happenings and the state they left behind.
// Called after Realm::step().
void audit(const Realm& realm, Findings& findings);

}  // namespace mu::sim
