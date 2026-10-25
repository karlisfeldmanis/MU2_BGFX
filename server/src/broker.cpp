// The Broker's service (docs/game/broker.md): the market a Broker opens in Lorencia, Devias and Noria.
// Since 2026-10-21 one market for every realm (docs/game/shared-market.md): this file is the hands,
// on every server, and the book is the keeper's (keeper.cpp), this server's own or another's over
// the line (market_link.cpp). The hands hear a BrokerAsk, check he stands at a Broker, ask the
// keeper, and move his things with the realm's four commands, queued into his world's next tick
// with tickets of their own (kBrokerTickets) and finished when that tick answers them
// (brokerHeard). His character is written in the pass his things moved, and what the keeper must
// hear of it goes only after that (rule 1).
#include "broker.h"

#include "host.h"
#include "keeper.h"
#include "market_link.h"

#include "net/broker_wire.h"
#include "sim/broker.h"
#include "sim/vault.h"
#include "sim/wear.h"

namespace mu::server {

namespace {

// The Broker's tickets, the server's own and apart from a Join's (kJoinTickets).
constexpr uint32_t kBrokerTickets = 0xC0000000u;
// An ask the keeper has not answered in this long is given up (a late answer is put right:
// answered() settles a hold nobody waits for).
constexpr auto kKeeperWithin = std::chrono::seconds(30);

uint32_t g_nextTicket = kBrokerTickets;

// One realm command queued and not yet answered.
struct Pending {
    enum class Op : uint8_t { List, Buy, Give, Zen } op = Op::List;
    const World* world = nullptr;
    uint32_t ticket = 0;
    uint64_t ask = 0;      // the Waiting it is part of
    uint64_t token = 0;
    size_t index = 0;      // Buy: which of the held listings
    sim::Parcel parcel;    // Give and Zen: what was taken out of his Collect
    Clock::time_point at = Clock::now();
};
std::vector<Pending> g_pending;

// An ask of his to the keeper, and the realm commands between its two halves -- a buy of five
// Blesses, a Collect -- by the serial of the keeper's ask it waits on.
struct Waiting {
    MarketOp op = MarketOp::View;
    Trader who;
    net::BrokerAsk asked;
    bool atKeeper = true;  // sent and not yet answered
    uint64_t hold = 0;     // Settle, Return: the Take's or TakeBox's serial
    Clock::time_point at = Clock::now();
    // List
    int64_t floor = 0;
    sim::Listing listing;
    // the realm's part
    int left = 0, done = 0, failed = 0, full = 0, bagged = 0;
    bool vault = false;  // a player's buy, into his vault
    int64_t zen = 0;
    std::string thing;
    std::vector<sim::Listing> listings;  // Take: held for him
    std::vector<int32_t> answers;        // Take: the realm's answer for each
    std::vector<sim::Parcel> back;       // TakeBox: what did not reach him
};
std::map<uint64_t, Waiting> g_waiting;

std::string accountOf(const Session& one) {
    if (!one.account.empty()) return one.account;
    char key[24];
    std::snprintf(key, sizeof key, "#%016llx", (unsigned long long)one.token);
    return key;
}

Trader traderOf(const Session& one) { return Trader{one.token, accountOf(one), one.name, one.bot, g_region}; }

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
    return std::any_of(g_pending.begin(), g_pending.end(), [&](const Pending& p) { return p.token == token; }) ||
           std::any_of(g_waiting.begin(), g_waiting.end(), [&](const auto& w) {
               return w.second.who.token == token && w.second.op != MarketOp::View;
           });
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

// He is here to be answered and to have his things moved: on his line, in a world.
Session* here(uint64_t token, std::vector<std::unique_ptr<Session>>& sessions) {
    Session* one = sessionOfToken(token, sessions);
    return one != nullptr && one->welcomed && one->world != nullptr && one->player != 0 && !one->sentAway ? one : nullptr;
}

void answered(const MarketAnswer& answer, std::vector<std::unique_ptr<Session>>& sessions);

// An ask to the keeper: this server's own, answered at once, or over the line.
void ask(MarketAsk asked, Waiting waiting, std::vector<std::unique_ptr<Session>>& sessions) {
    asked.serial = marketSerial();
    asked.who = waiting.who;
    asked.ask = waiting.asked;
    waiting.op = asked.op;
    waiting.atKeeper = true;
    waiting.at = Clock::now();
    if (!marketKeeperHere() && !durable(asked.op) && !marketOpen()) {
        if (Session* one = here(waiting.who.token, sessions)) {
            refuse(*one, waiting.asked.ask, sim::BrokerNo::Closed, "The market is closed for a moment.");
        }
        return;
    }
    g_waiting[asked.serial] = std::move(waiting);
    if (marketKeeperHere()) {
        answered(keeperServe(asked, MarketSource{g_region, ""}, sessions), sessions);
    } else {
        marketSend(asked);
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

// The slot he lists from, if it still holds what he saw.
const sim::Held* listed(const Session& one, const net::BrokerAsk& asked) {
    const sim::Realm& realm = *one.world->realm;
    const sim::Held* there = nullptr;
    if (asked.fromVault) {
        const sim::Vault* vault = realm.vaultOf(one.player);
        if (vault != nullptr && asked.slot >= 0 && asked.slot < sim::kVaultCells) there = &(*vault)[asked.slot];
    } else {
        const sim::Satchel* bag = realm.satchelOf(one.player);
        if (bag != nullptr && sim::baggable(asked.slot)) there = &(*bag)[asked.slot];
    }
    return there == nullptr || there->empty() || !same(*there, asked.seen) ? nullptr : there;
}

// How many of a stack he lists (the ask's count), 0 for the whole of it or of what never stacks.
int piecesOf(const Session& one, const sim::Held& there, const net::BrokerAsk& asked) {
    const content::Tables& tables = one.world->tables;
    if (size_t(there.item) >= tables.items.size()) return 0;
    const content::ItemRow& row = tables.items[size_t(there.item)];
    if (!sim::stacks(row) || sim::ammunition(row)) return 0;
    return asked.count > 0 && asked.count < there.durability ? asked.count : 0;
}

// What goes up: the thing, or that many of the stack.
sim::Held partOf(const Session& one, const sim::Held& there, const net::BrokerAsk& asked) {
    sim::Held part = there;
    if (const int pieces = piecesOf(one, there, asked); pieces > 0) part.durability = int16_t(pieces);
    return part;
}

void list(Session& one, const net::BrokerAsk& asked, std::vector<std::unique_ptr<Session>>& sessions) {
    const sim::Held* there = listed(one, asked);
    if (there == nullptr) {
        refuse(one, asked.ask, sim::BrokerNo::Empty, "That is no longer there.");
        return;
    }
    const sim::BrokerNo kind = sim::brokerable(one.world->tables, *there);
    if (kind != sim::BrokerNo::Yes) {
        refuse(one, asked.ask, kind, "The Broker will not take that.");
        return;
    }
    Waiting w;
    w.who = traderOf(one);
    w.asked = asked;
    w.floor = std::max<int64_t>(1, sim::npcValue(one.world->tables, partOf(one, *there, asked)));
    w.thing = nameOf(partOf(one, *there, asked));
    MarketAsk may{.op = MarketOp::MayList};
    may.floor = w.floor;
    ask(std::move(may), std::move(w), sessions);
}

// The keeper said he may: out of his bag, then the listing up (complete).
void take(Session& one, uint64_t serial, Waiting& w) {
    const sim::Held* there = listed(one, w.asked);
    if (there == nullptr) {
        refuse(one, w.asked.ask, sim::BrokerNo::Empty, "That is no longer there.");
        g_waiting.erase(serial);
        return;
    }
    w.listing.seller = one.token;
    w.listing.account = w.who.account;
    w.listing.name = one.name;
    w.listing.what = partOf(one, *there, w.asked);
    w.listing.price = w.asked.price;
    w.listing.floor = w.floor;
    w.left = 1;
    Pending p;
    p.op = Pending::Op::List;
    p.ask = serial;
    sim::Command command{.kind = sim::Command::Kind::BrokerTake};
    sim::packHeld(*there, command);
    command.zen = w.asked.slot | (w.asked.fromVault ? 0x10000 : 0) |
                  int64_t(piecesOf(one, *there, w.asked)) << 32;
    queue(one, command, std::move(p));
}

void buy(Session& one, uint64_t serial, Waiting& w) {
    w.left = int(w.listings.size());
    w.answers.assign(w.listings.size(), -1);
    w.vault = !one.bot;
    w.thing = nameOf(w.listings.front().what);
    for (size_t i = 0; i < w.listings.size(); ++i) {
        Pending p;
        p.op = Pending::Op::Buy;
        p.ask = serial;
        p.index = i;
        sim::Command pay{.kind = sim::Command::Kind::BrokerBuy};
        sim::packHeld(w.listings[i].what, pay);
        pay.zen = w.listings[i].price;
        // A player's into his vault, a bot's into his bag (sim::Realm::brokerBuy).
        if (!one.bot) pay.target |= 0x80000000u;
        queue(one, pay, std::move(p));
    }
}

void collect(Session& one, uint64_t serial, Waiting& w, const std::vector<sim::Parcel>& box) {
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
    w.left = int(things.size()) + (zen > 0 ? 1 : 0);
    for (const sim::Parcel& p : things) {
        Pending pending;
        pending.op = Pending::Op::Give;
        pending.parcel = p;
        pending.parcel.zen = 0;
        pending.ask = serial;
        sim::Command give{.kind = sim::Command::Kind::BrokerGive};
        sim::packHeld(p.what, give);
        queue(one, give, std::move(pending));
    }
    if (zen > 0) {
        Pending pending;
        pending.op = Pending::Op::Zen;
        pending.parcel = sim::Parcel{sim::Held{}, zen, zenAt != 0 ? zenAt : wallNow(), sim::Parcel::Why::Sold};
        pending.ask = serial;
        queue(one, sim::Command{.kind = sim::Command::Kind::BrokerZen, .zen = zen}, std::move(pending));
    }
}

// The second half: what the realm did, said to the keeper once it is written (rule 1).
void settle(uint64_t serial, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto it = g_waiting.find(serial);
    if (it == g_waiting.end()) return;
    Waiting w = std::move(it->second);
    g_waiting.erase(it);
    MarketAsk second;
    second.hold = serial;
    w.hold = serial;
    if (w.op == MarketOp::Take) {
        second.op = MarketOp::Settle;
        second.answers = w.answers;
    } else {
        second.op = MarketOp::Return;
        second.parcels = w.back;
    }
    ask(std::move(second), std::move(w), sessions);
}

// The last word: one line for his window, and his market as it now stands.
void finish(const Waiting& w, const MarketAnswer& answer, std::vector<std::unique_ptr<Session>>& sessions) {
    Session* one = here(w.who.token, sessions);
    if (one == nullptr) return;
    net::BrokerView view = answer.view;
    view.answered = w.asked.ask;
    switch (w.op) {
        case MarketOp::List: {
            const auto no = sim::BrokerNo(answer.no);
            view.no = answer.no;
            view.said = no == sim::BrokerNo::Yes  ? w.thing + " is on sale."
                        : no == sim::BrokerNo::Full ? "Your account has 20 listings up: it waits in Collect."
                                                    : "The Broker would not take it: it waits in Collect.";
            break;
        }
        case MarketOp::Settle:
            view.no = uint8_t(w.done > 0 ? sim::BrokerNo::Yes : sim::BrokerNo::Pay);
            if (w.done == 0) {
                view.said = "You cannot pay for it, with your vault's Zen too.";
            } else {
                view.said = "Bought " + (w.done > 1 ? std::to_string(w.done) + " " : std::string()) + w.thing +
                            (w.failed > 0 ? ": you could pay for " + std::to_string(w.done) + " of " +
                                                std::to_string(w.done + w.failed)
                                          : "") +
                            (w.full > 0      ? (w.vault ? ". Your vault and bag are full: it waits in Collect."
                                                        : ". Your bag is full: it waits in Collect.")
                             : w.bagged > 0  ? ". Your vault is full: it went to your bag."
                             : w.vault       ? ", into your vault."
                                             : ".");
            }
            break;
        case MarketOp::Return:
            view.no = uint8_t(w.done > 0 ? sim::BrokerNo::Yes : sim::BrokerNo::Empty);
            view.said = w.failed > 0 ? "Your bag is full: the rest waits in Collect."
                                     : (w.zen > 0 ? "Collected " + zenText(w.zen) + " Zen." : "Collected.");
            break;
        default: break;
    }
    send(*one, view);
}

void answered(const MarketAnswer& answer, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto it = g_waiting.find(answer.serial);
    if (it == g_waiting.end()) {
        // Given up on (kKeeperWithin) and answered after all: what it holds goes back at once.
        if (!answer.listings.empty()) {
            MarketAsk back{.op = MarketOp::Settle, .serial = marketSerial(), .hold = answer.serial};
            back.answers.assign(answer.listings.size(), -1);
            if (marketKeeperHere()) keeperServe(back, MarketSource{g_region, ""}, sessions); else marketSend(back);
        } else if (!answer.parcels.empty()) {
            MarketAsk back{.op = MarketOp::Return, .serial = marketSerial(), .hold = answer.serial};
            back.parcels = answer.parcels;
            if (marketKeeperHere()) keeperServe(back, MarketSource{g_region, ""}, sessions); else marketSend(back);
        }
        return;
    }
    Waiting& w = it->second;
    w.atKeeper = false;
    Session* one = here(w.who.token, sessions);
    const auto no = sim::BrokerNo(answer.no);
    switch (w.op) {
        case MarketOp::View:
            if (one != nullptr) send(*one, answer.view);
            g_waiting.erase(it);
            break;
        case MarketOp::MayList:
            if (one == nullptr || no != sim::BrokerNo::Yes) {
                if (one != nullptr) {
                    refuse(*one, w.asked.ask, no,
                           no == sim::BrokerNo::Low    ? "Under what a merchant pays: " + zenText(w.floor) + "."
                           : no == sim::BrokerNo::Full ? "Your account has 20 listings up."
                                                       : "Not at that price.");
                }
                g_waiting.erase(it);
                break;
            }
            take(*one, answer.serial, w);
            break;
        case MarketOp::Take:
            if (no != sim::BrokerNo::Yes || answer.listings.empty()) {
                if (one != nullptr) {
                    refuse(*one, w.asked.ask, no == sim::BrokerNo::Yes ? sim::BrokerNo::Gone : no,
                           no == sim::BrokerNo::Own ? "That is your own account's." : "Someone bought it first.");
                }
                g_waiting.erase(it);
                break;
            }
            w.listings = answer.listings;
            if (one == nullptr) {
                // Gone before he could pay: every listing back.
                w.answers.assign(w.listings.size(), -1);
                settle(answer.serial, sessions);
                break;
            }
            buy(*one, answer.serial, w);
            break;
        case MarketOp::TakeBox:
            if (no != sim::BrokerNo::Yes || answer.parcels.empty()) {
                if (one != nullptr) refuse(*one, w.asked.ask, sim::BrokerNo::Empty, "Nothing waits for you.");
                g_waiting.erase(it);
                break;
            }
            if (one == nullptr) {
                w.back = answer.parcels;
                settle(answer.serial, sessions);
                break;
            }
            collect(*one, answer.serial, w, answer.parcels);
            break;
        case MarketOp::List:
        case MarketOp::Settle:
        case MarketOp::Return: {
            const Waiting done = std::move(w);
            g_waiting.erase(it);
            finish(done, answer, sessions);
            break;
        }
        case MarketOp::Merge: g_waiting.erase(it); break;
    }
}

void complete(const Pending& p, int answer, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto it = g_waiting.find(p.ask);
    if (it == g_waiting.end()) return;
    Waiting& w = it->second;
    bool ok = false;
    switch (p.op) {
        case Pending::Op::List: ok = answer == 1; break;
        case Pending::Op::Buy:
            ok = answer == 1 || answer == 2 || answer == 3;
            if (p.index < w.answers.size()) w.answers[p.index] = answer;
            if (answer == 3) ++w.bagged;
            if (answer == 2) ++w.full;
            break;
        case Pending::Op::Give:
            ok = answer == 1;
            if (!ok) w.back.push_back(p.parcel);
            break;
        case Pending::Op::Zen: {
            const int64_t given = std::max(0, answer);
            ok = given > 0;
            if (given < p.parcel.zen) {
                sim::Parcel rest = p.parcel;
                rest.zen = p.parcel.zen - given;
                w.back.push_back(rest);
            }
            w.zen += given;
            break;
        }
    }
    // His things moved: written in this pass, and only then said to the keeper (rule 1).
    if (ok) {
        if (Session* one = sessionOfToken(p.token, sessions)) one->keepAt = Clock::time_point{};
    }
    ++(ok ? w.done : w.failed);
    if (--w.left > 0) return;
    if (p.op == Pending::Op::List) {
        if (!ok) {
            if (Session* one = here(w.who.token, sessions)) {
                refuse(*one, w.asked.ask, sim::BrokerNo::Empty, "That is no longer there.");
            }
            g_waiting.erase(it);
            return;
        }
        Waiting moved = std::move(w);
        g_waiting.erase(it);
        MarketAsk up{.op = MarketOp::List};
        up.listing = moved.listing;
        ask(std::move(up), std::move(moved), sessions);
        return;
    }
    settle(p.ask, sessions);
}

}  // namespace

void brokerOpen() {
    if (!marketKeeperHere()) marketLoad();
    keeperOpen();
    if (!marketKeeperHere()) keeperHandOver();
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
    Waiting w;
    w.who = traderOf(one);
    w.asked = asked;
    switch (asked.what) {
        case net::BrokerWhat::List: list(one, asked, sessions); return;
        case net::BrokerWhat::Buy: ask(MarketAsk{.op = MarketOp::Take}, std::move(w), sessions); return;
        case net::BrokerWhat::Collect: ask(MarketAsk{.op = MarketOp::TakeBox}, std::move(w), sessions); return;
        default: ask(MarketAsk{.op = MarketOp::View}, std::move(w), sessions); return;
    }
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

void brokerAnswered(const MarketAnswer& answer, std::vector<std::unique_ptr<Session>>& sessions) {
    answered(answer, sessions);
}

void brokerNews(const MarketNews& news, std::vector<std::unique_ptr<Session>>& sessions) {
    Session* seller = sessionOfToken(news.token, sessions);
    if (seller == nullptr || !seller->welcomed) return;
    seller->keepAt = Clock::time_point{};
    chatSystem(*seller, net::Channel::Server, news.chat);
    send(*seller, news.view);
}

void brokerLost(std::vector<std::unique_ptr<Session>>& sessions) {
    // The line went: what it was answering will not be answered. Its holds go back when it comes
    // again (rule 4); the durable asks wait in the outbox, and their askers with them.
    for (auto it = g_waiting.begin(); it != g_waiting.end();) {
        if (!it->second.atKeeper || durable(it->second.op)) {
            ++it;
            continue;
        }
        if (Session* one = here(it->second.who.token, sessions)) {
            refuse(*one, it->second.asked.ask, sim::BrokerNo::Closed, "The market is closed for a moment.");
        }
        it = g_waiting.erase(it);
    }
}

void brokerTick(std::vector<std::unique_ptr<Session>>& sessions) {
    // A second of the server's running, or g_speed of them under --speed: listings run out only
    // while it runs (the user, 2026-10-11).
    if (marketKeeperHere()) keeperTick(g_speed);
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
    // And an ask the keeper never answered, given up: its hold is put back if it comes (answered).
    for (auto it = g_waiting.begin(); it != g_waiting.end();) {
        if (!it->second.atKeeper || durable(it->second.op) || now - it->second.at < kKeeperWithin) {
            ++it;
            continue;
        }
        if (Session* one = here(it->second.who.token, sessions)) {
            refuse(*one, it->second.asked.ask, sim::BrokerNo::Closed, "The market did not answer.");
        }
        it = g_waiting.erase(it);
    }
}

void brokerSample(int count) {
    if (!marketKeeperHere()) {
        core::logError("broker: --market-sample on a server that is not the keeper: no samples");
        return;
    }
    keeperSample(count);
}

bool brokerHolds(uint64_t token) {
    if (marketKeeperHere()) return keeperHolds(token);
    return marketHolder(token) ||
           std::any_of(g_waiting.begin(), g_waiting.end(), [&](const auto& w) { return w.second.who.token == token; });
}

std::vector<uint64_t> brokerLive() {
    // A hold the realm is still moving things for, and one whose Settle or Return is written but
    // not yet answered: one sent already settled it before Live is read, one not yet sent will be.
    std::vector<uint64_t> live;
    for (const auto& [serial, w] : g_waiting) {
        if ((w.op == MarketOp::Take || w.op == MarketOp::TakeBox) && !w.atKeeper) live.push_back(serial);
        if ((w.op == MarketOp::Settle || w.op == MarketOp::Return) && w.hold != 0) live.push_back(w.hold);
    }
    return live;
}

void brokerKeep() {
    if (marketKeeperHere()) keeperKeep(); else marketKeep();
    // Written: now what waited on the disk may go (rules 1 and 2).
    marketSaved();
}

}  // namespace mu::server
