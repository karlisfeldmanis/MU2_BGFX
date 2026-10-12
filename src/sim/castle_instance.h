// Blood Castle's run as an instance (docs/order-plan.md §9e, slice I3): the run's state -- the
// CastleRun the snapshot writes whole -- and the code that drives it: which castle it is, its
// clock and its bridge each tick, a kill against its quotas, the win's pay, and the test commands
// that open it. The Realm holds one (Realm::run_) and hands it itself; the door to it -- the
// Messenger, the cloak, the hours -- the Archangel's page and the queries stay the Realm's
// (realm_move.cpp).
#pragma once

#include <type_traits>

#include "sim/event.h"

namespace mu::sim {

class Realm;
struct Body;

struct CastleInstance : CastleRun {
    // Which castle, 1 to 6: castle 1's garrison dressed as that castle's, one of the three statues.
    void set(Realm& r, int castle);
    // --castle-free's: no run, every barrier open, the garrison down and the statue stood up.
    void freeRun(Realm& r);
    // Once a tick, in the realm's step where castleTick was: the hand-ins and claims, the clock,
    // the bridge and its sorcerers, the rest and the way out.
    void tick(Realm& r);
    // The win's pay (paid*), into the current player's bag, once.
    void pay(Realm& r);
    // A monster's death counted against the run's quotas.
    void kill(Realm& r, const Body& dead);
    // --castle-bridge's: the run started, its first quota met, the bridge falling in `seconds`.
    void dropBridge(Realm& r, int seconds);
};

// Nothing of its own beyond the run, so the snapshot's bytes are the run's (realm_snapshot.cpp).
static_assert(sizeof(CastleInstance) == sizeof(CastleRun));
static_assert(std::is_trivially_copyable_v<CastleInstance>);

}  // namespace mu::sim
