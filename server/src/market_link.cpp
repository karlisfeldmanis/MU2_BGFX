// The line between the market's keeper and its hands (docs/game/shared-market.md, market_link.h).
//
// The keeper's server takes lines on --market-serve's port: each is challenged with a nonce and
// let in when it answers with the nonce's HMAC under the shared key (net::passProof, the game
// pass's own) and the same item list. A hands server keeps one line to it, opened on a job of its
// own so a keeper away never holds the loop, and again every 5 s while it is down.
//
// What may only go once it is on the disk waits for marketSaved, which the loop calls after the
// pass's saves (brokerKeep): the hands' durable asks (rule 1), and every answer of the keeper's
// (rule 2). The rest goes at once.
#include "market_link.h"

#include "broker.h"
#include "host.h"
#include "keeper.h"

#include <future>
#include <set>

#include "net/pass.h"

namespace mu::server {

// ---- the frames' bytes -------------------------------------------------------------------------

namespace {

struct Out {
    std::vector<uint8_t>& b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void blob(const std::vector<uint8_t>& v) {
        u32(uint32_t(v.size()));
        b.insert(b.end(), v.begin(), v.end());
    }
    void str(const std::string& s) { blob(std::vector<uint8_t>(s.begin(), s.end())); }
    void held(const sim::Held& h) {
        sim::Command c;
        sim::packHeld(h, c);
        i32(c.a);
        i32(c.b);
        i32(c.c);
        i32(c.d);
        u32(c.target);
    }
    void trader(const Trader& t) {
        u64(t.token);
        str(t.account);
        str(t.name);
        u8(t.bot);
        str(t.region);
    }
    void listing(const sim::Listing& l) {
        u32(l.id);
        u64(l.seller);
        str(l.account);
        str(l.name);
        held(l.what);
        i64(l.price);
        i64(l.floor);
        i32(l.secondsLeft);
        i64(l.listedAt);
    }
    void parcel(const sim::Parcel& p) {
        held(p.what);
        i64(p.zen);
        i64(p.at);
        u8(uint8_t(p.why));
    }
};

struct In {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) { return ok = ok && at + n <= b.size(); }
    uint8_t u8() { return need(1) ? b[at++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(b[at++]) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(b[at++]) << (8 * i);
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    int64_t i64() { return int64_t(u64()); }
    std::vector<uint8_t> blob() {
        const uint32_t n = u32();
        if (!need(n)) return {};
        std::vector<uint8_t> v(b.begin() + long(at), b.begin() + long(at + n));
        at += n;
        return v;
    }
    std::string str() {
        const std::vector<uint8_t> v = blob();
        return std::string(v.begin(), v.end());
    }
    // A count of things each at least `least` bytes: never more than are left.
    uint32_t count(size_t least) {
        const uint32_t n = u32();
        if (ok && size_t(n) * least > b.size() - at) ok = false;
        return ok ? n : 0;
    }
    sim::Held held() {
        sim::Command c;
        c.a = i32();
        c.b = i32();
        c.c = i32();
        c.d = i32();
        c.target = u32();
        return sim::unpackHeld(c);
    }
    Trader trader() {
        Trader t;
        t.token = u64();
        t.account = str();
        t.name = str();
        t.bot = u8() != 0;
        t.region = str();
        return t;
    }
    sim::Listing listing() {
        sim::Listing l;
        l.id = u32();
        l.seller = u64();
        l.account = str();
        l.name = str();
        l.what = held();
        l.price = i64();
        l.floor = i64();
        l.secondsLeft = i32();
        l.listedAt = i64();
        return l;
    }
    sim::Parcel parcel() {
        sim::Parcel p;
        p.what = held();
        p.zen = i64();
        p.at = i64();
        p.why = sim::Parcel::Why(u8());
        return p;
    }
    bool done() const { return ok && at == b.size(); }
};

// A BrokerAsk's and a BrokerView's bodies, as broker_wire writes them in a game frame: its
// length and kind left off.
template <class T>
std::vector<uint8_t> gameBody(const T& one) {
    std::vector<uint8_t> frame;
    net::put(frame, one);
    return frame.size() < 5 ? std::vector<uint8_t>{} : std::vector<uint8_t>(frame.begin() + 5, frame.end());
}

void putAsk(Out& o, const MarketAsk& one) {
    o.u8(uint8_t(one.op));
    o.u64(one.serial);
    o.trader(one.who);
    o.blob(gameBody(one.ask));
    o.i64(one.floor);
    o.listing(one.listing);
    o.u64(one.hold);
    o.u32(uint32_t(one.answers.size()));
    for (const int32_t a : one.answers) o.i32(a);
    o.u32(uint32_t(one.parcels.size()));
    for (const sim::Parcel& p : one.parcels) o.parcel(p);
    o.blob(one.book);
}

bool takeAsk(In& r, MarketAsk& out) {
    out.op = MarketOp(r.u8());
    out.serial = r.u64();
    out.who = r.trader();
    if (!r.ok || !net::parse(r.blob(), out.ask)) return false;
    out.floor = r.i64();
    out.listing = r.listing();
    out.hold = r.u64();
    out.answers.resize(r.count(4));
    for (int32_t& a : out.answers) a = r.i32();
    out.parcels.resize(r.count(37));
    for (sim::Parcel& p : out.parcels) p = r.parcel();
    out.book = r.blob();
    return r.ok && out.op >= MarketOp::View && out.op <= MarketOp::Merge;
}

}  // namespace

void putLink(std::vector<uint8_t>& out, LinkKind kind, const std::vector<uint8_t>& body) {
    Out o{out};
    o.u32(uint32_t(body.size() + 1));
    o.u8(uint8_t(kind));
    out.insert(out.end(), body.begin(), body.end());
}

int takeLink(const std::vector<uint8_t>& buffer, size_t& at, LinkKind& kind, std::vector<uint8_t>& body) {
    if (buffer.size() - at < 4) return 0;
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= uint32_t(buffer[at + size_t(i)]) << (8 * i);
    if (length < 1 || length > kMostLinkFrame) return -1;
    if (buffer.size() - at - 4 < length) return 0;
    kind = LinkKind(buffer[at + 4]);
    body.assign(buffer.begin() + long(at + 5), buffer.begin() + long(at + 4 + length));
    at += 4 + length;
    return 1;
}

std::vector<uint8_t> bodyOf(const MarketAsk& one) {
    std::vector<uint8_t> b;
    Out o{b};
    putAsk(o, one);
    return b;
}

std::vector<uint8_t> bodyOf(const MarketAnswer& one) {
    std::vector<uint8_t> b;
    Out o{b};
    o.u64(one.serial);
    o.u8(one.no);
    o.blob(gameBody(one.view));
    o.u32(uint32_t(one.listings.size()));
    for (const sim::Listing& l : one.listings) o.listing(l);
    o.u32(uint32_t(one.parcels.size()));
    for (const sim::Parcel& p : one.parcels) o.parcel(p);
    return b;
}

std::vector<uint8_t> bodyOf(const MarketNews& one) {
    std::vector<uint8_t> b;
    Out o{b};
    o.u64(one.token);
    o.str(one.chat);
    o.blob(gameBody(one.view));
    return b;
}

std::vector<uint8_t> bodyOf(const LinkHello& one) {
    std::vector<uint8_t> b;
    Out o{b};
    o.str(one.region);
    o.u64(one.stream);
    o.u64(one.items);
    o.str(one.proof);
    return b;
}

std::vector<uint8_t> bodyOf(const std::string& text) {
    std::vector<uint8_t> b;
    Out o{b};
    o.str(text);
    return b;
}

std::vector<uint8_t> bodyOf(const std::vector<std::pair<uint64_t, bool>>& tokens) {
    std::vector<uint8_t> b;
    Out o{b};
    o.u32(uint32_t(tokens.size()));
    for (const auto& [token, flag] : tokens) {
        o.u64(token);
        o.u8(flag);
    }
    return b;
}

bool parse(const std::vector<uint8_t>& body, MarketAsk& out) {
    In r{body};
    return takeAsk(r, out) && r.done();
}

bool parse(const std::vector<uint8_t>& body, MarketAnswer& out) {
    In r{body};
    out.serial = r.u64();
    out.no = r.u8();
    if (!r.ok || !net::parse(r.blob(), out.view)) return false;
    out.listings.resize(r.count(40));
    for (sim::Listing& l : out.listings) l = r.listing();
    out.parcels.resize(r.count(37));
    for (sim::Parcel& p : out.parcels) p = r.parcel();
    return r.done();
}

bool parse(const std::vector<uint8_t>& body, MarketNews& out) {
    In r{body};
    out.token = r.u64();
    out.chat = r.str();
    if (!r.ok || !net::parse(r.blob(), out.view)) return false;
    return r.done();
}

bool parse(const std::vector<uint8_t>& body, LinkHello& out) {
    In r{body};
    out.region = r.str();
    out.stream = r.u64();
    out.items = r.u64();
    out.proof = r.str();
    return r.done();
}

bool parse(const std::vector<uint8_t>& body, std::string& out) {
    In r{body};
    out = r.str();
    return r.done();
}

bool parse(const std::vector<uint8_t>& body, std::vector<std::pair<uint64_t, bool>>& out) {
    In r{body};
    out.resize(r.count(9));
    for (auto& [token, flag] : out) {
        token = r.u64();
        flag = r.u8() != 0;
    }
    return r.done();
}

constexpr uint32_t kOutboxMagic = 0x584f4254;  // "TBOX"

std::vector<uint8_t> writeOutbox(uint64_t stream, uint64_t next, const std::vector<MarketAsk>& asks) {
    std::vector<uint8_t> b;
    Out o{b};
    o.u32(kOutboxMagic);
    o.u64(stream);
    o.u64(next);
    o.u32(uint32_t(asks.size()));
    for (const MarketAsk& a : asks) o.blob(bodyOf(a));
    return b;
}

bool readOutbox(const std::vector<uint8_t>& bytes, uint64_t& stream, uint64_t& next, std::vector<MarketAsk>& asks) {
    In r{bytes};
    if (r.u32() != kOutboxMagic) return false;
    stream = r.u64();
    next = r.u64();
    asks.resize(r.count(4));
    for (MarketAsk& a : asks) {
        if (!parse(r.blob(), a)) return false;
    }
    return r.done();
}

uint64_t itemsFingerprint(const content::Tables& tables) {
    uint64_t hash = 1469598103934665603ull;  // FNV-1a
    for (const content::ItemRow& row : tables.items) {
        for (const char c : row.name + "\n") {
            hash ^= uint8_t(c);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

// ---- the line ----------------------------------------------------------------------------------

namespace {

enum class Role { Alone, Keeper, Hands };
Role g_role = Role::Alone;
std::string g_key;

std::string randomHex(size_t digits) {
    static std::random_device entropy;
    static const char* const hex = "0123456789abcdef";
    std::string out;
    while (out.size() < digits) out += hex[entropy() & 15];
    return out;
}

void sendFrame(net::Socket& socket, LinkKind kind, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    putLink(out, kind, body);
    if (!socket.send(out)) socket.close();
}

// -- the keeper's --

struct Branch {
    net::Socket socket;
    std::vector<uint8_t> in;
    std::string nonce;
    bool known = false;
    std::string region, stream;  // stream: "region#hex", whose durable asks are applied once
    Clock::time_point cameAt = Clock::now();
    std::vector<uint8_t> owed;   // frames that go once the keeper has saved (rule 2)
    uint32_t holdersSent = 0;
    bool holdersEver = false;
};
net::Socket g_listener;
std::vector<std::unique_ptr<Branch>> g_branches;
constexpr auto kHelloWithin = std::chrono::seconds(10);

// -- the hands' --

std::string g_host;
int g_port = 0;
net::Socket g_line;
std::vector<uint8_t> g_in;
bool g_welcomed = false;
struct Connected {
    net::Socket socket;
    std::string error;
};
std::future<Connected> g_connecting;
Clock::time_point g_retryAt{};
constexpr auto kRetryEvery = std::chrono::seconds(5);
constexpr auto kRefusedRetry = std::chrono::seconds(60);
// A keeper away is said once, and again each minute it stays away: not every 5 s.
Clock::time_point g_saidAwayAt{};
constexpr auto kSayAwayEvery = std::chrono::seconds(60);

uint64_t g_stream = 0, g_next = 1;
struct Boxed {
    MarketAsk ask;
    bool saved = false;
    bool sent = false;
};
std::vector<Boxed> g_outbox;
bool g_outboxDirty = false;
std::set<uint64_t> g_holders;
std::vector<std::pair<uint64_t, bool>> g_presentSent;
bool g_presentEver = false;
Clock::time_point g_secondAt{};

void lineLost(std::vector<std::unique_ptr<Session>>& sessions, const char* why) {
    if (g_line.open() || g_welcomed) core::logError("market: the keeper's line went (%s); the market is closed here", why);
    g_line.close();
    g_in.clear();
    g_welcomed = false;
    g_presentEver = false;
    for (Boxed& b : g_outbox) b.sent = false;
    brokerLost(sessions);
}

void sendOutbox() {
    if (!g_welcomed) return;
    for (Boxed& b : g_outbox) {
        if (!b.saved || b.sent) continue;
        sendFrame(g_line, LinkKind::Ask, bodyOf(b.ask));
        b.sent = true;
    }
}

void handsPass(std::vector<std::unique_ptr<Session>>& sessions) {
    const Clock::time_point now = Clock::now();
    if (!g_line.open()) {
        if (g_connecting.valid()) {
            if (g_connecting.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            Connected done = g_connecting.get();
            if (!done.socket.open()) {
                if (now - g_saidAwayAt >= kSayAwayEvery) {
                    core::logError("market: the keeper at %s:%d: %s", g_host.c_str(), g_port, done.error.c_str());
                    g_saidAwayAt = now;
                }
                return;
            }
            g_saidAwayAt = Clock::time_point{};
            g_line = std::move(done.socket);
            g_in.clear();
            core::logf("market: a line to the keeper at %s:%d", g_host.c_str(), g_port);
        } else {
            if (now < g_retryAt) return;
            g_retryAt = now + kRetryEvery;
            const std::string host = g_host;
            const int port = g_port;
            g_connecting = std::async(std::launch::async, [host, port] {
                Connected out;
                if (!out.socket.connect(host, port, 3.0, out.error)) out.socket.close();
                return out;
            });
            return;
        }
    }
    if (!g_line.receive(g_in)) {
        lineLost(sessions, "closed");
        return;
    }
    size_t at = 0;
    LinkKind kind{};
    std::vector<uint8_t> body;
    for (int took; (took = takeLink(g_in, at, kind, body)) != 0;) {
        if (took < 0) {
            lineLost(sessions, "not a market line");
            return;
        }
        switch (kind) {
            case LinkKind::Challenge: {
                std::string nonce;
                if (!parse(body, nonce)) break;
                LinkHello hello{g_region, g_stream, itemsFingerprint(g_items), net::passProof(g_key, nonce, 1)};
                sendFrame(g_line, LinkKind::Hello, bodyOf(hello));
                break;
            }
            case LinkKind::Welcome: {
                g_welcomed = true;
                core::logf("market: let in by the keeper; %zu asks in the outbox go again", g_outbox.size());
                // The outbox first, then the holds still worked on: every other is put back (rule 4).
                sendOutbox();
                std::vector<std::pair<uint64_t, bool>> live;
                for (const uint64_t serial : brokerLive()) live.push_back({serial, false});
                sendFrame(g_line, LinkKind::Live, bodyOf(live));
                break;
            }
            case LinkKind::Refused: {
                std::string why;
                parse(body, why);
                core::logError("market: the keeper refused this server: %s", why.c_str());
                g_retryAt = now + kRefusedRetry;
                lineLost(sessions, "refused");
                return;
            }
            case LinkKind::Answer: {
                MarketAnswer answer;
                if (!parse(body, answer)) {
                    lineLost(sessions, "an answer that does not read");
                    return;
                }
                for (size_t i = 0; i < g_outbox.size(); ++i) {
                    if (g_outbox[i].ask.serial != answer.serial) continue;
                    g_outbox.erase(g_outbox.begin() + std::ptrdiff_t(i));
                    g_outboxDirty = true;
                    break;
                }
                brokerAnswered(answer, sessions);
                break;
            }
            case LinkKind::News: {
                MarketNews news;
                if (parse(body, news)) brokerNews(news, sessions);
                break;
            }
            case LinkKind::Herald: {
                std::string text;
                if (parse(body, text)) heraldHere(text, sessions);
                break;
            }
            case LinkKind::Holders: {
                std::vector<std::pair<uint64_t, bool>> tokens;
                if (!parse(body, tokens)) break;
                g_holders.clear();
                for (const auto& [token, _] : tokens) g_holders.insert(token);
                break;
            }
            default: break;
        }
        if (!g_line.open()) break;
    }
    g_in.erase(g_in.begin(), g_in.begin() + long(at));
    if (!g_line.open()) {
        lineLost(sessions, "closed");
        return;
    }
    // Who is on here, when it changed, for the keeper's online dots.
    if (g_welcomed && now - g_secondAt >= std::chrono::seconds(1)) {
        g_secondAt = now;
        std::vector<std::pair<uint64_t, bool>> on;
        for (const auto& one : sessions) {
            if (one->welcomed && one->token != 0) on.push_back({one->token, one->bot});
        }
        std::sort(on.begin(), on.end());
        if (!g_presentEver || on != g_presentSent) {
            sendFrame(g_line, LinkKind::Present, bodyOf(on));
            g_presentSent = std::move(on);
            g_presentEver = true;
        }
    }
}

void branchGone(Branch& b) {
    if (b.known) {
        core::logf("market: %s's line went", b.region.c_str());
        keeperAway(b.region);
    }
    b.socket.close();
}

void keeperPass(std::vector<std::unique_ptr<Session>>& sessions) {
    for (net::Socket s = g_listener.accept(); s.open(); s = g_listener.accept()) {
        auto b = std::make_unique<Branch>();
        b->socket = std::move(s);
        b->nonce = randomHex(32);
        sendFrame(b->socket, LinkKind::Challenge, bodyOf(b->nonce));
        g_branches.push_back(std::move(b));
    }
    const Clock::time_point now = Clock::now();
    for (auto& held : g_branches) {
        Branch& b = *held;
        if (b.socket.open() && !b.socket.flush()) branchGone(b);
        if (!b.socket.open()) continue;
        if (!b.known && now - b.cameAt > kHelloWithin) {
            b.socket.close();
            continue;
        }
        if (!b.socket.receive(b.in)) {
            branchGone(b);
            continue;
        }
        size_t at = 0;
        LinkKind kind{};
        std::vector<uint8_t> body;
        for (int took; b.socket.open() && (took = takeLink(b.in, at, kind, body)) != 0;) {
            if (took < 0 || (!b.known && kind != LinkKind::Hello)) {
                branchGone(b);
                break;
            }
            switch (kind) {
                case LinkKind::Hello: {
                    LinkHello hello;
                    const bool read = parse(body, hello);
                    std::string refused;
                    if (!read || !net::proven(net::Pass{b.nonce, 1, hello.proof}, g_key)) {
                        refused = "its key is not ours";
                    } else if (hello.items != itemsFingerprint(g_items)) {
                        refused = "its item list is not ours: run the same build";
                    } else if (hello.region.empty() || hello.region == g_region) {
                        refused = "it names this server's own realm";
                    }
                    if (!refused.empty()) {
                        core::logError("market: a line refused (%s): %s", read ? hello.region.c_str() : "?",
                                       refused.c_str());
                        sendFrame(b.socket, LinkKind::Refused, bodyOf(refused));
                        b.socket.flush();
                        b.socket.close();
                        break;
                    }
                    // One line a realm: a new one from the same realm is its server come again.
                    for (auto& other : g_branches) {
                        if (other.get() != &b && other->known && other->region == hello.region) branchGone(*other);
                    }
                    b.known = true;
                    b.region = hello.region;
                    char stream[17];
                    std::snprintf(stream, sizeof stream, "%016llx", (unsigned long long)hello.stream);
                    b.stream = hello.region + "#" + stream;
                    core::logf("market: %s's line let in", b.region.c_str());
                    sendFrame(b.socket, LinkKind::Welcome, {});
                    break;
                }
                case LinkKind::Ask: {
                    MarketAsk asked;
                    if (!parse(body, asked)) {
                        branchGone(b);
                        break;
                    }
                    asked.who.region = b.region;
                    const MarketAnswer answer = keeperServe(asked, MarketSource{b.region, b.stream}, sessions);
                    putLink(b.owed, LinkKind::Answer, bodyOf(answer));
                    break;
                }
                case LinkKind::Present: {
                    std::vector<std::pair<uint64_t, bool>> on;
                    if (parse(body, on)) keeperPresent(b.region, on);
                    break;
                }
                case LinkKind::Herald: {
                    // Another realm's: told here, and passed on to every line but its own.
                    std::string text;
                    if (!parse(body, text)) break;
                    heraldHere(text, sessions);
                    for (auto& other : g_branches) {
                        if (other.get() == &b || !other->known || !other->socket.open()) continue;
                        putLink(other->owed, LinkKind::Herald, bodyOf(text));
                    }
                    break;
                }
                case LinkKind::Live: {
                    std::vector<std::pair<uint64_t, bool>> live;
                    if (!parse(body, live)) break;
                    std::vector<uint64_t> serials;
                    for (const auto& [serial, _] : live) serials.push_back(serial);
                    keeperLive(b.stream, serials);
                    break;
                }
                default: break;
            }
        }
        if (b.socket.open()) b.in.erase(b.in.begin(), b.in.begin() + long(at));
    }
    // Each realm's holders, when the book or the ledger moved.
    static Clock::time_point holdersAt{};
    if (now - holdersAt >= std::chrono::seconds(1)) {
        holdersAt = now;
        const uint32_t version = keeperVersion();
        for (auto& held : g_branches) {
            Branch& b = *held;
            if (!b.known || !b.socket.open() || (b.holdersEver && b.holdersSent == version)) continue;
            std::vector<std::pair<uint64_t, bool>> tokens;
            for (const uint64_t token : keeperHolders(b.region)) tokens.push_back({token, false});
            sendFrame(b.socket, LinkKind::Holders, bodyOf(tokens));
            b.holdersSent = version;
            b.holdersEver = true;
        }
    }
    for (size_t i = 0; i < g_branches.size();) {
        if (g_branches[i]->socket.open()) {
            ++i;
            continue;
        }
        if (g_branches[i]->known) keeperAway(g_branches[i]->region);
        g_branches.erase(g_branches.begin() + std::ptrdiff_t(i));
    }
}

}  // namespace

bool marketServe(int port, std::string& error) {
    if (!g_listener.listen(port, error)) return false;
    g_role = Role::Keeper;
    return true;
}

bool marketAt(const std::string& address) {
    const size_t colon = address.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    g_host = address.substr(0, colon);
    g_port = std::atoi(address.c_str() + colon + 1);
    if (g_port <= 0) return false;
    g_role = Role::Hands;
    return true;
}

bool marketKey(const std::string& hex) {
    if (!net::isSecret(hex)) return false;
    g_key = hex;
    return true;
}

bool marketKeeperHere() { return g_role != Role::Hands; }
bool marketOpen() { return g_role != Role::Hands || g_welcomed; }

void marketLoad() {
    std::vector<uint8_t> bytes;
    std::vector<MarketAsk> asks;
    if (g_store.findBroker(Store::kOutboxRow, bytes) && readOutbox(bytes, g_stream, g_next, asks)) {
        // Written with another item list: its things to this server's rows.
        if (const std::vector<int32_t>* map = g_store.brokerRenumber(Store::kOutboxRow)) {
            const auto move = [&](sim::Held& what) {
                if (!what.empty()) what.item = what.item >= 0 && size_t(what.item) < map->size() ? (*map)[size_t(what.item)] : -1;
            };
            for (MarketAsk& a : asks) {
                move(a.listing.what);
                for (sim::Parcel& p : a.parcels) move(p.what);
                if (!a.book.empty()) {
                    sim::Book book;
                    if (book.read(a.book)) {
                        book.renumber(*map);
                        a.book = book.write();
                    }
                }
            }
        }
        for (MarketAsk& a : asks) g_outbox.push_back({std::move(a), true, false});
        core::logf("market: hands to %s:%d, stream %016llx, %zu asks waiting for the keeper", g_host.c_str(), g_port,
                   (unsigned long long)g_stream, g_outbox.size());
        return;
    }
    // A stream of its own, never 0, for the keeper to apply its durable asks once by.
    std::random_device entropy;
    do g_stream = (uint64_t(entropy()) << 32) ^ entropy(); while (g_stream == 0);
    g_next = 1;
    g_outboxDirty = true;
    marketKeep();
    core::logf("market: hands to %s:%d, a new stream %016llx", g_host.c_str(), g_port, (unsigned long long)g_stream);
}

uint64_t marketSerial() { return g_next++; }

void marketSend(const MarketAsk& ask) {
    if (g_role != Role::Hands) return;
    if (durable(ask.op)) {
        // Its asker's things may be in it: told the keeper only once both are written (rule 1).
        if (ask.op == MarketOp::List) g_holders.insert(ask.who.token);
        g_outbox.push_back({ask, false, false});
        g_outboxDirty = true;
        return;
    }
    if (g_welcomed) sendFrame(g_line, LinkKind::Ask, bodyOf(ask));
}

void marketPass(std::vector<std::unique_ptr<Session>>& sessions) {
    if (g_role == Role::Keeper) keeperPass(sessions);
    if (g_role == Role::Hands) handsPass(sessions);
}

void marketKeep() {
    if (g_role != Role::Hands || !g_outboxDirty) return;
    std::vector<MarketAsk> asks;
    for (const Boxed& b : g_outbox) asks.push_back(b.ask);
    // The next number written ahead, so a number is never given twice across a restart.
    if (!g_store.keepBroker({{Store::kOutboxRow, writeOutbox(g_stream, g_next, asks)}})) return;
    g_outboxDirty = false;
    for (Boxed& b : g_outbox) b.saved = true;
}

void marketSaved() {
    if (g_role == Role::Hands) {
        sendOutbox();
        if (g_line.open() && !g_line.flush()) g_line.close();
        return;
    }
    for (auto& held : g_branches) {
        Branch& b = *held;
        if (!b.socket.open() || b.owed.empty()) continue;
        if (!b.socket.send(b.owed)) b.socket.close();
        b.owed.clear();
    }
}

void marketWatch(std::vector<pollfd>& watch) {
    if (g_role == Role::Keeper) {
        watch.push_back({g_listener.fd(), POLLIN, 0});
        for (const auto& b : g_branches) {
            if (b->socket.open()) watch.push_back({b->socket.fd(), short(POLLIN | (b->socket.waiting() > 0 ? POLLOUT : 0)), 0});
        }
    }
    if (g_role == Role::Hands && g_line.open()) {
        watch.push_back({g_line.fd(), short(POLLIN | (g_line.waiting() > 0 ? POLLOUT : 0)), 0});
    }
}

bool marketNewsTo(const std::string& region, const MarketNews& news) {
    for (auto& b : g_branches) {
        if (!b->known || b->region != region || !b->socket.open()) continue;
        putLink(b->owed, LinkKind::News, bodyOf(news));
        return true;
    }
    return false;
}

bool marketHolder(uint64_t token) { return g_holders.count(token) != 0; }

void marketHerald(const std::string& text) {
    if (g_role == Role::Hands) {
        if (g_welcomed && g_line.open()) sendFrame(g_line, LinkKind::Herald, bodyOf(text));
        return;
    }
    for (auto& b : g_branches) {
        if (b->known && b->socket.open()) putLink(b->owed, LinkKind::Herald, bodyOf(text));
    }
}

}  // namespace mu::server
