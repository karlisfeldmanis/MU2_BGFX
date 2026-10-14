// Devil Square's run as an instance (docs/plans/order-plan.md §9e, slice I2): the run's state -- the
// SquareRun the snapshot writes whole -- and the code that drives it: its clock and waves each
// tick, a kill's points, the ranks and the pay. The Realm holds one (Realm::square_) and hands it
// itself; the door to it -- Charon, the invitation, the gate's hours -- stays the Realm's, in Noria
// (realm_square.cpp).
#pragma once

#include <cstddef>
#include <type_traits>

#include "sim/square.h"

namespace mu::sim {

class Realm;
struct Body;

struct SquareInstance : SquareRun {
    // Once a tick, in the realm's step where squareTick was: the clock, the waves, the claims and
    // the rest's end.
    void tick(Realm& r);
    // A monster's death scored to whoever's it is (`scorer`, a player or his summon's master).
    void kill(Realm& r, const Body& dead, const Body& scorer);
    // Whether a monster's nest is in this run's square's arena (sim/square.h kSquareArena).
    bool owns(const Realm& r, const Body& one) const;
    // Every player's rank and row reckoned once, as the time is up; and the current one's paid.
    void rank(Realm& r);
    void pay(Realm& r);
    // Player `at`'s rank, 1 the best, and how many are ranked (`of`).
    int rankOf(const Realm& r, size_t at, int* of) const;
};

// Nothing of its own beyond the run, so the snapshot's bytes are the run's (realm_snapshot.cpp).
static_assert(sizeof(SquareInstance) == sizeof(SquareRun));
static_assert(std::is_trivially_copyable_v<SquareInstance>);

}  // namespace mu::sim
