#pragma once

// What the server's files share: the clock, its constants, its globals and the types every job
// reads -- World, Session, Party -- and each job's functions. Split out of a single main.cpp of
// 1850 lines on 2026-10-11 (docs/order-plan.md sprint C), code moved verbatim: chat.cpp, party.cpp,
// accounts.cpp, world.cpp (raise, hello, the tick, landing, keeping) and main.cpp (flags, the
// frame dispatch, the loop). The file comment that was main.cpp's is still main.cpp's.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <ctime>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <poll.h>
#ifdef __linux__
#include <sys/resource.h>
#endif

#include "content/tables.h"
#include "core/log.h"
#include "net/socket.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/quests.h"
#include "sim/gates.h"
#include "sim/maps.h"
#include "sim/travel.h"
#include "sim/rain.h"
#include "sim/realm.h"
#include "store.h"

namespace mu::server {

using namespace mu;

inline volatile std::sig_atomic_t g_stop = 0;
inline void onSignal(int) { g_stop = 1; }

using Clock = std::chrono::steady_clock;
constexpr double kTickSeconds = 0.05;  // 20 Hz, the realm's own (sim/realm.h)
// A tick's wall time: 50 ms, or less under --speed N, a test's world run N times as fast (the
// Claude bot's, tools/netbot/claude.h: "you need to find a way how massively to speed up
// things"). The clients follow whatever ticks come; nothing in the realm reads the wall.
inline Clock::duration g_tick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kTickSeconds));
// **And the wall clock the ticks carry, N times as fast** from the server's start: Blood Castle's
// door and a quest's twelve-hour rest read it (Realm::wallClock), and at --speed 100 a castle
// party stood at the Messenger for game hours while the door kept real minutes.
inline int g_speed = 1;
inline const int64_t g_wallStart = int64_t(std::time(nullptr));
inline int64_t wallNow() { return g_wallStart + (int64_t(std::time(nullptr)) - g_wallStart) * g_speed; }
// An order that sets a standing player off takes his world's tick at once, and the world's clock
// starts again from there: what the client does for a click on its own (game/play.cpp), done
// where the ticks are made. It was 0 to 50 ms, 25 on average, between his order reaching the
// server and the tick that carries it -- the one wait in the walk the line does not set.
// Invention, and it runs the world a little fast: one interval cut short at most this often
// per world, so never more than 1/20 faster (the user, 2026-10-08, agreed). Not when the
// deadline is nearly here anyway (kEarlyWithin): that would cut short for nothing.
constexpr double kEarlyApart = 1.0;
inline const Clock::duration kEarlyWithin = std::chrono::milliseconds(5);
constexpr int kHashEvery = 20;         // a hash a second
// A world is snapshot this often, and its past before the snapshot let go: a newcomer replays at
// most this much (docs/sprints/21-the-snapshot.md). About half a MB a town, taken in a millisecond.
// 20 s, from a minute (sprint 24): a client stepped 1200 ticks to come in, a second and more at
// 100 players, where now it steps 400.
constexpr int kSnapshotEvery = 20 * 20;
// A Join's ticket, the server's own: high, so it is never one a client numbered.
constexpr uint32_t kJoinTickets = 0x80000000u;
// Whoever is in a world is written this often as well as when he leaves: OpenMU's rate, and what
// a crash of the server can cost him. Each on his own minute (Session::keepAt): about 20 us a
// character on the box, so 500 at once was a 10 ms tick.
constexpr int kKeepEvery = 60 * 20;
// A line that has taken none of what the server owes it for this long, or owes more than this,
// is let go: a client that stopped reading would otherwise hold a tick's bytes a tick, forever.
// The most a healthy line owes is a Welcome, a few MB, and that keeps moving.
constexpr double kStalledSeconds = 20.0;
constexpr size_t kMostOwed = 64u << 20;
// A line silent this long is dead (Session::heardAt): his next Hello takes his character over.
constexpr auto kSilentFor = std::chrono::seconds(10);
// The most commands one player puts in one tick: a hand's worth of clicks, keys and the bot's
// orders many times over. More, and the rest of that tick's are dropped -- every one is sent to
// everyone in his world, and one client could otherwise flood the rest off their lines.
constexpr int kMostCommands = 64;
// How long after Devil Square's fight starts one who passed Charon's gate before it shut is still
// let into it (hello): the trip's load and a slow line.
constexpr int kSquareLate = 20;

// The characters, by token, on the server's disk.
inline server::Store g_store;
// The item rows a Roster's worn things are read against, and a new character's cradle weapon
// found in: Lorencia's, as the client's screen dresses its pedestals (an item's row is the same
// in every world's tables).
inline content::Tables g_items;

// Where a character is due next, by token: set when the realm sends him to another world, and
// written as where he is when his connection goes (placeOf).
struct Landing {
    std::string world;
    int column = 0, row = 0;
    int castle = 0;  // into Blood Castle: which one his ticket passed him into
    bool summon = false;  // her summon comes too: a Tab trip's (Realm::travel), no gate's
    int square = 0, squareWait = 0;  // into Devil Square: which, and the seconds of Charon's gate left
};
inline std::map<uint64_t, Landing> g_due;
// Tests (2026-10-08): `--storm` raises every world in a wet spell, `--invasion` begins the Golden
// Invasion as a world is raised on a map that has one -- so a client joining mid-storm and
// mid-invasion can be tried at once rather than after a dry spell and the dice.
inline bool g_storm = false;
inline bool g_invasion = false;
// A test (2026-10-11, Devil Square's server path): `--kit LEVEL:STEM[+N],...` makes every new
// character at that level with those things in his bag, carried in as one from another world is,
// so Charon's door can be played on a server without a day of levelling first. 0 for none.
inline int g_kitLevel = 0;
inline std::vector<std::pair<std::string, int>> g_kitItems;  // stem, plus
// The most players in one copy of a map before the next is opened (`--map-cap N`): where the
// client's own cost tells -- every client steps the whole map -- about 100 by the measurement of
// 2026-10-08 (docs/sprints/24-the-quiet-line.md).
inline int g_mapCap = 100;
// The tick datagrams' socket, on the game's port; closed if it would not bind, and then the
// stream alone carries the ticks, as before sprint 24.
inline net::Datagram g_udp;
// How many recent ticks a datagram may carry: four, a fifth of a second of a line's losses.
constexpr size_t kRecentTicks = 4;

// ---- the server's own timing (docs/sprints/25-the-clear-thread.md) -----------------------------
// One line a minute of how the loop kept time, and one for any pass over kSlowPass naming where
// it went: so a player's "lag" can be laid at the server's door, or not. The client logs its own
// side (RemoteLink's slow answers, Play's long frames).
constexpr double kSlowPass = 10.0;  // ms
constexpr auto kStatsEvery = std::chrono::seconds(60);
inline double msSince(Clock::time_point at) { return std::chrono::duration<double, std::milli>(Clock::now() - at).count(); }
// This thread's own CPU time, in ms: a long pass that used little of it was waiting for the
// machine -- another process, the host's steal on a shared box -- not working.
inline double cpuMs() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return double(now.tv_sec) * 1000.0 + double(now.tv_nsec) / 1e6;
}
struct Stats {
    Clock::time_point since = Clock::now();
    int ticks = 0;
    double tickMs = 0.0, tickMost = 0.0;
    std::string tickMostWorld;
    double passMost = 0.0;
    int slowPasses = 0;
    int slowWaited = 0;  // of those, the ones the machine held up: under half the pass on the CPU
    // A tick stepped after its deadline: how late the latest, and how many a whole tick or more.
    double lateMost = 0.0;
    int behind = 0;
    uint64_t streamBytes = 0, udpBytes = 0;
    int saved = 0;
    double saveMost = 0.0;
    double snapshotMost = 0.0;
    int packs = 0;
};
inline Stats g_stats;

struct World {
    std::string name;
    content::Tables tables;  // owned: the realm keeps a pointer to them
    std::unique_ptr<sim::Realm> realm;
    net::Welcome start;      // how it was raised: what every mirror raises from
    // The world as it last stood in a snapshot (Realm::snapshot), empty before the first, and
    // every tick since it: what a newcomer's mirror is laid with and then replays.
    std::vector<uint8_t> snapshot;
    std::vector<uint8_t> packed;  // `snapshot` packed for the line (net::pack)
    std::vector<net::Tick> log;
    // The next snapshot, being packed on a worker (sprint 25): until it is done the last one,
    // its pack and the past since it stand, and a newcomer is welcomed with those. Packing was
    // 3-5 ms on the Mac at 100 players, in a newcomer's welcome, on the thread every world ticks on.
    struct Packed {
        std::vector<uint8_t> snapshot, packed;
        uint32_t tick = 0;  // the tick it stands after
    };
    std::future<Packed> packing;
    int sinceSnapshot = 0;
    std::vector<sim::Command> queued;  // arrived since the last tick, in order
    std::vector<net::Arrival> arriving;  // characters carried in with this tick's Joins
    uint32_t nextTicket = kJoinTickets;
    std::vector<uint32_t> orphans;  // Joins whose connection went before they were answered
    int castle = 0;  // Blood Castle's number, 0 for any other map: one world a castle
    // Devil Square's number, 0 for any other map: one world a square's run, which everyone
    // through Charon's gate joins while it waits, and the next run a new one (sim/square.h).
    int square = 0;
    // Which copy of its map, from 1 (sprint 24): a map past g_mapCap players opens another, as
    // MU's sub-servers did, because every client steps its whole map and a crowded one is slow
    // to come into and to draw.
    int copy = 1;
    // The last few ticks' frames, oldest first, for the datagrams (net::putTicks): each carries
    // as many as fit, so one datagram lost costs nothing while the next arrives.
    std::deque<std::vector<uint8_t>> recent;
    // Its own deadline, so one world's early tick moves no other's clock.
    Clock::time_point next = Clock::now();
    Clock::time_point earlyAt{};  // when it last took a tick early
    bool wantsEarly = false;      // an order this poll that set a standing player off

    // The rain's clock (sim/rain.h): `rain` goes out with every tick.
    sim::RainClock weather;
};

struct Session {
    net::Socket socket;
    std::vector<uint8_t> in;
    std::string who;  // for the log: the order it came in, and later his name
    World* world = nullptr;
    uint32_t player = 0;   // his body's id, once welcomed
    uint32_t joining = 0;  // his Join's ticket while it waits for its tick
    bool welcomed = false;
    uint64_t token = 0;    // his character's: kept under it when he goes
    std::string account;   // the character screen's, once an Account has been said
    std::string name;      // what he is called over his head (net::Who)
    bool bot = false;      // a character of no account: for now, the bots (tools/netbot)
    // The realm sent him to another world (g_due) and this line has not gone yet: what he asks
    // here now is a lagging client's, and dropped. His next line, to the world he is due in, is
    // a new Session.
    bool sentAway = false;
    // When he is written next: a minute after his welcome and every minute on, so each player's
    // write falls on his own tick and a full server's never lands on one (sprint 24).
    Clock::time_point keepAt{};
    // The tick datagrams (sprint 24): his key, said in his Welcome and back from his UDP socket
    // (net::takeBind), and where that socket is once it has said it.
    uint64_t udpKey = 0;
    net::Address udpAt;
    bool udpKnown = false;
    // The guilds' marks this line has been sent (GuildMark), so each goes once.
    std::vector<uint32_t> marksSent;
    // The chat's allowance (sayAllowed): lines he may say at once, refilled with time.
    float sayAllowance = 0.0f;
    Clock::time_point saidAt{};
    // When anything last came in on his line. Every client pings at least every 5 s
    // (RemoteLink::pump), so one silent for kSilentFor is a dead line his next Hello may take.
    Clock::time_point heardAt = Clock::now();
    // Commands taken from him for the coming tick (kMostCommands), counted afresh each tick.
    int asked = 0;
};

// ---- the party (docs/party.md): its table ------------------------------------------------------
// The parties are the server's, as WebZen's gParty is: a party spans maps, and a map is a realm.
// Each realm learns its own members' party from Command::Kind::PartySet (partyTick), and shares a
// kill's experience and quest count among those near it. Kept by character token, so a member
// keeps his place across a map change, which is a new connection; one whose character is on no
// connection for kPartyAway is let go, as a logout leaves a party in MU.
struct Party {
    struct Member {
        uint64_t token = 0;
        std::string name;
    };
    uint32_t id = 0;
    std::vector<Member> members;  // the leader first
};
inline std::map<uint32_t, Party> g_parties;
inline std::map<uint64_t, uint32_t> g_partyOf;  // a member's token -> his party
// The latest invite to a player, by his token: who asked, and when; it stands kInviteFor.
struct PartyInvite {
    uint64_t from = 0;
    Clock::time_point at{};
};
inline std::map<uint64_t, PartyInvite> g_invites;
// Where each member was last put in a party in a realm: his world and body, so a new one (a map
// change) is told again. And since when a member has been on no connection.
inline std::map<uint64_t, std::pair<const World*, uint32_t>> g_partyBody;
inline std::map<uint64_t, Clock::time_point> g_partyAway;
inline uint32_t g_nextParty = 1;
constexpr auto kInviteFor = std::chrono::seconds(60);
constexpr auto kPartyAway = std::chrono::seconds(30);

// ---- the guild (docs/guild.md): its table -------------------------------------------------------
// The guilds are the server's, as WebZen's ExDB kept them: a guild spans maps and outlives a
// login, so it is kept on the disk (Store::keepGuild) whenever it changes. By character token,
// as the party is. The realm knows nothing of them: a guild is names, a mark and a chat.
struct Guild {
    using Member = Store::GuildRow::Member;
    uint32_t id = 0;
    std::string name;
    net::GuildMarkBits mark{};
    std::string notice;
    int64_t made = 0;
    std::vector<Member> members;  // the master first
    bool dirty = false;           // its state owed to its members (guildTick)
};
inline std::map<uint32_t, Guild> g_guilds;
inline std::map<uint64_t, uint32_t> g_guildOf;  // a member's token -> his guild
// A call out: a master's invite, by the invited's token, or a player's ask, by the asker's.
struct GuildCalled {
    uint64_t from = 0;  // the master who invited; for an ask, the master asked
    uint32_t guild = 0;
    std::string name;   // the invited's name, or the asker's
    Clock::time_point at{};
};
inline std::map<uint64_t, GuildCalled> g_guildInvites;
inline std::map<uint64_t, GuildCalled> g_guildAsks;
inline uint32_t g_nextGuild = 1;
constexpr auto kGuildCallFor = std::chrono::seconds(60);  // as a party's invite
// MU's (docs/guild.md): level 100 to found one (GS Gamemain.cpp:3435), 6 to join (protocol.cpp
// :11738), the master's level / 10 members (:11820) and 40 at most (GuildClass.h:16), a notice
// of 60 (define.h:4232), a name of 4 to 8 (MuMain NewUIGuildMakeWindow.cpp:21).
constexpr int kGuildFoundLevel = 100, kGuildJoinLevel = 6, kGuildMost = 40;
constexpr size_t kGuildNoticeMost = 60, kGuildNameLeast = 4, kGuildNameMost = 8;
// How near the Guild Master (sim::kGuildMaster) a founder must stand, in tiles: his Talk's reach
// and a little, since the window opens where the talk ended.
constexpr float kGuildMasterReach = 8.0f;

// ---- the chat ---------------------------------------------------------------------------------
// Lines said on a channel (net::Say) passed on as net::Heard, outside the lockstep as Who is: the
// realm knows no names. Each line goes to the server's log too.

// A burst of five lines, then one each 1.5 s: enough to talk, too little to flood a town.
constexpr float kSayBurst = 5.0f, kSayEvery = 1.5f;

// ---- the jobs' functions, by file ----

// world.cpp
void bye(Session& one, net::Why why);
void announce(const World& world, const std::vector<std::unique_ptr<Session>>& sessions);
bool worldName(const std::string& name);
World* raiseWorld(std::vector<std::unique_ptr<World>>& worlds, const net::Hello& hello,
    const std::string& assets, const sim::RealmConfig& config, uint64_t seed,
    const std::string& who, const sim::Kept* kept, int castle, int square = 0, int squareWait = 0);
bool welcome(Session& one, uint32_t player);
int peopleIn(const World& world, const std::vector<std::unique_ptr<Session>>& sessions);
bool hello(Session& one, const net::Hello& asked, std::vector<std::unique_ptr<World>>& worlds,
    std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
    const sim::RealmConfig& config, std::mt19937_64& seeds);
std::optional<Landing> landingOf(const World& world, const sim::Happening& said,
    const std::vector<sim::Command>& commands);
Session* playing(const World& world, std::vector<std::unique_ptr<Session>>& sessions, uint32_t id);
bool invadedElsewhere(const World& world, const std::vector<std::unique_ptr<World>>& worlds);
void takePacked(World& world);
void tick(World& world, std::vector<std::unique_ptr<Session>>& sessions,
    const std::vector<std::unique_ptr<World>>& worlds, bool early = false);
server::Store::Row placeOf(const Session& one);
void part(Session& one);
void keepEveryone(std::vector<std::unique_ptr<Session>>& sessions);

// chat.cpp
void hearLine(Session& one, const net::Heard& line);
void chatSystem(Session& one, net::Channel channel, const std::string& text);
Session* playerNamed(const std::string& name, std::vector<std::unique_ptr<Session>>& sessions);
bool nameInUse(const std::string& name, const std::vector<std::unique_ptr<Session>>& sessions,
    const Session* except);
bool sayAllowed(Session& one);
void say(Session& one, const net::Say& said, std::vector<std::unique_ptr<Session>>& sessions);

// party.cpp
std::vector<Session*> partyMates(Session& one, std::vector<std::unique_ptr<Session>>& sessions);
Session* sessionOfToken(uint64_t token, std::vector<std::unique_ptr<Session>>& sessions);
void partySet(Session& one, uint32_t party);
void partyLine(const Party& party, const std::string& text,
    std::vector<std::unique_ptr<Session>>& sessions);
void sendPartyState(Session& to, const net::PartyState& state);
void sendParty(const Party& party, std::vector<std::unique_ptr<Session>>& sessions);
void partyOut(uint64_t token, const std::string& why,
    std::vector<std::unique_ptr<Session>>& sessions);
void partyAsk(Session& one, const net::PartyAsk& ask,
    std::vector<std::unique_ptr<Session>>& sessions);
void partyTick(std::vector<std::unique_ptr<Session>>& sessions);

// guild.cpp
void loadGuilds();
uint32_t guildOf(uint64_t token);
std::vector<Session*> guildMates(Session& one, std::vector<std::unique_ptr<Session>>& sessions);
void sendMarks(Session& to, const std::vector<uint32_t>& guilds);
void guildAsk(Session& one, const net::GuildAsk& ask, std::vector<std::unique_ptr<Session>>& sessions);
void guildArrived(Session& one, std::vector<std::unique_ptr<Session>>& sessions);
void guildTick(std::vector<std::unique_ptr<Session>>& sessions);

// accounts.cpp
bool keyShape(const std::string& key);
bool inPlay(const std::vector<std::unique_ptr<Session>>& sessions, uint64_t token);
net::Roster rosterOf(const std::string& account, net::Refused refused);
bool answer(Session& one, net::Refused refused);
int freeSlot(const std::string& account);
bool account(Session& one, const net::Account& said);
bool create(Session& one, const net::Create& asked, std::mt19937_64& seeds,
    const std::vector<std::unique_ptr<Session>>& sessions);
bool drop(Session& one, const net::Delete& asked,
    const std::vector<std::unique_ptr<Session>>& sessions);

// main.cpp
bool hear(Session& one, std::vector<std::unique_ptr<World>>& worlds,
    std::vector<std::unique_ptr<Session>>& sessions, const std::string& assets,
    const sim::RealmConfig& config, std::mt19937_64& seeds);

}  // namespace mu::server
