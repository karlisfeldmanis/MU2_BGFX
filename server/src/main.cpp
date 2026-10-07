// mu2_server: MU2_BGFX's server (docs/sprints/18-the-wire.md, server/README.md).
//
//   mu2_server [--port N] [--assets DIR] [--castle-period S]
//
// Each connection is a player in a realm of his own (one player per world, the user's
// "network first", 2026-10-07; phases 2 and 3 make the world shared). The server raises his
// world on his Hello, outfits him with the rules' own cradle, and steps every realm on one
// 20 Hz deadline: poll, step every realm, flush (server-plan §3). Each tick it sends what went
// in -- the wall clock, the rain, the commands applied -- for the client's mirror to step the
// same tick; every second it sends its realm's hash, and a mirror that disagrees says so.
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

struct Session {
    net::Socket socket;
    std::vector<uint8_t> in;
    std::string who;  // for the log: the address order it came in, and later his name
    bool welcomed = false;
    content::Tables tables;  // his world's, owned: the realm keeps a pointer to them
    std::unique_ptr<sim::Realm> realm;
    std::vector<sim::Command> queued;  // arrived since the last tick, in order
};

bool welcome(Session& one, const net::Hello& hello, const std::string& assets,
             const sim::RealmConfig& config, uint64_t seed) {
    if (hello.version != net::kVersion) {
        core::logError("%s: version %u, this server speaks %u", one.who.c_str(), hello.version,
                       net::kVersion);
        return false;
    }
    // A world's name is a folder under cooked/: letters only, so a Hello cannot name a path.
    for (char c : hello.world) {
        if (!(c >= 'a' && c <= 'z')) {
            core::logError("%s: no world called '%s'", one.who.c_str(), hello.world.c_str());
            return false;
        }
    }
    std::string error;
    const std::string path = assets + "/cooked/" + hello.world + "/" + hello.world + ".mur";
    if (hello.world.empty() || !content::loadTables(path, one.tables, error)) {
        core::logError("%s: cannot load %s: %s", one.who.c_str(), path.c_str(), error.c_str());
        return false;
    }
    const int level = std::clamp(hello.level, 1, 400);
    const sim::Kin kin = sim::Kin(std::min<int>(hello.kin, int(sim::Kin::MagicGladiator)));
    one.realm = std::make_unique<sim::Realm>();
    one.realm->configure(config);
    if (!one.realm->raise(&one.tables, seed, hello.column, hello.row, kin, level)) {
        core::logError("%s: %s would not raise", one.who.c_str(), hello.world.c_str());
        return false;
    }
    sim::outfit(*one.realm, hello.weapon, hello.shield);

    net::Welcome w;
    w.seed = seed;
    w.world = hello.world;
    w.kin = uint8_t(kin);
    w.level = level;
    w.column = hello.column;
    w.row = hello.row;
    w.weapon = hello.weapon;
    w.shield = hello.shield;
    w.config = config;
    std::vector<uint8_t> out;
    net::put(out, w);
    one.welcomed = one.socket.send(out);
    core::logf("%s: welcomed into %s, class %d level %d at %d,%d, seed %llu", one.who.c_str(),
               hello.world.c_str(), int(kin), level, hello.column, hello.row,
               (unsigned long long)seed);
    return one.welcomed;
}

// Everything that arrived on one connection. False when it must go.
bool hear(Session& one, const std::string& assets, const sim::RealmConfig& config,
          std::mt19937_64& seeds) {
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
        if (kind == net::Kind::Hello && !one.welcomed) {
            net::Hello hello;
            if (!net::parse(body, hello) || !welcome(one, hello, assets, config, seeds())) return false;
        } else if (kind == net::Kind::Command && one.welcomed) {
            sim::Command command;
            if (!net::parse(body, command)) return false;
            // Whatever the client wrote there, it is his own hero who asks.
            command.player = one.realm->hero().id;
            one.queued.push_back(command);
        } else {
            core::logError("%s: a frame of kind %d out of turn", one.who.c_str(), int(kind));
            return false;
        }
    }
}

// One tick of one realm: its inputs applied and sent, the step, and now and then its hash.
bool tick(Session& one) {
    net::Tick t;
    t.wallClock = int64_t(std::time(nullptr));
    t.rain = false;  // the server has no weather yet: no rain, so no Golden Invasion
    t.commands.swap(one.queued);
    sim::Realm& realm = *one.realm;
    realm.setWallClock(t.wallClock);
    realm.invasionRain(t.rain);
    for (const sim::Command& c : t.commands) realm.command(c);
    realm.step();
    t.tick = uint32_t(realm.tick());
    std::vector<uint8_t> out;
    net::put(out, t);
    if (t.tick % kHashEvery == 0) net::put(out, net::Hash{t.tick, net::stateHash(realm)});
    return one.socket.send(out);
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
            if (one->socket.open() && !hear(*one, assets, config, seeds)) one->socket.close();
        }
        // Step every realm on the deadline, owing ticks rather than dropping them, and flush.
        const auto now = Clock::now();
        int owed = 0;
        while (next <= now && owed < 20) {
            for (auto& one : sessions) {
                if (one->socket.open() && one->welcomed && !tick(*one)) one->socket.close();
            }
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
                sessions.erase(sessions.begin() + long(i));
            } else {
                ++i;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    core::logf("mu2_server: stopped, %zu connected", sessions.size());
    return 0;
}
