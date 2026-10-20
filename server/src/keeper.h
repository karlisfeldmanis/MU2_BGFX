#pragma once

// The keeper of the market's one book (docs/game/shared-market.md): the listings, every Collect,
// what is held out of them for a buy or a Collect not yet settled, each seller's realm and who is
// on in each realm. It answers the hands' asks -- this server's own, called directly, and every
// other realm's over the line (market_link.cpp) -- and builds every BrokerView. One server keeps
// it; on every other the book stays empty but for one handed over (keeperHandOver).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "market_link.h"

namespace mu::server {

struct Session;

// Who an ask came from: this server's own hands (`stream` empty), or a realm's line, whose
// durable asks are applied once by `stream` (rule 3).
struct MarketSource {
    std::string region;
    std::string stream;
};

// The book and the ledger read back from characters.db; this server's own holds put back.
void keeperOpen();
// The hands' (a server with --market-at): a book of its own from before, sent to the keeper as
// one durable Merge and emptied here, in one write with the outbox.
void keeperHandOver();
MarketAnswer keeperServe(const MarketAsk& ask, const MarketSource& from,
                         std::vector<std::unique_ptr<Session>>& sessions);
// A line's holds, after its outbox: every hold of that stream's not in `live` goes back (rule 4).
void keeperLive(const std::string& stream, const std::vector<uint64_t>& live);
// Who is on in a realm, as its line said, and nobody once the line went.
void keeperPresent(const std::string& region, const std::vector<std::pair<uint64_t, bool>>& tokens);
void keeperAway(const std::string& region);
// A realm's tokens with a listing, a Collect or a hold (the Holders frame), and whether that set
// may have changed since `version`.
std::vector<uint64_t> keeperHolders(const std::string& region);
uint32_t keeperVersion();
bool keeperHolds(uint64_t token);
// Each second: listings run out.
void keeperTick(int seconds);
// The book and the ledger written if they changed, together.
void keeperKeep();
// A test's (--market-sample N).
void keeperSample(int count);
// News to a seller: his server's chat and window, here or over the line.
void deliverNews(const MarketNews& news, std::vector<std::unique_ptr<Session>>& sessions);

}  // namespace mu::server
