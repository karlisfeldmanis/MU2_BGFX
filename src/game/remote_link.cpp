#include "game/remote_link.h"

#include <chrono>
#include <thread>

#include "core/log.h"

namespace mu::game {

bool RemoteLink::join(const std::string& host, int port, const net::Hello& hello,
                      net::Welcome& welcome, double seconds) {
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
        if (!socket_.receive(in_) || !socket_.flush()) {
            core::logError("server: %s:%d would not have him (wrong version, or no such world)",
                           host.c_str(), port);
            socket_.close();
            return false;
        }
        net::Kind kind{};
        std::vector<uint8_t> body;
        const int took = net::take(in_, kind, body);
        if (took < 0 || (took == 1 && (kind != net::Kind::Welcome || !net::parse(body, welcome)))) {
            core::logError("server: %s:%d does not speak protocol %u", host.c_str(), port,
                           net::kVersion);
            socket_.close();
            return false;
        }
        if (took == 1) {
            core::logf("server: joined %s:%d -- %s, class %d level %d at %d,%d, seed %llu",
                       host.c_str(), port, welcome.world.c_str(), int(welcome.kin), welcome.level,
                       welcome.column, welcome.row, (unsigned long long)welcome.seed);
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
    check();
}

void RemoteLink::step() {
    if (ticks_.empty()) return;
    const net::Tick tick = std::move(ticks_.front());
    ticks_.pop_front();
    // The same inputs, in the same order, as the server's realm took them (server/src/main.cpp).
    mirror_.setWallClock(tick.wallClock);
    mirror_.invasionRain(tick.rain);
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

}  // namespace mu::game
