// **The Broker's errands** (docs/game/broker.md, phase 4; the user, 2026-10-11: players and bots alike
// use the market, and a bot may price up to 5% under the cheapest). Now and then a bot whose map
// has a Broker walks to him, takes in his Collect, lists what he would otherwise sell at the
// counter (Bot::spares, at most kListed up at once) and buys the best upgrade on sale that he
// can pay for (Bot::worth), through the same BrokerAsk a player's window sends.
//
// The bots' only trading (the user, 2026-10-12: "we need to teach our bots use marketplace, and get
// rid of chat trades"): the face-to-face swaps of b8547af5, named on the map's chat, are gone.
//
// Included inside netbot.cpp's namespace after NetBot; not a header to include anywhere else.
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
                    const int64_t every = bot.trader() ? kEvery / 4 : kEvery;
                    next_[bot.name()] = bot.clockNow() + (on->second.did ? every : every * 3);
                    on_.erase(on);
                }
                continue;
            }
            start(bot);
        }
    }

private:
    enum class Step : uint8_t { Walk, Look, Collect, Quote, List, Browse, Buy, Lower, Done };
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
        std::vector<uint32_t> lowered;  // his listings asked down this visit (Step::Lower)
        // The search: each kind of thing in turn, every page within his budget, and what each
        // page offered him (Pick), the best for his Zen bought at the end.
        std::vector<int> kinds;
        size_t kind = 0;
        int64_t budget = 0;
        struct Pick {
            uint32_t listing = 0;
            sim::Held what;
            int64_t price = 0;
            double gain = 0.0;
            std::string seller;
        };
        std::vector<Pick> picks;
        std::vector<Pick> flips;  // a trader's: underpriced, for listing again
        int pages = 0;          // looked through, all kinds
        int bought = 0;         // this visit
        int missed = 0;         // buys that fell through this visit, each searched again
        int partsUp = 0;        // event ticket halves he has listed now
    };
    static constexpr int64_t kEvery = 20 * 60 * 20;  // ticks: twenty game minutes between visits
    static constexpr auto kGiveUp = std::chrono::seconds(60);  // to get there and be done
    static constexpr int kListed = 20;               // a bot's listings up at once, a player's
                                                     // (the user, 2026-10-11: "same as real player")
    static constexpr int kHold = 8;                  // spares kept from the blacksmith for them
    static constexpr int kPages = 6;                 // of each kind looked through, at most
    static constexpr int kBuys = 3;                  // things bought in one visit, at most
    static constexpr int kMisses = 2;                // buys fallen through, searched again, at most
    static constexpr int kPartsUp = 2;               // ticket halves listed at once, the rest the counter's
    static constexpr int64_t kLeast = 10000;         // Zen worth a trip to buy with
    static constexpr int kCollectRoom = 8;           // free bag cells to take in his Collect

    // Off on an errand if it is time, there is a Broker on his map and something to do there.
    void start(NetBot& bot) {
        if (bot.driven() || !bot.alive() || bot.inCastle() || bot.inSquare() || !bot.tables() || bot.guildBusy()) return;
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
        if (column < 0) return;  // no Broker here: a trip begun waits for the town that has one
        // **The Broker first on every town trip** (the user, 2026-10-12: "how we can improve bots
        // so they really use market to get better"): a trip in a Broker's town sends him to the
        // market before the counters, whose shelves he then tries against what he bought.
        const bool tripped = bot.trips() != trips_[bot.name()];
        trips_[bot.name()] = bot.trips();
        if (!tripped) {
            if (const auto n = next_.find(bot.name()); n != next_.end() && bot.clockNow() < n->second) return;
        }
        next_[bot.name()] = bot.clockNow() + (bot.trader() ? kEvery / 4 : kEvery);
        // Nothing to sell and too little to buy with: no errand.
        if (bot.spares().empty() && bot.money() < kLeast) return;
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
            // **Two halves up at a time** (the user, 2026-10-12: "bot are selling to much BC,DS
            // ingreadients"): 74 of a run's 112 listings were Scrolls and Bones +1.
            if (!what.empty() && part(*bot.tables(), what) && e.partsUp >= kPartsUp) {
                ++e.spare;
                continue;
            }
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

    // **As a player searches** (the user, 2026-10-12): not the two cheapest pages of all he may
    // use -- 24 Short Swords and Pad Boots, the Spirit Boots +8 on page six never seen -- but each
    // kind of thing he wears or sets, cheapest first, until the prices pass what he would pay.
    void browse(NetBot& bot, Errand& e) {
        if (e.kinds.empty()) {
            using K = sim::BrokerKind;
            for (const K k : {K::Swords, K::Bows, K::Staves, K::Shields, K::Armour, K::Jewellery, K::Jewels, K::Runes,
                              K::Pets, K::Wings, K::Tickets, K::Skills}) {
                e.kinds.push_back(int(k));
            }
            e.budget = bot.trader() ? bot.money() * 9 / 10 : bot.money() * 2 / 3;
        }
        if (e.kind >= e.kinds.size()) {
            decide(bot, e);
            return;
        }
        e.step = Step::Browse;
        net::BrokerAsk asked{.what = net::BrokerWhat::Browse};
        const auto kind = sim::BrokerKind(e.kinds[e.kind]);
        asked.query.kind = int(kind);
        // A jewel, rune, pet or mount is anyone's; gear and wings his class's.
        if (kind != sim::BrokerKind::Jewels && kind != sim::BrokerKind::Runes && kind != sim::BrokerKind::Pets &&
            kind != sim::BrokerKind::Tickets && kind != sim::BrokerKind::Skills) {
            asked.query.usableBy = int(bot.kin());
        }
        asked.query.page = e.page;
        ask(bot, e, asked);
    }

    // The best for his Zen of all he was offered: the most gain a Zen, among those worth a third
    // of the biggest gain at least, so a trinket's 1 does not beat armour.
    void decide(NetBot& bot, Errand& e) {
        double most = 0.0;
        for (const Errand::Pick& p : e.picks) most = std::max(most, p.gain);
        const Errand::Pick* best = nullptr;
        double bestRatio = 0.0;
        for (const Errand::Pick& p : e.picks) {
            if (p.gain < most / 3.0) continue;
            const double ratio = p.gain / double(std::max<int64_t>(1, p.price));
            if (ratio > bestRatio) best = &p, bestRatio = ratio;
        }
        // A trader flips first while he has no upgrade worth half his budget.
        if ((!best || best->price * 2 > e.budget) && bot.trader() && !e.flips.empty()) {
            const Errand::Pick* flip = nullptr;
            double margin = 0.0;
            for (const Errand::Pick& p : e.flips) {
                const int64_t npc = std::max<int64_t>(1, sim::npcValue(*bot.tables(), p.what));
                const double m = double(askingPrice(p.what, npc)) / double(std::max<int64_t>(1, p.price));
                if (m > margin) flip = &p, margin = m;
            }
            if (flip) {
                bought_[bot.name()].push_back({flip->what, flip->price});
                e.quoted = flip->what;
                e.price = flip->price;
                e.step = Step::Buy;
                std::printf("  [broker] %s buys %s from %s at %lld to sell again (x%.1f)\n", bot.name().c_str(),
                            sim::itemLink(bot.tables()->items[size_t(flip->what.item)], flip->what).c_str(),
                            flip->seller.c_str(), (long long)flip->price, margin);
                e.flips.clear();
                ask(bot, e, {.what = net::BrokerWhat::Buy, .listing = flip->listing, .count = 1});
                return;
            }
        }
        if (!best) {
            std::printf("  [broker] %s finds no upgrade he can pay for in %d pages\n", bot.name().c_str(), e.pages);
            e.step = Step::Done;
            return;
        }
        e.quoted = best->what;
        e.price = best->price;
        e.step = Step::Buy;
        std::printf("  [broker] %s buys %s from %s at %lld (+%.0f, %d pages)\n", bot.name().c_str(),
                    sim::itemLink(bot.tables()->items[size_t(best->what.item)], best->what).c_str(),
                    best->seller.c_str(), (long long)best->price, best->gain, e.pages);
        ask(bot, e, {.what = net::BrokerWhat::Buy, .listing = best->listing, .count = 1});
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
                // **His wings listed over kWingAsk, brought down to it** (the user, 2026-10-13: "fix
                // old 73.9M price"): put up at four times the merchant's 18.4M before wings had a
                // price of their own, they would have sat their 24 hours unsold.
                for (const net::BrokerRow& row : view.mine) {
                    if (!wingRow(tables, row.what) || row.price <= kWingAsk) continue;
                    if (std::find(e.lowered.begin(), e.lowered.end(), row.id) != e.lowered.end()) continue;
                    e.lowered.push_back(row.id);
                    std::printf("  [broker] %s lowers %s from %lld to %lld\n", bot.name().c_str(),
                                sim::itemLink(tables.items[size_t(row.what.item)], row.what).c_str(), (long long)row.price,
                                (long long)kWingAsk);
                    e.step = Step::Lower;
                    ask(bot, e, {.what = net::BrokerWhat::Lower, .price = kWingAsk, .listing = row.id});
                    return;
                }
                // His Collect only with room for it: into a full bag it came back "the rest waits"
                // 113 times in a day, and its Zen
                // waits with the rest. The counter makes room on his next trip (Bot::brokerHeld).
                if (!view.box.empty() && bot.bagRoom() >= kCollectRoom) {
                    e.step = Step::Collect;
                    ask(bot, e, {.what = net::BrokerWhat::Collect});
                    return;
                }
                listed_[bot.name()] = int(view.mine.size());
                for (const net::BrokerRow& row : view.mine) e.partsUp += part(tables, row.what);
                hold(bot);
                nextSpare(bot, e, int(view.mine.size()));
                return;
            case Step::Lower:
                if (view.no != uint8_t(sim::BrokerNo::Yes)) std::printf("  [broker] %s: %s\n", bot.name().c_str(), view.said.c_str());
                e.step = Step::Look;
                ask(bot, e, {.what = net::BrokerWhat::Look});
                return;
            case Step::Collect:
                if (view.no == uint8_t(sim::BrokerNo::Yes)) e.did = true;
                std::printf("  [broker] %s collects: %s\n", bot.name().c_str(), view.said.c_str());
                listed_[bot.name()] = int(view.mine.size());
                e.partsUp = 0;
                for (const net::BrokerRow& row : view.mine) e.partsUp += part(tables, row.what);
                hold(bot);
                nextSpare(bot, e, int(view.mine.size()));
                return;
            case Step::Quote: {
                // **A half nobody is buying goes to the counter** (the user, 2026-10-12: 96 Scrolls
                // and Bones listed in a run, one sold): already up and no sale of it lately, another
                // listing only piles on. The counter takes it at his next town trip.
                if (part(tables, e.quoted) && view.cheapest > 0 && view.sales.empty()) {
                    ++e.spare;
                    nextSpare(bot, e, listed_[bot.name()]);
                    return;
                }
                // Up to 5% under the cheapest (the user's), what has sold, or four times the NPC's
                // as a window starts a player; never under what a merchant pays.
                const int64_t npc = std::max<int64_t>(1, sim::npcValue(tables, e.quoted));
                const int64_t asked = askingPrice(e.quoted, npc);
                // **Undercut only a market that does not sell** (2026-10-12): every listing 0-5%
                // under the last, Blood Castle's halves went from 160 to the counter's 40 in a game
                // hour while buyers outnumbered them. A thing that sold lately at the cheapest or
                // over goes up at the cheapest.
                const bool selling = view.cheapest > 0 && !view.sales.empty() && view.sales.front().price >= view.cheapest;
                int64_t price = view.cheapest > 0
                                    ? (selling ? view.cheapest : view.cheapest * (100 - int64_t(nameHash(bot.name()) % 6)) / 100)
                                : !view.sales.empty() ? view.sales.front().price
                                                      : asked;
                // Never under half what its options and runes ask: the cheapest of its name may be
                // a plain one, and a merchant pays nothing for them.
                if (asked > 4 * npc) price = std::max(price, asked / 2);
                // A trader's flip goes up for what he paid and a third over it.
                auto& paid = bought_[bot.name()];
                for (auto it = paid.begin(); it != paid.end(); ++it) {
                    if (it->first.item == e.quoted.item && it->first.refinement == e.quoted.refinement &&
                        it->first.excellent == e.quoted.excellent && it->first.sockets == e.quoted.sockets) {
                        price = std::max(price, it->second * 13 / 10);
                        paid.erase(it);
                        break;
                    }
                }
                e.price = std::clamp<int64_t>(price, npc, sim::kBrokerMostPrice);
                // **Wings at a price a bot can pay** (the user, 2026-10-13, B): a merchant pays
                // 18.4M for 1st wings and the listing went up at four times that, 73.9M, against
                // purses of 2-15M -- the wrong class's wings from the mix sat unsold. From
                // kWingAsk, undercut as anything is, never under kWingLeast; the merchant floor
                // is not theirs.
                if (wingRow(tables, e.quoted)) {
                    const int64_t wing = view.cheapest > 0 || !view.sales.empty() ? std::min(price, kWingAsk) : kWingAsk;
                    e.price = std::clamp<int64_t>(wing, kWingLeast, kWingAsk);
                }
                e.step = Step::List;
                ask(bot, e, {.what = net::BrokerWhat::List, .slot = e.spares[e.spare], .seen = e.quoted,
                             .price = e.price, .hours = 24});
                return;
            }
            case Step::List: {
                const std::string label = sim::itemLink(tables.items[size_t(e.quoted.item)], e.quoted);  // the link: botwatch colours it as the game does
                if (view.no == uint8_t(sim::BrokerNo::Yes)) {
                    e.did = true;
                    ++listed_[bot.name()];
                    e.partsUp += part(tables, e.quoted);
                    std::printf("  [broker] %s lists %s at %lld\n", bot.name().c_str(), label.c_str(), (long long)e.price);
                } else {
                    std::printf("  [broker] %s could not list %s: %s\n", bot.name().c_str(), label.c_str(), view.said.c_str());
                }
                ++e.spare;
                hold(bot);
                nextSpare(bot, e, listed_[bot.name()]);
                return;
            }
            case Step::Browse: {
                ++e.pages;
                bool past = false;  // a price past his budget: the rest of this kind is dearer
                for (const net::BrokerRow& row : view.rows) {
                    if (row.flags & net::BrokerRow::Own) continue;
                    if (row.price > e.budget) {
                        past = true;
                        continue;
                    }
                    const double gain = bot.worth(row.what);
                    if (gain > 0.0 && bot.fuseBuyAt(row.what, row.price)) e.picks.push_back({row.id, row.what, row.price, gain, row.seller});
                    // **A trader's flip** (the user, 2026-10-12: "couple of bot types which are
                    // focusing on making proffit and build hes gear score"): listed a third under
                    // what the same thing last sold for -- or, never sold, under half what its
                    // options, sockets and runes ask -- bought to list again. Weighed beside his
                    // upgrades, not only without one: a ticket half always was one, and no trader
                    // flipped once in a 100-bot run.
                    if (bot.trader()) {
                        const int64_t npc = std::max<int64_t>(1, sim::npcValue(*bot.tables(), row.what));
                        const bool under = row.lastSale > 0 ? row.price * 13 < row.lastSale * 10
                                                            : row.price * 2 < askingPrice(row.what, npc);
                        if (under) e.flips.push_back({row.id, row.what, row.price, 0.0, row.seller});
                    }
                }
                const int pages = int((view.total + sim::kBrokerPage - 1) / sim::kBrokerPage);
                if (past || ++e.page >= std::min(pages, kPages)) {
                    ++e.kind;
                    e.page = 0;
                }
                browse(bot, e);
                return;
            }
            case Step::Buy:
                if (view.no == uint8_t(sim::BrokerNo::Yes)) {
                    e.did = true;
                    // **And again, with what he wears now** (the user, 2026-10-12: "bots always
                    // should check for better gear with sockets on market and try to buy also some
                    // gems and put gems on items"): a socketed piece, then a rune for its socket,
                    // in one visit -- up to kBuys, the search run afresh on his new purse.
                    if (++e.bought < kBuys) {
                        e.kinds.clear();
                        e.kind = 0;
                        e.page = 0;
                        e.picks.clear();
                        e.flips.clear();
                        e.until = std::chrono::steady_clock::now() + kGiveUp;
                        browse(bot, e);
                        return;
                    }
                } else {
                    std::printf("  [broker] %s's buy fell through: %s\n", bot.name().c_str(), view.said.c_str());
                    // **Then the next one** (2026-10-12): a row of alike things carries its cheapest
                    // listing alone, every bot after the same one, and 1,415 of 1,867 buys in a run
                    // ended on "Someone bought it first" with the rest of the row still there.
                    if (++e.missed <= kMisses) {
                        e.kinds.clear();
                        e.kind = 0;
                        e.page = 0;
                        e.picks.clear();
                        e.flips.clear();
                        e.until = std::chrono::steady_clock::now() + kGiveUp;
                        browse(bot, e);
                        return;
                    }
                }
                e.step = Step::Done;
                return;
            default: return;
        }
    }

    // **Spares held for the Broker**: the brain sells everything a sale would take to the
    // blacksmith, so without this little ever reached the market. He keeps back as many as his
    // listings have room for, at most kHold; with his listings full, the blacksmith takes them.
    void hold(NetBot& bot) { bot.holdForBroker(std::min(kHold, kListed - listed_[bot.name()])); }

    // **What a buyer gets** (the user, 2026-10-12: "... how they sell items"): four times what a
    // merchant pays, as a window starts a player, and the merchant pays nothing for what makes a
    // piece wanted -- each excellent option doubles it, each socket half again, a set rune more
    // by its rarity.
    static constexpr int64_t kWingAsk = 8000000, kWingLeast = 3000000;
    static bool wingRow(const content::Tables& tables, const sim::Held& what) {
        if (what.empty() || size_t(what.item) >= tables.items.size()) return false;
        const content::ItemRow& row = tables.items[size_t(what.item)];
        return sim::firstWing(row) || sim::secondWing(row);
    }
    static int64_t askingPrice(const sim::Held& what, int64_t npc) {
        double rate = 1.0 + sim::excellentCount(what.excellent);
        for (int k = 0; k < std::min<int>(what.sockets, 3); ++k) {
            const sim::PowerRow* power = sim::powerOf(what.powers[k]);
            rate *= !power ? 1.5
                    : power->rarity == sim::Rarity::Legendary ? 4.0
                    : power->rarity == sim::Rarity::Epic       ? 3.0
                    : power->rarity == sim::Rarity::Rare       ? 2.0
                                                               : 1.6;
        }
        return int64_t(4.0 * double(npc) * rate);
    }

    // A Blood Castle or Devil Square half: Scroll, Bone, Eye or Key.
    static bool part(const content::Tables& tables, const sim::Held& what) {
        if (what.empty() || size_t(what.item) >= tables.items.size()) return false;
        const content::ItemRow& row = tables.items[size_t(what.item)];
        return sim::scrollOfArchangel(row) || sim::bloodBone(row) || sim::devilsEye(row) || sim::devilsKey(row);
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
    std::map<std::string, int> trips_;     // his town trips, as last seen
    std::map<std::string, std::vector<std::pair<sim::Held, int64_t>>> bought_;  // a trader's flips, and their cost
    uint32_t nextAsk_ = 1;
};
