#pragma once

// The one door between the client and the realm (docs/server-plan.md phase 1, batch 4 of
// docs/sprints/17-the-seam.md). The client asks through `send` -- every click, key and window
// action is a sim::Command -- and moves the world on through `step`; it reads what happened and
// what is there, and changes nothing else.
//
// LocalLink is single player as it has always been: the realm in this process, stepped by the
// frame clock. RemoteLink (game/remote_link.h, docs/sprints/18-the-wire.md) is the same door
// over a socket: `send` goes to the server, and `step` steps the mirror with the tick the server
// sent. What only the server will do -- raise a map, keep the clock, load and save a character,
// the GM's switches -- is done on Play's own `local_`, and refused while remote.
//
// The realm itself is Play's (Play::realmHeld_), so the references to it never move whichever
// link is in place; a link works on it.

#include <vector>

#include "sim/command.h"
#include "sim/realm.h"

namespace mu::game {

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
    // One tick on: what the realm said in it is in happenings() until the next.
    virtual void step() = 0;
    virtual const std::vector<sim::Happening>& happenings() const = 0;
    // What there is to see: a const realm -- the realm itself, or the mirror of the server's.
    virtual const sim::Realm& realm() const = 0;
    virtual bool remote() const { return false; }
    // The round-trip time to the server in milliseconds, smoothed; -1 when there is no server
    // or no measurement yet. Measured from sending a command to receiving the tick that carries it.
    virtual float rttMs() const { return -1.0f; }
    // The server's rain state for this tick: -1 not known (local), 0 dry, 1 wet. The client's
    // visual weather follows this when remote, so that the rain a player sees matches what the
    // server told the sim (and what every other player sees).
    virtual int serverRain() const { return -1; }
    // Once the realm is raised: whatever happened in it before this player came, stepped at once
    // -- a shared world's past, for a mirror (RemoteLink) -- and the realm turned to look at him.
    virtual void catchUp() {}
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

}  // namespace mu::game
