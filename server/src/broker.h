#pragma once

// The Broker's service (server/src/broker.cpp, docs/game/broker.md): the market's hands on this
// server, the book the keeper's (keeper.h), here or over the line (market_link.h,
// docs/game/shared-market.md).

#include <memory>
#include <vector>

#include "market_link.h"
#include "net/broker_wire.h"
#include "sim/realm.h"

namespace mu::server {

struct World;
struct Session;

// The book read back from characters.db, once the store is open, or the outbox of a server whose
// keeper is another's (and its own book from before handed over).
void brokerOpen();
// A BrokerAsk heard on his line.
void brokerAsk(Session& one, const net::BrokerAsk& asked, std::vector<std::unique_ptr<Session>>& sessions);
// After a world's tick: its Broker commands' answers.
void brokerHeard(const World& world, const std::vector<sim::Command>& commands,
                 const std::vector<sim::Happening>& said, std::vector<std::unique_ptr<Session>>& sessions);
// The keeper's answer from over the line, and its news for a seller here.
void brokerAnswered(const MarketAnswer& answer, std::vector<std::unique_ptr<Session>>& sessions);
void brokerNews(const MarketNews& news, std::vector<std::unique_ptr<Session>>& sessions);
// The keeper's line went: what it was answering is refused.
void brokerLost(std::vector<std::unique_ptr<Session>>& sessions);
// The holds these hands still work on, said to the keeper after the outbox (rule 4).
std::vector<uint64_t> brokerLive();
// Each second: listings run out, and a command no tick answered is a no.
void brokerTick(std::vector<std::unique_ptr<Session>>& sessions);
// The book (or the outbox) written, if it changed: in the pass that writes the characters. Then
// what waited for the disk goes over the line.
void brokerKeep();
// Whether a character has a listing up or a Collect waiting (a character moved to another region
// must have neither).
bool brokerHolds(uint64_t token);
// A test's (`--market-sample N`): N listings by sample sellers, from the item rows, so a market
// can be looked at and bought from on a fresh server. Never on the box's own. The keeper's alone.
void brokerSample(int count);

}  // namespace mu::server
