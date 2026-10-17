#pragma once

// The one door between the client and the realm (server/README.md phase 1, batch 4 of
// docs/sprints/17-the-seam.md). The client asks through `send` -- every click, key and window
// action is a sim::Command -- and moves the world on through `step`; it reads what happened and
// what is there, and changes nothing else.
//
// LocalLink is the realm in this process, stepped by the frame clock: the harness the tests, the
// benches and the headless runs drive, not a way to play (the game is client and server since
// 2026-10-07, docs/sprints/19-many-heroes.md). RemoteLink (client/remote_link.h, docs/sprints/18-the-wire.md) is the same door
// over a socket: `send` goes to the server, and `step` steps the mirror with the tick the server
// sent. What only the server will do -- raise a map, keep the clock, load and save a character,
// the GM's switches -- is done on Play's own `local_`, and refused while remote.
//
// The realm itself is Play's (Play::realmHeld_), so the references to it never move whichever
// link is in place; a link works on it.

#include <vector>

#include "net/broker_wire.h"
#include "net/wire.h"
#include "sim/command.h"
#include "sim/realm.h"

namespace mu::client {

class Link {
public:
    virtual ~Link() = default;
    // An ask, applied at the start of the next tick in the order sent.
    virtual void send(const sim::Command& command) = 0;
    // The network's in and out, once a frame; nothing for a realm in this process.
    virtual void pump() {}
    // Whether a tick may be stepped now: always for a local realm (the frame clock decides),
    // and for a remote one only once the server has sent it.
    virtual bool due() const { return true; }
    // How many ticks the server has sent that are not yet stepped: the mirror repays them a
    // few a frame rather than clamping (server-plan §3, "the client clock owes ticks").
    virtual int owed() const { return 0; }
    // Whether the next tick due was taken early by the server (net::Tick::early): stepped the
    // frame it arrives, not on the frame clock.
    virtual bool earlyDue() const { return false; }
    // One tick on: what the realm said in it is in happenings() until the next.
    virtual void step() = 0;
    virtual const std::vector<sim::Happening>& happenings() const = 0;
    // What there is to see: a const realm -- the realm itself, or the mirror of the server's.
    virtual const sim::Realm& realm() const = 0;
    virtual bool remote() const { return false; }
    // Whether the line to the server is gone: closed, or silent past RemoteLink's kSilentMost.
    // It stays gone -- nothing reopens it -- and Play freezes on it. Never for a local realm.
    virtual bool cut() const { return false; }
    // Why it went, in a line for the Disconnected window: the server's word when it closed the
    // line itself (net::Bye), else what the client saw. Empty while it is not cut.
    virtual std::string cutWhy() const { return {}; }
    // The round-trip time to the server in milliseconds, smoothed; -1 when there is no server
    // or no measurement yet. The line's alone: a Ping the server echoes the moment it reads it,
    // with no wait for a tick in it.
    virtual float rttMs() const { return -1.0f; }
    // The server's rain state for this tick: -1 not known (local), 0 dry, 1 wet. The client's
    // visual weather follows this when remote, so that the rain a player sees matches what the
    // server told the sim (and what every other player sees).
    virtual int serverRain() const { return -1; }
    // Once the realm is raised: whatever happened in it before this player came, stepped at once
    // -- a shared world's past, for a mirror (RemoteLink) -- and the realm turned to look at him.
    virtual void catchUp() {}
    // Another player's name and whether he is a bot, by his body's id, as the server last said
    // (net::Who); false for one it has not named, and always on a realm of this process.
    virtual bool nameOf(uint32_t id, std::string* name, bool* bot) const { return false; }
    // **The chat** (net::Say, net::Heard): a line said, and the lines heard since the last take,
    // oldest first, appended to `into`. Nothing on a realm of this process: no one to hear.
    virtual void say(const net::Say& said) {}
    virtual void takeHeard(std::vector<net::Heard>& into) {}
    // **The party** (docs/game/party.md, net::PartyAsk, net::PartyState): an ask of it, and his party
    // as the server last said it -- empty for none. Nothing on a realm of this process.
    virtual void askParty(const net::PartyAsk& asked) {}
    virtual const net::PartyState& party() const {
        static const net::PartyState none;
        return none;
    }
    // **The guild** (docs/game/guild.md, net::GuildAsk and its answers): an ask of it, his guild as the
    // server last said it (id 0 for none), a guild's name and mark by its id (null for one not
    // yet said), a player's guild by his body's id (0 for none), and the calls to answer -- an
    // invite to him, or an ask of his guild -- as they come. Nothing on a realm of this process.
    virtual void askGuild(const net::GuildAsk& asked) {}
    virtual const net::GuildState& guild() const {
        static const net::GuildState none;
        return none;
    }
    virtual const net::GuildMark* guildMark(uint32_t id) const { return nullptr; }
    virtual uint32_t guildOfPlayer(uint32_t id) const { return 0; }
    virtual bool takeGuildCall(net::GuildCall& out) { return false; }
    // **The Golden Invasion on the server** (net::Invasion): where its dragon is, on whichever
    // map, as the server last said -- Quiet for none, and always on a realm of this process,
    // whose own invasion is read off the realm (Play::dragonLanded).
    virtual const net::Invasion& invasion() const {
        static const net::Invasion none;
        return none;
    }
    // **The Broker** (docs/game/broker.md, net/broker_wire.h): an ask of the market, and its answers
    // as they come, oldest first. A realm of this process has no market -- it is the server's,
    // shared by every map -- so its ask is answered Closed.
    virtual void askBroker(const net::BrokerAsk& asked) { closedAsk_ = asked.ask; }
    virtual bool takeBrokerView(net::BrokerView& out) {
        if (closedAsk_ == 0) return false;
        out = net::BrokerView{};
        out.answered = closedAsk_;
        out.no = uint8_t(sim::BrokerNo::Closed);
        out.said = "The market is kept on the server: play online to trade here.";
        closedAsk_ = 0;
        return true;
    }
    // **The hero ahead** (docs/sprints/24-the-quiet-line.md): his own body as the mirror will
    // have it a round trip from now, with the orders he has sent and the server has not yet
    // answered already in it -- where he is drawn, so a click moves him at once rather than a
    // round trip later. nullptr when there is nothing to lead (a realm of this process, a short
    // line, an event map): then he is drawn where the realm has him, as everyone else is.
    virtual const sim::Body* ahead() { return nullptr; }
    // The ticks the drawing holds in hand against a line's jitter (Play::cushion_): the mirror is
    // that much further behind the server, so the hero ahead is led that much further.
    virtual void setCushion(int ticks) {}
    // Which copy of the map the server put him in, from 1 (net::Welcome::copy).
    // The realm ahead itself (whose route the hero coasts along, Play::coast), nullptr as ahead.
    virtual const sim::Realm* aheadRealm() { return nullptr; }
    virtual int mapCopy() const { return 1; }

private:
    uint32_t closedAsk_ = 0;  // a Broker ask with no market to answer it (askBroker)
};

class LocalLink final : public Link {
public:
    explicit LocalLink(sim::Realm& realm) : realm_(realm) {}
    void send(const sim::Command& command) override { realm_.command(command); }
    void step() override { realm_.step(); }
    const std::vector<sim::Happening>& happenings() const override { return realm_.happenings(); }
    const sim::Realm& realm() const override { return realm_; }

private:
    sim::Realm& realm_;
};

}  // namespace mu::client
