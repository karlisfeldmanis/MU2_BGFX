#include "economy.h"

#include "host.h"

namespace mu::server {
namespace {

// What a change in a player's Zen is put down to. The order is the line's.
enum Why : int { Kills, Crackers, Sold, Quests, Shop, Repair, Machine, Travel, Events, Market, Other, kWhys };
const char* const kWhyNames[kWhys] = {"kills",  "crackers", "sold",   "quests", "shop",
                                      "repair", "machine",  "travel", "events", "market", "other"};

// One side's hour: Zen in and out by why, and what trades moved.
struct Ledger {
    int64_t in[kWhys] = {};
    int64_t out[kWhys] = {};
    int64_t traded = 0;
};

constexpr auto kEvery = std::chrono::hours(1);

FILE* g_file = nullptr;
std::string g_path;
Ledger g_players, g_bots;
Clock::time_point g_since = Clock::now();
// Each session's Zen at its last tick, carried and kept, by its character's token and the world
// it stood in: a new world, or a session not seen before, starts from what it holds.
struct Held {
    const World* world = nullptr;
    int64_t zen = 0;
    bool seen = false;
};
std::map<uint64_t, Held> g_held;

// The why a command's answer names, or -1 for one that pays or takes nothing.
int answered(int32_t kind) {
    using Kind = sim::Command::Kind;
    switch (Kind(kind)) {
        case Kind::Buy: case Kind::BuyBack: return Shop;
        case Kind::Sell: return Sold;
        case Kind::Repair: case Kind::RepairAll: return Repair;
        case Kind::Mix: return Machine;
        case Kind::Travel: case Kind::GoBack: return Travel;
        case Kind::CompleteQuest: return Quests;
        case Kind::EnterCastle: case Kind::HandInStaff: case Kind::ClaimCastle: return Events;
        // The Broker's: a buy pays out, a Collect's Zen comes in.
        case Kind::BrokerBuy: case Kind::BrokerZen: return Market;
        default: return -1;
    }
}

std::string millions(int64_t zen) {
    char text[32];
    const double m = double(zen) / 1e6;
    if (zen == 0) return "0";
    if (std::abs(m) >= 0.1) std::snprintf(text, sizeof text, "%.1fM", m);
    else std::snprintf(text, sizeof text, "%.0fk", double(zen) / 1e3);
    return text;
}

std::string side(const char* name, const Ledger& l, int people, int64_t held) {
    int64_t in = 0, out = 0;
    for (int w = 0; w < kWhys; ++w) {
        in += l.in[w];
        out += l.out[w];
    }
    std::string line = std::string(name) + " " + std::to_string(people) + " holding " + millions(held) +
                       " | in " + millions(in);
    for (int w = 0; w < kWhys; ++w) {
        if (l.in[w] != 0) line += std::string(" ") + kWhyNames[w] + " " + millions(l.in[w]);
    }
    line += " | out " + millions(out);
    for (int w = 0; w < kWhys; ++w) {
        if (l.out[w] != 0) line += std::string(" ") + kWhyNames[w] + " " + millions(l.out[w]);
    }
    line += " | net " + std::string(in - out >= 0 ? "+" : "") + millions(in - out) + " | traded " +
            millions(l.traded);
    return line;
}

}  // namespace

bool economyOpen(const std::string& storePath) {
    const size_t slash = storePath.find_last_of('/');
    g_path = (slash == std::string::npos ? std::string() : storePath.substr(0, slash + 1)) + "economy.log";
    g_file = std::fopen(g_path.c_str(), "a");
    if (!g_file) {
        core::logError("economy: cannot write %s; the economy goes unlogged", g_path.c_str());
        return false;
    }
    core::logf("economy: an hour a line in %s", g_path.c_str());
    g_since = Clock::now();
    return true;
}

void economyTick(World& world, const std::vector<std::unique_ptr<Session>>& sessions) {
    if (!g_file) return;
    const sim::Realm& realm = *world.realm;
    if (realm.playerCount() == 0) return;
    for (const auto& one : sessions) {
        if (one->world != &world || !one->welcomed || one->player == 0) continue;
        // Read without lookAs: `me_` is in the snapshot and the hash, and a look back at a player
        // who has left fails and leaves it moved (a mirror diverged so).
        const int64_t zen = realm.heldOf(one->player);
        if (zen < 0) continue;
        Held& was = g_held[one->token];
        const bool fresh = !was.seen || was.world != &world;
        const int64_t change = zen - was.zen;
        was = {&world, zen, true};
        if (fresh || change == 0) continue;
        // What the realm said he did this tick, the first that pays or takes the right way.
        int why = -1;
        bool trade = false;
        for (const sim::Happening& said : realm.happenings()) {
            if (said.who != one->player) continue;
            int w = -1;
            if (said.what == sim::What::Picked && said.b == -1) w = Kills;
            else if (said.what == sim::What::Cracked && said.a == -1) w = Crackers;
            else if (said.what == sim::What::Answered && said.b >= 0) w = answered(said.a);
            else if (said.what == sim::What::Deal && said.a == int32_t(sim::DealEvent::Done)) trade = true;
            const bool pays = w == Kills || w == Crackers || w == Sold || w == Quests || w == Events;
            if (w >= 0 && (change > 0) == pays) {
                why = w;
                break;
            }
            if (w >= 0 && (w == Events || w == Market)) why = w;  // either way
        }
        Ledger& l = one->bot ? g_bots : g_players;
        if (trade && why < 0) {
            l.traded += std::abs(change);
            continue;
        }
        if (why < 0) why = Other;
        (change > 0 ? l.in[why] : l.out[why]) += std::abs(change);
    }
}

void economyReport(const std::vector<std::unique_ptr<Session>>& sessions, bool force) {
    if (!g_file || (!force && Clock::now() - g_since < kEvery)) return;
    int players = 0, bots = 0;
    int64_t playersHeld = 0, botsHeld = 0;
    for (const auto& one : sessions) {
        if (!one->welcomed || !one->socket.open()) continue;
        const auto at = g_held.find(one->token);
        const int64_t zen = at == g_held.end() ? 0 : at->second.zen;
        if (one->bot) {
            ++bots;
            botsHeld += zen;
        } else {
            ++players;
            playersHeld += zen;
        }
    }
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", std::localtime(&now));
    const double minutes = std::chrono::duration<double>(Clock::now() - g_since).count() / 60.0;
    std::fprintf(g_file, "%s  %.0f min\n  %s\n  %s\n", stamp, minutes,
                 side("players", g_players, players, playersHeld).c_str(),
                 side("bots   ", g_bots, bots, botsHeld).c_str());
    std::fflush(g_file);
    g_players = Ledger{};
    g_bots = Ledger{};
    g_since = Clock::now();
    // Whoever has gone is let go: his next session starts from what it holds.
    for (auto it = g_held.begin(); it != g_held.end();) {
        const bool here = std::any_of(sessions.begin(), sessions.end(),
                                      [&](const auto& one) { return one->token == it->first; });
        it = here ? std::next(it) : g_held.erase(it);
    }
}

}  // namespace mu::server
