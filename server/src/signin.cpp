// The website's accounts at the server's door (docs/plans/launcher.md, L1).
//
// A player who comes in through the launcher says a token where the machine key went: 64 hex
// digits the website gave him for his email and password (web/server.py). The server asks the
// website, on this machine, whose it is, and from there on he is `account000000042`, the key his
// characters are kept under. Everything past the door -- the roster, the Hello's check, the
// Broker's own account -- sees only that key and never the token.
//
// The asking is a job, never the loop's: a few milliseconds when the website is well, seconds when
// it is not, and the loop must not wait either. What it learns is kept for kRememberFor, so a
// player changing maps, each a new line with a new Hello, is not asked about again.

#include "host.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <future>
#include <mutex>
#include <unordered_map>

namespace mu::server {

namespace {

std::string g_host;   // the website's account service; empty when the server takes no tokens
int g_port = 0;

constexpr auto kRememberFor = std::chrono::minutes(10);
constexpr size_t kMostRemembered = 20000;
struct Known {
    std::string account;
    Clock::time_point until;
};
std::unordered_map<std::string, Known> g_known;

// What the website said of a token: his account's key, kUnknown for a token it does not keep (never
// given, signed out, or past its 90 days), kAway when it could not be asked.
const std::string kUnknown = "";
const std::string kAway = "!";

// One GET on this machine, read to its end. The service is Python's http.server, so HTTP/1.0 and a
// closed line mark the end.
std::string ask(const std::string& host, int port, const std::string& token) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return kAway;
    timeval wait{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof wait);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(uint16_t(port));
    if (::inet_pton(AF_INET, host.c_str(), &at.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0) {
        ::close(fd);
        return kAway;
    }
    const std::string asked = "GET /internal/session?token=" + token + " HTTP/1.0\r\nHost: " + host + "\r\n\r\n";
    if (::send(fd, asked.data(), asked.size(), 0) != ssize_t(asked.size())) {
        ::close(fd);
        return kAway;
    }
    std::string said;
    char buffer[2048];
    for (ssize_t got; (got = ::recv(fd, buffer, sizeof buffer, 0)) > 0 && said.size() < 16384;) {
        said.append(buffer, size_t(got));
    }
    ::close(fd);
    // "HTTP/1.0 200 OK ... {"account": 42}", or a 404 for a token it does not keep.
    const size_t space = said.find(' ');
    if (space == std::string::npos) return kAway;
    const int status = std::atoi(said.c_str() + space + 1);
    if (status == 404) return kUnknown;
    if (status != 200) return kAway;
    const size_t field = said.find("\"account\"");
    if (field == std::string::npos) return kAway;
    const size_t digits = said.find_first_of("0123456789", field);
    if (digits == std::string::npos) return kAway;
    const long long id = std::atoll(said.c_str() + digits);
    if (id <= 0) return kAway;
    return accountKeyOf(id);
}

bool lookedUp(const std::string& token, std::string& account) {
    const auto it = g_known.find(token);
    if (it == g_known.end()) return false;
    if (Clock::now() > it->second.until) {
        g_known.erase(it);
        return false;
    }
    account = it->second.account;
    return true;
}

void remember(const std::string& token, const std::string& account) {
    if (g_known.size() >= kMostRemembered) {
        const auto now = Clock::now();
        for (auto it = g_known.begin(); it != g_known.end();) {
            it = now > it->second.until ? g_known.erase(it) : std::next(it);
        }
        if (g_known.size() >= kMostRemembered) g_known.clear();
    }
    g_known[token] = Known{account, Clock::now() + kRememberFor};
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
    core::logf("accounts: tokens are asked of %s:%d", g_host.c_str(), g_port);
}

bool isToken(const std::string& key) {
    if (key.size() != 64) return false;
    for (char c : key) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
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

Door door(Session& one, std::string& key) {
    if (g_launcherOnly && takesTokens() && !isToken(key) && !one.local) {
        core::logf("%s: a game started without the launcher, turned away", one.who.c_str());
        return Door::Refuse;
    }
    if (!takesTokens() || !isToken(key)) return reservedKey(key) ? Door::Refuse : Door::Pass;
    std::string account;
    if (lookedUp(key, account)) {
        key = account;
        return Door::Pass;
    }
    const std::string host = g_host, token = key;
    const int port = g_port;
    one.signing = std::async(std::launch::async, [host, port, token] { return ask(host, port, token); });
    one.signingToken = key;
    return Door::Wait;
}

bool signed_(Session& one, std::string& account) {
    if (!one.signing.valid()) return false;
    if (one.signing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    account = one.signing.get();
    if (account != kUnknown && account != kAway) remember(one.signingToken, account);
    if (account == kUnknown) core::logf("%s: a token the website does not keep", one.who.c_str());
    if (account == kAway) core::logError("%s: the website's accounts could not be asked", one.who.c_str());
    one.signingToken.clear();
    return true;
}

}  // namespace mu::server
