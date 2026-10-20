// A character moved to another region's server (docs/game/transfer.md; the user, 2026-10-20: "we
// also need option to tranfer account to other realm in website for our players").
//
// The website keeps the move (web/server.py transfers) and only the servers reach it, through the
// accounts' own line: each 15 s this one asks /internal/transfers what is its to do and says each
// step back. The server he leaves hands over his bytes (out) and only then, at its next asking,
// marks him deleted on its disk (gone); between the two he is refused here (leavingRealm), so he
// is never on two servers at once. The server he goes to takes him in under his account and says
// done, or holds the move when his name is taken there or no slot is free. His vault stays: it is
// the account's on each server (Store::vaultOf).
//
// The asking and the telling are jobs, never the loop's; the store and the sessions are read and
// written here, on the loop's thread, when an answer has come.

#include "broker.h"
#include "host.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <future>
#include <random>
#include <set>
#include <sstream>

namespace mu::server {

namespace {

constexpr auto kAskEvery = std::chrono::seconds(15);

std::string g_at;  // the website's accounts: host
int g_atPort = 0;
std::future<std::string> g_asking;    // the work, as /internal/transfers said it
std::future<void> g_telling;          // the steps said back
Clock::time_point g_askedAt{};
std::set<uint64_t> g_leaving;         // handed over and not yet marked gone

// One request on this machine: the status, and the body after the headers.
int request(const std::string& host, int port, const std::string& head, const std::string& body, std::string& said) {
    said.clear();
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    timeval wait{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(uint16_t(port));
    if (::inet_pton(AF_INET, host.c_str(), &at.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0) {
        ::close(fd);
        return 0;
    }
    const std::string asked = head + " HTTP/1.0\r\nHost: " + host + "\r\nContent-Type: application/json\r\nContent-Length: " +
                              std::to_string(body.size()) + "\r\n\r\n" + body;
    if (::send(fd, asked.data(), asked.size(), 0) != ssize_t(asked.size())) {
        ::close(fd);
        return 0;
    }
    std::string all;
    char buffer[4096];
    for (ssize_t got; (got = ::recv(fd, buffer, sizeof buffer, 0)) > 0 && all.size() < (8u << 20);) {
        all.append(buffer, size_t(got));
    }
    ::close(fd);
    const size_t space = all.find(' '), end = all.find("\r\n\r\n");
    if (space == std::string::npos || end == std::string::npos) return 0;
    said = all.substr(end + 4);
    return std::atoi(all.c_str() + space + 1);
}

std::string encoded(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c)) out += char(c);
        else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string hexOf(const uint8_t* bytes, size_t size) {
    static const char* const kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 15];
    }
    return out;
}

bool bytesOf(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2) return false;
    for (size_t i = 0; i < hex.size(); i += 2) {
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(uint8_t(hi * 16 + lo));
    }
    return true;
}

// A name as a JSON string (the names are letters and digits; nothing is trusted).
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (uint8_t(c) >= 0x20) out += c;
    }
    return out + "\"";
}

std::string step(long long move, const char* state, const char* why = "", const std::string& kept = "") {
    return "{\"region\":" + quoted(g_region) + ",\"id\":" + std::to_string(move) + ",\"state\":\"" + state +
           "\",\"why\":\"" + why + "\",\"kept\":\"" + kept + "\"}";
}

// His row on this server, by account and name, or none.
bool his(const std::string& account, const std::string& name, Store::Listed& out) {
    for (const Store::Listed& one : g_store.list(account)) {
        if (one.name == name) {
            out = one;
            return true;
        }
    }
    return false;
}

// `give ID STATE ACCOUNT NAME`: one leaving this server.
void give(std::istringstream& line, std::vector<std::string>& said, std::vector<std::unique_ptr<Session>>& sessions) {
    long long move = 0, id = 0;
    std::string state, name;
    if (!(line >> move >> state >> id >> name) || id <= 0) return;
    const std::string account = accountKeyOf(id);
    Store::Listed him;
    const bool here = his(account, name, him);
    if (state == "out") {
        // The website has his bytes: he is marked gone here, his guild left first.
        if (!here) {
            said.push_back(step(move, "gone"));
            return;
        }
        g_leaving.insert(him.token);
        if (inPlay(sessions, him.token)) return;  // a restart let him in again; he goes once he is off
        guildLeave(him.token, sessions);
        if (!g_store.remove(account, him.token)) {
            core::logError("transfer %lld: %s not marked gone", move, name.c_str());
            return;
        }
        g_leaving.erase(him.token);
        core::logf("transfer %lld: %s (account %lld) has left for another region", move, name.c_str(), id);
        said.push_back(step(move, "gone"));
        return;
    }
    if (!here || him.fresh) {
        said.push_back(step(move, "failed", "missing"));
        return;
    }
    if (inPlay(sessions, him.token)) {
        said.push_back(step(move, "asked", "online"));
        return;
    }
    if (const uint32_t guild = guildOf(him.token); guild != 0) {
        const auto at = g_guilds.find(guild);
        if (at != g_guilds.end() && !at->second.members.empty() && at->second.members.front().token == him.token) {
            said.push_back(step(move, "failed", "master"));
            return;
        }
    }
    if (brokerHolds(him.token)) {
        said.push_back(step(move, "failed", "broker"));
        return;
    }
    sim::Kept kept;
    std::string world;
    if (!g_store.find(him.token, kept, world)) {
        said.push_back(step(move, "failed", "missing"));
        return;
    }
    kept.vault = {};  // the account's, and it stays
    std::vector<uint8_t> bytes;
    net::putKept(bytes, kept);
    char print[24];
    std::snprintf(print, sizeof print, "%016llx", (unsigned long long)g_items.itemsPrint());
    const std::string worldHex = world.empty() ? "-" : hexOf(reinterpret_cast<const uint8_t*>(world.data()), world.size());
    g_leaving.insert(him.token);
    core::logf("transfer %lld: %s (account %lld) handed over, %zu bytes", move, name.c_str(), id, bytes.size());
    said.push_back(step(move, "out", "",
                        std::to_string(net::kKeptLayout) + " " + print + " " + worldHex + " " + hexOf(bytes.data(), bytes.size())));
}

// `take ID ACCOUNT NAME LAYOUT ITEMS WORLD BYTES`: one coming to this server.
void take(std::istringstream& line, std::vector<std::string>& said, std::vector<std::unique_ptr<Session>>& sessions) {
    long long move = 0, id = 0;
    int layout = 0;
    std::string name, print, worldHex, hex;
    if (!(line >> move >> id >> name >> layout >> print >> worldHex >> hex) || id <= 0) return;
    const std::string account = accountKeyOf(id);
    Store::Listed already;
    if (his(account, name, already)) {  // taken in before, the done not heard
        said.push_back(step(move, "done"));
        return;
    }
    char mine[24];
    std::snprintf(mine, sizeof mine, "%016llx", (unsigned long long)g_items.itemsPrint());
    if (layout != net::kKeptLayout || print != mine) {
        static std::set<long long> told;
        if (told.insert(move).second) {
            core::logf("transfer %lld: %s waits for this server to be on his server's build", move, name.c_str());
        }
        return;
    }
    std::vector<uint8_t> bytes, worldBytes;
    sim::Kept kept;
    if (!bytesOf(hex, bytes) || !net::keptFrom(bytes, kept, layout) ||
        (worldHex != "-" && !bytesOf(worldHex, worldBytes))) {
        core::logError("transfer %lld: %s's bytes do not read", move, name.c_str());
        return;
    }
    if (!sim::goodName(name) || g_store.named(name) || nameInUse(name, sessions, nullptr)) {
        said.push_back(step(move, "held", "name"));
        return;
    }
    const int slot = freeSlot(account);
    if (slot < 0) {
        said.push_back(step(move, "held", "full"));
        return;
    }
    static std::mt19937_64 seeds{std::random_device{}()};
    uint64_t token = 0;
    do token = seeds(); while (token == 0 || g_store.has(token));
    std::string world(worldBytes.begin(), worldBytes.end());
    if (world.empty()) world = sim::homeWorld(kept.hero.kin);
    sim::Vault vault;
    if (g_store.vaultOf(account, vault)) kept.vault = vault;  // this server's vault of his account
    if (!g_store.create(token, account, name, kept.hero.kin, slot, world) ||
        !g_store.keep(Store::Row{token, world, kept})) {
        g_store.remove(account, token);
        core::logError("transfer %lld: %s not taken in", move, name.c_str());
        return;
    }
    core::logf("transfer %lld: %s (account %lld) came from another region, slot %d", move, name.c_str(), id, slot);
    said.push_back(step(move, "done"));
}

}  // namespace

void transferFrom(const std::string& address) {
    std::string rest = address;
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    const size_t colon = rest.rfind(':');
    g_at = colon == std::string::npos ? rest : rest.substr(0, colon);
    g_atPort = colon == std::string::npos ? 80 : std::atoi(rest.c_str() + colon + 1);
}

bool leavingRealm(uint64_t token) { return g_leaving.count(token) != 0; }

void transferTick(std::vector<std::unique_ptr<Session>>& sessions) {
    if (g_at.empty()) return;
    if (g_telling.valid()) {
        if (g_telling.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        g_telling.get();
    }
    if (g_asking.valid()) {
        if (g_asking.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const std::string work = g_asking.get();
        std::vector<std::string> said;
        std::istringstream lines(work);
        for (std::string text; std::getline(lines, text);) {
            std::istringstream line(text);
            std::string kind;
            line >> kind;
            if (kind == "give") give(line, said, sessions);
            else if (kind == "take") take(line, said, sessions);
        }
        if (!said.empty()) {
            const std::string host = g_at;
            const int port = g_atPort;
            g_telling = std::async(std::launch::async, [host, port, said] {
                for (const std::string& body : said) {
                    std::string answer;
                    if (request(host, port, "POST /internal/transfers", body, answer) != 200) {
                        core::logError("transfer: the website did not take %s", body.substr(0, 80).c_str());
                    }
                }
            });
        }
        return;
    }
    const auto now = Clock::now();
    if (now - g_askedAt < kAskEvery) return;
    g_askedAt = now;
    const std::string host = g_at, path = "GET /internal/transfers?region=" + encoded(g_region);
    const int port = g_atPort;
    g_asking = std::async(std::launch::async, [host, port, path] {
        std::string said;
        return request(host, port, path, "", said) == 200 ? said : std::string();
    });
}

}  // namespace mu::server
