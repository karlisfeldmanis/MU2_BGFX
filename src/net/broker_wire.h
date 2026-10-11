#pragma once

// The Broker on the wire (docs/broker.md): a BrokerAsk up and a BrokerView down, outside the
// lockstep as the chat and the party are -- the market is the server's, and spans maps. What
// moves a player's things is the realm's (Command::Kind::BrokerTake and its fellows), which the
// server queues into his world's tick. Its own file: net/wire.cpp is at its size.

#include <cstdint>
#include <string>
#include <vector>

#include "sim/broker.h"
#include "sim/items.h"

namespace mu::net {

// Up: what he asks the Broker. Each carries his own number back in its answer.
enum class BrokerWhat : uint8_t {
    Look = 0,     // his listings, his Collect and what he has sold lately
    Browse = 1,   // a page of what is on sale (query)
    Quote = 2,    // what a kind of thing goes for: the Sell tab's (item)
    List = 3,     // put up: from his bag or vault (fromVault, slot, seen), price, hours
    Buy = 4,      // listing, count: a row of alike things fills cheapest first
    Cancel = 5,   // listing
    Lower = 6,    // listing, price
    Collect = 7,  // everything in his Collect that fits
    Watch = 8,    // listing, or item for alike things; count 1 to watch, 0 to stop (query: the page)
};
struct BrokerAsk {
    BrokerWhat what = BrokerWhat::Look;
    uint32_t ask = 0;
    sim::BrokerQuery query;
    int32_t item = -1;
    bool fromVault = false;
    int32_t slot = -1;
    sim::Held seen;
    int64_t price = 0;
    uint8_t hours = 24;
    uint32_t listing = 0;
    int32_t count = 1;
};

// One row of the Buy tab or of his listings.
struct BrokerRow {
    enum Flag : uint8_t { Online = 1, Own = 2, Alike = 4, Bot = 8, Watched = 16 };
    uint32_t id = 0;        // the listing; for alike things, the cheapest
    sim::Held what;
    int64_t price = 0;      // its, or the cheapest of the alike
    int32_t secondsLeft = 0;
    std::string seller;
    uint8_t flags = 0;
    int32_t count = 1;      // alike: how many are on sale
    struct Tier {
        int64_t price = 0;
        int32_t count = 0;
        std::string seller;
    };
    std::vector<Tier> tiers;  // alike: by price, cheapest first
};

struct BrokerParcel {
    sim::Held what;
    int64_t zen = 0;
    int32_t secondsLeft = 0;  // before it is gone
    uint8_t why = 0;          // sim::Parcel::Why
};

// Down: the answer to his last ask, and whatever parts of the market it changed.
struct BrokerView {
    enum Part : uint8_t { Page = 1, Mine = 2, Box = 4, Quoted = 8 };
    uint32_t answered = 0;   // the ask this answers, 0 for news (something of his sold)
    uint8_t no = 0;          // sim::BrokerNo
    std::string said;        // one line for the window
    uint8_t parts = 0;
    // Page
    uint32_t total = 0;      // rows that match, all pages
    uint16_t page = 0;
    std::vector<BrokerRow> rows;
    // Rows that match in each sim::BrokerKind, every other choice of the query kept: the
    // category list's counts, then one more: how many he watches.
    std::vector<uint16_t> kinds;
    // Mine
    std::vector<BrokerRow> mine;
    int32_t accountCount = 0;
    std::vector<sim::Sale> soldLately;
    // Box
    std::vector<BrokerParcel> box;
    // Quoted
    int32_t item = -1;
    int64_t cheapest = 0;
    int32_t onSale = 0;
    std::vector<sim::Sale> sales;
};

void put(std::vector<uint8_t>& out, const BrokerAsk& one);
void put(std::vector<uint8_t>& out, const BrokerView& one);
bool parse(const std::vector<uint8_t>& body, BrokerAsk& out);
bool parse(const std::vector<uint8_t>& body, BrokerView& out);

}  // namespace mu::net
