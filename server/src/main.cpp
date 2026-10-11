// mu2_server: MU2_BGFX's server (docs/sprints/18-the-wire.md, server/README.md).
//
//   mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S] [--square-period S] [--map-cap N] [--speed N]
//
// **Connections to the same world share it** (docs/sprints/19-many-heroes.md): the first Hello
// for a world raises its realm with him as its first player; each later one comes in by a Join
// command at the next tick's start, and is welcomed once that tick has let him in -- with the
// world's start, its last snapshot and every tick since that (at most a minute's, sprint 21),
// which his mirror is laid with and replays to stand where the
// server stands. A connection that goes leaves by a Leave command, and his character (sim::Kept)
// is kept under his token in characters.db (store.h): a map change reconnects with it, and he
// comes into the next world as he left the last -- or, next week, as he left the server
// (docs/sprints/20-the-world-host.md). A world nobody is in is let go. Every
// realm steps on one 20 Hz deadline: poll, step every world, flush (server-plan §3).
// **Where he comes into the next world is the server's:** a gate, a Tab trip, a way home or the
// end of Blood Castle is read off what the realm said, and the Hello that follows is put down
// there, whatever tile the client asked for (landingOf). And the store keeps which world he is
// in: a run's first Hello that names another is answered Elsewhere.
// Each tick goes to everyone in the world -- the wall clock, the rain, the commands applied, each
// with its player -- and every second its hash, which a mirror that disagrees says.
//
// **The character screen is the server's too** (phase 6, docs/sprints/23-the-account.md): a
// connection that opens with Account is answered with the account's characters (Roster), and may
// then Create and Delete; each is answered with the Roster again. An account is a key the client
// made once, and its characters are played only with it.
//
// A malformed frame drops that connection, never the server.

#include "host.h"

namespace mu::server {


// Everything that arrived on one connection. False when it must go.
bool hear(Session& one, std::vector<std::unique_ptr<World>>& worlds,
          std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
          const sim::RealmConfig& config, std::mt19937_64& seeds) {
    const size_t had = one.in.size();
    if (!one.socket.receive(one.in)) return false;
    if (one.in.size() != had) one.heardAt = Clock::now();
    net::Kind kind{};
    std::vector<uint8_t> body;
    while (true) {
        const int took = net::take(one.in, kind, body, net::kMostAsked);
        if (took == 0) return true;
        if (took < 0) {
            core::logError("%s: not our protocol", one.who.c_str());
            return false;
        }
        if (kind == net::Kind::Account && one.world == nullptr && one.account.empty()) {
            net::Account said;
            if (!net::parse(body, said) || !account(one, said)) return false;
        } else if (kind == net::Kind::Create && one.world == nullptr && !one.account.empty()) {
            net::Create asked;
            if (!net::parse(body, asked) || !create(one, asked, seeds, sessions)) return false;
        } else if (kind == net::Kind::Delete && one.world == nullptr && !one.account.empty()) {
            net::Delete asked;
            if (!net::parse(body, asked) || !drop(one, asked, sessions)) return false;
        } else if (kind == net::Kind::Hello && one.world == nullptr) {
            net::Hello said;
            if (!net::parse(body, said) || !hello(one, said, worlds, sessions, assets, config, seeds)) {
                return false;
            }
        } else if (kind == net::Kind::Command && one.welcomed) {
            sim::Command command;
            if (!net::parse(body, command)) return false;
            // The world's door is the server's own.
            if (command.kind == sim::Command::Kind::Join || command.kind == sim::Command::Kind::Leave ||
                command.kind == sim::Command::Kind::PartySet) {
                continue;
            }
            // Sent to another world already (sentAway): nothing more of his here. A client behind
            // the server asked a paid trip again before it saw the first one's answer, and each
            // was paid -- a net bot 25 s behind at --speed 100 paid 3000 Zen every 10 s, 952
            // times in 18 game hours (the Claude bot's runs, 2026-10-09). A player on a slow
            // line who clicks twice is the same.
            if (one.sentAway) continue;
            // Nothing asked is nothing to send everyone; and no more than kMostCommands a tick.
            if (command.kind == sim::Command::Kind::None || ++one.asked > kMostCommands) continue;
            // Whatever the client wrote there, it is his own hero who asks.
            command.player = one.player;
            // One trip a tick: the landing is read from the Travel its answer's ticket names
            // (landingOf), and a refused trip's ticket on a second, granted one landed him where
            // the first asked -- Lost Tower 7 at Noria's price, no level, quest or wings asked.
            if (command.kind == sim::Command::Kind::Travel &&
                std::any_of(one.world->queued.begin(), one.world->queued.end(), [&](const sim::Command& c) {
                    return c.kind == sim::Command::Kind::Travel && c.player == one.player;
                })) {
                continue;
            }
            if (command.kind == sim::Command::Kind::Order) {
                const sim::Body* him = one.world->realm->find(one.player);
                if (him != nullptr && !him->walking) one.world->wantsEarly = true;
            }
            one.world->queued.push_back(command);
        } else if (kind == net::Kind::PartyAsk && one.welcomed) {
            net::PartyAsk asked;
            if (!net::parse(body, asked)) return false;
            partyAsk(one, asked, sessions);
        } else if (kind == net::Kind::Chat && one.welcomed) {
            net::Say said;
            if (!net::parse(body, said)) return false;
            say(one, said, sessions);
        } else if (kind == net::Kind::Ping) {
            // Back the moment it is read, ahead of any tick: the line's round trip alone.
            net::Ping ping;
            if (!net::parse(body, ping)) return false;
            std::vector<uint8_t> out;
            net::put(out, ping);
            if (!one.socket.send(out)) return false;
        } else if ((kind == net::Kind::Command || kind == net::Kind::Chat || kind == net::Kind::PartyAsk) &&
                   one.joining != 0) {
            // Asked or said before his Welcome: he has no body yet to ask with, and nobody hears
            // him yet. Not a frame out of turn: a client quick on the keys after its Hello.
            continue;
        } else {
            core::logError("%s: a frame of kind %d out of turn", one.who.c_str(), int(kind));
            return false;
        }
    }
}


}  // namespace mu::server

using namespace mu;
using namespace mu::server;

int main(int argc, char** argv) {
    int port = net::kDefaultPort;
    std::string assets = MU2_ASSET_DIR;
    std::string store = "characters.db";
    sim::RealmConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "--store" && i + 1 < argc) store = argv[++i];
        else if (a == "--speed" && i + 1 < argc) {
            g_speed = std::max(1, std::atoi(argv[++i]));
            g_tick /= g_speed;
        }
        else if (a == "--storm") g_storm = true;
        else if (a == "--invasion") g_invasion = true;
        else if (a == "--map-cap" && i + 1 < argc) g_mapCap = std::max(1, std::atoi(argv[++i]));
        else if (a == "--castle-period" && i + 1 < argc) {
            const int s = std::max(2, std::atoi(argv[++i]));
            config.castle = {s, s / 2, s / 2};
        } else if (a == "--kit" && i + 1 < argc) {
            // LEVEL:STEM[+N],STEM... -- every new character made so (a test).
            const std::string kit = argv[++i];
            const size_t colon = kit.find(':');
            g_kitLevel = std::clamp(std::atoi(kit.c_str()), 1, sim::kMaximumLevel);
            for (size_t at = colon == std::string::npos ? kit.size() : colon + 1; at < kit.size();) {
                size_t comma = kit.find(',', at);
                if (comma == std::string::npos) comma = kit.size();
                std::string one = kit.substr(at, comma - at);
                int plus = 0;
                if (const size_t p = one.find('+'); p != std::string::npos) {
                    plus = std::atoi(one.c_str() + p + 1);
                    one.resize(p);
                }
                if (!one.empty()) g_kitItems.push_back({one, plus});
                at = comma + 1;
            }
        } else if (a == "--square-period" && i + 1 < argc) {
            // Charon's gate every S seconds for S/2, not two-hourly (a test).
            const int s = std::max(2, std::atoi(argv[++i]));
            config.square = {s, s / 2, s / 2};
        } else {
            std::fprintf(stderr, "usage: mu2_server [--port N] [--assets DIR] [--store FILE] [--castle-period S] [--square-period S] [--kit LEVEL:STEM+N,...] [--storm] [--invasion] [--map-cap N] [--speed N]\n");
            return 2;
        }
    }
    // A line at a time: under systemd stdout is a pipe to the journal, fully buffered otherwise,
    // and who joined would not be seen until kilobytes later.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::string error;
    // A server that cannot keep its characters does not open: whatever was played on it would
    // be lost.
    if (!g_store.open(store, error)) {
        core::logError("%s", error.c_str());
        return 1;
    }
    // The roster's item rows (g_items): a server without them stands the screen's characters bare.
    if (!content::loadTables(assets + "/cooked/lorencia/lorencia.mur", g_items, error)) {
        core::logError("lorencia's tables: %s; the character screen's figures go bare", error.c_str());
    }
    net::Socket listener;
    if (!listener.listen(port, error)) {
        core::logError("%s", error.c_str());
        return 1;
    }
    if (!g_udp.bind(port, error)) core::logError("%s: the ticks go down the stream alone", error.c_str());
    core::logf("mu2_server: protocol %u, listening on %d%s, tables from %s, %d characters in %s",
               net::kVersion, port, g_udp.open() ? " (and udp)" : "", assets.c_str(), g_store.count(),
               store.c_str());

    // A token is all a character's login is until accounts: not one the clock would guess.
    std::random_device entropy;
    std::mt19937_64 seeds((uint64_t(entropy()) << 32) ^ entropy() ^ uint64_t(std::time(nullptr)));
    std::vector<std::unique_ptr<World>> worlds;
    std::vector<std::unique_ptr<Session>> sessions;
    // What the last wait said of each session, in its order then: whether it has something to
    // read (or has gone). One arrived since is read regardless.
    std::vector<bool> ready;
    std::vector<pollfd> watch;
    uint64_t arrivals = 0;
    while (!g_stop) {
        // Where this pass's time goes (g_stats): the poll's wait is not counted.
        const Clock::time_point passAt = Clock::now();
        const double passCpuAt = cpuMs();
        // Who arrived, and what each that spoke said.
        for (net::Socket s = listener.accept(); s.open(); s = listener.accept()) {
            auto one = std::make_unique<Session>();
            one->socket = std::move(s);
            one->who = "player " + std::to_string(++arrivals);
            do one->udpKey = seeds(); while (one->udpKey == 0);
            core::logf("%s: connected", one->who.c_str());
            sessions.push_back(std::move(one));
        }
        // Each UDP socket that says its key is where that player's datagrams go: said again a
        // second later, and from a new address when his router moves him.
        std::vector<uint8_t> datagram;
        net::Address from;
        while (g_udp.receive(datagram, &from)) {
            uint64_t key = 0;
            if (!net::takeBind(datagram, key)) continue;
            for (auto& one : sessions) {
                if (one->udpKey != key) continue;
                if (!one->udpKnown || !(one->udpAt == from)) {
                    core::logf("%s: his ticks by udp as well", one->who.c_str());
                }
                one->udpAt = from;
                one->udpKnown = true;
            }
        }
        for (size_t i = 0; i < sessions.size(); ++i) {
            Session& one = *sessions[i];
            if (i < ready.size() && !ready[i]) continue;
            if (one.socket.open() && !hear(one, worlds, sessions, assets, config, seeds)) bye(one, net::Why::Protocol);
        }
        const double heardMs = msSince(passAt);
        // Step every world on its deadline, owing ticks rather than dropping them, and flush.
        const auto now = Clock::now();
        const Clock::time_point ticksAt = now;
        // The slowest world's ticks this pass, for the slow-pass line.
        double worldMost = 0.0;
        std::string worldMostName;
        int stepped = 0;
        const auto timedTick = [&](World& world, bool early) {
            const double late = std::chrono::duration<double, std::milli>(now - world.next).count();
            if (!early && late > 0.0) {
                g_stats.lateMost = std::max(g_stats.lateMost, late);
                if (late >= std::chrono::duration<double, std::milli>(g_tick).count()) ++g_stats.behind;
            }
            const Clock::time_point at = Clock::now();
            tick(world, sessions, worlds, early);
            const double ms = msSince(at);
            ++g_stats.ticks;
            ++stepped;
            g_stats.tickMs += ms;
            const std::string name = world.name + (world.copy > 1 ? "/" + std::to_string(world.copy) : "");
            if (ms > g_stats.tickMost) {
                g_stats.tickMost = ms;
                g_stats.tickMostWorld = name;
            }
            if (ms > worldMost) {
                worldMost = ms;
                worldMostName = name;
            }
        };
        for (auto& world : worlds) {
            takePacked(*world);
            const bool early = world->wantsEarly && world->next - now > kEarlyWithin &&
                               now - world->earlyAt >= std::chrono::duration<double>(kEarlyApart);
            world->wantsEarly = false;
            if (early) {
                timedTick(*world, true);
                world->earlyAt = now;
                world->next = now + g_tick;
            }
        }
        // What each world owes, a tick of each in turn (sprint 25): after a stall one world's
        // twenty went back to back while every other world waited behind them.
        for (int owed = 0; owed < 20; ++owed) {
            bool any = false;
            for (auto& world : worlds) {
                if (world->next > now) continue;
                timedTick(*world, false);
                world->next += g_tick;
                any = true;
            }
            if (!any) break;
        }
        for (auto& world : worlds) {
            if (world->next < now) world->next = now;  // more than a second behind: start again
        }
        const double ticksMs = msSince(ticksAt);
        // The parties' upkeep, each second (partyTick).
        const Clock::time_point partyStarted = Clock::now();
        static Clock::time_point partyAt = now;
        if (now - partyAt >= std::chrono::seconds(1)) {
            partyTick(sessions);
            partyAt = now;
        }
        const double partyMs = msSince(partyStarted);
        // Whoever's minute it is, written in one go.
        const Clock::time_point savesAt = Clock::now();
        std::vector<server::Store::Row> due;
        for (auto& one : sessions) {
            if (one->world == nullptr || !one->welcomed || one->keepAt > now) continue;
            due.push_back(placeOf(*one));
            one->keepAt = now + g_tick * kKeepEvery;
        }
        if (!due.empty()) {
            g_store.keep(due);
            g_stats.saved += int(due.size());
        }
        const double savesMs = msSince(savesAt);
        g_stats.saveMost = std::max(g_stats.saveMost, savesMs);
        const Clock::time_point sendAt = Clock::now();
        for (auto& one : sessions) {
            if (!one->socket.open()) continue;
            if (!one->socket.flush()) {
                one->socket.close();
            } else if (one->socket.stalledSeconds() > kStalledSeconds || one->socket.waiting() > kMostOwed) {
                core::logError("%s: has taken nothing for %.0f s, %zu KB owed: let go", one->who.c_str(),
                               one->socket.stalledSeconds(), one->socket.waiting() / 1024);
                bye(*one, net::Why::Behind);
            }
        }
        const double sendMs = msSince(sendAt);
        const Clock::time_point goneAt = Clock::now();
        for (size_t i = 0; i < sessions.size();) {
            if (!sessions[i]->socket.open()) {
                core::logf("%s: gone", sessions[i]->who.c_str());
                part(*sessions[i]);
                sessions.erase(sessions.begin() + long(i));
            } else {
                ++i;
            }
        }
        // Worlds are never let go: the server is always online, and a world once raised keeps
        // its monsters, its weather and its state for as long as the server runs -- but for an
        // event's (a Blood Castle's, a Devil Square's run), which is one run's and nobody joins
        // again once it has begun: let go when nobody is in it, or every run left a world ticking.
        for (size_t i = 0; i < worlds.size();) {
            World& w = *worlds[i];
            if ((w.castle > 0 || w.square > 0) && peopleIn(w, sessions) == 0) {
                core::logf("%s: let go, nobody in it (castle %d, square %d)", w.name.c_str(), w.castle,
                           w.square);
                for (auto it = g_partyBody.begin(); it != g_partyBody.end();) {
                    it = it->second.first == &w ? g_partyBody.erase(it) : std::next(it);
                }
                worlds.erase(worlds.begin() + long(i));
            } else {
                ++i;
            }
        }
        const double goneMs = msSince(goneAt);
        // The pass's time, and where it went when it was long.
        const double passMs = msSince(passAt);
        g_stats.passMost = std::max(g_stats.passMost, passMs);
        if (passMs > kSlowPass) {
            const double passCpu = cpuMs() - passCpuAt;
            ++g_stats.slowPasses;
            if (passCpu < passMs / 2) ++g_stats.slowWaited;
            core::logf("slow pass %.1f ms, %.1f on the cpu: heard %.1f, ticks %.1f (%d, the longest %s %.1f), party %.1f, "
                       "saves %.1f (%zu), sent %.1f, gone %.1f",
                       passMs, passCpu, heardMs, ticksMs, stepped, worldMostName.empty() ? "-" : worldMostName.c_str(),
                       worldMost, partyMs, savesMs, due.size(), sendMs, goneMs);
        }
        // Each minute, how the loop kept time.
        if (Clock::now() - g_stats.since >= kStatsEvery) {
            const double seconds = std::chrono::duration<double>(Clock::now() - g_stats.since).count();
            int players = 0, bots = 0;
            for (const auto& one : sessions) {
                if (!one->welcomed || !one->socket.open()) continue;
                ++players;
                bots += one->bot;
            }
            core::logf("timing: %d players (%d bots), %zu worlds | %d ticks, %.3f ms mean, %.2f most (%s) | "
                       "pass most %.1f ms, %d slow (%d held up) | late most %.1f ms, %d a tick behind | out %.1f KB/s stream, "
                       "%.1f KB/s udp | %d saved, %.2f ms most | snapshot %.2f ms most, %d packed",
                       players, bots, worlds.size(), g_stats.ticks,
                       g_stats.ticks > 0 ? g_stats.tickMs / g_stats.ticks : 0.0, g_stats.tickMost,
                       g_stats.tickMostWorld.empty() ? "-" : g_stats.tickMostWorld.c_str(), g_stats.passMost,
                       g_stats.slowPasses, g_stats.slowWaited, g_stats.lateMost, g_stats.behind, g_stats.streamBytes / 1024.0 / seconds,
                       g_stats.udpBytes / 1024.0 / seconds, g_stats.saved, g_stats.saveMost, g_stats.snapshotMost,
                       g_stats.packs);
            g_stats = Stats{};
        }
        // Then asleep until a line speaks or the next world's tick is due (sprint 24). It was a
        // 2 ms nap and a read of every line after it, 500 times a second whether anyone spoke:
        // ~0.5 us a line a pass, 13% of the box at 500 players, and up to 2 ms on an order
        // before it was even read. The order that sets a player off now takes its tick at once.
        auto wake = Clock::now() + std::chrono::milliseconds(100);
        for (const auto& world : worlds) wake = std::min(wake, world->next);
        watch.clear();
        watch.push_back({listener.fd(), POLLIN, 0});
        for (const auto& one : sessions) {
            const short wants = short(POLLIN | (one->socket.waiting() > 0 ? POLLOUT : 0));
            watch.push_back({one->socket.fd(), wants, 0});
        }
        if (g_udp.open()) watch.push_back({g_udp.fd(), POLLIN, 0});
        const auto left = std::max<Clock::duration>(Clock::duration::zero(), wake - Clock::now());
#ifdef __linux__
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(left).count();
        const timespec timeout{time_t(ns / 1000000000), long(ns % 1000000000)};
        ppoll(watch.data(), nfds_t(watch.size()), &timeout, nullptr);
#else
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(left).count();
        poll(watch.data(), nfds_t(watch.size()), int((us + 999) / 1000));
#endif
        ready.assign(sessions.size(), false);
        for (size_t i = 0; i < sessions.size(); ++i) ready[i] = watch[i + 1].revents != 0;
    }
    keepEveryone(sessions);
    for (auto& one : sessions) bye(*one, net::Why::Stopping);
    core::logf("mu2_server: stopped, %zu connected", sessions.size());
    return 0;
}
