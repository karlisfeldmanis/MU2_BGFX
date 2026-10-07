#pragma once

// The Link over a socket (docs/sprints/18-the-wire.md): the client on this Mac playing on the
// server (server/src/main.cpp). Lockstep: `join` says who he is and takes the server's start;
// Play raises the mirror from it exactly as the server raised its realm. Each tick the server
// sends is stepped on the mirror with the same inputs -- the wall clock, the rain, the commands
// in order -- and the server's hash, a second apart, is checked against the mirror's own.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "game/link.h"
#include "net/socket.h"
#include "net/wire.h"

namespace mu::game {

class RemoteLink final : public Link {
public:
    explicit RemoteLink(sim::Realm& mirror) : mirror_(mirror) {}
    ~RemoteLink() override;

    // Connects and says Hello; waits up to `seconds` for the Welcome. False, with the reason
    // logged, when there is no server or it will not have him.
    bool join(const std::string& host, int port, const net::Hello& hello, net::Welcome& welcome,
              double seconds = 10.0);

    void send(const sim::Command& command) override;
    void pump() override;
    bool due() const override { return !ticks_.empty(); }
    int owed() const override { return int(ticks_.size()); }
    void step() override;
    const std::vector<sim::Happening>& happenings() const override { return mirror_.happenings(); }
    const sim::Realm& realm() const override { return mirror_; }
    bool remote() const override { return true; }

    bool connected() const { return socket_.open(); }
    // Ticks whose hash the server sent and the mirror matched, and the first one it did not.
    uint32_t agreed() const { return agreed_; }
    uint32_t divergedAt() const { return divergedAt_; }

private:
    void check();

    sim::Realm& mirror_;
    net::Socket socket_;
    std::vector<uint8_t> in_;
    std::deque<net::Tick> ticks_;
    std::deque<net::Hash> theirs_;                       // the server's, not yet compared
    std::deque<std::pair<uint32_t, uint64_t>> ours_;     // the mirror's, by tick, the last few
    uint32_t agreed_ = 0, divergedAt_ = 0;
};

}  // namespace mu::game
