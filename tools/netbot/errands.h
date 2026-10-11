// **The Broker's errands** (docs/broker.md, phase 4; the user, 2026-10-11: players and bots alike
// use the market, and a bot may price up to 5% under the cheapest). Now and then a bot whose map
// has a Broker walks to him, takes in his Collect, lists what he would otherwise sell at the
// counter (Bot::spares, at most kListed up at once) and buys the best upgrade on sale that he
// can pay for (Bot::worth), through the same BrokerAsk a player's window sends.
//
// Included inside netbot.cpp's namespace after NetBot, beside the Market (market.h); not a header
// to include anywhere else.
#pragma once

class Errands {
public:
    void tick(std::vector<std::unique_ptr<NetBot>>& bots) {
        for (auto& bp : bots) {
            NetBot& bot = *bp;
            const auto on = on_.find(bot.name());
            if (!bot.up()) {
                // Gone to another map (a new line): let him go, or he walks to this map's spot there.
                if (on != on_.end()) on_.erase(on), bot.errand(-1, -1);
                continue;
            }
            // Every answer is taken, an errand's or not: news of a sale would otherwise pile up.
            for (net::BrokerView view; bot.takeBrokerView(view);) {
                if (on != on_.end() && view.answered != 0 && view.answered == on->second.waiting) {
                    heard(bot, on->second, view);
                }
            }
            if (on != on_.end()) {
                step(bot, on->second);
                if (on->second.step == Step::Done) {
                    bot.errand(-1, -1);
                    next_[bot.name()] = bot.clockNow() + (on->second.did ? kEvery : kEvery * 3);
                    on_.erase(on);
                }
                continue;
            }
            start(bot);
        }
    }

private:
    enum class Step : uint8_t { Walk, Look, Collect, Quote, List, Browse, Buy, Done };
    struct Errand {
        Step step = Step::Walk;
        uint32_t waiting = 0;   // the ask whose answer moves it on
        std::chrono::steady_clock::time_point until;  // given up after: the wall's, as the
                                                       // server answers in the wall's time
        std::vector<int> spares;
        size_t spare = 0;       // the one being priced
        sim::Held quoted;
        int64_t price = 0;
        int page = 0;
        bool did = false;       // something listed, bought or collected
    };
    static constexpr int64_t kEvery = 20 * 60 * 20;  // ticks: twenty game minutes between visits
    static constexpr auto kGiveUp = std::chrono::seconds(30);  // to get there and be done
    static constexpr int kListed = 3;                // a bot's listings up at once
    static constexpr int kPages = 2;                 // of upgrades looked through
    static constexpr int64_t kLeast = 100000;        // Zen worth a trip to buy with

    // Off on an errand if it is time, there is a Broker on his map and something to do there.
    void start(NetBot& bot) {
        if (bot.driven() || bot.swapping() || !bot.alive() || bot.inCastle() || bot.inSquare() || !bot.tables()) return;
        if (const auto n = next_.find(bot.name()); n != next_.end() && bot.clockNow() < n->second) return;
        next_[bot.name()] = bot.clockNow() + kEvery;
        // Nothing to sell and too little to buy with: no errand.
        if (bot.spares().empty() && bot.money() < kLeast) return;
        int column = -1, row = -1;
        for (const content::Townsperson& one : bot.tables()->folk) {
            if (one.number != sim::kBroker) continue;
            // An open tile beside him: the brain stops within a step of it, and from any of those
            // he is inside the counter's reach (sim::kCounter, 3).
            static constexpr int kNear[8][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
            for (const auto& d : kNear) {
                if (column < 0 && bot.tables()->grid.open(one.x + d[0], one.y + d[1])) {
                    column = one.x + d[0];
                    row = one.y + d[1];
                }
            }
        }
        if (column < 0) return;
        std::printf("  [broker] %s goes to the Broker with %lld Zen\n", bot.name().c_str(), (long long)bot.money());
        Errand e;
        e.until = std::chrono::steady_clock::now() + kGiveUp;
        e.spares = bot.spares();
        bot.errand(column, row);
        on_[bot.name()] = std::move(e);
    }

    void ask(NetBot& bot, Errand& e, net::BrokerAsk asked) {
        asked.ask = nextAsk_++;
        e.waiting = asked.ask;
        bot.askBroker(asked);
    }

    void step(NetBot& bot, Errand& e) {
        if (std::chrono::steady_clock::now() > e.until || !bot.alive()) {
            std::printf("  [broker] %s gave up %s\n", bot.name().c_str(),
                        e.step == Step::Walk ? "on the way" : "waiting on the Broker");
            e.step = Step::Done;
            return;
        }
        if (e.step == Step::Walk && bot.atBroker()) {
            e.step = Step::Look;
            ask(bot, e, {.what = net::BrokerWhat::Look});
        }
    }

    // The next spare to price, or on to the upgrades.
    void nextSpare(NetBot& bot, Errand& e, int up) {
        while (e.spare < e.spares.size() && up < kListed) {
            const sim::Held what = bot.held(e.spares[e.spare]);
            if (!what.empty() && sim::brokerable(*bot.tables(), what) == sim::BrokerNo::Yes) {
                e.quoted = what;
                e.step = Step::Quote;
                ask(bot, e, {.what = net::BrokerWhat::Quote, .item = what.item});
                return;
            }
            ++e.spare;
        }
        browse(bot, e);
    }

    void browse(NetBot& bot, Errand& e) {
        e.step = Step::Browse;
        net::BrokerAsk asked{.what = net::BrokerWhat::Browse};
        asked.query.usableBy = int(bot.kin());
        asked.query.page = e.page;
        ask(bot, e, asked);
    }

    void heard(NetBot& bot, Errand& e, const net::BrokerView& view) {
        e.waiting = 0;
        const content::Tables& tables = *bot.tables();
        const bool asking = e.step == Step::Look || e.step == Step::Quote || e.step == Step::Browse;
        if (asking && view.no != uint8_t(sim::BrokerNo::Yes)) {
            std::printf("  [broker] %s: %s\n", bot.name().c_str(), view.said.c_str());
            e.step = Step::Done;
            return;
        }
        switch (e.step) {
            case Step::Look:
                if (!view.box.empty()) {
                    e.step = Step::Collect;
                    ask(bot, e, {.what = net::BrokerWhat::Collect});
                    return;
                }
                listed_[bot.name()] = int(view.mine.size());
                nextSpare(bot, e, int(view.mine.size()));
                return;
            case Step::Collect:
                if (view.no == uint8_t(sim::BrokerNo::Yes)) e.did = true;
                std::printf("  [broker] %s collects: %s\n", bot.name().c_str(), view.said.c_str());
                listed_[bot.name()] = int(view.mine.size());
                nextSpare(bot, e, int(view.mine.size()));
                return;
            case Step::Quote: {
                // Up to 5% under the cheapest (the user's), what has sold, or four times the NPC's
                // as a window starts a player; never under what a merchant pays.
                const int64_t npc = std::max<int64_t>(1, sim::npcValue(tables, e.quoted));
                int64_t price = view.cheapest > 0 ? view.cheapest * (100 - int64_t(nameHash(bot.name()) % 6)) / 100
                                : !view.sales.empty() ? view.sales.front().price
                                                      : 4 * npc;
                e.price = std::clamp<int64_t>(price, npc, sim::kBrokerMostPrice);
                e.step = Step::List;
                ask(bot, e, {.what = net::BrokerWhat::List, .slot = e.spares[e.spare], .seen = e.quoted,
                             .price = e.price, .hours = 24});
                return;
            }
            case Step::List: {
                const std::string label = sim::linkLabel(tables.items[size_t(e.quoted.item)], e.quoted);
                if (view.no == uint8_t(sim::BrokerNo::Yes)) {
                    e.did = true;
                    ++listed_[bot.name()];
                    std::printf("  [broker] %s lists %s at %lld\n", bot.name().c_str(), label.c_str(), (long long)e.price);
                } else {
                    std::printf("  [broker] %s could not list %s: %s\n", bot.name().c_str(), label.c_str(), view.said.c_str());
                }
                ++e.spare;
                nextSpare(bot, e, listed_[bot.name()]);
                return;
            }
            case Step::Browse: {
                // The best upgrade he can pay for and keep a third of his Zen: the most gain, the
                // cheaper of two alike.
                const int64_t budget = bot.money() * 2 / 3;
                const net::BrokerRow* best = nullptr;
                double bestGain = 0.0;
                for (const net::BrokerRow& row : view.rows) {
                    if ((row.flags & net::BrokerRow::Own) || row.price > budget) continue;
                    const double gain = bot.worth(row.what);
                    if (gain > bestGain || (best && gain == bestGain && row.price < best->price)) {
                        best = &row;
                        bestGain = gain;
                    }
                }
                if (best && bestGain > 0.0) {
                    e.quoted = best->what;
                    e.price = best->price;
                    e.step = Step::Buy;
                    std::printf("  [broker] %s buys %s from %s at %lld (+%.0f)\n", bot.name().c_str(),
                                sim::linkLabel(tables.items[size_t(best->what.item)], best->what).c_str(),
                                best->seller.c_str(), (long long)best->price, bestGain);
                    ask(bot, e, {.what = net::BrokerWhat::Buy, .listing = best->id, .count = 1});
                    return;
                }
                std::printf("  [broker] %s finds no upgrade he can pay for on page %d of %u listings\n",
                            bot.name().c_str(), e.page + 1, view.total);
                const int pages = int((view.total + sim::kBrokerPage - 1) / sim::kBrokerPage);
                if (++e.page < std::min(pages, kPages)) {
                    browse(bot, e);
                    return;
                }
                e.step = Step::Done;
                return;
            }
            case Step::Buy:
                if (view.no == uint8_t(sim::BrokerNo::Yes)) e.did = true;
                else std::printf("  [broker] %s's buy fell through: %s\n", bot.name().c_str(), view.said.c_str());
                e.step = Step::Done;
                return;
            default: return;
        }
    }

    // How far under the cheapest he goes, 0-5%: his own, so two bots do not undercut each other
    // by the same step every visit.
    static uint32_t nameHash(const std::string& name) {
        uint32_t h = 2166136261u;
        for (const char c : name) h = (h ^ uint8_t(c)) * 16777619u;
        return h;
    }

    std::map<std::string, Errand> on_;
    std::map<std::string, int64_t> next_;  // his clock: not before
    std::map<std::string, int> listed_;    // his listings up, as the last Look said
    uint32_t nextAsk_ = 1;
};
