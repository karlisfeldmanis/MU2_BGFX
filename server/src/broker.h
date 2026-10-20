#pragma once

// The Broker's service (server/src/broker.cpp, docs/game/broker.md): the market, the server's as the
// party is, since it spans maps.

#include <memory>
#include <vector>

#include "net/broker_wire.h"
#include "sim/realm.h"

namespace mu::server {

struct World;
struct Session;

// The book read back from characters.db, once the store is open.
void brokerOpen();
// A BrokerAsk heard on his line.
void brokerAsk(Session& one, const net::BrokerAsk& asked, std::vector<std::unique_ptr<Session>>& sessions);
// After a world's tick: its Broker commands' answers.
void brokerHeard(const World& world, const std::vector<sim::Command>& commands,
                 const std::vector<sim::Happening>& said, std::vector<std::unique_ptr<Session>>& sessions);
// Each second: listings run out, and a command no tick answered is a no.
void brokerTick(std::vector<std::unique_ptr<Session>>& sessions);
// The book written, if it changed: in the pass that writes the characters.
void brokerKeep();
// Whether a character has a listing up or a Collect waiting (a character moved to another region
// must have neither).
bool brokerHolds(uint64_t token);
// A test's (`--market-sample N`): N listings by sample sellers, from the item rows, so a market
// can be looked at and bought from on a fresh server. Never on the box's own.
void brokerSample(int count);

}  // namespace mu::server
