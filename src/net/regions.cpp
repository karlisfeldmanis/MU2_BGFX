#include "net/regions.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#include "core/log.h"
#include "net/socket.h"
#include "net/wire.h"

namespace mu::net {

const std::vector<Region>& regions() {
    // The Hetzner box (kDefaultHost), the game's own default.
    static const std::vector<Region> all = {
        {"eu-north", "North Europe", "Helsinki", "mu-classic.com"},
        // Hetzner Nuremberg (server/README.md, the Central Europe box), for the Balkans and the south.
        {"eu-central", "Central Europe", "Nuremberg", "178.104.232.133"},
        // Vultr São Paulo (server/README.md, the South America box).
        {"sa-east", "South America", "Sao Paulo", "216.238.127.33"},  // no ã in the launcher's face
    };
    return all;
}

const Region& regionNamed(const std::string& id) {
    for (const Region& one : regions()) {
        if (one.id == id) return one;
    }
    return regions().front();
}

int regionOfServer(const std::string& server) {
    const std::string host = server.substr(0, server.rfind(':'));
    const std::string asked = host.empty() ? kDefaultHost : host;
    for (size_t i = 0; i < regions().size(); ++i) {
        const std::string& one = regions()[i].server;
        if (one.substr(0, one.rfind(':')) == asked) return int(i);
    }
    return -1;
}

double pingMs(const Region& region, double seconds) {
    const size_t colon = region.server.rfind(':');
    const std::string host = colon == std::string::npos ? region.server : region.server.substr(0, colon);
    const int port = colon == std::string::npos ? kDefaultPort : std::atoi(region.server.c_str() + colon + 1);
    double best = -1.0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        Socket socket;
        std::string error;
        const auto from = std::chrono::steady_clock::now();
        if (!socket.connect(host, port, seconds, error)) continue;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
        if (best < 0.0 || ms < best) best = ms;
    }
    return best;
}

std::string quickest(const std::vector<double>& pings) {
    std::string id;
    double best = -1.0;
    for (size_t i = 0; i < pings.size() && i < regions().size(); ++i) {
        if (pings[i] >= 0.0 && (best < 0.0 || pings[i] < best)) {
            best = pings[i];
            id = regions()[i].id;
        }
    }
    return id;
}

int nearer(const std::vector<double>& pings, int here, double by) {
    if (here < 0 || size_t(here) >= pings.size() || pings[size_t(here)] < 0.0) return -1;
    int best = -1;
    for (size_t i = 0; i < pings.size(); ++i) {
        if (int(i) == here || pings[i] < 0.0) continue;
        if (best < 0 || pings[i] < pings[size_t(best)]) best = int(i);
    }
    return best >= 0 && pings[size_t(best)] + by <= pings[size_t(here)] ? best : -1;
}

namespace {

// Shared with the thread, which may outlive whoever asked: the process's, never freed.
struct PingAll {
    std::mutex lock;
    bool started = false, done = false;
    std::vector<double> pings;
};
PingAll& pingAll() {
    static PingAll* all = new PingAll;
    return *all;
}

}  // namespace

void pingAllOnce() {
    PingAll& all = pingAll();
    {
        std::lock_guard<std::mutex> hold(all.lock);
        if (all.started) return;
        all.started = true;
    }
    std::thread([&all] {
        std::vector<double> pings;
        std::string line;
        for (const Region& one : regions()) {
            pings.push_back(pingMs(one));
            char ms[64];
            std::snprintf(ms, sizeof ms, "%s%s %.0f ms", line.empty() ? "" : ", ", one.name.c_str(), pings.back());
            line += ms;
        }
        core::logf("regions: timed from here, %s", line.c_str());
        std::lock_guard<std::mutex> hold(all.lock);
        all.pings = std::move(pings);
        all.done = true;
    }).detach();
}

bool pingAllTaken(std::vector<double>* pings) {
    PingAll& all = pingAll();
    std::lock_guard<std::mutex> hold(all.lock);
    if (!all.done) return false;
    *pings = all.pings;
    return true;
}

}  // namespace mu::net
