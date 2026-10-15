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
// And the chat, outside the lockstep as Who is (the chat mockup,
// claude.ai/artifact/AMHkjGrUWpJVkPmAbuDvGX): one kind both ways, Chat --
//
//   client -> server   Say    a line on a channel: the map he is on, his party, his guild, or
//                             a whisper to a player by name, wherever he is
//   server -> client   Heard  a line to show: who said it on which channel, or, with no one's
//                             name, the server's own line on that channel ("Valeria has joined
//                             the party.", "Brakka is not here.")
//
// A frame is a u32 length (of what follows), a u8 kind and the body, little-endian throughout.
// A frame that does not parse drops that connection, never the server (server-plan §3).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sim/command.h"
#include "sim/config.h"
#include "sim/cradle.h"
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
// 15: Chat, a Say up and a Heard down.
// 16: the party (docs/game/party.md): a PartyAsk up and a PartyState down, and the realm's
//     Command::Kind::PartySet and Body::party (snapshot shape 2).
// 17: a Kept carries the perch he sat or leant on (layout 6).
// 18: the trade between players (sim/realm_deal.cpp): Command::Kind::DealAsk and its fellows, and
//     a Player's trade in the snapshot (shape 4).
// 19: Devil Square on the server: the Welcome's square and its wait (Realm::setSquare), the
//     config's square clock (RealmConfig::square), Command::Kind::EnterSquare.
// 20: one realm's rules for players together: Blood Castle's quotas once a kill and its win its
//     winner's (CastleRun::winner, snapshot shape 6), a trade called off as one side leaves,
//     a leaver's threat forgotten.
// 21: a player's place taken again kPlaceRest after he left (Player::goneAt, snapshot shape 7),
//     nothing picked up while a trade is open.
// 22: a kill shared as WebZen's: the drop to the most damage, the experience by damage outside a
//     party (Body::hurt, snapshot shape 8).
// 23: Bye, why the server is closing his line, sent just before it does.
// 24: auto-drink (sim::AutoDrink): Command::Kind::AutoDrink, a Kept's lines (layout 7) and a
//     Player's (snapshot shape 9).
// 25: the Broker (docs/game/broker.md): a BrokerAsk up and a BrokerView down (net/broker_wire.h),
//     Command::Kind::BrokerTake and its fellows, and a Player's Broker (snapshot shape 10).
// 26: a Player's event fields as one EventSeat (snapshot shape 11; order-plan §9e, I4).
// 27: a Broker row's last sale of the same thing (BrokerRow::lastSale), the Buy list's "vs last
//     sale" (the market's redesign B, 2026-10-12).
// 28: a bleeder's claws (sim kBleeders): Body::bleed and its fellows, Happening::bled (snapshot
//     shape 12), the Forest and Bloody Orcs' blow a stacking bleed in place of their poison.
// 29: the guild (docs/game/guild.md): a GuildAsk up, a GuildState, GuildMark and GuildCall down, and
//     each player's guild in Who, after the players.
// 30: the raid (docs/game/party.md): PartyWhat::Raid and ::Unraid, PartyState::raid after the members'
//     health, and up to sim::kRaidMost members in a PartyState; and Invasion, where the Golden
//     Dragon is on the server, to every client on every map.
// 31: the website's accounts (docs/plans/launcher.md): an Account's key may be the launcher's
//     sign-in token, and a Roster refused SignIn when the website does not keep it.
// 32: the game's pass (net/pass.h, docs/plans/security.md) where the token went, in an Account's
//     key and a Hello's account: the token itself no longer crosses the line.
constexpr uint32_t kVersion = 32;
// The server the game plays on when it is not told another (server/README.md): the Hetzner box,
// by its name, so the game talks to mu-classic.com as the launcher does, not to a bare address.
constexpr const char* kDefaultHost = "mu-classic.com";
// The shape of a character's bytes (putKept), apart from the protocol's: what the server's store
// keeps beside each row. 3 is protocol 3's; 4 adds the way back; 5 a 25th quest (Devin's errand to
// Tersia, 2026-10-09), where 3 and 4 carry 24; 6 the perch he logged out on (2026-10-09); 7 his auto-drink lines (2026-10-11).
constexpr int kKeptLayout = 7;
// MU's GameServer listened on 55901; ours is its own.
constexpr int kDefaultPort = 44406;
// The longest frame a client accepts: a Welcome with its snapshot, about half a MB for a town of
// three hundred bodies. And the longest a server accepts from a client, whose frames are a
// Hello and commands, so a line cannot make it hold megabytes.
constexpr uint32_t kMostFrame = 16u << 20;
constexpr uint32_t kMostAsked = 64u << 10;

enum class Kind : uint8_t { Hello = 1, Welcome = 2, Command = 3, Tick = 4, Hash = 5, Elsewhere = 6,
                          Ping = 7, Account = 8, Roster = 9, Create = 10, Delete = 11, Who = 12,
                          Chat = 13, PartyAsk = 14, PartyState = 15, Bye = 16,
                          BrokerAsk = 17, BrokerView = 18, GuildAsk = 19, GuildState = 20,
                          GuildMark = 21, GuildCall = 22, Invasion = 23 };
// The last kind there is: a frame of a higher one is not our protocol (take).
constexpr Kind kLastKind = Kind::Invasion;

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
        // His guild's id (GuildMark names it), 0 for none. Sent after the players.
        uint32_t guild = 0;
    };
    std::vector<One> players;
};

// **The chat** (Kind::Chat). Where a line goes: everyone on his map (the world and its copy), his
// party, his guild, or one player by name. A line with no speaker is the server's on that
// channel; Server is its own notices, which belong to no channel.
enum class Channel : uint8_t { Map = 0, Party = 1, Guild = 2, Whisper = 3, Server = 4 };
constexpr uint8_t kChannels = 5;
// The longest line said, in bytes: about two lines of the chat window. The server cuts a longer
// one (saidText), so the wire's 255-byte strings always carry it whole.
constexpr size_t kMostSaid = 200;

// Up: what he said, and for a whisper to whom.
struct Say {
    Channel channel = Channel::Map;
    std::string to;
    std::string text;
};

// Down: a line to show. `from` is the speaker's body in the hearer's own world, 0 when he is
// elsewhere or it is the server's line; `to`, on a whisper's copy back to its sayer, whom it went
// to ("To Valeria").
struct Heard {
    Channel channel = Channel::Map;
    uint32_t from = 0;
    std::string name;
    std::string to;
    std::string text;
    bool bot = false;
};

// ---- the party (docs/game/party.md) ---------------------------------------------------------------
// Up: what a player asks of the party. Invite and Kick name a player, Accept and Decline the one
// who invited him (empty for the latest invite), Leave nobody. The server keeps the parties: they
// span maps, and a map is a realm.
// Promote: the leader hands the party to the member named (ours; WebZen's party has no such ask).
// Raid: the leader makes his party a raid of up to sim::kRaidMost; Unraid makes it a party again,
// while it has no more than sim::kPartyMost (ours, as WoW's "Convert to raid").
enum class PartyWhat : uint8_t { Invite = 0, Accept = 1, Decline = 2, Leave = 3, Kick = 4, Promote = 5, Raid = 6, Unraid = 7 };
struct PartyAsk {
    PartyWhat what = PartyWhat::Invite;
    std::string name;
};
// Down: his party as it stands, to every member when it changes and each second for the bar;
// no members when he is in none. The leader first. `here` is the member's body in the hearer's
// own world, 0 when he is on another map.
struct PartyState {
    struct Member {
        std::string name;
        uint8_t kin = 0;
        int32_t level = 1;
        std::string world;
        uint32_t here = 0;
        uint8_t health = 0;  // percent
        uint8_t mana = 0;    // percent
        bool bot = false;
        // SD, percent; kNoShield when he has none, or from a server older than the field. Sent
        // after the members, so a client reads an older server's PartyState as before.
        uint8_t shield = 255;
        // His health in points, and its most: -1 from a server older than them. After the
        // shields, the same way.
        int32_t life = -1;
        int32_t lifeMost = -1;
    };
    static constexpr uint8_t kNoShield = 255;
    std::vector<Member> members;
    // A raid, not a party (PartyWhat::Raid): it takes up to sim::kRaidMost.
    bool raid = false;
};

// ---- the guild (docs/game/guild.md) ---------------------------------------------------------------
// The server keeps the guilds, as WebZen's ExDB did: a guild spans maps and outlives a login.
// Its mark is MU's: 8x8 pixels of 4 bits, two a byte, the high nibble first, from a palette of
// 16 whose first is clear (MuMain ZzzInventory.cpp CreateGuildMark).
constexpr size_t kGuildMarkBytes = 32;
using GuildMarkBits = std::array<uint8_t, kGuildMarkBytes>;
// A mark's pixel, 0 to 15, at column `x` and row `y`.
inline int guildPixel(const GuildMarkBits& mark, int x, int y) {
    const uint8_t two = mark[size_t((y * 8 + x) / 2)];
    return (x % 2) == 0 ? two >> 4 : two & 0x0F;
}
// Up: what a player asks of a guild. Found names the guild (`name`) and gives its mark; Invite a
// player to his guild; Ask to join the guild of the master named; Accept and Decline answer a
// call by its caller's name (empty for the latest); Remove names a member; Notice gives `text`.
enum class GuildWhat : uint8_t { Found = 0, Invite = 1, Ask = 2, Accept = 3, Decline = 4, Leave = 5,
                                 Remove = 6, Disband = 7, Notice = 8 };
struct GuildAsk {
    GuildWhat what = GuildWhat::Invite;
    std::string name;
    std::string text;
    GuildMarkBits mark{};
};
// Down: his guild as it stands, to every member online when it changes and each few seconds;
// id 0 when he is in none. The master first. `here` is the member's body in the hearer's own
// world, 0 elsewhere; `seen` how many seconds ago an offline member last played.
struct GuildState {
    struct Member {
        std::string name;
        uint8_t kin = 0;
        int32_t level = 1;
        std::string world;  // empty while offline
        uint32_t here = 0;
        int32_t seen = 0;
        bool bot = false;
    };
    uint32_t id = 0;
    std::string name;
    GuildMarkBits mark{};
    std::string notice;
    int32_t cap = 0;
    std::vector<Member> members;
};
// Down: a guild's name and mark, once to each client that sees one of its members (Who's guild).
struct GuildMark {
    uint32_t id = 0;
    std::string name;
    GuildMarkBits mark{};
    std::string master;  // its master's name: whom a player asks to join (the player menu)
};
// Down: the card to answer (game/ui/chat.h's invite card): a master's invite to him, or, to a
// master, a player asking to join. `from` is the caller; the rest says what joining means.
struct GuildCall {
    bool ask = false;  // a player asks to join his guild, rather than an invite to one
    std::string from;
    uint8_t fromKin = 0;
    int32_t fromLevel = 1;
    std::string fromWorld;
    bool fromInParty = false;
    uint32_t guild = 0;
    std::string guildName;
    GuildMarkBits mark{};
    int32_t members = 0, cap = 0;
    std::string master;
    uint8_t masterKin = 0;
    int32_t masterLevel = 1;
};

// A said line made fit to pass on: control bytes gone, spaces trimmed, cut to kMostSaid at a
// whole UTF-8 letter. Empty when nothing is left.
std::string saidText(const std::string& text);

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
enum class Refused : uint8_t { None = 0, Taken = 1, NoRoom = 2, BadName = 3, NotYours = 4, Playing = 5,
                               SignIn = 6 };

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
    // And Devil Square's number and its wait in seconds when the world is one (Realm::setSquare,
    // straight after the raise), or 0: one world a square's run, everyone through Charon's gate.
    int32_t square = 0, squareWait = 0;
    // Which copy of the map, from 1: a map past the server's cap opens another (sprint 24).
    uint8_t copy = 1;
    // His key for the tick datagrams (sprint 24): the client says it from its UDP socket, and the
    // server sends the ticks there as well as down the stream. 0 for a server without them.
    uint64_t udpKey = 0;
    std::vector<uint8_t> snapshot;
};

// What raising the world takes, read off a Welcome (sim::raiseFrom): the server raises its own
// realm from the Welcome it is about to send, and every mirror from the one it was sent.
inline sim::RaiseOrder raiseOrder(const Welcome& w) {
    sim::RaiseOrder order;
    order.seed = w.seed;
    order.column = w.column;
    order.row = w.row;
    order.kin = sim::Kin(w.kin);
    order.level = w.level;
    order.config = w.config;
    order.castle = w.castle;
    order.square = w.square;
    order.squareWait = w.squareWait;
    order.weapon = w.weapon;
    order.shield = w.shield;
    order.kept = w.kept ? &w.first : nullptr;
    return order;
}

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

// Down, the last frame on a line the server closes itself: why, for the client's Disconnected
// window (the user, 2026-10-11: "when there is disconnects on server for client we need to show
// reason"). A line that dies without one -- the box gone, the way to it -- the client names
// for itself (RemoteLink::cutWhy).
enum class Why : uint8_t {
    Unknown = 0,
    Stopping = 1,    // the server is shutting down: a restart or an update
    Elsewhere = 2,   // his character was logged in from another connection
    Protocol = 3,    // a frame the server could not read, or out of turn
    Behind = 4,      // he took nothing for too long and fell too far behind
};
struct Bye {
    Why why = Why::Unknown;
};

// Down: where the Golden Invasion is on the server (sim/invasion.h), to every client on every map
// whenever it changes and once at his welcome -- a client mirrors only his own map, so a dragon
// landed on another is known only from this (the user, 2026-10-13: "when dragon fight is
// happening show on map raid"; the big map and the travel list mark it). One at a time on the
// server (invadedElsewhere). Quiet for none; while Entering or Standing, the map and its copy
// it comes down on, and the tile it lands on (Body::homeColumn, homeRow). Outside the lockstep.
struct Invasion {
    sim::InvasionPhase phase = sim::InvasionPhase::Quiet;
    uint32_t map = 0;
    uint8_t copy = 1;
    int16_t column = 0, row = 0;
    bool operator==(const Invasion& o) const {
        return phase == o.phase && map == o.map && copy == o.copy && column == o.column && row == o.row;
    }
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
void put(std::vector<uint8_t>& out, const Say& one);
void put(std::vector<uint8_t>& out, const Heard& one);
void put(std::vector<uint8_t>& out, const PartyAsk& one);
void put(std::vector<uint8_t>& out, const PartyState& one);
void put(std::vector<uint8_t>& out, const GuildAsk& one);
void put(std::vector<uint8_t>& out, const GuildState& one);
void put(std::vector<uint8_t>& out, const GuildMark& one);
void put(std::vector<uint8_t>& out, const GuildCall& one);
void put(std::vector<uint8_t>& out, const Bye& one);
void put(std::vector<uint8_t>& out, const Invasion& one);

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
bool parse(const std::vector<uint8_t>& body, Bye& out);
bool parse(const std::vector<uint8_t>& body, Invasion& out);
// A Chat frame is a Say on the server and a Heard on a client.
bool parse(const std::vector<uint8_t>& body, Say& out);
bool parse(const std::vector<uint8_t>& body, Heard& out);
bool parse(const std::vector<uint8_t>& body, PartyAsk& out);
bool parse(const std::vector<uint8_t>& body, PartyState& out);
bool parse(const std::vector<uint8_t>& body, GuildAsk& out);
bool parse(const std::vector<uint8_t>& body, GuildState& out);
bool parse(const std::vector<uint8_t>& body, GuildMark& out);
bool parse(const std::vector<uint8_t>& body, GuildCall& out);

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
