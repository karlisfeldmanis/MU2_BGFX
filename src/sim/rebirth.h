// **Rebirth**, the reset (the user, 2026-10-22, docs/game/rebirth.md): at level 400 he kneels before
// Lugard in Devias and his body goes back to level 1 for Zen; his stats, his points in hand, his
// skills and his talents stay ("the heavens take the body's levels, the soul keeps everything it
// has learned"). Accumulative: every level on the way back up gives its points again, and his
// talent points count every life's levels (sim::talentPoints). Worn gear that asks for a higher
// level than 1 goes into the bag, as Reset Points does; a bag without room refuses the rebirth
// whole. No cap. Ours: MU has no reset.
#pragma once

#include <cstdint>

#include "sim/rules.h"

namespace mu::sim {

// Lugard, the Fallen Knight (MuMain's MONSTER_LUGARD), who keeps the rite at Devias 207,45.
constexpr int kLugard = 540;
// The level a rebirth asks: the top.
constexpr int kRebirthLevel = kMaximumLevel;
// What the `next` rebirth costs (1 for the first): 10,000,000 Zen times its number -- the user,
// 2026-10-22: "lets ask 10million zen for reset and it increases for every reset".
constexpr int64_t kRebirthZen = 10000000;
constexpr int64_t rebirthZen(int next) { return kRebirthZen * int64_t(next < 1 ? 1 : next); }

}  // namespace mu::sim
