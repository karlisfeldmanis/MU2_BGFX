// The Broker's window's sums (game/ui/broker.h), apart from its drawing so tests/market_test.cpp
// can hold them to their word: what a count of alike things costs, how many his Zen pays for, what
// a typed price says against the market, the pages, and the head's Collect word. The server
// decides every one of them again; these only say beforehand what it will say.
#pragma once

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <string>
#include <vector>

#include "net/broker_wire.h"
#include "sim/broker.h"

namespace mu::game::market {

// The rail's words: sim::BrokerKind's in its order, the Options, and the sort list with the
// query each asks.
inline constexpr const char* kKinds[] = {"Swords and axes", "Bows",  "Staves", "Shields", "Armour", "Rings and pendants",
                                         "Jewels",          "Runes", "Wings",  "Pets",    "Event tickets", "Orbs and scrolls"};
inline constexpr const char* kOptionWords[] = {"Usable by me", "Excellent", "+7 and up", "Luck", "Socket"};
struct SortOf {
    const char* word;
    sim::BrokerQuery::Sort sort;
    bool descending;
};
inline constexpr SortOf kSortsOf[] = {{"Newest", sim::BrokerQuery::Sort::Newest, false},
                                      {"Lowest price", sim::BrokerQuery::Sort::Price, false},
                                      {"Highest price", sim::BrokerQuery::Sort::Price, true},
                                      {"Ending soonest", sim::BrokerQuery::Sort::Left, false},
                                      {"Name", sim::BrokerQuery::Sort::Name, false}};
inline int sortIndex(const sim::BrokerQuery& q) {
    for (int i = 0; i < int(std::size(kSortsOf)); ++i) {
        if (kSortsOf[i].sort == q.sort && kSortsOf[i].descending == q.descending) return i;
    }
    return 0;
}

// A buy of this much asks a second press (Confirm), the old window's line kept.
constexpr int64_t kConfirmFrom = 1000000;

// Digits alone, stopped past the market's most: "1,850,000" and "1850000" are one price.
inline int64_t parsed(const std::string& text) {
    int64_t v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') continue;
        if (v > sim::kBrokerMostPrice) break;
        v = v * 10 + (c - '0');
    }
    return v;
}

// The rail's price boxes are in millions of Zen, a point allowed: "1.5" is 1,500,000, "" none.
inline int64_t parsedMillions(const std::string& text) {
    int64_t whole = 0, part = 0, scale = 1;
    bool point = false, any = false;
    for (const char c : text) {
        if (c == '.' || c == ',') {
            if (point) break;
            point = true;
            continue;
        }
        if (c < '0' || c > '9') continue;
        any = true;
        if (!point) {
            if (whole > 2000) break;
            whole = whole * 10 + (c - '0');
        } else if (scale < 1000000) {
            part = part * 10 + (c - '0');
            scale *= 10;
        }
    }
    if (!any) return 0;
    return whole * 1000000 + part * 1000000 / scale;
}

// 1,234,567 with its commas.
inline std::string commas(int64_t v) {
    const bool minus = v < 0;
    std::string digits = std::to_string(minus ? -v : v), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return minus ? "-" + out : out;
}

// The server sends a row's eight cheapest tiers, so no more than those are priced here.
inline int onOffer(const net::BrokerRow& row) {
    if (!(row.flags & net::BrokerRow::Alike)) return 1;
    int n = 0;
    for (const net::BrokerRow::Tier& t : row.tiers) n += t.count;
    return std::max(1, std::min(n, std::max(1, row.count)));
}

// What `count` of a row costs, cheapest first: one listing's price, or alike things down their
// tiers. -1 when fewer than `count` are on sale.
inline int64_t costOf(const net::BrokerRow& row, int count) {
    if (count <= 0) return 0;
    if (!(row.flags & net::BrokerRow::Alike)) return count == 1 ? row.price : -1;
    int64_t cost = 0;
    int left = count;
    for (const net::BrokerRow::Tier& t : row.tiers) {
        const int take = std::min(left, t.count);
        cost += int64_t(take) * t.price;
        left -= take;
        if (left == 0) break;
    }
    return left == 0 ? cost : -1;
}

// How many his Zen buys, cheapest first: at least 1 so the box never reads 0, at most on sale.
inline int mostAffordable(const net::BrokerRow& row, int64_t funds) {
    if (!(row.flags & net::BrokerRow::Alike)) return 1;
    int most = 0;
    for (const net::BrokerRow::Tier& t : row.tiers) {
        for (int i = 0; i < t.count && funds >= t.price; ++i) {
            funds -= t.price;
            ++most;
        }
    }
    return std::clamp(most, 1, onOffer(row));
}

inline int pagesOf(int64_t total, int per) { return std::max<int64_t>(1, (total + per - 1) / per); }

// A typed price against the market: the line under the price box, and whether List may go.
struct Verdict {
    enum class Says : uint8_t { None, Low, High, Only, Cheapest, Behind } says = Says::None;
    bool listable = false;
    int behind = 0;  // Behind: how many listings are cheaper
};
// `cheapest` the cheapest on sale now (0 for none), `onSale` how many are, `floor` what a merchant
// pays. Behind counts the cheaper ones when only the cheapest is known: one at least.
inline Verdict verdict(int64_t asked, int64_t floor, int64_t cheapest, int onSale) {
    Verdict v;
    if (asked <= 0) return v;
    if (asked < floor) {
        v.says = Verdict::Says::Low;
        return v;
    }
    if (asked > sim::kBrokerMostPrice) {
        v.says = Verdict::Says::High;
        return v;
    }
    v.listable = true;
    if (onSale <= 0 || cheapest <= 0) v.says = Verdict::Says::Only;
    else if (asked <= cheapest) v.says = Verdict::Says::Cheapest;
    else v.says = Verdict::Says::Behind, v.behind = 1;
    return v;
}

// The head's Collect: his Zen waiting and how many things, worded short.
inline std::string collectWord(const std::vector<net::BrokerParcel>& box) {
    int64_t zen = 0;
    int things = 0;
    for (const net::BrokerParcel& p : box) {
        zen += p.zen;
        if (!p.what.empty()) ++things;
    }
    if (zen <= 0 && things == 0) return "";
    std::string word = "Collect ";
    if (zen > 0) word += commas(zen);
    if (things > 0) word += (zen > 0 ? " + " : "") + std::to_string(things) + (things == 1 ? " item" : " items");
    return word;
}

// The quantity box held to what is on sale and what he typed.
inline int clampQty(int want, const net::BrokerRow& row) { return std::clamp(want, 1, onOffer(row)); }

// "48 h", "35 min".
inline std::string hoursLeft(int32_t seconds) {
    const int hours = seconds / 3600;
    if (hours >= 1) return std::to_string(hours) + " h";
    return std::to_string(std::max(1, seconds / 60)) + " min";
}

// "-6%" under the last sale, "+4%" over it, "" with no sale to go by.
inline std::string againstLast(int64_t price, int64_t last) {
    if (last <= 0 || price <= 0) return "";
    const int64_t pct = (price - last) * 100 / last;
    if (pct == 0) return "Same as the last sale";
    return std::to_string(pct < 0 ? -pct : pct) + (pct < 0 ? "% under the last sale" : "% over the last sale");
}

}  // namespace mu::game::market
