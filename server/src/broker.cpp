// The Broker's service (docs/game/broker.md): the market a Broker opens in Lorencia, Devias and Noria,
// one book for all three, as the party is one table for every map. It hears a BrokerAsk, checks
// he stands at a Broker, and answers with a BrokerView; what moves his things is the realm's,
// four commands it queues into his world's next tick with tickets of its own (kBrokerTickets)
// and finishes when that tick answers them (brokerHeard). The book is written to characters.db
// whenever it changed, in the pass that writes the characters it moved things for.
#include "broker.h"

#include "host.h"

#include "net/broker_wire.h"
#include "sim/broker.h"
#include "sim/vault.h"
#include "sim/wear.h"

namespace mu::server {

namespace {

// The Broker's tickets, the server's own and apart from a Join's (kJoinTickets).
constexpr uint32_t kBrokerTickets = 0xC0000000u;

sim::Book g_book;
uint32_t g_bookKept = 0;
uint32_t g_nextTicket = kBrokerTickets;
uint32_t g_nextBatch = 1;

// One command queued and not yet answered.
struct Pending {
    enum class Op : uint8_t { List, Buy, Give, Zen } op = Op::List;
    const World* world = nullptr;
    uint32_t ticket = 0;
    uint32_t batch = 0;
    uint64_t token = 0;
    sim::Listing listing;  // List: what goes up; Buy: what was taken out of the book
    int hours = 24;
    sim::Parcel parcel;    // Give and Zen: what was taken out of his Collect
    Clock::time_point at = Clock::now();
};
std::vector<Pending> g_pending;

// An ask of several commands -- a buy of five Blesses, a Collect -- answered once, when its last
// command is.
struct Batch {
    uint64_t token = 0;
    uint32_t ask = 0;
    net::BrokerWhat what = net::BrokerWhat::Look;
    int left = 0, done = 0, failed = 0, full = 0, bagged = 0;
    bool vault = false;  // a player's buy, into his vault
    int64_t zen = 0;
    std::string thing;
};
std::map<uint32_t, Batch> g_batches;

std::string accountOf(const Session& one) {
    if (!one.account.empty()) return one.account;
    char key[24];
    std::snprintf(key, sizeof key, "#%016llx", (unsigned long long)one.token);
    return key;
}

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

bool same(const sim::Held& a, const sim::Held& b) {
    sim::Command x, y;
    sim::packHeld(a, x);
    sim::packHeld(b, y);
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.target == y.target;
}

bool busy(uint64_t token) {
    return std::any_of(g_pending.begin(), g_pending.end(), [&](const Pending& p) { return p.token == token; });
}

void send(Session& one, const net::BrokerView& view) {
    std::vector<uint8_t> out;
    net::put(out, view);
    if (!one.socket.send(out)) one.socket.close();
}

void refuse(Session& one, uint32_t ask, sim::BrokerNo no, const std::string& said) {
    net::BrokerView view;
    view.answered = ask;
    view.no = uint8_t(no);
    view.said = said;
    send(one, view);
}

net::BrokerRow rowFor(const sim::Listing& l, const std::string& viewer,
                      std::vector<std::unique_ptr<Session>>& sessions) {
    net::BrokerRow row;
    row.id = l.id;
    row.what = l.what;
    row.price = l.price;
    row.secondsLeft = l.secondsLeft;
    row.seller = l.name;
    const Session* seller = sessionOfToken(l.seller, sessions);
    // A bot is always on (the user, 2026-10-11): its seller line is never a grey dot.
    if (seller != nullptr && seller->welcomed) row.flags |= net::BrokerRow::Online;
    if (seller != nullptr && seller->bot) row.flags |= net::BrokerRow::Bot | net::BrokerRow::Online;
    if (l.account == viewer) row.flags |= net::BrokerRow::Own;
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
void page(net::BrokerView& view, const sim::BrokerQuery& query, const std::string& viewer,
          std::vector<std::unique_ptr<Session>>& sessions) {
    std::vector<net::BrokerRow> rows;
    std::map<int32_t, size_t> alikeAt;  // item -> its row
    for (const sim::Listing* l : sim::brokerMatches(g_book, g_items, query)) {
        const content::ItemRow* row = rowOf(l->what);
        if (row == nullptr) continue;
        if (!sim::brokerAlike(*row, l->what)) {
            rows.push_back(rowFor(*l, viewer, sessions));
            if (g_book.watching(query.watcher, *l, false)) rows.back().flags |= net::BrokerRow::Watched;
            continue;
        }
        // His own alike things are not his to buy, and wait under My listings.
        if (l->account == viewer) continue;
        const auto at = alikeAt.find(l->what.item);
        if (at == alikeAt.end()) {
            alikeAt[l->what.item] = rows.size();
            net::BrokerRow first = rowFor(*l, viewer, sessions);
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

void mine(net::BrokerView& view, const Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    view.parts |= net::BrokerView::Mine | net::BrokerView::Box;
    const std::string account = accountOf(one);
    for (const sim::Listing& l : g_book.listings()) {
        if (l.seller == one.token) view.mine.push_back(rowFor(l, account, sessions));
    }
    view.accountCount = g_book.countOf(account);
    view.soldLately = g_book.soldBy(one.token);
    const int64_t now = wallNow();
    for (const sim::Parcel& p : g_book.collectOf(one.token)) {
        net::BrokerParcel out;
        out.what = p.what;
        out.zen = p.zen;
        out.secondsLeft = int32_t(std::max<int64_t>(0, sim::kBrokerCollectSeconds - (now - p.at)));
        out.why = uint8_t(p.why);
        view.box.push_back(out);
    }
}

uint32_t queue(Session& one, sim::Command command, Pending pending) {
    command.player = one.player;
    command.ticket = g_nextTicket++;
    if (g_nextTicket < kBrokerTickets) g_nextTicket = kBrokerTickets;
    one.world->queued.push_back(command);
    pending.world = one.world;
    pending.ticket = command.ticket;
    pending.token = one.token;
    g_pending.push_back(std::move(pending));
    return command.ticket;
}

uint32_t batchOf(const Session& one, uint32_t ask, net::BrokerWhat what, int commands, std::string thing) {
    const uint32_t id = g_nextBatch++;
    g_batches[id] = {one.token, ask, what, commands, 0, 0, 0, 0, !one.bot, 0, std::move(thing)};
    return id;
}

void list(Session& one, const net::BrokerAsk& asked) {
    const sim::Realm& realm = *one.world->realm;
    const sim::Held* there = nullptr;
    if (asked.fromVault) {
        const sim::Vault* vault = realm.vaultOf(one.player);
        if (vault != nullptr && asked.slot >= 0 && asked.slot < sim::kVaultCells) there = &(*vault)[asked.slot];
    } else {
        const sim::Satchel* bag = realm.satchelOf(one.player);
        if (bag != nullptr && sim::baggable(asked.slot)) there = &(*bag)[asked.slot];
    }
    if (there == nullptr || there->empty() || !same(*there, asked.seen)) {
        refuse(one, asked.ask, sim::BrokerNo::Empty, "That is no longer there.");
        return;
    }
    const sim::BrokerNo kind = sim::brokerable(one.world->tables, *there);
    if (kind != sim::BrokerNo::Yes) {
        refuse(one, asked.ask, kind, "The Broker will not take that.");
        return;
    }
    const int64_t floor = std::max<int64_t>(1, sim::npcValue(one.world->tables, *there));
    const std::string account = accountOf(one);
    const sim::BrokerNo can = g_book.canList(account, asked.price, floor, asked.hours);
    if (can != sim::BrokerNo::Yes) {
        refuse(one, asked.ask, can, can == sim::BrokerNo::Low ? "Under what a merchant pays: " + zenText(floor) + "."
                                    : can == sim::BrokerNo::Full ? "Your account has 20 listings up." : "Not at that price.");
        return;
    }
    Pending p;
    p.op = Pending::Op::List;
    p.listing.seller = one.token;
    p.listing.account = account;
    p.listing.name = one.name;
    p.listing.what = *there;
    p.listing.price = asked.price;
    p.listing.floor = floor;
    p.hours = asked.hours;
    p.batch = batchOf(one, asked.ask, asked.what, 1, nameOf(*there));
    sim::Command take{.kind = sim::Command::Kind::BrokerTake};
    sim::packHeld(*there, take);
    take.zen = asked.slot | (asked.fromVault ? 0x10000 : 0);
    queue(one, take, std::move(p));
}

void buy(Session& one, const net::BrokerAsk& asked) {
    const sim::Listing* chosen = g_book.find(asked.listing);
    const std::string account = accountOf(one);
    if (chosen == nullptr) {
        refuse(one, asked.ask, sim::BrokerNo::Gone, "Someone bought it first.");
        return;
    }
    std::vector<uint32_t> ids;
    const content::ItemRow* row = rowOf(chosen->what);
    if (row != nullptr && sim::brokerAlike(*row, chosen->what) && asked.count > 1) {
        // A row of alike things, filled cheapest first and never from his own account.
        std::vector<const sim::Listing*> alike;
        for (const sim::Listing& l : g_book.listings()) {
            if (l.what.item == chosen->what.item && l.account != account && sim::brokerAlike(*row, l.what)) alike.push_back(&l);
        }
        std::sort(alike.begin(), alike.end(), [](const auto* a, const auto* b) { return a->price < b->price; });
        for (size_t i = 0; i < alike.size() && int(i) < asked.count; ++i) ids.push_back(alike[i]->id);
    } else {
        const sim::BrokerNo can = g_book.canBuy(asked.listing, account);
        if (can != sim::BrokerNo::Yes) {
            refuse(one, asked.ask, can, can == sim::BrokerNo::Own ? "That is your own account's." : "Not to be bought.");
            return;
        }
        ids.push_back(asked.listing);
    }
    if (ids.empty()) {
        refuse(one, asked.ask, sim::BrokerNo::Gone, "Someone bought it first.");
        return;
    }
    const uint32_t batch = batchOf(one, asked.ask, asked.what, int(ids.size()), nameOf(chosen->what));
    for (const uint32_t id : ids) {
        Pending p;
        p.op = Pending::Op::Buy;
        p.listing = g_book.take(id);
        p.batch = batch;
        sim::Command pay{.kind = sim::Command::Kind::BrokerBuy};
        sim::packHeld(p.listing.what, pay);
        pay.zen = p.listing.price;
        // A player's into his vault, a bot's into his bag (sim::Realm::brokerBuy).
        if (!one.bot) pay.target |= 0x80000000u;
        queue(one, pay, std::move(p));
    }
}

void collect(Session& one, const net::BrokerAsk& asked) {
    std::vector<sim::Parcel> box;
    while (!g_book.collectOf(one.token).empty()) box.push_back(g_book.takeParcel(one.token, 0));
    if (box.empty()) {
        refuse(one, asked.ask, sim::BrokerNo::Empty, "Nothing waits for you.");
        return;
    }
    int64_t zen = 0;
    int64_t zenAt = 0;
    std::vector<sim::Parcel> things;
    for (const sim::Parcel& p : box) {
        if (!p.what.empty()) {
            things.push_back(p);
            if (p.zen > 0) zen += p.zen;
        } else {
            zen += p.zen;
            if (zenAt == 0) zenAt = p.at;
        }
    }
    const uint32_t batch = batchOf(one, asked.ask, asked.what, int(things.size()) + (zen > 0 ? 1 : 0), "");
    for (const sim::Parcel& p : things) {
        Pending pending;
        pending.op = Pending::Op::Give;
        pending.parcel = p;
        pending.parcel.zen = 0;
        pending.batch = batch;
        sim::Command give{.kind = sim::Command::Kind::BrokerGive};
        sim::packHeld(p.what, give);
        queue(one, give, std::move(pending));
    }
    if (zen > 0) {
        Pending pending;
        pending.op = Pending::Op::Zen;
        pending.parcel = sim::Parcel{sim::Held{}, zen, zenAt != 0 ? zenAt : wallNow(), sim::Parcel::Why::Sold};
        pending.batch = batch;
        queue(one, sim::Command{.kind = sim::Command::Kind::BrokerZen, .zen = zen}, std::move(pending));
    }
}

// A batch's last command answered: one line for his window, and his market as it now stands.
void finish(uint32_t id, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto it = g_batches.find(id);
    if (it == g_batches.end()) return;
    const Batch b = it->second;
    g_batches.erase(it);
    Session* one = sessionOfToken(b.token, sessions);
    if (one == nullptr || !one->welcomed) return;
    // His things moved: written in this pass, with the book (brokerKeep).
    one->keepAt = Clock::time_point{};
    net::BrokerView view;
    view.answered = b.ask;
    switch (b.what) {
        case net::BrokerWhat::List:
            view.no = uint8_t(b.done > 0 ? sim::BrokerNo::Yes : sim::BrokerNo::Empty);
            view.said = b.done > 0 ? b.thing + " is on sale." : "That is no longer there.";
            break;
        case net::BrokerWhat::Buy:
            view.no = uint8_t(b.done > 0 ? sim::BrokerNo::Yes : sim::BrokerNo::Pay);
            if (b.done == 0) {
                view.said = "You cannot pay for it, with your vault's Zen too.";
            } else {
                view.said = "Bought " + (b.done > 1 ? std::to_string(b.done) + " " : std::string()) + b.thing +
                            (b.failed > 0 ? ": you could pay for " + std::to_string(b.done) + " of " +
                                                std::to_string(b.done + b.failed)
                                          : "") +
                            (b.full > 0      ? (b.vault ? ". Your vault and bag are full: it waits in Collect."
                                                        : ". Your bag is full: it waits in Collect.")
                             : b.bagged > 0  ? ". Your vault is full: it went to your bag."
                             : b.vault       ? ", into your vault."
                                             : ".");
            }
            break;
        case net::BrokerWhat::Collect:
            view.no = uint8_t(b.done > 0 ? sim::BrokerNo::Yes : sim::BrokerNo::Empty);
            view.said = b.failed > 0 ? "Your bag is full: the rest waits in Collect."
                                     : (b.zen > 0 ? "Collected " + zenText(b.zen) + " Zen." : "Collected.");
            break;
        default: break;
    }
    mine(view, *one, sessions);
    send(*one, view);
}

void complete(const Pending& p, int answer, std::vector<std::unique_ptr<Session>>& sessions) {
    Batch* batch = g_batches.count(p.batch) ? &g_batches[p.batch] : nullptr;
    const int64_t now = wallNow();
    bool ok = false;
    switch (p.op) {
        case Pending::Op::List:
            ok = answer == 1;
            if (ok) g_book.list(p.listing, p.hours, now);
            break;
        case Pending::Op::Buy:
            ok = answer == 1 || answer == 2 || answer == 3;
            if (!ok) {
                g_book.restore(p.listing);
                break;
            }
            g_book.sold(p.listing, now);
            if (answer == 3 && batch) ++batch->bagged;
            if (answer == 2) {
                g_book.deliver(p.token, sim::Parcel{p.listing.what, 0, now, sim::Parcel::Why::Bought});
                if (batch) ++batch->full;
            }
            // The seller, if he is on: a chat line, as the user chose (2026-10-11).
            if (Session* seller = sessionOfToken(p.listing.seller, sessions); seller != nullptr && seller->welcomed) {
                seller->keepAt = Clock::time_point{};
                chatSystem(*seller, net::Channel::Server,
                           "Your " + nameOf(p.listing.what) + " sold for " + zenText(p.listing.price) +
                               " Zen. Collect it at any Broker.");
                net::BrokerView news;
                news.said = "Your " + nameOf(p.listing.what) + " sold.";
                mine(news, *seller, sessions);
                send(*seller, news);
            }
            break;
        case Pending::Op::Give:
            ok = answer == 1;
            if (!ok) g_book.deliver(p.token, p.parcel);
            break;
        case Pending::Op::Zen: {
            const int64_t given = std::max(0, answer);
            ok = given > 0;
            if (given < p.parcel.zen) {
                sim::Parcel rest = p.parcel;
                rest.zen = p.parcel.zen - given;
                g_book.deliver(p.token, rest);
            }
            if (batch) batch->zen += given;
            break;
        }
    }
    if (batch != nullptr) {
        ++(ok ? batch->done : batch->failed);
        if (--batch->left <= 0) finish(p.batch, sessions);
    }
}

// The query as he asked it, with what only the server knows: whose watches.
sim::BrokerQuery watcherOf(const Session& one, sim::BrokerQuery query) {
    query.watcher = one.token;
    return query;
}

}  // namespace

void brokerOpen() {
    std::vector<uint8_t> bytes;
    if (!g_store.findBook(bytes)) {
        core::logf("broker: a new book");
    } else if (!g_book.read(bytes)) {
        core::logError("broker: the book in characters.db could not be read; a new one");
    } else {
        // A book written with another item list, its things to this server's rows (Store::useItems).
        if (const std::vector<int32_t>* map = g_store.bookRenumber()) {
            g_book.renumber(*map);
            core::logf("broker: the book renumbered to this server's items");
        }
        core::logf("broker: %zu listings", g_book.listings().size());
    }
    g_bookKept = g_book.version();
}

void brokerAsk(Session& one, const net::BrokerAsk& asked, std::vector<std::unique_ptr<Session>>& sessions) {
    if (one.world == nullptr || one.player == 0) return;
    // At a Broker, the user's rule: no market from afar.
    if (!one.world->realm->atBroker(one.player)) {
        refuse(one, asked.ask, sim::BrokerNo::Away, "Speak to a Broker.");
        return;
    }
    const bool moves = asked.what == net::BrokerWhat::List || asked.what == net::BrokerWhat::Buy ||
                       asked.what == net::BrokerWhat::Collect;
    if (moves && busy(one.token)) {
        refuse(one, asked.ask, sim::BrokerNo::Busy, "One moment.");
        return;
    }
    net::BrokerView view;
    view.answered = asked.ask;
    const std::string account = accountOf(one);
    switch (asked.what) {
        case net::BrokerWhat::Look: mine(view, one, sessions); break;
        case net::BrokerWhat::Browse: page(view, watcherOf(one, asked.query), account, sessions); break;
        case net::BrokerWhat::Watch: {
            const sim::Listing* l = g_book.find(asked.listing);
            const bool alike = asked.item >= 0;
            if (!alike && l == nullptr && asked.count != 0) {
                view.no = uint8_t(sim::BrokerNo::Gone);
                view.said = "That listing is gone.";
            } else if (!g_book.watch(one.token, alike ? sim::Book::watchItem(asked.item) : asked.listing,
                                     asked.count != 0)) {
                view.no = uint8_t(sim::BrokerNo::Many);
                view.said = "You watch " + std::to_string(sim::kBrokerWatched) + " things already.";
            }
            page(view, watcherOf(one, asked.query), account, sessions);
            break;
        }
        case net::BrokerWhat::Quote: {
            view.parts |= net::BrokerView::Quoted;
            view.item = asked.item;
            for (const sim::Listing& l : g_book.listings()) {
                if (l.what.item != asked.item) continue;
                ++view.onSale;
                if (view.cheapest == 0 || l.price < view.cheapest) view.cheapest = l.price;
            }
            view.sales = g_book.salesOf(asked.item);
            break;
        }
        case net::BrokerWhat::List: list(one, asked); return;
        case net::BrokerWhat::Buy: buy(one, asked); return;
        case net::BrokerWhat::Collect: collect(one, asked); return;
        case net::BrokerWhat::Cancel: {
            const sim::BrokerNo no = g_book.cancel(asked.listing, one.token, wallNow());
            view.no = uint8_t(no);
            view.said = no == sim::BrokerNo::Yes ? "Cancelled: it waits in Collect." : "That listing is gone.";
            mine(view, one, sessions);
            break;
        }
        case net::BrokerWhat::Lower: {
            const sim::BrokerNo no = g_book.lower(asked.listing, one.token, asked.price);
            view.no = uint8_t(no);
            view.said = no == sim::BrokerNo::Yes ? "Price lowered."
                        : no == sim::BrokerNo::Higher ? "A price can only be lowered."
                        : no == sim::BrokerNo::Low ? "Not under what a merchant pays."
                                                   : "That listing is gone.";
            mine(view, one, sessions);
            break;
        }
    }
    send(one, view);
}

void brokerHeard(const World& world, const std::vector<sim::Command>& commands,
                 const std::vector<sim::Happening>& said, std::vector<std::unique_ptr<Session>>& sessions) {
    if (g_pending.empty()) return;
    // Its own command's answer alone: a ticket, and one of the Broker's four kinds.
    const auto ours = [](int32_t kind) {
        using K = sim::Command::Kind;
        return kind == int32_t(K::BrokerTake) || kind == int32_t(K::BrokerBuy) ||
               kind == int32_t(K::BrokerGive) || kind == int32_t(K::BrokerZen);
    };
    std::vector<Pending> answered;
    for (size_t i = 0; i < g_pending.size();) {
        const Pending& p = g_pending[i];
        const bool inTick = p.world == &world &&
                            std::any_of(commands.begin(), commands.end(), [&](const sim::Command& c) {
                                return c.ticket == p.ticket && ours(int32_t(c.kind));
                            });
        if (!inTick) {
            ++i;
            continue;
        }
        answered.push_back(p);
        g_pending.erase(g_pending.begin() + std::ptrdiff_t(i));
    }
    for (const Pending& p : answered) {
        int answer = -1;  // a command the realm dropped -- he left -- is a no
        for (const sim::Happening& h : said) {
            if (h.what == sim::What::Answered && uint32_t(h.c) == p.ticket && ours(h.a)) answer = h.b;
        }
        complete(p, answer, sessions);
    }
}

void brokerTick(std::vector<std::unique_ptr<Session>>& sessions) {
    // A second of the server's running, or g_speed of them under --speed: listings run out only
    // while it runs (the user, 2026-10-11).
    g_book.expire(g_speed, wallNow());
    // A command whose world never ticked it -- the world gone -- is a no after ten seconds.
    const Clock::time_point now = Clock::now();
    for (size_t i = 0; i < g_pending.size();) {
        if (now - g_pending[i].at < std::chrono::seconds(10)) {
            ++i;
            continue;
        }
        const Pending p = g_pending[i];
        g_pending.erase(g_pending.begin() + std::ptrdiff_t(i));
        complete(p, -1, sessions);
    }
}

void brokerSample(int count) {
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

void brokerKeep() {
    if (g_book.version() == g_bookKept) return;
    if (g_store.keepBook(g_book.write())) g_bookKept = g_book.version();
}

}  // namespace mu::server
