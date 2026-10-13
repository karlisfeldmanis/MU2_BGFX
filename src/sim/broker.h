#pragma once

// The Broker's book (docs/broker.md): the market a Broker opens in Lorencia, Devias and Noria,
// one for all three -- the listings and every character's Collect box, and the rules over them.
// Ours, WoW's auction house on the user's word (2026-10-11); MU 0.75 has no market.
//
// Pure data and no realm: the server holds the one book (server/src/broker.cpp), saves it beside
// the characters and moves the things themselves with the realm's four Broker commands
// (Realm::brokerTake and its fellows), so every mirror sees a bag change. sim_test proves it.
//
// A listing is one thing out of one slot, at one buyout price: jewels do not stack in 0.75, so
// four Blesses are four listings, which the window shows as one row filled cheapest first.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/command.h"
#include "sim/items.h"

namespace mu::sim {

// The Broker, Orsin in Lorencia, Kesra in Devias and Liane in Noria. Not a MU NPC: a number past
// every MU version's (Season 6's run to the 580s). invention.
constexpr int kBroker = 900;

// Why the Broker says no; Yes when he does it.
enum class BrokerNo : uint8_t {
    Yes = 0,
    Bound,    // bound to its earner, or a thing MuMain will not trade (tradeable)
    Quest,    // a quest's thing: Loch's Feather
    Kind,     // a potion, ammunition, or an orb or scroll a merchant sells: not the market's
    Worn,     // not mended: repair it first
    Low,      // under what the NPC pays for it
    High,     // over kBrokerMostPrice
    Full,     // the account has kBrokerListings up
    Hours,    // not 12, 24 or 48
    Gone,     // that listing is no more: sold, cancelled, run out
    Own,      // a listing of his own account's
    NotYours, // cancelling or lowering another's
    Higher,   // a lowered price that is not lower
    Away,     // not at a Broker
    Pay,      // he cannot pay, with his vault's Zen too
    Busy,     // the last ask is still being answered
    Empty,    // nothing in that slot, or not the thing he saw
    Closed,   // no market here: local play, which has no server
    Many,     // he watches kBrokerWatched things already
};

constexpr int kBrokerListings = 20;               // an account's, the user's
constexpr int64_t kBrokerMostPrice = 2000000000;  // MU's Zen ceiling, the user's
constexpr int kBrokerHours[3] = {12, 24, 48};
constexpr int64_t kBrokerCollectSeconds = 30 * 24 * 3600;  // then gone, as WoW's mail

// The window's categories, in its order.
enum class BrokerKind : uint8_t {
    Swords, Bows, Staves, Shields, Armour, Jewellery, Jewels, Runes, Wings, Pets, Tickets, Skills, Count
};

// What the NPC counter pays for it (Realm::sellValue's price, without the counter), -1 for a thing
// no merchant buys. The Broker's floor.
int64_t npcValue(const content::Tables& tables, const Held& what);
// Whether it may be listed, and if not why.
BrokerNo brokerable(const content::Tables& tables, const Held& what);
BrokerKind brokerKind(const content::ItemRow& row);
// A thing that sells as one of many alike: a jewel, a rune or a ticket's part with nothing of its
// own. The window rows them together and a buy of N takes the N cheapest.
bool brokerAlike(const content::ItemRow& row, const Held& what);

// A whole Held in a command's a, b, c, d and target, for the server's Broker commands.
void packHeld(const Held& what, Command& into);
Held unpackHeld(const Command& from);

struct Listing {
    uint32_t id = 0;
    uint64_t seller = 0;     // his character's token: whose Collect the money goes to
    std::string account;     // whose 20 it counts against: the account, or "#token" for a bot
    std::string name;        // shown on the listing
    Held what;
    int64_t price = 0;
    int64_t floor = 0;       // npcValue as listed: a lowered price stays above it
    int32_t secondsLeft = 0; // counted only while the server runs (Book::expire)
    int64_t listedAt = 0;    // the wall's seconds: newest first
};

struct Parcel {
    enum class Why : uint8_t { Sold, Unsold, Cancelled, Bought };
    Held what;          // empty for Zen alone
    int64_t zen = 0;
    int64_t at = 0;     // the wall's seconds it came: gone kBrokerCollectSeconds after
    Why why = Why::Sold;
};

// A sale remembered: the Sell tab's "typical" and "last sales" for a kind of thing, and the
// seller's "sold lately".
struct Sale {
    Held what;
    int64_t price = 0;
    int64_t at = 0;
};
constexpr size_t kBrokerSalesKept = 10;  // a kind's
constexpr size_t kBrokerSoldKept = 6;    // a seller's
constexpr size_t kBrokerWatched = 40;    // what one character watches

class Book {
public:
    BrokerNo canList(const std::string& account, int64_t price, int64_t floor, int hours) const;
    // Put up. The caller has taken the thing out of his bag (Realm::brokerTake). Its id.
    uint32_t list(Listing listing, int hours, int64_t now);
    const Listing* find(uint32_t id) const;
    BrokerNo canBuy(uint32_t id, const std::string& buyerAccount) const;
    // Out of the book for a buy; put back by `restore` if the buyer could not pay.
    Listing take(uint32_t id);
    void restore(const Listing& listing);
    // Paid for: the price into the seller's Collect, and the sale remembered.
    void sold(const Listing& listing, int64_t now);
    // A kind's last sales and a seller's, newest first.
    const std::vector<Sale>& salesOf(int32_t item) const;
    const std::vector<Sale>& soldBy(uint64_t seller) const;
    BrokerNo cancel(uint32_t id, uint64_t seller, int64_t now);
    BrokerNo lower(uint32_t id, uint64_t seller, int64_t price);
    // `seconds` of the server's running gone by: what has run out goes back to its seller's
    // Collect, and what has lain there kBrokerCollectSeconds is gone. How many ran out.
    int expire(int seconds, int64_t now);
    void deliver(uint64_t owner, const Parcel& parcel);
    // His Collect, oldest first; one out of it by its place, for delivering.
    const std::vector<Parcel>& collectOf(uint64_t owner) const;
    Parcel takeParcel(uint64_t owner, int index);
    // What he watches, the Buy tab's star: a listing by its id, or alike things -- the Blesses --
    // by watchItem(item). False when he watches kBrokerWatched already.
    static uint64_t watchItem(int32_t item) { return (uint64_t(1) << 32) | uint32_t(item); }
    bool watch(uint64_t who, uint64_t key, bool on);
    bool watching(uint64_t who, const Listing& listing, bool alike) const;

    const std::vector<Listing>& listings() const { return listings_; }
    int countOf(const std::string& account) const;
    // Moved by every change: a window redraws on it, the server saves on it.
    uint32_t version() const { return version_; }

    std::vector<uint8_t> write() const;
    bool read(const std::vector<uint8_t>& bytes);

private:
    std::vector<Listing> listings_;
    std::map<uint64_t, std::vector<Parcel>> collect_;
    std::map<int32_t, std::vector<Sale>> sales_;
    std::map<uint64_t, std::vector<Sale>> soldBy_;
    std::map<uint64_t, std::vector<uint64_t>> watches_;
    uint32_t nextId_ = 1;
    uint32_t version_ = 0;
};

// One page of what is on sale, for the Buy tab.
struct BrokerQuery {
    int kind = -1;          // a BrokerKind, -1 for all
    std::string text;       // in the name, any case
    int usableBy = -1;      // a Kin, -1 for any
    bool excellentOnly = false, sevenUp = false;
    uint64_t seller = 0;    // one seller's, 0 for all (the server's own: a token is never sent)
    std::string sellerName; // one seller's by the name on his listings, empty for all
    bool watchedOnly = false;  // what `watcher` watches (the server's own: a token is never sent)
    uint64_t watcher = 0;
    enum class Sort : uint8_t { Price, Name, Seller, Left } sort = Sort::Price;
    bool descending = false;
    int page = 0;
};
constexpr int kBrokerPage = 12;
// The listings that match, sorted, all pages; the window takes its page.
std::vector<const Listing*> brokerMatches(const Book& book, const content::Tables& tables,
                                          const BrokerQuery& query);

}  // namespace mu::sim
