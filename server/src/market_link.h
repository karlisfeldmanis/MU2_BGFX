#pragma once

// One market for every realm (docs/game/shared-market.md): what the hands on each server ask the
// keeper of the one book, and its answers, and the line between two servers that carries them.
//
// The keeper is one server's (--market-serve PORT), or a server's alone when it has no market
// flag, as every server was before. Every other server (--market-at HOST:PORT) is its hands: it
// moves the things in its own realms and asks the keeper for the rest over one TCP line, opened
// with a challenge and an HMAC of MU2_MARKET_KEY and refused when the two item lists differ.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>

#include "net/broker_wire.h"
#include "sim/broker.h"

namespace mu::server {

struct Session;

// Who asks: what the keeper needs of a character it never sees.
struct Trader {
    uint64_t token = 0;
    std::string account;  // whose 20 listings (broker.cpp accountOf)
    std::string name;
    bool bot = false;
    std::string region;   // the realm he plays on (g_region of his server)
};

enum class MarketOp : uint8_t {
    View = 1,     // Look, Browse, Watch, Quote, Cancel, Lower: the view as before
    MayList = 2,  // whether his account may put this up at that price (ask.price, ask.hours, floor)
    List = 3,     // durable: put up, the thing already out of his bag (listing, ask.hours)
    Take = 4,     // a buy: ask.listing, ask.count out of the book into a hold
    Settle = 5,   // durable: a buy's hold settled, the realm's answer for each listing
    TakeBox = 6,  // a Collect: his whole box out into a hold
    Return = 7,   // durable: a Collect's hold settled, what did not reach him back in his box
    Merge = 8,    // durable: this server's own book from before, once
};
// Asks that change both servers: sent only after the asker has saved, kept until answered, and
// applied once (docs/game/shared-market.md, rules 1 and 3).
inline bool durable(MarketOp op) {
    return op == MarketOp::List || op == MarketOp::Settle || op == MarketOp::Return || op == MarketOp::Merge;
}

struct MarketAsk {
    MarketOp op = MarketOp::View;
    uint64_t serial = 0;           // the asking server's, in its own stream
    Trader who;
    net::BrokerAsk ask;            // as he asked it
    int64_t floor = 0;             // MayList: npcValue as he lists it
    sim::Listing listing;          // List
    uint64_t hold = 0;             // Settle, Return: the Take's or TakeBox's serial
    std::vector<int32_t> answers;  // Settle: the realm's answer for each listing taken, in order
    std::vector<sim::Parcel> parcels;  // Return: what did not reach him
    std::vector<uint8_t> book;     // Merge: sim::Book::write
};

struct MarketAnswer {
    uint64_t serial = 0;
    uint8_t no = 0;                    // sim::BrokerNo
    net::BrokerView view;              // View, List, Settle, Return: his window's parts
    std::vector<sim::Listing> listings;  // Take: what is held for him
    std::vector<sim::Parcel> parcels;    // TakeBox: his box, held
};

// The keeper to a seller's server: his thing sold. A chat line and his window's news.
struct MarketNews {
    uint64_t token = 0;
    std::string chat;
    net::BrokerView view;
};

// ---- the line's frames: a u32 length, a u8 kind, the body -------------------------------------

enum class LinkKind : uint8_t {
    Challenge = 1,  // keeper: a nonce (32 hex digits)
    Hello = 2,      // hands: region, stream, the item list's fingerprint, proof
    Welcome = 3,    // keeper: let in
    Refused = 4,    // keeper: not, and why
    Ask = 5,        // hands: a MarketAsk
    Answer = 6,     // keeper: a MarketAnswer
    News = 7,       // keeper: a MarketNews
    Present = 8,    // hands: who is on, (token, bot) each
    Holders = 9,    // keeper: the tokens of this realm's that have a listing, a Collect or a hold
    Live = 10,      // hands: the holds it still works on, after its outbox
};
constexpr uint32_t kMostLinkFrame = 64u << 20;

struct LinkHello {
    std::string region;
    uint64_t stream = 0;
    uint64_t items = 0;
    std::string proof;
};

void putLink(std::vector<uint8_t>& out, LinkKind kind, const std::vector<uint8_t>& body);
// One frame off `buffer` from `at`: 1 taken, 0 not all there yet, -1 not a link's.
int takeLink(const std::vector<uint8_t>& buffer, size_t& at, LinkKind& kind, std::vector<uint8_t>& body);

std::vector<uint8_t> bodyOf(const MarketAsk& one);
std::vector<uint8_t> bodyOf(const MarketAnswer& one);
std::vector<uint8_t> bodyOf(const MarketNews& one);
std::vector<uint8_t> bodyOf(const LinkHello& one);
std::vector<uint8_t> bodyOf(const std::string& text);
std::vector<uint8_t> bodyOf(const std::vector<std::pair<uint64_t, bool>>& tokens);
bool parse(const std::vector<uint8_t>& body, MarketAsk& out);
bool parse(const std::vector<uint8_t>& body, MarketAnswer& out);
bool parse(const std::vector<uint8_t>& body, MarketNews& out);
bool parse(const std::vector<uint8_t>& body, LinkHello& out);
bool parse(const std::vector<uint8_t>& body, std::string& out);
bool parse(const std::vector<uint8_t>& body, std::vector<std::pair<uint64_t, bool>>& out);

// The durable asks waiting for the keeper, with the stream they are numbered in: a hands
// server's, in characters.db beside its characters.
std::vector<uint8_t> writeOutbox(uint64_t stream, uint64_t next, const std::vector<MarketAsk>& asks);
bool readOutbox(const std::vector<uint8_t>& bytes, uint64_t& stream, uint64_t& next, std::vector<MarketAsk>& asks);

// The item list's fingerprint: two servers share a market only when theirs is the same, since a
// thing is its row's number.
uint64_t itemsFingerprint(const content::Tables& tables);

// ---- the line (market_link.cpp) ----------------------------------------------------------------

// Flags: this server keeps the book and takes lines on `port`, or is hands to `host:port`; and the
// key both prove themselves with (--market-key, else MU2_MARKET_KEY), 64 hex digits.
bool marketServe(int port, std::string& error);
bool marketAt(const std::string& address);
bool marketKey(const std::string& hex);
// The hands': the outbox read back from characters.db (brokerOpen, before the book).
void marketLoad();
// Whether the keeper is this server's own (no flag, or --market-serve).
bool marketKeeperHere();
// The hands: whether the keeper's line is up and let in.
bool marketOpen();
// The hands: an ask to the keeper. A durable one waits in the outbox, sent once saved (marketSaved).
void marketSend(const MarketAsk& ask);
// The hands: the stream's next number, and the outbox written with it (marketKeep).
uint64_t marketSerial();
// Each pass: lines taken in, frames read and answered, a line to the keeper opened again.
void marketPass(std::vector<std::unique_ptr<Session>>& sessions);
// After the pass's saves: what may go only once it is on the disk goes.
void marketSaved();
// The hands: the outbox written if it changed (in brokerKeep's pass).
void marketKeep();
// The line's sockets, for the loop's wait.
void marketWatch(std::vector<pollfd>& watch);
// The keeper: news for a seller on another realm; false when that realm's line is down.
bool marketNewsTo(const std::string& region, const MarketNews& news);
// The hands: whether the keeper has said this token holds something here.
bool marketHolder(uint64_t token);

}  // namespace mu::server
