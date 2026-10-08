#include "game/remote_link.h"

#include <chrono>
#include <thread>

#include "core/log.h"

namespace mu::game {

bool RemoteLink::join(const std::string& host, int port, const net::Hello& hello,
                      net::Welcome& welcome, net::Elsewhere* elsewhere, double seconds) {
    std::string error;
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

void RemoteLink::send(const sim::Command& command) {
    if (!socket_.open()) return;
    std::vector<uint8_t> out;
    net::put(out, command);
    if (!socket_.send(out)) {
        core::logError("server: the connection is gone");
        socket_.close();
    }
}

void RemoteLink::ping() {
    if (!socket_.open()) return;
    pingAt_ = std::chrono::steady_clock::now();
    pingOut_ = true;
    std::vector<uint8_t> out;
    net::put(out, net::Ping{++pingNonce_});
    if (!socket_.send(out)) {
        core::logError("server: the connection is gone");
        socket_.close();
    }
}

void RemoteLink::pump() {
    if (!socket_.open()) return;
    if (!socket_.receive(in_) || !socket_.flush()) {
        core::logError("server: the connection is gone (%zu ticks unstepped)", ticks_.size());
        socket_.close();
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
            if (ok) ticks_.push_back(std::move(tick));
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
}

void RemoteLink::catchUp() {
    const size_t past = ticks_.size();
    const auto from = std::chrono::steady_clock::now();
    // The world as the server last snapshot it, over the mirror raised from its start: then only
    // the ticks after it are stepped (docs/sprints/21-the-snapshot.md).
    if (!snapshot_.empty()) {
        if (mirror_.restoreSnapshot(snapshot_)) {
            core::logf("server: the world laid from a %zu KB snapshot at tick %lld", snapshot_.size() / 1024,
                       (long long)mirror_.tick());
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
