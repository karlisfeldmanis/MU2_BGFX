// The keeper of the market's one book (docs/game/shared-market.md, keeper.h). Split out of
// broker.cpp on 2026-10-21, when the realms began to share one market: the views (page, mine,
// the quote) are that file's as they were, read for a Trader in place of a Session.
#include "keeper.h"

#include "host.h"

#include "net/broker_wire.h"
#include "sim/broker.h"
#include "sim/wear.h"

namespace mu::server {

namespace {

sim::Book g_book;

// What is out of the book for an ask not yet settled: a buy's listings or a Collect's parcels.
struct Hold {
    std::string stream;  // whose: "" this server's own hands
    uint64_t serial = 0;
    Trader who;
    std::vector<sim::Listing> listings;
    std::vector<sim::Parcel> parcels;
};
std::vector<Hold> g_holds;
// Each seller's realm, by token: a token not here is this server's (a book from before).
std::map<uint64_t, std::string> g_homes;
// The last durable ask applied from each stream.
std::map<std::string, uint64_t> g_applied;
// Who is on in each other realm, by its line: token -> a bot.
std::map<std::string, std::map<uint64_t, bool>> g_present;
uint32_t g_ledgerVersion = 1;
uint32_t g_bookKept = 0, g_ledgerKept = 0;

constexpr uint32_t kLedgerMagic = 0x4547444c;  // "LDGE"
constexpr uint32_t kLedgerShape = 1;

// ---- the ledger's bytes ----

struct Out {
    std::vector<uint8_t>& b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void str(const std::string& s) {
        u32(uint32_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void held(const sim::Held& h) {
        sim::Command c;
        sim::packHeld(h, c);
        u32(uint32_t(c.a));
        u32(uint32_t(c.b));
        u32(uint32_t(c.c));
        u32(uint32_t(c.d));
        u32(c.target);
    }
};

struct In {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) { return ok = ok && at + n <= b.size(); }
    uint8_t u8() { return need(1) ? b[at++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(b[at++]) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(b[at++]) << (8 * i);
        return v;
    }
    int64_t i64() { return int64_t(u64()); }
    std::string str() {
        const uint32_t n = u32();
        if (n > (1u << 16) || !need(n)) return ok = false, std::string();
        std::string s(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return s;
    }
    sim::Held held() {
        sim::Command c;
        c.a = int32_t(u32());
        c.b = int32_t(u32());
        c.c = int32_t(u32());
        c.d = int32_t(u32());
        c.target = u32();
        return sim::unpackHeld(c);
    }
};

std::vector<uint8_t> writeLedger() {
    std::vector<uint8_t> bytes;
    Out o{bytes};
    o.u32(kLedgerMagic);
    o.u32(kLedgerShape);
    o.u32(uint32_t(g_holds.size()));
    for (const Hold& h : g_holds) {
        o.str(h.stream);
        o.u64(h.serial);
        o.u64(h.who.token);
        o.str(h.who.account);
        o.str(h.who.name);
        o.u8(h.who.bot);
        o.str(h.who.region);
        o.u32(uint32_t(h.listings.size()));
        for (const sim::Listing& l : h.listings) {
            o.u32(l.id);
            o.u64(l.seller);
            o.str(l.account);
            o.str(l.name);
            o.held(l.what);
            o.i64(l.price);
            o.i64(l.floor);
            o.u32(uint32_t(l.secondsLeft));
            o.i64(l.listedAt);
        }
        o.u32(uint32_t(h.parcels.size()));
        for (const sim::Parcel& p : h.parcels) {
            o.held(p.what);
            o.i64(p.zen);
            o.i64(p.at);
            o.u8(uint8_t(p.why));
        }
    }
    o.u32(uint32_t(g_homes.size()));
    for (const auto& [token, region] : g_homes) {
        o.u64(token);
        o.str(region);
    }
    o.u32(uint32_t(g_applied.size()));
    for (const auto& [stream, serial] : g_applied) {
        o.str(stream);
        o.u64(serial);
    }
    return bytes;
}

bool readLedger(const std::vector<uint8_t>& bytes, const std::vector<int32_t>* renumber) {
    In r{bytes};
    if (r.u32() != kLedgerMagic || r.u32() != kLedgerShape) return false;
    const auto move = [&](sim::Held& what) {
        if (renumber == nullptr || what.empty()) return true;
        what.item = what.item >= 0 && size_t(what.item) < renumber->size() ? (*renumber)[size_t(what.item)] : -1;
        return what.item >= 0;
    };
    std::vector<Hold> holds;
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        Hold h;
        h.stream = r.str();
        h.serial = r.u64();
        h.who.token = r.u64();
        h.who.account = r.str();
        h.who.name = r.str();
        h.who.bot = r.u8() != 0;
        h.who.region = r.str();
        const uint32_t listings = r.u32();
        for (uint32_t k = 0; k < listings && r.ok; ++k) {
            sim::Listing l;
            l.id = r.u32();
            l.seller = r.u64();
            l.account = r.str();
            l.name = r.str();
            l.what = r.held();
            l.price = r.i64();
            l.floor = r.i64();
            l.secondsLeft = int32_t(r.u32());
            l.listedAt = r.i64();
            if (move(l.what)) h.listings.push_back(std::move(l));
        }
        const uint32_t parcels = r.u32();
        for (uint32_t k = 0; k < parcels && r.ok; ++k) {
            sim::Parcel p;
            p.what = r.held();
            p.zen = r.i64();
            p.at = r.i64();
            p.why = sim::Parcel::Why(r.u8());
            if (move(p.what)) h.parcels.push_back(p);
        }
        holds.push_back(std::move(h));
    }
    std::map<uint64_t, std::string> homes;
    const uint32_t homeCount = r.u32();
    for (uint32_t i = 0; i < homeCount && r.ok; ++i) {
        const uint64_t token = r.u64();
        homes[token] = r.str();
    }
    std::map<std::string, uint64_t> applied;
    const uint32_t streams = r.u32();
    for (uint32_t i = 0; i < streams && r.ok; ++i) {
        std::string stream = r.str();
        applied[stream] = r.u64();
    }
    if (!r.ok) return false;
    g_holds = std::move(holds);
    g_homes = std::move(homes);
    g_applied = std::move(applied);
    return true;
}

// ---- who and where ----

const content::ItemRow* rowOf(const sim::Held& what) {
    return what.item >= 0 && size_t(what.item) < g_items.items.size() ? &g_items.items[size_t(what.item)] : nullptr;
}

std::string nameOf(const sim::Held& what) {
    const content::ItemRow* row = rowOf(what);
    if (row == nullptr) return "it";
    return what.refinement > 0 ? row->label + " +" + std::to_string(what.refinement) : row->label;
}

std::string zenText(int64_t zen) {
    std::string digits = std::to_string(zen), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

const std::string& homeOf(uint64_t token) {
    const auto at = g_homes.find(token);
    return at == g_homes.end() ? g_region : at->second;
}

void settleHome(uint64_t token, const std::string& region) {
    if (region.empty() || homeOf(token) == region) return;
    g_homes[token] = region;
    ++g_ledgerVersion;
}

// Whether he is on, and a bot: this server's sessions, or his realm's line.
bool online(uint64_t token, bool& bot, std::vector<std::unique_ptr<Session>>& sessions) {
    bot = false;
    const std::string& home = homeOf(token);
    if (home == g_region) {
        const Session* one = sessionOfToken(token, sessions);
        if (one == nullptr) return false;
        bot = one->bot;
        return one->welcomed || one->bot;
    }
    const auto realm = g_present.find(home);
    if (realm == g_present.end()) return false;
    const auto at = realm->second.find(token);
    if (at == realm->second.end()) return false;
    bot = at->second;
    return true;
}

// ---- the views: broker.cpp's, for a Trader ----

net::BrokerRow rowFor(const sim::Listing& l, const Trader& viewer, std::vector<std::unique_ptr<Session>>& sessions) {
    net::BrokerRow row;
    row.id = l.id;
    row.what = l.what;
    row.price = l.price;
    row.secondsLeft = l.secondsLeft;
    row.seller = l.name;
    bool bot = false;
    // A bot is always on (the user, 2026-10-11): its seller line is never a grey dot.
    if (online(l.seller, bot, sessions)) row.flags |= net::BrokerRow::Online;
    if (bot) row.flags |= net::BrokerRow::Bot | net::BrokerRow::Online;
    if (l.account == viewer.account) row.flags |= net::BrokerRow::Own;
    if (homeOf(l.seller) != (viewer.region.empty() ? g_region : viewer.region)) row.flags |= net::BrokerRow::Elsewhere;
    // The newest sale of the same thing: a Kris +5 against a Kris +5 with what it carries, never
    // against a plain one, so "vs last sale" compares like with like.
    for (const sim::Sale& sale : g_book.salesOf(l.what.item)) {
        const sim::Held& w = sale.what;
        if (w.refinement == l.what.refinement && w.luck == l.what.luck && w.option == l.what.option &&
            w.excellent == l.what.excellent && w.sockets == l.what.sockets) {
            row.lastSale = sale.price;
            break;
        }
    }
    return row;
}

// The Buy tab's page: alike things -- the Blesses -- one row each kind, by price.
void page(net::BrokerView& view, sim::BrokerQuery query, const Trader& who,
          std::vector<std::unique_ptr<Session>>& sessions) {
    query.watcher = who.token;
    const std::string& viewer = who.account;
    std::vector<net::BrokerRow> rows;
    std::map<int32_t, size_t> alikeAt;  // item -> its row
    for (const sim::Listing* l : sim::brokerMatches(g_book, g_items, query)) {
        const content::ItemRow* row = rowOf(l->what);
        if (row == nullptr) continue;
        if (!sim::brokerAlike(*row, l->what)) {
            rows.push_back(rowFor(*l, who, sessions));
            if (g_book.watching(query.watcher, *l, false)) rows.back().flags |= net::BrokerRow::Watched;
            continue;
        }
        // His own alike things are not his to buy, and wait under My listings.
        if (l->account == viewer) continue;
        const auto at = alikeAt.find(l->what.item);
        if (at == alikeAt.end()) {
            alikeAt[l->what.item] = rows.size();
            net::BrokerRow first = rowFor(*l, who, sessions);
            first.flags |= net::BrokerRow::Alike;
            if (g_book.watching(query.watcher, *l, true)) first.flags |= net::BrokerRow::Watched;
            first.count = 0;
            rows.push_back(std::move(first));
        }
        net::BrokerRow& group = rows[alikeAt[l->what.item]];
        ++group.count;
        if (l->price < group.price) {
            group.price = l->price;
            group.id = l->id;
        }
        auto tier = std::find_if(group.tiers.begin(), group.tiers.end(), [&](const net::BrokerRow::Tier& t) {
            return t.price == l->price && t.seller == l->name;
        });
        if (tier != group.tiers.end()) {
            ++tier->count;
        } else {
            group.tiers.push_back({l->price, 1, l->name});
        }
    }
    for (net::BrokerRow& r : rows) {
        std::sort(r.tiers.begin(), r.tiers.end(), [](const auto& a, const auto& b) { return a.price < b.price; });
        if (r.tiers.size() > 8) r.tiers.resize(8);
    }
    view.parts |= net::BrokerView::Page;
    view.total = uint32_t(rows.size());
    // The category list's counts: the same query in every kind, a kind of alike things one row,
    // and after them how many of those he watches.
    view.kinds.assign(size_t(sim::BrokerKind::Count) + 1, 0);
    sim::BrokerQuery any = query;
    any.kind = -1;
    any.watchedOnly = false;
    std::map<int32_t, bool> alikeCounted;
    for (const sim::Listing* l : sim::brokerMatches(g_book, g_items, any)) {
        const content::ItemRow* row = rowOf(l->what);
        if (row == nullptr) continue;
        const bool alike = sim::brokerAlike(*row, l->what);
        if (alike) {
            if (l->account == viewer || alikeCounted[l->what.item]) continue;
            alikeCounted[l->what.item] = true;
        }
        const size_t kind = size_t(sim::brokerKind(*row));
        if (kind < size_t(sim::BrokerKind::Count)) ++view.kinds[kind];
        if (g_book.watching(query.watcher, *l, alike)) ++view.kinds.back();
    }
    const int pages = std::max(1, int((rows.size() + sim::kBrokerPage - 1) / sim::kBrokerPage));
    const int at = std::clamp(query.page, 0, pages - 1);
    view.page = uint16_t(at);
    for (size_t i = size_t(at) * sim::kBrokerPage; i < rows.size() && i < size_t(at + 1) * sim::kBrokerPage; ++i) {
        view.rows.push_back(std::move(rows[i]));
    }
}

void mine(net::BrokerView& view, const Trader& who, std::vector<std::unique_ptr<Session>>& sessions) {
    view.parts |= net::BrokerView::Mine | net::BrokerView::Box;
    for (const sim::Listing& l : g_book.listings()) {
        if (l.seller == who.token) view.mine.push_back(rowFor(l, who, sessions));
    }
    view.accountCount = g_book.countOf(who.account);
    view.soldLately = g_book.soldBy(who.token);
    const int64_t now = wallNow();
    for (const sim::Parcel& p : g_book.collectOf(who.token)) {
        net::BrokerParcel out;
        out.what = p.what;
        out.zen = p.zen;
        out.secondsLeft = int32_t(std::max<int64_t>(0, sim::kBrokerCollectSeconds - (now - p.at)));
        out.why = uint8_t(p.why);
        view.box.push_back(out);
    }
}

void quote(net::BrokerView& view, int32_t item) {
    view.parts |= net::BrokerView::Quoted;
    view.item = item;
    // A piece's price, so a stack of jewels and a single one quote alike (the user,
    // 2026-10-20: jewels stack): the window and the bots ask that many times it.
    const auto each = [](const sim::Held& what, int64_t price) {
        const content::ItemRow* row = rowOf(what);
        return row != nullptr && sim::stacks(*row) ? price / std::max<int64_t>(1, what.durability) : price;
    };
    for (const sim::Listing& l : g_book.listings()) {
        if (l.what.item != item) continue;
        ++view.onSale;
        const int64_t one = each(l.what, l.price);
        if (view.cheapest == 0 || one < view.cheapest) view.cheapest = one;
    }
    view.sales = g_book.salesOf(item);
    for (sim::Sale& sale : view.sales) sale.price = each(sale.what, sale.price);
}

net::BrokerView viewFor(const MarketAsk& asked, std::vector<std::unique_ptr<Session>>& sessions) {
    net::BrokerView view;
    view.answered = asked.ask.ask;
    const Trader& who = asked.who;
    const net::BrokerAsk& ask = asked.ask;
    switch (ask.what) {
        case net::BrokerWhat::Look: mine(view, who, sessions); break;
        case net::BrokerWhat::Browse: page(view, ask.query, who, sessions); break;
        case net::BrokerWhat::Watch: {
            const sim::Listing* l = g_book.find(ask.listing);
            const bool alike = ask.item >= 0;
            if (!alike && l == nullptr && ask.count != 0) {
                view.no = uint8_t(sim::BrokerNo::Gone);
                view.said = "That listing is gone.";
            } else if (!g_book.watch(who.token, alike ? sim::Book::watchItem(ask.item) : ask.listing, ask.count != 0)) {
                view.no = uint8_t(sim::BrokerNo::Many);
                view.said = "You watch " + std::to_string(sim::kBrokerWatched) + " things already.";
            }
            page(view, ask.query, who, sessions);
            break;
        }
        case net::BrokerWhat::Quote: quote(view, ask.item); break;
        case net::BrokerWhat::Cancel: {
            const sim::BrokerNo no = g_book.cancel(ask.listing, who.token, wallNow());
            view.no = uint8_t(no);
            view.said = no == sim::BrokerNo::Yes ? "Cancelled: it waits in Collect." : "That listing is gone.";
            mine(view, who, sessions);
            break;
        }
        case net::BrokerWhat::Lower: {
            const sim::BrokerNo no = g_book.lower(ask.listing, who.token, ask.price);
            view.no = uint8_t(no);
            view.said = no == sim::BrokerNo::Yes ? "Price lowered."
                        : no == sim::BrokerNo::Higher ? "A price can only be lowered."
                        : no == sim::BrokerNo::Low ? "Not under what a merchant pays."
                                                   : "That listing is gone.";
            mine(view, who, sessions);
            break;
        }
        default: break;
    }
    return view;
}

// The listings a buy takes: one, or a row of alike things cheapest first, never his own account's.
sim::BrokerNo choose(const MarketAsk& asked, std::vector<uint32_t>& ids) {
    const sim::Listing* chosen = g_book.find(asked.ask.listing);
    if (chosen == nullptr) return sim::BrokerNo::Gone;
    const content::ItemRow* row = rowOf(chosen->what);
    if (row != nullptr && sim::brokerAlike(*row, chosen->what) && asked.ask.count > 1) {
        std::vector<const sim::Listing*> alike;
        for (const sim::Listing& l : g_book.listings()) {
            if (l.what.item == chosen->what.item && l.account != asked.who.account && sim::brokerAlike(*row, l.what)) {
                alike.push_back(&l);
            }
        }
        std::sort(alike.begin(), alike.end(), [](const auto* a, const auto* b) { return a->price < b->price; });
        for (size_t i = 0; i < alike.size() && int(i) < asked.ask.count; ++i) ids.push_back(alike[i]->id);
        return ids.empty() ? sim::BrokerNo::Gone : sim::BrokerNo::Yes;
    }
    const sim::BrokerNo can = g_book.canBuy(asked.ask.listing, asked.who.account);
    if (can == sim::BrokerNo::Yes) ids.push_back(asked.ask.listing);
    return can;
}

Hold* holdOf(const std::string& stream, uint64_t serial) {
    for (Hold& h : g_holds) {
        if (h.stream == stream && h.serial == serial) return &h;
    }
    return nullptr;
}

void dropHold(const Hold* gone) {
    g_holds.erase(g_holds.begin() + (gone - g_holds.data()));
    ++g_ledgerVersion;
}

// A hold nobody settles: its listings back in the book, its parcels back in his box.
void putBack(const Hold& h) {
    for (const sim::Listing& l : h.listings) g_book.restore(l);
    for (const sim::Parcel& p : h.parcels) g_book.deliver(h.who.token, p);
}

void sold(const sim::Listing& l, std::vector<std::unique_ptr<Session>>& sessions) {
    // The seller: a chat line, as the user chose (2026-10-11), wherever he plays.
    MarketNews news;
    news.token = l.seller;
    news.chat = "Your " + nameOf(l.what) + " sold for " + zenText(l.price) + " Zen. Collect it at any Broker.";
    news.view.said = "Your " + nameOf(l.what) + " sold.";
    Trader seller{l.seller, l.account, l.name, false, homeOf(l.seller)};
    mine(news.view, seller, sessions);
    deliverNews(news, sessions);
}

}  // namespace

void keeperOpen() {
    std::vector<uint8_t> bytes;
    if (!g_store.findBroker(Store::kBookRow, bytes)) {
        core::logf("broker: a new book");
    } else if (!g_book.read(bytes)) {
        core::logError("broker: the book in characters.db could not be read; a new one");
    } else {
        // A book written with another item list, its things to this server's rows (Store::useItems).
        if (const std::vector<int32_t>* map = g_store.brokerRenumber(Store::kBookRow)) {
            g_book.renumber(*map);
            core::logf("broker: the book renumbered to this server's items");
        }
        core::logf("broker: %zu listings", g_book.listings().size());
    }
    if (g_store.findBroker(Store::kLedgerRow, bytes) &&
        !readLedger(bytes, g_store.brokerRenumber(Store::kLedgerRow))) {
        core::logError("broker: the ledger in characters.db could not be read; its holds are lost");
    }
    // This server's own hands began again: whatever they held is nobody's now (rule 4).
    keeperLive("", {});
    g_bookKept = g_book.version();
    g_ledgerKept = g_ledgerVersion;
}

void keeperHandOver() {
    if (g_book.empty()) return;
    MarketAsk merge;
    merge.op = MarketOp::Merge;
    merge.serial = marketSerial();
    merge.who.region = g_region;
    merge.book = g_book.write();
    core::logf("broker: this server's own book (%zu listings) goes to the keeper", g_book.listings().size());
    marketSend(merge);
    g_book = sim::Book{};
    g_holds.clear();
    g_homes.clear();
    g_applied.clear();
    // The book emptied and the outbox holding it, in one write (marketKeep writes the outbox too).
    if (!g_store.keepBroker({{Store::kBookRow, g_book.write()}, {Store::kLedgerRow, writeLedger()}})) {
        core::logError("broker: the emptied book could not be written");
    }
    marketKeep();
    g_bookKept = g_book.version();
    g_ledgerKept = g_ledgerVersion;
}

MarketAnswer keeperServe(const MarketAsk& asked, const MarketSource& from,
                         std::vector<std::unique_ptr<Session>>& sessions) {
    MarketAnswer answer;
    answer.serial = asked.serial;
    answer.no = uint8_t(sim::BrokerNo::Yes);
    const Trader& who = asked.who;
    const int64_t now = wallNow();
    // A durable ask said again after a line went, already applied: his window as it stands.
    if (durable(asked.op) && !from.stream.empty()) {
        uint64_t& last = g_applied[from.stream];
        if (asked.serial <= last) {
            if (asked.op != MarketOp::Merge) mine(answer.view, who, sessions);
            answer.view.answered = asked.ask.ask;
            return answer;
        }
        last = asked.serial;
        ++g_ledgerVersion;
    }
    switch (asked.op) {
        case MarketOp::View: answer.view = viewFor(asked, sessions); break;
        case MarketOp::MayList:
            answer.no = uint8_t(g_book.canList(who.account, asked.ask.price, asked.floor, asked.ask.hours));
            break;
        case MarketOp::List: {
            settleHome(who.token, who.region);
            const sim::BrokerNo can = g_book.canList(who.account, asked.listing.price, asked.listing.floor, asked.ask.hours);
            if (can == sim::BrokerNo::Yes) {
                g_book.list(asked.listing, asked.ask.hours, now);
            } else {
                // Out of his bag already, and refused since it was asked (his account's 20 filled on
                // another realm meanwhile): it waits in his Collect, never lost.
                g_book.deliver(who.token, sim::Parcel{asked.listing.what, 0, now, sim::Parcel::Why::Cancelled});
            }
            answer.no = uint8_t(can);
            mine(answer.view, who, sessions);
            break;
        }
        case MarketOp::Take: {
            std::vector<uint32_t> ids;
            const sim::BrokerNo can = choose(asked, ids);
            answer.no = uint8_t(can);
            if (can != sim::BrokerNo::Yes) break;
            Hold h{from.stream, asked.serial, who, {}, {}};
            for (const uint32_t id : ids) h.listings.push_back(g_book.take(id));
            answer.listings = h.listings;
            g_holds.push_back(std::move(h));
            ++g_ledgerVersion;
            break;
        }
        case MarketOp::Settle: {
            Hold* h = holdOf(from.stream, asked.hold);
            if (h != nullptr) {
                settleHome(who.token, who.region);
                for (size_t i = 0; i < h->listings.size(); ++i) {
                    const sim::Listing& l = h->listings[i];
                    const int a = i < asked.answers.size() ? asked.answers[i] : -1;
                    if (a != 1 && a != 2 && a != 3) {
                        g_book.restore(l);
                        continue;
                    }
                    g_book.sold(l, now);
                    if (a == 2) g_book.deliver(who.token, sim::Parcel{l.what, 0, now, sim::Parcel::Why::Bought});
                    sold(l, sessions);
                }
                dropHold(h);
            }
            mine(answer.view, who, sessions);
            break;
        }
        case MarketOp::TakeBox: {
            Hold h{from.stream, asked.serial, who, {}, {}};
            while (!g_book.collectOf(who.token).empty()) h.parcels.push_back(g_book.takeParcel(who.token, 0));
            if (h.parcels.empty()) {
                answer.no = uint8_t(sim::BrokerNo::Empty);
                break;
            }
            answer.parcels = h.parcels;
            g_holds.push_back(std::move(h));
            ++g_ledgerVersion;
            break;
        }
        case MarketOp::Return: {
            if (Hold* h = holdOf(from.stream, asked.hold)) {
                for (const sim::Parcel& p : asked.parcels) g_book.deliver(who.token, p);
                dropHold(h);
            }
            mine(answer.view, who, sessions);
            break;
        }
        case MarketOp::Merge: {
            sim::Book theirs;
            if (!theirs.read(asked.book)) {
                core::logError("broker: %s's book could not be read; not merged", who.region.c_str());
                break;
            }
            std::vector<uint64_t> tokens;
            g_book.merge(theirs, &tokens);
            for (const uint64_t token : tokens) settleHome(token, who.region);
            core::logf("broker: %s's book merged: %zu listings, %zu sellers and owners", who.region.c_str(),
                       theirs.listings().size(), tokens.size());
            break;
        }
    }
    answer.view.answered = asked.ask.ask;
    return answer;
}

void keeperLive(const std::string& stream, const std::vector<uint64_t>& live) {
    for (size_t i = 0; i < g_holds.size();) {
        const Hold& h = g_holds[i];
        if (h.stream != stream || std::find(live.begin(), live.end(), h.serial) != live.end()) {
            ++i;
            continue;
        }
        core::logf("broker: a hold of %s's put back (%zu listings, %zu parcels)",
                   stream.empty() ? "this server" : stream.c_str(), h.listings.size(), h.parcels.size());
        putBack(h);
        g_holds.erase(g_holds.begin() + std::ptrdiff_t(i));
        ++g_ledgerVersion;
    }
}

void keeperAway(const std::string& region) { g_present.erase(region); }

void keeperPresent(const std::string& region, const std::vector<std::pair<uint64_t, bool>>& tokens) {
    std::map<uint64_t, bool>& on = g_present[region];
    on.clear();
    for (const auto& [token, bot] : tokens) on[token] = bot;
}

std::vector<uint64_t> keeperHolders(const std::string& region) {
    std::vector<uint64_t> out;
    for (const sim::Listing& l : g_book.listings()) {
        if (homeOf(l.seller) == region) out.push_back(l.seller);
    }
    for (const Hold& h : g_holds) {
        if (h.who.region == region) out.push_back(h.who.token);
    }
    for (const auto& [token, region_] : g_homes) {
        if (region_ == region && !g_book.collectOf(token).empty()) out.push_back(token);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

uint32_t keeperVersion() { return g_book.version() + g_ledgerVersion; }

bool keeperHolds(uint64_t token) {
    for (const sim::Listing& l : g_book.listings()) {
        if (l.seller == token) return true;
    }
    for (const Hold& h : g_holds) {
        if (h.who.token == token) return true;
    }
    return !g_book.collectOf(token).empty();
}

void keeperTick(int seconds) { g_book.expire(seconds, wallNow()); }

void keeperKeep() {
    if (g_book.version() == g_bookKept && g_ledgerVersion == g_ledgerKept) return;
    if (g_store.keepBroker({{Store::kBookRow, g_book.write()}, {Store::kLedgerRow, writeLedger()}})) {
        g_bookKept = g_book.version();
        g_ledgerKept = g_ledgerVersion;
    }
}

void keeperSample(int count) {
    std::vector<int32_t> rows;
    for (size_t i = 0; i < g_items.items.size(); ++i) {
        sim::Held probe;
        probe.item = int32_t(i);
        probe.durability = int16_t(std::max(1, sim::maximumDurability(g_items.items[i], 0)));
        if (sim::brokerable(g_items, probe) == sim::BrokerNo::Yes) rows.push_back(int32_t(i));
    }
    if (rows.empty()) return;
    const char* const names[] = {"Elowen", "Garran", "Torvik", "Maeve", "Lysa", "Ilsa", "Brannoc"};
    const int64_t now = wallNow();
    for (int i = 0; i < count; ++i) {
        const int32_t item = rows[size_t(i * 37 + i / 3) % rows.size()];
        const content::ItemRow& row = g_items.items[size_t(item)];
        sim::Listing l;
        l.what.item = item;
        const bool gear = sim::takesOptions(row);
        l.what.refinement = int16_t(gear ? (i * 7) % 10 : 0);
        l.what.luck = gear && i % 3 == 0;
        l.what.option = int8_t(gear && i % 4 == 1 ? 1 + i % 3 : 0);
        l.what.durability = int16_t(std::max(1, sim::maximumDurability(row, l.what)));
        const int who = i % 7;
        l.seller = 0x5A5A000000000000ull + uint64_t(who);
        l.account = std::string("#sample-") + names[who];
        l.name = names[who];
        l.floor = std::max<int64_t>(1, sim::npcValue(g_items, l.what));
        l.price = l.floor * (3 + i % 5);
        g_book.list(l, sim::kBrokerHours[i % 3], now - i * 60);
    }
    core::logf("broker: %d sample listings (--market-sample)", count);
}

void deliverNews(const MarketNews& news, std::vector<std::unique_ptr<Session>>& sessions) {
    const std::string& home = homeOf(news.token);
    if (home != g_region) {
        marketNewsTo(home, news);
        return;
    }
    Session* seller = sessionOfToken(news.token, sessions);
    if (seller == nullptr || !seller->welcomed) return;
    seller->keepAt = Clock::time_point{};
    chatSystem(*seller, net::Channel::Server, news.chat);
    std::vector<uint8_t> out;
    net::put(out, news.view);
    if (!seller->socket.send(out)) seller->socket.close();
}

}  // namespace mu::server
