#pragma once

// The Link over a socket (docs/sprints/18-the-wire.md): the client on this Mac playing on the
// server (server/src/main.cpp). Lockstep: `join` says who he is and takes the server's start;
// Play raises the mirror from it exactly as the server raised its realm. Each tick the server
// sends is stepped on the mirror with the same inputs -- the wall clock, the rain, the commands
// in order -- and the server's hash, a second apart, is checked against the mirror's own.

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/link.h"
#include "net/socket.h"
#include "net/wire.h"

namespace mu::client {

class RemoteLink final : public Link {
public:
    explicit RemoteLink(sim::Realm& mirror) : mirror_(mirror) {}
    ~RemoteLink() override;

    // Connects and says Hello; waits up to `seconds` for the Welcome. False, with the reason
    // logged, when there is no server or it will not have him -- or when his character is in
    // another world, which `elsewhere` then names (net::Elsewhere): the run opens that one.
    bool join(const std::string& host, int port, const net::Hello& hello, net::Welcome& welcome,
              net::Elsewhere* elsewhere = nullptr, double seconds = 10.0);

    void send(const sim::Command& command) override;
    void pump() override;
    bool due() const override { return !ticks_.empty(); }
    int owed() const override { return int(ticks_.size()); }
    // The server's early tick, when it is for his own click: an early tick for another player's
    // is stepped on this clock like any other (Play::update catches up on it gently).
    bool earlyDue() const override;
    void step() override;
    const std::vector<sim::Happening>& happenings() const override { return mirror_.happenings(); }
    const sim::Realm& realm() const override { return mirror_; }
    bool remote() const override { return true; }
    float rttMs() const override { return rttMs_; }
    int serverRain() const override { return serverRain_; }
    // The world's past, every tick of the Welcome's backlog, stepped on the mirror with nothing
    // drawn; then the mirror looks at his own player (Welcome::you).
    void catchUp() override;
    bool nameOf(uint32_t id, std::string* name, bool* bot) const override;
    void say(const net::Say& said) override;
    void takeHeard(std::vector<net::Heard>& into) override;
    void askParty(const net::PartyAsk& asked) override;
    const net::PartyState& party() const override { return party_; }
    void askGuild(const net::GuildAsk& asked) override;
    const net::GuildState& guild() const override { return guild_; }
    const net::GuildMark* guildMark(uint32_t id) const override;
    uint32_t guildOfPlayer(uint32_t id) const override;
    bool takeGuildCall(net::GuildCall& out) override;
    const net::Invasion& invasion() const override { return invasion_; }
    void askBroker(const net::BrokerAsk& asked) override;
    bool takeBrokerView(net::BrokerView& out) override;
    const sim::Body* ahead() override;
    const sim::Realm* aheadRealm() override;
    int mapCopy() const override { return copy_; }
    void setCushion(int ticks) override;
    // How many ticks ahead of the mirror the hero is drawn: the round trip in ticks, held still
    // until it has moved most of a tick so he does not jitter with the line.
    int lead() const { return lead_; }
    // A test's line (`--lag MS`, `--jitter MS`): every byte each way held half of `ms`, and up to
    // `jitterMs` more, in order, as a far or a busy line would -- to feel and measure what a
    // player far from the box gets, on this Mac.
    void setLag(double ms, double jitterMs);
    // And `--loss P`: each packet lost with chance P, as a line loses them -- a stream's held up,
    // with everything behind it, until it is sent again; a datagram simply gone.
    void setLoss(double chance);
    // And `--stall MS EVERY`: nothing read off the line, stream or datagram, for `ms` every
    // `everySeconds` -- the kernel holds it, as a Wi-Fi that stops for a moment holds it.
    void setStall(double ms, double everySeconds);
    // The tick datagrams (sprint 24), on unless a test turns them off (`--no-udp`).
    void setUdp(bool on) { udpWanted_ = on; }
    // How many ticks came first by datagram, and by the stream.
    uint32_t ticksByUdp() const { return byUdp_; }
    uint32_t ticksByStream() const { return byStream_; }

    bool connected() const { return socket_.open(); }
    bool cut() const override { return !socket_.open(); }
    std::string cutWhy() const override { return socket_.open() ? std::string() : why_; }
    // Ticks whose hash the server sent and the mirror matched, and the first one it did not.
    uint32_t agreed() const { return agreed_; }
    uint32_t divergedAt() const { return divergedAt_; }

    // **One mirror, several lines** (tools/netbot, 2026-10-13): every line on a world hears the
    // same Tick (server/src/world.cpp, tick), so a process with many players there steps one
    // mirror with whichever line has the next tick, and the others drop what it has stepped --
    // with the hashes for them, and the Welcome's snapshot, which catchUp would have laid.
    void passThrough(int64_t tick);
    // The tick at the front of what is in hand, -1 for none.
    int64_t nextTick() const { return ticks_.empty() ? -1 : int64_t(ticks_.front().tick); }

private:
    void check();
    void ping();
    // Closes the line, keeping `why` for cutWhy unless a reason is already kept. The server's
    // Bye clears it first: it was the last thing the server said, and is read after the close
    // the client saw.
    void close(const char* why);
    void put(const std::vector<uint8_t>& bytes);  // out over the line, or held by setLag
    void predict();

    using Clock = std::chrono::steady_clock;
    sim::Realm& mirror_;
    // His orders sent and not yet seen in a server's tick, each with the mirror's tick when it
    // went: what the realm ahead is given, at the step the server will most likely take it.
    struct Own {
        sim::Command command;
        int64_t at = 0;
        Clock::time_point sent{};  // for the slow answer's line (RemoteLink::step)
    };
    std::deque<Own> own_;
    std::unique_ptr<sim::Realm> ahead_;  // raised once from the mirror's tables, laid each tick
    std::vector<uint8_t> aheadBytes_;
    bool aheadStale_ = true;   // the mirror has stepped since the realm ahead was laid
    bool aheadOk_ = false;     // and the last laying worked
    bool aheadNever_ = false;  // this world is not led (an event map, or it would not raise)
    int lead_ = 0;
    int cushion_ = 0;
    int slowPongs_ = 0;  // round trips in a row far over rttMs_, set aside (kSpikeMs)
    int copy_ = 1;
    void relead();  // lead_ from the round trip and the cushion
    // A tick from the stream or a datagram, taken if it is the next and kept if it is ahead of
    // a gap; one already had is dropped.
    void offer(net::Tick&& tick, bool byUdp);
    void hearUdp(const std::vector<uint8_t>& datagram);
    double heldMs();   // a held byte's wait under setLag: half the round trip and some jitter
    bool lost();       // setLoss's die
    net::Datagram udp_;
    uint64_t udpKey_ = 0;
    bool udpWanted_ = true;
    std::string why_;  // cutWhy's
    std::string host_;
    int port_ = 0;
    Clock::time_point bindAt_{};
    int64_t lastTick_ = -1;              // the newest tick in ticks_ or stepped
    std::map<uint32_t, net::Tick> strays_;  // datagrams' ticks past a gap the stream has yet to fill
    uint32_t byUdp_ = 0, byStream_ = 0;
    double lossChance_ = 0.0;
    // setLag's: the line's delay each way, its jitter, and the bytes held in each direction.
    double halfLagMs_ = 0.0, jitterMs_ = 0.0;
    struct Held {
        Clock::time_point due;
    double stallMs_ = 0.0, stallEvery_ = 0.0;
    Clock::time_point stallFrom_{};
        std::vector<uint8_t> bytes;
    };
    std::deque<Held> inHeld_, outHeld_;
    std::vector<Held> udpHeld_;  // datagrams, each its own wait, in no order
    Clock::time_point inLast_{}, outLast_{};
    uint32_t lagDice_ = 0x2545F491u;
    net::Socket socket_;
    std::vector<uint8_t> in_;
    std::deque<net::Tick> ticks_;
    std::deque<net::Hash> theirs_;                       // the server's, not yet compared
    std::deque<std::pair<uint32_t, uint64_t>> ours_;     // the mirror's, by tick, the last few
    uint32_t agreed_ = 0, divergedAt_ = 0;
    uint32_t you_ = 0;
    std::vector<uint8_t> snapshot_;  // the Welcome's, laid on the mirror by catchUp
    // The readout's round trip: the Ping out and when it went, and the smoothed result in ms.
    std::chrono::steady_clock::time_point pingAt_{};
    uint32_t pingNonce_ = 0;
    bool pingOut_ = false;
    bool pingUnread_ = false;  // the Ping out has waited past a gap between pumps
    float rttMs_ = -1.0f;
    Clock::time_point heardAt_{};   // the last byte from the server, by stream or datagram
    Clock::time_point pumpedAt_{};  // the last pump: a Ping's answer read later than kUnreadMost is not counted
    bool pumpSteady_ = false;       // this pump came soon after the last: a late tick is the line's
    Clock::time_point takenAt_{};   // when the newest tick was taken (RemoteLink::offer)
    int serverRain_ = -1;  // the latest tick's rain: -1 unknown, 0 dry, 1 wet
    net::Who who_;         // the players' names, the latest the server sent
    // Chat lines heard and not yet taken; the oldest dropped past kHeardMost, if nobody takes them.
    std::deque<net::Heard> heard_;
    net::PartyState party_;  // his party, the latest the server sent
    net::GuildState guild_;  // his guild, the latest the server sent
    std::map<uint32_t, net::GuildMark> marks_;  // the guilds' names and marks, by id
    std::deque<net::GuildCall> guildCalls_;     // the cards to answer, not yet taken
    net::Invasion invasion_;  // where the Golden Dragon is on the server, the latest it sent
    std::deque<net::BrokerView> brokerViews_;  // the market's answers, not yet taken
};

// The character screen's line to the server (server-plan phase 6, docs/sprints/23-the-account.md):
// the account's key said once, and its characters, their making and their deleting asked for over
// it. Nothing here is a world: the screen hands the pick's token to Play, which opens a line of
// its own with a Hello.
class AccountLink {
public:
    // Connects, says Account and waits up to `seconds` for the first Roster. False, with the
    // reason logged, when there is no server or it would not have the account.
    bool open(const std::string& host, int port, const net::Account& account, net::Roster& roster,
              double seconds = 10.0);
    void create(const std::string& name, uint8_t kin);
    void drop(uint64_t token);
    // A Roster the server has answered since the last call, into `roster`: true once for each.
    bool poll(net::Roster& roster);
    bool up() const { return socket_.open(); }
    void close() { socket_.close(); }

private:
    void put(const std::vector<uint8_t>& frame);
    net::Socket socket_;
    std::vector<uint8_t> in_;
};

}  // namespace mu::client
