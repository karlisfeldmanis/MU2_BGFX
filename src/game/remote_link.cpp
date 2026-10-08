#include "game/remote_link.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "core/log.h"
#include "sim/maps.h"

namespace mu::game {

bool RemoteLink::join(const std::string& host, int port, const net::Hello& hello,
                      net::Welcome& welcome, net::Elsewhere* elsewhere, double seconds) {
    std::string error;
    host_ = host;
    port_ = port;
    if (!socket_.connect(host, port, seconds, error)) {
        core::logError("server: %s", error.c_str());
        return false;
    }
    std::vector<uint8_t> out;
    net::put(out, hello);
    if (!socket_.send(out)) {
        core::logError("server: %s:%d closed before the Hello went", host.c_str(), port);
        return false;
    }
    // The one wait the client makes on the server: before the world is raised, nothing is
    // drawn that the wait could hold up.
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        // What came before a close is read first: an Elsewhere is said and the line shut.
        const bool open = socket_.receive(in_) && socket_.flush();
        net::Kind kind{};
        std::vector<uint8_t> body;
        const int took = net::take(in_, kind, body);
        if (!open && took == 0) {
            core::logError("server: %s:%d would not have him (wrong version, or no such world)",
                           host.c_str(), port);
            socket_.close();
            return false;
        }
        // His character is in another world: where, for the run to open that one instead.
        net::Elsewhere there;
        if (took == 1 && kind == net::Kind::Elsewhere && net::parse(body, there)) {
            core::logf("server: %s:%d has him in %s at %d,%d, not %s", host.c_str(), port,
                       there.world.c_str(), there.column, there.row, hello.world.c_str());
            if (elsewhere != nullptr) *elsewhere = there;
            socket_.close();
            return false;
        }
        if (took < 0 || (took == 1 && (kind != net::Kind::Welcome || !net::parse(body, welcome)))) {
            core::logError("server: %s:%d does not speak protocol %u", host.c_str(), port,
                           net::kVersion);
            socket_.close();
            return false;
        }
        if (took == 1) {
            core::logf("server: joined %s:%d -- %s as #%u, raised round class %d level %d at "
                       "%d,%d, seed %llu, %u ticks of its past", host.c_str(), port,
                       welcome.world.c_str(), welcome.you, int(welcome.kin), welcome.level,
                       welcome.column, welcome.row, (unsigned long long)welcome.seed,
                       welcome.backlog);
            you_ = welcome.you;
            copy_ = std::max<int>(1, welcome.copy);
            udpKey_ = welcome.udpKey;
            snapshot_ = welcome.snapshot;
            // The past follows the Welcome at once: every one of its Ticks in hand before the
            // world is raised, so catchUp steps it whole. A long-running world's is a few MB.
            const auto pastUntil = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (ticks_.size() < welcome.backlog) {
                if (std::chrono::steady_clock::now() > pastUntil || !socket_.open()) {
                    core::logError("server: %zu of %u ticks of the world's past came", ticks_.size(),
                                   welcome.backlog);
                    socket_.close();
                    return false;
                }
                pump();
                if (ticks_.size() < welcome.backlog) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    core::logError("server: %s:%d said nothing in %.0f s", host.c_str(), port, seconds);
    socket_.close();
    return false;
}

RemoteLink::~RemoteLink() {
    if (agreed_ == 0 && divergedAt_ == 0) return;
    core::logf("server: %u of the server's hashes agreed with the mirror%s", agreed_,
               divergedAt_ ? ", then it diverged" : ", none disagreed");
}

namespace {

// What moves him, and so what the realm ahead is given: his orders and his casts. The rest -- a
// purchase, the vault, a quest -- waits for the server as it always has.
bool leads(sim::Command::Kind kind) {
    using Kind = sim::Command::Kind;
    return kind == Kind::Order || kind == Kind::Cast || kind == Kind::CastAt || kind == Kind::LetGo;
}

bool same(const sim::Command& a, const sim::Command& b) {
    return a.kind == b.kind && a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.target == b.target;
}

// An order the server never answered (sent as the line closed, or before his Welcome let him
// in) is dropped from the lead after this many ticks rather than led forever.
constexpr int64_t kOwnForgotten = 40;
// The longest lead: a 400 ms round trip. Past it the hero would run far ahead of a world that
// is drawn where it was, and the line is too poor to hide anyway.
constexpr int kMostLead = 8;
constexpr float kTickMs = 50.0f;  // the realm's 20 Hz (server/src/main.cpp kTickSeconds)

}  // namespace

void RemoteLink::send(const sim::Command& command) {
    if (!socket_.open()) return;
    if (leads(command.kind)) own_.push_back({command, mirror_.tick()});
    std::vector<uint8_t> out;
    net::put(out, command);
    put(out);
}

void RemoteLink::put(const std::vector<uint8_t>& bytes) {
    if (halfLagMs_ > 0.0 || jitterMs_ > 0.0) {
        lagDice_ ^= lagDice_ << 13; lagDice_ ^= lagDice_ >> 17; lagDice_ ^= lagDice_ << 5;
        const double ms = halfLagMs_ + jitterMs_ * double(lagDice_ & 0xFFFFu) / 65535.0;
        // In order, as TCP keeps it: never due before what went ahead of it.
        outLast_ = std::max(outLast_, Clock::now() + std::chrono::microseconds(int64_t(ms * 1000.0)));
        outHeld_.push_back({outLast_, bytes});
        return;
    }
    if (!socket_.send(bytes)) {
        core::logError("server: the connection is gone");
        socket_.close();
    }
}

// The lead follows the line, but only once it has moved most of a tick: a lead that changes is a
// hero who jumps a step. The cushion's ticks are on top of the round trip: the mirror is drawn
// that much later, so he is led that much further.
void RemoteLink::relead() {
    if (rttMs_ < 0.0f) return;
    const float ticks = rttMs_ / kTickMs + float(cushion_);
    if (std::fabs(ticks - float(lead_)) <= 0.75f) return;
    const int was = lead_;
    lead_ = std::clamp(int(ticks + 0.5f), 0, kMostLead);
    if (lead_ != was) {
        core::logf("server: the hero led %d ticks (%.0f ms round trip, %d held)", lead_, rttMs_, cushion_);
    }
}

void RemoteLink::setCushion(int ticks) {
    cushion_ = std::max(0, ticks);
    relead();
}

double RemoteLink::heldMs() {
    lagDice_ ^= lagDice_ << 13; lagDice_ ^= lagDice_ >> 17; lagDice_ ^= lagDice_ << 5;
    return halfLagMs_ + jitterMs_ * double(lagDice_ & 0xFFFFu) / 65535.0;
}

bool RemoteLink::lost() {
    if (lossChance_ <= 0.0) return false;
    lagDice_ ^= lagDice_ << 13; lagDice_ ^= lagDice_ >> 17; lagDice_ ^= lagDice_ << 5;
    return double(lagDice_ & 0xFFFFu) / 65536.0 < lossChance_;
}

void RemoteLink::setLoss(double chance) {
    lossChance_ = std::clamp(chance, 0.0, 1.0);
    if (lossChance_ > 0.0) core::logf("server: a test's line loses %.1f%% of its packets", lossChance_ * 100.0);
}

void RemoteLink::offer(net::Tick&& tick, bool byUdp) {
    if (lastTick_ >= 0 && int64_t(tick.tick) <= lastTick_) return;  // had already
    if (lastTick_ >= 0 && int64_t(tick.tick) > lastTick_ + 1) {
        // Past a gap: only a datagram can be, the stream is in order. Kept for the gap's filling.
        if (byUdp && strays_.size() < 256) strays_.emplace(tick.tick, std::move(tick));
        return;
    }
    if (lastTick_ < 0 && byUdp) return;  // the stream says where the ticks begin
    lastTick_ = tick.tick;
    ++(byUdp ? byUdp_ : byStream_);
    ticks_.push_back(std::move(tick));
    while (!strays_.empty() && int64_t(strays_.begin()->first) <= lastTick_ + 1) {
        auto next = strays_.begin();
        if (int64_t(next->first) == lastTick_ + 1) {
            lastTick_ = next->first;
            ++byUdp_;
            ticks_.push_back(std::move(next->second));
        }
        strays_.erase(next);
    }
}

void RemoteLink::hearUdp(const std::vector<uint8_t>& datagram) {
    std::vector<uint8_t> frames;
    if (!net::ticksOf(datagram, frames)) return;
    net::Kind kind{};
    std::vector<uint8_t> body;
    while (net::take(frames, kind, body) == 1) {
        net::Tick tick;
        if (kind == net::Kind::Tick && net::parse(body, tick)) offer(std::move(tick), true);
    }
}

void RemoteLink::setLag(double ms, double jitterMs) {
    halfLagMs_ = std::max(0.0, ms) * 0.5;
    jitterMs_ = std::max(0.0, jitterMs);
    if (halfLagMs_ > 0.0 || jitterMs_ > 0.0) {
        core::logf("server: a test's line, %.0f ms round trip and up to %.0f ms of jitter each way",
                   ms, jitterMs_);
    }
}

void RemoteLink::ping() {
    if (!socket_.open()) return;
    pingAt_ = std::chrono::steady_clock::now();
    pingOut_ = true;
    std::vector<uint8_t> out;
    net::put(out, net::Ping{++pingNonce_});
    put(out);
}

void RemoteLink::pump() {
    if (!socket_.open()) return;
    const auto now = Clock::now();
    while (!outHeld_.empty() && outHeld_.front().due <= now && socket_.open()) {
        if (!socket_.send(outHeld_.front().bytes)) {
            core::logError("server: the connection is gone");
            socket_.close();
        }
        outHeld_.pop_front();
    }
    const bool lagged = halfLagMs_ > 0.0 || jitterMs_ > 0.0 || lossChance_ > 0.0;
    std::vector<uint8_t> fresh;
    if (!socket_.receive(lagged ? fresh : in_) || !socket_.flush()) {
        core::logError("server: the connection is gone (%zu ticks unstepped)", ticks_.size());
        socket_.close();
    }
    if (lagged) {
        if (!fresh.empty()) {
            double ms = heldMs();
            // A lost packet on a stream: sent again after the retransmission timeout (200 ms at
            // the least on Linux, and a round trip for the loss to be seen), and everything
            // behind it waits with it.
            if (lost()) ms += 200.0 + 2.0 * halfLagMs_;
            inLast_ = std::max(inLast_, now + std::chrono::microseconds(int64_t(ms * 1000.0)));
            inHeld_.push_back({inLast_, std::move(fresh)});
        }
        while (!inHeld_.empty() && inHeld_.front().due <= now) {
            in_.insert(in_.end(), inHeld_.front().bytes.begin(), inHeld_.front().bytes.end());
            inHeld_.pop_front();
        }
    }
    // The datagrams: his key said once a second from the UDP socket, and every Ticks datagram
    // that came, each tick taken if it is the next (offer).
    if (udpKey_ != 0 && udpWanted_) {
        std::string error;
        if (!udp_.open() && !udp_.aim(host_, port_, error)) {
            core::logError("server: %s; the ticks by the stream alone", error.c_str());
            udpWanted_ = false;
        }
        if (udp_.open() && now - bindAt_ >= std::chrono::seconds(1)) {
            std::vector<uint8_t> bind;
            net::putBind(bind, udpKey_);
            udp_.send(bind.data(), bind.size());
            bindAt_ = now;
        }
        std::vector<uint8_t> datagram;
        while (udp_.receive(datagram)) {
            if (lost()) continue;
            if (lagged) {
                udpHeld_.push_back({now + std::chrono::microseconds(int64_t(heldMs() * 1000.0)), datagram});
            } else {
                hearUdp(datagram);
            }
        }
        for (size_t i = 0; i < udpHeld_.size();) {
            if (udpHeld_[i].due <= now) {
                hearUdp(udpHeld_[i].bytes);
                udpHeld_.erase(udpHeld_.begin() + long(i));
            } else {
                ++i;
            }
        }
    }
    net::Kind kind{};
    std::vector<uint8_t> body;
    while (true) {
        const int took = net::take(in_, kind, body);
        if (took == 0) break;
        bool ok = took == 1;
        if (ok && kind == net::Kind::Tick) {
            net::Tick tick;
            ok = net::parse(body, tick);
            if (ok) offer(std::move(tick), false);
        } else if (ok && kind == net::Kind::Ping) {
            net::Ping pong;
            ok = net::parse(body, pong);
            if (ok && pingOut_ && pong.nonce == pingNonce_) {
                const float ms = std::chrono::duration<float, std::milli>(
                                     std::chrono::steady_clock::now() - pingAt_).count();
                // 0.7 / 0.3: about six samples to reflect a 63% change, so a spike shows in a
                // few seconds and the number does not flicker.
                rttMs_ = rttMs_ < 0.0f ? ms : rttMs_ * 0.7f + ms * 0.3f;
                pingOut_ = false;
                relead();
            }
        } else if (ok && kind == net::Kind::Who) {
            ok = net::parse(body, who_);
        } else if (ok && kind == net::Kind::Hash) {
            net::Hash hash;
            ok = net::parse(body, hash);
            if (ok) theirs_.push_back(hash);
        } else {
            ok = false;
        }
        if (!ok) {
            core::logError("server: a frame this client cannot read; closing");
            socket_.close();
            break;
        }
    }
    // A Ping a second, the next only once the last is back (or lost for five): the readout
    // stays live whether he moves or not.
    const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - pingAt_).count();
    if (since >= (pingOut_ ? 5.0f : 1.0f)) ping();
    check();
}

bool RemoteLink::nameOf(uint32_t id, std::string* name, bool* bot) const {
    for (const net::Who::One& one : who_.players) {
        if (one.id != id) continue;
        if (name) *name = one.name;
        if (bot) *bot = one.bot;
        return true;
    }
    return false;
}

void RemoteLink::step() {
    if (ticks_.empty()) return;
    const net::Tick tick = std::move(ticks_.front());
    ticks_.pop_front();
    // The same inputs, in the same order, as the server's realm took them (server/src/main.cpp).
    mirror_.setWallClock(tick.wallClock);
    serverRain_ = tick.rain ? 1 : 0;
    mirror_.invasionRain(tick.rain, tick.invasionElsewhere);
    for (const net::Arrival& one : tick.arrivals) mirror_.carry(one.ticket, one.kept);
    for (const sim::Command& one : tick.commands) mirror_.command(one);
    mirror_.step();
    if (uint32_t(mirror_.tick()) != tick.tick) {
        core::logError("server: the mirror is at tick %lld and the server's step was %u",
                       (long long)mirror_.tick(), tick.tick);
    }
    ours_.emplace_back(uint32_t(mirror_.tick()), net::stateHash(mirror_));
    while (ours_.size() > 256) ours_.pop_front();
    check();
    // His own orders the server has now taken leave the lead, the oldest like one first; and
    // one it never took is let go after kOwnForgotten.
    for (const sim::Command& one : tick.commands) {
        if (one.player != you_ || !leads(one.kind)) continue;
        for (auto at = own_.begin(); at != own_.end(); ++at) {
            if (same(at->command, one)) {
                own_.erase(at);
                break;
            }
        }
    }
    while (!own_.empty() && own_.front().at + kOwnForgotten < mirror_.tick()) own_.pop_front();
    aheadStale_ = true;
}

// The realm ahead laid from the mirror and stepped `lead_` ticks with his own orders in it, each
// at the step the server most likely takes it: sent when the mirror stood at tick `at`, it
// reaches the server a one-way trip later, when the server is a one-way trip ahead of the
// mirror, so it is taken in the step to tick at + lead + 1. A click this tick is led from the
// next one: about 25 ms, the wait of a realm in this process before the early tick.
//
// Nothing here is the server's word: it is laid again from the mirror every tick, so a guess
// that was wrong (an order taken a tick later than thought, a monster in his way the mirror did
// not yet have) is gone by the next tick, and the drawing's interpolation turns the difference
// into a slide of a step at most.
void RemoteLink::predict() {
    aheadStale_ = false;
    aheadOk_ = false;
    if (lead_ <= 0 || aheadNever_ || you_ == 0 || ticks_.size() > 2) return;
    if (!ahead_) {
        const content::Tables* tables = mirror_.tables();
        const sim::MapRow* map = tables ? sim::mapNumbered(int(tables->map)) : nullptr;
        // An event map changes its own grid under the run (Blood Castle's bridge, its doors):
        // a realm ahead laid on another copy of the tables is not trusted to agree.
        if (tables == nullptr || map == nullptr || map->event) {
            aheadNever_ = true;
            return;
        }
        ahead_ = std::make_unique<sim::Realm>();
        ahead_->configure(mirror_.config());
        const sim::Body& me = mirror_.hero();
        if (!ahead_->raise(tables, 1, me.column(), me.row(), me.kin, me.level)) {
            core::logError("server: the realm ahead would not raise; the hero is drawn where the mirror has him");
            ahead_.reset();
            aheadNever_ = true;
            return;
        }
    }
    if (!mirror_.snapshot(aheadBytes_) || !ahead_->restoreSnapshot(aheadBytes_) || !ahead_->lookAs(you_)) {
        return;
    }
    const int64_t now = mirror_.tick();
    for (int k = 0; k < lead_; ++k) {
        for (const Own& one : own_) {
            const int64_t step = std::clamp<int64_t>(one.at + lead_ - now, 0, lead_);
            if (step != k) continue;
            sim::Command asked = one.command;
            asked.player = you_;
            ahead_->command(asked);
        }
        ahead_->step();
    }
    aheadOk_ = true;
}

const sim::Body* RemoteLink::ahead() {
    if (aheadStale_) predict();
    return aheadOk_ ? ahead_->find(you_) : nullptr;
}

void RemoteLink::catchUp() {
    const size_t past = ticks_.size();
    const auto from = std::chrono::steady_clock::now();
    // The world as the server last snapshot it, over the mirror raised from its start: then only
    // the ticks after it are stepped (docs/sprints/21-the-snapshot.md).
    if (!snapshot_.empty()) {
        std::vector<uint8_t> raw;
        if (net::unpack(snapshot_, raw) && mirror_.restoreSnapshot(raw)) {
            core::logf("server: the world laid from a %zu KB snapshot (%zu KB on the line) at tick %lld",
                       raw.size() / 1024, snapshot_.size() / 1024, (long long)mirror_.tick());
        } else {
            core::logError("server: the world's snapshot did not read; this mirror will not agree");
        }
        snapshot_.clear();
        snapshot_.shrink_to_fit();
    }
    while (!ticks_.empty()) step();
    if (you_ != 0 && !mirror_.lookAs(you_)) {
        core::logError("server: the mirror has no player #%u after the world's past", you_);
    }
    if (past > 0) {
        core::logf("server: the world's past, %zu ticks, replayed in %.2f s; %d players here", past,
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count(),
                   mirror_.playersHere());
    }
}

// Each of the server's hashes against the mirror's for the same tick, once the mirror has it.
void RemoteLink::check() {
    while (!theirs_.empty()) {
        const net::Hash& one = theirs_.front();
        auto at = ours_.begin();
        while (at != ours_.end() && at->first != one.tick) ++at;
        if (at == ours_.end()) {
            if (!ours_.empty() && ours_.back().first > one.tick) theirs_.pop_front();  // too old
            else break;  // the mirror has not stepped it yet
            continue;
        }
        if (at->second == one.hash) {
            ++agreed_;
        } else if (divergedAt_ == 0) {
            divergedAt_ = one.tick;
            core::logError("server: the mirror DIVERGED from the server at tick %u "
                           "(%u hashes agreed before it)", one.tick, agreed_);
        }
        theirs_.pop_front();
    }
}

bool AccountLink::open(const std::string& host, int port, const net::Account& account,
                       net::Roster& roster, double seconds) {
    std::string error;
    in_.clear();
    if (!socket_.connect(host, port, seconds, error)) {
        core::logError("account: %s", error.c_str());
        return false;
    }
    std::vector<uint8_t> out;
    net::put(out, account);
    put(out);
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (socket_.open() && std::chrono::steady_clock::now() < until) {
        if (poll(roster)) {
            core::logf("account: %s:%d has %zu character(s) on it", host.c_str(), port, roster.seats.size());
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    core::logError("account: %s:%d would not have the account (wrong version, or no answer in %.0f s)",
                   host.c_str(), port, seconds);
    socket_.close();
    return false;
}

void AccountLink::create(const std::string& name, uint8_t kin) {
    std::vector<uint8_t> out;
    net::put(out, net::Create{name, kin});
    put(out);
}

void AccountLink::drop(uint64_t token) {
    std::vector<uint8_t> out;
    net::put(out, net::Delete{token});
    put(out);
}

void AccountLink::put(const std::vector<uint8_t>& frame) {
    if (socket_.open() && !socket_.send(frame)) {
        core::logError("account: the connection is gone");
        socket_.close();
    }
}

bool AccountLink::poll(net::Roster& roster) {
    if (!socket_.open()) return false;
    if (!socket_.receive(in_) || !socket_.flush()) {
        core::logError("account: the connection is gone");
        socket_.close();
    }
    net::Kind kind{};
    std::vector<uint8_t> body;
    const int took = net::take(in_, kind, body);
    if (took < 0 || (took == 1 && (kind != net::Kind::Roster || !net::parse(body, roster)))) {
        core::logError("account: the server does not speak protocol %u", net::kVersion);
        socket_.close();
        return false;
    }
    return took == 1;
}

}  // namespace mu::game
