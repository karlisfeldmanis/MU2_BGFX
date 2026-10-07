#pragma once

// The one door between the client and the realm (docs/server-plan.md phase 1, batch 4 of
// docs/sprints/17-the-seam.md). The client asks through `send` -- every click, key and window
// action is a sim::Command -- and moves the world on through `step`; it reads what happened and
// what is there, and changes nothing else.
//
// LocalLink is single player as it has always been: the realm in this process, stepped by the
// frame clock. RemoteLink (phase 4) will be the same door over a socket, its `step` taking the
// next tick the server sent. What only the server will do -- raise a map, keep the clock, load
// and save a character, the GM's switches -- is LocalLink's alone, behind `local()`, so every use
// of it is a line to move to the server and easy to find.

#include <vector>

#include "sim/command.h"
#include "sim/realm.h"

namespace mu::game {

class Link {
public:
    virtual ~Link() = default;
    // An ask, applied at the start of the next tick in the order sent.
    virtual void send(const sim::Command& command) = 0;
    // One tick on: what the realm said in it is in happenings() until the next.
    virtual void step() = 0;
    virtual const std::vector<sim::Happening>& happenings() const = 0;
    // What there is to see. The realm itself for now; batch 5 puts a client-side View here.
    virtual const sim::Realm& realm() const = 0;
};

class LocalLink final : public Link {
public:
    void send(const sim::Command& command) override { realm_.command(command); }
    void step() override { realm_.step(); }
    const std::vector<sim::Happening>& happenings() const override { return realm_.happenings(); }
    const sim::Realm& realm() const override { return realm_; }
    // The server's half, in this process: raise, the clock, saves, setup and the GM's switches.
    sim::Realm& local() { return realm_; }

private:
    sim::Realm realm_;
};

}  // namespace mu::game
