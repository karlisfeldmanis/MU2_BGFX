// The Golden Invasion as an instance (docs/order-plan.md §9e, slice J1): its run, the dragon's
// slot in the realm's bodies, and the begin owed to the next tick -- and the code that raises the
// dragon down, rolls for it as a wet spell begins, brings it down, stands it and ends it. The
// Realm holds one (Realm::invasion_) and hands it itself; the raid (realm_raid.cpp) reads it.
#pragma once

#include "sim/invasion.h"

namespace mu::sim {

class Realm;

struct InvasionInstance : InvasionRun {
    // The dragon's body in `bodies_`, raised down at the end of them on a map with an invasion
    // (raise), -1 elsewhere.
    int slot = -1;
    bool owed = false;     // invade()'s, begun inside the next tick
    bool landNow = false;  // invade(true)'s: landing on the tick after it begins
    void raise(Realm& realm);
    void rain(Realm& realm, bool raining, bool elsewhere);
    bool invade(Realm& realm, bool now);
    // Once a tick, where invasionTick was in the realm's step.
    void tick(Realm& realm);
    // Laid down for good: killed (Realm::kill) or its thirty minutes up.
    void end(Realm& realm);
};

}  // namespace mu::sim
