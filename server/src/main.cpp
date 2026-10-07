// mu2_server: MU2_BGFX's server (docs/sprints/18-the-wire.md, server/README.md).
//
//   mu2_server [--port N] [--assets DIR] [--castle-period S]
//
// **Connections to the same world share it** (docs/sprints/19-many-heroes.md): the first Hello
// for a world raises its realm with him as its first player; each later one comes in by a Join
// command at the next tick's start, and is welcomed once that tick has let him in -- with the
// world's start and every tick since it was raised, which his mirror replays to stand where the
// server stands. A connection that goes leaves by a Leave command; a world nobody is in is let
// go. Every realm steps on one 20 Hz deadline: poll, step every world, flush (server-plan §3).
// Each tick goes to everyone in the world -- the wall clock, the rain, the commands applied, each
// with its player -- and every second its hash, which a mirror that disagrees says.
//
// A malformed frame drops that connection, never the server.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "net/socket.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/realm.h"

namespace {

using namespace mu;

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

constexpr double kTickSeconds = 0.05;  // 20 Hz, the realm's own (sim/realm.h)
constexpr int kHashEvery = 20;         // a hash a second
// A Join's ticket, the server's own: high, so it is never one a client numbered.
constexpr uint32_t kJoinTickets = 0x80000000u;

struct World {
    std::string name;
    content::Tables tables;  // owned: the realm keeps a pointer to them
    std::unique_ptr<sim::Realm> realm;
    net::Welcome start;      // how it was raised: what every mirror raises from
    std::vector<net::Tick> log;  // every tick since, what a newcomer's mirror replays
    std::vector<sim::Command> queued;  // arrived since the last tick, in order
    uint32_t nextTicket = kJoinTickets;
    std::vector<uint32_t> orphans;  // Joins whose connection went before they were answered
};

struct Session {
    net::Socket socket;
    std::vector<uint8_t> in;
    std::string who;  // for the log: the order it came in, and later his name
    World* world = nullptr;
    uint32_t player = 0;   // his body's id, once welcomed
    uint32_t joining = 0;  // his Join's ticket while it waits for its tick
    bool welcomed = false;
};

bool worldName(const std::string& name) {
    // A world's name is a folder under cooked/: letters only, so a Hello cannot name a path.
    if (name.empty()) return false;
    for (char c : name) {
        if (!(c >= 'a' && c <= 'z')) return false;
    }
    return true;
}

// The world's first player: its realm raised round him, as sprint 18 raised one per connection.
World* raiseWorld(std::vector<std::unique_ptr<World>>& worlds, const net::Hello& hello,
                  const std::string& assets, const sim::RealmConfig& config, uint64_t seed,
                  const std::string& who) {
    auto world = std::make_unique<World>();
    world->name = hello.world;
    std::string error;
    const std::string path = assets + "/cooked/" + hello.world + "/" + hello.world + ".mur";
    if (!content::loadTables(path, world->tables, error)) {
        core::logError("%s: cannot load %s: %s", who.c_str(), path.c_str(), error.c_str());
        return nullptr;
    }
    const int level = std::clamp(hello.level, 1, sim::kMaximumLevel);
    const sim::Kin kin = sim::Kin(std::min<int>(hello.kin, int(sim::Kin::MagicGladiator)));
    world->realm = std::make_unique<sim::Realm>();
    world->realm->configure(config);
    if (!world->realm->raise(&world->tables, seed, hello.column, hello.row, kin, level)) {
        core::logError("%s: %s would not raise", who.c_str(), hello.world.c_str());
        return nullptr;
    }
    sim::outfit(*world->realm, hello.weapon, hello.shield);
    net::Welcome& w = world->start;
    w.seed = seed;
    w.world = hello.world;
    w.kin = uint8_t(kin);
    w.level = level;
    w.column = hello.column;
    w.row = hello.row;
    w.weapon = hello.weapon;
    w.shield = hello.shield;
    w.config = config;
    core::logf("%s: raised %s, class %d level %d at %d,%d, seed %llu", who.c_str(),
               hello.world.c_str(), int(kin), level, hello.column, hello.row,
               (unsigned long long)seed);
    worlds.push_back(std::move(world));
    return worlds.back().get();
}

// His Welcome: the world's start, his id, and the world's past after it.
bool welcome(Session& one, uint32_t player) {
    net::Welcome w = one.world->start;
    w.you = player;
    w.backlog = uint32_t(one.world->log.size());
    std::vector<uint8_t> out;
    net::put(out, w);
    for (const net::Tick& t : one.world->log) net::put(out, t);
    one.player = player;
    one.joining = 0;
    one.welcomed = one.socket.send(out);
    core::logf("%s: welcomed into %s as #%u, %u ticks of its past, %d here", one.who.c_str(),
               one.world->name.c_str(), player, w.backlog, one.world->realm->playersHere());
    return one.welcomed;
}

bool hello(Session& one, const net::Hello& said, std::vector<std::unique_ptr<World>>& worlds,
           const std::string& assets, const sim::RealmConfig& config, uint64_t seed) {
    if (said.version != net::kVersion) {
        core::logError("%s: version %u, this server speaks %u", one.who.c_str(), said.version,
                       net::kVersion);
        return false;
    }
    if (!worldName(said.world)) {
        core::logError("%s: no world called '%s'", one.who.c_str(), said.world.c_str());
        return false;
    }
    for (auto& world : worlds) {
        if (world->name != said.world) continue;
        // Into a world already running: a Join at the next tick's start, his hands by arm index.
        const content::Tables& tables = world->tables;
        const int32_t held = said.weapon.empty() ? -1 : tables.armNamed(said.weapon);
        const int32_t worn = said.shield.empty() ? -1 : tables.armNamed(said.shield);
        sim::Command join;
        join.kind = sim::Command::Kind::Join;
        join.ticket = world->nextTicket++;
        join.a = std::min<int>(said.kin, int(sim::Kin::MagicGladiator));
        join.b = std::clamp(said.level, 1, sim::kMaximumLevel);
        join.c = said.column;
        join.d = said.row;
        join.target = uint32_t(held + 1);
        join.zen = worn + 1;
        world->queued.push_back(join);
        one.world = world.get();
        one.joining = join.ticket;
        core::logf("%s: joining %s, class %d level %d at %d,%d", one.who.c_str(),
                   said.world.c_str(), join.a, join.b, said.column, said.row);
        return true;
    }
    World* world = raiseWorld(worlds, said, assets, config, seed, one.who);
    if (world == nullptr) return false;
    one.world = world;
    return welcome(one, world->realm->hero().id);
}

// Everything that arrived on one connection. False when it must go.
bool hear(Session& one, std::vector<std::unique_ptr<World>>& worlds, const std::string& assets,
          const sim::RealmConfig& config, std::mt19937_64& seeds) {
    if (!one.socket.receive(one.in)) return false;
    net::Kind kind{};
    std::vector<uint8_t> body;
    while (true) {
        const int took = net::take(one.in, kind, body);
        if (took == 0) return true;
        if (took < 0) {
            core::logError("%s: not our protocol", one.who.c_str());
            return false;
        }
        if (kind == net::Kind::Hello && one.world == nullptr) {
            net::Hello said;
            if (!net::parse(body, said) || !hello(one, said, worlds, assets, config, seeds())) {
                return false;
            }
        } else if (kind == net::Kind::Command && one.welcomed) {
            sim::Command command;
            if (!net::parse(body, command)) return false;
            // The world's door is the server's own.
            if (command.kind == sim::Command::Kind::Join || command.kind == sim::Command::Kind::Leave) {
                continue;
            }
            // Whatever the client wrote there, it is his own hero who asks.
            command.player = one.player;
            one.world->queued.push_back(command);
        } else if (kind == net::Kind::Command && one.joining != 0) {
            // Asked before his Welcome: he has no body yet to ask with.
            continue;
        } else {
            core::logError("%s: a frame of kind %d out of turn", one.who.c_str(), int(kind));
            return false;
        }
    }
}

// One tick of one world: its inputs applied, the step, sent to everyone in it with now and then
// its hash; then whoever it let in, welcomed.
void tick(World& world, std::vector<std::unique_ptr<Session>>& sessions) {
    net::Tick t;
    t.wallClock = int64_t(std::time(nullptr));
    t.rain = false;  // the server has no weather yet: no rain, so no Golden Invasion
    t.commands.swap(world.queued);
    sim::Realm& realm = *world.realm;
    realm.setWallClock(t.wallClock);
    realm.invasionRain(t.rain);
    for (const sim::Command& c : t.commands) realm.command(c);
    realm.step();
    t.tick = uint32_t(realm.tick());
    std::vector<uint8_t> out;
    net::put(out, t);
    if (t.tick % kHashEvery == 0) net::put(out, net::Hash{t.tick, net::stateHash(realm)});
    world.log.push_back(std::move(t));
    for (auto& one : sessions) {
        if (one->world != &world || !one->welcomed || !one->socket.open()) continue;
        if (!one->socket.send(out)) one->socket.close();
    }
    // The Joins this tick answered: each newcomer welcomed with the past up to this tick, or,
    // with his connection already gone, sent out again.
    for (const sim::Happening& said : realm.happenings()) {
        if (said.what != sim::What::Answered || said.a != int32_t(sim::Command::Kind::Join)) continue;
        const uint32_t ticket = uint32_t(said.c);
        const auto orphan = std::find(world.orphans.begin(), world.orphans.end(), ticket);
        if (orphan != world.orphans.end()) {
            world.orphans.erase(orphan);
            if (said.b > 0) {
                world.queued.push_back({.kind = sim::Command::Kind::Leave, .player = uint32_t(said.b)});
            }
            continue;
        }
        for (auto& one : sessions) {
            if (one->world != &world || one->joining != ticket) continue;
            if (said.b <= 0) {
                core::logError("%s: nowhere to stand in %s", one->who.c_str(), world.name.c_str());
                one->socket.close();
            } else if (!welcome(*one, uint32_t(said.b))) {
                one->socket.close();
            }
        }
    }
}

// A connection gone: his player leaves the world at its next tick.
void part(Session& one) {
    if (one.world == nullptr) return;
    if (one.welcomed) {
        one.world->queued.push_back({.kind = sim::Command::Kind::Leave, .player = one.player});
    } else if (one.joining != 0) {
        one.world->orphans.push_back(one.joining);
    }
}

}  // namespace

int main(int argc, char** argv) {
    int port = net::kDefaultPort;
    std::string assets = MU2_ASSET_DIR;
    sim::RealmConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "--castle-period" && i + 1 < argc) {
            const int s = std::max(2, std::atoi(argv[++i]));
            config.castle = {s, s / 2, s / 2};
        } else {
            std::fprintf(stderr, "usage: mu2_server [--port N] [--assets DIR] [--castle-period S]\n");
            return 2;
        }
    }
    // A line at a time: under systemd stdout is a pipe to the journal, fully buffered otherwise,
    // and who joined would not be seen until kilobytes later.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    net::Socket listener;
    std::string error;
    if (!listener.listen(port, error)) {
        core::logError("%s", error.c_str());
        return 1;
    }
    core::logf("mu2_server: protocol %u, listening on %d, tables from %s", net::kVersion, port,
               assets.c_str());

    std::mt19937_64 seeds(uint64_t(std::time(nullptr)));
    std::vector<std::unique_ptr<World>> worlds;
    std::vector<std::unique_ptr<Session>> sessions;
    uint64_t arrivals = 0;
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    while (!g_stop) {
        // Poll: who arrived, what each said.
        for (net::Socket s = listener.accept(); s.open(); s = listener.accept()) {
            auto one = std::make_unique<Session>();
            one->socket = std::move(s);
            one->who = "player " + std::to_string(++arrivals);
            core::logf("%s: connected", one->who.c_str());
            sessions.push_back(std::move(one));
        }
        for (auto& one : sessions) {
            if (one->socket.open() && !hear(*one, worlds, assets, config, seeds)) one->socket.close();
        }
        // Step every world on the deadline, owing ticks rather than dropping them, and flush.
        const auto now = Clock::now();
        int owed = 0;
        while (next <= now && owed < 20) {
            for (auto& world : worlds) tick(*world, sessions);
            next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kTickSeconds));
            ++owed;
        }
        if (next < now) next = now;  // more than a second behind: start the clock again
        for (auto& one : sessions) {
            if (one->socket.open() && !one->socket.flush()) one->socket.close();
        }
        for (size_t i = 0; i < sessions.size();) {
            if (!sessions[i]->socket.open()) {
                core::logf("%s: gone", sessions[i]->who.c_str());
                part(*sessions[i]);
                sessions.erase(sessions.begin() + long(i));
            } else {
                ++i;
            }
        }
        // A world nobody is in, or on the way into, is let go; the next Hello raises it fresh.
        for (size_t i = 0; i < worlds.size();) {
            const World* world = worlds[i].get();
            const bool someone = std::any_of(sessions.begin(), sessions.end(),
                                             [&](const auto& one) { return one->world == world; });
            if (!someone) {
                core::logf("%s: empty after %zu ticks, let go", world->name.c_str(), world->log.size());
                worlds.erase(worlds.begin() + long(i));
            } else {
                ++i;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    core::logf("mu2_server: stopped, %zu connected", sessions.size());
    return 0;
}
