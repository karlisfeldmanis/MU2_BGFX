// The website's accounts at the server's door (docs/plans/launcher.md, L1; docs/plans/security.md).
//
// A player who comes in through the launcher says a pass where the machine key went (net/pass.h):
// the ticket the website gave the launcher, a count, and a proof made with the ticket's secret. The
// first time a ticket is seen, the server asks the website, on this machine, whose it is and what
// its secret is -- the website tells that once per ticket -- and from there on he is
// `account000000042`, the key his characters are kept under. Everything past the door -- the
// roster, the Hello's check, the Broker's own account -- sees only that key.
//
// The asking is a job, never the loop's: a few milliseconds when the website is well, seconds when
// it is not, and the loop must not wait either. The secret is kept for kPassFor after its last
// proof, in memory and in characters.db (Store::keepPass), so a player changing maps, each a new
// line with a new Hello and a count one higher, is checked here and never asked about again --
// across a restart of the server too. A count no higher than the last proven is a line someone
// read and said again, and is turned away.

#include "host.h"

#include <arpa/inet.h>
#include <atomic>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace mu::server {

namespace {

std::string g_host;   // the website's account service; empty when the server takes no passes
int g_port = 0;

constexpr int64_t kPassFor = 24 * 3600;  // seconds after its last proof
constexpr size_t kMostRemembered = 20000;
// By ticket: the disk's rows (Store::PassRow) as they were last read or written. `count` is the
// highest proven, and a pass must come with a higher one.
std::unordered_map<std::string, Store::PassRow> g_known;
// The website's answers still coming (Session::signing), and those whose line went before they
// came: kept here till they come, since a std::async future waits on its destruction, and a stream
// of made-up passes, each line closed at once, held the loop's one thread three seconds each. Past
// kMostAsking at once a new pass is turned away.
std::atomic<int> g_asking{0};
std::vector<std::future<Redeemed>> g_letGo;
constexpr int kMostAsking = 64;

int64_t now() { return int64_t(std::time(nullptr)); }

// The ticket's row, from memory or else the disk (a restart): nullptr for one neither keeps.
Store::PassRow* knownPass(const std::string& ticket) {
    auto it = g_known.find(ticket);
    if (it == g_known.end()) {
        Store::PassRow row;
        if (!g_store.findPass(ticket, row)) return nullptr;
        it = g_known.emplace(ticket, row).first;
    }
    if (it->second.until < now()) {
        g_known.erase(it);
        return nullptr;
    }
    return &it->second;
}

const std::string kUnknown = "";
const std::string kAway = "!";

// A JSON field's text, `"name": "value"`, or "".
std::string field(const std::string& said, const char* name) {
    const size_t at = said.find("\"" + std::string(name) + "\"");
    if (at == std::string::npos) return {};
    const size_t open = said.find('"', said.find(':', at) + 1);
    if (open == std::string::npos) return {};
    const size_t close = said.find('"', open + 1);
    return close == std::string::npos ? std::string() : said.substr(open + 1, close - open - 1);
}

// One GET on this machine, read to its end. The service is Python's http.server, so HTTP/1.0 and a
// closed line mark the end.
Redeemed ask(const std::string& host, int port, const std::string& ticket) {
    const Redeemed away{kAway, {}};
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return away;
    timeval wait{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(uint16_t(port));
    if (::inet_pton(AF_INET, host.c_str(), &at.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0) {
        ::close(fd);
        return away;
    }
    const std::string asked = "GET /internal/ticket?ticket=" + ticket + " HTTP/1.0\r\nHost: " + host + "\r\n\r\n";
    if (::send(fd, asked.data(), asked.size(), 0) != ssize_t(asked.size())) {
        ::close(fd);
        return away;
    }
    std::string said;
    char buffer[2048];
    for (ssize_t got; (got = ::recv(fd, buffer, sizeof buffer, 0)) > 0 && said.size() < 16384;) {
        said.append(buffer, size_t(got));
    }
    ::close(fd);
    // "HTTP/1.0 200 OK ... {"account": 42, "secret": "..."}", or a 404 for a ticket it does not keep.
    const size_t space = said.find(' ');
    if (space == std::string::npos) return away;
    const int status = std::atoi(said.c_str() + space + 1);
    if (status == 404) return {kUnknown, {}};
    if (status != 200) return away;
    const size_t named = said.find("\"account\"");
    if (named == std::string::npos) return away;
    const size_t digits = said.find_first_of("0123456789", named);
    if (digits == std::string::npos) return away;
    const long long id = std::atoll(said.c_str() + digits);
    const std::string secret = field(said, "secret");
    if (id <= 0 || !net::isSecret(secret)) return away;
    return {accountKeyOf(id), secret};
}

// The website's own sign-in, 64 hex digits: what the launcher said here before passes, and never
// again -- it would cross the line in the open (docs/plans/security.md).
bool isToken(const std::string& key) {
    if (key.size() != 64) return false;
    return key.find_first_not_of("0123456789abcdef") == std::string::npos;
}

void remember(const net::Pass& pass, const Redeemed& his) {
    if (g_known.size() >= kMostRemembered) {
        for (auto it = g_known.begin(); it != g_known.end();) {
            it = it->second.until < now() ? g_known.erase(it) : std::next(it);
        }
        if (g_known.size() >= kMostRemembered) g_known.clear();  // the disk has them still
    }
    Store::PassRow& row = g_known[pass.ticket];
    row = {pass.ticket, his.account, his.secret, pass.count, now() + kPassFor};
    if (!g_store.keepPass(row)) core::logError("passes: ticket %.6s not written", pass.ticket.c_str());
}

}  // namespace

std::string accountKeyOf(long long id) {
    char key[32];
    std::snprintf(key, sizeof key, "account%09lld", id);
    return key;
}

bool takesTokens() { return !g_host.empty(); }

void takeTokensFrom(const std::string& address) {
    // "http://127.0.0.1:8080", or "127.0.0.1:8080".
    std::string rest = address;
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    const size_t colon = rest.rfind(':');
    g_host = colon == std::string::npos ? rest : rest.substr(0, colon);
    g_port = colon == std::string::npos ? 80 : std::atoi(rest.c_str() + colon + 1);
    core::logf("accounts: tickets are asked of %s:%d", g_host.c_str(), g_port);
}

bool reservedKey(const std::string& key) { return key.rfind("account", 0) == 0; }

bool fromHere(int fd) {
    sockaddr_storage peer{};
    socklen_t size = sizeof peer;
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &size) != 0) return false;
    if (peer.ss_family == AF_INET) {
        return (ntohl(reinterpret_cast<sockaddr_in*>(&peer)->sin_addr.s_addr) >> 24) == 127;
    }
    if (peer.ss_family == AF_INET6) {
        const in6_addr& a = reinterpret_cast<sockaddr_in6*>(&peer)->sin6_addr;
        return IN6_IS_ADDR_LOOPBACK(&a) ||
               (IN6_IS_ADDR_V4MAPPED(&a) && a.s6_addr[12] == 127);
    }
    return false;
}

std::string addressOf(int fd) {
    sockaddr_storage peer{};
    socklen_t size = sizeof peer;
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &size) != 0) return "?";
    char text[INET6_ADDRSTRLEN] = {};
    if (peer.ss_family == AF_INET) {
        ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&peer)->sin_addr, text, sizeof text);
    } else if (peer.ss_family == AF_INET6) {
        ::inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&peer)->sin6_addr, text, sizeof text);
    }
    return text[0] != '\0' ? text : "?";
}

Door door(Session& one, std::string& key) {
    net::Pass pass;
    const bool passed = takesTokens() && net::parsePass(key, pass);
    if (g_launcherOnly && takesTokens() && !passed && !one.local) {
        core::logf("%s: a game started without the launcher, turned away", one.who.c_str());
        return Door::Refuse;
    }
    if (takesTokens() && isToken(key)) {
        core::logf("%s: the website's token said on the line, turned away (an old game)", one.who.c_str());
        return Door::Refuse;
    }
    if (!passed) return reservedKey(key) ? Door::Refuse : Door::Pass;
    if (Store::PassRow* known = knownPass(pass.ticket)) {
        if (!net::proven(pass, known->secret) || pass.count <= known->count) {
            core::logError("%s: a pass of ticket %.6s that does not prove itself (count %llu, last %llu)",
                           one.who.c_str(), pass.ticket.c_str(), (unsigned long long)pass.count,
                           (unsigned long long)known->count);
            return Door::Refuse;
        }
        known->count = pass.count;
        known->until = now() + kPassFor;
        if (!g_store.keepPass(*known)) core::logError("passes: ticket %.6s not written", pass.ticket.c_str());
        key = known->account;
        return Door::Pass;
    }
    if (g_asking.load() >= kMostAsking) {
        core::logError("%s: %d passes already asked of the website, turned away", one.who.c_str(), kMostAsking);
        return Door::Refuse;
    }
    const std::string host = g_host, ticket = pass.ticket;
    const int port = g_port;
    ++g_asking;
    one.signing = std::async(std::launch::async, [host, port, ticket] {
        Redeemed his = ask(host, port, ticket);
        --g_asking;
        return his;
    });
    one.signingPass = pass;
    return Door::Wait;
}

namespace {

// One POST on this machine, its answer not read past the status: the website's /internal/online.
void post(const std::string& host, int port, const std::string& path, const std::string& body) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    timeval wait{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(uint16_t(port));
    if (::inet_pton(AF_INET, host.c_str(), &at.sin_addr) == 1 &&
        ::connect(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0) {
        const std::string asked = "POST " + path + " HTTP/1.0\r\nHost: " + host +
                                  "\r\nContent-Type: application/json\r\nContent-Length: " +
                                  std::to_string(body.size()) + "\r\n\r\n" + body;
        if (::send(fd, asked.data(), asked.size(), 0) == ssize_t(asked.size())) {
            char buffer[256];
            ::recv(fd, buffer, sizeof buffer, 0);
        }
    }
    ::close(fd);
}

// A name as a JSON string: the server's names are letters and digits, but nothing is trusted.
std::string jsonText(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (uint8_t(c) >= 0x20) out += c;
    }
    return out + "\"";
}

// One item as the Online page's card shows it: its name, plus, luck, option, excellent lines,
// sockets and wear, by the rows' own words (sim::excellentLine, powerOf).
std::string itemJson(const sim::Held& held, const char* where) {
    if (held.item < 0 || size_t(held.item) >= g_items.items.size()) return {};
    const content::ItemRow& row = g_items.items[size_t(held.item)];
    std::string out = "{\"where\":" + jsonText(where) + ",\"name\":" + jsonText(row.label.empty() ? row.name : row.label) +
                      ",\"plus\":" + std::to_string(held.refinement) + ",\"luck\":" + (held.luck ? "true" : "false") +
                      ",\"skill\":" + (held.skill ? "true" : "false") + ",\"durability\":" + std::to_string(held.durability);
    if (held.option > 0 && sim::takesOptions(row)) out += ",\"option\":" + std::to_string(sim::optionValue(row, held.option));
    if (held.excellent != 0) {
        out += ",\"excellent\":[";
        bool first = true;
        for (int bit = 0; bit < 6; ++bit) {
            if (!(held.excellent & (1 << bit))) continue;
            out += (first ? "" : ",") + jsonText(sim::excellentLine(row, bit));
            first = false;
        }
        out += "]";
    }
    if (held.sockets > 0) {
        out += ",\"sockets\":[";
        for (int s = 0; s < held.sockets && s < 3; ++s) {
            const sim::PowerRow* power = held.powers[s] ? sim::powerOf(held.powers[s]) : nullptr;
            out += (s ? "," : "") + jsonText(power ? power->name : "empty");
        }
        out += "]";
    }
    if (held.bound) out += ",\"bound\":true";
    return out + "}";
}

// What a kept character is: his points, his experience and Zen, what he wears and what he carries.
std::string heroJson(const sim::HeroRecord& hero) {
    static const char* const kWhere[sim::kWorn] = {"Right hand", "Left hand", "Helm", "Armour", "Pants",
                                                   "Gloves", "Boots", "Wings", "Pet", "Pendant",
                                                   "Right ring", "Left ring", "Mount"};
    std::string out = ",\"second\":" + std::string(sim::promoted(hero.quests, int(hero.kin)) ? "true" : "false") +
                      ",\"experience\":" + std::to_string(hero.experience) + ",\"zen\":" + std::to_string(hero.money) +
                      ",\"points\":{\"strength\":" + std::to_string(hero.points.strength) +
                      ",\"agility\":" + std::to_string(hero.points.agility) +
                      ",\"vitality\":" + std::to_string(hero.points.vitality) +
                      ",\"energy\":" + std::to_string(hero.points.energy) +
                      ",\"command\":" + std::to_string(hero.points.command) +
                      ",\"free\":" + std::to_string(hero.pointsInHand) + "},\"worn\":[";
    bool first = true;
    for (int slot = 0; slot < sim::kWorn; ++slot) {
        const std::string item = itemJson(hero.slots[slot], kWhere[slot]);
        if (item.empty()) continue;
        out += (first ? "" : ",") + item;
        first = false;
    }
    out += "],\"bag\":[";
    first = true;
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        const std::string item = itemJson(hero.slots[slot], "Bag");
        if (item.empty()) continue;
        out += (first ? "" : ",") + item;
        first = false;
    }
    return out + "]";
}

std::future<void> g_telling;
Clock::time_point g_toldAt{}, g_rollAt{};
constexpr auto kTellEvery = std::chrono::seconds(5);
// And every character of an account, for the All players list: a minute apart, and with the first.
constexpr auto kRollEvery = std::chrono::seconds(60);

}  // namespace

void tellOnline(const std::vector<std::unique_ptr<Session>>& sessions) {
    if (!takesTokens()) return;
    const auto now = Clock::now();
    if (now - g_toldAt < kTellEvery) return;
    // The last one still on its way (the website slow or gone): this one waits for the next turn.
    if (g_telling.valid() && g_telling.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    g_toldAt = now;
    std::string body = "{\"region\":" + jsonText(g_region) + ",\"players\":[";
    bool first = true;
    for (const auto& one : sessions) {
        if (!one->welcomed || !one->socket.open() || one->world == nullptr) continue;
        const sim::Body* him = one->world->realm->find(one->player);
        if (him == nullptr) continue;
        // account000000042 -> 42; a character of no account (a bot, a local run) 0.
        const long long id = reservedKey(one->playedFor) ? std::atoll(one->playedFor.c_str() + 7) : 0;
        char line[512];
        std::snprintf(line, sizeof line,
                      "%s{\"account\":%lld,\"name\":%s,\"class\":%s,\"level\":%d,\"world\":%s,\"copy\":%d,"
                      "\"castle\":%d,\"square\":%d,\"x\":%d,\"y\":%d,\"bot\":%s,\"ms\":%d}",
                      first ? "" : ",", id, jsonText(one->name).c_str(),
                      jsonText(sim::className(int(him->kin), him->second)).c_str(), int(him->level),
                      jsonText(one->world->name).c_str(), one->world->copy, one->world->castle,
                      one->world->square, int(him->x), int(him->y), one->bot ? "true" : "false",
                      one->pingMs > 0 ? one->pingMs : one->socket.rttMs());
        body += line;
        first = false;
    }
    body += "]";
    if (g_rollAt == Clock::time_point{} || now - g_rollAt >= kRollEvery) {
        g_rollAt = now;
        body += ",\"characters\":[";
        first = true;
        for (const Store::Roll& one : g_store.everyone()) {
            const long long id = reservedKey(one.account) ? std::atoll(one.account.c_str() + 7) : 0;
            if (id <= 0) continue;
            const bool second = one.read && sim::promoted(one.kept.hero.quests, int(one.kin));
            char line[384];
            std::snprintf(line, sizeof line,
                          "%s{\"account\":%lld,\"name\":%s,\"class\":%s,\"level\":%d,\"world\":%s,"
                          "\"saved\":%lld,\"fresh\":%s",
                          first ? "" : ",", id, jsonText(one.name).c_str(),
                          jsonText(sim::className(int(one.kin), second)).c_str(), one.level,
                          jsonText(one.world).c_str(), (long long)one.saved, one.fresh ? "true" : "false");
            body += line;
            if (one.read) body += heroJson(one.kept.hero);
            body += "}";
            first = false;
        }
        body += "]";
    }
    body += "}";
    const std::string host = g_host;
    const int port = g_port;
    g_telling = std::async(std::launch::async, [host, port, body] { post(host, port, "/internal/online", body); });
}

void tellRebirth(const std::string& name, const char* kind, int count) {
    if (!takesTokens()) return;
    const std::string body = "{\"region\":" + jsonText(g_region) + ",\"name\":" + jsonText(name) +
                             ",\"class\":" + jsonText(kind) + ",\"count\":" + std::to_string(count) + "}";
    const std::string host = g_host;
    const int port = g_port;
    // Its own thread, let go: a rebirth is rare, and the post gives up in 3 s.
    std::thread([host, port, body] { post(host, port, "/internal/rebirth", body); }).detach();
}

bool signed_(Session& one, std::string& account) {
    if (!one.signing.valid()) return false;
    if (one.signing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    const Redeemed his = one.signing.get();
    account = his.account;
    if (account == kAway) core::logError("%s: the website's accounts could not be asked", one.who.c_str());
    if (account == kUnknown) core::logf("%s: a ticket the website does not keep", one.who.c_str());
    if (account != kUnknown && account != kAway) {
        if (net::proven(one.signingPass, his.secret)) {
            remember(one.signingPass, his);
        } else {
            core::logError("%s: a pass of ticket %.6s that does not prove itself", one.who.c_str(),
                           one.signingPass.ticket.c_str());
            account = kUnknown;
        }
    }
    one.signingPass = {};
    return true;
}

void letGoOf(Session& one) {
    if (one.signing.valid()) g_letGo.push_back(std::move(one.signing));
}

void sweepLetGo() {
    g_letGo.erase(std::remove_if(g_letGo.begin(), g_letGo.end(),
                                 [](const std::future<Redeemed>& f) {
                                     return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                 }),
                  g_letGo.end());
}

}  // namespace mu::server
