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
// And the character screen's, before any world (server-plan phase 6, docs/sprints/23-the-account.md):
//
//   client -> server   Account  his account's key, and the characters this machine played there
//                               before accounts, to be claimed onto it
//                      Create   a character of a name and a class
//                      Delete   one of his, by its token
//   server -> client   Roster   the account's characters as the screen stands them, and how the
//                               last ask went
//
// And in a world: Who, the players in it by their bodies' ids -- each one's name and whether he is
// a bot -- sent to everyone there whenever someone is welcomed. What the hover plate over another
// player says (game/ui/vitals.h); the realm knows no names, so nothing of it is in the lockstep.
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
// 9: a thing on the ground carries its owner (sim::Lying::owner), so the snapshot's lying grew.
// 10: accounts -- Account, Roster, Create, Delete, and the Hello's `account`.
// 11: Who, the players' names; the Hello's `name`, a character of no account's.
// 12: a Tick's `invasionElsewhere`, in the rain's byte; and the raid in every invasion, so the
//     snapshot's Body, Hazard and RaidState grew.
// 13: the Welcome's snapshot is packed (net::pack): about 1/25th of it, a Noria of 1.7 MB in 69 KB;
//     and the Welcome carries the map's copy and the line's UDP key
//     (docs/sprints/24-the-quiet-line.md).
// 14: a Kept carries 25 quests, Devin's errand to Tersia the 25th (layout 5).
constexpr uint32_t kVersion = 14;
// The server the game plays on when it is not told another (server/README.md): the Hetzner box.
constexpr const char* kDefaultHost = "37.27.158.226";
// The shape of a character's bytes (putKept), apart from the protocol's: what the server's store
// keeps beside each row. 3 is protocol 3's; 4 adds the way back; 5 a 25th quest (Devin's errand to
// Tersia, 2026-10-09), where 3 and 4 carry 24.
constexpr int kKeptLayout = 5;
// MU's GameServer listened on 55901; ours is its own.
constexpr int kDefaultPort = 44406;
// The longest frame a client accepts: a Welcome with its snapshot, about half a MB for a town of
// three hundred bodies. And the longest a server accepts from a client, whose frames are a
// Hello and commands, so a line cannot make it hold megabytes.
constexpr uint32_t kMostFrame = 16u << 20;
constexpr uint32_t kMostAsked = 64u << 10;

enum class Kind : uint8_t { Hello = 1, Welcome = 2, Command = 3, Tick = 4, Hash = 5, Elsewhere = 6,
                          Ping = 7, Account = 8, Roster = 9, Create = 10, Delete = 11, Who = 12 };

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
    // His account's key (Account), which a character of an account is played only with. Empty
    // for the bench's door: a character of no account, as the bots and `--new` make.
    std::string account;
    // What a character of no account is called over his head (net::Who): a bot's name. An
    // account's character is called what the account made him, and this is not read.
    std::string name;
};

// The players in a world, by body id (net::Who).
struct Who {
    struct One {
        uint32_t id = 0;
        std::string name;
        bool bot = false;
    };
    std::vector<One> players;
};

// A character this machine played on the server before accounts, by the token kept beside his
// save: claimed onto the account when it is nobody's yet.
struct Claim {
    uint64_t token = 0;
    std::string name;
    int32_t slot = -1;
};

struct Account {
    uint32_t version = kVersion;
    std::string key;  // the account's secret: kMostKey letters, made once on the machine
    std::vector<Claim> claims;
};

struct Create {
    std::string name;
    uint8_t kin = 0;
};

struct Delete {
    uint64_t token = 0;
};

// How the last ask went, in the screen's own words (game/ui/lobby.h).
enum class Refused : uint8_t { None = 0, Taken = 1, NoRoom = 2, BadName = 3, NotYours = 4, Playing = 5 };

// One character as the screen stands him: on his pedestal, in what he wears.
struct Seat {
    uint64_t token = 0;   // what the Hello plays him with
    int32_t slot = 0;     // his pedestal, 0 to 4
    std::string name;
    uint8_t kin = 0;
    int32_t level = 1;
    bool second = false;  // his class's second (sim::promoted)
    std::string world;    // where he comes in
    struct Worn {
        uint8_t slot = 0;  // below sim::kWorn
        sim::Held held;
    };
    std::vector<Worn> worn;
};

struct Roster {
    Refused refused = Refused::None;
    std::vector<Seat> seats;
};

// The longest key an Account carries.
constexpr size_t kMostKey = 64;

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
    // The world as it stood when the server last snapshot it (Realm::snapshot), packed (pack),
    // empty for none: the mirror raised from the rest is laid with it, and `backlog` is the
    // ticks since.
    // Blood Castle's number when the world is one (Realm::setCastle, straight after the raise), or 0.
    int32_t castle = 0;
    // Which copy of the map, from 1: a map past the server's cap opens another (sprint 24).
    uint8_t copy = 1;
    // His key for the tick datagrams (sprint 24): the client says it from its UDP socket, and the
    // server sends the ticks there as well as down the stream. 0 for a server without them.
    uint64_t udpKey = 0;
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
    // And whether the Golden Dragon is on another map of the server's now: one dragon at a time
    // (the user, 2026-10-08), so a storm beginning here does not roll for it. In the rain's byte.
    bool invasionElsewhere = false;
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
void put(std::vector<uint8_t>& out, const Account& one);
void put(std::vector<uint8_t>& out, const Roster& one);
void put(std::vector<uint8_t>& out, const Create& one);
void put(std::vector<uint8_t>& out, const Delete& one);
void put(std::vector<uint8_t>& out, const Who& one);

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
bool parse(const std::vector<uint8_t>& body, Account& out);
bool parse(const std::vector<uint8_t>& body, Roster& out);
bool parse(const std::vector<uint8_t>& body, Create& out);
bool parse(const std::vector<uint8_t>& body, Delete& out);
bool parse(const std::vector<uint8_t>& body, Who& out);

// A character alone, in the bytes a Welcome or a Tick carries him in: what the server's character
// store keeps (server/src/store.h). `keptFrom` is false unless the bytes are exactly one.
void putKept(std::vector<uint8_t>& out, const sim::Kept& one);
bool keptFrom(const std::vector<uint8_t>& bytes, sim::Kept& out, int layout = kKeptLayout);

// The realm after a step, as one number: what the tick said (every happening's bytes, which
// Realm::say zeroes padding and all), the dice drawn so far, and where each player stands. Two
// realms that agree on it for every tick are walking the same walk.
uint64_t stateHash(const sim::Realm& realm);

// **The tick datagrams** (sprint 24). A client says its Welcome's `udpKey` from its UDP socket
// once a second (a Bind): the server learns where to send, and the home router keeps the way
// open. Each tick the server then sends a Ticks datagram -- the magic, then the Tick frames of
// the last few ticks, oldest first, as many as fit in kMostDatagram -- beside the stream. The
// client takes each tick from whichever brings it first, so a packet the stream lost and must
// send again is not a stall: the next datagram already carried that tick.
constexpr uint32_t kBindMagic = 0x3142554Du;   // "MUB1"
constexpr uint32_t kTicksMagic = 0x3154554Du;  // "MUT1"
// Under any line's MTU, so a datagram is never cut in pieces (net::kMostDatagram's twin: the
// wire does not include the socket).
constexpr size_t kMostDatagramBytes = 1200;
void putBind(std::vector<uint8_t>& out, uint64_t key);
bool takeBind(const std::vector<uint8_t>& datagram, uint64_t& key);
// The Ticks datagram from Tick frames (each a whole `put(out, Tick)`), newest last: the newest
// always, then older ones while they fit. False when even the newest alone does not fit.
bool putTicks(std::vector<uint8_t>& out, const std::vector<const std::vector<uint8_t>*>& frames);
// Its frames, for `take` -- false if it is not one.
bool ticksOf(const std::vector<uint8_t>& datagram, std::vector<uint8_t>& frames);

// A snapshot for the line: deflated (zlib, its fastest level), behind its own length. A realm's
// bytes are mostly zeroes and repeats -- 538 KB of Lorencia packs into 23 KB. `unpack` is false
// for bytes that are not one.
bool pack(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out);
bool unpack(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out);

}  // namespace mu::net
