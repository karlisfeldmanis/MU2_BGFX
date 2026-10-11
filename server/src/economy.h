#pragma once

// The economy log (the user, 2026-10-11: "we need some economy log on seperated place"): where the
// realm's Zen comes from and where it goes, an hour to a line, in economy.log beside the store and
// apart from the server's own log.
//
// Read off the realm from outside it, so no rule changes: every tick each player's Zen -- what he
// carries and what his vault keeps -- is compared with the tick before, and a change is put down to
// what the realm said he did that tick (a kill's coins, a sale, a purchase, a repair, the Chaos
// Machine, a trip, a quest's pay, a trade). A change nothing explains is `other`: an event's
// reward, and whatever the realm pays or takes that says nothing of it. A trade moves Zen between
// two players and makes none, so it is counted apart from what comes in and goes out. Bots and
// players are kept apart, so their share shows.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mu::server {

struct World;
struct Session;

// Opens the log beside the store (`storePath`'s folder) and says where. False, with a line in the
// server's log, when it cannot be written; the server runs on without it.
bool economyOpen(const std::string& storePath);
// After a world's step: each of its players' Zen against the tick before, put down to what the
// realm said this tick.
void economyTick(World& world, const std::vector<std::unique_ptr<Session>>& sessions);
// The hour's line, when an hour has passed since the last; `force` writes it now (stopping).
void economyReport(const std::vector<std::unique_ptr<Session>>& sessions, bool force = false);

}  // namespace mu::server
