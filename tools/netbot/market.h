// The run's bots trading among themselves through the trade window (sim/realm_deal.cpp): netbot's
// Market, out of netbot.cpp to make room. Included inside netbot.cpp's namespace after NetBot,
// whose trading half it drives; not a header to include anywhere else.
#pragma once

// **The market** (the user, 2026-10-11: "we now have trading in our game, that means we need to
// teach bots do some trading so they can collabrate and become better"): the run's bots hand each
// other what one would sell and another would wear, and the jewels one holds past his keep that
// another is short of, through the trade window as players do (sim/realm_deal.cpp). A gift: the
// run's bots are one crew, and what the blacksmith pays for a piece is less than a friend's
// upgrade is worth.
//
// Every two seconds it reads each free bot's spares (Bot::spares) and tries each on every other
// free bot of his map (Bot::worth: put in his bag on a scratch copy of his mirror and the best of
// it worn), and the most gained is arranged: the two meet -- at the giver's feet when they are
// near, else at the town's arrival point -- wait there with their windows shut, and swap
// (NetBot::dealStep). The giver names the thing on the map's chat as a link (sim/item_link.h).
// A swap is given two game minutes; one that fails is not tried again for ten.
//
// The market sees only this process's bots, as the board does.
class Market {
public:
    void tick(std::vector<std::unique_ptr<NetBot>>& bots) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_) return;
        next_ = now + std::chrono::seconds(2);
        std::map<std::string, NetBot*> byName;
        for (auto& bot : bots) byName[bot->name()] = bot.get();
        const auto named = [&](const std::string& n) -> NetBot* {
            const auto it = byName.find(n);
            return it == byName.end() || !it->second->up() ? nullptr : it->second;
        };
        // The swaps under way: done, failed or out of time.
        for (Open& o : open_) {
            NetBot* g = named(o.giver);
            NetBot* t = named(o.taker);
            const int gs = g ? g->swapState() : -1, ts = t ? t->swapState() : -1;
            if (gs == 0 && ts == 0 && g && g->clockNow() < o.until) continue;
            const bool done = gs == 1 || ts == 1;
            if (g) g->endSwap();
            if (t) t->endSwap();
            o.gone = true;
            if (done) {
                ++swapped_;
                std::printf("  [market] %s gave %s to %s (+%.0f), %d swapped this run\n", o.giver.c_str(),
                            o.label.c_str(), o.taker.c_str(), o.gain, swapped_);
                if (t) {
                    t->sayOn(net::Channel::Map, "thanks " + o.giver + "!");
                    received_[o.taker + "|" + o.key] = t->clockNow() + 10 * 60 * 20;
                }
            } else {
                std::printf("  [market] %s's %s to %s fell through\n", o.giver.c_str(), o.label.c_str(), o.taker.c_str());
                if (g) failed_[o.taker + "|" + o.key] = g->clockNow() + 10 * 60 * 20;
            }
        }
        std::erase_if(open_, [](const Open& o) { return o.gone; });

        std::set<std::string> busy;
        for (const Open& o : open_) busy.insert(o.giver), busy.insert(o.taker);
        const auto free = [&](NetBot& b) {
            return b.up() && !b.driven() && !busy.count(b.name()) && !b.swapping() && !b.onErrand() && b.alive() && !b.inCastle() &&
                   !b.inSquare() && b.tables() != nullptr;
        };
        struct Best {
            NetBot* giver = nullptr;
            NetBot* taker = nullptr;
            sim::Held what;
            std::string key;
            double gain = 0.0;
        } best;
        for (auto& gp : bots) {
            NetBot& giver = *gp;
            if (!free(giver)) continue;
            for (const int slot : giver.spares()) {
                const sim::Held what = giver.held(slot);
                const std::string key = keyOf(what);
                // Not what he was just given: a trial and his own choice may part, and a pair of
                // Leather Pants went between two knights four times.
                if (const auto r = received_.find(giver.name() + "|" + key);
                    r != received_.end() && giver.clockNow() < r->second) {
                    continue;
                }
                for (auto& tp : bots) {
                    NetBot& taker = *tp;
                    if (&taker == &giver || !free(taker) || taker.world() != giver.world() ||
                        taker.mapNow() != giver.mapNow() || giver.bodyOf(taker.name()) == 0) {
                        continue;
                    }
                    const std::string mark = taker.name() + "|" + key;
                    if (const auto f = failed_.find(mark); f != failed_.end() && taker.clockNow() < f->second) continue;
                    // A trial is a scratch laid from his mirror: kept while his level and his gear
                    // stand. Kept a wall minute instead, at mu2_server --speed 100 a gain outlived
                    // the gloves he put on, and he was handed the same Leather Gloves +3 three times.
                    double gain;
                    const std::string worn = taker.wornKey();
                    const auto c = tried_.find(mark);
                    if (c != tried_.end() && c->second.first == worn) {
                        gain = c->second.second;
                    } else {
                        gain = taker.worth(what);
                        tried_[mark] = {worn, gain};
                    }
                    if (gain > best.gain) best = {&giver, &taker, what, key, gain};
                }
            }
        }
        if (!best.giver) return;
        NetBot& g = *best.giver;
        NetBot& t = *best.taker;
        const int apart = std::max(std::abs(g.column() - t.column()), std::abs(g.row() - t.row()));
        int column = g.column(), row = g.row();
        if (apart > kNear) {
            const WorldRow* w = worldOf(g.mapNow());
            if (!w || w->map == int(sim::kBloodCastleMap) || w->map == int(sim::kDevilSquareMap)) {
                failed_[t.name() + "|" + best.key] = t.clockNow() + 10 * 60 * 20;
                return;
            }
            column = w->arrive[0];
            row = w->arrive[1];
        }
        const content::ItemRow& itemRow = g.tables()->items[size_t(best.what.item)];
        Open o;
        o.giver = g.name();
        o.taker = t.name();
        o.key = best.key;
        o.label = sim::linkLabel(itemRow, best.what);
        o.gain = best.gain;
        o.until = g.clockNow() + kSwapTicks + 20 * 20;
        NetBot::Swap gs;
        gs.partner = g.bodyOf(t.name());
        gs.giving = true;
        gs.what = best.what;
        gs.until = g.clockNow() + kSwapTicks;
        NetBot::Swap ts;
        ts.partner = t.bodyOf(g.name());
        ts.giving = false;
        ts.what = best.what;
        ts.until = t.clockNow() + kSwapTicks;
        if (gs.partner == 0 || ts.partner == 0) return;
        g.swap(gs, column, row);
        // The taker stands a step off the giver, not on him.
        t.swap(ts, column + (apart > kNear ? 1 : 0), row);
        open_.push_back(o);
        std::printf("  [market] %s gives %s to %s (+%.0f), meeting at %d,%d\n", o.giver.c_str(), o.label.c_str(),
                    o.taker.c_str(), o.gain, column, row);
        g.sayOn(net::Channel::Map, t.name() + ", I have " + sim::itemLink(itemRow, best.what) + " for you");
    }

private:
    struct Open {
        std::string giver, taker, key, label;
        double gain = 0.0;
        int64_t until = 0;  // the giver's clock
        bool gone = false;
    };
    static std::string keyOf(const sim::Held& h) {
        char out[96];
        std::snprintf(out, sizeof out, "%d.%d.%d.%d.%d.%d.%d.%d.%d", h.item, h.refinement, h.luck, h.option,
                      h.excellent, h.sockets, h.powers[0], h.powers[1], h.powers[2]);
        return out;
    }
    static constexpr int kNear = 12;                     // tiles: met where the giver stands
    static constexpr int64_t kSwapTicks = 2 * 60 * 20;  // two game minutes
    std::vector<Open> open_;
    std::map<std::string, int64_t> failed_;  // taker|thing: not before this (taker's clock)
    std::map<std::string, int64_t> received_;  // taker|thing: not his to give before this (his clock)
    std::map<std::string, std::pair<std::string, double>> tried_;  // taker|thing: (his wornKey, gain)
    std::chrono::steady_clock::time_point next_{};
    int swapped_ = 0;
};
