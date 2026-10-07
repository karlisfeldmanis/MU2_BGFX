#pragma once

// What goes over the wire between MU2's client and its server (docs/sprints/18-the-wire.md).
//
// Lockstep: the server steps the realm and the client's mirror steps the same realm from the
// same start with the same inputs, so what crosses is the start and the inputs, never the state.
//
//   client -> server   Hello    who he is and where he stands: world, class, level, tile, hands
//                      Command  a sim::Command, as the client's Link sent it
//   server -> client   Welcome  the world's start -- the seed, and the first player's tile,
//                               class, level and hands, the config -- his own body's id, and
//                               how many Ticks follow at once: the world's past, every tick
//                               since it was raised, which the mirror replays to stand where
//                               the server stands (docs/sprints/19-many-heroes.md)
//                      Tick     one tick's inputs: its number, the wall clock, the rain, and the
//                               commands the server applied at its start, in order
//                      Hash     the server realm's hash after a tick, for the mirror to compare
//                      Elsewhere  instead of a Welcome: his character is in another world, at
//                               this tile; the client opens that one and says Hello there
//
// A frame is a u32 length (of what follows), a u8 kind and the body, little-endian throughout.
// A frame that does not parse drops that connection, never the server (server-plan §3).

#include <cstdint>
#include <string>
#include <vector>

#include "sim/command.h"
#include "sim/config.h"
#include "sim/realm.h"

namespace mu::sim {
class Realm;
}

namespace mu::net {

// Bumped whenever a message changes shape. A client and a server of different versions do not
// talk: the server answers a Hello of another version by closing.
// 2: a world shared by its connections -- the Welcome's `you` and `backlog`, Join and Leave.
// 3: the character carried between worlds -- the token, the Welcome's kept first player, a
//    Tick's arrivals (docs/sprints/20-the-world-host.md).
// 4: the server keeps which world he is in -- the Hello's `arriving`, Elsewhere.
// 5: Ping, echoed at once, for a readout of the line alone; a Tick's `early`.
// 6: the Welcome's snapshot: the world as it stood, and only the ticks after it
//    (docs/sprints/21-the-snapshot.md).
// 7: the Welcome's castle: which Blood Castle the world is, set on the raise.
// 8: a Kept carries his way back (Go Back!, sim::WayBack); the Hello's `arriving` is gone, since
//    every way between worlds is now the server's to see.
constexpr uint32_t kVersion = 8;
// The shape of a character's bytes (putKept), apart from the protocol's: what the server's store
// keeps beside each row. 3 is protocol 3's; 4 adds the way back.
constexpr int kKeptLayout = 4;
// MU's GameServer listened on 55901; ours is its own.
constexpr int kDefaultPort = 44406;
// The longest frame a client accepts: a Welcome with its snapshot, about half a MB for a town of
// three hundred bodies. And the longest a server accepts from a client, whose frames are a
// Hello and commands, so a line cannot make it hold megabytes.
constexpr uint32_t kMostFrame = 16u << 20;
constexpr uint32_t kMostAsked = 64u << 10;

enum class Kind : uint8_t { Hello = 1, Welcome = 2, Command = 3, Tick = 4, Hash = 5, Elsewhere = 6,
                          Ping = 7 };

struct Hello {
    uint32_t version = kVersion;
    std::string world;
    uint8_t kin = 0;
    int32_t level = 1;
    int32_t column = 0, row = 0;
    std::string weapon, shield;
    // The server's word for his character, from his last Welcome, or 0 for a new one: a map
    // change reconnects with it, and the server brings him back whole (sim::Kept).
    uint64_t token = 0;
};

struct Elsewhere {
    std::string world;
    int32_t column = 0, row = 0;
};

struct Welcome {
    uint32_t version = kVersion;
    uint64_t seed = 0;
    std::string world;
    uint8_t kin = 0;
    int32_t level = 1;
    int32_t column = 0, row = 0;
    std::string weapon, shield;
    sim::RealmConfig config;
    uint32_t you = 0;      // his player's body id (Realm::lookAs)
    uint32_t backlog = 0;  // Ticks that follow this frame and are the world's past
    uint64_t token = 0;    // his character's, for the next Hello
    // The world's first player came from another world: laid on him after the raise and the
    // cradle (Realm::restoreKept), as the server laid it.
    bool kept = false;
    sim::Kept first;
    // The world as it stood when the server last snapshot it (Realm::snapshot), empty for none:
    // the mirror raised from the rest is laid with it, and `backlog` is the ticks since.
    // Blood Castle's number when the world is one (Realm::setCastle, straight after the raise), or 0.
    int32_t castle = 0;
    std::vector<uint8_t> snapshot;
};

// A character carried into the world this tick, for the Join with its ticket (Realm::carry).
struct Arrival {
    uint32_t ticket = 0;
    sim::Kept kept;
};

struct Tick {
    uint32_t tick = 0;       // the realm's tick after this step
    int64_t wallClock = 0;   // Realm::setWallClock before the step
    bool rain = false;       // Realm::invasionRain before the step
    std::vector<sim::Command> commands;  // Realm::command each, in order, before the step
    std::vector<Arrival> arrivals;       // Realm::carry each, before the commands
    // Taken before its deadline, for an order that set a standing player off (server/src/main.cpp,
    // kEarlyApart): a mirror steps it the frame it arrives rather than on its own 50 ms clock.
    bool early = false;
};

// The client's, sent back by the server the moment it is read: the round trip of the line alone,
// with no wait for a tick in it -- what the readout shows.
struct Ping {
    uint32_t nonce = 0;
};

struct Hash {
    uint32_t tick = 0;
    uint64_t hash = 0;
};

// Frames, appended to `out`.
void put(std::vector<uint8_t>& out, const Hello& one);
void put(std::vector<uint8_t>& out, const Welcome& one);
void put(std::vector<uint8_t>& out, const sim::Command& one);
void put(std::vector<uint8_t>& out, const Tick& one);
void put(std::vector<uint8_t>& out, const Hash& one);
void put(std::vector<uint8_t>& out, const Elsewhere& one);
void put(std::vector<uint8_t>& out, const Ping& one);

// One frame off the front of `buffer`, its kind and body. Returns 1 for a frame taken (and
// removed), 0 for not all of one there yet, -1 for a buffer that is not our protocol.
int take(std::vector<uint8_t>& buffer, Kind& kind, std::vector<uint8_t>& body,
         uint32_t most = kMostFrame);

// A body, parsed. False on a body of the wrong length or shape.
bool parse(const std::vector<uint8_t>& body, Hello& out);
bool parse(const std::vector<uint8_t>& body, Welcome& out);
bool parse(const std::vector<uint8_t>& body, sim::Command& out);
bool parse(const std::vector<uint8_t>& body, Tick& out);
bool parse(const std::vector<uint8_t>& body, Hash& out);
bool parse(const std::vector<uint8_t>& body, Elsewhere& out);
bool parse(const std::vector<uint8_t>& body, Ping& out);

// A character alone, in the bytes a Welcome or a Tick carries him in: what the server's character
// store keeps (server/src/store.h). `keptFrom` is false unless the bytes are exactly one.
void putKept(std::vector<uint8_t>& out, const sim::Kept& one);
bool keptFrom(const std::vector<uint8_t>& bytes, sim::Kept& out, int layout = kKeptLayout);

// The realm after a step, as one number: what the tick said (every happening's bytes, which
// Realm::say zeroes padding and all), the dice drawn so far, and where each player stands. Two
// realms that agree on it for every tick are walking the same walk.
uint64_t stateHash(const sim::Realm& realm);

}  // namespace mu::net
